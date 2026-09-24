// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for getopt.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr GETOPT_OPTIONS = std::array{
    // [GNU] -o, --options: short option specification
    OPTION("-o", "--options", "short option specification", STRING_TYPE),
    // [GNU] -l, --longoptions: long options to be recognized
    OPTION("-l", "--longoptions", "long options to be recognized", STRING_TYPE),
    // [GNU] -n, --name: name used in diagnostics
    OPTION("-n", "--name", "name used in diagnostics", STRING_TYPE),
    // [GNU] -q, --quiet: disable error reporting by getopt
    OPTION("-q", "--quiet", "disable error reporting by getopt"),
    // [GNU] -Q, --quiet-output: no normal output
    OPTION("-Q", "--quiet-output", "no normal output"),
    // [GNU] -s, --shell: set quoting conventions to those of <shell>
    OPTION("-s", "--shell", "set quoting conventions to those of <shell>",
           STRING_TYPE),
    // [GNU] -T, --test: test for getopt(1) version
    OPTION("-T", "--test", "test for getopt(1) version"),
    // [GNU] -u, --unquoted: do not quote the output
    OPTION("-u", "--unquoted", "do not quote the output"),
    // [GNU] -a, --alternative: allow long options starting with single -
    OPTION("-a", "--alternative", "allow long options starting with single -")};

namespace getopt_pipeline {

enum class ArgKind { None, Required, Optional };

// [util-linux] shells recognized by -s/--shell.
enum class Shell { Bash, Tcsh };

struct Config {
  std::string name = "getopt";
  std::string optstring;
  std::vector<std::string> longoptions;  // -l/--longoptions occurrences
  bool quiet = false;
  bool quiet_output = false;
  bool unquoted = false;
  bool alternative = false;
  Shell shell = Shell::Bash;
  bool bad_shell = false;
  bool test = false;
  std::vector<std::string> args;
};

struct LongOption {
  std::string name;
  ArgKind kind = ArgKind::None;
};

// [util-linux] add_long_options(): each -l/--longoptions argument is a list
// separated by commas OR whitespace; each entry may carry a ':' (required
// arg) or '::' (optional arg) suffix.  The option may be given multiple
// times and the lists accumulate.  Returns nullopt for an empty entry,
// which GNU reports as a parse error (exit status 2).
auto parse_long_options(const std::vector<std::string>& specs)
    -> std::optional<std::vector<LongOption>> {
  std::vector<LongOption> result;
  auto is_sep = [](char c) {
    return c == ',' || c == ' ' || c == '\t' || c == '\n';
  };
  for (const auto& spec : specs) {
    size_t pos = 0;
    while (pos < spec.size()) {
      while (pos < spec.size() && is_sep(spec[pos])) ++pos;
      if (pos >= spec.size()) break;
      size_t end = pos;
      while (end < spec.size() && !is_sep(spec[end])) ++end;
      std::string entry = spec.substr(pos, end - pos);
      pos = end;
      LongOption opt;
      if (entry.ends_with("::")) {
        opt.kind = ArgKind::Optional;
        entry.resize(entry.size() - 2);
      } else if (entry.ends_with(":")) {
        opt.kind = ArgKind::Required;
        entry.pop_back();
      }
      if (entry.empty()) return std::nullopt;
      opt.name = std::move(entry);
      result.push_back(std::move(opt));
    }
  }
  return result;
}

// [util-linux] print_normalized(): each non-option token is emitted as
// " 'text'" with shell-specific escaping.  Bash only needs '\'' handling;
// tcsh additionally escapes '\\', '!' and whitespace.
auto shell_quote(std::string_view text, Shell shell) -> std::string {
  std::string out = "'";
  for (char ch : text) {
    if (shell == Shell::Tcsh) {
      switch (ch) {
        case '\\':
          out += "\\\\";
          continue;
        case '!':
          out += "'\\!'";
          continue;
        case '\n':
          out += "\\n";
          continue;
        default:
          break;
      }
      if (std::isspace(static_cast<unsigned char>(ch))) {
        out += "'\\";
        out.push_back(ch);
        out += "'";
        continue;
      }
    }
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('\'');
  return out;
}

auto quote_arg(std::string_view text, bool unquoted, Shell shell)
    -> std::string {
  if (unquoted) return std::string(text);
  return shell_quote(text, shell);
}

auto option_kind(std::string_view optstring, char option)
    -> std::optional<ArgKind> {
  for (size_t i = 0; i < optstring.size(); ++i) {
    if (optstring[i] != option) continue;
    if (i + 1 < optstring.size() && optstring[i + 1] == ':') {
      if (i + 2 < optstring.size() && optstring[i + 2] == ':') {
        return ArgKind::Optional;
      }
      return ArgKind::Required;
    }
    return ArgKind::None;
  }
  return std::nullopt;
}

// Resolve a long option name, allowing unambiguous abbreviations like
// getopt_long does.  Returns the unique match, or nullopt; on ambiguity the
// candidate names are collected into `ambiguous`.
auto find_long_option(const std::vector<LongOption>& longopts,
                      std::string_view name,
                      std::vector<std::string_view>* ambiguous = nullptr)
    -> const LongOption* {
  const LongOption* match = nullptr;
  bool exact = false;
  for (const auto& opt : longopts) {
    if (opt.name == name) return &opt;
    if (opt.name.size() > name.size() && opt.name.starts_with(name)) {
      if (ambiguous) ambiguous->push_back(opt.name);
      if (match) {
        exact = false;
        continue;  // keep collecting candidates
      }
      match = &opt;
      exact = true;
    }
  }
  return exact ? match : nullptr;
}

// [util-linux] shell_type(): -s/--shell accepts bash, sh, tcsh and csh.
auto parse_shell(std::string_view name) -> std::optional<Shell> {
  if (name == "bash" || name == "sh") return Shell::Bash;
  if (name == "tcsh" || name == "csh") return Shell::Tcsh;
  return std::nullopt;
}

// [util-linux] compatibility mode: selected when GETOPT_COMPATIBLE is set
// in the environment, or when the first parameter does not start with
// '-'.  In compatible mode argv[1] is the optstring (leading '-'/'+
// stripped), the rest of the arguments are the parameters, and output
// is not quoted.  WinuxCmd always provides the enhanced implementation.
auto is_compatible_mode(const CommandContext<GETOPT_OPTIONS.size()>& ctx)
    -> bool {
  if (std::getenv("GETOPT_COMPATIBLE") != nullptr) return true;
  return !ctx.raw_args.empty() &&
         (ctx.raw_args.front().empty() || ctx.raw_args.front().front() != '-');
}

auto build_config(const CommandContext<GETOPT_OPTIONS.size()>& ctx)
    -> std::optional<Config> {
  Config cfg;

  if (is_compatible_mode(ctx)) {
    // [util-linux] argv[1] is the optstring with a leading '-' or '+'
    // stripped; the remaining arguments are the parameters to normalize.
    cfg.unquoted = true;
    if (ctx.raw_args.empty()) return cfg;  // handled by caller
    std::string_view optstr = ctx.raw_args.front();
    while (!optstr.empty() && (optstr.front() == '-' || optstr.front() == '+'))
      optstr.remove_prefix(1);
    cfg.optstring = std::string(optstr);
    for (size_t i = 1; i < ctx.raw_args.size(); ++i) {
      cfg.args.emplace_back(ctx.raw_args[i]);
    }
    return cfg;
  }

  cfg.name = ctx.get<std::string>("-n", ctx.get<std::string>("--name", ""));
  if (cfg.name.empty()) cfg.name = "getopt";
  cfg.quiet = ctx.get<bool>("-q", false) || ctx.get<bool>("--quiet", false);
  cfg.quiet_output =
      ctx.get<bool>("-Q", false) || ctx.get<bool>("--quiet-output", false);
  cfg.unquoted =
      ctx.get<bool>("-u", false) || ctx.get<bool>("--unquoted", false);
  cfg.alternative =
      ctx.get<bool>("-a", false) || ctx.get<bool>("--alternative", false);
  auto shell_name =
      ctx.get<std::string>("-s", ctx.get<std::string>("--shell", ""));
  if (!shell_name.empty()) {
    auto shell = parse_shell(shell_name);
    if (!shell) {
      // [util-linux] parse_error(): "unknown shell after -s or --shell
      // argument", exit status 2.
      cfg.bad_shell = true;
    } else {
      cfg.shell = *shell;
    }
  }
  cfg.test = ctx.get<bool>("-T", false) || ctx.get<bool>("--test", false);
  if (ctx.has("-o") || ctx.has("--options")) {
    cfg.optstring =
        ctx.get<std::string>("-o", ctx.get<std::string>("--options", ""));
  }
  cfg.longoptions = ctx.get_all<std::string>("-l");

  size_t first_arg = 0;
  if (!ctx.has("-o") && !ctx.has("--options")) {
    // Without -o the first non-option operand is the optstring.
    if (ctx.positionals.empty()) return std::nullopt;
    cfg.optstring = std::string(ctx.positionals.front());
    first_arg = 1;
  }

  // The generic parser stops option processing at the optstring operand,
  // so a literal "--" right after it (getopt's own option terminator) is
  // delivered as a positional; skip it like getopt_long does.
  if (first_arg < ctx.positionals.size() &&
      ctx.positionals[first_arg] == "--") {
    ++first_arg;
  }
  for (size_t i = first_arg; i < ctx.positionals.size(); ++i) {
    cfg.args.emplace_back(ctx.positionals[i]);
  }
  return cfg;
}

// [util-linux] getopt(3) diagnostics during normalization.  They are
// suppressed by -q/--quiet and by a leading ':' in the optstring; either
// way the final exit status is 1.
struct ErrorSink {
  const Config& cfg;
  bool silent;
  auto report(std::string_view message) const -> void {
    if (!cfg.quiet && !silent) {
      safeErrorPrintLn(cfg.name + ": " + std::string(message));
    }
  }
};

auto run(const Config& cfg) -> int {
  auto longopts_or = parse_long_options(cfg.longoptions);
  if (!longopts_or) {
    // [util-linux] parse_error(): exit status 2.
    safeErrorPrintLn("getopt: empty long option after -l or --long argument");
    safeErrorPrintLn("Try 'getopt --help' for more information.");
    return 2;
  }
  const auto& longopts = *longopts_or;

  // [util-linux] optstring prefixes: '+' stops at the first non-option,
  // '-' returns non-options in place, ':' silences getopt(3) diagnostics.
  // POSIXLY_CORRECT prepends '+' unless the optstring already has a
  // prefix.
  std::string_view optstr = cfg.optstring;
  bool stop_at_first = false;
  bool return_in_order = false;
  bool silent = false;
  const bool posixly = std::getenv("POSIXLY_CORRECT") != nullptr;
  if (!optstr.empty() && optstr.front() == '+') {
    stop_at_first = true;
    optstr.remove_prefix(1);
  } else if (!optstr.empty() && optstr.front() == '-') {
    if (posixly) {
      stop_at_first = true;  // "+-..." : '-' becomes a normal option char
    } else {
      return_in_order = true;
      optstr.remove_prefix(1);
    }
  } else if (posixly) {
    stop_at_first = true;
  }
  if (!optstr.empty() && optstr.front() == ':') {
    silent = true;
    optstr.remove_prefix(1);
  }

  const ErrorSink errors{cfg, silent};

  // [util-linux] generate_output(): the parameters are scanned with
  // getopt_long semantics.  Bare option tokens are emitted as they are
  // found; option arguments and operands are emitted shell-quoted.
  // Without '+'/'-' prefix non-option operands are permuted to the tail,
  // after the final bare "--".
  int exit_code = 0;
  std::string out;
  std::vector<std::string> deferred;  // permuted operands
  auto append_option = [&](std::string_view token) {
    out.push_back(' ');
    out += token;
  };
  auto append_quoted = [&](std::string_view token) {
    out.push_back(' ');
    out += quote_arg(token, cfg.unquoted, cfg.shell);
  };
  auto append_operand = [&](std::string_view token) {
    if (return_in_order) {
      append_quoted(token);
    } else {
      deferred.emplace_back(token);
    }
  };

  bool scan_done = false;
  for (size_t i = 0; i < cfg.args.size(); ++i) {
    const std::string& arg = cfg.args[i];
    if (scan_done) {
      deferred.push_back(arg);
      continue;
    }
    if (arg == "--") {
      scan_done = true;
      continue;
    }
    if (arg.size() < 2 || arg[0] != '-') {
      // A non-option operand (a lone '-' included).
      append_operand(arg);
      if (stop_at_first) {
        scan_done = true;  // '+' prefix: the rest are all operands
      }
      continue;
    }

    // Long options: "--name[=value]", or "-name" under -a/--alternative
    // (getopt_long_only).
    const bool double_dash = arg.starts_with("--");
    if (double_dash || (cfg.alternative && arg.size() > 2)) {
      if (!longopts.empty() || double_dash) {
        std::string_view body =
            std::string_view(arg).substr(double_dash ? 2 : 1);
        std::string_view lname = body;
        std::string_view inline_value;
        bool has_inline_value = false;
        if (auto eq = body.find('='); eq != std::string_view::npos) {
          lname = body.substr(0, eq);
          inline_value = body.substr(eq + 1);
          has_inline_value = true;
        }
        std::vector<std::string_view> ambiguous;
        if (const LongOption* opt =
                find_long_option(longopts, lname, &ambiguous)) {
          std::string value;
          bool have_value = false;
          if (opt->kind != ArgKind::None) {
            have_value = true;
            if (has_inline_value) {
              value = std::string(inline_value);
            } else if (opt->kind == ArgKind::Required) {
              if (i + 1 >= cfg.args.size()) {
                errors.report("option '--" + opt->name +
                              "' requires an argument");
                exit_code = 1;
                have_value = false;
              } else {
                value = cfg.args[++i];
              }
            }
          }
          if (have_value || opt->kind == ArgKind::None) {
            append_option("--" + opt->name);
            if (opt->kind != ArgKind::None) {
              // [util-linux] an optional long argument without a value
              // still emits " ''".
              append_quoted(value);
            }
          }
          continue;
        }
        if (ambiguous.size() > 1) {
          std::string msg = "option '--" + std::string(lname) +
                            "' is ambiguous; possibilities:";
          for (auto name : ambiguous) {
            msg += " '--" + std::string(name) + "'";
          }
          errors.report(msg);
          exit_code = 1;
          continue;
        }
        if (double_dash) {
          errors.report("unrecognized option '--" + std::string(lname) + "'");
          exit_code = 1;
          continue;
        }
        // -a: fall through to short-option processing.
      }
    }

    for (size_t pos = 1; pos < arg.size(); ++pos) {
      char option = arg[pos];
      auto kind = option_kind(optstr, option);
      if (!kind) {
        errors.report(std::string("invalid option -- '") + option + "'");
        exit_code = 1;
        continue;  // getopt(3) keeps scanning the cluster
      }

      if (*kind == ArgKind::None) {
        append_option(std::string("-") + option);
        continue;
      }

      std::string value;
      if (pos + 1 < arg.size()) {
        // Attached argument: -bfoo, also for optional arguments.
        value = std::string(arg.substr(pos + 1));
        pos = arg.size();
      } else if (*kind == ArgKind::Required) {
        if (i + 1 >= cfg.args.size()) {
          errors.report(std::string("option requires an argument -- '") +
                        option + "'");
          exit_code = 1;
          break;  // option token is not emitted
        }
        value = cfg.args[++i];
      }
      append_option(std::string("-") + option);
      append_quoted(value);
      break;
    }
  }

  if (!cfg.quiet_output) {
    append_option("--");
    for (const auto& operand : deferred) append_quoted(operand);
    safePrintLn(out);
  }
  return exit_code;
}

}  // namespace getopt_pipeline

REGISTER_COMMAND(getopt, "getopt", "getopt <optstring> <parameters>",
                 "Parse command options.\n"
                 "The enhanced getopt(1) normalizes its parameter list and "
                 "prints a shell-compatible argument list.\n"
                 "Supports short options, long options, required ':' "
                 "arguments, and optional '::' arguments.",
                 "  getopt ab:c -- -a -b value file\n"
                 "  getopt -o ab:c -l alpha -- -a -b value --alpha file",
                 "env(1), xargs(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 GETOPT_OPTIONS) {
  // [util-linux] -T tests whether this is the enhanced getopt: exit 4.
  // The test result is returned before the optstring is required.
  if (!getopt_pipeline::is_compatible_mode(ctx) &&
      (ctx.has("-T") || ctx.has("--test"))) {
    return 4;
  }
  auto cfg = getopt_pipeline::build_config(ctx);
  if (!cfg) {
    safeErrorPrintLn("getopt: missing optstring argument");
    safeErrorPrintLn("Try 'getopt --help' for more information.");
    return 2;
  }
  if (cfg->test) return 4;
  if (cfg->bad_shell) {
    safeErrorPrintLn("getopt: unknown shell after -s or --shell argument");
    safeErrorPrintLn("Try 'getopt --help' for more information.");
    return 2;
  }
  // [util-linux] GETOPT_COMPATIBLE with no arguments prints " --".
  if (getopt_pipeline::is_compatible_mode(ctx) && ctx.raw_args.empty()) {
    safePrintLn(" --");
    return 0;
  }
  return getopt_pipeline::run(*cfg);
}
