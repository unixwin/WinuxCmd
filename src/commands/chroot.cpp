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

auto constexpr CHROOT_OPTIONS = std::array{
    OPTION("", "--groups", "specify supplementary groups", STRING_TYPE),
    OPTION("", "--userspec", "specify user and group", STRING_TYPE),
    OPTION("", "--skip-chdir", "do not change working directory to '/'"),
};

namespace chroot_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string newroot;
};

auto build_config(const CommandContext<CHROOT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  (void)ctx.get<std::string>("--groups", "");
  (void)ctx.get<std::string>("--userspec", "");
  (void)ctx.get<bool>("--skip-chdir", false);

  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  cfg.newroot = std::string(ctx.positionals[0]);
  std::filesystem::path root_path(cfg.newroot);

  std::error_code ec;
  bool exists = std::filesystem::exists(root_path, ec);
  if (ec || !exists || !std::filesystem::is_directory(root_path, ec)) {
    return std::unexpected("cannot change root directory to '" + cfg.newroot +
                           "': no such directory");
  }

  return cfg;
}

auto run(const Config&) -> int {
  safeErrorPrint("chroot: changing the root directory is not supported on ");
  safeErrorPrint("Windows\n");
  return 125;
}

}  // namespace chroot_pipeline

REGISTER_COMMAND(
    chroot, "chroot",
    "chroot [OPTION] NEWROOT [COMMAND [ARG]...]",
    "Run COMMAND with root directory set to NEWROOT.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for chroot, but\n"
    "Windows does not provide a safe equivalent to POSIX chroot semantics.\n"
    "This command therefore acts as a compatibility placeholder and reports\n"
    "that changing the root directory is not supported on Windows.",
    "  chroot C:\\jail cmd.exe /c echo hi\n"
    "  chroot --userspec=user:group C:\\jail cmd.exe /c echo hi\n"
    "  chroot --skip-chdir C:\\jail cmd.exe /c echo hi",
    "runcon(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CHROOT_OPTIONS) {
  using namespace chroot_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("chroot: ");
    safeErrorPrint(cfg_result.error());
    safeErrorPrint("\n");
    return 125;
  }

  return run(*cfg_result);
}
