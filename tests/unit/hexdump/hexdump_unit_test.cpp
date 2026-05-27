/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(hexdump, hexdump_canonical) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f,
                                0x72, 0x6c, 0x64, 0x0a};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-C", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Canonical format should show hex and ASCII
  EXPECT_TRUE(r.stdout_text.find("48 65 6c 6c 6f") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Hello") != std::string::npos);
}

TEST(hexdump, hexdump_hex2) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-x", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Two-byte hex format
}

TEST(hexdump, hexdump_octal1) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-b", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(hexdump, hexdump_char) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x0a, 0x09};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-c", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(hexdump, hexdump_length) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f,
                                0x72, 0x6c, 0x64};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-n", L"5", L"-C", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should only show first 5 bytes
}

TEST(hexdump, hexdump_skip) {
  TempDir tmp;
  std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f,
                                0x72, 0x6c, 0x64};
  std::ofstream(tmp.path / "test.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(data.data()), data.size());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-s", L"6", L"-C", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should skip first 6 bytes
}

TEST(hexdump, hexdump_stdin) {
  Pipeline p;
  p.set_stdin("Hello World");
  p.add(L"hexdump.exe", {L"-C"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(hexdump, hexdump_nonexistent_file) {
  Pipeline p;
  p.add(L"hexdump.exe", {L"nonexistent_file_xyz.bin"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
