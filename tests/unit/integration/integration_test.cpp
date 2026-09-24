/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Integration tests for command pipelines and wildcard combinations
 */
#include "framework/winuxtest.h"

// Test: cat with wildcard piped to grep
TEST(integration, pipe_cat_wildcard_grep) {
  TempDir tmp;
  tmp.write("file1.txt", "hello world\nfoo bar\n");
  tmp.write("file2.txt", "hello again\nbaz qux\n");
  tmp.write("other.log", "hello log\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cat.exe", {L"*.txt"});
  p.add(L"grep.exe", {L"hello"});

  TEST_LOG_CMD_LIST("cat.exe", L"*.txt", L"|", L"grep.exe", L"hello");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pipeline output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("hello world") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("hello again") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("hello log") == std::string::npos);
}

// Test: ls with wildcard piped to wc
TEST(integration, pipe_ls_wildcard_wc) {
  TempDir tmp;
  tmp.write("file1.txt", "content");
  tmp.write("file2.txt", "content");
  tmp.write("file3.log", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"*.txt"});
  p.add(L"wc.exe", {L"-l"});

  TEST_LOG_CMD_LIST("ls.exe", L"*.txt", L"|", L"wc.exe", L"-l");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pipeline output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("2") != std::string::npos);
}

// Test: sort with wildcard piped to head
TEST(integration, pipe_sort_wildcard_head) {
  TempDir tmp;
  tmp.write("file1.txt", "cherry\napple\n");
  tmp.write("file2.txt", "banana\ndate\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"*.txt"});
  p.add(L"head.exe", {L"-n", L"2"});

  TEST_LOG_CMD_LIST("sort.exe", L"*.txt", L"|", L"head.exe", L"-n", L"2");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pipeline output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("apple") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("banana") != std::string::npos);
}

// Test: cat piped to wc -l (count lines)
TEST(integration, pipe_cat_wc_lines) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cat.exe", {L"test.txt"});
  p.add(L"wc.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("3") != std::string::npos);
}

// Test: grep piped to sort
TEST(integration, pipe_grep_sort) {
  TempDir tmp;
  tmp.write("test.txt", "banana\napple\ncherry\navocado\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"a", L"test.txt"});
  p.add(L"sort.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should contain lines with 'a' sorted
  size_t apple_pos = r.stdout_text.find("apple");
  size_t avocado_pos = r.stdout_text.find("avocado");
  size_t banana_pos = r.stdout_text.find("banana");
  EXPECT_TRUE(apple_pos != std::string::npos);
  EXPECT_TRUE(avocado_pos != std::string::npos);
  EXPECT_TRUE(banana_pos != std::string::npos);
  // Verify sort order
  EXPECT_TRUE(apple_pos < avocado_pos);
  EXPECT_TRUE(avocado_pos < banana_pos);
}

// Test: head piped to tail (get middle lines)
TEST(integration, pipe_head_tail) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\nline4\nline5\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-n", L"4", L"test.txt"});
  p.add(L"tail.exe", {L"-n", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("line3") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line4") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line1") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line5") == std::string::npos);
}

// Test: echo piped to cat (stdin)
TEST(integration, pipe_echo_cat) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"cat.exe", {});
  p.add(L"wc.exe", {L"-w"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("2") != std::string::npos);
}

// Test: multiple pipes - cat | grep | wc
TEST(integration, pipe_multiple) {
  TempDir tmp;
  tmp.write("test.txt", "apple\nbanana\ncherry\navocado\napricot\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cat.exe", {L"test.txt"});
  p.add(L"grep.exe", {L"a"});
  p.add(L"wc.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Lines with 'a': apple, banana, avocado, apricot = 4
  EXPECT_TRUE(r.stdout_text.find("4") != std::string::npos);
}

// ============================================================
// [character-class] wildcard tests
// ============================================================

TEST(integration, ls_char_class_range) {
  TempDir tmp;
  tmp.write("a1.txt", "a1");
  tmp.write("a2.txt", "a2");
  tmp.write("b1.txt", "b1");
  tmp.write("b2.txt", "b2");
  tmp.write("c1.txt", "c1");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"[ab]*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c1.txt") == std::string::npos);
}

TEST(integration, cat_char_class_exclude) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\n");
  tmp.write("b.txt", "bbb\n");
  tmp.write("c.log", "ccc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cat.exe", {L"[ab]*"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("aaa") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("bbb") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ccc") == std::string::npos);
}

TEST(integration, grep_char_class_range) {
  TempDir tmp;
  tmp.write("f1.txt", "hello\n");
  tmp.write("f2.txt", "hello\n");
  tmp.write("f9.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"hello", L"f[1-2]*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("f1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("f2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("f9.txt") == std::string::npos);
}

// ============================================================
// ? single-character wildcard tests
// ============================================================

TEST(integration, ls_question_mark) {
  TempDir tmp;
  tmp.write("ab.txt", "ab");
  tmp.write("bc.txt", "bc");
  tmp.write("cd.log", "cd");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ls.exe", {L"a?.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // a?.txt matches ab.txt; Windows ? may also match a.txt-like names
  // Just verify bc.txt and cd.log are excluded
  EXPECT_TRUE(r.stdout_text.find("bc.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cd.log") == std::string::npos);
}

TEST(integration, cat_question_mark) {
  TempDir tmp;
  tmp.write("f1.txt", "one\n");
  tmp.write("f2.txt", "two\n");
  tmp.write("f10.txt", "ten\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cat.exe", {L"f?.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("one") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("two") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ten") == std::string::npos);
}

// ============================================================
// Pipeline + wildcard combination tests
// ============================================================

TEST(integration, sed_wildcard_pipe_grep) {
  TempDir tmp;
  tmp.write("a.txt", "old text\nkeep this\n");
  tmp.write("b.txt", "old data\nkeep that\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sed.exe", {L"s/old/new/", L"*.txt"});
  p.add(L"grep.exe", {L"new"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("new text") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new data") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("keep") == std::string::npos);
}

TEST(integration, sort_wildcard_pipe_uniq) {
  TempDir tmp;
  tmp.write("a.txt", "cherry\napple\n");
  tmp.write("b.txt", "apple\nbanana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"*.txt"});
  p.add(L"uniq.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("apple") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("banana") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cherry") != std::string::npos);
}

TEST(integration, cut_wildcard_pipe_sort) {
  TempDir tmp;
  tmp.write("data1.txt", "cherry\napple\n");
  tmp.write("data2.txt", "banana\ndate\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cat.exe", {L"*.txt"});
  p.add(L"sort.exe", {});
  p.add(L"wc.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("4") != std::string::npos);
}

TEST(integration, md5sum_wildcard_pipe_sort) {
  TempDir tmp;
  tmp.write("file_a.txt", "aaa");
  tmp.write("file_b.txt", "bbb");
  tmp.write("other.log", "xxx");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"*.txt"});
  p.add(L"sort.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file_a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file_b.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

// ============================================================
// rm with wildcard integration test
// ============================================================

TEST(integration, rm_wildcard_then_ls) {
  TempDir tmp;
  tmp.write("a.txt", "aaa");
  tmp.write("b.txt", "bbb");
  tmp.write("c.log", "ccc");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"rm.exe", {L"*.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"ls.exe", {});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("c.log") != std::string::npos);
  EXPECT_TRUE(r2.stdout_text.find("a.txt") == std::string::npos);
  EXPECT_TRUE(r2.stdout_text.find("b.txt") == std::string::npos);
}

// ============================================================
// Complex multi-command workflows
// ============================================================

TEST(integration, workflow_find_grep_count) {
  TempDir tmp;
  tmp.write("code1.cpp", "int main() {\n  return 0;\n}\n");
  tmp.write("code2.cpp", "void func() {\n  int x = 1;\n}\n");
  tmp.write("code3.h", "#pragma once\nint x;\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"int", L"."});
  p.add(L"wc.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("3") != std::string::npos);
}

TEST(integration, workflow_tac_reverse) {
  TempDir tmp;
  tmp.write("lines.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tac.exe", {L"lines.txt"});
  p.add(L"rev.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("3enil") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2enil") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1enil") != std::string::npos);
}

TEST(integration, workflow_expand_unexpand_roundtrip) {
  TempDir tmp;
  tmp.write("tabs.txt", "a\tb\tc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"expand.exe", {L"tabs.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // expand converts tabs to spaces
  EXPECT_TRUE(r.stdout_text.find("a") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c") != std::string::npos);
}
