/// @Author: caomengxuan666
/// @contributors:
///   - contributor1 <email1@example.com>
///   - contributor2 <email2@example.com>
///   - contributor3 <email3@example.com>
///   - description
/// @Description: Console utilities
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
module;

#define WIN32_LEAN_AND_MEAN
#include "pch/pch.h"

export module utils:console;

import std;
import :utf8;
import :i18n;

/// @brief ANSI escape sequences for terminal text coloring.
/// These follow the default GNU `ls --color=auto` scheme and are compatible
/// with modern terminals (Windows Terminal, iTerm2, GNOME Terminal, etc.).
/// Usage: prefix filename with color constant, suffix with COLOR_RESET.
export constexpr auto COLOR_RESET = L"\033[0m";    ///< Reset all attributes
export constexpr auto COLOR_DIR = L"\033[01;34m";  ///< Directories (bold blue)
export constexpr auto COLOR_EXEC =
    L"\033[01;32m";  ///< Executables (bold green)
export constexpr auto COLOR_LINK =
    L"\033[01;36m";                             ///< Symbolic links (bold cyan)
export constexpr auto COLOR_FILE = L"\033[0m";  ///< Regular files (default)
export constexpr auto COLOR_ARCHIVE =
    L"\033[01;31m";  ///< Archives: .zip, .tar, .7z (bold red)
export constexpr auto COLOR_SCRIPT =
    L"\033[01;33m";  ///< Scripts: .ps1, .sh, .py (bold yellow)
export constexpr auto COLOR_SOURCE =
    L"\033[01;36m";  ///< Source code: .cpp, .rs, .ts (bold cyan)
export constexpr auto COLOR_MEDIA =
    L"\033[01;35m";  ///< Media: .jpg, .mp4 (bold magenta)

export constexpr auto ANSI_RESET = "\033[0m";
export constexpr auto ANSI_BOLD = "\033[1m";
export constexpr auto ANSI_DIM = "\033[2m";
export constexpr auto ANSI_UNDERLINE = "\033[4m";

namespace {
thread_local HANDLE g_cached_stdout = INVALID_HANDLE_VALUE;
thread_local HANDLE g_cached_stderr = INVALID_HANDLE_VALUE;
thread_local bool g_handles_valid = false;
thread_local bool g_stdout_pipe_closed = false;
thread_local bool g_stderr_pipe_closed = false;
thread_local DWORD g_stdout_write_error = 0;
thread_local bool g_stdout_is_console = false;
thread_local bool g_stderr_is_console = false;
thread_local bool g_console_checked = false;

// Capture handles (set by daemon when capturing)
HANDLE g_capture_stdout_write = nullptr;
HANDLE g_capture_stderr_write = nullptr;
bool g_capture_active = false;

HANDLE getStdOut() {
  if (g_capture_active && g_capture_stdout_write) {
    return g_capture_stdout_write;
  }

  if (!g_handles_valid) {
    g_cached_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    g_cached_stderr = GetStdHandle(STD_ERROR_HANDLE);
    g_handles_valid = true;
  }
  return g_cached_stdout;
}

HANDLE getStdErr() {
  if (g_capture_active && g_capture_stderr_write) {
    return g_capture_stderr_write;
  }

  if (!g_handles_valid) {
    g_cached_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    g_cached_stderr = GetStdHandle(STD_ERROR_HANDLE);
    g_handles_valid = true;
  }
  return g_cached_stderr;
}

bool isBrokenPipeError(DWORD err) {
  return err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA;
}

// Record a failed write to stdout. A broken pipe maps to GNU's SIGPIPE
// death (the command exits quietly); any other failure — for example
// ERROR_INVALID_HANDLE when the caller closed stdout (`>&-`) — is a fatal
// "write error" the command is expected to report.
void note_stdout_write_failure() {
  const DWORD err = GetLastError();
  if (isBrokenPipeError(err)) {
    g_stdout_pipe_closed = true;
  } else if (err != ERROR_SUCCESS) {
    g_stdout_write_error = err;
  }
}

bool env_var_present(const char* name) {
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
    return false;
  }
  bool present = std::string_view(value).size() > 0;
  std::free(value);
  return present;
}

bool env_var_equals(const char* name, std::string_view expected) {
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
    return false;
  }
  std::string actual(value);
  std::free(value);
  return actual == expected;
}

bool enable_virtual_terminal_processing(HANDLE h) {
  if (h == INVALID_HANDLE_VALUE || h == nullptr) return false;

  DWORD mode = 0;
  if (!GetConsoleMode(h, &mode)) return false;

  if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) return true;

  return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
}  // namespace

// Exported function to set output capture handles
export void set_output_capture_handles(HANDLE stdout_handle,
                                       HANDLE stderr_handle, bool active) {
  g_capture_stdout_write = stdout_handle;
  g_capture_stderr_write = stderr_handle;
  g_capture_active = active;
  // Invalidate cache to force re-evaluation
  g_handles_valid = false;
}

// Exported function to invalidate cached handles (call after SetStdHandle)
export void invalidateCachedHandles() {
  g_handles_valid = false;
  g_console_checked = false;
}

export bool is_stdout_pipe_closed() { return g_stdout_pipe_closed; }

export bool is_stderr_pipe_closed() { return g_stderr_pipe_closed; }

// True once a stdout write failed for a reason other than a closed pipe
// (broken-pipe failures raise is_stdout_pipe_closed() instead). Commands
// that stream output should treat this as fatal: GNU reports a "write
// error" diagnostic and exits non-zero rather than silently dropping data.
export bool is_stdout_write_failed() { return g_stdout_write_error != 0; }

// The Win32 error code recorded by the first failed stdout write; 0 when
// is_stdout_write_failed() is false.
export DWORD stdout_write_error() { return g_stdout_write_error; }

// Byte counter for `ls --dired`: GNU emits "//DIRED//" trailer lines with
// the byte offsets of each filename in the output stream.  Counting is
// opt-in (set_stdout_byte_counting) so the extra UTF-8 length computation
// on the console path is only paid while a listing is running.
thread_local uint64_t g_stdout_bytes_written = 0;
thread_local bool g_count_stdout_bytes = false;

export void set_stdout_byte_counting(bool on) { g_count_stdout_bytes = on; }

export uint64_t stdout_bytes_written() { return g_stdout_bytes_written; }

export void clear_pipe_closed_flags() {
  g_stdout_pipe_closed = false;
  g_stderr_pipe_closed = false;
  g_stdout_write_error = 0;
}

bool isConsoleHandle(HANDLE h) {
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD mode;
  return GetConsoleMode(h, &mode) != 0;
}

// Cached console check - only calls GetConsoleMode once per thread
bool isStdoutConsole() {
  if (!g_console_checked) {
    g_stdout_is_console = isConsoleHandle(getStdOut());
    g_stderr_is_console = isConsoleHandle(getStdErr());
    g_console_checked = true;
  }
  return g_stdout_is_console;
}

bool isStderrConsole() {
  if (!g_console_checked) {
    g_stdout_is_console = isConsoleHandle(getStdOut());
    g_stderr_is_console = isConsoleHandle(getStdErr());
    g_console_checked = true;
  }
  return g_stderr_is_console;
}

export bool writeConsole(const std::wstring_view& wstr) {
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) return false;

  DWORD written;
  return WriteConsoleW(hOut, wstr.data(), static_cast<DWORD>(wstr.size()),
                       &written, nullptr) != 0;
}

export bool writeErrorConsole(const std::wstring_view& wstr) {
  HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
  if (hErr == INVALID_HANDLE_VALUE) return false;

  DWORD written;
  return WriteConsoleW(hErr, wstr.data(), static_cast<DWORD>(wstr.size()),
                       &written, nullptr) != 0;
}

export bool isOutputConsole() { return isConsoleHandle(getStdOut()); }

export bool isErrorConsole() { return isConsoleHandle(getStdErr()); }

export bool isInputConsole() {
  return isConsoleHandle(GetStdHandle(STD_INPUT_HANDLE));
}

export std::pair<int, int> getConsoleViewportSize() {
  if (!isOutputConsole()) return {80, 24};

  CONSOLE_SCREEN_BUFFER_INFO csbi{};
  HANDLE hConsole = getStdOut();
  if (hConsole == INVALID_HANDLE_VALUE ||
      !GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return {80, 24};
  }

  int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  return {std::max(width, 1), std::max(height, 2)};
}

export void clearConsoleViewport() {
  HANDLE hConsole = getStdOut();
  if (hConsole == INVALID_HANDLE_VALUE || hConsole == nullptr) return;

  CONSOLE_SCREEN_BUFFER_INFO csbi{};
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return;

  const SHORT left = csbi.srWindow.Left;
  const SHORT top = csbi.srWindow.Top;
  const SHORT width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  const SHORT height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  DWORD written = 0;

  for (SHORT row = 0; row < height; ++row) {
    COORD start{left, static_cast<SHORT>(top + row)};
    FillConsoleOutputCharacterW(hConsole, L' ', static_cast<DWORD>(width),
                                start, &written);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes,
                               static_cast<DWORD>(width), start, &written);
  }

  SetConsoleCursorPosition(hConsole, {left, top});
}

export std::string ansiFg256(int color) {
  color = std::clamp(color, 0, 255);
  return "\033[38;5;" + std::to_string(color) + "m";
}

export std::string ansiBg256(int color) {
  color = std::clamp(color, 0, 255);
  return "\033[48;5;" + std::to_string(color) + "m";
}

export std::string ansiFgRgb(int red, int green, int blue) {
  red = std::clamp(red, 0, 255);
  green = std::clamp(green, 0, 255);
  blue = std::clamp(blue, 0, 255);
  return "\033[38;2;" + std::to_string(red) + ";" + std::to_string(green) +
         ";" + std::to_string(blue) + "m";
}

export std::string ansiBgRgb(int red, int green, int blue) {
  red = std::clamp(red, 0, 255);
  green = std::clamp(green, 0, 255);
  blue = std::clamp(blue, 0, 255);
  return "\033[48;2;" + std::to_string(red) + ";" + std::to_string(green) +
         ";" + std::to_string(blue) + "m";
}

export bool enableAnsiColorStdout() {
  return enable_virtual_terminal_processing(getStdOut());
}

export bool enableAnsiColorStderr() {
  return enable_virtual_terminal_processing(getStdErr());
}

export bool shouldUseAnsiColorStdout() {
  if (!isOutputConsole()) return false;
  if (env_var_present("NO_COLOR")) return false;
  if (env_var_equals("TERM", "dumb")) return false;
  return enableAnsiColorStdout();
}

export bool shouldUseAnsiColorStderr() {
  if (!isErrorConsole()) return false;
  if (env_var_present("NO_COLOR")) return false;
  if (env_var_equals("TERM", "dumb")) return false;
  return enableAnsiColorStderr();
}

export std::string colorizeStdout(std::string_view text,
                                  std::string_view style) {
  if (!shouldUseAnsiColorStdout() || text.empty() || style.empty()) {
    return std::string(text);
  }

  std::string result;
  result.reserve(style.size() + text.size() +
                 std::string_view(ANSI_RESET).size());
  result.append(style);
  result.append(text);
  result.append(ANSI_RESET);
  return result;
}

/**
 * @brief Check if terminal supports color
 * @return true if terminal supports color, false otherwise
 */
export bool isTerminalSupportsColor() {
  // No color in pipe/file
  if (!isOutputConsole()) {
    return false;
  }

  return enableAnsiColorStdout();
}

/**
 * @brief Get terminal width
 * @return Terminal width in columns
 */
export int getTerminalWidth() { return getConsoleViewportSize().first; }

/**
 * @brief Smart set console mode (only enable wide char for console)
 * @return true if successful, false on error
 */
export bool setupConsoleForUnicode() {
  // Only set wide char mode for console (not pipe/file)
  if (!isOutputConsole()) {
    // Pipe/file: set UTF-8 code page
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    return true;
  }

  // Console: enable wide char mode for Chinese
  bool success = true;
  if (_setmode(_fileno(stdout), _O_U16TEXT) == -1) {
    success = false;
  }

  if (_setmode(_fileno(stderr), _O_U16TEXT) == -1) {
    success = false;
  }

  // Enable ANSI color support for Windows Terminal/CMD
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hConsole != INVALID_HANDLE_VALUE) {
    DWORD consoleMode;
    if (GetConsoleMode(hConsole, &consoleMode)) {
      SetConsoleMode(hConsole,
                     consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
  }

  return success;
}

// ============================================================================
// Low level write primitives - NO fprintf/wprintf
// ============================================================================
namespace detail {
bool writeConsoleW(HANDLE h, const wchar_t* data, size_t len) {
  if (!data || len == 0) return true;
  DWORD written;
  return WriteConsoleW(h, data, static_cast<DWORD>(len), &written, nullptr) !=
         0;
}

bool writeFile(HANDLE h, const char* data, size_t len) {
  if (!data || len == 0) return true;
  DWORD written;
  return WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr) != 0;
}
}  // namespace detail

// ============================================================================
// Stack allocated conversion buffer (zero heap allocation)
// ============================================================================
namespace detail {
template <size_t StackSize = 256>
class wchar_buffer {
  wchar_t stack_[StackSize];
  wchar_t* data_;
  size_t size_;

 public:
  explicit wchar_buffer(std::string_view sv) {
    int len = MultiByteToWideChar(CP_UTF8, 0, sv.data(),
                                  static_cast<int>(sv.size()), nullptr, 0);
    if (len <= 0) {
      data_ = nullptr;
      size_ = 0;
      return;
    }

    if (static_cast<size_t>(len) <= StackSize) {
      data_ = stack_;
    } else {
      data_ = static_cast<wchar_t*>(
          HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t)));
    }

    size_ = static_cast<size_t>(len);
    MultiByteToWideChar(CP_UTF8, 0, sv.data(), static_cast<int>(sv.size()),
                        data_, len);
  }

  ~wchar_buffer() {
    if (data_ && data_ != stack_) {
      HeapFree(GetProcessHeap(), 0, data_);
    }
  }

  wchar_buffer(const wchar_buffer&) = delete;
  wchar_buffer& operator=(const wchar_buffer&) = delete;

  const wchar_t* data() const { return data_; }
  size_t size() const { return size_; }
  bool valid() const { return data_ != nullptr; }
};
}  // namespace detail

// ============================================================================
// Core output functions - ALL PATHS use WriteFile/WriteConsoleW, NO fprintf
// ============================================================================

// ----------------------------------------------------------------------------
// Wide string overloads (zero conversion)
// ----------------------------------------------------------------------------
export void safePrint(std::wstring_view wsv) {
  std::string utf8 = wstring_to_utf8(wsv);
  // L"..." literals are cataloged by the same legacy-key scheme; narrow the
  // text, look it up, and emit the translation when one exists.
  const auto translated = winux::i18n::translate_legacy(utf8);
  HANDLE h = getStdOut();
  if (isStdoutConsole()) {
    if (translated != utf8) {
      const std::wstring wide = utf8_to_wstring(translated);
      if (!detail::writeConsoleW(h, wide.data(), wide.size()) &&
          isBrokenPipeError(GetLastError())) {
        g_stdout_pipe_closed = true;
      }
    } else if (!detail::writeConsoleW(h, wsv.data(), wsv.size()) &&
               isBrokenPipeError(GetLastError())) {
      g_stdout_pipe_closed = true;
    }
    if (g_count_stdout_bytes) {
      g_stdout_bytes_written += translated.size();
    }
  } else {
    if (!detail::writeFile(h, translated.data(), translated.size()) &&
        isBrokenPipeError(GetLastError())) {
      g_stdout_pipe_closed = true;
    }
    if (g_count_stdout_bytes) {
      g_stdout_bytes_written += translated.size();
    }
  }
}

export void safeErrorPrint(std::wstring_view wsv) {
  std::string utf8 = wstring_to_utf8(wsv);
  const auto translated = winux::i18n::translate_legacy(utf8);
  HANDLE h = getStdErr();
  if (isStderrConsole()) {
    if (translated != utf8) {
      const std::wstring wide = utf8_to_wstring(translated);
      if (!detail::writeConsoleW(h, wide.data(), wide.size()) &&
          isBrokenPipeError(GetLastError())) {
        g_stderr_pipe_closed = true;
      }
    } else if (!detail::writeConsoleW(h, wsv.data(), wsv.size()) &&
               isBrokenPipeError(GetLastError())) {
      g_stderr_pipe_closed = true;
    }
  } else {
    if (!detail::writeFile(h, translated.data(), translated.size()) &&
        isBrokenPipeError(GetLastError())) {
      g_stderr_pipe_closed = true;
    }
  }
}

export void safePrintLn(std::wstring_view wsv) {
  safePrint(wsv);
  safePrint(L"\n");
}

export void safeErrorPrintLn(std::wstring_view wsv) {
  safeErrorPrint(wsv);
  safeErrorPrint(L"\n");
}

// ----------------------------------------------------------------------------
// UTF-8 string overloads (stack conversion)
// ----------------------------------------------------------------------------
export void safePrint(std::string_view sv) {
  const auto translated = winux::i18n::translate_legacy(sv);
  sv = translated;
  HANDLE h = getStdOut();
  if (isStdoutConsole()) {
    detail::wchar_buffer buf(sv);
    if (buf.valid()) {
      if (!detail::writeConsoleW(h, buf.data(), buf.size()) &&
          isBrokenPipeError(GetLastError())) {
        g_stdout_pipe_closed = true;
      }
    }
  } else {
    if (!detail::writeFile(h, sv.data(), sv.size()) &&
        isBrokenPipeError(GetLastError())) {
      g_stdout_pipe_closed = true;
    }
  }
  if (g_count_stdout_bytes) {
    g_stdout_bytes_written += sv.size();
  }
}

export void safeErrorPrint(std::string_view sv) {
  const auto translated = winux::i18n::translate_legacy(sv);
  sv = translated;
  HANDLE h = getStdErr();
  if (isStderrConsole()) {
    detail::wchar_buffer buf(sv);
    if (buf.valid()) {
      if (!detail::writeConsoleW(h, buf.data(), buf.size()) &&
          isBrokenPipeError(GetLastError())) {
        g_stderr_pipe_closed = true;
      }
    }
  } else {
    if (!detail::writeFile(h, sv.data(), sv.size()) &&
        isBrokenPipeError(GetLastError())) {
      g_stderr_pipe_closed = true;
    }
  }
}

export void safePrintLn(std::string_view sv) {
  safePrint(winux::i18n::translate_legacy(sv));
  safePrint("\n");
}

export void safeErrorPrintLn(std::string_view sv) {
  safeErrorPrint(winux::i18n::translate_help_hint(sv));
  safeErrorPrint("\n");
}

// ----------------------------------------------------------------------------
// C string overloads (zero copy)
// ----------------------------------------------------------------------------
export void safePrint(const char* str) {
  if (str) safePrint(std::string_view(str));
}

export void safeErrorPrint(const char* str) {
  if (str) safeErrorPrint(std::string_view(str));
}

export void safePrintLn(const char* str) {
  if (str)
    safePrint(std::string_view(str));
  else
    safePrint("\n");
  safePrint("\n");
}

export void safeErrorPrintLn(const char* str) {
  if (str)
    safeErrorPrint(std::string_view(str));
  else
    safeErrorPrint("\n");
  safeErrorPrint("\n");
}

export void safePrint(const wchar_t* wstr) {
  if (!wstr) return;
  safePrint(std::wstring_view(wstr));
}

export void safeErrorPrint(const wchar_t* wstr) {
  if (!wstr) return;
  safeErrorPrint(std::wstring_view(wstr));
}

export void safePrintLn(const wchar_t* wstr) {
  safePrint(wstr);
  safePrint(L"\n");
}

export void safeErrorPrintLn(const wchar_t* wstr) {
  safeErrorPrint(wstr);
  safeErrorPrint(L"\n");
}
// ----------------------------------------------------------------------------
// Character overloads (single char)
// ----------------------------------------------------------------------------
export void safePrint(char c) {
  HANDLE h = getStdOut();
  if (isStdoutConsole()) {
    wchar_t wc = static_cast<wchar_t>(c);
    detail::writeConsoleW(h, &wc, 1);
  } else {
    detail::writeFile(h, &c, 1);
  }
}

export void safeErrorPrint(char c) {
  HANDLE h = getStdErr();
  if (isStderrConsole()) {
    wchar_t wc = static_cast<wchar_t>(c);
    detail::writeConsoleW(h, &wc, 1);
  } else {
    detail::writeFile(h, &c, 1);
  }
}

export void safePrintLn(char c) {
  safePrint(c);
  safePrint("\n");
}

export void safeErrorPrintLn(char c) {
  safeErrorPrint(c);
  safeErrorPrint("\n");
}

// ----------------------------------------------------------------------------
// Integer overloads (stack formatting)
// ----------------------------------------------------------------------------
export void safePrint(int n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", n);
  safePrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safeErrorPrint(int n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", n);
  safeErrorPrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safePrint(unsigned int n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%u", n);
  safePrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safeErrorPrint(unsigned int n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%u", n);
  safeErrorPrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safePrint(long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%ld", n);
  safePrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safeErrorPrint(long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%ld", n);
  safeErrorPrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safePrint(unsigned long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%lu", n);
  safePrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safeErrorPrint(unsigned long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%lu", n);
  safeErrorPrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safePrint(long long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%lld", n);
  safePrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safeErrorPrint(long long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%lld", n);
  safeErrorPrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safePrint(unsigned long long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%llu", n);
  safePrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safeErrorPrint(unsigned long long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%llu", n);
  safeErrorPrint(std::string_view(buf, static_cast<size_t>(len)));
}

// ----------------------------------------------------------------------------
// Pointer overload
// ----------------------------------------------------------------------------
export void safePrint(const void* ptr) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%p", ptr);
  safePrint(std::string_view(buf, static_cast<size_t>(len)));
}

export void safeErrorPrint(const void* ptr) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%p", ptr);
  safeErrorPrint(std::string_view(buf, static_cast<size_t>(len)));
}

// ----------------------------------------------------------------------------
// Boolean overload
// ----------------------------------------------------------------------------
export void safePrint(bool b) { safePrint(b ? "true" : "false"); }

export void safeErrorPrint(bool b) { safeErrorPrint(b ? "true" : "false"); }

/// @brief Calculate display width of a wide string in terminal columns.
/// Handles Unicode fullwidth/halfwidth characters (e.g., Chinese = 2 cols).
/// Returns number of *columns*, not code points.
/// @brief Calculate the terminal display width of a wide string.
/// Fullwidth characters (e.g., Chinese, Japanese) count as 2 columns.
/// Halfwidth characters (e.g., ASCII) count as 1 column.
export size_t string_display_width(const std::wstring& s) {
  size_t width = 0;
  for (wchar_t c : s) {
    // Check for fullwidth Unicode ranges
    if ((c >= 0x3000 && c <= 0x303F) ||    // CJK Symbols and Punctuation
        (c >= 0xFF00 && c <= 0xFFEF) ||    // Fullwidth forms
        (c >= 0x4E00 && c <= 0x9FFF) ||    // CJK Unified Ideographs
        (c >= 0x3400 && c <= 0x4DBF) ||    // CJK Extension A
        (c >= 0x20000 && c <= 0x2A6DF) ||  // CJK Extension B
        (c >= 0x2A700 && c <= 0x2B73F) ||  // CJK Extension C
        (c >= 0x2B740 && c <= 0x2B81F) ||  // CJK Extension D
        (c >= 0x2B820 && c <= 0x2CEAF) ||  // CJK Extension E/F/G
        (c >= 0xAC00 && c <= 0xD7AF) ||    // Hangul Syllables
        (c >= 0x1100 && c <= 0x11FF)) {
      // Hangul Jamo
      width += 2;
    } else {
      width += 1;
    }
  }
  return width;
}
