# Runs to_game.ps1 several times per configuration and builds one sheet per configuration
# with the frames where overlay text should be visible (banner, lineups, live score bar).
#   batch_text.ps1 -Out <folder> -Runs 4 -Configs @{default=""; full="--gpu.xenos.readback_resolve=full"}
param([string]$Out, [int]$Runs = 4, [hashtable]$Configs, [string]$Build = "win-amd64")
Add-Type -AssemblyName System.Drawing
foreach ($name in $Configs.Keys) {
  for ($i = 1; $i -le $Runs; $i++) {
    $dir = Join-Path $Out "$name`_$i"
    & "$PSScriptRoot\to_game.ps1" -Out $dir -Build $Build -Extra $Configs[$name] -Watch 72 -Every 8 | Out-Null
    $log = Get-ChildItem (Join-Path (Split-Path $PSScriptRoot) "out\build\$Build\Release\logs") | Sort-Object LastWriteTime | Select-Object -Last 1
    Copy-Item $log.FullName (Join-Path $dir "game.log")
    "$name run $i done"
  }
  # One row per run: frames 2, 4, 5, 8, 9.
  $w = 384; $h = 216; $pick = 2, 4, 5, 8, 9
  $m = New-Object System.Drawing.Bitmap ($w * $pick.Count), ($h * $Runs); $g = [System.Drawing.Graphics]::FromImage($m)
  $font = New-Object System.Drawing.Font 'Arial', 12
  for ($i = 1; $i -le $Runs; $i++) {
    for ($j = 0; $j -lt $pick.Count; $j++) {
      $f = Join-Path $Out ("$name`_$i\g{0:D3}.png" -f $pick[$j])
      if (Test-Path $f) { $img = [System.Drawing.Image]::FromFile($f); $g.DrawImage($img, $j * $w, ($i - 1) * $h, $w, $h); $img.Dispose() }
    }
    $g.DrawString("$name $i", $font, [System.Drawing.Brushes]::Yellow, 4, ($i - 1) * $h + 4)
  }
  $m.Save((Join-Path $Out "$name.png")); "sheet $name"
}
