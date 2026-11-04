// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(printf, printf_reuses_format_for_extra_arguments) {
  Pipeline p;
  p.add(L"printf.exe", {L"[%s]=%d\\n", L"one", L"1", L"two", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "[one]=1\n[two]=2\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_reuses_format_with_missing_final_argument_defaults) {
  Pipeline p;
  p.add(L"printf.exe", {L"%s:%d\\n", L"one", L"9", L"two"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one:9\ntwo:0\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_interprets_format_escapes) {
  Pipeline p;
  p.add(L"printf.exe", {L"A\\n\\x42\\101\\t%s\\n", L"done"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "A\nBA\tdone\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_percent_b_interprets_gnu_escapes_and_nul) {
  Pipeline p;
  p.add(L"printf.exe", {L"%b", L"hi\\n\\0\\x41\\0101"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("hi\n\0AA", 6));
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_backslash_c_stops_output_from_format) {
  Pipeline p;
  p.add(L"printf.exe", {L"%s\\c%s", L"left", L"right"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "left");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_backslash_c_stops_output_from_percent_b) {
  Pipeline p;
  p.add(L"printf.exe", {L"%b%s", L"left\\cright", L"later"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "left");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_respects_basic_width_and_precision) {
  Pipeline p;
  p.add(L"printf.exe",
        {L"|%6s|%-6.3s|%05d|%.2f|", L"cat", L"kitten", L"42", L"3.14159"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "|   cat|kit   |00042|3.14|");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_invalid_conversion_specification_is_unquoted) {
  Pipeline p;
  p.add(L"printf.exe", {L"%y"});

  auto r = p.run();

  // [GNU] printf.c:663: the directive is reported without quotes —
  // "printf: %y: invalid conversion specification" (coreutils 9.4
  // under LC_ALL=C, #1445).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text,
                 "printf: %y: invalid conversion specification\n");
}

TEST(printf, printf_warns_for_invalid_numeric_conversion) {
  Pipeline p;
  p.add(L"printf.exe", {L"%d:%g", L"abc", L"1.2tail"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "0:1.2");
  EXPECT_NE(r.stderr_text.find("expected a numeric value"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("value not completely converted"),
            std::string::npos);
}

TEST(printf, printf_dynamic_width_precision_and_shell_quote) {
  Pipeline p;
  p.add(L"printf.exe",
        {L"|%*d|%.*f|%q|", L"5", L"42", L"2", L"3.14159", L"a b"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "|   42|3.14|'a b'|");
}

// [GNU] printf.c never calls getopt: --help/--version are honored only as
// the sole argument; in every other position they are literal operands
// (Savannah #1194 class).

TEST(printf, printf_help_and_version_after_format_are_literal_operands) {
  Pipeline help;
  help.add(L"printf.exe", {L"%s\\n", L"--help"});
  auto hr = help.run();
  EXPECT_EQ(hr.exit_code, 0);
  EXPECT_EQ_TEXT(hr.stdout_text, "--help\n");
  EXPECT_EQ_TEXT(hr.stderr_text, "");

  Pipeline version;
  version.add(L"printf.exe", {L"%s\\n", L"--version"});
  auto vr = version.run();
  EXPECT_EQ(vr.exit_code, 0);
  EXPECT_EQ_TEXT(vr.stdout_text, "--version\n");
  EXPECT_EQ_TEXT(vr.stderr_text, "");
}

TEST(printf, printf_double_dash_makes_next_argument_the_format) {
  Pipeline p;
  p.add(L"printf.exe", {L"--", L"--help\\n"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "--help\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(printf, printf_leading_help_is_format_when_not_sole_argument) {
  Pipeline p;
  p.add(L"printf.exe", {L"--help", L"extra"});

  auto r = p.run();

  // GNU treats "--help" as the format string (and warns about the excess
  // argument); the exit status stays 0.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "--help");
}

TEST(printf, printf_sole_help_still_prints_usage) {
  Pipeline p;
  p.add(L"printf.exe", {L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Usage"), std::string::npos);
}

TEST(printf, printf_sole_version_still_prints_version) {
  Pipeline p;
  p.add(L"printf.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("printf (WinuxCmd)"), std::string::npos);
}

TEST(printf, printf_help_abbreviation_is_literal_format) {
  // [GNU] printf parses options directly precisely so that abbreviations
  // such as "--he" are the format string, not --help.
  Pipeline p;
  p.add(L"printf.exe", {L"--he"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "--he");
}

TEST(printf, printf_dash_arguments_after_format_are_literal) {
  // GNU: `printf '%d\n' -5` prints -5. GNU printf has no getopt layer at
  // all, so `--` is a literal operand too: `printf '%d\n' -- -5` prints
  // "0\n-5\n" (0 for the invalid number `--`) and fails.
  Pipeline negative;
  negative.add(L"printf.exe", {L"%d\\n", L"-5"});
  auto nr = negative.run();
  EXPECT_EQ(nr.exit_code, 0);
  EXPECT_EQ_TEXT(nr.stdout_text, "-5\n");

  Pipeline dd;
  dd.add(L"printf.exe", {L"%d\\n", L"--", L"-5"});
  auto dr = dd.run();
  EXPECT_EQ(dr.exit_code, 1);
  EXPECT_EQ_TEXT(dr.stdout_text, "0\n-5\n");

  // Even the -v extension is data once the format operand was seen.
  Pipeline v;
  v.add(L"printf.exe", {L"%s\\n", L"-v", L"x"});
  auto vr = v.run();
  EXPECT_EQ(vr.exit_code, 0);
  EXPECT_EQ_TEXT(vr.stdout_text, "-v\nx\n");
}

TEST(printf, printf_leading_v_extension_still_parses) {
  // WinuxCmd extension (not in standalone GNU printf): "-v VAR" ahead of
  // the format keeps working.
  Pipeline p;
  p.add(L"printf.exe", {L"-v", L"var", L"%s\\n", L"x"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "x\n");
}

// ---------------------------------------------------------------------------
// Issue #1368: printf.c registers close_stdout, so a failed stdout write
// surfaces as "printf: write error: <strerror>" with exit status 1. With
// stdout closed (`>&-`) the first write fails with ERROR_INVALID_HANDLE —
// the Win32 image of EBADF — and printf must no longer exit 0 silently.
// ---------------------------------------------------------------------------
TEST(printf, issue1368_closed_stdout_reports_write_error) {
  auto r = run_command_closed_stdout(ProjectPaths::exe(L"printf.exe"),
                                     {L"hi\\n"});

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_CONTAINS(r.stderr_text, "printf: write error: ");
  EXPECT_CONTAINS(r.stderr_text, "Bad file descriptor");
}

// ---------------------------------------------------------------------------
// Issue #471: printf %a renders in the glibc %La form (the leading
// digit is the significand's top nibble and the exponent is exp-3,
// e.g. 1.5 -> 0xcp-3), not the old newlib-style normalized 0x1.8p+0.
// The 8.32-vs-9.4 oracle question is settled: both print %a through
// the same strtold + %La chain, so the form is version-independent.
// ---------------------------------------------------------------------------
TEST(printf, printf_hex_float_glibc_form_issue471) {
  Pipeline p;
  p.add(L"printf.exe", {L"%a\\n%a\\n%a\\n", L"1.5", L"3", L"0.1"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0xcp-3\n0xcp-2\n0xc.ccccccccccccdp-7\n");
}

TEST(printf, printf_hex_float_precision_and_carry_issue471) {
  Pipeline p;
  p.add(L"printf.exe",
        {L"%.2a\\n%.0a\\n%#a\\n%A\\n", L"1.5", L"0xf.fffffffffffffffp-3",
         L"1.5", L"1.5"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  // %.0a of the all-ones significand carries out of the top nibble:
  // glibc renders digit 1 with the exponent raised by 4.
  EXPECT_EQ_TEXT(r.stdout_text, "0xc.00p-3\n0x1p+1\n0xc.p-3\n0XCp-3\n");
}

TEST(printf, printf_hex_float_wide_significand_issue471) {
  Pipeline p;
  p.add(L"printf.exe",
        {L"%a\\n%.20a\\n", L"0x1.23456789abcdef123456789p+100",
         L"0x1.23456789abcdef123456789p+100"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0x9.1a2b3c4d5e6f789p+97\n"
                 "0x9.1a2b3c4d5e6f78900000p+97\n");
}

TEST(printf, printf_hex_float_subnormal_issue471) {
  Pipeline p;
  p.add(L"printf.exe",
        {L"%a\\n%a\\n%.3a\\n", L"0x1p-16445",
         L"0x0.fffffffffffffffp-16382", L"0x0.123456789abcdefp-16382"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0x0.000000000000001p-16385\n"
                 "0x7.ffffffffffffff8p-16385\n"
                 "0x0.91ap-16385\n");
}

TEST(printf, printf_hex_float_zero_sign_width_issue471) {
  Pipeline p;
  p.add(L"printf.exe",
        {L"%a\\n%a\\n%a\\n%020a\\n", L"0", L"-0", L"-1.5", L"1.5"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "0x0p+0\n-0x0p+0\n-0xcp-3\n0x00000000000000cp-3\n");
}
