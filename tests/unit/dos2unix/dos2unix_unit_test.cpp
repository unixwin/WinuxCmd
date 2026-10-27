/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(dos2unix, dos2unix_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\r\nline2\r\n");
  p.add(L"dos2unix.exe", {});

  TEST_LOG_CMD_LIST("dos2unix.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("dos2unix output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\n");
}

TEST(dos2unix, dos2unix_stdin_preserves_missing_final_newline) {
  Pipeline p;
  p.set_stdin("x");
  p.add(L"dos2unix.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "x");
}

// [#1211] piped stdin is a byte stream: fd 0 must not drop data at 0x1A.
TEST(dos2unix, dos2unix_stdin_bytes_issue1211) {
  const std::string payload(
      "a\r\nb\r\n\x1a"
      "c\r\n",
      10);
  const std::string expected(
      "a\nb\n\x1a"
      "c\n",
      7);
  Pipeline p;
  p.set_stdin(payload);
  p.add(L"dos2unix.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}

namespace dos2unix_issue1385 {
auto read_file(const std::filesystem::path& p) -> std::string {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}
}  // namespace dos2unix_issue1385

// #1385 (write-side residual): the read side already folded its operand, but
// the write side used to reopen the raw spelling — so with a shell root
// configured, `dos2unix /ra.txt` read inside the payload (shell) root while
// rewriting the converted copy on the current drive root. Both sides must
// open the folded operand; the drive spelling keeps resolving through the
// drive fold.
TEST(dos2unix, dos2unix_write_side_uses_shell_root_issue1385) {
  TempDir payload;
  payload.write("ra.txt", "a\r\nb\r\n");
  TempDir elsewhere;

  Pipeline root_spelling;
  root_spelling.set_cwd(elsewhere.wpath());
  root_spelling.set_env(L"WINUXSH_ROOT", payload.wpath());
  root_spelling.add(L"dos2unix.exe", {L"/ra.txt"});
  auto root_result = root_spelling.run();

  EXPECT_EQ(root_result.exit_code, 0);
  EXPECT_EQ_TEXT(dos2unix_issue1385::read_file(payload.path / L"ra.txt"),
                 "a\nb\n");
  // The raw spelling must not have landed a converted copy on the cwd.
  EXPECT_FALSE(std::filesystem::exists(elsewhere.path / L"ra.txt"));

  // The drive spelling of the same file keeps resolving through the fold.
  const std::wstring generic = (payload.path / L"drive.txt").generic_wstring();
  std::wstring drive_spelled_arg = L"/";
  drive_spelled_arg.push_back(
      static_cast<wchar_t>(std::tolower(generic.front())));
  drive_spelled_arg += generic.substr(2);
  payload.write("drive.txt", "x\r\n");

  Pipeline drive_spelled;
  drive_spelled.set_cwd(elsewhere.wpath());
  drive_spelled.set_env(L"WINUXSH_ROOT", payload.wpath());
  drive_spelled.add(L"dos2unix.exe", {drive_spelled_arg});
  auto drive_result = drive_spelled.run();

  EXPECT_EQ(drive_result.exit_code, 0);
  EXPECT_EQ_TEXT(dos2unix_issue1385::read_file(payload.path / L"drive.txt"),
                 "x\n");
}

TEST(dos2unix, dos2unix_relative_operand_unchanged_issue1385) {
  TempDir tmp;
  tmp.write("nearby.txt", "a\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dos2unix.exe", {L"nearby.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(dos2unix_issue1385::read_file(tmp.path / L"nearby.txt"),
                 "a\n");
}
