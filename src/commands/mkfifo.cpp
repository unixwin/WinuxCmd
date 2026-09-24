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
    // [DIFFERS]
    OPTION("-m", "--mode", "set file permission bits", STRING_TYPE),
    // [DIFFERS]
    OPTION("-Z", "--security-context",
           "set the SELinux security context to default type"),
    // [DIFFERS]
    OPTION("", "--context", "set the SELinux security context"),
};

namespace mkfifo_pipeline {
namespace cp = core::pipeline;

auto is_plausible_mode(std::string_view mode) -> bool {
  if (mode.empty()) return false;

  // Check for valid octal mode (1-4 octal digits)
  bool octal = true;
  size_t octal_count = 0;
  for (char ch : mode) {
    if (ch < '0' || ch > '7') {
      octal = false;
      break;
    }
    octal_count++;
  }
  if (octal && octal_count >= 1 && octal_count <= 4) return true;

  // Check for valid symbolic mode: [ugoa]*[+-=][rwxst]*, comma-separated
  bool valid_symbolic = true;
  size_t start = 0;
  for (size_t i = 0; i <= mode.size(); i++) {
    if (i == mode.size() || mode[i] == ',') {
      std::string_view clause(mode.data() + start, i - start);
      if (clause.empty()) {
        valid_symbolic = false;
        break;
      }
      // Check clause format: [ugoa]*[+-=][rwxst]*
      size_t j = 0;
      // Who part
      while (j < clause.size() && (clause[j] == 'u' || clause[j] == 'g' ||
                                   clause[j] == 'o' || clause[j] == 'a')) {
        j++;
      }
      // Op part
      if (j >= clause.size() ||
          (clause[j] != '+' && clause[j] != '-' && clause[j] != '=')) {
        valid_symbolic = false;
        break;
      }
      j++;
      // What part
      while (j < clause.size()) {
        if (clause[j] != 'r' && clause[j] != 'w' && clause[j] != 'x' &&
            clause[j] != 's' && clause[j] != 't' && clause[j] != 'X') {
          valid_symbolic = false;
          break;
        }
        j++;
      }
      if (!valid_symbolic) break;
      start = i + 1;
    }
  }
  return valid_symbolic;
}

struct Config {
  std::vector<std::string> fifos;
  std::string mode;
  bool context_requested = false;
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

  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  cfg.context_requested = context_requested;
  add_fifo_args(cfg, std::span<const std::string_view>(ctx.positionals.data(),
                                                       ctx.positionals.size()));

  return cfg;
}

auto run(const Config& cfg) -> int {
  if (cfg.context_requested) {
    safeErrorPrintLn(winux::i18n::translate(
        "command.mkfifo.error.context-unsupported",
        "mkfifo: SELinux security contexts are not supported on Windows"));
    return 1;
  }

  int exit_code = 0;
  for (const auto& fifo : cfg.fifos) {
    std::filesystem::path p(fifo);
    std::error_code ec;
    bool exists = std::filesystem::exists(p, ec);
    if (ec) {
      safeErrorPrint("mkfifo: cannot create fifo '");
      safeErrorPrint(fifo);
      safeErrorPrint("': ");
      safeErrorPrint(ec.message());
      safeErrorPrint("\n");
      exit_code = 1;
      continue;
    }
    if (exists) {
      safeErrorPrint("mkfifo: cannot create fifo '");
      safeErrorPrint(fifo);
      safeErrorPrint("': File exists\n");
      exit_code = 1;
      continue;
    }

    // A mode without any write bit maps to the DOS readonly attribute.
    bool readonly = false;
    if (!cfg.mode.empty() &&
        cfg.mode.find_first_of("02367") == std::string::npos &&
        std::ranges::all_of(cfg.mode,
                            [](char ch) { return ch >= '0' && ch <= '7'; })) {
      readonly = true;
    }
    const DWORD created =
        native_path::create_winux_fifo_w(utf8_to_wstring(fifo), readonly);
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
