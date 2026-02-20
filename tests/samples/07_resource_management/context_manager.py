# ============================================================================
# context_manager.py — Python context managers for resource management demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import List, Optional


class ManagedFile:
    """A file-like object with __enter__/__exit__ for context management."""

    def __init__(self, path: str, mode: str = "w"):
        self.path = path
        self.mode = mode
        self.closed = True
        self.content = ""

    def __enter__(self):
        """Open the file and return self."""
        self.closed = False
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Close the file."""
        self.closed = True
        return False

    def write(self, data: str) -> int:
        """Write data to the file. Returns bytes written."""
        if not self.closed:
            self.content += data
            return len(data)
        return 0

    def read(self) -> str:
        """Read the file content."""
        return self.content


class Timer:
    """A timer context manager for measuring execution time."""

    def __init__(self, label: str):
        self.label = label
        self.start_time = 0.0
        self.elapsed = 0.0

    def __enter__(self):
        """Record start time."""
        import time
        self.start_time = time.time()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Compute elapsed time."""
        import time
        self.elapsed = time.time() - self.start_time
        return False

    def get_elapsed(self) -> float:
        """Return elapsed time in seconds."""
        return self.elapsed


class TempDirectory:
    """A temporary directory context manager."""

    def __init__(self, prefix: str = "tmp_"):
        self.prefix = prefix
        self.path = ""
        self.files: List[str] = []

    def __enter__(self):
        """Create the temp directory."""
        self.path = f"/tmp/{self.prefix}dir"
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up the temp directory."""
        self.files.clear()
        self.path = ""
        return False

    def add_file(self, name: str) -> str:
        """Add a file to the temp directory."""
        full_path = f"{self.path}/{name}"
        self.files.append(full_path)
        return full_path

    def list_files(self) -> List[str]:
        """List files in the temp directory."""
        return self.files
