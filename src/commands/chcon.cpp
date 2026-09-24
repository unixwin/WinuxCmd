/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CHCON_OPTIONS = std::array{
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--dereference", "affect the referent of each symbolic link"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of referenced files"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--reference", "use RFILE's security context", STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-u", "--user", "set user component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-r", "--role", "set role component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-t", "--type", "set type component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-l", "--range", "set range component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-H", "", "follow command-line symbolic links to directories"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-L", "", "follow every symbolic link to a directory"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-P", "", "do not follow symbolic links"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
};

namespace chcon_pipeline {
namespace cp = core::pipeline;

auto make_error(std::string message) -> cp::Error {
  static thread_local std::string storage;
  storage = std::move(message);
  return storage;
}

struct Config {
  bool verbose = false;
  std::vector<std::string> files;
};

auto add_file_args(Config& cfg, std::span<const std::string_view> args)
    -> void {
  for (auto arg : args) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }
}

auto build_config(const CommandContext<CHCON_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);

  // [DIFFERS] - SELinux is unavailable on Windows; these flags are parsed
  // for GNU-compatible surface but have no runtime effect.
  (void)ctx.get<bool>("--dereference", false);
  (void)ctx.get<bool>("-h", false);
  (void)ctx.get<bool>("--no-dereference", false);
  (void)ctx.get<bool>("--preserve-root", false);
  (void)ctx.get<bool>("--no-preserve-root", false);
  (void)ctx.get<bool>("-R", false);
  (void)ctx.get<bool>("--recursive", false);
  (void)ctx.get<bool>("-H", false);
  (void)ctx.get<bool>("-L", false);
  (void)ctx.get<bool>("-P", false);

  bool using_reference = ctx.has("--reference");
  bool using_components = ctx.has("-u") || ctx.has("--user") || ctx.has("-r") ||
                          ctx.has("--role") || ctx.has("-t") ||
                          ctx.has("--type") || ctx.has("-l") ||
                          ctx.has("--range");

  if (using_reference || using_components) {
    if (ctx.positionals.empty()) {
      return std::unexpected(winux::i18n::translate(
          "command.chcon.error.missing_file_operand", "missing file operand"));
    }
    add_file_args(cfg, std::span<const std::string_view>(
                           ctx.positionals.data(), ctx.positionals.size()));
  } else {
    if (ctx.positionals.empty()) {
      return std::unexpected(winux::i18n::translate(
          "command.chcon.error.missing_operand", "missing operand"));
    }
    if (ctx.positionals.size() < 2) {
      return std::unexpected(
          winux::i18n::translate("command.chcon.error.missing_operand_after",
                                 std::string("missing operand after '") +
                                     std::string(ctx.positionals[0]) + "'"));
    }
    add_file_args(
        cfg, std::span<const std::string_view>(ctx.positionals.data() + 1,
                                               ctx.positionals.size() - 1));
  }

  if (cfg.files.empty()) {
    return std::unexpected(winux::i18n::translate(
        "command.chcon.error.missing_file_operand", "missing file operand"));
  }

  for (const auto& file : cfg.files) {
    std::wstring wfile = utf8_to_wstring(file);
    if (GetFileAttributesW(wfile.c_str()) == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected(
          winux::i18n::translate("command.chcon.error.cannot_access",
                                 std::string("cannot access '") + file +
                                     "': No such file or directory"));
    }
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  if (cfg.verbose) {
    for (const auto& file : cfg.files) {
      safeErrorPrintLn(winux::i18n::translate(
          "command.chcon.error.unsupported_verbose",
          std::string("chcon: ") + file +
              ": SELinux file contexts are not supported on Windows"));
    }
  } else {
    safeErrorPrintLn(winux::i18n::translate(
        "command.chcon.error.unsupported",
        "chcon: SELinux file contexts are not supported on Windows"));
  }
  return 1;
}

}  // namespace chcon_pipeline

REGISTER_COMMAND(
    chcon, "chcon",
    "chcon [OPTION]... CONTEXT FILE...\n"
    "  or:  chcon [OPTION]... [-u USER] [-r ROLE] [-l RANGE] [-t TYPE] "
    "FILE...\n"
    "  or:  chcon [OPTION]... --reference=RFILE FILE...",
    "Change the SELinux security context of each FILE.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for chcon, but\n"
    "Windows does not provide SELinux file contexts. This command therefore\n"
    "acts as a compatibility command and reports that the operation is "
    "not\n"
    "supported on Windows.",
    "  chcon system_u:object_r:httpd_sys_content_t:s0 file.txt\n"
    "  chcon -R --type=httpd_sys_content_t public_html\n"
    "  chcon --reference=reference.txt target.txt",
    "chmod(1), chown(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CHCON_OPTIONS) {
  using namespace chcon_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("chcon: ");
    safeErrorPrint(winux::i18n::translate_error(cfg_result.error()));
    safeErrorPrint("\n");
    safeErrorPrintLn(winux::i18n::format(
        "common.try_help", "Try '{} --help' for more information.", "chcon"));
    return 1;
  }

  return run(*cfg_result);
}
