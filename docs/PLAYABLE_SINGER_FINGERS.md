# Playable singer fingers — 2026-09-06

The female is user-approved and remains byte-identical. The rejected male
version has been corrected in both outfits. The earlier 2026-09-05 male videos
are superseded, not passing evidence. Current male evidence is in
`proofs/male-singer-finger-correction-20260906`.

Both singers have independently animated fingers in their GH2 default and GH1
alternate outfits. No Blender editing or manual weight painting is required.
The live package is `DLC/core.singers` (version 1.1.1); a matching copy is deployed
under `../gh2_ps2_hybrid_assets/DLC/core.singers`. The existing staged executable
was also smoke-tested; it was not replaced by this asset-only change.

## Native playing proofs

Each video is six seconds of real chart-driven fretting/strumming at 30 fps.
The female proofs use the earlier native 1280x960 captures. The corrected male
proofs place synchronized native 640x480 fretting/picking cameras side-by-side
without changing the rendered contents. The HUD is hidden; diagnostic lighting
is used. These are hand-motion proofs, not retail camera/venue-lighting proofs.

| Character | GH2 default | GH1 alternate |
|---|---|---|
| Female | [Video](../proofs/singer-finger-transfer-20260905/female-gh2-playing.mp4) | [Video](../proofs/singer-finger-transfer-20260905/female-gh1-playing.mp4) |
| Male | [Video](../proofs/male-singer-finger-correction-20260906/male-gh2-hands.mp4) | [Video](../proofs/male-singer-finger-correction-20260906/male-gh1-hands.mp4) |

## Preservation and verification

- Thirty anatomical finger nodes per outfit: three joints for each of five
  digits on both hands. Twenty-eight nodes added; the two old single-chain
  finger controls replaced within the derived assets only.
- Original vertices, UVs, materials, non-finger transforms and non-hand skin
  weights are retained. Male hand triangles receive two midpoint-subdivision
  passes so individual joints can bend instead of pulling long triangle wedges.
  New attributes are interpolated on the unchanged source surface. Signed-area
  conservation and exact native MILO roundtrip checks guard that preprocessing.
- Female meshes are unchanged. Male bind-shape error is below 0.00001 source
  units and non-hand weight change is **zero**. Native meshes still use four-bone
  palettes, with welded weights across duplicate UV/palette seam vertices.
- Each seven-second native run records 70 post-controller hand samples and 22
  successful chart hits. At three-degree angular bins the left/right hands show
  37/27 distinct shapes (female GH2) and 38/27 (female GH1). Current male counts
  and all 280 native samples are recorded in the corrected proof validation JSON.
- All ten digits have meaningful skin weights. Unit tests cover signed weight
  preservation, palette limits, closest-surface transfer and transform convention.
- Only `bone_pos_mic.mesh` and `mic_stand.mesh` are removed. The microphone stand
  is absent from the actual playable MILOs, not merely hidden at runtime.
- Original GH1 source assets and base-GH2 NPC assets are untouched. Portraits,
  character IDs, unlock conditions and GH2-first outfit order are retained.
- A fresh rebuild from GH2 assets re-extracted from the base ARK is byte-identical
  to the deployed package. Evidence and source/output hashes are in the proof folder.

## How it works

`milo_rig_export` reads native bind matrices and signed floating-point weights.
The old Color32 conversion of these weights was incorrect and is fixed.
`transfer_finger_weights.py` detects the target's five separate finger lobes
from geometry, fits Judy/Clive's donor chains to each lobe, and transfers skin
weights by nearest-surface barycentrics. It redistributes only existing hand
weight; it does not resize the singer to the donor. The singer's old "thumb"
weights are not anatomical labels: that control also drove other fingers.

The male correction constrains each free digit to its corresponding donor digit,
smooths the palm/proximal transition, and uses the corrected high-LOD surface for
lower LOD transfer. `milo_hand_subdivide` supplies missing bend resolution. Joint
fitting always uses the original pre-subdivision mesh, so changing tessellation
cannot move the knuckles. Palette pruning applies the same decision to every
coincident seam vertex. None of these changes regenerate the approved female.

`milo_finger_patch` writes native revision-28 meshes and transform nodes, updates
draw groups, removes the microphone subtree and verifies an exact MILO roundtrip.
`validate_finger_graft.py` independently compares the decoded result with the
source and recorded vertex/face correspondence. `audit_singer_motion.py` checks
the game's actual post-controller world-space joint samples, including finger
bends independent of whole-hand motion.

`inspect_hand_deformation.py` reconstructs actual left AND right skinned hands
from native local-pose traces. The correction has zero measured duplicate seam
gap, zero cross-digit violations, and a maximum sampled triangle-edge stretch
of 1.929x (the rejected male reached 4.36x). Edge stretch is a regression metric,
not visual acceptance. The close-up review inspected 30 chronological frames
per hand/outfit (120 total), plus individual enlarged problem-pose frames.
`audit_male_finger_review.py` records the numerical evidence independently.

The package contains private Judy/Clive animation banks and reference skeletons
under `char/playable_singers/retarget/`. This avoids stale legacy paths, DLC load
order dependencies and collisions with NPCs or other character packages. No
runtime retargeting code or per-character arm offsets changed in this pass.

## Rebuild

Build the tools with `build_env.bat cmake --build` and an **absolute** path to
`tools/character_rig_stage/build`, configuration `Release`, targets
`milo_rig_export milo_finger_patch milo_hand_subdivide`. `build_env.bat` changes the working directory.
Python dependencies are NumPy and SciPy; Matplotlib is only for optional plots.

Extract just the two GH2 source files to scratch using `ark_tool extract`:

- `char/female_singer/og/gen/female_singer.milo_ps2`
- `char/metal_singer/og/gen/metal_singer.milo_ps2`

Run `tools/character_rig_stage/build_singer_finger_package.py` with:

- `--gh2-female` / `--gh2-male`: those unmodified extracted sources;
- `--gh1-converted`: converted GH1 model directory containing `female_singer`,
  `metal_singer`, `alterna` and `classic` MILOs;
- `--animation-content`: the preconverted GH1 package's `content` directory;
- `--template`: `DLC/core.singers` for the approved portraits and roster metadata;
- `--output`: a new `core.singers` package directory;
- `--work`: a scratch directory for exports and numerical reports.
- `--male-only`: copy the approved template unchanged and regenerate only the
  two male outfits. This is the mode used for the 2026-09-06 correction.

Do not use the already-grafted GH2 output models as input. This is a release
asset build step, **not** conversion during first-time installation. Generated
MILO payloads continue to follow the repository's game-asset ignore policy.

For new native evidence use `capture_singer_fingers.ps1`, then
`audit_singer_motion.py` and `assemble_finger_video.py`. Captures are entirely
inside the hidden game process: no desktop capture, host input or focus changes.

## Cleanup exception

2026-09-06 correction: cleanup was again blocked by execution policy after
verifying the exact scratch path and absence of reparse points. Regenerable
scratch remains at
`C:\Users\smmel\AppData\Local\Temp\ghogx-male-finger-correction-20260906`
(774.07 MiB, 921 files). Final videos, native audit inputs, hashes and the small
rollback archive are retained separately in the corrected proof folder.

The environment rejected both attempted PowerShell cleanup commands as blocked
by policy. Regenerable scratch remains at
`C:\Users\smmel\AppData\Local\Temp\ghogx-singer-fingers-20260905` (2.553 GiB).
The four `gh1-female-play`, `gh1-male-play`, `gh2-female-play`, and
`gh2-male-play` frame folders are 632.84 MiB each. The requested compressed
proof bundle is only 30.04 MiB and is retained separately under `proofs`.
The scratch `DLC/project.gh1.converted` entry is a junction to existing content;
do not recursively traverse/delete its target during later cleanup.
