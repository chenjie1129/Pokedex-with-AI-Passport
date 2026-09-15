#!/usr/bin/env python3
"""Validate the six-stage Pokédex program controls without network access."""

from datetime import date
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STATUS_PATH = ROOT / "docs/verification/pokedex-program-status.json"
RISK_PATH = ROOT / "docs/verification/pokedex-program-risk-register.json"
CONTRACT_PATH = ROOT / "docs/verification/pokedex-program-evidence-contract.json"
PROGRAM_PATH = ROOT / "docs/plans/pokedex-six-stage-program.md"


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_status(status):
    require(status.get("schema_version") == 1, "Unsupported status schema")
    require(status.get("overall_status") == "active", "Program is not active")
    require(status.get("branch") == "feat/pokemon-pokedex", "Wrong branch")
    require(bool(status.get("accountable")), "Missing accountable owner")
    stages = status.get("stages")
    require(isinstance(stages, list), "Stages must be a list")
    require([stage.get("id") for stage in stages] == list(range(1, 7)),
            "Stages must be ordered 1 through 6")
    current = status.get("current_stage")
    require(current == 2, "Stage 2 must be the active implementation stage")
    in_progress = [
        stage["id"] for stage in stages if stage.get("status") == "in_progress"
    ]
    require(in_progress == [current], "Exactly the current stage must be active")

    previous_date = None
    allowed_statuses = {"completed", "in_progress", "planned", "blocked"}
    for stage in stages:
        stage_id = stage["id"]
        require(stage.get("status") in allowed_statuses,
                f"Stage {stage_id} has an invalid status")
        require(bool(stage.get("owner")), f"Stage {stage_id} has no owner")
        target = date.fromisoformat(stage["target_date"])
        require(previous_date is None or target >= previous_date,
                "Stage target dates must be nondecreasing")
        previous_date = target
        progress = stage.get("progress_percent")
        require(isinstance(progress, int) and 0 <= progress <= 100,
                f"Stage {stage_id} progress is invalid")
        require(bool(stage.get("acceptance")),
                f"Stage {stage_id} has no acceptance criteria")
        expected_prerequisites = [] if stage_id == 1 else [stage_id - 1]
        require(stage.get("prerequisites") == expected_prerequisites,
                f"Stage {stage_id} prerequisites are not serial")
        if stage_id < current:
            require(stage["status"] == "completed" and progress == 100,
                    f"Stage {stage_id} must be complete")
        elif stage_id > current:
            require(stage["status"] in {"planned", "blocked"} and progress == 0,
                    f"Future stage {stage_id} cannot report implementation")
        if stage["status"] == "blocked":
            require(bool(stage.get("blocked_by")),
                    f"Blocked stage {stage_id} has no blocker")
    for stage_id in (4, 5, 6):
        require(stages[stage_id - 1]["status"] == "blocked",
                f"Stage {stage_id} must remain product/capacity gated")


def validate_risks(register):
    require(register.get("schema_version") == 1, "Unsupported risk schema")
    require(bool(register.get("review_cadence")), "Missing risk cadence")
    risks = register.get("risks")
    require(isinstance(risks, list) and risks, "Risk register is empty")
    ids = [risk.get("id") for risk in risks]
    require(len(ids) == len(set(ids)), "Risk IDs must be unique")
    required = {
        "id",
        "title",
        "severity",
        "probability",
        "owner",
        "affected_stages",
        "trigger",
        "mitigation",
        "status",
    }
    for risk in risks:
        missing = required - set(risk)
        require(not missing, f"Risk {risk.get('id')} missing {sorted(missing)}")
        require(risk["severity"] in {"low", "medium", "high", "critical"},
                f"Risk {risk['id']} has invalid severity")
        require(risk["probability"] in {"low", "medium", "high"},
                f"Risk {risk['id']} has invalid probability")
        require(bool(risk["owner"]) and bool(risk["trigger"])
                and bool(risk["mitigation"]),
                f"Risk {risk['id']} is not actionable")
        require(
            all(isinstance(stage, int) and 1 <= stage <= 6
                for stage in risk["affected_stages"]),
            f"Risk {risk['id']} references an invalid stage",
        )


def validate_contract(contract):
    require(contract.get("schema_version") == 1, "Unsupported contract schema")
    require(contract.get("state") == "active", "Evidence contract is not active")
    require(contract.get("max_attempts") == 2, "Repair budget must remain fixed")
    intent = contract.get("intent", {})
    for key in ("objective", "constraints", "assumptions", "non_goals",
                "ambiguities", "risk_tier"):
        require(key in intent, f"Evidence intent missing {key}")
    checks = contract.get("checks")
    claims = contract.get("claims")
    require(isinstance(checks, list) and checks, "Evidence checks are empty")
    require(isinstance(claims, list) and claims, "Evidence claims are empty")
    check_ids = {check.get("id") for check in checks}
    require(len(check_ids) == len(checks), "Evidence check IDs are not unique")
    for claim in claims:
        require(bool(claim.get("statement")), "Claim statement is empty")
        require(bool(claim.get("evidence")), "Claim has no evidence")
        require(set(claim["evidence"]) <= check_ids,
                f"Claim {claim.get('id')} references an unknown check")


def main():
    status = load(STATUS_PATH)
    risks = load(RISK_PATH)
    contract = load(CONTRACT_PATH)
    validate_status(status)
    validate_risks(risks)
    validate_contract(contract)
    program = PROGRAM_PATH.read_text(encoding="utf-8")
    for stage_id in range(1, 7):
        require(f"阶段 {stage_id}" in program,
                f"Program document is missing stage {stage_id}")
    print(
        "Pokédex program controls valid: "
        f"current_stage={status['current_stage']} "
        f"risks={len(risks['risks'])} "
        f"claims={len(contract['claims'])}"
    )


if __name__ == "__main__":
    main()
