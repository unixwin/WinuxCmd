// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/**
 *@breif PreCompile headers to speed up compilation.
 *@note Pch doesn't affect the final executable size.
 */

#ifndef PCH_H
#define PCH_H
#pragma warning(disable : 4530)
#pragma warning(disable : 4541)  // Disable typeid warning with /GR-
#pragma warning(disable : 4129)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT
#define _CRT_SECURE_NO_WARNINGS
// clang-format off: winsock2.h must be included before windows.h.
#include <winsock2.h>  // Must be before windows.h to avoid conflicts
#include <windows.h>  // For basic windows functions
// clang-format on
// Include these headers after windows.h
#include <fcntl.h>       // For _setmode
#include <fileapi.h>     // For FindFirstFileW, FindNextFileW
#include <handleapi.h>   // For GetStdHandle, INVALID_HANDLE_VALUE
#include <io.h>          // For _get_osfhandle
#include <lmcons.h>      // For UNLEN
#include <psapi.h>       // For GetProcessMemoryInfo
#include <sddl.h>        // For ConvertSidToStringSidW
#include <shlwapi.h>     // For PathFileExistsW
#include <sysinfoapi.h>  // For GetUserNameW
#include <tlhelp32.h>    // For CreateToolhelp32Snapshot, Process32First
#include <winternl.h>    // For PROCESS_BASIC_INFORMATION

#include <cctype>   // For isspace
#include <cstdint>  // For uint64_t
// #include <cstdio>   // For printf, fflush
#include <cstdlib>  // For basic functions
#include <cstring>  // For strlen
#include <ctime>    // For localtime_s, strftime, std::time_t. MSVC 14.51 no
                   // longer reaches <time.h> transitively through the
                   // windows.h chain, and several commands (cpio, logger,
                   // pinky, ...) rely on it.
#include <cwchar>   // For wprintf, fwprintf
// std::string_view for headers that spell it in their signatures
// (terminfo_table.h). Kept here, not in the header: in the unity build the
// header is reached AFTER an earlier command's 'import std;', and a real
// std header included past an import is a redefinition error (MSVC 14.51
// unity TU: iosfwd/__msvc_string_view.hpp C2953/C2995). The PCH include
// path runs before every import.
#include <string_view>

#endif  // PCH_H
