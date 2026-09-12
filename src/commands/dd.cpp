/*
 *  Copyright © 2026 WinuxCmd
 */
// clang-format off
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;
// clang-format on

auto constexpr DD_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("if", "", "read from FILE instead of stdin", STRING_TYPE),
    // [DIFFERS]
    OPTION("of", "", "write to FILE instead of stdout", STRING_TYPE),
    // [DIFFERS]
    OPTION("ibs", "", "read up to BYTES bytes at a time", STRING_TYPE),
    // [DIFFERS]
    OPTION("obs", "", "write BYTES bytes at a time", STRING_TYPE),
    // [DIFFERS]
    OPTION("bs", "", "read and write up to BYTES bytes at a time", STRING_TYPE),
    // [DIFFERS]
    OPTION("cbs", "", "convert BYTES bytes at a time", STRING_TYPE),
    // [DIFFERS]
    OPTION("count", "", "copy only N input blocks", STRING_TYPE),
    // [DIFFERS]
    OPTION("skip", "", "skip N ibs-sized input blocks", STRING_TYPE),
    // [DIFFERS]
    OPTION("seek", "", "skip N obs-sized output blocks", STRING_TYPE),
    // [DIFFERS]
    OPTION("conv", "", "convert as per comma-separated symbol list",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("status", "", "control diagnostic output", STRING_TYPE),
    // [GNU] iflag: read flags
    OPTION("iflag", "", "read as per comma-separated symbol list", STRING_TYPE),
    // [GNU] oflag: write flags
    OPTION("oflag", "", "write as per comma-separated symbol list",
           STRING_TYPE)};

namespace dd_pipeline {

struct Config {
  std::string input_file;
  std::string output_file;
  std::uintmax_t ibs = 512;
  std::uintmax_t obs = 512;
  std::uintmax_t cbs = 0;
  std::uintmax_t count = 0;
  std::uintmax_t skip = 0;
  std::uintmax_t seek = 0;
  bool count_set = false;
  bool notrunc = false;
  bool noerror = false;
  bool sync_blocks = false;
  bool status_none = false;
  bool status_noxfer = false;
};

struct CopyStats {
  std::uintmax_t in_records = 0;
  std::uintmax_t out_records = 0;
  std::uintmax_t bytes_copied = 0;
};

enum class ReadBlockAction { success, recovered, stop };

auto recover_read_block(
    std::function<bool(char*, std::size_t, std::size_t&)> read,
    std::function<bool(std::size_t)> seek, bool noerror, bool sync_blocks,
    std::size_t request, std::vector<char>& output_buffer,
    std::size_t& input_records, std::vector<char>& input_buffer,
    std::size_t& bytes_read) -> ReadBlockAction {
  bytes_read = 0;
  if (read(input_buffer.data(), request, bytes_read)) {
    return ReadBlockAction::success;
  }
  if (!noerror) return ReadBlockAction::stop;

  const bool can_continue = seek(request);
  if (sync_blocks) output_buffer.insert(output_buffer.end(), request, '\0');
  ++input_records;
  return can_continue ? ReadBlockAction::recovered : ReadBlockAction::stop;
}

auto parse_size_operand(std::string_view text)
    -> std::optional<std::uintmax_t> {
  if (text.empty()) return std::nullopt;

  size_t number_end = 0;
  while (number_end < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[number_end]))) {
    ++number_end;
  }
  if (number_end == 0) return std::nullopt;

  std::uintmax_t value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + number_end, value);
  if (ec != std::errc() || ptr != text.data() + number_end) {
    return std::nullopt;
  }

  auto suffix = text.substr(number_end);
  std::uintmax_t multiplier = 1;
  if (suffix.empty()) {
    multiplier = 1;
  } else if (suffix == "c") {
    multiplier = 1;
  } else if (suffix == "w") {
    multiplier = 2;
  } else if (suffix == "b") {
    multiplier = 512;
  } else if (suffix == "kB") {
    multiplier = 1000;
  } else if (suffix == "K" || suffix == "KiB") {
    multiplier = 1024;
  } else if (suffix == "MB") {
    multiplier = 1000ULL * 1000ULL;
  } else if (suffix == "M" || suffix == "MiB") {
    multiplier = 1024ULL * 1024ULL;
  } else if (suffix == "GB") {
    multiplier = 1000ULL * 1000ULL * 1000ULL;
  } else if (suffix == "G" || suffix == "GiB") {
    multiplier = 1024ULL * 1024ULL * 1024ULL;
  } else {
    return std::nullopt;
  }

  if (value > std::numeric_limits<std::uintmax_t>::max() / multiplier) {
    return std::nullopt;
  }
  return value * multiplier;
}

auto parse_required_size(std::string_view name, std::string_view text,
                         std::uintmax_t& target, bool allow_zero = false)
    -> bool {
  if (text.empty()) return true;
  auto parsed = parse_size_operand(text);
  if (!parsed || (!allow_zero && *parsed == 0)) {
    safeErrorPrintLn(winux::i18n::translate("command.dd.error.invalid_value",
                                            std::string("dd: invalid ") +
                                                std::string(name) + " value '" +
                                                std::string(text) + "'"));
    return false;
  }
  target = *parsed;
  return true;
}

auto set_operand(Config& cfg, std::string_view name, std::string_view value)
    -> bool {
  if (name == "if") {
    cfg.input_file = std::string(value);
  } else if (name == "of") {
    cfg.output_file = std::string(value);
  } else if (name == "ibs") {
    return parse_required_size("ibs", value, cfg.ibs);
  } else if (name == "obs") {
    return parse_required_size("obs", value, cfg.obs);
  } else if (name == "bs") {
    auto parsed = parse_size_operand(value);
    if (!parsed || *parsed == 0) {
      safeErrorPrintLn(winux::i18n::translate(
          "command.dd.error.invalid_bs",
          std::string("dd: invalid bs value '") + std::string(value) + "'"));
      return false;
    }
    cfg.ibs = *parsed;
    cfg.obs = *parsed;
  } else if (name == "cbs") {
    return parse_required_size("cbs", value, cfg.cbs);
  } else if (name == "count") {
    cfg.count_set = true;
    return parse_required_size("count", value, cfg.count, true);
  } else if (name == "skip") {
    return parse_required_size("skip", value, cfg.skip, true);
  } else if (name == "seek") {
    return parse_required_size("seek", value, cfg.seek, true);
  } else if (name == "conv") {
    std::stringstream ss{std::string(value)};
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token == "notrunc") {
        cfg.notrunc = true;
      } else if (token == "sync") {
        cfg.sync_blocks = true;
      } else if (token == "noerror") {
        cfg.noerror = true;
      } else if (token.empty()) {
        continue;
      } else {
        safeErrorPrint("dd: unsupported conv flag '");
        safeErrorPrint(token);
        safeErrorPrint("'\n");
        return false;
      }
    }
  } else if (name == "status") {
    if (value == "none") {
      cfg.status_none = true;
    } else if (value == "noxfer") {
      cfg.status_noxfer = true;
    } else if (!value.empty() && value != "progress") {
      safeErrorPrint("dd: invalid status value '");
      safeErrorPrint(std::string(value));
      safeErrorPrint("'\n");
      return false;
    }
  } else if (name == "iflag") {
    std::stringstream ss{std::string(value)};
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token == "fullblock" || token == "direct" || token == "directory" ||
          token == "dsync" || token == "sync" || token == "nonblock" ||
          token == "noatime" || token == "nocache" || token == "noctty" ||
          token == "nofollow" || token.empty()) {
        continue;
      }
      safeErrorPrint("dd: unsupported iflag flag '");
      safeErrorPrint(token);
      safeErrorPrint("' [DIFFERS: not supported on Windows]\n");
      return false;
    }
  } else if (name == "oflag") {
    std::stringstream ss{std::string(value)};
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token == "append" || token == "direct" || token == "directory" ||
          token == "dsync" || token == "sync" || token == "nonblock" ||
          token == "noatime" || token == "nocache" || token == "noctty" ||
          token == "nofollow" || token.empty()) {
        continue;
      }
      safeErrorPrint("dd: unsupported oflag flag '");
      safeErrorPrint(token);
      safeErrorPrint("' [DIFFERS: not supported on Windows]\n");
      return false;
    }
  } else {
    safeErrorPrint("dd: unrecognized operand '");
    safeErrorPrint(std::string(name));
    safeErrorPrint("'\n");
    return false;
  }
  return true;
}

auto parse_config(const CommandContext<DD_OPTIONS.size()>& ctx, Config& cfg)
    -> bool {
  cfg.input_file = ctx.get<std::string>("if", "");
  cfg.output_file = ctx.get<std::string>("of", "");

  if (!parse_required_size("ibs", ctx.get<std::string>("ibs", ""), cfg.ibs) ||
      !parse_required_size("obs", ctx.get<std::string>("obs", ""), cfg.obs)) {
    return false;
  }

  std::string bs = ctx.get<std::string>("bs", "");
  if (!bs.empty()) {
    auto parsed = parse_size_operand(bs);
    if (!parsed || *parsed == 0) {
      safeErrorPrint("dd: invalid bs value '");
      safeErrorPrint(bs);
      safeErrorPrint("'\n");
      return false;
    }
    cfg.ibs = *parsed;
    cfg.obs = *parsed;
  }

  if (!parse_required_size("cbs", ctx.get<std::string>("cbs", ""), cfg.cbs)) {
    return false;
  }

  if (!parse_required_size("count", ctx.get<std::string>("count", ""),
                           cfg.count, true) ||
      !parse_required_size("skip", ctx.get<std::string>("skip", ""), cfg.skip,
                           true) ||
      !parse_required_size("seek", ctx.get<std::string>("seek", ""), cfg.seek,
                           true)) {
    return false;
  }
  cfg.count_set = !ctx.get<std::string>("count", "").empty();

  auto conv = ctx.get<std::string>("conv", "");
  if (!conv.empty()) {
    std::stringstream ss(conv);
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token == "notrunc") {
        cfg.notrunc = true;
      } else if (token == "sync") {
        cfg.sync_blocks = true;
      } else if (token == "noerror") {
        cfg.noerror = true;
      } else if (token.empty()) {
        continue;
      } else {
        safeErrorPrint("dd: unsupported conv flag '");
        safeErrorPrint(token);
        safeErrorPrint("'\n");
        return false;
      }
    }
  }

  // [GNU] iflag: read flags (accepted for compatibility; most flags are no-ops
  // on Windows)
  auto iflag = ctx.get<std::string>("iflag", "");
  if (!iflag.empty()) {
    std::stringstream ss(iflag);
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token == "fullblock") {
        // fullblock: accumulate full blocks of input - honored conceptually
      } else if (token.empty()) {
        continue;
      } else if (token == "direct" || token == "directory" ||
                 token == "dsync" || token == "sync" || token == "nonblock" ||
                 token == "noatime" || token == "nocache" ||
                 token == "noctty" || token == "nofollow") {
        // These I/O flags have no direct Windows console equivalent; accepted
        // silently
      } else {
        safeErrorPrint("dd: unsupported iflag flag '");
        safeErrorPrint(token);
        safeErrorPrint("' [DIFFERS: not supported on Windows]\n");
        return false;
      }
    }
  }

  // [GNU] oflag: write flags (accepted for compatibility; most flags are no-ops
  // on Windows)
  auto oflag = ctx.get<std::string>("oflag", "");
  if (!oflag.empty()) {
    std::stringstream ss(oflag);
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token == "append") {
        // append mode - could be implemented with Windows FILE_APPEND_DATA
      } else if (token.empty()) {
        continue;
      } else if (token == "direct" || token == "directory" ||
                 token == "dsync" || token == "sync" || token == "nonblock" ||
                 token == "noatime" || token == "nocache" ||
                 token == "noctty" || token == "nofollow") {
        // These I/O flags have no direct Windows console equivalent; accepted
        // silently
      } else {
        safeErrorPrint("dd: unsupported oflag flag '");
        safeErrorPrint(token);
        safeErrorPrint("' [DIFFERS: not supported on Windows]\n");
        return false;
      }
    }
  }

  auto status = ctx.get<std::string>("status", "");
  if (status == "none") {
    cfg.status_none = true;
  } else if (status == "noxfer") {
    cfg.status_noxfer = true;
  } else if (!status.empty() && status != "progress") {
    safeErrorPrint("dd: invalid status value '");
    safeErrorPrint(status);
    safeErrorPrint("'\n");
    return false;
  }

  for (auto positional : ctx.positionals) {
    auto eq = positional.find('=');
    if (eq == std::string_view::npos || eq == 0) {
      safeErrorPrint("dd: invalid operand '");
      safeErrorPrint(std::string(positional));
      safeErrorPrint("'\n");
      return false;
    }

    auto name = positional.substr(0, eq);
    auto value = positional.substr(eq + 1);
    if (!set_operand(cfg, name, value)) return false;
  }

  return true;
}

auto seek_handle(HANDLE handle, std::uintmax_t offset) -> bool {
  LARGE_INTEGER li;
  li.QuadPart = static_cast<LONGLONG>(offset);
  return SetFilePointerEx(handle, li, nullptr, FILE_BEGIN) != 0;
}

auto checked_product(std::uintmax_t a, std::uintmax_t b)
    -> std::optional<std::uintmax_t> {
  if (b != 0 && a > std::numeric_limits<std::uintmax_t>::max() / b) {
    return std::nullopt;
  }
  return a * b;
}

auto discard_input(HANDLE input, std::uintmax_t bytes) -> bool {
  std::array<char, 8192> buffer{};
  std::uintmax_t remaining = bytes;
  while (remaining > 0) {
    DWORD request =
        static_cast<DWORD>(std::min<std::uintmax_t>(remaining, buffer.size()));
    DWORD got = 0;
    if (!ReadFile(input, buffer.data(), request, &got, nullptr)) return false;
    if (got == 0) return true;
    remaining -= got;
  }
  return true;
}

auto write_all(HANDLE output, const char* data, size_t size) -> bool {
  size_t written_total = 0;
  while (written_total < size) {
    DWORD request = static_cast<DWORD>(std::min<size_t>(
        size - written_total,
        static_cast<size_t>(std::numeric_limits<DWORD>::max())));
    DWORD written = 0;
    if (!WriteFile(output, data + written_total, request, &written, nullptr)) {
      return false;
    }
    if (written == 0) return false;
    written_total += written;
  }
  return true;
}

auto flush_output_buffer(HANDLE output, std::vector<char>& pending,
                         std::uintmax_t obs, CopyStats& stats, bool force)
    -> bool {
  while (pending.size() >= obs || (force && !pending.empty())) {
    size_t chunk_size =
        pending.size() >= obs ? static_cast<size_t>(obs) : pending.size();
    if (!write_all(output, pending.data(), chunk_size)) return false;
    ++stats.out_records;
    stats.bytes_copied += chunk_size;
    pending.erase(pending.begin(), pending.begin() + chunk_size);
  }
  return true;
}

auto report_stats(const Config& cfg, const CopyStats& stats) -> void {
  if (cfg.status_none) return;
  safeErrorPrintLn(std::to_string(stats.in_records) + " records in");
  safeErrorPrintLn(std::to_string(stats.out_records) + " records out");
  if (!cfg.status_noxfer) {
    safeErrorPrintLn(std::to_string(stats.bytes_copied) + " bytes copied");
  }
}

}  // namespace dd_pipeline

REGISTER_COMMAND(dd,
                 /* name */
                 "dd",

                 /* synopsis */
                 "dd [OPTION]...",

                 /* description */
                 "Convert and copy a file with specified block size and count.",

                 /* examples */
                 "dd if=input.txt of=output.txt bs=4096",

                 /* see_also */
                 "cp(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 DD_OPTIONS) {
  using namespace dd_pipeline;

  Config cfg;
  if (!parse_config(ctx, cfg)) return 1;

  HANDLE hIn = INVALID_HANDLE_VALUE;
  HANDLE hOut = INVALID_HANDLE_VALUE;
  HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

  if (!cfg.input_file.empty()) {
    // Route through the shared operand boundary so MSYS-style paths and
    // POSIX pseudo-devices (/dev/null -> NUL) resolve like other tools (#276).
    std::wstring winput =
        utf8_to_wstring(native_path::normalize_api_operand(cfg.input_file));
    hIn = CreateFileW(winput.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hIn == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.open_input", "dd: failed to open '{}': {}",
          cfg.input_file, win32_posix_error_text(GetLastError())));
      return 1;
    }
  } else {
    hIn = stdin_handle;
  }

  if (!cfg.output_file.empty()) {
    std::wstring woutput =
        utf8_to_wstring(native_path::normalize_api_operand(cfg.output_file));
    DWORD creation = cfg.notrunc ? OPEN_ALWAYS : CREATE_ALWAYS;
    hOut = CreateFileW(woutput.c_str(), GENERIC_WRITE, 0, nullptr, creation,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("dd: cannot create output file '" + cfg.output_file +
                       "'");
      if (hIn != INVALID_HANDLE_VALUE && hIn != stdin_handle) CloseHandle(hIn);
      return 1;
    }
  } else {
    hOut = stdout_handle;
  }

  auto skip_bytes = checked_product(cfg.skip, cfg.ibs);
  auto seek_bytes = checked_product(cfg.seek, cfg.obs);
  if (!skip_bytes || !seek_bytes) {
    // [GNU] an offset that overflows reports EOVERFLOW wording
    // (uutils #14199)
    if (!skip_bytes) {
      const std::string& in_name =
          cfg.input_file.empty() ? std::string("standard input")
                                 : cfg.input_file;
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.cannot_skip",
          "dd: {}: cannot skip: Value too large for defined data type",
          in_name));
    } else {
      const std::string& out_name =
          cfg.output_file.empty() ? std::string("standard output")
                                  : cfg.output_file;
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.cannot_seek",
          "dd: {}: cannot seek: Value too large for defined data type",
          out_name));
    }
    if (hIn != INVALID_HANDLE_VALUE && hIn != stdin_handle) CloseHandle(hIn);
    if (hOut != INVALID_HANDLE_VALUE && hOut != stdout_handle)
      CloseHandle(hOut);
    return 1;
  }

  if (*skip_bytes > 0) {
    bool skipped = cfg.input_file.empty() ? discard_input(hIn, *skip_bytes)
                                          : seek_handle(hIn, *skip_bytes);
    if (!skipped) {
      safeErrorPrintLn("dd: failed to skip input");
      if (hIn != INVALID_HANDLE_VALUE && hIn != stdin_handle) CloseHandle(hIn);
      if (hOut != INVALID_HANDLE_VALUE && hOut != stdout_handle)
        CloseHandle(hOut);
      return 1;
    }
  }

  if (*seek_bytes > 0 && !cfg.output_file.empty()) {
    if (!seek_handle(hOut, *seek_bytes)) {
      safeErrorPrintLn("dd: failed to seek output");
      if (hIn != INVALID_HANDLE_VALUE && hIn != stdin_handle) CloseHandle(hIn);
      if (hOut != INVALID_HANDLE_VALUE && hOut != stdout_handle)
        CloseHandle(hOut);
      return 1;
    }
  }

  std::vector<char> input_buffer(static_cast<size_t>(cfg.ibs));
  std::vector<char> output_buffer;
  output_buffer.reserve(static_cast<size_t>(cfg.obs));
  CopyStats stats;
  bool read_failed = false;

  while (!cfg.count_set || stats.in_records < cfg.count) {
    std::size_t bytes_read = 0;
    DWORD request = static_cast<DWORD>(std::min<std::uintmax_t>(
        cfg.ibs,
        static_cast<std::uintmax_t>(std::numeric_limits<DWORD>::max())));
    bool read_error = false;
    const auto action = recover_read_block(
        [&](char* data, std::size_t amount, std::size_t& got) {
          DWORD native_bytes = 0;
          const bool ok = ReadFile(hIn, data, static_cast<DWORD>(amount),
                                   &native_bytes, nullptr);
          if (!ok) {
            const DWORD err = GetLastError();
            // A pipe whose write end closed (MSYS/Git-Bash pipelines) reports
            // ERROR_BROKEN_PIPE; that is EOF, not a read failure. Treat it
            // like ReadFile's normal EOF (TRUE + 0 bytes) so piped stdin
            // terminates cleanly instead of reporting "dd: read error".
            if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) {
              got = 0;
              read_error = false;
              return true;
            }
          }
          got = native_bytes;
          read_error = !ok;
          return ok;
        },
        [&](std::size_t amount) { return seek_handle(hIn, amount); },
        cfg.noerror && !cfg.input_file.empty(), cfg.sync_blocks, request,
        output_buffer, stats.in_records, input_buffer, bytes_read);
    if (read_error) {
      safeErrorPrintLn(
          winux::i18n::translate("command.dd.error.read", "dd: read error"));
    }
    if (action == ReadBlockAction::stop) {
      if (read_error && (!cfg.noerror || cfg.input_file.empty())) {
        read_failed = true;
      }
      break;
    }
    if (action == ReadBlockAction::recovered) continue;
    if (bytes_read == 0) break;

    ++stats.in_records;
    output_buffer.insert(output_buffer.end(), input_buffer.begin(),
                         input_buffer.begin() + bytes_read);
    if (cfg.sync_blocks && bytes_read < request) {
      // [GNU] conv=sync pads to cbs (conversion block size), not ibs
      std::uintmax_t pad_target = cfg.cbs > 0 ? cfg.cbs : cfg.ibs;
      if (bytes_read < pad_target) {
        output_buffer.insert(output_buffer.end(),
                             static_cast<size_t>(pad_target - bytes_read),
                             '\0');
      }
    }
    if (!flush_output_buffer(hOut, output_buffer, cfg.obs, stats, false)) {
      safeErrorPrintLn("dd: write error");
      break;
    }
  }

  bool flushed = flush_output_buffer(hOut, output_buffer, cfg.obs, stats, true);
  if (!flushed) safeErrorPrintLn("dd: write error");

  if (hIn != INVALID_HANDLE_VALUE && hIn != stdin_handle) CloseHandle(hIn);
  if (hOut != INVALID_HANDLE_VALUE && hOut != stdout_handle) CloseHandle(hOut);

  report_stats(cfg, stats);

  return flushed && !read_failed ? 0 : 1;
}
