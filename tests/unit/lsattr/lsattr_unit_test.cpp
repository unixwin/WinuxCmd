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

// [#1462] -l/--long must hold for plain file operands too; the
// historical build honored it only for directory children.
TEST(lsattr, lsattr_long_names_for_file_operand_issue1462) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  ASSERT_TRUE(tmp.add_attrs("file.txt", FILE_ATTRIBUTE_READONLY));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"lsattr.exe", {L"-l", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "readonly");
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
