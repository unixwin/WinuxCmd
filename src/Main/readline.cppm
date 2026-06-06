/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: readline.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
module;
#include "pch/pch.h"
export module readline;
import std;
import utils;

// ──────────────────────────────────────────────────────────────────────────────
// Internal rendering helpers
// ──────────────────────────────────────────────────────────────────────────────
namespace {

struct RenderState {
  COORD input_start{0, 0};  // cursor position right after the prompt
};

// Move to the previous UTF-8 code-point boundary.
size_t prevUtf8Pos(const std::string& s, size_t pos) noexcept {
  if (pos == 0) return 0;
  size_t i = pos - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return i;
}

// Move to the next UTF-8 code-point boundary.
size_t nextUtf8Pos(const std::string& s, size_t pos) noexcept {
  if (pos >= s.size()) return s.size();
  size_t i = pos + 1;
  while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
}

std::optional<std::string> readClipboardUtf8SingleLine() noexcept {
  if (!OpenClipboard(nullptr)) return std::nullopt;
  std::optional<std::string> result;
  HANDLE hText = GetClipboardData(CF_UNICODETEXT);
  if (hText != nullptr) {
    auto* text = static_cast<const wchar_t*>(GlobalLock(hText));
    if (text != nullptr) {
      std::wstring normalized(text);
      std::ranges::replace(normalized, L'\r', L' ');
      std::ranges::replace(normalized, L'\n', L' ');
      result = wstring_to_utf8(normalized);
      GlobalUnlock(hText);
    }
  }
  CloseClipboard();
  return result;
}

// Move cursor safely (clamp to buffer extent).
void moveTo(HANDLE hOut, SHORT x, SHORT y) noexcept {
  SetConsoleCursorPosition(hOut, {x, y});
}

// Erase `count` characters starting at (x, y) using the default attribute.
void eraseChars(HANDLE hOut, COORD pos, DWORD count, WORD attr) noexcept {
  DWORD written = 0;
  FillConsoleOutputCharacterW(hOut, L' ', count, pos, &written);
  FillConsoleOutputAttribute(hOut, attr, count, pos, &written);
}

// Redraw the input line.
void redraw(HANDLE hOut, RenderState& state, const std::string& buffer,
            size_t cursor_bytes) noexcept {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hOut, &csbi);
  const SHORT W = csbi.dwSize.X;
  const WORD defaultAttr = csbi.wAttributes;

  {
    DWORD clear = (DWORD)(W - state.input_start.X);
    if (clear > 0) eraseChars(hOut, state.input_start, clear, defaultAttr);
  }

  moveTo(hOut, state.input_start.X, state.input_start.Y);
  const std::wstring wbuf =
      utf8_to_wstring(buffer);  // converted once, reused for cursor restore
  if (!wbuf.empty()) {
    DWORD written = 0;
    WriteConsoleW(hOut, wbuf.c_str(), (DWORD)wbuf.size(), &written, nullptr);
  }

  moveTo(hOut, state.input_start.X, state.input_start.Y);
  // Reuse wbuf; compute wchar offset for cursor_bytes without extra allocation.
  if (!wbuf.empty() && cursor_bytes > 0) {
    int wchar_cursor = MultiByteToWideChar(
        CP_UTF8, 0, buffer.c_str(),
        static_cast<int>(std::min(cursor_bytes, buffer.size())), nullptr, 0);
    if (wchar_cursor > 0) {
      DWORD written = 0;
      WriteConsoleW(hOut, wbuf.c_str(), static_cast<DWORD>(wchar_cursor),
                    &written, nullptr);
    }
  }
}

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

/// Read one line interactively.
///
/// @param prompt     Wide-string prompt shown before the cursor (e.g. L"winux>
/// ")
/// @returns The accepted UTF-8 string, or "" on Ctrl+C / EOF.
export std::string readInteractiveLine(const std::wstring& prompt) {
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

  // ── Verify we are attached to a real console ──
  {
    DWORD mode = 0;
    if (!GetConsoleMode(hIn, &mode) || !GetConsoleMode(hOut, &mode)) {
      // Fallback: plain getline
      std::string line;
      std::getline(std::cin, line);
      return line;
    }
  }

  // ── Save original console modes ──
  DWORD inModeOrig = 0, outModeOrig = 0;
  GetConsoleMode(hIn, &inModeOrig);
  GetConsoleMode(hOut, &outModeOrig);

  // Raw-ish key input: keep most existing flags and only disable line/echo
  // buffering so we can process keys while preserving host behavior.
  DWORD inModeInteractive = inModeOrig;
  // Disable processed input so Ctrl+C is delivered as a key event instead of
  // a process-level CTRL_C_EVENT that terminates winuxcmd.
  inModeInteractive &=
      ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
  inModeInteractive |= (ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
  SetConsoleMode(hIn, inModeInteractive);
  // Enable virtual-terminal processing so ANSI codes work in the host.
  SetConsoleMode(hOut, outModeOrig | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  // ── Print prompt ──
  {
    DWORD written = 0;
    WriteConsoleW(hOut, prompt.c_str(), (DWORD)prompt.size(), &written,
                  nullptr);
  }

  // ── Record where the editable area starts ──
  RenderState state;
  {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    state.input_start = csbi.dwCursorPosition;
  }

  std::string buffer;
  size_t cursor = 0;
  static std::vector<std::string> history;
  std::optional<size_t> history_index;
  std::string history_draft;
  auto historyUp = [&]() {
    if (history.empty()) return;
    if (!history_index.has_value()) {
      history_draft = buffer;
      history_index = history.size();
    }
    if (*history_index > 0) {
      --(*history_index);
      buffer = history[*history_index];
      cursor = buffer.size();
    }
  };
  auto historyDown = [&]() {
    if (!history_index.has_value()) return;
    if (*history_index + 1 < history.size()) {
      ++(*history_index);
      buffer = history[*history_index];
    } else {
      history_index.reset();
      buffer = history_draft;
    }
    cursor = buffer.size();
  };

  // ── Main input loop ──
  while (true) {
    redraw(hOut, state, buffer, cursor);

    // Read one raw input event.
    INPUT_RECORD ir;
    DWORD nread = 0;
    if (!ReadConsoleInputW(hIn, &ir, 1, &nread) || nread == 0) continue;

    // Ignore everything except key-down events.
    if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

    const auto& ke = ir.Event.KeyEvent;
    WORD vk = ke.wVirtualKeyCode;
    wchar_t ch = ke.uChar.UnicodeChar;
    DWORD ctrl = ke.dwControlKeyState;

    // ── Enter: confirm ──
    if (vk == VK_RETURN) {
      cursor = buffer.size();
      redraw(hOut, state, buffer, cursor);
      DWORD written = 0;
      WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);
      bool has_non_ws = std::ranges::any_of(buffer, [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
      });
      if (has_non_ws && (history.empty() || history.back() != buffer))
        history.push_back(buffer);
      break;
    }

    // ── Arrow up: history ──
    if (vk == VK_UP) {
      historyUp();
      continue;
    }

    // ── Arrow down: history ──
    if (vk == VK_DOWN) {
      historyDown();
      continue;
    }

    // ── Arrow left / right: move caret inside line ──
    if (vk == VK_LEFT) {
      cursor = prevUtf8Pos(buffer, cursor);
      continue;
    }
    if (vk == VK_RIGHT) {
      cursor = nextUtf8Pos(buffer, cursor);
      continue;
    }

    // ── Backspace ──
    if (vk == VK_BACK) {
      if (!buffer.empty() && cursor > 0) {
        size_t prev = prevUtf8Pos(buffer, cursor);
        buffer.erase(prev, cursor - prev);
        cursor = prev;
        history_index.reset();
      }
      continue;
    }

    if ((vk == 'V' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) ||
        (vk == VK_INSERT && (ctrl & SHIFT_PRESSED))) {
      auto clip = readClipboardUtf8SingleLine();
      if (clip.has_value() && !clip->empty()) {
        buffer.insert(cursor, *clip);
        cursor += clip->size();
        history_index.reset();
      }
      continue;
    }

    // ── Ctrl+C: abort ──
    if (vk == 'C' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) &&
        (ctrl & (SHIFT_PRESSED | LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) == 0) {
      redraw(hOut, state, "", 0);
      DWORD written = 0;
      WriteConsoleW(hOut, L"^C\r\n", 4, &written, nullptr);
      SetConsoleMode(hIn, inModeOrig);
      SetConsoleMode(hOut, outModeOrig);
      return "";
    }

    // ── Ctrl+D: EOF / exit ──
    if (vk == 'D' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
      if (buffer.empty()) {
        redraw(hOut, state, "", 0);
        DWORD written = 0;
        WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);
        SetConsoleMode(hIn, inModeOrig);
        SetConsoleMode(hOut, outModeOrig);
        return "\x04";  // sentinel for EOF
      }
      continue;
    }

    // ── Printable Unicode character ──
    if (ch >= 0x20 && ch != 0x7F) {
      // Convert wchar_t → UTF-8 and append.
      char mb[4] = {};
      int len =
          WideCharToMultiByte(CP_UTF8, 0, &ch, 1, mb, 4, nullptr, nullptr);
      buffer.insert(cursor, mb, static_cast<size_t>(len));
      cursor += static_cast<size_t>(len);
      history_index.reset();
      continue;
    }
  }

  // ── Restore console modes ──
  SetConsoleMode(hIn, inModeOrig);
  SetConsoleMode(hOut, outModeOrig);
  return buffer;
}
