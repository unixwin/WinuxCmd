// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for hostid command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr HOSTID_OPTIONS =
    // [DIFFERS]
    std::array{OPTION("", "", "print machine identifier", STRING_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Get machine GUID from registry
std::string get_machine_guid() {
  HKEY hKey;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                    KEY_READ, &hKey) != ERROR_SUCCESS) {
    return "0";
  }

  wchar_t buffer[256];
  DWORD size = sizeof(buffer);
  if (RegGetValueW(hKey, nullptr, L"MachineGuid", RRF_RT_REG_SZ, nullptr,
                   buffer, &size) != ERROR_SUCCESS) {
    RegCloseKey(hKey);
    return "0";
  }

  RegCloseKey(hKey);

  // Convert GUID to numeric ID
  std::string guid = wstring_to_utf8(buffer);
  std::hash<std::string> hasher;
  size_t hash = hasher(guid);

  // Take lower 32 bits and convert to hex string
  unsigned int id = static_cast<unsigned int>(hash & 0xFFFFFFFF);
  char hex[16];
  sprintf_s(hex, sizeof(hex), "%08x", id);

  return hex;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    hostid,
    /* cmd_name */ "hostid",
    /* cmd_synopsis */ "hostid [OPTION]",
    /* cmd_desc */
    "Print the numeric identifier of the current host.\n"
    "Print a 32-bit identifier, in hexadecimal, for the current host.\n"
    "On Windows, this is derived from the machine GUID.",
    /* examples */
    "  hostid\n"
    "  hostid | xxd -r -p",
    /* see_also */ "hostname, whoami",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ HOSTID_OPTIONS) {
  std::string hex_id = get_machine_guid();

  safePrintLn(hex_id);

  return 0;
}
