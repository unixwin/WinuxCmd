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
 *  - File: stat.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for stat.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr STAT_OPTIONS = std::array{
    OPTION("-L", "--dereference", "follow symbolic links", BOOL_TYPE),
    OPTION("-f", "--file-system",
           "display file system status instead of file status", BOOL_TYPE),
    OPTION("-c", "--format", "use the specified FORMAT instead of the default",
           STRING_TYPE),
    OPTION("-t", "--terse", "print the information in terse form", BOOL_TYPE),
    OPTION("", "--printf", "like --format, but interpret backslash escapes",
           STRING_TYPE),
    OPTION("", "--cached", "ignored; Windows status is always fetched fresh",
           STRING_TYPE)};

namespace stat_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool dereference = false;
  bool file_system = false;
  std::string format;
  bool terse = false;
  bool printf_format = false;
  SmallVector<std::string, 64> files;
};

struct FileStatData {
  WIN32_FILE_ATTRIBUTE_DATA attrs{};
  uint64_t size = 0;
  uint64_t file_index = 0;
  uint64_t volume_serial = 0;
  uint32_t hard_links = 1;
  uint32_t io_block_size = 4096;
};

struct FileSystemStatData {
  uint64_t free_available = 0;
  uint64_t total_bytes = 0;
  uint64_t total_free = 0;
  uint32_t block_size = 4096;
  uint32_t max_component = 0;
  uint32_t serial = 0;
  std::string fs_name;
};

auto build_config(const CommandContext<STAT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.dereference =
      ctx.get<bool>("--dereference", false) || ctx.get<bool>("-L", false);
  cfg.file_system =
      ctx.get<bool>("--file-system", false) || ctx.get<bool>("-f", false);
  cfg.format = ctx.get<std::string>("--format", "");
  if (cfg.format.empty()) {
    cfg.format = ctx.get<std::string>("-c", "");
  }
  cfg.terse = ctx.get<bool>("--terse", false) || ctx.get<bool>("-t", false);

  auto printf_fmt = ctx.get<std::string>("--printf", "");
  if (!printf_fmt.empty()) {
    cfg.format = printf_fmt;
    cfg.printf_format = true;
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
    return std::unexpected("missing operand");
  }

  return cfg;
}

auto format_timestamp(FILETIME ft) -> std::string {
  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);

  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear,
           st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return std::string(buf);
}

auto filetime_to_unix_seconds(FILETIME ft) -> int64_t {
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  constexpr uint64_t kWindowsToUnixEpoch100ns = 116444736000000000ULL;
  if (value.QuadPart < kWindowsToUnixEpoch100ns) return 0;
  return static_cast<int64_t>((value.QuadPart - kWindowsToUnixEpoch100ns) /
                              10000000ULL);
}

auto format_size(uint64_t size) -> std::string {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double dsize = static_cast<double>(size);

  while (dsize >= 1024.0 && unit_index < 4) {
    dsize /= 1024.0;
    unit_index++;
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "%.2f %s", dsize, units[unit_index]);
  return std::string(buf);
}

auto format_permissions(DWORD attrs) -> std::string {
  std::string perm;
  perm += (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 'd' : '-';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? 'r' : 'r';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? '-' : 'w';
  perm += '-';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? 'r' : 'r';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? '-' : 'w';
  perm += '-';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? 'r' : 'r';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? '-' : 'w';
  perm += '-';
  return perm;
}

auto file_type_name(DWORD attrs) -> std::string {
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) return "directory";
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return "symbolic link";
  return "regular file";
}

auto format_mode_octal(DWORD attrs) -> std::string {
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    return (attrs & FILE_ATTRIBUTE_READONLY) ? "555" : "777";
  }
  return (attrs & FILE_ATTRIBUTE_READONLY) ? "444" : "666";
}

auto io_block_size_for(const std::filesystem::path& p) -> uint32_t {
  auto dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path(".");
  std::error_code ec;
  auto absolute = std::filesystem::absolute(dir, ec);
  auto wdir = ec ? dir.wstring() : absolute.wstring();

  wchar_t root[MAX_PATH];
  if (!GetVolumePathNameW(wdir.c_str(), root, MAX_PATH)) return 4096;

  DWORD sectors_per_cluster = 0;
  DWORD bytes_per_sector = 0;
  DWORD free_clusters = 0;
  DWORD total_clusters = 0;
  if (!GetDiskFreeSpaceW(root, &sectors_per_cluster, &bytes_per_sector,
                         &free_clusters, &total_clusters)) {
    return 4096;
  }

  uint64_t block_size =
      static_cast<uint64_t>(sectors_per_cluster) * bytes_per_sector;
  if (block_size == 0 || block_size > std::numeric_limits<uint32_t>::max()) {
    return 4096;
  }
  return static_cast<uint32_t>(block_size);
}

auto load_file_stat(const std::filesystem::path& p)
    -> cp::Result<FileStatData> {
  FileStatData stat;
  if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &stat.attrs)) {
    return std::unexpected("Access denied");
  }

  stat.size = stat.attrs.nFileSizeLow +
              (static_cast<uint64_t>(stat.attrs.nFileSizeHigh) << 32);
  stat.io_block_size = io_block_size_for(p);

  HANDLE h =
      CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(h, &info)) {
      stat.file_index = info.nFileIndexLow +
                        (static_cast<uint64_t>(info.nFileIndexHigh) << 32);
      stat.volume_serial = info.dwVolumeSerialNumber;
      stat.hard_links = std::max<DWORD>(info.nNumberOfLinks, 1);
    }
    CloseHandle(h);
  }

  return stat;
}

auto expand_backslash_escapes(std::string_view input) -> std::string {
  std::string out;
  out.reserve(input.size());

  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] != '\\' || i + 1 >= input.size()) {
      out.push_back(input[i]);
      continue;
    }

    char escaped = input[++i];
    switch (escaped) {
      case 'a':
        out.push_back('\a');
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 'v':
        out.push_back('\v');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '0':
        if (i + 1 < input.size() && input[i + 1] >= '0' &&
            input[i + 1] <= '7') {
          int value = 0;
          int digits = 0;
          while (i + 1 < input.size() && digits < 3 && input[i + 1] >= '0' &&
                 input[i + 1] <= '7') {
            value = value * 8 + (input[++i] - '0');
            ++digits;
          }
          out.push_back(static_cast<char>(value));
        } else {
          out.push_back('\0');
        }
        break;
      default:
        out.push_back(escaped);
        break;
    }
  }

  return out;
}

auto load_file_system_stat(const std::string& filename)
    -> cp::Result<FileSystemStatData> {
  std::error_code ec;
  std::filesystem::path fs_path(filename);
  auto absolute = std::filesystem::absolute(fs_path, ec);
  std::wstring path = ec ? utf8_to_wstring(filename) : absolute.wstring();

  wchar_t root[MAX_PATH];
  if (!GetVolumePathNameW(path.c_str(), root, MAX_PATH)) {
    return std::unexpected("cannot read file system");
  }

  ULARGE_INTEGER free_available{}, total_bytes{}, total_free{};
  if (!GetDiskFreeSpaceExW(root, &free_available, &total_bytes, &total_free)) {
    return std::unexpected("cannot read file system");
  }

  DWORD sectors_per_cluster = 0;
  DWORD bytes_per_sector = 0;
  DWORD free_clusters = 0;
  DWORD total_clusters = 0;
  uint32_t block_size = 4096;
  if (GetDiskFreeSpaceW(root, &sectors_per_cluster, &bytes_per_sector,
                        &free_clusters, &total_clusters)) {
    uint64_t computed =
        static_cast<uint64_t>(sectors_per_cluster) * bytes_per_sector;
    if (computed != 0 && computed <= std::numeric_limits<uint32_t>::max()) {
      block_size = static_cast<uint32_t>(computed);
    }
  }

  wchar_t fs_name[MAX_PATH] = L"";
  DWORD serial = 0;
  DWORD max_component = 0;
  DWORD flags = 0;
  GetVolumeInformationW(root, nullptr, 0, &serial, &max_component, &flags,
                        fs_name, MAX_PATH);

  return FileSystemStatData{
      .free_available = static_cast<uint64_t>(free_available.QuadPart),
      .total_bytes = static_cast<uint64_t>(total_bytes.QuadPart),
      .total_free = static_cast<uint64_t>(total_free.QuadPart),
      .block_size = block_size,
      .max_component = max_component,
      .serial = serial,
      .fs_name = wstring_to_utf8(fs_name)};
}

auto render_format(std::string_view format, const std::string& filename,
                   const FileStatData& stat) -> std::string {
  std::string out;
  out.reserve(format.size() + filename.size());

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%' || i + 1 >= format.size()) {
      out.push_back(format[i]);
      continue;
    }

    char code = format[++i];
    switch (code) {
      case '%':
        out.push_back('%');
        break;
      case 'n':
        out += filename;
        break;
      case 'N':
        out += "'";
        out += filename;
        out += "'";
        break;
      case 's':
        out += std::to_string(stat.size);
        break;
      case 'b':
        out += std::to_string((stat.size + 511) / 512);
        break;
      case 'B':
        out += "512";
        break;
      case 'F':
        out += file_type_name(stat.attrs.dwFileAttributes);
        break;
      case 'A':
        out += format_permissions(stat.attrs.dwFileAttributes);
        break;
      case 'a':
        out += format_mode_octal(stat.attrs.dwFileAttributes);
        break;
      case 'h':
        out += std::to_string(stat.hard_links);
        break;
      case 'i':
        out += std::to_string(stat.file_index);
        break;
      case 'd':
        out += std::to_string(stat.volume_serial);
        break;
      case 'D': {
        char buf[32];
        snprintf(buf, sizeof(buf), "%llx",
                 static_cast<unsigned long long>(stat.volume_serial));
        out += buf;
        break;
      }
      case 'o':
        out += std::to_string(stat.io_block_size);
        break;
      case 'u':
      case 'g':
        out += "0";
        break;
      case 'U':
      case 'G':
        out += "UNKNOWN";
        break;
      case 'x':
        out += format_timestamp(stat.attrs.ftLastAccessTime);
        break;
      case 'y':
        out += format_timestamp(stat.attrs.ftLastWriteTime);
        break;
      case 'w':
        out += format_timestamp(stat.attrs.ftCreationTime);
        break;
      case 'z':
        out += format_timestamp(stat.attrs.ftLastWriteTime);
        break;
      case 'X':
        out += std::to_string(
            filetime_to_unix_seconds(stat.attrs.ftLastAccessTime));
        break;
      case 'Y':
        out += std::to_string(
            filetime_to_unix_seconds(stat.attrs.ftLastWriteTime));
        break;
      case 'Z':
        out += std::to_string(
            filetime_to_unix_seconds(stat.attrs.ftLastWriteTime));
        break;
      case 'W':
        out +=
            std::to_string(filetime_to_unix_seconds(stat.attrs.ftCreationTime));
        break;
      default:
        out.push_back('%');
        out.push_back(code);
        break;
    }
  }

  return out;
}

auto render_file_system_format(std::string_view format,
                               const std::string& filename,
                               const FileSystemStatData& stat) -> std::string {
  std::string out;
  out.reserve(format.size() + filename.size());

  auto blocks = [&](uint64_t bytes) -> uint64_t {
    return stat.block_size == 0 ? 0 : bytes / stat.block_size;
  };

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%' || i + 1 >= format.size()) {
      out.push_back(format[i]);
      continue;
    }

    char code = format[++i];
    switch (code) {
      case '%':
        out.push_back('%');
        break;
      case 'a':
        out += std::to_string(blocks(stat.free_available));
        break;
      case 'b':
        out += std::to_string(blocks(stat.total_bytes));
        break;
      case 'c':
      case 'd':
        out += "0";
        break;
      case 'f':
        out += std::to_string(blocks(stat.total_free));
        break;
      case 'i': {
        char buf[16];
        snprintf(buf, sizeof(buf), "%08lx",
                 static_cast<unsigned long>(stat.serial));
        out += buf;
        break;
      }
      case 'l':
        out += std::to_string(stat.max_component);
        break;
      case 'n':
        out += filename;
        break;
      case 's':
      case 'S':
        out += std::to_string(stat.block_size);
        break;
      case 't': {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lx",
                 static_cast<unsigned long>(stat.serial));
        out += buf;
        break;
      }
      case 'T':
        out += stat.fs_name;
        break;
      default:
        out.push_back('%');
        out.push_back(code);
        break;
    }
  }

  return out;
}

auto print_file_system_stat(const std::string& filename) -> int {
  auto stat_result = load_file_system_stat(filename);
  if (!stat_result) {
    safePrint("stat: cannot read file system for '");
    safePrint(filename);
    safePrintLn("'");
    return 1;
  }
  const auto& stat = *stat_result;

  safePrint("  File: \"");
  safePrint(filename);
  safePrintLn("\"");
  char serial_buf[16];
  snprintf(serial_buf, sizeof(serial_buf), "%08lx",
           static_cast<unsigned long>(stat.serial));
  safePrint("    ID: ");
  safePrint(serial_buf);
  safePrint("\tNamelen: ");
  safePrint(stat.max_component);
  safePrint("\tType: ");
  safePrintLn(stat.fs_name);
  safePrint("Block size: ");
  safePrint(stat.block_size);
  safePrint("\tTotal bytes: ");
  safePrint(stat.total_bytes);
  safePrint("\tFree bytes: ");
  safePrintLn(stat.total_free);
  return 0;
}

auto print_stat(const std::string& filename, const Config& cfg) -> int {
  std::error_code ec;
  std::filesystem::path p(filename);

  if (!std::filesystem::exists(p, ec)) {
    safePrint("stat: cannot stat '");
    safePrint(filename);
    safePrint("': No such file or directory\n");
    return 1;
  }

  if (cfg.file_system) {
    if (cfg.format.empty()) {
      return print_file_system_stat(filename);
    }

    auto stat_result = load_file_system_stat(filename);
    if (!stat_result) {
      safePrint("stat: cannot read file system for '");
      safePrint(filename);
      safePrintLn("'");
      return 1;
    }

    auto format =
        cfg.printf_format ? expand_backslash_escapes(cfg.format) : cfg.format;
    safePrint(render_file_system_format(format, filename, *stat_result));
    if (!cfg.printf_format) {
      safePrint("\n");
    }
    return 0;
  }

  auto stat_result = load_file_stat(p);
  if (!stat_result) {
    safePrint("stat: cannot stat '");
    safePrint(filename);
    safePrint("': Access denied\n");
    return 1;
  }
  const auto& stat = *stat_result;

  if (cfg.terse) {
    // Terse format
    safePrint(filename);
    safePrint(" ");
    safePrint(stat.size);
    safePrint(" ");
    safePrint(format_timestamp(stat.attrs.ftLastWriteTime));
    safePrint("\n");
  } else if (!cfg.format.empty()) {
    auto format =
        cfg.printf_format ? expand_backslash_escapes(cfg.format) : cfg.format;
    safePrint(render_format(format, filename, stat));
    if (!cfg.printf_format) {
      safePrint("\n");
    }
  } else {
    // Default format
    safePrint("  File: ");
    safePrint(filename);
    safePrint("\n");
    safePrint("  Size: ");
    safePrint(format_size(stat.size));
    safePrint("\t");
    safePrint("Blocks: ");
    safePrint((stat.size + 511) / 512);
    safePrint("\t");
    safePrint("IO Block: ");
    safePrint(stat.io_block_size);
    safePrint("\t");

    safePrint(file_type_name(stat.attrs.dwFileAttributes));
    safePrint("\n");

    safePrint("Access: (");
    safePrint(format_permissions(stat.attrs.dwFileAttributes));
    safePrint(")\n");

    safePrint("Modify: ");
    safePrint(format_timestamp(stat.attrs.ftLastWriteTime));
    safePrint("\n");

    safePrint("Birth: ");
    safePrint(format_timestamp(stat.attrs.ftCreationTime));
    safePrint("\n");
  }

  return 0;
}

auto run(const Config& cfg) -> int {
  int exit_code = 0;

  for (const auto& file : cfg.files) {
    if (print_stat(file, cfg) != 0) {
      exit_code = 1;
    }
  }

  return exit_code;
}

}  // namespace stat_pipeline

REGISTER_COMMAND(stat, "stat", "stat [OPTION]... FILE...",
                 "Display file or file system status.",
                 "  stat file.txt\n"
                 "  stat -t file.txt\n"
                 "  stat -c %s file.txt",
                 "ls(1), find(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 STAT_OPTIONS) {
  using namespace stat_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"stat");
    return 1;
  }

  return run(*cfg_result);
}
