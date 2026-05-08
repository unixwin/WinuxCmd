/*
 * MIT License
 *
 * Copyright (c) 2024-2026 WinuxCmd contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: ffi.h
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

#ifndef WINUX_FFI_H
#define WINUX_FFI_H

#ifdef _WIN32
#ifdef WINUX_FFI_EXPORTS
#define WINUX_API __declspec(dllexport)
#else
#define WINUX_API __declspec(dllimport)
#endif
#else
#define WINUX_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Execute command directly (no IPC, no daemon)
 *
 * @param command Command name (e.g., "ls", "echo")
 * @param args Array of arguments (NULL-terminated, can be NULL)
 * @param arg_count Number of arguments
 * @param cwd Current working directory (NULL to use current directory)
 * @param output Output buffer (allocated by FFI, must be freed with
 * winux_free_buffer)
 * @param error Error buffer (allocated by FFI, must be freed with
 * winux_free_buffer)
 * @param output_size Output size in bytes
 * @param error_size Error size in bytes
 * @return Exit code (0 on success, non-zero on error)
 *
 * @note All output and error buffers are allocated by the FFI and must be freed
 * using winux_free_buffer()
 */
WINUX_API int winux_execute(const char* command, const char** args,
                            int arg_count, const char* cwd, char** output,
                            char** error, size_t* output_size,
                            size_t* error_size);

/**
 * @brief Free memory allocated by FFI functions
 * @param buffer Buffer to free (can be NULL)
 */
WINUX_API void winux_free_buffer(char* buffer);

/**
 * @brief Free command array allocated by winux_get_all_commands
 * @param commands Command array to free
 * @param count Number of commands
 */
WINUX_API void winux_free_commands_array(char** commands, int count);

/**
 * @brief Get version information
 * @return Version string (e.g., "0.8.0")
 */
WINUX_API const char* winux_get_version(void);

/**
 * @brief Get all available command names
 * @param commands Array of command names (allocated by FFI, must be freed with
 * winux_free_commands_array)
 * @param count Number of commands
 * @return 0 on success, non-zero on error
 */
WINUX_API int winux_get_all_commands(char*** commands, int* count);

#ifdef __cplusplus
}
#endif

#endif  // WINUX_FFI_H
