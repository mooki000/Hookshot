"""Documentation checks: relative links, documented commands and the MCP tool/resource tables.

    python -I -B tests/test_docs.py

The link and command checks are stdlib-only. The MCP table check imports the server and skips cleanly when the `mcp`
package is not installed (tests/run_all.py also knows that dependency). Nothing is executed except git and the
in-memory MCP client.
"""
import asyncio
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
PYTHON_COMMAND = re.compile(r"\bpython(?:3)?\s+(?:-I\s+)?(?:-B\s+)?([A-Za-z0-9_./\\-]+\.py)\b")
POWERSHELL_COMMAND = re.compile(r"\bpowershell\s+-File\s+([A-Za-z0-9_./\\-]+\.ps1)\b")
RUN_ALL_MODULES = ["mcp"]


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


def mcp_tables():
    sys.path.insert(0, str(ROOT / "scripts"))
    try:
        import hookshot_mcp
        from mcp import Client
    except ModuleNotFoundError as exc:
        raise SystemExit(f"SKIP: {exc}")

    async def names():
        async with Client(hookshot_mcp.mcp) as client:
            tools = {tool.name for tool in (await client.list_tools()).tools}
            resources = {resource.uri for resource in (await client.list_resources()).resources}
        return tools, resources

    tools, resources = asyncio.run(names())
    text = (ROOT / "docs" / "mcp.md").read_text(encoding="utf-8", errors="replace")
    section = text.split("## Tools", 1)[1].split("\n## ", 1)[0]
    documented_tools = set(re.findall(r"^\|\s*`([^`]+)`\s*\|", section, re.MULTILINE))
    documented_resources = set(re.findall(r"`(hookshot://[^`]+)`", text))
    problems = []
    if documented_tools != tools:
        problems.append(f"docs/mcp.md tools {sorted(documented_tools)} != server {sorted(tools)}")
    if documented_resources != resources:
        problems.append(f"docs/mcp.md resources {sorted(documented_resources)} != server {sorted(resources)}")
    return problems


def main():
    files = tracked_markdown()
    problems = check_links(files) + check_commands(files) + mcp_tables()
    if problems:
        print("documentation check failed:\n")
        for problem in problems:
            print(f"  - {problem}")
        raise SystemExit(1)
    print(f"PASS: {len(files)} Markdown files: links, commands and MCP tables")


if __name__ == "__main__":
    main()
