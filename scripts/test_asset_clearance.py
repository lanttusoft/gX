#ai: guard asset provenance reports against outside-checkout imports and false clearance claims.
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from asset_clearance import ClearanceError, RESOURCES, build_report


class AssetClearanceTests(unittest.TestCase):
    def test_author_asset_report_is_hash_bound_and_does_not_claim_verification(self):
        with tempfile.NamedTemporaryFile(dir="resources", prefix="clearance-test-",
                                         suffix=".bin", delete=False) as stream:
            stream.write(b"self-authored fixture")
            path = Path(stream.name)
        try:
            report = build_report(
                [str(path)], origin="author", creator="Test Author",
                source="Created from scratch in Blender", license_text="project (GPLv3)",
                publish="keep", generated_at="2026-10-06T00:00:00+00:00",
            )
            asset = report["assets"][0]
            self.assertEqual(asset["path"], f"resources/{path.name}")
            self.assertEqual(asset["sha256"], hashlib.sha256(b"self-authored fixture").hexdigest())
            self.assertEqual(asset["bytes"], len(b"self-authored fixture"))
            self.assertEqual(report["suggested_provenance_entries"][0]["match"], asset["path"])
            self.assertTrue(any("does not independently verify" in item
                                for item in report["limitations"]))
        finally:
            path.unlink(missing_ok=True)

    def test_rejects_assets_outside_this_gx_resources_tree(self):
        with tempfile.NamedTemporaryFile() as stream:
            with self.assertRaisesRegex(ClearanceError, "already be inside"):
                build_report(
                    [stream.name], origin="author", creator="Test Author",
                    source="Original work", license_text="project (GPLv3)", publish="keep",
                )

    def test_third_party_requires_attribution(self):
        with tempfile.NamedTemporaryFile(dir="resources", prefix="clearance-test-",
                                         suffix=".bin", delete=False) as stream:
            stream.write(b"third party fixture")
            path = Path(stream.name)
        try:
            with self.assertRaisesRegex(ClearanceError, "require an attribution"):
                build_report(
                    [str(path)], origin="third-party", creator="Test Author",
                    source="Obtained from provider", license_text="CC0", publish="keep",
                )
        finally:
            path.unlink(missing_ok=True)

    def test_refuses_manifest_quarantine_paths(self):
        #ai: use a local fixture and manifest so clean checkouts need no ignored quarantine assets.
        with tempfile.NamedTemporaryFile(dir=RESOURCES, prefix="clearance-test-",
                                         suffix=".glb", delete=False) as stream:
            stream.write(b"quarantined fixture")
            path = Path(stream.name)
        try:
            relative = f"resources/{path.resolve().relative_to(RESOURCES).as_posix()}"
            with tempfile.TemporaryDirectory() as directory:
                manifest = Path(directory) / "provenance.json"
                manifest.write_text(json.dumps({"quarantine": [relative], "denied": []}))
                with patch("asset_clearance.MANIFEST", manifest):
                    with self.assertRaisesRegex(ClearanceError, "quarantined resources"):
                        build_report(
                            [str(path)], origin="author", creator="Test Author",
                            source="Original work", license_text="project (GPLv3)",
                            publish="keep",
                        )
        finally:
            path.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
