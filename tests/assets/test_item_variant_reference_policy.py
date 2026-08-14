import hashlib
import json
from pathlib import Path
import unittest


REPO = Path(__file__).resolve().parents[2]
GENERATION = REPO / "assets/remastered/style-proof/generation"
LOCKED_CURRENT_JOBS_SHA256 = (
    "c1d3b54becf00f5c331aa5a93bf821c0716cccfe8f61bcfe71064b7d5bcbc665"
)


class ItemVariantReferencePolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.jobs_bytes = (GENERATION / "jobs.json").read_bytes()
        cls.jobs = json.loads(cls.jobs_bytes)["jobs"]
        cls.policy = json.loads(
            (GENERATION / "variant-reference-policy.json").read_text(
                encoding="utf-8"
            )
        )
        cls.schema = json.loads(
            (GENERATION / "jobs.schema.json").read_text(encoding="utf-8")
        )
        cls.classification_bytes = (
            GENERATION / "item-classifications.json"
        ).read_bytes()
        cls.classifications = json.loads(cls.classification_bytes)
        cls.classification_schema = json.loads(
            (GENERATION / "item-classifications.schema.json").read_text(
                encoding="utf-8"
            )
        )

    def test_current_jobs_remain_immutable_and_do_not_invent_relations(self) -> None:
        self.assertEqual(
            LOCKED_CURRENT_JOBS_SHA256, hashlib.sha256(self.jobs_bytes).hexdigest()
        )
        self.assertFalse(any("variant_of" in job for job in self.jobs))

    def test_schema_supports_only_explicit_pack_aware_variant_links(self) -> None:
        job_properties = self.schema["$defs"]["job"]["properties"]
        self.assertEqual(
            {"$ref": "#/$defs/variantRelation"}, job_properties["variant_of"]
        )
        relation = self.schema["$defs"]["variantRelation"]
        self.assertFalse(relation["additionalProperties"])
        self.assertEqual(["job_id", "resource_key"], relation["required"])
        self.assertEqual(
            {"$ref": "#/$defs/resourceKey"},
            relation["properties"]["resource_key"],
        )

    def test_policy_requires_the_classic_variant_and_approved_base(self) -> None:
        self.assertEqual(
            "realmz-item-variant-reference-policy", self.policy["policy_kind"]
        )
        rules = self.policy["rules"]
        self.assertEqual(
            ["own_locked_classic", "approved_base_master"],
            rules["ordered_reference_roles"],
        )
        self.assertTrue(rules["base_approval_required_before_variant_call"])
        self.assertTrue(rules["model_call_must_use_every_emitted_reference_path"])
        self.assertTrue(rules["receipt_reference_set_sha256_required"])
        self.assertFalse(rules["unclassified_item_generation_allowed"])
        self.assertFalse(rules["display_alias_implies_visual_variant"])
        self.assertFalse(rules["animation_sequences_are_variants"])

    def test_known_6106_relationship_is_candidate_not_current_job_mutation(self) -> None:
        self.assertEqual(1, len(self.policy["known_relationships"]))
        relation = self.policy["known_relationships"][0]
        self.assertEqual(6100, relation["base_resource_key"]["id"])
        self.assertEqual(6106, relation["variant_resource_key"]["id"])
        self.assertEqual("base_plus_effect", relation["geometry_policy"])
        self.assertEqual(
            "candidate_requires_human_family_approval",
            relation["evidence"]["status"],
        )
        self.assertEqual(
            "historical_rejected_attempts_only",
            self.policy["rules"]["current_style_proof_job_13_status"],
        )

    def test_classifications_are_jobs_bound_complete_and_fail_closed(self) -> None:
        value = self.classifications
        self.assertEqual(
            self.classification_bytes,
            (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode(),
        )
        self.assertEqual(1, value["schema_version"])
        self.assertEqual(
            "style-proof-item-generation-classifications",
            value["classification_set_kind"],
        )
        self.assertEqual(
            hashlib.sha256(self.jobs_bytes).hexdigest(), value["jobs_sha256"]
        )
        item_ids = {
            job["job_id"] for job in self.jobs if job["family"] == "item_spell"
        }
        self.assertEqual(item_ids, {entry["job_id"] for entry in value["entries"]})
        self.assertEqual(
            {
                "classification_manifest_sha256_required_in_call_record": True,
                "classification_required_before_item_call": True,
                "generation_reference_set_required_in_call_record": True,
                "historical_rejected_attempts_may_not_be_backfilled": True,
                "known_variant_candidate_generation_allowed": False,
            },
            value["rules"],
        )
        self.assertFalse(self.classification_schema["additionalProperties"])
        self.assertEqual(
            {"standalone", "base", "animation_frame", "known_variant_candidate", "variant"},
            {
                self.classification_schema["$defs"][name]["properties"]["classification"]["const"]
                for name in (
                    "standalone", "base", "animationFrame",
                    "knownVariantCandidate", "variant",
                )
            },
        )

    def test_exact_current_item_classifications_and_animation_sequences(self) -> None:
        by_id = {
            entry["job_id"]: entry for entry in self.classifications["entries"]
        }
        candidate = by_id["13_item_spell_cicn_6106"]
        self.assertEqual("known_variant_candidate", candidate["classification"])
        self.assertFalse(candidate["generation_allowed"])
        self.assertEqual(6100, candidate["candidate_base_resource_key"]["id"])
        standalone = by_id["14_item_spell_cicn_566"]
        self.assertEqual("standalone", standalone["classification"])
        self.assertEqual(["own_locked_classic"], standalone["reference_roles"])
        expected = {
            "15_item_spell_cicn_12044": (12040, 12044, 6),
            "16_item_spell_cicn_12100": (12096, 12100, 13),
        }
        for job_id, (base, resource_id, spelllook1) in expected.items():
            entry = by_id[job_id]
            sequence = entry["sequence"]
            self.assertEqual("animation_frame", entry["classification"])
            self.assertEqual(list(range(base, base + 8)), sequence["resource_ids"])
            self.assertEqual(8, sequence["frame_count"])
            self.assertEqual(4, sequence["frame_index"])
            self.assertEqual(resource_id, base + sequence["frame_index"])
            self.assertEqual(spelllook1, sequence["spelllook1"])
            self.assertEqual(base, 11992 + sequence["spelllook1"] * 8)
            self.assertEqual(
                "src/realmz_orig/combatinfo-combatchoice.c",
                sequence["source"]["path"],
            )
            self.assertEqual(771, sequence["source"]["first_line"])
            self.assertEqual(773, sequence["source"]["last_line"])
        for entry in by_id.values():
            self.assertEqual(
                "rejected_provenance_incomplete_do_not_backfill",
                entry["historical_attempts"]["status"],
            )


if __name__ == "__main__":
    unittest.main()
