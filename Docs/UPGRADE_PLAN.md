# Upgrade plan (mentor feedback: longer / harder, better assets, UI, audio)

Part A (no editor) = this plan + downloaded, licence-logged source files. Part B (after "EDITOR FREE") applies it one step
at a time: compile / editor script / one test / commit. No refactors; 30-minute limit per problem.

## 1. Length and complexity: three new shade puzzles

Sun: 30 deg high over the sea (+Y), so shade is cast toward -Y and a seaward object at height h above the path shades
the path about 1.73 h inland of itself. Shade walks 110 cm/s, trots 270 cm/s, and evaporates after ~0.42 s in full sun.

### New route (only the stairs and the terrace change)

```
x  -700 .. 1700   passage, Via del Gatto, P1 lemon cart (push)          unchanged
x  1700 .. 2500   piazza, P2 café awning (hold)                         unchanged
x  2500 .. 2660   S4a  (sea-side house, shortened)                      shade
x  2660 .. 2860   NEW GAP over the lower stairs -> P3 "Washing day"     sun, moving shade
x  2860 .. 3000   S4b  (new narrow house)                               shade
x  3000 .. 3150   NEW GAP over the upper stairs -> P4 "The shutter"     sun, timed shade
x  3150 .. 3330   Casa Azzurra (moved east and shortened)               shade
x  3330 .. 3650   terrace now open to the sun -> P5 "Two shadows"       needs Azzurra's awning AND the pot
x  3650 ..        bell tower shade, Biscotto, finale                    unchanged
```

| # | Puzzle | Mechanic it builds on | What the player does | Why it is harder |
|---|---|---|---|---|
| P1 | Lemon cart | push | push the cart into the alley sunbeam | (existing) single static source |
| P2 | Café awning | hold E | roll the canvas out over the piazza | (existing) single static source |
| **P3** | **Washing day** (moving shade) | hold E (like P2) + timing | Hold the pulley cord on S4a's corner: three sheets run out on a line across the gap, 2 m above the steps. Sea gusts every ~4.3 s lift the sheets (a 1.8 s gust, then a 2.5 s lull) and their shadow slides off the steps. Cross in a lull (walking just fits, trotting is safe). | First shade that comes and goes; you must read the rhythm (sheets flap, gust whoosh). |
| **P4** | **The shutter** (timed shade) | tug a cord once | Tug the cord on S4b's corner: a big green shutter on S4b's east wall swings open across the upper gap, holds 0.6 s, then the breeze creaks it shut over 1.6 s. Its shadow covers the steps only while it is open: walking (1.8 s) is too slow, trotting (0.8 s) makes it. Tug again to retry. | Time pressure; teaches trot. A failed try costs meter, not progress. |
| **P5** | **Two shadows** (combine two sources) | tug (P2) + nudge (old pot puzzle) | The terrace is now sunlit. Tug the cord of Casa Azzurra's blue awning (on its east wall, reachable from the shaded stair top): its shade covers x 3330-3500 including the garden wall. Only then can Shade hop the wall and nudge the lemon-tree pot off; the pot's canopy covers x 3500-3650. Both shadows are needed to reach the tower's shade. | Two sources, and the order matters (the wall is in sun until the awning is out). |

The finale (bell rings, the tower's shadow sweeps to Biscotto) is unchanged.

Estimated autoplay timeline: P1 solved ~28 s, P2 ~41 s, P3 ~55 s, P4 ~65 s, P5 ~85-90 s (pot), so about 90 s of play
from first control to the last puzzle, then the ~25 s finale. The recording grows from ~1:50 to ~2:20 (trim the intro
if the video must stay short). The shadow meter drain is re-tuned so a clean run still ends with about two thirds left.

### Code (Part B step 2), no new systems beyond these puzzles
- `AShadeWashingLine` (ShadeInteractables): hold interaction like `AShadeAwning` (Extension 0..1 runs the sheets out
  along the line); each sheet is a pivot + `SM_Sheet_*` mesh that blocks only the Sunlight channel and swings about the
  line by a deterministic gust cycle (calm flutter +/-6 deg, gust up to ~75 deg inland). `IsGusting()` for sound/HUD.
- `AShadeShutter`: tap interaction; hinge yaw springs 0 -> 100 deg (OutBack), holds `HoldOpen` (0.6 s), creaks shut over
  `CloseTime` (1.6 s); re-usable; fires `SolvedEvent` the first time Shade reaches the zone past it (zone event, not on
  tug). Cord + handle reuse `SM_Cord` / `SM_CordHandle`.
- P5 reuses `AShadeAwning` (second instance, blue stripes, shorter depth, faster open) and `AShadeFlowerPot` as is.
- `ShadeAutoplayComponent`: one new step `WaitShade(tag)` (waits until the sun subsystem reports a waypoint shaded for
  N consecutive frames) used before crossing P3 and right after tugging P4 (with sprint on); new waypoints.
- `ShadeGameMode::BuildObjectives`: three new objectives + narration lines; zones `Zone_AfterWash`, `Zone_AfterShutter`.
- Shadow meter drain re-tuned for the longer run.

### Level (Part B step 3): `sns_level.py`
- S4 -> S4a (2500-2660) + S4b (2860-3000); Azzurra 3000-3500 -> 3150-3330; pot wall stays (3435, 130).
- Spawn the washing line (line across 2660-2860 at y 200, ~200 cm above the steps), the shutter (hinge on S4b's east
  face, y 190-320), Azzurra's awning (east wall, x 3330, extending +X 175 cm, width spanning y 120-460), cords, zones,
  objective targets and autoplay waypoints. Re-run `sns_lighting.py`, then `-sunmap` to check every patch of shade
  against these numbers before tuning.

## 3. UI: one style, readable everywhere (Part B step 5, `ShadeHUD.cpp` only)

Audit (all UI is one custom-painted Slate widget, `SShadeOverlay`; no default Slate buttons/borders, no UMG):
- The `UI::` colours are sRGB numbers passed as linear `FLinearColor`, so everything renders washed out (terracotta
  shows as peach; kicker text on cream is ~2:1 contrast, hints ~3.3:1).
- Four families (Lato, Lobster, Kalam, Cormorant) - the objective card alone mixes three; Cormorant's hairlines break up
  over bright sun; sizes 11-13 (15-17 px) are too small; missing fonts silently fall back to Roboto.
- Weak or no text backing (marker distance, controls labels, toasts over sky, the exposure warning sits inside the
  white heat-haze); `Soft()` fakes a blur with 14 stacked boxes; pause/end screens are plain full-screen boxes; two
  different keycap styles; ~8 tints hard-coded outside `UI::`; one-line narration overflows.

Style:
- **Fonts:** Fraunces SemiBold / SemiBold Italic (soft, 72 pt optical size) for display - logo, headings, menu items,
  card titles, toasts; **Nunito** SemiBold / Bold / ExtraBold for everything else. Both OFL, fetched and baked to
  static TTFs by `Scripts/fetch_fonts.py` (done). Lobster, Kalam, Cormorant and Lato are dropped from the UI.
- **Type scale (1080p, scales with height):** logo 128, screen heading 96, menu 34, card title / toast 28, narration 26
  (wraps at 1100 px, max 2 lines), body / hint 18, kicker 14 caps (tracking 120), keycap 15. Nothing below 14.
- **Palette (sRGB hex, converted properly):** Ink #2B2230, Shade #3A2F45, Cream #F6EBD9, Plaster #E6D3B3,
  Terracotta #A84A2A, Sun #F2A541 (on dark only), Sea #2F6F8F, Olive #6E7B3A, InkSoft #5A4E5C.
- **Two card types only** (`FSlateRoundedBoxBrush`): *paper card* (cream, radius 16, plaster hairline) for the
  objective and shadow meter; *shade card* (Shade 82%, radius 14, faint cream hairline) for everything floating over
  the world: prompt, marker distance, narration, toasts, exposure warning, controls strip, pause and end panels. Soft
  drop shadow = two offset rounded boxes instead of the 14-box `Soft()`.
- **One keycap:** cream rounded key (radius 6) with an ink glyph and a plaster lip; gamepad buttons as circles/pills;
  the hold ring is a sun arc around it.
- **Motion:** in 0.22 s (OutCubic, 8 px rise), out 0.16 s; menu rows stagger 60 ms with a sun underline; toasts drop in
  0.25 s, hold 2.8 s, fade 0.4 s; only the low-meter pulse stays animated.
- Every paint function (objective, meter, marker, prompt, exposure, narration, toasts, controls, title, menu, pause,
  end, letterbox) moves to these tokens/helpers; the new puzzles' objectives use the same card.

## 2. Assets on the demo path (Part B step 4)

Downloaded (CC0, logged in `asset-library/manifest_assets_new.json`, 26 entries, ~179 MB): ambientCG via the new
`Scripts/fetch_ambientcg.py` (stored with the same `_diff/_nor_dx/_arm` names as the Poly Haven sets) and Poly Haven
via `Scripts/fetch_polyhaven.py --only` (which also fixes its alpha-mask filter).

**Existing bug found:** the window-box flowers on most sills (`flower_gazania`, `flower_heliophila`, `flower_ursinia`,
`shrub_sorrel_01`) use the opaque `M_Prop` master and their alpha masks were never downloaded, so the petal cards
render as dark rectangles. Masks are now downloaded; fix = import them and re-parent those MIs to `M_Foliage`
(AlphaTex + light wind), Nanite shape preservation on.

| Where | Now | Replacement |
|---|---|---|
| Lane cobbles (x -1000..2650) + alley | mossy northern cobble, tiles 24x | ambientCG PavingStones150 (Italian fan cobbles) |
| Piazza, church terrace flags | brown crazy paving | ambientCG Tiles143 (limestone flags) |
| Stairs, sills, thresholds, bollard, church steps | dark crazy paving on risers | ambientCG Travertine009 (+ a roughness floor in `M_Surface`; the scan is polished) |
| Lemon / peach / ochre walls (first facade, Caffe Rosa) | modern spray stucco | ambientCG PaintedPlaster018 / 017 (lime plaster, peeling) |
| Bedrock / retaining walls in every wide shot | coursed blocks visibly tiling | Poly Haven coral_stone_wall + 6-10 rock_face_01/02 scans along the seaward faces |
| Roof tiles | Marseille tiles | ambientCG RoofingTiles014B (coppi) |
| Shutters, doors, cart body | tinted planks + fake noise chipping | ambientCG PaintedWood009B (green) / 007B (blue) on `M_Surface` |
| Pergola beams, crates, cord handle | plank texture across 14 cm beams | Poly Haven rough_wood |
| Railings, balconies, awning arms | flat procedural iron | Poly Haven green_metal_rust normal/roughness/rust |
| Café awning + laundry | synthetic sine-weave normal, flat stripe | ambientCG Fabric061 (canvas normal), Fabric071 (striped linen) |
| Ivy on facades | painted PIL leaf cards | ambientCG LeafSet017 (real ivy atlas) on `M_Foliage` |
| Window boxes | floor-tile grout lines | ambientCG GlazedTerracotta001 |
| Biscotto's finale bench (+1) | three boxes | Poly Haven painted_wooden_sofa (blue settle, 5.8k tris) |
| Café menu | flat card on the wall | Poly Haven standing_chalkboard_01 |
| New dressing | - | Poly Haven ceramic_pot, painted_wooden_chair_02 at doorsteps, planter_box_01 with lemons by the cart, crystalline_iceplant / fern_02 / weed_plant_02 at wall seams and steps, large_iron_gate closing the terrace's east end |

Also without downloads: the bell gets a bronze tint, mountains get the existing marble_cliff_02 as a detail layer, the
potted tree gets the (built but never attached) `SM_LemonCluster`. No CC0 bougainvillea, geranium, olive, cypress or
agave exists on Poly Haven / ambientCG, so the procedural bougainvillea stays.
Budget: ~+0.4 M triangles (mostly Nanite) and ~+80 MB texture memory net; re-check with `perf_run.ps1` after.

## 4. Audio (Part B step 6)

All 41 synthesised sounds are replaced by real CC0 recordings / produced CC0 sounds, plus a new town bed and five puzzle
sounds. Sources (36, all CC0, verified on each item's page; logged in `asset-library/Audio/manifest_audio.json`):
Freesound (HQ previews of CC0 originals), FreePD (music; site closed in 2025, licence checked on Internet Archive
copies of its pages) and Kenney packs. `Scripts/prep_audio.py` builds `asset-library/Audio/game/*.wav` (47 files,
48 kHz / 16-bit, mono point sources, stereo beds/music): trims, fades, seamless loop crossfades, 40 Hz high-pass,
loudness-normalised per category with a look-ahead limiter at -1 dBTP (music -18 LUFS, beds -24, town -24, puzzle
SFX -20, stingers -18, UI -22, paws -26).

- Music: "Wisdom in the Sun" (Kevin MacLeod, FreePD, warm guitars/bells, 2:34) for the theme; "Champ de tournesol"
  (Komiku, FreePD, acoustic guitar ending, 0:51) for the reunion.
- Beds: Mediterranean waves on cliff rocks + harbour lapping; cicadas in a Ligurian town; NEW town bed (Padua piazza
  after lunch + café terrace cutlery + swifts) placed in the piazza and lane.
- Every SFX recorded: cat trill, purr, soft paw foley, ceramic pot smash + thud, ratchet clicks, cloth flaps, real
  bell toll, glockenspiel/celesta/harp stingers, Kenney UI plucks and journal open/close for the pause menu.
- New for the puzzles: shutter creak, laundry flap loop, rope pulley creak, gate creak, timer tick.

In-game mix: import the WAVs over the same asset names (code keeps working), add the new ones, add a master submix with
a limiter (dynamics processor, -1 dB ceiling) as the project's master submix, set per-category volumes in
`ShadeAssets` now that files are loudness-matched, and shorten the overlapping finale bell tolls.
Not yet listened to: a listening pass on `asset-library/Audio/game` before import is the first thing in this step.

## Part B order (after "EDITOR FREE"); one test and one commit per step
0. Commit the already-approved work (pot, eyes, cord, perf pass, cleanup) as a baseline.
1. Audio: listen, import, master limiter, per-category volumes. Test: one autoplay run, listen to the capture.
2. New puzzle code (washing line, shutter, autoplay `WaitShade`, objectives, meter retune). Test: compile.
3. Level: stairs/terrace layout + new puzzle actors + waypoints; re-run lighting; `-sunmap`. Test: one autoplay run
   reaching `SHADE_COMPLETE` with 0-1 evaporations.
4. Assets: import textures/models, flower alpha fix, material swaps, new dressing. Test: camshots + perf_run.
5. UI: fonts, tokens, cards, keycaps, every paint function. Test: title/HUD/pause/end screenshots at 1080p.
6. Repackage Shipping, update README / BOUNDARIES / CREDITS, rebuild the zips.
