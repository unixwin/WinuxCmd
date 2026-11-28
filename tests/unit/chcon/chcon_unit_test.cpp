#include "framework/winuxtest.h"

TEST(chcon, chcon_missing_operand_fails) {
  Pipeline p;
  p.add(L"chcon.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing operand"), std::string::npos);
}

TEST(chcon, chcon_missing_file_after_context_fails) {
  Pipeline p;
  p.add(L"chcon.exe", {L"system_u:object_r:user_home_t:s0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing operand after"), std::string::npos);
}

TEST(chcon, chcon_reference_requires_file_operand) {
  Pipeline p;
  p.add(L"chcon.exe", {L"--reference=reference.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("missing operand") != std::string::npos ||
              r.stderr_text.find("missing file operand") != std::string::npos);
}

TEST(chcon, chcon_existing_file_reports_windows_limitation) {
  TempDir tmp;
  tmp.write("file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe", {L"system_u:object_r:user_home_t:s0", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("not supported on Windows"), std::string::npos);
}

TEST(chcon, chcon_reference_form_reports_windows_limitation) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe", {L"--reference=reference.txt", L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("failed to get security context"),
            std::string::npos);
}

TEST(chcon, chcon_verbose_reports_each_file) {
  TempDir tmp;
  tmp.write("one.txt", "1");
  tmp.write("two.txt", "2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe",
        {L"-v", L"system_u:object_r:user_home_t:s0", L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("one.txt"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("two.txt"), std::string::npos);
}

// ===== #1241 regression: POSIX drive-dialect operands =====
// chcon probed its file operands raw: an unfolded "/e/..." spelling makes
// GetFileAttributesW answer for "<cwd>:\e\...", so existing E: files were
// reported as "cannot access" whenever the cwd sat on another drive. The
// probes synthesize the dialect spelling from the TempDir's own drive so
// they pass on any host.
namespace chcon_dialect_detail {

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

auto utf8(const std::wstring &text) -> std::string {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace chcon_dialect_detail

TEST(chcon, chcon_drive_dialect_operand_resolves_then_reports_unsupported) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const std::wstring operand =
      chcon_dialect_detail::slash_drive_form(tmp.path / L"g1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe", {L"-v", L"system_u:object_r:user_home_t:s0", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chcon.exe", L"-v", L"system_u:object_r:user_home_t:s0",
                    operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chcon stderr", r.stderr_text);

  // Parse-layer verdict: the operand resolves (the verbose report names the
  // file), then the command reports its Windows limitation - never
  // "cannot access" for an existing file.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find(chcon_dialect_detail::utf8(operand) +
                               ": SELinux file contexts are not supported"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("cannot access"), std::string::npos);
}

TEST(chcon, chcon_drive_dialect_cwd_on_other_drive_resolves_operand) {
  TempDir tmp;
  tmp.write("g1235", "probe");
  const wchar_t other = chcon_dialect_detail::other_fixed_drive(
      chcon_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      chcon_dialect_detail::slash_drive_form(tmp.path / L"g1235");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"chcon.exe", {L"-v", L"system_u:object_r:user_home_t:s0", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("chcon.exe", L"-v", L"system_u:object_r:user_home_t:s0",
                    operand);
  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find(chcon_dialect_detail::utf8(operand) +
                               ": SELinux file contexts are not supported"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("cannot access"), std::string::npos);
}

TEST(chcon, chcon_missing_drive_dialect_operand_quotes_spelling) {
  TempDir tmp;
  const std::wstring missing =
      chcon_dialect_detail::slash_drive_form(tmp.path / L"nope1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chcon.exe", {L"-v", L"system_u:object_r:user_home_t:s0", missing});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chcon stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("cannot access '" +
                               chcon_dialect_detail::utf8(missing) + "'"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("X:\\"), std::string::npos);
}
