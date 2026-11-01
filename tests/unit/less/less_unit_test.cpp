// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(less, less_file_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  TEST_LOG_FILE_CONTENT("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-F", L"test.txt"});

  TEST_LOG_CMD_LIST("less.exe", L"-F", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"less.exe", {L"-F"});

  TEST_LOG_CMD_LIST("less.exe", L"-F");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less stdin output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_quit_if_one_screen) {
  TempDir tmp;
  tmp.write("short.txt", "test\n");

  TEST_LOG_FILE_CONTENT("short.txt", "test\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-F", L"short.txt"});

  TEST_LOG_CMD_LIST("less.exe", L"-F", L"short.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less quit if one screen output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_numeric_window_option_is_accepted) {
  TempDir tmp;
  tmp.write("short.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-50", L"-F", L"short.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_quit_on_interrupt_option_is_accepted) {
  TempDir tmp;
  tmp.write("short.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-K", L"-F", L"short.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_no_init_option_is_accepted) {
  TempDir tmp;
  tmp.write("short.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"--no-init", L"-F", L"short.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

// [#1212] piped stdin is a byte stream: fd 0 must not drop data at 0x1A.
TEST(less, less_stdin_bytes_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  const std::string expected(
      "AAAA\x1a"
      "BBBB\n",
      10);
  Pipeline p;
  p.set_stdin(payload);
  p.add(L"less.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}

// #1385 (enclave rule): with a shell root configured, a file operand spelled
// from the root (`less /ra.txt`) reads through the payload (shell) root,
// never on the current drive root; the drive spelling of the same file still
// resolves through the drive fold, and relative operands are unchanged.
TEST(less, less_file_operand_uses_shell_root_issue1385) {
  TempDir payload;
  payload.write("ra.txt", "payload-content\n");
  TempDir elsewhere;

  Pipeline root_spelling;
  root_spelling.set_cwd(elsewhere.wpath());
  root_spelling.set_env(L"WINUXSH_ROOT", payload.wpath());
  root_spelling.add(L"less.exe", {L"/ra.txt"});
  auto root_result = root_spelling.run();

  EXPECT_EQ(root_result.exit_code, 0);
  EXPECT_EQ_TEXT(root_result.stdout_text, "payload-content\n");

  // The drive spelling of the same file ("/c/Users/.../ra.txt") must keep
  // resolving through the drive fold onto the very same bytes.
  const std::wstring generic = (payload.path / L"ra.txt").generic_wstring();
  std::wstring drive_spelled_arg = L"/";
  drive_spelled_arg.push_back(
      static_cast<wchar_t>(std::tolower(generic.front())));
  drive_spelled_arg += generic.substr(2);

  Pipeline drive_spelled;
  drive_spelled.set_cwd(elsewhere.wpath());
  drive_spelled.set_env(L"WINUXSH_ROOT", payload.wpath());
  drive_spelled.add(L"less.exe", {drive_spelled_arg});
  auto drive_result = drive_spelled.run();

  EXPECT_EQ(drive_result.exit_code, 0);
  EXPECT_EQ_TEXT(drive_result.stdout_text, "payload-content\n");
}

TEST(less, less_relative_operand_unchanged_issue1385) {
  TempDir tmp;
  tmp.write("nearby.txt", "relative-content\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"nearby.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "relative-content\n");
}

// [refs #1127] The recorded P2c options are accepted; in the
// non-console (piped) path they do not change passthrough output.
// The interactive faces (follow, mouse wheel, incsearch preview,
// --file-size prompt, rscroll marker, --line-num-width rendering)
// need a real console and are covered by code + report notes.
TEST(less, p2c_options_accepted_in_passthrough) {
  TempDir tmp;
  tmp.write("doc.txt", "alpha\nbeta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe",
        {L"--mouse", L"--incsearch", L"--follow-name", L"--no-histdups",
         L"--no-keypad", L"--file-size", L"--line-num-width", L"4",
         L"--rscroll", L">", L"doc.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nbeta\n");
}

// [refs #1127] A negative --line-num-width is a usage error.
TEST(less, negative_line_num_width_rejected) {
  TempDir tmp;
  tmp.write("doc.txt", "alpha\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"--line-num-width", L"-1", L"doc.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}
