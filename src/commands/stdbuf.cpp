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
 *  - File: stdbuf.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for stdbuf command.
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

auto constexpr STDBUF_OPTIONS =
    std::array{OPTION("-i", "--input", "adjust standard input stream buffering",
                      STRING_TYPE),
               OPTION("-o", "--output",
                      "adjust standard output stream buffering", STRING_TYPE),
               OPTION("-e", "--error", "adjust standard error stream buffering",
                      STRING_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
struct BufferSizeSuffix {
  std::string_view suffix;
  std::size_t base;
  unsigned power;
};

auto checked_pow(std::size_t base, unsigned power) -> std::optional<size_t> {
  std::size_t result = 1;
  for (unsigned i = 0; i < power; ++i) {
    if (result > std::numeric_limits<std::size_t>::max() / base) {
      return std::nullopt;
    }
    result *= base;
  }
  return result;
}

auto parse_buffer_suffix(std::string_view suffix) -> std::optional<size_t> {
  static constexpr std::array suffixes{
      BufferSizeSuffix{"", 1, 0},       BufferSizeSuffix{"K", 1024, 1},
      BufferSizeSuffix{"KB", 1000, 1},  BufferSizeSuffix{"kB", 1000, 1},
      BufferSizeSuffix{"KiB", 1024, 1}, BufferSizeSuffix{"M", 1024, 2},
      BufferSizeSuffix{"MB", 1000, 2},  BufferSizeSuffix{"MiB", 1024, 2},
      BufferSizeSuffix{"G", 1024, 3},   BufferSizeSuffix{"GB", 1000, 3},
      BufferSizeSuffix{"GiB", 1024, 3}, BufferSizeSuffix{"T", 1024, 4},
      BufferSizeSuffix{"TB", 1000, 4},  BufferSizeSuffix{"TiB", 1024, 4},
      BufferSizeSuffix{"P", 1024, 5},   BufferSizeSuffix{"PB", 1000, 5},
      BufferSizeSuffix{"PiB", 1024, 5}, BufferSizeSuffix{"E", 1024, 6},
      BufferSizeSuffix{"EB", 1000, 6},  BufferSizeSuffix{"EiB", 1024, 6},
      BufferSizeSuffix{"Z", 1024, 7},   BufferSizeSuffix{"ZB", 1000, 7},
      BufferSizeSuffix{"ZiB", 1024, 7}, BufferSizeSuffix{"Y", 1024, 8},
      BufferSizeSuffix{"YB", 1000, 8},  BufferSizeSuffix{"YiB", 1024, 8},
      BufferSizeSuffix{"R", 1024, 9},   BufferSizeSuffix{"RB", 1000, 9},
      BufferSizeSuffix{"RiB", 1024, 9}, BufferSizeSuffix{"Q", 1024, 10},
      BufferSizeSuffix{"QB", 1000, 10}, BufferSizeSuffix{"QiB", 1024, 10}};

  for (const auto& entry : suffixes) {
    if (entry.suffix != suffix) continue;
    return checked_pow(entry.base, entry.power);
  }
  return std::nullopt;
}

// Parse buffer size string
auto parse_buffer_size(const std::string& str) -> std::optional<size_t> {
  if (str.empty()) return std::nullopt;

  size_t digits = 0;
  while (digits < str.size() &&
         std::isdigit(static_cast<unsigned char>(str[digits])) != 0) {
    ++digits;
  }

  std::uint64_t value = 1;
  if (digits > 0) {
    value = 0;
    auto number = std::string_view(str).substr(0, digits);
    auto [ptr, ec] =
        std::from_chars(number.data(), number.data() + number.size(), value);
    if (ec != std::errc() || ptr != number.data() + number.size()) {
      return std::nullopt;
    }
  }

  std::string suffix = str.substr(digits);
  auto multiplier = parse_buffer_suffix(suffix);
  if (!multiplier.has_value()) {
    return std::nullopt;
  }

  if (value > std::numeric_limits<size_t>::max() / *multiplier) {
    return std::nullopt;
  }

  return static_cast<size_t>(value * *multiplier);
}

auto validate_buffer_mode(std::string_view stream_name, const std::string& mode)
    -> bool {
  if (mode.empty()) return true;
  if (mode == "0") return true;
  if (mode == "L") return stream_name != "standard input";
  auto size = parse_buffer_size(mode);
  return size.has_value() && *size > 0;
}

// Set buffering mode for a stream
auto set_stream_buffering(FILE* stream, const std::string& mode) -> bool {
  if (mode.empty()) {
    return true;
  }
  if (mode == "0") {
    setvbuf(stream, nullptr, _IONBF, 0);  // Unbuffered
    return true;
  }
  if (mode == "L") {
    setvbuf(stream, nullptr, _IOLBF, 0);  // Line buffered
    return true;
  }

  auto size = parse_buffer_size(mode);
  if (!size.has_value() || *size == 0) {
    return false;
  }

  setvbuf(stream, nullptr, _IOFBF, *size);  // Fully buffered
  return true;
}

auto command_status_from_create_error(DWORD error) -> int {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    stdbuf,
    /* cmd_name */ "stdbuf",
    /* cmd_synopsis */ "stdbuf [OPTION]... COMMAND [ARG]...",
    /* cmd_desc */
    "Run COMMAND, with modified buffering operations for its standard streams.",
    /* examples */
    "  stdbuf -oL grep pattern file.txt\n"
    "  stdbuf -i0 -o0 cat file.txt\n"
    "  stdbuf -o 1M tail -f logfile\n"
    "  stdbuf -e 0 make",
    /* see_also */ "unbuffer, buffer, setvbuf",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ STDBUF_OPTIONS) {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("stdbuf: missing command");
    safePrintLn("Try 'stdbuf --help' for more information.");
    return 1;
  }

  std::string input_mode = ctx.get<std::string>("-i", "");
  std::string output_mode = ctx.get<std::string>("-o", "");
  std::string error_mode = ctx.get<std::string>("-e", "");

  if (!validate_buffer_mode("standard input", input_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard input: " + input_mode);
    return 1;
  }
  if (!validate_buffer_mode("standard output", output_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard output: " +
                     output_mode);
    return 1;
  }
  if (!validate_buffer_mode("standard error", error_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard error: " + error_mode);
    return 1;
  }

  // Build command string
  std::string cmd;
  for (size_t i = 0; i < ctx.positionals.size(); ++i) {
    if (i > 0) cmd += " ";
    cmd += ctx.positionals[i];
  }

  // For Windows, we'll just execute the command directly
  // and set the parent process buffering
  if (!set_stream_buffering(stdin, input_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard input: " + input_mode);
    return 1;
  }
  if (!set_stream_buffering(stdout, output_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard output: " +
                     output_mode);
    return 1;
  }
  if (!set_stream_buffering(stderr, error_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard error: " + error_mode);
    return 1;
  }

  // Execute command
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;

  std::wstring wcmd = utf8_to_wstring(cmd);

  if (!CreateProcessW(nullptr, const_cast<wchar_t*>(wcmd.c_str()), nullptr,
                      nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
    DWORD error = GetLastError();
    safeErrorPrintLn("stdbuf: failed to execute command: " +
                     std::string(ctx.positionals[0]));
    return command_status_from_create_error(error);
  }

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return static_cast<int>(exit_code);
}
