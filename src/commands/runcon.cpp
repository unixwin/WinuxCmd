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

auto constexpr RUNCON_OPTIONS = std::array{
    OPTION("-c", "--compute",
           "compute process transition context before modifying"),
    OPTION("-u", "--user", "set user component of the context", STRING_TYPE),
    OPTION("-r", "--role", "set role component of the context", STRING_TYPE),
    OPTION("-t", "--type", "set type component of the context", STRING_TYPE),
    OPTION("-l", "--range", "set range component of the context", STRING_TYPE),
};

namespace runcon_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool has_plain_context = false;
  bool has_custom_context = false;
  std::string context;
  std::string command;
};

auto build_config(const CommandContext<RUNCON_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.has_custom_context =
      ctx.has("-c") || ctx.has("--compute") || ctx.has("-u") ||
      ctx.has("--user") || ctx.has("-r") || ctx.has("--role") || ctx.has("-t") ||
      ctx.has("--type") || ctx.has("-l") || ctx.has("--range");

  if (cfg.has_custom_context) {
    if (!ctx.positionals.empty()) {
      cfg.command = std::string(ctx.positionals[0]);
    }
    return cfg;
  }

  if (ctx.positionals.empty()) {
    return cfg;
  }

  cfg.has_plain_context = true;
  cfg.context = std::string(ctx.positionals[0]);

  if (ctx.positionals.size() < 2) {
    return std::unexpected("missing command after '" + cfg.context + "'");
  }

  cfg.command = std::string(ctx.positionals[1]);
  return cfg;
}

auto run(const Config&) -> int {
  safeErrorPrint("runcon: SELinux process contexts are not supported on ");
  safeErrorPrint("Windows\n");
  return 1;
}

}  // namespace runcon_pipeline

REGISTER_COMMAND(
    runcon, "runcon",
    "runcon CONTEXT COMMAND [ARG]...\n"
    "  or:  runcon [-c] [-u USER] [-r ROLE] [-t TYPE] [-l RANGE] [COMMAND [ARG]...]",
    "Run a program in a different SELinux security context.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for runcon, but\n"
    "Windows does not provide SELinux process contexts. This command therefore\n"
    "acts as a compatibility placeholder and reports that the operation is not\n"
    "supported on Windows.",
    "  runcon system_u:system_r:httpd_t:s0 cmd.exe /c echo hi\n"
    "  runcon -t httpd_t cmd.exe /c echo hi\n"
    "  runcon -c -u system_u -r system_r -t httpd_t cmd.exe /c echo hi",
    "chcon(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", RUNCON_OPTIONS) {
  using namespace runcon_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("runcon: ");
    safeErrorPrint(cfg_result.error());
    safeErrorPrint("\n");
    return 1;
  }

  return run(*cfg_result);
}
