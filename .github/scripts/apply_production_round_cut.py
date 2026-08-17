from pathlib import Path

HEADER = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h")
PIPELINE = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp")
PROCESSORS = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp")
COMBAT_TESTS = Path("Source/MassAICrowdDemo/CrowdDemoCombatStateTests.cpp")
LOCAL_TESTS = Path("Source/MassAICrowdDemo/CrowdDemoLocalPredictiveInteractionTests.cpp")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


def remove_between(text: str, start: str, end: str, label: str) -> str:
    si = text.find(start)
    if si < 0:
        raise RuntimeError(f"{label}: start marker missing")
    ei = text.find(end, si + len(start))
    if ei < 0:
        raise RuntimeError(f"{label}: end marker missing")
    return text[:si] + text[ei:]


def remove_test(text: str, test_name: str) -> str:
    marker = f"IMPLEMENT_SIMPLE_AUTOMATION_TEST(\n  {test_name},"
    si = text.find(marker)
    if si < 0:
        raise RuntimeError(f"test {test_name}: marker missing")
    ei = text.find("IMPLEMENT_SIMPLE_AUTOMATION_TEST(", si + len(marker))
    if ei < 0:
        raise RuntimeError(f"test {test_name}: next test marker missing")
    return text[:si] + text[ei:]


# ---------------------------------------------------------------------------
# Slice 1: remove the old full-state SoftPressure rollback history/replay path.
# ---------------------------------------------------------------------------
header = read(HEADER)
header = remove_between(
    header,
    "struct FCrowdDemoSoftPressureRollbackAgentState\n{",
    "struct FCrowdDemoPreparedReactiveMotionStep\n{",
    "remove per-agent rollback payloads",
)
header = remove_between(
    header,
    "struct FCrowdDemoTargetRegionCapabilityCohortRollbackState\n{",
    "struct FCrowdDemoPreparedSteeringGuidance\n{",
    "remove full rollback snapshot structures",
)
header = remove_between(
    header,
    "  void RecordSoftPressureRollbackSnapshot(\n",
    "  void RecordSoftPressureRollbackOutcome(",
    "remove rollback history public API",
)
header = replace_exact(
    header,
    "  TMap<int32, FCrowdDemoSoftPressureRollbackSnapshot> SoftPressureRollbackHistory;\n",
    "",
    1,
    "remove rollback history member",
)

# Full Production fast-path API is intentionally adjacent to the retained
# legacy early-intent API. Bootstrap/Shadow/Canary keep the old path for now.
header = replace_exact(
    header,
    "  bool TrySubmitWorkerV2ClockIntentEarly();\n  ECrowdBoundaryPollResult TryPrepareRoundApply();",
    "  bool TrySubmitWorkerV2ClockIntentEarly();\n"
    "  bool CanUseFullWorkerProductionFastPath() const;\n"
    "  bool TrySubmitFullWorkerProductionIntent();\n"
    "  bool IsCurrentStepFullWorkerProductionFastPath() const\n"
    "  { return bCurrentStepFullWorkerProductionFastPath; }\n"
    "  uint64 GetCurrentStepFullWorkerInputSequence() const\n"
    "  { return CurrentStepFullWorkerInputSequence; }\n"
    "  bool MarkFullWorkerProductionResultCommitted(\n"
    "    double CommitMilliseconds);\n"
    "  ECrowdBoundaryPollResult TryPrepareRoundApply();",
    1,
    "add full worker production fast-path API",
)
header = replace_exact(
    header,
    "  bool bCurrentStepWorkerDirtyMassApplied = false;\n"
    "  uint64 CurrentStepWorkerDirtyMassPublishSequence = 0;\n"
    "  int32 CurrentStepWorkerDirtyMassEntityCount = 0;",
    "  bool bCurrentStepWorkerDirtyMassApplied = false;\n"
    "  uint64 CurrentStepWorkerDirtyMassPublishSequence = 0;\n"
    "  int32 CurrentStepWorkerDirtyMassEntityCount = 0;\n"
    "  bool bCurrentStepFullWorkerProductionFastPath = false;\n"
    "  uint64 CurrentStepFullWorkerInputSequence = 0;\n"
    "  uint64 FullWorkerProductionFastPathStepCount = 0;",
    1,
    "add fast-path step state",
)
write(HEADER, header)

pipeline = read(PIPELINE)
pipeline = replace_exact(
    pipeline,
    "    SoftPressureRollbackHistory.Reset();\n",
    "",
    1,
    "remove rollback history reset",
)
pipeline = remove_between(
    pipeline,
    "void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackSnapshot(\n",
    "void UCrowdDemoRoundSimPipelineSubsystem::InitializeOpenSpawnRelaxation(\n",
    "remove rollback record/restore implementation",
)

# Reset fast-path state on plan activation, step begin, failure and authority
# invalidation. It must survive between Submit and Commit for one fixed step.
pipeline = replace_exact(
    pipeline,
    "  bPlanActive = true;\n  bStepInProgress = false;\n  BoundarySnapshot = {};",
    "  bPlanActive = true;\n"
    "  bStepInProgress = false;\n"
    "  bCurrentStepFullWorkerProductionFastPath = false;\n"
    "  CurrentStepFullWorkerInputSequence = 0;\n"
    "  BoundarySnapshot = {};",
    1,
    "reset fast path on plan activation",
)
pipeline = replace_exact(
    pipeline,
    "  CurrentStepWorkerDirtyMassPublishSequence = 0;\n"
    "  CurrentStepWorkerDirtyMassEntityCount = 0;\n"
    "  CurrentStepMassDirtyEntityRefs.Reset();\n"
    "  bStepInProgress = true;",
    "  CurrentStepWorkerDirtyMassPublishSequence = 0;\n"
    "  CurrentStepWorkerDirtyMassEntityCount = 0;\n"
    "  bCurrentStepFullWorkerProductionFastPath = false;\n"
    "  CurrentStepFullWorkerInputSequence = 0;\n"
    "  CurrentStepMassDirtyEntityRefs.Reset();\n"
    "  bStepInProgress = true;",
    1,
    "reset fast path on fixed-step begin",
)
pipeline = replace_exact(
    pipeline,
    "  bStepInProgress = false;\n  bPlanActive = false;\n  CurrentBoundaryRequestStartSeconds = 0.0;",
    "  bStepInProgress = false;\n"
    "  bPlanActive = false;\n"
    "  bCurrentStepFullWorkerProductionFastPath = false;\n"
    "  CurrentStepFullWorkerInputSequence = 0;\n"
    "  CurrentBoundaryRequestStartSeconds = 0.0;",
    1,
    "reset fast path on failed step",
)
pipeline = replace_exact(
    pipeline,
    "  bStepInProgress = false;\n  CurrentBoundaryRequestStartSeconds = 0.0;\n  BoundarySnapshot = {};",
    "  bStepInProgress = false;\n"
    "  bCurrentStepFullWorkerProductionFastPath = false;\n"
    "  CurrentStepFullWorkerInputSequence = 0;\n"
    "  CurrentBoundaryRequestStartSeconds = 0.0;\n"
    "  BoundarySnapshot = {};",
    1,
    "reset fast path on authoritative invalidation",
)

# Insert direct Production submit/commit helpers immediately before the retained
# Shadow-comparison drain. These helpers deliberately do not allocate a Round
# WorkBatch or BoundaryFacingWorkState.
insert_marker = (
    "bool UCrowdDemoRoundSimPipelineSubsystem::\n"
    "  DrainWorkerV2MovementShadowComparisons()\n"
)
if pipeline.count(insert_marker) != 1:
    raise RuntimeError("fast path insertion marker mismatch")
fast_impl = r'''bool UCrowdDemoRoundSimPipelineSubsystem::
  CanUseFullWorkerProductionFastPath() const
{
  if (!IsFullWorkerProductionMode()
    || !bPlanActive || !bStepInProgress
    || bCurrentStepFullWorkerProductionFastPath
    || CurrentStepFullWorkerInputSequence != 0
    || !IsBoundarySnapshotCurrent() || !GetWorld()
    || GetWorld()->GetNetMode() == NM_Client)
    return false;

  const UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
    return false;
  const FCrowdWorkerBoundaryShadowSync& WorkerShadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  if (!WorkerShadow.IsStarted()
    || WorkerShadow.GetMetrics().FullResnapshotCount == 0
    || LastWorkerV2MovementControlGeneration
      != WorkerShadow.GetGeneration()
    || LastWorkerV2MovementControlPlanRevision
      != GetCurrentPlanRevision()
    || RuntimeSubsystem->GetWorkerMovementAuthority().GetMode()
      != ECrowdWorkerMovementAuthorityMode::Production
    || RuntimeSubsystem->GetWorkerBehaviorAuthority().GetMode()
      != ECrowdWorkerBehaviorAuthorityMode::Production)
    return false;

  const bool bTargetActive = IsTargetRegionExecutionActive();
  const bool bProjectileActive =
    ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && ActivePlan.Rules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
    && ActivePlan.Rules.RangedCombatSettings.bEnabled != 0;
  return (!bTargetActive || bWorkerV2TargetStateBootstrapped)
    && (!bProjectileActive || bWorkerV2ProjectileStateBootstrapped);
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  TrySubmitFullWorkerProductionIntent()
{
  check(IsInGameThread());
  if (!CanUseFullWorkerProductionFastPath())
    return false;

  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
    return false;
  const FCrowdWorkerBoundaryShadowSync& WorkerShadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  const bool bTargetActive = IsTargetRegionExecutionActive();
  const bool bProjectileActive =
    ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && ActivePlan.Rules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
    && ActivePlan.Rules.RangedCombatSettings.bEnabled != 0;

  TArray<FCrowdWorkerObjectiveRevisionDelta> TargetObjectives;
  const uint64 TargetObjectiveSemanticHash = bTargetActive
    ? CalculateTargetObjectiveSemanticHash(GetTargetFact()) : 0;
  const bool bPublishTargetObjective = bTargetActive
    && TargetObjectiveSemanticHash
      != LastWorkerV2TargetObjectiveSemanticHash;
  if (bPublishTargetObjective)
  {
    FCrowdWorkerObjectiveRevisionDelta& Objective =
      TargetObjectives.AddDefaulted_GetRef();
    if (!BuildTargetObjectiveRevisionDelta(
        GetTargetFact(), GetCurrentFixedStepIndex(),
        NextWorkerV2TargetObjectiveRevision, Objective))
      return false;
  }

  const uint64 PreviousInputSequence =
    WorkerShadow.GetMetrics().LastSubmittedInputSequence;
  CurrentBoundaryRequestStartSeconds = FPlatformTime::Seconds();
  if (!FCrowdDemoWorkerInputSync::SubmitIntentBatch(
      *GetWorld(), GetCurrentFixedStepIndex(),
      GetCurrentPlanRevision(),
      GetCurrentStepEndServerTimeSeconds(), {}, {}, {}, {}, nullptr,
      TargetObjectives))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFullWorkerProductionIntentRejected step=%d previous_sequence=%llu"),
      GetCurrentFixedStepIndex(), PreviousInputSequence);
    return false;
  }
  const uint64 AcceptedInputSequence =
    RuntimeSubsystem->GetWorkerShadowSync().GetMetrics().
      LastSubmittedInputSequence;
  if (AcceptedInputSequence == 0
    || AcceptedInputSequence <= PreviousInputSequence)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFullWorkerProductionIntentSequence step=%d previous_sequence=%llu accepted_sequence=%llu"),
      GetCurrentFixedStepIndex(), PreviousInputSequence,
      AcceptedInputSequence);
    return false;
  }

  CurrentStepFullWorkerInputSequence = AcceptedInputSequence;
  bCurrentStepFullWorkerProductionFastPath = true;
  ++WorkerV2MovementControlReuseCount;
  if (bTargetActive)
  {
    ++WorkerV2TargetControlReuseCount;
    if (bPublishTargetObjective)
    {
      LastWorkerV2TargetObjectiveSemanticHash =
        TargetObjectiveSemanticHash;
      ++WorkerV2TargetObjectivePublishCount;
      ++NextWorkerV2TargetObjectiveRevision;
      if (NextWorkerV2TargetObjectiveRevision == 0)
        return false;
    }
    else
    {
      ++WorkerV2TargetObjectiveReuseCount;
    }
  }
  if (bProjectileActive)
    ++WorkerV2ProjectileControlReuseCount;
  ++WorkerV2EarlyClockIntentCount;
  if (WorkerV2EarlyClockIntentCount == 1
    || WorkerV2EarlyClockIntentCount % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoFullWorkerProductionFastPathCheckpoint submitted=%llu input_sequence=%llu simulation_tick=%d generation=%llu plan_revision=%d objective_published=%llu objective_reused=%llu source=PersistentRuntimeAuthority"),
      WorkerV2EarlyClockIntentCount, AcceptedInputSequence,
      GetCurrentFixedStepIndex(), WorkerShadow.GetGeneration(),
      GetCurrentPlanRevision(), WorkerV2TargetObjectivePublishCount,
      WorkerV2TargetObjectiveReuseCount);
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  MarkFullWorkerProductionResultCommitted(
    const double CommitMilliseconds)
{
  check(IsInGameThread());
  if (!bStepInProgress
    || !bCurrentStepFullWorkerProductionFastPath
    || CurrentStepFullWorkerInputSequence == 0
    || !bCurrentStepWorkerDirtyMassApplied
    || CurrentStepMassAccessCounts.CommitWriteCount != 1)
    return false;
  ++FullWorkerProductionFastPathStepCount;
  if (FullWorkerProductionFastPathStepCount == 1
    || FullWorkerProductionFastPathStepCount % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoFullWorkerProductionCommitCheckpoint count=%llu step=%d input_sequence=%llu publish_sequence=%llu dirty_entities=%d commit_ms=%.3f source=WorkerResultApply"),
      FullWorkerProductionFastPathStepCount,
      GetCurrentFixedStepIndex(), CurrentStepFullWorkerInputSequence,
      CurrentStepWorkerDirtyMassPublishSequence,
      CurrentStepWorkerDirtyMassEntityCount, CommitMilliseconds);
  }
  return true;
}

'''
pipeline = pipeline.replace(insert_marker, fast_impl + insert_marker)
write(PIPELINE, pipeline)

# ---------------------------------------------------------------------------
# Processor control flow: full Production ordinary ticks bypass the old Round
# transaction. Bootstrap/Shadow/Canary retain the existing flow unchanged.
# ---------------------------------------------------------------------------
processors = read(PROCESSORS)
processors = replace_exact(
    processors,
    "  FCrowdDemoRoundFacingFinalizeStage CommitFacingFinalize;\n"
    "  CommitFacingFinalize.UseQuery(ResultCommitQuery);\n"
    "  const auto Commit = [&]() -> ECrowdDemoRoundFrameStageResult",
    "  FCrowdDemoRoundFacingFinalizeStage CommitFacingFinalize;\n"
    "  CommitFacingFinalize.UseQuery(ResultCommitQuery);\n"
    "  const bool bFullProductionFastPath =\n"
    "    Pipeline->IsCurrentStepFullWorkerProductionFastPath();\n"
    "  const auto Commit = [&]() -> ECrowdDemoRoundFrameStageResult",
    1,
    "capture fast-path commit mode",
)
old_poll = '''    const ECrowdBoundaryPollResult PollResult =
      Pipeline->IsPreparedMovementBoundaryCommitCurrent()
        || Pipeline->GetRoundWorkState()
          == ECrowdBoundaryTransactionState::ReadyToCommit
        ? ECrowdBoundaryPollResult::Ready
        : Pipeline->TryPrepareRoundApply();
    if (PollResult == ECrowdBoundaryPollResult::Pending)
    {
      Pipeline->RecordPipelineFramePerformance(
        0,
        GetRoundPipelineServerTime(*World),
        false, false);
      return ECrowdDemoRoundFrameStageResult::Pending;
    }
    if (PollResult == ECrowdBoundaryPollResult::Failed)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryPollFailed step=%d prepare_checkpoint=%d"),
        Pipeline->GetCurrentFixedStepIndex(),
        Pipeline->GetLastBoundaryPrepareCheckpoint());
      Pipeline->FailFixedStep();
      return ECrowdDemoRoundFrameStageResult::Failed;
    }

    const double FinalizeStart = FPlatformTime::Seconds();
    if (!Pipeline->IsPreparedMovementBoundaryCommitCurrent())
    {
      if (Pipeline->GetRules().Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure)
      {
        CommitMovementWork.Execute(EntityManager, Context);
        CommitParticleConstraint.Execute(EntityManager, Context);
      }
      CommitFacingFinalize.Execute(EntityManager, Context);
    }
    const float FinalizeMs = static_cast<float>(
      (FPlatformTime::Seconds() - FinalizeStart) * 1000.0);
    Pipeline->RecordPerformanceStage(
      ECrowdDemoRoundPerformanceStage::FacingFinalize,
      FinalizeMs);

    const bool bRequiresCombatCommit =
      Pipeline->IsRangedProjectileCombat()
      || (Pipeline->GetRules().Scenario
          == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::
            MultiStateVatHitResponse);
    if ((bRequiresCombatCommit
        && !Pipeline->IsPreparedCombatBoundaryCommitCurrent())
      || !Pipeline->IsPreparedMovementBoundaryCommitCurrent())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryPreparedCommitMissing step=%d combat=%d"),
        Pipeline->GetCurrentFixedStepIndex(),
        bRequiresCombatCommit ? 1 : 0);
      Pipeline->FailFixedStep();
      return ECrowdDemoRoundFrameStageResult::Failed;
    }
'''
new_poll = '''    if (!bFullProductionFastPath)
    {
      const ECrowdBoundaryPollResult PollResult =
        Pipeline->IsPreparedMovementBoundaryCommitCurrent()
          || Pipeline->GetRoundWorkState()
            == ECrowdBoundaryTransactionState::ReadyToCommit
          ? ECrowdBoundaryPollResult::Ready
          : Pipeline->TryPrepareRoundApply();
      if (PollResult == ECrowdBoundaryPollResult::Pending)
      {
        Pipeline->RecordPipelineFramePerformance(
          0,
          GetRoundPipelineServerTime(*World),
          false, false);
        return ECrowdDemoRoundFrameStageResult::Pending;
      }
      if (PollResult == ECrowdBoundaryPollResult::Failed)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoBoundaryPollFailed step=%d prepare_checkpoint=%d"),
          Pipeline->GetCurrentFixedStepIndex(),
          Pipeline->GetLastBoundaryPrepareCheckpoint());
        Pipeline->FailFixedStep();
        return ECrowdDemoRoundFrameStageResult::Failed;
      }

      const double FinalizeStart = FPlatformTime::Seconds();
      if (!Pipeline->IsPreparedMovementBoundaryCommitCurrent())
      {
        if (Pipeline->GetRules().Scenario
          == ECrowdDemoScenario::SimRoundSoftPressure)
        {
          CommitMovementWork.Execute(EntityManager, Context);
          CommitParticleConstraint.Execute(EntityManager, Context);
        }
        CommitFacingFinalize.Execute(EntityManager, Context);
      }
      const float FinalizeMs = static_cast<float>(
        (FPlatformTime::Seconds() - FinalizeStart) * 1000.0);
      Pipeline->RecordPerformanceStage(
        ECrowdDemoRoundPerformanceStage::FacingFinalize,
        FinalizeMs);

      const bool bRequiresCombatCommit =
        Pipeline->IsRangedProjectileCombat()
        || (Pipeline->GetRules().Scenario
            == ECrowdDemoScenario::SimRoundSoftPressure
          && Pipeline->GetRules().SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::
              MultiStateVatHitResponse);
      if ((bRequiresCombatCommit
          && !Pipeline->IsPreparedCombatBoundaryCommitCurrent())
        || !Pipeline->IsPreparedMovementBoundaryCommitCurrent())
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoBoundaryPreparedCommitMissing step=%d combat=%d"),
          Pipeline->GetCurrentFixedStepIndex(),
          bRequiresCombatCommit ? 1 : 0);
        Pipeline->FailFixedStep();
        return ECrowdDemoRoundFrameStageResult::Failed;
      }
    }
'''
processors = replace_exact(processors, old_poll, new_poll, 1, "split fast commit from legacy prepare")
processors = replace_exact(
    processors,
    "    if (!PendingWorkerResult->IsValid())\n"
    "    {\n"
    "      UE_LOG(LogTemp, Error,\n"
    "        TEXT(\"VIOLATION CrowdDemoWorkerOwnerBarrierPendingInvalid step=%d publish=%llu\"),\n"
    "        Pipeline->GetCurrentFixedStepIndex(),\n"
    "        PendingWorkerResult->WorkerCommitToken.PublishSequence);\n"
    "      Pipeline->FailFixedStep();\n"
    "      return ECrowdDemoRoundFrameStageResult::Failed;\n"
    "    }\n\n"
    "    const double CommitStart = FPlatformTime::Seconds();",
    "    if (!PendingWorkerResult->IsValid())\n"
    "    {\n"
    "      UE_LOG(LogTemp, Error,\n"
    "        TEXT(\"VIOLATION CrowdDemoWorkerOwnerBarrierPendingInvalid step=%d publish=%llu\"),\n"
    "        Pipeline->GetCurrentFixedStepIndex(),\n"
    "        PendingWorkerResult->WorkerCommitToken.PublishSequence);\n"
    "      Pipeline->FailFixedStep();\n"
    "      return ECrowdDemoRoundFrameStageResult::Failed;\n"
    "    }\n"
    "    if (bFullProductionFastPath)\n"
    "    {\n"
    "      const uint64 ExpectedInputSequence =\n"
    "        Pipeline->GetCurrentStepFullWorkerInputSequence();\n"
    "      const uint64 AppliedInputSequence =\n"
    "        PendingWorkerResult->PreparedProxyResult.Batch.\n"
    "          LastAppliedInputSequence;\n"
    "      if (AppliedInputSequence < ExpectedInputSequence)\n"
    "      {\n"
    "        Pipeline->RecordPipelineFramePerformance(\n"
    "          0, GetRoundPipelineServerTime(*World), false, false);\n"
    "        return ECrowdDemoRoundFrameStageResult::Pending;\n"
    "      }\n"
    "      if (ExpectedInputSequence == 0\n"
    "        || AppliedInputSequence != ExpectedInputSequence)\n"
    "      {\n"
    "        UE_LOG(LogTemp, Error,\n"
    "          TEXT(\"VIOLATION CrowdDemoFullWorkerProductionResultSequence step=%d expected=%llu actual=%llu\"),\n"
    "          Pipeline->GetCurrentFixedStepIndex(), ExpectedInputSequence,\n"
    "          AppliedInputSequence);\n"
    "        Pipeline->FailFixedStep();\n"
    "        return ECrowdDemoRoundFrameStageResult::Failed;\n"
    "      }\n"
    "    }\n\n"
    "    const double CommitStart = FPlatformTime::Seconds();",
    1,
    "validate fast result sequence",
)
processors = replace_exact(
    processors,
    "          return FCrowdDemoWorkerInputSync::\n"
    "              PrepareCommittedResultSideEffects(\n"
    "                *World, PendingWorkerResult->PreparedProxyResult,\n"
    "                PreparedSideEffects)\n"
    "            && Pipeline->FinalValidatePreparedTargetResourcePlan(\n"
    "              *PendingWorkerResult->PreparedTargetResourcePlan)\n"
    "            && CommitFacingFinalize.ValidatePreparedCommit(*Pipeline)\n"
    "            && FinalValidatePreparedWorkerMassDirtyPlan(",
    "          return FCrowdDemoWorkerInputSync::\n"
    "              PrepareCommittedResultSideEffects(\n"
    "                *World, PendingWorkerResult->PreparedProxyResult,\n"
    "                PreparedSideEffects)\n"
    "            && (bFullProductionFastPath\n"
    "              || (Pipeline->FinalValidatePreparedTargetResourcePlan(\n"
    "                    *PendingWorkerResult->PreparedTargetResourcePlan)\n"
    "                && CommitFacingFinalize.ValidatePreparedCommit(\n"
    "                  *Pipeline)))\n"
    "            && FinalValidatePreparedWorkerMassDirtyPlan(",
    1,
    "skip legacy final validation on fast path",
)
old_side_effects = '''          CommitValidatedWorkerMassSideEffects(MassPlan, *Pipeline);
          Pipeline->ApplyPreparedTargetResourcePlanNoFail(
            *PendingWorkerResult->PreparedTargetResourcePlan);
          CommitFacingFinalize.CommitValidatedSideEffects(*Pipeline);
          checkf(Pipeline->MarkRoundApplyCommitted(
              (FPlatformTime::Seconds() - CommitStart) * 1000.0),
            TEXT("Validated Round owner commit unexpectedly failed"));
          FCrowdDemoWorkerInputSync::
'''
new_side_effects = '''          CommitValidatedWorkerMassSideEffects(MassPlan, *Pipeline);
          if (bFullProductionFastPath)
          {
            checkf(Pipeline->MarkFullWorkerProductionResultCommitted(
                (FPlatformTime::Seconds() - CommitStart) * 1000.0),
              TEXT("Validated Full Worker Production commit unexpectedly failed"));
          }
          else
          {
            Pipeline->ApplyPreparedTargetResourcePlanNoFail(
              *PendingWorkerResult->PreparedTargetResourcePlan);
            CommitFacingFinalize.CommitValidatedSideEffects(*Pipeline);
            checkf(Pipeline->MarkRoundApplyCommitted(
                (FPlatformTime::Seconds() - CommitStart) * 1000.0),
              TEXT("Validated Round owner commit unexpectedly failed"));
          }
          FCrowdDemoWorkerInputSync::
'''
processors = replace_exact(processors, old_side_effects, new_side_effects, 1, "split fast side effects")
processors = replace_exact(
    processors,
    "    const bool bApplied = BarrierResult\n"
    "        == ECrowdWorkerResultOwnerCommitResult::Committed\n"
    "      && Pipeline->IsMovementFinalizeAppliedCurrent();",
    "    const bool bApplied = BarrierResult\n"
    "        == ECrowdWorkerResultOwnerCommitResult::Committed\n"
    "      && (bFullProductionFastPath\n"
    "        ? Pipeline->IsCurrentStepWorkerDirtyMassApplied()\n"
    "        : Pipeline->IsMovementFinalizeAppliedCurrent());",
    1,
    "fast commit applied condition",
)
processors = replace_exact(
    processors,
    "  else if (bCommitted)\n"
    "  {\n"
    "    FCrowdDemoRoundAuthorityCommitStage AuthorityCommit;\n"
    "    AuthorityCommit.Execute(EntityManager, Context);\n"
    "  }\n"
    "  if (bCommitted)\n"
    "  {\n"
    "    FCrowdDemoRoundPostFinalizeMetricsStage PostFinalizeMetrics;\n"
    "    PostFinalizeMetrics.Execute(EntityManager, Context);",
    "  else if (bCommitted && !bFullProductionFastPath)\n"
    "  {\n"
    "    FCrowdDemoRoundAuthorityCommitStage AuthorityCommit;\n"
    "    AuthorityCommit.Execute(EntityManager, Context);\n"
    "  }\n"
    "  if (bCommitted)\n"
    "  {\n"
    "    if (!bFullProductionFastPath)\n"
    "    {\n"
    "      FCrowdDemoRoundPostFinalizeMetricsStage PostFinalizeMetrics;\n"
    "      PostFinalizeMetrics.Execute(EntityManager, Context);\n"
    "    }",
    1,
    "skip legacy post-commit stages on fast path",
)
# Branch to direct Production immediately after the transitional Worker-proxy
# cache is refreshed. TargetFactApply itself is O(1) and has no orchestrator
# dependency, so it remains the external moving-target fact producer.
processors = replace_exact(
    processors,
    "    const double SnapshotApplyMilliseconds =\n"
    "      (FPlatformTime::Seconds() - SnapshotApplyStartSeconds) * 1000.0;\n"
    "    if (!Pipeline->BeginBoundaryTransaction(SnapshotApplyMilliseconds))",
    "    const double SnapshotApplyMilliseconds =\n"
    "      (FPlatformTime::Seconds() - SnapshotApplyStartSeconds) * 1000.0;\n"
    "    if (Pipeline->CanUseFullWorkerProductionFastPath())\n"
    "    {\n"
    "      TargetFactApply.Execute(EntityManager, Context);\n"
    "      if (!Pipeline->TrySubmitFullWorkerProductionIntent())\n"
    "      {\n"
    "        UE_LOG(LogTemp, Error,\n"
    "          TEXT(\"VIOLATION CrowdDemoFullWorkerProductionSubmitFailed step=%d\"),\n"
    "          Pipeline->GetCurrentFixedStepIndex());\n"
    "        Pipeline->FailFixedStep();\n"
    "        return false;\n"
    "      }\n"
    "      Pipeline->RecordPerformanceStage(\n"
    "        ECrowdDemoRoundPerformanceStage::BusinessPrepare,\n"
    "        static_cast<float>(SnapshotApplyMilliseconds));\n"
    "      Pipeline->RecordPipelineFramePerformance(\n"
    "        ExecutedSteps, TargetServerTime, false, false);\n"
    "      return true;\n"
    "    }\n"
    "    if (!Pipeline->BeginBoundaryTransaction(SnapshotApplyMilliseconds))",
    1,
    "insert production bypass before round transaction",
)

# Remove the production construction of a full rollback snapshot. The retained
# PostFinalize stage is now legacy/diagnostic only and no longer owns replay.
processors = replace_exact(
    processors,
    "  TArray<FCrowdDemoSoftPressureRollbackAgentState> SoftPressureRollbackAgents;\n",
    "",
    1,
    "remove post-finalize rollback agent array",
)
processors = replace_exact(
    processors,
    "  TArray<FCrowdDemoPreparedCombatRollbackFact> RollbackCombatStates;\n"
    "  TArray<FCrowdDemoParticleAppliedRoundSimState> ParticleAppliedStates;\n"
    "  RollbackCombatStates.Reserve(BoundaryByAgentId.Num());",
    "  TArray<FCrowdDemoParticleAppliedRoundSimState> ParticleAppliedStates;",
    1,
    "remove post-finalize rollback combat array",
)
processors = remove_between(
    processors,
    "      FCrowdDemoSoftPressureRollbackAgentState& Rollback =\n",
    "      if (Pipeline->GetRules().Scenario\n        == ECrowdDemoScenario::SimRoundSoftPressure)\n      {\n        FCrowdDemoParticleAppliedRoundSimState& Applied =\n",
    "remove post-finalize per-agent rollback copy",
)
# remove_between kept the particle block marker; restore its desired indentation
# and content marker is already present.
processors = replace_exact(
    processors,
    "  Pipeline->RecordSoftPressureRollbackSnapshot(\n"
    "    Pipeline->GetCurrentFixedStepIndex(),\n"
    "    MoveTemp(SoftPressureRollbackAgents));\n"
    "  Pipeline->RecordFlowAgentSamples(\n"
    "    MetricSamples, World->GetNetMode() == NM_Client);\n"
    "  if (RollbackCombatStates.Num() != BoundaryByAgentId.Num()\n"
    "    || !Pipeline->CompleteSoftPressureRollbackCombatState(\n"
    "      Pipeline->GetCurrentFixedStepIndex(), RollbackCombatStates))\n"
    "  {\n"
    "    UE_LOG(LogTemp, Error,\n"
    "      TEXT(\"VIOLATION CrowdDemoPostFinalizeCombatFactsIncomplete step=%d rollback=%d expected=%d\"),\n"
    "      Pipeline->GetCurrentFixedStepIndex(), RollbackCombatStates.Num(),\n"
    "      BoundaryByAgentId.Num());\n"
    "    return;\n"
    "  }",
    "  Pipeline->RecordFlowAgentSamples(\n"
    "    MetricSamples, World->GetNetMode() == NM_Client);",
    1,
    "remove rollback snapshot completion from post-finalize",
)
processors = replace_exact(
    processors,
    "  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure\n"
    "    && !Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(\n"
    "      Pipeline->GetCurrentFixedStepIndex()))\n"
    "  {\n"
    "    UE_LOG(LogTemp, Error,\n"
    "      TEXT(\"VIOLATION CrowdDemoCheckpointSnapshotIncomplete step=%d\"),\n"
    "      Pipeline->GetCurrentFixedStepIndex());\n"
    "    return;\n"
    "  }\n",
    "",
    1,
    "remove checkpoint rollback replay gate",
)
write(PROCESSORS, processors)

# ---------------------------------------------------------------------------
# Tests: retire tests for the deleted replay mechanism, keep architecture tests
# and turn the old positive rollback assertion into an absence assertion.
# ---------------------------------------------------------------------------
combat = read(COMBAT_TESTS)
combat = remove_test(combat, "FCrowdDemoCombatRollbackCompletionGateTest")
combat = remove_test(combat, "FCrowdDemoSf1CorrectionHistorySnapshotTest")
combat = replace_exact(
    combat,
    "  TestTrue(TEXT(\"post-finalize completes combat rollback facts\"),\n"
    "    PostFinalizeBlock.Contains(TEXT(\n"
    "      \"CompleteSoftPressureRollbackCombatState(\")));",
    "  TestFalse(TEXT(\"post-finalize no longer builds full rollback history\"),\n"
    "    PostFinalizeBlock.Contains(TEXT(\n"
    "      \"CompleteSoftPressureRollbackCombatState(\"))\n"
    "      || PostFinalizeBlock.Contains(TEXT(\n"
    "        \"RecordSoftPressureRollbackSnapshot(\")));",
    1,
    "update rollback architecture assertion",
)
# Add explicit static assertions for the new split without requiring UE compile.
combat = replace_exact(
    combat,
    "  TestTrue(TEXT(\"early Clock intent is restricted to bootstrapped Production domains\"),",
    "  TestTrue(TEXT(\"Full Production has a direct Worker intent path\"),\n"
    "    ProcessorSource.Contains(TEXT(\n"
    "      \"Pipeline->CanUseFullWorkerProductionFastPath()\"))\n"
    "      && ProcessorSource.Contains(TEXT(\n"
    "        \"Pipeline->TrySubmitFullWorkerProductionIntent()\"))\n"
    "      && PipelineSource.Contains(TEXT(\n"
    "        \"CrowdDemoFullWorkerProductionFastPathCheckpoint\")));\n"
    "  const int32 ProductionFastPathCall = ProcessorSource.Find(TEXT(\n"
    "    \"Pipeline->CanUseFullWorkerProductionFastPath()\"));\n"
    "  const int32 LegacyBoundaryBeginCall = ProcessorSource.Find(TEXT(\n"
    "    \"Pipeline->BeginBoundaryTransaction(SnapshotApplyMilliseconds)\"),\n"
    "    ESearchCase::CaseSensitive, ESearchDir::FromStart,\n"
    "    ProductionFastPathCall);\n"
    "  TestTrue(TEXT(\"Full Production bypass is decided before legacy transaction begin\"),\n"
    "    ProductionFastPathCall != INDEX_NONE\n"
    "      && LegacyBoundaryBeginCall > ProductionFastPathCall);\n"
    "  TestFalse(TEXT(\"full rollback replay API is physically retired\"),\n"
    "    PipelineSource.Contains(TEXT(\"SoftPressureRollbackHistory\"))\n"
    "      || PipelineSource.Contains(TEXT(\"RestoreSoftPressureRuntime(\"))\n"
    "      || PipelineHeader.Contains(TEXT(\"FCrowdDemoSoftPressureRollbackSnapshot\")));\n"
    "  TestTrue(TEXT(\"early Clock intent is restricted to bootstrapped Production domains\"),",
    1,
    "add production bypass architecture assertions",
)
write(COMBAT_TESTS, combat)

local_tests = read(LOCAL_TESTS)
local_tests = remove_test(local_tests, "FCrowdDemoLocalPredictiveRollbackStateTest")
local_tests = remove_test(local_tests, "FCrowdDemoRollbackDerivedResourceContractTest")
write(LOCAL_TESTS, local_tests)

# ---------------------------------------------------------------------------
# Static gates. This batch deliberately keeps the old WorkBatch/TryPrepare path
# for bootstrap, Shadow and Canary, but Production must now have a direct branch.
# ---------------------------------------------------------------------------
header = read(HEADER)
pipeline = read(PIPELINE)
processors = read(PROCESSORS)
combat = read(COMBAT_TESTS)
local_tests = read(LOCAL_TESTS)
all_source = header + pipeline + processors + combat + local_tests

for retired in [
    "SoftPressureRollbackHistory",
    "FCrowdDemoSoftPressureRollbackSnapshot",
    "FCrowdDemoSoftPressureRollbackAgentState",
    "FCrowdDemoPreparedCombatRollbackFact",
    "RecordSoftPressureRollbackSnapshot(",
    "CompleteSoftPressureRollbackCombatState(",
    "FindSoftPressureRollbackSnapshot(",
    "IsSoftPressureRollbackSnapshotReadyForReplay(",
    "RestoreSoftPressureRuntime(",
]:
    if retired in all_source:
        raise RuntimeError(f"retired full rollback symbol remains: {retired}")

required = [
    "CanUseFullWorkerProductionFastPath()",
    "TrySubmitFullWorkerProductionIntent()",
    "MarkFullWorkerProductionResultCommitted(",
    "CrowdDemoFullWorkerProductionFastPathCheckpoint",
    "FCrowdWorkerResultOwnerCommitBarrier::Commit(",
    "ApplyValidatedWorkerMassDirtyPlan(",
]
for symbol in required:
    if symbol not in header + pipeline + processors:
        raise RuntimeError(f"required production symbol missing: {symbol}")

# Legacy path must remain in this slice; physical deletion is the next slice.
for retained in [
    "FCrowdDemoRoundWorkBatch",
    "BeginBoundaryTransaction(",
    "TryPrepareRoundApply()",
    "DispatchBoundarySoftPressureWorkGraph(",
]:
    if retained not in header + pipeline + processors:
        raise RuntimeError(f"legacy bootstrap/parity symbol removed too early: {retained}")

advance_start = processors.find("bool AdvanceRoundWorkerFrame(")
advance_end = processors.find("UCrowdDemoWorkerInputSyncProcessor::", advance_start)
if advance_start < 0 or advance_end <= advance_start:
    raise RuntimeError("AdvanceRoundWorkerFrame block missing")
advance = processors[advance_start:advance_end]
fast_decision = advance.find("Pipeline->CanUseFullWorkerProductionFastPath()")
legacy_begin = advance.find("Pipeline->BeginBoundaryTransaction(SnapshotApplyMilliseconds)")
if fast_decision < 0 or legacy_begin <= fast_decision:
    raise RuntimeError("Production fast decision does not precede legacy transaction begin")
if "if (!bFullProductionFastPath)" not in advance:
    raise RuntimeError("commit does not isolate legacy TryPrepareRoundApply")
if "if (!bFullProductionFastPath)\n    {\n      const ECrowdBoundaryPollResult" not in advance:
    raise RuntimeError("legacy prepare gate shape changed unexpectedly")
if "MarkFullWorkerProductionResultCommitted" not in advance:
    raise RuntimeError("fast result does not close through explicit commit marker")

print("production round cut patch applied; static gates passed")
