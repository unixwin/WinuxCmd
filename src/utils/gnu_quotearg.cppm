// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// Faithful port of gnulib quotearg's shell_escape_quoting_style — the
// style GNU printf %q uses, and the one od uses for file operands in
// diagnostics (quotef).
//
// The port reproduces the shipped coreutils 8.32/9.4 behavior
// byte-for-byte, including the quirk behind uutils#9638: when the string
// contains a single quote, quotearg scans ahead in a suppressed-write
// mode and then reprocesses; `pending_shell_escape_end` is declared
// before the process_input label and therefore keeps its end-of-scan
// value on the second pass.  Net effect: if the argument contains '\''
// and ends with an escape-rendered byte, the FIRST escape renders as a
// bare \ooo inside the initial '...' quotes ('\001'... instead of
// ''$'\001'...).  Verified byte-exact against GNU 9.4 for inputs like
// $'\x01'\''$'\x01' -> '\001'\'''$'\001''.

module;

// GMF: global-namespace C names (size_t/uint32_t) that `import std`
// does not provide.
#include <cstddef>
#include <cstdint>

export module utils:gnu_quotearg;
import std;

export namespace gnu_quotearg {

namespace detail {

// Characters quotearg stores bare in every style: [0-9A-Za-z] plus
//   % + , - . / : ] _ { } # ~
// ({} # ~ are only contextually special — handled by position/length).
inline bool shell_always_safe(unsigned char c) {
  if (c >= '0' && c <= '9') return true;
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= 'a' && c <= 'z') return true;
  switch (c) {
    case '%':
    case '+':
    case ',':
    case '-':
    case '.':
    case '/':
    case ':':
    case ']':
    case '_':
      return true;
    default:
      return false;
  }
}

// Characters that force quoting but store plainly inside '...'.
inline bool shell_special(unsigned char c) {
  switch (c) {
    case ' ':
    case '!':
    case '"':
    case '$':
    case '&':
    case '(':
    case ')':
    case '*':
    case ';':
    case '<':
    case '=':
    case '>':
    case '[':
    case '^':
    case '`':
    case '|':
    case '?':
      return true;
    default:
      return false;
  }
}

// c-and-shell compatible chars: space, the always-safe set, '\'', and
// printable multibyte sequences.  Used for the "string with a quote may
// render as \"...\"" decision.
inline bool c_and_shell_compat(unsigned char c) {
  return c == ' ' || c == '\'' || shell_always_safe(c) || c == '{' ||
         c == '}' || c == '#' || c == '~';
}

// Named escapes rendered as $'\n' etc.
inline char named_escape(unsigned char c) {
  switch (c) {
    case '\a':
      return 'a';
    case '\b':
      return 'b';
    case '\f':
      return 'f';
    case '\n':
      return 'n';
    case '\r':
      return 'r';
    case '\t':
      return 't';
    case '\v':
      return 'v';
    default:
      return 0;
  }
}

// Decode one UTF-8 sequence at s[i]; returns its length and whether it
// is a printable scalar.  Invalid sequences get length 1, false.
inline size_t utf8_seq(std::string_view s, size_t i, bool& printable) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  printable = false;
  if (c < 0x80) {
    printable = c >= 0x20 && c != 0x7F;
    return 1;
  }
  uint32_t cp = 0;
  size_t len = 0;
  if (c >= 0xC2 && c <= 0xDF) {
    cp = c & 0x1F;
    len = 2;
  } else if (c >= 0xE0 && c <= 0xEF) {
    cp = c & 0x0F;
    len = 3;
  } else if (c >= 0xF0 && c <= 0xF4) {
    cp = c & 0x07;
    len = 4;
  } else {
    return 1;
  }
  if (i + len > s.size()) return 1;
  for (size_t k = 1; k < len; ++k) {
    unsigned char cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80) return 1;
    cp = (cp << 6) | (cc & 0x3F);
  }
  if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
      (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF)) {
    return 1;  // overlong / surrogate / out of range
  }
  // Approximation of c32isprint: control planes and format/space
  // separators are not printable; the rest of Unicode is.
  printable =
      !(cp < 0xA0 || (cp >= 0x2028 && cp <= 0x2029) || cp == 0xFEFF ||
        (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFFF0 && cp <= 0xFFFF));
  return len;
}

}  // namespace detail

// shell_escape_quoting_style: return ARG bare if it needs no quoting,
// else quoted.  Printable = printable in a UTF-8 locale.
inline std::string shell_escape_quote(std::string_view arg) {
  using detail::c_and_shell_compat;
  using detail::named_escape;
  using detail::shell_always_safe;
  using detail::shell_special;
  using detail::utf8_seq;

  const size_t n = arg.size();

  // Pass 0 (elide): decide whether quoting is needed at all.
  bool need = n == 0;
  for (size_t i = 0; !need && i < n; ++i) {
    unsigned char c = static_cast<unsigned char>(arg[i]);
    if (shell_always_safe(c)) continue;
    if (c == '{' || c == '}') {
      if (n != 1) continue;
      need = true;
    } else if (c == '#' || c == '~') {
      if (i != 0) continue;
      need = true;
    } else if (shell_special(c) || c == '\'' || c == '\\') {
      need = true;
    } else if (c < 0x80) {
      need = true;  // control char
    } else {
      bool printable;
      size_t len = utf8_seq(arg, i, printable);
      if (!printable) need = true;
      i += len - 1;
    }
  }
  if (!need) return std::string(arg);

  // Pass 1 (scan): quotearg switches to a suppressed-write scan when it
  // meets '\'' and reprocesses if the result can't use the shorter
  // "..." form.  The only output-affecting outcome of that scan is
  // whether pending_shell_escape_end was left set — which happens iff
  // the LAST byte was escape-rendered — combined with the reprocess
  // trigger (a '\'' present while some char is not c-compatible).
  bool has_squote = arg.find('\'') != std::string_view::npos;
  bool all_compat = true;
  bool last_escapes = false;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = static_cast<unsigned char>(arg[i]);
    bool escapes = false;
    bool compat = false;
    if (named_escape(c) || c < 0x20 || c == 0x7F) {
      escapes = true;
    } else if (c >= 0x80) {
      bool printable;
      size_t len = utf8_seq(arg, i, printable);
      escapes = !printable;
      compat = printable;
      i += len - 1;
    } else {
      compat = c_and_shell_compat(c);
    }
    if (!compat) all_compat = false;
    last_escapes = escapes;
  }

  if (has_squote && all_compat) {
    // c_quoting_style restyle: "..." with C escapes; the only chars
    // needing escapes in an all-compat string are '"' and '\\', which
    // are not c-compatible so they can't appear.
    std::string out = "\"";
    out += arg;
    out += '"';
    return out;
  }

  bool pending = has_squote && last_escapes;

  // Pass that writes: shell_always emission with $'...' escapes.
  std::string out;
  out.reserve(n + 8);
  out.push_back('\'');
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = static_cast<unsigned char>(arg[i]);
    char esc = named_escape(c);
    if (esc) {
      if (!pending) {
        out += "'$'";
        pending = true;
      }
      out.push_back('\\');
      out.push_back(esc);
      continue;
    }
    if (c == '\'') {
      out += "'\\''";
      pending = false;
      continue;
    }
    if (c < 0x20 || c == 0x7F || c >= 0x80) {
      bool printable = false;
      size_t len = 1;
      if (c >= 0x80) len = utf8_seq(arg, i, printable);
      if (!printable) {
        // octal escape each byte of the unprintable sequence
        for (size_t k = 0; k < len; ++k) {
          if (!pending) {
            out += "'$'";
            pending = true;
          }
          unsigned char b = static_cast<unsigned char>(arg[i + k]);
          out.push_back('\\');
          out.push_back(static_cast<char>('0' + (b >> 6)));
          out.push_back(static_cast<char>('0' + ((b >> 3) & 7)));
          out.push_back(static_cast<char>('0' + (b & 7)));
        }
        i += len - 1;
        continue;
      }
      // printable multibyte: END_ESC then copy bytes
      if (pending) {
        out += "''";
        pending = false;
      }
      out.append(arg.substr(i, len));
      i += len - 1;
      continue;
    }
    // ordinary byte (safe, special, '\\', space)
    if (pending) {
      out += "''";
      pending = false;
    }
    out.push_back(static_cast<char>(c));
  }
  out.push_back('\'');
  return out;
}

}  // namespace gnu_quotearg
