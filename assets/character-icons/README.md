# DLC selector portraits

Authored 2026-09-09 with the built-in image-generation tool. The PNGs are the
full-resolution source art for Midori, Duke of Gravity, Penelope McQueen,
Female Singer and Male Singer. Both singer portraits depict their GH2 models.
One portrait identifies a character across its outfits.

## Style references and prompts

The style reference was decoded from `ui/gen/sel_character.milo_ps2` in the
local GH2 asset ARK: `char_goth.tex`, `char_alterna.tex`, `char_classic.tex`,
`char_metal.tex`, `char_glam.tex`, and `char_funk1.tex`. Their alpha channels
were displayed with the menu's dark material tint at the actual 3:4 quad aspect.
Identity references were native game captures of the installed models, including
PS2 Midori's accepted base outfit and Penelope's source-model head close-up.

Shared generation specification: style-transfer; one 3:4 head portrait;
match stock GH2 black stencil/ink shapes, chunky irregular contours, angular
simple facial features, white negative space and tiny-thumbnail legibility.
Tightly crop head and a hint of shoulders. Pure black ink on white, no color,
gray shading, fine hatching, engraving, realistic/3D rendering, text, guitar,
hands, frame or drop shadow. Packaging supplies the original card treatment.

Character-specific prompt specifications:

| Source | Identity and composition |
| --- | --- |
| `midori.png` | PS2 GH3 Midori; straight blunt bangs, two high spiky ponytails with round ties, slightly tilted three-quarter face, confident mischievous smile, collar bow. Match Pandora/Judy simplification. |
| `duke.png` | Round thick goggles, narrow angular male face, pointed mutton-chop sideburns, tall chaotic mohawk and swept side hair, high buckled collar; three-quarter left, reserved expression. |
| `penelope.png` | Soft newsboy cap and short visor, pale blunt bangs completely covering both eyes, long straight pale side locks, serious closed lips, high jacket collar, small bow and necklace; three-quarter right. |
| `female-singer.png` | GH2 blonde singer, chunky curly/dreadlike locks, headband, oval face, hoop earrings and jacket; three-quarter left, confident closed lips. Pale locks use white spaces and sparse black contours. |
| `male-singer.png` | GH2 male singer, medium shaggy layered hair, uneven center part, long angular clean-shaven face, strong chin, cheeky grin and raglan crewneck; three-quarter left. Match Clive/Axel simplification. |

## Native packaging

Use `ark_tool extract` to extract `ui/gen/sel_character.milo_ps2`,
`milo_tool extract-entry` for `char_goth.tex`, and `tex_tool decode` to obtain
`char_goth.bmp`. Keep these rebuildable references in a temporary directory.

Run from the repository root, substituting the desired source and destination:

```powershell
python -B tools/encode_ps2_bitmap.py assets/character-icons/midori.png portrait.bmp_ps2 --content-rect 9,16,55,114 --silhouette-alpha --stock-frame char_goth.bmp
```

The encoder stores the art in the stock 64x128 HMXBitmap dimensions; the
retail 30x40 quad restores its display aspect. White RGB receives the selector's
authored dark tint. The stock frame's exterior alpha is retained, the interior
contains the new ink, and ink opacity matches the stock maximum 204/255.
Each direct-color bitmap is 32,800 bytes. This changes texture payloads and
portrait registration, not menu rendering code.

The outputs are installed in the matching `DLC/*/content/ui/image/dlc/` paths.
Midori gains a `characters[].portrait` registration and indexed file; other
characters replace their existing portrait payloads. Update each content index's
size and SHA-256. Preserve each installation's model/animation files and manifest
version: the local accepted Midori installation is newer than its repository
package, and adding a portrait must not downgrade it.

## Verification

All five characters were individually inspected in native headless captures of
`sel_character_new_screen` and `multi_sel_character_screen`, 90 fixed steps at
1/60 second, screenshot at frame 85. The latter includes stock Clive alongside
each new portrait. `GHOGX_MENU_SEED_WON_CAMPAIGN=1` exposes the singers for proof.
These captures assess icons; the 2P screen's existing LOADING labels are not a
load-time measurement. No host keyboard/mouse/controller input was used.

See [the proof set](../../docs/proofs/dlc-character-icons/README.md).
The user accepted all five portraits on 2026-09-09: "Absolutely perfect. Commit and push".
