
#include "framework/winuxtest.h"

TEST(cygpath, default_converts_windows_path_to_unix) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"C:\\Users\\Alice\\Documents"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/c/Users/Alice/Documents\n");
}

TEST(cygpath, explicit_windows_and_mixed_modes_convert_posix_drive_paths) {
  Pipeline windows;
  windows.add(L"cygpath.exe", {L"-w", L"/c/Users/Alice/Documents"});
  auto windows_result = windows.run();

  EXPECT_EQ(windows_result.exit_code, 0);
  EXPECT_EQ_TEXT(windows_result.stdout_text, "C:\\Users\\Alice\\Documents\n");

  Pipeline mixed;
  mixed.add(L"cygpath.exe", {L"-m", L"/c/Users/Alice/Documents"});
  auto mixed_result = mixed.run();

  EXPECT_EQ(mixed_result.exit_code, 0);
  EXPECT_EQ_TEXT(mixed_result.stdout_text, "C:/Users/Alice/Documents\n");
}

TEST(cygpath, type_option_selects_output_mode) {
  Pipeline unix_mode;
  unix_mode.add(L"cygpath.exe",
                {L"--type=unix", L"C:\\Users\\Alice\\Documents"});
  auto unix_result = unix_mode.run();

  EXPECT_EQ(unix_result.exit_code, 0);
  EXPECT_EQ_TEXT(unix_result.stdout_text, "/c/Users/Alice/Documents\n");

  Pipeline mixed_mode;
  mixed_mode.add(L"cygpath.exe",
                 {L"--type", L"mixed", L"/c/Users/Alice/Documents"});
  auto mixed_result = mixed_mode.run();

  EXPECT_EQ(mixed_result.exit_code, 0);
  EXPECT_EQ_TEXT(mixed_result.stdout_text, "C:/Users/Alice/Documents\n");
}

TEST(cygpath, path_list_conversion_uses_platform_separators) {
  Pipeline to_unix;
  to_unix.add(L"cygpath.exe", {L"-p", L"-u", L"C:\\A;D:\\B"});
  auto unix_result = to_unix.run();

  EXPECT_EQ(unix_result.exit_code, 0);
  EXPECT_EQ_TEXT(unix_result.stdout_text, "/c/A:/d/B\n");

  Pipeline to_windows;
  to_windows.add(L"cygpath.exe", {L"-p", L"-w", L"/c/A:/d/B"});
  auto windows_result = to_windows.run();

  EXPECT_EQ(windows_result.exit_code, 0);
  EXPECT_EQ_TEXT(windows_result.stdout_text, "C:\\A;D:\\B\n");
}

TEST(cygpath, path_list_mixed_mode_uses_semicolon_and_forward_slashes) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"-p", L"-m", L"/c/A:/d/B"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "C:/A;D:/B\n");
}

TEST(cygpath, conflicting_output_modes_are_rejected) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"-u", L"-w", L"C:\\A"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "only one output type");
}

TEST(cygpath, unsupported_upstream_options_are_accepted_with_diagnostic) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"--option", L"opts.txt", L"-w", L"/c/A"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "accepted for compatibility");
}

TEST(cygpath, ignore_allows_missing_operands) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"--ignore"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(cygpath, file_option_reads_paths_line_by_line) {
  TempDir tmp;
  tmp.write("paths.txt", "C:\\A\nD:\\B\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cygpath.exe", {L"-f", L"paths.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/c/A\n/d/B\n");
}

TEST(cygpath, proc_cygdrive_uses_cygwin_style_prefix) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"-U", L"C:\\Users\\Alice"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "/proc/cygdrive/c/Users/Alice\n");
}

TEST(cygpath, mode_reports_native_binmode) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"-M", L"C:\\A"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "binmode\n");
}

TEST(cygpath, special_folder_options_emit_paths) {
  Pipeline p;
  p.add(L"cygpath.exe", {L"-W"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "/");
}
