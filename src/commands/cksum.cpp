// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cksum with GNU parity options.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h

#include "core/command_macros.h"


import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CKSUM_OPTIONS = std::array{
    // [GNU]
    OPTION("-a", "--algorithm",
           "select the digest type to use.  See DIGEST below", STRING_TYPE),
    // [GNU]
    OPTION("-b", "--binary",
           "read in binary mode (default unless reading tty stdin)"),
    // [GNU]
    OPTION("-c", "--check", "read checksums from the FILEs and check them"),
    // [GNU]
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files"),
    // [GNU]
    // [GNU] --quiet/--status are long-only in GNU cksum; the -q/-s short
    // forms belong to md5sum & co. and are invented here (#1411).
    OPTION("", "--quiet",
           "don't print OK for each successfully verified file"),
    // [GNU]
    OPTION("", "--status",
           "don't output anything, status code shows success"),
    // [GNU]
    OPTION("-t", "--text", "read in text mode (default if reading tty stdin)"),
    // [GNU]
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines"),
    // [GNU]
    OPTION("", "--strict",
           "exit non-zero for improperly formatted checksum lines"),
    // [GNU]
    OPTION("", "--tag",
           "create a BSD-style checksum (the default for compatibility)"),
    // [GNU]
    OPTION("", "--untagged",
           "create a reversed style checksum, without digest type"),
    // [GNU]
    OPTION("-z", "--zero",
           "end each output line with NUL, not newline, and disable file name "
           "escaping"),
    // [GNU]
    OPTION("", "--raw", "print a raw binary digest, not hexadecimal"),
    // [GNU]
    OPTION("", "--base64", "print a base64-encoded digest"),
    // [GNU]
    OPTION("-l", "--length",
           "digest length in bits; must not exceed the maximum for the blake2 "
           "algorithm and must be a multiple of 8",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--debug", "print debug output")};


REGISTER_COMMAND(
    cksum, "cksum", "cksum [OPTION]... [FILE]...",
    "Print or verify checksums.\n"
    "\n"
    "By default use the 32 bit CRC algorithm.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "  -a, --algorithm=TYPE  select the digest type to use.  See DIGEST "
    "below\n"
    "      --base64          print base64-encoded digests (not hexadecimal)\n"
    "  -b, --binary          read in binary mode (default unless reading tty "
    "stdin)\n"
    "  -c, --check           read checksums from the FILEs and check them\n"
    "      --ignore-missing  don't fail or report status for missing files\n"
    "  -l, --length=BITS     digest length in bits; must not exceed the max "
    "for\n"
    "                          the blake2 algorithm and must be a multiple "
    "of 8\n"
    "  -q, --quiet           don't print OK for each successfully verified "
    "file\n"
    "      --raw             print a raw binary digest, not hexadecimal\n"
    "  -s, --status          don't output anything, status code shows "
    "success\n"
    "      --strict          exit non-zero for improperly formatted checksum "
    "lines\n"
    "  -t, --text            read in text mode (default if reading tty "
    "stdin)\n"
    "      --tag             create a BSD-style checksum (the default for "
    "digest algorithms)\n"
    "      --untagged        create a reversed style checksum, without "
    "digest type\n"
    "  -w, --warn            warn about improperly formatted checksum lines\n"
    "  -z, --zero            end each output line with NUL, not newline\n"
    "      --debug           print debug output\n"
    "\n"
    "DIGEST determines the digest algorithm and default output format:\n"
    "\n"
    "  sysv    (equivalent to sum -s)\n"
    "  bsd     (equivalent to sum -r)\n"
    "  crc     (equivalent to cksum)\n"
    "  md5     (equivalent to md5sum)\n"
    "  sha1    (equivalent to sha1sum)\n"
    "  sha224  (equivalent to sha224sum)\n"
    "  sha256  (equivalent to sha256sum)\n"
    "  sha384  (equivalent to sha384sum)\n"
    "  sha512  (equivalent to sha512sum)\n"
    "  blake2b (equivalent to b2sum)\n"
    "  sm3     (only available through cksum)\n",
    "  cksum file.txt\n"
    "  cksum -a sha256 file.txt\n"
    "  cksum -a blake2b -l 128 file.txt\n"
    "  cksum --tag file.txt\n"
    "  cksum -c checksums.txt\n"
    "  echo \"test\" | cksum",
    "md5sum(1), sha1sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CKSUM_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/cksum.rs); the C++ side keeps only this
  // registration — the 1.x cksum_pipeline namespace is deleted. ctx.raw_args
  // carries the operands verbatim for the rust2 getopt layer.
  //
  // stdin mode (ABI v1 whole-buffer, the md5sum precedent): GNU digest.c
  // reads fd 0 when no FILE operand stands and treats "-" as a per-operand
  // stdin marker (including `-c` checklists), so the bridge slurps fd 0
  // exactly for those shapes.
  bool stdin_is_input = ctx.positionals.empty();
  for (auto arg : ctx.positionals) {
    if (arg == "-") stdin_is_input = true;
  }
  if (stdin_is_input) {
    return winux::rust2::run_command_with_stdin("cksum", ctx.raw_args);
  }
  return winux::rust2::run_command("cksum", ctx.raw_args);
}
