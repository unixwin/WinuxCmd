/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr TSORT_OPTIONS =
    std::array{OPTION("", "", "topological sort", STRING_TYPE),
               // [GNU] -w: accepted and ignored (POSIX.1-2024); hidden
               OPTION("-w", "", "", BOOL_TYPE)};

REGISTER_COMMAND(tsort,
                 /* cmd_name */ "tsort",
                 /* cmd_synopsis */ "tsort [FILE]",
                 /* cmd_desc */ "Perform topological sort.",
                 /* examples */ "tsort deps.txt",
                 /* see_also */ "sort",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TSORT_OPTIONS) {
  std::string input;

  if (ctx.positionals.empty()) {
    // Read from stdin
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
  } else {
    // Read from file
    std::string filename = std::string(ctx.positionals[0]);
    std::wstring wfilename = utf8_to_wstring(filename);
    HANDLE hFile =
        CreateFileW(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("tsort: cannot open '" + filename + "'");
      return 1;
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    input.resize(fileSize.QuadPart);
    DWORD bytesRead;
    ReadFile(hFile, &input[0], static_cast<DWORD>(fileSize.QuadPart),
             &bytesRead, nullptr);
    CloseHandle(hFile);
    input.resize(bytesRead);
  }

  // [GNU] Empty input is not an error: tsort simply prints nothing and
  // exits 0 (e.g. "tsort /dev/null").

  // [GNU] tsort reads whitespace-separated pairs; an odd token count is
  // an error (uutils #7077).
  std::vector<std::string> tokens;
  std::istringstream tok_ss(input);
  std::string tok;
  while (tok_ss >> tok) tokens.push_back(tok);
  if (tokens.size() % 2 != 0) {
    safeErrorPrintLn(
        ::winux::i18n::format("command.tsort.error.odd_tokens",
                              "tsort: input contains an odd number of tokens"));
    return 1;
  }

  std::set<std::string> nodes;
  std::map<std::string, std::vector<std::string>> graph;
  std::set<std::pair<std::string, std::string>> seen_pairs;
  const std::string input_name =
      ctx.positionals.empty() ? "-" : std::string(ctx.positionals[0]);

  for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
    const std::string& node = tokens[i];
    const std::string& dep = tokens[i + 1];
    // [GNU] record_relation() ignores a relation whose two members are
    // identical, so a self-pair like "a a" registers the node but is not a
    // loop (uutils #8743).
    if (node == dep) {
      nodes.insert(node);
      continue;
    }
    // [GNU] Duplicate pairs are recorded silently (each adds a redundant
    // edge that decrements exactly as often as it was counted); dropping
    // the extra edge here is equivalent and produces no diagnostic.
    if (!seen_pairs.insert({node, dep}).second) {
      continue;
    }
    graph[node].push_back(dep);
    nodes.insert(node);
    nodes.insert(dep);
  }

  std::map<std::string, int> remaining;
  std::map<std::string, size_t> order_index;
  size_t order = 0;
  for (const auto& n : nodes) {
    remaining[n] = 0;
    order_index[n] = order++;
  }
  for (const auto& [node, deps] : graph)
    for (const auto& dependency : deps) ++remaining[dependency];
  std::set<std::string> ready;
  for (const auto& [node, degree] : remaining)
    if (degree == 0) ready.insert(node);
  size_t emitted = 0;
  bool had_loop = false;
  while (emitted < nodes.size()) {
    if (ready.empty()) {
      // [GNU] Break the cycle: emit the earliest-mentioned remaining node
      // and report the loop on stderr. Output continues with the partial
      // order, but the exit status is 1 (uutils #7074).
      had_loop = true;
      std::string start;
      size_t best = SIZE_MAX;
      for (const auto& n : nodes) {
        if (remaining[n] >= 0 && order_index[n] < best) {
          best = order_index[n];
          start = n;
        }
      }
      if (start.empty()) break;
      // Find the loop path from start (DFS) for the stderr report.
      std::vector<std::string> loop{start};
      std::set<std::string> visited{start};
      std::string current = start;
      while (true) {
        const auto it = graph.find(current);
        if (it == graph.end()) break;
        bool advanced = false;
        for (const auto& dependency : it->second) {
          if (dependency == start) {
            current = start;
            advanced = true;
            break;
          }
          if (nodes.contains(dependency) && !visited.contains(dependency)) {
            visited.insert(dependency);
            loop.push_back(dependency);
            current = dependency;
            advanced = true;
            break;
          }
        }
        if (!advanced || current == start) break;
      }
      safeErrorPrintLn("tsort: " + input_name + ": input contains a loop:");
      for (const auto& member : loop) {
        safeErrorPrintLn("tsort: " + member);
      }
      // Break the loop at the start node.
      ready.insert(start);
      remaining[start] = 0;
    }
    auto node = *ready.begin();
    ready.erase(ready.begin());
    safePrintLn(node);
    ++emitted;
    remaining[node] = -1;  // mark emitted
    for (const auto& dependency : graph[node])
      if (--remaining[dependency] == 0) ready.insert(dependency);
  }
  if (had_loop) {
    return 1;
  }

  return 0;
}
