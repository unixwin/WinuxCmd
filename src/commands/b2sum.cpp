// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for b2sum.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr B2SUM_OPTIONS = std::array{
    // [GNU]
    OPTION("-l", "--length", "digest length in bits; must be multiple of 8",
           STRING_TYPE),
    // [GNU]
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    // [GNU]
    OPTION("-c", "--check", "read BLAKE2 sums from the FILEs and check them",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    // [GNU]
    OPTION("-t", "--text", "read in text mode", BOOL_TYPE),
    // [GNU]
    OPTION("", "--quiet", "don't print OK for each successfully verified file",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--status", "don't output anything, status code shows success",
           BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--tag", "create a BSD-style checksum", BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--zero", "end each output line with NUL, not newline",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--strict", "with --check, exit non-zero for any invalid input",
           BOOL_TYPE)};

namespace b2sum_pipeline {
namespace cp = core::pipeline;

struct Config {
  int digest_bits = 512;  // GNU b2sum defaults to the full BLAKE2b width.
  bool binary_mode = true;
  bool check_mode = false;
  bool text_mode = false;
  bool quiet = false;
  bool status = false;
  bool warn = false;
  bool tag = false;
  bool zero = false;
  bool strict = false;
  bool ignore_missing = false;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<B2SUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.text_mode = ctx.get<bool>("--text", false) || ctx.get<bool>("-t", false);
#ifdef _WIN32
  cfg.binary_mode = !cfg.text_mode;  // Binary mode is default on Windows
#else
  cfg.binary_mode =
      ctx.get<bool>("--binary", false) || ctx.get<bool>("-b", false);
#endif

  auto length_opt = ctx.get<std::string>("--length", "");
  if (length_opt.empty()) {
    length_opt = ctx.get<std::string>("-l", "");
  }
  if (!length_opt.empty()) {
    auto [ptr, ec] =
        std::from_chars(length_opt.data(),
                        length_opt.data() + length_opt.size(), cfg.digest_bits);
    if (ec != std::errc() || ptr != length_opt.data() + length_opt.size()) {
      return std::unexpected("invalid digest length");
    }
    if (cfg.digest_bits <= 0 || cfg.digest_bits > 512 ||
        cfg.digest_bits % 8 != 0) {
      return std::unexpected(
          "digest length must be a positive multiple of 8 and at most 512 "
          "bits");
    }
  }

  cfg.check_mode =
      ctx.get<bool>("--check", false) || ctx.get<bool>("-c", false);
  cfg.tag = ctx.get<bool>("--tag", false);
  cfg.zero = ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);
  cfg.strict = ctx.get<bool>("--strict", false);
  cfg.ignore_missing = ctx.get<bool>("--ignore-missing", false);

  // [GNU] the last of --status/--warn/--quiet wins and resets the other two
  // (getopt cases STATUS_OPTION/'w'/QUIET_OPTION in src/cksum.c).
  for (const auto& occurrence : ctx.options.occurrences()) {
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.long_name == "--status") {
      cfg.status = true;
      cfg.warn = false;
      cfg.quiet = false;
    } else if (meta.long_name == "--warn") {
      cfg.status = false;
      cfg.warn = true;
      cfg.quiet = false;
    } else if (meta.long_name == "--quiet") {
      cfg.status = false;
      cfg.warn = false;
      cfg.quiet = true;
    }
  }

  // [GNU] option-conflict checks, in src/cksum.c main() order.
  const bool mode_seen = ctx.has("-b") || ctx.has("--binary") ||
                         ctx.has("-t") || ctx.has("--text");
  if (cfg.zero && cfg.check_mode) {
    return std::unexpected(
        "the --zero option is not supported when verifying checksums");
  }
  if (cfg.tag && cfg.check_mode) {
    return std::unexpected(
        "the --tag option is meaningless when verifying checksums");
  }
  if (mode_seen && cfg.check_mode) {
    return std::unexpected(
        "the --binary and --text options are meaningless when verifying "
        "checksums");
  }
  if (cfg.ignore_missing && !cfg.check_mode) {
    return std::unexpected(
        "the --ignore-missing option is meaningful only when verifying "
        "checksums");
  }
  if (cfg.status && !cfg.check_mode) {
    return std::unexpected(
        "the --status option is meaningful only when verifying checksums");
  }
  if (cfg.warn && !cfg.check_mode) {
    return std::unexpected(
        "the --warn option is meaningful only when verifying checksums");
  }
  if (cfg.quiet && !cfg.check_mode) {
    return std::unexpected(
        "the --quiet option is meaningful only when verifying checksums");
  }
  if (cfg.strict && !cfg.check_mode) {
    return std::unexpected(
        "the --strict option is meaningful only when verifying checksums");
  }
  if (cfg.tag && cfg.text_mode) {
    return std::unexpected("--tag does not support --text mode");
  }

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    for (const auto& file : expand_file_operand(file_arg)) {
      cfg.files.push_back(file);
    }
  }

  // [GNU] with no FILE operand both modes default to standard input.
  if (cfg.files.empty()) {
    cfg.files.push_back("-");
  }

  return cfg;
}

auto input_open_error(const std::string& filename) -> std::string {
  return filename + ": " + portable_digest::open_error_reason(filename);
}

auto calculate_hash(const std::string& filename, bool text_mode = false,
                    size_t digest_bytes = 64) -> cp::Result<std::string> {
  return portable_digest::hash_file_hex(portable_digest::HashAlgorithm::Blake2b,
                                        filename, text_mode, digest_bytes);
}

auto run(const Config& cfg) -> int {
  if (cfg.check_mode) {
    // [GNU] digest_check() over every check FILE operand ("-" = stdin);
    // digest length is auto-detected per line like GNU b2sum.
    portable_digest::SumCheckFlags flags{
        .status_only = cfg.status,
        .quiet = cfg.quiet,
        .warn = cfg.warn,
        .strict = cfg.strict,
        .ignore_missing = cfg.ignore_missing,
    };
    auto hash_fn = [](const std::string& filename, size_t digest_bytes) {
      return calculate_hash(filename, false, digest_bytes);
    };
    return portable_digest::sum_check_main(
        "b2sum", "BLAKE2b", 0, flags,
        std::span<const std::string>(cfg.files.data(), cfg.files.size()),
        hash_fn);
  }

  bool all_ok = true;
  for (const auto& file : cfg.files) {
    auto hash_result = calculate_hash(file, cfg.text_mode,
                                      static_cast<size_t>(cfg.digest_bits / 8));
    if (!hash_result) {
      cp::report_error(hash_result, L"b2sum");
      all_ok = false;
      continue;
    }

    // GNU/MSYS defaults to binary marker on Windows; --text uses a space.
    std::string output;
    if (cfg.tag) {
      std::string tag = "BLAKE2b";
      if (cfg.digest_bits < 512) {
        tag += "-" + std::to_string(cfg.digest_bits);
      }
      output = tag + " (" + file + ") = " + *hash_result;
    } else {
      output = *hash_result + (cfg.binary_mode ? " *" : "  ") + file;
    }
    output.push_back(cfg.zero ? '\0' : '\n');
    safePrint(output);
  }

  return all_ok ? 0 : 1;
}

}  // namespace b2sum_pipeline

REGISTER_COMMAND(
    b2sum, "b2sum", "b2sum [OPTION]... [FILE]...",
    "Print or check BLAKE2 (512-bit) checksums.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Uses WinuxCmd's portable BLAKE2b implementation, matching GNU b2sum's "
    "default 512-bit digest and --length truncation behavior.",
    "  b2sum file.txt\n"
    "  echo \"test\" | b2sum\n"
    "  b2sum *.txt > checksums.b2",
    "sha1sum(1), sha256sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    B2SUM_OPTIONS) {
  using namespace b2sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"b2sum");
    safeErrorPrintLn("Try 'b2sum --help' for more information.");
    return 1;
  }

  return run(*cfg_result);
}
