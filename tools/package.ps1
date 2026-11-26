# Packs a Windows build into a zip that runs on another PC: the exe, the SDK and SDL DLLs and the
# Visual C++ runtime (copied in by the build), without logs, saves, options or linker leftovers.
# The game files are not included; the launcher sets them up from a disc image on first start.
# THIRD_PARTY_NOTICES.txt goes in too, gathered by tools/notices.py from the SDK source the
# build used (-Sdk, the patched SDK checkout by default).
# Beside the zip it writes <zip name>-symbols.zip with the PDBs of the packed exe and DLLs. Players
# do not need it; keep it with the build, since tools/symbolize.py needs the PDBs of the exact build
# that crashed to name the frames of a crash report someone sends.
#   package.ps1 [-Build win-amd64-patched] [-Config Release] [-Out <zip>] [-Sdk <rexglue-sdk-src>]
param([string]$Build = "win-amd64-patched", [string]$Config = "Release", [string]$Out = "",
      [string]$Sdk = "")
$ErrorActionPreference = "Stop"
$d = Split-Path $PSScriptRoot
# A Debug build needs the debug C runtime, which Microsoft does not allow to be shipped.
if ($Config -eq "Debug") { throw "a Debug build cannot run on another PC; package Release or RelWithDebInfo" }
$src = "$d\out\build\$Build\$Config"
if (-not (Test-Path "$src\ncaabb10.exe")) { throw "no build at $src" }
$runtime = "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_atomic_wait.dll"
foreach ($dll in $runtime) {
  if (-not (Test-Path "$src\$dll")) { throw "$dll missing from $src; rebuild with build_win.cmd" }
}
if (-not $Out) { $Out = "$d\out\ncaabb10-windows-x64.zip" }
$SymbolsOut = [IO.Path]::ChangeExtension($Out, $null).TrimEnd(".") + "-symbols.zip"
if (-not $Sdk) { $Sdk = Join-Path (Split-Path $d) "rexglue-sdk-src" }
if (-not (Test-Path "$Sdk\thirdparty\sdl3")) {
  throw "no SDK source with submodules at $Sdk (needed for the license notices); pass -Sdk"
}
$stage = Join-Path ([IO.Path]::GetTempPath()) ("ncaabb10-package-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $stage | Out-Null
try {
  # Only what the game loads: the SDK's modules (some loaded by name, not imported), SDL and the
  # runtime. Not other DLLs a build folder collects, nor Tracy, which only non-Release builds link.
  $wanted = @("SDL3.dll") + $runtime
  if ($Config -ne "Release") { $wanted += "TracyClient.dll" }
  Copy-Item "$src\ncaabb10.exe" $stage
  Get-ChildItem $src -File -Filter *.dll |
    Where-Object { $_.Name -like "rex*.dll" -or $wanted -contains $_.Name } |
    Copy-Item -Destination $stage
  Copy-Item "$d\README.md" $stage
  Copy-Item "$d\LICENSE" "$stage\LICENSE.txt"
  & python -I "$d\tools\notices.py" $Sdk "$stage\THIRD_PARTY_NOTICES.txt"
  if ($LASTEXITCODE -ne 0) { throw "tools/notices.py failed" }
  if (Test-Path $Out) { Remove-Item $Out -Confirm:$false }
  Compress-Archive -Path "$stage\*" -DestinationPath $Out
  $files = (Get-ChildItem $stage -File).Count
  $mb = (Get-Item $Out).Length / 1MB
  "packed $files files into $Out ({0:N1} MB)" -f $mb

  # The PDBs of what went in, for tools/symbolize.py.
  $pdbs = @(Get-ChildItem $stage -File | Where-Object { $_.Extension -in ".exe", ".dll" } |
    ForEach-Object { Join-Path $src ($_.BaseName + ".pdb") } | Where-Object { Test-Path $_ })
  if (-not ($pdbs | Where-Object { $_ -like "*\ncaabb10.pdb" })) {
    Write-Warning "no ncaabb10.pdb in $src; crash reports from this build cannot be symbolized"
  }
  if ($pdbs.Count) {
    if (Test-Path $SymbolsOut) { Remove-Item $SymbolsOut -Confirm:$false }
    Compress-Archive -Path $pdbs -DestinationPath $SymbolsOut
    "packed $($pdbs.Count) PDBs into $SymbolsOut ({0:N1} MB)" -f ((Get-Item $SymbolsOut).Length / 1MB)
  }
} finally {
  Remove-Item $stage -Recurse -Force -Confirm:$false
}
