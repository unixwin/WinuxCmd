module;

#include "version.hpp"

export module version;

export namespace WinuxCmd {
// Re-export all symbols from version.hpp
// NOTE: the inner `export` keywords are intentionally omitted: the enclosing
// `export namespace` already exports these using-declarations, and a second
// `export` on them is ill-formed (GCC: "'export' may only occur once in an
// export declaration"). MSVC accepts the redundant form; GCC 16 rejects it.
using ::WinuxCmd::VERSION_MAJOR;
using ::WinuxCmd::VERSION_MINOR;
using ::WinuxCmd::VERSION_PATCH;
using ::WinuxCmd::VERSION_STRING;
using ::WinuxCmd::VERSION_NUMBER;
using ::WinuxCmd::BUILD_TYPE;
using ::WinuxCmd::BUILD_TIMESTAMP;
using ::WinuxCmd::COMPILER_ID;
using ::WinuxCmd::COMPILER_VERSION;
}  // namespace WinuxCmd
