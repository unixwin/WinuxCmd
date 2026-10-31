// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tr.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <fcntl.h>
#include <io.h>

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr TR_OPTIONS = std::array{
    // [GNU] -c, --complement
    OPTION("-c", "--complement", "use the complement of SET1", BOOL_TYPE),
    // [EXT] -C
    OPTION("-C", "", "same as -c", BOOL_TYPE),
    // [GNU] -d, --delete
    OPTION("-d", "--delete", "delete characters in SET1, do not translate",
           BOOL_TYPE),
    // [GNU] -s, --squeeze-repeats
    OPTION("-s", "--squeeze-repeats",
           "replace each input sequence of a repeated character that is listed "
           "in SET1 with a single occurrence of that character",
           BOOL_TYPE),
    // [GNU] -t, --truncate-set1
    OPTION("-t", "--truncate-set1", "first truncate SET1 to length of SET2",
           BOOL_TYPE),
    // [GNU] -A: undocumented historical no-op, accepted and ignored
    OPTION("-A", "", "", BOOL_TYPE)};

namespace tr_pipeline {
namespace cp = core::pipeline;

auto make_dynamic_error(std::string message) -> cp::Error {
  static thread_local std::string storage;
  storage = std::move(message);
  return storage;
}

// [GNU] tr.c unquote diagnoses a lone backslash at the end of an operand:
//   tr: warning: an unescaped backslash at end of string is not portable
// Set by parse_atomic_token when it consumes a trailing unescaped
// backslash; reset per operand in parse_set and reported by build_config.
thread_local bool g_saw_trailing_backslash = false;

// [GNU] an ambiguous octal escape (\400-\777) is diagnosed by
// unquote (tr.c:471-512); the message is stashed here and reported
// by build_config after the operand parses.
thread_local std::string g_octal_warning;

// Parse escape sequences. Returns one character, or two for an
// ambiguous octal escape (see the octal case).
auto parse_escape_sequence(std::string_view& str) -> std::string {
  if (str.empty()) return "\\";

  char c = str[0];
  str = str.substr(1);

  switch (c) {
    case 'n':
      return "\n";
    case 't':
      return "\t";
    case 'r':
      return "\r";
    case 'b':
      return "\b";
    case 'f':
      return "\f";
    case 'v':
      return "\v";
    case 'a':
      return "\a";
    case '\\':
      return "\\";
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
      // Parse octal \NNN (at most 3 digits).
      std::string digits(1, c);
      int value = c - '0';
      while (digits.size() < 3 && !str.empty() && str[0] >= '0' &&
             str[0] <= '7') {
        digits += str[0];
        value = value * 8 + (str[0] - '0');
        str = str.substr(1);
      }
      if (digits.size() == 3 && value > 255) {
        // [GNU] the escape stops before the digit that would
        // overflow a byte: the set gains the two-digit value AND
        // the third digit as a literal character, with a warning
        // (audit #1462). The old code cast the 3-digit value to
        // char, yielding a single wrong byte and no warning.
        const int two = (digits[0] - '0') * 8 + (digits[1] - '0');
        char buf[8];
        snprintf(buf, sizeof(buf), "%03o", two);
        g_octal_warning =
            "warning: the ambiguous octal escape \\" + digits +
            " is being interpreted as the 2-byte sequence \\" + buf +
            ", " + digits[2];
        std::string out;
        out += static_cast<char>(two);
        out += digits[2];
        return out;
      }
      return std::string(1, static_cast<char>(value));
    }
    default:
      return std::string(1, c);
  }
}

auto ascii_class(std::string_view name) -> std::optional<std::string> {
  if (name == "lower") return std::string("abcdefghijklmnopqrstuvwxyz");
  if (name == "upper") return std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  if (name == "digit") return std::string("0123456789");
  // [GNU] class members come out in ascending byte order (the
  // get_next scan, tr.c:1055-1067): blank is TAB, SPACE and space
  // is TAB, LF, VT, FF, CR, SPACE. Order matters for positional
  // translation (audit #1462).
  if (name == "blank") return std::string("\t ");
  if (name == "space") return std::string("\t\n\v\f\r ");
  if (name == "alpha") {
    return std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  }
  if (name == "alnum") {
    return std::string(
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  }
  if (name == "xdigit") return std::string("0123456789ABCDEFabcdef");
  if (name == "cntrl") {
    std::string chars;
    for (int i = 0; i < 32; ++i) chars += static_cast<char>(i);
    chars += static_cast<char>(127);
    return chars;
  }
  if (name == "print") {
    std::string chars;
    for (char c = ' '; c <= '~'; ++c) chars += c;
    return chars;
  }
  if (name == "graph") {
    std::string chars;
    for (char c = '!'; c <= '~'; ++c) chars += c;
    return chars;
  }
  if (name == "punct") {
    return std::string("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
  }

  return std::nullopt;
}

// What a parsed atom was, for GNU's validate() checks (tr.c:1435).
struct AtomInfo {
  bool is_class = false;
  std::string class_name;
  bool is_equiv = false;
};

auto parse_atomic_token(std::string_view& str, AtomInfo* info = nullptr)
    -> cp::Result<std::string> {
  if (str.empty()) return std::unexpected("missing character");

  if (str[0] == '\\') {
    str = str.substr(1);
    if (str.empty()) {
      // [GNU] A backslash with nothing left to escape is taken literally
      // but is diagnosed as non-portable.
      g_saw_trailing_backslash = true;
      return std::string("\\");
    }
    return parse_escape_sequence(str);
  }

  if (str.size() >= 4 && str[0] == '[' && str[1] == ':') {
    size_t close = str.find(":]");
    if (close != std::string_view::npos) {
      std::string_view name = str.substr(2, close - 2);
      auto chars = ascii_class(name);
      if (!chars) {
        // [GNU] an empty class name has its own diagnostic
        // (tr.c:884) (audit #1462).
        if (name.empty()) {
          return std::unexpected("missing character class name '[::]'");
        }
        return std::unexpected(
            make_dynamic_error(std::string("invalid character class '") +
                               std::string(name) + "'"));
      }
      if (info) {
        info->is_class = true;
        info->class_name = std::string(name);
      }
      str = str.substr(close + 2);
      return *chars;
    }
  }

  char c = str[0];
  str = str.substr(1);
  return std::string(1, c);
}

// GNU tr treats [c*n] with the count omitted or zero as an "indefinite"
// repeat: it contributes nothing during parsing and only expands in string2
// to fill up to string1's effective length.
struct RepeatSpec {
  size_t count = 0;
  bool indefinite = false;
};

// The translator only needs a 256-entry table, and GNU expands repeats
// per-character with "last occurrence wins" (verified: tr '[a*10]x' 'yz'
// maps a->z, x->z). Any run longer than the longest possible operand maps
// exactly like an unbounded run would, so clamping keeps huge counts like
// [a*1000000000000] cheap and semantically identical (GNU itself stalls on
// such counts).
constexpr size_t kMaxMaterializedRepeat = 65536;

// Parse the digit segment of a [c*n] construct (everything between '*' and
// the closing bracket). An empty segment means an indefinite repeat; a
// malformed or overflowing count is a GNU error.
auto parse_repeat_spec(std::string_view digits) -> cp::Result<RepeatSpec> {
  RepeatSpec spec;
  if (digits.empty()) {
    spec.indefinite = true;  // [c*]
    return spec;
  }
  if (digits[0] < '0' || digits[0] > '9') {
    return std::unexpected(make_dynamic_error("invalid repeat count '" +
                                              std::string(digits) +
                                              "' in [c*n] construct"));
  }

  // GNU parses the count in octal when the first digit is '0' (xstrtoumax).
  size_t base = (digits[0] == '0') ? 8 : 10;
  size_t count = 0;
  bool overflow = false;
  for (char ch : digits) {
    if (ch < '0' || ch >= static_cast<char>('0' + base)) {
      return std::unexpected(make_dynamic_error("invalid repeat count '" +
                                                std::string(digits) +
                                                "' in [c*n] construct"));
    }
    size_t digit = static_cast<size_t>(ch - '0');
    if (count > (std::numeric_limits<size_t>::max() - digit) / base) {
      overflow = true;
      break;
    }
    count = count * base + digit;
  }
  if (overflow) {
    return std::unexpected(make_dynamic_error("invalid repeat count '" +
                                              std::string(digits) +
                                              "' in [c*n] construct"));
  }
  if (count == 0) {
    spec.indefinite = true;  // [c*0] behaves like [c*]
    return spec;
  }
  spec.count = count;
  return spec;
}

auto parse_set_atom(std::string_view& str, RepeatSpec* repeat_out,
                    AtomInfo* info = nullptr) -> cp::Result<std::string> {
  if (str.empty()) return std::unexpected("missing character");

  if (str[0] == '[') {
    size_t close = str.find(']');
    if (close != std::string_view::npos) {
      std::string_view body = str.substr(1, close - 1);

      if (body.size() >= 3 && body.front() == '=' && body.back() == '=') {
        std::string_view equiv = body.substr(1, body.size() - 2);
        auto atom = parse_atomic_token(equiv);
        if (atom && equiv.empty() && atom->size() == 1) {
          if (info) info->is_equiv = true;
          str = str.substr(close + 1);
          return *atom;
        }
        if (atom && equiv.empty()) {
          // [GNU] a well-formed [=...=] whose content is not
          // exactly one character is fatal (tr.c:913-925); the
          // caller rewrites this sentinel with the operand text
          // (audit #1462). The old code fell through and treated
          // the construct as literal characters.
          return std::unexpected("EQUIV_CLASS_SIZE:");
        }
      }

      if (body.find('*') != std::string_view::npos) {
        std::string_view repeated = body;
        auto atom = parse_atomic_token(repeated);
        if (atom && atom->size() == 1 && !repeated.empty() &&
            repeated[0] == '*') {
          // repeated now holds everything between '*' and the closing
          // bracket: the repeat-count digit segment.
          auto spec = parse_repeat_spec(repeated.substr(1));
          if (!spec) return std::unexpected(spec.error());
          str = str.substr(close + 1);
          if (repeat_out) {
            // Caller expands the repeat; hand back the bare character.
            *repeat_out = *spec;
            return *atom;
          }
          // No repeat context (e.g. a range endpoint): expand here.
          size_t n = spec->indefinite
                         ? 0
                         : std::min(spec->count, kMaxMaterializedRepeat);
          return std::string(n, (*atom)[0]);
        }
      }
    }
  }

  return parse_atomic_token(str);
}

// Parse a character set string, tracking [c*] indefinite repeats so the
// caller can apply GNU validation and string2 fill semantics.
struct SetParseResult {
  std::string chars;
  size_t indefinite_count = 0;
  char indefinite_char = 0;
  // Index within `chars` where the indefinite repeat appeared; the fill
  // character must be inserted at this position, not appended.
  size_t indefinite_pos = 0;
  // [GNU] validate() metadata (tr.c:1435-1495).
  bool has_equiv_class = false;
  bool has_char_class = false;
  bool has_restricted_class = false;  // a class other than upper/lower
  bool ends_with_class = false;       // last appended atom was a class
  std::vector<size_t> element_starts;  // expanded offset of each element
  // Expanded spans of [:upper:]/[:lower:] elements (validate_case_classes).
  std::vector<std::pair<size_t, size_t>> case_spans;
};

auto parse_set(std::string_view str) -> cp::Result<SetParseResult> {
  g_saw_trailing_backslash = false;
  const std::string_view operand = str;
  SetParseResult out;
  std::string& result = out.chars;

  // [GNU] "<operand>: equivalence class operand must be a single
  // character" -- parse_set_atom reports a sentinel; rewrite it
  // with the full operand text here.
  auto rewrite_error = [&](const cp::Error& err) -> cp::Error {
    if (err == "EQUIV_CLASS_SIZE:") {
      return make_dynamic_error(
          "'" + std::string(operand) +
          "': equivalence class operand must be a single character");
    }
    return err;
  };

  while (!str.empty()) {
    RepeatSpec spec;
    AtomInfo info;
    auto atom = parse_set_atom(str, &spec, &info);
    if (!atom) return std::unexpected(rewrite_error(atom.error()));
    if (info.is_equiv) out.has_equiv_class = true;
    if (info.is_class) {
      out.has_char_class = true;
      if (info.class_name != "upper" && info.class_name != "lower") {
        out.has_restricted_class = true;
      }
    }

    if (spec.indefinite) {
      ++out.indefinite_count;
      out.indefinite_char = (*atom)[0];
      out.indefinite_pos = result.size();
      out.ends_with_class = false;
      continue;  // contributes nothing until the fill phase
    }
    if (spec.count > 0) {
      out.element_starts.push_back(result.size());
      result +=
          std::string(std::min(spec.count, kMaxMaterializedRepeat), (*atom)[0]);
      out.ends_with_class = false;
      continue;
    }

    if (atom->size() == 1 && str.size() >= 2 && str[0] == '-') {
      std::string_view range_tail = str.substr(1);
      auto end_atom = parse_set_atom(range_tail, nullptr);
      if (!end_atom) return std::unexpected(rewrite_error(end_atom.error()));

      if (end_atom->size() == 1) {
        unsigned char start = static_cast<unsigned char>((*atom)[0]);
        unsigned char end = static_cast<unsigned char>((*end_atom)[0]);
        if (start > end) {
          std::string range_text;
          range_text += (*atom)[0];
          range_text += '-';
          range_text += (*end_atom)[0];
          return std::unexpected(make_dynamic_error(
              std::string("range-endpoints of '") + range_text +
              "' are in reverse collating sequence order"));
        }
        out.element_starts.push_back(result.size());
        for (unsigned int c = start; c <= end; ++c) {
          result += static_cast<char>(c);
        }
        out.ends_with_class = false;
        str = range_tail;
      } else {
        result += *atom;
        result += '-';
        out.ends_with_class = false;
        str = str.substr(1);
      }
    } else {
      out.element_starts.push_back(result.size());
      const size_t span_start = result.size();
      result += *atom;
      if (info.is_class && (info.class_name == "upper" ||
                            info.class_name == "lower")) {
        out.case_spans.emplace_back(span_start, result.size());
      }
      out.ends_with_class = info.is_class;
    }
  }

  return out;
}

// Build translation table
void build_translation_table(const std::string& set1, const std::string& set2,
                             bool complement, bool truncate_set1,
                             char fill_char, size_t fill_pos,
                             std::array<char, 256>& table) {
  // Initialize table to no change
  for (int i = 0; i < 256; ++i) {
    table[i] = static_cast<char>(i);
  }

  std::string effective_set1 = set1;
  std::string effective_set2 = set2;

  // Handle complement
  if (complement) {
    std::string temp;
    for (int i = 0; i < 256; ++i) {
      char c = static_cast<char>(i);
      if (set1.find(c) == std::string::npos) {
        temp += c;
      }
    }
    effective_set1 = temp;
  }

  // Handle truncate
  if (truncate_set1 && effective_set1.size() > effective_set2.size()) {
    effective_set1 = effective_set1.substr(0, effective_set2.size());
  }

  // GNU: an indefinite repeat [c*] in string2 expands to bring string2 up
  // to string1's effective length, inserted at the repeat's own position.
  if (fill_char != '\0' && effective_set2.size() < effective_set1.size()) {
    size_t count = effective_set1.size() - effective_set2.size();
    if (fill_pos > effective_set2.size()) fill_pos = effective_set2.size();
    effective_set2.insert(fill_pos, count, fill_char);
  }

  // Build translation
  size_t len = std::min(effective_set1.size(), effective_set2.size());
  for (size_t i = 0; i < len; ++i) {
    table[static_cast<unsigned char>(effective_set1[i])] = effective_set2[i];
  }

  // If set2 is shorter, repeat last character
  if (!effective_set2.empty() &&
      effective_set1.size() > effective_set2.size()) {
    char last = effective_set2.back();
    for (size_t i = effective_set2.size(); i < effective_set1.size(); ++i) {
      table[static_cast<unsigned char>(effective_set1[i])] = last;
    }
  }
}

// Build delete set
auto build_delete_set(const std::string& set1, bool complement)
    -> std::array<bool, 256> {
  std::array<bool, 256> delete_set{};
  delete_set.fill(false);

  if (complement) {
    // Mark all characters as to be deleted
    delete_set.fill(true);
    // Unmark characters in set1
    for (char c : set1) {
      delete_set[static_cast<unsigned char>(c)] = false;
    }
  } else {
    // Mark characters in set1 as to be deleted
    for (char c : set1) {
      delete_set[static_cast<unsigned char>(c)] = true;
    }
  }

  return delete_set;
}

// Build squeeze set
auto build_squeeze_set(const std::string& set1, bool complement)
    -> std::array<bool, 256> {
  std::array<bool, 256> squeeze_set{};
  squeeze_set.fill(false);

  if (complement) {
    squeeze_set.fill(true);
    for (char c : set1) {
      squeeze_set[static_cast<unsigned char>(c)] = false;
    }
  } else {
    for (char c : set1) {
      squeeze_set[static_cast<unsigned char>(c)] = true;
    }
  }

  return squeeze_set;
}

struct Config {
  bool complement = false;
  bool delete_mode = false;
  bool squeeze = false;
  bool truncate_set1 = false;
  std::string set1;
  std::string set2;
  // GNU [c*] fill state for string2 (only meaningful when translating).
  char fill_char = 0;
  size_t fill_pos = 0;
};

auto build_config(const CommandContext<TR_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.complement = ctx.get<bool>("--complement", false) ||
                   ctx.get<bool>("-c", false) || ctx.get<bool>("-C", false);
  cfg.delete_mode =
      ctx.get<bool>("--delete", false) || ctx.get<bool>("-d", false);
  cfg.squeeze =
      ctx.get<bool>("--squeeze-repeats", false) || ctx.get<bool>("-s", false);
  cfg.truncate_set1 =
      ctx.get<bool>("--truncate-set1", false) || ctx.get<bool>("-t", false);

  // For -ds mode, both sets are needed
  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  if (!cfg.delete_mode && !cfg.squeeze && ctx.positionals.size() == 1) {
    return std::unexpected("missing operand after set1 for translate");
  }

  if (cfg.delete_mode && cfg.squeeze && ctx.positionals.size() == 1) {
    return std::unexpected("missing operand after set1 for delete+squeeze");
  }

  if (cfg.delete_mode && !cfg.squeeze && ctx.positionals.size() > 1) {
    return std::unexpected("extra operand after delete");
  }

  // [GNU] warn for a lone backslash at the end of each SET operand; the
  // warning is emitted even when a later operand fails to parse.
  auto warn_trailing_backslash = [] {
    if (g_saw_trailing_backslash) {
      g_saw_trailing_backslash = false;
      safeErrorPrintLn(winux::i18n::format(
          "command.tr.warn.trailing_backslash",
          "tr: warning: an unescaped backslash at end of string is not "
          "portable"));
    }
  };

  auto warn_octal = [] {
    if (!g_octal_warning.empty()) {
      safeErrorPrintLn("tr: " + g_octal_warning);
      g_octal_warning.clear();
    }
  };

  auto set1 = parse_set(ctx.positionals[0]);
  warn_trailing_backslash();
  warn_octal();
  if (!set1) return std::unexpected(set1.error());

  // In -ds mode, the second argument is the squeeze set, not the
  // translation string.
  std::optional<decltype(parse_set(ctx.positionals[0]))> set2;
  if (ctx.positionals.size() > 1) {
    set2 = parse_set(ctx.positionals[1]);
    warn_trailing_backslash();
    warn_octal();
    if (!*set2) return std::unexpected((*set2).error());
  }

  // [GNU] validate() (tr.c:1435-1495), in GNU's order. The old code
  // performed none of the translating checks (audit #1462).
  const bool translating = !cfg.delete_mode;
  if (set1->indefinite_count > 0) {
    return std::unexpected(make_dynamic_error(
        "the [c*] repeat construct may not appear in string1"));
  }
  cfg.set1 = set1->chars;

  if (set2) {
    const auto& s2 = **set2;
    if (s2.indefinite_count > 1) {
      return std::unexpected(make_dynamic_error(
          "only one [c*] repeat construct may appear in string2"));
    }
    if (translating) {
      if (s2.has_equiv_class) {
        return std::unexpected(make_dynamic_error(
            "[=c=] expressions may not appear in string2 when "
            "translating"));
      }
      if (s2.has_restricted_class) {
        return std::unexpected(make_dynamic_error(
            "when translating, the only character classes that may "
            "appear in\nstring2 are 'upper' and 'lower'"));
      }
      // validate_case_classes: a case class starting in string2
      // must coincide with a case class starting in string1.
      // (GNU skips the check entirely when complementing.)
      if (!cfg.complement) {
        for (const auto& span : s2.case_spans) {
          if (span.first >= set1->chars.size()) break;
          bool aligned = false;
          for (const auto& s1_span : set1->case_spans) {
            if (s1_span.first == span.first) {
              aligned = true;
              break;
            }
          }
          if (!aligned) {
            return std::unexpected(make_dynamic_error(
                "misaligned [:upper:] and/or [:lower:] construct"));
          }
        }
      }
      // string2's effective length, with an indefinite repeat
      // resolved to fill string1 (get_s2_spec_stats).
      const size_t eff2 =
          s2.indefinite_count > 0
              ? std::max(s2.chars.size(), set1->chars.size())
              : s2.chars.size();
      if (!cfg.truncate_set1 && eff2 < set1->chars.size()) {
        if (eff2 == 0) {
          return std::unexpected(make_dynamic_error(
              "when not truncating set1, string2 must be non-empty"));
        }
        if (s2.ends_with_class) {
          return std::unexpected(make_dynamic_error(
              "when translating with string1 longer than string2, "
              "the latter string must not end with a character class"));
        }
      }
      if (cfg.complement && set1->has_char_class) {
        std::string eff = s2.chars;
        if (s2.indefinite_count > 0 && eff.size() < set1->chars.size()) {
          const size_t pos = std::min(s2.indefinite_pos, eff.size());
          eff.insert(pos, set1->chars.size() - eff.size(),
                     s2.indefinite_char);
        }
        const bool homogeneous =
            !eff.empty() &&
            std::all_of(eff.begin(), eff.end(),
                        [&](char c) { return c == eff[0]; });
        if (!(eff.size() == set1->chars.size() && homogeneous)) {
          return std::unexpected(make_dynamic_error(
              "when translating with complemented character "
              "classes,\nstring2 must map all characters in the "
              "domain to one"));
        }
      }
      if (s2.indefinite_count > 0) {
        cfg.fill_char = s2.indefinite_char;
        cfg.fill_pos = s2.indefinite_pos;
      }
    } else if (s2.indefinite_count > 0) {
      return std::unexpected(make_dynamic_error(
          "the [c*] construct may appear in string2 only when translating"));
    }
    cfg.set2 = s2.chars;
  } else {
    cfg.set2 = "";  // Default empty set
  }

  if (ctx.positionals.size() > 2) {
    return std::unexpected("extra operand after set2");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  std::array<char, 256> trans_table;
  std::array<bool, 256> delete_set{};
  std::array<bool, 256> squeeze_set{};

  // Build tables based on mode
  if (cfg.delete_mode) {
    delete_set = build_delete_set(cfg.set1, cfg.complement);
  } else {
    build_translation_table(cfg.set1, cfg.set2, cfg.complement,
                            cfg.truncate_set1, cfg.fill_char, cfg.fill_pos,
                            trans_table);
  }

  if (cfg.squeeze) {
    // GNU squeezes repeated characters from the last specified set after
    // deletion or translation has been applied.
    if (!cfg.set2.empty()) {
      squeeze_set = build_squeeze_set(cfg.set2, false);
      // An indefinite [c*] in string2 is part of the squeeze set too.
      if (cfg.fill_char != '\0' && cfg.set2.size() < cfg.set1.size()) {
        squeeze_set[static_cast<unsigned char>(cfg.fill_char)] = true;
      }
    } else {
      squeeze_set = build_squeeze_set(cfg.set1, cfg.complement);
    }
  }

  std::array<char, 64 * 1024> input_buffer{};
  std::array<char, 64 * 1024> output_buffer{};
  char prev_char = '\0';
  bool prev_was_squeezed = false;

  auto write_block = [](const char* data, size_t size) -> bool {
    return size == 0 || ::fwrite(data, 1, size, stdout) == size;
  };

  // GNU tr keeps its hot paths streaming through read_and_xlate,
  // read_and_delete, and squeeze_filter. Keep the same shape here instead of
  // materializing stdin/stdout as whole strings.
  while (true) {
    size_t got = ::fread(input_buffer.data(), 1, input_buffer.size(), stdin);
    if (got == 0) break;

    if (!cfg.delete_mode && !cfg.squeeze) {
      for (size_t i = 0; i < got; ++i) {
        unsigned char uc = static_cast<unsigned char>(input_buffer[i]);
        input_buffer[i] = trans_table[uc];
      }
      if (!write_block(input_buffer.data(), got)) {
        safeErrorPrintLn("tr: write error");
        return 1;
      }
      continue;
    }

    size_t out_len = 0;
    for (size_t pos = 0; pos < got; ++pos) {
      char c = input_buffer[pos];
      unsigned char uc = static_cast<unsigned char>(c);

      if (cfg.delete_mode && delete_set[uc]) {
        continue;
      }

      char translated = cfg.delete_mode ? c : trans_table[uc];
      unsigned char translated_uc = static_cast<unsigned char>(translated);

      if (cfg.squeeze && squeeze_set[translated_uc]) {
        if (prev_was_squeezed && translated == prev_char) {
          continue;
        }
        prev_char = translated;
        prev_was_squeezed = true;
      } else {
        prev_char = translated;
        prev_was_squeezed = false;
      }

      output_buffer[out_len++] = translated;
    }

    if (!write_block(output_buffer.data(), out_len)) {
      safeErrorPrintLn("tr: write error");
      return 1;
    }
  }

  if (::ferror(stdin)) {
    safeErrorPrintLn("tr: read error");
    return 1;
  }

  return 0;
}

}  // namespace tr_pipeline

REGISTER_COMMAND(
    tr, "tr", "tr [OPTION]... SET1 [SET2]",
    "Translate, squeeze, and/or delete characters from standard input,\n"
    "writing to standard output.\n"
    "\n"
    "SET1 and SET2 specify arrays of characters to translate, delete,\n"
    "or squeeze. Interpreted sequences are:\n"
    "  \\\\NNN  character with octal value NNN (1 to 3 digits)\n"
    "  \\\\\\\\\\  backslash\n"
    "  \\a    audible BEL\n"
    "  \\b    backspace\n"
    "  \\f    form feed\n"
    "  \\n    new line\n"
    "  \\r    return\n"
    "  \\t    horizontal tab\n"
    "  \\v    vertical tab\n"
    "CHAR1-CHAR2  all characters from CHAR1 to CHAR2 in ascending order\n"
    "[:CLASS:]    ASCII classes: alnum, alpha, blank, cntrl, digit,\n"
    "             graph, lower, print, punct, space, upper, xdigit",
    "  echo 'hello' | tr 'a-z' 'A-Z'\n"
    "  echo 'hello   world' | tr -s ' '\n"
    "  echo 'Hello World' | tr -d 'a-z'\n"
    "  echo 'Hello World' | tr -c 'A-Z' '*'",
    "expand(1), unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    TR_OPTIONS) {
  using namespace tr_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      cp::report_error(cfg_result, L"tr");
      safeErrorPrintLn("Try 'tr --help' for more information.");
      return 1;
    }
    if (cfg_result.error() == "missing operand after set1 for translate") {
      safeErrorPrintLn(
          std::format("tr: missing operand after '{}'", ctx.positionals[0]));
      safeErrorPrintLn("Two strings must be given when translating.");
      safeErrorPrintLn("Try 'tr --help' for more information.");
      return 1;
    }
    if (cfg_result.error() == "missing operand after set1 for delete+squeeze") {
      safeErrorPrintLn(
          std::format("tr: missing operand after '{}'", ctx.positionals[0]));
      safeErrorPrintLn(
          "Two strings must be given when deleting and squeezing.");
      safeErrorPrintLn("Try 'tr --help' for more information.");
      return 1;
    }
    if (cfg_result.error() == "extra operand after delete") {
      safeErrorPrintLn(
          std::format("tr: extra operand '{}'", ctx.positionals[1]));
      safeErrorPrintLn(
          "Only one string may be given when deleting without squeezing "
          "repeats.");
      safeErrorPrintLn("Try 'tr --help' for more information.");
      return 1;
    }
    if (cfg_result.error() == "extra operand after set2") {
      safeErrorPrintLn(
          std::format("tr: extra operand '{}'", ctx.positionals[2]));
      safeErrorPrintLn("Try 'tr --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"tr");
    return 1;
  }

  return run(*cfg_result);
}
