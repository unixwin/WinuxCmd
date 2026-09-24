#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
license_header.py - Automatically insert/update copyright headers
Optimized version: Batch Git queries for significant speed improvement
"""

import os
import sys
import subprocess
import threading
from datetime import datetime
import re
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import time

# ===== Configuration =====
DEBUG = False
USE_GIT_HISTORY = True
THREAD_COUNT = 8
DEFAULT_LICT_PATH = "TemplateLicense.lict"
DEFAULT_EXCLUDE_DIRS = {
    "third_party",
    "build",
    "bin",
    "obj",
    ".git",
    ".svn",
    ".hg",
    ".bzr",
}
SOURCE_EXTS = {".h", ".hpp", ".c", ".cpp", ".cc" ,".cppm"}
# ===== End Configuration =====


class PerformanceStats:
    def __init__(self):
        self.timings = defaultdict(float)
        self.counts = defaultdict(int)
        self.lock = threading.Lock()

    def add_time(self, category, duration):
        with self.lock:
            self.timings[category] += duration
            self.counts[category] += 1

    def print_stats(self):
        print("\n" + "=" * 60)
        print("Performance Statistics:")
        print("=" * 60)

        total_time = sum(self.timings.values())
        print(f"Total processing time: {total_time:.2f} seconds")

        for category in sorted(self.timings.keys()):
            avg = self.timings[category] / max(self.counts[category], 1)
            percentage = (
                (self.timings[category] / total_time) * 100 if total_time > 0 else 0
            )
            print(
                f"  {category:20s}: {self.timings[category]:8.2f}s "
                f"(avg: {avg:.4f}s, share: {percentage:5.1f}%)"
            )
        print("=" * 60)


stats = PerformanceStats()


class GitTimeCache:
    """Git time cache with batch queries"""

    def __init__(self):
        self.git_root = None
        self.first_commit_cache = {}
        self.last_commit_cache = {}
        self.cache_lock = threading.Lock()
        self.init_git_root()

    def init_git_root(self):
        """Initialize Git repository root"""
        start_time = time.time()
        try:
            cmd = ["git", "rev-parse", "--show-toplevel"]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=2)
            if result.returncode == 0:
                self.git_root = result.stdout.strip()
                if DEBUG:
                    print(f"[DEBUG] Git repository root: {self.git_root}")
                stats.add_time("git_init", time.time() - start_time)
                return True
        except Exception as e:
            if DEBUG:
                print(f"[DEBUG] Git initialization failed: {e}")
        stats.add_time("git_init", time.time() - start_time)
        return False

    def batch_query_git_times(self, file_paths):
        """Batch query Git times for all files"""
        start_time = time.time()

        if not self.git_root or not file_paths:
            stats.add_time("git_batch_query", time.time() - start_time)
            return

        try:
            rel_paths = []
            file_map = {}  # relative path -> original path mapping

            for file_path in file_paths:
                try:
                    abs_path = os.path.abspath(file_path)
                    rel_path = os.path.relpath(abs_path, self.git_root)
                    rel_paths.append(rel_path)
                    file_map[rel_path] = file_path
                except ValueError:
                    continue

            if not rel_paths:
                stats.add_time("git_batch_query", time.time() - start_time)
                return

            first_times = self._get_first_commit_times_batch(rel_paths, file_map)
            last_times = self._get_last_commit_times_batch(rel_paths, file_map)

            with self.cache_lock:
                for rel_path, file_path in file_map.items():
                    if rel_path in first_times:
                        self.first_commit_cache[file_path] = first_times[rel_path]
                    if rel_path in last_times:
                        self.last_commit_cache[file_path] = last_times[rel_path]

            if DEBUG:
                print(f"[DEBUG] Batch queried Git times for {len(rel_paths)} files")

        except Exception as e:
            if DEBUG:
                print(f"[DEBUG] Batch Git query failed: {e}")

        stats.add_time("git_batch_query", time.time() - start_time)

    def _get_first_commit_times_batch(self, rel_paths, file_map):
        """Batch get first commit times"""
        first_times = {}

        try:
            for rel_path, file_path in file_map.items():
                cmd = ["git", "log", "--reverse", "--format=%at", "--", rel_path]
                result = subprocess.run(
                    cmd, cwd=self.git_root, capture_output=True, text=True, timeout=1
                )
                if result.returncode == 0 and result.stdout.strip():
                    timestamps = result.stdout.strip().split("\n")
                    if timestamps and timestamps[0]:
                        first_times[rel_path] = datetime.fromtimestamp(
                            int(timestamps[0])
                        )
        except:
            pass

        return first_times

    def _get_last_commit_times_batch(self, rel_paths, file_map):
        """Batch get last commit times"""
        last_times = {}

        try:
            cmd = ["git", "log", "-1", "--pretty=format:%H%x00%at%x00", "--"]
            cmd.extend(rel_paths)

            result = subprocess.run(
                cmd, cwd=self.git_root, capture_output=True, text=True, timeout=5
            )

            if result.returncode == 0 and result.stdout.strip():
                lines = result.stdout.strip().split("\n")
                for line in lines:
                    if "\x00" in line:
                        parts = line.split("\x00")
                        if len(parts) >= 3:
                            rel_path = parts[2]
                            timestamp = parts[1]
                            if rel_path in file_map and timestamp:
                                last_times[rel_path] = datetime.fromtimestamp(
                                    int(timestamp)
                                )
        except:
            for rel_path, file_path in file_map.items():
                try:
                    cmd = ["git", "log", "-1", "--format=%at", "--", rel_path]
                    result = subprocess.run(
                        cmd,
                        cwd=self.git_root,
                        capture_output=True,
                        text=True,
                        timeout=1,
                    )
                    if result.returncode == 0 and result.stdout.strip():
                        last_times[rel_path] = datetime.fromtimestamp(
                            int(result.stdout.strip())
                        )
                except:
                    pass

        return last_times


git_cache = GitTimeCache() if USE_GIT_HISTORY else None


def get_file_times_from_cache(file_path):
    """Get file times from cache"""
    start_time = time.time()

    if git_cache:
        with git_cache.cache_lock:
            first_time = git_cache.first_commit_cache.get(file_path)
            last_time = git_cache.last_commit_cache.get(file_path)

        if first_time and last_time:
            stats.add_time("get_file_times", time.time() - start_time)
            return first_time, last_time

    fs_create = None
    fs_modify = None

    try:
        create_timestamp = os.path.getctime(file_path)
        fs_create = datetime.fromtimestamp(create_timestamp)

        modify_timestamp = os.path.getmtime(file_path)
        fs_modify = datetime.fromtimestamp(modify_timestamp)
    except Exception as e:
        if DEBUG:
            print(f"[DEBUG] Failed to get file times {file_path}: {e}")

    stats.add_time("get_file_times", time.time() - start_time)
    return fs_create or datetime.now(), fs_modify or datetime.now()


def get_copyright_year_fast(file_path):
    """Quickly generate copyright year"""
    start_time = time.time()

    create_time, modify_time = get_file_times_from_cache(file_path)

    create_year = create_time.year
    modify_year = modify_time.year
    current_year = datetime.now().year

    if create_year > current_year:
        if modify_year <= current_year:
            result = str(modify_year)
        else:
            result = str(current_year)
    elif create_year == modify_year:
        result = str(create_year)
    else:
        if 2000 <= create_year <= current_year and 2000 <= modify_year <= current_year:
            result = f"{create_year}-{modify_year}"
        else:
            reasonable_year = (
                create_year if 2000 <= create_year <= current_year else modify_year
            )
            if not (2000 <= reasonable_year <= current_year):
                reasonable_year = current_year
            result = str(reasonable_year)

    stats.add_time("get_copyright_year", time.time() - start_time)
    return result


def get_username():
    """Get current username"""
    return os.environ.get(
        "USERNAME" if sys.platform.startswith("win") else "USER", "Unknown"
    )


def read_lict_file(lict_path):
    """Read template file"""
    if not os.path.exists(lict_path):
        print(f"Error: Template file {lict_path} does not exist!")
        sys.exit(1)
    with open(lict_path, "r", encoding="utf-8") as f:
        return f.read()


def should_skip_copyright_update_fast(content, new_copyright_year):
    """Quickly check if copyright update is needed"""
    start_time = time.time()

    lines = content.split("\n")
    for line in lines[:20]:
        if "Copyright" in line and ("©" in line or "Copyright" in line):
            year_match = re.search(r"(\d{4})(?:-(\d{4}))?", line)
            if year_match:
                existing_start = year_match.group(1)
                existing_end = year_match.group(2) or existing_start

                try:
                    start_year = int(existing_start)
                    end_year = int(existing_end)

                    if "-" in new_copyright_year:
                        new_start, new_end = map(int, new_copyright_year.split("-"))
                        new_year = new_end
                    else:
                        new_year = int(new_copyright_year)

                    if start_year <= new_year <= end_year:
                        stats.add_time("skip_check", time.time() - start_time)
                        return True
                except:
                    pass

    stats.add_time("skip_check", time.time() - start_time)
    return False


def process_file_fast(file_path, lict_template, exclude_dirs):
    """Quickly process single file"""
    start_time = time.time()

    abs_path = os.path.abspath(file_path)
    for ex_dir in exclude_dirs:
        if ex_dir in abs_path:
            stats.add_time("process_file", time.time() - start_time)
            return

    try:
        read_start = time.time()
        with open(file_path, "r", encoding="utf-8") as f:
            content = f.read()
        stats.add_time("file_read", time.time() - read_start)

        copyright_year = get_copyright_year_fast(file_path)

        if should_skip_copyright_update_fast(content, copyright_year):
            stats.add_time("process_file", time.time() - start_time)
            return

        prepare_start = time.time()
        replace_dict = {
            "File": os.path.basename(file_path),
            "Username": get_username(),
            "CopyrightYear": copyright_year,
        }

        header = lict_template
        for key, value in replace_dict.items():
            header = header.replace(f"%({key})", str(value))

        lines = header.split("\n")
        cpp_header = "/*\n"
        for line in lines:
            cpp_header += " *  " + line + "\n"
        cpp_header += " */"
        stats.add_time("prepare_header", time.time() - prepare_start)

        if cpp_header.strip() in content:
            stats.add_time("process_file", time.time() - start_time)
            return

        write_start = time.time()
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(cpp_header + "\n" + content)
        stats.add_time("file_write", time.time() - write_start)

        print(f"Processed: {file_path}")

    except Exception as e:
        if DEBUG:
            print(f"[DEBUG] Failed to process file {file_path}: {e}")

    stats.add_time("process_file", time.time() - start_time)


def find_source_files(root_dir, exclude_dirs):
    """Recursively find source files"""
    start_time = time.time()

    source_files = []
    for dirpath, dirs, files in os.walk(root_dir):
        dirs[:] = [d for d in dirs if d not in exclude_dirs]

        for file in files:
            ext = os.path.splitext(file)[1]
            if ext in SOURCE_EXTS:
                source_files.append(os.path.join(dirpath, file))

    stats.add_time("find_files", time.time() - start_time)
    return source_files


def process_files_parallel_fast(file_paths, lict_template, exclude_dirs, max_workers):
    """Process files in parallel"""
    start_time = time.time()

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for file_path in file_paths:
            future = executor.submit(
                process_file_fast, file_path, lict_template, exclude_dirs
            )
            futures.append(future)

        completed = 0
        for future in as_completed(futures):
            completed += 1
            if DEBUG and completed % 100 == 0:
                print(f"[DEBUG] Completed {completed}/{len(futures)} files")

    stats.add_time("parallel_processing", time.time() - start_time)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Automatically insert/update copyright headers")
    parser.add_argument(
        "--lict",
        default=DEFAULT_LICT_PATH,
        help=f"Template file path (default: {DEFAULT_LICT_PATH})",
    )
    parser.add_argument("--exclude", default="", help="Additional exclude directories, comma-separated")
    parser.add_argument("--debug", action="store_true", help="Enable debug mode")
    parser.add_argument("--no-git", action="store_true", help="Don't use Git history")
    parser.add_argument(
        "--threads",
        type=int,
        default=THREAD_COUNT,
        help=f"Number of threads (default: {THREAD_COUNT})",
    )
    parser.add_argument("--no-parallel", action="store_true", help="Disable parallel processing")
    parser.add_argument("--fast", action="store_true", help="Fast mode (recommended)")
    args = parser.parse_args()

    global DEBUG, USE_GIT_HISTORY, git_cache
    DEBUG = args.debug
    USE_GIT_HISTORY = not args.no_git

    lict_path = args.lict
    extra_exclude = set(args.exclude.split(",")) if args.exclude else set()
    exclude_dirs = DEFAULT_EXCLUDE_DIRS.union(extra_exclude)

    if DEBUG:
        print(f"[DEBUG] Using Git history: {USE_GIT_HISTORY}")
        print(f"[DEBUG] Thread count: {args.threads}")
        print(f"[DEBUG] Fast mode: {args.fast}")
        print(f"[DEBUG] Starting file collection...")

    total_start = time.time()

    lict_template = read_lict_file(lict_path)
    root_dir = os.getcwd()
    source_files = find_source_files(root_dir, exclude_dirs)

    if DEBUG:
        print(f"[DEBUG] Found {len(source_files)} source files")

    if USE_GIT_HISTORY and args.fast:
        git_cache = GitTimeCache()
        if git_cache.git_root:
            if DEBUG:
                print(f"[DEBUG] Batch querying Git time information...")
            git_cache.batch_query_git_times(source_files)

    if args.no_parallel:
        if DEBUG:
            print(f"[DEBUG] Using single-threaded processing...")

        for i, file_path in enumerate(source_files, 1):
            if DEBUG and i % 50 == 0:
                print(f"[DEBUG] Processed {i}/{len(source_files)} files...")
            process_file_fast(file_path, lict_template, exclude_dirs)
    else:
        if DEBUG:
            print(f"[DEBUG] Using multi-threaded processing (threads: {args.threads})...")

        process_files_parallel_fast(
            source_files, lict_template, exclude_dirs, args.threads
        )

    total_time = time.time() - total_start

    print(f"\nProcessing complete! Total files processed: {len(source_files)}")
    print(f"Total time: {total_time:.2f} seconds")

    stats.print_stats()


if __name__ == "__main__":
    main()