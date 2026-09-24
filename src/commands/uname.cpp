// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for uname.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr UNAME_OPTIONS = std::array{
    // [DIFFERS] option
    OPTION("-a", "--all", "print all information", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-s", "--kernel-name", "print the kernel name", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-n", "--nodename", "print the network node hostname", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-r", "--kernel-release", "print the kernel release", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-v", "--kernel-version", "print the kernel version", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-m", "--machine", "print the machine hardware name", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-p", "--processor", "print the processor type", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-i", "--hardware-platform", "print the hardware platform",
           BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-o", "--operating-system", "print the operating system", BOOL_TYPE),
    // [GNU] --sysname: obsolescent alias for -s/--kernel-name; hidden
    OPTION("", "--sysname", "", BOOL_TYPE),
    // [GNU] --release: obsolescent alias for -r/--kernel-release; hidden
    OPTION("", "--release", "", BOOL_TYPE)};

namespace uname_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool all = false;
  bool kernel_name = false;
  bool nodename = false;
  bool kernel_release = false;
  bool kernel_version = false;
  bool machine = false;
  bool processor = false;
  bool hardware_platform = false;
  bool operating_system = false;
};

auto build_config(const CommandContext<UNAME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  cfg.kernel_name = ctx.get<bool>("--kernel-name", false) ||
                    ctx.get<bool>("-s", false) ||
                    ctx.get<bool>("--sysname", false);
  cfg.nodename =
      ctx.get<bool>("--nodename", false) || ctx.get<bool>("-n", false);
  cfg.kernel_release = ctx.get<bool>("--kernel-release", false) ||
                       ctx.get<bool>("-r", false) ||
                       ctx.get<bool>("--release", false);
  cfg.kernel_version =
      ctx.get<bool>("--kernel-version", false) || ctx.get<bool>("-v", false);
  cfg.machine = ctx.get<bool>("--machine", false) || ctx.get<bool>("-m", false);
  cfg.processor =
      ctx.get<bool>("--processor", false) || ctx.get<bool>("-p", false);
  cfg.hardware_platform =
      ctx.get<bool>("--hardware-platform", false) || ctx.get<bool>("-i", false);
  cfg.operating_system =
      ctx.get<bool>("--operating-system", false) || ctx.get<bool>("-o", false);

  // If no option specified, print kernel name
  if (!cfg.all && !cfg.kernel_name && !cfg.nodename && !cfg.kernel_release &&
      !cfg.kernel_version && !cfg.machine && !cfg.processor &&
      !cfg.hardware_platform && !cfg.operating_system) {
    cfg.kernel_name = true;
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  SmallVector<std::string, 16> outputs;

  Config local_cfg = cfg;
  if (local_cfg.all) {
    local_cfg.kernel_name = true;
    local_cfg.nodename = true;
    local_cfg.kernel_release = true;
    local_cfg.kernel_version = true;
    local_cfg.machine = true;
    local_cfg.processor = true;
    local_cfg.hardware_platform = true;
    local_cfg.operating_system = true;
  }

  // Get Windows version info.  [GNU] -r/-v must reflect the running OS,
  // so use RtlGetVersion (GetVersionEx is subject to manifest shims).
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  RTL_OSVERSIONINFOW osvi = {0};
  osvi.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
  if (auto* rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
          GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
    rtl_get_version(&osvi);
  }

  // Kernel name
  if (local_cfg.kernel_name) {
    outputs.push_back("Windows_NT");
  }

  // Nodename (hostname)
  if (local_cfg.nodename) {
    WCHAR hostname[256];
    DWORD size = 256;
    if (GetComputerNameW(hostname, &size)) {
      std::wstring ws(hostname);
      std::string host = wstring_to_utf8(ws);
      outputs.push_back(host);
    } else {
      outputs.push_back("unknown");
    }
  }

  // Kernel release
  if (local_cfg.kernel_release) {
    outputs.push_back(std::to_string(osvi.dwMajorVersion) + "." +
                      std::to_string(osvi.dwMinorVersion));
  }

  // Kernel version
  if (local_cfg.kernel_version) {
    outputs.push_back(std::to_string(osvi.dwBuildNumber));
  }

  // Machine
  if (local_cfg.machine) {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
      outputs.push_back("x86_64");
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
      outputs.push_back("aarch64");
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
      outputs.push_back("i386");
    } else {
      outputs.push_back("unknown");
    }
  }

  // Processor
  if (local_cfg.processor) {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    outputs.push_back(
        "unknown");  // Detailed processor info not easily accessible
  }

  // Hardware platform
  if (local_cfg.hardware_platform) {
    outputs.push_back("PC");
  }

  // Operating system
  if (local_cfg.operating_system) {
    outputs.push_back("Windows_NT");
  }

  // Print with space separator
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (i > 0) {
      safePrint(" ");
    }
    safePrint(outputs[i]);
  }
  safePrintLn("");

  return 0;
}

}  // namespace uname_pipeline

REGISTER_COMMAND(
    uname, "uname", "uname [OPTION]...",
    "Print certain system information.\n"
    "\n"
    "With no OPTION, same as -s.\n"
    "\n"
    "Note: This implementation provides Windows-specific information.",
    "  uname -a\n"
    "  uname -m\n"
    "  uname -r\n"
    "  uname -n",
    "arch(1), hostname(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    UNAME_OPTIONS) {
  using namespace uname_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"uname");
    return 1;
  }

  return run(*cfg_result);
}
