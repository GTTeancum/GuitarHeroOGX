# Practice, previews, navigation audio, and shared camera verification

**Resumed and completed on September 11.** Runtime fixes are installed; the
[final camera/crowd record](../crowds-3d-20260911/README.md) supersedes the camera
and crowd open-status notes below.

This record covers the original TO_DO items 6–9 requested together on September
9, 2026. All captures use the game's own renderer and process-local test input.
No desktop input, screen takeover, or external capture application was used.

## Practice presentation

- [Before](practice-before.png): missing highway and incorrectly composed credits.
- [Repaired title](practice-title.png): original Practice Room, live GH2 highway,
  title / “as made famous by” / artist, using the stock MTV font and camera.
- [Playing notes](practice-playing.png): actual chart notes and gameplay judgment.
- [End of Practice](practice-end.png): the stock result panel receives actual hits
  and misses. Practice results do not enter the Career result path.

`practice.dtb` declares `practice_panel`, `game`, `hud`, `track_panel`, and
`mtv_overlay_panel`. The missing runtime connection was between the Practice
loading/game screen and Gameplay's highway renderer. Practice now prepares the
selected chart and audio without loading a stage world, retains the authored
room, and draws the live highway over it. `game.dtb` provides the title/caption/
artist ordering. The shared song-intro renderer prevents duplicate generic
menu labels and uses a presentation clock that continues after a zero-length
camera intro.

The native process-local route also exercises Training → Practice → song →
part → difficulty → sections → speed → gameplay → pause → resume → restart.
The stock restart command now reloads Practice. Empty rock meter does not stop
Practice judgment; a focused session test checks a miss followed by a later hit.
The normal failure behavior remains enabled. This work restores presentation;
it is not a certification of every Practice speed/section gameplay feature.

The end-panel proof exposed another shared interpreter defect: `sprintf`'s
literal `%%` consumed an argument, displaying `100% (8/0)` instead of `8/8`.
The formatter now leaves arguments untouched for a literal percent, with a
regression test using the stock Practice format.

## Song previews and Soundcheck

The stock setlist panel issues `song_preview` when its highlighted song changes
and a zero-argument stop on exit. The host now implements both. It streams the
song's VGS from the selected content archive, seeks to the authored millisecond
preview interval, loops at its end, coalesces requests within one update, and
honors Practice's explicit interval. Direct screen startup no longer restarts
menu ambience over a preview after scripts stopped it.

Native verification played Shout at the Devil (65.5–85.5 s), Mother
(61.1–81.5 s), and Surrender (46–66 s), then stopped on Back. A separate
24-second real-time run observed the authored Shout at the Devil loop.
See [menu telemetry](menu-verification.json) and [setlist](setlist.png).

Stock `sfx.dtb` maps `SCROLL_MSG` to `button_toggle`. Soundcheck's existing
source route in the worktree is correct; rebuilding and installing it supplies
that route. Native Down/Up events each submitted the `button_toggle` sound
(the bank chooses clap3/clap2 variants). No speculative substitute sample was
introduced. See [Soundcheck](soundcheck.png) and menu telemetry.

## Camera driver and GH1 release package

The 15-venue native matrix used executable
`eb73d0164c83f877990ecf92b9f1a8b500d70aa19147e7cd55969a77f34efba0`.
All 15 passed, with 361 submitted transforms each (5,415 total). The final
matrix's 45 native stills are byte-identical to the reviewed candidate's stills;
all camera samples are also identical. The later installation changes only
Practice result publication and literal-percent formatting, not camera code.

- [Trace integrity and boundaries](camera-verification.json)
- [Retained native transforms](camera-traces.json.gz)
- [GH2 Battle / Small1 / Small2](camera-1.png)
- [GH2 Big / Theatre / Fest](camera-2.png)
- [GH2 Arena / Stone / GH1 Basement](camera-3.png)
- [GH1 Small Club / Small Club Multi / Big Club](camera-4.png)
- [GH1 Theatre / Fest / Arena](camera-5.png)

Native smoke is only the compatibility gate. The source gate additionally uses
the recovered executable contracts, saved retail final-camera fixture, shared
filter/rotation/selection/path tests, source animation-task tests, projection
tests, and the retained retail handoff trace indexed in the
[shared driver audit](../../CAMERA_DRIVER_AUDIT.md). This certifies the recovered
normal PS2 driver contracts and these tested inputs, not bit-exact equivalence
for every possible retail state or physical-PS2 floating-point edge case.

The GH1 release camera regeneration and 3D promotion pool were already complete;
the queue described older work. All seven installed venue MILOs were extracted
and compared against the refreshed release: every object name and object body
matches, despite different container bytes. [Package evidence](gh1-payload-verification.json).
This covers the source FOV curves, path/parent policy, selection weights, and
other objects, without replacing or downgrading the installed packages.

A fresh [saved retail crowd oracle](retail-crowd-oracle.json), using the current
native crowd implementation, matches all 92 origins, all 12 ordered regions,
and the saved live selection. The verifier's nine adversarial tests also pass.
This is independent retail memory evidence, not a second copy of our algorithm.

**Crowd presentation remains a separate open requirement:** the user's standing
order is to replace *all* sprite/billboard crowds with 3D characters. Retail
region promotion alone does not satisfy it. The retained sprite-heavy views
are not approved final crowd presentation. This requirement is explicit in
TO_DO.MD and is not waived by camera-source parity.

## Test boundary and installation

The focused camera, projection, render-matrix, crowd-region, source-animation,
MILO-scene, gameplay-session and UI audio-contract checks pass. The broader
venue/band structural test has the same 15 source-string failures as the
pre-existing executable; its failure list is unchanged. The broad UI suite's
13 failures also reproduce on the pre-existing executable. Neither broad
suite is claimed green. See [deployment and test record](deployment.json).

Only the live runtime executable was installed. Existing character DLC and
venue packages were preserved. Scratch builds, raw extracts, verbose logs and
duplicate captures are disposable; this directory retains the compact evidence.
