param(
  [string]$EditorPath = "D:\UE\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe",
  [string]$FfmpegPath = "E:\Projects\SuperInvincibleTank_BugFix\Tools\FFmpeg\Win64\bin\ffmpeg.exe",
  [string]$ProjectPath = "",
  [string]$Map = "/Engine/Maps/Templates/OpenWorld",
  [int]$Port = 7951,
  [int]$EntityCount = 500,
  [string]$Scenario = "StaticTarget",
  [string]$VisualMode = "",
  [int]$InitialAliveCount = -1,
  [int]$DurationSeconds = 45,
  [int]$ServerWarmupSeconds = 8,
  [int]$CaptureSeconds = 45,
  [int]$ClientHoldSeconds = 8,
  [int]$ResX = 1280,
  [int]$ResY = 720,
  [int]$WindowX = 0,
  [int]$WindowY = 0,
  [int]$FrameRate = 30,
  [int]$Crf = 23,
  [int]$Mpeg4Quality = 5,
  [int]$ClientReadyTimeoutSeconds = 60,
  [bool]$RequireClientReady = $true,
  [string]$VideoEncoder = "auto",
  [string]$Preset = "veryfast",
  [switch]$KeepClientTopMost,
  [switch]$NoMinimizeDesktop
)

$ErrorActionPreference = "Stop"

function Stop-ProcessIfRunning {
  param([System.Diagnostics.Process]$Process)
  if ($Process -and !$Process.HasExited) {
    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
  }
}

function Stop-FfmpegGracefully {
  param([System.Diagnostics.Process]$Process)
  if (!$Process -or $Process.HasExited) {
    return
  }

  try {
    $Process.StandardInput.WriteLine("q")
    if (!$Process.WaitForExit(5000)) {
      Stop-ProcessIfRunning $Process
    }
  }
  catch {
    Stop-ProcessIfRunning $Process
  }
}

function Resolve-VideoEncoderArgs {
  param(
    [string]$FfmpegPath,
    [string]$RequestedEncoder,
    [int]$Crf,
    [int]$Mpeg4Quality,
    [string]$Preset
  )

  $EncoderList = & $FfmpegPath -hide_banner -encoders 2>$null
  $EncoderText = $EncoderList -join "`n"
  $ResolvedEncoder = $RequestedEncoder
  if ($RequestedEncoder -eq "auto") {
    if ($EncoderText -match "\blibx264\b") {
      $ResolvedEncoder = "libx264"
    }
    elseif ($EncoderText -match "\bmpeg4\b") {
      $ResolvedEncoder = "mpeg4"
    }
    else {
      throw "No supported ffmpeg video encoder found. Need libx264 or mpeg4."
    }
  }

  switch ($ResolvedEncoder) {
    "libx264" {
      return @("-c:v", "libx264", "-preset", "$Preset", "-crf", "$Crf", "-pix_fmt", "yuv420p")
    }
    "mpeg4" {
      return @("-c:v", "mpeg4", "-q:v", "$Mpeg4Quality", "-pix_fmt", "yuv420p")
    }
    default {
      return @("-c:v", "$ResolvedEncoder", "-pix_fmt", "yuv420p")
    }
  }
}

if (-not ("CrowdDemoWin32WindowTools" -as [type])) {
  Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class CrowdDemoWin32WindowTools
{
  [DllImport("user32.dll")]
  public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

  [DllImport("user32.dll")]
  public static extern bool SetForegroundWindow(IntPtr hWnd);

  [DllImport("user32.dll")]
  public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, UInt32 uFlags);
}
"@
}

function Wait-ForMainWindowHandle {
  param(
    [System.Diagnostics.Process]$Process,
    [int]$TimeoutSeconds = 30
  )

  $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  while ((Get-Date) -lt $Deadline) {
    if (!$Process -or $Process.HasExited) {
      return [IntPtr]::Zero
    }
    $Process.Refresh()
    if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
      return $Process.MainWindowHandle
    }
    Start-Sleep -Milliseconds 250
  }
  return [IntPtr]::Zero
}

function Place-CaptureWindow {
  param(
    [System.Diagnostics.Process]$Process,
    [int]$X,
    [int]$Y,
    [int]$Width,
    [int]$Height,
    [bool]$TopMost
  )

  $Handle = Wait-ForMainWindowHandle -Process $Process -TimeoutSeconds 30
  if ($Handle -eq [IntPtr]::Zero) {
    Write-Warning "[CrowdDemoCapture] Client main window handle was not found; desktop capture may miss the client."
    return
  }

  $SwRestore = 9
  $SwpShowWindow = 0x0040
  $HwndTopMost = [IntPtr](-1)
  $HwndTop = [IntPtr]::Zero
  $InsertAfter = $HwndTop
  if ($TopMost) {
    $InsertAfter = $HwndTopMost
  }
  [void][CrowdDemoWin32WindowTools]::ShowWindow($Handle, $SwRestore)
  [void][CrowdDemoWin32WindowTools]::SetWindowPos($Handle, $InsertAfter, $X, $Y, $Width, $Height, $SwpShowWindow)
  [void][CrowdDemoWin32WindowTools]::SetForegroundWindow($Handle)
  Start-Sleep -Milliseconds 750
  return $Handle
}

function Wait-ForLogPattern {
  param(
    [string]$Path,
    [string]$Pattern,
    [int]$TimeoutSeconds
  )

  $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  while ((Get-Date) -lt $Deadline) {
    if (Test-Path -LiteralPath $Path) {
      $Match = Select-String -Path $Path -Pattern $Pattern -SimpleMatch -Quiet
      if ($Match) {
        return $true
      }
    }
    Start-Sleep -Milliseconds 500
  }
  return $false
}

function Get-ImageMeanLuma {
  param([string]$Path)
  Add-Type -AssemblyName System.Drawing
  $Bitmap = New-Object System.Drawing.Bitmap($Path)
  try {
    [double]$Sum = 0.0
    [int]$Count = 0
    for ($Y = 0; $Y -lt $Bitmap.Height; $Y += 16) {
      for ($X = 0; $X -lt $Bitmap.Width; $X += 16) {
        $Pixel = $Bitmap.GetPixel($X, $Y)
        $Sum += 0.2126 * $Pixel.R + 0.7152 * $Pixel.G + 0.0722 * $Pixel.B
        $Count++
      }
    }
    return $Sum / [Math]::Max(1, $Count)
  }
  finally {
    $Bitmap.Dispose()
  }
}

$Root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
  $ProjectPath = Join-Path $Root "MassAICrowdDemo.uproject"
}

if (!(Test-Path -LiteralPath $EditorPath)) {
  throw "UnrealEditor not found: $EditorPath"
}
if (!(Test-Path -LiteralPath $FfmpegPath)) {
  throw "ffmpeg not found: $FfmpegPath"
}
if (!(Test-Path -LiteralPath $ProjectPath)) {
  throw "Project not found: $ProjectPath"
}

$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$RunId = "CrowdDemoCapture_${Port}_${Stamp}"
$LogDir = Join-Path $Root "Saved\CrowdDemoCapture\$RunId"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$ServerLog = Join-Path $LogDir "server.log"
$ClientLog = Join-Path $LogDir "client.log"
$FfmpegLog = Join-Path $LogDir "ffmpeg.log"
$VideoPath = Join-Path $LogDir "crowd_demo_phase_f.mp4"
$CommonArgs = "-CrowdDemoEntityCount=$EntityCount -CrowdDemoScenario=$Scenario -CrowdDemoDurationSeconds=$DurationSeconds -unattended -NoSound"
if ($RequireClientReady) {
  $CommonArgs = "$CommonArgs -CrowdDemoRequireClientReady -CrowdDemoReadyLeadSeconds=3 -CrowdDemoReadyTimeoutSeconds=60"
}
if ($InitialAliveCount -ge 0) {
  $CommonArgs = "$CommonArgs -CrowdDemoInitialAliveCount=$InitialAliveCount"
}
if (![string]::IsNullOrWhiteSpace($VisualMode)) {
  $CommonArgs = "$CommonArgs -CrowdDemoVisualMode=$VisualMode"
}

$ServerProcess = $null
$ClientProcess = $null
$FfmpegProcess = $null
$ShellApplication = $null

$ServerArgs = "`"$ProjectPath`" $Map -server -port=$Port -NullRHI -log -AbsLog=`"$ServerLog`" $CommonArgs"
$ClientArgs = "`"$ProjectPath`" 127.0.0.1:$Port -game -windowed -ResX=$ResX -ResY=$ResY -WinX=$WindowX -WinY=$WindowY -AbsLog=`"$ClientLog`" $CommonArgs"

$EncoderArgs = Resolve-VideoEncoderArgs -FfmpegPath $FfmpegPath -RequestedEncoder $VideoEncoder -Crf $Crf -Mpeg4Quality $Mpeg4Quality -Preset $Preset

Write-Host "[CrowdDemoCapture] Logs: $LogDir"
Write-Host "[CrowdDemoCapture] Video: $VideoPath"
Write-Host "[CrowdDemoCapture] Starting server: $ServerArgs"
$ServerProcess = Start-Process -FilePath $EditorPath -ArgumentList $ServerArgs -PassThru -WindowStyle Hidden

try {
  Start-Sleep -Seconds $ServerWarmupSeconds

  if (!$NoMinimizeDesktop) {
    $ShellApplication = New-Object -ComObject Shell.Application
    $ShellApplication.MinimizeAll()
    Start-Sleep -Seconds 1
  }

  Write-Host "[CrowdDemoCapture] Starting visible client: $ClientArgs"
  $ClientProcess = Start-Process -FilePath $EditorPath -ArgumentList $ClientArgs -PassThru

  $ClientWindowHandle = Place-CaptureWindow -Process $ClientProcess -X $WindowX -Y $WindowY -Width $ResX -Height $ResY -TopMost ([bool]$KeepClientTopMost)
  if ($ClientWindowHandle -eq [IntPtr]::Zero) {
    throw "Client window handle is unavailable; refusing desktop fallback capture."
  }
  Write-Host "[CrowdDemoCapture] Waiting for validation readiness..."
  $ClientReady = Wait-ForLogPattern -Path $ClientLog -Pattern "CrowdDemoValidationReady role=client" -TimeoutSeconds $ClientReadyTimeoutSeconds
  if (!$ClientReady) {
    throw "Client readiness was not observed before capture."
  }
  # Network travel can recreate the native client window. Reacquire the handle
  # after readiness so gdigrab never receives a stale pre-travel HWND.
  $ClientWindowHandle = Place-CaptureWindow -Process $ClientProcess -X $WindowX -Y $WindowY -Width $ResX -Height $ResY -TopMost ([bool]$KeepClientTopMost)
  if ($ClientWindowHandle -eq [IntPtr]::Zero) {
    throw "Post-travel client window handle is unavailable."
  }
  $FfmpegArgs = @(
    "-y",
    "-hide_banner",
    "-nostats",
    "-loglevel", "warning",
    "-f", "gdigrab",
    "-draw_mouse", "0",
    "-framerate", "$FrameRate",
    "-offset_x", "$WindowX",
    "-offset_y", "$WindowY",
    "-video_size", "${ResX}x${ResY}",
    "-i", "desktop",
    "-vf", "scale=${ResX}:${ResY}:force_original_aspect_ratio=decrease,pad=${ResX}:${ResY}:(ow-iw)/2:(oh-ih)/2"
  ) + $EncoderArgs + @(
    "`"$VideoPath`""
  )

  Write-Host "[CrowdDemoCapture] Starting ffmpeg: $($FfmpegArgs -join ' ')"
  $FfmpegStartInfo = New-Object System.Diagnostics.ProcessStartInfo
  $FfmpegStartInfo.FileName = $FfmpegPath
  $FfmpegStartInfo.Arguments = $FfmpegArgs -join " "
  $FfmpegStartInfo.WorkingDirectory = $LogDir
  $FfmpegStartInfo.UseShellExecute = $false
  $FfmpegStartInfo.RedirectStandardInput = $true
  $FfmpegStartInfo.RedirectStandardError = $true
  $FfmpegStartInfo.RedirectStandardOutput = $true
  $FfmpegStartInfo.CreateNoWindow = $true

  $FfmpegProcess = New-Object System.Diagnostics.Process
  $FfmpegProcess.StartInfo = $FfmpegStartInfo
  [void]$FfmpegProcess.Start()

  Start-Sleep -Seconds $CaptureSeconds
  Stop-FfmpegGracefully $FfmpegProcess

  $FfmpegOutput = $FfmpegProcess.StandardOutput.ReadToEnd()
  $FfmpegError = $FfmpegProcess.StandardError.ReadToEnd()
  Set-Content -LiteralPath $FfmpegLog -Value ($FfmpegOutput + [Environment]::NewLine + $FfmpegError)

  Start-Sleep -Seconds $ClientHoldSeconds
}
finally {
  Stop-FfmpegGracefully $FfmpegProcess
  Stop-ProcessIfRunning $ClientProcess
  Stop-ProcessIfRunning $ServerProcess
  if ($ShellApplication -and !$NoMinimizeDesktop) {
    Start-Sleep -Milliseconds 500
    $ShellApplication.UndoMinimizeAll()
  }
}

if (Test-Path -LiteralPath $ServerLog) {
  Select-String -Path $ServerLog -Pattern "CrowdDemo:","CrowdDemoMass:","CrowdDemoSummary","CrowdDemoArena:" -SimpleMatch | Select-Object -Last 24
}
if (Test-Path -LiteralPath $ClientLog) {
  Select-String -Path $ClientLog -Pattern "CrowdDemo:","CrowdDemoVisual:","CrowdDemoSummary","CrowdDemoArena:" -SimpleMatch | Select-Object -Last 18
}
if (Test-Path -LiteralPath $VideoPath) {
  $VideoItem = Get-Item -LiteralPath $VideoPath
  $QaFrame = Join-Path $LogDir "capture_qa_mid.png"
  $ContactSheet = Join-Path $LogDir "capture_contact_sheet.jpg"
  $MidpointSeconds = [Math]::Max(1, [Math]::Floor($CaptureSeconds / 2))
  & $FfmpegPath -y -hide_banner -loglevel error -ss $MidpointSeconds -i $VideoPath -frames:v 1 $QaFrame
  & $FfmpegPath -y -hide_banner -loglevel error -i $VideoPath -vf "fps=1/4,scale=480:-1,tile=4x2" -frames:v 1 $ContactSheet
  if (!(Test-Path -LiteralPath $QaFrame)) {
    throw "Capture QA frame was not created."
  }
  $MeanLuma = Get-ImageMeanLuma -Path $QaFrame
  if ($MeanLuma -lt 8.0 -or $MeanLuma -gt 247.0) {
    throw "Capture QA rejected near-black/near-white video. mean_luma=$MeanLuma"
  }
  Write-Host "[CrowdDemoCapture] QA mean_luma=$([Math]::Round($MeanLuma, 3)) contact_sheet=$ContactSheet"
  Write-Host "[CrowdDemoCapture] VideoFile=$($VideoItem.FullName) SizeBytes=$($VideoItem.Length)"
}
else {
  Write-Warning "[CrowdDemoCapture] Video was not created. Check $FfmpegLog"
}
