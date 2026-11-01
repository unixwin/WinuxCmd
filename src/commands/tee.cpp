// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for tee.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <fcntl.h>
#include <io.h>

#include <cerrno>
#include <csignal>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief TEE command options definition
 *
 * This array defines all the options supported by the tee command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -a, @a --append: Append to the given FILEs, do not overwrite
 * [IMPLEMENTED]
 * - @a -i, @a --ignore-interrupts: Ignore interrupt signals [IMPLEMENTED]
 * - @a -p: Diagnose write errors using warn-nopipe mode [IMPLEMENTED]
 */
auto constexpr TEE_OPTIONS = std::array{
    // [GNU] -a, --append
    OPTION("-a", "--append", "append to the given FILEs, do not overwrite"),
    // [GNU] -i, --ignore-interrupts
    OPTION("-i", "--ignore-interrupts", "ignore interrupt signals"),
    // [GNU] -p has no long form; the "--diagnose" alias was invented and
    // is removed (#1355).
    OPTION("-p", "", "write errors to standard error"),
    // [GNU] --output-error
    OPTION("", "--output-error",
           "set behavior on write error: 'warn', 'warn-nopipe', "
           "'exit', 'exit-nopipe'",
           OPTIONAL_STRING_TYPE)};

REGISTER_COMMAND(
    tee, "tee",
    "read from standard input and write to standard output and files",
    "Copy standard input to each FILE, and also to standard output.\n"
    "\n"
    "FILE operands are opened literally; '-' names a file called '-'.",
    "  echo 'Hello' | tee output.txt       Save output to file\n"
    "  echo 'World' | tee -a output.txt    Append to file\n"
    "  cat file.txt | tee backup.txt       Create backup while viewing",
    "cat(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", TEE_OPTIONS) {
  using namespace core::pipeline;

#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  // [GNU] A closed standard input (<&-) is a read error, not EOF (#973).
  // The CRT reports EBADF on the first read while std::cin would silently
  // yield EOF and the command would exit 0. _lseek(0, 0, SEEK_CUR) probes
  // fd validity without disturbing pipes (ESPIPE) or regular files (no-op).
  {
    errno = 0;
    if (_lseek(0, 0, SEEK_CUR) == -1 && errno == EBADF) {
      safeErrorPrintLn("tee: read error: Bad file descriptor");
      return 1;
    }
  }

  bool append = ctx.get<bool>("-a", false) || ctx.get<bool>("--append", false);
  bool ignore_interrupts =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--ignore-interrupts", false);
  bool diagnose = ctx.get<bool>("-p", false);

  // [GNU] tee.c:48-57,132-167: the output-error mode machine.
  // The default (neither -p nor --output-error) is the traditional
  // sigpipe mode; -p and a bare --output-error select warn-nopipe;
  // --output-error=MODE goes through XARGMATCH, so unambiguous
  // abbreviations are accepted.  -p and --output-error are the
  // same getopt option in GNU, so when both appear the last one
  // in argv order wins.
  enum class OutputErrorMode {
    kSigpipe,
    kWarn,
    kWarnNopipe,
    kExit,
    kExitNopipe
  };
  OutputErrorMode mode = OutputErrorMode::kSigpipe;
  {
    auto resolve_mode_value = [&](const std::string& value) -> bool {
      static constexpr std::array<std::pair<std::string_view,
                                            OutputErrorMode>,
                                  4>
          kModes{{{"warn", OutputErrorMode::kWarn},
                  {"warn-nopipe", OutputErrorMode::kWarnNopipe},
                  {"exit", OutputErrorMode::kExit},
                  {"exit-nopipe", OutputErrorMode::kExitNopipe}}};
      for (const auto& [name, m] : kModes) {
        if (value == name) {
          mode = m;
          return true;
        }
      }
      int matches = 0;
      for (const auto& [name, m] : kModes) {
        if (name.starts_with(value) && !value.empty()) {
          mode = m;
          ++matches;
        }
      }
      if (matches == 1) return true;
      std::string msg = "invalid argument '" + value +
                        "' for '--output-error'\n"
                        "Valid arguments are:";
      for (const auto& [name, m] : kModes) {
        msg += "\n  - '";
        msg += name;
        msg += '\'';
      }
      msg += "\nTry 'tee --help' for more information.";
      safeErrorPrintLn("tee: " + msg);
      return false;
    };

    bool p_is_last = false;
    if (ctx.has("--output-error") && diagnose && ctx.metas) {
      // -p and --output-error share one getopt slot in GNU
      // (tee.c:63), and each occurrence reassigns the mode
      // (tee.c:145-151), so the last of the pair in argv order
      // wins.  occurrences() is argv-ordered and occurrence.index
      // selects the meta, so track which of the pair came last.
      bool last_was_p = false;
      for (const auto& occurrence : ctx.options.occurrences()) {
        const auto& meta = (*ctx.metas)[occurrence.index];
        if (meta.short_name == "-p") {
          last_was_p = true;
        } else if (meta.long_name == "--output-error") {
          last_was_p = false;
        }
      }
      p_is_last = last_was_p;
    }

    if (p_is_last) {
      mode = OutputErrorMode::kWarnNopipe;
    } else if (ctx.has("--output-error")) {
      auto value = ctx.get<std::string>("--output-error", "");
      if (value.empty()) {
        mode = OutputErrorMode::kWarnNopipe;
      } else if (!resolve_mode_value(value)) {
        return 1;
      }
    } else if (diagnose) {
      mode = OutputErrorMode::kWarnNopipe;
    }
  }

  if (ignore_interrupts) {
    std::signal(SIGINT, SIG_IGN);
  }

  // Get output files from positional arguments
  SmallVector<std::string, 32> output_files;
  for (auto arg : ctx.positionals) {
    output_files.push_back(std::string(arg));
  }

  bool ok = true;

  // [GNU] tee.c fail_output / tee_files: a failed output is
  // diagnosed once as 'tee: <name>: <error>' and removed; the
  // remaining outputs keep receiving input.  Whether a failure is
  // reported, and whether it is immediately fatal, depends on the
  // mode (EPIPE is silent in the *-nopipe modes).
  auto fail_output = [&](const std::string& display_name, int err) -> bool {
    bool is_epipe = (err == EPIPE);
    bool reportable = !is_epipe || mode == OutputErrorMode::kWarn ||
                      mode == OutputErrorMode::kExit;
    if (reportable) {
      // [GNU] tee.c fail_output: the name goes through quotef, so a
      // plain name prints bare and a special one is shell-quoted.
      safeErrorPrintLn("tee: " +
                       gnu_quotearg::shell_escape_quote(display_name) +
                       ": " + std::string(strerror(err)));
      ok = false;
    } else if (mode == OutputErrorMode::kSigpipe) {
      // Traditional mode dies on SIGPIPE; there is no signal to
      // die by here, so the run simply fails (PORT).
      ok = false;
    }
    return reportable && (mode == OutputErrorMode::kExit ||
                          mode == OutputErrorMode::kExitNopipe);
  };

  struct OutputSink {
    std::string name;
    std::ofstream stream;
    bool alive = true;
  };
  SmallVector<OutputSink, 32> sinks;

  // Open output files
  for (const auto& filename : output_files) {
    // Resolve through the shared operand boundary so MSYS-style paths,
    // POSIX pseudo-devices (/dev/null -> NUL), and the /dev/std{out,err}
    // fd bindings work like other tools (#276/#1056). Error messages keep
    // echoing the user-visible operand verbatim. A trailing separator
    // requires a directory target (ENOTDIR on regular files); silently
    // stripping it would truncate the file (#1052).
    std::ofstream file = file_io::create_binary_file(filename, append);
    if (!file.is_open()) {
      // Capture errno before printing: output helpers can clobber it.
      const int saved_errno = errno;
      // [GNU] tee.c:261-283: the diagnostic is quotef(file) -- a
      // plain name prints bare, a name with shell-special characters
      // (e.g. a backslash) is quoted -- and in the exit modes the
      // open failure is fatal on the spot.
      safeErrorPrintLn("tee: " +
                       gnu_quotearg::shell_escape_quote(filename) + ": " +
                       std::string(strerror(saved_errno)));
      if (mode == OutputErrorMode::kExit ||
          mode == OutputErrorMode::kExitNopipe) {
        return 1;
      }
      ok = false;
      continue;
    }

    sinks.push_back(OutputSink{filename, std::move(file), true});
  }

  bool stdout_alive = true;
  size_t alive_count = sinks.size() + 1;

  // Read from stdin and write exact bytes to all live outputs.
  std::array<char, 8192> buffer{};
  while (alive_count > 0 && std::cin.good()) {
    std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto got = static_cast<size_t>(std::cin.gcount());
    if (got == 0) break;

    if (stdout_alive &&
        fwrite(buffer.data(), 1, got, stdout) != got) {
      int err = errno != 0 ? errno : EIO;
      stdout_alive = false;
      --alive_count;
      if (fail_output("standard output", err)) return 1;
    }

    for (auto& sink : sinks) {
      if (!sink.alive) continue;
      sink.stream.write(buffer.data(), static_cast<std::streamsize>(got));
      if (sink.stream.fail()) {
        int err = errno != 0 ? errno : EIO;
        sink.alive = false;
        --alive_count;
        if (fail_output(sink.name, err)) return 1;
      }
    }
  }

  if (std::cin.bad()) {
    // [GNU] tee.c:327: 'tee: read error: <errno text>'.
    if (errno != 0) {
      safeErrorPrintLn("tee: read error: " + std::string(strerror(errno)));
    } else {
      safeErrorPrintLn("tee: read error");
    }
    return 1;
  }

  // Close the files that are still outputs (removed outputs are
  // not closed again, as in tee_files).
  for (auto& sink : sinks) {
    if (!sink.alive) continue;
    sink.stream.close();
    if (sink.stream.fail()) {
      int err = errno != 0 ? errno : EIO;
      safeErrorPrintLn("tee: " + sink.name + ": " +
                       std::string(strerror(err)));
      ok = false;
    }
  }

  return ok ? 0 : 1;
}
