# WPM — WinuxCmd Package Manager User Guide

> **Version:** 0.3.0 | **License:** MIT | **Copyright:** 2026 WinuxCmd Project

---

## Table of Contents

- [1. What is WPM?](#1-what-is-wpm)
- [2. Installation](#2-installation)
- [3. Quick Start](#3-quick-start)
- [4. Subcommands Reference](#4-subcommands-reference)
- [5. Global Options](#5-global-options)
- [6. Package Sources (wpm-source)](#6-package-sources-wpm-source)
- [7. Adding Custom Sources](#7-adding-custom-sources)
- [8. Known Packages](#8-known-packages)
- [9. Package Artifact Format](#9-package-artifact-format)
- [10. WPM vs Chocolatey / Scoop](#10-wpm-vs-chocolatey--scoop)
- [11. Troubleshooting](#11-troubleshooting)
- [12. Developer Guide — Adding Packages](#12-developer-guide--adding-packages)

---

## 1. What is WPM?

**WPM** (WinuxCmd Package Manager) is the built-in package and link manager for [WinuxCmd](https://github.com/unixwin/winuxcmd). It is implemented entirely in C++ and ships as part of the `winuxcmd.exe` binary — no external runtime (Node.js, Python, .NET) is required.

WPM's responsibilities:

| Capability | Description |
|---|---|
| **Package index** | Maintains a local JSON index of available packages, fetched from remote sources. |
| **Download & install** | Downloads artifacts via WinHTTP (with automatic proxy detection), verifies SHA-256 checksums, extracts archives, and places files. |
| **Shim management** | Creates hardlinks or symbolic links of `winuxcmd.exe` in `usr/bin` so every installed command is available on `%PATH%`. |
| **Self-update** | Can update the `winuxcmd.exe` binary itself from the package index. |
| **Cache & cleanup** | Manages a local download cache and staging area; cleans transient state on demand. |

WPM uses the [wpm-source](https://github.com/unixwin/wpm-source) repository as its canonical package index.

---

## 2. Installation

WPM is **already included** in every WinuxCmd installation. If you have `winuxcmd.exe` on your system, you have WPM:

```bash
wpm --help
wpm --version
```

### Install Layout

WPM expects the following directory structure under the WinuxCmd root:

```
<root>/
├── bin/            # Legacy command links
├── usr/
│   ├── bin/        # Canonical command links (hardlinks / symlinks)
│   └── local/
│       └── bin/    # Additional user links
├── etc/            # Configuration
├── var/            # Variable state
├── tmp/            # Temporary files
├── dev/            # Device nodes
├── opt/            # Shim-layout package payloads
└── .wpm/           # WPM state directory
    ├── config.json # User configuration
    ├── indexes/    # Local package index
    ├── cache/      # Downloaded artifacts
    ├── staging/    # Extraction staging area
    └── backup/     # Pre-update backups
```

When you run any `wpm install` command, the layout is automatically created if it does not exist.

---

## 3. Quick Start

```bash
# Update the package index from remote sources
wpm index update

# Search for a package
wpm search curl

# Install a package
wpm install curl

# Install multiple packages at once
wpm install curl wget openssh

# List all installed packages
wpm installed

# Check for available updates
wpm outdated

# Uninstall a package
wpm uninstall curl

# Clean the download cache
wpm clean
```

---

## 4. Subcommands Reference

### 4.1 `wpm install <package>...`

Install one or more packages. Downloads the artifact, verifies the SHA-256 checksum, extracts archives, and places files according to the artifact's `files` mapping.

```bash
wpm install goawk
wpm install curl wget bsdtar
wpm install neovim --force        # Overwrite existing files
wpm install gzip --dry-run        # Preview changes without writing
```

**Behavior:**

- If the package is already installed and all destination files exist, WPM reports `"already installed"` and skips unless `--force` is used.
- For `layout=shim` packages, the actual payload goes to `<root>/opt/<pkg>/` and command shims (hardlinks of `winuxcmd.exe`) are created in `usr/bin/`.
- For `layout=flat` (default) packages, files are placed directly into `usr/bin/`.
- Downloads use a SHA-256 verified cache. Repeated installs skip the download if the cached artifact is valid.

### 4.2 `wpm uninstall | remove | erase <package>...`

Remove one or more installed packages. Removes the files placed by the package's artifact mapping.

```bash
wpm uninstall curl
wpm remove wget bsdtar
wpm erase gzip --dry-run
```

**Safety rules:**

- The canonical `winuxcmd.exe` (`usr/bin/winuxcmd.exe`) is **never** deleted.
- The currently running executable is **never** deleted.
- The core `winuxcmd` package cannot be uninstalled this way — use `wpm update winuxcmd` instead.

### 4.3 `wpm update | upgrade winuxcmd`

Update the WinuxCmd binary itself from the local package index. A backup of the current `winuxcmd.exe` is saved to `.wpm/backup/` before replacement.

```bash
wpm update winuxcmd
```

### 4.4 `wpm list [query]`

List all packages in the local index. Optionally filter by a search query (matches name, description, category, license, and commands).

```bash
wpm list               # Show installable (ready) packages
wpm list --all         # Include index-only placeholders
wpm list curl          # Filter by name/description
wpm list --category editors
```

### 4.5 `wpm search <query>`

Search packages by name, description, category, license, or command name. Equivalent to `wpm list <query>`.

```bash
wpm search neovim
wpm search "text editor"
wpm search gnu
```

### 4.6 `wpm info <package>`

Display detailed metadata for a specific package, including version, kind, category, license, commands, description, artifact type, download URLs, file count, and SHA-256 presence.

```bash
wpm info curl
wpm info neovim --json
```

### 4.7 `wpm installed`

List packages currently installed in this root. Installation state is derived by checking whether the artifact's destination files exist on disk.

```bash
wpm installed
wpm installed --json
```

### 4.8 `wpm export [--plain]`

Export installed package names as a plain text list (one package per line). Useful for creating reproducible profiles.

```bash
wpm export > my-profile.txt
wpm export --plain   # Same output, no winuxcmd itself
```

### 4.9 `wpm restore <file>`

Install packages from a plain text list file (one package name per line, `#` comments supported).

```bash
wpm restore my-profile.txt
```

### 4.10 `wpm outdated`

Check for available updates by comparing the local index against the remote index. Reports packages where the remote version differs from the locally cached version.

```bash
wpm outdated
wpm outdated --json
```

### 4.11 `wpm categories`

List all package categories with counts of total, ready, index-only, and installed packages.

```bash
wpm categories
wpm categories --json
```

### 4.12 `wpm index status | update`

Manage the local package index.

```bash
wpm index status      # Show local index metadata
wpm index update      # Fetch the latest index from remote sources
wpm update-index      # Alias for 'index update'
```

### 4.13 `wpm source list | use | add | test`

Manage package index sources.

```bash
wpm source list                           # List all configured sources
wpm source use official-jsdelivr          # Set preferred source
wpm source add my-repo https://...       # Add a custom source
wpm source test                           # Test connectivity (runs index update)
```

### 4.14 `wpm links list | rebuild | remove`

Manage WinuxCmd command hardlinks.

```bash
wpm links list       # List all command links in usr/bin
wpm links rebuild    # Recreate all hardlinks for registered commands
wpm links remove     # Remove all command links
```

### 4.15 `wpm clean [cache|staging|all]`

Remove transient state directories.

```bash
wpm clean            # Clean both cache and staging
wpm clean cache      # Clean only download cache
wpm clean staging    # Clean only extraction staging
wpm cache clean      # Alias
```

### 4.16 `wpm version`

Print the WPM version.

```bash
wpm version
```

---

## 5. Global Options

These options apply to all subcommands:

| Option | Short | Description |
|---|---|---|
| `--root <dir>` | `-r` | Manage a specific WinuxCmd root directory instead of the default. |
| `--source <name>` | `-s` | Use a specific index source for this operation. |
| `--all` | `-a` | Show index-only (placeholder) packages in `list` output. |
| `--force` | `-f` | Overwrite existing files when safe to do so. |
| `--dry-run` | `-n` | Show planned changes without writing any files. |
| `--verbose` | `-v` | Print detailed progress information. |
| `--category <name>` | | Filter `list` and `search` output by category. |
| `--json` | | Output machine-readable JSON instead of human text. |
| `--plain` | | Print only package names (for `export`). |
| `--help` | | Display help and exit. |
| `--version` | `-V` | Display version information and exit. |

---

## 6. Package Sources (wpm-source)

WPM fetches its package index from remote sources. The canonical source is the [unixwin/wpm-source](https://github.com/unixwin/wpm-source) GitHub repository.

### Built-in Sources

WPM ships with four built-in source definitions:

| Source Name | Region | Priority | URL |
|---|---|---|---|
| `official-github-raw` | global | 10 | `https://raw.githubusercontent.com/unixwin/wpm-source/main/index.json` |
| `official-jsdelivr` | global | 15 | `https://cdn.jsdelivr.net/gh/unixwin/wpm-source@main/index.json` |
| `official-github-release` | global | 20 | `https://github.com/unixwin/wpm-source/releases/latest/download/wpm-index.json` |
| `official-cn` | cn | 30 | *(reserved, not yet populated)* |

### How Source Selection Works

1. Sources are sorted by **priority** (lower = higher priority).
2. If `--source <name>` is specified, only that source is tried.
3. If `preferred_source` is set in config (via `wpm source use`), that source is tried first.
4. If `preferred_source` is `"auto"` (the default), sources are tried in priority order.
5. If a source fails, WPM falls back through remaining sources.
6. User-added sources are merged in after built-in sources.

### Proxy Support

WPM automatically detects proxy settings from:

1. Environment variables (checked in order): `WPM_HTTPS_PROXY`, `WPM_HTTP_PROXY`, `HTTPS_PROXY`, `ALL_PROXY`, `HTTP_PROXY`
2. Windows Internet Explorer proxy configuration (system settings)
3. PAC auto-detection (WPAD)
4. Loopback proxy fallback: if all else fails, WPM probes common local proxy ports (`7897`, `7890`, `7891`, `7892`, `7893`, `10808`, `10809`)

---

## 7. Adding Custom Sources

You can add your own package index sources:

```bash
# Add a custom source
wpm source add my-org https://my-org.example.com/wpm/index.json

# Set it as the preferred source
wpm source use my-org

# Verify connectivity
wpm source test
```

### Custom Source Format

A custom source is an object added to the `user_sources` array in `.wpm/config.json`:

```json
{
  "name": "my-org",
  "region": "custom",
  "priority": 5,
  "index_urls": [
    "https://my-org.example.com/wpm/index.json"
  ]
}
```

### Index JSON Format

The index JSON file must conform to this schema:

```json
{
  "schema": 1,
  "name": "my-source",
  "version": "2026.08.20",
  "updated": "2026-08-20",
  "packages": [
    {
      "name": "my-tool",
      "version": "1.0.0",
      "kind": "exe",
      "category": "utilities",
      "license": "MIT",
      "commands": ["my-tool"],
      "description": "A useful tool",
      "homepage": "https://example.com",
      "artifacts": {
        "windows-x64": {
          "type": "exe",
          "layout": "flat",
          "urls": ["https://example.com/my-tool.exe"],
          "sha256": "abcdef1234567890...",
          "size": 1048576,
          "files": [
            { "from": "my-tool.exe", "to": "my-tool.exe" }
          ]
        },
        "windows-arm64": {
          "type": "exe",
          "layout": "flat",
          "urls": ["https://example.com/my-tool-arm64.exe"],
          "sha256": "...",
          "size": 1048576,
          "files": [
            { "from": "my-tool.exe", "to": "my-tool.exe" }
          ]
        }
      }
    }
  ]
}
```

See [Section 12](#12-developer-guide--adding-packages) for the full artifact format specification.

---

## 8. Known Packages

The following packages are available through the official WPM source ([wpm-source](https://github.com/unixwin/wpm-source)):

| Package | Category | Description |
|---|---|---|
| **goawk** | text-processing | Go implementation of AWK — a pattern scanning and processing language |
| **bsdtar** | archive | BSD tar implementation — create and extract tar, zip, and other archives |
| **gzip** | archive | GNU gzip — compression and decompression utility |
| **openssh** | networking | OpenSSH client and server — secure remote login and file transfer |
| **make** | build | GNU Make — build automation tool |
| **neovim** | editors | Hyperextensible Vim-based text editor |
| **curl** | networking | Command-line tool for transferring data with URLs |
| **wget** | networking | Network downloader — retrieve files from the web |

### Category Taxonomy

Packages are organized by category. Use `wpm categories` to see all available categories:

- **archive** — compression and archive tools (bsdtar, gzip, ...)
- **build** — build tools and compilers (make, ...)
- **editors** — text editors (neovim, ...)
- **networking** — network utilities (curl, wget, openssh, ...)
- **text-processing** — text transformation tools (goawk, ...)
- **utilities** — general-purpose utilities

---

## 9. Package Artifact Format

### Artifact Types

WPM supports the following artifact types:

| Type | Extension | Extraction |
|---|---|---|
| `exe` | `.exe` | Single binary, no extraction needed |
| `zip` | `.zip` | Extracted via `tar.exe -xf` (Windows built-in) |
| `tar.gz` / `tgz` | `.tar.gz` | Extracted via `tar.exe -xf` |
| `tar.xz` | `.tar.xz` | Extracted via `tar.exe -xf` |

### Architecture Detection

WPM automatically detects the CPU architecture:

| CPU | Architecture Key |
|---|---|
| AMD64 / Intel 64 | `windows-x64` |
| ARM64 | `windows-arm64` |

### Layout Types

| Layout | Behavior |
|---|---|
| `flat` (default) | Files are placed directly into `usr/bin/`. |
| `shim` | Payload goes to `<root>/opt/<pkg>/`; command shims (hardlinks of `winuxcmd.exe`) are created in `usr/bin/`. The dispatcher routes to the actual payload. |

### SHA-256 Verification

Every artifact **must** have a `sha256` field. WPM refuses to install remote artifacts without a checksum. After downloading, the checksum is verified before extraction.

### File Mapping

The `files` array maps source paths (inside the archive or binary) to destination paths:

```json
{
  "from": "bin/my-tool.exe",
  "to": "my-tool.exe"
}
```

- If `to` is omitted, the destination filename is derived from `from`.
- Directory mappings are supported: if `from` ends with `/` or `kind` is `"dir"`, the entire directory is copied.
- Flat-layout destinations are resolved relative to `usr/bin/`.
- Shim-layout destinations are resolved relative to `<root>/opt/<pkg>/`.

---

## 10. WPM vs Chocolatey / Scoop

| Feature | WPM | Chocolatey | Scoop |
|---|---|---|---|
| **Language** | C++ (native) | C# / PowerShell | PowerShell |
| **Runtime dependency** | None | .NET Framework / PowerShell | PowerShell |
| **Package index** | JSON (GitHub-hosted) | NuGet-based repos | JSON (GitHub-hosted) |
| **Built into the tool** | Yes (`winuxcmd.exe`) | Separate install | Separate install |
| **Scope** | WinuxCmd commands + external tools | System-wide software | User-level software |
| **Self-update** | `wpm update winuxcmd` | `choco upgrade` | `scoop update` |
| **Cache** | SHA-256 verified local cache | Built-in cache | Built-in cache |
| **Proxy support** | Env vars + IE/PAC auto-detect | Config-based | Config-based |
| **Architecture** | x64, ARM64 | x64, ARM64 | x64 only (mostly) |
| **Shim system** | Hardlinks of the dispatcher binary | Shim EXEs (PowerShell) | Shim EXEs (PowerShell) |
| **Package count** | Growing (focused on CLI tools) | 8,000+ | 1,200+ |
| **Admin required** | No (user-level install) | Often yes | No |

### When to use WPM

- You already use WinuxCmd and want integrated package management.
- You want a zero-dependency, native package manager for CLI tools.
- You need packages that integrate with the WinuxCmd command ecosystem.
- You prefer a focused, minimal tool over a general-purpose software manager.

### When to consider Chocolatey / Scoop alongside WPM

- You need GUI applications or system services.
- You need a very large package catalog (thousands of packages).
- You need advanced features like pinning, uninstall hooks, or package dependencies.

---

## 11. Troubleshooting

### "no reachable index source"

**Cause:** WPM cannot fetch the package index from any source.

**Fixes:**

- Check your internet connection.
- Try a different source: `wpm index update -s official-jsdelivr`
- If behind a proxy, set environment variables:

  ```bash
  set WPM_HTTPS_PROXY=127.0.0.1:7890
  wpm index update
  ```

- WPM also probes common loopback proxy ports automatically.

### "sha256 mismatch"

**Cause:** The downloaded file does not match the expected checksum.

**Fixes:**

- The remote file may have been updated without an index change. Run `wpm index update` and retry.
- If the problem persists, the source may be compromised — do not install.

### "destination exists; use --force"

**Cause:** A file already exists at the target location.

**Fix:** Use `--force` to overwrite, or uninstall the conflicting package first.

### "package not found in local index"

**Cause:** The package name is not in the local index.

**Fix:** Update the index: `wpm index update`, then retry. If the package is from a custom source, ensure the source is configured.

### "wpm: refusing to uninstall 'winuxcmd'"

**Cause:** You tried to uninstall the core WinuxCmd package.

**Fix:** Use `wpm update winuxcmd` to manage the core binary instead.

### "executable_missing" or "winuxcmd.exe not found"

**Cause:** The WinuxCmd root structure is broken.

**Fix:** Reinstall WinuxCmd or run `wpm links rebuild` to recreate the command links.

### Cache is growing large

**Fix:** Clean the cache:

```bash
wpm clean cache
```

### Resetting everything

To fully reset WPM state:

```bash
wpm clean all
wpm index update
```

### Debugging with verbose mode

Add `--verbose` (`-v`) to any command for detailed progress output:

```bash
wpm install curl -v
wpm index update -v
```

### Machine-readable output

Use `--json` for scriptable output:

```bash
wpm list --json | jq '.packages[] | select(.installed)'
wpm installed --json
wpm info curl --json
```

---

## 12. Developer Guide — Adding Packages

To add a new package to the official WPM source, contribute to the [unixwin/wpm-source](https://github.com/unixwin/wpm-source) repository.

### Step 1: Create the package entry

Add a new object to the `packages` array in `index.json`:

```json
{
  "name": "my-new-tool",
  "version": "1.0.0",
  "kind": "exe",
  "category": "utilities",
  "license": "MIT",
  "commands": ["my-new-tool"],
  "description": "Short description of the tool",
  "homepage": "https://github.com/my-org/my-new-tool",
  "artifacts": {
    "windows-x64": {
      "type": "exe",
      "layout": "flat",
      "urls": [
        "https://github.com/my-org/my-new-tool/releases/download/v1.0.0/my-new-tool-x64.exe"
      ],
      "sha256": "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
      "size": 1048576,
      "files": [
        { "from": "my-new-tool.exe", "to": "my-new-tool.exe" }
      ]
    }
  }
}
```

### Step 2: Required fields

| Field | Type | Required | Description |
|---|---|---|---|
| `name` | string | Yes | Unique package name (lowercase, alphanumeric + hyphens). |
| `version` | string | Yes | Semantic version string. |
| `kind` | string | Yes | Package kind: `exe` for executables. |
| `category` | string | Yes | One of the established categories or a new one. |
| `license` | string | Yes | SPDX license identifier. |
| `commands` | string[] | Yes | Command names that become available after install. |
| `description` | string | Yes | One-line description. |
| `homepage` | string | Yes | Project homepage URL. |
| `artifacts` | object | Yes | Map of architecture key to artifact definition. |

### Step 3: Artifact definition

Each artifact must include:

| Field | Type | Required | Description |
|---|---|---|---|
| `type` | string | Yes | `exe`, `zip`, `tar.gz`, `tgz`, or `tar.xz`. |
| `layout` | string | No | `flat` (default) or `shim`. |
| `urls` | string[] | Yes | Download URLs (tried in order; first success wins). |
| `sha256` | string | Yes | SHA-256 hex digest of the artifact file. |
| `size` | number | No | Expected file size in bytes (informational). |
| `files` | array | Yes | File mapping from archive/internal paths to install destinations. |

### Step 4: File mapping rules

- **Simple binary:** `{ "from": "tool.exe", "to": "tool.exe" }`
- **Binary in subdirectory:** `{ "from": "bin/tool.exe", "to": "tool.exe" }`
- **Directory copy:** `{ "from": "lib/", "to": "lib/", "kind": "dir" }`
- **Omit `to`:** Destination filename is derived from `from`.

### Step 5: Computing SHA-256

**Windows (PowerShell):**

```powershell
(Get-FileHash -Algorithm SHA256 .\my-new-tool.exe).Hash.ToLower()
```

**Linux / macOS:**

```bash
sha256sum my-new-tool.exe | cut -d' ' -f1
```

### Step 6: Shim vs Flat layout

- Use **flat** (default) when the package is a single executable with no private DLLs.
- Use **shim** when the package has private DLLs or supporting files that must stay together. The shim layout isolates the payload under `<root>/opt/<pkg>/` and creates forwarding hardlinks in `usr/bin/`.

### Step 7: Submit a PR

1. Fork [unixwin/wpm-source](https://github.com/unixwin/wpm-source).
2. Add your package entry to `index.json`.
3. Verify your entry is valid JSON.
4. Submit a pull request.

### Index Schema Version

The current index schema version is `1`. Always set `"schema": 1` at the root of the index file.

---

## Appendix A: Configuration File

WPM stores configuration in `<root>/.wpm/config.json`:

```json
{
  "preferred_source": "auto",
  "region": "auto",
  "last_success_source": "official-github-raw",
  "user_sources": [
    {
      "name": "my-org",
      "region": "custom",
      "priority": 5,
      "index_urls": [
        "https://my-org.example.com/wpm/index.json"
      ]
    }
  ]
}
```

## Appendix B: Environment Variables

| Variable | Description |
|---|---|
| `WPM_HTTPS_PROXY` | HTTPS proxy server (highest priority for HTTPS) |
| `WPM_HTTP_PROXY` | HTTP proxy server |
| `HTTPS_PROXY` / `https_proxy` | Standard HTTPS proxy |
| `HTTP_PROXY` / `http_proxy` | Standard HTTP proxy |
| `ALL_PROXY` / `all_proxy` | Proxy for all protocols |
| `WT_SESSION` | Windows Terminal session (enables progress bar) |
| `WINUXSH_WPM_PROGRESS` | Force enable WPM progress bar |

## Appendix C: Directory Structure

| Path | Description |
|---|---|
| `.wpm/config.json` | User configuration |
| `.wpm/indexes/official.json` | Cached local package index |
| `.wpm/cache/` | Downloaded artifact files (SHA-256 verified) |
| `.wpm/staging/` | Temporary extraction area |
| `.wpm/backup/` | Pre-update backups of `winuxcmd.exe` |
| `usr/bin/` | Command hardlinks and shims |
| `opt/<pkg>/` | Shim-layout package payloads |
