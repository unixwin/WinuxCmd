// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sha1sum.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include <wincrypt.h>

#include "core/command_macros.h"

#pragma comment(lib, "advapi32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// sha1sum uses the same options as md5sum
auto constexpr SHA1SUM_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    // [GNU]
    OPTION("-c", "--check", "read SHA1 sums from the FILEs and check them",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    // [DIFFERS]
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

namespace sha1sum_pipeline {
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

auto input_open_error(std::string_view path) -> std::string {
  return std::string(path) + ": " + portable_digest::open_error_reason(path);
}

auto build_config(const CommandContext<SHA1SUM_OPTIONS.size()>& ctx)
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

// Calculate SHA1 hash using Windows CryptoAPI
// Note: CALG_SHA1 is supported by Windows CryptoAPI
auto calculate_sha1(const std::string& filename, bool text_mode = false)
    -> cp::Result<std::string> {
  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;

  // Open cryptographic provider
  if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL,
                           CRYPT_VERIFYCONTEXT)) {
    return std::unexpected("failed to acquire cryptographic context");
  }

  // Create hash object using SHA1
  if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
    CryptReleaseContext(hProv, 0);
    return std::unexpected("failed to create hash object");
  }

  bool success = false;
  if (filename == "-" || filename.empty()) {
    // [GNU] closed stdin (<&-) reports "-: Bad file descriptor" rather
    // than hashing an empty stream.
    if (file_io::stdin_is_bad()) {
      return std::unexpected(std::string(filename.empty() ? "-" : filename) +
                             ": Bad file descriptor");
    }
    // Read from stdin
    std::array<char, 8192> buffer;
    size_t bytes_read;

    while ((bytes_read = fread(buffer.data(), 1, buffer.size(), stdin)) > 0) {
      if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer.data()),
                         static_cast<DWORD>(bytes_read), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return std::unexpected("failed to hash data");
      }
    }
    success = true;
  } else {
    // Read from file.  [GNU] -t/--text changes only the output marker,
    // never the bytes hashed: coreutils hashes raw file bytes in both
    // modes, so no CRLF translation may happen here.
    std::ifstream file(native_path::normalize_api_operand(filename),
                       std::ios::binary);
    if (!file) {
      CryptDestroyHash(hHash);
      CryptReleaseContext(hProv, 0);
      return std::unexpected(input_open_error(filename));
    }

    std::array<char, 8192> buffer;
    while (file) {
      file.read(buffer.data(), buffer.size());
      std::streamsize bytes_read = file.gcount();
      if (bytes_read > 0) {
        if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer.data()),
                           static_cast<DWORD>(bytes_read), 0)) {
          CryptDestroyHash(hHash);
          CryptReleaseContext(hProv, 0);
          return std::unexpected("failed to hash data");
        }
      }
    }
    success = !file.fail();
  }

  // Get hash value
  DWORD hash_len = 20;  // SHA1 produces 20 bytes
  std::array<BYTE, 20> hash_value{};

  if (!CryptGetHashParam(hHash, HP_HASHVAL, hash_value.data(), &hash_len, 0)) {
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return std::unexpected("failed to get hash value");
  }

  CryptDestroyHash(hHash);
  CryptReleaseContext(hProv, 0);

  // Convert to hex string
  std::string result;
  result.reserve(40);
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
      return calculate_sha1(filename, false);
    };
    return portable_digest::sum_check_main(
        "sha1sum", "SHA1", 40, flags,
        std::span<const std::string>(cfg.files.data(), cfg.files.size()),
        hash_fn);
  }

  bool all_ok = true;

  for (const auto& file : cfg.files) {
    auto hash_result = calculate_sha1(file, cfg.text_mode);
    if (!hash_result) {
      cp::report_error(hash_result, L"sha1sum");
      all_ok = false;
      continue;
    }

    // Output format: HASH  FILENAME (or BSD-style if --tag)
    std::string output;
    if (cfg.tag) {
      output = "SHA1 (" + file + ") = " + *hash_result;
    } else {
      output = *hash_result + (cfg.binary_mode ? " *" : "  ") + file;
    }
    output.push_back(cfg.zero ? '\0' : '\n');
    safePrint(output);
  }

  return all_ok ? 0 : 1;
}

}  // namespace sha1sum_pipeline

REGISTER_COMMAND(sha1sum, "sha1sum", "sha1sum [OPTION]... [FILE]...",
                 "Compute and check SHA1 message digest.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "SHA1 produces a 160-bit (20-byte) hash value, typically "
                 "rendered as a 40-digit hexadecimal number.",
                 "  sha1sum file.txt\n"
                 "  echo \"test\" | sha1sum\n"
                 "  sha1sum *.txt > checksums.sha1",
                 "md5sum(1), sha256sum(1), sha512sum(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", SHA1SUM_OPTIONS) {
  using namespace sha1sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"sha1sum");
    safeErrorPrintLn("Try 'sha1sum --help' for more information.");
    return 1;
  }

  return run(*cfg_result);
}
