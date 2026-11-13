import importlib.util
import io
import json
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[2] / "tools/bootstrap_render_core.py"
SPEC = importlib.util.spec_from_file_location("render_core_bootstrap", SCRIPT)
BOOTSTRAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOOTSTRAP)
ROOT_NAME = "jellyframe-render-core-0.8.0"


class RenderCoreBootstrapTests(unittest.TestCase):
    def archive(self, path: Path, name: str, kind: str = "file") -> None:
        with tarfile.open(path, "w:gz") as output:
            member = tarfile.TarInfo(name)
            if kind == "dir":
                member.type = tarfile.DIRTYPE
                output.addfile(member)
            elif kind == "symlink":
                member.type = tarfile.SYMTYPE
                member.linkname = "../../outside"
                output.addfile(member)
            else:
                data = b"fixture"
                member.size = len(data)
                output.addfile(member, io.BytesIO(data))

    def install_metadata(self, prefix: Path, *, version="0.8.0", abi="3", source_hash="a" * 64,
                         manifest_hash=None, profile_version=None, profile_abi=None) -> None:
        package = prefix / "lib/cmake/JellyFrameRenderCore"
        package.mkdir(parents=True)
        (package / "JellyFrameRenderCoreConfig.cmake").write_text(
            "\n".join((
                f'set(JellyFrameRenderCore_PACKAGE_VERSION "{version}")',
                f'set(JellyFrameRenderCore_ENGINE_ABI "{abi}")',
                f'set(JellyFrameRenderCore_SOURCE_HASH "{source_hash}")',
                'set(JellyFrameRenderCore_SOURCE_FILE_COUNT "12")',
            )), encoding="utf-8")
        (prefix / "share/jellyframe_render_core_source_manifest.json").parent.mkdir(parents=True)
        (prefix / "share/jellyframe_render_core_source_manifest.json").write_text(json.dumps({
            "sourceHash": manifest_hash or source_hash, "sourceFileCount": 12,
        }), encoding="utf-8")
        (prefix / "share/jellyframe_render_core_profile.json").write_text(json.dumps({
            "packageVersion": profile_version or version, "engineAbi": int(profile_abi or abi),
        }), encoding="utf-8")

    def test_lock_values_match_current_runtime_pin(self):
        lock = BOOTSTRAP.read_lock(SCRIPT.parents[1] / "cmake/jellyframe_dependency_lock.cmake")
        self.assertEqual(lock["version"], "0.8.0")
        self.assertEqual(lock["abi"], "3")
        self.assertEqual(lock["source_hash"], "fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795")

    def test_extracts_one_rooted_source_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "valid.tar.gz"
            self.archive(archive, f"{ROOT_NAME}/src/file.cpp")
            extracted = BOOTSTRAP.extract_source_archive(archive, root / "unpack", ROOT_NAME)
            self.assertEqual((extracted / "src/file.cpp").read_bytes(), b"fixture")

    def test_rejects_traversal_before_writing_outside_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "traversal.tar.gz"
            self.archive(archive, f"{ROOT_NAME}/../../outside.txt")
            destination = root / "unpack"
            with self.assertRaisesRegex(ValueError, "unsafe archive member path"):
                BOOTSTRAP.extract_source_archive(archive, destination, ROOT_NAME)
            self.assertFalse((root / "outside.txt").exists())
            self.assertFalse(destination.exists())

    def test_rejects_symlinks_and_wrong_archive_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            link_archive = root / "link.tar.gz"
            self.archive(link_archive, f"{ROOT_NAME}/link", "symlink")
            with self.assertRaisesRegex(ValueError, "link or special file"):
                BOOTSTRAP.extract_source_archive(link_archive, root / "link-out", ROOT_NAME)
            wrong_root = root / "wrong-root.tar.gz"
            self.archive(wrong_root, "other-core/src/file.cpp")
            with self.assertRaisesRegex(ValueError, "outside jellyframe-render-core-0.8.0"):
                BOOTSTRAP.extract_source_archive(wrong_root, root / "wrong-out", ROOT_NAME)

    def test_rejects_archive_checksum_before_creating_workspace(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "source.tar.gz"
            self.archive(archive, f"{ROOT_NAME}/src/file.cpp")
            workspace = root / "bootstrap"
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--archive", str(archive),
                 "--expected-sha256", "0" * 64, "--workspace", str(workspace)],
                capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 2)
            self.assertIn("archive SHA-256 mismatch", result.stderr)
            self.assertFalse(workspace.exists())

    def test_refuses_to_reuse_workspace(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "source.tar.gz"
            self.archive(archive, f"{ROOT_NAME}/src/file.cpp")
            workspace = root / "bootstrap"
            workspace.mkdir()
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--archive", str(archive),
                 "--expected-sha256", BOOTSTRAP.archive_sha256(archive), "--workspace", str(workspace)],
                capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 2)
            self.assertIn("workspace already exists", result.stderr)

    def test_checks_installed_manifest_and_profile_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            self.install_metadata(prefix)
            identity = BOOTSTRAP.installed_identity(prefix)
            self.assertEqual(identity["version"], "0.8.0")
            self.assertEqual(identity["abi"], "3")
            self.assertEqual(identity["source_file_count"], 12)
            with tempfile.TemporaryDirectory() as mismatch_dir:
                mismatch = Path(mismatch_dir)
                self.install_metadata(mismatch, manifest_hash="b" * 64)
                with self.assertRaisesRegex(ValueError, "manifest does not match"):
                    BOOTSTRAP.installed_identity(mismatch)
            with tempfile.TemporaryDirectory() as profile_dir:
                mismatch = Path(profile_dir)
                self.install_metadata(mismatch, profile_abi="4")
                with self.assertRaisesRegex(ValueError, "profile does not match"):
                    BOOTSTRAP.installed_identity(mismatch)


if __name__ == "__main__":
    unittest.main()
