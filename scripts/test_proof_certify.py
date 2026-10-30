#ai: proof-certificate gate selection, verdict and hash controls.
import unittest

try:
    import proof_certify
except SystemExit as exc:
    raise unittest.SkipTest(f"proof_certify unavailable: {exc}")


class ProofCertifyTests(unittest.TestCase):
    def test_tier_gate_selection(self):
        catalog = proof_certify.gate_commands("catalog")
        kernel = proof_certify.gate_commands("kernel")
        guarded = proof_certify.gate_commands("guarded")
        self.assertEqual([name for name, _ in catalog], ["proof-inventory"])
        self.assertIn("proofs-full", [name for name, _ in kernel])
        self.assertGreater(len(guarded), len(kernel))
        commands = [tuple(command) for _, command in guarded]
        self.assertEqual(len(commands), len(set(commands)))

    def test_verdicts(self):
        pass_gate = [{"status": "pass"}]
        fail_gate = [{"status": "fail"}]
        self.assertEqual(proof_certify.certificate_verdict(
            pass_gate, {"verdict": "PASS"}, []), "CERTIFIED")
        self.assertEqual(proof_certify.certificate_verdict(
            pass_gate, {"verdict": "PASS"}, [{"module": "x"}]), "GATED")
        self.assertEqual(proof_certify.certificate_verdict(
            fail_gate, {"verdict": "PASS"}, []), "NOT CERTIFIED")

    def test_hash_stability(self):
        first = {"a": 1, "b": [2, 3]}
        second = {"b": [2, 3], "a": 1}
        self.assertEqual(proof_certify.certificate_hash(first),
                         proof_certify.certificate_hash(second))
        self.assertNotEqual(proof_certify.certificate_hash(first),
                            proof_certify.certificate_hash({"a": 2, "b": [2, 3]}))


if __name__ == "__main__":
    unittest.main()
