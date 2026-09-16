#!/usr/bin/env python3
"""Refresh or validate the checked-in Pokédex source census.

The refresh path pins PokéAPI's data repository commit and collects aggregate
identifiers and counts. It deliberately does not copy official descriptions or
media into the production catalog.
"""

import argparse
from collections import defaultdict
import csv
from datetime import datetime, timezone
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import io
import json
import os
from pathlib import Path
import random
import re
import socket
import subprocess
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "content/research/pokedex_sources.json"
DEFAULT_SNAPSHOT = ROOT / "content/research/pokedex_source_snapshot.json"
MAX_RESPONSE_BYTES = 16 * 1024 * 1024
USER_AGENT = "PokedexResearchSync/1.0 (+offline firmware research)"
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
GENERATION_NAMES = {
    1: "generation-i",
    2: "generation-ii",
    3: "generation-iii",
    4: "generation-iv",
    5: "generation-v",
    6: "generation-vi",
    7: "generation-vii",
    8: "generation-viii",
    9: "generation-ix",
}
EXPECTED_FILES = {
    "species",
    "pokemon",
    "forms",
    "pokedexes",
    "pokedex_memberships",
}


def canonical_json(value):
    return json.dumps(
        value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
    ).encode("ascii")


def sha256(value):
    return hashlib.sha256(value).hexdigest()


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def validate_config(config):
    if config.get("schema_version") != 1:
        raise ValueError("Unsupported source registry schema")
    source = config.get("automated_source")
    if not isinstance(source, dict):
        raise ValueError("Missing automated source")
    repository = urlparse(source.get("repository_url", ""))
    if repository.scheme != "https" or repository.hostname != "github.com":
        raise ValueError("Repository must be an HTTPS GitHub URL")
    branch = source.get("repository_branch")
    if not isinstance(branch, str) or not re.fullmatch(
        r"[A-Za-z0-9._/-]+", branch
    ):
        raise ValueError("Invalid repository branch")
    template = source.get("raw_base_url", "")
    if "{revision}" not in template:
        raise ValueError("Raw URL must contain a revision placeholder")
    raw = urlparse(template.format(revision="0" * 40))
    if raw.scheme != "https" or raw.hostname != source.get("allowed_host"):
        raise ValueError("Raw source must use its allow-listed HTTPS host")
    files = source.get("files")
    if not isinstance(files, dict) or set(files) != EXPECTED_FILES:
        raise ValueError("Unexpected automated file set")
    for filename in files.values():
        if (
            not isinstance(filename, str)
            or Path(filename).name != filename
            or not filename.endswith(".csv")
        ):
            raise ValueError(f"Invalid source filename: {filename!r}")
    interval = source.get("refresh_interval_seconds")
    if not isinstance(interval, int) or interval < 3600:
        raise ValueError("Refresh interval must be at least one hour")
    verifications = config.get("verification_sources")
    if not isinstance(verifications, list) or not verifications:
        raise ValueError("At least one verification source is required")
    for verification in verifications:
        parsed = urlparse(verification.get("url", ""))
        if parsed.scheme != "https" or not parsed.hostname:
            raise ValueError("Verification sources must use HTTPS")


def resolve_revision(source):
    ref = f"refs/heads/{source['repository_branch']}"
    output = subprocess.check_output(
        ["git", "ls-remote", source["repository_url"], ref],
        text=True,
        timeout=30,
    ).strip()
    parts = output.split()
    if len(parts) != 2 or parts[1] != ref or not COMMIT_RE.fullmatch(parts[0]):
        raise ValueError(f"Could not resolve a unique repository revision: {ref}")
    return parts[0]


def source_file_url(source, revision, filename):
    base = source["raw_base_url"].format(revision=revision)
    if not base.endswith("/"):
        base += "/"
    url = base + filename
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname != source["allowed_host"]:
        raise ValueError(f"Source URL escapes allow-list: {url}")
    return url


def fetch_bytes(url, timeout=60, attempts=3):
    for attempt in range(attempts):
        request = Request(
            url,
            headers={"Accept": "text/csv", "User-Agent": USER_AGENT},
        )
        try:
            with urlopen(request, timeout=timeout) as response:
                body = response.read(MAX_RESPONSE_BYTES + 1)
                if len(body) > MAX_RESPONSE_BYTES:
                    raise ValueError(
                        f"Response exceeds {MAX_RESPONSE_BYTES} bytes: {url}"
                    )
                headers = {
                    key.lower(): value
                    for key, value in response.headers.items()
                }
                return body, headers
        except HTTPError as error:
            if error.code < 500 and error.code != 429:
                raise
            failure = error
        except (socket.timeout, TimeoutError, URLError) as error:
            failure = error
        if attempt + 1 < attempts:
            time.sleep((2 ** attempt) + random.random())
    raise RuntimeError(
        f"Source fetch failed after {attempts} attempts: {url}"
    ) from failure


def fetch_many(resources, workers=3):
    output = {}
    with ThreadPoolExecutor(max_workers=workers) as executor:
        pending = {
            executor.submit(fetch_bytes, url): key
            for key, url in resources.items()
        }
        for future in as_completed(pending):
            output[pending[future]] = future.result()
    return output


def parse_csv(body, required_fields):
    text = body.decode("utf-8-sig")
    reader = csv.DictReader(io.StringIO(text))
    fields = set(reader.fieldnames or [])
    missing = set(required_fields) - fields
    if missing:
        raise ValueError(f"CSV is missing fields: {sorted(missing)}")
    return list(reader)


def iso_utc(timestamp=None):
    value = datetime.now(timezone.utc) if timestamp is None else timestamp
    return value.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_observed_at(value):
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ValueError("observed_at must be UTC ISO-8601")
    return datetime.fromisoformat(value[:-1] + "+00:00")


def build_snapshot(config, revision, files, observed_at):
    species_rows = parse_csv(
        files["species"][0], {"id", "identifier", "generation_id"}
    )
    pokemon_rows = parse_csv(
        files["pokemon"][0], {"id", "identifier", "species_id"}
    )
    form_rows = parse_csv(
        files["forms"][0], {"id", "identifier", "pokemon_id"}
    )
    pokedex_rows = parse_csv(
        files["pokedexes"][0], {"id", "identifier", "is_main_series"}
    )
    membership_rows = parse_csv(
        files["pokedex_memberships"][0],
        {"species_id", "pokedex_id", "pokedex_number"},
    )

    species = sorted(
        (
            {
                "id": int(row["id"]),
                "name": row["identifier"],
                "generation_id": int(row["generation_id"]),
            }
            for row in species_rows
        ),
        key=lambda row: row["id"],
    )
    species_ids = [row["id"] for row in species]
    species_names = [row["name"] for row in species]
    expected_ids = list(range(1, len(species) + 1))
    species_id_set = set(species_ids)
    pokemon_ids = [int(row["id"]) for row in pokemon_rows]
    pokemon_id_set = set(pokemon_ids)
    form_ids = [int(row["id"]) for row in form_rows]
    pokedex_ids = [int(row["id"]) for row in pokedex_rows]

    generation_members = defaultdict(list)
    for row in species:
        generation_members[row["generation_id"]].append(row["id"])
    generations = []
    for generation_id in sorted(generation_members):
        ids = generation_members[generation_id]
        generations.append(
            {
                "id": generation_id,
                "name": GENERATION_NAMES.get(
                    generation_id, f"generation-{generation_id}"
                ),
                "species_count": len(ids),
                "species_ids_sha256": sha256(canonical_json(ids)),
            }
        )

    if len(pokedex_ids) != len(set(pokedex_ids)):
        raise ValueError("Duplicate Pokédex IDs")
    pokedex_by_id = {}
    for row in pokedex_rows:
        pokedex_id = int(row["id"])
        pokedex_by_id[pokedex_id] = {
            "id": pokedex_id,
            "name": row["identifier"],
            "is_main_series": row["is_main_series"] == "1",
            "members": [],
        }
    for row in membership_rows:
        pokedex_id = int(row["pokedex_id"])
        if pokedex_id not in pokedex_by_id:
            raise ValueError(f"Unknown Pokédex ID in membership: {pokedex_id}")
        pokedex_by_id[pokedex_id]["members"].append(
            (int(row["pokedex_number"]), int(row["species_id"]))
        )

    pokedexes = []
    national_entries = None
    for pokedex_id in sorted(pokedex_by_id):
        row = pokedex_by_id[pokedex_id]
        members = sorted(row.pop("members"))
        numbers = [number for number, _ in members]
        if len(numbers) != len(set(numbers)):
            raise ValueError(f"Duplicate entry number in Pokédex {row['name']}")
        pokedexes.append(
            {
                **row,
                "entry_count": len(members),
                "entries_sha256": sha256(canonical_json(members)),
            }
        )
        if row["name"] == "national":
            national_entries = members
    if national_entries is None:
        raise ValueError("Source data has no national Pokédex")

    national_numbers = [number for number, _ in national_entries]
    national_species_ids = [species_id for _, species_id in national_entries]
    invariants = {
        "species_ids_contiguous": species_ids == expected_ids,
        "species_names_unique":
            len(species_names) == len(set(species_names)),
        "pokemon_ids_unique":
            len(pokemon_ids) == len(pokemon_id_set),
        "form_ids_unique":
            len(form_ids) == len(set(form_ids)),
        "pokedex_ids_unique":
            len(pokedex_ids) == len(set(pokedex_ids)),
        "forms_reference_known_pokemon":
            all(int(row["pokemon_id"]) in pokemon_id_set for row in form_rows),
        "pokemon_reference_known_species":
            all(
                int(row["species_id"]) in species_id_set
                for row in pokemon_rows
            ),
        "memberships_reference_known_species":
            all(
                int(row["species_id"]) in species_id_set
                for row in membership_rows
            ),
        "national_count_matches_species":
            len(national_entries) == len(species),
        "national_numbers_contiguous":
            national_numbers == expected_ids,
        "national_numbers_match_species_ids":
            national_species_ids == expected_ids,
        "generation_partition_matches_national":
            sorted(
                species_id
                for members in generation_members.values()
                for species_id in members
            )
            == expected_ids,
        "pokedex_names_unique":
            len(pokedexes) == len({row["name"] for row in pokedexes}),
    }
    if not all(invariants.values()):
        failed = [key for key, value in invariants.items() if not value]
        raise ValueError(f"Source census invariants failed: {failed}")

    official = next(
        item
        for item in config["verification_sources"]
        if item["id"] == "pokemon-official-national-tail"
    )
    expected = official["expected"]
    last_species = species[-1]
    if (
        last_species["id"] != expected["national_number"]
        or last_species["name"].casefold()
        != expected["english_name"].casefold()
    ):
        raise ValueError("Structured source disagrees with official tail check")

    source = config["automated_source"]
    source_files = {}
    for key, filename in source["files"].items():
        body, headers = files[key]
        source_files[key] = {
            "filename": filename,
            "bytes": len(body),
            "sha256": sha256(body),
            **{
                name: headers[name]
                for name in ("etag", "last-modified")
                if headers.get(name)
            },
        }
    core = {
        "totals": {
            "species": len(species),
            "pokemon_varieties": len(pokemon_rows),
            "forms": len(form_rows),
            "pokedex_scopes": len(pokedexes),
            "generations": len(generations),
            "pokedex_memberships": len(membership_rows),
        },
        "national_index": {
            "first": {
                "id": species[0]["id"],
                "name": species[0]["name"],
            },
            "last": {
                "id": last_species["id"],
                "name": last_species["name"],
            },
            "ids_sha256": sha256(canonical_json(species_ids)),
            "names_sha256": sha256(canonical_json(species_names)),
        },
        "generation_counts": generations,
        "pokedex_counts": pokedexes,
        "invariants": invariants,
    }
    return {
        "schema_version": 1,
        "observed_at": iso_utc(observed_at),
        "source_registry_sha256": sha256(canonical_json(config)),
        "automated_source": {
            "id": source["id"],
            "authority_tier": source["authority_tier"],
            "repository_url": source["repository_url"],
            "data_commit": revision,
            "files": source_files,
        },
        "official_cross_check": {
            "source_id": official["id"],
            "url": official["url"],
            "verified_on": official["verified_on"],
            **expected,
            "status": "matched",
        },
        **core,
        "data_sha256": sha256(canonical_json(core)),
    }


def validate_snapshot(snapshot, config=None, local_catalog=None):
    if snapshot.get("schema_version") != 1:
        raise ValueError("Unsupported snapshot schema")
    parse_observed_at(snapshot.get("observed_at"))
    totals = snapshot.get("totals", {})
    required_totals = (
        "species",
        "pokemon_varieties",
        "forms",
        "pokedex_scopes",
        "generations",
        "pokedex_memberships",
    )
    for key in required_totals:
        if not isinstance(totals.get(key), int) or totals[key] <= 0:
            raise ValueError(f"Invalid total: {key}")
    if not totals["forms"] >= totals["pokemon_varieties"] >= totals["species"]:
        raise ValueError("Species, variety, and form totals are inconsistent")

    generations = snapshot.get("generation_counts", [])
    if len(generations) != totals["generations"]:
        raise ValueError("Generation count does not match generation rows")
    if sum(row["species_count"] for row in generations) != totals["species"]:
        raise ValueError("Generation species totals do not match national total")

    pokedexes = snapshot.get("pokedex_counts", [])
    if len(pokedexes) != totals["pokedex_scopes"]:
        raise ValueError("Pokédex count does not match Pokédex rows")
    if len({row["name"] for row in pokedexes}) != len(pokedexes):
        raise ValueError("Duplicate Pokédex names")
    national = [row for row in pokedexes if row["name"] == "national"]
    if len(national) != 1 or national[0]["entry_count"] != totals["species"]:
        raise ValueError("National Pokédex total is inconsistent")

    invariants = snapshot.get("invariants")
    if not isinstance(invariants, dict) or not invariants:
        raise ValueError("Missing source invariants")
    if not all(value is True for value in invariants.values()):
        raise ValueError("Snapshot contains failed source invariants")

    core = {
        "totals": totals,
        "national_index": snapshot["national_index"],
        "generation_counts": generations,
        "pokedex_counts": pokedexes,
        "invariants": invariants,
    }
    if snapshot.get("data_sha256") != sha256(canonical_json(core)):
        raise ValueError("Snapshot data hash mismatch")
    commit = snapshot.get("automated_source", {}).get("data_commit", "")
    if not COMMIT_RE.fullmatch(commit):
        raise ValueError("Snapshot has no pinned data commit")

    if config is not None:
        validate_config(config)
        if snapshot.get("source_registry_sha256") != sha256(
            canonical_json(config)
        ):
            raise ValueError("Snapshot was built from another source registry")
        expected = next(
            item["expected"]
            for item in config["verification_sources"]
            if item["id"] == "pokemon-official-national-tail"
        )
        official = snapshot.get("official_cross_check", {})
        if (
            official.get("national_number") != expected["national_number"]
            or official.get("english_name") != expected["english_name"]
            or official.get("status") != "matched"
        ):
            raise ValueError("Official cross-check is missing or stale")

    if local_catalog is not None:
        rows = local_catalog.get("species")
        if not isinstance(rows, list) or not rows:
            raise ValueError("Local species catalog is empty")
        original = [row for row in rows if row.get("origin") == "original"]
        if any(not 60000 <= row["id"] < 65535 for row in original):
            raise ValueError("Original species ID must use reserved range")
        ids = [row["id"] for row in rows if row.get("origin") != "original"]
        if len(rows) != len({row["id"] for row in rows}):
            raise ValueError("Local species IDs are not unique")
        maximum = snapshot["official_cross_check"]["national_number"]
        if any(
            not isinstance(value, int) or value <= 0 or value > maximum
            for value in ids
        ):
            raise ValueError(
                "Local species ID is outside verified national range"
            )
    return True


def snapshot_diff(previous, current):
    if previous is None:
        return {"kind": "initial", "changed": True}
    if previous.get("data_sha256") == current.get("data_sha256"):
        return {"kind": "unchanged", "changed": False}
    total_changes = {}
    for key, value in current["totals"].items():
        before = previous.get("totals", {}).get(key)
        if before != value:
            total_changes[key] = {"before": before, "after": value}
    old_dex = {
        row["name"]: row["entry_count"]
        for row in previous.get("pokedex_counts", [])
    }
    new_dex = {
        row["name"]: row["entry_count"]
        for row in current["pokedex_counts"]
    }
    return {
        "kind": "changed",
        "changed": True,
        "total_changes": total_changes,
        "added_pokedexes": sorted(set(new_dex) - set(old_dex)),
        "removed_pokedexes": sorted(set(old_dex) - set(new_dex)),
        "changed_pokedexes": sorted(
            name
            for name in set(old_dex) & set(new_dex)
            if old_dex[name] != new_dex[name]
        ),
    }


def write_atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=path.parent,
        prefix=path.name + ".",
        delete=False,
    ) as handle:
        handle.write(text)
        temporary = Path(handle.name)
    os.replace(temporary, path)


def is_refresh_due(snapshot, interval, now):
    if snapshot is None:
        return True
    observed = parse_observed_at(snapshot["observed_at"])
    return (now - observed).total_seconds() >= interval


def refresh(config, snapshot_path, force=False):
    previous = load_json(snapshot_path) if snapshot_path.exists() else None
    now = datetime.now(timezone.utc)
    source = config["automated_source"]
    config_hash = sha256(canonical_json(config))
    registry_matches = (
        previous is not None
        and previous.get("source_registry_sha256") == config_hash
    )
    if (
        not force
        and registry_matches
        and not is_refresh_due(
            previous, source["refresh_interval_seconds"], now
        )
    ):
        validate_snapshot(previous, config)
        return previous, {"kind": "not-due", "changed": False}

    revision = resolve_revision(source)
    previous_revision = (
        previous.get("automated_source", {}).get("data_commit")
        if previous
        else None
    )
    if not force and registry_matches and revision == previous_revision:
        validate_snapshot(previous, config)
        return previous, {"kind": "upstream-unchanged", "changed": False}

    resources = {
        key: source_file_url(source, revision, filename)
        for key, filename in source["files"].items()
    }
    files = fetch_many(resources)
    current = build_snapshot(config, revision, files, now)
    validate_snapshot(current, config)
    change = snapshot_diff(previous, current)
    if change["changed"] or not registry_matches:
        write_atomic(snapshot_path, current)
    return current, change


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--refresh",
        action="store_true",
        help="Poll the pinned source repository and update the snapshot.",
    )
    mode.add_argument(
        "--check",
        action="store_true",
        help="Validate the checked-in registry and snapshot without network I/O.",
    )
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--snapshot", type=Path, default=DEFAULT_SNAPSHOT)
    args = parser.parse_args()
    if args.force and not args.refresh:
        parser.error("--force requires --refresh")

    config = load_json(args.config)
    validate_config(config)
    if args.check:
        snapshot = load_json(args.snapshot)
        local_catalog = load_json(ROOT / "content/species.json")
        validate_snapshot(snapshot, config, local_catalog)
        print(
            "Validated Pokédex census: "
            f"species={snapshot['totals']['species']} "
            f"forms={snapshot['totals']['forms']} "
            f"pokedexes={snapshot['totals']['pokedex_scopes']} "
            f"observed_at={snapshot['observed_at']}"
        )
        return

    snapshot, change = refresh(config, args.snapshot, force=args.force)
    print(
        f"Pokédex census {change['kind']}: "
        f"species={snapshot['totals']['species']} "
        f"forms={snapshot['totals']['forms']} "
        f"pokedexes={snapshot['totals']['pokedex_scopes']} "
        f"data_sha256={snapshot['data_sha256']}"
    )
    if change.get("total_changes"):
        print(json.dumps(change, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
