"""Exercise production sample transport for every clip/sample in named banks.

One hidden native process per bank keeps cache memory bounded. No disc writes,
extractions, native-window inputs or bulk capture files; retain compact JSON.
"""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--hdr", type=Path, required=True)
    parser.add_argument("--ark", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    banks = json.loads(args.inventory.read_text(encoding="utf-8"))["rows"]
    results = []
    for i, bank in enumerate(banks):
        path = bank["source"]
        loose = Path(path).is_file()
        run = subprocess.run([str(args.test_exe), "--all-pages",
                              "" if loose else str(args.hdr),
                              "" if loose else str(args.ark), path],
                             capture_output=True, text=True, encoding="utf-8", errors="replace",
                             timeout=180, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        matches = [line.split("\t") for line in run.stdout.splitlines() if line.startswith("PAGES\t")]
        passed = run.returncode == 0 and len(matches) == 1 and matches[0][1] == path
        item = {"source": path, "loose": loose, "pass": passed, "exit_code": run.returncode}
        if passed:
            for key, value in zip(("clips", "nonempty_pages", "samples", "source_bytes", "clip_phases"), matches[0][2:]):
                item[key] = int(value)
        else:
            item["diagnostic"] = run.stderr[-1600:]
        results.append(item)
        print(f"{i+1}/{len(banks)} {'PASS' if passed else 'FAIL'} {path}", flush=True)
    result = {"scope": "all catalog clips and all raw samples in listed banks; native buffer transport, NOT visual/retail motion parity",
              "native_test_sha256": hashlib.sha256(args.test_exe.read_bytes()).hexdigest(),
              "all_pass": all(row["pass"] for row in results),
              "banks": len(results), "passed": sum(row["pass"] for row in results),
              "clips": sum(row.get("clips", 0) for row in results),
              "clip_phases": sum(row.get("clip_phases", 0) for row in results),
              "samples": sum(row.get("samples", 0) for row in results), "rows": results}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Completed: {result['passed']}/{result['banks']} banks, {result['clips']} clips, {result['samples']} samples", flush=True)
    return 0 if result["all_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
