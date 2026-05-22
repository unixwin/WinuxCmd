/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: find.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 WinuxCmd
/// @Description: Implementation for find command.
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

/**
 * @brief FIND command options definition
 *
 * This array defines all the options supported by the find command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -name: Base of file name (the path with the leading directories
 * removed)
 * matches shell pattern PATTERN [IMPLEMENTED]
 * - @a -iname: Like
 * -name, but the match is case insensitive [IMPLEMENTED]
 * - @a -path: File
 * name matches shell pattern PATTERN [IMPLEMENTED]
 * - @a -ipath: Like -path,
 * but the match is case insensitive [IMPLEMENTED]
 * - @a -type: File is of
 *
 * type c: b,d,p,f,l,s,D [only d,f,l are supported]
 * [IMPLEMENTED]
 * - @a
 * -size: File uses n units of space [IMPLEMENTED]
 * - @a -empty: File is empty
 * and is either a regular file or a directory
 * [IMPLEMENTED]
 * - @a -mtime:
 * File data was last modified n*24 hours ago [IMPLEMENTED]
 * - @a -mmin: File
 * data was last modified n minutes ago [IMPLEMENTED]
 * - @a
 * -mindepth:
 * Descend at least LEVELS levels of directories before tests
 * [IMPLEMENTED]

 * * - @a -maxdepth: Descend at most LEVELS levels of directories below
 * starting-points [IMPLEMENTED]
 * - @a -print: Print the full file name on the standard output [IMPLEMENTED]
 * - @a -print0: Print the full file name on the standard output, followed by a
 * null character [IMPLEMENTED]
 * - @a -L: Follow symbolic links [IMPLEMENTED]
 * - @a -H: Do not follow
 * symbolic links, except while processing command line arguments [NOT SUPPORT]
 * - @a -P: Never follow symbolic links (default) [IMPLEMENTED]
 * - @a -delete: Delete files [IMPLEMENTED]
 * - @a -exec: Execute command
 * [IMPLEMENTED: ; and {} + forms]
 * - @a -ok: Execute command after
 * confirmation [IMPLEMENTED: ; form]
 * -
 * @a -printf: Print format [PARTIAL: %p,%f,%h,%y,%s,%m,%T@,%%]
 * - @a -prune:

 * * Prune tree [IMPLEMENTED]
 * - @a -quit: Exit immediately [IMPLEMENTED]
 * -
 * @a -true: Always true [IMPLEMENTED]
 * - @a -false: Always false
 * [IMPLEMENTED]
 * - @a -regex: Whole path matches regular expression
 * [IMPLEMENTED]
 * - @a -iregex: Like -regex, but case insensitive
 * [IMPLEMENTED]
 * - @a -newer: File was modified more recently than reference
 * [IMPLEMENTED]
 * - @a -depth: Process directory contents before the directory
 * [IMPLEMENTED]
 */
auto constexpr FIND_OPTIONS = std::array{
    OPTION("-name", "",
           "base of file name (the path with the leading directories removed) "
           "matches shell pattern PATTERN",
           STRING_TYPE),
    OPTION("-iname", "", "like -name, but the match is case insensitive",
           STRING_TYPE),
    OPTION("-path", "", "file name matches shell pattern PATTERN", STRING_TYPE),
    OPTION("-ipath", "", "like -path, but the match is case insensitive",
           STRING_TYPE),
    OPTION("-type", "",
           "file is of type c: b,d,p,f,l,s,D [only d,f,l are supported]",
           STRING_TYPE),
    OPTION("-size", "", "file uses n units of space", STRING_TYPE),
    OPTION("-empty", "",
           "file is empty and is either a regular file or a directory"),
    OPTION("-mtime", "", "file data was last modified n*24 hours ago",
           STRING_TYPE),
    OPTION("-mmin", "", "file data was last modified n minutes ago",
           STRING_TYPE),
    OPTION("-mindepth", "",
           "descend at least LEVELS levels of directories before tests",
           INT_TYPE),
    OPTION("-maxdepth", "",
           "descend at most LEVELS levels of directories below starting-points",
           INT_TYPE),
    OPTION("-print", "", "print the full file name on the standard output"),
    OPTION("-print0", "",
           "print the full file name on the standard output, followed by a "
           "null character"),
    OPTION("-L", "", "follow symbolic links"),
    OPTION("-H", "",
           "do not follow symbolic links, except while processing command line "
           "arguments [NOT SUPPORT]"),
    OPTION("-P", "", "never follow symbolic links (default)"),
    OPTION("-delete", "", "delete files"),
    OPTION("-exec", "", "execute command", TERMINATED_STRING_TYPE),
    OPTION("-ok", "", "execute command after confirmation",
           TERMINATED_STRING_TYPE),
    OPTION("-printf", "", "print format [PARTIAL: %p,%f,%h,%y,%s,%m,%T@,%%]",
           STRING_TYPE),
    OPTION("-prune", "", "prune tree"),
    OPTION("-quit", "", "exit immediately"),
    OPTION("-true", "", "always true"),
    OPTION("-false", "", "always false"),
    OPTION("-regex", "", "whole path matches regular expression", STRING_TYPE),
    OPTION("-iregex", "",
           "whole path matches regular expression, case insensitive",
           STRING_TYPE),
    OPTION("-newer", "",
           "file was modified more recently than the reference file",
           STRING_TYPE),
    OPTION("-depth", "",
           "process each directory's contents before the directory itself"),
    OPTION("!", "", "negate expression"),
    OPTION("-not", "", "negate expression"),
    OPTION("-a", "", "and expression"),
    OPTION("-and", "", "and expression"),
    OPTION("-o", "", "or expression"),
    OPTION("-or", "", "or expression")};

namespace find_pipeline {
namespace cp = core::pipeline;

enum class NumericComparison { Exact, GreaterThan, LessThan };

struct NumericPredicate {
  NumericComparison comparison = NumericComparison::Exact;
  long long value = 0;
};

struct SizePredicate {
  NumericPredicate predicate;
  unsigned long long unit = 512;
};

enum class ExprKind {
  Always,
  Name,
  IName,
  Path,
  IPath,
  Regex,
  IRegex,
  Type,
  Empty,
  Size,
  MTime,
  MMin,
  Newer,
  Print,
  Print0,
  Printf,
  False,
  Exec,
  Delete,
  Prune,
  Quit,
  And,
  Or,
  Not
};

struct ExprNode {
  ExprKind kind = ExprKind::Always;
  std::string text;
  std::optional<std::regex> regex;
  std::optional<SizePredicate> size;
  std::optional<NumericPredicate> numeric;
  std::optional<std::filesystem::file_time_type> reference_time;
  size_t action_index = 0;
  std::unique_ptr<ExprNode> left;
  std::unique_ptr<ExprNode> right;
};

struct ExecAction {
  bool prompt = false;
  bool aggregate = false;
  std::string command;
  std::vector<std::string> args;
  std::vector<std::string> pending_paths;
};

struct Config {
  SmallVector<std::string, 64> roots;
  std::string name_pattern;
  std::string iname_pattern;
  std::string path_pattern;
  std::string ipath_pattern;
  std::string type_filter;
  std::optional<SizePredicate> size_filter;
  bool empty_filter = false;
  std::optional<NumericPredicate> mtime_filter;
  std::optional<NumericPredicate> mmin_filter;
  int mindepth = 0;
  int maxdepth = std::numeric_limits<int>::max();
  bool has_print = false;
  bool delete_action = false;
  bool depth_first = false;
  std::vector<ExecAction> exec_actions;
  bool follow_symlinks = false;
  bool prune_current = false;
  bool quit = false;
  std::unique_ptr<ExprNode> expression;

  bool unsupported_used = false;
  bool had_error = false;
};

// Wildcard matching is now provided by utils:wildcard module

auto parse_numeric_predicate(std::string_view text)
    -> cp::Result<NumericPredicate> {
  if (text.empty()) return std::unexpected("missing numeric argument");

  NumericPredicate result;
  size_t pos = 0;
  if (text[pos] == '+') {
    result.comparison = NumericComparison::GreaterThan;
    ++pos;
  } else if (text[pos] == '-') {
    result.comparison = NumericComparison::LessThan;
    ++pos;
  }

  if (pos >= text.size()) return std::unexpected("invalid numeric argument");

  long long value = 0;
  auto begin = text.data() + pos;
  auto end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc() || ptr != end) {
    return std::unexpected("invalid numeric argument");
  }
  if (value < 0) return std::unexpected("invalid numeric argument");

  result.value = value;
  return result;
}

auto parse_size_predicate(std::string_view text) -> cp::Result<SizePredicate> {
  if (text.empty()) return std::unexpected("missing -size argument");

  size_t suffix_pos = text.size();
  char suffix = '\0';
  unsigned char last = static_cast<unsigned char>(text.back());
  if (!std::isdigit(last)) {
    suffix = text.back();
    suffix_pos = text.size() - 1;
  }

  auto numeric = parse_numeric_predicate(text.substr(0, suffix_pos));
  if (!numeric) return std::unexpected(numeric.error());

  SizePredicate result;
  result.predicate = *numeric;
  switch (suffix) {
    case '\0':
    case 'b':
      result.unit = 512;
      break;
    case 'c':
      result.unit = 1;
      break;
    case 'w':
      result.unit = 2;
      break;
    case 'k':
      result.unit = 1024;
      break;
    case 'M':
      result.unit = 1024ULL * 1024ULL;
      break;
    case 'G':
      result.unit = 1024ULL * 1024ULL * 1024ULL;
      break;
    default:
      return std::unexpected("invalid -size unit");
  }

  return result;
}

auto parse_regex(std::string_view pattern, bool case_insensitive)
    -> cp::Result<std::regex> {
  auto flags = std::regex_constants::ECMAScript;
  if (case_insensitive) flags |= std::regex_constants::icase;

  try {
    return std::regex(std::string(pattern), flags);
  } catch (const std::regex_error&) {
    return std::unexpected("invalid regular expression");
  }
}

auto reference_write_time(std::string_view path)
    -> cp::Result<std::filesystem::file_time_type> {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(std::filesystem::path(path), ec);
  if (ec) return std::unexpected("cannot read reference file for -newer");
  return t;
}

auto numeric_matches(const NumericPredicate& pred, long long actual) -> bool {
  switch (pred.comparison) {
    case NumericComparison::Exact:
      return actual == pred.value;
    case NumericComparison::GreaterThan:
      return actual > pred.value;
    case NumericComparison::LessThan:
      return actual < pred.value;
  }
  return false;
}

auto type_matches(const std::filesystem::directory_entry& e,
                  std::string_view type) -> bool {
  if (type.empty()) return true;

  if (type == "f") return e.is_regular_file();
  if (type == "d") return e.is_directory();
  if (type == "l") return e.is_symlink();

  return false;
}

auto is_unsupported_used(const CommandContext<FIND_OPTIONS.size()>& ctx)
    -> std::optional<std::string> {
  if (ctx.get<bool>("-H", false)) return "-H is [NOT SUPPORT]";
  return std::nullopt;
}

auto is_path_option(std::string_view arg) -> bool {
  return arg == "-name" || arg == "-iname" || arg == "-path" ||
         arg == "-ipath" || arg == "-regex" || arg == "-iregex" ||
         arg == "-type" || arg == "-size" || arg == "-empty" ||
         arg == "-mtime" || arg == "-mmin" || arg == "-newer" ||
         arg == "-mindepth" || arg == "-maxdepth" || arg == "-print" ||
         arg == "-print0" || arg == "-delete" || arg == "-exec" ||
         arg == "-ok" || arg == "-printf" || arg == "-prune" ||
         arg == "-quit" || arg == "-true" || arg == "-false" ||
         arg == "-depth" || arg == "!" || arg == "-not" || arg == "-a" ||
         arg == "-and" || arg == "-o" || arg == "-or";
}

auto parse_roots(std::span<const std::string_view> args)
    -> SmallVector<std::string, 64> {
  SmallVector<std::string, 64> roots;
  for (auto arg : args) {
    if (arg == "-L" || arg == "-P" || arg == "-H") continue;
    if (is_path_option(arg) || arg == "!" || arg == "(" || arg == ")" ||
        arg == ",") {
      break;
    }
    roots.emplace_back(arg);
  }
  if (roots.empty()) roots.emplace_back(".");
  return roots;
}

auto parse_exec_actions(std::span<const std::string_view> args)
    -> cp::Result<std::vector<ExecAction>> {
  std::vector<ExecAction> actions;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] != "-exec" && args[i] != "-ok") continue;

    ExecAction action;
    action.prompt = args[i] == "-ok";

    std::vector<std::string> command_words;
    std::optional<std::string_view> terminator;
    size_t j = i + 1;
    for (; j < args.size(); ++j) {
      if (args[j] == ";" || args[j] == "+") {
        terminator = args[j];
        break;
      }
      command_words.emplace_back(args[j]);
    }

    if (!terminator) return std::unexpected("missing -exec/-ok terminator");
    if (command_words.empty()) {
      return std::unexpected("missing command after -exec/-ok");
    }

    action.aggregate = *terminator == "+";
    if (action.prompt && action.aggregate) {
      return std::unexpected("-ok does not support the {} + form");
    }
    if (action.aggregate) {
      if (command_words.back() != "{}") {
        return std::unexpected("-exec ... + requires {} before +");
      }
      command_words.pop_back();
      if (command_words.empty()) {
        return std::unexpected("missing command after -exec");
      }
    }

    action.command = command_words.front();
    action.args.assign(command_words.begin() + 1, command_words.end());
    actions.push_back(std::move(action));
    i = j;
  }

  return actions;
}

auto expression_start_index(std::span<const std::string_view> args) -> size_t {
  for (size_t i = 0; i < args.size(); ++i) {
    auto arg = args[i];
    if (arg == "-L" || arg == "-P" || arg == "-H") continue;
    if (is_path_option(arg) || arg == "(" || arg == ")" || arg == ",") {
      return i;
    }
  }
  return args.size();
}

auto make_expr(ExprKind kind) -> std::unique_ptr<ExprNode> {
  auto node = std::make_unique<ExprNode>();
  node->kind = kind;
  return node;
}

class ExpressionParser {
 public:
  explicit ExpressionParser(std::span<const std::string_view> tokens)
      : tokens_(tokens) {}

  auto parse() -> cp::Result<std::unique_ptr<ExprNode>> {
    if (at_end()) {
      return make_expr(ExprKind::Always);
    }

    auto expr = parse_or();
    if (!expr) return std::unexpected(expr.error());
    if (!at_end()) {
      return std::unexpected("invalid find expression");
    }
    return expr;
  }

 private:
  std::span<const std::string_view> tokens_;
  size_t pos_ = 0;
  size_t exec_index_ = 0;

  auto at_end() const -> bool { return pos_ >= tokens_.size(); }

  auto peek() const -> std::string_view {
    return at_end() ? std::string_view{} : tokens_[pos_];
  }

  auto consume() -> std::string_view { return tokens_[pos_++]; }

  auto match(std::string_view token) -> bool {
    if (peek() != token) return false;
    ++pos_;
    return true;
  }

  static auto is_or(std::string_view token) -> bool {
    return token == "-o" || token == "-or";
  }

  static auto is_and(std::string_view token) -> bool {
    return token == "-a" || token == "-and";
  }

  static auto starts_primary(std::string_view token) -> bool {
    return token == "!" || token == "-not" || token == "(" ||
           token == "-name" || token == "-iname" || token == "-path" ||
           token == "-ipath" || token == "-regex" || token == "-iregex" ||
           token == "-type" || token == "-size" || token == "-empty" ||
           token == "-mtime" || token == "-mmin" || token == "-newer" ||
           token == "-mindepth" || token == "-maxdepth" || token == "-print" ||
           token == "-print0" || token == "-printf" || token == "-prune" ||
           token == "-quit" || token == "-true" || token == "-false" ||
           token == "-depth" || token == "-delete" || token == "-exec" ||
           token == "-ok" || token == "-L" || token == "-P" || token == "-H";
  }

  auto require_value(std::string_view option) -> cp::Result<std::string> {
    if (at_end()) {
      return std::unexpected(std::string("missing argument for ") +
                             std::string(option));
    }
    return std::string(consume());
  }

  auto parse_or() -> cp::Result<std::unique_ptr<ExprNode>> {
    auto left = parse_and();
    if (!left) return std::unexpected(left.error());

    while (!at_end() && is_or(peek())) {
      consume();
      auto right = parse_and();
      if (!right) return std::unexpected(right.error());

      auto node = make_expr(ExprKind::Or);
      node->left = std::move(*left);
      node->right = std::move(*right);
      left = std::move(node);
    }
    return left;
  }

  auto parse_and() -> cp::Result<std::unique_ptr<ExprNode>> {
    auto left = parse_not();
    if (!left) return std::unexpected(left.error());

    while (!at_end() && peek() != ")" && !is_or(peek())) {
      if (is_and(peek())) {
        consume();
      } else if (!starts_primary(peek())) {
        return std::unexpected("invalid find expression");
      }

      auto right = parse_not();
      if (!right) return std::unexpected(right.error());

      auto node = make_expr(ExprKind::And);
      node->left = std::move(*left);
      node->right = std::move(*right);
      left = std::move(node);
    }
    return left;
  }

  auto parse_not() -> cp::Result<std::unique_ptr<ExprNode>> {
    if (match("!") || match("-not")) {
      auto operand = parse_not();
      if (!operand) return std::unexpected(operand.error());
      auto node = make_expr(ExprKind::Not);
      node->left = std::move(*operand);
      return node;
    }
    return parse_primary();
  }

  auto parse_primary() -> cp::Result<std::unique_ptr<ExprNode>> {
    if (at_end()) return std::unexpected("missing expression");

    if (match("(")) {
      auto expr = parse_or();
      if (!expr) return std::unexpected(expr.error());
      if (!match(")")) return std::unexpected("missing closing parenthesis");
      return expr;
    }
    if (peek() == ")") return std::unexpected("unexpected closing parenthesis");

    auto option = consume();
    if (option == "-name" || option == "-iname" || option == "-path" ||
        option == "-ipath" || option == "-type") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      ExprKind kind = ExprKind::Name;
      if (option == "-iname") kind = ExprKind::IName;
      if (option == "-path") kind = ExprKind::Path;
      if (option == "-ipath") kind = ExprKind::IPath;
      if (option == "-type") {
        if (*value != "f" && *value != "d" && *value != "l") {
          return std::unexpected("-type currently supports only f,d,l");
        }
        kind = ExprKind::Type;
      }
      auto node = make_expr(kind);
      node->text = std::move(*value);
      return node;
    }

    if (option == "-regex" || option == "-iregex") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_regex(*value, option == "-iregex");
      if (!parsed) return std::unexpected(parsed.error());
      auto node =
          make_expr(option == "-regex" ? ExprKind::Regex : ExprKind::IRegex);
      node->text = std::move(*value);
      node->regex = std::move(*parsed);
      return node;
    }

    if (option == "-empty") {
      return make_expr(ExprKind::Empty);
    }

    if (option == "-size") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_size_predicate(*value);
      if (!parsed) return std::unexpected(parsed.error());
      auto node = make_expr(ExprKind::Size);
      node->size = *parsed;
      return node;
    }

    if (option == "-mtime" || option == "-mmin") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = parse_numeric_predicate(*value);
      if (!parsed) return std::unexpected(parsed.error());
      auto node =
          make_expr(option == "-mtime" ? ExprKind::MTime : ExprKind::MMin);
      node->numeric = *parsed;
      return node;
    }

    if (option == "-newer") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto parsed = reference_write_time(*value);
      if (!parsed) return std::unexpected(parsed.error());
      auto node = make_expr(ExprKind::Newer);
      node->text = std::move(*value);
      node->reference_time = *parsed;
      return node;
    }

    if (option == "-mindepth" || option == "-maxdepth") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      return make_expr(ExprKind::Always);
    }

    if (option == "-true" || option == "-depth") {
      return make_expr(ExprKind::Always);
    }

    if (option == "-false") {
      return make_expr(ExprKind::False);
    }

    if (option == "-printf") {
      auto value = require_value(option);
      if (!value) return std::unexpected(value.error());
      auto node = make_expr(ExprKind::Printf);
      node->text = std::move(*value);
      return node;
    }

    if (option == "-exec" || option == "-ok") {
      bool found_terminator = false;
      while (!at_end()) {
        auto token = consume();
        if (token == ";" || token == "+") {
          found_terminator = true;
          break;
        }
      }
      if (!found_terminator) {
        return std::unexpected("missing -exec/-ok terminator");
      }
      auto node = make_expr(ExprKind::Exec);
      node->action_index = exec_index_++;
      return node;
    }

    if (option == "-print") {
      return make_expr(ExprKind::Print);
    }

    if (option == "-print0") {
      return make_expr(ExprKind::Print0);
    }

    if (option == "-quit") {
      return make_expr(ExprKind::Quit);
    }

    if (option == "-delete") {
      return make_expr(ExprKind::Delete);
    }

    if (option == "-prune") {
      return make_expr(ExprKind::Prune);
    }

    if (option == "-L" || option == "-P" || option == "-H") {
      return make_expr(ExprKind::Always);
    }

    if (is_and(option) || is_or(option)) {
      return std::unexpected("missing expression");
    }

    return std::unexpected("invalid find expression");
  }
};

auto parse_expression(std::span<const std::string_view> raw_args)
    -> cp::Result<std::unique_ptr<ExprNode>> {
  size_t start = expression_start_index(raw_args);
  ExpressionParser parser(raw_args.subspan(start));
  return parser.parse();
}

auto build_config(const CommandContext<FIND_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (auto u = is_unsupported_used(ctx); u.has_value()) {
    return std::unexpected(*u);
  }

  cfg.name_pattern = ctx.get<std::string>("-name", "");
  cfg.iname_pattern = ctx.get<std::string>("-iname", "");
  cfg.path_pattern = ctx.get<std::string>("-path", "");
  cfg.ipath_pattern = ctx.get<std::string>("-ipath", "");
  cfg.type_filter = ctx.get<std::string>("-type", "");
  cfg.empty_filter = ctx.get<bool>("-empty", false);
  cfg.mindepth = ctx.get<int>("-mindepth", 0);
  cfg.maxdepth = ctx.get<int>("-maxdepth", std::numeric_limits<int>::max());

  cfg.delete_action = ctx.get<bool>("-delete", false);
  cfg.depth_first = ctx.get<bool>("-depth", false);
  auto exec_actions = parse_exec_actions(std::span<const std::string_view>(
      ctx.raw_args.data(), ctx.raw_args.size()));
  if (!exec_actions) return std::unexpected(exec_actions.error());
  cfg.exec_actions = std::move(*exec_actions);
  cfg.follow_symlinks = ctx.get<bool>("-L", false);

  if (!cfg.type_filter.empty() && cfg.type_filter != "f" &&
      cfg.type_filter != "d" && cfg.type_filter != "l") {
    return std::unexpected("-type currently supports only f,d,l");
  }

  auto size_text = ctx.get<std::string>("-size", "");
  if (!size_text.empty()) {
    auto parsed = parse_size_predicate(size_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.size_filter = *parsed;
  }

  auto mtime_text = ctx.get<std::string>("-mtime", "");
  if (!mtime_text.empty()) {
    auto parsed = parse_numeric_predicate(mtime_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.mtime_filter = *parsed;
  }

  auto mmin_text = ctx.get<std::string>("-mmin", "");
  if (!mmin_text.empty()) {
    auto parsed = parse_numeric_predicate(mmin_text);
    if (!parsed) return std::unexpected(parsed.error());
    cfg.mmin_filter = *parsed;
  }

  if (cfg.mindepth < 0 || cfg.maxdepth < 0 || cfg.mindepth > cfg.maxdepth) {
    return std::unexpected("invalid depth range");
  }

  cfg.roots = parse_roots(std::span<const std::string_view>(
      ctx.raw_args.data(), ctx.raw_args.size()));

  auto expression = parse_expression(std::span<const std::string_view>(
      ctx.raw_args.data(), ctx.raw_args.size()));
  if (!expression) return std::unexpected(expression.error());
  cfg.expression = std::move(*expression);

  bool has_print_action = ctx.get<bool>("-print", false) ||
                          ctx.get<bool>("-print0", false) || ctx.has("-printf");
  if (!has_print_action && !cfg.delete_action && cfg.exec_actions.empty()) {
    cfg.has_print = true;  // default action
  }

  return cfg;
}

auto relative_or_self(const std::filesystem::path& root,
                      const std::filesystem::path& p) -> std::filesystem::path {
  std::error_code ec;
  auto rel = std::filesystem::relative(p, root, ec);
  if (ec || rel.empty()) return std::filesystem::path(".");
  return rel;
}

auto depth_from_root(const std::filesystem::path& root,
                     const std::filesystem::path& p) -> int {
  auto rel = relative_or_self(root, p);
  if (rel == ".") return 0;
  int d = 0;
  for (const auto& part : rel) {
    (void)part;
    ++d;
  }
  return d;
}

auto path_display(const std::filesystem::path& p) -> std::string {
  auto s = p.generic_string();
  if (s.empty()) return ".";
  return s;
}

auto is_empty_entry(const std::filesystem::directory_entry& e) -> bool {
  std::error_code ec;
  if (e.is_regular_file(ec) && !ec) {
    return e.file_size(ec) == 0 && !ec;
  }
  if (e.is_directory(ec) && !ec) {
    std::filesystem::directory_iterator it(
        e.path(), std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) return false;
    return it == std::filesystem::directory_iterator{};
  }
  return false;
}

auto size_matches(const std::filesystem::directory_entry& e,
                  const SizePredicate& pred) -> bool {
  std::error_code ec;
  if (!e.is_regular_file(ec) || ec) return false;

  auto bytes = e.file_size(ec);
  if (ec) return false;

  unsigned long long units = 0;
  if (bytes != 0) {
    units = (bytes + pred.unit - 1) / pred.unit;
  }
  if (units >
      static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
    return pred.predicate.comparison == NumericComparison::GreaterThan;
  }

  return numeric_matches(pred.predicate, static_cast<long long>(units));
}

auto modification_age_units(const std::filesystem::directory_entry& e,
                            std::chrono::seconds unit)
    -> std::optional<long long> {
  std::error_code ec;
  auto write_time = e.last_write_time(ec);
  if (ec) return std::nullopt;

  auto now = std::filesystem::file_time_type::clock::now();
  auto elapsed = now - write_time;
  if (elapsed < std::filesystem::file_time_type::duration::zero()) {
    elapsed = std::filesystem::file_time_type::duration::zero();
  }

  auto elapsed_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(elapsed);
  return elapsed_seconds.count() / unit.count();
}

auto print_path(std::string_view path, bool null_terminated) -> void;
auto printf_path(std::string_view format, const std::filesystem::path& p,
                 const std::filesystem::directory_entry& e) -> void;
auto execute_action_for_path(ExecAction& action, std::string_view path,
                             Config& cfg) -> bool;
auto delete_path(const std::filesystem::path& p, Config& cfg) -> bool;

auto evaluate_expression(const ExprNode& expr, const std::filesystem::path& p,
                         const std::filesystem::directory_entry& e, Config& cfg)
    -> bool {
  switch (expr.kind) {
    case ExprKind::Always:
      return true;

    case ExprKind::Name: {
      auto filename = p.filename().string();
      if (filename.empty()) filename = p.generic_string();
      return wildcard_match(expr.text, filename, true);
    }

    case ExprKind::IName: {
      auto filename = p.filename().string();
      if (filename.empty()) filename = p.generic_string();
      return wildcard_match(expr.text, filename, false);
    }

    case ExprKind::Path: {
      auto full_path = p.generic_string();
      if (full_path.empty()) full_path = ".";
      return wildcard_match(expr.text, full_path, true);
    }

    case ExprKind::IPath: {
      auto full_path = p.generic_string();
      if (full_path.empty()) full_path = ".";
      return wildcard_match(expr.text, full_path, false);
    }

    case ExprKind::Regex:
    case ExprKind::IRegex: {
      if (!expr.regex) return false;
      auto full_path = p.generic_string();
      if (full_path.empty()) full_path = ".";
      return std::regex_match(full_path, *expr.regex);
    }

    case ExprKind::Type:
      return type_matches(e, expr.text);

    case ExprKind::Empty:
      return is_empty_entry(e);

    case ExprKind::Size:
      return expr.size && size_matches(e, *expr.size);

    case ExprKind::MTime: {
      if (!expr.numeric) return false;
      auto age = modification_age_units(e, std::chrono::hours(24));
      return age && numeric_matches(*expr.numeric, *age);
    }

    case ExprKind::MMin: {
      if (!expr.numeric) return false;
      auto age = modification_age_units(e, std::chrono::minutes(1));
      return age && numeric_matches(*expr.numeric, *age);
    }

    case ExprKind::Newer: {
      if (!expr.reference_time) return false;
      std::error_code ec;
      auto write_time = e.last_write_time(ec);
      return !ec && write_time > *expr.reference_time;
    }

    case ExprKind::Print:
      print_path(path_display(p), false);
      return true;

    case ExprKind::Print0:
      print_path(path_display(p), true);
      return true;

    case ExprKind::Printf:
      printf_path(expr.text, p, e);
      return true;

    case ExprKind::False:
      return false;

    case ExprKind::Exec:
      if (expr.action_index >= cfg.exec_actions.size()) return false;
      return execute_action_for_path(cfg.exec_actions[expr.action_index],
                                     path_display(p), cfg);

    case ExprKind::Delete:
      return delete_path(p, cfg);

    case ExprKind::Prune:
      cfg.prune_current = true;
      return true;

    case ExprKind::Quit:
      cfg.quit = true;
      return true;

    case ExprKind::And:
      return expr.left && expr.right &&
             evaluate_expression(*expr.left, p, e, cfg) &&
             evaluate_expression(*expr.right, p, e, cfg);

    case ExprKind::Or:
      return expr.left && expr.right &&
             (evaluate_expression(*expr.left, p, e, cfg) ||
              evaluate_expression(*expr.right, p, e, cfg));

    case ExprKind::Not:
      return expr.left && !evaluate_expression(*expr.left, p, e, cfg);
  }

  return false;
}

auto entry_matches(Config& cfg, const std::filesystem::path& p,
                   const std::filesystem::directory_entry& e, int depth)
    -> bool {
  cfg.prune_current = false;
  if (depth < cfg.mindepth || depth > cfg.maxdepth) return false;

  if (cfg.expression) {
    return evaluate_expression(*cfg.expression, p, e, cfg);
  }

  auto filename = p.filename().string();
  if (filename.empty()) filename = p.generic_string();

  if (!cfg.name_pattern.empty() &&
      !wildcard_match(cfg.name_pattern, filename, true)) {
    return false;
  }

  if (!cfg.iname_pattern.empty() &&
      !wildcard_match(cfg.iname_pattern, filename, false)) {
    return false;
  }

  auto full_path = p.generic_string();
  if (full_path.empty()) full_path = ".";

  if (!cfg.path_pattern.empty() &&
      !wildcard_match(cfg.path_pattern, full_path, true)) {
    return false;
  }

  if (!cfg.ipath_pattern.empty() &&
      !wildcard_match(cfg.ipath_pattern, full_path, false)) {
    return false;
  }

  if (!type_matches(e, cfg.type_filter)) return false;

  if (cfg.empty_filter && !is_empty_entry(e)) return false;

  if (cfg.size_filter && !size_matches(e, *cfg.size_filter)) return false;

  if (cfg.mtime_filter) {
    auto age = modification_age_units(e, std::chrono::hours(24));
    if (!age || !numeric_matches(*cfg.mtime_filter, *age)) return false;
  }

  if (cfg.mmin_filter) {
    auto age = modification_age_units(e, std::chrono::minutes(1));
    if (!age || !numeric_matches(*cfg.mmin_filter, *age)) return false;
  }

  return true;
}

auto print_path(std::string_view path, bool null_terminated) -> void {
  safePrint(path);
  if (null_terminated) {
    safePrint(char{'\0'});
  } else {
    safePrint("\n");
  }
}

auto file_type_char(const std::filesystem::directory_entry& e) -> char {
  std::error_code ec;
  if (e.is_regular_file(ec) && !ec) return 'f';
  if (e.is_directory(ec) && !ec) return 'd';
  if (e.is_symlink(ec) && !ec) return 'l';
  return '?';
}

auto file_size_bytes(const std::filesystem::directory_entry& e)
    -> unsigned long long {
  std::error_code ec;
  if (!e.is_regular_file(ec) || ec) return 0;
  auto size = e.file_size(ec);
  if (ec) return 0;
  return static_cast<unsigned long long>(size);
}

auto permission_bits(const std::filesystem::directory_entry& e) -> std::string {
  std::error_code ec;
  auto status = e.symlink_status(ec);
  if (ec) return "000";

  auto perms = status.permissions();
  auto has = [&](std::filesystem::perms bit) {
    return (perms & bit) != std::filesystem::perms::none;
  };

  unsigned mode = 0;
  if (has(std::filesystem::perms::set_uid)) mode |= 04000;
  if (has(std::filesystem::perms::set_gid)) mode |= 02000;
  if (has(std::filesystem::perms::sticky_bit)) mode |= 01000;
  if (has(std::filesystem::perms::owner_read)) mode |= 0400;
  if (has(std::filesystem::perms::owner_write)) mode |= 0200;
  if (has(std::filesystem::perms::owner_exec)) mode |= 0100;
  if (has(std::filesystem::perms::group_read)) mode |= 0040;
  if (has(std::filesystem::perms::group_write)) mode |= 0020;
  if (has(std::filesystem::perms::group_exec)) mode |= 0010;
  if (has(std::filesystem::perms::others_read)) mode |= 0004;
  if (has(std::filesystem::perms::others_write)) mode |= 0002;
  if (has(std::filesystem::perms::others_exec)) mode |= 0001;

  char buf[8];
  if (mode >= 01000) {
    snprintf(buf, sizeof(buf), "%04o", mode);
  } else {
    snprintf(buf, sizeof(buf), "%03o", mode);
  }
  return std::string(buf);
}

auto modification_time_seconds(const std::filesystem::directory_entry& e)
    -> std::string {
  std::error_code ec;
  auto write_time = e.last_write_time(ec);
  if (ec) return "0.000000000";

  auto system_time = std::chrono::time_point_cast<std::chrono::nanoseconds>(
      write_time - std::filesystem::file_time_type::clock::now() +
      std::chrono::system_clock::now());
  auto epoch = system_time.time_since_epoch();
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
  auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(epoch - seconds)
          .count();
  if (nanos < 0) {
    --seconds;
    nanos += 1000000000LL;
  }

  std::ostringstream out;
  out << seconds.count() << "." << std::setw(9) << std::setfill('0') << nanos;
  return out.str();
}

auto basename_display(const std::filesystem::path& p) -> std::string {
  auto filename = p.filename().generic_string();
  if (!filename.empty()) return filename;
  return path_display(p);
}

auto dirname_display(const std::filesystem::path& p) -> std::string {
  auto parent = p.parent_path().generic_string();
  if (parent.empty()) return ".";
  return parent;
}

auto format_printf(std::string_view format, const std::filesystem::path& p,
                   const std::filesystem::directory_entry& e) -> std::string {
  std::string out;
  out.reserve(format.size() + p.generic_string().size());

  for (size_t i = 0; i < format.size(); ++i) {
    char ch = format[i];
    if (ch == '\\' && i + 1 < format.size()) {
      char escaped = format[++i];
      switch (escaped) {
        case 'n':
          out.push_back('\n');
          break;
        case 't':
          out.push_back('\t');
          break;
        case '0':
          out.push_back('\0');
          break;
        case '\\':
          out.push_back('\\');
          break;
        default:
          out.push_back('\\');
          out.push_back(escaped);
          break;
      }
      continue;
    }

    if (ch != '%' || i + 1 >= format.size()) {
      out.push_back(ch);
      continue;
    }

    char code = format[++i];
    switch (code) {
      case '%':
        out.push_back('%');
        break;
      case 'p':
        out += path_display(p);
        break;
      case 'f':
        out += basename_display(p);
        break;
      case 'h':
        out += dirname_display(p);
        break;
      case 'y':
        out.push_back(file_type_char(e));
        break;
      case 's':
        out += std::to_string(file_size_bytes(e));
        break;
      case 'm':
        out += permission_bits(e);
        break;
      case 'T':
        if (i + 1 < format.size() && format[i + 1] == '@') {
          ++i;
          out += modification_time_seconds(e);
        } else {
          out += "%T";
        }
        break;
      default:
        out.push_back('%');
        out.push_back(code);
        break;
    }
  }

  return out;
}

auto printf_path(std::string_view format, const std::filesystem::path& p,
                 const std::filesystem::directory_entry& e) -> void {
  safePrint(format_printf(format, p, e));
}

auto delete_path(const std::filesystem::path& p, Config& cfg) -> bool {
  std::error_code ec;
  bool removed = std::filesystem::remove(p, ec);
  if (!removed || ec) {
    safeErrorPrint("find: cannot delete '");
    safeErrorPrint(path_display(p));
    safeErrorPrint("'");
    if (ec) {
      safeErrorPrint(": ");
      safeErrorPrint(ec.message());
    }
    safeErrorPrint("\n");
    cfg.had_error = true;
    return false;
  }
  return true;
}

auto quote_arg(const std::wstring& arg) -> std::wstring {
  if (arg.empty()) return L"\"\"";

  bool need_quote = arg.find_first_of(L" \t\"") != std::wstring::npos;
  if (!need_quote) return arg;

  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      backslashes = 0;
      out.push_back(c);
    }
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

auto build_command_line(const std::string& command,
                        const std::vector<std::string>& args) -> std::wstring {
  std::wstring cmd_line = quote_arg(utf8_to_wstring(command));
  for (const auto& arg : args) {
    cmd_line.push_back(L' ');
    cmd_line += quote_arg(utf8_to_wstring(arg));
  }
  return cmd_line;
}

auto replace_placeholder(std::string_view text, std::string_view path)
    -> std::string {
  std::string out(text);
  size_t pos = 0;
  while ((pos = out.find("{}", pos)) != std::string::npos) {
    out.replace(pos, 2, path);
    pos += path.size();
  }
  return out;
}

auto run_child(const std::string& command, const std::vector<std::string>& args)
    -> int {
  auto cmd_line = build_command_line(command, args);
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};

  BOOL ok = CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, TRUE, 0,
                           nullptr, nullptr, &si, &pi);
  if (!ok) return 127;

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(exit_code);
}

auto ask_confirmation(const ExecAction& action, std::string_view path) -> bool {
  safeErrorPrint("< ");
  safeErrorPrint(action.command);
  for (const auto& arg : action.args) {
    safeErrorPrint(" ");
    safeErrorPrint(replace_placeholder(arg, path));
  }
  safeErrorPrint(" ... ");
  safeErrorPrint(path);
  safeErrorPrint(" > ? ");

  std::string answer;
  if (!std::getline(std::cin, answer)) return false;
  return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

auto execute_action_for_path(ExecAction& action, std::string_view path,
                             Config& cfg) -> bool {
  if (action.aggregate) {
    action.pending_paths.emplace_back(path);
    return true;
  }

  if (action.prompt && !ask_confirmation(action, path)) return false;

  std::vector<std::string> args;
  args.reserve(action.args.size());
  for (const auto& arg : action.args) {
    args.push_back(replace_placeholder(arg, path));
  }

  int status = run_child(action.command, args);
  if (status != 0) cfg.had_error = true;
  return status == 0;
}

auto flush_exec_actions(Config& cfg) -> void {
  for (auto& action : cfg.exec_actions) {
    if (!action.aggregate || action.pending_paths.empty()) continue;

    std::vector<std::string> args = action.args;
    args.insert(args.end(), action.pending_paths.begin(),
                action.pending_paths.end());
    int status = run_child(action.command, args);
    if (status != 0) cfg.had_error = true;
    action.pending_paths.clear();
  }
}

auto apply_actions(const std::filesystem::path& p,
                   const std::filesystem::directory_entry&, Config& cfg)
    -> void {
  if (cfg.has_print) {
    print_path(path_display(p), false);
  }
}

auto should_descend_into(const std::filesystem::directory_entry& e,
                         const Config& cfg) -> bool {
  std::error_code ec;
  if (!e.is_directory(ec) || ec) return false;
  if (!cfg.follow_symlinks && e.is_symlink(ec) && !ec) return false;
  return true;
}

auto scan_depth_first(const std::filesystem::path& p, int depth, Config& cfg,
                      bool& matched_any) -> void {
  std::error_code ec;
  std::filesystem::directory_entry entry(p, ec);
  if (ec) {
    safeErrorPrint("find: '");
    safeErrorPrint(path_display(p));
    safeErrorPrint("': ");
    safeErrorPrint(ec.message());
    safeErrorPrint("\n");
    cfg.had_error = true;
    return;
  }

  if (depth < cfg.maxdepth && should_descend_into(entry, cfg)) {
    auto options = std::filesystem::directory_options::skip_permission_denied;
    if (cfg.follow_symlinks) {
      options |= std::filesystem::directory_options::follow_directory_symlink;
    }

    std::filesystem::directory_iterator it(p, options, ec);
    std::filesystem::directory_iterator end;
    if (ec) {
      safeErrorPrint("find: '");
      safeErrorPrint(path_display(p));
      safeErrorPrint("': ");
      safeErrorPrint(ec.message());
      safeErrorPrint("\n");
      cfg.had_error = true;
    } else {
      for (; it != end && !(cfg.quit && matched_any); it.increment(ec)) {
        if (ec) {
          safeErrorPrint("find: '");
          safeErrorPrint(path_display(p));
          safeErrorPrint("': ");
          safeErrorPrint(ec.message());
          safeErrorPrint("\n");
          cfg.had_error = true;
          break;
        }
        scan_depth_first(it->path(), depth + 1, cfg, matched_any);
      }
    }
  }

  if (cfg.quit && matched_any) return;

  bool matched = entry_matches(cfg, p, entry, depth);
  if (cfg.quit && !matched) return;
  if (matched) {
    apply_actions(p, entry, cfg);
    matched_any = true;
  }
}

auto scan_delete_depth_first(const std::filesystem::path& p, int depth,
                             Config& cfg, bool& matched_any) -> void {
  std::error_code ec;
  std::filesystem::directory_entry entry(p, ec);
  if (ec) {
    safeErrorPrint("find: '");
    safeErrorPrint(path_display(p));
    safeErrorPrint("': ");
    safeErrorPrint(ec.message());
    safeErrorPrint("\n");
    cfg.had_error = true;
    return;
  }

  if (depth < cfg.maxdepth && should_descend_into(entry, cfg)) {
    auto options = std::filesystem::directory_options::skip_permission_denied;
    if (cfg.follow_symlinks) {
      options |= std::filesystem::directory_options::follow_directory_symlink;
    }

    std::filesystem::directory_iterator it(p, options, ec);
    std::filesystem::directory_iterator end;
    if (ec) {
      safeErrorPrint("find: '");
      safeErrorPrint(path_display(p));
      safeErrorPrint("': ");
      safeErrorPrint(ec.message());
      safeErrorPrint("\n");
      cfg.had_error = true;
    } else {
      for (; it != end && !(cfg.quit && matched_any); it.increment(ec)) {
        if (ec) {
          safeErrorPrint("find: '");
          safeErrorPrint(path_display(p));
          safeErrorPrint("': ");
          safeErrorPrint(ec.message());
          safeErrorPrint("\n");
          cfg.had_error = true;
          break;
        }
        scan_delete_depth_first(it->path(), depth + 1, cfg, matched_any);
      }
    }
  }

  if (cfg.quit && matched_any) return;

  bool matched = entry_matches(cfg, p, entry, depth);
  if (cfg.quit && !matched) return;
  if (matched) {
    apply_actions(p, entry, cfg);
    matched_any = true;
  }
}

auto scan_one_root(const std::filesystem::path& root, Config& cfg,
                   bool& matched_any) -> void {
  std::error_code ec;
  bool exists = std::filesystem::exists(root, ec);
  if (ec || !exists) {
    safeErrorPrint("find: '");
    safeErrorPrint(root.string());
    safeErrorPrint("': No such file or directory\n");
    cfg.had_error = true;
    return;
  }

  std::filesystem::directory_entry root_entry(root, ec);
  if (!ec) {
    int d = 0;
    bool matched = entry_matches(cfg, root, root_entry, d);
    if (cfg.quit && !matched) return;
    if (matched) {
      apply_actions(root, root_entry, cfg);
      matched_any = true;
      if (cfg.quit) return;
    }
    if (cfg.prune_current && should_descend_into(root_entry, cfg)) return;
  }

  if (!std::filesystem::is_directory(root, ec) || ec) return;

  auto options = std::filesystem::directory_options::skip_permission_denied;
  if (cfg.follow_symlinks) {
    options |= std::filesystem::directory_options::follow_directory_symlink;
  }

  std::filesystem::recursive_directory_iterator it(root, options, ec);
  std::filesystem::recursive_directory_iterator end;

  if (ec) {
    cfg.had_error = true;
    return;
  }

  for (; it != end; ++it) {
    std::error_code iec;
    const auto& de = *it;
    auto p = de.path();
    int d = depth_from_root(root, p);

    if (d > cfg.maxdepth) {
      it.disable_recursion_pending();
      continue;
    }

    bool matched = entry_matches(cfg, p, de, d);
    if (cfg.quit && !matched) return;
    if (matched) {
      apply_actions(p, de, cfg);
      matched_any = true;
      if (cfg.quit) return;
    }
    if (cfg.prune_current && should_descend_into(de, cfg)) {
      it.disable_recursion_pending();
    }
  }
}

auto process(Config& cfg) -> int {
  bool matched_any = false;
  for (const auto& r : cfg.roots) {
    auto root = std::filesystem::path(r);
    if (cfg.delete_action) {
      std::error_code ec;
      bool exists = std::filesystem::exists(root, ec);
      if (ec || !exists) {
        safeErrorPrint("find: '");
        safeErrorPrint(root.string());
        safeErrorPrint("': No such file or directory\n");
        cfg.had_error = true;
      } else {
        scan_delete_depth_first(root, 0, cfg, matched_any);
      }
    } else if (cfg.depth_first) {
      std::error_code ec;
      bool exists = std::filesystem::exists(root, ec);
      if (ec || !exists) {
        safeErrorPrint("find: '");
        safeErrorPrint(root.string());
        safeErrorPrint("': No such file or directory\n");
        cfg.had_error = true;
      } else {
        scan_depth_first(root, 0, cfg, matched_any);
      }
    } else {
      scan_one_root(root, cfg, matched_any);
    }
    if (cfg.quit && matched_any) break;
  }

  flush_exec_actions(cfg);

  if (cfg.had_error) return 1;
  return 0;
}

}  // namespace find_pipeline

REGISTER_COMMAND(find, "find", "find [path...] [expression]",
                 "Search for files in a directory hierarchy.\n"
                 "If no path is given, '.' is used.",
                 "  find . -name '*.cpp'\n"
                 "  find src -type f -maxdepth 2\n"
                 "  find . -iname 'readme*'",
                 "grep(1), ls(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FIND_OPTIONS) {
  using namespace find_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"find");
    return 1;
  }

  return process(*cfg);
}
