# Shared camera-driver audit

Status: **shared PS2 gameplay driver recovered; current 15-venue verification and full 3D crowd replacement passed** (2026-09-11).

## Current completion — 2026-09-11

The current installed executable is
`c474acbf47c8cb124fa27b879af0992ed4b795d3a435be83b1de3754fa1c9fea`.
All 15 venues pass; all 5,415 camera samples match the previous matrix exactly.
The ten focused camera/session CTests pass, and the fresh saved retail crowd
oracle still matches. All 45 final native stills were reviewed. See the
[current requirement/evidence record](proofs/crowds-3d-20260911/README.md).

The user's standing requirement overrides retail sprite presentation: every
visible GH1/GH2 crowd placement now draws a 3D character. Source selection,
visibility and density are preserved. Shared pose batching and conservative
posed-mesh clipping keep all venues at 58.561–59.926 steady desktop fps.
GH1 Big Club's sprite obstruction is removed without any camera adjustment.
Authored views can still include foreground 3D people, including Stone.

Earlier notes below are historical, including statements that release GH1
camera payloads, 3D promotion, or the shared-driver tests remain unfinished.
The live/release object comparison and current evidence record supersede those
statuses. Certification covers the recovered normal PS2 contracts and tested
inputs, not every possible physical-PS2 arithmetic edge case.

All eight GH2 venues and seven converted GH1 venues are compatibility inputs.
There are no venue-specific camera adjustments in this audit. Retained PS2
camera captures are comparison evidence, not a substitute runtime driver.

## Previous acceptance — shared driver and all-venue native proof

Current executable:
`df24602e52429b145f311b3931fbd9045f5f8e2e34809416cab5fc736aa3ca3e`.

The source-recovered normal PS2 gameplay driver reports zero hidden or deferred
gameplay blockers. The current native matrix passed all eight GH2 and all seven
converted GH1 venues: zero errors, 361 final submitted transforms per venue,
5,415 total transforms, exactly one authored cut per six-second proof, and no
one-update shot islands or cut/return oscillation. Nine focused release tests
pass. All current videos and the exact evidence boundary are indexed in
[`CURRENT_SHARED_CAMERA_ACCEPTANCE_20260905.md`](../proofs/camera-full-re-20260903/CURRENT_SHARED_CAMERA_ACCEPTANCE_20260905.md).

This build also corrects the last source-RNG lifecycle mismatch found by the
completion audit: coalesced `force_pick_shot` calls now consume every overwritten
`get_shot_duration` draw from the same process-wide Rand stream used by weighted
camera selection and CamShot shake. Advancing only the diagnostic counter had
left later source decisions on the wrong draw. No venue-specific behavior was
added.

The ordered contact sheets for every current video were inspected. Stone and
converted GH1 Big Club retain heavily foregrounded authored crowd views; their
camera ownership and transforms remain stable. Crowd presentation is not
silently converted into a camera offset. PCSX2 was not launched for this
acceptance run.

An exact-current-hash no-capture Stone run sustains 58.126 fps over its
331-frame steady window, confirming the lower encoded-proof rate is capture
overhead rather than camera-driver cadence. A structural scan of the shared
pose/selection source ranges finds no literal name of any of the 15 venues.

Everything below this section is the chronological reverse-engineering log.
Statements such as “remaining,” “not wired,” or “partial” describe the build at
that historical checkpoint and are superseded where the current acceptance
record documents the implemented path.

## Current continuation — Big fan, performer root, and regulation boundary

Current executable
`04420b98f319b80ae953be996f75c15f189ca451f6761605faa32a5d98c097d3`
has a new 60 Hz native Big proof covering exactly seconds 6-12. All 361 frames
were inspected sequentially. The former slide/snap/return, repeated guitarist
root spin, and fan/light hitch are absent. Camera transforms remain continuous;
the fan and floor-light projection advance together from the source-authored
`music_start` route. This supersedes the older `music-start-native` visual,
which is retained only as defect history.

An existing retail save now also resolves the live regulation waypoint names.
Its guitarist has move_self enabled and a null regulator, while the bassist
uses `start_bassist.way`; singer/drummer are in the non-self-moving branch.
This rules out a fabricated guitarist spawn regulator and keeps regulation
data-driven. Details, exact proof paths, scope, and remaining gates are in
[`CURRENT_BIG_6_12_AND_REGULATION_REVIEW_20260905.md`](../proofs/camera-full-re-20260903/CURRENT_BIG_6_12_AND_REGULATION_REVIEW_20260905.md).

This focused correction is not the final all-fifteen-venue matched-retail
visual acceptance, so the shared-camera goal remains open.

## Current continuation — projection source drivers (2026-09-05)

The filter-cache and bf011 projection sweeps below are now terminal: each
passed native trace integrity for all15 venues/15,765 poses. A further
executable audit found GH1 rebuilds a normalized basis while GH2 takes a
full-affine inverse. The shared implementation now preserves that distinction.
See [source addresses, tests and proof scope](../proofs/camera-full-re-20260903/PROJECTION_DRIVER_REVIEW_20260905.md).
Full camera parity is still open; the historical builds below are not current.

## Historical continuation — basis fidelity (2026-09-05)

Latest executable: `37c81cb11fb4a6eb0c78352331b1441fbf29ac615c82e08a37af8c47049c605a`.
This includes the correction in the production per-source-frame target/parent
caches, not just the older helper path. Parent matrix filtering also runs at
product0 and after cache seeding, matching267400..267410. Tests/build pass;
the final all-venue regression is still running. The completed43865 capture
set has15 videos and15,765 submitted poses, not a visual/retail-parity pass.
See `proofs/camera-full-re-20260903/NATIVE_VENUE_VIDEOS_20260905.md` for clips.

Additional source check: `267188..2671BC` selects
`min(projected_distance, 1)` and multiplies it by the authored shot filter.
It does not clamp the product; `2671C0..267214` uses exact0/1 branches before
the weighted target/cache sum. Native incorrectly disabled negative filters,
clamped gains above1 and saturated the cache update. The shared helper and
runtime now preserve signed/>1 extrapolation. New edge-case tests and the
existing saved-pose/filter suite pass. This change is not yet in the43865
binary used by the ongoing1051-frame all-venue captures. Scalar/VU bit-exact
rounding and matched live target-cache parity are not certified by these tests.

Trace tooling now accepts authored shot names containing spaces and audits
all segments rather than assuming every long capture contains only one cut.
Its integrity checks require the exact requested frame count and executable
hash; motion peaks/brief returns are review locations, not source verdicts.

The shared interpolation/shake path now retains the actual PS2 matrix rows,
including quaternion conversion at exact blend endpoints and the final
SetFrame conversion. Extra native Gram-Schmidt steps were removed from that
path. `2DA3F0..2DA444` also selects the FIRST largest diagonal on ties when
converting a negative-trace matrix; our former standard conversion chose
the last. The cyclic index table at `0x45AA00` in retail memory is `(1,2,0)`.
The corrected tie cases and saved `lose01` final pose pass native unit tests.

The prior basis build (`523b24a9...`) completed **15/15 native smoke checks,
915 camera frames**, with source servos enabled for all four band roles in
every venue. This is compatibility, not matched-retail parity. BigClub still
fails midpoint visual review because foreground crowd cards obscure the stage.
The final build including tie correction and bounded diagnostic labels is
`43865ab00e724558ca7e5e52ff09eddf879ea2626c296248259e33ef8bfb45d2`;
its longer native motion regression is running. Do not attribute the previous
15-venue sweep to this newer hash.

**Correction to stale resume notes:** source Character-local servo ownership
is already the default for native/conversion characters. Only explicit
`GHOGX_DISABLE_SOURCE_SERVO_RUNTIME` disables it for diagnostics; external
retargets keep their separate compatibility path. Implementation/default
enablement is not an open task, but matched-retail motion verification is.

The offscreen PCSX2 attempt did not boot. A follow-up PE import audit corrected
the earlier claim of many stripped Qt exports: that claim came from pefile's
default 8,192-export limit. With full export tables, the genuine mismatch is
the plugin's `createPlatformVulkanInstance` import against installed QtGui.
The private PySide QtCore also lacks PCSX2's `qResourceFeatureZstd` import.
No further emulator launch or installed dependency modification was attempted.
See `BASIS_FIDELITY_REVIEW_20260905.md` for retained evidence/cleanup limits.

## Previous continuation — saved retail crowd oracle (2026-09-05)

The native GH1 Basement save (`SLUS-21224 (B815F724).01.p2s`, EE memory
SHA256 `28f750aeb942b948c8caf00207699a4485f63dc1d3fb02a88005a3dafee2c5fd`)
now provides an actual retail runtime oracle, not just a reimplementation of
the same algorithm. Its native screenshot establishes that the scene is
Basement with Judy, **not Big Club**. No emulator or desktop operation was
needed to read it. The source save and assets were not modified.

`tools/audit_gh1_saved_crowd.py` validates Crowd/MultiMesh vtables, list and
vector bounds, and conservation of instances across the active, SetSizes-held
and SetRegion-held lists. It compares with `ghogx_gh1_crowd_region_audit`, which
compiles the renderer's actual `gh1_crowd_regions.cpp`. All 92 original origins
match; all 12 regions now match ordered membership and ground height, with
maximum center/radius/plane error 0.000060916. Nine verifier tests reject
reordering, lost/duplicated members, wrong indices/height, missing regions,
nonfinite bounds and empty evidence.

Four corrections are source/data-backed, shared across converted GH1 venues:

- The archetype order is `arena::Crowd%02d.mm`, starting at 1 and stopping at
  the first missing object (USA ELF `0x171A14`, `0x171A50..0x171AB8`). MILO file
  order differs and previously changed the promoted 3D crowd prefix.
- The promotion plane is the first geometry vertex transformed to world Z
  (`0x171338..0x171374`, stored at `0x1713BC`, consumed at `0x172BEC`). This
  **supersedes the earlier negative-translation interpretation**: that scratch
  value was overwritten before being stored. All 12 planes had the wrong sign.
- The original scalar triangle test (`0x1E3600..0x1E37AC`) is sensitive to
  operation rounding. The retained PCSX2 configuration chops ADD/SUB/MUL but
  uses nearest DIV rounding. Reproducing that distinction recovers Basement
  region 5's missing Crowd02 member without changing its mesh or epsilon.
  This is validated against that emulator oracle; it does not by itself prove
  all physical-PS2 arithmetic edge cases. Existing camera arithmetic is unchanged.
- `SetSizes` transfers excess **front** nodes into its held list, retaining
  the suffix (`0x17308C..0x1730E4`). The prior prefix policy got the count right
  but all 36 visible cards wrong in the saved scene. With source selection and
  the saved 0.5/0.5 fractions, all 36 visible cards now match exactly as well.
  `--live-state` makes this an additional independent gate. This saved-state
  check does not certify every multi-transition list-splice history.

Evidence: `proofs/camera-full-re-20260903/gh1-retail-crowd-live-state-fixed-20260905.json`.
The new development build is
`51ce2fdff09f37fe459fa5e6a60a40d39ddb4a37e05f770ecc6bcb735decc7e4`.
Crowd-region, camera-filter and intro-timing native unit tests pass. This does
not close Big Club obstruction, full live camera transforms/targets/parents,
performer-root spinning, or the final all-venue visual review.

Seven native181-frame GH1 regressions pass on this exact build, with all1,267
camera submissions unchanged from692d in order. See
`crowd-retail-oracle-native-20260905/camera-invariance.json`. All7midpoint stills
were inspected, not every video frame. BigClub remains heavily obstructed;
Theatre retains a dark right-foreground obstruction. One61-frame stockGH2 Big
smoke also passes with zeroGH1 crowd rows. The prior8-venue GH2 matrix is still
historical evidence on692d, not claimed as a new51ce all-venue test.

## Prior continuation — one GH1 crowd owner across render passes (2026-09-05)

Native diagnostics found the same converted crowd MultiMeshes in both the
assembled world and separate lighting scenes. Only the world population
received the source SwitchCam/SetSizes state, so the lighting copy bypassed
camera-region exclusion. This was not a camera-position defect or a missing
alpha flag. The source Arena owns one Crowd (the original USA ELF resolves
its crowd environment into Arena+0x9c at 0x1685dc..0x168628).

The secondary renderer now excludes only an exact duplicate Arena-owned draw:
ownership marker, template name, all instance transforms, material reference,
indices and decoded vertex data must match. It retains animation/lighting
objects and does not alter source showing flags. Native GH2 has no ownership
marker and remains unaffected. Geometry/transform mismatches are retained.
The crowd-region unit test covers these guards.

New development build `692daf3d582e3a1676bf9040dea6497776c0d9c8cbbce66f30c00fed58043c70`
passes all seven GH1 native smoke runs. Their 1,267 submitted camera rows match
the previous build exactly, in order. `crowd-single-owner-native-20260905/ownership-audit.json`
records removal of five duplicate MultiMesh objects each in SmallClub,
SmallClubMulti, BigClub and Arena, and zero in Basement, Theatre and Fest.

SmallClubMulti's giant foreground crowd wall is gone at the reviewed midpoint.
BigClub remains obstructed and is NOT passed. Its source camera DTB explicitly
sets region0/hide_crowd0/near10 for flr_near_lft03x2w; changing that region would
not be source-backed. Full source membership/transform and retail camera parity
remain open. All seven midpoint images were inspected, not every video frame.

## Prior continuation — compiled GH1 release cameras (2026-09-05)

The release package under `release/packages/project.gh1.converted` has now
been refreshed from original GH1 camera DTBs, shared path policy/FX and game
configuration. All 201 compiled CamShots previously had zero selection weight;
all now have the source-uniform weight of one. Per-venue record counts are
25/24/4/38/32/45/33 (Basement/Small Club/Small Club Multi/Big Club/Theatre/Fest/Arena).
This repairs the bundle rather than adding venue-specific runtime fallbacks.

`tools/dlc_installer/refresh_gh1_camera_package.py` performs this offline release
engineering step. It validates the entire candidate package and checks typed
CamShot counts/weights before replacing the seven venue MILOs and content index.
All 114 other indexed files and the manifest remain byte-identical. The
converter preserves all non-camera objects within the venue MILOs. Three tests
cover rollback after partial publication, rollback after failed validation and
isolation of other packages sharing hard links. The source disc is read-only;
temporary extracted entries, candidates and rollback links are removed.
A second real recompile is byte-identical for all seven camera payloads and the
entire package fingerprint. Candidate preparation preserves qualification and
other ancillary metadata for an accurate fingerprint, without re-granting it.
See `gh1-release-camera-refresh-idempotence.json`.

Evidence: `gh1-release-camera-refresh.json` records original-disc entry hashes,
converter/inspector hashes and every before/after camera payload hash. Existing
qualification was not modified or re-granted; it is not evidence of completion
of this newer full-camera audit. The installed staging DLC was not overwritten.
Native tests now load the refreshed release package directly. Full all-venue
framing and matched-retail camera parity are still open.

Path FOV verification now supports exact executable-hash, all-source-path and
complete-duration gates. Complete duration includes the final hold after the
zoom finishes. Four verifier tests pass, and the previous partial Encore proof
correctly fails that stronger duration gate. All eight final-build paths now
pass, including final holds:6,308 submitted frames, maximum FOV error
1.201216e-7 radians. Evidence is under `path-complete-20260905`; no redundant
stills were captured for numerical runs. This is still not a complete retail
path-transform/aim/parent oracle.

Seven active-play runs against the refreshed GH1 release bundle now pass with
181 submitted transforms and four helper polls apiece, and retain native clips.
Midpoint inspection still fails BigClub and SmallClubMulti because crowd cards
obscure the camera; the separate Basement opening still is behind structure.
See `gh1-refreshed-active-helper/REVIEW.md`. No full sequential review or
all-venue visual acceptance is claimed. The initial compact/helper flag conflict
is corrected in the harness; missing logs were not evidence of missing execution.

A fresh1200-sample retail PINE trace records guest-only retry from lose01 to
Intro_fast, with no host input. It does not expand parent/path branch coverage.
The owned PCSX2 oracle process was closed afterward. Source/summary files are
`gh2-retail-retry-driver-*`. Full shared-camera goal remains active.

## Latest checkpoint — path transform and keyframe settings (2026-09-04)

The typed USA-disc inventory now covers all 293 stock GH2 CamShots. Eight
reference a TransAnim path, 71 have a nonempty parent, and 178 have a nonempty
target object. Empty target slots are excluded from that last count. Adjacent
pairs classify as 118 same-target, three changed-target, nine target-appears,
12 target-disappears and 118 without nonempty targets. These are authored
references; runtime object resolution is a separate check. Full fields for
the eight path shots are in `gh2-usa-disc-camshot-audit.json`.

Two shared runtime errors are corrected. Path sampling formerly retained only
the first CamShotFrame's FOV/target/screen offset instead of evaluating GetKey.
It also omitted the first frame's world-offset transform. Original PS2
BuildTransform `267344..267350` loads the first frame through `shot+10`, then
calls `Multiply(first.worldOffset, sampledPath, sampledPath)` at `2DAF00`.
The recovered VU instructions preserve row-vector order and all matrix rows.
Native now applies that transform to both selected CamShot frames, preserving
their independent FOV, screen offset, target, parent, easing, and shake fields.
Their parent transforms are then composed by the shared BuildTransform route.
Converted GH1's compiled direct-keyframe route is unchanged.

Build `0821f3927928aeeace2dc58cc4a8965cd5544f6fa41073e6c914783e97b700d5`
passes camera math and venue-orchestration tests. The noncommuting transform
test includes scale and translation. Theatre Intro01 now changes FOV from
1.04719758 to 0.60241574 radians and passes all 721 comparisons against
independently evaluated disc keyframes (maximum error 0.000006738 radians).
The retained previous-build trace fails the same verifier by 0.44478184 radians.
Native video and verifier reports are in `path-settings-fixed/`.

The initially reported Arena slowdown was measured with expensive historical
RE candidate diagnostics enabled. With the same executable and normal camera
processing, compact submitted-transform tracing records 58.739 steady fps;
the verbose candidate mode records about 0.46 fps. The first three submitted
camera transforms agree within 0.000000467, the verbose log's print precision.
Use `audit_camera_venue_matrix.py --compact-trace` for performance/video proof;
the older all-venue diagnostic timings are not valid release-performance claims.

### Final shared-target/blend check

Original PS2 SameTargets `2664D0..26659C` compares serialized list size and
then searches the other list for each object-pointer/subpart identity. Empty
lists match; null slots remain significant; ordering is irrelevant and matches
are not consumed. Native formerly stripped null slots and compared resolved
targets in order. The shared loader/identity comparison now preserves these
semantics, with tests for ordering, nulls, subparts and duplicate membership.
Runtime identity still uses resolved transform IDs, so matched live pointer
resolution remains a separate parity gate.

Interp now consumes GetKey's blend directly rather than reconstructing it from
rounded presentation-frame timestamps. Final executable SHA256:
`70854664f96f342dd45f4e067a0a2d62a86bfefcc52d56f36a3ff7fea173b16e`.
Six focused CTests pass (camera matrix, animation task, venue contract, camera
math, intro timing and character driver flags). Final-build native checks:

| Source path shot | Samples | Maximum FOV error (radians) | Steady FPS |
|---|---:|---:|---:|
| Fest lose01 | 721 | 0.0000001202 | 59.520 |
| Arena encore | 721 | 0.0000000653 | 59.216 |

Both runs use compact traces and 60 Hz simulation; no desktop input. Encore
exercises the authored null-to-guitarist target pair, but matching its FOV does
not by itself certify the resulting aim against retail. The first pass's Fest
failure (0.000070964 radians) is retained as superseded evidence in
`path-settings-cases/fov-audit.json`; both final runs pass in
`path-final-cases/fov-audit.json`.

The other six path shots passed the 721-sample FOV check on the intermediate
path-transform build: Small1 Win01, Theatre Intro01/Win02, Arena
balcony_lft03/balcony_lft04/intro02. Thus all eight authored path shots have a
passing test, but not all on the final executable or through their full authored
duration. Remaining: final-build full-duration runs and matched live
matrices/aim/parent/target behavior. Midpoint stills of the final two runs were
inspected; this is not a sequential video review or all-venue visual acceptance.

Redundant intermediate PNGs were discarded; compact raw traces, verifier
reports, the Theatre path video and the final-case stills remain. Historic
`screenshots` entries in intermediate JSON identify discarded captures.
This checkpoint does not certify all-venue framing, matched live retail poses,
or the whole camera goal.

## Previous checkpoint — exact shake/projection math and oracle scope (2026-09-04)

The shared runtime now uses GH2's 65-value sine table, EE-style chopped float
operations, recovered Euler `MakeRotMatrix` order and the corrected retail
shake spring equation. The spring advances by two percent of the normalized
target-to-output displacement per update; it does not multiply that direction
by the displacement length a second time. The 121-sample independent replay in
`source-shake-trace-20260904-v2/shake-audit.json` includes 40 random impulses
and has maximum absolute error `1.538723753968796e-08`.

`RndCam::UpdatedWorldXfm` is now preserved as a full affine view rather than
being normalized back into a look-at camera. The original saved EE-memory
projection oracle passes both recovered compositions: `inverse(world) *
projection` has maximum absolute error `3.480105186781657e-05`, and
`inverse(projection) * world` has maximum error
`2.4359381067817765e-08`. Native proof submits a custom affine view on all
121/121 audited frames at a steady 59.793 fps. The current executable SHA-256
is `fae64843e9c14e977540567d4b6371664442293186545332ee19a5d2160fee35`.

The saved Battle directory contains 39 CamShots and 41 adjacent keyframe pairs.
Its resolved path pointers are all null, but five shots retain non-null singer
or guitarist parent pointers. Exact serialized target-list identity and live
pointer resolution classify the pairs as 17 same-target, one different-target,
two target-appears, one target-disappears and 20 no-live-target branches. This
scope is retained in `gh2-retail-saved-camshot-branch-audit.json`; it corrects
the earlier object-truthiness inventory that treated the string `"0x0"` as a
live pointer. The state can cover parent and target branches, but not a live
path branch. That remains a separate proof gate; the static `lose01` oracle
must not be presented as full driver parity. Camera selection and shake also
share one process-lifetime source RNG now, while exact time-matched interleaving
with random draws from unrelated retail subsystems remains an explicit
comparison limitation.

Latest diagnosis (2026-09-04): `THEATRE_OBSTRUCTION_RE.md` in the camera proof
directory identifies a real flat MultiMesh occluder outside the source-selected
region. Region metadata and original geometry match, and near10 is submitted.
The staged GH1 camera asset is stale (unmigrated FOV); both isolated current
rebind and full source recompilation still fail visually. Their snapshots were
removed and staging unchanged. Therefore neither missing selected-region3D
replacement nor old FOV/helper conversion alone explains this shot. Next is
the source flat-crowd draw/placement path and matched full-pose verification.
Do not add a camera offset, expand the region, or fabricate a hide-list.

## Latest checkpoint — source six-downbeat intro handoff (2026-09-04)

The first regular shot was previously selected on the first post-presentation
update. That erased `intro_start_msg`'s six-bar counter and replaced the
selected intro underneath active play. The original
`world_objects_worldbase.dta` does the opposite: `intro_start_msg` starts the
INTRO CamShot with six bars remaining; each `downbeat` decrements the counter,
and `check_camera_shot` can pick only after it reaches zero. The retained
60 Hz retail CameraManager trace independently keeps `Intro_fast` current,
advances it beyond its nominal 60-frame duration, and publishes its first
regular CamShot at beat-domain task time `20.021`. CamShot duration therefore
starts the intro clock; the bar script owns the later replacement.

Native now retains the selected decoded intro as CameraManager `mCurrentShot`,
with `mCamStartTime=-mDuration/30`, across the song-clock handoff. Beat zero is
delivered as the first real downbeat instead of being discarded. The common
counter then records beats `0,4,8,12,16,20`, bars left `5,4,3,2,1,0`, and queues
the first regular shot only on beat 20. This is shared driver state; there is no
Big-specific timing, shot name, transform, or camera offset.

Evidence is retained under `intro-six-bar-native/`. The lifecycle audit passes
18/18 comparisons against the retail trace. The per-update native transform
audit retains all 1,500 submitted poses and passes 9/9 checks: exactly two shot
segments, one `Intro01 -> flr_near_rt01` boundary, incoming local frame zero,
and no return/third-shot intrusion. The focused native 60 Hz video contains
frames 1320–1420; all 101 were inspected sequentially. It shows one cut on
video image 47, with no one-frame alternate angle or back-and-forth through the
remaining 55 frames. This closes the shared initial handoff defect only. It is
not all-venue pose parity, full-shot visual acceptance, or a real-time
performance benchmark; heavy per-update logging reduced this diagnostic run
to about 50 FPS.

## Latest checkpoint — source-servo all-venue audit and fan cadence

The opt-in Character-local source-servo path completes all eight stock GH2 and
seven converted GH1 native smoke runs, but the matrix is not a visual or
performance pass. Stock Arena/Fest fall to roughly 10/8 FPS; a no-visibility,
servo-on/off control proves visibility logging is not the cause and servo is
not Fest's root cost. Stone places crowd actors above/outside the venue.

The release-staged converted GH1 camera files retain zero selection weights, so
their regular pools always choose the first fallback. A temporary true source
recompile restores one-unit neutral weights across all seven venues and keeps
their fixed-step render rates near60 FPS. Correct selection removes several bad
fallback frames, but Basement, Big Club, Small Club Multi, and Theatre retain
structural/crowd-card obstructions. Installed DLC was not modified. Detailed
per-venue results and inspected stills are in
`SOURCE_SERVO_ALL_VENUE_REVIEW.md`.

The small periodic Big fan pause visible in the prior proof is now isolated to
that proof's 30Hz simulation clock. A 60Hz active-play trace retains121
consecutive source/local-transform samples with no repeated frame and no
zero-angle step; it reaches the authored24.000 endpoint before reversing.
`FAN_ANIMATION_TRACE.md` has the exact sequence and limits. No fan-specific
smoothing was added.

## Latest checkpoint — shared intro task starvation (2026-09-04)

Superseded as latest by **authored music_start dispatch**: original-disc audit
finds the marker in all64 GH2 MIDIs; the old loader incorrectly fired it before
the intro, and the timed text-event loop ignored it. Shared runtime now sends
it at its authored chart timestamp. Four regression tests and native timing
checks in Big/Battle/GH1Theatre plus a Battle seek pass. No venue-specific rule.
Source addresses, limits and new proof are in
`proofs/camera-full-re-20260903/MUSIC_START_RE.md`. New-build full15-venue and
matched-retail coverage remain open. GH1Theatre's new post-intro still is badly
crowd-obstructed and is not visually accepted. Original49 GH1 charts lack the
marker; their startup compatibility policy needs separate source verification.

**Big's red obstruction was a frozen authored curtain, not a bad camera pose.**
Native authored-camera mesh picking identified `curtain.mesh` / `curtain.mat`.
The runtime geometry MILO matches the original USA ISO byte-for-byte (SHA256
`00389ed5c40466d8c176fa88c6bfd62092117ba575243af6d5ef53ca16ef3e81`).
Its source `Intro_start.trig` starts `curtain_rising.filt` with no wait/delay:
frames0..100 at30Hz, targeting `Curtain.grp` -> `curtain.tnm`. The source moves
the curtain from z30.598 to z314.203. `intro_end` sets frame100 with a2sec blend.
The tiny `curtain-source.json` fixture retains those decoded source facts.

The baseline native run queued the rising filter but never sampled it. Tick's
intro branch returned before the shared venue/lighting task polls. It also
reset the song/presentation clock without rebasing task timestamps, causing
tasks started during intro to pause again after song release.

`poll_venue_presentation_tasks` now runs during the pre-song camera sequence as
well as normal play; chart/scoring/audio remain gated. The shared release path
rebases all ten venue/lighting animation queues, script deadlines and animated
proxy start times to preserve elapsed time. No curtain-specific runtime rule,
camera height offset, fabricated hide list or venue-specific fix was added.

New build SHA256:
`1181216e8eac322305a9892531b47ceafa587b6002fcdb1cf6d3c5d0cd67e43a`.
Six regression tests pass. `audit_camera_presentation_tasks.py` passes five
source-fixture checks on the fixed run and fails three on the baseline:
intro polling, progression and handoff-blend progression. Recorded positions
match the source translation keys within the printed0.001 precision.

The new Big native clip shows the curtain down initially, raised with the stage
visible at6sec, and a performer close-up after song release. Its three retained
stills were individually inspected; no full-frame-by-frame review claimed.
See `presentation-poll-big/big/camera-motion.mp4`. All 15 venue regression runs
completed with successful smoke status; all 15 midpoint stills were individually
inspected. `presentation-poll-matrix/REVIEW.md` records the visual exceptions.
GH2 Arena and several GH1 shots retain foreground crowd obstructions; GH2 Fest
and Arena diagnostic runs remain slow (~7.6 and ~9.4 fps). Run success is not
retail parity. Next: source-trace shared startup/music_start timing, then the
remaining camera lifecycle and crowd representation gaps; do not mask them
with per-venue camera changes. Final cleanup is recorded in CURRENT_STATE.

## Prior checkpoint — original saved-camera oracle (2026-09-04)

The original PCSX2 slot1 state contains 39 constructor-identified CamShots and
the active Battle of Bands `lose01` camera. `audit_gh2_saved_camshots.py` reads
the native saved EE memory without emulator execution, desktop access or input.
The original screenshot is `proofs/camera-full-re-20260903/gh2-retail-slot1.png`.
The retail ELF in `analysis/_midori_patch_identity_audit/elf/SLUS_214.47` matches
the ISO member byte-for-byte, SHA256
`30152bfddac4d73a84ef1debc35fc89409b6cd485deb15f2bbf33d64a7871678`.

Source SetPos262B80 locates the camera through WorldDir+21C. Projection builder
1B1FB4 reads near/far/FOV at +2C0/+2C4/+2C8. The reader retains padded local/world
rows, raw projection/inverse-projection rows, source key transforms, references,
per-key caches and per-shot shake state. Labels intentionally distinguish raw
padding from conventional homogeneous matrix components.

The saved active shot has null target pointers, zero target caches, no parent,
no path and zero angular shake. Its translation shake is nonzero. A prior native
forced-lose01 probe resolves a live guitar target, so that probe is **not** a
matched-retail comparison and its different camera eye is not proof of a bug.

`audit_gh2_saved_camera_pose.py` independently reconstructs this narrowly gated
saved pose from authored keys, frame189.170654/duration270 and saved shake.
It does not use the captured camera basis to make the prediction. The position,
basis and FOV maximum errors are respectively 0.0000318 game units, 0.000000083
and 0.000000046 radians. See `gh2-retail-pose-audit.json` (PASS). This is one
static original-game oracle, not native runtime or all-venue parity.

Two details were essential: Matrix3->Quat2DA318 returns **without** normalizing
the endpoint quaternion; Interp2DA6D0 normalizes after blending. Runtime now
uses `quat_from_row_matrix` without the premature endpoint normalization, with
a regression fixture from those saved keys. Also, even zero angular shake
calls MakeRotMatrix2DA888, whose sine2DC500 uses a 64-interval lookup table.
EE truncating arithmetic reaches its zero-slope final interval at cos(0), yielding
a slight uniform basis scale. Runtime now reproduces that table and operation
order, and preserves the resulting affine basis in the renderer-facing view.
Only unrelated-subsystem interleaving into the shared retail random stream
remains time-alignment work; no venue-specific compensation was added.

Big's blocked intro has no targets, parents, path, hide/show lists or draw
overrides in decoded metadata. It therefore cannot be explained by a missing
referenced path or fixed by inventing a shot-specific hide list. Retained
`visibility-diagnostic/big/camera.json` records that constraint.

Build completed, six regression tests pass. App SHA256:
`3498344b8c30a14b249c972649e8a10cced7c39ecfe3bd183c8d0087712a9dec`.
`retail-oracle-native` contains Battle and GH1 Basement six-second clips with
2/2 smoke passes (steady58.7/60.1fps). All six retained PNGs were inspected;
full-video and matched-retail acceptance are not claimed. See its REVIEW.md.
The prior 15-venue matrix below remains the last broad coverage, not acceptance.
Removed13 rebuildable/scratch files totaling8,980,178bytes; no previous rejected
cleanup target was retried and no new retained artifact reaches100MB.

## Prior checkpoint — GH2 path duration/easing (2026-09-04)

Another shared-driver mismatch is now corrected in runtime: GH2 BuildTransform
`267230..2672F0` maps CamShot mFrame over mDuration into 0..1 and multiplies by
the referenced TransAnim's EndFrame. Zero path_ease uses LinearInterpolator;
nonzero uses ATanInterpolator. Previously the live path sampler used
`first_key_frame + elapsed_frames`, allowing a short path to reach its end long
before its owning shot finished. No venue-specific adjustment was added.

Retail `world_objects.dtb` and property handlers now identify three formerly
ambiguous fields: +44 is selection_weight, +54 is path_ease, +3C is fade_time.
The serialized float immediately after looping is path_ease (Save2643E4,
property26873C), not a loop frame. The float after the path symbol is fade_time
(property26850C), not a path frame. StartAnim262884 passes positive fade_time
to WorldDir26F660 with animation task units (1AB000). Further original-ELF RE
locates the draw at WorldDir26F7E0 (26F7C8 is an earlier epilogue): material
+27C uses texture +280, type0x18 FrontBuffer, with blur-grow/alpha lifecycle.
**That effect is still not implemented.** All293 original-disc GH2 CamShots
have fade_time=0, so it cannot explain their snapping and must not be
repurposed as a path-position/pose adjustment. Details and evidence:
`proofs/camera-full-re-20260903/GH2_CAMERA_BLUR_RE.md`.

`camera_path_timing.h` implements the recovered linear/atan mapping, with the
source 1e-6 linear-duration threshold, no imposed clamping and no first-key
offset. Numerical tests cover unequal path/shot durations, endpoints, quarter
easing, extrapolation and degenerate linear duration. Runtime retains the
pre-rev40 path_ease value and routes it through shared intro/regular metadata
to direct translation/rotation/scale-page sampling. Modern/no-legacy and raw
GH1 paths keep their separate existing route. Six runtime tests pass.

New runtime SHA256: `99cdaeb1e1fa43d7375e580d4c6a9781b20c2777a4ce0a4955fdd4c85c5a7399`.
`path-timing-matrix` completed all 15 venues from song start, 361 frames each,
with native Theatre/Arena clips. The harness retains path timing samples with
local frame, mapped frame, duration, EndFrame and easing. Retail timing/framing
parity is not yet certified. All 15 native smoke checks and the independent
telemetry equation audit pass. Twenty mapped samples (Theatre/Arena only) have
maximum error 0.000463 frames against the recovered formula, within printed
precision. The other 13 venues did not exercise the legacy path route here.

Every midpoint image was inspected, plus Theatre ending and video frame299.
Theatre's prior below-stage failure is not reproduced in those inspected frames;
this is not a full-video or matched-retail acceptance. Arena's crowd, Big's
close-up red surface and GH1 Small Club/Multi crowds remain visual blockers.
Big's selected intro has no referenced TransAnim, so path normalization cannot
explain or fix that obstruction. `path-timing-matrix/REVIEW.md` links all images
and the native Theatre/Arena videos with per-venue observations.

Cleanup removed 4,942,896 bytes of permitted rebuildable tests and scratch.
Native temporary snapshots/BMPs were removed by their owning harnesses; source
assets were unchanged. Previously policy-rejected cleanup targets were not
retried. No individual new retained artifact is 100MB or larger. App/converter
tools remain for reproducibility; no deployed installation was updated.

Weighted selection validation completed: `weighted-selection-matrix` passed
15/15 native smoke checks. Fresh conversion `weighted-gh1-matrix` passed 7/7;
Theatre regular pools total 24 and 23, matching their eligible entry counts
instead of all-zero weights. Converter tests pass; binary SHA256
`811e733d6d83465b6e30d7e359be8dba67988f6a3a71ca2ea709d4ed4aed4bff`.
Both runs preserved installed originals and removed private snapshots.
Basement midpoint was inspected: performers visible in a rear/side shot;
this alone is not matched-retail acceptance. Other new GH1 images remain to
be inspected. Earlier weighted-selection media uses the previous runtime.

## GH2 weighted selection (2026-09-04)

The executable's weighted, used-marker selection now drives intro, normal/solo/
lighter, and explicit category picks. The pre-rev38 category float is retained
as selection weight. Requested categories contribute cumulative float weights
in source order; each category first excludes used eligible entries, then clears
eligible used flags and retries only if its first pass yielded none. Selection
uses the first threshold >= the random draw and marks that winner used. It does
not rotate category arrays, advance a category cursor, or prefer path-based
intros. The chosen intro's used marker is retained in the shared shot registry.

`camera_weighted_selection.h` implements the common collector/chooser. Numerical
tests exercise unequal weights, inclusive boundaries (including zero), used
cycles, eligibility changes, independent category retries, unchanged source
order, empty pools and NaN draws. `camera_source_random.h` preserves the recovered
generator. Camera picks and scripted duration draws now share a persistent
stream rather than replaying independent seeds. GH2 `2D9D58` accesses gRand;
`2D9BE8..2D9C54` proves `low + (low16/65536)*(high-low)`, with initialization
to `0x29A` at `2D9D78..2D9DA4`. **Other subsystems' interleaved global draws and
seek/reset equivalence are not yet matched to retail.** Do not equate a matching
generator with matching full-game random state.

All six runtime regression checks pass. The latest runtime build is
`1155871892882b3acd760be0f40befa8c29445ce3f959d55e0ab5fd006f286d3`.
The 15-venue native matrix completed in `weighted-selection-matrix`, using
40s song seek and 361 frames; this tests selection during active play, not intro
framing. `[camera-select]` logs retain candidate counts, total weights, random
draws and winners. Native Battle's first regular pool has 7 candidates with
total weight 4.8, confirming nonuniform weights survive decoding.

The GH1 converter previously wrote zero to its historically misnamed
`legacy_category_frame`. The target GH2 driver interprets that as weight, so
zero makes all-zero categories degenerate. The converter was corrected to
write one unit for each unweighted GH1 switch_cam entry. This is a neutral GH2
format mapping, not a claim that GH1's original chooser/used-cycle is identical
to GH2. The original GH1 `arena/gen/camera.dtb` uses `random_elem` for singer and
win/lose shot lists; retained evidence is `gh1-camera-choice-config.json`.
Fresh converted-GH1 validation completed in `weighted-gh1-matrix` (7/7 smoke).

## Previous checkpoint — owning-shot duration and calibrated handoff (2026-09-04)

The 15-venue startup sweep exposed a shared loader defect that the earlier
three-venue run missed: native Theatre and Arena selected CamShots with a
referenced TransAnim. The timeline read the sampled path's absent frame
timing and reported a zero-second intro. This was not an authored zero-duration
finding. `select_intro_camera_anim` now retains the owning decoded CamShot's
sum of keyframe duration + blend, matching `CamShot::CacheFrames`; the intro
clock consumes that value whether movement is a path or direct poses. There
is no Theatre/Arena branch or guessed minimum duration. Native verification in
`owning-shot-matrix` confirmed 10 seconds for Theatre and Arena, but Theatre's
10-second image still places the camera below the stage. Timing alone is not
visual acceptance. The pre-correction `calibrated-intro-matrix` run is failure
evidence for those two venues despite its compatibility `smoke_pass` flags.

The same loader split also discarded the owning shot's target/parent/base-pose
metadata. Intros now reuse the selected entry in the shared decoded CamShot
registry, which carries source keyframes and path pages together. The native
log identifies this as `source=shared_camshot_loader`. Fresh Theatre verification
in `shared-intro-matrix/theatre/frame_00300.png` still shows the below-stage view.
Retaining the metadata is correct but did not resolve this path-framing defect;
no camera coordinates were patched. Treat that image/video as failure evidence.

Latest verification: `shared-intro-matrix/matrix.json`, executable SHA-256
`3acb29e46a8fcb2bec39bc1077194f3da619972eb3489521752147d04f777491`.
Theatre, Arena, Battle and converted GH1 Basement each pass a 601-frame native
startup run with highway visible, duration 10s, track entrance 8s, and song
release 10s. All reach song time 10.033 with two autoplay hits and no misses.
All six regression tests pass (`shared-intro-tests.txt`). These are startup
checks, not a pass for framing/selection or matched-retail behavior. Only this
four-case subset was rerun after the shared-loader change; the earlier 15-case
matrix belongs to the preceding executable. No build was deployed.

The highway's negative preroll also now uses the same audio-calibration offset
as its post-intro presentation clock. Track entrance/feedback scheduling uses
raw audio-master time after the handoff, so calibration cannot shift its cues.
Numerical checks cover -500, -150, 0, +150 and +500 ms offsets.

Rechecked GH1 crowd-region projection against `0x172900..0x17295C` and
`RndCam::UpdateLocal` at `0x1B1FBC..0x1B2004`: the source radius is
`abs(radius * cot(horizontalFov/2) / cameraDepth) * viewportWidth`.
There is no extra half-radius factor. `RndCam::Project:0x1B117C..0x1B11D4`
applies the half factor to the center coordinates, then viewport scale/offset.
The current full-viewport region score matches this distinction; no speculative
radius adjustment was made. This does not close GH1 crowd-promotion/lifetime
gaps or certify split-screen viewport handling.

### Historical weighted-selection mismatch — CLOSED above

The mismatch described in this checkpoint was subsequently implemented and
validated by the later `GH2 weighted selection` checkpoint. The historical
source recovery remains below for provenance; it is no longer an open gate.

The actual GH2 USA intro call at `0x107570` enters `0x260478`, then
`0x2605B8`, not the later RB3 category-rotation implementation. `0x260110`
collects qualifying shots, excludes already-used entries at offset `+0x110`,
and accumulates the float at shot `+0x44` into manager `+0x38`. If no unused
eligible entry was collected, `0x260234..0x260318` retries while clearing the
eligible entries' used markers. `0x260348..0x2603C0` draws a random value over
the accumulated range, takes the first cumulative threshold >= the draw,
and marks the selected shot used. The wrappers and candidate/category traversal
are retained in `gh2-intro-pick-shot.txt`, `gh2-intro-pick-candidates.txt`, and
`gh2-camera-candidate-score.txt`.

Current intro selection instead prefers path candidates and then the first
entry; regular selection follows later-source bucket rotation. Neither is
certified equivalent to this executable. Next work must reconcile this shared
selector, exact eligibility/reset lifetime, and random-number state. The
serialized pre-rev38 category float is currently discarded by the loader.
The follow-up GH2 Load trace at `0x265088..0x2650B0` proves its destination:
for rev >= 3 it reads the category at `+0x40`, then four bytes into `+0x44`,
the same float accumulated by the selector. Evidence:
`gh2-camera-weight-load.txt`. The float must be retained when implementing
the executable's weighted selector; its current discard is a confirmed gap.
Do not infer a venue-specific shot preference or reuse a later game's rule.

## Previous checkpoint — authored intro timeline (2026-09-04)

The previous six-MIDI-bar pre-song duration and appended 2.5-second highway
phase were not source-correct. GH2 `SLUS_214.47:10746C..1074A4` reads
`game.track_extend_sec`; `107568..107598` picks INTRO/INTRO_ENCORE and starts
the task clock at **negative selected CamShot mDuration / 30**. The scheduling
at `107A54..107A68` subtracts the current clock from track_extend_sec. The source
`ui/gen/game.dtb` creates `{new GamePanel game ... (track_extend_sec -2) ...}`;
it is not a keyed `(game ...)` block. Its extend_track handler starts
TrackPanel immediately, schedules meter entrance at +1.8s, and intro_end at
+2s. TrackPanel's +2.5s task only refreshes player buttons; it does not block
song-clock zero. The existing +2.05s meter sound therefore occurs just after
song start and must survive the handoff.

Evidence: `gh2-intro-controller*.txt`, `gh2-intro-setclock.txt`, and
`gh2-intro-{config,sequence-config}.json` / `gh2-track-sequence-config.json`
under the camera proof directory. The DTB audit decoded all179 GH2 disc DTBs,
skipped none, found the setting only in ui/gen/game.dtb, and records its hash.
The reusable audit tool now accepts --symbol/--before/--after while retaining
its original cam_filter defaults. ISO assets and ARKs were not extracted/copied.

Runtime now derives the presentation length from the selected shot's decoded
frame duration, reads the source property, shows the highway at D-2, and releases
the audio/chart clock at D. Intro-end dispatch occurs before the render-only
presentation clock resets, avoiding a delayed second-duration event. Feedback
uses one signed elapsed calculation across the handoff, retaining the +2.05s
HUD cue. Native GH2 and converted GH1 use this same scheduling contract; the
source six-bar director counter is retained but no longer misused as elapsed
presentation time. Exact constructor/2P and crowd/render parity remain open.

The first native attempt aborted because the new config reader looked for a
keyed game block. That reader was corrected to the actual GamePanel constructor
form; do not treat that failed run as a passing proof. All six regression tests
pass in `intro-timing-tests.txt`, including numeric duration/overlap fixtures
and integration guards. The capture harness now retains a bounded failure-log
tail on abnormal exit and supports --show-highway for visible overlap evidence.
Intermediate timing-only proof executable hash (before preroll correction):
`93a1274fd69f75285f912acb43940b88b21c85d8d56a567d2b3f53fc0c645dca`.
Native rerun evidence is `authored-intro-matrix/`; its JSON records are the
authority for completion of each run. No deployment or retail-parity claim.

The visible-highway rerun exposed another domain error: highway note rendering
used positive camera elapsed time during pre-song presentation. At 8.5s this
displayed a future blue sustain. All three highway draw routes now consume
`highway_song_time()`: presentation-minus-camera-duration during preroll,
then the calibrated song clock. Added numerical/integration regressions pass
with all six selected CTests. New executable SHA-256:
`56532330a298118baed22c4c99c3d980a868ae8c7cd652ecf626791a9c6129e8`.
The 7.5s/8.5s extra PNGs are extracted frames of this native MP4; the corrected
8.5s frame has no early sustain. These replaced intermediate-build extracts.
Nonzero audio-offset continuity at the preroll/song boundary still needs an
explicit check; do not infer it from this default-offset capture.

Final rerun: Basement (converted GH1), Battle and Festival (native GH2), all
601 frames at30Hz from startup with highway/HUD visible, passed with the
56532330 build. Each selected shot reports duration10s, highway start8s and
source property -2; each final summary reaches song time10.033s after20s of
rendering, with2 hits and0 misses under native autoplay. Both private converted
camera snapshots and BMP sequences were removed normally; installed originals
retain their hashes. This is **3/15 (20%) fresh startup coverage**, not an
overall completion percentage. Regression checks are6/6. Retained native MP4s
are in the Basement and Battle folders. Reviewed midpoint stills in all three
venues and extra7.5/8.5s Basement frames; did not inspect every video frame.
The corrected8.5s capture confirms the early sustain is absent. Source framing,
initial rafter obstruction, crowd promotion, and matched-retail coverage remain
open; Festival's midpoint cuts off the top of the guitarist's head. Do not
label these videos accepted retail-parity proofs.

## Previous checkpoint — GH1 SwitchCam player selection

The previous helper integration did not update player selection on shot changes.
Recovered `SLUS_212.24:16FA84..16FAE8` and implemented the source rule: with
exactly two players, authored shot names starting with case-sensitive `Left`
choose index0, `Right` choose index1, and all others choose -1 (centroid). Other
roster sizes select index0. These are **shot-name**, not category/path tests:
`16F414..16F424` reads argument3 of the `switch_cam` DataArray into controller
`+0x70`. The prefix constants are at `311360` / `311368`, confirmed directly
from the ISO's ELF (SHA-256
`484a3b4c90420860ef405da00e78bec4dbfffa803cb8f5062149418b2a04a2c7`).

`start_camera_shot_runtime` queues this operation only for GH1 helper shots;
the camera application consumes it once when the live roster is available.
Selection does not reset the filtered helper. It is not reapplied on each
poll, which would override later explicit player selection. Constructor default
index count-minus-one is still seeded before the initial shot selection; the
exact constructor timing remains unverified, since initialization currently
happens on the first camera submission. Native GH2 does not enter this code.

Additional source evidence: `16DF20..16DFC8` selects a supplied player pointer
by roster membership and immediately reseeds to that player's world position;
`16DFD0..16DFD8` selects the centroid without reseeding. These entry points are
NOT wired into native gameplay yet. The roster itself also needs real 2P
validation: source GH2 FaceOff active-player flags are not a substitute for
GH1's ArenaSinger roster. Tests for prefix boundaries/case, 0/1/2/3 players,
pending selection lifetime, preserved helper position and reset all pass.
All six selected CTests pass (`helper-selection-tests.txt`).

`audit_gh1_camera_policy_matrix.py` now exposes `--start` (default40 unchanged)
so startup can be captured from zero. New proof executable SHA-256:
`961a383445d7068989da09271515455fa4154f338059ab650f59fe0a6421cf54`.
This is an undeployed proof build. Earlier proof hashes below identify their
own runs, not this executable. Startup evidence is in `helper-startup-matrix/`;
final capture duration/results are recorded in its per-venue JSONs.

Final startup run: 961 frames at 30Hz from zero in Basement and Battle; both
native smokes pass and transition from Intro01 to regular gameplay. GH1 logs
17 helper checkpoints with exactly one seed, continuing through poll960 across
the handoff. Both final summaries reach song time14.2s after 32s of rendering;
the existing pre-song presentation consumes the difference. This validates the
runtime handoff, NOT the correctness of that duration against retail. The prior
181/481-frame runs were too short to reach regular gameplay. Their intermediate
stills were superseded, not treated as handoff proof. Private snapshots removed
normally and installed assets remain unchanged.

Visual review of GH1 startup/early/mid/final stills shows initial rafter/wire
obstruction; later frames expose the band but wires remain prominent. Preserve
this as failure evidence, without moving the camera to hide it. This turn did
not review every video frame or certify the GH2 control's visuals. Retained
32-second GH1 native diagnostic: `helper-startup-matrix/gh1_basement/camera-motion.mp4`.

## Previous checkpoint — GH1 helper runtime integration

This section supersedes the earlier helper-core-only checkpoint below. The
helper is now used by the proof executable, not just its unit tests. Original
GH1 `config/gen/gh.dtb` supplies `cam_filter=0.3` to the camera compiler. Each
converted shot records helper-parent/target policy and sampled raw shake
translation. The runtime preserves the helper across shot changes, polls the
live player head, and binds the translation-only result only for source-default
helper references. Explicit source references remain intact. Converted shots
disable the additional GH2 target filter; native GH2 filtering is unchanged.

The recompile command now requires the source game-config DTB before `--out`.
It uses the existing object-aligned MILO block writer: the former fixed-size
splitter exceeded its 128-block table after helper metadata was added. Nonshaky
shots omit redundant zero-shake arrays. The full converter also reads source
game config and rejects a missing filter instead of guessing one.

Evidence: `proofs/camera-full-re-20260903/helper-runtime-matrix/`.
All seven converted GH1 venues plus the native GH2 Battle control completed
181-frame native smoke captures. Each GH1 trace records four helper checkpoints
(poll 1/60/120/180); Battle records none. Battle's submitted camera samples are
identical to `source-curve-matrix/battle/camera.json`. The seven camera MILOs were
recompiled only in a private hardlinked DLC snapshot; installed assets were
verified unchanged and that snapshot was removed. Converter tests pass and all
six camera/gameplay/culling CTests pass. This is NOT matched-retail acceptance.

Midpoint PNGs were inspected individually in all eight cases. Theatre, Small
Club, Small Club Multi, and Big Club still have severe crowd-card obstruction.
Basement, Festival, Arena and Battle expose the performer but do not establish
source framing parity; Festival/Battle are notably dark. The retained Basement
and Theatre MP4s are diagnostic evidence, not approved visual proofs. This pass
reviewed midpoint stills, not every video frame.

Remaining helper gaps are explicit: constructor-equivalent seed timing (current
runtime seeds on its first camera submission), player-selection messages,
actual multiplayer roster integration, and the empty-roster arena fallback
(covered by the core test but not wired into gameplay). Missing player heads
currently emit an error and retain the camera. Source crowd 3D promotion and
full startup/switch/lifecycle comparisons remain open. Do not claim complete
GH1/GH2 parity or fix these with per-venue camera positions.

Proof executable SHA-256:
`bee5f855a08901e0b179e10ab1eec54bf8f57464c887bc7b58ee080e637c114e`.
Camera converter SHA-256:
`2182c61617380e80d8d065af100421966343c9e5816338f8ca87be321793798b`.
Neither the installed executable nor installed converted assets were deployed.

## PS2 executable evidence and corrections

Source binary: USA GH2 `SLUS_214.47`. ihatecompvir's public source supplies
class/function context; the GH2 MIPS body determines platform-specific behavior.

| Routine / address | Verified behavior | Runtime correction |
| --- | --- | --- |
| BuildTransform `0x267008..0x2676c0` | `filter == 0` copies the live target; a nonzero filter uses projected target error | Zero-filter target caches no longer freeze |
| BuildTransform `0x267394..0x2673a8` | Copies authored rotation in the non-path branch | Removed travel-direction-derived basis from production seed construction |
| BuildTransform `0x2673ac`, `0x2674f0..0x26754c` | Height clamp is in the resolved-parent branch and requires one authored target | Added parent and authored target-count gates |
| Interp `0x2666b8`, `0x2666d4`; BuildTransform `0x267550` | Outgoing and incoming builds receive `!SameTargets`; false skips both aiming and screen translation | Same-target transitions now blend source transforms before aiming, not aim twice |
| Matrix3 Interp `0x2da808..0x2da878`; Quat Interp `0x2da6d0..0x2da804` | Matrix-to-quaternion, hemisphere correction, normalized linear interpolation, quaternion-to-matrix | Final camera blends now use this rotation path rather than separate forward/up interpolation |

Captured writer/seed overrides now require explicit diagnostic opt-ins:
`GHOGX_DEBUG_CAMERA_TRACE_WRITER_BRIDGE` / `GHOGX_DEBUG_CAMERA_TRACE_SEED`.
Normal gameplay does not apply them automatically.

The preceding work also recovered the shared ShotOk/check_shot and Shake
bodies. Those remain subject to the full parity gate below; a successful build
or positive source-contract test does not certify their visible behavior.

## Verification

- `ghogx_camera_filter_test`: numerical target tracking, projected-error gain,
  opposite quaternion signs, half-turn midpoint, normalization and monotonicity.
- `ghogx_gameplay_venue_band_contract_test`: source-order and integration guards.
- `tools/audit_camera_venue_matrix.py`: native, hidden-process, no host input;
  uses stock `funk1`, normal camera selection and active chart autoplay. Hides
  highway/HUD only to expose the camera view. Records exact command, camera
  samples, selection events, runtime summary and native PNGs; optional MP4s.
  Diagnostic/concurrent-run performance counters are not release FPS benchmarks.
- Coverage: `battle`, `small1`, `small2`, `big`, `theatre`, `fest`, `arena`,
  `stone`, `gh1_basement`, `gh1_small_club`, `gh1_small_club_multi`,
  `gh1_big_club`, `gh1_theatre`, `gh1_fest`, `gh1_arena`.

Latest evidence: `proofs/camera-full-re-20260903/matrix/matrix.json`.
Each row is explicitly **native compatibility**, not matched-retail parity.
Final run: **15/15 native smoke checks passed**, with two selection events per
venue, finite submitted camera samples, and three native screenshots per venue.
All six relevant CTest checks passed. The Battle and GH1 Theatre folders also
contain 12-second MP4s; the Theatre clip is failure evidence, not acceptance.

Visual review has **not passed** the converted GH1 Big Club and Theatre cases:
the sampled camera can point away from the band or be obstructed by the venue.
The Big Club trace resolves its guitarist-head target and arena parent, reaches
the normal GH2 solver, and submits a translated/aimed transform; this is not a
missing-target fallback. Its converted shot carries a screen offset of
`(-1.7, -0.75)`. Compare the original VenueCam framing/offset contract with the
converted CamShot contract before changing either. Do not restore an inferred
travel-direction basis or add venue-specific camera positions to conceal it.

The harness initially mounted repository DLC instead of staged converted DLC;
those blank GH1 captures failed and were rerun with explicit `--addons-dir`.
Telemetry now reads `[camera-result] stage=submitted`, not the `[camera]` line's
authored `eye/at`. These are different spaces and must not be confused.
The audited executable SHA-256 is
`3efd40c141ddde8e38d97e5dfa311a67465f8468b35a52247a48e6c039c57fde`.
It is a proof build, **not deployed as a completed camera fix**.
Cleanup restrictions and exact remaining scratch paths/sizes are recorded in
`proofs/camera-full-re-20260903/CLEANUP_PENDING.md`.

## Remaining completion gate

- Match retail memory traces for representative parent/no-parent, same/different
  target, moving path, transition, clamp and shake cases; compare time-aligned
  matrices and FOV, not just visually plausible shots.
- Extend the now-passing initial six-downbeat handoff coverage to seek/reset,
  looping, shot-over and multiplayer selection using executable evidence and
  live samples.
- Audit remaining path-evaluation and projection contracts before removing
  historical `partial` / `locals_only` diagnostic labels.
- Keep the camera goal open until those checks and visual review pass.

## GH1 shared path-policy correction (continued audit)

GH1 USA ELF is read in memory from `/SLUS_212.24;1`, without extracting the
disc. `tools/audit_gh1_camera_source.py` similarly reads the 4,397-byte
`arena/gen/cam_paths.dtb` directly from the ISO's ARK. Policy SHA-256:
`001563f95b08a81bdbf3fbc92d93815573edab8b1f775ce19f805e0e40aa8d4e`.

| GH1 address | Recovered contract |
| --- | --- |
| `0x16e080..0x16e264` | Default ArenaSinger target-position resolver, **not** the complete update routine |
| `0x16f078`, `0x16f160` | Query `SystemConfig(arena, cam_paths, path)` for `parent` / `target` |
| `0x16fbc8..0x16fcc4` | Resolve those overrides; absent overrides use the live player-position helper |
| `0x16e3e0..0x16e468` | Evaluate shake transform into stack `+0x00`, or identity |
| `0x16e71c..0x16e754` | Add shake translation to filtered helper position |
| `0x16e780..0x16e810` | Evaluate path, then multiply shake by path |
| `0x16e818..0x16e8d0` | Add interpolated offset, then compose explicit/default parent |
| `0x16e964..0x16ea98` | Aim at selected target, then translate in camera axes by screen-offset/projected-distance contract |

The previous blanket parent/target conclusion was wrong: `arena` at the start
of `switch_cam` is the message receiver, not the camera's transform parent.
The compiler now requires the shared policy DTB, emits explicit refs where
authored, and uses translation-only player-head parenting for the default
helper. The exact source `if_else {exists ref} ref fallback` form is retained
as TypeProps and resolved against live objects before the shared camera solver.
Unknown paths/expressions are rejected, not guessed. Stock GH2 keys have no
such fallback properties and retain their existing path.

Big Club `flr_near_lft01x2w` uses `Cam_nt_np_close`, whose target is
`arena::stage_spot_01.mesh`, not the guitarist head. The first isolated native
run then exposed a separate lookup defect: only `arena::venue.view` had been
published to the solver. Runtime now resolves all requested authored arena
refs, preferring Group/View WorldXfm over diagnostic centroids. This includes
non-draw helpers loaded from lighting sections.

`milo_convert_tool rebind-gh1-camera-paths` migrates only bindings/conditional
metadata in existing converted MILOs; curve bytes, geometry, and animations
are not regenerated. Native round-trip and idempotence tests cover migration.
`tools/audit_gh1_camera_policy_matrix.py` uses a temporary hard-linked DLC
snapshot with private migrated MILOs/index, verifies installed hashes, and
removes the snapshot afterward. This is proof-only, not deployment.

**Still open:** matched-retail framing review; the current converter's additive
shake approximation has not yet been reconciled with the full transform/helper
composition above. Default helper targeting in multiplayer also needs explicit
coverage. Do not treat the path-policy correction as full camera parity.

Native rerun: `proofs/camera-full-re-20260903/path-policy-matrix/` contains
**8/8 compatibility passes** (seven GH1 venues plus GH2 Battle), using executable
SHA-256 `e28a3c0ea8e9bd71010720fd37c9245f102eccebc2269ec00d3c99565e27830d`.
All seven migrated camera files round-tripped and the installed originals kept
their hashes. Temporary hard-linked snapshots were removed. Six engine checks
and the converter test pass. This is not a release deployment.

Visual review was of each venue's midpoint PNG, plus Theatre's ending PNG,
not every video frame. Big Club now frames the guitarist rather than the
ceiling. Theatre and Small Club remain obstructed; Small Club Multi also has
heavy crowd foreground. Basement, Festival, Arena and Battle show the guitarist
but are not certified against a retail frame. Two six-second clips retain the
Big Club improvement and Theatre failure. `theatre-target-diagnostic/` confirms
the head and parent resolve, with head near `(147.573,462.978,48.982)`; this is
not a missing head target.

### GH1 crowd-region driver — recovered, integration still WIP

`crowd_region` now reaches shot-start runtime code and the GH1 renderer. This
does **not** complete crowd or camera parity. Earlier docs substituted frustum
culling for this separate source mechanism; that claim was incorrect.

Source is GH1 USA `SLUS_212.24`, read in memory from the ISO:

- `0x16FE98..0x16FEA0`: SwitchCam calls Crowd region selection before the new
  path is polled. Selection is not recomputed every rendering frame.
- `0x171F58..0x1720AC`: discover consecutive `crowd_limits%02d.mesh` from 00,
  stopping at the first missing mesh.
- `0x170DA8..0x171434`: invert each region's full world transform and test
  original MultiMesh instance origins. Local Z must be strictly between zero
  and `crowd_flat_height` (100.8 in `system/run/config/gen/arena.dtb`).
- `0x1E3600..0x1E37AC`: XY triangle coverage, inclusive barycentric edges,
  either winding, two division branches using a 1e-5 X-axis threshold. This is
  not a bounding-box or camera-frustum test.
- `0x1712A8..0x171424`: accepted world origins establish the region bounds;
  sphere center is their midpoint and radius is the full bounds diagonal.
- `0x1727B0..0x172A00`: explicit nonnegative region index, or negative auto
  selection. Auto uses projected center and radius, scoring
  `min(projected_radius,15)/max(distance_to_screen_center,0.2)`. It uses a
  strict winning-score comparison and defaults to the last region.
- **Critical list direction:** `0x172A88` restores previous held instances to
  the flat MultiMesh; `0x172B04` removes newly selected instances from it.
  `0x2AA0A8` is the doubly-linked list transfer primitive confirming direction.
  `0x172BA8..0x172C40` assigns selected transforms to the 3D character pool,
  replacing their Z with the region plane Z. Region membership is therefore
  a **flat exclusion / 3D promotion list**, not the set of flat cards to keep.

`render/gh1_crowd_regions.*` implements membership and selection, including
full affine inverse, source score and restoring flat availability on a region
switch. Only the converter's `__gh1_runtime_multimeshes.grp` opts in; native
GH2 WorldCrowd is untouched. `ghogx_gh1_crowd_regions_test` covers these rules.
An initial keep-list experiment was rejected after native inspection and the
list-transfer trace; the code and retained proof use exclusion, not keep-list.

**Remaining:** wire the GH1 3D pool with source actor order, capacity, animation
and region-plane height. It is NOT currently rendered as replacement characters.
Auto selection currently uses the previous native submitted projection; on the
first shot without a submitted camera it is deferred to first submission. That
startup camera ownership/timing needs a matched retail trace. Empty/malformed
regions fail safely rather than emulating retail undefined data. The raw-GH1
fallback and intro path still need explicit metadata coverage.

`crowd-region-matrix/` contains three native compatibility passes (Theatre,
Small Club, GH2 Battle), 181 frames each and a six-second Theatre clip. Six
targeted CTests pass. In this run Theatre selects 12/500 then 11/500 instances;
Small Club auto selects region 13 (12/161). Counts in that capture are labeled
`promoted`, meaning selected for promotion, NOT that 3D replacements exist.
Later logs explicitly label `flat_excluded` and `replacement_3d_pool=not_wired`.
Battle emits no region events and its submitted camera samples exactly equal
the preceding path-policy run. Installed assets were hash-checked unchanged.

Theatre and Small Club midpoint PNGs still fail visual framing: foreground
crowd/venue objects obstruct the performer. The Theatre ending was also
inspected in the initial experiment; no frame-by-frame video review was done.
Do not report these clips as success or hide extra geometry to make them pass.

## GH1/GH2 projection convention — source-verified correction

The games do not store the same FOV convention. GH1 USA RndCam::UpdateLocal
(`SLUS_212.24`, `0x1B1FBC..0x1B2004`) computes `mxx=1/tan(fov/2)` and
`mzx=-1/(tan(fov/2)*YRatio)`: horizontal FOV. GH2 USA
(`SLUS_214.47`, `0x1B2054..0x1B2094`) computes `mxx=YRatio/tan(fov/2)` and
`mzx=-1/tan(fov/2)`: vertical FOV. The shared GH2 runtime projection remains
unchanged. GH1 source data must be adapted, not native GH2 camera behavior.

Evidence: `proofs/camera-full-re-20260903/gh1-retail-fov-evidence.json` reads
the original GH1 PCSX2 slot-2 save directly, without launching the emulator or
operating its window. Its native `Screenshot.png` is retained as
`gh1-retail-state.png`. The saved Basement `flr_near_lft01` camera has 45-degree
horizontal FOV, mxx=2.4142139, mzx=-3.2189519, YRatio=0.75. The saved helper
projects to the authored (-0.5,0.4) target within 1.4e-7. This is a retail
projection oracle, not a time-aligned comparison of the native animation.

Correction: `vertical=2*atan(tan(horizontal/2)*0.75)`, so 45 degrees becomes
34.515874 degrees. The source 4:3 ratio is used even when the output window is
widescreen. Implemented for the GH1 VenueCam compiler and both legacy raw-GH1
regular/intro loading paths. Fresh compiled curves adaptively subdivide after
the nonlinear FOV conversion. `gh1_fov_space=gh2_vertical` marks converted
shots; the existing source-policy migration converts unmarked GH1 shot keys
once and preserves native GH2 shots. Unknown nonempty markers are rejected.
Migration is idempotent, but changes existing key endpoints only: release
payloads with varying FOV still need fresh conversion/resampling before parity
is claimed. The installed DLC is not modified by these proof runs.

Converter tests cover the saved projection coefficients, varying-FOV curve
timing, fixed 60-degree conversion, migration preservation and idempotence.
Runtime source-contract checks cover both legacy call sites. Full camera
parity remains OPEN: GH1 filtered target-helper behavior, shake composition,
crowd 3D promotion, startup/lifecycle/multiplayer and matched-retail coverage.

`fov-matrix/` has eight native compatibility runs (all seven GH1 venues plus
GH2 Battle), three native PNGs each and six-second Basement/Theatre videos.
All eight smoke checks pass; Battle submitted samples exactly match the prior
crowd-region run. Midpoint visual review of Basement, Theatre and Small Club
still rejects Theatre/Small Club obstruction. The other five midpoint images
have not been reviewed in this pass; no frame-by-frame video review is claimed.
`fov-judy-basement/` adds a six-second run requested with `alterna1` and variant
`gh1_alterna`; its midpoint was inspected. Neither run is time-matched retail
parity. These captures use the pre-fallback-change native executable SHA256
`4fe571cbc373092ee48bb81106b0d7dd3b76ec1478e7cbe9bfa6fc840465e560` and corrected
converted shots; the legacy raw paths are not exercised by them.

## GH1 shake composition and target-helper audit — continued

GH1 `0x16E804..0x16E810` calls `Multiply(shake, path, path)`; its matrix routine
`0x24B608..0x24B660` forms row-vector products. The converter now uses the full
product: `R=Rshake*Rpath`, `T=Tshake*Rpath+Tpath+offset`. Previously it added the
unrotated shake translation and discarded all shake rotation. Rotation-only
shake is retained even if the main path has no authored rotation keys. Adaptive
subdivision runs on the composed result. Unit fixtures test different rotation
axes (so reversed order fails), transformed translation, and identity-path
rotation-only shake. `milo_convert_test` passes.

This corrects the authored path component, NOT the complete GH1 shake driver:
the original also adds shake translation to the filtered helper. That live
helper and its feedback state still need implementation; source TransAnim
scale/repeat behavior and shake scheduling remain parity gates.

`milo_convert_tool recompile-gh1-cameras` replaces only compiler-owned
`gh1_venue_camera` objects from original camera DTBs, existing converted
campaths, and the shared animation source. It leaves unrelated objects intact
and verifies the resulting native directory round trip. The proof harness's
`--recompile` reads each `venues/<source>/gen/camera.dtb` and
`../../system/run/arena/gen/fx.rnd_ps2` directly from the GH1 disc, converts the
small shared FX file privately, and resamples camera curves without redoing
venue geometry. All scratch and hard-linked DLC snapshots are removed afterward.
This also provides the fresh-curve route required by the FOV correction above;
installed/release DLC has not been regenerated in place.

`source-curve-matrix/` contains 8/8 native smoke passes, all seven GH1 venues
plus Battle, with three PNGs each and six-second Basement/Theatre MP4s.
All eight midpoint PNGs were inspected. Theatre, Small Club, Small Club Multi
and Big Club still fail because crowd cards obscure the view. Basement, Fest,
Arena and Battle expose the performer at the inspected instant, but are not
certified time-matched retail framing. No sequential video-frame review or full
shot-library coverage is claimed. Battle submitted samples exactly equal the
preceding FOV run. Native executable SHA256 is
`d16fcd0be517d592d480f17c93f46337929aa6a8546e3c83626079974ee62321`;
converter SHA256 is `fa0123dbe2fb61adc8646275181114da0c083cdd6fdcdb85b4792ccd480c6b90`.
The migration report proves all installed source files unchanged and scratch
removed. Its per-venue results identify fresh recompilation (the top-level mode
and source-DTB/shared-FX hashes were added to the harness afterward for future runs).

Helper RE findings for the next integration step:

- `0x16DB34..0x16DB58` reads `SystemConfig(arena).cam_filter` into controller
  `+0x2c`. Config layering is now resolved: `config/gen/gh.dtb` has an explicit
  `(arena ... (cam_filter 0.3) ... #merge ../../../system/run/config/arena.dta)`.
  The shared engine arena config's 0.1 is not the game's effective setting.
  `gh1-camera-config-layers.json` records all matches from 156 decoded source
  DTBs (none skipped), with exact asset hashes. The saved controller's 0.3
  agrees. Carry the game config value into the converted driver rather than
  deriving it from a savestate or inheriting the shared engine's default.
- `0x16E604..0x16E6B8` projects the live player point with the current camera,
  measures Euclidean viewport error against the interpolated desired screen
  point, then uses `cam_filter * min(error,1)`. A zero resulting step retains
  the cached helper (`0x16E6CC..0x16E718`); it does NOT use GH2's disabled-filter
  bypass. Filtering belongs to the persistent helper, not independently to each
  resampled CamShot frame.
- The saved player's virtual transform callback resolves to `0x18D3C0`, which
  looks up `bone_head.mesh` (`0x316CA0`) and returns its world transform. The
  default helper resolver `0x16E080` chooses controller `+0x74`'s player index;
  a negative index averages the active players. The empty roster uses the arena
  fallback position with +60 Z. Initialization seeds this helper at
  `0x16DD74..0x16DD84`. Source startup/switch lifetime and multiplayer tests are
  still required before replacing the existing live-head approximation.

### Helper implementation checkpoint (not wired into gameplay yet)

`engine/src/game/gh1_camera_helper.h` implements the recovered player-point
selection, viewport-error filter step, and persistent helper update. It has an
explicit constructor seed: polling uninitialized state fails instead of silently
seeding from a later camera frame. Zero gain retains the previous helper;
current shake is added after interpolation, including feedback from the previous
shake. Indexed/averaged/empty-roster cases are tested; an invalid positive player
index is rejected rather than guessed. `ghogx_camera_filter_test` passes with
these regressions and the existing GH2 checks (`helper-core-tests.txt`).

This header is currently used by tests only, NOT by the game executable. No new
native visual proof is claimed for it. Integration must publish the helper for
default GH1 parent/target bindings, carry the effective game config and sampled
shake translation, and keep its state through shot/key changes while clearing
it on controller teardown. The current `CameraResultBuilderState::reset()` is
called on shot start, so placing the helper there without splitting lifetimes
would be incorrect. Remove the additional GH2 per-key target filtering for
these converted shots once the helper is authoritative; native GH2 must retain
its existing filtering and lifecycle. Player selection messages and the active
multiplayer roster must be covered before claiming multiplayer parity.

## Separate upstream finding

In the initial Battle trace with `glam1`, the live `bone_pos_guitar` attachment
was approximately `(133.32, 183.56, -275.03)` before performer placement.
The camera correctly received the resulting below-stage guitar target. This is
an animation/attachment defect to investigate separately, not a reason to add a
camera offset or use a different camera for that character. `funk1` is the fixed
test performer to isolate the driver while that upstream issue remains open.

## Big fan / pre-song event-clock correction (2026-09-04)

The [user-confirmed retail recording](https://www.youtube.com/watch?v=VUdVOPwnLKc)
shows Big's mechanical fan moving before the song while the authored
dynamic-light projection moves through it. Original
GH2 USA data ties `fan.tnm` and `fan_floorspot.mnm` to the same four-second
`music_start` filter. The PS2 Game poll feeds the running song/beat clock to the
EVENTS reader during the arena intro; Shout at the Devil's marker is due at
7.058192 seconds, before the ten-second intro finishes.

The shared engine now polls that authored `music_start` during the intro while
leaving ordinary section events, notes, scoring and audio behind the intro
gate. It no longer relies on the disabled startup excitement trigger. Native
build `fd55e4b05595d2b190d9d1edee5f8b08ec2a84bcb6680326b0f08e768641d76d`
dispatches once at 7.066667 with `intro=1`; fan and floor-spot transforms both
advance under the source filter. The 61-frame corrected visual slice was
reviewed sequentially with no skipped frames and no backward tick, freeze or
pose reset. Evidence is under
`proofs/camera-full-re-20260903/big-fan-retail-route-20260904/` and
`big-fan-uv-proof-20260904/`.

This closes the identified fan startup-route defect only. Full shared camera
parity and all-venue matched-retail coverage remain open.
