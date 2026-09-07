# GH1 / GH2 / GH80s Character Variant Merge

## Runtime contract

GH2 remains the primary game. Character selection presents one canonical
character entry and an ordered list of that character's GH2, GH1, and GH80s
variants, so GH2 supplies the default whenever it exists. Selecting a variant
carries its exact model, animation set,
and unique highway texture into gameplay; it does not replace GH2 gameplay,
HUD, title-card, or menu presentation.

The live catalog is layered. Runtime loads the packed built-in
`config/gen/character_variants.dtb` first, then merges self-contained packages
from `DLC/*/manifest.json` or `GHOGX_ADDONS_DIR`. The merged
`character_provider` owns roster length, order, labels, variant selection, and
portrait paths for the character-select UI. Each package's `content/` tree is
mounted process-wide at the same virtual paths an ARK would expose, so existing
character, venue, song, texture, and audio loaders do not need a second asset
path system.

The stock fixed rack is not used as the roster. Its source card geometry,
materials, camera, and background remain authoritative, while runtime creates
a cyclic film reel from the provider: five visible cards for single-player and
independent three-card columns for Player 1 and Player 2. The selected entry is
fixed in the center while values wrap and scroll through it. Runtime-created
cards reset the copied serialized-world cache to local before parent
composition; retaining a source card's old world cache collapses every dynamic
row onto one position.

The costume selector has a fixed two-value viewport. For a character with more
than two variants, the selected value is the top row and the next chronological
value is the bottom row. Down advances the values while the panel background
stays fixed; Up reverses them. Both directions wrap at the ends. Characters
with one or two variants retain the same cyclic order.

## Source facts and identity

`tools/build_character_variant_overlay.py` derives the catalog rather than
maintaining a character-name table:

1. Read each source archive's authored `CHARACTERS` and `LOAD_CHARACTERS`
   macros.
2. Resolve each selection's authored localized display name.
3. Join equal authored display names into a canonical identity.
4. Preserve each source game's authored variant order inside the project order
   GH2, GH1, GH80s.
5. Use the authored outfit label where one exists, then apply the
   project-authored selection labels in
   `config/character_variant_labels.tsv`. The manifest is keyed by canonical
   character, exact selection, and source game; generation fails if any key
   no longer matches the source-derived catalog.
6. Resolve model, UI model, UI/main/strum/fret animation, and highway paths
   from the source archive inventories.

The source archives produce 11 canonical characters and 33 selectable source
variants: 8 from GH1, 19 from GH2, and 6 from GH80s. The built-in
`DLC/core.singers` package adds two identities and four outfits, for a deployed
total of 13 characters and 37 selectable variants after campaign completion.

## Project playable characters

`config/playable_character_variants.tsv` remains a build-time extension point
for content that must be packed into the generated catalog. Runtime-owned
project content uses the same public DLC contract as community content instead
of requiring an ARK overlay.

`DLC/core.singers` adds the female and male singers as distinct guitarist
identities. Each lists the clean GH2 singer model first and the converted GH1
model second. Female animation retargeting is driven by Judy Nails (`alterna`);
male animation retargeting is driven by Clive Winston (`classic`). Both declare
the exact authored `bone_pos_mic.mesh` root in `guitarist_hidden_roots`, which
hides the embedded mic stand only for the guitarist role. Guitar attachment,
two-hand IK, vocalist use of the same models, and all normal prop behavior stay
generic. Both identities retain the persistent `won_campaign` requirement.

External downloads use one `manifest.json` per addon folder, not a global
user-maintained catalog. A character addon may declare:

```json
{
  "schema_version": 1,
  "id": "community.example",
  "characters": [{
    "id": "example_character",
    "label": "Example Character",
    "portrait": "ui/image/dlc/example/portrait.bmp_ps2",
    "preferred_guitar": "guitar_sg",
    "preferred_guitar_finish": "guitar_sg_default",
    "preferred_guitar_paint_primary": 0,
    "preferred_guitar_paint_secondary": 1,
    "outfits": [{
      "selection": "example_character_default",
      "label": "Standard",
      "model": "char/example/og/gen/example.milo_ps2",
      "ui_model": "char/example/og/gen/example_ui.milo_ps2",
      "ui_anim": "char/example/anims/gen/example_ui.milo_ps2",
      "main_anim": "char/example/anims/gen/example_main.milo_ps2",
      "strum_anim": "char/example/anims/gen/example_strum.milo_ps2",
      "fret_anim": "char/example/anims/gen/example_fret.milo_ps2"
    }]
  }]
}
```

Top-level `outfits` may extend an existing character by naming `character`.
One schema may also declare guitars, top-level finishes, venues, songs, and
setlists in any combination. Duplicate package/content/selection/catalog IDs
are rejected. A package cannot replace a base-ARK path unless that exact path
appears in its `replaces` array. Package application is transactional: a late
validation error rolls back earlier character, song, guitar, finish, venue,
quickplay, and setlist mutations from that manifest.

Preferred-guitar fields may be inherited from the character or overridden by
an outfit. They are a source-authored fallback only: the menu resolves them
when the player has no saved guitar selection, never over an explicit player
choice. The referenced guitar must exist in the merged catalog; otherwise the
normal first-guitar fallback remains in force.

All manifest asset values are normalized ARK-relative paths. Files live under
`DLC/<package>/content/<same path>`; references may also resolve an unchanged
base-ARK asset. See `DLC/README.md` and
`DLC/examples/everything/manifest.json` for the complete schema and folder
layout.

## Project-authored outfit names

`config/character_variant_labels.tsv` contains the 17 project-authored names
that replace temporary source-game labels or ambiguous labels reused by more
than one game:

1. Johnny Napalm: GH1 `Atomic`; GH80s `80's Punk`.
2. Judy Nails: GH1 `Bad in black`; GH80s `Hairspray`.
3. Izzy Sparks: GH1 `War paint`; GH80s `Steel Panther`.
4. Pandora: GH1 `Latex`; GH80s `Short shorts`.
5. Axel Steel: GH1 `Metal shirt`; GH80s `Jacket`.
6. Clive Winston: GH1 `Strawberry Fields`; GH2 `Kashmir`.
7. Xavier Stone: GH1 `Hip hop`; GH2 `Funk`.
8. Grim Ripper: GH1 `Classic`; GH2 `Time`; GH80s `No time`.

These are build data, not runtime identity branches. The generator validates
the manifest's canonical character, exact selection symbol, and source game
against the newly derived catalog before applying a label. A stale or
misidentified row therefore fails generation instead of silently naming the
wrong model.

## Generated catalog

The build writes:

- `generated/character_variant_overlay/config/gen/character_variants.dta`
- `generated/character_variant_overlay/config/gen/character_variants.dtb`
- `generated/character_variant_overlay/character-variant-overlay.tsv`

Each canonical-character node contains ordered variant nodes with:

- `source`
- `label`
- `model`
- `ui_model`
- `ui_anim`
- `main_anim`
- `strum_anim`
- `fret_anim`
- `highway_surface`
- `portrait`
- `animation_source_model`
- `retarget_animation`
- `guitarist_hidden_roots`
- `unlock`
- `character_label`

The runtime reads this catalog through `ConfigDb` and exposes it through
`character_provider`. UI and gameplay therefore consume the same exact
selection record.

## Collision handling

The generator compares archive paths before applying the overlay. When a GH1
package would replace a distinct stock GH2 asset, the GH2 asset is retained
under a generated namespace and the GH2 catalog row points to that retained
path. GH80s imports are likewise namespaced. This is derived from path/content
collisions and never from character-specific runtime branches.

## Rebuild and apply

From the repository root:

```powershell
python tools/build_character_variant_overlay.py `
  --ghogx engine/out/build/win-amd64-release/ghogx.exe `
  --ark-tool tools/ark/build/Release/ark_tool.exe `
  --dtb-tool tools/dtb/build/Release/dtb_tool.exe `
  --gh1-gen ../GH1/GEN `
  --gh2-gen ../gh2_ps2_hybrid_assets/gen `
  --gh2-clean-hdr ../gh2_ps2_hybrid_assets/gen/main.hdr.pre-overlay.bak `
  --gh2-clean-ark ../gh2_ps2_hybrid_assets/gen/main_0.ark `
  --gh80-gen ../hybrid_gh80s_assets/gen `
  --out-root generated/character_variant_overlay

tools/ark/build/Release/ark_tool.exe overlay `
  ../gh2_ps2_hybrid_assets/gen/main.hdr `
  ../gh2_ps2_hybrid_assets/gen/main_0.ark `
  --root generated/character_variant_overlay `
  --manifest generated/character_variant_overlay/character-variant-overlay.tsv
```

Applying the same manifest again must report every row as reused and append
zero bytes.

## Campaign door presentation

The campaign selector's door is an external PanelDir mesh, not part of a
character model and not a `sel_character.milo_ps2` TransAnim:

1. Stock `career.dtb` enters the screen with
   `{char_single set_door 0 {sel_character_panel find cs_door.mesh}}` and
   clears player zero's binding with `{char_single set_door 0}` on exit.
2. `sel_character.milo_ps2` contains `cs_door.mesh`, but its TransAnim
   inventory contains only `char_highlight.tnm`, `cs_arrow.tnm`,
   `cs_posters.tnm`, and `sel_skin.tnm`.
3. A stock GH2 character UI bank carries the external target as
   `bone_door.rotz`. For `punk1_ui`, `ui_enter` starts at `-0.00854492` and
   opens across the entry clip; `ui_loop` holds the authored open value
   `2.52563`.
4. Converted GH1 Punk has only `punk_idle_ui`, with no `ui_enter`, events, or
   `bone_door` channel. That is a source-format fact, not a reason for a
   Punk-specific exception.

The GH2 PS2 executable supplies the transform bridge that is not represented
by those assets:

1. The `set_door` handler at `SLUS_214.47:0x00143774` calls the wrapper at
   `0x001432C0`.
2. That wrapper calls `0x00142720`, which only replaces the indexed
   `ObjPtr<RndTransformable>` in the 0x30-byte player cache row. It does not
   reparent the panel mesh or apply a CharBones channel to that mesh.
3. `CharsysPanel::Poll` reaches the bound pointer at `0x00142420`, finds
   `bone_door` in the loaded character at `0x0014242C`, and passes that
   character transform's Matrix3 to `MakeEuler` at `0x0014243C`.
4. Poll then overwrites Euler X with `1.5707963` radians and Euler Y with zero
   at `0x00142444..0x0014244C`, while retaining the recovered character Z.
   `MakeRotMatrix` at `0x0014245C` writes that rotation into the external
   panel door's local Matrix3. The panel door's authored translation remains
   unchanged.

The resulting source contract is therefore
`external_door_rotation = MakeRotMatrix(pi/2, 0, bone_door_z)`. With
Harmonix's row-vector `Ry * Rx * Rz` order, the exact rows are
`[cos(z), sin(z), 0]`, `[0, 0, 1]`, and `[sin(z), -cos(z), 0]`.
The mandatory X quarter-turn is why directly applying `bone_door.rotz` to
`cs_door.mesh` produced the rejected edge-on gray slab.

`CharsysPanel::set_door` now retains the indexed external mesh binding and the
runtime reproduces that recovered Poll bridge. If the selected source has no
door channel, the runtime finds an authored catalog `ui_loop` that does
publish `bone_door` and uses only that loop's open-door Z pose through the
same bridge. No character name, model offset, mesh hiding, or guessed door
angle is used.

The bounded `[menu-char-door]` ledger records outfit, target, selected-driver
versus authored-open mode, clip, recovered source address, the exact Euler row,
and all twelve local-transform values whenever the active door clip changes.
Current visual and numeric proof is in:

- `proofs/campaign-character-select-door-corrected/`
- `proofs/campaign-character-select-door-gh1-fallback/`

## Verification

`ghogx_character_variant_catalog_test` loads the deployed archive and checks:

1. Canonical and variant counts, including project-manifest rows.
2. GH1/GH2/GH80s chronological order.
3. Unique selection symbols.
4. Provider order and labels.
5. Forward and reverse wrap.
6. The two-value viewport contract.
7. Existence and decoding of every exact UI model and source-authored UI
   animation route.
8. Direct `bone_door` coverage and availability of the authored-open fallback
   for the variants whose selected UI route has no door channel.
9. The recovered `MakeRotMatrix(pi/2, 0, z)` external-door bridge, including
   the mandatory X quarter-turn and preservation of character Z.
10. Deployed DTB readback matches all 17 project-authored labels exactly.
11. The converted female singer is hidden before `won_campaign`, visible after
    it, and resolves the declared model and animation owners exactly.

Current result:

```text
PASS character catalog characters=12 variants=34 gh1=9 gh2=19 gh80=6 order=chronological wrap=both viewport=2 door_bridge=euler_pi_over_2_0_z door_direct=25 door_open_pose_fallback=9
```

The 2026-07-30 naming deployment replaced only
`config/gen/character_variants.dtb` (`reused=64 replaced=1 added=0
appended=21147`). Reapplying the same manifest is idempotent
(`reused=65 replaced=0 added=0 appended=0`). Deployed readback and UI proof
are in `proofs/campaign-character-select-label-catalog/`.
