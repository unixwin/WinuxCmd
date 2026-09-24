/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(tsort, tsort_basic) {
  Pipeline p;
  p.set_stdin("a b\nb c\n");
  p.add(L"tsort.exe", {});

  TEST_LOG_CMD_LIST("tsort.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tsort output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tsort, tsort_respects_dependency_edges) {
  Pipeline p;
  p.set_stdin("c b\nb a\n");
  p.add(L"tsort.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "c\nb\na\n");
}

TEST(tsort, tsort_self_pair_is_not_a_loop) {
  // [GNU] record_relation() ignores relations whose members are identical,
  // so "a a" is not a loop (uutils #8743).
  Pipeline p;
  p.set_stdin("a a\nb c\n");
  p.add(L"tsort.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\nc\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(tsort, tsort_duplicate_pair_is_silent) {
  // [GNU] repeated relations are recorded without a diagnostic.
  Pipeline p;
  p.set_stdin("a b\na b\n");
  p.add(L"tsort.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(tsort, tsort_empty_input_is_not_an_error) {
  Pipeline p;
  p.set_stdin("");
  p.add(L"tsort.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.empty());
}

// [GNU] -w is accepted and ignored (POSIX.1-2024) (issue #1085).
TEST(tsort, tsort_w_flag_is_noop) {
  Pipeline p;
  p.set_stdin("a b\nb c\n");
  p.add(L"tsort.exe", {L"-w"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\nc\n");
}
