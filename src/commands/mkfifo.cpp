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
    // [GNU]
    OPTION("-m", "--mode", "set file permission bits", STRING_TYPE),
    // [GNU] -Z pairs with --context[=CTX] (system.h:344-345); the old
    // declaration invented a --security-context long name and a
    // separate argument-less --context flag.
    OPTION("-Z", "--context", "set the SELinux security context",
           OPTIONAL_STRING_TYPE),
};

namespace mkfifo_pipeline {
namespace cp = core::pipeline;

// [GNU] mode_compile + the permission-bits gate (mkfifo.c:151-165):
// the mode is applied to a 0666 base, and any resulting bit outside
// S_IRWXUGO is fatal. The old plausibility sieve mapped readonly by
// searching the TEXT for digits ("0400" contains '0' and was treated
// as writable) and rejected multi-op and copy clauses outright.
// Symbolic evaluation: clauses of [ugoa]* followed by one or more
// (op, perms) pairs; perms may copy another class (g=u); 's'/'t'
// raise the special bits the gate then rejects. An omitted who is
// treated as 'a' (Windows has no umask to consult).
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

      auto class_bits = [&](int cls) -> unsigned {
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
            default: break;  // 's'/'t' handled as special bits below
          }
        }
        (void)cls;
        return bits;
      };
      const bool who[3] = {who_u, who_g, who_o};
      for (int cls = 0; cls < 3; ++cls) {
        if (!who[cls]) continue;
        const int shift = 6 - 3 * cls;
        const unsigned bits = class_bits(cls);
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

struct Config {
  std::vector<std::string> fifos;
  bool has_mode = false;
  unsigned mode_value = 0;
  std::string context_value;
};

auto add_fifo_args(Config& cfg, std::span<const std::string_view> args)
    -> void {
  for (auto arg : args) {
    cfg.fifos.emplace_back(arg);
  }
}

auto build_config(const CommandContext<MKFIFO_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  // [GNU] The missing-operand check precedes mode compilation
  // (mkfifo.c:133 vs 154-157).
  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  if (ctx.has("-m") || ctx.has("--mode")) {
    std::string mode = ctx.get<std::string>("--mode", "");
    if (mode.empty()) mode = ctx.get<std::string>("-m", "");
    auto evaluated = evaluate_mode(mode);
    if (!evaluated) return std::unexpected(evaluated.error());
    cfg.has_mode = true;
    cfg.mode_value = *evaluated;
  }

  cfg.context_value = ctx.get<std::string>("--context", "");
  if (cfg.context_value.empty()) {
    cfg.context_value = ctx.get<std::string>("-Z", "");
  }
  add_fifo_args(cfg, std::span<const std::string_view>(ctx.positionals.data(),
                                                       ctx.positionals.size()));

  return cfg;
}

auto run(const Config& cfg) -> int {
  // [GNU] On a kernel without SELinux, --context=CTX warns and the
  // fifo is still created; a bare -Z is silently ignored
  // (mkfifo.c:118-122). This used to be a hard failure that created
  // nothing.
  if (!cfg.context_value.empty()) {
    safeErrorPrint(
        "mkfifo: warning: ignoring --context; it requires an "
        "SELinux/SMACK-enabled kernel\n");
  }

  int exit_code = 0;
  for (const auto& fifo : cfg.fifos) {
    // #1235: the operand may use any accepted POSIX drive spelling
    // ("/e/p", "/cygdrive/e/p", "/mnt/e/p").  Handing that raw text to the
    // existence probe / create_winux_fifo_w makes Win32 resolve a leading
    // "/e" against the CURRENT drive, so from a C: cwd `mkfifo /e/p` probed
    // "<cwd>:\e\p" and would have landed the marker beside the cwd.  Fold
    // once through the shared make_api_path_operand boundary before any
    // filesystem call; `fifo` stays verbatim in every message, like MSYS.
    const auto operand = native_path::make_api_path_operand(fifo);
    const DWORD existing = native_path::operand_target_attributes_w(operand);
    if (existing != INVALID_FILE_ATTRIBUTES) {
      safeErrorPrint("mkfifo: cannot create fifo '");
      safeErrorPrint(fifo);
      safeErrorPrint("': File exists\n");
      exit_code = 1;
      continue;
    }

    // A mode without any write bit maps to the DOS readonly
    // attribute, computed from the evaluated mode value.
    const bool readonly = cfg.has_mode && (cfg.mode_value & 0222) == 0;
    const DWORD created =
        native_path::create_winux_fifo_w(operand.normalized, readonly);
    if (created != ERROR_SUCCESS) {
      safeErrorPrint("mkfifo: cannot create fifo '");
      safeErrorPrint(fifo);
      safeErrorPrint("': ");
      safeErrorPrint(win32_posix_error_text(
          created, {.file_exists = true, .invalid_name_as_missing = true}));
      safeErrorPrint("\n");
      exit_code = 1;
    }
  }
  return exit_code;
}

}  // namespace mkfifo_pipeline

REGISTER_COMMAND(
    mkfifo, "mkfifo", "mkfifo [OPTION]... NAME...",
    "Create named pipes (FIFOs) with the given NAMEs.\n"
    "\n"
    "Windows has no filesystem FIFO node type, so WinuxCmd emulates one with\n"
    "an on-disk marker file. WinuxCmd commands opening the marker for\n"
    "reading or writing are bridged to a Windows named pipe with blocking\n"
    "POSIX open() semantics; other tools see a small marker file.",
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
    if (cfg_result.error().starts_with("missing operand")) {
      safeErrorPrint("Try 'mkfifo --help' for more information.\n");
    }
    return 1;
  }

  return run(*cfg_result);
}
