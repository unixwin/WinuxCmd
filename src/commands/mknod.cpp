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

auto constexpr MKNOD_OPTIONS = std::array{
    OPTION("-m", "--mode", "set file permission bits", STRING_TYPE),
    OPTION("-Z", "", "set the SELinux security context to default type"),
    OPTION("", "--context", "set the SELinux security context",
           OPTIONAL_STRING_TYPE),
};

namespace mknod_pipeline {
namespace cp = core::pipeline;

enum class NodeType { Block, Character, Fifo };

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

auto parse_node_type(std::string_view type) -> cp::Result<NodeType> {
  if (type.empty()) {
    return std::unexpected("missing device type");
  }

  switch (type.front()) {
    case 'b':
      return NodeType::Block;
    case 'c':
    case 'u':
      return NodeType::Character;
    case 'p':
      return NodeType::Fifo;
    default:
      return std::unexpected("invalid device type '" + std::string(type) + "'");
  }
}

auto parse_uint_arg(std::string_view arg, std::string_view label)
    -> cp::Result<unsigned long long> {
  if (arg.empty()) {
    return std::unexpected("missing " + std::string(label));
  }
  unsigned long long value = 0;
  auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
  if (ec != std::errc() || ptr != arg.data() + arg.size()) {
    return std::unexpected("invalid " + std::string(label) + " '" +
                           std::string(arg) + "'");
  }
  return value;
}

struct Config {
  std::string name;
  NodeType type = NodeType::Fifo;
};

auto build_config(const CommandContext<MKNOD_OPTIONS.size()>& ctx)
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

  if (ctx.positionals.size() < 2) {
    return std::unexpected("missing operand");
  }

  cfg.name = std::string(ctx.positionals[0]);
  auto type_result = parse_node_type(ctx.positionals[1]);
  if (!type_result) return std::unexpected(type_result.error());
  cfg.type = *type_result;

  if (cfg.type == NodeType::Fifo) {
    if (ctx.positionals.size() > 2) {
      return std::unexpected("fifo type does not accept major and minor device numbers");
    }
    return cfg;
  }

  if (ctx.positionals.size() < 4) {
    return std::unexpected("special file type requires major and minor device numbers");
  }

  auto major_result = parse_uint_arg(ctx.positionals[2], "major");
  if (!major_result) return std::unexpected(major_result.error());
  auto minor_result = parse_uint_arg(ctx.positionals[3], "minor");
  if (!minor_result) return std::unexpected(minor_result.error());

  return cfg;
}

auto run(const Config& cfg) -> int {
  std::filesystem::path p(cfg.name);
  std::error_code ec;
  if (std::filesystem::exists(p, ec)) {
    safeErrorPrint("mknod: cannot create special file '");
    safeErrorPrint(cfg.name);
    safeErrorPrint("': File exists\n");
    return 1;
  }

  safeErrorPrint("mknod: cannot create special file '");
  safeErrorPrint(cfg.name);
  safeErrorPrint("': special files are not supported on Windows\n");
  return 1;
}

}  // namespace mknod_pipeline

REGISTER_COMMAND(
    mknod, "mknod", "mknod [OPTION]... NAME TYPE [MAJOR MINOR]",
    "Create the special file NAME of the given TYPE.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for mknod, but\n"
    "Windows does not provide POSIX special files or device nodes equivalent\n"
    "to `mknod`. This command therefore acts as a compatibility placeholder\n"
    "and reports that special files are not supported on Windows.",
    "  mknod mypipe p\n"
    "  mknod ttyS0 c 4 64\n"
    "  mknod sda b 8 0",
    "mkfifo(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MKNOD_OPTIONS) {
  using namespace mknod_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("mknod: ");
    safeErrorPrint(cfg_result.error());
    safeErrorPrint("\n");
    return 1;
  }

  return run(*cfg_result);
}
