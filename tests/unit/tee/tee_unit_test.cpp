/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(tee, tee_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("hello\n");
  p.add(L"tee.exe", {L"output.txt"});

  TEST_LOG_CMD_LIST("tee.exe", L"output.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tee output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello\n");
}

TEST(tee, tee_output_operand_is_literal_not_glob) {
  TempDir tmp;
  tmp.write("outa.txt", "old\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("new\n");
  p.add(L"tee.exe", {L"out[abc].txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "new\n");
  EXPECT_EQ(tmp.read("outa.txt"), "old\n");
  EXPECT_EQ(tmp.read("out[abc].txt"), "new\n");
}

TEST(tee, tee_dash_operand_duplicates_stdout_like_gnu) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("hello\n");
  p.add(L"tee.exe", {L"-", L"output.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello\nhello\n");
  EXPECT_EQ(tmp.read("output.txt"), "hello\n");
}

TEST(tee, tee_append_appends_instead_of_truncating) {
  TempDir tmp;
  tmp.write("output.txt", "old\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("new\n");
  p.add(L"tee.exe", {L"-a", L"output.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "new\n");
  EXPECT_EQ(tmp.read("output.txt"), "old\nnew\n");
}

TEST(tee, tee_preserves_missing_final_newline) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("hello");
  p.add(L"tee.exe", {L"output.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello");
  EXPECT_EQ(tmp.read("output.txt"), "hello");
}

TEST(tee, tee_preserves_crlf_bytes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("line1\r\nline2\r\n");
  p.add(L"tee.exe", {L"output.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\r\nline2\r\n");
  EXPECT_EQ(tmp.read("output.txt"), "line1\r\nline2\r\n");
}

TEST(tee, tee_open_failure_still_writes_stdout_and_other_outputs) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("hello\n");
  p.add(L"tee.exe", {L"missing-dir\\output.txt", L"good.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "hello\n");
  EXPECT_EQ(tmp.read("good.txt"), "hello\n");
  EXPECT_TRUE(r.stderr_text.find("tee: 'missing-dir\\output.txt'") !=
              std::string::npos);
}

TEST(tee, tee_invalid_output_error_mode_is_rejected) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("hello\n");
  p.add(L"tee.exe", {L"--output-error=bogus", L"output.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(
      r.stderr_text.find("tee: invalid argument 'bogus' for '--output-error'") !=
      std::string::npos);
}
