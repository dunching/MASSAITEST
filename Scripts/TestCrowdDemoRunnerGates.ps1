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
    "LogTemp: Display: CrowdWorkerTargetCheckpoint role=server round_id=1 valid=1 fixed_step=1199 generation=1 worker_epoch=1200 input_sequence=1265 publish_sequence=1200 target_revision=1 objective_revision_match=1 expected_target_agent_count=20 target_agent_count=20 valid_target_state_count=20 cohort_count=1 plan_unrouted_agent_count=0 unrouted_target_state_count=0 worker_state_hash=123 source=WorkerResultApply")
  Assert-RunnerGateTest `
    (@(Get-CrowdDemoHardFailures @($CleanLog)).Count -eq 0) `
    "clean log has no hard failures"
  $Metrics = Assert-CrowdDemoWorkerTargetGate $CleanLog 20
  Assert-RunnerGateTest ($Metrics.fixed_step -eq '1199') `
    "valid Worker Target checkpoint parses"

  $RejectCases = @(
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
    "LogTemp: Display: CrowdWorkerTargetCheckpoint role=server valid=0 objective_revision_match=0 expected_target_agent_count=20 target_agent_count=19 valid_target_state_count=19 cohort_count=0 plan_unrouted_agent_count=1 unrouted_target_state_count=1"
  Assert-RunnerGateThrows `
    { Assert-CrowdDemoWorkerTargetGate $InvalidLog 20 } `
    "invalid checkpoint fails"

  $MissingLog = Join-Path $TestRoot "missing.log"
  Set-Content -LiteralPath $MissingLog -Value "LogTemp: Display: no target checkpoint"
  Assert-RunnerGateThrows `
    { Assert-CrowdDemoWorkerTargetGate $MissingLog 20 } `
    "missing checkpoint fails"

  Write-Host "CrowdDemo runner gate tests PASS 9/9"
}
finally {
  Remove-Item -LiteralPath $TestRoot -Recurse -Force
}
