"""Exercise original driver -> typed buffer -> servo against resident characters.

Uses the existing bank inventory. Stage performers only; crowd has a separate
model layout/lifecycle. No render, controller/IK, or retail motion-parity claim.
"""
import argparse
import concurrent.futures
import hashlib
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--hdr", required=True)
    parser.add_argument("--ark", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = json.loads(args.inventory.read_text(encoding="utf-8"))["rows"]

    def run(row):
        bank = row["source"].replace("\\", "/")
        role = bank.split("/char/", 1)[-1].split("/")[0] if "/char/" in bank else bank.split("/")[1]
        if role in {"crowd", "gh1_crowd"}:
            return {"bank": bank, "skip": "crowd model assembly is outside this performer test"}
        char_base = bank.split("/anims/", 1)[0]
        model = f"{char_base}/og/gen/{role}.milo_ps2"
        loose = Path(bank).is_file()
        result = subprocess.run([str(args.test_exe), "--asset", "" if loose else args.hdr,
                                 "" if loose else args.ark, model, bank],
                                capture_output=True, text=True, encoding="utf-8", errors="replace",
                                creationflags=subprocess.CREATE_NO_WINDOW)
        lines = [line for line in result.stdout.splitlines() if line.startswith("ASSET\t")]
        output = {"bank": bank, "model": model, "exit_code": result.returncode, "pass": False}
        if result.returncode == 0 and len(lines) == 1:
            fields = lines[0].split("\t")
            output.update({"pass": True, "clips": int(fields[3]), "frames": int(fields[4]),
                           "allocated_rows": int(fields[5]), "dummy_rows": int(fields[6]),
                           "max_owner_position_step": float(fields[7])})
        else:
            output["error"] = (result.stderr + result.stdout)[-1800:]
        return output

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for row in pool.map(run, rows):
            results.append(row)
            print(("SKIP" if "skip" in row else "PASS" if row["pass"] else "FAIL") + " " + row["bank"], flush=True)
    tested = [row for row in results if "skip" not in row]
    report = {"scope": "real performer locals, root decode, all listed bank clips at 241 driver steps; not visual/retail/controller parity",
              "test_sha256": hashlib.sha256(args.test_exe.read_bytes()).hexdigest(),
              "all_pass": all(row["pass"] for row in tested), "tested_banks": len(tested),
              "skipped_crowd_banks": len(results) - len(tested),
              "clips": sum(row.get("clips", 0) for row in tested),
              "driver_steps": sum(row.get("frames", 0) for row in tested), "rows": results}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{sum(row['pass'] for row in tested)}/{len(tested)} performer banks passed; {report['clips']} clips; {report['driver_steps']} driver steps")
    return 0 if report["all_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
