/*
 *  Copyright (c) 2026 [caomengxuan666]
 *  @author [arookieofc] <2128194521@qq.com>
 */
export module native_completion;

import std;

export struct NativeCompletionItem {
  std::string text;
  std::string hint;
};

namespace {

struct UserOptionItem {
  std::string command_lower;
  std::string option;
  std::string hint;
};

struct UserCompletionData {
  std::vector<NativeCompletionItem> commands;
  std::vector<UserOptionItem> options;
};

struct BuiltinOptionDef {
  std::string_view command;
  std::string_view option;
  std::string_view hint;
};

constexpr std::pair<std::string_view, std::string_view> kBuiltinCommands[] = {
    {"arp", "Display and modify ARP cache"},
    {"assoc", "Display or modify file extension associations"},
    {"attrib", "Display or change file attributes"},
    {"bcdedit", "Manage boot configuration data"},
    {"cd", "Change current directory"},
    {"chcp", "Display or set active code page"},
    {"chkdsk", "Check a disk and display status report"},
    {"cls", "Clear screen"},
    {"copy", "Copy one or more files"},
    {"date", "Display or set system date"},
    {"del", "Delete one or more files"},
    {"dir", "List files and directories"},
    {"diskpart", "Disk partition management"},
    {"driverquery", "Display installed device drivers"},
    {"echo", "Display text"},
    {"findstr", "Search for strings in files"},
    {"hostname", "Display host name"},
    {"ipconfig", "Display and manage IP configuration"},
    {"mklink", "Create symbolic links"},
    {"move", "Move files"},
    {"net", "Network and service management command set"},
    {"netsh", "Network configuration shell"},
    {"netstat", "Display network connections and ports"},
    {"pathping", "Trace route and packet loss"},
    {"ping", "Send ICMP echo requests"},
    {"powercfg", "Power settings management"},
    {"reg", "Registry query and edit tool"},
    {"ren", "Rename files"},
    {"robocopy", "Robust file copy utility"},
    {"route", "Display and modify routing table"},
    {"sfc", "System file checker"},
    {"shutdown", "Shutdown or restart computer"},
    {"start", "Start a program or command"},
    {"systeminfo", "Display OS and hardware information"},
    {"taskkill", "Terminate processes"},
    {"tasklist", "Display running processes"},
    {"time", "Display or set system time"},
    {"tree", "Display directory tree"},
    {"type", "Display text file contents"},
    {"where", "Locate files in PATH"},
    {"whoami", "Display current user info"},
    {"xcopy", "Copy files and directory trees"},
};

constexpr std::pair<std::string_view, std::string_view> kPowerShellCommands[] =
    {
        {"Get-Process", "Get processes on local/remote machine"},
        {"Get-Service", "Get services on local/remote machine"},
        {"Get-Command", "Get all available commands"},
        {"Get-ChildItem", "List files/directories (PowerShell)"},
        {"Get-Content", "Read file content"},
        {"Set-Content", "Write content to file"},
        {"Set-Location", "Change current location"},
        {"Select-String", "Search text patterns"},
        {"Where-Object", "Filter objects by script block"},
        {"Select-Object", "Select object properties"},
        {"Sort-Object", "Sort objects"},
        {"ForEach-Object", "Process each pipeline object"},
        {"Measure-Object", "Compute count/sum/avg/min/max"},
        {"Start-Process", "Start one or more processes"},
        {"Stop-Process", "Stop one or more running processes"},
        {"Test-Path", "Check whether path exists"},
        {"Out-File", "Send output to file"},
        {"Tee-Object", "Write output to file and pipeline"},
};

constexpr BuiltinOptionDef kBuiltinOptions[] = {
    {"dir", "/a", "Show files with specified attributes"},
    {"dir", "/b", "Bare format (no heading/summary)"},
    {"dir", "/o", "Sort order (n/e/s/d/g/-)"},
    {"dir", "/s", "Include subdirectories"},
    {"dir", "/p", "Pause after each screen"},
    {"dir", "/w", "Wide list format"},
    {"copy", "/a", "Treat source/destination as ASCII"},
    {"copy", "/b", "Treat source/destination as binary"},
    {"copy", "/v", "Verify written files"},
    {"copy", "/y", "Suppress overwrite confirmation"},
    {"copy", "/-y", "Force overwrite confirmation"},
    {"del", "/p", "Prompt before deleting each file"},
    {"del", "/f", "Force delete read-only files"},
    {"del", "/s", "Delete matching files in subdirectories"},
    {"del", "/q", "Quiet mode"},
    {"xcopy", "/s", "Copy directories and subdirectories except empty"},
    {"xcopy", "/e", "Copy all subdirectories including empty"},
    {"xcopy", "/y", "Suppress overwrite prompt"},
    {"xcopy", "/i", "Assume destination is directory"},
    {"xcopy", "/d", "Copy files changed on/after date"},
    {"xcopy", "/h", "Copy hidden/system files"},
    {"robocopy", "/e", "Copy subdirectories including empty"},
    {"robocopy", "/mir", "Mirror a complete directory tree"},
    {"robocopy", "/xo", "Exclude older files"},
    {"robocopy", "/xn", "Exclude newer files"},
    {"robocopy", "/mt", "Multi-threaded copy"},
    {"robocopy", "/r", "Retry count for failed copies"},
    {"robocopy", "/w", "Wait time between retries"},
    {"tasklist", "/fi", "Filter with condition"},
    {"tasklist", "/fo", "Output format TABLE/LIST/CSV"},
    {"tasklist", "/nh", "No header"},
    {"tasklist", "/v", "Verbose output"},
    {"tasklist", "/svc", "Show hosted services"},
    {"taskkill", "/pid", "Terminate by process ID"},
    {"taskkill", "/im", "Terminate by image name"},
    {"taskkill", "/f", "Force termination"},
    {"taskkill", "/t", "Terminate child process tree"},
    {"taskkill", "/fi", "Apply filter"},
    {"where", "/r", "Recursive search from root"},
    {"where", "/q", "Quiet mode"},
    {"findstr", "/i", "Case-insensitive search"},
    {"findstr", "/n", "Show line numbers"},
    {"findstr", "/r", "Use regular expressions"},
    {"findstr", "/s", "Search matching files in current dir and subdirs"},
    {"ping", "-t", "Ping until stopped"},
    {"ping", "-n", "Number of echo requests"},
    {"ping", "-l", "Send buffer size"},
    {"ping", "-w", "Timeout in milliseconds"},
    {"ping", "-4", "Force IPv4"},
    {"ping", "-6", "Force IPv6"},
    {"ipconfig", "/all", "Full configuration details"},
    {"ipconfig", "/release", "Release IPv4 address"},
    {"ipconfig", "/renew", "Renew IPv4 address"},
    {"ipconfig", "/flushdns", "Flush DNS resolver cache"},
    {"ipconfig", "/displaydns", "Display DNS resolver cache"},
    {"netstat", "-a", "Display all active connections and listening ports"},
    {"netstat", "-n", "Show addresses and port numbers numerically"},
    {"netstat", "-o", "Show owning process ID"},
    {"netstat", "-b", "Show executable involved in each connection"},
    {"netstat", "-r", "Display routing table"},
};

constexpr BuiltinOptionDef kPowerShellOptions[] = {
    {"get-process", "-Name", "Filter by process name"},
    {"get-process", "-Id", "Filter by process id"},
    {"get-service", "-Name", "Filter by service name"},
    {"get-command", "-Name", "Filter command names"},
    {"get-childitem", "-Path", "Target path"},
    {"get-childitem", "-Recurse", "Include subdirectories"},
    {"get-childitem", "-File", "Only files"},
    {"get-content", "-Path", "Input file path"},
    {"get-content", "-Raw", "Read full file as one string"},
    {"set-content", "-Path", "Output file path"},
    {"select-string", "-Pattern", "Pattern to search"},
    {"select-string", "-Path", "Target file path"},
    {"where-object", "-FilterScript", "Filter script block"},
    {"select-object", "-First", "Select first N"},
    {"select-object", "-Last", "Select last N"},
    {"sort-object", "-Property", "Sort by property"},
    {"sort-object", "-Descending", "Sort descending"},
    {"foreach-object", "-Process", "Process script block"},
    {"measure-object", "-Property", "Measure property"},
    {"measure-object", "-Sum", "Calculate sum"},
    {"measure-object", "-Average", "Calculate average"},
    {"start-process", "-FilePath", "Executable path"},
    {"start-process", "-ArgumentList", "Argument list"},
    {"stop-process", "-Name", "Target process name"},
    {"stop-process", "-Id", "Target process id"},
    {"test-path", "-Path", "Path to test"},
    {"out-file", "-FilePath", "Output file path"},
    {"tee-object", "-FilePath", "File to tee output"},
};

constexpr uint32_t kCacheMagic = 0x57434331u;  // WCC1
constexpr uint32_t kCacheVersion = 1u;

static std::string toLowerAscii(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static bool startsWithCaseInsensitive(std::string_view text,
                                      std::string_view prefix) {
  if (prefix.size() > text.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    unsigned char a = static_cast<unsigned char>(text[i]);
    unsigned char b = static_cast<unsigned char>(prefix[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

static bool equalsCaseInsensitive(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  return startsWithCaseInsensitive(a, b);
}

static std::string trimAscii(std::string s) {
  auto is_ws = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

static std::vector<std::string> splitString(std::string_view s,
                                            char delimiter) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= s.size()) {
    size_t next = s.find(delimiter, pos);
    if (next == std::string_view::npos) next = s.size();
    out.emplace_back(s.substr(pos, next - pos));
    if (next == s.size()) break;
    pos = next + 1;
  }
  return out;
}

static std::filesystem::path getUserCompletionFilePath() {
  const char* overridePath = std::getenv("WINUXCMD_COMPLETION_FILE");
  if (overridePath && *overridePath) {
    return std::filesystem::path(overridePath);
  }
  const char* userProfile = std::getenv("USERPROFILE");
  if (!userProfile || !*userProfile) return {};
  return std::filesystem::path(userProfile) / ".winuxcmd" / "completions" /
         "user-completions.txt";
}

static std::filesystem::path getUserCompletionCachePath(
    const std::filesystem::path& sourcePath) {
  auto p = sourcePath;
  p += ".cache.bin";
  return p;
}

static bool writeString(std::ofstream& out, const std::string& s) {
  uint32_t len = static_cast<uint32_t>(s.size());
  out.write(reinterpret_cast<const char*>(&len), sizeof(len));
  if (!out.good()) return false;
  if (len > 0) out.write(s.data(), len);
  return out.good();
}

static bool readString(std::ifstream& in, std::string& s) {
  uint32_t len = 0;
  in.read(reinterpret_cast<char*>(&len), sizeof(len));
  if (!in.good()) return false;
  s.resize(len);
  if (len > 0) in.read(s.data(), len);
  return in.good();
}

static UserCompletionData parseUserCompletionFile(
    const std::filesystem::path& path) {
  UserCompletionData data;
  std::ifstream in(path);
  if (!in.is_open()) return data;

  std::string line;
  while (std::getline(in, line)) {
    line = trimAscii(line);
    if (line.empty() || line.starts_with('#')) continue;

    auto parts = splitString(line, '|');
    for (auto& p : parts) p = trimAscii(std::move(p));
    if (parts.empty()) continue;

    std::string kind = toLowerAscii(parts[0]);
    if (kind == "cmd" && parts.size() >= 3) {
      data.commands.push_back({parts[1], parts[2]});
    } else if (kind == "opt" && parts.size() >= 4) {
      data.options.push_back({toLowerAscii(parts[1]), parts[2], parts[3]});
    }
  }
  return data;
}

static bool loadUserCompletionCache(const std::filesystem::path& cachePath,
                                    uint64_t expectedMtime,
                                    uint64_t expectedSize,
                                    UserCompletionData& out) {
  std::ifstream in(cachePath, std::ios::binary);
  if (!in.is_open()) return false;

  uint32_t magic = 0, version = 0;
  uint64_t sourceMtime = 0, sourceSize = 0;
  uint32_t cmdCount = 0, optCount = 0;
  in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  in.read(reinterpret_cast<char*>(&version), sizeof(version));
  in.read(reinterpret_cast<char*>(&sourceMtime), sizeof(sourceMtime));
  in.read(reinterpret_cast<char*>(&sourceSize), sizeof(sourceSize));
  in.read(reinterpret_cast<char*>(&cmdCount), sizeof(cmdCount));
  in.read(reinterpret_cast<char*>(&optCount), sizeof(optCount));
  if (!in.good()) return false;
  if (magic != kCacheMagic || version != kCacheVersion) return false;
  if (sourceMtime != expectedMtime || sourceSize != expectedSize) return false;

  out = {};
  out.commands.reserve(cmdCount);
  out.options.reserve(optCount);

  for (uint32_t i = 0; i < cmdCount; ++i) {
    std::string c, h;
    if (!readString(in, c) || !readString(in, h)) return false;
    out.commands.push_back({std::move(c), std::move(h)});
  }
  for (uint32_t i = 0; i < optCount; ++i) {
    std::string c, o, h;
    if (!readString(in, c) || !readString(in, o) || !readString(in, h))
      return false;
    out.options.push_back(
        {toLowerAscii(std::move(c)), std::move(o), std::move(h)});
  }

  return true;
}

static void saveUserCompletionCache(const std::filesystem::path& cachePath,
                                    uint64_t sourceMtime, uint64_t sourceSize,
                                    const UserCompletionData& data) {
  std::error_code ec;
  std::filesystem::create_directories(cachePath.parent_path(), ec);
  if (ec) return;

  auto tmpPath = cachePath;
  tmpPath += ".tmp";

  std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return;

  uint32_t magic = kCacheMagic;
  uint32_t version = kCacheVersion;
  uint32_t cmdCount = static_cast<uint32_t>(data.commands.size());
  uint32_t optCount = static_cast<uint32_t>(data.options.size());
  out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  out.write(reinterpret_cast<const char*>(&version), sizeof(version));
  out.write(reinterpret_cast<const char*>(&sourceMtime), sizeof(sourceMtime));
  out.write(reinterpret_cast<const char*>(&sourceSize), sizeof(sourceSize));
  out.write(reinterpret_cast<const char*>(&cmdCount), sizeof(cmdCount));
  out.write(reinterpret_cast<const char*>(&optCount), sizeof(optCount));

  for (const auto& c : data.commands) {
    if (!writeString(out, c.text) || !writeString(out, c.hint)) return;
  }
  for (const auto& o : data.options) {
    if (!writeString(out, o.command_lower) || !writeString(out, o.option) ||
        !writeString(out, o.hint))
      return;
  }
  out.flush();
  if (!out.good()) return;
  out.close();

  std::filesystem::rename(tmpPath, cachePath, ec);
  if (ec) std::filesystem::remove(tmpPath, ec);
}

static UserCompletionData loadUserCompletionDataFromCacheOrText() {
  UserCompletionData data;
  auto sourcePath = getUserCompletionFilePath();
  if (sourcePath.empty()) return data;

  std::error_code ec;
  if (!std::filesystem::exists(sourcePath, ec) || ec) return data;

  auto sourceSize = std::filesystem::file_size(sourcePath, ec);
  if (ec) return data;
  auto sourceMtimeRaw = std::filesystem::last_write_time(sourcePath, ec);
  if (ec) return data;
  uint64_t sourceMtime =
      static_cast<uint64_t>(sourceMtimeRaw.time_since_epoch().count());
  uint64_t sourceSize64 = static_cast<uint64_t>(sourceSize);

  auto cachePath = getUserCompletionCachePath(sourcePath);
  if (loadUserCompletionCache(cachePath, sourceMtime, sourceSize64, data)) {
    return data;
  }

  data = parseUserCompletionFile(sourcePath);
  saveUserCompletionCache(cachePath, sourceMtime, sourceSize64, data);
  return data;
}

static const UserCompletionData& getUserCompletionData() {
  static const UserCompletionData data =
      loadUserCompletionDataFromCacheOrText();
  return data;
}

}  // namespace

export std::vector<NativeCompletionItem> queryNativeCommandCompletionsForShell(
    std::string_view prefix, bool includePowerShell) noexcept {
  std::vector<NativeCompletionItem> items;
  std::unordered_set<std::string> seenLower;
  seenLower.reserve(128);

  auto tryAdd = [&](std::string_view cmd, std::string_view hint) {
    if (!startsWithCaseInsensitive(cmd, prefix)) return;
    std::string key = toLowerAscii(std::string(cmd));
    if (!seenLower.insert(key).second) return;
    items.push_back({std::string(cmd), std::string(hint)});
  };

  for (const auto& [cmd, hint] : kBuiltinCommands) tryAdd(cmd, hint);
  if (includePowerShell) {
    for (const auto& [cmd, hint] : kPowerShellCommands) tryAdd(cmd, hint);
  }
  for (const auto& c : getUserCompletionData().commands) tryAdd(c.text, c.hint);

  std::ranges::sort(items, {}, &NativeCompletionItem::text);
  return items;
}

export std::vector<NativeCompletionItem> queryNativeOptionCompletionsForShell(
    std::string_view command, std::string_view prefix,
    bool includePowerShell) noexcept {
  std::vector<NativeCompletionItem> items;
  std::unordered_set<std::string> seenLower;
  seenLower.reserve(64);

  std::string commandLower = toLowerAscii(std::string(command));
  auto tryAdd = [&](std::string_view opt, std::string_view hint) {
    if (!startsWithCaseInsensitive(opt, prefix)) return;
    std::string key = toLowerAscii(std::string(opt));
    if (!seenLower.insert(key).second) return;
    items.push_back({std::string(opt), std::string(hint)});
  };

  for (const auto& def : kBuiltinOptions) {
    if (equalsCaseInsensitive(def.command, commandLower))
      tryAdd(def.option, def.hint);
  }
  if (includePowerShell) {
    for (const auto& def : kPowerShellOptions) {
      if (equalsCaseInsensitive(def.command, commandLower))
        tryAdd(def.option, def.hint);
    }
  }
  for (const auto& o : getUserCompletionData().options) {
    if (o.command_lower == commandLower) tryAdd(o.option, o.hint);
  }

  std::ranges::sort(items, {}, &NativeCompletionItem::text);
  return items;
}

export std::vector<NativeCompletionItem> queryNativeCommandCompletions(
    std::string_view prefix) noexcept {
  return queryNativeCommandCompletionsForShell(prefix, false);
}

export std::vector<NativeCompletionItem> queryNativeOptionCompletions(
    std::string_view command, std::string_view prefix) noexcept {
  return queryNativeOptionCompletionsForShell(command, prefix, false);
}
