#include "framework/winuxtest.h"

TEST(getfacl, prints_acl_header_for_file) {
  TempDir tmp;
  auto file = tmp.write_file("file.txt", "data");

  Pipeline p;
  p.add(L"getfacl.exe", {file.wstring()});

  auto r = p.run();

  ASSERT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "# file:");
  EXPECT_CONTAINS(r.stdout_text, "# owner:");
  EXPECT_CONTAINS(r.stdout_text, "# group:");
}

TEST(getfacl, numeric_mode_prints_acl_header) {
  TempDir tmp;
  auto file = tmp.write_file("file.txt", "data");

  Pipeline p;
  p.add(L"getfacl.exe", {L"--numeric", file.wstring()});

  auto r = p.run();

  ASSERT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "# owner:");
  EXPECT_CONTAINS(r.stdout_text, "# group:");
}

TEST(getfacl, missing_operand_fails) {
  Pipeline p;
  p.add(L"getfacl.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "missing file operand");
}
