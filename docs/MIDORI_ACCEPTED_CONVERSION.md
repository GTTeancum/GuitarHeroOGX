# Midori: accepted PS2 conversion record

Accepted by the user on 2026-09-09. The accepted package is
`community.gh3.midori` version **0.2.3**, with both PS2 outfits, source animation,
and an instrument-free animated character-select presentation. This document
supersedes the retired donor-rig experiment and rejected menu trials.

## Source and reproducibility

- Source: Guitar Hero III: Legends of Rock (USA), **PS2**, not the PC GHWT install.
- Skeleton: `skeletons/gh3_guitarist_midori.ske.ps2`, 72 bones, 8,184 bytes;
  SHA-256 prefix `0662e879482221d4`.
- Source IR manifest SHA-256:
  `e7f0543b61f78a6994415dbcf891653b5683d871d9c345cba6c9005b244056ce`.
- Both `midori_1` and `midori_2` skins retain their source skeleton and weights.
- Source extraction used NXTools commit `6cea808`. Required third-party tools
  and copyrighted source assets are not bundled with the repository.
- Rebuild recipe: `tools/gh3_ps2_recipes/midori_actions.json`.
- Commands, dependencies, audit procedures, and runtime diagnostics:
  [GH3_PS2_CONVERSION.md](../tools/GH3_PS2_CONVERSION.md).
- Final installed manifest and hashes:
  [accepted-package.json](proofs/midori-instrument-free-menu/accepted-package.json).

## Geometry, bones, controllers, and motion

`gh3_midori_ir.py` decodes the PS2 skin, textures, skeleton, SKA key formats, and
little-endian partial-animation masks. Resolve bone names by checksum; a label
on an imported bone-name list is not evidence of the model's source platform.

`gh3_ps2_source_character.py` preserves the source rig instead of projecting
Midori onto a stock character's rest pose. The conversion changes coordinates
and bone names, derives GH2 positive-X arm frames and elbow hinge axes from the
source bind pose, and compensates descendants when rebasing transforms. The
converter checks world-pose invariance. Keep the recipe's explicit instrument
frame registration and source hand/instrument proxies; the MILO writer's
`--preserve-guitar-proxies` prevents their replacement with stock helpers.

Source-authored body animation owns the arm chains. Target donor IK flags must
not overwrite a complete source arm pose. Body clips retain disjoint facial and
accessory overlays, including eyes, mouth, and bouncing hair. Facial bones must
not collapse onto the head or collide after renaming. Retain source timing in
seconds rather than assuming that a source clip has a stock GH2 duration.

Finger motion uses disjoint source finger-joint compositions, separate high-neck
variants, source wrist-bend ramps for vibrato, and source note-onset timing.
`fit_gh3_ps2_hand_calls.py` compares stock distal-bend signatures to source
candidates. It proposes mappings; it does not copy Casey's poses or establish
visual guitar contact. The final samples were checked in native gameplay with
both fretting and strumming motion. No newly hand-authored pose keys were used.

## Animation-call contract

Both outfits share four banks: 72 main clips in 18 groups, two UI clips,
25 fret clips, and 11 guitarist strum clips. The recipe explicitly specifies
aliases, group membership, selection flags, playback flags, and blend widths.
`gh2_native_guitarist_contract.json` validates required consumers before build.

| Native consumer | Source behavior and runtime handling |
|---|---|
| normal / idle / bad | Source playing, break, and poor-performance actions |
| solo / extreme | Source solo and intense-performance actions |
| star_power | Actual activation dispatches the authored main driver; completion resumes the current normal/idle/solo state |
| sync_jump / sync_wag / sync_head_bang | Source movement; transient completion resumes the performance state |
| intro | Source pre-song introduction |
| win / lose / win_finals | Real result handlers; campaign selection uses win3 |
| walk_turn / walk_walk / walk_stop | Source strides/stops with extracted root motion and generated transition links |
| fretting / strumming | Complete required native guitarist hand-call inventory |

Shared engine fixes include `i_won`/`i_lost` fallback, native `set_game_over`,
campaign outcome selection, continued ending-animation time after scoring/audio
stop, and transient main-animation return. Star Power was tested through an
actual process-local guitar input edge, not merely by forcing a clip to render.
Chart messages and named handlers use the normal character-type dispatcher.

For locomotion, `build_gh2_locomotion_graph.py` matches existing lower-body poses
and emits transition edges without changing body keys. Source root translation
becomes virtual GH2 facing channels; stationary clips supply zero facing.
There are no authored directional turns in this source set, so start strides
receive a continuous heading adaptation. The native CharWalk predictor owns
world movement once: the servo must not apply facing again to the pelvis.
Related fixes preserve position when rotating the basis and remove terminal
waypoint teleport snaps. The virtual facing channel is not emitted as a CharBone.

The MILO converter sorts ACP channel/sample buckets consistently, validates
explicit groups and transition references, and serializes the native graph.
Both models reference the same banks; rebuilding an alternate outfit must not
silently substitute different animation or controller ownership.

## Character select: accepted behavior and rejected approaches

The authoritative PS2 `guitar_animation_data_midori.qb` table maps
`stance_frontend/idle` to the **full frontend intro2 and intro4 clips**.
`audit_gh3_ps2_frontend.py` reads QB/PAB data and verifies exact SKA contents in
`midori_1_anims.pak.ps2`. Frontend and gameplay SKAs can have identical basenames
with different bytes; a basename match alone is insufficient.

- Career uses full frontend intro2 as `ui_enter`, then full frontend intro4 as
  `ui_loop`, with the normal 1P placement and entrance route.
- Manage Band uses the multiplayer idle route (`ui_loop`) without a 1P entrance.
  Each posed model is framed once at 20% screen width, within the left 40% bay;
  a stable camera avoids breathing/gesture-induced reframing.
- Character select has **no guitar**. Neither outfit specifies `ui_guitar`.
- Both outfits retain moving body, face, and hair channels. No held-body flags
  are enabled on either UI clip.
- The out-to-A/out-idle pairing was wrong: `stance_frontend_guitar` is the
  separate guitar-selection hold. Freezing an intro's terminal pose was also
  rejected. Adding a visible guitar was rejected. None is an accepted template.
- Optional generic runtime prop support remains dormant. Do not infer source
  menu prop visibility from hand positions or animation names.

Authoritative source evidence:
[frontend-source.json](proofs/midori-authored-menu/frontend-source.json).
Accepted visual evidence:
[instrument-free menu video](proofs/midori-instrument-free-menu/manage-band.mp4).
Earlier held-pose and guitar-present menu captures are superseded.

## Validation and limits

The expanded gameplay package rebuilt cleanly with all six content files
matching the incremental candidate. Strict native all-pages binding checks
passed for all four banks. Five converter tests and native character-type
script tests passed, including dispatch against a real stock character model.
An installed alt-outfit gameplay smoke recorded 14 hits, zero misses, and actual
Star Power activation selecting `special_02`. Longer proofs cover both outfits,
movement, hand contact, intros, transient actions, and real win/loss/campaign
outcomes. The accepted menu capture verifies both outfits with no attached prop.

[Gameplay verification](proofs/midori-action-mapping/verification.json) and
[menu verification](proofs/midori-instrument-free-menu/verification.json) record
checks and hashes. Visual review was sampled, not an exhaustive every-frame,
every-variant contact audit. Retail PS2 GH2 execution remains unverified.
Optional retail interactions and six bassist slap/pluck hand calls are outside
this native guitarist contract. Face/hair phase continuity between distinct
body clips has not been exhaustively qualified.

## Change history and handling

Core source conversion: `c8b9b958`; inventory audit: `ed9779a9`; native action
contract and shared runtime fixes: `5fbe8e70`; menu framing: `0a73373d`; verified
frontend source selection/audit: `2ea7ee06`. The accepted 0.2.3 recipe removes
instrument metadata while retaining full frontend motion. Commit history records
rejected intermediate states; use this document and the current recipe.

Installed assets live under `gh2_ps2_hybrid_assets/DLC/community.gh3.midori`.
Versioned prior packages/runtime executables were moved to `.install-audit` and
`.runtime-rollback` for recovery. Builds, extracted scratch, duplicate candidate
packages, and bulk captures were removed after verification. Original build
outputs were restored. Tests use the target application's own capture and
process-local diagnostics; no desktop or OS input automation is needed.
