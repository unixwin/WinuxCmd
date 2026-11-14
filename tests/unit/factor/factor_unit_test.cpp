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

// [#1004] re-pinned for #1488 wave-W1: the 1.x bignum path factored numbers
// beyond uint64. The rust2 subset covers 0..=u64::MAX exactly and rejects
// larger operands with the invalid-operand diagnostic rc 1 — the documented
// deviation recorded by gate row fa24-beyond-u64 (`note`, never gating) and
// by the module header. GNU factors these via GMP; restoring that is a
// command-impl item, not a wiring item.
TEST(factor, factor_numbers_above_uint64_are_rejected) {
  Pipeline p;
  p.add(L"factor.exe", {L"18446744073709551616"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("is not a valid positive integer") !=
              std::string::npos);

  Pipeline semiprime;
  semiprime.add(L"factor.exe", {L"18446744073709551617"});
  auto rs = semiprime.run();
  EXPECT_EQ(rs.exit_code, 1);
  EXPECT_TRUE(rs.stdout_text.empty());
}

// [#1006] -h selects exponent form. The 1.x `-e` alias is RETIRED by the
// #1488 wave-W1 wiring (GNU factor 9.x has no -e: "invalid option -- 'e'",
// rc 1, probed coreutils 9.4), matching the retired `-e` help line.
TEST(factor, factor_exponents_flag_forms) {
  Pipeline h;
  h.add(L"factor.exe", {L"-h", L"72"});
  auto rh = h.run();
  EXPECT_EQ(rh.exit_code, 0);
  EXPECT_EQ_TEXT(rh.stdout_text, "72: 2^3 3^2\n");

  Pipeline e;
  e.add(L"factor.exe", {L"-e", L"8"});
  auto re = e.run();
  EXPECT_EQ(re.exit_code, 1);
  EXPECT_TRUE(re.stdout_text.empty());
  EXPECT_TRUE(re.stderr_text.find("invalid option") != std::string::npos);

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

// [#1212] text mode dropped "20 30" after the 0x1A; 30 must still be factored.
TEST(factor, factor_stdin_ctrlz_not_truncated_issue1212) {
  Pipeline p;
  p.set_stdin(
      std::string("12 18\x1a"
                  "20 30\n",
                  12));
  p.add(L"factor.exe", {});
  auto r = p.run();

  EXPECT_TRUE(r.stdout_text.find("30:") != std::string::npos);
}

// [#1462] GNU factor.c:125 reads stdin tokens with DELIM "\n\t " only, so
// a CR stays inside the token and makes it invalid (it is not a factor
// separator); a token-initial tab is likewise not skipped by
// print_factors (only ' ' is).
TEST(factor, factor_stdin_cr_token_is_invalid) {
  Pipeline p;
  p.set_stdin("12\r\n");
  p.add(L"factor.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "not a valid positive integer");
}

TEST(factor, factor_leading_tab_operand_is_invalid) {
  Pipeline p;
  p.add(L"factor.exe", {L"\t12"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "not a valid positive integer");
}
