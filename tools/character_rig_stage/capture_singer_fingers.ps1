param(
    [Parameter(Mandatory=$true)][string]$AddonRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Variant='gh1_female_singer',
    [string]$Executable='',
    [string]$ArkDirectory='C:\Programming\GitHub\Guitar Hero II\gh2_ps2_hybrid_assets\GEN',
    [string]$CaptureFrames='60,120,180',
    [int]$Frames=190,
    [double]$StartTime=30,
    [double]$Distance=70,
    [double]$TargetZ=2,
    [double]$Pitch=0.18,
    [double]$Yaw=0,
    [string]$TargetBone='',
    [string]$RenderSize='1280x960'
)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $Executable){$Executable="$repo\engine\out\build\win-amd64-release\src\app\ghogx_app.exe"}
$env:GHOGX_ADDONS_DIR=$AddonRoot
$env:GHOGX_HIDE_HUD='1'
$env:GHOGX_HIDE_HIGHWAY='1'
$env:GHOGX_DEBUG_HAND_POSE_ROWS='1'
$env:GHOGX_DEBUG_HAND_POSE_ROLE='guitarist0'
$env:GHOGX_DEBUG_HAND_POSE_STRIDE='0.10'
$env:GHOGX_DIAGNOSTIC_FRONT_CAMERA_DISTANCE="$Distance"
$env:GHOGX_DIAGNOSTIC_FRONT_CAMERA_TARGET_Z="$TargetZ"
$env:GHOGX_DIAGNOSTIC_FRONT_CAMERA_PITCH="$Pitch"
$env:GHOGX_DIAGNOSTIC_FRONT_CAMERA_YAW_OFFSET="$Yaw"
$cameraArgs=@('--diagnostic-front-camera','guitarist0')
if($TargetBone){
    $cameraArgs=@()
    $env:GHOGX_DEBUG_GAMEPLAY_CAMERA='1'
    $env:GHOGX_DEBUG_GAMEPLAY_CAMERA_TARGET="guitarist0:$TargetBone"
    $env:GHOGX_DEBUG_GAMEPLAY_CAMERA_TARGET_Z="$TargetZ"
    $env:GHOGX_DEBUG_GAMEPLAY_CAMERA_DIST="$Distance"
    $env:GHOGX_DEBUG_GAMEPLAY_CAMERA_PITCH="$Pitch"
    $env:GHOGX_DEBUG_GAMEPLAY_CAMERA_YAW="$Yaw"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$nativeArgs=@('--ark-dir',$ArkDirectory,'--song','shoutatthedevil','--auto-start','--difficulty','3','--diagnostic-character-variant',$Variant,'--diagnostic-autoplay','--diagnostic-song-start',"$StartTime")+$cameraArgs+@('--diagnostic-venue','small1','--diagnostic-proof-lighting','--mute-audio','--fixed-dt','0.016666667','--frames',"$Frames",'--render-size',$RenderSize,'--screenshot-dir',$OutputDirectory,'--screenshot-frames',$CaptureFrames)
# Direct file redirection avoids Windows PowerShell 5 wrapping native stderr
# rows and writing UTF-16 ErrorRecord formatting into the diagnostic trace.
$quotedArgs=($nativeArgs | ForEach-Object { '"'+$_+'"' }) -join ' '
$process=Start-Process -FilePath $Executable -ArgumentList $quotedArgs -WindowStyle Hidden -RedirectStandardError "$OutputDirectory.log" -RedirectStandardOutput "$OutputDirectory.stdout.log" -PassThru -Wait
$nativeExit=$process.ExitCode
Get-Content "$OutputDirectory.log" -Tail 4
exit $nativeExit
