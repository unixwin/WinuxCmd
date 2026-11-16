// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

// The shell-namespace /tmp mount only resolves while a shell root is
// configured; pin the three non-selected aliases empty and set
// WINUXSH_ROOT, the same contract the realpath/stat suites use.
void use_shell_root(Pipeline& p, const std::wstring& root) {
  p.set_env(L"__RUBASH_SHELL_ROOT", L"");
  p.set_env(L"NIU_ROOT", L"");
  p.set_env(L"RUBASH_ROOT", L"");
  p.set_env(L"WINUXSH_ROOT", root);
}

}  // namespace

// #1769: under a Git Bash parent TEMP/TMP arrive as the MSYS form "/tmp"
// itself. tmp_base_w must skip POSIX-spelled candidates; when every
// candidate is POSIX-spelled the base falls back to GetTempPathW, which
// resolves the POSIX TMP against the child's current drive ("<drive>:\tmp").
// cygpath -w /tmp/<leaf> exposes the resolved mount base verbatim.
TEST(native_path, tmp_base_all_posix_env_falls_back_to_gettemppath) {
  TempDir payload;
  TempDir cwd;

  Pipeline p;
  p.set_cwd(cwd.wpath());
  use_shell_root(p, payload.wpath());
  p.set_env(L"TMPDIR", L"/tmp");
  p.set_env(L"TEMP", L"/tmp");
  p.set_env(L"TMP", L"/tmp");
  p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::wstring root_name = cwd.path.root_name().wstring();
  ASSERT_FALSE(root_name.empty());
  const std::string expected =
      std::string(1, static_cast<char>(root_name[0])) + ":\\tmp\\g5probe\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

// #1769: a POSIX-spelled candidate is skipped, not trusted — niubash#94's
// TMPDIR > TEMP > TMP priority continues over the remaining candidates.
TEST(native_path, tmp_base_skips_posix_candidates_keeps_priority) {
  TempDir payload;
  TempDir cwd;
  TempDir tmp_b;
  TempDir tmp_a;

  {
    // TMPDIR and TEMP POSIX-spelled: the last candidate TMP wins.
    Pipeline p;
    p.set_cwd(cwd.wpath());
    use_shell_root(p, payload.wpath());
    p.set_env(L"TMPDIR", L"/tmp");
    p.set_env(L"TEMP", L"/tmp");
    p.set_env(L"TMP", tmp_b.wpath());
    p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, tmp_b.path.string() + "\\g5probe\n");
  }

  {
    // Only TMPDIR POSIX-spelled: TEMP still outranks TMP.
    Pipeline p;
    p.set_cwd(cwd.wpath());
    use_shell_root(p, payload.wpath());
    p.set_env(L"TMPDIR", L"/tmp");
    p.set_env(L"TEMP", tmp_a.wpath());
    p.set_env(L"TMP", tmp_b.wpath());
    p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, tmp_a.path.string() + "\\g5probe\n");
  }
}

// niubash#94 priority is unchanged for native-spelled candidates:
// TMPDIR wins over TEMP and TMP.
TEST(native_path, tmp_base_native_candidates_keep_tmpdir_priority) {
  TempDir payload;
  TempDir cwd;
  TempDir tmpdir;
  TempDir temp;

  Pipeline p;
  p.set_cwd(cwd.wpath());
  use_shell_root(p, payload.wpath());
  p.set_env(L"TMPDIR", tmpdir.wpath());
  p.set_env(L"TEMP", temp.wpath());
  p.set_env(L"TMP", temp.wpath());
  p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, tmpdir.path.string() + "\\g5probe\n");
}

// #1792 mirror: a POSIX-spelled TMPDIR in the MSYS drive spelling (/c/...)
// names a real Windows directory through the dialect's drive folds
// (/cygdrive/X, /mnt/X, /X -> X:\). tmp_base_w must ADOPT it — the blunt
// skip discarded the spelling and dropped every temp operand onto TEMP
// while TMPDIR named an existing directory (the rust2 twin's CI diagnostic
// pinned exactly this shape: created parent = TEMP root, TMPDIR ignored).
TEST(native_path, tmp_base_adopts_resolvable_posix_drive_form) {
  TempDir payload;
  TempDir cwd;
  TempDir tmpdir;

  const std::wstring native_tmp = tmpdir.wpath();
  ASSERT_FALSE(native_tmp.empty());
  ASSERT_TRUE(native_tmp.size() >= 2 && native_tmp[1] == L':');
  std::wstring posix = L"/";
  wchar_t drive = native_tmp[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  posix.push_back(drive);
  posix.append(native_tmp.substr(2));

  Pipeline p;
  p.set_cwd(cwd.wpath());
  use_shell_root(p, payload.wpath());
  p.set_env(L"TMPDIR", posix);
  p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, tmpdir.path.string() + "\\g5probe\n");
}

// #1792 mirror: a spelling that folds but names no existing directory
// (stale workspace, or a drive the machine does not have) must not hijack
// the base — niubash#94's priority falls through to the next candidate.
TEST(native_path, tmp_base_falls_through_unresolvable_posix_drive_form) {
  TempDir payload;
  TempDir cwd;
  TempDir temp;

  Pipeline p;
  p.set_cwd(cwd.wpath());
  use_shell_root(p, payload.wpath());
  p.set_env(L"TMPDIR", L"/q/zzz-nonexistent-wc-probe");
  p.set_env(L"TEMP", temp.wpath());
  p.set_env(L"TMP", temp.wpath());
  p.add(L"cygpath.exe", {L"-w", L"/tmp/g5probe"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, temp.path.string() + "\\g5probe\n");
}
