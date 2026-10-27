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
  EXPECT_CONTAINS(help_result.stdout_text, "Home: <https://unixwin.dev>");
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

TEST(wpm, wpm_apply_update_relinks_a_stale_command_link) {
  TempDir tmp;
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);

  Pipeline rebuild;
  rebuild.add(L"winuxcmd.exe",
              {L"wpm", L"links", L"rebuild", L"--root", tmp.wpath()});
  ASSERT_EQ(rebuild.run().exit_code, 0);

  // Leave one name on a previous image - the mixed state an interrupted
  // update used to keep silently (#1206). The update itself must move it
  // onto the new image, so nothing keeps running the old build.
  const auto previous = tmp.path / L"previous-image.exe";
  tmp.write("previous-image.exe", "previous winuxcmd image\n");
  const auto stale = tmp.path / L"usr" / L"bin" / L"yes.exe";
  std::filesystem::remove(stale);
  ASSERT_TRUE(CreateHardLinkW(stale.wstring().c_str(),
                              previous.wstring().c_str(), nullptr) != 0);
  EXPECT_FALSE(same_file(root_exe, stale));

  auto result = run_command(
      build_winuxcmd_path().wstring(),
      {L"wpm", L"--", L"__apply-update", L"--root", tmp.wpath(), L"--payload",
       build_winuxcmd_path().wstring(), L"--parent", L"0"});

  EXPECT_EQ(result.exit_code, 0);
  // Every name in usr/bin that links to winuxcmd.exe shares its inode again,
  // the one that was left behind included.
  EXPECT_TRUE(same_file(root_exe, stale));
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

TEST(wpm, wpm_install_success_line_reports_version_and_destination_1363) {
  // #1363: the success line is the only confirmation a plain install prints,
  // so it must carry the package name, the installed version and the primary
  // destination. The benign already-installed notice stays exactly as it was
  // and must not repeat the success line.
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"jq.exe";
  const auto index_path = tmp.path / L"fixture-install-index.json";
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
  EXPECT_TRUE(install_result.stdout_text.find("installed jq 1.0.0") !=
              std::string::npos);
  EXPECT_TRUE(install_result.stdout_text.find("usr\\bin\\jq.exe") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("usr/bin/jq.exe"), "external exe\n");

  Pipeline again;
  again.add(L"winuxcmd.exe",
            {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
  auto again_result = again.run();
  EXPECT_EQ(again_result.exit_code, 0);
  EXPECT_TRUE(again_result.stdout_text.find("already installed jq") !=
              std::string::npos);
  EXPECT_TRUE(again_result.stdout_text.find("installed jq 1.0.0") ==
              std::string::npos);
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

  // A locked destination is an error, not a silent skip (#1156). The install
  // now refuses *before* touching anything - a file held by a running process
  // cannot be replaced at all (delete and overwrite both return Access denied),
  // so discovering it mid-copy used to leave the package half-applied - and the
  // refusal names the file to close.
  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "in use and cannot be replaced");
  EXPECT_CONTAINS(r.stderr_text, "jq.exe");
  EXPECT_EQ(r.stdout_text.find("installing files"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(locked));
}

TEST(wpm, wpm_install_replaces_a_destination_a_process_runs_from) {
  TempDir tmp;
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);

  // The fixture artifact is a real executable installed under an applet name,
  // so the installed destination can actually be started - which is the shape
  // git's fsmonitor daemon leaves behind for libexec/git-core/git.exe.
  const auto artifact_path = tmp.path / L"source" / L"sleep.exe";
  std::filesystem::create_directories(artifact_path.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), artifact_path,
                             std::filesystem::copy_options::overwrite_existing);
  const auto sha = sha256_file_hex(artifact_path);
  ASSERT_EQ(sha.size(), 64u);
  if (sha.size() != 64) return;
  tmp.write(".wpm/indexes/official.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-live-image\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"sleeper\",\"version\":\"1.0.0\","
            "\"description\":\"live image fixture\","
            "\"kind\":\"external\",\"commands\":[\"sleep\"],"
            "\"artifacts\":{\"" +
                current_arch_key() + "\":{\"type\":\"exe\",\"sha256\":\"" +
                sha + "\",\"urls\":[\"" + file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"sleep.exe\","
                "\"to\":\"sleep.exe\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"sleeper", L"--root", tmp.wpath()});
  auto install_result = install.run();
  ASSERT_EQ(install_result.exit_code, 0);
  if (install_result.exit_code != 0) return;

  const auto destination = tmp.path / L"usr" / L"bin" / L"sleep.exe";
  ASSERT_TRUE(std::filesystem::exists(destination));

  // Start a process whose image is the destination. It refuses delete and
  // overwrite (Access denied) but can be renamed aside, so the upgrade must
  // complete by parking the old name.
  std::wstring command = L"\"" + destination.wstring() + L"\" 30";
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const auto started =
      CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  ASSERT_TRUE(started);
  if (!started) return;

  Pipeline force;
  force.add(L"winuxcmd.exe", {L"wpm", L"install", L"sleeper", L"--force",
                              L"--root", tmp.wpath()});
  auto r = force.run();

  TerminateProcess(process.hProcess, 0);
  WaitForSingleObject(process.hProcess, 5000);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "installing files");
  EXPECT_NOT_CONTAINS(r.stdout_text, "already installed");
  EXPECT_CONTAINS(r.stdout_text, "installed sleeper");
  EXPECT_TRUE(std::filesystem::exists(destination));
  EXPECT_NOT_CONTAINS(r.stderr_text, "failed to replace");
  EXPECT_NOT_CONTAINS(r.stderr_text, "in use and cannot be replaced");
  // The name now holds a new file, while the process keeps running from the
  // old inode (visible through the spare link).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "installing files");
  EXPECT_NOT_CONTAINS(r.stdout_text, "already installed");
  EXPECT_CONTAINS(r.stdout_text, "installed sleeper");
  EXPECT_TRUE(std::filesystem::exists(destination));
  EXPECT_NOT_CONTAINS(r.stderr_text, "failed to replace");
  EXPECT_NOT_CONTAINS(r.stderr_text, "in use and cannot be replaced");
  // Both the parked old bytes (the running process keeps them) and the new file
  // under the destination must still be executable.
  EXPECT_TRUE(std::filesystem::exists(destination));
  Pipeline still_works;
  still_works.add(destination.wstring(), {L"0"});
  EXPECT_EQ(still_works.run().exit_code, 0);

  // The parked names are cleaned up by `wpm clean` when their holder is gone.
  Pipeline clean;
  clean.add(L"winuxcmd.exe",
            {L"wpm", L"clean", L"staging", L"--root", tmp.wpath()});
  auto cleaned = clean.run();
  EXPECT_EQ(cleaned.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(destination));
  EXPECT_NOT_CONTAINS(cleaned.stderr_text, "failed to clean");
}

TEST(wpm, wpm_links_verify_names_stale_command_links) {
  TempDir tmp;
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);

  Pipeline rebuild;
  rebuild.add(L"winuxcmd.exe",
              {L"wpm", L"links", L"rebuild", L"--root", tmp.wpath()});
  ASSERT_EQ(rebuild.run().exit_code, 0);

  // A consistent installation verifies clean.
  Pipeline clean;
  clean.add(L"winuxcmd.exe",
            {L"wpm", L"links", L"verify", L"--root", tmp.wpath()});
  auto clean_result = clean.run();
  EXPECT_EQ(clean_result.exit_code, 0);
  EXPECT_CONTAINS(clean_result.stdout_text, "every command link points");

  // Point one name at another image, which is what an update that could not
  // replace it leaves behind (#1206).
  const auto stale_source = tmp.path / L"previous-image.exe";
  tmp.write("previous-image.exe", "previous winuxcmd image\n");
  const auto stale = tmp.path / L"usr" / L"bin" / L"yes.exe";
  std::filesystem::remove(stale);
  ASSERT_TRUE(CreateHardLinkW(stale.wstring().c_str(),
                              stale_source.wstring().c_str(), nullptr) != 0);

  Pipeline verify;
  verify.add(L"winuxcmd.exe",
             {L"wpm", L"links", L"verify", L"--root", tmp.wpath()});
  auto r = verify.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "yes.exe");
  EXPECT_CONTAINS(r.stderr_text, "links rebuild --force");
}

TEST(wpm, wpm_links_rebuild_names_a_link_it_cannot_replace) {
  TempDir tmp;
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);

  // A stale command link: `yes.exe` points at a different image (exactly what
  // an interrupted update leaves behind).
  const auto stale_source = tmp.path / L"previous-image.exe";
  tmp.write("previous-image.exe", "previous winuxcmd image\n");
  const auto stale = tmp.path / L"usr" / L"bin" / L"yes.exe";
  ASSERT_TRUE(CreateHardLinkW(stale.wstring().c_str(),
                              stale_source.wstring().c_str(), nullptr) != 0);

  // Hold it without FILE_SHARE_DELETE, so neither deleting nor renaming the
  // name is possible: the rebuild must say *which* link it could not replace
  // instead of leaving a bare count behind (#1206).
  HANDLE hold =
      CreateFileW(stale.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_TRUE(hold != INVALID_HANDLE_VALUE);
  if (hold == INVALID_HANDLE_VALUE) return;

  Pipeline rebuild;
  rebuild.add(L"winuxcmd.exe", {L"wpm", L"links", L"rebuild", L"--force",
                                L"--root", tmp.wpath()});
  auto r = rebuild.run();
  CloseHandle(hold);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "yes.exe");
  EXPECT_CONTAINS(r.stderr_text, "failed to replace the command link");
  // Every other name still resolves to the current image.
  EXPECT_TRUE(same_file(root_exe, tmp.path / L"usr" / L"bin" / L"ls.exe"));
  EXPECT_FALSE(same_file(root_exe, stale));
}

TEST(wpm, wpm_clean_backup_removes_rollback_copies) {
  TempDir tmp;
  tmp.write(".wpm/backup/winuxcmd-before-update-111.exe", "old image\n");
  tmp.write(".wpm/backup/winuxcmd-before-update-222.exe", "older image\n");
  tmp.write(".wpm/indexes/official.json", "index\n");

  Pipeline backup;
  backup.add(L"winuxcmd.exe",
             {L"wpm", L"clean", L"backup", L"--root", tmp.wpath()});
  auto r = backup.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm" / L"backup" /
                                       L"winuxcmd-before-update-111.exe"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L".wpm" / L"indexes" /
                                      L"official.json"));

  // `clean all` reclaims the rollback copies too (#1208).
  tmp.write(".wpm/backup/winuxcmd-before-update-333.exe", "another\n");
  Pipeline all;
  all.add(L"winuxcmd.exe", {L"wpm", L"clean", L"all", L"--root", tmp.wpath()});
  EXPECT_EQ(all.run().exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm" / L"backup"));
}

namespace {

auto count_update_backups(const std::filesystem::path& root) -> int {
  int count = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(
           root / L".wpm" / L"backup",
           std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (entry.path().filename().string().rfind("winuxcmd-before-update-", 0) ==
        0)
      ++count;
  }
  return count;
}

}  // namespace

TEST(wpm, wpm_apply_update_keeps_two_rollback_backups) {
  TempDir tmp;
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  tmp.write("usr/bin/winuxcmd.exe", "old image\n");

  // A machine that has updated a few times: one rollback copy per run, never
  // pruned (#1208). Write time, not the pid in the name, decides what stays.
  namespace chrono = std::chrono;
  const auto now = std::filesystem::file_time_type::clock::now();
  for (int age_hours : {96, 72, 48, 24}) {
    const auto backup =
        tmp.write_file(".wpm/backup/winuxcmd-before-update-" +
                           std::to_string(age_hours) + ".exe",
                       "image from " + std::to_string(age_hours) + "h ago\n");
    std::error_code time_ec;
    std::filesystem::last_write_time(backup, now - chrono::hours(age_hours),
                                     time_ec);
    ASSERT_FALSE(time_ec);
  }
  EXPECT_EQ(count_update_backups(tmp.path), 4);

  auto result = run_command(
      build_winuxcmd_path().wstring(),
      {L"wpm", L"--", L"__apply-update", L"--root", tmp.wpath(), L"--payload",
       build_winuxcmd_path().wstring(), L"--parent", L"0"});

  EXPECT_EQ(result.exit_code, 0);
  // Ten updates of the same version leave at most two backup files: the
  // newest earlier copy, plus the fresh rollback this run just wrote.
  EXPECT_EQ(count_update_backups(tmp.path), 2);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm" / L"backup" /
                                       L"winuxcmd-before-update-96.exe"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L".wpm" / L"backup" /
                                      L"winuxcmd-before-update-24.exe"));
  // The fresh rollback holds the image this update replaced, so a failed
  // swap still has something to restore from.
  EXPECT_CONTAINS(tmp.read(".wpm/backup/winuxcmd-before-update-24.exe"),
                  "image from 24h ago");
  const auto backups = tmp.path / L".wpm" / L"backup";
  for (const auto& entry : std::filesystem::directory_iterator(backups)) {
    if (entry.path().filename().string() == "winuxcmd-before-update-24.exe")
      continue;
    EXPECT_EQ(tmp.read(".wpm/backup/" + entry.path().filename().string()),
              "old image\n");
  }
}

TEST(wpm, wpm_apply_update_failed_swap_keeps_a_usable_backup) {
  TempDir tmp;
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  tmp.write("usr/bin/winuxcmd.exe", "old image\n");

  // Hold the image the way a running program does - without FILE_SHARE_WRITE
  // or FILE_SHARE_DELETE - so neither the unlink nor the in-place replace can
  // land. The update must fail loudly, leave the previous image running, and
  // keep a usable rollback copy (#1208).
  HANDLE hold =
      CreateFileW(root_exe.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_TRUE(hold != INVALID_HANDLE_VALUE);
  if (hold == INVALID_HANDLE_VALUE) return;

  auto result = run_command(
      build_winuxcmd_path().wstring(),
      {L"wpm", L"--", L"__apply-update", L"--root", tmp.wpath(), L"--payload",
       build_winuxcmd_path().wstring(), L"--parent", L"0"});
  CloseHandle(hold);

  EXPECT_NE(result.exit_code, 0);
  EXPECT_CONTAINS(result.stderr_text, "failed to replace winuxcmd.exe");
  EXPECT_EQ(tmp.read("usr/bin/winuxcmd.exe"), "old image\n");
  // The one rollback copy still holds the image that was running.
  EXPECT_EQ(count_update_backups(tmp.path), 1);
  for (const auto& entry :
       std::filesystem::directory_iterator(tmp.path / L".wpm" / L"backup")) {
    EXPECT_CONTAINS(tmp.read(".wpm/backup/" + entry.path().filename().string()),
                    "old image\n");
  }
}

TEST(wpm, wpm_shim_packages_use_the_forwarder_when_present) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tool.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/tool.exe", "external exe\n");
  tmp.write("fixture-shim-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"tool\",\"version\":\"1.0.0\","
            "\"description\":\"shim fixture\",\"kind\":\"external\","
            "\"commands\":[\"tool\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                "a1e090af291226f322\",\"layout\":\"shim\","
                "\"urls\":[\"" +
                file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"tool.exe\","
                "\"to\":\"tool.exe\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(tmp.path / L"fixture-shim-index.json")),
           L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);
  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  // Without the forwarder the shim stays a link to winuxcmd.exe: the previous
  // behaviour, so an installation that predates the forwarder keeps working.
  Pipeline first;
  first.add(L"winuxcmd.exe",
            {L"wpm", L"install", L"tool", L"--root", tmp.wpath()});
  EXPECT_EQ(first.run().exit_code, 0);
  const auto shim = tmp.path / L"usr" / L"bin" / L"tool.exe";
  EXPECT_TRUE(same_file(root_exe, shim));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm" / L"shims.map"));

  // With it present the shim links the forwarder and the map names the payload,
  // so a `layout: shim` command no longer pays a WinuxCmd startup
  // (unixwin/WinuxCmd#1207).
  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");
  Pipeline again;
  again.add(L"winuxcmd.exe",
            {L"wpm", L"install", L"tool", L"--force", L"--root", tmp.wpath()});
  EXPECT_EQ(again.run().exit_code, 0);
  EXPECT_TRUE(same_file(forwarder, shim));
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"), "tool=opt");
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"), "tool.exe");
}

// The PATH-entry link (<root>\bin\<cmd>.exe) normally hardlinks the payload
// itself - that saves a forwarding process per call. Commands the forwarders
// answer with an argv adapter (gawk's `-v BINMODE=3`, unixwin/niubash#467)
// must route the entry link through a forwarder instead, or `bin\gawk.exe`
// would reach the payload with its text-mode stdio unadapted.
TEST(wpm, wpm_adapted_command_entry_link_targets_the_forwarder) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"gawk.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/gawk.exe", "external exe\n");
  tmp.write("fixture-shim-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"gawk\",\"version\":\"1.0.0\","
            "\"description\":\"shim fixture\",\"kind\":\"external\","
            "\"commands\":[\"gawk\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                "a1e090af291226f322\",\"layout\":\"shim\","
                "\"urls\":[\"" +
                file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"gawk.exe\","
                "\"to\":\"gawk.exe\"}]}}},\n"
            "    {\"name\":\"tool\",\"version\":\"1.0.0\","
            "\"description\":\"control fixture\",\"kind\":\"external\","
            "\"commands\":[\"tool\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                "a1e090af291226f322\",\"layout\":\"shim\","
                "\"urls\":[\"" +
                file_url(tmp.path / L"source" / L"tool.exe") +
                "\"],\"files\":[{\"from\":\"tool.exe\","
                "\"to\":\"tool.exe\"}]}}}\n"
            "  ]\n"
            "}\n");
  tmp.write("source/tool.exe", "external exe\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(tmp.path / L"fixture-shim-index.json")),
           L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);
  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");
  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"gawk", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);
  Pipeline install_tool;
  install_tool.add(L"winuxcmd.exe",
                   {L"wpm", L"install", L"tool", L"--root", tmp.wpath()});
  EXPECT_EQ(install_tool.run().exit_code, 0);

  // Adapted command: the PATH entry routes through the forwarder.
  EXPECT_TRUE(
      same_file(forwarder, tmp.path / L"bin" / L"gawk.exe"));
  // Unadapted command: the PATH entry stays a free link to the payload.
  EXPECT_TRUE(same_file(tmp.path / L"opt" / L"tool" / L"tool.exe",
                        tmp.path / L"bin" / L"tool.exe"));
}

// The uninstall path must recognise a shim that links the forwarder: it used
// to compare every usr\bin entry against winuxcmd.exe only, so a forwarder
// package could never be uninstalled and left its map entries behind
// (unixwin/WinuxCmd#1207).
TEST(wpm, wpm_uninstall_removes_forwarder_shims_and_map_entries) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tool.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/tool.exe", "external exe\n");
  tmp.write("fixture-shim-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"tool\",\"version\":\"1.0.0\","
            "\"description\":\"shim fixture\",\"kind\":\"external\","
            "\"commands\":[\"tool\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                "a1e090af291226f322\",\"layout\":\"shim\","
                "\"urls\":[\"" +
                file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"tool.exe\","
                "\"to\":\"tool.exe\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(tmp.path / L"fixture-shim-index.json")),
           L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);
  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");
  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"tool", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);
  const auto shim = tmp.path / L"usr" / L"bin" / L"tool.exe";
  EXPECT_TRUE(same_file(forwarder, shim));
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"), "tool=");

  Pipeline uninstall;
  uninstall.add(L"winuxcmd.exe", {L"wpm", L"uninstall", L"tool", L"--yes",
                                  L"--root", tmp.wpath()});
  const auto result = uninstall.run();
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(shim));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"opt" / L"tool"));
  EXPECT_NOT_CONTAINS(tmp.read(".wpm/shims.map"), "tool=");
  EXPECT_NOT_CONTAINS(result.stderr_text, "protected");
}

// A shim that is not ours (a user file on the same name) survives an
// uninstall; the package's own files still go.
TEST(wpm, wpm_uninstall_keeps_a_foreign_shim_file) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tool.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/tool.exe", "external exe\n");
  tmp.write("fixture-shim-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"tool\",\"version\":\"1.0.0\","
            "\"description\":\"shim fixture\",\"kind\":\"external\","
            "\"commands\":[\"tool\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                "a1e090af291226f322\",\"layout\":\"shim\","
                "\"urls\":[\"" +
                file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"tool.exe\","
                "\"to\":\"tool.exe\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(tmp.path / L"fixture-shim-index.json")),
           L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);
  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");
  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"tool", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);

  // Replace the shim with a foreign file: unlink the name first, otherwise the
  // write would land in the forwarder's inode.
  const auto shim = tmp.path / L"usr" / L"bin" / L"tool.exe";
  std::error_code ec;
  std::filesystem::remove(shim, ec);
  ASSERT_FALSE(ec);
  tmp.write("usr/bin/tool.exe", "a user tool that only looks like a shim\n");

  Pipeline uninstall;
  uninstall.add(L"winuxcmd.exe", {L"wpm", L"uninstall", L"tool", L"--yes",
                                  L"--json", L"--root", tmp.wpath()});
  const auto result = uninstall.run();
  // The refusal is soft: the package itself is removed, the foreign file is
  // reported and kept.
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(shim));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"opt" / L"tool"));
  EXPECT_CONTAINS(result.stdout_text, "protected");
  EXPECT_CONTAINS(result.stdout_text, "\"status\": \"removed\"");
  // The map entry goes with the package either way.
  EXPECT_NOT_CONTAINS(tmp.read(".wpm/shims.map"), "tool=");
}

// An installation that received the forwarder through a self-update has no
// map yet and its shims are still links of winuxcmd.exe. One
// `links rebuild --force` - which activation runs at every shell start - has
// to record the payloads and move the shims onto the forwarder
// (unixwin/WinuxCmd#1207).
TEST(wpm, wpm_links_rebuild_migrates_legacy_shim_receipts) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tool.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/tool.exe", "external exe\n");
  tmp.write("fixture-shim-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"tool\",\"version\":\"1.0.0\","
            "\"description\":\"shim fixture\",\"kind\":\"external\","
            "\"commands\":[\"tool\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                "a1e090af291226f322\",\"layout\":\"shim\","
                "\"urls\":[\"" +
                file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"tool.exe\","
                "\"to\":\"tool.exe\"}]}}}\n"
                "  ]\n"
                "}\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(tmp.path / L"fixture-shim-index.json")),
           L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);
  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  // The legacy state: the shim links winuxcmd.exe and the map does not exist.
  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"tool", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);
  const auto shim = tmp.path / L"usr" / L"bin" / L"tool.exe";
  EXPECT_TRUE(same_file(root_exe, shim));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm" / L"shims.map"));

  // The forwarder arrives with a self-update; the next rebuild migrates.
  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");
  Pipeline rebuild;
  rebuild.add(L"winuxcmd.exe", {L"wpm", L"links", L"rebuild", L"--force",
                                L"--root", tmp.wpath()});
  EXPECT_EQ(rebuild.run().exit_code, 0);
  EXPECT_TRUE(same_file(forwarder, shim));
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"), "tool=opt\\tool\\tool.exe");

  // Idempotent: a second rebuild keeps the map and the link as they are.
  Pipeline again;
  again.add(L"winuxcmd.exe",
            {L"wpm", L"links", L"rebuild", L"--force", L"--root", tmp.wpath()});
  EXPECT_EQ(again.run().exit_code, 0);
  EXPECT_TRUE(same_file(forwarder, shim));
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"), "tool=opt\\tool\\tool.exe");
}

// Self-update lands the staged forwarder in usr\bin, so an installation picks
// up the cheap dispatch without reinstalling its packages
// (unixwin/WinuxCmd#1207).
TEST(wpm, wpm_apply_update_installs_the_forwarder) {
  TempDir tmp;
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  tmp.write("usr/bin/winuxcmd.exe", "old image\n");
  tmp.write("source/winuxcmd-shim.exe", "forwarder stub\n");

  auto result = run_command(
      build_winuxcmd_path().wstring(),
      {L"wpm", L"--", L"__apply-update", L"--root", tmp.wpath(), L"--payload",
       build_winuxcmd_path().wstring(), L"--parent", L"0", L"--forwarder",
       (tmp.path / L"source" / L"winuxcmd-shim.exe").wstring()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(tmp.read("usr/bin/winuxcmd-shim.exe"), "forwarder stub\n");
  // The swap itself still happened.
  EXPECT_NE(tmp.read("usr/bin/winuxcmd.exe"), "old image\n");

  // Without a staged forwarder nothing is written and nothing fails.
  TempDir quiet;
  const auto quiet_exe = canonical_exe(quiet.path, L"winuxcmd.exe");
  std::filesystem::create_directories(quiet_exe.parent_path());
  quiet.write("usr/bin/winuxcmd.exe", "old image\n");
  auto plain = run_command(
      build_winuxcmd_path().wstring(),
      {L"wpm", L"--", L"__apply-update", L"--root", quiet.wpath(), L"--payload",
       build_winuxcmd_path().wstring(), L"--parent", L"0"});
  EXPECT_EQ(plain.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(quiet.path / L"usr" / L"bin" /
                                       L"winuxcmd-shim.exe"));
}

namespace {

// An index with `git` forwarding to `target`, plus a cycle and a dangling alias.
std::string alias_fixture_index_json(const std::filesystem::path& artifact_path) {
  return std::string("{\n"
                     "  \"schema\": 1,\n"
                     "  \"name\": \"fixture\",\n"
                     "  \"version\": \"fixture-alias\",\n"
                     "  \"packages\": [\n"
                     "    {\"name\":\"target\",\"version\":\"1.0.0\","
                     "\"description\":\"alias target\",\"kind\":\"external\","
                     "\"commands\":[\"tool\"],\"artifacts\":{\"") +
         std::string(current_arch_key()) +
         "\":{\"type\":\"exe\","
         "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38a1e090a"
         "f291226f322\",\"urls\":[\"" +
         file_url(artifact_path) +
         "\"],\"files\":[{\"from\":\"tool.exe\",\"to\":\"tool.exe\"}]}}},\n"
         "    {\"name\":\"git\",\"version\":\"2.0.0\","
         "\"description\":\"alias of target\",\"kind\":\"external\","
         "\"alias_of\":\"target\"},\n"
         "    {\"name\":\"loop-a\",\"version\":\"1.0.0\","
         "\"alias_of\":\"loop-b\"},\n"
         "    {\"name\":\"loop-b\",\"version\":\"1.0.0\","
         "\"alias_of\":\"loop-a\"},\n"
         "    {\"name\":\"dangling\",\"version\":\"1.0.0\","
         "\"alias_of\":\"not-in-this-index\"}\n"
         "  ]\n"
         "}\n";
}

}  // namespace

TEST(wpm, wpm_shim_map_points_at_a_non_conventional_payload) {
  // A payload that must keep its own directory layout (codex ships
  // bin/codex.exe beside codex-resources/) declares its destination in `files`;
  // the shim map has to follow that, not the opt/<pkg>/<cmd>.exe convention.
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tool.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/tool.exe", "external exe\n");
  tmp.write("fixture-nested-shim-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-nested-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"tool\",\"version\":\"1.0.0\","
            "\"description\":\"nested shim fixture\",\"kind\":\"external\","
            "\"commands\":[\"tool\"],\"artifacts\":{\"" +
                current_arch_key() +
                "\":{\"type\":\"exe\","
                "\"sha256\":\"5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                "a1e090af291226f322\",\"layout\":\"shim\","
                "\"urls\":[\"" + file_url(artifact_path) +
                "\"],\"files\":[{\"from\":\"tool.exe\","
                "\"to\":\"bin/tool.exe\"}]}}}\n"
                "  ]\n"
                "}\n");
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");

  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"fixture",
           widen_ascii(file_url(tmp.path / L"fixture-nested-shim-index.json")),
           L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);
  Pipeline use;
  use.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"use", L"fixture", L"--root", tmp.wpath()});
  EXPECT_EQ(use.run().exit_code, 0);

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"tool", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);

  const auto map = tmp.read(".wpm/shims.map");
  EXPECT_CONTAINS(map, "tool=opt");
  EXPECT_CONTAINS(map, "bin");
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"opt" / L"tool" / L"bin" /
                                      L"tool.exe"));
}

TEST(wpm, wpm_install_follows_a_package_alias) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tool.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/tool.exe", "external exe\n");
  tmp.write(".wpm/indexes/official.json", alias_fixture_index_json(artifact_path));

  // `wpm install git` installs what `git` forwards to, and says so (#1218).
  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"git", L"--root", tmp.wpath()});
  auto r = install.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "is provided by 'target'");
  EXPECT_EQ(tmp.read("usr/bin/tool.exe"), "external exe\n");

  // `info` shows the forwarding too.
  Pipeline info;
  info.add(L"winuxcmd.exe", {L"wpm", L"info", L"git", L"--root", tmp.wpath()});
  auto info_result = info.run();
  EXPECT_EQ(info_result.exit_code, 0);
  EXPECT_CONTAINS(info_result.stdout_text, "Alias of: target");
}

TEST(wpm, wpm_install_rejects_alias_cycles_and_missing_targets) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"tool.exe";
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("source/tool.exe", "external exe\n");
  tmp.write(".wpm/indexes/official.json", alias_fixture_index_json(artifact_path));

  Pipeline cycle;
  cycle.add(L"winuxcmd.exe",
            {L"wpm", L"install", L"loop-a", L"--root", tmp.wpath()});
  auto cycle_result = cycle.run();
  EXPECT_NE(cycle_result.exit_code, 0);
  EXPECT_CONTAINS(cycle_result.stderr_text, "alias cycle");
  EXPECT_CONTAINS(cycle_result.stderr_text, "loop-a -> loop-b -> loop-a");

  Pipeline dangling;
  dangling.add(L"winuxcmd.exe",
               {L"wpm", L"install", L"dangling", L"--root", tmp.wpath()});
  auto dangling_result = dangling.run();
  EXPECT_NE(dangling_result.exit_code, 0);
  EXPECT_CONTAINS(dangling_result.stderr_text, "is not in the index");
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

// Extracts a top-level `"key": "value"` string field from a pretty-printed
// nlohmann payload (config.json, --json output). Returns "" when absent.
auto flat_json_string_field(const std::string& json_text,
                            const std::string& key) -> std::string {
  const auto needle = "\"" + key + "\": \"";
  const auto pos = json_text.find(needle);
  if (pos == std::string::npos) return "";
  const auto start = pos + needle.size();
  const auto end = json_text.find('"', start);
  if (end == std::string::npos || end == start) return "";
  return json_text.substr(start, end - start);
}

auto is_sha256_hex(const std::string& text) -> bool {
  if (text.size() != 64) return false;
  return std::ranges::all_of(text, [](char c) {
    const auto lowered =
        static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return (lowered >= '0' && lowered <= '9') ||
           (lowered >= 'a' && lowered <= 'f');
  });
}

// Issue #1176: a cached index that declares its own 'sources' array must not
// have those sources merged into the candidate set. The fixture smuggles a
// fully self-consistent attack (index + matching artifact sha256 over
// file://) into the cached index only; nothing points at it from the config.
TEST(wpm, wpm_rejects_sources_declared_in_cached_index) {
  TempDir tmp;
  const auto artifact_path = tmp.path / L"source" / L"evil-tool.exe";
  tmp.write("source/evil-tool.exe", "external exe\n");
  const auto evil_index_path = tmp.path / L"evil-index.json";
  // Self-consistent poison: the artifact sha256 matches the payload, so the
  // only defense is refusing to trust the smuggled source at all.
  tmp.write("evil-index.json",
            exe_index_json(artifact_path, "evil-tool", "9.9.9",
                           "5140f4f6bf8b5691b7bccc1c4f00a2027dae00b2110d38"
                           "a1e090af291226f322"));

  // Cached index as a previous poisoned fetch would have persisted it: the
  // package list is empty, but a 'sources' array names the attacker's index
  // with the highest priority.
  tmp.write(".wpm/indexes/official.json",
            std::string("{\n"
                        "  \"schema\": 1,\n"
                        "  \"name\": \"poisoned\",\n"
                        "  \"version\": \"poisoned-1\",\n"
                        "  \"sources\": [\n"
                        "    {\n"
                        "      \"name\": \"cache-evil\",\n"
                        "      \"region\": \"global\",\n"
                        "      \"priority\": 1,\n"
                        "      \"index_urls\": [\"") +
                file_url(evil_index_path) +
                std::string("\"]\n"
                            "    }\n"
                            "  ],\n"
                            "  \"packages\": []\n"
                            "}\n"));

  // The user's only legitimate source is a harmless file:// catalog with a
  // lower priority number than built-ins, so every fetch stays offline and
  // succeeds before any network source is consulted.
  const auto good_index_path = tmp.path / L"good-index.json";
  tmp.write("good-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"good\",\n"
            "  \"version\": \"good-1\",\n"
            "  \"packages\": []\n"
            "}\n");
  Pipeline add;
  add.add(L"winuxcmd.exe",
          {L"wpm", L"source", L"add", L"good",
           widen_ascii(file_url(good_index_path)), L"--root", tmp.wpath()});
  EXPECT_EQ(add.run().exit_code, 0);

  Pipeline list;
  list.add(L"winuxcmd.exe",
           {L"wpm", L"source", L"list", L"--json", L"--root", tmp.wpath()});
  auto list_result = list.run();
  EXPECT_EQ(list_result.exit_code, 0);
  EXPECT_NOT_CONTAINS(list_result.stdout_text, "cache-evil");
  EXPECT_CONTAINS(list_result.stdout_text, "\"name\": \"good\"");

  Pipeline update;
  update.add(L"winuxcmd.exe",
             {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
  auto update_result = update.run();
  EXPECT_EQ(update_result.exit_code, 0);
  EXPECT_CONTAINS(update_result.stdout_text, "index updated from good");

  // End to end: the smuggled package must never become installable even
  // though its index and artifact are mutually consistent.
  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"evil-tool", L"--root", tmp.wpath()});
  auto install_result = install.run();
  EXPECT_NE(install_result.exit_code, 0);
  EXPECT_CONTAINS(install_result.stderr_text,
                  "package not found after index update: evil-tool");
  EXPECT_FALSE(
      std::filesystem::exists(tmp.path / L"usr" / L"bin" / L"evil-tool.exe"));
}

// Issue #1176: `wpm index update` records the winning source and the hash of
// the persisted index; when the content changes while being served from a
// non-preferred source, the user is warned. A same-hash refresh stays quiet.
TEST(wpm, wpm_index_update_records_hash_and_warns_on_nonpreferred_change) {
  TempDir tmp;
  tmp.write("index-a.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"ledger-a\",\n"
            "  \"version\": \"ledger-v1\",\n"
            "  \"packages\": []\n"
            "}\n");
  tmp.write("index-b.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"ledger-b\",\n"
            "  \"version\": \"ledger-v2\",\n"
            "  \"packages\": []\n"
            "}\n");
  const auto index_a = tmp.path / L"index-a.json";
  const auto index_b = tmp.path / L"index-b.json";
  tmp.write(".wpm/config.json",
            std::string("{\n"
                        "  \"preferred_source\": \"ledger-a\",\n"
                        "  \"region\": \"auto\",\n"
                        "  \"user_sources\": [\n"
                        "    {\"name\": \"ledger-a\", \"region\": \"custom\","
                        " \"priority\": 1, \"index_urls\": [\"") +
                file_url(index_a) +
                std::string("\"]},\n"
                            "    {\"name\": \"ledger-b\", \"region\": "
                            "\"custom\", \"priority\": 2, \"index_urls\": "
                            "[\"") +
                file_url(index_b) +
                std::string("\"]}\n"
                            "  ]\n"
                            "}\n"));

  auto run_update = [&tmp]() {
    Pipeline update;
    update.add(L"winuxcmd.exe",
               {L"wpm", L"index", L"update", L"--root", tmp.wpath()});
    return update.run();
  };
  auto read_config = [&tmp]() {
    std::ifstream in(tmp.path / L".wpm" / L"config.json");
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
  };

  // Baseline: the preferred source serves the first index; the ledger is
  // seeded with its source name and content hash.
  auto first = run_update();
  EXPECT_EQ(first.exit_code, 0);
  EXPECT_CONTAINS(first.stdout_text, "index updated from ledger-a");
  const auto config_after_first = read_config();
  EXPECT_CONTAINS(config_after_first, "\"last_success_source\": \"ledger-a\"");
  const auto first_hash =
      flat_json_string_field(config_after_first, "last_index_sha256");
  EXPECT_TRUE(is_sha256_hex(first_hash));

  // The preferred source dies and a mirror with different content answers
  // instead: the update still succeeds (fallback contract) but the change of
  // content on a non-preferred source must be surfaced.
  std::error_code remove_ec;
  std::filesystem::remove(index_a, remove_ec);
  auto second = run_update();
  EXPECT_EQ(second.exit_code, 0);
  EXPECT_CONTAINS(second.stdout_text, "index updated from ledger-b");
  EXPECT_CONTAINS(second.stderr_text, "non-preferred source 'ledger-b'");
  const auto config_after_second = read_config();
  EXPECT_CONTAINS(config_after_second, "\"last_success_source\": \"ledger-b\"");
  const auto second_hash =
      flat_json_string_field(config_after_second, "last_index_sha256");
  EXPECT_TRUE(is_sha256_hex(second_hash));
  EXPECT_NE(second_hash, first_hash);

  // A refresh that returns identical content raises no poisoning warning.
  auto third = run_update();
  EXPECT_EQ(third.exit_code, 0);
  EXPECT_NOT_CONTAINS(third.stderr_text, "non-preferred source");
}

// Issue #1176: the machine-readable payload of `wpm index update --json`
// carries the recorded index hash alongside the winning source.
TEST(wpm, wpm_index_update_json_records_index_sha256) {
  TempDir tmp;
  const auto index_path = tmp.path / L"fixture-index.json";
  tmp.write("fixture-index.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-hash-1\",\n"
            "  \"packages\": []\n"
            "}\n");

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
  EXPECT_CONTAINS(r.stdout_text, "\"source\": \"fixture\"");
  const auto recorded = flat_json_string_field(r.stdout_text, "index_sha256");
  EXPECT_TRUE(is_sha256_hex(recorded));
}

// Issue #1177: probing loopback proxy ports (Clash/V2Ray defaults) is
// opt-in. A dead artifact URL must fail without touching 127.0.0.1 proxy
// candidates unless the user sets allow_loopback_fallback in config. The
// probe leaves a "loopback proxy ... failed" trace in the download error,
// which doubles as the observable for both polarities.
TEST(wpm, wpm_loopback_proxy_fallback_off_by_default_opt_in_via_config) {
  const std::string poisoned_index =
      "{\n"
      "  \"schema\": 1,\n"
      "  \"name\": \"fixture\",\n"
      "  \"version\": \"fixture-loopback\",\n"
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
      "}\n";

  auto run_install = [&poisoned_index](bool allow_loopback) {
    TempDir tmp;
    tmp.write(".wpm/indexes/official.json", poisoned_index);
    if (allow_loopback) {
      tmp.write(".wpm/config.json",
                "{\n  \"allow_loopback_fallback\": true\n}\n");
    }
    Pipeline install;
    install.add(L"winuxcmd.exe",
                {L"wpm", L"install", L"jq", L"--root", tmp.wpath()});
    return install.run();
  };

  // Default: direct failure only — no loopback proxy candidates probed.
  auto denied = run_install(false);
  EXPECT_NE(denied.exit_code, 0);
  EXPECT_CONTAINS(denied.stderr_text,
                  "download failed from http://127.0.0.1:9/jq.exe");
  EXPECT_NOT_CONTAINS(denied.stderr_text, "loopback proxy");

  // Opt-in: the same failure now travels the loopback candidate list.
  auto allowed = run_install(true);
  EXPECT_NE(allowed.exit_code, 0);
  EXPECT_CONTAINS(allowed.stderr_text, "loopback proxy 127.0.0.1:");
}
