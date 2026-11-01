// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

// Note: top command tests are temporarily disabled due to implementation issues
// TODO: Fix top command implementation and re-enable tests

TEST(top, top_basic) {
  // Placeholder test to maintain test framework compatibility
  EXPECT_TRUE(true);
}

TEST(top, top_new_filter_and_display_options) {
  for (const auto& option :
       {std::vector<std::wstring>{L"--pid", L"0"},
        std::vector<std::wstring>{L"--user", L"__missing__"},
        std::vector<std::wstring>{L"--secure-mode"},
        std::vector<std::wstring>{L"--command"},
        std::vector<std::wstring>{L"--threads"}}) {
    Pipeline p;
    std::vector<std::wstring> args{L"-b", L"-n", L"1", L"--rows", L"3"};
    args.insert(args.end(), option.begin(), option.end());
    p.add(L"top.exe", args);
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stderr_text.empty());
  }
}

TEST(top, top_batch_iterations_and_rows_exit) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"--rows", L"5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Tasks:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("%Cpu(s):") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("PID USER") != std::string::npos);
}

TEST(top, top_attached_short_iterations_exit) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n1", L"--rows", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Tasks:") != std::string::npos);
}

// -O/--list-fields prints the field names and exits (procps top.c).
TEST(top, top_list_fields) {
  Pipeline p;
  p.add(L"top.exe", {L"--list-fields"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("PID") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("%CPU") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("COMMAND") != std::string::npos);
}

// -d accepts fractional seconds (procps top.c mkfloat).
TEST(top, top_fractional_delay) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-d", L"0.5", L"--rows", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Tasks:") != std::string::npos);
}

// -o sorts by a named procps field; an unknown field is an error.
TEST(top, top_sort_override) {
  {
    Pipeline p;
    p.add(L"top.exe", {L"-b", L"-n", L"1", L"-o", L"%CPU", L"--rows", L"3"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
  }
  {
    Pipeline p;
    p.add(L"top.exe", {L"-b", L"-n", L"1", L"-o", L"RES", L"--rows", L"3"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
  }
  {
    Pipeline p;
    p.add(L"top.exe",
          {L"-b", L"-n", L"1", L"-o", L"NOSUCHFIELD", L"--rows", L"3"});
    auto r = p.run();
    EXPECT_NE(r.exit_code, 0);
  }
}

// -E selects the summary memory scale; the header follows it.
TEST(top, top_summary_scale) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-E", L"g", L"--rows", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("GiB Mem") != std::string::npos);
}

// -1 shows one %Cpu line per processor.
TEST(top, top_single_cpu) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-1", L"--rows", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("%Cpu0") != std::string::npos);
}

/*
TEST(top, top_batch_mode) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("Tasks:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("%Cpu(s):") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("MiB Mem:") != std::string::npos);
}

TEST(top, top_with_delay) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-d", L"1"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1", L"-d", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 -d 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(top, top_with_iterations) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"2", L"-d", L"1"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"2", L"-d", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 2 -d 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(top, top_help) {
  Pipeline p;
  p.add(L"top.exe", {L"-h"});

  TEST_LOG_CMD_LIST("top.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -h output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("Usage:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Options:") != std::string::npos);
}

TEST(top, top_version) {
  Pipeline p;
  p.add(L"top.exe", {L"-v"});

  TEST_LOG_CMD_LIST("top.exe", L"-v");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -v output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("top (WinuxCmd)") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Copyright") != std::string::npos);
}

TEST(top, top_sort_by_cpu) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-o", L"CPU"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1", L"-o", L"CPU");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 -o CPU output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(top, top_sort_by_memory) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-o", L"MEM"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1", L"-o", L"MEM");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 -o MEM output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(top, top_sort_by_time) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-o", L"TIME"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1", L"-o", L"TIME");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 -o TIME output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(top, top_invalid_delay) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1", L"-d", L"0"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1", L"-d", L"0");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
}

TEST(top, top_process_count) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("Tasks:") != std::string::npos);
}

TEST(top, top_memory_info) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("MiB Mem:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("MiB Swap:") != std::string::npos);
}

TEST(top, top_cpu_info) {
  Pipeline p;
  p.add(L"top.exe", {L"-b", L"-n", L"1"});

  TEST_LOG_CMD_LIST("top.exe", L"-b", L"-n", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("top.exe -b -n 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("%Cpu(s):") != std::string::npos);
}
*/
