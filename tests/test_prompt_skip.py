"""Game-free prompt-cache proofs, using authored files and only replacing the expensive drawing.

python -I -B tests/test_prompt_skip.py
HOOKSHOT_PROMPT_TEST_SOURCE=/path/to/main/nx2 python -I -B tests/test_prompt_skip.py
The unchanged-build case also runs against old make_game.py: it fails because the drawing runs twice.
"""
import ast
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("HOOKSHOT_PROMPT_TEST_SOURCE", ROOT / "nx2"))
sys.path.insert(0, str(SOURCE))
spec = importlib.util.spec_from_file_location("prompt_assets", SOURCE / "prompt_assets.py")
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)
spec = importlib.util.spec_from_file_location("prompt_make_game", SOURCE / "make_game.py")
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)

# The actual finish-stage call site, including its no-fontTools fallback and packaged/source decision.
main = next(n for n in ast.parse((SOURCE / "make_game.py").read_text(encoding="utf-8")).body
            if isinstance(n, ast.FunctionDef) and n.name == "main")
start = next(i for i, n in enumerate(main.body) if isinstance(n, ast.Assign) and
             any(isinstance(t, ast.Name) and t.id == "prompts" for t in n.targets))
end = next(i for i in range(start + 1, len(main.body)) if isinstance(main.body[i], ast.Assign) and
           any(isinstance(t, ast.Name) and t.id == "icon" for t in main.body[i].targets))
FINISH = compile(ast.Module(body=main.body[start:end], type_ignores=[]), str(SOURCE / "make_game.py"), "exec")


class PromptSkipTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="prompt skip ")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.here = self.root / "nx2"
        self.romfs = self.root / "romfs"
        self.out = self.root / "game" / "prompts"
        self.out.parent.mkdir()
        self.source_files = [self.here / n for n in ("prompt_assets.py", "prompt_draw.py", "prompt_faces.py", "prompt_keys.py")]
        self.game_files = [self.romfs / n for n in (P.FONT, P.prompt_keys.LABEL_FONT, "Pack/ZsDic.pack.zs", P.TEXTURES, P.LAYOUTS,
                                                  "System/Resource/ResourceSizeTable.authored.rsizetable.zs")]
        self.kenney = self.root / "kenney_input"
        self.keyboard = self.root / "kenney_input_keyboard"
        self.font_files = [self.kenney / f["font"] for f in P.FACES.values()] + [self.keyboard / P.prompt_keys.FONT]
        for path in self.source_files + self.game_files + self.font_files:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"authored input: " + path.name.encode())
        self.args = types.SimpleNamespace(romfs=str(self.romfs), out=str(self.out), faces=",".join(P.ALL_FACES),
                                          no_textures=False, dump=None)
        self.calls = 0
        self.during = None
        self.packages = {name: ["authored-version", True] for name in getattr(P, "PROMPT_PACKAGES", {"PIL": "pillow"})}
        self.log = io.StringIO()
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(mock.patch.dict(os.environ, {}, clear=True))
        self.stack.enter_context(contextlib.redirect_stdout(self.log))
        self.stack.enter_context(mock.patch.multiple(P, HERE=str(self.here), KENNEY=str(self.kenney), KENNEY_KEYBOARD=str(self.keyboard)))
        self.stack.enter_context(mock.patch.object(M.importlib.util, "find_spec", return_value=object()))
        if hasattr(P, "make_prompts"):
            self.stack.enter_context(mock.patch.object(P, "generate", side_effect=self.draw))
            self.stack.enter_context(mock.patch.object(P, "prompt_packages", side_effect=lambda: self.packages.copy()))
        self.stack.enter_context(mock.patch.object(M, "run", side_effect=self.child))

    def draw(self, a):
        self.calls += 1
        out = Path(a.out)
        out.mkdir(parents=True, exist_ok=True)
        (out / "External.bfotf").write_bytes(b"authored complete font")
        for face in a.faces.split(","):
            (out / face).mkdir()
            (out / face / "hint.bin").write_bytes(b"authored hint")
            if not a.no_textures and face != "keyboard":
                (out / face / "textures.bin").write_bytes(b"authored texture")
        for name in ("Common.blarc", "ring.bin", "rsizetable.zs") + (() if a.no_textures else ("Common.bntx",)):
            (out / name).write_bytes(b"authored " + name.encode())
        if self.during:
            self.during(out)

    def child(self, command, **kwargs):
        a = types.SimpleNamespace(**vars(self.args))
        a.out = command[command.index("--out") + 1]
        try:
            if hasattr(P, "make_prompts"):
                P.make_prompts(a, "--incremental" in command)
            else:
                self.draw(a)
        except (OSError, SystemExit):
            return 1   # a failed child, not an interruption of the builder itself
        return 0

    def build(self, packaged=False):
        env = dict(vars(M), romfs=str(self.romfs), out=str(self.out.parent), launcher="packaged launcher" if packaged else None)
        exec(FINISH, env)

    def stamp(self):
        return self.out / getattr(P, "PROMPT_KEY", ".prompt_key")

    def test_unchanged_skips_actual_finish_call(self):
        self.build()
        self.build()
        self.assertEqual(self.calls, 1, "unchanged finish must not draw the prompts twice")
        self.assertIn("prompts: unchanged, kept", self.log.getvalue())

    def test_every_file_input_kind(self):
        paths = list(dict.fromkeys(self.source_files + self.game_files + self.font_files))
        for path in paths:
            with self.subTest(input=path.name):
                self.build()
                before = self.calls
                old_time = path.stat().st_mtime_ns
                path.write_bytes(b"changed input of the same or another size")
                os.utime(path, ns=(old_time, old_time))
                self.build()
                self.assertEqual(self.calls, before + 1, str(path))

    def test_override_added_changed_and_removed(self):
        path = self.here / "prompt_faces" / "xbox" / "Controller_Pro.png"
        self.build()
        for value in (b"first authored override", b"second authored override", None):
            before = self.calls
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value) if value is not None else path.unlink()
            self.build()
            self.assertEqual(self.calls, before + 1)

    def test_table_selection_and_optional_font_presence(self):
        self.build()
        table = self.game_files[-1].with_name("ResourceSizeTable.aaa.rsizetable.zs")
        table.write_bytes(b"new first table")
        self.build()
        self.assertEqual(self.calls, 2)
        self.game_files[1].unlink()
        self.build()
        self.assertEqual(self.calls, 3)
        self.game_files[1].write_bytes(b"bold font returns")
        self.build()
        self.assertEqual(self.calls, 4)

    def test_each_package_version_and_presence(self):
        self.build()
        for name in self.packages:
            for value in (["different-version", True], [None, False, name]):
                with self.subTest(package=name, value=value):
                    before = self.calls
                    self.packages[name] = value
                    self.build()
                    self.assertEqual(self.calls, before + 1)

    def test_switch_and_parameters(self):
        self.build()
        os.environ["SOURCE_DATE_EPOCH"] = "1234567890"
        self.build()
        self.assertEqual(self.calls, 2)
        self.args.no_textures = True
        self.build()
        self.assertEqual(self.calls, 3)
        self.args.faces = "switch"
        self.build()
        self.assertEqual(self.calls, 4)

    def test_missing_truncated_and_same_size_corrupt_output(self):
        self.build()
        for damage in (lambda p: p.unlink(), lambda p: p.write_bytes(b"cut"),
                       lambda p: p.write_bytes(b"X" * p.stat().st_size)):
            with self.subTest(damage=damage):
                before = self.calls
                damage(self.out / "xbox" / "textures.bin")
                self.build()
                self.assertEqual(self.calls, before + 1)

    def test_missing_empty_keyboard_directory(self):
        self.build()
        (self.out / "keyboard" / "hint.bin").unlink()
        self.build()
        (self.out / "keyboard" / "hint.bin").unlink()
        (self.out / "keyboard").rmdir()
        self.build()
        self.assertEqual(self.calls, 3)

    def test_kept_originals_unchanged_and_runtime_files_cleared(self):
        self.build()
        original = {p.relative_to(self.out): p.read_bytes() for p in self.out.rglob("*") if p.is_file()}
        for name in P.PROMPT_RUNTIME:
            (self.out / name).write_bytes(b"runtime output")
        self.build()
        self.assertEqual(self.calls, 1)
        self.assertEqual({p.relative_to(self.out): p.read_bytes() for p in self.out.rglob("*") if p.is_file()}, original)
        self.assertTrue(all(not (self.out / name).exists() for name in P.PROMPT_RUNTIME))

    def test_unclearable_runtime_file_regenerates_instead_of_keeping(self):
        self.build()
        runtime = self.out / "Common.key"
        runtime.write_bytes(b"runtime output")
        remove = os.remove
        def denied(path):
            if Path(path) == runtime:
                raise PermissionError("authored runtime file denial")
            remove(path)
        with mock.patch.object(P.os, "remove", side_effect=denied):
            self.build()
        self.assertEqual(self.calls, 2)
        self.assertFalse(runtime.exists())
        self.assertIn("runtime files could not be cleared", self.log.getvalue())

    def test_failed_generation_removes_prompts_for_source_and_packaged_builds(self):
        for packaged in (False, True):
            with self.subTest(packaged=packaged):
                self.during = None
                self.build(packaged=packaged)
                self.assertTrue(self.out.exists())
                os.environ["HOOKSHOT_FORCE_PROMPTS"] = "1"
                def failed(out):
                    (out / "External.bfotf").write_bytes(b"partial font")
                    raise OSError("authored drawing failure")
                self.during = failed
                self.build(packaged=packaged)
                self.assertFalse(self.out.exists())
                self.assertIn("the button prompts of other controllers were not made: the game shows its own",
                              self.log.getvalue())

    def test_interrupted_generation_keeps_old_output_without_stamp(self):
        self.build()
        old = (self.out / "External.bfotf").read_bytes()
        os.environ["HOOKSHOT_FORCE_PROMPTS"] = "1"
        def interrupt(out):
            (out / "External.bfotf").write_bytes(b"partial new font")
            raise KeyboardInterrupt()
        self.during = interrupt
        with self.assertRaises(KeyboardInterrupt):
            self.build()
        self.assertFalse(self.stamp().exists())
        self.assertEqual((self.out / "External.bfotf").read_bytes(), old)
        self.assertFalse(list(self.out.parent.glob(".prompts-*")))

    def test_mid_generation_edit_never_stamps_old_output(self):
        self.during = lambda out: self.source_files[0].write_bytes(b"changed while the old source was drawing")
        self.build()
        self.assertFalse(self.stamp().exists(), "post-work key must not certify old output")
        self.during = None
        self.build()
        self.assertEqual(self.calls, 2)
        self.assertTrue(self.stamp().exists())

    def test_unreadable_input_never_skips_or_stamps(self):
        self.build()
        real_file = P.prompt_file
        def denied(path):
            if Path(path) == self.source_files[0]:
                raise PermissionError("authored unreadable input")
            return real_file(path)
        with mock.patch.object(P, "prompt_file", side_effect=denied):
            self.build()
            self.build()
        self.assertEqual(self.calls, 3)
        self.assertFalse(self.stamp().exists())
        self.assertIn("input unreadable", self.log.getvalue())

    def test_forced_switch_and_env_on_false_values(self):
        self.build()
        for value in ("", "0", " false ", "OFF", "No"):
            os.environ["HOOKSHOT_FORCE_PROMPTS"] = value
            self.build()
            self.assertEqual(self.calls, 1)
        os.environ["HOOKSHOT_FORCE_PROMPTS"] = "yes"
        self.build()
        self.assertEqual(self.calls, 2)
        self.assertIn("regenerated (forced)", self.log.getvalue())

    def test_packaged_build_always_regenerates(self):
        self.build(packaged=True)
        self.build(packaged=True)
        self.assertEqual(self.calls, 2)
        self.assertFalse(self.stamp().exists())

    def test_malformed_and_empty_manifest(self):
        self.build()
        for text in ("{", "[]", '{"inputs": {}, "outputs": {}}'):
            self.stamp().write_text(text, encoding="utf-8")
            before = self.calls
            self.build()
            self.assertEqual(self.calls, before + 1)

    def test_incomplete_generation_leaves_no_prompts(self):
        self.build()
        os.environ["HOOKSHOT_FORCE_PROMPTS"] = "1"
        self.during = lambda out: (out / "External.bfotf").unlink()
        self.build()
        self.assertFalse(self.out.exists())
        self.assertFalse(self.stamp().exists())

    def test_failed_publication_leaves_no_prompts(self):
        self.build()
        os.environ["HOOKSHOT_FORCE_PROMPTS"] = "1"
        replace = os.replace
        def fail_new(src, dst):
            if Path(src).name == "new":
                raise PermissionError("authored publication failure")
            replace(src, dst)
        with mock.patch.object(P.os, "replace", side_effect=fail_new):
            self.build()
        self.assertFalse(self.out.exists())
        self.assertFalse(self.stamp().exists())

    def test_no_fonttools_keeps_original_fallback(self):
        self.build()
        with mock.patch.object(M.importlib.util, "find_spec", return_value=None):
            self.build()
        self.assertEqual(self.calls, 1)
        self.assertFalse(self.out.exists())


    def test_unreadable_reason_is_printed_even_if_forced_generation_fails(self):
        os.environ["HOOKSHOT_FORCE_PROMPTS"] = "1"
        def interrupted(out):
            raise OSError("authored generation failure")
        self.during = interrupted
        with mock.patch.object(P, "prompt_key", side_effect=PermissionError("authored unreadable input")):
            self.build()
        self.assertIn("input unreadable", self.log.getvalue())
        self.assertFalse(self.stamp().exists())
        self.assertFalse(self.out.exists())


class PromptDependencyTests(unittest.TestCase):
    def test_package_versions_and_unavailable_imports_are_recorded(self):
        versions = {package: "authored-" + package for package in P.PROMPT_PACKAGES.values()}
        def unavailable(name):
            raise ModuleNotFoundError(name=name)
        with mock.patch.object(P.importlib.metadata, "version", side_effect=lambda n: versions[n]), \
                mock.patch.object(P.importlib, "import_module", side_effect=unavailable):
            got = P.prompt_packages()
        for module, package in P.PROMPT_PACKAGES.items():
            self.assertEqual(got[module], [versions[package], False, module])
        # An importable package without version metadata is not a basis for reusing output.
        with mock.patch.object(P.importlib.metadata, "version", side_effect=P.importlib.metadata.PackageNotFoundError), \
                mock.patch.object(P.importlib, "import_module", return_value=object()):
            with self.assertRaises(OSError):
                P.prompt_packages()

    def test_local_imports_and_environment_are_accounted_for(self):
        sources = {"prompt_assets", "prompt_draw", "prompt_faces", "prompt_keys"}
        for name in sources:
            tree = ast.parse((SOURCE / (name + ".py")).read_text(encoding="utf-8"))
            for node in ast.walk(tree):
                modules = [a.name for a in node.names] if isinstance(node, ast.Import) else \
                    [node.module] if isinstance(node, ast.ImportFrom) and node.module else []
                for module in modules:
                    if (SOURCE / (module.split(".")[0] + ".py")).is_file():
                        self.assertIn(module, sources | {"kit"}, "new prompt dependency needs fingerprint coverage")
        self.assertIn("SOURCE_DATE_EPOCH", P.PROMPT_ENV)
        self.assertIn("FREETYPE_PROPERTIES", P.PROMPT_ENV)
        self.assertIn("archspec", P.PROMPT_PACKAGES)
        self.assertIn("numpy", P.PROMPT_PACKAGES)
        self.assertIn("zstandard", P.PROMPT_PACKAGES)


if __name__ == "__main__":
    unittest.main()
