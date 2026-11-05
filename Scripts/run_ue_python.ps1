param([Parameter(Mandatory=$true)][string]$Script, [int]$TimeoutSec = 3600, [switch]$WithRHI)
# Runs a Python script inside a dedicated, headless editor process for THIS project only.
$proj = Join-Path $PSScriptRoot "..\shadeNseek.uproject" | Resolve-Path
$cmd = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$logDir = Join-Path $PSScriptRoot "..\Saved\Logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ("py_" + [IO.Path]::GetFileNameWithoutExtension($Script) + ".log")
$scriptPath = (Resolve-Path $Script).Path -replace '\\', '/'
$args = @("`"$proj`"", "-ExecutePythonScript=`"$scriptPath`"", "-unattended", "-nosplash", "-nop4", "-NoLiveCoding", "-stdout", "-FullStdOutLogOutput", "-abslog=`"$log`"")
if (-not $WithRHI) { $args += "-nullrhi" }
$p = Start-Process -FilePath $cmd -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput "$log.stdout"
if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); Write-Output "TIMEOUT" }
Write-Output "EXIT $($p.ExitCode)"
Get-Content $log -ErrorAction SilentlyContinue | Select-String -Pattern "SNS_|Error:|LogPython: Error|Traceback|Exception|line \d+" | Select-Object -Last 80
