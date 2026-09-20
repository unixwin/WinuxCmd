// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <windows.h>

#include <stdexcept>

#include "framework/framework_pch.h"
#include "framework/paths.h"

/**
 * @brief Implementation of get_current_exe_dir for Windows platform
 *
 * Uses Windows API GetModuleFileNameW to retrieve the full path of
 * the current executable, then extracts just the directory portion.
 *
 * @return std::filesystem::path Directory containing the current executable
 * @throws std::runtime_error if GetModuleFileNameW fails or buffer is too small
 */
std::filesystem::path get_current_exe_dir() {
  wchar_t buf[MAX_PATH];

  // Retrieve the full path of the current executable
  DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);

  // Check for errors: function failed or buffer was too small
  if (len == 0 || len == MAX_PATH) {
    throw std::runtime_error("GetModuleFileNameW failed");
  }

  // Convert to filesystem path and return parent directory
  return std::filesystem::path(buf).parent_path();
}
