# Packs a Windows build into a zip that runs on another PC: the exe, the SDK and SDL DLLs and the
# Visual C++ runtime (copied in by the build), without logs, saves, options or linker leftovers.
# The game files are not included; the launcher sets them up from a disc image on first start.
#   package.ps1 [-Build win-amd64-patched] [-Config Release] [-Out <zip>]
param([string]$Build = "win-amd64-patched", [string]$Config = "Release", [string]$Out = "")
$ErrorActionPreference = "Stop"
$d = Split-Path $PSScriptRoot
$src = "$d\out\build\$Build\$Config"
if (-not (Test-Path "$src\ncaabb10.exe")) { throw "no build at $src" }
foreach ($dll in "vcruntime140.dll", "msvcp140.dll") {
  if (-not (Test-Path "$src\$dll")) { throw "$dll missing from $src; rebuild with build_win.cmd" }
}
if (-not $Out) { $Out = "$d\out\ncaabb10-windows-x64.zip" }
$stage = Join-Path ([IO.Path]::GetTempPath()) ("ncaabb10-package-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $stage | Out-Null
try {
  Get-ChildItem $src -File | Where-Object { $_.Extension -in ".exe", ".dll" } |
    Copy-Item -Destination $stage
  Copy-Item "$d\README.md" $stage
  if (Test-Path $Out) { Remove-Item $Out -Confirm:$false }
  Compress-Archive -Path "$stage\*" -DestinationPath $Out
  $files = (Get-ChildItem $stage -File).Count
  $mb = (Get-Item $Out).Length / 1MB
  "packed $files files into $Out ({0:N1} MB)" -f $mb
} finally {
  Remove-Item $stage -Recurse -Force -Confirm:$false
}
