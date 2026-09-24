#!/usr/bin/env python3
"""
MSVC Map File Profiler
==========================================
Complete analysis tool for MSVC .map files with visualization and deep inspection.

Features:
    - Precise symbol size calculation using section table
    - 18-category classification with no double counting
    - Template instantiation hotspot analysis
    - COMDAT folding estimation (/OPT:ICF)
    - STL overhead attribution per object file
    - Console output with ASCII progress bars
    - CSV export for all symbols
    - JSON export with summary and optional full symbol list
    - Matplotlib charts (pie chart, bar chart)
    - Standalone HTML report with ECharts
    - Multi-file comparison
"""

import re
import sys
import os
import argparse
import json
import csv
import shutil
import subprocess
from collections import defaultdict
from pathlib import Path

# Keep emoji/status output from crashing on GBK consoles.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

# ============================================================================
# Optional dependencies
# ============================================================================
try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

try:
    import colorama
    from colorama import Fore, Style
    colorama.init()
    HAS_COLORAMA = True
except ImportError:
    HAS_COLORAMA = False


# ============================================================================
# Constants & Classification Rules
# ============================================================================

# Category priority: symbols are only assigned to the FIRST matching category
CATEGORY_PRIORITY = [
    ("exception",           ["throw", "catch", "ehstate", "xthrow", "uncaught",
                             "_tls", "seh_", "CxxFrameHandler"]),
    ("rtti_vtable",        ["??_7", "UEAA", "RTTI", "type_info", "`vftable'",
                            "`RTTI Complete Object Locator'"]),
    ("std_function",       ["_Func_impl", "lambda", "operator()", "?R", "?A0x"]),
    ("unordered_map",      ["unordered_map", "_Hash", "_Umap", "registry", "_HashMap"]),
    ("stl_string",         ["?$basic_string", "?$char_traits", "?$allocator"]),
    ("stl_vector",         ["?$vector", "?$_Vector", "?$_Reallocate"]),
    ("stl_other",          ["?$list", "?$deque", "?$set", "?$map", "?$_Tree", "?$_List"]),
    ("module_metadata",    ["?__CxxModule", "cppm.obj"]),
    ("crt_startup",        ["_crt_", "__scrt_", "mainCRTStartup", "WinMainCRTStartup"]),
    ("thread_local",       ["_tls_", "TLS", "thread_local"]),
    ("guard",             ["_guard", "__guard", "___guard"]),
    ("global_ctor",       ["`dynamic initializer'", "`dynamic atexit destructor'"]),
    ("virtual_inline",    ["*::`vcall'", "*::`scalar deleting destructor'"]),
    ("code_my",           []),  # Business logic - matched after excluding libs
    ("code_lib",          []),  # Static library code (CRT, etc)
    ("data",             ["?g_", "?s_", "?_"]),
    ("other",            []),
]

CATEGORY_NAMES = [c[0] for c in CATEGORY_PRIORITY]
CATEGORY_PATTERNS = [c[1] for c in CATEGORY_PRIORITY]

# Object files that are considered part of the C/C++ runtime
LIB_OBJ_KEYWORDS = [
    "libc", "libcpmt", "libvcruntime", "libucrt", "libcmtd", "libcmt",
    "msvcrt", "vcruntime", "ucrt", "crt",
]

PROJECT_OBJ_PREFIXES = ("winuxcmd-commands:",)
PROJECT_OBJS = {"main.cpp.obj"}


def is_project_object(obj, obj_full=""):
    """Return True if an object token belongs to this project."""
    return (
        obj in PROJECT_OBJS
        or any(obj.startswith(prefix) for prefix in PROJECT_OBJ_PREFIXES)
        or any(obj_full.startswith(prefix) for prefix in PROJECT_OBJ_PREFIXES)
    )


def is_runtime_object(obj, obj_full=""):
    obj_text = f"{obj} {obj_full}".lower()
    return any(lib in obj_text for lib in LIB_OBJ_KEYWORDS)


def symbol_owner(symbol):
    """Coarse ownership bucket for code-size attribution."""
    obj = symbol.get('obj', '')
    obj_full = symbol.get('obj_full', obj)
    if is_project_object(obj, obj_full):
        return 'project'
    if is_runtime_object(obj, obj_full):
        return 'runtime'
    return 'other'


def display_source(obj):
    """Prefer source/object names without the static-library prefix."""
    for prefix in PROJECT_OBJ_PREFIXES:
        if obj.startswith(prefix):
            return obj[len(prefix):]
    return obj


def _strip_obj_suffix(source):
    for suffix in (".cppm.obj", ".cpp.obj", ".c.obj"):
        if source.endswith(suffix):
            return source[:-len(suffix)]
    return source


def infer_symbol_group(symbol, readable_name=""):
    """Best-effort command/module bucket for Unity-build symbols.

    In Unity builds most command code lands in one generated .obj, so object
    attribution is too coarse.  MSVC decorated names still carry namespaces such
    as `ls_pipeline`, `wpm`, and instantiated template argument types; use those
    to recover useful command-level ownership.
    """
    source = display_source(symbol.get('obj', ''))
    text = f"{symbol.get('name', '')} {readable_name}"

    for pattern in (
        r'\b([A-Za-z][A-Za-z0-9_]*)_pipeline::',
        r'@([A-Za-z][A-Za-z0-9_]*)_pipeline@@',
    ):
        match = re.search(pattern, text)
        if match:
            return match.group(1)

    if re.search(r'(\bwpm::|@wpm@@)', text):
        return "wpm"

    for pattern in (
        r'\bexecute([A-Za-z][A-Za-z0-9_]*)(?:<|\()',
        r'\?execute([A-Za-z][A-Za-z0-9_]*)(?:[@$])',
    ):
        match = re.search(pattern, text)
        if match:
            return match.group(1)

    if "nlohmann" in text:
        return "third_party:nlohmann_json"

    if "commands_unity.cpp.obj" in source:
        return "unity:unattributed"

    if source.endswith(".cppm.obj"):
        return f"module:{_strip_obj_suffix(source)}"
    return _strip_obj_suffix(source)


# ============================================================================
# Core Parser
# ============================================================================
class MapParser:
    """MSVC .map file parser with section table and symbol resolution."""

    SECTION_LINE_RE = re.compile(
        r'^\s*([0-9A-F]{4}):([0-9A-F]{8})\s+([0-9A-F]{6,8}H?)\s+(\S+)\s+(\S+)',
        re.IGNORECASE
    )

    SYMBOL_LINE_RE = re.compile(
        r'^\s*([0-9A-F]{4}):([0-9A-F]{8})\s+(\S+)\s+([0-9A-F]{16})\s+(.*)$',
        re.IGNORECASE
    )

    def __init__(self, map_path):
        self.map_path = Path(map_path)
        self.sections = {}        # (section_index, start_offset) -> {length, name, class}
        self.sections_by_segment = defaultdict(list)
        self.symbols = []         # Raw symbols without size
        self.symbols_with_size = []  # Symbols with calculated size
        self.alias_groups = 0

    def parse(self):
        """Parse map file and calculate symbol sizes."""
        with open(self.map_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        self._parse_sections(content)
        self._parse_publics(content)
        self._calculate_sizes()
        return self

    def _parse_sections(self, content):
        """Extract section table information."""
        lines = content.splitlines()
        in_section_table = False

        for line in lines:
            if line.startswith(" Start         Length     Name                   Class"):
                in_section_table = True
                continue
            if not in_section_table:
                continue
            if not line.strip() or line.startswith(" ---"):
                continue

            m = self.SECTION_LINE_RE.match(line)
            if m:
                section = m.group(1)
                start = m.group(2)
                length_str = m.group(3).rstrip('H')
                length = int(length_str, 16)
                name = m.group(4)
                key = (section, start)
                self.sections[key] = {
                    'length': length,
                    'name': name,
                    'class': m.group(5)
                }
                self.sections_by_segment[section].append({
                    'start': int(start, 16),
                    'end': int(start, 16) + length,
                    'length': length,
                    'name': name,
                    'class': m.group(5),
                })
            else:
                if line and not line[0].isspace():
                    in_section_table = False

    def _parse_publics(self, content):
        """Extract public symbols from 'Publics by Value' section."""
        marker = "Address         Publics by Value"
        if marker not in content:
            marker = "Publics by Value"
        if marker not in content:
            print("❌ ERROR: Cannot find symbol table in map file!")
            return

        start = content.find(marker)
        end_candidates = [
            pos for pos in [
                content.find("\n Static symbols", start),
                content.find("\n entry point at", start),
                content.find("\n\n entry point at", start),
            ]
            if pos != -1
        ]
        end = min(end_candidates) if end_candidates else -1
        if end == -1:
            end = len(content)

        symbol_text = content[start:end]
        lines = symbol_text.split('\n')[1:]

        for line in lines:
            line = line.strip()
            if not line or line.startswith('---'):
                continue

            m = self.SYMBOL_LINE_RE.match(line)
            if m:
                section = m.group(1)
                offset = m.group(2)
                name = m.group(3)
                rva = m.group(4) or ''
                tail = m.group(5).strip()
                obj = self._extract_object_name(tail)
                obj_name = self._short_object_name(obj)

                self.symbols.append({
                    'name': name,
                    'section': section,
                    'offset': offset,
                    'offset_int': int(offset, 16),
                    'rva': rva,
                    'obj': obj_name,
                    'obj_full': obj,
                    'line': line.strip()
                })

    @staticmethod
    def _extract_object_name(tail):
        """Return the final Lib:Object token from a MAP symbol line."""
        if not tail:
            return 'unknown'
        parts = tail.split()
        if not parts:
            return 'unknown'
        obj = parts[-1]
        if obj == '<absolute>' or '.' not in obj:
            return 'unknown'
        return obj

    @staticmethod
    def _short_object_name(obj):
        """Normalize object names while preserving archive prefixes."""
        if obj == 'unknown':
            return obj
        normalized = obj.replace('\\', '/')
        if ':' in normalized and not re.match(r'^[A-Za-z]:/', normalized):
            lib, member = normalized.split(':', 1)
            return f"{Path(lib).name}:{Path(member).name}"
        return Path(normalized).name

    @staticmethod
    def _is_project_symbol(sym):
        return is_project_object(sym.get('obj', ''), sym.get('obj_full', ''))

    def _segment_bounds(self, section):
        spans = self.sections_by_segment.get(section, [])
        if not spans:
            return None
        return min(s['start'] for s in spans), max(s['end'] for s in spans)

    def _span_for_offset(self, section, offset):
        for span in self.sections_by_segment.get(section, []):
            if span['start'] <= offset < span['end']:
                return span
        return None

    def _representative_symbol(self, aliases):
        """Pick one symbol for an address, preferring project code over runtime aliases."""
        project = [s for s in aliases if self._is_project_symbol(s)]
        return project[0] if project else aliases[0]

    def _calculate_sizes(self):
        """Calculate symbol sizes by unique address within each segment.

        MSVC MAP files often list many COMDAT/ICF aliases at the same address.
        Counting each alias independently both double-counts code and gives
        zero-sized clutter.  We group by (segment, offset), choose one readable
        representative, then assign size from the next unique address.
        """
        sec_map = defaultdict(list)
        for sym in self.symbols:
            sec_map[sym['section']].append(sym)

        for sec, symlist in sec_map.items():
            bounds = self._segment_bounds(sec)
            if not bounds:
                continue
            seg_start, seg_end = bounds

            by_offset = defaultdict(list)
            for sym in symlist:
                if seg_start <= sym['offset_int'] < seg_end:
                    by_offset[sym['offset_int']].append(sym)

            offsets = sorted(by_offset)
            for i, offset in enumerate(offsets):
                next_offset = offsets[i + 1] if i + 1 < len(offsets) else seg_end
                size = max(0, next_offset - offset)
                if size == 0:
                    continue

                aliases = by_offset[offset]
                self.alias_groups += max(0, len(aliases) - 1)
                sym = dict(self._representative_symbol(aliases))
                sym['size'] = size
                sym['size_kb'] = size / 1024.0
                sym['alias_count'] = len(aliases)
                span = self._span_for_offset(sec, offset)
                if span:
                    sym['section_name'] = span['name']
                    sym['section_class'] = span['class']
                else:
                    sym['section_name'] = ''
                    sym['section_class'] = ''
                if len(aliases) > 1:
                    sym['aliases'] = [a['name'] for a in aliases]
                self.symbols_with_size.append(sym)

        # Filter out zero-size and abnormally large (>1MB) symbols
        self.symbols_with_size = [s for s in self.symbols_with_size
                                  if 0 < s.get('size', 0) < 1024 * 1024]

    def apply_section_filter(self, section_class):
        """Restrict analysis symbols to CODE, DATA, or ALL section classes."""
        wanted = section_class.upper()
        if wanted == 'ALL':
            return self
        self.symbols_with_size = [
            s for s in self.symbols_with_size
            if s.get('section_class', '').upper() == wanted
        ]
        return self


# ============================================================================
# Symbol Classifier
# ============================================================================
class Classifier:
    """Classify symbols into categories based on name patterns."""

    @staticmethod
    def classify(symbol):
        """Return category name for the given symbol."""
        name = symbol['name']
        obj = symbol['obj']
        obj_full = symbol.get('obj_full', obj)
        obj_text = f"{obj} {obj_full}".lower()
        is_project = is_project_object(obj, obj_full)

        # Match against prioritized patterns
        for cat_name, patterns in zip(CATEGORY_NAMES, CATEGORY_PATTERNS):
            for p in patterns:
                if p.lower() in name.lower():
                    return cat_name

        # Special: module metadata (C++20 modules)
        if 'cppm.obj' in obj_text:
            return 'module_metadata'

        # Business logic vs library code
        if is_project:
            return 'code_my'
        if is_runtime_object(obj, obj_full):
            return 'code_lib'

        return 'other'


# ============================================================================
# Statistics Aggregator
# ============================================================================
class StatsAggregator:
    """Aggregate statistics by category and object file."""

    def __init__(self, symbols):
        self.symbols = symbols
        self.by_category = defaultdict(lambda: {'size': 0, 'count': 0})
        self.by_owner = defaultdict(lambda: {'size': 0, 'count': 0})
        self.by_obj = defaultdict(lambda: {'size': 0, 'count': 0})
        self.by_source = defaultdict(lambda: {'size': 0, 'count': 0})
        self.by_obj_detailed = defaultdict(list)
        self._aggregate()

    def _aggregate(self):
        """Process all symbols and build statistics."""
        for sym in self.symbols:
            cat = Classifier.classify(sym)
            size = sym['size']
            obj = sym['obj']
            owner = symbol_owner(sym)
            source = display_source(obj)

            self.by_category[cat]['size'] += size
            self.by_category[cat]['count'] += 1

            self.by_owner[owner]['size'] += size
            self.by_owner[owner]['count'] += 1

            self.by_obj[obj]['size'] += size
            self.by_obj[obj]['count'] += 1
            self.by_source[source]['size'] += size
            self.by_source[source]['count'] += 1
            self.by_obj_detailed[obj].append(sym)

    def get_category_stats(self):
        """Return statistics per category in KB."""
        return {k: {'size_kb': v['size'] / 1024, 'count': v['count']}
                for k, v in self.by_category.items()}

    def get_owner_stats(self):
        """Return coarse ownership stats in KB."""
        return {k: {'size_kb': v['size'] / 1024, 'count': v['count']}
                for k, v in self.by_owner.items()}

    def get_top_obj(self, n=15):
        """Return top N object files by size."""
        sorted_obj = sorted(self.by_obj.items(),
                            key=lambda x: x[1]['size'],
                            reverse=True)
        return sorted_obj[:n]

    def get_top_source(self, n=15, owner=None):
        """Return top N source/object names, optionally filtered by owner."""
        if owner is None:
            items = self.by_source.items()
        else:
            grouped = defaultdict(lambda: {'size': 0, 'count': 0})
            for sym in self.symbols:
                if symbol_owner(sym) != owner:
                    continue
                source = display_source(sym['obj'])
                grouped[source]['size'] += sym['size']
                grouped[source]['count'] += 1
            items = grouped.items()
        return sorted(items, key=lambda x: x[1]['size'], reverse=True)[:n]

    def get_top_symbols(self, n=25, owner=None, obj=None):
        """Return largest public symbol groups."""
        symbols = self.symbols
        if owner is not None:
            symbols = [s for s in symbols if symbol_owner(s) == owner]
        if obj is not None:
            symbols = [s for s in symbols if display_source(s['obj']) == obj or s['obj'] == obj]
        return sorted(symbols, key=lambda s: s['size'], reverse=True)[:n]


# ============================================================================
# Extension Analyzers
# ============================================================================
class ExtensionAnalyzer:
    """Deep analysis extensions."""

    @staticmethod
    def template_hotspots(symbols, threshold=2):
        """
        Find frequently instantiated templates across object files.

        Templates with same simplified signature are considered duplicates
        that could be unified with explicit instantiation.
        """
        template_stats = defaultdict(lambda: {
            'count': 0,
            'objs': set(),
            'sizes': [],
            'example': ''
        })

        for sym in symbols:
            name = sym['name']
            if '?$' in name or '<' in name:
                # Normalize type parameters to detect duplicates
                simplified = re.sub(r'\?[A-Z0-9]+@', '?T@', name)
                simplified = re.sub(r'\$[0-9A-F]+', '', simplified)
                key = simplified[:120]

                template_stats[key]['count'] += 1
                template_stats[key]['objs'].add(sym['obj'])
                template_stats[key]['sizes'].append(sym['size'])
                if not template_stats[key]['example']:
                    template_stats[key]['example'] = name

        # Sort by repetition count
        hot = sorted(template_stats.items(),
                     key=lambda x: x[1]['count'],
                     reverse=True)

        print("\n🔥 TEMPLATE INSTANTIATION HOTSPOTS (repeat >= {})".format(threshold))
        print("-" * 80)
        total_saved = 0

        for i, (key, stat) in enumerate(hot[:20]):
            if stat['count'] >= threshold:
                avg_size = sum(stat['sizes']) / len(stat['sizes']) / 1024
                total_size = sum(stat['sizes']) / 1024
                potential_save = (stat['count'] - 1) * (sum(stat['sizes']) / len(stat['sizes']))
                total_saved += potential_save

                print(f"{i+1:2}. Repeated {stat['count']:3} times | "
                      f"Total {total_size:7.2f}KB | Avg {avg_size:5.2f}KB")

                obj_list = list(stat['objs'])[:5]
                print(f"      Objects: {', '.join(obj_list)}{'...' if len(stat['objs']) > 5 else ''}")

        print(f"\n    💡 Potential saving with explicit instantiation: {total_saved/1024:.2f}KB")

    @staticmethod
    def estimate_icf_savings(symbols):
        """
        Estimate potential size reduction with /OPT:ICF (COMDAT folding).

        Symbols with same section, size, and normalized name are considered
        identical and could be folded by the linker.
        """
        groups = defaultdict(list)

        for sym in symbols:
            if sym['size'] == 0:
                continue

            # Remove address offsets to detect identical functions
            simple_name = re.sub(r'[0-9A-F]{8,16}', '', sym['name'])
            simple_name = re.sub(r'\?\?_[0-9]', '', simple_name)
            key = (sym['section'], sym['size'], simple_name[:50])
            groups[key].append(sym)

        saved = 0
        collapsed = 0

        print("\n🗜️  COMDAT FOLDING ESTIMATE (/OPT:ICF)")
        print("-" * 80)

        for key, symlist in groups.items():
            if len(symlist) > 1:
                repeat = len(symlist) - 1
                size = symlist[0]['size']
                saved += repeat * size
                collapsed += repeat
                print(f"  Fold {repeat:2} instances, save {repeat * size / 1024:7.2f}KB  {symlist[0]['name'][:60]}")

        print(f"\n    💡 Estimated saving: {saved / 1024:.2f}KB ({collapsed} symbols)")

    @staticmethod
    def stl_per_obj(stats):
        """
        Attribute STL instantiation costs to specific object files.

        Shows which business logic modules are pulling in STL code,
        helping prioritize optimization efforts.
        """
        stl_cats = {'stl_string', 'stl_vector', 'stl_other', 'unordered_map', 'std_function'}
        per_obj = defaultdict(float)

        for obj, symlist in stats.by_obj_detailed.items():
            for sym in symlist:
                cat = Classifier.classify(sym)
                if cat in stl_cats:
                    per_obj[obj] += sym['size']

        sorted_obj = sorted(per_obj.items(), key=lambda x: x[1], reverse=True)

        print("\n📎 STL INSTANTIATION BY OBJECT FILE")
        print("-" * 80)

        for obj, sz in sorted_obj[:15]:
            print(f"  {sz/1024:7.2f}KB  {obj}")

        total_stl = sum(per_obj.values()) / 1024
        print(f"\n    💡 Total STL overhead from business logic: {total_stl:.2f}KB")
        return per_obj

    @staticmethod
    def export_html_report(parser, stats, args, output_path):
        """
        Generate standalone HTML report with interactive charts.

        Uses ECharts CDN, no additional dependencies required.
        Includes category pie chart, object file bar chart, and template hotspots.
        """
        cat_stats = stats.get_category_stats()
        total_kb = sum(s['size_kb'] for s in cat_stats.values())

        cat_data = [{'name': k, 'value': round(v['size_kb'], 2)}
                    for k, v in cat_stats.items()]
        obj_data = [{'name': o[:30], 'value': round(st['size']/1024, 2)}
                    for o, st in stats.get_top_obj(20)]

        # Get template hotspots for HTML report
        template_hotspots = []
        hotspots_raw = ExtensionAnalyzer._get_template_hotspots_raw(stats.symbols, top=10)
        template_hotspots = [{
            'name': h['name'][:60],
            'count': h['count'],
            'total_kb': round(h['total_kb'], 2)
        } for h in hotspots_raw]

        html_content = f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>MSVC Map Analysis - {parser.map_path.name}</title>
    <script src="https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js"></script>
    <style>
        body {{ font-family: 'Segoe UI', Arial, sans-serif; margin: 20px; background: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: auto; background: white; padding: 20px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }}
        h1 {{ color: #333; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }}
        h2 {{ color: #555; margin-top: 30px; }}
        .summary {{ background: #e8f5e9; padding: 15px; border-radius: 5px; margin: 20px 0; }}
        .chart-container {{ display: flex; flex-wrap: wrap; justify-content: space-around; }}
        .chart-box {{ width: 45%; min-width: 400px; height: 400px; margin-bottom: 30px; }}
        table {{ border-collapse: collapse; width: 100%; margin-top: 20px; }}
        th, td {{ border: 1px solid #ddd; padding: 8px; text-align: left; }}
        th {{ background-color: #4CAF50; color: white; }}
        tr:nth-child(even) {{ background-color: #f2f2f2; }}
        .footer {{ text-align: right; color: #666; margin-top: 30px; font-style: italic; }}
    </style>
</head>
<body>
<div class="container">
    <h1>📊 MSVC Map Analysis Report - {parser.map_path.name}</h1>
    
    <div class="summary">
        <strong>Total Code Size:</strong> {total_kb:.2f}KB &nbsp;|&nbsp;
        <strong>Symbol Count:</strong> {len(parser.symbols_with_size)} &nbsp;|&nbsp;
        <strong>Analysis Time:</strong> {__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
    </div>
    
    <div class="chart-container">
        <div id="pieChart" class="chart-box"></div>
        <div id="barChart" class="chart-box"></div>
    </div>
    
    <h2>📌 Template Hotspots (Top 10)</h2>
    <table>
        <thead>
            <tr>
                <th>Template Signature</th>
                <th>Repeat Count</th>
                <th>Total Size (KB)</th>
            </tr>
        </thead>
        <tbody>
            {''.join(f"<tr><td>{h['name']}</td><td>{h['count']}</td><td>{h['total_kb']}</td></tr>"
                     for h in template_hotspots)}
        </tbody>
    </table>
    
    <h2>📦 Top 20 Object Files</h2>
    <table>
        <thead>
            <tr>
                <th>Object File</th>
                <th>Size (KB)</th>
            </tr>
        </thead>
        <tbody>
            {''.join(f"<tr><td>{d['name']}</td><td>{d['value']}</td></tr>" for d in obj_data)}
        </tbody>
    </table>
    
    <div class="footer">
        Generated by MSVC Map Profiler - Ultimate Edition
    </div>
</div>

<script>
    // Pie chart
    var pieChart = echarts.init(document.getElementById('pieChart'));
    var pieOption = {{
        title: {{ text: 'Size by Category', left: 'center' }},
        tooltip: {{ trigger: 'item' }},
        series: [
            {{
                name: 'Size (KB)',
                type: 'pie',
                radius: '50%',
                data: {json.dumps(cat_data)},
                emphasis: {{
                    itemStyle: {{
                        shadowBlur: 10,
                        shadowOffsetX: 0,
                        shadowColor: 'rgba(0,0,0,0.5)'
                    }}
                }}
            }}
        ]
    }};
    pieChart.setOption(pieOption);
    
    // Bar chart
    var barChart = echarts.init(document.getElementById('barChart'));
    var barOption = {{
        title: {{ text: 'Top 20 Object Files', left: 'center' }},
        tooltip: {{ trigger: 'axis' }},
        xAxis: {{
            type: 'category',
            data: {json.dumps([d['name'] for d in obj_data])},
            axisLabel: {{ rotate: 45 }}
        }},
        yAxis: {{
            type: 'value',
            name: 'Size (KB)'
        }},
        series: [{{
            data: {json.dumps([d['value'] for d in obj_data])},
            type: 'bar',
            itemStyle: {{ color: '#4CAF50' }}
        }}]
    }};
    barChart.setOption(barOption);
</script>
</body>
</html>"""

        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(html_content)
        print(f"✅ HTML report generated: {output_path}")

    @staticmethod
    def _get_template_hotspots_raw(symbols, top=10):
        """Internal helper to get raw template hotspot data."""
        template_stats = defaultdict(lambda: {'count': 0, 'sizes': [], 'name': ''})

        for sym in symbols:
            name = sym['name']
            if '?$' in name or '<' in name:
                simplified = re.sub(r'\?[A-Z0-9]+@', '?T@', name)
                simplified = re.sub(r'\$[0-9A-F]+', '', simplified)
                key = simplified[:120]
                template_stats[key]['count'] += 1
                template_stats[key]['sizes'].append(sym['size'])
                if not template_stats[key]['name']:
                    template_stats[key]['name'] = name

        hot = sorted(template_stats.items(),
                     key=lambda x: x[1]['count'],
                     reverse=True)[:top]

        result = []
        for key, stat in hot:
            result.append({
                'name': stat['name'][:80],
                'count': stat['count'],
                'total_kb': sum(stat['sizes']) / 1024
            })
        return result


# ============================================================================
# MSVC symbol demangling
# ============================================================================
class Demangler:
    """Best-effort wrapper around MSVC undname.exe."""

    def __init__(self, enabled=False, undname_path=None):
        self.enabled = enabled
        self.undname_path = undname_path or shutil.which("undname.exe")
        self.cache = {}

    def available(self):
        return bool(self.enabled and self.undname_path)

    @staticmethod
    def _undname_input(name):
        # CMake/MSVC modules may append pseudo-scope tags such as ::<!core>.
        # undname.exe only understands the decorated symbol before that suffix.
        return name.split("::<!", 1)[0]

    def many(self, names):
        if not self.available():
            return {}

        requested = [n for n in dict.fromkeys(names) if n]
        normalized = {n: self._undname_input(n) for n in requested}
        pending = [n for n in requested if n not in self.cache]
        pending_inputs = list(dict.fromkeys(normalized[n] for n in pending))
        resolved_inputs = {}

        for i in range(0, len(pending_inputs), 80):
            chunk = pending_inputs[i:i + 80]
            try:
                result = subprocess.run(
                    [self.undname_path, *chunk],
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
            except OSError:
                self.enabled = False
                break

            current = None
            for line in result.stdout.splitlines():
                m = re.match(r'^Undecoration of :- "(.*)"', line)
                if m:
                    current = m.group(1)
                    continue
                m = re.match(r'^is :- "(.*)"', line)
                if m and current:
                    resolved_inputs[current] = m.group(1)
                    current = None

        for original in pending:
            clean = normalized[original]
            self.cache[original] = resolved_inputs.get(clean, original)

        return {n: self.cache.get(n, n) for n in names}

    def one(self, name):
        return self.many([name]).get(name, name)


# ============================================================================
# Reporter
# ============================================================================
class Reporter:
    """Generate console, CSV, JSON, and chart reports."""

    def __init__(self, parser, stats, args):
        self.parser = parser
        self.stats = stats
        self.args = args
        self.demangler = Demangler(
            enabled=getattr(args, 'demangle', False),
            undname_path=getattr(args, 'undname', None),
        )

    def _symbol_name(self, sym):
        return self.demangler.one(sym['name']) if self.demangler.available() else sym['name']

    def _project_symbols_with_names(self):
        symbols = [s for s in self.parser.symbols_with_size
                   if symbol_owner(s) == 'project']
        if self.demangler.available():
            self.demangler.many([s['name'] for s in symbols])
        return [(s, self._symbol_name(s) if self.demangler.available() else '')
                for s in symbols]

    def _top_symbol_groups(self, n):
        grouped = defaultdict(lambda: {'size': 0, 'count': 0})
        for sym, readable_name in self._project_symbols_with_names():
            group = infer_symbol_group(sym, readable_name)
            grouped[group]['size'] += sym['size']
            grouped[group]['count'] += 1
        return sorted(grouped.items(), key=lambda x: x[1]['size'],
                      reverse=True)[:n]

    def print_console(self):
        """Print colored console report with ASCII progress bars."""
        if HAS_COLORAMA and not self.args.no_color:
            red = Fore.RED
            green = Fore.GREEN
            yellow = Fore.YELLOW
            cyan = Fore.CYAN
            reset = Style.RESET_ALL
        else:
            red = green = yellow = cyan = reset = ''

        print(f"\n{cyan}🔍 ANALYSIS REPORT: {self.parser.map_path}{reset}")
        print("=" * 80)
        print(f"  Section class     : {self.args.section_class}")

        # Category statistics
        cat_stats = self.stats.get_category_stats()
        total_kb = sum(s['size_kb'] for s in cat_stats.values())
        sorted_cat = sorted(cat_stats.items(),
                            key=lambda x: x[1]['size_kb'],
                            reverse=True)

        print(f"\n{cyan}📊 CATEGORY SIZE RANKING{reset}")
        print("-" * 80)

        for cat, stat in sorted_cat:
            pct = (stat['size_kb'] / total_kb * 100) if total_kb else 0
            bar_len = int(stat['size_kb'] / total_kb * 50) if total_kb else 0
            bar = '█' * bar_len + '░' * (50 - bar_len)
            size_str = f"{stat['size_kb']:>8.2f}KB"
            print(f"{bar} {size_str} {pct:5.1f}%  {cat:20} ({stat['count']} symbols)")

        # Owner statistics
        owner_stats = self.stats.get_owner_stats()
        print(f"\n{cyan}🧭 OWNERSHIP SIZE RANKING{reset}")
        print("-" * 80)
        for owner, stat in sorted(owner_stats.items(), key=lambda x: x[1]['size_kb'], reverse=True):
            pct = (stat['size_kb'] / total_kb * 100) if total_kb else 0
            print(f"  {owner:10}: {stat['size_kb']:>8.2f}KB  {pct:5.1f}%  ({stat['count']} symbols)")

        # High-risk categories
        print(f"\n{red}🔥 HIGH-RISK OPTIMIZATION TARGETS{reset}")
        print("-" * 80)
        targets = ['std_function', 'unordered_map', 'stl_string', 'stl_vector', 'stl_other']
        for t in targets:
            if t in cat_stats:
                kb = cat_stats[t]['size_kb']
                if kb > 5:
                    print(f"  {t:20}: {kb:>8.2f}KB  ✅ Immediate action recommended")

        # Project code
        my_code = owner_stats.get('project', {}).get('size_kb', 0)
        print(f"\n{green}💻 BUSINESS LOGIC{reset}")
        print("-" * 80)
        print(f"  Total business code: {my_code:.2f}KB  ({my_code/total_kb*100:.1f}%)")

        # Top project source/object files
        print(f"\n{yellow}📦 TOP PROJECT SOURCE OBJECTS{reset}")
        print("-" * 80)
        top_source = self.stats.get_top_source(self.args.top_objects, owner='project')
        for i, (obj, st) in enumerate(top_source, 1):
            size_kb = st['size'] / 1024
            print(f"{i:2}. {size_kb:>8.2f}KB  {obj}")

        # Unity build command/module attribution
        print(f"\n{yellow}🧩 TOP PROJECT SYMBOL GROUPS{reset}")
        print("-" * 80)
        for i, (group, st) in enumerate(self._top_symbol_groups(self.args.top_groups), 1):
            size_kb = st['size'] / 1024
            print(f"{i:2}. {size_kb:>8.2f}KB  {group:28} ({st['count']} symbols)")

        # Top object files including runtime
        print(f"\n{yellow}📦 TOP OBJECT FILES (ALL){reset}")
        print("-" * 80)
        top_obj = self.stats.get_top_obj(self.args.top_objects)
        for i, (obj, st) in enumerate(top_obj, 1):
            size_kb = st['size'] / 1024
            print(f"{i:2}. {size_kb:>8.2f}KB  {obj}")

        # Top project functions/symbol groups
        print(f"\n{yellow}🔧 TOP PROJECT FUNCTIONS / SYMBOL GROUPS{reset}")
        print("-" * 80)
        top_symbols = self.stats.get_top_symbols(self.args.top_symbols, owner='project')
        if self.demangler.available():
            self.demangler.many([sym['name'] for sym in top_symbols])
        for i, sym in enumerate(top_symbols, 1):
            size_kb = sym['size'] / 1024
            aliases = f" aliases={sym.get('alias_count', 1)}" if sym.get('alias_count', 1) > 1 else ""
            print(f"{i:2}. {size_kb:>8.2f}KB  {display_source(sym['obj']):24} {self._symbol_name(sym)}{aliases}")

        # Overall statistics
        print(f"\n{cyan}📈 OVERALL STATISTICS{reset}")
        print("-" * 80)
        print(f"  Symbols parsed    : {len(self.parser.symbols_with_size)}")
        print(f"  Folded aliases    : {self.parser.alias_groups}")
        print(f"  Total code size   : {total_kb:.2f}KB")
        avg_kb = total_kb / len(self.parser.symbols_with_size) if self.parser.symbols_with_size else 0
        print(f"  Average symbol size: {avg_kb:.2f}KB")

        # Extension analyses
        if not self.args.skip_template:
            ExtensionAnalyzer.template_hotspots(self.parser.symbols_with_size)
        if not self.args.skip_icf:
            ExtensionAnalyzer.estimate_icf_savings(self.parser.symbols_with_size)
        if not self.args.skip_stl_per_obj:
            ExtensionAnalyzer.stl_per_obj(self.stats)

        # Action items summary
        print(f"\n{green}💡 ACTION ITEMS{reset}")
        print("-" * 80)
        if 'std_function' in cat_stats and cat_stats['std_function']['size_kb'] > 10:
            print(f"  • Remove std::function: save {cat_stats['std_function']['size_kb']:.1f}KB → "
                  f"Use function pointers or static dispatch")
        if 'unordered_map' in cat_stats and cat_stats['unordered_map']['size_kb'] > 10:
            print(f"  • Remove unordered_map: save {cat_stats['unordered_map']['size_kb']:.1f}KB → "
                  f"Replace with std::map or linear search")
        if 'stl_string' in cat_stats and cat_stats['stl_string']['size_kb'] > 50:
            print(f"  • STL string overhead: {cat_stats['stl_string']['size_kb']:.1f}KB → "
                  f"Add explicit instantiation, use string_view")
        if not self.args.skip_icf:
            print(f"  • Enable /OPT:ICF linker option: zero-cost saving")
        print()

    def export_csv(self, output_path):
        """Export all symbols to CSV file."""
        with open(output_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow([
                'Name', 'Demangled', 'Size(KB)', 'Category', 'Owner',
                'Object', 'Source', 'AliasCount', 'Section', 'SectionName',
                'SectionClass', 'Offset', 'RVA'
            ])

            for sym in self.parser.symbols_with_size:
                cat = Classifier.classify(sym)
                writer.writerow([
                    sym['name'],
                    self._symbol_name(sym) if self.demangler.available() else '',
                    round(sym['size_kb'], 3),
                    cat,
                    symbol_owner(sym),
                    sym['obj'],
                    display_source(sym['obj']),
                    sym.get('alias_count', 1),
                    sym['section'],
                    sym.get('section_name', ''),
                    sym.get('section_class', ''),
                    sym['offset'],
                    sym['rva']
                ])

        print(f"✅ CSV report saved: {output_path}")

    def export_json(self, output_path):
        """Export JSON summary with optional full symbol list."""
        cat_stats = self.stats.get_category_stats()
        total_kb = sum(s['size_kb'] for s in cat_stats.values())

        summary = {
            'map_file': str(self.parser.map_path),
            'total_symbols': len(self.parser.symbols_with_size),
            'total_size_kb': round(total_kb, 2),
            'folded_aliases': self.parser.alias_groups,
            'category': {},
            'owner': {},
            'top_obj': []
        }

        for cat, stat in cat_stats.items():
            summary['category'][cat] = {
                'size_kb': round(stat['size_kb'], 2),
                'count': stat['count'],
                'percentage': round(stat['size_kb'] / total_kb * 100, 1) if total_kb else 0
            }

        for owner, stat in self.stats.get_owner_stats().items():
            summary['owner'][owner] = {
                'size_kb': round(stat['size_kb'], 2),
                'count': stat['count'],
                'percentage': round(stat['size_kb'] / total_kb * 100, 1) if total_kb else 0
            }

        for obj, st in self.stats.get_top_obj(20):
            summary['top_obj'].append({
                'object': obj,
                'source': display_source(obj),
                'size_kb': round(st['size'] / 1024, 2),
                'count': st['count']
            })

        summary['top_project_groups'] = []
        for group, st in self._top_symbol_groups(self.args.top_groups):
            summary['top_project_groups'].append({
                'group': group,
                'size_kb': round(st['size'] / 1024, 2),
                'count': st['count']
            })

        summary['top_project_symbols'] = []
        for sym in self.stats.get_top_symbols(self.args.top_symbols, owner='project'):
            summary['top_project_symbols'].append({
                'name': sym['name'],
                'demangled': self._symbol_name(sym) if self.demangler.available() else '',
                'size_kb': round(sym['size_kb'], 3),
                'object': sym['obj'],
                'source': display_source(sym['obj']),
                'alias_count': sym.get('alias_count', 1),
                'section_name': sym.get('section_name', ''),
                'section_class': sym.get('section_class', ''),
            })

        if self.args.full_json:
            summary['symbols'] = []
            for sym in self.parser.symbols_with_size:
                summary['symbols'].append({
                    'name': sym['name'],
                    'demangled': self._symbol_name(sym) if self.demangler.available() else '',
                    'size_kb': round(sym['size_kb'], 3),
                    'category': Classifier.classify(sym),
                    'owner': symbol_owner(sym),
                    'object': sym['obj'],
                    'source': display_source(sym['obj']),
                    'alias_count': sym.get('alias_count', 1),
                    'section': sym['section'],
                    'section_name': sym.get('section_name', ''),
                    'section_class': sym.get('section_class', ''),
                    'offset': sym['offset'],
                    'rva': sym['rva']
                })

        with open(output_path, 'w', encoding='utf-8') as f:
            json.dump(summary, f, indent=2, ensure_ascii=False)

        print(f"✅ JSON report saved: {output_path}")

    def generate_charts(self, output_prefix):
        """Generate matplotlib charts (pie chart and bar chart)."""
        if not HAS_MATPLOTLIB:
            print("⚠️ matplotlib not installed, skipping charts")
            return

        cat_stats = self.stats.get_category_stats()
        total_kb = sum(s['size_kb'] for s in cat_stats.values())

        # Prepare data for pie chart
        labels, sizes = [], []
        for cat, stat in cat_stats.items():
            pct = stat['size_kb'] / total_kb * 100
            if pct >= 1.0:
                labels.append(f"{cat}\n({stat['size_kb']:.1f}KB)")
                sizes.append(stat['size_kb'])
            else:
                if 'Others' not in labels:
                    labels.append('Others')
                    sizes.append(stat['size_kb'])
                else:
                    idx = labels.index('Others')
                    sizes[idx] += stat['size_kb']

        # Pie chart
        plt.figure(figsize=(10, 6))
        plt.pie(sizes, labels=labels, autopct='%1.1f%%', startangle=90)
        plt.title(f"Code Size by Category - {self.parser.map_path.name}")
        plt.axis('equal')
        pie_path = f"{output_prefix}_pie.png"
        plt.savefig(pie_path, dpi=120)
        plt.close()
        print(f"✅ Pie chart saved: {pie_path}")

        # Bar chart - Top 15 object files
        top_obj = self.stats.get_top_obj(15)
        obj_names = [os.path.basename(o[0])[:30] for o in top_obj]
        obj_sizes = [o[1]['size'] / 1024 for o in top_obj]

        plt.figure(figsize=(12, 6))
        plt.barh(range(len(obj_names)), obj_sizes, color='steelblue')
        plt.yticks(range(len(obj_names)), obj_names)
        plt.xlabel('Size (KB)')
        plt.title(f"Top 15 Object Files - {self.parser.map_path.name}")
        plt.gca().invert_yaxis()

        bar_path = f"{output_prefix}_top_obj.png"
        plt.tight_layout()
        plt.savefig(bar_path, dpi=120)
        plt.close()
        print(f"✅ Bar chart saved: {bar_path}")


# ============================================================================
# Multi-file comparison
# ============================================================================
def compare_maps(map_files, args):
    """Compare multiple map files side by side."""
    if len(map_files) < 2:
        return

    print("\n📊 MULTI-FILE COMPARISON")
    print("=" * 80)

    results = []
    for mf in map_files:
        parser = MapParser(mf).parse().apply_section_filter(args.section_class)
        stats = StatsAggregator(parser.symbols_with_size)
        cat_stats = stats.get_category_stats()
        total_kb = sum(s['size_kb'] for s in cat_stats.values())
        results.append({
            'file': Path(mf).name,
            'total_kb': total_kb,
            'cat': cat_stats
        })

    # Print header
    header = f"{'Category':30} " + " ".join([f"{r['file']:>12}" for r in results])
    print(header)
    print("-" * 80)

    # Print category rows
    all_cats = set()
    for r in results:
        all_cats.update(r['cat'].keys())

    for cat in sorted(all_cats):
        row = f"{cat:30} "
        for r in results:
            kb = r['cat'].get(cat, {}).get('size_kb', 0)
            row += f"{kb:>12.2f} "
        print(row)

    # Delta analysis
    print("\nDELTA ANALYSIS:")
    base = results[0]
    for other in results[1:]:
        diff = other['total_kb'] - base['total_kb']
        print(f"{other['file']} vs {base['file']}: {'+' if diff > 0 else ''}{diff:.2f}KB")

        for cat in all_cats:
            diff_cat = other['cat'].get(cat, {}).get('size_kb', 0) - \
                       base['cat'].get(cat, {}).get('size_kb', 0)
            if abs(diff_cat) > 10:
                print(f"    {cat}: {'+' if diff_cat > 0 else ''}{diff_cat:.2f}KB")


# ============================================================================
# Main entry point
# ============================================================================
def main():
    parser = argparse.ArgumentParser(
        description="MSVC Map File Profiler - Ultimate Edition",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic analysis with all extensions
  python map_profiler.py release/app.map
  
  # Generate HTML report
  python map_profiler.py app.map --html report.html
  
  # Export CSV and JSON
  python map_profiler.py app.map --csv symbols.csv --json summary.json

  # Demangle MSVC names and show more function hotspots
  python map_profiler.py app.map --demangle --top-symbols 40
  
  # Compare two versions
  python map_profiler.py old.map new.map
  
  # Skip expensive extensions for quick analysis
  python map_profiler.py app.map --skip-template --skip-icf
        """
    )

    parser.add_argument('map_files', nargs='+', help='One or more .map files to analyze')
    parser.add_argument('--csv', help='Export all symbols to CSV file')
    parser.add_argument('--json', help='Export JSON summary')
    parser.add_argument('--full-json', action='store_true',
                        help='Include full symbol list in JSON export')
    parser.add_argument('--chart', help='Prefix for chart files (e.g., "output/chart")')
    parser.add_argument('--html', help='Generate interactive HTML report')
    parser.add_argument('--threshold', type=float, default=1.0,
                        help='Symbol size threshold in KB (default: 1.0)')
    parser.add_argument('--section-class', choices=['CODE', 'DATA', 'ALL'],
                        default='CODE',
                        help='Section class to analyze (default: CODE)')
    parser.add_argument('--top-objects', type=int, default=15,
                        help='Number of object/source rows to print (default: 15)')
    parser.add_argument('--top-groups', type=int, default=20,
                        help='Number of inferred command/module groups to print (default: 20)')
    parser.add_argument('--top-symbols', type=int, default=25,
                        help='Number of large symbols/functions to print (default: 25)')
    parser.add_argument('--demangle', action='store_true',
                        help='Use MSVC undname.exe to print readable C++ names')
    parser.add_argument('--undname',
                        help='Path to undname.exe; defaults to PATH lookup')
    parser.add_argument('--no-color', action='store_true',
                        help='Disable colored output')
    parser.add_argument('--skip-template', action='store_true',
                        help='Skip template hotspot analysis')
    parser.add_argument('--skip-icf', action='store_true',
                        help='Skip COMDAT folding estimation')
    parser.add_argument('--skip-stl-per-obj', action='store_true',
                        help='Skip STL attribution analysis')

    args = parser.parse_args()

    # Single file analysis
    if len(args.map_files) == 1:
        map_file = args.map_files[0]
        print(f"\n🔍 Analyzing: {map_file}")

        map_parser = MapParser(map_file).parse().apply_section_filter(args.section_class)
        stats = StatsAggregator(map_parser.symbols_with_size)
        reporter = Reporter(map_parser, stats, args)

        reporter.print_console()

        if args.csv:
            reporter.export_csv(args.csv)
        if args.json:
            reporter.export_json(args.json)
        if args.chart:
            reporter.generate_charts(args.chart)
        if args.html:
            ExtensionAnalyzer.export_html_report(map_parser, stats, args, args.html)

    # Multi-file comparison
    else:
        compare_maps(args.map_files, args)


if __name__ == '__main__':
    main()
