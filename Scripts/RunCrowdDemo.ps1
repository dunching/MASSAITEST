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
  [switch]$TargetStabilityDiagnostic,
  [switch]$TargetRegionTransportDiagnostic,
  [switch]$TargetRegionPlanLifecycleDiagnostic,
  [switch]$DrawTargetAcceptanceMarkers,
  [switch]$RequireParticleCorrectionReplay,
  [switch]$RequirePerformanceGate,
  [switch]$ContinuousLifecycle,
  [double]$ContinuousStartDelaySeconds = 5.0,
  [switch]$NavSurfaceGraph,
  [switch]$NavFlowProductSmall,
  [switch]$FriendlyLogisticsSmall,
  [switch]$MixedSandbox,
  [switch]$MixedCombatIntegration,
  [switch]$RangedProjectileGolden,
  [double]$MaxFixedStepP95Ms = 33.333,
  [double]$MinSimulationRealtimeFactor = 0.95,
  [double]$MaxClientFrameP95Ms = 33.333,
  [double]$MaxVisualProcessorP95Ms = 16.667,
  [double]$MaxCollapsedStepsP95 = 1.0,
  [string]$ClientExtraArgs = "",
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
if ($MixedCombatIntegration -and
  $Map -eq "/Engine/Maps/Templates/OpenWorld") {
  $Map = "/Game/Maps/CrowdDemo_NavSurfaceGraphVerticalSmall"
}

$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$RunId = "CrowdDemo_${Port}_${Stamp}"
$LogDir = Join-Path $Root "Saved\CrowdDemo\$RunId"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$ServerLog = Join-Path $LogDir "server.log"
$ClientLog = Join-Path $LogDir "client.log"
$CommonArgs = "-CrowdDemoEntityCount=$EntityCount -CrowdDemoScenario=$Scenario -CrowdDemoDurationSeconds=$DurationSeconds -unattended -NoSound"
if ($ContinuousLifecycle) {
  $CommonArgs = "$CommonArgs -CrowdDemoContinuousLifecycle -CrowdDemoContinuousStartDelay=$ContinuousStartDelaySeconds"
}
if ($NavSurfaceGraph) {
  $CommonArgs = "$CommonArgs -CrowdDemoNavSurfaceGraph"
}
if ($NavFlowProductSmall) {
  $CommonArgs = "$CommonArgs -CrowdDemoNavFlowProductSmall"
}
if ($FriendlyLogisticsSmall) {
  $CommonArgs = "$CommonArgs -CrowdDemoFriendlyLogisticsSmall"
}
if ($MixedSandbox) {
  $CommonArgs = "$CommonArgs -CrowdDemoMixedSandbox"
}
if ($MixedCombatIntegration) {
  $CommonArgs = "$CommonArgs -CrowdDemoMixedCombatIntegration"
}

if ($RequireClientReady -and !$NoClient) {
  $CommonArgs = "$CommonArgs -CrowdDemoRequireClientReady -CrowdDemoReadyLeadSeconds=3 -CrowdDemoReadyTimeoutSeconds=60"
}
if ($RequireParticleCorrectionReplay) {
  $CommonArgs = "$CommonArgs -CrowdDemoRequireParticleCorrectionReplay"
}
if ($SoftPressureRouteDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoSoftPressureRouteDiagnostic"
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
if ($DrawTargetAcceptanceMarkers) {
  $CommonArgs = "$CommonArgs -CrowdDemoDrawTargetAcceptanceMarkers"
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
    $ClientArgs = "`"$ProjectPath`" 127.0.0.1:$Port -game -RenderOffScreen -ResX=1280 -ResY=720 -log -AbsLog=`"$ClientLog`" $CommonArgs $ClientExtraArgs"
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

$HardFailures = @($ServerLog, $ClientLog) |
  Where-Object { Test-Path -LiteralPath $_ } |
  ForEach-Object {
    Select-String -Path $_ `
      -Pattern 'Fatal error|Assertion failed|Ensure condition failed|LogWindows: Error|(?-i:\bVIOLATION\b)'
  }
if ($HardFailures.Count -gt 0) {
  $FirstFailure = $HardFailures | Select-Object -First 1
  throw "CrowdDemo hard failure gate failed: count=$($HardFailures.Count) first=$($FirstFailure.Path):$($FirstFailure.LineNumber) $($FirstFailure.Line)"
}

if ($RangedProjectileGolden) {
  $ExpectedAttackHash = "41852579"
  $ExpectedProjectileHash = "488896174"
  $ExpectedEventHash = "4204062592"
  $ServerProjectile = Select-String -Path $ServerLog `
    -Pattern "CrowdDemoProjectileCheckpoint role=server round_id=1 " |
    Select-Object -Last 1
  $ClientProjectile = if ($NoClient) { $null } else {
    Select-String -Path $ClientLog `
      -Pattern "CrowdDemoProjectileCheckpoint role=client round_id=1 " |
      Select-Object -Last 1
  }
  $ServerGolden = $ServerProjectile -and
    $ServerProjectile.Line -match 'valid=1\b' -and
    $ServerProjectile.Line -match 'acquired=50\b' -and
    $ServerProjectile.Line -match 'windup=50\b' -and
    $ServerProjectile.Line -match 'spawned=50\b' -and
    $ServerProjectile.Line -match 'active=0\b' -and
    $ServerProjectile.Line -match 'impacted=50\b' -and
    $ServerProjectile.Line -match 'expired=0\b' -and
    $ServerProjectile.Line -match 'duplicate_fire=0\b' -and
    $ServerProjectile.Line -match 'duplicate_hit=0\b' -and
    $ServerProjectile.Line -match 'damage=50\b' -and
    $ServerProjectile.Line -match "attack_hash=$ExpectedAttackHash\b" -and
    $ServerProjectile.Line -match "projectile_hash=$ExpectedProjectileHash\b" -and
    $ServerProjectile.Line -match "event_hash=$ExpectedEventHash\b"
  $ClientGolden = $NoClient -or ($ClientProjectile -and
    $ClientProjectile.Line -match 'valid=1/1\b' -and
    $ClientProjectile.Line -match 'spawned=50/50\b' -and
    $ClientProjectile.Line -match 'impacted=50/50\b' -and
    $ClientProjectile.Line -match 'damage=50/50\b' -and
    $ClientProjectile.Line -match "attack_hash=$ExpectedAttackHash/$ExpectedAttackHash\b" -and
    $ClientProjectile.Line -match "projectile_hash=$ExpectedProjectileHash/$ExpectedProjectileHash\b" -and
    $ClientProjectile.Line -match "event_hash=$ExpectedEventHash/$ExpectedEventHash\b" -and
    $ClientProjectile.Line -match 'match=1\b')
  if (!$ServerGolden -or !$ClientGolden) {
    throw "CrowdDemo ranged projectile golden gate failed: server=$([bool]$ServerGolden) client=$([bool]$ClientGolden) expected=$ExpectedAttackHash/$ExpectedProjectileHash/$ExpectedEventHash"
  }
  Write-Host "[CrowdDemo] Ranged projectile golden gate passed: attack/projectile/event=$ExpectedAttackHash/$ExpectedProjectileHash/$ExpectedEventHash"
}

if ($MixedSandbox) {
  $ServerPass = Select-String -Path $ServerLog -Pattern "PASS CrowdDemoMixedSandbox role=server" | Select-Object -Last 1
  $ClientPass = if ($NoClient) { $true } else {
    Select-String -Path $ClientLog -Pattern "PASS CrowdDemoMixedSandbox role=client" | Select-Object -Last 1
  }
  $MixedViolations = @($ServerLog, $ClientLog) | Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object {
      Select-String -Path $_ -Pattern 'Fatal error|Assertion failed|Ensure condition failed|LogWindows: Error|(?-i:\bVIOLATION\b)'
    }
  $ExpectedProjectiles = [Math]::Max(1, [int]($EntityCount / 5))
  $ProjectileReady = $ServerPass -and
    $ServerPass.Line -match "projectile_expected=$ExpectedProjectiles\b" -and
    $ServerPass.Line -match "projectile_spawned=$ExpectedProjectiles\b" -and
    $ServerPass.Line -match "projectile_impacted=$ExpectedProjectiles\b" -and
    $ServerPass.Line -match "projectile_damage=$ExpectedProjectiles\b" -and
    $ServerPass.Line -match 'projectile_duplicate=0\b' -and
    $ServerPass.Line -match 'projectile_hash=(\d+)'
  $ProjectileHash = if ($ProjectileReady) { $Matches[1] } else { "" }
  $ClientProjectileReady = $NoClient -or ($ClientPass -and
    $ClientPass.Line -match "projectile_expected=$ExpectedProjectiles\b" -and
    $ClientPass.Line -match "projectile_spawned=$ExpectedProjectiles\b" -and
    $ClientPass.Line -match "projectile_impacted=$ExpectedProjectiles\b" -and
    $ClientPass.Line -match "projectile_damage=$ExpectedProjectiles\b" -and
    $ClientPass.Line -match 'projectile_duplicate=0\b' -and
    $ProjectileHash -ne "" -and
    $ClientPass.Line -match "projectile_hash=$ProjectileHash\b")
  if (!$ServerPass -or !$ClientPass -or !$ProjectileReady -or
    !$ClientProjectileReady -or $MixedViolations.Count -gt 0) {
    throw "CrowdDemo mixed sandbox gate failed: server_pass=$([bool]$ServerPass) client_pass=$([bool]$ClientPass) projectile=$([bool]$ProjectileReady) client_projectile=$([bool]$ClientProjectileReady) violations=$($MixedViolations.Count)"
  }
  Write-Host "[CrowdDemo] Mixed sandbox gate passed: projectiles=$ExpectedProjectiles projectile_hash=$ProjectileHash"
}

if ($MixedCombatIntegration) {
  $ServerPass = Select-String -Path $ServerLog -Pattern "PASS CrowdDemoMixedCombat role=server" | Select-Object -Last 1
  $ClientPass = if ($NoClient) { $true } else {
    Select-String -Path $ClientLog -Pattern "PASS CrowdDemoMixedCombat role=client" | Select-Object -Last 1
  }
  $ServerEntityHash = if ($ServerPass -and
    $ServerPass.Line -match 'entity_hash=(\d+)\b') {
    $Matches[1]
  } else { "" }
  $ServerMembershipHash = if ($ServerPass -and
    $ServerPass.Line -match 'membership_hash=(\d+)\b') {
    $Matches[1]
  } else { "" }
  $ServerP95 = if ($ServerPass -and
    $ServerPass.Line -match 'fixed_step_ms_p95=([0-9.]+)\b') {
    [double]$Matches[1]
  } else { [double]::PositiveInfinity }
  $ServerReady = $ServerPass -and
    $ServerPass.Line -match 'population=20\b' -and
    $ServerPass.Line -match 'melee_intent=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'midrange_intent=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'ranged_intent=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'impact=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'damage=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'death=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'target_switch=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'target_region_rebuild=([1-9]\d*)\b' -and
    $ServerPass.Line -match 'referenced_dead=0\b' -and
    $ServerPass.Line -match 'projectile_duplicate=0\b' -and
    $ServerPass.Line -match 'projectile_conserved=1\b' -and
    $ServerP95 -le 33.333
  $ClientReady = $NoClient -or ($ClientPass -and
    $ClientPass.Line -match 'population=20\b' -and
    $ClientPass.Line -match 'melee_intent=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'midrange_intent=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'ranged_intent=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'impact=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'damage=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'death=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'target_switch=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'target_region_rebuild=([1-9]\d*)\b' -and
    $ClientPass.Line -match 'projectile_duplicate=0\b' -and
    $ClientPass.Line -match 'projectile_conserved=1\b' -and
    $ServerEntityHash -ne "" -and
    $ClientPass.Line -match "entity_hash=$ServerEntityHash\b" -and
    $ServerMembershipHash -ne "" -and
    $ClientPass.Line -match "membership_hash=$ServerMembershipHash\b")
  if (!$ServerReady -or !$ClientReady) {
    throw "CrowdDemo mixed combat gate failed: server=$([bool]$ServerReady) client=$([bool]$ClientReady)"
  }
  Write-Host "[CrowdDemo] Mixed combat T9 gate passed"
}

if ($ContinuousLifecycle) {
  $ServerCheckpoint = Select-String -Path $ServerLog -Pattern "CrowdDemoContinuousLifecycleCheckpoint role=server" |
    Select-Object -Last 1
  $ClientCheckpoint = if ($NoClient) { $null } else {
    Select-String -Path $ClientLog -Pattern "CrowdDemoContinuousLifecycleCheckpoint role=client" |
      Select-Object -Last 1
  }
  $ServerOperations = @(Select-String -Path $ServerLog -Pattern "CrowdDemoContinuousLifecycle role=server stage=operation")
  $ClientOperations = if ($NoClient) { @() } else {
    @(Select-String -Path $ClientLog -Pattern "CrowdDemoContinuousLifecycle role=client stage=operation")
  }
  $ServerOperation = $ServerOperations | Select-Object -Last 1
  $ClientOperation = $ClientOperations | Select-Object -Last 1
  $ContinuousViolations = @($ServerLog, $ClientLog) | Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object {
      Select-String -Path $_ -Pattern 'Fatal error|Assertion failed|Ensure condition failed|LogWindows: Error|(?-i:\bVIOLATION\b)'
    }
  $ServerReady = $ServerCheckpoint -and $ServerOperation -and
    $ServerCheckpoint.Line -match 'max_population=20' -and
    $ServerCheckpoint.Line -match 'stale_reject=0' -and
    $ServerOperation.Line -match 'active=(19|20)'
  $ClientCheckpointActive = if ($ClientCheckpoint -and
    $ClientCheckpoint.Line -match 'active=(\d+)') { $Matches[1] } else { "" }
  $ClientReady = $NoClient -or ($ClientCheckpoint -and
    $ClientOperation -and
    $ClientCheckpointActive -ne "" -and
    $ClientCheckpoint.Line -match "visible=$ClientCheckpointActive" -and
    $ClientOperation.Line -match 'active=(19|20)' -and
    $ClientCheckpoint.Line -match 'stale_reject=0')
  $MatchedSequence = ""
  $MatchedHash = ""
  if (!$NoClient) {
    foreach ($ClientLine in ($ClientOperations | Select-Object -Last 32)) {
      if ($ClientLine.Line -match 'sequence=(\d+).+hash=(\d+)') {
        $CandidateSequence = $Matches[1]
        $CandidateHash = $Matches[2]
        $ServerMatch = $ServerOperations | Where-Object {
          $_.Line -match "sequence=$CandidateSequence\b" -and
          $_.Line -match "hash=$CandidateHash\b"
        } | Select-Object -Last 1
        if ($ServerMatch) {
          $MatchedSequence = $CandidateSequence
          $MatchedHash = $CandidateHash
        }
      }
    }
  }
  $HashReady = $NoClient -or ($MatchedSequence -ne "" -and $MatchedHash -ne "")
  if (!$ServerReady -or !$ClientReady -or !$HashReady -or $ContinuousViolations.Count -gt 0) {
    throw "CrowdDemo continuous lifecycle gate failed: server=$([bool]$ServerReady) client=$([bool]$ClientReady) hash=$([bool]$HashReady) violations=$($ContinuousViolations.Count)"
  }
  Write-Host "[CrowdDemo] Continuous lifecycle gate passed: sequence=$MatchedSequence entity_set_hash=$MatchedHash"
}

if ($NavFlowProductSmall) {
  $ProductPass = Select-String -Path $ServerLog `
    -Pattern "PASS CrowdDemoNavSurfaceGraph stage=validation.*product_small=1" |
    Select-Object -Last 1
  $BoundaryPass = Select-String -Path $ServerLog `
    -Pattern "CrowdDemoBoundaryTransaction step=" |
    Select-Object -Last 1
  $ProductViolations = @($ServerLog, $ClientLog) |
    Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object {
      Select-String -Path $_ `
        -Pattern 'Fatal error|Assertion failed|Ensure condition failed|LogWindows: Error|(?-i:\bVIOLATION\b)'
    }
  if (!$ProductPass -or !$BoundaryPass -or $ProductViolations.Count -gt 0) {
    throw "CrowdDemo NavFlowProductSmall gate failed: product=$([bool]$ProductPass) boundary=$([bool]$BoundaryPass) violations=$($ProductViolations.Count)"
  }
  Write-Host "[CrowdDemo] NavFlowProductSmall gate passed"
}

if ($FriendlyLogisticsSmall) {
  $ServerPass = Select-String -Path $ServerLog `
    -Pattern "PASS CrowdDemoFriendlyLogistics role=server" |
    Select-Object -Last 1
  $ClientPass = if ($NoClient) { $true } else {
    Select-String -Path $ClientLog `
      -Pattern "PASS CrowdDemoFriendlyLogistics role=client" |
      Select-Object -Last 1
  }
  $FriendlyViolations = @($ServerLog, $ClientLog) |
    Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object {
      Select-String -Path $_ `
        -Pattern 'Fatal error|Assertion failed|Ensure condition failed|LogWindows: Error|(?-i:\bVIOLATION\b)'
  }
  $HashMatch = $NoClient
  $CargoVisualReady = $NoClient
  if ((!$NoClient) -and $ServerPass -and $ClientPass -and
    ($ServerPass.Line -match 'state_hash=(\d+)')) {
    $ServerHash = $Matches[1]
    $HashMatch = $ClientPass.Line -match "state_hash=$ServerHash\b"
    $CargoVisualReady =
      $ClientPass.Line -match 'cargo_attach=([1-9]\d*)' -and
      $ClientPass.Line -match 'cargo_detach=([1-9]\d*)' -and
      $ClientPass.Line -match 'cargo_visible=0' -and
      $ClientPass.Line -match 'presentation_instances=20'
  }
  if ((!$ServerPass) -or (!$ClientPass) -or (!$HashMatch) -or
    (!$CargoVisualReady) -or
    ($FriendlyViolations.Count -gt 0)) {
    throw "CrowdDemo FriendlyLogisticsSmall gate failed: server=$([bool]$ServerPass) client=$([bool]$ClientPass) hash=$HashMatch cargo_visual=$CargoVisualReady violations=$($FriendlyViolations.Count)"
  }
  Write-Host "[CrowdDemo] FriendlyLogisticsSmall gate passed"
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
      $FramePhaseMatch = Select-String -Path $ClientLog -Pattern "CrowdDemoClientFramePhases role=client" | Select-Object -Last 1
      if (!$FramePhaseMatch) {
        $Failures.Add("client frame phase checkpoint missing")
      }
      else {
        $FramePhases = ConvertFrom-CrowdDemoMetricLine $FramePhaseMatch.Line
        Write-Host "[CrowdDemo] Client phases: game=$($FramePhases.game_ms_p95)ms render=$($FramePhases.render_ms_p95)ms gpu=$($FramePhases.gpu_ms_p95)ms shader_frames=$($FramePhases.shader_frames) async_frames=$($FramePhases.async_loading_frames)"
      }
      if ([double]$VisualPerformance.client_frame_ms_p95 -gt $MaxClientFrameP95Ms) {
        $PhaseSuffix = if ($FramePhaseMatch) {
          " game=$($FramePhases.game_ms_p95) render=$($FramePhases.render_ms_p95) gpu=$($FramePhases.gpu_ms_p95) shader_frames=$($FramePhases.shader_frames) async_frames=$($FramePhases.async_loading_frames)"
        } else { "" }
        $Failures.Add("client_frame_ms_p95=$($VisualPerformance.client_frame_ms_p95)>$MaxClientFrameP95Ms$PhaseSuffix")
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
