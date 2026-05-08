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
 *  - File: patch.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for patch command.
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

auto constexpr PATCH_OPTIONS = std::array{
    OPTION("-p", "", "strip NUM leading components from file names", INT_TYPE),
    OPTION("-i", "", "read patch from FILE", STRING_TYPE),
    OPTION("-R", "--reverse",
           "assume patch was created with old and new files swapped"),
    OPTION("-N", "--forward",
           "assume patch was created with old and new files swapped"),
    OPTION("-b", "", "back up the original file", STRING_TYPE),
    OPTION("", "--dry-run", "do not actually change any files")};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Parse hunk header
struct Hunk {
  int old_start = 0;
  int old_count = 0;
  int new_start = 0;
  int new_count = 0;
  std::vector<std::string> old_lines;
  std::vector<std::string> new_lines;
};

// Write lines to file
bool write_file_lines(const std::string& filename,
                      const std::vector<std::string>& lines) {
  std::wstring wfilename = utf8_to_wstring(filename);
  HANDLE hFile = CreateFileW(wfilename.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  std::string content;
  for (const auto& line : lines) {
    content += line + "\r\n";
  }

  DWORD bytesWritten;
  WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()),
            &bytesWritten, nullptr);
  CloseHandle(hFile);

  return true;
}

// Parse unified diff hunk
bool parse_hunk(const std::string& line, Hunk& hunk) {
  if (line.length() < 4 || line.substr(0, 2) != "@@") {
    return false;
  }

  // Parse @@ -old_start,old_count +new_start,new_count @@
  size_t at_pos = line.find("@@", 2);
  if (at_pos == std::string::npos) return false;

  std::string hunk_info = line.substr(3, at_pos - 3);
  size_t plus_pos = hunk_info.find('+');
  if (plus_pos == std::string::npos) return false;

  std::string old_part = hunk_info.substr(0, plus_pos);
  std::string new_part = hunk_info.substr(plus_pos + 1);

  // Trim whitespace
  while (!old_part.empty() &&
         std::isspace(static_cast<unsigned char>(old_part.back()))) {
    old_part.pop_back();
  }
  while (!new_part.empty() &&
         std::isspace(static_cast<unsigned char>(new_part.back()))) {
    new_part.pop_back();
  }

  // Parse old_start and old_count
  size_t comma_pos = old_part.find(',');
  if (comma_pos == std::string::npos) {
    try {
      hunk.old_start = std::stoi(old_part);
      hunk.old_count = 1;
    } catch (...) {
      return false;
    }
  } else {
    try {
      hunk.old_start = std::stoi(old_part.substr(0, comma_pos));
      hunk.old_count = std::stoi(old_part.substr(comma_pos + 1));
    } catch (...) {
      return false;
    }
  }

  // Convert to absolute values (patch format uses - to indicate old file)
  if (hunk.old_start < 0) hunk.old_start = -hunk.old_start;
  if (hunk.old_count < 0) hunk.old_count = -hunk.old_count;

  // Parse new_start and new_count
  comma_pos = new_part.find(',');
  if (comma_pos == std::string::npos) {
    try {
      hunk.new_start = std::stoi(new_part);
      hunk.new_count = 1;
    } catch (...) {
      return false;
    }
  } else {
    try {
      hunk.new_start = std::stoi(new_part.substr(0, comma_pos));
      hunk.new_count = std::stoi(new_part.substr(comma_pos + 1));
    } catch (...) {
      return false;
    }
  }

  return true;
}

// Apply hunk to file lines
bool apply_hunk(std::vector<std::string>& lines, const Hunk& hunk,
                bool reverse = false) {
  // Adjust for 1-based indexing and check bounds
  if (hunk.old_start <= 0) {
    return false;
  }
  size_t old_line_idx = static_cast<size_t>(hunk.old_start - 1);

  // Check if old lines match
  bool match = true;
  for (size_t i = 0;
       i < hunk.old_lines.size() && old_line_idx + i < lines.size(); ++i) {
    if (lines[old_line_idx + i] != hunk.old_lines[i]) {
      match = false;
      break;
    }
  }

  if (!match) {
    return false;  // Hunk doesn't match
  }

  // Remove old lines
  lines.erase(lines.begin() + old_line_idx,
              lines.begin() + old_line_idx + hunk.old_count);

  // Insert new lines at the correct position
  lines.insert(lines.begin() + old_line_idx, hunk.new_lines.begin(),
               hunk.new_lines.end());

  return true;
}

// Backup file
bool backup_file(const std::string& filename, const std::string& backup_ext) {
  std::wstring wsrc = utf8_to_wstring(filename);
  std::wstring wdst = utf8_to_wstring(filename + backup_ext);

  return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE) != FALSE;
}

// Strip path components
std::string strip_path(const std::string& path, int components) {
  if (components <= 0) return path;

  size_t pos = 0;
  int count = 0;

  // Find position after the Nth path separator
  for (size_t i = 0; i < path.length() && count < components; ++i) {
    if (path[i] == '/' || path[i] == '\\') {
      count++;
      pos = i + 1;  // Position after separator
    }
  }

  return (pos < path.length()) ? path.substr(pos) : path;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    patch,
    /* cmd_name */ "patch",
    /* cmd_synopsis */ "patch [OPTION]... [ORIGFILE [PATCHFILE]]",
    /* cmd_desc */
    "Apply a diff file to an original.\n"
    "Apply a diff file (patch) to an original file, creating a patched "
    "version.\n"
    "Supports unified diff format. Can also read patch from standard input.",
    /* examples */
    "  patch < changes.diff\n"
    "  patch -p1 < changes.diff\n"
    "  patch -i patch.txt original.c\n"
    "  patch -p0 -b .orig < changes.diff",
    /* see_also */ "diff, diff3",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ PATCH_OPTIONS) {
  int strip_components = ctx.get<bool>("-p", false)
                             ? std::stoi(ctx.get<std::string>("-p", ""))
                             : 1;
  std::string patch_file = ctx.get<std::string>("-i", "");
  bool reverse =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--reverse", false);
  std::string backup_ext = ctx.get<std::string>("-b", ".orig");
  bool dry_run = ctx.get<bool>("--dry-run", false);

  std::string patch_content;

  // Read patch file
  if (!patch_file.empty()) {
    std::wstring wfile = utf8_to_wstring(patch_file);
    HANDLE hFile =
        CreateFileW(wfile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("patch: cannot open patch file '" + patch_file + "'");
      return 1;
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    patch_content.resize(fileSize.QuadPart);
    DWORD bytesRead;
    ReadFile(hFile, patch_content.data(), static_cast<DWORD>(fileSize.QuadPart),
             &bytesRead, nullptr);
    CloseHandle(hFile);
  } else {
    patch_content = std::string(std::istreambuf_iterator<char>(std::cin),
                                std::istreambuf_iterator<char>());
  }

  if (patch_content.empty()) {
    safeErrorPrintLn("patch: no patch input");
    return 1;
  }

  // Parse patch
  std::istringstream iss(patch_content);
  std::string line;
  std::string target_file;
  std::vector<Hunk> hunks;
  Hunk current_hunk;
  bool in_hunk = false;

  while (std::getline(iss, line)) {
    // Remove trailing CR
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Parse file header
    if (line.length() >= 4 && line.substr(0, 4) == "--- ") {
      // Old file
      size_t space = line.find(' ');
      if (space != std::string::npos && space < line.length()) {
        size_t next_space = line.find(' ', space + 1);
        if (next_space != std::string::npos) {
          std::string file = line.substr(space + 1, next_space - space - 1);
          target_file = strip_path(file, strip_components);
        } else {
          // No second space, take everything after first space
          std::string file = line.substr(space + 1);
          target_file = strip_path(file, strip_components);
        }
      }
    } else if (line.length() >= 4 && line.substr(0, 4) == "+++ ") {
      // New file
      size_t space = line.find(' ');
      if (space != std::string::npos && space < line.length()) {
        size_t next_space = line.find(' ', space + 1);
        if (next_space != std::string::npos) {
          std::string file = line.substr(space + 1, next_space - space - 1);
          if (target_file.empty()) {
            target_file = strip_path(file, strip_components);
          }
        } else {
          // No second space, take everything after first space
          std::string file = line.substr(space + 1);
          if (target_file.empty()) {
            target_file = strip_path(file, strip_components);
          }
        }
      }
    } else if (parse_hunk(line, current_hunk)) {
      in_hunk = true;
    } else if (in_hunk) {
      if (line.empty() || line[0] == ' ' || line[0] == '+' || line[0] == '-') {
        if (line[0] == ' ' || line[0] == '-') {
          current_hunk.old_lines.push_back(line.substr(1));
        }
        if (line[0] == ' ' || line[0] == '+') {
          current_hunk.new_lines.push_back(line.substr(1));
        }
      } else {
        // End of hunk
        hunks.push_back(current_hunk);
        current_hunk = Hunk();
        in_hunk = false;
      }
    }
  }

  // Add last hunk
  if (in_hunk) {
    hunks.push_back(current_hunk);
  }

  if (target_file.empty()) {
    safeErrorPrintLn("patch: cannot find target file in patch");
    return 1;
  }

  // Read original file
  std::vector<std::string> lines = read_file_lines(target_file);

  if (lines.empty() && !hunks.empty()) {
    safeErrorPrintLn("patch: cannot open target file '" + target_file + "'");
    return 1;
  }

  // Backup file if requested
  if (!dry_run && ctx.get<bool>("-b", false)) {
    backup_file(target_file, backup_ext);
  }

  // Apply hunks
  int applied = 0;
  int failed = 0;

  for (const auto& hunk : hunks) {
    if (apply_hunk(lines, hunk, reverse)) {
      applied++;
    } else {
      failed++;
      safeErrorPrintLn("patch: hunk FAILED at line " +
                       std::to_string(hunk.old_start));
    }
  }

  // Write patched file
  if (!dry_run && failed == 0) {
    if (!write_file_lines(target_file, lines)) {
      safeErrorPrintLn("patch: cannot write to file '" + target_file + "'");
      return 1;
    }
  }

  safePrintLn("patch: " + std::to_string(applied) + " hunks applied, " +
              std::to_string(failed) + " failed");

  return (failed > 0) ? 1 : 0;
}
