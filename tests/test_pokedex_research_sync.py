import copy
from datetime import datetime, timedelta, timezone
import sys
from pathlib import Path
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from sync_pokedex_research import (  # noqa: E402
    build_snapshot,
    canonical_json,
    is_refresh_due,
    sha256,
    snapshot_diff,
    validate_config,
    validate_snapshot,
)


def csv_bytes(text):
    return text.strip().encode("ascii") + b"\n", {"etag": '"fixture"'}


def fixture_config():
    return {
        "schema_version": 1,
        "automated_source": {
            "id": "fixture",
            "authority_tier": "structured-secondary",
            "repository_url": "https://github.com/example/data.git",
            "repository_branch": "main",
            "raw_base_url":
                "https://raw.githubusercontent.com/example/data/"
                "{revision}/csv/",
            "allowed_host": "raw.githubusercontent.com",
            "refresh_interval_seconds": 86400,
            "files": {
                "species": "pokemon_species.csv",
                "pokemon": "pokemon.csv",
                "forms": "pokemon_forms.csv",
                "pokedexes": "pokedexes.csv",
                "pokedex_memberships": "pokemon_dex_numbers.csv",
            },
        },
        "verification_sources": [
            {
                "id": "pokemon-official-national-tail",
                "authority_tier": "official",
                "url": "https://example.com/pokedex/3/",
                "verified_on": "2026-09-15",
                "expected": {
                    "national_number": 3,
                    "english_name": "Gamma",
                },
            }
        ],
    }


def fixture_files():
    return {
        "species": csv_bytes(
            """
id,identifier,generation_id
1,alpha,1
2,beta,1
3,gamma,2
"""
        ),
        "pokemon": csv_bytes(
            """
id,identifier,species_id
1,alpha,1
2,beta,2
3,gamma,3
10001,alpha-special,1
"""
        ),
        "forms": csv_bytes(
            """
id,identifier,pokemon_id
1,alpha,1
2,beta,2
3,gamma,3
4,alpha-special,10001
5,alpha-cosmetic,10001
"""
        ),
        "pokedexes": csv_bytes(
            """
id,identifier,is_main_series
1,national,1
2,local,1
"""
        ),
        "pokedex_memberships": csv_bytes(
            """
species_id,pokedex_id,pokedex_number
1,1,1
2,1,2
3,1,3
1,2,1
3,2,2
"""
        ),
    }


class PokedexResearchSyncTests(unittest.TestCase):
    def test_build_and_validate_snapshot(self):
        config = fixture_config()
        snapshot = build_snapshot(
            config,
            "a" * 40,
            fixture_files(),
            datetime(2026, 9, 15, tzinfo=timezone.utc),
        )
        self.assertEqual(
            snapshot["totals"],
            {
                "species": 3,
                "pokemon_varieties": 4,
                "forms": 5,
                "pokedex_scopes": 2,
                "generations": 2,
                "pokedex_memberships": 5,
            },
        )
        self.assertEqual(
            [row["species_count"] for row in snapshot["generation_counts"]],
            [2, 1],
        )
        self.assertTrue(
            validate_snapshot(
                snapshot,
                config,
                {"species": [{"id": 1}, {"id": 3}]},
            )
        )

    def test_original_ids_are_separate_from_national_census(self):
        config = fixture_config()
        snapshot = build_snapshot(config, "a" * 40, fixture_files(),
                                  datetime(2026, 9, 15, tzinfo=timezone.utc))
        original = {"id": 60000, "origin": "original"}
        self.assertTrue(validate_snapshot(snapshot, config,
                        {"species": [{"id": 1}, original]}))
        for rows in ([original, original], [{"id": 4}],
                     [{"id": 1, "origin": "original"}]):
            with self.assertRaises(ValueError):
                validate_snapshot(snapshot, config, {"species": rows})

    def test_snapshot_hash_detects_tampering(self):
        config = fixture_config()
        snapshot = build_snapshot(
            config,
            "b" * 40,
            fixture_files(),
            datetime(2026, 9, 15, tzinfo=timezone.utc),
        )
        snapshot["totals"]["species"] = 4
        with self.assertRaisesRegex(ValueError, "Generation species totals"):
            validate_snapshot(snapshot, config)

    def test_diff_reports_added_and_changed_pokedexes(self):
        previous = {
            "data_sha256": "old",
            "totals": {"species": 2},
            "pokedex_counts": [
                {"name": "national", "entry_count": 2},
                {"name": "old-region", "entry_count": 1},
            ],
        }
        current = {
            "data_sha256": "new",
            "totals": {"species": 3},
            "pokedex_counts": [
                {"name": "national", "entry_count": 3},
                {"name": "new-region", "entry_count": 2},
            ],
        }
        change = snapshot_diff(previous, current)
        self.assertEqual(
            change["total_changes"]["species"],
            {"before": 2, "after": 3},
        )
        self.assertEqual(change["added_pokedexes"], ["new-region"])
        self.assertEqual(change["removed_pokedexes"], ["old-region"])
        self.assertEqual(change["changed_pokedexes"], ["national"])

    def test_source_allowlist_rejects_host_escape(self):
        config = fixture_config()
        config["automated_source"]["allowed_host"] = "example.com"
        with self.assertRaisesRegex(ValueError, "allow-listed"):
            validate_config(config)

    def test_refresh_interval(self):
        now = datetime(2026, 9, 15, 12, tzinfo=timezone.utc)
        recent = {"observed_at": "2026-09-15T11:30:00Z"}
        old = {"observed_at": "2026-09-14T11:30:00Z"}
        self.assertFalse(is_refresh_due(recent, 3600, now))
        self.assertTrue(is_refresh_due(old, 3600, now))
        self.assertTrue(is_refresh_due(None, 3600, now))

    def test_data_hash_is_independent_of_observation_time(self):
        config = fixture_config()
        first = build_snapshot(
            config,
            "c" * 40,
            fixture_files(),
            datetime(2026, 9, 15, tzinfo=timezone.utc),
        )
        second = copy.deepcopy(first)
        second["observed_at"] = "2026-09-16T00:00:00Z"
        self.assertEqual(first["data_sha256"], second["data_sha256"])
        self.assertEqual(snapshot_diff(first, second)["kind"], "unchanged")
        core = {
            "totals": second["totals"],
            "national_index": second["national_index"],
            "generation_counts": second["generation_counts"],
            "pokedex_counts": second["pokedex_counts"],
            "invariants": second["invariants"],
        }
        self.assertEqual(second["data_sha256"], sha256(canonical_json(core)))


if __name__ == "__main__":
    unittest.main()
