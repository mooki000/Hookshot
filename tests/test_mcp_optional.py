"""The optional server fails once, helpfully, without site packages; its core stays stdlib-only."""
import ast
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OptionalMcp(unittest.TestCase):
    def test_missing_sdk_has_one_install_line_and_no_protocol(self):
        result = subprocess.run([sys.executable, "-I", "-S", str(ROOT / "scripts" / "hookshot_mcp.py")],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr.splitlines(), ["Install: python -m pip install -r scripts/mcp_requirements.txt"])

    def test_session_and_general_checks_do_not_import_sdk(self):
        for path in ("scripts/hookshot_ctl.py", "tests/test_hookshot_ctl.py", "tests/test_docs.py"):
            tree = ast.parse((ROOT / path).read_text(encoding="utf-8"))
            imports = [n.module for n in ast.walk(tree) if isinstance(n, ast.ImportFrom)]
            imports += [a.name for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names]
            self.assertFalse(any(name and name.split(".")[0] in ("fastmcp", "mcp", "pydantic") for name in imports), path)
        self.assertEqual((ROOT / "scripts" / "mcp_requirements.txt").read_text().splitlines()[-1], "fastmcp==4.1.3")
        self.assertNotIn("fastmcp", (ROOT / "requirements.txt").read_text().lower())


if __name__ == "__main__":
    unittest.main()
