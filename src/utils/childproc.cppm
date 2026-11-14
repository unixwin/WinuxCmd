// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Shared child-command resolution for commands that spawn
///   subprocesses (timeout, xargs, env, find -exec, ...).
///
/// Problem being fixed (observed 2026-10): WinuxCmd spawners hand a bare
/// command name to CreateProcessW with lpApplicationName == nullptr. The
/// Win32 fallback search order is
///
///     application directory -> current directory -> System32 -> Windows ->
///     PATH
///
/// so any child name that collides with a System32 image (timeout, sort,
/// find, more, tree, hostname, choice, ...) resolves to the Windows binary
/// even when the caller's PATH points at the GNU-compatible one first.
/// PATH strings produced by POSIX shells (rubash exports PATH verbatim,
/// rubash#149/#331) additionally mix '/c/...' drive spellings and ':'
/// separators into the ';'-separated Windows form, which Win32 SearchPath
/// cannot parse at all.
///
/// The helper implements GNU execvp semantics for the Windows world:
///   1. an operand containing a separator is an explicit (relative or
///      absolute) path and is used as given;
///   2. a bare name is searched ONLY in the normalized PATH entries --
///      never the application directory, the current directory, or the
///      implicit System32/Windows fallbacks.  When PATH is unset entirely
///      (env -i, emptied environments) execvp falls back to the system
///      default path, so the system directories are used as a last resort
///      in that case only.
///   3. a resolved file with a `#!` line launches through its interpreter
///      (the binfmt_script value model: [interpreter, optional-arg, script,
///      argv...]), with the interpreter resolved recursively through the
///      same chain; a POSIX-rooted interpreter spelling is mapped through
///      the shell namespace first, then its leaf name through PATH (#1623).
/// Callers pass the resolved path as CreateProcessW's lpApplicationName
/// (native images) or as the first command-line token (batch scripts,
/// which CreateProcess routes through the command interpreter), which
/// bypasses the Win32 default search entirely.

module;

// Pre-include a C++ header so c++config.h's std declarations (e.g.
// std::__is_constant_evaluated) are first seen with C++ linkage: pch.h's
// windows.h chain (winnt.h -> x86intrin -> mm_malloc -> stdlib.h -> cstdlib)
// would otherwise declare them inside winnt.h's extern "C" region, which
// conflicts with the same declarations imported from module std (#1412).
// The <filesystem> include below sits after pch.h, too late to serve this
// role, so the guard is needed here as in the other pch-first fragments.
#include <cstddef>
// os_defines.h (via cstddef) defines NOMINMAX as 1; drop it so pch.h's own
// bare definition below applies exactly as before, without a warning.
#undef NOMINMAX

// clang-format off: pch.h must include winsock2.h before Windows headers.
#include "pch/pch.h"
#include <filesystem>
// clang-format on

export module utils:childproc;

import std;
import :utf8;
import :win32;
import :native_path;

namespace winux::childproc {

export enum class ChildResolveStatus {
  Found,
  NotFound,       // ENOENT -- callers exit 127
  NotExecutable,  // EACCES -- callers exit 126
  NotADirectory,  // ENOTDIR -- callers exit 126
};

export struct ChildResolveResult {
  ChildResolveStatus status = ChildResolveStatus::NotFound;
  std::wstring path;          // resolved candidate when Found
  bool batch_script = false;  // .cmd/.bat -- needs the command interpreter
};

// Internal-linkage helpers live in an anonymous namespace: `export
// namespace` blocks export every name inside them, which trips MSVC C2294
// ("cannot export a symbol with internal linkage") for the helpers.
namespace {

auto attributes_for(const std::wstring& path) -> DWORD {
  return GetFileAttributesW(path.c_str());
}

auto is_regular_file_at(const std::wstring& path) -> bool {
  const DWORD attrs = attributes_for(path);
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

auto exists_at(const std::wstring& path) -> bool {
  return attributes_for(path) != INVALID_FILE_ATTRIBUTES;
}

auto is_drive_letter(wchar_t ch) -> bool {
  return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

// Converts one POSIX '/c/...' (or '/cygdrive/c/...') spelling into the
// Windows 'C:\...' form. Non-POSIX entries are returned unchanged apart
// from separator normalization. Mirrors cygpath's unix_to_windows shapes.
auto normalize_entry(std::wstring entry) -> std::wstring {
  while (!entry.empty() && (entry.back() == L'"' || entry.back() == L' ' ||
                            entry.back() == L'\t')) {
    entry.pop_back();
  }
  size_t begin = 0;
  while (
      begin < entry.size() &&
      (entry[begin] == L'"' || entry[begin] == L' ' || entry[begin] == L'\t')) {
    ++begin;
  }
  entry.erase(0, begin);

  if (entry.starts_with(L"/cygdrive/") && entry.size() >= 12 &&
      is_drive_letter(entry[10]) && (entry.size() == 11 || entry[11] == L'/')) {
    std::wstring rest = entry.substr(11);
    entry.assign(1, static_cast<wchar_t>(std::towupper(entry[10])));
    entry += L":";
    entry += rest;
  } else if (entry.size() >= 2 && entry[0] == L'/' &&
             is_drive_letter(entry[1]) &&
             (entry.size() == 2 || entry[2] == L'/')) {
    std::wstring rest = entry.substr(2);
    entry.assign(1, static_cast<wchar_t>(std::towupper(entry[1])));
    entry += L":";
    entry += rest;
  }

  // Normalize separators, preserving a leading UNC '\\'.
  const bool unc = entry.starts_with(L"\\\\") || entry.starts_with(L"//");
  for (size_t i = unc ? 2 : 0; i < entry.size(); ++i) {
    if (entry[i] == L'/') entry[i] = L'\\';
  }
  return entry;
}

// PATHEXT candidates used when the program name carries no extension.
// CreateProcess itself only launches native images; .cmd/.bat candidates
// are reported through ChildResolveResult::batch_script so callers can
// route them through the command interpreter.
auto pathext_candidates() -> std::vector<std::wstring> {
  std::vector<std::wstring> exts;
  wchar_t buffer[2048]{};
  const DWORD length = GetEnvironmentVariableW(
      L"PATHEXT", buffer, static_cast<DWORD>(std::size(buffer)));
  std::wstring raw;
  if (length > 0 && length < std::size(buffer)) {
    raw.assign(buffer, length);
  }
  size_t start = 0;
  while (start <= raw.size()) {
    size_t pos = raw.find(L';', start);
    if (pos == std::wstring::npos) {
      exts.emplace_back(raw.substr(start));
      break;
    }
    exts.emplace_back(raw.substr(start, pos - start));
    start = pos + 1;
  }
  std::erase_if(exts, [](const std::wstring& ext) { return ext.empty(); });
  if (exts.empty()) {
    for (const wchar_t* fallback : {L".com", L".exe", L".bat", L".cmd"}) {
      exts.emplace_back(fallback);
    }
  }
  for (auto& ext : exts) {
    if (ext.front() != L'.') ext.insert(ext.begin(), L'.');
    for (auto& ch : ext) {
      ch = static_cast<wchar_t>(std::towlower(ch));
    }
  }
  return exts;
}

auto lowercase_extension(std::wstring_view path) -> std::wstring {
  const size_t slash = path.find_last_of(L"/\\");
  const size_t dot = path.find_last_of(L'.');
  if (dot == std::wstring_view::npos ||
      (slash != std::wstring_view::npos && dot < slash)) {
    return {};
  }
  std::wstring ext{path.substr(dot)};
  for (auto& ch : ext) {
    ch = static_cast<wchar_t>(std::towlower(ch));
  }
  return ext;
}

auto has_path_separator(std::wstring_view text) -> bool {
  return text.find(L'/') != std::wstring_view::npos ||
         text.find(L'\\') != std::wstring_view::npos;
}

// Scans one directory entry (or an explicit base path) for the program
// with every acceptable extension. Returns the first regular-file match.
auto scan_candidates(const std::wstring& base, bool explicit_extension,
                     ChildResolveStatus& fallback) -> ChildResolveResult {
  std::vector<std::wstring> names{base};
  if (!explicit_extension) {
    for (const auto& ext : pathext_candidates()) {
      names.push_back(base + ext);
    }
  }

  for (const auto& candidate : names) {
    const DWORD attrs = attributes_for(candidate);
    if (attrs == INVALID_FILE_ATTRIBUTES) continue;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      if (fallback == ChildResolveStatus::NotFound) {
        fallback = ChildResolveStatus::NotExecutable;
      }
      continue;
    }
    ChildResolveResult found;
    found.status = ChildResolveStatus::Found;
    found.path = candidate;
    const std::wstring ext = lowercase_extension(candidate);
    found.batch_script = ext == L".cmd" || ext == L".bat";
    return found;
  }
  return {};
}

// Shared resolution core: a bare name is looked up in the given normalized
// entries only; an operand with a separator is used as an explicit path.
auto resolve_in_entries(std::wstring_view program,
                        const std::vector<std::wstring>& entries)
    -> ChildResolveResult {
  const std::wstring name(program);
  ChildResolveStatus fallback = ChildResolveStatus::NotFound;

  if (has_path_separator(name)) {
    // [GNU] a trailing separator after a regular file is ENOTDIR
    // ("Not a directory"), not ENOENT.
    if (name.back() == L'/' || name.back() == L'\\') {
      std::wstring stripped(name);
      while (!stripped.empty() &&
             (stripped.back() == L'/' || stripped.back() == L'\\')) {
        stripped.pop_back();
      }
      if (!stripped.empty() && exists_at(stripped) &&
          !is_regular_file_at(stripped)) {
        ChildResolveResult notdir;
        notdir.status = ChildResolveStatus::NotADirectory;
        return notdir;
      }
      return {};
    }
    const std::wstring extension = lowercase_extension(name);
    ChildResolveResult explicit_result =
        scan_candidates(normalize_entry(name), !extension.empty(), fallback);
    if (explicit_result.status == ChildResolveStatus::Found) {
      return explicit_result;
    }
    ChildResolveResult result;
    result.status = explicit_result.status != ChildResolveStatus::NotFound
                        ? explicit_result.status
                        : fallback;
    return result;
  }

  for (const auto& dir : entries) {
    std::filesystem::path base = std::filesystem::path(dir) / name;
    ChildResolveResult found = scan_candidates(
        base.wstring(), !lowercase_extension(name).empty(), fallback);
    if (found.status == ChildResolveStatus::Found) return found;
  }

  ChildResolveResult result;
  result.status = fallback;
  return result;
}

// execvp consults a system default path when PATH is unset. On Windows the
// equivalent of "/bin:/usr/bin" is the system directory plus the Windows
// directory. This applies ONLY to an unset PATH (env -i); a set PATH is
// searched exclusively, without implicit system fallbacks.
auto system_default_search_entries() -> std::vector<std::wstring> {
  std::vector<std::wstring> entries;
  wchar_t system_directory[MAX_PATH]{};
  if (GetSystemDirectoryW(system_directory, MAX_PATH) > 0) {
    entries.emplace_back(system_directory);
  }
  wchar_t windows_directory[MAX_PATH]{};
  if (GetWindowsDirectoryW(windows_directory, MAX_PATH) > 0) {
    entries.emplace_back(windows_directory);
  }
  return entries;
}

}  // namespace

// Reads the process PATH verbatim. nullopt means the variable is unset.
export auto process_path_value() -> std::optional<std::wstring> {
  DWORD capacity = 8192;
  std::wstring raw;
  for (;;) {
    raw.resize(capacity, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        L"PATH", raw.data(), static_cast<DWORD>(raw.size()));
    if (written == 0) {
      return GetLastError() == ERROR_ENVVAR_NOT_FOUND
                 ? std::nullopt
                 : std::optional<std::wstring>(L"");
    }
    if (written < raw.size()) {
      raw.resize(written);
      return raw;
    }
    if (written >= 32768) return std::wstring{};
    capacity = written + 1;
  }
}

// Splits a PATH-style string that may mix Windows ';'-separated entries
// with POSIX ':'-separated entries and '/c/...' drive spellings into
// ordered Windows directory entries. A ':' is only a drive separator when
// it directly follows a single drive letter ("C:\x", "C:/x", "C:foo");
// every other ':' separates entries, matching how MSYS/Cygwin render PATH.
// Empty entries are dropped: WinuxCmd never searches the current directory
// implicitly (an explicit "./child" operand still works).
export auto normalize_path_entries(std::wstring_view raw)
    -> std::vector<std::wstring> {
  std::vector<std::wstring> entries;
  size_t segment_start = 0;
  for (size_t i = 0; i <= raw.size(); ++i) {
    const bool at_end = i == raw.size();
    const bool split_semicolon = !at_end && raw[i] == L';';
    bool split_colon = false;
    if (!at_end && !split_semicolon && raw[i] == L':') {
      const size_t segment_length = i - segment_start;
      const bool drive_colon = segment_length == 1 &&
                               is_drive_letter(raw[i - 1]) &&
                               (i + 1 >= raw.size() || raw[i + 1] != L':');
      split_colon = !drive_colon;
    }
    if (at_end || split_semicolon || split_colon) {
      std::wstring entry = normalize_entry(
          std::wstring(raw.substr(segment_start, i - segment_start)));
      if (!entry.empty()) entries.push_back(std::move(entry));
      segment_start = i + 1;
    }
  }
  return entries;
}

// The caller's PATH (env var), normalized via normalize_path_entries.
// Unset PATH yields the system default entries (execvp's _CS_PATH analog).
export auto child_path_entries() -> std::vector<std::wstring> {
  const std::optional<std::wstring> raw = process_path_value();
  if (!raw.has_value()) return system_default_search_entries();
  return normalize_path_entries(*raw);
}

// Resolves a child command the way GNU execvp does on Windows:
//   - an operand containing '/' or '\\' is an explicit relative/absolute
//     path and is used as given (with PATHEXT completion when extensionless);
//   - a bare name is searched ONLY in the normalized PATH entries.
// The application directory, the current directory, and the Win32 implicit
// System32/Windows fallbacks are deliberately NOT consulted, so a child
// name that collides with a System32 image (timeout, sort, find, more,
// tree, hostname, choice, ...) resolves from PATH instead.
export auto resolve_child_command(std::wstring_view program)
    -> ChildResolveResult {
  if (program.empty()) return {};
  return resolve_in_entries(program, child_path_entries());
}

// Same contract as resolve_child_command, but against an explicit PATH
// value instead of the process environment. Callers that materialize a
// modified environment before spawning (env NAME=VALUE ... command) pass
// the value they are about to install; nullopt defers to the process PATH.
export auto resolve_child_command_in_path(
    std::wstring_view program, const std::optional<std::wstring>& path_value)
    -> ChildResolveResult {
  if (program.empty()) return {};
  if (!path_value.has_value()) return resolve_child_command(program);
  return resolve_in_entries(program, normalize_path_entries(*path_value));
}

// --- `#!` script support (#1623) ----------------------------------------
//
// Mirrors rust2/crates/winux-cmds/src/childproc.rs: a resolved file is
// classified into its launch shape (batch extension, MZ image, or `#!`
// script), and a script's interpreter is resolved recursively through the
// same chain. One deliberate boundary: a file that exists but carries
// neither MZ magic nor `#!` keeps the historical shape (handed to
// CreateProcessW as an image, which fails with ERROR_BAD_EXE_FORMAT /
// 126) instead of the Rust BadFormat "Exec format error" — the non-script
// path is not this issue's scope.
namespace {

// binfmt_script.c reads the shebang line from the same first page the image
// loaders use: exec.c zero-fills a BINPRM_BUF_SIZE (256) buffer and reads
// only BINPRM_BUF_SIZE - 1 bytes into it, so the line the kernel parses is
// at most 255 bytes. An overlong line is truncated at that edge and still
// launches, it is not rejected (probed WSL GNU 6.18: `#!/bin/echo ` + 300
// x's runs /bin/echo with a 243-byte argument -- 255 bytes minus "#!",
// "/bin/echo" and the separating space).
constexpr size_t kShebangHeadBytes = 256;
constexpr size_t kShebangLineBytes = kShebangHeadBytes - 1;
// Guard against interpreter cycles (`#!/x` where x is itself `#!...`).
// The kernel bounds binfmt recursion the same way in spirit; three levels
// already exceeds every real-world chain.
constexpr unsigned kShebangRecursionLimit = 3;

enum class LaunchKind { Image, Batch, Script };

struct ClassifiedLaunch {
  ChildResolveStatus status = ChildResolveStatus::NotFound;
  LaunchKind kind = LaunchKind::Image;
  std::wstring path;  // image/batch path, or resolved interpreter path
  // Script only: the leading argv the OS prepends before the caller's
  // arguments -- resolved interpreter, optional shebang argument, script,
  // and (for a script-chain interpreter) the deeper tokens.
  std::vector<std::wstring> script_argv;
  bool interpreter_is_batch = false;  // Script only
};

struct ShebangLine {
  std::string interpreter;  // UTF-8 bytes as written in the file
  std::optional<std::string> arg;
};

auto classify_file(const std::wstring& path,
                   const std::optional<std::wstring>& path_override,
                   unsigned depth) -> ClassifiedLaunch;
auto resolve_interpreter(const std::wstring& spelling,
                         const std::optional<std::wstring>& path_override,
                         unsigned depth) -> ClassifiedLaunch;

// The first HEAD_BYTES of a file, short-read tolerant. 256 matches the
// kernel's BINPRM_BUF_SIZE: the shebang line is parsed within it (truncated,
// not rejected, when it overflows).
auto read_file_head(const std::wstring& path) -> std::vector<unsigned char> {
  std::vector<unsigned char> head(kShebangHeadBytes);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  DWORD filled = 0;
  while (filled < head.size()) {
    DWORD read = 0;
    if (!ReadFile(file, head.data() + filled,
                  static_cast<DWORD>(head.size() - filled), &read, nullptr) ||
        read == 0) {
      break;
    }
    filled += read;
  }
  CloseHandle(file);
  head.resize(filled);
  return head;
}

// binfmt_script.c line split of a `#!` head: the interpreter is the first
// space/tab token of the line (up to LF or the 255-byte kernel window); the
// remainder after the whitespace run is ONE optional argument, trailing
// space/tab trimmed (the kernel keeps a CR -- a CRLF shebang names an
// interpreter with a trailing CR and misses it, probed ENOENT). An overlong
// line is truncated at the window edge, not rejected: probed WSL GNU
// launches `#!/bin/echo ` + 300 x's and echoes the 243-byte argument, so the
// window end is the line end when no LF is present. nullopt only where the
// kernel gives ENOEXEC: `#!` with no interpreter token. A bare `#!` is a
// product boundary: GNU `env` reaches 0 through the libc execvp /bin/sh
// fallback, which is out of contract here.
auto parse_shebang_line(const std::vector<unsigned char>& head)
    -> std::optional<ShebangLine> {
  // Skip the "#!" magic: the kernel splits the line that FOLLOWS it. The
  // last byte of the kernel's BINPRM_BUF_SIZE buffer stays NUL (exec.c reads
  // one byte less), so the line is bounded by kShebangLineBytes; no LF
  // inside that window means the kernel truncates the line and still
  // launches.
  const size_t window =
      head.size() < kShebangLineBytes ? head.size() : kShebangLineBytes;
  const size_t body_size = window - 2;
  const unsigned char* body = head.data() + 2;
  // No LF inside the head: the kernel truncates the line at
  // BINPRM_BUF_SIZE and still launches, so the buffer end is the line end.
  size_t line_end = body_size;
  for (size_t i = 0; i < body_size; ++i) {
    if (body[i] == '\n') {
      line_end = i;
      break;
    }
  }
  size_t end = line_end;
  while (end > 0 && (body[end - 1] == ' ' || body[end - 1] == '\t')) --end;
  size_t start = 0;
  while (start < end && (body[start] == ' ' || body[start] == '\t')) ++start;
  size_t interp_end = start;
  while (interp_end < end && body[interp_end] != ' ' &&
         body[interp_end] != '\t') {
    ++interp_end;
  }
  if (interp_end == start) return std::nullopt;  // "#!" alone
  ShebangLine out;
  out.interpreter.assign(
      reinterpret_cast<const char*>(body + start), interp_end - start);
  size_t arg_start = interp_end;
  while (arg_start < end &&
         (body[arg_start] == ' ' || body[arg_start] == '\t')) {
    ++arg_start;
  }
  if (arg_start < end) {
    out.arg.emplace(
        reinterpret_cast<const char*>(body + arg_start), end - arg_start);
  }
  return out;
}

// True when the spelling lives in the POSIX shell namespace (a single
// leading slash, not a "//x" UNC form and not a "/c/..." drive spelling
// the native chain already resolves).
auto is_posix_rooted(std::wstring_view name) -> bool {
  if (name.empty() || name[0] != L'/') return false;
  if (name.size() >= 2 && name[1] == L'/') return false;
  if (name.size() >= 2 && is_drive_letter(name[1]) &&
      (name.size() == 2 || name[2] == L'/')) {
    return false;
  }
  return true;
}

auto comspec_path() -> std::wstring {
  wchar_t comspec[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(
      L"COMSPEC", comspec, static_cast<DWORD>(std::size(comspec)));
  if (length > 0 && length < std::size(comspec)) {
    return std::wstring(comspec, length);
  }
  return L"cmd.exe";
}

// Launch assembly for a `#!` script: [interpreter, optional-arg, script],
// with the interpreter resolved through the same chain. Status mapping
// follows the kernel's errnos (all GNU-probed): a MISSING interpreter is
// ENOENT (callers report 127 "No such file or directory" against the
// script operand), an unrunnable interpreter ENOEXEC, a directory EACCES.
auto resolve_shebang_script(const std::wstring& script,
                            const std::vector<unsigned char>& head,
                            const std::optional<std::wstring>& path_override,
                            unsigned depth) -> ClassifiedLaunch {
  const std::optional<ShebangLine> parsed = parse_shebang_line(head);
  if (!parsed.has_value() || depth >= kShebangRecursionLimit) {
    // A bare `#!` (no interpreter token) or the recursion guard: the
    // BadFormat analog, which the C++ status set reports as 126. This is a
    // product boundary for the bare `#!` shape: GNU `env` reaches 0 only
    // through the libc execvp /bin/sh fallback, which is out of contract.
    ClassifiedLaunch bad_format;
    bad_format.status = ChildResolveStatus::NotExecutable;
    return bad_format;
  }
  // binfmt_script opens the interpreter by PATH-lookup-free open(): a bare
  // interpreter name is a cwd-relative path, never a PATH search (probed
  // WSL GNU: "#!env sh" -> 127). Route bare names through the explicit-path
  // branch so the bare-name search stays out of the way.
  const std::wstring interpreter = utf8_to_wstring(parsed->interpreter);
  const ClassifiedLaunch inner =
      resolve_interpreter(interpreter, path_override, depth + 1);
  if (inner.status != ChildResolveStatus::Found) return inner;

  ClassifiedLaunch script_launch;
  script_launch.status = ChildResolveStatus::Found;
  script_launch.kind = LaunchKind::Script;
  script_launch.path = inner.path;
  if (inner.kind == LaunchKind::Script) {
    // Script-chain interpreter: `inner` is already the inner script's
    // complete launch argv [interp, arg?, inner-script]; append only the
    // outer shebang's [arg?, script] tokens -- the inner script must
    // appear exactly once (#1595).
    script_launch.script_argv = inner.script_argv;
    script_launch.interpreter_is_batch = inner.interpreter_is_batch;
  } else {
    script_launch.script_argv.push_back(inner.path);
    script_launch.interpreter_is_batch = inner.kind == LaunchKind::Batch;
  }
  if (parsed->arg.has_value()) {
    script_launch.script_argv.push_back(utf8_to_wstring(*parsed->arg));
  }
  script_launch.script_argv.push_back(script);
  return script_launch;
}

// Full resolution chain for a shebang interpreter spelling. `depth` counts
// shebang levels only; the namespace passes are namespace translations,
// not new script levels.
auto resolve_interpreter(const std::wstring& spelling,
                         const std::optional<std::wstring>& path_override,
                         unsigned depth) -> ClassifiedLaunch {
  std::wstring name(spelling);
  if (name.find(L'/') == std::wstring::npos &&
      name.find(L'\\') == std::wstring::npos) {
    name = L"./" + name;
  }
  ChildResolveStatus fallback = ChildResolveStatus::NotFound;

  // Pass A: the native chain, used as given.
  {
    const ChildResolveResult native_result = resolve_in_entries(name, {});
    if (native_result.status == ChildResolveStatus::Found) {
      return classify_file(native_result.path, path_override, depth);
    }
    if (native_result.status != ChildResolveStatus::NotFound) {
      fallback = native_result.status;
    }
  }
  if (is_posix_rooted(name)) {
    // Pass B: the product's shell-namespace mapper for POSIX-rooted
    // spellings ("#!/bin/sh" has no native analog; the file commands
    // resolve the same spelling through this mapper).
    if (auto mapped = native_path::shell_tree_to_native_w(name)) {
      ChildResolveStatus mapped_fallback = ChildResolveStatus::NotFound;
      const ChildResolveResult mapped_result = scan_candidates(
          *mapped, !lowercase_extension(*mapped).empty(), mapped_fallback);
      if (mapped_result.status == ChildResolveStatus::Found) {
        return classify_file(mapped_result.path, path_override, depth);
      }
      if (mapped_fallback != ChildResolveStatus::NotFound) {
        fallback = mapped_fallback;
      }
    }
    // Pass C: the leaf name through PATH -- a known accommodation, not
    // kernel parity: the kernel never PATH-searches a shebang interpreter
    // (probed: "#!/nonexistent/interp" with `interp` on PATH is still
    // ENOENT/127). This is the last accommodation for a POSIX-rooted
    // spelling on a host with neither an install root nor the mapped
    // prefix, but the tool on PATH (a shebang interpreter's "/bin/..."
    // prefix only exists on POSIX; the leaf is what a Windows host can
    // actually run).
    const size_t slash = name.find_last_of(L'/');
    const std::wstring leaf =
        slash == std::wstring::npos ? name : name.substr(slash + 1);
    if (!leaf.empty()) {
      const ChildResolveResult bare =
          resolve_child_command_in_path(leaf, path_override);
      if (bare.status == ChildResolveStatus::Found) {
        return classify_file(bare.path, path_override, depth);
      }
      ClassifiedLaunch missed;
      missed.status = bare.status;
      return missed;
    }
  }
  ClassifiedLaunch missed;
  missed.status = fallback;
  return missed;
}

// Classifies one resolved regular file into its launch shape (#1587).
// Resolution order: the batch extension routes through the command
// interpreter before any magic read; MZ magic is a native image; `#!`
// resolves the interpreter line recursively; anything else keeps the
// historical shape (see the block comment above).
auto classify_file(const std::wstring& path,
                   const std::optional<std::wstring>& path_override,
                   unsigned depth) -> ClassifiedLaunch {
  const std::wstring ext = lowercase_extension(path);
  if (ext == L".cmd" || ext == L".bat") {
    ClassifiedLaunch found;
    found.status = ChildResolveStatus::Found;
    found.kind = LaunchKind::Batch;
    found.path = path;
    return found;
  }
  const std::vector<unsigned char> head = read_file_head(path);
  if (head.size() >= 2 && head[0] == 'M' && head[1] == 'Z') {
    ClassifiedLaunch found;
    found.status = ChildResolveStatus::Found;
    found.kind = LaunchKind::Image;
    found.path = path;
    return found;
  }
  if (head.size() >= 2 && head[0] == '#' && head[1] == '!') {
    return resolve_shebang_script(path, head, path_override, depth);
  }
  ClassifiedLaunch found;
  found.status = ChildResolveStatus::Found;
  found.kind = LaunchKind::Image;
  found.path = path;
  return found;
}

}  // namespace

// Everything a spawner needs to hand CreateProcessW without falling into
// the Win32 default search order.
export struct ChildLaunchRequest {
  std::string command;               // program operand, as typed
  std::vector<std::string> args;     // trailing arguments
  std::optional<std::string> argv0;  // env -a: replaces the first token
  std::optional<std::wstring> path_override;  // env: modified PATH value
  // Caller-built lpCommandLine for the native-image case (used by spawners
  // with command-specific quoting rules, e.g. xargs' cmd /C caret
  // escaping). Empty means plan_child_launch builds a generic quoted line
  // from command + args. Batch routing always re-wraps this line inside
  // the interpreter invocation.
  std::wstring command_line_override;
};

export struct ChildLaunchPlan {
  ChildResolveStatus status = ChildResolveStatus::NotFound;
  std::optional<std::wstring> application_name;  // image or interpreter
  std::wstring command_line;                     // lpCommandLine, ready-made
  std::wstring resolved_path;                    // diagnostics
  bool via_interpreter = false;  // .cmd/.bat or #! script via interpreter
};

// Resolves the command and builds the (application name, command line)
// pair for CreateProcessW:
//   - native image: lpApplicationName = resolved path, lpCommandLine keeps
//     the caller's spelling for argv[0] (GNU execvp parity);
//   - batch script: lpApplicationName = COMSPEC, lpCommandLine =
//     '/d /s /c "<script> <args...>"' -- cmd.exe does the resolution work
//     CreateProcessW cannot (it cannot launch a batch file directly);
//   - `#!` script: lpApplicationName = resolved interpreter path (or
//     COMSPEC when the interpreter is itself a batch file), lpCommandLine =
//     '<interpreter> [<opt-arg>] <script> <args...>' -- the binfmt_script
//     value model, with the interpreter resolved recursively through the
//     same chain (#1623).
// Callers must check status before using the plan; NotFound maps to exit
// 127 (child_spawn_exit_status / resolve status) like GNU execvp.
export auto plan_child_launch(const ChildLaunchRequest& request)
    -> ChildLaunchPlan {
  ChildLaunchPlan plan;
  if (request.command.empty()) return plan;

  const std::wstring command = utf8_to_wstring(request.command);
  const ChildResolveResult resolved =
      resolve_child_command_in_path(command, request.path_override);
  plan.status = resolved.status;
  if (resolved.status != ChildResolveStatus::Found) return plan;
  plan.resolved_path = resolved.path;

  std::wstring tail;
  for (const auto& arg : request.args) {
    append_windows_command_arg(tail, utf8_to_wstring(arg));
  }

  if (resolved.batch_script) {
    std::wstring inner = quote_windows_command_arg(resolved.path);
    if (!tail.empty()) inner += L' ';
    inner += tail;
    plan.application_name = comspec_path();
    plan.command_line = L"/d /s /c \"" + inner + L"\"";
    plan.via_interpreter = true;
    return plan;
  }

  // #1623: a resolved file with a `#!` line launches through its
  // interpreter (binfmt_script value model, mirroring rust2 childproc.rs).
  const ClassifiedLaunch classified =
      classify_file(resolved.path, request.path_override, 0);
  if (classified.status != ChildResolveStatus::Found) {
    plan.status = classified.status;
    return plan;
  }
  if (classified.kind == LaunchKind::Script) {
    // The kernel discards the spawner's argv[0] for a script (the script
    // path takes its place), so env -a has no effect here; and a
    // caller-built command line is a native-image affordance (the batch
    // branch ignores it the same way).
    std::wstring script_line;
    for (const auto& token : classified.script_argv) {
      if (!script_line.empty()) script_line += L' ';
      script_line += quote_windows_command_arg(token);
    }
    if (!tail.empty()) {
      script_line += L' ';
      script_line += tail;
    }
    if (classified.interpreter_is_batch) {
      plan.application_name = comspec_path();
      plan.command_line = L"/d /s /c \"" + script_line + L"\"";
    } else {
      plan.application_name = classified.script_argv.front();
      plan.command_line = std::move(script_line);
    }
    plan.via_interpreter = true;
    return plan;
  }

  if (!request.command_line_override.empty()) {
    plan.command_line = request.command_line_override;
  } else {
    const std::wstring argv0 =
        request.argv0.has_value() ? utf8_to_wstring(*request.argv0) : command;
    plan.command_line = quote_windows_command_arg(argv0);
    if (!tail.empty()) {
      plan.command_line += L' ';
      plan.command_line += tail;
    }
  }
  plan.application_name = resolved.path;
  return plan;
}

// Maps a CreateProcessW failure to the GNU shell convention:
// 127 for "command not found", 126 for "found but not executable".
export auto child_spawn_exit_status(unsigned long create_error) -> int {
  switch (create_error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}

// Inverse of child_spawn_exit_status for diagnostics: resolve failures are
// reported through the same Win32 error text path CreateProcess failures
// use, so a spawner prints one message shape for both.
export auto child_status_to_win32_error(ChildResolveStatus status)
    -> unsigned long {
  switch (status) {
    case ChildResolveStatus::Found:
      return ERROR_SUCCESS;
    case ChildResolveStatus::NotExecutable:
      return ERROR_ACCESS_DENIED;
    case ChildResolveStatus::NotADirectory:
      return ERROR_DIRECTORY;
    case ChildResolveStatus::NotFound:
    default:
      return ERROR_FILE_NOT_FOUND;
  }
}

// Rewrites the first shell word of a shell-command string ("watch sort -u",
// "split --filter 'cat > $FILE'") to its PATH-resolved absolute form. The
// mediated shell (cmd.exe /c) searches System32 ahead of PATH, so without
// this substitution a colliding name still resolves to the wrong binary
// even when the spawner itself uses plan_child_launch. Words that do not
// resolve are left untouched so the shell reports the error verbatim.
// Quote grouping ('"' and '\'') is honored when isolating the first word.
export auto resolve_shell_command_first_word(
    std::string_view shell_command,
    const std::optional<std::wstring>& path_value = std::nullopt)
    -> std::string {
  size_t begin = 0;
  while (begin < shell_command.size() &&
         (shell_command[begin] == ' ' || shell_command[begin] == '\t')) {
    ++begin;
  }
  if (begin >= shell_command.size()) return std::string(shell_command);

  std::string word;
  size_t cursor = begin;
  bool closed = true;
  while (cursor < shell_command.size()) {
    const char ch = shell_command[cursor];
    if (ch == '"' || ch == '\'') {
      closed = !closed;
      ++cursor;
      continue;
    }
    if (closed && (ch == ' ' || ch == '\t')) break;
    word.push_back(ch);
    ++cursor;
  }
  if (word.empty()) return std::string(shell_command);

  const std::wstring wide_word = utf8_to_wstring(word);
  const ChildResolveResult resolved =
      resolve_child_command_in_path(wide_word, path_value);
  if (resolved.status != ChildResolveStatus::Found) {
    return std::string(shell_command);
  }
  std::string replacement =
      wstring_to_utf8(quote_windows_command_arg(resolved.path));

  std::string result(shell_command.substr(0, begin));
  result += replacement;
  result += shell_command.substr(cursor);
  return result;
}

}  // namespace winux::childproc
