// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(nproc, nproc_basic) {
  Pipeline p;
  p.add(L"nproc.exe", {});

  TEST_LOG_CMD_LIST("nproc.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nproc output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  int num = std::stoi(r.stdout_text);
  EXPECT_GT(num, 0);
}

TEST(nproc, nproc_all) {
  Pipeline p;
  p.add(L"nproc.exe", {L"--all"});

  TEST_LOG_CMD_LIST("nproc.exe", L"--all");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nproc output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  int num = std::stoi(r.stdout_text);
  EXPECT_GT(num, 0);
}

TEST(nproc, nproc_ignore) {
  Pipeline p;
  p.add(L"nproc.exe", {L"--ignore", L"1"});

  TEST_LOG_CMD_LIST("nproc.exe", L"--ignore", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nproc output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  int num = std::stoi(r.stdout_text);
  EXPECT_TRUE(num >= 1);
}

TEST(nproc, nproc_ignore_subtracts_processing_units) {
  Pipeline base;
  base.add(L"nproc.exe", {});
  auto base_result = base.run();
  EXPECT_EQ(base_result.exit_code, 0);
  if (base_result.exit_code != 0) return;
  int base_count = std::stoi(base_result.stdout_text);

  Pipeline ignored;
  ignored.add(L"nproc.exe", {L"--ignore", L"1"});
  auto ignored_result = ignored.run();

  EXPECT_EQ(ignored_result.exit_code, 0);
  int ignored_count = std::stoi(ignored_result.stdout_text);
  EXPECT_EQ(ignored_count, std::max(1, base_count - 1));
}

TEST(nproc, nproc_ignore_equals_form_subtracts_processing_units) {
  Pipeline base;
  base.add(L"nproc.exe", {});
  auto base_result = base.run();
  EXPECT_EQ(base_result.exit_code, 0);
  if (base_result.exit_code != 0) return;
  int base_count = std::stoi(base_result.stdout_text);

  Pipeline ignored;
  ignored.add(L"nproc.exe", {L"--ignore=1"});
  auto ignored_result = ignored.run();

  EXPECT_EQ(ignored_result.exit_code, 0);
  int ignored_count = std::stoi(ignored_result.stdout_text);
  EXPECT_EQ(ignored_count, std::max(1, base_count - 1));
}

TEST(nproc, nproc_rejects_extra_operand_like_gnu) {
  Pipeline p;
  p.add(L"nproc.exe", {L"extra"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("extra operand") != std::string::npos);
}

// [GNU] nproc honors the OpenMP environment: OMP_NUM_THREADS replaces the
// detected count outright (not clamped to it) and OMP_THREAD_LIMIT bounds
// the result; both are applied via the smaller valid value (#1028).
TEST(nproc, nproc_omp_num_threads_replaces_processor_count) {
  Pipeline p;
  p.set_env(L"OMP_NUM_THREADS", L"2");
  p.set_env(L"OMP_THREAD_LIMIT", L"");
  p.add(L"nproc.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n");
}

TEST(nproc, nproc_omp_thread_limit_bounds_the_result) {
  Pipeline p;
  p.set_env(L"OMP_NUM_THREADS", L"2");
  p.set_env(L"OMP_THREAD_LIMIT", L"1");
  p.add(L"nproc.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n");
}

TEST(nproc, nproc_omp_num_threads_is_not_clamped_to_processor_count) {
  Pipeline p;
  p.set_env(L"OMP_NUM_THREADS", L"999999");
  p.set_env(L"OMP_THREAD_LIMIT", L"");
  p.add(L"nproc.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "999999\n");
}

TEST(nproc, nproc_invalid_omp_values_are_ignored) {
  for (const wchar_t* value :
       {L"0", L"-2", L"+3", L"", L"abc", L"2x", L"2 x"}) {
    Pipeline p;
    p.set_env(L"OMP_NUM_THREADS", value);
    p.set_env(L"OMP_THREAD_LIMIT", L"");
    p.add(L"nproc.exe", {});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_GT(std::stoull(r.stdout_text), 2ull);
  }
}

TEST(nproc, nproc_omp_num_threads_accepts_whitespace_and_comma_lists) {
  for (const wchar_t* value : {L" 2 ", L"2,3", L"2,"}) {
    Pipeline p;
    p.set_env(L"OMP_NUM_THREADS", value);
    p.set_env(L"OMP_THREAD_LIMIT", L"");
    p.add(L"nproc.exe", {});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "2\n");
  }
}

TEST(nproc, nproc_all_ignores_openmp_environment) {
  Pipeline base;
  base.set_env(L"OMP_NUM_THREADS", L"");
  base.set_env(L"OMP_THREAD_LIMIT", L"");
  base.add(L"nproc.exe", {});
  auto base_result = base.run();
  EXPECT_EQ(base_result.exit_code, 0);

  Pipeline all;
  all.set_env(L"OMP_NUM_THREADS", L"2");
  all.set_env(L"OMP_THREAD_LIMIT", L"1");
  all.add(L"nproc.exe", {L"--all"});
  auto all_result = all.run();

  EXPECT_EQ(all_result.exit_code, 0);
  EXPECT_EQ_TEXT(all_result.stdout_text, base_result.stdout_text);
}

TEST(nproc, nproc_ignore_applies_after_openmp_selection) {
  Pipeline p;
  p.set_env(L"OMP_NUM_THREADS", L"2");
  p.set_env(L"OMP_THREAD_LIMIT", L"");
  p.add(L"nproc.exe", {L"--ignore", L"1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n");

  Pipeline clamped;
  clamped.set_env(L"OMP_NUM_THREADS", L"2");
  clamped.set_env(L"OMP_THREAD_LIMIT", L"");
  clamped.add(L"nproc.exe", {L"--ignore=5"});
  auto clamped_result = clamped.run();

  EXPECT_EQ(clamped_result.exit_code, 0);
  EXPECT_EQ_TEXT(clamped_result.stdout_text, "1\n");
}

TEST(nproc, nproc_invalid_ignore_value_exits_1) {
  for (const wchar_t* value : {L"abc", L"-1"}) {
    Pipeline p;
    p.add(L"nproc.exe", {L"--ignore", value});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 1);
    EXPECT_TRUE(r.stderr_text.find("invalid number") != std::string::npos);
  }
}
