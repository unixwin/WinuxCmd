// Example: embed the WinuxCmd command library through the C ABI.
//
// Build (from a configured build tree, MSVC):
//   cl /nologo /I src\shared examples\consume\abi-consumer.cpp ^
//      build-vs\rust2-target\x86_64-pc-windows-msvc\release\winuxcmd_ffi.lib ^
//      advapi32.lib kernel32.lib ntdll.lib userenv.lib ws2_32.lib dbghelp.lib ^
//      wtsapi32.lib netapi32.lib
//
// The ABI passes argv/envp/cwd/stdin explicitly and returns Rust-owned
// stdout/stderr buffers. No CRT fd and no process stdio crosses the boundary.
#include <stdio.h>
#include <string.h>

#include "rust2_abi.h"

static int run_sort(const unsigned char* input, size_t input_len) {
  const char* envp[] = {"LC_ALL=C", NULL};
  winuxcmd_cbuf out = {0};
  winuxcmd_cbuf err = {0};

  int rc = winuxcmd_run("sort", 0, NULL, envp, ".", input, input_len, &out, &err);
  if (rc == 0) {
    fwrite(out.ptr, 1, out.len, stdout);
  } else {
    fprintf(stderr, "sort failed: rc=%d\n", rc);
    if (err.len) fwrite(err.ptr, 1, err.len, stderr);
  }
  winuxcmd_free_buf(&out);
  winuxcmd_free_buf(&err);
  return rc;
}

int main(void) {
  printf("abi=%d has_sort=%d has_nosuch=%d\n", winuxcmd_abi_version(),
         winuxcmd_has_command("sort"), winuxcmd_has_command("nosuch"));

  const char* text = "gamma\nalpha\nbeta\n";
  if (run_sort((const unsigned char*)text, strlen(text)) != 0) return 1;

  /* Explicit empty stdin: the command must not read the process's stdin. */
  return run_sort(NULL, 0);
}
