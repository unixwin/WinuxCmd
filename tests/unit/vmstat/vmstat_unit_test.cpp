// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

int count_lines(const std::string& text) {
  int lines = 0;
  for (char c : text) {
    if (c == '\n') ++lines;
  }
  return lines;
}

}  // namespace

// Default report: procps vmstat.c header block and field row.
TEST(vmstat, vmstat_default_report) {
  Pipeline p;
  p.add(L"vmstat.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("procs -----------memory----------") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("swpd") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("buff") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cache") != std::string::npos);
}

// `vmstat 1 2` prints the since-boot row plus one delta row.
TEST(vmstat, vmstat_delay_and_count) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"1", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 2 header lines + 2 data rows.
  EXPECT_EQ(count_lines(r.stdout_text), 4);
}

// -y suppresses the since-boot row: `vmstat -y 1 2` prints 2 rows.
TEST(vmstat, vmstat_no_first) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"-y", L"1", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(count_lines(r.stdout_text), 4);
}

// -s prints the event counter summary.
TEST(vmstat, vmstat_stats) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"-s"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("total memory") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("total swap") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("CPU context switches") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("forks") != std::string::npos);
}

// -f prints only the fork count and exits (procps vmstat.c handles it
// during option parsing).
TEST(vmstat, vmstat_forks_only) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"-f"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("forks") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("procs ---") == std::string::npos);
}

// -d / -D print disk statistics.
TEST(vmstat, vmstat_disk_modes) {
  {
    Pipeline p;
    p.add(L"vmstat.exe", {L"-d"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("disk- ") != std::string::npos);
  }
  {
    Pipeline p;
    p.add(L"vmstat.exe", {L"-D"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("disks") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("total reads") != std::string::npos);
  }
}

// -m prints the slabinfo header.
TEST(vmstat, vmstat_slabs_header) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"-m"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Cache") != std::string::npos);
}

// An unknown -S unit is an error (procps vmstat.c message shape).
TEST(vmstat, vmstat_bad_unit) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"-S", L"z"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("-S requires k, K, m or M") !=
              std::string::npos);
}

// A zero delay is rejected.
TEST(vmstat, vmstat_zero_delay) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("delay must be positive integer") !=
              std::string::npos);
}

// An unknown partition is reported and exits 1.
TEST(vmstat, vmstat_partition_not_found) {
  Pipeline p;
  p.add(L"vmstat.exe", {L"-p", L"NoSuchVolume12345"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("not found") != std::string::npos);
}
