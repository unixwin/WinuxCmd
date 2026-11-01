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
  EXPECT_NE(r.stderr_text.find("missing operand after 'b'"),
            std::string::npos);
  EXPECT_NE(r.stderr_text.find("require major and minor device numbers"),
            std::string::npos);
}

TEST(mknod, mknod_fifo_rejects_major_minor) {
  Pipeline p;
  p.add(L"mknod.exe", {L"node1", L"p", L"1", L"2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("Fifos do not have"), std::string::npos);
}

TEST(mknod, mknod_fifo_creates_marker) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mknod.exe", {L"node1", L"p"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("node1"), "!<fifo>");

  Pipeline t;
  t.set_cwd(tmp.wpath());
  t.add(L"test.exe", {L"-p", L"node1"});
  EXPECT_EQ(t.run().exit_code, 0);
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

// ===== #1235 regression: POSIX drive-dialect operands =====
// Same rule as mkfifo (#1235): mknod handed the raw operand to the existence
// probe and create_winux_fifo_w, and Win32 resolved a leading "/e" against
// the CURRENT drive. The probes synthesize the dialect spelling from the
// TempDir's own drive so they pass on any host.
namespace mknod_dialect_detail {

auto drive_letter(const std::filesystem::path &path) -> wchar_t {
  wchar_t drive = path.wstring()[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  return drive;
}

auto slash_drive_form(const std::filesystem::path &path) -> std::wstring {
  std::wstring text = path.wstring();
  text[0] = drive_letter(path);
  text.erase(1, 1);  // drop the ':' from "c:\..." (MSYS spelling has none)
  for (wchar_t &ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
}

auto other_fixed_drive(wchar_t other) -> wchar_t {
  const DWORD mask = GetLogicalDrives();
  for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
    if ((mask & (1u << (letter - L'A'))) == 0) continue;
    if (letter == other) continue;
    if (GetDriveTypeW((std::wstring(1, letter) + L":\\").c_str()) !=
        DRIVE_FIXED) {
      continue;
    }
    return letter;
  }
  return L'\0';
}

auto shadow_leaf(const TempDir &tmp, wchar_t cwd_drive, const wchar_t *leaf)
    -> std::filesystem::path {
  const wchar_t operand_drive = drive_letter(tmp.path);
  std::wstring text(1, cwd_drive);
  text += L":\\";
  text += operand_drive;
  text += L"\\";
  text += tmp.path.wstring().substr(3);
  return std::filesystem::path(text) / leaf;
}

}  // namespace mknod_dialect_detail

TEST(mknod, mknod_fifo_drive_dialect_operand_lands_on_operand_drive) {
  TempDir tmp;
  const wchar_t tmp_drive = mknod_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand =
      mknod_dialect_detail::slash_drive_form(tmp.path / L"probe1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mknod.exe", {operand, L"p"});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mknod.exe", operand, L"p");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mknod stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("probe1235"), "!<fifo>");
  EXPECT_FALSE(std::filesystem::exists(
      mknod_dialect_detail::shadow_leaf(tmp, tmp_drive, L"probe1235")));
}

TEST(mknod, mknod_fifo_drive_dialect_cwd_on_other_drive_lands_on_operand) {
  TempDir tmp;
  const wchar_t other = mknod_dialect_detail::other_fixed_drive(
      mknod_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      mknod_dialect_detail::slash_drive_form(tmp.path / L"probe1235");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"mknod.exe", {operand, L"p"});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mknod.exe", operand, L"p");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mknod stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("probe1235"), "!<fifo>");
  EXPECT_FALSE(std::filesystem::exists(
      mknod_dialect_detail::shadow_leaf(tmp, other, L"probe1235")));
}

TEST(mknod, mknod_bare_drive_root_operand_reports_file_exists) {
  TempDir tmp;
  const wchar_t drive = mknod_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand = std::wstring(L"/") + drive;
  const std::filesystem::path shadow_root =
      std::filesystem::path(std::wstring(1, drive) + L":\\" + drive);
  const bool shadow_pre_existed = std::filesystem::exists(shadow_root);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mknod.exe", {operand, L"p"});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mknod.exe", operand, L"p");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mknod stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("File exists"), std::string::npos);
  if (!shadow_pre_existed) {
    EXPECT_FALSE(std::filesystem::exists(shadow_root));
  }
}

TEST(mknod, mknod_drive_dialect_existing_target_quotes_operand_spelling) {
  TempDir tmp;
  tmp.write("node1235", "already here");
  const std::wstring operand =
      mknod_dialect_detail::slash_drive_form(tmp.path / L"node1235");
  const std::string operand_utf8(operand.begin(), operand.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mknod.exe", {operand, L"p"});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mknod.exe", operand, L"p");
  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
  // MSYS pins the unquoted form here ("mknod: /e/nx: File exists").
  EXPECT_NE(r.stderr_text.find(operand_utf8 + ": File exists"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("X:\\"), std::string::npos);
}
