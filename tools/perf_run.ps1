# Boots into a Play Now game with NCAABB10_PERF=1, samples the process's GPU 3D engine use, and
# prints per-phase averages: menus (before tip-off) and in game (the last -GameSeconds).
#   perf_run.ps1 -Out <folder> [-Build win-amd64-patched] [-Extra "--gpu.d3d12.render_target_path=rtv"]
param([string]$Out, [string]$Build = "win-amd64-patched", [string]$Extra = "", [int]$Watch = 120,
      [int]$GameSeconds = 60)
$d = Split-Path $PSScriptRoot
New-Item -ItemType Directory -Force $Out | Out-Null
$gpuFile = Join-Path $Out "gpu.txt"
Remove-Item $gpuFile -ErrorAction SilentlyContinue
$job = Start-Job -ScriptBlock { param($out, $seconds)
  $end = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $end) {
    $p = Get-Process ncaabb10 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p) {
      try {
        $c = (Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage' -ErrorAction Stop).CounterSamples |
          Where-Object { $_.InstanceName -like "pid_$($p.Id)_*" } | Measure-Object CookedValue -Sum
        Add-Content $out ("{0} {1:N1}" -f (Get-Date).ToString('HH:mm:ss'), $c.Sum)
      } catch {}
    }
    Start-Sleep 4
  }
} -ArgumentList $gpuFile, ($Watch + 140)
$env:NCAABB10_PERF = "1"
& "$PSScriptRoot\to_game.ps1" -Out $Out -Build $Build -Extra $Extra -Watch $Watch -Every 20 | Out-Null
Stop-Job $job; Remove-Job $job
$log = Get-ChildItem "$d\out\build\$Build\Release\logs" | Sort-Object LastWriteTime | Select-Object -Last 1
Copy-Item $log.FullName (Join-Path $Out "game.log")

# Per-window lines: time, fps, cpu.
$rows = Get-Content $log.FullName | Where-Object { $_ -match 'ncaabb10: perf ([\d.]+) fps, frame avg ([\d.]+) ms p95 ([\d.]+) ms max ([\d.]+) ms.*cpu ([\d.]+) cores' } | ForEach-Object {
  $null = $_ -match '^\S+ (\d\d:\d\d:\d\d)'; $t = $Matches[1]
  $null = $_ -match 'perf ([\d.]+) fps, frame avg ([\d.]+) ms p95 ([\d.]+) ms max ([\d.]+) ms.*cpu ([\d.]+) cores'
  [pscustomobject]@{ Time = [datetime]$t; Fps = [double]$Matches[1]; P95 = [double]$Matches[3]; Cpu = [double]$Matches[5] }
}
$gpu = Get-Content $gpuFile -ErrorAction SilentlyContinue | ForEach-Object { $p = $_ -split ' '; [pscustomobject]@{ Time = [datetime]$p[0]; Gpu = [double]$p[1] } }
if (-not $rows) { "no perf lines"; return }
$gameStart = $rows[-1].Time.AddSeconds(-$GameSeconds)
$game = $rows | Where-Object { $_.Time -ge $gameStart }
$gameGpu = $gpu | Where-Object { $_.Time -ge $gameStart }
"{0}: in game (last {1} s) {2:N1} fps, p95 {3:N1} ms, cpu {4:N2} cores, gpu3d {5:N0}%" -f (Split-Path $Out -Leaf), $GameSeconds,
  ($game | Measure-Object Fps -Average).Average, ($game | Measure-Object P95 -Average).Average,
  ($game | Measure-Object Cpu -Average).Average, ($gameGpu | Measure-Object Gpu -Average).Average
