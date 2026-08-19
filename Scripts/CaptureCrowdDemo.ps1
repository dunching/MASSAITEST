param(
  [string]$EditorPath = "D:\UE\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe",
  [string]$FfmpegPath = "",
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
  [switch]$NoMinimizeDesktop,
  [switch]$T7StateAcceptance,
  [double]$EventSliceLeadSeconds = 1.0,
  [double]$EventSliceTailSeconds = 3.0,
  [string]$CommonExtraArgs = ""
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "CrowdDemoRunnerGates.ps1")

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
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT
  {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }

  [DllImport("user32.dll")]
  public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

  [DllImport("user32.dll")]
  public static extern bool SetForegroundWindow(IntPtr hWnd);

  [DllImport("user32.dll")]
  public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, UInt32 uFlags);

  [DllImport("user32.dll")]
  public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
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
if ([string]::IsNullOrWhiteSpace($FfmpegPath)) {
  $FfmpegPath = Join-Path $Root "Tools\FFmpeg\Win64\bin\ffmpeg.exe"
}
if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
  $ProjectPath = Join-Path $Root "MassAICrowdDemo.uproject"
}

if ($T7StateAcceptance) {
  $Map = "/Game/Maps/CrowdDemo_MultiStateVatHitResponseSmall"
  $EntityCount = 20
  $Scenario = "SimRoundSoftPressure"
  $RequireClientReady = $true
  $ProductionArgs = @(
    "-CrowdWorkerMovementMode=Production",
    "-CrowdWorkerBehaviorMode=Production",
    "-CrowdWorkerTargetMode=Production",
    "-CrowdWorkerParticleMode=Production",
    "-CrowdWorkerProjectileMode=Production",
    "-CrowdWorkerCombatMode=Production"
  ) -join " "
  $CommonExtraArgs = "$ProductionArgs $CommonExtraArgs".Trim()
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
$StateSidecarPath = Join-Path $LogDir "scenario_state_events.jsonl"
$AcceptanceManifestPath = Join-Path $LogDir "acceptance_manifest.json"
$CommonArgs = "-CrowdDemoEntityCount=$EntityCount -CrowdDemoScenario=$Scenario -CrowdDemoDurationSeconds=$DurationSeconds -unattended -NoSound"
if (![string]::IsNullOrWhiteSpace($CommonExtraArgs)) {
  $CommonArgs = "$CommonArgs $CommonExtraArgs"
}
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
$CaptureStartUtcTicks = 0L

$ServerArgs = "`"$ProjectPath`" $Map -server -port=$Port -NullRHI -log -AbsLog=`"$ServerLog`" $CommonArgs"
$ClientArgs = "`"$ProjectPath`" 127.0.0.1:$Port -game -windowed -ResX=$ResX -ResY=$ResY -WinX=$WindowX -WinY=$WindowY -AbsLog=`"$ClientLog`" $CommonArgs"
if ($T7StateAcceptance) {
  $ClientArgs = "$ClientArgs -CrowdDemoDrawScenarioStateLabels -CrowdDemoScenarioStateSidecar=`"$StateSidecarPath`""
}

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

  $ClientWindowHandle = Place-CaptureWindow -Process $ClientProcess -X $WindowX -Y $WindowY -Width $ResX -Height $ResY -TopMost $true
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
  $ClientWindowHandle = Place-CaptureWindow -Process $ClientProcess -X $WindowX -Y $WindowY -Width $ResX -Height $ResY -TopMost $true
  if ($ClientWindowHandle -eq [IntPtr]::Zero) {
    throw "Post-travel client window handle is unavailable."
  }
  $CaptureRect = New-Object CrowdDemoWin32WindowTools+RECT
  if (![CrowdDemoWin32WindowTools]::GetWindowRect($ClientWindowHandle, [ref]$CaptureRect)) {
    throw "Failed to query the post-travel client window rectangle."
  }
  $CaptureWidth = $CaptureRect.Right - $CaptureRect.Left
  $CaptureHeight = $CaptureRect.Bottom - $CaptureRect.Top
  if ($CaptureWidth -le 0 -or $CaptureHeight -le 0) {
    throw "Invalid post-travel client window rectangle."
  }
  $FfmpegArgs = @(
    "-y",
    "-hide_banner",
    "-nostats",
    "-loglevel", "warning",
    "-f", "gdigrab",
    "-draw_mouse", "0",
    "-framerate", "$FrameRate",
    "-offset_x", "$($CaptureRect.Left)",
    "-offset_y", "$($CaptureRect.Top)",
    "-video_size", "${CaptureWidth}x${CaptureHeight}",
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
  $CaptureStartUtcTicks = [DateTime]::UtcNow.Ticks
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

if ($T7StateAcceptance) {
  foreach ($RuntimeLog in @($ServerLog, $ClientLog)) {
    if (!(Test-Path -LiteralPath $RuntimeLog)) {
      throw "T7 runtime log is missing: $RuntimeLog"
    }
  }
  $HardFailure = @(Get-CrowdDemoHardFailures @(
      $ServerLog, $ClientLog)) | Select-Object -First 1
  if ($HardFailure) {
    throw "T7 runtime hard failure in $($HardFailure.Path): $($HardFailure.Line.Trim())"
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

if ($T7StateAcceptance) {
  if (!(Test-Path -LiteralPath $VideoPath)) {
    throw "T7 acceptance video was not created."
  }
  if (!(Test-Path -LiteralPath $StateSidecarPath)) {
    throw "T7 authoritative state sidecar was not created: $StateSidecarPath"
  }
  if ($CaptureStartUtcTicks -le 0) {
    throw "T7 capture start timestamp was not recorded."
  }

  $SidecarRows = @()
  foreach ($Line in Get-Content -LiteralPath $StateSidecarPath -Encoding utf8) {
    if ([string]::IsNullOrWhiteSpace($Line)) {
      continue
    }
    try {
      $SidecarRows += $Line | ConvertFrom-Json
    }
    catch {
      throw "Invalid JSONL row in T7 sidecar: $Line"
    }
  }
  $MetadataRows = @($SidecarRows | Where-Object { $_.kind -eq "metadata" })
  $StateRows = @($SidecarRows | Where-Object { $_.kind -eq "state" })
  if ($MetadataRows.Count -lt 1 -or $StateRows.Count -lt 20) {
    throw "T7 sidecar is incomplete. metadata=$($MetadataRows.Count) state_rows=$($StateRows.Count)"
  }
  $ObservedFormationCount = @(
    $StateRows |
      Select-Object -ExpandProperty formation_index -Unique |
      Where-Object { $_ -ge 0 -and $_ -lt 20 }
  ).Count
  if ($ObservedFormationCount -ne 20) {
    throw "T7 sidecar did not observe all 20 formation indices. observed=$ObservedFormationCount"
  }

  $EventSpecifications = @(
    [pscustomobject]@{
      Step = 30
      Name = "knockback"
      Candidates = @($StateRows | Where-Object {
        $_.formation_index -ge 12 -and $_.formation_index -lt 14 -and
        $_.reactive -eq 1 -and $_.business -eq 3 -and $_.visual -eq 3
      })
    },
    [pscustomobject]@{
      Step = 60
      Name = "knockup"
      Candidates = @($StateRows | Where-Object {
        $_.formation_index -ge 14 -and $_.formation_index -lt 16 -and
        $_.reactive -eq 2 -and $_.business -eq 3 -and $_.visual -eq 3
      })
    },
    [pscustomobject]@{
      Step = 90
      Name = "death"
      Candidates = @($StateRows | Where-Object {
        $_.formation_index -ge 16 -and $_.formation_index -lt 20 -and
        $_.alive -eq 0 -and $_.business -eq 4 -and $_.visual -eq 4
      })
    }
  )

  $SliceManifests = @()
  foreach ($Specification in $EventSpecifications) {
    $Event = $Specification.Candidates |
      Sort-Object -Property utc_ticks |
      Select-Object -First 1
    if (!$Event) {
      throw "T7 sidecar did not contain the expected actual event for step $($Specification.Step) ($($Specification.Name))."
    }

    $EventOffsetSeconds =
      ([int64]$Event.utc_ticks - $CaptureStartUtcTicks) / 10000000.0
    if ($EventOffsetSeconds -lt 0.0 -or
        $EventOffsetSeconds -gt $CaptureSeconds) {
      throw "T7 event $($Specification.Name) falls outside the recorded video. offset=$EventOffsetSeconds"
    }
    $SliceStartSeconds = [Math]::Max(
      0.0, $EventOffsetSeconds - $EventSliceLeadSeconds)
    $SliceDurationSeconds = [Math]::Min(
      $EventSliceLeadSeconds + $EventSliceTailSeconds,
      $CaptureSeconds - $SliceStartSeconds)
    $SliceStartText = [string]::Format(
      [Globalization.CultureInfo]::InvariantCulture,
      "{0:0.000}", $SliceStartSeconds)
    $SliceDurationText = [string]::Format(
      [Globalization.CultureInfo]::InvariantCulture,
      "{0:0.000}", $SliceDurationSeconds)
    $SliceBaseName = "step_{0:D3}_{1}" -f
      $Specification.Step, $Specification.Name
    $SlicePath = Join-Path $LogDir "$SliceBaseName.mp4"
    $SliceContactSheetPath = Join-Path $LogDir "$SliceBaseName.jpg"
    $SliceArgs = @(
      "-y", "-hide_banner", "-loglevel", "error",
      "-ss", $SliceStartText,
      "-i", $VideoPath,
      "-t", $SliceDurationText
    ) + $EncoderArgs + @($SlicePath)
    & $FfmpegPath @SliceArgs
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $SlicePath)) {
      throw "Failed to create T7 event slice: $SlicePath"
    }
    & $FfmpegPath -y -hide_banner -loglevel error `
      -i $SlicePath `
      -vf "fps=4,scale=400:-1,tile=4x4" `
      -frames:v 1 $SliceContactSheetPath
    if ($LASTEXITCODE -ne 0 -or
        !(Test-Path -LiteralPath $SliceContactSheetPath)) {
      throw "Failed to create T7 event contact sheet: $SliceContactSheetPath"
    }
    $SliceManifests += [ordered]@{
      expected_step = $Specification.Step
      event_name = $Specification.Name
      authority_sample_step = [int]$Event.fixed_step
      observation_fixed_step = [int]$Event.observation_fixed_step
      event_offset_seconds = [Math]::Round($EventOffsetSeconds, 4)
      slice_start_seconds = [Math]::Round($SliceStartSeconds, 4)
      slice_duration_seconds = [Math]::Round($SliceDurationSeconds, 4)
      agent_id = [int]$Event.agent_id
      formation_index = [int]$Event.formation_index
      video = $SlicePath
      contact_sheet = $SliceContactSheetPath
    }
  }

  $PreviousErrorActionPreference = $ErrorActionPreference
  try {
    # Windows PowerShell promotes native stderr to ErrorRecord when the global
    # preference is Stop. ffmpeg writes probe/filter diagnostics to stderr even
    # on success, so collect it under Continue and validate the native exit code.
    $ErrorActionPreference = "Continue"
    $FreezeOutput = (
      & $FfmpegPath -hide_banner -loglevel info -i $VideoPath `
        -vf "freezedetect=n=0.003:d=2.0" -an -f null NUL 2>&1 |
        Out-String)
    $FreezeExitCode = $LASTEXITCODE
  }
  finally {
    $ErrorActionPreference = $PreviousErrorActionPreference
  }
  if ($FreezeExitCode -ne 0) {
    throw "T7 ffmpeg freeze diagnostic failed. exit_code=$FreezeExitCode"
  }
  $FreezeDurations = @(
    [regex]::Matches($FreezeOutput, "freeze_duration:\s*([0-9.]+)") |
      ForEach-Object {
        [double]::Parse(
          $_.Groups[1].Value,
          [Globalization.CultureInfo]::InvariantCulture)
      }
  )
  $MaxFreezeDurationSeconds = 0.0
  if ($FreezeDurations.Count -gt 0) {
    $MaxFreezeDurationSeconds = (
      $FreezeDurations | Measure-Object -Maximum).Maximum
  }
  if ($MaxFreezeDurationSeconds -ge 5.0) {
    throw "T7 video contains a gross freeze of $MaxFreezeDurationSeconds seconds."
  }

  $PreRoundRows = @($StateRows | Where-Object {
    $_.pre_round_sample -eq 1
  })
  $SampleEdgeRows = @($StateRows | Where-Object {
    $_.sample_edge_tolerance -eq 1
  })
  $MismatchRows = @($StateRows | Where-Object {
    $_.match -eq 0 -and $_.pre_round_sample -ne 1
  })
  $AcceptanceManifest = [ordered]@{
    version = 1
    scenario = "T7"
    capture_start_utc_ticks = $CaptureStartUtcTicks
    video = $VideoPath
    state_sidecar = $StateSidecarPath
    observed_formation_count = $ObservedFormationCount
    state_event_count = $StateRows.Count
    pre_round_transition_count = $PreRoundRows.Count
    sample_edge_transition_count = $SampleEdgeRows.Count
    mismatch_transition_count = $MismatchRows.Count
    freeze_event_count = $FreezeDurations.Count
    max_freeze_duration_seconds =
      [Math]::Round([double]$MaxFreezeDurationSeconds, 4)
    slices = $SliceManifests
  }
  $AcceptanceManifest |
    ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $AcceptanceManifestPath -Encoding utf8
  Write-Host "[CrowdDemoCapture] T7 sidecar=$StateSidecarPath"
  Write-Host "[CrowdDemoCapture] T7 manifest=$AcceptanceManifestPath"
  Write-Host "[CrowdDemoCapture] T7 slices=3 max_freeze_seconds=$MaxFreezeDurationSeconds mismatches=$($MismatchRows.Count)"
}
