/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Basic sed implementation with s/// substitutions
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;
import container;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr SED_OPTIONS = std::array{
    OPTION("-n", "--quiet", "suppress automatic printing of pattern space"),
    OPTION("", "--silent", "alias for -n"),
    OPTION("-s", "--separate",
           "consider files as separate rather than as a single continuous "
           "long stream"),
    OPTION("-z", "--null-data", "separate lines by NUL characters"),
    OPTION("", "--zero-terminated", "alias for -z"),
    OPTION("-u", "--unbuffered", "buffer input and output minimally"),
    OPTION("-b", "--binary", "open files in binary mode"),
    OPTION("-i", "--in-place", "edit files in place", OPTIONAL_STRING_TYPE),
    OPTION("-e", "--expression",
           "add the script to the commands to be executed", STRING_TYPE),
    OPTION("-f", "--file", "add the script from FILE", STRING_TYPE),
    OPTION("-l", "--line-length", "specify line-wrap length for the l command",
           INT_TYPE),
    OPTION("-E", "--regexp-extended", "use extended regular expressions"),
    OPTION("-r", "", "alias for -E")};

// ======================================================
// Pipeline components
// ======================================================
namespace sed_pipeline {
namespace cp = core::pipeline;

struct Script {
  enum class Kind {
    Subst,
    Print,
    Delete,
    Append,
    Insert,
    Change,
    Quit,
    LineNumber,
    List
  } kind;
  std::regex pattern;                     // for Subst
  std::string replacement;                // for Subst
  bool global = false;                    // for Subst
  size_t occurrence = 0;                  // for Subst; 0 means first/all
  bool print_on_match = false;            // for Subst
  bool invert_address = false;            // for address!
  std::string text;                       // for Append/Insert/Change
  std::array<unsigned char, 256> ymap{};  // for y///
  bool has_ymap = false;
  struct Address {
    enum class Kind { None, Line, Last, Regex, Step } kind = Kind::None;
    size_t line_no = 0;
    size_t step = 0;
    std::regex regex;
  } addr1, addr2;
};

struct Config {
  bool suppress_output = false;
  bool separate_files = false;
  char delimiter = '\n';
  bool in_place = false;
  std::string in_place_suffix;
  size_t list_line_length = 70;
  SmallVector<Script, 32> scripts;
  SmallVector<std::string, 64> files;
  std::regex_constants::syntax_option_type regex_syntax =
      std::regex_constants::basic;
};

struct ScriptState {
  bool range_active = false;
};

auto split_lines_string(const std::string& s) -> std::vector<std::string> {
  std::vector<std::string> out;
  out.reserve(s.size() / 20);  // Reserve for ~20 chars per line
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == '\n') {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

// parse s/pat/repl/flags
auto parse_subst(std::string_view expr,
                 std::regex_constants::syntax_option_type syntax)
    -> cp::Result<Script> {
  if (expr.size() < 4 || expr[0] != 's')
    return std::unexpected("unsupported script (only s///)");
  char delim = expr[1];
  size_t i = 2;
  std::string pat, repl;

  auto read_part = [&](std::string& out) -> cp::Result<void> {
    bool escape = false;
    for (; i < expr.size(); ++i) {
      char c = expr[i];
      if (escape) {
        if (c == delim) {
          out.push_back(c);
        } else {
          out.push_back('\\');
          out.push_back(c);
        }
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == delim) {
        ++i;
        return {};
      }
      out.push_back(c);
    }
    return std::unexpected("unterminated s command");
  };

  auto p1 = read_part(pat);
  if (!p1) return std::unexpected(p1.error());
  auto p2 = read_part(repl);
  if (!p2) return std::unexpected(p2.error());

  bool g = false, pflag = false, ignore_case = false;
  size_t occurrence = 0;
  for (; i < expr.size(); ++i) {
    char f = expr[i];
    if (f == 'g') {
      g = true;
    } else if (f == 'p') {
      pflag = true;
    } else if (f == 'I' || f == 'i') {
      ignore_case = true;
    } else if (std::isdigit(static_cast<unsigned char>(f)) != 0) {
      size_t start = i;
      while (i < expr.size() &&
             std::isdigit(static_cast<unsigned char>(expr[i])) != 0) {
        ++i;
      }
      auto value_text = expr.substr(start, i - start);
      auto [ptr, ec] = std::from_chars(
          value_text.data(), value_text.data() + value_text.size(), occurrence);
      if (ec != std::errc() || ptr != value_text.data() + value_text.size() ||
          occurrence == 0) {
        return std::unexpected("invalid occurrence in s command");
      }
      --i;
    } else if (f == ' ') {
      continue;
    } else {
      return std::unexpected("unknown flag in s command");
    }
  }

  try {
    Script s;
    s.kind = Script::Kind::Subst;
    auto effective_syntax = syntax;
    if (ignore_case) effective_syntax |= std::regex_constants::icase;
    s.pattern = std::regex(pat, effective_syntax);
    s.replacement = repl;
    s.global = g;
    s.occurrence = occurrence;
    s.print_on_match = pflag;
    return s;
  } catch (const std::regex_error&) {
    return std::unexpected("invalid regular expression");
  }
}

auto parse_simple_cmd(std::string_view line) -> cp::Result<Script> {
  if (line.empty()) return std::unexpected("empty script line");
  char c = line[0];
  std::string_view rest = line.substr(1);
  auto trim_space = [](std::string_view v) {
    size_t b = 0, e = v.size();
    while (b < e && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(v[e - 1]))) --e;
    return v.substr(b, e - b);
  };
  rest = trim_space(rest);
  if (c == 'p') return Script{Script::Kind::Print, {}, {}, false, false, {}};
  if (c == 'd') return Script{Script::Kind::Delete, {}, {}, false, false, {}};
  if (c == '=') {
    Script s;
    s.kind = Script::Kind::LineNumber;
    return s;
  }
  if (c == 'l') {
    Script s;
    s.kind = Script::Kind::List;
    return s;
  }
  if (c == 'q' || c == 'Q') {
    Script s;
    s.kind = Script::Kind::Quit;
    return s;
  }
  if (c == 'a') {
    Script s;
    s.kind = Script::Kind::Append;
    s.text = std::string(rest);
    return s;
  }
  if (c == 'i') {
    Script s;
    s.kind = Script::Kind::Insert;
    s.text = std::string(rest);
    return s;
  }
  if (c == 'c') {
    Script s;
    s.kind = Script::Kind::Change;
    s.text = std::string(rest);
    return s;
  }
  return std::unexpected("unsupported script command");
}

auto parse_y_cmd(std::string_view line) -> cp::Result<Script> {
  if (line.size() < 4 || line[0] != 'y')
    return std::unexpected("unsupported script (only y///)");
  char delim = line[1];
  size_t i = 2;
  std::string src, dst;
  auto read_part = [&](std::string& out) -> cp::Result<void> {
    bool escape = false;
    for (; i < line.size(); ++i) {
      char c = line[i];
      if (escape) {
        if (c == delim) {
          out.push_back(c);
        } else {
          out.push_back('\\');
          out.push_back(c);
        }
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == delim) {
        ++i;
        return {};
      }
      out.push_back(c);
    }
    return std::unexpected("unterminated y command");
  };
  auto p1 = read_part(src);
  if (!p1) return std::unexpected(p1.error());
  auto p2 = read_part(dst);
  if (!p2) return std::unexpected(p2.error());
  if (src.size() != dst.size())
    return std::unexpected("y command requires equal length strings");
  Script s;
  s.kind = Script::Kind::Subst;  // reused placeholder
  s.has_ymap = true;
  for (size_t k = 0; k < s.ymap.size(); ++k)
    s.ymap[k] = static_cast<unsigned char>(k);
  for (size_t k = 0; k < src.size(); ++k) {
    s.ymap[static_cast<unsigned char>(src[k])] =
        static_cast<unsigned char>(dst[k]);
  }
  return s;
}

auto parse_address(std::string_view line, size_t& i,
                   std::regex_constants::syntax_option_type syntax)
    -> cp::Result<Script::Address> {
  Script::Address addr;
  if (i >= line.size()) return addr;
  if (line[i] == '$') {
    ++i;
    addr.kind = Script::Address::Kind::Last;
    return addr;
  }
  if (std::isdigit(static_cast<unsigned char>(line[i]))) {
    size_t start = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
      ++i;
    auto first = std::stoul(std::string(line.substr(start, i - start)));
    if (i < line.size() && line[i] == '~') {
      ++i;
      size_t step_start = i;
      while (i < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[i]))) {
        ++i;
      }
      if (i == step_start) {
        return std::unexpected("invalid step address");
      }
      auto step =
          std::stoul(std::string(line.substr(step_start, i - step_start)));
      if (step == 0) {
        return std::unexpected("invalid step address");
      }
      addr.kind = Script::Address::Kind::Step;
      addr.line_no = first;
      addr.step = step;
      return addr;
    }
    addr.kind = Script::Address::Kind::Line;
    addr.line_no = first;
    return addr;
  }
  if (line[i] == '/') {
    ++i;
    std::string pat;
    bool escape = false;
    for (; i < line.size(); ++i) {
      char c = line[i];
      if (escape) {
        pat.push_back(c);
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == '/') {
        ++i;
        auto effective_syntax = syntax;
        if (i < line.size() && (line[i] == 'I' || line[i] == 'i')) {
          effective_syntax |= std::regex_constants::icase;
          ++i;
        }
        try {
          addr.kind = Script::Address::Kind::Regex;
          addr.regex = std::regex(pat, effective_syntax);
          return addr;
        } catch (const std::regex_error&) {
          return std::unexpected("invalid address regex");
        }
      }
      pat.push_back(c);
    }
    return std::unexpected("unterminated address regex");
  }
  return addr;
}

auto parse_script_line(std::string_view line,
                       std::regex_constants::syntax_option_type syntax)
    -> cp::Result<std::vector<Script>> {
  if (!line.empty() && (line.back() == '\r')) line.remove_suffix(1);
  if (line.empty()) return std::unexpected("empty script line");
  auto split_commands = [](std::string_view text) {
    std::vector<std::string> parts;
    std::string cur;
    bool escape = false;
    bool in_addr = false;
    bool in_sy = false;
    char sy_delim = '\0';
    int sy_parts = 0;
    int sy_need = 0;
    for (size_t i = 0; i < text.size(); ++i) {
      char c = text[i];
      if (escape) {
        cur.push_back(c);
        escape = false;
        continue;
      }
      if (c == '\\') {
        cur.push_back(c);
        escape = true;
        continue;
      }
      if (in_sy) {
        cur.push_back(c);
        if (c == sy_delim) {
          ++sy_parts;
          if (sy_parts >= sy_need) in_sy = false;
        }
        continue;
      }
      if (in_addr) {
        cur.push_back(c);
        if (c == '/') in_addr = false;
        continue;
      }
      if (c == '/') {
        cur.push_back(c);
        in_addr = true;
        continue;
      }
      auto cur_has_only_space = [&]() {
        for (char ch : cur) {
          if (!std::isspace(static_cast<unsigned char>(ch))) return false;
        }
        return true;
      };
      if ((c == 's' || c == 'y') && (cur.empty() || cur_has_only_space())) {
        cur.push_back(c);
        if (i + 1 < text.size()) {
          sy_delim = text[i + 1];
          in_sy = true;
          sy_parts = 0;
          sy_need = 3;
        }
        continue;
      }
      if (c == ';') {
        if (!cur.empty()) {
          parts.push_back(cur);
          cur.clear();
        }
        continue;
      }
      cur.push_back(c);
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
  };

  std::vector<Script> out;
  for (const auto& part : split_commands(line)) {
    size_t i = 0;
    while (i < part.size() && std::isspace(static_cast<unsigned char>(part[i])))
      ++i;
    Script::Address a1, a2;
    auto addr1 = parse_address(part, i, syntax);
    if (!addr1) return std::unexpected(addr1.error());
    a1 = *addr1;
    if (i < part.size() && part[i] == ',') {
      ++i;
      auto addr2 = parse_address(part, i, syntax);
      if (!addr2) return std::unexpected(addr2.error());
      a2 = *addr2;
    }
    while (i < part.size() && std::isspace(static_cast<unsigned char>(part[i])))
      ++i;
    bool invert_address = false;
    if (i < part.size() && part[i] == '!') {
      invert_address = true;
      ++i;
      while (i < part.size() &&
             std::isspace(static_cast<unsigned char>(part[i]))) {
        ++i;
      }
    }
    std::string_view cmd = std::string_view(part).substr(i);
    cp::Result<Script> s;
    if (!cmd.empty() && cmd[0] == 's')
      s = parse_subst(cmd, syntax);
    else if (!cmd.empty() && cmd[0] == 'y')
      s = parse_y_cmd(cmd);
    else
      s = parse_simple_cmd(cmd);
    if (!s) return std::unexpected(s.error());
    s->addr1 = a1;
    s->addr2 = a2;
    s->invert_address = invert_address;
    out.push_back(*s);
  }
  return out;
}

auto read_script_file(const std::string& path,
                      std::regex_constants::syntax_option_type syntax)
    -> cp::Result<std::vector<Script>> {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open())
    return std::unexpected("cannot open script file '" + path + "'");
  std::vector<Script> out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    auto s = parse_script_line(line, syntax);
    if (!s) return std::unexpected(s.error());
    out.insert(out.end(), s->begin(), s->end());
  }
  return out;
}

auto build_config(const CommandContext<SED_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.suppress_output = ctx.get<bool>("--quiet", false) ||
                        ctx.get<bool>("-n", false) ||
                        ctx.get<bool>("--silent", false);
  cfg.separate_files =
      ctx.get<bool>("--separate", false) || ctx.get<bool>("-s", false);
  if (ctx.get<bool>("--null-data", false) ||
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false)) {
    cfg.delimiter = '\0';
  }
  if (ctx.has("--in-place") || ctx.has("-i")) {
    cfg.in_place = true;
    for (const auto& occurrence :
         ctx.string_occurrences({"--in-place", "-i"})) {
      cfg.in_place_suffix = occurrence.value;
    }
  }

  if (ctx.get<bool>("--regexp-extended", false) || ctx.get<bool>("-E", false) ||
      ctx.get<bool>("-r", false)) {
    cfg.regex_syntax = std::regex_constants::ECMAScript;
  }

  if (ctx.has("--line-length") || ctx.has("-l")) {
    int line_length = ctx.get<int>("--line-length", ctx.get<int>("-l", 70));
    if (line_length < 0) {
      return std::unexpected("invalid line length");
    }
    cfg.list_line_length = static_cast<size_t>(line_length);
  }

  std::vector<Script> scripts;
  scripts.reserve(32);  // Reserve for reasonable number of scripts

  auto script_options =
      ctx.string_occurrences({"--expression", "-e", "--file", "-f"});
  for (const auto& occurrence : script_options) {
    if (occurrence.long_name == "--expression" ||
        occurrence.short_name == "-e") {
      auto lines = split_lines_string(occurrence.value);
      for (auto& e : lines) {
        if (e.empty()) continue;
        auto s = parse_script_line(e, cfg.regex_syntax);
        if (!s) return std::unexpected(s.error());
        scripts.insert(scripts.end(), s->begin(), s->end());
      }
    } else if (occurrence.long_name == "--file" ||
               occurrence.short_name == "-f") {
      auto fscripts = read_script_file(occurrence.value, cfg.regex_syntax);
      if (!fscripts) return std::unexpected(fscripts.error());
      scripts.insert(scripts.end(), fscripts->begin(), fscripts->end());
    }
  }

  size_t consumed_positional = 0;
  if (scripts.empty()) {
    if (ctx.positionals.empty()) return std::unexpected("script required");
    auto s = parse_script_line(ctx.positionals[0], cfg.regex_syntax);
    if (!s) return std::unexpected(s.error());
    scripts.insert(scripts.end(), s->begin(), s->end());
    consumed_positional = 1;
  }

  for (auto& s : scripts) cfg.scripts.push_back(std::move(s));

  for (size_t i = consumed_positional; i < ctx.positionals.size(); ++i) {
    cfg.files.emplace_back(ctx.positionals[i]);
  }
  if (cfg.files.empty()) cfg.files.emplace_back("-");
  return cfg;
}

auto expand_substitution_replacement(const std::string& replacement,
                                     const std::smatch& match) -> std::string {
  std::string out;
  out.reserve(replacement.size() + match.length(0));
  for (size_t i = 0; i < replacement.size(); ++i) {
    char c = replacement[i];
    if (c == '&') {
      out.append(match.str(0));
      continue;
    }
    if (c == '\\' && i + 1 < replacement.size()) {
      char next = replacement[++i];
      if (next >= '1' && next <= '9') {
        size_t group = static_cast<size_t>(next - '0');
        if (group < match.size() && match[group].matched) {
          out.append(match.str(group));
        }
      } else if (next == '&' || next == '\\') {
        out.push_back(next);
      } else {
        out.push_back(next);
      }
      continue;
    }
    out.push_back(c);
  }
  return out;
}

auto substitute_line(const std::string& input, const Script& script,
                     bool& changed) -> std::string {
  changed = false;
  std::string output;
  output.reserve(input.size());

  size_t search_start = 0;
  size_t last_append = 0;
  size_t match_index = 0;
  std::smatch match;
  std::string suffix = input;

  while (std::regex_search(suffix, match, script.pattern)) {
    ++match_index;
    size_t match_start = search_start + static_cast<size_t>(match.position(0));
    size_t match_end = match_start + static_cast<size_t>(match.length(0));
    const bool replace_this =
        script.occurrence == 0
            ? script.global || match_index == 1
            : match_index >= script.occurrence &&
                  (script.global || match_index == script.occurrence);

    if (replace_this) {
      output.append(input, last_append, match_start - last_append);
      output.append(expand_substitution_replacement(script.replacement, match));
      last_append = match_end;
      changed = true;
    }

    if (match.length(0) == 0) {
      if (match_end >= input.size()) break;
      search_start = match_end + 1;
    } else {
      search_start = match_end;
    }
    suffix = input.substr(search_start);

    if (script.occurrence == 0 && !script.global && changed) break;
    if (script.occurrence != 0 && !script.global &&
        match_index >= script.occurrence) {
      break;
    }
  }

  if (changed) {
    output.append(input, last_append, std::string::npos);
    return output;
  }
  return input;
}

auto list_escape_line(std::string_view input, size_t wrap_length)
    -> std::string {
  std::string output;
  output.reserve(input.size() + 8);
  size_t column = 0;

  auto append_visible = [&](std::string_view text) {
    for (char ch : text) {
      output.push_back(ch);
      ++column;
    }
  };

  auto append_wrapped = [&](std::string_view text) {
    if (wrap_length > 0 && column > 0 && column + text.size() > wrap_length) {
      output.push_back('\\');
      output.push_back('\n');
      column = 0;
    }
    append_visible(text);
  };

  for (unsigned char ch : input) {
    switch (ch) {
      case '\\':
        append_wrapped("\\\\");
        break;
      case '\a':
        append_wrapped("\\a");
        break;
      case '\b':
        append_wrapped("\\b");
        break;
      case '\f':
        append_wrapped("\\f");
        break;
      case '\r':
        append_wrapped("\\r");
        break;
      case '\t':
        append_wrapped("\\t");
        break;
      case '\v':
        append_wrapped("\\v");
        break;
      default:
        if (ch < 32 || ch == 127) {
          std::array<char, 5> escaped{};
          std::snprintf(escaped.data(), escaped.size(), "\\%03o", ch);
          append_wrapped(std::string_view(escaped.data(), 4));
        } else {
          char visible = static_cast<char>(ch);
          append_wrapped(std::string_view(&visible, 1));
        }
        break;
    }
  }

  if (wrap_length > 0 && column + 1 > wrap_length) {
    output.push_back('\\');
    output.push_back('\n');
  }
  output.push_back('$');
  return output;
}

auto apply_scripts(std::string_view line, const std::vector<Script>& scripts,
                   std::vector<ScriptState>& states, const Config& cfg,
                   size_t line_no, bool is_last, std::string& out_line,
                   bool& matched_any, bool& should_quit) -> bool {
  std::string current(line);
  matched_any = false;
  std::vector<std::string> early_output;
  early_output.reserve(4);
  std::vector<std::string> insert_before;
  insert_before.reserve(8);  // Reserve for reasonable number of insertions
  std::vector<std::string> append_after;
  append_after.reserve(
      8);  // Reserve for reasonable number of append operations
  bool deleted = false;
  bool explicit_print = false;
  should_quit = false;

  for (size_t idx = 0; idx < scripts.size(); ++idx) {
    const auto& s = scripts[idx];
    auto& state = states[idx];

    auto addr_match = [&](const Script::Address& a) -> bool {
      if (a.kind == Script::Address::Kind::None) return true;
      if (a.kind == Script::Address::Kind::Line) return line_no == a.line_no;
      if (a.kind == Script::Address::Kind::Last) return is_last;
      if (a.kind == Script::Address::Kind::Step) {
        return line_no >= a.line_no && (line_no - a.line_no) % a.step == 0;
      }
      return std::regex_search(current, a.regex);
    };

    bool apply = false;
    if (s.addr2.kind == Script::Address::Kind::None) {
      apply = addr_match(s.addr1);
    } else {
      if (!state.range_active) {
        if (addr_match(s.addr1)) {
          state.range_active = true;
          apply = true;
        }
      } else {
        apply = true;
        if (addr_match(s.addr2)) state.range_active = false;
      }
    }

    if (s.invert_address) apply = !apply;

    if (!apply) continue;

    switch (s.kind) {
      case Script::Kind::Subst: {
        if (s.has_ymap) {
          for (auto& ch : current) {
            ch = static_cast<char>(s.ymap[static_cast<unsigned char>(ch)]);
          }
        } else {
          bool changed = false;
          std::string replaced = substitute_line(current, s, changed);
          matched_any = matched_any || changed;
          current.swap(replaced);
          if (s.print_on_match && changed) explicit_print = true;
        }
        break;
      }
      case Script::Kind::Print:
        explicit_print = true;
        break;
      case Script::Kind::Delete:
        deleted = true;
        break;
      case Script::Kind::Quit:
        should_quit = true;
        break;
      case Script::Kind::Insert:
        insert_before.push_back(s.text);
        break;
      case Script::Kind::Append:
        append_after.push_back(s.text);
        break;
      case Script::Kind::Change:
        current = s.text;
        break;
      case Script::Kind::LineNumber:
        early_output.push_back(std::to_string(line_no));
        break;
      case Script::Kind::List:
        early_output.push_back(list_escape_line(current, cfg.list_line_length));
        break;
    }
    if (deleted || should_quit) break;
  }

  std::string output;
  for (auto& e : early_output) {
    output.append(e);
    output.push_back(cfg.delimiter);
  }
  for (auto& b : insert_before) {
    output.append(b);
    output.push_back('\n');
  }

  if (!deleted && (!cfg.suppress_output || explicit_print)) {
    output.append(current);
    output.push_back(cfg.delimiter);
  }

  if (deleted && explicit_print) {
    output.append(current);
    output.push_back(cfg.delimiter);
  }

  for (auto& a : append_after) {
    output.append(a);
    output.push_back('\n');
  }

  if (!output.empty() && output.back() == cfg.delimiter) output.pop_back();
  out_line.swap(output);
  return !out_line.empty();
}

auto process_stream(std::istream& in, const Config& cfg,
                    std::vector<ScriptState>& states, size_t& line_no,
                    bool final_input, std::string* capture) -> bool {
  std::vector<Script> scripts_vec(cfg.scripts.begin(), cfg.scripts.end());
  std::string line;
  while (std::getline(in, line, cfg.delimiter)) {
    std::string out_line;
    bool matched_any = false;
    bool should_quit = false;
    bool is_last = final_input && in.peek() == EOF;
    bool should_print =
        apply_scripts(line, scripts_vec, states, cfg, line_no, is_last,
                      out_line, matched_any, should_quit);
    if (should_print) {
      if (capture) {
        capture->append(out_line);
        capture->push_back(cfg.delimiter);
      } else {
        safePrint(out_line);
        safePrint(std::string_view(&cfg.delimiter, 1));
      }
    }
    if (should_quit) return true;
    ++line_no;
  }
  return false;
}

auto make_in_place_backup_path(const std::string& path,
                               const std::string& suffix)
    -> std::filesystem::path {
  if (suffix.find('*') == std::string::npos) {
    return std::filesystem::path(path + suffix);
  }

  std::string backup;
  backup.reserve(suffix.size() + path.size());
  for (char ch : suffix) {
    if (ch == '*') {
      backup.append(path);
    } else {
      backup.push_back(ch);
    }
  }
  return std::filesystem::path(backup);
}

auto preserve_in_place_backup(const std::filesystem::path& original,
                              const std::string& suffix) -> bool {
  if (suffix.empty()) return true;

  auto backup = make_in_place_backup_path(original.string(), suffix);
  std::error_code ec;
  std::filesystem::copy_file(
      original, backup, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    safeErrorPrint("sed: cannot create backup '" + backup.string() + "'\n");
    return false;
  }
  return true;
}

auto replace_file_atomically(const std::string& path,
                             const std::string& backup_suffix,
                             const std::string& content) -> bool {
  auto original = std::filesystem::path(path);
  auto suffix =
      std::string(".winuxtmp.") + std::to_string(GetCurrentProcessId());
  auto temp = std::filesystem::path(path + suffix);
  auto backup = std::filesystem::path(path + suffix + ".bak");

  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      safeErrorPrint("sed: cannot create temporary file for '" + path + "'\n");
      return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good()) {
      safeErrorPrint("sed: cannot write temporary file for '" + path + "'\n");
      std::error_code cleanup_ec;
      std::filesystem::remove(temp, cleanup_ec);
      return false;
    }
  }

  if (!preserve_in_place_backup(original, backup_suffix)) {
    std::error_code cleanup_ec;
    std::filesystem::remove(temp, cleanup_ec);
    return false;
  }

  std::error_code ec;
  std::filesystem::remove(backup, ec);
  ec.clear();
  std::filesystem::rename(original, backup, ec);
  if (ec) {
    safeErrorPrint("sed: cannot replace '" + path + "'\n");
    std::error_code cleanup_ec;
    std::filesystem::remove(temp, cleanup_ec);
    return false;
  }

  std::filesystem::rename(temp, original, ec);
  if (ec) {
    std::error_code restore_ec;
    std::filesystem::rename(backup, original, restore_ec);
    safeErrorPrint("sed: cannot replace '" + path + "'\n");
    return false;
  }

  std::filesystem::remove(backup, ec);
  return true;
}

auto process_files(const Config& cfg) -> int {
  // Expand wildcards in file arguments
  std::vector<std::string> expanded_files;
  for (const auto& f : cfg.files) {
    if (f == "-") {
      expanded_files.push_back(f);
      continue;
    }

    // Smart glob expansion for wildcard patterns
    if (contains_wildcard(f)) {
      auto glob_result = glob_expand(f);
      if (glob_result.expanded) {
        // Pattern was expanded, add all matched files
        for (const auto& file : glob_result.files) {
          expanded_files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }

    // Not a wildcard or expansion failed, use as-is
    expanded_files.push_back(f);
  }

  bool any_error = false;
  std::vector<ScriptState> states(cfg.scripts.size());
  size_t line_no = 1;
  for (size_t file_index = 0; file_index < expanded_files.size();
       ++file_index) {
    const auto& f = expanded_files[file_index];
    if (cfg.in_place && f == "-") {
      safeErrorPrint("sed: cannot edit standard input in place\n");
      any_error = true;
      continue;
    }

    std::ifstream file;
    std::istream* in = nullptr;
    if (f == "-") {
      in = &std::cin;
    } else {
      file.open(f, std::ios::binary);
      if (!file.is_open()) {
        safeErrorPrint("sed: cannot open '" + f + "'\n");
        any_error = true;
        continue;
      }
      in = &file;
    }

    if (cfg.separate_files || cfg.in_place) {
      states.assign(cfg.scripts.size(), {});
      line_no = 1;
    }

    const bool final_input =
        cfg.separate_files || (file_index + 1 == expanded_files.size());

    if (cfg.in_place) {
      std::string output;
      bool should_quit =
          process_stream(*in, cfg, states, line_no, true, &output);
      file.close();
      if (!replace_file_atomically(f, cfg.in_place_suffix, output)) {
        any_error = true;
      }
      if (should_quit) break;
      continue;
    }

    if (process_stream(*in, cfg, states, line_no, final_input, nullptr))
      return any_error ? 1 : 0;
  }
  return any_error ? 1 : 0;
}

}  // namespace sed_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    sed, "sed", "sed [OPTION]... {script} [FILE]...",
    "Apply basic sed scripts (s///, p, d, a, i, c) to each line of input.",
    "  sed \"s/foo/bar/\" file.txt\n"
    "  sed -n \"s/foo/bar/p\" file.txt",
    "grep, awk", "WinuxCmd", "Copyright © 2026 WinuxCmd", SED_OPTIONS) {
  using namespace sed_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"sed");
    return 1;
  }

  return process_files(*cfg);
}
