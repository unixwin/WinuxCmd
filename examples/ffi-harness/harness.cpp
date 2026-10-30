// ffi-harness — standalone host for the winuxcore.dll FFI ABI v2 prototype.
//
// This is the DESIGN (ffi-route) section 3.2 / 4.1 loader surface in
// miniature, written against src/ffi/winux_ffi.h only:
//
//   * Layout:    <root>/usr/bin/ffi/current.txt names the active version
//                directory; the DLL is <root>/usr/bin/ffi/<ver>/winuxcore.dll
//                with its tiers.tsv and winuxcore.sha256 beside it.
//   * Discovery: only the explicit install root (argv) or the absolute
//                NIU_WINUXCMD_FFI_DLL override. Never CWD, never PATH.
//   * Integrity: the DLL's SHA-256 must match the pinned value in
//                winuxcore.sha256 before LoadLibraryExW is called, and the
//                DLL is loaded with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
//                LOAD_LIBRARY_SEARCH_SYSTEM32. Loaded once per process.
//   * Switch:    NIU_WINUXCMD_FFI unset or "0" declines every call to the
//                spawn form; "1" enables in-process calls. NIU_WINUXCMD_
//                FFI_EXCLUDE is a comma-separated per-command opt-out.
//   * Tiering:   only commands whose tiers.tsv row says tier1 are called
//                in-process; per-call fallback shapes from the same row
//                (e.g. rm -i/-I) decline the individual call.
//   * Failure:   everything is fail-closed to spawning
//                <root>/usr/bin/winuxcmd.exe, with a distinguishable
//                reason code on stderr (stdout belongs to the command).
//
// Usage: ffi-harness.exe <install-root> <applet> [args...]
//        ffi-harness.exe sequence <install-root> <scratch-dir> [reps]
//                        [applets-csv]
//        (sequence mode: same-process multi-call liveness rotations, no
//        spawn fallback — see run_sequence below.)
// Exit:  the command's exit code; 64 for harness usage errors; 65 when
//        neither the FFI path nor the spawn target could run.

#include <windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr uint32_t kExpectedAbi = 2;  // WINUX_FFI_ABI_VERSION
constexpr int kFfiErrFloor = -1000;   // WINUX_FFI_ERR_GENERIC

// Mirror of src/ffi/winux_ffi.h (prototype harness: parsed, not included,
// so the harness also builds in a bare checkout without the DLL target).
struct FfiInitArgs {
  uint32_t struct_size;
  uint32_t abi_version;
  const wchar_t *install_root;
  const wchar_t *locale_stamp;
};
struct FfiContext {
  uint32_t struct_size;
  uint32_t abi_version;
  const wchar_t *applet;
  int32_t argc;
  const wchar_t *const *argv;
  const wchar_t *env_block;
  const wchar_t *cwd;
  HANDLE stdin_handle;
  HANDLE stdout_handle;
  HANDLE stderr_handle;
  int (*cancel_check)(void *);
  void *cancel_user_data;
};
using FnAbiVersion = uint32_t (*)();
using FnInit = int (*)(const FfiInitArgs *);
using FnCall = int (*)(const FfiContext *);

void diag(const std::wstring &line) {
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  if (!h || h == INVALID_HANDLE_VALUE) return;
  const std::string prefix = "ffi-harness: ";
  DWORD written = 0;
  WriteFile(h, prefix.data(), static_cast<DWORD>(prefix.size()), &written,
            nullptr);
  // Diagnostics are ASCII-safe by construction (paths are printed %ls-free:
  // version dir names and reason codes only).
  std::string narrow;
  narrow.reserve(line.size());
  for (wchar_t c : line) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
  narrow.push_back('\n');
  WriteFile(h, narrow.data(), static_cast<DWORD>(narrow.size()), &written,
            nullptr);
}

std::wstring trim(const std::wstring &s) {
  const size_t b = s.find_first_not_of(L" \t\r\n");
  if (b == std::wstring::npos) return L"";
  const size_t e = s.find_last_not_of(L" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool read_file_text(const std::wstring &path, std::wstring &out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  char buf[8192];
  DWORD got = 0;
  std::string raw;
  while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) {
    raw.append(buf, got);
    if (raw.size() > (1u << 20)) break;
  }
  CloseHandle(h);
  out.assign(raw.begin(), raw.end());
  return true;
}

bool file_exists(const std::wstring &path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES &&
         !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool is_absolute_wpath(const std::wstring &p) {
  if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\') return true;
  return p.size() >= 3 && ((p[0] >= L'a' && p[0] <= L'z') ||
                           (p[0] >= L'A' && p[0] <= L'Z')) &&
         p[1] == L':' && (p[2] == L'\\' || p[2] == L'/');
}

// SHA-256 of a file via BCrypt; lowercase hex on success.
bool sha256_file_hex(const std::wstring &path, std::string &hex_out) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = false;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    return false;
  DWORD hash_len = 0, cb = 0;
  if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len),
                        sizeof(hash_len), &cb, 0) == 0 &&
      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      std::vector<unsigned char> buf(64 * 1024);
      DWORD got = 0;
      ok = true;
      while (ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got,
                      nullptr) &&
             got > 0) {
        if (BCryptHashData(hash, buf.data(), got, 0) != 0) { ok = false; break; }
      }
      CloseHandle(h);
      if (ok) {
        std::vector<unsigned char> digest(hash_len);
        if (BCryptFinishHash(hash, digest.data(), hash_len, 0) == 0) {
          static const char *digits = "0123456789abcdef";
          hex_out.clear();
          for (unsigned char b : digest) {
            hex_out.push_back(digits[b >> 4]);
            hex_out.push_back(digits[b & 0xF]);
          }
        } else {
          ok = false;
        }
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

// First whitespace-delimited token of winuxcore.sha256, lowercased.
std::string pinned_hash(const std::wstring &manifest_path) {
  std::wstring text;
  if (!read_file_text(manifest_path, text)) return {};
  std::string token;
  for (wchar_t c : text) {
    if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
      if (!token.empty()) break;
      continue;
    }
    char ch = c < 128 ? static_cast<char>(c) : '?';
    if (ch >= 'A' && ch <= 'F') ch = static_cast<char>(ch - 'A' + 'a');
    token.push_back(ch);
  }
  return token;
}

struct TierRow {
  bool found = false;
  std::wstring tier;
  std::vector<std::wstring> fallback_shapes;
};

TierRow lookup_tier(const std::wstring &tiers_path, const std::wstring &applet) {
  TierRow row;
  std::wstring text;
  if (!read_file_text(tiers_path, text)) return row;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t eol = text.find(L'\n', pos);
    std::wstring line = text.substr(pos, eol == std::wstring::npos
                                              ? std::wstring::npos
                                              : eol - pos);
    pos = (eol == std::wstring::npos) ? text.size() : eol + 1;
    line = trim(line);
    if (line.empty() || line[0] == L'#') continue;
    // Split into up to 4 tab-separated fields.
    std::vector<std::wstring> fields;
    size_t fp = 0;
    while (fields.size() < 4) {
      const size_t tab = line.find(L'\t', fp);
      if (tab == std::wstring::npos) {
        fields.push_back(trim(line.substr(fp)));
        break;
      }
      fields.push_back(trim(line.substr(fp, tab - fp)));
      fp = tab + 1;
    }
    if (fields.size() < 3 || fields[0] == L"command" || fields[0] != applet)
      continue;
    row.found = true;
    row.tier = fields[1];
    if (fields[2] != L"-" && !fields[2].empty()) {
      size_t sp = 0;
      while (true) {
        const size_t comma = fields[2].find(L',', sp);
        row.fallback_shapes.push_back(trim(fields[2].substr(
            sp, comma == std::wstring::npos ? std::wstring::npos : comma - sp)));
        if (comma == std::wstring::npos) break;
        sp = comma + 1;
      }
    }
    return row;
  }
  return row;
}

// Per-call shape judgment over the argv STRUCTURE (the tiers row lists
// shapes that must decline this individual call, e.g. rm's -i/-I):
//   * "--" ends option territory: every later token is an operand and
//     can never match a shape. (rm -- -weird.txt removes a file whose
//     name happens to contain "i"; it is not an interactive call. An
//     earlier substring heuristic matched the "i" anyway and declined
//     the call — routing only, but the table is load-bearing, so the
//     judgment is structural now.)
//   * An exact shape token matches anywhere before "--".
//   * A single-letter short shape (-i) also matches a short-flag cluster
//     (-ri, -fi), but only when the token is a pure cluster: one leading
//     '-', then ASCII letters only. A token with any non-letter in it
//     (-weird.txt) is not a cluster — its letters are a filename, and a
//     later "i" means nothing.
bool shape_matches(const std::vector<std::wstring> &args,
                   const std::vector<std::wstring> &shapes) {
  for (const std::wstring &arg : args) {
    if (arg == L"--") break;
    for (const std::wstring &shape : shapes) {
      if (arg == shape) return true;
      if (shape.size() == 2 && shape[0] == L'-' && shape[1] != L'-' &&
          arg.size() > 2 && arg[0] == L'-' && arg[1] != L'-') {
        bool pure_cluster = true;
        for (size_t i = 1; i < arg.size(); ++i) {
          const wchar_t c = arg[i];
          if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))) {
            pure_cluster = false;
            break;
          }
        }
        if (pure_cluster && arg.find(shape[1], 1) != std::wstring::npos) {
          return true;
        }
      }
    }
  }
  return false;
}

std::wstring quote_arg(const std::wstring &arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
    return arg;
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') { ++backslashes; continue; }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
    } else {
      out.append(backslashes, L'\\');
      out.push_back(c);
    }
    backslashes = 0;
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

// Spawn form: <root>/usr/bin/winuxcmd.exe <applet> <args...>, inheriting
// this process's std handles and current directory — the reference form
// every FFI decision falls back to.
int run_spawn(const std::wstring &root, const std::wstring &applet,
              const std::vector<std::wstring> &args) {
  const std::wstring exe = root + L"\\usr\\bin\\winuxcmd.exe";
  if (!file_exists(exe)) {
    diag(L"route=none reason=spawn-target-missing");
    return 65;
  }
  std::wstring cmdline = quote_arg(exe) + L" " + quote_arg(applet);
  for (const auto &a : args) cmdline += L" " + quote_arg(a);
  std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
  mutable_cmd.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, nullptr, &si, &pi)) {
    diag(L"route=none reason=spawn-failed err=" +
         std::to_wstring(GetLastError()));
    return 65;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 65;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}

bool env_flag_on(const wchar_t *name) {
  const wchar_t *v = _wgetenv(name);
  return v && v[0] == L'1' && v[1] == L'\0';
}

bool excluded(const std::wstring &applet) {
  const wchar_t *v = _wgetenv(L"NIU_WINUXCMD_FFI_EXCLUDE");
  if (!v) return false;
  std::wstring list(v);
  size_t pos = 0;
  while (pos <= list.size()) {
    const size_t comma = list.find(L',', pos);
    const std::wstring item = trim(list.substr(
        pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos));
    if (item == applet) return true;
    if (comma == std::wstring::npos) break;
    pos = comma + 1;
  }
  return false;
}

std::wstring locale_stamp() {
  for (const wchar_t *name : {L"WINUX_LANG", L"LC_ALL", L"LC_MESSAGES", L"LANG"}) {
    const wchar_t *v = _wgetenv(name);
    if (v && *v) return v;
  }
  return L"";
}

std::string narrow_of(const std::wstring &s) {
  std::string narrow;
  narrow.reserve(s.size());
  for (wchar_t c : s) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
  return narrow;
}

// --- same-process sequence mode ----------------------------------------------
//   ffi-harness.exe sequence <install-root> <scratch-dir> [reps] [applets]
//
// One process, one DLL load, N rotations of in-process calls. This mode
// exists because the one-call-per-process form is blind to a whole class
// of failure: a call that faults inside the DLL and leaves the DLL's
// serialization lock held still produces correct output end-to-end (the
// single-call harness falls back to spawn), so per-call smoke passes
// while the FFI face is dead for the rest of the process. The sequence
// asserts, inside one process, that every call routes in-process (tier1,
// no shape decline), returns the applet's own success, and — via the
// rotation — that calls AFTER an rm still work. There is no spawn
// fallback in this mode: any FFI-layer failure fails the sequence.
// Supported rotation applets and their operands (fixed scenario):
//   touch {f} / cat {f} / cp {f} {f.copy} / rm {f.copy} / chmod {644} {f}
// The scratch file f is primed by the harness before the first rep so a
// rotation starting with cat (the demo doubles implement only cat/cp/rm)
// still has input.

void seq_out(const std::string &line) {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!h || h == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
}

std::vector<std::wstring> split_csv(const std::wstring &csv) {
  std::vector<std::wstring> out;
  size_t pos = 0;
  while (pos <= csv.size()) {
    const size_t comma = csv.find(L',', pos);
    const std::wstring item = trim(csv.substr(
        pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos));
    if (!item.empty()) out.push_back(item);
    if (comma == std::wstring::npos) break;
    pos = comma + 1;
  }
  return out;
}

bool seq_args_for(const std::wstring &applet, const std::wstring &fname,
                  const std::wstring &fcopy,
                  std::vector<std::wstring> &args) {
  if (applet == L"touch" || applet == L"cat") {
    args = {fname};
  } else if (applet == L"cp") {
    args = {fname, fcopy};
  } else if (applet == L"rm") {
    args = {fcopy};
  } else if (applet == L"chmod") {
    args = {L"644", fname};
  } else {
    return false;
  }
  return true;
}

int run_sequence(int argc, wchar_t *argv[]) {
  if (argc < 4) {
    diag(L"usage: ffi-harness.exe sequence <install-root> <scratch-dir> "
         L"[reps] [applets-csv]");
    return 64;
  }
  const std::wstring root = argv[2];
  const std::wstring scratch = argv[3];
  const int reps = argc >= 5 ? _wtoi(argv[4]) : 25;
  const std::vector<std::wstring> rotation =
      split_csv(argc >= 6 ? argv[5] : L"touch,cat,cp,rm,chmod");
  if (reps < 1 || rotation.empty() || !is_absolute_wpath(scratch)) {
    diag(L"sequence: bad arguments (reps>=1, applets non-empty, scratch "
         L"must be absolute)");
    return 64;
  }
  const std::wstring fname = L"seqfile.txt";
  const std::wstring fcopy = L"seqfile.txt.copy";
  for (const auto &applet : rotation) {
    std::vector<std::wstring> probe;
    if (!seq_args_for(applet, fname, fcopy, probe)) {
      diag(L"sequence: unsupported applet in rotation: " + applet);
      return 64;
    }
  }

  // Discovery and integrity: identical rules to the single-call path
  // (absolute override or current.txt pointer, per-version pin before
  // load). The switch/exclude policy is the caller's business — this
  // mode measures the DLL face itself.
  std::wstring dll_path, ver_dir;
  const wchar_t *override_path = _wgetenv(L"NIU_WINUXCMD_FFI_DLL");
  if (override_path && *override_path) {
    if (!is_absolute_wpath(override_path)) {
      seq_out("SEQUENCE FAIL stage=discovery reason=override-not-absolute\n");
      return 1;
    }
    dll_path = override_path;
    const size_t slash = dll_path.find_last_of(L"\\/");
    ver_dir = slash == std::wstring::npos ? L"" : dll_path.substr(0, slash);
  } else {
    std::wstring pointer;
    if (!read_file_text(root + L"\\usr\\bin\\ffi\\current.txt", pointer)) {
      seq_out("SEQUENCE FAIL stage=discovery reason=pointer-missing\n");
      return 1;
    }
    const std::wstring ver = trim(pointer);
    if (ver.empty() || ver.find_first_of(L"\\/:") != std::wstring::npos) {
      seq_out("SEQUENCE FAIL stage=discovery reason=pointer-invalid\n");
      return 1;
    }
    ver_dir = root + L"\\usr\\bin\\ffi\\" + ver;
    dll_path = ver_dir + L"\\winuxcore.dll";
  }
  if (!file_exists(dll_path)) {
    seq_out("SEQUENCE FAIL stage=discovery reason=dll-missing\n");
    return 1;
  }
  const std::string pinned = pinned_hash(ver_dir + L"\\winuxcore.sha256");
  std::string actual;
  if (pinned.size() != 64 || !sha256_file_hex(dll_path, actual) ||
      actual != pinned) {
    seq_out("SEQUENCE FAIL stage=integrity reason=pin-mismatch\n");
    return 1;
  }
  HMODULE dll = LoadLibraryExW(
      dll_path.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!dll) {
    seq_out("SEQUENCE FAIL stage=load reason=load-failed\n");
    return 1;
  }
  const auto fn_abi = reinterpret_cast<FnAbiVersion>(
      GetProcAddress(dll, "winux_ffi_abi_version"));
  const auto fn_init =
      reinterpret_cast<FnInit>(GetProcAddress(dll, "winux_ffi_init"));
  const auto fn_call =
      reinterpret_cast<FnCall>(GetProcAddress(dll, "winux_ffi_call"));
  if (!fn_abi || !fn_init || !fn_call || fn_abi() != kExpectedAbi) {
    seq_out("SEQUENCE FAIL stage=load reason=symbols-or-abi\n");
    return 1;
  }
  const std::wstring stamp = locale_stamp();
  FfiInitArgs init{};
  init.struct_size = sizeof(init);
  init.abi_version = kExpectedAbi;
  init.install_root = root.c_str();
  init.locale_stamp = stamp.empty() ? nullptr : stamp.c_str();
  if (fn_init(&init) != 0) {
    seq_out("SEQUENCE FAIL stage=init\n");
    return 1;
  }

  // Prime the scratch file so a rotation starting with cat has input.
  const std::wstring fpath = scratch + L"\\" + fname;
  const std::wstring cpath = scratch + L"\\" + fcopy;
  CreateDirectoryW(scratch.c_str(), nullptr);
  {
    HANDLE h = CreateFileW(fpath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      seq_out("SEQUENCE FAIL stage=prime\n");
      return 1;
    }
    const char payload[] = "ffi sequence payload\n";
    DWORD written = 0;
    WriteFile(h, payload, sizeof(payload) - 1, &written, nullptr);
    CloseHandle(h);
  }

  LPWCH env_block = GetEnvironmentStringsW();
  long calls = 0;
  int fail_rc = 0;
  for (int rep = 0; rep < reps && fail_rc == 0; ++rep) {
    for (const auto &applet : rotation) {
      std::vector<std::wstring> args;
      seq_args_for(applet, fname, fcopy, args);
      const TierRow tier = lookup_tier(ver_dir + L"\\tiers.tsv", applet);
      if (!tier.found || tier.tier != L"tier1" ||
          shape_matches(args, tier.fallback_shapes)) {
        seq_out("SEQUENCE FAIL rep=" + std::to_string(rep) +
                " applet=" + narrow_of(applet) + " reason=not-ffi-eligible\n");
        fail_rc = 1;
        break;
      }
      std::vector<const wchar_t *> argv_ptrs;
      argv_ptrs.reserve(args.size());
      for (const auto &a : args) argv_ptrs.push_back(a.c_str());
      FfiContext ctx{};
      ctx.struct_size = sizeof(ctx);
      ctx.abi_version = kExpectedAbi;
      ctx.applet = applet.c_str();
      ctx.argc = static_cast<int32_t>(argv_ptrs.size());
      ctx.argv = argv_ptrs.empty() ? nullptr : argv_ptrs.data();
      ctx.env_block = env_block;
      ctx.cwd = scratch.c_str();
      ctx.stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
      ctx.stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
      ctx.stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
      ctx.cancel_check = nullptr;
      ctx.cancel_user_data = nullptr;
      const int rc = fn_call(&ctx);
      ++calls;
      seq_out("seq rep=" + std::to_string(rep) +
              " applet=" + narrow_of(applet) + " rc=" + std::to_string(rc) +
              "\n");
      if (rc <= kFfiErrFloor) {
        seq_out("SEQUENCE FAIL rep=" + std::to_string(rep) +
                " applet=" + narrow_of(applet) +
                " reason=ffi-layer rc=" + std::to_string(rc) + "\n");
        fail_rc = 1;
        break;
      }
      if (rc != 0) {
        seq_out("SEQUENCE FAIL rep=" + std::to_string(rep) +
                " applet=" + narrow_of(applet) +
                " reason=applet-rc rc=" + std::to_string(rc) + "\n");
        fail_rc = 1;
        break;
      }
      if (applet == L"cp" && !file_exists(cpath)) {
        seq_out("SEQUENCE FAIL rep=" + std::to_string(rep) +
                " applet=cp reason=copy-missing\n");
        fail_rc = 1;
        break;
      }
      if (applet == L"rm" && file_exists(cpath)) {
        seq_out("SEQUENCE FAIL rep=" + std::to_string(rep) +
                " applet=rm reason=copy-survived\n");
        fail_rc = 1;
        break;
      }
    }
  }
  if (env_block) FreeEnvironmentStringsW(env_block);
  if (fail_rc != 0) return fail_rc;
  if (!file_exists(fpath)) {
    seq_out("SEQUENCE FAIL stage=final reason=scratch-file-missing\n");
    return 1;
  }
  seq_out("SEQUENCE OK reps=" + std::to_string(reps) +
          " calls=" + std::to_string(calls) + "\n");
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t *argv[]) {
  if (argc >= 2 && std::wstring(argv[1]) == L"sequence") {
    return run_sequence(argc, argv);
  }
  if (argc < 3) {
    diag(L"usage: ffi-harness.exe <install-root> <applet> [args...] | "
         L"ffi-harness.exe sequence <install-root> <scratch-dir> [reps] "
         L"[applets-csv]");
    return 64;
  }
  const std::wstring root = argv[1];
  const std::wstring applet = argv[2];
  std::vector<std::wstring> args(argv + 3, argv + argc);

  // Switch first: off means the DLL is never even looked for.
  if (!env_flag_on(L"NIU_WINUXCMD_FFI")) {
    diag(L"route=spawn reason=switch-off applet=" + applet);
    return run_spawn(root, applet, args);
  }
  if (excluded(applet)) {
    diag(L"route=spawn reason=excluded applet=" + applet);
    return run_spawn(root, applet, args);
  }

  // Discovery: override (absolute only) or the current.txt pointer.
  std::wstring dll_path, ver_dir;
  const wchar_t *override_path = _wgetenv(L"NIU_WINUXCMD_FFI_DLL");
  if (override_path && *override_path) {
    if (is_absolute_wpath(override_path)) {
      dll_path = override_path;
      const size_t slash = dll_path.find_last_of(L"\\/");
      ver_dir = slash == std::wstring::npos ? L"" : dll_path.substr(0, slash);
    } else {
      diag(L"route=spawn reason=override-not-absolute");
      return run_spawn(root, applet, args);
    }
  } else {
    std::wstring pointer;
    if (!read_file_text(root + L"\\usr\\bin\\ffi\\current.txt", pointer)) {
      diag(L"route=spawn reason=pointer-missing");
      return run_spawn(root, applet, args);
    }
    const std::wstring ver = trim(pointer);
    if (ver.empty() || ver.find_first_of(L"\\/:") != std::wstring::npos) {
      diag(L"route=spawn reason=pointer-invalid");
      return run_spawn(root, applet, args);
    }
    ver_dir = root + L"\\usr\\bin\\ffi\\" + ver;
    dll_path = ver_dir + L"\\winuxcore.dll";
  }
  if (!file_exists(dll_path)) {
    diag(L"route=spawn reason=dll-missing");
    return run_spawn(root, applet, args);
  }

  // Tiering: only tier1 rows may run in-process, and per-call shapes in
  // the row decline this individual call.
  const TierRow tier = lookup_tier(ver_dir + L"\\tiers.tsv", applet);
  if (!tier.found) {
    diag(L"route=spawn reason=tier-unknown applet=" + applet);
    return run_spawn(root, applet, args);
  }
  if (tier.tier != L"tier1") {
    diag(L"route=spawn reason=not-tier1 tier=" + tier.tier +
         L" applet=" + applet);
    return run_spawn(root, applet, args);
  }
  if (shape_matches(args, tier.fallback_shapes)) {
    diag(L"route=spawn reason=shape-fallback applet=" + applet);
    return run_spawn(root, applet, args);
  }

  // Integrity: pinned SHA-256 before any load attempt.
  const std::string pinned = pinned_hash(ver_dir + L"\\winuxcore.sha256");
  if (pinned.size() != 64) {
    diag(L"route=spawn reason=manifest-missing");
    return run_spawn(root, applet, args);
  }
  std::string actual;
  if (!sha256_file_hex(dll_path, actual)) {
    diag(L"route=spawn reason=hash-unreadable");
    return run_spawn(root, applet, args);
  }
  if (actual != pinned) {
    diag(L"route=spawn reason=hash-mismatch");
    return run_spawn(root, applet, args);
  }

  // Load once per process; never search CWD/PATH for the DLL or its deps.
  static HMODULE dll = LoadLibraryExW(
      dll_path.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!dll) {
    diag(L"route=spawn reason=load-failed err=" +
         std::to_wstring(GetLastError()));
    return run_spawn(root, applet, args);
  }
  const auto fn_abi = reinterpret_cast<FnAbiVersion>(
      GetProcAddress(dll, "winux_ffi_abi_version"));
  const auto fn_init =
      reinterpret_cast<FnInit>(GetProcAddress(dll, "winux_ffi_init"));
  const auto fn_call =
      reinterpret_cast<FnCall>(GetProcAddress(dll, "winux_ffi_call"));
  if (!fn_abi || !fn_init || !fn_call) {
    diag(L"route=spawn reason=symbols-missing");
    return run_spawn(root, applet, args);
  }
  if (fn_abi() != kExpectedAbi) {
    diag(L"route=spawn reason=abi-mismatch abi=" +
         std::to_wstring(fn_abi()));
    return run_spawn(root, applet, args);
  }

  const std::wstring stamp = locale_stamp();
  FfiInitArgs init{};
  init.struct_size = sizeof(init);
  init.abi_version = kExpectedAbi;
  init.install_root = root.c_str();
  init.locale_stamp = stamp.empty() ? nullptr : stamp.c_str();
  const int init_rc = fn_init(&init);
  if (init_rc != 0) {
    diag(L"route=spawn reason=init-failed rc=" + std::to_wstring(init_rc));
    return run_spawn(root, applet, args);
  }

  std::vector<const wchar_t *> argv_ptrs;
  argv_ptrs.reserve(args.size());
  for (const auto &a : args) argv_ptrs.push_back(a.c_str());

  wchar_t cwd_buf[32768];
  const DWORD cwd_len = GetCurrentDirectoryW(
      static_cast<DWORD>(sizeof(cwd_buf) / sizeof(cwd_buf[0])), cwd_buf);
  if (cwd_len == 0) {
    diag(L"route=spawn reason=cwd-unavailable");
    return run_spawn(root, applet, args);
  }
  LPWCH env_block = GetEnvironmentStringsW();

  FfiContext ctx{};
  ctx.struct_size = sizeof(ctx);
  ctx.abi_version = kExpectedAbi;
  ctx.applet = applet.c_str();
  ctx.argc = static_cast<int32_t>(argv_ptrs.size());
  ctx.argv = argv_ptrs.empty() ? nullptr : argv_ptrs.data();
  ctx.env_block = env_block;
  ctx.cwd = cwd_buf;
  ctx.stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
  ctx.stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  ctx.stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
  ctx.cancel_check = nullptr;
  ctx.cancel_user_data = nullptr;

  diag(L"route=ffi applet=" + applet);
  const int rc = fn_call(&ctx);
  if (env_block) FreeEnvironmentStringsW(env_block);
  if (rc <= kFfiErrFloor) {
    diag(L"route=spawn reason=ffi-call-failed rc=" + std::to_wstring(rc) +
         L" applet=" + applet);
    return run_spawn(root, applet, args);
  }
  return rc;
}
