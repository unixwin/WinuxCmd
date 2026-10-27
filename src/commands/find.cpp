// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 WinuxCmd
/// @Description: Implementation for find command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <AclAPI.h>

#include "core/command_macros.h"

import std;
import core;
import utils;
import container;
import version;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief FIND command options definition
 *
 * This array defines all the options supported by the find command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -name: Base of file name (the path with the leading directories
 * removed)
 * matches shell pattern PATTERN [IMPLEMENTED]
 * - @a -iname: Like
 * -name, but the match is case insensitive [IMPLEMENTED]
 * - @a -path: File
 * name matches shell pattern PATTERN [IMPLEMENTED]
 * - @a -ipath: Like -path,
 * but the match is case insensitive [IMPLEMENTED]
 * - @a -type: File is of
 *
 * type c: b,d,p,f,l,s,D [only d,f,l,p are supported]
 * [IMPLEMENTED]
 * - @a
 * -size: File uses n units of space [IMPLEMENTED]
 * - @a -empty: File is empty
 * and is either a regular file or a directory
 * [IMPLEMENTED]
 * - @a -mtime:
 * File data was last modified n*24 hours ago [IMPLEMENTED]
 * - @a -mmin: File
 * data was last modified n minutes ago [IMPLEMENTED]
 * - @a
 * -mindepth:
 * Descend at least LEVELS levels of directories before tests
 * [IMPLEMENTED]

 * * - @a -maxdepth: Descend at most LEVELS levels of directories below
 * starting-points [IMPLEMENTED]
 * - @a -print: Print the full file name on the standard output [IMPLEMENTED]
 * - @a -print0: Print the full file name on the standard output, followed by a
 * null character [IMPLEMENTED]
 * - @a -L: Follow symbolic links [IMPLEMENTED]
 * - @a -H: Do not follow
 * symbolic links, except while processing command line arguments [IMPLEMENTED]
 * - @a -P: Never follow symbolic links (default) [IMPLEMENTED]
 * - @a -delete: Delete files [IMPLEMENTED]
 * - @a -exec: Execute command
 * [IMPLEMENTED: ; and {} + forms]
 * - @a -ok: Execute command after
 * confirmation [IMPLEMENTED: ; form]
 * -
 * @a -printf: Print format [PARTIAL:
 %p,%f,%h,%l,%H,%P,%y,%Y,%s,%b,%k,%d,%D,%F,%g,%G,%o,%u,%U,%S,%m,%M,%n,%i,%A@,%B@,%C@,%T@,%AY,%Am,%Ad,%AH,%AM,%AS,%Aj,%BY,%Bm,%Bd,%BH,%BM,%BS,%Bj,%CY,%Cm,%Cd,%CH,%CM,%CS,%Cj,%TY,%Tm,%Td,%TH,%TM,%TS,%Tj,%%]
 * - @a -prune:

 * * Prune tree [IMPLEMENTED]
 * - @a -quit: Exit immediately [IMPLEMENTED]
 * -
 * @a -true: Always true [IMPLEMENTED]
 * - @a -false: Always false
 * [IMPLEMENTED]
 * - @a -regex: Whole path matches regular expression
 * [IMPLEMENTED]
 * - @a -iregex: Like -regex, but case insensitive
 * [IMPLEMENTED]
 * - @a -newer: File was modified more recently than reference
 * [IMPLEMENTED]
 * - @a -depth: Process directory contents before the directory
 * [IMPLEMENTED]
 */
auto constexpr FIND_OPTIONS = std::array{
    // [GNU]
    OPTION("-name", "",
           "base of file name (the path with the leading directories removed) "
           "matches shell pattern PATTERN",
           STRING_TYPE),
    // [GNU]
    OPTION("-iname", "", "like -name, but the match is case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-path", "", "file name matches shell pattern PATTERN", STRING_TYPE),
    // [GNU]
    OPTION("-ipath", "", "like -path, but the match is case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-wholename", "", "same as -path", STRING_TYPE),
    // [GNU]
    OPTION("-iwholename", "", "same as -ipath, but case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-type", "",
           "file is of type c: b,c,d,p,f,l,s,D [D: Solaris doors are not "
           "supported on this platform]",
           STRING_TYPE),
    // [GNU]
    OPTION("-xtype", "",
           "like -type but checks the opposite symlink resolution state "
           "[D: Solaris doors are not supported on this platform]",
           STRING_TYPE),
    // [GNU]
    OPTION("-execdir", "",
           "execute command from the subdirectory containing the matched file",
           TERMINATED_STRING_TYPE),
    // [GNU] -D/--debug (Windows diagnostics differ from findutils categories)
    OPTION("-D", "--debug",
           "print find traversal diagnostics [DIFFERS: Windows diagnostics]",
           STRING_TYPE),
    // [GNU] warning and readdir-race policy controls
    OPTION("-nowarn", "", "turn off file traversal warnings"),
    OPTION("-warn", "", "turn on file traversal warnings"),
    OPTION("-ignore_readdir_race", "",
           "ignore errors when entries disappear during traversal"),
    OPTION("-noignore_readdir_race", "",
           "report errors when entries disappear during traversal"),
    // [GNU]
    OPTION("-nouser", "", "file has no user corresponding to its owner ID"),
    // [GNU]
    OPTION("-used", "",
           "file was last accessed n days after its status was last changed",
           STRING_TYPE),
    // [GNU]
    OPTION("-perm", "",
           "file's permission bits match MODE [PARTIAL: octal modes only]",
           STRING_TYPE),
    // [GNU]
    OPTION("-readable", "", "file can be read"),
    // [GNU]
    OPTION("-writable", "", "file can be written"),
    // [GNU]
    OPTION("-executable", "", "file can be executed or searched"),
    // [GNU]
    OPTION("-inum", "", "file has inode/file-index number N", STRING_TYPE),
    // [GNU]
    OPTION("-links", "", "file has N hard links", STRING_TYPE),
    // [GNU]
    OPTION("-user", "", "file is owned by user name or UID", STRING_TYPE),
    // [GNU]
    OPTION("-group", "", "file belongs to group name or GID", STRING_TYPE),
    OPTION("-nogroup", "",
           "file has no group corresponding to its group ID [DIFFERS: Windows "
           "account mapping]"),
    // [GNU]
    OPTION("-uid", "", "file owner's numeric UID matches N", STRING_TYPE),
    // [GNU]
    OPTION("-gid", "", "file group's numeric GID matches N", STRING_TYPE),
    // [GNU]
    OPTION("-size", "", "file uses n units of space", STRING_TYPE),
    // [GNU]
    OPTION("-empty", "",
           "file is empty and is either a regular file or a directory"),
    // [GNU]
    OPTION("-amin", "", "file was last accessed n minutes ago", STRING_TYPE),
    // [GNU]
    OPTION("-atime", "", "file was last accessed n*24 hours ago", STRING_TYPE),
    // [GNU]
    OPTION("-cmin", "", "file status was last changed n minutes ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-ctime", "", "file status was last changed n*24 hours ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-mtime", "", "file data was last modified n*24 hours ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-mmin", "", "file data was last modified n minutes ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-mindepth", "",
           "descend at least LEVELS levels of directories before tests",
           INT_TYPE),
    // [GNU]
    OPTION("-maxdepth", "",
           "descend at most LEVELS levels of directories below starting-points",
           INT_TYPE),
    // [GNU]
    OPTION("-print", "", "print the full file name on the standard output"),
    // [GNU]
    OPTION("-print0", "",
           "print the full file name on the standard output, followed by a "
           "null character"),
    // [GNU]
    OPTION("-fprint", "", "print the full file name into FILE", STRING_TYPE),
    // [GNU]
    OPTION("-fprint0", "",
           "print the full file name into FILE, followed by a null character",
           STRING_TYPE),
    // [GNU]
    OPTION("-L", "", "follow symbolic links"),
    // [GNU]
    OPTION("-H", "",
           "do not follow symbolic links, except while processing command line "
           "arguments"),
    // [GNU]
    OPTION("-P", "", "never follow symbolic links (default)"),
    // [GNU]
    OPTION("-follow", "", "dereference symbolic links"),
    // [GNU]
    OPTION("-O", "", "set optimization level (0-3, currently ignored)",
           INT_TYPE),
    // [GNU]
    OPTION("-delete", "", "delete files"),
    // [GNU]
    OPTION("-exec", "", "execute command", TERMINATED_STRING_TYPE),
    // [GNU]
    OPTION("-ok", "", "execute command after confirmation",
           TERMINATED_STRING_TYPE),
    OPTION("-okdir", "",
           "execute command from the subdirectory after confirmation [DIFFERS: "
           "Windows command prompt]",
           TERMINATED_STRING_TYPE),
    OPTION("-ls", "",
           "list current file in ls -dils format [DIFFERS: Windows metadata]"),
    OPTION("-fls", "",
           "list current file in ls -dils format into FILE [DIFFERS: Windows "
           "metadata]",
           STRING_TYPE),
    // [GNU]
    OPTION(
        "-printf", "",
        "print format [PARTIAL: "
        "%p,%f,%h,%l,%H,%P,%y,%Y,%s,%b,%k,%d,%D,%F,%g,%G,%o,%u,%U,%S,%m,%M,%n,%"
        "i,%a,%c,%t,%A@,%B@,%C@,%T@,%AY,%Am,%Ad,%AH,%AM,%AS,%Aj,%BY,%Bm,%Bd,%BH,%BM,%BS,"
        "%Bj,%CY,%Cm,%Cd,%CH,%CM,%CS,%Cj,%TY,%Tm,%Td,%TH,%TM,%TS,%Tj,%%]",
        STRING_TYPE),
    // [GNU]
    OPTION("-fprintf", "", "print format into FILE", STRING_TYPE),
    // [GNU]
    OPTION("-prune", "", "prune tree"),
    // [GNU]
    OPTION("-quit", "", "exit immediately"),
    // [GNU]
    OPTION("-true", "", "always true"),
    // [GNU]
    OPTION("-false", "", "always false"),
    // [GNU]
    OPTION("-regex", "", "whole path matches regular expression", STRING_TYPE),
    // [GNU]
    OPTION("-iregex", "",
           "whole path matches regular expression, case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-newer", "",
           "file was modified more recently than the reference file",
           STRING_TYPE),
    OPTION("-anewer", "",
           "file was accessed more recently than the reference file",
           STRING_TYPE),
    OPTION("-cnewer", "",
           "file status changed more recently than the reference file "
           "[DIFFERS: Windows change time]",
           STRING_TYPE),
    OPTION("-fstype", "",
           "file is on a file system of type TYPE [DIFFERS: Windows volume "
           "file system name]",
           STRING_TYPE),
    OPTION(
        "-ilname", "",
        "symbolic link target matches shell pattern PATTERN, case insensitive",
        STRING_TYPE),
    OPTION("-lname", "", "symbolic link target matches shell pattern PATTERN",
           STRING_TYPE),
    // [GNU]
    OPTION("-neweraa", "", "compare access time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-neweraB", "", "compare access time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerac", "", "compare access time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-neweram", "", "compare access time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerat", "", "compare access time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-newerBa", "", "compare birth time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBB", "", "compare birth time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBc", "", "compare birth time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBm", "", "compare birth time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBt", "", "compare birth time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-newerca", "", "compare change time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newercB", "", "compare change time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newercc", "", "compare change time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newercm", "", "compare change time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerct", "", "compare change time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-newerma", "", "compare modify time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermB", "", "compare modify time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermc", "", "compare modify time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermm", "", "compare modify time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermt", "", "compare modify time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-samefile", "",
           "file refers to the same file as the reference file", STRING_TYPE),
    // [GNU]
    OPTION("-files0-from", "", "read starting points from NUL-delimited file",
           STRING_TYPE),
    // [GNU]
    OPTION("-depth", "",
           "process each directory's contents before the directory itself"),
    // [GNU]
    OPTION("-d", "", "same as -depth"),
    // [GNU]
    OPTION("-daystart", "", "measure times from start of today"),
    // [GNU]
    OPTION("-mount", "", "do not descend into other file systems"),
    // [GNU]
    OPTION("-xdev", "", "same as -mount"),
    // [GNU]
    OPTION("-noleaf", "", "do not optimize by assuming 2+ hard links"),
    // [GNU]
    OPTION("-regextype", "", "set regex syntax for later -regex/-iregex",
           STRING_TYPE),
    // [GNU]
    OPTION("!", "", "negate expression"),
    // [GNU]
    OPTION("-not", "", "negate expression"),
    // [GNU]
    OPTION("-a", "", "and expression"),
    // [GNU]
    OPTION("-and", "", "and expression"),
    // [GNU]
    OPTION("-o", "", "or expression"),
    // [GNU]
    OPTION("-or", "", "or expression"),
    // [GNU] findutils accepts -help/-version (single dash) like every
    // other predicate-style option.
    OPTION("-help", "", "display this help and exit"),
    // [GNU]
    OPTION("-version", "", "output version information and exit"),
    // [GNU] SELinux context predicate: on a system without SELinux GNU
    // find reports "invalid predicate -context: SELinux is not enabled."
    OPTION("-context", "", "file matches SELinux security context CTX",
           STRING_TYPE)};

namespace find_pipeline {
namespace cp = core::pipeline;

struct Win32OwnershipInfo {
  std::string owner_name;
  std::string owner_id;
  std::string group_name;
  std::string group_id;
};

auto win32_ownership_info(const std::filesystem::path& p) -> Win32OwnershipInfo;

auto win32_accessible(const std::filesystem::path& p, DWORD desired_access)
    -> bool;
auto win32_executable(const std::filesystem::path& p,
                      const std::filesystem::directory_entry& e) -> bool;

enum class NumericComparison { Exact, GreaterThan, LessThan };

struct NumericPredicate {
  NumericComparison comparison = NumericComparison::Exact;
  long long value = 0;
};

struct SizePredicate {
  NumericPredicate predicate;
  unsigned long long unit = 512;
};

// Time predicates (-amin/-atime/-cmin/-ctime/-mmin/-mtime/-used) use a
// floating-point value, unlike the integer numerics above: GNU accepts
// fractions (0.5, .5, 1., 1e3) for every *min/*time numeric.
struct TimePredicate {
  NumericComparison comparison = NumericComparison::Exact;
  double value = 0;
};

enum class PermissionComparison { Exact, All, Any };

struct PermissionPredicate {
  PermissionComparison comparison = PermissionComparison::Exact;
  unsigned mode = 0;
};

struct FileIdentity {
  DWORD volume_serial = 0;
  ULONGLONG file_index = 0;

  auto operator==(const FileIdentity& other) const -> bool {
    return volume_serial == other.volume_serial &&
           file_index == other.file_index;
  }
};

enum class FindFileTimeKind { Access, Birth, Change, Modify };

enum class ExprKind {
  Always,
  Name,
  IName,
  Path,
  IPath,
  Regex,
  IRegex,
  Type,
  XType,
  Perm,
  Readable,
  Writable,
  Executable,
  Inum,
  Links,
  User,
  NoUser,
  NoGroup,
  Group,
  Uid,
  Gid,
  Empty,
  Size,
  ATime,
  AMin,
  CTime,
  CMin,
  MTime,
  MMin,
  Used,
  Newer,
  ANewer,
  CNewer,
  Fstype,
  Lname,
  ILname,
  NewerXY,
  SameFile,
  Print,
  Print0,
  FPrint,
  FPrint0,
  Printf,
  FPrintf,
  Ls,
  Fls,
  False,
  Exec,
  OkDir,
  Delete,
  Prune,
  Quit,
  Comma,
  And,
  Or,
  Not
};

struct ExprNode {
  ExprKind kind = ExprKind::Always;
  std::string text;
  std::string format_text;
  std::optional<portable_regex::Pattern> regex;
  std::optional<SizePredicate> size;
  std::optional<NumericPredicate> numeric;
  std::optional<TimePredicate> time_pred;
  std::optional<PermissionPredicate> permission;
  std::optional<std::filesystem::file_time_type> reference_time;
  std::optional<long long> reference_ticks;
  std::optional<FileIdentity> reference_identity;
  FindFileTimeKind time_kind = FindFileTimeKind::Modify;
  size_t action_index = 0;
  std::unique_ptr<ExprNode> left;
  std::unique_ptr<ExprNode> right;
};

struct ExecAction {
  bool prompt = false;
  bool aggregate = false;
  bool execdir = false;
  std::string command;
  std::filesystem::path working_directory;
  std::vector<std::string> args;
  std::vector<std::string> pending_paths;
};

struct Config {
  SmallVector<std::string, 64> roots;
  std::string name_pattern;
  std::string iname_pattern;
  std::string path_pattern;
  std::string ipath_pattern;
  std::string type_filter;
  std::optional<SizePredicate> size_filter;
  bool empty_filter = false;
  std::optional<TimePredicate> atime_filter;
  std::optional<TimePredicate> amin_filter;
  std::optional<TimePredicate> ctime_filter;
  std::optional<TimePredicate> cmin_filter;
  std::optional<TimePredicate> mtime_filter;
  std::optional<TimePredicate> mmin_filter;
  std::optional<TimePredicate> used_filter;
  int mindepth = 0;
  int maxdepth = std::numeric_limits<int>::max();
  bool has_print = false;
  bool delete_action = false;
  bool depth_first = false;
  std::vector<ExecAction> exec_actions;
  std::unordered_map<std::string, std::unique_ptr<std::ofstream>> output_files;
  bool follow_symlinks = false;
  bool follow_arg_symlinks = false;
  bool debug_enabled = false;
  bool nowarn = false;
  bool ignore_readdir_race = false;
  std::string debug_category;
  std::string files0_from;
  bool prune_current = false;
  bool quit = false;
  // [GNU] -daystart (findutils-4.10.0/find/pred.c): shift -amin/-atime/
  // -cmin/-ctime/-mmin/-mtime measurements to the start of the local day.
  bool daystart = false;
  // [GNU] -xdev/-mount (findutils-4.10.0/find/ftsfind.c): do not descend
  // into directories on other file systems.  On Windows this is emulated by
  // comparing the volume root (drive letter / mount point) of each descent
  // candidate with the traversal starting point; other kinds of mount
  // boundaries are detected only as far as GetVolumePathNameW exposes them.
  bool stay_on_filesystem = false;
  std::wstring xdev_volume_key;
  std::unique_ptr<ExprNode> expression;

  bool unsupported_used = false;
  bool had_error = false;
};

enum class SymlinkMode {
  Never,
  CommandLineOnly,
  All,
};

// Wildcard matching is now provided by utils:wildcard module

auto parse_numeric_predicate(std::string_view text)
    -> cp::Result<NumericPredicate> {
  if (text.empty()) return std::unexpected("missing numeric argument");

  NumericPredicate result;
  size_t pos = 0;
  if (text[pos] == '+') {
    result.comparison = NumericComparison::GreaterThan;
    ++pos;
  } else if (text[pos] == '-') {
    result.comparison = NumericComparison::LessThan;
    ++pos;
  }

  if (pos >= text.size()) return std::unexpected("invalid numeric argument");

  long long value = 0;
  auto begin = text.data() + pos;
  auto end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc() || ptr != end) {
    return std::unexpected("invalid numeric argument");
  }
  if (value < 0) return std::unexpected("invalid numeric argument");

  result.value = value;
  return result;
}

// Time predicates accept fractions: GNU parses *min/*time numerics with
// strtod semantics, so 0.5, .5, 1. and 1e3 are all valid. std::from_chars in
// general format covers those spellings; full consumption rejects a bare
// trailing exponent (1e), hex, and inf/nan (checked via isfinite).
auto parse_time_predicate(std::string_view text)
    -> cp::Result<TimePredicate> {
  if (text.empty()) return std::unexpected("missing numeric argument");

  TimePredicate result;
  size_t pos = 0;
  if (text[pos] == '+') {
    result.comparison = NumericComparison::GreaterThan;
    ++pos;
  } else if (text[pos] == '-') {
    result.comparison = NumericComparison::LessThan;
    ++pos;
  }

  if (pos >= text.size()) return std::unexpected("invalid numeric argument");
  if (text[pos] == '+' || text[pos] == '-') {
    return std::unexpected("invalid numeric argument");
  }

  double value = 0;
  auto begin = text.data() + pos;
  auto end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc() || ptr != end || !std::isfinite(value)) {
    return std::unexpected("invalid numeric argument");
  }
  if (value < 0) return std::unexpected("invalid numeric argument");

  result.value = value;
  return result;
}

auto parse_size_predicate(std::string_view text) -> cp::Result<SizePredicate> {
  if (text.empty()) return std::unexpected("missing -size argument");

  size_t suffix_pos = text.size();
  char suffix = '\0';
  unsigned char last = static_cast<unsigned char>(text.back());
  if (!std::isdigit(last)) {
    suffix = text.back();
    suffix_pos = text.size() - 1;
  }

  auto numeric = parse_numeric_predicate(text.substr(0, suffix_pos));
  if (!numeric) return std::unexpected(numeric.error());

  SizePredicate result;
  result.predicate = *numeric;
  switch (suffix) {
    case '\0':
    case 'b':
      result.unit = 512;
      break;
    case 'c':
      result.unit = 1;
      break;
    case 'w':
      result.unit = 2;
      break;
    case 'k':
      result.unit = 1024;
      break;
    case 'M':
      result.unit = 1024ULL * 1024ULL;
      break;
    case 'G':
      result.unit = 1024ULL * 1024ULL * 1024ULL;
      break;
    default:
      return std::unexpected("invalid -size unit");
  }

  return result;
}

auto parse_permission_predicate(std::string_view text)
    -> cp::Result<PermissionPredicate> {
  if (text.empty()) return std::unexpected("missing -perm argument");

  PermissionPredicate result;
  size_t pos = 0;
  if (text[pos] == '-') {
    result.comparison = PermissionComparison::All;
    ++pos;
  } else if (text[pos] == '/') {
    result.comparison = PermissionComparison::Any;
    ++pos;
  }

  if (pos >= text.size()) return std::unexpected("invalid -perm mode");

  auto symbolic_bit = [](char who, char perm) -> std::optional<unsigned> {
    switch (perm) {
      case 'r':
        if (who == 'u') return 0400U;
        if (who == 'g') return 0040U;
        if (who == 'o') return 0004U;
        break;
      case 'w':
        if (who == 'u') return 0200U;
        if (who == 'g') return 0020U;
        if (who == 'o') return 0002U;
        break;
      case 'x':
      case 'X':
        if (who == 'u') return 0100U;
        if (who == 'g') return 0010U;
        if (who == 'o') return 0001U;
        break;
      case 's':
        if (who == 'u') return 04000U;
        if (who == 'g') return 02000U;
        break;
      case 't':
        if (who == 'o') return 01000U;
        break;
      default:
        break;
    }
    return std::nullopt;
  };

  auto parse_symbolic = [&]() -> cp::Result<unsigned> {
    unsigned mode = 0;
    while (pos < text.size()) {
      std::string who;
      while (pos < text.size() && (text[pos] == 'u' || text[pos] == 'g' ||
                                   text[pos] == 'o' || text[pos] == 'a')) {
        if (text[pos] == 'a') {
          who = "ugo";
        } else if (who.find(text[pos]) == std::string::npos) {
          who.push_back(text[pos]);
        }
        ++pos;
      }
      if (who.empty()) who = "ugo";

      if (pos >= text.size() ||
          (text[pos] != '+' && text[pos] != '-' && text[pos] != '=')) {
        return std::unexpected("invalid -perm symbolic mode");
      }
      const char op = text[pos++];

      unsigned clause_bits = 0;
      while (pos < text.size() && text[pos] != ',') {
        const char perm = text[pos++];
        for (char target : who) {
          // [#1314] A u/g/o letter in permission position copies the
          // source class's current rwx bits (from the mode accumulated
          // so far, starting at 0) into the target class, for all three
          // operators; "=" still clears the target's bits first.
          if (perm == 'u' || perm == 'g' || perm == 'o') {
            const unsigned source = perm == 'u'   ? (mode >> 6) & 7U
                                     : perm == 'g' ? (mode >> 3) & 7U
                                                   : mode & 7U;
            const unsigned shift =
                target == 'u' ? 6U : target == 'g' ? 3U : 0U;
            clause_bits |= source << shift;
            continue;
          }
          auto bit = symbolic_bit(target, perm);
          if (!bit) {
            return std::unexpected("invalid -perm symbolic mode");
          }
          clause_bits |= *bit;
        }
      }

      if (op == '=') {
        unsigned who_mask = 0;
        for (char target : who) {
          who_mask |= target == 'u' ? 04700U : target == 'g' ? 02070U : 01007U;
        }
        mode = (mode & ~who_mask) | clause_bits;
      } else if (op == '+') {
        mode |= clause_bits;
      } else {
        mode &= ~clause_bits;
      }

      if (pos < text.size()) {
        if (text[pos] != ',')
          return std::unexpected("invalid -perm symbolic mode");
        ++pos;
        if (pos >= text.size())
          return std::unexpected("invalid -perm symbolic mode");
      }
    }
    return mode;
  };

  if (std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos] == '+' ||
      text[pos] == '=' || text[pos] == ',') {
    auto symbolic = parse_symbolic();
    if (!symbolic) return std::unexpected(symbolic.error());
    result.mode = *symbolic & 07777U;
    return result;
  }

  unsigned mode = 0;
  for (; pos < text.size(); ++pos) {
    unsigned char ch = static_cast<unsigned char>(text[pos]);
    if (ch < '0' || ch > '7') {
      return std::unexpected("invalid -perm mode");
    }
    mode = (mode << 3) + static_cast<unsigned>(ch - '0');
  }

  result.mode = mode & 07777;
  return result;
}

auto is_decimal_text(std::string_view text) -> bool {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

auto parse_find_time_kind(char ch) -> std::optional<FindFileTimeKind> {
  switch (ch) {
    case 'a':
      return FindFileTimeKind::Access;
    case 'B':
      return FindFileTimeKind::Birth;
    case 'c':
      return FindFileTimeKind::Change;
    case 'm':
      return FindFileTimeKind::Modify;
    default:
      return std::nullopt;
  }
}

auto is_newerxy_option(std::string_view option) -> bool {
  if (option.size() != 8 || option.substr(0, 6) != "-newer") return false;
  if (!parse_find_time_kind(option[6])) return false;
  return option[7] == 't' || parse_find_time_kind(option[7]).has_value();
}

auto filetime_ticks(FILETIME ft) -> long long {
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  if (value.QuadPart >
      static_cast<ULONGLONG>(std::numeric_limits<long long>::max())) {
    return std::numeric_limits<long long>::max();
  }
  return static_cast<long long>(value.QuadPart);
}

auto local_system_time_to_filetime_ticks(const SYSTEMTIME& local_time)
    -> std::optional<long long> {
  SYSTEMTIME utc_time{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &local_time, &utc_time)) {
    return std::nullopt;
  }

  FILETIME file_time{};
  if (!SystemTimeToFileTime(&utc_time, &file_time)) return std::nullopt;
  return filetime_ticks(file_time);
}

// [#1311] Local calendar shift for -newermt month/year offsets:
// Jan 31 - 1 month -> Dec 31, Mar 31 - 1 month -> Feb 28/29 (day clamp).
auto shift_local_calendar(long long now_ticks, std::string_view unit,
                          long long delta) -> std::optional<long long> {
  FILETIME filetime{.dwLowDateTime =
                        static_cast<DWORD>(now_ticks & 0xFFFFFFFFLL),
                    .dwHighDateTime = static_cast<DWORD>(now_ticks >> 32)};
  SYSTEMTIME utc{};
  if (FileTimeToSystemTime(&filetime, &utc) == 0) return std::nullopt;
  SYSTEMTIME local{};
  if (SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local) == 0) {
    return std::nullopt;
  }
  auto days_in_month = [](int year, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
    if (month != 2) return kDays[month - 1];
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  };
  int year = local.wYear;
  int month = local.wMonth;
  if (unit == "month") {
    long long total =
        static_cast<long long>(year) * 12 + (month - 1) + delta;
    long long new_year = total >= 0 ? total / 12 : -((-total + 11) / 12);
    int new_month = static_cast<int>(total - new_year * 12) + 1;
    year = static_cast<int>(new_year);
    month = new_month;
  } else {
    year = static_cast<int>(static_cast<long long>(year) + delta);
  }
  int day = std::min<int>(local.wDay, days_in_month(year, month));
  SYSTEMTIME shifted{.wYear = static_cast<WORD>(year),
                     .wMonth = static_cast<WORD>(month),
                     .wDay = static_cast<WORD>(day),
                     .wHour = local.wHour,
                     .wMinute = local.wMinute,
                     .wSecond = local.wSecond,
                     .wMilliseconds = local.wMilliseconds};
  return local_system_time_to_filetime_ticks(shifted);
}

// [#1311] Subset of GNU get_date for -newermt: now/today/tomorrow/
// yesterday, "last"/"next" <unit>, "<n>|a <unit>[s] ago", and month-name
// dates ("Jan 1 2025", "1 Jan 2025"). Day words are offsets from now,
// not midnights. Not covered (GNU accepts, out of scope here): compound
// expressions ("2 days 3 hours ago"), fractional magnitudes, 2-digit
// years, and weekday names.
auto parse_relative_time_ticks(std::string_view text)
    -> std::optional<long long> {
  std::string lowered;
  for (char c : text) {
    lowered +=
        static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  std::vector<std::string_view> words;
  size_t pos = 0;
  while (pos < lowered.size()) {
    while (pos < lowered.size() &&
           std::isspace(static_cast<unsigned char>(lowered[pos])) != 0) {
      ++pos;
    }
    size_t start = pos;
    while (pos < lowered.size() &&
           std::isspace(static_cast<unsigned char>(lowered[pos])) == 0) {
      ++pos;
    }
    if (pos > start) words.emplace_back(lowered.data() + start, pos - start);
  }
  if (words.empty()) return std::nullopt;

  auto unit_seconds = [](std::string_view unit) -> std::optional<long long> {
    if (unit == "second" || unit == "sec") return 1LL;
    if (unit == "minute" || unit == "min") return 60LL;
    if (unit == "hour") return 3600LL;
    if (unit == "day") return 86400LL;
    if (unit == "week") return 604800LL;
    if (unit == "fortnight") return 1209600LL;
    return std::nullopt;
  };
  auto strip_plural = [](std::string_view unit) {
    if (!unit.empty() && unit.back() == 's') unit.remove_suffix(1);
    return unit;
  };

  FILETIME now_filetime{};
  GetSystemTimeAsFileTime(&now_filetime);
  long long now_ticks = filetime_ticks(now_filetime);
  constexpr long long kTicksPerSecond = 10000000LL;

  if (words.size() == 1) {
    if (words[0] == "now" || words[0] == "today") return now_ticks;
    if (words[0] == "tomorrow") {
      return now_ticks + 86400LL * kTicksPerSecond;
    }
    if (words[0] == "yesterday") {
      return now_ticks - 86400LL * kTicksPerSecond;
    }
    return std::nullopt;
  }

  if (words.size() == 2 && (words[0] == "last" || words[0] == "next")) {
    std::string_view unit = strip_plural(words[1]);
    long long sign = words[0] == "last" ? -1 : 1;
    if (unit == "month" || unit == "year") {
      return shift_local_calendar(now_ticks, unit, sign);
    }
    auto seconds = unit_seconds(unit);
    if (!seconds) return std::nullopt;
    return now_ticks + sign * *seconds * kTicksPerSecond;
  }

  if (words.size() == 3 && words[2] == "ago") {
    long long magnitude = 0;
    if (words[0] == "a") {
      magnitude = 1;
    } else {
      auto [ptr, ec] = std::from_chars(words[0].data(),
                                       words[0].data() + words[0].size(),
                                       magnitude);
      if (ec != std::errc() || ptr != words[0].data() + words[0].size() ||
          magnitude < 0) {
        return std::nullopt;
      }
    }
    std::string_view unit = strip_plural(words[1]);
    if (unit == "month" || unit == "year") {
      return shift_local_calendar(now_ticks, unit, -magnitude);
    }
    auto seconds = unit_seconds(unit);
    if (!seconds) return std::nullopt;
    return now_ticks - magnitude * *seconds * kTicksPerSecond;
  }

  if (words.size() == 3) {
    static constexpr std::array<std::string_view, 12> kMonths = {
        "january", "february", "march",     "april",   "may",      "june",
        "july",    "august",   "september", "october", "november", "december"};
    int month = 0;
    size_t month_idx = words.size();
    for (size_t i = 0; i < words.size(); ++i) {
      for (size_t m = 0; m < kMonths.size(); ++m) {
        if (words[i] == kMonths[m] || words[i] == kMonths[m].substr(0, 3)) {
          month = static_cast<int>(m) + 1;
          month_idx = i;
        }
      }
    }
    if (month == 0) return std::nullopt;
    auto parse_number = [](std::string_view word) -> std::optional<long long> {
      long long value = 0;
      auto [ptr, ec] =
          std::from_chars(word.data(), word.data() + word.size(), value);
      if (ec != std::errc() || ptr != word.data() + word.size()) {
        return std::nullopt;
      }
      return value;
    };
    long long day = 0;
    long long year = 0;
    for (size_t i = 0; i < words.size(); ++i) {
      if (i == month_idx) continue;
      auto number = parse_number(words[i]);
      if (!number) return std::nullopt;
      if (*number >= 1000) {
        year = *number;
      } else if (*number >= 1 && *number <= 31 && day == 0) {
        day = *number;
      } else {
        return std::nullopt;
      }
    }
    if (day == 0 || year == 0) return std::nullopt;
    SYSTEMTIME st{.wYear = static_cast<WORD>(year),
                  .wMonth = static_cast<WORD>(month),
                  .wDay = static_cast<WORD>(day)};
    return local_system_time_to_filetime_ticks(st);
  }

  return std::nullopt;
}

auto parse_literal_time_ticks(std::string_view text)
    -> std::optional<long long> {
  auto trim = [](std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
      value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
      value.remove_suffix(1);
    }
    return value;
  };

  text = trim(text);
  if (text.empty()) return std::nullopt;

  // [#1311] GNU -newermt parses via get_date: relative expressions,
  // @epoch, and month-name dates are all valid. This is the subset the
  // issue needs; day words are offsets from now (today == now, tomorrow
  // == now + 1 day, yesterday == now - 1 day), 'a' counts as 1, 'an' is
  // rejected by GNU and is rejected here too, and 'midnight'/'noon' are
  // rejected by GNU's -newermt and are not added. Month/year offsets use
  // local calendar arithmetic with day clamping (Jan 31 - 1 month ->
  // Dec 31; Mar 31 - 1 month -> Feb 28/29).
  if (auto relative = parse_relative_time_ticks(text)) return *relative;

  if (text.front() == '@') {
    long long seconds = 0;
    auto numeric = text.substr(1);
    auto [ptr, ec] = std::from_chars(numeric.data(),
                                     numeric.data() + numeric.size(), seconds);
    if (ec == std::errc() && ptr == numeric.data() + numeric.size()) {
      constexpr long long kUnixToWindowsEpochSeconds = 11644473600LL;
      return (seconds + kUnixToWindowsEpochSeconds) * 10000000LL;
    }
  }

  std::string normalized(text);
  std::replace(normalized.begin(), normalized.end(), 'T', ' ');
  std::replace(normalized.begin(), normalized.end(), '/', '-');

  const std::array formats = {"%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M",
                              "%Y-%m-%d"};
  for (const char* format : formats) {
    std::tm tm{};
    tm.tm_isdst = -1;
    std::istringstream stream(normalized);
    stream >> std::get_time(&tm, format);
    if (stream.fail()) continue;
    stream >> std::ws;
    if (!stream.eof()) continue;

    SYSTEMTIME st{.wYear = static_cast<WORD>(tm.tm_year + 1900),
                  .wMonth = static_cast<WORD>(tm.tm_mon + 1),
                  .wDay = static_cast<WORD>(tm.tm_mday),
                  .wHour = static_cast<WORD>(tm.tm_hour),
                  .wMinute = static_cast<WORD>(tm.tm_min),
                  .wSecond = static_cast<WORD>(tm.tm_sec)};
    return local_system_time_to_filetime_ticks(st);
  }

  return std::nullopt;
}

constexpr std::array<std::string_view, 13> kFindRegexTypeNames = {
    "findutils-default",
    "awk",
    "ed",
    "egrep",
    "emacs",
    "gnu-awk",
    "grep",
    "posix-awk",
    "posix-basic",
    "posix-egrep",
    "posix-extended",
    "posix-minimal-basic",
    "sed"};

auto find_regex_type_names() -> std::string {
  std::string names;
  for (std::string_view name : kFindRegexTypeNames) {
    if (!names.empty()) names += ", ";
    names += name;
  }
  return names;
}

// [GNU] -regextype (findutils-4.10.0/find/parser.c, lib/regextype.c).
// findutils-default (and "emacs") use EMACS regex syntax: the characters
// `+ ? | ( )` are LITERAL unless escaped, `\|` is alternation, `\+` and
// `\?` are postfix repetition operators and `\(...\)` groups.  The local
// regex engine only exposes Basic and Extended syntax, so the observable
// EMACS subset is emulated by translating the pattern to ERE before
// matching: unescaped operators are escaped (made literal) and escaped
// operators are unescaped (made active).
enum class FindRegexDialect { EmacsTranslated, Basic, Extended };

auto translate_emacs_regex_to_ere(std::string_view pattern) -> std::string {
  std::string out;
  out.reserve(pattern.size());
  for (size_t i = 0; i < pattern.size(); ++i) {
    char ch = pattern[i];
    if (ch == '\\' && i + 1 < pattern.size()) {
      char next = pattern[++i];
      switch (next) {
        case '|':
        case '(':
        case ')':
        case '+':
        case '?':
          out.push_back(next);
          break;
        default:
          out.push_back('\\');
          out.push_back(next);
          break;
      }
      continue;
    }
    switch (ch) {
      case '+':
      case '?':
      case '|':
      case '(':
      case ')':
        out.push_back('\\');
        break;
      default:
        break;
    }
    out.push_back(ch);
  }
  return out;
}

auto parse_find_regex_syntax(std::string_view type)
    -> cp::Result<FindRegexDialect> {
  // GNU findutils maps -regextype through lib/regextype.c.  The local regex
  // engine currently exposes BRE and ERE syntax classes, so accepted GNU names
  // are folded into the closest supported class while staying positional.
  if (type == "awk" || type == "egrep" || type == "gnu-awk" ||
      type == "posix-awk" || type == "posix-egrep" ||
      type == "posix-extended") {
    return FindRegexDialect::Extended;
  }
  if (type == "emacs" || type == "findutils-default") {
    return FindRegexDialect::EmacsTranslated;
  }
  if (type == "ed" || type == "grep" || type == "posix-basic" ||
      type == "posix-minimal-basic" || type == "sed") {
    return FindRegexDialect::Basic;
  }

  return std::unexpected("Unknown regular expression type '" +
                         std::string(type) +
                         "'; valid types are: " + find_regex_type_names());
}

auto parse_regex(FindRegexDialect dialect, std::string_view pattern,
                 bool case_insensitive) -> cp::Result<portable_regex::Pattern> {
  auto syntax = portable_regex::Syntax::Extended;
  std::string translated(pattern);
  if (dialect == FindRegexDialect::EmacsTranslated) {
    translated = translate_emacs_regex_to_ere(pattern);
  } else if (dialect == FindRegexDialect::Basic) {
    syntax = portable_regex::Syntax::Basic;
  }
  auto parsed = portable_regex::compile(syntax, translated, case_insensitive);
  if (!parsed) {
    return std::unexpected("invalid regular expression");
  }
  return std::move(parsed.pattern);
}

auto reference_write_time(std::string_view path)
    -> cp::Result<std::filesystem::file_time_type> {
  std::error_code ec;
  // #1288: the -newer reference is a user operand: normalize it at the
  // operand boundary and read the extended spelling, or a reference
  // past MAX_PATH (or spelled with '/' separators) reads as missing.
  const auto operand = native_path::make_api_path_operand(path);
  auto t = std::filesystem::last_write_time(
      std::filesystem::path(operand.extended), ec);
  if (ec) return std::unexpected("cannot read reference file for -newer");
  return t;
}

auto numeric_matches(const NumericPredicate& pred, long long actual) -> bool {
  switch (pred.comparison) {
    case NumericComparison::Exact:
      return actual == pred.value;
    case NumericComparison::GreaterThan:
      return actual > pred.value;
    case NumericComparison::LessThan:
      return actual < pred.value;
  }
  return false;
}

auto numeric_matches_unsigned(const NumericPredicate& pred,
                              unsigned long long actual) -> bool {
  const auto max_signed =
      static_cast<unsigned long long>(std::numeric_limits<long long>::max());
  if (actual > max_signed) {
    switch (pred.comparison) {
      case NumericComparison::Exact:
        return false;
      case NumericComparison::GreaterThan:
        return true;
      case NumericComparison::LessThan:
        return false;
    }
  }
  return numeric_matches(pred, static_cast<long long>(actual));
}

auto is_directory_reparse_point(const std::filesystem::directory_entry& e)
    -> bool;

// [#1377] GNU find accepts a comma-separated list of type letters for
// -type/-xtype (findutils parser.c, parse_type): any one matching letter
// is enough.  Validation happens at parse time (validate_type_argument).
auto type_letter_matches(const std::filesystem::directory_entry& e,
                         std::string_view type) -> bool {
  std::error_code ec;
  bool link_like = (e.is_symlink(ec) && !ec) || is_directory_reparse_point(e);
  ec.clear();

  if (type == "f") {
    return e.is_regular_file(ec) && !ec &&
           !native_path::is_winux_fifo_w(e.path().wstring());
  }
  if (type == "d") return e.is_directory(ec) && !ec && !link_like;
  if (type == "l") return link_like;
  // WinuxCmd fifo marker (#1038): on-disk marker file counts as 'p'.
  if (type == "p") return native_path::is_winux_fifo_w(e.path().wstring());
  if (type == "b") return e.is_block_file(ec) && !ec && !link_like;
  if (type == "c") return e.is_character_file(ec) && !ec && !link_like;
  if (type == "s") return e.is_socket(ec) && !ec && !link_like;

  return false;
}

auto type_matches(const std::filesystem::directory_entry& e,
                  std::string_view type) -> bool {
  if (type.empty()) return true;
  size_t start = 0;
  while (start <= type.size()) {
    size_t comma = type.find(',', start);
    if (comma == std::string_view::npos) comma = type.size();
    if (type_letter_matches(e, type.substr(start, comma - start))) {
      return true;
    }
    if (comma == type.size()) break;
    start = comma + 1;
  }
  return false;
}

auto file_type_letter_matches(std::filesystem::file_type file_type,
                              bool link_like, const std::filesystem::path& p,
                              std::string_view type) -> bool {
  if (type == "l") {
    return link_like || file_type == std::filesystem::file_type::symlink;
  }
  if (type == "d") {
    return file_type == std::filesystem::file_type::directory && !link_like;
  }
  if (type == "f") {
    return file_type == std::filesystem::file_type::regular && !link_like &&
           !native_path::is_winux_fifo_w(p.wstring());
  }
  // WinuxCmd fifo marker (#1038).
  if (type == "p") return native_path::is_winux_fifo_w(p.wstring());
  // [#1377] Device and socket letters: a type that cannot occur on this
  // platform simply matches nothing, like GNU.
  if (type == "b") {
    return file_type == std::filesystem::file_type::block && !link_like;
  }
  if (type == "c") {
    return file_type == std::filesystem::file_type::character && !link_like;
  }
  if (type == "s") {
    return file_type == std::filesystem::file_type::socket && !link_like;
  }
  return false;
}

auto file_type_matches(std::filesystem::file_type file_type, bool link_like,
                       const std::filesystem::path& p, std::string_view type)
    -> bool {
  if (type.empty()) return true;
  size_t start = 0;
  while (start <= type.size()) {
    size_t comma = type.find(',', start);
    if (comma == std::string_view::npos) comma = type.size();
    if (file_type_letter_matches(file_type, link_like, p,
                                 type.substr(start, comma - start))) {
      return true;
    }
    if (comma == type.size()) break;
    start = comma + 1;
  }
  return false;
}
auto self_type_matches(const std::filesystem::path& p,
                       const std::filesystem::directory_entry& e,
                       std::string_view type) -> bool {
  std::error_code ec;
  auto status = std::filesystem::symlink_status(p, ec);
  if (ec) return false;
  bool link_like = status.type() == std::filesystem::file_type::symlink ||
                   is_directory_reparse_point(e);
  return file_type_matches(status.type(), link_like, p, type);
}
auto target_type_matches(const std::filesystem::path& p, std::string_view type)
    -> std::optional<bool> {
  std::error_code ec;
  auto status = std::filesystem::status(p, ec);
  if (ec) return std::nullopt;
  return file_type_matches(status.type(), false, p, type);
}
auto xtype_matches(const std::filesystem::path& p,
                   const std::filesystem::directory_entry& e,
                   std::string_view type, const Config& cfg,
                   const std::filesystem::path& root) -> bool {
  bool command_line_root = p.lexically_normal() == root.lexically_normal();
  bool normally_follows =
      cfg.follow_symlinks || (cfg.follow_arg_symlinks && command_line_root);
  if (normally_follows) {
    return self_type_matches(p, e, type);
  }
  if (auto target = target_type_matches(p, type)) {
    return *target;
  }
  return self_type_matches(p, e, type);
}
auto is_directory_reparse_point(const std::filesystem::directory_entry& e)
    -> bool {
  const auto path = e.path().wstring();
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return false;

  return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

auto permission_mode(const std::filesystem::path& p,
                     const std::filesystem::directory_entry& e) -> unsigned;

auto win32_file_identity(const std::filesystem::path& p)
    -> std::optional<FileIdentity>;
auto win32_file_time_ticks(const std::filesystem::path& p,
                           FindFileTimeKind kind) -> std::optional<long long>;
auto win32_file_index_value(const std::filesystem::path& p)
    -> std::optional<unsigned long long>;
auto win32_hard_link_count(const std::filesystem::path& p)
    -> unsigned long long;
auto owner_matches(const std::filesystem::path& p, std::string_view expected)
    -> bool;
auto group_matches(const std::filesystem::path& p, std::string_view expected)
    -> bool;
auto owner_id_matches(const std::filesystem::path& p,
                      const NumericPredicate& expected) -> bool;
auto group_id_matches(const std::filesystem::path& p,
                      const NumericPredicate& expected) -> bool;

auto parse_newerxy_reference(std::string_view option, std::string_view value)
    -> cp::Result<long long> {
  if (!is_newerxy_option(option)) {
    return std::unexpected("invalid -newerXY predicate");
  }

  if (option[7] == 't') {
    auto literal = parse_literal_time_ticks(value);
    if (!literal) {
      return std::unexpected("cannot parse literal time for " +
                             std::string(option));
    }
    return *literal;
  }

  auto reference_kind = parse_find_time_kind(option[7]);
  if (!reference_kind) {
    return std::unexpected("invalid reference time selector for " +
                           std::string(option));
  }
  auto ticks = win32_file_time_ticks(
      std::filesystem::path(utf8_to_wstring(std::string(value))),
      *reference_kind);
  if (!ticks) {
    return std::unexpected("cannot read reference file for " +
                           std::string(option));
  }
  return *ticks;
}

auto is_unsupported_used(const CommandContext<FIND_OPTIONS.size()>& ctx)
    -> std::optional<std::string> {
  return std::nullopt;
}

// [#1377] GNU -type/-xtype argument validation (findutils parser.c,
// parse_type, probed against findutils 4.9.0): the argument is a
// comma-separated list of single letters from "bcdpfls"; "D" is rejected
// with the doors diagnostic, a run of valid letters without commas is
// rejected with the "Must separate" diagnostic, and anything else is an
// "Unknown argument".  Returns the GNU error text (without the "find: "
// prefix) or std::nullopt when the argument is valid.
auto validate_type_argument(std::string_view option, std::string_view value)
    -> std::optional<std::string> {
  if (value.empty()) {
    return "Arguments to " + std::string(option) +
           " should contain at least one letter";
  }
  size_t start = 0;
  while (true) {
    size_t comma = value.find(',', start);
    bool last = comma == std::string_view::npos;
    std::string_view element =
        value.substr(start, (last ? value.size() : comma) - start);
    if (element.empty()) {
      if (last && start > 0) {
        return "Last file type in list argument to " + std::string(option) +
               " is missing, i.e., list is ending on: ','";
      }
      return "Unknown argument to " + std::string(option) + ": ,";
    }
    if (element.size() > 1) {
      bool all_type_letters =
          std::all_of(element.begin(), element.end(), [](char ch) {
            return ch == 'b' || ch == 'c' || ch == 'd' || ch == 'p' ||
                   ch == 'f' || ch == 'l' || ch == 's' || ch == 'D';
          });
      if (all_type_letters) {
        return "Must separate multiple arguments to " + std::string(option) +
               " using: ','";
      }
      return "Unknown argument to " + std::string(option) + ": " +
             std::string(element);
    }
    char letter = element[0];
    if (letter == 'D') {
      return std::string(option) +
             " D is not supported because Solaris doors are not supported "
             "on the platform find was compiled on.";
    }
    if (letter != 'b' && letter != 'c' && letter != 'd' && letter != 'p' &&
        letter != 'f' && letter != 'l' && letter != 's') {
      return "Unknown argument to " + std::string(option) + ": " +
             std::string(element);
    }
    if (last) break;
    start = comma + 1;
  }
  return std::nullopt;
}

auto is_path_option(std::string_view arg) -> bool {
  return arg == "-name" || arg == "-iname" || arg == "-path" ||
         arg == "-ipath" || arg == "-wholename" || arg == "-iwholename" ||
         arg == "-regex" || arg == "-iregex" || arg == "-type" ||
         arg == "-xtype" || arg == "-perm" || arg == "-readable" ||
         arg == "-writable" || arg == "-executable" || arg == "-inum" ||
         arg == "-links" || arg == "-user" || arg == "-nouser" ||
         arg == "-nogroup" || arg == "-group" || arg == "-uid" ||
         arg == "-gid" || arg == "-size" || arg == "-empty" ||
         arg == "-mtime" || arg == "-mmin" || arg == "-used" ||
         arg == "-anewer" || arg == "-cnewer" || arg == "-fstype" ||
         arg == "-lname" || arg == "-ilname" || arg == "-newer" ||
         is_newerxy_option(arg) || arg == "-samefile" ||
         arg == "-files0-from" || arg == "-mindepth" || arg == "-maxdepth" ||
         arg == "-print" || arg == "-print0" || arg == "-fprint" ||
         arg == "-fprint0" || arg == "-delete" || arg == "-exec" ||
         arg == "-execdir" || arg == "-ok" || arg == "-okdir" || arg == "-ls" ||
         arg == "-fls" || arg == "-printf" || arg == "-fprintf" ||
         arg == "-prune" || arg == "-quit" || arg == "-true" ||
         arg == "-false" || arg == "-depth" || arg == "-d" ||
         arg == "-follow" || arg == "-mount" || arg == "-xdev" ||
         arg == "-noleaf" || arg == "-daystart" || arg == "-regextype" ||
         arg == "-O" || arg == "!" || arg == "-not" || arg == "-a" ||
         arg == "-and" || arg == "-o" || arg == "-or";
}

auto parse_roots(std::span<const std::string_view> args)
    -> SmallVector<std::string, 64> {
  SmallVector<std::string, 64> roots;
  for (size_t i = 0; i < args.size(); ++i) {
    auto arg = args[i];
    if (roots.empty() &&
        (arg == "-L" || arg == "-P" || arg == "-H" || arg == "-follow" ||
         arg == "-mount" || arg == "-xdev" || arg == "-noleaf" ||
         arg == "-daystart" || arg == "-d")) {
      continue;
    }
    if (roots.empty() && arg == "-O") {
      ++i;
      continue;
    }
    if (roots.empty() && arg == "-regextype") {
      ++i;
      continue;
    }
    if (roots.empty() && arg == "-files0-from") {
      ++i;
      continue;
    }
    // [#1380] The remaining leading global options must be skipped the
    // same way expression_start_index skips them; otherwise "-D tree"
    // ends the root scan here and the real start point that follows is
    // never collected (the traversal silently runs on ".").
    if (roots.empty() &&
        (arg == "-nowarn" || arg == "-warn" ||
         arg == "-ignore_readdir_race" || arg == "-noignore_readdir_race")) {
      continue;
    }
    if (roots.empty() && (arg == "-D" || arg == "--debug")) {
      ++i;
      continue;
    }
    if (roots.empty() && arg.size() > 2 && arg[0] == '-' && arg[1] == 'O' &&
        std::all_of(arg.begin() + 2, arg.end(), [](char ch) {
          return std::isdigit(static_cast<unsigned char>(ch)) != 0;
        })) {
      continue;
    }
    // [#1317] A leading "--" ends option scanning (GNU findutils
    // parser.c): skip it while no root has been seen yet. Once a root
    // exists, "--" is an expression token, so stop the root scan and let
    // the expression parser reject it as an unknown predicate.
    if (arg == "--") {
      if (roots.empty()) continue;
      break;
    }
    // [#1317] Any other dash-led token that is not a leading global option
    // is an expression token, never a root: stop the scan so the
    // expression parser reports it (GNU: unknown predicate, e.g. for
    // "-maxdepth=1") instead of treating it as a start point. A lone "-"
    // stays a root (GNU treats "-" as a path).
    if (arg.size() > 1 && arg[0] == '-') {
      break;
    }
    if (is_path_option(arg) || arg == "!" || arg == "(" || arg == ")" ||
        arg == ",") {
      break;
    }
    // [#1170] Start points go through the same operand boundary as every
    // other tool's path operands: POSIX drive forms (/c/..., /d/...,
    // /cygdrive/c/...) resolve to their native spellings, matching msys
    // find semantics where the tool's own POSIX layer handles the start
    // point. Native and relative forms pass through unchanged.
    roots.emplace_back(native_path::normalize_api_operand(arg));
  }
  if (roots.empty()) roots.emplace_back(".");
  return roots;
}

auto parse_exec_actions(std::span<const std::string_view> args)
    -> cp::Result<std::vector<ExecAction>> {
  std::vector<ExecAction> actions;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] != "-exec" && args[i] != "-execdir" && args[i] != "-ok" &&
        args[i] != "-okdir")
      continue;

    ExecAction action;
    action.prompt = args[i] == "-ok" || args[i] == "-okdir";
    action.execdir = args[i] == "-execdir" || args[i] == "-okdir";

    std::vector<std::string> command_words;
    std::optional<std::string_view> terminator;
    size_t j = i + 1;
    for (; j < args.size(); ++j) {
      if (args[j] == ";" || args[j] == "+") {
        terminator = args[j];
        break;
      }
      command_words.emplace_back(args[j]);
    }

    if (!terminator) return std::unexpected("missing -exec/-ok terminator");
    if (command_words.empty()) {
      return std::unexpected("missing command after -exec/-ok");
    }

    action.aggregate = *terminator == "+";
    if (action.aggregate) {
      if (command_words.back() != "{}") {
        return std::unexpected("-exec/-ok ... + requires {} before +");
      }
      command_words.pop_back();
      if (command_words.empty()) {
        return std::unexpected("missing command after -exec/-ok");
      }
    }

    action.command = command_words.front();
    action.args.assign(command_words.begin() + 1, command_words.end());
    actions.push_back(std::move(action));
    i = j;
  }

  return actions;
}

auto resolve_symlink_mode(const CommandContext<FIND_OPTIONS.size()>& ctx)
    -> SymlinkMode {
  SymlinkMode mode = SymlinkMode::Never;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= FIND_OPTIONS.size()) continue;
    const auto& meta = FIND_OPTIONS[occurrence.index];

    if (meta.short_name == "-L" || meta.short_name == "-follow") {
      mode = SymlinkMode::All;
    } else if (meta.short_name == "-H") {
      mode = SymlinkMode::CommandLineOnly;
    } else if (meta.short_name == "-P") {
      mode = SymlinkMode::Never;
    }
  }

  return mode;
}

auto expression_start_index(std::span<const std::string_view> args) -> size_t {
  bool roots_seen = false;
  for (size_t i = 0; i < args.size(); ++i) {
    auto arg = args[i];
    if (!roots_seen &&
        (arg == "-L" || arg == "-P" || arg == "-H" || arg == "-follow" ||
         arg == "-mount" || arg == "-xdev" || arg == "-noleaf" ||
         arg == "-daystart" || arg == "-d" || arg == "-nowarn" ||
         arg == "-warn" || arg == "-ignore_readdir_race" ||
         arg == "-noignore_readdir_race")) {
      continue;
    }
    if (!roots_seen && arg == "-O" && i + 1 < args.size()) {
      ++i;
      continue;
    }
    if (!roots_seen && arg == "-regextype" && i + 1 < args.size()) {
      ++i;
      continue;
    }
    if (!roots_seen && (arg == "-D" || arg == "--debug") &&
        i + 1 < args.size()) {
      ++i;
      continue;
    }
    if (!roots_seen && arg == "-files0-from" && i + 1 < args.size()) {
      ++i;
      continue;
    }
    if (!roots_seen && arg.size() > 2 && arg[0] == '-' && arg[1] == 'O' &&
        std::all_of(arg.begin() + 2, arg.end(), [](char ch) {
          return std::isdigit(static_cast<unsigned char>(ch)) != 0;
        })) {
      continue;
    }
    // [#1317] "--" ends option scanning only before the first root; after
    // a root it starts the expression (GNU rejects it there as an unknown
    // predicate).
    if (arg == "--") {
      if (!roots_seen) continue;
      return i;
    }
    // [#1317] Any other dash-led token starts the expression, so the
    // parser reports unknown predicates like "-maxdepth=1" instead of
    // swallowing the token as a root. A lone "-" stays a root.
    if (arg.size() > 1 && arg[0] == '-') {
      return i;
    }
    if (is_path_option(arg) || arg == "(" || arg == ")" || arg == ",") {
      return i;
    }
    roots_seen = true;
  }
  return args.size();
}

auto make_expr(ExprKind kind) -> std::unique_ptr<ExprNode> {
  auto node = std::make_unique<ExprNode>();
  node->kind = kind;
  return node;
}

class ExpressionParser {
 public:
  explicit ExpressionParser(std::span<const std::string_view> tokens,
                            FindRegexDialect regex_dialect)
      : tokens_(tokens), regex_dialect_(regex_dialect) {}

  auto parse() -> cp::Result<std::unique_ptr<ExprNode>> {
    if (at_end()) {
      return make_expr(ExprKind::Always);
    }

    auto expr = parse_comma();
    if (!expr) return std::unexpected(expr.error());
    if (!at_end()) {
      return std::unexpected("invalid find expression");
    }
    return expr;
  }

 private:
  std::span<const std::string_view> tokens_;
  size_t pos_ = 0;
  size_t exec_index_ = 0;
  FindRegexDialect regex_dialect_ = FindRegexDialect::EmacsTranslated;

  auto at_end() const -> bool { return pos_ >= tokens_.size(); }

  auto peek() const -> std::string_view {
    return at_end() ? std::string_view{} : tokens_[pos_];
  }

  auto consume() -> std::string_view { return tokens_[pos_++]; }

  auto match(std::string_view token) -> bool {
    if (peek() != token) return false;
    ++pos_;
    return true;
  }

  static auto is_or(std::string_view token) -> bool {
    return token == "-o" || token == "-or";
  }

  static auto is_and(std::string_view token) -> bool {
    return token == "-a" || token == "-and";
  }

  static auto starts_primary(std::string_view token) -> bool {
    return token == "!" || token == "-not" || token == "(" ||
           token == "-name" || token == "-iname" || token == "-path" ||
           token == "-ipath" || token == "-wholename" ||
           token == "-iwholename" || token == "-regex" || token == "-iregex" ||
           token == "-type" || token == "-xtype" || token == "-perm" ||
           token == "-readable" || token == "-writable" ||
           token == "-executable" || token == "-inum" || token == "-links" ||
           token == "-user" || token == "-nouser" || token == "-group" ||
           token == "-nogroup" || token == "-uid" || token == "-gid" ||
           token == "-size" || token == "-empty" || token == "-amin" ||
           token == "-atime" || token == "-cmin" || token == "-ctime" ||
           token == "-mtime" || token == "-mmin" || token == "-used" ||
           token == "-newer" || token == "-anewer" || token == "-cnewer" ||
           is_newerxy_option(token) || token == "-samefile" ||
           token == "-fstype" || token == "-lname" || token == "-ilname" ||
           token == "-files0-from" || token == "-mindepth" ||
           token == "-maxdepth" || token == "-print" || token == "-print0" ||
           token == "-fprint" || token == "-fprint0" || token == "-printf" ||
           token == "-fprintf" || token == "-prune" || token == "-quit" ||
           token == "-true" || token == "-false" || token == "-depth" ||
           token == "-d" || token == "-follow" || token == "-mount" ||
           token == "-xdev" || token == "-noleaf" || token == "-daystart" ||
           token == "-regextype" || token == "-O" || token == "-delete" ||
           token == "-exec" || token == "-execdir" || token == "-ok" ||
           token == "-okdir" || token == "-ls" || token == "-fls" ||
           token == "-L" || token == "-P" || token == "-H" || token == "-D" ||
           token == "--debug" || token == "-nowarn" || token == "-warn" ||
           token == "-ignore_readdir_race" || token == "-noignore_readdir_race";
  }

  auto require_value(std::string_view option) -> cp::Result<std::string> {
    if (at_end()) {
      return std::unexpected(std::string("missing argument for ") +
                             std::string(option));
    }
    return std::string(consume());
  }

  auto parse_or() -> cp::Result<std::unique_ptr<ExprNode>> {
    auto left = parse_and();
    if (!left) return std::unexpected(left.error());

    while (!at_end() && is_or(peek())) {
      consume();
      auto right = parse_and();
      if (!right) return std::unexpected(right.error());

      auto node = make_expr(ExprKind::Or);
      node->left = std::move(*left);
      node->right = std::move(*right);
      left = std::move(node);
    }
    return left;
  }

  auto parse_comma() -> cp::Result<std::unique_ptr<ExprNode>> {
    auto left = parse_or();
    if (!left) return std::unexpected(left.error());

    while (!at_end() && peek() == ",") {
      consume();
      auto right = parse_or();
      if (!right) return std::unexpected(right.error());

      auto node = make_expr(ExprKind::Comma);
      node->left = std::move(*left);
      node->right = std::move(*right);
      left = std::move(node);
    }
    return left;
  }

  auto parse_and() -> cp::Result<std::unique_ptr<ExprNode>> {
    auto left = parse_not();
    if (!left) return std::unexpected(left.error());

    while (!at_end() && peek() != ")" && peek() != "," && !is_or(peek())) {
      if (is_and(peek())) {
        consume();
      } else if (!starts_primary(peek())) {
        // [#1317] GNU reports an unrecognized predicate by name.
        return std::unexpected("unknown predicate `" + std::string(peek()) +
                                "'");
      }

      auto right = parse_not();
      if (!right) return std::unexpected(right.error());

      auto node = make_expr(ExprKind::And);
      node->left = std::move(*left);
      node->right = std::move(*right);
      left = std::move(node);
    }
    return left;
  }

  auto parse_not() -> cp::Result<std::unique_ptr<ExprNode>> {
    if (match("!") || match("-not")) {
      auto operand = parse_not();
      if (!operand) return std::unexpected(operand.error());
      auto node = make_expr(ExprKind::Not);
      node->left = std::move(*operand);
      return node;
    }
    return parse_primary();
  }

  auto parse_primary() -> cp::Result<std::unique_ptr<ExprNode>> {
    if (at_end()) return std::unexpected("missing expression");

    if (match("(")) {
      auto expr = parse_comma();
      if (!expr) return std::unexpected(expr.error());
      if (!match(")")) return std::unexpected("missing closing parenthesis");
      return expr;
    }
    if (peek() == ")") return std::unexpected("unexpected closing parenthesis");

    auto option = consume();
    if (option == "-name" || option == "-iname" || option == "-path" ||
        option == "-ipath" || option == "-wholename" ||
        option == "-iwholename" || option == "-type" || option == "-xtype") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      ExprKind kind = ExprKind::Name;
      if (option == "-iname") kind = ExprKind::IName;
      if (option == "-path") kind = ExprKind::Path;
      if (option == "-ipath") kind = ExprKind::IPath;
      if (option == "-wholename") kind = ExprKind::Path;
      if (option == "-iwholename") kind = ExprKind::IPath;
      if (option == "-type" || option == "-xtype") {
        // [#1377] Full GNU letter set and diagnostics.
        if (auto error = validate_type_argument(option, *value)) {
          return std::unexpected(*error);
        }
        kind = option == "-type" ? ExprKind::Type : ExprKind::XType;
      }
      auto node = make_expr(kind);
      node->text = std::move(*value);
      return node;
    }

    if (option == "-perm") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_permission_predicate(*value);
      if (!parsed) return std::unexpected(parsed.error());
      auto node = make_expr(ExprKind::Perm);
      node->text = std::move(*value);
      node->permission = *parsed;
      return node;
    }

    if (option == "-readable" || option == "-writable" ||
        option == "-executable") {
      if (option == "-readable") return make_expr(ExprKind::Readable);
      if (option == "-writable") return make_expr(ExprKind::Writable);
      return make_expr(ExprKind::Executable);
    }

    if (option == "-inum" || option == "-links" || option == "-uid" ||
        option == "-gid") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_numeric_predicate(*value);
      if (!parsed) return std::unexpected(parsed.error());
      ExprKind kind = ExprKind::Inum;
      if (option == "-links") kind = ExprKind::Links;
      if (option == "-uid") kind = ExprKind::Uid;
      if (option == "-gid") kind = ExprKind::Gid;
      auto node = make_expr(kind);
      node->numeric = *parsed;
      node->text = std::move(*value);
      return node;
    }

    if (option == "-nouser" || option == "-nogroup") {
      return make_expr(option == "-nouser" ? ExprKind::NoUser
                                           : ExprKind::NoGroup);
    }

    if (option == "-lname" || option == "-ilname" || option == "-fstype") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      ExprKind kind = ExprKind::Lname;
      if (option == "-ilname") kind = ExprKind::ILname;
      if (option == "-fstype") kind = ExprKind::Fstype;
      auto node = make_expr(kind);
      node->text = std::move(*value);
      return node;
    }

    if (option == "-user" || option == "-group") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      // [#1313] GNU resolves a non-numeric name at parse time: an
      // unknown user/group is an error, not a never-matching predicate.
      // Numeric specs pass without a lookup (matching the fallback in
      // win32_account_matches).
      const bool all_digits =
          !value->empty() &&
          std::all_of(value->begin(), value->end(), [](char ch) {
            return std::isdigit(static_cast<unsigned char>(ch)) != 0;
          });
      if (!all_digits &&
          !win32_lookup_account(utf8_to_wstring(*value))) {
        return std::unexpected(
            "'" + *value + "' is not the name of " +
            (option == "-user" ? "a known user" : "an existing group"));
      }
      auto node =
          make_expr(option == "-user" ? ExprKind::User : ExprKind::Group);
      node->text = std::move(*value);
      return node;
    }

    if (option == "-regex" || option == "-iregex") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_regex(regex_dialect_, *value, option == "-iregex");
      if (!parsed) return std::unexpected(parsed.error());
      auto node =
          make_expr(option == "-regex" ? ExprKind::Regex : ExprKind::IRegex);
      node->text = std::move(*value);
      node->regex = std::move(*parsed);
      return node;
    }

    if (option == "-empty") {
      return make_expr(ExprKind::Empty);
    }

    if (option == "-size") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_size_predicate(*value);
      if (!parsed) return std::unexpected(parsed.error());
      auto node = make_expr(ExprKind::Size);
      node->size = *parsed;
      return node;
    }

    if (option == "-amin" || option == "-atime" || option == "-cmin" ||
        option == "-ctime" || option == "-mtime" || option == "-mmin" ||
        option == "-used") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_time_predicate(*value);
      if (!parsed) return std::unexpected(parsed.error());
      ExprKind kind = ExprKind::MMin;
      if (option == "-used") kind = ExprKind::Used;
      if (option == "-amin") kind = ExprKind::AMin;
      if (option == "-atime") kind = ExprKind::ATime;
      if (option == "-cmin") kind = ExprKind::CMin;
      if (option == "-ctime") kind = ExprKind::CTime;
      if (option == "-mtime") kind = ExprKind::MTime;
      auto node = make_expr(kind);
      node->time_pred = *parsed;
      return node;
    }

    if (option == "-anewer" || option == "-cnewer") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto reference =
          win32_file_time_ticks(std::filesystem::path(utf8_to_wstring(*value)),
                                option == "-anewer" ? FindFileTimeKind::Access
                                                    : FindFileTimeKind::Change);
      if (!reference)
        return std::unexpected("cannot read reference file for " +
                               std::string(option));
      auto node =
          make_expr(option == "-anewer" ? ExprKind::ANewer : ExprKind::CNewer);
      node->text = std::move(*value);
      node->reference_ticks = *reference;
      return node;
    }

    if (option == "-newer") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = reference_write_time(*value);
      if (!parsed) return std::unexpected(parsed.error());
      auto node = make_expr(ExprKind::Newer);
      node->text = std::move(*value);
      node->reference_time = *parsed;
      return node;
    }

    if (is_newerxy_option(option)) {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto reference = parse_newerxy_reference(option, *value);
      if (!reference) return std::unexpected(reference.error());
      auto candidate_kind = parse_find_time_kind(option[6]);
      if (!candidate_kind) {
        return std::unexpected("invalid candidate time selector for " +
                               std::string(option));
      }
      auto node = make_expr(ExprKind::NewerXY);
      node->text = std::move(*value);
      node->time_kind = *candidate_kind;
      node->reference_ticks = *reference;
      return node;
    }

    if (option == "-samefile") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto identity =
          win32_file_identity(std::filesystem::path(utf8_to_wstring(*value)));
      if (!identity) {
        return std::unexpected("cannot read reference file for -samefile");
      }
      auto node = make_expr(ExprKind::SameFile);
      node->text = std::move(*value);
      node->reference_identity = *identity;
      return node;
    }

    if (option == "-regextype") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_find_regex_syntax(*value);
      if (!parsed) return std::unexpected(parsed.error());
      regex_dialect_ = *parsed;
      return make_expr(ExprKind::Always);
    }

    if (option == "-mindepth" || option == "-maxdepth" || option == "-O" ||
        option == "-files0-from") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      return make_expr(ExprKind::Always);
    }

    if (option == "-true" || option == "-depth" || option == "-d" ||
        option == "-follow" || option == "-mount" || option == "-xdev" ||
        option == "-noleaf" || option == "-daystart") {
      return make_expr(ExprKind::Always);
    }

    if (option == "-false") {
      return make_expr(ExprKind::False);
    }

    if (option == "-printf" || option == "-fprintf") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto node =
          make_expr(option == "-printf" ? ExprKind::Printf : ExprKind::FPrintf);
      if (option == "-fprintf") {
        auto format = require_value(option);
        if (!format) return std::unexpected(format.error());
        node->text = std::move(*value);
        node->format_text = std::move(*format);
      } else {
        node->text = std::move(*value);
      }
      return node;
    }

    if (option == "-exec" || option == "-execdir" || option == "-ok" ||
        option == "-okdir") {
      bool found_terminator = false;
      while (!at_end()) {
        auto token = consume();
        if (token == ";" || token == "+") {
          found_terminator = true;
          break;
        }
      }
      if (!found_terminator) {
        return std::unexpected("missing -exec/-execdir/-ok terminator");
      }
      auto node =
          make_expr(option == "-okdir" ? ExprKind::OkDir : ExprKind::Exec);
      node->action_index = exec_index_++;
      return node;
    }

    if (option == "-ls" || option == "-fls") {
      auto node = make_expr(option == "-ls" ? ExprKind::Ls : ExprKind::Fls);
      if (option == "-fls") {
        auto value = require_value(option);
        if (!value) return std::unexpected(value.error());
        node->text = std::move(*value);
      }
      return node;
    }

    if (option == "-D" || option == "--debug" || option == "-nowarn" ||
        option == "-warn" || option == "-ignore_readdir_race" ||
        option == "-noignore_readdir_race") {
      if (option == "-D" || option == "--debug") {
        auto value = require_value(option);
        if (!value) return std::unexpected(value.error());
      }
      return make_expr(ExprKind::Always);
    }

    if (option == "-print") {
      return make_expr(ExprKind::Print);
    }

    if (option == "-print0") {
      return make_expr(ExprKind::Print0);
    }

    if (option == "-fprint" || option == "-fprint0") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto node =
          make_expr(option == "-fprint" ? ExprKind::FPrint : ExprKind::FPrint0);
      node->text = std::move(*value);
      return node;
    }

    if (option == "-quit") {
      return make_expr(ExprKind::Quit);
    }

    if (option == "-delete") {
      return make_expr(ExprKind::Delete);
    }

    if (option == "-prune") {
      return make_expr(ExprKind::Prune);
    }

    if (option == "-L" || option == "-P" || option == "-H") {
      return make_expr(ExprKind::Always);
    }

    if (is_and(option) || is_or(option)) {
      return std::unexpected("missing expression");
    }

    // [#1317] GNU reports an unrecognized predicate by name.
    return std::unexpected("unknown predicate `" + std::string(option) + "'");
  }
};

auto initial_regex_syntax(std::span<const std::string_view> raw_args,
                          size_t expression_start)
    -> cp::Result<FindRegexDialect> {
  // [GNU] findutils-4.10.0 defaults to the findutils-default (EMACS) regex
  // syntax (parser.c: check_type_arg), NOT ERE.
  auto syntax = FindRegexDialect::EmacsTranslated;
  for (size_t i = 0; i < expression_start && i < raw_args.size(); ++i) {
    if (raw_args[i] != "-regextype") continue;
    if (i + 1 >= raw_args.size()) {
      return std::unexpected("missing argument for -regextype");
    }
    auto parsed = parse_find_regex_syntax(raw_args[i + 1]);
    if (!parsed) return std::unexpected(parsed.error());
    syntax = *parsed;
    ++i;
  }
  return syntax;
}

auto parse_expression(std::span<const std::string_view> raw_args)
    -> cp::Result<std::unique_ptr<ExprNode>> {
  size_t start = expression_start_index(raw_args);
  auto syntax = initial_regex_syntax(raw_args, start);
  if (!syntax) return std::unexpected(syntax.error());
  ExpressionParser parser(raw_args.subspan(start), *syntax);
  return parser.parse();
}

auto prepare_find_output_file(Config& cfg, const std::string& path)
    -> cp::Result<void> {
  if (cfg.output_files.contains(path)) return {};
  // #1288: open the operand's extended spelling (the -fprint* target is
  // a user path: the raw spelling made "/d/out" land in "<cwd>\d\out"
  // and fails outright past MAX_PATH). The map stays keyed by the
  // original operand string.
  const auto operand = native_path::make_api_path_operand(path);
  auto out = std::make_unique<std::ofstream>(
      std::filesystem::path(operand.extended),
      std::ios::binary | std::ios::trunc);
  if (!out->is_open()) {
    return std::unexpected("cannot open output file " + path);
  }
  cfg.output_files.emplace(path, std::move(out));
  return {};
}

auto prepare_file_output_actions(const ExprNode& expr, Config& cfg)
    -> cp::Result<void> {
  if (expr.kind == ExprKind::FPrint || expr.kind == ExprKind::FPrint0 ||
      expr.kind == ExprKind::FPrintf || expr.kind == ExprKind::Fls) {
    if (auto ok = prepare_find_output_file(cfg, expr.text); !ok) {
      return std::unexpected(ok.error());
    }
  }
  if (expr.left) {
    if (auto ok = prepare_file_output_actions(*expr.left, cfg); !ok) {
      return std::unexpected(ok.error());
    }
  }
  if (expr.right) {
    if (auto ok = prepare_file_output_actions(*expr.right, cfg); !ok) {
      return std::unexpected(ok.error());
    }
  }
  return {};
}

auto load_roots_from_files0(std::string_view source)
    -> cp::Result<SmallVector<std::string, 64>> {
  std::string data;
  if (source == "-") {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    data = buffer.str();
  } else {
    std::ifstream in(std::filesystem::path(utf8_to_wstring(
                         native_path::normalize_api_operand(source))),
                     std::ios::binary);
    if (!in.is_open()) {
      return std::unexpected("cannot open -files0-from file '" +
                             std::string(source) + "'");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    data = buffer.str();
  }

  SmallVector<std::string, 64> roots;
  size_t start = 0;
  while (start <= data.size()) {
    size_t end = data.find('\0', start);
    if (end == std::string::npos) end = data.size();
    if (end > start) {
      // [#1170] -files0-from entries are start points too: apply the same
      // POSIX-form operand resolution as command-line roots.
      roots.emplace_back(
          native_path::normalize_api_operand(data.substr(start, end - start)));
    }
    if (end == data.size()) break;
    start = end + 1;
  }
  return roots;
}

auto build_config(const CommandContext<FIND_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (auto u = is_unsupported_used(ctx); u.has_value()) {
    return std::unexpected(*u);
  }

  cfg.name_pattern = ctx.get<std::string>("-name", "");
  cfg.iname_pattern = ctx.get<std::string>("-iname", "");
  cfg.path_pattern = ctx.get<std::string>("-path", "");
  cfg.ipath_pattern = ctx.get<std::string>("-ipath", "");
  cfg.type_filter = ctx.get<std::string>("-type", "");
  cfg.empty_filter = ctx.get<bool>("-empty", false);
  cfg.mindepth = ctx.get<int>("-mindepth", 0);
  cfg.maxdepth = ctx.get<int>("-maxdepth", std::numeric_limits<int>::max());
  cfg.files0_from = ctx.get<std::string>("-files0-from", "");
  cfg.debug_category = ctx.get<std::string>("-D", "");
  cfg.debug_enabled = ctx.has("-D") || ctx.has("--debug");
  // [#1380] The generic parser no longer claims "--debug" for find (see
  // option_policy_for_command), so honor the -D alias from the raw
  // arguments, where find's own expression parser already accepts it.
  if (!cfg.debug_enabled) {
    for (size_t i = 0; i + 1 < ctx.raw_args.size(); ++i) {
      if (ctx.raw_args[i] == "--debug") {
        cfg.debug_enabled = true;
        cfg.debug_category = std::string(ctx.raw_args[i + 1]);
        break;
      }
    }
  }
  cfg.nowarn = ctx.has("-nowarn") && !ctx.has("-warn");
  cfg.ignore_readdir_race =
      ctx.has("-ignore_readdir_race") && !ctx.has("-noignore_readdir_race");
  // [GNU] -daystart / -xdev / -mount (parser.c).
  cfg.daystart = ctx.has("-daystart");
  cfg.stay_on_filesystem = ctx.has("-xdev") || ctx.has("-mount");

  cfg.delete_action = ctx.get<bool>("-delete", false);
  cfg.depth_first =
      ctx.get<bool>("-depth", false) || ctx.get<bool>("-d", false);
  if (cfg.delete_action && ctx.get<bool>("-prune", false) && !cfg.depth_first) {
    return std::unexpected(
        "The -delete action automatically turns on -depth, but -prune does "
        "nothing when -depth is in effect.  If you want to carry on anyway, "
        "just explicitly use the -depth option.");
  }
  auto exec_actions = parse_exec_actions(std::span<const std::string_view>(
      ctx.raw_args.data(), ctx.raw_args.size()));
  if (!exec_actions) return std::unexpected(exec_actions.error());
  cfg.exec_actions = std::move(*exec_actions);
  switch (resolve_symlink_mode(ctx)) {
    case SymlinkMode::All:
      cfg.follow_symlinks = true;
      cfg.follow_arg_symlinks = true;
      break;
    case SymlinkMode::CommandLineOnly:
      cfg.follow_symlinks = false;
      cfg.follow_arg_symlinks = true;
      break;
    case SymlinkMode::Never:
      cfg.follow_symlinks = false;
      cfg.follow_arg_symlinks = false;
      break;
  }

  if (!cfg.type_filter.empty()) {
    // [#1377] Same GNU validation as the expression parser.
    if (auto error = validate_type_argument("-type", cfg.type_filter)) {
      return std::unexpected(*error);
    }
  }

  auto size_text = ctx.get<std::string>("-size", "");
  if (!size_text.empty()) {
    auto parsed = parse_size_predicate(size_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.size_filter = *parsed;
  }

  auto atime_text = ctx.get<std::string>("-atime", "");
  if (!atime_text.empty()) {
    auto parsed = parse_time_predicate(atime_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.atime_filter = *parsed;
  }

  auto amin_text = ctx.get<std::string>("-amin", "");
  if (!amin_text.empty()) {
    auto parsed = parse_time_predicate(amin_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.amin_filter = *parsed;
  }

  auto ctime_text = ctx.get<std::string>("-ctime", "");
  if (!ctime_text.empty()) {
    auto parsed = parse_time_predicate(ctime_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.ctime_filter = *parsed;
  }

  auto cmin_text = ctx.get<std::string>("-cmin", "");
  if (!cmin_text.empty()) {
    auto parsed = parse_time_predicate(cmin_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.cmin_filter = *parsed;
  }

  auto mtime_text = ctx.get<std::string>("-mtime", "");
  if (!mtime_text.empty()) {
    auto parsed = parse_time_predicate(mtime_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.mtime_filter = *parsed;
  }

  auto used_text = ctx.get<std::string>("-used", "");
  if (!used_text.empty()) {
    auto parsed = parse_time_predicate(used_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.used_filter = *parsed;
  }

  auto mmin_text = ctx.get<std::string>("-mmin", "");
  if (!mmin_text.empty()) {
    auto parsed = parse_time_predicate(mmin_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.mmin_filter = *parsed;
  }

  // [#1312] GNU treats -mindepth greater than -maxdepth as a valid,
  // never-matching range (empty output, exit 0), not an error. Only
  // negative depths are rejected.
  if (cfg.mindepth < 0 || cfg.maxdepth < 0) {
    return std::unexpected("invalid depth range");
  }

  if (!cfg.files0_from.empty()) {
    auto roots = load_roots_from_files0(cfg.files0_from);
    if (!roots) return std::unexpected(roots.error());
    cfg.roots = std::move(*roots);
  } else {
    cfg.roots = parse_roots(std::span<const std::string_view>(
        ctx.raw_args.data(), ctx.raw_args.size()));
  }

  auto expression = parse_expression(std::span<const std::string_view>(
      ctx.raw_args.data(), ctx.raw_args.size()));
  if (!expression) return std::unexpected(expression.error());
  cfg.expression = std::move(*expression);

  if (auto ok = prepare_file_output_actions(*cfg.expression, cfg); !ok) {
    return std::unexpected(ok.error());
  }

  // [#1309] -ls, -fls and -quit are actions too: their presence must
  // suppress the default -print, just like -print/-printf/-exec do.
  bool has_print_action = ctx.get<bool>("-print", false) ||
                          ctx.get<bool>("-print0", false) ||
                          ctx.has("-fprint") || ctx.has("-fprint0") ||
                          ctx.has("-printf") || ctx.has("-fprintf") ||
                          ctx.has("-ls") || ctx.has("-fls") ||
                          ctx.has("-quit");
  if (!has_print_action && !cfg.delete_action && cfg.exec_actions.empty()) {
    cfg.has_print = true;  // default action
  }

  return cfg;
}

auto relative_or_self(const std::filesystem::path& root,
                      const std::filesystem::path& p) -> std::filesystem::path {
  std::error_code ec;
  auto rel = std::filesystem::relative(p, root, ec);
  if (ec || rel.empty()) return std::filesystem::path(".");
  return rel;
}

auto depth_from_root(const std::filesystem::path& root,
                     const std::filesystem::path& p) -> int {
  auto rel = relative_or_self(root, p);
  if (rel == ".") return 0;
  int d = 0;
  for (const auto& part : rel) {
    (void)part;
    ++d;
  }
  return d;
}

// Convert a filesystem path to UTF-8 explicitly (niubash #88). On MSVC,
// path::string()/generic_string() convert through the system ACP (e.g. GBK
// on zh-CN installs), so non-ASCII filenames leak as ACP bytes instead of
// UTF-8, both in printed output and in -name/-path pattern matching.
auto strip_extended_display_w(std::wstring_view path) -> std::wstring {
  if (path.size() >= 8 && path.compare(0, 8, L"\\\\?\\UNC\\") == 0) {
    return L"\\\\" + std::wstring(path.substr(8));
  }
  if (path.size() >= 4 && path.compare(0, 4, L"\\\\?\\") == 0) {
    return std::wstring(path.substr(4));
  }
  return std::wstring(path);
}

auto path_to_utf8(const std::filesystem::path& p) -> std::string {
  auto s = strip_extended_display_w(p.wstring());
  std::replace(s.begin(), s.end(), L'\\', L'/');
  return wstring_to_utf8(s);
}

auto path_display(const std::filesystem::path& p) -> std::string {
  auto s = path_to_utf8(p);
  if (s.empty()) return ".";
  return s;
}

// Probe/iterate through the extended-length API boundary so roots and
// descendants past MAX_PATH are not reported as missing (D9). std::filesystem
// without \\?\ fails GetFileAttributes with ERROR_PATH_NOT_FOUND even when the
// directory exists.
auto to_io_path(const std::filesystem::path& p) -> std::filesystem::path {
  return std::filesystem::path(native_path::to_extended_path(p.wstring()));
}

// Map an extended-path iterator entry back onto the command-line spelling so
// relative starting points keep relative printed names and -path patterns.
auto logical_from_io(const std::filesystem::path& logical_root,
                     const std::filesystem::path& io_root,
                     const std::filesystem::path& io_path)
    -> std::filesystem::path {
  const std::wstring io_root_s = io_root.wstring();
  const std::wstring io_path_s = io_path.wstring();
  if (io_path_s.size() <= io_root_s.size() ||
      _wcsnicmp(io_path_s.c_str(), io_root_s.c_str(), io_root_s.size()) != 0) {
    return std::filesystem::path(strip_extended_display_w(io_path_s));
  }
  std::wstring rel = io_path_s.substr(io_root_s.size());
  while (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/')) {
    rel.erase(rel.begin());
  }
  if (rel.empty()) return logical_root;
  return logical_root / std::filesystem::path(std::move(rel));
}

auto root_probe_error_text(DWORD error) -> std::string {
  if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
    return "Permission denied";
  }
  return win32_posix_error_text(error);
}

auto report_root_probe_failure(const std::filesystem::path& root) -> void {
  const DWORD error = GetLastError();
  safeErrorPrint("find: '");
  safeErrorPrint(path_display(root));
  safeErrorPrint("': ");
  safeErrorPrint(root_probe_error_text(error));
  safeErrorPrint("\n");
}

auto root_exists(const std::filesystem::path& root) -> bool {
  return native_path::exists_w(root.wstring());
}

auto is_empty_entry(const std::filesystem::directory_entry& e) -> bool {
  std::error_code ec;
  if (e.is_regular_file(ec) && !ec) {
    return e.file_size(ec) == 0 && !ec;
  }
  if (e.is_directory(ec) && !ec) {
    std::filesystem::directory_iterator it(
        e.path(), std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) return false;
    return it == std::filesystem::directory_iterator{};
  }
  return false;
}

// [#1310] GNU applies -size to every file type: the lstat size for
// symlinks (target-path length on this platform), the stat size for
// regular files and directories, and 0 for fifos/sockets. Under -L a
// symlink resolves to its target's size, except a dangling link, which
// falls back to the link itself (probed against GNU findutils 4.9.0).
auto find_size_bytes(const std::filesystem::directory_entry& e,
                     bool follow_symlinks) -> unsigned long long {
  std::error_code ec;
  if (e.is_symlink(ec) || is_directory_reparse_point(e)) {
    if (follow_symlinks) {
      ec.clear();
      auto target_size = e.file_size(ec);
      if (!ec) return static_cast<unsigned long long>(target_size);
    }
    ec.clear();
    auto target = std::filesystem::read_symlink(e.path(), ec);
    if (ec) return 0;
    return static_cast<unsigned long long>(path_to_utf8(target).size());
  }

  ec.clear();
  if (e.is_regular_file(ec) || e.is_directory(ec)) {
    auto size = e.file_size(ec);
    if (ec) return 0;
    return static_cast<unsigned long long>(size);
  }

  // Fifos, sockets and other special files have no size: 0 units.
  return 0;
}

auto size_matches(const std::filesystem::directory_entry& e,
                  const SizePredicate& pred, bool follow_symlinks) -> bool {
  const auto bytes = find_size_bytes(e, follow_symlinks);

  unsigned long long units = 0;
  if (bytes != 0) {
    units = (bytes + pred.unit - 1) / pred.unit;
  }
  if (units >
      static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
    return pred.predicate.comparison == NumericComparison::GreaterThan;
  }

  return numeric_matches(pred.predicate, static_cast<long long>(units));
}

auto permission_matches(const std::filesystem::path& p,
                        const std::filesystem::directory_entry& e,
                        const PermissionPredicate& pred) -> bool {
  const unsigned mode = permission_mode(p, e) & 07777U;
  switch (pred.comparison) {
    case PermissionComparison::Exact:
      return mode == pred.mode;
    case PermissionComparison::All:
      return (mode & pred.mode) == pred.mode;
    case PermissionComparison::Any:
      if (pred.mode == 0) return true;
      return (mode & pred.mode) != 0;
  }
  return false;
}

// [GNU] -daystart (findutils-4.10.0/find/pred.c): when -daystart is in
// effect, the "current time" used by -amin/-atime/-cmin/-ctime/-mmin/
// -mtime is the start of the local day instead of the current instant.
auto age_reference_now_ticks(bool daystart) -> long long {
  FILETIME now_filetime{};
  GetSystemTimeAsFileTime(&now_filetime);
  if (!daystart) return filetime_ticks(now_filetime);

  SYSTEMTIME local_now{};
  GetLocalTime(&local_now);
  SYSTEMTIME day_start{.wYear = local_now.wYear,
                       .wMonth = local_now.wMonth,
                       .wDay = local_now.wDay};
  auto ticks = local_system_time_to_filetime_ticks(day_start);
  return ticks ? *ticks : filetime_ticks(now_filetime);
}

// [#1311] GNU time predicates use two window families over the age in
// whole seconds, with NO clamping of future timestamps (a negative age
// simply falls wherever the window puts it):
//   MIN family (amin/cmin/mmin; -used on the atime-ctime gap in days):
//     exact v: (v-1)*u <= age < v*u;  +v: age > v*u;  -v: age < v*u.
//   DAY family (atime/ctime/mtime):
//     exact v: v*u <= age < (v+1)*u;  +v: age >= (v+1)*u;  -v: age < v*u.
// The old floor/trunc model (age clamped at 0, then divided) is wrong for
// both families, not just for future files: e.g. a 61-second-old file
// matches neither -mmin 1 nor -mmin 2 under the old model, but GNU
// matches it with -mmin 2 only.
auto file_age_seconds(const std::filesystem::path& p, FindFileTimeKind kind)
    -> std::optional<long long> {
  auto ticks = win32_file_time_ticks(p, kind);
  if (!ticks) return std::nullopt;
  long long now_ticks = age_reference_now_ticks(false);
  return (now_ticks - *ticks) / 10000000LL;
}

// [#1311] -daystart for the DAY family: calendar-day difference between
// the file's local day and today, DST-safe via llround (a 23/25-hour day
// still counts as one day). Returns day_count * 86400 so callers keep
// working in seconds.
auto file_age_daystart_seconds(const std::filesystem::path& p,
                               FindFileTimeKind kind)
    -> std::optional<long long> {
  auto ticks = win32_file_time_ticks(p, kind);
  if (!ticks) return std::nullopt;
  FILETIME filetime{.dwLowDateTime = static_cast<DWORD>(*ticks & 0xFFFFFFFFLL),
                    .dwHighDateTime =
                        static_cast<DWORD>(*ticks >> 32)};
  SYSTEMTIME utc{};
  if (FileTimeToSystemTime(&filetime, &utc) == 0) return std::nullopt;
  SYSTEMTIME local{};
  if (SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local) == 0) {
    return std::nullopt;
  }
  SYSTEMTIME day_start_local{.wYear = local.wYear,
                             .wMonth = local.wMonth,
                             .wDay = local.wDay};
  auto day_start_ticks = local_system_time_to_filetime_ticks(day_start_local);
  if (!day_start_ticks) return std::nullopt;
  long long now_day_ticks = age_reference_now_ticks(true);
  double days = std::llround(static_cast<double>(now_day_ticks -
                                                 *day_start_ticks) /
                             864000000000.0);
  return static_cast<long long>(days * 86400.0);
}

// [#1311] -used gap in raw ticks (access_time - change_time), unclamped.
// The negativity test must use ticks, not seconds: a freshly created
// file can carry atime a few hundred microseconds BEFORE ctime, and GNU
// (timespec-precise) treats any negative gap as never-matching, while
// truncating to seconds would falsely report gap 0 and match -used 1.
auto used_gap_ticks(const std::filesystem::path& p)
    -> std::optional<long long> {
  auto access_ticks = win32_file_time_ticks(p, FindFileTimeKind::Access);
  auto change_ticks = win32_file_time_ticks(p, FindFileTimeKind::Change);
  if (!access_ticks || !change_ticks) return std::nullopt;
  return *access_ticks - *change_ticks;
}

auto time_matches(const TimePredicate& pred, long long age_seconds,
                  std::chrono::seconds unit, bool day_family) -> bool {
  double age = static_cast<double>(age_seconds);
  double u = static_cast<double>(unit.count());
  double v = pred.value;
  switch (pred.comparison) {
    case NumericComparison::Exact:
      if (day_family) return age >= v * u && age < (v + 1) * u;
      return age >= (v - 1) * u && age < v * u;
    case NumericComparison::GreaterThan:
      if (day_family) return age >= (v + 1) * u;
      return age > v * u;
    case NumericComparison::LessThan:
      return age < v * u;
  }
  return false;
}

auto print_path(std::string_view path, bool null_terminated) -> void;
auto write_find_output_file(Config& cfg, const std::string& path,
                            std::string_view text) -> bool;
auto stop_on_stdout_failure(Config& cfg) -> void;
auto printf_path(std::string_view format, const std::filesystem::path& p,
                 const std::filesystem::directory_entry& e, int depth,
                 const std::filesystem::path& root) -> void;
auto execute_action_for_path(ExecAction& action, std::string_view path,
                             Config& cfg) -> bool;
auto delete_path(const std::filesystem::path& p, Config& cfg) -> bool;
auto link_target_display(const std::filesystem::directory_entry& e)
    -> std::string;

auto win32_filesystem_type_name(const std::filesystem::path& p) -> std::string;
auto file_size_bytes(const std::filesystem::directory_entry& e)
    -> unsigned long long;
auto permission_string(const std::filesystem::path& p,
                       const std::filesystem::directory_entry& e)
    -> std::string;
auto allocated_block_count(const std::filesystem::path& p,
                           const std::filesystem::directory_entry& e,
                           unsigned long long unit) -> unsigned long long;
auto format_printf(std::string_view fmt, const std::filesystem::path& p,
                   const std::filesystem::directory_entry& e, int depth,
                   const std::filesystem::path& root) -> std::string;
// [#1309] GNU "ls -dils" date: "%b %e %H:%M" for files modified within
// the last half Julian year (15778476 seconds) and not more than 3600
// seconds in the future, else "%b %e %Y" (space-padded day in both).
auto format_ls_date(const std::filesystem::path& p) -> std::string {
  static constexpr std::array<const char*, 12> kMonths = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  auto ticks = win32_file_time_ticks(p, FindFileTimeKind::Modify);
  if (!ticks) return "";
  LARGE_INTEGER value;
  value.QuadPart = *ticks;
  FILETIME file_time{};
  file_time.dwLowDateTime = value.LowPart;
  file_time.dwHighDateTime = value.HighPart;
  FILETIME local_file_time{};
  SYSTEMTIME local_system_time{};
  if (!FileTimeToLocalFileTime(&file_time, &local_file_time) ||
      !FileTimeToSystemTime(&local_file_time, &local_system_time)) {
    return "";
  }
  const long long age_seconds =
      (age_reference_now_ticks(false) - *ticks) / 10000000;
  std::ostringstream out;
  const auto month = local_system_time.wMonth >= 1 &&
                             local_system_time.wMonth <= 12
                         ? kMonths[local_system_time.wMonth - 1]
                         : "???";
  out << month << ' ' << std::setw(2) << local_system_time.wDay << ' ';
  if (age_seconds >= -3600 && age_seconds <= 15778476) {
    out << std::setw(2) << std::setfill('0') << local_system_time.wHour
        << ':' << std::setw(2) << std::setfill('0')
        << local_system_time.wMinute;
  } else {
    out << local_system_time.wYear;
  }
  return out.str();
}

auto evaluate_expression(const ExprNode& expr, const std::filesystem::path& p,
                         const std::filesystem::directory_entry& e, int depth,
                         const std::filesystem::path& root, Config& cfg)
    -> bool {
  switch (expr.kind) {
    case ExprKind::Always:
      return true;

    case ExprKind::Name: {
      auto filename = path_to_utf8(p.filename());
      if (filename.empty()) filename = path_to_utf8(p);
      return wildcard_match(expr.text, filename, true);
    }

    case ExprKind::IName: {
      auto filename = path_to_utf8(p.filename());
      if (filename.empty()) filename = path_to_utf8(p);
      return wildcard_match(expr.text, filename, false);
    }

    case ExprKind::Path: {
      auto full_path = path_to_utf8(p);
      if (full_path.empty()) full_path = ".";
      return wildcard_match(expr.text, full_path, true);
    }

    case ExprKind::IPath: {
      auto full_path = path_to_utf8(p);
      if (full_path.empty()) full_path = ".";
      return wildcard_match(expr.text, full_path, false);
    }

    case ExprKind::Regex:
    case ExprKind::IRegex: {
      if (!expr.regex) return false;
      auto full_path = path_to_utf8(p);
      if (full_path.empty()) full_path = ".";
      return expr.regex->matches_entire(full_path);
    }

    case ExprKind::Type:
      return type_matches(e, expr.text);
    case ExprKind::XType:
      return xtype_matches(p, e, expr.text, cfg, root);

    case ExprKind::Perm:
      return expr.permission && permission_matches(p, e, *expr.permission);

    case ExprKind::Readable:
      return win32_accessible(p, GENERIC_READ);

    case ExprKind::Writable:
      return win32_accessible(p, GENERIC_WRITE);

    case ExprKind::Executable:
      return win32_executable(p, e);

    case ExprKind::Inum: {
      if (!expr.numeric) return false;
      auto index = win32_file_index_value(p);
      return index && numeric_matches_unsigned(*expr.numeric, *index);
    }

    case ExprKind::Links:
      return expr.numeric &&
             numeric_matches_unsigned(*expr.numeric, win32_hard_link_count(p));

    case ExprKind::User:
      return owner_matches(p, expr.text);

    case ExprKind::NoUser:
      return win32_ownership_info(p).owner_name.empty();

    case ExprKind::NoGroup: {
      // On Windows every file carries a primary group SID; the default one
      // resolves to the well-known "None" group, which corresponds to no
      // real POSIX group. Treat it (and unresolved SIDs) as -nogroup hits.
      const auto& group = win32_ownership_info(p).group_name;
      bool is_none_group = group.size() == 4;
      for (size_t idx = 0; is_none_group && idx < 4; ++idx) {
        is_none_group =
            std::tolower(static_cast<unsigned char>(group[idx])) == "none"[idx];
      }
      return group.empty() || is_none_group;
    }

    case ExprKind::Group:
      return group_matches(p, expr.text);

    case ExprKind::Uid:
      return expr.numeric && owner_id_matches(p, *expr.numeric);

    case ExprKind::Gid:
      return expr.numeric && group_id_matches(p, *expr.numeric);

    case ExprKind::Empty:
      return is_empty_entry(e);

    case ExprKind::Size:
      return expr.size && size_matches(e, *expr.size, cfg.follow_symlinks);

    case ExprKind::ATime: {
      if (!expr.time_pred) return false;
      auto age = cfg.daystart
                     ? file_age_daystart_seconds(p, FindFileTimeKind::Access)
                     : file_age_seconds(p, FindFileTimeKind::Access);
      return age &&
             time_matches(*expr.time_pred, *age, std::chrono::hours(24), true);
    }

    case ExprKind::AMin: {
      if (!expr.time_pred) return false;
      auto age = file_age_seconds(p, FindFileTimeKind::Access);
      return age &&
             time_matches(*expr.time_pred, *age, std::chrono::minutes(1),
                          false);
    }

    case ExprKind::CTime: {
      if (!expr.time_pred) return false;
      auto age = cfg.daystart
                     ? file_age_daystart_seconds(p, FindFileTimeKind::Change)
                     : file_age_seconds(p, FindFileTimeKind::Change);
      return age &&
             time_matches(*expr.time_pred, *age, std::chrono::hours(24), true);
    }

    case ExprKind::CMin: {
      if (!expr.time_pred) return false;
      auto age = file_age_seconds(p, FindFileTimeKind::Change);
      return age &&
             time_matches(*expr.time_pred, *age, std::chrono::minutes(1),
                          false);
    }

    case ExprKind::MTime: {
      if (!expr.time_pred) return false;
      auto age = cfg.daystart
                     ? file_age_daystart_seconds(p, FindFileTimeKind::Modify)
                     : file_age_seconds(p, FindFileTimeKind::Modify);
      return age &&
             time_matches(*expr.time_pred, *age, std::chrono::hours(24), true);
    }

    case ExprKind::MMin: {
      if (!expr.time_pred) return false;
      auto age = file_age_seconds(p, FindFileTimeKind::Modify);
      return age &&
             time_matches(*expr.time_pred, *age, std::chrono::minutes(1),
                          false);
    }

    case ExprKind::Used: {
      if (!expr.time_pred) return false;
      auto gap_ticks = used_gap_ticks(p);
      return gap_ticks && *gap_ticks >= 0 &&
             time_matches(*expr.time_pred, *gap_ticks / 10000000LL,
                          std::chrono::hours(24), false);
    }

    case ExprKind::Newer: {
      if (!expr.reference_time) return false;
      std::error_code ec;
      auto write_time = e.last_write_time(ec);
      return !ec && write_time > *expr.reference_time;
    }

    case ExprKind::NewerXY: {
      if (!expr.reference_ticks) return false;
      auto ticks = win32_file_time_ticks(p, expr.time_kind);
      return ticks && *ticks > *expr.reference_ticks;
    }

    case ExprKind::ANewer:
    case ExprKind::CNewer: {
      if (!expr.reference_ticks) return false;
      auto ticks = win32_file_time_ticks(p, expr.kind == ExprKind::ANewer
                                                ? FindFileTimeKind::Access
                                                : FindFileTimeKind::Change);
      return ticks && *ticks > *expr.reference_ticks;
    }

    case ExprKind::Fstype:
      return win32_filesystem_type_name(p) == expr.text;

    case ExprKind::Lname:
    case ExprKind::ILname:
      return wildcard_match(expr.text, link_target_display(e),
                            expr.kind == ExprKind::Lname);

    case ExprKind::SameFile: {
      if (!expr.reference_identity) return false;
      auto identity = win32_file_identity(p);
      return identity && *identity == *expr.reference_identity;
    }

    case ExprKind::Print:
      print_path(path_display(p), false);
      stop_on_stdout_failure(cfg);
      return true;

    case ExprKind::Print0:
      print_path(path_display(p), true);
      stop_on_stdout_failure(cfg);
      return true;

    case ExprKind::FPrint: {
      std::string text(path_display(p));
      text.push_back(char{10});
      return write_find_output_file(cfg, expr.text, text);
    }

    case ExprKind::FPrint0: {
      std::string text(path_display(p));
      text.push_back(char{0});
      return write_find_output_file(cfg, expr.text, text);
    }

    case ExprKind::Printf:
      printf_path(expr.text, p, e, depth, root);
      stop_on_stdout_failure(cfg);
      return true;

    case ExprKind::FPrintf: {
      auto text = format_printf(expr.format_text, p, e, depth, root);
      return write_find_output_file(cfg, expr.text, text);
    }

    case ExprKind::False:
      return false;

    case ExprKind::Ls:
    case ExprKind::Fls: {
      // [#1309] GNU "ls -dils" shape: inode, 1K blocks, mode, links,
      // owner, group, size, date, path. Fields are single-space
      // separated (GNU pads inode/blocks columns; content and field
      // count match, padding does not).
      const auto ownership = win32_ownership_info(p);
      const auto index = win32_file_index_value(p);
      std::string text = std::to_string(index.value_or(0)) + " " +
                         std::to_string(allocated_block_count(p, e, 1024)) +
                         " " + permission_string(p, e) + " " +
                         std::to_string(win32_hard_link_count(p)) + " " +
                         ownership.owner_name + " " +
                         ownership.group_name + " " +
                         std::to_string(file_size_bytes(e)) + " " +
                         format_ls_date(p) + " " + path_display(p) + "\n";
      if (expr.kind == ExprKind::Ls) {
        safePrint(text);
        stop_on_stdout_failure(cfg);
        return true;
      }
      return write_find_output_file(cfg, expr.text, text);
    }

    case ExprKind::Exec:
    case ExprKind::OkDir:
      if (expr.action_index >= cfg.exec_actions.size()) return false;
      return execute_action_for_path(cfg.exec_actions[expr.action_index],
                                     path_display(p), cfg);

    case ExprKind::Delete:
      return delete_path(p, cfg);

    case ExprKind::Prune:
      cfg.prune_current = true;
      return true;

    case ExprKind::Quit:
      cfg.quit = true;
      return true;

    case ExprKind::Comma:
      if (!expr.left || !expr.right) return false;
      (void)evaluate_expression(*expr.left, p, e, depth, root, cfg);
      return evaluate_expression(*expr.right, p, e, depth, root, cfg);

    case ExprKind::And:
      return expr.left && expr.right &&
             evaluate_expression(*expr.left, p, e, depth, root, cfg) &&
             evaluate_expression(*expr.right, p, e, depth, root, cfg);

    case ExprKind::Or:
      return expr.left && expr.right &&
             (evaluate_expression(*expr.left, p, e, depth, root, cfg) ||
              evaluate_expression(*expr.right, p, e, depth, root, cfg));

    case ExprKind::Not:
      return expr.left &&
             !evaluate_expression(*expr.left, p, e, depth, root, cfg);
  }

  return false;
}

auto entry_matches(Config& cfg, const std::filesystem::path& p,
                   const std::filesystem::directory_entry& e, int depth,
                   const std::filesystem::path& root) -> bool {
  cfg.prune_current = false;
  if (depth < cfg.mindepth || depth > cfg.maxdepth) return false;

  if (cfg.expression) {
    return evaluate_expression(*cfg.expression, p, e, depth, root, cfg);
  }

  auto filename = path_to_utf8(p.filename());
  if (filename.empty()) filename = path_to_utf8(p);

  if (!cfg.name_pattern.empty() &&
      !wildcard_match(cfg.name_pattern, filename, true)) {
    return false;
  }

  if (!cfg.iname_pattern.empty() &&
      !wildcard_match(cfg.iname_pattern, filename, false)) {
    return false;
  }

  auto full_path = path_to_utf8(p);
  if (full_path.empty()) full_path = ".";

  if (!cfg.path_pattern.empty() &&
      !wildcard_match(cfg.path_pattern, full_path, true)) {
    return false;
  }

  if (!cfg.ipath_pattern.empty() &&
      !wildcard_match(cfg.ipath_pattern, full_path, false)) {
    return false;
  }

  if (!type_matches(e, cfg.type_filter)) return false;

  if (cfg.empty_filter && !is_empty_entry(e)) return false;

  if (cfg.size_filter &&
      !size_matches(e, *cfg.size_filter, cfg.follow_symlinks)) {
    return false;
  }

  if (cfg.atime_filter) {
    auto age = cfg.daystart
                   ? file_age_daystart_seconds(p, FindFileTimeKind::Access)
                   : file_age_seconds(p, FindFileTimeKind::Access);
    if (!age ||
        !time_matches(*cfg.atime_filter, *age, std::chrono::hours(24), true)) {
      return false;
    }
  }

  if (cfg.amin_filter) {
    auto age = file_age_seconds(p, FindFileTimeKind::Access);
    if (!age ||
        !time_matches(*cfg.amin_filter, *age, std::chrono::minutes(1),
                      false)) {
      return false;
    }
  }

  if (cfg.ctime_filter) {
    auto age = cfg.daystart
                   ? file_age_daystart_seconds(p, FindFileTimeKind::Change)
                   : file_age_seconds(p, FindFileTimeKind::Change);
    if (!age ||
        !time_matches(*cfg.ctime_filter, *age, std::chrono::hours(24), true)) {
      return false;
    }
  }

  if (cfg.cmin_filter) {
    auto age = file_age_seconds(p, FindFileTimeKind::Change);
    if (!age ||
        !time_matches(*cfg.cmin_filter, *age, std::chrono::minutes(1),
                      false)) {
      return false;
    }
  }

  if (cfg.mtime_filter) {
    auto age = cfg.daystart
                   ? file_age_daystart_seconds(p, FindFileTimeKind::Modify)
                   : file_age_seconds(p, FindFileTimeKind::Modify);
    if (!age ||
        !time_matches(*cfg.mtime_filter, *age, std::chrono::hours(24), true)) {
      return false;
    }
  }

  if (cfg.mmin_filter) {
    auto age = file_age_seconds(p, FindFileTimeKind::Modify);
    if (!age ||
        !time_matches(*cfg.mmin_filter, *age, std::chrono::minutes(1),
                      false)) {
      return false;
    }
  }

  if (cfg.used_filter) {
    auto gap_ticks = used_gap_ticks(p);
    if (!gap_ticks || *gap_ticks < 0 ||
        !time_matches(*cfg.used_filter, *gap_ticks / 10000000LL,
                      std::chrono::hours(24), false)) {
      return false;
    }
  }

  return true;
}

auto write_find_output_file(Config& cfg, const std::string& path,
                            std::string_view text) -> bool {
  auto it = cfg.output_files.find(path);
  if (it == cfg.output_files.end()) {
    if (auto ok = prepare_find_output_file(cfg, path); !ok) {
      safeErrorPrint("find: cannot open output file " + path + "\n");
      cfg.had_error = true;
      return false;
    }
    it = cfg.output_files.find(path);
  }
  if (it == cfg.output_files.end() || !it->second || !it->second->is_open()) {
    safeErrorPrint("find: cannot open output file " + path + "\n");
    cfg.had_error = true;
    return false;
  }
  auto& out = *it->second;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out.good()) {
    safeErrorPrint("find: cannot write output file " + path + "\n");
    cfg.had_error = true;
    return false;
  }
  return true;
}

auto print_path(std::string_view path, bool null_terminated) -> void {
  safePrint(path);
  if (null_terminated) {
    safePrint(char{'\0'});
  } else {
    safePrint("\n");
  }
}

// #1368: once a stdout write has failed (e.g. closed stdout, `>&-`),
// continuing the traversal only feeds a dead sink; GNU find dies at the
// first failed write. Reuse the -quit flag to unwind the walk promptly;
// the exit mapping happens once at the end of process().
auto stop_on_stdout_failure(Config& cfg) -> void {
  if (is_stdout_write_failed()) cfg.quit = true;
}

auto file_type_char(const std::filesystem::directory_entry& e) -> char {
  std::error_code ec;
  if ((e.is_symlink(ec) && !ec) || is_directory_reparse_point(e)) return 'l';
  ec.clear();
  if (e.is_regular_file(ec) && !ec) {
    return native_path::is_winux_fifo_w(e.path().wstring()) ? 'p' : 'f';
  }
  if (e.is_directory(ec) && !ec) return 'd';
  return '?';
}

auto symlink_target_type_char(const std::filesystem::directory_entry& e)
    -> char {
  std::error_code ec;
  if (!e.is_symlink(ec) && !is_directory_reparse_point(e)) {
    return file_type_char(e);
  }

  ec.clear();
  auto target = std::filesystem::read_symlink(e.path(), ec);
  if (ec) return '?';

  std::filesystem::path resolved_target = target;
  if (resolved_target.is_relative()) {
    resolved_target = e.path().parent_path() / resolved_target;
  }
  resolved_target = resolved_target.lexically_normal();

  auto status = std::filesystem::status(resolved_target, ec);
  if (ec) return '?';
  if (status.type() == std::filesystem::file_type::not_found) return 'N';
  if (status.type() == std::filesystem::file_type::regular) return 'f';
  if (status.type() == std::filesystem::file_type::directory) return 'd';
  if (status.type() == std::filesystem::file_type::symlink) return 'l';
  return '?';
}

auto file_size_bytes(const std::filesystem::directory_entry& e)
    -> unsigned long long {
  std::error_code ec;
  if (e.is_symlink(ec) || is_directory_reparse_point(e)) {
    ec.clear();
    auto target = std::filesystem::read_symlink(e.path(), ec);
    if (ec) return 0;
    return static_cast<unsigned long long>(path_to_utf8(target).size());
  }
  ec.clear();
  if (!e.is_regular_file(ec) || ec) return 0;
  auto size = e.file_size(ec);
  if (ec) return 0;
  return static_cast<unsigned long long>(size);
}

// #1288: the win32_* probes below receive the walk's logical (display)
// spelling, but Win32 calls on that spelling stat the wrong object past
// MAX_PATH or on a trailing-dot component — the unextended namespace
// strips the dot and enforces the legacy length limit. Probe and open
// the extended-length spelling instead, the same pattern #1285 applied
// to the walk roots. DOS device names are the one exception: they
// resolve only in the device namespace the \\?\ prefix bypasses, so
// they stay verbatim (the invariant native_path::attributes_w honours).
auto probe_spelling(const std::filesystem::path& p) -> std::wstring {
  const std::wstring raw = p.wstring();
  if (native_path::is_dos_device_name_w(raw)) return raw;
  return to_io_path(p).wstring();
}

auto win32_accessible(const std::filesystem::path& p, DWORD desired_access)
    -> bool {
  const std::wstring io = probe_spelling(p);
  DWORD attrs = GetFileAttributesW(io.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return false;
  if ((desired_access & GENERIC_WRITE) != 0 &&
      (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
    return false;
  }

  DWORD flags = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0
                    ? FILE_FLAG_BACKUP_SEMANTICS
                    : FILE_ATTRIBUTE_NORMAL;
  HANDLE handle =
      CreateFileW(io.c_str(), desired_access,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  CloseHandle(handle);
  return true;
}

auto win32_executable(const std::filesystem::path& p,
                      const std::filesystem::directory_entry& e) -> bool {
  std::error_code ec;
  if (e.is_directory(ec) && !ec) {
    return win32_accessible(p, FILE_LIST_DIRECTORY);
  }
  if (!e.is_regular_file(ec) || ec || !win32_accessible(p, GENERIC_READ)) {
    return false;
  }
  std::string ext = p.extension().generic_string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".cmd" ||
         ext == ".ps1";
}

auto permission_mode(const std::filesystem::path& p,
                     const std::filesystem::directory_entry& e) -> unsigned {
  std::error_code ec;
  if (e.is_symlink(ec) || is_directory_reparse_point(e)) {
    return 0777;
  }
  ec.clear();
  if (e.is_directory(ec) && !ec) {
    DWORD attrs = GetFileAttributesW(probe_spelling(p).c_str());
    unsigned mode = 0755;
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
      mode &= ~0222U;
    }
    return mode;
  }

  std::string ext = p.extension().generic_string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  bool executable =
      ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".ps1";
  unsigned mode = executable ? 0755 : 0644;
  DWORD attrs = GetFileAttributesW(probe_spelling(p).c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
    mode &= ~0222U;
  }
  return mode;
}

auto permission_bits(const std::filesystem::path& p,
                     const std::filesystem::directory_entry& e) -> std::string {
  unsigned mode = permission_mode(p, e);
  char buf[8];
  snprintf(buf, sizeof(buf), "%03o", mode);
  return std::string(buf);
}

auto permission_string(const std::filesystem::path& p,
                       const std::filesystem::directory_entry& e)
    -> std::string {
  char perms[11] = "----------";
  perms[10] = '\0';

  std::error_code ec;
  if (e.is_symlink(ec) || is_directory_reparse_point(e)) {
    perms[0] = 'l';
  } else if (e.is_directory(ec) && !ec) {
    perms[0] = 'd';
  }

  unsigned mode = permission_mode(p, e);
  if (mode & 0400) perms[1] = 'r';
  if (mode & 0200) perms[2] = 'w';
  if (mode & 0100) perms[3] = 'x';
  if (mode & 0040) perms[4] = 'r';
  if (mode & 0020) perms[5] = 'w';
  if (mode & 0010) perms[6] = 'x';
  if (mode & 0004) perms[7] = 'r';
  if (mode & 0002) perms[8] = 'w';
  if (mode & 0001) perms[9] = 'x';
  return std::string(perms, 10);
}

auto win32_hard_link_count(const std::filesystem::path& p)
    -> unsigned long long {
  const std::wstring io = probe_spelling(p);
  DWORD attrs = GetFileAttributesW(io.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return 1;

  DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
  HANDLE handle = CreateFileW(
      io.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return 1;

  BY_HANDLE_FILE_INFORMATION info{};
  bool ok = GetFileInformationByHandle(handle, &info) != 0;
  CloseHandle(handle);
  if (!ok) return 1;
  return static_cast<unsigned long long>(
      std::max<DWORD>(info.nNumberOfLinks, 1));
}

auto win32_file_identity(const std::filesystem::path& p)
    -> std::optional<FileIdentity> {
  const std::wstring io = probe_spelling(p);
  DWORD attrs = GetFileAttributesW(io.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return std::nullopt;

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(io.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

  BY_HANDLE_FILE_INFORMATION info{};
  std::optional<FileIdentity> result;
  if (GetFileInformationByHandle(handle, &info)) {
    result = FileIdentity{
        .volume_serial = info.dwVolumeSerialNumber,
        .file_index = (static_cast<ULONGLONG>(info.nFileIndexHigh) << 32) |
                      static_cast<ULONGLONG>(info.nFileIndexLow),
    };
  }
  CloseHandle(handle);
  return result;
}

auto win32_file_index_value(const std::filesystem::path& p)
    -> std::optional<unsigned long long> {
  auto identity = win32_file_identity(p);
  if (!identity) return std::nullopt;
  return static_cast<unsigned long long>(identity->file_index);
}

// [#1378] Identity of the directory a path resolves TO when symlinks are
// followed (no FILE_FLAG_OPEN_REPARSE_POINT): win32_file_identity pins the
// reparse point itself, which can never alias an ancestor directory, so
// loop detection needs the followed identity.
auto win32_followed_file_identity(const std::filesystem::path& p)
    -> std::optional<FileIdentity> {
  DWORD attrs = GetFileAttributesW(p.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return std::nullopt;

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }

  HANDLE handle =
      CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

  BY_HANDLE_FILE_INFORMATION info{};
  std::optional<FileIdentity> result;
  if (GetFileInformationByHandle(handle, &info)) {
    result = FileIdentity{
        .volume_serial = info.dwVolumeSerialNumber,
        .file_index = (static_cast<ULONGLONG>(info.nFileIndexHigh) << 32) |
                      static_cast<ULONGLONG>(info.nFileIndexLow),
    };
  }
  CloseHandle(handle);
  return result;
}

auto win32_file_time_ticks(const std::filesystem::path& p,
                           FindFileTimeKind kind) -> std::optional<long long> {
  const std::wstring io = probe_spelling(p);
  DWORD attrs = GetFileAttributesW(io.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return std::nullopt;

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(io.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

  FILE_BASIC_INFO info{};
  std::optional<long long> result;
  if (GetFileInformationByHandleEx(handle, FileBasicInfo, &info,
                                   sizeof(info))) {
    LARGE_INTEGER value = info.LastWriteTime;
    switch (kind) {
      case FindFileTimeKind::Access:
        value = info.LastAccessTime;
        break;
      case FindFileTimeKind::Birth:
        value = info.CreationTime;
        break;
      case FindFileTimeKind::Change:
        value = info.ChangeTime;
        break;
      case FindFileTimeKind::Modify:
        value = info.LastWriteTime;
        break;
    }
    result = value.QuadPart;
  }
  CloseHandle(handle);
  return result;
}

auto win32_file_index(const std::filesystem::path& p) -> std::string {
  DWORD attrs = GetFileAttributesW(p.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return {};

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};

  BY_HANDLE_FILE_INFORMATION info{};
  std::string result;
  if (GetFileInformationByHandle(handle, &info)) {
    ULONGLONG index = (static_cast<ULONGLONG>(info.nFileIndexHigh) << 32) |
                      static_cast<ULONGLONG>(info.nFileIndexLow);
    result = std::to_string(index);
  }
  CloseHandle(handle);
  return result;
}

auto win32_allocation_size_bytes(const std::filesystem::path& p)
    -> unsigned long long {
  DWORD attrs = GetFileAttributesW(p.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return 0;

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return 0;

  FILE_STANDARD_INFO info{};
  unsigned long long size = 0;
  if (GetFileInformationByHandleEx(handle, FileStandardInfo, &info,
                                   sizeof(info))) {
    size = static_cast<unsigned long long>(info.AllocationSize.QuadPart);
  }
  CloseHandle(handle);
  return size;
}

auto allocated_block_count(const std::filesystem::path& p,
                           const std::filesystem::directory_entry& e,
                           unsigned long long unit) -> unsigned long long {
  if (unit == 0) return 0;

  const auto allocated = win32_allocation_size_bytes(p);
  unsigned long long blocks = (allocated + unit - 1) / unit;

  std::error_code ec;
  if (e.is_regular_file(ec) && !ec) {
    const auto logical_size = file_size_bytes(e);
    if (logical_size > 0) {
      const unsigned long long min_blocks = (1024ULL + unit - 1) / unit;
      blocks = std::max(blocks, min_blocks);
    }
  }

  return blocks;
}

auto file_sparseness(const std::filesystem::path& p,
                     const std::filesystem::directory_entry& e) -> std::string {
  std::error_code ec;
  if (!e.is_regular_file(ec) || ec) {
    return "1";
  }

  const auto logical_size = file_size_bytes(e);
  if (logical_size == 0) {
    return "1";
  }

  const auto allocated = win32_allocation_size_bytes(p);
  const double sparseness =
      static_cast<double>(allocated) / static_cast<double>(logical_size);
  std::ostringstream out;
  out << sparseness;
  return out.str();
}

auto win32_volume_serial_number(const std::filesystem::path& p) -> std::string {
  DWORD attrs = GetFileAttributesW(p.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return {};

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};

  BY_HANDLE_FILE_INFORMATION info{};
  std::string result;
  if (GetFileInformationByHandle(handle, &info)) {
    result = std::to_string(
        static_cast<unsigned long long>(info.dwVolumeSerialNumber));
  }
  CloseHandle(handle);
  return result;
}

auto win32_filesystem_type_name(const std::filesystem::path& p) -> std::string {
  auto dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path(".");
  std::error_code ec;
  auto absolute = std::filesystem::absolute(dir, ec);
  auto wdir = ec ? dir.wstring() : absolute.wstring();

  wchar_t root[MAX_PATH];
  if (!GetVolumePathNameW(wdir.c_str(), root, MAX_PATH)) return {};

  wchar_t fs_name[MAX_PATH] = L"";
  DWORD serial = 0;
  DWORD max_component = 0;
  DWORD flags = 0;
  if (!GetVolumeInformationW(root, nullptr, 0, &serial, &max_component, &flags,
                             fs_name, MAX_PATH)) {
    return {};
  }

  return wstring_to_utf8(fs_name);
}

auto win32_io_block_size(const std::filesystem::path& p) -> unsigned long long {
  auto dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path(".");
  std::error_code ec;
  auto absolute = std::filesystem::absolute(dir, ec);
  auto wdir = ec ? dir.wstring() : absolute.wstring();

  wchar_t root[MAX_PATH];
  if (!GetVolumePathNameW(wdir.c_str(), root, MAX_PATH)) return 4096;

  DWORD sectors_per_cluster = 0;
  DWORD bytes_per_sector = 0;
  DWORD free_clusters = 0;
  DWORD total_clusters = 0;
  if (!GetDiskFreeSpaceW(root, &sectors_per_cluster, &bytes_per_sector,
                         &free_clusters, &total_clusters)) {
    return 4096;
  }

  const unsigned long long block_size =
      static_cast<unsigned long long>(sectors_per_cluster) * bytes_per_sector;
  return block_size == 0 ? 4096 : block_size;
}

auto win32_account_name_from_sid(PSID sid) -> std::string {
  if (sid == nullptr) return {};

  DWORD name_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;
  LookupAccountSidW(nullptr, sid, nullptr, &name_size, nullptr, &domain_size,
                    &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return {};
  }

  std::wstring name(name_size, L'\0');
  std::wstring domain(domain_size, L'\0');
  if (!LookupAccountSidW(nullptr, sid, name.data(), &name_size, domain.data(),
                         &domain_size, &sid_type)) {
    return {};
  }

  name.resize(name_size);
  return wstring_to_utf8(name);
}

auto win32_account_id_from_sid(PSID sid) -> std::string {
  if (sid == nullptr || !IsValidSid(sid)) return {};

  PUCHAR subauth_count = GetSidSubAuthorityCount(sid);
  if (subauth_count == nullptr || *subauth_count == 0) return {};

  DWORD* rid = GetSidSubAuthority(sid, *subauth_count - 1);
  if (rid == nullptr) return {};

  return std::to_string(*rid);
}

auto win32_ownership_info(const std::filesystem::path& p)
    -> Win32OwnershipInfo {
  std::wstring wpath = p.wstring();
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, &owner_sid,
      &group_sid, nullptr, nullptr, &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) {
      LocalFree(security_desc);
    }
    return {};
  }

  Win32OwnershipInfo info{.owner_name = win32_account_name_from_sid(owner_sid),
                          .owner_id = win32_account_id_from_sid(owner_sid),
                          .group_name = win32_account_name_from_sid(group_sid),
                          .group_id = win32_account_id_from_sid(group_sid)};
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  return info;
}

auto ascii_equals_ignore_case(std::string_view lhs, std::string_view rhs)
    -> bool {
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i) {
    auto a = static_cast<unsigned char>(lhs[i]);
    auto b = static_cast<unsigned char>(rhs[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

auto parse_unsigned_decimal(std::string_view text)
    -> std::optional<unsigned long long> {
  if (!is_decimal_text(text)) return std::nullopt;
  unsigned long long value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

auto numeric_id_matches(std::string_view actual,
                        const NumericPredicate& expected) -> bool {
  auto actual_value = parse_unsigned_decimal(actual);
  return actual_value &&
         numeric_matches_unsigned(
             expected, static_cast<unsigned long long>(*actual_value));
}

auto owner_matches(const std::filesystem::path& p, std::string_view expected)
    -> bool {
  auto ownership = win32_ownership_info(p);
  return win32_account_matches(expected, ownership.owner_name,
                               ownership.owner_id);
}

auto group_matches(const std::filesystem::path& p, std::string_view expected)
    -> bool {
  auto ownership = win32_ownership_info(p);
  return win32_account_matches(expected, ownership.group_name,
                               ownership.group_id);
}

auto owner_id_matches(const std::filesystem::path& p,
                      const NumericPredicate& expected) -> bool {
  return numeric_id_matches(win32_ownership_info(p).owner_id, expected);
}

auto group_id_matches(const std::filesystem::path& p,
                      const NumericPredicate& expected) -> bool {
  return numeric_id_matches(win32_ownership_info(p).group_id, expected);
}

enum class Win32FileTimeKind {
  LastAccess,
  Creation,
  LastWrite,
};

// [#1379] GNU renders %X@ as epoch seconds with exactly ten fractional
// digits (findutils print.c uses the nanosecond field plus a trailing
// zero from its 100ns-free representation).  FILETIME ticks are exact
// 100ns counts, so the rendering is exact and deterministic — unlike a
// std::filesystem clock conversion, which drifts by the offset between
// two now() samples.
auto format_epoch_seconds(long long ticks) -> std::string {
  long long seconds = ticks / 10000000LL - 11644473600LL;
  long long rem = ticks % 10000000LL;
  if (rem < 0) {
    --seconds;
    rem += 10000000LL;
  }
  std::ostringstream stream;
  stream << seconds << "." << std::setw(10) << std::setfill('0')
         << rem * 1000LL;
  return stream.str();
}

auto win32_file_time_seconds(const std::filesystem::path& p,
                             Win32FileTimeKind kind) -> std::string {
  FindFileTimeKind find_kind = FindFileTimeKind::Modify;
  if (kind == Win32FileTimeKind::LastAccess) {
    find_kind = FindFileTimeKind::Access;
  } else if (kind == Win32FileTimeKind::Creation) {
    find_kind = FindFileTimeKind::Birth;
  }
  auto ticks = win32_file_time_ticks(p, find_kind);
  if (!ticks) return "0.0000000000";
  return format_epoch_seconds(*ticks);
}

// [#1379] ctime-style rendering for the %a/%c/%t directives: GNU prints
// the time in ctime(3) layout but with the fractional seconds inserted
// before the year, at the same ten-digit precision as %X@
// (e.g. "Sun Sep 13 12:26:40.0000000000 2020").
auto win32_file_time_ctime(const std::filesystem::path& p,
                           Win32FileTimeKind kind) -> std::string {
  auto find_kind = FindFileTimeKind::Modify;
  if (kind == Win32FileTimeKind::LastAccess) {
    find_kind = FindFileTimeKind::Access;
  } else if (kind == Win32FileTimeKind::Creation) {
    find_kind = FindFileTimeKind::Birth;
  }
  auto ticks = win32_file_time_ticks(p, find_kind);
  if (!ticks) return "";

  FILETIME file_time{};
  file_time.dwLowDateTime = static_cast<DWORD>(*ticks & 0xFFFFFFFFLL);
  file_time.dwHighDateTime = static_cast<DWORD>(*ticks >> 32);
  FILETIME local_file_time{};
  SYSTEMTIME local{};
  if (!FileTimeToLocalFileTime(&file_time, &local_file_time) ||
      !FileTimeToSystemTime(&local_file_time, &local)) {
    return "";
  }

  static constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed",
                                              "Thu", "Fri", "Sat"};
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                            "May", "Jun", "Jul", "Aug",
                                            "Sep", "Oct", "Nov", "Dec"};
  long long rem = *ticks % 10000000LL;
  if (rem < 0) rem += 10000000LL;

  std::ostringstream stream;
  stream << kWeekdays[local.wDayOfWeek % 7] << ' '
         << kMonths[(local.wMonth - 1) % 12] << ' ' << std::setw(2)
         << std::setfill(' ') << local.wDay << ' ' << std::setw(2)
         << std::setfill('0') << local.wHour << ':' << std::setw(2)
         << local.wMinute << ':' << std::setw(2) << local.wSecond << '.'
         << std::setw(10) << rem * 1000LL << ' ' << local.wYear;
  return stream.str();
}

auto win32_file_time_component(const std::filesystem::path& p,
                               Win32FileTimeKind kind, char component)
    -> std::string {
  DWORD attrs = GetFileAttributesW(p.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return {};

  DWORD flags = 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};

  FILE_BASIC_INFO info{};
  std::string out;
  if (GetFileInformationByHandleEx(handle, FileBasicInfo, &info,
                                   sizeof(info))) {
    FILETIME file_time{};
    LARGE_INTEGER value = info.LastWriteTime;
    if (kind == Win32FileTimeKind::LastAccess) {
      value = info.LastAccessTime;
    } else if (kind == Win32FileTimeKind::Creation) {
      value = info.CreationTime;
    }

    file_time.dwLowDateTime = value.LowPart;
    file_time.dwHighDateTime = value.HighPart;

    FILETIME local_file_time{};
    SYSTEMTIME local_system_time{};
    if (FileTimeToLocalFileTime(&file_time, &local_file_time) &&
        FileTimeToSystemTime(&local_file_time, &local_system_time)) {
      std::ostringstream stream;
      auto is_leap_year = [](WORD year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
      };
      switch (component) {
        case 'Y':
          stream << std::setw(4) << std::setfill('0')
                 << local_system_time.wYear;
          break;
        case 'm':
          stream << std::setw(2) << std::setfill('0')
                 << local_system_time.wMonth;
          break;
        case 'd':
          stream << std::setw(2) << std::setfill('0') << local_system_time.wDay;
          break;
        case 'H':
          stream << std::setw(2) << std::setfill('0')
                 << local_system_time.wHour;
          break;
        case 'M':
          stream << std::setw(2) << std::setfill('0')
                 << local_system_time.wMinute;
          break;
        case 'S':
          stream << std::setw(2) << std::setfill('0')
                 << local_system_time.wSecond;
          break;
        case 'j': {
          static constexpr int days_before_month[12] = {
              0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
          int day_of_year = days_before_month[local_system_time.wMonth - 1] +
                            local_system_time.wDay;
          if (local_system_time.wMonth > 2 &&
              is_leap_year(local_system_time.wYear)) {
            ++day_of_year;
          }
          stream << std::setw(3) << std::setfill('0') << day_of_year;
          break;
        }
        default:
          break;
      }
      out = stream.str();
    }
  }
  CloseHandle(handle);
  return out;
}

auto modification_time_seconds(const std::filesystem::directory_entry& e)
    -> std::string {
  // [#1379] Exact FILETIME-based rendering (see format_epoch_seconds):
  // the previous std::filesystem clock conversion sampled two clocks
  // and produced a different sub-microsecond tail on every run.
  auto ticks = win32_file_time_ticks(e.path(), FindFileTimeKind::Modify);
  if (!ticks) return "0.0000000000";
  return format_epoch_seconds(*ticks);
}

auto append_file_time_printf(std::string& out, std::string_view format,
                             size_t& i, char directive,
                             const std::filesystem::path& p,
                             const std::filesystem::directory_entry& e)
    -> void {
  if (i + 1 >= format.size()) {
    safeErrorPrint("find: warning: unrecognized format directive `%");
    safeErrorPrint(std::string(1, directive));
    safeErrorPrint("'\n");
    out.push_back(directive);
    return;
  }

  ++i;
  const char subcode = format[i];
  if (subcode == '@') {
    switch (directive) {
      case 'A':
        out += win32_file_time_seconds(p, Win32FileTimeKind::LastAccess);
        return;
      case 'B':
      case 'C':
        out += win32_file_time_seconds(p, Win32FileTimeKind::Creation);
        return;
      case 'T':
        out += modification_time_seconds(e);
        return;
      default:
        break;
    }
  }

  if (subcode == 'Y' || subcode == 'm' || subcode == 'd' || subcode == 'H' ||
      subcode == 'M' || subcode == 'S' || subcode == 'j') {
    Win32FileTimeKind kind = Win32FileTimeKind::LastWrite;
    if (directive == 'A') {
      kind = Win32FileTimeKind::LastAccess;
    } else if (directive == 'B' || directive == 'C') {
      kind = Win32FileTimeKind::Creation;
    }

    out += win32_file_time_component(p, kind, subcode);
    return;
  }

  safeErrorPrint("find: warning: unrecognized format directive `%");
  safeErrorPrint(std::string(1, directive));
  safeErrorPrint(std::string(1, subcode));
  safeErrorPrint("'\n");
  out.push_back(directive);
  out.push_back(subcode);
}

auto basename_display(const std::filesystem::path& p) -> std::string {
  auto filename = path_to_utf8(p.filename());
  if (!filename.empty()) return filename;
  return path_display(p);
}

auto dirname_display(const std::filesystem::path& p) -> std::string {
  auto parent = path_to_utf8(p.parent_path());
  if (parent.empty()) return ".";
  return parent;
}

auto link_target_display(const std::filesystem::directory_entry& e)
    -> std::string {
  std::error_code ec;
  if (!e.is_symlink(ec) && !is_directory_reparse_point(e)) {
    return "";
  }

  ec.clear();
  auto target = std::filesystem::read_symlink(e.path(), ec);
  if (ec) return "";
  return path_to_utf8(target);
}

auto root_path_display(const std::filesystem::path& root) -> std::string {
  return path_display(root);
}

auto path_below_root_display(const std::filesystem::path& root,
                             const std::filesystem::path& p) -> std::string {
  std::error_code ec;
  auto rel = std::filesystem::relative(p, root, ec);
  if (ec) return path_display(p);
  if (rel.empty() || rel == ".") return "";
  return path_to_utf8(rel);
}

// [GNU] -printf field formatting (findutils-4.10.0/find/print.c):
// directives accept printf-style flags (`-`, space, `+`, `#`, `0`), a field
// width and a precision, e.g. %8p, %-10s, %.5f, %08d.
struct PrintfFieldSpec {
  std::string flags;
  int width = -1;
  int precision = -1;
};

auto parse_printf_field_spec(std::string_view format, size_t& i)
    -> PrintfFieldSpec {
  PrintfFieldSpec spec;
  while (i < format.size() &&
         (format[i] == '-' || format[i] == ' ' || format[i] == '+' ||
          format[i] == '#' || format[i] == '0')) {
    spec.flags.push_back(format[i++]);
  }
  if (i < format.size() && format[i] >= '0' && format[i] <= '9') {
    int width = 0;
    while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
      width = width * 10 + (format[i++] - '0');
    }
    spec.width = width;
  }
  if (i < format.size() && format[i] == '.') {
    ++i;
    int precision = 0;
    bool any = false;
    while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
      precision = precision * 10 + (format[i++] - '0');
      any = true;
    }
    if (any) spec.precision = precision;
  }
  return spec;
}

// [#1379] Flag semantics differ per conversion kind in GNU find
// (findutils print.c, probed against findutils 4.9.0): the '+' and ' '
// sign flags are honored only for %d; the '0' flag zero-pads only %d
// and %m and is ignored (space padding) for every other conversion,
// including numeric ones like %s and %i; precision zero-extends the
// integer conversions %d/%m but truncates string pieces.
enum class PrintfPieceKind { String, Numeric, SignedNumeric, OctalNumeric };

auto apply_printf_field_spec(std::string piece, const PrintfFieldSpec& spec,
                             PrintfPieceKind kind) -> std::string {
  if (spec.precision >= 0) {
    if (kind == PrintfPieceKind::SignedNumeric ||
        kind == PrintfPieceKind::OctalNumeric) {
      size_t sign_len =
          (!piece.empty() && (piece[0] == '-' || piece[0] == '+' ||
                              piece[0] == ' '))
              ? 1
              : 0;
      if (piece.size() - sign_len < static_cast<size_t>(spec.precision)) {
        piece.insert(sign_len,
                     static_cast<size_t>(spec.precision) - (piece.size() - sign_len),
                     '0');
      }
    } else if (static_cast<int>(piece.size()) > spec.precision) {
      piece.resize(static_cast<size_t>(spec.precision));
    }
  }
  if (kind == PrintfPieceKind::SignedNumeric &&
      (piece.empty() || piece[0] != '-')) {
    if (spec.flags.find('+') != std::string::npos) {
      piece.insert(0, "+");
    } else if (spec.flags.find(' ') != std::string::npos) {
      piece.insert(0, " ");
    }
  }
  if (spec.width >= 0 && static_cast<int>(piece.size()) < spec.width) {
    bool left_justify = spec.flags.find('-') != std::string::npos;
    bool zero_pad = spec.flags.find('0') != std::string::npos &&
                    !left_justify &&
                    (kind == PrintfPieceKind::SignedNumeric ||
                     kind == PrintfPieceKind::OctalNumeric);
    std::string padding(static_cast<size_t>(spec.width) - piece.size(),
                        zero_pad ? '0' : ' ');
    if (left_justify) {
      piece += padding;
    } else if (zero_pad && !piece.empty() &&
               (piece[0] == '-' || piece[0] == '+' || piece[0] == ' ')) {
      piece.insert(1, padding);
    } else {
      piece.insert(0, padding);
    }
  }
  return piece;
}

auto format_printf(std::string_view format, const std::filesystem::path& p,
                   const std::filesystem::directory_entry& e, int depth,
                   const std::filesystem::path& root) -> std::string {
  std::string out;
  out.reserve(format.size() + p.generic_string().size());

  for (size_t i = 0; i < format.size(); ++i) {
    char ch = format[i];
    if (ch == '\\' && i + 1 < format.size()) {
      char escaped = format[++i];
      if (escaped >= '0' && escaped <= '7') {
        unsigned int value = static_cast<unsigned int>(escaped - '0');
        int consumed = 0;
        while ((i + 1) < format.size() && consumed < 2 &&
               format[i + 1] >= '0' && format[i + 1] <= '7') {
          value = (value * 8) + static_cast<unsigned int>(format[i + 1] - '0');
          ++i;
          ++consumed;
        }
        out.push_back(static_cast<char>(value & 0xFF));
        continue;
      }
      switch (escaped) {
        case 'a':
          out.push_back('\a');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'c':
          return out;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'v':
          out.push_back('\v');
          break;
        case '0':
          out.push_back('\0');
          break;
        case '\\':
          out.push_back('\\');
          break;
        default:
          safeErrorPrint("find: warning: unrecognized escape `\\");
          safeErrorPrint(std::string(1, escaped));
          safeErrorPrint("'\n");
          out.push_back('\\');
          out.push_back(escaped);
          break;
      }
      continue;
    }

    if (ch != '%' || i + 1 >= format.size()) {
      out.push_back(ch);
      continue;
    }

    // [GNU] printf-style flags/width/precision precede the directive char
    // (findutils-4.10.0/find/print.c: get_format_flags_length).
    const size_t spec_start = i;
    ++i;
    PrintfFieldSpec spec = parse_printf_field_spec(format, i);
    if (i >= format.size()) {
      out.push_back('%');
      break;
    }

    char code = format[i];
    std::string piece;
    PrintfPieceKind kind = PrintfPieceKind::String;
    switch (code) {
      case '%':
        piece.push_back('%');
        break;
      case 'p':
        piece += path_display(p);
        break;
      case 'f':
        piece += basename_display(p);
        break;
      case 'h':
        piece += dirname_display(p);
        break;
      case 'l':
        piece += link_target_display(e);
        break;
      case 'H':
        piece += root_path_display(root);
        break;
      case 'P':
        piece += path_below_root_display(root, p);
        break;
      case 'y':
        piece.push_back(file_type_char(e));
        break;
      case 'Y':
        piece.push_back(symlink_target_type_char(e));
        break;
      case 's':
        kind = PrintfPieceKind::Numeric;
        piece += std::to_string(file_size_bytes(e));
        break;
      case 'b':
        kind = PrintfPieceKind::Numeric;
        piece += std::to_string(allocated_block_count(p, e, 512));
        break;
      case 'k':
        kind = PrintfPieceKind::Numeric;
        piece += std::to_string(allocated_block_count(p, e, 1024));
        break;
      case 'd':
        kind = PrintfPieceKind::SignedNumeric;
        piece += std::to_string(depth);
        break;
      case 'D':
        kind = PrintfPieceKind::Numeric;
        piece += win32_volume_serial_number(p);
        break;
      case 'F':
        piece += win32_filesystem_type_name(p);
        break;
      case 'g':
        piece += win32_ownership_info(p).group_name;
        break;
      case 'G':
        kind = PrintfPieceKind::Numeric;
        piece += win32_ownership_info(p).group_id;
        break;
      case 'o':
        kind = PrintfPieceKind::Numeric;
        piece += std::to_string(win32_io_block_size(p));
        break;
      case 'u':
        piece += win32_ownership_info(p).owner_name;
        break;
      case 'U':
        kind = PrintfPieceKind::Numeric;
        piece += win32_ownership_info(p).owner_id;
        break;
      case 'S':
        piece += file_sparseness(p, e);
        break;
      case 'm':
        kind = PrintfPieceKind::OctalNumeric;
        piece += permission_bits(p, e);
        break;
      case 'M':
        piece += permission_string(p, e);
        break;
      case 'n':
        kind = PrintfPieceKind::Numeric;
        piece += std::to_string(win32_hard_link_count(p));
        break;
      case 'i':
        kind = PrintfPieceKind::Numeric;
        piece += win32_file_index(p);
        break;
      case 'A':
        append_file_time_printf(piece, format, i, 'A', p, e);
        break;
      case 'B':
        append_file_time_printf(piece, format, i, 'B', p, e);
        break;
      case 'C':
        append_file_time_printf(piece, format, i, 'C', p, e);
        break;
      case 'T':
        append_file_time_printf(piece, format, i, 'T', p, e);
        break;
      // [#1379] %a/%c/%t print the access/change/modification time as a
      // ctime-style string with fractional seconds (GNU print.c).
      case 'a':
        piece += win32_file_time_ctime(p, Win32FileTimeKind::LastAccess);
        break;
      case 'c':
        piece += win32_file_time_ctime(p, Win32FileTimeKind::Creation);
        break;
      case 't':
        piece += win32_file_time_ctime(p, Win32FileTimeKind::LastWrite);
        break;
      default:
        safeErrorPrint("find: warning: unrecognized format directive `%");
        safeErrorPrint(std::string(1, code));
        safeErrorPrint("'\n");
        // [#1379] GNU echoes the whole directive verbatim, '%' and any
        // flags/width included ("%Q" prints "%Q", "%5Q" prints "%5Q");
        // the field spec must not be applied to it.
        out += format.substr(spec_start, i - spec_start + 1);
        piece.clear();
        kind = PrintfPieceKind::String;
        spec = PrintfFieldSpec{};
        break;
    }
    out += apply_printf_field_spec(std::move(piece), spec, kind);
  }

  return out;
}

auto printf_path(std::string_view format, const std::filesystem::path& p,
                 const std::filesystem::directory_entry& e, int depth,
                 const std::filesystem::path& root) -> void {
  safePrint(format_printf(format, p, e, depth, root));
}

auto delete_path(const std::filesystem::path& p, Config& cfg) -> bool {
  std::error_code ec;
  // #1288: delete through the extended spelling (#1285's to_io_path);
  // removing the logical spelling fails past MAX_PATH and on
  // trailing-dot components.
  bool removed = std::filesystem::remove(to_io_path(p), ec);
  if (!removed || ec) {
    safeErrorPrint("find: cannot delete '");
    safeErrorPrint(path_display(p));
    safeErrorPrint("'");
    if (ec) {
      safeErrorPrint(": ");
      safeErrorPrint(ec.message());
    }
    safeErrorPrint("\n");
    cfg.had_error = true;
    return false;
  }
  return true;
}

auto replace_placeholder(std::string_view text, std::string_view path)
    -> std::string {
  std::string out(text);
  size_t pos = 0;
  while ((pos = out.find("{}", pos)) != std::string::npos) {
    out.replace(pos, 2, path);
    pos += path.size();
  }
  return out;
}

// [#1315] Spawn outcome: a nullopt status means the child never ran
// (resolve or CreateProcessW failure) and carries the diagnostic reason;
// a plain 127 used to conflate "spawn failed" with "child exited 127",
// which silenced GNU's per-file spawn diagnostic. Callers print
// `find: '<command>': <reason>` and keep find's exit status unchanged
// for the `\;` form (GNU exec.c:388-392).
struct ChildRunResult {
  std::optional<int> status;
  std::string spawn_error;
};

auto run_child(const std::string& command, const std::vector<std::string>& args,
               const std::filesystem::path& working_directory = {})
    -> ChildRunResult {
  // Resolve the child with GNU execvp semantics (PATH-only; no
  // application-directory, cwd, or System32 fallback) so a name colliding
  // with a Windows image resolves from the caller's PATH.
  winux::childproc::ChildLaunchRequest find_launch_request;
  find_launch_request.command = command;
  find_launch_request.args = args;
  auto launch = winux::childproc::plan_child_launch(find_launch_request);
  if (launch.status != winux::childproc::ChildResolveStatus::Found) {
    std::string reason = "No such file or directory";
    if (launch.status ==
        winux::childproc::ChildResolveStatus::NotExecutable) {
      reason = "Permission denied";
    } else if (launch.status ==
               winux::childproc::ChildResolveStatus::NotADirectory) {
      reason = "Not a directory";
    }
    return ChildRunResult{std::nullopt, reason};
  }
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};

  std::wstring cwd =
      working_directory.empty() ? L"" : working_directory.wstring();
  BOOL ok = CreateProcessW(
      launch.application_name->c_str(), launch.command_line.data(), nullptr,
      nullptr, TRUE, 0, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
  if (!ok) {
    DWORD error = GetLastError();
    wchar_t* buffer = nullptr;
    std::string reason = "Unknown error";
    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0,
                       reinterpret_cast<LPWSTR>(&buffer), 0, nullptr) != 0 &&
        buffer != nullptr) {
      reason = wstring_to_utf8(buffer);
      LocalFree(buffer);
      while (!reason.empty() &&
             (reason.back() == '\n' || reason.back() == '\r' ||
              reason.back() == ' ')) {
        reason.pop_back();
      }
      if (reason.empty()) reason = "Unknown error";
    }
    return ChildRunResult{std::nullopt, reason};
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return ChildRunResult{static_cast<int>(exit_code), ""};
}

auto report_spawn_failure(const std::string& command,
                          const std::string& reason) -> void {
  safeErrorPrint("find: '");
  safeErrorPrint(command);
  safeErrorPrint("': ");
  safeErrorPrint(reason);
  safeErrorPrint("\n");
}

auto ask_confirmation(const ExecAction& action, std::string_view path) -> bool {
  safeErrorPrint("< ");
  safeErrorPrint(action.command);
  for (const auto& arg : action.args) {
    safeErrorPrint(" ");
    safeErrorPrint(replace_placeholder(arg, path));
  }
  safeErrorPrint(" ... ");
  safeErrorPrint(path);
  safeErrorPrint(" > ? ");

  std::string answer;
  if (!std::getline(std::cin, answer)) return false;
  return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

auto ask_confirmation_aggregate(const ExecAction& action) -> bool {
  safeErrorPrint("< ");
  safeErrorPrint(action.command);
  for (const auto& arg : action.args) {
    safeErrorPrint(" ");
    safeErrorPrint(arg);
  }
  for (const auto& path : action.pending_paths) {
    safeErrorPrint(" ");
    safeErrorPrint(path);
  }
  safeErrorPrint(" > ? ");

  std::string answer;
  if (!std::getline(std::cin, answer)) return false;
  return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

// [GNU] -execdir/-okdir (findutils-4.10.0/find/exec.c:114-129): the
// placeholder is replaced with "./basename", the relative name of the file
// inside the directory the command runs in, not with the full traversal
// path.
auto execdir_target_display(const std::filesystem::path& p) -> std::string {
  return "./" + basename_display(p);
}

auto execute_action_for_path(ExecAction& action, std::string_view path,
                             Config& cfg) -> bool {
  if (action.aggregate) {
    action.pending_paths.emplace_back(path);
    return true;
  }

  std::filesystem::path wpath =
      std::filesystem::path(utf8_to_wstring(std::string(path)));
  std::string target = std::string(path);
  std::filesystem::path cwd;
  if (action.execdir) {
    target = execdir_target_display(wpath);
    cwd = wpath.parent_path();
  }

  if (action.prompt && !ask_confirmation(action, target)) return false;

  std::vector<std::string> args;
  args.reserve(action.args.size());
  for (const auto& arg : action.args) {
    args.push_back(replace_placeholder(arg, target));
  }

  // [#1315] {} is substituted in the command position too: `-exec {} \;`
  // runs the matched file itself, as GNU does. The substitution loop
  // above only covered the arguments.
  std::string program = replace_placeholder(action.command, target);

  // [GNU] exec.c:388-392: `-exec cmd \;` only makes the predicate false
  // when the invoked command fails; it must NOT change find's exit status.
  // Only `-exec {} +` sets the program exit status. A spawn failure is
  // diagnosed per file (GNU prints it for every file) and also yields
  // false, without touching the exit status.
  auto result = run_child(program, args, cwd);
  if (!result.status) {
    report_spawn_failure(program, result.spawn_error);
    return false;
  }
  return *result.status == 0;
}

// [#1315] -exec ... {} + expansion: the parser already popped the single
// trailing {}, which the flush appends below. A non-trailing {} argument
// expands to the full target list instead of being passed literally.
auto expand_aggregate_args(const std::vector<std::string>& args,
                           const std::vector<std::string>& targets)
    -> std::vector<std::string> {
  std::vector<std::string> expanded;
  expanded.reserve(args.size() + targets.size());
  for (const auto& arg : args) {
    if (arg == "{}") {
      expanded.insert(expanded.end(), targets.begin(), targets.end());
    } else {
      expanded.push_back(arg);
    }
  }
  return expanded;
}

auto flush_exec_actions(Config& cfg) -> void {
  for (auto& action : cfg.exec_actions) {
    if (!action.aggregate || action.pending_paths.empty()) continue;

    // [GNU] exec.c: -execdir {} + batches pending paths per directory and
    // runs the command once per directory with "./basename" arguments.
    if (action.execdir) {
      std::map<std::wstring, std::vector<std::string>> per_directory;
      for (const auto& path : action.pending_paths) {
        std::filesystem::path wpath(utf8_to_wstring(path));
        per_directory[wpath.parent_path().wstring()].push_back(
            execdir_target_display(wpath));
      }
      for (auto& [dir, targets] : per_directory) {
        if (action.prompt && !ask_confirmation_aggregate(action)) {
          continue;
        }
        std::vector<std::string> args =
            expand_aggregate_args(action.args, targets);
        args.insert(args.end(), targets.begin(), targets.end());
        // [GNU] exec.c:397-399: `-exec {} +` is TRUE even when the invoked
        // command fails, but it sets find's exit status.
        auto result =
            run_child(action.command, args, std::filesystem::path(dir));
        if (!result.status) {
          report_spawn_failure(action.command, result.spawn_error);
          cfg.had_error = true;
        } else if (*result.status != 0) {
          cfg.had_error = true;
        }
      }
      action.pending_paths.clear();
      continue;
    }

    if (action.prompt && !ask_confirmation_aggregate(action)) {
      action.pending_paths.clear();
      continue;
    }

    std::vector<std::string> args =
        expand_aggregate_args(action.args, action.pending_paths);
    args.insert(args.end(), action.pending_paths.begin(),
                action.pending_paths.end());
    // [GNU] exec.c:397-399: the predicate value of `-exec {} +` stays TRUE
    // when the command fails; only the exit status is affected.
    auto result = run_child(action.command, args);
    if (!result.status) {
      report_spawn_failure(action.command, result.spawn_error);
      cfg.had_error = true;
    } else if (*result.status != 0) {
      cfg.had_error = true;
    }
    action.pending_paths.clear();
  }
}

auto apply_actions(const std::filesystem::path& p,
                   const std::filesystem::directory_entry&, Config& cfg)
    -> void {
  if (cfg.has_print) {
    print_path(path_display(p), false);
    stop_on_stdout_failure(cfg);
  }
}

// [GNU] -xdev/-mount: on Windows a file system boundary is approximated by
// the volume root (drive letter or mounted volume) reported by
// GetVolumePathNameW.  This is best-effort: it detects crossing to another
// volume but not same-volume mount nuances.
auto volume_root_key(const std::filesystem::path& p) -> std::wstring {
  wchar_t root[MAX_PATH];
  const auto wide = native_path::to_extended_path(p.wstring());
  if (GetVolumePathNameW(wide.c_str(), root, MAX_PATH)) {
    return root;
  }
  return {};
}

// [#1378] GNU find -L loop detection (findutils ftsfind.c): while
// following symlinks, find tracks the (device, inode) identity of every
// directory on the current traversal path; a directory whose followed
// identity equals an ancestor's is reported once and never descended
// into (and the entry itself is not evaluated), instead of expanding
// the cycle until the path stops fitting.
struct AncestorDirectory {
  FileIdentity identity;
  std::filesystem::path path;
};

auto report_filesystem_loop(const std::filesystem::path& p,
                            const std::filesystem::path& ancestor, Config& cfg)
    -> void {
  safeErrorPrint("find: File system loop detected; '");
  safeErrorPrint(path_display(p));
  safeErrorPrint("' is part of the same file system loop as '");
  safeErrorPrint(path_display(ancestor));
  safeErrorPrint("'.\n");
  cfg.had_error = true;
}

auto find_loop_ancestor(const std::vector<AncestorDirectory>& ancestors,
                        const FileIdentity& identity)
    -> const std::filesystem::path* {
  for (const auto& ancestor : ancestors) {
    if (ancestor.identity == identity) return &ancestor.path;
  }
  return nullptr;
}

auto should_descend_into(const std::filesystem::directory_entry& e,
                         const Config& cfg, bool command_line_root = false)
    -> bool {
  std::error_code ec;
  if (!e.is_directory(ec) || ec) return false;
  const bool is_link_like =
      (e.is_symlink(ec) && !ec) || is_directory_reparse_point(e);
  if (command_line_root) {
    if (cfg.follow_arg_symlinks) return true;
    if (is_link_like) return false;
  }
  if (!cfg.follow_symlinks && is_link_like) return false;
  // [GNU] -xdev/-mount: never descend into a directory that lives on a
  // different file system than the traversal starting point.  Command line
  // starting points are always processed regardless of their volume.
  if (cfg.stay_on_filesystem && !command_line_root &&
      !cfg.xdev_volume_key.empty()) {
    auto entry_volume = volume_root_key(e.path());
    if (!entry_volume.empty() && entry_volume != cfg.xdev_volume_key) {
      return false;
    }
  }
  return true;
}

// [#1316] GNU evaluates -quit as an action that exits as soon as it is
// reached, regardless of whether any entry matched yet. The quit checks
// below therefore test cfg.quit alone: gating them on matched_any let
// traversal continue into unvisited roots (and report their errors)
// when -quit fired on a non-matching entry. matched_any is kept as a
// write-only parameter to avoid churning every call site.
auto scan_depth_first(const std::filesystem::path& root,
                      const std::filesystem::path& p, int depth, Config& cfg,
                      bool& matched_any,
                      std::vector<AncestorDirectory>& ancestors) -> void {
  const auto io_p = to_io_path(p);
  std::error_code ec;
  std::filesystem::directory_entry entry(io_p, ec);
  if (ec) {
    safeErrorPrint("find: '");
    safeErrorPrint(path_display(p));
    safeErrorPrint("': ");
    safeErrorPrint(ec.message());
    safeErrorPrint("\n");
    cfg.had_error = true;
    return;
  }

  // [#1378] Loop detection applies to directories this call would
  // descend into while following symlinks; the root itself (depth 0)
  // starts a fresh ancestor chain.
  std::optional<FileIdentity> self_identity;
  if (depth < cfg.maxdepth && cfg.follow_symlinks &&
      should_descend_into(entry, cfg, depth == 0)) {
    self_identity = win32_followed_file_identity(p);
    if (self_identity && depth > 0) {
      if (const auto* loop = find_loop_ancestor(ancestors, *self_identity)) {
        report_filesystem_loop(p, *loop, cfg);
        return;
      }
    }
  }
  const size_t ancestor_mark = ancestors.size();
  if (self_identity) {
    ancestors.push_back(AncestorDirectory{*self_identity, p});
  }

  if (depth < cfg.maxdepth && should_descend_into(entry, cfg, depth == 0)) {
    auto options = std::filesystem::directory_options::skip_permission_denied;
    if (cfg.follow_symlinks) {
      options |= std::filesystem::directory_options::follow_directory_symlink;
    }

    std::filesystem::directory_iterator it(io_p, options, ec);
    std::filesystem::directory_iterator end;
    if (ec) {
      safeErrorPrint("find: '");
      safeErrorPrint(path_display(p));
      safeErrorPrint("': ");
      safeErrorPrint(root_probe_error_text(ec.value()));
      safeErrorPrint("\n");
      cfg.had_error = true;
    } else {
      for (; it != end && !cfg.quit; it.increment(ec)) {
        if (ec) {
          safeErrorPrint("find: '");
          safeErrorPrint(path_display(p));
          safeErrorPrint("': ");
          safeErrorPrint(root_probe_error_text(ec.value()));
          safeErrorPrint("\n");
          cfg.had_error = true;
          break;
        }
        auto child = logical_from_io(p, io_p, it->path());
        scan_depth_first(root, child, depth + 1, cfg, matched_any, ancestors);
      }
    }
  }
  ancestors.resize(ancestor_mark);

  if (cfg.quit) return;

  bool matched = entry_matches(cfg, p, entry, depth, root);
  if (cfg.quit && !matched) return;
  if (matched) {
    apply_actions(p, entry, cfg);
    matched_any = true;
  }
}

auto scan_delete_depth_first(const std::filesystem::path& root,
                             const std::filesystem::path& p, int depth,
                             Config& cfg, bool& matched_any,
                             std::vector<AncestorDirectory>& ancestors)
    -> void {
  const auto io_p = to_io_path(p);
  std::error_code ec;
  std::filesystem::directory_entry entry(io_p, ec);
  if (ec) {
    safeErrorPrint("find: '");
    safeErrorPrint(path_display(p));
    safeErrorPrint("': ");
    safeErrorPrint(ec.message());
    safeErrorPrint("\n");
    cfg.had_error = true;
    return;
  }

  // [#1378] Same loop detection as scan_depth_first.
  std::optional<FileIdentity> self_identity;
  if (depth < cfg.maxdepth && cfg.follow_symlinks &&
      should_descend_into(entry, cfg, depth == 0)) {
    self_identity = win32_followed_file_identity(p);
    if (self_identity && depth > 0) {
      if (const auto* loop = find_loop_ancestor(ancestors, *self_identity)) {
        report_filesystem_loop(p, *loop, cfg);
        return;
      }
    }
  }
  const size_t ancestor_mark = ancestors.size();
  if (self_identity) {
    ancestors.push_back(AncestorDirectory{*self_identity, p});
  }

  if (depth < cfg.maxdepth && should_descend_into(entry, cfg, depth == 0)) {
    auto options = std::filesystem::directory_options::skip_permission_denied;
    if (cfg.follow_symlinks) {
      options |= std::filesystem::directory_options::follow_directory_symlink;
    }

    std::filesystem::directory_iterator it(io_p, options, ec);
    std::filesystem::directory_iterator end;
    if (ec) {
      safeErrorPrint("find: '");
      safeErrorPrint(path_display(p));
      safeErrorPrint("': ");
      safeErrorPrint(root_probe_error_text(ec.value()));
      safeErrorPrint("\n");
      cfg.had_error = true;
    } else {
      for (; it != end && !cfg.quit; it.increment(ec)) {
        if (ec) {
          safeErrorPrint("find: '");
          safeErrorPrint(path_display(p));
          safeErrorPrint("': ");
          safeErrorPrint(root_probe_error_text(ec.value()));
          safeErrorPrint("\n");
          cfg.had_error = true;
          break;
        }
        auto child = logical_from_io(p, io_p, it->path());
        scan_delete_depth_first(root, child, depth + 1, cfg, matched_any,
                                ancestors);
      }
    }
  }
  ancestors.resize(ancestor_mark);

  if (cfg.quit) return;

  bool matched = entry_matches(cfg, p, entry, depth, root);
  if (cfg.quit && !matched) return;
  if (matched) {
    apply_actions(p, entry, cfg);
    matched_any = true;
  }
}

auto scan_one_root(const std::filesystem::path& root, Config& cfg,
                   bool& matched_any) -> void {
  if (!root_exists(root)) {
    report_root_probe_failure(root);
    cfg.had_error = true;
    return;
  }

  const auto io_root = to_io_path(root);
  std::error_code ec;
  std::filesystem::directory_entry root_entry(io_root, ec);
  if (!ec) {
    int d = 0;
    bool matched = entry_matches(cfg, root, root_entry, d, root);
    if (cfg.quit && !matched) return;
    if (matched) {
      apply_actions(root, root_entry, cfg);
      matched_any = true;
      if (cfg.quit) return;
    }
    if (cfg.prune_current && should_descend_into(root_entry, cfg, true)) {
      return;
    }

    if (!should_descend_into(root_entry, cfg, true)) {
      return;
    }
  }

  if (!native_path::is_directory_w(io_root.wstring())) return;

  auto options = std::filesystem::directory_options::skip_permission_denied;
  if (cfg.follow_symlinks) {
    options |= std::filesystem::directory_options::follow_directory_symlink;
  }

  std::filesystem::recursive_directory_iterator it(io_root, options, ec);
  std::filesystem::recursive_directory_iterator end;

  if (ec) {
    safeErrorPrint("find: '");
    safeErrorPrint(path_display(root));
    safeErrorPrint("': ");
    safeErrorPrint(root_probe_error_text(ec.value()));
    safeErrorPrint("\n");
    cfg.had_error = true;
    return;
  }

  // [#1378] Ancestor chain for loop detection, indexed by depth:
  // dir_stack[k] is the directory at depth k on the current path whose
  // contents the iterator is walking (index 0 is the root itself).
  std::vector<AncestorDirectory> dir_stack;
  if (cfg.follow_symlinks) {
    if (auto root_identity = win32_followed_file_identity(root)) {
      dir_stack.push_back(AncestorDirectory{*root_identity, root});
    }
  }

  // #1283: advance with the error_code overload, exactly like
  // scan_depth_first / scan_delete_depth_first do. The throwing ++it used
  // to abort the whole process (0xC0000409) when a step failed — stepping
  // into a directory whose name ends in a dot, or walking past MAX_PATH —
  // where GNU find reports the entry and finishes with a nonzero status.
  std::error_code iec;
  for (; it != end;) {
    const auto& de = *it;
    auto p = logical_from_io(root, io_root, de.path());
    int d = static_cast<int>(it.depth()) + 1;

    if (d > cfg.maxdepth) {
      it.disable_recursion_pending();
    } else {
      // [#1378] Directories deeper than the current path belong to a
      // subtree that has been left behind; drop them before checking.
      if (cfg.follow_symlinks &&
          dir_stack.size() > static_cast<size_t>(d)) {
        dir_stack.resize(static_cast<size_t>(d));
      }
      bool will_descend = should_descend_into(de, cfg);
      std::optional<FileIdentity> self_identity;
      bool looped = false;
      if (cfg.follow_symlinks && will_descend) {
        self_identity = win32_followed_file_identity(p);
        if (self_identity) {
          if (const auto* loop =
                  find_loop_ancestor(dir_stack, *self_identity)) {
            report_filesystem_loop(p, *loop, cfg);
            it.disable_recursion_pending();
            looped = true;
          }
        }
      }
      if (!looped) {
        bool matched = entry_matches(cfg, p, de, d, root);
        if (cfg.quit && !matched) return;
        if (matched) {
          apply_actions(p, de, cfg);
          matched_any = true;
          if (cfg.quit) return;
        }
        if (cfg.prune_current || !should_descend_into(de, cfg)) {
          it.disable_recursion_pending();
        } else if (self_identity) {
          if (dir_stack.size() > static_cast<size_t>(d)) {
            dir_stack.resize(static_cast<size_t>(d));
          }
          if (dir_stack.size() == static_cast<size_t>(d)) {
            dir_stack.push_back(AncestorDirectory{*self_identity, p});
          } else {
            dir_stack[static_cast<size_t>(d)] =
                AncestorDirectory{*self_identity, p};
          }
        }
      }
    }

    iec.clear();
    it.increment(iec);
    if (iec) {
      safeErrorPrint("find: '");
      safeErrorPrint(path_display(p));
      safeErrorPrint("': ");
      safeErrorPrint(root_probe_error_text(iec.value()));
      safeErrorPrint("\n");
      cfg.had_error = true;
      break;
    }
  }
}

auto process(Config& cfg) -> int {
  bool matched_any = false;
  for (const auto& r : cfg.roots) {
    // Roots arrive as UTF-8 from the argv boundary; build the path from the
    // wide form because the narrow path ctor would decode via the ACP.
    auto root = std::filesystem::path(utf8_to_wstring(r));
    // [GNU] -xdev: the volume of the starting point anchors the traversal.
    cfg.xdev_volume_key = volume_root_key(root);
    if (cfg.delete_action) {
      if (!root_exists(root)) {
        report_root_probe_failure(root);
        cfg.had_error = true;
      } else {
        std::vector<AncestorDirectory> ancestors;
        scan_delete_depth_first(root, root, 0, cfg, matched_any, ancestors);
      }
    } else if (cfg.depth_first) {
      if (!root_exists(root)) {
        report_root_probe_failure(root);
        cfg.had_error = true;
      } else {
        std::vector<AncestorDirectory> ancestors;
        scan_depth_first(root, root, 0, cfg, matched_any, ancestors);
      }
    } else {
      scan_one_root(root, cfg, matched_any);
    }
    if (cfg.quit) break;
  }

  flush_exec_actions(cfg);

  // [GNU] findutils registers close_stdout: a failed stdout write is a
  // fatal error reported once as "find: write error: <strerror>" with the
  // nonzero findutils error status 1 (#1368). A closed downstream pipe
  // keeps the quiet SIGPIPE-style status.
  if (int write_error_rc = report_stdout_write_error("find", 1)) {
    return write_error_rc;
  }
  if (cfg.had_error) return 1;
  return 0;
}

}  // namespace find_pipeline

REGISTER_COMMAND(find, "find", "find [path...] [expression]",
                 "Search for files in a directory hierarchy.\n"
                 "If no path is given, '.' is used.",
                 "  find . -name '*.cpp'\n"
                 "  find src -type f -maxdepth 2\n"
                 "  find . -iname 'readme*'",
                 "grep(1), ls(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FIND_OPTIONS) {
  using namespace find_pipeline;

  // [GNU] -help/-version/-context act where they appear on the command
  // line, so honor whichever comes first (a "--" ends option processing).
  for (const auto& arg : ctx.raw_args) {
    if (arg == "--") break;
    if (arg == "-help") {
      cmd::meta::Registry::print_help("find");
      return 0;
    }
    if (arg == "-version") {
      safePrintLn("find (WinuxCmd) " + std::string(WinuxCmd::VERSION_STRING));
      return 0;
    }
    if (arg == "-context") {
      // findutils parser.c: with SELinux support compiled out the
      // predicate is rejected outright.
      safeErrorPrintLn(
          "find: invalid predicate -context: SELinux is not enabled.");
      return 1;
    }
  }

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"find");
    return 1;
  }

  return process(*cfg);
}
