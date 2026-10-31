/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(col, col_basic) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"col.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
}

TEST(col, col_no_backspaces) {
  Pipeline p;
  p.set_stdin("abc\bd\n");
  p.add(L"col.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // With -b, backspace should be ignored
}

TEST(col, col_spaces_for_tabs) {
  Pipeline p;
  p.set_stdin("hello\tworld\n");
  p.add(L"col.exe", {L"-x"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Tabs should be converted to spaces
}

TEST(col, col_forward_half) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"col.exe", {L"-f"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(col, col_pass_unknown) {
  Pipeline p;
  p.set_stdin("hello\x01world\n");
  p.add(L"col.exe", {L"-p"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(col, col_empty_input) {
  Pipeline p;
  p.set_stdin("");
  p.add(L"col.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

// [#1212] a piped payload must not be truncated at the first 0x1A.
TEST(col, col_stdin_ctrlz_not_truncated_issue1212) {
  const std::string payload(
      "AAAA\x1a"
      "BBBB\n",
      10);
  Pipeline p;
  p.set_stdin(payload);
  p.add(L"col.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("BBBB") != std::string::npos);
}

// [#1127 P2c] util-linux col compresses space runs into tabs by default
// (col.c:293-301): a gap from column 1 to column 8 is one tab.
TEST(col, col_default_compresses_spaces_to_tabs) {
  Pipeline p;
  p.set_stdin("a       b\n");
  p.add(L"col.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\tb\n");
}

// [#1127 P2c] -x/--spaces disables the compression; --tabs keeps it.
TEST(col, col_spaces_option_disables_compression) {
  Pipeline p;
  p.set_stdin("a       b\n");
  p.add(L"col.exe", {L"--spaces"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a       b\n");

  Pipeline t;
  t.set_stdin("a       b\n");
  t.add(L"col.exe", {L"--tabs"});
  auto tr = t.run();

  EXPECT_EQ(tr.exit_code, 0);
  EXPECT_EQ_TEXT(tr.stdout_text, "a\tb\n");
}

// [#1127 P2c] The long option names from col.c's longopts table are
// accepted, -l/--lines validates its argument, and -h/-x conflict.
TEST(col, col_long_options_and_lines_validation) {
  Pipeline p;
  p.set_stdin("hello\n");
  p.add(L"col.exe",
        {L"--no-backspaces", L"--fine", L"--pass", L"--lines", L"64"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  Pipeline bad;
  bad.set_stdin("hello\n");
  bad.add(L"col.exe", {L"--lines", L"bogus"});
  auto br = bad.run();
  EXPECT_EQ(br.exit_code, 1);
  EXPECT_TRUE(br.stderr_text.find("bad -l argument") != std::string::npos);

  Pipeline conflict;
  conflict.set_stdin("hello\n");
  conflict.add(L"col.exe", {L"-h", L"-x"});
  auto cr = conflict.run();
  EXPECT_EQ(cr.exit_code, 1);
}
