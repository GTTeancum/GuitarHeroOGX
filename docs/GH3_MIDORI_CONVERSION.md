# Retired GH3 Midori to GH2 Casey experiment

For the current PS2 source-rig pipeline and both-outfit action conversion, see
[GH3 PS2 conversion](../tools/GH3_PS2_CONVERSION.md). The experiment below is retired.

## Status

The repository contains one retired Midori outfit experiment at
`DLC/community.gh3.midori`. It is neither a usable GuitarHeroClassic conversion
nor a completed retail GH2 PS2 release. The final clone gameplay proof was
explicitly rejected because the legs float, the arms stretch, and the pose is
unnatural. Passing structural, animation-call, hash, and runtime checks did not
produce coherent deformation.

The conversion preserves Casey Lynch's native GH2 `rock1` BandCharacter and
controller graph while replacing the visible render payload and ten body
performance clips. Midori remains selectable as `gh3_midori_1`; the packaged
MILO payload itself carries the Casey runtime contract.

Outfit 2 was intentionally excluded. Work stopped with outfit 1 rejected.

## Model contract

The current model has:

- 45 active Midori meshes;
- 169 total meshes, with 124 Casey template meshes retained but hidden;
- 115 Casey transforms and 20 Casey controllers;
- two hand IK controllers, fret IK, eyes, and Casey's BandCharacter root;
- PS2-bounded 256x256 indexed textures;
- no more than four bone influences per emitted mesh palette; and
- static face behavior for this phase.

The active Midori model SHA-256 is
`C8B7BE6DEF202AB60B0E71563924B11C687DD8C947A7535436E19D81B8CEA286`.

## Automated rebuild

The final conversion path is checked into `tools/`; it no longer depends on a
parent Codex workspace or on the discarded experiment scripts. The entry point
is `tools/gh3_midori_build_casey_clone_package.py`. It runs at Windows Idle
priority, limits numerical libraries to one worker, and does not use an ISO or
emulator as part of the loose-package workflow.

Content-derived source data is intentionally not embedded in the tool. Stage
the canonical intermediates under `out/midori/input`:

```text
out/midori/input/
  GEN/MAIN.HDR
  GEN/MAIN_0.ARK
  midori_source_ir_manifest.json
  textures/midori_1_539357ac.png
  outfit1_mesh_ir/midori_1.mesh_ir.json
  gh3_guitarist_midori.skeleton_ir.json
  gh2_casey_rock1_rig.json
  rock1.casey_template.milo_ps2
  stock_casey_banks/rock1_main.milo_ps2
  stock_casey_banks/rock1_ui.milo_ps2
  stock_casey_banks/rock1_fret.milo_ps2
  stock_casey_banks/rock1_strum.milo_ps2
  casey_native_animation_set_v1/final_acp/*.acp
  casey_native_animation_set_v1/final_reports/*.report.json
```

`rock1.casey_template.milo_ps2` is the byte-exact retail
`char/rock1/og/gen/rock1.milo_ps2` entry. The four stock banks are likewise
byte-exact Casey entries from the supplied extracted GH2 `GEN` archive. The
mesh/skeleton IR may be produced through the GLB comparison bridge; the final
MILO writer consumes structured data rather than Blender state.

After building `milo_convert_tool`, rebuild both converted payloads and the
loose package with one command:

```powershell
python tools/gh3_midori_build_casey_clone_package.py `
  --rebuild-model --rebuild-main --overwrite
```

The command regenerates the wrist-weighted model and ten-clip main bank,
copies the three unchanged Casey hand/UI banks, checks exact expected hashes,
checks the 45-visible/115-transform/20-controller model contract, and compares
all 157 clip calls plus all 30 groups against the supplied retail Casey banks.
Outputs and reports go under `out/midori`; the deployable loose package remains
`DLC/community.gh3.midori`.

## Static retail overlay

Retail GH2 does not consume the clone's addon manifest. The same authenticated
payloads can be staged, without constructing an ISO, at Casey's five native
archive paths:

```powershell
python tools/gh3_midori_build_casey_clone_package.py `
  --stage-retail-overlay --overwrite --require-clone-proof

python tools/gh3_midori_build_casey_clone_package.py `
  --verify-only --require-retail-overlay --require-clone-proof
```

The first command writes `out/midori/retail_casey_overlay/char/rock1/...`.
The overlay contains exactly `rock1.milo_ps2`, `rock1_main.milo_ps2`,
`rock1_ui.milo_ps2`, `rock1_fret.milo_ps2`, and `rock1_strum.milo_ps2` at
their native GH2 paths. It contains no addon manifest, archive, disc image, or
emulator configuration. The second command re-hashes every file against the
conversion sources and requires the hash-bound clone proof. Building or
executing a retail image remains a separate deferred gate.

The stage command also performs a read-only audit of the supplied retail GH2
`MAIN.HDR` / `MAIN_0.ARK`. It requires all five Casey paths to exist with exact
case, verifies the staged stock Casey files byte-for-byte against those archive
entries, and confirms that only the model and main bank change while UI, fret,
and strum remain stock. It writes the `ark_tool overlay` input manifest to
`out/midori/retail_casey_overlay.tsv` and records the projected appended size
without modifying the source archive. Verify-only mode requires both this TSV
and its source-archive report to remain byte-current.

The final proof is recorded with `--user-acceptance rejected`. Only a future,
explicitly approved replacement proof may use `accepted`; until then the
copied-archive gate must remain closed:

```powershell
python tools/gh3_midori_apply_casey_retail_overlay.py --apply
python tools/gh3_midori_apply_casey_retail_overlay.py --verify-only
```

The apply command refuses pending and rejected proofs, refuses to alias the
supplied source archive, copies `MAIN.HDR` and `MAIN_0.ARK` under
`out/midori/retail_gate/GEN`, applies the authenticated TSV with `ark_tool`,
and reads all five Casey entries back to verify exact replacement hashes. It
still creates no ISO and invokes no emulator. The multi-gigabyte copy and
overlay are intentionally not run until the visual gate is accepted. The
default ARK tool path is `tools/ark/build/Release/ark_tool.exe`; use
`--ark-tool` when the local build is elsewhere.

## Animation contract

The published banks preserve the complete Casey call surface:

| Bank | Calls | Midori replacements |
| --- | ---: | ---: |
| Main | 113 | 10 |
| UI | 2 | 0 |
| Strum | 17 | 0 |
| Fret | 25 | 0 |
| Total | 157 | 10 |

All 30 Casey `CharClipGroup` names and memberships remain available. The main
bank SHA-256 is
`F7B202330E9233378BA55C896845DEEF1923C885174B7C0CF49097C115C17002`.

## Wrist seam repair

The rejected prototype moved wrist vertices toward one sampled hand pose. That
approach could close one frame while reopening the sleeve-to-hand seam under a
different hand transform.

The current conversion instead transfers skin weight at source-conversion
time. On each arm it modifies only forearm-only vertices in the distal 20
percent of the authored elbow-to-hand bind axis. Each eligible vertex
approaches the nearest authored forearm/hand transition blend before mesh
palette partitioning. The operation changes 42 source vertices per side while
leaving positions, normals, UVs, source vertex identity, and all 4,992 source
faces unchanged.

A ten-clip sweep tests five poses per clip. All 50 poses pass the source-copy,
weight-scope, normalization, four-bone, Casey-controller, and seam-distance
gates. No screenshot is involved in that test.

## Local verification

Build the affected targets through the repository's MSVC environment wrapper.
Then verify the packaged model and all animation banks against extracted GH2
assets:

```powershell
& "$PWD\build_env.bat" cmake --build `
  engine\out\build\win-amd64-release `
  --config Release --parallel 1 `
  --target ghogx_character_variant_catalog_test

& engine\out\build\win-amd64-release\src\ui\ghogx_character_variant_catalog_test.exe `
  --midori-assets-only C:\path\to\GEN\MAIN.HDR `
  C:\path\to\GEN\MAIN_0.ARK "$PWD\DLC"
```

The expected summary is:

```text
ghogx_character_variant_catalog_test: PASS (Midori external assets: 1 model, 1 texture, 157 clips)
```

`ghogx_character_pose_export` can sample the loose model and animation banks
headlessly and can emit both JSONL transforms and a deformed mesh snapshot for
data-level comparison.

The bounded clone proof runner keeps both screenshot and no-image preflight
runs hidden, applies Windows Idle priority, limits numerical libraries to one
worker, and terminates the clone if its timeout expires. A preflight uses the
same conversion package, pose, venue, and camera path as a later capture:

```powershell
python tools/gh3_midori_run_casey_clone_pose.py `
  --log out/midori/proof/casey_pose_preflight.log

python tools/gh3_midori_run_casey_clone_pose.py `
  --log out/midori/proof/casey_pose_capture.log `
  --screenshot out/midori/proof/casey_pose_capture.bmp `
  --validation-output `
    out/midori/proof/midori_casey_wristweights_gameplay.validation.json
```

The runner requires a bounded frame count, confirms a hidden D3D9 window,
authenticates the 169-mesh/45-visible/115-transform Midori package, verifies
the requested animation sample and venue, and requires live gameplay with at
least one chart hit and no misses. It refuses ISO paths and never invokes an
emulator. Add `--verify-existing` with the original run parameters to
authenticate an existing log and screenshot without launching the clone. A
validation output binds the screenshot and runtime log to the exact packaged
model and main-bank hashes while recording the explicit `pending`, `accepted`,
or `rejected` visual verdict. The
package verifier re-hashes both referenced artifacts and fails closed if either
file is missing or has changed.

Require that proof when checking the package:

```powershell
python tools/gh3_midori_build_casey_clone_package.py `
  --verify-only --require-clone-proof
```

The promoted Python contract tests are independent of commercial assets:

```powershell
python tools/test_gh3_midori_conversion.py -v
```

## Runtime boundary

Gameplay verification used `ghogx_app`, the in-repository loose DLC package,
and extracted GH2 `GEN` assets. The latest image is a hard visual failure, not
a review candidate. ISO construction, ISO mounting, and emulator execution are
not part of the iteration path.

No retail overlay was applied and no retail GH2 PS2 build/run was attempted.
The loose package must never be treated as permission to apply the retail
overlay or run a final game image. See `MIDORI_RETIREMENT_NOTES.md` before
resuming this experiment.
