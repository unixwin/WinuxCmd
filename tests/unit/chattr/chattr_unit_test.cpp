#include "framework/winuxtest.h"

TEST(chattr, chattr_sets_and_clears_readonly_hidden_attributes) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"chattr.exe", {L"+RH", L"file.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
  }

  DWORD after_set = tmp.attrs("file.txt");
  ASSERT_NE(after_set, INVALID_FILE_ATTRIBUTES);
  EXPECT_TRUE((after_set & FILE_ATTRIBUTE_READONLY) != 0);
  EXPECT_TRUE((after_set & FILE_ATTRIBUTE_HIDDEN) != 0);

  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"chattr.exe", {L"-RH", L"file.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
  }

  DWORD after_clear = tmp.attrs("file.txt");
  ASSERT_NE(after_clear, INVALID_FILE_ATTRIBUTES);
  EXPECT_FALSE((after_clear & FILE_ATTRIBUTE_READONLY) != 0);
  EXPECT_FALSE((after_clear & FILE_ATTRIBUTE_HIDDEN) != 0);
}

TEST(chattr, chattr_recursive_applies_to_children) {
  TempDir tmp;
  tmp.mkdir("dir");
  tmp.write("dir/child.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chattr.exe", {L"-R", L"+I", L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  DWORD dir_attrs = tmp.attrs("dir");
  DWORD child_attrs = tmp.attrs("dir/child.txt");
  ASSERT_NE(dir_attrs, INVALID_FILE_ATTRIBUTES);
  ASSERT_NE(child_attrs, INVALID_FILE_ATTRIBUTES);
  EXPECT_TRUE((dir_attrs & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) != 0);
  EXPECT_TRUE((child_attrs & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) != 0);
}

TEST(chattr, chattr_recursive_negative_mode_is_not_parsed_as_options) {
  TempDir tmp;
  tmp.mkdir("dir");
  tmp.write("dir/child.txt", "content");
  DWORD child_attrs = tmp.attrs("dir/child.txt");
  ASSERT_NE(child_attrs, INVALID_FILE_ATTRIBUTES);
  ASSERT_TRUE(tmp.add_attrs("dir/child.txt", FILE_ATTRIBUTE_HIDDEN));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chattr.exe", {L"-R", L"-H", L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  DWORD after_clear = tmp.attrs("dir/child.txt");
  ASSERT_NE(after_clear, INVALID_FILE_ATTRIBUTES);
  EXPECT_FALSE((after_clear & FILE_ATTRIBUTE_HIDDEN) != 0);
}

TEST(chattr, chattr_rejects_unsupported_attribute) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chattr.exe", {L"+C", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "unsupported attribute");
}
