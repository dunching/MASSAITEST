param(
  [string]$EditorPath = "D:\UE\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe",
  [string]$ProjectPath = "",
  [string]$Map = "/Engine/Maps/Templates/OpenWorld",
  [int]$Port = 7901,
  [int]$EntityCount = 500,
  [string]$Scenario = "StaticTarget",
  [string]$VisualMode = "",
  [int]$InitialAliveCount = -1,
  [int]$DurationSeconds = 12,
  [int]$RunSeconds = 0,
  [int]$ResultWaitTimeoutSeconds = 90,
  [int]$ServerWarmupSeconds = 20,
  [int]$ClientHoldSeconds = 4,
  [bool]$RequireClientReady = $true,
  [switch]$ParticleConstraintDiagnostic,
  [switch]$SoftPressureRouteDiagnostic,
  [switch]$TargetInfluenceExecutionDiagnostic,
  [switch]$TargetStabilityDiagnostic,
  [switch]$TargetRegionTransportDiagnostic,
  [switch]$TargetRegionPlanLifecycleDiagnostic,
  [switch]$RequireParticleCorrectionReplay,
  [switch]$RequirePerformanceGate,
  [double]$MaxFixedStepP95Ms = 33.333,
  [double]$MinSimulationRealtimeFactor = 0.95,
  [double]$MaxClientFrameP95Ms = 33.333,
  [double]$MaxVisualProcessorP95Ms = 16.667,
  [double]$MaxCollapsedStepsP95 = 1.0,
  [switch]$NoClient
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
  $ProjectPath = Join-Path $Root "MassAICrowdDemo.uproject"
}

if (!(Test-Path -LiteralPath $EditorPath)) {
  throw "UnrealEditor not found: $EditorPath"
}
if (!(Test-Path -LiteralPath $ProjectPath)) {
  throw "Project not found: $ProjectPath"
}

$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$RunId = "CrowdDemo_${Port}_${Stamp}"
$LogDir = Join-Path $Root "Saved\CrowdDemo\$RunId"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$ServerLog = Join-Path $LogDir "server.log"
$ClientLog = Join-Path $LogDir "client.log"
$CommonArgs = "-CrowdDemoEntityCount=$EntityCount -CrowdDemoScenario=$Scenario -CrowdDemoDurationSeconds=$DurationSeconds -unattended -NoSound"
if ($RequireClientReady -and !$NoClient) {
  $CommonArgs = "$CommonArgs -CrowdDemoRequireClientReady -CrowdDemoReadyLeadSeconds=3 -CrowdDemoReadyTimeoutSeconds=60"
}
if ($RequireParticleCorrectionReplay) {
  $CommonArgs = "$CommonArgs -CrowdDemoRequireParticleCorrectionReplay"
}
if ($SoftPressureRouteDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoSoftPressureRouteDiagnostic"
}
if ($TargetInfluenceExecutionDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTargetInfluenceExecutionDiagnostic"
}
if ($TargetStabilityDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTargetStabilityDiagnostic"
}
if ($TargetRegionTransportDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTargetRegionTransportDiagnostic"
}
if ($TargetRegionPlanLifecycleDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTargetRegionPlanLifecycleDiagnostic"
}
if ($InitialAliveCount -ge 0) {
  $CommonArgs = "$CommonArgs -CrowdDemoInitialAliveCount=$InitialAliveCount"
}
if (![string]::IsNullOrWhiteSpace($VisualMode)) {
  $CommonArgs = "$CommonArgs -CrowdDemoVisualMode=$VisualMode"
}

$ServerDiagnosticArgs = ""
if ($ParticleConstraintDiagnostic) {
  $ParticleFixturePath = Join-Path $LogDir "particle_constraint_failure_fixture.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoParticleFixtureOutput=`"$ParticleFixturePath`""
}
if ($TargetInfluenceExecutionDiagnostic) {
  $TargetExecutionDiagnosticPath = Join-Path $LogDir "target_influence_execution_diagnostic.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoTargetInfluenceExecutionDiagnosticOutput=`"$TargetExecutionDiagnosticPath`""
}
if ($TargetStabilityDiagnostic) {
  $LocalPredictiveFixturePath = Join-Path $LogDir "local_predictive_component_fixture.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoLocalPredictiveFixtureOutput=`"$LocalPredictiveFixturePath`""
}
if ($TargetRegionTransportDiagnostic) {
  $TargetRegionTransportDiagnosticPath = Join-Path $LogDir "target_region_transport_failure_fixture.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoTargetRegionTransportDiagnosticOutput=`"$TargetRegionTransportDiagnosticPath`""
}
if ($TargetRegionPlanLifecycleDiagnostic) {
  $TargetRegionPlanLifecyclePath = Join-Path $LogDir "target_region_plan_lifecycle_fixture.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoTargetRegionPlanLifecycleDiagnosticOutput=`"$TargetRegionPlanLifecyclePath`""
}
$ServerArgs = "`"$ProjectPath`" $Map -server -port=$Port -NullRHI -log -AbsLog=`"$ServerLog`" $CommonArgs$ServerDiagnosticArgs"
Write-Host "[CrowdDemo] Starting server: $ServerArgs"
$ServerProcess = Start-Process -FilePath $EditorPath -ArgumentList $ServerArgs -PassThru -WindowStyle Hidden
$EffectiveRunSeconds = if ($RunSeconds -gt 0) { $RunSeconds } else { $DurationSeconds }
$ClientProcess = $null

try {
  if (!$NoClient) {
    Start-Sleep -Seconds $ServerWarmupSeconds
    $ClientArgs = "`"$ProjectPath`" 127.0.0.1:$Port -game -RenderOffScreen -ResX=1280 -ResY=720 -log -AbsLog=`"$ClientLog`" $CommonArgs"
    Write-Host "[CrowdDemo] Starting client: $ClientArgs"
    $ClientProcess = Start-Process -FilePath $EditorPath -ArgumentList $ClientArgs -PassThru -WindowStyle Hidden
    if ($RequirePerformanceGate) {
      $Deadline = (Get-Date).AddSeconds([Math]::Max($ResultWaitTimeoutSeconds, $EffectiveRunSeconds))
      $ResultComplete = $false
      while ((Get-Date) -lt $Deadline -and !$ClientProcess.HasExited -and !$ServerProcess.HasExited) {
        $ServerComplete = (Test-Path -LiteralPath $ServerLog) -and
          [bool](Select-String -Path $ServerLog -Pattern "CrowdDemoPerformanceCheckpoint role=server" -Quiet)
        $ClientComplete = (Test-Path -LiteralPath $ClientLog) -and
          [bool](Select-String -Path $ClientLog -Pattern "CrowdDemoRoundInputCheckpoint role=client" -Quiet)
        if ($ServerComplete -and $ClientComplete) {
          $ResultComplete = $true
          break
        }
        Start-Sleep -Milliseconds 500
      }
      if (!$ResultComplete) {
        throw "CrowdDemo performance run timed out before server/client RoundResult completion"
      }
      Start-Sleep -Seconds $ClientHoldSeconds
    }
    else {
      Start-Sleep -Seconds ($EffectiveRunSeconds + $ClientHoldSeconds)
    }
    if (!$ClientProcess.HasExited) {
      Stop-Process -Id $ClientProcess.Id -Force
    }
  }
  else {
    if ($RequirePerformanceGate) {
      $Deadline = (Get-Date).AddSeconds([Math]::Max($ResultWaitTimeoutSeconds, $EffectiveRunSeconds))
      while ((Get-Date) -lt $Deadline -and !$ServerProcess.HasExited) {
        if ((Test-Path -LiteralPath $ServerLog) -and
          (Select-String -Path $ServerLog -Pattern "CrowdDemoPerformanceCheckpoint role=server" -Quiet)) {
          break
        }
        Start-Sleep -Milliseconds 500
      }
    }
    else {
      Start-Sleep -Seconds ($EffectiveRunSeconds + $ClientHoldSeconds)
    }
  }
}
finally {
  if ($null -ne $ClientProcess -and !$ClientProcess.HasExited) {
    Stop-Process -Id $ClientProcess.Id -Force
  }
  if (!$ServerProcess.HasExited) {
    Stop-Process -Id $ServerProcess.Id -Force
  }
}

Write-Host "[CrowdDemo] Logs: $LogDir"
if (Test-Path -LiteralPath $ServerLog) {
  Select-String -Path $ServerLog -Pattern "CrowdDemo:","CrowdDemoMass:","CrowdDemoSummary","CrowdDemoCorrectionFrame" -SimpleMatch | Select-Object -Last 20
}
if (Test-Path -LiteralPath $ClientLog) {
  Select-String -Path $ClientLog -Pattern "CrowdDemo:","CrowdDemoMass:","CrowdDemoSummary","CrowdDemoCorrectionFrame" -SimpleMatch | Select-Object -Last 20
}

function ConvertFrom-CrowdDemoMetricLine([string]$Line) {
  $Metrics = @{}
  foreach ($Token in ($Line -split '\s+')) {
    if ($Token -match '^([^=]+)=([^=]+)$') {
      $Metrics[$Matches[1]] = $Matches[2]
    }
  }
  return $Metrics
}

if ($RequirePerformanceGate) {
  $ServerPerformanceMatch = Select-String -Path $ServerLog -Pattern "CrowdDemoPerformanceCheckpoint role=server" | Select-Object -Last 1
  if (!$ServerPerformanceMatch) {
    throw "CrowdDemo performance gate: server performance checkpoint missing"
  }
  $ServerPerformance = ConvertFrom-CrowdDemoMetricLine $ServerPerformanceMatch.Line
  $Failures = [System.Collections.Generic.List[string]]::new()
  if ([double]$ServerPerformance.fixed_step_ms_p95 -gt $MaxFixedStepP95Ms) {
    $Failures.Add("fixed_step_ms_p95=$($ServerPerformance.fixed_step_ms_p95)>$MaxFixedStepP95Ms")
  }
  if ([double]$ServerPerformance.simulation_realtime_factor -lt $MinSimulationRealtimeFactor) {
    $Failures.Add("simulation_realtime_factor=$($ServerPerformance.simulation_realtime_factor)<$MinSimulationRealtimeFactor")
  }
  if ([int]$ServerPerformance.max_step_limit_hit_count -ne 0) {
    $Failures.Add("max_step_limit_hit_count=$($ServerPerformance.max_step_limit_hit_count)")
  }
  if (!$NoClient) {
    $VisualPerformanceMatch = Select-String -Path $ClientLog -Pattern "CrowdDemoVisualPerformance role=client" | Select-Object -Last 1
    if (!$VisualPerformanceMatch) {
      $Failures.Add("client visual performance checkpoint missing")
    }
    else {
      $VisualPerformance = ConvertFrom-CrowdDemoMetricLine $VisualPerformanceMatch.Line
      if ([double]$VisualPerformance.client_frame_ms_p95 -gt $MaxClientFrameP95Ms) {
        $Failures.Add("client_frame_ms_p95=$($VisualPerformance.client_frame_ms_p95)>$MaxClientFrameP95Ms")
      }
      if ([double]$VisualPerformance.visual_processor_ms_p95 -gt $MaxVisualProcessorP95Ms) {
        $Failures.Add("visual_processor_ms_p95=$($VisualPerformance.visual_processor_ms_p95)>$MaxVisualProcessorP95Ms")
      }
      if ([double]$VisualPerformance.collapsed_steps_p95 -gt $MaxCollapsedStepsP95) {
        $Failures.Add("collapsed_steps_p95=$($VisualPerformance.collapsed_steps_p95)>$MaxCollapsedStepsP95")
      }
      if ([int]$VisualPerformance.non_correction_discontinuity_count -ne 0) {
        $Failures.Add("non_correction_discontinuity_count=$($VisualPerformance.non_correction_discontinuity_count)")
      }
    }
  }
  $ViolationMatches = @($ServerLog, $ClientLog) | Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object {
      Select-String -Path $_ -Pattern 'Fatal error|Assertion failed|Ensure condition failed|LogWindows: Error|(?-i:\bVIOLATION\b)'
    }
  if ($ViolationMatches.Count -gt 0) {
    $Failures.Add("fatal/assert/ensure/error/violation log count=$($ViolationMatches.Count)")
  }
  if ($Failures.Count -gt 0) {
    throw "CrowdDemo performance gate failed: $($Failures -join '; ')"
  }
  Write-Host "[CrowdDemo] Performance gate passed: fixed_step_p95=$($ServerPerformance.fixed_step_ms_p95)ms realtime=$($ServerPerformance.simulation_realtime_factor)"
}
