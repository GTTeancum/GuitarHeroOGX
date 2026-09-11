# Shared 3D crowds and camera completion — 2026-09-11

Executable: `c474acbf47c8cb124fa27b879af0992ed4b795d3a435be83b1de3754fa1c9fea`.

The standing requirement is **3D characters for every visible crowd placement**,
including placements retail rendered as sprites. Camera selection, hiding and
density still control visibility; none of the visible population uses impostors.
This applies to all eight GH2 and seven converted GH1 venues and future content
using either crowd system.

## Native proof

- GH1 Big Club: [before](gh1_big_club-before.png), [after](gh1_big_club.png).
  The blue sprite obstruction is gone without moving the camera.
- [Stone](stone.png): fully 3D foreground crowd; authored crowd-facing camera
  compositions still include foreground people.
- All venues, frames 0 / 180 / 360: [1](venues-1.png), [2](venues-2.png),
  [3](venues-3.png), [4](venues-4.png), [5](venues-5.png).

All 45 final native stills were visually reviewed. This is three stills per
venue, not a claim to have inspected every rendered frame. The retained
[verification](verification.json) accounts for all 5,415 submitted camera
samples, which match the prior camera matrix exactly. All 15 runs pass with
13 crowd telemetry samples each and zero sprite draws. Steady native desktop
performance ranges from 58.561 to 59.926 fps, including the still captures.
These measurements do not certify original-Xbox hardware performance.

## Implementation

GH1 retains its independently audited source region/population machinery. The
presentation layer combines promoted placements and the remaining visible
population, using authored region ground planes or actual card geometry bottoms
for foot pivots. Its existing animated actor pool supplies repeated placements;
owned sprite MultiMeshes no longer submit cards. GH2 similarly retains source
selection, density and hiding before drawing visible placements as 3D actors.

The first implementation reached only 12 fps in Stone. CharRenderer now skins
each shared pose once per mesh and reuses the geometry/material setup across
placements. Only active WorldFx require individual ordered rendering. Exact
posed mesh bounds skip fully clipped instances using the actual D3D view and
projection. Near-plane intersections and invalid bounds are conservatively
retained. No crowd sprites, camera offsets, reduced population, or frozen
animations were used to recover performance.

The batching-only Big Club/Stone proof stills were byte-identical to individual
3D drawing. Frustum boundary/invalid-input tests and crowd population/grounding
tests pass. The original retail crowd membership implementation still passes
the fresh [saved-state oracle](retail-crowd-oracle.json).

## Closure of original TO_DO 6–9

| Requirement | Evidence |
|---|---|
| Practice Room + native highway + correct credits | [Practice proof and source mapping](../menu-camera-20260909/README.md) |
| Highlighted-song previews, boundaries, loop and stop | [Native menu/audio telemetry](../menu-camera-20260909/menu-verification.json); focused audio contract passes |
| Source Soundcheck navigation cue | Stock SCROLL_MSG → button_toggle and native Down/Up submissions in the same telemetry |
| Camera targets, blends, paths, shake/helpers, selection and lifecycle | [Recovered source/address audit](../../CAMERA_DRIVER_AUDIT.md), saved retail pose/handoff fixtures, ten passing focused CTests, 5,415 unchanged current camera samples |
| GH1 release FOV/path policy and crowd promotion | [Live/release object comparison](../menu-camera-20260909/gh1-payload-verification.json), fresh retail crowd oracle; complete visible population now rendered in 3D |
| All venues, no venue-specific camera fixes | Current 15/15 native matrix, unchanged camera samples, all-venue visual review |

Camera certification covers the recovered normal PS2 driver contracts and
the tested inputs. It does not claim exhaustive bit-exact behavior for every
possible physical-PS2 floating-point edge case. Native smoke alone is not the
source-parity evidence; the recovered executable contracts and retail memory
fixtures are independent gates.

The ten camera/session CTests now all pass. Fifteen outdated structural checks
were corrected to recognize existing loading callbacks and diagnostic hold
wrappers, and to scope highway ordering to the actual gameplay draw function.
Their original behavior requirements remain checked. The interpreter and UI
audio contract also pass. The unrelated broad UI suite's previously recorded
13 baseline failures are not represented as fixed by this work.

No host input or desktop capture was used. Character/venue DLC packages were
preserved. No commit or push is included in this completion.

## Commit verification

The recorded executable and runtime proofs were produced from the installed
worktree. The completion commit isolates Practice, previews, camera/crowd work
and these records from unrelated pending menu-loading and character-conversion
edits. MSVC syntax checks pass for the staged menu, song overlay, preview
dispatch, gameplay, character renderer and scene renderer with staged headers.
The runtime hash above identifies the captured build, not a clean-checkout
build of this isolated commit.
