// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Shared GNU-faithful expression parser for `test` and `[`.
///   Mirrors coreutils test.c (9.x): posixtest() dispatches on the argument
///   count (special unary/binary forms up to 4 arguments, expr() beyond),
///   diagnostics use GNU wording, and file predicates use stat() semantics
///   that follow the final symlink/junction (issues 236, 963, 1063).
///
///   This header is textually included by test.cpp and test_bracket.cpp.
///   It must be included AFTER "pch/pch.h" and `import utils;` because it
///   uses native_path:: helpers and Win32 types.

#pragma once

// SE_FILE_OBJECT / GetNamedSecurityInfoW live in the ACL API headers.
#include <AccCtrl.h>
#include <AclAPI.h>

namespace test_expr {

/**
 * @brief File metadata gathered with stat() semantics — the final
 * component's reparse point is FOLLOWED, so dangling links report
 * "not found" just like POSIX stat().
 */
struct FileInfo {
  bool exists = false;
  bool is_directory = false;
  bool is_readonly = false;
  bool is_char_device = false;
  bool is_pipe = false;
  unsigned long long size = 0;
  unsigned long long volume_serial = 0;
  unsigned long long file_index = 0;
  FILETIME mtime{};
  FILETIME atime{};

  [[nodiscard]] auto is_regular() const -> bool {
    return !is_directory && !is_char_device && !is_pipe;
  }
};

/**
 * @brief stat() equivalent: opens the operand without
 * FILE_FLAG_OPEN_REPARSE_POINT so the final symlink/junction is followed
 * and dangling links fail to open (POSIX stat() ENOENT).  Desired access
 * 0 queries metadata only, so read-protected files still succeed.
 */
inline auto stat_follow(const std::string& path) -> FileInfo {
  FileInfo info;
  const auto operand = native_path::make_api_path_operand(path);
  const DWORD link_attrs = GetFileAttributesW(operand.extended.c_str());
  const bool link_is_reparse = link_attrs != INVALID_FILE_ATTRIBUTES &&
                               (link_attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  HANDLE handle =
      CreateFileW(operand.extended.c_str(), 0,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    // A non-reparse operand that exists but cannot be opened still
    // "exists" for stat purposes.
    if (link_attrs != INVALID_FILE_ATTRIBUTES && !link_is_reparse) {
      info.exists = true;
      info.is_directory = (link_attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
      info.is_readonly = (link_attrs & FILE_ATTRIBUTE_READONLY) != 0;
      WIN32_FILE_ATTRIBUTE_DATA data{};
      if (GetFileAttributesExW(operand.extended.c_str(), GetFileExInfoStandard,
                               &data)) {
        info.size =
            (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
            data.nFileSizeLow;
        info.mtime = data.ftLastWriteTime;
        info.atime = data.ftLastAccessTime;
      }
    }
    return info;
  }

  const DWORD type = GetFileType(handle);
  if (type == FILE_TYPE_CHAR) {
    // NUL, CON, ...  (-f must be false, -c true; issue 1063)
    info.exists = true;
    info.is_char_device = true;
  } else if (type == FILE_TYPE_PIPE) {
    info.exists = true;
    info.is_pipe = true;
  } else {
    BY_HANDLE_FILE_INFORMATION bh{};
    if (GetFileInformationByHandle(handle, &bh)) {
      info.exists = true;
      info.is_directory = (bh.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      // A WinuxCmd fifo marker (#1038) is an on-disk file that stat()
      // reports as a pipe (S_IFIFO, st_size 0).
      if (!info.is_directory &&
          native_path::is_winux_fifo_w(operand.normalized)) {
        info.is_pipe = true;
        info.size = 0;
        CloseHandle(handle);
        return info;
      }
      info.is_readonly = (bh.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
      info.size = (static_cast<unsigned long long>(bh.nFileSizeHigh) << 32) |
                  bh.nFileSizeLow;
      info.mtime = bh.ftLastWriteTime;
      info.atime = bh.ftLastAccessTime;
      info.volume_serial = bh.dwVolumeSerialNumber;
      info.file_index =
          (static_cast<unsigned long long>(bh.nFileIndexHigh) << 32) |
          bh.nFileIndexLow;
    } else {
      info.exists = true;
    }
  }
  CloseHandle(handle);
  return info;
}

/**
 * @brief lstat() equivalent: true only when the operand itself is a
 * reparse point (symlink or junction).
 */
inline auto is_link_operand(const std::string& path) -> bool {
  const auto operand = native_path::make_api_path_operand(path);
  const DWORD attrs = GetFileAttributesW(operand.extended.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

/**
 * @brief True when the file's owner/group SID equals the current process
 * token's user / primary group SID (approximates GNU's euid/egid tests).
 */
inline auto file_owner_matches(const std::string& path,
                               TOKEN_INFORMATION_CLASS info_class) -> bool {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  DWORD need = 0;
  GetTokenInformation(token, info_class, nullptr, 0, &need);
  std::vector<BYTE> buffer(need);
  const bool got = need != 0 && GetTokenInformation(token, info_class,
                                                    buffer.data(), need, &need);
  CloseHandle(token);
  if (!got) return false;

  const PSID wanted =
      info_class == TokenUser
          ? reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid
          : reinterpret_cast<const TOKEN_GROUPS*>(buffer.data())->Groups->Sid;

  const auto operand = native_path::make_api_path_operand(path);
  PSID sid = nullptr;
  PSECURITY_DESCRIPTOR sd = nullptr;
  const DWORD rc = GetNamedSecurityInfoW(
      operand.extended.c_str(), SE_FILE_OBJECT,
      info_class == TokenUser ? OWNER_SECURITY_INFORMATION
                              : GROUP_SECURITY_INFORMATION,
      &sid, nullptr, nullptr, nullptr, &sd);
  if (rc != ERROR_SUCCESS) return false;
  const bool match = sid != nullptr && EqualSid(sid, wanted);
  LocalFree(sd);
  return match;
}

inline auto has_executable_extension(const std::string& path) -> bool {
  const auto dot = path.find_last_of('.');
  const auto sep = path.find_last_of("\\/");
  if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) {
    return false;
  }
  std::string ext = path.substr(dot + 1);
  for (auto& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext == "exe" || ext == "bat" || ext == "cmd" || ext == "com" ||
         ext == "ps1";
}

/**
 * @brief GNU find_int(): leading blanks skipped, optional '+'/'-', at
 * least one digit, then only blanks to the end.
 */
struct IntOperand {
  bool negative = false;
  std::string magnitude;  ///< digits only, no leading zeros ("0" for zero)
};

inline auto parse_int_operand(const std::string& raw)
    -> std::optional<IntOperand> {
  const auto first = raw.find_first_not_of(" \t\n\v\f\r");
  const auto last = raw.find_last_not_of(" \t\n\v\f\r");
  if (first == std::string::npos) return std::nullopt;
  std::string_view trimmed(raw.data() + first, last - first + 1);
  IntOperand result;
  if (trimmed.front() == '+' || trimmed.front() == '-') {
    result.negative = trimmed.front() == '-';
    trimmed.remove_prefix(1);
  }
  if (trimmed.empty() || !std::ranges::all_of(trimmed, [](char c) {
        return c >= '0' && c <= '9';
      })) {
    return std::nullopt;
  }
  const auto nonzero = trimmed.find_first_not_of('0');
  if (nonzero == std::string_view::npos) {
    result.negative = false;
    result.magnitude = "0";
  } else {
    result.magnitude = std::string(trimmed.substr(nonzero));
  }
  return result;
}

/// Arbitrary-precision magnitude comparison (operands have no sign).
inline auto compare_bignum(std::string_view a, std::string_view b) -> int {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  if (a == b) return 0;
  return a < b ? -1 : 1;
}

inline auto compare_int_operands(const IntOperand& a, const IntOperand& b)
    -> int {
  if (a.negative != b.negative) return a.negative ? -1 : 1;
  const int cmp = compare_bignum(a.magnitude, b.magnitude);
  return a.negative ? -cmp : cmp;
}

/**
 * @brief GNU-faithful test/[ expression parser.
 *
 * Mirrors coreutils test.c: posixtest() picks the interpretation by
 * argument count, term()/and()/or() handle '!', '-a', '-o' and
 * '( expr )', and the diagnostics match GNU:
 *   "missing argument after 'X'", "'X': binary operator expected",
 *   "'-X': unary operator expected", "extra argument 'X'",
 *   "')' expected[, found 'X']", "invalid integer 'X'".
 */
class Parser {
 public:
  explicit Parser(std::span<const std::string> args) : args_(args) {}

  auto parse() -> int {
    if (args_.empty()) return 1;
    const int value = posixtest(args_.size());
    if (!error_.empty()) return 2;
    if (pos_ != args_.size()) {
      fail("extra argument '" + std::string(args_[pos_]) + "'");
      return 2;
    }
    return value;
  }

  [[nodiscard]] auto error() const -> const std::string& { return error_; }

 private:
  auto at(size_t idx) const -> std::string_view {
    return idx < args_.size() ? std::string_view(args_[idx])
                              : std::string_view();
  }

  static auto is_binop(std::string_view s) -> bool {
    return s == "=" || s == "!=" || s == "==" || s == "-nt" || s == "-ot" ||
           s == "-ef" || s == "-eq" || s == "-ne" || s == "-lt" || s == "-le" ||
           s == "-gt" || s == "-ge";
  }

  static auto is_unary_char(char c) -> bool {
    // GNU test.c unary_operator() switch cases.
    return std::string_view("GLNOSbcdefghknprstuwxz").find(c) !=
           std::string_view::npos;
  }

  auto fail(std::string msg) -> int {
    if (error_.empty()) error_ = std::move(msg);
    return 2;
  }

  /// GNU beyond(): "missing argument after '<last argument>'".
  auto beyond() -> int {
    return fail("missing argument after '" + std::string(args_.back()) + "'");
  }

  static auto invert(int status) -> int { return status == 0 ? 1 : 0; }

  /// GNU posixtest(): dispatch by the number of remaining arguments.
  auto posixtest(size_t nargs) -> int {
    switch (nargs) {
      case 1:
        return one_argument();
      case 2:
        return two_arguments();
      case 3:
        return three_arguments();
      case 4:
        if (at(pos_) == "!") {
          ++pos_;
          if (pos_ >= args_.size()) return beyond();
          return invert(three_arguments());
        }
        if (at(pos_) == "(" && at(pos_ + 3) == ")") {
          ++pos_;
          const int value = two_arguments();
          ++pos_;
          return value;
        }
        [[fallthrough]];
      default:
        return expr();
    }
  }

  auto one_argument() -> int { return args_[pos_++].empty() ? 1 : 0; }

  auto two_arguments() -> int {
    if (at(pos_) == "!") {
      ++pos_;
      return invert(one_argument());
    }
    // Any two-character "-X" goes through unary_operator(); the switch
    // rejects unknown letters with "unary operator expected".
    if (args_[pos_].size() == 2 && args_[pos_].front() == '-') {
      return unary_operator_eval();
    }
    return beyond();
  }

  auto three_arguments() -> int {
    if (is_binop(args_[pos_ + 1])) return binary_operator(false);
    if (at(pos_) == "!") {
      ++pos_;
      if (pos_ >= args_.size()) return beyond();
      return invert(two_arguments());
    }
    if (at(pos_) == "(" && at(pos_ + 2) == ")") {
      ++pos_;
      const int value = one_argument();
      ++pos_;
      return value;
    }
    if (args_[pos_ + 1] == "-a" || args_[pos_ + 1] == "-o") return expr();
    return fail("'" + std::string(args_[pos_ + 1]) +
                "': binary operator expected");
  }

  auto expr() -> int {
    if (pos_ >= args_.size()) return beyond();
    return or_expr();
  }

  auto or_expr() -> int {
    int value = and_expr();
    while (error_.empty() && pos_ < args_.size() && args_[pos_] == "-o") {
      ++pos_;
      const int right = and_expr();
      value = (value == 0 || right == 0) ? 0 : 1;
    }
    return value;
  }

  auto and_expr() -> int {
    int value = term();
    while (error_.empty() && pos_ < args_.size() && args_[pos_] == "-a") {
      ++pos_;
      const int right = term();
      value = (value == 0 && right == 0) ? 0 : 1;
    }
    return value;
  }

  auto term() -> int {
    bool negated = false;
    while (pos_ < args_.size() && args_[pos_] == "!") {
      ++pos_;
      if (pos_ >= args_.size()) return beyond();
      negated = !negated;
    }
    if (pos_ >= args_.size()) return beyond();

    int value;
    if (args_[pos_] == "(") {
      ++pos_;
      if (pos_ >= args_.size()) return beyond();
      // Scan for ')' over up to four arguments, like GNU.
      size_t nargs = 1;
      for (; pos_ + nargs < args_.size() && args_[pos_ + nargs] != ")";
           ++nargs) {
        if (nargs == 4) {
          nargs = args_.size() - pos_;
          break;
        }
      }
      value = posixtest(nargs);
      if (!error_.empty()) return 2;
      if (pos_ >= args_.size()) return fail("')' expected");
      if (args_[pos_] != ")") {
        return fail("')' expected, found '" + std::string(args_[pos_]) + "'");
      }
      ++pos_;
    } else if (args_.size() - pos_ >= 4 && args_[pos_] == "-l" &&
               is_binop(args_[pos_ + 2])) {
      value = binary_operator(true);
      if (!error_.empty()) return 2;
    } else if (args_.size() - pos_ >= 3 && is_binop(args_[pos_ + 1])) {
      value = binary_operator(false);
      if (!error_.empty()) return 2;
    } else if (args_[pos_].size() == 2 && args_[pos_].front() == '-') {
      value = unary_operator_eval();
      if (!error_.empty()) return 2;
    } else {
      value = args_[pos_].empty() ? 1 : 0;
      ++pos_;
    }
    return negated ? invert(value) : value;
  }

  auto binary_operator(bool l_is_l) -> int {
    if (l_is_l) ++pos_;
    const size_t op = pos_ + 1;
    bool r_is_l = false;
    if (op < args_.size() - 2 && args_[op + 1] == "-l") {
      r_is_l = true;
      ++pos_;
    }
    const std::string_view ops = args_[op];

    if (!ops.empty() && ops.front() == '-') {
      if (ops == "-eq" || ops == "-ne" || ops == "-lt" || ops == "-le" ||
          ops == "-gt" || ops == "-ge") {
        IntOperand left;
        if (l_is_l) {
          left.magnitude = std::to_string(args_[op - 1].size());
        } else if (auto parsed = parse_int_operand(std::string(args_[op - 1]));
                   parsed) {
          left = std::move(*parsed);
        } else {
          return fail("invalid integer '" + std::string(args_[op - 1]) + "'");
        }
        IntOperand right;
        if (r_is_l) {
          right.magnitude = std::to_string(args_[op + 2].size());
        } else if (auto parsed = parse_int_operand(std::string(args_[op + 1]));
                   parsed) {
          right = std::move(*parsed);
        } else {
          return fail("invalid integer '" + std::string(args_[op + 1]) + "'");
        }
        const int cmp = compare_int_operands(left, right);
        pos_ += 3;
        if (ops == "-eq") return cmp == 0 ? 0 : 1;
        if (ops == "-ne") return cmp != 0 ? 0 : 1;
        if (ops == "-lt") return cmp < 0 ? 0 : 1;
        if (ops == "-le") return cmp <= 0 ? 0 : 1;
        if (ops == "-gt") return cmp > 0 ? 0 : 1;
        return cmp >= 0 ? 0 : 1;  // -ge
      }

      if (ops == "-nt" || ops == "-ot" || ops == "-ef") {
        pos_ += 3;
        if (l_is_l || r_is_l) {
          return fail(std::string(ops) + " does not accept -l");
        }
        const FileInfo left = stat_follow(std::string(args_[op - 1]));
        const FileInfo right = stat_follow(std::string(args_[op + 1]));
        if (ops == "-nt") {
          return left.exists && (!right.exists ||
                                 CompareFileTime(&left.mtime, &right.mtime) > 0)
                     ? 0
                     : 1;
        }
        if (ops == "-ot") {
          return right.exists &&
                         (!left.exists ||
                          CompareFileTime(&left.mtime, &right.mtime) < 0)
                     ? 0
                     : 1;
        }
        // -ef: same device and file index (hard-link test).
        return left.exists && right.exists &&
                       left.volume_serial == right.volume_serial &&
                       left.file_index == right.file_index
                   ? 0
                   : 1;
      }

      return fail("'" + std::string(ops) + "': unknown binary operator");
    }

    // String comparisons index relative to (possibly advanced) pos_,
    // exactly like GNU's argv[pos] / argv[pos + 2].
    if (ops == "=" || ops == "==") {
      const bool value = args_[pos_] == args_[pos_ + 2];
      pos_ += 3;
      return value ? 0 : 1;
    }
    // ops == "!="
    const bool value = args_[pos_] != args_[pos_ + 2];
    pos_ += 3;
    return value ? 0 : 1;
  }

  auto unary_operator_eval() -> int {
    // args_[pos_] is exactly two characters "-X"; GNU's switch validates
    // the letter before consuming the operand.
    const char opc = args_[pos_][1];
    if (!is_unary_char(opc)) {
      return fail("'" + std::string(args_[pos_]) +
                  "': unary operator expected");
    }
    ++pos_;  // unary_advance(): pos now at the operand
    if (pos_ >= args_.size()) return beyond();
    const std::string arg(args_[pos_]);
    ++pos_;

    switch (opc) {
      case 'n':
        return arg.empty() ? 1 : 0;
      case 'z':
        return arg.empty() ? 0 : 1;
      case 't': {
        // GNU: find_int() then strtol(); out-of-range fds are simply not
        // ttys (isatty fails), not errors.
        const auto parsed = parse_int_operand(arg);
        if (!parsed) return fail("invalid integer '" + arg + "'");
        errno = 0;
        char* end = nullptr;
        const long fd = std::strtol(
            (parsed->negative ? "-" + parsed->magnitude : parsed->magnitude)
                .c_str(),
            &end, 10);
        return errno != ERANGE && fd >= 0 && fd <= INT_MAX &&
                       _isatty(static_cast<int>(fd)) != 0
                   ? 0
                   : 1;
      }
      case 'h':
      case 'L':
        return is_link_operand(arg) ? 0 : 1;
      case 'u':  // setuid — no Windows equivalent
      case 'g':  // setgid
      case 'k':  // sticky bit
      case 'b':  // block device — none on Windows
      case 'S':  // socket filesystem node — none on Windows
        return 1;
      default: {
        const FileInfo st = stat_follow(arg);
        switch (opc) {
          case 'e':
            return st.exists ? 0 : 1;
          case 'r':
            return st.exists ? 0 : 1;
          case 'w':
            return st.exists && !st.is_readonly ? 0 : 1;
          case 'x':
            return st.exists && !st.is_char_device && !st.is_pipe &&
                           (st.is_directory || has_executable_extension(arg))
                       ? 0
                       : 1;
          case 'N':
            return st.exists && CompareFileTime(&st.mtime, &st.atime) > 0 ? 0
                                                                          : 1;
          case 'O':
            return st.exists && file_owner_matches(arg, TokenUser) ? 0 : 1;
          case 'G':
            return st.exists && file_owner_matches(arg, TokenGroups) ? 0 : 1;
          case 'f':
            return st.exists && st.is_regular() ? 0 : 1;
          case 'd':
            return st.exists && st.is_directory ? 0 : 1;
          case 's':
            // Directories have non-zero st_size on Linux.
            return st.exists && (st.is_directory || st.size > 0) ? 0 : 1;
          case 'c':
            return st.exists && st.is_char_device ? 0 : 1;
          case 'p':
            return st.exists && st.is_pipe ? 0 : 1;
        }
        return 1;
      }
    }
  }

  std::span<const std::string> args_;
  size_t pos_ = 0;
  std::string error_;
};

}  // namespace test_expr
