#include "framework/winuxtest.h"

TEST(rev, rev_reverses_stdin_records) {
  Pipeline p;
  p.set_stdin("abc\nxy\n");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "cba\nyx\n");
}

TEST(rev, rev_preserves_unterminated_final_record) {
  Pipeline p;
  p.set_stdin("abc");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ(r.stdout_text, "cba");
}

TEST(rev, rev_reverses_multiple_files_in_order) {
  TempDir tmp;
  tmp.write("a.txt", "ab\n");
  tmp.write("b.txt", "cd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rev.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ba\ndc\n");
}

TEST(rev, rev_missing_file_reports_failure_but_continues) {
  TempDir tmp;
  tmp.write("ok.txt", "abc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rev.exe", {L"missing.txt", L"ok.txt"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "cba\n");
  EXPECT_CONTAINS(r.stderr_text, "missing.txt");
}

TEST(rev, rev_zero_option_uses_nul_separator) {
  std::string input("abc\0de\0tail", 12);
  std::string expected("cba\0ed\0liat", 12);

  Pipeline p;
  p.set_stdin(input);
  p.add(L"rev.exe", {L"-0"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_BYTES(r.stdout_text, expected);
}

TEST(rev, rev_crlf_input_preserves_carriage_return_as_data) {
  Pipeline p;
  p.set_stdin("ab\r\n");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ(r.stdout_text, "\rba\n");
}

TEST(rev, rev_reverses_utf8_codepoint_units) {
  Pipeline p;
  p.set_stdin(std::string("\xC3\xA9x\n", 4));
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ(r.stdout_text, std::string("x\xC3\xA9\n", 4));
}

TEST(rev, rev_expands_wildcard_file_operands) {
  TempDir tmp;
  tmp.write("a.txt", "ab\n");
  tmp.write("b.txt", "cd\n");
  tmp.write("skip.log", "ef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rev.exe", {L"*.txt"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_CONTAINS(r.stdout_text, "ba\n");
  EXPECT_CONTAINS(r.stdout_text, "dc\n");
  EXPECT_NOT_CONTAINS(r.stdout_text, "fe\n");
}
