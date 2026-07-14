#include "CrowdDemoReplicator.h"

#include "Arena/CrowdDemoTargetActor.h"
#include "CrowdDemoRoundSimCoordinator.h"
#include "Mass/CrowdDemoMassReplication.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
  constexpr float CrowdDemoSevereOverlapCm = 42.0f;
  constexpr float CrowdDemoOverlapCm = 78.0f;
  constexpr float CrowdDemoMetricIntervalSeconds = 1.0f;
}

ACrowdDemoReplicator::ACrowdDemoReplicator()
{
  PrimaryActorTick.bCanEverTick = true;
  bReplicates = true;
  bAlwaysRelevant = true;
  SetNetUpdateFrequency(20.0f);
  SetMinNetUpdateFrequency(10.0f);

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
  SetRootComponent(SceneRoot);

  CrowdInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CrowdInstances"));
  CrowdInstances->SetupAttachment(SceneRoot);
  CrowdInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  CrowdInstances->SetMobility(EComponentMobility::Movable);
  CrowdInstances->NumCustomDataFloats = 3;

  PreviewFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewFloor"));
  PreviewFloor->SetupAttachment(SceneRoot);
  PreviewFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  PreviewFloor->SetMobility(EComponentMobility::Movable);
  PreviewFloor->SetRelativeLocation(FVector(0.0f, -900.0f, -10.0f));
  PreviewFloor->SetRelativeScale3D(FVector(100.0f, 100.0f, 0.05f));

  static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
  if (CubeMesh.Succeeded())
  {
    CrowdInstances->SetStaticMesh(CubeMesh.Object);
    PreviewFloor->SetStaticMesh(CubeMesh.Object);
  }

  static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  if (BasicShapeMaterial.Succeeded())
  {
    CrowdInstances->SetMaterial(0, BasicShapeMaterial.Object);
    PreviewFloor->SetMaterial(0, BasicShapeMaterial.Object);
    bVisualMaterialLoaded = true;
  }
}

void ACrowdDemoReplicator::BeginPlay()
{
  Super::BeginPlay();

  if (CrowdInstances && CrowdInstances->GetMaterial(0))
  {
    UMaterialInstanceDynamic* CohortAMaterial = UMaterialInstanceDynamic::Create(CrowdInstances->GetMaterial(0), this);
    CohortAMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.08f, 0.42f, 1.0f, 1.0f));
    CrowdInstances->SetMaterial(0, CohortAMaterial);
  }
  EntityCount = ResolveEntityCount();
  DurationSeconds = ResolveDurationSeconds();
  StartedSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
  LastMetricSeconds = StartedSeconds;

  if (bLocalVisualHostOnly)
  {
    UE_LOG(LogTemp, Display, TEXT("CrowdDemo: START role=client_visual_host duration=%.2f visual_material=%s source=MassClientBubble"), DurationSeconds, bVisualMaterialLoaded ? TEXT("loaded") : TEXT("missing"));
  }
  else if (HasAuthority())
  {
    RefreshServerSummaryState();
    UE_LOG(LogTemp, Display, TEXT("CrowdDemo: START role=server entity_count=%d duration=%.2f visual_material=%s source=MassClientBubble"), EntityStates.Num(), DurationSeconds, bVisualMaterialLoaded ? TEXT("loaded") : TEXT("missing"));
  }
  else
  {
    UE_LOG(LogTemp, Display, TEXT("CrowdDemo: START role=client duration=%.2f visual_material=%s source=MassClientBubble"), DurationSeconds, bVisualMaterialLoaded ? TEXT("loaded") : TEXT("missing"));
  }
}

void ACrowdDemoReplicator::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);

  if (HasAuthority() && !bLocalVisualHostOnly)
  {
    ServerFrameMsSamples.Add(DeltaSeconds * 1000.0f);
    RefreshServerSummaryState();
  }

  LogSummaryIfReady();
}

void ACrowdDemoReplicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

}

UInstancedStaticMeshComponent* ACrowdDemoReplicator::GetCrowdInstancesForClientVisuals() const
{
  return CrowdInstances;
}

void ACrowdDemoReplicator::ClearCrowdVisualInstances()
{
  if (CrowdInstances)
  {
    CrowdInstances->ClearInstances();
    CrowdInstances->MarkRenderStateDirty();
  }
  EntityStates.Reset();
}

int32 ACrowdDemoReplicator::GetCrowdVisualInstanceCount() const
{
  return CrowdInstances ? CrowdInstances->GetInstanceCount() : 0;
}

void ACrowdDemoReplicator::RecordClientVisualSample(const float ReplicationSampleAgeMs, const float DisplayToAuthoritativeCm)
{
  if (ReplicationSampleAgeMs >= 0.0f)
  {
    ReplicationSampleAgeMsSamples.Add(ReplicationSampleAgeMs);
  }
  if (DisplayToAuthoritativeCm >= 0.0f)
  {
    DisplayToAuthoritativeCmSamples.Add(DisplayToAuthoritativeCm);
  }
}

void ACrowdDemoReplicator::RecordRoundSimVisualSmoothing(
  const float CorrectionOffsetCm,
  const float YawOffsetDegrees,
  const bool bSmoothingActive)
{
  if (CorrectionOffsetCm >= 0.0f)
  {
    RoundVisualCorrectionOffsetCmSamples.Add(CorrectionOffsetCm);
  }
  if (YawOffsetDegrees >= 0.0f)
  {
    RoundVisualYawOffsetDegSamples.Add(YawOffsetDegrees);
  }
  if (bSmoothingActive)
  {
    ++RoundVisualSmoothingActiveCount;
  }
}

void ACrowdDemoReplicator::ResetClientMassEntityStates()
{
  if (!HasAuthority() || bLocalVisualHostOnly)
  {
    EntityStates.Reset();
  }
}

void ACrowdDemoReplicator::UpsertClientMassEntityState(const FCrowdDemoEntityState& State)
{
  FCrowdDemoEntityState& ExistingState = FindOrAddEntityState(State.Id, State.LifecycleSerial);
  ExistingState = State;
}

void ACrowdDemoReplicator::SetLocalVisualHostOnly(const bool bInLocalVisualHostOnly)
{
  bLocalVisualHostOnly = bInLocalVisualHostOnly;
}

void ACrowdDemoReplicator::RefreshServerSummaryState()
{
  const double SolverStartSeconds = FPlatformTime::Seconds();
  const UWorld* World = GetWorld();
  const float NowSeconds = World ? World->GetTimeSeconds() : 0.0f;
  UCrowdDemoMassSubsystem* MassSubsystem = World ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;

  if (MassSubsystem)
  {
    MassSubsystem->BuildVisualSnapshot(EntityStates, NowSeconds);
  }

  const double SolverEndSeconds = FPlatformTime::Seconds();
  SolverMsSamples.Add(static_cast<float>((SolverEndSeconds - SolverStartSeconds) * 1000.0));

  if (NowSeconds - LastMetricSeconds >= CrowdDemoMetricIntervalSeconds)
  {
    LastMetricSeconds = NowSeconds;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemo: TICK role=server t=%.2f agents=%d source=RoundSim"),
      NowSeconds - static_cast<float>(StartedSeconds),
      MassSubsystem ? MassSubsystem->GetTrackedAgentCount() : 0);
  }
}

FCrowdDemoEntityState& ACrowdDemoReplicator::FindOrAddEntityState(const int32 Id, const int32 LifecycleSerial)
{
  for (FCrowdDemoEntityState& State : EntityStates)
  {
    if (State.Id == Id && State.LifecycleSerial == LifecycleSerial)
    {
      return State;
    }
  }

  FCrowdDemoEntityState& State = EntityStates.AddDefaulted_GetRef();
  State.Id = Id;
  State.LifecycleSerial = LifecycleSerial;
  return State;
}

void ACrowdDemoReplicator::LogSummaryIfReady()
{
  const UWorld* World = GetWorld();
  if (!World || World->GetTimeSeconds() - StartedSeconds < DurationSeconds)
  {
    return;
  }

  const bool bServer = HasAuthority() && !bLocalVisualHostOnly;
  if ((bServer && bServerSummaryLogged) || (!bServer && bClientSummaryLogged))
  {
    return;
  }
  if (!bServer && EntityStates.IsEmpty() && GetCrowdVisualInstanceCount() == 0)
  {
    return;
  }
  bServerSummaryLogged = bServer;
  bClientSummaryLogged = !bServer;

  const FCrowdDemoSummaryMetrics Metrics = BuildSummaryMetrics();
  const TCHAR* SummaryRole = bServer ? TEXT("server") : TEXT("client");
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoSummary role=%s agents=%d visible_instances=%d server_frame_ms_p95=%.3f crowd_solver_ms_p95=%.3f replication_sample_age_ms_p95=%.3f display_to_sim_cm_p95=%.3f current_round_id=%d completed_round_count=%d correction_frame_applied_count=%d sim_position_error_cm_p95=%.3f source=MassClientBubble"),
    SummaryRole,
    Metrics.Agents,
    Metrics.VisibleInstances,
    Metrics.ServerFrameMsP95,
    Metrics.CrowdSolverMsP95,
    Metrics.ReplicationSampleAgeMsP95,
    Metrics.DisplayToAuthoritativeCmP95,
    Metrics.SimCurrentRoundId,
    Metrics.SimCompletedRoundCount,
    Metrics.CorrectionFrameAppliedCount,
    Metrics.SimPositionErrorCmP95);

  if (Metrics.FlowFieldRevision > 0)
  {
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoFlowSummary role=%s agents=%d visible_instances=%d flow_field_revision=%d flow_field_build_hash=%u flow_field_rebuild_count=%d flow_blocked_cell_count=%d flow_unreachable_agent_count=%d flow_goal_reached_count=%d flow_wall_pass_count=%d flow_corridor_exit_count=%d flow_turn_exit_count=%d server_obstacle_penetration_count=%d client_sim_obstacle_penetration_count=%d sim_position_error_cm_p95=%.3f correction_frame_applied_count=%d source=SharedFlowField"),
      SummaryRole,
      Metrics.Agents,
      Metrics.VisibleInstances,
      Metrics.FlowFieldRevision,
      Metrics.FlowFieldBuildHash,
      Metrics.FlowFieldRebuildCount,
      Metrics.FlowBlockedCellCount,
      Metrics.FlowUnreachableAgentCount,
      Metrics.FlowGoalReachedCount,
      Metrics.FlowWallPassCount,
      Metrics.FlowCorridorExitCount,
      Metrics.FlowTurnExitCount,
      Metrics.ServerObstaclePenetrationCount,
      Metrics.ClientSimObstaclePenetrationCount,
      Metrics.SimPositionErrorCmP95,
      Metrics.CorrectionFrameAppliedCount);
  }
  if (Metrics.PbdSolverMsP95 >= 0.0f)
  {
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoSf2Summary role=%s agents=%d visible_instances=%d initial_overlap_pair_count=%d overlap_pair_count_p50=%.3f overlap_pair_count_p95=%.3f overlap_pair_count_max=%d severe_overlap_pair_count_p50=%.3f severe_overlap_pair_count_p95=%.3f severe_overlap_pair_count_max=%d soft_separation_applied_agent_count=%d pbd_corrected_agent_count=%d pbd_corrected_pair_count=%d pbd_max_pair_correction_cm=%.3f pbd_max_agent_total_correction_cm=%.3f pbd_max_obstacle_reproject_delta_cm=%.3f pbd_max_final_safety_delta_cm=%.3f pbd_solver_ms_p95=%.3f flow_goal_reached_count=%d flow_corridor_exit_count=%d corridor_deadlock_agent_count=%d server_obstacle_penetration_count=%d client_sim_obstacle_penetration_count=%d sim_position_error_cm_p95=%.3f correction_frame_applied_count=%d source=MassPipeline"),
      SummaryRole,
      Metrics.Agents,
      Metrics.VisibleInstances,
      Metrics.InitialOverlapPairCount,
      Metrics.OverlapPairCountP50,
      Metrics.OverlapPairCountP95,
      Metrics.OverlapPairCountMax,
      Metrics.SevereOverlapPairCountP50,
      Metrics.SevereOverlapPairCountP95,
      Metrics.SevereOverlapPairCountMax,
      Metrics.SoftSeparationAppliedAgentCount,
      Metrics.PbdCorrectedAgentCount,
      Metrics.PbdCorrectedPairCount,
      Metrics.PbdMaxPairCorrectionCm,
      Metrics.PbdMaxAgentTotalCorrectionCm,
      Metrics.PbdMaxObstacleReprojectDeltaCm,
      Metrics.PbdMaxFinalSafetyDeltaCm,
      Metrics.PbdSolverMsP95,
      Metrics.FlowGoalReachedCount,
      Metrics.FlowCorridorExitCount,
      Metrics.CorridorDeadlockAgentCount,
      Metrics.ServerObstaclePenetrationCount,
      Metrics.ClientSimObstaclePenetrationCount,
      Metrics.SimPositionErrorCmP95,
      Metrics.CorrectionFrameAppliedCount);
  }
}

FCrowdDemoSummaryMetrics ACrowdDemoReplicator::BuildSummaryMetrics() const
{
  FCrowdDemoSummaryMetrics Metrics;
  Metrics.Agents = EntityStates.Num();
  Metrics.VisibleInstances = GetNetMode() != NM_DedicatedServer ? GetCrowdVisualInstanceCount() : 0;
  Metrics.ServerFrameMsP95 = ComputeP95(ServerFrameMsSamples);
  Metrics.CrowdSolverMsP95 = ComputeP95(SolverMsSamples);
  Metrics.ReplicationSampleAgeMsP95 = ComputeP95(ReplicationSampleAgeMsSamples);
  Metrics.DisplayToAuthoritativeCmP95 = ComputeP95(DisplayToAuthoritativeCmSamples);
  Metrics.RoundVisualCorrectionOffsetCmP95 = ComputeP95(RoundVisualCorrectionOffsetCmSamples);
  Metrics.RoundVisualYawOffsetDegP95 = ComputeP95(RoundVisualYawOffsetDegSamples);
  Metrics.RoundVisualSmoothingActiveCount = RoundVisualSmoothingActiveCount;
  if (const UWorld* WorldForRoundSim = GetWorld())
  {
    for (TActorIterator<ACrowdDemoRoundSimCoordinator> It(WorldForRoundSim); It; ++It)
    {
      const FCrowdDemoRoundCompareMetrics& CompareMetrics = It->GetLastCompareMetrics();
      Metrics.SimCurrentRoundId = CompareMetrics.CurrentRoundId;
      Metrics.SimCompletedRoundCount = CompareMetrics.CompletedRoundCount;
      Metrics.SimCorrectionAppliedCount = CompareMetrics.CorrectionAppliedCount;
      Metrics.SimCheckpointRevision = CompareMetrics.CheckpointRevision;
      Metrics.SimPositionErrorCmP50 = CompareMetrics.SimPositionErrorCmP50;
      Metrics.SimPositionErrorCmP95 = CompareMetrics.SimPositionErrorCmP95;
      Metrics.SimPositionErrorCmMax = CompareMetrics.SimPositionErrorCmMax;
      Metrics.CorrectionIntervalPositionErrorCmP95 = CompareMetrics.CorrectionIntervalPositionErrorCmP95;
      Metrics.CorrectionIntervalPositionErrorCmMax = CompareMetrics.CorrectionIntervalPositionErrorCmMax;
      Metrics.CrossRoundPositionErrorCmP95Max = CompareMetrics.CrossRoundPositionErrorCmP95Max;
      Metrics.CrossRoundPositionErrorGrowthCm = CompareMetrics.CrossRoundPositionErrorGrowthCm;
      Metrics.CrossRoundCorrectionIntervalErrorCmP95Max = CompareMetrics.CrossRoundCorrectionIntervalErrorCmP95Max;
      Metrics.CrossRoundCorrectionIntervalErrorGrowthCm = CompareMetrics.CrossRoundCorrectionIntervalErrorGrowthCm;
      Metrics.SimYawErrorDegP95 = CompareMetrics.SimYawErrorDegP95;
      Metrics.SimVelocityErrorCmpsP95 = CompareMetrics.SimVelocityErrorCmpsP95;
      Metrics.SimOverlapPairDelta = CompareMetrics.SimOverlapPairDelta;
      Metrics.SimCorrectionEntitiesCount = CompareMetrics.CorrectionEntitiesCount;
      Metrics.SimCorrectionMaxCm = CompareMetrics.CorrectionMaxCm;
      Metrics.RoundBoundaryCenterJumpCmP95 = CompareMetrics.RoundBoundaryCenterJumpCmP95;
      Metrics.RoundBoundaryYawJumpDegP95 = CompareMetrics.RoundBoundaryYawJumpDegP95;
      Metrics.RoundBoundaryVelocityJumpCmpsP95 = CompareMetrics.RoundBoundaryVelocityJumpCmpsP95;
      Metrics.RoundPlanRevisionSeen = CompareMetrics.RoundPlanRevisionSeen;
      Metrics.RoundPlanAppliedCount = CompareMetrics.RoundPlanAppliedCount;
      Metrics.RoundPlanGapCount = CompareMetrics.RoundPlanGapCount;
      Metrics.RoundPlanLateCount = CompareMetrics.RoundPlanLateCount;
      Metrics.RoundBootstrapAgentCount = CompareMetrics.RoundBootstrapAgentCount;
      Metrics.SyntheticSkippedCheckpointCount = CompareMetrics.SyntheticSkippedCheckpointCount;
      Metrics.InitialOverlapPairCount = CompareMetrics.InitialOverlapPairCount;
      Metrics.OverlapPairCountP50 = CompareMetrics.OverlapPairCountP50;
      Metrics.OverlapPairCountP95 = CompareMetrics.OverlapPairCountP95;
      Metrics.OverlapPairCountMax = CompareMetrics.OverlapPairCountMax;
      Metrics.SevereOverlapPairCountP50 = CompareMetrics.SevereOverlapPairCountP50;
      Metrics.SevereOverlapPairCountP95 = CompareMetrics.SevereOverlapPairCountP95;
      Metrics.SevereOverlapPairCountMax = CompareMetrics.SevereOverlapPairCountMax;
      Metrics.FlowFieldRevision = CompareMetrics.FlowFieldRevision;
      Metrics.FlowFieldBuildHash = CompareMetrics.FlowFieldBuildHash;
      Metrics.FlowFieldRebuildCount = CompareMetrics.FlowFieldRebuildCount;
      Metrics.FlowBlockedCellCount = CompareMetrics.FlowBlockedCellCount;
      Metrics.FlowUnreachableAgentCount = CompareMetrics.FlowUnreachableAgentCount;
      Metrics.FlowGoalReachedCount = CompareMetrics.FlowGoalReachedCount;
      Metrics.FlowWallPassCount = CompareMetrics.FlowWallPassCount;
      Metrics.FlowCorridorExitCount = CompareMetrics.FlowCorridorExitCount;
      Metrics.FlowTurnExitCount = CompareMetrics.FlowTurnExitCount;
      Metrics.ServerObstaclePenetrationCount = CompareMetrics.ServerObstaclePenetrationCount;
      Metrics.ClientSimObstaclePenetrationCount = CompareMetrics.ClientSimObstaclePenetrationCount;
      Metrics.SoftSeparationAppliedAgentCount = CompareMetrics.SoftSeparationAppliedAgentCount;
      Metrics.PbdCorrectedAgentCount = CompareMetrics.PbdCorrectedAgentCount;
      Metrics.PbdCorrectedPairCount = CompareMetrics.PbdCorrectedPairCount;
      Metrics.PbdMaxPairCorrectionCm = CompareMetrics.PbdMaxPairCorrectionCm;
      Metrics.PbdMaxAgentTotalCorrectionCm = CompareMetrics.PbdMaxAgentTotalCorrectionCm;
      Metrics.PbdMaxObstacleReprojectDeltaCm = CompareMetrics.PbdMaxObstacleReprojectDeltaCm;
      Metrics.PbdMaxFinalSafetyDeltaCm = CompareMetrics.PbdMaxFinalSafetyDeltaCm;
      Metrics.PbdSolverMsP95 = CompareMetrics.PbdSolverMsP95;
      Metrics.TrafficMetrics = CompareMetrics.TrafficMetrics;
      Metrics.CorridorDeadlockAgentCount = CompareMetrics.CorridorDeadlockAgentCount;
      const FCrowdDemoCorrectionFrameMetrics& CorrectionMetrics = It->GetLastCorrectionFrameMetrics();
      Metrics.CorrectionFrameRevision = CorrectionMetrics.CorrectionFrameRevision;
      Metrics.CorrectionFrameAppliedCount = CorrectionMetrics.CorrectionFrameAppliedCount;
      Metrics.CorrectionFrameHeaderReceivedCount = CorrectionMetrics.CorrectionFrameHeaderReceivedCount;
      Metrics.CorrectionFrameChunkReceivedCount = CorrectionMetrics.CorrectionFrameChunkReceivedCount;
      Metrics.LatestChunkRevisionSeen = CorrectionMetrics.LatestChunkRevisionSeen;
      Metrics.CorrectionChunkReceivedCount = CorrectionMetrics.CorrectionChunkReceivedCount;
      Metrics.CorrectionUniqueChunkCount = CorrectionMetrics.CorrectionUniqueChunkCount;
      Metrics.CorrectionExpectedChunkCount = CorrectionMetrics.CorrectionExpectedChunkCount;
      Metrics.CorrectionAssemblyCompleteCount = CorrectionMetrics.CorrectionAssemblyCompleteCount;
      Metrics.CorrectionAssemblySupersededCount = CorrectionMetrics.CorrectionAssemblySupersededCount;
      Metrics.CorrectionFrameCompleteCount = CorrectionMetrics.CorrectionFrameCompleteCount;
      Metrics.CorrectionFramePublishedCount = CorrectionMetrics.CorrectionFramePublishedCount;
      Metrics.CorrectionFrameReceivedCount = CorrectionMetrics.CorrectionFrameReceivedCount;
      Metrics.CorrectionFrameDroppedOldCount = CorrectionMetrics.CorrectionFrameDroppedOldCount;
      Metrics.CorrectionFrameDroppedMismatchCount = CorrectionMetrics.CorrectionFrameDroppedMismatchCount;
      Metrics.CorrectionFrameFuturePendingCount = CorrectionMetrics.CorrectionFrameFuturePendingCount;
      Metrics.CorrectionFrameFutureDropCount = CorrectionMetrics.CorrectionFrameFutureDropCount;
      Metrics.CorrectionFrameIncompleteDropCount = CorrectionMetrics.CorrectionFrameIncompleteDropCount;
      Metrics.CorrectionFrameStaleDropCount = CorrectionMetrics.CorrectionFrameStaleDropCount;
      Metrics.CorrectionFrameReplayToNowCount = CorrectionMetrics.CorrectionFrameReplayToNowCount;
      Metrics.CorrectionFrameLatestRevisionSeen = CorrectionMetrics.CorrectionFrameLatestRevisionSeen;
      Metrics.CorrectionFrameLatestRevisionApplied = CorrectionMetrics.CorrectionFrameLatestRevisionApplied;
      Metrics.CorrectionFrameRevisionGapCount = CorrectionMetrics.CorrectionFrameRevisionGapCount;
      Metrics.CorrectionFrameChunksPerFrame = CorrectionMetrics.CorrectionFrameChunksPerFrame;
      Metrics.CorrectionFrameChunkSize = CorrectionMetrics.CorrectionFrameChunkSize;
      Metrics.CorrectionAgentCount = CorrectionMetrics.CorrectionAgentCount;
      Metrics.CorrectionFrameAgeMsP50 = CorrectionMetrics.CorrectionFrameAgeMsP50;
      Metrics.CorrectionFrameAgeMsP95 = CorrectionMetrics.CorrectionFrameAgeMsP95;
      Metrics.CorrectionFrameAssemblyMsP95 = CorrectionMetrics.CorrectionFrameAssemblyMsP95;
      Metrics.CorrectionFrameReplayMsP95 = CorrectionMetrics.CorrectionFrameReplayMsP95;
      Metrics.CorrectionPositionErrorCmP50 = CorrectionMetrics.CorrectionPositionErrorCmP50;
      Metrics.CorrectionPositionErrorCmP95 = CorrectionMetrics.CorrectionPositionErrorCmP95;
      Metrics.CorrectionPositionErrorCmMax = CorrectionMetrics.CorrectionPositionErrorCmMax;
      Metrics.CorrectionYawErrorDegP95 = CorrectionMetrics.CorrectionYawErrorDegP95;
      Metrics.CorrectionVelocityErrorCmpsP95 = CorrectionMetrics.CorrectionVelocityErrorCmpsP95;
      Metrics.CorrectionErrorAgentIdMax = CorrectionMetrics.CorrectionErrorAgentIdMax;
      Metrics.YawErrorAgentIdMax = CorrectionMetrics.YawErrorAgentIdMax;
      Metrics.CorrectionErrorVectorMean = FVector(CorrectionMetrics.CorrectionErrorVectorMean);
      Metrics.CorrectionErrorVectorStdDev = CorrectionMetrics.CorrectionErrorVectorStdDev;
      Metrics.CohortCenterErrorCm = CorrectionMetrics.CohortCenterErrorCm;
      Metrics.CohortYawErrorDeg = CorrectionMetrics.CohortYawErrorDeg;
      Metrics.ResidualErrorAfterCenterAlignP95 = CorrectionMetrics.ResidualErrorAfterCenterAlignP95;
      Metrics.ResidualErrorAfterRigidAlignP95 = CorrectionMetrics.ResidualErrorAfterRigidAlignP95;
      Metrics.RoundTimeDeltaMs = CorrectionMetrics.RoundTimeDeltaMs;
      Metrics.CorrectionIntervalMsP95 = CorrectionMetrics.CorrectionIntervalMsP95;
      break;
    }
  }
  return Metrics;
}

float ACrowdDemoReplicator::ComputeP95(TArray<float> Samples)
{
  if (Samples.IsEmpty())
  {
    return -1.0f;
  }

  Samples.Sort();
  const int32 Index = FMath::Clamp(
    FMath::CeilToInt(static_cast<float>(Samples.Num()) * 0.95f) - 1,
    0,
    Samples.Num() - 1);
  return Samples[Index];
}

int32 ACrowdDemoReplicator::ResolveEntityCount()
{
  int32 Count = 500;
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoEntityCount="), Count);
  return FMath::Clamp(Count, 1, 2000);
}

float ACrowdDemoReplicator::ResolveDurationSeconds()
{
  float Duration = 12.0f;
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoDurationSeconds="), Duration);
  return FMath::Clamp(Duration, 1.0f, 300.0f);
}
