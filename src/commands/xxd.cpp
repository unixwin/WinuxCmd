/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr XXD_OPTIONS =
    std::array{OPTION("-r", "--reverse", "reverse: convert hex to binary")};

REGISTER_COMMAND(xxd,
                 /* cmd_name */ "xxd",
                 /* cmd_synopsis */ "xxd [OPTION] [FILE]",
                 /* cmd_desc */ "Make a hexdump or do the reverse.",
                 /* examples */ "xxd file.txt\nxxd -r hex.txt > file.txt",
                 /* see_also */ "od",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ XXD_OPTIONS) {
  bool reverse =
      ctx.get<bool>("-r", false) || ctx.get<bool>("--reverse", false);

  if (reverse) {
    safePrintLn("xxd: reverse mode not fully implemented");
    return 1;
  }

  std::string filename =
      ctx.positionals.empty() ? "-" : std::string(ctx.positionals[0]);

  if (filename == "-") {
    std::vector<char> data;
    data.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());

    for (size_t i = 0; i < data.size(); i += 16) {
      char buf[32];
      sprintf_s(buf, sizeof(buf), "%08zx: ", i);
      safePrint(buf);

      for (size_t j = 0; j < 16 && i + j < data.size(); ++j) {
        sprintf_s(buf, sizeof(buf), "%02x",
                  static_cast<unsigned char>(data[i + j]));
        safePrint(buf);
        if (j % 2 == 1) safePrint(" ");
      }

      // Padding
      for (size_t j = data.size() % 16; j < 16; ++j) {
        safePrint("  ");
        if (j % 2 == 1) safePrint(" ");
      }

      safePrint(" ");

      for (size_t j = 0; j < 16 && i + j < data.size(); ++j) {
        char c = data[i + j];
        safePrint((c >= 32 && c < 127) ? std::string(1, c) : ".");
      }
      safePrintLn("\n");
    }
  } else {
    std::wstring wfilename = utf8_to_wstring(filename);
    HANDLE hFile =
        CreateFileW(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("xxd: cannot open '" + filename + "'");
      return 1;
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    std::vector<char> buffer(fileSize.QuadPart);
    DWORD bytesRead;
    ReadFile(hFile, buffer.data(), static_cast<DWORD>(fileSize.QuadPart),
             &bytesRead, nullptr);
    CloseHandle(hFile);

    for (size_t i = 0; i < bytesRead; i += 16) {
      char buf[32];
      sprintf_s(buf, sizeof(buf), "%08zx: ", i);
      safePrint(buf);

      for (size_t j = 0; j < 16 && i + j < bytesRead; ++j) {
        sprintf_s(buf, sizeof(buf), "%02x",
                  static_cast<unsigned char>(buffer[i + j]));
        safePrint(buf);
        if (j % 2 == 1) safePrint(" ");
      }

      // Padding
      size_t remaining = (bytesRead - i) % 16;
      for (size_t j = remaining; j < 16; ++j) {
        safePrint("  ");
        if (j % 2 == 1) safePrint(" ");
      }

      safePrint(" ");

      for (size_t j = 0; j < 16 && i + j < bytesRead; ++j) {
        char c = buffer[i + j];
        safePrint((c >= 32 && c < 127) ? std::string(1, c) : ".");
      }
      safePrintLn("\n");
    }
  }

  return 0;
}
