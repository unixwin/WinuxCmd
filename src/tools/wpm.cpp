/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: wpm.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Winux Package Manager - Manage external packages and command
/// aliases
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

import std;
import utils;

namespace fs = std::filesystem;

namespace wpm {

// ======================================================
// Constants
// ======================================================

constexpr const char* WPM_VERSION = "0.1.0";
constexpr const char* DEFAULT_CONFIG_FILE = "config.json";
constexpr const char* DEFAULT_ALIASES_FILE = "aliases.json";
constexpr const char* DEFAULT_PACKAGES_DIR = "packages";
constexpr const char* DEFAULT_BIN_DIR = "bin";
constexpr const char* DEFAULT_CACHE_DIR = "cache";
constexpr const char* DEFAULT_SOURCE_URL =
    "https://raw.githubusercontent.com/caomengxuan666/wpm-packages/main";

// ======================================================
// Design Patterns: Strategy Pattern for Download
// ======================================================

struct DownloadResult {
  bool success;
  std::vector<std::byte> data;
  std::string error;
  size_t total_size;
};

class IDownloadStrategy {
 public:
  virtual ~IDownloadStrategy() = default;
  using ProgressCallback = std::function<void(size_t current, size_t total)>;
  virtual DownloadResult download(const std::string& url,
                                  ProgressCallback callback = nullptr) = 0;
};

class WinHTTPDownloadStrategy : public IDownloadStrategy {
 public:
  DownloadResult download(const std::string& url,
                          ProgressCallback callback = nullptr) override {
    DownloadResult result;
    result.total_size = 0;

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    try {
      // Parse URL
      std::wstring wurl(url.begin(), url.end());
      URL_COMPONENTS urlComp = {0};
      urlComp.dwStructSize = sizeof(URL_COMPONENTS);
      urlComp.dwSchemeLength = (DWORD)-1;
      urlComp.dwHostNameLength = (DWORD)-1;
      urlComp.dwUrlPathLength = (DWORD)-1;
      urlComp.dwExtraInfoLength = (DWORD)-1;

      if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        result.error = "Failed to parse URL: " + url;
        return result;
      }

      // Get host name and path
      std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
      std::wstring urlPath;
      if (urlComp.lpszUrlPath) {
        urlPath = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
      }
      if (urlComp.lpszExtraInfo) {
        urlPath +=
            std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
      }

      // Open session
      hSession = WinHttpOpen(L"WinuxPM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
      if (!hSession) {
        result.error = "Failed to initialize WinHTTP session";
        return result;
      }

      // Connect to server
      hConnect = WinHttpConnect(hSession, hostName.c_str(),
                                INTERNET_DEFAULT_HTTPS_PORT, 0);
      if (!hConnect) {
        result.error = "Failed to connect to server";
        WinHttpCloseHandle(hSession);
        return result;
      }

      // Create request
      hRequest = WinHttpOpenRequest(
          hConnect, L"GET", urlPath.c_str(), NULL, WINHTTP_NO_REFERER,
          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
      if (!hRequest) {
        result.error = "Failed to create request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
      }

      // Send request
      if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        result.error = "Failed to send request";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
      }

      // Receive response
      if (!WinHttpReceiveResponse(hRequest, NULL)) {
        result.error = "Failed to receive response";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
      }

      // Get content length
      DWORD contentLength = 0;
      DWORD bufferLength = sizeof(contentLength);
      WinHttpQueryHeaders(
          hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &bufferLength,
          WINHTTP_NO_HEADER_INDEX);
      result.total_size = contentLength;

      // Read data with progress callback
      const int BUFFER_SIZE = 8192;
      std::byte buffer[BUFFER_SIZE];
      DWORD bytesRead = 0;
      size_t totalRead = 0;

      while (WinHttpReadData(hRequest, buffer, BUFFER_SIZE, &bytesRead) &&
             bytesRead > 0) {
        result.data.insert(result.data.end(), buffer, buffer + bytesRead);
        totalRead += bytesRead;

        // Call progress callback if provided
        if (callback && result.total_size > 0) {
          callback(totalRead, result.total_size);
        }
      }

      result.success = true;

    } catch (const std::exception& e) {
      result.error = std::string("Download error: ") + e.what();
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    return result;
  }
};

class Downloader {
 private:
  std::unique_ptr<IDownloadStrategy> strategy_;

 public:
  Downloader() : strategy_(std::make_unique<WinHTTPDownloadStrategy>()) {}

  DownloadResult download(
      const std::string& url,
      IDownloadStrategy::ProgressCallback callback = nullptr) {
    return strategy_->download(url, callback);
  }

  bool download_to_file(
      const std::string& url, const fs::path& filepath,
      IDownloadStrategy::ProgressCallback progress_callback = nullptr) {
    DownloadResult result;

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    try {
      // Parse URL
      std::wstring wurl(url.begin(), url.end());
      URL_COMPONENTS urlComp = {0};
      urlComp.dwStructSize = sizeof(URL_COMPONENTS);
      urlComp.dwSchemeLength = (DWORD)-1;
      urlComp.dwHostNameLength = (DWORD)-1;
      urlComp.dwUrlPathLength = (DWORD)-1;
      urlComp.dwExtraInfoLength = (DWORD)-1;

      if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        safePrintLn("Failed to parse URL: " + url);
        return false;
      }

      // Get host name and path
      std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
      std::wstring urlPath;
      if (urlComp.lpszUrlPath) {
        urlPath = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
      }
      if (urlComp.lpszExtraInfo) {
        urlPath +=
            std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
      }

      // Open session
      hSession = WinHttpOpen(L"WinuxPM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
      if (!hSession) {
        safePrintLn("Failed to initialize WinHTTP session");
        return false;
      }

      // Connect to server
      hConnect = WinHttpConnect(hSession, hostName.c_str(),
                                INTERNET_DEFAULT_HTTPS_PORT, 0);
      if (!hConnect) {
        safePrintLn("Failed to connect to server");
        WinHttpCloseHandle(hSession);
        return false;
      }

      // Create request
      hRequest = WinHttpOpenRequest(
          hConnect, L"GET", urlPath.c_str(), NULL, WINHTTP_NO_REFERER,
          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
      if (!hRequest) {
        safePrintLn("Failed to create request");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
      }

      // Send request
      if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        safePrintLn("Failed to send request");
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
      }

      // Receive response
      if (!WinHttpReceiveResponse(hRequest, NULL)) {
        safePrintLn("Failed to receive response");
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
      }

      // Get content length
      size_t totalSize = 0;
      DWORD contentLength = 0;
      DWORD bufferLength = sizeof(contentLength);
      if (WinHttpQueryHeaders(
              hRequest,
              WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
              WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &bufferLength,
              WINHTTP_NO_HEADER_INDEX)) {
        totalSize = contentLength;
      }

      // Create output file
      fs::create_directories(filepath.parent_path());
      std::ofstream out(filepath, std::ios::binary);
      if (!out) {
        safePrintLn("Failed to open file for writing: " + filepath.string());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
      }

      // Read and write data with progress callback
      const int BUFFER_SIZE = 8192;
      std::byte buffer[BUFFER_SIZE];
      DWORD bytesRead = 0;
      size_t totalRead = 0;

      while (WinHttpReadData(hRequest, buffer, BUFFER_SIZE, &bytesRead) &&
             bytesRead > 0) {
        out.write(reinterpret_cast<const char*>(buffer), bytesRead);
        totalRead += bytesRead;

        // Call progress callback if provided
        if (progress_callback && totalSize > 0) {
          progress_callback(totalRead, totalSize);
        }
      }

      out.close();

      // Cleanup
      if (hRequest) WinHttpCloseHandle(hRequest);
      if (hConnect) WinHttpCloseHandle(hConnect);
      if (hSession) WinHttpCloseHandle(hSession);

      return true;

    } catch (const std::exception& e) {
      safePrintLn("Download error: " + std::string(e.what()));
      if (hRequest) WinHttpCloseHandle(hRequest);
      if (hConnect) WinHttpCloseHandle(hConnect);
      if (hSession) WinHttpCloseHandle(hSession);
      return false;
    }
  }
};

// ======================================================
// Package Models
// ======================================================

struct PackageFile {
  std::string url;
  std::string filename;
  std::string sha256;
  size_t size;

  nlohmann::json to_json() const {
    return nlohmann::json{{"url", url},
                          {"filename", filename},
                          {"sha256", sha256},
                          {"size", size}};
  }

  static PackageFile from_json(const nlohmann::json& j) {
    PackageFile pkg;
    if (j.contains("url")) pkg.url = j["url"].get<std::string>();
    if (j.contains("filename")) pkg.filename = j["filename"].get<std::string>();
    if (j.contains("sha256")) pkg.sha256 = j["sha256"].get<std::string>();
    if (j.contains("size")) pkg.size = j["size"].get<size_t>();
    return pkg;
  }
};

struct Package {
  std::string name;
  std::string version;
  std::string description;
  std::string homepage;
  std::string type;
  std::vector<std::string> arch;
  std::vector<PackageFile> files;
  std::string rename_to;
  bool replace_builtin;
  std::vector<std::string> dependencies;

  nlohmann::json to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["version"] = version;
    j["description"] = description;
    j["homepage"] = homepage;
    j["type"] = type;
    j["arch"] = arch;
    nlohmann::json files_json = nlohmann::json::array();
    for (const auto& f : files) {
      files_json.push_back(f.to_json());
    }
    j["files"] = files_json;
    j["rename_to"] = rename_to;
    j["replace_builtin"] = replace_builtin;
    j["dependencies"] = dependencies;
    return j;
  }

  static Package from_json(const nlohmann::json& j) {
    Package pkg;
    if (j.contains("name")) pkg.name = j["name"].get<std::string>();
    if (j.contains("version")) pkg.version = j["version"].get<std::string>();
    if (j.contains("description"))
      pkg.description = j["description"].get<std::string>();
    if (j.contains("homepage")) pkg.homepage = j["homepage"].get<std::string>();
    if (j.contains("type")) pkg.type = j["type"].get<std::string>();
    if (j.contains("arch"))
      pkg.arch = j["arch"].get<std::vector<std::string>>();
    if (j.contains("files")) {
      pkg.files.clear();
      for (const auto& f : j["files"]) {
        pkg.files.push_back(PackageFile::from_json(f));
      }
    }
    if (j.contains("rename_to"))
      pkg.rename_to = j["rename_to"].get<std::string>();
    if (j.contains("replace_builtin"))
      pkg.replace_builtin = j["replace_builtin"].get<bool>();
    if (j.contains("dependencies"))
      pkg.dependencies = j["dependencies"].get<std::vector<std::string>>();
    return pkg;
  }
};

// ======================================================
// Configuration Management
// ======================================================

class ConfigManager {
 private:
  fs::path config_file_;
  nlohmann::json config_;

 public:
  ConfigManager(const fs::path& config_file) : config_file_(config_file) {
    load();
  }

  bool load() {
    if (!fs::exists(config_file_)) {
      create_default();
      return true;
    }

    try {
      std::ifstream file(config_file_);
      config_ = nlohmann::json::parse(file);
      return true;
    } catch (const std::exception& e) {
      safePrintLn("Error loading config: " + std::string(e.what()));
      return false;
    }
  }

  bool save() {
    try {
      fs::create_directories(config_file_.parent_path());
      std::ofstream file(config_file_);
      file << config_.dump(2);
      return true;
    } catch (const std::exception& e) {
      safePrintLn("Error saving config: " + std::string(e.what()));
      return false;
    }
  }

  void create_default() {
    config_ = nlohmann::json{
        {"sources", std::vector<std::string>{DEFAULT_SOURCE_URL}},
        {"install_dir", "bin"},
        {"cache_dir", "cache"}};
    save();
  }

  std::vector<std::string> get_sources() const {
    return config_.value("sources",
                         std::vector<std::string>{DEFAULT_SOURCE_URL});
  }

  void add_source(const std::string& source) {
    auto sources = get_sources();
    if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
      sources.push_back(source);
      config_["sources"] = sources;
    }
  }

  void remove_source(const std::string& source) {
    auto sources = get_sources();
    sources.erase(std::remove(sources.begin(), sources.end(), source),
                  sources.end());
    config_["sources"] = sources;
  }
};

// ======================================================
// Alias Management
// ======================================================

class AliasManager {
 private:
  fs::path aliases_file_;
  nlohmann::json aliases_;

 public:
  AliasManager(const fs::path& aliases_file) : aliases_file_(aliases_file) {
    load();
  }

  bool load() {
    if (!fs::exists(aliases_file_)) {
      aliases_ = nlohmann::json::object();
      return true;
    }

    try {
      std::ifstream file(aliases_file_);
      aliases_ = nlohmann::json::parse(file);
      return true;
    } catch (const std::exception& e) {
      safePrintLn("Error loading aliases: " + std::string(e.what()));
      return false;
    }
  }

  bool save() {
    try {
      fs::create_directories(aliases_file_.parent_path());
      std::ofstream file(aliases_file_);
      file << aliases_.dump(2);
      return true;
    } catch (const std::exception& e) {
      safePrintLn("Error saving aliases: " + std::string(e.what()));
      return false;
    }
  }

  void set_alias(const std::string& alias, const std::string& target) {
    aliases_[alias] = target;
    save();
  }

  std::optional<std::string> get_alias(const std::string& alias) const {
    if (aliases_.contains(alias)) {
      return aliases_[alias].get<std::string>();
    }
    return std::nullopt;
  }

  void remove_alias(const std::string& alias) {
    if (aliases_.contains(alias)) {
      aliases_.erase(alias);
      save();
    }
  }

  std::vector<std::string> list_aliases() const {
    std::vector<std::string> result;
    for (auto it = aliases_.begin(); it != aliases_.end(); ++it) {
      result.push_back(it.key() + " -> " + it.value().get<std::string>());
    }
    return result;
  }
};

// ======================================================
// Package Repository
// ======================================================

class PackageRepository {
 private:
  std::map<std::string, Package> packages_;
  Downloader downloader_;

 public:
  void load_from_file(const fs::path& filepath) {
    try {
      std::ifstream file(filepath);
      if (!file.is_open()) {
        safePrintLn("Failed to open file: " + filepath.string());
        return;
      }

      std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
      auto json_data = nlohmann::json::parse(content);
      Package pkg = Package::from_json(json_data);
      packages_[pkg.name] = pkg;
    } catch (const std::exception& e) {
      safePrintLn("Error loading package from " + filepath.string() + ": " +
                  e.what());
    }
  }

  void load_from_local(const fs::path& directory) {
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
      return;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
      if (entry.path().extension() == ".json") {
        load_from_file(entry.path());
      }
    }
  }

  void load_from_remote(const std::string& base_url) {
    std::string index_url = base_url + "/index.json";
    auto result = downloader_.download(index_url);

    if (!result.success) {
      safePrintLn("Failed to load index from " + base_url + ": " +
                  result.error);
      return;
    }

    try {
      auto json_data = nlohmann::json::parse(
          std::string(reinterpret_cast<const char*>(result.data.data()),
                      result.data.size()));

      if (json_data.contains("packages")) {
        for (const auto& pkg_name : json_data["packages"]) {
          std::string pkg_url =
              base_url + "/" + pkg_name.get<std::string>() + ".json";
          auto pkg_result = downloader_.download(pkg_url);

          if (pkg_result.success) {
            try {
              auto pkg_json = nlohmann::json::parse(std::string(
                  reinterpret_cast<const char*>(pkg_result.data.data()),
                  pkg_result.data.size()));
              Package pkg = Package::from_json(pkg_json);
              packages_[pkg.name] = pkg;
            } catch (const std::exception& e) {
              safePrintLn("Error parsing package " +
                          pkg_name.get<std::string>() + ": " + e.what());
            }
          }
        }
      }
    } catch (const std::exception& e) {
      safePrintLn("Error parsing index: " + std::string(e.what()));
    }
  }

  void add_package(const Package& pkg) { packages_[pkg.name] = pkg; }

  std::optional<Package> get_package(const std::string& name) const {
    auto it = packages_.find(name);
    if (it != packages_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::vector<Package> search_packages(const std::string& keyword) const {
    std::vector<Package> results;
    std::string lower_keyword = keyword;
    std::transform(lower_keyword.begin(), lower_keyword.end(),
                   lower_keyword.begin(), ::tolower);

    for (const auto& pair : packages_) {
      const std::string& name = pair.first;
      const Package& pkg = pair.second;

      std::string lower_name = name;
      std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                     ::tolower);

      std::string lower_desc = pkg.description;
      std::transform(lower_desc.begin(), lower_desc.end(), lower_desc.begin(),
                     ::tolower);

      if (lower_name.find(lower_keyword) != std::string::npos ||
          lower_desc.find(lower_keyword) != std::string::npos) {
        results.push_back(pkg);
      }
    }

    return results;
  }

  std::vector<Package> list_all() const {
    std::vector<Package> result;
    for (const auto& pair : packages_) {
      result.push_back(pair.second);
    }
    return result;
  }

  size_t size() const { return packages_.size(); }
};

// ======================================================
// Package Manager (Main Logic)
// ======================================================

class PackageManager {
 private:
  fs::path wpm_home_;
  fs::path bin_dir_;
  fs::path cache_dir_;
  ConfigManager config_;
  AliasManager alias_manager_;
  PackageRepository repository_;
  Downloader downloader_;

 public:
  PackageManager(const std::string& wpm_home = "")
      : wpm_home_(wpm_home.empty() ? get_wpm_home() : wpm_home),
        bin_dir_(wpm_home_ / DEFAULT_BIN_DIR),
        cache_dir_(wpm_home_ / DEFAULT_CACHE_DIR),
        config_(wpm_home_ / DEFAULT_CONFIG_FILE),
        alias_manager_(wpm_home_ / DEFAULT_ALIASES_FILE) {
    initialize_directories();
    load_packages();
  }

  bool install_package(const std::string& package_name) {
    auto pkg_opt = repository_.get_package(package_name);
    if (!pkg_opt) {
      safePrintLn("Package '" + package_name + "' not found");
      return false;
    }

    Package pkg = *pkg_opt;
    safePrintLn("Installing " + pkg.name + " v" + pkg.version + "...");

    // Download and install files
    for (const auto& file : pkg.files) {
      safePrintLn("Downloading " + file.filename + "...");

      cppbar::ProgressBar bar(100, "Progress");
      bar.set_style(cppbar::style::PresetStyles::modern());
      bar.set_foreground_color(cppbar::color::Color::from_256(33));  // Blue

      // Download with real progress callback
      bool downloadSuccess = downloader_.download_to_file(
          file.url, cache_dir_ / file.filename,
          [&bar](size_t current, size_t total) {
            if (total > 0) {
              int progress = static_cast<int>((current * 100) / total);
              bar.update(progress);
            }
          });

      if (!downloadSuccess) {
        safePrintLn("Failed to download " + file.filename);
        return false;
      }

      bar.finish();

      // Copy to bin directory
      fs::path dest_path = bin_dir_ / file.filename;
      if (pkg.replace_builtin && !pkg.rename_to.empty()) {
        dest_path = bin_dir_ / pkg.rename_to;
      }

      try {
        fs::copy_file(cache_dir_ / file.filename, dest_path,
                      fs::copy_options::overwrite_existing);
        safePrintLn("Installed: " + dest_path.string());
      } catch (const std::exception& e) {
        safePrintLn("Error copying file: " + std::string(e.what()));
        return false;
      }
    }

    // Add to aliases if needed
    if (pkg.replace_builtin && !pkg.rename_to.empty()) {
      alias_manager_.set_alias(pkg.name, pkg.rename_to);
    }

    safePrintLn("Package '" + package_name + "' installed successfully!");
    return true;
  }

  bool uninstall_package(const std::string& package_name) {
    auto pkg_opt = repository_.get_package(package_name);
    if (!pkg_opt) {
      safePrintLn("Package '" + package_name + "' not found");
      return false;
    }

    Package pkg = *pkg_opt;
    safePrintLn("Uninstalling " + package_name + "...");

    // Remove files
    for (const auto& file : pkg.files) {
      fs::path file_path = bin_dir_ / file.filename;
      if (pkg.replace_builtin && !pkg.rename_to.empty()) {
        file_path = bin_dir_ / pkg.rename_to;
      }

      if (fs::exists(file_path)) {
        try {
          fs::remove(file_path);
          safePrintLn("Removed: " + file_path.string());
        } catch (const std::exception& e) {
          safePrintLn("Error removing file: " + std::string(e.what()));
        }
      }
    }

    // Remove alias
    if (pkg.replace_builtin && !pkg.rename_to.empty()) {
      alias_manager_.remove_alias(pkg.name);
    }

    safePrintLn("Package '" + package_name + "' uninstalled successfully!");
    return true;
  }

  void list_packages(bool installed_only = false) {
    if (installed_only) {
      safePrintLn("Installed packages:");
      if (fs::exists(bin_dir_)) {
        for (const auto& entry : fs::directory_iterator(bin_dir_)) {
          safePrintLn("  " + entry.path().filename().string());
        }
      }
    } else {
      safePrintLn("Available packages:");
      auto packages = repository_.list_all();
      for (const auto& pkg : packages) {
        safePrintLn("  " + pkg.name + " v" + pkg.version + " - " +
                    pkg.description);
      }
    }
  }

  void search_packages(const std::string& keyword) {
    safePrintLn("Search results for '" + keyword + "':");
    auto results = repository_.search_packages(keyword);

    if (results.empty()) {
      safePrintLn("No packages found.");
    } else {
      for (const auto& pkg : results) {
        safePrintLn("  " + pkg.name + " v" + pkg.version + " - " +
                    pkg.description);
      }
    }
  }

  void show_package_info(const std::string& package_name) {
    auto pkg_opt = repository_.get_package(package_name);
    if (!pkg_opt) {
      safePrintLn("Package '" + package_name + "' not found");
      return;
    }

    Package pkg = *pkg_opt;

    safePrintLn("Name: " + pkg.name);
    safePrintLn("Version: " + pkg.version);
    safePrintLn("Description: " + pkg.description);
    safePrintLn("Homepage: " + pkg.homepage);
    safePrintLn("Type: " + pkg.type);

    safePrint("Architectures: ");
    for (size_t i = 0; i < pkg.arch.size(); ++i) {
      safePrint(pkg.arch[i]);
      if (i < pkg.arch.size() - 1) safePrint(", ");
    }
    safePrintLn("");
  }

  void list_sources() {
    auto sources = config_.get_sources();
    safePrintLn("Package sources:");
    for (size_t i = 0; i < sources.size(); ++i) {
      safePrintLn("  " + std::to_string(i + 1) + ". " + sources[i]);
    }
  }

  void add_source(const std::string& source) {
    config_.add_source(source);
    safePrintLn("Source added: " + source);
    load_packages();
  }

  void remove_source(const std::string& source) {
    config_.remove_source(source);
    safePrintLn("Source removed: " + source);
    load_packages();
  }

  void list_aliases() {
    auto aliases = alias_manager_.list_aliases();
    safePrintLn("Command aliases:");
    for (const auto& alias : aliases) {
      safePrintLn("  " + alias);
    }
  }

  void set_alias(const std::string& alias, const std::string& target) {
    alias_manager_.set_alias(alias, target);
    safePrintLn("Alias set: " + alias + " -> " + target);
  }

  void remove_alias(const std::string& alias) {
    alias_manager_.remove_alias(alias);
    safePrintLn("Alias removed: " + alias);
  }

  void reload_packages() {
    load_packages();
    safePrintLn("Package index reloaded");
  }

 private:
  void initialize_directories() {
    fs::create_directories(wpm_home_);
    fs::create_directories(bin_dir_);
    fs::create_directories(cache_dir_);
  }

  void load_packages() {
    repository_ = PackageRepository();  // Clear and reload

    auto sources = config_.get_sources();
    for (const auto& source : sources) {
      if (source.find("http") == 0) {
        repository_.load_from_remote(source);
      } else {
        repository_.load_from_local(source);
      }
    }
  }

  static std::string get_wpm_home() {
    const char* wpm_home = std::getenv("WPM_HOME");
    if (wpm_home) {
      return expand_home_path(wpm_home);
    }

    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");

    if (home) {
      return std::string(home) + "/.winuxcmd";
    }

    return "./.winuxcmd";
  }

  static std::string expand_home_path(const std::string& path) {
    if (!path.empty() && path[0] == '~') {
      const char* home = std::getenv("HOME");
      if (!home) home = std::getenv("USERPROFILE");
      if (home) {
        return std::string(home) + path.substr(1);
      }
    }
    return path;
  }
};

// ======================================================
// Command Pattern
// ======================================================

class ICommand {
 public:
  virtual ~ICommand() = default;
  virtual int execute(PackageManager& manager,
                      const std::vector<std::string>& args) = 0;
  virtual std::string description() const = 0;
};

class InstallCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    if (args.empty()) {
      safePrintLn("Usage: wpm install <package>");
      return 1;
    }
    return manager.install_package(args[0]) ? 0 : 1;
  }

  std::string description() const override { return "Install a package"; }
};

class UninstallCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    if (args.empty()) {
      safePrintLn("Usage: wpm uninstall <package>");
      return 1;
    }
    return manager.uninstall_package(args[0]) ? 0 : 1;
  }

  std::string description() const override { return "Uninstall a package"; }
};

class ListCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    bool installed_only = false;
    if (!args.empty() && args[0] == "--installed") {
      installed_only = true;
    }
    manager.list_packages(installed_only);
    return 0;
  }

  std::string description() const override { return "List packages"; }
};

class SearchCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    if (args.empty()) {
      safePrintLn("Usage: wpm search <keyword>");
      return 1;
    }
    manager.search_packages(args[0]);
    return 0;
  }

  std::string description() const override { return "Search packages"; }
};

class InfoCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    if (args.empty()) {
      safePrintLn("Usage: wpm info <package>");
      return 1;
    }
    manager.show_package_info(args[0]);
    return 0;
  }

  std::string description() const override {
    return "Show package information";
  }
};

class SourceCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    if (args.empty()) {
      manager.list_sources();
      return 0;
    }

    if (args.size() >= 2) {
      if (args[0] == "add") {
        manager.add_source(args[1]);
        return 0;
      } else if (args[0] == "remove") {
        manager.remove_source(args[1]);
        return 0;
      }
    }

    safePrintLn("Usage: wpm source [add|remove] <url>");
    return 1;
  }

  std::string description() const override { return "Manage package sources"; }
};

class AliasCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    if (args.empty()) {
      manager.list_aliases();
      return 0;
    }

    if (args.size() >= 2) {
      if (args[0] == "set") {
        manager.set_alias(args[1], args[2]);
        return 0;
      } else if (args[0] == "remove") {
        manager.remove_alias(args[1]);
        return 0;
      }
    }

    safePrintLn("Usage: wpm alias [set|remove] <alias> [target]");
    return 1;
  }

  std::string description() const override { return "Manage command aliases"; }
};

class ReloadCommand : public ICommand {
 public:
  int execute(PackageManager& manager,
              const std::vector<std::string>& args) override {
    manager.reload_packages();
    return 0;
  }

  std::string description() const override { return "Reload package index"; }
};

// ======================================================
// CLI Application
// ======================================================

class CLIApplication {
 private:
  PackageManager package_manager_;
  std::map<std::string, std::unique_ptr<ICommand>> commands_;

 public:
  CLIApplication() {
    register_command("install", std::make_unique<InstallCommand>());
    register_command("uninstall", std::make_unique<UninstallCommand>());
    register_command("list", std::make_unique<ListCommand>());
    register_command("search", std::make_unique<SearchCommand>());
    register_command("info", std::make_unique<InfoCommand>());
    register_command("source", std::make_unique<SourceCommand>());
    register_command("alias", std::make_unique<AliasCommand>());
    register_command("reload", std::make_unique<ReloadCommand>());
  }

  void register_command(const std::string& name,
                        std::unique_ptr<ICommand> command) {
    commands_[name] = std::move(command);
  }

  void print_usage() {
    safePrintLn("Winux Package Manager v" + std::string(WPM_VERSION));
    safePrintLn("");
    safePrintLn("Usage: wpm <command> [options]");
    safePrintLn("");
    safePrintLn("Commands:");
    for (const auto& [name, cmd] : commands_) {
      safePrintLn("  " + name + "     " + cmd->description());
    }
    safePrintLn("  help     Show this help message");
    safePrintLn("  version  Show version information");
  }

  int run(const std::vector<std::string>& args) {
    if (args.empty()) {
      print_usage();
      return 1;
    }

    std::string command = args[0];

    if (command == "help" || command == "-h" || command == "--help") {
      print_usage();
      return 0;
    }

    if (command == "version" || command == "-v" || command == "--version") {
      safePrintLn("Winux Package Manager v" + std::string(WPM_VERSION));
      return 0;
    }

    auto it = commands_.find(command);
    if (it == commands_.end()) {
      safePrintLn("Unknown command: " + command);
      safePrintLn("Run 'wpm help' for usage information");
      return 1;
    }

    std::vector<std::string> cmd_args(args.begin() + 1, args.end());
    return it->second->execute(package_manager_, cmd_args);
  }
};

// ======================================================
// Main Function
// ======================================================

int wpm_main(const std::vector<std::string>& args) {
  CLIApplication app;
  return app.run(args);
}

}  // namespace wpm

int main(int argc, char* argv[]) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return wpm::wpm_main(args);
}
