"""Documentation checks: relative links and documented commands.

    python -I -B tests/test_docs.py

These checks use only the standard library. The live MCP catalogue check is in test_mcp_server.py,
so missing the optional SDK never skips general documentation checks.
"""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
PYTHON_COMMAND = re.compile(r"\bpython(?:3)?\s+(?:-I\s+)?(?:-B\s+)?([A-Za-z0-9_./\\-]+\.py)\b")
POWERSHELL_COMMAND = re.compile(r"\bpowershell\s+-File\s+([A-Za-z0-9_./\\-]+\.ps1)\b")


def tracked_markdown():
    result = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "--", "*.md"],
                            capture_output=True, check=True)
    files = []
    for item in result.stdout.decode("utf-8", "replace").split("\0"):
        if item and not item.replace("\\", "/").startswith("third_party/"):   # (vendored upstream docs)
            files.append(ROOT / item)
    return files


def without_fences(text):
    """The lines outside ``` fences, with the line numbers of the rest."""
    kept = []
    fenced = False
    for number, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if not fenced:
            kept.append((number, line))
    return kept


def check_links(files):
    problems = []
    for file in files:
        text = file.read_text(encoding="utf-8", errors="replace")
        for number, line in without_fences(text):
            for raw in LINK.findall(line):
                target = raw.strip().split(" ", 1)[0].strip("<>")
                if not target or target.startswith(("#", "http://", "https://", "mailto:", "data:")):
                    continue
                target = target.split("#", 1)[0]
                if not target:
                    continue
                resolved = (file.parent / target.replace("\\", "/")).resolve()
                if not resolved.exists():
                    problems.append(f"{file.relative_to(ROOT)}:{number}: link does not resolve: {raw}")
    return problems


def check_commands(files):
    problems = []
    for file in files:
        text = file.read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            for pattern in (PYTHON_COMMAND, POWERSHELL_COMMAND):
                for raw in pattern.findall(line):
                    path = (ROOT / raw.replace("\\", "/")).resolve()
                    if not path.is_file():
                        problems.append(f"{file.relative_to(ROOT)}:{number}: command names a missing file: {raw}")
    return problems


def main():
    files = tracked_markdown()
    problems = check_links(files) + check_commands(files)
    if problems:
        print("documentation check failed:\n")
        for problem in problems:
            print(f"  - {problem}")
        raise SystemExit(1)
    print(f"PASS: {len(files)} Markdown files: links and commands")


if __name__ == "__main__":
    main()
