// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for hostname.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

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
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/hostname.rs, hostname_cmd); the C++ side
  // keeps only this registration — the Winsock/WSAStartup lookup, the
  // #1526 -F/--file read_file/set_name/check_name pipeline and the
  // Debian usage table are deleted. ctx.raw_args passes the operands
  // verbatim because hostname.c main() parses its own option run
  // (net-tools rejection, -F operand conflict, set-operand errors).
  return winux::rust2::run_command("hostname", ctx.raw_args);
}
