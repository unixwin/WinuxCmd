#include "framework/winuxtest.h"

TEST(mknod, mknod_missing_operand) {
  Pipeline p;
  p.add(L"mknod.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing operand"), std::string::npos);
}

TEST(mknod, mknod_invalid_type_fails) {
  Pipeline p;
  p.add(L"mknod.exe", {L"node1", L"x"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid device type"), std::string::npos);
}

TEST(mknod, mknod_block_requires_major_minor) {
  Pipeline p;
  p.add(L"mknod.exe", {L"node1", L"b"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("requires major and minor"), std::string::npos);
}

TEST(mknod, mknod_fifo_rejects_major_minor) {
  Pipeline p;
  p.add(L"mknod.exe", {L"node1", L"p", L"1", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("fifo type does not accept"), std::string::npos);
}

TEST(mknod, mknod_fifo_reports_windows_limitation) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mknod.exe", {L"node1", L"p"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(mknod, mknod_existing_path_fails_like_gnu_shape) {
  TempDir tmp;
  tmp.write("node1", "already here");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mknod.exe", {L"node1", L"p"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("File exists"), std::string::npos);
}
