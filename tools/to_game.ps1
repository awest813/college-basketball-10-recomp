# Drives a Release build from boot into a Play Now game (Duke at North Carolina, player on UNC)
# and screenshots the start of the game. Needs a profile save with autosave on in <build>/user.
#   to_game.ps1 -Out <folder> [-Config Release] [-Extra "--gpu.xenos.readback_resolve=full"] [-Watch 60]
param([string]$Out, [string]$Config = "Release", [string]$Extra = "", [int]$Watch = 60, [double]$Every = 6)
$d = Split-Path $PSScriptRoot
$exe = "$d\out\build\win-amd64\$Config\ncaabb10.exe"
$args2 = "--title.game_root=$d\assets --app.cache_root=$d\out\cache --ui.fullscreen=false --hid.mnk_mode=true $Extra"
$steps = "wait:33","key:RETURN","wait:5","key:SPACE","wait:28","key:RETURN","wait:5","key:SPACE","wait:6","key:D","wait:1.5","key:SPACE","wait:3","key:SPACE","wait:5","key:SPACE","watch:${Watch}:${Every}:g"
& "$PSScriptRoot\drive.ps1" -Exe $exe -GameArgs $args2 -OutDir $Out -Steps $steps
& "$PSScriptRoot\montage.ps1" -Dir $Out -Filter "g*.png" -Out "$Out\sheet.png" -Width 320 -Cols 5
