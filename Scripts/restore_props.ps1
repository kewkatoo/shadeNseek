# Re-imports the raw glTF of every Poly Haven prop that has no merged SM_ mesh yet, merges just those, then removes
# the raw Interchange imports (Props/*/Mesh), which nothing references once the merged SM_ exists.
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $root
try {
    $env:SNS_ONLY = "models"
    .\Scripts\run_ue_python.ps1 -Script Scripts\unreal\sns_import.py | Select-String "SNS_IMPORT (model|models)|Traceback" | Select-Object -Last 40
    Remove-Item Env:\SNS_ONLY
    $env:SNS_MISSING_ONLY = "1"
    .\Scripts\run_ue_python.ps1 -Script Scripts\unreal\sns_props.py | Select-String "SNS_GEO (prop|ERROR|FAILED|no pieces)|ALL DONE|Traceback" | Select-Object -Last 60
    Remove-Item Env:\SNS_MISSING_ONLY
    Get-ChildItem Content\Shade\Props -Directory | ForEach-Object {
        $mesh = Join-Path $_.FullName "Mesh"
        $merged = Get-ChildItem $_.FullName -Filter "SM_*.uasset" -ErrorAction SilentlyContinue
        if ((Test-Path $mesh) -and $merged) { Remove-Item $mesh -Recurse -Force; "removed raw import $($_.Name)/Mesh" }
    }
} finally { Pop-Location }
