/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr XXD_OPTIONS = std::array{
    // [EXT] option
    OPTION("-r", "--reverse", "reverse: convert hex to binary"),
    // [EXT] option
    OPTION("-p", "--plain", "plain hexdump"),
    // [EXT] option
    OPTION("-c", "--cols", "set bytes per line", INT_TYPE),
    // [EXT] option
    OPTION("-l", "--len", "limit input to LEN bytes", INT_TYPE),
    // [EXT] option
    OPTION("-s", "--seek", "start at SEEK bytes into input", INT_TYPE),
    // [EXT] option
    OPTION("-o", "--offset", "add OFFSET to displayed file position", INT_TYPE),
    // [EXT] option
    OPTION("-u", "--upper-case", "use upper-case hex digits"),
    OPTION("-g", "--group", "set bytes per hex group", INT_TYPE),
    // [EXT] option
    OPTION("-i", "--include", "output in C include file style"),
    // [EXT] option
    OPTION("-n", "--name", "set the variable name used in C include output",
           STRING_TYPE)};

namespace {
struct XxdConfig {
  bool reverse = false;
  bool plain = false;
  bool upper_case = false;
  bool include = false;
  std::optional<std::string> name;
  std::optional<size_t> length;
  size_t seek = 0;
  size_t display_offset = 0;
  size_t columns = 16;
  bool columns_specified = false;
  size_t group_width = 2;
  std::string input_file = "-";
  std::optional<std::string> output_file;
};

std::vector<unsigned char> read_stdin_bytes() {
  std::vector<unsigned char> data;
  char ch = 0;
  while (std::cin.get(ch)) {
    data.push_back(static_cast<unsigned char>(ch));
  }
  return data;
}

std::optional<std::vector<unsigned char>> read_file_bytes(
    const std::string& filename) {
  std::wstring wfilename = utf8_to_wstring(filename);
  HANDLE hFile =
      CreateFileW(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return std::nullopt;

  LARGE_INTEGER fileSize{};
  if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 0) {
    CloseHandle(hFile);
    return std::nullopt;
  }

  std::vector<unsigned char> buffer(static_cast<size_t>(fileSize.QuadPart));
  DWORD bytesRead = 0;
  BOOL ok = TRUE;
  if (!buffer.empty()) {
    ok = ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &bytesRead, nullptr);
  }
  CloseHandle(hFile);
  if (!ok) return std::nullopt;
  buffer.resize(bytesRead);
  return buffer;
}

std::string byte_hex(unsigned char value, bool upper_case) {
  constexpr char lower_digits[] = "0123456789abcdef";
  constexpr char upper_digits[] = "0123456789ABCDEF";
  const char* digits = upper_case ? upper_digits : lower_digits;
  std::string out;
  out.push_back(digits[(value >> 4) & 0x0f]);
  out.push_back(digits[value & 0x0f]);
  return out;
}

std::string offset_hex(size_t offset, bool upper_case) {
  char buf[32];
  if (upper_case) {
    sprintf_s(buf, sizeof(buf), "%08zX", offset);
  } else {
    sprintf_s(buf, sizeof(buf), "%08zx", offset);
  }
  return std::string(buf);
}

std::optional<unsigned char> parse_hex_byte(char high, char low) {
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int h = digit(high), l = digit(low);
  if (h < 0 || l < 0) return std::nullopt;
  return static_cast<unsigned char>((h << 4) | l);
}

std::optional<std::vector<unsigned char>> reverse_plain_xxd(
    const std::vector<unsigned char>& input) {
  std::vector<unsigned char> output;
  int high_nibble = -1;
  for (unsigned char c : input) {
    if (std::isspace(c)) continue;
    int value = -1;
    if (c >= '0' && c <= '9')
      value = c - '0';
    else if (c >= 'a' && c <= 'f')
      value = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      value = c - 'A' + 10;
    else
      return std::nullopt;
    if (high_nibble < 0) {
      high_nibble = value;
    } else {
      output.push_back(static_cast<unsigned char>((high_nibble << 4) | value));
      high_nibble = -1;
    }
  }
  if (high_nibble >= 0) return std::nullopt;
  return output;
}

std::optional<std::vector<unsigned char>> reverse_xxd(
    const std::vector<unsigned char>& input) {
  std::string text(input.begin(), input.end());
  std::vector<unsigned char> output;
  size_t line_start = 0;
  while (line_start <= text.size()) {
    size_t line_end = text.find('\n', line_start);
    if (line_end == std::string::npos) line_end = text.size();
    std::string_view line(text.data() + line_start, line_end - line_start);
    size_t colon = line.find(':');
    if (colon != std::string_view::npos) line.remove_prefix(colon + 1);
    size_t pos = 0;
    while (pos < line.size()) {
      while (pos < line.size() &&
             std::isspace(static_cast<unsigned char>(line[pos])))
        ++pos;
      size_t token_start = pos;
      while (pos < line.size() &&
             !std::isspace(static_cast<unsigned char>(line[pos])))
        ++pos;
      auto token = line.substr(token_start, pos - token_start);
      if (token.empty()) continue;
      if (token.size() % 2 != 0) break;
      std::vector<unsigned char> token_bytes;
      bool valid = true;
      for (size_t i = 0; i < token.size(); i += 2) {
        auto byte = parse_hex_byte(token[i], token[i + 1]);
        if (!byte) {
          valid = false;
          break;
        }
        token_bytes.push_back(*byte);
      }
      if (!valid) break;
      output.insert(output.end(), token_bytes.begin(), token_bytes.end());
    }
    if (line_end == text.size()) break;
    line_start = line_end + 1;
  }
  return output;
}

void print_plain_xxd(const std::vector<unsigned char>& data, size_t columns,
                     bool upper_case) {
  for (size_t offset = 0; offset < data.size(); offset += columns) {
    size_t count = std::min(columns, data.size() - offset);
    for (size_t i = 0; i < count; ++i) {
      safePrint(byte_hex(data[offset + i], upper_case));
    }
    safePrint("\n");
  }
}

void print_default_xxd(const std::vector<unsigned char>& data, size_t columns,
                       bool upper_case, size_t display_offset,
                       size_t group_width) {
  for (size_t offset = 0; offset < data.size(); offset += columns) {
    size_t count = std::min(columns, data.size() - offset);
    safePrint(offset_hex(display_offset + offset, upper_case));
    safePrint(": ");

    for (size_t i = 0; i < columns; ++i) {
      if (i < count) {
        safePrint(byte_hex(data[offset + i], upper_case));
      } else {
        safePrint("  ");
      }
      if (group_width > 1 && i % group_width == group_width - 1) safePrint(" ");
    }

    safePrint(" ");
    for (size_t i = 0; i < count; ++i) {
      unsigned char c = data[offset + i];
      safePrint((c > 31 && c < 127) ? std::string(1, static_cast<char>(c))
                                    : ".");
    }
    safePrint("\n");
  }
}

std::string include_identifier(std::string_view value) {
  std::string identifier;
  identifier.reserve(value.size() + 2);
  for (unsigned char c : value) {
    identifier.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
  }
  if (!identifier.empty() &&
      std::isdigit(static_cast<unsigned char>(identifier.front()))) {
    identifier.insert(0, "__");
  }
  return identifier;
}

std::string default_include_name(std::string_view filename) {
  const size_t separator = filename.find_last_of("/\\");
  if (separator != std::string_view::npos)
    filename.remove_prefix(separator + 1);
  return include_identifier(filename);
}

void print_include_xxd(const std::vector<unsigned char>& data, size_t columns,
                       bool upper_case, std::string_view filename,
                       const std::optional<std::string>& explicit_name) {
  if (explicit_name || filename != "-") {
    const std::string name = explicit_name ? include_identifier(*explicit_name)
                                           : default_include_name(filename);
    safePrint("unsigned char " + name + "[] = {\n");
    for (size_t offset = 0; offset < data.size(); offset += columns) {
      const size_t count = std::min(columns, data.size() - offset);
      safePrint("  ");
      for (size_t i = 0; i < count; ++i) {
        if (i != 0) safePrint(", ");
        safePrint("0x" + byte_hex(data[offset + i], upper_case));
      }
      safePrint(offset + count == data.size() ? "\n" : ",\n");
    }
    safePrint("};\n");
    safePrint("unsigned int " + name + "_len = " + std::to_string(data.size()) +
              ";\n");
    return;
  }

  for (size_t offset = 0; offset < data.size(); offset += columns) {
    const size_t count = std::min(columns, data.size() - offset);
    safePrint("  ");
    for (size_t i = 0; i < count; ++i) {
      if (i != 0) safePrint(", ");
      safePrint("0x" + byte_hex(data[offset + i], upper_case));
    }
    safePrint("\n");
  }
}

XxdConfig build_config(const CommandContext<XXD_OPTIONS.size()>& ctx) {
  XxdConfig cfg;
  cfg.reverse = ctx.get<bool>("-r", false) || ctx.get<bool>("--reverse", false);
  cfg.plain = ctx.get<bool>("-p", false) || ctx.get<bool>("--plain", false);
  cfg.upper_case =
      ctx.get<bool>("-u", false) || ctx.get<bool>("--upper-case", false);
  cfg.include = ctx.get<bool>("-i", false) || ctx.get<bool>("--include", false);
  if (ctx.has("-n") || ctx.has("--name")) {
    cfg.name = ctx.get<std::string>("--name", ctx.get<std::string>("-n", ""));
  }
  if (ctx.has("-l") || ctx.has("--len")) {
    const int length = ctx.get<int>("--len", ctx.get<int>("-l", 0));
    if (length >= 0) cfg.length = static_cast<size_t>(length);
  }
  if (ctx.has("-s") || ctx.has("--seek")) {
    const int seek = ctx.get<int>("--seek", ctx.get<int>("-s", 0));
    if (seek >= 0) cfg.seek = static_cast<size_t>(seek);
  }
  if (ctx.has("-o") || ctx.has("--offset")) {
    const int offset = ctx.get<int>("--offset", ctx.get<int>("-o", 0));
    if (offset >= 0) cfg.display_offset = static_cast<size_t>(offset);
  }
  if (ctx.has("-c") || ctx.has("--cols")) {
    cfg.columns_specified = true;
    int cols = ctx.get<int>("--cols", ctx.get<int>("-c", 16));
    if (cols <= 0) {
      throw std::runtime_error(winux::i18n::translate(
          "command.xxd.error.invalid_columns", "xxd: invalid column count"));
    }
    cfg.columns = static_cast<size_t>(cols);
  }
  if (ctx.has("-g") || ctx.has("--group")) {
    int g = ctx.get<int>("--group", ctx.get<int>("-g", 2));
    if (g < 1) g = 2;
    cfg.group_width = static_cast<size_t>(g);
  }
  if (cfg.columns == 0) {
    throw std::runtime_error(winux::i18n::translate(
        "command.xxd.error.invalid_columns", "xxd: invalid column count"));
  }
  if (!ctx.positionals.empty()) {
    cfg.input_file = std::string(ctx.positionals[0]);
  }
  if (cfg.reverse && ctx.positionals.size() > 1) {
    cfg.output_file = std::string(ctx.positionals[1]);
  }
  return cfg;
}

void write_output(HANDLE handle, const std::vector<unsigned char>& data) {
  if (data.empty()) return;
  DWORD written = 0;
  if (!WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written,
                 nullptr) ||
      written != data.size()) {
    throw std::runtime_error(winux::i18n::translate(
        "command.xxd.error.invalid_hex_dump", "xxd: invalid hex dump"));
  }
}
}  // namespace

REGISTER_COMMAND(xxd,
                 /* cmd_name */ "xxd",
                 /* cmd_synopsis */ "xxd [OPTION]... [FILE]...",
                 /* cmd_desc */ "Make a hexdump or do the reverse.",
                 /* examples */ "xxd file.txt\nxxd -r hex.txt > file.txt",
                 /* see_also */ "od",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ XXD_OPTIONS) {
  using namespace core::pipeline;

  XxdConfig cfg;
  try {
    cfg = build_config(ctx);
  } catch (const std::exception& ex) {
    safeErrorPrintLn(ex.what());
    return 1;
  }

  auto load_input = [&](const std::string& filename)
      -> std::optional<std::vector<unsigned char>> {
    if (filename == "-") return read_stdin_bytes();
    return read_file_bytes(filename);
  };

  auto input = load_input(cfg.input_file);
  if (!input) {
    safeErrorPrintLn(winux::i18n::format("command.xxd.error.cannot_open",
                                         "xxd: cannot open {}",
                                         cfg.input_file));
    return 1;
  }

  if (cfg.seek >= input->size()) {
    input->clear();
  } else if (cfg.seek > 0) {
    input->erase(input->begin(), input->begin() + cfg.seek);
  }
  if (cfg.length && *cfg.length < input->size()) input->resize(*cfg.length);

  if (cfg.reverse) {
    std::optional<std::vector<unsigned char>> decoded;
    if (cfg.plain) {
      decoded = reverse_plain_xxd(*input);
    } else {
      decoded = reverse_xxd(*input);
    }
    if (!decoded) {
      safeErrorPrintLn(winux::i18n::translate(
          "command.xxd.error.invalid_hex_dump", "xxd: invalid hex dump"));
      return 1;
    }

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE output_file = INVALID_HANDLE_VALUE;
    if (cfg.output_file) {
      const auto output_name = utf8_to_wstring(*cfg.output_file);
      output_file = CreateFileW(output_name.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (output_file == INVALID_HANDLE_VALUE) {
        safeErrorPrintLn(winux::i18n::format("command.xxd.error.cannot_create",
                                             "xxd: cannot create {}",
                                             *cfg.output_file));
        return 1;
      }
      output = output_file;
    }
    try {
      write_output(output, *decoded);
    } catch (const std::exception& ex) {
      if (output_file != INVALID_HANDLE_VALUE) CloseHandle(output_file);
      safeErrorPrintLn(ex.what());
      return 1;
    }
    if (output_file != INVALID_HANDLE_VALUE) CloseHandle(output_file);
    return 0;
  }

  if (cfg.include) {
    print_include_xxd(*input, cfg.columns_specified ? cfg.columns : 12,
                      cfg.upper_case, cfg.input_file, cfg.name);
  } else if (cfg.plain) {
    print_plain_xxd(*input, cfg.columns, cfg.upper_case);
  } else {
    print_default_xxd(*input, cfg.columns, cfg.upper_case, cfg.display_offset,
                      cfg.group_width);
  }
  return 0;
}
