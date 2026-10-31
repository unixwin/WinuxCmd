// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sha256sum.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include <bcrypt.h>

#include "core/command_macros.h"

#pragma comment(lib, "bcrypt.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr SHA256SUM_OPTIONS = std::array{
    // [GNU] -b, --binary
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    // [GNU] -c, --check
    OPTION("-c", "--check", "read SHA256 sums from the FILEs and check them",
           BOOL_TYPE),
    // [GNU] --ignore-missing
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    // [GNU] -t, --text
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

namespace sha256sum_pipeline {
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

auto build_config(const CommandContext<SHA256SUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  // [GNU] the text marker ("  ") is the default; -b/--binary selects the
  // "*" marker and the last of -b/-t wins, mirroring cksum.c.  Only the
  // marker changes -- the bytes hashed are always raw.
  cfg.binary_mode = false;
  cfg.text_mode = false;
  for (const auto& occurrence : ctx.options.occurrences()) {
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.short_name == "-b" || meta.long_name == "--binary") {
      cfg.binary_mode = true;
      cfg.text_mode = false;
    } else if (meta.short_name == "-t" || meta.long_name == "--text") {
      cfg.binary_mode = false;
      cfg.text_mode = true;
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

// Calculate SHA256 hash using Windows CNG (BCrypt)
auto calculate_sha256(const std::string& filename, bool text_mode = false)
    -> cp::Result<std::string> {
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;

  // [perf #1127-P4] CNG replaces the deprecated CryptoAPI provider path
  // (roadmap item 5): BCrypt dispatches to the hardware implementation
  // (SHA-NI where the CPU offers it) for the same digest, and the feed
  // buffer grows from 8 KiB to 64 KiB. The shape mirrors the in-repo
  // BCrypt idiom of wpm.cpp (sha256_file); the error strings remain the
  // historical ones so failure output does not change.
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
    return std::unexpected("failed to acquire cryptographic context");
  }

  DWORD object_size = 0;
  DWORD data_size = 0;
  BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&object_size),
                    sizeof(object_size), &data_size, 0);
  std::vector<UCHAR> hash_object(object_size);
  if (BCryptCreateHash(hAlg, &hHash, hash_object.data(), object_size,
                       nullptr, 0, 0) != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::unexpected("failed to create hash object");
  }
  auto destroy_hash = [&] {
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
  };

  if (filename == "-" || filename.empty()) {
    // [GNU] closed stdin (<&-) reports "-: Bad file descriptor" rather
    // than hashing an empty stream.
    if (file_io::stdin_is_bad()) {
      destroy_hash();
      return std::unexpected(std::string(filename.empty() ? "-" : filename) +
                             ": Bad file descriptor");
    }
    // Read from stdin.  [#1195] fd 0 uses the CRT's text mode by default,
    // where fread() silently stops at the first 0x1A (Ctrl-Z) byte; like
    // GNU, the digest must hash raw bytes, so switch stdin to binary mode
    // before reading (same idiom as cat/wc/tr).
    _setmode(_fileno(stdin), _O_BINARY);
    std::array<char, 64 * 1024> buffer;
    size_t bytes_read;

    while ((bytes_read = fread(buffer.data(), 1, buffer.size(), stdin)) > 0) {
      if (BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buffer.data()),
                         static_cast<ULONG>(bytes_read), 0) != 0) {
        destroy_hash();
        return std::unexpected("failed to hash data");
      }
    }
  } else {
    // Read from file.  [GNU] -t/--text changes only the output marker,
    // never the bytes hashed: coreutils hashes raw file bytes in both
    // modes, so no CRLF translation may happen here.
    std::ifstream file(native_path::normalize_api_operand(filename),
                       std::ios::binary);
    if (!file) {
      destroy_hash();
      return std::unexpected(input_open_error(filename));
    }

    std::array<char, 64 * 1024> buffer;
    while (file) {
      file.read(buffer.data(), buffer.size());
      std::streamsize bytes_read = file.gcount();
      if (bytes_read > 0) {
        if (BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buffer.data()),
                           static_cast<ULONG>(bytes_read), 0) != 0) {
          destroy_hash();
          return std::unexpected("failed to hash data");
        }
      }
    }
  }

  // Get hash value
  DWORD hash_len = 32;  // SHA256 produces 32 bytes
  std::array<BYTE, 32> hash_value{};

  if (BCryptFinishHash(hHash, hash_value.data(), hash_len, 0) != 0) {
    destroy_hash();
    return std::unexpected("failed to get hash value");
  }

  destroy_hash();

  // Convert to hex string
  std::string result;
  result.reserve(64);
  for (DWORD i = 0; i < hash_len; ++i) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", hash_value[i]);
    result += buf;
  }

  return result;
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
      return calculate_sha256(filename, false);
    };
    return portable_digest::sum_check_main(
        "sha256sum", "SHA256", 64, flags,
        std::span<const std::string>(cfg.files.data(), cfg.files.size()),
        hash_fn);
  }

  bool all_ok = true;

  for (const auto& file : cfg.files) {
    auto hash_result = calculate_sha256(file, cfg.text_mode);
    if (!hash_result) {
      cp::report_error(hash_result, L"sha256sum");
      all_ok = false;
      continue;
    }

    // Output format: HASH  FILENAME (or BSD-style if --tag)
    std::string output;
    if (cfg.tag) {
      output = "SHA256 (" + file + ") = " + *hash_result;
    } else {
      output = *hash_result + (cfg.binary_mode ? " *" : "  ") + file;
    }
    output.push_back(cfg.zero ? '\0' : '\n');
    safePrint(output);
  }

  return all_ok ? 0 : 1;
}

}  // namespace sha256sum_pipeline

REGISTER_COMMAND(sha256sum, "sha256sum", "sha256sum [OPTION]... [FILE]...",
                 "Compute and check SHA256 message digest.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.",
                 "  sha256sum file.txt\n"
                 "  echo \"test\" | sha256sum\n"
                 "  sha256sum *.txt > checksums.sha256",
                 "md5sum(1), sha1sum(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", SHA256SUM_OPTIONS) {
  using namespace sha256sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"sha256sum");
    safeErrorPrintLn("Try 'sha256sum --help' for more information.");
    return 1;
  }

  return run(*cfg_result);
}
