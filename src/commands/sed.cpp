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
    OPTION("-i", "--in-place", "edit files in place"),
    OPTION("-e", "--expression",
           "add the script to the commands to be executed", STRING_TYPE),
    OPTION("-f", "--file", "add the script from FILE", STRING_TYPE),
    OPTION("-E", "--regexp-extended", "use extended regular expressions"),
    OPTION("-r", "", "alias for -E")};

// ======================================================
// Pipeline components
// ======================================================
namespace sed_pipeline {
namespace cp = core::pipeline;

struct Script {
  enum class Kind { Subst, Print, Delete, Append, Insert, Change, Quit } kind;
  std::regex pattern;                     // for Subst
  std::string replacement;                // for Subst
  bool global = false;                    // for Subst
  bool print_on_match = false;            // for Subst
  bool ecma_repl = false;                 // use $1 style in replacement
  std::string text;                       // for Append/Insert/Change
  std::array<unsigned char, 256> ymap{};  // for y///
  bool has_ymap = false;
  struct Address {
    enum class Kind { None, Line, Last, Regex } kind = Kind::None;
    size_t line_no = 0;
    std::regex regex;
  } addr1, addr2;
};

struct Config {
  bool suppress_output = false;
  bool in_place = false;
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

  bool g = false, pflag = false;
  for (; i < expr.size(); ++i) {
    char f = expr[i];
    if (f == 'g')
      g = true;
    else if (f == 'p')
      pflag = true;
    else if (f == ' ')
      continue;
    else
      return std::unexpected("unknown flag in s command");
  }

  try {
    Script s;
    s.kind = Script::Kind::Subst;
    s.pattern = std::regex(pat, syntax);
    s.replacement = repl;
    s.global = g;
    s.print_on_match = pflag;
    s.ecma_repl = (syntax == std::regex_constants::ECMAScript);
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
    addr.kind = Script::Address::Kind::Line;
    addr.line_no = std::stoul(std::string(line.substr(start, i - start)));
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
        try {
          addr.kind = Script::Address::Kind::Regex;
          addr.regex = std::regex(pat, syntax);
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
  cfg.in_place =
      ctx.get<bool>("--in-place", false) || ctx.get<bool>("-i", false);

  if (ctx.get<bool>("--regexp-extended", false) || ctx.get<bool>("-E", false) ||
      ctx.get<bool>("-r", false)) {
    cfg.regex_syntax = std::regex_constants::ECMAScript;
  }

  std::vector<Script> scripts;
  scripts.reserve(32);  // Reserve for reasonable number of scripts

  auto expr_opt = ctx.get<std::string>("--expression", "");
  if (!expr_opt.empty()) {
    auto lines = split_lines_string(expr_opt);
    for (auto& e : lines) {
      if (e.empty()) continue;
      auto s = parse_script_line(e, cfg.regex_syntax);
      if (!s) return std::unexpected(s.error());
      scripts.insert(scripts.end(), s->begin(), s->end());
    }
  }

  auto file_opt = ctx.get<std::string>("--file", "");
  if (file_opt.empty()) file_opt = ctx.get<std::string>("-f", "");
  if (!file_opt.empty()) {
    auto fscripts = read_script_file(file_opt, cfg.regex_syntax);
    if (!fscripts) return std::unexpected(fscripts.error());
    scripts.insert(scripts.end(), fscripts->begin(), fscripts->end());
  }

  size_t consumed_positional = 0;
  if (scripts.empty()) {
    if (ctx.positionals.empty()) return std::unexpected("script required");
    size_t script_end =
        ctx.positionals.size() >= 2 ? ctx.positionals.size() - 1 : 1;
    for (size_t i = 0; i < script_end; ++i) {
      auto s = parse_script_line(ctx.positionals[i], cfg.regex_syntax);
      if (!s) return std::unexpected(s.error());
      scripts.insert(scripts.end(), s->begin(), s->end());
    }
    consumed_positional = script_end;
  }

  for (auto& s : scripts) cfg.scripts.push_back(std::move(s));

  for (size_t i = consumed_positional; i < ctx.positionals.size(); ++i) {
    cfg.files.emplace_back(ctx.positionals[i]);
  }
  if (cfg.files.empty()) cfg.files.emplace_back("-");
  return cfg;
}

auto apply_scripts(std::string_view line, const std::vector<Script>& scripts,
                   std::vector<ScriptState>& states, size_t line_no,
                   bool suppress, bool is_last, std::string& out_line,
                   bool& matched_any, bool& should_quit) -> bool {
  std::string current(line);
  matched_any = false;
  std::vector<std::string> insert_before;
  insert_before.reserve(8);  // Reserve for reasonable number of insertions
  std::vector<std::string> append_after;
  append_after.reserve(
      8);  // Reserve for reasonable number of append operations
  bool deleted = false;
  bool explicit_print = false;
  should_quit = false;

  auto to_ecma_repl = [](const std::string& in) -> std::string {
    std::string out;
    out.reserve(in.size());
    out.reserve(in.size());
    bool escape = false;
    for (size_t i = 0; i < in.size(); ++i) {
      char c = in[i];
      if (escape) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
          out.push_back('$');
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
      out.push_back(c);
    }
    if (escape) out.push_back('\\');
    return out;
  };

  for (size_t idx = 0; idx < scripts.size(); ++idx) {
    const auto& s = scripts[idx];
    auto& state = states[idx];

    auto addr_match = [&](const Script::Address& a) -> bool {
      if (a.kind == Script::Address::Kind::None) return true;
      if (a.kind == Script::Address::Kind::Line) return line_no == a.line_no;
      if (a.kind == Script::Address::Kind::Last) return is_last;
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

    if (!apply) continue;

    switch (s.kind) {
      case Script::Kind::Subst: {
        if (s.has_ymap) {
          for (auto& ch : current) {
            ch = static_cast<char>(s.ymap[static_cast<unsigned char>(ch)]);
          }
        } else {
          std::regex_constants::match_flag_type mflags =
              std::regex_constants::format_default;
          if (!s.global) mflags |= std::regex_constants::format_first_only;
          std::string repl =
              s.ecma_repl ? to_ecma_repl(s.replacement) : s.replacement;
          std::string replaced =
              std::regex_replace(current, s.pattern, repl, mflags);
          bool changed = (replaced != current);
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
    }
    if (deleted || should_quit) break;
  }

  std::string output;
  for (auto& b : insert_before) {
    output.append(b);
    output.push_back('\n');
  }

  if (!deleted && (!suppress || explicit_print)) {
    output.append(current);
    output.push_back('\n');
  }

  if (deleted && explicit_print) {
    output.append(current);
    output.push_back('\n');
  }

  for (auto& a : append_after) {
    output.append(a);
    output.push_back('\n');
  }

  if (!output.empty() && output.back() == '\n') output.pop_back();
  out_line.swap(output);
  return !out_line.empty();
}

auto process_stream(std::istream& in, const Config& cfg, std::string* capture)
    -> bool {
  const char delim = '\n';
  size_t line_no = 1;
  std::vector<ScriptState> states(cfg.scripts.size());
  std::vector<Script> scripts_vec(cfg.scripts.begin(), cfg.scripts.end());
  std::string line;
  while (std::getline(in, line)) {
    std::string out_line;
    bool matched_any = false;
    bool should_quit = false;
    bool is_last = false;
    if (in.peek() == EOF) is_last = true;
    bool should_print =
        apply_scripts(line, scripts_vec, states, line_no, cfg.suppress_output,
                      is_last, out_line, matched_any, should_quit);
    if (should_print) {
      if (capture) {
        capture->append(out_line);
        capture->push_back(delim);
      } else {
        safePrint(out_line);
        safePrint(std::string_view(&delim, 1));
      }
    }
    if (should_quit) return true;
    ++line_no;
  }
  return false;
}

auto replace_file_atomically(const std::string& path,
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
  for (const auto& f : expanded_files) {
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

    if (cfg.in_place) {
      std::string output;
      bool should_quit = process_stream(*in, cfg, &output);
      file.close();
      if (!replace_file_atomically(f, output)) {
        any_error = true;
      }
      if (should_quit) break;
      continue;
    }

    if (process_stream(*in, cfg, nullptr)) return any_error ? 1 : 0;
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
