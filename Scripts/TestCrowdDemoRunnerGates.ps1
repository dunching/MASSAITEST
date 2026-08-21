$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "CrowdDemoRunnerGates.ps1")

function Assert-RunnerGateTest {
  param(
    [bool]$Condition,
    [string]$Message
  )
  if (!$Condition) {
    throw "Runner gate test failed: $Message"
  }
}

function Assert-RunnerGateThrows {
  param(
    [scriptblock]$Action,
    [string]$Message
  )
  $Threw = $false
  try {
    & $Action
  }
  catch {
    $Threw = $true
  }
  Assert-RunnerGateTest $Threw $Message
}

$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
  ("CrowdDemoRunnerGate_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TestRoot | Out-Null
try {
  $CleanLog = Join-Path $TestRoot "clean.log"
  Set-Content -LiteralPath $CleanLog -Value @(
    "LogTemp: Display: clean",
    "LogTemp: Display: CrowdWorkerTargetCheckpoint role=server round_id=1 valid=1 fixed_step=1199 generation=1 worker_epoch=1200 input_sequence=1265 publish_sequence=1200 target_revision=1 objective_revision_match=1 expected_target_agent_count=20 target_agent_count=20 valid_target_state_count=20 cohort_count=1 plan_unrouted_agent_count=0 unrouted_target_state_count=0 total_feasible_capacity=224 assignable_population=20 overflow_population=0 capacity_hold_target_state_count=0 worker_state_hash=123 source=WorkerResultApply")
  Assert-RunnerGateTest `
    (@(Get-CrowdDemoHardFailures @($CleanLog)).Count -eq 0) `
    "clean log has no hard failures"
  $Metrics = Assert-CrowdDemoWorkerTargetGate $CleanLog 20
  Assert-RunnerGateTest ($Metrics.fixed_step -eq '1199') `
    "valid Worker Target checkpoint parses"

  $RejectCases = @(
    "LogTemp: Warning: CrowdWorkerRuntimeV2Failed stage=resume_before_input failure=5",
    "LogTemp: Error: CrowdWorkerTargetDemandRejected fixed_step=886 cohort=0",
    "LogTemp: Error: CrowdWorkerTargetDomainRejected stage=demand fixed_step=886",
    "LogTemp: Error: CrowdWorkerTargetDomainRejected stage=plan fixed_step=886",
    "LogTemp: Error: CrowdWorkerTargetDomainRejected stage=guidance fixed_step=886",
    "LogTemp: Error: CrowdWorkerDomainExecutionRejected domain=3 shard=0")
  for ($Index = 0; $Index -lt $RejectCases.Count; ++$Index) {
    $RejectLog = Join-Path $TestRoot "reject_$Index.log"
    Set-Content -LiteralPath $RejectLog -Value $RejectCases[$Index]
    Assert-RunnerGateTest `
      (@(Get-CrowdDemoHardFailures @($RejectLog)).Count -eq 1) `
      "Worker Target rejection case $Index is a hard failure"
  }

  $InvalidLog = Join-Path $TestRoot "invalid.log"
  Set-Content -LiteralPath $InvalidLog -Value `
    "LogTemp: Display: CrowdWorkerTargetCheckpoint role=server valid=0 objective_revision_match=0 expected_target_agent_count=20 target_agent_count=19 valid_target_state_count=19 cohort_count=0 plan_unrouted_agent_count=1 unrouted_target_state_count=1 total_feasible_capacity=6 assignable_population=6 overflow_population=13 capacity_hold_target_state_count=13"
  Assert-RunnerGateThrows `
    { Assert-CrowdDemoWorkerTargetGate $InvalidLog 20 } `
    "invalid checkpoint fails"

  $MissingLog = Join-Path $TestRoot "missing.log"
  Set-Content -LiteralPath $MissingLog -Value "LogTemp: Display: no target checkpoint"
  Assert-RunnerGateThrows `
    { Assert-CrowdDemoWorkerTargetGate $MissingLog 20 } `
    "missing checkpoint fails"

  $ScenarioLog = Join-Path $TestRoot "scenario.log"
  Set-Content -LiteralPath $ScenarioLog -Value @(
    "LogTemp: Display: CrowdDemoT1Checkpoint role=server round_id=1 valid=1 phase=6 transitions=6 active=19 active_sequence=5,10,15,19,20,19 batches=3 inserted=15 removed=1 layer_max=3 influenced=20 insert_settle=15 post_remove_settle=15",
    "LogTemp: Display: CrowdDemoT2Checkpoint role=server round_id=1 valid=1 flow_approach_entered_count=20 transport_handoff_count=20 inside_effective_band_count=20 plan_unrouted_count=0 guidance_unrouted_count=0 transport_validation_failure_count=0 terminal_settled_count=20 flow_contract_violation_count=0 final_deadlock_agent_count=0",
    "LogTemp: Display: CrowdDemoT3Checkpoint role=server round_id=1 valid=1 center_crossed=10,10 completed=10,10 total_completed=20 final_deadlock=0 unreachable_samples=0 last_step=1170",
    "LogTemp: Display: CrowdDemoT4Checkpoint role=server round_id=1 valid=1 wall_passed=20 corridor_exited=20 completed=20 final_settled=20 final_deadlock=0 unreachable_samples=0 last_step=1170")
  foreach ($Scenario in @('T1', 'T2', 'T3', 'T4')) {
    $ScenarioMetrics = Assert-CrowdDemoScenarioAcceptanceGate `
      $ScenarioLog $Scenario 20
    Assert-RunnerGateTest ($ScenarioMetrics.valid -eq '1') `
      "$Scenario valid acceptance checkpoint passes"
  }

  $FalsePositiveT3Log = Join-Path $TestRoot "t3_false_positive.log"
  Set-Content -LiteralPath $FalsePositiveT3Log -Value `
    "LogTemp: Display: CrowdDemoT3Checkpoint role=server round_id=1 valid=1 center_crossed=0,0 completed=0,0 total_completed=0 final_deadlock=0 unreachable_samples=0 last_step=-1"
  Assert-RunnerGateThrows `
    { Assert-CrowdDemoScenarioAcceptanceGate $FalsePositiveT3Log T3 20 } `
    "T3 zero-progress checkpoint cannot pass"

  $T6BLog = Join-Path $TestRoot "t6b.log"
  $T6BLines = @(
    "LogTemp: Display: CrowdDemoTargetRegionTransportCheckpoint role=server round_id=1 valid=1 inside_band=20 plan_unrouted=0",
    "LogTemp: Display: CrowdDemoT6TargetCheckpoint role=server round_id=1 testcase=8 valid=1 capability_profiles=7 cross_profile_hard=0 cross_profile_swept=0")
  for ($Index = 0; $Index -lt 7; ++$Index) {
    $Radius = @(30, 42, 60)[$Index % 3]
    $Agents = if ($Index -eq 6) { 2 } else { 3 }
    $T6BLines += "LogTemp: Display: CrowdDemoT6TargetProfileCheckpoint role=server round_id=1 profile=$Index agents=$Agents radius_cm=$Radius mobility=1.0 hard_gap_cm=10.0 soft_margin_cm=17.0 feasible_cells=10 feasible_regions=10 coverage=$Agents terminal_population=$Agents total_capacity=10 desired=$Agents assignable=$Agents overflow=0 routed=0 unrouted=0 active_claims=0 completed_transitions=1 released_claims=0 capacity_holds=0 overbooked_cells=0"
  }
  Set-Content -LiteralPath $T6BLog -Value $T6BLines
  $T6BMetrics = Assert-CrowdDemoT6BStaticTargetGate $T6BLog 20 7
  Assert-RunnerGateTest ($T6BMetrics.valid -eq '1') `
    "T6-B complete profile acceptance passes"

  $T6BInvalidLog = Join-Path $TestRoot "t6b_invalid.log"
  $T6BLines[-1] = $T6BLines[-1] -replace 'overbooked_cells=0', 'overbooked_cells=1'
  Set-Content -LiteralPath $T6BInvalidLog -Value $T6BLines
  Assert-RunnerGateThrows `
    { Assert-CrowdDemoT6BStaticTargetGate $T6BInvalidLog 20 7 } `
    "T6-B overbooked profile cannot pass"

  $T6CLog = Join-Path $TestRoot "t6c.log"
  $T6CLines = @(
    "LogTemp: Display: CrowdDemoT6TargetCheckpoint role=server round_id=1 testcase=9 valid=1 capability_profiles=7 cross_profile_hard=0 cross_profile_swept=0",
    "LogTemp: Display: CrowdDemoTargetRegionTransportCheckpoint role=server round_id=1 valid=1 inside_band=20 plan_unrouted=0",
    "LogTemp: Display: CrowdWorkerTargetCheckpoint role=server round_id=1 valid=1 objective_revision_match=1 objective_resource_revision=31 objective_effective_fixed_step=11 target_moved=1 target_displacement_cm=80 expected_target_agent_count=20 target_agent_count=20 valid_target_state_count=20 unrouted_target_state_count=0 overbooked_cell_count=0 active_claim_count=1 completed_transition_count=14 released_claim_count=12",
    "LogTemp: Display: CrowdDemoParticleCheckpoint role=server hard_pair_violation_count=0 swept_pair_violation_count=0 obstacle_penetration_count=0 bounds_violation_count=0",
    "LogTemp: Display: CrowdDemoFullWorkerProductionFastPathCheckpoint objective_published=2",
    "LogTemp: Display: CrowdDemoFullWorkerProductionFastPathCheckpoint objective_published=301",
    "LogTemp: Display: CrowdDemoDynamicSharedFlowCheckpoint anchor_cell=2730 integration_rebuild_count=1",
    "LogTemp: Display: CrowdDemoDynamicSharedFlowCheckpoint anchor_cell=2716 integration_rebuild_count=37")
  for ($Index = 0; $Index -lt 7; ++$Index) {
    $Agents = if ($Index -eq 6) { 2 } else { 3 }
    $T6CLines += "LogTemp: Display: CrowdDemoT6TargetProfileCheckpoint role=server round_id=1 profile=$Index agents=$Agents total_capacity=10 assignable=$Agents overflow=0 capacity_holds=0 unrouted=0 overbooked_cells=0 released_claims=1 completed_transitions=1"
  }
  Set-Content -LiteralPath $T6CLog -Value $T6CLines
  $T6CMetrics = Assert-CrowdDemoT6CHeterogeneousMovingTargetGate $T6CLog 20 7
  Assert-RunnerGateTest ($T6CMetrics.valid -eq '1') `
    "T6-C moving acceptance passes"

  $T6CLines[3] = $T6CLines[3] -replace 'swept_pair_violation_count=0', 'swept_pair_violation_count=1'
  Set-Content -LiteralPath $T6CLog -Value $T6CLines
  Assert-RunnerGateThrows `
    { Assert-CrowdDemoT6CHeterogeneousMovingTargetGate $T6CLog 20 7 } `
    "T6-C swept particle violation cannot pass"

  Write-Host "CrowdDemo runner gate tests PASS 19/19"
}
finally {
  Remove-Item -LiteralPath $TestRoot -Recurse -Force
}
