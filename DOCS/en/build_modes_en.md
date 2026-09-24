# Build Modes Configuration Guide

## Overview

WinuxCmd now supports two build modes: **Development mode** (DEV) and **Release mode** (RELEASE), each optimized for different usage scenarios.

## Build Mode Options

### BUILD_MODE
Controls the overall build configuration:
- `DEV` - Development mode (default)
- `RELEASE` - Release mode

## Detailed Configuration by Mode

### Development Mode (DEV)
Optimized for daily development and debugging with fast incremental builds:

| Option | Default Value | Description |
|--------|---------------|-------------|
| ENABLE_TESTS | ON | Enable unit tests |
| GENERATE_MAP_INFO | ON | Generate map files for debugging |
| ENABLE_UNITY_BUILD | OFF | Disable Unity build (faster incremental compilation) |
| ENABLE_UPX | OFF | Disable UPX compression |

**Advantages:**
- Fast incremental compilation
- Complete debug information
- Includes all test cases
- Convenient for development and debugging

### Release Mode (RELEASE)
Optimized for production environments with better performance and smaller size:

| Option | Default Value | Description |
|--------|---------------|-------------|
| ENABLE_TESTS | OFF | Disable unit tests |
| GENERATE_MAP_INFO | OFF | Don't generate map files |
| ENABLE_UNITY_BUILD | ON | Enable Unity build (better performance) |
| ENABLE_UPX | OFF | Disable UPX compression (always disabled) |

**Advantages:**
- Smaller executable file size
- Faster runtime performance
- Removes debug and test code
- Suitable for distribution to end users

## Usage Instructions

### 1. Command Line Building

#### Development Mode Build (Default)
```bash
cmake -B build
cmake --build build
```

#### Explicitly Specify Development Mode
```bash
cmake -B build -DBUILD_MODE=DEV
cmake --build build
```

#### Release Mode Build
```bash
cmake -B build -DBUILD_MODE=RELEASE
cmake --build build --config Release
```

### 2. Override Default Configuration

Individual options can be overridden even in specific modes:

```bash
# Enable Unity build for performance testing in development mode
cmake -B build -DBUILD_MODE=DEV -DENABLE_UNITY_BUILD=ON

# Keep tests in release mode
cmake -B build -DBUILD_MODE=RELEASE -DENABLE_TESTS=ON
```

## Build Output Examples

### Development Mode Output
```
-- Configuring for DEVELOPMENT mode
--   - Fast incremental builds: ENABLED
--   - Tests: ENABLED
--   - Map generation: ENABLED
--   - Unity build: DISABLED (for faster incremental compilation)
--   - UPX compression: DISABLED
-- =================== BUILD CONFIGURATION SUMMARY ===================
-- Build Mode: DEV
-- Tests Enabled: ON
-- Map Info Generation: ON
-- Unity Build: OFF
-- UPX Compression: OFF
-- Static CRT: ON
-- Hardlinks: ON
-- =================================================================
```

### Release Mode Output
```
-- Configuring for RELEASE mode
--   - Optimized performance and size: ENABLED
--   - Tests: DISABLED
--   - Map generation: DISABLED
--   - Unity build: ENABLED (improves performance and reduces size)
--   - UPX compression: DISABLED
-- =================== BUILD CONFIGURATION SUMMARY ===================
-- Build Mode: RELEASE
-- Tests Enabled: OFF
-- Map Info Generation: OFF
-- Unity Build: ON
-- UPX Compression: OFF
-- Static CRT: ON
-- Hardlinks: ON
-- =================================================================
```

## Best Practice Recommendations

### Daily Development
```bash
# Use default development mode
cmake -B build-dev
cmake --build build-dev
```

### Performance Testing
```bash
# Enable Unity build for performance testing
cmake -B build-perf -DBUILD_MODE=DEV -DENABLE_UNITY_BUILD=ON
cmake --build build-perf
```

### Release Preparation
```bash
# Full release build
cmake -B build-release -DBUILD_MODE=RELEASE
cmake --build build-release --config Release
```

### CI/CD Pipeline
```bash
# Development environment build (includes tests)
cmake -B build-ci-dev -DBUILD_MODE=DEV
cmake --build build-ci-dev
ctest --test-dir build-ci-dev

# Production environment build
cmake -B build-ci-prod -DBUILD_MODE=RELEASE
cmake --build build-ci-prod --config Release
```

## Important Notes

1. **Incremental Compilation**: Development mode disables Unity build for fastest incremental compilation
2. **First Build**: First build will be slow regardless of mode
3. **Disk Space**: Development mode generates more intermediate files and debug information
4. **Runtime Performance**: Release mode executables typically have 20-30% performance improvement
5. **File Size**: Release mode produces smaller binaries without UPX compression
6. **UPX Disabled**: UPX compression is permanently disabled in all modes for reliability

## Troubleshooting

If you encounter configuration issues:

1. Clean build directory:
   ```bash
   rm -rf build/
   ```

2. Check CMake version:
   ```bash
   cmake --version
   ```
   Ensure you're using CMake 3.30.0 or higher

3. View detailed configuration information:
   ```bash
   cmake -B build -DBUILD_MODE=DEV --debug-output
   ```

## UPX Compression Policy

UPX compression has been permanently disabled due to:
- Potential compatibility issues with antivirus software
- Unreliable decompression on some systems
- Debugging difficulties when compressed
- Minimal size benefits compared to Unity build optimization

The ENABLE_UPX option is maintained for backward compatibility but has no effect.