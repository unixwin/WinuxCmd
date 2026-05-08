/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: cpio.cpp
 *  - CopyrightYear: 2026
 */
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
    std::array{OPTION("-o", "--create", "create archive"),
               OPTION("-i", "--extract", "extract archive"),
               OPTION("-t", "--list", "list archive contents"),
               OPTION("-d", "", "create leading directories where needed"),
               OPTION("-m", "", "preserve modification time"),
               OPTION("-v", "--verbose", "verbose output")};

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
  sprintf_s(buffer, len + 1, "%08llx", value);
}

// Read file content
std::vector<char> read_file(const std::string& filename) {
  std::vector<char> content;

  std::wstring wfilename = utf8_to_wstring(filename);
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

// Write file content
bool write_file(const std::string& filename, const std::vector<char>& content) {
  std::wstring wfilename = utf8_to_wstring(filename);
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

// Create directory
bool create_directory(const std::string& path) {
  std::wstring wpath = utf8_to_wstring(path);
  return CreateDirectoryW(wpath.c_str(), nullptr) != FALSE;
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
  bool make_dirs = false;
  bool preserve_time = false;
  bool verbose = false;
};

auto build_config(const CommandContext<CPIO_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.create = ctx.get<bool>("-o", false) || ctx.get<bool>("--create", false);
  cfg.extract = ctx.get<bool>("-i", false) || ctx.get<bool>("--extract", false);
  cfg.list = ctx.get<bool>("-t", false) || ctx.get<bool>("--list", false);
  cfg.make_dirs = ctx.get<bool>("-d", false);
  cfg.preserve_time = ctx.get<bool>("-m", false);
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);

  if (!cfg.create && !cfg.extract && !cfg.list) {
    return std::unexpected("missing option (-o/-i/-t)");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Read input (file list or archive)
  std::string input;
  input.assign(std::istreambuf_iterator<char>(std::cin),
               std::istreambuf_iterator<char>());

  if (cfg.create) {
    // Create archive
    std::istringstream iss(input);
    std::string line;
    std::vector<char> archive;

    while (std::getline(iss, line)) {
      // Remove trailing CR
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      if (line.empty()) continue;

      // Read file
      std::vector<char> content = read_file(line);
      if (content.empty()) {
        safeErrorPrint("cpio: warning: cannot read file '");
        safeErrorPrint(line);
        safeErrorPrintLn("'");
        continue;
      }

      // Get file attributes
      std::wstring wfile = utf8_to_wstring(line);
      DWORD attrs = GetFileAttributesW(wfile.c_str());
      bool is_dir = is_directory(attrs);

      // Create header
      CpioHeader header;
      memcpy(header.magic, "070701", 6);
      ull_to_hex(0, header.inode, 8);
      ull_to_hex(is_dir ? 040755 : 0100644, header.mode, 8);
      ull_to_hex(0, header.uid, 8);
      ull_to_hex(0, header.gid, 8);
      ull_to_hex(1, header.nlink, 8);

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

      // Add file content to archive
      if (!is_dir) {
        archive.insert(archive.end(), content.begin(), content.end());
        while (archive.size() % 4 != 0) {
          archive.push_back(0);
        }
      }
    }

    // Add end marker
    CpioHeader end_header;
    memcpy(end_header.magic, "070701", 6);
    memset(reinterpret_cast<char*>(&end_header) + 6, 0, sizeof(CpioHeader) - 6);
    ull_to_hex(0, end_header.inode, 8);
    ull_to_hex(0, end_header.mode, 8);
    ull_to_hex(0, end_header.namesize, 8);
    ull_to_hex(1, end_header.filesize, 8);  // One null byte for name

    archive.insert(archive.end(), reinterpret_cast<char*>(&end_header),
                   reinterpret_cast<char*>(&end_header) + sizeof(CpioHeader));
    archive.push_back('T');
    archive.push_back('R');
    archive.push_back('A');
    archive.push_back('I');
    archive.push_back('L');
    archive.push_back('E');
    archive.push_back('R');
    archive.push_back('!');
    archive.push_back('\0');
    while (archive.size() % 4 != 0) {
      archive.push_back(0);
    }

    // Output archive
    safePrint(std::string(archive.begin(), archive.end()));

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

      // Extract file
      if (cfg.verbose) {
        safePrintLn(filename);
      }

      if (filesize > 0) {
        if (pos + filesize > archive.size()) break;

        std::vector<char> content(archive.data() + pos,
                                  archive.data() + pos + filesize);
        pos += filesize;

        // Pad to 4-byte boundary
        while (pos % 4 != 0) {
          pos++;
        }

        if (!write_file(filename, content)) {
          safeErrorPrint("cpio: warning: cannot write file '");
          safeErrorPrint(filename);
          safeErrorPrintLn("'");
        }
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
      DWORD attrs = GetFileAttributesW(utf8_to_wstring(filename).c_str());
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
    "and listing archive contents. Uses new ASCII format (070701/070702).",

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
    safePrintLn("Usage: cpio -o | cpio -i | cpio -t");
    return 1;
  }

  return run(*cfg_result);
}
