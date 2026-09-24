# Option Handling Guide

## Overview

This guide explains how to use the option handling system in the WinuxCmd project. With the new pipeline-based architecture, option handling is more streamlined and type-safe, providing a consistent way to handle command-line options across all commands.

## Core Concepts

### CommandContext

The `CommandContext` class provides type-safe access to command options and arguments. It automatically parses options based on the provided option definitions and allows you to access them with proper type checking:

```cpp
// Example: Accessing options with CommandContext
template<size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<std::vector<std::string>> {
  // Type-safe access to boolean options
  bool all = ctx.get<bool>("--all", false);
  bool list = ctx.get<bool>("--list", false);
  
  // Type-safe access to numeric options
  int tabsize = ctx.get<int>("--tabsize", 8);
  int width = ctx.get<int>("--width", 0);
  
  // Access positional arguments
  std::vector<std::string> paths;
  for (const auto& arg : ctx.positionals) {
    paths.push_back(std::string(arg));
  }
  
  return paths;
}
```

### OptionMeta

The `OptionMeta` structure defines a single command option with the following fields:

```cpp
struct OptionMeta {
  const char* short_name; // Short option (e.g., "-l")
  const char* long_name;  // Long option (e.g., "--list")
  const char* desc;       // Option description
};
```

### OPTION Macro

The `OPTION` macro simplifies creating `OptionMeta` objects:

```cpp
#define OPTION(short_opt, long_opt, description) \
    cmd::meta::OptionMeta{short_opt, long_opt, description}
```

## Usage Example (ls command)

### 1. Define Options

First, define your command options using the `OPTION` macro in a constexpr array:

```cpp
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

export auto constexpr LS_OPTIONS =
    std::array{
        OPTION("-a", "--all", "do not ignore entries starting with ."),
        OPTION("-A", "--almost-all", "do not list implied . and .."),
        OPTION("-l", "--list", "use a long listing format"),
        OPTION("-h", "--human-readable", "with -l, print sizes in human readable format"),
        OPTION("-T", "--tabsize", "assume tab stops at each COLS instead of 8"),
        OPTION("-w", "--width", "set output width to COLS. 0 means no limit"),
        // ... other options
    };
```

### 2. Create Pipeline Components

Create pipeline components to process the command, using `CommandContext` for option access:

```cpp
namespace ls_pipeline {
  namespace cp=core::pipeline;
  
  // Validate arguments
  auto validate_arguments(std::span<const std::string_view> args) -> cp::Result<std::vector<std::string>> {
    std::vector<std::string> paths;
    for (auto arg : args) {
      paths.push_back(std::string(arg));
    }
    return paths;
  }

  // Main pipeline
  template<size_t N>
  auto process_command(const CommandContext<N>& ctx)
      -> cp::Result<std::vector<std::string>>
  {
    return validate_arguments(ctx.positionals);
  }

}
```

### 3. Register the Command

Register the command with the `REGISTER_COMMAND` macro, passing the option definitions:

```cpp
REGISTER_COMMAND(ls,
                 /* name */
                 "ls",

                 /* synopsis */
                 "ls [OPTION]... [FILE]...",

                 /* description */
                 "List information about the FILEs (the current directory by default).\n"
                 "Sort entries alphabetically if none of -cftuvSUX nor --sort is specified.",

                 /* examples */
                 "  ls -la                  List all files in long format\n"
                 "  ls -lh                 List files with human-readable sizes\n"
                 "  ls -la /path/to/dir    List all files in specified directory",

                 /* see_also */
                 "cp, mv, rm, mkdir, rmdir",

                 /* author */
                 "WinuxCmd Team",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 LS_OPTIONS
) {
  using namespace ls_pipeline;

  auto result = process_command(ctx);
  if (!result) {
    cp::report_error(result, L"ls");
    return 1;
  }

  auto paths = *result;
  
  // Access options
  bool all = ctx.get<bool>("--all", false);
  bool almost_all = ctx.get<bool>("--almost-all", false);
  bool list = ctx.get<bool>("--list", false);
  bool human_readable = ctx.get<bool>("--human-readable", false);
  int tabsize = ctx.get<int>("--tabsize", 8);
  int width = ctx.get<int>("--width", 0);
  
  // Command logic
  // ...

  return 0;
}
```

## Migration Guide

To migrate an existing command to the new option handling system:

1. **Add necessary imports**:
   ```cpp
   import core;
   import utils;
   using cmd::meta::OptionMeta;
   using cmd::meta::OptionType;
   ```

2. **Define options**:
   - Replace manual option definitions with a constexpr array using the `OPTION` macro
   - Ensure all options have both short and long forms when appropriate

3. **Update command structure**:
   - Create a pipeline namespace for your command
   - Implement `validate_arguments` and `process_command` functions
   - Use `CommandContext` for option access instead of manual parsing

4. **Register the command**:
   - Use the new `REGISTER_COMMAND` macro format
   - Pass the option definitions array

5. **Update error handling**:
   - Use `core::pipeline::Result` for error reporting
   - Use `cp::report_error` to display errors

## Best Practices

- **Type safety**: Always use `ctx.get<T>()` with the appropriate type for option access
- **Default values**: Always provide a default value when accessing options
- **Consistency**: Use the same option names across commands when possible
- **Documentation**: Provide clear descriptions for all options
- **Error handling**: Use pipeline components and `Result` for consistent error handling
- **Modularity**: Separate validation from processing logic

## Troubleshooting

### Common Issues

1. **Option not found**: Ensure the option is defined in the options array
2. **Type mismatch**: Ensure the type used in `ctx.get<T>()` matches the expected type
3. **Default values**: Ensure you provide appropriate default values for all options
4. **Module imports**: Ensure you've imported all necessary modules

### Debugging Tips

- Print the `CommandContext` to verify options are being parsed correctly
- Check that positional arguments are being captured properly
- Ensure option names in the array match those used in `ctx.get<T>()`

## Example: Full Implementation

The `echo.cppm` file serves as the reference implementation for the new option handling system. Key features include:

1. **Option definitions**: Uses a constexpr array with `OPTION` macro
2. **Pipeline components**: Implements validation and processing functions
3. **CommandContext usage**: Uses type-safe option access
4. **Error handling**: Uses `core::pipeline::Result` for error reporting
5. **Command registration**: Uses the new `REGISTER_COMMAND` macro format

By following this pattern, all commands can benefit from consistent, type-safe, and maintainable option handling.
