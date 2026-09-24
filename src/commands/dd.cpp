/*
 *  Copyright © 2026 WinuxCmd
 */
// clang-format off
#include "core/command_macros.h"
#include "pch/pch.h"
#include <winioctl.h>
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
    // [GNU] iseek=N: alias for skip=N (coreutils >= 9.1, uutils#5903)
    OPTION("iseek", "", "same as skip=N", STRING_TYPE),
    // [GNU] oseek=N: alias for seek=N (coreutils >= 9.1, uutils#5903)
    OPTION("oseek", "", "same as seek=N", STRING_TYPE),
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
  bool input_given = false;
  bool output_given = false;
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
  // [GNU] byte-count modes: an 'NB' operand value or the obsolescent
  // count_bytes/skip_bytes/seek_bytes flags make the number a byte count
  // instead of a block count (flags since 8.32, 'B' suffix since 9.1).
  bool skip_is_bytes = false;
  bool seek_is_bytes = false;
  bool count_is_bytes = false;
  bool iflag_nonblock = false;
  bool conv_excl = false;
  bool conv_nocreat = false;
  bool conv_sparse = false;
  bool conv_fsync = false;
  // [GNU] bs= is applied after all operands, so it wins over ibs=/obs=
  // regardless of operand order.
  std::optional<std::uintmax_t> bs_override;
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

// [GNU] dd.c parse_integer: a non-negative decimal integer, one optional
// scale suffix per factor ("bcEGkKMPQRTwYZ0"), an optional single 'B'
// byte-count marker, and recursive 'x' factor multiplication.
struct ParsedNumber {
  std::uintmax_t value = 0;
  enum class Error { ok, invalid, overflow } error = Error::ok;
};

// GNU stores results in intmax_t; anything above INTMAX_MAX is an overflow.
inline constexpr std::uintmax_t kIntMax =
    static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max());

auto parse_dd_number(std::string_view text) -> ParsedNumber {
  using Error = ParsedNumber::Error;

  size_t digits_end = 0;
  while (digits_end < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[digits_end]))) {
    ++digits_end;
  }
  if (digits_end == 0) return {0, Error::invalid};

  std::uintmax_t n = 0;
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + digits_end, n);
  bool overflow = ec == std::errc::result_out_of_range;
  if (overflow) n = std::numeric_limits<std::uintmax_t>::max();

  // Optional scale suffix (xstrtoumax rules): one letter, and for the
  // power-of-two letters an optional 'B'/'D' (xbase=1000) or 'iB' (xbase
  // stays 1024) second suffix.
  if (digits_end < text.size()) {
    const char suffix = text[digits_end];
    std::uintmax_t base = 0;
    unsigned power = 0;
    bool have_suffix = true;
    bool power_letter = false;
    switch (suffix) {
      case 'b':
        base = 512;
        power = 1;
        break;
      case 'c':
        base = 1;
        power = 0;
        break;
      case 'w':
        base = 2;
        power = 1;
        break;
      case 'k':
      case 'K':
        base = 1024;
        power = 1;
        power_letter = true;
        break;
      case 'M':
        base = 1024;
        power = 2;
        power_letter = true;
        break;
      case 'G':
        base = 1024;
        power = 3;
        power_letter = true;
        break;
      case 'T':
        base = 1024;
        power = 4;
        power_letter = true;
        break;
      case 'P':
        base = 1024;
        power = 5;
        power_letter = true;
        break;
      case 'E':
        base = 1024;
        power = 6;
        power_letter = true;
        break;
      case 'Z':
        base = 1024;
        power = 7;
        power_letter = true;
        break;
      case 'Y':
        base = 1024;
        power = 8;
        power_letter = true;
        break;
      case 'R':
        base = 1024;
        power = 9;
        power_letter = true;
        break;
      case 'Q':
        base = 1024;
        power = 10;
        power_letter = true;
        break;
      default:
        have_suffix = false;
        break;
    }
    if (have_suffix) {
      size_t pos = digits_end + 1;
      if (power_letter && pos < text.size()) {
        if (text[pos] == 'B' || text[pos] == 'D') {
          base = 1000;
          ++pos;
        } else if (pos + 1 < text.size() && text[pos] == 'i' &&
                   text[pos + 1] == 'B') {
          pos += 2;
        }
      }
      // Multiply stepwise like gnulib bkm_scale_by_power so that a zero
      // multiplicand never overflows (dd count=0R is 0, not an error).
      for (unsigned i = 0; i < power && !overflow; ++i) {
        if (n != 0 && n > std::numeric_limits<std::uintmax_t>::max() / base) {
          overflow = true;
          n = std::numeric_limits<std::uintmax_t>::max();
          break;
        }
        n *= base;
      }
      digits_end = pos;
    }
  }

  std::string_view rem = text.substr(digits_end);

  // [GNU] A single 'B' not preceded by 'B' is the byte-count marker; it is
  // stripped by the parser (the caller inspects the raw operand for 'B').
  if (!rem.empty() && rem.front() == 'B' && text[digits_end - 1] != 'B') {
    rem.remove_prefix(1);
  }

  if (!rem.empty() && rem.front() == 'x') {
    const ParsedNumber factor = parse_dd_number(rem.substr(1));
    if (factor.error == Error::invalid) return {0, Error::invalid};

    const bool mul_overflow = n != 0 && factor.value > kIntMax / n;
    const std::uintmax_t product = mul_overflow ? kIntMax : n * factor.value;
    if (mul_overflow ||
        (product != 0 && (overflow || factor.error == Error::overflow))) {
      return {kIntMax, Error::overflow};
    }
    // [GNU] A zero product forgives any factor overflow; warn only when the
    // factor string literally starts with "0x" (uutils#11672, uutils#14160).
    if (product == 0 && text.starts_with("0x")) {
      safeErrorPrintLn(winux::i18n::translate(
          "command.dd.warning.zero_multiplier",
          "dd: warning: '0x' is a zero multiplier; use '00x' if that is "
          "intended"));
    }
    return {product, Error::ok};
  }

  if (!rem.empty()) return {0, Error::invalid};
  if (n > kIntMax) return {kIntMax, Error::overflow};
  return {n, overflow ? Error::overflow : Error::ok};
}

auto report_invalid_number(std::string_view text, bool overflowed) -> bool {
  if (overflowed) {
    safeErrorPrintLn(winux::i18n::format(
        "command.dd.error.invalid_number_overflow",
        "dd: invalid number: '{}': Value too large for defined data type",
        std::string(text)));
  } else {
    safeErrorPrintLn(winux::i18n::format("command.dd.error.invalid_number",
                                         "dd: invalid number: '{}'",
                                         std::string(text)));
  }
  return false;
}

// [GNU] Each numeric operand has a minimum (1 for block sizes, 0 for
// skip/seek/count) and a maximum (~intmax_t); overflow is reported with the
// EOVERFLOW wording, anything else as a plain invalid number.
auto parse_numeric_operand(std::string_view text, std::uintmax_t min_value,
                           std::uintmax_t max_value, std::uintmax_t& target)
    -> bool {
  const ParsedNumber parsed = parse_dd_number(text);
  switch (parsed.error) {
    case ParsedNumber::Error::invalid:
      return report_invalid_number(text, false);
    case ParsedNumber::Error::overflow:
      return report_invalid_number(text, true);
    case ParsedNumber::Error::ok:
      break;
  }
  if (parsed.value < min_value) return report_invalid_number(text, false);
  if (parsed.value > max_value) return report_invalid_number(text, true);
  target = parsed.value;
  return true;
}

auto print_try_help() -> void {
  safeErrorPrintLn(winux::i18n::format(
      "common.try_help", "Try '{} --help' for more information.", "dd"));
}

// [GNU] conv= symbols actually implemented here; other valid-but-unimplemented
// GNU conversions are still diagnosed as unsupported.
auto parse_conv_list(std::string_view value, Config& cfg) -> bool {
  std::stringstream ss{std::string(value)};
  std::string token;
  while (std::getline(ss, token, ',')) {
    if (token == "notrunc") {
      cfg.notrunc = true;
    } else if (token == "sync") {
      cfg.sync_blocks = true;
    } else if (token == "noerror") {
      cfg.noerror = true;
    } else if (token == "excl") {
      cfg.conv_excl = true;
    } else if (token == "nocreat") {
      cfg.conv_nocreat = true;
    } else if (token == "sparse") {
      cfg.conv_sparse = true;
    } else if (token == "fsync" || token == "fdatasync") {
      cfg.conv_fsync = true;
    } else if (token == "ascii" || token == "ebcdic" || token == "ibm" ||
               token == "block" || token == "unblock" || token == "lcase" ||
               token == "ucase" || token == "swab") {
      safeErrorPrint("dd: unsupported conv flag '");
      safeErrorPrint(token);
      safeErrorPrint("' [DIFFERS: not supported on Windows]\n");
      return false;
    } else if (token.empty()) {
      continue;
    } else {
      safeErrorPrintLn(
          winux::i18n::format("command.dd.error.invalid_conversion",
                              "dd: invalid conversion: '{}'", token));
      print_try_help();
      return false;
    }
  }
  if (cfg.conv_excl && cfg.conv_nocreat) {
    // [GNU] die(), not usage(): no "Try 'dd --help'" suffix.
    safeErrorPrintLn(
        winux::i18n::translate("command.dd.error.cannot_combine_excl_nocreat",
                               "dd: cannot combine excl and nocreat"));
    return false;
  }
  return true;
}

// [GNU] iflag=/oflag= share one symbol table; flags that make no sense on a
// handle kind are accepted and ignored, matching GNU. oflag=fullblock is a
// hard error in GNU ("invalid output flag: 'fullblock'"), so it is absent
// from the accepted list and falls into the generic invalid-flag path.
auto parse_io_flags(std::string_view value, bool input, Config& cfg) -> bool {
  std::stringstream ss{std::string(value)};
  std::string token;
  while (std::getline(ss, token, ',')) {
    if (token.empty()) continue;
    if (token == "nonblock") {
      if (input) cfg.iflag_nonblock = true;
      continue;
    }
    if (token == "skip_bytes") {
      if (input) cfg.skip_is_bytes = true;
      continue;
    }
    if (token == "count_bytes") {
      if (input) cfg.count_is_bytes = true;
      continue;
    }
    if (token == "seek_bytes") {
      if (!input) cfg.seek_is_bytes = true;
      continue;
    }
    if (input && token == "fullblock") {
      // fullblock: accumulate full input blocks - honored conceptually; the
      // obs aggregation below already produces identical output bytes.
      continue;
    }
    if (token == "append" || token == "binary" || token == "cio" ||
        token == "direct" || token == "directory" || token == "dsync" ||
        token == "noatime" || token == "nocache" || token == "noctty" ||
        token == "nofollow" || token == "nolinks" || token == "sync" ||
        token == "text") {
      // [DIFFERS] no direct Windows console/file equivalent; accepted.
      continue;
    }
    safeErrorPrintLn(
        winux::i18n::format(input ? "command.dd.error.invalid_iflag"
                                  : "command.dd.error.invalid_oflag",
                            input ? "dd: invalid input flag: '{}'"
                                  : "dd: invalid output flag: '{}'",
                            token));
    print_try_help();
    return false;
  }
  return true;
}

auto parse_status_value(std::string_view value, Config& cfg) -> bool {
  if (value == "none") {
    cfg.status_none = true;
  } else if (value == "noxfer") {
    cfg.status_noxfer = true;
  } else if (!value.empty() && value != "progress") {
    safeErrorPrintLn(winux::i18n::format("command.dd.error.invalid_status",
                                         "dd: invalid status level: '{}'",
                                         std::string(value)));
    print_try_help();
    return false;
  }
  return true;
}

auto set_operand(Config& cfg, std::string_view name, std::string_view value,
                 std::string_view operand) -> bool {
  if (name == "if") {
    cfg.input_file = std::string(value);
    cfg.input_given = true;
  } else if (name == "of") {
    cfg.output_file = std::string(value);
    cfg.output_given = true;
  } else if (name == "ibs") {
    return parse_numeric_operand(value, 1, kIntMax - 1, cfg.ibs);
  } else if (name == "obs") {
    return parse_numeric_operand(value, 1, kIntMax - 1, cfg.obs);
  } else if (name == "bs") {
    std::uintmax_t bs = 0;
    if (!parse_numeric_operand(value, 1, kIntMax - 1, bs)) return false;
    cfg.bs_override = bs;
  } else if (name == "cbs") {
    return parse_numeric_operand(value, 1, kIntMax, cfg.cbs);
  } else if (name == "count") {
    cfg.count_set = true;
    if (!parse_numeric_operand(value, 0, kIntMax, cfg.count)) return false;
    cfg.count_is_bytes |= value.find('B') != std::string_view::npos;
  } else if (name == "skip" || name == "iseek") {
    if (!parse_numeric_operand(value, 0, kIntMax, cfg.skip)) return false;
    cfg.skip_is_bytes |= value.find('B') != std::string_view::npos;
  } else if (name == "seek" || name == "oseek") {
    if (!parse_numeric_operand(value, 0, kIntMax, cfg.seek)) return false;
    cfg.seek_is_bytes |= value.find('B') != std::string_view::npos;
  } else if (name == "conv") {
    return parse_conv_list(value, cfg);
  } else if (name == "status") {
    return parse_status_value(value, cfg);
  } else if (name == "iflag") {
    return parse_io_flags(value, true, cfg);
  } else if (name == "oflag") {
    return parse_io_flags(value, false, cfg);
  } else {
    // [GNU] diagnostics quote the whole "name=value" operand
    safeErrorPrintLn(winux::i18n::format(
        "command.dd.error.unrecognized_operand",
        "dd: unrecognized operand '{}'", std::string(operand)));
    print_try_help();
    return false;
  }
  return true;
}

auto parse_config(const CommandContext<DD_OPTIONS.size()>& ctx, Config& cfg)
    -> bool {
  // Operands may arrive either as "name=value" positionals (the dd idiom) or
  // as registered --name=value options; both paths feed the same set_operand.
  for (auto positional : ctx.positionals) {
    auto eq = positional.find('=');
    if (eq == std::string_view::npos || eq == 0) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.unrecognized_operand",
          "dd: unrecognized operand '{}'", std::string(positional)));
      print_try_help();
      return false;
    }

    auto name = positional.substr(0, eq);
    auto value = positional.substr(eq + 1);
    if (!set_operand(cfg, name, value, positional)) return false;
  }

  // Registered long-option form (--if=FILE, --bs=1K, ...). An explicitly empty
  // value is still an operand, e.g. `dd --count=` is an invalid number, while
  // `if=` targets an empty file name.
  static constexpr std::array numeric_options = {
      "ibs", "obs", "bs", "cbs", "count", "skip", "seek", "iseek", "oseek"};
  static constexpr std::array string_options = {"if",     "of",    "conv",
                                                "status", "iflag", "oflag"};
  for (auto name : string_options) {
    if (ctx.has(name)) {
      if (!set_operand(cfg, name, ctx.get<std::string>(name, ""), name)) {
        return false;
      }
    }
  }
  for (auto name : numeric_options) {
    if (ctx.has(name)) {
      if (!set_operand(cfg, name, ctx.get<std::string>(name, ""), name)) {
        return false;
      }
    }
  }

  // [GNU] bs= applies after every operand has been seen.
  if (cfg.bs_override) {
    cfg.ibs = *cfg.bs_override;
    cfg.obs = *cfg.bs_override;
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

auto discard_input(HANDLE input, std::uintmax_t bytes,
                   std::uintmax_t& discarded) -> bool {
  std::array<char, 8192> buffer{};
  std::uintmax_t remaining = bytes;
  discarded = 0;
  while (remaining > 0) {
    DWORD request =
        static_cast<DWORD>(std::min<std::uintmax_t>(remaining, buffer.size()));
    DWORD got = 0;
    if (!ReadFile(input, buffer.data(), request, &got, nullptr)) return false;
    if (got == 0) return true;
    discarded += got;
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

// [GNU] quotef-style display name: simple names pass through, anything with
// spaces or quotes (or the stdin/stdout pseudo-names) gets single quotes.
auto quoted_name(std::string_view name) -> std::string {
  const bool needs_quotes =
      name.find_first_of(" \t'\"") != std::string_view::npos;
  if (!needs_quotes) return std::string(name);
  return "'" + std::string(name) + "'";
}

// Pseudo-device operands handled inside dd rather than by the generic path
// boundary: /dev/stdin must resolve to the real stdin handle (a pipe!), not
// CONIN$; /dev/zero, /dev/urandom and /dev/full have no Windows file at all.
enum class InputSource { handle, zero, random };
enum class OutputSink { handle, full };

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
  bool own_hIn = false;
  bool own_hOut = false;
  InputSource input_source = InputSource::handle;
  OutputSink output_sink = OutputSink::handle;

  auto std_handle_for_fd = [](int fd) -> HANDLE {
    switch (fd) {
      case 0:
        return GetStdHandle(STD_INPUT_HANDLE);
      case 1:
        return GetStdHandle(STD_OUTPUT_HANDLE);
      default:
        return GetStdHandle(STD_ERROR_HANDLE);
    }
  };

  if (cfg.input_given) {
    // Route through the shared operand boundary so MSYS-style paths and
    // POSIX pseudo-devices (/dev/null -> NUL) resolve like other tools (#276).
    if (cfg.input_file == "/dev/zero" || cfg.input_file == "/dev/full") {
      input_source = InputSource::zero;
    } else if (cfg.input_file == "/dev/urandom" ||
               cfg.input_file == "/dev/random") {
      input_source = InputSource::random;
    } else if (auto fd = native_path::pseudo_device_std_fd(cfg.input_file)) {
      hIn = std_handle_for_fd(*fd);
    } else {
      std::wstring winput =
          utf8_to_wstring(native_path::normalize_api_operand(cfg.input_file));
      if (native_path::is_winux_fifo_w(winput)) {
        // On-disk fifo marker (#1038): read through the named pipe.
        hIn = file_io::open_fifo_read_handle(winput);
      } else {
        hIn =
            CreateFileW(winput.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      }
      own_hIn = true;
      if (hIn == INVALID_HANDLE_VALUE) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.open_input", "dd: failed to open '{}': {}",
            cfg.input_file,
            win32_posix_error_text(GetLastError(),
                                   {.invalid_name_as_missing = true})));
        return 1;
      }
    }
  } else {
    hIn = stdin_handle;
  }

  if (cfg.output_given) {
    if (cfg.output_file == "/dev/full") {
      output_sink = OutputSink::full;
    } else if (auto fd = native_path::pseudo_device_std_fd(cfg.output_file)) {
      hOut = std_handle_for_fd(*fd);
    } else {
      std::wstring woutput =
          utf8_to_wstring(native_path::normalize_api_operand(cfg.output_file));
      // [GNU] O_TRUNC is dropped when seek= or conv=notrunc is given; the
      // seek truncation below then sizes the file like GNU's ftruncate
      // (uutils#9745: a failed truncate must surface, not be suppressed).
      DWORD creation = CREATE_ALWAYS;
      if (cfg.conv_excl) {
        creation = CREATE_NEW;
      } else if (cfg.conv_nocreat) {
        creation = OPEN_EXISTING;
      } else if (cfg.notrunc || cfg.seek != 0) {
        creation = OPEN_ALWAYS;
      }
      if (native_path::is_winux_fifo_w(woutput)) {
        // On-disk fifo marker (#1038): write blocks until a reader opens.
        hOut = file_io::open_fifo_write_handle(woutput);
      } else {
        hOut = CreateFileW(woutput.c_str(), GENERIC_WRITE, 0, nullptr, creation,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
      }
      own_hOut = true;
      if (hOut == INVALID_HANDLE_VALUE) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.open_output", "dd: failed to open '{}': {}",
            cfg.output_file,
            win32_posix_error_text(
                GetLastError(),
                {.file_exists = true, .invalid_name_as_missing = true})));
        if (own_hIn && hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);
        return 1;
      }
      if (cfg.conv_sparse) {
        // [GNU] conv=sparse: mark the file sparse so later NUL runs do not
        // allocate storage; the write path itself is unchanged.
        DWORD returned = 0;
        DeviceIoControl(hOut, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
                        &returned, nullptr);
      }
    }
  } else {
    hOut = stdout_handle;
  }

  auto input_name = [&]() -> std::string {
    return cfg.input_given ? quoted_name(cfg.input_file)
                           : std::string("'standard input'");
  };
  auto output_name = [&]() -> std::string {
    return cfg.output_given ? quoted_name(cfg.output_file)
                            : std::string("'standard output'");
  };

  auto close_handles = [&]() {
    if (own_hIn && hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);
    if (own_hOut && hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
  };

  auto skip_bytes = cfg.skip_is_bytes ? std::optional<std::uintmax_t>(cfg.skip)
                                      : checked_product(cfg.skip, cfg.ibs);
  auto seek_bytes = cfg.seek_is_bytes ? std::optional<std::uintmax_t>(cfg.seek)
                                      : checked_product(cfg.seek, cfg.obs);
  const bool seek_overflow = !seek_bytes;

  // [GNU] An input skip offset that does not fit off_t reports EOVERFLOW for
  // seekable inputs and is discarded-by-read for streams; an output seek
  // offset overflow is the "offset too large" diagnostic.
  if (!skip_bytes) {
    const DWORD ftype =
        input_source == InputSource::handle ? GetFileType(hIn) : FILE_TYPE_DISK;
    if (ftype == FILE_TYPE_DISK || ftype == FILE_TYPE_CHAR) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.cannot_skip",
          "dd: {}: cannot skip: Value too large for defined data type",
          input_name()));
      report_stats(cfg, CopyStats{});
      close_handles();
      return 1;
    }
    // Unseekable input: read-and-discard to EOF, then warn below.
    skip_bytes = std::numeric_limits<std::uintmax_t>::max();
  }
  if (!seek_bytes) {
    if (cfg.output_given && output_sink == OutputSink::handle) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.offset_too_large",
          "dd: offset too large: cannot truncate to a length of seek={} "
          "({}-byte) blocks",
          cfg.seek, cfg.obs));
      close_handles();
      return 1;
    }
    // stdout: GNU falls into the seek path which fails ESPIPE.
    seek_bytes = std::numeric_limits<std::uintmax_t>::max();
  }

  if (*skip_bytes > 0) {
    bool skip_short = false;
    if (input_source != InputSource::handle) {
      // /dev/zero, /dev/urandom, /dev/full: infinite sources, lseek succeeds.
    } else {
      const DWORD ftype = GetFileType(hIn);
      if (ftype == FILE_TYPE_DISK) {
        LARGE_INTEGER size{};
        const bool have_size = GetFileSizeEx(hIn, &size) != 0;
        // [GNU] skipping past EOF of a nonempty regular file clamps to EOF
        // and warns "cannot skip to specified offset" (non-fatal).
        if (have_size && size.QuadPart > 0 &&
            *skip_bytes > static_cast<std::uintmax_t>(size.QuadPart)) {
          if (!seek_handle(hIn, static_cast<std::uintmax_t>(size.QuadPart))) {
            safeErrorPrintLn(winux::i18n::format(
                "command.dd.error.cannot_skip_errno", "dd: {}: cannot skip: {}",
                input_name(), win32_posix_error_text(GetLastError())));
            report_stats(cfg, CopyStats{});
            close_handles();
            return 1;
          }
          skip_short = true;
        } else if (!seek_handle(hIn, *skip_bytes)) {
          safeErrorPrintLn(winux::i18n::format(
              "command.dd.error.cannot_skip_errno", "dd: {}: cannot skip: {}",
              input_name(), win32_posix_error_text(GetLastError())));
          report_stats(cfg, CopyStats{});
          close_handles();
          return 1;
        }
      } else if (ftype == FILE_TYPE_CHAR) {
        // NUL/console-class device: lseek trivially succeeds, nothing to
        // discard (matches GNU on /dev/null).
      } else {
        std::uintmax_t discarded = 0;
        if (!discard_input(hIn, *skip_bytes, discarded)) {
          safeErrorPrintLn(winux::i18n::format(
              "command.dd.error.read_named", "dd: error reading '{}': {}",
              cfg.input_given ? cfg.input_file : std::string("standard input"),
              win32_posix_error_text(GetLastError())));
          report_stats(cfg, CopyStats{});
          close_handles();
          return 1;
        }
        skip_short = discarded < *skip_bytes;
      }
    }
    if (skip_short && !cfg.status_none) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.warning.cannot_skip_offset",
          "dd: {}: cannot skip to specified offset", input_name()));
    }
  }

  if (*seek_bytes > 0) {
    if (output_sink == OutputSink::full) {
      // /dev/full accepts seeks; the write fails later like GNU.
    } else if (!cfg.output_given) {
      // GNU lseeks stdout: works for a redirected regular file, ESPIPE for a
      // pipe/console.
      if (!seek_handle(hOut, *seek_bytes)) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.cannot_seek_errno", "dd: {}: cannot seek: {}",
            output_name(),
            seek_overflow ? std::string("Value too large for defined data type")
                          : std::string("Illegal seek")));
        report_stats(cfg, CopyStats{});
        close_handles();
        return 1;
      }
    } else if (GetFileType(hOut) != FILE_TYPE_CHAR) {
      if (!seek_handle(hOut, *seek_bytes)) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.cannot_seek_errno", "dd: {}: cannot seek: {}",
            output_name(), win32_posix_error_text(GetLastError())));
        report_stats(cfg, CopyStats{});
        close_handles();
        return 1;
      }
      // [GNU] ftruncate(seek*obs): commit the size now so the tail is zero
      // filled (or truncated) even when nothing is ever written.
      if (!cfg.notrunc && !SetEndOfFile(hOut)) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.truncate_output",
            "dd: failed to truncate to {} bytes in output file '{}': {}",
            *seek_bytes, cfg.output_file,
            win32_posix_error_text(GetLastError())));
        report_stats(cfg, CopyStats{});
        close_handles();
        return 1;
      }
    }
  }

  std::vector<char> input_buffer;
  std::vector<char> output_buffer;
  try {
    input_buffer.resize(static_cast<size_t>(cfg.ibs));
    output_buffer.reserve(static_cast<size_t>(cfg.obs));
  } catch (const std::bad_alloc&) {
    safeErrorPrintLn(winux::i18n::translate("command.dd.error.memory_exhausted",
                                            "dd: memory exhausted"));
    close_handles();
    return 1;
  }
  CopyStats stats;
  bool read_failed = false;
  bool write_failed = false;
  std::uintmax_t bytes_remaining =
      cfg.count_is_bytes ? cfg.count
                         : std::numeric_limits<std::uintmax_t>::max();
  std::random_device random_source;

  while (!cfg.count_set ||
         (cfg.count_is_bytes ? bytes_remaining > 0
                             : stats.in_records < cfg.count)) {
    std::size_t bytes_read = 0;
    DWORD request = static_cast<DWORD>(std::min<std::uintmax_t>(
        cfg.ibs,
        static_cast<std::uintmax_t>(std::numeric_limits<DWORD>::max())));
    if (cfg.count_is_bytes &&
        static_cast<std::uintmax_t>(request) > bytes_remaining) {
      request = static_cast<DWORD>(bytes_remaining);
    }
    bool read_error = false;
    std::string read_error_text;
    const auto action = recover_read_block(
        [&](char* data, std::size_t amount, std::size_t& got) {
          if (input_source == InputSource::zero) {
            std::memset(data, 0, amount);
            got = amount;
            return true;
          }
          if (input_source == InputSource::random) {
            std::size_t offset = 0;
            while (offset < amount) {
              const unsigned int v = random_source();
              const std::size_t chunk = std::min(sizeof(v), amount - offset);
              std::memcpy(data + offset, &v, chunk);
              offset += chunk;
            }
            got = amount;
            return true;
          }
          if (cfg.iflag_nonblock) {
            // [GNU] O_NONBLOCK on the input: a read that would block returns
            // EAGAIN. Windows has no per-handle nonblocking mode, but for
            // pipes PeekNamedPipe reports the buffered byte count (uutils
            // #11543). Regular files and console handles are unaffected.
            DWORD available = 0;
            if (PeekNamedPipe(hIn, nullptr, 0, nullptr, &available, nullptr) &&
                available == 0) {
              read_error_text = "Resource temporarily unavailable";
              read_error = true;
              return false;
            }
          }
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
            read_error_text = win32_posix_error_text(err);
          }
          got = native_bytes;
          read_error = !ok;
          return ok;
        },
        [&](std::size_t amount) { return seek_handle(hIn, amount); },
        cfg.noerror && !cfg.input_file.empty(), cfg.sync_blocks, request,
        output_buffer, stats.in_records, input_buffer, bytes_read);
    if (read_error) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.read_named", "dd: error reading '{}': {}",
          cfg.input_given ? cfg.input_file : std::string("standard input"),
          read_error_text));
    }
    if (action == ReadBlockAction::stop) {
      if (read_error && (!cfg.noerror || cfg.input_file.empty())) {
        read_failed = true;
      }
      break;
    }
    if (action == ReadBlockAction::recovered) continue;
    if (bytes_read == 0) break;
    if (cfg.count_is_bytes) {
      bytes_remaining -= std::min<std::uintmax_t>(bytes_remaining, bytes_read);
    }

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
    if (output_sink == OutputSink::full) {
      // [GNU] writes to /dev/full fail ENOSPC.
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.write_named", "dd: error writing '{}': {}",
          cfg.output_file, std::string("No space left on device")));
      write_failed = true;
      break;
    }
    if (!flush_output_buffer(hOut, output_buffer, cfg.obs, stats, false)) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.write_named", "dd: error writing '{}': {}",
          cfg.output_given ? cfg.output_file : std::string("standard output"),
          win32_posix_error_text(GetLastError())));
      write_failed = true;
      break;
    }
  }

  bool flushed = true;
  if (!write_failed) {
    if (output_sink == OutputSink::full) {
      if (!output_buffer.empty()) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.write_named", "dd: error writing '{}': {}",
            cfg.output_file, std::string("No space left on device")));
        flushed = false;
        write_failed = true;
      }
    } else {
      flushed = flush_output_buffer(hOut, output_buffer, cfg.obs, stats, true);
      if (!flushed) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.write_named", "dd: error writing '{}': {}",
            cfg.output_given ? cfg.output_file : std::string("standard output"),
            win32_posix_error_text(GetLastError())));
      }
    }
  }

  // [GNU] conv=fsync/fdatasync: flush the output before finishing.
  if (!write_failed && cfg.conv_fsync && hOut != INVALID_HANDLE_VALUE &&
      GetFileType(hOut) == FILE_TYPE_DISK) {
    if (!FlushFileBuffers(hOut)) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.fsync_failed", "dd: fsync failed for '{}': {}",
          cfg.output_given ? cfg.output_file : std::string("standard output"),
          win32_posix_error_text(GetLastError())));
      write_failed = true;
    }
  }

  close_handles();
  report_stats(cfg, stats);

  return flushed && !read_failed && !write_failed ? 0 : 1;
}
