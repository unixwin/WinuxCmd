// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for hostname.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <winsock2.h>

#include "core/command_macros.h"

#pragma comment(lib, "ws2_32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr HOSTNAME_OPTIONS = std::array{
    // [GNU] -b, --boot: set default hostname if none available
    OPTION("-b", "--boot", "set default hostname if none available", BOOL_TYPE),
    // [EXT]
    OPTION("-i", "--ip-address", "addresses for the hostname", BOOL_TYPE),
    // [EXT]
    OPTION("-I", "--all-ip-addresses", "all addresses for the hostname",
           BOOL_TYPE),
    // [EXT]
    OPTION("-s", "--short", "short host name", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-f", "--fqdn", "long host name (FQDN)", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-a", "--alias", "host alias names (not supported on Windows)",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-A", "--all-fqdns",
           "all FQDNs of the host (not supported on Windows)", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-d", "--domain", "DNS domain name", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-F", "--file", "read host name or NIS domain name from FILE",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-y", "--yp", "NIS/YP domain name (not supported on Windows)",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-n", "--node", "network node hostname (not supported on Windows)",
           BOOL_TYPE)};

namespace hostname_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool show_ip = false;
  bool show_all_ips = false;
  bool short_name = false;
  bool fqdn = false;
  bool domain = false;
  bool node = false;
  // -F/--file: [GNU] supported since hostname.c main() wires it through
  // read_file()/set_name() (issue #1526).  has_file distinguishes an
  // attached empty value ("--file=", fopen("") -> ENOENT) from the
  // option being absent.
  bool has_file = false;
  std::string file;
  // [GNU] hostname.c main(): a positional operand together with -F is
  // usage(stderr), exit(-1) -> 255.
  bool file_operand_conflict = false;
};

auto build_config(const CommandContext<HOSTNAME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  // [GNU] coreutils hostname only prints/sets the system name; the
  // net-tools option set (-i/-s/-f/-d/...) is rejected with an
  // "unknown option" error (uutils #8656).
  struct UnsupportedOption {
    std::string_view short_name;
    std::string_view long_name;
  };
  static constexpr UnsupportedOption unsupported[] = {
      {"-b", "--boot"},
      {"-i", "--ip-address"},
      {"-I", "--all-ip-addresses"},
      {"-s", "--short"},
      {"-f", "--fqdn"},
      {"-a", "--alias"},
      {"-A", "--all-fqdns"},
      {"-d", "--domain"},
      {"-y", "--yp"},
      {"-n", "--node"}};
  for (const auto& opt : unsupported) {
    if (!opt.short_name.empty() && ctx.has(std::string(opt.short_name))) {
      return std::unexpected("unknown option -- " +
                             std::string(opt.short_name.substr(1)) +
                             "\nTry 'hostname --help' for more information.");
    }
    if (ctx.has(std::string(opt.long_name))) {
      return std::unexpected("unrecognized option '" +
                             std::string(opt.long_name) +
                             "'\nTry 'hostname --help' for more information.");
    }
  }
  (void)cfg.show_ip;
  (void)cfg.show_all_ips;
  (void)cfg.short_name;
  (void)cfg.fqdn;
  (void)cfg.domain;
  (void)cfg.node;

  // [GNU] hostname.c main(): -F/--file is a required-argument option
  // handled through read_file() before any display path.
  if (ctx.has("-F") || ctx.has("--file")) {
    cfg.has_file = true;
    cfg.file = ctx.get<std::string>("-F", "");
    if (cfg.file.empty()) {
      cfg.file = ctx.get<std::string>("--file", "");
    }
  }

  return cfg;
}

namespace detail {

// [GNU] hostname.c check_name(): RFC 1035 2.3.1 - alphanumeric first
// and last, only alnum/'-'/'.' inside, no '.'/'-' adjacency pairs.
auto is_host_char(char c) -> bool {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z');
}

auto check_name(const std::string& name) -> bool {
  const size_t len = name.size();
  if (len == 0 || !is_host_char(name[0]) || !is_host_char(name[len - 1])) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const char c = name[i];
    if (!is_host_char(c) && c != '-' && c != '.') {
      return false;
    }
    if (c == '-' && ((i > 0 && name[i - 1] == '.') ||
                     (i + 1 < len && name[i + 1] == '.'))) {
      return false;
    }
    if (c == '.' && i > 0 && name[i - 1] == '.') {
      return false;
    }
  }
  return true;
}

auto is_name_space(char c) -> bool {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}

// [GNU] hostname.c usage(stderr) body, verbatim; exit(-1) -> 255.
constexpr std::string_view kDebianUsage =
    "Usage: hostname [-b] {hostname|-F file}         set host name (from "
    "file)\n"
    "       hostname [-a|-A|-d|-f|-i|-I|-s|-y]       display formatted name\n"
    "       hostname                                 display host name\n"
    "\n"
    "       {yp,nis,}domainname {nisdomain|-F file}  set NIS domain name "
    "(from file)\n"
    "       {yp,nis,}domainname                      display NIS domain name\n"
    "\n"
    "       dnsdomainname                            display dns domain name\n"
    "\n"
    "       hostname -V|--version|-h|--help          print info and exit\n"
    "\n"
    "Program name:\n"
    "       {yp,nis,}domainname=hostname -y\n"
    "       dnsdomainname=hostname -d\n"
    "\n"
    "Program options:\n"
    "    -a, --alias            alias names\n"
    "    -A, --all-fqdns        all long host names (FQDNs)\n"
    "    -b, --boot             set default hostname if none available\n"
    "    -d, --domain           DNS domain name\n"
    "    -f, --fqdn, --long     long host name (FQDN)\n"
    "    -F, --file             read host name or NIS domain name from given "
    "file\n"
    "    -i, --ip-address       addresses for the host name\n"
    "    -I, --all-ip-addresses all addresses for the host\n"
    "    -s, --short            short host name\n"
    "    -y, --yp, --nis        NIS/YP domain name\n"
    "\n"
    "Description:\n"
    "   This command can get or set the host name or the NIS domain name. You "
    "can\n"
    "   also get the DNS domain or the FQDN (fully qualified domain name).\n"
    "   Unless you are using bind or NIS for host lookups you can change the\n"
    "   FQDN (Fully Qualified Domain Name) and the DNS domain name (which is\n"
    "   part of the FQDN) in the /etc/hosts file.\n";

}  // namespace detail

auto run(const Config& cfg) -> int {
  // Initialize Winsock
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    cp::Result<int> result = std::unexpected("failed to initialize Winsock");
    cp::report_error(result, L"hostname");
    return 1;
  }

  // Get local hostname
  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    WSACleanup();
    cp::Result<int> result = std::unexpected("failed to get hostname");
    cp::report_error(result, L"hostname");
    return 1;
  }

  if (cfg.has_file) {
    // [GNU] hostname.c read_file(): a failed fopen is err(1, NULL) ->
    // "hostname: <strerror>" on stderr, exit 1.  #1522/#1528: open
    // through the wide path operand; the narrow ctor decoded the UTF-8
    // name through the ANSI code page (the #342 class).  The stream
    // stays in TEXT mode: a translated CRLF only turns '\r' into a
    // trailing-isspace byte, which set_name()'s trim absorbs, so the
    // semantics are byte-identical to GNU's fgets view of the file.
    // #1526: -F was previously rejected as an unsupported option.
    auto operand = native_path::make_api_path_operand(cfg.file);
    std::ifstream file(std::filesystem::path(operand.extended));
    if (!file) {
      WSACleanup();
      cp::Result<int> result = std::unexpected(
          portable_digest::open_error_reason(cfg.file));
      cp::report_error(result, L"hostname");
      return 1;
    }
    // [GNU] read_file(): take the first line that is neither blank
    // (buf[0] == '\n') nor starts with '#', stripping only the trailing
    // newline (so a carriage return survives into set_name()'s trim,
    // exactly like GNU's fgets view).  A file with no usable line leaves
    // the calloc'd empty buffer, i.e. an empty name.
    std::string name;
    bool have_line = false;
    std::string line;
    while (std::getline(file, line)) {
      if (line.empty() || line.front() == '#') {
        continue;
      }
      name = line;
      have_line = true;
      break;
    }

    // [GNU] hostname.c main(): the operand conflict (usage(stderr),
    // exit(-1) -> 255) is decided before set_name() validates anything.
    if (cfg.file_operand_conflict) {
      WSACleanup();
      safeErrorPrint(std::string(detail::kDebianUsage));
      return 255;
    }

    // [GNU] set_name(DEFAULT): strip leading/trailing isspace, then
    // check_name(); an invalid name is errx(1, "the specified hostname
    // is invalid") -> "hostname: ..." exit 1.
    size_t begin = 0;
    while (begin < name.size() && detail::is_name_space(name[begin])) {
      ++begin;
    }
    size_t end = name.size();
    while (end > begin && detail::is_name_space(name[end - 1])) {
      --end;
    }
    name = name.substr(begin, end - begin);
    if (!detail::check_name(name)) {
      WSACleanup();
      cp::Result<int> result =
          std::unexpected("the specified hostname is invalid");
      cp::report_error(result, L"hostname");
      return 1;
    }
    // [GNU] sethostname() rejects len > HOST_NAME_MAX (64 on Linux)
    // with EINVAL, reported as errx(1, "name too long").
    if (name.size() > 64) {
      WSACleanup();
      cp::Result<int> result = std::unexpected("name too long");
      cp::report_error(result, L"hostname");
      return 1;
    }
    // [DIFFERS] GNU calls sethostname() here, silent on success.  On
    // Windows the host name is a reboot-scoped, administrative API
    // (SetComputerNameEx), so this build accepts the file, validates it
    // with GNU's full read_file/set_name/check_name pipeline, and
    // completes with the GNU success status without changing the host.
    WSACleanup();
    return 0;
  }

  if (cfg.show_ip || cfg.show_all_ips) {
    // Get host info
    hostent* host_info = gethostbyname(hostname);
    if (!host_info) {
      WSACleanup();
      cp::Result<int> result = std::unexpected("failed to get host info");
      cp::report_error(result, L"hostname");
      return 1;
    }

    // Print IP addresses
    for (int i = 0; host_info->h_addr_list[i] != nullptr; ++i) {
      in_addr* addr = reinterpret_cast<in_addr*>(host_info->h_addr_list[i]);
      char* ip = inet_ntoa(*addr);
      safePrintLn(ip);
      if (!cfg.show_all_ips) {
        break;  // Only show first IP address
      }
    }
  } else if (cfg.domain) {
    // Print DNS domain name
    std::string host_str = hostname;
    size_t dot_pos = host_str.find('.');
    if (dot_pos != std::string::npos) {
      safePrintLn(host_str.substr(dot_pos + 1));
    } else {
      safePrintLn("(none)");
    }
  } else if (cfg.fqdn) {
    safePrintLn(hostname);
  } else if (cfg.node) {
    // [DIFFERS] -n/--node: on Windows, node hostname is the same as hostname
    safePrintLn(hostname);
  } else if (cfg.short_name) {
    // Print only the short name (first part before dot)
    std::string short_name = hostname;
    size_t dot_pos = short_name.find('.');
    if (dot_pos != std::string::npos) {
      short_name = short_name.substr(0, dot_pos);
    }
    safePrintLn(short_name);
  } else {
    // Print full hostname
    safePrintLn(hostname);
  }

  WSACleanup();
  return 0;
}

}  // namespace hostname_pipeline

REGISTER_COMMAND(hostname, "hostname", "hostname [OPTION]...",
                 "Print or set system name.\n"
                 "\n"
                 "Print the name of the current host.\n"
                 "\n"
                 "Note: This implementation only supports printing hostname.\n"
                 "Setting hostname is not implemented on Windows.",
                 "  hostname\n"
                 "  hostname -i\n"
                 "  hostname -I\n"
                 "  hostname -s\n"
                 "  hostname -f",
                 "dnsdomainname(1), ypdomainname(1), nisdomainname(1)",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", HOSTNAME_OPTIONS) {
  using namespace hostname_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"hostname");
    return 1;
  }

  // [GNU] hostname.c main(): with -F the file is opened and its operand
  // conflict decided before any display path or name validation, so the
  // whole invocation routes through the file branch (issue #1526).
  if (cfg_result->has_file) {
    cfg_result->file_operand_conflict = !ctx.positionals.empty();
    return run(*cfg_result);
  }

  // [GNU] hostname.c: one operand SETS the host name (hostname.c:99
  // fails with "cannot set name to %s" when sethostname fails); a
  // second operand is an extra-operand error (hostname.c:91). The
  // applet silently ignored operands and printed the current name.
  if (ctx.positionals.size() > 1) {
    return cp::report_extra_operand("hostname", ctx.positionals[1]);
  }
  if (ctx.positionals.size() == 1) {
    // [DIFFERS] Windows has no sethostname equivalent for this
    // process; setting the computer name is an administrative,
    // reboot-scoped operation, so the GNU failure is reported.
    cp::report_custom_error(
        L"hostname",
        utf8_to_wstring("cannot set name to '" +
                        std::string(ctx.positionals.front()) + "'"));
    return 1;
  }

  return run(*cfg_result);
}
