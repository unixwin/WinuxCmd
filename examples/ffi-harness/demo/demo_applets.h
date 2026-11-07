// Demo applets shared by the FFI harness test doubles.
//
// These are NOT WinuxCmd command implementations. They exist so the
// harness loader (discovery, hash pinning, tiering, switch, fail-closed
// fallback) can be exercised under Wine/CI before the real winuxcore.dll
// is available in a given environment: demo_dll.cpp wraps them in the
// ABI v2 surface, demo_main.cpp exposes the identical logic as a spawn
// twin named like winuxcmd.exe. Byte-equivalence checks in this directory
// compare the two doubles against each other; equivalence of the real
// DLL against the real winuxcmd.exe is a separate verification step.
#pragma once

#include <string>
#include <vector>

// Each returns a process-style exit code and writes to the current std
// handles only. argv excludes the applet name.
int demo_cat(const std::vector<std::wstring> &args);
int demo_cp(const std::vector<std::wstring> &args);
int demo_rm(const std::vector<std::wstring> &args);

// Dispatch by name; returns -1 when the applet is unknown to the demo set.
int demo_dispatch(const std::wstring &applet,
                  const std::vector<std::wstring> &args);
