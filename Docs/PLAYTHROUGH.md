# Shade and Seek — recording guide (target 1:45–1:55 from launch, always under 2:00)

Record one continuous take with OBS (Game Capture) **with sound**, playing yourself with keyboard + mouse.
Launch the game window with:

```
Dist\Packaged\Windows\shadeNseek.exe -windowed -ResX=1920 -ResY=1080
```
(the packaged build from `Scripts/package_game.ps1`; from source use
`"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "<path>\shadeNseek.uproject" -game -windowed -ResX=1920 -ResY=1080`)

Run it once before recording so shaders/distance fields are cached (the first launch after a rebuild is slower).
Close other GPU-heavy apps (another Unreal instance especially): the scene uses Lumen + Nanite on a 6 GB GPU. The game
holds roughly 90-120 fps at 1080p on a laptop RTX 4050 by rendering at a dynamic resolution that TSR upscales; add
`-epic` to the command line for the old all-Epic settings (much slower).
To watch the intended route first, run the same command with `-autoplay` (title to end card in ~1:50).

Shade walks at an unhurried cat pace; hold **Shift** to trot on the long straights if you want to tighten the take.
Watch the **SHADOW LEFT** tile (bottom-left): it drains with time and quickly in sunlight and never refills - if it
empties, the walk restarts from the passage. A clean take ends with about two thirds left; each burn costs about a sixth.

| Time | Beat | What to do |
|---|---|---|
| 0:00–0:05 | Title over the rooftops, ambient score | **E** / Enter on "Begin the walk home" |
| 0:05–0:08 | Dip to dusk-black, open on Shade under the vaulted passage, narration | — |
| 0:08–0:14 | Via del Gatto, cool side of the lane | Walk east (W) |
| 0:14–0:18 | **Sun is lethal** | Toe into the alley sunbeam (~x 900): sizzle, smoke, "TOO BRIGHT". Step back |
| 0:18–0:33 | **Puzzle 1 – lemon cart** | Behind the cart, **E** to grab, hold **W** to shove it into the sunbeam; **E** to let go |
| 0:33–0:46 | **Puzzle 2 – café awning** | At the corner cord, **hold E**: the canvas rolls out over the piazza. Cross along the café side |
| 0:46–0:57 | **Puzzle 3 – washing day** | At the foot of the stairs **hold E** on the pulley: three sheets run out over the sunny gap. Gusts lift them every ~4 s (they billow just before) – go the moment they drop (trot to be safe) |
| 0:57–1:00 | **Puzzle 4 – the shutter** | Tug the cord (**E**) and **trot (Shift + W) straight away**: the shutter's shade lasts ~1.5 s |
| 1:00–1:11 | **Puzzle 5 – two shadows** | At Casa Azzurra's corner **hold E** to roll out the blue awning (its shade covers the wall), hop onto the wall (**Space**), nudge the lemon tree (**E**) into the last sunny gap |
| 1:11–1:20 | **Finale** | Bell tolls, the sun sinks and swings west: wait in the tower's shade while its shadow sweeps onto Biscotto |
| 1:20–1:34 | **Reunion** | Walk to Biscotto inside the moving shadow, **E**: Shade pours into his shadow, he wakes and sits up, purrs |
| 1:34–1:50 | End card | "Reunited", 5 / 5 – stop recording |

Mechanics shown: movement / camera / jump / trot, lethal sunlight + exposure feedback (and evaporate/re-form if you
linger), pushing an object to cast shade, hold interactions that unroll shade, shade that comes and goes on a rhythm
(gusts), timed shade (the shutter), combining two shade sources in the right order, knocking an object off a ledge, a
dynamic moving shadow (the sun animates), the goal and the end state.
