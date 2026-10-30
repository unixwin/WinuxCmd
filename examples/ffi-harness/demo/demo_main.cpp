// Spawn twin of the demo DLL: `winuxcmd-demo.exe <applet> [args...]`
// dispatching the identical demo applet logic in a child process, so the
// harness's spawn fallback and the in-process path can be byte-compared.
// Test double only — NOT the real winuxcmd.exe; never installed.
#include <windows.h>

#include <string>
#include <vector>

#include "demo_applets.h"

int wmain(int argc, wchar_t *argv[]) {
  if (argc < 2) return 64;
  std::vector<std::wstring> args(argv + 2, argv + argc);
  const int rc = demo_dispatch(argv[1], args);
  return rc < 0 ? 127 : rc;  // unknown applet, like command-not-found
}
