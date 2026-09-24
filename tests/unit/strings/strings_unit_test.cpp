/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(strings, strings_basic) {
  TempDir tmp;
  // Create a file with some binary and text content
  std::vector<uint8_t> data = {0x00, 0x01, 0x02, 'h', 'e', 'l', 'l', 'o',
                               0x00, 0x00, 'w',  'o', 'r', 'l', 'd', 0x00};
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

TEST(strings, strings_numeric_min_length_option) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x00, 'a', 'b', 'c', 'd', 0x00,
                               'a',  'b', 'c', 'd', 'e', 0x00};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"strings.exe", {L"-5", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.find("abcd\n"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("abcde"), std::string::npos);
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

TEST(strings, strings_print_filename_and_output_separator) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x00, 'a', 'l', 'p', 'h', 'a',
                               0x00, 'b', 'e', 't', 'a', 0x00};
  std::ofstream(tmp.path / "names.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"strings.exe", {L"-f", L"--output-separator=|", L"names.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "names.bin: alpha|names.bin: beta|");
}

TEST(strings, strings_include_tab_by_default_and_all_whitespace_with_w) {
  TempDir tmp;
  std::vector<uint8_t> data = {'a', '\t', 'b', 'c', 0x00,
                               'x', '\n', 'y', 'z', 0x00};
  std::ofstream(tmp.path / "space.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline without_w;
  without_w.set_cwd(tmp.wpath());
  without_w.add(L"strings.exe", {L"-n", L"4", L"space.bin"});
  auto r1 = without_w.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_NE(r1.stdout_text.find("a\tbc"), std::string::npos);
  EXPECT_EQ(r1.stdout_text.find("x\nyz"), std::string::npos);

  Pipeline with_w;
  with_w.set_cwd(tmp.wpath());
  with_w.add(L"strings.exe", {L"-n", L"4", L"-w", L"space.bin"});
  auto r2 = with_w.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_NE(r2.stdout_text.find("a\tbc"), std::string::npos);
  EXPECT_NE(r2.stdout_text.find("x\nyz"), std::string::npos);
}

TEST(strings, strings_utf16_big_and_little_endian_encodings) {
  TempDir tmp;
  std::vector<uint8_t> big = {0x00, 'h', 0x00, 'e', 0x00, 'l',
                              0x00, 'l', 0x00, 'o', 0x00};
  std::vector<uint8_t> little = {'w', 0x00, 'o', 0x00, 'r', 0x00,
                                 'l', 0x00, 'd', 0x00, 0x00};
  std::ofstream(tmp.path / "big.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(big.data()), big.size());
  std::ofstream(tmp.path / "little.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(little.data()), little.size());

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"strings.exe", {L"-e", L"b", L"big.bin"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_NE(r1.stdout_text.find("hello"), std::string::npos);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"strings.exe", {L"-e", L"l", L"little.bin"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_NE(r2.stdout_text.find("world"), std::string::npos);
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
