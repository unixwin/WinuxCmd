
#include "framework/winuxtest.h"

TEST(dd, dd_copies_with_block_size_and_count) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghijkl");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=4", L"count=2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abcdefgh");
  EXPECT_TRUE(r.stderr_text.find("2 records in") != std::string::npos);
}

TEST(dd, dd_conv_sync_pads_short_input_blocks) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"bs=4", L"count=1", L"conv=sync"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("abc\0", 4));
  EXPECT_TRUE(r.stderr_text.find("4 bytes copied") != std::string::npos);
}

TEST(dd, dd_skip_uses_input_block_size) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghi");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"ibs=3", L"obs=2",
                    L"skip=1", L"count=1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "def");
}

TEST(dd, dd_zero_count_skip_and_seek_are_allowed) {
  TempDir tmp;
  tmp.write("input.bin", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=2", L"cbs=4",
                    L"count=0", L"skip=0", L"seek=0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "");
  EXPECT_TRUE(r.stderr_text.find("0 records in") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("0 records out") != std::string::npos);
}

TEST(dd, dd_seek_and_notrunc_preserve_existing_bytes) {
  TempDir tmp;
  tmp.write("input.bin", "zz");
  tmp.write("out.bin", "ABCDEFGH");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"obs=4", L"seek=1", L"conv=notrunc"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "ABCDzzGH");
}

TEST(dd, dd_status_none_suppresses_diagnostics) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1", L"status=none"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("out.bin"), "abc");
}

TEST(dd, dd_status_noxfer_omits_bytes_copied_line) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1", L"status=noxfer"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("records in") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("records out") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("bytes copied") == std::string::npos);
  EXPECT_EQ(tmp.read("out.bin"), "abc");
}

TEST(dd, dd_rejects_invalid_size_operand) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=bad"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid bs value") != std::string::npos);
}
