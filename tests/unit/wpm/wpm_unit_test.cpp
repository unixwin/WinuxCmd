// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

auto build_winuxcmd_path() -> std::filesystem::path {
  const auto build_dir = std::filesystem::path(WINUXCMD_BIN_DIR);
  const auto installed = build_dir / L"usr" / L"bin" / L"winuxcmd.exe";
  if (std::filesystem::is_regular_file(installed)) return installed;
  return build_dir / L"winuxcmd.exe";
}

auto canonical_bin_dir(const std::filesystem::path& root)
    -> std::filesystem::path {
  return root / L"usr" / L"bin";
}

auto canonical_exe(const std::filesystem::path& root, const wchar_t* name)
    -> std::filesystem::path {
  return canonical_bin_dir(root) / name;
}

auto current_arch_key() -> std::string {
#if defined(_M_ARM64) || defined(__aarch64__)
  return "windows-arm64";
#else
  return "windows-x64";
#endif
}

auto widen_ascii(const std::string& text) -> std::wstring {
  return std::wstring(text.begin(), text.end());
}

auto file_url(std::filesystem::path path) -> std::string {
  auto text = path.string();
  std::ranges::replace(text, '\\', '/');
  return "file://" + text;
}

auto sha256_file_hex(const std::filesystem::path& path) -> std::string {
  auto result = run_command(build_winuxcmd_path().wstring(),
                            {L"sha256sum", path.wstring()});
  if (result.exit_code != 0 || result.stdout_text.size() < 64) return "";
  return result.stdout_text.substr(0, 64);
}

auto exe_index_json(const std::filesystem::path& artifact_path,
                    const std::string& package, const std::string& version,
                    const std::string& sha256) -> std::string {
  return "{\n"
         "  \"schema\": 1,\n"
         "  \"name\": \"fixture\",\n"
         "  \"version\": \"fixture-exe-index\",\n"
         "  \"packages\": [\n"
         "    {\n"
         "      \"name\": \"" +
         package +
         "\",\n"
         "      \"version\": \"" +
         version +
         "\",\n"
         "      \"description\": \"Local fixture executable\",\n"
         "      \"kind\": \"external\",\n"
         "      \"artifacts\": {\n"
         "        \"" +
         current_arch_key() +
         "\": {\n"
         "          \"type\": \"exe\",\n"
         "          \"sha256\": \"" +
         sha256 +
         "\",\n"
         "          \"urls\": [\"" +
         file_url(artifact_path) +
         "\"],\n"
         "          \"files\": [{\"from\":\"bin/" +
         package +
         ".exe\"}]\n"
         "        }\n"
         "      }\n"
         "    }\n"
         "  ]\n"
         "}\n";
}

auto install_fixture_index_json(const std::filesystem::path& artifact_path)
    -> std::string {
  return exe_index_json(artifact_path, "jq", "1.0.0",
                        "5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                        "a1e090af291226f322");
}

auto catalog_fixture_index_json() -> std::string {
  return "{\n"
         "  \"schema\": 1,\n"
         "  \"name\": \"fixture\",\n"
         "  \"version\": \"fixture-catalog\",\n"
         "  \"packages\": [\n"
         "    {\"name\":\"winuxcmd\",\"version\":\"0.13.1\","
         "\"description\":\"WinuxCmd core command set\",\"kind\":\"core\","
         "\"commands\":[\"winuxcmd\",\"wpm\"],\"artifacts\":{\"" +
         current_arch_key() +
         "\":{\"type\":\"zip\",\"sha256\":\"present\","
         "\"urls\":[\"https://example.invalid/winuxcmd.zip\"],"
         "\"files\":[{\"from\":\"winuxcmd.exe\"}]}}},\n"
         "    {\"name\":\"gawk\",\"version\":\"\","
         "\"description\":\"GNU awk placeholder\",\"kind\":\"external\","
         "\"category\":\"text\",\"commands\":[\"gawk\",\"awk\"],"
         "\"artifacts\":{}},\n"
         "    {\"name\":\"jq\",\"version\":\"1.8.2\","
         "\"description\":\"Command-line JSON processor.\","
         "\"kind\":\"external\",\"category\":\"data\","
         "\"commands\":[\"jq\"],\"artifacts\":{\"" +
         current_arch_key() +
         "\":{\"type\":\"exe\",\"sha256\":\"present\","
         "\"urls\":[\"https://example.invalid/jq.exe\"],"
         "\"files\":[{\"from\":\"jq.exe\"}]}}},\n"
         "    {\"name\":\"ripgrep\",\"version\":\"15.2.0\","
         "\"description\":\"Fast recursive search tool.\","
         "\"kind\":\"external\",\"category\":\"search\","
         "\"commands\":[\"rg\"],\"artifacts\":{\"" +
         current_arch_key() +
         "\":{\"type\":\"zip\",\"sha256\":\"present\","
         "\"urls\":[\"https://example.invalid/rg.zip\"],"
         "\"files\":[{\"from\":\"rg.exe\"}]}}},\n"
         "    {\"name\":\"fd\",\"version\":\"10.4.2\","
         "\"description\":\"Fast user-friendly file finder.\","
         "\"kind\":\"external\",\"category\":\"search\","
         "\"commands\":[\"fd\"],\"artifacts\":{\"" +
         current_arch_key() +
         "\":{\"type\":\"zip\",\"sha256\":\"present\","
         "\"urls\":[\"https://example.invalid/fd.zip\"],"
         "\"files\":[{\"from\":\"fd.exe\"}]}}},\n"
         "    {\"name\":\"sd\",\"version\":\"1.1.0\","
         "\"description\":\"Intuitive find-and-replace command.\","
         "\"kind\":\"external\",\"category\":\"text\","
         "\"commands\":[\"sd\"],\"artifacts\":{\"" +
         current_arch_key() +
         "\":{\"type\":\"zip\",\"sha256\":\"present\","
         "\"urls\":[\"https://example.invalid/sd.zip\"],"
         "\"files\":[{\"from\":\"sd.exe\"}]}}}\n"
         "  ]\n"
         "}\n";
}

auto winuxcmd_update_fixture_index_json(
    const std::filesystem::path& artifact_path) -> std::string {
  return "{\n"
         "  \"schema\": 1,\n"
         "  \"name\": \"fixture\",\n"
         "  \"version\": \"fixture-winuxcmd-update\",\n"
         "  \"packages\": [\n"
         "    {\n"
         "      \"name\": \"winuxcmd\",\n"
         "      \"version\": \"99.0.0\",\n"
         "      \"description\": \"WinuxCmd update fixture\",\n"
         "      \"kind\": \"external\",\n"
         "      \"commands\": [\"winuxcmd\", \"wpm\"],\n"
         "      \"artifacts\": {\n"
         "        \"" +
         current_arch_key() +
         "\": {\n"
         "          \"type\": \"exe\",\n"
         "          \"sha256\": "
         "\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38a1e090af291226f322\","
         "\n"
         "          \"urls\": [\"" +
         file_url(artifact_path) +
         "\"],\n"
         "          \"files\": [{\"from\":\"winuxcmd.exe\","
         "\"to\":\"winuxcmd.exe\"}]\n"
         "        }\n"
         "      }\n"
         "    }\n"
         "  ]\n"
         "}\n";
}

auto same_file(const std::filesystem::path& a, const std::filesystem::path& b)
    -> bool {
  HANDLE ha =
      CreateFileW(a.wstring().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE hb =
      CreateFileW(b.wstring().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (ha == INVALID_HANDLE_VALUE || hb == INVALID_HANDLE_VALUE) {
    if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
    if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
    return false;
  }
  BY_HANDLE_FILE_INFORMATION ia{};
  BY_HANDLE_FILE_INFORMATION ib{};
  bool ok = GetFileInformationByHandle(ha, &ia) != 0 &&
            GetFileInformationByHandle(hb, &ib) != 0;
  CloseHandle(ha);
  CloseHandle(hb);
  return ok && ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
         ia.nFileIndexHigh == ib.nFileIndexHigh &&
         ia.nFileIndexLow == ib.nFileIndexLow;
}

// Mirrors the receipt `wpm install` writes under .wpm/installed/<name>.json.
// wpm only reports "already installed" when this receipt exists; destination
// files without one are treated as foreign and require --force. Fixtures that
// pre-place files in usr/bin must write a receipt to simulate a real prior
// install rather than a same-named file from another source.
auto install_receipt_json(const std::filesystem::path& root,
                          const std::string& name, const std::string& version,
                          const std::string& sha256) -> std::string {
  const auto destination =
      (canonical_bin_dir(root) / (name + ".exe")).generic_string();
  return "{\n"
         "  \"name\": \"" +
         name +
         "\",\n"
         "  \"version\": \"" +
         version +
         "\",\n"
         "  \"sha256\": \"" +
         sha256 +
         "\",\n"
         "  \"layout\": \"flat\",\n"
         "  \"destinations\": [\"" +
         destination +
         "\"]\n"
         "}\n";
}

}  // namespace

TEST(wpm, wpm_hardlink_entrypoint_reports_version) {
  Pipeline p;
  p.add(L"wpm.exe", {L"version"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("wpm 0.4.0") != std::string::npos);
}

TEST(wpm, wpm_help_matches_plain_usage) {
  Pipeline plain;
  plain.add(L"wpm.exe", {});
  auto plain_result = plain.run();

  Pipeline help;
  help.add(L"wpm.exe", {L"--help"});
  auto help_result = help.run();

  EXPECT_EQ(plain_result.exit_code, 0);
  EXPECT_EQ(help_result.exit_code, 0);
  EXPECT_EQ_TEXT(help_result.stdout_text, plain_result.stdout_text);
  EXPECT_TRUE(help_result.stdout_text.find("Usage: wpm <command> [args] "
                                           "[options]") != std::string::npos);
  EXPECT_TRUE(help_result.stdout_text.find("Commands:") != std::string::npos);
  EXPECT_TRUE(help_result.stdout_text.find("Options:") != std::string::npos);
  EXPECT_TRUE(help_result.stdout_text.find("clean [cache|staging|all]") !=
              std::string::npos);
  EXPECT_TRUE(help_result.stdout_text.find("cache clean [cache|staging|all]") !=
              std::string::npos);
  EXPECT_TRUE(help_result.stdout_text.find("source list|use|add|region|test") !=
              std::string::npos);
  EXPECT_TRUE(help_result.stdout_text.find("export [--plain]") !=
              std::string::npos);
  EXPECT_TRUE(help_result.stdout_text.find("restore <file>") !=
              std::string::npos);
}

TEST(wpm, wpm_standard_version_uses_wpm_version) {
  Pipeline p;
  p.add(L"wpm.exe", {L"--version"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "wpm 0.4.0\n");
}

TEST(wpm, wpm_install_without_package_shows_usage) {
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"install"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("wpm: usage: wpm install <package>...") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("unknown command") == std::string::npos);
}

TEST(wpm, wpm_links_list_includes_internal_tool) {
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"links", L"list"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("wpm") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ls") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("jq") == std::string::npos);
}

TEST(wpm, wpm_index_status_uses_builtin_index_offline) {
  TempDir tmp;
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"index", L"status", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("WPM index") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("packages:") != std::string::npos);
}

TEST(wpm, wpm_sources_prefer_builtin_urls_over_stale_local_index) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"stale\",\n"
            "  \"version\": \"stale-1\",\n"
            "  \"sources\": [\n"
            "    {\n"
            "      \"name\": \"official-github-raw\",\n"
            "      \"region\": \"global\",\n"
            "      \"priority\": 10,\n"
            "      \"index_urls\": [\n"
            "        "
            "\"https://raw.githubusercontent.com/unixwin/WinuxCmd/main/"
            "wpm-source/index.json\"\n"
            "      ]\n"
            "    }\n"
            "  ],\n"
            "  \"packages\": []\n"
            "}\n");

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"source", L"list", L"-v", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("https://raw.githubusercontent.com/unixwin/"
                                 "wpm-source/main/index.json") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("https://raw.githubusercontent.com/unixwin/"
                                 "WinuxCmd/main/wpm-source/index.json") ==
              std::string::npos);
}

TEST(wpm, wpm_list_without_local_index_prompts_update) {
  TempDir tmp;
  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"list", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("no packages in the local index") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("wpm index update") != std::string::npos);
}

TEST(wpm, wpm_list_marks_source_packages_ready) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());

  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"list", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  if (current_arch_key() == "windows-x64") {
    EXPECT_TRUE(r.stdout_text.find("[ready] winuxcmd 0.13.1 [winuxcmd, wpm]") !=
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("[ready] jq 1.8.2 [jq]") !=
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("[ready] ripgrep 15.2.0 [rg]") !=
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("[ready] fd 10.4.2 [fd]") !=
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("[ready] sd 1.1.0 [sd]") !=
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("[index-only] ripgrep [rg]") ==
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("[index-only] gawk [gawk, awk]") ==
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("use --all to show placeholders") !=
                std::string::npos);
  }
}

TEST(wpm, wpm_list_all_shows_index_only_placeholders) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());

  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"list", L"--all", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[index-only] gawk [gawk, awk]") !=
              std::string::npos);
}

TEST(wpm, wpm_installed_lists_only_present_package_files) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/jq.exe", "installed jq\n");

  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"installed", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("[installed] jq 1.8.2 [jq]") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[installed] ripgrep") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[installed] fd") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[index-only]") == std::string::npos);
}

TEST(wpm, wpm_export_plain_lists_profile_packages) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/winuxcmd.exe", "core executable\n");
  tmp.write("usr/bin/jq.exe", "installed jq\n");
  tmp.write("usr/bin/rg.exe", "installed rg\n");

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"export", L"--plain", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("jq\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ripgrep\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("winuxcmd") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("[installed]") == std::string::npos);
}

TEST(wpm, wpm_restore_installs_plain_profile_list) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/jq.exe", "installed jq\n");
  tmp.write(".wpm/installed/jq.json",
            install_receipt_json(tmp.path, "jq", "1.8.2", "present"));
  tmp.write("packages.txt", "\n# profile comment\n  jq  \n");

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"restore", (tmp.path / L"packages.txt").wstring(), L"--root",
         tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("already installed jq") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("profile comment") == std::string::npos);
}

TEST(wpm, wpm_info_marks_common_packages_installable_on_windows_x64) {
  if (current_arch_key() != "windows-x64") return;

  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  struct PackageCase {
    const wchar_t* arg;
    const char* name;
    const char* version;
  };
  const PackageCase packages[] = {{L"jq", "jq", "1.8.2"},
                                  {L"ripgrep", "ripgrep", "15.2.0"},
                                  {L"fd", "fd", "10.4.2"},
                                  {L"sd", "sd", "1.1.0"}};

  for (const auto& package : packages) {
    Pipeline p;
    p.add(L"winuxcmd.exe",
          {L"wpm", L"info", package.arg, L"--root", tmp.wpath()});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("Name: " + std::string(package.name)) !=
                std::string::npos);
    EXPECT_TRUE(
        r.stdout_text.find("Version: " + std::string(package.version)) !=
        std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("Install state: ready") !=
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("Artifact: windows-x64") !=
                std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("SHA256: present") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("URLs: 0") == std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("Files: 0") == std::string::npos);
  }
}

TEST(wpm, wpm_info_refreshes_index_when_package_missing) {
  TempDir tmp;
  const auto index_path = tmp.path / L"fixture-catalog-index.json";
  tmp.write("fixture-catalog-index.json", catalog_fixture_index_json());

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline info;
  info.add(L"winuxcmd.exe", {L"wpm", L"info", L"jq", L"--root", tmp.wpath()});
  auto r = info.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("updating index from configured sources") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Name: jq") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Version: 1.8.2") != std::string::npos);
}

TEST(wpm, wpm_search_filters_source_packages) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"search", L"search", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("ripgrep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("fd 10.4.2 [fd]") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("jq [jq]") == std::string::npos);
}

TEST(wpm, wpm_links_rebuild_creates_internal_tool_hardlink) {
  TempDir tmp;
  auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"links", L"rebuild", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  auto wpm_exe = canonical_exe(tmp.path, L"wpm.exe");
  EXPECT_TRUE(std::filesystem::exists(wpm_exe));
  EXPECT_TRUE(same_file(root_exe, wpm_exe));
}

TEST(wpm, wpm_links_rebuild_removes_legacy_jq_hardlink) {
  TempDir tmp;
  auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  auto legacy_jq = tmp.path / L"jq.exe";
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  bool linked = CreateHardLinkW(legacy_jq.wstring().c_str(),
                                root_exe.wstring().c_str(), nullptr) != 0;
  EXPECT_TRUE(linked);
  if (!linked) return;

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"links", L"rebuild", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(legacy_jq));
  EXPECT_TRUE(std::filesystem::exists(canonical_exe(tmp.path, L"wpm.exe")));
}

TEST(wpm, wpm_links_rebuild_removes_legacy_link_on_running_inode) {
  // Production self-lock: every usr\bin\*.exe is a hardlink to the inode the
  // running wpm image is mapped from, so DeleteFileW on a stale link fails
  // with Access denied. The rebuild must still remove it (POSIX unlink).
  // The root must live on the same volume as the running binary so
  // usr\bin\winuxcmd.exe can share its inode — TempDir (system %TEMP%) may
  // be on another volume where hardlinks are impossible.
  auto scratch =
      std::filesystem::path(WINUXCMD_BIN_DIR) /
      (L"wpm-selflock-test-" + std::to_wstring(GetCurrentProcessId()));
  std::filesystem::remove_all(scratch);
  std::filesystem::create_directories(scratch);

  auto root_exe = canonical_exe(scratch, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  bool linked =
      CreateHardLinkW(root_exe.wstring().c_str(),
                      build_winuxcmd_path().wstring().c_str(), nullptr) != 0;
  EXPECT_TRUE(linked);
  auto legacy_jq = scratch / L"jq.exe";
  if (linked) {
    linked = CreateHardLinkW(legacy_jq.wstring().c_str(),
                             root_exe.wstring().c_str(), nullptr) != 0;
    EXPECT_TRUE(linked);
  }

  if (linked) {
    Pipeline p;
    p.add(L"winuxcmd.exe",
          {L"wpm", L"links", L"rebuild", L"--root", scratch.wstring()});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(std::filesystem::exists(legacy_jq));
    EXPECT_TRUE(std::filesystem::exists(root_exe));
  }

  std::error_code ec;
  std::filesystem::remove_all(scratch, ec);
}

TEST(wpm, wpm_apply_update_replaces_root_and_rebuilds_links) {
  TempDir tmp;
  auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  auto wpm_exe = canonical_exe(tmp.path, L"wpm.exe");
  auto old_payload = tmp.path / L"old-winuxcmd.exe";

  std::filesystem::create_directories(root_exe.parent_path());
  tmp.write("old-winuxcmd.exe", "old exe\n");
  std::filesystem::copy_file(old_payload, root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  bool linked = CreateHardLinkW(wpm_exe.wstring().c_str(),
                                root_exe.wstring().c_str(), nullptr) != 0;
  EXPECT_TRUE(linked);
  if (!linked) return;

  auto result = run_command(
      build_winuxcmd_path().wstring(),
      {L"wpm", L"--", L"__apply-update", L"--root", tmp.wpath(), L"--payload",
       build_winuxcmd_path().wstring(), L"--parent", L"0"});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(run_command(root_exe.wstring(), {L"--version"}).exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L".wpm" / L"backup"));
  EXPECT_TRUE(same_file(root_exe, wpm_exe));
}

TEST(wpm, wpm_index_update_uses_local_file_source) {
  TempDir tmp;
  const auto index_path = tmp.path / L"fixture-index.json";
  tmp.write("fixture-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-1\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"fixture-tool\",\"version\":\"1.0.0\","
            "\"description\":\"Local fixture\",\"kind\":\"external\","
            "\"artifacts\":{}}\n"
            "  ]\n"
            "}\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  auto add_result = add.run();
  EXPECT_EQ(add_result.exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  auto use_result = use.run();
  EXPECT_EQ(use_result.exit_code, 0);

  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  auto update_result = update.run();
  EXPECT_EQ(update_result.exit_code, 0);
  EXPECT_TRUE(update_result.stdout_text.find("index updated from fixture") !=
              std::string::npos);

  Pipeline info;
  info.add(L"winuxcmd.exe",
           {L"wpm", L"info", L"fixture-tool", L"--root", tmp.wpath()});
  auto info_result = info.run();
  EXPECT_EQ(info_result.exit_code, 0);
  EXPECT_TRUE(info_result.stdout_text.find("Version: 1.0.0") !=
              std::string::npos);
}

TEST(wpm, wpm_source_region_command_updates_config) {
  TempDir tmp;

  Pipeline set;
  set.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"region", L"cn", L"--root", tmp.wpath()});
  auto set_result = set.run();
  EXPECT_EQ(set_result.exit_code, 0);
  EXPECT_TRUE(set_result.stdout_text.find("region set to cn") !=
              std::string::npos);

  std::ifstream in(tmp.path / L".wpm" / L"config.json");
  std::string config_text{std::istreambuf_iterator<char>(in),
                          std::istreambuf_iterator<char>()};
  EXPECT_TRUE(config_text.find("\"region\": \"cn\"") != std::string::npos);

  Pipeline bad;
  bad.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"region", L"moon", L"--root", tmp.wpath()});
  auto bad_result = bad.run();
  EXPECT_EQ(bad_result.exit_code, 1);
}

TEST(wpm, wpm_index_update_auto_prefers_cn_when_global_unreachable) {
  TempDir tmp;
  const auto cn_index = tmp.path / L"cn-index.json";
  tmp.write("cn-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"cn-fixture\",\n"
            "  \"version\": \"cn-1\",\n"
            "  \"packages\": []\n"
            "}\n");

  // A dead global source at 127.0.0.1:9 (discard port) is refused instantly,
  // so the auto probe fails fast and deterministically without real network.
  tmp.write(".wpm/config.json",
            std::string("{\n"
                        "  \"preferred_source\": \"auto\",\n"
                        "  \"region\": \"auto\",\n"
                        "  \"user_sources\": [\n"
                        "    {\"name\": \"dead-global\", \"region\": "
                        "\"global\", \"priority\": 1, \"index_urls\": "
                        "[\"http://127.0.0.1:9/index.json\"]},\n"
                        "    {\"name\": \"local-cn\", \"region\": \"cn\", "
                        "\"priority\": 2, \"index_urls\": [\"") +
                file_url(cn_index) +
                std::string("\"]}\n"
                            "  ]\n"
                            "}\n"));

  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  auto r = update.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("index updated from local-cn") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("trying regional mirrors first") !=
              std::string::npos);
}

TEST(wpm, wpm_index_update_region_cn_skips_probe) {
  TempDir tmp;
  const auto cn_index = tmp.path / L"cn-index.json";
  tmp.write("cn-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"cn-fixture\",\n"
            "  \"version\": \"cn-1\",\n"
            "  \"packages\": []\n"
            "}\n");

  tmp.write(".wpm/config.json",
            std::string("{\n"
                        "  \"preferred_source\": \"auto\",\n"
                        "  \"region\": \"cn\",\n"
                        "  \"user_sources\": [\n"
                        "    {\"name\": \"dead-global\", \"region\": "
                        "\"global\", \"priority\": 1, \"index_urls\": "
                        "[\"http://127.0.0.1:9/index.json\"]},\n"
                        "    {\"name\": \"local-cn\", \"region\": \"cn\", "
                        "\"priority\": 2, \"index_urls\": [\"") +
                file_url(cn_index) +
                std::string("\"]}\n"
                            "  ]\n"
                            "}\n"));

  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  auto r = update.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("index updated from local-cn") !=
              std::string::npos);
  // Explicit region=cn reorders without a probe, so no notice is printed.
  EXPECT_TRUE(r.stdout_text.find("trying regional mirrors first") ==
              std::string::npos);
}

TEST(wpm, wpm_index_update_region_global_keeps_order) {
  TempDir tmp;
  const auto cn_index = tmp.path / L"cn-index.json";
  tmp.write("cn-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"cn-fixture\",\n"
            "  \"version\": \"cn-1\",\n"
            "  \"packages\": []\n"
            "}\n");

  tmp.write(".wpm/config.json",
            std::string("{\n"
                        "  \"preferred_source\": \"auto\",\n"
                        "  \"region\": \"global\",\n"
                        "  \"user_sources\": [\n"
                        "    {\"name\": \"dead-global\", \"region\": "
                        "\"global\", \"priority\": 1, \"index_urls\": "
                        "[\"http://127.0.0.1:9/index.json\"]},\n"
                        "    {\"name\": \"local-cn\", \"region\": \"cn\", "
                        "\"priority\": 2, \"index_urls\": [\"") +
                file_url(cn_index) +
                std::string("\"]}\n"
                            "  ]\n"
                            "}\n"));

  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  auto r = update.run();
  // Dead global fails first but the cn source is still reached by fallback.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("index updated from local-cn") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("trying regional mirrors first") ==
              std::string::npos);
}

TEST(wpm, wpm_index_update_preferred_source_falls_back) {
  TempDir tmp;
  const auto cn_index = tmp.path / L"cn-index.json";
  tmp.write("cn-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"cn-fixture\",\n"
            "  \"version\": \"cn-1\",\n"
            "  \"packages\": []\n"
            "}\n");

  // preferred_source names the dead source: it is tried first, then the
  // remaining sources remain as fallback instead of being skipped.
  tmp.write(".wpm/config.json",
            std::string("{\n"
                        "  \"preferred_source\": \"dead-global\",\n"
                        "  \"user_sources\": [\n"
                        "    {\"name\": \"dead-global\", \"region\": "
                        "\"global\", \"priority\": 1, \"index_urls\": "
                        "[\"http://127.0.0.1:9/index.json\"]},\n"
                        "    {\"name\": \"local-cn\", \"region\": \"cn\", "
                        "\"priority\": 2, \"index_urls\": [\"") +
                file_url(cn_index) +
                std::string("\"]}\n"
                            "  ]\n"
                            "}\n"));

  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  auto r = update.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("index updated from local-cn") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("trying other sources") != std::string::npos);
}

TEST(wpm, wpm_install_downloads_local_exe_with_sha256) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"jq.exe";
  const auto index_path = tmp.path / L"fixture-install-index.json";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  const auto installed_jq = canonical_exe(tmp.path, L"jq.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  bool linked = CreateHardLinkW(installed_jq.wstring().c_str(),
                                root_exe.wstring().c_str(), nullptr) != 0;
  EXPECT_TRUE(linked);
  if (!linked) return;
  tmp.write("source/jq.exe", "external exe\n");

  tmp.write("fixture-install-index.json",
            install_fixture_index_json(artifact_path));

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  auto install_result = install.run();
  EXPECT_EQ(install_result.exit_code, 0);
  if (install_result.exit_code != 0) return;
  EXPECT_TRUE(install_result.stdout_text.find("updating index from configured "
                                              "sources") != std::string::npos);
  EXPECT_TRUE(install_result.stdout_text.find("installed jq") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "external exe\n");
  EXPECT_FALSE(same_file(root_exe, installed_jq));
}

TEST(wpm, wpm_install_failed_download_reports_transport_retry) {
  // A dead artifact URL makes the WinHTTP attempt fail; the URLMon fallback
  // must announce itself, otherwise a second progress bar looks like a
  // duplicate download.
  TempDir tmp;
  const auto index_path = tmp.path / L"fixture-dead-index.json";
  tmp.write("fixture-dead-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-dead\",\n"
            "  \"packages\": [\n"
            "    {\n"
            "      \"name\": \"jq\",\n"
            "      \"version\": \"1.0.0\",\n"
            "      \"kind\": \"external\",\n"
            "      \"artifacts\": {\n"
            "        \"" +
                current_arch_key() +
                "\": {\n"
                "          \"type\": \"exe\",\n"
                "          \"sha256\": \"00\",\n"
                "          \"urls\": [\"http://127.0.0.1:9/jq.exe\"],\n"
                "          \"files\": [{\"from\":\"jq.exe\"}]\n"
                "        }\n"
                "      }\n"
                "    }\n"
                "  ]\n"
                "}\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  auto r = install.run();
  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("retrying via fallback transport") !=
              std::string::npos);
}

TEST(wpm, wpm_update_winuxcmd_refreshes_index_before_staging) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"winuxcmd.exe";
  const auto index_path = tmp.path / L"fixture-winuxcmd-index.json";
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("source/winuxcmd.exe", "external exe\n");
  tmp.write("fixture-winuxcmd-index.json",
            winuxcmd_update_fixture_index_json(artifact_path));

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline update;
  update.add(L"winuxcmd.exe", {L"wpm", L"update", L"winuxcmd", L"--dry-run",
                               L"--root", tmp.wpath()});
  auto update_result = update.run();

  EXPECT_EQ(update_result.exit_code, 0);
  EXPECT_TRUE(update_result.stdout_text.find(
                  "checking for latest winuxcmd package metadata") !=
              std::string::npos);
  EXPECT_TRUE(update_result.stdout_text.find("index updated from fixture") !=
              std::string::npos);
  EXPECT_TRUE(update_result.stdout_text.find("would apply update from") !=
              std::string::npos);

  Pipeline info;
  info.add(L"winuxcmd.exe",
           {L"wpm", L"info", L"winuxcmd", L"--root", tmp.wpath()});
  auto info_result = info.run();
  EXPECT_EQ(info_result.exit_code, 0);
  EXPECT_TRUE(info_result.stdout_text.find("Version: 99.0.0") !=
              std::string::npos);
}

TEST(wpm, wpm_install_existing_package_skips_download) {
  TempDir tmp;
  const auto missing_artifact_path = tmp.path / L"source" / L"jq.exe";
  tmp.write(".wpm/indexes/official.json",
            install_fixture_index_json(missing_artifact_path));
  tmp.write("usr/bin/jq.exe", "already here\n");
  tmp.write(
      ".wpm/installed/jq.json",
      install_receipt_json(tmp.path, "jq", "1.0.0",
                           "5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                           "a1e090af291226f322"));

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  auto install_result = install.run();

  EXPECT_EQ(install_result.exit_code, 0);
  EXPECT_TRUE(install_result.stdout_text.find("already installed jq") !=
              std::string::npos);
  EXPECT_TRUE(install_result.stdout_text.find("downloading jq") ==
              std::string::npos);
  EXPECT_TRUE(install_result.stderr_text.find("destination exists") ==
              std::string::npos);
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "already here\n");
}

TEST(wpm, wpm_install_uses_valid_cached_artifact_before_downloading) {
  TempDir tmp;
  const auto missing_artifact_path = tmp.path / L"source" / L"jq.exe";
  tmp.write(".wpm/indexes/official.json",
            install_fixture_index_json(missing_artifact_path));
  // Cache key embeds version and short artifact hash: jq-1.0.0-5140f4f6.exe.
  tmp.write(".wpm/cache/jq-1.0.0-5140f4f6.exe", "external exe\n");

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  auto install_result = install.run();

  EXPECT_EQ(install_result.exit_code, 0);
  EXPECT_TRUE(install_result.stdout_text.find("installed jq") !=
              std::string::npos);
  EXPECT_TRUE(install_result.stdout_text.find("downloading jq") ==
              std::string::npos);
  EXPECT_TRUE(install_result.stderr_text.find("download failed") ==
              std::string::npos);
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "external exe\n");
}

TEST(wpm, wpm_install_cache_key_distinguishes_versions) {
  TempDir tmp;
  // The index advertises jq 2.0.0 but its artifact URL is unreachable, so a
  // successful install could only come from wrongly reusing a 1.0.0 cache
  // entry (issue #1154).
  const auto missing_artifact_path = tmp.path / L"source" / L"jq.exe";
  tmp.write(".wpm/indexes/official.json",
            exe_index_json(missing_artifact_path, "jq", "2.0.0",
                           "5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                           "a1e090af291226f322"));
  tmp.write(".wpm/cache/jq-1.0.0-5140f4f6.exe", "external exe\n");
  tmp.write(".wpm/cache/jq.exe", "external exe\n");

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  auto install_result = install.run();

  EXPECT_EQ(install_result.exit_code, 1);
  EXPECT_TRUE(install_result.stderr_text.find("download failed") !=
              std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"jq.exe"));
}

TEST(wpm, wpm_install_redownloads_when_cached_artifact_sha_mismatches) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"jq.exe";
  tmp.write("source/jq.exe", "external exe\n");
  tmp.write(".wpm/indexes/official.json",
            install_fixture_index_json(artifact_path));
  // Right cache key, wrong bytes: the entry must be discarded and replaced.
  tmp.write(".wpm/cache/jq-1.0.0-5140f4f6.exe", "corrupted payload\n");

  Pipeline install;
  install.add(L"winuxcmd.exe", {L"wpm", L"install", L"jq", L"--verbose",
                                L"--root", tmp.wpath()});
  auto install_result = install.run();

  EXPECT_EQ(install_result.exit_code, 0);
  EXPECT_TRUE(install_result.stdout_text.find("downloading") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "external exe\n");
  EXPECT_EQ(tmp.read(".wpm/cache/jq-1.0.0-5140f4f6.exe"), "external exe\n");
}

TEST(wpm, wpm_install_force_after_version_bump_installs_new_artifact) {
  TempDir tmp;
  const auto old_artifact = tmp.path / L"source" / L"jq-1.0.0.exe";
  const auto new_artifact = tmp.path / L"source" / L"jq-2.0.0.exe";
  tmp.write("source/jq-1.0.0.exe", "external exe\n");
  tmp.write("source/jq-2.0.0.exe", "upgraded exe\n");
  const auto new_sha = sha256_file_hex(new_artifact);
  EXPECT_EQ(new_sha.size(), 64u);
  if (new_sha.size() != 64) return;
  tmp.write(".wpm/indexes/official.json",
            exe_index_json(old_artifact, "jq", "1.0.0",
                           "5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                           "a1e090af291226f322"));

  Pipeline first;
  first.add(L"winuxcmd.exe",
            {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  auto first_result = first.run();
  EXPECT_EQ(first_result.exit_code, 0);
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "external exe\n");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L".wpm" / L"cache" /
                                      L"jq-1.0.0-5140f4f6.exe"));

  // The index moves to 2.0.0 with a new URL and sha256; --force must install
  // the new artifact instead of silently reusing the 1.0.0 cache entry.
  tmp.write(".wpm/indexes/official.json",
            exe_index_json(new_artifact, "jq", "2.0.0", new_sha));

  Pipeline reinstall;
  reinstall.add(L"winuxcmd.exe", {L"wpm", L"install", L"jq", L"--force",
                                  L"--root", tmp.wpath()});
  auto reinstall_result = reinstall.run();

  EXPECT_EQ(reinstall_result.exit_code, 0);
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "upgraded exe\n");
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / L".wpm" / L"cache" /
                              ("jq-2.0.0-" + new_sha.substr(0, 8) + ".exe")));
}

TEST(wpm, wpm_install_single_exe_can_rename_command) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tealdeer-upstream.exe";
  tmp.write("source/tealdeer-upstream.exe", "external exe\n");
  tmp.write(".wpm/indexes/official.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-single-exe-rename\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"tealdeer\",\"version\":\"1.0.0\","
            "\"description\":\"single exe rename fixture\","
            "\"kind\":\"external\",\"commands\":[\"tldr\"],"
            "\"aliases\":[{\"name\":\"tl\",\"target\":\"tldr\"}],"
            "\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":"
                "\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38a1e090af291226"
                "f322\","
                "\"urls\":[\"" +
                file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"tldr.exe\","
                "\"to\":\"tldr.exe\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"tealdeer", L"--root", tmp.wpath()});
  auto install_result = install.run();

  EXPECT_EQ(install_result.exit_code, 0);
  EXPECT_TRUE(install_result.stdout_text.find("installed tealdeer") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("usr/bin/tldr.exe"), "external exe\n");
  EXPECT_EQ(tmp.read("usr/bin/tl.exe"), "external exe\n");
  EXPECT_TRUE(same_file(canonical_exe(tmp.path, L"tldr.exe"),
                        canonical_exe(tmp.path, L"tl.exe")));
}

TEST(wpm, wpm_install_tar_gz_with_directory_mapping) {
  TempDir tmp;
  const auto payload_dir = tmp.path / L"payload";
  const auto archive_path = tmp.path / L"tool.tar.gz";
  tmp.write("payload/bin/tool.exe", "external exe\n");
  tmp.write("payload/runtime/lang.txt", "runtime file\n");

  auto tar_result = run_command(
      L"tar.exe",
      {L"-czf", archive_path.wstring(), L"-C", payload_dir.wstring(), L"."});
  EXPECT_EQ(tar_result.exit_code, 0);
  if (tar_result.exit_code != 0) return;

  auto sha256 = sha256_file_hex(archive_path);
  EXPECT_EQ(sha256.size(), 64u);
  if (sha256.size() != 64) return;
  auto size = std::filesystem::file_size(archive_path);
  tmp.write(".wpm/indexes/official.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-tar-gz-dir\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"tool\",\"version\":\"1.0.0\","
            "\"description\":\"tar directory fixture\","
            "\"kind\":\"external\",\"commands\":[\"tool\"],"
            "\"artifacts\":{\"" +
                current_arch_key() + "\":{\"type\":\"tar.gz\",\"size\":" +
                std::to_string(size) + ",\"sha256\":\"" + sha256 +
                "\",\"urls\":[\"" + file_url(archive_path) +
                "\"],\"files\":["
                "{\"from\":\"bin/tool.exe\",\"to\":\"tool.exe\"},"
                "{\"from\":\"runtime\",\"to\":\"runtime\","
                "\"kind\":\"dir\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline info;
  info.add(L"winuxcmd.exe", {L"wpm", L"info", L"tool", L"--root", tmp.wpath()});
  auto info_result = info.run();
  EXPECT_EQ(info_result.exit_code, 0);
  EXPECT_TRUE(info_result.stdout_text.find("Type: tar.gz") !=
              std::string::npos);
  EXPECT_TRUE(info_result.stdout_text.find("Size: ") != std::string::npos);

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"tool", L"--root", tmp.wpath()});
  auto install_result = install.run();

  EXPECT_EQ(install_result.exit_code, 0);
  EXPECT_TRUE(install_result.stdout_text.find("installed tool") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("usr/bin/tool.exe"), "external exe\n");
  EXPECT_EQ(tmp.read("runtime/lang.txt"), "runtime file\n");
}

TEST(wpm, wpm_clean_dry_run_preserves_transient_state) {
  TempDir tmp;
  tmp.write(".wpm/cache/tool.exe", "cached tool\n");
  tmp.write(".wpm/staging/tool/tool.exe", "staged tool\n");
  tmp.write(".wpm/indexes/official.json", "index\n");

  Pipeline clean;
  clean.add(L"winuxcmd.exe",
            {L"wpm", L"clean", L"--dry-run", L"--root", tmp.wpath()});
  auto result = clean.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("would remove") != std::string::npos);
  EXPECT_EQ(tmp.read(".wpm/cache/tool.exe"), "cached tool\n");
  EXPECT_EQ(tmp.read(".wpm/staging/tool/tool.exe"), "staged tool\n");
  EXPECT_EQ(tmp.read(".wpm/indexes/official.json"), "index\n");
}

TEST(wpm, wpm_clean_removes_cache_and_staging_only) {
  TempDir tmp;
  tmp.write(".wpm/cache/tool.exe", "cached tool\n");
  tmp.write(".wpm/staging/tool/tool.exe", "staged tool\n");
  tmp.write(".wpm/indexes/official.json", "index\n");
  tmp.write(".wpm/config.json", "{}\n");

  Pipeline clean;
  clean.add(L"winuxcmd.exe", {L"wpm", L"clean", L"--root", tmp.wpath()});
  auto result = clean.run();

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm/cache"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm/staging"));
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / L".wpm/indexes/official.json"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L".wpm/config.json"));
}

TEST(wpm, wpm_install_dry_run_does_not_claim_install_success) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"jq.exe";
  const auto index_path = tmp.path / L"fixture-install-index.json";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  const auto installed_jq = canonical_exe(tmp.path, L"jq.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  bool linked = CreateHardLinkW(installed_jq.wstring().c_str(),
                                root_exe.wstring().c_str(), nullptr) != 0;
  EXPECT_TRUE(linked);
  if (!linked) return;
  tmp.write("source/jq.exe", "external exe\n");
  tmp.write("fixture-install-index.json",
            install_fixture_index_json(artifact_path));

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  EXPECT_EQ(update.run().exit_code, 0);

  Pipeline install;
  install.add(L"winuxcmd.exe", {L"wpm", L"install", L"jq", L"jq", L"--dry-run",
                                L"--root", tmp.wpath()});
  auto install_result = install.run();

  EXPECT_EQ(install_result.exit_code, 0);
  EXPECT_TRUE(install_result.stdout_text.find("would copy") !=
              std::string::npos);
  EXPECT_TRUE(install_result.stdout_text.find("would install jq") !=
              std::string::npos);
  EXPECT_TRUE(install_result.stdout_text.find("installed jq") ==
              std::string::npos);
  EXPECT_TRUE(install_result.stdout_text.find("requested=2 failed=0") !=
              std::string::npos);
  EXPECT_TRUE(same_file(root_exe, installed_jq));
}

TEST(wpm, wpm_uninstall_removes_package_files_and_reports_json) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/jq.exe", "installed jq\n");

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"uninstall", L"jq", L"--json", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\"status\": \"removed\"") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\"failed\": 0") != std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"jq.exe"));
}

TEST(wpm, wpm_uninstall_refuses_core_and_unknown_packages) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/winuxcmd.exe", "core executable\n");

  Pipeline core;
  core.add(L"winuxcmd.exe", {L"wpm", L"uninstall", L"winuxcmd", L"--json",
                             L"--root", tmp.wpath()});
  auto core_result = core.run();
  EXPECT_EQ(core_result.exit_code, 1);
  EXPECT_TRUE(core_result.stdout_text.find("\"status\": \"refused\"") !=
              std::string::npos);
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"winuxcmd.exe"));

  Pipeline unknown;
  unknown.add(L"winuxcmd.exe", {L"wpm", L"uninstall", L"no-such-package",
                                L"--root", tmp.wpath()});
  auto unknown_result = unknown.run();
  EXPECT_EQ(unknown_result.exit_code, 1);
  EXPECT_TRUE(unknown_result.stderr_text.find("not found in index") !=
              std::string::npos);
}

TEST(wpm, wpm_uninstall_dry_run_keeps_files_on_disk) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/rg.exe", "installed rg\n");

  Pipeline p;
  p.add(L"winuxcmd.exe", {L"wpm", L"uninstall", L"ripgrep", L"--dry-run",
                          L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("would remove ripgrep") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"rg.exe"));
}

TEST(wpm, wpm_outdated_reports_newer_remote_versions) {
  TempDir tmp;
  const auto index_path = tmp.path / L"fixture-remote-index.json";
  tmp.write(".wpm/indexes/official.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"local-1\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"jq\",\"version\":\"1.0.0\","
            "\"description\":\"JSON processor\",\"kind\":\"external\","
            "\"commands\":[\"jq\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\",\"sha256\":\"present\","
                "\"urls\":[\"https://example.invalid/jq.exe\"],"
                "\"files\":[{\"from\":\"jq.exe\"}]}}},\n"
                "    {\"name\":\"fd\",\"version\":\"10.4.2\","
                "\"description\":\"File finder\",\"kind\":\"external\","
                "\"commands\":[\"fd\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"zip\",\"sha256\":\"present\","
                "\"urls\":[\"https://example.invalid/fd.zip\"],"
                "\"files\":[{\"from\":\"fd.exe\"}]}}}\n"
                "  ]\n"
                "}\n");
  tmp.write("usr/bin/jq.exe", "installed jq\n");
  tmp.write("fixture-remote-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"remote-2\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"jq\",\"version\":\"2.0.0\","
            "\"description\":\"JSON processor\",\"kind\":\"external\","
            "\"commands\":[\"jq\"],\"artifacts\":{}},\n"
            "    {\"name\":\"fd\",\"version\":\"10.4.2\","
            "\"description\":\"File finder\",\"kind\":\"external\","
            "\"commands\":[\"fd\"],\"artifacts\":{}}\n"
            "  ]\n"
            "}\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"remote",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"remote", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline outdated;
  outdated.add(L"winuxcmd.exe",
               {L"wpm", L"outdated", L"--json", L"--root", tmp.wpath()});
  auto r = outdated.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\"checked\": 1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\"available\": \"2.0.0\"") !=
              std::string::npos);
}

TEST(wpm, wpm_source_list_json_marks_custom_and_preferred) {
  TempDir tmp;

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"my-mirror",
           L"https://mirror.example/index.json", L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"my-mirror", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline list;
  list.add(L"winuxcmd.exe",
           {L"wpm", L"source", L"list", L"--json", L"--root", tmp.wpath()});
  auto r = list.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\"schema\": 1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\"preferred\": \"my-mirror\"") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("\"custom\": true") != std::string::npos);
}

// --- #1156 interaction audit regressions -----------------------------------

TEST(wpm, wpm_help_documents_install_force_semantics) {
  Pipeline p;
  p.add(L"wpm.exe", {L"--help"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "--force to reinstall");
}

TEST(wpm, wpm_uninstall_accepts_piped_yes_confirmation) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/jq.exe", "installed jq\n");

  Pipeline p;
  p.set_stdin("y\n");
  p.add(L"winuxcmd.exe", {L"wpm", L"uninstall", L"jq", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "uninstalled jq");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"jq.exe"));
}

TEST(wpm, wpm_uninstall_piped_no_aborts_without_removal) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/jq.exe", "installed jq\n");

  Pipeline decline;
  decline.set_stdin("n\n");
  decline.add(L"winuxcmd.exe",
              {L"wpm", L"uninstall", L"jq", L"--root", tmp.wpath()});
  auto declined = decline.run();

  EXPECT_EQ(declined.exit_code, 0);
  EXPECT_CONTAINS(declined.stdout_text, "aborted; nothing was removed");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"jq.exe"));

  // A closed stdin answers EOF, which is not "yes": nothing is removed.
  Pipeline eof;
  eof.set_stdin("");
  eof.add(L"winuxcmd.exe",
          {L"wpm", L"uninstall", L"jq", L"--root", tmp.wpath()});
  auto eof_result = eof.run();

  EXPECT_EQ(eof_result.exit_code, 0);
  EXPECT_CONTAINS(eof_result.stdout_text, "aborted; nothing was removed");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"jq.exe"));
}

TEST(wpm, wpm_uninstall_yes_flag_skips_confirmation) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/jq.exe", "installed jq\n");

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"uninstall", L"jq", L"-y", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "uninstalled jq");
  EXPECT_NOT_CONTAINS(r.stdout_text, "continue?");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"jq.exe"));
}

TEST(wpm, wpm_uninstall_locked_destination_fails_loudly) {
  TempDir tmp;
  tmp.write(".wpm/indexes/official.json", catalog_fixture_index_json());
  tmp.write("usr/bin/jq.exe", "installed jq\n");
  const auto locked = tmp.path / L"usr" / L"bin" / L"jq.exe";

  // Hold the destination open without FILE_SHARE_DELETE, like a running
  // process would: the removal must fail loudly, never silently report
  // success (#1156).
  HANDLE hold =
      CreateFileW(locked.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_TRUE(hold != INVALID_HANDLE_VALUE);
  if (hold == INVALID_HANDLE_VALUE) return;

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"uninstall", L"jq", L"-y", L"--root", tmp.wpath()});
  auto r = p.run();
  CloseHandle(hold);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "failed to remove");
  EXPECT_TRUE(std::filesystem::exists(locked));
}

TEST(wpm, wpm_install_force_locked_destination_fails_loudly) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"jq.exe";
  const auto index_path = tmp.path / L"fixture-install-index.json";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/jq.exe", "external exe\n");
  tmp.write("fixture-install-index.json",
            install_fixture_index_json(artifact_path));

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  ASSERT_EQ(install.run().exit_code, 0);

  const auto locked = tmp.path / L"usr" / L"bin" / L"jq.exe";
  HANDLE hold =
      CreateFileW(locked.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_TRUE(hold != INVALID_HANDLE_VALUE);
  if (hold == INVALID_HANDLE_VALUE) return;

  Pipeline force;
  force.add(L"winuxcmd.exe",
            {L"wpm", L"install", L"jq", L"--force", L"--root", tmp.wpath()});
  auto r = force.run();
  CloseHandle(hold);

  // A locked destination is an error, not a silent skip (#1156).
  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "failed to install");
  EXPECT_TRUE(std::filesystem::exists(locked));
}

TEST(wpm, wpm_outdated_uses_receipt_version_after_index_update) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"jq.exe";
  const auto v1_path = tmp.path / L"fixture-v1.json";
  const auto v2_path = tmp.path / L"fixture-v2.json";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/jq.exe", "external exe\n");
  tmp.write("fixture-v1.json", install_fixture_index_json(artifact_path));
  tmp.write("fixture-v2.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"v2\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"jq\",\"version\":\"2.0.0\","
            "\"description\":\"JSON processor\",\"kind\":\"external\","
            "\"commands\":[\"jq\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\",\"sha256\":\"present\","
                "\"urls\":[\"https://example.invalid/jq2.exe\"],"
                "\"files\":[{\"from\":\"jq.exe\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline add_v1;
  add_v1.add(L"winuxcmd.exe",
             {L"wpm", L"source", L"add", L"fixture",
              widen_ascii(file_url(v1_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add_v1.run().exit_code, 0);
  Pipeline use_v1;
  use_v1.add(L"winuxcmd.exe",
             {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use_v1.run().exit_code, 0);

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  ASSERT_EQ(install.run().exit_code, 0);

  // The local index copy moves to 2.0.0; the disk still holds 1.0.0.
  Pipeline add_v2;
  add_v2.add(L"winuxcmd.exe",
             {L"wpm", L"source", L"add", L"fixture-v2",
              widen_ascii(file_url(v2_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add_v2.run().exit_code, 0);
  Pipeline use_v2;
  use_v2.add(L"winuxcmd.exe", {L"wpm", L"source", L"use", L"fixture-v2",
                               L"--root", tmp.wpath()});
  EXPECT_EQ(use_v2.run().exit_code, 0);
  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  ASSERT_EQ(update.run().exit_code, 0);

  // outdated must anchor on the receipt (1.0.0 installed), not on the
  // refreshed local index: after `index update` it used to claim "all up to
  // date" even though the old files were still on disk (#1156).
  Pipeline outdated_json;
  outdated_json.add(L"winuxcmd.exe",
                    {L"wpm", L"outdated", L"--json", L"--root", tmp.wpath()});
  auto r = outdated_json.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "\"version\": \"1.0.0\"");
  EXPECT_CONTAINS(r.stdout_text, "\"available\": \"2.0.0\"");

  Pipeline outdated_human;
  outdated_human.add(L"winuxcmd.exe",
                     {L"wpm", L"outdated", L"--root", tmp.wpath()});
  auto human = outdated_human.run();

  EXPECT_EQ(human.exit_code, 0);
  EXPECT_CONTAINS(human.stdout_text, "update available: jq 1.0.0 -> 2.0.0");
  EXPECT_NOT_CONTAINS(human.stdout_text,
                      "all installed packages are up to date");
}

TEST(wpm, wpm_install_json_prints_payload_only) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"jq.exe";
  const auto index_path = tmp.path / L"fixture-install-index.json";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/jq.exe", "external exe\n");
  tmp.write("fixture-install-index.json",
            install_fixture_index_json(artifact_path));

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"jq", L"--json", L"--root", tmp.wpath()});
  auto r = install.run();

  EXPECT_EQ(r.exit_code, 0);
  // stdout carries exactly one machine-readable payload; human progress
  // belongs on stderr (#1156).
  EXPECT_EQ_TEXT(r.stdout_text,
                 "{\n"
                 "  \"failed\": 0,\n"
                 "  \"results\": [\n"
                 "    {\n"
                 "      \"dry_run\": false,\n"
                 "      \"errors\": [],\n"
                 "      \"name\": \"jq\",\n"
                 "      \"status\": \"installed\",\n"
                 "      \"version\": \"1.0.0\"\n"
                 "    }\n"
                 "  ],\n"
                 "  \"schema\": 1\n"
                 "}\n");
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "external exe\n");

  Pipeline again;
  again.add(L"winuxcmd.exe",
            {L"wpm", L"install", L"jq", L"--json", L"--root", tmp.wpath()});
  auto repeat = again.run();

  EXPECT_EQ(repeat.exit_code, 0);
  EXPECT_CONTAINS(repeat.stdout_text, "\"status\": \"already_installed\"");
  EXPECT_NOT_CONTAINS(repeat.stdout_text, "wpm:");
}

TEST(wpm, wpm_index_update_json_prints_payload) {
  TempDir tmp;
  const auto index_path = tmp.path / L"fixture-index.json";
  tmp.write("fixture-index.json", catalog_fixture_index_json());

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline p;
  p.add(L"winuxcmd.exe",
        {L"wpm", L"index", L"update", L"--json", L"--root", tmp.wpath()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "\"status\": \"updated\"");
  EXPECT_NOT_CONTAINS(r.stdout_text, "wpm: index updated");
  EXPECT_CONTAINS(r.stderr_text, "index updated from");
}
