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
$env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS = '1'  # The test controller works without window focus.
$startArgs = @{ FilePath = $Exe; WorkingDirectory = (Split-Path $Exe); PassThru = $true }
if ($GameArgs) { $startArgs.ArgumentList = $GameArgs }  # Start-Process rejects an empty list.
$p = Start-Process @startArgs
# The game has its copy of the environment now: do not leave these set in the caller's shell.
Remove-Item Env:NCAABB10_INPUT_FILE, Env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS -ErrorAction SilentlyContinue
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
# burst:<count>:<milliseconds>:<prefix> takes <count> screenshots <milliseconds> apart.
function Burst($v) {
  $count, $ms, $prefix = $v.Split(':')
  for ($i = 1; $i -le [int]$count; $i++) { Shot ('{0}{1:D3}' -f $prefix, $i) | Out-Null; Start-Sleep -Milliseconds ([int]$ms) }
  "burst $count shots as $prefix"
}
# winsize:<name> logs the game window's size (to check fullscreen).
function WinSize($name) {
  $h = Hwnd; $r = New-Object RECT; [W]::GetClientRect($h, [ref]$r) | Out-Null
  "size $name $($r.R)x$($r.B)"
}
# ui:<KEY>[+ALT] presses a key through the game window's event queue (UI binds, ImGui).
function UiKey($v) {
  $name, $mod = $v.Split('+')
  $vk = [uint32][System.Windows.Forms.Keys]::$name
  Append ("ui $vk " + $(if ($mod -eq 'ALT') { 'alt' } else { '' })); "ui key $v"
}
# Test controllers (SDL virtual pads): pad:<BUTTON>[:<ms>] presses a button, padhold:<BUTTON> and
# padup:<BUTTON> hold and release one, axis:<AXIS>:<value> sets a stick or trigger (-32768..32767),
# text:<text> types into a focused text field. unplug:<slot> unplugs one. <BUTTON>@1 and <AXIS>@1 use a second controller (player 2).
$PadButtons = @{ A = 0; B = 1; X = 2; Y = 3; BACK = 4; GUIDE = 5; START = 6; LS = 7; RS = 8; LB = 9; RB = 10; UP = 11; DOWN = 12; LEFT = 13; RIGHT = 14 }
$PadAxes = @{ LX = 0; LY = 1; RX = 2; RY = 3; LT = 4; RT = 5 }
function Slot($name) { $n, $slot = $name.Split('@'); if (-not $slot) { $slot = 0 }; $n, $slot }
function Pad($v) {
  $name, $ms = $v.Split(':'); if (-not $ms) { $ms = 150 }
  $n, $slot = Slot $name; $b = $PadButtons[$n]
  Append "pad $b 1 $slot"; Start-Sleep -Milliseconds ([int]$ms); Append "pad $b 0 $slot"; "pad $name"
}
function PadSet($v, $down) { $n, $slot = Slot $v; Append "pad $($PadButtons[$n]) $down $slot"; "pad $v $down" }
function Axis($v) { $name, $value = $v.Split(':'); $n, $slot = Slot $name; Append "axis $($PadAxes[$n]) $value $slot"; "axis $name $value" }
foreach ($s in $Steps) {
  if ($p.HasExited) { "game exited with code $($p.ExitCode)"; break }
  $k, $v = $s.Split(':', 2)
  switch ($k) { 'wait' { Start-Sleep -Seconds ([double]$v) } 'shot' { Shot $v } 'key' { Key $v } 'watch' { Watch $v } 'burst' { Burst $v } 'winsize' { WinSize $v } 'ui' { UiKey $v } 'pad' { Pad $v } 'padhold' { PadSet $v 1 } 'padup' { PadSet $v 0 } 'axis' { Axis $v } 'unplug' { Append "unplug $v"; "unplug $v" } 'text' { Append "text $v"; "text $v" } }
}
if (-not $Keep -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force; "stopped" }
