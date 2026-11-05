param([string]$Target = "shadeNseekEditor", [string]$Config = "Development")
# Compiles the C++ module with UnrealBuildTool (the editor for THIS project must be closed, Live Coding can't be running).
$proj = (Resolve-Path (Join-Path $PSScriptRoot "..\shadeNseek.uproject")).Path
$bat = "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat"
$log = Join-Path $PSScriptRoot "..\Saved\build_last.log"
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
& $bat $Target Win64 $Config "-Project=$proj" -WaitMutex -NoHotReloadFromIDE *> $log
$code = $LASTEXITCODE
# Only our own diagnostics (engine headers spam deprecation warnings).
Get-Content $log | Select-String -Pattern "shadeNseek\\Source.*(error|warning)|error C|error LNK|fatal error|Result:|Total execution" | Select-Object -Last 60
Write-Output "EXIT $code"
