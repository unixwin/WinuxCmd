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
 *  - File: truncate.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for truncate.
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

auto constexpr TRUNCATE_OPTIONS = std::array{
    OPTION("-c", "--no-create", "do not create files", BOOL_TYPE),
    OPTION("-o", "--io-blocks", "treat SIZE as number of IO blocks", BOOL_TYPE),
    OPTION("-s", "--size", "set or adjust the file size by SIZE bytes",
           STRING_TYPE),
    OPTION("-r", "--reference", "base size on RFILE", STRING_TYPE)};

namespace truncate_pipeline {
namespace cp = core::pipeline;

enum class SizeMode {
  Absolute,
  Add,
  Subtract,
  AtMost,
  AtLeast,
  RoundDown,
  RoundUp
};

struct SizeSpec {
  SizeMode mode = SizeMode::Absolute;
  int64_t value = 0;
};

struct Config {
  bool no_create = false;
  bool io_blocks = false;
  SizeSpec size;
  std::string reference_file;
  SmallVector<std::string, 64> files;
};

auto pow_ld(long double base, int exponent) -> long double {
  long double value = 1.0L;
  for (int i = 0; i < exponent; ++i) {
    value *= base;
  }
  return value;
}

auto match_suffix(std::string& text) -> long double {
  struct Unit {
    const char* suffix;
    long double multiplier;
  };

  const std::array units{Unit{"QiB", pow_ld(1024.0L, 10)},
                         Unit{"RiB", pow_ld(1024.0L, 9)},
                         Unit{"YiB", pow_ld(1024.0L, 8)},
                         Unit{"ZiB", pow_ld(1024.0L, 7)},
                         Unit{"EiB", pow_ld(1024.0L, 6)},
                         Unit{"PiB", pow_ld(1024.0L, 5)},
                         Unit{"TiB", pow_ld(1024.0L, 4)},
                         Unit{"GiB", pow_ld(1024.0L, 3)},
                         Unit{"MiB", pow_ld(1024.0L, 2)},
                         Unit{"KiB", pow_ld(1024.0L, 1)},
                         Unit{"qib", pow_ld(1024.0L, 10)},
                         Unit{"rib", pow_ld(1024.0L, 9)},
                         Unit{"yib", pow_ld(1024.0L, 8)},
                         Unit{"zib", pow_ld(1024.0L, 7)},
                         Unit{"eib", pow_ld(1024.0L, 6)},
                         Unit{"pib", pow_ld(1024.0L, 5)},
                         Unit{"tib", pow_ld(1024.0L, 4)},
                         Unit{"gib", pow_ld(1024.0L, 3)},
                         Unit{"mib", pow_ld(1024.0L, 2)},
                         Unit{"kib", pow_ld(1024.0L, 1)},
                         Unit{"QB", pow_ld(1000.0L, 10)},
                         Unit{"RB", pow_ld(1000.0L, 9)},
                         Unit{"YB", pow_ld(1000.0L, 8)},
                         Unit{"ZB", pow_ld(1000.0L, 7)},
                         Unit{"EB", pow_ld(1000.0L, 6)},
                         Unit{"PB", pow_ld(1000.0L, 5)},
                         Unit{"TB", pow_ld(1000.0L, 4)},
                         Unit{"GB", pow_ld(1000.0L, 3)},
                         Unit{"MB", pow_ld(1000.0L, 2)},
                         Unit{"KB", pow_ld(1000.0L, 1)},
                         Unit{"qb", pow_ld(1000.0L, 10)},
                         Unit{"rb", pow_ld(1000.0L, 9)},
                         Unit{"yb", pow_ld(1000.0L, 8)},
                         Unit{"zb", pow_ld(1000.0L, 7)},
                         Unit{"eb", pow_ld(1000.0L, 6)},
                         Unit{"pb", pow_ld(1000.0L, 5)},
                         Unit{"tb", pow_ld(1000.0L, 4)},
                         Unit{"gb", pow_ld(1000.0L, 3)},
                         Unit{"mb", pow_ld(1000.0L, 2)},
                         Unit{"kb", pow_ld(1000.0L, 1)},
                         Unit{"Q", pow_ld(1024.0L, 10)},
                         Unit{"R", pow_ld(1024.0L, 9)},
                         Unit{"Y", pow_ld(1024.0L, 8)},
                         Unit{"Z", pow_ld(1024.0L, 7)},
                         Unit{"E", pow_ld(1024.0L, 6)},
                         Unit{"P", pow_ld(1024.0L, 5)},
                         Unit{"T", pow_ld(1024.0L, 4)},
                         Unit{"G", pow_ld(1024.0L, 3)},
                         Unit{"M", pow_ld(1024.0L, 2)},
                         Unit{"K", pow_ld(1024.0L, 1)},
                         Unit{"q", pow_ld(1024.0L, 10)},
                         Unit{"r", pow_ld(1024.0L, 9)},
                         Unit{"y", pow_ld(1024.0L, 8)},
                         Unit{"z", pow_ld(1024.0L, 7)},
                         Unit{"e", pow_ld(1024.0L, 6)},
                         Unit{"p", pow_ld(1024.0L, 5)},
                         Unit{"t", pow_ld(1024.0L, 4)},
                         Unit{"g", pow_ld(1024.0L, 3)},
                         Unit{"m", pow_ld(1024.0L, 2)},
                         Unit{"k", pow_ld(1024.0L, 1)},
                         Unit{"b", 512.0L},
                         Unit{"B", 1.0L}};

  for (const auto& unit : units) {
    std::string_view suffix(unit.suffix);
    if (text.size() >= suffix.size() &&
        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
      text.resize(text.size() - suffix.size());
      return unit.multiplier;
    }
  }

  return 1.0L;
}

auto scale_size(int64_t value, long double multiplier) -> cp::Result<int64_t> {
  long double scaled = static_cast<long double>(value) * multiplier;
  if (scaled < 0.0L ||
      scaled > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return std::unexpected("size overflow");
  }
  return static_cast<int64_t>(scaled);
}

auto parse_size(const std::string& size_str) -> cp::Result<SizeSpec> {
  try {
    std::string s = size_str;
    SizeSpec spec;

    if (!s.empty()) {
      switch (s[0]) {
        case '+':
          spec.mode = SizeMode::Add;
          s.erase(0, 1);
          break;
        case '-':
          spec.mode = SizeMode::Subtract;
          s.erase(0, 1);
          break;
        case '<':
          spec.mode = SizeMode::AtMost;
          s.erase(0, 1);
          break;
        case '>':
          spec.mode = SizeMode::AtLeast;
          s.erase(0, 1);
          break;
        case '/':
          spec.mode = SizeMode::RoundDown;
          s.erase(0, 1);
          break;
        case '%':
          spec.mode = SizeMode::RoundUp;
          s.erase(0, 1);
          break;
        default:
          break;
      }
    }

    if (s.empty()) {
      return std::unexpected("invalid size");
    }

    long double multiplier = match_suffix(s);
    if (s.empty()) {
      return std::unexpected("invalid size");
    }

    size_t consumed = 0;
    int64_t base_value = std::stoll(s, &consumed);
    if (consumed != s.size() || base_value < 0) {
      return std::unexpected("invalid size format");
    }

    auto scaled = scale_size(base_value, multiplier);
    if (!scaled) {
      return std::unexpected(scaled.error());
    }
    spec.value = *scaled;
    if ((spec.mode == SizeMode::RoundDown || spec.mode == SizeMode::RoundUp) &&
        spec.value == 0) {
      return std::unexpected("division by zero size");
    }

    return spec;
  } catch (...) {
    return std::unexpected("invalid size format");
  }
}

auto build_config(const CommandContext<TRUNCATE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.no_create =
      ctx.get<bool>("--no-create", false) || ctx.get<bool>("-c", false);
  cfg.io_blocks =
      ctx.get<bool>("--io-blocks", false) || ctx.get<bool>("-o", false);

  auto size_opt = ctx.get<std::string>("--size", "");
  if (size_opt.empty()) {
    size_opt = ctx.get<std::string>("-s", "");
  }

  if (size_opt.empty()) {
    auto ref_opt = ctx.get<std::string>("--reference", "");
    if (ref_opt.empty()) {
      ref_opt = ctx.get<std::string>("-r", "");
    }

    if (!ref_opt.empty()) {
      cfg.reference_file = ref_opt;
    } else {
      return std::unexpected("specify --size or --reference");
    }
  } else {
    auto size_result = parse_size(size_opt);
    if (!size_result) {
      return std::unexpected(size_result.error());
    }
    cfg.size = *size_result;
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
    return std::unexpected("missing file operand");
  }

  return cfg;
}

auto get_file_size(const std::string& file) -> cp::Result<int64_t> {
  std::error_code ec;
  auto size = std::filesystem::file_size(file, ec);
  if (ec) {
    return std::unexpected("cannot get file size");
  }
  if (size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return std::unexpected("file too large");
  }
  return static_cast<int64_t>(size);
}

auto io_block_size_for(const std::string& file) -> uint64_t {
  std::filesystem::path p(file);
  auto dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path(".");
  std::error_code ec;
  auto abs = std::filesystem::absolute(dir, ec);
  if (ec) {
    abs = dir;
  }

  wchar_t root[MAX_PATH];
  DWORD sectors_per_cluster = 0;
  DWORD bytes_per_sector = 0;
  DWORD free_clusters = 0;
  DWORD total_clusters = 0;

  auto wdir = abs.wstring();
  if (!GetVolumePathNameW(wdir.c_str(), root, MAX_PATH)) {
    return 512;
  }
  if (!GetDiskFreeSpaceW(root, &sectors_per_cluster, &bytes_per_sector,
                         &free_clusters, &total_clusters)) {
    return 512;
  }
  uint64_t block_size =
      static_cast<uint64_t>(sectors_per_cluster) * bytes_per_sector;
  return block_size == 0 ? 512 : block_size;
}

auto apply_size_mode(int64_t current_size, SizeSpec spec,
                     uint64_t io_block_size) -> cp::Result<int64_t> {
  if (io_block_size != 1) {
    auto scaled =
        scale_size(spec.value, static_cast<long double>(io_block_size));
    if (!scaled) {
      return std::unexpected(scaled.error());
    }
    spec.value = *scaled;
  }

  int64_t target = spec.value;
  switch (spec.mode) {
    case SizeMode::Absolute:
      break;
    case SizeMode::Add:
      if (spec.value > std::numeric_limits<int64_t>::max() - current_size) {
        return std::unexpected("size overflow");
      }
      target = current_size + spec.value;
      break;
    case SizeMode::Subtract:
      target = current_size > spec.value ? current_size - spec.value : 0;
      break;
    case SizeMode::AtMost:
      target = std::min(current_size, spec.value);
      break;
    case SizeMode::AtLeast:
      target = std::max(current_size, spec.value);
      break;
    case SizeMode::RoundDown:
      target = (current_size / spec.value) * spec.value;
      break;
    case SizeMode::RoundUp:
      if (current_size % spec.value == 0) {
        target = current_size;
      } else {
        int64_t multiplier = (current_size / spec.value) + 1;
        if (multiplier > std::numeric_limits<int64_t>::max() / spec.value) {
          return std::unexpected("size overflow");
        }
        target = multiplier * spec.value;
      }
      break;
  }

  return target;
}

auto set_file_size(const std::string& file, int64_t target_size) -> bool {
  HANDLE hFile =
      CreateFileA(file.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  LARGE_INTEGER distance;
  distance.QuadPart = target_size;
  bool ok = SetFilePointerEx(hFile, distance, nullptr, FILE_BEGIN) != 0 &&
            SetEndOfFile(hFile) != 0;
  CloseHandle(hFile);
  return ok;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;
  std::optional<int64_t> reference_size;

  if (!cfg.reference_file.empty()) {
    auto ref_size = get_file_size(cfg.reference_file);
    if (!ref_size) {
      safeErrorPrintLn("truncate: cannot stat reference file '" +
                       cfg.reference_file + "'");
      return 1;
    }
    reference_size = *ref_size;
  }

  for (const auto& file : cfg.files) {
    std::error_code ec;
    bool exists = std::filesystem::exists(file, ec);
    if (!exists) {
      if (cfg.no_create) {
        continue;
      }
    }

    int64_t current_size = 0;
    if (exists) {
      auto current = get_file_size(file);
      if (!current) {
        safeErrorPrintLn("truncate: cannot get size of '" + file + "'");
        all_ok = false;
        continue;
      }
      current_size = *current;
    }

    int64_t target_size = reference_size.value_or(0);
    if (!reference_size) {
      auto applied = apply_size_mode(
          current_size, cfg.size,
          cfg.io_blocks ? io_block_size_for(file) : static_cast<uint64_t>(1));
      if (!applied) {
        safeErrorPrintLn("truncate: " + std::string(applied.error()));
        all_ok = false;
        continue;
      }
      target_size = *applied;
    }

    if (!set_file_size(file, target_size)) {
      safeErrorPrintLn("truncate: cannot resize '" + file + "'");
      all_ok = false;
      continue;
    }
  }

  return all_ok ? 0 : 1;
}

}  // namespace truncate_pipeline

REGISTER_COMMAND(
    truncate, "truncate", "truncate OPTION... FILE...",
    "Shrink or extend the size of each FILE to the specified size.\n"
    "\n"
    "A FILE argument that does not exist is created.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "SIZE is an integer and optional unit (example: 10M is 10*1024*1024).\n"
    "Units are K, M, G, T, P, E, Z, Y (powers of 1024) or KB, MB,... (powers "
    "of 1000).\n"
    "\n"
    "SIZE may also be prefixed with +, -, <, >, /, or % for GNU-style\n"
    "relative, clamp, and rounding adjustments. -o treats SIZE as file-system\n"
    "I/O blocks.",
    "  truncate -s 0 file.txt          # empty file\n"
    "  truncate -s 1K file.txt         # 1KB file\n"
    "  truncate -s +100 file.txt      # extend by 100 bytes\n"
    "  truncate -s -50 file.txt       # shrink by 50 bytes\n"
    "  truncate -s 1M newfile.txt     # create 1MB file",
    "dd(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TRUNCATE_OPTIONS) {
  using namespace truncate_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"truncate");
    return 1;
  }

  return run(*cfg_result);
}
