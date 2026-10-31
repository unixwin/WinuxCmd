// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU] -i, --input
    std::array{OPTION("-i", "--input", "adjust standard input stream buffering",
                      STRING_TYPE),
               // [GNU] -o, --output
               OPTION("-o", "--output",
                      "adjust standard output stream buffering", STRING_TYPE),
               // [GNU] -e, --error
               OPTION("-e", "--error", "adjust standard error stream buffering",
                      STRING_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
constexpr int kStdbufUsageErrorExitCode = 125;

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
      BufferSizeSuffix{"k", 1024, 1},
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

  // [GNU] stdbuf.c:64-79 (parse_size via xstrtoumax): a size must
  // start with at least one digit; a bare suffix ('K') is invalid,
  // not an implied 1.
  if (digits == 0) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  {
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
  if (mode == "L") return true;
  // [GNU] any size that parses is valid, including 0 in any spelling
  // ('0', '00', '0K'): it selects no buffering.
  auto size = parse_buffer_size(mode);
  return size.has_value();
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
    // MSVC's CRT does not implement POSIX line buffering reliably for these
    // streams and may abort through the invalid-parameter handler. Keep L as
    // an accepted child-inheritance marker and leave the parent stream alone.
    return true;
  }

  auto size = parse_buffer_size(mode);
  if (!size.has_value()) {
    return false;
  }
  if (*size == 0) {
    setvbuf(stream, nullptr, _IONBF, 0);  // Unbuffered ('00', '0K', ...)
    return true;
  }

  setvbuf(stream, nullptr, _IOFBF, *size);  // Fully buffered
  return true;
}

auto stdbuf_command_status_from_create_error(DWORD error) -> int {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}

auto stdbuf_windows_error_text(DWORD error) -> std::string {
  return win32_posix_error_text(error);
}

auto set_child_buffer_mode(const char* name, const std::string& mode)
    -> std::optional<std::string> {
  const char* old = std::getenv(name);
  std::optional<std::string> previous;
  if (old != nullptr) previous = old;
  _putenv_s(name, mode.c_str());
  return previous;
}

auto restore_child_buffer_mode(const char* name,
                               const std::optional<std::string>& previous)
    -> void {
  _putenv_s(name, previous.has_value() ? previous->c_str() : "");
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
  const bool input_given = ctx.has("-i") || ctx.has("--input");
  const bool output_given = ctx.has("-o") || ctx.has("--output");
  const bool error_given = ctx.has("-e") || ctx.has("--error");
  std::string input_mode = ctx.get<std::string>("-i", "");
  if (input_mode.empty()) input_mode = ctx.get<std::string>("--input", "");
  std::string output_mode = ctx.get<std::string>("-o", "");
  if (output_mode.empty()) output_mode = ctx.get<std::string>("--output", "");
  std::string error_mode = ctx.get<std::string>("-e", "");
  if (error_mode.empty()) error_mode = ctx.get<std::string>("--error", "");

  // [GNU] stdbuf.c:335-336: leading whitespace of a MODE is skipped
  // before the value is examined (so '-i " L"' is the L refusal and
  // '-o " 0"' is size 0).
  auto strip_leading_whitespace = [](std::string& mode) {
    size_t pos = mode.find_first_not_of(" \t\n\v\f\r");
    if (pos == std::string::npos) {
      mode.clear();
    } else if (pos > 0) {
      mode.erase(0, pos);
    }
  };
  strip_leading_whitespace(input_mode);
  strip_leading_whitespace(output_mode);
  strip_leading_whitespace(error_mode);

  // [GNU] stdbuf.c: the buffering MODE operand is validated while parsing
  // options, so an invalid mode is diagnosed even when COMMAND is missing.
  // An explicitly empty MODE ("stdbuf -i ''") is invalid too.
  auto invalid_mode = [](const std::string& mode) {
    safeErrorPrintLn("stdbuf: " +
                     winux::i18n::format("command.stdbuf.error.invalid_mode",
                                         "invalid mode '{}'", mode));
  };
  // [GNU] stdbuf.c:340-346: an input MODE starting with 'L' is refused
  // while the option is parsed, before anything else is diagnosed.
  if (input_given && !input_mode.empty() && input_mode.front() == 'L') {
    safeErrorPrintLn("stdbuf: line buffering stdin is meaningless");
    safeErrorPrintLn(winux::i18n::format(
        "common.try_help", "Try '{} --help' for more information.", "stdbuf"));
    return kStdbufUsageErrorExitCode;
  }
  if ((input_given && input_mode.empty()) ||
      !validate_buffer_mode("standard input", input_mode)) {
    invalid_mode(input_mode);
    return kStdbufUsageErrorExitCode;
  }
  if ((output_given && output_mode.empty()) ||
      !validate_buffer_mode("standard output", output_mode)) {
    invalid_mode(output_mode);
    return kStdbufUsageErrorExitCode;
  }
  if ((error_given && error_mode.empty()) ||
      !validate_buffer_mode("standard error", error_mode)) {
    invalid_mode(error_mode);
    return kStdbufUsageErrorExitCode;
  }

  if (ctx.positionals.empty()) {
    // [GNU] stdbuf.c: "missing operand" when COMMAND is absent.
    safeErrorPrintLn("stdbuf: " +
                     winux::i18n::translate("common.error.missing_operand",
                                            "missing operand"));
    safeErrorPrintLn(winux::i18n::format(
        "common.try_help", "Try '{} --help' for more information.", "stdbuf"));
    return kStdbufUsageErrorExitCode;
  }

  // [GNU] stdbuf.c refuses to run COMMAND when no -i/-o/-e buffering mode
  // option was given at all (checked after the missing-command diagnostic).
  if (!input_given && !output_given && !error_given) {
    safeErrorPrintLn(
        "stdbuf: " +
        winux::i18n::format("command.stdbuf.error.mode_required",
                            "you must specify a buffering mode option"));
    safeErrorPrintLn(winux::i18n::format(
        "common.try_help", "Try '{} --help' for more information.", "stdbuf"));
    return kStdbufUsageErrorExitCode;
  }

  // For Windows, we'll just execute the command directly
  // and set the parent process buffering
  if (input_mode != "L" && !set_stream_buffering(stdin, input_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard input: " + input_mode);
    return kStdbufUsageErrorExitCode;
  }
  if (!set_stream_buffering(stdout, output_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard output: " +
                     output_mode);
    return kStdbufUsageErrorExitCode;
  }
  if (!set_stream_buffering(stderr, error_mode)) {
    safeErrorPrintLn("stdbuf: invalid mode for standard error: " + error_mode);
    return kStdbufUsageErrorExitCode;
  }

  // WinuxCmd children apply these inherited settings at their common entry
  // point. Store resolved byte sizes so the child does not duplicate the
  // suffix parser; line mode remains the explicit L marker.
  auto resolve_mode = [](const std::string& mode) {
    if (mode == "L" || mode == "0" || mode.empty()) return mode;
    return std::to_string(*parse_buffer_size(mode));
  };
  auto old_i =
      set_child_buffer_mode("WINUX_STDBUF_I", resolve_mode(input_mode));
  auto old_o =
      set_child_buffer_mode("WINUX_STDBUF_O", resolve_mode(output_mode));
  auto old_e =
      set_child_buffer_mode("WINUX_STDBUF_E", resolve_mode(error_mode));

  // [GNU] stdbuf.c:271-300: the documented contract a child reads is
  // _STDBUF_I/_STDBUF_O/_STDBUF_E ('L' or a decimal byte size).
  // Publish those names alongside the internal WINUX_* ones so a
  // child implementing the GNU contract sees its modes; like GNU,
  // only streams that were given a mode are touched.
  std::optional<std::optional<std::string>> old_gnu_i, old_gnu_o, old_gnu_e;
  if (!input_mode.empty()) {
    old_gnu_i = set_child_buffer_mode("_STDBUF_I", resolve_mode(input_mode));
  }
  if (!output_mode.empty()) {
    old_gnu_o = set_child_buffer_mode("_STDBUF_O", resolve_mode(output_mode));
  }
  if (!error_mode.empty()) {
    old_gnu_e = set_child_buffer_mode("_STDBUF_E", resolve_mode(error_mode));
  }

  auto restore_all = [&]() {
    restore_child_buffer_mode("WINUX_STDBUF_I", old_i);
    restore_child_buffer_mode("WINUX_STDBUF_O", old_o);
    restore_child_buffer_mode("WINUX_STDBUF_E", old_e);
    if (old_gnu_i) restore_child_buffer_mode("_STDBUF_I", *old_gnu_i);
    if (old_gnu_o) restore_child_buffer_mode("_STDBUF_O", *old_gnu_o);
    if (old_gnu_e) restore_child_buffer_mode("_STDBUF_E", *old_gnu_e);
  };

  // Execute command
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  // Execute command. The child is resolved with GNU execvp semantics
  // (PATH-only; no application-directory, cwd, or System32 fallback) so a
  // name colliding with a Windows image resolves from the caller's PATH.
  winux::childproc::ChildLaunchRequest stdbuf_launch_request;
  stdbuf_launch_request.command = std::string(ctx.positionals[0]);
  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    stdbuf_launch_request.args.emplace_back(ctx.positionals[i]);
  }
  auto launch = winux::childproc::plan_child_launch(stdbuf_launch_request);
  if (launch.status != winux::childproc::ChildResolveStatus::Found) {
    restore_all();
    const DWORD resolve_error =
        winux::childproc::child_status_to_win32_error(launch.status);
    safeErrorPrintLn("stdbuf: failed to run command '" +
                     std::string(ctx.positionals[0]) +
                     "': " + stdbuf_windows_error_text(resolve_error));
    return stdbuf_command_status_from_create_error(resolve_error);
  }

  if (!CreateProcessW(launch.application_name->c_str(),
                      launch.command_line.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, nullptr, &si, &pi)) {
    restore_all();
    DWORD error = GetLastError();
    safeErrorPrintLn("stdbuf: failed to run command '" +
                     std::string(ctx.positionals[0]) +
                     "': " + stdbuf_windows_error_text(error));
    return stdbuf_command_status_from_create_error(error);
  }

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  restore_all();

  return static_cast<int>(exit_code);
}
