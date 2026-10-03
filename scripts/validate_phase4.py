#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12 contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate exported Xbox probe evidence, never emulate a GPU or certify games."""

import argparse
import json
from pathlib import Path
import sys


class EvidenceError(ValueError):
    pass


def load_jsonl(path):
    records = []
    with Path(path).open(encoding="utf-8-sig") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except ValueError as error:
                raise EvidenceError(f"{path}:{line_number}: invalid JSON") from error
            if not isinstance(record, dict):
                raise EvidenceError(f"{path}:{line_number}: expected object")
            records.append(record)
    if not records:
        raise EvidenceError(f"{path}: empty evidence")
    return records


def fields(record):
    details = record.get("details")
    if not isinstance(details, str):
        raise EvidenceError("missing string details")
    result = {}
    for item in details.split(";"):
        if "=" not in item:
            continue  # Older probes also contain descriptive prose.
        key, value = item.split("=", 1)
        if key in result:
            raise EvidenceError(f"duplicate detail field: {key}")
        result[key] = value
    return result


CONTRACTS = {
    "uwp-presentation": {"shader_format": "DXIL", "shader_compiler": "DXC"},
    "d3d12-shaders": {
        "guest_isa": "0", "shader_format": "DXIL",
        "translation_location": "xbox_runtime", "expected": "100,101,102,103",
        "reference_hlsl_passed": "1", "translated_readback_passed": "1",
    },
    "d3d12-shader-push-data": {
        "abi": "Shader.PushData", "abi_bytes": "120", "root_words": "30",
        "cbv_register": "0", "register_space": "0", "ud_regs": "16",
        "packed_buffer_offsets": "40", "upstream_emitter_linked": "0",
        "expected": "1066,1067,1068,1069", "readback_passed": "1",
        "translation_location": "xbox_runtime", "shader_format": "DXIL",
    },
    "d3d12-shader-upstream": {
        "emitter": "Shader.Backend.SPIRV.EmitSPIRV", "upstream_emitter_linked": "1",
        "source": "authored_shadps4_ir", "guest_isa": "0", "guest_runtime_linked": "0",
        "translation_location": "xbox_runtime", "shader_format": "DXIL",
        "root_words": "30", "local_size": "2x2x1", "storage_image": "R32_UINT",
        "expected": "100,101,102,103", "readback_passed": "1",
    },
    "d3d12-shader-graphics": {
        "emitter": "Shader.Backend.SPIRV.EmitSPIRV", "source": "authored_shadps4_ir",
        "guest_isa": "0", "guest_runtime_linked": "0", "translation_location": "xbox_runtime",
        "stages": "vertex,fragment", "shader_format": "DXIL", "varying_location": "0",
        "varying": "float4", "vs_cbv": "b0_space1", "ps_cbv": "b0_space2",
        "root_words": "60", "draws": "2", "pixels_per_draw": "16",
        "expected_rgba": "255,0,0,255|0,255,0,255", "readback_passed": "1",
    },
    "d3d12-resources": {"failed_allocations": "0", "upload_bytes": "0", "readback_bytes": "0"},
    "lifecycle-suspend": {"gpu_drained": "1"},
}
REQUIRED = set(CONTRACTS) | {
    "d3d12-device", "d3d12-pipelines", "d3d12-transfers", "d3d12-videocore", "lifecycle-journal",
}


def validate(results, journal, require_resume=False):
    probes = {}
    for record in results:
        name = record.get("probe")
        if not isinstance(name, str) or not name or name in probes:
            raise EvidenceError("missing or duplicate probe name")
        if record.get("passed") is not True or type(record.get("win32_error")) is not int or record["win32_error"] != 0:
            raise EvidenceError(f"probe failed or invalid status: {name}")
        probes[name] = fields(record)
    missing = REQUIRED - probes.keys()
    if missing:
        raise EvidenceError(f"missing probes: {', '.join(sorted(missing))}")
    for name, contract in CONTRACTS.items():
        for key, expected in contract.items():
            if probes[name].get(key) != expected:
                raise EvidenceError(f"{name}: {key} must be {expected}")

    states, resumed, presented = {}, set(), set()
    for record in journal:
        session, event = record.get("session"), record.get("event")
        if not isinstance(session, str) or not session or not isinstance(event, str):
            raise EvidenceError("invalid lifecycle record")
        if not isinstance(record.get("details"), str):
            raise EvidenceError("invalid lifecycle details")
        if event == "launch":
            if session in states:
                raise EvidenceError("duplicate lifecycle launch")
            states[session] = "active"
        elif session not in states:
            raise EvidenceError("lifecycle event without launch")
        elif event == "suspend":
            if states[session] == "suspended":
                raise EvidenceError("duplicate suspend without resume")
            states[session] = "suspended"
        elif event == "resume":
            if states[session] != "suspended":
                raise EvidenceError("resume without preceding suspend")
            states[session] = "resumed"
            resumed.add(session)
        elif event == "navigation" and states[session] == "resumed" and "presented" in record["details"]:
            presented.add(session)
    session = probes["lifecycle-journal"].get("session")
    if session not in states or probes["lifecycle-suspend"].get("session") != session:
        raise EvidenceError("results/journal session mismatch")
    if not any(r.get("session") == session and r.get("event") == "suspend" for r in journal):
        raise EvidenceError("results suspension absent from journal")
    if require_resume and not resumed:
        raise EvidenceError("no same-process suspend/resume in journal")
    return {
        "scope": "phase4_initial_authored_ir_only", "passed": True,
        "probes_passed": len(probes), "results_session": session,
        "resume_sessions": sorted(resumed), "presentation_after_resume_sessions": sorted(presented),
        "warnings": [] if presented else ["resume event is not proof of post-resume presentation"],
        "guest_shader_support": False, "vulkan_d3d12_golden_comparison": False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", required=True, type=Path)
    parser.add_argument("--lifecycle", required=True, type=Path)
    parser.add_argument("--require-resume", action="store_true")
    args = parser.parse_args(argv)
    try:
        report = validate(load_jsonl(args.results), load_jsonl(args.lifecycle), args.require_resume)
    except (OSError, UnicodeError, EvidenceError) as error:
        print(f"phase4 evidence rejected: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
