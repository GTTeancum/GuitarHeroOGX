# TO-DO

This file tracks open work and standing requirements. Completed work belongs in Git history,
proof manifests, and technical documentation—not in this queue.

## Open work at a glance

1. Certify calibration on real hardware.
2. Add original-Xbox support for Xbox 360 guitar hardware.
3. Decide whether to add a later-game-style HOPO glow.
4. Prove the complete mixed-content gameplay matrix.
5. Convert selected Rock Band 2 and Neversoft Guitar Hero characters to release-quality GH2 assets.
6. Add data-driven setlist buckets and preference-aware launch controls.

## Standing requirements

Crowds must use actual 3D characters throughout the game, including converted
GH1 venues and future conversions. Replace the entire visible sprite/billboard
population, not just camera-selected retail promotions. Source sprite retention
is not an acceptable fallback. Preserve appropriate visibility, density and
animation, and verify native visuals and performance.

Implementation and all-venue evidence: [3D crowd verification](docs/proofs/crowds-3d-20260911/README.md).

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
    the native proof holds 60 FPS. His forearms still show visible deformation
    problems in the Manage Band preview and therefore have not passed visual
    acceptance. Before selecting a release roster, evaluate baking useful RB2
    bump/specular detail into the diffuse atlas alongside AO so the PS2 target
    retains more of the source material definition. Apply pipeline-level fixes
    to a deliberately selected roster rather than adding per-character
    exceptions.

   Midori from Guitar Hero 3 has been certified by the player. Additional Neversoft characters and outfits will be requested by the user.

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

    Current proposed conversions from user:
    1. The Duke of Gravity (Wii RB2)
    2. Penelope McQueen (Wii RB2)
    3. Moosejaw Bordreau (Wii RB2)
    4. Mothership Q (Wii RB2)
    5. Elroy Budvis (PS2 GH3)
    6. Casey Lynch outfits (PS2 GH3)
    7. Xavier Stone outfits (PS2 GH3)
    8. Clive Winston outfits (PS2 GHWT and GH Metallica) - user to supply disc images

6. **Setlist buckets and Quickplay launch controls**

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
