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
  std::string filename = source;
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

// [GNU] --backup CONTROL parsing follows argmatch: an exact name wins,
// otherwise a unique prefix of one of the eight names; no match is
// "invalid" and several matches are "ambiguous". Matching is done on the
// lowercased value; a bare --backup (empty value) means existing.
auto parse_ln_backup_control(const std::string &raw) -> LnBackupParse {
  std::string value = raw;
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
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
  bool force = ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  bool interactive =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--interactive", false);
  // [GNU] -b is --backup with no argument (CONTROL defaults to
  // existing); --backup takes an optional CONTROL parsed by argmatch
  // rules. When both are given, the --backup value wins.
  bool backup_flag = ctx.get<bool>("-b", false);
  bool backup_given = backup_flag || ctx.has("--backup");
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
  std::string backup_suffix = ctx.get<std::string>("--suffix", "");
  if (backup_suffix.empty()) backup_suffix = ctx.get<std::string>("-S", "");
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
  // [DIFFERS] -L/--logical/--dereference: on Windows, CreateHardLinkW
  // already dereferences symlinks transparently, so this flag is
  // effectively a no-op for hard links.
  bool logical = ctx.get<bool>("-L", false) ||
                 ctx.get<bool>("--logical", false) ||
                 ctx.get<bool>("--dereference", false);
  // [DIFFERS] -P/--physical: on Windows, hard links are always created
  // directly to the target without following symlinks (the default).
  bool physical =
      ctx.get<bool>("-P", false) || ctx.get<bool>("--physical", false);
  // [DIFFERS] -d/--directory/-F: hard links to directories are not
  // supported on Windows.
  bool directory_link = ctx.get<bool>("-d", false) ||
                        ctx.get<bool>("--directory", false) ||
                        ctx.get<bool>("-F", false);
  // [DIFFERS] -r/--relative: on Windows, relative symlink targets are
  // computed using std::filesystem::relative, which may differ from POSIX
  // path resolution.
  bool relative =
      ctx.get<bool>("-r", false) || ctx.get<bool>("--relative", false);

  // [DIFFERS] -d/--directory/-F: not supported on Windows.
  if (directory_link) {
    safeErrorPrintLn(winux::i18n::translate(
        "command.ln.error.directory-hardlink-unsupported",
        "ln: hard links to directories are not supported on Windows"));
    return 1;
  }
  if (logical && physical) {
    safeErrorPrintLn(
        "ln: options --logical and --physical are mutually exclusive");
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
    if (ctx.positionals.empty()) {
      safeErrorPrint("ln: missing file operand\n");
      safeErrorPrint("Try 'ln --help' for more information.\n");
      return 1;
    }
    // GNU 9.11 ln.c: missing -t dir reports "failed to access"; an
    // existing non-directory reports "target 'x' is not a directory".
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
    if (td_attrs == INVALID_FILE_ATTRIBUTES ||
        !(td_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
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

  size_t target_count = link_jobs.size();
  size_t success_count = 0;
  size_t error_count = 0;

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
    }

    // Check if target exists (lstat semantics: a dangling symlink
    // destination exists, #1367)
    std::wstring wtarget = utf8_to_wstring(target_api);
    DWORD target_attrs = 0;
    bool target_exists = lstat_attrs(wtarget, target_attrs);

    if (target_exists) {
      if (interactive && !force) {
        // [GNU] The prompt goes to stderr, like the cp/mv overwrite
        // prompts; declining is an error (GNU exits 1) (#1367).
        safeErrorPrint("ln: replace '" + target + "'? ");
        std::string response;
        std::getline(std::cin, response);
        if (response.empty() || (response[0] != 'y' && response[0] != 'Y')) {
          error_count++;
          continue;
        }
      }

      // Backup if requested
      if (backup && target_exists) {
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
          safeErrorPrint("ln: cannot backup '" + target + "'\n");
          error_count++;
          continue;
        }
        if (verbose) {
          // [GNU] "'<backup>' ~ '<dest>' -> '<src>'" for symlinks, "=>"
          // for hard links.
          safePrint("'" + backup_name + "' ~ '" + target + "' " +
                    (symbolic ? "->" : "=>") + " '" + source + "'\n");
        }
      } else if (force) {
        // Remove existing target if force is set
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
    auto result = symbolic
                      ? create_symlink(source_api, target_api, verbose, source,
                                       target)
                      : create_hardlink(source_api, target_api, verbose,
                                        source, target);

    if (!result) {
      safeErrorPrint("ln: ");
      safeErrorPrint(result.error());
      safeErrorPrint("\n");
      error_count++;
    } else {
      success_count++;
    }
  }

  return (error_count > 0) ? 1 : 0;
}
