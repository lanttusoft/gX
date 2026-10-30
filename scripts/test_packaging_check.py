#!/usr/bin/env python3
#ai: synthetic local Git fixtures verify exclusion policy and both archive layouts; no real releases.
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from packaging_check import archive_names, check_names, PRIVATE_FILES
try:
    import certify
except ImportError:
    certify = None

ROOT = Path(__file__).resolve().parents[1]


class PackagingCheckTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="gx-packaging-fixture-", dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.env = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        self.git("init", "--quiet", "--template=")
        files = {
            "CMakeLists.txt": "fixture only\n", "src/main.c": "fixture only\n",
            "modules/module_exports.txt": "fixture ABI source\n",
            "resources/texture/approved.png": "synthetic licensed fixture\n",
            ".ai/README.md": "public contract fixture\n",
            "docs/certification.md": "public documentation fixture\n",
            "certificates/evidence.json": "private fixture\n",
            "bugs/report.json": "private fixture\n", "screenshot/capture.png": "private fixture\n",
            ".opencode/private.json": "private fixture\n", ".claude/private.json": "private fixture\n",
        }
        files.update({name: "private fixture\n" for name in PRIVATE_FILES})
        files.update({f"screenshot{i:03}.png": "private fixture\n" for i in range(5)})
        for relative, text in files.items():
            self.write(relative, text)
        self.git("add", ".")
        self.git("-c", "user.name=Packaging fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "synthetic unsafe snapshot")
        self.git("tag", "legacy")
        self.write(".gitattributes", (ROOT / ".gitattributes").read_text())

    def git(self, *arguments):
        return subprocess.run(["git", "-C", str(self.repo), *arguments], env=self.env,
                              check=True, capture_output=True, text=True).stdout

    def write(self, relative, text):
        path = self.repo / "master" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def commit_policy(self):
        self.git("add", ".")
        self.git("-c", "user.name=Packaging fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "synthetic export policy")

    def test_committed_policy_both_layouts(self):
        self.commit_policy()
        for layout in ["subtree", "release"]:
            with self.subTest(layout=layout):
                names = archive_names(self.repo, layout=layout)
                self.assertEqual(check_names(names, layout), [])
                prefix = "master/" if layout == "release" else ""
                self.assertIn(prefix + "resources/texture/approved.png", names)
                self.assertIn(prefix + ".ai/README.md", names)
                self.assertIn(prefix + "docs/certification.md", names)
                self.assertIn(prefix + "modules/module_exports.txt", names)
                self.assertNotIn(prefix + "screenshot000.png", names)
        self.assertTrue((self.repo / "master/screenshot000.png").exists())
        self.assertTrue((self.repo / "master/certificates/evidence.json").exists())
        self.assertIn("master/screenshot000.png", self.git("ls-files"))

    def test_legacy_tag_cannot_inherit_current_policy(self):
        self.commit_policy()
        for layout in ["subtree", "release"]:
            with self.subTest(layout=layout):
                problems = check_names(archive_names(self.repo, "legacy", layout), layout)
                self.assertTrue(any("screenshot000.png" in problem for problem in problems))
                self.assertTrue(any("certificates" in problem for problem in problems))
                self.assertTrue(any("settings.json" in problem for problem in problems))

    def test_worktree_preview_is_explicit(self):
        for layout in ["subtree", "release"]:
            with self.subTest(layout=layout):
                self.assertTrue(check_names(archive_names(self.repo, layout=layout), layout))
                names = archive_names(self.repo, layout=layout, worktree_attributes=True)
                self.assertEqual(check_names(names, layout), [])

    def test_denylist_without_export_rules(self):
        self.assertTrue(check_names(["CMakeLists.txt", "src/main.c", "bugs/report.json"]))
        self.assertTrue(check_names(["CMakeLists.txt", "src/main.c", "build/cache"]))
        self.assertTrue(check_names(["CMakeLists.txt", "src/main.c", "proofs/spec.lean"]))
        self.assertTrue(check_names(["CMakeLists.txt", "src/main.c", "../escape"]))
        self.assertTrue(check_names(["CMakeLists.txt", "src/main.c", "/absolute"]))
        self.assertTrue(check_names(["master/CMakeLists.txt", "master/src/main.c", "other/file"], "release"))

    def test_required_shape(self):
        self.assertEqual(check_names([
            "CMakeLists.txt", "src/main.c", "modules/module_exports.txt"]), [])
        self.assertTrue(check_names([
            "nested/CMakeLists.txt", "src/main.c", "modules/module_exports.txt"]))
        self.assertTrue(check_names([
            "CMakeLists.txt", "resources/file", "modules/module_exports.txt"]))
        self.assertTrue(any("module_exports.txt" in problem
                            for problem in check_names([
                                "CMakeLists.txt", "src/main.c"])))

    def test_lab_module_artifacts_are_rejected(self):
        base = ["CMakeLists.txt", "src/main.c", "modules/module_exports.txt"]
        cases = (
            ("modules/gx_devcube.so", "module/shared binary"),
            ("modules/gx_shell.so", "module/shared binary"),
            ("build/labs/gx_devcube.so", "non-source tree"),
            ("build/scripting/gx_shell.so", "non-source tree"),
            ("build/labs/gx_shell.dylib", "non-source tree"),
            ("modules/gx_devcube.dll", "module/shared binary"),
        )
        for name, expected in cases:
            with self.subTest(path=name):
                problems = check_names(base + [name])
                self.assertTrue(any(expected in problem for problem in problems),
                                problems)

    @unittest.skipIf(certify is None, "certify.py is private-release only")
    def test_certification_uses_committed_policy(self):
        original = certify.ROOT
        self.addCleanup(setattr, certify, "ROOT", original)
        certify.ROOT = self.repo / "master"
        self.assertEqual(certify.packaging_structure_gate()["status"], "fail")
        self.commit_policy()
        self.assertEqual(certify.packaging_structure_gate()["status"], "pass")

    def test_local_override_cannot_sanitize_legacy_tag(self):
        self.commit_policy()
        info = self.repo / ".git/info/attributes"
        info.parent.mkdir(exist_ok=True)
        info.write_text("* export-ignore\n")
        with self.assertRaisesRegex(RuntimeError, "local info/attributes"):
            archive_names(self.repo, "legacy")

    def test_release_helper_refuses_unsafe_tag_before_output(self):
        (self.repo / "master/.gitattributes").unlink()
        for name in ["mkrelease.sh", "packaging_check.py"]:
            self.write("scripts/" + name, (ROOT / "scripts" / name).read_text())
        raylib = self.repo / "master/external/raylib"
        raylib.mkdir(parents=True)
        command = ["git", "-C", str(raylib)]
        subprocess.run(command + ["init", "--quiet", "--template="], env=self.env, check=True)
        (raylib / "CMakeLists.txt").write_text("synthetic dependency\n")
        subprocess.run(command + ["add", "."], env=self.env, check=True)
        subprocess.run(command + ["-c", "user.name=Packaging fixture", "-c", "user.email=fixture@example.invalid",
                       "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "synthetic dependency"],
                       env=self.env, check=True)
        revision = subprocess.run(command + ["rev-parse", "HEAD"], env=self.env, check=True,
                                  capture_output=True, text=True).stdout.strip()
        self.git("add", "master/scripts")
        self.git("update-index", "--add", "--cacheinfo", f"160000,{revision},master/external/raylib")
        self.git("-c", "user.name=Packaging fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "synthetic unsafe release")
        self.git("tag", "unsafe-release")
        output = self.repo / "output"
        result = subprocess.run(["bash", str(self.repo / "master/scripts/mkrelease.sh"),
                                 "unsafe-release", "--out", str(output)], cwd=self.repo,
                                env=self.env, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PACKAGING_CHECK: FAIL", result.stdout)
        self.assertIn("screenshot000.png", result.stdout)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
