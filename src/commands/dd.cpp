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
    OPTION("ibs", "", "read up to BYTES bytes at a time (default: 512)",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("obs", "", "write BYTES bytes at a time (default: 512)",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("bs", "",
           "read and write up to BYTES bytes at a time (default: 512); "
           "overrides ibs and obs",
           STRING_TYPE),
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
    OPTION("conv", "",
           "convert the file as per the comma separated symbol "
           "list",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("status", "",
           "The LEVEL of information to print to stderr; 'none' suppresses "
           "everything but error messages, 'noxfer' suppresses the final "
           "transfer statistics, 'progress' is accepted; final statistics "
           "are printed as the copy finishes",
           STRING_TYPE),
    // [GNU] iflag: read flags
    OPTION("iflag", "", "read as per the comma separated symbol list",
           STRING_TYPE),
    // [GNU] oflag: write flags
    OPTION("oflag", "", "write as per the comma separated symbol list",
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
  // [GNU] dd.c conversions[] (9.4 src/dd.c:270-285): the eight data
  // conversions each imply the two-buffer output path; ascii/ebcdic/ibm
  // also imply unblock/block. cbs=0 later clears block/unblock silently
  // (dd.c:1609-1610), so the parsed form is kept separate.
  bool conv_ascii = false;
  bool conv_ebcdic = false;
  bool conv_ibm = false;
  bool conv_block = false;
  bool conv_unblock = false;
  bool conv_lcase = false;
  bool conv_ucase = false;
  bool conv_swab = false;
  // [GNU] C_TWOBUFS stays set even when cbs=0 clears block/unblock; it only
  // gates the partial-read warning here.
  bool conv_twobufs = false;
  bool bs_given = false;
  bool iflag_fullblock = false;
  bool iflag_direct = false;
  bool oflag_direct = false;
  bool iflag_nocache = false;
  bool oflag_nocache = false;
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

// [GNU] dd.c:183-193: the summary counts full and partial input/output
// blocks separately; truncated conv=block records get their own line.
struct CopyStats {
  std::uintmax_t in_full = 0;
  std::uintmax_t in_partial = 0;
  std::uintmax_t out_full = 0;
  std::uintmax_t out_partial = 0;
  std::uintmax_t bytes_copied = 0;
  std::uintmax_t truncated_records = 0;
};

enum class ReadBlockAction { success, pad, skip, stop };

// On a read error with conv=noerror, GNU seeks past the failed block
// (dd.c:2220-2230) and continues; with conv=sync the block is replaced by
// the pad byte (' ' under block/unblock, NUL otherwise - dd.c:2191-2193,
// 2242-2248) and flows through the conversion pipeline like a normal
// partial read. A failed seek still continues the copy but marks the run
// failed, like GNU's advance_input_after_read_error error path.
auto recover_read_block(
    std::function<bool(char*, std::size_t, std::size_t&)> read,
    std::function<bool(std::size_t)> seek, bool noerror, bool sync_blocks,
    char pad_byte, std::size_t request, std::vector<char>& input_buffer,
    std::size_t& bytes_read, bool& recovery_seek_failed) -> ReadBlockAction {
  bytes_read = 0;
  recovery_seek_failed = false;
  if (read(input_buffer.data(), request, bytes_read)) {
    return ReadBlockAction::success;
  }
  if (!noerror) return ReadBlockAction::stop;

  const bool can_continue = seek(request);
  recovery_seek_failed = !can_continue;
  if (!sync_blocks) return ReadBlockAction::skip;
  // [GNU] the recovered block is a full ibs-sized pad block (dd.c:2191-2193
  // pre-fills ibuf; 2242-2248 pads to input_blocksize).
  std::fill(input_buffer.begin(), input_buffer.end(), pad_byte);
  return ReadBlockAction::pad;
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

// [GNU] conv= symbol table (dd.c:270-285): ascii/ebcdic/ibm imply the
// record conversions unblock/block respectively; all of the eight data
// conversions also imply the two-buffer output path. Mutually exclusive
// combinations are diagnosed after parsing, not here (dd.c:1665-1676).
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
    } else if (token == "ascii") {
      cfg.conv_ascii = true;
      cfg.conv_unblock = cfg.conv_twobufs = true;
    } else if (token == "ebcdic") {
      cfg.conv_ebcdic = true;
      cfg.conv_block = cfg.conv_twobufs = true;
    } else if (token == "ibm") {
      cfg.conv_ibm = true;
      cfg.conv_block = cfg.conv_twobufs = true;
    } else if (token == "block") {
      cfg.conv_block = cfg.conv_twobufs = true;
    } else if (token == "unblock") {
      cfg.conv_unblock = cfg.conv_twobufs = true;
    } else if (token == "lcase") {
      cfg.conv_lcase = cfg.conv_twobufs = true;
    } else if (token == "ucase") {
      cfg.conv_ucase = cfg.conv_twobufs = true;
    } else if (token == "swab") {
      cfg.conv_swab = cfg.conv_twobufs = true;
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
      // [GNU] iread_fullblock (dd.c:1155-1173): accumulate short reads
      // into full input blocks.
      cfg.iflag_fullblock = true;
      continue;
    }
    if (token == "direct") {
      // [DIFFERS] no Windows equivalent of O_DIRECT; tracked only for the
      // GNU direct+nocache exclusivity check and the partial-read warning.
      if (input)
        cfg.iflag_direct = true;
      else
        cfg.oflag_direct = true;
      continue;
    }
    if (token == "nocache") {
      if (input)
        cfg.iflag_nocache = true;
      else
        cfg.oflag_nocache = true;
      continue;
    }
    if (token == "append" || token == "binary" || token == "cio" ||
        token == "directory" || token == "dsync" || token == "noatime" ||
        token == "noctty" || token == "nofollow" || token == "nolinks" ||
        token == "sync" || token == "text") {
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
    // [native-glob] #1374: in WINUX_SHELL_GLOB=native mode `dd if=*`
    // reaches us unexpanded; the if= value goes through the shared
    // expansion boundary like a plain file operand (first match wins).
    // of= names a file to create and is never expanded.
    auto expanded = expand_file_operand(value);
    cfg.input_file = expanded.empty() ? std::string(value) : expanded.front();
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
    cfg.bs_given = true;
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

  // [GNU] dd.c:1609-1610: without cbs= the record conversions block/unblock
  // (including the ones implied by ascii/ebcdic/ibm) are dropped silently.
  if (cfg.cbs == 0) {
    cfg.conv_block = false;
    cfg.conv_unblock = false;
  }

  // [GNU] dd.c:1665-1676: exclusivity is diagnosed only after every operand
  // has been parsed; error(), not usage(), so there is no Try-help line.
  const int charset_count = static_cast<int>(cfg.conv_ascii) +
                            static_cast<int>(cfg.conv_ebcdic) +
                            static_cast<int>(cfg.conv_ibm);
  if (charset_count > 1) {
    safeErrorPrintLn(winux::i18n::translate(
        "command.dd.error.cannot_combine_charset",
        "dd: cannot combine any two of {ascii,ebcdic,ibm}"));
    return false;
  }
  if (cfg.conv_block && cfg.conv_unblock) {
    safeErrorPrintLn(
        winux::i18n::translate("command.dd.error.cannot_combine_block_unblock",
                               "dd: cannot combine block and unblock"));
    return false;
  }
  if (cfg.conv_lcase && cfg.conv_ucase) {
    safeErrorPrintLn(
        winux::i18n::translate("command.dd.error.cannot_combine_lcase_ucase",
                               "dd: cannot combine lcase and ucase"));
    return false;
  }
  if (cfg.conv_excl && cfg.conv_nocreat) {
    safeErrorPrintLn(
        winux::i18n::translate("command.dd.error.cannot_combine_excl_nocreat",
                               "dd: cannot combine excl and nocreat"));
    return false;
  }
  if ((cfg.iflag_direct && cfg.iflag_nocache) ||
      (cfg.oflag_direct && cfg.oflag_nocache)) {
    safeErrorPrintLn(
        winux::i18n::translate("command.dd.error.cannot_combine_direct_nocache",
                               "dd: cannot combine direct and nocache"));
    return false;
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

// ---- RLIMIT_FSIZE enforcement (WinuxCmd issue #1453) --------------------
// Windows has no setrlimit()/RLIMIT_FSIZE, but a POSIX parent shell can
// still *declare* a file-size limit to this process through its
// environment. Two channels are honored, in precedence order:
//
//   WINUXCMD_RLIMIT_FSIZE=<bytes>
//       Canonical opt-in contract: the soft file-size limit in decimal
//       bytes. "unlimited" or an empty value disables enforcement; a
//       malformed value warns and disables it.
//   __RUBASH_ULIMIT_F=<soft>:<hard>
//       rubash's Windows ulimit emulation fork-inheritance channel
//       (rubash src/builtins/ulimit.rs: "<soft>:<hard>" decimal *bytes*,
//       18446744073709551615 = RLIM_INFINITY). The soft value is taken as
//       the RLIMIT_FSIZE soft limit, so `ulimit -f 0; exec dd ...` reaches
//       dd exactly like it reaches a Linux child.
//
// GNU truth (coreutils dd under setrlimit(RLIMIT_FSIZE), Linux): the
// write that would cross the limit is delivered only up to the limit and
// the process is then killed by SIGXFSZ (default disposition) - dd prints
// no diagnostic and no transfer summary, and the parent shell reports
// "File size limit exceeded" with $? = 128+25 = 153. (With SIGXFSZ
// blocked/ignored, write() instead fails EFBIG and dd prints
// "dd: error writing 'FILE': File size limit exceeded" + summary, exit 1;
// reproducing that shape needs the parent's trap state, which has no
// channel, so only the death shape is emitted.)
// Linux applies RLIMIT_FSIZE to regular-file writes only, so enforcement
// is gated on GetFileType(hOut) == FILE_TYPE_DISK.
inline constexpr std::uintmax_t kRlimInfinity =
    std::numeric_limits<std::uintmax_t>::max();
// SIGXFSZ death in shell exit-code terms: 128+25.
inline constexpr int kSigXfszExit = 128 + 25;

// Parse a strict decimal byte count; std::nullopt on any non-digit tail.
auto parse_decimal_bytes(std::string_view text) -> std::optional<uint64_t> {
  if (text.empty()) return std::nullopt;
  uint64_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// Resolve the declared soft file-size limit; "unlimited" when absent.
auto declared_fsize_limit() -> std::uintmax_t {
  if (const char* raw = std::getenv("WINUXCMD_RLIMIT_FSIZE")) {
    const std::string_view sv{raw};
    if (sv.empty() || sv == "unlimited") return kRlimInfinity;
    if (const auto parsed = parse_decimal_bytes(sv)) return *parsed;
    safeErrorPrintLn(winux::i18n::translate(
        "command.dd.warning.invalid_rlimit_fsize",
        "dd: warning: ignoring invalid WINUXCMD_RLIMIT_FSIZE value; file "
        "size limit not enforced"));
    return kRlimInfinity;
  }
  if (const char* raw = std::getenv("__RUBASH_ULIMIT_F")) {
    const std::string_view sv{raw};
    const auto colon = sv.find(':');
    if (colon != std::string_view::npos) {
      if (const auto soft = parse_decimal_bytes(sv.substr(0, colon))) {
        return *soft;  // the kRlimInfinity sentinel means unlimited
      }
    }
    // Third-party channel: an unparsable value is ignored silently.
  }
  return kRlimInfinity;
}

// Tracks the output file's write frontier against the declared cap and
// meters each pending write the way the kernel meters RLIMIT_FSIZE: bytes
// up to the limit are delivered, the crossing write is truncated there.
struct FsizeGuard {
  bool active = false;
  std::uintmax_t cap = 0;
  std::uintmax_t offset = 0;

  static auto open(HANDLE output, std::uintmax_t soft_limit) -> FsizeGuard {
    FsizeGuard guard;
    if (soft_limit == kRlimInfinity) return guard;
    if (GetFileType(output) != FILE_TYPE_DISK) return guard;
    LARGE_INTEGER pos{};
    if (!SetFilePointerEx(output, LARGE_INTEGER{}, &pos, FILE_CURRENT)) {
      return guard;
    }
    guard.active = true;
    guard.cap = soft_limit;
    guard.offset = static_cast<std::uintmax_t>(pos.QuadPart);
    return guard;
  }

  // How many bytes of `n` may still be written at the current offset;
  // advances the frontier. A short result is the SIGXFSZ crossing.
  [[nodiscard]] auto admit(std::uintmax_t n) -> std::uintmax_t {
    if (!active) return n;
    const std::uintmax_t remaining = cap > offset ? cap - offset : 0;
    const std::uintmax_t take = std::min(n, remaining);
    offset += take;
    return take;
  }
};

enum class FlushResult { ok, error, sigxfsz };

auto flush_output_buffer(HANDLE output, std::vector<char>& pending,
                         std::uintmax_t obs, CopyStats& stats, bool force,
                         FsizeGuard& fsize) -> FlushResult {
  while (pending.size() >= obs || (force && !pending.empty())) {
    size_t chunk_size =
        pending.size() >= obs ? static_cast<size_t>(obs) : pending.size();
    const std::uintmax_t allowed =
        fsize.admit(static_cast<std::uintmax_t>(chunk_size));
    if (allowed < static_cast<std::uintmax_t>(chunk_size)) {
      // RLIMIT_FSIZE crossing: deliver the partial prefix (kernel
      // behavior), then die by SIGXFSZ - silently, like GNU dd.
      if (allowed > 0 &&
          !write_all(output, pending.data(), static_cast<size_t>(allowed))) {
        return FlushResult::error;
      }
      return FlushResult::sigxfsz;
    }
    if (!write_all(output, pending.data(), chunk_size)) {
      return FlushResult::error;
    }
    // [GNU] write_output (dd.c:1258-1274): full obs blocks count as full
    // output records; a shorter final flush is the partial record.
    if (chunk_size == obs)
      ++stats.out_full;
    else
      ++stats.out_partial;
    stats.bytes_copied += chunk_size;
    pending.erase(pending.begin(), pending.begin() + chunk_size);
  }
  return FlushResult::ok;
}

// ---- GNU human_readable() port for the transfer summary ---------------
// dd.c:739-815 print_xfer_stats renders "<n> <unit>" sizes and rates with
// gnulib human.c (autoscale | round_to_nearest | space_before_unit | SI |
// B). The byte counts go through the exact-integer path (human.c:269-345),
// the rate through the floating-point autoscale path (human.c:227-263).

inline constexpr std::array<char, 11> kSiPowerLetters = {
    '\0', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y', 'R', 'Q'};

auto human_suffix(std::uintmax_t base, int exponent) -> std::string {
  if (exponent == 0) return " B";
  const char letter =
      (base == 1000 && exponent == 1) ? 'k' : kSiPowerLetters[exponent];
  if (base == 1024) {
    return std::string(" ") + letter + "iB";
  }
  return std::string(" ") + letter + 'B';
}

// Exact-integer autoscale (human.c use_integer_arithmetic branch): scale by
// repeated division, keep one decimal digit below 10 units, round to
// nearest.
auto human_int(std::uintmax_t n, std::uintmax_t base) -> std::string {
  constexpr int kExponentMax = 10;
  std::uintmax_t amt = n;
  unsigned tenths = 0;
  unsigned rounding = 0;
  int exponent = 0;
  while (base <= amt && exponent < kExponentMax) {
    const unsigned r10 = static_cast<unsigned>(amt % base) * 10 + tenths;
    const unsigned r2 = (r10 % base) * 2 + (rounding >> 1);
    amt /= base;
    tenths = r10 / base;
    rounding = (r2 < base ? static_cast<unsigned>((r2 + rounding) != 0)
                          : 2 + static_cast<unsigned>(base < r2 + rounding));
    ++exponent;
  }

  std::string number;
  bool decimal_emitted = false;
  if (amt < 10) {
    if (2u < rounding + (tenths & 1)) {
      ++tenths;
      rounding = 0;
      if (tenths == 10) {
        ++amt;
        tenths = 0;
      }
    }
    // human_suppress_point_zero is not in human_opts: "1.0 kB" keeps the
    // zero fraction.
    if (amt < 10) {
      number = std::to_string(amt) + "." + std::to_string(tenths);
      decimal_emitted = true;
      tenths = rounding = 0;
    }
  }
  if (!decimal_emitted) {
    if (5u < tenths + static_cast<unsigned>((rounding + (amt & 1)) > 0)) {
      ++amt;
      if (amt == base && exponent < kExponentMax) {
        ++exponent;
        return "1.0" + human_suffix(base, exponent);
      }
    }
    number = std::to_string(amt);
  }
  return number + human_suffix(base, exponent);
}

// Floating-point autoscale for the transfer rate (human.c:227-263): the
// exponent always advances at least once, so sub-kB/s rates still print
// "x.y kB/s"; the single decimal survives only below 100 units ("88.3 kB",
// but "885 kB").
auto human_rate(std::uintmax_t bytes, std::uint64_t delta_ns) -> std::string {
  if (delta_ns == 0) return "Infinity B/s";
  constexpr int kExponentMax = 10;
  const long double rate = static_cast<long double>(bytes) * 1.0e9L / delta_ns;
  long double scale = 1;
  int exponent = 0;
  do {
    scale *= 1000;
    ++exponent;
  } while (scale * 1000 <= rate && exponent < kExponentMax);
  const long double damt = rate / scale;

  char buf[64];
  std::snprintf(buf, sizeof buf, "%.1Lf", damt);
  std::string number = buf;
  if (number.size() > 4) {
    std::snprintf(buf, sizeof buf, "%.0Lf", damt);
    number = buf;
  }
  const char letter = exponent == 1 ? 'k' : kSiPowerLetters[exponent];
  return number + ' ' + letter + "B/s";
}

// [GNU] print_stats + print_xfer_stats (dd.c:739-847): "F+P records in",
// "F+P records out", optional truncated-record line, then
// "<n> bytes[(...)] copied, <secs> s, <rate>". Rates are masked by the
// differential harness, but the shape must match GNU byte for byte.
auto report_stats(const Config& cfg, const CopyStats& stats,
                  std::chrono::steady_clock::time_point copy_started) -> void {
  if (cfg.status_none) return;
  safeErrorPrintLn(
      std::format("{}+{} records in", stats.in_full, stats.in_partial));
  safeErrorPrintLn(
      std::format("{}+{} records out", stats.out_full, stats.out_partial));
  if (stats.truncated_records != 0) {
    // [GNU] ngettext singular/plural (dd.c:832-836).
    safeErrorPrintLn(
        stats.truncated_records == 1
            ? std::string("1 truncated record")
            : std::format("{} truncated records", stats.truncated_records));
  }
  if (cfg.status_noxfer) return;

  const auto now = std::chrono::steady_clock::now();
  const auto delta_ns = static_cast<std::uint64_t>(std::max<std::int64_t>(
      0,
      std::chrono::duration_cast<std::chrono::nanoseconds>(now - copy_started)
          .count()));
  const double delta_s = static_cast<double>(delta_ns) / 1.0e9;
  char delta_buf[40];
  std::snprintf(delta_buf, sizeof delta_buf, "%g s", delta_s);

  const std::uintmax_t n = stats.bytes_copied;
  std::string line;
  if (n < 1000) {
    // The SI abbreviation has no prefix ("8 B"), so the plain pluralized
    // form is used (dd.c:780-787).
    line = std::format("{} {} copied", n, n == 1 ? "byte" : "bytes");
  } else if (n < 1024) {
    // SI has a prefix but IEC does not (dd.c:788-791).
    line = std::format("{} bytes ({}) copied", n, human_int(n, 1000));
  } else {
    line = std::format("{} bytes ({}, {}) copied", n, human_int(n, 1000),
                       human_int(n, 1024));
  }
  std::format_to(std::back_inserter(line), ", {}, {}", delta_buf,
                 human_rate(n, delta_ns));
  safeErrorPrintLn(line);
}

// ---- conv= data conversions (dd.c:1692-1772, 1960-2050) -----------------

// The three POSIX 1003.1 charset tables, transcribed from dd.c:385-498.
static constexpr std::array<unsigned char, 256> kAsciiToEbcdic = {
    0x00, 0x01, 0x02, 0x03, 0x37, 0x2D, 0x2E, 0x2F, 0x16, 0x05, 0x25, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x3C, 0x3D, 0x32, 0x26,
    0x18, 0x19, 0x3F, 0x27, 0x1C, 0x1D, 0x1E, 0x1F, 0x40, 0x5A, 0x7F, 0x7B,
    0x5B, 0x6C, 0x50, 0x7D, 0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0x7A, 0x5E,
    0x4C, 0x7E, 0x6E, 0x6F, 0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xAD, 0xE0, 0xBD, 0x9A, 0x6D,
    0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x91, 0x92,
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xC0, 0x4F, 0xD0, 0x5F, 0x07, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x15, 0x06, 0x17, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x09, 0x0A, 0x1B,
    0x30, 0x31, 0x1A, 0x33, 0x34, 0x35, 0x36, 0x08, 0x38, 0x39, 0x3A, 0x3B,
    0x04, 0x14, 0x3E, 0xE1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x62, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x80, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x6A,
    0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xAA, 0xAB, 0xAC, 0x4A, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xA1, 0xBE, 0xBF, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0xDF, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xFA, 0xFB,
    0xFC, 0xFD, 0xFE, 0xFF};

static constexpr std::array<unsigned char, 256> kAsciiToIbm = {
    0x00, 0x01, 0x02, 0x03, 0x37, 0x2D, 0x2E, 0x2F, 0x16, 0x05, 0x25, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x3C, 0x3D, 0x32, 0x26,
    0x18, 0x19, 0x3F, 0x27, 0x1C, 0x1D, 0x1E, 0x1F, 0x40, 0x5A, 0x7F, 0x7B,
    0x5B, 0x6C, 0x50, 0x7D, 0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0x7A, 0x5E,
    0x4C, 0x7E, 0x6E, 0x6F, 0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xAD, 0xE0, 0xBD, 0x5F, 0x6D,
    0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x91, 0x92,
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xC0, 0x4F, 0xD0, 0xA1, 0x07, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x15, 0x06, 0x17, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x09, 0x0A, 0x1B,
    0x30, 0x31, 0x1A, 0x33, 0x34, 0x35, 0x36, 0x08, 0x38, 0x39, 0x3A, 0x3B,
    0x04, 0x14, 0x3E, 0xE1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x62, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x80, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x9A,
    0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xBD, 0xBE, 0xBF, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0xDF, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xFA, 0xFB,
    0xFC, 0xFD, 0xFE, 0xFF};

static constexpr std::array<unsigned char, 256> kEbcdicToAscii = {
    0x00, 0x01, 0x02, 0x03, 0x9C, 0x09, 0x86, 0x7F, 0x97, 0x8D, 0x8E, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x9D, 0x85, 0x08, 0x87,
    0x18, 0x19, 0x92, 0x8F, 0x1C, 0x1D, 0x1E, 0x1F, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x0A, 0x17, 0x1B, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x05, 0x06, 0x07,
    0x90, 0x91, 0x16, 0x93, 0x94, 0x95, 0x96, 0x04, 0x98, 0x99, 0x9A, 0x9B,
    0x14, 0x15, 0x9E, 0x1A, 0x20, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xD5, 0x2E, 0x3C, 0x28, 0x2B, 0x7C, 0x26, 0xA9, 0xAA, 0xAB,
    0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0x7E,
    0x2D, 0x2F, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xCB, 0x2C,
    0x25, 0x5F, 0x3E, 0x3F, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1,
    0xC2, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22, 0xC3, 0x61, 0x62, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
    0xCA, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x5E, 0xCC,
    0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xE5, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0xD2, 0xD3, 0xD4, 0x5B, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0x5D, 0xE6, 0xE7,
    0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0xE8, 0xE9,
    0xEA, 0xEB, 0xEC, 0xED, 0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
    0x51, 0x52, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0x5C, 0x9F, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xFA, 0xFB,
    0xFC, 0xFD, 0xFE, 0xFF};

// Stream state for the conversion pipeline; mirrors the file-scope state
// GNU keeps in trans_table/newline_character/saved_byte/col/pending_spaces.
struct ConvPlan {
  std::array<unsigned char, 256> trans_table{};
  bool translation_needed = false;
  char newline_character = '\n';
  char space_character = ' ';
  int saved_byte = -1;
  std::uintmax_t col = 0;
  std::uintmax_t pending_spaces = 0;
  std::vector<char> swab_buffer;
};

// [GNU] apply_translations (dd.c:1692-1727): ascii is applied first, then
// case folding, then ebcdic/ibm - the composition order matters when the
// flags are combined (e.g. conv=ascii,ucase).
auto build_conv_plan(const Config& cfg) -> ConvPlan {
  ConvPlan plan;
  for (int i = 0; i < 256; ++i) {
    plan.trans_table[i] = static_cast<unsigned char>(i);
  }
  auto translate_charset = [&](const std::array<unsigned char, 256>& map) {
    for (int i = 0; i < 256; ++i) {
      plan.trans_table[i] = map[plan.trans_table[i]];
    }
    plan.translation_needed = true;
  };
  if (cfg.conv_ascii) translate_charset(kEbcdicToAscii);
  if (cfg.conv_ucase) {
    for (auto& cell : plan.trans_table) {
      if (cell >= 'a' && cell <= 'z') cell = cell - 'a' + 'A';
    }
    plan.translation_needed = true;
  } else if (cfg.conv_lcase) {
    for (auto& cell : plan.trans_table) {
      if (cell >= 'A' && cell <= 'Z') cell = cell - 'A' + 'a';
    }
    plan.translation_needed = true;
  }
  if (cfg.conv_ebcdic) {
    translate_charset(kAsciiToEbcdic);
    plan.newline_character = static_cast<char>(kAsciiToEbcdic['\n']);
    plan.space_character = static_cast<char>(kAsciiToEbcdic[' ']);
  } else if (cfg.conv_ibm) {
    translate_charset(kAsciiToIbm);
    plan.newline_character = static_cast<char>(kAsciiToIbm['\n']);
    plan.space_character = static_cast<char>(kAsciiToIbm[' ']);
  }
  return plan;
}

// [GNU] swab_buffer (dd.c:1749-1772): swap input bytes pairwise, carrying
// a leftover byte across block boundaries. The result lives in
// plan.swab_buffer.
auto swab_block(const char* in, std::size_t n, ConvPlan& plan)
    -> std::span<const char> {
  auto& out = plan.swab_buffer;
  out.clear();
  if (n == 0) return out;
  const int prev = plan.saved_byte;
  std::size_t m = n;
  if ((prev < 0) == ((n & 1u) != 0u)) {
    plan.saved_byte = static_cast<unsigned char>(in[--m]);
  } else {
    plan.saved_byte = -1;
  }
  out.reserve(m + 1);
  if (prev < 0) {
    for (std::size_t i = 0; i < m; i += 2) {
      out.push_back(in[i + 1]);
      out.push_back(in[i]);
    }
  } else {
    out.push_back(in[0]);
    out.push_back(static_cast<char>(prev));
    for (std::size_t i = 2; i < m; i += 2) {
      out.push_back(in[i]);
      out.push_back(in[i - 1]);
    }
  }
  return out;
}

// [GNU] copy_with_block (dd.c:1984-2011): newline-terminated records are
// padded to cbs bytes with the space character; bytes beyond cbs count a
// truncated record once per overlong record.
auto copy_with_block(std::span<const char> data, ConvPlan& plan,
                     std::uintmax_t cbs, std::vector<char>& output,
                     CopyStats& stats) -> void {
  for (const char c : data) {
    if (c == plan.newline_character) {
      while (plan.col < cbs) {
        output.push_back(plan.space_character);
        ++plan.col;
      }
      plan.col = 0;
    } else {
      if (plan.col == cbs)
        ++stats.truncated_records;
      else if (plan.col < cbs)
        output.push_back(c);
      ++plan.col;
    }
  }
}

// [GNU] copy_with_unblock (dd.c:2014-2048): trailing spaces in each cbs
// record are replaced by a newline.
auto copy_with_unblock(std::span<const char> data, ConvPlan& plan,
                       std::uintmax_t cbs, std::vector<char>& output) -> void {
  for (std::size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];
    if (plan.col++ >= cbs) {
      plan.col = plan.pending_spaces = 0;
      --i;  // reprocess the char as the first of the next record
      output.push_back(plan.newline_character);
    } else if (c == plan.space_character) {
      ++plan.pending_spaces;
    } else {
      while (plan.pending_spaces != 0) {
        output.push_back(plan.space_character);
        --plan.pending_spaces;
      }
      output.push_back(c);
    }
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
                 "Copy a file, converting and formatting according to the "
                 "operands.\n"
                 "\n"
                 "  bs=BYTES        read and write up to BYTES bytes at a "
                 "time (default: 512);\n"
                 "                  overrides ibs and obs\n"
                 "  cbs=BYTES       convert BYTES bytes at a time\n"
                 "  conv=CONVS      convert the file as per the comma "
                 "separated symbol list\n"
                 "  count=N         copy only N input blocks\n"
                 "  ibs=BYTES       read up to BYTES bytes at a time "
                 "(default: 512)\n"
                 "  if=FILE         read from FILE instead of stdin\n"
                 "  iflag=FLAGS     read as per the comma separated symbol "
                 "list\n"
                 "  obs=BYTES       write BYTES bytes at a time (default: "
                 "512)\n"
                 "  of=FILE         write to FILE instead of stdout\n"
                 "  oflag=FLAGS     write as per the comma separated symbol "
                 "list\n"
                 "  seek=N          (or oseek=N) skip N obs-sized output "
                 "blocks\n"
                 "  skip=N          (or iseek=N) skip N ibs-sized input "
                 "blocks\n"
                 "  status=LEVEL    The LEVEL of information to print to "
                 "stderr;\n"
                 "                  'none' suppresses everything but error "
                 "messages,\n"
                 "                  'noxfer' suppresses the final transfer "
                 "statistics,\n"
                 "                  'progress' is accepted; final statistics "
                 "are printed\n"
                 "                  as the copy finishes\n"
                 "\n"
                 "N and BYTES may be followed by the following multiplicative "
                 "suffixes:\n"
                 "c=1, w=2, b=512, kB=1000, K=1024, MB=1000*1000, "
                 "M=1024*1024, xM=M,\n"
                 "GB=1000*1000*1000, G=1024*1024*1024, and so on for T, P, E, "
                 "Z, Y, R, Q.\n"
                 "Binary prefixes can be used, too: KiB=K, MiB=M, and so "
                 "on.\n"
                 "If N ends in 'B', it counts bytes not blocks.\n"
                 "\n"
                 "Each CONV symbol may be:\n"
                 "\n"
                 "  ascii     from EBCDIC to ASCII\n"
                 "  ebcdic    from ASCII to EBCDIC\n"
                 "  ibm       from ASCII to alternate EBCDIC\n"
                 "  block     pad newline-terminated records with spaces to "
                 "cbs-size\n"
                 "  unblock   replace trailing spaces in cbs-size records "
                 "with newline\n"
                 "  lcase     change upper case to lower case\n"
                 "  ucase     change lower case to upper case\n"
                 "  sparse    try to seek rather than write all-NUL output "
                 "blocks\n"
                 "  swab      swap every pair of input bytes\n"
                 "  sync      pad every input block with NULs to ibs-size; "
                 "when used\n"
                 "            with block or unblock, pad with spaces rather "
                 "than NULs\n"
                 "  excl      fail if the output file already exists\n"
                 "  nocreat   do not create the output file\n"
                 "  notrunc   do not truncate the output file\n"
                 "  noerror   continue after read errors\n"
                 "  fdatasync  physically write output file data before "
                 "finishing\n"
                 "  fsync     likewise, but also write metadata\n"
                 "\n"
                 "Each FLAG symbol may be (accepted for GNU compatibility; "
                 "on\n"
                 "Windows only fullblock and nonblock affect input "
                 "handling):\n"
                 "\n"
                 "  append    append mode (makes sense only for output; "
                 "conv=notrunc\n"
                 "            suggested)\n"
                 "  direct    use direct I/O for data\n"
                 "  directory  fail unless a directory\n"
                 "  dsync     use synchronized I/O for data\n"
                 "  sync      likewise, but also for metadata\n"
                 "  fullblock  accumulate full blocks of input (iflag only)\n"
                 "  nonblock  use non-blocking I/O\n"
                 "  noatime   do not update access time\n"
                 "  nocache   request to drop cache; see also oflag=sync\n"
                 "  noctty    do not assign controlling terminal from file\n"
                 "  nofollow  do not follow symlinks",

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

  // [GNU] start_time = gethrxtime() runs right before dd_copy (dd.c:2531),
  // which covers the skip/seek phase below.
  const auto copy_started = std::chrono::steady_clock::now();

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
      report_stats(cfg, CopyStats{}, copy_started);
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
            report_stats(cfg, CopyStats{}, copy_started);
            close_handles();
            return 1;
          }
          skip_short = true;
        } else if (!seek_handle(hIn, *skip_bytes)) {
          safeErrorPrintLn(winux::i18n::format(
              "command.dd.error.cannot_skip_errno", "dd: {}: cannot skip: {}",
              input_name(), win32_posix_error_text(GetLastError())));
          report_stats(cfg, CopyStats{}, copy_started);
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
          report_stats(cfg, CopyStats{}, copy_started);
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
        report_stats(cfg, CopyStats{}, copy_started);
        close_handles();
        return 1;
      }
    } else if (GetFileType(hOut) != FILE_TYPE_CHAR) {
      if (!seek_handle(hOut, *seek_bytes)) {
        safeErrorPrintLn(winux::i18n::format(
            "command.dd.error.cannot_seek_errno", "dd: {}: cannot seek: {}",
            output_name(), win32_posix_error_text(GetLastError())));
        report_stats(cfg, CopyStats{}, copy_started);
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
        report_stats(cfg, CopyStats{}, copy_started);
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
  // RLIMIT_FSIZE: meter writes against the declared soft limit (issue
  // #1453). Opened after the seek/truncate handling so the frontier starts
  // at the real output position.
  FsizeGuard fsize = output_sink == OutputSink::handle
                         ? FsizeGuard::open(hOut, declared_fsize_limit())
                         : FsizeGuard{};
  std::uintmax_t bytes_remaining =
      cfg.count_is_bytes ? cfg.count
                         : std::numeric_limits<std::uintmax_t>::max();
  std::random_device random_source;

  ConvPlan conv = build_conv_plan(cfg);
  // [GNU] the conv=sync pad byte is a space under block/unblock, NUL
  // otherwise (dd.c:2191-2193, 2245-2248); it is applied to the input
  // buffer BEFORE charset translation.
  const char pad_byte = (cfg.conv_block || cfg.conv_unblock) ? ' ' : '\0';
  const bool uses_two_bufs = !cfg.bs_given || cfg.conv_twobufs;
  // [GNU] warn_partial_read (dd.c:1650-1659): a partial read is diagnosed
  // once, on the read after it happened (dd.c:1133-1146), and only in the
  // one-buffer mode when counting/skipping or using direct I/O.
  bool warn_partial_read =
      !uses_two_bufs && !cfg.iflag_fullblock &&
      ((cfg.skip_is_bytes ? cfg.skip / cfg.ibs : cfg.skip) != 0 ||
       (cfg.count_set && cfg.count != 0) || cfg.iflag_direct ||
       cfg.oflag_direct);
  std::uintmax_t prev_read_size = 0;

  while (!cfg.count_set ||
         (cfg.count_is_bytes ? bytes_remaining > 0
                             : stats.in_full + stats.in_partial < cfg.count)) {
    std::size_t bytes_read = 0;
    DWORD request = static_cast<DWORD>(std::min<std::uintmax_t>(
        cfg.ibs,
        static_cast<std::uintmax_t>(std::numeric_limits<DWORD>::max())));
    if (cfg.count_is_bytes &&
        static_cast<std::uintmax_t>(request) > bytes_remaining) {
      request = static_cast<DWORD>(bytes_remaining);
    }
    bool read_error = false;
    bool recovery_seek_failed = false;
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
          std::size_t total = 0;
          DWORD native_bytes = 0;
          do {
            const bool ok =
                ReadFile(hIn, data + total, static_cast<DWORD>(amount - total),
                         &native_bytes, nullptr);
            if (!ok) {
              const DWORD err = GetLastError();
              // A pipe whose write end closed (MSYS/Git-Bash pipelines)
              // reports ERROR_BROKEN_PIPE; that is EOF, not a read failure.
              // Treat it like ReadFile's normal EOF (TRUE + 0 bytes) so
              // piped stdin terminates cleanly.
              if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) {
                got = total;
                read_error = false;
                return true;
              }
              read_error_text = win32_posix_error_text(err);
              read_error = true;
              return false;
            }
            total += native_bytes;
            // [GNU] iread_fullblock (dd.c:1155-1173) loops until the block
            // is full or EOF; a single read stops at the first result.
          } while (cfg.iflag_fullblock && native_bytes != 0 && total < amount);
          got = total;
          read_error = false;
          return true;
        },
        [&](std::size_t amount) { return seek_handle(hIn, amount); },
        cfg.noerror && !cfg.input_file.empty(), cfg.sync_blocks, pad_byte,
        request, input_buffer, bytes_read, recovery_seek_failed);
    if (read_error) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.read_named", "dd: error reading '{}': {}",
          cfg.input_given ? cfg.input_file : std::string("standard input"),
          read_error_text));
    }
    if (recovery_seek_failed) read_failed = true;
    if (action == ReadBlockAction::stop) {
      if (read_error && (!cfg.noerror || cfg.input_file.empty())) {
        read_failed = true;
      }
      break;
    }
    if (action == ReadBlockAction::skip) continue;

    std::size_t n_bytes;
    if (action == ReadBlockAction::pad) {
      // [GNU] a noerror+sync recovery counts as a partial input record
      // padded to ibs.
      ++stats.in_partial;
      n_bytes = static_cast<std::size_t>(cfg.ibs);
    } else {
      if (warn_partial_read && bytes_read > 0 && prev_read_size > 0 &&
          prev_read_size < request) {
        // [GNU] dd.c:1133-1146: diagnosed once, after the fact.
        if (!cfg.status_none) {
          safeErrorPrintLn(
              prev_read_size == 1
                  ? winux::i18n::translate(
                        "command.dd.warning.partial_read_one",
                        "dd: warning: partial read (1 byte); suggest "
                        "iflag=fullblock")
                  : winux::i18n::format(
                        "command.dd.warning.partial_read",
                        "dd: warning: partial read ({} bytes); suggest "
                        "iflag=fullblock",
                        prev_read_size));
        }
        warn_partial_read = false;
      }
      prev_read_size = read_error ? request : bytes_read;

      if (bytes_read == 0) break;
      if (cfg.count_is_bytes) {
        bytes_remaining -=
            std::min<std::uintmax_t>(bytes_remaining, bytes_read);
      }
      n_bytes = bytes_read;
      if (n_bytes < cfg.ibs) {
        ++stats.in_partial;
        if (cfg.sync_blocks) {
          // [GNU] conv=sync pads the input block to ibs (dd.c:2242-2248).
          std::fill(input_buffer.begin() + n_bytes, input_buffer.end(),
                    pad_byte);
          n_bytes = static_cast<std::size_t>(cfg.ibs);
        }
      } else {
        ++stats.in_full;
      }
    }

    // [GNU] conversions run on the whole input block in this order:
    // charset/case translation, then swab, then the record conversion
    // (dd.c:2290-2304).
    if (conv.translation_needed) {
      for (std::size_t i = 0; i < n_bytes; ++i) {
        input_buffer[i] = static_cast<char>(
            conv.trans_table[static_cast<unsigned char>(input_buffer[i])]);
      }
    }
    const std::span<const char> stage =
        cfg.conv_swab ? swab_block(input_buffer.data(), n_bytes, conv)
                      : std::span<const char>(input_buffer.data(), n_bytes);
    if (cfg.conv_block) {
      copy_with_block(stage, conv, cfg.cbs, output_buffer, stats);
    } else if (cfg.conv_unblock) {
      copy_with_unblock(stage, conv, cfg.cbs, output_buffer);
    } else {
      output_buffer.insert(output_buffer.end(), stage.begin(), stage.end());
    }

    if (output_sink == OutputSink::full) {
      // [GNU] writes to /dev/full fail ENOSPC.
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.write_named", "dd: error writing '{}': {}",
          cfg.output_file, std::string("No space left on device")));
      write_failed = true;
      break;
    }
    const FlushResult flushed_chunk =
        flush_output_buffer(hOut, output_buffer, cfg.obs, stats, false, fsize);
    if (flushed_chunk == FlushResult::sigxfsz) {
      // GNU dd dies by SIGXFSZ mid-copy: no diagnostic, no summary.
      close_handles();
      return kSigXfszExit;
    }
    if (flushed_chunk == FlushResult::error) {
      safeErrorPrintLn(winux::i18n::format(
          "command.dd.error.write_named", "dd: error writing '{}': {}",
          cfg.output_given ? cfg.output_file : std::string("standard output"),
          win32_posix_error_text(GetLastError())));
      write_failed = true;
      break;
    }
  }

  if (!write_failed) {
    // [GNU] epilogue (dd.c:2309-2334): flush a leftover conv=swab byte,
    // pad a trailing conv=block record, terminate a trailing conv=unblock
    // record.
    if (conv.saved_byte >= 0) {
      const char saved = static_cast<char>(conv.saved_byte);
      if (cfg.conv_block) {
        copy_with_block(std::span<const char>(&saved, 1), conv, cfg.cbs,
                        output_buffer, stats);
      } else if (cfg.conv_unblock) {
        copy_with_unblock(std::span<const char>(&saved, 1), conv, cfg.cbs,
                          output_buffer);
      } else {
        output_buffer.push_back(saved);
      }
    }
    if (cfg.conv_block && conv.col > 0) {
      while (conv.col < cfg.cbs) {
        output_buffer.push_back(conv.space_character);
        ++conv.col;
      }
    }
    if (conv.col != 0 && cfg.conv_unblock) {
      output_buffer.push_back(conv.newline_character);
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
      const FlushResult tail =
          flush_output_buffer(hOut, output_buffer, cfg.obs, stats, true, fsize);
      flushed = tail == FlushResult::ok;
      if (tail == FlushResult::sigxfsz) {
        // GNU dd dies by SIGXFSZ mid-copy: no diagnostic, no summary.
        close_handles();
        return kSigXfszExit;
      }
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
  report_stats(cfg, stats, copy_started);

  return flushed && !read_failed && !write_failed ? 0 : 1;
}
