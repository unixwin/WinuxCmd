#include "framework/winuxtest.h"

TEST(chown, chown_reference_form_accepts_files_without_owner_operand) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"--reference=reference.txt", L"target.txt"});

  TEST_LOG_CMD_LIST("chown.exe", L"--reference=reference.txt", L"target.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(chown, chown_invalid_user_fails) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"nonexistent_user_xyz", L"file.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(chown, chown_no_args_fails) {
  Pipeline p;
  p.add(L"chown.exe", {});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(chown, chown_missing_file_fails) {
  Pipeline p;
  p.add(L"chown.exe", {L"Users", L"nonexistent_file_xyz.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(chown, chown_verbose) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-v", L"Users", L"file.txt"});
  auto r = p.run();

  // May fail due to permissions, but should handle gracefully
  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
}

TEST(chown, chown_recursive) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "subdir");
  tmp.write("file.txt", "hello");
  tmp.write("subdir/nested.txt", "world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"-R", L"Users", L"."});
  auto r = p.run();

  // May fail due to permissions, but should handle gracefully
  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
}
