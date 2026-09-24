module;

#include "version.hpp"

export module version;

export namespace WinuxCmd {
// Re-export all symbols from version.hpp
export using ::WinuxCmd::VERSION_MAJOR;
export using ::WinuxCmd::VERSION_MINOR;
export using ::WinuxCmd::VERSION_PATCH;
export using ::WinuxCmd::VERSION_STRING;
export using ::WinuxCmd::VERSION_NUMBER;
export using ::WinuxCmd::BUILD_TYPE;
export using ::WinuxCmd::BUILD_TIMESTAMP;
export using ::WinuxCmd::COMPILER_ID;
export using ::WinuxCmd::COMPILER_VERSION;
}  // namespace WinuxCmd
