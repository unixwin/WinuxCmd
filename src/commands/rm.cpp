// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
///   - @description:
/// @Description: Implemention for rm.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
#pragma comment(lib, "shlwapi.lib")
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

using namespace core::pipeline;

/**
 * @brief RM command options definition
 *
 * This array defines all the options supported by the rm command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -f, @a --force: Ignore nonexistent files and arguments, never prompt
 * [IMPLEMENTED]
 * - @a -i: Prompt before every removal [IMPLEMENTED]
 * - @a -I: Prompt once before removing more than three files, or when removing
 * recursively [IMPLEMENTED]
 * - @a -d, @a --dir: Remove empty directories [IMPLEMENTED]
 * - @a -r, @a --recursive: Remove directories and their contents recursively
 * [IMPLEMENTED]
 * - @a -R, @a --recursive: Remove directories and their contents recursively
 * [IMPLEMENTED]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a --interactive: Prompt according to WHEN: never, once (-I), or always
 * (-i) [IMPLEMENTED]
 * - @a --one-file-system: When removing a hierarchy recursively, skip any
 *
 * directory that is on a file system different from that of the corresponding

 * * command line argument [APPROXIMATE ON WINDOWS]
 * - @a --no-preserve-root:
 * Do not treat '/' specially [IMPLEMENTED]
 * - @a --preserve-root: Do not
 * remove '/' (default) [IMPLEMENTED]
 */
// clang-format off
constexpr auto RM_OPTIONS = std::array{
// [GNU]
    OPTION("-f", "--force", "ignore nonexistent files and arguments, never prompt"),
// [GNU]
    OPTION("-i", "", "prompt before every removal"),
// [GNU]
    OPTION("-I", "", "prompt once before removing more than three files, or when removing recursively"),
// [GNU]
    OPTION("-d", "--dir", "remove empty directories"),
// [GNU]
    OPTION("-r", "--recursive", "remove directories and their contents recursively"),
// [GNU]
    OPTION("-R", "--recursive", "remove directories and their contents recursively"),
// [GNU]
    OPTION("-v", "--verbose", "explain what is being done"),
// [GNU]
    OPTION("", "--interactive", "prompt according to WHEN: never, once (-I), or always (-i)", OPTIONAL_STRING_TYPE),
// [GNU]
    OPTION("", "--one-file-system", "when removing a hierarchy recursively, skip any directory that is on a file system different from that of the corresponding command line argument"),
// [DIFFERS]
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
// [DIFFERS]
    OPTION("", "--preserve-root", "do not remove '/' (default)", OPTIONAL_STRING_TYPE)};
// clang-format on

// ======================================================
// Pipeline components
// ======================================================
namespace rm_pipeline {
namespace cp = core::pipeline;

enum class InteractiveMode { never, once, always, sometimes };

struct RmConfig {
  bool force = false;
  InteractiveMode interactive = InteractiveMode::never;
  bool recursive = false;
  bool remove_empty_dir = false;
  bool verbose = false;
  bool one_file_system = false;
  bool no_preserve_root = false;
  bool preserve_root = true;
  bool preserve_root_all = false;
  // [GNU] RMI_SOMETIMES (the default when no -i/-I/-f/--interactive is
  // given) prompts only for write-protected entries, and only when
  // standard input is a terminal (rm.c:189-199, remove.c:213-221).
  bool stdin_is_tty = false;
};

auto get_system_error_message(DWORD error) -> std::wstring {
  LPWSTR raw = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS;
  // Use English to avoid multibyte encoding issues in non-Unicode console paths
  DWORD len = FormatMessageW(flags, nullptr, error,
                             MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                             (LPWSTR)&raw, 0, nullptr);
  if (len == 0 || raw == nullptr) {
    // Fallback to system default language
    len = FormatMessageW(flags, nullptr, error,
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                         (LPWSTR)&raw, 0, nullptr);
  }
  if (len == 0 || raw == nullptr) {
    return L"unknown error";
  }

  std::wstring message(raw, len);
  LocalFree(raw);
  while (!message.empty() &&
         (message.back() == L'\r' || message.back() == L'\n' ||
          message.back() == L' ' || message.back() == L'\t')) {
    message.pop_back();
  }
  return message;
}

/**
 * @brief Read a single y/n response from stdin, consuming the entire input
 * line to prevent leftover characters from polluting subsequent prompts.
 * @return true if the user answered 'y' or 'Y', false otherwise.
 */
auto read_yes_no_response() -> bool {
  std::string line;
  if (!std::getline(std::cin, line)) {
    return false;
  }
  return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
}

/**
 * @brief Prompt the user to confirm removal of a non-directory item.
 * [GNU] The prompt names the item kind: "regular file", "regular empty
 * file", "symbolic link", ... and never appends a "(y/n)" suffix (#1327).
 * @param path Path to display in the prompt.
 * @param noun GNU item-kind wording for the prompt.
 * @return true if the user confirms, false otherwise.
 */
auto prompt_remove_typed(std::string_view path, std::string_view noun)
    -> bool {
  safeErrorPrint(winux::i18n::format("command.rm.prompt.remove_typed",
                                     "rm: remove {} '{}'? ", noun, path));
  return read_yes_no_response();
}

/**
 * @brief Prompt for a write-protected item.  [GNU] remove.c:316-317:
 * "remove write-protected <kind> '<path>'?" — used both by -i and by
 * the default RMI_SOMETIMES mode when a prompt fires at all.
 */
auto prompt_remove_write_protected(std::string_view path,
                                   std::string_view noun) -> bool {
  safeErrorPrint(winux::i18n::format(
      "command.rm.prompt.remove_write_protected",
      "rm: remove write-protected {} '{}'? ", noun, path));
  return read_yes_no_response();
}

auto prompt_descend_write_protected_directory(std::string_view path) -> bool {
  safeErrorPrint(winux::i18n::format(
      "command.rm.prompt.descend_write_protected_directory",
      "rm: descend into write-protected directory '{}'? ", path));
  return read_yes_no_response();
}

/**
 * @brief Prompt the user to confirm removal of a directory itself.
 * @param path Path to display in the prompt.
 * @return true if the user confirms, false otherwise.
 */
auto prompt_remove_directory(std::string_view path) -> bool {
  safeErrorPrint(winux::i18n::format("command.rm.prompt.remove_directory",
                                     "rm: remove directory '{}'? ", path));
  return read_yes_no_response();
}

/**
 * @brief Prompt the user to confirm descending into a directory.
 * Matches GNU rm's "descend into directory" prompt for -ri.
 * @param path Path to display in the prompt.
 * @return true if the user confirms, false otherwise.
 */
auto prompt_descend_directory(std::string_view path) -> bool {
  safeErrorPrint(winux::i18n::format("command.rm.prompt.descend_directory",
                                     "rm: descend into directory '{}'? ",
                                     path));
  return read_yes_no_response();
}

/**
 * @brief True when a directory holds no entries besides "." and "..".
 * An enumeration failure other than "no entries" is reported as
 * non-empty, so callers keep the descend prompt and the removal attempt
 * surfaces the real error.
 */
auto directory_is_empty(const std::wstring& wpath) -> bool {
  WIN32_FIND_DATAW findData;
  HANDLE hFind = FindFirstFileW((wpath + L"\\*").c_str(), &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    return GetLastError() == ERROR_FILE_NOT_FOUND;
  }
  bool empty = true;
  do {
    std::wstring_view name(findData.cFileName);
    if (name != L"." && name != L"..") {
      empty = false;
      break;
    }
  } while (FindNextFileW(hFind, &findData));
  FindClose(hFind);
  return empty;
}

/**
 * @brief The operand's OWN attributes when it is a reparse point (symlink
 * or junction), or nullopt when it is not one.  GetFileAttributesW — and
 * therefore operand_target_attributes_w — reports the TARGET's attributes
 * for a link, so classification probes the link itself through
 * FindFirstFileW, whose find data describes the link (#1327).  The find
 * data also describes a dangling link, whose target attributes read as
 * INVALID_FILE_ATTRIBUTES even though the link itself exists.
 */
auto operand_link_attributes(const std::wstring& wpath)
    -> std::optional<DWORD> {
  WIN32_FIND_DATAW findData;
  HANDLE hFind = FindFirstFileW(wpath.c_str(), &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  const DWORD attributes = findData.dwFileAttributes;
  FindClose(hFind);
  if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
    return std::nullopt;
  }
  return attributes;
}

/**
 * @brief True when the operand itself is a reparse point (symlink or
 * junction); see operand_link_attributes.
 */
auto operand_is_reparse_link(const std::wstring& wpath) -> bool {
  return operand_link_attributes(wpath).has_value();
}

/**
 * @brief Join a display path with a child name the way GNU prints it:
 * parent + "/" + name (no extra separator when the parent already ends
 * with one).  Display paths are operand-relative, unlike the extended
 * \\?\ API paths used for the actual removal (#1328).
 */
auto child_display_path(const std::string& parent_display,
                        const std::string& item_name) -> std::string {
  if (!parent_display.empty() &&
      (parent_display.back() == '/' || parent_display.back() == '\\')) {
    return parent_display + item_name;
  }
  return parent_display + "/" + item_name;
}

/**
 * @brief Check if paths are provided
 * @param paths Paths to check
 * @return Result with paths if valid, error otherwise
 */
auto check_paths(const std::vector<std::string>& paths)
    -> cp::Result<std::vector<std::string>> {
  if (paths.empty()) {
    // [GNU] rm's wording is "missing operand" (rm.c:334-337), unlike
    // the "missing file operand" of cp/mv/ln.
    return std::unexpected("missing operand");
  }
  return paths;
}

auto is_root_path(std::string_view path) -> bool {
  // [GNU] The check is by identity (dev/ino of "/"), so any spelling
  // that resolves to the root counts — including a run of separators
  // alone ("//", "\\\\"), which the old exact-match test missed.
  if (!path.empty() && path.find_first_not_of("/\\") == std::string_view::npos) {
    return true;
  }

  if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    for (size_t i = 3; i < path.size(); ++i) {
      if (path[i] != '\\' && path[i] != '/') {
        return false;
      }
    }
    return true;
  }

  return false;
}

auto get_volume_root(const std::wstring& path) -> std::wstring {
  wchar_t volume[MAX_PATH];
  if (!GetVolumePathNameW(path.c_str(), volume, MAX_PATH)) {
    return {};
  }
  return volume;
}

auto confirm_bulk_remove(size_t path_count, bool recursive) -> bool {
  // [GNU] ngettext singular/plural, no "(y/n)" suffix (rm.c:351-364).
  safeErrorPrint("rm: remove ");
  safeErrorPrint(std::to_string(path_count));
  safeErrorPrint(path_count == 1 ? " argument" : " arguments");
  safeErrorPrint(recursive ? " recursively? " : "? ");
  return read_yes_no_response();
}

// [GNU] --interactive WHEN follows argmatch (rm.c:84-96, 258-286): an
// exact spelling wins, otherwise a prefix that matches spellings of a
// single mode. (The yes/no/none spellings are the applet's extension
// of the oracle's never/once/always set.)
auto parse_interactive_mode(std::string_view value)
    -> std::optional<InteractiveMode> {
  if (value.empty()) {
    return InteractiveMode::always;
  }
  static constexpr std::pair<std::string_view, InteractiveMode> kNames[] = {
      {"never", InteractiveMode::never},
      {"no", InteractiveMode::never},
      {"none", InteractiveMode::never},
      {"once", InteractiveMode::once},
      {"always", InteractiveMode::always},
      {"yes", InteractiveMode::always},
  };
  for (auto [name, mode] : kNames) {
    if (value == name) return mode;
  }
  std::optional<InteractiveMode> matched;
  for (auto [name, mode] : kNames) {
    if (name.starts_with(value)) {
      if (matched && *matched != mode) return std::nullopt;
      matched = mode;
    }
  }
  return matched;
}

auto path_is_current_or_parent_directory(std::wstring_view path) -> bool {
  path = native_path::strip_trailing_separators(path);
  size_t pos = path.find_last_of(L"\\/");
  auto name = pos == std::wstring_view::npos ? path : path.substr(pos + 1);
  if (name.empty() && pos != std::wstring_view::npos) {
    auto parent = native_path::strip_trailing_separators(path.substr(0, pos));
    pos = parent.find_last_of(L"\\/");
    name = pos == std::wstring_view::npos ? parent : parent.substr(pos + 1);
  }
  return name == L"." || name == L"..";
}

auto clear_readonly_attribute(const std::wstring& path, DWORD attr) -> void {
  if (attr == INVALID_FILE_ATTRIBUTES ||
      (attr & FILE_ATTRIBUTE_READONLY) == 0) {
    return;
  }
  SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
}

auto remove_empty_directory_path(const std::wstring& wpath,
                                 std::string_view display_path,
                                 const RmConfig& cfg) -> bool {
  DWORD attr = GetFileAttributesW(wpath.c_str());
  // [GNU] RMI_SOMETIMES: a write-protected directory prompts before
  // removal when stdin is a terminal (remove.c:213-221, 316-317).
  if (cfg.interactive == InteractiveMode::sometimes && cfg.stdin_is_tty &&
      attr != INVALID_FILE_ATTRIBUTES &&
      (attr & FILE_ATTRIBUTE_READONLY) != 0) {
    if (!prompt_remove_write_protected(display_path, "directory")) {
      return true;  // declined: success, directory kept
    }
  }
  clear_readonly_attribute(wpath, attr);
  if (!RemoveDirectoryW(wpath.c_str())) {
    // [GNU] One diagnostic shape for every removal failure:
    // "cannot remove '<path>': <strerror>" (remove.c:430, 458).
    safeErrorPrint("rm: cannot remove '");
    safeErrorPrint(display_path);
    safeErrorPrint("': ");
    safeErrorPrint(win32_posix_error_text(GetLastError()));
    safeErrorPrint("\n");
    return false;
  }

  if (cfg.verbose) {
    // [GNU] Directory removals are reported as "removed directory"
    // (#1328); files keep the plain "removed" form below.
    safePrint("removed directory '");
    safePrint(display_path);
    safePrint("'\n");
  }
  return true;
}

auto remove_file_path(const std::wstring& wpath, std::string_view display_path,
                      const RmConfig& cfg) -> bool {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  const bool have_data = GetFileAttributesExW(wpath.c_str(),
                                              GetFileExInfoStandard, &data);
  DWORD attr = have_data ? data.dwFileAttributes : INVALID_FILE_ATTRIBUTES;
  // [GNU] RMI_SOMETIMES: a write-protected file prompts before removal
  // when stdin is a terminal; without the prompt the old code silently
  // cleared the attribute and deleted (remove.c:213-221, 316-317).
  if (cfg.interactive == InteractiveMode::sometimes && cfg.stdin_is_tty &&
      attr != INVALID_FILE_ATTRIBUTES &&
      (attr & FILE_ATTRIBUTE_READONLY) != 0) {
    const bool empty_file =
        have_data && data.nFileSizeHigh == 0 && data.nFileSizeLow == 0;
    if (!prompt_remove_write_protected(
            display_path, empty_file ? "regular empty file" : "regular file")) {
      return true;  // declined: success, file kept
    }
  }
  clear_readonly_attribute(wpath, attr);
  if (!DeleteFileW(wpath.c_str())) {
    safeErrorPrint("rm: cannot remove '");
    safeErrorPrint(display_path);
    safeErrorPrint("': ");
    safeErrorPrint(win32_posix_error_text(GetLastError()));
    safeErrorPrint("\n");
    return false;
  }

  if (cfg.verbose) {
    safePrint("removed '");
    safePrint(display_path);
    safePrint("'\n");
  }
  return true;
}

auto build_config(const CommandContext<RM_OPTIONS.size()>& ctx)
    -> cp::Result<RmConfig> {
  RmConfig cfg;
  cfg.recursive = ctx.get<bool>("--recursive", false) ||
                  ctx.get<bool>("-r", false) || ctx.get<bool>("-R", false);
  cfg.remove_empty_dir =
      ctx.get<bool>("--dir", false) || ctx.get<bool>("-d", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  cfg.one_file_system = ctx.get<bool>("--one-file-system", false);
  DWORD console_mode = 0;
  cfg.stdin_is_tty =
      GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &console_mode) != 0;

  if (!ctx.metas) {
    return cfg;
  }

  bool saw_prompt_option = false;
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= ctx.metas->size()) {
      continue;
    }

    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.short_name == "-f" || meta.long_name == "--force") {
      cfg.force = true;
      cfg.interactive = InteractiveMode::never;
      saw_prompt_option = true;
    } else if (meta.short_name == "-i") {
      cfg.force = false;
      cfg.interactive = InteractiveMode::always;
      saw_prompt_option = true;
    } else if (meta.short_name == "-I") {
      cfg.force = false;
      cfg.interactive = InteractiveMode::once;
      saw_prompt_option = true;
    } else if (meta.long_name == "--interactive") {
      cfg.force = false;
      saw_prompt_option = true;
      auto value = std::get_if<std::string>(&occurrence.value);
      auto mode = parse_interactive_mode(value ? *value : std::string_view{});
      if (!mode.has_value()) {
        return std::unexpected(
            "invalid argument '" + *value +
            "' for '--interactive'\n"
            "Valid arguments are:\n"
            "  - 'never'\n"
            "  - 'once'\n"
            "  - 'always'\n"
            "Try 'rm --help' for more information.");
      }
      cfg.interactive = *mode;
    } else if (meta.long_name == "--no-preserve-root") {
      // [GNU] --preserve-root/--no-preserve-root toggle one boolean in
      // argv order; the later option wins (rm.c:295-322).
      cfg.preserve_root = false;
      cfg.preserve_root_all = false;
    } else if (meta.long_name == "--preserve-root") {
      auto value = std::get_if<std::string>(&occurrence.value);
      const std::string text = value ? *value : std::string{};
      if (!text.empty() && text != "all") {
        return std::unexpected("unrecognized --preserve-root argument: '" +
                               text + "'");
      }
      cfg.preserve_root = true;
      cfg.preserve_root_all = (text == "all");
    }
  }
  cfg.no_preserve_root = !cfg.preserve_root;

  // [GNU] With no prompting option at all, rm runs in RMI_SOMETIMES
  // mode: prompt only for write-protected entries, only on a terminal.
  if (!saw_prompt_option) {
    cfg.interactive = InteractiveMode::sometimes;
  }

  return cfg;
}

/**
 * @brief Remove a file or directory
 * @param path Path to remove
 * @param
 * ctx Command context with options
 * @return true if removal was successful,
 * false on error
 */
auto remove_path(const std::string& path, const RmConfig& cfg) -> bool {
  // Use extended-length path to bypass Windows reserved device names
  // (nul, con, prn, aux, com0-9, lpt0-9) which would otherwise redirect
  // file operations to the corresponding device instead of the actual file.
  auto operand = native_path::make_api_path_operand(path);
  const std::wstring& wpath = operand.extended;
  DWORD attr = native_path::operand_target_attributes_w(operand);
  // When the operand itself is a link, its own attributes — not the
  // target's — drive classification (GNU's lstat rule, #1433).  The
  // target probe above reports a directory for a link-to-directory and
  // INVALID_FILE_ATTRIBUTES for a dangling link; both misclassify.
  const std::optional<DWORD> link_attr = operand_link_attributes(wpath);

  // [GNU] The '.'/'..' refusal is unconditional (remove.c:470-478);
  // it used to be gated on -r here, so `rm -d .` attempted the removal.
  if (path_is_current_or_parent_directory(utf8_to_wstring(path))) {
    safeErrorPrint("rm: refusing to remove '.' or '..' directory: skipping '");
    safeErrorPrint(path);
    safeErrorPrint("'\n");
    return false;
  }

  // [GNU] The root failsafe is an identity check loaded only for
  // recursive runs (rm.c:340-347): a bare `rm /` without -r reports
  // "Is a directory" from the directory branch below instead.
  // Wording follows lib/root-dev-ino.h ROOT_DEV_INO_WARN.
  if (cfg.recursive && cfg.preserve_root && is_root_path(path)) {
    if (path == "/" || path == "\\") {
      safeErrorPrint("rm: it is dangerous to operate recursively on '/'\n");
    } else {
      safeErrorPrint("rm: it is dangerous to operate recursively on '");
      safeErrorPrint(path);
      safeErrorPrint("' (same as '/')\n");
    }
    safeErrorPrint("rm: use --no-preserve-root to override this failsafe\n");
    return false;
  }

  // --preserve-root=all: also protect mount points
  if (cfg.preserve_root_all && cfg.recursive) {
    std::wstring wvol = get_volume_root(wpath);
    if (!wvol.empty()) {
      // Normalize: ensure trailing backslash for comparison
      std::wstring norm_path = wpath;
      if (!norm_path.empty() && norm_path.back() != L'\\' &&
          norm_path.back() != L'/') {
        norm_path += L'\\';
      }
      std::wstring norm_vol = wvol;
      if (!norm_vol.empty() && norm_vol.back() != L'\\' &&
          norm_vol.back() != L'/') {
        norm_vol += L'\\';
      }
      if (_wcsicmp(norm_path.c_str(), norm_vol.c_str()) == 0) {
        safeErrorPrint("rm: it is dangerous to operate recursively on '");
        safeErrorPrint(path);
        safeErrorPrint("' (same as '");
        safeErrorPrint(wstring_to_utf8(wvol));
        safeErrorPrint("')\n");
        return false;
      }
    }
  }

  if (attr == INVALID_FILE_ATTRIBUTES &&
      !(link_attr && !operand.had_trailing_separator)) {
    // [GNU] -f ignores only the "guaranteed nonexistent" errnos
    // (ENOENT, ENOTDIR, EINVAL, EILSEQ — remove.c:330-360); any other
    // probe failure is a real error even under -f.
    DWORD probe_error = 0;
    GetFileAttributesW(wpath.c_str());
    probe_error = GetLastError();
    const bool nonexistent = probe_error == ERROR_FILE_NOT_FOUND ||
                             probe_error == ERROR_PATH_NOT_FOUND ||
                             probe_error == ERROR_INVALID_NAME;
    if (cfg.force && nonexistent) {
      return true;
    }
    safeErrorPrint("rm: cannot remove '");
    safeErrorPrint(path);
    safeErrorPrint("': ");
    safeErrorPrint(nonexistent ? "No such file or directory"
                               : win32_posix_error_text(probe_error));
    safeErrorPrint("\n");
    return false;
  }

  if (operand.had_trailing_separator &&
      (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    safeErrorPrint("rm: cannot remove '");
    safeErrorPrint(path);
    safeErrorPrint("': Not a directory\n");
    return false;
  }

  const bool is_directory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const bool is_reparse_point = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  // Interactive prompt before removal.  [GNU] Prompt selection follows
  // the operand kind (#1327): a directory is asked about with a descend
  // prompt only when it holds something; an empty directory is asked
  // about directly ("remove directory"); a directory without -r or -d
  // is never prompted (it falls through to the "Is a directory"
  // diagnostic); a non-empty directory with -d is never prompted (it
  // falls through to "Directory not empty").  Files and symlinks get the
  // typed prompt.  Note the existence check above runs first, so a
  // missing operand is diagnosed without any prompt.
  bool top_dir_empty = false;
  if (cfg.interactive == InteractiveMode::always) {
    bool response = true;
    bool prompted = true;
    if (operand_is_reparse_link(wpath)) {
      response = prompt_remove_typed(path, "symbolic link");
    } else if (is_directory) {
      if (cfg.recursive) {
        top_dir_empty = directory_is_empty(wpath);
        // [GNU] Under -i a write-protected directory gets the
        // write-protected descend prompt (remove.c:278-279).
        response =
            top_dir_empty
                ? prompt_remove_directory(path)
                : ((attr & FILE_ATTRIBUTE_READONLY) != 0
                       ? prompt_descend_write_protected_directory(path)
                       : prompt_descend_directory(path));
      } else if (cfg.remove_empty_dir) {
        top_dir_empty = directory_is_empty(wpath);
        if (top_dir_empty) {
          response = prompt_remove_directory(path);
        } else {
          prompted = false;  // non-empty with -d: diagnostic, no prompt
        }
      } else {
        prompted = false;  // plain directory: diagnostic, no prompt
      }
    } else {
      WIN32_FILE_ATTRIBUTE_DATA data{};
      const bool have_data =
          GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data);
      const bool empty_file =
          have_data && data.nFileSizeHigh == 0 && data.nFileSizeLow == 0;
      const std::string_view noun =
          empty_file ? "regular empty file" : "regular file";
      // [GNU] Under -i a write-protected file gets the
      // write-protected prompt form (remove.c:316-317).
      response = (have_data &&
                  (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
                     ? prompt_remove_write_protected(path, noun)
                     : prompt_remove_typed(path, noun);
    }
    if (prompted && !response) {
      return true;  // user declined, treat as success
    }
  }

  // [GNU] A link operand is a leaf: rm unlinks the link itself, whatever
  // the target is — "rm linkdir" succeeds without -r (the target-attribute
  // branch below used to refuse it with "Is a directory") and "rm -r
  // linkdir" leaves the target's contents untouched (the recursion below
  // used to descend through the link) (#1433).  The link's own DIRECTORY
  // bit selects the removal call, as for link children in the recursion.
  // A trailing separator opts out: it dereferences the link, and the
  // directory branches then apply to the target ("rm linkdir/").
  if (link_attr && !operand.had_trailing_separator) {
    if (*link_attr & FILE_ATTRIBUTE_DIRECTORY) {
      return remove_empty_directory_path(wpath, path, cfg);
    }
    return remove_file_path(wpath, path, cfg);
  }

  if ((attr & FILE_ATTRIBUTE_DIRECTORY) && !cfg.recursive) {
    if (!cfg.remove_empty_dir) {
      // OPTIMIZED: Avoid wstring concatenation
      safeErrorPrint("rm: cannot remove '");
      safeErrorPrint(path);
      safeErrorPrint("': Is a directory\n");
      return false;
    }

    return remove_empty_directory_path(wpath, path, cfg);
  }

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
      return remove_empty_directory_path(wpath, path, cfg);
    }

    // Recursive function to delete directory with post-order traversal.
    // A display path is threaded alongside the API path so prompts and
    // verbose lines show operand-relative paths instead of the extended
    // \\?\ form (#1328).  prompt_final is false only for a top-level
    // empty directory, whose removal was already prompted for above.
    std::function<bool(const std::wstring&, const std::string&, bool)>
        remove_directory_recursive;
    std::wstring root_volume =
        cfg.one_file_system ? get_volume_root(wpath) : L"";
    remove_directory_recursive = [&](const std::wstring& dirPath,
                                     const std::string& dirDisplay,
                                     bool prompt_final) -> bool {
      if (cfg.one_file_system && !root_volume.empty()) {
        std::wstring current_volume = get_volume_root(dirPath);
        if (!current_volume.empty() && current_volume != root_volume) {
          // [GNU] A -x skip is always reported on stderr and fails
          // the run (remove.c:557-567); it used to be a verbose-only
          // stdout note that left the exit status at 0.
          safeErrorPrint("rm: skipping '");
          safeErrorPrint(dirDisplay);
          safeErrorPrint("', since it's on a different device\n");
          return false;
        }
      }

      // Enumerate all items in the directory.  An empty directory
      // enumerates as ERROR_FILE_NOT_FOUND: not an error, just an empty
      // item list — fall through to the final removal below.
      std::wstring searchPath = dirPath + L"\\*";
      WIN32_FIND_DATAW findData;
      HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

      std::vector<std::pair<std::wstring, std::string>> subdirs;
      bool success = true;
      bool all_removed = true;

      if (hFind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
          // [GNU] FTS_DNR wording (remove.c:509-513).
          safeErrorPrint("rm: cannot read directory '");
          safeErrorPrint(dirDisplay);
          safeErrorPrint("': ");
          safeErrorPrint(win32_posix_error_text(error));
          safeErrorPrint("\n");
          return false;
        }
      } else {
        do {
          std::wstring itemName(findData.cFileName);
          if (itemName != L"." && itemName != L"..") {
            std::wstring itemPath = dirPath + L"\\" + itemName;
            std::string itemDisplay =
                child_display_path(dirDisplay, wstring_to_utf8(itemName));

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
              // Prompt before descending into subdirectories.  Empty
              // subdirectories get no descend prompt: their own removal
              // prompt happens at the end of the recursion (#1327).
              const bool item_write_protected =
                  (findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
              if (!(findData.dwFileAttributes &
                    FILE_ATTRIBUTE_REPARSE_POINT) &&
                  !directory_is_empty(itemPath)) {
                if (cfg.interactive == InteractiveMode::always) {
                  const bool declined =
                      item_write_protected
                          ? !prompt_descend_write_protected_directory(
                                itemDisplay)
                          : !prompt_descend_directory(itemDisplay);
                  if (declined) {
                    all_removed = false;  // user declined, leave subtree
                    continue;
                  }
                } else if (cfg.interactive == InteractiveMode::sometimes &&
                           cfg.stdin_is_tty && item_write_protected) {
                  // [GNU] RMI_SOMETIMES descend prompt for a
                  // write-protected directory (remove.c:278-279).
                  if (!prompt_descend_write_protected_directory(itemDisplay)) {
                    all_removed = false;
                    continue;
                  }
                }
              }
              // Store subdirectory for later recursive deletion
              subdirs.emplace_back(itemPath, itemDisplay);
            } else {
              // Prompt before removing files and file symlinks
              if (cfg.interactive == InteractiveMode::always) {
                std::string_view noun = "regular file";
                if (findData.dwFileAttributes &
                    FILE_ATTRIBUTE_REPARSE_POINT) {
                  noun = "symbolic link";
                } else if (findData.nFileSizeHigh == 0 &&
                           findData.nFileSizeLow == 0) {
                  noun = "regular empty file";
                }
                // [GNU] Write-protected entries get the
                // write-protected prompt form under -i too
                // (remove.c:316-317).
                const bool declined =
                    (findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0
                        ? !prompt_remove_write_protected(itemDisplay, noun)
                        : !prompt_remove_typed(itemDisplay, noun);
                if (declined) {
                  all_removed = false;  // user declined
                  continue;
                }
              }
              if (!remove_file_path(itemPath, itemDisplay, cfg)) {
                success = false;
                all_removed = false;
              }
            }
          }
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
      }

      // If we encountered errors, don't continue
      if (!success) {
        return false;
      }

      // Recursively delete all subdirectories (post-order traversal)
      for (const auto& [subdir, subdir_display] : subdirs) {
        DWORD sub_attr = GetFileAttributesW(subdir.c_str());
        bool sub_success = false;
        if (sub_attr != INVALID_FILE_ATTRIBUTES &&
            (sub_attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
          // Prompt before removing reparse points (symlinks/junctions).
          // Note the attributes here describe the link itself only when
          // the enumeration reported it as a directory entry; the
          // removal target is the link, not its referent.
          if (cfg.interactive == InteractiveMode::always) {
            if (!prompt_remove_typed(subdir_display, "symbolic link")) {
              all_removed = false;
              continue;
            }
          }
          sub_success =
              remove_empty_directory_path(subdir, subdir_display, cfg);
        } else {
          sub_success =
              remove_directory_recursive(subdir, subdir_display, true);
        }
        if (!sub_success) {
          success = false;
          all_removed = false;
        }
      }

      if (!success) {
        return false;
      }

      // If the user declined to remove any child, the directory is still
      // non-empty — leave it alone (matches GNU rm behavior).
      if (!all_removed) {
        return true;
      }

      // Finally, remove the directory itself.  GNU asks first, unless
      // the top-level prompt above already covered this directory.
      if (cfg.interactive == InteractiveMode::always && prompt_final) {
        if (!prompt_remove_directory(dirDisplay)) {
          return true;  // declined: leave the (now empty) directory
        }
      }
      return remove_empty_directory_path(dirPath, dirDisplay, cfg);
    };

    // [GNU] RMI_SOMETIMES: a write-protected, non-empty top-level
    // directory gets the descend prompt before anything inside it is
    // touched (remove.c:278-279).
    if (cfg.interactive == InteractiveMode::sometimes && cfg.stdin_is_tty &&
        (attr & FILE_ATTRIBUTE_READONLY) != 0 &&
        !directory_is_empty(wpath)) {
      if (!prompt_descend_write_protected_directory(path)) {
        return true;  // declined: success, tree kept
      }
    }

    // Start recursive directory deletion.  A top-level empty directory
    // already got its "remove directory" prompt above, so the recursion
    // must not ask again for the root.
    return remove_directory_recursive(
        wpath, path,
        !(cfg.interactive == InteractiveMode::always && top_dir_empty));
  } else {
    return remove_file_path(wpath, path, cfg);
  }

  return true;
}

/**
 * @brief Process all paths
 * @param paths Paths to process
 * @param ctx Command context with options
 * @return Result with success status
 */
auto process_paths(const std::vector<std::string>& paths, const RmConfig& cfg)
    -> cp::Result<bool> {
  bool success = true;
  for (const auto& path : paths) {
    if (!remove_path(path, cfg)) {
      success = false;
    }
  }
  return success;
}

/**
 * @brief Main pipeline
 * @param ctx Command context
 * @return Result with success status
 */
auto process_command(const CommandContext<RM_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    return std::unexpected(cfg_result.error());
  }
  const auto& cfg = *cfg_result;

  std::vector<std::string> paths;
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          paths.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    paths.push_back(file_arg);
  }

  if (paths.empty() && cfg.force) {
    return true;
  }

  return check_paths(paths).and_then(
      [&](const std::vector<std::string>& valid_paths) {
        if (cfg.interactive == InteractiveMode::once &&
            (cfg.recursive || valid_paths.size() > 3)) {
          if (!confirm_bulk_remove(valid_paths.size(), cfg.recursive)) {
            return cp::Result<bool>{true};
          }
        }
        return process_paths(valid_paths, cfg);
      });
}
}  // namespace rm_pipeline

REGISTER_COMMAND(
    rm,
    /* cmd_name */ "rm",
    /* cmd_synopsis */ "remove files or directories",
    /* cmd_desc */
    "Remove the FILE(s).\n"
    "\n"
    "By default, rm does not remove directories. Use the --recursive (-r) "
    "option\n"
    "to remove each listed directory, too, along with all of its contents.\n",
    /* examples */
    "  rm file.txt               Remove file.txt\n"
    "  rm -r dir/                Recursively remove directory dir/\n"
    "  rm -v file1.txt file2.txt Verbose remove\n"
    "  rm -i file.txt            Interactive remove (prompt before removal)",
    /* see_also */ "cp(1), mv(1), mkdir(1), rmdir(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    RM_OPTIONS) {
  using namespace rm_pipeline;

  // [GNU] rm.c refuses ANY abbreviated spelling of --no-preserve-root
  // (getopt_long resolves the prefix, then rm errors out before acting):
  //   rm --no-p f  ->  "rm: you may not abbreviate the --no-preserve-root
  //   option" (exit 1, nothing removed).
  // ctx.raw_args is post-normalization: the dispatcher already expands
  // unambiguous long-option prefixes (getopt_long behaviour), so the
  // abbreviation is invisible there.  Inspect the original process
  // arguments via the CRT globals instead (uutils#10188, issue 962).
  {
    constexpr std::string_view kNoPreserveRoot = "--no-preserve-root";
    bool end_of_options = false;
    for (int i = 1; i < __argc; ++i) {
      const std::string token = wstring_to_utf8(__wargv[i]);
      std::string_view arg(token);
      if (end_of_options || arg == "--") {
        end_of_options = true;
        continue;
      }
      if (!arg.starts_with("--")) continue;
      const auto eq = arg.find('=');
      const bool has_arg = eq != std::string_view::npos;
      const std::string_view name = has_arg ? arg.substr(0, eq) : arg;
      if (name.size() <= 2 || !kNoPreserveRoot.starts_with(name)) continue;
      if (has_arg) {
        // getopt_long reports the argument on the *resolved* option name
        // before rm's abbreviation check runs.
        safeErrorPrintLn(
            "rm: option '--no-preserve-root' doesn't allow an argument");
        safeErrorPrintLn("Try 'rm --help' for more information.");
        return 1;
      }
      if (name.size() < kNoPreserveRoot.size()) {
        safeErrorPrintLn(
            "rm: you may not abbreviate the --no-preserve-root option");
        return 1;
      }
    }
  }

  auto result = process_command(ctx);
  if (!result) {
    if (result.error() == "missing operand") {
      safeErrorPrintLn("rm: missing operand");
      safeErrorPrintLn("Try 'rm --help' for more information.");
      return 1;
    }
    cp::report_error(result, L"rm");
    return 1;
  }

  return *result ? 0 : 1;
}
