/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: hostname.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for hostname.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <winsock.h>
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
    OPTION("-i", "--ip-address", "addresses for the hostname", BOOL_TYPE),
    OPTION("-I", "--all-ip-addresses", "all addresses for the hostname",
           BOOL_TYPE),
    OPTION("-s", "--short", "short host name", BOOL_TYPE),
    OPTION("-f", "--fqdn", "long host name (FQDN)", BOOL_TYPE)
    // -a, --alias (not implemented)
    // -A, --all-fqdns (not implemented)
    // -d, --domain (not implemented)
    // -y, --yp (not implemented)
    // -n, --node (not implemented)
};

namespace hostname_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool show_ip = false;
  bool show_all_ips = false;
  bool short_name = false;
  bool fqdn = false;
};

auto build_config(const CommandContext<HOSTNAME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.show_ip =
      ctx.get<bool>("--ip-address", false) || ctx.get<bool>("-i", false);
  cfg.show_all_ips =
      ctx.get<bool>("--all-ip-addresses", false) || ctx.get<bool>("-I", false);
  cfg.short_name =
      ctx.get<bool>("--short", false) || ctx.get<bool>("-s", false);
  cfg.fqdn = ctx.get<bool>("--fqdn", false) || ctx.get<bool>("-f", false);
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
  } else if (cfg.fqdn) {
    // Try to get FQDN (Windows doesn't provide this directly)
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
