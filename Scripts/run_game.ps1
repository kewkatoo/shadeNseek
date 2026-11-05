param([int]$TimeoutSec = 2400, [string]$Extra = "-autoplay -autoplayexit -autoplayshots", [int]$ResX = 1600, [int]$ResY = 900)
# Launches Shade and Seek as a standalone game process (separate from any open editor) and waits for it to exit.
$proj = (Resolve-Path (Join-Path $PSScriptRoot "..\shadeNseek.uproject")).Path
$exe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$log = Join-Path $PSScriptRoot "..\Saved\Logs\game_run.log"
$args = "`"$proj`" -game -windowed -ResX=$ResX -ResY=$ResY -NoLiveCoding -nosplash -unattended -log -abslog=`"$log`" $Extra"
$p = Start-Process -FilePath $exe -ArgumentList $args -PassThru
if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); Write-Output "TIMEOUT" }
Write-Output "EXIT $($p.ExitCode)"
Get-Content $log -ErrorAction SilentlyContinue | Select-String -Pattern "LogShade|Error:|Ensure|Fatal|Shader.*remaining" | Select-Object -Last 120
