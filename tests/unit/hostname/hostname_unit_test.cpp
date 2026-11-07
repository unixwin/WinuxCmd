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
// hexdump (their pins stand). The incoherence was filed as #1526 and
// is FIXED here: -F/--file now runs GNU hostname.c's read_file()/
// set_name()/check_name() pipeline. This pin keeps the non-ASCII
// operand from the original #1522 probe -- it must open through the
// wide path (#1528, the narrow ctor decodes UTF-8 through the ANSI
// code page, the #342 class), succeed per GNU's set-name semantics
// (silent, exit 0; this build cannot actually sethostname on Windows)
// and print nothing.
// ---------------------------------------------------------------------------
TEST(hostname, hostname_file_reads_wide_path_operand_issue1526) {
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
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

// ---------------------------------------------------------------------------
// Issue #1526: GNU hostname.c (Debian) -F semantics, byte-probed
// against the WSL oracle 2026-11-07.
// ---------------------------------------------------------------------------
TEST(hostname, hostname_file_missing_reports_errno_shape) {
  Pipeline p;
  p.add(L"hostname.exe", {L"-F", L"no-such-host-file-xyz"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("hostname: No such file or directory") !=
              std::string::npos);
}

TEST(hostname, hostname_file_empty_is_invalid_name) {
  TempDir tmp;
  tmp.write("empty.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hostname.exe", {L"-F", L"empty.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "hostname: the specified hostname is invalid") !=
              std::string::npos);
}

TEST(hostname, hostname_file_trims_space_and_skips_comments_and_blank_lines) {
  TempDir tmp;
  // "  padded  \n\n" trims to "padded"; "# comment\nreal\n" skips the
  // comment line (hostname.c read_file: buf[0] == '#' or '\n').
  tmp.write("pad.txt", "  padded  \n\n");
  tmp.write("hash.txt", "# comment\nreal\n");
  tmp.write("blank.txt", "\n\nname2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hostname.exe", {L"-F", L"pad.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"hostname.exe", {L"--file=hash.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);

  Pipeline p3;
  p3.set_cwd(tmp.wpath());
  p3.add(L"hostname.exe", {L"-F", L"blank.txt"});
  auto r3 = p3.run();
  EXPECT_EQ(r3.exit_code, 0);
}

TEST(hostname, hostname_file_invalid_name_chars_rejected) {
  TempDir tmp;
  // "ab#cd" -> check_name() fails on '#' and never falls through to
  // the second line; a CRLF file trims the '\r' and succeeds.
  tmp.write("bad.txt", "ab#cd\nx\n");
  tmp.write("bang.txt", "bad!name\n");
  tmp.write("crlf.txt", "ok-name\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hostname.exe", {L"-F", L"bad.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "hostname: the specified hostname is invalid") !=
              std::string::npos);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"hostname.exe", {L"-F", L"bang.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 1);

  Pipeline p3;
  p3.set_cwd(tmp.wpath());
  p3.add(L"hostname.exe", {L"-F", L"crlf.txt"});
  auto r3 = p3.run();
  EXPECT_EQ(r3.exit_code, 0);
}

TEST(hostname, hostname_file_over_long_name_reports_name_too_long) {
  TempDir tmp;
  tmp.write("long.txt", std::string(99, 'a') + "\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hostname.exe", {L"-F", L"long.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("hostname: name too long") !=
              std::string::npos);
}

TEST(hostname, hostname_file_with_operand_is_usage_stderr_255) {
  TempDir tmp;
  tmp.write("hn.txt", "probe-host\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hostname.exe", {L"-F", L"hn.txt", L"extra"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 255);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find(
                  "Usage: hostname [-b] {hostname|-F file}") !=
              std::string::npos);
}
