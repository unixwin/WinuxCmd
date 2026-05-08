/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: hostid.cpp
 *  - CopyrightYear: 2026
 */
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
