# Test driver: starts the game, then runs a script of steps against its window only.
# Keys go through the game's NCAABB10_INPUT_FILE feed (never the desktop); screenshots use
# PrintWindow, so other windows on the desktop are neither touched nor captured.
#   -Steps "wait:40","shot:title","key:RETURN","wait:5","shot:after","watch:60:5:game"
# Keys are System.Windows.Forms.Keys names (RETURN, ESCAPE, UP, DOWN, LEFT, RIGHT, SPACE, A..Z).
param([string]$Exe, [string]$GameArgs, [string[]]$Steps, [string]$OutDir, [switch]$Keep)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public struct RECT { public int L, T, R, B; }
public static class W {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
}
"@
New-Item -ItemType Directory -Force $OutDir | Out-Null
$feed = Join-Path $OutDir 'input.txt'; Set-Content -Path $feed -Value '' -NoNewline
$env:NCAABB10_INPUT_FILE = $feed
$p = Start-Process -FilePath $Exe -ArgumentList $GameArgs -WorkingDirectory (Split-Path $Exe) -PassThru
function Hwnd { $p.Refresh(); $p.MainWindowHandle }
function Shot($name) {
  $h = Hwnd; $r = New-Object RECT; [W]::GetClientRect($h, [ref]$r) | Out-Null
  if ($r.R -le 0) { "no window for $name"; return }
  $b = New-Object System.Drawing.Bitmap $r.R, $r.B; $g = [System.Drawing.Graphics]::FromImage($b)
  $dc = $g.GetHdc(); [W]::PrintWindow($h, $dc, 3) | Out-Null; $g.ReleaseHdc($dc)
  $b.Save((Join-Path $OutDir "$name.png")); "shot $name"
}
function Append($text) {
  for ($i = 0; $i -lt 50; $i++) { try { [IO.File]::AppendAllText($feed, "$text`n"); return } catch { Start-Sleep -Milliseconds 10 } }
  "could not write '$text' to $feed"
}
function Key($name, $holdMs = 150) {
  $vk = [uint32][System.Windows.Forms.Keys]::$name
  Append "down $vk"; Start-Sleep -Milliseconds $holdMs
  Append "up $vk"; "key $name"
}
# watch:<seconds>:<interval>:<prefix> takes a screenshot every <interval> seconds.
function Watch($v) {
  $secs, $every, $prefix = $v.Split(':'); $n = [int]([double]$secs / [double]$every)
  for ($i = 1; $i -le $n; $i++) { Start-Sleep -Seconds ([double]$every); if ($p.HasExited) { return }; Shot ('{0}{1:D3}' -f $prefix, $i) | Out-Null }
  "watched $n shots as $prefix"
}
foreach ($s in $Steps) {
  if ($p.HasExited) { "game exited with code $($p.ExitCode)"; break }
  $k, $v = $s.Split(':', 2)
  switch ($k) { 'wait' { Start-Sleep -Seconds ([double]$v) } 'shot' { Shot $v } 'key' { Key $v } 'watch' { Watch $v } }
}
if (-not $Keep -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force; "stopped" }
