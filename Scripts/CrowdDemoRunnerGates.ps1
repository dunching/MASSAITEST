$script:CrowdDemoHardFailurePattern =
  'Fatal error|Assertion failed|Ensure condition failed|LogWindows: Error|(?-i:\bVIOLATION\b)|CrowdWorkerRuntimeV2Failed\b|CrowdWorkerTargetDemandRejected\b|CrowdWorkerTargetDomainRejected\b|CrowdWorkerDomainExecutionRejected\b'

function Get-CrowdDemoHardFailures {
  param([string[]]$LogPaths)

  return @($LogPaths) |
    Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
    ForEach-Object {
      Select-String -Path $_ -Pattern $script:CrowdDemoHardFailurePattern
    }
}

function ConvertFrom-CrowdDemoGateMetricLine {
  param([string]$Line)

  $Metrics = @{}
  foreach ($Token in ($Line -split '\s+')) {
    if ($Token -match '^([^=]+)=([^=]+)$') {
      $Metrics[$Matches[1]] = $Matches[2]
    }
  }
  return $Metrics
}

function Assert-CrowdDemoWorkerTargetGate {
  param(
    [string]$ServerLog,
    [int]$ExpectedTargetAgentCount = -1
  )

  if (!(Test-Path -LiteralPath $ServerLog)) {
    throw "CrowdDemo Worker Target gate: server log missing"
  }
  $Match = Select-String -Path $ServerLog `
    -Pattern 'CrowdWorkerTargetCheckpoint role=server ' |
    Select-Object -Last 1
  if (!$Match) {
    throw "CrowdDemo Worker Target gate: checkpoint missing"
  }
  $Metrics = ConvertFrom-CrowdDemoGateMetricLine $Match.Line
  $Failures = [System.Collections.Generic.List[string]]::new()
  if ($Metrics.valid -ne '1') {
    $Failures.Add("valid=$($Metrics.valid)")
  }
  if ($Metrics.objective_revision_match -ne '1') {
    $Failures.Add(
      "objective_revision_match=$($Metrics.objective_revision_match)")
  }
  if ([int]$Metrics.cohort_count -le 0) {
    $Failures.Add("cohort_count=$($Metrics.cohort_count)")
  }
  if ([int]$Metrics.unrouted_target_state_count -ne 0) {
    $Failures.Add(
      "unrouted_target_state_count=$($Metrics.unrouted_target_state_count)")
  }
  if ([int]$Metrics.plan_unrouted_agent_count -ne 0) {
    $Failures.Add(
      "plan_unrouted_agent_count=$($Metrics.plan_unrouted_agent_count)")
  }
  $DesiredPopulation = [int]$Metrics.target_agent_count
  $AssignablePopulation = [int]$Metrics.assignable_population
  $OverflowPopulation = [int]$Metrics.overflow_population
  $TotalFeasibleCapacity = [int]$Metrics.total_feasible_capacity
  $CapacityHoldCount = [int]$Metrics.capacity_hold_target_state_count
  if ($AssignablePopulation + $OverflowPopulation -ne $DesiredPopulation) {
    $Failures.Add(
      "target_capacity_conservation=$AssignablePopulation+$OverflowPopulation!=$DesiredPopulation")
  }
  if ($AssignablePopulation -gt $TotalFeasibleCapacity) {
    $Failures.Add(
      "assignable_population=$AssignablePopulation>total_feasible_capacity=$TotalFeasibleCapacity")
  }
  if ($CapacityHoldCount -ne $OverflowPopulation) {
    $Failures.Add(
      "capacity_hold_target_state_count=$CapacityHoldCount!=overflow_population=$OverflowPopulation")
  }
  if ($ExpectedTargetAgentCount -ge 0) {
    if ([int]$Metrics.expected_target_agent_count `
        -ne $ExpectedTargetAgentCount) {
      $Failures.Add(
        "expected_target_agent_count=$($Metrics.expected_target_agent_count)!=$ExpectedTargetAgentCount")
    }
    if ([int]$Metrics.target_agent_count `
        -ne $ExpectedTargetAgentCount) {
      $Failures.Add(
        "target_agent_count=$($Metrics.target_agent_count)!=$ExpectedTargetAgentCount")
    }
    if ([int]$Metrics.valid_target_state_count `
        -ne $ExpectedTargetAgentCount) {
      $Failures.Add(
        "valid_target_state_count=$($Metrics.valid_target_state_count)!=$ExpectedTargetAgentCount")
    }
  }
  if ($Failures.Count -gt 0) {
    throw "CrowdDemo Worker Target gate failed: $($Failures -join '; ')"
  }
  return $Metrics
}

function Assert-CrowdDemoScenarioAcceptanceGate {
  param(
    [string]$ServerLog,
    [ValidateSet('T1', 'T2', 'T3', 'T4')]
    [string]$Scenario,
    [int]$ExpectedAgentCount = 20
  )

  if (!(Test-Path -LiteralPath $ServerLog)) {
    throw "CrowdDemo $Scenario acceptance gate: server log missing"
  }
  $Match = Select-String -Path $ServerLog `
    -Pattern "CrowdDemo${Scenario}Checkpoint role=server " |
    Select-Object -Last 1
  if (!$Match) {
    throw "CrowdDemo $Scenario acceptance gate: checkpoint missing"
  }
  $Metrics = ConvertFrom-CrowdDemoGateMetricLine $Match.Line
  $Failures = [System.Collections.Generic.List[string]]::new()
  if ($Metrics.valid -ne '1') {
    $Failures.Add("valid=$($Metrics.valid)")
  }
  switch ($Scenario) {
    'T1' {
      if ([int]$Metrics.phase -ne 6) {
        $Failures.Add("phase=$($Metrics.phase)")
      }
      if ([int]$Metrics.inserted -le 0 -or
          [int]$Metrics.removed -le 0) {
        $Failures.Add(
          "inserted/removed=$($Metrics.inserted)/$($Metrics.removed)")
      }
      if ([int]$Metrics.insert_settle -le 0 -or
          [int]$Metrics.post_remove_settle -le 0) {
        $Failures.Add(
          "settle=$($Metrics.insert_settle)/$($Metrics.post_remove_settle)")
      }
    }
    'T2' {
      foreach ($Name in @(
        'flow_approach_entered_count',
        'transport_handoff_count',
        'inside_effective_band_count',
        'terminal_settled_count')) {
        if ([int]$Metrics[$Name] -ne $ExpectedAgentCount) {
          $Failures.Add("$Name=$($Metrics[$Name])")
        }
      }
      foreach ($Name in @(
        'plan_unrouted_count', 'guidance_unrouted_count',
        'transport_validation_failure_count',
        'flow_contract_violation_count',
        'final_deadlock_agent_count')) {
        if ([int]$Metrics[$Name] -ne 0) {
          $Failures.Add("$Name=$($Metrics[$Name])")
        }
      }
    }
    'T3' {
      $ExpectedCohortCount = [int]($ExpectedAgentCount / 2)
      if ($Metrics.center_crossed -ne
          "$ExpectedCohortCount,$ExpectedCohortCount") {
        $Failures.Add("center_crossed=$($Metrics.center_crossed)")
      }
      if ($Metrics.completed -ne
          "$ExpectedCohortCount,$ExpectedCohortCount") {
        $Failures.Add("completed=$($Metrics.completed)")
      }
      if ([int]$Metrics.total_completed -ne $ExpectedAgentCount) {
        $Failures.Add("total_completed=$($Metrics.total_completed)")
      }
      if ([int]$Metrics.final_deadlock -ne 0 -or
          [int]$Metrics.unreachable_samples -ne 0 -or
          [int]$Metrics.last_step -lt 0) {
        $Failures.Add(
          "deadlock/unreachable/last=$($Metrics.final_deadlock)/$($Metrics.unreachable_samples)/$($Metrics.last_step)")
      }
    }
    'T4' {
      foreach ($Name in @(
        'wall_passed', 'corridor_exited', 'completed',
        'final_settled')) {
        if ([int]$Metrics[$Name] -ne $ExpectedAgentCount) {
          $Failures.Add("$Name=$($Metrics[$Name])")
        }
      }
      if ([int]$Metrics.final_deadlock -ne 0 -or
          [int]$Metrics.unreachable_samples -ne 0 -or
          [int]$Metrics.last_step -lt 0) {
        $Failures.Add(
          "deadlock/unreachable/last=$($Metrics.final_deadlock)/$($Metrics.unreachable_samples)/$($Metrics.last_step)")
      }
    }
  }
  if ($Failures.Count -gt 0) {
    throw "CrowdDemo $Scenario acceptance gate failed: $($Failures -join '; ')"
  }
  return $Metrics
}
