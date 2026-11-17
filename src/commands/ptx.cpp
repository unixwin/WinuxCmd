/*
 * Copyright 2026 WinuxCmd
 * ptx compatible subset based on GNU coreutils ptx data flow.
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PTX_OPTIONS = std::array{
    // [GNU]
    OPTION("-A", "--auto-reference",
           "output automatically generated references", BOOL_TYPE),
    // [EXT] GNU ptx has no -C/--copyright (ptx.c long_options and
    // optstring carry neither); kept as a WinuxCmd extension.
    OPTION("-C", "--copyright", "display copyright and version", BOOL_TYPE),
    // [GNU]
    OPTION("-G", "--traditional", "use traditional non-GNU word splitting",
           BOOL_TYPE),
    // [GNU]
    OPTION("-F", "--flag-truncation", "string used for line truncation",
           STRING_TYPE),
    // [GNU]
    OPTION("-M", "--macro-name", "macro name for formatted output",
           STRING_TYPE),
    // [GNU]
    OPTION("-O", "", "generate output as roff directives", BOOL_TYPE),
    // [GNU]
    OPTION("-R", "--right-side-refs", "put references in right margin",
           BOOL_TYPE),
    // [GNU]
    OPTION("-S", "--sentence-regexp", "regexp for sentence ends", STRING_TYPE),
    // [GNU]
    OPTION("-T", "", "generate output as TeX directives", BOOL_TYPE),
    // [GNU] --format=roff|tex (ptx.c long_options, val 10): the long
    // spelling of -O/-T.
    OPTION("", "--format", "set output format ('roff' or 'tex')",
           STRING_TYPE),
    // [GNU]
    OPTION("-W", "--word-regexp", "regexp for words", STRING_TYPE),
    // [GNU]
    OPTION("-b", "--break-file", "file containing word break characters",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--ignore-case", "fold lower case to upper case for sorting",
           BOOL_TYPE),
    // [GNU]
    OPTION("-g", "--gap-size", "gap size in columns between output fields",
           INT_TYPE),
    // [GNU]
    OPTION("-i", "--ignore-file", "ignore words from file", STRING_TYPE),
    // [GNU]
    OPTION("-o", "--only-file", "only output words from file", STRING_TYPE),
    // [GNU]
    OPTION("-r", "--references", "first input field is reference", BOOL_TYPE),
    // [GNU]
    OPTION("-t", "--typeset-mode", "output for troff or nroff", BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--width", "output width in columns, reference excluded",
           INT_TYPE)};

REGISTER_COMMAND(ptx, "ptx", "ptx [OPTION]... [FILE]...",
                 "Produce a permuted index of file contents.",
                 "  ptx file.txt\n  ptx -w 40 file.txt", "grep(1), sort(1)",
                 "WinuxCmd", "Copyright 2026 WinuxCmd", PTX_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/ptx.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // stdin is pulled on demand through the streaming ABI, so fd 0 stays
  // untouched until (and unless) the command actually reads it.
  return winux::rust2::run_command_with_stdin_streaming("ptx", ctx.raw_args);
}
