#include "framework/winuxtest.h"

TEST(lsattr, lsattr_prints_windows_attribute_flags_for_file) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  ASSERT_TRUE(tmp.add_attrs("file.txt",
                            FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsattr.exe", {L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "RH");
  EXPECT_CONTAINS(r.stdout_text, "file.txt");
}

TEST(lsattr, lsattr_directory_option_lists_directory_itself) {
  TempDir tmp;
  tmp.mkdir("dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsattr.exe", {L"-d", L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "D");
  EXPECT_CONTAINS(r.stdout_text, "dir");
}
