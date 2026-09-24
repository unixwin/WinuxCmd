/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr HMAC256_OPTIONS =
    // [EXT]
    std::array{OPTION("", "", "key string", STRING_TYPE)};

// ======================================================
// Pipeline components
// ======================================================
namespace hmac256_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string key;
  std::string filename;
};

auto build_config(const CommandContext<HMAC256_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  if (ctx.positionals.empty()) {
    return std::unexpected("missing key");
  }

  Config cfg;
  cfg.key = std::string(ctx.positionals[0]);
  cfg.filename =
      ctx.positionals.size() > 1 ? std::string(ctx.positionals[1]) : "-";
  return cfg;
}

auto input_open_error(const std::string& filename) -> std::string {
  std::error_code ec;
  if (std::filesystem::is_directory(std::filesystem::u8path(filename), ec) &&
      !ec) {
    return "cannot open \x27" + filename + "\x27 for reading: Is a directory";
  }
  return "cannot open \x27" + filename +
         "\x27 for reading: No such file or directory";
}

auto run(const Config& cfg) -> int {
  std::istream* input = &std::cin;
  std::ifstream file;
  if (cfg.filename != "-") {
    file.open(cfg.filename, std::ios::binary);
    if (!file) {
      safeErrorPrint("hmac256: ");
      safeErrorPrintLn(input_open_error(cfg.filename));
      return 1;
    }
    input = &file;
  }

  auto digest = portable_digest::hmac_sha256_stream_hex(cfg.key, *input);
  if (!digest) {
    safeErrorPrint("hmac256: ");
    safeErrorPrintLn(digest.error());
    return 1;
  }

  safePrint(*digest);
  if (cfg.filename != "-") {
    safePrint("  ");
    safePrint(cfg.filename);
  }
  safePrintLn("");
  return 0;
}

}  // namespace hmac256_pipeline

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(hmac256,
                 /* name */
                 "hmac256",

                 /* synopsis */
                 "hmac256 [KEY] [FILE]",

                 /* description */
                 "Compute HMAC-SHA256 checksum.",

                 /* examples */
                 "  hmac256 secret file.txt",

                 /* see_also */
                 "sha256sum(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 HMAC256_OPTIONS) {
  using namespace hmac256_pipeline;
  using namespace core::pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("hmac256: ");
    safeErrorPrintLn(cfg_result.error());
    return 1;
  }

  return run(*cfg_result);
}
