// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for install.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright ? 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionType;

auto constexpr INSTALL_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--backup", "make a backup of each existing destination file",
           BOOL_TYPE),
    // [GNU] -c is accepted and ignored (install.c:821-822: "(ignored)")
    OPTION("-c", "", "(ignored)", BOOL_TYPE),
    // [GNU]
    OPTION("-C", "--compare",
           "compare source and destination and skip copy if identical",
           BOOL_TYPE),
    // [GNU]
    OPTION("-d", "--directory", "treat all arguments as directory names",
           BOOL_TYPE),
    // [GNU]
    OPTION("-D", "", "create all leading components of DEST except the last",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-g", "--group", "set group ownership", STRING_TYPE),
    // [GNU]
    OPTION("-m", "--mode", "set permission mode", STRING_TYPE),
    // [DIFFERS]
    OPTION("-o", "--owner", "set ownership", STRING_TYPE),
    // [GNU]
    OPTION("-p", "--preserve-timestamps",
           "apply access/modification times of SOURCE files", BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--strip", "strip symbol tables", BOOL_TYPE),
    // [GNU]
    OPTION("", "--debug", "print debugging information", BOOL_TYPE),
    // [GNU]
    OPTION("", "--strip-program", "program used to strip binaries",
           STRING_TYPE),
    // [GNU]
    OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
    // [GNU]
    OPTION("-t", "--target-directory", "specify the destination directory",
           STRING_TYPE),
    // [GNU]
    OPTION("-T", "--no-target-directory",
           "do not treat the last operand specially when it is a directory",
           BOOL_TYPE),
    // [GNU]
    OPTION("-v", "--verbose",
           "print the name of each directory as it is created", BOOL_TYPE),
    // [DIFFERS]
    OPTION("", "--preserve-context", "preserve SELinux security context",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-Z", "",
           "set SELinux security context of destination files to default",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("", "--context", "set SELinux security context of destination files",
           OPTIONAL_STRING_TYPE)};

namespace install_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool backup = false;
  bool directory_mode = false;
  bool preserve_timestamps = false;
  bool compare = false;
  bool strip = false;
  bool verbose = false;
  bool create_leading_dirs = false;
  bool no_target_directory = false;
  bool preserve_context = false;
  bool default_context = false;
  std::string selinux_context;
  std::string backup_suffix = "~";
  std::string group;
  std::string mode;
  std::string owner;
  std::string strip_program;
  std::string target_dir;
  SmallVector<std::string, 64> sources;
};

struct ModeState {
  bool owner_write = true;
  bool group_write = false;
  bool other_write = false;
};

// [GNU] quoteaf-style escaping for mode diagnostics: non-printable bytes
// are rendered as \ooo octal escapes (uutils#13834).
auto quote_mode_text(std::string_view text) -> std::string {
  std::string out = "'";
  for (unsigned char ch : text) {
    if (ch < 0x20 || ch == 0x7f) {
      char esc[8];
      std::snprintf(esc, sizeof(esc), "\\%03o", static_cast<unsigned>(ch));
      out += esc;
    } else {
      out.push_back(static_cast<char>(ch));
    }
  }
  out += "'";
  return out;
}

auto parse_install_mode(std::string_view mode_text) -> cp::Result<ModeState> {
  ModeState state{};
  if (mode_text.empty()) {
    return state;
  }

  auto parse_numeric = [](std::string_view text) -> std::optional<unsigned> {
    if (text.size() != 3 && text.size() != 4) {
      return std::nullopt;
    }
    unsigned mode = 0;
    for (char ch : text) {
      if (ch < '0' || ch > '7') {
        return std::nullopt;
      }
      mode = (mode << 3U) | static_cast<unsigned>(ch - '0');
    }
    return mode;
  };

  if (auto numeric = parse_numeric(mode_text)) {
    return ModeState{(*numeric & 0200U) != 0, (*numeric & 0020U) != 0,
                     (*numeric & 0002U) != 0};
  }

  state = ModeState{false, false, false};
  std::string mode_string(mode_text);
  size_t start = 0;
  while (start <= mode_string.size()) {
    size_t comma = mode_string.find(',', start);
    std::string_view clause =
        comma == std::string::npos
            ? std::string_view(mode_string).substr(start)
            : std::string_view(mode_string).substr(start, comma - start);
    if (clause.empty()) {
      return std::unexpected("invalid mode " + quote_mode_text(mode_string));
    }

    size_t i = 0;
    bool target_user = false;
    bool target_group = false;
    bool target_other = false;
    while (i < clause.size()) {
      char ch = clause[i];
      if (ch == 'u') {
        target_user = true;
      } else if (ch == 'g') {
        target_group = true;
      } else if (ch == 'o') {
        target_other = true;
      } else if (ch == 'a') {
        target_user = target_group = target_other = true;
      } else {
        break;
      }
      ++i;
    }
    if (!target_user && !target_group && !target_other) {
      target_user = target_group = target_other = true;
    }

    if (i >= clause.size() ||
        (clause[i] != '+' && clause[i] != '-' && clause[i] != '=')) {
      return std::unexpected("invalid mode " + quote_mode_text(mode_string));
    }
    char op = clause[i++];

    bool perm_write = false;
    bool saw_perm = false;
    for (; i < clause.size(); ++i) {
      char perm = clause[i];
      switch (perm) {
        case 'r':
        case 'x':
        case 'X':
        case 's':
        case 't':
          saw_perm = true;
          break;
        case 'w':
          saw_perm = true;
          perm_write = true;
          break;
        case 'u':
          saw_perm = true;
          perm_write = perm_write || state.owner_write;
          break;
        case 'g':
          saw_perm = true;
          perm_write = perm_write || state.group_write;
          break;
        case 'o':
          saw_perm = true;
          perm_write = perm_write || state.other_write;
          break;
        default:
          return std::unexpected("invalid mode " +
                                 quote_mode_text(mode_string));
      }
    }
    if (!saw_perm) {
      return std::unexpected("invalid mode " + quote_mode_text(mode_string));
    }

    auto apply_write = [op, perm_write](bool current) -> bool {
      if (op == '+') {
        return current || perm_write;
      }
      if (op == '-') {
        return current && !perm_write;
      }
      return perm_write;
    };

    if (target_user) {
      state.owner_write = apply_write(state.owner_write);
    }
    if (target_group) {
      state.group_write = apply_write(state.group_write);
    }
    if (target_other) {
      state.other_write = apply_write(state.other_write);
    }

    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }

  return state;
}

// Wide + \\?\-extended attribute probe so deep paths and non-ASCII names
// work (uutils#8963).
auto native_attributes(const std::string& path) -> DWORD {
  return native_path::operand_target_attributes_w(
      native_path::make_api_path_operand(path));
}

auto file_owner_writable(const std::string& path) -> std::optional<bool> {
  DWORD attrs = native_attributes(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return std::nullopt;
  }
  return (attrs & FILE_ATTRIBUTE_READONLY) == 0;
}

auto apply_mode_state(const std::string& path, const ModeState& mode_state)
    -> bool {
  auto operand = native_path::make_api_path_operand(path);
  DWORD attrs = native_path::operand_target_attributes_w(operand);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  DWORD new_attrs = attrs;
  if (mode_state.owner_write) {
    new_attrs &= ~FILE_ATTRIBUTE_READONLY;
  } else {
    new_attrs |= FILE_ATTRIBUTE_READONLY;
  }
  if (new_attrs == attrs) {
    return true;
  }
  return SetFileAttributesW(operand.extended.c_str(), new_attrs) != 0;
}

// [GNU] mkancesdirs-style creation: every missing leading component is
// created.  Uses \\?\ extended paths so trees deeper than MAX_PATH work
// (uutils#8963).  Components actually created are appended to `created`
// (top-down) when non-null.  Returns false on failure.
auto create_directories_gnu(const std::string& path,
                            std::vector<std::string>* created) -> bool {
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    char ch = path[i];
    cur.push_back(ch);
    const bool at_sep = (ch == '/' || ch == '\\');
    const bool last = (i + 1 == path.size());
    if (!at_sep && !last) continue;
    std::string comp = at_sep ? cur.substr(0, cur.size() - 1) : cur;
    if (comp.empty() || comp == "." || comp == "..") continue;
    if (comp.size() == 2 && comp[1] == ':') continue;  // drive root "C:"
    DWORD attrs = native_attributes(comp);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
      if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;  // ENOTDIR
      continue;
    }
    auto operand = native_path::make_api_path_operand(comp);
    if (!CreateDirectoryW(operand.extended.c_str(), nullptr)) {
      DWORD err = GetLastError();
      if (err == ERROR_ALREADY_EXISTS) continue;  // racing creators (#330)
      return false;
    }
    if (created) created->push_back(comp);
  }
  return true;
}

// [GNU] install copies /dev/stdin like any other file (uutils#12407).
// On Windows there is no /dev, so stdin aliases are streamed from the
// process' own stdin handle.
auto is_stdin_alias(std::string_view source) -> bool {
  return source == "/dev/stdin" || source == "/dev/fd/0" ||
         source == "/proc/self/fd/0";
}

auto copy_stdin_to_dest(const std::string& dest) -> bool {
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  if (in == nullptr || in == INVALID_HANDLE_VALUE) return false;
  auto dst_operand = native_path::make_api_path_operand(dest);
  HANDLE out =
      CreateFileW(dst_operand.extended.c_str(), GENERIC_WRITE, 0, nullptr,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE) return false;
  char buf[64 * 1024];
  DWORD got = 0;
  bool ok = true;
  while (ReadFile(in, buf, sizeof(buf), &got, nullptr) && got > 0) {
    DWORD written = 0;
    if (!WriteFile(out, buf, got, &written, nullptr) || written != got) {
      ok = false;
      break;
    }
  }
  CloseHandle(out);
  return ok;
}

void append_source_operand(Config& cfg, const std::string& file_arg) {
  if (contains_wildcard(file_arg)) {
    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded) {
      for (const auto& file : glob_result.files) {
        cfg.sources.push_back(wstring_to_utf8(file));
      }
      return;
    }
  }
  cfg.sources.push_back(file_arg);
}

auto build_config(const CommandContext<INSTALL_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.backup = ctx.get<bool>("--backup", false) || ctx.get<bool>("-b", false);
  cfg.directory_mode =
      ctx.get<bool>("--directory", false) || ctx.get<bool>("-d", false);
  cfg.preserve_timestamps = ctx.get<bool>("--preserve-timestamps", false) ||
                            ctx.get<bool>("-p", false);
  // [GNU] -c does not enable --compare; it is a no-op.
  cfg.compare = ctx.get<bool>("--compare", false) || ctx.get<bool>("-C", false);
  cfg.strip = ctx.get<bool>("--strip", false) || ctx.get<bool>("-s", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) ||
                ctx.get<bool>("-v", false) || ctx.get<bool>("--debug", false);
  cfg.create_leading_dirs = ctx.get<bool>("-D", false);
  cfg.no_target_directory = ctx.get<bool>("-T", false) ||
                            ctx.get<bool>("--no-target-directory", false);
  cfg.preserve_context = ctx.get<bool>("--preserve-context", false);
  cfg.default_context = ctx.get<bool>("-Z", false);
  cfg.selinux_context = ctx.get<std::string>("--context", "");

  auto group_opt = ctx.get<std::string>("--group", "");
  if (group_opt.empty()) {
    group_opt = ctx.get<std::string>("-g", "");
  }
  cfg.group = group_opt;

  auto mode_opt = ctx.get<std::string>("--mode", "");
  if (mode_opt.empty()) {
    mode_opt = ctx.get<std::string>("-m", "");
  }
  cfg.mode = mode_opt;

  auto owner_opt = ctx.get<std::string>("--owner", "");
  if (owner_opt.empty()) {
    owner_opt = ctx.get<std::string>("-o", "");
  }
  cfg.owner = owner_opt;

  auto suffix_opt = ctx.get<std::string>("--suffix", "");
  if (suffix_opt.empty()) {
    suffix_opt = ctx.get<std::string>("-S", "");
  }
  if (!suffix_opt.empty()) {
    cfg.backup_suffix = suffix_opt;
  }

  cfg.strip_program = ctx.get<std::string>("--strip-program", "");

  auto target_opt = ctx.get<std::string>("--target-directory", "");
  if (target_opt.empty()) {
    target_opt = ctx.get<std::string>("-t", "");
  }
  cfg.target_dir = target_opt;
  if (!cfg.target_dir.empty() && cfg.no_target_directory) {
    return std::unexpected(
        "cannot combine --target-directory (-t) and --no-target-directory "
        "(-T)\nTry 'install --help' for more information.");
  }

  if (ctx.positionals.empty()) {
    return std::unexpected(
        "missing file operand\nTry 'install --help' for more information.");
  }

  if (cfg.directory_mode) {
    for (auto arg : ctx.positionals) {
      cfg.sources.push_back(std::string(arg));
    }
    return cfg;
  }

  if (!cfg.target_dir.empty()) {
    for (auto arg : ctx.positionals) {
      append_source_operand(cfg, std::string(arg));
    }
    cfg.sources.push_back(cfg.target_dir);
    return cfg;
  }

  if (ctx.positionals.size() == 1) {
    // GNU: a single operand in copy mode is a missing destination.
    return std::unexpected("missing destination file operand after '" +
                           std::string(ctx.positionals[0]) +
                           "'\nTry 'install --help' for more information.");
  }

  for (size_t i = 0; i + 1 < ctx.positionals.size(); ++i) {
    append_source_operand(cfg, std::string(ctx.positionals[i]));
  }
  cfg.sources.push_back(std::string(ctx.positionals.back()));

  return cfg;
}

auto files_match(const std::string& lhs, const std::string& rhs) -> bool {
  std::error_code ec;
  if (!std::filesystem::exists(lhs, ec) || !std::filesystem::exists(rhs, ec)) {
    return false;
  }

  auto lhs_size = std::filesystem::file_size(lhs, ec);
  if (ec) return false;
  auto rhs_size = std::filesystem::file_size(rhs, ec);
  if (ec || lhs_size != rhs_size) return false;

  std::ifstream lhs_file(lhs, std::ios::binary);
  std::ifstream rhs_file(rhs, std::ios::binary);
  if (!lhs_file || !rhs_file) return false;

  constexpr size_t kBufferSize = 64 * 1024;
  std::array<char, kBufferSize> lhs_buf{};
  std::array<char, kBufferSize> rhs_buf{};

  while (lhs_file && rhs_file) {
    lhs_file.read(lhs_buf.data(), static_cast<std::streamsize>(lhs_buf.size()));
    rhs_file.read(rhs_buf.data(), static_cast<std::streamsize>(rhs_buf.size()));

    auto lhs_got = lhs_file.gcount();
    auto rhs_got = rhs_file.gcount();
    if (lhs_got != rhs_got) return false;
    if (std::memcmp(lhs_buf.data(), rhs_buf.data(),
                    static_cast<size_t>(lhs_got)) != 0) {
      return false;
    }

    if (lhs_got == 0) break;
  }

  return true;
}

auto preserve_timestamps(const std::string& source, const std::string& dest)
    -> bool {
  auto src_operand = native_path::make_api_path_operand(source);
  auto dst_operand = native_path::make_api_path_operand(dest);
  HANDLE hSource =
      CreateFileW(src_operand.extended.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hSource == INVALID_HANDLE_VALUE) {
    return false;
  }

  HANDLE hDest =
      CreateFileW(dst_operand.extended.c_str(), FILE_WRITE_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hDest == INVALID_HANDLE_VALUE) {
    CloseHandle(hSource);
    return false;
  }

  FILETIME creation{}, access{}, write{};
  bool ok = GetFileTime(hSource, &creation, &access, &write) != 0;
  if (ok) {
    ok = SetFileTime(hDest, &creation, &access, &write) != 0;
  }

  CloseHandle(hDest);
  CloseHandle(hSource);
  return ok;
}

auto run(const Config& cfg) -> int {
  auto mode_result = parse_install_mode(cfg.mode);
  if (!mode_result) {
    safeErrorPrint("install: ");
    safeErrorPrintLn(mode_result.error());
    return 1;
  }
  const ModeState desired_mode = *mode_result;

  // Warn about unsupported features on Windows
  if (!cfg.group.empty()) {
    safeErrorPrint("install: warning: --group is not supported on Windows\n");
  }
  if (!cfg.owner.empty()) {
    safeErrorPrint("install: warning: --owner is not supported on Windows\n");
  }
  if (cfg.preserve_context || cfg.default_context ||
      !cfg.selinux_context.empty()) {
    safeErrorPrint(
        "install: warning: SELinux context options are not supported on "
        "Windows\n");
  }

  if (cfg.directory_mode) {
    for (const auto& dir : cfg.sources) {
      std::vector<std::string> created;
      if (!create_directories_gnu(dir, &created)) {
        safeErrorPrint("install: cannot create directory '");
        safeErrorPrint(dir);
        safeErrorPrintLn("'");
        return 1;
      }
      if (cfg.verbose) {
        // GNU announces every component it actually creates.
        for (const auto& comp : created) {
          safePrint("install: creating directory '");
          safePrint(comp);
          safePrintLn("'");
        }
      }
      // [GNU] -m applies to the named directory itself (uutils#9302).
      if (!apply_mode_state(dir, desired_mode)) {
        safeErrorPrint("install: cannot change permissions of '");
        safeErrorPrint(dir);
        safeErrorPrintLn("'");
        return 1;
      }
    }
    return 0;
  }

  if (cfg.sources.size() < 2) {
    return 1;
  }

  SmallVector<std::string, 64> sources = cfg.sources;
  std::string target = sources.back();
  sources.pop_back();

  if (cfg.no_target_directory && sources.size() > 1) {
    safeErrorPrint("install: extra operand '" + sources[1] +
                   "'\nTry 'install --help' for more information.\n");
    return 1;
  }

  DWORD attrs = native_attributes(target);
  bool target_is_dir = !cfg.no_target_directory &&
                       (attrs != INVALID_FILE_ATTRIBUTES) &&
                       (attrs & FILE_ATTRIBUTE_DIRECTORY);
  if (!cfg.target_dir.empty()) {
    if (!target_is_dir && cfg.create_leading_dirs) {
      std::vector<std::string> created;
      create_directories_gnu(target, &created);
      attrs = native_attributes(target);
      target_is_dir = (attrs != INVALID_FILE_ATTRIBUTES) &&
                      (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    if (!target_is_dir) {
      // GNU: install -t reports "failed to access".
      safeErrorPrintLn("install: failed to access '" + target +
                       (attrs == INVALID_FILE_ATTRIBUTES
                            ? "': No such file or directory"
                            : "': Not a directory"));
      return 1;
    }
  }
  if (!target_is_dir && sources.size() > 1) {
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      safeErrorPrintLn("install: target '" + target +
                       "': No such file or directory");
    } else {
      safeErrorPrintLn("install: target '" + target + "': Not a directory");
    }
    return 1;
  }
  // GNU: install -T onto an existing directory is an error.
  if (cfg.no_target_directory && attrs != INVALID_FILE_ATTRIBUTES &&
      (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    safeErrorPrintLn("install: cannot overwrite directory '" + target +
                     "' with non-directory");
    return 1;
  }

  // [GNU] install.c continues over -t sources after a failure and exits
  // nonzero at the end.
  bool had_failure = false;
  for (const auto& source : sources) {
    std::string dest = target;

    if (target_is_dir) {
      size_t last_slash = source.find_last_of("/\\");
      std::string filename = (last_slash != std::string::npos)
                                 ? source.substr(last_slash + 1)
                                 : source;
      if (!dest.empty() && dest.back() != '\\' && dest.back() != '/') {
        dest += "\\";
      }
      dest += filename;
    }

    auto dest_owner_writable = file_owner_writable(dest);
    bool dest_mode_matches =
        dest_owner_writable.has_value() &&
        dest_owner_writable.value() == desired_mode.owner_write;
    if (cfg.compare && std::filesystem::exists(dest) &&
        files_match(source, dest) && dest_mode_matches) {
      if (cfg.verbose) {
        safeErrorPrint("install: skipping identical destination '");
        safeErrorPrint(dest);
        safeErrorPrintLn("'");
      }
      continue;
    }

    if (cfg.create_leading_dirs) {
      std::filesystem::path dest_path(dest);
      auto parent = dest_path.parent_path();
      if (!parent.empty()) {
        std::vector<std::string> created;
        if (!create_directories_gnu(parent.string(), &created)) {
          safeErrorPrint("install: cannot create directory '");
          safeErrorPrint(parent.string());
          safeErrorPrintLn("'");
          had_failure = true;
          continue;
        }
        if (cfg.verbose) {
          for (const auto& comp : created) {
            safePrint("install: creating directory '");
            safePrint(comp);
            safePrintLn("'");
          }
        }
      }
    }

    if (cfg.backup) {
      DWORD dest_attrs = native_attributes(dest);
      if (dest_attrs != INVALID_FILE_ATTRIBUTES) {
        std::string backup_path = dest + cfg.backup_suffix;
        auto dest_operand = native_path::make_api_path_operand(dest);
        auto backup_operand = native_path::make_api_path_operand(backup_path);
        if (MoveFileExW(dest_operand.extended.c_str(),
                        backup_operand.extended.c_str(),
                        MOVEFILE_REPLACE_EXISTING)) {
          if (cfg.verbose) {
            safePrintLn("(backup: '" + backup_path + "')");
          }
        }
      }
    }

    if (cfg.verbose) {
      // GNU: 'src' -> 'dest'
      safePrint("'");
      safePrint(source);
      safePrint("' -> '");
      safePrint(dest);
      safePrintLn("'");
    }

    if (is_stdin_alias(source)) {
      // [GNU] install copies /dev/stdin contents like a regular file
      // (uutils#12407).
      if (!copy_stdin_to_dest(dest)) {
        safeErrorPrintLn("install: cannot create regular file '" + dest + "'");
        had_failure = true;
        continue;
      }
    } else {
      DWORD src_attrs = native_attributes(source);
      if (src_attrs == INVALID_FILE_ATTRIBUTES) {
        safeErrorPrintLn("install: cannot stat '" + source +
                         "': No such file or directory");
        had_failure = true;
        continue;
      }
      auto src_operand = native_path::make_api_path_operand(source);
      auto dst_operand = native_path::make_api_path_operand(dest);
      if (!CopyFileW(src_operand.extended.c_str(), dst_operand.extended.c_str(),
                     FALSE)) {
        safeErrorPrintLn("install: cannot create regular file '" + dest +
                         "': " + win32_posix_error_text(GetLastError()));
        had_failure = true;
        continue;
      }
    }

    if (cfg.preserve_timestamps && !preserve_timestamps(source, dest)) {
      safeErrorPrint("install: cannot preserve timestamps for '");
      safeErrorPrint(dest);
      safeErrorPrintLn("'");
      had_failure = true;
      continue;
    }

    // GNU install always applies the final mode after copying.  Windows has no
    // group/other mode bits, so map the final owner-write bit to ReadOnly.
    if (!apply_mode_state(dest, desired_mode)) {
      safeErrorPrint("install: cannot change permissions of '");
      safeErrorPrint(dest);
      safeErrorPrintLn("'");
      had_failure = true;
      continue;
    }
    if (cfg.verbose && !cfg.mode.empty()) {
      safePrint("install: set mode '");
      safePrint(cfg.mode);
      safePrint("' on '");
      safePrint(dest);
      safePrintLn("'");
    }

    // Strip symbol tables if requested (Windows: call strip.exe if available)
    // [SECURITY] Launch strip via CreateProcessW with proper argv quoting
    // instead of std::system: a dest path containing a double quote must not
    // be able to break out of the command line (command injection).
    if (cfg.strip) {
      const std::string strip_name =
          cfg.strip_program.empty() ? std::string("strip") : cfg.strip_program;
      std::wstring strip_command_line;
      append_windows_command_arg(strip_command_line,
                                 utf8_to_wstring(strip_name));
      append_windows_command_arg(strip_command_line, utf8_to_wstring(dest));

      STARTUPINFOW si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      if (CreateProcessW(nullptr, strip_command_line.data(), nullptr, nullptr,
                         FALSE, 0, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD strip_exit_code = 1;
        GetExitCodeProcess(pi.hProcess, &strip_exit_code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (strip_exit_code != 0 && cfg.verbose) {
          safePrint("install: warning: strip failed for '");
          safePrint(dest);
          safePrintLn("'");
        }
      } else if (cfg.verbose) {
        safePrint("install: warning: strip failed for '");
        safePrint(dest);
        safePrintLn("'");
      }
    }
  }

  return had_failure ? 1 : 0;
}

}  // namespace install_pipeline

REGISTER_COMMAND(
    install, "install",
    "install [OPTION]... [-T] SOURCE DEST\n"
    "  install [OPTION]... SOURCE... DIRECTORY\n"
    "  install [OPTION]... -t DIRECTORY SOURCE...",
    "Copy files and set attributes.\n"
    "\n"
    "Note: This Windows implementation supports copying, compare-and-skip,\n"
    "timestamp preservation, and selected destination handling. Ownership,\n"
    "group, strip, and SELinux context handling remain limited on Windows.",
    "  install source.txt dest.txt\n"
    "  install -b file.txt backup/\n"
    "  install -v src/*.txt /target/\n"
    "  install -d /tmp/dir",
    "cp(1), mv(1)", "WinuxCmd", "Copyright ? 2026 WinuxCmd", INSTALL_OPTIONS) {
  using namespace install_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"install");
    return 1;
  }

  return run(*cfg_result);
}
