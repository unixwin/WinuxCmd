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
        OPTION("-v", "--verbose", "verbose output"),
        // [GNU]
        OPTION("-F", "--file", "use given archive FILE instead of stdin/stdout",
               STRING_TYPE),
        // [GNU]
        OPTION("-H", "--format", "use given archive FORMAT", STRING_TYPE),
        // [GNU]
        OPTION("-R", "--owner", "set the ownership of all files", STRING_TYPE),
        // [GNU] -A, --append: append to an existing archive (copy-out
        // only, requires -F; cpio 2.15 main.c:616)
        OPTION("-A", "--append", "append to an existing archive"),
        // [GNU] --absolute-filenames: member names are taken literally
        // (a leading '/' names the current drive's root)
        OPTION("", "--absolute-filenames",
               "do not strip leading file name slashes"),
        // [GNU] --block-size: block size in units of 512 bytes
        // (cpio 2.15 main.c:124-125)
        OPTION("", "--block-size", "block size in units of 512 bytes",
               STRING_TYPE),
        // [GNU] -L, --dereference: follow symbolic links. The port
        // already archives the target's content, so this is accepted
        // and matches the standing behavior.
        OPTION("-L", "--dereference", "follow symbolic links"),
        // [GNU] --device-independent: renumber inodes and ignore
        // device numbers (cpio 2.15 main.c:469-472)
        OPTION("", "--device-independent",
               "make the archive independent of device and inode numbers"),
        // [GNU] --reproducible: alias of --device-independent
        // (cpio 2.15 main.c:202-204)
        OPTION("", "--reproducible", "alias of --device-independent"),
        // [GNU] -D, --directory DIR: change to directory DIR
        // (cpio 2.15 main.c:115-116)
        OPTION("-D", "--directory", "change to directory DIR", STRING_TYPE),
        // [GNU] -V, --dot: print a dot for each member processed
        OPTION("-V", "--dot", "print a dot for each file processed")};

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

// Convert hex string to number
unsigned long long hex_to_ull(const char* str, size_t len) {
  char buffer[32] = {0};
  strncpy_s(buffer, str, len);
  return std::stoull(buffer, nullptr, 16);
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

// [GNU] --absolute-filenames: a POSIX-absolute member name is taken
// literally instead of folding through the payload-root boundary:
// '/' is the root of the current drive, '/c/...' the root of drive C.
auto literal_member_path_w(const std::string& name) -> std::wstring {
  if (!name.empty() && name.front() == '/') {
    std::string rest = name.substr(1);
    std::wstring root;
    auto is_alpha = [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    if (rest.size() >= 2 && is_alpha(rest[0]) && rest[1] == '/') {
      root.push_back(static_cast<wchar_t>(rest[0]));
      root += L":\\";
      rest = rest.substr(2);
    } else {
      wchar_t cwd[MAX_PATH];
      if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0 && cwd[1] == L':') {
        root.assign(cwd, 3);  // "X:\"
      }
    }
    if (!root.empty()) {
      std::string tail = rest;
      std::ranges::replace(tail, '/', '\\');
      return root + utf8_to_wstring(tail);
    }
  }
  return operand_path_w(name);
}

// Read file content (already-resolved path)
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

// Read file content
std::vector<char> read_file(const std::string& filename) {
  return read_file_w(operand_path_w(filename));
}

// Write file content (already-resolved path)
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

// Write file content
bool write_file(const std::string& filename, const std::vector<char>& content) {
  return write_file_w(operand_path_w(filename), content);
}

// Copy the modification time from one file to another (best effort).
void copy_file_mtime(const std::wstring& src, const std::wstring& dst) {
  HANDLE hSrc = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hSrc == INVALID_HANDLE_VALUE) return;
  FILETIME ft;
  const bool have_time = GetFileTime(hSrc, nullptr, nullptr, &ft) != FALSE;
  CloseHandle(hSrc);
  if (!have_time) return;
  HANDLE hDst = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hDst != INVALID_HANDLE_VALUE) {
    SetFileTime(hDst, nullptr, nullptr, &ft);
    CloseHandle(hDst);
  }
}

// Scan a newc archive for the offset of its TRAILER header (used by
// --append: cpio 2.15 splices new members in before the trailer,
// src/util.c:636 prepare_append).
auto find_trailer_offset(const std::vector<char>& archive)
    -> std::optional<size_t> {
  size_t pos = 0;
  while (pos + sizeof(CpioHeader) <= archive.size()) {
    const CpioHeader* header =
        reinterpret_cast<const CpioHeader*>(archive.data() + pos);
    if (memcmp(header->magic, "070701", 6) != 0 &&
        memcmp(header->magic, "070702", 6) != 0) {
      return std::nullopt;
    }
    const unsigned long long namesize = hex_to_ull(header->namesize, 8);
    const unsigned long long filesize = hex_to_ull(header->filesize, 8);
    if (namesize < 1 || pos + sizeof(CpioHeader) + namesize > archive.size()) {
      return std::nullopt;
    }
    const std::string name(archive.data() + pos + sizeof(CpioHeader),
                           static_cast<size_t>(namesize) - 1);
    if (name == "TRAILER!!!") {
      return pos;
    }
    pos += sizeof(CpioHeader) + static_cast<size_t>(namesize);
    while (pos % 4 != 0) ++pos;
    pos += static_cast<size_t>(filesize);
    while (pos % 4 != 0) ++pos;
  }
  return std::nullopt;
}

// Windows equivalent for S_ISREG
bool is_regular_file(DWORD attrs) {
  return (attrs != INVALID_FILE_ATTRIBUTES) &&
         !(attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE));
}

// Windows equivalent for S_ISDIR
bool is_directory(DWORD attrs) {
  return (attrs != INVALID_FILE_ATTRIBUTES) &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Windows equivalent for S_ISFIFO
bool is_pipe(DWORD attrs) {
  return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DEVICE);
}

// Windows equivalent for S_ISCHR
bool is_char_device(DWORD attrs) {
  return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DEVICE);
}

// Get file type character
char get_file_type(DWORD attrs) {
  if (attrs == INVALID_FILE_ATTRIBUTES) return '?';
  if (is_directory(attrs)) return 'd';
  if (is_regular_file(attrs)) return '-';
  if (is_pipe(attrs)) return 'p';
  if (is_char_device(attrs)) return 'c';
  return '?';
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
  bool verbose = false;
  bool append = false;
  bool absolute_filenames = false;
  bool dereference = false;
  bool device_independent = false;
  bool dot = false;
  unsigned long long block_bytes = 0;
  std::string directory;
  std::string destination;
  std::string format;
  std::string owner;
  std::string file;
};

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
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  cfg.append = ctx.get<bool>("-A", false) || ctx.get<bool>("--append", false);
  cfg.absolute_filenames = ctx.get<bool>("--absolute-filenames", false);
  cfg.dereference =
      ctx.get<bool>("-L", false) || ctx.get<bool>("--dereference", false);
  cfg.device_independent = ctx.get<bool>("--device-independent", false) ||
                           ctx.get<bool>("--reproducible", false);
  cfg.dot = ctx.get<bool>("-V", false) || ctx.get<bool>("--dot", false);
  cfg.directory = ctx.get<std::string>("-D", "");
  if (cfg.directory.empty()) {
    cfg.directory = ctx.get<std::string>("--directory", "");
  }
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

  // --block-size: a count of 512-byte blocks (cpio 2.15 main.c:124-125);
  // a value below 1 is a usage error ("invalid block size", main.c:324).
  std::string block_text = ctx.get<std::string>("--block-size", "");
  if (!block_text.empty()) {
    unsigned long long blocks = 0;
    auto [ptr, ec] = std::from_chars(
        block_text.data(), block_text.data() + block_text.size(), blocks);
    if (ec != std::errc() || ptr != block_text.data() + block_text.size() ||
        blocks < 1) {
      return std::unexpected("invalid block size");
    }
    cfg.block_bytes = blocks * 512;
  }

  if (!cfg.create && !cfg.extract && !cfg.list && !cfg.pass_through) {
    return std::unexpected("missing option (-o/-i/-t/-p)");
  }

  // -H/--format is only valid with -o/--create
  if (!cfg.create && !cfg.format.empty()) {
    return std::unexpected("--format is only allowed with --create");
  }

  // [GNU] --append is copy-out only and needs an archive file
  // (cpio 2.15 main.c:616 and its usage checks).
  if (cfg.append && !cfg.create) {
    return std::unexpected("--append is only valid with -o/--create");
  }
  if (cfg.append && cfg.file.empty()) {
    return std::unexpected(
        "--append is used but no archive file name is "
        "given (use -F or -O options)");
  }

  if (cfg.pass_through) {
    if (ctx.positionals.empty()) {
      return std::unexpected("copy-pass mode requires a destination directory");
    }
    cfg.destination = std::string(ctx.positionals[0]);
  }

  return cfg;
}

// Resolve a member name to an on-disk path: through the payload-root
// boundary by default (#1422 policy), literally with
// --absolute-filenames.
auto member_path_w(const Config& cfg, const std::string& name) -> std::wstring {
  if (cfg.absolute_filenames) {
    return literal_member_path_w(name);
  }
  return operand_path_w(name);
}

auto run(const Config& cfg) -> int {
  // Read input (file list or archive). With -F/--file the archive is the
  // named file (an argv operand, resolved through the shared boundary by
  // read_file) instead of stdin; in create mode stdin always carries the
  // file list. Input is read BEFORE the -D directory change, matching
  // GNU, which opens the archive in main() and changes directory at the
  // start of the copy phase (cpio 2.15 util.c:1504 change_dir, called
  // from copyin.c:1447, copyout.c:633, copypass.c:83).
  std::string input;
  if (!cfg.create && !cfg.pass_through && !cfg.file.empty()) {
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

  // [GNU] -D/--directory: change to DIR before processing members; a
  // missing DIR is created when -d is also given (cpio 2.15 util.c:1504).
  if (!cfg.directory.empty()) {
    const std::wstring dir_w = operand_path_w(cfg.directory);
    if (GetFileAttributesW(dir_w.c_str()) == INVALID_FILE_ATTRIBUTES &&
        cfg.make_dirs) {
      native_path::create_directories_w(dir_w);
    }
    if (!SetCurrentDirectoryW(dir_w.c_str())) {
      safeErrorPrint("cpio: cannot change to directory '");
      safeErrorPrint(cfg.directory);
      safeErrorPrintLn("'");
      return 1;
    }
  }

  if (cfg.create) {
    // Create archive
    std::istringstream iss(input);
    std::string line;
    std::vector<char> archive;

    // [GNU] --append: start from the existing archive with its trailer
    // removed; the new trailer is written after the added members.
    if (cfg.append) {
      std::vector<char> existing = read_file(cfg.file);
      if (existing.empty() &&
          GetFileAttributesW(operand_path_w(cfg.file).c_str()) ==
              INVALID_FILE_ATTRIBUTES) {
        safeErrorPrint("cpio: cannot open archive '");
        safeErrorPrint(cfg.file);
        safeErrorPrintLn("'");
        return 1;
      }
      auto trailer = find_trailer_offset(existing);
      if (!trailer) {
        safeErrorPrint("cpio: cannot append to '");
        safeErrorPrint(cfg.file);
        safeErrorPrintLn("': no cpio trailer found");
        return 1;
      }
      archive.assign(existing.begin(), existing.begin() + *trailer);
    }

    unsigned long long next_inode = 1;
    while (std::getline(iss, line)) {
      // Remove trailing CR
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      if (line.empty()) continue;

      // Get file attributes first: a genuinely empty file is archived
      // like any other; only an unreadable one is skipped with a
      // warning. (-L/--dereference matches the standing behavior: the
      // target's content and attributes are what get archived.)
      std::wstring wfile = operand_path_w(line);
      DWORD attrs = GetFileAttributesW(wfile.c_str());
      if (attrs == INVALID_FILE_ATTRIBUTES) {
        safeErrorPrint("cpio: warning: cannot read file '");
        safeErrorPrint(line);
        safeErrorPrintLn("'");
        continue;
      }
      bool is_dir = is_directory(attrs);
      std::vector<char> content =
          is_dir ? std::vector<char>{} : read_file(line);

      // Create header
      CpioHeader header;
      memcpy(header.magic, "070701", 6);
      // [GNU] --device-independent/--reproducible: inode numbers are
      // renumbered sequentially and directory nlink is normalized
      // (device fields are always written as 0 by this port).
      ull_to_hex(cfg.device_independent ? next_inode++ : 0, header.inode, 8);
      ull_to_hex(is_dir ? 040755 : 0100644, header.mode, 8);
      ull_to_hex(0, header.uid, 8);
      ull_to_hex(0, header.gid, 8);
      ull_to_hex(cfg.device_independent && is_dir ? 2 : 1, header.nlink, 8);

      // Get modification time
      HANDLE hFile =
          CreateFileW(wfile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (hFile != INVALID_HANDLE_VALUE) {
        FILETIME ft;
        GetFileTime(hFile, nullptr, nullptr, &ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        ull_to_hex((uli.QuadPart - 116444736000000000ULL) / 10000000ULL,
                   header.mtime, 8);
        CloseHandle(hFile);
      } else {
        ull_to_hex(0, header.mtime, 8);
      }

      ull_to_hex(content.size(), header.filesize, 8);
      ull_to_hex(0, header.devmajor, 8);
      ull_to_hex(0, header.devminor, 8);
      ull_to_hex(0, header.rdevmajor, 8);
      ull_to_hex(0, header.rdevminor, 8);
      ull_to_hex(line.length() + 1, header.namesize, 8);
      ull_to_hex(0, header.check, 8);

      // Add header to archive
      archive.insert(archive.end(), reinterpret_cast<char*>(&header),
                     reinterpret_cast<char*>(&header) + sizeof(CpioHeader));

      // Add filename to archive
      archive.insert(archive.end(), line.begin(), line.end());
      archive.push_back('\0');

      // Pad to 4-byte boundary
      while (archive.size() % 4 != 0) {
        archive.push_back(0);
      }

      // Print verbose output in create mode
      if (cfg.verbose) {
        safePrintLn(line);
      }

      // Add file content to archive
      if (!is_dir) {
        archive.insert(archive.end(), content.begin(), content.end());
        while (archive.size() % 4 != 0) {
          archive.push_back(0);
        }
      }

      // [GNU] -V/--dot: one dot per member (cpio 2.15 copyout.c:831).
      if (cfg.dot) {
        safeErrorPrint(".");
      }
    }

    // Add end marker
    static constexpr std::string_view kTrailerName = "TRAILER!!!";
    CpioHeader end_header;
    memcpy(end_header.magic, "070701", 6);
    memset(reinterpret_cast<char*>(&end_header) + 6, 0, sizeof(CpioHeader) - 6);
    ull_to_hex(0, end_header.inode, 8);
    ull_to_hex(0, end_header.mode, 8);
    ull_to_hex(1, end_header.nlink, 8);
    ull_to_hex(0, end_header.filesize, 8);
    ull_to_hex(kTrailerName.size() + 1, end_header.namesize, 8);
    ull_to_hex(0, end_header.check, 8);

    archive.insert(archive.end(), reinterpret_cast<char*>(&end_header),
                   reinterpret_cast<char*>(&end_header) + sizeof(CpioHeader));
    archive.insert(archive.end(), kTrailerName.begin(), kTrailerName.end());
    archive.push_back('\0');
    while (archive.size() % 4 != 0) {
      archive.push_back(0);
    }

    // [GNU] --block-size: the finished archive is padded with zeros to
    // a whole number of blocks.
    if (cfg.block_bytes > 0) {
      while (archive.size() % cfg.block_bytes != 0) {
        archive.push_back(0);
      }
    }

    // Output archive: to the -F/--file target when given, else stdout.
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

  } else if (cfg.extract) {
    // Extract archive
    std::vector<char> archive(input.begin(), input.end());
    size_t pos = 0;

    while (pos < archive.size()) {
      if (pos + sizeof(CpioHeader) > archive.size()) break;

      CpioHeader* header = reinterpret_cast<CpioHeader*>(archive.data() + pos);

      // Check magic
      if (memcmp(header->magic, "070701", 6) != 0 &&
          memcmp(header->magic, "070702", 6) != 0) {
        break;
      }

      // Parse header
      unsigned long long namesize = hex_to_ull(header->namesize, 8);
      unsigned long long filesize = hex_to_ull(header->filesize, 8);
      unsigned long long mode = hex_to_ull(header->mode, 8);

      pos += sizeof(CpioHeader);

      // Get filename
      std::string filename(archive.data() + pos, namesize - 1);
      pos += namesize;

      // Pad to 4-byte boundary
      while (pos % 4 != 0) {
        pos++;
      }

      // Check for end marker
      if (filename == "TRAILER!!!") {
        break;
      }

      const bool is_dir_member = (mode & 0170000ULL) == 0040000ULL;
      const std::wstring target = member_path_w(cfg, filename);

      // Read the member's content (may be empty)
      std::vector<char> content;
      if (filesize > 0) {
        if (pos + filesize > archive.size()) break;
        content.assign(archive.data() + pos, archive.data() + pos + filesize);
      }
      pos += filesize;
      // Pad to 4-byte boundary
      while (pos % 4 != 0) {
        pos++;
      }

      // Extract member
      if (cfg.verbose) {
        safePrintLn(filename);
      }

      if (is_dir_member) {
        // Directory members materialize as directories.
        native_path::create_directories_w(target);
      } else {
        // Create leading directories if -d/--make-directories: the parent
        // of the resolved target path, recursively (best effort).
        if (cfg.make_dirs) {
          auto pos_sep = target.find_last_of(L"\\/");
          if (pos_sep != std::wstring::npos) {
            native_path::create_directories_w(target.substr(0, pos_sep));
          }
        }

        if (!write_file_w(target, content)) {
          safeErrorPrint("cpio: warning: cannot write file '");
          safeErrorPrint(filename);
          safeErrorPrintLn("'");
        }

        // Preserve modification time if -m/--preserve-modification-time
        if (cfg.preserve_time) {
          HANDLE hFile =
              CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
          if (hFile != INVALID_HANDLE_VALUE) {
            // Convert mtime from hex header to Windows FILETIME
            unsigned long long mtime_val = hex_to_ull(header->mtime, 8);
            ULARGE_INTEGER uli;
            uli.QuadPart = mtime_val * 10000000ULL + 116444736000000000ULL;
            FILETIME ft;
            ft.dwLowDateTime = uli.LowPart;
            ft.dwHighDateTime = uli.HighPart;
            SetFileTime(hFile, nullptr, nullptr, &ft);
            CloseHandle(hFile);
          }
        }
      }

      // [GNU] -V/--dot: one dot per member (cpio 2.15 copyin.c:1561ff).
      if (cfg.dot) {
        safeErrorPrint(".");
      }
    }

  } else if (cfg.list) {
    // List archive contents
    std::vector<char> archive(input.begin(), input.end());
    size_t pos = 0;

    while (pos < archive.size()) {
      if (pos + sizeof(CpioHeader) > archive.size()) break;

      CpioHeader* header = reinterpret_cast<CpioHeader*>(archive.data() + pos);

      // Check magic
      if (memcmp(header->magic, "070701", 6) != 0 &&
          memcmp(header->magic, "070702", 6) != 0) {
        break;
      }

      // Parse header
      unsigned long long namesize = hex_to_ull(header->namesize, 8);
      unsigned long long filesize = hex_to_ull(header->filesize, 8);
      unsigned long long mode = hex_to_ull(header->mode, 8);

      pos += sizeof(CpioHeader);

      // Get filename
      std::string filename(archive.data() + pos, namesize - 1);
      pos += namesize;

      // Pad to 4-byte boundary
      while (pos % 4 != 0) {
        pos++;
      }

      // Check for end marker
      if (filename == "TRAILER!!!") {
        break;
      }

      // Print file info
      DWORD attrs = GetFileAttributesW(operand_path_w(filename).c_str());
      char file_type = get_file_type(attrs);

      char buf[256];
      sprintf_s(buf, sizeof(buf), "%c %10llu %s", file_type, filesize,
                filename.c_str());
      safePrintLn(buf);

      // Skip file content
      pos += filesize;
      while (pos % 4 != 0) {
        pos++;
      }
    }
  } else if (cfg.pass_through) {
    // Copy-pass mode: copy each stdin-listed file into the destination
    // directory, preserving relative names.
    const std::wstring dest_root = operand_path_w(cfg.destination);
    if (cfg.make_dirs) {
      native_path::create_directories_w(dest_root);
    }
    if (!is_directory(GetFileAttributesW(dest_root.c_str()))) {
      safeErrorPrint("cpio: destination directory '");
      safeErrorPrint(cfg.destination);
      safeErrorPrintLn("' does not exist");
      return 1;
    }

    int failures = 0;
    std::istringstream iss(input);
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty()) continue;

      const std::wstring src_w = operand_path_w(line);
      const DWORD attrs = GetFileAttributesW(src_w.c_str());
      if (attrs == INVALID_FILE_ATTRIBUTES) {
        safeErrorPrint("cpio: warning: cannot read file '");
        safeErrorPrint(line);
        safeErrorPrintLn("'");
        ++failures;
        continue;
      }
      const std::wstring dst_w = operand_path_w(cfg.destination + "/" + line);

      if (is_directory(attrs)) {
        native_path::create_directories_w(dst_w);
      } else {
        if (cfg.make_dirs) {
          auto pos_sep = dst_w.find_last_of(L"\\/");
          if (pos_sep != std::wstring::npos) {
            native_path::create_directories_w(dst_w.substr(0, pos_sep));
          }
        }
        if (!write_file_w(dst_w, read_file_w(src_w))) {
          safeErrorPrint("cpio: warning: cannot write file '");
          safeErrorPrint(line);
          safeErrorPrintLn("'");
          ++failures;
          continue;
        }
        if (cfg.preserve_time) {
          copy_file_mtime(src_w, dst_w);
        }
      }

      if (cfg.verbose) {
        safePrintLn(line);
      }
      // [GNU] -V/--dot: one dot per member (cpio 2.15 copypass.c).
      if (cfg.dot) {
        safeErrorPrint(".");
      }
    }
    return failures > 0 ? 1 : 0;
  }

  return 0;
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
    "Copy files to and from cpio archives. Supports creating, extracting,\n"
    "listing and copy-pass (-p DESTINATION). Uses new ASCII format\n"
    "(070701/070702).  -A/--append splices new members in before the\n"
    "trailer of the -F archive; --block-size pads the finished archive\n"
    "to whole 512-byte blocks; --device-independent renumbers inode\n"
    "numbers so archives are reproducible; -D/--directory changes the\n"
    "working directory before members are processed; -V/--dot prints\n"
    "one dot per member.  Without --absolute-filenames, member names\n"
    "resolve through the shell's payload-root path boundary.",

    /* examples */
    "  find . | cpio -o > archive.cpio\n"
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
