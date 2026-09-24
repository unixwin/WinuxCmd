// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for stat.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include <winioctl.h>

#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK (0xA000000CL)
#endif

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT (0xA0000003L)
#endif

namespace stat_win32_compat {
#pragma pack(push, 1)
struct ReparseDataBuffer {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
};
#pragma pack(pop)
}  // namespace stat_win32_compat

// [GNU] -L, --dereference: follow symbolic links
// [GNU] -f, --file-system: display file system status
// [GNU] -c, --format: use specified FORMAT
// [GNU] -t, --terse: print information in terse form
// [GNU] --printf: like --format with backslash escapes
// [DIFFERS] --cached: GNU cache controls are unavailable on Windows
auto constexpr STAT_OPTIONS = std::array{
    OPTION("-L", "--dereference", "follow symbolic links", BOOL_TYPE),
    OPTION("-f", "--file-system",
           "display file system status instead of file status", BOOL_TYPE),
    OPTION("-c", "--format", "use the specified FORMAT instead of the default",
           STRING_TYPE),
    OPTION("-t", "--terse", "print the information in terse form", BOOL_TYPE),
    OPTION("", "--printf", "like --format, but interpret backslash escapes",
           STRING_TYPE),
    // [GNU] --cached is a required-argument caching hint (never/always/
    // default). It is accepted as a no-op on Windows.
    OPTION("", "--cached",
           "use cached attribute data WHEN (hint only; always fresh on "
           "Windows)",
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
  // [GNU] ctime: Windows has no stat() "status change" time. NTFS
  // ChangeTime (FILE_BASIC_INFO) is the closest analog; filesystems that
  // do not maintain it report zero and fall back to CreationTime.
  FILETIME change_time{};
  // st_blocks source: on-disk allocation in bytes (FILE_STANDARD_INFO).
  uint64_t allocated_size = 0;
  uint64_t size = 0;
  uint64_t file_index = 0;
  uint64_t volume_serial = 0;
  uint32_t hard_links = 1;
  uint32_t io_block_size = 4096;
  // [GNU] A Windows character device (NUL, CONIN$, CONOUT$) reports
  // FILE_ATTRIBUTE_ARCHIVE and is indistinguishable from an ordinary file by
  // attributes alone, but GNU reports /dev/null as a character special file.
  bool character_device = false;
  // [GNU] `stat -` on a pipe reports "fifo" (st_mode S_IFIFO).
  bool fifo = false;
  std::string owner_name;
  std::string owner_id;
  std::string group_name;
  std::string group_id;
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
  // [GNU] --cached=WHEN accepts 'never', 'always' and 'default' as a
  // read-caching hint (#1069). Windows always queries fresh attributes, so
  // a valid WHEN is a no-op; anything else is a GNU-style usage error.
  if (ctx.has("--cached")) {
    const auto when = ctx.get<std::string>("--cached", "");
    if (when != "never" && when != "always" && when != "default") {
      return std::unexpected("invalid argument '" + when +
                             "' for '--cached'\n"
                             "Valid arguments are:\n"
                             "  - 'default'\n"
                             "  - 'never'\n"
                             "  - 'always'");
    }
  }
  cfg.dereference =
      ctx.get<bool>("--dereference", false) || ctx.get<bool>("-L", false);
  cfg.file_system =
      ctx.get<bool>("--file-system", false) || ctx.get<bool>("-f", false);

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= STAT_OPTIONS.size()) {
      continue;
    }

    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.long_name == "--terse" || meta.short_name == "-t") {
      cfg.terse = true;
      cfg.format.clear();
      cfg.printf_format = false;
      continue;
    }

    if (meta.long_name == "--printf") {
      if (const auto* value = std::get_if<std::string>(&occurrence.value)) {
        cfg.terse = false;
        cfg.format = *value;
        cfg.printf_format = true;
      }
      continue;
    }

    if (meta.long_name == "--format" || meta.short_name == "-c") {
      if (const auto* value = std::get_if<std::string>(&occurrence.value)) {
        cfg.terse = false;
        cfg.format = *value;
        cfg.printf_format = false;
      }
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

  if (cfg.files.empty()) {
    return std::unexpected("missing operand");
  }

  return cfg;
}

auto filetime_to_100ns(FILETIME ft) -> uint64_t {
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

// [GNU] Human-readable timestamps are
// "YYYY-MM-DD HH:MM:SS.NNNNNNNNN +ZZZZ" (#1022). FILETIME carries 100ns
// precision, so the nanosecond field always ends in 0. The numeric offset
// is the local-zone offset in effect at that instant (including DST),
// derived from the same conversion used for the wall-clock fields.
auto format_timestamp(FILETIME ft) -> std::string {
  FILETIME local = ft;
  FileTimeToLocalFileTime(&ft, &local);
  SYSTEMTIME st{};
  FileTimeToSystemTime(&local, &st);

  const int64_t offset_100ns = static_cast<int64_t>(filetime_to_100ns(local)) -
                               static_cast<int64_t>(filetime_to_100ns(ft));
  int64_t offset_minutes = offset_100ns / 600000000LL;
  char sign = '+';
  if (offset_minutes < 0) {
    sign = '-';
    offset_minutes = -offset_minutes;
  }
  const uint64_t nanos = (filetime_to_100ns(ft) % 10000000ULL) * 100ULL;

  char buf[80];
  snprintf(buf, sizeof(buf),
           "%04d-%02d-%02d %02d:%02d:%02d.%09llu %c%02lld%02lld",
           static_cast<int>(st.wYear), static_cast<int>(st.wMonth),
           static_cast<int>(st.wDay), static_cast<int>(st.wHour),
           static_cast<int>(st.wMinute), static_cast<int>(st.wSecond),
           static_cast<unsigned long long>(nanos), sign,
           static_cast<long long>(offset_minutes / 60),
           static_cast<long long>(offset_minutes % 60));
  return std::string(buf);
}

auto filetime_to_unix_seconds(FILETIME ft) -> int64_t {
  const uint64_t quad = filetime_to_100ns(ft);
  constexpr uint64_t kWindowsToUnixEpoch100ns = 116444736000000000ULL;
  if (quad < kWindowsToUnixEpoch100ns) {
    return -static_cast<int64_t>((kWindowsToUnixEpoch100ns - quad) /
                                 10000000ULL);
  }
  return static_cast<int64_t>((quad - kWindowsToUnixEpoch100ns) / 10000000ULL);
}

auto format_permissions(DWORD attrs, bool fifo = false) -> std::string {
  // [GNU] lstat() reports symlinks as lrwxrwxrwx regardless of the target.
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    return "lrwxrwxrwx";
  }
  if (fifo) {
    return (attrs & FILE_ATTRIBUTE_READONLY) ? "pr--r--r--" : "prw-r--r--";
  }
  const bool directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const bool writable = (attrs & FILE_ATTRIBUTE_READONLY) == 0;
  std::string perm = directory ? "d" : "-";
  perm += writable ? (directory ? "rwxr-xr-x" : "rw-r--r--")
                   : (directory ? "r-xr-xr-x" : "r--r--r--");
  return perm;
}

auto file_type_name(const FileStatData& stat) -> std::string {
  if (stat.fifo) return "fifo";
  if (stat.character_device) return "character special file";
  const DWORD attrs = stat.attrs.dwFileAttributes;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) return "directory";
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return "symbolic link";
  return "regular file";
}

auto format_mode_octal(DWORD attrs) -> std::string {
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    return "777";
  }
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    return (attrs & FILE_ATTRIBUTE_READONLY) ? "555" : "755";
  }
  return (attrs & FILE_ATTRIBUTE_READONLY) ? "444" : "644";
}

// [GNU] %f is the raw st_mode in hex (type bits | permission bits), e.g.
// 81a4 for a 0644 regular file, 41ed for a directory, a1ff for a symlink.
auto st_mode_bits(const FileStatData& stat) -> uint32_t {
  // [GNU] S_IFIFO 0x1000, S_IFCHR 0x2000 — `stat -c %f -` on a pipe
  // prints 11b4, on /dev/null-style devices 21b6.
  if (stat.fifo) {
    return 0x1000u |
           ((stat.attrs.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? 0444u
                                                                    : 0644u);
  }
  if (stat.character_device) return 0x2000u | 0666u;
  const DWORD attrs = stat.attrs.dwFileAttributes;
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return 0xA000u | 0777u;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    return 0x4000u | ((attrs & FILE_ATTRIBUTE_READONLY) ? 0555u : 0755u);
  }
  return 0x8000u | ((attrs & FILE_ATTRIBUTE_READONLY) ? 0444u : 0644u);
}

// [GNU] The default primary group on Windows resolves to a well-known
// group literally named "None" which has no POSIX equivalent; treat it as
// unresolvable so it prints like a GNU nameless gid (#1024).
auto is_none_group_name(std::string_view name) -> bool {
  return name.size() == 4 &&
         std::tolower(static_cast<unsigned char>(name[0])) == 'n' &&
         std::tolower(static_cast<unsigned char>(name[1])) == 'o' &&
         std::tolower(static_cast<unsigned char>(name[2])) == 'n' &&
         std::tolower(static_cast<unsigned char>(name[3])) == 'e';
}

// [GNU] stat prints "UNKNOWN" where the owner/group name cannot be
// resolved (e.g. `Gid: ( 1234/ UNKNOWN)`).
auto display_owner_name(std::string_view name) -> std::string {
  return name.empty() ? "UNKNOWN" : std::string(name);
}

auto display_group_name(std::string_view name) -> std::string {
  return (name.empty() || is_none_group_name(name)) ? "UNKNOWN"
                                                    : std::string(name);
}

// [GNU] %b is the number of allocated 512B blocks (st_blocks). On Windows
// this comes from FileStandardInfo AllocationSize; resident files report 0
// allocated bytes, matching GNU on drvfs/9P mounts.
auto allocated_block_count(const FileStatData& stat) -> uint64_t {
  if (stat.attrs.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return 0;
  return (stat.allocated_size + 511) / 512;
}

auto pad_right(std::string s, size_t width) -> std::string {
  if (s.size() < width) s.append(width - s.size(), ' ');
  return s;
}

auto pad_left(std::string s, size_t width) -> std::string {
  if (s.size() < width) s.insert(0, width - s.size(), ' ');
  return s;
}

auto io_block_size_for(const std::filesystem::path& p) -> uint32_t {
  auto dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path(".");
  std::error_code ec;
  auto absolute = std::filesystem::absolute(dir, ec);
  auto wdir = ec ? dir.wstring() : absolute.wstring();

  auto operand = native_path::make_api_path_operand_w(wdir);
  wchar_t root[32768];
  if (!GetVolumePathNameW(operand.extended.c_str(), root,
                          static_cast<DWORD>(std::size(root))))
    return 4096;

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

// [GNU] Without -L, stat uses lstat(): a symlink operand reports the link's
// own metadata, so a dangling link still stats successfully.
auto operand_is_symlink_w(const std::wstring& path) -> bool {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(
      std::filesystem::path(native_path::to_extended_path(path)), ec);
  return !ec && std::filesystem::is_symlink(status);
}

auto load_file_stat(const std::filesystem::path& p, bool lstat_link = false)
    -> cp::Result<FileStatData> {
  FileStatData stat;
  auto operand = native_path::make_api_path_operand_w(p.wstring());
  // GetFileAttributesExW rejects DOS device names outright (see
  // native_path::file_attribute_data_w), so the shared device-aware probe is
  // what lets `stat /dev/null` report a character device instead of failing.
  if (!native_path::file_attribute_data_w(operand.extended, stat.attrs)) {
    if (!lstat_link) {
      return std::unexpected("Access denied");
    }
    // lstat() path: GetFileAttributesExW resolves (and fails on) dangling
    // links; FindFirstFileW returns the reparse point's own metadata.
    WIN32_FIND_DATAW link_data{};
    HANDLE find = FindFirstFileW(operand.extended.c_str(), &link_data);
    if (find == INVALID_HANDLE_VALUE) {
      return std::unexpected("Access denied");
    }
    FindClose(find);
    stat.attrs.dwFileAttributes = link_data.dwFileAttributes;
    stat.attrs.ftCreationTime = link_data.ftCreationTime;
    stat.attrs.ftLastAccessTime = link_data.ftLastAccessTime;
    stat.attrs.ftLastWriteTime = link_data.ftLastWriteTime;
    stat.attrs.nFileSizeHigh = link_data.nFileSizeHigh;
    stat.attrs.nFileSizeLow = link_data.nFileSizeLow;
  }

  auto accounts = win32_file_accounts(operand.extended);
  stat.owner_name = accounts.owner.name;
  stat.owner_id = accounts.owner.id;
  stat.group_name = accounts.group.name;
  stat.group_id = accounts.group.id;

  stat.size = stat.attrs.nFileSizeLow +
              (static_cast<uint64_t>(stat.attrs.nFileSizeHigh) << 32);
  if (lstat_link) {
    // [GNU] st_size of a symlink is the length of its target name.
    std::error_code ec;
    const auto target = std::filesystem::read_symlink(p, ec);
    if (!ec) {
      stat.size = target.wstring().size();
    }
  }
  stat.io_block_size = io_block_size_for(p);
  stat.character_device = native_path::is_character_device_w(p.wstring());
  // WinuxCmd fifo markers (#1038) report like GNU lstat() on a FIFO:
  // type "fifo", S_IFIFO mode bits, and st_size 0.
  if (!stat.character_device &&
      (stat.attrs.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
      native_path::is_winux_fifo_w(p.wstring())) {
    stat.fifo = true;
    stat.size = 0;
  }

  DWORD open_flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (lstat_link) {
    // Stat the reparse point itself so inode/change time describe the
    // link, not its target.
    open_flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }
  HANDLE h = CreateFileW(operand.extended.c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, open_flags, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(h, &info)) {
      stat.file_index = info.nFileIndexLow +
                        (static_cast<uint64_t>(info.nFileIndexHigh) << 32);
      stat.volume_serial = info.dwVolumeSerialNumber;
      stat.hard_links = std::max<DWORD>(info.nNumberOfLinks, 1);
    }

    FILE_BASIC_INFO basic{};
    if (GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic))) {
      stat.change_time.dwLowDateTime = basic.ChangeTime.LowPart;
      stat.change_time.dwHighDateTime = basic.ChangeTime.HighPart;
    }
    FILE_STANDARD_INFO standard{};
    if (GetFileInformationByHandleEx(h, FileStandardInfo, &standard,
                                     sizeof(standard)) &&
        standard.AllocationSize.QuadPart > 0) {
      stat.allocated_size =
          static_cast<uint64_t>(standard.AllocationSize.QuadPart);
    }
    CloseHandle(h);
  }

  if (filetime_to_100ns(stat.change_time) == 0) {
    // Filesystem does not maintain ChangeTime (e.g. FAT32): the closest
    // remaining analog is the file creation time.
    stat.change_time = stat.attrs.ftCreationTime;
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

  auto path_operand = native_path::make_api_path_operand_w(path);
  std::array<wchar_t, 32768> root{};
  if (!GetVolumePathNameW(path_operand.extended.c_str(), root.data(),
                          static_cast<DWORD>(root.size()))) {
    return std::unexpected("cannot read file system");
  }

  ULARGE_INTEGER free_available{}, total_bytes{}, total_free{};
  if (!GetDiskFreeSpaceExW(root.data(), &free_available, &total_bytes,
                           &total_free)) {
    return std::unexpected("cannot read file system");
  }

  DWORD sectors_per_cluster = 0;
  DWORD bytes_per_sector = 0;
  DWORD free_clusters = 0;
  DWORD total_clusters = 0;
  uint32_t block_size = 4096;
  if (GetDiskFreeSpaceW(root.data(), &sectors_per_cluster, &bytes_per_sector,
                        &free_clusters, &total_clusters)) {
    uint64_t computed =
        static_cast<uint64_t>(sectors_per_cluster) * bytes_per_sector;
    if (computed != 0 && computed <= std::numeric_limits<uint32_t>::max()) {
      block_size = static_cast<uint32_t>(computed);
    }
  }

  std::array<wchar_t, 32768> fs_name{};
  DWORD serial = 0;
  DWORD max_component = 0;
  DWORD flags = 0;
  GetVolumeInformationW(root.data(), nullptr, 0, &serial, &max_component,
                        &flags, fs_name.data(),
                        static_cast<DWORD>(fs_name.size()));

  return FileSystemStatData{
      .free_available = static_cast<uint64_t>(free_available.QuadPart),
      .total_bytes = static_cast<uint64_t>(total_bytes.QuadPart),
      .total_free = static_cast<uint64_t>(total_free.QuadPart),
      .block_size = block_size,
      .max_component = max_component,
      .serial = serial,
      .fs_name = wstring_to_utf8(fs_name.data())};
}

// [GNU] %N dereferences symlinks in the output: 'link' -> 'target'
// (uutils #8789).
auto read_link_target_utf8(const std::string& filename) -> std::string {
  const std::wstring wname = utf8_to_wstring(filename);
  const DWORD link_attrs = native_path::attributes_w(wname);
  if (link_attrs == INVALID_FILE_ATTRIBUTES ||
      (link_attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
    return {};
  }
  HANDLE handle = CreateFileW(
      native_path::to_extended_path(wname).c_str(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};

  std::string result;
  std::array<std::byte, 16 * 1024> reparse_buffer{};
  DWORD returned = 0;
  if (DeviceIoControl(
          handle, FSCTL_GET_REPARSE_POINT, nullptr, 0, reparse_buffer.data(),
          static_cast<DWORD>(reparse_buffer.size()), &returned, nullptr)) {
    auto* reparse = reinterpret_cast<stat_win32_compat::ReparseDataBuffer*>(
        reparse_buffer.data());
    std::wstring target;
    if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
      const auto& sl = reparse->SymbolicLinkReparseBuffer;
      target.assign(sl.PathBuffer + sl.PrintNameOffset / sizeof(wchar_t),
                    sl.PrintNameLength / sizeof(wchar_t));
    } else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
      const auto& mp = reparse->MountPointReparseBuffer;
      target.assign(mp.PathBuffer + mp.PrintNameOffset / sizeof(wchar_t),
                    mp.PrintNameLength / sizeof(wchar_t));
    }
    result = wstring_to_utf8(target);
  }
  CloseHandle(handle);
  return result;
}

// [GNU] A directive may be preceded by printf-style modifiers:
// '-' left-justify, '0' zero-pad numbers, '#' alternate form (0x for hex,
// leading 0 for octal %a), a decimal field width and a '.precision'
// (min digit count for numbers, max length for strings).
struct DirectiveSpec {
  bool left = false;
  bool zero = false;
  bool alter = false;
  size_t width = 0;
  int precision = -1;
  bool any = false;  // at least one modifier present
};

// `i` points at the directive char position following '%' on entry; on
// return it is the index of the final directive letter.
auto parse_directive_spec(std::string_view format, size_t& i) -> DirectiveSpec {
  DirectiveSpec spec;
  size_t j = i;
  while (j < format.size()) {
    switch (format[j]) {
      case '-':
        spec.left = true;
        break;
      case '0':
        spec.zero = true;
        break;
      case '#':
        spec.alter = true;
        break;
      case '+':
      case ' ':
        break;  // accepted, no effect for our fields
      default:
        goto flags_done;
    }
    spec.any = true;
    ++j;
  }
flags_done:
  while (j < format.size() && format[j] >= '0' && format[j] <= '9') {
    spec.width = spec.width * 10 + static_cast<size_t>(format[j++] - '0');
    spec.any = true;
  }
  if (j < format.size() && format[j] == '.') {
    ++j;
    spec.precision = 0;
    spec.any = true;
    while (j < format.size() && format[j] >= '0' && format[j] <= '9') {
      spec.precision = spec.precision * 10 + (format[j++] - '0');
      spec.any = true;
    }
  }
  i = j;
  return spec;
}

auto apply_directive_spec(std::string value, const DirectiveSpec& spec,
                          char code, bool numeric) -> std::string {
  if (numeric) {
    if (spec.precision >= 0 &&
        value.size() < static_cast<size_t>(spec.precision)) {
      value.insert(0, static_cast<size_t>(spec.precision) - value.size(), '0');
    }
    if (spec.alter) {
      if ((code == 'f' || code == 'D' || code == 't' || code == 'T') &&
          value != "0") {
        value.insert(0, "0x");
      } else if (code == 'a' && (value.empty() || value.front() != '0')) {
        value.insert(0, "0");
      }
    }
  } else if (spec.precision >= 0 &&
             value.size() > static_cast<size_t>(spec.precision)) {
    value.resize(static_cast<size_t>(spec.precision));
  }
  if (value.size() < spec.width) {
    const char pad = (spec.zero && numeric) ? '0' : ' ';
    if (spec.left) {
      value.append(spec.width - value.size(), pad);
    } else {
      value.insert(0, spec.width - value.size(), pad);
    }
  }
  return value;
}

// Returns the rendered text; `ctx_failed` is set when %C was requested
// (GNU reports a hard failure when the security context is unavailable).
auto render_format(std::string_view format, const std::string& filename,
                   const FileStatData& stat, bool& ctx_failed) -> std::string {
  std::string out;
  out.reserve(format.size() + filename.size());

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%' || i + 1 >= format.size()) {
      out.push_back(format[i]);
      continue;
    }

    size_t pos = i + 1;
    const DirectiveSpec spec = parse_directive_spec(format, pos);
    if (pos >= format.size()) {
      out.push_back('%');
      break;
    }
    const char code = format[pos];
    i = pos;

    // Whether the directive produces a number for '0'/precision padding.
    const bool numeric = std::string_view("abdfghiorstTuDXYZW").find(code) !=
                         std::string_view::npos;

    std::string value;
    bool handled = true;
    switch (code) {
      case '%':
        value = "%";
        break;
      case 'n':
        value = filename;
        break;
      case 'N': {
        const std::string target = read_link_target_utf8(filename);
        if (spec.any) {
          // [GNU] modifiers suppress quoting; name and target are each
          // padded to the field width.
          value = apply_directive_spec(filename, spec, code, false);
          if (!target.empty()) {
            value += " -> ";
            value += apply_directive_spec(target, spec, code, false);
          }
        } else {
          value = "'" + filename + "'";
          if (!target.empty()) value += " -> '" + target + "'";
        }
        break;
      }
      case 's':
        value = std::to_string(stat.size);
        break;
      case 'b':
        value = std::to_string(allocated_block_count(stat));
        break;
      case 'B':
        value = "512";
        break;
      case 'F':
        value = file_type_name(stat);
        break;
      case 'A':
        value = format_permissions(stat.attrs.dwFileAttributes, stat.fifo);
        break;
      case 'a':
        value = format_mode_octal(stat.attrs.dwFileAttributes);
        break;
      case 'f': {
        // [GNU] %f: raw mode in hex.
        char buf[32];
        snprintf(buf, sizeof(buf), "%x",
                 static_cast<unsigned int>(st_mode_bits(stat)));
        value = buf;
        break;
      }
      case 'c':
        // [GNU] %c: SELinux security context string; none on Windows.
        value = "?";
        break;
      case 'C':
        // [GNU] %C: SELinux security context; GNU reports a failure when
        // it cannot be read, so flag it and let the caller set rc=1.
        value = "?";
        ctx_failed = true;
        break;
      case 'r':
        // [GNU] %r: device number (st_rdev) in decimal. Windows has no
        // block/character special files, so it is always 0.
        value = "0";
        break;
      case 't':
      case 'T':
        // [GNU] %t/%T: major/minor device type in hex for special files;
        // 0 for everything else (Windows has no special files).
        value = "0";
        break;
      case 'h':
        value = std::to_string(stat.hard_links);
        break;
      case 'i':
        value = std::to_string(stat.file_index);
        break;
      case 'd':
        value = std::to_string(stat.volume_serial);
        break;
      case 'D': {
        char buf[32];
        snprintf(buf, sizeof(buf), "%llx",
                 static_cast<unsigned long long>(stat.volume_serial));
        value = buf;
        break;
      }
      case 'o':
        value = std::to_string(stat.io_block_size);
        break;
      case 'u':
        value = stat.owner_id.empty() ? "0" : stat.owner_id;
        break;
      case 'g':
        value = stat.group_id.empty() ? "0" : stat.group_id;
        break;
      case 'U':
        value = display_owner_name(stat.owner_name);
        break;
      case 'G':
        value = display_group_name(stat.group_name);
        break;
      case 'x':
        value = format_timestamp(stat.attrs.ftLastAccessTime);
        break;
      case 'y':
        value = format_timestamp(stat.attrs.ftLastWriteTime);
        break;
      case 'w':
        value = format_timestamp(stat.attrs.ftCreationTime);
        break;
      case 'z':
        value = format_timestamp(stat.change_time);
        break;
      case 'X':
        value = std::to_string(
            filetime_to_unix_seconds(stat.attrs.ftLastAccessTime));
        break;
      case 'Y':
        value = std::to_string(
            filetime_to_unix_seconds(stat.attrs.ftLastWriteTime));
        break;
      case 'Z':
        value = std::to_string(filetime_to_unix_seconds(stat.change_time));
        break;
      case 'W':
        value =
            std::to_string(filetime_to_unix_seconds(stat.attrs.ftCreationTime));
        break;
      case 'm': {
        // Mount point
        std::array<wchar_t, 32768> volume{};
        auto operand = native_path::make_api_path_operand(filename);
        if (GetVolumePathNameW(operand.extended.c_str(), volume.data(),
                               static_cast<DWORD>(volume.size()))) {
          value = wstring_to_utf8(volume.data());
        } else {
          value = "/";
        }
        break;
      }
      default:
        // [GNU] unrecognized format directives print '?'.
        handled = false;
        break;
    }

    if (handled) {
      out += (code == 'N')
                 ? value
                 : apply_directive_spec(std::move(value), spec, code, numeric);
    } else {
      out += apply_directive_spec("?", spec, code, false);
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
        snprintf(buf, sizeof(buf), "%lx",
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
      case 't':
        // [GNU] %t in file-system mode is the fs magic number in hex
        // (e.g. ef53); Windows exposes no such identifier.
        out += '0';
        break;
      case 'T':
        out += stat.fs_name;
        break;
      default:
        // [GNU] unrecognized format directives print '?'.
        out.push_back('?');
        break;
    }
  }

  return out;
}

// [GNU] -f default layout (#1023):
//     File: "<n>"
//       ID: %-8i Namelen: %-7l Type: %T
//   Block size: %-10s Fundamental block size: %S
//   Blocks: Total: %-10b Free: %-10f Available: %a
//   Inodes: Total: %-10c Free: %d
auto print_file_system_stat(const std::string& filename,
                            const FileSystemStatData& stat) -> int {
  auto blocks = [&](uint64_t bytes) -> uint64_t {
    return stat.block_size == 0 ? 0 : bytes / stat.block_size;
  };

  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%lx",
           static_cast<unsigned long>(stat.serial));

  safePrint("  File: \"");
  safePrint(filename);
  safePrintLn("\"");
  safePrint("    ID: ");
  safePrint(pad_right(id_buf, 8));
  safePrint(" Namelen: ");
  safePrint(pad_right(std::to_string(stat.max_component), 7));
  safePrint(" Type: ");
  safePrintLn(stat.fs_name);
  safePrint("Block size: ");
  safePrint(pad_right(std::to_string(stat.block_size), 10));
  safePrint(" Fundamental block size: ");
  safePrintLn(std::to_string(stat.block_size));
  safePrint("Blocks: Total: ");
  safePrint(pad_right(std::to_string(blocks(stat.total_bytes)), 10));
  safePrint(" Free: ");
  safePrint(pad_right(std::to_string(blocks(stat.total_free)), 10));
  safePrint(" Available: ");
  safePrintLn(std::to_string(blocks(stat.free_available)));
  // [GNU] POSIX inode counts have no Windows equivalent; report 0.
  safePrint("Inodes: Total: ");
  safePrint(pad_right("0", 10));
  safePrint(" Free: ");
  safePrintLn("0");
  return 0;
}

auto emit_stat_output(const std::string& filename, const FileStatData& stat,
                      const std::string& link_target, const Config& cfg) -> int;

// [GNU] `stat -` stats open file descriptor 0: a pipe reports "fifo"
// (S_IFIFO), a console "character special file", a redirected file a
// regular file.  Closed stdin is "cannot stat standard input: Bad file
// descriptor".
auto load_stdin_stat() -> cp::Result<FileStatData> {
  if (file_io::stdin_is_bad()) {
    return std::unexpected("cannot stat standard input: Bad file descriptor");
  }
  FileStatData stat;
  stat.io_block_size = 4096;
  const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  const DWORD ftype = (h != nullptr && h != INVALID_HANDLE_VALUE)
                          ? GetFileType(h)
                          : FILE_TYPE_UNKNOWN;
  stat.fifo = (ftype == FILE_TYPE_PIPE);
  stat.character_device = (ftype == FILE_TYPE_CHAR);
  if (ftype == FILE_TYPE_DISK) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(h, &info)) {
      stat.size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
      stat.allocated_size = stat.size;
      stat.file_index =
          (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
      stat.hard_links = info.nNumberOfLinks;
      stat.volume_serial = info.dwVolumeSerialNumber;
      stat.attrs.dwFileAttributes = info.dwFileAttributes;
      stat.attrs.ftCreationTime = info.ftCreationTime;
      stat.attrs.ftLastAccessTime = info.ftLastAccessTime;
      stat.attrs.ftLastWriteTime = info.ftLastWriteTime;
      stat.change_time = info.ftLastWriteTime;
    }
  }
  return stat;
}

auto print_stat(const std::string& filename, const Config& cfg) -> int {
  // [GNU] "-" names standard input, not a file literally called "-".
  if (filename == "-") {
    if (cfg.file_system) {
      safeErrorPrintLn("stat: cannot read file system information for '-'");
      return 1;
    }
    auto stdin_result = load_stdin_stat();
    if (!stdin_result) {
      safeErrorPrint("stat: ");
      safeErrorPrintLn(stdin_result.error());
      return 1;
    }
    return emit_stat_output(filename, *stdin_result, "", cfg);
  }
  // Decode UTF-8 explicitly: a narrow std::filesystem::path decodes via the
  // system ACP, and exists() without the \\?\ prefix fails beyond MAX_PATH
  // (#1061).
  std::filesystem::path p(utf8_to_wstring(filename));

  // [GNU] Default stat() is lstat(): a symlink operand (even a dangling one)
  // stats the link itself; only -L follows it.
  const bool is_link = operand_is_symlink_w(p.wstring());
  const bool lstat_link = !cfg.dereference && is_link;

  // [GNU] stat -L follows the link: a dangling target fails with ENOENT
  // ("cannot statx 'dang': No such file or directory") (#1060).  Windows
  // attribute queries return the reparse point's own metadata even when
  // the target is gone, so resolve the operand through canonicalize.
  std::filesystem::path stat_target = p;
  if (cfg.dereference && is_link) {
    auto resolved = native_path::canonicalize_path_w(
        p.wstring(), native_path::CanonMode::existing);
    if (!resolved) {
      // [GNU] modern GNU stat uses the statx verb (uutils #13012)
      safeErrorPrint("stat: cannot statx '");
      safeErrorPrint(filename);
      safeErrorPrint("': No such file or directory\n");
      return 1;
    }
    stat_target = *resolved;
  }
  if (!lstat_link && !native_path::exists_w(stat_target.wstring())) {
    // [GNU] modern GNU stat uses the statx verb (uutils #13012)
    safeErrorPrint("stat: cannot statx '");
    safeErrorPrint(filename);
    safeErrorPrint("': No such file or directory\n");
    return 1;
  }

  if (cfg.file_system) {
    auto stat_result = load_file_system_stat(filename);
    if (!stat_result) {
      safeErrorPrint("stat: cannot read file system for '");
      safeErrorPrint(filename);
      safeErrorPrintLn("'");
      return 1;
    }

    if (cfg.terse) {
      // [GNU] -f -t: %n %i %l %t %s %S %b %f %a %c %d
      safePrint(render_file_system_format("%n %i %l %t %s %S %b %f %a %c %d",
                                          filename, *stat_result));
      safePrint("\n");
      return 0;
    }
    if (cfg.format.empty()) {
      return print_file_system_stat(filename, *stat_result);
    }

    auto format =
        cfg.printf_format ? expand_backslash_escapes(cfg.format) : cfg.format;
    safePrint(render_file_system_format(format, filename, *stat_result));
    if (!cfg.printf_format) {
      safePrint("\n");
    }
    return 0;
  }

  auto stat_result = load_file_stat(stat_target, lstat_link);
  if (!stat_result) {
    safeErrorPrint("stat: cannot statx '");
    safeErrorPrint(filename);
    safeErrorPrint("': Permission denied\n");
    return 1;
  }
  const auto& stat = *stat_result;

  // [GNU] A non-dereferenced symlink operand displays as "'name' ->
  // 'target'" in the File: line (uutils #8789).
  std::string link_target;
  if (lstat_link) {
    std::error_code ec;
    const auto target = std::filesystem::read_symlink(p, ec);
    if (!ec) {
      link_target = wstring_to_utf8(target.wstring());
    }
  }

  return emit_stat_output(filename, stat, link_target, cfg);
}

// Shared stat output for both file operands and the synthesized stdin
// stat (`stat -`).  |link_target| is non-empty for a non-dereferenced
// symlink operand.
auto emit_stat_output(const std::string& filename, const FileStatData& stat,
                      const std::string& link_target, const Config& cfg)
    -> int {
  if (cfg.terse) {
    // [GNU] -t: %n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %W %o (#1023)
    bool ctx_failed = false;
    safePrint(render_format("%n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %W %o",
                            filename, stat, ctx_failed));
    safePrint("\n");
  } else if (!cfg.format.empty()) {
    auto format =
        cfg.printf_format ? expand_backslash_escapes(cfg.format) : cfg.format;
    bool ctx_failed = false;
    safePrint(render_format(format, filename, stat, ctx_failed));
    if (!cfg.printf_format) {
      safePrint("\n");
    }
    if (ctx_failed) {
      // [GNU] %C without an SELinux context:
      // "stat: failed to get security context of 'F': No data available"
      safeErrorPrint("stat: failed to get security context of '");
      safeErrorPrint(filename);
      safeErrorPrint("': No data available\n");
      return 1;
    }
  } else {
    // [GNU] default layout (#1023):
    //   File: <n>[ -> <target>]
    //   Size: %-10s\tBlocks: %-10b IO Block: %-6o %F
    // Device: <dev>,<sub>\tInode: %-11i Links: %h
    // Access: (%04a/%A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)
    // Access/Modify/Change/ Birth timestamps
    safePrint("  File: ");
    safePrint(filename);
    if (!link_target.empty()) {
      safePrint(" -> ");
      safePrint(link_target);
    }
    safePrint("\n");

    safePrint("  Size: ");
    safePrint(pad_right(std::to_string(stat.size), 10));
    safePrint("\t");
    safePrint("Blocks: ");
    safePrint(pad_right(std::to_string(allocated_block_count(stat)), 10));
    safePrint(" IO Block: ");
    safePrint(pad_right(std::to_string(stat.io_block_size), 6));
    safePrint(" ");
    safePrint(file_type_name(stat));
    safePrint("\n");

    // [GNU] "Device: <major>,<minor>". Windows exposes a single device
    // identifier (the volume serial number); it is reported as the device
    // with subunit 0.
    safePrint("Device: ");
    safePrint(stat.volume_serial);
    safePrint(",0\t");
    safePrint("Inode: ");
    safePrint(pad_right(std::to_string(stat.file_index), 11));
    safePrint(" Links: ");
    safePrint(stat.hard_links);
    safePrint("\n");

    char mode_buf[8];
    snprintf(mode_buf, sizeof(mode_buf), "%04o", st_mode_bits(stat) & 07777u);
    const std::string uid = stat.owner_id.empty() ? "0" : stat.owner_id;
    const std::string gid = stat.group_id.empty() ? "0" : stat.group_id;
    safePrint("Access: (");
    safePrint(mode_buf);
    safePrint("/");
    safePrint(format_permissions(stat.attrs.dwFileAttributes, stat.fifo));
    safePrint(")  ");
    safePrint("Uid: (");
    safePrint(pad_left(uid, 5));
    safePrint("/");
    safePrint(pad_left(display_owner_name(stat.owner_name), 8));
    safePrint(")   ");
    safePrint("Gid: (");
    safePrint(pad_left(gid, 5));
    safePrint("/");
    safePrint(pad_left(display_group_name(stat.group_name), 8));
    safePrint(")\n");

    safePrint("Access: ");
    safePrint(format_timestamp(stat.attrs.ftLastAccessTime));
    safePrint("\n");
    safePrint("Modify: ");
    safePrint(format_timestamp(stat.attrs.ftLastWriteTime));
    safePrint("\n");
    safePrint("Change: ");
    safePrint(format_timestamp(stat.change_time));
    safePrint("\n");
    safePrint(" Birth: ");
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
    if (cfg_result.error() == "missing operand") {
      safeErrorPrint("stat: missing operand\n");
    } else {
      cp::report_error(cfg_result, L"stat");
    }
    safeErrorPrintLn("Try 'stat --help' for more information.");
    return 1;
  }

  return run(*cfg_result);
}
