/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: dispatcher.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
export module core:dispatcher;

import std;
import :cmd_meta;
import :command_context;
import utils;

export template <size_t N>
using CommandFunc = int (*)(CommandContext<N> &) noexcept;

export template <size_t N>
struct CommandEntry {
  cmd::meta::CommandMetaHandle meta;
  CommandFunc<N> handler;
  std::string_view brief_desc;

  CommandEntry() = default;
  CommandEntry(cmd::meta::CommandMetaHandle m, CommandFunc<N> h)
      : meta(std::move(m)), handler(h), brief_desc(meta.brief_desc()) {}
};

// Per-option information returned for completion
export struct OptionInfo {
  std::string short_name;
  std::string long_name;
  std::string description;
};

struct CommandEntryErased {
  cmd::meta::CommandMetaHandle meta;
  std::function<int(std::span<std::string_view>)> handler;
  std::string_view brief_desc;

  CommandEntryErased() = default;

  CommandEntryErased(cmd::meta::CommandMetaHandle m,
                     std::function<int(std::span<std::string_view>)> h,
                     std::string_view brief)
      : meta(std::move(m)), handler(std::move(h)), brief_desc(brief) {}
};

auto is_legacy_tail_from_start_count(std::string_view arg) -> bool {
  if (arg.size() < 2 || arg[0] != '+') return false;
  return std::ranges::all_of(
      arg.substr(1), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

auto legacy_count_value(std::string_view arg) -> std::string {
  if (arg.empty()) return {};
  if ((arg[0] == '-' || arg[0] == '+') && arg.size() > 1) {
    return std::string(arg.substr(1));
  }
  return std::string(arg);
}

auto append_remaining_args(std::vector<std::string> &out,
                           std::span<std::string_view> args, size_t start)
    -> void {
  for (size_t i = start; i < args.size(); ++i) {
    out.emplace_back(args[i]);
  }
}

auto parse_decimal_prefix(std::string_view arg, size_t start) -> size_t {
  size_t pos = start;
  while (pos < arg.size() &&
         std::isdigit(static_cast<unsigned char>(arg[pos])) != 0) {
    ++pos;
  }
  return pos;
}

auto rewrite_head_obsolete_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (args.empty()) return std::nullopt;
  std::string_view first = args[0];
  if (first.size() < 2 || first[0] != '-' || first[1] == '-')
    return std::nullopt;

  size_t suffix_pos = parse_decimal_prefix(first, 1);
  if (suffix_pos == 1) return std::nullopt;

  std::string count(first.substr(1, suffix_pos - 1));
  std::string mode = "-n";
  std::string value = count;
  std::vector<std::string> flags;

  for (size_t i = suffix_pos; i < first.size(); ++i) {
    switch (first[i]) {
      case 'l':
        mode = "-n";
        value = count;
        break;
      case 'c':
        mode = "-c";
        value = count;
        break;
      case 'b':
        mode = "-c";
        value = count + "b";
        break;
      case 'k':
        mode = "-c";
        value = count + "K";
        break;
      case 'm':
        mode = "-c";
        value = count + "M";
        break;
      case 'q':
        flags.emplace_back("-q");
        break;
      case 'v':
        flags.emplace_back("-v");
        break;
      default:
        return std::nullopt;
    }
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + flags.size() + 1);
  rewritten.emplace_back(mode);
  rewritten.emplace_back(value);
  for (const auto &flag : flags) rewritten.emplace_back(flag);
  append_remaining_args(rewritten, args, 1);
  return rewritten;
}

auto rewrite_tail_obsolete_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (args.empty()) return std::nullopt;
  std::string_view first = args[0];

  if (is_legacy_tail_from_start_count(first)) {
    std::vector<std::string> rewritten;
    rewritten.reserve(args.size() + 1);
    rewritten.emplace_back("-n");
    rewritten.emplace_back(std::string(first));
    append_remaining_args(rewritten, args, 1);
    return rewritten;
  }

  if (first.size() < 2 || first[0] != '-' || first[1] == '-')
    return std::nullopt;

  size_t suffix_pos = parse_decimal_prefix(first, 1);
  if (suffix_pos == 1) return std::nullopt;

  bool has_compact_suffix = suffix_pos < first.size();
  if (has_compact_suffix && args.size() > 2) return std::nullopt;

  std::string count(first.substr(1, suffix_pos - 1));
  std::string mode = "-n";
  std::string value = count;
  bool follow = false;

  for (size_t i = suffix_pos; i < first.size(); ++i) {
    switch (first[i]) {
      case 'l':
        mode = "-n";
        value = count;
        break;
      case 'c':
        mode = "-c";
        value = count;
        break;
      case 'b':
        mode = "-c";
        value = count + "b";
        break;
      case 'f':
        follow = true;
        break;
      default:
        return std::nullopt;
    }
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 2);
  rewritten.emplace_back(mode);
  rewritten.emplace_back(value);
  if (follow) rewritten.emplace_back("-f");
  append_remaining_args(rewritten, args, 1);
  return rewritten;
}

auto rewrite_head_tail_count_args(std::string_view cmdName,
                                  std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (cmdName == "head") return rewrite_head_obsolete_args(args);
  if (cmdName == "tail") return rewrite_tail_obsolete_args(args);
  return std::nullopt;
}

auto is_chmod_negative_symbolic_mode_arg(std::string_view arg) -> bool {
  if (arg.size() < 2 || arg[0] != '-' || arg[1] == '-') {
    return false;
  }

  return std::ranges::all_of(arg.substr(1), [](unsigned char ch) {
    return ch == 'r' || ch == 'w' || ch == 'x';
  });
}

auto rewrite_chmod_gnu_negative_mode_args(std::string_view cmdName,
                                          std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (cmdName != "chmod" || args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> negative_modes;
  std::vector<std::string> pre_double_hyphen_args;
  std::vector<std::string> post_double_hyphen_args;
  bool seen_double_hyphen = false;

  for (auto arg : args) {
    if (!seen_double_hyphen && arg == "--") {
      seen_double_hyphen = true;
      continue;
    }

    if (!seen_double_hyphen && is_chmod_negative_symbolic_mode_arg(arg)) {
      negative_modes.push_back("a" + std::string(arg));
      continue;
    }

    if (seen_double_hyphen) {
      post_double_hyphen_args.emplace_back(arg);
    } else {
      pre_double_hyphen_args.emplace_back(arg);
    }
  }

  if (negative_modes.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 1);

  std::string merged_mode;
  for (size_t i = 0; i < negative_modes.size(); ++i) {
    if (i != 0) {
      merged_mode.push_back(',');
    }
    merged_mode += negative_modes[i];
  }

  rewritten.push_back(std::move(merged_mode));
  for (const auto& arg : pre_double_hyphen_args) {
    rewritten.push_back(arg);
  }
  if (seen_double_hyphen) {
    rewritten.push_back("--");
    for (const auto& arg : post_double_hyphen_args) {
      rewritten.push_back(arg);
    }
  }

  return rewritten;
}

auto parse_legacy_nice_adjustment(std::string_view arg) -> std::optional<int> {
  if (arg.size() < 2 || arg[0] != '-') {
    return std::nullopt;
  }

  if (arg == "-n" || arg == "--adjustment") {
    return std::nullopt;
  }

  std::string_view numeric = arg.substr(1);
  if (numeric.empty() || numeric == "-" || numeric == "+") {
    return std::nullopt;
  }

  if (numeric[0] == '+') {
    numeric.remove_prefix(1);
    if (numeric.empty()) {
      return std::nullopt;
    }
  }

  int value = 0;
  auto [ptr, ec] =
      std::from_chars(numeric.data(), numeric.data() + numeric.size(), value);
  if (ec != std::errc() || ptr != numeric.data() + numeric.size()) {
    return std::nullopt;
  }

  return value;
}

auto rewrite_nice_legacy_args(std::string_view cmdName,
                              std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (cmdName != "nice" || args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size());
  bool changed = false;
  bool seen_command = false;
  bool expecting_adjustment_value = false;

  for (auto arg : args) {
    if (seen_command) {
      rewritten.emplace_back(arg);
      continue;
    }

    if (arg == "--") {
      rewritten.emplace_back(arg);
      seen_command = true;
      expecting_adjustment_value = false;
      continue;
    }

    if (expecting_adjustment_value) {
      rewritten.emplace_back(arg);
      expecting_adjustment_value = false;
      continue;
    }

    if (arg == "-n" || arg == "--adjustment") {
      rewritten.emplace_back(arg);
      expecting_adjustment_value = true;
      continue;
    }

    if (arg.starts_with("-n") || arg.starts_with("--adjustment=")) {
      rewritten.emplace_back(arg);
      continue;
    }

    if (auto legacy_value = parse_legacy_nice_adjustment(arg)) {
      rewritten.emplace_back("-n" + std::to_string(*legacy_value));
      changed = true;
      continue;
    }

    if (!arg.empty() && arg[0] != '-') {
      rewritten.emplace_back("--");
      rewritten.emplace_back(arg);
      seen_command = true;
      changed = true;
      continue;
    }

    rewritten.emplace_back(arg);
  }

  if (!changed) {
    return std::nullopt;
  }

  return rewritten;
}

auto echo_posixly_correct_literal_mode(std::string_view cmdName,
                                       std::span<std::string_view> args)
    -> bool {
  if (cmdName != "echo") {
    return false;
  }

  const char* value = std::getenv("POSIXLY_CORRECT");
  if (value == nullptr || value[0] == '\0') {
    return false;
  }

  return args.empty() || args[0] != "-n";
}

auto command_supports_dispatcher_version(std::string_view cmdName) -> bool {
  return cmdName == "yes" || cmdName == "true" || cmdName == "false" ||
         cmdName == "link" || cmdName == "unlink" || cmdName == "arch" ||
         cmdName == "hostid" || cmdName == "logname" ||
         cmdName == "whoami" || cmdName == "factor" || cmdName == "tsort" ||
         cmdName == "users";
}

auto wants_standard_version(std::string_view cmdName,
                            std::span<std::string_view> args) -> bool {
  if (!command_supports_dispatcher_version(cmdName)) {
    return false;
  }

  for (const auto& arg : args) {
    if (arg == "--") {
      break;
    }
    if (arg == "--version" || arg == "-V") {
      return true;
    }
  }

  return false;
}

auto rewrite_echo_posix_args(std::string_view cmdName,
                             std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (!echo_posixly_correct_literal_mode(cmdName, args)) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 1);
  rewritten.emplace_back("--");
  append_remaining_args(rewritten, args, 0);
  return rewritten;
}

// Internal registry implementation class
class RegistryImpl {
  std::unordered_map<std::string_view, CommandEntryErased> registry_;

  static auto is_posixly_correct() -> bool {
    const char* value = std::getenv("POSIXLY_CORRECT");
    return value != nullptr && value[0] != '\0';
  }

  static auto parse_error_exit_code(std::string_view name) -> int {
    if (name == "env") return 125;
    if (name == "nice") return 125;
    if (name == "nohup") return is_posixly_correct() ? 127 : 125;
    if (name == "stdbuf") return 125;
    if (name == "timeout") return 125;
    if (name == "printenv") return 2;
    if (name == "tty") return 2;
    return 1;
  }

 public:
  // Register a command with compile-time metadata
  template <size_t N>
  void add(std::string_view name, const cmd::meta::CommandMeta<N> &meta,
           CommandFunc<N> handler) {
    // compile-time meta registry
    cmd::meta::Registry::register_command(name, meta);

    // type erase
    auto lambda = [meta, handler, name](std::span<std::string_view> args)
        -> int {
      bool ok = true;
      auto ctx = make_context<N>(args, meta.options(), ok);
      if (!ok) {
        if (!ctx.parse_error.empty()) {
          safeErrorPrintLn(std::string(name) + ": " + ctx.parse_error);
          safeErrorPrintLn("Try '" + std::string(name) +
                           " --help' for more information.");
        }
        return parse_error_exit_code(name);
      }
      return handler(ctx);
    };

    registry_.emplace(
        name, CommandEntryErased{cmd::meta::CommandMetaHandle(meta), lambda,
                                 meta.brief_desc()});
  }

  // Dispatch command execution
  int run(std::string_view cmdName, std::span<std::string_view> args) {
    auto it = registry_.find(cmdName);
    if (it == registry_.end()) {
      safePrintLn(L"winuxcmd: command not found: " +
                  std::wstring(cmdName.begin(), cmdName.end()));
      return 127;
    }

    std::vector<std::string> rewritten_storage;
    std::vector<std::string_view> rewritten_views;
    std::span<std::string_view> effective_args = args;

    if (auto rewritten = rewrite_head_tail_count_args(cmdName, args)) {
      rewritten_storage = std::move(*rewritten);
      rewritten_views.reserve(rewritten_storage.size());
      for (const auto &arg : rewritten_storage) {
        rewritten_views.emplace_back(arg);
      }
      effective_args = std::span<std::string_view>(rewritten_views);
    }

    if (auto rewritten =
            rewrite_chmod_gnu_negative_mode_args(cmdName, effective_args)) {
      rewritten_storage = std::move(*rewritten);
      rewritten_views.clear();
      rewritten_views.reserve(rewritten_storage.size());
      for (const auto &arg : rewritten_storage) {
        rewritten_views.emplace_back(arg);
      }
      effective_args = std::span<std::string_view>(rewritten_views);
    }

    if (auto rewritten = rewrite_nice_legacy_args(cmdName, effective_args)) {
      rewritten_storage = std::move(*rewritten);
      rewritten_views.clear();
      rewritten_views.reserve(rewritten_storage.size());
      for (const auto &arg : rewritten_storage) {
        rewritten_views.emplace_back(arg);
      }
      effective_args = std::span<std::string_view>(rewritten_views);
    }

    if (auto rewritten = rewrite_echo_posix_args(cmdName, effective_args)) {
      rewritten_storage = std::move(*rewritten);
      rewritten_views.clear();
      rewritten_views.reserve(rewritten_storage.size());
      for (const auto& arg : rewritten_storage) {
        rewritten_views.emplace_back(arg);
      }
      effective_args = std::span<std::string_view>(rewritten_views);
    }

    // Get meta data from the command
    const auto &meta = it->second.meta;
    auto options = meta.options();  // std::span<const OptionMeta>

    // Check if it contains help
    bool wants_help = false;
    if (!echo_posixly_correct_literal_mode(cmdName, args)) {
      for (const auto &arg : effective_args) {
        if (arg == "--help") {
          wants_help = true;
          break;
        }
      }

      if (!wants_help) {
        // Check if -h is already been registered
        bool has_h_option = false;
        for (const auto &opt : options) {
          if (opt.short_name == "-h") {
            has_h_option = true;
            break;
          }
        }
        if (!has_h_option) {
          for (const auto &arg : effective_args) {
            if (arg == "-h") {
              wants_help = true;
              break;
            }
          }
        }
      }
    }

    if (wants_help) {
      cmd::meta::Registry::print_help(cmdName);
      return 0;
    }

    if (wants_standard_version(cmdName, effective_args)) {
      safePrintLn(std::string(cmdName) + " (WinuxCmd) 0.1.0");
      return 0;
    }

    return it->second.handler(effective_args);
  }

  // Print command help
  void help(std::string_view cmdName) {
    cmd::meta::Registry::print_help(cmdName);
  }

  // Get man page for a command
  std::string man(std::string_view cmdName) {
    return cmd::meta::Registry::get_man(cmdName);
  }

  // Get all registered command names
  std::vector<std::pair<std::string_view, std::string_view>> list() {
    std::vector<std::pair<std::string_view, std::string_view>> commands;
    commands.reserve(registry_.size());
    for (const auto &[name, entry] : registry_) {
      commands.emplace_back(name, entry.brief_desc);
    }
    return commands;
  }

  // Get options for a specific command (for completion)
  std::vector<OptionInfo> command_options(std::string_view cmdName) {
    auto it = registry_.find(cmdName);
    if (it == registry_.end()) return {};
    auto opts = it->second.meta.options();
    std::vector<OptionInfo> result;
    result.reserve(opts.size());
    for (const auto &opt : opts) {
      result.push_back({std::string(opt.short_name), std::string(opt.long_name),
                        std::string(opt.description)});
    }
    return result;
  }
};

// Get singleton instance
inline RegistryImpl &getImpl() {
  static RegistryImpl instance;
  return instance;
}

// Static interface class
export class CommandRegistry {
 public:
  // Template method for registering commands with compile-time metadata
  template <size_t N>
  static void registerCommand(std::string_view name,
                              const cmd::meta::CommandMeta<N> &meta,
                              CommandFunc<N> handler) {
    getImpl().add<N>(name, meta, handler);
  }

  // Dispatch command execution (public interface)
  static int dispatch(std::string_view cmdName,
                      std::span<std::string_view> args) noexcept {
    return getImpl().run(cmdName, args);
  }

  // Print command help (public interface)
  static void printHelp(std::string_view cmdName) noexcept {
    getImpl().help(cmdName);
  }

  // Get all registered command names (public interface)
  static std::vector<std::pair<std::string_view, std::string_view>>
  getAllCommands() noexcept {
    return getImpl().list();
  }

  // Get options for a command (for completion)
  static std::vector<OptionInfo> getCommandOptions(
      std::string_view cmdName) noexcept {
    return getImpl().command_options(cmdName);
  }

  // Check whether a command is registered.
  static bool hasCommand(std::string_view cmdName) noexcept {
    auto all = getImpl().list();
    return std::ranges::any_of(
        all, [cmdName](const auto &item) { return item.first == cmdName; });
  }

  // Print man page for a command
  static std::string getManPage(std::string_view cmdName) noexcept {
    return getImpl().man(cmdName);
  }
};
