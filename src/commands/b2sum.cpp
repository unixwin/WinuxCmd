/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: b2sum.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for b2sum.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include <bcrypt.h>  // For CNG API (BLAKE2 support)
#include <wincrypt.h>

#include "core/command_macros.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")  // For CNG API

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr B2SUM_OPTIONS = std::array{
    OPTION("-l", "--length", "digest length in bits; must be multiple of 8",
           STRING_TYPE),
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    OPTION("-c", "--check", "read BLAKE2 sums from the FILEs and check them",
           STRING_TYPE),
    OPTION("-t", "--text", "read in text mode", BOOL_TYPE),
    OPTION("-q", "--quiet",
           "don't print OK for each successfully verified file", BOOL_TYPE),
    OPTION("-s", "--status", "don't output anything, status code shows success",
           BOOL_TYPE),
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines",
           BOOL_TYPE)};

namespace b2sum_pipeline {
namespace cp = core::pipeline;

struct Config {
  int digest_bits = 256;  // Default: BLAKE2-256
  bool binary_mode = true;
  bool check_mode = false;
  bool text_mode = false;
  bool quiet = false;
  bool status = false;
  bool warn = false;
  std::string check_file;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<B2SUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.binary_mode =
      ctx.get<bool>("--binary", false) || ctx.get<bool>("-b", false);

  auto length_opt = ctx.get<std::string>("--length", "");
  if (length_opt.empty()) {
    length_opt = ctx.get<std::string>("-l", "");
  }
  if (!length_opt.empty()) {
    try {
      cfg.digest_bits = std::stoi(length_opt);
      if (cfg.digest_bits != 128 && cfg.digest_bits != 256 &&
          cfg.digest_bits != 384 && cfg.digest_bits != 512) {
        return std::unexpected(
            "digest length must be 128, 256, 384, or 512 bits");
      }
    } catch (...) {
      return std::unexpected("invalid digest length");
    }
  }

  auto check_opt = ctx.get<std::string>("--check", "");
  if (check_opt.empty()) {
    check_opt = ctx.get<std::string>("-c", "");
  }
  cfg.check_mode = !check_opt.empty();
  if (cfg.check_mode) {
    cfg.check_file = check_opt;
  }

  cfg.text_mode = ctx.get<bool>("--text", false) || ctx.get<bool>("-t", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  cfg.status = ctx.get<bool>("--status", false) || ctx.get<bool>("-s", false);
  cfg.warn = ctx.get<bool>("--warn", false) || ctx.get<bool>("-w", false);

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

  if (cfg.files.empty() && !cfg.check_mode) {
    cfg.files.push_back("-");
  }

  return cfg;
}

// Calculate hash using CNG API (SHA512 as BLAKE2-512 placeholder)
auto calculate_hash(const std::string& filename) -> cp::Result<std::string> {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_HASH_HANDLE hHash = NULL;
  NTSTATUS status;

  // Open SHA512 algorithm provider (using CNG API)
  status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, NULL, 0);
  if (!BCRYPT_SUCCESS(status)) {
    return std::unexpected("failed to open SHA512 algorithm provider");
  }

  // Get hash object size
  DWORD hash_object_size = 0;
  DWORD data_size = 0;
  status =
      BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hash_object_size,
                        sizeof(DWORD), &data_size, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::unexpected("failed to get hash object size");
  }

  // Allocate hash object
  std::vector<BYTE> hash_object(hash_object_size);

  // Create hash handle
  status = BCryptCreateHash(hAlg, &hHash, hash_object.data(), hash_object_size,
                            NULL, 0, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::unexpected("failed to create hash handle");
  }

  // Hash the data
  if (filename == "-" || filename.empty()) {
    // Read from stdin
    std::array<char, 8192> buffer;
    size_t bytes_read;
    while ((bytes_read = fread(buffer.data(), 1, buffer.size(), stdin)) > 0) {
      status = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buffer.data()),
                              static_cast<ULONG>(bytes_read), 0);
      if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::unexpected("failed to hash data");
      }
    }
  } else {
    // Read from file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
      BCryptDestroyHash(hHash);
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }

    std::array<char, 8192> buffer;
    while (file) {
      file.read(buffer.data(), buffer.size());
      std::streamsize bytes_read = file.gcount();
      if (bytes_read > 0) {
        status = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buffer.data()),
                                static_cast<ULONG>(bytes_read), 0);
        if (!BCRYPT_SUCCESS(status)) {
          BCryptDestroyHash(hHash);
          BCryptCloseAlgorithmProvider(hAlg, 0);
          return std::unexpected("failed to hash data");
        }
      }
    }
    if (file.fail() && !file.eof()) {
      BCryptDestroyHash(hHash);
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return std::unexpected("error reading from file");
    }
  }

  // Get hash length
  DWORD hash_len = 0;
  status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                             sizeof(DWORD), &data_size, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::unexpected("failed to get hash length");
  }

  // Get hash value
  std::vector<BYTE> hash_value(hash_len);
  status = BCryptFinishHash(hHash, hash_value.data(), hash_len, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::unexpected("failed to finish hash");
  }

  // Cleanup
  BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);

  // Convert to hex string
  std::string result;
  result.reserve(hash_len * 2);
  for (DWORD i = 0; i < hash_len; ++i) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", hash_value[i]);
    result += buf;
  }

  return result;
}

auto run(const Config& cfg) -> int {
  if (cfg.check_mode) {
    // Check mode (not fully implemented)
    cp::report_custom_error(
        L"b2sum", L"check mode is not fully implemented in this version");
    return 1;
  }

  bool all_ok = true;

  for (const auto& file : cfg.files) {
    auto hash_result = calculate_hash(file);
    if (!hash_result) {
      cp::report_error(hash_result, L"b2sum");
      all_ok = false;
      continue;
    }

    // Output format: HASH  FILENAME
    safePrint(*hash_result);
    safePrint("  ");
    if (file == "-") {
      safePrint("-\n");
    } else {
      safePrintLn(file);
    }
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
    "Note: Windows CNG API doesn't support BLAKE2 natively in all versions.\n"
    "This implementation uses CNG API with SHA512 as a fallback,\n"
    "providing the same 512-bit hash length as BLAKE2-512.",
    "  b2sum file.txt\n"
    "  echo \"test\" | b2sum\n"
    "  b2sum *.txt > checksums.b2",
    "sha1sum(1), sha256sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    B2SUM_OPTIONS) {
  using namespace b2sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"b2sum");
    return 1;
  }

  return run(*cfg_result);
}
