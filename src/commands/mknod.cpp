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
    // [DIFFERS]
    OPTION("-m", "--mode", "set file permission bits", STRING_TYPE),
    // [DIFFERS]
    OPTION("-Z", "", "set the SELinux security context to default type"),
    // [DIFFERS]
    OPTION("", "--context", "set the SELinux security context",
           OPTIONAL_STRING_TYPE),
};

namespace mknod_pipeline {
namespace cp = core::pipeline;

auto make_error(std::string message) -> cp::Error {
  static thread_local std::string storage;
  storage = std::move(message);
  return storage;
}

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
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '+' ||
        ch == '-' || ch == '=' || ch == ',' || ch == 'X') {
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
      return std::unexpected(
          make_error("invalid device type '" + std::string(type) + "'"));
  }
}

auto parse_uint_arg(std::string_view arg, std::string_view label)
    -> cp::Result<unsigned long long> {
  if (arg.empty()) {
    return std::unexpected(make_error("missing " + std::string(label)));
  }
  unsigned long long value = 0;
  auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
  if (ec != std::errc() || ptr != arg.data() + arg.size()) {
    return std::unexpected(make_error("invalid " + std::string(label) + " '" +
                                      std::string(arg) + "'"));
  }
  return value;
}

struct Config {
  std::string name;
  NodeType type = NodeType::Fifo;
  std::string mode;
  bool context_requested = false;
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
    cfg.mode = mode;
  }

  const bool context_requested =
      ctx.get<bool>("-Z", false) || ctx.has("--context");

  if (ctx.positionals.size() < 2) {
    if (ctx.positionals.empty()) {
      return std::unexpected("missing operand");
    }
    return std::unexpected(make_error(
        "missing operand after '" + std::string(ctx.positionals.back()) + "'"));
  }

  cfg.context_requested = context_requested;
  cfg.name = std::string(ctx.positionals[0]);
  auto type_result = parse_node_type(ctx.positionals[1]);
  if (!type_result) return std::unexpected(type_result.error());
  cfg.type = *type_result;

  if (cfg.type == NodeType::Fifo) {
    if (ctx.positionals.size() > 2) {
      return std::unexpected(
          make_error("extra operand '" + std::string(ctx.positionals[2]) +
                     "'\nFifos do not have major and minor device numbers."));
    }
    return cfg;
  }

  if (ctx.positionals.size() < 4) {
    return std::unexpected(make_error(
        "missing operand after '" + std::string(ctx.positionals.back()) +
        "'\nspecial files requires major and minor device numbers."));
  }

  auto major_result = parse_uint_arg(ctx.positionals[2], "major");
  if (!major_result) return std::unexpected(major_result.error());
  auto minor_result = parse_uint_arg(ctx.positionals[3], "minor");
  if (!minor_result) return std::unexpected(minor_result.error());

  return cfg;
}

auto run(const Config& cfg) -> int {
  if (cfg.context_requested) {
    safeErrorPrintLn(winux::i18n::translate(
        "command.mknod.error.context-unsupported",
        "mknod: SELinux security contexts are not supported on Windows"));
    return 1;
  }

  std::filesystem::path p(cfg.name);
  std::error_code ec;
  if (std::filesystem::exists(p, ec)) {
    safeErrorPrint("mknod: ");
    safeErrorPrint(cfg.name);
    safeErrorPrint(": File exists\n");
    return 1;
  }

  if (cfg.type == NodeType::Fifo) {
    const DWORD created =
        native_path::create_winux_fifo_w(utf8_to_wstring(cfg.name));
    if (created != ERROR_SUCCESS) {
      safeErrorPrint("mknod: '");
      safeErrorPrint(cfg.name);
      safeErrorPrint("': ");
      safeErrorPrint(win32_posix_error_text(
          created, {.file_exists = true, .invalid_name_as_missing = true}));
      safeErrorPrint("\n");
      return 1;
    }
    return 0;
  }

  // Block/character device nodes have no Windows equivalent.
  safeErrorPrint("mknod: '");
  safeErrorPrint(cfg.name);
  safeErrorPrint("': Operation not permitted\n");
  return 1;
}

}  // namespace mknod_pipeline

REGISTER_COMMAND(
    mknod, "mknod", "mknod [OPTION]... NAME TYPE [MAJOR MINOR]",
    "Create the special file NAME of the given TYPE.\n"
    "\n"
    "Type 'p' creates a FIFO: on Windows this is an on-disk marker file\n"
    "that WinuxCmd commands bridge to a named pipe. Block and character\n"
    "device nodes have no Windows equivalent and report an error.",
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
    if (cfg_result.error().starts_with("missing operand") ||
        cfg_result.error().starts_with("extra operand")) {
      safeErrorPrint("Try 'mknod --help' for more information.\n");
    }
    return 1;
  }

  return run(*cfg_result);
}
