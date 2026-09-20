// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

auto wide_to_utf8_for_readlink_test(const std::wstring& text) -> std::string {
  if (text.empty()) return {};
  int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), needed, nullptr, nullptr);
  return out;
}

auto utf8_to_wide_for_readlink_test(const std::string& text) -> std::wstring {
  if (text.empty()) return {};
  int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) return {};
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), needed);
  return out;
}

auto long_existing_prefix_path(std::filesystem::path path)
    -> std::filesystem::path {
  std::vector<std::filesystem::path> suffix;
  std::error_code ec;
  while (!path.empty() && !std::filesystem::exists(path, ec)) {
    suffix.push_back(path.filename());
    auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
    ec.clear();
  }

  HANDLE handle =
      CreateFileW(path.wstring().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    DWORD needed = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (needed > 0) {
      std::wstring buffer(needed + 1, L'\0');
      DWORD written = GetFinalPathNameByHandleW(
          handle, buffer.data(), static_cast<DWORD>(buffer.size()),
          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      if (written > 0 && written < buffer.size()) {
        buffer.resize(written);
        if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) {
          path = L"\\\\" + buffer.substr(8);
        } else if (buffer.rfind(L"\\\\?\\", 0) == 0 ||
                   buffer.rfind(L"\\??\\", 0) == 0) {
          path = buffer.substr(4);
        } else {
          path = buffer;
        }
      }
    }
    CloseHandle(handle);
  } else {
    std::wstring base = path.wstring();
    DWORD needed = GetLongPathNameW(base.c_str(), nullptr, 0);
    if (needed > 0) {
      std::wstring buffer(needed, L'\0');
      DWORD written = GetLongPathNameW(base.c_str(), buffer.data(), needed);
      if (written > 0 && written < needed) {
        buffer.resize(written);
        path = buffer;
      }
    }
  }

  for (auto it = suffix.rbegin(); it != suffix.rend(); ++it) {
    path /= *it;
  }
  return path;
}

auto normalize_path_text(std::string text) -> std::string {
  std::string terminator;
  while (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
    terminator.insert(terminator.begin(), text.back());
    text.pop_back();
  }

  std::replace(text.begin(), text.end(), '/', '\\');
  if (text.rfind("\\\\?\\UNC\\", 0) == 0) {
    text = "\\\\" + text.substr(8);
  } else if (text.rfind("\\\\?\\", 0) == 0 || text.rfind("\\??\\", 0) == 0) {
    text.erase(0, 4);
  }

  auto wide = utf8_to_wide_for_readlink_test(text);
  if (!wide.empty()) {
    auto normalized = long_existing_prefix_path(std::filesystem::path(wide));
    auto utf8 = wide_to_utf8_for_readlink_test(normalized.wstring());
    if (!utf8.empty()) {
      text = utf8;
    }
  }

  return text + terminator;
}

bool create_symlink_or_skip(const std::filesystem::path& link,
                            const std::filesystem::path& target) {
  DWORD flags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif

  if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(),
                          flags)) {
    return true;
  }

  std::cout << "  SKIPPED (CreateSymbolicLinkW failed with error "
            << GetLastError() << ")\n";
  return false;
}

}  // namespace

TEST(readlink, readlink_regular_file_fails) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"target.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(readlink, readlink_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"readlink.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "readlink: missing operand\nTry 'readlink --help' for more "
                 "information.\n");
}

TEST(readlink, readlink_verbose_reports_diagnostics) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-v", L"target.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(readlink, readlink_quiet_suppresses_diagnostics) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-q", L"target.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(readlink, readlink_posixly_correct_regular_file_reports_invalid_argument) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"readlink.exe", {L"target.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "readlink: target.txt: Invalid argument\n");
}

TEST(readlink,
     readlink_posixly_correct_ignores_quiet_and_silent_for_regular_file) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline quiet_result;
  quiet_result.set_cwd(tmp.wpath());
  quiet_result.set_env(L"POSIXLY_CORRECT", L"1");
  quiet_result.add(L"readlink.exe", {L"-q", L"target.txt"});

  auto q = quiet_result.run();

  EXPECT_EQ(q.exit_code, 1);
  EXPECT_TRUE(q.stdout_text.empty());
  EXPECT_EQ_TEXT(q.stderr_text, "readlink: target.txt: Invalid argument\n");

  Pipeline silent_result;
  silent_result.set_cwd(tmp.wpath());
  silent_result.set_env(L"POSIXLY_CORRECT", L"1");
  silent_result.add(L"readlink.exe", {L"-s", L"target.txt"});

  auto s = silent_result.run();

  EXPECT_EQ(s.exit_code, 1);
  EXPECT_TRUE(s.stdout_text.empty());
  EXPECT_EQ_TEXT(s.stderr_text, "readlink: target.txt: Invalid argument\n");
}

TEST(readlink, readlink_verbose_and_silent_follow_last_occurrence) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  Pipeline quiet_then_verbose;
  quiet_then_verbose.set_cwd(tmp.wpath());
  quiet_then_verbose.add(L"readlink.exe", {L"-s", L"-v", L"target.txt"});

  auto verbose_result = quiet_then_verbose.run();

  EXPECT_NE(verbose_result.exit_code, 0);
  EXPECT_TRUE(verbose_result.stdout_text.empty());
  EXPECT_FALSE(verbose_result.stderr_text.empty());

  Pipeline verbose_then_quiet;
  verbose_then_quiet.set_cwd(tmp.wpath());
  verbose_then_quiet.add(L"readlink.exe", {L"-v", L"-s", L"target.txt"});

  auto quiet_result = verbose_then_quiet.run();

  EXPECT_NE(quiet_result.exit_code, 0);
  EXPECT_TRUE(quiet_result.stdout_text.empty());
  EXPECT_TRUE(quiet_result.stderr_text.empty());
}

TEST(readlink, readlink_canonicalize_existing) {
  TempDir tmp;
  tmp.mkdir("existing");
  tmp.write("existing/present.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"existing\\..\\existing\\present.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected =
      normalize_path_text((tmp.path / "existing" / "present.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected + "\n");
}

TEST(readlink, readlink_canonicalize_missing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-m", L"missing\\branch\\leaf.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text(
      (tmp.path / "missing" / "branch" / "leaf.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected + "\n");
}

TEST(readlink, readlink_canonicalize_mode_last_occurrence_wins_to_existing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-m", L"-e", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(readlink, readlink_canonicalize_mode_last_occurrence_wins_to_missing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"-m", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text((tmp.path / "missing.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected + "\n");
}

TEST(readlink, readlink_no_newline_suppresses_delimiter) {
  TempDir tmp;
  tmp.write("present.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"-n", L"present.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text((tmp.path / "present.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected);
}

TEST(readlink, readlink_zero_terminated_uses_nul) {
  TempDir tmp;
  tmp.write("present.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"-z", L"present.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text((tmp.path / "present.txt").string());
  EXPECT_BYTES(normalize_path_text(r.stdout_text),
               expected + std::string(1, '\0'));
}

TEST(readlink, readlink_no_newline_multiple_operands_warns_and_uses_newlines) {
  TempDir tmp;
  tmp.write("one.txt", "1");
  tmp.write("two.txt", "2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"-n", L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.ends_with("\n"));
  EXPECT_TRUE(r.stdout_text.find("\n") != r.stdout_text.rfind("\n"));
}

TEST(readlink, readlink_symlink_target_if_available) {
  TempDir tmp;
  tmp.write("target.txt", "hello\n");

  auto link = tmp.path / "link.txt";
  if (!create_symlink_or_skip(link, tmp.path / "target.txt")) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);

  auto expected = normalize_path_text((tmp.path / "target.txt").string());
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), expected + "\n");
}

TEST(readlink, readlink_f_resolves_dangling_leaf_target) {
  TempDir tmp;

  std::filesystem::path link = tmp.path / "dang";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"z_absent_tgt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-f", L"dang"});
  auto r = p.run();

  // [GNU] readlink -f resolves a dangling leaf link to its target's path,
  // exit 0 (uutils#6688 follow-up).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("z_absent_tgt") != std::string::npos);
}

TEST(readlink, readlink_e_fails_on_dangling_leaf_target) {
  TempDir tmp;

  std::filesystem::path link = tmp.path / "dang";
  if (!create_symlink_or_skip(link, std::filesystem::path(L"z_absent_tgt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-e", L"dang"});
  auto r = p.run();

  // [GNU] readlink -e requires the resolved target to exist: exit 1.
  EXPECT_EQ(r.exit_code, 1);
}

TEST(readlink, readlink_f_resolves_intermediate_symlink) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "real");
  tmp.write("real/f.txt", "content");

  std::filesystem::path link = tmp.path / "ldir";
  std::wstring mklink = L"cmd /d /c mklink /d \"" + link.wstring() + L"\" \"" +
                        (tmp.path / "real").wstring() + L"\" >nul";
  if (_wsystem(mklink.c_str()) != 0) {
    std::cout << "  SKIPPED (mklink /d failed)\n";
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"readlink.exe", {L"-f", L"ldir/f.txt"});
  auto r = p.run();

  // [GNU] -f resolves every component: "ldir/f.txt" -> ".../real/f.txt".
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("real") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("f.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ldir") == std::string::npos);
}
