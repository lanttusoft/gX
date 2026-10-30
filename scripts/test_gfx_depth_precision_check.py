#ai: parser negative controls; synthetic fixtures do not count as GPU evidence.
import unittest
import re
from gfx_depth_precision_check import parse_probe


def fixture():
    lines = ["DEPTH_PROBE_DRIVER synthetic", "DEPTH_PROBE_FORMAT scene=6402 bits=24 msaa=33190 msbits=24 samples=4",
             "DEPTH_PROBE_RESULT complete=1 glerror=0", "DEPTH_PROBE_RESTORE restored=1 glerror=0"]
    for name, distance in [("near", .125), ("mid", 3.), ("far", 900.)]:
        depth = 1000/999.9-100/(999.9*distance)
        fields = {"distance": distance, "ideal": depth, "input": depth, "direct": depth,
                  "readback": depth, "msfirst": depth, "msmin": depth, "msmax": depth,
                  "msmean": depth, "resolved": depth}
        lines.append("DEPTH_PROBE_SAMPLE "+name+" "+" ".join(k+"="+v.hex() for k, v in fields.items()))
    return "\n".join(lines)


class DepthPrecisionTests(unittest.TestCase):
    def test_complete_fixture(self):
        self.assertEqual(parse_probe(fixture())["samples"], 4)

    def test_missing_and_duplicate_metadata(self):
        for text in ["", fixture()+"\nDEPTH_PROBE_DRIVER duplicate"]:
            with self.assertRaises(ValueError):
                parse_probe(text)

    def test_gl_errors_and_restore_rejected(self):
        for text in [fixture().replace("glerror=0", "glerror=1282"),
                     fixture().replace("restored=1", "restored=0"), fixture()+"\nGL_STATE_DIFF depth_probe: bad"]:
            with self.assertRaises(ValueError):
                parse_probe(text)

    def test_missing_sample_rejected(self):
        with self.assertRaises(ValueError):
            parse_probe("\n".join(line for line in fixture().splitlines() if "SAMPLE far" not in line))

    def test_unequal_msaa_rejected(self):
        with self.assertRaises(ValueError):
            parse_probe(re.sub(r"msmin=[^ ]+", "msmin=0x0p+0", fixture()))


if __name__ == "__main__":
    unittest.main()
