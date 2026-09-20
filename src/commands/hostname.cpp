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
  std::string file;
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
      {"-F", "--file"},
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

  return cfg;
}

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

  if (!cfg.file.empty()) {
    // Read hostname from file
    std::ifstream ifs(cfg.file);
    if (!ifs) {
      WSACleanup();
      cp::Result<int> result =
          std::unexpected("cannot open file '" + cfg.file + "'");
      cp::report_error(result, L"hostname");
      return 1;
    }
    std::string line;
    if (std::getline(ifs, line)) {
      safePrintLn(line);
    }
  } else if (cfg.show_ip || cfg.show_all_ips) {
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

  return run(*cfg_result);
}
