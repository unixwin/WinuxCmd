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
#include <cwchar>   // For wprintf, fwprintf

#endif  // PCH_H
