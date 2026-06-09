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

auto constexpr MKFIFO_OPTIONS = std::array{
    OPTION("-m", "--mode", "set file permission bits", STRING_TYPE),
    OPTION("-Z", "", "set the SELinux security context to default type"),
    OPTION("", "--context", "set the SELinux security context",
           OPTIONAL_STRING_TYPE),
};

namespace mkfifo_pipeline {
namespace cp = core::pipeline;

auto is_plausible_mode(std::string_view mode) -> bool {
  if (mode.empty()) return false;

  bool octal = true;
  for (char ch : mode) {
    if (ch < '0' || ch > '7') {
      octal = false;
      break;
    }
  }
  if (octal) return true;

  for (char ch : mode) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '+' || ch == '-' ||
        ch == '=' || ch == ',' || ch == 'X') {
      continue;
    }
    return false;
  }
  return true;
}

struct Config {
  std::vector<std::string> fifos;
};

auto add_fifo_args(Config& cfg, std::span<const std::string_view> args) -> void {
  for (auto arg : args) {
    cfg.fifos.emplace_back(arg);
  }
}

auto build_config(const CommandContext<MKFIFO_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (ctx.has("-m") || ctx.has("--mode")) {
    std::string mode = ctx.get<std::string>("--mode", "");
    if (mode.empty()) mode = ctx.get<std::string>("-m", "");
    if (!is_plausible_mode(mode)) {
      return std::unexpected("invalid mode");
    }
  }

  (void)ctx.get<bool>("-Z", false);
  (void)ctx.get<std::string>("--context", "");

  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  add_fifo_args(
      cfg, std::span<const std::string_view>(ctx.positionals.data(),
                                             ctx.positionals.size()));

  return cfg;
}

auto run(const Config& cfg) -> int {
  int exit_code = 0;
  for (const auto& fifo : cfg.fifos) {
    std::filesystem::path p(fifo);
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
      safeErrorPrint("mkfifo: cannot create fifo '");
      safeErrorPrint(fifo);
      safeErrorPrint("': File exists\n");
      exit_code = 1;
      continue;
    }

    safeErrorPrint("mkfifo: cannot create fifo '");
    safeErrorPrint(fifo);
    safeErrorPrint("': filesystem FIFOs are not supported on Windows\n");
    exit_code = 1;
  }
  return exit_code;
}

}  // namespace mkfifo_pipeline

REGISTER_COMMAND(
    mkfifo, "mkfifo", "mkfifo [OPTION]... NAME...",
    "Create named pipes (FIFOs) with the given NAMEs.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for mkfifo, but\n"
    "Windows does not provide filesystem FIFOs equivalent to POSIX named\n"
    "pipes. This command therefore acts as a compatibility placeholder and\n"
    "reports that filesystem FIFOs are not supported on Windows.",
    "  mkfifo mypipe\n"
    "  mkfifo -m 600 mypipe\n"
    "  mkfifo --context system_u:object_r:fifo_file_t:s0 mypipe",
    "mknod(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MKFIFO_OPTIONS) {
  using namespace mkfifo_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("mkfifo: ");
    safeErrorPrint(cfg_result.error());
    safeErrorPrint("\n");
    return 1;
  }

  return run(*cfg_result);
}
