#include "framework/winuxtest.h"

TEST(egrep, egrep_entry_point_defaults_to_extended_regex) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\n^a+$\na+\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"egrep.exe", {L"^a+$", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aaa\n");
}

TEST(egrep, egrep_keeps_grep_options) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"egrep.exe", {L"-n", L"alpha|beta", L"a.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1:alpha\n2:beta\n");
}
