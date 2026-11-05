param([switch]$Gallery)
# Rebuilds the cat end to end: SDF sculpt -> Blender (ears, eyes, whiskers, rig, weights, fur shells) -> UE import
# -> materials. Optional: compile and run the -catgallery QA shots (Saved/Shots/cat_gallery*.png).
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $root
try {
    python -I Scripts\blender\cat_sdf.py . | Select-Object -Last 1
    & "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Scripts\blender\make_cat.py -- . 2>&1 |
        Select-String "^CAT|Error|Traceback" | Select-Object -Last 12
    $env:SNS_ONLY = "cat"
    .\Scripts\run_ue_python.ps1 -Script Scripts\unreal\sns_import.py | Select-String "SNS_CAT|Traceback" | Select-Object -Last 6
    Remove-Item Env:\SNS_ONLY
    .\Scripts\run_ue_python.ps1 -Script Scripts\unreal\sns_materials.py -WithRHI | Select-String "ALL DONE|link failed|Traceback|ERROR" | Select-Object -Last 6
    if ($Gallery) {
        .\Scripts\build.ps1 | Select-Object -Last 2
        .\Scripts\run_game.ps1 -Extra "-catgallery" -TimeoutSec 600 | Select-String "EXIT|Error" | Select-Object -Last 4
    }
} finally { Pop-Location }
