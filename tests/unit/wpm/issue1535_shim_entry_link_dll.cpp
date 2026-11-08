// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//
// Regression pins for unixwin/WinuxCmd#1535: under `layout: shim`, the
// PATH-entry link (<root>\bin\<cmd>.exe) of a non-adapter command was a
// hardlink to the payload binary itself. A payload with private DLLs beside
// it (the ezwinports pkg-config shape) then starts with bin\ as its
// application directory, never finds those DLLs, and dies with 0xC0000135.
// Such an entry must route through the forwarder - which starts the payload
// in its own directory - exactly like an argv-adapted command already does;
// a payload without private DLLs keeps the free hardlink.

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

// Stages a root with the forwarder present and one shim package built from
// the staged tar tree: a payload exe, plus its private DLL when `with_dll`.
// Returns false when the archive could not be staged.
auto stage_payload_root(TempDir& tmp, const std::string& package,
                        bool with_dll) -> bool {
  const auto root_exe = canonical_exe(tmp.path, L"winuxcmd.exe");
  std::filesystem::create_directories(root_exe.parent_path());
  std::filesystem::copy_file(build_winuxcmd_path(), root_exe,
                             std::filesystem::copy_options::overwrite_existing);
  tmp.write("usr/bin/winuxcmd-shim.exe", "forwarder stub\n");
  tmp.write("payload/" + package + ".exe", "external exe\n");
  std::string files = "{\"from\":\"" + package + ".exe\",\"to\":\"" +
                      package + ".exe\"}";
  if (with_dll) {
    tmp.write("payload/lib" + package + ".dll", "private dll\n");
    files += ",{\"from\":\"lib" + package + ".dll\",\"to\":\"lib" + package +
             ".dll\"}";
  }

  const auto archive_path = tmp.path / L"payload.tar.gz";
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
            "  \"version\": \"fixture-dll-shim\",\n"
            "  \"packages\": [\n"
            "    {\"name\":\"" +
                package +
                "\",\"version\":\"1.0.0\","
                "\"description\":\"dll shim fixture\","
                "\"kind\":\"external\",\"commands\":[\"" +
                package + "\"],\"artifacts\":{\"" + current_arch_key() +
                "\":{\"type\":\"tar.gz\",\"size\":" + std::to_string(size) +
                ",\"sha256\":\"" + sha256 +
                "\",\"layout\":\"shim\",\"urls\":[\"" +
                file_url(archive_path) + "\"],\"files\":[" + files +
                "]}}}\n"
                "  ]\n"
                "}\n");
  return true;
}

}  // namespace

TEST(wpm, issue1535_entry_link_forwards_when_payload_has_private_dlls) {
  TempDir tmp;
  ASSERT_TRUE(stage_payload_root(tmp, "pkgt", /*with_dll=*/true));

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"pkgt", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);

  const auto payload = tmp.path / L"opt" / L"pkgt" / L"pkgt.exe";
  EXPECT_TRUE(std::filesystem::exists(payload));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"opt" / L"pkgt" /
                                      L"libpkgt.dll"));
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"), "pkgt=opt\\pkgt\\pkgt.exe");

  // The defect: bin\pkgt.exe was a hardlink to the payload itself, so the
  // loader looked for libpkgt.dll in bin\ and the process died 0xC0000135.
  const auto forwarder = tmp.path / L"usr" / L"bin" / L"winuxcmd-shim.exe";
  EXPECT_TRUE(same_file(forwarder, tmp.path / L"usr" / L"bin" / L"pkgt.exe"));
  EXPECT_TRUE(same_file(forwarder, tmp.path / L"bin" / L"pkgt.exe"));
  EXPECT_FALSE(same_file(payload, tmp.path / L"bin" / L"pkgt.exe"));
}

TEST(wpm, issue1535_entry_link_stays_a_payload_link_without_private_dlls) {
  TempDir tmp;
  ASSERT_TRUE(stage_payload_root(tmp, "plain", /*with_dll=*/false));

  Pipeline install;
  install.add(L"winuxcmd.exe",
              {L"wpm", L"install", L"plain", L"--root", tmp.wpath()});
  EXPECT_EQ(install.run().exit_code, 0);

  // No private DLLs: the entry keeps the zero-cost hardlink to the payload.
  const auto payload = tmp.path / L"opt" / L"plain" / L"plain.exe";
  EXPECT_TRUE(std::filesystem::exists(payload));
  EXPECT_TRUE(same_file(payload, tmp.path / L"bin" / L"plain.exe"));
  EXPECT_CONTAINS(tmp.read(".wpm/shims.map"), "plain=opt\\plain\\plain.exe");
}
