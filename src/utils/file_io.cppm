/// @Author: caomengxuan666
/// @Description: File I/O utilities
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
module;

#include "pch/pch.h"
export module utils:file_io;

import std;
import :utf8;

/**
 * @brief Read file into lines
 * @param filename File path
 * @return Vector of lines (empty on error)
 */
export std::vector<std::string> read_file_lines(const std::string& filename) {
  std::vector<std::string> lines;

  std::wstring wfilename = utf8_to_wstring(filename);
  HANDLE hFile =
      CreateFileW(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hFile == INVALID_HANDLE_VALUE) {
    return lines;
  }

  LARGE_INTEGER fileSize;
  GetFileSizeEx(hFile, &fileSize);

  if (fileSize.QuadPart > 0) {
    std::string content;
    content.resize(fileSize.QuadPart);
    DWORD bytesRead;
    ReadFile(hFile, content.data(), static_cast<DWORD>(fileSize.QuadPart),
             &bytesRead, nullptr);
    content.resize(bytesRead);

    // Skip UTF-8 BOM if present
    size_t start = 0;
    if (bytesRead >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
      start = 3;
    }

    // Split into lines
    std::istringstream iss(content.substr(start));
    std::string line;
    while (std::getline(iss, line)) {
      // Remove trailing CR if present
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(line);
    }
  }

  CloseHandle(hFile);
  return lines;
}
