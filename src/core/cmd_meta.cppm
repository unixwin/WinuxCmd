// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
module;
#include "pch/pch.h"
export module core:cmd_meta;
import std;
import utils;

namespace cmd::meta {
// OptionMeta with constexpr support
export enum class OptionType {
  Bool,
  Int,
  String,
  OptionalInt,
  OptionalString,
  TerminatedString
};

export struct OptionMeta {
  std::string_view short_name;
  std::string_view long_name;
  std::string_view description;
  OptionType type;
  size_t index = 0;

  constexpr OptionMeta(std::string_view s = "", std::string_view l = "",
                       std::string_view d = "", OptionType t = OptionType::Bool)
      : short_name(s), long_name(l), description(d), type(t) {}
};

export constexpr auto option_matches(const OptionMeta& meta,
                                     std::string_view short_name,
                                     std::string_view long_name) -> bool {
  return (!short_name.empty() && meta.short_name == short_name) ||
         (!long_name.empty() && meta.long_name == long_name);
}

namespace {
auto append_styled(std::string& out, std::string_view text,
                   std::string_view style, bool color) -> void {
  if (!color || style.empty()) {
    out.append(text.data(), text.size());
    return;
  }

  out.append(style.data(), style.size());
  out.append(text.data(), text.size());
  out += ANSI_RESET;
}

auto trim_left_ascii(std::string_view value) -> std::string_view {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  return value;
}

auto trim_right_ascii(std::string_view value) -> std::string_view {
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

// Help text is UTF-8, so a byte-based fallback wrap must not split a
// multi-byte code point. The normal space-based break is already safe.
auto utf8_boundary_at_or_before(std::string_view value, size_t position)
    -> size_t {
  position = std::min(position, value.size());
  size_t boundary = 0;
  while (boundary < position) {
    const auto lead = static_cast<unsigned char>(value[boundary]);
    size_t length = 1;
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
    }
    if (boundary + length > position) return boundary;
    boundary += length;
  }
  return boundary;
}

auto append_wrapped_line(std::string& out, std::string_view line,
                         size_t continuation_indent, size_t text_width,
                         bool indent_first_line) -> void {
  line = trim_left_ascii(line);

  bool first_output = true;
  while (line.size() > text_width && text_width > 0) {
    size_t break_pos = line.rfind(' ', text_width);
    bool broke_on_space = true;
    if (break_pos == std::string_view::npos || break_pos == 0) {
      break_pos = text_width;
      broke_on_space = false;
    }
    if (!broke_on_space) {
      break_pos = utf8_boundary_at_or_before(line, break_pos);
      if (break_pos == 0) {
        // Keep progress guaranteed for an unusually small width.
        break_pos = std::min(text_width, line.size());
      }
    }

    if (!first_output || indent_first_line) {
      out.append(continuation_indent, ' ');
    }
    std::string_view chunk = trim_right_ascii(line.substr(0, break_pos));
    out.append(chunk.data(), chunk.size());
    out += "\n";

    size_t next_pos = broke_on_space ? break_pos + 1 : break_pos;
    line = trim_left_ascii(line.substr(std::min(next_pos, line.size())));
    first_output = false;
  }

  if (!first_output || indent_first_line) {
    out.append(continuation_indent, ' ');
  }
  out.append(line.data(), line.size());
  out += "\n";
}

auto append_wrapped_description(std::string& out, std::string_view desc,
                                size_t continuation_indent,
                                size_t terminal_width) -> void {
  const size_t fallback_width = 80;
  const size_t effective_width = terminal_width > continuation_indent + 24
                                     ? terminal_width
                                     : fallback_width;
  const size_t text_width =
      std::max<size_t>(24, effective_width - continuation_indent);
  bool first_line = true;
  while (!desc.empty()) {
    size_t newline_pos = desc.find('\n');
    if (newline_pos == std::string_view::npos) {
      append_wrapped_line(out, desc, continuation_indent, text_width,
                          !first_line);
      break;
    }

    append_wrapped_line(out, desc.substr(0, newline_pos), continuation_indent,
                        text_width, !first_line);
    desc = desc.substr(newline_pos + 1);
    first_line = false;
  }
}

auto option_argument_suffix(const OptionMeta& opt) -> std::string_view {
  if (opt.short_name == "-NUM") return "";
  switch (opt.type) {
    case OptionType::Bool:
      return "";
    case OptionType::Int:
      return " NUM";
    case OptionType::String:
    case OptionType::TerminatedString:
      return " ARG";
    case OptionType::OptionalInt:
      return "[=NUM]";
    case OptionType::OptionalString:
      return "[=ARG]";
  }
  return "";
}

auto format_option_names(const OptionMeta& opt) -> std::string {
  const auto suffix = option_argument_suffix(opt);
  if (!opt.short_name.empty() && !opt.long_name.empty()) {
    return "  " + std::string(opt.short_name) + ", " +
           std::string(opt.long_name) + std::string(suffix);
  }
  if (!opt.short_name.empty()) {
    return "  " + std::string(opt.short_name) + std::string(suffix);
  }
  if (!opt.long_name.empty()) {
    return "      " + std::string(opt.long_name) + std::string(suffix);
  }
  return {};
}

auto has_option(std::span<const OptionMeta> options, std::string_view long_name)
    -> bool {
  return std::ranges::any_of(options, [long_name](const OptionMeta& opt) {
    return opt.long_name == long_name;
  });
}

auto has_short_option(std::span<const OptionMeta> options,
                      std::string_view short_name) -> bool {
  return std::ranges::any_of(options, [short_name](const OptionMeta& opt) {
    return opt.short_name == short_name;
  });
}

auto synopsis_looks_like_usage(std::string_view name, std::string_view synopsis)
    -> bool {
  synopsis = trim_left_ascii(synopsis);
  if (synopsis.empty()) return false;
  if (synopsis.starts_with(name)) return true;
  return name == "[" && synopsis.starts_with("[");
}
}  // namespace

auto format_help_text(std::string_view name, std::string_view synopsis,
                      std::string_view description,
                      std::span<const OptionMeta> options) -> std::string {
  std::string result;
  result.reserve(4096);

  const bool color = shouldUseAnsiColorStdout();
  const std::string title_style =
      std::string(ANSI_BOLD) + ansiFgRgb(98, 214, 255);
  const std::string section_style =
      std::string(ANSI_BOLD) + ANSI_UNDERLINE + ansiFg256(82);
  const std::string option_style = std::string(ANSI_BOLD) + ansiFg256(117);
  const std::string subtle_style = ansiFg256(245);
  const size_t terminal_width =
      static_cast<size_t>(std::clamp(getTerminalWidth(), 80, 120));

  append_styled(result, winux::i18n::translate("common.usage", "Usage:"),
                section_style, color);
  result += " ";
  if (synopsis_looks_like_usage(name, synopsis)) {
    append_styled(result, synopsis, title_style, color);
  } else {
    append_styled(result, name, title_style, color);
    result += " [OPTION]... [FILE]...";
  }
  result += "\n\n";

  const auto translated_description = winux::i18n::translate(
      "command." + std::string(name) + ".description", description);
  if (!translated_description.empty()) {
    result.append(translated_description.data(), translated_description.size());
    result += "\n\n";
  }

  std::vector<OptionMeta> display_options;
  display_options.reserve(options.size() + 2);
  for (const auto& opt : options) {
    // Empty descriptions are parser sentinels, not user-facing options.
    if (!opt.description.empty()) display_options.push_back(opt);
  }
  if (!has_option(options, "--help")) {
    display_options.emplace_back("", "--help", "display this help and exit",
                                 OptionType::Bool);
  }
  if (!has_option(options, "--version")) {
    display_options.emplace_back(
        has_short_option(options, "-V") ? "" : "-V", "--version",
        "output version information and exit", OptionType::Bool);
  }

  if (!display_options.empty()) {
    size_t max_option_width = 0;
    for (const auto& opt : display_options) {
      max_option_width =
          std::max(max_option_width, format_option_names(opt).size());
    }

    append_styled(result, winux::i18n::translate("common.options", "OPTIONS:"),
                  section_style, color);
    result += "\n";
    for (const auto& opt : display_options) {
      std::string option_str = format_option_names(opt);
      const auto option_key =
          (opt.long_name == "--help" || opt.long_name == "--version")
              ? "common.option." +
                    std::string(opt.long_name == "--help" ? "help" : "version")
              : "command." + std::string(name) + ".option." +
                    (!opt.long_name.empty()
                         ? std::string(opt.long_name.substr(2))
                     : opt.short_name.size() > 1
                         ? std::string(opt.short_name.substr(1))
                         : std::string("arg"));
      const auto translated_option =
          winux::i18n::translate(option_key, opt.description);

      append_styled(result, option_str, option_style, color);
      if (translated_option.empty()) {
        result += "\n";
      } else {
        size_t padding = max_option_width + 2 - option_str.size();
        if (padding > 0) {
          result.append(padding, ' ');
        }
        append_wrapped_description(result, translated_option,
                                   max_option_width + 2, terminal_width);
      }
    }
    result += "\n";
  }

  append_styled(result,
                winux::i18n::translate("common.exit_status", "EXIT STATUS:"),
                section_style, color);
  result += "\n";
  result += "  0  " + winux::i18n::translate("common.exit.ok", "if OK,") + "\n";
  result += "  1  " +
            winux::i18n::translate("common.exit.minor", "if minor problems,") +
            "\n";
  result +=
      "  2  " +
      winux::i18n::translate("common.exit.serious", "if serious trouble.") +
      "\n\n";
  append_styled(result, "WinuxCmd", subtle_style, color);
  result += " " + winux::i18n::translate(
                      "common.about",
                      "is a Windows implementation of GNU CoreUtils for "
                      "Linux-Windows developers and AI coding assistants.");

  return result;
}

auto format_man_text(std::string_view name, std::string_view synopsis,
                     std::string_view description,
                     std::span<const OptionMeta> options,
                     std::string_view examples, std::string_view see_also,
                     std::string_view author, std::string_view copyright)
    -> std::string {
  std::string result;
  result.reserve(4096);

  result += "NAME\n";
  result += "       ";
  result.append(name.data(), name.size());
  result += " - ";
  result.append(synopsis.data(), synopsis.size());
  result += "\n\n";

  result += "SYNOPSIS\n";
  result += "       ";
  result.append(name.data(), name.size());
  result += " [OPTION]... [FILE]...\n\n";

  result += "DESCRIPTION\n";
  if (!description.empty()) {
    std::string_view desc = description;
    while (!desc.empty()) {
      result += "       ";

      size_t newline_pos = desc.find('\n');
      if (newline_pos == std::string_view::npos) {
        result.append(desc.data(), desc.size());
        result += "\n";
        break;
      }

      result.append(desc.data(), newline_pos);
      result += "\n";
      desc = desc.substr(newline_pos + 1);
    }
    result += "\n";
  }

  if (!options.empty()) {
    result += "OPTIONS\n";

    size_t max_option_width = 0;
    for (const auto& opt : options) {
      size_t width = 7;
      if (!opt.short_name.empty() && !opt.long_name.empty()) {
        width += opt.short_name.size() + 2 + opt.long_name.size();
      } else if (!opt.short_name.empty()) {
        width += opt.short_name.size();
      } else if (!opt.long_name.empty()) {
        width += opt.long_name.size();
      }
      max_option_width = std::max(max_option_width, width);
    }

    for (const auto& opt : options) {
      result += "       ";
      if (!opt.short_name.empty() && !opt.long_name.empty()) {
        result += opt.short_name;
        result += ", ";
        result += opt.long_name;
      } else if (!opt.short_name.empty()) {
        result += opt.short_name;
      } else if (!opt.long_name.empty()) {
        result += opt.long_name;
      }

      if (!opt.description.empty()) {
        size_t current_width = result.length() - result.rfind('\n');
        if (current_width < max_option_width) {
          result.append(max_option_width - current_width, ' ');
        } else {
          result += "\n";
          result.append(max_option_width, ' ');
        }

        std::string_view desc = opt.description;
        bool first_line = true;
        while (!desc.empty()) {
          if (!first_line) {
            result += "\n";
            result.append(max_option_width, ' ');
          }

          size_t newline_pos = desc.find('\n');
          if (newline_pos == std::string_view::npos) {
            result.append(desc.data(), desc.size());
            break;
          }

          result.append(desc.data(), newline_pos);
          desc = desc.substr(newline_pos + 1);
          first_line = false;
        }
      }
      result += "\n";
    }
    result += "\n";
  }

  result += "       --help\n";
  result += "              " +
            winux::i18n::translate("common.option.help",
                                   "display this help and exit") +
            "\n\n";
  result += "       -V, --version\n";
  result += "              " +
            winux::i18n::translate("common.option.version",
                                   "output version information and exit") +
            "\n\n";

  if (!examples.empty()) {
    result += "EXAMPLES\n";
    std::string_view ex = examples;
    while (!ex.empty()) {
      result += "       ";

      size_t newline_pos = ex.find('\n');
      if (newline_pos == std::string_view::npos) {
        result.append(ex.data(), ex.size());
        result += "\n";
        break;
      }

      result.append(ex.data(), newline_pos);
      result += "\n";
      ex = ex.substr(newline_pos + 1);
    }
    result += "\n";
  }

  if (!see_also.empty()) {
    result += "SEE ALSO\n";
    result += "       ";
    result.append(see_also.data(), see_also.size());
    result += "\n\n";
  }

  result += "AUTHOR\n";
  result += "       ";
  result.append(author.data(), author.size());
  result += "\n\n";

  result += "COPYRIGHT\n";
  result += "       ";
  result.append(copyright.data(), copyright.size());
  result += "\n";

  return result;
}

export auto format_custom_help(std::string_view name, std::string_view help)
    -> std::string {
  const bool color = shouldUseAnsiColorStdout();
  const std::string title_style =
      std::string(ANSI_BOLD) + ansiFgRgb(98, 214, 255);
  const std::string section_style =
      std::string(ANSI_BOLD) + ANSI_UNDERLINE + ansiFg256(82);
  const std::string option_style = std::string(ANSI_BOLD) + ansiFg256(117);
  const std::string subtle_style = ansiFg256(245);

  std::string result;
  size_t start = 0;
  bool first_line = true;
  while (start <= help.size()) {
    const size_t end = help.find('\n', start);
    const size_t length =
        end == std::string_view::npos ? help.size() - start : end - start;
    std::string_view line = help.substr(start, length);
    const size_t indent = line.find_first_not_of(" \t");
    const bool indented = indent != std::string_view::npos && indent > 0;
    std::string_view content = indented ? line.substr(indent) : line;

    if (first_line) {
      const size_t ascii_separator = content.find(':');
      const size_t utf8_separator = content.find("：");
      size_t separator = ascii_separator;
      size_t separator_size = 1;
      if (utf8_separator != std::string_view::npos &&
          (separator == std::string_view::npos || utf8_separator < separator)) {
        separator = utf8_separator;
        separator_size = std::string_view("：").size();
      }
      if (separator != std::string_view::npos) {
        append_styled(result, content.substr(0, separator + separator_size),
                      section_style, color);
        result.append(content.substr(separator + separator_size));
      } else {
        append_styled(result, content, title_style, color);
      }
    } else if (!indented && !content.empty() &&
               (content.ends_with(':') || content.ends_with("："))) {
      append_styled(result, content, section_style, color);
    } else if (indented) {
      result.append(line.substr(0, indent));
      const size_t option_end = content.find("  ");
      if (option_end != std::string_view::npos && option_end > 0) {
        append_styled(result, content.substr(0, option_end), option_style,
                      color);
        result.append(content.substr(option_end));
      } else {
        result.append(content);
      }
    } else {
      result.append(line);
    }
    if (end == std::string_view::npos) break;
    result += '\n';
    start = end + 1;
    first_line = false;
  }

  if (!name.empty() && !result.empty()) {
    // Custom help blocks are intentionally authored by the command, but the
    // shared formatter still gives them the same final attribution as normal
    // help.
    result += '\n';
    append_styled(result, "WinuxCmd", subtle_style, color);
    result += " " + winux::i18n::translate(
                        "common.about",
                        "is a Windows implementation of GNU CoreUtils for "
                        "Linux-Windows developers and AI coding assistants.");
  }
  return result;
}

// Compile-time command metadata (fully compile-time)
export template <size_t OptionCount>
class CommandMeta {
 private:
  std::string_view m_name;
  std::string_view m_synopsis;
  std::string_view m_description;
  std::array<OptionMeta, OptionCount> m_options;
  std::string_view m_examples;
  std::string_view m_see_also;
  std::string_view m_author;
  std::string_view m_copyright;
  std::string_view m_brief_desc;  // Brief description for help listing

  static constexpr std::array<OptionMeta, OptionCount> with_index(
      std::array<OptionMeta, OptionCount> opts) {
    for (size_t i = 0; i < OptionCount; ++i) opts[i].index = i;
    return opts;
  }

 public:
  // constexpr constructor
  constexpr CommandMeta(
      std::string_view name, std::string_view synopsis,
      std::string_view description, std::array<OptionMeta, OptionCount> options,
      std::string_view examples = "", std::string_view see_also = "",
      std::string_view author = "WinuxCmd Project",
      std::string_view copyright = "Copyright © 2026 WinuxCmd",
      std::string_view brief_desc = "")
      : m_name(name),
        m_synopsis(synopsis),
        m_description(description),
        m_options(with_index(options)),
        m_examples(examples),
        m_see_also(see_also),
        m_author(author),
        m_copyright(copyright),
        m_brief_desc(brief_desc) {}

  // Accessors
  constexpr std::string_view name() const { return m_name; }
  constexpr std::string_view synopsis() const { return m_synopsis; }
  constexpr std::string_view description() const { return m_description; }
  constexpr const auto& options() const { return m_options; }
  constexpr size_t option_count() const { return OptionCount; }
  constexpr std::string_view examples() const { return m_examples; }
  constexpr std::string_view see_also() const { return m_see_also; }
  constexpr std::string_view author() const { return m_author; }
  constexpr std::string_view copyright() const { return m_copyright; }

  constexpr std::string_view brief_desc() const {
    return m_brief_desc;
  }  // Brief description getter

  constexpr int find_index(std::string_view name) const {
    for (size_t i = 0; i < OptionCount; ++i) {
      if (m_options[i].long_name == name || m_options[i].short_name == name) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  constexpr size_t findIndexOrThrow(std::string_view name) const {
    for (size_t i = 0; i < OptionCount; ++i) {
      if (options[i].long_name == name || options[i].short_name == name)
        return i;
    }
    static_assert(OptionCount != OptionCount, "Option name not found");
    return 0;
  }

  std::string get_help() const {
    return format_help_text(m_name, m_synopsis, m_description, m_options);
  }

  [[maybe_unused]]
  std::string get_man() const {
    return format_man_text(m_name, m_synopsis, m_description, m_options,
                           m_examples, m_see_also, m_author, m_copyright);
  }
};

// Runtime type-erased wrapper
export class CommandMetaBase {
 public:
  virtual ~CommandMetaBase() = default;

  virtual std::string_view name() const = 0;
  virtual std::string_view synopsis() const = 0;
  virtual std::string_view description() const = 0;

  virtual std::string get_help() const = 0;

  virtual std::string get_man() const = 0;

  virtual std::string_view brief_desc() const = 0;

  virtual std::span<const OptionMeta> options() const = 0;
};

export template <size_t N>
class CommandMetaWrapper : public CommandMetaBase {
  const CommandMeta<N> m_meta;

 public:
  CommandMetaWrapper(const CommandMeta<N>& meta) : m_meta(meta) {}

  std::string_view name() const override { return m_meta.name(); }
  std::string_view synopsis() const override { return m_meta.synopsis(); }
  std::string_view description() const override { return m_meta.description(); }
  std::string get_help() const override { return m_meta.get_help(); }
  std::string get_man() const override { return m_meta.get_man(); }
  std::string_view brief_desc() const override { return m_meta.brief_desc(); }

  std::span<const OptionMeta> options() const override {
    return m_meta.options();
  }
};

// Type-erased runtime handle
export class CommandMetaHandle {
  std::unique_ptr<CommandMetaBase> m_ptr;

 public:
  constexpr CommandMetaHandle() noexcept : m_ptr(nullptr) {}

  template <size_t N>
  CommandMetaHandle(const CommandMeta<N>& meta)
      : m_ptr(std::make_unique<CommandMetaWrapper<N> >(meta)) {}

  std::string_view name() const { return m_ptr->name(); }
  std::string_view synopsis() const { return m_ptr->synopsis(); }
  std::string_view description() const { return m_ptr->description(); }
  std::string get_help() const { return m_ptr->get_help(); }
  std::string get_man() const { return m_ptr->get_man(); }
  std::string_view brief_desc() const { return m_ptr->brief_desc(); }

  std::span<const OptionMeta> options() const { return m_ptr->options(); }
};

// Registry for metadata
export class Registry {
 public:
  template <size_t N>
  static void register_command(std::string_view command_name,
                               const CommandMeta<N>& meta) {
    get_storage()[command_name] = CommandMetaHandle(meta);
  }

  static bool print_help(std::string_view cmd_name) {
    auto& storage = get_storage();
    auto it = storage.find(cmd_name);
    if (it != storage.end()) {
      std::wstring whelp = utf8_to_wstring(it->second.get_help().c_str());
      safePrintLn(whelp);
      return true;
    }
    return false;
  }

  static std::string get_man(std::string_view cmd_name) {
    auto& storage = get_storage();
    auto it = storage.find(cmd_name);
    if (it != storage.end()) {
      return it->second.get_man();
    }
    return "";
  }

 private:
  static std::unordered_map<std::string_view, CommandMetaHandle>&
  get_storage() {
    static std::unordered_map<std::string_view, CommandMetaHandle> instance;
    return instance;
  }
};
}  // namespace cmd::meta
