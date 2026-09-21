// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cksum with GNU parity options.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

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

auto constexpr CKSUM_OPTIONS = std::array{
    // [GNU]
    OPTION("-a", "--algorithm",
           "select the digest type to use.  See DIGEST below", STRING_TYPE),
    // [GNU]
    OPTION("-b", "--binary",
           "read in binary mode (default unless reading tty stdin)"),
    // [GNU]
    OPTION("-c", "--check", "read checksums from the FILEs and check them"),
    // [GNU]
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files"),
    // [GNU]
    OPTION("-q", "--quiet",
           "don't print OK for each successfully verified file"),
    // [GNU]
    OPTION("-s", "--status",
           "don't output anything, status code shows success"),
    // [GNU]
    OPTION("-t", "--text", "read in text mode (default if reading tty stdin)"),
    // [GNU]
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines"),
    // [GNU]
    OPTION("", "--strict",
           "exit non-zero for improperly formatted checksum lines"),
    // [GNU]
    OPTION("", "--tag",
           "create a BSD-style checksum (the default for compatibility)"),
    // [GNU]
    OPTION("", "--untagged",
           "create a reversed style checksum, without digest type"),
    // [GNU]
    OPTION("-z", "--zero",
           "end each output line with NUL, not newline, and disable file name "
           "escaping"),
    // [GNU]
    OPTION("", "--raw", "print a raw binary digest, not hexadecimal"),
    // [GNU]
    OPTION("", "--base64", "print a base64-encoded digest"),
    // [GNU]
    OPTION("-l", "--length",
           "digest length in bits; must not exceed the maximum for the blake2 "
           "algorithm and must be a multiple of 8",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--debug", "print debug output"),
    // [GNU] -r: create a reversed style checksum, without digest type
    OPTION("-r", "", "create a reversed style checksum, without digest type")};

namespace cksum_pipeline {
namespace cp = core::pipeline;

// [GNU] cksum.c algorithm tables.  The first three entries are the legacy
// (non-cryptographic) checksums; the rest are real digests that support
// tagged output, --untagged, --base64, --raw and --check verification.
enum class Algorithm {
  BSD,
  SYSV,
  CRC,
  CRC32B,
  MD5,
  SHA1,
  SHA224,
  SHA256,
  SHA384,
  SHA512,
  BLAKE2B,
  SM3
};

struct AlgorithmEntry {
  Algorithm algo;
  const char* name;  // [GNU] -a/--algorithm argument
  const char* tag;   // [GNU] algorithm_tags[] / DIGEST_TYPE_STRING
  int bits;          // [GNU] algorithm_bits[]
};

// Order matches the "Valid arguments are:" diagnostic of GNU cksum.
inline constexpr std::array kAlgorithms = {
    AlgorithmEntry{Algorithm::BSD, "bsd", "BSD", 16},
    AlgorithmEntry{Algorithm::SYSV, "sysv", "SYSV", 16},
    AlgorithmEntry{Algorithm::CRC, "crc", "CRC", 32},
    AlgorithmEntry{Algorithm::CRC32B, "crc32b", "CRC32B", 32},
    AlgorithmEntry{Algorithm::MD5, "md5", "MD5", 128},
    AlgorithmEntry{Algorithm::SHA1, "sha1", "SHA1", 160},
    AlgorithmEntry{Algorithm::SHA224, "sha224", "SHA224", 224},
    AlgorithmEntry{Algorithm::SHA256, "sha256", "SHA256", 256},
    AlgorithmEntry{Algorithm::SHA384, "sha384", "SHA384", 384},
    AlgorithmEntry{Algorithm::SHA512, "sha512", "SHA512", 512},
    AlgorithmEntry{Algorithm::BLAKE2B, "blake2b", "BLAKE2b", 512},
    AlgorithmEntry{Algorithm::SM3, "sm3", "SM3", 256},
};

auto is_legacy(Algorithm a) -> bool {
  // [GNU] CRC32B flows through the digest output machinery, not the
  // legacy "checksum length" formats.
  return a == Algorithm::BSD || a == Algorithm::SYSV || a == Algorithm::CRC;
}

auto algorithm_entry(Algorithm a) -> const AlgorithmEntry& {
  for (const auto& e : kAlgorithms) {
    if (e.algo == a) return e;
  }
  std::unreachable();
}

auto to_hash_algorithm(Algorithm a) -> portable_digest::HashAlgorithm {
  switch (a) {
    case Algorithm::CRC32B:
      return portable_digest::HashAlgorithm::Crc32b;
    case Algorithm::MD5:
      return portable_digest::HashAlgorithm::Md5;
    case Algorithm::SHA1:
      return portable_digest::HashAlgorithm::Sha1;
    case Algorithm::SHA224:
      return portable_digest::HashAlgorithm::Sha224;
    case Algorithm::SHA256:
      return portable_digest::HashAlgorithm::Sha256;
    case Algorithm::SHA384:
      return portable_digest::HashAlgorithm::Sha384;
    case Algorithm::SHA512:
      return portable_digest::HashAlgorithm::Sha512;
    case Algorithm::BLAKE2B:
      return portable_digest::HashAlgorithm::Blake2b;
    case Algorithm::SM3:
      return portable_digest::HashAlgorithm::Sm3;
    default:
      std::unreachable();
  }
}

struct Config {
  Algorithm algorithm = Algorithm::CRC;
  bool algorithm_specified = false;
  bool check_mode = false;
  bool ignore_missing = false;
  bool quiet = false;
  bool status = false;
  bool warn = false;
  bool strict = false;
  int tag_mode = -1;  // [GNU] prefix_tag: -1 unset, 0 untagged, 1 tagged
  bool zero_terminated = false;
  bool raw_output = false;
  bool base64_output = false;
  bool debug = false;
  int binary = -1;  // [GNU] -1 unset, 0 --text, 1 --binary
  std::string length_str;
  int digest_length = 0;      // bits; 0 = default for algorithm
  bool had_operands = false;  // [GNU] optind != argc
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<CKSUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto algo = ctx.get<std::string>("--algorithm", "");
  if (algo.empty()) algo = ctx.get<std::string>("-a", "");
  if (!algo.empty()) {
    cfg.algorithm_specified = true;
    bool found = false;
    for (const auto& e : kAlgorithms) {
      if (algo == e.name) {
        cfg.algorithm = e.algo;
        found = true;
        break;
      }
    }
    if (!found) {
      std::string msg = "invalid argument '" + algo +
                        "' for '--algorithm'\n"
                        "Valid arguments are:";
      for (const auto& e : kAlgorithms) {
        msg += "\n  - '";
        msg += e.name;
        msg += '\'';
      }
      msg += "\nTry 'cksum --help' for more information.";
      return std::unexpected(msg);
    }
  }

  cfg.check_mode = ctx.has("--check") || ctx.has("-c");

  // [GNU] deprecated no-op flags; they only affect the untagged output
  // marker ('*' vs ' ') for digest algorithms.
  if (ctx.has("--binary") || ctx.has("-b")) cfg.binary = 1;
  if (ctx.has("--text") || ctx.has("-t")) cfg.binary = 0;

  cfg.ignore_missing = ctx.has("--ignore-missing");
  cfg.quiet = ctx.has("--quiet") || ctx.has("-q");
  cfg.status = ctx.has("--status") || ctx.has("-s");
  cfg.warn = ctx.has("--warn") || ctx.has("-w");
  cfg.strict = ctx.has("--strict");
  if (ctx.has("--tag")) cfg.tag_mode = 1;
  if (ctx.has("--untagged") || ctx.has("-r")) cfg.tag_mode = 0;
  cfg.zero_terminated = ctx.has("--zero") || ctx.has("-z");
  cfg.raw_output = ctx.has("--raw");
  cfg.base64_output = ctx.has("--base64");
  cfg.debug = ctx.has("--debug");

  auto length_str = ctx.get<std::string>("--length", "");
  if (length_str.empty()) {
    length_str = ctx.get<std::string>("-l", "");
  }
  if (!length_str.empty()) {
    cfg.length_str = length_str;
    auto [ptr, ec] = std::from_chars(length_str.data(),
                                     length_str.data() + length_str.size(),
                                     cfg.digest_length);
    if (ec != std::errc() || ptr != length_str.data() + length_str.size() ||
        cfg.digest_length < 0) {
      return std::unexpected("invalid length");
    }
  }

  if (ctx.has("--tag") && ctx.has("--untagged")) {
    return std::unexpected("--tag and --untagged are mutually exclusive");
  }

  cfg.had_operands = !ctx.positionals.empty();
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    for (const auto& file : expand_file_operand(file_arg)) {
      cfg.files.push_back(file);
    }
  }

  if (cfg.files.empty()) {
    cfg.files.push_back("-");
  }

  return cfg;
}

// [GNU] option-interaction checks performed after parsing.  Errors that GNU
// reports through error(EXIT_FAILURE) return false after printing
// "cksum: <msg>"; errors that GNU reports through usage(EXIT_FAILURE) also
// print the "Try 'cksum --help' ..." hint.
auto validate_config(const Config& cfg) -> bool {
  auto fail = [](const std::string& msg) {
    safeErrorPrintLn("cksum: " + msg);
    return false;
  };
  auto usage_fail = [&fail](const std::string& msg) {
    fail(msg);
    safeErrorPrintLn("Try 'cksum --help' for more information.");
    return false;
  };

  if (cfg.digest_length && cfg.algorithm != Algorithm::BLAKE2B &&
      cfg.algorithm != Algorithm::CRC) {
    return fail("--length is only supported with --algorithm=blake2b");
  }
  if (cfg.algorithm == Algorithm::BLAKE2B) {
    if (cfg.digest_length > 512) {
      fail("invalid length: '" + cfg.length_str + "'");
      return fail("maximum digest length for 'BLAKE2b' is 512 bits");
    }
    if (cfg.digest_length % 8 != 0) {
      fail("invalid length: '" + cfg.length_str + "'");
      return fail("length is not a multiple of 8");
    }
  }

  if (cfg.check_mode && cfg.algorithm_specified && is_legacy(cfg.algorithm)) {
    return fail("--check is not supported with --algorithm={bsd,sysv,crc}");
  }

  if (cfg.base64_output && cfg.raw_output) {
    return usage_fail("--base64 and --raw are mutually exclusive");
  }

  if (cfg.zero_terminated && cfg.check_mode) {
    return usage_fail(
        "the --zero option is not supported when verifying checksums");
  }

  if (cfg.tag_mode == 1 && cfg.check_mode) {
    return usage_fail(
        "the --tag option is meaningless when verifying checksums");
  }

  if (cfg.binary >= 0 && cfg.check_mode) {
    return usage_fail(
        "the --binary and --text options are meaningless when verifying "
        "checksums");
  }

  if (cfg.ignore_missing && !cfg.check_mode) {
    return usage_fail(
        "the --ignore-missing option is meaningful only when verifying "
        "checksums");
  }
  if (cfg.status && !cfg.check_mode) {
    return usage_fail(
        "the --status option is meaningful only when verifying checksums");
  }
  if (cfg.warn && !cfg.check_mode) {
    return usage_fail(
        "the --warn option is meaningful only when verifying checksums");
  }
  if (cfg.quiet && !cfg.check_mode) {
    return usage_fail(
        "the --quiet option is meaningful only when verifying checksums");
  }
  if (cfg.strict && !cfg.check_mode) {
    return usage_fail(
        "the --strict option is meaningful only when verifying checksums");
  }

  const int prefix_tag = cfg.tag_mode == -1 ? 1 : cfg.tag_mode;
  if (prefix_tag && cfg.binary == 0) {
    return usage_fail("--text mode is only supported with --untagged");
  }

  if (!cfg.check_mode && cfg.raw_output && cfg.files.size() > 1) {
    return fail("the --raw option is not supported with multiple files");
  }

  return true;
}

struct FileData {
  std::vector<unsigned char> data;
  uint64_t byte_count = 0;
};

auto input_open_error(std::string_view path) -> std::string {
  // [GNU] diagnostics use "cksum: FILE: <strerror>" wording.
  return std::string(path) + ": " + portable_digest::open_error_reason(path);
}

auto read_file(const std::string& filename) -> cp::Result<FileData> {
  FileData fd;

  if (filename == "-" || filename.empty()) {
    // [GNU] closed stdin (<&-) reports "-: Bad file descriptor" rather
    // than hashing an empty stream.
    if (file_io::stdin_is_bad()) {
      return std::unexpected(std::string(filename.empty() ? "-" : filename) +
                             ": Bad file descriptor");
    }
    fd.data.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
  } else {
    std::ifstream f(native_path::normalize_api_operand(filename),
                    std::ios::binary);
    if (!f) {
      return std::unexpected(input_open_error(filename));
    }
    fd.data.assign(std::istreambuf_iterator<char>(f),
                   std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      return std::unexpected(std::string("error reading '") + filename + "'");
    }
  }

  fd.byte_count = fd.data.size();
  return fd;
}

auto read_crc_file(const std::string& filename)
    -> cp::Result<portable_digest::PosixCksumResult> {
  std::istream* input = &std::cin;
  std::ifstream file;
  if (filename == "-" || filename.empty()) {
    // [GNU] closed stdin (<&-) reports "-: Bad file descriptor".
    if (file_io::stdin_is_bad()) {
      return std::unexpected(std::string(filename.empty() ? "-" : filename) +
                             ": Bad file descriptor");
    }
  } else {
    file.open(
        std::filesystem::u8path(native_path::normalize_api_operand(filename)),
        std::ios::binary);
    if (!file) {
      return std::unexpected(input_open_error(filename));
    }
    input = &file;
  }

  auto result = portable_digest::posix_cksum_stream(*input);
  if (!result) {
    if (filename == "-" || filename.empty()) {
      return std::unexpected("error reading from standard input");
    }
    return std::unexpected(std::string("error reading '") + filename + "'");
  }
  return *result;
}

auto calculate_sysv(const std::vector<unsigned char>& data)
    -> std::pair<uint32_t, uint32_t> {
  // [GNU] sysv_sum_stream: sum of all bytes modulo 2^32, folded to 16 bits;
  // the size field counts 512-byte blocks.
  uint32_t s = 0;
  for (unsigned char byte : data) {
    s += byte;
  }
  uint32_t r = (s & 0xffffU) + (s >> 16U);
  uint32_t checksum = (r & 0xffffU) + (r >> 16U);
  uint32_t blocks =
      static_cast<uint32_t>((data.size() + 511) / 512);  // ceil division
  return {checksum, blocks};
}

auto calculate_bsd(const std::vector<unsigned char>& data)
    -> std::pair<uint32_t, uint32_t> {
  // [GNU] bsd_sum_stream: 16-bit checksum with right rotation, plus byte
  // count in 1024-byte blocks.
  uint32_t checksum = 0;
  for (unsigned char byte : data) {
    checksum = (checksum >> 1) + ((checksum & 1) << 15);
    checksum += byte;
    checksum &= 0xFFFF;
  }
  uint32_t blocks =
      static_cast<uint32_t>((data.size() + 1023) / 1024);  // ceil division
  return {checksum, blocks};
}

auto to_hex(const std::vector<unsigned char>& data) -> std::string {
  std::string result;
  result.reserve(data.size() * 2);
  for (unsigned char byte : data) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", byte);
    result += buf;
  }
  return result;
}

auto to_base64(const std::vector<unsigned char>& data) -> std::string {
  static constexpr char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    unsigned int n = static_cast<unsigned int>(data[i]) << 16;
    if (i + 1 < data.size()) n |= static_cast<unsigned int>(data[i + 1]) << 8;
    if (i + 2 < data.size()) n |= static_cast<unsigned int>(data[i + 2]);
    result += table[(n >> 18) & 0x3F];
    result += table[(n >> 12) & 0x3F];
    result += (i + 1 < data.size()) ? table[(n >> 6) & 0x3F] : '=';
    result += (i + 2 < data.size()) ? table[n & 0x3F] : '=';
  }
  return result;
}

// Convert raw CRC32 bytes for raw/base64 output
auto crc32_to_bytes(uint32_t crc) -> std::vector<unsigned char> {
  return {static_cast<unsigned char>((crc >> 24) & 0xFF),
          static_cast<unsigned char>((crc >> 16) & 0xFF),
          static_cast<unsigned char>((crc >> 8) & 0xFF),
          static_cast<unsigned char>(crc & 0xFF)};
}

// Parse a hex string to bytes
auto hex_to_bytes(const std::string& hex) -> std::vector<unsigned char> {
  auto hex_value = [](char ch) -> unsigned char {
    if (ch >= '0' && ch <= '9') return static_cast<unsigned char>(ch - '0');
    if (ch >= 'a' && ch <= 'f') {
      return static_cast<unsigned char>(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
      return static_cast<unsigned char>(ch - 'A' + 10);
    }
    return 0;
  };

  std::vector<unsigned char> bytes;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<unsigned char>((hex_value(hex[i]) << 4) |
                                               hex_value(hex[i + 1])));
  }
  return bytes;
}

// [GNU] problematic_chars(): escape is needed when the name contains
// '\\', '\n' or '\r'.
auto has_problematic_chars(std::string_view s) -> bool {
  return s.find_first_of("\\\n\r") != std::string_view::npos;
}

// [GNU] print_filename(escape=true): \n -> "\\n", \r -> "\\r", \ -> "\\\\".
auto escape_filename(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

// [GNU] output_bsd/output_sysv/output_crc: the legacy checksums ignore
// --tag/--untagged and always print "checksum length [file]".
auto output_legacy(const Config& cfg, Algorithm algo, uint32_t checksum,
                   uint64_t length, const std::string& filename, bool args)
    -> void {
  const char sep = cfg.zero_terminated ? '\0' : '\n';

  if (cfg.raw_output) {
    // [GNU] network byte order (big endian).
    if (algo == Algorithm::CRC) {
      unsigned char buf[4] = {static_cast<unsigned char>(checksum >> 24),
                              static_cast<unsigned char>(checksum >> 16),
                              static_cast<unsigned char>(checksum >> 8),
                              static_cast<unsigned char>(checksum)};
      fwrite(buf, 1, sizeof buf, stdout);
    } else {
      unsigned char buf[2] = {static_cast<unsigned char>(checksum >> 8),
                              static_cast<unsigned char>(checksum)};
      fwrite(buf, 1, sizeof buf, stdout);
    }
    return;
  }

  // [GNU] output_crc ignores tagged mode entirely: CRC always prints
  // "%u %ju [file]" (cksum.c:357-378); no "CRC32 (f) = hex" form exists.

  char buf[64];
  switch (algo) {
    case Algorithm::BSD:
      // %05d %5s with the block count human_readable'd at 1024.
      snprintf(buf, sizeof buf, "%05u %5llu", checksum,
               static_cast<unsigned long long>(length));
      break;
    case Algorithm::SYSV:
    case Algorithm::CRC:
    default:
      snprintf(buf, sizeof buf, "%u %llu", checksum,
               static_cast<unsigned long long>(length));
      break;
  }
  safePrint(buf);
  if (args) {
    safePrint(" ");
    safePrint(filename);
  }
  safePrint(std::string(1, sep));
}

// [GNU] output_file(): tagged "TAG[-bits] (file) = digest" (the default for
// digest algorithms) or untagged "digest [*| ]file".
auto output_digest_file(const Config& cfg, const AlgorithmEntry& entry,
                        int length_bits, const std::string& digest_hex,
                        const std::string& filename, int binary_file) -> void {
  const char sep = cfg.zero_terminated ? '\0' : '\n';
  const auto digest_bytes = hex_to_bytes(digest_hex);

  if (cfg.raw_output) {
    fwrite(digest_bytes.data(), 1, digest_bytes.size(), stdout);
    return;
  }

  const std::string digest_str =
      cfg.base64_output ? to_base64(digest_bytes) : digest_hex;
  const bool tagged = cfg.tag_mode != 0;  // [GNU] tagged is the default
  const bool escape = sep == '\n' && has_problematic_chars(filename);
  const std::string shown_name = escape ? escape_filename(filename) : filename;

  if (escape) safePrint("\\");

  if (tagged) {
    std::string tag = entry.tag;
    if (entry.algo == Algorithm::BLAKE2B && length_bits < entry.bits) {
      tag += "-" + std::to_string(length_bits);
    }
    safePrint(tag);
    safePrint(" (");
    safePrint(shown_name);
    safePrint(") = ");
    safePrint(digest_str);
  } else {
    safePrint(digest_str);
    safePrint(" ");
    safePrint(binary_file ? "*" : " ");
    safePrint(shown_name);
  }
  safePrint(std::string(1, sep));
}

// ===== [GNU] --check verification =========================================
// Mirrors digest_check() in cksum.c, reusing the shared *sum check-mode
// machinery (portable_digest::sum_parse_check_line) for the actual line
// parsing once the algorithm tag has been identified.

// [WinuxCmd] legacy untagged check record: "<decimal-sum> <size> <file>",
// the format printed by "cksum FILE" for the default CRC algorithm.  GNU's
// --check only accepts tagged lines without -a; WinuxCmd additionally
// verifies its own default output.
struct LegacyCheckLine {
  uint64_t sum = 0;
  uint64_t size = 0;
  std::string filename;
};

auto parse_legacy_check_line(std::string_view line)
    -> std::optional<LegacyCheckLine> {
  auto take_decimal = [&](size_t& pos, uint64_t& out) {
    size_t end = pos;
    while (end < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[end]))) {
      ++end;
    }
    if (end == pos) return false;
    auto [ptr, ec] = std::from_chars(line.data() + pos, line.data() + end, out);
    if (ec != std::errc()) return false;
    pos = end;
    return true;
  };
  auto skip_blanks = [&](size_t& pos) {
    if (pos >= line.size() || (line[pos] != ' ' && line[pos] != '\t')) {
      return false;
    }
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
      ++pos;
    }
    return pos < line.size();
  };

  size_t pos = 0;
  if (pos < line.size() && line[pos] == '\\') ++pos;
  LegacyCheckLine rec;
  if (!take_decimal(pos, rec.sum)) return std::nullopt;
  if (!skip_blanks(pos)) return std::nullopt;
  if (!take_decimal(pos, rec.size)) return std::nullopt;
  if (!skip_blanks(pos)) return std::nullopt;
  rec.filename = std::string(line.substr(pos));
  return rec;
}

// [GNU] algorithm_from_tag(): the tag is the leading run of characters that
// is not a blank, '-' or '(' (after blanks and an optional '\' escape
// marker).
auto detect_tag_entry(std::string_view line) -> const AlgorithmEntry* {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    ++i;
  }
  if (i < line.size() && line[i] == '\\') ++i;
  size_t j = i;
  while (j < line.size() && line[j] != ' ' && line[j] != '\t' &&
         line[j] != '-' && line[j] != '(') {
    ++j;
  }
  const std::string_view tag = line.substr(i, j - i);
  for (const auto& e : kAlgorithms) {
    if (tag == e.tag) return &e;
  }
  return nullptr;
}

auto check_one_file(const std::string& checkfile_name, const Config& cfg,
                    int& bsd_reversed) -> bool {
  intmax_t n_misformatted_lines = 0;
  intmax_t n_mismatched_checksums = 0;
  intmax_t n_computed_checksums = 0;
  intmax_t n_open_or_read_failures = 0;
  bool properly_formatted_lines = false;
  bool matched_checksums = false;

  const bool is_stdin = checkfile_name == "-";
  std::string display_name = is_stdin ? "standard input" : checkfile_name;
  std::istream* input = &std::cin;
  std::ifstream checkfile;
  if (!is_stdin) {
    checkfile.open(native_path::normalize_api_operand(checkfile_name));
    if (!checkfile) {
      std::error_code ec;
      // [GNU] fopen() succeeds on a directory and the first getline() then
      // fails: GNU reports "<file>: read error", not "Is a directory".
      if (std::filesystem::is_directory(std::filesystem::u8path(checkfile_name),
                                        ec) &&
          !ec) {
        safeErrorPrintLn(
            "cksum: " + portable_digest::detail::sum_quotef(checkfile_name) +
            ": Is a directory");
      } else {
        safeErrorPrintLn(
            "cksum: " + portable_digest::detail::sum_quotef(checkfile_name) +
            ": " + portable_digest::open_error_reason(checkfile_name));
      }
      return false;
    }
    input = &checkfile;
  }

  std::string line;
  intmax_t line_number = 0;
  while (std::getline(*input, line)) {
    ++line_number;
    if (!line.empty() && line.front() == '#') continue;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    // [GNU] split_3(): without -a, only tagged lines are supported and the
    // tag selects the algorithm; legacy (crc/sysv/bsd) tags are rejected.
    const AlgorithmEntry* entry = nullptr;
    std::optional<LegacyCheckLine> legacy;
    if (cfg.algorithm_specified) {
      entry = &algorithm_entry(cfg.algorithm);
    } else {
      const AlgorithmEntry* detected = detect_tag_entry(line);
      if (detected != nullptr && !is_legacy(detected->algo)) {
        entry = detected;
      } else {
        // [WinuxCmd] fall back to the default CRC untagged record
        // "<sum> <size> <file>" so "cksum FILE > list; cksum -c list"
        // round-trips.
        legacy = parse_legacy_check_line(line);
      }
    }

    std::optional<portable_digest::SumCheckLine> parsed;
    if (entry != nullptr) {
      const size_t hex_bytes =
          entry->algo == Algorithm::BLAKE2B ? 0 : entry->bits / 4;
      parsed = portable_digest::sum_parse_check_line(line, entry->tag,
                                                     hex_bytes, bsd_reversed);
    }

    if ((!parsed && !legacy) ||
        (parsed && is_stdin && parsed->filename == "-") ||
        (legacy && is_stdin && legacy->filename == "-")) {
      ++n_misformatted_lines;
      if (cfg.warn) {
        safeErrorPrintLn(
            "cksum: " + portable_digest::detail::sum_quotef(display_name) +
            ": " + std::to_string(line_number) +
            ": improperly formatted checksum line");
      }
      continue;
    }

    properly_formatted_lines = true;

    const std::string& target = legacy ? legacy->filename : parsed->filename;

    bool missing = false;
    if (cfg.ignore_missing && target != "-") {
      std::error_code ec;
      missing = !std::filesystem::exists(std::filesystem::u8path(target), ec);
    }
    if (missing) continue;

    bool match = false;
    bool read_ok = false;
    if (legacy) {
      // [WinuxCmd] verify "<sum> <size> <file>" against the CRC output.
      auto crc_result = read_crc_file(target);
      if (crc_result) {
        read_ok = true;
        match = crc_result->checksum == legacy->sum &&
                crc_result->bytes == legacy->size;
      }
    } else {
      const size_t out_bytes =
          entry->algo == Algorithm::BLAKE2B ? parsed->digest_bytes : 0;
      auto hash_result = portable_digest::hash_file_hex(
          to_hash_algorithm(entry->algo), parsed->filename, false, out_bytes);
      if (hash_result) {
        read_ok = true;
        match =
            hash_result->size() == parsed->expected_hash.size() &&
            std::equal(hash_result->begin(), hash_result->end(),
                       parsed->expected_hash.begin(), [](char a, char b) {
                         return std::tolower(static_cast<unsigned char>(a)) ==
                                std::tolower(static_cast<unsigned char>(b));
                       });
      }
    }
    if (!read_ok) {
      ++n_open_or_read_failures;
      safeErrorPrintLn("cksum: " + portable_digest::detail::sum_quotef(target) +
                       ": " + portable_digest::open_error_reason(target));
      if (!cfg.status) {
        safePrintLn(portable_digest::detail::sum_quotef(target) +
                    ": FAILED open or read");
      }
      continue;
    }

    ++n_computed_checksums;
    if (match) {
      matched_checksums = true;
    } else {
      ++n_mismatched_checksums;
    }

    if (!cfg.status) {
      if (!match || !cfg.quiet) {
        safePrintLn(portable_digest::detail::sum_quotef(target) +
                    (match ? ": OK" : ": FAILED"));
      }
    }
  }

  if (!properly_formatted_lines) {
    // [GNU] this diagnostic does not include the algorithm name.
    safeErrorPrintLn(
        "cksum: " + portable_digest::detail::sum_quotef(display_name) +
        ": no properly formatted checksum lines found");
  } else if (!cfg.status) {
    if (cfg.warn && n_misformatted_lines != 0) {
      safeErrorPrintLn(
          "cksum: WARNING: " + std::to_string(n_misformatted_lines) +
          (n_misformatted_lines == 1 ? " line is improperly formatted"
                                     : " lines are improperly formatted"));
    }
    if (n_open_or_read_failures != 0) {
      safeErrorPrintLn(
          "cksum: WARNING: " + std::to_string(n_open_or_read_failures) +
          (n_open_or_read_failures == 1 ? " listed file could not be read"
                                        : " listed files could not be read"));
    }
    if (n_mismatched_checksums != 0) {
      safeErrorPrintLn(
          "cksum: WARNING: " + std::to_string(n_mismatched_checksums) + " of " +
          std::to_string(n_computed_checksums) +
          " computed checksums did NOT match");
    }
  }

  // [GNU] --ignore-missing records that skipped every file still count
  // as a successful check; nothing was computed, nothing failed.
  bool ok = properly_formatted_lines && n_mismatched_checksums == 0 &&
            n_open_or_read_failures == 0 &&
            (!cfg.strict || n_misformatted_lines == 0);
  if (!matched_checksums && !(cfg.ignore_missing && n_computed_checksums == 0))
    ok = false;
  return ok;
}

// [GNU] digest_check() loops over every operand; "-" (or no operand) reads
// the list from standard input.
auto run_check_mode(const Config& cfg) -> int {
  int bsd_reversed = -1;
  bool ok = true;
  for (const auto& checkfile : cfg.files) {
    ok = check_one_file(checkfile, cfg, bsd_reversed) && ok;
  }
  return ok ? 0 : 1;
}

auto run(const Config& cfg) -> int {
  if (!validate_config(cfg)) return 1;
  if (cfg.check_mode) {
    return run_check_mode(cfg);
  }

  const auto& entry = algorithm_entry(cfg.algorithm);
  // [GNU] blake2b digest length defaults to the algorithm maximum.
  const int length_bits =
      cfg.algorithm == Algorithm::BLAKE2B && cfg.digest_length == 0
          ? entry.bits
          : cfg.digest_length;
  const bool legacy = is_legacy(cfg.algorithm);
  // [GNU] the untagged mode marker defaults to text (' '); --binary
  // selects '*'.  File reads are always binary-capable on this platform.
  const int binary = cfg.binary < 0 ? 0 : cfg.binary;

  bool all_ok = true;
  for (const auto& file : cfg.files) {
    if (legacy) {
      uint32_t checksum = 0;
      uint64_t display_length = 0;
      if (cfg.algorithm == Algorithm::CRC) {
        auto crc_result = read_crc_file(file);
        if (!crc_result) {
          cp::report_error(crc_result, L"cksum");
          all_ok = false;
          continue;
        }
        checksum = crc_result->checksum;
        display_length = crc_result->bytes;
      } else {
        auto file_data = read_file(file);
        if (!file_data) {
          cp::report_error(file_data, L"cksum");
          all_ok = false;
          continue;
        }
        auto [sum, blocks] = cfg.algorithm == Algorithm::SYSV
                                 ? calculate_sysv(file_data->data)
                                 : calculate_bsd(file_data->data);
        checksum = sum;
        display_length = blocks;
      }

      if (cfg.debug) {
        safeErrorPrint("cksum: algorithm: " + std::string(entry.name) + "\n");
        safeErrorPrint("cksum: file: " + file + "\n");
        safeErrorPrint("cksum: bytes: " + std::to_string(display_length) +
                       "\n");
      }

      // [GNU] the legacy formats print the filename only when operands were
      // given on the command line.
      output_legacy(cfg, cfg.algorithm, checksum, display_length, file,
                    cfg.had_operands);
    } else {
      const size_t out_bytes = cfg.algorithm == Algorithm::BLAKE2B
                                   ? static_cast<size_t>(length_bits / 8)
                                   : 0;
      auto hex = portable_digest::hash_file_hex(
          to_hash_algorithm(cfg.algorithm), file, false, out_bytes);
      if (!hex) {
        safeErrorPrintLn("cksum: " + portable_digest::detail::sum_quotef(file) +
                         ": " + portable_digest::open_error_reason(file));
        all_ok = false;
        continue;
      }

      if (cfg.debug) {
        safeErrorPrint("cksum: algorithm: " + std::string(entry.name) + "\n");
        safeErrorPrint("cksum: file: " + file + "\n");
        safeErrorPrint("cksum: length: " + std::to_string(length_bits) + "\n");
      }

      output_digest_file(cfg, entry, length_bits, *hex, file, binary);
    }
  }

  return all_ok ? 0 : 1;
}

}  // namespace cksum_pipeline

REGISTER_COMMAND(
    cksum, "cksum", "cksum [OPTION]... [FILE]...",
    "Print or verify checksums.\n"
    "\n"
    "By default use the 32 bit CRC algorithm.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "  -a, --algorithm=TYPE  select the digest type to use.  See DIGEST "
    "below\n"
    "      --base64          print base64-encoded digests (not hexadecimal)\n"
    "  -b, --binary          read in binary mode (default unless reading tty "
    "stdin)\n"
    "  -c, --check           read checksums from the FILEs and check them\n"
    "      --ignore-missing  don't fail or report status for missing files\n"
    "  -l, --length=BITS     digest length in bits; must not exceed the max "
    "for\n"
    "                          the blake2 algorithm and must be a multiple "
    "of 8\n"
    "  -q, --quiet           don't print OK for each successfully verified "
    "file\n"
    "      --raw             print a raw binary digest, not hexadecimal\n"
    "  -s, --status          don't output anything, status code shows "
    "success\n"
    "      --strict          exit non-zero for improperly formatted checksum "
    "lines\n"
    "  -t, --text            read in text mode (default if reading tty "
    "stdin)\n"
    "      --tag             create a BSD-style checksum (the default for "
    "digest algorithms)\n"
    "      --untagged        create a reversed style checksum, without "
    "digest type\n"
    "  -w, --warn            warn about improperly formatted checksum lines\n"
    "  -z, --zero            end each output line with NUL, not newline\n"
    "      --debug           print debug output\n"
    "\n"
    "DIGEST determines the digest algorithm and default output format:\n"
    "\n"
    "  sysv    (equivalent to sum -s)\n"
    "  bsd     (equivalent to sum -r)\n"
    "  crc     (equivalent to cksum)\n"
    "  md5     (equivalent to md5sum)\n"
    "  sha1    (equivalent to sha1sum)\n"
    "  sha224  (equivalent to sha224sum)\n"
    "  sha256  (equivalent to sha256sum)\n"
    "  sha384  (equivalent to sha384sum)\n"
    "  sha512  (equivalent to sha512sum)\n"
    "  blake2b (equivalent to b2sum)\n"
    "  sm3     (only available through cksum)\n",
    "  cksum file.txt\n"
    "  cksum -a sha256 file.txt\n"
    "  cksum -a blake2b -l 128 file.txt\n"
    "  cksum --tag file.txt\n"
    "  cksum -c checksums.txt\n"
    "  echo \"test\" | cksum",
    "md5sum(1), sha1sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CKSUM_OPTIONS) {
  using namespace cksum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"cksum");
    return 1;
  }

  return run(*cfg_result);
}
