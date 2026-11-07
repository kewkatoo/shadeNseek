param(
    [string]$Extra = "-autoplay -autoplayexit",
    [string]$Tag = "run",
    [int]$ResX = 1920, [int]$ResY = 1080,
    [int]$TimeoutSec = 600,
    [string]$Exe = "",
    [switch]$NoCsv
)
# Plays the packaged build at a real-time frame rate over the autoplay route and reports performance.
#  default: CSV profiler on (game/render/GPU thread times and per-pass GPU timings every frame), summarised by
#           Scripts/perf_report.py. The profiler itself costs a little render-thread time.
#  -NoCsv:  only the game's own -perflog (wall-clock frame times while Shade is in play) - works for Shipping builds.
#   Scripts/perf_run.ps1 -Tag base
#   Scripts/perf_run.ps1 -NoCsv -Tag ship -Exe Dist\PackagedShipping\Windows\shadeNseek.exe
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Exe) { $Exe = Join-Path $root "Dist\Packaged\Windows\shadeNseek.exe" }
$Exe = (Resolve-Path $Exe).Path
$gameSaved = Join-Path (Split-Path $Exe) "shadeNseek\Saved"
$csvDir = Join-Path $gameSaved "Profiling\CSV"
$out = Join-Path $root "Saved\Perf"
New-Item -ItemType Directory -Force $out | Out-Null
$log = Join-Path $out "$Tag.log"
$perfLogs = @((Join-Path $gameSaved "Perf\perflog.txt"), (Join-Path $env:LOCALAPPDATA "shadeNseek\Saved\Perf\perflog.txt"))
foreach ($f in $perfLogs) { Remove-Item $f -ErrorAction SilentlyContinue }
$before = @(Get-ChildItem $csvDir -Filter *.csv -ErrorAction SilentlyContinue | ForEach-Object FullName)
if ($NoCsv) {
    $args = "-windowed -ResX=$ResX -ResY=$ResY -nosplash -unattended -perflog -abslog=`"$log`" $Extra"
} else {
    $args = "-windowed -ResX=$ResX -ResY=$ResY -nosplash -unattended -perflog -csvCaptureFrames=100000 -csvGpuStats " +
            "-ExecCmds=`"r.VSync 0,t.MaxFPS 0,r.GPUCsvStatsEnabled 1`" -abslog=`"$log`" $Extra"
}
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru
if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); Write-Output "TIMEOUT" }
Write-Output "EXIT $($p.ExitCode)"
foreach ($f in $perfLogs) { if (Test-Path $f) { Write-Output "perflog: $(Get-Content $f | Select-Object -First 1)" } }
if (-not $NoCsv) {
    $new = Get-ChildItem $csvDir -Filter *.csv -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.FullName } |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if ($new) {
        Copy-Item $new.FullName (Join-Path $out "$Tag.csv") -Force
        python -I (Join-Path $PSScriptRoot "perf_report.py") (Join-Path $out "$Tag.csv") $log
    } else { Write-Output "no CSV written" }
}
