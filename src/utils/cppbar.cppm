/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: cppbar.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
module;

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#undef RGB
#endif

export module utils:cppbar;

namespace cppbar {

// ======================================================
// Terminal Functions (Windows only)
// ======================================================

namespace terminal {

enum class Platform { Windows, Linux, MacOS, Unknown };

struct TerminalInfo {
  int width;
  int height;
  bool supports_ansi;
  bool supports_unicode;
  bool is_tty;
};

inline Platform get_platform() {
#ifdef _WIN32
  return Platform::Windows;
#else
  return Platform::Unknown;
#endif
}

inline bool is_tty() {
#ifdef _WIN32
  return _isatty(_fileno(stdout));
#else
  return false;
#endif
}

inline bool supports_ansi() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE) return false;

  DWORD mode = 0;
  if (!GetConsoleMode(h, &mode)) return false;

  return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
  return false;
#endif
}

inline bool supports_unicode() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

inline int get_width() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE) return 80;

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(h, &csbi)) {
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
  }
  return 80;
#else
  return 80;
#endif
}

inline int get_height() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE) return 24;

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(h, &csbi)) {
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }
  return 24;
#else
  return 24;
#endif
}

inline TerminalInfo get_terminal_info() {
  TerminalInfo info;
  info.width = get_width();
  info.height = get_height();
  info.supports_ansi = supports_ansi();
  info.supports_unicode = supports_unicode();
  info.is_tty = is_tty();
  return info;
}

inline void set_cursor_position(int row, int col) {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    COORD pos = {static_cast<SHORT>(col), static_cast<SHORT>(row)};
    SetConsoleCursorPosition(h, pos);
  }
#endif
}

inline void move_cursor_up(int lines) {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
      COORD pos = csbi.dwCursorPosition;
      pos.Y = (pos.Y >= lines) ? (pos.Y - lines) : 0;
      SetConsoleCursorPosition(h, pos);
    }
  }
#endif
}

inline void move_cursor_down(int lines) {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
      COORD pos = csbi.dwCursorPosition;
      pos.Y += lines;
      SetConsoleCursorPosition(h, pos);
    }
  }
#endif
}

inline void clear_line() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
      DWORD written;
      COORD start = {0, csbi.dwCursorPosition.Y};
      FillConsoleOutputCharacterA(h, ' ', csbi.dwSize.X, start, &written);
      FillConsoleOutputAttribute(h, csbi.wAttributes, csbi.dwSize.X, start,
                                 &written);
      SetConsoleCursorPosition(h, start);
    }
  }
#endif
}

inline void hide_cursor() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    CONSOLE_CURSOR_INFO cci;
    if (GetConsoleCursorInfo(h, &cci)) {
      cci.bVisible = FALSE;
      SetConsoleCursorInfo(h, &cci);
    }
  }
#endif
}

inline void show_cursor() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    CONSOLE_CURSOR_INFO cci;
    if (GetConsoleCursorInfo(h, &cci)) {
      cci.bVisible = TRUE;
      SetConsoleCursorInfo(h, &cci);
    }
  }
#endif
}

inline void enable_alternate_screen() {
  // Not supported on Windows
}

inline void disable_alternate_screen() {
  // Not supported on Windows
}

}  // namespace terminal

// ======================================================
// Color Functions
// ======================================================

namespace color {

struct RGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  constexpr RGB(uint8_t red, uint8_t green, uint8_t blue)
      : r(red), g(green), b(blue) {}

  constexpr RGB() : r(0), g(0), b(0) {}

  std::string to_ansi_foreground() const {
    std::ostringstream oss;
    oss << "\033[38;2;" << static_cast<int>(r) << ";" << static_cast<int>(g)
        << ";" << static_cast<int>(b) << "m";
    return oss.str();
  }

  std::string to_ansi_background() const {
    std::ostringstream oss;
    oss << "\033[48;2;" << static_cast<int>(r) << ";" << static_cast<int>(g)
        << ";" << static_cast<int>(b) << "m";
    return oss.str();
  }

  static constexpr RGB black() { return RGB(0, 0, 0); }
  static constexpr RGB red() { return RGB(255, 0, 0); }
  static constexpr RGB green() { return RGB(0, 255, 0); }
  static constexpr RGB yellow() { return RGB(255, 255, 0); }
  static constexpr RGB blue() { return RGB(0, 0, 255); }
  static constexpr RGB magenta() { return RGB(255, 0, 255); }
  static constexpr RGB cyan() { return RGB(0, 255, 255); }
  static constexpr RGB white() { return RGB(255, 255, 255); }

  static constexpr RGB bright_black() { return RGB(128, 128, 128); }
  static constexpr RGB bright_red() { return RGB(255, 128, 128); }
  static constexpr RGB bright_green() { return RGB(128, 255, 128); }
  static constexpr RGB bright_yellow() { return RGB(255, 255, 128); }
  static constexpr RGB bright_blue() { return RGB(128, 128, 255); }
  static constexpr RGB bright_magenta() { return RGB(255, 128, 255); }
  static constexpr RGB bright_cyan() { return RGB(128, 255, 255); }
  static constexpr RGB bright_white() { return RGB(255, 255, 255); }
};

export class Color {
 public:
  enum class Named {
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    BrightBlack,
    BrightRed,
    BrightGreen,
    BrightYellow,
    BrightBlue,
    BrightMagenta,
    BrightCyan,
    BrightWhite,
    None
  };

 private:
  enum class Type { None, Named, RGB };
  Type type_;
  Named named_;
  RGB rgb_;

 public:
  Color() : type_(Type::None) {}

  Color(Named named) : type_(Type::Named), named_(named) {}

  Color(uint8_t r, uint8_t g, uint8_t b) : type_(Type::RGB), rgb_(r, g, b) {}

  Color(const RGB& rgb) : type_(Type::RGB), rgb_(rgb) {}

  bool is_none() const { return type_ == Type::None; }

  std::string to_ansi_foreground() const {
    if (type_ == Type::None) {
      return "";
    }

    if (type_ == Type::RGB) {
      return rgb_.to_ansi_foreground();
    }

    const int ansi_colors[] = {30, 31, 32, 33, 34, 35, 36, 37,
                               90, 91, 92, 93, 94, 95, 96, 97};

    int index = static_cast<int>(named_);
    std::ostringstream oss;
    oss << "\033[" << ansi_colors[index] << "m";
    return oss.str();
  }

  std::string to_ansi_background() const {
    if (type_ == Type::None) {
      return "";
    }

    if (type_ == Type::RGB) {
      return rgb_.to_ansi_background();
    }

    const int ansi_colors[] = {40,  41,  42,  43,  44,  45,  46,  47,
                               100, 101, 102, 103, 104, 105, 106, 107};

    int index = static_cast<int>(named_);
    std::ostringstream oss;
    oss << "\033[" << ansi_colors[index] << "m";
    return oss.str();
  }

  static Color from_256(uint8_t code) {
    if (code < 16) {
      int index = code;
      if (index < 8) {
        return static_cast<Named>(index);
      } else {
        return static_cast<Named>(index + 8);
      }
    } else if (code < 232) {
      code -= 16;
      uint8_t r = (code / 36) * 51;
      uint8_t g = ((code % 36) / 6) * 51;
      uint8_t b = (code % 6) * 51;
      return Color(r, g, b);
    } else {
      uint8_t gray = (code - 232) * 10 + 8;
      return Color(gray, gray, gray);
    }
  }

  static Color from_hex(const std::string& hex) {
    if (hex.empty() || hex[0] != '#') {
      return Color();
    }

    std::string h = hex.substr(1);

    try {
      if (h.length() == 3) {
        uint8_t r = static_cast<uint8_t>(
            std::stoi(h.substr(0, 1) + h.substr(0, 1), nullptr, 16));
        uint8_t g = static_cast<uint8_t>(
            std::stoi(h.substr(1, 1) + h.substr(1, 1), nullptr, 16));
        uint8_t b = static_cast<uint8_t>(
            std::stoi(h.substr(2, 1) + h.substr(2, 1), nullptr, 16));
        return Color(r, g, b);
      } else if (h.length() == 6) {
        uint8_t r =
            static_cast<uint8_t>(std::stoi(h.substr(0, 2), nullptr, 16));
        uint8_t g =
            static_cast<uint8_t>(std::stoi(h.substr(2, 2), nullptr, 16));
        uint8_t b =
            static_cast<uint8_t>(std::stoi(h.substr(4, 2), nullptr, 16));
        return Color(r, g, b);
      }
    } catch (...) {
      return Color();
    }

    return Color();
  }

  static constexpr RGB black_c() { return RGB(0, 0, 0); }
  static constexpr RGB red_c() { return RGB(255, 0, 0); }
  static constexpr RGB green_c() { return RGB(0, 255, 0); }
  static constexpr RGB yellow_c() { return RGB(255, 255, 0); }
  static constexpr RGB blue_c() { return RGB(0, 0, 255); }
  static constexpr RGB magenta_c() { return RGB(255, 0, 255); }
  static constexpr RGB cyan_c() { return RGB(0, 255, 255); }
  static constexpr RGB white_c() { return RGB(255, 255, 255); }
  static constexpr RGB bright_black_c() { return RGB(128, 128, 128); }
  static constexpr RGB bright_red_c() { return RGB(255, 128, 128); }
  static constexpr RGB bright_green_c() { return RGB(128, 255, 128); }
  static constexpr RGB bright_yellow_c() { return RGB(255, 255, 128); }
  static constexpr RGB bright_blue_c() { return RGB(128, 128, 255); }
  static constexpr RGB bright_magenta_c() { return RGB(255, 128, 255); }
  static constexpr RGB bright_cyan_c() { return RGB(128, 255, 255); }
  static constexpr RGB bright_white_c() { return RGB(255, 255, 255); }
};

inline std::string apply_color(const std::string& text,
                               const Color& fg = Color(),
                               const Color& bg = Color()) {
  std::string result;
  result += fg.to_ansi_foreground();
  result += bg.to_ansi_background();
  result += text;
  result += "\033[0m";
  return result;
}

inline std::string reset_color() { return "\033[0m"; }

}  // namespace color

// ======================================================
// Style Functions
// ======================================================

namespace style {

struct StyleConfig {
  std::string left_bracket;
  std::string right_bracket;
  std::string filled_char;
  std::string empty_char;
  std::string tip_char;

  StyleConfig(const std::string& lb, const std::string& rb,
              const std::string& fc, const std::string& ec,
              const std::string& tc)
      : left_bracket(lb),
        right_bracket(rb),
        filled_char(fc),
        empty_char(ec),
        tip_char(tc) {}
};

class Style {
 public:
  enum class Type { Unicode, Ascii, Braille };

 private:
  Type type_;

 public:
  Style() : type_(Type::Unicode) {}

  explicit Style(Type type) : type_(type) {}

  StyleConfig get_config() const {
    switch (type_) {
      case Type::Ascii:
        return StyleConfig("[", "]", "=", " ", ">");
      case Type::Braille:
        return StyleConfig("|", "|", "#", ".", ">");
      case Type::Unicode:
      default:
        return StyleConfig("[", "]", "#", ".", ">");
    }
  }

  void set_type(Type type) { type_ = type; }
  Type get_type() const { return type_; }

  static Style unicode() { return Style(Type::Unicode); }
  static Style ascii() { return Style(Type::Ascii); }
  static Style braille() { return Style(Type::Braille); }
};

export struct PresetStyles {
  static StyleConfig classic() { return StyleConfig("[", "]", "=", " ", ">"); }

  static StyleConfig modern() { return StyleConfig("[", "]", "#", ".", ">"); }

  static StyleConfig minimal() { return StyleConfig("", "", "=", " ", ">"); }

  static StyleConfig blocks() { return StyleConfig("|", "|", "#", ".", ":"); }

  static StyleConfig dots() { return StyleConfig("<", ">", "O", "o", "@"); }

  static StyleConfig squares() { return StyleConfig("[", "]", "#", ":", "+"); }
};

}  // namespace style

// ======================================================
// Progress Bar
// ======================================================

export class ProgressBar {
 private:
  int total_;
  int current_;
  int width_;
  std::string message_;
  style::StyleConfig style_;
  color::Color fg_color_;
  color::Color bg_color_;

 public:
  ProgressBar(int total, const std::string& message = "", int width = 50)
      : total_(total),
        current_(0),
        width_(width),
        message_(message),
        style_(style::StyleConfig("[", "]", "=", " ", ">")),
        fg_color_(),
        bg_color_() {}

  void update(int progress) {
    current_ = progress;
    display();
  }

  void increment() {
    if (current_ < total_) {
      current_++;
      display();
    }
  }

  void finish() {
    current_ = total_;
    display();
    std::cout << std::endl;
  }

  void set_style(const style::StyleConfig& style) { style_ = style; }

  void set_foreground_color(const color::Color& color) { fg_color_ = color; }

  void set_background_color(const color::Color& color) { bg_color_ = color; }

 private:
  void display() {
    float percentage = (total_ > 0) ? (100.0f * current_ / total_) : 0.0f;
    int filled = (width_ * current_) / total_;
    int partial = ((width_ * current_ * 10) / total_) % 10;

    std::string result =
        "\r" + fg_color_.to_ansi_foreground() + bg_color_.to_ansi_background();

    result += message_ + " ";
    result += style_.left_bracket;

    // Filled portion
    for (int i = 0; i < filled; ++i) {
      result += style_.filled_char;
    }

    // Partial character (if any)
    if (partial > 0 && filled < width_) {
      result += style_.tip_char;
      filled++;
    }

    // Empty portion
    for (int i = filled; i < width_; ++i) {
      result += style_.empty_char;
    }

    result += style_.right_bracket;
    result += " ";
    result += std::to_string(static_cast<int>(percentage)) + "%";
    result += "\033[0m";

    std::cout << result << std::flush;
  }
};

// ======================================================
// Spinner (simple animation)
// ======================================================

export class Spinner {
 private:
  std::string message_;
  int frame_;
  bool active_;
  std::vector<std::string> frames_;

 public:
  Spinner(const std::string& message = "")
      : message_(message), frame_(0), active_(false) {
    frames_ = {"|", "/", "-", "\\"};
  }

  void set_message(const std::string& message) { message_ = message; }

  void set_frames(const std::vector<std::string>& frames) { frames_ = frames; }

  void start() {
    active_ = true;
    display();
  }

  void update() {
    if (active_) {
      frame_ = (frame_ + 1) % frames_.size();
      display();
    }
  }

  void stop() {
    active_ = false;
    std::cout << "\r" << message_ << " "
              << std::string(message_.length() + 1, ' ') << "\r" << std::flush;
  }

 private:
  void display() {
    if (active_) {
      std::cout << "\r" << message_ << " " << frames_[frame_] << "\033[?25l"
                << std::flush;
    }
  }
};

}  // namespace cppbar

// ======================================================
// Export all types
// ======================================================

// Terminal
export using cppbar::terminal::Platform;
export using cppbar::terminal::TerminalInfo;
export using cppbar::terminal::get_platform;
export using cppbar::terminal::is_tty;
export using cppbar::terminal::supports_ansi;
export using cppbar::terminal::supports_unicode;
export using cppbar::terminal::get_width;
export using cppbar::terminal::get_height;
export using cppbar::terminal::get_terminal_info;
export using cppbar::terminal::set_cursor_position;
export using cppbar::terminal::move_cursor_up;
export using cppbar::terminal::move_cursor_down;
export using cppbar::terminal::clear_line;
export using cppbar::terminal::hide_cursor;
export using cppbar::terminal::show_cursor;
export using cppbar::terminal::enable_alternate_screen;
export using cppbar::terminal::disable_alternate_screen;

// Color
export using cppbar::color::RGB;
export using cppbar::color::Color;
export using cppbar::color::apply_color;
export using cppbar::color::reset_color;

// Style
export using cppbar::style::StyleConfig;
export using cppbar::style::Style;
export using cppbar::style::PresetStyles;

// Progress components
export using cppbar::ProgressBar;
export using cppbar::Spinner;

// Export color namespace for direct access
export namespace color {
using Color = cppbar::color::Color;
using RGB = cppbar::color::RGB;
}  // namespace color

// Export style namespace for direct access
export namespace style {
using StyleConfig = cppbar::style::StyleConfig;
using Style = cppbar::style::Style;
using PresetStyles = cppbar::style::PresetStyles;
}  // namespace style
