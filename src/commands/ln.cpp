// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for ln.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief LN command options definition
 *
 * This array defines all the options supported by the ln command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -s, @a --symbolic: Make symbolic links instead of hard links
 * [IMPLEMENTED]
 * - @a -f, @a --force: Remove existing destination files [IMPLEMENTED]
 * - @a -v, @a --verbose: Print name of each linked file [IMPLEMENTED]
 * - @a -n, @a --no-dereference: Treat LINK_NAME as a normal file if it is a
 * symbolic link to a directory [NOT SUPPORT]
 */
auto constexpr LN_OPTIONS = std::array{
    // [GNU]
    OPTION("-s", "--symbolic", "make symbolic links instead of hard links"),
    // [GNU]
    OPTION("-f", "--force", "remove existing destination files"),
    // [GNU]
    OPTION("-v", "--verbose", "print name of each linked file"),
    // [DIFFERS] -n: on Windows, directory-symlink target detection uses
    // GetFileAttributesW; behaviour differs from GNU.
    OPTION("-n", "--no-dereference",
           "treat LINK_NAME as a normal file if it is a symbolic link to a "
           "directory"),
    // [GNU]
    OPTION("-i", "--interactive", "prompt whether to remove destinations"),
    // [DIFFERS] -L/--logical: on Windows, CreateHardLinkW and the default
    // file-open behaviour already dereference symlinks, making this flag
    // effectively a no-op for hard links.
    OPTION("-L", "--logical", "dereference TARGETs that are symbolic links"),
    // [DIFFERS] -P/--physical: on Windows, hard links are always created
    // directly to the target (symlinks are not followed); this is the
    // default behaviour.
    OPTION("-P", "--physical", "make hard links directly to symbolic links"),
    // [DIFFERS] --dereference: alias for -L; same Windows caveats apply.
    OPTION("", "--dereference", "dereference TARGETs that are symbolic links"),
    // [GNU]
    OPTION("-b", "", "like --backup but does not accept an argument"),
    OPTION("", "--backup", "make a backup of each existing destination file",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
    // [DIFFERS] -r/--relative: on Windows, relative symlink targets are
    // computed using std::filesystem::relative which may differ from POSIX
    // path resolution.
    OPTION("-r", "--relative",
           "with -s, create links relative to link location"),
    // [GNU]
    OPTION("-t", "--target-directory",
           "specify the DIRECTORY in which to create the links", STRING_TYPE),
    // [GNU]
    OPTION("-T", "--no-target-directory",
           "treat LINK_NAME as a normal file always"),
    // [DIFFERS] -d/--directory: hard links to directories are not supported
    // on Windows; an error is reported at runtime.
    OPTION("-d", "--directory",
           "allow the hard link to be a directory (privileged)"),
    // [DIFFERS] -F: alias for -d; same Windows limitation applies.
    OPTION("-F", "", "allow hard link to directory (alias for -d)")};

namespace ln_pipeline {
namespace cp = core::pipeline;

auto join_target_path(const std::string &directory, const std::string &source)
    -> std::string {
  // [GNU] last_name strips trailing slashes before taking the basename
  // (ln.c:666-673): "sub/" links as "sub", not as the empty string.
  std::string filename = source;
  while (filename.size() > 1 &&
         (filename.back() == '/' || filename.back() == '\\')) {
    filename.pop_back();
  }
  size_t sep = filename.find_last_of("/\\");
  if (sep != std::string::npos) {
    filename = filename.substr(sep + 1);
  }
  return directory + "\\" + filename;
}

// [GNU] -r/--relative: symbolic-link targets are relative to the link.
// SOURCE and TARGET arrive already folded through normalize_api_operand
// (#1234): std::filesystem::absolute would root a raw "/e/x" spelling on the
// CURRENT drive and compute the relative form against the wrong base.
auto relative_symlink_target(const std::string &source,
                             const std::string &target) -> std::string {
  std::error_code ec;
  // Build paths from the wide form: the narrow path ctor decodes via the
  // system ACP, which mangles non-ASCII UTF-8 sources/targets (#88).
  auto source_path = std::filesystem::absolute(utf8_to_wstring(source), ec);
  if (ec) return source;
  auto target_path = std::filesystem::absolute(utf8_to_wstring(target), ec);
  if (ec) return source;
  auto relative =
      std::filesystem::relative(source_path, target_path.parent_path(), ec);
  return ec ? source : wstring_to_utf8(relative.wstring());
}

auto ln_windows_error_text(DWORD error) -> std::string {
  Win32ErrorTextOptions options;
  options.file_exists = true;
  options.privilege_not_held_as_not_permitted = true;
  return win32_posix_error_text(error, options);
}

auto ln_creation_failure_prefix(bool symbolic, const std::string &target)
    -> std::string {
  return symbolic ? "failed to create symbolic link '" + target + "'"
                  : "failed to create hard link '" + target + "'";
}

enum class LnBackupControl { none, simple, numbered, existing };

struct LnBackupParse {
  LnBackupControl control = LnBackupControl::none;
  int error_kind = 0;  // 0 = ok, 1 = invalid, 2 = ambiguous
};

// [GNU] --backup CONTROL parsing follows argmatch (xget_version): an
// exact name wins, otherwise a unique prefix of one of the eight names;
// no match is "invalid" and several matches are "ambiguous". Matching is
// case-sensitive; a bare --backup (empty value) means existing.
auto parse_ln_backup_control(const std::string &raw) -> LnBackupParse {
  const std::string &value = raw;
  static constexpr std::pair<std::string_view, LnBackupControl> kNames[] = {
      {"none", LnBackupControl::none},
      {"off", LnBackupControl::none},
      {"simple", LnBackupControl::simple},
      {"never", LnBackupControl::simple},
      {"existing", LnBackupControl::existing},
      {"nil", LnBackupControl::existing},
      {"numbered", LnBackupControl::numbered},
      {"t", LnBackupControl::numbered},
  };
  for (auto [name, control] : kNames) {
    if (value == name) return {control, 0};
  }
  if (value.empty()) return {LnBackupControl::existing, 0};
  int matches = 0;
  LnBackupControl matched = LnBackupControl::none;
  for (auto [name, control] : kNames) {
    if (name.starts_with(value)) {
      ++matches;
      matched = control;
    }
  }
  if (matches == 1) return {matched, 0};
  return {LnBackupControl::none, matches == 0 ? 1 : 2};
}

// [GNU] argmatch diagnostic for a bad backup type; the caller adds the
// "ln: " prefix and the trailing newline.
auto ln_backup_type_message(int error_kind, const std::string &value)
    -> std::string {
  std::string message =
      error_kind == 2 ? "ambiguous argument '" : "invalid argument '";
  message += value;
  message +=
      "' for 'backup type'\n"
      "Valid arguments are:\n"
      "  - 'none', 'off'\n"
      "  - 'simple', 'never'\n"
      "  - 'existing', 'nil'\n"
      "  - 'numbered', 't'\n"
      "Try 'ln --help' for more information.";
  return message;
}

// [GNU] Highest N among existing "<target>.~N~" siblings, or 0 when none.
// The directory scan uses the folded API form, like the rest of the
// backup flow; any scan failure is treated as "no numbered backups".
auto ln_max_numbered_backup(const std::string &target_api) -> int {
  std::filesystem::path target_path(utf8_to_wstring(target_api));
  std::filesystem::path dir = target_path.parent_path();
  if (dir.empty()) dir = ".";
  const std::string base = wstring_to_utf8(target_path.filename().wstring());
  const std::string prefix = base + ".~";
  int max_version = 0;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    const std::string name =
        wstring_to_utf8(entry.path().filename().wstring());
    if (!name.starts_with(prefix) || name.size() <= prefix.size() + 1 ||
        name.back() != '~') {
      continue;
    }
    const std::string digits =
        name.substr(prefix.size(), name.size() - prefix.size() - 1);
    if (!std::ranges::all_of(digits, [](unsigned char ch) {
          return std::isdigit(ch) != 0;
        })) {
      continue;
    }
    int version = 0;
    for (char ch : digits) version = version * 10 + (ch - '0');
    if (version > max_version) max_version = version;
  }
  return max_version;
}

// [GNU] Numbered backups take max existing N + 1 (with .~1~ and .~3~
// present the next backup is .~4~, not the first free slot).
auto ln_numbered_backup_name(const std::string &target_api) -> std::string {
  return target_api + ".~" +
         std::to_string(ln_max_numbered_backup(target_api) + 1) + "~";
}

/**
 * @brief Create a hard link
 * @param source Source file path (folded API form, see the #1234 call site)
 * @param target Target link path (folded API form, see the #1234 call site)
 * @param verbose Print verbose output
 * @param source_display Source as the user typed it, for messages
 * @param target_display Target as the user typed it, for messages
 * @return Result with success status
 */
auto create_hardlink(const std::string &source, const std::string &target,
                     bool verbose, const std::string &source_display,
                     const std::string &target_display) -> cp::Result<void> {
  std::wstring wsource = utf8_to_wstring(source);
  std::wstring wtarget = utf8_to_wstring(target);

  if (CreateHardLinkW(wtarget.c_str(), wsource.c_str(), nullptr)) {
    if (verbose) {
      safePrint("'");
      safePrint(target_display);
      safePrint("' => '");
      safePrint(source_display);
      safePrint("'\n");
    }
    return {};
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to create hard link '" + target_display +
                         "': " + ln_windows_error_text(error));
}

/**
 * @brief Create a symbolic link
 * @param source Source file/directory path (folded API form, #1234)
 * @param target Target link path (folded API form, #1234)
 * @param verbose Print verbose output
 * @param source_display Source as the user typed it, for messages
 * @param target_display Target as the user typed it, for messages
 * @return Result with success status
 */
auto create_symlink(const std::string &source, const std::string &target,
                    bool verbose, const std::string &source_display,
                    const std::string &target_display) -> cp::Result<void> {
  // [DIFFERS] #1101: reparse-point targets must use backslash separators;
  // "./bds" or "dir/file" resolve as ERROR_INVALID_NAME on native Windows.
  // Store the user-visible link text unchanged, but normalize what goes
  // into the reparse point.
  std::wstring wsource =
      win32_normalize_symlink_target(utf8_to_wstring(source));
  std::wstring wtarget = utf8_to_wstring(target);

  // Check if source is a directory
  DWORD attrs = GetFileAttributesW(wsource.c_str());
  bool is_directory = attrs != INVALID_FILE_ATTRIBUTES &&
                      (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
  DWORD flags = is_directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  // [GNU] a symlink source may be dangling; creation must not fail when the
  // source does not exist (uutils #13667)

  if (CreateSymbolicLinkW(wtarget.c_str(), wsource.c_str(), flags)) {
    if (verbose) {
      safePrint("'");
      safePrint(target_display);
      safePrint("' -> '");
      safePrint(source_display);
      safePrint("'\n");
    }
    return {};
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to create symbolic link '" + target_display +
                         "': " + ln_windows_error_text(error));
}

// [GNU] #1367: existence with lstat semantics.  GetFileAttributesW follows
// symlinks, so a dangling destination link looks missing and the "File
// exists" error, the -i prompt, the -b backup, and -f removal all misfire.
// FindFirstFileW reports the link itself (the same probe the hard-link
// source check in the command body uses).
auto lstat_attrs(const std::wstring &wpath, DWORD &attrs) -> bool {
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(wpath.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  FindClose(h);
  attrs = fd.dwFileAttributes;
  return true;
}

// [GNU] File identity for the same-file and just-created guards
// (ln.c:234-300): volume serial + file index of the directory entry
// itself (the handle is opened on the reparse point, not its target),
// plus the link count for the SAME_INODE refinement.
struct LnFileIdentity {
  uint64_t volume = 0;
  uint64_t index = 0;
  uint32_t links = 0;
};

auto ln_file_identity(const std::string &api_path)
    -> std::optional<LnFileIdentity> {
  HANDLE handle = CreateFileW(
      utf8_to_wstring(api_path).c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
  BY_HANDLE_FILE_INFORMATION info{};
  std::optional<LnFileIdentity> result;
  if (GetFileInformationByHandle(handle, &info)) {
    result = LnFileIdentity{
        info.dwVolumeSerialNumber,
        (static_cast<uint64_t>(info.nFileIndexHigh) << 32) |
            info.nFileIndexLow,
        info.nNumberOfLinks};
  }
  CloseHandle(handle);
  return result;
}

// [GNU] SAME_INODE plus the ln.c refinement: two names for one inode
// are "the same file" for removal purposes only when the file has a
// single link, or when both names spell the same directory entry.
auto ln_same_file(const std::string &source_api, const std::string &dest_api)
    -> bool {
  auto source_id = ln_file_identity(source_api);
  auto dest_id = ln_file_identity(dest_api);
  if (!source_id || !dest_id) return false;
  if (source_id->volume != dest_id->volume ||
      source_id->index != dest_id->index) {
    return false;
  }
  if (source_id->links <= 1) return true;
  std::error_code ec;
  auto source_abs = std::filesystem::absolute(utf8_to_wstring(source_api), ec);
  if (ec) return false;
  auto dest_abs = std::filesystem::absolute(utf8_to_wstring(dest_api), ec);
  if (ec) return false;
  return _wcsicmp(source_abs.c_str(), dest_abs.c_str()) == 0;
}

/**
 * @brief Remove existing file/directory
 * @param path Path to remove (folded API form, see the #1234 call site)
 * @param path_display Path as the user typed it, for messages
 * @return Result with success status
 */
auto remove_existing(const std::string &path,
                     const std::string &path_display) -> cp::Result<void> {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attrs = 0;
  if (!lstat_attrs(wpath, attrs)) {
    return {};  // File doesn't exist, nothing to remove
  }

  bool is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
  bool is_reparse_point = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  // Try to remove as reparse point (symlink)
  if (is_reparse_point) {
    if (RemoveDirectoryW(wpath.c_str()) || DeleteFileW(wpath.c_str())) {
      return {};
    }
  }

  // Try to remove as directory or file
  if (is_directory) {
    if (RemoveDirectoryW(wpath.c_str())) {
      return {};
    }
  } else {
    if (DeleteFileW(wpath.c_str())) {
      return {};
    }
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to remove '" + path_display +
                         "': " + ln_windows_error_text(error));
}

}  // namespace ln_pipeline

REGISTER_COMMAND(
    ln, "ln", "make links between files",
    "Create links between files. By default, make hard links.\n"
    "\n"
    "  -s, --symbolic         make symbolic links instead of hard links\n"
    "  -f, --force            remove existing destination files\n"
    "  -i, --interactive      prompt whether to remove destinations\n"
    "  -b, --backup           make a backup of each existing destination\n"
    "  -S, --suffix=SUFFIX    override the usual backup suffix\n"
    "  -t, --target-directory=DIRECTORY  specify the target directory\n"
    "  -T, --no-target-directory  treat LINK_NAME as a normal file\n"
    "  -r, --relative         create links relative to link location\n"
    "\n"
    "On Windows, hard links and symbolic links are supported.\n"
    "Note: Creating symbolic links may require administrator privileges.",
    "  ln source link         Create a hard link\n"
    "  ln -s source link      Create a symbolic link\n"
    "  ln -sf source link     Force create, overwrite if exists\n"
    "  ln -sv source link     Verbose symbolic link creation\n"
    "  ln -t dir/ src1 src2   Create links in dir/\n"
    "  ln -b source link      Backup existing link before creating",
    "link(1), symlink(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    LN_OPTIONS) {
  using namespace ln_pipeline;

  bool symbolic =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--symbolic", false);
  // [GNU] -f and -i are last-one-wins in argv order: -f clears a
  // previous -i and vice versa (ln.c:506-513), so resolve them from
  // the occurrence stream rather than as independent booleans.
  bool force = false;
  bool interactive = false;
  // [GNU] -L/-P edit one logical flag in argv order (ln.c:514-522).
  bool logical = false;
  if (ctx.metas) {
    for (const auto &occurrence : ctx.options.occurrences()) {
      if (occurrence.index >= LN_OPTIONS.size()) continue;
      const auto &meta = LN_OPTIONS[occurrence.index];
      if (meta.short_name == "-f" || meta.long_name == "--force") {
        force = true;
        interactive = false;
      } else if (meta.short_name == "-i" ||
                 meta.long_name == "--interactive") {
        interactive = true;
        force = false;
      } else if (meta.short_name == "-L" || meta.long_name == "--logical" ||
                 meta.long_name == "--dereference") {
        logical = true;
      } else if (meta.short_name == "-P" || meta.long_name == "--physical") {
        logical = false;
      }
    }
  }
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  std::string backup_suffix = ctx.get<std::string>("--suffix", "");
  if (backup_suffix.empty()) backup_suffix = ctx.get<std::string>("-S", "");
  // [GNU] -b is --backup with no argument (CONTROL defaults to
  // existing); --backup takes an optional CONTROL parsed by argmatch
  // rules. When both are given, the --backup value wins. -S alone also
  // enables backups (ln.c:550-553).
  bool backup_flag = ctx.get<bool>("-b", false);
  bool backup_given = backup_flag || ctx.has("--backup") ||
                      ctx.has("-S") || ctx.has("--suffix");
  LnBackupControl backup_control = LnBackupControl::none;
  if (backup_given) {
    if (ctx.has("--backup")) {
      std::string raw_control = ctx.get<std::string>("--backup", "");
      auto parsed = parse_ln_backup_control(raw_control);
      if (parsed.error_kind != 0) {
        safeErrorPrint("ln: " +
                       ln_backup_type_message(parsed.error_kind, raw_control) +
                       "\n");
        return 1;
      }
      backup_control = parsed.control;
    } else {
      backup_control = LnBackupControl::existing;
    }
  }
  bool backup = backup_control != LnBackupControl::none;
  if (backup && backup_suffix.empty()) backup_suffix = "~";
  std::string target_dir = ctx.get<std::string>("--target-directory", "");
  if (target_dir.empty()) target_dir = ctx.get<std::string>("-t", "");
  bool no_target_dir = ctx.get<bool>("-T", false) ||
                       ctx.get<bool>("--no-target-directory", false);
  // [DIFFERS] -n/--no-dereference: on Windows, directory-symlink target
  // detection uses GetFileAttributesW, which may differ from POSIX
  // lstat() behaviour.
  bool no_dereference =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--no-dereference", false);
  // [DIFFERS] -d/--directory/-F: the flag is recorded, but Windows
  // cannot hard-link a directory; without it a directory source gets
  // the GNU diagnostic at creation time, with it the CreateHardLinkW
  // failure text surfaces (see the per-job source check).
  bool directory_link = ctx.get<bool>("-d", false) ||
                        ctx.get<bool>("--directory", false) ||
                        ctx.get<bool>("-F", false);
  // [DIFFERS] -r/--relative: on Windows, relative symlink targets are
  // computed using std::filesystem::relative, which may differ from POSIX
  // path resolution.
  bool relative =
      ctx.get<bool>("-r", false) || ctx.get<bool>("--relative", false);

  // [GNU] A repeated -t is fatal during option processing
  // (ln.c:529-531).
  if (ctx.count({"-t", "--target-directory"}) > 1) {
    safeErrorPrint("ln: multiple target directories specified\n");
    safeErrorPrint("Try 'ln --help' for more information.\n");
    return 1;
  }

  if (!target_dir.empty() && no_target_dir) {
    // GNU 9.11 ln.c: "cannot combine --target-directory and
    // --no-target-directory"
    safeErrorPrint(
        "ln: cannot combine --target-directory "
        "and --no-target-directory\n");
    return 1;
  }

  std::vector<std::pair<std::string, std::string>> link_jobs;

  if (!target_dir.empty()) {
    // GNU 9.11 ln.c: the -t directory is validated during option
    // processing, before the missing-operand check (ln.c:529-543):
    // missing -t dir reports "failed to access"; an existing
    // non-directory reports "target 'x' is not a directory".
    // The attribute probe folds the drive dialect first (#1234): a raw
    // "/e/Data" spelling resolves against the current drive and the check
    // would answer for "<cwd>:\e\Data".
    const auto td_operand = native_path::make_api_path_operand(target_dir);
    DWORD td_attrs = GetFileAttributesW(td_operand.extended.c_str());
    if (td_attrs == INVALID_FILE_ATTRIBUTES) {
      safeErrorPrint("ln: failed to access '" + target_dir +
                     "': No such file or directory\n");
      return 1;
    }
    if (!(td_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      safeErrorPrint("ln: target '" + target_dir + "' is not a directory\n");
      return 1;
    }
    if (ctx.positionals.empty()) {
      safeErrorPrint("ln: missing file operand\n");
      safeErrorPrint("Try 'ln --help' for more information.\n");
      return 1;
    }
    for (auto arg : ctx.positionals) {
      std::string source(arg);
      link_jobs.emplace_back(source, join_target_path(target_dir, source));
    }
  } else if (ctx.positionals.empty()) {
    safeErrorPrint("ln: missing file operand\n");
    safeErrorPrint("Try 'ln --help' for more information.\n");
    return 1;
  } else if (no_target_dir && ctx.positionals.size() == 1) {
    // GNU: -T requires exactly two operands.
    safeErrorPrint("ln: missing destination file operand after '" +
                   std::string(ctx.positionals[0]) + "'\n");
    safeErrorPrint("Try 'ln --help' for more information.\n");
    return 1;
  } else if (ctx.positionals.size() == 1) {
    std::string source(std::string(ctx.positionals[0]));
    link_jobs.emplace_back(source, join_target_path(".", source));
  } else if (no_target_dir) {
    if (ctx.positionals.size() > 2) {
      safeErrorPrint("ln: extra operand '" + std::string(ctx.positionals[2]) +
                     "'\n");
      safeErrorPrint("Try 'ln --help' for more information.\n");
      return 1;
    }
    link_jobs.emplace_back(std::string(ctx.positionals[0]),
                           std::string(ctx.positionals[1]));
  } else if (ctx.positionals.size() == 2) {
    std::string source(std::string(ctx.positionals[0]));
    std::string target(std::string(ctx.positionals[1]));
    // Directory detection folds the drive dialect first (#1234): a raw
    // "/e/Data" operand must be answered for E:\Data, not "<cwd>:\e\Data".
    const auto target_operand = native_path::make_api_path_operand(target);
    DWORD target_attrs =
        GetFileAttributesW(target_operand.extended.c_str());
    std::error_code target_ec;
    const bool target_is_symlink = std::filesystem::is_symlink(
        std::filesystem::symlink_status(target_operand.normalized, target_ec));
    if (target_attrs != INVALID_FILE_ATTRIBUTES &&
        (target_attrs & FILE_ATTRIBUTE_DIRECTORY) &&
        !(no_dereference && target_is_symlink)) {
      link_jobs.emplace_back(source, join_target_path(target, source));
    } else {
      link_jobs.emplace_back(source, target);
    }
  } else {
    target_dir = std::string(ctx.positionals.back());
    // Same dialect fold as the -t branch (#1234).
    const auto td_operand = native_path::make_api_path_operand(target_dir);
    DWORD td_attrs = GetFileAttributesW(td_operand.extended.c_str());
    // [GNU] -n applies to the multi-operand form too (ln.c:605-620): a
    // symlink-to-directory last operand is treated as a normal file,
    // so it is not a valid target directory.
    std::error_code td_ec;
    const bool td_is_symlink = std::filesystem::is_symlink(
        std::filesystem::symlink_status(td_operand.normalized, td_ec));
    if (td_attrs == INVALID_FILE_ATTRIBUTES ||
        !(td_attrs & FILE_ATTRIBUTE_DIRECTORY) ||
        (no_dereference && td_is_symlink)) {
      // GNU 9.11 ln.c: errno-style "target 'x': <reason>".
      safeErrorPrint("ln: target '" + target_dir +
                     (td_attrs == INVALID_FILE_ATTRIBUTES
                          ? "': No such file or directory\n"
                          : "': Not a directory\n"));
      return 1;
    }

    for (size_t i = 0; i + 1 < ctx.positionals.size(); ++i) {
      std::string source(ctx.positionals[i]);
      link_jobs.emplace_back(source, join_target_path(target_dir, source));
    }
  }

  // [GNU] -r requires -s (ln.c:571-572); checked after the operand
  // rules above, before any link is created.
  if (relative && !symbolic) {
    safeErrorPrint("ln: cannot do --relative without --symbolic\n");
    return 1;
  }

  size_t target_count = link_jobs.size();
  size_t success_count = 0;
  size_t error_count = 0;
  // [GNU] Identities of destinations created by this run, for the
  // just-created guard (ln.c:234-242).
  std::set<std::pair<uint64_t, uint64_t>> created_dests;

  for (const auto &[job_source, target] : link_jobs) {
    std::string source = job_source;
    // #1234: fold the drive dialect once per job, before any Win32 call.
    // Raw "/e/..." spellings handed to CreateHardLinkW / CreateSymbolicLinkW
    // / GetFileAttributesW resolve a leading "/e" against the CURRENT drive,
    // so from a C: cwd `ln -s /e/Data /e/Data/link` really wrote
    // "C:\e\Data\link" and the hard-link source probe looked for
    // "C:\e\Data\probe". Messages below keep the operand text verbatim.
    std::string source_api = native_path::normalize_api_operand(source);
    const std::string target_api = native_path::normalize_api_operand(target);
    if (!symbolic && logical) {
      std::error_code source_ec;
      auto resolved =
          std::filesystem::canonical(utf8_to_wstring(source_api), source_ec);
      if (!source_ec) {
        source_api = wstring_to_utf8(resolved.wstring());
        source = source_api;
      }
    }
    if (symbolic && relative) {
      source = relative_symlink_target(source_api, target_api);
      source_api = native_path::normalize_api_operand(source);
    }

    // [GNU] For hard links, ln stats the source first so a missing TARGET
    // reports "failed to access 'src'".  FindFirstFileW is the lstat
    // equivalent here: a dangling symlink still counts as existing.
    if (!symbolic) {
      WIN32_FIND_DATAW sfd{};
      HANDLE sh = FindFirstFileW(utf8_to_wstring(source_api).c_str(), &sfd);
      if (sh == INVALID_HANDLE_VALUE) {
        safeErrorPrint("ln: failed to access '" + source +
                       "': No such file or directory\n");
        error_count++;
        continue;
      }
      FindClose(sh);
      // [GNU] A directory source is refused with its own diagnostic
      // unless -d/-F was given (ln.c:202-208). With -d the request
      // falls through and the platform refusal surfaces from
      // CreateHardLinkW, since Windows cannot hard-link a directory.
      if ((sfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          !(sfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
          !directory_link) {
        safeErrorPrint("ln: '" + source +
                       "': hard link not allowed for directory\n");
        error_count++;
        continue;
      }
    }

    // Check if target exists (lstat semantics: a dangling symlink
    // destination exists, #1367)
    std::wstring wtarget = utf8_to_wstring(target_api);
    DWORD target_attrs = 0;
    bool target_exists = lstat_attrs(wtarget, target_attrs);

    bool made_backup = false;
    if (target_exists) {
      // [GNU] A directory destination is refused before force, backup
      // or prompting (ln.c:228-232). A reparse point is not a directory
      // for this check (lstat semantics).
      if ((target_attrs & FILE_ATTRIBUTE_DIRECTORY) &&
          !(target_attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        safeErrorPrint("ln: '" + target + "': cannot overwrite directory\n");
        error_count++;
        continue;
      }
      if (backup || force || interactive) {
        // [GNU] Never remove a destination this run just created
        // (ln.c:234-242).
        if (auto dest_id = ln_file_identity(target_api)) {
          if (created_dests.contains(
                  {dest_id->volume, dest_id->index})) {
            safeErrorPrint("ln: will not overwrite just-created '" + target +
                           "' with '" + source + "'\n");
            error_count++;
            continue;
          }
        }
        // [GNU] Never let -f/backup removal delete the source itself
        // (ln.c:260-300): the guard applies to backup of hard links
        // and to -f removal.
        if (((backup && !symbolic) || force) &&
            ln_same_file(source_api, target_api)) {
          safeErrorPrint("ln: '" + source + "' and '" + target +
                         "' are the same file\n");
          error_count++;
          continue;
        }
      }
      // An affirmative -i answer approves removing the destination,
      // like GNU: the removal below must honour it, or the run falls
      // into the no-force error branch and the link is never replaced
      // (#1367 interactive case).
      bool approved_removal = false;
      if (interactive) {
        // [GNU] The prompt goes to stderr, like the cp/mv overwrite
        // prompts; declining is an error (GNU exits 1) (#1367).
        safeErrorPrint("ln: replace '" + target + "'? ");
        std::string response;
        std::getline(std::cin, response);
        if (response.empty() || (response[0] != 'y' && response[0] != 'Y')) {
          error_count++;
          continue;
        }
        approved_removal = true;
      }

      // Backup if requested
      if (backup) {
        std::string backup_name;
        if (backup_control == LnBackupControl::numbered) {
          backup_name = ln_numbered_backup_name(target_api);
        } else if (backup_control == LnBackupControl::existing &&
                   ln_max_numbered_backup(target_api) > 0) {
          backup_name = ln_numbered_backup_name(target_api);
        } else {
          backup_name = target_api + backup_suffix;
        }
        std::wstring wbackup = utf8_to_wstring(backup_name);
        // Remove existing backup if any
        DeleteFileW(wbackup.c_str());
        RemoveDirectoryW(wbackup.c_str());
        if (!MoveFileW(wtarget.c_str(), wbackup.c_str())) {
          // [GNU] The failure carries its reason (ln.c:296-303).
          safeErrorPrint("ln: cannot backup '" + target +
                         "': " + ln_windows_error_text(GetLastError()) +
                         "\n");
          error_count++;
          continue;
        }
        made_backup = true;
        if (verbose) {
          // [GNU] "'<backup>' ~ '<dest>' -> '<src>'" for symlinks, "=>"
          // for hard links.
          safePrint("'" + backup_name + "' ~ '" + target + "' " +
                    (symbolic ? "->" : "=>") + " '" + source + "'\n");
        }
      } else if (force || approved_removal) {
        // Remove existing target if force is set, or -i approved it
        auto result = remove_existing(target_api, target);
        if (!result) {
          safeErrorPrint("ln: ");
          safeErrorPrint(result.error());
          safeErrorPrint("\n");
          error_count++;
          continue;
        }
      } else if (!backup) {
        // No force, no backup, no interactive - error.  [GNU] ln never
        // overwrites an existing destination without -f, including -T
        // (ln.c reports "File exists" for every existing target).
        (void)no_target_dir;
        (void)target_attrs;
        safeErrorPrint("ln: " + ln_creation_failure_prefix(symbolic, target) +
                       ": File exists\n");
        error_count++;
        continue;
      }
    }

    // Create the link.  The folded forms drive the Win32 calls; the raw
    // operand text rides along for messages, like MSYS prints them (#1234).
    // [GNU] When a backup was made, the combined backup line printed
    // above is the only verbose line (ln.c:345-367).
    auto result = symbolic
                      ? create_symlink(source_api, target_api,
                                       verbose && !made_backup, source, target)
                      : create_hardlink(source_api, target_api,
                                        verbose && !made_backup, source,
                                        target);

    if (!result) {
      safeErrorPrint("ln: ");
      safeErrorPrint(result.error());
      safeErrorPrint("\n");
      error_count++;
    } else {
      success_count++;
      if (auto dest_id = ln_file_identity(target_api)) {
        created_dests.insert({dest_id->volume, dest_id->index});
      }
    }
  }

  return (error_count > 0) ? 1 : 0;
}
