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
  [int]$ServerWarmupSeconds = 20,
  [int]$ClientHoldSeconds = 4,
  [bool]$RequireClientReady = $true,
  [ValidateSet("P0", "P1")]
  [string]$Sf3Profile = "P0",
  [switch]$Sf3DeterminismDiagnostic,
  [switch]$Sf3PortalDiagnostic,
  [switch]$Sf4IngressDiagnostic,
  [switch]$Sf4ReservationOrcaDiagnostic,
  [switch]$Sf4ObstacleConstraintDiagnostic,
  [switch]$TransitJointDiagnostic,
  [switch]$TransitCapacityShadow,
  [switch]$ElasticCrowdShadow,
  [switch]$ParticleConstraintDiagnostic,
  [switch]$SoftPressureRouteDiagnostic,
  [switch]$TargetInfluenceExecutionDiagnostic,
  [switch]$TargetStabilityDiagnostic,
  [switch]$TargetRegionTransportDiagnostic,
  [switch]$RequireParticleCorrectionReplay,
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
if ($Sf3Profile -eq "P1") {
  $CommonArgs = "$CommonArgs -CrowdDemoSf3ProfileP1"
}
if ($Sf3DeterminismDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoSf3DeterminismDiagnostic"
}
if ($Sf3PortalDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoSf3PortalDiagnostic"
}
if ($Sf4IngressDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoSf4IngressDiagnostic"
}
if ($Sf4ReservationOrcaDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoSf4ReservationOrcaDiagnostic"
}
if ($Sf4ObstacleConstraintDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoSf4ObstacleConstraintDiagnostic"
}
if ($TransitJointDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTransitJointDiagnostic -CrowdDemoTransitHardSafetyGapCm=0 -CrowdDemoTransitPreferredSpacingGapCm=0 -CrowdDemoTransitContextScaleQ15=0"
}
if ($TransitCapacityShadow) {
  $CommonArgs = "$CommonArgs -CrowdDemoTransitCapacityShadow"
}
if ($ElasticCrowdShadow) {
  $CommonArgs = "$CommonArgs -CrowdDemoElasticCrowdShadow"
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
if ($TargetInfluenceExecutionDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTargetInfluenceExecutionDiagnostic"
}
if ($TargetStabilityDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTargetStabilityDiagnostic"
}
if ($TargetRegionTransportDiagnostic) {
  $CommonArgs = "$CommonArgs -CrowdDemoTargetRegionTransportDiagnostic"
}
if ($InitialAliveCount -ge 0) {
  $CommonArgs = "$CommonArgs -CrowdDemoInitialAliveCount=$InitialAliveCount"
}
if (![string]::IsNullOrWhiteSpace($VisualMode)) {
  $CommonArgs = "$CommonArgs -CrowdDemoVisualMode=$VisualMode"
}

$ServerDiagnosticArgs = ""
if ($Sf4ReservationOrcaDiagnostic) {
  $FixturePath = Join-Path $LogDir "sf4_reservation_orca_fixture.json"
  $ServerDiagnosticArgs = " -CrowdDemoSf4FixtureOutput=`"$FixturePath`""
}
if ($TransitJointDiagnostic) {
  $TransitFixturePath = Join-Path $LogDir "crowd_transit_joint_fixture.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoTransitJointFixtureOutput=`"$TransitFixturePath`""
}
if ($TransitCapacityShadow) {
  $TransitCapacityFixturePath = Join-Path $LogDir "crowd_transit_capacity_failure_fixture.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoTransitCapacityFixtureOutput=`"$TransitCapacityFixturePath`""
}
if ($ElasticCrowdShadow) {
  $ElasticFixturePath = Join-Path $LogDir "elastic_crowd_failure_fixture.json"
  $ServerDiagnosticArgs = "$ServerDiagnosticArgs -CrowdDemoElasticCrowdFixtureOutput=`"$ElasticFixturePath`""
}
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
$ServerArgs = "`"$ProjectPath`" $Map -server -port=$Port -NullRHI -log -AbsLog=`"$ServerLog`" $CommonArgs$ServerDiagnosticArgs"
Write-Host "[CrowdDemo] Starting server: $ServerArgs"
$ServerProcess = Start-Process -FilePath $EditorPath -ArgumentList $ServerArgs -PassThru -WindowStyle Hidden
$EffectiveRunSeconds = if ($RunSeconds -gt 0) { $RunSeconds } else { $DurationSeconds }

try {
  if (!$NoClient) {
    Start-Sleep -Seconds $ServerWarmupSeconds
    $ClientArgs = "`"$ProjectPath`" 127.0.0.1:$Port -game -RenderOffScreen -ResX=1280 -ResY=720 -log -AbsLog=`"$ClientLog`" $CommonArgs"
    Write-Host "[CrowdDemo] Starting client: $ClientArgs"
    $ClientProcess = Start-Process -FilePath $EditorPath -ArgumentList $ClientArgs -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds ($EffectiveRunSeconds + $ClientHoldSeconds)
    if (!$ClientProcess.HasExited) {
      Stop-Process -Id $ClientProcess.Id -Force
    }
  }
  else {
    Start-Sleep -Seconds ($EffectiveRunSeconds + $ClientHoldSeconds)
  }
}
finally {
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
