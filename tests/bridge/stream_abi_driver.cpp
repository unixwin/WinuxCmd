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
///             empty} x {tac, base64, b2sum, sha224sum, sha384sum} — the
///             README "Validation" matrix, replayed at the C++ bridge
///             layer (digest commands wired through the streaming entry
///             by the #1488 digest-family adoption).
///   errors    host-stops-feeding (-1) => status 124 (GNU timeout(1)
///             convention); host read failure (-2) => the command's own
///             read-error rc (tac: 1 silent; digests: 1 silent — the
///             GNU-exact closed-stdin shape for sha224sum/sha384sum/tac);
///             NULL callback => empty stdin.
///   stdin     the REAL-stdin entry (run_command_with_stdin_streaming)
///             against the v1 real-stdin entry (run_command_with_stdin):
///             byte-identical stdout/stderr/rc when fd 0 is an anonymous
///             pipe and when it is a redirected file, for {tac, b2sum,
///             sha224sum, sha384sum}.
///   mem       64 MiB ledger: peak commit charge sampled with
///             GetProcessMemoryInfo in self-children; the streaming run
///             must not hold the whole input the v1 slurp + ABI copy hold
///             (Rust-side ledger: ~2x input for tac, README "Measured
///             ledger"), for {tac, sha224sum, b2sum}.
///   output    streaming output ABI v3 (winuxcmd_run_io, host write
///             callbacks): chunking (line-buffered `seq 3` = 3 callbacks,
///             block-buffered = 1), stderr callback before the buffered
///             stdout flush, a -1 (broken pipe) / -2 stdout callback stops
///             `seq inf` after exactly one call — driven ABI-direct with
///             recording callbacks; then the bridge at the process
///             boundary: `seq inf` into a pipe whose reader leaves after
///             line 1 exits 141 promptly, `seq 3` with a closed stdout
///             keeps the #1368 diagnostic + rc 1 (`true` stays silent rc
///             0), stderr/stdout interleave in program order on one
///             shared file, and a 40 MiB `seq` output runs in bounded
///             memory.
///
///   probe     (not a ctest lane) product-level probe channel for an
///             arbitrary exe, e.g. old vs new winuxcmd.exe:
///             `probe head1 <exe> args...` (stdout pipe, reader leaves
///             after the first line), `probe drain <exe> args...` (read
///             all, report the last line + bytes + wall time + the
///             child's peak working set), `probe closed <exe> args...`
///             (no stdout handle, the `>&-` shape). `--stdin FILE` before
///             the mode feeds a file as stdin (`--stdin '<closed>'`: no
///             stdin handle, the `<&-` shape).
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
#include "shared/rust2_abi.h"

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
                 "\xe4\xb8\x96\xe7\x95\x8c"  // U+4E16 U+754C
                 "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81"
                 "\xaf"  // konnichiha
                 "\n"
                 "\xf0\x9f\x98\x80\xf0\x9f\x9a\x80"  // U+1F600 U+1F680
                 " emoji line\n"
                 "\xc3\xa9\xc3\xa8 caf\xc3\xa9\n"});  // e-acute x2
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
  size_t chunk = 0;           // max bytes per fill
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
auto run_child(const std::vector<std::string>& args, std::string_view payload,
               bool stdin_via_file = false) -> ChildRun {
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
  HANDLE out_file =
      CreateFileW(out_path.c_str(), GENERIC_WRITE, 0, &inherit, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_TEMPORARY, nullptr);
  HANDLE err_file =
      CreateFileW(err_path.c_str(), GENERIC_WRITE, 0, &inherit, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_TEMPORARY, nullptr);

  // stdin payload staging: either the pipe write end or the redirected
  // file (written up front, then opened for reading).
  HANDLE child_stdin_write = nullptr;
  HANDLE child_stdin_read = nullptr;
  if (stdin_via_file) {
    {
      std::ofstream f(in_path, std::ios::binary);
      f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    child_stdin_read =
        CreateFileW(in_path.c_str(), GENERIC_READ, 0, &inherit, OPEN_EXISTING,
                    FILE_ATTRIBUTE_TEMPORARY, nullptr);
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
      CreateProcessA(g_self_exe.string().c_str(), cmd.data(), nullptr, nullptr,
                     TRUE, 0, nullptr, nullptr, &si, &pi);
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
        const DWORD want =
            static_cast<DWORD>(std::min<size_t>(rest.size(), 1u << 20));
        if (!WriteFile(child_stdin_write, rest.data(), want, &wrote, nullptr) ||
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
  return winux::rust2::run_command_with_stdin_source(name, args, &seg_read, &s);
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

// `run <cmd> [args...]`: the pass-nothing bridge entry (run_command) with
// the given operands — the shape of every no-stdin wired command.
// `memrun <report> <cmd> [args...]`: same + the peak memory report.
auto child_run_modes(const std::string& sub,
                     const std::vector<std::string>& rest) -> int {
  const size_t first = (sub == "memrun") ? 3 : 2;
  if (rest.size() < first) return 2;
  const char* name = rest[first - 1].c_str();
  std::vector<std::string_view> args;
  for (size_t i = first; i < rest.size(); ++i) args.emplace_back(rest[i]);
  const int status = winux::rust2::run_command(name, args);
  if (sub == "memrun") write_mem_report(std::filesystem::path(rest[1]));
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
  if (sub == "run" || sub == "memrun") return child_run_modes(sub, rest);
  return 2;
}

// ---- parent lanes --------------------------------------------------------

// The commands covered by the lanes: tac + base64 (the #1556 matrix) and
// the digest family wired through the streaming entry by the #1488
// digest-family adoption.
constexpr std::array<std::string_view, 5> kStreamCmds{"tac", "base64", "b2sum",
                                                      "sha224sum", "sha384sum"};

// The child argv slices passed to run_child; argv[0] (this exe) is added
// by run_child itself.
auto lane_segments() -> void {
  const auto payloads = make_payloads();
  const auto& cmds = kStreamCmds;
  const std::array<unsigned long long, 4> seg_sizes{1, 7, 997, 65537};
  for (const auto cmd : cmds) {
    for (const auto& p : payloads) {
      const ChildRun v1 =
          run_child({"--child", "bytes", std::string(cmd)}, p.bytes);
      for (const auto seg : seg_sizes) {
        const ChildRun v2 = run_child(
            {"--child", "seg", std::string(cmd), std::to_string(seg)}, p.bytes);
        compare_children("segments " + std::string(cmd) + " " +
                             std::string(p.name) + " " + std::to_string(seg) +
                             "B-chunks vs v1",
                         v1, v2);
      }
      const ChildRun v2full =
          run_child({"--child", "full", std::string(cmd)}, p.bytes);
      compare_children("segments " + std::string(cmd) + " " +
                           std::string(p.name) + " full-fill vs v1",
                       v1, v2full);
      if (p.bytes.empty()) {
        // NULL callback == empty stdin: only meaningful against the empty
        // payload (the child ignores its stdin in this mode).
        const ChildRun v2null =
            run_child({"--child", "null", std::string(cmd)}, p.bytes);
        compare_children(
            "segments " + std::string(cmd) + " null-callback vs v1-empty", v1,
            v2null);
      }
    }
  }
}

auto lane_errors() -> void {
  // Host stops feeding: 124 regardless of the command's own status (the
  // flag is tracked at the StreamReader wrapper, not inferred from the
  // status — README "Timeout convention").
  for (const auto cmd : kStreamCmds) {
    const ChildRun stopped =
        run_child({"--child", "stop", std::string(cmd)}, "unused-by-child");
    record(stopped.spawned && stopped.exit_code == 124,
           "errors " + std::string(cmd) + " host-stop (-1) => 124");
  }
  // Host read failure (-2) propagates per each command's own contract
  // (README callback table; rust2 stream_tests host_io_error_surfaces_as_
  // command_rc): tac maps the stdin read error to rc 1 with no output and
  // no wording for the "-" input (io::copy err -> failed flag) — and the
  // digestsum family does the same, which is GNU-exact for
  // sha224sum/sha384sum/tac on a closed stdin (probed coreutils 9.4:
  // silent rc 1) and a known missing-diagnostic gap for b2sum (GNU prints
  // "b2sum: -: Bad file descriptor"; command-side wording, out of bridge
  // scope); base64 propagates through `?`, which the boundary maps to 125.
  for (const auto cmd : {"tac", "b2sum", "sha224sum", "sha384sum"}) {
    const ChildRun err =
        run_child({"--child", "ioerr", std::string(cmd)}, "unused-by-child");
    record(err.exit_code == 1 && err.out.empty() && err.err.empty(),
           "errors " + std::string(cmd) + " read-failure (-2) => rc " +
               std::to_string(err.exit_code) + ", no output/wording");
  }
  const ChildRun b64_err =
      run_child({"--child", "ioerr", "base64"}, "unused-by-child");
  record(b64_err.exit_code == 125, "errors base64 read-failure (-2) => rc " +
                                       std::to_string(b64_err.exit_code) +
                                       " (Io -> 125 mapping)");
}

auto lane_stdin() -> void {
  const auto payloads = make_payloads();
  for (const auto& p : payloads) {
    for (const auto cmd : {"tac", "b2sum", "sha224sum", "sha384sum"}) {
      // Pipe face.
      const ChildRun v1 = run_child({"--child", "real", "v1", std::string(cmd)},
                                    p.bytes, false);
      const ChildRun v2 = run_child({"--child", "real", "v2", std::string(cmd)},
                                    p.bytes, false);
      compare_children("stdin pipe " + std::string(cmd) + " " +
                           std::string(p.name) + " streaming vs v1",
                       v1, v2);
      // Redirected-file face.
      const ChildRun v1f =
          run_child({"--child", "real", "v1", std::string(cmd)}, p.bytes, true);
      const ChildRun v2f =
          run_child({"--child", "real", "v2", std::string(cmd)}, p.bytes, true);
      compare_children("stdin file " + std::string(cmd) + " " +
                           std::string(p.name) + " streaming vs v1",
                       v1f, v2f);
    }
  }
}

auto lane_mem(size_t mib) -> void {
  const std::string payload = make_big_payload(mib);
  const unsigned long long margin =
      static_cast<unsigned long long>(mib) * 1024 * 1024;
  const auto mib_of = [](unsigned long long b) {
    return static_cast<double>(b) / (1024.0 * 1024.0);
  };
  for (std::string_view cmd : {"tac", "sha224sum", "b2sum"}) {
    const auto report_v1 = make_temp_path("mem-v1");
    const auto report_v2 = make_temp_path("mem-v2");
    const ChildRun c1 = run_child(
        {"--child", "memprobe", "v1", std::string(cmd), report_v1.string()},
        payload);
    const ChildRun c2 = run_child(
        {"--child", "memprobe", "v2", std::string(cmd), report_v2.string()},
        payload);
    record(c1.spawned && c1.exit_code == 0,
           "mem v1 " + std::string(cmd) + " " + std::to_string(mib) +
               "MiB run (exit " + std::to_string(c1.exit_code) + ")");
    record(c2.spawned && c2.exit_code == 0,
           "mem v2 " + std::string(cmd) + " " + std::to_string(mib) +
               "MiB run (exit " + std::to_string(c2.exit_code) + ")");
    const MemPeak p1 = read_mem_report(report_v1);
    const MemPeak p2 = read_mem_report(report_v2);
    std::fprintf(
        stderr,
        "INFO mem ledger %zuMiB input, %.*s: v1 "
        "peak_pagefile=%.1f MiB, v2=%.1f MiB, gap=%.1f MiB "
        "(workingset v1=%.1f, v2=%.1f)\n",
        mib, static_cast<int>(cmd.size()), cmd.data(), mib_of(p1.pagefile),
        mib_of(p2.pagefile),
        mib_of(p1.pagefile > p2.pagefile ? p1.pagefile - p2.pagefile : 0),
        mib_of(p1.workingset), mib_of(p2.workingset));
    // Residency gate, per command family. The v1 run holds the slurp + the
    // ABI stdin copy + the command buffers; v2 drops the first two.
    // * tac (buffers ~2x input across its input + reversed-output Vecs):
    //   the removed slurp+ABI stay visible at the commit-charge peak
    //   (measured gap 82-84 MiB, ~1.3x input) — assert >= 1x input on
    //   pagefile.
    // * digest family (digestsum io::copy holds one whole-input Vec in
    //   BOTH entries — command code unchanged): measured 64 MiB ledger,
    //   3 runs, v1 213.7 -> v2 194.4 pagefile (gap 19.2 MiB, ~0.3x input)
    //   but v1 199.4 -> v2 134.3 workingset (gap ~65 MiB, ~1x input = the
    //   removed slurp+ABI). The small pagefile gap is the command Vec's
    //   own growth: under v1, io::copy routes to Cursor::read_to_end,
    //   which try_reserve()s the input EXACTLY (std 1.95 cursor.rs
    //   read_to_end), while under v2 the unknown-length stream takes the
    //   default read_to_end probe loop and the Vec's amortized doubling
    //   transiently commits ~1.5x input (std copy.rs: BufferedWriterSpec
    //   for Vec<u8> -> read_to_end). The residency win is real (workingset)
    //   and the commit-charge peak still moves down; assert both facts so
    //   a regression that reintroduces whole-buffer residency fails the
    //   lane.
    const bool tac_margin = p1.pagefile > p2.pagefile + margin;
    const bool digest_margin =
        p1.pagefile > p2.pagefile && p1.workingset > p2.workingset + margin / 2;
    const bool is_tac = cmd == "tac";
    record(c1.exit_code == 0 && c2.exit_code == 0 &&
               (is_tac ? tac_margin : digest_margin),
           "mem v2 " + std::string(cmd) + " below v1 by " +
               (is_tac ? ">= 1x input (pagefile)"
                       : ">= 0.5x input (workingset; pagefile strictly "
                         "below)"));
  }
}

// ---- output lane (ABI v3 streaming output) -------------------------------

// Recording host for winuxcmd_run_io: one log for both streams so the
// cross-stream callback order is observable.
struct OutEvent {
  bool is_stdout = true;
  std::string bytes;
};

struct OutRecorder {
  std::vector<OutEvent> events;
  int stdout_rc = 0;
  int stdout_calls = 0;
};

auto rec_stdout(void* ud, const unsigned char* buf, size_t len) -> int {
  auto* r = static_cast<OutRecorder*>(ud);
  ++r->stdout_calls;
  r->events.push_back(
      {true, std::string(reinterpret_cast<const char*>(buf), len)});
  return r->stdout_rc;
}

auto rec_stderr(void* ud, const unsigned char* buf, size_t len) -> int {
  auto* r = static_cast<OutRecorder*>(ud);
  r->events.push_back(
      {false, std::string(reinterpret_cast<const char*>(buf), len)});
  return 0;
}

// ABI-direct run (no bridge): explicit argv, empty env, the current cwd.
auto abi_run_io(const char* name, const std::vector<std::string>& args,
                OutRecorder& rec, unsigned flags) -> int {
  std::vector<const char*> argv;
  for (const auto& a : args) argv.push_back(a.c_str());
  const char* envp[] = {nullptr};
  const std::string cwd = std::filesystem::current_path().string();
  const winuxcmd_io_v3 io{nullptr,     nullptr, &rec_stdout, &rec,
                          &rec_stderr, &rec,    flags};
  return winuxcmd_run_io(name, static_cast<int>(argv.size()),
                         argv.empty() ? nullptr : argv.data(), envp,
                         cwd.c_str(), &io);
}

auto stdout_chunks(const OutRecorder& r) -> std::vector<std::string> {
  std::vector<std::string> out;
  for (const auto& e : r.events) {
    if (e.is_stdout) out.push_back(e.bytes);
  }
  return out;
}

// Spawn `cmdline` with stdout = a pipe the parent reads; the parent stops
// reading (and closes its end) once `stop_after_newlines` newlines were
// seen (0 = read to EOF). stderr goes to a temp file. `stdin_file` (may be
// empty) is opened as the child's stdin. Returns the child's exit code
// (STILL_ACTIVE-style 259 / -1 never expected), the bytes read, the wall
// time, and the child's peak working set / commit charge.
struct PipeRun {
  bool spawned = false;
  bool timed_out = false;
  DWORD exit_code = 0;
  std::string head;       // head mode: the bytes up to the Nth newline
  std::string last_line;  // drain mode: the last complete line
  unsigned long long bytes = 0;
  double ms = 0;
  unsigned long long peak_ws = 0;
  unsigned long long peak_pagefile = 0;
  std::string err;
};

auto quote_arg(const std::string& a) -> std::string { return "\"" + a + "\""; }

auto run_pipe(const std::string& exe, const std::vector<std::string>& args,
              size_t stop_after_newlines, const std::string& stdin_file,
              DWORD timeout_ms = 60000) -> PipeRun {
  PipeRun run;
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE out_r = nullptr;
  HANDLE out_w = nullptr;
  if (!CreatePipe(&out_r, &out_w, &inherit, 0)) return run;
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  const auto err_path = make_temp_path("pipe-err");
  HANDLE err_file =
      CreateFileW(err_path.c_str(), GENERIC_WRITE, 0, &inherit, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_TEMPORARY, nullptr);
  HANDLE in_h = nullptr;
  if (stdin_file == "<closed>") {
    in_h = nullptr;  // the `<&-` shape: no stdin handle at all
  } else if (!stdin_file.empty()) {
    in_h = CreateFileA(stdin_file.c_str(), GENERIC_READ, FILE_SHARE_READ,
                       &inherit, OPEN_EXISTING, 0, nullptr);
  } else {
    in_h = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &inherit,
                       OPEN_EXISTING, 0, nullptr);
  }
  std::string cmd = quote_arg(exe);
  for (const auto& a : args) cmd += " " + quote_arg(a);
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_h;
  si.hStdOutput = out_w;
  si.hStdError = err_file;
  PROCESS_INFORMATION pi{};
  const auto t0 = std::chrono::steady_clock::now();
  const BOOL ok = CreateProcessA(exe.c_str(), cmd.data(), nullptr, nullptr,
                                 TRUE, 0, nullptr, nullptr, &si, &pi);
  CloseHandle(out_w);
  if (in_h != nullptr && in_h != INVALID_HANDLE_VALUE) CloseHandle(in_h);
  if (!ok) {
    CloseHandle(out_r);
    CloseHandle(err_file);
    return run;
  }
  run.spawned = true;
  CloseHandle(pi.hThread);
  std::vector<char> buf(1 << 16);
  size_t newlines = 0;
  std::string tail;
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(out_r, buf.data(), static_cast<DWORD>(buf.size()), &got,
                  nullptr) ||
        got == 0) {
      break;
    }
    run.bytes += got;
    std::string_view chunk(buf.data(), got);
    if (stop_after_newlines != 0) {
      // Model `head -N`: keep the bytes up to the Nth newline, then leave.
      bool done = false;
      for (const char c : chunk) {
        run.head.push_back(c);
        if (c == '\n' && ++newlines == stop_after_newlines) {
          done = true;
          break;
        }
      }
      if (done) break;
    } else {
      // Model `tail -1`: only the end of the stream is kept.
      tail.append(chunk);
      if (tail.size() > 16384) tail.erase(0, tail.size() - 8192);
    }
  }
  CloseHandle(out_r);  // the reader leaves (head) / saw EOF (drain)
  if (WaitForSingleObject(pi.hProcess, timeout_ms) != WAIT_OBJECT_0) {
    run.timed_out = true;
    TerminateProcess(pi.hProcess, 255);
    WaitForSingleObject(pi.hProcess, 10000);
  }
  run.ms = std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
               .count();
  GetExitCodeProcess(pi.hProcess, &run.exit_code);
  PROCESS_MEMORY_COUNTERS pmc{};
  pmc.cb = sizeof(pmc);
  if (GetProcessMemoryInfo(pi.hProcess, &pmc, sizeof(pmc))) {
    run.peak_ws = pmc.PeakWorkingSetSize;
    run.peak_pagefile = pmc.PeakPagefileUsage;
  }
  CloseHandle(pi.hProcess);
  CloseHandle(err_file);
  if (stop_after_newlines == 0) {
    if (!tail.empty() && tail.back() == '\n') tail.pop_back();
    const size_t nl = tail.rfind('\n');
    run.last_line = nl == std::string::npos ? tail : tail.substr(nl + 1);
  }
  run.err = read_file_bytes(err_path);
  std::error_code ec;
  std::filesystem::remove(err_path, ec);
  return run;
}

// Spawn with NO stdout handle (the `>&-` shape, as process_win32's
// run_command_closed_stdout does); stderr to a temp file.
auto run_closed_stdout(const std::string& exe,
                       const std::vector<std::string>& args,
                       const std::string& stdin_file = "") -> ChildRun {
  ChildRun run;
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const auto err_path = make_temp_path("closed-err");
  HANDLE err_file =
      CreateFileW(err_path.c_str(), GENERIC_WRITE, 0, &inherit, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_TEMPORARY, nullptr);
  HANDLE in_h =
      stdin_file.empty()
          ? CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &inherit,
                        OPEN_EXISTING, 0, nullptr)
          : CreateFileA(stdin_file.c_str(), GENERIC_READ, FILE_SHARE_READ,
                        &inherit, OPEN_EXISTING, 0, nullptr);
  std::string cmd = quote_arg(exe);
  for (const auto& a : args) cmd += " " + quote_arg(a);
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_h;
  si.hStdOutput = nullptr;
  si.hStdError = err_file;
  PROCESS_INFORMATION pi{};
  if (CreateProcessA(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0,
                     nullptr, nullptr, &si, &pi)) {
    run.spawned = true;
    CloseHandle(pi.hThread);
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_OBJECT_0) {
      TerminateProcess(pi.hProcess, 255);
      WaitForSingleObject(pi.hProcess, 10000);
    }
    GetExitCodeProcess(pi.hProcess, &run.exit_code);
    CloseHandle(pi.hProcess);
  }
  CloseHandle(in_h);
  CloseHandle(err_file);
  run.err = read_file_bytes(err_path);
  std::error_code ec;
  std::filesystem::remove(err_path, ec);
  return run;
}

// Spawn with stdout AND stderr on the same file (`>f 2>&1`), so the file
// content carries the write order across both streams.
auto run_shared_out_err(const std::vector<std::string>& args) -> ChildRun {
  ChildRun run;
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const auto path = make_temp_path("shared-out");
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  HANDLE in_h = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &inherit,
                            OPEN_EXISTING, 0, nullptr);
  std::string cmd = quote_arg(g_self_exe.string());
  for (const auto& a : args) cmd += " " + quote_arg(a);
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_h;
  si.hStdOutput = f;
  si.hStdError = f;
  PROCESS_INFORMATION pi{};
  if (CreateProcessA(g_self_exe.string().c_str(), cmd.data(), nullptr, nullptr,
                     TRUE, 0, nullptr, nullptr, &si, &pi)) {
    run.spawned = true;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &run.exit_code);
    CloseHandle(pi.hProcess);
  }
  CloseHandle(in_h);
  CloseHandle(f);
  run.out = read_file_bytes(path);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return run;
}

auto lane_output() -> void {
  // (a) chunking. seq writes "1\n2\n3\n" as ONE chunk; line-buffered
  // stdout splits it into one callback per complete line, block-buffered
  // stdout delivers it as one callback at the post-dispatch flush.
  {
    OutRecorder rec;
    const int rc =
        abi_run_io("seq", {"3"}, rec, WINUXCMD_IO_STDOUT_LINE_BUFFERED);
    const auto chunks = stdout_chunks(rec);
    record(rc == 0 && chunks == std::vector<std::string>{"1\n", "2\n", "3\n"},
           "output seq 3 line-buffered => 3 callbacks \"1\\n\",\"2\\n\","
           "\"3\\n\" (got " +
               std::to_string(chunks.size()) + ")");
  }
  {
    OutRecorder rec;
    const int rc = abi_run_io("seq", {"3"}, rec, 0);
    const auto chunks = stdout_chunks(rec);
    record(rc == 0 && chunks == std::vector<std::string>{"1\n2\n3\n"},
           "output seq 3 block-buffered => 1 callback (got " +
               std::to_string(chunks.size()) + ")");
  }
  // (b) stderr is unbuffered: realpath writes "." (stdout, buffered) and
  // then a diagnostic (stderr) mid-run; the stderr callback must fire
  // before the stdout flush.
  {
    OutRecorder rec;
    const int rc = abi_run_io("realpath", {".", "no-such-dir-v3out/x"}, rec, 0);
    const bool order_ok = rec.events.size() >= 2 &&
                          !rec.events.front().is_stdout &&
                          rec.events.back().is_stdout;
    record(rc == 1 && order_ok,
           "output stderr callback fires before the buffered stdout flush "
           "(realpath . missing)");
  }
  // (c) broken pipe: -1 at the first stdout call stops an unbounded
  // producer; the host is called exactly once (sticky failure).
  for (const unsigned flags : {0u, WINUXCMD_IO_STDOUT_LINE_BUFFERED}) {
    OutRecorder rec;
    rec.stdout_rc = -1;
    const auto t0 = std::chrono::steady_clock::now();
    abi_run_io("seq", {"inf"}, rec, flags);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    record(rec.stdout_calls == 1 && ms < 10000,
           "output seq inf stdout -1 (flags=" + std::to_string(flags) +
               ") => returns promptly (" + std::to_string(int(ms)) +
               " ms), callback invoked " + std::to_string(rec.stdout_calls) +
               "x");
  }
  // (d) other write failure: -2, same sticky stop.
  {
    OutRecorder rec;
    rec.stdout_rc = -2;
    abi_run_io("seq", {"inf"}, rec, 0);
    record(rec.stdout_calls == 1,
           "output seq inf stdout -2 => callback invoked " +
               std::to_string(rec.stdout_calls) + "x");
  }

  // Bridge at the process boundary.
  const std::string self = g_self_exe.string();
  // (c') `seq inf | head -1`: the bridge maps the broken pipe to 141.
  {
    const PipeRun r =
        run_pipe(self, {"--child", "run", "seq", "inf"}, 1, "", 30000);
    record(r.spawned && !r.timed_out && r.exit_code == 141 && r.head == "1\n" &&
               r.err.empty(),
           "output bridge seq inf | head -1 => rc " +
               std::to_string(r.exit_code) + " (141), first line \"1\", " +
               std::to_string(int(r.ms)) + " ms");
  }
  // (d') the #1368 closed-stdout contract survives the switch.
  {
    const ChildRun r = run_closed_stdout(self, {"--child", "run", "seq", "3"});
    record(
        r.exit_code == 1 && r.err == "seq: write error: Bad file descriptor\n",
        "output bridge seq 3 >&- => rc " + std::to_string(r.exit_code) +
            ", one 'write error: Bad file descriptor' diagnostic");
    const ChildRun t = run_closed_stdout(self, {"--child", "run", "true"});
    record(t.exit_code == 0 && t.err.empty(),
           "output bridge true >&- => rc 0, silent");
    const ChildRun big =
        run_closed_stdout(self, {"--child", "run", "seq", "10000000"});
    record(big.exit_code == 1 &&
               big.err == "seq: write error: Bad file descriptor\n",
           "output bridge seq 10000000 >&- => rc " +
               std::to_string(big.exit_code) + ", one diagnostic");
  }
  // Interleaving at the process boundary: stdout and stderr on one file;
  // the diagnostic lands before the (block-buffered) stdout bytes — the
  // CBuf drain always wrote stdout first.
  {
    const ChildRun r = run_shared_out_err(
        {"--child", "run", "realpath", ".", "no-such-dir-v3out/x"});
    record(r.exit_code == 1 && r.out.rfind("realpath: ", 0) == 0,
           "output bridge stderr/stdout program order on one shared file");
  }
  // Bounded memory: ~39 MiB of seq output through the bridge into a pipe
  // the parent drains; the CBuf drain held the whole output (plus its
  // console-layer copy), the streaming run holds at most a chunk.
  {
    const auto report = make_temp_path("mem-out");
    const PipeRun r = run_pipe(
        self, {"--child", "memrun", report.string(), "seq", "1", "5000000"}, 0,
        "", 120000);
    const MemPeak p = read_mem_report(report);
    const double mib = static_cast<double>(p.pagefile) / (1024.0 * 1024.0);
    std::fprintf(stderr,
                 "INFO output seq 1 5000000 (%llu bytes): peak_pagefile=%.1f "
                 "MiB, workingset=%.1f MiB\n",
                 r.bytes, mib,
                 static_cast<double>(p.workingset) / (1024.0 * 1024.0));
    record(r.exit_code == 0 && r.last_line == "5000000" &&
               r.bytes == 38888896ull && mib < 24.0,
           "output bridge seq 1 5000000 streams in bounded memory "
           "(peak_pagefile < 24 MiB for a 37 MiB output)");
  }
}

// ---- probe mode (product-level channel, not a ctest lane) ---------------

auto probe_main(const std::vector<std::string>& args) -> int {
  // probe [--stdin FILE] head1|drain|closed <exe> [args...]
  size_t i = 1;
  std::string stdin_file;
  if (i + 1 < args.size() && args[i] == "--stdin") {
    stdin_file = args[i + 1];
    i += 2;
  }
  if (i + 1 >= args.size()) return 2;
  const std::string mode = args[i];
  const std::string exe = args[i + 1];
  const std::vector<std::string> rest(args.begin() + i + 2, args.end());
  if (mode == "closed") {
    const ChildRun r = run_closed_stdout(exe, rest, stdin_file);
    std::printf("rc=%lu\nstderr=%s", static_cast<unsigned long>(r.exit_code),
                r.err.c_str());
    return 0;
  }
  // head1 waits at most 15 s for the producer to die after the reader
  // left (a producer that never notices the broken pipe is killed and
  // reported timed_out=1 — the pre-v3 `seq inf` shape grows its output
  // buffer by GBs per second, so the wait must stay short).
  const PipeRun r = run_pipe(exe, rest, mode == "head1" ? 1 : 0, stdin_file,
                             mode == "head1" ? 15000 : 600000);
  std::printf(
      "rc=%lu timed_out=%d bytes=%llu ms=%.1f peak_ws_mib=%.1f "
      "peak_pagefile_mib=%.1f\n",
      static_cast<unsigned long>(r.exit_code), r.timed_out ? 1 : 0, r.bytes,
      r.ms, static_cast<double>(r.peak_ws) / (1024.0 * 1024.0),
      static_cast<double>(r.peak_pagefile) / (1024.0 * 1024.0));
  if (mode == "head1") {
    std::printf("head=%s", r.head.c_str());
  } else {
    std::printf("last=%s\n", r.last_line.c_str());
  }
  if (!r.err.empty()) std::printf("stderr=%s", r.err.c_str());
  return 0;
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
  if (mode == "probe") return probe_main(args);
  const size_t mem_mib = 64;
  if (mode == "segments" || mode == "all") lane_segments();
  if (mode == "errors" || mode == "all") lane_errors();
  if (mode == "stdin" || mode == "all") lane_stdin();
  if (mode == "mem" || mode == "all") lane_mem(mem_mib);
  if (mode == "output" || mode == "all") lane_output();

  if (g_failures != 0) {
    std::fprintf(stderr, "rust2-stream-driver: %d check(s) FAILED\n",
                 g_failures);
    return 1;
  }
  std::fprintf(stderr, "rust2-stream-driver: all checks passed\n");
  return 0;
}
