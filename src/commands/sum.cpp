// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sum.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr SUM_OPTIONS = std::array{
    // [GNU] -r, --bsd
    OPTION("-r", "--bsd", "use BSD sum algorithm (1024-byte blocks)",
           BOOL_TYPE),
    // [GNU] -s, --sysv
    OPTION("-s", "--sysv", "use System V sum algorithm (512-byte blocks)",
           BOOL_TYPE)};

namespace sum_pipeline {
namespace cp = core::pipeline;

struct Config {
  enum class Algorithm { Bsd, Sysv };

  Algorithm algorithm = Algorithm::Bsd;
  struct InputFile {
    std::string path;
    bool display_name = true;
  };
  SmallVector<InputFile, 64> files;
};

auto choose_algorithm(const CommandContext<SUM_OPTIONS.size()>& ctx)
    -> Config::Algorithm {
  auto algorithm = Config::Algorithm::Bsd;
  for (auto arg : ctx.raw_args) {
    if (arg == "-r" || arg == "--bsd") algorithm = Config::Algorithm::Bsd;
    if (arg == "-s" || arg == "--sysv") algorithm = Config::Algorithm::Sysv;
  }
  return algorithm;
}

auto build_config(const CommandContext<SUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.algorithm = choose_algorithm(ctx);

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back({wstring_to_utf8(file), true});
        }
        continue;
      }
    }
    cfg.files.push_back({file_arg, true});
  }

  if (cfg.files.empty()) {
    cfg.files.push_back({"-", false});
  }

  return cfg;
}

auto calculate_checksum(const std::string& filename, uint32_t& block_count,
                        Config::Algorithm algorithm) -> cp::Result<uint16_t> {
  // [GNU] diagnostics use "sum: FILE: <strerror>" wording.
  auto input_open_error = [](std::string_view path) -> std::string {
    return std::string(path) + ": " + portable_digest::open_error_reason(path);
  };

  std::istream* input = &std::cin;
  std::ifstream file;
  if (filename == "-" || filename.empty()) {
    // [GNU] closed stdin (<&-) reports "-: Bad file descriptor".
    if (file_io::stdin_is_bad()) {
      return std::unexpected(std::string(filename.empty() ? "-" : filename) +
                             ": Bad file descriptor");
    }
  } else {
    file.open(native_path::normalize_api_operand(filename), std::ios::binary);
    if (!file) {
      return std::unexpected(input_open_error(filename));
    }
    input = &file;
  }

  uint64_t total_bytes = 0;
  uint32_t sysv_sum = 0;
  uint32_t bsd_checksum = 0;
  std::array<char, 32768> buffer{};

  while (*input) {
    input->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = input->gcount();
    if (read <= 0) {
      continue;
    }

    const auto bytes =
        std::span<const char>(buffer.data(), static_cast<size_t>(read));
    total_bytes += bytes.size();
    if (algorithm == Config::Algorithm::Sysv) {
      for (unsigned char byte : bytes) {
        sysv_sum += byte;
      }
    } else {
      for (unsigned char byte : bytes) {
        bsd_checksum = (bsd_checksum >> 1) + ((bsd_checksum & 1U) << 15);
        bsd_checksum += byte;
        bsd_checksum &= 0xffffU;
      }
    }
  }

  if (input->bad()) {
    return std::unexpected("error reading from file");
  }

  const uint32_t block_size =
      algorithm == Config::Algorithm::Sysv ? 512U : 1024U;
  block_count =
      static_cast<uint32_t>((total_bytes + block_size - 1U) / block_size);

  if (algorithm == Config::Algorithm::Sysv) {
    uint32_t folded = (sysv_sum & 0xffffU) + (sysv_sum >> 16U);
    folded = (folded & 0xffffU) + (folded >> 16U);
    return static_cast<uint16_t>(folded);
  }

  return static_cast<uint16_t>(bsd_checksum);
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;

  for (const auto& file : cfg.files) {
    uint32_t block_count = 0;
    auto checksum_result =
        calculate_checksum(file.path, block_count, cfg.algorithm);

    if (!checksum_result) {
      cp::report_error(checksum_result, L"sum");
      all_ok = false;
      continue;
    }

    char buf[64];
    if (cfg.algorithm == Config::Algorithm::Bsd) {
      snprintf(buf, sizeof(buf), "%05u %5u", *checksum_result, block_count);
    } else {
      snprintf(buf, sizeof(buf), "%u %u", *checksum_result, block_count);
    }
    safePrint(buf);

    if (file.display_name) {
      safePrint(" ");
      safePrint(file.path);
    }
    safePrintLn("");
  }

  return all_ok ? 0 : 1;
}

}  // namespace sum_pipeline

REGISTER_COMMAND(sum, "sum", "sum [OPTION]... [FILE]...",
                 "Checksum and count the blocks in a file.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "The BSD algorithm is used by default (1024-byte blocks).\n",
                 "  sum file.txt\n"
                 "  echo \"test\" | sum\n"
                 "  sum -r file.txt",
                 "cksum(1), md5sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 SUM_OPTIONS) {
  using namespace sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"sum");
    return 1;
  }

  return run(*cfg_result);
}
