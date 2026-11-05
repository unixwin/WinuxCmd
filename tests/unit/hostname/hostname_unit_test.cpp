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
// Issue #1522, captain-corrected after the #1525 post-merge CI run:
// hostname has NO live -F reader to fix. The applet models coreutils
// hostname: build_config lists -F/--file in its unsupported-option
// rejection table and never populates Config::file, so `hostname -F`
// is rejected as an unknown option (exit 1, nothing on stdout) before
// any file access, and the file branch in run() is unreachable. The
// residuals lane's original pin asserted the file is read and
// printed; CI showed exit 1 / empty stdout -- the rejection working
// as designed. #1522's live narrow-ctor readers were dircolors and
// hexdump (their pins stand). The remaining incoherence -- help
// advertises -F while the parser rejects it and the read branch
// sits dead -- is filed as #1526. This pin asserts the designed
// rejection so the dead surface cannot silently change shape.
// ---------------------------------------------------------------------------
TEST(hostname, hostname_file_option_rejected_by_design_issue1522) {
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
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}
