#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"

#include "Mass/CrowdDemoSeparationKernel.h"

namespace
{
  constexpr float CorrectionIntervalSeconds = 0.5f;
  constexpr float CorrectionMaxAgeMs = 1000.0f;
  constexpr float OverlapRadiusCm = 78.0f;
  constexpr float SevereOverlapRadiusCm = 42.0f;

  bool IsFlowScenario(const ECrowdDemoScenario Scenario)
  {
    return Scenario == ECrowdDemoScenario::SimRoundObstacle
      || Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      || Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic
      || Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
  }

  bool IsTrafficScenario(const ECrowdDemoScenario Scenario)
  {
    return Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic
      || Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
  }

  uint32 FoldHash(uint32 Hash, const uint32 Value)
  {
    for (int32 Shift = 0; Shift < 32; Shift += 8)
    {
      Hash ^= (Value >> Shift) & 0xffu;
      Hash *= 16777619u;
    }
    return Hash;
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueBootstrap(const FCrowdDemoRoundBootstrapPacket& Packet)
{
  if (Packet.bValid != 0 && (!bBootstrapApplied || Packet.Revision >= PendingBootstrap.Revision))
  {
    PendingBootstrap = Packet;
    if (!bPlanActive)
    {
      LastClaimedPlanApplyBoundarySequence = MAX_uint64;
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueRoundPlan(const FCrowdDemoRoundPlanPacket& Packet)
{
  if (Packet.bValid != 0 && (!bPlanActive || Packet.Revision > ActivePlan.Revision))
  {
    PendingPlans.Add(Packet.Revision, Packet);
    LastCompareMetrics.RoundPlanRevisionSeen = FMath::Max(LastCompareMetrics.RoundPlanRevisionSeen, Packet.Revision);
    if (!bPlanActive)
    {
      LastClaimedPlanApplyBoundarySequence = MAX_uint64;
    }
  }
}

float FCrowdDemoRoundErrorSeries::GetMax() const
{
  float Maximum = -1.0f;
  for (const float Sample : CheckpointP95Samples)
  {
    Maximum = FMath::Max(Maximum, Sample);
  }
  return Maximum;
}

float FCrowdDemoRoundErrorSeries::GetExpansionFromFirst() const
{
  if (CheckpointP95Samples.Num() < 2)
  {
    return 0.0f;
  }
  float LaterMax = CheckpointP95Samples[1];
  for (int32 Index = 2; Index < CheckpointP95Samples.Num(); ++Index)
  {
    LaterMax = FMath::Max(LaterMax, CheckpointP95Samples[Index]);
  }
  return FMath::Max(0.0f, LaterMax - CheckpointP95Samples[0]);
}

bool UCrowdDemoRoundSimPipelineSubsystem::TryClaimPlanApplyBoundary()
{
  if (LastClaimedPlanApplyBoundarySequence == PlanApplyBoundarySequence)
  {
    return false;
  }
  LastClaimedPlanApplyBoundarySequence = PlanApplyBoundarySequence;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::EnsureFormationIndexCache(
  const TConstArrayView<int32> AgentIds)
{
  uint64 MembershipHash = 1469598103934665603ull ^ static_cast<uint64>(AgentIds.Num());
  for (const int32 AgentId : AgentIds)
  {
    uint64 Mixed = static_cast<uint64>(static_cast<uint32>(AgentId)) + 0x9e3779b97f4a7c15ull;
    Mixed = (Mixed ^ (Mixed >> 30)) * 0xbf58476d1ce4e5b9ull;
    Mixed = (Mixed ^ (Mixed >> 27)) * 0x94d049bb133111ebull;
    Mixed ^= Mixed >> 31;
    MembershipHash ^= Mixed;
  }
  if (FormationMembershipCount == AgentIds.Num()
    && FormationMembershipHash == MembershipHash
    && FormationIndexByAgentId.Num() == AgentIds.Num())
  {
    return;
  }

  TArray<int32> SortedAgentIds;
  SortedAgentIds.Append(AgentIds.GetData(), AgentIds.Num());
  SortedAgentIds.Sort();
  FormationIndexByAgentId.Empty(SortedAgentIds.Num());
  for (int32 Index = 0; Index < SortedAgentIds.Num(); ++Index)
  {
    FormationIndexByAgentId.Add(SortedAgentIds[Index], Index);
  }
  FormationMembershipCount = AgentIds.Num();
  FormationMembershipHash = MembershipHash;
  ++FormationCacheRebuildCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueRoundResult(const FCrowdDemoRoundResultPacket& Packet)
{
  if (Packet.bValid != 0)
  {
    const bool bNewResult = !PendingResults.Contains(Packet.CheckpointRevision);
    PendingResults.Add(Packet.CheckpointRevision, Packet);
    if (!bNewResult)
    {
      return;
    }
    ++RoundResultPipelineQueuedCount;
    // Network work may arrive after the stationary round-end boundary was
    // already inspected. Re-open that boundary without advancing simulation.
    LastClaimedPlanApplyBoundarySequence = MAX_uint64;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoRoundResultTransport role=client stage=pipeline_queued round_id=%d checkpoint_revision=%d agents=%d pipeline_queued_count=%d source=MassPipeline"),
      Packet.RoundId,
      Packet.CheckpointRevision,
      Packet.Agents.Num(),
      RoundResultPipelineQueuedCount);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueCorrectionFrame(
  const FCrowdDemoCorrectionFrame& Frame,
  const float ReceiveServerTimeSeconds)
{
  if (Frame.bValid != 0 && Frame.CorrectionRevision > LastAppliedCorrectionRevision)
  {
    PendingCorrections.Add(Frame.CorrectionRevision, TPair<FCrowdDemoCorrectionFrame, float>(Frame, ReceiveServerTimeSeconds));
    LastCorrectionMetrics.CorrectionFrameLatestRevisionSeen = FMath::Max(
      LastCorrectionMetrics.CorrectionFrameLatestRevisionSeen,
      Frame.CorrectionRevision);
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::PeekBootstrap(FCrowdDemoRoundBootstrapPacket& OutPacket) const
{
  if (bBootstrapApplied || PendingBootstrap.bValid == 0)
  {
    return false;
  }
  OutPacket = PendingBootstrap;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkBootstrapApplied(const int32 AgentCount)
{
  bBootstrapApplied = true;
  LastCompareMetrics.RoundBootstrapAgentCount = AgentCount;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopDueRoundPlan(
  const float BoundaryServerTimeSeconds,
  FCrowdDemoRoundPlanPacket& OutPacket)
{
  if (bPlanActive
    && GetWorld()
    && GetWorld()->GetNetMode() == NM_Client
    && SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER
      >= ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds
    && LastCompareMetrics.CompletedRoundCount < ActivePlan.RoundId)
  {
    return false;
  }
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, FCrowdDemoRoundPlanPacket>& Pair : PendingPlans)
  {
    if (Pair.Value.StartServerTimeSeconds <= BoundaryServerTimeSeconds + KINDA_SMALL_NUMBER
      && (SelectedRevision == INDEX_NONE || Pair.Key < SelectedRevision))
    {
      SelectedRevision = Pair.Key;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  OutPacket = PendingPlans.FindAndRemoveChecked(SelectedRevision);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopCorrectionForBoundary(
  FCrowdDemoCorrectionFrame& OutFrame,
  float& OutReceiveServerTimeSeconds)
{
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, TPair<FCrowdDemoCorrectionFrame, float>>& Pair : PendingCorrections)
  {
    const FCrowdDemoCorrectionFrame& Frame = Pair.Value.Key;
    if (Frame.CorrectionRevision > LastAppliedCorrectionRevision
      && Frame.RoundId == GetCurrentRoundId()
      && Frame.RoundRevision == GetCurrentPlanRevision()
      && Frame.ServerTimeSeconds <= SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER
      && Pair.Key > SelectedRevision)
    {
      SelectedRevision = Pair.Key;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  TPair<FCrowdDemoCorrectionFrame, float> Pair = PendingCorrections.FindAndRemoveChecked(SelectedRevision);
  OutFrame = MoveTemp(Pair.Key);
  OutReceiveServerTimeSeconds = Pair.Value;
  TArray<int32> OldRevisions;
  for (const TPair<int32, TPair<FCrowdDemoCorrectionFrame, float>>& Pending : PendingCorrections)
  {
    if (Pending.Key < SelectedRevision)
    {
      OldRevisions.Add(Pending.Key);
    }
  }
  for (const int32 Revision : OldRevisions)
  {
    PendingCorrections.Remove(Revision);
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopRoundResultForBoundary(FCrowdDemoRoundResultPacket& OutPacket)
{
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, FCrowdDemoRoundResultPacket>& Pair : PendingResults)
  {
    if (Pair.Value.RoundId == GetCurrentRoundId()
      && Pair.Value.Revision == GetCurrentPlanRevision()
      && Pair.Value.EndServerTimeSeconds <= SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER)
    {
      SelectedRevision = Pair.Key;
      break;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  OutPacket = PendingResults.FindAndRemoveChecked(SelectedRevision);
  ++RoundResultBoundaryAppliedCount;
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundResultTransport role=client stage=boundary_applied round_id=%d checkpoint_revision=%d agents=%d pipeline_queued_count=%d boundary_applied_count=%d source=MassPipeline"),
    OutPacket.RoundId,
    OutPacket.CheckpointRevision,
    OutPacket.Agents.Num(),
    RoundResultPipelineQueuedCount,
    RoundResultBoundaryAppliedCount);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::HasDueRoundPlan(const float BoundaryServerTimeSeconds) const
{
  for (const TPair<int32, FCrowdDemoRoundPlanPacket>& Pair : PendingPlans)
  {
    if (Pair.Value.StartServerTimeSeconds <= BoundaryServerTimeSeconds + KINDA_SMALL_NUMBER)
    {
      return true;
    }
  }
  return false;
}

void UCrowdDemoRoundSimPipelineSubsystem::ActivatePlan(
  const FCrowdDemoRoundPlanPacket& Packet,
  const int32 AgentCount,
  const bool bLate)
{
  if (bPlanActive && Packet.Revision > ActivePlan.Revision + 1)
  {
    LastCompareMetrics.RoundPlanGapCount += Packet.Revision - ActivePlan.Revision - 1;
  }
  ActivePlan = Packet;
  bPlanActive = true;
  CurrentFixedStepSeconds = FMath::Max(1.0f / 120.0f, Packet.Rules.FixedStepSeconds);
  if (LastCompareMetrics.RoundPlanAppliedCount == 0 || SimulatedServerTimeSeconds < Packet.StartServerTimeSeconds)
  {
    SimulatedServerTimeSeconds = Packet.StartServerTimeSeconds;
  }
  ++LastCompareMetrics.RoundPlanAppliedCount;
  LastCompareMetrics.CurrentRoundId = Packet.RoundId;
  LastCompareMetrics.RoundId = Packet.RoundId;
  LastCompareMetrics.Revision = Packet.Revision;
  LastCompareMetrics.RoundBootstrapAgentCount = AgentCount;
  if (bLate)
  {
    ++LastCompareMetrics.RoundPlanLateCount;
  }
  LastBuiltResultRoundId = FMath::Min(LastBuiltResultRoundId, Packet.RoundId - 1);
  CorrectionIntervalPositionP95Samples.Reset();
  CorrectionIntervalPositionMaxSamples.Reset();
  LastCompareMetrics.CorrectionIntervalPositionErrorCmP95 = -1.0f;
  LastCompareMetrics.CorrectionIntervalPositionErrorCmMax = -1.0f;
  if (IsFlowScenario(Packet.Rules.Scenario))
  {
    FlowGoalReachedAgentIds.Reset();
    FlowWallPassAgentIds.Reset();
    FlowCorridorExitAgentIds.Reset();
    FlowTurnExitAgentIds.Reset();
    FlowLowSpeedSecondsByAgentId.Reset();
    FlowCorridorDeadlockAgentIds.Reset();
    LastCompareMetrics.FlowUnreachableAgentCount = 0;
    LastCompareMetrics.FlowGoalReachedCount = 0;
    LastCompareMetrics.FlowWallPassCount = 0;
    LastCompareMetrics.FlowCorridorExitCount = 0;
    LastCompareMetrics.FlowTurnExitCount = 0;
    LastCompareMetrics.ServerObstaclePenetrationCount = 0;
    LastCompareMetrics.ClientSimObstaclePenetrationCount = 0;
    LastCompareMetrics.CorridorDeadlockAgentCount = 0;
  }
  if (Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    SoftPressureRollbackHistory.Reset();
    SoftPressureRollbackSnapshotHitCount = 0;
    SoftPressureRollbackSnapshotMissCount = 0;
    SoftPressureRollbackAgentMismatchCount = 0;
    SoftPressureRollbackReplayedStepCount = 0;
    FlowSeparationOverlapPairSamples.Reset();
    FlowSeparationSevereOverlapPairSamples.Reset();
    PbdSolverMillisecondsSamples.Reset();
    ParticleSolverMillisecondsSamples.Reset();
    LastParticleCandidateSummary = FCrowdDemoParticleConstraintSummary();
    LastParticleAppliedSummary = FCrowdDemoParticleConstraintSummary();
    ParticleCandidateStateHash = 2166136261u;
    ParticleAppliedStateHash = 2166136261u;
    ParticleInvalidStepCount = 0;
    ParticleGlobalFallbackStepCount = 0;
    ParticleStepCount = 0;
    ParticleSettlingWindowCount = 0;
    ParticleSettlingSteps = INDEX_NONE;
    ParticlePreviousSoftErrorP95 = -1.0f;
    bParticleConstraintRunFailure = false;
    ParticleFailureFixture = FCrowdDemoParticleFailureFixture();
    LastCompareMetrics.InitialOverlapPairCount = 0;
    LastCompareMetrics.OverlapPairCountP50 = -1.0f;
    LastCompareMetrics.OverlapPairCountP95 = -1.0f;
    LastCompareMetrics.OverlapPairCountMax = 0;
    LastCompareMetrics.SevereOverlapPairCountP50 = -1.0f;
    LastCompareMetrics.SevereOverlapPairCountP95 = -1.0f;
    LastCompareMetrics.SevereOverlapPairCountMax = 0;
    LastCompareMetrics.SoftSeparationAppliedAgentCount = 0;
    LastCompareMetrics.PbdCorrectedAgentCount = 0;
    LastCompareMetrics.PbdCorrectedPairCount = 0;
    LastCompareMetrics.PbdMaxPairCorrectionCm = 0.0f;
    LastCompareMetrics.PbdMaxAgentTotalCorrectionCm = 0.0f;
    LastCompareMetrics.PbdMaxObstacleReprojectDeltaCm = 0.0f;
    LastCompareMetrics.PbdMaxFinalSafetyDeltaCm = 0.0f;
    LastCompareMetrics.PbdSolverMsP95 = -1.0f;
  }
  if (IsTrafficScenario(Packet.Rules.Scenario))
  {
    for (FCrowdDemoSf3StageHash& StageHash : Sf3StageHashes)
    {
      StageHash = FCrowdDemoSf3StageHash();
    }
    Sf3StageHashHistory.Reset();
    Sf3RollbackHistory.Reset();
    PreparedTrafficAgents.Reset();
    PreparedTrafficCells.Reset();
    PreparedTrafficPortals.Reset();
    PortalExtractionSummary = FCrowdDemoPortalExtractionSummary();
    PreparedPortalCandidates.Reset();
    PreparedPortalDecisions.Reset();
    PreparedOrcaAgents.Reset();
    PreparedOrcaResults.Reset();
    PursuitTargetFact = FCrowdDemoPursuitTargetFact();
    PursuitPositioningSettings = FCrowdDemoPursuitPositioningSettings();
    PreparedPositionCandidates.Reset();
    PreparedPositionAssignments.Reset();
    PreparedHoldingCandidates.Reset();
    TransitCapacitySelection = FCrowdDemoTransitCapacityResult();
    PreparedHoldingCompatibilities.Reset();
    PreparedHoldingAssignments.Reset();
    PreparedCommitRequests.Reset();
    PreparedCommitGateResult = FCrowdDemoCommitGateResult();
    PreparedSteeringGuidance.Reset();
    HoldingSummary = FCrowdDemoHoldingSummary();
    HoldingCompatibilityInputHash = 0;
    JointAssignmentInputHash = 0;
    ResidualPositioningSummary = FCrowdDemoResidualPositioningSummary();
    HoldingMatchingResult = FCrowdDemoHoldingMatchingResult();
    HoldingHallFixture = FCrowdDemoHoldingHallFixture();
    HallGeometryFixture = FCrowdDemoHallGeometryFixture();
    JointPositioningResult = FCrowdDemoJointPositioningResult();
    UnfinishedBoundaryFixture = FCrowdDemoSf4UnfinishedBoundaryFixture();
    PhysicalUnsatisfiedBoundaryFixture = FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture();
    JointCommitResidualResult = FCrowdDemoJointCommitResidualResult();
    SteeringStateHash = 2166136261u;
    PreparedPositionApproachRoutes.Reset();
    PreparedFrontPhaseReservationRequests.Reset();
    PreparedFrontPhaseReservationResult = FCrowdDemoFrontPhaseReservationResult();
    PreparedFrontPhaseReservationDecisions.Reset();
    PreparedFrontAdmissionResult = FCrowdDemoFrontAdmissionResult();
    PreparedFrontReservationWaitEdges.Reset();
    FrontReservationWaitGraphSummary = FCrowdDemoFrontReservationWaitGraphSummary();
    FrontReservationWaitGraphFixture = FCrowdDemoFrontReservationWaitGraphFixture();
    Sf4ReservationOrcaDiagnosticFixture = FCrowdDemoSf4ReservationOrcaDiagnosticFixture();
    Sf4ReservationOrcaCapturedRoundId = INDEX_NONE;
    TransitJointDiagnosticFixture = FCrowdDemoTransitJointDiagnosticFixture();
    TransitJointDiagnosticCapturedRoundId = INDEX_NONE;
    TransitCapacityShadowAgents.Reset();
    TransitCapacityShadowPairs.Reset();
    TransitCapacityShadowComponents.Reset();
    TransitCapacityShadowResults.Reset();
    TransitCapacityShadowSummary = FCrowdDemoTransitCapacityShadowSummary();
    TransitCapacityShadowSolverMsSamples.Reset();
    ElasticCrowdShadowAgents.Reset();
    ElasticCrowdShadowResults.Reset();
    ElasticCrowdShadowSummary = FCrowdDemoElasticCrowdSummary();
    ElasticParallelState = FCrowdDemoElasticShadowParallelState();
    for (int32 ElasticStageIndex = 0; ElasticStageIndex < 8; ++ElasticStageIndex)
    {
      ElasticBaselineDesiredForward[ElasticStageIndex] = 0;
      ElasticBaselineActualForward[ElasticStageIndex] = 0;
      ElasticTwinDesiredForward[ElasticStageIndex] = 0;
      ElasticTwinActualForward[ElasticStageIndex] = 0;
    }
    ElasticZeroProgressSteps.Reset();
    ElasticSpacingDeficitSamples.Reset();
    ElasticTransitDeficitSamples.Reset();
    ElasticRecoveryErrorSamples.Reset();
    ElasticSolverMsSamples.Reset();
    ElasticFailureFixture = FCrowdDemoElasticShadowFailureFixture();
    TransitCapacityFailureFixture = FCrowdDemoTransitCapacityFailureFixture();
    LastPositioningSummary = FCrowdDemoPositioningSummary();
    LastPositionIngressSummary = FCrowdDemoPositionIngressSummary();
    MinimumPositionIngressFixture = FCrowdDemoPositionIngressFixture();
    PositionIngressLowSpeedStepsByAgentId.Reset();
    PositionPromotedAgentIds.Reset();
    PositionCandidateBuiltRevision = INDEX_NONE;
    PositionAssignmentRevision = 0;
    Sf3GoalDiagnostics.Reset();
    FlowReachabilityPreviousStage.Reset();
    FlowFinalInvalidAgentIds.Reset();
    FlowReachabilityPreviousStep = INDEX_NONE;
    for (FCrowdDemoFlowReachabilityWitness& Witness : FlowReachabilityWitnesses)
      Witness = FCrowdDemoFlowReachabilityWitness();
    TrafficQueueSamples.Reset();
    TrafficOccupiedSamples.Reset();
    BandLateralErrorSamples.Reset();
    OrcaNeighborSamples.Reset();
    OrcaConstraintSamples.Reset();
    OrcaSolverMsSamples.Reset();
    OrcaOracleRecoveryMsSamples.Reset();
    PhaseReservationHeldStepSamples.Reset();
    FlowSeparationOverlapPairSamples.Reset();
    FlowSeparationSevereOverlapPairSamples.Reset();
    TrafficRoundHash = PortalRoundHash = OrcaRoundHash = PriorityOrcaRoundHash = 2166136261u;
    TrafficFixedStepIndex = 0;
    LastCompareMetrics.TrafficMetrics = FCrowdDemoTrafficMetrics();
    LastCompareMetrics.OverlapPairCountP50 = -1.0f;
    LastCompareMetrics.OverlapPairCountP95 = -1.0f;
    LastCompareMetrics.OverlapPairCountMax = 0;
    LastCompareMetrics.SevereOverlapPairCountP50 = -1.0f;
    LastCompareMetrics.SevereOverlapPairCountP95 = -1.0f;
    LastCompareMetrics.SevereOverlapPairCountMax = 0;
    PbdSolverMillisecondsSamples.Reset();
    LastCompareMetrics.PbdCorrectedAgentCount = 0;
    LastCompareMetrics.PbdCorrectedPairCount = 0;
    LastCompareMetrics.PbdMaxPairCorrectionCm = 0.0f;
    LastCompareMetrics.PbdMaxAgentTotalCorrectionCm = 0.0f;
    LastCompareMetrics.PbdMaxObstacleReprojectDeltaCm = 0.0f;
    LastCompareMetrics.PbdMaxFinalSafetyDeltaCm = 0.0f;
    LastCompareMetrics.PbdSolverMsP95 = -1.0f;
  }
  const UWorld* World = GetWorld();
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundInit role=%s round_id=%d revision=%d previous_checkpoint_revision=%d agents=%d start_server_time=%.3f duration=%.3f fixed_step=%.4f scenario=%d plan_late=%d source=MassPipeline"),
    World && World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
    Packet.RoundId,
    Packet.Revision,
    Packet.PreviousCheckpointRevision,
    AgentCount,
    Packet.StartServerTimeSeconds,
    Packet.DurationSeconds,
    CurrentFixedStepSeconds,
    static_cast<int32>(Packet.Rules.Scenario),
    bLate ? 1 : 0);
}

bool UCrowdDemoRoundSimPipelineSubsystem::EnsureSharedFlowField(
  const FCrowdDemoSharedFlowFieldConfig& Config)
{
  const bool bNeedsRebuild = !SharedFlowField.IsValid()
    || SharedFlowField.Config.Revision != Config.Revision
    || !FVector(SharedFlowField.Config.GoalLocation).Equals(FVector(Config.GoalLocation), 0.01f);
  if (bNeedsRebuild)
  {
    if (!FCrowdDemoSharedFlowFieldKernel::Build(Config, SharedFlowField))
    {
      return false;
    }
    ++SharedFlowFieldRebuildCount;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSharedFlowField: role=%s revision=%d hash=%u rebuild_count=%d cells=%d blocked=%d goal_cell=%d"),
      GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
      Config.Revision,
      SharedFlowField.BuildHash,
      SharedFlowFieldRebuildCount,
      SharedFlowField.Width * SharedFlowField.Height,
      SharedFlowField.BlockedCellCount,
      SharedFlowField.GoalCellIndex);
  }
  LastCompareMetrics.FlowFieldRevision = SharedFlowField.Config.Revision;
  LastCompareMetrics.FlowFieldBuildHash = SharedFlowField.BuildHash;
  LastCompareMetrics.FlowFieldRebuildCount = SharedFlowFieldRebuildCount;
  LastCompareMetrics.FlowBlockedCellCount = SharedFlowField.BlockedCellCount;
  return SharedFlowField.IsValid();
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPositioningMetrics(
  const FCrowdDemoPositioningSummary& Summary,
  const int32 StableOccupiedCount,
  const int32 ReserveHoldCount,
  const int32 ChurnCount,
  const float ArrivalErrorP95)
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.PositionCandidateCount = Summary.CandidateCount;
  Metrics.PositionFrontCapacity = Summary.FrontCapacity;
  Metrics.PositionReserveCapacity = Summary.ReserveCapacity;
  Metrics.PositionAssignedCount = Summary.AssignedCount;
  Metrics.PositionUnassignedCount = Summary.UnassignedCount;
  Metrics.PositionAssignmentReusedCount = Summary.ReusedCount;
  Metrics.PositionAssignmentChangedCount += Summary.ChangedCount;
  Metrics.PositionInvalidatedCount += Summary.InvalidatedCount;
  Metrics.PositionCandidateOverlapCount = Summary.CandidateOverlapCount;
  Metrics.PositionCandidateUnreachableCount = Summary.CandidateUnreachableCount;
  Metrics.PositionCandidateHash = Summary.CandidateHash != 0
    ? Summary.CandidateHash : Metrics.PositionCandidateHash;
  Metrics.PositionAssignmentHash = Summary.AssignmentHash != 0
    ? Summary.AssignmentHash : Metrics.PositionAssignmentHash;
  Metrics.PositionStableOccupiedCount = StableOccupiedCount;
  Metrics.PositionReserveHoldCount = ReserveHoldCount;
  Metrics.PositionAssignmentChurnCount = ChurnCount;
  Metrics.PositionArrivalErrorCmP95 = ArrivalErrorP95;
  uint32 TargetHash = 2166136261u;
  TargetHash = FoldHash(TargetHash, static_cast<uint32>(PursuitTargetFact.TargetId));
  TargetHash = FoldHash(TargetHash, static_cast<uint32>(FMath::RoundToInt(PursuitTargetFact.Location.X)));
  TargetHash = FoldHash(TargetHash, static_cast<uint32>(FMath::RoundToInt(PursuitTargetFact.Location.Y)));
  TargetHash = FoldHash(TargetHash, static_cast<uint32>(PursuitTargetFact.Revision));
  Metrics.TargetFactHash = TargetHash;
  Metrics.TargetRevisionCount = PursuitTargetFact.Revision;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTransitCapacitySelection()
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.TransitCapacityPositionCount = TransitCapacitySelection.PositionCapacity;
  Metrics.TransitCapacityHoldingCount = TransitCapacitySelection.HoldingCapacity;
  Metrics.TransitCapacityPositionDeficit = TransitCapacitySelection.PositionCapacityDeficit;
  Metrics.TransitCapacityHoldingDeficit = TransitCapacitySelection.HoldingCapacityDeficit;
  Metrics.TransitCapacitySelectionHash = TransitCapacitySelection.CapacityHash;
  Metrics.bTransitCapacitySelectionApplied = TransitCapacitySelection.bValid
    && TransitCapacitySelection.PositionCapacityDeficit == 0
    && TransitCapacitySelection.HoldingCapacityDeficit == 0 ? 1 : 0;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPositionPromotionTransitions(
  const TConstArrayView<int32> AgentIds)
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  for (const int32 AgentId : AgentIds)
  {
    if (PositionPromotedAgentIds.Contains(AgentId)) continue;
    PositionPromotedAgentIds.Add(AgentId);
    ++Metrics.PositionPromotionTransitionCount;
  }
  Metrics.PositionPromotionAgentCount = PositionPromotedAgentIds.Num();
  Metrics.PositionPromotionCount = Metrics.PositionPromotionTransitionCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSteeringFirstMetrics(
  const uint32 InSteeringHash, const int32 PursuitCount, const int32 HoldingCount,
  const int32 CommitCount, const int32 StableCount, const int32 ReserveCount,
  const int32 ReacquireCount, const int32 ArrivedCount, const int32 ReleaseCount,
  const int32 CommitReleaseCount, const int32 NoProgressCount,
  const int32 GhostOwnerCount)
{
  SteeringStateHash = InSteeringHash;
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.HoldingCandidateCount = HoldingSummary.CandidateCount;
  Metrics.HoldingCompatibilityEdgeCount = HoldingSummary.CompatibilityCount;
  Metrics.HoldingAssignedAgentCount = HoldingSummary.AssignedCount;
  Metrics.HoldingAllocationFailureCount = HoldingSummary.UnassignedCount;
  Metrics.HoldingSelectedCompatibilityValidCount = HoldingSummary.SelectedCompatibilityValidCount;
  Metrics.HoldingSelectedCompatibilityInvalidCount = HoldingSummary.SelectedCompatibilityInvalidCount;
  Metrics.HoldingDuplicateCompatibilityKeyCount = HoldingSummary.DuplicateCompatibilityKeyCount;
  Metrics.HoldingArrivedAgentCount = FMath::Max(Metrics.HoldingArrivedAgentCount, ArrivedCount);
  Metrics.CommitRequestCount += PreparedCommitRequests.Num();
  Metrics.CommitGrantedCount += PreparedCommitGateResult.GrantedAgentIds.Num();
  Metrics.CommitHeldCount += PreparedCommitGateResult.HeldCount;
  Metrics.CommitInvalidCount += PreparedCommitGateResult.ReacquireCount;
  Metrics.CommitInvalidPositionCount += PreparedCommitGateResult.InvalidPositionCount;
  Metrics.CommitTargetRevisionMismatchCount += PreparedCommitGateResult.TargetRevisionMismatchCount;
  Metrics.CommitCompatibilityMissingCount += PreparedCommitGateResult.CompatibilityMissingCount;
  Metrics.CommitCompatibilityRejectedCount += PreparedCommitGateResult.CompatibilityRejectedCount;
  Metrics.CommitUniqueAgentCount = FMath::Max(Metrics.CommitUniqueAgentCount,
    PreparedCommitGateResult.GrantedAgentIds.Num());
  Metrics.SteeringStatePursuitCount = PursuitCount;
  Metrics.SteeringStateHoldingCount = HoldingCount;
  Metrics.SteeringStateCommitCount = CommitCount;
  Metrics.SteeringStateStableCount = StableCount;
  Metrics.SteeringStateReserveCount = ReserveCount;
  Metrics.SteeringStateReacquireCount = ReacquireCount;
  Metrics.HoldingReleaseCount += ReleaseCount;
  Metrics.CommitReleaseCount += CommitReleaseCount;
  Metrics.PositioningNoProgressAgentCount = NoProgressCount;
  Metrics.GhostOwnerCount = GhostOwnerCount;
  Metrics.HoldingCandidateHash = HoldingSummary.CandidateHash;
  Metrics.HoldingAssignmentHash = HoldingSummary.AssignmentHash;
  Metrics.CommitDecisionHash = PreparedCommitGateResult.DecisionHash;
  Metrics.SteeringStateHash = InSteeringHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSteeringFirstRuntimeDiagnostic(
  const FCrowdDemoSteeringFirstRuntimeDiagnostic& Diagnostic)
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.SteeringStateFinalCounts = Diagnostic.StateCounts;
  Metrics.SteeringStateDistanceCmP50 = Diagnostic.DistanceP50;
  Metrics.SteeringStateDistanceCmP95 = Diagnostic.DistanceP95;
  Metrics.SteeringStatePreferredForwardCmpsP50 = Diagnostic.PreferredForwardP50;
  Metrics.SteeringStatePreferredForwardCmpsP95 = Diagnostic.PreferredForwardP95;
  Metrics.SteeringStateOrcaForwardCmpsP50 = Diagnostic.OrcaForwardP50;
  Metrics.SteeringStateOrcaForwardCmpsP95 = Diagnostic.OrcaForwardP95;
  Metrics.SteeringStateFinalForwardCmpsP50 = Diagnostic.FinalForwardP50;
  Metrics.SteeringStateFinalForwardCmpsP95 = Diagnostic.FinalForwardP95;
  Metrics.PursuitOutsideHandoffCount = Diagnostic.PursuitOutsideHandoffCount;
  Metrics.PursuitInvalidFlowCount = Diagnostic.PursuitInvalidFlowCount;
  Metrics.HoldingFinalDistanceNotReadyCount =
    PreparedCommitGateResult.HoldingDistanceNotReadyCount;
  Metrics.HoldingFinalSpeedNotReadyCount =
    PreparedCommitGateResult.HoldingSpeedNotReadyCount;
  Metrics.HoldingFinalReadyConflictCount = PreparedCommitGateResult.ReadyConflictHeldCount;
  Metrics.HoldingFinalReadyGrantedCount = PreparedCommitGateResult.ReadyGrantedCount;
  Metrics.HoldingFinalTargetRejectCount = PreparedCommitGateResult.ReadyTargetRejectCount;
  Metrics.HoldingFinalFlowRejectCount = PreparedCommitGateResult.ReadyFlowRejectCount;
  Metrics.HoldingFinalObstacleRejectCount = PreparedCommitGateResult.ReadyObstacleRejectCount;
  Metrics.HoldingFinalStableBlockerRejectCount =
    PreparedCommitGateResult.ReadyStableBlockerRejectCount;
  Metrics.HoldingFinalReserveBlockerRejectCount =
    PreparedCommitGateResult.ReadyReserveBlockerRejectCount;
  Metrics.HoldingFinalActiveCommitConflictCount =
    PreparedCommitGateResult.ReadyActiveCommitConflictCount;
  Metrics.HoldingFinalSelectedConflictCount = PreparedCommitGateResult.ReadySelectedConflictCount;
  Metrics.CommitGateYieldableStableConflictCount =
    PreparedCommitGateResult.YieldableStableConflictCount;
  Metrics.CommitGateYieldableReserveConflictCount =
    PreparedCommitGateResult.YieldableReserveConflictCount;
  Metrics.CommitGateHardConflictHeldCount = PreparedCommitGateResult.HardConflictHeldCount;
  Metrics.CommitPreferredNonzeroOrcaZeroCount =
    Diagnostic.CommitPreferredNonzeroOrcaZeroCount;
  Metrics.CommitRouteForwardSpeedCmpsP50 = Diagnostic.CommitRouteForwardSpeedCmpsP50;
  Metrics.CommitRouteForwardSpeedCmpsP95 = Diagnostic.CommitRouteForwardSpeedCmpsP95;
  Metrics.StablePhysicalDisplacedCount = Diagnostic.StablePhysicalDisplacedCount;
  Metrics.StablePhysicalDisplacementCmP95 = Diagnostic.StablePhysicalDisplacementCmP95;
  Metrics.StablePhysicalDisplacementCmMax = Diagnostic.StablePhysicalDisplacementCmMax;
  Metrics.ReservePhysicalDisplacedCount = Diagnostic.ReservePhysicalDisplacedCount;
  Metrics.ReservePhysicalDisplacementCmP95 = Diagnostic.ReservePhysicalDisplacementCmP95;
  Metrics.ReservePhysicalDisplacementCmMax = Diagnostic.ReservePhysicalDisplacementCmMax;
  Metrics.PhysicallySatisfiedPositionCount = Diagnostic.PhysicallySatisfiedPositionCount;
  Metrics.SteeringStateOrcaConstraintSourceMatrix = Diagnostic.OrcaConstraintSourceMatrix;
  Metrics.SteeringStateOrcaInfeasibleCounts = Diagnostic.OrcaInfeasibleCounts;
  Metrics.SteeringStateOrcaFallbackStopCounts = Diagnostic.OrcaFallbackStopCounts;
  Metrics.SteeringReacquireReasonCounts = Diagnostic.ReacquireReasonCounts;
  Metrics.SteeringCommitArrivalErrorCmP95 = Diagnostic.CommitArrivalErrorCmP95;
  Metrics.SteeringCommitNoProgressStepsMax = Diagnostic.CommitNoProgressStepsMax;
  Metrics.SteeringCommitObstacleCorrectionCmP95 = Diagnostic.CommitObstacleCorrectionCmP95;
  Metrics.SteeringCommitPbdCorrectionCmP95 = Diagnostic.CommitPbdCorrectionCmP95;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordResidualPositioningSummary(
  const FCrowdDemoResidualPositioningSummary& Summary)
{
  ResidualPositioningSummary = Summary;
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.ResidualUnfinishedAgentCount = Summary.UnfinishedAgentCount;
  M.ResidualRemainingPositionCount = Summary.RemainingPositionCount;
  M.ResidualCompatibleEdgeCount = Summary.CompatibleEdgeCount;
  M.ResidualMaximumMatchingCount = Summary.MaximumMatchingCount;
  M.ResidualAgentWithoutHoldingCount = Summary.AgentWithoutHoldingCount;
  M.ResidualAgentWithoutPositionEdgeCount = Summary.AgentWithoutPositionEdgeCount;
  M.ResidualAgentWithoutCommitRouteCount = Summary.AgentWithoutCommitRouteCount;
  M.ResidualStableBlockerEdgeRejectCount = Summary.StableBlockerEdgeRejectCount;
  M.ResidualReserveBlockerEdgeRejectCount = Summary.ReserveBlockerEdgeRejectCount;
  M.ResidualTargetRejectCount = Summary.TargetRejectCount;
  M.ResidualObstacleRejectCount = Summary.ObstacleRejectCount;
  M.ResidualFlowRejectCount = Summary.FlowRejectCount;
  M.ResidualRevisionRejectCount = Summary.RevisionRejectCount;
  M.ResidualCurrentMatching = Summary.CurrentMatching;
  M.ResidualNoStableMatching = Summary.NoStableMatching;
  M.ResidualNoReserveMatching = Summary.NoReserveMatching;
  M.ResidualBestSingleBlockerRemovalGain = Summary.BestSingleBlockerRemovalGain;
  M.ResidualBlockerCriticalCount = Summary.BlockerCriticalCount;
  M.ResidualTargetLimitedCount = Summary.TargetLimitedCount;
  M.ResidualGeometryLimitedCount = Summary.GeometryLimitedCount;
  M.ResidualCapacityHash = Summary.ResidualCapacityHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordHoldingMatchingDiagnostic(
  const int32 PositionValidCount, const int32 GreedyCount)
{
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.ResidualPositionValidCount = PositionValidCount;
  M.ResidualHoldingMatchingCount = HoldingMatchingResult.MaximumCardinality;
  M.ResidualJointFeasibleCount = FMath::Min(PositionValidCount,
    HoldingMatchingResult.MaximumCardinality);
  M.ResidualGreedyHoldingCount = GreedyCount;
  M.ResidualHoldingMatchingHash = HoldingMatchingResult.MatchingHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordHoldingHallDiagnostic()
{
  const FCrowdDemoHoldingHallSummary& S = HoldingHallFixture.Summary;
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.HoldingHallCurrentMatchingCount = S.CurrentMatchingCount;
  M.HoldingHallNoStableOwnerMatchingCount = S.NoStableOwnerMatchingCount;
  M.HoldingHallNoReserveOwnerMatchingCount = S.NoReserveOwnerMatchingCount;
  M.HoldingHallNoCommitOwnerMatchingCount = S.NoCommitOwnerMatchingCount;
  M.HoldingHallAgentCount = S.HallAgentCount;
  M.HoldingHallAvailableHoldingCount = S.HallAvailableHoldingCount;
  M.HoldingHallDeficiency = S.HallDeficiency;
  M.HoldingHallMissingCompatibilityRecordCount = S.MissingCompatibilityRecordCount;
  M.HoldingHallFlowRejectCount = S.FlowRejectCount;
  M.HoldingHallTargetRejectCount = S.TargetRejectCount;
  M.HoldingHallObstacleRejectCount = S.ObstacleRejectCount;
  M.HoldingHallRevisionRejectCount = S.RevisionRejectCount;
  M.HoldingHallStableOwnerRejectCount = S.StableOwnerRejectCount;
  M.HoldingHallReserveOwnerRejectCount = S.ReserveOwnerRejectCount;
  M.HoldingHallFixtureHash = S.StableHash;
  M.HoldingHallFullDeficiency = S.FullHallDeficiency;
  M.HoldingHallOwnerReleaseStableMatchingCount = S.OwnerReleaseStableMatchingCount;
  M.HoldingHallOwnerReleaseReserveMatchingCount = S.OwnerReleaseReserveMatchingCount;
  M.HoldingHallOwnerReleaseCommitMatchingCount = S.OwnerReleaseCommitMatchingCount;
  M.HoldingHallPhysicalStableRemovalMatchingCount = S.PhysicalStableBlockerRemovalMatchingCount;
  M.HoldingHallPhysicalReserveRemovalMatchingCount = S.PhysicalReserveBlockerRemovalMatchingCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordHallGeometryDiagnostic()
{
  const FCrowdDemoHallGeometryFixture& F = HallGeometryFixture;
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.HallGeometryAgentId = F.AgentId;
  M.HallGeometryPositionId = F.PositionId;
  M.HallGeometryBestHoldingId = F.BestHoldingId;
  M.HallGeometryBestBlockerAgentId = F.BestBlockerAgentId;
  M.HallGeometryBestClearanceMarginCm = F.BestClearanceMarginCm;
  M.HallGeometryNonNegativeMarginHoldingCount = F.NonNegativeMarginHoldingCount;
  M.HallGeometryTargetOnlyRejectCount = F.TargetOnlyRejectCount;
  M.HallGeometryStableOnlyRejectCount = F.StableOnlyRejectCount;
  M.HallGeometryMultiLabelRejectCount = F.MultiLabelRejectCount;
  M.HallGeometrySelfBlockerCount = F.SelfBlockerCount;
  M.HallGeometryWitnessPositionBlockerCount = F.BlockerUsesWitnessPositionCount;
  M.HallGeometryDuplicateBlockerCount = F.DuplicateBlockerCount;
  M.HallGeometryStaleBlockerCount = F.StaleBlockerCount;
  M.HallGeometryRadiusSemanticsErrorCount = F.RadiusSemanticsErrorCount;
  M.HallGeometryEndpointContactCount = F.EndpointContactCount;
  M.HallGeometryFormalMismatchCount = F.FormalClassificationMismatchCount;
  M.HallGeometryFixtureHash = F.FixtureHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordJointPositioningDiagnostic()
{
  const FCrowdDemoJointPositioningResult& R = JointPositioningResult;
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.JointPositioningMaximumCardinality = R.MaximumCardinality;
  M.JointPositioningHardLockedCount = R.HardLockedCount;
  M.JointPositioningReusedCombinationCount = R.ReusedCombinationCount;
  M.JointPositioningUnmatchedAgentCount = R.UnmatchedAgentCount;
  M.JointPositioningDuplicateHoldingCount = R.DuplicateHoldingCount;
  M.JointPositioningDuplicatePositionCount = R.DuplicatePositionCount;
  M.JointPositioningHash = R.StableHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordJointCommitResidualDiagnostic()
{
  const FCrowdDemoJointCommitResidualResult& R=JointCommitResidualResult;
  FCrowdDemoTrafficMetrics& M=LastCompareMetrics.TrafficMetrics;
  M.JointCommitResidualCandidateCount=R.CandidateCount;
  M.JointCommitResidualFeasibleCount=R.FeasibleCount;
  M.JointCommitResidualInfeasibleCount=R.InfeasibleCount;
  M.JointCommitResidualHash=R.StableHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordUnfinishedBoundaryDiagnostic()
{
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.Sf4UnfinishedBoundaryAgentCount = UnfinishedBoundaryFixture.Agents.Num();
  M.Sf4UnfinishedBoundaryHash = UnfinishedBoundaryFixture.StableHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPhysicalUnsatisfiedBoundaryDiagnostic()
{
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.Sf4PhysicalUnsatisfiedAgentCount = PhysicalUnsatisfiedBoundaryFixture.Agents.Num();
  M.Sf4PhysicalUnsatisfiedTotalAgentCount =
    PhysicalUnsatisfiedBoundaryFixture.TotalAgentCount;
  M.Sf4PhysicalUnsatisfiedSatisfiedCount =
    PhysicalUnsatisfiedBoundaryFixture.PhysicallySatisfiedCount;
  M.Sf4PhysicalUnsatisfiedCountClosed =
    PhysicalUnsatisfiedBoundaryFixture.bCountClosed ? 1 : 0;
  M.Sf4PhysicalUnsatisfiedHash = PhysicalUnsatisfiedBoundaryFixture.StableHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordFrontAdmission(
  const FCrowdDemoFrontAdmissionResult& Result)
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.PositionFrontAdmissionGrantCount += Result.GrantedAgentIds.Num();
  Metrics.PositionFrontAdmissionRequeueCount += Result.RequeuedAgentIds.Num();
  Metrics.PositionFrontAdmissionDecisionHash = FoldHash(
    FoldHash(Metrics.PositionFrontAdmissionDecisionHash,
      static_cast<uint32>(GetCurrentFixedStepIndex())), Result.DecisionHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordFrontPhaseReservationSchedule(
  const TConstArrayView<FCrowdDemoFrontPhaseReservationRequest> Requests,
  const TConstArrayView<FCrowdDemoFrontPhaseReservationDecisionRecord> Decisions,
  const uint32 DecisionHash)
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  for (const FCrowdDemoFrontPhaseReservationRequest& Request : Requests)
    Metrics.PhaseReservationRequestCount += Request.bHasRequest ? 1 : 0;
  for (const FCrowdDemoFrontPhaseReservationDecisionRecord& Decision : Decisions)
  {
    Metrics.PhaseReservationGrantedCount += Decision.Decision
      == ECrowdDemoFrontPhaseReservationDecision::Granted ? 1 : 0;
    Metrics.PhaseReservationHeldCount += Decision.Decision
      == ECrowdDemoFrontPhaseReservationDecision::Held ? 1 : 0;
    Metrics.PhaseReservationInvalidCount += Decision.Decision
      == ECrowdDemoFrontPhaseReservationDecision::Invalid ? 1 : 0;
    Metrics.PhaseReservationTargetExclusionRejectCount += Decision.Reason
      == ECrowdDemoFrontPhaseReservationReason::TargetExclusion ? 1 : 0;
    Metrics.PhaseReservationRouteConflictCount += Decision.Reason
      == ECrowdDemoFrontPhaseReservationReason::RouteConflict ? 1 : 0;
  }
  Metrics.PhaseReservationDecisionHash = FoldHash(
    FoldHash(Metrics.PhaseReservationDecisionHash,
      static_cast<uint32>(GetCurrentFixedStepIndex())), DecisionHash);
  Metrics.PhaseReservationClientHashMatch = 1;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordFrontPhaseReservationTransitions(
  const int32 TransitionCount,
  const TConstArrayView<int32> HeldSteps)
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.PhaseReservationTransitionCount += TransitionCount;
  for (const int32 Steps : HeldSteps)
    PhaseReservationHeldStepSamples.Add(static_cast<float>(Steps));
  TArray<float> Sorted = PhaseReservationHeldStepSamples;
  Sorted.Sort();
  Metrics.PhaseReservationHeldStepsP95 = Sorted.IsEmpty() ? 0.0f : Sorted[FMath::Clamp(
    FMath::CeilToInt(static_cast<float>(Sorted.Num()) * 0.95f) - 1, 0, Sorted.Num() - 1)];
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordFrontReservationWaitGraph(
  const FCrowdDemoFrontReservationWaitGraphSummary& Summary,
  const FCrowdDemoFrontReservationWaitGraphFixture& Fixture)
{
  FrontReservationWaitGraphSummary = Summary;
  if (Fixture.bValid && (!FrontReservationWaitGraphFixture.bValid
      || Fixture.Agents.Num() < FrontReservationWaitGraphFixture.Agents.Num()
      || (Fixture.Agents.Num() == FrontReservationWaitGraphFixture.Agents.Num()
        && Fixture.StableHash < FrontReservationWaitGraphFixture.StableHash)))
  {
    FrontReservationWaitGraphFixture = Fixture;
  }
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.PhaseReservationUniqueBlockedRequestCount = FMath::Max(
    Metrics.PhaseReservationUniqueBlockedRequestCount, Summary.UniqueBlockedRequestCount);
  Metrics.PhaseReservationUniqueBlockerCount = FMath::Max(
    Metrics.PhaseReservationUniqueBlockerCount, Summary.UniqueBlockerCount);
  Metrics.PhaseReservationWaitEdgeCount = FMath::Max(
    Metrics.PhaseReservationWaitEdgeCount, Summary.WaitEdgeCount);
  Metrics.PhaseReservationReciprocalEdgeCount = FMath::Max(
    Metrics.PhaseReservationReciprocalEdgeCount, Summary.ReciprocalEdgeCount);
  Metrics.PhaseReservationCycleCount = FMath::Max(
    Metrics.PhaseReservationCycleCount, Summary.CycleCount);
  Metrics.PhaseReservationMaxCycleSize = FMath::Max(
    Metrics.PhaseReservationMaxCycleSize, Summary.MaxCycleSize);
  Metrics.PhaseReservationStalledBlockerCount = FMath::Max(
    Metrics.PhaseReservationStalledBlockerCount, Summary.StalledBlockerCount);
  Metrics.PhaseReservationProgressingBlockerCount = FMath::Max(
    Metrics.PhaseReservationProgressingBlockerCount, Summary.ProgressingBlockerCount);
  Metrics.PhaseReservationStaleOwnerCount = FMath::Max(
    Metrics.PhaseReservationStaleOwnerCount, Summary.StaleOwnerCount);
  Metrics.PhaseReservationBlockerRadialCount = FMath::Max(
    Metrics.PhaseReservationBlockerRadialCount, Summary.BlockerRadialCount);
  Metrics.PhaseReservationBlockerAngularCount = FMath::Max(
    Metrics.PhaseReservationBlockerAngularCount, Summary.BlockerAngularCount);
  Metrics.PhaseReservationBlockerRadialCommitCount = FMath::Max(
    Metrics.PhaseReservationBlockerRadialCommitCount, Summary.BlockerRadialCommitCount);
  Metrics.PhaseReservationAtomicHandoffCycleCount = FMath::Max(
    Metrics.PhaseReservationAtomicHandoffCycleCount, Summary.AtomicHandoffCycleCount);
  Metrics.PhaseReservationMaxAtomicHandoffSetSize = FMath::Max(
    Metrics.PhaseReservationMaxAtomicHandoffSetSize, Summary.MaxAtomicHandoffSetSize);
  Metrics.PhaseReservationWaitGraphHash = FoldHash(
    FoldHash(Metrics.PhaseReservationWaitGraphHash,
      static_cast<uint32>(GetCurrentFixedStepIndex())), Summary.WaitGraphHash);
  Metrics.PhaseReservationWaitGraphClientHashMatch = 1;
  if (FrontReservationWaitGraphFixture.bValid)
  {
    Metrics.PhaseReservationWaitGraphFixtureHash = FrontReservationWaitGraphFixture.StableHash;
    Metrics.PhaseReservationWaitGraphFixtureAgentCount =
      FrontReservationWaitGraphFixture.Agents.Num();
    Metrics.PhaseReservationWaitGraphFixtureEdgeCount =
      FrontReservationWaitGraphFixture.Edges.Num();
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPositionIngressDiagnostic(
  const FCrowdDemoPositionIngressSummary& Summary,
  const FCrowdDemoPositionIngressFixture& Fixture,
  TMap<int32, int32>&& LowSpeedStepsByAgentId)
{
  if (!IsSf4IngressDiagnosticEnabled()) return;
  LastPositionIngressSummary = Summary;
  if (Fixture.bValid && (!MinimumPositionIngressFixture.bValid
      || Fixture.ConstraintCount < MinimumPositionIngressFixture.ConstraintCount
      || (Fixture.ConstraintCount == MinimumPositionIngressFixture.ConstraintCount
        && Fixture.StableHash < MinimumPositionIngressFixture.StableHash)))
  {
    MinimumPositionIngressFixture = Fixture;
  }
  PositionIngressLowSpeedStepsByAgentId = MoveTemp(LowSpeedStepsByAgentId);
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.PositionIngressSlotCommitCount = Summary.SlotCommitCount;
  Metrics.PositionIngressErrorOver300Count = Summary.SlotCommitErrorOver300Count;
  Metrics.PositionIngressTargetBlockedCount = Summary.DirectPathTargetBlockedCount;
  Metrics.PositionIngressStableBlockedCount = Summary.DirectPathStableBlockedCount;
  Metrics.PositionIngressReserveBlockedCount = Summary.DirectPathReserveBlockedCount;
  Metrics.PositionIngressCommitBlockedCount = Summary.DirectPathCommitBlockedCount;
  Metrics.PositionIngressStableBlockerPairCount = Summary.StableBlockerPairCount;
  Metrics.PositionIngressReserveBlockerPairCount = Summary.ReserveBlockerPairCount;
  Metrics.PositionIngressCommitBlockerPairCount = Summary.CommitBlockerPairCount;
  Metrics.PositionIngressSectorDeltaP50 = Summary.AssignedSectorDeltaP50;
  Metrics.PositionIngressSectorDeltaP95 = Summary.AssignedSectorDeltaP95;
  Metrics.PositionIngressSectorDeltaMax = Summary.AssignedSectorDeltaMax;
  Metrics.PositionIngressRadialDeltaP50 = Summary.AssignedRadialDeltaP50;
  Metrics.PositionIngressRadialDeltaP95 = Summary.AssignedRadialDeltaP95;
  Metrics.PositionIngressRadialDeltaMax = Summary.AssignedRadialDeltaMax;
  Metrics.PositionIngressUnblockedAlternativeFrontCount = Summary.UnblockedAlternativeFrontCount;
  Metrics.PositionIngressSameSideAlternativeFrontCount = Summary.SameSideAlternativeFrontCount;
  Metrics.PositionIngressNoAlternativeFrontCount = Summary.NoAlternativeFrontCount;
  Metrics.PositionIngressOrcaFromStableCount = Summary.OrcaConstraintsFromStableCount;
  Metrics.PositionIngressOrcaFromReserveCount = Summary.OrcaConstraintsFromReserveCount;
  Metrics.PositionIngressOrcaFromCommitCount = Summary.OrcaConstraintsFromCommitCount;
  Metrics.PositionIngressOrcaFromOtherCount = Summary.OrcaConstraintsFromOtherCount;
  Metrics.PositionIngressPreferredSpeedP95 = Summary.SlotCommitPreferredSpeedP95;
  Metrics.PositionIngressOrcaSpeedP95 = Summary.SlotCommitOrcaSpeedP95;
  Metrics.PositionIngressObstacleSpeedP95 = Summary.SlotCommitObstacleSpeedP95;
  Metrics.PositionIngressFinalSpeedP95 = Summary.SlotCommitFinalSpeedP95;
  Metrics.PositionIngressLowSpeedStepsMax = Summary.SlotCommitLowSpeedStepsMax;
  Metrics.PositionIngressTargetExclusionCrossingCount = Summary.TargetExclusionCrossingCount;
  Metrics.PositionIngressOrderInversionCount = Summary.IngressOrderInversionCount;
  Metrics.PositionIngressPbdPushAwayCount = Summary.PbdPushAwayCount;
  Metrics.PositionIngressObstaclePushAwayCount = Summary.ObstaclePushAwayCount;
  Metrics.PositionIngressMinimumFixtureHash = MinimumPositionIngressFixture.StableHash;
  Metrics.PositionIngressMinimumFixtureConstraintCount = MinimumPositionIngressFixture.ConstraintCount;
  Metrics.PositionIngressEvaluationHash = Summary.EvaluationHash;
  Metrics.PositionFrontAssignedWaitingCount = Summary.FrontAssignedWaitingCount;
  Metrics.PositionFrontRadialStageCount = Summary.RadialStageCount;
  Metrics.PositionFrontAngularAlignCount = Summary.AngularAlignCount;
  Metrics.PositionFrontRadialCommitCount = Summary.RadialCommitCount;
  Metrics.PositionFrontGateInvalidCount = Summary.GateInvalidCount;
  Metrics.PositionFrontRadialCommitBlockedCount = Summary.RadialCommitBlockedCount;
  Metrics.PositionFrontRouteHash = Summary.RouteHash;
  Metrics.PositionFrontRadialPreferredSpeedP95 = Summary.RadialPreferredSpeedP95;
  Metrics.PositionFrontRadialOrcaSpeedP95 = Summary.RadialOrcaSpeedP95;
  Metrics.PositionFrontRadialFinalSpeedP95 = Summary.RadialFinalSpeedP95;
  Metrics.PositionFrontRadialOrcaForwardSpeedP50 = Summary.RadialOrcaForwardSpeedP50;
  Metrics.PositionFrontRadialOrcaForwardSpeedMin = Summary.RadialOrcaForwardSpeedMin;
  Metrics.PositionFrontRadialFinalForwardSpeedP50 = Summary.RadialFinalForwardSpeedP50;
  Metrics.PositionFrontRadialFinalForwardSpeedMin = Summary.RadialFinalForwardSpeedMin;
  Metrics.PositionFrontRadialOrcaConstraintP95 = Summary.RadialOrcaConstraintP95;
  Metrics.PositionFrontRadialConstraintFromActiveCount = Summary.RadialConstraintFromActiveCount;
  Metrics.PositionFrontRadialConstraintFromWaitingCount = Summary.RadialConstraintFromWaitingCount;
  Metrics.PositionFrontRadialConstraintFromReserveCommitCount = Summary.RadialConstraintFromReserveCommitCount;
  Metrics.PositionFrontRadialConstraintFromStableCount = Summary.RadialConstraintFromStableCount;
  Metrics.PositionFrontRadialConstraintFromOtherCount = Summary.RadialConstraintFromOtherCount;
  Metrics.PositionFrontRadialErrorP50 = Summary.RadialErrorP50;
  Metrics.PositionFrontRadialErrorP95 = Summary.RadialErrorP95;
  Metrics.PositionFrontRadialErrorMax = Summary.RadialErrorMax;
  Metrics.PositionFrontRadialErrorImprovedCount = Summary.RadialErrorImprovedCount;
  Metrics.PositionFrontRadialQuantizedProgressStallCount = Summary.RadialQuantizedProgressStallCount;
  Metrics.PositionFrontComposeBoundarySwitchCount = Summary.ComposeBoundarySwitchCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSf4ReservationOrcaDiagnostic(
  const FCrowdDemoSf4ReservationOrcaDiagnosticFixture& Fixture)
{
  if (!IsSf4ReservationOrcaDiagnosticEnabled()
    || Sf4ReservationOrcaCapturedRoundId == GetCurrentRoundId()) return;
  Sf4ReservationOrcaCapturedRoundId = GetCurrentRoundId();
  Sf4ReservationOrcaDiagnosticFixture = Fixture;
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.Sf4ReservationOrcaFixtureValid = Fixture.bValid ? 1 : 0;
  Metrics.Sf4ReservationOrcaFixtureTooLarge = Fixture.Summary.bFixtureTooLarge ? 1 : 0;
  Metrics.Sf4ReservationOrcaFixtureAgentCount = Fixture.Agents.Num();
  Metrics.Sf4ReservationOrcaCoreConstraintCount = Fixture.CoreConstraints.Num();
  Metrics.Sf4ReservationOrcaActiveConflictCount = Fixture.Summary.ActiveRouteConflictCount;
  Metrics.Sf4ReservationOrcaActiveDisjointContainedCount =
    Fixture.Summary.ActiveRouteDisjointContainedCount;
  Metrics.Sf4ReservationOrcaActiveOutsideCorridorCount =
    Fixture.Summary.ActiveRouteDisjointOutsideCorridorCount;
  Metrics.Sf4ReservationOrcaWaitingCount = Fixture.Summary.WaitingCount;
  Metrics.Sf4ReservationOrcaStableCount = Fixture.Summary.StableCount;
  Metrics.Sf4ReservationOrcaOtherCount = Fixture.Summary.OtherCount;
  Metrics.Sf4ReservationOrcaFixtureHash = Fixture.StableHash;
  Metrics.Sf4ReservationOrcaClientHashMatch = 1;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTransitJointDiagnostic(
  const FCrowdDemoTransitJointDiagnosticFixture& Fixture)
{
  if (!IsTransitJointDiagnosticEnabled()
    || TransitJointDiagnosticCapturedRoundId == GetCurrentRoundId()) return;
  TransitJointDiagnosticCapturedRoundId = GetCurrentRoundId();
  TransitJointDiagnosticFixture = Fixture;
  LastCompletedTransitJointDiagnosticFixture = Fixture;
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.TransitJointFixtureValid = Fixture.bValid ? 1 : 0;
  Metrics.TransitJointFixtureTooLarge = Fixture.Summary.bFixtureTooLarge ? 1 : 0;
  Metrics.TransitJointFixtureAgentCount = Fixture.Summary.ComponentAgentCount;
  Metrics.TransitJointFixturePairCount = Fixture.Summary.ComponentPairCount;
  Metrics.TransitJointFixtureConstraintCount = Fixture.Summary.ConstraintCount;
  Metrics.TransitJointPriorityForwardSpeedCmps = Fixture.Summary.PriorityForwardSpeedCmps;
  Metrics.TransitJointFinalSpeedCmps = Fixture.Summary.FinalSpeedCmps;
  Metrics.TransitJointForwardSpeedCmps = Fixture.Summary.JointForwardSpeedCmps;
  Metrics.TransitJointDownstreamZeroStage =
    static_cast<int32>(Fixture.Summary.DownstreamZeroStage);
  Metrics.TransitJointSafeForward = Fixture.Summary.bJointQuantizedSafeForward ? 1 : 0;
  Metrics.TransitJointFixtureHash = Fixture.StableHash;
  Metrics.TransitJointClientHashMatch = 1;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTransitCapacityShadow(
  const TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
  const TConstArrayView<FCrowdDemoJointVelocityPair> Pairs,
  const TConstArrayView<FCrowdDemoJointVelocityComponent> Components,
  const TConstArrayView<FCrowdDemoJointVelocityComponentResult> Results,
  const FCrowdDemoTransitCapacityShadowSummary& Summary)
{
  if (!IsTransitCapacityShadowEnabled()) return;
  TransitCapacityShadowAgents = TArray<FCrowdDemoJointVelocityAgent>(Agents);
  TransitCapacityShadowPairs = TArray<FCrowdDemoJointVelocityPair>(Pairs);
  TransitCapacityShadowComponents = TArray<FCrowdDemoJointVelocityComponent>(Components);
  TransitCapacityShadowResults = TArray<FCrowdDemoJointVelocityComponentResult>(Results);
  TransitCapacityShadowSummary = Summary;
  TransitCapacityShadowSolverMsSamples.Add(Summary.SolverMs);
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.TransitCapacityShadowComponentCount += Summary.ComponentCount;
  M.TransitCapacityShadowMaximumComponentSize = FMath::Max(
    M.TransitCapacityShadowMaximumComponentSize, Summary.MaximumComponentSize);
  M.TransitCapacityShadowComponent2Count += Summary.Component2Count;
  M.TransitCapacityShadowComponent5Count += Summary.Component5Count;
  M.TransitCapacityShadowComponent8Count += Summary.Component8Count;
  M.TransitCapacityShadowComponent12Count += Summary.Component12Count;
  M.TransitCapacityShadowComponent20Count += Summary.Component20Count;
  M.TransitCapacityShadowOversizeCount += Summary.OversizeCount;
  M.TransitCapacityShadowSolvedCount += Summary.SolvedCount;
  M.TransitCapacityShadowInfeasibleCount += Summary.InfeasibleCount;
  M.TransitCapacityShadowHardInfeasibleCount += Summary.HardInfeasibleCount;
  M.TransitCapacityShadowIterationLimitCount += Summary.IterationLimitCount;
  M.TransitCapacityShadowClearanceNotAchievedCount +=
    Summary.ClearanceNotAchievedCount;
  M.TransitCapacityShadowNoForwardGainCount += Summary.NoForwardGainCount;
  M.TransitCapacityShadowInvalidInputCount += Summary.InvalidInputCount;
  M.TransitCapacityShadowNumericalFailureCount += Summary.NumericalFailureCount;
  M.TransitCapacityShadowQuantizedFailureCount += Summary.QuantizedFailureCount;
  M.TransitCapacityShadowYieldingAgentCount += Summary.YieldingAgentCount;
  M.TransitCapacityShadowDirectRelevantAgentCount +=
    Summary.TransitDirectRelevantAgentCount;
  M.TransitCapacityShadowHardSafetyClosureAgentCount +=
    Summary.HardSafetyClosureAgentCount;
  M.TransitCapacityShadowHardPairViolationCount += Summary.HardPairViolationCount;
  M.TransitCapacityShadowJointCandidateHardPairViolationCount +=
    Summary.JointCandidateHardPairViolationCount;
  M.TransitCapacityShadowBaselineHardPairViolationCount +=
    Summary.BaselineFallbackHardPairViolationCount;
  M.TransitCapacityShadowObstacleViolationCount += Summary.ObstacleViolationCount;
  M.TransitCapacityShadowFlowBoundsViolationCount += Summary.FlowBoundsViolationCount;
  M.TransitCapacityShadowTargetViolationCount += Summary.TargetViolationCount;
  M.TransitCapacityShadowJointCandidateFlowBoundsViolationCount +=
    Summary.JointCandidateFlowBoundsViolationCount;
  M.TransitCapacityShadowJointCandidateObstacleViolationCount +=
    Summary.JointCandidateObstacleViolationCount;
  M.TransitCapacityShadowJointCandidateTargetViolationCount +=
    Summary.JointCandidateTargetViolationCount;
  M.TransitCapacityShadowBaselineFlowBoundsViolationCount +=
    Summary.BaselineFallbackFlowBoundsViolationCount;
  M.TransitCapacityShadowBaselineObstacleViolationCount +=
    Summary.BaselineFallbackObstacleViolationCount;
  M.TransitCapacityShadowBaselineTargetViolationCount +=
    Summary.BaselineFallbackTargetViolationCount;
  M.TransitCapacityShadowPairDoubleOwnerCount += Summary.PairDoubleOwnerCount;
  M.TransitCapacityShadowForwardSpeedRatioQ15 = Summary.TransitForwardSpeedRatioQ15;
  M.TransitCapacityShadowPreferredSpacingDeficitCmMax = FMath::Max(
    M.TransitCapacityShadowPreferredSpacingDeficitCmMax,
    Summary.PreferredSpacingDeficitCmMax);
  M.TransitCapacityShadowApertureDeficitCmMax = FMath::Max(
    M.TransitCapacityShadowApertureDeficitCmMax, Summary.ApertureDeficitCmMax);
  M.TransitCapacityShadowClearanceDeficitCmMax = FMath::Max(
    M.TransitCapacityShadowClearanceDeficitCmMax,
    Summary.TransitCapsuleClearanceDeficitCmMax);
  M.TransitCapacityShadowJointCandidateClearanceDeficitCmMax = FMath::Max(
    M.TransitCapacityShadowJointCandidateClearanceDeficitCmMax,
    Summary.JointCandidateClearanceDeficitCmMax);
  M.TransitCapacityShadowBaselineClearanceDeficitCmMax = FMath::Max(
    M.TransitCapacityShadowBaselineClearanceDeficitCmMax,
    Summary.BaselineFallbackClearanceDeficitCmMax);
  M.TransitCapacityShadowMaximumYieldDisplacementCm = FMath::Max(
    M.TransitCapacityShadowMaximumYieldDisplacementCm,
    Summary.MaximumYieldDisplacementCm);
  M.TransitCapacityShadowSolverMsP95 = Percentile(
    TransitCapacityShadowSolverMsSamples, 0.95f);
  M.TransitCapacityShadowHash = FoldHash(FoldHash(
    M.TransitCapacityShadowHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    Summary.StableHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTransitCapacityFailureFixture(
  const FCrowdDemoTransitCapacityFailureFixture& Fixture)
{
  if (!IsTransitCapacityShadowEnabled() || !Fixture.bValid) return;
  TransitCapacityFailureFixture = Fixture;
  LastCompletedTransitCapacityFailureFixture = Fixture;
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.TransitCapacityFailureFixtureAgentCount = Fixture.Agents.Num();
  M.TransitCapacityFailureFixturePairCount = Fixture.Pairs.Num();
  M.TransitCapacityFailureFixtureStatus = static_cast<int32>(Fixture.Result.Status);
  M.TransitCapacityFailureFixtureHash = Fixture.StableHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordElasticCrowdShadow(
  const FCrowdDemoElasticShadowTwinResult& Twin,
  const int32 ZeroProgressStepMax,
  const float SolverMs)
{
  if (!IsElasticCrowdShadowEnabled() || !Twin.bValid) return;
  ElasticCrowdShadowResults = Twin.Elastic.ElasticResults;
  ElasticCrowdShadowSummary = Twin.Elastic.ElasticSummary;
  for (const FCrowdDemoElasticCrowdResult& Result : Twin.Elastic.ElasticResults)
  {
    ElasticSpacingDeficitSamples.Add(Result.MaxSpacingDeficitCm);
    ElasticTransitDeficitSamples.Add(Result.MaxTransitDeficitCm);
  }
  ElasticSolverMsSamples.Add(SolverMs);
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  constexpr int32 StageCount = static_cast<int32>(ECrowdDemoElasticShadowStage::Count);
  if (M.ElasticBaselineStageHashes.Num() != StageCount)
  {
    M.ElasticBaselineStageHashes.Init(2166136261u, StageCount);
    M.ElasticTwinStageHashes.Init(2166136261u, StageCount);
    M.ElasticBaselineStageHardPairCounts.Init(0, StageCount);
    M.ElasticTwinStageHardPairCounts.Init(0, StageCount);
    M.ElasticBaselineStageTargetCounts.Init(0, StageCount);
    M.ElasticTwinStageTargetCounts.Init(0, StageCount);
    M.ElasticBaselineStageSourceForwardQ15.Init(32767, StageCount);
    M.ElasticTwinStageSourceForwardQ15.Init(32767, StageCount);
  }
  for (int32 Index = 0; Index < StageCount; ++Index)
  {
    const auto& B = Twin.Baseline.StageSummaries[Index];
    const auto& E = Twin.Elastic.StageSummaries[Index];
    ElasticBaselineDesiredForward[Index] += B.DesiredSourceForwardCmps;
    ElasticBaselineActualForward[Index] += B.ActualSourceForwardCmps;
    ElasticTwinDesiredForward[Index] += E.DesiredSourceForwardCmps;
    ElasticTwinActualForward[Index] += E.ActualSourceForwardCmps;
    M.ElasticBaselineStageHashes[Index] = FoldHash(FoldHash(
      M.ElasticBaselineStageHashes[Index], static_cast<uint32>(GetCurrentFixedStepIndex())),
      B.StableHash);
    M.ElasticTwinStageHashes[Index] = FoldHash(FoldHash(
      M.ElasticTwinStageHashes[Index], static_cast<uint32>(GetCurrentFixedStepIndex())),
      E.StableHash);
    M.ElasticBaselineStageHardPairCounts[Index] += B.HardPairViolationCount;
    M.ElasticTwinStageHardPairCounts[Index] += E.HardPairViolationCount;
    M.ElasticBaselineStageTargetCounts[Index] += B.TargetViolationCount;
    M.ElasticTwinStageTargetCounts[Index] += E.TargetViolationCount;
    M.ElasticBaselineStageSourceForwardQ15[Index] = ElasticBaselineDesiredForward[Index] > 0
      ? FMath::Clamp(FMath::RoundToInt(static_cast<double>(ElasticBaselineActualForward[Index])
        * 32767.0 / static_cast<double>(ElasticBaselineDesiredForward[Index])), 0, 131068)
      : 32767;
    M.ElasticTwinStageSourceForwardQ15[Index] = ElasticTwinDesiredForward[Index] > 0
      ? FMath::Clamp(FMath::RoundToInt(static_cast<double>(ElasticTwinActualForward[Index])
        * 32767.0 / static_cast<double>(ElasticTwinDesiredForward[Index])), 0, 131068)
      : 32767;
  }
  const int32 ObstacleStage = static_cast<int32>(ECrowdDemoElasticShadowStage::Obstacle);
  const int32 ReprojectStage = static_cast<int32>(ECrowdDemoElasticShadowStage::Reproject);
  const auto& Obstacle = Twin.Elastic.StageSummaries[ObstacleStage];
  const auto& Reproject = Twin.Elastic.StageSummaries[ReprojectStage];
  M.ElasticObstacleClippedCount += Obstacle.ObstacleClippedCount + Reproject.ObstacleClippedCount;
  M.ElasticObstacleSlideCount += Obstacle.ObstacleSlideCount + Reproject.ObstacleSlideCount;
  M.ElasticObstacleStoppedCount += Obstacle.ObstacleStoppedCount + Reproject.ObstacleStoppedCount;
  M.ElasticObstacleConstraintDeltaCmMax = FMath::Max(M.ElasticObstacleConstraintDeltaCmMax,
    FMath::Max(Obstacle.MaximumObstacleDeltaCm, Reproject.MaximumObstacleDeltaCm));
  M.ElasticOrcaInfeasibleCount += Twin.Elastic.OrcaSummary.InfeasibleAgentCount;
  M.ElasticOrcaFallbackStopCount += Twin.Elastic.OrcaSummary.FallbackStopCount;
  M.ElasticOrcaStopViolationCount += Twin.Elastic.OrcaSummary.StopViolatesConstraintCount;
  const FCrowdDemoElasticCrowdSummary& Summary = Twin.Elastic.ElasticSummary;
  M.ElasticSpacingPairCount = FMath::Max(M.ElasticSpacingPairCount, Summary.SpacingPairCount);
  M.ElasticInfluencedAgentCount = FMath::Max(
    M.ElasticInfluencedAgentCount, Summary.InfluencedAgentCount);
  M.ElasticPropagationLayerMax = FMath::Max(
    M.ElasticPropagationLayerMax, Summary.PropagationLayerCount);
  M.ElasticSpacingDeficitCmP95 = Percentile(ElasticSpacingDeficitSamples, 0.95f);
  M.ElasticSpacingDeficitCmMax = FMath::Max(
    M.ElasticSpacingDeficitCmMax, Summary.MaxSpacingDeficitCm);
  M.ElasticTransitDeficitCmP95 = Percentile(ElasticTransitDeficitSamples, 0.95f);
  M.ElasticTransitDeficitCmMax = FMath::Max(
    M.ElasticTransitDeficitCmMax, Summary.MaxTransitDeficitCm);
  M.ElasticSourceForwardRatioQ15 = M.ElasticTwinStageSourceForwardQ15[ReprojectStage];
  M.ElasticBaselineSourceForwardRatioQ15 =
    M.ElasticBaselineStageSourceForwardQ15[ReprojectStage];
  M.ElasticZeroProgressStepMax = FMath::Max(
    M.ElasticZeroProgressStepMax, ZeroProgressStepMax);
  M.ElasticHardPairViolationCount += Reproject.HardPairViolationCount;
  M.ElasticObstacleViolationCount += Obstacle.ObstaclePenetrationCount
    + Reproject.ObstaclePenetrationCount;
  M.ElasticFlowBoundsViolationCount += Obstacle.FlowBoundsHitCount + Reproject.FlowBoundsHitCount;
  M.ElasticTargetViolationCount += Reproject.TargetViolationCount;
  M.ElasticInvalidInputCount += Summary.InvalidInputCount;
  M.ElasticSolverMsP95 = Percentile(ElasticSolverMsSamples, 0.95f);
  M.ElasticShadowHash = FoldHash(FoldHash(
    M.ElasticShadowHash, static_cast<uint32>(GetCurrentFixedStepIndex())), Twin.StableHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordElasticParallelRollout(
  const FCrowdDemoElasticShadowParallelState& State,
  const FCrowdDemoElasticShadowTwinResult& Step)
{
  if (!IsElasticCrowdShadowEnabled() || !State.bActive || !Step.bValid) return;
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.ElasticParallelCompletedSteps = State.StepIndex;
  M.ElasticParallelHash = FoldHash(FoldHash(M.ElasticParallelHash,
    static_cast<uint32>(State.StepIndex)), Step.StableHash);
  if (!State.bCompleted) return;
  const auto& S = State.Summary;
  M.ElasticParallelEligibleRecoveryCount = S.EligibleRecoveryAgentCount;
  M.ElasticParallelBaselineRecoveryCompletedCount = S.BaselineRecoveryCompletedCount;
  M.ElasticParallelRecoveryCompletedCount = S.ElasticRecoveryCompletedCount;
  M.ElasticParallelBaselineImprovedCount = S.BaselineImprovedCount;
  M.ElasticParallelImprovedCount = S.ElasticImprovedCount;
  M.ElasticParallelBaselinePermanentHoleCount = S.BaselinePermanentHoleCount;
  M.ElasticParallelPermanentHoleCount = S.ElasticPermanentHoleCount;
  M.ElasticParallelBaselineRecoveryTimeP95 = Percentile(S.BaselineRecoveryTimesSeconds, 0.95f);
  M.ElasticParallelRecoveryTimeP95 = Percentile(S.ElasticRecoveryTimesSeconds, 0.95f);
  M.ElasticParallelBaselineEndErrorCmP95 = Percentile(S.BaselineEndErrorsCm, 0.95f);
  M.ElasticParallelEndErrorCmP95 = Percentile(S.ElasticEndErrorsCm, 0.95f);
  M.ElasticParallelBaselineSourceForwardQ15 = S.BaselineDesiredSourceForwardCmps > 0
    ? FMath::Clamp(FMath::RoundToInt(static_cast<double>(S.BaselineActualSourceForwardCmps)
      * 32767.0 / static_cast<double>(S.BaselineDesiredSourceForwardCmps)), 0, 131068)
    : 32767;
  M.ElasticParallelSourceForwardQ15 = S.ElasticDesiredSourceForwardCmps > 0
    ? FMath::Clamp(FMath::RoundToInt(static_cast<double>(S.ElasticActualSourceForwardCmps)
      * 32767.0 / static_cast<double>(S.ElasticDesiredSourceForwardCmps)), 0, 131068)
    : 32767;
  M.ElasticParallelBaselineHardPairViolationCount = S.BaselineHardPairViolationCount;
  M.ElasticParallelHardPairViolationCount = S.ElasticHardPairViolationCount;
  M.ElasticParallelBaselineObstaclePenetrationCount = S.BaselineObstaclePenetrationCount;
  M.ElasticParallelObstaclePenetrationCount = S.ElasticObstaclePenetrationCount;
  M.ElasticParallelBaselineTargetViolationCount = S.BaselineTargetViolationCount;
  M.ElasticParallelTargetViolationCount = S.ElasticTargetViolationCount;
  M.ElasticParallelBaselineOrcaStopViolationCount = S.BaselineOrcaStopViolationCount;
  M.ElasticParallelOrcaStopViolationCount = S.ElasticOrcaStopViolationCount;
  M.ElasticRecoveryErrorCmP95 = M.ElasticParallelEndErrorCmP95;
  M.ElasticParallelHash = FoldHash(M.ElasticParallelHash, S.StableHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordElasticCrowdFailureFixture(
  const FCrowdDemoElasticShadowFailureFixture& Fixture)
{
  if (!IsElasticCrowdShadowEnabled() || !Fixture.bValid
    || ElasticFailureFixture.bValid) return;
  if (!Fixture.bFixtureTooLarge
    && (Fixture.Agents.Num() < 2 || Fixture.Agents.Num() > 20)) return;
  ElasticFailureFixture = Fixture;
  LastCompletedElasticFailureFixture = Fixture;
  FCrowdDemoTrafficMetrics& M = LastCompareMetrics.TrafficMetrics;
  M.ElasticFailureFixtureAgentCount = Fixture.ClosureAgentCount;
  M.ElasticFailureFixtureHash = Fixture.StableHash;
  M.ElasticFailureFixedStep = Fixture.FixedStepIndex;
  M.ElasticFailureStage = static_cast<int32>(Fixture.Stage);
  M.ElasticFailureKind = static_cast<int32>(Fixture.FailureKind);
  M.ElasticFailureAttribution = static_cast<int32>(Fixture.Attribution);
  M.ElasticFailureClosureAgentCount = Fixture.ClosureAgentCount;
  M.ElasticFailureFixtureTooLarge = Fixture.bFixtureTooLarge ? 1 : 0;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPositioningDiagnostic(
  const FCrowdDemoPositioningRuntimeDiagnostic& Diagnostic)
{
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.PositionSlotCommitCount = Diagnostic.SlotCommitCount;
  Metrics.PositionReserveCommitCount = Diagnostic.ReserveCommitCount;
  Metrics.PositionUnsettledPortalOwnedCount = Diagnostic.PortalOwnedCount;
  Metrics.PositionUnsettledOutsideComposeRangeCount = Diagnostic.OutsideComposeRangeCount;
  Metrics.PositionUnsettledGuidanceActiveCount = Diagnostic.GuidanceActiveCount;
  Metrics.PositionUnsettledArrivalSpeedRejectedCount = Diagnostic.ArrivalSpeedRejectedCount;
  Metrics.PositionUnsettledErrorLe30Count = Diagnostic.ErrorLe30Count;
  Metrics.PositionUnsettledError31To100Count = Diagnostic.Error31To100Count;
  Metrics.PositionUnsettledError101To300Count = Diagnostic.Error101To300Count;
  Metrics.PositionUnsettledErrorOver300Count = Diagnostic.ErrorOver300Count;
  Metrics.PositionUnsettledPreviousOrcaFallbackCount = Diagnostic.PreviousOrcaFallbackCount;
  Metrics.PositionUnsettledPreviousOrcaInfeasibleCount = Diagnostic.PreviousOrcaInfeasibleCount;
  Metrics.PositionUnsettledPreviousPbdCorrectedCount = Diagnostic.PreviousPbdCorrectedCount;
  Metrics.PositionUnsettledSpeedCmpsP95 = Diagnostic.SpeedP95;
  Metrics.PositionUnsettledGuidanceSpeedCmpsP95 = Diagnostic.GuidanceSpeedP95;
  Metrics.PositionUnsettledOrcaSpeedCmpsP95 = Diagnostic.OrcaSpeedP95;
  Metrics.PositionUnsettledObstacleSpeedCmpsP95 = Diagnostic.ObstacleSpeedP95;
  Metrics.PositionUnsettledOrcaAdjustedCount = Diagnostic.OrcaAdjustedCount;
  Metrics.PositionUnsettledOrcaZeroCount = Diagnostic.OrcaZeroCount;
  Metrics.PositionUnsettledObstacleHitCount = Diagnostic.ObstacleHitCount;
  Metrics.PositionUnsettledOrcaConstraintP95 = Diagnostic.OrcaConstraintP95;
}

bool UCrowdDemoRoundSimPipelineSubsystem::EnsureTrafficFlowFields()
{
  if (!IsActive()
    || (!IsTrafficScenario(GetRules().Scenario)
      && GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure))
  {
    return false;
  }
  bool bAllValid = true;
  for (const FCrowdDemoTrafficCohortRule& Cohort : GetRules().TrafficCohorts)
  {
    FCrowdDemoSharedFlowField& Field = TrafficFlowFields.FindOrAdd(Cohort.CohortId);
    if (!Field.IsValid() || Field.Config.Revision != Cohort.FlowFieldConfig.Revision
      || !FVector(Field.Config.GoalLocation).Equals(FVector(Cohort.FlowFieldConfig.GoalLocation), 0.01f))
    {
      bAllValid &= FCrowdDemoSharedFlowFieldKernel::Build(Cohort.FlowFieldConfig, Field);
    }
  }
  return bAllValid;
}

const FCrowdDemoSharedFlowField* UCrowdDemoRoundSimPipelineSubsystem::FindTrafficFlowField(const int32 CohortId) const
{
  return TrafficFlowFields.Find(CohortId);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTrafficStep(
  const FCrowdDemoTrafficStepSummary& TrafficSummary,
  const FCrowdDemoOrcaSummary& OrcaSummary,
  const float OrcaSolverMs)
{
  if (!IsActive() || !IsTrafficScenario(GetRules().Scenario))
  {
    return;
  }
  LastTrafficStepSummary = TrafficSummary;
  LastOrcaSummary = OrcaSummary;
  TrafficRoundHash = FoldHash(FoldHash(TrafficRoundHash, TrafficFixedStepIndex), TrafficSummary.TrafficFieldHash);
  PortalRoundHash = FoldHash(FoldHash(PortalRoundHash, TrafficFixedStepIndex), TrafficSummary.PortalDecisionHash);
  OrcaRoundHash = FoldHash(FoldHash(OrcaRoundHash, TrafficFixedStepIndex), OrcaSummary.VelocityHash);
  PriorityOrcaRoundHash = FoldHash(
    FoldHash(PriorityOrcaRoundHash, TrafficFixedStepIndex), OrcaSummary.PriorityHash);
  ++TrafficFixedStepIndex;
  TrafficQueueSamples.Add(static_cast<float>(TrafficSummary.QueuedCount));
  TrafficOccupiedSamples.Add(static_cast<float>(TrafficSummary.OccupiedCount));
  OrcaNeighborSamples.Append(OrcaSummary.NeighborCounts);
  OrcaConstraintSamples.Append(OrcaSummary.ConstraintCounts);
  OrcaSolverMsSamples.Add(OrcaSolverMs);
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.TrafficFieldHash = TrafficRoundHash;
  Metrics.PortalDecisionHash = PortalRoundHash;
  Metrics.OrcaVelocityHash = OrcaRoundHash;
  Metrics.PriorityOrcaHash = PriorityOrcaRoundHash;
  Metrics.TrafficPortalCount = TrafficSummary.PortalCount;
  Metrics.RawPortalCandidateCount = PortalExtractionSummary.RawCrossSectionCandidateCount;
  Metrics.ExtractedPortalCount = PortalExtractionSummary.ExtractedPortalCount;
  Metrics.TrafficPortalQueueCountP50 = Percentile(TrafficQueueSamples, 0.50f);
  Metrics.TrafficPortalQueueCountP95 = Percentile(TrafficQueueSamples, 0.95f);
  Metrics.TrafficPortalQueueCountMax = FMath::Max(Metrics.TrafficPortalQueueCountMax, TrafficSummary.QueuedCount);
  Metrics.TrafficPortalOccupiedCountP95 = Percentile(TrafficOccupiedSamples, 0.95f);
  Metrics.TrafficPortalOccupiedCountMax = FMath::Max(Metrics.TrafficPortalOccupiedCountMax, TrafficSummary.OccupiedCount);
  Metrics.TrafficAdmissionGrantedCount += TrafficSummary.AdmissionGrantedCount;
  Metrics.TrafficAdmissionDeniedCount += TrafficSummary.AdmissionDeniedCount;
  Metrics.PortalBindCount += TrafficSummary.PortalBindCount;
  Metrics.PortalRebindCount += TrafficSummary.PortalRebindCount;
  Metrics.PortalReleaseCount += TrafficSummary.PortalReleaseCount;
  Metrics.InvalidSideCandidateCount += TrafficSummary.InvalidSideCandidateCount;
  Metrics.WrongSpanCandidateCount += TrafficSummary.WrongSpanCandidateCount;
  Metrics.ReservedToInsideCount += TrafficSummary.ReservedToInsideCount;
  Metrics.InsideToExitedCount += TrafficSummary.InsideToExitedCount;
  Metrics.PortalZeroThroughputStepCount += TrafficSummary.QueuedCount > 0
    && TrafficSummary.InsideToExitedCount == 0 ? 1 : 0;
  Metrics.TrafficBandAssignmentCount += TrafficSummary.BandAssignmentCount;
  Metrics.TrafficBandReassignmentCount += TrafficSummary.BandReassignmentCount;
  Metrics.HoldingTargetCount = FMath::Max(Metrics.HoldingTargetCount, TrafficSummary.HoldingTargetCount);
  Metrics.HoldingTargetAllocationFailureCount += TrafficSummary.HoldingTargetAllocationFailureCount;
  Metrics.HoldingTargetOverlapCount += TrafficSummary.HoldingTargetOverlapCount;
  BandLateralErrorSamples.Append(TrafficSummary.BandLateralErrors);
  Metrics.BandLateralErrorP50 = Percentile(BandLateralErrorSamples, 0.50f);
  Metrics.BandLateralErrorP95 = Percentile(BandLateralErrorSamples, 0.95f);
  for (const float Error : TrafficSummary.BandLateralErrors)
    Metrics.BandLateralErrorMax = FMath::Max(Metrics.BandLateralErrorMax, Error);
  Metrics.ReservedPositiveAxialVelocityCount += TrafficSummary.ReservedPositiveAxialVelocityCount;
  Metrics.ReservedZeroVelocityCount += TrafficSummary.ReservedZeroVelocityCount;
  Metrics.TrafficDirectionEpochChangeCount += TrafficSummary.DirectionEpochChangeCount;
  Metrics.ReservationTimeoutCount += TrafficSummary.ReservationTimeoutCount;
  Metrics.TransitTimeoutCount += TrafficSummary.TransitTimeoutCount;
  Metrics.PortalCapacityViolationCount += TrafficSummary.CapacityViolationCount;
  Metrics.TrafficDensityAgentCountMax = FMath::Max(Metrics.TrafficDensityAgentCountMax, TrafficSummary.DensityAgentCountMax);
  Metrics.OrcaProcessedAgentCount += OrcaSummary.ProcessedAgentCount;
  Metrics.OrcaNeighborCountP50 = Percentile(OrcaNeighborSamples, 0.50f);
  Metrics.OrcaNeighborCountP95 = Percentile(OrcaNeighborSamples, 0.95f);
  Metrics.OrcaNeighborCountMax = FMath::Max(Metrics.OrcaNeighborCountMax, OrcaSummary.NeighborCountMax);
  Metrics.OrcaConstraintCountP50 = Percentile(OrcaConstraintSamples, 0.50f);
  Metrics.OrcaConstraintCountP95 = Percentile(OrcaConstraintSamples, 0.95f);
  Metrics.OrcaConstraintCountMax = FMath::Max(Metrics.OrcaConstraintCountMax, OrcaSummary.ConstraintCountMax);
  Metrics.OrcaConstraintCutoffCircleCount += OrcaSummary.CutoffCircleConstraintCount;
  Metrics.OrcaConstraintLeftLegCount += OrcaSummary.LeftLegConstraintCount;
  Metrics.OrcaConstraintRightLegCount += OrcaSummary.RightLegConstraintCount;
  Metrics.OrcaConstraintPenetrationCount += OrcaSummary.PenetrationConstraintCount;
  Metrics.OrcaNoConstraintCount += OrcaSummary.NoConstraintCount;
  Metrics.OrcaPreferredFeasibleCount += OrcaSummary.PreferredFeasibleCount;
  Metrics.OrcaLpFeasibleCount += OrcaSummary.LpFeasibleCount;
  Metrics.OrcaSingleConstraintOutsideSpeedCircleCount += OrcaSummary.SingleConstraintOutsideSpeedCircleCount;
  Metrics.OrcaMultiConstraintEmptyCount += OrcaSummary.MultiConstraintEmptyIntersectionCount;
  Metrics.OrcaQuantizationDestroyedCount += OrcaSummary.QuantizationDestroyedFeasibilityCount;
  Metrics.OrcaFallbackFlowFeasibleCount += OrcaSummary.FallbackFlowFeasibleCount;
  Metrics.OrcaFallbackPortalFeasibleCount += OrcaSummary.FallbackPortalFeasibleCount;
  Metrics.OrcaStopFeasibleCount += OrcaSummary.StopFeasibleCount;
  Metrics.OrcaStopViolationCount += OrcaSummary.StopViolationCount;
  Metrics.OrcaAdjustedAgentCount = FMath::Max(Metrics.OrcaAdjustedAgentCount, OrcaSummary.AdjustedAgentCount);
  Metrics.OrcaInfeasibleAgentCount += OrcaSummary.InfeasibleAgentCount;
  Metrics.OrcaFallbackStopCount += OrcaSummary.FallbackStopCount;
  Metrics.OrcaStopSatisfiesConstraintCount += OrcaSummary.StopSatisfiesConstraintCount;
  Metrics.OrcaStopViolatesConstraintCount += OrcaSummary.StopViolatesConstraintCount;
  Metrics.PriorityOrcaEqualPairCount += OrcaSummary.PriorityEqualPairCount;
  Metrics.PriorityOrcaAsymmetricPairCount += OrcaSummary.PriorityAsymmetricPairCount;
  Metrics.PriorityOrcaHighSide25Count += OrcaSummary.PriorityHighSide25Count;
  Metrics.PriorityOrcaLowSide75Count += OrcaSummary.PriorityLowSide75Count;
  Metrics.PriorityOrcaResponsibilitySumViolationCount +=
    OrcaSummary.PriorityResponsibilitySumViolationCount;
  Metrics.OrcaFormalLpFeasibleCount += OrcaSummary.FormalLpFeasibleCount;
  Metrics.OrcaFormalLpQuantizedRecoveredCount += OrcaSummary.FormalLpQuantizedRecoveredCount;
  Metrics.OrcaFormalLpQuantizedGeometryRecoveredCount +=
    OrcaSummary.FormalLpQuantizedGeometryRecoveredCount;
  Metrics.OrcaFormalLpMissedZeroRecoveredCount += OrcaSummary.FormalLpMissedZeroRecoveredCount;
  Metrics.OrcaFormalLpMissedOracleRecoveredCount += OrcaSummary.FormalLpMissedOracleRecoveredCount;
  Metrics.OrcaContinuousFeasibleQuantizedEmptyCount += OrcaSummary.ContinuousFeasibleQuantizedEmptyCount;
  Metrics.OrcaTrueNoFeasibleWitnessCount += OrcaSummary.TrueNoFeasibleWitnessCount;
  Metrics.OrcaOracleInvocationCount += OrcaSummary.OracleInvocationCount;
  Metrics.OrcaCalledAfterContinuousFailureCount += OrcaSummary.OracleCalledAfterContinuousFailureCount;
  Metrics.OrcaCalledAfterQuantizationFailureCount += OrcaSummary.OracleCalledAfterQuantizationFailureCount;
  Metrics.OrcaQuantizedWitnessUsedCount += OrcaSummary.OracleQuantizedWitnessUsedCount;
  Metrics.OrcaNeighborhood3x3RecoveredCount += OrcaSummary.Neighborhood3x3RecoveredCount;
  Metrics.OrcaOracleNoWitnessCount += OrcaSummary.OracleNoWitnessCount;
  Metrics.OrcaTrueNoWitnessReachableFlowCount += OrcaSummary.TrueNoWitnessReachableFlowCount;
  Metrics.OrcaTrueNoWitnessInvalidFlowCount += OrcaSummary.TrueNoWitnessInvalidFlowCount;
  Metrics.OrcaTrueNoWitnessGoalNearCount += OrcaSummary.TrueNoWitnessGoalNearCount;
  Metrics.OrcaTrueNoWitnessCorridorCount += OrcaSummary.TrueNoWitnessCorridorCount;
  Metrics.OrcaParallelBranchCount += OrcaSummary.ParallelBranchCount;
  Metrics.OrcaNearParallelBranchCount += OrcaSummary.NearParallelBranchCount;
  Metrics.OrcaRedundantParallelCount += OrcaSummary.RedundantParallelCount;
  Metrics.OrcaStricterParallelCount += OrcaSummary.StricterParallelCount;
  Metrics.OrcaTrueParallelContradictionCount += OrcaSummary.TrueParallelContradictionCount;
  Metrics.OrcaNumericalToleranceAcceptanceCount += OrcaSummary.NumericalToleranceAcceptanceCount;
  Metrics.OrcaOracleRecoveryMsP95 = Percentile(OrcaOracleRecoveryMsSamples, 0.95f);
  Metrics.WaitingOrcaInfeasibleCount += OrcaSummary.WaitingInfeasibleCount;
  Metrics.ApproachOrcaInfeasibleCount += OrcaSummary.ApproachInfeasibleCount;
  Metrics.ReservedOrcaInfeasibleCount += OrcaSummary.ReservedInfeasibleCount;
  Metrics.InsideOrcaInfeasibleCount += OrcaSummary.InsideInfeasibleCount;
  Metrics.WaitingOrcaFallbackStopCount += OrcaSummary.WaitingFallbackStopCount;
  Metrics.ApproachOrcaFallbackStopCount += OrcaSummary.ApproachFallbackStopCount;
  Metrics.ReservedOrcaFallbackStopCount += OrcaSummary.ReservedFallbackStopCount;
  Metrics.InsideOrcaFallbackStopCount += OrcaSummary.InsideFallbackStopCount;
  Metrics.OrcaSolverMsP95 = Percentile(OrcaSolverMsSamples, 0.95f);
  Metrics.OrcaSolverMsP50 = Percentile(OrcaSolverMsSamples, 0.50f);
  Metrics.OrcaSolverMsMax = OrcaSolverMsSamples.IsEmpty() ? 0.0f : FMath::Max(OrcaSolverMs, Metrics.OrcaSolverMsMax);
  auto AccumulateStateCounts = [](TArray<int32>& Target, const TStaticArray<int32, 6>& Source)
  {
    if (Target.Num() != 6) Target.Init(0, 6);
    for (int32 Index = 0; Index < 6; ++Index) Target[Index] += Source[Index];
  };
  AccumulateStateCounts(Metrics.OrcaProcessedByAdmissionState, OrcaSummary.ProcessedByAdmissionState);
  AccumulateStateCounts(Metrics.OrcaFormalLpMissedByAdmissionState, OrcaSummary.FormalLpMissedByAdmissionState);
  AccumulateStateCounts(Metrics.OrcaQuantizedEmptyByAdmissionState, OrcaSummary.QuantizedEmptyByAdmissionState);
  AccumulateStateCounts(Metrics.OrcaInfeasibleByAdmissionState, OrcaSummary.InfeasibleByAdmissionState);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSf3OverlapSample(
  const int32 OverlapPairs,
  const int32 SevereOverlapPairs,
  const int32 ResidualPbdPairs,
  const int32 ObstaclePenetrations)
{
  if (!IsActive() || !IsTrafficScenario(GetRules().Scenario))
  {
    return;
  }
  LastSeparationOverlapPairCount = OverlapPairs;
  LastSeparationSevereOverlapPairCount = SevereOverlapPairs;
  FlowSeparationOverlapPairSamples.Add(static_cast<float>(OverlapPairs));
  FlowSeparationSevereOverlapPairSamples.Add(static_cast<float>(SevereOverlapPairs));
  LastCompareMetrics.OverlapPairCountP50 = Percentile(FlowSeparationOverlapPairSamples, 0.50f);
  LastCompareMetrics.OverlapPairCountP95 = Percentile(FlowSeparationOverlapPairSamples, 0.95f);
  LastCompareMetrics.OverlapPairCountMax = FMath::Max(LastCompareMetrics.OverlapPairCountMax, OverlapPairs);
  LastCompareMetrics.SevereOverlapPairCountP50 = Percentile(FlowSeparationSevereOverlapPairSamples, 0.50f);
  LastCompareMetrics.SevereOverlapPairCountP95 = Percentile(FlowSeparationSevereOverlapPairSamples, 0.95f);
  LastCompareMetrics.SevereOverlapPairCountMax = FMath::Max(
    LastCompareMetrics.SevereOverlapPairCountMax, SevereOverlapPairs);
  LastCompareMetrics.TrafficMetrics.ResidualPbdPenetrationPairCount = FMath::Max(
    LastCompareMetrics.TrafficMetrics.ResidualPbdPenetrationPairCount, ResidualPbdPairs);
  LastCompareMetrics.TrafficMetrics.FinalObstaclePenetrationCount += ObstaclePenetrations;
}

FCrowdDemoTrafficMetrics UCrowdDemoRoundSimPipelineSubsystem::BuildTrafficMetrics(
  const TConstArrayView<FCrowdDemoRoundAgentState> States) const
{
  FCrowdDemoTrafficMetrics Metrics = LastCompareMetrics.TrafficMetrics;
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoRoundAgentState& State : States)
  {
    Hash = FoldHash(Hash, static_cast<uint32>(State.AgentId));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Location.X)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Location.Y)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Velocity.X)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Velocity.Y)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.YawDegrees)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.RadiusCm)));
  }
  Metrics.AgentStateHash = Hash;
  Metrics.InvalidFlowDeadlockCount = 0;
  for (const int32 AgentId : FlowFinalInvalidAgentIds)
    Metrics.InvalidFlowDeadlockCount += FlowCorridorDeadlockAgentIds.Contains(AgentId) ? 1 : 0;
  if (IsSf3OrcaReferenceDiagnosticEnabled())
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3OrcaReferenceDiagnostic samples=%d current_exact=%d reference_exact=%d current_miss_reference_hit=%d both_miss_oracle_hit=%d current_hit_reference_miss=%d all_exact_miss=%d continuous_hit_quantized_miss=%d three_by_three_recovered=%d oracle_witness_available=%d best_effort_used=%d minimum_fixture_hash=%u minimum_fixture_constraints=%d source=MassPipeline"),
      Metrics.OrcaReferenceSampleCount, Metrics.OrcaReferenceCurrentExactCount,
      Metrics.OrcaReferenceExactCount, Metrics.OrcaCurrentMissReferenceHitCount,
      Metrics.OrcaBothMissOracleHitCount, Metrics.OrcaCurrentHitReferenceMissCount,
      Metrics.OrcaAllExactMissCount, Metrics.OrcaReferenceContinuousHitQuantizedMissCount,
      Metrics.OrcaReferenceThreeByThreeRecoveredCount,
      Metrics.OrcaReferenceOracleWitnessAvailableCount,
      Metrics.OrcaReferenceBestEffortUsedCount, Metrics.OrcaReferenceMinimumFixtureHash,
      Metrics.OrcaReferenceMinimumFixtureConstraintCount);
  }
  if (IsSf3FlowReachabilityDiagnosticEnabled())
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3FlowReachability reachable=%d out_of_bounds=%d blocked_raster=%d unreachable_free=%d reachable_to_out_of_bounds=%d reachable_to_blocked=%d reachable_to_unreachable_free=%d at_predict=%d at_obstacle=%d at_pbd=%d at_reproject=%d final_invalid=%d invalid_to_reachable=%d continuous_legal_blocked=%d continuous_legal_unreachable_free=%d continuous_legal_out_of_bounds=%d blocked_penetrating=%d blocked_not_penetrating=%d invalid_preferred_zero=%d invalid_final_zero=%d invalid_deadlock=%d nearest_reachable_max_cm=%.3f navigation_domain_reproject_max_cm=%.3f source=MassPipeline"),
      Metrics.FlowReachableFinalCount, Metrics.FlowOutOfBoundsFinalCount,
      Metrics.FlowBlockedRasterFinalCount, Metrics.FlowUnreachableFreeFinalCount,
      Metrics.FlowReachableToOutOfBoundsCount, Metrics.FlowReachableToBlockedCellCount,
      Metrics.FlowReachableToUnreachableFreeCount, Metrics.FlowTransitionAtPredictCount,
      Metrics.FlowTransitionAtObstacleConstraintCount, Metrics.FlowTransitionAtPbdCount,
      Metrics.FlowTransitionAtObstacleReprojectCount, Metrics.FlowFinalInvalidAgentCount,
      Metrics.FlowInvalidToReachableRecoveryCount, Metrics.ContinuousLegalButBlockedCellCount,
      Metrics.ContinuousLegalButUnreachableFreeCount, Metrics.ContinuousLegalButOutOfBoundsCount,
      Metrics.BlockedCellAndPenetratingCount, Metrics.BlockedCellButNotPenetratingCount,
      Metrics.InvalidFlowPreferredZeroCount, Metrics.InvalidFlowFinalZeroVelocityCount,
      Metrics.InvalidFlowDeadlockCount, Metrics.FlowNearestReachableDistanceCmMax,
      Metrics.NavigationDomainReprojectDeltaCmMax);
    const TCHAR* StageNames[] = {TEXT("StepStart"),TEXT("MovementPredict"),TEXT("ObstacleConstraint"),
      TEXT("HardPBD"),TEXT("ObstacleReproject"),TEXT("MovementFinalize")};
    const TCHAR* StatusNames[] = {TEXT("Reachable"),TEXT("OutOfBounds"),
      TEXT("BlockedRasterCell"),TEXT("UnreachableFreeCell")};
    for (const FCrowdDemoFlowReachabilityWitness& Witness : FlowReachabilityWitnesses)
    {
      if (!Witness.bValid) continue;
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf3FlowWitness stage=%s previous_cell=%d previous_status=%s next_cell=%d next_status=%s delta_x=%.3f delta_y=%.3f blocked=%d continuous_penetrating=%d nearest_reachable_cm=%.3f source=MassPipeline"),
        StageNames[static_cast<int32>(Witness.Stage)], Witness.PreviousStableCellKey,
        StatusNames[static_cast<int32>(Witness.PreviousStatus)], Witness.NextStableCellKey,
        StatusNames[static_cast<int32>(Witness.NextStatus)], Witness.WorldDelta.X, Witness.WorldDelta.Y,
        Witness.bBlocked ? 1 : 0, Witness.bContinuousPenetrating ? 1 : 0,
        Witness.NearestReachableCellDistanceCm);
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3OrcaRecoverySplit oracle_after_continuous_failure=%d oracle_after_quantization_failure=%d oracle_quantized_witness_used=%d neighborhood_3x3_recovered=%d oracle_no_witness=%d true_no_witness_reachable_flow=%d true_no_witness_invalid_flow=%d true_no_witness_goal_near=%d true_no_witness_corridor=%d source=MassPipeline"),
      Metrics.OrcaCalledAfterContinuousFailureCount, Metrics.OrcaCalledAfterQuantizationFailureCount,
      Metrics.OrcaQuantizedWitnessUsedCount, Metrics.OrcaNeighborhood3x3RecoveredCount,
      Metrics.OrcaOracleNoWitnessCount, Metrics.OrcaTrueNoWitnessReachableFlowCount,
      Metrics.OrcaTrueNoWitnessInvalidFlowCount, Metrics.OrcaTrueNoWitnessGoalNearCount,
      Metrics.OrcaTrueNoWitnessCorridorCount);
  }
  if (IsSf3DeterminismDiagnosticEnabled())
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3OrcaClassification constraints_cutoff=%d constraints_left_leg=%d constraints_right_leg=%d constraints_penetration=%d no_constraint=%d preferred_feasible=%d lp_feasible=%d single_outside_speed_circle=%d multi_empty=%d quantization_destroyed=%d fallback_flow=%d fallback_portal=%d stop_feasible=%d stop_violation=%d waiting_infeasible=%d approach_infeasible=%d reserved_infeasible=%d inside_infeasible=%d source=MassPipeline"),
      Metrics.OrcaConstraintCutoffCircleCount,
      Metrics.OrcaConstraintLeftLegCount,
      Metrics.OrcaConstraintRightLegCount,
      Metrics.OrcaConstraintPenetrationCount,
      Metrics.OrcaNoConstraintCount,
      Metrics.OrcaPreferredFeasibleCount,
      Metrics.OrcaLpFeasibleCount,
      Metrics.OrcaSingleConstraintOutsideSpeedCircleCount,
      Metrics.OrcaMultiConstraintEmptyCount,
      Metrics.OrcaQuantizationDestroyedCount,
      Metrics.OrcaFallbackFlowFeasibleCount,
      Metrics.OrcaFallbackPortalFeasibleCount,
      Metrics.OrcaStopFeasibleCount,
      Metrics.OrcaStopViolationCount,
      Metrics.WaitingOrcaInfeasibleCount,
      Metrics.ApproachOrcaInfeasibleCount,
      Metrics.ReservedOrcaInfeasibleCount,
      Metrics.InsideOrcaInfeasibleCount);
    const auto StateValue = [](const TArray<int32>& Values, const int32 Index)
    {
      return Values.IsValidIndex(Index) ? Values[Index] : 0;
    };
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3OrcaLpRepair processed=%d preferred_feasible=%d formal_lp_feasible=%d formal_lp_quantized_recovered=%d formal_lp_geometry_recovered=%d formal_lp_missed_zero_recovered=%d formal_lp_missed_oracle_recovered=%d continuous_feasible_quantized_empty=%d true_no_feasible_witness=%d flow_fallback=%d portal_fallback=%d stop_feasible=%d stop_violation=%d infeasible=%d constraint_p50=%.3f constraint_p95=%.3f constraint_max=%d solver_ms_p50=%.3f solver_ms_p95=%.3f solver_ms_max=%.3f oracle_invocations=%d oracle_recovery_ms_p95=%.3f parallel=%d near_parallel=%d redundant_parallel=%d stricter_parallel=%d true_parallel_contradiction=%d numerical_tolerance_acceptance=%d processed_by_state=%d,%d,%d,%d,%d,%d missed_by_state=%d,%d,%d,%d,%d,%d quantized_empty_by_state=%d,%d,%d,%d,%d,%d infeasible_by_state=%d,%d,%d,%d,%d,%d source=MassPipeline"),
      Metrics.OrcaProcessedAgentCount, Metrics.OrcaPreferredFeasibleCount,
      Metrics.OrcaFormalLpFeasibleCount, Metrics.OrcaFormalLpQuantizedRecoveredCount,
      Metrics.OrcaFormalLpQuantizedGeometryRecoveredCount,
      Metrics.OrcaFormalLpMissedZeroRecoveredCount, Metrics.OrcaFormalLpMissedOracleRecoveredCount,
      Metrics.OrcaContinuousFeasibleQuantizedEmptyCount, Metrics.OrcaTrueNoFeasibleWitnessCount,
      Metrics.OrcaFallbackFlowFeasibleCount, Metrics.OrcaFallbackPortalFeasibleCount,
      Metrics.OrcaStopFeasibleCount, Metrics.OrcaStopViolationCount,
      Metrics.OrcaInfeasibleAgentCount, Metrics.OrcaConstraintCountP50,
      Metrics.OrcaConstraintCountP95, Metrics.OrcaConstraintCountMax,
      Metrics.OrcaSolverMsP50, Metrics.OrcaSolverMsP95, Metrics.OrcaSolverMsMax,
      Metrics.OrcaOracleInvocationCount, Metrics.OrcaOracleRecoveryMsP95,
      Metrics.OrcaParallelBranchCount, Metrics.OrcaNearParallelBranchCount,
      Metrics.OrcaRedundantParallelCount, Metrics.OrcaStricterParallelCount,
      Metrics.OrcaTrueParallelContradictionCount, Metrics.OrcaNumericalToleranceAcceptanceCount,
      StateValue(Metrics.OrcaProcessedByAdmissionState,0), StateValue(Metrics.OrcaProcessedByAdmissionState,1),
      StateValue(Metrics.OrcaProcessedByAdmissionState,2), StateValue(Metrics.OrcaProcessedByAdmissionState,3),
      StateValue(Metrics.OrcaProcessedByAdmissionState,4), StateValue(Metrics.OrcaProcessedByAdmissionState,5),
      StateValue(Metrics.OrcaFormalLpMissedByAdmissionState,0), StateValue(Metrics.OrcaFormalLpMissedByAdmissionState,1),
      StateValue(Metrics.OrcaFormalLpMissedByAdmissionState,2), StateValue(Metrics.OrcaFormalLpMissedByAdmissionState,3),
      StateValue(Metrics.OrcaFormalLpMissedByAdmissionState,4), StateValue(Metrics.OrcaFormalLpMissedByAdmissionState,5),
      StateValue(Metrics.OrcaQuantizedEmptyByAdmissionState,0), StateValue(Metrics.OrcaQuantizedEmptyByAdmissionState,1),
      StateValue(Metrics.OrcaQuantizedEmptyByAdmissionState,2), StateValue(Metrics.OrcaQuantizedEmptyByAdmissionState,3),
      StateValue(Metrics.OrcaQuantizedEmptyByAdmissionState,4), StateValue(Metrics.OrcaQuantizedEmptyByAdmissionState,5),
      StateValue(Metrics.OrcaInfeasibleByAdmissionState,0), StateValue(Metrics.OrcaInfeasibleByAdmissionState,1),
      StateValue(Metrics.OrcaInfeasibleByAdmissionState,2), StateValue(Metrics.OrcaInfeasibleByAdmissionState,3),
      StateValue(Metrics.OrcaInfeasibleByAdmissionState,4), StateValue(Metrics.OrcaInfeasibleByAdmissionState,5));
  }
  if (IsSf3GoalCongestionDiagnosticEnabled())
  {
    struct FBucket
    {
      int32 Agents = 0;
      int32 Reached = 0;
      int32 NonReached = 0;
      int32 Stopped = 0;
      int32 Processed = 0;
      int32 Preferred = 0;
      int32 Lp = 0;
      int32 Infeasible = 0;
      int32 FallbackStop = 0;
      int32 StopFeasible = 0;
      int32 StopViolation = 0;
      int32 AgainstReached = 0;
      int32 AgainstNonReached = 0;
      TArray<float> Speeds;
      TArray<float> StoppedSeconds;
      TArray<float> Neighbors;
      TArray<float> Constraints;
    };
    FVector Goal = FVector(GetRules().FlowFieldConfig.GoalLocation);
    if (!GetRules().TrafficCohorts.IsEmpty())
      Goal = FVector(GetRules().TrafficCohorts[0].FlowFieldConfig.GoalLocation);
    TStaticArray<FBucket, 6> DistanceBuckets;
    TStaticArray<FBucket, 6> RegionBuckets;
    const auto Average = [](const TConstArrayView<float> Values)
    {
      float Sum = 0.0f;
      for (const float Value : Values) Sum += Value;
      return Values.IsEmpty() ? 0.0f : Sum / Values.Num();
    };
    const auto Maximum = [](const TConstArrayView<float> Values)
    {
      float Result = 0.0f;
      for (const float Value : Values) Result = FMath::Max(Result, Value);
      return Result;
    };
    TArray<float> ReachedDistances;
    TArray<float> NonReachedDistances;
    TArray<float> ReachedSpeeds;
    TArray<float> OracleCandidates;
    TArray<const FCrowdDemoRoundAgentState*> ReachedStates;
    float NearestNonReached = MAX_flt;
    float FarthestReached = 0.0f;
    int32 FinalNonReached = 0;
    int32 ReachedStillInOrca = 0;
    int32 NonReachedWithReachedNeighbor = 0;
    int32 ReachedToNonReachedConstraints = 0;
    int32 ReachedZeroPreferred = 0;
    int32 ReachedGeneratingConstraints = 0;
    int32 LpFailedZeroFeasible = 0;
    int32 LpFailedOracleFeasible = 0;
    int32 LpFailedOracleNoWitness = 0;
    int32 ContinuousQuantizedFailure = 0;
    int32 GenuineSpeedCircleEmpty = 0;
    int32 EverReached = 0;
    int32 ReachedThenLeft = 0;
    int32 NeverReached = 0;
    int32 StoppedNear = 0;
    int32 StoppedFar = 0;
    int32 MovingNear = 0;
    int32 MovingFar = 0;
    uint32 DiagnosticHash = 2166136261u;
    for (const FCrowdDemoRoundAgentState& State : States)
    {
      const FCrowdDemoSf3GoalAgentDiagnostic* Diagnostic = Sf3GoalDiagnostics.Find(State.AgentId);
      const float Distance = FVector::Dist2D(State.Location, Goal);
      const bool bReached = FlowGoalReachedAgentIds.Contains(State.AgentId)
        || (Diagnostic && Diagnostic->bEverReached);
      const float Speed = State.Velocity.Size2D();
      const int32 DistanceBucketIndex = Sf3GoalDistanceBucket(Distance);
      const int32 RegionBucketIndex = Sf3FlowRegionBucket(
        State.Location, Diagnostic ? Diagnostic->IntegrationCost : MAX_int32, Distance);
      auto AddToBucket = [&](FBucket& Bucket)
      {
        ++Bucket.Agents;
        Bucket.Reached += bReached ? 1 : 0;
        Bucket.NonReached += bReached ? 0 : 1;
        Bucket.Speeds.Add(Speed);
        if (!Diagnostic) return;
        const bool bStopped = Speed < 1.0f && Diagnostic->MaxStoppedSeconds >= 2.0f;
        Bucket.Stopped += bStopped ? 1 : 0;
        Bucket.StoppedSeconds.Add(Diagnostic->MaxStoppedSeconds);
        Bucket.Processed += Diagnostic->ProcessedAgentSteps;
        Bucket.Preferred += Diagnostic->PreferredFeasibleCount;
        Bucket.Lp += Diagnostic->LpFeasibleCount;
        Bucket.Infeasible += Diagnostic->InfeasibleCount;
        Bucket.FallbackStop += Diagnostic->FallbackStopCount;
        Bucket.StopFeasible += Diagnostic->StopFeasibleCount;
        Bucket.StopViolation += Diagnostic->StopViolationCount;
        Bucket.AgainstReached += Diagnostic->ConstraintsAgainstReached;
        Bucket.AgainstNonReached += Diagnostic->ConstraintsAgainstNonReached;
        Bucket.Neighbors.Append(Diagnostic->NeighborCounts);
        Bucket.Constraints.Append(Diagnostic->ConstraintCounts);
      };
      AddToBucket(DistanceBuckets[DistanceBucketIndex]);
      AddToBucket(RegionBuckets[RegionBucketIndex]);
      if (bReached)
      {
        ++EverReached;
        ReachedDistances.Add(Distance);
        ReachedSpeeds.Add(Speed);
        ReachedStates.Add(&State);
        FarthestReached = FMath::Max(FarthestReached, Distance);
        ReachedThenLeft += Distance > 140.0f ? 1 : 0;
        if (Diagnostic)
        {
          ReachedStillInOrca += Diagnostic->ProcessedAgentSteps > 0 ? 1 : 0;
          ReachedZeroPreferred += Diagnostic->LastPreferredVelocity.Size() < 1.0f ? 1 : 0;
          ReachedGeneratingConstraints += Diagnostic->ConstraintsAgainstReached
            + Diagnostic->ConstraintsAgainstNonReached > 0 ? 1 : 0;
        }
      }
      else
      {
        ++NeverReached;
        ++FinalNonReached;
        NonReachedDistances.Add(Distance);
        NearestNonReached = FMath::Min(NearestNonReached, Distance);
        if (Diagnostic)
        {
          NonReachedWithReachedNeighbor += Diagnostic->bHadReachedNeighbor ? 1 : 0;
          ReachedToNonReachedConstraints += Diagnostic->ConstraintsAgainstReached;
        }
      }
      if (Diagnostic)
      {
        LpFailedZeroFeasible += Diagnostic->LpFailedZeroFeasibleCount;
        LpFailedOracleFeasible += Diagnostic->LpFailedOracleFeasibleCount;
        LpFailedOracleNoWitness += Diagnostic->LpFailedOracleNoWitnessCount;
        ContinuousQuantizedFailure += Diagnostic->ContinuousFeasibleQuantizedFailureCount;
        GenuineSpeedCircleEmpty += Diagnostic->GenuineSpeedCircleEmptyCount;
        OracleCandidates.Append(Diagnostic->OracleCandidateCounts);
        const bool bStopped = Speed < 1.0f && Diagnostic->MaxStoppedSeconds >= 2.0f;
        const bool bNear = Distance <= 400.0f;
        if (bStopped && bNear) ++StoppedNear;
        else if (bStopped) ++StoppedFar;
        else if (bNear) ++MovingNear;
        else ++MovingFar;
      }
      DiagnosticHash = FoldHash(DiagnosticHash, State.AgentId);
      DiagnosticHash = FoldHash(DiagnosticHash, DistanceBucketIndex);
      DiagnosticHash = FoldHash(DiagnosticHash, RegionBucketIndex);
      DiagnosticHash = FoldHash(DiagnosticHash, bReached ? 1u : 0u);
      DiagnosticHash = FoldHash(DiagnosticHash, Diagnostic ? Diagnostic->InfeasibleCount : 0u);
    }
    int32 ReachedOverlapPairs = 0;
    for (int32 A = 0; A < ReachedStates.Num(); ++A)
      for (int32 B = A + 1; B < ReachedStates.Num(); ++B)
        ReachedOverlapPairs += FVector::DistSquared2D(
          ReachedStates[A]->Location, ReachedStates[B]->Location)
          < FMath::Square(ReachedStates[A]->RadiusCm + ReachedStates[B]->RadiusCm) ? 1 : 0;
    FVector2D MinExtent(MAX_flt, MAX_flt), MaxExtent(-MAX_flt, -MAX_flt);
    for (const FCrowdDemoRoundAgentState* State : ReachedStates)
    {
      MinExtent.X = FMath::Min(MinExtent.X, State->Location.X);
      MinExtent.Y = FMath::Min(MinExtent.Y, State->Location.Y);
      MaxExtent.X = FMath::Max(MaxExtent.X, State->Location.X);
      MaxExtent.Y = FMath::Max(MaxExtent.Y, State->Location.Y);
    }
    static const TCHAR* DistanceNames[] = {
      TEXT("0_100"), TEXT("100_200"), TEXT("200_400"),
      TEXT("400_800"), TEXT("800_1200"), TEXT("1200_plus")};
    static const TCHAR* RegionNames[] = {
      TEXT("GoalCell"), TEXT("GoalNear"), TEXT("PostCorridor"),
      TEXT("Corridor"), TEXT("PreCorridor"), TEXT("FarRoute")};
    const TCHAR* Role = GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
    for (int32 Index = 0; Index < 6; ++Index)
    {
      const FBucket& Bucket = DistanceBuckets[Index];
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf3GoalDistance role=%s round_id=%d bucket=%s agents=%d reached=%d non_reached=%d average_speed_cmps=%.3f speed_p50=%.3f speed_p95=%.3f stopped=%d stopped_seconds_p50=%.3f stopped_seconds_p95=%.3f stopped_seconds_max=%.3f processed=%d preferred_feasible=%d lp_feasible=%d infeasible=%d fallback_stop=%d stop_feasible=%d stop_violation=%d neighbor_p50=%.3f neighbor_p95=%.3f neighbor_max=%.3f constraint_p50=%.3f constraint_p95=%.3f constraint_max=%.3f constraints_against_reached=%d constraints_against_non_reached=%d source=MassPipeline"),
        Role, GetCurrentRoundId(), DistanceNames[Index], Bucket.Agents, Bucket.Reached,
        Bucket.NonReached, Average(Bucket.Speeds),
        Percentile(Bucket.Speeds, 0.50f), Percentile(Bucket.Speeds, 0.95f), Bucket.Stopped,
        Percentile(Bucket.StoppedSeconds, 0.50f), Percentile(Bucket.StoppedSeconds, 0.95f),
        Maximum(Bucket.StoppedSeconds),
        Bucket.Processed, Bucket.Preferred, Bucket.Lp, Bucket.Infeasible,
        Bucket.FallbackStop, Bucket.StopFeasible, Bucket.StopViolation,
        Percentile(Bucket.Neighbors, 0.50f), Percentile(Bucket.Neighbors, 0.95f),
        Maximum(Bucket.Neighbors),
        Percentile(Bucket.Constraints, 0.50f), Percentile(Bucket.Constraints, 0.95f),
        Maximum(Bucket.Constraints),
        Bucket.AgainstReached, Bucket.AgainstNonReached);
      const FBucket& Region = RegionBuckets[Index];
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf3FlowRegion role=%s round_id=%d region=%s agents=%d non_reached=%d stopped=%d average_speed_cmps=%.3f infeasible=%d fallback_stop=%d stop_violation=%d source=MassPipeline"),
        Role, GetCurrentRoundId(), RegionNames[Index], Region.Agents, Region.NonReached,
        Region.Stopped, Average(Region.Speeds),
        Region.Infeasible, Region.FallbackStop, Region.StopViolation);
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3GoalCongestion role=%s round_id=%d diagnostic_hash=%u goal_reached_radius_cm=140.000 reached=%d final_non_reached=%d nearest_non_reached_goal_distance_cm=%.3f farthest_reached_goal_distance_cm=%.3f reached_still_in_orca=%d non_reached_with_reached_neighbor=%d reached_to_non_reached_constraints=%d reached_pair_overlaps=%d reached_extent_x=%.3f reached_extent_y=%.3f reached_radial_p50=%.3f reached_radial_p95=%.3f reached_radial_max=%.3f non_reached_radial_min=%.3f non_reached_radial_p50=%.3f non_reached_radial_p95=%.3f reached_average_speed=%.3f reached_zero_preferred=%d reached_generating_constraints=%d ever_reached=%d reached_then_left=%d never_reached=%d stopped_near=%d stopped_far=%d moving_near=%d moving_far=%d lp_failed_zero_feasible=%d lp_failed_oracle_feasible=%d lp_failed_oracle_no_witness=%d oracle_candidates_p95=%.3f continuous_feasible_quantized_failure=%d genuine_speed_circle_empty=%d source=MassPipeline"),
      Role, GetCurrentRoundId(), DiagnosticHash, EverReached, FinalNonReached,
      FinalNonReached > 0 ? NearestNonReached : -1.0f, FarthestReached,
      ReachedStillInOrca, NonReachedWithReachedNeighbor, ReachedToNonReachedConstraints,
      ReachedOverlapPairs, ReachedStates.IsEmpty() ? 0.0f : MaxExtent.X - MinExtent.X,
      ReachedStates.IsEmpty() ? 0.0f : MaxExtent.Y - MinExtent.Y,
      Percentile(ReachedDistances, 0.50f), Percentile(ReachedDistances, 0.95f),
      Maximum(ReachedDistances),
      NonReachedDistances.IsEmpty() ? -1.0f : NearestNonReached,
      Percentile(NonReachedDistances, 0.50f), Percentile(NonReachedDistances, 0.95f),
      Average(ReachedSpeeds),
      ReachedZeroPreferred, ReachedGeneratingConstraints, EverReached, ReachedThenLeft,
      NeverReached, StoppedNear, StoppedFar, MovingNear, MovingFar,
      LpFailedZeroFeasible, LpFailedOracleFeasible, LpFailedOracleNoWitness,
      Percentile(OracleCandidates, 0.95f), ContinuousQuantizedFailure,
      GenuineSpeedCircleEmpty);
  }
  if (IsElasticCrowdShadowEnabled())
  {
    const auto JoinUInt = [](const TArray<uint32>& Values)
    {
      FString Out;
      for (int32 Index = 0; Index < Values.Num(); ++Index)
        Out += FString::Printf(TEXT("%s%u"), Index == 0 ? TEXT("") : TEXT(","), Values[Index]);
      return Out;
    };
    const auto JoinInt = [](const TArray<int32>& Values)
    {
      FString Out;
      for (int32 Index = 0; Index < Values.Num(); ++Index)
        Out += FString::Printf(TEXT("%s%d"), Index == 0 ? TEXT("") : TEXT(","), Values[Index]);
      return Out;
    };
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoElasticCrowdShadow role=%s round_id=%d spacing_pairs=%d influenced=%d propagation_layer=%d spacing_p95_cm=%.3f spacing_max_cm=%.3f transit_p95_cm=%.3f transit_max_cm=%.3f source_forward_q15=%d baseline_forward_q15=%d zero_progress_steps=%d recovery_p95_cm=%.3f hard_violation=%d obstacle_violation=%d flow_bounds_violation=%d target_violation=%d invalid=%d solver_ms_p95=%.3f hash=%u source=MassPipeline"),
      GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
      GetCurrentRoundId(), Metrics.ElasticSpacingPairCount,
      Metrics.ElasticInfluencedAgentCount, Metrics.ElasticPropagationLayerMax,
      Metrics.ElasticSpacingDeficitCmP95, Metrics.ElasticSpacingDeficitCmMax,
      Metrics.ElasticTransitDeficitCmP95, Metrics.ElasticTransitDeficitCmMax,
      Metrics.ElasticSourceForwardRatioQ15,
      Metrics.ElasticBaselineSourceForwardRatioQ15,
      Metrics.ElasticZeroProgressStepMax, Metrics.ElasticRecoveryErrorCmP95,
      Metrics.ElasticHardPairViolationCount, Metrics.ElasticObstacleViolationCount,
      Metrics.ElasticFlowBoundsViolationCount, Metrics.ElasticTargetViolationCount,
      Metrics.ElasticInvalidInputCount, Metrics.ElasticSolverMsP95,
      Metrics.ElasticShadowHash);
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoElasticShadowScience role=%s round_id=%d first_step=%d first_stage=%d first_kind=%d attribution=%d closure=%d too_large=%d obstacle_clipped=%d obstacle_slide=%d obstacle_stopped=%d obstacle_delta_cm_max=%.3f orca_infeasible=%d orca_stop=%d orca_stop_violation=%d rollout_steps=%d recovery_eligible=%d baseline_completed=%d elastic_completed=%d baseline_improved=%d elastic_improved=%d baseline_holes=%d elastic_holes=%d baseline_recovery_p95=%.3f elastic_recovery_p95=%.3f baseline_end_error_p95=%.3f elastic_end_error_p95=%.3f rollout_hash=%u source=MassPipeline"),
      GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
      GetCurrentRoundId(), Metrics.ElasticFailureFixedStep,
      Metrics.ElasticFailureStage, Metrics.ElasticFailureKind,
      Metrics.ElasticFailureAttribution, Metrics.ElasticFailureClosureAgentCount,
      Metrics.ElasticFailureFixtureTooLarge, Metrics.ElasticObstacleClippedCount,
      Metrics.ElasticObstacleSlideCount, Metrics.ElasticObstacleStoppedCount,
      Metrics.ElasticObstacleConstraintDeltaCmMax, Metrics.ElasticOrcaInfeasibleCount,
      Metrics.ElasticOrcaFallbackStopCount, Metrics.ElasticOrcaStopViolationCount,
      Metrics.ElasticParallelCompletedSteps, Metrics.ElasticParallelEligibleRecoveryCount,
      Metrics.ElasticParallelBaselineRecoveryCompletedCount,
      Metrics.ElasticParallelRecoveryCompletedCount,
      Metrics.ElasticParallelBaselineImprovedCount, Metrics.ElasticParallelImprovedCount,
      Metrics.ElasticParallelBaselinePermanentHoleCount,
      Metrics.ElasticParallelPermanentHoleCount,
      Metrics.ElasticParallelBaselineRecoveryTimeP95,
      Metrics.ElasticParallelRecoveryTimeP95,
      Metrics.ElasticParallelBaselineEndErrorCmP95,
      Metrics.ElasticParallelEndErrorCmP95, Metrics.ElasticParallelHash);
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoElasticParallelSafety role=%s round_id=%d baseline_source_q15=%d elastic_source_q15=%d baseline_hard=%d elastic_hard=%d baseline_obstacle=%d elastic_obstacle=%d baseline_target=%d elastic_target=%d baseline_orca_stop_violation=%d elastic_orca_stop_violation=%d source=MassPipeline"),
      GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
      GetCurrentRoundId(), Metrics.ElasticParallelBaselineSourceForwardQ15,
      Metrics.ElasticParallelSourceForwardQ15,
      Metrics.ElasticParallelBaselineHardPairViolationCount,
      Metrics.ElasticParallelHardPairViolationCount,
      Metrics.ElasticParallelBaselineObstaclePenetrationCount,
      Metrics.ElasticParallelObstaclePenetrationCount,
      Metrics.ElasticParallelBaselineTargetViolationCount,
      Metrics.ElasticParallelTargetViolationCount,
      Metrics.ElasticParallelBaselineOrcaStopViolationCount,
      Metrics.ElasticParallelOrcaStopViolationCount);
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoElasticShadowStages role=%s round_id=%d order=preferred,orca,predict,obstacle,pbd1,pbd2,pbd3,reproject baseline_hash=%s elastic_hash=%s baseline_hard=%s elastic_hard=%s baseline_target=%s elastic_target=%s baseline_source_q15=%s elastic_source_q15=%s source=MassPipeline"),
      GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
      GetCurrentRoundId(), *JoinUInt(Metrics.ElasticBaselineStageHashes),
      *JoinUInt(Metrics.ElasticTwinStageHashes),
      *JoinInt(Metrics.ElasticBaselineStageHardPairCounts),
      *JoinInt(Metrics.ElasticTwinStageHardPairCounts),
      *JoinInt(Metrics.ElasticBaselineStageTargetCounts),
      *JoinInt(Metrics.ElasticTwinStageTargetCounts),
      *JoinInt(Metrics.ElasticBaselineStageSourceForwardQ15),
      *JoinInt(Metrics.ElasticTwinStageSourceForwardQ15));
  }
  return Metrics;
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSf3DeterminismDiagnosticEnabled() const
{
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSf3DeterminismDiagnostic"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic;
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSf3GoalCongestionDiagnosticEnabled() const
{
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSf3GoalCongestionDiagnostic"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic;
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSf3FlowReachabilityDiagnosticEnabled() const
{
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSf3FlowReachabilityDiagnostic"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic;
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSf3OrcaReferenceDiagnosticEnabled() const
{
#if WITH_DEV_AUTOMATION_TESTS
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSf3OrcaReferenceDiagnostic"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic;
#else
  return false;
#endif
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSf4IngressDiagnosticEnabled() const
{
#if WITH_DEV_AUTOMATION_TESTS
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSf4IngressDiagnostic"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
#else
  return false;
#endif
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSf4ReservationOrcaDiagnosticEnabled() const
{
#if WITH_DEV_AUTOMATION_TESTS
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSf4ReservationOrcaDiagnostic"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
#else
  return false;
#endif
}

bool UCrowdDemoRoundSimPipelineSubsystem::HasCapturedSf4ReservationOrcaDiagnostic() const
{
  return Sf4ReservationOrcaCapturedRoundId == GetCurrentRoundId();
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsTransitJointDiagnosticEnabled() const
{
#if WITH_DEV_AUTOMATION_TESTS
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoTransitJointDiagnostic"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
#else
  return false;
#endif
}

bool UCrowdDemoRoundSimPipelineSubsystem::HasCapturedTransitJointDiagnostic() const
{
  return TransitJointDiagnosticCapturedRoundId == GetCurrentRoundId();
}

FCrowdDemoAdaptiveSpacingSettings
UCrowdDemoRoundSimPipelineSubsystem::GetTransitJointDiagnosticSettings() const
{
  FCrowdDemoAdaptiveSpacingSettings Settings;
  Settings.HardSafetyGapCm = 0.0f;
  Settings.PreferredSpacingGapCm = 0.0f;
  Settings.DefaultContextScaleQ15 = 0;
  Settings.MaximumComponentAgents = 8;
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoTransitHardSafetyGapCm="),
    Settings.HardSafetyGapCm);
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoTransitPreferredSpacingGapCm="),
    Settings.PreferredSpacingGapCm);
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoTransitContextScaleQ15="),
    Settings.DefaultContextScaleQ15);
  Settings.HardSafetyGapCm = FMath::Max(0.0f, Settings.HardSafetyGapCm);
  Settings.PreferredSpacingGapCm = FMath::Max(0.0f, Settings.PreferredSpacingGapCm);
  Settings.DefaultContextScaleQ15 = FMath::Clamp(Settings.DefaultContextScaleQ15, 0, 32767);
  return Settings;
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsTransitCapacityShadowEnabled() const
{
#if WITH_DEV_AUTOMATION_TESTS
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoTransitCapacityShadow"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
#else
  return false;
#endif
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsElasticCrowdShadowEnabled() const
{
#if WITH_DEV_AUTOMATION_TESTS
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoElasticCrowdShadow"));
  return bEnabled && IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
#else
  return false;
#endif
}

FCrowdDemoAdaptiveSpacingSettings
UCrowdDemoRoundSimPipelineSubsystem::GetTransitCapacityShadowSettings() const
{
  FCrowdDemoAdaptiveSpacingSettings Settings;
  Settings.HardSafetyGapCm = 10.0f;
  Settings.PreferredSpacingGapCm = 34.0f;
  Settings.DefaultContextScaleQ15 = 32767;
  Settings.MaximumComponentAgents = 20;
  Settings.SolverIterations = 128;
  Settings.TransitClearanceWeightQ8 = 256;
  Settings.TransitClearanceSpeedLimitQ15 = 29490;
  return Settings;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSf3OrcaReferenceDifferential(
  const FCrowdDemoOrcaReferenceDifferentialSummary& Summary)
{
  if (!IsSf3OrcaReferenceDiagnosticEnabled()) return;
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  Metrics.OrcaReferenceSampleCount += Summary.SampleCount;
  Metrics.OrcaReferenceCurrentExactCount += Summary.CurrentExactCount;
  Metrics.OrcaReferenceExactCount += Summary.ReferenceExactCount;
  Metrics.OrcaCurrentMissReferenceHitCount += Summary.CurrentMissReferenceHitCount;
  Metrics.OrcaBothMissOracleHitCount += Summary.BothMissOracleHitCount;
  Metrics.OrcaCurrentHitReferenceMissCount += Summary.CurrentHitReferenceMissCount;
  Metrics.OrcaAllExactMissCount += Summary.AllExactMissCount;
  Metrics.OrcaReferenceContinuousHitQuantizedMissCount += Summary.ContinuousHitQuantizedMissCount;
  Metrics.OrcaReferenceThreeByThreeRecoveredCount += Summary.ThreeByThreeRecoveredCount;
  Metrics.OrcaReferenceOracleWitnessAvailableCount += Summary.OracleWitnessAvailableCount;
  Metrics.OrcaReferenceBestEffortUsedCount += Summary.BestEffortUsedCount;
  if (Summary.MinimumFixtureHash != 0
    && (Metrics.OrcaReferenceMinimumFixtureHash == 0
      || Summary.MinimumFixtureConstraintCount < Metrics.OrcaReferenceMinimumFixtureConstraintCount
      || (Summary.MinimumFixtureConstraintCount == Metrics.OrcaReferenceMinimumFixtureConstraintCount
        && Summary.MinimumFixtureHash < Metrics.OrcaReferenceMinimumFixtureHash)))
  {
    Metrics.OrcaReferenceMinimumFixtureHash = Summary.MinimumFixtureHash;
    Metrics.OrcaReferenceMinimumFixtureConstraintCount = Summary.MinimumFixtureConstraintCount;
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSf3FlowReachabilityStage(
  const ECrowdDemoFlowReachabilityStage Stage,
  const TConstArrayView<FCrowdDemoFlowReachabilityStageSample> Samples)
{
  if (!IsSf3FlowReachabilityDiagnosticEnabled()) return;
  const int32 Step = GetCurrentFixedStepIndex();
  if (Stage == ECrowdDemoFlowReachabilityStage::StepStart
    || FlowReachabilityPreviousStep != Step)
  {
    FlowReachabilityPreviousStage.Reset();
    FlowReachabilityPreviousStep = Step;
  }
  FCrowdDemoTrafficMetrics& Metrics = LastCompareMetrics.TrafficMetrics;
  TMap<int32, FCrowdDemoFlowReachabilityStageSample> Current;
  Current.Reserve(Samples.Num());
  if (Stage == ECrowdDemoFlowReachabilityStage::MovementFinalize)
  {
    Metrics.FlowReachableFinalCount = 0;
    Metrics.FlowOutOfBoundsFinalCount = 0;
    Metrics.FlowBlockedRasterFinalCount = 0;
    Metrics.FlowUnreachableFreeFinalCount = 0;
    Metrics.FlowFinalInvalidAgentCount = 0;
    Metrics.InvalidFlowDeadlockCount = 0;
    FlowFinalInvalidAgentIds.Reset();
  }
  for (const FCrowdDemoFlowReachabilityStageSample& Sample : Samples)
  {
    Current.Add(Sample.AgentId, Sample);
    const FCrowdDemoFlowReachabilityStageSample* Previous = FlowReachabilityPreviousStage.Find(Sample.AgentId);
    if (Previous && Previous->Status == ECrowdDemoFlowLocationStatus::Reachable
      && Sample.Status != ECrowdDemoFlowLocationStatus::Reachable)
    {
      int32 WitnessIndex = 0;
      if (Sample.Status == ECrowdDemoFlowLocationStatus::OutOfBounds)
      {
        ++Metrics.FlowReachableToOutOfBoundsCount;
        ++Metrics.ContinuousLegalButOutOfBoundsCount;
        WitnessIndex = 2;
      }
      else if (Sample.Status == ECrowdDemoFlowLocationStatus::BlockedRasterCell)
      {
        ++Metrics.FlowReachableToBlockedCellCount;
        if (Sample.bContinuousPenetrating)
          ++Metrics.BlockedCellAndPenetratingCount;
        else
        {
          ++Metrics.BlockedCellButNotPenetratingCount;
          ++Metrics.ContinuousLegalButBlockedCellCount;
        }
        WitnessIndex = 0;
      }
      else
      {
        ++Metrics.FlowReachableToUnreachableFreeCount;
        if (!Sample.bContinuousPenetrating)
          ++Metrics.ContinuousLegalButUnreachableFreeCount;
        WitnessIndex = 1;
      }
      switch (Stage)
      {
      case ECrowdDemoFlowReachabilityStage::MovementPredict: ++Metrics.FlowTransitionAtPredictCount; break;
      case ECrowdDemoFlowReachabilityStage::ObstacleConstraint: ++Metrics.FlowTransitionAtObstacleConstraintCount; break;
      case ECrowdDemoFlowReachabilityStage::HardPbd: ++Metrics.FlowTransitionAtPbdCount; break;
      case ECrowdDemoFlowReachabilityStage::ObstacleReproject: ++Metrics.FlowTransitionAtObstacleReprojectCount; break;
      default: break;
      }
      FCrowdDemoFlowReachabilityWitness& Witness = FlowReachabilityWitnesses[WitnessIndex];
      if (!Witness.bValid)
      {
        Witness.bValid = true;
        Witness.Stage = Stage;
        Witness.PreviousStatus = Previous->Status;
        Witness.NextStatus = Sample.Status;
        Witness.PreviousStableCellKey = Previous->StableCellKey;
        Witness.NextStableCellKey = Sample.StableCellKey;
        Witness.WorldDelta = Sample.Location - Previous->Location;
        Witness.bBlocked = Sample.Status == ECrowdDemoFlowLocationStatus::BlockedRasterCell;
        Witness.bContinuousPenetrating = Sample.bContinuousPenetrating;
        const FCrowdDemoReachableFlowCellSearchResult Nearest =
          FCrowdDemoSharedFlowFieldKernel::FindNearestReachableCell(SharedFlowField, Sample.Location, 8);
        Witness.NearestReachableCellDistanceCm = Nearest.bFound ? Nearest.WorldDistanceCm : -1.0f;
        if (Nearest.bFound)
          Metrics.FlowNearestReachableDistanceCmMax = FMath::Max(
            Metrics.FlowNearestReachableDistanceCmMax, Nearest.WorldDistanceCm);
      }
    }
    else if (Previous && Previous->Status != ECrowdDemoFlowLocationStatus::Reachable
      && Sample.Status == ECrowdDemoFlowLocationStatus::Reachable)
    {
      ++Metrics.FlowInvalidToReachableRecoveryCount;
    }
    if (Stage == ECrowdDemoFlowReachabilityStage::StepStart
      && Sample.Status != ECrowdDemoFlowLocationStatus::Reachable
      && Sample.Velocity.IsNearlyZero(0.01f))
      ++Metrics.InvalidFlowPreferredZeroCount;
    if (Stage == ECrowdDemoFlowReachabilityStage::MovementFinalize)
    {
      switch (Sample.Status)
      {
      case ECrowdDemoFlowLocationStatus::Reachable: ++Metrics.FlowReachableFinalCount; break;
      case ECrowdDemoFlowLocationStatus::OutOfBounds: ++Metrics.FlowOutOfBoundsFinalCount; break;
      case ECrowdDemoFlowLocationStatus::BlockedRasterCell: ++Metrics.FlowBlockedRasterFinalCount; break;
      case ECrowdDemoFlowLocationStatus::UnreachableFreeCell: ++Metrics.FlowUnreachableFreeFinalCount; break;
      }
      if (Sample.Status != ECrowdDemoFlowLocationStatus::Reachable)
      {
        ++Metrics.FlowFinalInvalidAgentCount;
        FlowFinalInvalidAgentIds.Add(Sample.AgentId);
        if (Sample.Velocity.IsNearlyZero(0.01f)) ++Metrics.InvalidFlowFinalZeroVelocityCount;
        if (FlowCorridorDeadlockAgentIds.Contains(Sample.AgentId)) ++Metrics.InvalidFlowDeadlockCount;
      }
    }
  }
  FlowReachabilityPreviousStage = MoveTemp(Current);
}

int32 UCrowdDemoRoundSimPipelineSubsystem::Sf3GoalDistanceBucket(const float DistanceCm)
{
  if (DistanceCm < 100.0f) return 0;
  if (DistanceCm < 200.0f) return 1;
  if (DistanceCm < 400.0f) return 2;
  if (DistanceCm < 800.0f) return 3;
  if (DistanceCm < 1200.0f) return 4;
  return 5;
}

int32 UCrowdDemoRoundSimPipelineSubsystem::Sf3FlowRegionBucket(
  const FVector& Location,
  const int32 IntegrationCost,
  const float GoalDistanceCm)
{
  if (IntegrationCost == 0) return 0;
  if (GoalDistanceCm <= 400.0f) return 1;
  if (Location.Y > 750.0f) return 2;
  if (Location.Y > -2050.0f && Location.Y < -650.0f) return 3;
  if (Location.Y <= -2050.0f) return 4;
  return 5;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSf3GoalOrcaStep(
  const TConstArrayView<FCrowdDemoOrcaAgent> Agents,
  const TConstArrayView<FCrowdDemoOrcaResult> Results)
{
  if (!IsSf3GoalCongestionDiagnosticEnabled()) return;
  FVector Goal = FVector(GetRules().FlowFieldConfig.GoalLocation);
  if (!GetRules().TrafficCohorts.IsEmpty())
  {
    Goal = FVector(GetRules().TrafficCohorts[0].FlowFieldConfig.GoalLocation);
  }
  TMap<int32, const FCrowdDemoOrcaAgent*> AgentById;
  TSet<int32> ReachedNow;
  for (const FCrowdDemoOrcaAgent& Agent : Agents)
  {
    AgentById.Add(Agent.AgentId, &Agent);
    if (FlowGoalReachedAgentIds.Contains(Agent.AgentId)
      || (Agent.Position - FVector2f(Goal.X, Goal.Y)).Size() <= 140.0f)
    {
      ReachedNow.Add(Agent.AgentId);
    }
  }
  for (const FCrowdDemoOrcaResult& Result : Results)
  {
    const FCrowdDemoOrcaAgent* const* AgentPtr = AgentById.Find(Result.AgentId);
    if (!AgentPtr) continue;
    const FCrowdDemoOrcaAgent& Agent = **AgentPtr;
    FCrowdDemoSf3GoalAgentDiagnostic& Diagnostic = Sf3GoalDiagnostics.FindOrAdd(Result.AgentId);
    Diagnostic.AgentId = Result.AgentId;
    Diagnostic.FinalLocation = FVector(Agent.Position.X, Agent.Position.Y, Goal.Z);
    Diagnostic.FinalVelocity = FVector(Result.Velocity.X, Result.Velocity.Y, 0.0f);
    Diagnostic.LastPreferredVelocity = Agent.PreferredVelocity;
    Diagnostic.RadiusCm = Agent.RadiusCm;
    Diagnostic.IntegrationCost = Agent.IntegrationCost;
    Diagnostic.bEverReached |= ReachedNow.Contains(Result.AgentId);
    ++Diagnostic.ProcessedAgentSteps;
    Diagnostic.PreferredFeasibleCount += Result.FailureReason == ECrowdDemoOrcaFeasibility::PreferredFeasible ? 1 : 0;
    Diagnostic.LpFeasibleCount += Result.FailureReason == ECrowdDemoOrcaFeasibility::FormalLpFeasible
      || Result.FailureReason == ECrowdDemoOrcaFeasibility::FormalLpQuantizedRecovered ? 1 : 0;
    Diagnostic.InfeasibleCount += Result.bInfeasible ? 1 : 0;
    Diagnostic.FallbackStopCount += Result.FallbackStage == 4 ? 1 : 0;
    Diagnostic.StopFeasibleCount += Result.Feasibility == ECrowdDemoOrcaFeasibility::StopFeasible ? 1 : 0;
    Diagnostic.StopViolationCount += Result.Feasibility == ECrowdDemoOrcaFeasibility::StopViolation ? 1 : 0;
    Diagnostic.ContinuousFeasibleQuantizedFailureCount +=
      Result.FailureReason == ECrowdDemoOrcaFeasibility::ContinuousFeasibleQuantizedEmpty ? 1 : 0;
    Diagnostic.GenuineSpeedCircleEmptyCount +=
      Result.FailureReason == ECrowdDemoOrcaFeasibility::SingleConstraintOutsideSpeedCircle ? 1 : 0;
    Diagnostic.NeighborCounts.Add(static_cast<float>(Result.NeighborCount));
    Diagnostic.ConstraintCounts.Add(static_cast<float>(Result.ConstraintCount));
    const float Speed = Result.Velocity.Size();
    if (Speed < 1.0f)
    {
      Diagnostic.CurrentStoppedSeconds += GetCurrentFixedStepSeconds();
      Diagnostic.MaxStoppedSeconds = FMath::Max(
        Diagnostic.MaxStoppedSeconds, Diagnostic.CurrentStoppedSeconds);
    }
    else
    {
      Diagnostic.CurrentStoppedSeconds = 0.0f;
    }
    for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
    {
      if (ReachedNow.Contains(Constraint.OtherAgentId))
      {
        ++Diagnostic.ConstraintsAgainstReached;
        Diagnostic.bHadReachedNeighbor = true;
      }
      else
      {
        ++Diagnostic.ConstraintsAgainstNonReached;
      }
    }
    Diagnostic.LpFailedZeroFeasibleCount +=
      Result.FailureReason == ECrowdDemoOrcaFeasibility::FormalLpMissedZeroRecovered ? 1 : 0;
    Diagnostic.LpFailedOracleFeasibleCount +=
      Result.FailureReason == ECrowdDemoOrcaFeasibility::FormalLpMissedOracleRecovered ? 1 : 0;
    Diagnostic.LpFailedOracleNoWitnessCount +=
      Result.FailureReason == ECrowdDemoOrcaFeasibility::TrueNoFeasibleWitness ? 1 : 0;
  }
}

int32 UCrowdDemoRoundSimPipelineSubsystem::GetCurrentFixedStepIndex() const
{
  if (!IsActive() || CurrentFixedStepSeconds <= 0.0f)
  {
    return INDEX_NONE;
  }
  return FMath::Max(0, FMath::RoundToInt(
    ((bStepInProgress ? CurrentStepStartServerTimeSeconds : SimulatedServerTimeSeconds)
      - ActivePlan.StartServerTimeSeconds)
    / CurrentFixedStepSeconds));
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSf3StageHash(
  const ECrowdDemoSf3DeterminismStage Stage,
  const uint32 Hash,
  const int32 ItemCount,
  const TConstArrayView<int32> StableKeys)
{
  if (!IsSf3DeterminismDiagnosticEnabled())
  {
    return;
  }
  FCrowdDemoSf3StageHash& Snapshot = Sf3StageHashes[static_cast<int32>(Stage)];
  Snapshot.Hash = Hash;
  Snapshot.ItemCount = ItemCount;
  Snapshot.FixedStepIndex = GetCurrentFixedStepIndex();
  Snapshot.StableKeys.Reset();
  const int32 KeyCount = FMath::Min(8, StableKeys.Num());
  Snapshot.StableKeys.Append(StableKeys.GetData(), KeyCount);
  const int32 StepIndex = Snapshot.FixedStepIndex;
  Sf3StageHashHistory.FindOrAdd(StepIndex)[static_cast<int32>(Stage)] = Snapshot;
  const int32 OldestStepToKeep = StepIndex - 128;
  Sf3StageHashHistory.Remove(OldestStepToKeep);
}

const FCrowdDemoSf3StageHash& UCrowdDemoRoundSimPipelineSubsystem::GetSf3StageHash(
  const ECrowdDemoSf3DeterminismStage Stage) const
{
  return Sf3StageHashes[static_cast<int32>(Stage)];
}

void UCrowdDemoRoundSimPipelineSubsystem::LogSf3DiagnosticBoundary(
  const int32 CorrectionRevision,
  const TCHAR* Phase,
  const int32 FixedStepOverride) const
{
  if (!IsSf3DeterminismDiagnosticEnabled())
  {
    return;
  }
  const int32 StepIndex = FixedStepOverride != INDEX_NONE
    ? FixedStepOverride
    : GetSf3StageHash(ECrowdDemoSf3DeterminismStage::FinalState).FixedStepIndex;
  const auto* Historical = Sf3StageHashHistory.Find(StepIndex);
  const auto& Hashes = Historical ? *Historical : Sf3StageHashes;
  const FCrowdDemoSf3StageHash& Final = Hashes[static_cast<int32>(ECrowdDemoSf3DeterminismStage::FinalState)];
  const TCHAR* Role = GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoSf3StageHash role=%s phase=%s round_id=%d correction_revision=%d fixed_step=%d counts=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d hashes=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u source=MassPipeline"),
    Role, Phase, GetCurrentRoundId(), CorrectionRevision, Final.FixedStepIndex,
    Hashes[0].ItemCount, Hashes[1].ItemCount, Hashes[2].ItemCount,
    Hashes[3].ItemCount, Hashes[4].ItemCount, Hashes[5].ItemCount,
    Hashes[6].ItemCount, Hashes[7].ItemCount, Hashes[8].ItemCount,
    Hashes[9].ItemCount, Hashes[10].ItemCount,
    Hashes[0].Hash, Hashes[1].Hash, Hashes[2].Hash,
    Hashes[3].Hash, Hashes[4].Hash, Hashes[5].Hash,
    Hashes[6].Hash, Hashes[7].Hash, Hashes[8].Hash,
    Hashes[9].Hash, Hashes[10].Hash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSf3RollbackSnapshot(
  const int32 FixedStepIndex,
  TArray<FCrowdDemoSf3RollbackAgentState>&& Agents)
{
  if (!IsActive() || !IsTrafficScenario(GetRules().Scenario))
  {
    return;
  }
  Agents.Sort([](const FCrowdDemoSf3RollbackAgentState& A, const FCrowdDemoSf3RollbackAgentState& B)
  {
    return A.AgentId < B.AgentId;
  });
  FCrowdDemoSf3RollbackSnapshot& Snapshot = Sf3RollbackHistory.FindOrAdd(FixedStepIndex);
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.Agents = MoveTemp(Agents);
  Snapshot.Portals = PreparedTrafficPortals;
  Snapshot.TargetFact = PursuitTargetFact;
  Snapshot.PositionCandidates = PreparedPositionCandidates;
  Snapshot.PositionAssignments = PreparedPositionAssignments;
  Snapshot.HoldingCandidates = PreparedHoldingCandidates;
  Snapshot.TransitCapacitySelection = TransitCapacitySelection;
  Snapshot.HoldingCompatibilities = PreparedHoldingCompatibilities;
  Snapshot.HoldingAssignments = PreparedHoldingAssignments;
  Snapshot.CommitRequests = PreparedCommitRequests;
  Snapshot.CommitGateResult = PreparedCommitGateResult;
  Snapshot.SteeringGuidance = PreparedSteeringGuidance;
  Snapshot.HoldingSummary = HoldingSummary;
  Snapshot.PositionApproachRoutes = PreparedPositionApproachRoutes;
  Snapshot.FrontPhaseReservationRequests = PreparedFrontPhaseReservationRequests;
  Snapshot.FrontPhaseReservationResult = PreparedFrontPhaseReservationResult;
  Snapshot.FrontPhaseReservationDecisions = PreparedFrontPhaseReservationDecisions;
  Snapshot.FrontAdmissionResult = PreparedFrontAdmissionResult;
  Snapshot.FrontReservationWaitEdges = PreparedFrontReservationWaitEdges;
  Snapshot.FrontReservationWaitGraphSummary = FrontReservationWaitGraphSummary;
  Snapshot.FrontReservationWaitGraphFixture = FrontReservationWaitGraphFixture;
  Snapshot.PositioningSummary = LastPositioningSummary;
  Snapshot.PositionIngressSummary = LastPositionIngressSummary;
  Snapshot.PositionIngressFixture = MinimumPositionIngressFixture;
  Snapshot.PositionIngressLowSpeedSteps = PositionIngressLowSpeedStepsByAgentId;
  Snapshot.PositionPromotedAgentIds = PositionPromotedAgentIds;
  Snapshot.PositionCandidateBuiltRevision = PositionCandidateBuiltRevision;
  Snapshot.PositionAssignmentRevision = PositionAssignmentRevision;
  Snapshot.HoldingCompatibilityInputHash = HoldingCompatibilityInputHash;
  Snapshot.JointAssignmentInputHash = JointAssignmentInputHash;
  Snapshot.ResidualPositioningSummary = ResidualPositioningSummary;
  Snapshot.HoldingMatchingResult = HoldingMatchingResult;
  Snapshot.HoldingHallFixture = HoldingHallFixture;
  Snapshot.HallGeometryFixture = HallGeometryFixture;
  Snapshot.JointPositioningResult = JointPositioningResult;
  Snapshot.JointCommitResidualResult = JointCommitResidualResult;
  Snapshot.UnfinishedBoundaryFixture = UnfinishedBoundaryFixture;
  Snapshot.PhysicalUnsatisfiedBoundaryFixture = PhysicalUnsatisfiedBoundaryFixture;
  Snapshot.TransitJointDiagnosticFixture = TransitJointDiagnosticFixture;
  Snapshot.TransitCapacityShadowAgents = TransitCapacityShadowAgents;
  Snapshot.TransitCapacityShadowPairs = TransitCapacityShadowPairs;
  Snapshot.TransitCapacityShadowComponents = TransitCapacityShadowComponents;
  Snapshot.TransitCapacityShadowResults = TransitCapacityShadowResults;
  Snapshot.TransitCapacityShadowSummary = TransitCapacityShadowSummary;
  Snapshot.TransitCapacityShadowSolverMsSampleCount =
    TransitCapacityShadowSolverMsSamples.Num();
  Snapshot.ElasticCrowdShadowAgents = ElasticCrowdShadowAgents;
  Snapshot.ElasticCrowdShadowResults = ElasticCrowdShadowResults;
  Snapshot.ElasticCrowdShadowSummary = ElasticCrowdShadowSummary;
  Snapshot.ElasticParallelState = ElasticParallelState;
  Snapshot.ElasticBaselineDesiredForward = ElasticBaselineDesiredForward;
  Snapshot.ElasticBaselineActualForward = ElasticBaselineActualForward;
  Snapshot.ElasticTwinDesiredForward = ElasticTwinDesiredForward;
  Snapshot.ElasticTwinActualForward = ElasticTwinActualForward;
  Snapshot.ElasticZeroProgressSteps = ElasticZeroProgressSteps;
  Snapshot.ElasticSpacingDeficitSampleCount = ElasticSpacingDeficitSamples.Num();
  Snapshot.ElasticTransitDeficitSampleCount = ElasticTransitDeficitSamples.Num();
  Snapshot.ElasticRecoveryErrorSampleCount = ElasticRecoveryErrorSamples.Num();
  Snapshot.ElasticSolverMsSampleCount = ElasticSolverMsSamples.Num();
  Snapshot.ElasticFailureFixture = ElasticFailureFixture;
  Snapshot.TransitCapacityFailureFixture = TransitCapacityFailureFixture;
  Snapshot.SteeringStateHash = SteeringStateHash;
  Snapshot.TrafficRoundHash = TrafficRoundHash;
  Snapshot.PortalRoundHash = PortalRoundHash;
  Snapshot.OrcaRoundHash = OrcaRoundHash;
  Snapshot.PriorityOrcaRoundHash = PriorityOrcaRoundHash;
  Snapshot.TrafficFixedStepIndex = TrafficFixedStepIndex;
  Snapshot.TrafficMetrics = LastCompareMetrics.TrafficMetrics;
  Snapshot.TrafficQueueSampleCount = TrafficQueueSamples.Num();
  Snapshot.TrafficOccupiedSampleCount = TrafficOccupiedSamples.Num();
  Snapshot.BandLateralErrorSampleCount = BandLateralErrorSamples.Num();
  Snapshot.OrcaNeighborSampleCount = OrcaNeighborSamples.Num();
  Snapshot.OrcaConstraintSampleCount = OrcaConstraintSamples.Num();
  Snapshot.OrcaSolverMsSampleCount = OrcaSolverMsSamples.Num();
  Snapshot.OrcaOracleRecoveryMsSampleCount = OrcaOracleRecoveryMsSamples.Num();
  Snapshot.PhaseReservationHeldStepSampleCount = PhaseReservationHeldStepSamples.Num();
  Snapshot.ParticleCandidateSummary = LastParticleCandidateSummary;
  Snapshot.ParticleAppliedSummary = LastParticleAppliedSummary;
  Snapshot.ParticleSolverMsSampleCount = ParticleSolverMillisecondsSamples.Num();
  Snapshot.ParticleCandidateStateHash = ParticleCandidateStateHash;
  Snapshot.ParticleAppliedStateHash = ParticleAppliedStateHash;
  Snapshot.ParticleInvalidStepCount = ParticleInvalidStepCount;
  Snapshot.ParticleGlobalFallbackStepCount = ParticleGlobalFallbackStepCount;
  Snapshot.ParticleStepCount = ParticleStepCount;
  Snapshot.ParticleSettlingWindowCount = ParticleSettlingWindowCount;
  Snapshot.ParticleSettlingSteps = ParticleSettlingSteps;
  Snapshot.ParticlePreviousSoftErrorP95 = ParticlePreviousSoftErrorP95;
  Snapshot.bParticleConstraintRunFailure = bParticleConstraintRunFailure;
  Snapshot.ParticleFailureFixture = ParticleFailureFixture;
  if (IsSf3GoalCongestionDiagnosticEnabled())
  {
    Snapshot.GoalDiagnostics = Sf3GoalDiagnostics;
  }
  if (IsSf3FlowReachabilityDiagnosticEnabled())
  {
    Snapshot.FlowReachabilityPreviousStage = FlowReachabilityPreviousStage;
    Snapshot.FlowReachabilityPreviousStep = FlowReachabilityPreviousStep;
  }
  Snapshot.Portals.Sort([](const FCrowdDemoTrafficPortalRuntime& A, const FCrowdDemoTrafficPortalRuntime& B)
  {
    return A.Portal.PortalId < B.Portal.PortalId;
  });
  Sf3RollbackHistory.Remove(FixedStepIndex - 128);
}

const FCrowdDemoSf3RollbackSnapshot* UCrowdDemoRoundSimPipelineSubsystem::FindSf3RollbackSnapshot(
  const int32 FixedStepIndex) const
{
  return Sf3RollbackHistory.Find(FixedStepIndex);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackSnapshot(
  const int32 FixedStepIndex,
  TArray<FCrowdDemoSoftPressureRollbackAgentState>&& Agents)
{
  if (!IsActive() || GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure)
    return;
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  FCrowdDemoSoftPressureRollbackSnapshot& Snapshot =
    SoftPressureRollbackHistory.FindOrAdd(FixedStepIndex);
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.Agents = MoveTemp(Agents);
  Snapshot.ParticleCandidateSummary = LastParticleCandidateSummary;
  Snapshot.ParticleAppliedSummary = LastParticleAppliedSummary;
  Snapshot.ParticleSolverMsSampleCount = ParticleSolverMillisecondsSamples.Num();
  Snapshot.ParticleCandidateStateHash = ParticleCandidateStateHash;
  Snapshot.ParticleAppliedStateHash = ParticleAppliedStateHash;
  Snapshot.ParticleInvalidStepCount = ParticleInvalidStepCount;
  Snapshot.ParticleGlobalFallbackStepCount = ParticleGlobalFallbackStepCount;
  Snapshot.ParticleStepCount = ParticleStepCount;
  Snapshot.ParticleSettlingWindowCount = ParticleSettlingWindowCount;
  Snapshot.ParticleSettlingSteps = ParticleSettlingSteps;
  Snapshot.ParticlePreviousSoftErrorP95 = ParticlePreviousSoftErrorP95;
  Snapshot.bParticleConstraintRunFailure = bParticleConstraintRunFailure;
  Snapshot.ParticleFailureFixture = ParticleFailureFixture;
  Snapshot.FlowGoalReachedAgentIds = FlowGoalReachedAgentIds;
  Snapshot.FlowWallPassAgentIds = FlowWallPassAgentIds;
  Snapshot.FlowCorridorExitAgentIds = FlowCorridorExitAgentIds;
  Snapshot.FlowTurnExitAgentIds = FlowTurnExitAgentIds;
  Snapshot.FlowLowSpeedSecondsByAgentId = FlowLowSpeedSecondsByAgentId;
  Snapshot.FlowCorridorDeadlockAgentIds = FlowCorridorDeadlockAgentIds;
  Snapshot.CompareMetrics = LastCompareMetrics;
  SoftPressureRollbackHistory.Remove(FixedStepIndex - 128);
}

const FCrowdDemoSoftPressureRollbackSnapshot*
UCrowdDemoRoundSimPipelineSubsystem::FindSoftPressureRollbackSnapshot(
  const int32 FixedStepIndex) const
{
  return SoftPressureRollbackHistory.Find(FixedStepIndex);
}

void UCrowdDemoRoundSimPipelineSubsystem::RestoreSoftPressureRuntime(
  const FCrowdDemoSoftPressureRollbackSnapshot& Snapshot)
{
  LastParticleCandidateSummary = Snapshot.ParticleCandidateSummary;
  LastParticleAppliedSummary = Snapshot.ParticleAppliedSummary;
  ParticleSolverMillisecondsSamples.SetNum(FMath::Min(
    ParticleSolverMillisecondsSamples.Num(), Snapshot.ParticleSolverMsSampleCount));
  ParticleCandidateStateHash = Snapshot.ParticleCandidateStateHash;
  ParticleAppliedStateHash = Snapshot.ParticleAppliedStateHash;
  ParticleInvalidStepCount = Snapshot.ParticleInvalidStepCount;
  ParticleGlobalFallbackStepCount = Snapshot.ParticleGlobalFallbackStepCount;
  ParticleStepCount = Snapshot.ParticleStepCount;
  ParticleSettlingWindowCount = Snapshot.ParticleSettlingWindowCount;
  ParticleSettlingSteps = Snapshot.ParticleSettlingSteps;
  ParticlePreviousSoftErrorP95 = Snapshot.ParticlePreviousSoftErrorP95;
  bParticleConstraintRunFailure = Snapshot.bParticleConstraintRunFailure;
  ParticleFailureFixture = Snapshot.ParticleFailureFixture;
  FlowGoalReachedAgentIds = Snapshot.FlowGoalReachedAgentIds;
  FlowWallPassAgentIds = Snapshot.FlowWallPassAgentIds;
  FlowCorridorExitAgentIds = Snapshot.FlowCorridorExitAgentIds;
  FlowTurnExitAgentIds = Snapshot.FlowTurnExitAgentIds;
  FlowLowSpeedSecondsByAgentId = Snapshot.FlowLowSpeedSecondsByAgentId;
  FlowCorridorDeadlockAgentIds = Snapshot.FlowCorridorDeadlockAgentIds;
  LastCompareMetrics = Snapshot.CompareMetrics;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackOutcome(
  const bool bHit,
  const bool bAgentMismatch,
  const int32 ReplayedSteps)
{
  SoftPressureRollbackSnapshotHitCount += bHit ? 1 : 0;
  SoftPressureRollbackSnapshotMissCount += bHit ? 0 : 1;
  SoftPressureRollbackAgentMismatchCount += bAgentMismatch ? 1 : 0;
  SoftPressureRollbackReplayedStepCount += FMath::Max(0, ReplayedSteps);
  const TCHAR* Role = GetWorld() && GetWorld()->GetNetMode() == NM_Client
    ? TEXT("client") : TEXT("server");
  if (bHit && !bAgentMismatch)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSoftPressureRollback role=%s round_id=%d hit=1 miss=0 mismatch=0 replayed_steps=%d totals=%d,%d,%d,%d"),
      Role, GetCurrentRoundId(), FMath::Max(0, ReplayedSteps),
      SoftPressureRollbackSnapshotHitCount, SoftPressureRollbackSnapshotMissCount,
      SoftPressureRollbackAgentMismatchCount, SoftPressureRollbackReplayedStepCount);
  }
  else
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoSoftPressureRollback role=%s round_id=%d hit=0 miss=1 mismatch=%d replayed_steps=0 totals=%d,%d,%d,%d VIOLATION"),
      Role, GetCurrentRoundId(), bAgentMismatch ? 1 : 0,
      SoftPressureRollbackSnapshotHitCount, SoftPressureRollbackSnapshotMissCount,
      SoftPressureRollbackAgentMismatchCount, SoftPressureRollbackReplayedStepCount);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RestoreSf3PortalRuntime(
  const FCrowdDemoSf3RollbackSnapshot& Snapshot)
{
  PreparedTrafficPortals = Snapshot.Portals;
  PursuitTargetFact = Snapshot.TargetFact;
  PreparedPositionCandidates = Snapshot.PositionCandidates;
  PreparedPositionAssignments = Snapshot.PositionAssignments;
  PreparedHoldingCandidates = Snapshot.HoldingCandidates;
  TransitCapacitySelection = Snapshot.TransitCapacitySelection;
  PreparedHoldingCompatibilities = Snapshot.HoldingCompatibilities;
  PreparedHoldingAssignments = Snapshot.HoldingAssignments;
  PreparedCommitRequests = Snapshot.CommitRequests;
  PreparedCommitGateResult = Snapshot.CommitGateResult;
  PreparedSteeringGuidance = Snapshot.SteeringGuidance;
  HoldingSummary = Snapshot.HoldingSummary;
  PreparedPositionApproachRoutes = Snapshot.PositionApproachRoutes;
  PreparedFrontPhaseReservationRequests = Snapshot.FrontPhaseReservationRequests;
  PreparedFrontPhaseReservationResult = Snapshot.FrontPhaseReservationResult;
  PreparedFrontPhaseReservationDecisions = Snapshot.FrontPhaseReservationDecisions;
  PreparedFrontAdmissionResult = Snapshot.FrontAdmissionResult;
  PreparedFrontReservationWaitEdges = Snapshot.FrontReservationWaitEdges;
  FrontReservationWaitGraphSummary = Snapshot.FrontReservationWaitGraphSummary;
  FrontReservationWaitGraphFixture = Snapshot.FrontReservationWaitGraphFixture;
  LastPositioningSummary = Snapshot.PositioningSummary;
  LastPositionIngressSummary = Snapshot.PositionIngressSummary;
  MinimumPositionIngressFixture = Snapshot.PositionIngressFixture;
  PositionIngressLowSpeedStepsByAgentId = Snapshot.PositionIngressLowSpeedSteps;
  PositionPromotedAgentIds = Snapshot.PositionPromotedAgentIds;
  PositionCandidateBuiltRevision = Snapshot.PositionCandidateBuiltRevision;
  PositionAssignmentRevision = Snapshot.PositionAssignmentRevision;
  HoldingCompatibilityInputHash = Snapshot.HoldingCompatibilityInputHash;
  JointAssignmentInputHash = Snapshot.JointAssignmentInputHash;
  ResidualPositioningSummary = Snapshot.ResidualPositioningSummary;
  HoldingMatchingResult = Snapshot.HoldingMatchingResult;
  HoldingHallFixture = Snapshot.HoldingHallFixture;
  HallGeometryFixture = Snapshot.HallGeometryFixture;
  JointPositioningResult = Snapshot.JointPositioningResult;
  JointCommitResidualResult = Snapshot.JointCommitResidualResult;
  UnfinishedBoundaryFixture = Snapshot.UnfinishedBoundaryFixture;
  PhysicalUnsatisfiedBoundaryFixture = Snapshot.PhysicalUnsatisfiedBoundaryFixture;
  TransitJointDiagnosticFixture = Snapshot.TransitJointDiagnosticFixture;
  TransitCapacityShadowAgents = Snapshot.TransitCapacityShadowAgents;
  TransitCapacityShadowPairs = Snapshot.TransitCapacityShadowPairs;
  TransitCapacityShadowComponents = Snapshot.TransitCapacityShadowComponents;
  TransitCapacityShadowResults = Snapshot.TransitCapacityShadowResults;
  TransitCapacityShadowSummary = Snapshot.TransitCapacityShadowSummary;
  ElasticCrowdShadowAgents = Snapshot.ElasticCrowdShadowAgents;
  ElasticCrowdShadowResults = Snapshot.ElasticCrowdShadowResults;
  ElasticCrowdShadowSummary = Snapshot.ElasticCrowdShadowSummary;
  ElasticParallelState = Snapshot.ElasticParallelState;
  ElasticBaselineDesiredForward = Snapshot.ElasticBaselineDesiredForward;
  ElasticBaselineActualForward = Snapshot.ElasticBaselineActualForward;
  ElasticTwinDesiredForward = Snapshot.ElasticTwinDesiredForward;
  ElasticTwinActualForward = Snapshot.ElasticTwinActualForward;
  ElasticZeroProgressSteps = Snapshot.ElasticZeroProgressSteps;
  ElasticFailureFixture = Snapshot.ElasticFailureFixture;
  TransitCapacityFailureFixture = Snapshot.TransitCapacityFailureFixture;
  SteeringStateHash = Snapshot.SteeringStateHash;
  TrafficRoundHash = Snapshot.TrafficRoundHash;
  PortalRoundHash = Snapshot.PortalRoundHash;
  OrcaRoundHash = Snapshot.OrcaRoundHash;
  PriorityOrcaRoundHash = Snapshot.PriorityOrcaRoundHash;
  TrafficFixedStepIndex = Snapshot.TrafficFixedStepIndex;
  LastCompareMetrics.TrafficMetrics = Snapshot.TrafficMetrics;
  TrafficQueueSamples.SetNum(FMath::Min(TrafficQueueSamples.Num(), Snapshot.TrafficQueueSampleCount));
  TrafficOccupiedSamples.SetNum(FMath::Min(TrafficOccupiedSamples.Num(), Snapshot.TrafficOccupiedSampleCount));
  BandLateralErrorSamples.SetNum(FMath::Min(BandLateralErrorSamples.Num(), Snapshot.BandLateralErrorSampleCount));
  OrcaNeighborSamples.SetNum(FMath::Min(OrcaNeighborSamples.Num(), Snapshot.OrcaNeighborSampleCount));
  OrcaConstraintSamples.SetNum(FMath::Min(OrcaConstraintSamples.Num(), Snapshot.OrcaConstraintSampleCount));
  OrcaSolverMsSamples.SetNum(FMath::Min(OrcaSolverMsSamples.Num(), Snapshot.OrcaSolverMsSampleCount));
  TransitCapacityShadowSolverMsSamples.SetNum(FMath::Min(
    TransitCapacityShadowSolverMsSamples.Num(),
    Snapshot.TransitCapacityShadowSolverMsSampleCount));
  ElasticSpacingDeficitSamples.SetNum(FMath::Min(
    ElasticSpacingDeficitSamples.Num(), Snapshot.ElasticSpacingDeficitSampleCount));
  ElasticTransitDeficitSamples.SetNum(FMath::Min(
    ElasticTransitDeficitSamples.Num(), Snapshot.ElasticTransitDeficitSampleCount));
  ElasticRecoveryErrorSamples.SetNum(FMath::Min(
    ElasticRecoveryErrorSamples.Num(), Snapshot.ElasticRecoveryErrorSampleCount));
  ElasticSolverMsSamples.SetNum(FMath::Min(
    ElasticSolverMsSamples.Num(), Snapshot.ElasticSolverMsSampleCount));
  OrcaOracleRecoveryMsSamples.SetNum(FMath::Min(
    OrcaOracleRecoveryMsSamples.Num(), Snapshot.OrcaOracleRecoveryMsSampleCount));
  PhaseReservationHeldStepSamples.SetNum(FMath::Min(
    PhaseReservationHeldStepSamples.Num(), Snapshot.PhaseReservationHeldStepSampleCount));
  LastParticleCandidateSummary = Snapshot.ParticleCandidateSummary;
  LastParticleAppliedSummary = Snapshot.ParticleAppliedSummary;
  ParticleSolverMillisecondsSamples.SetNum(FMath::Min(
    ParticleSolverMillisecondsSamples.Num(), Snapshot.ParticleSolverMsSampleCount));
  ParticleCandidateStateHash = Snapshot.ParticleCandidateStateHash;
  ParticleAppliedStateHash = Snapshot.ParticleAppliedStateHash;
  ParticleInvalidStepCount = Snapshot.ParticleInvalidStepCount;
  ParticleGlobalFallbackStepCount = Snapshot.ParticleGlobalFallbackStepCount;
  ParticleStepCount = Snapshot.ParticleStepCount;
  ParticleSettlingWindowCount = Snapshot.ParticleSettlingWindowCount;
  ParticleSettlingSteps = Snapshot.ParticleSettlingSteps;
  ParticlePreviousSoftErrorP95 = Snapshot.ParticlePreviousSoftErrorP95;
  bParticleConstraintRunFailure = Snapshot.bParticleConstraintRunFailure;
  ParticleFailureFixture = Snapshot.ParticleFailureFixture;
  if (IsSf3GoalCongestionDiagnosticEnabled())
  {
    Sf3GoalDiagnostics = Snapshot.GoalDiagnostics;
  }
  if (IsSf3FlowReachabilityDiagnosticEnabled())
  {
    FlowReachabilityPreviousStage = Snapshot.FlowReachabilityPreviousStage;
    FlowReachabilityPreviousStep = Snapshot.FlowReachabilityPreviousStep;
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::RecordSf3CompletedRoundHash(const uint32 AgentStateHash)
{
  if (FirstCompletedSf3AgentStateHash == 0)
  {
    FirstCompletedSf3AgentStateHash = AgentStateHash;
  }
  const bool bMatch = AgentStateHash == FirstCompletedSf3AgentStateHash;
  const TCHAR* Role = GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
  if (bMatch)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3RoundReplay role=%s round_id=%d agent_state_hash=%u first_round_hash=%u match=1"),
      Role, GetCurrentRoundId(), AgentStateHash, FirstCompletedSf3AgentStateHash);
  }
  else
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoSf3RoundReplay role=%s round_id=%d agent_state_hash=%u first_round_hash=%u match=0 VIOLATION"),
      Role, GetCurrentRoundId(), AgentStateHash, FirstCompletedSf3AgentStateHash);
  }
  return bMatch;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordFlowAgentSamples(
  const TConstArrayView<FCrowdDemoRoundFlowAgentSample> Samples,
  const bool bClient)
{
  int32 UnreachableCount = 0;
  int32 PenetrationCount = 0;
  const FVector Goal = FVector(GetRules().FlowFieldConfig.GoalLocation);
  for (const FCrowdDemoRoundFlowAgentSample& Sample : Samples)
  {
    UnreachableCount += Sample.bUnreachable ? 1 : 0;
    PenetrationCount += Sample.bPenetrating ? 1 : 0;
    const bool bInsideCorridorZone = Sample.Location.Y > -2050.0f
      && Sample.Location.Y < -650.0f;
    float& LowSpeedSeconds = FlowLowSpeedSecondsByAgentId.FindOrAdd(Sample.AgentId);
    if (bInsideCorridorZone && Sample.Velocity.Size2D() < 10.0f)
    {
      LowSpeedSeconds += GetCurrentFixedStepSeconds();
      if (LowSpeedSeconds > 1.5f)
      {
        FlowCorridorDeadlockAgentIds.Add(Sample.AgentId);
      }
    }
    else
    {
      LowSpeedSeconds = 0.0f;
    }
    if (FVector::DistSquared2D(Sample.Location, Goal) <= FMath::Square(140.0f))
    {
      FlowGoalReachedAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > -1950.0f)
    {
      FlowWallPassAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > -650.0f)
    {
      FlowCorridorExitAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > 750.0f)
    {
      FlowTurnExitAgentIds.Add(Sample.AgentId);
    }
  }
  LastCompareMetrics.FlowUnreachableAgentCount = UnreachableCount;
  LastCompareMetrics.FlowGoalReachedCount = FlowGoalReachedAgentIds.Num();
  LastCompareMetrics.FlowWallPassCount = FlowWallPassAgentIds.Num();
  LastCompareMetrics.FlowCorridorExitCount = FlowCorridorExitAgentIds.Num();
  LastCompareMetrics.FlowTurnExitCount = FlowTurnExitAgentIds.Num();
  LastCompareMetrics.CorridorDeadlockAgentCount = FlowCorridorDeadlockAgentIds.Num();
  if (bClient)
  {
    LastCompareMetrics.ClientSimObstaclePenetrationCount += PenetrationCount;
  }
  else
  {
    LastCompareMetrics.ServerObstaclePenetrationCount += PenetrationCount;
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::TryBeginFixedStep(const float TargetServerTimeSeconds)
{
  if (!bPlanActive || bStepInProgress)
  {
    return false;
  }
  const float RoundEnd = ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds;
  if (SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER >= RoundEnd)
  {
    return false;
  }
  const float StepEnd = FMath::Min(SimulatedServerTimeSeconds + CurrentFixedStepSeconds, RoundEnd);
  if (StepEnd > TargetServerTimeSeconds + KINDA_SMALL_NUMBER)
  {
    return false;
  }
  CurrentStepStartServerTimeSeconds = SimulatedServerTimeSeconds;
  CurrentStepEndServerTimeSeconds = StepEnd;
  bStepInProgress = true;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::FinishFixedStep()
{
  if (bStepInProgress)
  {
    SimulatedServerTimeSeconds = CurrentStepEndServerTimeSeconds;
    ++PlanApplyBoundarySequence;
    bStepInProgress = false;
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsRoundSimScenarioActive() const
{
  return bPlanActive && IsCrowdDemoRoundSimScenario(ActivePlan.Rules.Scenario);
}

void UCrowdDemoRoundSimPipelineSubsystem::SetLastSeparationSummary(
  const int32 GridCells,
  const int32 AppliedAgents,
  const int32 OverlapPairs,
  const int32 SevereOverlapPairs)
{
  LastSeparationGridCellCount = GridCells;
  LastSeparationAppliedAgentCount = AppliedAgents;
  LastSeparationOverlapPairCount = OverlapPairs;
  LastSeparationSevereOverlapPairCount = SevereOverlapPairs;
  if (IsActive() && GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    FlowSeparationOverlapPairSamples.Add(static_cast<float>(OverlapPairs));
    FlowSeparationSevereOverlapPairSamples.Add(static_cast<float>(SevereOverlapPairs));
    LastCompareMetrics.OverlapPairCountP50 = Percentile(FlowSeparationOverlapPairSamples, 0.50f);
    LastCompareMetrics.OverlapPairCountP95 = Percentile(FlowSeparationOverlapPairSamples, 0.95f);
    LastCompareMetrics.OverlapPairCountMax = FMath::Max(LastCompareMetrics.OverlapPairCountMax, OverlapPairs);
    LastCompareMetrics.SevereOverlapPairCountP50 = Percentile(FlowSeparationSevereOverlapPairSamples, 0.50f);
    LastCompareMetrics.SevereOverlapPairCountP95 = Percentile(FlowSeparationSevereOverlapPairSamples, 0.95f);
    LastCompareMetrics.SevereOverlapPairCountMax = FMath::Max(
      LastCompareMetrics.SevereOverlapPairCountMax,
      SevereOverlapPairs);
    LastCompareMetrics.SoftSeparationAppliedAgentCount = FMath::Max(
      LastCompareMetrics.SoftSeparationAppliedAgentCount,
      AppliedAgents);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordHardSeparationPbdSummary(
  const FCrowdDemoHardSeparationPbdSummary& Summary,
  const float SolverMilliseconds)
{
  if (!IsActive() || (GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    && GetRules().Scenario != ECrowdDemoScenario::SimRoundCrowdTraffic))
  {
    return;
  }
  PbdSolverMillisecondsSamples.Add(SolverMilliseconds);
  LastCompareMetrics.PbdCorrectedAgentCount = FMath::Max(
    LastCompareMetrics.PbdCorrectedAgentCount,
    Summary.CorrectedAgentCount);
  LastCompareMetrics.PbdCorrectedPairCount = FMath::Max(
    LastCompareMetrics.PbdCorrectedPairCount,
    Summary.CorrectedPairCount);
  LastCompareMetrics.PbdMaxPairCorrectionCm = FMath::Max(
    LastCompareMetrics.PbdMaxPairCorrectionCm,
    Summary.MaxPairCorrectionCm);
  LastCompareMetrics.PbdMaxAgentTotalCorrectionCm = FMath::Max(
    LastCompareMetrics.PbdMaxAgentTotalCorrectionCm,
    Summary.MaxAgentTotalCorrectionCm);
  LastCompareMetrics.PbdSolverMsP95 = Percentile(PbdSolverMillisecondsSamples, 0.95f);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPbdSafetyDeltas(
  const float ObstacleReprojectDeltaCm,
  const float FinalSafetyDeltaCm)
{
  LastCompareMetrics.PbdMaxObstacleReprojectDeltaCm = FMath::Max(
    LastCompareMetrics.PbdMaxObstacleReprojectDeltaCm,
    ObstacleReprojectDeltaCm);
  LastCompareMetrics.PbdMaxFinalSafetyDeltaCm = FMath::Max(
    LastCompareMetrics.PbdMaxFinalSafetyDeltaCm,
    FinalSafetyDeltaCm);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordParticleConstraintSummary(
  const FCrowdDemoParticleConstraintSummary& CandidateSummary,
  const FCrowdDemoParticleConstraintSummary& AppliedSummary,
  const uint32 AppliedStateHash,
  const bool bGlobalFallback,
  const float SolverMilliseconds)
{
  if (!IsActive() || GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure)
    return;
  LastParticleCandidateSummary = CandidateSummary;
  LastParticleAppliedSummary = AppliedSummary;
  ParticleSolverMillisecondsSamples.Add(FMath::Max(0.0f, SolverMilliseconds));
  ParticleCandidateStateHash = CandidateSummary.CandidateHash;
  ParticleAppliedStateHash = AppliedStateHash;
  if (!CandidateSummary.bValid || !AppliedSummary.bValid)
  {
    ++ParticleInvalidStepCount;
    if (!bParticleConstraintRunFailure)
    {
      bParticleConstraintRunFailure = true;
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoParticleInvalid role=%s round_id=%d fixed_step=%d hard=%d swept=%d obstacle=%d bounds=%d candidate_hash=%u applied_hash=%u action=early_round_failure VIOLATION"),
        GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
        GetCurrentRoundId(), GetCurrentFixedStepIndex(),
        CandidateSummary.HardPairViolationCount + AppliedSummary.HardPairViolationCount,
        CandidateSummary.SweptPairViolationCount + AppliedSummary.SweptPairViolationCount,
        CandidateSummary.ObstaclePenetrationCount + AppliedSummary.ObstaclePenetrationCount,
        CandidateSummary.BoundsViolationCount + AppliedSummary.BoundsViolationCount,
        CandidateSummary.CandidateHash, AppliedStateHash);
    }
  }
  if (bGlobalFallback) ++ParticleGlobalFallbackStepCount;

  FCrowdDemoParticleSettlingTracker SettlingTracker;
  SettlingTracker.StepCount = ParticleStepCount;
  SettlingTracker.ConsecutiveSettledSampleCount = ParticleSettlingWindowCount;
  SettlingTracker.SettlingSteps = ParticleSettlingSteps;
  SettlingTracker.PreviousSoftErrorCmP95 = ParticlePreviousSoftErrorP95;
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
    SettlingTracker, AppliedSummary.MaxAgentCorrectionCm, AppliedSummary.SoftErrorCmP95);
  ParticleStepCount = SettlingTracker.StepCount;
  ParticleSettlingWindowCount = SettlingTracker.ConsecutiveSettledSampleCount;
  ParticleSettlingSteps = SettlingTracker.SettlingSteps;
  ParticlePreviousSoftErrorP95 = SettlingTracker.PreviousSoftErrorCmP95;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordParticleFailureFixture(
  const FCrowdDemoParticleFailureFixture& Fixture)
{
  if (!ParticleFailureFixture.bValid && Fixture.bValid)
    ParticleFailureFixture = Fixture;
}

void UCrowdDemoRoundSimPipelineSubsystem::StopAfterParticleConstraintFailure()
{
  if (GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && bParticleConstraintRunFailure)
  {
    bPlanActive = false;
    bStepInProgress = false;
  }
}

float UCrowdDemoRoundSimPipelineSubsystem::GetParticleSolverMsP95() const
{
  return Percentile(ParticleSolverMillisecondsSamples, 0.95f);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundStart(TConstArrayView<FCrowdDemoRoundAgentState> States)
{
  RoundInitialOverlapPairCount = CountOverlapPairs(States, OverlapRadiusCm);
  RoundInitialSevereOverlapPairCount = CountOverlapPairs(States, SevereOverlapRadiusCm);
  LastCompareMetrics.InitialOverlapPairCount = RoundInitialOverlapPairCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordCorrectionComparisonAndApplied(
  TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
  const FCrowdDemoCorrectionFrame& Frame,
  const float CurrentServerTimeSeconds)
{
  TMap<int32, const FCrowdDemoRoundAgentState*> ClientById;
  for (const FCrowdDemoRoundAgentState& State : ClientStates)
  {
    ClientById.Add(State.AgentId, &State);
  }
  TArray<float> PositionErrors;
  TArray<float> YawErrors;
  TArray<float> VelocityErrors;
  float MaxPositionError = 0.0f;
  int32 MaxAgentId = INDEX_NONE;
  for (const FCrowdDemoRoundAgentState& ServerState : Frame.AgentStates)
  {
    const FCrowdDemoRoundAgentState* const* ClientStatePtr = ClientById.Find(ServerState.AgentId);
    if (!ClientStatePtr)
    {
      continue;
    }
    const FCrowdDemoRoundAgentState& ClientState = **ClientStatePtr;
    const float PositionError = FVector::Dist2D(FVector(ClientState.Location), FVector(ServerState.Location));
    PositionErrors.Add(PositionError);
    YawErrors.Add(FMath::Abs(FMath::FindDeltaAngleDegrees(ClientState.YawDegrees, ServerState.YawDegrees)));
    VelocityErrors.Add(FVector::Dist2D(FVector(ClientState.Velocity), FVector(ServerState.Velocity)));
    if (PositionError > MaxPositionError)
    {
      MaxPositionError = PositionError;
      MaxAgentId = ServerState.AgentId;
    }
  }
  LastCorrectionMetrics.CorrectionFrameRevision = Frame.CorrectionRevision;
  LastCorrectionMetrics.CorrectionFrameLatestRevisionApplied = Frame.CorrectionRevision;
  LastCorrectionMetrics.CorrectionAgentCount = Frame.AgentStates.Num();
  LastCorrectionMetrics.CorrectionPositionErrorCmP50 = Percentile(PositionErrors, 0.50f);
  LastCorrectionMetrics.CorrectionPositionErrorCmP95 = Percentile(PositionErrors, 0.95f);
  LastCorrectionMetrics.CorrectionPositionErrorCmMax = MaxPositionError;
  LastCorrectionMetrics.CorrectionYawErrorDegP95 = Percentile(YawErrors, 0.95f);
  LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95 = Percentile(VelocityErrors, 0.95f);
  LastCorrectionMetrics.CorrectionErrorAgentIdMax = MaxAgentId;
  LastCorrectionMetrics.RoundTimeDeltaMs = FMath::Max(0.0f, (CurrentServerTimeSeconds - Frame.ServerTimeSeconds) * 1000.0f);
  LastCorrectionMetrics.CorrectionFrameAgeMsP95 = LastCorrectionMetrics.RoundTimeDeltaMs;
  LastCorrectionMetrics.CorrectionFrameReplayMsP95 = LastCorrectionMetrics.RoundTimeDeltaMs;
  if (Frame.RoundId > 0)
  {
    CorrectionIntervalPositionP95Samples.Add(LastCorrectionMetrics.CorrectionPositionErrorCmP95);
    CorrectionIntervalPositionMaxSamples.Add(LastCorrectionMetrics.CorrectionPositionErrorCmMax);
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95 = Percentile(
      CorrectionIntervalPositionP95Samples,
      0.95f);
    LastCompareMetrics.CorrectionIntervalPositionErrorCmMax = -1.0f;
    for (const float Sample : CorrectionIntervalPositionMaxSamples)
    {
      LastCompareMetrics.CorrectionIntervalPositionErrorCmMax = FMath::Max(
        LastCompareMetrics.CorrectionIntervalPositionErrorCmMax,
        Sample);
    }
    ++LastCorrectionMetrics.CorrectionFrameAppliedCount;
    ++LastCorrectionMetrics.CorrectionFrameReplayToNowCount;
    LastAppliedCorrectionRevision = Frame.CorrectionRevision;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoCorrectionFrame role=client revision=%d round_id=%d applied_count=%d agents=%d correction_position_error_cm_p50=%.3f correction_position_error_cm_p95=%.3f correction_position_error_cm_max=%.3f correction_yaw_error_deg_p95=%.3f correction_velocity_error_cmps_p95=%.3f round_time_delta_ms=%.3f apply_boundary_time=%.3f source=MassPipeline"),
      Frame.CorrectionRevision,
      Frame.RoundId,
      LastCorrectionMetrics.CorrectionFrameAppliedCount,
      Frame.AgentStates.Num(),
      LastCorrectionMetrics.CorrectionPositionErrorCmP50,
      LastCorrectionMetrics.CorrectionPositionErrorCmP95,
      LastCorrectionMetrics.CorrectionPositionErrorCmMax,
      LastCorrectionMetrics.CorrectionYawErrorDegP95,
      LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95,
      LastCorrectionMetrics.RoundTimeDeltaMs,
      SimulatedServerTimeSeconds);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundResultComparisonAndApplied(
  TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
  const FCrowdDemoRoundResultPacket& Packet)
{
  if (GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    LastCompareMetrics.ParticleMetrics = Packet.ParticleMetrics;
    LastCompareMetrics.ParticleMetrics.RollbackSnapshotHitCount =
      SoftPressureRollbackSnapshotHitCount;
    LastCompareMetrics.ParticleMetrics.RollbackSnapshotMissCount =
      SoftPressureRollbackSnapshotMissCount;
    LastCompareMetrics.ParticleMetrics.RollbackAgentMismatchCount =
      SoftPressureRollbackAgentMismatchCount;
    LastCompareMetrics.ParticleMetrics.RollbackReplayedStepCount =
      SoftPressureRollbackReplayedStepCount;
    const bool bRollbackValidationRequested = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoRequireParticleCorrectionReplay"));
    const bool bRollbackValidationPass = SoftPressureRollbackSnapshotHitCount > 0
      && SoftPressureRollbackReplayedStepCount > 0
      && SoftPressureRollbackSnapshotMissCount == 0
      && SoftPressureRollbackAgentMismatchCount == 0;
    UE_LOG(LogTemp,
      Display,
      TEXT("CrowdDemoSoftPressureRollbackSummary role=client round_id=%d hit=%d miss=%d mismatch=%d replayed_steps=%d validation_required=%d pass=%d source=MassPipeline"),
      Packet.RoundId, SoftPressureRollbackSnapshotHitCount,
      SoftPressureRollbackSnapshotMissCount, SoftPressureRollbackAgentMismatchCount,
      SoftPressureRollbackReplayedStepCount, bRollbackValidationRequested ? 1 : 0,
      bRollbackValidationPass ? 1 : 0);
    if (bRollbackValidationRequested && !bRollbackValidationPass)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoSoftPressureRollbackSummary role=client round_id=%d validation_failed=1 VIOLATION"),
        Packet.RoundId);
    }
    const bool bCandidateHashMatch = ParticleCandidateStateHash
      == Packet.ParticleMetrics.ParticleCandidateHash;
    const bool bAppliedHashMatch = ParticleAppliedStateHash
      == Packet.ParticleMetrics.ParticleAppliedStateHash;
    const bool bParticleHashMatch = bCandidateHashMatch && bAppliedHashMatch;
    LastCompareMetrics.ServerClientParticleHashMatch = bParticleHashMatch ? 1 : 0;
    if (bParticleHashMatch)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoParticleStateHash role=client round_id=%d candidate_local=%u candidate_server=%u applied_local=%u applied_server=%u match=1 source=MassPipeline"),
        Packet.RoundId, ParticleCandidateStateHash,
        Packet.ParticleMetrics.ParticleCandidateHash, ParticleAppliedStateHash,
        Packet.ParticleMetrics.ParticleAppliedStateHash);
    }
    else
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoParticleStateHash role=client round_id=%d candidate_local=%u candidate_server=%u candidate_match=%d applied_local=%u applied_server=%u applied_match=%d match=0 source=MassPipeline VIOLATION"),
        Packet.RoundId, ParticleCandidateStateHash,
        Packet.ParticleMetrics.ParticleCandidateHash, bCandidateHashMatch ? 1 : 0,
        ParticleAppliedStateHash, Packet.ParticleMetrics.ParticleAppliedStateHash,
        bAppliedHashMatch ? 1 : 0);
    }
    const uint32 LocalFixtureHash = ParticleFailureFixture.FixtureHash;
    const uint32 ServerFixtureHash = Packet.ParticleMetrics.ParticleFailureFixtureHash;
    const bool bFixtureHashMatch = LocalFixtureHash == ServerFixtureHash;
    if (bFixtureHashMatch)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoParticleFailureFixtureHash role=client round_id=%d local=%u server=%u match=1 step=%d pair=%d,%d source=MassPipeline"),
        Packet.RoundId, LocalFixtureHash, ServerFixtureHash,
        Packet.ParticleMetrics.ParticleFailureFixtureStep,
        Packet.ParticleMetrics.ParticleFailureMinAgentId,
        Packet.ParticleMetrics.ParticleFailureMaxAgentId);
    }
    else
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoParticleFailureFixtureHash role=client round_id=%d local=%u server=%u match=0 step=%d pair=%d,%d source=MassPipeline VIOLATION"),
        Packet.RoundId, LocalFixtureHash, ServerFixtureHash,
        Packet.ParticleMetrics.ParticleFailureFixtureStep,
        Packet.ParticleMetrics.ParticleFailureMinAgentId,
        Packet.ParticleMetrics.ParticleFailureMaxAgentId);
    }
    if (Packet.ParticleMetrics.ParticleInvalidStepCount > 0)
      StopAfterParticleConstraintFailure();
  }
  if (IsTrafficScenario(GetRules().Scenario))
  {
    const FCrowdDemoTrafficMetrics Local = BuildTrafficMetrics(ClientStates);
    RecordSf3CompletedRoundHash(Local.AgentStateHash);
    const FCrowdDemoTrafficMetrics& Server = Packet.TrafficMetrics;
    const bool bSf4ReservationDiagnosticRequested = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoSf4ReservationOrcaDiagnostic"));
    const bool bTransitJointDiagnosticRequested = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoTransitJointDiagnostic"));
    const bool bTransitCapacityShadowRequested = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoTransitCapacityShadow"));
    const bool bElasticCrowdShadowRequested = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoElasticCrowdShadow"));
    const bool bHashMatch = Local.TrafficFieldHash == Server.TrafficFieldHash
      && Local.PortalDecisionHash == Server.PortalDecisionHash
      && Local.OrcaVelocityHash == Server.OrcaVelocityHash
      && (GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning
        || Local.PriorityOrcaHash == Server.PriorityOrcaHash)
      && Local.AgentStateHash == Server.AgentStateHash
      && (GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning
        || (Local.HoldingCandidateHash == Server.HoldingCandidateHash
          && Local.TransitCapacityPositionCount == Server.TransitCapacityPositionCount
          && Local.TransitCapacityHoldingCount == Server.TransitCapacityHoldingCount
          && Local.TransitCapacityPositionDeficit == Server.TransitCapacityPositionDeficit
          && Local.TransitCapacityHoldingDeficit == Server.TransitCapacityHoldingDeficit
          && Local.TransitCapacitySelectionHash == Server.TransitCapacitySelectionHash
          && Local.bTransitCapacitySelectionApplied == Server.bTransitCapacitySelectionApplied
          && Local.HoldingAssignmentHash == Server.HoldingAssignmentHash
          && Local.CommitDecisionHash == Server.CommitDecisionHash
          && Local.SteeringStateHash == Server.SteeringStateHash
          && Local.ResidualCapacityHash == Server.ResidualCapacityHash
          && Local.ResidualHoldingMatchingHash == Server.ResidualHoldingMatchingHash
          && Local.HoldingHallFixtureHash == Server.HoldingHallFixtureHash
          && Local.HallGeometryFixtureHash == Server.HallGeometryFixtureHash
          && Local.JointPositioningHash == Server.JointPositioningHash
          && Local.JointCommitResidualHash == Server.JointCommitResidualHash
          && Local.Sf4UnfinishedBoundaryAgentCount == Server.Sf4UnfinishedBoundaryAgentCount
          && Local.Sf4UnfinishedBoundaryHash == Server.Sf4UnfinishedBoundaryHash
          && Local.Sf4PhysicalUnsatisfiedAgentCount
            == Server.Sf4PhysicalUnsatisfiedAgentCount
          && Local.Sf4PhysicalUnsatisfiedTotalAgentCount
            == Server.Sf4PhysicalUnsatisfiedTotalAgentCount
          && Local.Sf4PhysicalUnsatisfiedSatisfiedCount
            == Server.Sf4PhysicalUnsatisfiedSatisfiedCount
          && Local.Sf4PhysicalUnsatisfiedCountClosed
            == Server.Sf4PhysicalUnsatisfiedCountClosed
          && Local.Sf4PhysicalUnsatisfiedHash == Server.Sf4PhysicalUnsatisfiedHash
          && (!bTransitCapacityShadowRequested
            || (Local.TransitCapacityShadowHash == Server.TransitCapacityShadowHash
              && Local.TransitCapacityFailureFixtureAgentCount
                == Server.TransitCapacityFailureFixtureAgentCount
              && Local.TransitCapacityFailureFixturePairCount
                == Server.TransitCapacityFailureFixturePairCount
              && Local.TransitCapacityFailureFixtureStatus
                == Server.TransitCapacityFailureFixtureStatus
              && Local.TransitCapacityFailureFixtureHash
                == Server.TransitCapacityFailureFixtureHash))
          && (!bElasticCrowdShadowRequested
            || (Local.ElasticShadowHash == Server.ElasticShadowHash
              && Local.ElasticParallelHash == Server.ElasticParallelHash
              && Local.ElasticFailureFixtureAgentCount
                == Server.ElasticFailureFixtureAgentCount
              && Local.ElasticFailureFixtureHash == Server.ElasticFailureFixtureHash
              && Local.ElasticFailureFixedStep == Server.ElasticFailureFixedStep
              && Local.ElasticFailureStage == Server.ElasticFailureStage
              && Local.ElasticFailureKind == Server.ElasticFailureKind
              && Local.ElasticFailureAttribution == Server.ElasticFailureAttribution))
          && (!bSf4ReservationDiagnosticRequested
            || (Local.Sf4ReservationOrcaFixtureValid == 1
              && Server.Sf4ReservationOrcaFixtureValid == 1
              && Local.Sf4ReservationOrcaFixtureHash
                == Server.Sf4ReservationOrcaFixtureHash))
          && (!bTransitJointDiagnosticRequested
            || (Local.TransitJointFixtureValid == 1
              && Server.TransitJointFixtureValid == 1
              && Local.TransitJointFixtureHash == Server.TransitJointFixtureHash))));
    if (bHashMatch)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf3Hash role=client round_id=%d traffic=%u portal=%u orca=%u priority_orca=%u state=%u holding_candidate=%u holding_assignment=%u commit=%u steering=%u hall=%u hall_geometry=%u joint=%u joint_residual=%u unfinished=%d/%u physical_unsatisfied=%d/%d satisfied=%d count_closed=%d physical_hash=%u match=1"),
        Packet.RoundId, Server.TrafficFieldHash, Server.PortalDecisionHash,
        Server.OrcaVelocityHash, Server.PriorityOrcaHash, Server.AgentStateHash,
        Server.HoldingCandidateHash, Server.HoldingAssignmentHash,
        Server.CommitDecisionHash, Server.SteeringStateHash,
        Server.HoldingHallFixtureHash, Server.HallGeometryFixtureHash,
        Server.JointPositioningHash,Server.JointCommitResidualHash,
        Server.Sf4UnfinishedBoundaryAgentCount,Server.Sf4UnfinishedBoundaryHash,
        Server.Sf4PhysicalUnsatisfiedAgentCount,
        Server.Sf4PhysicalUnsatisfiedTotalAgentCount,
        Server.Sf4PhysicalUnsatisfiedSatisfiedCount,
        Server.Sf4PhysicalUnsatisfiedCountClosed,
        Server.Sf4PhysicalUnsatisfiedHash);
      if (bTransitCapacityShadowRequested)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTransitCapacityShadow role=client round_id=%d components=%d max_component=%d solved=%d infeasible=%d numerical=%d quantized=%d yielding=%d hard_violation=%d obstacle_violation=%d flow_bounds_violation=%d target_violation=%d pair_double_owner=%d spacing_deficit_cm=%.3f aperture_deficit_cm=%.3f clearance_deficit_cm=%.3f max_yield_cm=%.3f solver_ms_p95=%.3f hash=%u match=1 source=MassPipeline"),
          Packet.RoundId, Server.TransitCapacityShadowComponentCount,
          Server.TransitCapacityShadowMaximumComponentSize,
          Server.TransitCapacityShadowSolvedCount,
          Server.TransitCapacityShadowInfeasibleCount,
          Server.TransitCapacityShadowNumericalFailureCount,
          Server.TransitCapacityShadowQuantizedFailureCount,
          Server.TransitCapacityShadowYieldingAgentCount,
          Server.TransitCapacityShadowHardPairViolationCount,
          Server.TransitCapacityShadowObstacleViolationCount,
          Server.TransitCapacityShadowFlowBoundsViolationCount,
          Server.TransitCapacityShadowTargetViolationCount,
          Server.TransitCapacityShadowPairDoubleOwnerCount,
          Server.TransitCapacityShadowPreferredSpacingDeficitCmMax,
          Server.TransitCapacityShadowApertureDeficitCmMax,
          Server.TransitCapacityShadowClearanceDeficitCmMax,
          Server.TransitCapacityShadowMaximumYieldDisplacementCm,
          Server.TransitCapacityShadowSolverMsP95,
          Server.TransitCapacityShadowHash);
      }
      if (bElasticCrowdShadowRequested)
      {
        const auto JoinUInt = [](const TArray<uint32>& Values)
        {
          FString Out;
          for (int32 Index = 0; Index < Values.Num(); ++Index)
            Out += FString::Printf(TEXT("%s%u"), Index == 0 ? TEXT("") : TEXT(","), Values[Index]);
          return Out;
        };
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoElasticCrowdShadow role=client round_id=%d spacing_pairs=%d influenced=%d propagation_layer=%d spacing_p95_cm=%.3f spacing_max_cm=%.3f transit_p95_cm=%.3f transit_max_cm=%.3f source_forward_q15=%d baseline_forward_q15=%d zero_progress_steps=%d recovery_p95_cm=%.3f hard_violation=%d obstacle_violation=%d flow_bounds_violation=%d target_violation=%d invalid=%d solver_ms_p95=%.3f hash=%u match=1 source=MassPipeline"),
          Packet.RoundId, Server.ElasticSpacingPairCount,
          Server.ElasticInfluencedAgentCount, Server.ElasticPropagationLayerMax,
          Server.ElasticSpacingDeficitCmP95, Server.ElasticSpacingDeficitCmMax,
          Server.ElasticTransitDeficitCmP95, Server.ElasticTransitDeficitCmMax,
          Server.ElasticSourceForwardRatioQ15,
          Server.ElasticBaselineSourceForwardRatioQ15,
          Server.ElasticZeroProgressStepMax, Server.ElasticRecoveryErrorCmP95,
          Server.ElasticHardPairViolationCount, Server.ElasticObstacleViolationCount,
          Server.ElasticFlowBoundsViolationCount, Server.ElasticTargetViolationCount,
          Server.ElasticInvalidInputCount, Server.ElasticSolverMsP95,
          Server.ElasticShadowHash);
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoElasticShadowScience role=client round_id=%d first_step=%d first_stage=%d first_kind=%d attribution=%d closure=%d too_large=%d rollout_steps=%d recovery_eligible=%d baseline_completed=%d elastic_completed=%d baseline_holes=%d elastic_holes=%d rollout_hash=%u match=1 source=MassPipeline"),
          Packet.RoundId, Server.ElasticFailureFixedStep,
          Server.ElasticFailureStage, Server.ElasticFailureKind,
          Server.ElasticFailureAttribution, Server.ElasticFailureClosureAgentCount,
          Server.ElasticFailureFixtureTooLarge, Server.ElasticParallelCompletedSteps,
          Server.ElasticParallelEligibleRecoveryCount,
          Server.ElasticParallelBaselineRecoveryCompletedCount,
          Server.ElasticParallelRecoveryCompletedCount,
          Server.ElasticParallelBaselinePermanentHoleCount,
          Server.ElasticParallelPermanentHoleCount, Server.ElasticParallelHash);
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoElasticParallelSafety role=client round_id=%d baseline_source_q15=%d elastic_source_q15=%d baseline_hard=%d elastic_hard=%d baseline_obstacle=%d elastic_obstacle=%d baseline_target=%d elastic_target=%d baseline_orca_stop_violation=%d elastic_orca_stop_violation=%d match=1 source=MassPipeline"),
          Packet.RoundId, Server.ElasticParallelBaselineSourceForwardQ15,
          Server.ElasticParallelSourceForwardQ15,
          Server.ElasticParallelBaselineHardPairViolationCount,
          Server.ElasticParallelHardPairViolationCount,
          Server.ElasticParallelBaselineObstaclePenetrationCount,
          Server.ElasticParallelObstaclePenetrationCount,
          Server.ElasticParallelBaselineTargetViolationCount,
          Server.ElasticParallelTargetViolationCount,
          Server.ElasticParallelBaselineOrcaStopViolationCount,
          Server.ElasticParallelOrcaStopViolationCount);
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoElasticShadowStages role=client round_id=%d order=preferred,orca,predict,obstacle,pbd1,pbd2,pbd3,reproject baseline_hash=%s elastic_hash=%s match=1 source=MassPipeline"),
          Packet.RoundId, *JoinUInt(Server.ElasticBaselineStageHashes),
          *JoinUInt(Server.ElasticTwinStageHashes));
      }
    }
    else
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoSf3Hash role=client round_id=%d traffic_local=%u traffic_server=%u portal_local=%u portal_server=%u orca_local=%u orca_server=%u priority_orca_local=%u priority_orca_server=%u state_local=%u state_server=%u holding_candidate_local=%u holding_candidate_server=%u holding_assignment_local=%u holding_assignment_server=%u commit_local=%u commit_server=%u steering_local=%u steering_server=%u match=0 VIOLATION"),
        Packet.RoundId, Local.TrafficFieldHash, Server.TrafficFieldHash,
        Local.PortalDecisionHash, Server.PortalDecisionHash,
        Local.OrcaVelocityHash, Server.OrcaVelocityHash,
        Local.PriorityOrcaHash, Server.PriorityOrcaHash,
        Local.AgentStateHash, Server.AgentStateHash,
        Local.HoldingCandidateHash, Server.HoldingCandidateHash,
        Local.HoldingAssignmentHash, Server.HoldingAssignmentHash,
        Local.CommitDecisionHash, Server.CommitDecisionHash,
        Local.SteeringStateHash, Server.SteeringStateHash);
    }
    LastCompareMetrics.TrafficMetrics = Server;
    LastCompareMetrics.TrafficMetrics.PhaseReservationClientHashMatch = bHashMatch ? 1 : 0;
    LastCompareMetrics.TrafficMetrics.PhaseReservationWaitGraphClientHashMatch = bHashMatch ? 1 : 0;
    LastCompareMetrics.TrafficMetrics.Sf4ReservationOrcaClientHashMatch =
      bSf4ReservationDiagnosticRequested && Local.Sf4ReservationOrcaFixtureValid == 1
        && Server.Sf4ReservationOrcaFixtureValid == 1
        && Local.Sf4ReservationOrcaFixtureHash == Server.Sf4ReservationOrcaFixtureHash ? 1 : 0;
    LastCompareMetrics.TrafficMetrics.TransitJointClientHashMatch =
      bTransitJointDiagnosticRequested && Local.TransitJointFixtureValid == 1
        && Server.TransitJointFixtureValid == 1
        && Local.TransitJointFixtureHash == Server.TransitJointFixtureHash ? 1 : 0;
    if (bTransitJointDiagnosticRequested)
    {
      const bool bTransitMatch = LastCompareMetrics.TrafficMetrics.TransitJointClientHashMatch == 1;
      if (bTransitMatch)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTransitJointDiagnostic role=client round_id=%d valid_local=%d valid_server=%d agents=%d pairs=%d constraints=%d fixture_hash_local=%u fixture_hash_server=%u match=1"),
          Packet.RoundId, Local.TransitJointFixtureValid, Server.TransitJointFixtureValid,
          Server.TransitJointFixtureAgentCount, Server.TransitJointFixturePairCount,
          Server.TransitJointFixtureConstraintCount,
          Local.TransitJointFixtureHash, Server.TransitJointFixtureHash);
      }
      else
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoTransitJointDiagnostic role=client round_id=%d valid_local=%d valid_server=%d agents=%d pairs=%d constraints=%d fixture_hash_local=%u fixture_hash_server=%u match=0"),
          Packet.RoundId, Local.TransitJointFixtureValid, Server.TransitJointFixtureValid,
          Server.TransitJointFixtureAgentCount, Server.TransitJointFixturePairCount,
          Server.TransitJointFixtureConstraintCount,
          Local.TransitJointFixtureHash, Server.TransitJointFixtureHash);
      }
    }
  }
  const FCrowdDemoCorrectionFrameMetrics PreviousCorrectionMetrics = LastCorrectionMetrics;
  const int32 PreviousAppliedCorrectionRevision = LastAppliedCorrectionRevision;
  FCrowdDemoCorrectionFrame Frame;
  Frame.AgentStates = Packet.Agents;
  Frame.CorrectionRevision = Packet.CheckpointRevision;
  RecordCorrectionComparisonAndApplied(ClientStates, Frame, Packet.EndServerTimeSeconds);
  LastCompareMetrics.RoundId = Packet.RoundId;
  LastCompareMetrics.Revision = Packet.Revision;
  LastCompareMetrics.CheckpointRevision = Packet.CheckpointRevision;
  LastCompareMetrics.SimPositionErrorCmP50 = LastCorrectionMetrics.CorrectionPositionErrorCmP50;
  LastCompareMetrics.SimPositionErrorCmP95 = LastCorrectionMetrics.CorrectionPositionErrorCmP95;
  LastCompareMetrics.SimPositionErrorCmMax = LastCorrectionMetrics.CorrectionPositionErrorCmMax;
  CrossRoundPositionErrorSeries.Record(LastCompareMetrics.SimPositionErrorCmP95);
  LastCompareMetrics.CrossRoundPositionErrorCmP95Max = CrossRoundPositionErrorSeries.GetMax();
  LastCompareMetrics.CrossRoundPositionErrorGrowthCm = CrossRoundPositionErrorSeries.GetExpansionFromFirst();
  CrossRoundCorrectionIntervalErrorSeries.Record(
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95);
  LastCompareMetrics.CrossRoundCorrectionIntervalErrorCmP95Max =
    CrossRoundCorrectionIntervalErrorSeries.GetMax();
  LastCompareMetrics.CrossRoundCorrectionIntervalErrorGrowthCm =
    CrossRoundCorrectionIntervalErrorSeries.GetExpansionFromFirst();
  LastCompareMetrics.SimYawErrorDegP95 = LastCorrectionMetrics.CorrectionYawErrorDegP95;
  LastCompareMetrics.SimVelocityErrorCmpsP95 = LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95;
  LastCompareMetrics.SimOverlapPairDelta = CountOverlapPairs(ClientStates, OverlapRadiusCm) - Packet.OverlapPairCount;
  LastCorrectionMetrics = PreviousCorrectionMetrics;
  LastAppliedCorrectionRevision = PreviousAppliedCorrectionRevision;
  ++LastCompareMetrics.CompletedRoundCount;
  ++LastCompareMetrics.CorrectionAppliedCount;
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundCheckpoint role=client round_id=%d revision=%d checkpoint_revision=%d completed_round_count=%d correction_applied_count=%d sim_position_error_cm_p50=%.3f sim_position_error_cm_p95=%.3f sim_position_error_cm_max=%.3f correction_interval_position_error_cm_p95=%.3f correction_interval_position_error_cm_max=%.3f cross_round_position_error_cm_p95_max=%.3f cross_round_position_error_growth_cm=%.3f cross_round_correction_interval_error_cm_p95_max=%.3f cross_round_correction_interval_error_growth_cm=%.3f sim_yaw_error_deg_p95=%.3f sim_velocity_error_cmps_p95=%.3f sim_overlap_pair_delta=%d source=MassPipeline"),
    Packet.RoundId,
    Packet.Revision,
    Packet.CheckpointRevision,
    LastCompareMetrics.CompletedRoundCount,
    LastCompareMetrics.CorrectionAppliedCount,
    LastCompareMetrics.SimPositionErrorCmP50,
    LastCompareMetrics.SimPositionErrorCmP95,
    LastCompareMetrics.SimPositionErrorCmMax,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmMax,
    LastCompareMetrics.CrossRoundPositionErrorCmP95Max,
    LastCompareMetrics.CrossRoundPositionErrorGrowthCm,
    LastCompareMetrics.CrossRoundCorrectionIntervalErrorCmP95Max,
    LastCompareMetrics.CrossRoundCorrectionIntervalErrorGrowthCm,
    LastCompareMetrics.SimYawErrorDegP95,
    LastCompareMetrics.SimVelocityErrorCmpsP95,
    LastCompareMetrics.SimOverlapPairDelta);
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoFlowCheckpoint role=client round_id=%d agents=%d flow_field_revision=%d flow_field_build_hash=%u flow_field_rebuild_count=%d flow_unreachable_agent_count=%d flow_goal_reached_count=%d flow_wall_pass_count=%d flow_corridor_exit_count=%d flow_turn_exit_count=%d corridor_deadlock_agent_count=%d client_sim_obstacle_penetration_count=%d sim_position_error_cm_p95=%.3f correction_interval_position_error_cm_p95=%.3f cross_round_position_error_growth_cm=%.3f source=MassPipeline"),
    Packet.RoundId,
    Packet.Agents.Num(),
    LastCompareMetrics.FlowFieldRevision,
    LastCompareMetrics.FlowFieldBuildHash,
    LastCompareMetrics.FlowFieldRebuildCount,
    LastCompareMetrics.FlowUnreachableAgentCount,
    LastCompareMetrics.FlowGoalReachedCount,
    LastCompareMetrics.FlowWallPassCount,
    LastCompareMetrics.FlowCorridorExitCount,
    LastCompareMetrics.FlowTurnExitCount,
    LastCompareMetrics.CorridorDeadlockAgentCount,
    LastCompareMetrics.ClientSimObstaclePenetrationCount,
    LastCompareMetrics.SimPositionErrorCmP95,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95,
    LastCompareMetrics.CrossRoundPositionErrorGrowthCm);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordNavigationDomainReprojectDelta(const float DeltaCm)
{
  LastCompareMetrics.TrafficMetrics.NavigationDomainReprojectDeltaCmMax = FMath::Max(
    LastCompareMetrics.TrafficMetrics.NavigationDomainReprojectDeltaCmMax, DeltaCm);
}

bool UCrowdDemoRoundSimPipelineSubsystem::ShouldBuildCorrectionFrame() const
{
  return bPlanActive && SimulatedServerTimeSeconds - LastCorrectionBuildServerTimeSeconds >= CorrectionIntervalSeconds;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ShouldBuildRoundResult() const
{
  const float BoundaryTime = bStepInProgress ? CurrentStepEndServerTimeSeconds : SimulatedServerTimeSeconds;
  return bPlanActive
    && LastBuiltResultRoundId < ActivePlan.RoundId
    && (bParticleConstraintRunFailure
      || BoundaryTime + KINDA_SMALL_NUMBER >= ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds);
}

void UCrowdDemoRoundSimPipelineSubsystem::EnqueueOutgoingCorrectionFrame(FCrowdDemoCorrectionFrame&& Frame)
{
  OutgoingCorrectionFrames.Add(MoveTemp(Frame));
}

void UCrowdDemoRoundSimPipelineSubsystem::EnqueueOutgoingRoundResult(FCrowdDemoRoundResultPacket&& Packet)
{
  OutgoingRoundResults.Add(MoveTemp(Packet));
}

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueOutgoingCorrectionFrame(FCrowdDemoCorrectionFrame& OutFrame)
{
  if (OutgoingCorrectionFrames.IsEmpty())
  {
    return false;
  }
  OutFrame = MoveTemp(OutgoingCorrectionFrames[0]);
  OutgoingCorrectionFrames.RemoveAt(0, 1, EAllowShrinking::No);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueOutgoingRoundResult(FCrowdDemoRoundResultPacket& OutPacket)
{
  if (OutgoingRoundResults.IsEmpty())
  {
    return false;
  }
  OutPacket = MoveTemp(OutgoingRoundResults[0]);
  OutgoingRoundResults.RemoveAt(0, 1, EAllowShrinking::No);
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkCorrectionFrameBuilt(const float ServerTimeSeconds)
{
  LastCorrectionBuildServerTimeSeconds = ServerTimeSeconds;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkRoundResultBuilt(const int32 CheckpointRevision)
{
  LastBuiltResultRoundId = ActivePlan.RoundId;
  LastCheckpointRevision = CheckpointRevision;
  ++LastCompareMetrics.CompletedRoundCount;
  LastCompareMetrics.CheckpointRevision = CheckpointRevision;
  LastCompletedRoundMetrics = LastCompareMetrics;
  LastCompletedHoldingHallFixture = HoldingHallFixture;
  LastCompletedHallGeometryFixture = HallGeometryFixture;
  LastCompletedJointPositioningResult = JointPositioningResult;
  LastCompletedJointCommitResidualResult = JointCommitResidualResult;
}

void UCrowdDemoRoundSimPipelineSubsystem::MergeNetworkCorrectionMetrics(
  const FCrowdDemoCorrectionFrameMetrics& NetworkMetrics)
{
  LastCorrectionMetrics.CorrectionFrameHeaderReceivedCount = NetworkMetrics.CorrectionFrameHeaderReceivedCount;
  LastCorrectionMetrics.CorrectionFrameChunkReceivedCount = NetworkMetrics.CorrectionFrameChunkReceivedCount;
  LastCorrectionMetrics.CorrectionFrameCompleteCount = NetworkMetrics.CorrectionFrameCompleteCount;
  LastCorrectionMetrics.CorrectionFramePublishedCount = NetworkMetrics.CorrectionFramePublishedCount;
  LastCorrectionMetrics.CorrectionFrameReceivedCount = NetworkMetrics.CorrectionFrameReceivedCount;
  LastCorrectionMetrics.CorrectionFrameRevisionGapCount = NetworkMetrics.CorrectionFrameRevisionGapCount;
  LastCorrectionMetrics.CorrectionFrameChunksPerFrame = NetworkMetrics.CorrectionFrameChunksPerFrame;
  LastCorrectionMetrics.CorrectionFrameChunkSize = NetworkMetrics.CorrectionFrameChunkSize;
  LastCorrectionMetrics.CorrectionIntervalMsP95 = NetworkMetrics.CorrectionIntervalMsP95;
  LastCorrectionMetrics.CorrectionFrameAssemblyMsP95 = NetworkMetrics.CorrectionFrameAssemblyMsP95;
}

void UCrowdDemoRoundSimPipelineSubsystem::LogStageOnce(const TCHAR* StageName, const int32 AgentCount)
{
  const FName StageKey(StageName);
  if (LoggedStages.Contains(StageKey))
  {
    return;
  }
  LoggedStages.Add(StageKey);
  const UWorld* World = GetWorld();
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundPipeline role=%s stage=%s order_revision=%d fixed_step=%.4f agents=%d source=MassProcessor"),
    World && World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
    StageName,
    GetCurrentPlanRevision(),
    CurrentFixedStepSeconds,
    AgentCount);
}

int32 UCrowdDemoRoundSimPipelineSubsystem::CountOverlapPairs(
  TConstArrayView<FCrowdDemoRoundAgentState> States,
  const float RadiusCm)
{
  int32 Count = 0;
  const float RadiusSquared = FMath::Square(RadiusCm);
  for (int32 A = 0; A < States.Num(); ++A)
  {
    for (int32 B = A + 1; B < States.Num(); ++B)
    {
      if (FVector::DistSquared2D(FVector(States[A].Location), FVector(States[B].Location)) < RadiusSquared)
      {
        ++Count;
      }
    }
  }
  return Count;
}

float UCrowdDemoRoundSimPipelineSubsystem::Percentile(TArray<float> Values, const float Quantile)
{
  if (Values.IsEmpty())
  {
    return -1.0f;
  }
  Values.Sort();
  const int32 Index = FMath::Clamp(FMath::CeilToInt(Values.Num() * Quantile) - 1, 0, Values.Num() - 1);
  return Values[Index];
}
