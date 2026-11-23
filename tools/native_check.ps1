# Classifies the 5-second perf windows of a game log (NCAABB10_PERF=1) by whether the title ran at
# one of its native rates:
#   60      at least 59.5 fps and no frame over 25 ms (no missed vblank)
#   30      29.7-30.3 fps and no frame over 42 ms (the title's own 30 Hz presentation)
#   switch  next to a 30 window: the title changing rate inside the window
#   slow    anything else
#   native_check.ps1 -Log <game.log> [-SkipFirst 6] [-Last N]   (skips the boot windows; -Last keeps the final N)
param([string]$Log, [int]$SkipFirst = 6, [int]$Last = 0)
$rows = Get-Content $Log | Where-Object { $_ -match 'ncaabb10: perf ([\d.]+) fps, frame avg [\d.]+ ms p95 ([\d.]+) ms max ([\d.]+) ms' } | ForEach-Object {
  $null = $_ -match '^\S+ (\d\d:\d\d:\d\d).*perf ([\d.]+) fps, frame avg [\d.]+ ms p95 ([\d.]+) ms max ([\d.]+) ms'
  [pscustomobject]@{ Time = $Matches[1]; Fps = [double]$Matches[2]; P95 = [double]$Matches[3]; Max = [double]$Matches[4]; Kind = '' }
} | Select-Object -Skip $SkipFirst
if ($Last -gt 0) { $rows = $rows | Select-Object -Last $Last }
foreach ($r in $rows) {
  if ($r.Fps -ge 59.5 -and $r.Max -lt 25) { $r.Kind = '60' }
  elseif ($r.Fps -ge 29.7 -and $r.Fps -le 30.3 -and $r.Max -lt 42) { $r.Kind = '30' }
}
for ($i = 0; $i -lt $rows.Count; $i++) {
  if ($rows[$i].Kind) { continue }
  $prev = if ($i -gt 0) { $rows[$i - 1].Kind } else { '' }
  $next = if ($i + 1 -lt $rows.Count) { $rows[$i + 1].Kind } else { '' }
  $rows[$i].Kind = if ($prev -eq '30' -or $next -eq '30') { 'switch' } else { 'slow' }
}
$total = $rows.Count
"{0} windows ({1:N1} min): " -f $total, ($total * 5 / 60) + (($rows | Group-Object Kind | Sort-Object Name | ForEach-Object { "{0} {1} ({2:N1}%)" -f $_.Name, $_.Count, (100.0 * $_.Count / $total) }) -join ', ')
$slow = $rows | Where-Object { $_.Kind -eq 'slow' }
if ($slow) { "slow windows:"; $slow | ForEach-Object { "  {0} {1,5} fps p95 {2,6} ms max {3,7} ms" -f $_.Time, $_.Fps, $_.P95, $_.Max } }
