import os
import sys
import subprocess
import multiprocessing
import fnmatch
from pathlib import Path
from update_commant import remove_duplicate_copyright_comments


def find_clang_format():
    """Find clang-format executable"""
    # Use environment variable first
    if "CLANG_FORMAT" in os.environ:
        if os.path.exists(os.environ["CLANG_FORMAT"]):
            return os.environ["CLANG_FORMAT"]

    # Check system PATH
    for name in ["clang-format", "clang-format.exe"]:
        try:
            subprocess.run(
                [name, "--version"],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            return name
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue

    return None


def load_gitignore(root_dir):
    """Load .gitignore patterns manually"""
    gitignore_path = os.path.join(root_dir, ".gitignore")
    patterns = []
    if os.path.exists(gitignore_path):
        with open(gitignore_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                # Handle negation patterns
                if line.startswith("!"):
                    # For simplicity, we skip negation patterns in this implementation
                    continue
                # Handle absolute patterns (starting with /)
                if line.startswith("/"):
                    patterns.append(line[1:])  # Remove leading /
                else:
                    patterns.append(line)
                    # Also add **/pattern for matching in subdirectories
                    patterns.append("**/" + line)

                # Special handling for directory patterns ending with /
                if line.endswith("/"):
                    patterns.append(line + "**")
                    if not line.startswith("/"):
                        patterns.append("**/" + line + "**")
    return patterns


def is_ignored(path, root_dir, ignore_patterns):
    """Check if file is ignored by manually parsing .gitignore rules"""
    try:
        rel_path = os.path.relpath(path, root_dir).replace("\\", "/")
    except ValueError:
        # Handle case where path is on different drive
        return False

    # Manual parsing of .gitignore patterns
    for pattern in ignore_patterns:
        # Convert glob patterns to proper regex-like matching
        if fnmatch.fnmatch(rel_path, pattern):
            return True
        # Also check the file name alone
        if fnmatch.fnmatch(os.path.basename(path), pattern):
            return True
    return False


def find_files(root_dir, suffixes):
    """Find all files that need formatting"""
    ignore_patterns = load_gitignore(root_dir)
    files = []

    for dirpath, _, filenames in os.walk(root_dir):
        # Manually exclude third_party directories
        if "third_party" in dirpath.split(os.sep):
            continue

        # Also exclude any directory that matches third-party pattern
        dir_parts = dirpath.split(os.sep)
        if any("third-party" == part.lower() for part in dir_parts):
            continue

        # Check if the directory itself is ignored
        try:
            rel_dir_path = os.path.relpath(dirpath, root_dir).replace("\\", "/") + "/"
            for pattern in ignore_patterns:
                if fnmatch.fnmatch(rel_dir_path, pattern):
                    continue
        except ValueError:
            # Handle case where path is on different drive
            pass

        for filename in filenames:
            # Check file extension
            ext = Path(filename).suffix
            if ext not in suffixes:
                continue

            file_path = os.path.join(dirpath, filename)

            # Check if file is ignored
            if is_ignored(file_path, root_dir, ignore_patterns):
                print(f"Skipping ignored file: {file_path}")
                continue

            files.append(file_path)

    return files


def format_file(args):
    """Format a single file and ensure it ends with a newline"""
    file_path, clang_format = args
    try:
        # Step 1: Run clang-format
        result = subprocess.run(
            [clang_format, "-i", "-style=file", file_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        if result.returncode != 0:
            return f"Error: Failed to format {file_path}: {result.stderr}"

        # Step 2: Ensure file ends with a single newline
        try:
            with open(file_path, "rb") as f:
                try:
                    f.seek(-1, os.SEEK_END)
                    last_char = f.read(1)
                except OSError:  # File is empty
                    last_char = b""

            # If last character is not newline, append one
            if last_char != b"\n":
                with open(file_path, "ab") as f:
                    f.write(b"\n")
        except Exception as e:
            return f"Error: Failed to ensure newline at end of {file_path}: {str(e)}"

        return f"Formatted: {file_path}"

    except Exception as e:
        return f"Exception: Error processing {file_path}: {str(e)}"


def main():
    # Configuration parameters
    default_root = (
        os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        if len(sys.argv) <= 1
        else sys.argv[1]
    )
    root_dir = sys.argv[1] if len(sys.argv) > 1 else default_root
    suffixes = os.environ.get("SUFFIXES", ".h .cc .cpp .hpp .cxx .hxx .C .cppm").split()
    parallel_jobs = int(os.environ.get("PARALLEL_JOBS", multiprocessing.cpu_count()))

    # Check for clang-format
    clang_format = find_clang_format()
    if not clang_format:
        print(
            "Error: clang-format not found. Please install and ensure it is in PATH, or specify via CLANG_FORMAT environment variable",
            file=sys.stderr,
        )
        sys.exit(1)

    # Print version info
    try:
        version = subprocess.run(
            [clang_format, "--version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout.strip()
        print(f"Using {version}")
    except:
        pass

    # Find files
    print("Searching for files...")
    files = find_files(root_dir, suffixes)

    if not files:
        print("No files found that need formatting")
        return

    # remove extra copyright comments
    print("Removing duplicate copyright comments...")
    for file_path in files:
        remove_duplicate_copyright_comments(file_path)

    print(f"Found {len(files)} files, processing with {parallel_jobs} parallel jobs...")

    # Process in parallel
    with multiprocessing.Pool(parallel_jobs) as pool:
        results = pool.map(format_file, [(f, clang_format) for f in files])

        # Print results
        for result in results:
            print(result)

    print("Formatting complete")


if __name__ == "__main__":
    main()
