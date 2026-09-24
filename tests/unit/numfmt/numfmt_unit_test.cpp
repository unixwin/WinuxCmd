/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(numfmt, numfmt_basic) {
  Pipeline p;
  p.set_stdin("1000\n2000\n");
  p.add(L"numfmt.exe", {});

  TEST_LOG_CMD_LIST("numfmt.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("numfmt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(numfmt, numfmt_from_iec) {
  Pipeline p;
  p.set_stdin("1.5K\n2.0M\n");
  p.add(L"numfmt.exe", {L"--from=iec"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1536") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2097152") != std::string::npos);
}

TEST(numfmt, numfmt_auto_uses_decimal_for_large_suffixes) {
  Pipeline p;
  p.set_stdin("1T\n");
  p.add(L"numfmt.exe", {L"--from=auto"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1000000000000") != std::string::npos);
}

TEST(numfmt, numfmt_to_iec) {
  Pipeline p;
  p.set_stdin("1536\n2097152\n");
  p.add(L"numfmt.exe", {L"--to=iec"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1.5K") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2.0M") != std::string::npos);
}

TEST(numfmt, numfmt_to_iec_i_and_selected_field) {
  Pipeline p;
  p.set_stdin("value 1500\n");
  p.add(L"numfmt.exe", {L"--to=iec-i", L"--delimiter= ", L"--field=2"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "value 1.5Ki\n");
}

TEST(numfmt, numfmt_to_si_uses_decimal_scaling) {
  Pipeline p;
  p.set_stdin("1500\n");
  p.add(L"numfmt.exe", {L"--to=si"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  // [GNU] 8.32/9.4 oracles print an upper-case K for SI kilo.
  EXPECT_EQ(r.stdout_text, "1.5K\n");
}

TEST(numfmt, numfmt_format_preserves_numeric_value) {
  Pipeline p;
  p.set_stdin("42\n");
  p.add(L"numfmt.exe", {L"--format=%05.1f"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "042.0\n");
}

TEST(numfmt, numfmt_round_up) {
  Pipeline p;
  p.set_stdin("1.1K\n");
  p.add(L"numfmt.exe", {L"--from=iec", L"--round=up"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 1.1K = 1126.4 bytes, rounded up should be 1127
  EXPECT_TRUE(r.stdout_text.find("1127") != std::string::npos);
}

TEST(numfmt, numfmt_round_down) {
  Pipeline p;
  p.set_stdin("1.9K\n");
  p.add(L"numfmt.exe", {L"--from=iec", L"--round=down"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 1.9K = 1945.6 bytes, rounded down should be 1945
  EXPECT_TRUE(r.stdout_text.find("1945") != std::string::npos);
}

TEST(numfmt, numfmt_round_nearest) {
  Pipeline p;
  p.set_stdin("1.5K\n");
  p.add(L"numfmt.exe", {L"--from=iec", L"--round=nearest"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // 1.5K = 1536 bytes, nearest is 1536
  EXPECT_TRUE(r.stdout_text.find("1536") != std::string::npos);
}

TEST(numfmt, numfmt_padding) {
  Pipeline p;
  p.set_stdin("42\n");
  p.add(L"numfmt.exe", {L"--padding=10"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should have leading spaces
  EXPECT_TRUE(r.stdout_text.find("42") != std::string::npos);
}

TEST(numfmt, numfmt_header) {
  Pipeline p;
  p.set_stdin("HEADER\n1000\n2000\n");
  p.add(L"numfmt.exe", {L"--header=1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("HEADER") != std::string::npos);
}

TEST(numfmt, numfmt_invalid_input) {
  Pipeline p;
  p.set_stdin("notanumber\n");
  p.add(L"numfmt.exe", {});
  auto r = p.run();

  // Should report error for invalid input
  EXPECT_NE(r.exit_code, 0);
}

TEST(numfmt, numfmt_debug_reports_conversions) {
  Pipeline p;
  p.set_stdin("1500\n");
  p.add(L"numfmt.exe", {L"--to=si", L"--debug"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1.5K\n");
  EXPECT_NE(r.stderr_text.find("numfmt: debug: converted '1500' -> '1.5K'"),
            std::string::npos);
}

TEST(numfmt, numfmt_debug_reports_invalid_input) {
  Pipeline p;
  p.set_stdin("notanumber\n");
  p.add(L"numfmt.exe", {L"--debug"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_NE(
      r.stderr_text.find("numfmt: debug: failed to parse input 'notanumber'"),
      std::string::npos);
  EXPECT_NE(r.stderr_text.find("numfmt: invalid number: 'notanumber'"),
            std::string::npos);
}

// [GNU] numbers carrying a unit suffix are rejected unless a --from
// conversion was requested.
TEST(numfmt, numfmt_rejects_unit_suffix_without_from) {
  Pipeline p;
  p.set_stdin("1K\n");
  p.add(L"numfmt.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("rejecting suffix in input: '1K'"),
            std::string::npos);
}

// [GNU] unit sizes must be integers (optionally with a unit-size suffix);
// fractional values are rejected at option-parse time.
TEST(numfmt, numfmt_rejects_fractional_from_unit) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--from-unit=1.5", L"3"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_NE(r.stderr_text.find("invalid unit size: '1.5'"), std::string::npos);
}

// [GNU] scaled values are rounded at the requested precision only after
// scaling (issue: 10499 must round to 11K, not 10.5K).
TEST(numfmt, numfmt_to_si_rounds_after_scaling) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--to=si", L"10499"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "11K\n");
}

TEST(numfmt, numfmt_to_si_rounds_scaled_value_at_input_precision) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--to=si", L"1536"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.6K\n");
}

// [GNU] an explicit format's precision applies to the scaled value, and the
// unit suffix is appended after formatting.
TEST(numfmt, numfmt_to_si_with_zero_precision_format) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--to=si", L"--format=%.0f", L"1500"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2K\n");
}

TEST(numfmt, numfmt_to_iec_with_explicit_format) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--to=iec", L"--format=%.8f", L"1048576"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.00000000M\n");
}

// [GNU] format width pads the converted number; a leading '-' in the
// conversion left-justifies inside the field.
TEST(numfmt, numfmt_format_pads_converted_number) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--format=%10f", L"--from=si", L"--", L"-1.5K"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "     -1500\n");
}

TEST(numfmt, numfmt_format_zero_pads_after_sign) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--format=%010f", L"--from=si", L"--", L"-1.5K"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "-000001500\n");
}

// [GNU] without a conversion or format, the input precision is preserved.
TEST(numfmt, numfmt_preserves_input_precision) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"1.5"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.5\n");
}

TEST(numfmt, numfmt_from_auto_accepts_si_and_iec_suffixes) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--from=auto", L"1Ki"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1024\n");
}

// [GNU] --grouping is incompatible with --to and with --format.
TEST(numfmt, numfmt_rejects_grouping_with_to) {
  Pipeline p;
  p.add(L"numfmt.exe", {L"--grouping", L"--to=si", L"1500"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("grouping cannot be combined with --to"),
            std::string::npos);
}

TEST(numfmt, numfmt_grouping_reformats_only_integer_part) {
  Pipeline p;
  p.set_stdin("1234567.891\n");
  p.add(L"numfmt.exe", {L"--grouping"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1,234,567.891\n");
}
