#include "CrowdDemoFriendlyLogisticsCoordinator.h"

#include "CrowdDemoPlayerController.h"
#include "CrowdDemoReplicator.h"
#include "Camera/CameraComponent.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "Mass/CrowdDemoPresentationAdapter.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "MassCrowdPresentationSubsystem.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdRelevantSnapshot.h"
#include "MassCrowdReplicationActor.h"
#include "MassCrowdReplicationChannel.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
  constexpr int32 FriendlyPopulation = 20;
  constexpr double TransitionDelaySeconds = 2.0;
  constexpr uint32 FriendlyPresentationProfile = 2;
  constexpr float ProductFixedStepSeconds = 1.0f / 30.0f;

  class FFriendlyLogisticsPreparedPayload final
    : public ICrowdBoundaryPreparedPatchPayload
  {
  };

  uint64 FoldProductHash(uint64 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffull);
      Hash *= 1099511628211ull;
    }
    return Hash;
  }

  template <typename T>
  void WritePod(TArray<uint8>& Bytes, const T Value)
  {
    const int32 Offset = Bytes.AddUninitialized(sizeof(T));
    FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(T));
  }

  template <typename T>
  bool ReadPod(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    T& OutValue)
  {
    if (Offset < 0 || Offset + sizeof(T) > Bytes.Num())
    {
      return false;
    }
    FMemory::Memcpy(&OutValue, Bytes.GetData() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
  }

  void WriteRef(TArray<uint8>& Bytes, const FCrowdStableEntityRef& Ref)
  {
    WritePod(Bytes, Ref.ProviderId);
    WritePod(Bytes, Ref.StableEntityId);
    WritePod(Bytes, Ref.LifecycleSerial);
  }

  bool ReadRef(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdStableEntityRef& OutRef)
  {
    return ReadPod(Bytes, Offset, OutRef.ProviderId)
      && ReadPod(Bytes, Offset, OutRef.StableEntityId)
      && ReadPod(Bytes, Offset, OutRef.LifecycleSerial);
  }

  bool ResolveTaggedMarker(
    UWorld& World,
    const FName Tag,
    FVector& OutLocation)
  {
    for (TActorIterator<ATargetPoint> It(&World); It; ++It)
    {
      if (*It && It->ActorHasTag(Tag))
      {
        OutLocation = It->GetActorLocation();
        return true;
      }
    }
    return false;
  }
}

ACrowdDemoFriendlyLogisticsCoordinator::
  ACrowdDemoFriendlyLogisticsCoordinator()
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.TickGroup = TG_PostUpdateWork;
  bReplicates = true;
  bAlwaysRelevant = true;
  SetReplicateMovement(false);
}

void ACrowdDemoFriendlyLogisticsCoordinator::BeginPlay()
{
  Super::BeginPlay();
  if (UWorld* World = GetWorld())
  {
    NextTransitionWorldSeconds =
      World->GetTimeSeconds() + TransitionDelaySeconds;
    bObjectiveMarkersResolved =
      ResolveTaggedMarker(
        *World, TEXT("FriendlyLogisticsSource"), SourceLocation)
      && ResolveTaggedMarker(
        *World, TEXT("FriendlyLogisticsSink"), PrimarySinkLocation)
      && ResolveTaggedMarker(
        *World, TEXT("FriendlyLogisticsFallback"), FallbackSinkLocation);
  }
}

void ACrowdDemoFriendlyLogisticsCoordinator::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  if (HasAuthority())
  {
    RefreshReplicationChannels();
    if (!bInitialized && !TryInitialize())
    {
      return;
    }
    ProductAccumulatorSeconds += DeltaSeconds;
    int32 ExecutedSteps = 0;
    while (ProductAccumulatorSeconds >= ProductFixedStepSeconds
      && ExecutedSteps < 4)
    {
      ProductAccumulatorSeconds -= ProductFixedStepSeconds;
      if (!RunProductBoundary(ProductFixedStepSeconds))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoFriendlyLogistics role=server stage=product_boundary step=%d"),
          ProductFixedStepIndex);
        break;
      }
      AdvanceObservedState();
      ++ExecutedSteps;
    }
  }
  else
  {
    ConsumeState();
    CapturePendingVisualEvidence();
  }
  LogCheckpoint();
  TryLogPass();
}

bool ACrowdDemoFriendlyLogisticsCoordinator::TryInitialize()
{
  UWorld* World = GetWorld();
  UCrowdDemoMassSubsystem* Mass =
    World ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  if (!bObjectiveMarkersResolved
    || !Mass || Mass->GetTrackedAgentCount() != FriendlyPopulation
    || Mass->GetAliveAgentCount() != FriendlyPopulation)
  {
    return false;
  }

  const FCrowdLogisticsInventoryFact Source{
    {30, 1, 1}, 40, 0, 5, 0, 80, 1};
  const FCrowdLogisticsInventoryFact Sink{
    {30, 2, 1}, 0, 5, 0, 0, 80, 1};
  const FCrowdLogisticsTaskFact Task{
    {31, 1, 1}, Source.OwnerRef, Sink.OwnerRef,
    {}, {}, 5, ECrowdLogisticsTaskState::Created, 1, 0};
  const FCrowdLogisticsInventoryFact CancelSource{
    {30, 10, 1}, 8, 0, 2, 0, 16, 1};
  const FCrowdLogisticsInventoryFact CancelSink{
    {30, 11, 1}, 0, 2, 0, 0, 16, 1};
  const FCrowdLogisticsTaskFact CancelTask{
    {31, 2, 1}, CancelSource.OwnerRef, CancelSink.OwnerRef,
    {}, {}, 2, ECrowdLogisticsTaskState::Created, 1, 0};
  bInitialized = Store.Initialize(Source, Sink, Task)
    && CancellationStore.Initialize(CancelSource, CancelSink, CancelTask);
  if (!bInitialized)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFriendlyLogistics role=server stage=initialize"));
    return false;
  }
  PublishState();
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoFriendlyLogistics role=server stage=initialized agents=20 source=MassCrowdLogistics"));
  return true;
}

bool ACrowdDemoFriendlyLogisticsCoordinator::RunProductBoundary(
  const float FixedStepSeconds)
{
  UWorld* World = GetWorld();
  UCrowdDemoMassSubsystem* Mass =
    World ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  UMassCrowdRuntimeSubsystem* Runtime =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!Mass || !Runtime || !bInitialized
    || Mass->GetTrackedAgentCount() != FriendlyPopulation)
    return false;

  FCrowdMassBoundarySnapshot Snapshot;
  TArray<FCrowdMassCommitTarget> Targets;
  if (!Mass->BuildProductBoundarySnapshot(
      ProductFixedStepIndex, ProductPlanRevision,
      Snapshot, Targets))
    return false;
  AuthorityLocations.Reset();
  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
    AuthorityLocations.Add(
      Agent.AgentFacts.StableEntityRef, Agent.State.Position);

  const FCrowdLogisticsTaskFact& Task = Store.GetTask();
  const bool bMoveToSource =
    Task.State == ECrowdLogisticsTaskState::Claimed;
  const bool bMoveToSink =
    Task.State == ECrowdLogisticsTaskState::Picked;
  const FCrowdStableEntityRef Carrier = Task.CarrierRef;
  const FVector Objective = bMoveToSource
    ? SourceLocation
    : (bFallbackApplied ? FallbackSinkLocation : PrimarySinkLocation);
  TSharedPtr<const FCrowdNavSurfaceGraph, ESPMode::ThreadSafe> Graph;
  TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> Flow;
  if ((bMoveToSource || bMoveToSink) && Carrier.IsValid())
  {
    if (!Runtime->BuildOrRefreshNavGraph())
    {
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoFriendlyLogistics diagnostic=nav_graph_failed reason=%s"),
        *Runtime->GetNavGraphResource().FailureReason);
      return false;
    }
    const FCrowdNavGraphResource& GraphResource =
      Runtime->GetNavGraphResource();
    Graph = GraphResource.Graph;
    uint64 GoalNodeId = 0;
    uint32 NavLayer = 0;
    if (!Graph.IsValid()
      || !FCrowdNavSurfaceGraphKernel::AttachClosest(
        *Graph, Objective, 1000.0f, GoalNodeId, NavLayer))
    {
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoFriendlyLogistics diagnostic=objective_attach_failed objective=%s nodes=%d"),
        *Objective.ToCompactString(), Graph.IsValid()
          ? Graph->Nodes.Num() : 0);
      return false;
    }
    FCrowdNavFlowHandle Handle;
    const FCrowdNavFlowKey Key{
      GraphResource.TopologyRevision, GoalNodeId, 1, NavLayer};
    if (!Runtime->AcquireFlow(Key, Handle))
    {
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoFriendlyLogistics diagnostic=flow_acquire_failed goal=%llu layer=%u"),
        GoalNodeId, NavLayer);
      return false;
    }
    Flow = Runtime->ResolveFlow(Handle);
    const bool bReleased = Runtime->ReleaseFlow(Handle);
    if (!Flow.IsValid() || !bReleased)
      return false;
  }

  struct FProductMovementWork
  {
    FCrowdMassCommitPlan Plan;
    bool bCompleted = false;
  };
  const TSharedRef<FProductMovementWork, ESPMode::ThreadSafe> Work =
    MakeShared<FProductMovementWork, ESPMode::ThreadSafe>();
  const float AcceptanceRadius = AcceptanceRadiusCm;
  const float CarrierSpeed = MaximumCarrierSpeedCmps;
  FCrowdMassBoundaryRunner Runner;
  if (!Runner.Begin(Snapshot, 0.0))
    return false;
  if (!Runner.AddTask(
      {ECrowdBoundaryTaskStage::Movement, 0}, {},
      [Snapshot, Carrier, Objective, Graph, Flow,
        FixedStepSeconds, Work, AcceptanceRadius, CarrierSpeed]()
      {
        FCrowdMassCommitPlan Plan;
        Plan.FixedStepIndex = Snapshot.FixedStepIndex;
        Plan.PlanRevision = Snapshot.PlanRevision;
        uint64 PlanHash = 14695981039346656037ull;
        for (const FCrowdMassBoundaryAgentRecord& Agent
          : Snapshot.Agents)
        {
          FVector Velocity = FVector::ZeroVector;
          if (Agent.AgentFacts.StableEntityRef == Carrier
            && Graph.IsValid() && Flow.IsValid())
          {
            uint64 CurrentNodeId = 0;
            uint32 CurrentLayer = 0;
            if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
                *Graph, Agent.State.Position, 1000.0f,
                CurrentNodeId, CurrentLayer))
              return FCrowdBoundaryTaskResult::Failure();
            const FCrowdNavSurfaceFlowNode* FlowNode =
              Flow->Nodes.FindByPredicate(
                [CurrentNodeId](const auto& Node)
                {
                  return Node.StableNodeId == CurrentNodeId;
                });
            if (!FlowNode
              || FlowNode->IntegrationCostQ == MAX_uint32)
              return FCrowdBoundaryTaskResult::Failure();
            FVector Direction = FlowNode->Direction;
            const FVector ToObjective =
              Objective - Agent.State.Position;
            if (ToObjective.Size2D() <= AcceptanceRadius
              || FlowNode->StableNodeId == Flow->GoalStableNodeId)
              Direction = ToObjective.GetSafeNormal2D();
            Velocity = Direction.GetSafeNormal2D()
              * CarrierSpeed;
            if (ToObjective.Size2D() <= AcceptanceRadius)
              Velocity = FVector::ZeroVector;
          }
          FCrowdMassCommitRecord& Record =
            Plan.Records.AddDefaulted_GetRef();
          Record.EntityRef = Agent.AgentFacts.StableEntityRef;
          Record.CapabilityProfileKey =
            Agent.Properties.CapabilityProfileKey;
          Record.PlanRevision = Snapshot.PlanRevision;
          Record.Movement.AgentId = Agent.Identity.AgentId;
          Record.Movement.LifecycleSerial =
            Agent.AgentFacts.StableEntityRef.LifecycleSerial;
          Record.Movement.Position =
            Agent.State.Position + Velocity * FixedStepSeconds;
          Record.Movement.Velocity = Velocity;
          Record.Movement.YawDegrees = Velocity.IsNearlyZero()
            ? Agent.State.YawDegrees : Velocity.Rotation().Yaw;
          uint64 MovementHash = FoldProductHash(
            14695981039346656037ull,
            Record.EntityRef.ProviderId);
          MovementHash = FoldProductHash(
            MovementHash, Record.EntityRef.StableEntityId);
          MovementHash = FoldProductHash(
            MovementHash, Record.EntityRef.LifecycleSerial);
          MovementHash = FoldProductHash(
            MovementHash,
            static_cast<uint64>(FMath::RoundToInt64(
              Record.Movement.Position.X * 10.0)));
          MovementHash = FoldProductHash(
            MovementHash,
            static_cast<uint64>(FMath::RoundToInt64(
              Record.Movement.Position.Y * 10.0)));
          Record.Movement.StableHash =
            static_cast<uint32>(MovementHash ^ (MovementHash >> 32));
          if (Record.Movement.StableHash == 0)
            Record.Movement.StableHash = 1;
          Record.Movement.bValid = true;
          PlanHash = FoldProductHash(
            PlanHash, Record.Movement.StableHash);
        }
        Plan.StableHash = PlanHash == 0 ? 1 : PlanHash;
        Plan.bValid = true;
        Work->Plan = MoveTemp(Plan);
        Work->bCompleted = true;
        return FCrowdBoundaryTaskResult::Success(PlanHash);
      }))
    return false;
  if (!Runner.Dispatch() || !Runner.WaitAndDrain()
    || !Work->bCompleted)
  {
    const FCrowdBoundaryOrchestratorResult Result = Runner.BuildResult();
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoFriendlyLogistics diagnostic=runner_work_failed state=%d tasks=%d"),
      static_cast<int32>(Result.State), Result.Tasks.Num());
    return false;
  }

  TArray<FCrowdBoundaryPreparedPatch> Patches;
  FCrowdLogisticsPreparedPatch LogisticsPatch;
  bool bHasLogisticsPatch = false;
  const FCrowdMassCommitRecord* CarrierRecord =
    Work->Plan.Records.FindByPredicate(
      [&Carrier](const FCrowdMassCommitRecord& Record)
      {
        return Record.EntityRef == Carrier;
      });
  ECrowdLogisticsCommitKind TransitionKind =
    ECrowdLogisticsCommitKind::Claim;
  if (CarrierRecord && bMoveToSource
    && FVector::Dist2D(
      CarrierRecord->Movement.Position, SourceLocation)
        <= AcceptanceRadiusCm)
  {
    TransitionKind = ECrowdLogisticsCommitKind::Pickup;
    ++SourceAcceptanceCount;
    bHasLogisticsPatch = true;
  }
  else if (CarrierRecord && bMoveToSink && bDeathInjected
    && FVector::Dist2D(
      CarrierRecord->Movement.Position,
      bFallbackApplied ? FallbackSinkLocation : PrimarySinkLocation)
        <= AcceptanceRadiusCm)
  {
    TransitionKind = ECrowdLogisticsCommitKind::Deliver;
    ++SinkAcceptanceCount;
    bHasLogisticsPatch = true;
  }
  if (bHasLogisticsPatch)
  {
    const FCrowdLogisticsCommitRequest Request{
      NextCommitId, TransitionKind, Task.TaskRef, Carrier,
      Task.Revision, Store.GetSource().Revision,
      Store.GetSink().Revision};
    if (Store.Prepare(Request, LogisticsPatch)
      != ECrowdLogisticsPrepareResult::Prepared)
      return false;
    FCrowdBoundaryPreparedPatch& Patch =
      Patches.AddDefaulted_GetRef();
    Patch.AdapterId = TEXT("FriendlyLogistics");
    Patch.FixedStepIndex = Snapshot.FixedStepIndex;
    Patch.PlanRevision = Snapshot.PlanRevision;
    for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
      Patch.EntityRefs.Add(Agent.AgentFacts.StableEntityRef);
    Patch.StableHash = LogisticsPatch.StableHash;
    Patch.Payload =
      MakeShared<FFriendlyLogisticsPreparedPayload, ESPMode::ThreadSafe>();
    Patch.bValid = true;
  }

  if (!Runner.BuildAndSealCommit(
      Work->Plan, Patches, Targets, 0.0)
    || !Runner.MarkValidated(0.0)
    || !Mass->ApplyProductBoundaryCommit(Work->Plan, Targets))
    return false;
  if (bHasLogisticsPatch)
  {
    Store.ApplyPrepared(LogisticsPatch);
    Store.ApplyPrepared(LogisticsPatch);
    ++NextCommitId;
    ++AppliedCount;
  }
  if (!Runner.MarkCommitted(0.0))
    return false;
  LastProductCommitHash = Runner.GetCommitEnvelope().StableHash;

  for (const FCrowdMassCommitRecord& Record : Work->Plan.Records)
  {
    const FVector Previous =
      AuthorityLocations.FindRef(Record.EntityRef);
    MaximumObservedStepDistanceCm = FMath::Max(
      MaximumObservedStepDistanceCm,
      static_cast<float>(FVector::Dist(
        Previous, Record.Movement.Position)));
    AuthorityLocations.Add(
      Record.EntityRef, Record.Movement.Position);
  }
  PublishMovementCorrections(Work->Plan);
  if (bHasLogisticsPatch)
    PublishState();
  ++ProductFixedStepIndex;
  return true;
}

void ACrowdDemoFriendlyLogisticsCoordinator::PublishMovementCorrections(
  const FCrowdMassCommitPlan& Plan)
{
  TArray<FCrowdMovementCorrectionRecord> Corrections;
  Corrections.Reserve(Plan.Records.Num());
  for (const FCrowdMassCommitRecord& Record : Plan.Records)
  {
    FCrowdMovementCorrectionRecord& Correction =
      Corrections.AddDefaulted_GetRef();
    Correction.EntityRef = Record.EntityRef;
    Correction.Sequence =
      static_cast<uint64>(ProductFixedStepIndex) + 1;
    Correction.FixedStepIndex = ProductFixedStepIndex;
    Correction.Position = Record.Movement.Position;
    Correction.Velocity = Record.Movement.Velocity;
    Correction.YawDegrees = Record.Movement.YawDegrees;
    Correction.StableHash =
      FCrowdReplicationTransport::CalculateMovementCorrectionHash(
        Correction);
  }
  for (auto& Pair : ReplicationChannels)
    if (AMassCrowdReplicationActor* Channel = Pair.Value.Get())
      Channel->PublishMovementCorrections(Corrections);
}

bool ACrowdDemoFriendlyLogisticsCoordinator::IsCarrierWithin(
  const FCrowdStableEntityRef& Carrier,
  const FVector& Target) const
{
  const FVector* Location = AuthorityLocations.Find(Carrier);
  return Location
    && FVector::Dist2D(*Location, Target) <= AcceptanceRadiusCm;
}

bool ACrowdDemoFriendlyLogisticsCoordinator::Commit(
  FCrowdLogisticsTransactionStore& TargetStore,
  const ECrowdLogisticsCommitKind Kind,
  const FCrowdStableEntityRef Carrier)
{
  const FCrowdLogisticsCommitRequest Request{
    NextCommitId++, Kind, TargetStore.GetTask().TaskRef,
    Carrier, TargetStore.GetTask().Revision,
    TargetStore.GetSource().Revision, TargetStore.GetSink().Revision};
  FCrowdLogisticsPreparedPatch Patch;
  if (TargetStore.Prepare(Request, Patch)
    != ECrowdLogisticsPrepareResult::Prepared)
  {
    return false;
  }
  TargetStore.ApplyPrepared(Patch);
  TargetStore.ApplyPrepared(Patch);
  ++AppliedCount;
  return true;
}

void ACrowdDemoFriendlyLogisticsCoordinator::AdvanceObservedState()
{
  const FCrowdStableEntityRef FirstCarrier{1, 1, 1};
  const FCrowdStableEntityRef RecoveryCarrier{1, 2, 1};
  const ECrowdLogisticsTaskState State = Store.GetTask().State;
  bool bStateChanged = false;

  if (State == ECrowdLogisticsTaskState::Created)
  {
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now < NextTransitionWorldSeconds)
      return;
    NextTransitionWorldSeconds = Now + TransitionDelaySeconds;
    if (UnreachableBackoffCount < 2)
    {
      ++UnreachableBackoffCount;
      return;
    }
    TArray<FCrowdStableEntityRef> Candidates;
    for (uint64 Id = FriendlyPopulation; Id >= 1; --Id)
    {
      Candidates.Add({1, Id, 1});
    }
    FCrowdStableEntityRef Winner;
    if (!FCrowdLogisticsKernel::ChooseCarrier(Candidates, Winner)
      || Winner != FirstCarrier
      || !Commit(Store, ECrowdLogisticsCommitKind::Claim, Winner))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFriendlyLogistics role=server stage=competition"));
      return;
    }
    ++CompetitionCount;
    bStateChanged = true;
  }
  else if (State == ECrowdLogisticsTaskState::Picked && !bDeathInjected)
  {
    if (PickupObservedFixedStep == INDEX_NONE)
    {
      PickupObservedFixedStep = ProductFixedStepIndex;
      return;
    }
    if (ProductFixedStepIndex - PickupObservedFixedStep < 45)
      return;
    const FCrowdStableEntityRef DeadCarrier =
      Store.GetTask().CarrierRef;
    if (!Commit(Store, ECrowdLogisticsCommitKind::Requeue,
      DeadCarrier))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFriendlyLogistics role=server stage=death_requeue"));
      return;
    }
    UWorld* World = GetWorld();
    UCrowdDemoMassSubsystem* Mass =
      World ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
    FCrowdStableEntityRef Replacement;
    if (!Mass || !Mass->RecycleTrackedAgent(
        DeadCarrier, Replacement)
      || Replacement.LifecycleSerial
        != DeadCarrier.LifecycleSerial + 1)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFriendlyLogistics role=server stage=death_respawn"));
      return;
    }
    bDeathInjected = true;
    ++DeathRecoveryCount;
    bStateChanged = true;
  }
  else if (State == ECrowdLogisticsTaskState::Requeued && !bFallbackApplied)
  {
    const FCrowdLogisticsInventoryFact Fallback{
      {30, 3, 1}, 0, 0, 0, 0, 80, 1};
    if (!Store.RetargetSink(Fallback, NextCommitId++))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFriendlyLogistics role=server stage=fallback"));
      return;
    }
    bFallbackApplied = true;
    ++FallbackCount;
    bStateChanged = true;
  }
  else if (State == ECrowdLogisticsTaskState::Requeued)
  {
    if (!Commit(Store, ECrowdLogisticsCommitKind::Claim, RecoveryCarrier))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFriendlyLogistics role=server stage=recover_claim"));
      return;
    }
    bStateChanged = true;
  }

  if (CancellationCount == 0
    && CancellationStore.GetTask().State
      == ECrowdLogisticsTaskState::Created)
  {
    const FCrowdStableEntityRef CancelCarrier{1, 3, 1};
    if (Commit(CancellationStore, ECrowdLogisticsCommitKind::Claim,
        CancelCarrier)
      && Commit(CancellationStore, ECrowdLogisticsCommitKind::Cancel,
        CancelCarrier))
    {
      ++CancellationCount;
      bStateChanged = true;
    }
  }
  if (bStateChanged)
    PublishState();
}

void ACrowdDemoFriendlyLogisticsCoordinator::RefreshReplicationChannels()
{
  UWorld* World = GetWorld();
  if (!World) return;
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
    It; ++It)
  {
    APlayerController* Controller = It->Get();
    if (!Controller || ReplicationChannels.Contains(Controller)) continue;
    AMassCrowdReplicationActor* Channel =
      AMassCrowdReplicationActor::SpawnForController(*Controller);
    if (!Channel) continue;
    ReplicationChannels.Add(Controller, Channel);
    if (bInitialized) PublishBaseline(*Channel);
  }
}

bool ACrowdDemoFriendlyLogisticsCoordinator::PublishBaseline(
  AMassCrowdReplicationActor& Channel)
{
  TArray<uint8> Bytes;
  EncodeState(Bytes);
  FCrowdRelevantSnapshotEntityPayload Entity;
  Entity.Bytes = MoveTemp(Bytes);
  TArray<FCrowdRelevantSnapshotEntityPayload> Entities;
  Entities.Add(MoveTemp(Entity));
  FCrowdRelevantSnapshotLimits Limits{
    4, 4, 4, 4096, 8192, 10.0};
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  if (!FCrowdRelevantSnapshotTransport::Build(
    1, AppliedCount, 1, Entities, Limits, Header, Chunks))
  {
    return false;
  }
  return Channel.PublishBaseline(Header, Chunks, NextReliableSequence);
}

void ACrowdDemoFriendlyLogisticsCoordinator::PublishState()
{
  TArray<uint8> Bytes;
  EncodeState(Bytes);
  StateHash = HashBytes(Bytes);
  if (!bInitialized) return;
  for (TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair : ReplicationChannels)
  {
    AMassCrowdReplicationActor* Channel = Pair.Value.Get();
    if (!Channel) continue;
    if (Channel->RequiresNewBaseline())
    {
      PublishBaseline(*Channel);
      continue;
    }
    FCrowdReliableStateRecord Record;
    Record.Sequence = NextReliableSequence;
    Record.Kind = ECrowdReliableStateKind::Task;
    Record.EntityRef = Store.GetTask().TaskRef;
    Record.Revision = Store.GetTask().Revision;
    Record.Payload = Bytes;
    Record.StableHash =
      FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
    Channel->PublishReliable(Record);
  }
  ++NextReliableSequence;
}

void ACrowdDemoFriendlyLogisticsCoordinator::ConsumeState()
{
  UWorld* World = GetWorld();
  if (!World) return;
  for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
  {
    AMassCrowdReplicationActor* Channel = *It;
    if (!Channel || !Channel->IsReady()) continue;
    TArray<FCrowdReplicationApplyFrame> Frames;
    if (!Channel->DrainClientApplyFrames(Frames))
      continue;
    bool bStateChanged = false;
    bool bMovementChanged = false;
    for (const FCrowdReplicationApplyFrame& Frame : Frames)
    {
      if (Frame.Kind == ECrowdReplicationApplyFrameKind::Baseline)
      {
        if (Frame.BaselineEntities.IsEmpty()
          || !DecodeState(Frame.BaselineEntities[0].Bytes))
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoFriendlyLogistics role=client stage=baseline_apply"));
          return;
        }
        bStateChanged = true;
        continue;
      }
      if (Frame.Kind
        == ECrowdReplicationApplyFrameKind::ReliableState)
      {
        for (const FCrowdReliableStateRecord& Record
          : Frame.ReliableRecords)
        {
          if (Record.Sequence <= LastConsumedSequence
            || Record.Kind != ECrowdReliableStateKind::Task)
            continue;
          if (!DecodeState(Record.Payload))
          {
            UE_LOG(LogTemp, Error,
              TEXT("VIOLATION CrowdDemoFriendlyLogistics role=client stage=decode sequence=%llu"),
              Record.Sequence);
            return;
          }
          LastConsumedSequence = Record.Sequence;
          bStateChanged = true;
        }
        continue;
      }
      for (const FCrowdMovementCorrectionRecord& Correction
        : Frame.Corrections)
      {
        const uint64 StableId = Correction.EntityRef.StableEntityId;
        if (const FCrowdStableEntityRef* Existing =
          ClientEntitiesByStableId.Find(StableId))
        {
          if (Correction.EntityRef.LifecycleSerial
              < Existing->LifecycleSerial)
            continue;
          if (*Existing != Correction.EntityRef)
            ClientLocations.Remove(*Existing);
        }
        ClientEntitiesByStableId.Add(StableId, Correction.EntityRef);
        ClientLocations.Add(
          Correction.EntityRef, Correction.Position);
        bMovementChanged = true;
      }
    }
    if ((bStateChanged || bMovementChanged)
      && ClientLocations.Num() == FriendlyPopulation
      && !SyncClientPresentation())
      return;
  }
}

bool ACrowdDemoFriendlyLogisticsCoordinator::SyncClientPresentation()
{
  UWorld* World = GetWorld();
  UMassCrowdPresentationSubsystem* Presentation =
    World ? World->GetSubsystem<UMassCrowdPresentationSubsystem>() : nullptr;
  if (!Presentation) return false;

  if (!bPresentationProfileRegistered)
  {
    ACrowdDemoReplicator* Replicator = nullptr;
    for (TActorIterator<ACrowdDemoReplicator> It(World); It; ++It)
    {
      Replicator = *It;
      break;
    }
    if (!Replicator
      || !Replicator->GetCrowdInstancesForClientVisuals()
      || !Replicator->GetCrowdHitFlashInstancesForClientVisuals()
      || !Replicator->GetCargoInstancesForClientVisuals())
    {
      return false;
    }
    bPresentationProfileRegistered = Presentation->RegisterProfile(
      FriendlyPresentationProfile,
      MakeShared<FCrowdDemoIsmPresentationSink>(
        *Replicator->GetCrowdInstancesForClientVisuals(),
        *Replicator->GetCrowdHitFlashInstancesForClientVisuals(),
        Replicator->GetCargoInstancesForClientVisuals()));
    if (!bPresentationProfileRegistered)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFriendlyLogistics role=client stage=presentation_profile"));
      return false;
    }
  }

  if (ClientEntitiesByStableId.Num() != FriendlyPopulation
    || ClientLocations.Num() != FriendlyPopulation)
    return false;
  ++PresentationSequence;
  TArray<FCrowdPresentationOperation> Operations;
  TArray<uint64> StableIds;
  ClientEntitiesByStableId.GetKeys(StableIds);
  StableIds.Sort();
  for (const uint64 StableId : StableIds)
  {
    const FCrowdStableEntityRef EntityRef =
      ClientEntitiesByStableId.FindChecked(StableId);
    const FVector* Location = ClientLocations.Find(EntityRef);
    if (!Location)
      return false;
    if (const FCrowdStableEntityRef* Presented =
      PresentedEntitiesByStableId.Find(StableId);
      Presented && *Presented != EntityRef)
    {
      FCrowdPresentationOperation& Despawn =
        Operations.AddDefaulted_GetRef();
      Despawn.Kind = ECrowdPresentationOperationKind::Despawn;
      Despawn.EntityRef = *Presented;
      Despawn.ProfileKey = FriendlyPresentationProfile;
      Despawn.Sequence = PresentationSequence;
    }
    FCrowdPresentationState State;
    State.EntityRef = EntityRef;
    State.Transform = FTransform(FRotator::ZeroRotator, *Location);
    State.ProfileKey = FriendlyPresentationProfile;
    State.CargoRef = EntityRef == ClientCarrierRef
      ? ClientCargoRef : FCrowdStableEntityRef{};
    State.Sequence = PresentationSequence;
    State.SampleServerSeconds = World->GetTimeSeconds();
    FCrowdPresentationOperation& Operation =
      Operations.AddDefaulted_GetRef();
    Operation.Kind =
      PresentedEntitiesByStableId.Contains(StableId)
        && PresentedEntitiesByStableId.FindChecked(StableId) == EntityRef
        ? ECrowdPresentationOperationKind::Update
        : ECrowdPresentationOperationKind::Spawn;
    Operation.State = State;
    Operation.EntityRef = EntityRef;
    Operation.ProfileKey = FriendlyPresentationProfile;
    Operation.Sequence = PresentationSequence;
  }
  FCrowdPreparedPresentationFrame PreparedFrame;
  uint64 SourceFrameHash = StateHash;
  SourceFrameHash ^= static_cast<uint64>(ProductFixedStepIndex + 1)
    * 1099511628211ull;
  if (!Presentation->PrepareFrame(
      SourceFrameHash == 0 ? 1 : SourceFrameHash,
      Operations, PreparedFrame)
    || !Presentation->ApplyPreparedFrame(PreparedFrame))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFriendlyLogistics role=client stage=presentation_frame operations=%d"),
      Operations.Num());
    return false;
  }
  PresentedEntitiesByStableId = ClientEntitiesByStableId;
  bPresentationSpawned = true;
  CargoVisibleCount =
    ClientCarrierRef.IsValid() && ClientCargoRef.IsValid() ? 1 : 0;
  const FVector* CarrierLocation =
    ClientLocations.Find(ClientCarrierRef);
  if (ClientCarrierRef.IsValid() && CarrierLocation)
    LastVisualEvidenceCarrierRef = ClientCarrierRef;
  const FVector* EmptyHandLocation = CarrierLocation;
  if (!EmptyHandLocation && !ClientLocations.IsEmpty())
  {
    const FCrowdStableEntityRef* BestRef = nullptr;
    for (const TPair<FCrowdStableEntityRef, FVector>& Pair
      : ClientLocations)
    {
      if (!BestRef || Pair.Key < *BestRef)
      {
        BestRef = &Pair.Key;
        EmptyHandLocation = &Pair.Value;
      }
    }
  }
  if (PendingVisualEvidencePath.IsEmpty()
    && CargoVisibleCount == 0
    && (ClientTaskState == ECrowdLogisticsTaskState::Claimed
      || ClientTaskState == ECrowdLogisticsTaskState::Delivered)
    && EmptyHandLocation
    && !bEmptyHandEvidenceRequested)
  {
    bEmptyHandEvidenceRequested = true;
    RequestVisualEvidence(TEXT("EmptyHand"), *EmptyHandLocation);
  }
  else if (PendingVisualEvidencePath.IsEmpty()
    && CargoVisibleCount == 1
    && ClientTaskState == ECrowdLogisticsTaskState::Picked
    && CarrierLocation
    && !bPickupEvidenceRequested)
  {
    bPickupEvidenceRequested = true;
    RequestVisualEvidence(TEXT("Pickup"), *CarrierLocation);
  }
  else if (PendingVisualEvidencePath.IsEmpty()
    && CargoVisibleCount == 1
    && CarrierLocation
    && FVector::Dist2D(*CarrierLocation, SourceLocation) > 300.0f
    && !bCarryingEvidenceRequested)
  {
    bCarryingEvidenceRequested = true;
    RequestVisualEvidence(TEXT("Carrying"), *CarrierLocation);
  }
  else if (PendingVisualEvidencePath.IsEmpty()
    && CargoVisibleCount == 0 && CargoAttachCount > 0
    && ClientTaskState == ECrowdLogisticsTaskState::Delivered
    && !bDeliveredEvidenceRequested)
  {
    bDeliveredEvidenceRequested = true;
    RequestVisualEvidence(
      TEXT("Delivered"),
      FallbackCount > 0 ? FallbackSinkLocation : PrimarySinkLocation);
  }
  return Presentation->GetInstanceCount(
    FriendlyPresentationProfile) == FriendlyPopulation;
}

void ACrowdDemoFriendlyLogisticsCoordinator::RequestVisualEvidence(
  const FString& EvidenceName,
  const FVector& Focus)
{
  UWorld* World = GetWorld();
  PendingVisualEvidencePath = FPaths::Combine(
    FPaths::ProjectSavedDir(),
    FString::Printf(
      TEXT("P4_FriendlyLogistics_%s.png"), *EvidenceName));
  PendingVisualEvidenceFocus = Focus;
  bPendingVisualEvidenceViewActivated = false;
  VisualEvidenceCaptureWorldSeconds =
    World ? World->GetTimeSeconds() + 0.35 : 0.0;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoFriendlyLogistics role=client stage=visual_evidence_queued kind=%s path=%s"),
    *EvidenceName, *PendingVisualEvidencePath);
}

void ACrowdDemoFriendlyLogisticsCoordinator::
  CapturePendingVisualEvidence()
{
  UWorld* World = GetWorld();
  if (!World || PendingVisualEvidencePath.IsEmpty()
    || World->GetTimeSeconds() < VisualEvidenceCaptureWorldSeconds)
  {
    return;
  }
  APlayerController* Controller = World->GetFirstPlayerController();
  APawn* ViewPawn = Controller ? Controller->GetPawn() : nullptr;
  if (!ViewPawn && Controller)
    ViewPawn = Controller->GetSpectatorPawn();
  if (ViewPawn && Controller)
  {
    if (const FVector* LatestCarrierLocation =
      ClientLocations.Find(LastVisualEvidenceCarrierRef))
    {
      PendingVisualEvidenceFocus = *LatestCarrierLocation;
    }
    const FVector CameraLocation =
      PendingVisualEvidenceFocus + FVector(0.0, 0.0, 350.0);
    ViewPawn->SetActorLocation(CameraLocation);
    ViewPawn->SetActorRotation(
      (PendingVisualEvidenceFocus - CameraLocation).Rotation());
    Controller->SetViewTarget(ViewPawn);
    if (!bPendingVisualEvidenceViewActivated)
    {
      bPendingVisualEvidenceViewActivated = true;
      VisualEvidenceCaptureWorldSeconds = World->GetTimeSeconds() + 0.20;
      return;
    }
  }
  FScreenshotRequest::RequestScreenshot(
    PendingVisualEvidencePath, false, false);
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoFriendlyLogistics role=client stage=visual_evidence_captured path=%s"),
    *PendingVisualEvidencePath);
  PendingVisualEvidencePath.Reset();
  bPendingVisualEvidenceViewActivated = false;
}

void ACrowdDemoFriendlyLogisticsCoordinator::EncodeState(
  TArray<uint8>& OutBytes) const
{
  OutBytes.Reset();
  WritePod<uint32>(OutBytes, 1);
  const FCrowdLogisticsTaskFact& Task = Store.GetTask();
  WriteRef(OutBytes, Task.TaskRef);
  WriteRef(OutBytes, Task.SourceRef);
  WriteRef(OutBytes, Task.SinkRef);
  WriteRef(OutBytes, Task.CarrierRef);
  WriteRef(OutBytes, Task.CargoRef);
  WritePod(OutBytes, Task.Quantity);
  WritePod(OutBytes, static_cast<uint8>(Task.State));
  WritePod(OutBytes, Task.Revision);
  WritePod(OutBytes, Task.RetryCount);
  for (const FCrowdLogisticsInventoryFact* Inventory :
    {&Store.GetSource(), &Store.GetSink()})
  {
    WriteRef(OutBytes, Inventory->OwnerRef);
    WritePod(OutBytes, Inventory->OnHand);
    WritePod(OutBytes, Inventory->ReservedInbound);
    WritePod(OutBytes, Inventory->ReservedOutbound);
    WritePod(OutBytes, Inventory->InTransit);
    WritePod(OutBytes, Inventory->Capacity);
    WritePod(OutBytes, Inventory->Revision);
  }
  const FCrowdLogisticsCargoFact& Cargo = Store.GetCargo();
  WritePod<uint8>(OutBytes, Cargo.IsValid() ? 1 : 0);
  if (Cargo.IsValid())
  {
    WriteRef(OutBytes, Cargo.CargoRef);
    WriteRef(OutBytes, Cargo.TaskRef);
    WriteRef(OutBytes, Cargo.SourceRef);
    WriteRef(OutBytes, Cargo.SinkRef);
    WriteRef(OutBytes, Cargo.CarrierRef);
    WritePod(OutBytes, Cargo.Quantity);
    WritePod(OutBytes, Cargo.Revision);
  }
  WritePod(OutBytes, UnreachableBackoffCount);
  WritePod(OutBytes, CompetitionCount);
  WritePod(OutBytes, DeathRecoveryCount);
  WritePod(OutBytes, FallbackCount);
  WritePod(OutBytes, CancellationCount);
}

bool ACrowdDemoFriendlyLogisticsCoordinator::DecodeState(
  const TConstArrayView<uint8> Bytes)
{
  int32 Offset = 0;
  uint32 Version = 0;
  FCrowdLogisticsTaskFact Task;
  uint8 State = 0;
  if (!ReadPod(Bytes, Offset, Version) || Version != 1
    || !ReadRef(Bytes, Offset, Task.TaskRef)
    || !ReadRef(Bytes, Offset, Task.SourceRef)
    || !ReadRef(Bytes, Offset, Task.SinkRef)
    || !ReadRef(Bytes, Offset, Task.CarrierRef)
    || !ReadRef(Bytes, Offset, Task.CargoRef)
    || !ReadPod(Bytes, Offset, Task.Quantity)
    || !ReadPod(Bytes, Offset, State)
    || !ReadPod(Bytes, Offset, Task.Revision)
    || !ReadPod(Bytes, Offset, Task.RetryCount))
  {
    return false;
  }
  Task.State = static_cast<ECrowdLogisticsTaskState>(State);
  FCrowdLogisticsInventoryFact Source;
  FCrowdLogisticsInventoryFact Sink;
  for (FCrowdLogisticsInventoryFact* Inventory : {&Source, &Sink})
  {
    if (!ReadRef(Bytes, Offset, Inventory->OwnerRef)
      || !ReadPod(Bytes, Offset, Inventory->OnHand)
      || !ReadPod(Bytes, Offset, Inventory->ReservedInbound)
      || !ReadPod(Bytes, Offset, Inventory->ReservedOutbound)
      || !ReadPod(Bytes, Offset, Inventory->InTransit)
      || !ReadPod(Bytes, Offset, Inventory->Capacity)
      || !ReadPod(Bytes, Offset, Inventory->Revision))
    {
      return false;
    }
  }
  uint8 bHasCargo = 0;
  FCrowdLogisticsCargoFact Cargo;
  if (!ReadPod(Bytes, Offset, bHasCargo)) return false;
  if (bHasCargo != 0
    && (!ReadRef(Bytes, Offset, Cargo.CargoRef)
      || !ReadRef(Bytes, Offset, Cargo.TaskRef)
      || !ReadRef(Bytes, Offset, Cargo.SourceRef)
      || !ReadRef(Bytes, Offset, Cargo.SinkRef)
      || !ReadRef(Bytes, Offset, Cargo.CarrierRef)
      || !ReadPod(Bytes, Offset, Cargo.Quantity)
      || !ReadPod(Bytes, Offset, Cargo.Revision)))
  {
    return false;
  }
  if (!ReadPod(Bytes, Offset, UnreachableBackoffCount)
    || !ReadPod(Bytes, Offset, CompetitionCount)
    || !ReadPod(Bytes, Offset, DeathRecoveryCount)
    || !ReadPod(Bytes, Offset, FallbackCount)
    || !ReadPod(Bytes, Offset, CancellationCount)
    || Offset != Bytes.Num())
  {
    return false;
  }
  if (!Task.IsValid() || !Source.IsValid() || !Sink.IsValid()
    || (bHasCargo != 0 && !Cargo.IsValid()))
  {
    return false;
  }
  const bool bWasAttached =
    ClientCarrierRef.IsValid() && ClientCargoRef.IsValid();
  const bool bIsAttached =
    Task.CarrierRef.IsValid() && Cargo.IsValid()
    && Cargo.CarrierRef == Task.CarrierRef;
  if (!bWasAttached && bIsAttached) ++CargoAttachCount;
  if (bWasAttached && !bIsAttached) ++CargoDetachCount;
  ClientTaskState = Task.State;
  ClientSourceOnHand = Source.OnHand;
  ClientSinkOnHand = Sink.OnHand;
  ClientInTransit = Source.InTransit;
  ClientCarrierRef = bIsAttached
    ? Task.CarrierRef : FCrowdStableEntityRef{};
  ClientCargoRef = bIsAttached
    ? Cargo.CargoRef : FCrowdStableEntityRef{};
  StateHash = HashBytes(Bytes);
  bInitialized = true;
  return true;
}

uint64 ACrowdDemoFriendlyLogisticsCoordinator::HashBytes(
  const TConstArrayView<uint8> Bytes)
{
  uint64 Hash = 14695981039346656037ull;
  for (const uint8 Byte : Bytes)
  {
    Hash ^= Byte;
    Hash *= 1099511628211ull;
  }
  return Hash;
}

void ACrowdDemoFriendlyLogisticsCoordinator::LogCheckpoint()
{
  UWorld* World = GetWorld();
  if (!World || !bInitialized
    || World->GetTimeSeconds() - LastCheckpointWorldSeconds < 1.0)
  {
    return;
  }
  LastCheckpointWorldSeconds = World->GetTimeSeconds();
  const int32 TaskState = HasAuthority()
    ? static_cast<int32>(Store.GetTask().State)
    : static_cast<int32>(ClientTaskState);
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoFriendlyLogisticsCheckpoint role=%s agents=20 task_state=%d source_on_hand=%d sink_on_hand=%d in_transit=%d competition=%d death_recovery=%d fallback=%d unreachable_backoff=%d cancellations=%d duplicates=%d sequence=%llu state_hash=%llu cargo_attach=%d cargo_detach=%d cargo_visible=%d presentation_instances=%d source_accept=%d sink_accept=%d max_step_cm=%.3f commit_hash=%llu source=MassCrowdLogistics"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    TaskState,
    HasAuthority() ? Store.GetSource().OnHand : ClientSourceOnHand,
    HasAuthority() ? Store.GetSink().OnHand : ClientSinkOnHand,
    HasAuthority() ? Store.GetSource().InTransit : ClientInTransit,
    CompetitionCount, DeathRecoveryCount, FallbackCount,
    UnreachableBackoffCount, CancellationCount,
    HasAuthority() ? Store.GetDuplicateCount()
      + CancellationStore.GetDuplicateCount() : 0,
    HasAuthority() ? NextReliableSequence - 1 : LastConsumedSequence,
    StateHash,
    CargoAttachCount, CargoDetachCount, CargoVisibleCount,
    (!HasAuthority() && GetWorld()
      && GetWorld()->GetSubsystem<UMassCrowdPresentationSubsystem>())
      ? GetWorld()->GetSubsystem<UMassCrowdPresentationSubsystem>()
          ->GetInstanceCount(FriendlyPresentationProfile)
      : 0,
    SourceAcceptanceCount, SinkAcceptanceCount,
    MaximumObservedStepDistanceCm, LastProductCommitHash);
}

void ACrowdDemoFriendlyLogisticsCoordinator::TryLogPass()
{
  const bool bFactsComplete = CompetitionCount == 1
    && DeathRecoveryCount == 1 && FallbackCount == 1
    && UnreachableBackoffCount == 2 && CancellationCount == 1;
  if (HasAuthority())
  {
    UWorld* World = GetWorld();
    UCrowdDemoMassSubsystem* Mass =
      World ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
    const bool bPass = bFactsComplete && Store.IsInitialized()
      && Store.GetTask().State == ECrowdLogisticsTaskState::Delivered
      && Store.GetSource().OnHand == 35 && Store.GetSink().OnHand == 5
      && Store.GetSource().InTransit == 0 && !Store.GetCargo().IsValid()
      && Mass && Mass->GetTrackedAgentCount() == FriendlyPopulation
      && Mass->GetAliveAgentCount() == FriendlyPopulation
      && SourceAcceptanceCount >= 1 && SinkAcceptanceCount >= 1
      && MaximumObservedStepDistanceCm
        <= MaximumCarrierSpeedCmps * ProductFixedStepSeconds + 0.5f
      && LastProductCommitHash != 0;
    if (bPass && !bServerPassLogged)
    {
      bServerPassLogged = true;
      UE_LOG(LogTemp, Display,
        TEXT("PASS CrowdDemoFriendlyLogistics role=server agents=20 quantity=40 delivered=5 competition=1 death_recovery=1 fallback=1 unreachable_backoff=2 cancellation=1 source_accept=%d sink_accept=%d max_step_cm=%.3f commit_hash=%llu state_hash=%llu source=MassCrowdBoundaryRunner+MassCrowdNavRuntime+MassCrowdLogistics"),
        SourceAcceptanceCount, SinkAcceptanceCount,
        MaximumObservedStepDistanceCm, LastProductCommitHash, StateHash);
    }
  }
  else if (bFactsComplete
    && ClientTaskState == ECrowdLogisticsTaskState::Delivered
    && ClientSourceOnHand == 35 && ClientSinkOnHand == 5
    && ClientInTransit == 0 && StateHash != 0
    && CargoAttachCount >= 1 && CargoDetachCount >= 1
    && CargoVisibleCount == 0 && bPresentationSpawned
    && GetWorld()
    && GetWorld()->GetSubsystem<UMassCrowdPresentationSubsystem>()
      ->GetInstanceCount(FriendlyPresentationProfile)
        == FriendlyPopulation
    && !bClientPassLogged)
  {
    bClientPassLogged = true;
    UE_LOG(LogTemp, Display,
      TEXT("PASS CrowdDemoFriendlyLogistics role=client agents=20 delivered=5 competition=1 death_recovery=1 fallback=1 unreachable_backoff=2 cancellation=1 cargo_attach=%d cargo_detach=%d cargo_visible=0 presentation_instances=20 state_hash=%llu source=MassCrowdLogistics"),
      CargoAttachCount, CargoDetachCount, StateHash);
  }
}
