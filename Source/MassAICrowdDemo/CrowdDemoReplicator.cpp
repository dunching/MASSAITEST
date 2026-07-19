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

  CrowdHitFlashInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CrowdHitFlashInstances"));
  CrowdHitFlashInstances->SetupAttachment(SceneRoot);
  CrowdHitFlashInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  CrowdHitFlashInstances->SetMobility(EComponentMobility::Movable);
  CrowdHitFlashInstances->NumCustomDataFloats = 3;

  ProjectileInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ProjectileInstances"));
  ProjectileInstances->SetupAttachment(SceneRoot);
  ProjectileInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  ProjectileInstances->SetMobility(EComponentMobility::Movable);

  ProjectileImpactInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ProjectileImpactInstances"));
  ProjectileImpactInstances->SetupAttachment(SceneRoot);
  ProjectileImpactInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  ProjectileImpactInstances->SetMobility(EComponentMobility::Movable);

  PreviewFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewFloor"));
  PreviewFloor->SetupAttachment(SceneRoot);
  PreviewFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  PreviewFloor->SetMobility(EComponentMobility::Movable);
  PreviewFloor->SetRelativeLocation(FVector(0.0f, -900.0f, -10.0f));
  PreviewFloor->SetRelativeScale3D(FVector(100.0f, 100.0f, 0.05f));

  static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
  if (CubeMesh.Succeeded())
  {
    PreviewFloor->SetStaticMesh(CubeMesh.Object);
  }

  static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
  if (SphereMesh.Succeeded())
  {
    ProjectileInstances->SetStaticMesh(SphereMesh.Object);
    ProjectileImpactInstances->SetStaticMesh(SphereMesh.Object);
  }

  static ConstructorHelpers::FObjectFinder<UStaticMesh> VatMesh(
    TEXT("/Game/CrowdDemo/VAT/T7/Meshes/SM_CrowdDemoBug_Source.SM_CrowdDemoBug_Source"));
  if (VatMesh.Succeeded())
  {
    CrowdInstances->SetStaticMesh(VatMesh.Object);
    CrowdHitFlashInstances->SetStaticMesh(VatMesh.Object);
    bVatRuntimeMeshLoaded = true;
  }

  static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  if (BasicShapeMaterial.Succeeded())
  {
    PreviewFloor->SetMaterial(0, BasicShapeMaterial.Object);
    ProjectileInstances->SetMaterial(0, BasicShapeMaterial.Object);
    ProjectileImpactInstances->SetMaterial(0, BasicShapeMaterial.Object);
  }

  static ConstructorHelpers::FObjectFinder<UMaterialInterface> VatRuntimeMaterial(
    TEXT("/Game/CrowdDemo/VAT/T7/Materials/MI_CrowdDemoBug_Runtime_VAT.MI_CrowdDemoBug_Runtime_VAT"));
  if (VatRuntimeMaterial.Succeeded())
  {
    CrowdInstances->SetMaterial(0, VatRuntimeMaterial.Object);
    bVisualMaterialLoaded = true;
  }

  static ConstructorHelpers::FObjectFinder<UMaterialInterface> VatHitFlashMaterial(
    TEXT("/Game/CrowdDemo/VAT/T7/Materials/MI_CrowdDemoBug_Runtime_HitFlash_VAT.MI_CrowdDemoBug_Runtime_HitFlash_VAT"));
  if (VatHitFlashMaterial.Succeeded())
  {
    CrowdHitFlashInstances->SetMaterial(0, VatHitFlashMaterial.Object);
  }
}

void ACrowdDemoReplicator::BeginPlay()
{
  Super::BeginPlay();

  EntityCount = ResolveEntityCount();
  DurationSeconds = ResolveDurationSeconds();
  StartedSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
  LastMetricSeconds = StartedSeconds;

  if (bLocalVisualHostOnly)
  {
    UE_LOG(LogTemp, Display, TEXT("CrowdDemo: START role=client_visual_host duration=%.2f vat_mesh=%s vat_material=%s source=MassClientBubble"), DurationSeconds, bVatRuntimeMeshLoaded ? TEXT("loaded") : TEXT("missing"), bVisualMaterialLoaded ? TEXT("loaded") : TEXT("missing"));
  }
  else if (HasAuthority())
  {
    RefreshServerSummaryState();
    UE_LOG(LogTemp, Display, TEXT("CrowdDemo: START role=server entity_count=%d duration=%.2f vat_mesh=%s vat_material=%s source=MassClientBubble"), EntityStates.Num(), DurationSeconds, bVatRuntimeMeshLoaded ? TEXT("loaded") : TEXT("missing"), bVisualMaterialLoaded ? TEXT("loaded") : TEXT("missing"));
  }
  else
  {
    UE_LOG(LogTemp, Display, TEXT("CrowdDemo: START role=client duration=%.2f vat_mesh=%s vat_material=%s source=MassClientBubble"), DurationSeconds, bVatRuntimeMeshLoaded ? TEXT("loaded") : TEXT("missing"), bVisualMaterialLoaded ? TEXT("loaded") : TEXT("missing"));
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
  else if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
  {
    ClientFrameMsSamples.Add(DeltaSeconds * 1000.0f);
  }

  if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
  {
    UpdateProjectileVisuals();
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

UInstancedStaticMeshComponent* ACrowdDemoReplicator::GetCrowdHitFlashInstancesForClientVisuals() const
{
  return CrowdHitFlashInstances;
}

void ACrowdDemoReplicator::ClearCrowdVisualInstances()
{
  if (CrowdInstances)
  {
    CrowdInstances->ClearInstances();
    CrowdInstances->MarkRenderStateDirty();
  }
  if (CrowdHitFlashInstances)
  {
    CrowdHitFlashInstances->ClearInstances();
    CrowdHitFlashInstances->MarkRenderStateDirty();
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

void ACrowdDemoReplicator::RecordRoundSimVisualContinuity(
  const int32 AgentId,
  const float SubmitIntervalMs,
  const float SimDeltaCm,
  const float DisplayDeltaCm,
  const float ExpectedDisplayDeltaCm,
  const int32 CollapsedSimSteps,
  const bool bCorrectionBoundary,
  const bool bPlanChanged,
  const bool bTestBoundaryReset,
  const bool bDiscontinuity,
  const int32 PreviousPlanRevision,
  const int32 CurrentPlanRevision,
  const float PreviousSimServerTimeSeconds,
  const float CurrentSimServerTimeSeconds,
  const FVector& PreviousDisplayLocation,
  const FVector& CurrentDisplayLocation)
{
  if (SubmitIntervalMs >= 0.0f) VisualSubmitIntervalMsSamples.Add(SubmitIntervalMs);
  if (SimDeltaCm >= 0.0f) VisualSimDeltaCmSamples.Add(SimDeltaCm);
  if (DisplayDeltaCm >= 0.0f) VisualDisplayDeltaCmSamples.Add(DisplayDeltaCm);
  VisualCollapsedSimStepSamples.Add(static_cast<float>(FMath::Max(0, CollapsedSimSteps)));
  if (CollapsedSimSteps > 1) ++VisualCatchupSubmitCount;
  if (bDiscontinuity && !bCorrectionBoundary && !bPlanChanged
    && !bTestBoundaryReset
    && CollapsedSimSteps > 1)
    ++VisualCatchupDiscontinuityCount;
  if (bDiscontinuity && !bCorrectionBoundary && !bPlanChanged
    && !bTestBoundaryReset
    && CollapsedSimSteps <= 1)
  {
    ++NonCorrectionVisualDiscontinuityCount;
    if (NonCorrectionVisualDiscontinuityCount == 1)
    {
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoVisualDiscontinuityWitness agent=%d display_delta_cm=%.3f expected_cm=%.3f sim_delta_cm=%.3f collapsed_steps=%d submit_interval_ms=%.3f correction=0 plan_changed=0 previous_plan=%d current_plan=%d previous_sim_time=%.3f current_sim_time=%.3f previous_display=(%.3f,%.3f) current_display=(%.3f,%.3f) source=MassClientVisual"),
        AgentId, DisplayDeltaCm, ExpectedDisplayDeltaCm, SimDeltaCm,
        CollapsedSimSteps, SubmitIntervalMs, PreviousPlanRevision,
        CurrentPlanRevision, PreviousSimServerTimeSeconds,
        CurrentSimServerTimeSeconds, PreviousDisplayLocation.X,
        PreviousDisplayLocation.Y, CurrentDisplayLocation.X,
        CurrentDisplayLocation.Y);
    }
  }
  if (bDiscontinuity && bPlanChanged)
    ++RoundResetVisualJumpCount;
  if (bDiscontinuity && bTestBoundaryReset)
    ++TestBoundaryResetVisualJumpCount;
}

void ACrowdDemoReplicator::RecordVisualInstanceRebuild()
{
  ++VisualIsmRebuildCount;
}

void ACrowdDemoReplicator::RecordVisualProcessorPerformance(const float Milliseconds)
{
  if (Milliseconds >= 0.0f)
  {
    VisualProcessorMsSamples.Add(Milliseconds);
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

void ACrowdDemoReplicator::ApplyProjectileVisualEvents(
  const TConstArrayView<FCrowdDemoProjectileVisualEvent> Events)
{
  TArray<FCrowdDemoProjectileVisualEvent> Sorted(Events);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex) return A.FixedStepIndex < B.FixedStepIndex;
    if (A.ProjectileId != B.ProjectileId) return A.ProjectileId < B.ProjectileId;
    return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
  });

  for (const FCrowdDemoProjectileVisualEvent& Event : Sorted)
  {
    const FCrowdDemoProjectileVisualEventKey EventKey{
      Event.ProjectileId, static_cast<uint8>(Event.Kind)};
    if (SeenProjectileVisualEvents.Contains(EventKey))
    {
      continue;
    }
    SeenProjectileVisualEvents.Add(EventKey);
    const int32 RoundId = static_cast<int32>((Event.ProjectileId >> 48) & 0xffffu);
    FCrowdDemoProjectileVisualRoundCounts& Counts = ProjectileVisualRoundCounts.FindOrAdd(RoundId);
    if (Event.Kind == ECrowdDemoProjectileVisualEventKind::Spawn)
    {
      FCrowdDemoProjectileVisualRuntime& Runtime = ActiveProjectileVisuals.FindOrAdd(Event.ProjectileId);
      Runtime.Position = FVector(Event.Position);
      Runtime.Velocity = FVector(Event.Velocity);
      Runtime.ServerTimeSeconds = Event.ServerTimeSeconds;
      Runtime.RadiusCm = Event.RadiusCm;
      ++Counts.Spawn;
    }
    else if (Event.Kind == ECrowdDemoProjectileVisualEventKind::Impact)
    {
      ActiveProjectileVisuals.Remove(Event.ProjectileId);
      ++Counts.Impact;
      if (ProjectileImpactInstances)
      {
        const float Scale = FMath::Max(Event.RadiusCm * 2.0f / 100.0f, 0.01f) * 2.5f;
        ProjectileImpactInstances->AddInstance(
          FTransform(FRotator::ZeroRotator, FVector(Event.Position), FVector(Scale)), true);
        ProjectileImpactExpireWorldSeconds.Add(
          (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + 0.18);
      }
    }
    else if (Event.Kind == ECrowdDemoProjectileVisualEventKind::Expire)
    {
      ActiveProjectileVisuals.Remove(Event.ProjectileId);
      ++Counts.Expire;
    }
  }
}

bool ACrowdDemoReplicator::GetProjectileVisualEventCounts(
  const int32 RoundId,
  int32& OutSpawn,
  int32& OutImpact,
  int32& OutExpire,
  int32& OutActive) const
{
  const FCrowdDemoProjectileVisualRoundCounts* Counts = ProjectileVisualRoundCounts.Find(RoundId);
  OutSpawn = Counts ? Counts->Spawn : 0;
  OutImpact = Counts ? Counts->Impact : 0;
  OutExpire = Counts ? Counts->Expire : 0;
  OutActive = 0;
  for (const auto& Pair : ActiveProjectileVisuals)
  {
    if (static_cast<int32>((Pair.Key >> 48) & 0xffffu) == RoundId)
    {
      ++OutActive;
    }
  }
  return Counts != nullptr;
}

void ACrowdDemoReplicator::UpdateProjectileVisuals()
{
  if (!ProjectileInstances || !ProjectileImpactInstances)
  {
    return;
  }
  const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
  const float ServerSeconds = GameState
    ? GameState->GetServerWorldTimeSeconds()
    : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
  TArray<uint64> ProjectileIds;
  ActiveProjectileVisuals.GetKeys(ProjectileIds);
  ProjectileIds.Sort();
  ProjectileInstances->ClearInstances();
  for (const uint64 ProjectileId : ProjectileIds)
  {
    const FCrowdDemoProjectileVisualRuntime* Runtime = ActiveProjectileVisuals.Find(ProjectileId);
    if (!Runtime)
    {
      continue;
    }
    const float Elapsed = FMath::Max(0.0f, ServerSeconds - Runtime->ServerTimeSeconds);
    const FVector DisplayPosition = Runtime->Position + Runtime->Velocity * Elapsed;
    const float Scale = FMath::Max(Runtime->RadiusCm * 2.0f / 100.0f, 0.01f);
    ProjectileInstances->AddInstance(
      FTransform(Runtime->Velocity.Rotation(), DisplayPosition, FVector(Scale)), true);
  }
  ProjectileInstances->MarkRenderStateDirty();

  const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
  for (int32 Index = ProjectileImpactExpireWorldSeconds.Num() - 1; Index >= 0; --Index)
  {
    if (NowSeconds >= ProjectileImpactExpireWorldSeconds[Index])
    {
      ProjectileImpactInstances->RemoveInstance(Index);
      ProjectileImpactExpireWorldSeconds.RemoveAt(Index);
    }
  }
  ProjectileImpactInstances->MarkRenderStateDirty();
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
    TEXT("CrowdDemoSummary role=%s agents=%d visible_instances=%d server_frame_ms_p95=%.3f snapshot_build_ms_p95=%.3f replication_sample_age_ms_p95=%.3f display_to_sim_cm_p95=%.3f current_round_id=%d completed_round_count=%d correction_frame_applied_count=%d sim_position_error_cm_p95=%.3f source=MassClientBubble"),
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
  if (!bServer)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoVisualPerformance role=client client_frame_ms_p95=%.3f client_frame_ms_max=%.3f visual_processor_ms_p95=%.3f visual_processor_ms_max=%.3f submit_interval_ms_p95=%.3f submit_interval_ms_max=%.3f sim_delta_cm_p95=%.3f sim_delta_cm_max=%.3f display_delta_cm_p95=%.3f display_delta_cm_max=%.3f collapsed_steps_p95=%.3f collapsed_steps_max=%d catchup_submit_count=%d catchup_discontinuity_count=%d non_correction_discontinuity_count=%d round_reset_jump_count=%d test_boundary_reset_jump_count=%d ism_rebuild_count=%d source=MassClientVisual"),
      Metrics.ClientFrameMsP95, Metrics.ClientFrameMsMax,
      Metrics.VisualProcessorMsP95, Metrics.VisualProcessorMsMax,
      Metrics.VisualSubmitIntervalMsP95, Metrics.VisualSubmitIntervalMsMax,
      Metrics.VisualSimDeltaCmP95, Metrics.VisualSimDeltaCmMax,
      Metrics.VisualDisplayDeltaCmP95, Metrics.VisualDisplayDeltaCmMax,
      Metrics.VisualCollapsedSimStepsP95, Metrics.VisualCollapsedSimStepsMax,
      Metrics.VisualCatchupSubmitCount, Metrics.VisualCatchupDiscontinuityCount,
      Metrics.NonCorrectionVisualDiscontinuityCount,
      Metrics.RoundResetVisualJumpCount, Metrics.TestBoundaryResetVisualJumpCount,
      Metrics.VisualIsmRebuildCount);
  }

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
}

FCrowdDemoSummaryMetrics ACrowdDemoReplicator::BuildSummaryMetrics() const
{
  FCrowdDemoSummaryMetrics Metrics;
  Metrics.Agents = EntityStates.Num();
  Metrics.VisibleInstances = GetNetMode() != NM_DedicatedServer ? GetCrowdVisualInstanceCount() : 0;
  Metrics.ServerFrameMsP95 = ComputeP95(ServerFrameMsSamples);
  Metrics.CrowdSolverMsP95 = ComputeP95(SolverMsSamples);
  Metrics.SnapshotBuildMsP95 = Metrics.CrowdSolverMsP95;
  Metrics.ClientFrameMsP95 = ComputeP95(ClientFrameMsSamples);
  Metrics.ClientFrameMsMax = ComputeMax(ClientFrameMsSamples);
  Metrics.VisualProcessorMsP95 = ComputeP95(VisualProcessorMsSamples);
  Metrics.VisualProcessorMsMax = ComputeMax(VisualProcessorMsSamples);
  Metrics.VisualSubmitIntervalMsP95 = ComputeP95(VisualSubmitIntervalMsSamples);
  Metrics.VisualSubmitIntervalMsMax = ComputeMax(VisualSubmitIntervalMsSamples);
  Metrics.VisualSimDeltaCmP95 = ComputeP95(VisualSimDeltaCmSamples);
  Metrics.VisualSimDeltaCmMax = ComputeMax(VisualSimDeltaCmSamples);
  Metrics.VisualDisplayDeltaCmP95 = ComputeP95(VisualDisplayDeltaCmSamples);
  Metrics.VisualDisplayDeltaCmMax = ComputeMax(VisualDisplayDeltaCmSamples);
  Metrics.VisualCollapsedSimStepsP95 = ComputeP95(VisualCollapsedSimStepSamples);
  Metrics.VisualCollapsedSimStepsMax = FMath::Max(
    0, FMath::RoundToInt(ComputeMax(VisualCollapsedSimStepSamples)));
  Metrics.VisualCatchupSubmitCount = VisualCatchupSubmitCount;
  Metrics.VisualCatchupDiscontinuityCount = VisualCatchupDiscontinuityCount;
  Metrics.NonCorrectionVisualDiscontinuityCount = NonCorrectionVisualDiscontinuityCount;
  Metrics.RoundResetVisualJumpCount = RoundResetVisualJumpCount;
  Metrics.TestBoundaryResetVisualJumpCount = TestBoundaryResetVisualJumpCount;
  Metrics.VisualIsmRebuildCount = VisualIsmRebuildCount;
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
      Metrics.SharedFlowMetrics = CompareMetrics.SharedFlowMetrics;
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

float ACrowdDemoReplicator::ComputeMax(const TConstArrayView<float> Samples)
{
  float MaxValue = -1.0f;
  for (const float Value : Samples)
  {
    MaxValue = FMath::Max(MaxValue, Value);
  }
  return MaxValue;
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
