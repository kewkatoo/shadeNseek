param([string]$Config = "Development", [string]$Out = "")
# Cooks and packages a standalone Windows build (no editor needed to play) into Dist/Packaged/Windows/.
# Run with no editor open for this project. Takes a while the first time (cook + shader compile).
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$proj = Join-Path $root "shadeNseek.uproject"
if (-not $Out) { $Out = Join-Path $root "Dist\Packaged" }
$uat = "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat"
$log = Join-Path $root "Saved\package_last.log"
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
& $uat BuildCookRun "-project=$proj" -noP4 -platform=Win64 "-clientconfig=$Config" -build -cook -stage -pak -iostore `
    -compressed -prereqs -archive "-archivedirectory=$Out" -utf8output -unattended *> $log
$code = $LASTEXITCODE
Get-Content $log | Select-String -Pattern "error|Warning: .*shadeNseek|BUILD SUCCESSFUL|AutomationTool exiting|Took .*s to run" |
    Select-Object -Last 60
Write-Output "EXIT $code"
