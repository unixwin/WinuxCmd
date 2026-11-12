// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Bridge-level harness for the rust2 streaming stdin ABI
///   (v2 adoption of #1556, #1488). The winuxcmd unit tests are
///   process-level (tests/framework Pipeline), and the streaming entry has
///   no wired command yet — so this driver is the entry's first caller and
///   its regression net:
///
///   segments  segmentation matrix: a synthetic pull callback feeding
///             {1 B, 7 B, 997 B, 65537 B} partial fills (plus a full-fill
///             and a NULL-callback empty run) must be byte-identical to
///             the v1 whole-buffer entry across {ASCII, multi-byte UTF-8
///             split mid-character, no trailing newline, embedded NUL,
///             empty} x {tac, base64} — the README "Validation" matrix,
///             replayed at the C++ bridge layer.
///   errors    host-stops-feeding (-1) => status 124 (GNU timeout(1)
///             convention); host read failure (-2) => the command's own
///             read-error rc (tac: 1); NULL callback => empty stdin.
///   stdin     the REAL-stdin entry (run_command_with_stdin_streaming)
///             against the v1 real-stdin entry (run_command_with_stdin):
///             byte-identical stdout/stderr/rc when fd 0 is an anonymous
///             pipe and when it is a redirected file.
///   mem       64 MiB ledger: peak commit charge sampled with
///             GetProcessMemoryInfo in self-children; the streaming run
///             must not hold the whole input the v1 slurp + ABI copy hold
///             (Rust-side ledger: ~2x input for tac, README "Measured
///             ledger").
///
///   The parent mode spawns THIS executable as `--child ...` so every run
///   is captured at the process boundary through the same console drain
///   the wired commands use (safePrintRaw), exactly like the Pipeline
///   tests. Console-face ^D records cannot be scripted non-interactively;
///   their segmentation is carried by file_io::read_stdin_record, the
///   same function the v1 slurp uses, so the stdin lanes cover pipe +
///   redirected-file faces and the console face stays shared-path with v1.
///
/// Compiled in the TOP-LEVEL CMake tree (not tests/, which re-enables
/// /EHsc) with the same flags as the bridge module world: no exceptions,
/// static CRT, RTTI off. Gated on WINUXCMD_ENABLE_RUST2.
/// @Version: 0.1.0
/// @License: MIT

#include "pch/pch.h"

import std;
import utils;
import rust2_bridge;

namespace {

auto g_self_exe = std::filesystem::path();

// ---- payloads ----------------------------------------------------------

struct Payload {
  std::string_view name;
  std::string bytes;
};

auto make_payloads() -> std::vector<Payload> {
  std::vector<Payload> out;
  // Plain ASCII lines (tac reverses them, base64 wraps them).
  out.push_back({"ascii", "alpha line\nbeta line\ngamma line\ndelta line\n"});
  // Multi-byte UTF-8 (CJK + emoji + accented), as explicit UTF-8 bytes so
  // 1-byte segments cut mid-character.
  out.push_back({"utf8",
                 "\xe4\xb8\x96\xe7\x95\x8c"          // U+4E16 U+754C
                 "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81"
                 "\xaf"                                // konnichiha
                 "\n"
                 "\xf0\x9f\x98\x80\xf0\x9f\x9a\x80"   // U+1F600 U+1F680
                 " emoji line\n"
                 "\xc3\xa9\xc3\xa8 caf\xc3\xa9\n"});   // e-acute x2
  // No trailing newline.
  out.push_back({"nonl", "first\nlast without newline"});
  // Embedded NUL bytes (binary-safe).
  out.push_back({"nul", std::string("a\0b\0c\n\0\0tail", 12)});
  // Empty.
  out.push_back({"empty", {}});
  return out;
}

// 64 MiB of newline-separated lines (deterministic content, ~60-char
// lines): the tac memory ledger input.
auto make_big_payload(size_t mib) -> std::string {
  const size_t total = mib * 1024 * 1024;
  std::string out;
  out.reserve(total + 64);
  size_t line_no = 0;
  while (out.size() < total) {
    const auto prefix = std::to_string(line_no++);
    out.append(prefix);
    const size_t pad = 60 - prefix.size();
    for (size_t i = 0; i < pad; ++i) {
      out.push_back(static_cast<char>('a' + (i + line_no) % 26));
    }
    out.push_back('\n');
  }
  return out;
}

// ---- synthetic pull callbacks ------------------------------------------

// Serves a held byte range in fixed-size partial fills. `fail_after` (a
// count of bytes served) turns the next call into -1 (host stops feeding);
// `io_error` makes every call return -2.
struct SegState {
  const unsigned char* data = nullptr;
  size_t len = 0;
  size_t pos = 0;
  size_t chunk = 0;       // max bytes per fill
  long long fail_after = -1;  // -1: never fail; serve this many bytes, then -1
  bool io_error = false;      // true: every call is -2
};

auto seg_read(void* user_data, unsigned char* buf, size_t cap) -> long long {
  auto* s = static_cast<SegState*>(user_data);
  if (s->io_error) return -2;
  if (s->fail_after >= 0 && s->pos >= static_cast<size_t>(s->fail_after)) {
    return -1;
  }
  size_t n = std::min({cap, s->chunk, s->len - s->pos});
  if (s->fail_after >= 0) {
    n = std::min(n, static_cast<size_t>(s->fail_after) - s->pos);
  }
  if (n == 0) return 0;
  std::memcpy(buf, s->data + s->pos, n);
  s->pos += n;
  return static_cast<long long>(n);
}

// ---- parent-side process runner ----------------------------------------

struct ChildRun {
  DWORD exit_code = 0;
  std::string out;
  std::string err;
  bool spawned = false;
};

auto make_temp_path(const std::string& tag) -> std::filesystem::path {
  static unsigned long counter = 0;
  auto dir = std::filesystem::temp_directory_path();
  const auto pid = GetCurrentProcessId();
  return dir / ("rust2-stream-" + std::to_string(pid) + "-" +
                std::to_string(++counter) + "-" + tag + ".tmp");
}

auto read_file_bytes(const std::filesystem::path& p) -> std::string {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  std::string data((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  return data;
}

// Spawn `args` (argv[0] is this executable) with `payload` on stdin via an
// anonymous pipe, stdout/stderr redirected to temp files. `stdin_via_file`
// feeds the same payload through a redirected FILE instead of a pipe (the
// redirected-file face of the real-stdin lane).
auto run_child(const std::vector<std::string>& args,
               std::string_view payload, bool stdin_via_file = false)
    -> ChildRun {
  ChildRun run;
  const std::filesystem::path out_path = make_temp_path("out");
  const std::filesystem::path err_path = make_temp_path("err");
  const std::filesystem::path in_path = make_temp_path("in");

  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

  HANDLE in_read = nullptr;
  HANDLE in_write = nullptr;
  bool pipe_ok = false;
  if (!stdin_via_file) {
    pipe_ok = CreatePipe(&in_read, &in_write, &inherit, 0) != 0;
    if (pipe_ok && in_write != nullptr) {
      // Only the READ end may cross into the child: if the child inherits
      // the write end too, read_all_stdin never observes EOF and the child
      // blocks forever (the classic CreateProcess inheritance deadlock).
      SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    }
  }
  HANDLE out_file = CreateFileW(out_path.c_str(), GENERIC_WRITE, 0, &inherit,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                nullptr);
  HANDLE err_file = CreateFileW(err_path.c_str(), GENERIC_WRITE, 0, &inherit,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                nullptr);

  // stdin payload staging: either the pipe write end or the redirected
  // file (written up front, then opened for reading).
  HANDLE child_stdin_write = nullptr;
  HANDLE child_stdin_read = nullptr;
  if (stdin_via_file) {
    {
      std::ofstream f(in_path, std::ios::binary);
      f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    child_stdin_read = CreateFileW(in_path.c_str(), GENERIC_READ, 0, &inherit,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY,
                                   nullptr);
  } else if (pipe_ok) {
    child_stdin_read = in_read;
    child_stdin_write = in_write;
  }

  std::string cmd = "\"" + g_self_exe.string() + "\"";
  for (const auto& a : args) {
    cmd += " \"" + a + "\"";
  }

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = child_stdin_read;
  si.hStdOutput = out_file;
  si.hStdError = err_file;
  PROCESS_INFORMATION pi{};
  const BOOL ok =
      CreateProcessA(g_self_exe.string().c_str(), cmd.data(), nullptr,
                     nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
  if (ok) {
    run.spawned = true;
    CloseHandle(pi.hThread);
    // The child has its own inherited stdin read handle now.
    if (!stdin_via_file && in_read != nullptr) {
      CloseHandle(in_read);
      in_read = nullptr;
    }
    // Feed the pipe payload, then close so the child sees EOF. The child
    // consumes concurrently (it either slurps or streams), so writes make
    // progress; its stdout/stderr are files, never a blocked pipe back to
    // us.
    if (child_stdin_write != nullptr) {
      std::string_view rest = payload;
      while (!rest.empty()) {
        DWORD wrote = 0;
        const DWORD want = static_cast<DWORD>(
            std::min<size_t>(rest.size(), 1u << 20));
        if (!WriteFile(child_stdin_write, rest.data(), want, &wrote,
                       nullptr) ||
            wrote == 0) {
          break;
        }
        rest.remove_prefix(wrote);
      }
      CloseHandle(child_stdin_write);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &run.exit_code);
    CloseHandle(pi.hProcess);
  }

  if (child_stdin_read != nullptr) CloseHandle(child_stdin_read);
  if (in_read != nullptr) CloseHandle(in_read);
  if (in_write != nullptr && !ok) CloseHandle(in_write);
  if (out_file != nullptr) CloseHandle(out_file);
  if (err_file != nullptr) CloseHandle(err_file);
  run.out = read_file_bytes(out_path);
  run.err = read_file_bytes(err_path);
  std::error_code ec;
  std::filesystem::remove(out_path, ec);
  std::filesystem::remove(err_path, ec);
  std::filesystem::remove(in_path, ec);
  return run;
}

// ---- parent-side checks -------------------------------------------------

int g_failures = 0;

auto record(bool ok, const std::string& what) -> bool {
  if (ok) {
    std::fprintf(stderr, "PASS %s\n", what.c_str());
  } else {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s\n", what.c_str());
  }
  return ok;
}

auto show_diff(const ChildRun& a, const ChildRun& b) -> void {
  std::fprintf(stderr,
               "  v1: exit=%lu out=%zu err=%zu | v2: exit=%lu out=%zu "
               "err=%zu\n",
               static_cast<unsigned long>(a.exit_code), a.out.size(),
               a.err.size(), static_cast<unsigned long>(b.exit_code),
               b.out.size(), b.err.size());
  const size_t n = std::min(a.out.size(), b.out.size());
  for (size_t i = 0; i < n && i < 64; ++i) {
    if (a.out[i] != b.out[i]) {
      std::fprintf(stderr, "  first stdout diff at %zu: %02x vs %02x\n", i,
                   static_cast<unsigned char>(a.out[i]),
                   static_cast<unsigned char>(b.out[i]));
      break;
    }
  }
  if (a.out.size() != b.out.size() && n < 64) {
    std::fprintf(stderr, "  stdout sizes differ\n");
  }
  if (!b.err.empty()) {
    std::fprintf(stderr, "  v2 stderr: %.*s\n",
                 static_cast<int>(std::min<size_t>(b.err.size(), 200)),
                 b.err.c_str());
  }
}

// Byte-for-byte child comparison (stdout, stderr, exit code).
auto compare_children(const std::string& what, const ChildRun& v1,
                      const ChildRun& v2) -> bool {
  const bool ok = v1.spawned && v2.spawned && v1.exit_code == v2.exit_code &&
                  v1.out == v2.out && v1.err == v2.err;
  if (!ok) show_diff(v1, v2);
  return record(ok, what);
}

// ---- mem report ---------------------------------------------------------

struct MemPeak {
  unsigned long long pagefile = 0;
  unsigned long long workingset = 0;
};

auto write_mem_report(const std::filesystem::path& p) -> void {
  PROCESS_MEMORY_COUNTERS pmc{};
  pmc.cb = sizeof(pmc);
  MemPeak peak;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    peak.pagefile = pmc.PeakPagefileUsage;
    peak.workingset = pmc.PeakWorkingSetSize;
  }
  std::ofstream f(p, std::ios::binary);
  // Sampled after the run drained, before exit: the peaks the run reached
  // (pagefile = commit charge peak, the Rust ledger's peak_pagefile_bytes).
  f << "pagefile " << peak.pagefile << "\nworkingset " << peak.workingset
    << "\n";
}

auto read_mem_report(const std::filesystem::path& p) -> MemPeak {
  MemPeak peak;
  std::ifstream f(p, std::ios::binary);
  std::string kind;
  unsigned long long v = 0;
  while (f >> kind >> v) {
    if (kind == "pagefile") peak.pagefile = v;
    if (kind == "workingset") peak.workingset = v;
  }
  std::error_code ec;
  std::filesystem::remove(p, ec);
  return peak;
}

// ---- child modes ---------------------------------------------------------
// The child IS the code under test: it calls the bridge entry named by the
// child mode and exits with the entry's status, so the parent captures the
// drained bytes at the process boundary and the exit code IS the status.

// `bytes <cmd>` / `seg <cmd> <chunk>` / `full <cmd>`: slurp fd 0 once (test
// plumbing so the callback has bytes to serve), then run the v1
// whole-buffer entry or replay it through the v2 pull callback.
auto child_buffered_modes(const std::string& sub,
                          const std::vector<std::string>& rest) -> int {
  if (rest.size() < 2) return 2;
  const char* name = rest[1].c_str();
  std::vector<std::string_view> args{};
  if (sub == "bytes") {
    auto content = file_io::read_all_stdin();
    if (!content) return 2;
    std::span<const unsigned char> bytes(
        reinterpret_cast<const unsigned char*>(content->data()),
        content->size());
    return winux::rust2::run_command_with_stdin_bytes(name, args, bytes);
  }
  if (sub == "seg" || sub == "full") {
    auto content = file_io::read_all_stdin();
    if (!content) return 2;
    SegState s;
    s.data = reinterpret_cast<const unsigned char*>(content->data());
    s.len = content->size();
    s.chunk = (sub == "full") ? std::numeric_limits<size_t>::max()
                              : static_cast<size_t>(std::stoull(rest[2]));
    return winux::rust2::run_command_with_stdin_source(name, args, &seg_read,
                                                       &s);
  }
  if (sub == "null") {
    // NULL stdin_read == empty stdin, regardless of what was piped in.
    return winux::rust2::run_command_with_stdin_source(name, args, nullptr,
                                                       nullptr);
  }
  return 2;
}

// `stop <cmd>`: serve two bytes, then -1 (host stops feeding). `ioerr
// <cmd>`: -2 on the first call. Exit code = the entry's status.
auto child_error_modes(const std::string& sub,
                       const std::vector<std::string>& rest) -> int {
  if (rest.size() < 2) return 2;
  const char* name = rest[1].c_str();
  std::vector<std::string_view> args{};
  static const unsigned char kPrefix[] = {'a', 'b'};
  SegState s;
  s.data = kPrefix;
  s.len = 2;
  s.chunk = 1;
  if (sub == "stop") {
    s.fail_after = 2;
  } else {
    s.io_error = true;
  }
  return winux::rust2::run_command_with_stdin_source(name, args, &seg_read,
                                                     &s);
}

// `real v1|v2 <cmd>`: the REAL-stdin entries on this process's fd 0.
// `memprobe v1|v2 <cmd> <report>`: same + write the peak memory report.
auto child_real_modes(const std::string& sub,
                      const std::vector<std::string>& rest) -> int {
  if (rest.size() < 3) return 2;
  const bool v2 = rest[1] == "v2";
  const char* name = rest[2].c_str();
  std::vector<std::string_view> args{};
  const int status =
      v2 ? winux::rust2::run_command_with_stdin_streaming(name, args)
         : winux::rust2::run_command_with_stdin(name, args);
  if (sub == "memprobe" && rest.size() >= 4) {
    write_mem_report(std::filesystem::path(rest[3]));
  }
  return status;
}

auto child_main(const std::vector<std::string>& args) -> int {
  const std::string& sub = args[1];
  const std::vector<std::string> rest(args.begin() + 1, args.end());
  if (sub == "bytes" || sub == "seg" || sub == "full" || sub == "null") {
    return child_buffered_modes(sub, rest);
  }
  if (sub == "stop" || sub == "ioerr") return child_error_modes(sub, rest);
  if (sub == "real" || sub == "memprobe") {
    return child_real_modes(sub, rest);
  }
  return 2;
}

// ---- parent lanes --------------------------------------------------------

// The child argv slices passed to run_child; argv[0] (this exe) is added
// by run_child itself.
auto lane_segments() -> void {
  const auto payloads = make_payloads();
  const std::array<std::string_view, 2> cmds{"tac", "base64"};
  const std::array<unsigned long long, 4> seg_sizes{1, 7, 997, 65537};
  for (const auto cmd : cmds) {
    for (const auto& p : payloads) {
      const ChildRun v1 =
          run_child({"--child", "bytes", std::string(cmd)}, p.bytes);
      for (const auto seg : seg_sizes) {
        const ChildRun v2 = run_child(
            {"--child", "seg", std::string(cmd), std::to_string(seg)},
            p.bytes);
        compare_children(
            "segments " + std::string(cmd) + " " + std::string(p.name) + " " +
                std::to_string(seg) + "B-chunks vs v1",
            v1, v2);
      }
      const ChildRun v2full =
          run_child({"--child", "full", std::string(cmd)}, p.bytes);
      compare_children(
          "segments " + std::string(cmd) + " " + std::string(p.name) +
              " full-fill vs v1",
          v1, v2full);
      if (p.bytes.empty()) {
        // NULL callback == empty stdin: only meaningful against the empty
        // payload (the child ignores its stdin in this mode).
        const ChildRun v2null =
            run_child({"--child", "null", std::string(cmd)}, p.bytes);
        compare_children(
            "segments " + std::string(cmd) + " null-callback vs v1-empty",
            v1, v2null);
      }
    }
  }
}

auto lane_errors() -> void {
  // Host stops feeding: 124 regardless of the command's own status (the
  // flag is tracked at the StreamReader wrapper, not inferred from the
  // status — README "Timeout convention").
  for (const auto cmd : {"tac", "base64"}) {
    const ChildRun stopped = run_child({"--child", "stop", std::string(cmd)},
                                       "unused-by-child");
    record(stopped.spawned && stopped.exit_code == 124,
           "errors " + std::string(cmd) + " host-stop (-1) => 124");
  }
  // Host read failure (-2) propagates per each command's own contract
  // (README callback table; rust2 stream_tests host_io_error_surfaces_as_
  // command_rc): tac maps the stdin read error to rc 1 with no output and
  // no wording for the "-" input (io::copy err -> failed flag); base64
  // propagates through `?`, which the boundary maps to 125.
  const ChildRun tac_err =
      run_child({"--child", "ioerr", "tac"}, "unused-by-child");
  record(tac_err.exit_code == 1 && tac_err.out.empty() && tac_err.err.empty(),
         "errors tac read-failure (-2) => rc " +
             std::to_string(tac_err.exit_code) + ", no output/wording");
  const ChildRun b64_err =
      run_child({"--child", "ioerr", "base64"}, "unused-by-child");
  record(b64_err.exit_code == 125,
         "errors base64 read-failure (-2) => rc " +
             std::to_string(b64_err.exit_code) + " (Io -> 125 mapping)");
}

auto lane_stdin() -> void {
  const auto payloads = make_payloads();
  for (const auto& p : payloads) {
    // Pipe face.
    const ChildRun v1 =
        run_child({"--child", "real", "v1", "tac"}, p.bytes, false);
    const ChildRun v2 =
        run_child({"--child", "real", "v2", "tac"}, p.bytes, false);
    compare_children(
        "stdin pipe tac " + std::string(p.name) + " streaming vs v1", v1, v2);
    // Redirected-file face.
    const ChildRun v1f =
        run_child({"--child", "real", "v1", "tac"}, p.bytes, true);
    const ChildRun v2f =
        run_child({"--child", "real", "v2", "tac"}, p.bytes, true);
    compare_children(
        "stdin file tac " + std::string(p.name) + " streaming vs v1", v1f,
        v2f);
  }
}

auto lane_mem(size_t mib) -> void {
  const std::string payload = make_big_payload(mib);
  const auto report_v1 = make_temp_path("mem-v1");
  const auto report_v2 = make_temp_path("mem-v2");
  const ChildRun c1 = run_child({"--child", "memprobe", "v1", "tac",
                                 report_v1.string()},
                                payload);
  const ChildRun c2 = run_child({"--child", "memprobe", "v2", "tac",
                                 report_v2.string()},
                                payload);
  record(c1.spawned && c1.exit_code == 0,
         "mem v1 tac " + std::to_string(mib) + "MiB run (exit " +
             std::to_string(c1.exit_code) + ")");
  record(c2.spawned && c2.exit_code == 0,
         "mem v2 tac " + std::to_string(mib) + "MiB run (exit " +
             std::to_string(c2.exit_code) + ")");
  const MemPeak p1 = read_mem_report(report_v1);
  const MemPeak p2 = read_mem_report(report_v2);
  const auto mib_of = [](unsigned long long b) {
    return static_cast<double>(b) / (1024.0 * 1024.0);
  };
  std::fprintf(stderr,
               "INFO mem ledger %zuMiB input, tac: v1 peak_pagefile=%.1f "
               "MiB, v2=%.1f MiB, gap=%.1f MiB (workingset v1=%.1f, "
               "v2=%.1f)\n",
               mib, mib_of(p1.pagefile), mib_of(p2.pagefile),
               mib_of(p1.pagefile - p2.pagefile), mib_of(p1.workingset),
               mib_of(p2.workingset));
  // Rough residency check ("v2 should not hold whole blocks"): the v1 run
  // holds the slurp + the ABI stdin copy + the command buffers; v2 drops
  // the first two (the Rust ledger measured ~2x input for tac). Enforce a
  // conservative 1x-input margin so a regression that reintroduces
  // whole-input residency fails this lane.
  const unsigned long long margin =
      static_cast<unsigned long long>(mib) * 1024 * 1024;
  record(c1.exit_code == 0 && c2.exit_code == 0 &&
             p1.pagefile > p2.pagefile + margin,
         "mem v2 peak_pagefile below v1 by >= 1x input");
}

}  // namespace

auto main(int argc, char** argv) -> int {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (!args.empty() && args[0] == "--child") {
    if (args.size() < 2) return 2;
    return child_main(args);
  }

  wchar_t exe[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  g_self_exe = (n > 0) ? std::filesystem::path(exe) : std::filesystem::path();

  std::string mode = args.empty() ? "all" : args[0];
  const size_t mem_mib = 64;
  if (mode == "segments" || mode == "all") lane_segments();
  if (mode == "errors" || mode == "all") lane_errors();
  if (mode == "stdin" || mode == "all") lane_stdin();
  if (mode == "mem" || mode == "all") lane_mem(mem_mib);

  if (g_failures != 0) {
    std::fprintf(stderr, "rust2-stream-driver: %d check(s) FAILED\n",
                 g_failures);
    return 1;
  }
  std::fprintf(stderr, "rust2-stream-driver: all checks passed\n");
  return 0;
}
