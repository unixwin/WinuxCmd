// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#pragma once
#include "wctest.h"

/**
 * @brief Main entry point for test executables
 *
 * Delegates to the wctest framework's default main function
 * which handles test discovery, execution, and reporting.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return int Exit code (0 for success, non-zero for failures)
 */
int main(int argc, char** argv) {
  // Test snapshots use the built-in English fallback, regardless of the
  // locale selected by the shell that launched the test process.
  SetEnvironmentVariableW(L"WINUX_LANG", nullptr);

  // Same reason for the POSIX locale variables: `locale charmap` answers from
  // the environment (see commands/locale.cpp), so a shell that happens to
  // export LC_ALL=C would otherwise change a snapshot's expected text. Unit
  // tests that want the C locale set it explicitly.
  SetEnvironmentVariableW(L"LC_ALL", nullptr);
  SetEnvironmentVariableW(L"LC_CTYPE", nullptr);
  SetEnvironmentVariableW(L"LANG", nullptr);

  return wctest::default_main(argc, argv);
}
