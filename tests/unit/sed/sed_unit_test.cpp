#include "framework/winuxtest.h"

TEST(sed, substitute_basic) {
  TempDir tmp;
  tmp.write("a.txt", "foo bar\nfoo baz\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/qux/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "qux bar\nqux baz\n");
}

TEST(sed, substitute_global_and_print_flag) {
  TempDir tmp;
  tmp.write("a.txt", "foo foo\nnone\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"s/foo/bar/gp", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar bar\n");
}

TEST(sed, append_insert_change) {
  TempDir tmp;
  tmp.write("a.txt", "line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  // insert before, change line, append after
  p.add(L"sed.exe", {L"i before", L"c middle", L"a after", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "before\nmiddle\nafter\n");
}

TEST(sed, script_file_and_quiet) {
  TempDir tmp;
  tmp.write("a.txt", "foo\n");
  tmp.write("script.sed", "s/foo/bar/\np\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-n", L"-f", L"script.sed", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "bar\n");
}

TEST(sed, in_place_edit) {
  TempDir tmp;
  tmp.write("a.txt", "foo\nbar\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-i", L"s/foo/baz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");

  auto content = tmp.read("a.txt");
  EXPECT_EQ_TEXT(content, "baz\nbar\n");
}

TEST(sed, extended_regex_option) {
  TempDir tmp;
  tmp.write("a.txt", "a1\nb2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"-E", L"s/([a-z])(\\d)/X\\2/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "X1\nX2\n");
}

TEST(sed, line_range_substitution) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"1,2s/o/O/g", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "One\ntwO\nthree\n");
}

TEST(sed, regex_range_delete) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\nbbb\nccc\nddd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"/bbb/,/ccc/d", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aaa\nddd\n");
}

TEST(sed, y_command_translate) {
  TempDir tmp;
  tmp.write("a.txt", "abc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"y/abc/xyz/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "xyz\n");
}

TEST(sed, last_line_address) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"$s/o/O/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\nthree\n");
}

TEST(sed, semicolon_multiple_commands) {
  TempDir tmp;
  tmp.write("a.txt", "ab\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/a/A/;s/b/B/", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "AB\n");
}

TEST(sed, quit_command) {
  TempDir tmp;
  tmp.write("a.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"2q", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\n");
}

TEST(sed, wildcard_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello world\n");
  tmp.write("file2.txt", "hello there\n");
  tmp.write("other.log", "hello log\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/hello/bye/", L"*.txt"});

  TEST_LOG_CMD_LIST("sed.exe", L"s/hello/bye/", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sed output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("bye world") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("bye there") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("bye log") == std::string::npos);
}

TEST(sed, wildcard_question_mark) {
  TempDir tmp;
  tmp.write("a1.txt", "foo bar\n");
  tmp.write("a2.txt", "foo baz\n");
  tmp.write("a10.txt", "foo qux\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/foo/REPLACED/", L"a?.txt"});

  TEST_LOG_CMD_LIST("sed.exe", L"s/foo/REPLACED/", L"a?.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sed output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("REPLACED bar") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("REPLACED baz") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("REPLACED qux") == std::string::npos);
}
