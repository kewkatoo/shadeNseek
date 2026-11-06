# Shade and Seek — recording guide (target 1:30–1:50 from launch)

Record one continuous take with OBS (Game Capture) **with sound**, playing yourself with keyboard + mouse.
Launch the game window with:

```
Dist\Packaged\Windows\shadeNseek.exe -windowed -ResX=1920 -ResY=1080
```
(the packaged build from `Scripts/package_game.ps1`; from source use
`"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "<path>\shadeNseek.uproject" -game -windowed -ResX=1920 -ResY=1080`)

Run it once before recording so shaders/distance fields are cached (the first launch after a rebuild is slower).
Close other GPU-heavy apps (another Unreal instance especially): the scene uses Lumen + Nanite on a 6 GB GPU.
To watch the intended route first, run the same command with `-autoplay` (it finishes the game in ~1:40).

Shade walks at an unhurried cat pace; hold **Shift** to trot on the long straights if you want to tighten the take.
Watch the **SHADOW LEFT** tile (bottom-left): it drains with time and quickly in sunlight and never refills - if it
empties, the walk restarts from the passage. A clean take ends with about two thirds left.

| Time | Beat | What to do |
|---|---|---|
| 0:00–0:08 | Title over the rooftops against the golden sky, ambient score | Let it breathe ~5 s, **E** / Enter on "Begin the walk home" |
| 0:08–0:12 | The rooftop shot dips to dusk-black and opens on Shade under the vaulted passage, narration | — |
| 0:12–0:22 | Via del Gatto, cool side of the lane | Walk east (W). Pan up to the sunlit upper walls, festoon lights, laundry |
| 0:22–0:26 | **Sun is lethal** | Toe briefly into the alley sunbeam (~x 900): sizzle, smoke, heat haze, "TOO BRIGHT" ring. Step back |
| 0:26–0:40 | **Puzzle 1 – lemon cart** | Behind the cart, **E** to grab, hold **W** to shove it into the sunbeam; **E** to let go. Walk past on the cart side |
| 0:40–0:50 | **Puzzle 2 – café awning** | At the corner cord, **hold E**: the striped canvas rolls out and its shadow sweeps the piazza. Cross along the café side |
| 0:50–1:00 | Steps up between tall houses | Climb (W); watch the paws find each step |
| 1:00–1:08 | **Puzzle 3 – flowerpot** | Hop (**Space** – Shade crouches, then springs) onto the low wall, nudge the lemon tree (**E**): it tumbles into the sunny gap and its leafy shadow bridges it |
| 1:08–1:22 | **Finale** | Bell rings, the sun sinks and swings west: stay in the tower's shade while its shadow sweeps onto Biscotto (asleep on the bench, no shadow) |
| 1:22–1:45 | **Reunion** | Walk to Biscotto inside the moving shadow, **E**: Shade pours into his shadow, he wakes with a yawn, sits up, purrs; fade to the wide shot and the **Reunited** card |

Mechanics shown: movement / camera / jump / trot, lethal sunlight + exposure feedback (and evaporate/re-form if you
linger), pushing an object to cast shade, a hold interaction that unrolls shade, knocking an object off a ledge, a
dynamic moving shadow (the sun animates), the goal and the end state.
