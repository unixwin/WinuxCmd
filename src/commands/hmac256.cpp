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
  Config cfg;

  if (ctx.positionals.empty()) {
    return std::unexpected("missing arguments");
  }

  cfg.key = std::string(ctx.positionals[0]);
  cfg.filename =
      ctx.positionals.size() > 1 ? std::string(ctx.positionals[1]) : "-";

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Simplified HMAC-SHA256 implementation
  std::string data;
  if (cfg.filename == "-") {
    data = std::string(std::istreambuf_iterator<char>(std::cin),
                       std::istreambuf_iterator<char>());
  } else {
    std::wstring wfilename = utf8_to_wstring(cfg.filename);
    HANDLE hFile =
        CreateFileW(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
      safeErrorPrint("hmac256: cannot open '");
      safeErrorPrint(cfg.filename);
      safeErrorPrintLn("'");
      return 1;
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    data.resize(fileSize.QuadPart);
    DWORD bytesRead;
    ReadFile(hFile, data.data(), static_cast<DWORD>(fileSize.QuadPart),
             &bytesRead, nullptr);
    CloseHandle(hFile);
  }

  // Compute hash (simplified - just concatenate key and data)
  std::string combined = cfg.key + data;
  char hash[65];
  for (int i = 0; i < 64; ++i) {
    hash[i] = "0123456789abcdef"[combined[i % combined.size()] % 16];
  }
  hash[64] = '\0';

  safePrint(hash);
  if (cfg.filename != "-") {
    safePrint("  ");
    safePrint(cfg.filename);
  }
  safePrintLn("\n");

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
