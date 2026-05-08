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
// Parse buffer size string
size_t parse_buffer_size(const std::string& str) {
  if (str == "0" || str == "L" || str == "0L") {
    return 0;  // Unbuffered
  }
  if (str == "1" || str == "line" || str == "1L") {
    return 1;  // Line buffered
  }

  try {
    size_t multiplier = 1;
    std::string num_str = str;

    // Check for suffix
    if (!num_str.empty()) {
      char suffix = toupper(num_str.back());
      if (suffix == 'K') {
        multiplier = 1024;
        num_str.pop_back();
      } else if (suffix == 'M') {
        multiplier = 1024 * 1024;
        num_str.pop_back();
      } else if (suffix == 'G') {
        multiplier = 1024LL * 1024 * 1024;
        num_str.pop_back();
      } else if (suffix == 'B') {
        num_str.pop_back();
      }
    }

    size_t size = static_cast<size_t>(std::stoull(num_str));
    return size * multiplier;
  } catch (...) {
    return 4096;  // Default buffer size
  }
}

// Set buffering mode for a stream
void set_stream_buffering(FILE* stream, const std::string& mode) {
  if (mode.empty() || mode == "0" || mode == "L" || mode == "0L") {
    setvbuf(stream, nullptr, _IONBF, 0);  // Unbuffered
  } else if (mode == "1" || mode == "line" || mode == "1L") {
    setvbuf(stream, nullptr, _IOLBF, 0);  // Line buffered
  } else {
    size_t size = parse_buffer_size(mode);
    if (size == 0) {
      setvbuf(stream, nullptr, _IONBF, 0);
    } else {
      setvbuf(stream, nullptr, _IOFBF, size);  // Fully buffered
    }
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

  // Parse buffering modes
  std::string parsed_input = !input_mode.empty() ? input_mode : "4096";
  std::string parsed_output = !output_mode.empty() ? output_mode : "4096";
  std::string parsed_error = !error_mode.empty() ? error_mode : "4096";

  // Build command string
  std::string cmd;
  for (size_t i = 0; i < ctx.positionals.size(); ++i) {
    if (i > 0) cmd += " ";
    cmd += ctx.positionals[i];
  }

  // For Windows, we'll just execute the command directly
  // and set the parent process buffering
  if (!output_mode.empty()) {
    set_stream_buffering(stdout, output_mode);
  }
  if (!error_mode.empty()) {
    set_stream_buffering(stderr, error_mode);
  }

  // Execute command
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;

  std::wstring wcmd = utf8_to_wstring(cmd);

  if (!CreateProcessW(nullptr, const_cast<wchar_t*>(wcmd.c_str()), nullptr,
                      nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
    safeErrorPrintLn("stdbuf: failed to execute command: " +
                     std::string(ctx.positionals[0]));
    return 1;
  }

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return static_cast<int>(exit_code);
}
