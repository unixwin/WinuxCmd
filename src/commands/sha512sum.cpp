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
 *  - File: sha512sum.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sha512sum.
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

auto constexpr SHA512SUM_OPTIONS = std::array{
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    OPTION("-c", "--check", "read SHA512 sums from the FILEs and check them",
           STRING_TYPE),
    OPTION("-t", "--text", "read in text mode", BOOL_TYPE),
    OPTION("-q", "--quiet",
           "don't print OK for each successfully verified file", BOOL_TYPE),
    OPTION("-s", "--status", "don't output anything, status code shows success",
           BOOL_TYPE),
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines",
           BOOL_TYPE)};

namespace sha512sum_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool binary_mode = true;
  bool check_mode = false;
  bool text_mode = false;
  bool quiet = false;
  bool status = false;
  bool warn = false;
  std::string check_file;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<SHA512SUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.binary_mode =
      ctx.get<bool>("--binary", false) || ctx.get<bool>("-b", false);
  auto check_opt = ctx.get<std::string>("--check", "");
  cfg.check_mode =
      !check_opt.empty() || !ctx.get<std::string>("-c", "").empty();
  cfg.text_mode = ctx.get<bool>("--text", false) || ctx.get<bool>("-t", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  cfg.status = ctx.get<bool>("--status", false) || ctx.get<bool>("-s", false);
  cfg.warn = ctx.get<bool>("--warn", false) || ctx.get<bool>("-w", false);

  if (cfg.check_mode) {
    cfg.check_file = ctx.get<std::string>("--check", "");
    if (cfg.check_file.empty()) {
      cfg.check_file = ctx.get<std::string>("-c", "");
    }
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

  if (cfg.files.empty() && !cfg.check_mode) {
    cfg.files.push_back("-");
  }

  return cfg;
}

// Calculate SHA512 hash using Windows CryptoAPI
auto calculate_sha512(const std::string& filename) -> cp::Result<std::string> {
  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;

  // Open cryptographic provider
  // Note: SHA512 requires PROV_RSA_AES or a SHA512-capable provider
  if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES,
                           CRYPT_VERIFYCONTEXT)) {
    return std::unexpected("failed to acquire cryptographic context");
  }

  // Create hash object using SHA512
  if (!CryptCreateHash(hProv, CALG_SHA_512, 0, 0, &hHash)) {
    CryptReleaseContext(hProv, 0);
    return std::unexpected("failed to create hash object");
  }

  bool success = false;
  if (filename == "-" || filename.empty()) {
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
    // Read from file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
      CryptDestroyHash(hHash);
      CryptReleaseContext(hProv, 0);
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
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
  DWORD hash_len = 64;  // SHA512 produces 64 bytes
  std::array<BYTE, 64> hash_value{};

  if (!CryptGetHashParam(hHash, HP_HASHVAL, hash_value.data(), &hash_len, 0)) {
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return std::unexpected("failed to get hash value");
  }

  CryptDestroyHash(hHash);
  CryptReleaseContext(hProv, 0);

  // Convert to hex string
  std::string result;
  result.reserve(128);
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
        L"sha512sum", L"check mode is not fully implemented in this version");
    return 1;
  }

  bool all_ok = true;

  for (const auto& file : cfg.files) {
    auto hash_result = calculate_sha512(file);
    if (!hash_result) {
      cp::report_error(hash_result, L"sha512sum");
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

}  // namespace sha512sum_pipeline

REGISTER_COMMAND(sha512sum, "sha512sum", "sha512sum [OPTION]... [FILE]...",
                 "Compute and check SHA512 message digest.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "SHA512 produces a 512-bit (64-byte) hash value, typically "
                 "rendered as a 128-digit hexadecimal number.",
                 "  sha512sum file.txt\n"
                 "  echo \"test\" | sha512sum\n"
                 "  sha512sum *.txt > checksums.sha512",
                 "md5sum(1), sha1sum(1), sha256sum(1), sha384sum(1)",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", SHA512SUM_OPTIONS) {
  using namespace sha512sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"sha512sum");
    return 1;
  }

  return run(*cfg_result);
}
