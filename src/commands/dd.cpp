/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr DD_OPTIONS =
    std::array{OPTION("if", "", "input file", STRING_TYPE),
               OPTION("of", "", "output file", STRING_TYPE),
               OPTION("bs", "", "block size", STRING_TYPE),
               OPTION("count", "", "copy N blocks", STRING_TYPE)};

REGISTER_COMMAND(dd,
                 /* name */
                 "dd",

                 /* synopsis */
                 "dd [OPTION]...",

                 /* description */
                 "Convert and copy a file with specified block size and count.",

                 /* examples */
                 "dd if=input.txt of=output.txt bs=4096",

                 /* see_also */
                 "cp(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 DD_OPTIONS) {
  std::string input_file = ctx.get<std::string>("if", "");
  std::string output_file = ctx.get<std::string>("of", "");
  size_t block_size = 512;
  size_t block_count = 0;

  std::string bs_value = ctx.get<std::string>("bs", "");
  if (!bs_value.empty()) {
    try {
      block_size = std::stoul(bs_value);
    } catch (...) {
    }
  }

  std::string count_value = ctx.get<std::string>("count", "");
  if (!count_value.empty()) {
    try {
      block_count = std::stoul(count_value);
    } catch (...) {
    }
  }

  if (input_file.empty() && output_file.empty()) {
    safeErrorPrintLn("dd: missing input and output files");
    return 1;
  }

  HANDLE hIn = INVALID_HANDLE_VALUE;
  HANDLE hOut = INVALID_HANDLE_VALUE;

  if (!input_file.empty()) {
    std::wstring winput = utf8_to_wstring(input_file);
    hIn = CreateFileW(winput.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hIn == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("dd: cannot open input file '" + input_file + "'");
      return 1;
    }
  } else {
    hIn = GetStdHandle(STD_INPUT_HANDLE);
  }

  if (!output_file.empty()) {
    std::wstring woutput = utf8_to_wstring(output_file);
    hOut = CreateFileW(woutput.c_str(), GENERIC_WRITE, 0, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("dd: cannot create output file '" + output_file + "'");
      if (hIn != INVALID_HANDLE_VALUE && hIn != GetStdHandle(STD_INPUT_HANDLE))
        CloseHandle(hIn);
      return 1;
    }
  } else {
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  }

  std::vector<char> buffer(block_size);
  DWORD bytesRead;
  size_t blocks_copied = 0;
  size_t bytes_copied = 0;

  while (ReadFile(hIn, buffer.data(), static_cast<DWORD>(block_size),
                  &bytesRead, nullptr) &&
         bytesRead > 0) {
    DWORD bytesWritten;
    WriteFile(hOut, buffer.data(), bytesRead, &bytesWritten, nullptr);
    blocks_copied++;
    bytes_copied += bytesWritten;

    if (block_count > 0 && blocks_copied >= block_count) {
      break;
    }
  }

  if (hIn != INVALID_HANDLE_VALUE && hIn != GetStdHandle(STD_INPUT_HANDLE))
    CloseHandle(hIn);
  if (hOut != INVALID_HANDLE_VALUE && hOut != GetStdHandle(STD_OUTPUT_HANDLE))
    CloseHandle(hOut);

  safePrintLn(std::to_string(blocks_copied) + " records in");
  safePrintLn(std::to_string(blocks_copied) + " records out");
  safePrintLn(std::to_string(bytes_copied) + " bytes copied");

  return 0;
}
