# Combines screenshots into one contact sheet: montage.ps1 -Dir <folder> -Filter "game*.png" -Out sheet.png
param([string]$Dir, [string]$Filter = "*.png", [string]$Out, [int]$Cols = 4, [int]$Width = 480)
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem (Join-Path $Dir $Filter) | Sort-Object Name
$h = [int]($Width * 9 / 16); $rows = [math]::Ceiling($files.Count / $Cols)
$m = New-Object System.Drawing.Bitmap ($Width * $Cols), ($h * $rows); $g = [System.Drawing.Graphics]::FromImage($m)
$font = New-Object System.Drawing.Font 'Arial', 12
for ($i = 0; $i -lt $files.Count; $i++) {
  $img = [System.Drawing.Image]::FromFile($files[$i].FullName); $x = ($i % $Cols) * $Width; $y = [math]::Floor($i / $Cols) * $h
  $g.DrawImage($img, $x, $y, $Width, $h); $g.DrawString($files[$i].BaseName, $font, [System.Drawing.Brushes]::Yellow, $x + 4, $y + 4); $img.Dispose()
}
$m.Save($Out); "montage $($files.Count) images -> $Out"
