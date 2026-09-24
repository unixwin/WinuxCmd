#include "framework/winuxtest.h"

TEST(mkfifo, mkfifo_missing_operand) {
  Pipeline p;
  p.add(L"mkfifo.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing operand"), std::string::npos);
}

TEST(mkfifo, mkfifo_invalid_mode_fails) {
  Pipeline p;
  p.add(L"mkfifo.exe", {L"-m", L"bad!mode", L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid mode"), std::string::npos);
}

TEST(mkfifo, mkfifo_existing_path_fails_like_gnu_shape) {
  TempDir tmp;
  tmp.write("pipe1", "already here");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("File exists"), std::string::npos);
}

TEST(mkfifo, mkfifo_creates_fifo_marker) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("pipe1"), "!<fifo>");
  EXPECT_NE(tmp.attrs("pipe1") & FILE_ATTRIBUTE_SYSTEM, 0u);
}

TEST(mkfifo, mkfifo_marker_is_seen_as_fifo) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});
  ASSERT_EQ(p.run().exit_code, 0);

  Pipeline t;
  t.set_cwd(tmp.wpath());
  t.add(L"test.exe", {L"-p", L"pipe1"});
  EXPECT_EQ(t.run().exit_code, 0);

  Pipeline tf;
  tf.set_cwd(tmp.wpath());
  tf.add(L"test.exe", {L"-f", L"pipe1"});
  EXPECT_EQ(tf.run().exit_code, 1);

  Pipeline s;
  s.set_cwd(tmp.wpath());
  s.add(L"stat.exe", {L"-c", L"%F", L"pipe1"});
  auto sr = s.run();
  EXPECT_EQ(sr.exit_code, 0);
  EXPECT_NE(sr.stdout_text.find("fifo"), std::string::npos);
}

TEST(mkfifo, mkfifo_multiple_operands_create_each) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1", L"pipe2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("pipe1"), "!<fifo>");
  EXPECT_EQ(tmp.read("pipe2"), "!<fifo>");
}

TEST(mkfifo, mkfifo_mode_umask_applies_readonly) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"-m", L"444", L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(tmp.attrs("pipe1") & FILE_ATTRIBUTE_READONLY, 0u);
}
