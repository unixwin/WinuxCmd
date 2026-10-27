// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
module;

#include "pch/pch.h"

export module utils:native_path;

import std;
import :utf8;

namespace native_path {

export auto from_utf8(std::string_view path) -> std::wstring {
  return utf8_to_wstring(path);
}

export auto to_utf8(std::wstring_view path) -> std::string {
  return wstring_to_utf8(path);
}

export auto normalize_separators(std::wstring path) -> std::wstring {
  for (auto& ch : path) {
    if (ch == L'/') ch = L'\\';
  }
  return path;
}

export auto normalize_separators_utf8(std::string path) -> std::string {
  for (auto& ch : path) {
    if (ch == '/') ch = '\\';
  }
  return path;
}

export auto is_separator(wchar_t ch) -> bool {
  return ch == L'\\' || ch == L'/';
}

export auto is_separator(char ch) -> bool { return ch == '\\' || ch == '/'; }

export auto unc_root_length(std::wstring_view path) -> std::optional<size_t> {
  if (path.size() < 3 || !is_separator(path[0]) || !is_separator(path[1])) {
    return std::nullopt;
  }

  const auto server_end = path.find_first_of(L"\\/", 2);
  if (server_end == std::wstring_view::npos) return path.size();

  const auto share_start = server_end + 1;
  const auto share_end = path.find_first_of(L"\\/", share_start);
  if (share_end == std::wstring_view::npos) return path.size();
  return share_end;
}

export auto unc_root_length(std::string_view path) -> std::optional<size_t> {
  if (path.size() < 3 || !is_separator(path[0]) || !is_separator(path[1])) {
    return std::nullopt;
  }

  const auto server_end = path.find_first_of("\\/", 2);
  if (server_end == std::string_view::npos) return path.size();

  const auto share_start = server_end + 1;
  const auto share_end = path.find_first_of("\\/", share_start);
  if (share_end == std::string_view::npos) return path.size();
  return share_end;
}

export auto strip_trailing_separators(std::wstring_view path)
    -> std::wstring_view {
  while (path.size() > 1 && is_separator(path.back())) {
    if (path.size() == 3 && path[1] == L':') break;
    if (auto root_len = unc_root_length(path);
        root_len && path.size() <= *root_len + 1) {
      break;
    }
    path.remove_suffix(1);
  }
  return path;
}

export auto strip_trailing_separators(std::string_view path)
    -> std::string_view {
  while (path.size() > 1 && is_separator(path.back())) {
    if (path.size() == 3 && path[1] == ':') break;
    if (auto root_len = unc_root_length(path);
        root_len && path.size() <= *root_len + 1) {
      break;
    }
    path.remove_suffix(1);
  }
  return path;
}

export auto is_dos_device_name_w(std::wstring_view path) -> bool;
export auto attributes_w(std::wstring_view path) -> DWORD;
export auto attributes_are_directory(DWORD attrs) -> bool;
export auto attributes_are_regular_file(DWORD attrs) -> bool;
export auto valid_attributes(DWORD attrs) -> bool;
export auto current_directory_w() -> std::wstring;

// ---------------------------------------------------------------------------
// Shell namespace (niubash#346, the "coreutils own the shell tree" boundary).
//
// rubash hands its own applets the shell's POSIX spellings verbatim, so the
// applets resolve them here, mirroring rubash's shell_path_to_windows: "/",
// every path below the configured install root, /tmp, /var/tmp, /home and the
// /mnt/X WSL drive form. A foreign process never sees these spellings — rubash
// translates every non-coreutils child to a native path — so this is the
// spelling of the shell tree only, not a general POSIX emulation layer. It is
// inert unless a shell root is configured (the gate the "/" case always used),
// so standalone WinuxCmd keeps its native-dialect behaviour.
// ---------------------------------------------------------------------------
namespace shell_ns {

inline auto read_env_w(const wchar_t* name) -> std::wstring {
  const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
  if (needed == 0) return {};
  std::wstring value(needed, L'\0');
  const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
  if (written == 0 || written >= needed) return {};
  value.resize(written);
  return value;
}

inline auto is_drive_letter(wchar_t ch) -> bool {
  return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

// The configured install root (WINUXSH_ROOT and its neutral/compat aliases).
inline auto root() -> const std::wstring& {
  static const std::wstring value = [] {
    for (const auto* name : {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT",
                             L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
      std::wstring candidate = read_env_w(name);
      if (!candidate.empty()) return candidate;
    }
    return std::wstring{};
  }();
  return value;
}

// MSYS single-character drive form "/X" or "/X/...". No shell mount name
// collides (they are all multi-letter).
inline auto is_drive_form(std::wstring_view normalized) -> bool {
  return normalized.size() >= 2 && is_separator(normalized[0]) &&
         is_drive_letter(normalized[1]) &&
         (normalized.size() == 2 || is_separator(normalized[2]));
}

// Fold an environment-provided base ("C:/x", "/c/x") into the backslash
// native form join_tail expects.
inline auto nativeize_base_w(std::wstring_view base) -> std::wstring {
  std::wstring value = normalize_separators(std::wstring(base));
  if (value.size() >= 2 && value[0] == L'\\' && is_drive_letter(value[1]) &&
      (value.size() == 2 || value[2] == L'\\')) {
    if (value[1] >= L'a' && value[1] <= L'z') {
      value[1] = static_cast<wchar_t>(value[1] - L'a' + L'A');
    }
    value = value[1] + (L":" + value.substr(2));
  }
  return value;
}

inline auto tmp_base_w() -> std::wstring {
  for (const auto* name : {L"TMPDIR", L"TEMP", L"TMP"}) {
    std::wstring candidate = read_env_w(name);
    if (!candidate.empty()) return nativeize_base_w(candidate);
  }
  return {};
}

inline auto home_parent_w() -> std::wstring {
  for (const auto* name : {L"HOME", L"USERPROFILE"}) {
    std::wstring candidate = read_env_w(name);
    if (candidate.empty()) continue;
    const std::filesystem::path path(nativeize_base_w(candidate));
    if (path.has_parent_path()) return path.parent_path().wstring();
  }
  return {};
}

// Append a shell tail (the part after the mount name, with or without a
// leading separator) to a native base, folding "." and ".." lexically like
// rubash's map_logical_path.
inline auto join_tail(std::wstring_view tail, std::wstring base)
    -> std::wstring {
  std::filesystem::path joined(std::move(base));
  size_t start = 0;
  for (;;) {
    const size_t end = tail.find(L'/', start);
    const std::wstring_view component = tail.substr(
        start, end == std::wstring_view::npos ? std::wstring_view::npos
                                              : end - start);
    if (component == L"..") {
      if (joined.has_parent_path()) joined = joined.parent_path();
    } else if (!component.empty() && component != L".") {
      joined /= std::wstring(component);
    }
    if (end == std::wstring_view::npos) break;
    start = end + 1;
  }
  return joined.wstring();
}

inline auto has_prefix_ci(std::wstring_view value, std::wstring_view prefix)
    -> bool {
  if (value.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    wchar_t a = value[i];
    wchar_t b = prefix[i];
    if (a >= L'a' && a <= L'z') a = static_cast<wchar_t>(a - L'a' + L'A');
    if (b >= L'a' && b <= L'z') b = static_cast<wchar_t>(b - L'a' + L'A');
    if (a != b) return false;
  }
  return true;
}

}  // namespace shell_ns

// Map a shell-namespace spelling to a native path, or nullopt when the
// operand is not part of the shell namespace (a native/relative path, a
// double-slash spelling, /dev/*, a drive form handled by the caller, or an
// unconfigured root).
auto shell_tree_map_w(std::wstring_view normalized)
    -> std::optional<std::wstring> {
  if (shell_ns::root().empty()) return std::nullopt;
  if (normalized.empty() || !is_separator(normalized[0])) return std::nullopt;
  if (normalized.size() >= 2 && is_separator(normalized[1])) return std::nullopt;
  if (shell_ns::is_drive_form(normalized)) return std::nullopt;
  // /dev is a capability namespace resolved by resolve_pseudo_device_w; its
  // remaining entries must not silently become ordinary files.
  if (normalized == L"/dev" || normalized.starts_with(L"/dev/"))
    return std::nullopt;

  if (normalized == L"/tmp" || normalized.starts_with(L"/tmp/")) {
    std::wstring base = shell_ns::tmp_base_w();
    if (base.empty()) return std::nullopt;
    return shell_ns::join_tail(normalized.substr(4), std::move(base));
  }
  if (normalized == L"/var/tmp" || normalized.starts_with(L"/var/tmp/")) {
    std::wstring base = shell_ns::tmp_base_w();
    if (base.empty()) return std::nullopt;
    return shell_ns::join_tail(
        normalized.substr(8),
        (std::filesystem::path(std::move(base)) / L"var" / L"tmp").wstring());
  }
  if (normalized == L"/home" || normalized.starts_with(L"/home/")) {
    std::wstring base = shell_ns::home_parent_w();
    if (base.empty()) return std::nullopt;
    return shell_ns::join_tail(normalized.substr(5), std::move(base));
  }
  if (normalized.size() >= 6 && normalized.starts_with(L"/mnt/") &&
      shell_ns::is_drive_letter(normalized[5]) &&
      (normalized.size() == 6 || is_separator(normalized[6]))) {
    wchar_t drive = normalized[5];
    if (drive >= L'a' && drive <= L'z') {
      drive = static_cast<wchar_t>(drive - L'a' + L'A');
    }
    std::wstring base;
    base.push_back(drive);
    base.append(L":\\");
    const std::wstring_view rest =
        normalized.size() > 6 ? normalized.substr(7) : std::wstring_view{};
    return shell_ns::join_tail(rest, std::move(base));
  }

  // Everything else (including the bare "/") is relative to the install root.
  return shell_ns::join_tail(normalized.substr(1),
                             shell_ns::nativeize_base_w(shell_ns::root()));
}

// Resolve a shell-namespace operand through the shell tree, applying the MSYS
// open order the engine's niubash#177 funnel used to apply: an existing
// as-spelled path wins; otherwise the toolset's `X.exe` leaf resolves the bare
// `/usr/bin/X` spelling. Without this, `ls /usr/bin/seq`, `test -f
// /usr/bin/seq` and `head -1 /usr/bin/seq` (only `seq.exe` on disk) would
// regress once rubash stops translating the spelling for the coreutils class.
auto shell_tree_to_native_w(std::wstring_view normalized)
    -> std::optional<std::wstring> {
  auto mapped = shell_tree_map_w(normalized);
  if (!mapped) return std::nullopt;
  if (!mapped->empty() && !is_separator(mapped->back()) &&
      !valid_attributes(attributes_w(*mapped))) {
    std::wstring exe = *mapped + L".exe";
    if (valid_attributes(attributes_w(exe))) {
      *mapped = std::move(exe);
    }
  }
  return mapped;
}

// Re-express a native path back in the shell namespace: the canonical
// spelling of a path under a shell mount is its POSIX mount name. Returns
// nullopt when the path is not under any mount (native paths stay native).
auto shell_tree_display_w(std::wstring_view native)
    -> std::optional<std::wstring> {
  const std::wstring value = normalize_separators(std::wstring(native));

  const auto under =
      [&](std::wstring_view raw_base,
          std::wstring_view mount) -> std::optional<std::wstring> {
    const std::wstring base = shell_ns::nativeize_base_w(raw_base);
    if (base.empty() || !shell_ns::has_prefix_ci(value, base))
      return std::nullopt;
    if (value.size() > base.size() && !is_separator(value[base.size()]))
      return std::nullopt;
    std::wstring result(mount);
    for (size_t i = base.size(); i < value.size(); ++i) {
      result.push_back(value[i] == L'\\' ? L'/' : value[i]);
    }
    return result;
  };

  const std::wstring root = shell_ns::root();
  if (!root.empty()) {
    if (auto mapped = under(root, L"")) {
      return mapped->empty() ? std::wstring(L"/") : *mapped;
    }
  }
  const std::wstring tmp = shell_ns::tmp_base_w();
  if (!tmp.empty()) {
    if (auto mapped = under(
            (std::filesystem::path(tmp) / L"var" / L"tmp").wstring(),
            L"/var/tmp")) {
      return mapped;
    }
    if (auto mapped = under(tmp, L"/tmp")) return mapped;
  }
  return under(shell_ns::home_parent_w(), L"/home");
}

// Fold the Cygwin "/cygdrive/X" (#1145) and WSL interop "/mnt/X" (#1172)
// drive prefixes onto the MSYS single-letter spelling they name: "/e/Data"
// under either prefix folds to "/e/Data". Both shapes — a case-insensitive
// "cygdrive" name and a case-sensitive "mnt" literal (the spelling WSL
// itself emits), each followed by a separator, a drive letter of either
// case, and end-of-string or another separator — carry the exact recognition
// the normalize_api_operand_w fold historically applied inline; extracted
// here so the canonicalize resolver (#1246) runs the same folds instead of
// diverging from the argv boundary on the same spelling. Bare /mnt and
// non-drive children ("/mnt/data", "/mntcc") never fold. Returns nullopt
// when the operand carries neither prefix.
auto fold_posix_drive_prefix_w(std::wstring_view normalized)
    -> std::optional<std::wstring> {
  constexpr std::wstring_view k_cygdrive_name = L"cygdrive";
  if (normalized.size() >= k_cygdrive_name.size() + 3 &&
      is_separator(normalized[0])) {
    bool name_match = true;
    for (size_t i = 0; i < k_cygdrive_name.size(); ++i) {
      wchar_t ch = normalized[1 + i];
      if (ch >= L'A' && ch <= L'Z') {
        ch = static_cast<wchar_t>(ch - L'A' + L'a');
      }
      if (ch != k_cygdrive_name[i]) {
        name_match = false;
        break;
      }
    }
    const size_t drive_pos = k_cygdrive_name.size() + 2;
    const wchar_t drive_ch = normalized[drive_pos];
    const bool drive_letter = (drive_ch >= L'a' && drive_ch <= L'z') ||
                              (drive_ch >= L'A' && drive_ch <= L'Z');
    if (name_match && is_separator(normalized[1 + k_cygdrive_name.size()]) &&
        drive_letter &&
        (normalized.size() == drive_pos + 1 ||
         is_separator(normalized[drive_pos + 1]))) {
      return std::wstring(L"/").append(normalized.substr(drive_pos));
    }
  }
  // The name comparison is case-sensitive by contract: unlike cygdrive, the
  // literal must be lowercase "mnt" byte-for-byte — the spelling WSL itself
  // emits, and the exact shape rubash's windows_mnt_drive_argument accepts,
  // so the two layers never drift (rubash#444, rubash#592). "/mnt/data",
  // "/mnt/cfoo" and "/mnt/123" are ordinary POSIX words under a literal /mnt
  // directory, bare /mnt (the WSL root) is never a drive, and without the
  // separator check "/mntcc" would lexically fold to "/c" and name C:\.
  constexpr std::wstring_view k_mnt_name = L"mnt";
  if (normalized.size() >= k_mnt_name.size() + 3 &&
      is_separator(normalized[0])) {
    bool name_match = true;
    for (size_t i = 0; i < k_mnt_name.size(); ++i) {
      if (normalized[1 + i] != k_mnt_name[i]) {
        name_match = false;
        break;
      }
    }
    const size_t drive_pos = k_mnt_name.size() + 2;
    const wchar_t drive_ch = normalized[drive_pos];
    const bool drive_letter = (drive_ch >= L'a' && drive_ch <= L'z') ||
                              (drive_ch >= L'A' && drive_ch <= L'Z');
    if (name_match && is_separator(normalized[k_mnt_name.size() + 1]) &&
        drive_letter &&
        (normalized.size() == drive_pos + 1 ||
         is_separator(normalized[drive_pos + 1]))) {
      return std::wstring(L"/").append(normalized.substr(drive_pos));
    }
  }
  return std::nullopt;
}

// Fold the MSYS single-letter drive dialect onto its native spelling: "/e"
// and "/e/Data" name "E:\" and "E:\Data". Every boundary that accepts a raw
// POSIX spelling goes through this one helper so the recognition rule stays
// single-sourced: the first segment is exactly one drive letter ("usr",
// "cygdrive", "mntcc" are never drives; the /cygdrive/X (#1145) and /mnt/X
// (#1172) prefixes are folded into this same spelling by their own branches
// before this rule is consulted). Returns nullopt for any other spelling.
// Consumers: normalize_api_operand_w (the argv operand boundary) and
// canonicalize_path_w (the realpath/readlink/stat -L resolver, #1231).
auto fold_drive_dialect_w(std::wstring_view normalized)
    -> std::optional<std::wstring> {
  if (!shell_ns::is_drive_form(normalized)) {
    return std::nullopt;
  }
  std::wstring folded;
  wchar_t drive = normalized[1];
  if (drive >= L'a' && drive <= L'z') {
    drive = static_cast<wchar_t>(drive - L'a' + L'A');
  }
  folded.push_back(drive);
  folded.append(L":\\");
  if (normalized.size() > 3) {
    folded.append(normalized.substr(3));
  }
  return normalize_separators(std::move(folded));
}

// Fold literal colons in the operand's final component onto the
// private-use carrier U+F03A (#1283). A ':' in a final component is
// never a drive colon (those live in earlier components) and Win32
// reads it as an alternate-data-stream separator, while the shell's
// own path layer stores such names on disk with ':' encoded as
// U+F03A — the spelling redirects actually create and MSYS resolves.
// Before this fold the same operand spelled three ways across applets:
// "D:name" resolved drive-relative through GetFullPathNameW (touch,
// realpath), as the literal carrier file (cat), or not at all (stat,
// mv, head, tail, grep). Folding here, at the one operand boundary,
// makes every consumer resolve the one literal file. The bare drive
// ("D:") keeps its drive meaning, and verbatim/device spellings
// ("\\?\", "\\.\") are the caller's own responsibility and pass
// through untouched. Consumers: normalize_api_operand_w (the argv
// operand boundary) and canonicalize_path_w (the realpath/readlink
// resolver), mirroring the fold_posix_drive_prefix_w sharing.
auto fold_carrier_colons_w(std::wstring_view normalized)
    -> std::optional<std::wstring> {
  if (normalized.starts_with(L"\\\\?\\") ||
      normalized.starts_with(L"\\\\.\\")) {
    return std::nullopt;
  }
  const size_t sep = normalized.find_last_of(L"\\/");
  const std::wstring_view tail =
      sep == std::wstring_view::npos ? normalized : normalized.substr(sep + 1);
  if (tail.find(L':') == std::wstring_view::npos) {
    return std::nullopt;
  }
  if (sep == std::wstring_view::npos && normalized.size() == 2) {
    return std::nullopt;  // bare "D:" — the drive itself, not a file name
  }
  std::wstring folded(normalized);
  const size_t tail_begin =
      sep == std::wstring_view::npos ? 0 : sep + 1;
  for (size_t i = tail_begin; i < folded.size(); ++i) {
    if (folded[i] == L':') folded[i] = L'\uF03A';
  }
  return folded;
}

export auto normalize_api_operand_w(std::wstring_view path) -> std::wstring {
  const bool had_trailing_separator =
      strip_trailing_separators(path).size() != path.size();
  std::wstring normalized(strip_trailing_separators(path));
  if (!normalized.empty() &&
      normalized.find_first_not_of(L'/') == std::wstring::npos) {
    normalized = L"/";
  }
  // Cygwin drive prefix: /cygdrive/d/... names the same file as /d/... and
  // D:\... (the GNU runtime layer resolves every spelling; cygpath/mktemp
  // additionally recognize the prefix in raw argument text). Folding it to
  // the MSYS spelling first keeps a single drive-letter branch below owning
  // every POSIX drive form (#1145).  Both separators are accepted anywhere
  // in the prefix: callers routinely hand this function already-preferred
  // backslash forms (e.g. a "\cygdrive\d\src\*" enumeration pattern). The
  // WSL interop prefix folds in the same step (#1172), and the recognition
  // lives in the shared fold_posix_drive_prefix_w so canonicalize_path_w
  // (#1246) runs a byte-identical sequence instead of diverging on the same
  // spelling.
  if (auto prefix = fold_posix_drive_prefix_w(normalized)) {
    normalized = std::move(*prefix);
  }
  if (auto folded = fold_drive_dialect_w(normalized)) {
    normalized = std::move(*folded);
  }
  // #1283: literal colons in the final component name the carrier file
  // the shell puts on disk, never an ADS or a drive-relative path.
  if (auto carrier = fold_carrier_colons_w(normalized)) {
    normalized = std::move(*carrier);
  }
  // niubash#346: after the cygdrive/drive folds above, any remaining
  // "/"-prefixed operand is a shell-namespace spelling (the bare "/", a
  // virtual root, /tmp, /home, /mnt/X). The bundled coreutils receive these
  // verbatim, so resolve them through the shell tree exactly like rubash's
  // shell_path_to_windows.
  if (!normalized.empty() && is_separator(normalized[0])) {
    if (auto mapped = shell_tree_to_native_w(normalized)) {
      normalized = std::move(*mapped);
    }
  }
  // A trailing separator means the operand must name a directory (POSIX
  // ENOTDIR). If the stripped target is not a directory, keep a separator so
  // the OS rejects opens/creates instead of silently touching the regular
  // file (#1052 — stripping turned "file/" into "file" and truncated it).
  if (had_trailing_separator &&
      !attributes_are_directory(attributes_w(normalized))) {
    normalized.push_back(L'\\');
  }
  return normalized;
}

export auto resolve_pseudo_device_w(std::wstring_view path)
    -> std::optional<std::wstring>;

// [GNU] Standard-stream pseudo paths: /dev/std{in,out,err} plus the
// /dev/fd/N and /proc/*/fd/N spellings. MSYS/Git-Bash expands /dev/std* in
// native argv to /proc/self/fd/N (sometimes already resolved to
// /proc/<pid>/fd/N), so every spelling is recognized here (#1056).
// Returns the CRT file descriptor (0, 1, 2) the operand refers to.
export auto pseudo_device_std_fd_w(std::wstring_view path)
    -> std::optional<int>;

export auto pseudo_device_std_fd(std::string_view path) -> std::optional<int> {
  return pseudo_device_std_fd_w(from_utf8(path));
}

export auto normalize_api_operand(std::string_view path) -> std::string {
  // Delegate to the wide implementation so MSYS/Git-Bash style operands such
  // as "/d/repo/file" are converted to "D:\repo\file" exactly like
  // make_api_path_operand does; otherwise return the stripped path unchanged.
  // POSIX pseudo-devices resolve to their Windows equivalents first so tools
  // reading through this boundary (od, dd, ...) see /dev/null etc. (#276).
  const std::wstring wide = from_utf8(path);
  if (auto pseudo = resolve_pseudo_device_w(wide)) {
    return to_utf8(*pseudo);
  }
  return to_utf8(normalize_api_operand_w(wide));
}

// A component ending in '.' or ' ' that is not "." or ".." is a literal
// on-disk name (NTFS keeps such names under the \\?\ namespace), but
// GetFullPathNameW strips the suffix and absolutizes onto a different
// object — "D:\x\inner." comes back as "D:\x\inner" (#1283).
auto has_literal_dot_space_component(std::wstring_view native) -> bool {
  size_t start = 0;
  for (;;) {
    const size_t end = native.find(L'\\', start);
    const std::wstring_view component = native.substr(
        start, end == std::wstring_view::npos ? std::wstring_view::npos
                                            : end - start);
    if (!component.empty() && component != L"." && component != L".." &&
        (component.back() == L'.' || component.back() == L' ')) {
      return true;
    }
    if (end == std::wstring_view::npos) return false;
    start = end + 1;
  }
}

// Split an absolute path into its root ("D:\" or "\\server\share\") and the
// components below it.
auto split_absolute_components_w(std::wstring_view abs,
                                 std::vector<std::wstring_view>& components)
    -> std::wstring {
  std::wstring root;
  std::wstring_view rest;
  if (auto root_len = unc_root_length(abs); root_len) {
    root.assign(abs.substr(0, *root_len));
    rest = *root_len < abs.size() ? abs.substr(*root_len + 1)
                                  : std::wstring_view{};
  } else {
    root.assign(abs.substr(0, 2));
    rest = abs.size() > 3 ? abs.substr(3) : std::wstring_view{};
  }
  root.push_back(L'\\');
  size_t start = 0;
  for (;;) {
    const size_t end = rest.find(L'\\', start);
    const std::wstring_view component = rest.substr(
        start, end == std::wstring_view::npos ? std::wstring_view::npos
                                            : end - start);
    if (!component.empty()) components.push_back(component);
    if (end == std::wstring_view::npos) break;
    start = end + 1;
  }
  return root;
}

// Absolutize lexically so components ending in '.' or ' ' survive: every
// component except "." and ".." is literal, matching what MSYS resolves and
// what the \\?\ namespace stores. GetFullPathNameW cannot be used for these
// spellings because it rewrites the leaf name (#1283).
auto to_extended_path_preserving(std::wstring_view native) -> std::wstring {
  std::wstring base;
  std::wstring_view tail;
  if (native.size() >= 2 && is_separator(native[0]) &&
      is_separator(native[1])) {
    // UNC absolute: absolutization is identity for the root part.
    base.assign(native);
  } else if (is_separator(native[0])) {
    // Rooted on the current drive: keep only its root as the base.
    std::vector<std::wstring_view> ignored;
    base = split_absolute_components_w(current_directory_w(), ignored);
    tail = native.substr(1);
  } else if (native.size() > 2 && native[1] == L':' &&
             is_separator(native[2])) {
    base.assign(native.substr(0, 3));
    tail = native.substr(3);
  } else if (native.size() >= 2 && native[1] == L':') {
    // Drive-relative "D:rel": resolve against the drive's own cwd.
    std::wstring probe;
    probe.push_back(native[0]);
    probe.append(L":.");
    wchar_t abs_buf[32768];
    const DWORD len =
        GetFullPathNameW(probe.c_str(), 32768, abs_buf, nullptr);
    base.assign(len != 0 && len < 32768 ? std::wstring(abs_buf, len)
                                        : std::move(probe));
    tail = native.substr(2);
  } else {
    base = current_directory_w();
    tail = native;
  }

  std::vector<std::wstring_view> components;
  const std::wstring root = split_absolute_components_w(base, components);

  size_t start = 0;
  for (;;) {
    const size_t end = tail.find(L'\\', start);
    const std::wstring_view component = tail.substr(
        start, end == std::wstring_view::npos ? std::wstring_view::npos
                                            : end - start);
    if (component == L"..") {
      if (!components.empty()) components.pop_back();
    } else if (!component.empty() && component != L".") {
      components.push_back(component);
    }
    if (end == std::wstring_view::npos) break;
    start = end + 1;
  }

  std::wstring absolute = std::move(root);
  for (size_t i = 0; i < components.size(); ++i) {
    if (i != 0) absolute.push_back(L'\\');
    absolute.append(components[i]);
  }

  if (absolute.size() == 3 && absolute[1] == L':') return absolute;
  if (absolute.size() >= 2 && absolute.compare(0, 2, L"\\\\") == 0) {
    return L"\\\\?\\UNC\\" + absolute.substr(2);
  }
  return L"\\\\?\\" + absolute;
}

// True when the final component is a DOS device name but the path as a
// whole is not the bare device: "C:\dir\NUL" names an ordinary on-disk
// entry, while bare "NUL" names the device itself (#1297).
auto has_device_name_leaf(std::wstring_view native) -> bool {
  const std::wstring_view stripped = strip_trailing_separators(native);
  const size_t sep = stripped.find_last_of(L'\\');
  if (sep == std::wstring_view::npos) return false;
  return is_dos_device_name_w(stripped.substr(sep + 1));
}

export auto to_extended_path(std::wstring_view path) -> std::wstring {
  if (path.size() >= 4 && path.compare(0, 4, L"\\\\?\\") == 0) {
    return std::wstring(path);
  }

  // GetFullPathNameW mis-parses a mixed-separator root ("/\x" returns the
  // garbage UNC form "\\x" instead of "<drive>:\x"), so fold every separator
  // first; Win32 treats '/' and '\' interchangeably elsewhere (#1145).
  std::wstring native(normalize_separators(std::wstring(path)));

  // GetFullPathNameW collapses any path whose final component is a DOS
  // device name onto the device itself: "C:\dir\NUL" comes back as
  // "\\.\NUL", and the extended result names the device namespace
  // instead of the on-disk entry, so every later probe reports a real
  // file or directory as missing (#1297, verified on Windows CI). Such
  // operands name a literal entry under the \\?\ namespace, so
  // absolutize them lexically like the dot/space spellings below.
  if (has_device_name_leaf(native)) {
    return to_extended_path_preserving(native);
  }
  if (has_literal_dot_space_component(native) &&
      !(native.size() >= 4 &&
        (native.compare(0, 4, L"\\\\?\\") == 0 ||
         native.compare(0, 4, L"\\\\.\\") == 0))) {
    return to_extended_path_preserving(native);
  }

  wchar_t abs_buf[32768];
  DWORD len = GetFullPathNameW(native.c_str(), 32768, abs_buf, nullptr);
  if (len == 0 || len >= 32768) return native;

  std::wstring absolute(abs_buf, len);
  // A bare drive root ("D:\") must stay un-prefixed: attribute probes reject
  // the \\?\ spelling of a root — verified GetFileAttributesW("\\?\D:\") ->
  // INVALID_FILE_ATTRIBUTES while "D:\" -> FILE_ATTRIBUTE_DIRECTORY — and a
  // root can never exceed MAX_PATH, so nothing is lost (#1145: `ls /`).
  if (absolute.size() == 3 && absolute[1] == L':') {
    return absolute;
  }
  if (absolute.size() >= 2 && absolute.compare(0, 2, L"\\\\") == 0) {
    return L"\\\\?\\UNC\\" + absolute.substr(2);
  }
  return L"\\\\?\\" + absolute;
}

export struct ApiPathOperand {
  std::wstring original;
  std::wstring normalized;
  std::wstring extended;
  bool had_trailing_separator = false;
};

// [GNU] POSIX pseudo-devices that GNU environments expose under /dev but
// that have no directory entry on Windows. Mapping them at the shared path
// boundary (instead of relying on an external runtime directory such as
// niubash's dev/) keeps tools like dd/tee/cat/pr working when they receive
// literal /dev/* operands (#276 follow-up, uutils#9745).
export auto pseudo_device_std_fd_w(std::wstring_view path)
    -> std::optional<int> {
  if (path == L"/dev/stdin") return 0;
  if (path == L"/dev/stdout") return 1;
  if (path == L"/dev/stderr") return 2;

  const auto fd_of_tail = [](std::wstring_view tail) -> std::optional<int> {
    if (tail.size() == 1 && tail[0] >= L'0' && tail[0] <= L'2') {
      return static_cast<int>(tail[0] - L'0');
    }
    return std::nullopt;
  };

  if (path.starts_with(L"/dev/fd/")) {
    return fd_of_tail(path.substr(8));
  }
  // /proc/self/fd/N and the MSYS-resolved /proc/<pid>/fd/N form.
  if (path.starts_with(L"/proc/")) {
    const std::wstring_view rest = path.substr(6);
    const auto fd_pos = rest.find(L"/fd/");
    if (fd_pos != std::wstring_view::npos) {
      const std::wstring_view pid = rest.substr(0, fd_pos);
      const bool valid_pid =
          !pid.empty() &&
          (pid == L"self" || std::ranges::all_of(pid, [](wchar_t ch) {
             return ch >= L'0' && ch <= L'9';
           }));
      if (valid_pid) return fd_of_tail(rest.substr(fd_pos + 4));
    }
  }
  return std::nullopt;
}

export auto resolve_pseudo_device_w(std::wstring_view path)
    -> std::optional<std::wstring> {
  if (path == L"/dev/null") return std::wstring(L"NUL");
  if (path == L"/dev/tty") return std::wstring(L"CONIN$");
  if (auto fd = pseudo_device_std_fd_w(path)) {
    return *fd == 0 ? std::wstring(L"CONIN$") : std::wstring(L"CONOUT$");
  }
  // Bare DOS device names: MSYS/Git-Bash rewrites /dev/null to "nul" in
  // native argv, and users may type NUL/CON directly. They resolve only in
  // the Win32 device namespace, so they must pass through without the \\?\
  // prefix (#1055).
  static constexpr std::wstring_view kDosDevices[] = {
      L"NUL",  L"CON",  L"PRN",  L"AUX",  L"CONIN$", L"CONOUT$",
      L"COM1", L"COM2", L"COM3", L"COM4", L"COM5",   L"COM6",
      L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2",   L"LPT3",
      L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8",   L"LPT9"};
  for (const auto device : kDosDevices) {
    if (path.size() != device.size()) continue;
    bool equal = true;
    for (size_t i = 0; i < path.size(); ++i) {
      wchar_t a = path[i];
      wchar_t b = device[i];
      if (a >= L'a' && a <= L'z') a = static_cast<wchar_t>(a - L'a' + L'A');
      if (b >= L'a' && b <= L'z') b = static_cast<wchar_t>(b - L'a' + L'A');
      if (a != b) {
        equal = false;
        break;
      }
    }
    if (equal) return std::wstring(path);
  }
  return std::nullopt;
}

// True when the operand is a bare DOS device name.
//
// Device names resolve through the Win32 device namespace, which the \\?\
// extended-length prefix bypasses: to_extended_path would run
// GetFullPathNameW("NUL"), resolve the name against the current directory and
// hand back "\\?\<cwd>\NUL" - an ordinary, missing file. Every Win32 call must
// therefore receive these names verbatim, which is exactly the invariant
// make_api_path_operand_w establishes when it resolves a pseudo-device; this
// predicate lets the attribute probes honour it too. Verified with
// GetFileAttributesW: "NUL" -> 0x20 (valid), "\\?\<cwd>\NUL" -> INVALID.
// Keep the list in sync with resolve_pseudo_device_w, which is where the
// pseudo-devices that reach this function are produced.
export auto is_dos_device_name_w(std::wstring_view path) -> bool {
  path = strip_trailing_separators(path);

  // An already explicit device namespace ("\\.\NUL") needs no adjustment.
  if (path.size() > 4 && path.compare(0, 4, L"\\\\.\\") == 0) return true;

  // A device name only resolves when it is the whole operand: "dir\NUL" names
  // an ordinary file, and so does "\\?\C:\dir\NUL".
  if (path.find_first_of(L"\\/") != std::wstring_view::npos) return false;

  std::wstring upper;
  upper.reserve(path.size());
  for (const wchar_t ch : path) {
    upper.push_back((ch >= L'a' && ch <= L'z')
                        ? static_cast<wchar_t>(ch - L'a' + L'A')
                        : ch);
  }

  if (upper == L"NUL" || upper == L"CON" || upper == L"PRN" ||
      upper == L"AUX" || upper == L"CONIN$" || upper == L"CONOUT$") {
    return true;
  }
  return upper.size() == 4 &&
         (upper.starts_with(L"COM") || upper.starts_with(L"LPT")) &&
         upper[3] >= L'1' && upper[3] <= L'9';
}

// True when the operand denotes a Windows character device, either directly as
// a DOS device name or as a POSIX pseudo-device that resolves onto one. GNU
// reports /dev/null as a character special file, so `test -c /dev/null` is true
// and `test -b /dev/null` is false there; both predicates need this answer.
export auto is_character_device_w(std::wstring_view path) -> bool {
  if (auto pseudo = resolve_pseudo_device_w(path)) {
    return is_dos_device_name_w(*pseudo);
  }
  return is_dos_device_name_w(path);
}

export auto is_character_device(std::string_view path) -> bool {
  return is_character_device_w(from_utf8(path));
}

export auto make_api_path_operand_w(std::wstring_view path) -> ApiPathOperand {
  ApiPathOperand operand;
  operand.original = std::wstring(path);
  if (auto pseudo = resolve_pseudo_device_w(operand.original)) {
    // DOS device names (NUL, CONIN$, CONOUT$) must NOT carry the \\?\
    // prefix: the \\?\ namespace bypasses Win32 device-name resolution, and
    // GetFullPathNameW would resolve "NUL" against the current directory
    // instead. Keep the device name verbatim in both normalized and extended.
    operand.normalized = *pseudo;
    operand.extended = *pseudo;
    operand.had_trailing_separator = false;
    return operand;
  }
  operand.normalized = normalize_api_operand_w(operand.original);
  operand.extended = to_extended_path(operand.normalized);
  operand.had_trailing_separator =
      strip_trailing_separators(std::wstring_view(operand.original)).size() !=
      operand.original.size();
  return operand;
}

export auto make_api_path_operand(std::string_view path) -> ApiPathOperand {
  return make_api_path_operand_w(from_utf8(path));
}

// Probe the target's attributes ignoring the trailing-separator enforcement:
// extended keeps the separator for non-directory operands (#1052), which
// GetFileAttributesW rejects, so strip it before probing.
export auto operand_target_attributes_w(const ApiPathOperand& operand)
    -> DWORD {
  const std::wstring_view probe =
      operand.had_trailing_separator
          ? strip_trailing_separators(std::wstring_view(operand.extended))
          : std::wstring_view(operand.extended);
  return attributes_w(probe);
}

// errno-style error for opening/creating a file at this operand: 0 when the
// operand may proceed; ENOTDIR when a trailing separator named a regular
// file on a read-open; ENOENT when it named a missing path (#1052).
// For write/create opens GNU mirrors the Linux quirk where O_CREAT on
// "file/" reports EISDIR, and opening a directory for writing is EISDIR too.
export auto operand_file_open_error(const ApiPathOperand& operand,
                                    bool for_write = false) -> int {
  const DWORD attrs = operand_target_attributes_w(operand);
  if (operand.had_trailing_separator) {
    if (!valid_attributes(attrs)) return ENOENT;
    if (attributes_are_directory(attrs)) return for_write ? EISDIR : 0;
    return for_write ? EISDIR : ENOTDIR;
  }
  if (for_write && attributes_are_directory(attrs)) return EISDIR;
  return 0;
}

export auto attributes_w(std::wstring_view path) -> DWORD {
  // DOS device names resolve only in the Win32 device namespace, which the
  // \\?\ extended-length prefix bypasses (see is_dos_device_name_w): probe
  // them verbatim. resolve_pseudo_device_w also covers the /dev/* spellings
  // and the MSYS-rewritten bare names such as "nul".
  std::wstring_view probe = path;
  std::wstring resolved;
  if (auto pseudo = resolve_pseudo_device_w(path)) {
    resolved = std::move(*pseudo);
    probe = resolved;
  }
  if (is_dos_device_name_w(probe)) {
    const std::wstring verbatim(strip_trailing_separators(probe));
    return GetFileAttributesW(verbatim.c_str());
  }

  // Keep all attribute probes on the same extended-path API boundary as
  // file_io. This avoids MAX_PATH failures for otherwise valid paths.
  const std::wstring native = probe.starts_with(L"\\\\?\\")
                                  ? std::wstring(probe)
                                  : to_extended_path(probe);
  return GetFileAttributesW(native.c_str());
}

// Full attribute record for an operand that may name a character device.
//
// GetFileAttributesExW is the one attribute API that rejects DOS device names
// outright. Probed against kernel32 on Windows 11:
//
//   GetFileAttributesW("NUL")      -> 0x20   (FILE_ATTRIBUTE_ARCHIVE)
//   GetFileAttributesExW("NUL")    -> FALSE, ERROR_INVALID_PARAMETER (87)
//   GetFileAttributesExW("CONIN$") -> FALSE, ERROR_INVALID_FUNCTION (1)
//
// So a tool that renders a full stat record cannot use the Ex variant for
// /dev/* operands: the device resolves correctly and then the query fails. This
// helper keeps both APIs behind one device-aware call - device names are
// answered from GetFileAttributesW with a zeroed size/time record, which is
// exactly what a character device reports anyway (GNU prints a size of 0 for
// /dev/null). Ordinary paths keep the Ex variant so long paths and reparse
// points behave as before.
export auto file_attribute_data_w(std::wstring_view path,
                                  WIN32_FILE_ATTRIBUTE_DATA& out) -> bool {
  out = WIN32_FILE_ATTRIBUTE_DATA{};

  std::wstring probe;
  if (auto pseudo = resolve_pseudo_device_w(path)) {
    probe = *pseudo;
  } else {
    probe.assign(path);
  }

  if (is_dos_device_name_w(probe)) {
    const std::wstring verbatim(strip_trailing_separators(probe));
    const DWORD attrs = GetFileAttributesW(verbatim.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    out.dwFileAttributes = attrs;
    return true;
  }

  const std::wstring native =
      probe.starts_with(L"\\\\?\\") ? probe : to_extended_path(probe);
  return GetFileAttributesExW(native.c_str(), GetFileExInfoStandard, &out) != 0;
}

export auto file_attribute_data(std::string_view path,
                                WIN32_FILE_ATTRIBUTE_DATA& out) -> bool {
  return file_attribute_data_w(from_utf8(path), out);
}

export auto valid_attributes(DWORD attrs) -> bool {
  return attrs != INVALID_FILE_ATTRIBUTES;
}

// [GNU] canonicalize_filename_mode() existence rules shared by realpath and
// readlink (-f/-e/-m).
export enum class CanonMode {
  all_but_last,  // every component but the last must exist (realpath, -f)
  existing,      // every component must exist (realpath -e, readlink -e)
  missing,       // no component needs to exist (realpath -m, readlink -m)
};

export auto canonicalize_path_w(std::wstring_view path, CanonMode mode,
                                bool expand_symlinks, bool logical)
    -> std::expected<std::wstring, int>;

// On-disk representation of a WinuxCmd FIFO (#1038). Windows has no
// filesystem FIFO node type, and MSYS2/Cygwin keep their FIFOs purely
// in-runtime (invisible to native tools), so we emulate: a regular file
// whose first bytes are kFifoMarker carrying FILE_ATTRIBUTE_SYSTEM, the
// same trick Cygwin historically used for non-native symlinks.
// Reader/writer opens in file_io bridge the marker to a deterministic
// \\.\pipe\winuxcmd-fifo-<hash> endpoint, so blocking pipe semantics work
// between WinuxCmd processes; non-WinuxCmd readers simply see the marker.
export constexpr std::string_view kFifoMarker = "!<fifo>";

export auto is_winux_fifo_w(std::wstring_view path) -> bool {
  const DWORD attrs = attributes_w(path);
  if (!valid_attributes(attrs) || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      (attrs & FILE_ATTRIBUTE_SYSTEM) == 0) {
    return false;
  }
  const std::wstring native =
      to_extended_path(std::wstring(strip_trailing_separators(path)));
  HANDLE file =
      CreateFileW(native.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  std::array<char, kFifoMarker.size()> buffer{};
  DWORD read = 0;
  const bool ok = ReadFile(file, buffer.data(),
                           static_cast<DWORD>(buffer.size()), &read, nullptr);
  CloseHandle(file);
  return ok && read == buffer.size() &&
         std::string_view(buffer.data(), read) == kFifoMarker;
}

export auto is_winux_fifo(std::string_view path) -> bool {
  return is_winux_fifo_w(from_utf8(path));
}

// Deterministic \\.\pipe endpoint for a marker file, keyed on the
// canonical path so independent processes rendezvous on the same name.
export auto winux_fifo_pipe_name_w(std::wstring_view path) -> std::wstring {
  std::wstring canon;
  if (auto resolved =
          canonicalize_path_w(path, CanonMode::missing, true, false);
      resolved) {
    canon = std::move(*resolved);
  } else {
    canon.assign(path);
  }
  for (auto& ch : canon) {
    if (ch == L'/') ch = L'\\';
    if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
  }
  uint64_t hash = 14695981039346656037ull;  // FNV-1a
  for (const wchar_t ch : canon) {
    for (const int byte : {ch & 0xFF, (ch >> 8) & 0xFF}) {
      hash = (hash ^ static_cast<uint8_t>(byte)) * 1099511628211ull;
    }
  }
  std::wstring name = L"\\\\.\\pipe\\winuxcmd-fifo-";
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(hash));
  name.append(hex, hex + 16);
  return name;
}

// Create a fifo marker file. Returns ERROR_SUCCESS or the Win32 error
// (ERROR_FILE_EXISTS when the name is already taken).
export auto create_winux_fifo_w(std::wstring_view path, bool readonly = false)
    -> DWORD {
  const std::wstring native =
      to_extended_path(std::wstring(strip_trailing_separators(path)));
  HANDLE file = CreateFileW(native.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return GetLastError();
  DWORD written = 0;
  const bool ok =
      WriteFile(file, kFifoMarker.data(),
                static_cast<DWORD>(kFifoMarker.size()), &written, nullptr) &&
      written == kFifoMarker.size();
  const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(native.c_str());
    return write_error;
  }
  DWORD attrs = FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE;
  if (readonly) attrs |= FILE_ATTRIBUTE_READONLY;
  SetFileAttributesW(native.c_str(), attrs);
  return ERROR_SUCCESS;
}

export auto attributes_are_directory(DWORD attrs) -> bool {
  return valid_attributes(attrs) && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

export auto attributes_are_regular_file(DWORD attrs) -> bool {
  return valid_attributes(attrs) && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

export auto attributes_are_reparse_point(DWORD attrs) -> bool {
  return valid_attributes(attrs) && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

export auto current_directory_w() -> std::wstring {
  DWORD len = GetCurrentDirectoryW(0, nullptr);
  if (len == 0) return {};

  std::wstring buffer(len, L'\0');
  DWORD written = GetCurrentDirectoryW(len, buffer.data());
  if (written == 0 || written >= len) return {};
  buffer.resize(written);
  return buffer;
}

export auto current_directory() -> std::string {
  return to_utf8(current_directory_w());
}

export auto parent_path_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.parent_path().wstring();
}

export auto filename_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.filename().wstring();
}

export auto stem_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.stem().wstring();
}

export auto extension_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.extension().wstring();
}

export auto parent_path(std::string_view path) -> std::string {
  return to_utf8(parent_path_w(from_utf8(path)));
}

export auto filename(std::string_view path) -> std::string {
  return to_utf8(filename_w(from_utf8(path)));
}

export auto stem(std::string_view path) -> std::string {
  return to_utf8(stem_w(from_utf8(path)));
}

export auto extension(std::string_view path) -> std::string {
  return to_utf8(extension_w(from_utf8(path)));
}

export auto exists_w(std::wstring_view path) -> bool {
  return valid_attributes(attributes_w(path));
}

export auto is_directory_w(std::wstring_view path) -> bool {
  return attributes_are_directory(attributes_w(path));
}

export auto is_regular_file_w(std::wstring_view path) -> bool {
  return attributes_are_regular_file(attributes_w(path));
}

export auto exists(std::string_view path) -> bool {
  return exists_w(from_utf8(path));
}

export auto is_directory(std::string_view path) -> bool {
  return is_directory_w(from_utf8(path));
}

export auto is_regular_file(std::string_view path) -> bool {
  return is_regular_file_w(from_utf8(path));
}

export auto create_directory_w(std::wstring_view path) -> bool {
  std::wstring native(path);
  if (CreateDirectoryW(native.c_str(), nullptr)) return true;
  DWORD error = GetLastError();
  if (error != ERROR_ALREADY_EXISTS) return false;
  return is_directory_w(native);
}

export auto create_directories_w(std::wstring_view path) -> bool {
  std::error_code ec;
  // Use the extended-path form so `mkdir -p` and other recursive creators
  // work beyond MAX_PATH like the rest of the API boundary (#1061).
  const std::wstring native = to_extended_path(path);
  if (std::filesystem::create_directories(std::filesystem::path(native), ec)) {
    return true;
  }
  if (!ec) return is_directory_w(path);
  return is_directory_w(path);
}

export auto create_directory(std::string_view path) -> bool {
  return create_directory_w(from_utf8(path));
}

export auto create_directories(std::string_view path) -> bool {
  return create_directories_w(from_utf8(path));
}

export auto join_w(std::wstring_view base, std::wstring_view relative)
    -> std::wstring {
  std::filesystem::path joined = std::filesystem::path(std::wstring(base)) /
                                 std::filesystem::path(std::wstring(relative));
  return joined.make_preferred().wstring();
}

export auto join(std::string_view base, std::string_view relative)
    -> std::string {
  return to_utf8(join_w(from_utf8(base), from_utf8(relative)));
}

namespace canonicalize_detail {
// Split the relative part of a path into components, keeping "." and ".."
// entries so they can be applied against the resolved stack.
inline auto components_of(const std::filesystem::path& path)
    -> std::deque<std::filesystem::path> {
  std::deque<std::filesystem::path> parts;
  for (const auto& part : path.relative_path()) {
    parts.push_back(part);
  }
  return parts;
}

// Read the substitution text of a symlink component.  Returns nullopt for
// non-symlink reparse points (mount points carry a \\??\Volume{GUID} target
// that cannot be re-expressed as a Win32 path; leaving the component in
// place still lets the kernel resolve it).
inline auto symlink_target(const std::filesystem::path& path)
    -> std::optional<std::filesystem::path> {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  if (ec || !std::filesystem::is_symlink(status)) {
    return std::nullopt;
  }
  auto target = std::filesystem::read_symlink(path, ec);
  if (ec) {
    return std::nullopt;
  }
  std::wstring text = target.wstring();
  if (text.rfind(L"\\??\\", 0) == 0 || text.rfind(L"\\\\?\\", 0) == 0) {
    // UNC device form: "\\??\UNC\server\share" -> "\\server\share".
    if (text.size() > 8 && (text.compare(4, 4, L"UNC\\") == 0)) {
      text = L"\\\\" + text.substr(8);
    } else {
      text = text.substr(4);
    }
  }
  if (text.rfind(L"Volume{", 0) == 0) {
    return std::nullopt;  // mount point: keep the component verbatim
  }
  return std::filesystem::path(text);
}
}  // namespace canonicalize_detail

// [GNU] Resolve PATH component-wise, substituting symlink targets as each
// component is reached (realpath/readlink -f/-e/-m semantics).  Returns the
// resolved absolute path, or an errno value (ENOENT when a required
// component does not exist, ELOOP on a symlink cycle, ENOTDIR when a
// trailing separator names a non-directory).  When the resolved path fully
// exists it is re-expressed with on-disk casing via the file handle.
export auto canonicalize_path_w(std::wstring_view path, CanonMode mode,
                                bool expand_symlinks = true,
                                bool logical = false)
    -> std::expected<std::wstring, int> {
  // niubash#346: the shell hands its own applets POSIX spellings verbatim
  // (/usr/bin, /etc, /tmp, /home, /mnt/x). Resolve the operand through the
  // shell tree first, and re-express the result in the shell spelling when
  // the operand was a shell-namespace one — the shell tree's canonical name
  // is its POSIX mount name, so readlink -f/realpath agree with pwd.
  std::optional<std::wstring> shell_input;
  std::wstring effective(path);
  // #1231: fold the MSYS drive dialect before anything consumes the operand.
  // "/e/Data" has no root name in the std::filesystem sense, so the component
  // walk below used to strand it on the current directory and
  // realpath/readlink -f/stat -L resolved "<cwd-drive>:\e\Data"-style names
  // whenever the cwd sat on another drive. Same rule, same position as the
  // argv boundary (normalize_api_operand_w folds the drive form before the
  // shell-tree mapping looks at the operand); shell_tree_map_w declines
  // drive forms for the same reason. A drive-form operand is native-dialect
  // input: it resolves to the native spelling and is not re-expressed as a
  // shell-namespace path afterwards.
  // #1246: the /cygdrive/X (#1145) and /mnt/X (#1172) prefixes fold onto the
  // single-letter spelling BEFORE that rule, mirroring the argv boundary's
  // fold sequence byte-for-byte. Without the prefix fold the resolver and
  // the argv boundary disagreed on the same spelling: stat/test -f folded
  // "/cygdrive/e/f" to E:\f while realpath/readlink -f walked it verbatim
  // (or mapped it under the shell root), so "test -f said yes, realpath said
  // no" (#850 family). A prefix operand is native-dialect input just like a
  // drive-form one: it folds to the native spelling and is never
  // re-expressed as a shell-namespace path, with or without a shell root.
  std::wstring operand(strip_trailing_separators(path));
  if (auto prefix = fold_posix_drive_prefix_w(operand)) {
    operand = std::move(*prefix);
  }
  if (auto folded = fold_drive_dialect_w(operand)) {
    effective = std::move(*folded);
  } else if (auto carrier = fold_carrier_colons_w(operand)) {
    // #1283: same carrier fold as the argv boundary, so realpath and
    // readlink resolve "D:name" to the literal file the shell created
    // instead of stranding it on a drive-relative interpretation.
    effective = std::move(*carrier);
  } else if (auto mapped = shell_tree_to_native_w(operand)) {
    shell_input = *mapped;
    effective = *mapped;
  }
  std::filesystem::path input{std::move(effective)};
  const bool needs_directory =
      strip_trailing_separators(path).size() != path.size();

  std::filesystem::path current;
  std::deque<std::filesystem::path> pending;
  if (input.is_absolute()) {
    current = input.root_path();
    pending = canonicalize_detail::components_of(input);
  } else {
    current = std::filesystem::path(current_directory_w());
    pending = canonicalize_detail::components_of(input);
  }
  if (current.empty()) {
    return std::unexpected(ENOENT);
  }

  // -L/--logical folds "." and ".." lexically before symlink substitution.
  if (logical) {
    std::filesystem::path folded = (current / input.relative_path());
    folded = folded.lexically_normal();
    current = folded.root_path();
    pending = canonicalize_detail::components_of(folded);
  }

  int symlink_budget = 40;  // POSIX MAXSYMLINKS
  while (!pending.empty()) {
    const std::filesystem::path part = pending.front();
    pending.pop_front();

    const auto& native = part.native();
    if (native.empty() || native == L".") {
      continue;
    }
    if (native == L"..") {
      // Physical mode pops the *resolved* stack so "linkdir/../x" ascends
      // inside the link target's parent.
      if (current != current.root_path()) {
        current = current.parent_path();
      }
      continue;
    }

    std::filesystem::path candidate = current / part;
    const DWORD attrs = attributes_w(candidate.wstring());
    if (!valid_attributes(attrs)) {
      if (mode == CanonMode::missing) {
        current = candidate;  // keep appending; ".."-handling still works
        continue;
      }
      // [GNU] Traversing through a component that exists but is not a
      // directory reports ENOTDIR (readlink("file/x") fails ENOTDIR, not
      // ENOENT), in every mode that probes components.
      const DWORD parent_attrs = attributes_w(current.wstring());
      if (valid_attributes(parent_attrs) &&
          !attributes_are_directory(parent_attrs)) {
        return std::unexpected(ENOTDIR);
      }
      // [GNU] A component longer than NAME_MAX fails the per-component
      // lookup with ENAMETOOLONG; the CAN_ALL_BUT_LAST exemption only
      // covers ENOENT, and CAN_MISSING never probes (#370).
      constexpr size_t kMaxComponentLength = 255;  // NTFS NAME_MAX
      if (part.native().size() > kMaxComponentLength) {
        return std::unexpected(ENAMETOOLONG);
      }
      // [GNU] The CAN_ALL_BUT_LAST exemption applies when nothing but
      // separators follows the component (a trailing separator still
      // counts as the last component).  std::filesystem iteration yields
      // an empty element for a trailing separator.
      const bool only_separators_remain =
          std::ranges::all_of(pending, [](const std::filesystem::path& rem) {
            return rem.native().empty();
          });
      if (mode == CanonMode::all_but_last && only_separators_remain) {
        current = candidate;  // the last component may be missing
        continue;
      }
      return std::unexpected(ENOENT);
    }

    if (expand_symlinks) {
      if (auto target = canonicalize_detail::symlink_target(candidate)) {
        if (--symlink_budget <= 0) {
          return std::unexpected(ELOOP);
        }
        auto target_parts = canonicalize_detail::components_of(*target);
        if (target->is_absolute()) {
          current = target->root_path();
        }
        pending.insert(pending.begin(), target_parts.begin(),
                       target_parts.end());
        continue;
      }
    }
    current = candidate;
  }

  if (needs_directory) {
    // [GNU] A trailing separator makes the leaf require a directory, but a
    // *missing* leaf is still exempt under CAN_ALL_BUT_LAST (the ENOENT
    // exemption ignores trailing slashes).  Only an existing non-directory
    // reports ENOTDIR.
    const DWORD final_attrs = attributes_w(current.wstring());
    if (valid_attributes(final_attrs) &&
        !attributes_are_directory(final_attrs)) {
      return std::unexpected(ENOTDIR);
    }
  }

  // Prefer the kernel-canonical spelling (resolves 8.3 names and restores
  // on-disk casing) whenever the whole path exists.
  const std::wstring finished = current.wstring();
  std::wstring canonical = finished;
  if (mode != CanonMode::missing && valid_attributes(attributes_w(finished))) {
    const std::wstring native = to_extended_path(finished);
    HANDLE handle = CreateFileW(
        native.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
      std::wstring buffer(32768, L'\0');
      DWORD written = GetFinalPathNameByHandleW(
          handle, buffer.data(), static_cast<DWORD>(buffer.size()),
          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      CloseHandle(handle);
      if (written > 0 && written < buffer.size()) {
        buffer.resize(written);
        if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) {
          canonical = L"\\\\" + buffer.substr(8);
        } else if (buffer.rfind(L"\\\\?\\", 0) == 0) {
          canonical = buffer.substr(4);
        } else {
          canonical = buffer;
        }
      }
    }
  }
  if (shell_input) {
    if (auto display = shell_tree_display_w(canonical)) {
      canonical = std::move(*display);
    }
  }
  return canonical;
}

export auto canonicalize_path(std::string_view path, CanonMode mode,
                              bool expand_symlinks = true, bool logical = false)
    -> std::expected<std::string, int> {
  auto resolved =
      canonicalize_path_w(from_utf8(path), mode, expand_symlinks, logical);
  if (!resolved) {
    return std::unexpected(resolved.error());
  }
  return to_utf8(*resolved);
}

}  // namespace native_path
