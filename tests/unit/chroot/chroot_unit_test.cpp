#include "framework/winuxtest.h"

TEST(chroot, chroot_missing_operand) {
  Pipeline p;
  p.add(L"chroot.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_NE(r.stderr_text.find("missing operand"), std::string::npos);
}

TEST(chroot, chroot_missing_directory_fails) {
  Pipeline p;
  p.add(L"chroot.exe", {L"definitely_missing_dir_xyz"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_NE(r.stderr_text.find("no such directory"), std::string::npos);
}

TEST(chroot, chroot_file_operand_fails) {
  TempDir tmp;
  tmp.write("not_a_dir.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chroot.exe", {L"not_a_dir.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_NE(r.stderr_text.find("no such directory"), std::string::npos);
}

TEST(chroot, chroot_existing_directory_reports_windows_limitation) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "jail");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chroot.exe", {L"jail", L"cmd.exe", L"/c", L"echo", L"hi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(chroot, chroot_option_surface_reports_windows_limitation) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "jail");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chroot.exe", {L"--userspec=user:group", L"--groups=users",
                        L"--skip-chdir", L"jail", L"cmd.exe", L"/c", L"echo",
                        L"hi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}
