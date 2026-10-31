// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for pathchk command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr PATHCHK_OPTIONS = std::array{
    // [GNU] -p: check for most POSIX systems (no long form in GNU)
    OPTION("-p", "", "check for most POSIX systems"),
    // [GNU] -P: check for empty names and leading dash (no long form in GNU)
    OPTION("-P", "", "check for empty names and leading dash"),
    // [GNU] --portability: equivalent to -p -P combined (no short form in GNU)
    OPTION("", "--portability",
           "check for all POSIX systems (equivalent to -p -P)")};

// ======================================================
// Helper functions
// ======================================================

namespace {
constexpr size_t kPosixPathMax = 256;
constexpr size_t kPosixNameMax = 14;
constexpr size_t kWindowsComponentMax = 255;
// Non-portability length gate for the current filesystem: GNU asks
// pathconf(_PC_PATH_MAX); 4096 is the PATH_MAX_MINIMUM analogue.
constexpr size_t kHostPathMax = 4096;

auto split_components(const std::string& path, bool windows_separators)
    -> std::vector<std::string> {
  std::vector<std::string> components;
  std::string current;
  for (char c : path) {
    bool separator = c == '/' || (windows_separators && c == '\\');
    if (separator) {
      components.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  components.push_back(current);
  return components;
}

// [GNU] pathchk.c (quoteaf): always single-quoted in these messages.
auto quote_always(const std::string& name) -> std::string {
  return "'" + name + "'";
}

// [GNU] pathchk.c (quotef): plain names print bare; the empty name
// prints as ''.
auto quote_plain(const std::string& name) -> std::string {
  return name.empty() ? std::string("''") : name;
}

// [GNU] pathchk.c no_leading_hyphen: a '-' at the start of the name
// or right after a '/'.
auto has_leading_dash_component(const std::string& path) -> bool {
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '-' && (i == 0 || path[i - 1] == '/')) return true;
  }
  return false;
}

// Check if path is valid for Windows (subject-side filesystem rules;
// these are the "current filesystem" checks of the default mode).
auto windows_path_error(const std::string& path) -> std::optional<std::string> {
  if (path.empty()) return "empty file name";

  bool has_drive_prefix = path.size() >= 2 &&
                          std::isalpha(static_cast<unsigned char>(path[0])) &&
                          path[1] == ':';

  // Check for invalid characters
  const std::string invalid_chars = "<>:\"|?*";
  for (size_t i = 0; i < path.size(); ++i) {
    char c = path[i];
    if (has_drive_prefix && i == 1) continue;
    if (invalid_chars.find(c) != std::string::npos) {
      return "invalid Windows filename";
    }
  }

  const std::array<std::string_view, 22> reserved = {
      "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
      "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

  for (auto component : split_components(path, true)) {
    if (component.empty()) continue;
    if (component.size() > kWindowsComponentMax) return "file name too long";
    while (!component.empty() &&
           (component.back() == '.' || component.back() == ' ')) {
      component.pop_back();
    }
    size_t last_dot = component.find('.');
    if (last_dot != std::string::npos) component.resize(last_dot);

    std::transform(
        component.begin(), component.end(), component.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (auto name : reserved) {
      if (component == name) return "invalid Windows filename";
    }
  }

  return std::nullopt;
}

// Outcome of the existence probe, mirroring lstat() in validate_
// file_name's non-portability branch.
enum class ProbeOutcome { kExists, kNotThere, kError };
struct ProbeResult {
  ProbeOutcome outcome;
  std::string error_text;  // POSIX wording when outcome == kError
};

auto probe_existence(const std::string& path) -> ProbeResult {
  std::error_code ec;
  // Route the probe through the shared API-path boundary (the expression
  // cp.cpp's file-local api_operand_w wraps; #1140/#1145) so an MSYS-form
  // operand (/d/x) stats the same file GNU's lstat sees on the as-typed
  // name (pathchk.c:289), instead of a current-drive root-relative path.
  auto status = std::filesystem::symlink_status(
      native_path::make_api_path_operand(path).extended, ec);
  if (!ec) {
    (void)status;
    return {ProbeOutcome::kExists, {}};
  }
  if (ec == std::errc::no_such_file_or_directory) {
    return {ProbeOutcome::kNotThere, {}};
  }
  // POSIX errno wording for the shapes a name probe can hit.
  std::string text;
  if (ec == std::errc::not_a_directory) {
    text = "Not a directory";
  } else if (ec == std::errc::filename_too_long) {
    text = "File name too long";
  } else if (ec == std::errc::permission_denied) {
    text = "Permission denied";
  } else {
    text = ec.message();
  }
  return {ProbeOutcome::kError, text};
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    pathchk,
    /* cmd_name */ "pathchk",
    /* cmd_synopsis */ "pathchk [OPTION] NAME...",
    /* cmd_desc */
    "Check whether file names are valid or portable.\n"
    "Check that each file name is valid for the current filesystem,\n"
    "and optionally check for POSIX portability.",
    /* examples */
    "  pathchk myfile.txt\n"
    "  pathchk -p /tmp/test\n"
    "  pathchk -P -p filename",
    /* see_also */ "ln, mkdir",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ PATHCHK_OPTIONS) {
  // [GNU] pathchk.c validate_file_name: -p selects the POSIX-
  // minimum checks, -P adds the empty-name / leading-dash checks,
  // --portability is "equivalent to -p -P" (pathchk.c:93,120-133).
  bool check_basic =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--portability", false);
  bool check_extra =
      ctx.get<bool>("-P", false) || ctx.get<bool>("--portability", false);

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("pathchk: missing operand");
    safeErrorPrintLn("Try 'pathchk --help' for more information.");
    return 1;
  }

  int exit_code = 0;

  for (const auto& path : ctx.positionals) {
    std::string name(path);
    std::optional<std::string> error_line;

    // [GNU] pathchk.c:270-272: the leading-dash check runs first.
    if (!error_line && check_extra && has_leading_dash_component(name)) {
      error_line = "pathchk: leading '-' in a component of file name " +
                   quote_always(name);
    }

    // [GNU] pathchk.c:274-285: portable modes reject the empty name.
    if (!error_line && (check_basic || check_extra) && name.empty()) {
      error_line = "pathchk: empty file name";
    }

    bool file_exists = false;
    if (!error_line && check_basic) {
      // [GNU] pathchk.c:287-291 + portable_chars_only: the POSIX
      // portable character set, first offender named.
      for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '.' && c != '_' && c != '-' &&
            c != '/') {
          error_line = "pathchk: non-portable character '" +
                       std::string(1, c) + "' in file name " +
                       quote_always(name);
          break;
        }
      }
    } else if (!error_line) {
      // [GNU] pathchk.c:292-306: lstat the full name.  A missing
      // name (ENOENT) is fine; any other failure is reported with
      // the errno text -- this is what makes 'regfile/file' fail
      // with "Not a directory" while 'missingdir/file' passes.
      ProbeResult probe = probe_existence(name);
      if (probe.outcome == ProbeOutcome::kExists) {
        file_exists = true;
      } else if (probe.outcome == ProbeOutcome::kError ||
                 name.empty()) {
        std::string text = probe.outcome == ProbeOutcome::kError
                               ? probe.error_text
                               : "No such file or directory";
        error_line = "pathchk: " + quote_plain(name) + ": " + text;
      }
    }

    // [GNU] pathchk.c:308-341: total length.  Portability mode
    // compares against _POSIX_PATH_MAX (256); the default mode
    // only for names that do not exist, against pathconf's value.
    if (!error_line &&
        (check_basic || (!file_exists && name.size() >= kHostPathMax))) {
      size_t limit = check_basic ? kPosixPathMax - 1 : kHostPathMax - 1;
      if (name.size() > limit) {
        error_line = "pathchk: limit " + std::to_string(limit) +
                     " exceeded by length " +
                     std::to_string(name.size()) + " of file name " +
                     quote_always(name);
      }
    }

    // [GNU] pathchk.c:343-415: component lengths.  Portability
    // mode always checks against _POSIX_NAME_MAX (14); the default
    // mode checks only nonexistent names with a component over
    // NAME_MAX_MINIMUM (255), against 255.
    if (!error_line) {
      size_t component_limit = 0;
      if (check_basic) {
        component_limit = kPosixNameMax;
      } else if (!file_exists) {
        for (const auto& comp : split_components(name, false)) {
          if (comp.size() > kWindowsComponentMax) {
            component_limit = kWindowsComponentMax;
            break;
          }
        }
      }
      if (component_limit != 0) {
        for (const auto& comp : split_components(name, false)) {
          if (comp.size() > component_limit) {
            error_line = "pathchk: limit " +
                         std::to_string(component_limit) +
                         " exceeded by length " +
                         std::to_string(comp.size()) +
                         " of file name component " + quote_always(comp);
            break;
          }
        }
      }
    }

    // Subject-side "current filesystem" rules (Windows reserved
    // names and characters).  Like GNU's filesystem checks, these
    // belong to the default and -P modes; -p / --portability is
    // pure POSIX and must not see them (pathchk -p CON is valid).
    if (!error_line && !check_basic) {
      if (auto win_error = windows_path_error(name)) {
        error_line = "pathchk: " + quote_plain(name) + ": " + *win_error;
      }
    }

    if (error_line) {
      safeErrorPrintLn(*error_line);
      exit_code = 1;
    }
  }

  return exit_code;
}
