# Shade and Seek

*Environmental puzzle adventure · Unreal Engine 5.8 · C++ gameplay · Lumen + Nanite · no Fab / Marketplace content*

Biscotto the ginger cat dozed off in the noon sun on the cliffs of **Vellara**, a little Mediterranean village stacked up
the hillside above the sea, and his shadow slipped away. You are **Shade**, that living cat-shadow. Direct sunlight is
lethal, so you can only move through shade - and you make new shade by moving things around: push a lemon cart into a
sunbeam, tug a café awning across the piazza, run the washing out over a sunny gap and cross between the sea gusts that
lift it, beat a creaking shutter before the breeze swings it shut, and finally combine two shadows - Casa Azzurra's
awning to reach the garden wall, then a potted lemon tree knocked off it (as cats do). At six o'clock the
church bell rings, the sun sinks, and the bell tower's shadow sweeps across the terrace to where Biscotto lies sleeping,
shadowless, so you can slip back into place.

## Run
- Open `shadeNseek.uproject` (it builds the `shadeNseek` module on first open) and press Play. Map: `/Game/Shade/Maps/L_Vellara`.
- **Self-playing demo:** `UnrealEditor.exe shadeNseek.uproject -game -autoplay`
  - `-autoplayexit` quits when the run ends (unattended capture), `-autoplayshots` saves a screenshot every 2.5 s to `Saved/Shots`
  - QA: `-sunmap` writes `Saved/SunMap.png` (+ `_finale`) - a top-down map of gameplay shade; `-catgallery` screenshots the cat rig poses;
    `-apfrom=<AP tag>` starts the autoplay route at a puzzle (e.g. `AP_ShutterCord`); `-pauseshot=<s>` screenshots the pause
    menu; `-audiocap` records the final master mix to `Saved/AudioCap/`; `-camshots` screenshots every QA camera
    (sleep, sit, walk, trot, stand); `-camshots` screenshots every `CamCandidate_*` camera placed by `sns_lighting.py`
  - Motion QA: `-motioncap=follow|side|title -benchmark -fps=30` drives a scripted walk / veer / stop / about-turn / trot /
    hop (or the title-to-game transition), saves every frame to `Saved/MotionCap/<mode>/` with bone + camera tracks, and
    `python -I Scripts/qa_motion.py . <mode>` turns them into numbers (tail spin, joint snaps, paw slip, camera pops;
    `--flicker a:b` maps shimmer, `--sheet a:b:step` makes a contact sheet). More modes: `jumps` (hops in place, walking,
    trotting, camera swung at a wall; the CSV logs body visibility, bad bones and camera line of sight), `tour` (a slow
    pan at every route waypoint - z-fighting and camera pumping only show while the view moves) and `still`
    (`-capend=<s> -capdrift=<deg> -capcmds="2=r.Foo 0;4=r.Foo 1"` A/B render features inside one capture)
  - Performance: `Scripts/perf_run.ps1` plays the packaged build over the autoplay route at 1920x1080 with the CSV
    profiler on and `Scripts/perf_report.py` prints fps, game / render / GPU thread times, slow stretches, hitches
    and the GPU passes by cost (`-NoCsv` uses only the game's own `-perflog` frame timer, which also works in
    Shipping). The render budget (`AShadeGameMode::ApplyRenderBudget`: High scalability, Lumen with the
    irradiance-volume final gather, two shadow cascades, no hardware occlusion queries, dynamic resolution that TSR
    upscales - tuned for 90-120 fps on a laptop RTX 4050) is applied by code at startup; `-epic` skips it.
    `unreal/sns_tri_audit.py` lists placed meshes by classic (non-Nanite) triangles
- `Scripts/run_game.ps1` launches a standalone game window with any of those flags. Recording guide: `Docs/PLAYTHROUGH.md`.
- **Packaged build:** `Scripts/package_game.ps1` cooks a standalone Windows game into `Dist/Packaged/Windows/`
  (run `shadeNseek.exe`, same flags work). `Scripts/make_submission.ps1` builds the source + asset-library zips in `Dist/`.

## Controls
| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move / look | WASD or arrows / mouse | left / right stick |
| Jump | Space | A |
| Trot (sprint) | Shift | L3 |
| Use: push / let go, tug (hold), nudge, wake | E or left click | X |
| Pause | Esc / P | Start |

## How the shade works
`UShadeSunSubsystem` traces from three points on Shade's body toward the sun (the atmosphere directional light) on a
custom `Sunlight` trace channel. Anything that blocks the channel - houses, the cart, the awning canvas, the pot and its
canopy, the bell tower - casts gameplay shade that matches the rendered shadow, including shadows that move. Exposure
builds while lit (0.36 s at full exposure) and Shade evaporates, re-forming at the last spot that was comfortably in
shade. Shade itself never blocks sunlight.

On top of that short burn, Shade has a **shadow meter** (bottom-left tile) that never refills: it drains slowly with
time, fast in sunlight and by a chunk on every evaporation. When it runs out Shade is gone, the screen fades and the
walk restarts from the passage (`?retry`, no title). A clean run finishes with roughly two thirds left; every
burn costs a sixth of it, so the timed puzzles can be retried a few times but not forever.

Puzzles, in order (each harder than the last): **lemon cart** (push), **café awning** (hold), **washing day** (moving
shade: hold the pulley, then cross in a lull - gusts every 4.3 s lift the sheets for 1.8 s, a billow is the tell),
**the shutter** (timed shade: tug, then trot through before it shuts ~2 s later; walking is too slow), **two shadows**
(the wall is in sun until Casa Azzurra's awning is out; only from the wall can the pot be nudged into the last gap).
The autoplay route finishes in ~1:40 (title to end card about 1:50).

## Code map (`Source/shadeNseek`)
| File | Role |
|---|---|
| `ShadeCharacter` | Shade: movement, camera, sun exposure / evaporation / respawn, interaction focus, cart pushing, ink-wisp particles |
| `ShadeCatRig` | Procedural quadruped animation on a `UPoseableMeshComponent`: paws planted in the world and traced onto the ground (stairs too), digitigrade leg IK with shoulder blades, walk / trot gaits from ground speed, idle re-stepping, a lowered carriage and rolling shoulder blades on the move, spine bending into turns and slopes, jump crouch / air pose / landing squash, a tail of independent damped angular springs (lags and swings, never resonates), sprung ears, blinking, look-at; sit and sleep are blended in the same pose space |
| `ShadeSpringArm` | Follow-camera boom pivoting on the capsule centre: pulls in fast and eases out slowly on collisions, holds a pull-in briefly (no pumping at edges), ignores start-penetrating hits and keeps a minimum distance (the camera never ends up inside Shade); small props never block the camera |
| `ShadeSunSubsystem` | Sun queries + `-sunmap` QA image |
| `ShadeInteractables` | Lemon cart (push), café awning (hold to tug), washing line (hold; deterministic sea gusts lift the sheets), shutter (tug; opens, holds, creaks shut), flower pot (nudge off a ledge, only from the wall), nap cats / Biscotto, zones, sound scatter |
| `ShadeGameMode` | Run flow (title, intro, play, bell-tower finale with the moving sun, reunion cinematic, end), objectives, narration, music |
| `ShadeHUD` | Custom-painted Slate UI: cinematic title (Cormorant Garamond wordmark, text menu), ceramic-tile objective card, button-circle world prompts with hold rings, heat-haze exposure warning, storybook subtitles, pause and end cards |
| `ShadePlayerController` | Enhanced Input built in code, pause menu |
| `ShadeAutoplayComponent` | `-autoplay` route driver using the same Shade API as the player's input |

## Content pipeline (`Scripts/`)
Everything in `Content/Shade` is rebuilt from `asset-library/` by scripts:
1. `python -I Scripts/fetch_polyhaven.py .` - CC0 Poly Haven textures/models
   (also the CC0 sky HDRI `evening_road_01_puresky`)
   `python -I Scripts/fetch_ambientcg.py .` - CC0 ambientCG texture sets (cobbles, flags, travertine, lime plaster, roof tiles,
   painted wood, fabrics, ivy) in the same `_diff/_nor_dx/_arm` layout
2. `python -I Scripts/fetch_fonts.py .` - Fraunces + Nunito (the UI) and Cormorant Garamond (OFL), static weights baked from the
   variable fonts; stages the UI fonts
3. `python -I Scripts/prep_audio.py` - cuts, layers, loops and loudness-normalises the CC0 recordings in `asset-library/Audio/`
   (Freesound HQ previews, FreePD music, Kenney) into `asset-library/Audio/game/*.wav` (48 kHz, per-category LUFS, -1 dBTP);
   `python -I Scripts/qa_audio.py` checks every file for clipping, hiss/hum, loudness, loop seams and clicks. In game a
   master-bus limiter catches stacked peaks. (`gen_audio.py` made the old synthesised set in `Audio/generated`, no longer used.)
4. `python -I Scripts/gen_textures.py .` - awning stripes, majolica tiles, bougainvillea cards, signs, fabrics, fur strands…
5. The cat: `python -I Scripts/blender/cat_sdf.py .` (SDF sculpt + marching cubes) then
   `blender -b --factory-startup --python Scripts/blender/make_cat.py -- .` (ears, eyes, whiskers, tabby coat, skeleton,
   heat-diffused weights, fur shells -> FBX). `Scripts/rebuild_cat.ps1 -Gallery` runs the whole cat chain incl. UE import.
6. inside UE (`Scripts/run_ue_python.ps1 -Script <file>`): `unreal/sns_import.py` (runs `sns_cat_reimport.py` for the cat) →
   `sns_fix_masks.py` → `sns_fix_flowers.py` (alpha-tested foliage materials for props whose masks arrived later) →
   `sns_materials.py` (add `-WithRHI` to see shader compile errors) → `sns_props.py` → `sns_fix_trees.py` →
   `sns_nanite_props.py` (Nanite on every dense prop - the create option alone does not stick in 5.8) →
   `sns_meshes.py` → `sns_level.py` (houses are generated with GeometryScript, windows cut with booleans; the start
   passage's bridge house comes from `sns_bridge.py`, which can also regenerate just that mesh; it ends by running
   `sns_lighting.py`: HDRI sky dome, sun, bounce fill, sky light, fog, post, cinematic cameras - rerun it alone to iterate).
   After `sns_props.py` the raw Interchange imports in `Content/Shade/Props/*/Mesh` are unreferenced and can be deleted.
7. `python -I Scripts/make_credits.py .` - `asset-library/CREDITS.md` + `manifest.json`

Third-party assets: Poly Haven and ambientCG (CC0) models/textures/HDRI, Freesound / FreePD / Kenney (CC0) sounds and music,
and Google Fonts (OFL). Every file and its licence is listed in `asset-library/CREDITS.md` and `manifest.json`.
