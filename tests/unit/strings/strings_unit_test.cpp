/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(strings, strings_basic) {
  TempDir tmp;
  // Create a file with some binary and text content
  std::vector<uint8_t> data = {0x00, 0x01, 0x02, 'h', 'e', 'l', 'l', 'o',
                                0x00, 0x00, 'w', 'o', 'r', 'l', 'd', 0x00};
  std::ofstream(tmp.path / "binary.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"strings.exe", {L"binary.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
}

TEST(strings, strings_min_length) {
  TempDir tmp;
  std::vector<uint8_t> data = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"strings.exe", {L"-n", L"5", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should only show strings of at least 5 chars
  EXPECT_TRUE(r.stdout_text.find("abcde") != std::string::npos);
}

TEST(strings, strings_hex_offset) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x00, 'h', 'e', 'l', 'l', 'o', 0x00};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"strings.exe", {L"-t", L"x", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show hex offset
  EXPECT_TRUE(r.stdout_text.find("1") != std::string::npos);
}

TEST(strings, strings_octal_offset) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x00, 'h', 'e', 'l', 'l', 'o', 0x00};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"strings.exe", {L"-o", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show octal offset
}

TEST(strings, strings_stdin) {
  Pipeline p;
  p.set_stdin(std::string("hello\0world", 11));
  p.add(L"strings.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(strings, strings_no_strings_found) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x00, 0x01, 0x02, 0x03};
  std::ofstream(tmp.path / "binary.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"strings.exe", {L"binary.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // No printable strings, so empty output
}

TEST(strings, strings_nonexistent_file) {
  Pipeline p;
  p.add(L"strings.exe", {L"nonexistent_file_xyz.bin"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
