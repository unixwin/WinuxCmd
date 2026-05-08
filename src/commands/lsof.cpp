/*
 *  Copyright (c) 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: lsof.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 *
 */
/// @contributors:
///   - @contributor1 arookieofc 2128194521@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for lsof.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <iphlpapi.h>

#include "core/command_macros.h"
#include "pch/pch.h"
#pragma comment(lib, "iphlpapi.lib")

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LSOF_OPTIONS = std::array{
    OPTION("-p", "--pid", "list files opened by process id", INT_TYPE),
    OPTION("-a", "--all", "include non-file handles and unnamed entries"),
    OPTION("-i", "--internet",
           "show internet connections; optional spec ':PORT', 'PROTO', "
           "'PROTO:PORT'"),
    OPTION("-F", "--field", "machine-readable field output"),
    OPTION("-n", "--numeric", "do not convert native paths to DOS paths"),
    OPTION("", "--no-headers", "print no header line"),
    OPTION("-t", "--timeout-ms", "NtQueryObject timeout in milliseconds",
           INT_TYPE)};

namespace lsof_pipeline {

constexpr NTSTATUS STATUS_INFO_LENGTH_MISMATCH_NT =
    static_cast<NTSTATUS>(0xC0000004L);
constexpr NTSTATUS STATUS_BUFFER_OVERFLOW_NT =
    static_cast<NTSTATUS>(0x80000005L);
constexpr ULONG SYSTEM_EXTENDED_HANDLE_INFORMATION_CLASS = 64;
constexpr ULONG OBJECT_NAME_INFORMATION_CLASS = 1;
constexpr ULONG OBJECT_TYPE_INFORMATION_CLASS = 2;
constexpr ULONG IPV4_FAMILY = 2;
constexpr ULONG IPV6_FAMILY = 23;

auto nt_success(NTSTATUS status) -> bool { return status >= 0; }

struct HandleCloser {
  using pointer = HANDLE;
  void operator()(HANDLE h) const noexcept {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
};
using unique_handle = std::unique_ptr<HANDLE, HandleCloser>;

using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG,
                                                    PULONG);
using NtQueryObjectFn = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL {
  PVOID Object;
  ULONG_PTR UniqueProcessId;
  ULONG_PTR HandleValue;
  ULONG GrantedAccess;
  USHORT CreatorBackTraceIndex;
  USHORT ObjectTypeIndex;
  ULONG HandleAttributes;
  ULONG Reserved;
};

struct SYSTEM_HANDLE_INFORMATION_EX_LOCAL {
  ULONG_PTR NumberOfHandles;
  ULONG_PTR Reserved;
  SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL Handles[1];
};

struct MIB_TCP6ROW_OWNER_PID_LOCAL {
  UCHAR ucLocalAddr[16];
  DWORD dwLocalScopeId;
  DWORD dwLocalPort;
  UCHAR ucRemoteAddr[16];
  DWORD dwRemoteScopeId;
  DWORD dwRemotePort;
  DWORD dwState;
  DWORD dwOwningPid;
};

struct MIB_TCP6TABLE_OWNER_PID_LOCAL {
  DWORD dwNumEntries;
  MIB_TCP6ROW_OWNER_PID_LOCAL table[1];
};

struct MIB_UDP6ROW_OWNER_PID_LOCAL {
  UCHAR ucLocalAddr[16];
  DWORD dwLocalScopeId;
  DWORD dwLocalPort;
  DWORD dwOwningPid;
};

struct MIB_UDP6TABLE_OWNER_PID_LOCAL {
  DWORD dwNumEntries;
  MIB_UDP6ROW_OWNER_PID_LOCAL table[1];
};

struct OBJECT_TYPE_INFORMATION_MIN {
  UNICODE_STRING TypeName;
};

struct QueryCtx {
  HANDLE dup_handle = nullptr;
  NtQueryObjectFn query_fn = nullptr;
  ULONG info_class = 0;
  std::wstring text;
  bool success = false;
};

struct QueryResult {
  enum class Status { Ok, Timeout, Failed };
  Status status = Status::Failed;
  std::wstring text;
};

struct LsofEntry {
  std::wstring command;
  DWORD pid = 0;
  std::wstring type;
  std::wstring name;
};

struct InternetFilter {
  std::optional<unsigned short> port;
  std::optional<std::wstring> protocol;  // "TCP", "UDP", "TCP6", "UDP6"
};

struct Config {
  std::optional<DWORD> pid_filter;
  bool show_all = false;
  bool internet_only = false;
  bool field_mode = false;
  bool numeric = false;
  bool no_headers = false;
  DWORD timeout_ms = 80;
  std::optional<InternetFilter> inet_filter;
};

auto enable_debug_privilege() -> bool {
  HANDLE token_raw = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_raw)) {
    return false;
  }
  unique_handle token(token_raw);

  TOKEN_PRIVILEGES tp{};
  LUID luid{};
  if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
    return false;
  }

  tp.PrivilegeCount = 1;
  tp.Privileges[0].Luid = luid;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  if (!AdjustTokenPrivileges(token.get(), FALSE, &tp, sizeof(tp), nullptr,
                             nullptr)) {
    return false;
  }

  return GetLastError() == ERROR_SUCCESS;
}

auto load_ntdll_functions()
    -> std::optional<std::pair<NtQuerySystemInformationFn, NtQueryObjectFn>> {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return std::nullopt;

  auto query_sys = reinterpret_cast<NtQuerySystemInformationFn>(
      GetProcAddress(ntdll, "NtQuerySystemInformation"));
  auto query_obj =
      reinterpret_cast<NtQueryObjectFn>(GetProcAddress(ntdll, "NtQueryObject"));
  if (!query_sys || !query_obj) return std::nullopt;
  return std::make_pair(query_sys, query_obj);
}

auto query_system_handles(NtQuerySystemInformationFn query_sys)
    -> std::optional<std::vector<std::byte>> {
  ULONG size = 1UL << 20;  // 1MB initial
  for (int i = 0; i < 10; ++i) {
    std::vector<std::byte> buffer(size);
    ULONG return_len = 0;
    NTSTATUS st = query_sys(SYSTEM_EXTENDED_HANDLE_INFORMATION_CLASS,
                            buffer.data(), size, &return_len);
    if (nt_success(st)) {
      return buffer;
    }
    if (st != STATUS_INFO_LENGTH_MISMATCH_NT &&
        st != STATUS_BUFFER_OVERFLOW_NT) {
      return std::nullopt;
    }
    size = return_len ? (return_len + (1UL << 16)) : (size * 2);
  }
  return std::nullopt;
}

auto build_process_name_map() -> std::unordered_map<DWORD, std::wstring> {
  std::unordered_map<DWORD, std::wstring> names;
  names.reserve(4096);

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return names;
  unique_handle snap_holder(snap);

  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (!Process32FirstW(snap_holder.get(), &pe)) return names;

  do {
    names.emplace(pe.th32ProcessID, pe.szExeFile);
  } while (Process32NextW(snap_holder.get(), &pe));

  return names;
}

auto build_dos_device_map()
    -> std::vector<std::pair<std::wstring, std::wstring>> {
  std::vector<std::pair<std::wstring, std::wstring>> map;
  map.reserve(26);

  wchar_t device_target[2048] = {};
  for (wchar_t drive = L'A'; drive <= L'Z'; ++drive) {
    std::wstring drive_name;
    drive_name.push_back(drive);
    drive_name.push_back(L':');

    DWORD len = QueryDosDeviceW(drive_name.c_str(), device_target,
                                static_cast<DWORD>(std::size(device_target)));
    if (len == 0) continue;

    std::wstring nt_path(device_target);
    std::wstring dos_prefix = drive_name;
    map.emplace_back(std::move(nt_path), std::move(dos_prefix));
  }
  return map;
}

auto starts_with(const std::wstring& text, const std::wstring& prefix) -> bool {
  if (text.size() < prefix.size()) return false;
  return std::equal(prefix.begin(), prefix.end(), text.begin());
}

auto normalize_path(
    std::wstring path, bool numeric,
    const std::vector<std::pair<std::wstring, std::wstring>>& dos_map)
    -> std::wstring {
  if (numeric) return path;

  if (starts_with(path, L"\\??\\")) {
    path.erase(0, 4);
  }

  for (const auto& [nt_prefix, dos_prefix] : dos_map) {
    if (starts_with(path, nt_prefix)) {
      return dos_prefix + path.substr(nt_prefix.size());
    }
  }

  constexpr std::wstring_view mup_prefix = L"\\Device\\Mup\\";
  if (path.size() >= mup_prefix.size() &&
      std::equal(mup_prefix.begin(), mup_prefix.end(), path.begin())) {
    return L"\\\\" + path.substr(mup_prefix.size());
  }

  return path;
}

DWORD WINAPI nt_query_object_worker(LPVOID param) {
  auto* ctx = static_cast<QueryCtx*>(param);
  if (!ctx || !ctx->query_fn || !ctx->dup_handle) return 1;

  ULONG need = 0;
  ULONG size = 2048;
  std::vector<std::byte> buffer(size);

  for (int i = 0; i < 8; ++i) {
    NTSTATUS st = ctx->query_fn(ctx->dup_handle, ctx->info_class, buffer.data(),
                                size, &need);
    if (st == STATUS_INFO_LENGTH_MISMATCH_NT ||
        st == STATUS_BUFFER_OVERFLOW_NT) {
      size = need ? (need + 512) : (size * 2);
      buffer.resize(size);
      continue;
    }

    if (!nt_success(st)) {
      ctx->success = false;
      return 2;
    }

    UNICODE_STRING* u = nullptr;
    if (ctx->info_class == OBJECT_NAME_INFORMATION_CLASS) {
      u = reinterpret_cast<UNICODE_STRING*>(buffer.data());
    } else if (ctx->info_class == OBJECT_TYPE_INFORMATION_CLASS) {
      auto* t = reinterpret_cast<OBJECT_TYPE_INFORMATION_MIN*>(buffer.data());
      u = &t->TypeName;
    } else {
      ctx->success = false;
      return 3;
    }

    if (u && u->Buffer && u->Length > 0) {
      size_t chars = static_cast<size_t>(u->Length) / sizeof(wchar_t);
      ctx->text.assign(u->Buffer, chars);
    } else {
      ctx->text.clear();
    }
    ctx->success = true;
    return 0;
  }

  ctx->success = false;
  return 4;
}

auto query_object_text_with_timeout(HANDLE dup_handle,
                                    NtQueryObjectFn query_obj, ULONG info_class,
                                    DWORD timeout_ms) -> QueryResult {
  QueryCtx ctx;
  ctx.dup_handle = dup_handle;
  ctx.query_fn = query_obj;
  ctx.info_class = info_class;

  HANDLE thread =
      CreateThread(nullptr, 0, nt_query_object_worker, &ctx, 0, nullptr);
  if (!thread) {
    return {QueryResult::Status::Failed, L""};
  }

  DWORD wait_rc = WaitForSingleObject(thread, timeout_ms);
  if (wait_rc == WAIT_TIMEOUT) {
    TerminateThread(thread, 1);
    CloseHandle(thread);
    return {QueryResult::Status::Timeout, L""};
  }

  CloseHandle(thread);
  if (!ctx.success) {
    return {QueryResult::Status::Failed, L""};
  }
  return {QueryResult::Status::Ok, std::move(ctx.text)};
}

// Parse the argument to -i: ":PORT"  |  "PROTO"  |  "PROTO:PORT"
auto parse_inet_spec(std::string_view spec) -> InternetFilter {
  InternetFilter f;
  if (spec.empty()) return f;

  std::wstring ws;
  ws.reserve(spec.size());
  for (char c : spec)
    ws += static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(c)));

  if (ws.front() == L':') {
    // ":PORT" form
    std::wstring port_str = ws.substr(1);
    if (!port_str.empty()) {
      try {
        int v = std::stoi(std::string(port_str.begin(), port_str.end()));
        if (v > 0 && v <= 65535) f.port = static_cast<unsigned short>(v);
      } catch (...) {
      }
    }
  } else {
    auto colon = ws.find(L':');
    if (colon == std::wstring::npos) {
      f.protocol = ws;  // just "TCP", "UDP", etc.
    } else {
      f.protocol = ws.substr(0, colon);
      std::wstring port_str = ws.substr(ws.rfind(L':') + 1);
      if (!port_str.empty()) {
        try {
          int v = std::stoi(std::string(port_str.begin(), port_str.end()));
          if (v > 0 && v <= 65535) f.port = static_cast<unsigned short>(v);
        } catch (...) {
        }
      }
    }
  }
  return f;
}

auto parse_config(const CommandContext<LSOF_OPTIONS.size()>& ctx)
    -> std::optional<Config> {
  Config cfg;
  int pid_opt = ctx.get<int>("--pid", -1);
  if (pid_opt < 0) pid_opt = ctx.get<int>("-p", -1);
  if (pid_opt >= 0) cfg.pid_filter = static_cast<DWORD>(pid_opt);

  cfg.show_all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  cfg.internet_only =
      ctx.get<bool>("--internet", false) || ctx.get<bool>("-i", false);
  cfg.field_mode =
      ctx.get<bool>("--field", false) || ctx.get<bool>("-F", false);
  cfg.numeric = ctx.get<bool>("--numeric", false) || ctx.get<bool>("-n", false);
  cfg.no_headers = ctx.get<bool>("--no-headers", false);

  int timeout_opt = ctx.get<int>("--timeout-ms", -1);
  if (timeout_opt < 0) timeout_opt = ctx.get<int>("-t", -1);
  if (timeout_opt > 0) cfg.timeout_ms = static_cast<DWORD>(timeout_opt);

  // Parse inet spec from first positional when -i is set.
  // Examples:  lsof -i :8080   lsof -i tcp   lsof -i tcp:8080
  if (cfg.internet_only && !ctx.positionals.empty()) {
    std::string_view spec = ctx.positionals[0];
    if (!spec.empty() &&
        (spec.front() == ':' ||
         std::isalpha(static_cast<unsigned char>(spec.front())))) {
      cfg.inet_filter = parse_inet_spec(spec);
    }
  }

  return cfg;
}

auto is_internet_like_name(const std::wstring& object_name) -> bool {
  constexpr std::wstring_view prefixes[] = {
      L"\\Device\\Afd", L"\\Device\\Tcp", L"\\Device\\Udp", L"\\Device\\RawIp",
      L"\\Device\\Ip"};
  for (const auto& p : prefixes) {
    if (object_name.size() >= p.size() &&
        std::equal(p.begin(), p.end(), object_name.begin())) {
      return true;
    }
  }
  return false;
}

auto format_row(const LsofEntry& e) -> std::wstring {
  std::wostringstream oss;
  oss << std::left << std::setw(24) << e.command.substr(0, 24) << std::right
      << std::setw(8) << e.pid << L" " << std::left << std::setw(8)
      << e.type.substr(0, 8) << L" " << e.name;
  return oss.str();
}

auto emit_field_row(const LsofEntry& e) -> void {
  safePrint(L"p");
  safePrint(std::to_wstring(e.pid));
  safePrint(L"\n");
  safePrint(L"c");
  safePrint(e.command);
  safePrint(L"\n");
  safePrint(L"t");
  safePrint(e.type);
  safePrint(L"\n");
  safePrint(L"n");
  safePrint(e.name);
  safePrint(L"\n");
  safePrint(L"\n");
}

auto parse_port(DWORD be_port) -> unsigned short {
  return static_cast<unsigned short>(((be_port & 0xFF) << 8) |
                                     ((be_port >> 8) & 0xFF));
}

auto format_ipv4(DWORD addr) -> std::wstring {
  if (addr == 0) return L"*";
  unsigned int b1 = (addr) & 0xFF;
  unsigned int b2 = (addr >> 8) & 0xFF;
  unsigned int b3 = (addr >> 16) & 0xFF;
  unsigned int b4 = (addr >> 24) & 0xFF;
  return std::to_wstring(b1) + L"." + std::to_wstring(b2) + L"." +
         std::to_wstring(b3) + L"." + std::to_wstring(b4);
}

auto format_ipv6(const UCHAR* addr, DWORD scope_id) -> std::wstring {
  if (!addr) return L"*";
  bool all_zero = true;
  for (int i = 0; i < 16; ++i) {
    if (addr[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) return L"*";

  std::wostringstream oss;
  oss << std::hex << std::setfill(L'0');
  for (int i = 0; i < 8; ++i) {
    unsigned int hi = static_cast<unsigned int>(addr[i * 2]);
    unsigned int lo = static_cast<unsigned int>(addr[i * 2 + 1]);
    unsigned int group = (hi << 8) | lo;
    if (i) oss << L":";
    oss << std::setw(4) << group;
  }
  if (scope_id != 0) {
    oss << L"%" << std::dec << scope_id;
  }
  return oss.str();
}

auto tcp_state_name(DWORD state) -> std::wstring {
  switch (state) {
    case MIB_TCP_STATE_CLOSED:
      return L"CLOSED";
    case MIB_TCP_STATE_LISTEN:
      return L"LISTEN";
    case MIB_TCP_STATE_SYN_SENT:
      return L"SYN_SENT";
    case MIB_TCP_STATE_SYN_RCVD:
      return L"SYN_RECV";
    case MIB_TCP_STATE_ESTAB:
      return L"ESTABLISHED";
    case MIB_TCP_STATE_FIN_WAIT1:
      return L"FIN_WAIT1";
    case MIB_TCP_STATE_FIN_WAIT2:
      return L"FIN_WAIT2";
    case MIB_TCP_STATE_CLOSE_WAIT:
      return L"CLOSE_WAIT";
    case MIB_TCP_STATE_CLOSING:
      return L"CLOSING";
    case MIB_TCP_STATE_LAST_ACK:
      return L"LAST_ACK";
    case MIB_TCP_STATE_TIME_WAIT:
      return L"TIME_WAIT";
    case MIB_TCP_STATE_DELETE_TCB:
      return L"DELETE_TCB";
    default:
      return L"UNKNOWN";
  }
}

auto collect_internet_entries(
    const Config& cfg,
    const std::unordered_map<DWORD, std::wstring>& proc_names)
    -> std::vector<LsofEntry> {
  std::vector<LsofEntry> rows;
  rows.reserve(1024);

  ULONG size = 0;
  DWORD rc = GetExtendedTcpTable(nullptr, &size, TRUE, IPV4_FAMILY,
                                 TCP_TABLE_OWNER_PID_ALL, 0);
  if (rc == ERROR_INSUFFICIENT_BUFFER && size > 0) {
    std::vector<std::byte> buf(size);
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
    if (GetExtendedTcpTable(table, &size, TRUE, IPV4_FAMILY,
                            TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
      for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& r = table->table[i];
        if (cfg.pid_filter.has_value() && r.dwOwningPid != *cfg.pid_filter)
          continue;
        if (cfg.inet_filter) {
          const auto& f = *cfg.inet_filter;
          if (f.protocol && *f.protocol != L"TCP") continue;
          if (f.port) {
            unsigned short lp = parse_port(r.dwLocalPort);
            unsigned short rp = parse_port(r.dwRemotePort);
            if (lp != *f.port && rp != *f.port) continue;
          }
        }

        std::wstring cmd = L"?";
        if (auto it = proc_names.find(r.dwOwningPid);
            it != proc_names.end() && !it->second.empty()) {
          cmd = it->second;
        }

        std::wstring local = format_ipv4(r.dwLocalAddr) + L":" +
                             std::to_wstring(parse_port(r.dwLocalPort));
        std::wstring remote = format_ipv4(r.dwRemoteAddr) + L":" +
                              std::to_wstring(parse_port(r.dwRemotePort));
        std::wstring name =
            local + L"->" + remote + L" (" + tcp_state_name(r.dwState) + L")";

        rows.push_back(
            {std::move(cmd), r.dwOwningPid, L"TCP", std::move(name)});
      }
    }
  }

  size = 0;
  rc = GetExtendedUdpTable(nullptr, &size, TRUE, IPV4_FAMILY,
                           UDP_TABLE_OWNER_PID, 0);
  if (rc == ERROR_INSUFFICIENT_BUFFER && size > 0) {
    std::vector<std::byte> buf(size);
    auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
    if (GetExtendedUdpTable(table, &size, TRUE, IPV4_FAMILY,
                            UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
      for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& r = table->table[i];
        if (cfg.pid_filter.has_value() && r.dwOwningPid != *cfg.pid_filter)
          continue;
        if (cfg.inet_filter) {
          const auto& f = *cfg.inet_filter;
          if (f.protocol && *f.protocol != L"UDP") continue;
          if (f.port && parse_port(r.dwLocalPort) != *f.port) continue;
        }

        std::wstring cmd = L"?";
        if (auto it = proc_names.find(r.dwOwningPid);
            it != proc_names.end() && !it->second.empty()) {
          cmd = it->second;
        }

        std::wstring name = format_ipv4(r.dwLocalAddr) + L":" +
                            std::to_wstring(parse_port(r.dwLocalPort));
        rows.push_back(
            {std::move(cmd), r.dwOwningPid, L"UDP", std::move(name)});
      }
    }
  }

  size = 0;
  rc = GetExtendedTcpTable(nullptr, &size, TRUE, IPV6_FAMILY,
                           TCP_TABLE_OWNER_PID_ALL, 0);
  if (rc == ERROR_INSUFFICIENT_BUFFER && size > 0) {
    std::vector<std::byte> buf(size);
    auto* table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID_LOCAL*>(buf.data());
    if (GetExtendedTcpTable(table, &size, TRUE, IPV6_FAMILY,
                            TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
      for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& r = table->table[i];
        if (cfg.pid_filter.has_value() && r.dwOwningPid != *cfg.pid_filter)
          continue;
        if (cfg.inet_filter) {
          const auto& f = *cfg.inet_filter;
          if (f.protocol && *f.protocol != L"TCP6") continue;
          if (f.port) {
            unsigned short lp = parse_port(r.dwLocalPort);
            unsigned short rp = parse_port(r.dwRemotePort);
            if (lp != *f.port && rp != *f.port) continue;
          }
        }

        std::wstring cmd = L"?";
        if (auto it = proc_names.find(r.dwOwningPid);
            it != proc_names.end() && !it->second.empty()) {
          cmd = it->second;
        }

        std::wstring local = format_ipv6(r.ucLocalAddr, r.dwLocalScopeId) +
                             L":" + std::to_wstring(parse_port(r.dwLocalPort));
        std::wstring remote = format_ipv6(r.ucRemoteAddr, r.dwRemoteScopeId) +
                              L":" +
                              std::to_wstring(parse_port(r.dwRemotePort));
        std::wstring name =
            local + L"->" + remote + L" (" + tcp_state_name(r.dwState) + L")";

        rows.push_back(
            {std::move(cmd), r.dwOwningPid, L"TCP6", std::move(name)});
      }
    }
  }

  size = 0;
  rc = GetExtendedUdpTable(nullptr, &size, TRUE, IPV6_FAMILY,
                           UDP_TABLE_OWNER_PID, 0);
  if (rc == ERROR_INSUFFICIENT_BUFFER && size > 0) {
    std::vector<std::byte> buf(size);
    auto* table = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID_LOCAL*>(buf.data());
    if (GetExtendedUdpTable(table, &size, TRUE, IPV6_FAMILY,
                            UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
      for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& r = table->table[i];
        if (cfg.pid_filter.has_value() && r.dwOwningPid != *cfg.pid_filter)
          continue;
        if (cfg.inet_filter) {
          const auto& f = *cfg.inet_filter;
          if (f.protocol && *f.protocol != L"UDP6") continue;
          if (f.port && parse_port(r.dwLocalPort) != *f.port) continue;
        }

        std::wstring cmd = L"?";
        if (auto it = proc_names.find(r.dwOwningPid);
            it != proc_names.end() && !it->second.empty()) {
          cmd = it->second;
        }

        std::wstring name = format_ipv6(r.ucLocalAddr, r.dwLocalScopeId) +
                            L":" + std::to_wstring(parse_port(r.dwLocalPort));
        rows.push_back(
            {std::move(cmd), r.dwOwningPid, L"UDP6", std::move(name)});
      }
    }
  }

  return rows;
}

auto run(const Config& cfg) -> int {
  auto ntdll = load_ntdll_functions();
  if (!ntdll) {
    safeErrorPrintLn(L"lsof: cannot load required ntdll routines");
    return 1;
  }
  auto [query_sys, query_obj] = *ntdll;

  bool debug_enabled = enable_debug_privilege();

  auto handles_blob = query_system_handles(query_sys);
  if (!handles_blob) {
    safeErrorPrintLn(L"lsof: failed to query system handle table");
    return 1;
  }

  auto* all_handles =
      reinterpret_cast<const SYSTEM_HANDLE_INFORMATION_EX_LOCAL*>(
          handles_blob->data());
  if (!all_handles) {
    safeErrorPrintLn(L"lsof: invalid system handle data");
    return 1;
  }

  const ULONG_PTR count = all_handles->NumberOfHandles;
  auto proc_names = build_process_name_map();
  auto dos_map = build_dos_device_map();

  std::unordered_map<DWORD, HANDLE> process_handles;
  process_handles.reserve(1024);

  std::unordered_map<USHORT, std::wstring> type_cache;
  type_cache.reserve(128);

  std::vector<LsofEntry> rows;
  rows.reserve(4096);

  size_t access_denied = 0;
  size_t duplicate_failed = 0;
  size_t query_timeouts = 0;

  for (ULONG_PTR i = 0; i < count; ++i) {
    const auto& h = all_handles->Handles[i];
    DWORD pid = static_cast<DWORD>(h.UniqueProcessId);

    if (cfg.pid_filter.has_value() && pid != *cfg.pid_filter) continue;

    HANDLE src_proc = nullptr;
    auto p_it = process_handles.find(pid);
    if (p_it == process_handles.end()) {
      HANDLE opened = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
      process_handles.emplace(pid, opened);
      src_proc = opened;
    } else {
      src_proc = p_it->second;
    }

    if (!src_proc) {
      ++access_denied;
      continue;
    }

    HANDLE dup = nullptr;
    if (!DuplicateHandle(src_proc, reinterpret_cast<HANDLE>(h.HandleValue),
                         GetCurrentProcess(), &dup, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
      ++duplicate_failed;
      continue;
    }
    unique_handle dup_holder(dup);

    std::wstring type_name;
    auto tc_it = type_cache.find(h.ObjectTypeIndex);
    if (tc_it != type_cache.end()) {
      type_name = tc_it->second;
    } else {
      auto type_result = query_object_text_with_timeout(
          dup_holder.get(), query_obj, OBJECT_TYPE_INFORMATION_CLASS,
          cfg.timeout_ms);
      if (type_result.status == QueryResult::Status::Timeout) {
        ++query_timeouts;
        continue;
      }
      if (type_result.status != QueryResult::Status::Ok ||
          type_result.text.empty()) {
        continue;
      }
      type_name = type_result.text;
      type_cache.emplace(h.ObjectTypeIndex, type_name);
    }

    bool is_file = (type_name == L"File");
    if (!is_file && !cfg.show_all) continue;

    auto name_result = query_object_text_with_timeout(
        dup_holder.get(), query_obj, OBJECT_NAME_INFORMATION_CLASS,
        cfg.timeout_ms);
    if (name_result.status == QueryResult::Status::Timeout) {
      ++query_timeouts;
      continue;
    }

    std::wstring object_name;
    if (name_result.status == QueryResult::Status::Ok) {
      object_name = std::move(name_result.text);
    }

    if (object_name.empty() && !cfg.show_all) continue;
    if (object_name.empty()) object_name = L"(unnamed)";

    if (cfg.internet_only) {
      if (!is_internet_like_name(object_name)) continue;
      // Handle-based entries carry no port metadata.
      // When a port filter is active, skip them entirely;
      // collect_internet_entries() will supply the correctly filtered rows.
      if (cfg.inet_filter && cfg.inet_filter->port.has_value()) continue;
    }

    std::wstring cmd = L"?";
    if (auto it = proc_names.find(pid);
        it != proc_names.end() && !it->second.empty()) {
      cmd = it->second;
    }

    if (is_file && !cfg.internet_only) {
      object_name =
          normalize_path(std::move(object_name), cfg.numeric, dos_map);
    }

    rows.push_back(
        {std::move(cmd), pid, std::move(type_name), std::move(object_name)});
  }

  for (auto& [_, ph] : process_handles) {
    if (ph) CloseHandle(ph);
  }

  std::ranges::sort(rows, [](const LsofEntry& a, const LsofEntry& b) {
    if (a.pid != b.pid) return a.pid < b.pid;
    if (a.command != b.command) return a.command < b.command;
    if (a.type != b.type) return a.type < b.type;
    return a.name < b.name;
  });

  if (cfg.internet_only) {
    auto internet_rows = collect_internet_entries(cfg, proc_names);
    rows.insert(rows.end(), std::make_move_iterator(internet_rows.begin()),
                std::make_move_iterator(internet_rows.end()));
    std::ranges::sort(rows, [](const LsofEntry& a, const LsofEntry& b) {
      if (a.pid != b.pid) return a.pid < b.pid;
      if (a.command != b.command) return a.command < b.command;
      if (a.type != b.type) return a.type < b.type;
      return a.name < b.name;
    });
  }

  if (!cfg.field_mode) {
    if (!cfg.no_headers) {
      safePrintLn(L"COMMAND                     PID TYPE     NAME");
    }
    for (const auto& row : rows) {
      safePrintLn(format_row(row));
    }
  } else {
    for (const auto& row : rows) {
      emit_field_row(row);
    }
  }

  if (!debug_enabled) {
    safeErrorPrintLn(
        L"lsof: warning: SeDebugPrivilege not enabled; output may be partial");
  }
  if (access_denied > 0) {
    safeErrorPrint(L"lsof: warning: access denied on ");
    safeErrorPrint(static_cast<unsigned long long>(access_denied));
    safeErrorPrintLn(L" process handle sets");
  }
  if (duplicate_failed > 0) {
    safeErrorPrint(L"lsof: warning: failed to duplicate ");
    safeErrorPrint(static_cast<unsigned long long>(duplicate_failed));
    safeErrorPrintLn(L" handles");
  }
  if (query_timeouts > 0) {
    safeErrorPrint(L"lsof: warning: NtQueryObject timed out on ");
    safeErrorPrint(static_cast<unsigned long long>(query_timeouts));
    safeErrorPrintLn(L" handles");
  }

  return 0;
}

}  // namespace lsof_pipeline

REGISTER_COMMAND(lsof, "lsof", "lsof [options]",
                 "List open files for running processes on Windows.\n"
                 "This command enumerates kernel handles and prints entries "
                 "that map to files.\n"
                 "Some handles may require elevated privileges and some object "
                 "queries can timeout.",
                 "  lsof\n"
                 "  lsof -p 1234\n"
                 "  lsof --no-headers --pid 4321\n"
                 "  lsof -a --timeout-ms 120\n"
                 "  lsof -i\n"
                 "  lsof -i :8080\n"
                 "  lsof -i tcp\n"
                 "  lsof -i tcp:8080\n"
                 "  lsof -i -F",
                 "ps(1), handle.exe", "WinuxCmd", "Copyright (c) 2026 WinuxCmd",
                 LSOF_OPTIONS) {
  using namespace lsof_pipeline;

  auto cfg = parse_config(ctx);
  if (!cfg.has_value()) {
    safeErrorPrintLn(L"lsof: invalid options");
    return 1;
  }
  return run(*cfg);
}
