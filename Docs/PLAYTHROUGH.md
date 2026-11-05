# Shade and Seek — recording guide (target 1:10–1:30 from launch)

Record one continuous take with OBS (Game Capture) **with sound**, playing yourself with keyboard + mouse.
Launch the game window with:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "<path>\shadeNseek.uproject" -game -windowed -ResX=1920 -ResY=1080
```

Run it once before recording so shaders/distance fields are cached (the first launch after a rebuild is slower).
Close other GPU-heavy apps (another Unreal instance especially): the scene uses Lumen + Nanite on a 6 GB GPU.
To watch the intended route first, run the same command with `-autoplay`.

| Time | Beat | What to do |
|---|---|---|
| 0:00–0:06 | Title over the village from the sea, music | Wait ~4 s, press **E** |
| 0:06–0:10 | Camera swoops down to Shade under the vaulted passage | — |
| 0:10–0:18 | Via del Gatto, cool side of the lane | Walk east (W). Look up at the sunlit upper walls and festoon lights |
| 0:18–0:21 | **Sun is lethal** | Step briefly into the alley sunbeam (~x 900): sizzle, smoke, "TOO BRIGHT" ring. Step back |
| 0:21–0:32 | **Puzzle 1 – lemon cart** | Go behind the cart, **E** to grab, hold **W** to push it into the sunbeam until it stops; **E** to let go. Walk past it on the cart side (stay close to the cart) |
| 0:32–0:42 | **Puzzle 2 – café awning** | At the end of the lane, stand by the cord on the corner, **hold E**: the striped canvas rolls out over the piazza and its shadow sweeps across. Cross under it along the café side |
| 0:42–0:50 | Steps up between tall houses | Climb (W). Optional **Shift** to trot |
| 0:50–0:58 | **Puzzle 3 – flowerpot** | At the top, hop (**Space**) onto the low stone wall on the right, nudge the potted lemon tree (**E**) — it tumbles into the sunny gap and its leafy shadow bridges it. Hop down, cross in the leaf shade to the bell tower |
| 0:58–1:10 | **Finale** | The bell rings, the sun sinks and swings west: stay in the tower's shade and watch its shadow sweep across the terrace onto Biscotto (asleep on the bench, no shadow) |
| 1:10–1:25 | **Reunion** | Walk to Biscotto inside the moving shadow, **E**: Shade pours into his shadow, he wakes, yawns, sits up and purrs; fade to the wide sunset shot and the **Reunited** card |

Mechanics shown: movement / camera / jump / trot, lethal sunlight + exposure feedback (and evaporate/re-form if you
linger), pushing an object to cast shade, a hold interaction that unrolls shade, knocking an object off a ledge, a
dynamic moving shadow (the sun animates), the goal and the end state.
