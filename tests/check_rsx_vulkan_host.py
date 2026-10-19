#!/usr/bin/env python3
"""Exercise the real authored host; never count it as title/gameplay proof."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--missing-translation", action="store_true")
    parser.add_argument("--require-hardware", action="store_true")
    parser.add_argument("--validation", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.missing_translation and (args.validation or args.require_hardware):
        parser.error("missing-translation mode does not exercise a GPU or validation")
    exe = args.executable.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PS3RECOMP_VK_GUEST_PROGRAMS"] = "1"
    # This contract checks offscreen rendering. Window acceptance is separate.
    env.pop("PS3RECOMP_VK_WINDOW", None)
    if args.validation:
        env["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
        env["VK_LOADER_DEBUG"] = "layer"
    scenes = [()] if args.missing_translation else [
        (), ("--draw",), ("--depth",), ("--quads",), ("--shader",),
        ("--tex",), ("--tex-io",), ("--mip",), ("--rtt",),
        ("--depthtex",), ("--mrt",), ("--mrt-a",), ("--cube",),
        ("--depthbounds",),
    ]
    results = []
    for scene in scenes:
        name = scene[0].removeprefix("--") if scene else "clear"
        proc = subprocess.run([str(exe), "--frames=2", *scene], env=env,
                              capture_output=True, text=True, timeout=45)
        log = proc.stdout + proc.stderr
        (args.output / (name + ".log")).write_text(log, encoding="utf-8")
        if args.missing_translation:
            assert proc.returncode != 0, "missing translator was accepted"
            assert "requested but HLSL to SPIR-V translation is unavailable" in log
            assert "presented" not in log, "missing capability produced frames"
        else:
            assert proc.returncode == 0, f"{name}: exit {proc.returncode}; see log"
            assert "register-file draw engine path" in log, f"{name}: wrong path"
            assert "nothing to check" not in log and "skipping" not in log.lower(), name
            assert "VUID" not in log and "Validation Error" not in log, name
            assert " FAIL" not in log, name
            selected = re.search(r"using device (\d+): ([^\r\n]+)", log)
            assert selected, f"{name}: selected device not reported"
            device = re.search(r"device " + selected[1] + r": [^\r\n]+\(type (\d+),", log)
            assert device, f"{name}: device type not reported"
            if args.require_hardware:
                assert int(device[1]) in (1, 2), f"{name}: software/virtual device"
            if args.validation:
                assert re.search(r"Insert[^\r\n]*VK_LAYER_KHRONOS_validation", log), \
                    f"{name}: validation layer insertion not observed"
        result = {"scene": name, "exit": proc.returncode}
        if not args.missing_translation:
            result.update(device=selected[2], device_type=int(device[1]))
        results.append(result)
        print(f"{name}: {'expected capability rejection' if args.missing_translation else 'passed'}")
    report = {
        "executable": str(exe), "sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
        "missing_translation": args.missing_translation,
        "hardware_required": args.require_hardware, "validation_required": args.validation,
        "results": results,
        "limit": "Authored renderer contracts; no title assets, gameplay or FPS acceptance.",
    }
    (args.output / "summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
