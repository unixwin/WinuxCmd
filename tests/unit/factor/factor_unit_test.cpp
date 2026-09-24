/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(factor, factor_basic) {
  Pipeline p;
  p.add(L"factor.exe", {L"12"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "12: 2 2 3\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(factor, factor_version_succeeds) {
  Pipeline p;
  p.add(L"factor.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("factor (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

// [#1002] GNU prints "0:"/"1:" with no factors.
TEST(factor, factor_zero_and_one_have_no_factors) {
  Pipeline zero;
  zero.add(L"factor.exe", {L"0"});
  auto r0 = zero.run();
  EXPECT_EQ(r0.exit_code, 0);
  EXPECT_EQ_TEXT(r0.stdout_text, "0:\n");

  Pipeline one;
  one.add(L"factor.exe", {L"1"});
  auto r1 = one.run();
  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "1:\n");
}

// [#1001] invalid input exits 1; valid operands are still factored.
TEST(factor, factor_invalid_operand_exits_one) {
  Pipeline bad;
  bad.add(L"factor.exe", {L"abc"});
  auto rb = bad.run();
  EXPECT_EQ(rb.exit_code, 1);
  EXPECT_CONTAINS(rb.stderr_text, "not a valid positive integer");

  Pipeline mixed;
  mixed.add(L"factor.exe", {L"12", L"abc", L"24"});
  auto rm = mixed.run();
  EXPECT_EQ(rm.exit_code, 1);
  EXPECT_EQ_TEXT(rm.stdout_text, "12: 2 2 3\n24: 2 2 2 3\n");
}

// [#1003] GNU accepts leading whitespace and a leading '+' sign.
TEST(factor, factor_accepts_leading_whitespace_and_plus_sign) {
  Pipeline spaced;
  spaced.add(L"factor.exe", {L" 12"});
  auto rs = spaced.run();
  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_EQ_TEXT(rs.stdout_text, "12: 2 2 3\n");

  Pipeline plus;
  plus.add(L"factor.exe", {L"+12"});
  auto rp = plus.run();
  EXPECT_EQ(rp.exit_code, 0);
  EXPECT_EQ_TEXT(rp.stdout_text, "12: 2 2 3\n");

  Pipeline padded_plus;
  padded_plus.add(L"factor.exe", {L"  +36"});
  auto rpp = padded_plus.run();
  EXPECT_EQ(rpp.exit_code, 0);
  EXPECT_EQ_TEXT(rpp.stdout_text, "36: 2 2 3 3\n");

  // Trailing characters are still rejected.
  Pipeline trailing;
  trailing.add(L"factor.exe", {L"12 "});
  auto rt = trailing.run();
  EXPECT_EQ(rt.exit_code, 1);
}

// [#1004] numbers beyond uint64 are factored via bignum arithmetic.
TEST(factor, factor_numbers_above_uint64) {
  Pipeline p;
  p.add(L"factor.exe", {L"18446744073709551616"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  std::string expected = "18446744073709551616:";
  for (int i = 0; i < 64; ++i) expected += " 2";
  expected += "\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);

  Pipeline semiprime;
  semiprime.add(L"factor.exe", {L"18446744073709551617"});
  auto rs = semiprime.run();
  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_EQ_TEXT(rs.stdout_text,
                 "18446744073709551617: 274177 67280421310721\n");
}

// [#1006] -h (and documented alias -e) select exponent form.
TEST(factor, factor_exponents_flag_forms) {
  Pipeline h;
  h.add(L"factor.exe", {L"-h", L"72"});
  auto rh = h.run();
  EXPECT_EQ(rh.exit_code, 0);
  EXPECT_EQ_TEXT(rh.stdout_text, "72: 2^3 3^2\n");

  Pipeline e;
  e.add(L"factor.exe", {L"-e", L"8"});
  auto re = e.run();
  EXPECT_EQ(re.exit_code, 0);
  EXPECT_EQ_TEXT(re.stdout_text, "8: 2^3\n");

  Pipeline lng;
  lng.add(L"factor.exe", {L"--exponents", L"8"});
  auto rl = lng.run();
  EXPECT_EQ(rl.exit_code, 0);
  EXPECT_EQ_TEXT(rl.stdout_text, "8: 2^3\n");
}

// [GNU] stdin is tokenized on whitespace; bad tokens error but valid
// tokens are still factored and the exit status is 1.
TEST(factor, factor_stdin_reads_whitespace_separated_tokens) {
  Pipeline p;
  p.add(L"factor.exe", {});
  p.set_stdin("12 abc\n24\n");
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "12: 2 2 3\n24: 2 2 2 3\n");
  EXPECT_CONTAINS(r.stderr_text, "not a valid positive integer");
}
