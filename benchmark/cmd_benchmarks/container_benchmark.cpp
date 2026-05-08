/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights, to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to whom the Software
 *  is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: container_benchmark.cpp
 *  - CopyrightYear: 2026
 */

#include <benchmark/benchmark.h>

import std;
import container;
using namespace std::string_view_literals;

// Test data for benchmarks - use string_view instead of string
static constexpr auto test_extensions =
    std::to_array<std::pair<std::string_view, std::string_view>>({
        {".txt"sv, "ASCII text"sv},
        {".md"sv, "UTF-8 Unicode text"sv},
        {".json"sv, "JSON data"sv},
        {".xml"sv, "XML document text"sv},
        {".html"sv, "HTML document text"sv},
        {".htm"sv, "HTML document text"sv},
        {".css"sv, "Cascading Style Sheet text"sv},
        {".js"sv, "JavaScript source text"sv},
        {".ts"sv, "TypeScript source text"sv},
        {".py"sv, "Python script text"sv},
        {".cpp"sv, "C++ source text"sv},
        {".h"sv, "C header text"sv},
        {".hpp"sv, "C++ header text"sv},
        {".c"sv, "C source text"sv},
        {".exe"sv, "PE32 executable"sv},
        {".dll"sv, "PE32+ executable (DLL)"sv},
        {".pdf"sv, "PDF document"sv},
        {".zip"sv, "ZIP archive"sv},
        {".tar"sv, "TAR archive"sv},
        {".gz"sv, "GZIP compressed"sv},
    });

// Build ConstexprMap at compile time
constexpr auto constexpr_ext_map = make_constexpr_map(test_extensions);

// Test search strings - use string_view to match ConstexprMap key type
static constexpr auto search_strings =
    std::to_array<std::string_view>({".txt"sv, ".pdf"sv, ".exe"sv, ".dll"sv});

// ConstexprMap benchmarks
// =========================
static void BM_ConstexprMapLookup(benchmark::State& state) {
  for (auto _ : state) {
    for (const auto& key : search_strings) {
      auto result = constexpr_ext_map.get_or(key, ""sv);
      const volatile size_t count = result.size();
      benchmark::DoNotOptimize(count);
    }
  }
  state.SetItemsProcessed(state.iterations() * search_strings.size());
}
BENCHMARK(BM_ConstexprMapLookup);

static void BM_ConstexprMapIterate(benchmark::State& state) {
  for (auto _ : state) {
    const volatile size_t count = constexpr_ext_map.size();
    benchmark::DoNotOptimize(count);
    for (const auto& [key, value] : constexpr_ext_map) {
      (void)key;
      (void)value;
    }
  }
  state.SetItemsProcessed(state.iterations() * constexpr_ext_map.size());
}
BENCHMARK(BM_ConstexprMapIterate);

// std::unordered_map benchmarks
// ============================================
// Note: unordered_map uses std::string keys, so we need to use different test
// data

static std::unordered_map<std::string, std::string> unordered_ext_map = {
    {".txt", "ASCII text"},
    {".md", "UTF-8 Unicode text"},
    {".json", "JSON data"},
    {".xml", "XML document text"},
    {".html", "HTML document text"},
    {".htm", "HTML document text"},
    {".css", "Cascading Style Sheet text"},
    {".js", "JavaScript source text"},
    {".ts", "TypeScript source text"},
    {".py", "Python script text"},
    {".cpp", "C++ source text"},
    {".h", "C header text"},
    {".hpp", "C++ header text"},
    {".c", "C source text"},
    {".exe", "PE32 executable"},
    {".dll", "PE32+ executable (DLL)"},
    {".pdf", "PDF document"},
    {".zip", "ZIP archive"},
    {".tar", "TAR archive"},
    {".gz", "GZIP compressed"},
};

static const std::vector<std::string> unordered_search_strings = {
    ".txt", ".pdf", ".exe", ".dll"};

static void BM_UnorderedMapLookup(benchmark::State& state) {
  for (auto _ : state) {
    for (const auto& key : unordered_search_strings) {
      auto it = unordered_ext_map.find(key);
      const volatile bool found = (it != unordered_ext_map.end());
      benchmark::DoNotOptimize(found);
    }
  }
  state.SetItemsProcessed(state.iterations() * unordered_search_strings.size());
}
BENCHMARK(BM_UnorderedMapLookup);

static void BM_UnorderedMapInsert(benchmark::State& state) {
  for (auto _ : state) {
    std::unordered_map<std::string, std::string> temp;
    temp.reserve(test_extensions.size());
    for (const auto& [key, value] : test_extensions) {
      temp[std::string(key)] = std::string(value);
    }
    size_t total_size = 0;
    for (const auto& [key, value] : temp) {
      total_size += key.size() + value.size();
    }
    const volatile size_t count = total_size;
    benchmark::DoNotOptimize(count);
  }
  state.SetItemsProcessed(state.iterations() * test_extensions.size());
}
BENCHMARK(BM_UnorderedMapInsert);

// std::vector benchmarks
// ==============================
static void BM_StdVectorPushBack(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<int> vec;
    vec.reserve(state.range(0));
    for (int i = 0; i < state.range(0); ++i) {
      vec.push_back(i);
    }
    const volatile size_t count = vec.size();
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_StdVectorPushBack)->Range(4, 64);

static void BM_StdVectorIteration(benchmark::State& state) {
  std::vector<int> vec;
  for (int i = 0; i < 100; ++i) {
    vec.push_back(i);
  }
  for (auto _ : state) {
    size_t sum = 0;
    for (const auto& val : vec) {
      sum += val;
    }
    const volatile size_t count = sum;
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_StdVectorIteration);

// SmallVector push_back 测试
static void BM_SmallVectorPushBack(benchmark::State& state) {
  for (auto _ : state) {
    SmallVector<int, 64> vec;  // 内联容量 64
    for (int i = 0; i < state.range(0); ++i) {
      vec.push_back(i);
    }
    const volatile size_t count = vec.size();
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_SmallVectorPushBack)->Range(4, 256);

// SmallVector 迭代测试
static void BM_SmallVectorIteration(benchmark::State& state) {
  SmallVector<int, 64> vec;
  for (int i = 0; i < 100; ++i) {
    vec.push_back(i);
  }
  for (auto _ : state) {
    size_t sum = 0;
    for (const auto& val : vec) {
      sum += val;
    }
    const volatile size_t count = sum;
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_SmallVectorIteration);

// SmallVector emplace_back 测试
static void BM_SmallVectorEmplaceBack(benchmark::State& state) {
  for (auto _ : state) {
    SmallVector<std::string, 64> vec;
    for (int i = 0; i < state.range(0); ++i) {
      vec.emplace_back(10, 'a');
    }
    const volatile size_t count = vec.size();
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_SmallVectorEmplaceBack)->Range(4, 64);

// Run benchmarks
// BENCHMARK_MAIN();
