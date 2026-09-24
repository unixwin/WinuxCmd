#include "framework/winuxtest.h"

TEST(fgrep, fgrep_entry_point_defaults_to_fixed_strings) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\n^a+$\na+\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fgrep.exe", {L"^a+$", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "^a+$\n");
}

TEST(fgrep, fgrep_keeps_grep_options) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\nalpha beta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"fgrep.exe", {L"-n", L"alpha", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1:alpha\n3:alpha beta\n");
}
