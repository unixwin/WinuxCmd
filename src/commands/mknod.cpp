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

// [GNU] mode_compile + the permission-bits gate (mknod.c:154-162),
// shared in shape with mkfifo: apply to a 0666 base, then any bit
// outside S_IRWXUGO is fatal. The old sieve accepted any alphanumeric
// string ("bogus9" passed) as a mode.
auto evaluate_mode(std::string_view mode) -> cp::Result<unsigned> {
  if (mode.empty()) return std::unexpected("invalid mode");

  bool octal = std::ranges::all_of(mode, [](char ch) {
    return ch >= '0' && ch <= '7';
  });
  if (octal) {
    unsigned long long value = 0;
    for (char ch : mode) value = value * 8 + static_cast<unsigned>(ch - '0');
    if (value > 07777ULL) {
      return std::unexpected("mode must specify only file permission bits");
    }
    return static_cast<unsigned>(value);
  }

  unsigned cur = 0666;
  size_t start = 0;
  while (true) {
    const size_t comma = mode.find(',', start);
    const std::string_view clause =
        mode.substr(start, comma == std::string_view::npos
                               ? std::string_view::npos
                               : comma - start);
    if (clause.empty()) return std::unexpected("invalid mode");

    size_t j = 0;
    bool who_u = false, who_g = false, who_o = false;
    while (j < clause.size() &&
           (clause[j] == 'u' || clause[j] == 'g' || clause[j] == 'o' ||
            clause[j] == 'a')) {
      if (clause[j] == 'u' || clause[j] == 'a') who_u = true;
      if (clause[j] == 'g' || clause[j] == 'a') who_g = true;
      if (clause[j] == 'o' || clause[j] == 'a') who_o = true;
      ++j;
    }
    if (j == 0) {
      who_u = who_g = who_o = true;
    }

    bool any_op = false;
    while (j < clause.size()) {
      const char op = clause[j];
      if (op != '+' && op != '-' && op != '=') {
        return std::unexpected("invalid mode");
      }
      ++j;
      const size_t perm_begin = j;
      while (j < clause.size() &&
             std::string_view("rwxXstugo").find(clause[j]) !=
                 std::string_view::npos) {
        ++j;
      }
      const std::string_view perms = clause.substr(perm_begin, j - perm_begin);
      any_op = true;

      const bool who[3] = {who_u, who_g, who_o};
      for (int cls = 0; cls < 3; ++cls) {
        if (!who[cls]) continue;
        const int shift = 6 - 3 * cls;
        unsigned bits = 0;
        for (char ch : perms) {
          switch (ch) {
            case 'r': bits |= 4; break;
            case 'w': bits |= 2; break;
            case 'x': bits |= 1; break;
            case 'X': bits |= (cur & 0111) != 0 ? 1 : 0; break;
            case 'u': bits |= (cur >> 6) & 7; break;
            case 'g': bits |= (cur >> 3) & 7; break;
            case 'o': bits |= cur & 7; break;
            default: break;
          }
        }
        if (op == '+') {
          cur |= bits << shift;
        } else if (op == '-') {
          cur &= ~(bits << shift);
        } else {
          cur = (cur & ~(7U << shift)) | (bits << shift);
        }
      }
      unsigned special = 0;
      for (char ch : perms) {
        if (ch == 's') {
          if (who_u) special |= 04000;
          if (who_g) special |= 02000;
        } else if (ch == 't') {
          special |= 01000;
        }
      }
      if (op == '-') {
        cur &= ~special;
      } else {
        cur |= special;
      }
    }
    if (!any_op) return std::unexpected("invalid mode");

    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }

  if ((cur & ~0777U) != 0) {
    return std::unexpected("mode must specify only file permission bits");
  }
  return cur;
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

// [GNU] MAJOR/MINOR parse with xstrtoumax base 0 (mknod.c:236-245):
// 0x is hex, a leading 0 is octal, otherwise decimal. Failures read
// "invalid major device number '<arg>'" / minor likewise.
auto parse_uint_arg(std::string_view arg, std::string_view label)
    -> cp::Result<unsigned long long> {
  if (arg.empty()) {
    return std::unexpected(make_error("missing " + std::string(label)));
  }
  int base = 10;
  std::string_view digits = arg;
  if (arg.size() > 2 && arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
    base = 16;
    digits = arg.substr(2);
  } else if (arg.size() > 1 && arg[0] == '0') {
    base = 8;
    digits = arg.substr(1);
  }
  unsigned long long value = 0;
  auto [ptr, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), value,
                      base);
  if (ec != std::errc() || ptr != digits.data() + digits.size()) {
    return std::unexpected(make_error("invalid " + std::string(label) +
                                      " '" + std::string(arg) + "'"));
  }
  return value;
}

struct Config {
  std::string name;
  NodeType type = NodeType::Fifo;
  bool has_mode = false;
  unsigned mode_value = 0;
  std::string context_value;
};

auto build_config(const CommandContext<MKNOD_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (ctx.has("-m") || ctx.has("--mode")) {
    std::string mode = ctx.get<std::string>("--mode", "");
    if (mode.empty()) mode = ctx.get<std::string>("-m", "");
    auto evaluated = evaluate_mode(mode);
    if (!evaluated) return std::unexpected(evaluated.error());
    cfg.has_mode = true;
    cfg.mode_value = *evaluated;
  }

  cfg.context_value = ctx.get<std::string>("--context", "");

  if (ctx.positionals.size() < 2) {
    if (ctx.positionals.empty()) {
      return std::unexpected("missing operand");
    }
    return std::unexpected(make_error(
        "missing operand after '" + std::string(ctx.positionals.back()) + "'"));
  }

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

  if (ctx.positionals.size() > 4) {
    // [GNU] mknod.c:183-191: the first extra operand is named.
    return std::unexpected(make_error(
        "extra operand '" + std::string(ctx.positionals[4]) + "'"));
  }

  if (ctx.positionals.size() < 4) {
    // [GNU] The "Special files require..." note appears only when
    // exactly NAME and TYPE were given (mknod.c:177-179).
    std::string message = "missing operand after '" +
                          std::string(ctx.positionals.back()) + "'";
    if (ctx.positionals.size() == 2) {
      message += "\nSpecial files require major and minor device numbers.";
    }
    return std::unexpected(make_error(std::move(message)));
  }

  auto major_result =
      parse_uint_arg(ctx.positionals[2], "major device number");
  if (!major_result) return std::unexpected(major_result.error());
  auto minor_result =
      parse_uint_arg(ctx.positionals[3], "minor device number");
  if (!minor_result) return std::unexpected(minor_result.error());

  return cfg;
}

auto run(const Config& cfg) -> int {
  // [GNU] Non-SELinux warn-and-create for --context=CTX (mknod.c:
  // 131-140); a bare -Z is silent. This used to hard-fail.
  if (!cfg.context_value.empty()) {
    safeErrorPrint(
        "mknod: warning: ignoring --context; it requires an "
        "SELinux/SMACK-enabled kernel\n");
  }

  // #1235: fold the POSIX drive dialect ("/e/n", "/cygdrive/e/n", "/mnt/e/n")
  // through the shared boundary before any filesystem call; Win32 resolves a
  // leading "/e" against the CURRENT drive, so from a C: cwd the existence
  // probe answered for "<cwd>:\e\n" and a fifo node would land beside the
  // cwd.  `name` stays verbatim in every message, like MSYS.
  const auto operand = native_path::make_api_path_operand(cfg.name);
  if (native_path::operand_target_attributes_w(operand) !=
      INVALID_FILE_ATTRIBUTES) {
    // [GNU] quotef: the name is quoted (mknod.c:256-257).
    safeErrorPrint("mknod: '");
    safeErrorPrint(cfg.name);
    safeErrorPrint("': File exists\n");
    return 1;
  }

  if (cfg.type == NodeType::Fifo) {
    // [GNU] -m applies to the fifo too (mknod.c:264, 273): a mode
    // without any write bit maps to the readonly attribute. It used
    // to be validated and then ignored.
    const bool readonly = cfg.has_mode && (cfg.mode_value & 0222) == 0;
    const DWORD created =
        native_path::create_winux_fifo_w(operand.normalized, readonly);
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
        cfg_result.error().starts_with("extra operand") ||
        cfg_result.error().starts_with("invalid device type")) {
      safeErrorPrint("Try 'mknod --help' for more information.\n");
    }
    return 1;
  }

  return run(*cfg_result);
}
