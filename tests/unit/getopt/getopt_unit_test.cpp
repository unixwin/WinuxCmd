#include "framework/winuxtest.h"

// [util-linux] Enhanced mode is selected by -o/--options (or any first
// argument starting with '-'); normalized output is a leading-space-joined
// token list: bare option tokens, shell-quoted option arguments and
// operands, and a bare "--" separator before the permuted operands.
// An optstring-first invocation ("getopt ab:c ...") selects the old-style
// compatibility mode with unquoted output.

TEST(getopt, getopt_parses_short_options_after_double_dash) {
  Pipeline p;
  p.add(L"getopt.exe",
        {L"-o", L"ab:c", L"--", L"-a", L"-b", L"value", L"file"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, " -a -b 'value' -- 'file'\n");
}

TEST(getopt, getopt_supports_attached_required_argument) {
  Pipeline p;
  p.add(L"getopt.exe", {L"-o", L"ab:c", L"--", L"-abvalue", L"file"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, " -a -b 'value' -- 'file'\n");
}

TEST(getopt, getopt_reports_unknown_option) {
  Pipeline p;
  p.add(L"getopt.exe", {L"-o", L"ab:", L"--", L"-z"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "invalid option");
}

TEST(getopt, getopt_unquoted_mode_outputs_plain_tokens) {
  Pipeline p;
  p.add(L"getopt.exe", {L"-u", L"-o", L"ab:", L"--", L"-b", L"value"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, " -b value --\n");
}

TEST(getopt, getopt_compatibility_mode_outputs_unquoted) {
  Pipeline p;
  // [util-linux] a first parameter not starting with '-' is the optstring
  // and selects unquoted compatible-mode output.
  p.add(L"getopt.exe", {L"ab:c", L"-a", L"-b", L"value", L"file"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, " -a -b value -- file\n");
}

TEST(getopt, getopt_test_option_exits_four) {
  Pipeline p;
  p.add(L"getopt.exe", {L"-T"});

  auto r = p.run();

  // [util-linux] -T tests whether this is the enhanced getopt: exit 4.
  EXPECT_EQ(r.exit_code, 4);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}
