// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(hostname, hostname_basic) {
  Pipeline p;
  p.add(L"hostname.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Remove trailing newline
  std::string hostname = r.stdout_text;
  if (!hostname.empty() && hostname.back() == '\n') {
    hostname.pop_back();
  }
  EXPECT_FALSE(hostname.empty());
}

// [#1462] hostname.c: one operand is a set attempt, which fails
// ("cannot set name to ..."); it must not print the current name.
TEST(hostname, hostname_set_operand_fails) {
  Pipeline p;
  p.add(L"hostname.exe", {L"newname"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("cannot set name to 'newname'") !=
              std::string::npos);
}

// [#1462] hostname.c:91: a second operand is an extra operand.
TEST(hostname, hostname_extra_operand) {
  Pipeline p;
  p.add(L"hostname.exe", {L"one", L"two"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("extra operand") != std::string::npos);
}

TEST(hostname, hostname_long) {
  Pipeline p;
  p.add(L"hostname.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should return the full hostname
  EXPECT_FALSE(r.stdout_text.empty());
}

// ---------------------------------------------------------------------------
// Issue #1522: a -F FILE whose name is not ASCII (U+00FF) must open
// through the wide path operand; the narrow std::ifstream ctor
// decoded the UTF-8 spelling through the ANSI code page and the open
// failed. The stream stays in TEXT mode: the first line is printed
// verbatim, so a CRLF file must print without the '\r' -- that is
// exactly what the text-mode translation has always produced.
// ---------------------------------------------------------------------------
TEST(hostname, hostname_file_opens_non_ascii_filename_issue1522) {
  TempDir tmp;
  const std::wstring name = L"\xFF.txt";
  {
    std::ofstream out(tmp.path / name, std::ios::binary);
    out << "myhost\r\n";
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hostname.exe", {L"-F", name});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "myhost\n");
}
