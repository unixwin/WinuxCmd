// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//
// Regression pins for unixwin/WinuxCmd#1537: a `layout: shim` package whose
// payload arrives through a directory-level `files` mapping (the mingw
// shape: {"from":"toolbox64/","to":"./"}) installed its usr\bin forwarders
// but never wrote the payload registrations into .wpm\shims.map, so every
// command answered "no payload is registered". The payload resolution only
// knew the opt\<pkg>\<cmd>.exe convention and per-file mappings; the mapped
// directory tree was never searched, at install time or by `links rebuild`.

#include "framework/winuxtest.h"

namespace {

auto build_winuxcmd_path() -> std::filesystem::path {
  const auto build_dir = std::filesystem::path(WINUXCMD_BIN_DIR);
  const auto installed = build_dir / L"usr" / L"bin" / L"winuxcmd.exe";
  if (std::filesystem::is_regular_file(installed)) return installed;
  return build_dir / L"winuxcmd.exe";
}

auto canonical_exe(const std::filesystem::path& root, const wchar_t* name)
    -> std::filesystem::path {
  return root / L"usr" / L"bin" / name;
}

auto current_arch_key() -> std::string {
#if defined(_M_ARM64) || defined(__aarch64__)
  return "windows-arm64";
#else
  return "windows-x64";
#endif
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

// Stages a root whose only package is `toolbox`: a tar.gz holding a
// toolbox64 tree (bin\tool.exe, bin\helper.exe) mapped directory-level onto
// the payload root - the mingw shape from wpm-source. Returns false when the
// archive could not be staged.
auto stage_directory_mapped_root(TempDir& tmp) -> bool {
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("payload/toolbox64/bin/tool.exe", "external exe\n");
  tmp.write("payload/toolbox64/bin/helper.exe", "external exe\n");

  const auto archive_path = tmp.path / L"toolbox.tar.gz";
  auto tar_result = run_command(
      L"tar.exe", {L"-czf", archive_path.wstring(), L"-C",
                   (tmp.path / L"payload").wstring(), L"."});
  if (tar_result.exit_code != 0) return false;
  auto sha256 = sha256_file_hex(archive_path);
  if (sha256.size() != 64) return false;
  auto size = std::filesystem::file_size(archive_path);
  tmp.write(".wpm/indexes/official.json",
            "{\n"
            "  \"schema\": 1,\n"
            "  \"name\": \"fixture\",\n"
            "  \"version\": \"fixture-dir-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"toolbox\",\"version\":\"1.0.0\","
            "\"description\":\"directory-mapped shim fixture\","
            "\"kind\":\"external\",\"commands\":[\"tool\",\"helper\"],"
            "\"artifacts\":{\"" +
                current_arch_key() + "\":{\"type\":\"tar.gz\",\"size\":" +
                std::to_string(size) + ",\"sha256\":\"" + sha256 +
                "\",\"layout\":\"shim\",\"urls\":[\"" +
                file_url(archive_path) +
                "\"],\"files\":[{\"from\":\"toolbox64/\",\"to\":\"./\"}]}}}\n"
                "  ]\n"
                "}\n");
  return true;
}

}  // namespace

TEST(wpm, issue1537_directory_mapped_shim_install_registers_payloads) {
  TempDir tmp;
  ASSERT_TRUE(stage_directory_mapped_root(tmp));
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"toolbox", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);

  const auto payload =
      tmp.path / L"opt" / L"toolbox" / L"bin" / L"tool.exe";
  EXPECT_TRUE(std::filesystem::exists(payload));
  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  EXPECT_TRUE(same_file(forwarder, tmp.path / L"usr" / L"bin" / L"tool.exe"));
  EXPECT_TRUE(
      same_file(forwarder, tmp.path / L"usr" / L"bin" / L"helper.exe"));

  // The defect: these two registrations were never written, so both
  // forwarders died with "no payload is registered".
  const auto map = tmp.read(".wpm/shims.map");
  EXPECT_CONTAINS(map, "tool=opt\\toolbox\\bin\\tool.exe");
  EXPECT_CONTAINS(map, "helper=opt\\toolbox\\bin\\helper.exe");

  // The PATH entry resolves through the same directory mapping; the payload
  // carries no private DLLs, so it stays a free hardlink to the payload.
  EXPECT_TRUE(same_file(payload, tmp.path / L"bin" / L"tool.exe"));
}

TEST(wpm, issue1537_links_rebuild_registers_directory_mapped_payloads) {
  TempDir tmp;
  ASSERT_TRUE(stage_directory_mapped_root(tmp));

  // Legacy state: installed before the forwarder existed - the shims link
  // winuxcmd.exe and the map does not exist.
  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"toolbox", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  const auto shim = tmp.path / L"usr" / L"bin" / L"tool.exe";
  EXPECT_TRUE(same_file(root_exe, shim));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L".wpm" / L"shims.map"));

  // The forwarder arrives; one rebuild must register the directory-mapped
  // payloads too (it only relinked before, so it could not heal #1537).
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");
  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  Pipeline rebuild;
  rebuild.add(L"winuxcmd.exe", {L"wpm", L"links", L"rebuild", L"--force",
                                L"--root", tmp.wpath()});
  EXPECT_EQ(rebuild.run().exit_code, 0);
  EXPECT_TRUE(same_file(forwarder, shim));
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"),
                  "tool=opt\\toolbox\\bin\\tool.exe");
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"),
                  "helper=opt\\toolbox\\bin\\helper.exe");
}
