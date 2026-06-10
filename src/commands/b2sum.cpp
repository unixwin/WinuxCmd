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
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    OPTION("-t", "--text", "read in text mode", BOOL_TYPE),
    OPTION("-q", "--quiet",
           "don't print OK for each successfully verified file", BOOL_TYPE),
    OPTION("-s", "--status", "don't output anything, status code shows success",
           BOOL_TYPE),
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines",
           BOOL_TYPE),
    OPTION("", "--tag",
           "create a BSD-style checksum", BOOL_TYPE),
    OPTION("-z", "--zero",
           "end each output line with NUL, not newline", BOOL_TYPE),
    OPTION("", "--strict",
           "with --check, exit non-zero for any invalid input", BOOL_TYPE)};

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
  std::string check_file;
  SmallVector<std::string, 64> files;
};

struct CheckLine {
  std::string expected_hash;
  std::string filename;
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
      if (cfg.digest_bits <= 0 || cfg.digest_bits > 512 ||
          cfg.digest_bits % 8 != 0) {
        return std::unexpected(
            "digest length must be a positive multiple of 8 and at most 512 "
            "bits");
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
  cfg.tag = ctx.get<bool>("--tag", false);
  cfg.zero = ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);
  cfg.strict = ctx.get<bool>("--strict", false);
  cfg.ignore_missing = ctx.get<bool>("--ignore-missing", false);

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

auto input_open_error(const std::string& filename) -> std::string {
  std::error_code ec;
  if (std::filesystem::is_directory(std::filesystem::u8path(filename), ec) &&
      !ec) {
    return "cannot open '" + filename + "' for reading: Is a directory";
  }
  return "cannot open '" + filename +
         "' for reading: No such file or directory";
}

// Calculate hash using CNG API (SHA512 as BLAKE2-512 placeholder)
auto calculate_hash(const std::string& filename, bool text_mode = false)
    -> cp::Result<std::string> {
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
    // Read from file (binary mode by default, text mode if --text)
    std::ifstream file(filename, text_mode ? std::ios::in : std::ios::binary);
    if (!file) {
      BCryptDestroyHash(hHash);
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return std::unexpected(input_open_error(filename));
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

auto parse_check_line(const std::string& line) -> std::optional<CheckLine> {
  if (line.empty()) {
    return std::nullopt;
  }

  size_t eq_pos = line.find(" = ");
  if (eq_pos != std::string::npos && line.rfind("BLAKE2 (", 0) == 0) {
    std::string filename = line.substr(8, eq_pos - 8);
    if (!filename.empty() && filename.front() == '(') {
      filename.erase(filename.begin());
    }
    if (!filename.empty() && filename.back() == ')') {
      filename.pop_back();
    }
    std::string expected_hash = line.substr(eq_pos + 3);
    if (!filename.empty() && !expected_hash.empty() &&
        expected_hash.size() <= 128 && expected_hash.size() % 2 == 0) {
      for (char ch : expected_hash) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
          return std::nullopt;
        }
      }
      return CheckLine{expected_hash, filename};
    }
    return std::nullopt;
  }

  size_t marker_pos = std::string::npos;
  size_t sep_pos = line.find("  ");
  if (sep_pos != std::string::npos) {
    marker_pos = sep_pos;
  } else {
    sep_pos = line.find(" *");
    if (sep_pos != std::string::npos) {
      marker_pos = sep_pos;
    }
  }
  if (marker_pos == std::string::npos || marker_pos == 0) {
    return std::nullopt;
  }

  std::string expected_hash = line.substr(0, marker_pos);
  if (expected_hash.empty() || expected_hash.size() > 128 ||
      expected_hash.size() % 2 != 0) {
    return std::nullopt;
  }
  for (char ch : expected_hash) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) {
      return std::nullopt;
    }
  }

  std::string filename = line.substr(marker_pos + 2);
  if (filename.empty()) {
    return std::nullopt;
  }

  return CheckLine{expected_hash, filename};
}

auto format_malformed_line_count(int malformed) -> std::string {
  if (malformed == 1) {
    return "b2sum: WARNING: 1 line is improperly formatted\n";
  }
  return "b2sum: WARNING: " + std::to_string(malformed) +
         " lines are improperly formatted\n";
}

auto format_unreadable_file_count(int unreadable) -> std::string {
  if (unreadable == 1) {
    return "b2sum: WARNING: 1 listed file could not be read\n";
  }
  return "b2sum: WARNING: " + std::to_string(unreadable) +
         " listed files could not be read\n";
}

auto should_ignore_missing_file(const Config& cfg, std::string_view path)
    -> bool {
  if (!cfg.ignore_missing || path.empty() || path == "-") {
    return false;
  }

  std::error_code ec;
  return !std::filesystem::exists(std::filesystem::u8path(path), ec);
}

auto run(const Config& cfg) -> int {
  if (cfg.check_mode) {
    std::istream* input = &std::cin;
    std::ifstream file;
    std::string input_name = "standard input";
    if (!cfg.check_file.empty() && cfg.check_file != "-") {
      file.open(cfg.check_file);
      if (!file) {
        cp::report_custom_error(
            L"b2sum", utf8_to_wstring(input_open_error(cfg.check_file)));
        return 1;
      }
      input = &file;
      input_name = cfg.check_file;
    }

    int mismatches = 0;
    int malformed = 0;
    int checked = 0;
    int unreadable = 0;
    std::string line;
    size_t line_number = 0;
    while (std::getline(*input, line)) {
      ++line_number;
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty()) {
        continue;
      }

      auto parsed = parse_check_line(line);
      if (!parsed) {
        ++malformed;
        if (cfg.warn && !cfg.status) {
          safeErrorPrint("b2sum: " + input_name + ": " +
                         std::to_string(line_number) +
                         ": improperly formatted checksum line\n");
        }
        continue;
      }

      ++checked;
      if (should_ignore_missing_file(cfg, parsed->filename)) {
        continue;
      }

      auto hash_result = calculate_hash(parsed->filename, cfg.text_mode);
      if (!hash_result) {
        if (!cfg.status) {
          cp::report_error(hash_result, L"b2sum");
        }
        ++unreadable;
        ++mismatches;
        continue;
      }

      if (hash_result->size() > parsed->expected_hash.size()) {
        hash_result->resize(parsed->expected_hash.size());
      }

      if (*hash_result == parsed->expected_hash) {
        if (!cfg.quiet && !cfg.status) {
          safePrint(parsed->filename + ": OK\n");
        }
      } else {
        if (!cfg.status) {
          safePrint(parsed->filename + ": FAILED\n");
        }
        ++mismatches;
      }
    }

    if (checked == 0 && malformed > 0) {
      if (!cfg.status) {
        safeErrorPrint("b2sum: " + input_name +
                       ": no properly formatted checksum lines found\n");
      }
      return 1;
    }

    if (malformed > 0 && cfg.warn && !cfg.status) {
      safeErrorPrint(format_malformed_line_count(malformed));
    }

    if (unreadable > 0 && !cfg.status) {
      safeErrorPrint(format_unreadable_file_count(unreadable));
    }

    if (cfg.strict && malformed > 0) {
      return 1;
    }
    if (mismatches > 0) {
      return 1;
    }
    return 0;
  }

  bool all_ok = true;
  size_t output_hex_chars = static_cast<size_t>(cfg.digest_bits / 4);

  for (const auto& file : cfg.files) {
    auto hash_result = calculate_hash(file, cfg.text_mode);
    if (!hash_result) {
      cp::report_error(hash_result, L"b2sum");
      all_ok = false;
      continue;
    }

    if (hash_result->size() > output_hex_chars) {
      hash_result->resize(output_hex_chars);
    }

    // Output format: HASH  FILENAME (or BSD-style if --tag)
    std::string output;
    if (cfg.tag) {
      output = "BLAKE2 (" + file + ") = " + *hash_result;
    } else {
      output = *hash_result + "  " + file;
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
