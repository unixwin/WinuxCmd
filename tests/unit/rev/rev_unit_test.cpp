#include "framework/winuxtest.h"

TEST(rev, rev_reverses_stdin_records) {
  Pipeline p;
  p.set_stdin("abc\nxy\n");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "cba\nyx\n");
}

TEST(rev, rev_preserves_unterminated_final_record) {
  Pipeline p;
  p.set_stdin("abc");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ(r.stdout_text, "cba");
}

TEST(rev, rev_reverses_multiple_files_in_order) {
  TempDir tmp;
  tmp.write("a.txt", "ab\n");
  tmp.write("b.txt", "cd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rev.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ba\ndc\n");
}

TEST(rev, rev_missing_file_reports_failure_but_continues) {
  TempDir tmp;
  tmp.write("ok.txt", "abc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rev.exe", {L"missing.txt", L"ok.txt"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "cba\n");
  EXPECT_CONTAINS(r.stderr_text, "missing.txt");
}

TEST(rev, rev_zero_option_uses_nul_separator) {
  std::string input("abc\0de\0tail", 12);
  std::string expected("cba\0ed\0liat", 12);

  Pipeline p;
  p.set_stdin(input);
  p.add(L"rev.exe", {L"-0"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_BYTES(r.stdout_text, expected);
}

TEST(rev, rev_crlf_input_preserves_carriage_return_as_data) {
  Pipeline p;
  p.set_stdin("ab\r\n");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ(r.stdout_text, "\rba\n");
}

// #1488 rust2 wiring: util-linux rev reverses whole UTF-8 character units
// when the effective LC_CTYPE locale (LC_ALL > LC_CTYPE > LANG) selects a
// multibyte locale, and reverses bytes under the C/POSIX default — the
// effective locale with none of the three set is "C" (setlocale(LC_ALL, ""
// fall-back), probed GNU rev 2.39.3). The 1.x body always reversed UTF-8
// units, a GNU deviation the rust2 port replaced with the locale model.
TEST(rev, rev_reverses_utf8_codepoint_units) {
  Pipeline p;
  p.set_stdin(std::string("\xC3\xA9x\n", 4));
  p.set_env(L"LC_ALL", L"C.UTF-8");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ(r.stdout_text, std::string("x\xC3\xA9\n", 4));
}

TEST(rev, rev_c_locale_reverses_bytes) {
  Pipeline p;
  p.set_stdin(std::string("\xC3\xA9x\n", 4));
  // LC_ALL=C pinned explicitly (a host LANG must not leak into the face);
  // the GNU default face (none of LC_ALL/LC_CTYPE/LANG set -> C) is
  // covered by the locale-model unit tests in rust2 rev.rs.
  p.set_env(L"LC_ALL", L"C");
  p.add(L"rev.exe", {});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  // byte-wise: the record body [0xc3 0xa9 'x'] reverses to ['x' 0xa9 0xc3]
  EXPECT_EQ(r.stdout_text, std::string("x\xA9\xC3\n", 4));
}

// #1488 rust2 wiring: file operands stay LITERAL (the b2sum_wildcard
// digest-family pin) — util-linux rev receives shell-expanded operands
// and `rev '*.txt'` reports `rev: cannot open *.txt: ...` rc 1. The 1.x
// body glob-expanded, a GNU deviation the rust2 port does not carry.
TEST(rev, rev_wildcard_file_operands_stay_literal) {
  TempDir tmp;
  tmp.write("a.txt", "ab\n");
  tmp.write("b.txt", "cd\n");
  tmp.write("skip.log", "ef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rev.exe", {L"*.txt"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "rev: cannot open *.txt:");
}
