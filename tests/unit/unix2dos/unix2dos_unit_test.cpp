/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include <fstream>

#include "framework/winuxtest.h"

TEST(unix2dos, unix2dos_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"unix2dos.exe", {});

  TEST_LOG_CMD_LIST("unix2dos.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("unix2dos output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\r\n") != std::string::npos);
}

// [#1211] piped stdin is a byte stream: fd 0 must not drop data at 0x1A.
TEST(unix2dos, unix2dos_stdin_bytes_issue1211) {
  const std::string payload(
      "a\nb\n\x1a"
      "c\n",
      7);
  const std::string expected(
      "a\r\nb\r\n\x1a"
      "c\r\n",
      10);
  Pipeline p;
  p.set_stdin(payload);
  p.add(L"unix2dos.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}

namespace unix2dos_issue1385 {
auto read_file(const std::filesystem::path& p) -> std::string {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}
}  // namespace unix2dos_issue1385

// #1385 (enclave rule): with a shell root configured, a file operand spelled
// from the root (`unix2dos /ra.txt`) reads and rewrites inside the payload
// (shell) root, never on the current drive root; the drive spelling of the
// same file still resolves through the drive fold, and relative operands are
// unchanged.
TEST(unix2dos, unix2dos_file_operand_uses_shell_root_issue1385) {
  TempDir payload;
  payload.write("ra.txt", "a\nb\n");
  TempDir elsewhere;

  Pipeline root_spelling;
  root_spelling.set_cwd(elsewhere.wpath());
  root_spelling.set_env(L"WINUXSH_ROOT", payload.wpath());
  root_spelling.add(L"unix2dos.exe", {L"/ra.txt"});
  auto root_result = root_spelling.run();

  EXPECT_EQ(root_result.exit_code, 0);
  EXPECT_EQ_TEXT(unix2dos_issue1385::read_file(payload.path / L"ra.txt"),
                 "a\r\nb\r\n");
  // The raw spelling must not have landed a stray copy on the cwd's drive.
  EXPECT_FALSE(std::filesystem::exists(elsewhere.path / L"ra.txt"));

  // The drive spelling of the same file keeps resolving through the fold.
  const std::wstring generic = (payload.path / L"drive.txt").generic_wstring();
  std::wstring drive_spelled_arg = L"/";
  drive_spelled_arg.push_back(
      static_cast<wchar_t>(std::tolower(generic.front())));
  drive_spelled_arg += generic.substr(2);
  payload.write("drive.txt", "x\n");

  Pipeline drive_spelled;
  drive_spelled.set_cwd(elsewhere.wpath());
  drive_spelled.set_env(L"WINUXSH_ROOT", payload.wpath());
  drive_spelled.add(L"unix2dos.exe", {drive_spelled_arg});
  auto drive_result = drive_spelled.run();

  EXPECT_EQ(drive_result.exit_code, 0);
  EXPECT_EQ_TEXT(unix2dos_issue1385::read_file(payload.path / L"drive.txt"),
                 "x\r\n");
}

TEST(unix2dos, unix2dos_relative_operand_unchanged_issue1385) {
  TempDir tmp;
  tmp.write("nearby.txt", "a\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"unix2dos.exe", {L"nearby.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(unix2dos_issue1385::read_file(tmp.path / L"nearby.txt"),
                 "a\r\n");
}
