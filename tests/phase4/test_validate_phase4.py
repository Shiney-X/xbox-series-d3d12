# SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12 contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Telemetry-parser unit tests, not GPU fixtures or console acceptance."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "validate_phase4.py"
SPEC = importlib.util.spec_from_file_location("validate_phase4", SCRIPT)
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.results = [
            {"probe": name, "passed": True, "win32_error": 0,
             "details": ";".join(f"{k}={v}" for k, v in validator.CONTRACTS.get(name, {}).items())}
            for name in sorted(validator.REQUIRED)
        ]
        self.probe("lifecycle-journal")["details"] = "session=current"
        self.probe("lifecycle-suspend")["details"] += ";session=current"
        self.journal = [
            {"session": "previous", "event": "launch", "details": "started"},
            {"session": "previous", "event": "suspend", "details": "suspended"},
            {"session": "previous", "event": "resume", "details": "resumed"},
            {"session": "current", "event": "launch", "details": "started"},
            {"session": "current", "event": "suspend", "details": "suspended"},
        ]

    def probe(self, name):
        return next(r for r in self.results if r["probe"] == name)

    def test_separates_prior_resume_from_results_session(self):
        report = validator.validate(self.results, self.journal, True)
        self.assertEqual(report["results_session"], "current")
        self.assertEqual(report["resume_sessions"], ["previous"])
        self.assertEqual(report["presentation_after_resume_sessions"], [])
        self.assertTrue(report["warnings"])
        self.assertFalse(report["guest_shader_support"])

    def test_presentation_requires_event_after_resume(self):
        self.journal.insert(3, {"session": "previous", "event": "navigation", "details": "shell presented"})
        report = validator.validate(self.results, self.journal, True)
        self.assertEqual(report["presentation_after_resume_sessions"], ["previous"])
        self.assertFalse(report["warnings"])

    def test_every_contract_field_rejects_mutation(self):
        for name, contract in validator.CONTRACTS.items():
            for key, value in contract.items():
                with self.subTest(probe=name, field=key):
                    records = copy.deepcopy(self.results)
                    record = next(r for r in records if r["probe"] == name)
                    record["details"] = record["details"].replace(f"{key}={value}", f"{key}=INVALID")
                    with self.assertRaises(validator.EvidenceError):
                        validator.validate(records, self.journal)

    def test_missing_required_probes(self):
        for name in validator.REQUIRED:
            with self.subTest(probe=name), self.assertRaises(validator.EvidenceError):
                validator.validate([r for r in self.results if r["probe"] != name], self.journal)

    def test_failed_extra_probe_is_not_ignored(self):
        self.results.append({"probe": "other", "passed": False, "win32_error": 5, "details": "failed"})
        with self.assertRaises(validator.EvidenceError):
            validator.validate(self.results, self.journal)

    def test_duplicate_probe_or_field(self):
        with self.assertRaises(validator.EvidenceError):
            validator.validate(self.results + [self.results[0]], self.journal)
        self.results[0]["details"] += ";shader_format=DXIL;shader_format=DXIL"
        with self.assertRaises(validator.EvidenceError):
            validator.validate(self.results, self.journal)

    def test_status_types(self):
        for key, value in [("passed", 1), ("passed", "true"), ("win32_error", False), ("win32_error", "0")]:
            records = copy.deepcopy(self.results)
            records[0][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(validator.EvidenceError):
                validator.validate(records, self.journal)

    def test_session_mismatch(self):
        self.probe("lifecycle-journal")["details"] = "session=missing"
        with self.assertRaises(validator.EvidenceError):
            validator.validate(self.results, self.journal)

    def test_resume_without_suspend(self):
        del self.journal[1]
        with self.assertRaises(validator.EvidenceError):
            validator.validate(self.results, self.journal)

    def test_relaunch_is_not_resume(self):
        del self.journal[2]
        self.assertEqual(validator.validate(self.results, self.journal)["resume_sessions"], [])
        with self.assertRaises(validator.EvidenceError):
            validator.validate(self.results, self.journal, True)

    def test_jsonl_bom_blank_lines_and_bad_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.jsonl"
            path.write_text('\ufeff\n{"probe":"example"}\n', encoding="utf-8")
            self.assertEqual(len(validator.load_jsonl(path)), 1)
            for content in ["", "\n", "[]\n", "{broken\n", '{}\n{"partial"']:
                path.write_text(content, encoding="utf-8")
                with self.subTest(content=content), self.assertRaises(validator.EvidenceError):
                    validator.load_jsonl(path)


if __name__ == "__main__":
    unittest.main()
