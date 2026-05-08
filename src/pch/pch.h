/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: pch.h
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/**
 *@breif PreCompile headers to speed up compilation.
 *@note Pch doesn't affect the final executable size.
 */

#ifndef PCH_H
#pragma warning(disable : 4530)
#pragma warning(disable : 4541)  // Disable typeid warning with /GR-
#pragma warning(disable : 4129)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>   // For basic windows functions
#include <winsock2.h>  // Must be before windows.h to avoid conflicts
// Include these headers after windows.h
// Fuck clang-format.
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
