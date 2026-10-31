// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cpio command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr CPIO_OPTIONS =
    // [GNU]
    std::array{
        OPTION("-o", "--create", "create archive"),
        // [GNU]
        OPTION("-i", "--extract", "extract archive"),
        // [GNU]
        OPTION("-t", "--list", "list archive contents"),
        // [GNU]
        OPTION("-p", "--pass-through", "run in copy-pass mode"),
        // [GNU]
        OPTION("-d", "--make-directories",
               "create leading directories where needed"),
        // [GNU]
        OPTION("-m", "--preserve-modification-time",
               "preserve modification time"),
        // [GNU]
        OPTION("-u", "--unconditional",
               "replace files unconditionally (default: an existing file "
               "that is newer or same age is not replaced)"),
        // [GNU]
        OPTION("-v", "--verbose", "verbose output"),
        // [GNU]
        OPTION("", "--quiet", "do not print the number of blocks copied"),
        // [GNU]
        OPTION("-F", "--file", "use given archive FILE instead of stdin/stdout",
               STRING_TYPE),
        // [GNU]
        OPTION("-H", "--format", "use given archive FORMAT", STRING_TYPE),
        // [GNU]
        OPTION("-R", "--owner", "set the ownership of all files", STRING_TYPE),
        OPTION("-A", "--append", "append to an existing archive"),
        OPTION("", "--absolute-filenames",
               "do not strip leading '/' characters from file names"),
        OPTION("-B", "", "set the I/O block size to 5120 bytes"),
        OPTION("", "--block-size",
               "set the I/O block size to BLOCK-SIZE x 512 bytes",
               STRING_TYPE),
        OPTION("-L", "--dereference", "dereference symbolic links"),
        OPTION("", "--device-independent",
               "write device-independent (reproducible) archives"),
        OPTION("", "--reproducible",
               "write device-independent (reproducible) archives"),
        OPTION("-D", "--directory", "change to directory DIR", STRING_TYPE),
        OPTION("-V", "--dot", "print a dot for each file processed"),
        OPTION("-a", "--reset-access-time",
               "reset access times of input files")};

// ======================================================
// Helper functions
// ======================================================

namespace {
// CPIO header structure (new ASCII format)
struct CpioHeader {
  char magic[6];      // "070701" or "070702"
  char inode[8];      // inode number
  char mode[8];       // file mode
  char uid[8];        // user id
  char gid[8];        // group id
  char nlink[8];      // number of links
  char mtime[8];      // modification time
  char filesize[8];   // file size
  char devmajor[8];   // device major number
  char devminor[8];   // device minor number
  char rdevmajor[8];  // rdev major
  char rdevminor[8];  // rdev minor
  char namesize[8];   // name size
  char check[8];      // checksum (for "070702")
};

// Convert hex string to number. Parsed by hand (not std::stoull): a
// corrupt or foreign header field must degrade to a partial value the
// caller can reject, not throw through the command.
unsigned long long hex_to_ull(const char* str, size_t len) {
  unsigned long long value = 0;
  for (size_t i = 0; i < len; ++i) {
    const char c = str[i];
    unsigned digit;
    if (c >= '0' && c <= '9') {
      digit = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<unsigned>(c - 'a') + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<unsigned>(c - 'A') + 10;
    } else {
      break;
    }
    value = (value << 4) | digit;
  }
  return value;
}

// Convert number to hex string
void ull_to_hex(unsigned long long value, char* buffer, size_t len) {
  static constexpr char kDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    buffer[len - 1 - i] = kDigits[value & 0x0fULL];
    value >>= 4;
  }
}

// #1422: member names and the -F archive path are path operands, so they
// go through the shared operand boundary (native_path) like every other
// command's file operands instead of raw CreateFileW on the literal text.
// Written policy for member names (archive content — a different operand
// class from argv): names in the shell's POSIX dialect (rooted at '/',
// including '/c/...' drive spellings) fold through the boundary exactly
// like argv operands (bare '/' names the payload root, #1364 ruling);
// Windows-native absolute names and relative names are taken literally
// (absolutized against the working directory), as GNU cpio takes member
// names literally. The extended form is what keeps trailing-dot/space
// leaves and device-stem leaves ("NUL.txt") naming the on-disk file
// (#1332/#1333); a trailing separator keeps the normalized form so the
// #1052 ENOTDIR enforcement still applies.
auto operand_path_w(const std::string& name) -> std::wstring {
  auto operand = native_path::make_api_path_operand(name);
  if (operand.had_trailing_separator) {
    return operand.normalized;
  }
  return operand.extended;
}

// Read file content
std::vector<char> read_file_w(const std::wstring& wfilename) {
  std::vector<char> content;

  HANDLE hFile =
      CreateFileW(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    return content;
  }

  LARGE_INTEGER fileSize;
  GetFileSizeEx(hFile, &fileSize);

  if (fileSize.QuadPart > 0) {
    content.resize(fileSize.QuadPart);
    DWORD bytesRead;
    ReadFile(hFile, content.data(), static_cast<DWORD>(fileSize.QuadPart),
             &bytesRead, nullptr);
    content.resize(bytesRead);
  }

  CloseHandle(hFile);
  return content;
}

std::vector<char> read_file(const std::string& filename) {
  return read_file_w(operand_path_w(filename));
}

// Write file content
bool write_file_w(const std::wstring& wfilename,
                  const std::vector<char>& content) {
  HANDLE hFile = CreateFileW(wfilename.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD bytesWritten;
  WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()),
            &bytesWritten, nullptr);
  CloseHandle(hFile);

  return true;
}

bool write_file(const std::string& filename,
                const std::vector<char>& content) {
  return write_file_w(operand_path_w(filename), content);
}

// One stat of a filesystem object: everything the create and extract
// paths need (attributes, size, mtime as Unix seconds).
struct MemberStat {
  bool exists = false;
  bool is_dir = false;
  bool readonly = false;
  unsigned long long size = 0;
  unsigned long long mtime = 0;  // seconds since the Unix epoch
};

auto stat_member_w(const std::wstring& wpath) -> MemberStat {
  MemberStat st;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data)) {
    return st;
  }
  st.exists = true;
  st.is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  st.readonly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  st.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
            data.nFileSizeLow;
  ULARGE_INTEGER uli;
  uli.LowPart = data.ftLastWriteTime.dwLowDateTime;
  uli.HighPart = data.ftLastWriteTime.dwHighDateTime;
  if (uli.QuadPart >= 116444736000000000ULL) {
    st.mtime = (uli.QuadPart - 116444736000000000ULL) / 10000000ULL;
  }
  return st;
}

auto stat_member(const std::string& name) -> MemberStat {
  return stat_member_w(operand_path_w(name));
}

auto set_member_mtime_w(const std::wstring& wfile,
                        unsigned long long mtime, bool is_dir) -> void {
  HANDLE hFile = CreateFileW(
      wfile.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      is_dir ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return;
  ULARGE_INTEGER uli;
  uli.QuadPart = mtime * 10000000ULL + 116444736000000000ULL;
  FILETIME ft;
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  SetFileTime(hFile, nullptr, nullptr, &ft);
  CloseHandle(hFile);
}

auto set_member_mtime(const std::string& name, unsigned long long mtime,
                      bool is_dir) -> void {
  set_member_mtime_w(operand_path_w(name), mtime, is_dir);
}

// CRC-format (070702) checksum: the sum of all data bytes modulo 2^32
// (cpiohdr.h in GNU cpio: c_check is "the sum of the file's data bytes").
auto data_checksum(const char* data, size_t size) -> unsigned long {
  unsigned long sum = 0;
  for (size_t i = 0; i < size; ++i) {
    sum += static_cast<unsigned char>(data[i]);
  }
  return sum;
}

// [GNU] Every completed copy-in/copy-out/copy-pass run reports
// "%lu block(s)" on stderr, in units of the I/O block size (512 bytes
// by default, global.c io_block_size), unless --quiet (copyin.c
// process_copy_in tail, copyout.c, copypass.c).
auto print_blocks(unsigned long long bytes, bool quiet,
                  unsigned long long block_bytes = 512) -> void {
  if (quiet) return;
  const unsigned long long blocks =
      (bytes + block_bytes - 1) / block_bytes;
  if (blocks == 1) {
    safeErrorPrintLn("1 block");
  } else {
    safeErrorPrintLn(std::to_string(blocks) + " blocks");
  }
}

// The 10-character ls-style mode string GNU's long_format builds with
// filemode.c mode_string (copyin.c long_format).
auto mode_string(unsigned long long mode) -> std::string {
  std::string out(10, '-');
  switch (mode & 0170000ULL) {
    case 0040000ULL:
      out[0] = 'd';
      break;
    case 0120000ULL:
      out[0] = 'l';
      break;
    case 0010000ULL:
      out[0] = 'p';
      break;
    case 0140000ULL:
      out[0] = 's';
      break;
    case 0020000ULL:
      out[0] = 'c';
      break;
    case 0060000ULL:
      out[0] = 'b';
      break;
    case 0100000ULL:
      out[0] = '-';
      break;
    default:
      out[0] = '?';
      break;
  }
  static constexpr char kRwx[] = "rwx";
  for (int i = 0; i < 9; ++i) {
    if (mode & (0400ULL >> i)) out[1 + i] = kRwx[i % 3];
  }
  if (mode & 04000ULL) out[3] = (out[3] == 'x') ? 's' : 'S';
  if (mode & 02000ULL) out[6] = (out[6] == 'x') ? 's' : 'S';
  if (mode & 01000ULL) out[9] = (out[9] == 'x') ? 't' : 'T';
  return out;
}

// The date column of GNU's long_format: ctime text from position 4,
// with the year spliced in for entries older than six months or in the
// future (copyin.c long_format).
auto list_date(unsigned long long mtime) -> std::string {
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                            "May", "Jun", "Jul", "Aug",
                                            "Sep", "Oct", "Nov", "Dec"};
  const std::time_t when = static_cast<std::time_t>(mtime);
  std::tm local{};
  localtime_s(&local, &when);
  const std::time_t now = std::time(nullptr);
  const bool recent =
      when <= now && (now - when) < (31556952LL / 2);
  char buf[32];
  if (recent) {
    sprintf_s(buf, sizeof(buf), "%s %2d %02d:%02d", kMonths[local.tm_mon],
              local.tm_mday, local.tm_hour, local.tm_min);
  } else {
    sprintf_s(buf, sizeof(buf), "%s %2d  %04d", kMonths[local.tm_mon],
              local.tm_mday, local.tm_year + 1900);
  }
  return buf;
}
}  // namespace

// ======================================================
// Pipeline components
// ======================================================
namespace cpio_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool create = false;
  bool extract = false;
  bool list = false;
  bool pass_through = false;
  bool make_dirs = false;
  bool preserve_time = false;
  bool unconditional = false;
  bool verbose = false;
  bool quiet = false;
  bool crc_format = false;
  std::string format;
  std::string owner;
  std::string file;
  // [refs #1127 composition] Options ported onto the #1462 pipeline
  // from the P2c branch (GNU cpio 2.15 main.c option set).
  bool append = false;
  bool absolute_filenames = false;
  bool block_size_flag = false;  // -B
  unsigned long long block_bytes = 0;  // --block-size (0 = unset)
  bool dereference = false;      // -L (accepted; members store content)
  bool device_independent = false;
  bool dot = false;
  bool reset_access_time = false;  // -a (accepted; atime is not archived)
  std::string directory;           // -D/--directory
  std::string destination;         // copy-pass positional operand
};

// Effective I/O block size: 512 by default (global.c:30), 5120 with
// -B (main.c:321), BLOCK-SIZE x 512 with --block-size (main.c:324-330).
auto effective_block_bytes(const Config& cfg) -> unsigned long long {
  if (cfg.block_bytes > 0) return cfg.block_bytes;
  return cfg.block_size_flag ? 5120ULL : 512ULL;
}

auto build_config(const CommandContext<CPIO_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.create = ctx.get<bool>("-o", false) || ctx.get<bool>("--create", false);
  cfg.extract = ctx.get<bool>("-i", false) || ctx.get<bool>("--extract", false);
  cfg.list = ctx.get<bool>("-t", false) || ctx.get<bool>("--list", false);
  cfg.pass_through =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--pass-through", false);
  cfg.make_dirs =
      ctx.get<bool>("-d", false) || ctx.get<bool>("--make-directories", false);
  cfg.preserve_time = ctx.get<bool>("-m", false) ||
                      ctx.get<bool>("--preserve-modification-time", false);
  cfg.unconditional =
      ctx.get<bool>("-u", false) || ctx.get<bool>("--unconditional", false);
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  cfg.quiet = ctx.get<bool>("--quiet", false);
  cfg.format = ctx.get<std::string>("-H", "");
  if (cfg.format.empty()) {
    cfg.format = ctx.get<std::string>("--format", "");
  }
  cfg.owner = ctx.get<std::string>("-R", "");
  if (cfg.owner.empty()) {
    cfg.owner = ctx.get<std::string>("--owner", "");
  }
  cfg.file = ctx.get<std::string>("-F", "");
  if (cfg.file.empty()) {
    cfg.file = ctx.get<std::string>("--file", "");
  }

  if (!cfg.create && !cfg.extract && !cfg.list && !cfg.pass_through) {
    // [GNU] main.c: "You must specify one of -oipt options."
    return std::unexpected("You must specify one of -oipt options.");
  }
  if (cfg.list && (cfg.create || cfg.pass_through)) {
    return std::unexpected("--list is meaningless with --create/--pass-through");
  }
  // -t with -i is GNU's table mode, not an extraction.
  if (cfg.list) cfg.extract = true;

  cfg.append =
      ctx.get<bool>("-A", false) || ctx.get<bool>("--append", false);
  cfg.absolute_filenames = ctx.get<bool>("--absolute-filenames", false);
  cfg.block_size_flag = ctx.get<bool>("-B", false);
  cfg.dereference =
      ctx.get<bool>("-L", false) || ctx.get<bool>("--dereference", false);
  cfg.device_independent = ctx.get<bool>("--device-independent", false) ||
                           ctx.get<bool>("--reproducible", false);
  cfg.dot = ctx.get<bool>("-V", false) || ctx.get<bool>("--dot", false);
  cfg.reset_access_time = ctx.get<bool>("-a", false) ||
                          ctx.get<bool>("--reset-access-time", false);
  cfg.directory = ctx.get<std::string>("-D", "");
  if (cfg.directory.empty()) {
    cfg.directory = ctx.get<std::string>("--directory", "");
  }
  {
    const std::string block_size = ctx.get<std::string>("--block-size", "");
    if (!block_size.empty()) {
      // [GNU] main.c:324-330: the value counts 512-byte blocks and
      // must be positive ("invalid block size").
      unsigned long long units = 0;
      bool valid = true;
      for (const char c : block_size) {
        if (c < '0' || c > '9') {
          valid = false;
          break;
        }
        units = units * 10 + static_cast<unsigned long long>(c - '0');
      }
      if (!valid || units == 0) {
        return std::unexpected("invalid block size");
      }
      cfg.block_bytes = units * 512ULL;
    }
  }

  if (cfg.pass_through) {
    // [GNU] Copy-pass takes its destination directory as the sole
    // positional operand (copypass.c); without one the mode cannot run.
    if (!ctx.positionals.empty()) {
      cfg.destination = std::string(ctx.positionals[0]);
    }
    if (cfg.destination.empty()) {
      return std::unexpected(
          "copy-pass mode requires a destination directory");
    }
  }
  if (cfg.append && !cfg.create) {
    return std::unexpected("--append can only be used with --create (-o)");
  }
  if (cfg.append && cfg.file.empty()) {
    // [GNU] main.c:664-666, verbatim.
    return std::unexpected(
        "--append is used but no archive file name is given "
        "(use -F or -O options)");
  }

  if (!cfg.format.empty()) {
    // [GNU] main.c validates -H against the format name set and errors
    // with: invalid archive format `%s'; valid formats are:
    // crc newc odc bin ustar tar (all-caps also recognized).
    std::string lower = cfg.format;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static constexpr std::array kKnown = {"newc", "crc",  "odc",  "bin",
                                          "ustar", "tar", "hpodc", "hpbin"};
    if (std::find(kKnown.begin(), kKnown.end(), lower) == kKnown.end()) {
      return std::unexpected("invalid archive format `" + cfg.format +
                             "'; valid formats are:\n"
                             "crc newc odc bin ustar tar "
                             "(all-caps also recognized)");
    }
    if (cfg.create) {
      // Only the two new-ASCII formats are written; the historical build
      // silently wrote newc under any -H label, mislabeling archives.
      if (lower != "newc" && lower != "crc") {
        return std::unexpected("archive format '" + cfg.format +
                               "' is not supported for --create "
                               "(supported: newc, crc)");
      }
      cfg.crc_format = (lower == "crc");
    } else {
      // Reading auto-detects between the newc and crc magics; other
      // formats are not read, and naming one is an error rather than a
      // silent re-interpretation.
      if (lower != "newc" && lower != "crc") {
        return std::unexpected("archive format '" + cfg.format +
                               "' is not supported for reading "
                               "(supported: newc, crc)");
      }
    }
  }

  return cfg;
}

// ======================================================
// Composition helpers (P2c options on the #1462 pipeline)
// ======================================================

// A member name is rooted when it starts with a separator or carries
// a drive qualifier; rooted names are unaffected by -D, exactly as
// they are unaffected by GNU's change_dir (util.c:1504).
auto is_rooted_name(const std::string& name) -> bool {
  if (name.empty()) return false;
  if (name.front() == '/' || name.front() == '\\') return true;
  return name.size() >= 2 && name[1] == ':';
}

// Read side (copy-out inputs, copy-pass sources): -D joins relative
// names against the directory operand. The process working directory
// is never mutated, so the -F archive path and the name list keep
// resolving against the original directory, matching GNU's order
// (the archive descriptor is opened before change_dir, main.c).
auto member_source_w(const Config& cfg, const std::string& name)
    -> std::wstring {
  if (!cfg.directory.empty() && !is_rooted_name(name)) {
    return operand_path_w(cfg.directory + "/" + name);
  }
  return operand_path_w(name);
}

// Write side (copy-in targets): --absolute-filenames takes POSIX-
// absolute member names literally (a leading '/' is the current
// drive's root); otherwise -D joins relative names as on the read
// side.
auto literal_member_path_w(const std::string& name) -> std::wstring {
  std::string path = name;
  std::replace(path.begin(), path.end(), '/', '\\');
  if (path.size() >= 2 && path[0] == '\\' && path[1] != '\\') {
    // POSIX root: anchor at the current directory's drive.
    wchar_t drive[] = L"C:";
    wchar_t cwd_buf[8];
    if (GetCurrentDirectoryW(4, cwd_buf) > 0 && cwd_buf[1] == L':') {
      drive[0] = cwd_buf[0];
    }
    return std::wstring(drive) + utf8_to_wstring(path);
  }
  return utf8_to_wstring(path);
}

auto member_target_w(const Config& cfg, const std::string& name)
    -> std::wstring {
  if (cfg.absolute_filenames) return literal_member_path_w(name);
  return member_source_w(cfg, name);
}

// Offset just past the last real member of a newc/crc archive (the
// trailer's header start), for --append splicing. nullopt when the
// stream has no trailer.
auto find_trailer_offset(const std::vector<char>& archive)
    -> std::optional<size_t> {
  size_t pos = 0;
  while (pos + sizeof(CpioHeader) <= archive.size()) {
    const auto* header =
        reinterpret_cast<const CpioHeader*>(archive.data() + pos);
    if (memcmp(header->magic, "070701", 6) != 0 &&
        memcmp(header->magic, "070702", 6) != 0) {
      return std::nullopt;
    }
    const unsigned long long namesize = hex_to_ull(header->namesize, 8);
    const unsigned long long filesize = hex_to_ull(header->filesize, 8);
    if (namesize == 0) return std::nullopt;
    const size_t name_offset = pos + sizeof(CpioHeader);
    if (name_offset + namesize > archive.size()) return std::nullopt;
    const std::string name(archive.data() + name_offset, namesize - 1);
    if (name == "TRAILER!!!") return pos;
    size_t data = name_offset + namesize;
    while (data % 4 != 0) ++data;
    size_t end = data + static_cast<size_t>(filesize);
    while (end % 4 != 0) ++end;
    pos = end;
  }
  return std::nullopt;
}

// Copy-pass -m support: restamp the destination with the source's
// last-write time.
auto copy_file_mtime_w(const std::wstring& src, const std::wstring& dst)
    -> void {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &data)) {
    return;
  }
  HANDLE h = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  SetFileTime(h, nullptr, nullptr, &data.ftLastWriteTime);
  CloseHandle(h);
}

namespace {
// Shared header walk for extract and list. Returns false when the
// stream does not start with a new-ASCII magic at all.
struct Member {
  unsigned long long mode = 0;
  unsigned long long uid = 0;
  unsigned long long gid = 0;
  unsigned long long nlink = 0;
  unsigned long long mtime = 0;
  unsigned long long filesize = 0;
  unsigned long long check = 0;
  bool crc = false;
  std::string name;
  size_t data_offset = 0;  // offset of the file data in the archive
};

auto parse_member(const std::vector<char>& archive, size_t pos, Member& out)
    -> bool {
  if (pos + sizeof(CpioHeader) > archive.size()) return false;
  const auto* header =
      reinterpret_cast<const CpioHeader*>(archive.data() + pos);
  if (memcmp(header->magic, "070701", 6) == 0) {
    out.crc = false;
  } else if (memcmp(header->magic, "070702", 6) == 0) {
    out.crc = true;
  } else {
    return false;
  }
  out.mode = hex_to_ull(header->mode, 8);
  out.uid = hex_to_ull(header->uid, 8);
  out.gid = hex_to_ull(header->gid, 8);
  out.nlink = hex_to_ull(header->nlink, 8);
  out.mtime = hex_to_ull(header->mtime, 8);
  out.filesize = hex_to_ull(header->filesize, 8);
  out.check = hex_to_ull(header->check, 8);
  const unsigned long long namesize = hex_to_ull(header->namesize, 8);
  if (namesize == 0) return false;
  size_t name_offset = pos + sizeof(CpioHeader);
  if (name_offset + namesize > archive.size()) return false;
  out.name.assign(archive.data() + name_offset, namesize - 1);
  size_t data = name_offset + namesize;
  while (data % 4 != 0) ++data;
  out.data_offset = data;
  return true;
}

auto member_span_end(const Member& m) -> size_t {
  size_t end = m.data_offset + static_cast<size_t>(m.filesize);
  while (end % 4 != 0) ++end;
  return end;
}
}  // namespace

auto run_create(const Config& cfg, const std::string& input,
                const std::vector<char>& existing) -> int {
  std::istringstream iss(input);
  std::string line;
  std::vector<char> archive;
  bool failed = false;
  const char* magic = cfg.crc_format ? "070702" : "070701";

  if (cfg.append) {
    // [GNU] --append splices the new members where the old trailer
    // sat (copyout appends at the trailer of the existing archive).
    const auto trailer = find_trailer_offset(existing);
    if (!trailer) {
      safeErrorPrintLn(
          "cpio: cannot find the archive trailer; refusing to append");
      return 1;
    }
    archive.assign(existing.begin(), existing.begin() + *trailer);
  }
  // --device-independent/--reproducible: inode numbers are assigned
  // sequentially from 1 (the P2c composition; GNU zeroes the device
  // fields, which this writer already does).
  unsigned long long next_ino = 1;

  auto append_padded = [&archive](const char* data, size_t size) {
    archive.insert(archive.end(), data, data + size);
    while (archive.size() % 4 != 0) archive.push_back(0);
  };

  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    // Stat first: an unreadable or missing name is a per-file error
    // ("cpio: <name>: <reason>", GNU copyout's stat failure path), and
    // an empty file or a directory is a normal member. The historical
    // build conflated "read_file returned nothing" with failure, which
    // silently dropped every empty file and directory from archives.
    const std::wstring source_w = member_source_w(cfg, line);
    const MemberStat st = stat_member_w(source_w);
    if (!st.exists) {
      safeErrorPrint("cpio: ");
      safeErrorPrint(line);
      safeErrorPrint(": ");
      safeErrorPrintLn(win32_posix_error_text(GetLastError()));
      failed = true;
      continue;
    }

    std::vector<char> content;
    if (!st.is_dir) {
      content = read_file_w(source_w);
      if (content.size() != st.size) {
        safeErrorPrint("cpio: warning: cannot read file '");
        safeErrorPrint(line);
        safeErrorPrintLn("'");
        failed = true;
        continue;
      }
    }

    CpioHeader header;
    memcpy(header.magic, magic, 6);
    ull_to_hex(cfg.device_independent ? next_ino++ : 0, header.inode, 8);
    // Windows has no POSIX permission bits; store the closest stable
    // mapping (read-only files lose the write bits), as stat does.
    const unsigned long long mode =
        st.is_dir ? (st.readonly ? 040555ULL : 040755ULL)
                  : (st.readonly ? 0100444ULL : 0100644ULL);
    ull_to_hex(mode, header.mode, 8);
    ull_to_hex(0, header.uid, 8);
    ull_to_hex(0, header.gid, 8);
    ull_to_hex(st.is_dir ? 2 : 1, header.nlink, 8);
    ull_to_hex(st.mtime, header.mtime, 8);
    ull_to_hex(content.size(), header.filesize, 8);
    ull_to_hex(0, header.devmajor, 8);
    ull_to_hex(0, header.devminor, 8);
    ull_to_hex(0, header.rdevmajor, 8);
    ull_to_hex(0, header.rdevminor, 8);
    ull_to_hex(line.length() + 1, header.namesize, 8);
    ull_to_hex(cfg.crc_format
                   ? data_checksum(content.data(), content.size())
                   : 0,
               header.check, 8);

    // The header is NOT padded on its own: newc pads (header + name)
    // and (data) each to a 4-byte boundary counted from the header
    // start, and the running total is always 4-aligned at a member
    // boundary, so padding the running total is the alignment rule.
    archive.insert(archive.end(), reinterpret_cast<const char*>(&header),
                   reinterpret_cast<const char*>(&header) +
                       sizeof(CpioHeader));
    archive.insert(archive.end(), line.begin(), line.end());
    archive.push_back('\0');
    while (archive.size() % 4 != 0) archive.push_back(0);

    // [GNU] Copy-out verbose names go to stderr (copyout.c:829-830),
    // never into the archive stream on stdout; -V dots likewise
    // (copyout.c:831-832).
    if (cfg.verbose) safeErrorPrintLn(line);
    if (cfg.dot) safeErrorPrint(".");

    if (!st.is_dir) {
      append_padded(content.data(), content.size());
    }
  }

  // Trailer record. Every numeric field is an ASCII hex string, so the
  // header starts as all-'0' characters (a NUL-filled field would not
  // parse as hex on the read side).
  static constexpr std::string_view kTrailerName = "TRAILER!!!";
  CpioHeader end_header;
  memset(&end_header, '0', sizeof(CpioHeader));
  memcpy(end_header.magic, magic, 6);
  ull_to_hex(1, end_header.nlink, 8);
  ull_to_hex(kTrailerName.size() + 1, end_header.namesize, 8);
  archive.insert(archive.end(), reinterpret_cast<const char*>(&end_header),
                 reinterpret_cast<const char*>(&end_header) +
                     sizeof(CpioHeader));
  archive.insert(archive.end(), kTrailerName.begin(), kTrailerName.end());
  archive.push_back('\0');
  while (archive.size() % 4 != 0) archive.push_back(0);

  // The archive is padded to a whole number of I/O blocks (GNU writes
  // full blocks; main.c io_block_size, copyout.c tape pad).
  const unsigned long long block_bytes = effective_block_bytes(cfg);
  while (archive.size() % block_bytes != 0) archive.push_back(0);

  if (!cfg.file.empty()) {
    if (!write_file(cfg.file, archive)) {
      safeErrorPrint("cpio: cannot open archive '");
      safeErrorPrint(cfg.file);
      safeErrorPrintLn("'");
      return 1;
    }
  } else {
    safePrint(std::string(archive.begin(), archive.end()));
  }
  // [GNU] The dot line is terminated before the blocks report
  // (copyout.c:862-866).
  if (cfg.dot) safeErrorPrintLn("");
  print_blocks(archive.size(), cfg.quiet, block_bytes);
  return failed ? 1 : 0;
}

auto run_extract(const Config& cfg, const std::vector<char>& archive) -> int {
  size_t pos = 0;
  bool any = false;
  while (pos < archive.size()) {
    Member m;
    if (!parse_member(archive, pos, m)) {
      if (!any) {
        safeErrorPrintLn(
            "cpio: unsupported archive format (bad magic number)");
        return 1;
      }
      break;
    }
    if (m.name == "TRAILER!!!") break;
    any = true;
    const size_t end = member_span_end(m);
    if (m.data_offset + m.filesize > archive.size()) break;
    const char* data = archive.data() + m.data_offset;
    const unsigned long long type = m.mode & 0170000ULL;

    // [GNU] CRC archives are verified member by member as the data is
    // consumed: "%s: checksum error (0x%x, should be 0x%x)" (copyin.c).
    if (m.crc && type == 0100000ULL) {
      const unsigned long computed =
          data_checksum(data, static_cast<size_t>(m.filesize));
      if (computed != m.check) {
        char buf[64];
        sprintf_s(buf, sizeof(buf), " (0x%lx, should be 0x%llx)", computed,
                  m.check);
        safeErrorPrintLn(winux::i18n::format(
            "command.cpio.error.checksum",
            "cpio: {}: checksum error{}", m.name, buf));
      }
    }

    // [GNU] Copy-in verbose names go to stderr as well
    // (copyin.c:1583-1584: fprintf(stderr, "%s\n", c_name)); the
    // earlier stdout printing was the flagged composition divergence.
    if (cfg.verbose) safeErrorPrintLn(m.name);
    if (cfg.dot) safeErrorPrint(".");

    // The on-disk target: -D joins relative member names and
    // --absolute-filenames takes absolute names literally.
    const std::wstring target_w = member_target_w(cfg, m.name);

    // Create leading directories where the member needs them (-d).
    if (cfg.make_dirs) {
      auto pos_sep = target_w.find_last_of(L"\\/");
      if (pos_sep != std::wstring::npos) {
        native_path::create_directories_w(target_w.substr(0, pos_sep));
      }
    }

    if (type == 0040000ULL) {
      // Directory member: GNU creates it (with -m, keeping its time).
      native_path::create_directories_w(target_w);
      if (cfg.preserve_time) set_member_mtime_w(target_w, m.mtime, true);
    } else if (type == 0100000ULL || type == 0ULL) {
      // Regular file. [GNU] Without -u, an existing file that is newer
      // or the same age as the member is NOT replaced: "%s not created:
      // newer or same age version exists" (copyin.c).
      if (!cfg.unconditional) {
        const MemberStat existing = stat_member_w(target_w);
        if (existing.exists && !existing.is_dir &&
            m.mtime <= existing.mtime) {
          safeErrorPrintLn(winux::i18n::format(
              "command.cpio.error.not_created",
              "cpio: {} not created: newer or same age version exists",
              m.name));
          pos = end;
          continue;
        }
      }
      std::vector<char> content(data, data + m.filesize);
      if (!write_file_w(target_w, content)) {
        safeErrorPrint("cpio: warning: cannot write file '");
        safeErrorPrint(m.name);
        safeErrorPrintLn("'");
      } else if (cfg.preserve_time) {
        set_member_mtime_w(target_w, m.mtime, false);
      }
    } else if (type == 0120000ULL) {
      // Symbolic link member: the data is the target text.
      std::string target_text(data, static_cast<size_t>(m.filesize));
      std::wstring wtarget = target_w;
      // Remove a stale entry first (POSIX ln -f semantics of extraction).
      DeleteFileW(wtarget.c_str());
      if (!CreateSymbolicLinkW(wtarget.c_str(),
                               utf8_to_wstring(target_text).c_str(),
                               SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        safeErrorPrint("cpio: warning: cannot create symlink '");
        safeErrorPrint(m.name);
        safeErrorPrintLn("'");
      }
    } else {
      safeErrorPrint("cpio: warning: unsupported file type for '");
      safeErrorPrint(m.name);
      safeErrorPrintLn("'");
    }
    pos = end;
  }
  // [GNU] The dot line is terminated before the blocks report
  // (copyin.c:1590-1591).
  if (cfg.dot) safeErrorPrintLn("");
  print_blocks(archive.size(), cfg.quiet, effective_block_bytes(cfg));
  return 0;
}

// [refs #1127 composition] Copy-pass mode (-p), ported from the P2c
// branch onto the shared helpers: listed files are copied into the
// destination directory without an archive round-trip (copypass.c).
// The destination resolves against the ORIGINAL directory even when
// -D is given: GNU absolutizes it before change_dir (copypass.c).
auto run_pass(const Config& cfg, const std::string& input) -> int {
  const std::wstring dest_root = operand_path_w(cfg.destination);
  if (cfg.make_dirs) {
    native_path::create_directories_w(dest_root);
  }

  std::istringstream iss(input);
  std::string line;
  bool failed = false;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    const std::wstring src_w = member_source_w(cfg, line);
    const MemberStat st = stat_member_w(src_w);
    if (!st.exists) {
      safeErrorPrint("cpio: ");
      safeErrorPrint(line);
      safeErrorPrintLn(": No such file or directory");
      failed = true;
      continue;
    }

    const std::wstring dst_w =
        operand_path_w(cfg.destination + "/" + line);
    if (cfg.make_dirs) {
      auto pos_sep = dst_w.find_last_of(L"\\/");
      if (pos_sep != std::wstring::npos) {
        native_path::create_directories_w(dst_w.substr(0, pos_sep));
      }
    }

    if (st.is_dir) {
      if (cfg.make_dirs) native_path::create_directories_w(dst_w);
    } else {
      const std::vector<char> content = read_file_w(src_w);
      if (content.size() != st.size) {
        safeErrorPrint("cpio: ");
        safeErrorPrint(line);
        safeErrorPrintLn(": Cannot open, skipped.");
        failed = true;
        continue;
      }
      const bool written = write_file_w(dst_w, content);
      if (!written) {
        safeErrorPrint("cpio: warning: cannot write file '");
        safeErrorPrint(line);
        safeErrorPrintLn("'");
        failed = true;
        continue;
      }
      if (cfg.preserve_time) copy_file_mtime_w(src_w, dst_w);
    }

    // [GNU] Copy-pass verbose prints the OUTPUT name to stderr
    // (copypass.c:318-319); dots likewise (copypass.c:320-321).
    if (cfg.verbose) {
      safeErrorPrintLn(cfg.destination + "/" + line);
    }
    if (cfg.dot) safeErrorPrint(".");
  }
  if (cfg.dot) safeErrorPrintLn("");
  return failed ? 1 : 0;
}

auto run_list(const Config& cfg, const std::vector<char>& archive) -> int {
  size_t pos = 0;
  bool any = false;
  while (pos < archive.size()) {
    Member m;
    if (!parse_member(archive, pos, m)) {
      if (!any) {
        safeErrorPrintLn(
            "cpio: unsupported archive format (bad magic number)");
        return 1;
      }
      break;
    }
    if (m.name == "TRAILER!!!") break;
    any = true;
    if (m.data_offset + m.filesize > archive.size()) break;

    if (cfg.verbose) {
      // [GNU] long_format (copyin.c): mode string, nlink, owner and
      // group columns (numeric on Windows — there is no passwd file to
      // resolve names from), size, ls-style date, name; a symlink shows
      // its target after " -> ".
      char head[128];
      sprintf_s(head, sizeof(head), "%s %3llu %-8llu %-8llu %8llu ",
                mode_string(m.mode).c_str(), m.nlink, m.uid, m.gid,
                m.filesize);
      std::string line_out = head + list_date(m.mtime) + " " + m.name;
      if ((m.mode & 0170000ULL) == 0120000ULL) {
        line_out += " -> ";
        line_out.append(archive.data() + m.data_offset,
                        static_cast<size_t>(m.filesize));
      }
      safePrintLn(line_out);
    } else {
      // [GNU] A plain table of contents is the member names only.
      safePrintLn(m.name);
    }
    pos = member_span_end(m);
  }
  print_blocks(archive.size(), cfg.quiet, effective_block_bytes(cfg));
  return 0;
}

auto run(const Config& cfg) -> int {
  // Read input (file list or archive). With -F/--file the archive is
  // the named file (an argv operand, resolved through the shared
  // boundary by read_file) instead of stdin; in create mode stdin
  // always carries the file list.
  std::string input;
  if (!cfg.create && !cfg.file.empty()) {
    std::vector<char> archive_file = read_file(cfg.file);
    if (archive_file.empty() &&
        GetFileAttributesW(operand_path_w(cfg.file).c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
      safeErrorPrint("cpio: cannot open archive '");
      safeErrorPrint(cfg.file);
      safeErrorPrintLn("'");
      return 1;
    }
    input.assign(archive_file.begin(), archive_file.end());
  } else {
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
  }

  if (cfg.create) {
    // --append reads the existing -F archive (resolved against the
    // original directory, before any -D member joining) and splices
    // at its trailer.
    std::vector<char> existing;
    if (cfg.append) existing = read_file(cfg.file);
    return run_create(cfg, input, existing);
  }
  if (cfg.pass_through) return run_pass(cfg, input);
  std::vector<char> archive(input.begin(), input.end());
  if (cfg.list) return run_list(cfg, archive);
  return run_extract(cfg, archive);
}

}  // namespace cpio_pipeline

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    cpio,
    /* name */
    "cpio",

    /* synopsis */
    "cpio [OPTION]",

    /* description */
    "Copy files to and from archives.\n"
    "Copy files to and from cpio archives in the new ASCII formats\n"
    "(newc 070701, crc 070702 with checksum verification). Supports\n"
    "creating, extracting, listing archive contents, and copy-pass\n"
    "mode (-p).",

    /* examples */
    "  find . | cpio -o > archive.cpio\n"
    "  find . | cpio -o -H crc > archive.cpio\n"
    "  cpio -i < archive.cpio\n"
    "  cpio -t < archive.cpio\n"
    "  cpio -idmv < archive.cpio",

    /* see_also */
    "tar(1), pax(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    CPIO_OPTIONS) {
  using namespace cpio_pipeline;
  using namespace core::pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("cpio: ");
    safeErrorPrintLn(cfg_result.error());
    safePrintLn("Usage: cpio -o | cpio -i | cpio -t | cpio -p dest");
    return 1;
  }

  return run(*cfg_result);
}
