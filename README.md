# Shade and Seek

*Environmental puzzle adventure · Unreal Engine 5.8 · C++ gameplay · Lumen + Nanite · no Fab / Marketplace content*

Biscotto the ginger cat dozed off in the noon sun on the cliffs of **Vellara**, a little Mediterranean village stacked up
the hillside above the sea, and his shadow slipped away. You are **Shade**, that living cat-shadow. Direct sunlight is
lethal, so you can only move through shade - and you make new shade by moving things around: push a lemon cart into a
sunbeam, tug a café awning across the piazza, knock a potted lemon tree off a wall (as cats do). At six o'clock the
church bell rings, the sun sinks, and the bell tower's shadow sweeps across the terrace to where Biscotto lies sleeping,
shadowless, so you can slip back into place.

## Run
- Open `shadeNseek.uproject` (it builds the `shadeNseek` module on first open) and press Play. Map: `/Game/Shade/Maps/L_Vellara`.
- **Self-playing demo:** `UnrealEditor.exe shadeNseek.uproject -game -autoplay`
  - `-autoplayexit` quits when the run ends (unattended capture), `-autoplayshots` saves a screenshot every 2.5 s to `Saved/Shots`
  - QA: `-sunmap` writes `Saved/SunMap.png` (+ `_finale`) - a top-down map of gameplay shade; `-catgallery` screenshots the cat rig poses
- `Scripts/run_game.ps1` launches a standalone game window with any of those flags. Recording guide: `Docs/PLAYTHROUGH.md`.

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
builds while lit (0.42 s at full exposure) and Shade evaporates, re-forming at the last spot that was comfortably in
shade. Shade itself never blocks sunlight.

## Code map (`Source/shadeNseek`)
| File | Role |
|---|---|
| `ShadeCharacter` | Shade: movement, camera, sun exposure / evaporation / respawn, interaction focus, cart pushing, ink-wisp particles |
| `ShadeCatRig` | Procedural quadruped animation on a `UPoseableMeshComponent` (walk/trot gait, air, push, sit, sleep, look-at, ears, tail) |
| `ShadeSunSubsystem` | Sun queries + `-sunmap` QA image |
| `ShadeInteractables` | Lemon cart (push), café awning (hold to tug), flower pot (nudge off a ledge), nap cats / Biscotto, zones, sound scatter |
| `ShadeGameMode` | Run flow (title, intro, play, bell-tower finale with the moving sun, reunion cinematic, end), objectives, narration, music |
| `ShadeHUD` | Custom-painted Slate UI: title, ceramic-tile objective card, world prompts with hold rings, exposure warning, subtitles, menus |
| `ShadePlayerController` | Enhanced Input built in code, pause menu |
| `ShadeAutoplayComponent` | `-autoplay` route driver using the same Shade API as the player's input |

## Content pipeline (`Scripts/`)
Everything in `Content/Shade` is rebuilt from `asset-library/` by scripts:
1. `python -I Scripts/fetch_polyhaven.py .` - CC0 Poly Haven textures/models
2. `python -I Scripts/gen_audio.py asset-library/Audio/generated` - all music, ambience and SFX (synthesised)
3. `python -I Scripts/gen_textures.py .` - awning stripes, majolica tiles, bougainvillea cards, signs, fabrics…
4. `blender -b --factory-startup --python Scripts/blender/make_cat.py -- .` - the cat mesh + skeleton (FBX)
5. inside UE (`Scripts/run_ue_python.ps1 -Script <file>`): `unreal/sns_import.py` (runs `sns_cat_reimport.py` for the cat) →
   `sns_fix_masks.py` → `sns_materials.py` (add `-WithRHI` to see shader compile errors) → `sns_props.py` → `sns_fix_trees.py` →
   `sns_meshes.py` → `sns_level.py` (houses are generated with GeometryScript, windows cut with booleans).
   After `sns_props.py` the raw Interchange imports in `Content/Shade/Props/*/Mesh` are unreferenced and can be deleted.
6. `python -I Scripts/make_credits.py .` - `asset-library/CREDITS.md` + `manifest.json`

Third-party assets: Poly Haven (CC0) models/textures and Google Fonts (OFL). See `asset-library/CREDITS.md`.
