import json
from pathlib import Path
import re
import sys
import unittest


REPO = Path(__file__).resolve().parents[2]
SCRIPT_DIR = REPO / "scripts"
sys.path.insert(0, str(SCRIPT_DIR))
import validate_style_proof_outputs as RUNTIME  # noqa: E402


class StyleProofJobSchemaRegexTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        generation = REPO / "assets/remastered/style-proof/generation"
        cls.jobs = json.loads((generation / "jobs.json").read_text(encoding="utf-8"))[
            "jobs"
        ]
        schema = json.loads(
            (generation / "jobs.schema.json").read_text(encoding="utf-8")
        )
        properties = schema["$defs"]["job"]["properties"]
        cls.job_id_pattern = properties["job_id"]["pattern"]
        cls.output_path_pattern = properties["expected_output_path"]["pattern"]

    def test_schema_job_id_pattern_matches_runtime_and_every_committed_job(self) -> None:
        self.assertEqual(RUNTIME.JOB_ID_RE.pattern, self.job_id_pattern)
        pattern = re.compile(self.job_id_pattern)
        self.assertEqual(21, len(self.jobs))
        for job in self.jobs:
            with self.subTest(job_id=job["job_id"]):
                self.assertIsNotNone(pattern.fullmatch(job["job_id"]))

    def test_schema_output_path_pattern_accepts_every_committed_job_id(self) -> None:
        pattern = re.compile(self.output_path_pattern)
        for job in self.jobs:
            expected = (
                "assets/remastered/style-proof/generation/outputs/"
                f"{job['job_id']}.png"
            )
            with self.subTest(job_id=job["job_id"]):
                self.assertEqual(expected, job["expected_output_path"])
                self.assertIsNotNone(pattern.fullmatch(job["expected_output_path"]))


if __name__ == "__main__":
    unittest.main()
