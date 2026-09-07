# TO-DO

This file contains open work only. Completed work belongs in Git history,
proof manifests, and technical documentation—not in this queue.

## Open work at a glance

1. Certify calibration on real hardware.
2. Add original-Xbox support for Xbox 360 guitar hardware.
3. Decide whether to add a later-game-style HOPO glow.
4. Prove the complete mixed-content gameplay matrix.
5. Convert selected Rock Band 2 characters to release-quality GH2 assets.
6. Add a data-driven venue selection screen.
7. Restore source-accurate Practice mode presentation.
8. Restore setlist song-preview playback.
9. Correct the Soundcheck up/down navigation sound.
10. Complete shared camera-driver retail parity certification.
11. Add data-driven setlist buckets and preference-aware launch controls.

## Details

1. **Real-hardware calibration certification**

   Test representative guitars, displays, and audio devices. Record measured
   audio/video defaults and confirm note judgment without widening the hit
   window. The software timing path is implemented; hardware certification is
   the remaining release work.

2. **Original-Xbox support for Xbox 360 guitars**

   a. Implement wired Xbox 360 X-Plorer enumeration, initialization, report
   parsing, frets, strum, Start/Back, whammy, and tilt/star power.

   b. Implement the Xbox 360 Wireless Receiver: enumeration, initialization,
   connection state, input packets, pairing, and required LED/status commands.

   c. Support one receiver and one active wireless instrument per physical Xbox
   controller port. Rebind after disconnect; do not implement multitap fanout.

   d. Treat standard-enumerating PS2-to-Xbox guitar adapters as protocol
   translators and add only mapping/whammy validation unless their descriptors
   prove that driver work is required.

3. **Optional HOPO glow**

   Decide whether to add a distinct HOPO glow in the style of Guitar Hero III
   and later games. Remove this item if the feature is rejected.

4. **Complete mixed-content matrix**

    Prove GH1/GH2/GH80s songs, characters, and venues can be independently
    combined while GH2 remains authoritative for gameplay, highway, HUD, title
    card, scoring, and UI. A foreign character may supply only its authored
    unique highway texture. Run converted packages with GH1 compatibility paths
    disabled and cover the full supported gameplay cross-product.

    Investigate the native `glam1` guitar-attachment transform found below the
    stage during the camera audit. Correct its animation/attachment provider,
    not the cameras that consume that target. See `docs/CAMERA_DRIVER_AUDIT.md`.

5. **Selected Rock Band 2 character conversion**

    Turn the successful Duke of Gravity experiment into a reproducible,
    release-quality pipeline for a deliberately selected RB2 character roster.
    Preserve each character's source-authored identity and proportions while
    producing native GH2-compatible models, skeleton bindings, animations,
    materials, and DLC metadata. Do not solve characters with per-character
    transform, limb, or attachment exceptions.

    The additive Duke of Gravity package establishes the general body-shape,
    material, atlas, AO, GH2 rig, active-animation hand, instrument attachment,
    preferred-guitar, and loose-DLC packaging path. Its source-authored RB2
    height/weight deformation is baked across all vertices, fourteen textures
    are reduced to three atlas pages, transparent lenses are preserved, and
    the native proof holds 60 FPS. Apply this pipeline to a deliberately
    selected roster rather than adding per-character fixes.

    Ship every converted character as additive DLC with a unique character ID,
    portrait, outfit data, preferred guitar, and source-derived metadata. The
    Duke now has an additive identity and leaves Clive Winston unchanged.
    **Work in progress — Penelope McQueen:** her source-authored RB2 body,
    outfit, and proportions are converted as additive DLC, with Casey Lynch
    supplying the GH2 guitarist animation/control graph. Her current mouth
    deformation is visibly malformed, and the necklace alpha still has heavy
    edge/background artifacts. These must be corrected before her conversion
    can pass release review. The deployed package has passed a native auto-start
    song smoke test without replacing a stock character, but that is not visual
    acceptance.
    Remaining work is to author each selected prefab recipe, choose a
    source-appropriate GH2 animation donor, produce final GH2-style portraits,
    close facial-animation parity where applicable, and run the same native
    idle/strum/fret/movement/camera/lighting/performance acceptance matrix for
    every selected character.

6. **Venue selection screen**

    Add an in-theme, data-driven venue selection screen based on the supplied
    HTML design mockup in the repository-root
    `Guitar Hero Venue Selector.zip`. It must expose native and DLC venues
    without hard-coded roster entries, preserve GH2 as the primary UI language,
    and integrate with Quickplay venue choice and Manage Band venue preferences.
    The archive is recorded for later review and has not yet been
    inspected.
    The complete 14-image venue preview set was approved on 2026-09-07. On the
    selector, show the chosen preview in the left pane, center-framed
    vertically, with a tentative -3 degree rotation that remains tunable during
    the final layout pass. Use the canonical copies in
    `proofs/manage-band-20260906/venue-preview-approved/`.
    The Manage Band Favorite Venue submenu now implements that preview
    treatment and switches the cached image from the highlighted stable venue
    ID. The remaining item is the standalone Quickplay venue-selection screen
    and its final interaction/layout acceptance.

7. **Practice mode presentation**

    Restore the missing GH2 gameplay highway and correct the backwards
    song/title/artist text ordering. Preserve the original messy Practice Room
    backdrop and trace the final presentation against the retail source.

8. **Setlist song previews**

    Restore source-accurate song-preview playback when the highlighted setlist
    entry changes.

9. **Soundcheck navigation sound**

    Replace the current Soundcheck up/down sound with the source-correct menu
    navigation cue.

10. **Shared camera-driver parity — in progress**

    Finish the executable/memory-trace audit of camera targets, rotation blends,
    path evaluation, shake, lifecycle and selection. Validate all 15 GH2/GH1
    venues as inputs, without venue-specific fixes. Native smoke coverage alone
    is not retail parity. See `docs/CAMERA_DRIVER_AUDIT.md` for evidence and gates.

    Regenerate the release GH1 payload with source-correct horizontal-to-vertical
    FOV curves (compiler and legacy loading paths corrected; proof copies only).
    Restore GH1 shared camera-path parent/target policy in the release payload,
    and finish `crowd_region` integration: source-selected instances leave the
    flat list and enter a 3D character pool (not yet wired). Verify automatic
    selection startup timing and fix the still-obstructed close-ups using
    source evidence. Verify complete shake/helper-transform composition.

11. **Setlist buckets and Quickplay launch controls**

    Add switchable, ordered Quickplay setlist buckets sourced from the merged
    catalog. GH2, GH1, and GH80s remain distinct setlists. DLC manifests may
    define additional buckets through their existing `setlists` records; any
    Quickplay song not assigned to a declared bucket appears in one implicit
    `DLC` bucket. Keep this separate from song-preview restoration.

    Yellow switches setlist buckets by replacing the currently displayed song
    list with the selected bucket's complete setlist and resetting the highlight
    appropriately. It does not jump to another point in one combined master
    list. Do not add an Orange action or a per-song manual
    guitarist/guitar/venue screen. The setlist footer order is exactly:
    Green = play with game/song defaults, Red = back, Yellow = switch setlist,
    Blue = play with saved Manage Band preferences, Up/Down = navigate songs.
    Backing-band outfit preferences remain active in both launch modes; the
    Green/Blue distinction controls the Quickplay guitarist, guitar, and venue.
