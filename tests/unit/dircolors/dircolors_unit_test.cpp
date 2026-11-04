/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(dircolors, dircolors_default_bourne) {
  Pipeline p;
  p.add(L"dircolors.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output Bourne shell code with LS_COLORS
  EXPECT_TRUE(r.stdout_text.find("LS_COLORS") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("export LS_COLORS") != std::string::npos);
}

TEST(dircolors, dircolors_csh) {
  Pipeline p;
  p.add(L"dircolors.exe", {L"-c"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output C shell code with setenv
  EXPECT_TRUE(r.stdout_text.find("setenv LS_COLORS") != std::string::npos);
}

TEST(dircolors, dircolors_print_database) {
  Pipeline p;
  p.add(L"dircolors.exe", {L"-p"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output the default color database
  EXPECT_TRUE(r.stdout_text.find("Configuration file for dircolors") !=
              std::string::npos);
  // [GNU] -p prints the built-in database with long-form keywords.
  EXPECT_TRUE(r.stdout_text.find("DIR 01;34") != std::string::npos);
}

TEST(dircolors, dircolors_print_ls_colors) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.add(L"dircolors.exe", {L"--print-ls-colors"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\x1b[01;34mdi\t01;34\x1b[0m\n") !=
              std::string::npos);
}

TEST(dircolors, print_ls_colors_uses_display_format_for_custom_database) {
  Pipeline p;
  p.set_env(L"TERM", L"xterm");
  p.set_stdin("TERM xterm\nDIR 01;34\n");
  p.add(L"dircolors.exe", {L"--print-ls-colors", L"-"});
  const auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "\x1b[01;34mdi\t01;34\x1b[0m\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

// ---------------------------------------------------------------------------
// Issue #1522: a database FILE whose name is not ASCII (U+00FF) must
// open through the wide path operand; the narrow std::ifstream ctor
// decoded the UTF-8 spelling through the ANSI code page and the open
// failed. The stream stays in text mode (CRLF semantics preserved --
// parse_db_line treats '\r' as whitespace either way), so a CRLF
// database parses exactly like the LF spelling.
// ---------------------------------------------------------------------------
TEST(dircolors, dircolors_opens_non_ascii_filename_issue1522) {
  TempDir tmp;
  const std::wstring name = L"\xFF.txt";
  {
    std::ofstream out(tmp.path / name, std::ios::binary);
    out << "DIR 01;34\r\nFILE 00\r\n";
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"TERM", L"xterm");
  p.add(L"dircolors.exe", {L"-b", name});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("LS_COLORS='di=01;34:fi=00:") !=
              std::string::npos);
}
