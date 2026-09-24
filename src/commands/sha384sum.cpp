// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sha384sum.
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

auto constexpr SHA384SUM_OPTIONS = std::array{
    // [DIFFERS] -b, --binary
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    // [GNU] -c, --check
    OPTION("-c", "--check", "read SHA384 sums from the FILEs and check them",
           BOOL_TYPE),
    // [GNU] --ignore-missing
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    // [DIFFERS] -t, --text
    OPTION("-t", "--text", "read in text mode", BOOL_TYPE),
    // [GNU] -q, --quiet
    OPTION("", "--quiet", "don't print OK for each successfully verified file",
           BOOL_TYPE),
    // [GNU] -s, --status
    OPTION("", "--status", "don't output anything, status code shows success",
           BOOL_TYPE),
    // [GNU] -w, --warn
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines",
           BOOL_TYPE),
    // [GNU] --tag
    OPTION("", "--tag", "create a BSD-style checksum", BOOL_TYPE),
    // [GNU] -z, --zero
    OPTION("-z", "--zero", "end each output line with NUL, not newline",
           BOOL_TYPE),
    // [GNU] --strict
    OPTION("", "--strict", "with --check, exit non-zero for any invalid input",
           BOOL_TYPE)};

namespace sha384sum_pipeline {
namespace cp = core::pipeline;

struct Config {
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

auto build_config(const CommandContext<SHA384SUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.text_mode = ctx.get<bool>("--text", false) || ctx.get<bool>("-t", false);
#ifdef _WIN32
  cfg.binary_mode = !cfg.text_mode;  // Binary mode is default on Windows
#else
  cfg.binary_mode =
      ctx.get<bool>("--binary", false) || ctx.get<bool>("-b", false);
#endif
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
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }

  if (cfg.files.empty()) {
    cfg.files.push_back("-");
  }

  return cfg;
}

auto input_open_error(const std::string& filename) -> std::string {
  return filename + ": " + portable_digest::open_error_reason(filename);
}

auto calculate_sha384(const std::string& filename, bool text_mode = false)
    -> cp::Result<std::string> {
  return portable_digest::hash_file_hex(portable_digest::HashAlgorithm::Sha384,
                                        filename, text_mode);
}

auto run(const Config& cfg) -> int {
  if (cfg.check_mode) {
    // [GNU] digest_check() over every check FILE operand ("-" = stdin).
    portable_digest::SumCheckFlags flags{
        .status_only = cfg.status,
        .quiet = cfg.quiet,
        .warn = cfg.warn,
        .strict = cfg.strict,
        .ignore_missing = cfg.ignore_missing,
    };
    auto hash_fn = [](const std::string& filename, size_t) {
      return calculate_sha384(filename, false);
    };
    return portable_digest::sum_check_main(
        "sha384sum", "SHA384", 96, flags,
        std::span<const std::string>(cfg.files.data(), cfg.files.size()),
        hash_fn);
  }

  bool all_ok = true;

  for (const auto& file : cfg.files) {
    auto hash_result = calculate_sha384(file, cfg.text_mode);
    if (!hash_result) {
      cp::report_error(hash_result, L"sha384sum");
      all_ok = false;
      continue;
    }

    // GNU/MSYS defaults to binary marker on Windows; --text uses a space.
    std::string output;
    if (cfg.tag) {
      output = "SHA384 (" + file + ") = " + *hash_result;
    } else {
      output = *hash_result + (cfg.binary_mode ? " *" : "  ") + file;
    }
    output.push_back(cfg.zero ? '\0' : '\n');
    safePrint(output);
  }

  return all_ok ? 0 : 1;
}

}  // namespace sha384sum_pipeline

REGISTER_COMMAND(sha384sum, "sha384sum", "sha384sum [OPTION]... [FILE]...",
                 "Compute and check SHA384 message digest.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "SHA384 produces a 384-bit (48-byte) hash value, typically "
                 "rendered as a 96-digit hexadecimal number.\n"
                 "Uses WinuxCmd's portable SHA-384 implementation with the "
                 "FIPS-180-2 initial state and digest length.",
                 "  sha384sum file.txt\n"
                 "  echo \"test\" | sha384sum\n"
                 "  sha384sum *.txt > checksums.sha384",
                 "md5sum(1), sha1sum(1), sha256sum(1), sha512sum(1)",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", SHA384SUM_OPTIONS) {
  using namespace sha384sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"sha384sum");
    safeErrorPrintLn("Try 'sha384sum --help' for more information.");
    return 1;
  }

  return run(*cfg_result);
}
