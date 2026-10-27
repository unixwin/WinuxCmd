// Test-double DLL for the FFI harness: implements the winux_ffi ABI v2
// surface (same signatures as src/ffi/winux_ffi.h) over the demo applets,
// so loader mechanics — discovery, SHA-256 pinning, tiering, switch,
// fail-closed fallback — can be exercised where the real winuxcore.dll
// has not been built. NOT a WinuxCmd product binary; never installed.
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "demo_applets.h"

struct FfiInitArgs {
  uint32_t struct_size;
  uint32_t abi_version;
  const wchar_t *install_root;
  const wchar_t *locale_stamp;
};
struct FfiContext {
  uint32_t struct_size;
  uint32_t abi_version;
  const wchar_t *applet;
  int32_t argc;
  const wchar_t *const *argv;
  const wchar_t *env_block;
  const wchar_t *cwd;
  HANDLE stdin_handle;
  HANDLE stdout_handle;
  HANDLE stderr_handle;
  int (*cancel_check)(void *);
  void *cancel_user_data;
};

namespace {
bool g_initialized = false;

bool is_absolute_wpath(const wchar_t *p) {
  if (!p || !*p) return false;
  if (p[0] == L'\\' && p[1] == L'\\') return true;
  return ((p[0] >= L'a' && p[0] <= L'z') || (p[0] >= L'A' && p[0] <= L'Z')) &&
         p[1] == L':' && (p[2] == L'\\' || p[2] == L'/');
}
}  // namespace

extern "C" {

uint32_t winux_ffi_abi_version(void) { return 2; }

int winux_ffi_init(const FfiInitArgs *args) {
  if (!args || args->struct_size < sizeof(FfiInitArgs)) return -1001;
  if (args->abi_version != 2) return -1003;
  if (!is_absolute_wpath(args->install_root)) return -1001;
  g_initialized = true;
  return 0;
}

int winux_ffi_call(const FfiContext *ctx) {
  if (!ctx || ctx->struct_size < sizeof(FfiContext)) return -1001;
  if (ctx->abi_version != 2) return -1003;
  if (!g_initialized) return -1002;
  if (!ctx->applet || !is_absolute_wpath(ctx->cwd)) return -1001;
  if (ctx->cancel_check && ctx->cancel_check(ctx->cancel_user_data))
    return -1006;

  // Borrow the ctx handles/cwd like the real shell, minus env borrowing
  // (the demo applets read no environment).
  HANDLE saved_in = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE saved_out = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE saved_err = GetStdHandle(STD_ERROR_HANDLE);
  if (ctx->stdin_handle) SetStdHandle(STD_INPUT_HANDLE, ctx->stdin_handle);
  if (ctx->stdout_handle) SetStdHandle(STD_OUTPUT_HANDLE, ctx->stdout_handle);
  if (ctx->stderr_handle) SetStdHandle(STD_ERROR_HANDLE, ctx->stderr_handle);
  wchar_t saved_cwd[32768];
  GetCurrentDirectoryW(32768, saved_cwd);
  SetCurrentDirectoryW(ctx->cwd);

  std::vector<std::wstring> args;
  for (int32_t i = 0; i < ctx->argc; ++i) {
    args.emplace_back(ctx->argv && ctx->argv[i] ? ctx->argv[i] : L"");
  }
  const int rc = demo_dispatch(ctx->applet, args);

  SetCurrentDirectoryW(saved_cwd);
  if (ctx->stdin_handle) SetStdHandle(STD_INPUT_HANDLE, saved_in);
  if (ctx->stdout_handle) SetStdHandle(STD_OUTPUT_HANDLE, saved_out);
  if (ctx->stderr_handle) SetStdHandle(STD_ERROR_HANDLE, saved_err);
  return rc < 0 ? -1004 : rc;  // unknown applet in the demo set
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
  return TRUE;
}
