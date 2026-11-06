# Builds the two submission archives in Dist/:
#   shadeNseek.zip     -> shadeNseek/{shadeNseek.uproject, Source/, Content/, Config/, Scripts/, Docs/, README.md, manifest.json}
#   asset-library.zip  -> asset-library/ (raw CC0/OFL sources + generated audio/textures/models + CREDITS.md) + manifest.json
# Build artefacts (Binaries, Intermediate, Saved, DerivedDataCache) are excluded as required.
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$dist = Join-Path $root "Dist"
$stage = Join-Path $dist "stage\shadeNseek"
# Only clear this script's own outputs (Dist/Packaged holds the cooked game from package_game.ps1).
foreach ($old in @("stage", "stage_lib", "shadeNseek.zip", "asset-library.zip")) {
    Remove-Item (Join-Path $dist $old) -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force $stage | Out-Null
python -I (Join-Path $root "Scripts\make_credits.py") $root | Out-Null

foreach ($item in @("shadeNseek.uproject", "README.md", "manifest.json", "Source", "Content", "Config", "Scripts", "Docs")) {
    $src = Join-Path $root $item
    if (Test-Path $src) { Copy-Item $src (Join-Path $stage $item) -Recurse -Force }
}
Get-ChildItem $stage -Recurse -Include "__pycache__" -Directory | Remove-Item -Recurse -Force
Compress-Archive -Path (Join-Path $dist "stage\shadeNseek") -DestinationPath (Join-Path $dist "shadeNseek.zip") -CompressionLevel Optimal

$lib = Join-Path $dist "stage_lib"
New-Item -ItemType Directory -Force $lib | Out-Null
Copy-Item (Join-Path $root "asset-library") (Join-Path $lib "asset-library") -Recurse -Force
Copy-Item (Join-Path $root "manifest.json") (Join-Path $lib "manifest.json") -Force
Compress-Archive -Path (Join-Path $lib "*") -DestinationPath (Join-Path $dist "asset-library.zip") -CompressionLevel Optimal
Remove-Item (Join-Path $dist "stage"), $lib -Recurse -Force

Get-ChildItem $dist -Filter *.zip | Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } }
