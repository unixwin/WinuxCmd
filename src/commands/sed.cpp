/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Basic sed implementation with s/// substitutions
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h
#include <cerrno>
#include <cstring>

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
    // [GNU]
    OPTION("-n", "--quiet", "suppress automatic printing of pattern space"),
    // [GNU]
    OPTION("", "--silent", "alias for -n"),
    // [GNU]
    OPTION("-s", "--separate",
           "consider files as separate rather than as a single continuous "
           "long stream"),
    // [GNU]
    OPTION("-z", "--null-data", "separate lines by NUL characters"),
    // [GNU]
    OPTION("", "--zero-terminated", "alias for -z"),
    // [GNU]
    OPTION("-u", "--unbuffered", "buffer input and output minimally"),
    // [DIFFERS]
    OPTION("-b", "--binary", "open files in binary mode"),
    // [GNU]
    OPTION("-i", "--in-place", "edit files in place", OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-e", "--expression",
           "add the script to the commands to be executed", STRING_TYPE),
    // [GNU]
    OPTION("-f", "--file", "add the script from FILE", STRING_TYPE),
    // [GNU]
    OPTION("-l", "--line-length", "specify line-wrap length for the l command",
           INT_TYPE),
    // [GNU]
    OPTION("-E", "--regexp-extended", "use extended regular expressions"),
    // [DIFFERS] accepted for GNU compatibility; no execution annotation is
    // produced on this platform
    OPTION("", "--debug", "annotate program execution (accepted, no output)"),
    // [DIFFERS] accepted for GNU compatibility; symlinks are always opened
    // directly on this platform
    OPTION("", "--follow-symlinks",
           "follow symlinks when processing in place (accepted, no-op)"),
    // [GNU]
    OPTION("", "--sandbox", "restrict file system access in the script"),
    // [GNU]
    OPTION("", "--posix", "disable GNU extensions and follow POSIX sed"),
    // [GNU]
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
    PrintFirst,
    Delete,
    DeleteFirst,
    Append,
    Insert,
    Change,
    Quit,
    QuitSilent,
    LineNumber,
    PrintFilename,
    List,
    Next,
    AppendNext,
    HoldCopyToPattern,
    HoldAppendToPattern,
    PatternCopyToHold,
    PatternAppendToHold,
    Exchange,
    Label,
    Branch,
    TestBranch,
    TestNoBranch,
    ReadFile,
    ReadFileLine,
    WriteFile,
    WriteFirstFile,
    ClearPattern
  } kind;
  portable_regex::Pattern pattern;  // for Subst
  // [GNU] s///m and s///M modifiers (REG_NEWLINE): ^ and $ also match at
  // newlines inside the pattern space. The bundled regex engine anchors ^/$
  // only at string edges, so the anchors are stripped at compile time and
  // validated manually during matching.
  portable_regex::Pattern ml_pattern;  // pattern with stripped anchors
  bool ml_caret = false;               // pattern started with unescaped ^
  bool ml_dollar = false;              // pattern ended with unescaped $
  std::string replacement;             // for Subst
  bool global = false;                 // for Subst
  size_t occurrence = 0;               // for Subst; 0 means first/all
  bool print_on_match = false;         // for Subst
  std::string subst_write_file;        // for s///w FILE
  int quit_exit_code = 0;              // for q/Q
  size_t list_line_length = std::numeric_limits<size_t>::max();  // for l
  bool invert_address = false;                                   // for address!
  std::string text;   // for Append/Insert/Change
  std::string label;  // for :/b/t/T
  size_t jump_index = std::numeric_limits<size_t>::max();  // for b/t/T
  std::string file_path;                                   // for r/R/w/W
  std::array<unsigned char, 256> ymap{};                   // for y///
  bool has_ymap = false;
  struct Address {
    enum class Kind {
      None,
      Line,
      Last,
      Regex,
      Step,
      Relative,
      Modulo
    } kind = Kind::None;
    size_t line_no = 0;
    size_t step = 0;
    portable_regex::Pattern regex;
  } addr1, addr2;
  struct GroupAddress {
    Address addr1, addr2;
    bool invert = false;
  };
  std::vector<GroupAddress>
      group_addresses;            // addresses inherited from { } groups
  size_t group_state_offset = 0;  // first ctx.states slot owned by this script
  std::string literal_pattern;    // for fast literal s/// substitutions
  bool literal_substitution = false;
};

struct Config {
  bool suppress_output = false;
  bool separate_files = false;
  bool sandbox = false;
  bool posix = false;
  char delimiter = '\n';
  bool in_place = false;
  std::string in_place_suffix;
  size_t list_line_length = 70;
  SmallVector<Script, 32> scripts;
  SmallVector<std::string, 64> files;
  portable_regex::Syntax regex_syntax = portable_regex::Syntax::Basic;
};

struct ScriptState {
  bool range_active = false;
  bool range_closed = false;
  size_t range_end_line = 0;
};

struct LastRegex {
  std::string pattern;
  bool ignore_case = false;
  bool valid = false;
};

struct ParseContext {
  LastRegex last_regex;
  bool at_script_start = true;
  bool magic_silent = false;
  bool posix = false;
};

auto trim_left_space(std::string_view v) -> std::string_view {
  size_t b = 0;
  while (b < v.size() && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
  return v.substr(b);
}

auto strip_trailing_cr(std::string_view v) -> std::string_view {
  if (!v.empty() && v.back() == '\r') v.remove_suffix(1);
  return v;
}

auto split_script_lines(std::string_view s) -> std::vector<std::string> {
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

auto normalize_text_command_body(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c != '\\' || i + 1 >= text.size()) {
      out.push_back(c);
      continue;
    }

    char next = text[++i];
    switch (next) {
      case 'a':
        out.push_back('\a');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'v':
        out.push_back('\v');
        break;
      default:
        out.push_back(next);
        break;
    }
  }
  return out;
}

// [GNU] Backslash escapes inside the REGEXP of s/// are decoded to their
// control characters before the regex is compiled
// (sed-4.9/sed/compile.c:1449-1465, parse_regex -> compile_regex): \a \f \n
// \r \t \v become BEL FF LF CR TAB VT, so `s/\r//g` matches the CR the
// replacement side writes with `s/$/\r/`. Unknown escapes keep the
// backslash and stay byte-identical to the historical pattern text: the
// regex engine resolves them (\\ still matches a literal backslash), which
// mirrors GNU handing unrecognized escapes to the regex compiler verbatim.
// Escaped delimiters were already unescaped by read_part.
auto normalize_pattern_escapes(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c != '\\' || i + 1 >= text.size()) {
      out.push_back(c);
      continue;
    }
    char next = text[++i];
    switch (next) {
      case 'a':
        out.push_back('\a');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'v':
        out.push_back('\v');
        break;
      default:
        out.push_back('\\');
        out.push_back(next);
        break;
    }
  }
  return out;
}

auto has_text_line_continuation(std::string_view text) -> bool {
  size_t slash_count = 0;
  for (size_t i = text.size(); i > 0 && text[i - 1] == '\\'; --i) {
    ++slash_count;
  }
  return (slash_count % 2) == 1;
}

// Defined below with the rest of the script splitter; declared here so the
// a/i/c text commands in parse_simple_cmd can normalize multi-line bodies.
auto normalize_text_body_full(std::string_view raw) -> std::string;

auto is_literal_substitution_pattern(std::string_view pattern,
                                     portable_regex::Syntax syntax) -> bool {
  if (pattern.empty()) return false;

  const std::string_view bre_meta = ".^$*[\\";
  const std::string_view ere_meta = ".^$*+?()[{|\\";
  const auto meta =
      syntax == portable_regex::Syntax::Extended ? ere_meta : bre_meta;
  for (char ch : pattern) {
    if (meta.find(ch) != std::string_view::npos) return false;
  }
  return true;
}

auto is_literal_replacement(std::string_view replacement) -> bool {
  return replacement.find('&') == std::string_view::npos &&
         replacement.find('\\') == std::string_view::npos;
}

// parse s/pat/repl/flags
auto parse_subst(std::string_view expr, portable_regex::Syntax syntax,
                 ParseContext& parse_context) -> cp::Result<Script> {
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
  // [GNU] Decode regexp-side control escapes before any downstream scanning
  // (anchor detection, literal fast path, last-regex cache) inspects the
  // pattern, so `s/\r//g` and `s/$/\r/` see the same CR byte.
  pat = normalize_pattern_escapes(pat);

  bool g = false, pflag = false, ignore_case = false, multiline = false;
  size_t occurrence = 0;
  std::string write_file;
  for (; i < expr.size(); ++i) {
    char f = expr[i];
    if (f == 'g') {
      g = true;
    } else if (f == 'p') {
      pflag = true;
    } else if (f == 'I' || f == 'i') {
      if (parse_context.posix) {
        return std::unexpected("POSIX sed rejects GNU substitution modifiers");
      }
      ignore_case = true;
    } else if (f == 'm' || f == 'M') {
      // [GNU] multi-line modifier: ^ and $ also match at embedded newlines
      if (parse_context.posix) {
        return std::unexpected("POSIX sed rejects GNU substitution modifiers");
      }
      multiline = true;
    } else if (f == 'e') {
      // [DIFFERS] shell evaluation of the pattern space is intentionally not
      // supported; GNU accepts this modifier (sed-4.9/sed/compile.c:1049).
      return std::unexpected(
          "the 'e' flag to the s command is not supported by this sed "
          "implementation");
    } else if (f == 'w') {
      std::string_view path = trim_left_space(expr.substr(i + 1));
      if (path.empty()) {
        return std::unexpected("missing filename in s command w flag");
      }
      write_file = std::string(path);
      break;
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

  bool effective_ignore_case = ignore_case;
  if (pat.empty()) {
    if (ignore_case) {
      return std::unexpected("cannot specify modifiers on empty regexp");
    }
    if (!parse_context.last_regex.valid) {
      return std::unexpected("no previous regular expression");
    }
    pat = parse_context.last_regex.pattern;
    effective_ignore_case = parse_context.last_regex.ignore_case;
  } else {
    parse_context.last_regex.pattern = pat;
    parse_context.last_regex.ignore_case = ignore_case;
    parse_context.last_regex.valid = true;
  }

  auto compiled = portable_regex::compile(syntax, pat, effective_ignore_case);
  if (!compiled) {
    return std::unexpected("invalid regular expression");
  }

  Script s;
  s.kind = Script::Kind::Subst;
  s.pattern = std::move(compiled.pattern);
  if (multiline) {
    // [GNU] REG_NEWLINE anchors: strip the leading ^ and trailing unescaped $
    // so the matcher can validate them at embedded newlines.
    std::string ml = pat;
    if (!ml.empty() && ml.front() == '^') {
      s.ml_caret = true;
      ml.erase(ml.begin());
    }
    if (!ml.empty() && ml.back() == '$') {
      // unescaped unless preceded by an odd run of backslashes
      size_t backslashes = 0;
      while (backslashes < ml.size() - 1 &&
             ml[ml.size() - 2 - backslashes] == '\\') {
        ++backslashes;
      }
      if (backslashes % 2 == 0) {
        s.ml_dollar = true;
        ml.pop_back();
      }
    }
    if (s.ml_caret || s.ml_dollar) {
      auto ml_compiled =
          portable_regex::compile(syntax, ml, effective_ignore_case);
      if (!ml_compiled) {
        return std::unexpected("invalid regular expression");
      }
      s.ml_pattern = std::move(ml_compiled.pattern);
    }
  }
  s.replacement = repl;
  s.global = g;
  s.occurrence = occurrence;
  s.print_on_match = pflag;
  s.subst_write_file = std::move(write_file);
  if (!effective_ignore_case && is_literal_substitution_pattern(pat, syntax) &&
      is_literal_replacement(repl)) {
    s.literal_pattern = std::move(pat);
    s.literal_substitution = true;
  }
  return s;
}

auto parse_simple_cmd(std::string_view line) -> cp::Result<Script> {
  if (line.empty()) return std::unexpected("empty script line");
  char c = line[0];
  std::string_view rest = line.substr(1);
  auto text_body = [](std::string_view v) -> std::string {
    return normalize_text_body_full(v);
  };
  auto no_extra = [&](std::string_view command_name) -> cp::Result<void> {
    if (!trim_left_space(rest).empty()) {
      return std::unexpected(std::string("extra characters after ") +
                             std::string(command_name) + " command");
    }
    return {};
  };
  auto label_body = [](std::string_view v) -> std::string {
    v = trim_left_space(v);
    size_t end = 0;
    while (end < v.size() &&
           std::isspace(static_cast<unsigned char>(v[end])) == 0 &&
           v[end] != ';' && v[end] != '}' && v[end] != '#') {
      ++end;
    }
    return std::string(v.substr(0, end));
  };
  auto file_body = [](std::string_view v) -> std::string {
    v = trim_left_space(v);
    return std::string(v);
  };
  if (c == 'p') {
    if (auto ok = no_extra("p"); !ok) return std::unexpected(ok.error());
    Script s;
    s.kind = Script::Kind::Print;
    return s;
  }
  if (c == 'P') {
    if (auto ok = no_extra("P"); !ok) return std::unexpected(ok.error());
    Script s;
    s.kind = Script::Kind::PrintFirst;
    return s;
  }
  if (c == 'd') {
    if (auto ok = no_extra("d"); !ok) return std::unexpected(ok.error());
    Script s;
    s.kind = Script::Kind::Delete;
    return s;
  }
  if (c == 'D') {
    if (auto ok = no_extra("D"); !ok) return std::unexpected(ok.error());
    Script s;
    s.kind = Script::Kind::DeleteFirst;
    return s;
  }
  if (c == '=') {
    if (auto ok = no_extra("="); !ok) return std::unexpected(ok.error());
    Script s;
    s.kind = Script::Kind::LineNumber;
    return s;
  }
  if (c == 'F') {
    if (auto ok = no_extra("F"); !ok) return std::unexpected(ok.error());
    Script s;
    s.kind = Script::Kind::PrintFilename;
    return s;
  }
  if (c == 'l') {
    Script s;
    s.kind = Script::Kind::List;
    rest = trim_left_space(rest);
    if (!rest.empty()) {
      size_t line_length = 0;
      auto [ptr, ec] =
          std::from_chars(rest.data(), rest.data() + rest.size(), line_length);
      if (ec != std::errc() || ptr != rest.data() + rest.size()) {
        return std::unexpected("invalid l command line length");
      }
      s.list_line_length = line_length;
    }
    return s;
  }
  if (c == 'n' || c == 'N') {
    if (auto ok = no_extra(std::string_view(&c, 1)); !ok)
      return std::unexpected(ok.error());
    Script s;
    s.kind = c == 'n' ? Script::Kind::Next : Script::Kind::AppendNext;
    return s;
  }
  if (c == 'g' || c == 'G' || c == 'h' || c == 'H' || c == 'x' || c == 'z') {
    if (auto ok = no_extra(std::string_view(&c, 1)); !ok)
      return std::unexpected(ok.error());
    Script s;
    if (c == 'g') {
      s.kind = Script::Kind::HoldCopyToPattern;
    } else if (c == 'G') {
      s.kind = Script::Kind::HoldAppendToPattern;
    } else if (c == 'h') {
      s.kind = Script::Kind::PatternCopyToHold;
    } else if (c == 'H') {
      s.kind = Script::Kind::PatternAppendToHold;
    } else if (c == 'x') {
      s.kind = Script::Kind::Exchange;
    } else {
      s.kind = Script::Kind::ClearPattern;
    }
    return s;
  }
  if (c == ':') {
    Script s;
    s.kind = Script::Kind::Label;
    s.label = label_body(rest);
    if (s.label.empty()) return std::unexpected(": command lacks a label");
    return s;
  }
  if (c == 'b' || c == 't' || c == 'T') {
    Script s;
    s.kind = c == 'b'   ? Script::Kind::Branch
             : c == 't' ? Script::Kind::TestBranch
                        : Script::Kind::TestNoBranch;
    s.label = label_body(rest);
    return s;
  }
  if (c == 'r' || c == 'R' || c == 'w' || c == 'W') {
    Script s;
    s.kind = c == 'r'   ? Script::Kind::ReadFile
             : c == 'R' ? Script::Kind::ReadFileLine
             : c == 'w' ? Script::Kind::WriteFile
                        : Script::Kind::WriteFirstFile;
    s.file_path = file_body(rest);
    if (s.file_path.empty()) {
      return std::unexpected("missing filename in r/R/w/W command");
    }
    return s;
  }
  if (c == 'q' || c == 'Q') {
    Script s;
    s.kind = c == 'Q' ? Script::Kind::QuitSilent : Script::Kind::Quit;
    rest = trim_left_space(rest);
    if (!rest.empty()) {
      unsigned int exit_code = 0;
      auto [ptr, ec] =
          std::from_chars(rest.data(), rest.data() + rest.size(), exit_code);
      if (ec != std::errc() || ptr != rest.data() + rest.size()) {
        return std::unexpected("invalid q command exit code");
      }
      s.quit_exit_code = static_cast<int>(exit_code & 0xFFu);
    }
    return s;
  }
  if (c == 'a') {
    Script s;
    s.kind = Script::Kind::Append;
    s.text = text_body(rest);
    return s;
  }
  if (c == 'i') {
    Script s;
    s.kind = Script::Kind::Insert;
    s.text = text_body(rest);
    return s;
  }
  if (c == 'c') {
    Script s;
    s.kind = Script::Kind::Change;
    s.text = text_body(rest);
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
  // [GNU] normalize_text() processes backslash escapes inside both y/// lists
  // before the translate map is built (sed-4.9/sed/compile.c:1415-1446): \n,
  // \t and friends become their control characters and \\ collapses to a
  // single backslash. Escaped delimiters were already unescaped by read_part.
  src = normalize_text_command_body(src);
  dst = normalize_text_command_body(dst);
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

auto parse_unsigned_at(std::string_view text, size_t& i) -> cp::Result<size_t> {
  size_t start = i;
  while (i < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
    ++i;
  }
  if (i == start) return std::unexpected("expected number");

  size_t value = 0;
  auto value_text = text.substr(start, i - start);
  auto [ptr, ec] = std::from_chars(
      value_text.data(), value_text.data() + value_text.size(), value);
  if (ec != std::errc() || ptr != value_text.data() + value_text.size()) {
    return std::unexpected("invalid number");
  }
  return value;
}

auto parse_address(std::string_view line, size_t& i,
                   portable_regex::Syntax syntax, ParseContext& parse_context,
                   bool allow_range_extension = false)
    -> cp::Result<Script::Address> {
  Script::Address addr;
  if (i >= line.size()) return addr;
  if (allow_range_extension && (line[i] == '+' || line[i] == '~')) {
    if (parse_context.posix) {
      return std::unexpected("POSIX sed rejects GNU range extensions");
    }
    const char range_type = line[i++];
    auto value = parse_unsigned_at(line, i);
    if (!value) return std::unexpected(value.error());
    if (range_type == '+') {
      addr.kind = Script::Address::Kind::Relative;
      addr.line_no = *value;
    } else {
      addr.kind = Script::Address::Kind::Modulo;
      addr.step = *value;
    }
    return addr;
  }
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
      if (parse_context.posix) {
        return std::unexpected("POSIX sed rejects GNU step addresses");
      }
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
  if (line[i] == '/' || (line[i] == '\\' && i + 1 < line.size())) {
    char delim = '/';
    if (line[i] == '/') {
      ++i;
    } else {
      ++i;
      delim = line[i++];
    }
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
      if (c == delim) {
        ++i;
        bool ignore_case = false;
        if (i < line.size() && (line[i] == 'I' || line[i] == 'i')) {
          if (parse_context.posix) {
            return std::unexpected("POSIX sed rejects GNU address modifiers");
          }
          ignore_case = true;
          ++i;
        }
        auto compiled = portable_regex::compile(syntax, pat, ignore_case);
        if (!compiled) {
          return std::unexpected("invalid address regex");
        }
        addr.kind = Script::Address::Kind::Regex;
        addr.regex = std::move(compiled.pattern);
        parse_context.last_regex.pattern = pat;
        parse_context.last_regex.ignore_case = ignore_case;
        parse_context.last_regex.valid = true;
        return addr;
      }
      pat.push_back(c);
    }
    return std::unexpected("unterminated address regex");
  }
  return addr;
}

auto skip_split_address(std::string_view text, size_t& i,
                        bool allow_range_extension = false) -> bool {
  size_t pos = i;
  while (pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  if (pos >= text.size()) return false;

  if (allow_range_extension && (text[pos] == '+' || text[pos] == '~')) {
    ++pos;
    size_t start = pos;
    while (pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      ++pos;
    }
    if (pos == start) return false;
    i = pos;
    return true;
  }

  if (text[pos] == '$') {
    i = pos + 1;
    return true;
  }

  if (std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
    while (pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      ++pos;
    }
    if (pos < text.size() && text[pos] == '~') {
      ++pos;
      while (pos < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
      }
    }
    i = pos;
    return true;
  }

  if (text[pos] == '/' || (text[pos] == '\\' && pos + 1 < text.size())) {
    char delim = '/';
    if (text[pos] == '/') {
      ++pos;
    } else {
      ++pos;
      delim = text[pos++];
    }
    bool escape = false;
    for (; pos < text.size(); ++pos) {
      char c = text[pos];
      if (escape) {
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == delim) {
        ++pos;
        if (pos < text.size() && (text[pos] == 'I' || text[pos] == 'i')) {
          ++pos;
        }
        i = pos;
        return true;
      }
    }
  }

  return false;
}

auto command_index(std::string_view part) -> size_t {
  size_t i = 0;
  skip_split_address(part, i);
  while (i < part.size() && std::isspace(static_cast<unsigned char>(part[i]))) {
    ++i;
  }
  if (i < part.size() && part[i] == ',') {
    ++i;
    if (!skip_split_address(part, i, true)) return std::string_view::npos;
  }
  while (i < part.size() && std::isspace(static_cast<unsigned char>(part[i]))) {
    ++i;
  }
  if (i < part.size() && part[i] == '!') {
    ++i;
    while (i < part.size() &&
           std::isspace(static_cast<unsigned char>(part[i]))) {
      ++i;
    }
  }
  return i;
}

auto text_command_index(std::string_view part) -> size_t {
  size_t i = command_index(part);
  if (i < part.size() && (part[i] == 'a' || part[i] == 'i' || part[i] == 'c')) {
    return i;
  }
  return std::string_view::npos;
}

auto file_command_index(std::string_view part) -> size_t {
  size_t i = command_index(part);
  if (i < part.size() &&
      (part[i] == 'r' || part[i] == 'R' || part[i] == 'w' || part[i] == 'W')) {
    return i;
  }
  return std::string_view::npos;
}

// One command of a sed script, together with the addresses inherited from the
// brace groups { ... } that enclose it.
struct ParsedCommand {
  std::string text;  // command text, including its own address
  std::vector<std::string>
      group_prefixes;  // raw address text of each enclosing group
};

// Normalize the raw body of an a/i/c text command. The body runs from the
// character after the command to the newline that ends it; a line ending with
// an odd number of backslashes continues onto the next line.
auto normalize_text_body_full(std::string_view raw) -> std::string {
  std::string out;
  size_t pos = 0;
  auto next_line = [&]() -> std::optional<std::string_view> {
    if (pos >= raw.size()) return std::nullopt;
    size_t eol = raw.find('\n', pos);
    std::string_view frag;
    if (eol == std::string_view::npos) {
      frag = raw.substr(pos);
      pos = raw.size();
    } else {
      frag = raw.substr(pos, eol - pos);
      pos = eol + 1;
    }
    return strip_trailing_cr(frag);
  };

  auto first = next_line();
  if (!first) return out;
  std::string_view body = trim_left_space(*first);
  bool skip_first = false;
  if (!body.empty() && body.front() == '\\') {
    body.remove_prefix(1);
    skip_first = body.empty();
  }
  bool has_prev = false;
  for (;;) {
    std::string_view frag;
    if (has_prev || skip_first) {
      auto more = next_line();
      if (!more) return out;
      frag = *more;
    } else {
      frag = body;
    }
    bool continues = has_text_line_continuation(frag);
    if (continues) frag.remove_suffix(1);
    if (has_prev) out.push_back('\n');
    out.append(normalize_text_command_body(frag));
    has_prev = true;
    if (!continues) break;
  }
  return out;
}

// Split a whole sed script into commands. Semicolons and newlines separate
// commands; { ... } groups are supported and the address written before a { is
// inherited by every command in that group, including groups nested inside it.
auto split_script_commands(std::string_view text)
    -> cp::Result<std::vector<ParsedCommand>> {
  std::vector<ParsedCommand> parts;
  std::string cur;
  std::vector<std::string>
      group_stack;  // address text of every open brace group
  bool text_body_closed = false;
  bool escape = false;
  bool in_addr = false;
  bool in_sy = false;
  char addr_delim = '/';
  char sy_delim = '\0';
  int sy_parts = 0;
  int sy_need = 0;

  auto at_command_position = [&]() -> bool {
    return command_index(cur) == cur.size();
  };

  auto finish_command = [&]() {
    if (trim_left_space(cur).empty()) return;
    if (at_command_position()) return;  // an address with no command
    ParsedCommand cmd;
    cmd.text = cur;
    for (const auto& prefix : group_stack) {
      if (!trim_left_space(prefix).empty())
        cmd.group_prefixes.push_back(prefix);
    }
    parts.push_back(std::move(cmd));
  };

  // } closes the innermost brace group. GNU requires a group to end the line,
  // but a run of } characters closing nested groups is allowed on one line.
  auto close_groups_until_line_end = [&](size_t& i) -> cp::Result<void> {
    for (;;) {
      size_t j = i + 1;
      // Only horizontal whitespace: the newline itself is the terminator we are
      // looking for, so it must not be skipped here.
      while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) {
        ++j;
      }
      if (j >= text.size()) return {};
      if (text[j] == '#') {
        while (j < text.size() && text[j] != '\n') ++j;
        i = j;
        continue;
      }
      if (text[j] == '}') {
        if (group_stack.empty()) return std::unexpected("unexpected `}'");
        group_stack.pop_back();
        i = j;
        continue;
      }
      // [GNU] A ';' may follow '}' and start the next command on the same
      // line (e.g. "1{p};2{d}"). Leave the ';' for the main loop to consume
      // as an ordinary command separator.
      if (text[j] == ';') {
        i = j - 1;
        return {};
      }
      if (text[j] == '\n') {
        i = j;
        return {};
      }
      return std::unexpected("extra characters after command");
    }
  };

  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (escape) {
      cur.push_back(c);
      escape = false;
      continue;
    }
    if (c == '\\' && at_command_position() && i + 1 < text.size()) {
      cur.push_back(c);
      cur.push_back(text[++i]);
      addr_delim = text[i];
      in_addr = true;
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
      if (c == addr_delim) in_addr = false;
      continue;
    }
    if (c == '/') {
      cur.push_back(c);
      addr_delim = '/';
      in_addr = true;
      continue;
    }
    if ((c == 's' || c == 'y') && at_command_position()) {
      cur.push_back(c);
      if (i + 1 < text.size()) {
        sy_delim = text[i + 1];
        in_sy = true;
        sy_parts = 0;
        sy_need = 3;
      }
      continue;
    }
    if ((c == 'a' || c == 'i' || c == 'c') && at_command_position()) {
      cur.push_back(c);
      // The body runs from the character after the command to the end of the
      // last text line. A line ending with an odd number of backslashes
      // continues onto the next line. The first line is tested including the
      // a/i/c command itself, because that is where a "a\" marker lives.
      size_t body_start = i + 1;
      size_t body_end = body_start;
      size_t scan = body_start;
      for (;;) {
        size_t eol = text.find('\n', scan);
        if (eol == std::string_view::npos) {
          body_end = text.size();
          break;
        }
        std::string_view line = (scan == body_start)
                                    ? text.substr(i, eol - i)
                                    : text.substr(scan, eol - scan);
        body_end = eol;
        if (!has_text_line_continuation(strip_trailing_cr(line))) break;
        scan = eol + 1;
      }
      cur.append(text.substr(body_start, body_end - body_start));
      // The body is fully consumed here, so emit this command now and start the
      // next one fresh; otherwise the following characters would be appended
      // to the text body.
      finish_command();
      cur.clear();
      text_body_closed = false;
      i = body_end;
      continue;
    }
    if (c == '#' && trim_left_space(cur).empty()) {
      while (i < text.size() && text[i] != '\n') ++i;
      continue;
    }
    if (c == '{' && at_command_position()) {
      group_stack.push_back(cur);
      cur.clear();
      text_body_closed = false;
      continue;
    }
    if (c == '}' && text_command_index(cur) == std::string_view::npos &&
        file_command_index(cur) == std::string_view::npos) {
      finish_command();
      cur.clear();
      text_body_closed = false;
      if (group_stack.empty()) return std::unexpected("unexpected `}'");
      group_stack.pop_back();
      auto close = close_groups_until_line_end(i);
      if (!close) return std::unexpected(close.error());
      continue;
    }
    if (c == ';' && !text_body_closed &&
        (text_command_index(cur) != std::string_view::npos ||
         file_command_index(cur) != std::string_view::npos)) {
      cur.push_back(c);
      continue;
    }
    if (c == ';' || c == '\n') {
      finish_command();
      cur.clear();
      text_body_closed = false;
      continue;
    }
    cur.push_back(c);
  }
  finish_command();
  if (!group_stack.empty()) return std::unexpected("unmatched `{'");
  return parts;
}

auto parse_group_address(std::string_view text, portable_regex::Syntax syntax,
                         ParseContext& parse_context)
    -> cp::Result<Script::GroupAddress> {
  Script::GroupAddress g;
  size_t i = 0;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
    ++i;
  if (i >= text.size()) return std::unexpected("empty group address");
  auto a1 = parse_address(text, i, syntax, parse_context);
  if (!a1) return std::unexpected(a1.error());
  g.addr1 = *a1;
  if (i < text.size() && text[i] == ',') {
    ++i;
    auto a2 = parse_address(text, i, syntax, parse_context, true);
    if (!a2) return std::unexpected(a2.error());
    g.addr2 = *a2;
  }
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
    ++i;
  if (i < text.size() && text[i] == '!') {
    g.invert = true;
    ++i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
      ++i;
  }
  if (i != text.size()) return std::unexpected("invalid group address");
  return g;
}

auto parse_script_command(const ParsedCommand& part,
                          portable_regex::Syntax syntax,
                          ParseContext& parse_context) -> cp::Result<Script> {
  std::string_view text = part.text;
  size_t i = 0;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
    ++i;
  Script::Address a1, a2;
  auto addr1 = parse_address(text, i, syntax, parse_context);
  if (!addr1) return std::unexpected(addr1.error());
  a1 = *addr1;
  if (i < text.size() && text[i] == ',') {
    ++i;
    auto addr2 = parse_address(text, i, syntax, parse_context, true);
    if (!addr2) return std::unexpected(addr2.error());
    a2 = *addr2;
  }
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
    ++i;
  bool invert_address = false;
  if (i < text.size() && text[i] == '!') {
    invert_address = true;
    ++i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
      ++i;
  }
  std::string_view cmd = text.substr(i);
  cp::Result<Script> s;
  if (!cmd.empty() && cmd[0] == 's') {
    s = parse_subst(cmd, syntax, parse_context);
  } else if (!cmd.empty() && cmd[0] == 'y') {
    s = parse_y_cmd(cmd);
  } else {
    s = parse_simple_cmd(cmd);
  }
  if (!s) return std::unexpected(s.error());
  if (s->kind == Script::Kind::Label &&
      (a1.kind != Script::Address::Kind::None ||
       a2.kind != Script::Address::Kind::None || invert_address)) {
    return std::unexpected(": command does not accept addresses");
  }
  s->addr1 = a1;
  s->addr2 = a2;
  s->invert_address = invert_address;
  for (const auto& prefix : part.group_prefixes) {
    auto g = parse_group_address(prefix, syntax, parse_context);
    if (!g) return std::unexpected(g.error());
    s->group_addresses.push_back(std::move(*g));
  }
  return *s;
}

auto parse_script_text(std::string_view script, portable_regex::Syntax syntax,
                       ParseContext& parse_context)
    -> cp::Result<std::vector<Script>> {
  std::vector<Script> out;

  // GNU sed honors the #n magic comment only when it is the first line of the
  // whole script.
  auto lines = split_script_lines(script);
  if (!lines.empty() && parse_context.at_script_start) {
    auto trimmed = trim_left_space(strip_trailing_cr(lines[0]));
    if (trimmed.starts_with("#n")) parse_context.magic_silent = true;
    parse_context.at_script_start = false;
  }

  auto commands = split_script_commands(script);
  if (!commands) return std::unexpected(commands.error());
  for (const auto& part : *commands) {
    auto s = parse_script_command(part, syntax, parse_context);
    if (!s) return std::unexpected(s.error());
    out.push_back(std::move(*s));
  }
  return out;
}

auto read_script_file(const std::string& path) -> cp::Result<std::string> {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open())
    return std::unexpected("cannot open script file '" + path + "'");
  return std::string(std::istreambuf_iterator<char>{in},
                     std::istreambuf_iterator<char>{});
}

auto address_uses_gnu_extension(const Script::Address& addr) -> bool {
  return addr.kind == Script::Address::Kind::Step ||
         addr.kind == Script::Address::Kind::Relative ||
         addr.kind == Script::Address::Kind::Modulo ||
         (addr.kind == Script::Address::Kind::Line && addr.line_no == 0);
}

auto script_is_sandbox_forbidden(const Script& script) -> bool {
  return script.kind == Script::Kind::ReadFile ||
         script.kind == Script::Kind::ReadFileLine ||
         script.kind == Script::Kind::WriteFile ||
         script.kind == Script::Kind::WriteFirstFile ||
         !script.subst_write_file.empty();
}

auto script_is_posix_forbidden(const Script& script) -> bool {
  if (address_uses_gnu_extension(script.addr1) ||
      address_uses_gnu_extension(script.addr2)) {
    return true;
  }
  for (const auto& group : script.group_addresses) {
    if (address_uses_gnu_extension(group.addr1) ||
        address_uses_gnu_extension(group.addr2)) {
      return true;
    }
  }
  return script.kind == Script::Kind::QuitSilent ||
         script.kind == Script::Kind::PrintFilename ||
         script.kind == Script::Kind::TestNoBranch ||
         script.kind == Script::Kind::ReadFileLine ||
         script.kind == Script::Kind::WriteFirstFile ||
         script.kind == Script::Kind::ClearPattern;
}

auto validate_script_capabilities(const std::vector<Script>& scripts,
                                  const Config& cfg) -> cp::Result<void> {
  for (const auto& script : scripts) {
    if (cfg.sandbox && script_is_sandbox_forbidden(script)) {
      return std::unexpected(
          "--sandbox rejects scripts that read or write files");
    }
    if (cfg.posix && script_is_posix_forbidden(script)) {
      return std::unexpected("--posix rejects GNU sed extensions");
    }
  }
  return {};
}

auto resolve_labels(std::vector<Script>& scripts) -> cp::Result<void> {
  std::unordered_map<std::string, size_t> labels;
  labels.reserve(scripts.size());
  for (size_t i = 0; i < scripts.size(); ++i) {
    const auto& script = scripts[i];
    if (script.kind == Script::Kind::Label) {
      labels[script.label] = i;
    }
  }

  for (auto& script : scripts) {
    if (script.kind != Script::Kind::Branch &&
        script.kind != Script::Kind::TestBranch &&
        script.kind != Script::Kind::TestNoBranch) {
      continue;
    }

    if (script.label.empty()) {
      script.jump_index = scripts.size();
      continue;
    }

    auto it = labels.find(script.label);
    if (it == labels.end()) {
      return std::unexpected("can't find label for jump to '" + script.label +
                             "'");
    }
    script.jump_index = it->second;
  }
  return {};
}

auto build_config(const CommandContext<SED_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  // [DIFFERS] --debug: accepted for GNU compatibility; no execution
  // annotation is produced on this platform (must not fail).
  (void)ctx.has("--debug");
  // [DIFFERS] --follow-symlinks: accepted; symlink following is a no-op on
  // this platform (must not fail).
  (void)ctx.has("--follow-symlinks");
  // [DIFFERS] -u/--unbuffered: accepted; Windows stdout is line-buffered
  (void)ctx.has("--unbuffered");
  (void)ctx.has("-u");
  // [DIFFERS] -b/--binary: accepted; files are opened in binary mode on
  // Windows by default
  (void)ctx.has("--binary");
  (void)ctx.has("-b");
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
    cfg.regex_syntax = portable_regex::Syntax::Extended;
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
  ParseContext parse_context;
  cfg.sandbox = ctx.get<bool>("--sandbox", false);
  cfg.posix = ctx.get<bool>("--posix", false);
  parse_context.posix = cfg.posix;

  auto script_options =
      ctx.string_occurrences({"--expression", "-e", "--file", "-f"});
  if (!script_options.empty()) {
    // [GNU] Every -e expression and -f file is concatenated (separated by
    // newlines) into a single script before it is compiled, so brace groups,
    // a/i/c text bodies, labels and comments may span option boundaries:
    //   sed -e '1{' -e 'p' -e '}'
    std::string script_text;
    for (const auto& occurrence : script_options) {
      if (occurrence.long_name == "--expression" ||
          occurrence.short_name == "-e") {
        script_text += occurrence.value;
        script_text += '\n';
      } else if (occurrence.long_name == "--file" ||
                 occurrence.short_name == "-f") {
        auto file_text = read_script_file(occurrence.value);
        if (!file_text) return std::unexpected(file_text.error());
        script_text += *file_text;
        script_text += '\n';
      }
    }
    auto s = parse_script_text(script_text, cfg.regex_syntax, parse_context);
    if (!s) return std::unexpected(s.error());
    scripts.insert(scripts.end(), s->begin(), s->end());
  }

  size_t consumed_positional = 0;
  // When -e/-f supplied the script (even an empty one, as in "sed -e '' FILE"),
  // the first positional operand is an input file, not a script.
  if (script_options.empty()) {
    if (ctx.positionals.empty()) return std::unexpected("script required");
    auto s =
        parse_script_text(ctx.positionals[0], cfg.regex_syntax, parse_context);
    if (!s) return std::unexpected(s.error());
    scripts.insert(scripts.end(), s->begin(), s->end());
    consumed_positional = 1;
  }

  if (parse_context.magic_silent) cfg.suppress_output = true;

  if (auto ok = resolve_labels(scripts); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_script_capabilities(scripts, cfg); !ok) {
    return std::unexpected(ok.error());
  }

  // Each script owns one range state plus one per inherited group address, so
  // allocate the states densely and remember each script's first slot.
  size_t state_slots = 0;
  for (auto& s : scripts) {
    s.group_state_offset = state_slots;
    state_slots += 1 + s.group_addresses.size();
  }
  for (auto& s : scripts) cfg.scripts.push_back(std::move(s));

  for (size_t i = consumed_positional; i < ctx.positionals.size(); ++i) {
    cfg.files.emplace_back(ctx.positionals[i]);
  }
  // [GNU] with -i, stdin is never implied: GNU panics with "no input
  // files" (execute.c:1672). Leave the list empty; the command entry
  // reports the panic exit code 4.
  if (cfg.files.empty() && !cfg.in_place) cfg.files.emplace_back("-");
  return cfg;
}

// [GNU] Case conversion for s/// replacement text, mirroring the REPL_*
// handling in setup_replacement (sed-4.9/sed/sed.h:67-77,
// compile.c:738-830): \U and \L stay in effect until \E or the end of the
// replacement, \u and \l affect only the next character, and an active
// conversion also applies to text inserted by & and back-references.
namespace sed_case {
enum class Mode { None, Upper, Lower };
enum class First { None, Upper, Lower };

auto apply(std::string& out, char c, Mode mode, First& first) -> void {
  if (first == First::Upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    first = First::None;
  } else if (first == First::Lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    first = First::None;
  }
  if (mode == Mode::Upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  } else if (mode == Mode::Lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  out.push_back(c);
}
}  // namespace sed_case

auto expand_substitution_replacement(const std::string& replacement,
                                     std::string_view input,
                                     const portable_regex::Match& match)
    -> std::string {
  std::string out;
  out.reserve(replacement.size() + (match.end - match.begin));
  sed_case::Mode mode = sed_case::Mode::None;
  sed_case::First first = sed_case::First::None;
  auto emit_text = [&](std::string_view text) {
    for (char ch : text) {
      sed_case::apply(out, ch, mode, first);
    }
  };
  for (size_t i = 0; i < replacement.size(); ++i) {
    char c = replacement[i];
    if (c == '&') {
      emit_text(input.substr(match.begin, match.end - match.begin));
      continue;
    }
    if (c == '\\' && i + 1 < replacement.size()) {
      char next = replacement[++i];
      if (next >= '1' && next <= '9') {
        size_t group = static_cast<size_t>(next - '0');
        if (group < match.captures.size() && match.captures[group].matched) {
          emit_text(input.substr(
              match.captures[group].begin,
              match.captures[group].end - match.captures[group].begin));
        }
      } else if (next == 'a') {
        sed_case::apply(out, '\a', mode, first);
      } else if (next == 'f') {
        sed_case::apply(out, '\f', mode, first);
      } else if (next == 'n') {
        sed_case::apply(out, '\n', mode, first);
      } else if (next == 'r') {
        sed_case::apply(out, '\r', mode, first);
      } else if (next == 't') {
        sed_case::apply(out, '\t', mode, first);
      } else if (next == 'v') {
        sed_case::apply(out, '\v', mode, first);
      } else if (next == 'U') {
        mode = sed_case::Mode::Upper;
      } else if (next == 'L') {
        mode = sed_case::Mode::Lower;
      } else if (next == 'u') {
        first = sed_case::First::Upper;
      } else if (next == 'l') {
        first = sed_case::First::Lower;
      } else if (next == 'E') {
        mode = sed_case::Mode::None;
        first = sed_case::First::None;
      } else if (next == '&' || next == '\\') {
        sed_case::apply(out, next, mode, first);
      } else {
        // [GNU] unknown escapes keep the backslash (compile.c:560-565)
        sed_case::apply(out, '\\', mode, first);
        sed_case::apply(out, next, mode, first);
      }
      continue;
    }
    sed_case::apply(out, c, mode, first);
  }
  return out;
}

auto append_literal_substitution(std::string& output, std::string_view input,
                                 const Script& script) -> bool {
  const auto& needle = script.literal_pattern;
  const size_t output_start = output.size();
  size_t search_start = 0;
  size_t last_append = 0;
  size_t match_index = 0;
  bool changed = false;

  while (true) {
    size_t match_start = input.find(needle, search_start);
    if (match_start == std::string::npos) break;

    ++match_index;
    const size_t match_end = match_start + needle.size();
    const bool replace_this =
        script.occurrence == 0
            ? script.global || match_index == 1
            : match_index >= script.occurrence &&
                  (script.global || match_index == script.occurrence);

    if (replace_this) {
      output.append(input.substr(last_append, match_start - last_append));
      output.append(script.replacement);
      last_append = match_end;
      changed = true;
    }

    search_start = match_end;
    if (script.occurrence == 0 && !script.global && changed) break;
    if (script.occurrence != 0 && !script.global &&
        match_index >= script.occurrence) {
      break;
    }
  }

  if (changed) {
    output.append(input.substr(last_append));
    return true;
  }
  output.resize(output_start);
  return false;
}

auto substitute_literal_line(const std::string& input, const Script& script,
                             bool& changed) -> std::string {
  std::string output;
  output.reserve(input.size());
  changed = append_literal_substitution(output, input, script);
  if (changed) return output;
  return input;
}

auto substitute_line(const std::string& input, const Script& script,
                     bool& changed) -> std::string {
  if (script.literal_substitution) {
    return substitute_literal_line(input, script, changed);
  }

  changed = false;
  std::string output;
  output.reserve(input.size());

  size_t search_start = 0;
  size_t last_append = 0;
  size_t match_index = 0;

  // [GNU] REG_NEWLINE matching for s///m and s///M: with the m/M modifier
  // ^ and $ also match immediately after/before an embedded newline, so
  // matches from the anchor-stripped pattern are filtered by an explicit
  // anchor check at the candidate position.
  auto find_match = [&](size_t from) -> std::optional<portable_regex::Match> {
    if (!script.ml_caret && !script.ml_dollar) {
      return script.pattern.find_first(input, from);
    }
    size_t cursor = from;
    while (auto candidate = script.ml_pattern.find_first(input, cursor)) {
      if (script.ml_caret && candidate->begin != 0 &&
          input[candidate->begin - 1] != '\n') {
        cursor = candidate->end == candidate->begin ? candidate->end + 1
                                                    : candidate->end;
        if (cursor > input.size()) break;
        continue;
      }
      if (script.ml_dollar && candidate->end != input.size() &&
          input[candidate->end] != '\n') {
        cursor = candidate->end == candidate->begin ? candidate->end + 1
                                                    : candidate->end;
        if (cursor > input.size()) break;
        continue;
      }
      return candidate;
    }
    return std::nullopt;
  };

  while (auto match = find_match(search_start)) {
    ++match_index;
    size_t match_start = match->begin;
    size_t match_end = match->end;
    const bool replace_this =
        script.occurrence == 0
            ? script.global || match_index == 1
            : match_index >= script.occurrence &&
                  (script.global || match_index == script.occurrence);

    if (replace_this) {
      output.append(input, last_append, match_start - last_append);
      output.append(
          expand_substitution_replacement(script.replacement, input, *match));
      last_append = match_end;
      changed = true;
    }

    if (match_end == match_start) {
      if (match_end >= input.size()) break;
      search_start = match_end + 1;
    } else {
      search_start = match_end;
    }

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
    if (wrap_length > 0 && column > 0 &&
        column + text.size() + 1 > wrap_length) {
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

struct AppendAction {
  std::string text;
  bool terminate = true;
};

struct ProcessRuntime {
  std::string hold;
  bool hold_had_delimiter = false;
  std::unordered_set<std::string> truncated_write_files;
  std::unordered_map<std::string, std::ifstream> read_files;
  std::unordered_set<std::string> missing_read_files;
  bool io_error = false;
};

struct AddressEval {
  bool apply = false;
  bool range_ended = false;
};

auto saturating_add(size_t lhs, size_t rhs) -> size_t {
  if (std::numeric_limits<size_t>::max() - lhs < rhs) {
    return std::numeric_limits<size_t>::max();
  }
  return lhs + rhs;
}

auto set_dynamic_range_end(ScriptState& range_state,
                           const Script::Address& addr, size_t line_no)
    -> bool {
  if (addr.kind == Script::Address::Kind::Relative) {
    range_state.range_end_line = saturating_add(line_no, addr.line_no);
    return true;
  }
  if (addr.kind == Script::Address::Kind::Modulo) {
    if (addr.step == 0) {
      range_state.range_end_line = line_no;
    } else {
      size_t remainder = line_no % addr.step;
      size_t delta = remainder == 0 ? addr.step : addr.step - remainder;
      range_state.range_end_line = saturating_add(line_no, delta);
    }
    return true;
  }
  return false;
}

auto read_record(std::istream& in, char delimiter, std::string& record,
                 bool& had_delimiter) -> bool {
  record.clear();
  if (!std::getline(in, record, delimiter)) return false;
  had_delimiter = !in.eof();
  return true;
}

auto write_file_once_truncated(ProcessRuntime& runtime, const std::string& path,
                               std::string_view text) -> bool {
  auto mode = std::ios::binary | std::ios::app;
  if (!runtime.truncated_write_files.contains(path)) {
    mode = std::ios::binary | std::ios::trunc;
    runtime.truncated_write_files.insert(path);
  }

  std::ofstream out(path, mode);
  if (!out.is_open()) {
    safeErrorPrint("sed: cannot open '" + path + "' for writing\n");
    return false;
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out.good()) {
    safeErrorPrint("sed: cannot write '" + path + "'\n");
    return false;
  }
  return true;
}

auto read_whole_file_for_append(const std::string& path) -> std::string {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    // [GNU] report and keep going (execute.c:565, r command is never
    // fatal: "sed: can't read FILE: reason" on stderr)
    safeErrorPrint("sed: can't read " + path + ": " +
                   std::string(std::strerror(errno)) + "\n");
    return {};
  }
  return std::string(std::istreambuf_iterator<char>{in},
                     std::istreambuf_iterator<char>{});
}

auto read_one_file_record_for_append(ProcessRuntime& runtime,
                                     const std::string& path, char delimiter)
    -> std::optional<AppendAction> {
  if (runtime.missing_read_files.contains(path)) return std::nullopt;

  auto [it, inserted] = runtime.read_files.try_emplace(path);
  if (inserted) {
    it->second.open(path, std::ios::binary);
    if (!it->second.is_open()) {
      // [GNU] report once and keep going (execute.c:565)
      safeErrorPrint("sed: can't read " + path + ": " +
                     std::string(std::strerror(errno)) + "\n");
      runtime.missing_read_files.insert(path);
      return std::nullopt;
    }
  }

  std::string line;
  bool had_delimiter = false;
  if (!read_record(it->second, delimiter, line, had_delimiter)) {
    return std::nullopt;
  }
  return AppendAction{std::move(line), had_delimiter};
}

struct ExecutionContext {
  std::istream& in;
  const Config& cfg;
  const std::vector<Script>& scripts;
  std::vector<ScriptState>& states;
  ProcessRuntime& runtime;
  size_t& line_no;
  std::string_view current_file = "-";
  bool final_input = false;
  std::string current;
  bool current_had_delimiter = false;
  std::vector<AppendAction> append_after;
  std::string output;
  bool has_output = false;
  bool substituted_since_branch = false;
  bool should_quit = false;
  int quit_exit_code = 0;
};

auto append_output(ExecutionContext& ctx, std::string_view text, bool terminate)
    -> void {
  ctx.output.append(text);
  if (terminate) ctx.output.push_back(ctx.cfg.delimiter);
  ctx.has_output = true;
}

auto flush_append_queue(ExecutionContext& ctx) -> void {
  for (const auto& item : ctx.append_after) {
    if (!item.text.empty()) {
      ctx.output.append(item.text);
      ctx.has_output = true;
    }
    if (item.terminate) {
      ctx.output.push_back(ctx.cfg.delimiter);
      ctx.has_output = true;
    }
  }
  ctx.append_after.clear();
}

auto is_current_last_input_record(ExecutionContext& ctx) -> bool {
  if (!ctx.final_input) return false;
  if (!ctx.current_had_delimiter) return true;
  return ctx.in.peek() == EOF;
}

auto read_next_input_record(ExecutionContext& ctx, bool append) -> bool {
  std::string next;
  bool had_delimiter = false;
  if (!read_record(ctx.in, ctx.cfg.delimiter, next, had_delimiter)) {
    return false;
  }

  ++ctx.line_no;
  ctx.substituted_since_branch = false;
  if (append) {
    ctx.current.push_back(ctx.cfg.delimiter);
    ctx.current.append(next);
  } else {
    ctx.current = std::move(next);
  }
  ctx.current_had_delimiter = had_delimiter;
  return true;
}

auto evaluate_address_range(ExecutionContext& ctx, const Script::Address& addr1,
                            const Script::Address& addr2, bool invert_address,
                            ScriptState& state) -> AddressEval {
  auto addr_match = [&](const Script::Address& a) -> bool {
    if (a.kind == Script::Address::Kind::None) return true;
    if (a.kind == Script::Address::Kind::Line) return ctx.line_no == a.line_no;
    if (a.kind == Script::Address::Kind::Last) {
      return is_current_last_input_record(ctx);
    }
    if (a.kind == Script::Address::Kind::Step) {
      return ctx.line_no >= a.line_no &&
             (ctx.line_no - a.line_no) % a.step == 0;
    }
    if (a.kind == Script::Address::Kind::Relative ||
        a.kind == Script::Address::Kind::Modulo) {
      return false;
    }
    return a.regex.find_first(ctx.current).has_value();
  };

  AddressEval result;
  const bool line_zero_range = addr2.kind != Script::Address::Kind::None &&
                               addr1.kind == Script::Address::Kind::Line &&
                               addr1.line_no == 0;
  if (line_zero_range && !state.range_closed) {
    // GNU sed treats 0,/RE/ as a range that is active before line 1, so the
    // first matching line can also end the range.
    state.range_active = true;
  }
  if (addr2.kind == Script::Address::Kind::None) {
    result.apply = addr_match(addr1);
  } else {
    if (!state.range_active) {
      if (addr_match(addr1)) {
        result.apply = true;
        if (addr2.kind == Script::Address::Kind::Line &&
            ctx.line_no >= addr2.line_no) {
          result.range_ended = true;
        } else if (set_dynamic_range_end(state, addr2, ctx.line_no)) {
          if (ctx.line_no >= state.range_end_line) {
            result.range_ended = true;
          } else {
            state.range_active = true;
          }
        } else {
          state.range_active = true;
        }
      }
    } else {
      result.apply = true;
      bool end_matches = false;
      if (addr2.kind == Script::Address::Kind::Line) {
        end_matches = ctx.line_no >= addr2.line_no;
      } else if (addr2.kind == Script::Address::Kind::Relative ||
                 addr2.kind == Script::Address::Kind::Modulo) {
        end_matches = ctx.line_no >= state.range_end_line;
      } else {
        end_matches = addr_match(addr2);
      }
      if (end_matches) {
        state.range_active = false;
        if (line_zero_range) state.range_closed = true;
        result.range_ended = true;
      }
    }
  }

  if (invert_address) result.apply = !result.apply;
  return result;
}

// A command's own address is ANDed with the addresses of every brace group that
// encloses it, which is how GNU sed applies "1 { p; }" and nested groups.
auto evaluate_address(ExecutionContext& ctx, const Script& script,
                      size_t script_index) -> AddressEval {
  auto result =
      evaluate_address_range(ctx, script.addr1, script.addr2,
                             script.invert_address, ctx.states[script_index]);
  for (size_t k = 0; k < script.group_addresses.size(); ++k) {
    if (!result.apply) break;
    const auto& group = script.group_addresses[k];
    auto& group_state = ctx.states[script.group_state_offset + k];
    auto group_result = evaluate_address_range(ctx, group.addr1, group.addr2,
                                               group.invert, group_state);
    if (!group_result.apply) result.apply = false;
  }
  return result;
}

auto first_pattern_segment(std::string_view current, char delimiter)
    -> std::pair<std::string_view, bool> {
  size_t pos = current.find(delimiter);
  if (pos == std::string_view::npos) return {current, false};
  return {current.substr(0, pos), true};
}

auto pattern_with_terminator(std::string_view text, bool terminate,
                             char delimiter) -> std::string {
  std::string out(text);
  if (terminate) out.push_back(delimiter);
  return out;
}

auto run_scripts_for_cycle(ExecutionContext& ctx) -> void {
  bool deleted = false;
  bool discard_append_queue = false;
  bool end_cycle = false;
  size_t pc = 0;

  while (pc < ctx.scripts.size()) {
    const auto& s = ctx.scripts[pc];
    auto address = evaluate_address(ctx, s, pc);
    if (!address.apply) {
      ++pc;
      continue;
    }

    switch (s.kind) {
      case Script::Kind::Subst: {
        if (s.has_ymap) {
          for (auto& ch : ctx.current) {
            ch = static_cast<char>(s.ymap[static_cast<unsigned char>(ch)]);
          }
        } else {
          bool changed = false;
          std::string replaced = substitute_line(ctx.current, s, changed);
          ctx.substituted_since_branch =
              ctx.substituted_since_branch || changed;
          ctx.current.swap(replaced);
          if (s.print_on_match && changed) {
            append_output(ctx, ctx.current, true);
          }
          if (!s.subst_write_file.empty() && changed) {
            std::string text = pattern_with_terminator(
                ctx.current, ctx.current_had_delimiter, ctx.cfg.delimiter);
            ctx.runtime.io_error = !write_file_once_truncated(
                                       ctx.runtime, s.subst_write_file, text) ||
                                   ctx.runtime.io_error;
          }
        }
        break;
      }
      case Script::Kind::Print:
        append_output(ctx, ctx.current, true);
        break;
      case Script::Kind::PrintFirst: {
        auto [segment, ended_by_delimiter] =
            first_pattern_segment(ctx.current, ctx.cfg.delimiter);
        append_output(ctx, segment,
                      ended_by_delimiter || ctx.current_had_delimiter);
        break;
      }
      case Script::Kind::Delete:
        deleted = true;
        end_cycle = true;
        break;
      case Script::Kind::DeleteFirst: {
        size_t pos = ctx.current.find(ctx.cfg.delimiter);
        if (pos == std::string::npos) {
          deleted = true;
          end_cycle = true;
          break;
        }
        ctx.current.erase(0, pos + 1);
        pc = 0;
        continue;
      }
      case Script::Kind::Quit:
        ctx.should_quit = true;
        ctx.quit_exit_code = s.quit_exit_code;
        end_cycle = true;
        break;
      case Script::Kind::QuitSilent:
        ctx.should_quit = true;
        ctx.quit_exit_code = s.quit_exit_code;
        deleted = true;
        discard_append_queue = true;
        end_cycle = true;
        break;
      case Script::Kind::Insert:
        append_output(ctx, s.text, true);
        break;
      case Script::Kind::Append:
        ctx.append_after.push_back(AppendAction{s.text, true});
        break;
      case Script::Kind::Change:
        if (s.addr2.kind == Script::Address::Kind::None || s.invert_address ||
            address.range_ended) {
          append_output(ctx, s.text, true);
        }
        deleted = true;
        end_cycle = true;
        break;
      case Script::Kind::LineNumber:
        append_output(ctx, std::to_string(ctx.line_no), true);
        break;
      case Script::Kind::PrintFilename:
        append_output(ctx, ctx.current_file, true);
        break;
      case Script::Kind::List:
        append_output(
            ctx,
            list_escape_line(
                ctx.current,
                s.list_line_length == std::numeric_limits<size_t>::max()
                    ? ctx.cfg.list_line_length
                    : s.list_line_length),
            true);
        break;
      case Script::Kind::Next:
        if (!ctx.cfg.suppress_output) {
          append_output(ctx, ctx.current, ctx.current_had_delimiter);
        }
        flush_append_queue(ctx);
        if (!read_next_input_record(ctx, false)) {
          deleted = true;
          end_cycle = true;
        }
        break;
      case Script::Kind::AppendNext:
        if (!read_next_input_record(ctx, true)) {
          deleted = ctx.cfg.suppress_output;
          end_cycle = true;
        }
        break;
      case Script::Kind::HoldCopyToPattern:
        ctx.current = ctx.runtime.hold;
        ctx.current_had_delimiter = ctx.runtime.hold_had_delimiter;
        break;
      case Script::Kind::HoldAppendToPattern:
        ctx.current.push_back(ctx.cfg.delimiter);
        ctx.current.append(ctx.runtime.hold);
        break;
      case Script::Kind::PatternCopyToHold:
        ctx.runtime.hold = ctx.current;
        ctx.runtime.hold_had_delimiter = ctx.current_had_delimiter;
        break;
      case Script::Kind::PatternAppendToHold:
        ctx.runtime.hold.push_back(ctx.cfg.delimiter);
        ctx.runtime.hold.append(ctx.current);
        ctx.runtime.hold_had_delimiter = ctx.current_had_delimiter;
        break;
      case Script::Kind::Exchange:
        std::swap(ctx.current, ctx.runtime.hold);
        std::swap(ctx.current_had_delimiter, ctx.runtime.hold_had_delimiter);
        break;
      case Script::Kind::Label:
        break;
      case Script::Kind::Branch:
        pc = s.jump_index;
        continue;
      case Script::Kind::TestBranch:
        if (ctx.substituted_since_branch) {
          ctx.substituted_since_branch = false;
          pc = s.jump_index;
          continue;
        }
        break;
      case Script::Kind::TestNoBranch:
        if (!ctx.substituted_since_branch) {
          pc = s.jump_index;
          continue;
        }
        ctx.substituted_since_branch = false;
        break;
      case Script::Kind::ReadFile: {
        std::string contents = read_whole_file_for_append(s.file_path);
        if (!contents.empty()) {
          ctx.append_after.push_back(AppendAction{std::move(contents), false});
        }
        break;
      }
      case Script::Kind::ReadFileLine:
        if (auto line = read_one_file_record_for_append(
                ctx.runtime, s.file_path, ctx.cfg.delimiter)) {
          ctx.append_after.push_back(std::move(*line));
        }
        break;
      case Script::Kind::WriteFile: {
        std::string text = pattern_with_terminator(
            ctx.current, ctx.current_had_delimiter, ctx.cfg.delimiter);
        ctx.runtime.io_error =
            !write_file_once_truncated(ctx.runtime, s.file_path, text) ||
            ctx.runtime.io_error;
        break;
      }
      case Script::Kind::WriteFirstFile: {
        auto [segment, ended_by_delimiter] =
            first_pattern_segment(ctx.current, ctx.cfg.delimiter);
        std::string text = pattern_with_terminator(
            segment, ended_by_delimiter || ctx.current_had_delimiter,
            ctx.cfg.delimiter);
        ctx.runtime.io_error =
            !write_file_once_truncated(ctx.runtime, s.file_path, text) ||
            ctx.runtime.io_error;
        break;
      }
      case Script::Kind::ClearPattern:
        ctx.current.clear();
        break;
    }

    if (end_cycle || ctx.should_quit) break;
    ++pc;
  }

  if (!deleted && !ctx.cfg.suppress_output) {
    append_output(ctx, ctx.current, ctx.current_had_delimiter);
  }

  if (!discard_append_queue) {
    flush_append_queue(ctx);
  }
}

auto process_stream(std::istream& in, const Config& cfg,
                    std::vector<ScriptState>& states, ProcessRuntime& runtime,
                    size_t& line_no, bool final_input,
                    std::string_view current_file, std::string* capture)
    -> std::optional<int> {
  std::vector<Script> scripts_vec(cfg.scripts.begin(), cfg.scripts.end());

  for (;;) {
    ExecutionContext ctx{in, cfg, scripts_vec, states, runtime, line_no};
    ctx.current_file = current_file;
    ctx.final_input = final_input;
    if (!read_next_input_record(ctx, false)) break;

    run_scripts_for_cycle(ctx);

    if (ctx.has_output) {
      if (capture) {
        capture->append(ctx.output);
      } else {
        safePrint(ctx.output);
      }
    }
    if (ctx.should_quit) return ctx.quit_exit_code;
  }
  return std::nullopt;
}

auto can_use_literal_stream_fast_path(const Config& cfg) -> bool {
  if (cfg.scripts.size() != 1 || cfg.suppress_output) return false;

  const auto& script = cfg.scripts.front();
  return script.kind == Script::Kind::Subst && script.literal_substitution &&
         !script.print_on_match && !script.invert_address &&
         script.subst_write_file.empty() &&
         script.addr1.kind == Script::Address::Kind::None &&
         script.addr2.kind == Script::Address::Kind::None;
}

auto read_binary_stream(std::istream& in) -> std::string {
  return std::string(std::istreambuf_iterator<char>{in},
                     std::istreambuf_iterator<char>{});
}

auto apply_literal_stream_fast_path(std::string_view input, const Config& cfg)
    -> std::string {
  const auto& script = cfg.scripts.front();
  std::string output;
  output.reserve(input.size());

  if (script.global && script.occurrence == 0 &&
      script.literal_pattern.find(cfg.delimiter) == std::string::npos) {
    if (!append_literal_substitution(output, input, script)) {
      output.assign(input.data(), input.size());
    }
    return output;
  }

  size_t record_start = 0;
  while (record_start < input.size()) {
    const size_t delim_pos = input.find(cfg.delimiter, record_start);
    const bool had_delim = delim_pos != std::string_view::npos;
    const size_t record_end = had_delim ? delim_pos : input.size();
    const std::string_view record =
        input.substr(record_start, record_end - record_start);

    const size_t before_substitution = output.size();
    if (!append_literal_substitution(output, record, script)) {
      output.resize(before_substitution);
      output.append(record);
    }
    if (had_delim) output.push_back(cfg.delimiter);

    if (!had_delim) break;
    record_start = delim_pos + 1;
  }

  return output;
}

auto make_in_place_backup_path(const std::string& path,
                               const std::string& suffix)
    -> std::filesystem::path {
  // Build from the wide form: the narrow path ctor decodes via the system
  // ACP, which mangles non-ASCII UTF-8 paths (#88).
  if (suffix.find('*') == std::string::npos) {
    return std::filesystem::path(utf8_to_wstring(path + suffix));
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
  return std::filesystem::path(utf8_to_wstring(backup));
}

auto preserve_in_place_backup(const std::filesystem::path& original,
                              const std::string& suffix) -> bool {
  if (suffix.empty()) return true;

  auto backup =
      make_in_place_backup_path(wstring_to_utf8(original.wstring()), suffix);
  std::error_code ec;
  std::filesystem::copy_file(
      original, backup, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    safeErrorPrint("sed: cannot create backup '" +
                   wstring_to_utf8(backup.wstring()) + "'\n");
    return false;
  }
  return true;
}

auto replace_file_atomically(const std::string& path,
                             const std::string& backup_suffix,
                             const std::string& content) -> bool {
  auto original = std::filesystem::path(utf8_to_wstring(path));
  DWORD original_attrs = GetFileAttributesW(utf8_to_wstring(path).c_str());
  auto suffix =
      std::string(".winuxtmp.") + std::to_string(GetCurrentProcessId());
  auto temp = std::filesystem::path(utf8_to_wstring(path + suffix));
  auto backup = std::filesystem::path(utf8_to_wstring(path + suffix + ".bak"));

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

  if (original_attrs != INVALID_FILE_ATTRIBUTES)
    SetFileAttributesW(utf8_to_wstring(path).c_str(), original_attrs);

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

  // [GNU] exit codes: 1 = bad usage/script (reported by build_config),
  // 2 = bad input files or w/W/s///w write failures (EXIT_BAD_INPUT,
  // utils.h:22-25, execute.c:1710-1712). A q exit code wins only when no
  // error was recorded.
  int error_level = 0;
  size_t state_slots = 0;
  for (const auto& s : cfg.scripts) state_slots += 1 + s.group_addresses.size();
  std::vector<ScriptState> states(state_slots);
  ProcessRuntime runtime;
  size_t line_no = 0;
  for (size_t file_index = 0; file_index < expanded_files.size();
       ++file_index) {
    const auto& f = expanded_files[file_index];
    if (cfg.in_place && f == "-") {
      // [GNU] with -i, "-" is opened as a regular file name and fails
      // like any other unreadable input (EXIT_BAD_INPUT, utils.h:22-25).
      safeErrorPrint("sed: can't read -: No such file or directory\n");
      error_level = std::max(error_level, 2);
      continue;
    }

    std::ifstream file;
    std::istream* in = nullptr;
    if (f == "-") {
      // [GNU] closed stdin (<&-) reports a read error instead of
      // dereferencing a bad stream (MSYS sed itself segfaults here).
      if (file_io::stdin_is_bad()) {
        safeErrorPrint("sed: can't read -: Bad file descriptor\n");
        error_level = std::max(error_level, 2);
        continue;
      }
      in = &std::cin;
    } else {
      file.open(f, std::ios::binary);
      if (!file.is_open()) {
        // [GNU] "sed: can't read FILE: reason" + EXIT_BAD_INPUT (2)
        // (execute.c:562-566)
        safeErrorPrint("sed: can't read " + f + ": " +
                       std::string(std::strerror(errno)) + "\n");
        error_level = std::max(error_level, 2);
        continue;
      }
      in = &file;
    }

    if (cfg.separate_files || cfg.in_place) {
      size_t reset_slots = 0;
      for (const auto& s : cfg.scripts)
        reset_slots += 1 + s.group_addresses.size();
      states.assign(reset_slots, {});
      runtime.hold.clear();
      runtime.hold_had_delimiter = false;
      line_no = 0;
    }

    const bool final_input =
        cfg.separate_files || (file_index + 1 == expanded_files.size());

    if (can_use_literal_stream_fast_path(cfg)) {
      std::string content = read_binary_stream(*in);
      std::string output = apply_literal_stream_fast_path(content, cfg);
      if (cfg.in_place) {
        file.close();
        if (!replace_file_atomically(f, cfg.in_place_suffix, output)) {
          error_level = std::max(error_level, 1);
        }
      } else {
        safePrint(output);
      }
      continue;
    }

    const std::string_view current_file =
        f == "-" ? std::string_view("-") : std::string_view(f);
    if (cfg.in_place) {
      std::string output;
      auto quit_exit_code = process_stream(*in, cfg, states, runtime, line_no,
                                           true, current_file, &output);
      if (runtime.io_error) error_level = std::max(error_level, 2);
      file.close();
      if (!replace_file_atomically(f, cfg.in_place_suffix, output)) {
        error_level = std::max(error_level, 1);
      }
      if (quit_exit_code) {
        return error_level != 0 ? error_level : *quit_exit_code;
      }
      continue;
    }

    if (auto quit_exit_code =
            process_stream(*in, cfg, states, runtime, line_no, final_input,
                           current_file, nullptr)) {
      if (runtime.io_error) error_level = std::max(error_level, 2);
      return error_level != 0 ? error_level : *quit_exit_code;
    }
    if (runtime.io_error) error_level = std::max(error_level, 2);
  }
  return error_level;
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

  // [GNU] -i with no input files: PANIC, exit 4 (utils.h:26,
  // execute.c:1672)
  if (cfg->in_place && cfg->files.empty()) {
    safeErrorPrint("sed: no input files\n");
    return 4;
  }

  return process_files(*cfg);
}
