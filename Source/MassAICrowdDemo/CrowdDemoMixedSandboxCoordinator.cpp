#include "CrowdDemoMixedSandboxCoordinator.h"

#include "Camera/CameraActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CrowdDemoReplicator.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HighResScreenshot.h"
#include "MassCommonFragments.h"
#include "Mass/CrowdDemoPresentationAdapter.h"
#include "MassCrowdPresentationSubsystem.h"
#include "MassCrowdReplicationActor.h"
#include "MassCrowdRuntimeFragments.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
  constexpr double MixedFixedStepSeconds = 1.0 / 30.0;
  constexpr int32 MixedPopulation = 20;
  constexpr int32 LifecycleIntervalSteps = 45;
  constexpr float MixedStartDelaySeconds = 5.0f;
  constexpr float MinimumSafeSeparationCm = 70.0f;

  ACrowdDemoReplicator* FindMixedVisualHost(UWorld& World)
  {
    for (TActorIterator<ACrowdDemoReplicator> It(&World); It; ++It)
    {
      if (*It && !It->IsLocalVisualHostOnly()) return *It;
    }
    return nullptr;
  }

  uint32 BehaviorBit(const ECrowdActiveBehavior Behavior)
  {
    const uint8 Index = static_cast<uint8>(Behavior);
    return Index < 32 ? uint32{1} << Index : 0;
  }

  uint64 FoldMixedHash(uint64 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(
        (Value >> (Byte * 8)) & 0xffull);
      Hash *= 1099511628211ull;
    }
    return Hash;
  }

  void WriteU32(TArray<uint8>& Bytes, const uint32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
      Bytes.Add(static_cast<uint8>((Value >> (Byte * 8)) & 0xffu));
  }

  void WriteU64(TArray<uint8>& Bytes, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
      Bytes.Add(static_cast<uint8>((Value >> (Byte * 8)) & 0xffull));
  }

  bool ReadU32(
    const TConstArrayView<uint8> Bytes, int32& Offset, uint32& Out)
  {
    if (Offset < 0 || Offset + 4 > Bytes.Num()) return false;
    Out = 0;
    for (int32 Byte = 0; Byte < 4; ++Byte)
      Out |= static_cast<uint32>(Bytes[Offset++]) << (Byte * 8);
    return true;
  }

  bool ReadU64(
    const TConstArrayView<uint8> Bytes, int32& Offset, uint64& Out)
  {
    if (Offset < 0 || Offset + 8 > Bytes.Num()) return false;
    Out = 0;
    for (int32 Byte = 0; Byte < 8; ++Byte)
      Out |= static_cast<uint64>(Bytes[Offset++]) << (Byte * 8);
    return true;
  }

  void EncodeMixedAgent(
    const FCrowdDemoMixedAgentState& State,
    const int64 FixedStepIndex,
    const uint64 LifecycleResumeSequence,
    const uint32 InRelevantSetRevision,
    TArray<uint8>& OutBytes)
  {
    OutBytes.Reset();
    WriteU64(OutBytes, static_cast<uint64>(FixedStepIndex));
    WriteU64(OutBytes, LifecycleResumeSequence);
    WriteU32(OutBytes, InRelevantSetRevision);
    WriteU64(OutBytes, State.StableEntityId);
    WriteU32(OutBytes, State.LifecycleSerial);
    WriteU32(OutBytes, State.MembershipKey);
    WriteU32(OutBytes, static_cast<uint32>(
      FMath::RoundToInt(State.Location.X * 10.0)));
    WriteU32(OutBytes, static_cast<uint32>(
      FMath::RoundToInt(State.Location.Y * 10.0)));
    WriteU32(OutBytes, static_cast<uint32>(
      FMath::RoundToInt(State.Location.Z * 10.0)));
    OutBytes.Add(State.Behavior);
    OutBytes.Add(State.Health);
    WriteU32(OutBytes, State.TargetProviderId);
    WriteU64(OutBytes, State.TargetStableEntityId);
    WriteU32(OutBytes, State.TargetLifecycleSerial);
    WriteU32(OutBytes, State.TaskProviderId);
    WriteU64(OutBytes, State.TaskStableEntityId);
    WriteU32(OutBytes, State.TaskLifecycleSerial);
  }

  bool DecodeMixedAgent(
    const TConstArrayView<uint8> Bytes,
    FCrowdDemoMixedAgentState& OutState,
    int64& OutFixedStepIndex,
    uint64& OutLifecycleResumeSequence,
    uint32& OutRelevantSetRevision)
  {
    OutState = {};
    uint64 Step = 0;
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Z = 0;
    int32 Offset = 0;
    if (!ReadU64(Bytes, Offset, Step)
      || !ReadU64(Bytes, Offset, OutLifecycleResumeSequence)
      || !ReadU32(Bytes, Offset, OutRelevantSetRevision)
      || !ReadU64(Bytes, Offset, OutState.StableEntityId)
      || !ReadU32(Bytes, Offset, OutState.LifecycleSerial)
      || !ReadU32(Bytes, Offset, OutState.MembershipKey)
      || !ReadU32(Bytes, Offset, X)
      || !ReadU32(Bytes, Offset, Y)
      || !ReadU32(Bytes, Offset, Z)
      || Offset + 2 > Bytes.Num())
      return false;
    OutState.Behavior = Bytes[Offset++];
    OutState.Health = Bytes[Offset++];
    if (!ReadU32(Bytes, Offset, OutState.TargetProviderId)
      || !ReadU64(Bytes, Offset, OutState.TargetStableEntityId)
      || !ReadU32(Bytes, Offset, OutState.TargetLifecycleSerial)
      || !ReadU32(Bytes, Offset, OutState.TaskProviderId)
      || !ReadU64(Bytes, Offset, OutState.TaskStableEntityId)
      || !ReadU32(Bytes, Offset, OutState.TaskLifecycleSerial)
      || Offset != Bytes.Num())
      return false;
    OutFixedStepIndex = static_cast<int64>(Step);
    OutState.Location = FVector(
      static_cast<int32>(X) / 10.0,
      static_cast<int32>(Y) / 10.0,
      static_cast<int32>(Z) / 10.0);
    return true;
  }

  void EncodeLifecycleOperation(
    const FCrowdDemoContinuousLifecycleOperation& Operation,
    TArray<uint8>& OutBytes)
  {
    OutBytes.Reset();
    WriteU64(OutBytes, Operation.Sequence);
    WriteU64(OutBytes, static_cast<uint64>(Operation.FixedStepIndex));
    WriteU32(OutBytes, Operation.RelevantSetRevision);
    WriteU64(OutBytes, Operation.StableEntityId);
    WriteU32(OutBytes, Operation.LifecycleSerial);
    WriteU32(OutBytes, Operation.PreviousMembershipKey);
    WriteU32(OutBytes, Operation.NewMembershipKey);
    OutBytes.Add(static_cast<uint8>(Operation.Kind));
    OutBytes.Add(Operation.DespawnReason);
  }

  bool DecodeLifecycleOperation(
    const TConstArrayView<uint8> Bytes,
    FCrowdDemoContinuousLifecycleOperation& OutOperation)
  {
    OutOperation = {};
    uint64 Step = 0;
    int32 Offset = 0;
    if (!ReadU64(Bytes, Offset, OutOperation.Sequence)
      || !ReadU64(Bytes, Offset, Step)
      || !ReadU32(Bytes, Offset, OutOperation.RelevantSetRevision)
      || !ReadU64(Bytes, Offset, OutOperation.StableEntityId)
      || !ReadU32(Bytes, Offset, OutOperation.LifecycleSerial)
      || !ReadU32(Bytes, Offset, OutOperation.PreviousMembershipKey)
      || !ReadU32(Bytes, Offset, OutOperation.NewMembershipKey)
      || Offset + 2 != Bytes.Num())
      return false;
    OutOperation.Kind =
      static_cast<ECrowdDemoContinuousLifecycleOperationKind>(
        Bytes[Offset++]);
    OutOperation.DespawnReason = Bytes[Offset++];
    OutOperation.FixedStepIndex = static_cast<int64>(Step);
    return static_cast<uint8>(OutOperation.Kind)
      <= static_cast<uint8>(
        ECrowdDemoContinuousLifecycleOperationKind::Membership);
  }

}

ACrowdDemoMixedSandboxCoordinator::ACrowdDemoMixedSandboxCoordinator()
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.TickGroup = TG_PostUpdateWork;
  PrimaryActorTick.EndTickGroup = TG_PostUpdateWork;
  bReplicates = true;
  bAlwaysRelevant = true;
  SetNetUpdateFrequency(30.0f);
  SetMinNetUpdateFrequency(15.0f);
}

void ACrowdDemoMixedSandboxCoordinator::BeginPlay()
{
  Super::BeginPlay();
  bCaptureRequested = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoCaptureMixedSandbox"));
  if (HasAuthority()) TryInitializeServer();
}

void ACrowdDemoMixedSandboxCoordinator::EndPlay(
  const EEndPlayReason::Type EndPlayReason)
{
  if (UWorld* World = GetWorld())
  {
    if (UMassCrowdRuntimeSubsystem* Runtime =
      World->GetSubsystem<UMassCrowdRuntimeSubsystem>())
    {
      for (const TPair<uint64, FCrowdNavFlowHandle>& Pair
        : FlowHandleByGoalNode)
        Runtime->ReleaseFlow(Pair.Value);
    }
  }
  FlowHandleByGoalNode.Reset();
  NavGraphHandle.Reset();
  Super::EndPlay(EndPlayReason);
}

void ACrowdDemoMixedSandboxCoordinator::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  UWorld* World = GetWorld();
  if (!World) return;

  if (!HasAuthority())
  {
    ClientFrameMilliseconds.Add(FMath::Max(0.0f, DeltaSeconds) * 1000.0);
    if (ClientFrameMilliseconds.Num() > 2048) ClientFrameMilliseconds.RemoveAt(0, 512);
  }

  if (HasAuthority() && !bWorldInitialized
    && World->GetTimeSeconds() >= NextInitializationAttemptSeconds)
  {
    NextInitializationAttemptSeconds = World->GetTimeSeconds() + 1.0;
    TryInitializeServer();
  }
  if (!bWorldInitialized) return;
  if (HasAuthority())
    RefreshReplicationChannels();
  else
    ConsumeProductReplication();

  if (!HasAuthority() && bCaptureRequested && !bCaptureCompleted
    && CaptureAtWorldSeconds > 0.0
    && World->GetTimeSeconds() >= CaptureAtWorldSeconds)
  {
    FScreenshotRequest::RequestScreenshot(
      FPaths::ProjectSavedDir() / TEXT("StageJ_MixedSandbox_Visual.png"), false, false);
    bCaptureCompleted = true;
  }

  if (HasAuthority() && World->GetTimeSeconds() >= MixedStartDelaySeconds)
  {
    FixedStepAccumulatorSeconds += FMath::Max(DeltaSeconds, 0.0f);
    int32 Steps = 0;
    while (FixedStepAccumulatorSeconds >= MixedFixedStepSeconds && Steps < 8)
    {
      FixedStepAccumulatorSeconds -= MixedFixedStepSeconds;
      AdvanceServerFixedStep();
      ++Steps;
    }
  }

  if (!HasAuthority() && bVisualSyncPending) SyncClientVisualsIncremental();
  if (World->GetTimeSeconds() - LastCheckpointWorldSeconds >= 5.0)
  {
    LastCheckpointWorldSeconds = World->GetTimeSeconds();
    LogCheckpoint();
  }
  TryLogPass();
}

void ACrowdDemoMixedSandboxCoordinator::GetLifetimeReplicatedProps(
  TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(ACrowdDemoMixedSandboxCoordinator, Config);
}

void ACrowdDemoMixedSandboxCoordinator::OnRep_Config()
{
  if (!HasAuthority() && Config.bValid != 0 && !bWorldInitialized)
  {
    if (!InitializeLifecycleWorld())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=initialize"));
    }
  }
}

bool ACrowdDemoMixedSandboxCoordinator::TryInitializeServer()
{
  if (!HasAuthority() || bWorldInitialized) return bWorldInitialized;
  UWorld* World = GetWorld();
  if (!World) return false;

  UMassCrowdRuntimeSubsystem* Runtime =
    World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!Runtime) return false;
  FCrowdNavSurfaceGraphBuildConfig BuildConfig;
  BuildConfig.MinPortalWidthCm = 70;
  Runtime->SetGraphBuildConfig(BuildConfig);
  if (!Runtime->BuildOrRefreshNavGraph()) return false;
  const FCrowdNavGraphResource& Resource = Runtime->GetNavGraphResource();
  if (!Resource.IsReady()) return false;
  NavGraphHandle = Resource.Graph;

  MarkerLocations.Reset();
  for (TActorIterator<ATargetPoint> It(World); It; ++It)
  {
    for (const FName Tag : It->Tags)
    {
      if (Tag.ToString().StartsWith(TEXT("CrowdNav")))
        MarkerLocations.Add(Tag, It->GetActorLocation());
    }
  }
  const FName Required[] = {
    TEXT("CrowdNavLower"), TEXT("CrowdNavHigh"), TEXT("CrowdNavRouteA"),
    TEXT("CrowdNavRouteB"), TEXT("CrowdNavGoal")};
  for (const FName Tag : Required)
    if (!MarkerLocations.Contains(Tag)) return false;

  Config = {};
  Config.bValid = 1;
  Config.PopulationLimit = MixedPopulation;
  Config.SnapshotRevision = 1;
  Config.RelevantSetRevision = 1;
  Config.NavTopologyHash = Resource.TopologyHash;
  if (!InitializeLifecycleWorld())
  {
    Config.bValid = 0;
    return false;
  }
  ForceNetUpdate();
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoMixedSandbox role=server stage=initialized active=%d nodes=%d topology_hash=%llu source=LifecycleBehaviorSurfaceFlow"),
    LifecycleWorld.GetActiveEntityCount(),
    NavGraphHandle->Nodes.Num(), NavGraphHandle->TopologyHash);
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::InitializeLifecycleWorld()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem || Config.bValid == 0
    || Config.PopulationLimit != MixedPopulation) return false;

  FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
  const TArray<const UScriptStruct*> Types = {
    FCrowdMassAgentFragment::StaticStruct(),
    FCrowdMassBehaviorFragment::StaticStruct(),
    FCrowdMassMembershipFragment::StaticStruct(),
    FTransformFragment::StaticStruct(),
    FCrowdMassAgentTag::StaticStruct()};
  LifecycleArchetype = EntityManager.CreateArchetype(Types);
  if (!LifecycleArchetype.IsValid()) return false;

  Slots.SetNum(Config.PopulationLimit + 1);
  TArray<FCrowdLifecycleSnapshotEntity> Snapshot;
  Snapshot.Reserve(Config.PopulationLimit);
  for (int32 SlotIndex = 1; SlotIndex <= Config.PopulationLimit; ++SlotIndex)
  {
    InitializeSlotState(SlotIndex, 1);
    Snapshot.Add({Slots[SlotIndex].Facts, Slots[SlotIndex].MembershipKey});
  }
  FCrowdLifecycleBatchLimits Limits;
  Limits.MaxSnapshotEntities = Config.PopulationLimit;
  Limits.MaxEntriesPerBatch = Config.PopulationLimit;
  Limits.MaxTrackedSlots = Config.PopulationLimit;
  Limits.MaxSequenceHistory = 256;
  if (!LifecycleWorld.InitializeFromSnapshot(
    EntityManager, LifecycleArchetype, Config.SnapshotRevision, 0,
    Config.RelevantSetRevision, Snapshot, Limits)) return false;

  for (int32 SlotIndex = 1; SlotIndex <= Config.PopulationLimit; ++SlotIndex)
  {
    FMassEntityHandle Entity;
    if (!LifecycleWorld.TryGetEntityHandle(Slots[SlotIndex].Facts.StableEntityRef, Entity))
      return false;
    FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
    if (!Transform) return false;
    Transform->GetMutableTransform().SetLocation(Slots[SlotIndex].Location);
  }

  FixedStepIndex = 0;
  NextLifecycleSequence = 1;
  RelevantSetRevision = Config.RelevantSetRevision;
  MaxObservedPopulation = Config.PopulationLimit;
  LastExpectedEntitySetHash = LifecycleWorld.CalculateEntitySetHash();
  LastExpectedMembershipHash = LifecycleWorld.CalculateMembershipHash();
  bWorldInitialized = true;
  bVisualSyncPending = !HasAuthority();
  return true;
}

void ACrowdDemoMixedSandboxCoordinator::InitializeSlotState(
  const int32 SlotIndex,
  const uint32 LifecycleSerial)
{
  FSlotState& Slot = Slots[SlotIndex];
  Slot = {};
  Slot.Facts = MakeAgentFacts(SlotIndex, LifecycleSerial);
  Slot.MembershipKey = MembershipForBehavior(Slot.Facts.ActiveBehavior);
  Slot.Health = 100;
  Slot.bActive = true;
  Slot.TransitionRevision = 1;

  if (!NavGraphHandle.IsValid() || NavGraphHandle->Nodes.IsEmpty())
  {
    Slot.Location = FVector((SlotIndex % 5) * 180.0f, (SlotIndex / 5) * 180.0f, 60.0f);
    return;
  }

  const FCrowdNavSurfaceGraph& Graph = *NavGraphHandle;
  const FCrowdNavSurfaceFlow* GoalFlow = nullptr;
  GetOrBuildFlow(Marker(TEXT("CrowdNavGoal"), Graph.Nodes[0].Center), GoalFlow);
  const int32 Start = (SlotIndex * 17) % Graph.Nodes.Num();
  for (int32 Offset = 0; Offset < Graph.Nodes.Num(); ++Offset)
  {
    const int32 NodeIndex = (Start + Offset) % Graph.Nodes.Num();
    if (GoalFlow && GoalFlow->Nodes[NodeIndex].IntegrationCostQ == MAX_uint32) continue;
    const FVector Candidate = Graph.Nodes[NodeIndex].Center;
    bool bSeparated = true;
    for (int32 Other = 1; Other < SlotIndex; ++Other)
    {
      if (Slots[Other].bActive
        && FVector::Distance(Candidate, Slots[Other].Location) < 160.0f)
      {
        bSeparated = false;
        break;
      }
    }
    if (bSeparated)
    {
      Slot.Location = Candidate;
      return;
    }
  }
  Slot.Location = Graph.Nodes[Start].Center;
}

FCrowdAgentFacts ACrowdDemoMixedSandboxCoordinator::MakeAgentFacts(
  const int32 SlotIndex,
  const uint32 LifecycleSerial) const
{
  FCrowdAgentFacts Facts;
  Facts.StableEntityRef = {1, static_cast<uint64>(SlotIndex), LifecycleSerial};
  Facts.FactionKey = static_cast<uint32>((SlotIndex % 3) + 1);
  Facts.CapabilitySet.Add(ECrowdCapability::Move);
  Facts.CapabilitySet.Add(ECrowdCapability::UseNavLayer);
  if (SlotIndex <= 6)
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Haul);
    Facts.ActiveBehavior = ECrowdActiveBehavior::HaulPickup;
  }
  else if (SlotIndex <= 12)
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Pursue);
    Facts.CapabilitySet.Add(ECrowdCapability::Attack);
    Facts.ActiveBehavior = ECrowdActiveBehavior::Pursue;
  }
  else if (SlotIndex <= 16)
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Guard);
    Facts.CapabilitySet.Add(ECrowdCapability::Flee);
    Facts.ActiveBehavior = ECrowdActiveBehavior::Guard;
  }
  else
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Wander);
    Facts.CapabilitySet.Add(ECrowdCapability::MoveTo);
    Facts.ActiveBehavior = ECrowdActiveBehavior::Wander;
  }
  Facts.MovementProfileKey = 1;
  Facts.PresentationProfileKey = 1;
  Facts.RuntimeState = 1;
  return Facts;
}

void ACrowdDemoMixedSandboxCoordinator::AdvanceServerFixedStep()
{
  const double StartSeconds = FPlatformTime::Seconds();
  ++FixedStepIndex;
  if (!RebuildSpatialSafety())
  {
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=spatial_safety fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  const TArray<FSlotState> OriginalSlots = Slots;
  const FCrowdDemoBusinessCommitLedger OriginalBusinessLedger =
    BusinessLedger;
  const int32 OriginalBehaviorTransitionCount =
    BehaviorTransitionCount;
  const int32 OriginalDuplicateCommitCount =
    DuplicateCommitCount;
  const uint32 OriginalSeenBehaviorBits = SeenBehaviorBits;
  const int32 OriginalPendingCombatDeathSlot =
    PendingCombatDeathSlot;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    if (Slots[SlotIndex].bActive && !EvaluateSlotBehavior(SlotIndex))
    {
      Slots = OriginalSlots;
      BusinessLedger = OriginalBusinessLedger;
      BehaviorTransitionCount =
        OriginalBehaviorTransitionCount;
      DuplicateCommitCount = OriginalDuplicateCommitCount;
      SeenBehaviorBits = OriginalSeenBehaviorBits;
      PendingCombatDeathSlot =
        OriginalPendingCombatDeathSlot;
      ++StaleRejectCount;
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=behavior slot=%d fixed_step=%lld"),
        SlotIndex, FixedStepIndex);
      return;
    }
  }
  TArray<FCrowdAgentFacts> PreparedFacts;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
    if (Slots[SlotIndex].bActive)
      PreparedFacts.Add(Slots[SlotIndex].Facts);
  PreparedFacts.Sort([](const auto& A, const auto& B)
  {
    return A.StableEntityRef < B.StableEntityRef;
  });
  if (!LifecycleWorld.ApplyAgentFactsCorrectionsAtBoundary(
      FixedStepIndex, PreparedFacts))
  {
    Slots = OriginalSlots;
    BusinessLedger = OriginalBusinessLedger;
    BehaviorTransitionCount =
      OriginalBehaviorTransitionCount;
    DuplicateCommitCount = OriginalDuplicateCommitCount;
    SeenBehaviorBits = OriginalSeenBehaviorBits;
    PendingCombatDeathSlot =
      OriginalPendingCombatDeathSlot;
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=business_commit fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  if (!RunProductMovementBoundary())
  {
    TArray<FCrowdAgentFacts> OriginalFacts;
    for (int32 SlotIndex = 1;
      SlotIndex < OriginalSlots.Num(); ++SlotIndex)
      if (OriginalSlots[SlotIndex].bActive)
        OriginalFacts.Add(
          OriginalSlots[SlotIndex].Facts);
    OriginalFacts.Sort([](const auto& A, const auto& B)
    {
      return A.StableEntityRef < B.StableEntityRef;
    });
    const bool bRolledBack =
      LifecycleWorld.ApplyAgentFactsCorrectionsAtBoundary(
        FixedStepIndex, OriginalFacts);
    Slots = OriginalSlots;
    BusinessLedger = OriginalBusinessLedger;
    BehaviorTransitionCount =
      OriginalBehaviorTransitionCount;
    DuplicateCommitCount = OriginalDuplicateCommitCount;
    SeenBehaviorBits = OriginalSeenBehaviorBits;
    PendingCombatDeathSlot =
      OriginalPendingCombatDeathSlot;
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=product_boundary fixed_step=%lld rollback=%d"),
      FixedStepIndex, bRolledBack ? 1 : 0);
    return;
  }

  MinimumSeparationCm = TNumericLimits<float>::Max();
  for (int32 A = 1; A < Slots.Num(); ++A)
  {
    if (!Slots[A].bActive) continue;
    for (int32 B = A + 1; B < Slots.Num(); ++B)
    {
      if (!Slots[B].bActive
        || FMath::Abs(Slots[A].Location.Z - Slots[B].Location.Z) > 150.0f) continue;
      MinimumSeparationCm = FMath::Min(
        MinimumSeparationCm,
        FVector::Distance(Slots[A].Location, Slots[B].Location));
    }
  }

  if (FixedStepIndex % LifecycleIntervalSteps == 0)
  {
    FCrowdDemoContinuousLifecycleOperation Operation;
    if (BuildLifecycleOperation(Operation))
    {
      if (!ApplyLifecycleOperation(Operation))
      {
        ++StaleRejectCount;
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=lifecycle sequence=%llu"),
          Operation.Sequence);
        return;
      }
      FCrowdReliableStateRecord Record;
      Record.Sequence = NextStateSequence++;
      if (Operation.Kind
        == ECrowdDemoContinuousLifecycleOperationKind::Spawn)
        Record.Kind = ECrowdReliableStateKind::Spawn;
      else if (Operation.Kind
        == ECrowdDemoContinuousLifecycleOperationKind::Despawn)
        Record.Kind = ECrowdReliableStateKind::Despawn;
      else
        Record.Kind = ECrowdReliableStateKind::Membership;
      Record.EntityRef = {
        1, Operation.StableEntityId, Operation.LifecycleSerial};
      Record.Revision = Operation.RelevantSetRevision;
      EncodeLifecycleOperation(Operation, Record.Payload);
      Record.StableHash =
        FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
      for (TPair<TWeakObjectPtr<APlayerController>,
        TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
        : ReplicationChannels)
      {
        if (AMassCrowdReplicationActor* Channel = Pair.Value.Get())
          Channel->PublishReliable(Record);
      }
    }
  }
  if (FixedStepIndex % 3 == 0) PublishProductStateFrame();
  ServerStepMilliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
  if (ServerStepMilliseconds.Num() > 2048) ServerStepMilliseconds.RemoveAt(0, 512);
}

bool ACrowdDemoMixedSandboxCoordinator::EvaluateSlotBehavior(
  const int32 SlotIndex)
{
  FSlotState& Slot = Slots[SlotIndex];
  const ECrowdActiveBehavior Requested = ChooseBehavior(SlotIndex);
  const FVector Objective = ChooseObjectiveLocation(SlotIndex, Requested);
  const FCrowdStableEntityRef TargetRef = ChooseTargetRef(SlotIndex, Requested);
  const float Distance = FVector::Distance(Slot.Location, Objective);

  FCrowdRuntimeBehaviorContext Context;
  Context.AgentFacts = Slot.Facts;
  Context.RequestedBehavior = Requested;
  Context.FixedStepIndex = FixedStepIndex;
  Context.TransitionRevision = Slot.TransitionRevision;
  Context.TargetRef = TargetRef;
  Context.TargetLocation = Objective;
  Context.ObjectiveKey = static_cast<uint32>(Requested) + 1;
  Context.MovementProfileKey = 1;
  Context.InteractionPayloadKey = static_cast<uint32>(Requested) + 1;
  Context.InteractionQuantity = Requested == ECrowdActiveBehavior::Attack ? 25 : 1;
  if (Requested == ECrowdActiveBehavior::HaulPickup
    || Requested == ECrowdActiveBehavior::HaulDeliver)
  {
    Context.TaskRef = {2, static_cast<uint64>(SlotIndex), 1};
  }
  const bool bAttackReady = Requested == ECrowdActiveBehavior::Attack
    && Distance <= 180.0f
    && FixedStepIndex - Slot.LastAttackFixedStep >= 30;
  Context.bInteractionReady = bAttackReady
    || ((Requested == ECrowdActiveBehavior::HaulPickup
      || Requested == ECrowdActiveBehavior::HaulDeliver) && Distance <= 90.0f);

  FCrowdRuntimeBehaviorOutput Output;
  if (!FCrowdRuntimeBehaviorTransition::Evaluate(
    BehaviorProviders, Context, Output)) return false;
  if (Slot.Facts.ActiveBehavior != Requested)
  {
    ++BehaviorTransitionCount;
    ++Slot.TransitionRevision;
  }
  SeenBehaviorBits |= BehaviorBit(Requested);
  if (!FCrowdRuntimeBehaviorTransition::Commit(
      Output, Slot.Facts))
    return false;

  if (Output.BusinessCommitRequest.Kind != ECrowdBusinessCommitKind::None)
  {
    const ECrowdDemoBusinessCommitAcceptResult First =
      BusinessLedger.Apply(Output.BusinessCommitRequest);
    const ECrowdDemoBusinessCommitAcceptResult Replay =
      BusinessLedger.Apply(Output.BusinessCommitRequest);
    if (First != ECrowdDemoBusinessCommitAcceptResult::Applied
      || Replay != ECrowdDemoBusinessCommitAcceptResult::Duplicate) return false;
    ++DuplicateCommitCount;
    if (Requested == ECrowdActiveBehavior::Attack)
    {
      Slot.LastAttackFixedStep = FixedStepIndex;
      const int32 TargetSlot = static_cast<int32>(TargetRef.StableEntityId);
      if (!Slots.IsValidIndex(TargetSlot) || !Slots[TargetSlot].bActive) return false;
      Slots[TargetSlot].Health = FMath::Max(0, Slots[TargetSlot].Health - 25);
      if (Slots[TargetSlot].Health == 0 && PendingCombatDeathSlot == INDEX_NONE)
        PendingCombatDeathSlot = TargetSlot;
    }
  }

  return true;
}

ECrowdActiveBehavior ACrowdDemoMixedSandboxCoordinator::ChooseBehavior(
  const int32 SlotIndex) const
{
  const FSlotState& Slot = Slots[SlotIndex];
  if (SlotIndex <= 6)
  {
    return BusinessLedger.GetCargoCarrier(static_cast<uint64>(SlotIndex))
      == static_cast<uint64>(SlotIndex)
      ? ECrowdActiveBehavior::HaulDeliver : ECrowdActiveBehavior::HaulPickup;
  }
  if (SlotIndex <= 12)
  {
    const FCrowdStableEntityRef Target = ChooseTargetRef(SlotIndex, ECrowdActiveBehavior::Pursue);
    const int32 TargetSlot = static_cast<int32>(Target.StableEntityId);
    return Slots.IsValidIndex(TargetSlot) && Slots[TargetSlot].bActive
      && FVector::Distance(Slot.Location, Slots[TargetSlot].Location) <= 180.0f
        ? ECrowdActiveBehavior::Attack : ECrowdActiveBehavior::Pursue;
  }
  if (SlotIndex <= 16)
    return Slot.Health <= 50 ? ECrowdActiveBehavior::Flee : ECrowdActiveBehavior::Guard;

  const ECrowdActiveBehavior Current = Slot.Facts.ActiveBehavior;
  const FVector CurrentObjective = ChooseObjectiveLocation(SlotIndex, Current);
  if (FVector::Distance(Slot.Location, CurrentObjective) <= 100.0f)
  {
    return Current == ECrowdActiveBehavior::MoveTo
      ? ECrowdActiveBehavior::Wander : ECrowdActiveBehavior::MoveTo;
  }
  return Current == ECrowdActiveBehavior::MoveTo
    ? ECrowdActiveBehavior::MoveTo : ECrowdActiveBehavior::Wander;
}

FVector ACrowdDemoMixedSandboxCoordinator::ChooseObjectiveLocation(
  const int32 SlotIndex,
  const ECrowdActiveBehavior Behavior) const
{
  if (Behavior == ECrowdActiveBehavior::HaulPickup)
    return Marker(TEXT("CrowdNavLower"), FVector::ZeroVector);
  if (Behavior == ECrowdActiveBehavior::HaulDeliver)
    return Marker(TEXT("CrowdNavHigh"), FVector::ZeroVector);
  if (Behavior == ECrowdActiveBehavior::Pursue
    || Behavior == ECrowdActiveBehavior::Attack)
  {
    const FCrowdStableEntityRef Target = ChooseTargetRef(SlotIndex, Behavior);
    const int32 TargetSlot = static_cast<int32>(Target.StableEntityId);
    return Slots.IsValidIndex(TargetSlot)
      ? Slots[TargetSlot].Location : Marker(TEXT("CrowdNavGoal"), FVector::ZeroVector);
  }
  if (Behavior == ECrowdActiveBehavior::Guard)
    return Marker(TEXT("CrowdNavGoal"), FVector::ZeroVector);
  if (Behavior == ECrowdActiveBehavior::Flee
    || Behavior == ECrowdActiveBehavior::Wander)
    return Marker(TEXT("CrowdNavRouteA"), FVector::ZeroVector);
  return Marker(TEXT("CrowdNavRouteB"), FVector::ZeroVector);
}

FCrowdStableEntityRef ACrowdDemoMixedSandboxCoordinator::ChooseTargetRef(
  const int32 SlotIndex,
  const ECrowdActiveBehavior Behavior) const
{
  if (Behavior == ECrowdActiveBehavior::Pursue
    || Behavior == ECrowdActiveBehavior::Attack)
  {
    for (int32 Offset = 0; Offset < 4; ++Offset)
    {
      const int32 Candidate = 13 + ((SlotIndex - 7 + Offset) % 4);
      if (Slots.IsValidIndex(Candidate) && Slots[Candidate].bActive)
        return Slots[Candidate].Facts.StableEntityRef;
    }
  }
  if (Behavior == ECrowdActiveBehavior::Flee)
  {
    for (int32 Candidate = 7; Candidate <= 12; ++Candidate)
      if (Slots[Candidate].bActive) return Slots[Candidate].Facts.StableEntityRef;
  }
  return {};
}

bool ACrowdDemoMixedSandboxCoordinator::RunProductMovementBoundary()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassCrowdRuntimeSubsystem* Runtime =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!EntitySubsystem || !Runtime || !NavGraphHandle.IsValid())
    return false;

  TArray<FCrowdMassBoundaryAgentRecord> Gathered;
  TMap<FCrowdStableEntityRef, FVector> Objectives;
  TMap<FCrowdStableEntityRef,
    TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe>>
    Flows;
  TSet<FCrowdStableEntityRef> MovingRefs;
  TArray<FCrowdMassCommitTarget> Targets;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdMassBoundaryAgentRecord& Record =
      Gathered.AddDefaulted_GetRef();
    Record.Identity.AgentId = SlotIndex;
    Record.Identity.SetStableEntityRef(
      Slot.Facts.StableEntityRef);
    Record.AgentFacts = Slot.Facts;
    Record.State.Position = Slot.Location;
    Record.State.Velocity = FVector::ZeroVector;
    Record.State.YawDegrees = 0.0f;
    Record.State.PlanRevision =
      static_cast<int32>(FixedStepIndex);
    Record.State.bInitialized = true;
    Record.Properties.PhysicalRadiusCm =
      MinimumSafeSeparationCm * 0.5f;
    Record.Properties.HardSafetyGapCm = 0.0f;
    Record.Properties.SoftMarginCm = 0.0f;
    Record.Properties.Mobility = 1.0f;
    Record.Properties.MaximumSpeedCmps = 500.0f;
    Record.Properties.CapabilityProfileKey = 1;
    Targets.Add({
      Slot.Facts.StableEntityRef,
      SlotIndex,
      Slot.Facts.StableEntityRef.LifecycleSerial});

    const ECrowdActiveBehavior Behavior =
      Slot.Facts.ActiveBehavior;
    const FVector Objective =
      ChooseObjectiveLocation(SlotIndex, Behavior);
    const float Distance =
      FVector::Distance(Slot.Location, Objective);
    const bool bInteractionReady =
      (Behavior == ECrowdActiveBehavior::Attack
        && Distance <= 180.0f)
      || ((Behavior == ECrowdActiveBehavior::HaulPickup
        || Behavior == ECrowdActiveBehavior::HaulDeliver)
        && Distance <= 90.0f);
    if (Behavior == ECrowdActiveBehavior::Attack
      || bInteractionReady)
      continue;
    const FCrowdNavSurfaceFlow* IgnoredFlow = nullptr;
    if (!GetOrBuildFlow(Objective, IgnoredFlow))
      return false;
    uint64 GoalNodeId = 0;
    uint32 GoalLayer = 0;
    if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
        *NavGraphHandle, Objective, 350.0f,
        GoalNodeId, GoalLayer))
      return false;
    const FCrowdNavFlowHandle* Handle =
      FlowHandleByGoalNode.Find(GoalNodeId);
    const TSharedPtr<const FCrowdNavSurfaceFlow,
      ESPMode::ThreadSafe> Flow =
      Handle ? Runtime->ResolveFlow(*Handle) : nullptr;
    if (!Flow.IsValid())
      return false;
    MovingRefs.Add(Slot.Facts.StableEntityRef);
    Objectives.Add(Slot.Facts.StableEntityRef, Objective);
    Flows.Add(Slot.Facts.StableEntityRef, Flow);
  }
  FCrowdMassBoundarySnapshot Snapshot;
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    static_cast<int32>(FixedStepIndex),
    static_cast<int32>(FixedStepIndex),
    Gathered,
    Snapshot);
  if (!Snapshot.bValid)
    return false;

  struct FMixedMovementWork
  {
    FCrowdMassCommitPlan Plan;
    int32 SafetyHolds = 0;
    bool bCompleted = false;
  };
  const TSharedRef<FMixedMovementWork, ESPMode::ThreadSafe> Work =
    MakeShared<FMixedMovementWork, ESPMode::ThreadSafe>();
  const TSharedPtr<const FCrowdNavSurfaceGraph,
    ESPMode::ThreadSafe> Graph = NavGraphHandle;
  FCrowdMassBoundaryRunner Runner;
  if (!Runner.Begin(Snapshot, 0.0)
    || !Runner.AddTask(
      {ECrowdBoundaryTaskStage::Movement, 0}, {},
      [Snapshot, Graph, Flows, Objectives, MovingRefs, Work]()
      {
        TArray<FCrowdSpatialSafetyAgent> SafetyAgents;
        SafetyAgents.Reserve(Snapshot.Agents.Num());
        for (const FCrowdMassBoundaryAgentRecord& Agent
          : Snapshot.Agents)
          SafetyAgents.Add({
            Agent.AgentFacts.StableEntityRef,
            Agent.State.Position,
            MinimumSafeSeparationCm * 0.5f});
        FCrowdSpatialSafetyIndex Safety;
        if (!Safety.Build(
            SafetyAgents, MinimumSafeSeparationCm, 150.0f))
          return FCrowdBoundaryTaskResult::Failure();

        FCrowdMassCommitPlan Plan;
        Plan.FixedStepIndex = Snapshot.FixedStepIndex;
        Plan.PlanRevision = Snapshot.PlanRevision;
        uint64 PlanHash = 14695981039346656037ull;
        for (const FCrowdMassBoundaryAgentRecord& Agent
          : Snapshot.Agents)
        {
          FVector Position = Agent.State.Position;
          FVector Velocity = FVector::ZeroVector;
          const FCrowdStableEntityRef Ref =
            Agent.AgentFacts.StableEntityRef;
          if (MovingRefs.Contains(Ref))
          {
            const TSharedPtr<const FCrowdNavSurfaceFlow,
              ESPMode::ThreadSafe>* Flow = Flows.Find(Ref);
            const FVector* Objective = Objectives.Find(Ref);
            if (!Flow || !Flow->IsValid() || !Objective)
              return FCrowdBoundaryTaskResult::Failure();
            uint64 CurrentNodeId = 0;
            uint32 CurrentLayer = 0;
            if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
                *Graph, Position, 350.0f,
                CurrentNodeId, CurrentLayer))
              return FCrowdBoundaryTaskResult::Failure();
            const int32 CurrentIndex =
              Graph->FindNodeIndex(CurrentNodeId);
            if (CurrentIndex == INDEX_NONE
              || !(*Flow)->Nodes.IsValidIndex(CurrentIndex)
              || (*Flow)->Nodes[CurrentIndex].IntegrationCostQ
                == MAX_uint32)
              return FCrowdBoundaryTaskResult::Failure();
            FVector Destination = *Objective;
            const uint64 NextNodeId =
              (*Flow)->Nodes[CurrentIndex].NextStableNodeId;
            const int32 NextIndex =
              Graph->FindNodeIndex(NextNodeId);
            if (NextIndex != INDEX_NONE)
              Destination = Graph->Nodes[NextIndex].Center;
            const FVector Delta = Destination - Position;
            const FVector Candidate = Position
              + Delta.GetClampedToMaxSize(
                500.0f * static_cast<float>(
                  MixedFixedStepSeconds));
            if (Safety.IsCandidateSafe(
                Ref, Candidate,
                MinimumSafeSeparationCm * 0.5f))
            {
              if (!Safety.Update(Ref, Candidate))
                return FCrowdBoundaryTaskResult::Failure();
              Position = Candidate;
              Velocity = Delta.GetSafeNormal()
                * 500.0f;
            }
            else
            {
              ++Work->SafetyHolds;
            }
          }
          FCrowdMassCommitRecord& Record =
            Plan.Records.AddDefaulted_GetRef();
          Record.EntityRef = Ref;
          Record.CapabilityProfileKey = 1;
          Record.PlanRevision = Snapshot.PlanRevision;
          Record.Movement.AgentId = Agent.Identity.AgentId;
          Record.Movement.LifecycleSerial =
            Ref.LifecycleSerial;
          Record.Movement.Position = Position;
          Record.Movement.Velocity = Velocity;
          Record.Movement.YawDegrees = Velocity.IsNearlyZero()
            ? Agent.State.YawDegrees
            : Velocity.Rotation().Yaw;
          uint64 MovementHash =
            FoldMixedHash(14695981039346656037ull,
              Ref.ProviderId);
          MovementHash = FoldMixedHash(
            MovementHash, Ref.StableEntityId);
          MovementHash = FoldMixedHash(
            MovementHash, Ref.LifecycleSerial);
          MovementHash = FoldMixedHash(
            MovementHash,
            static_cast<uint64>(FMath::RoundToInt64(
              Position.X * 10.0)));
          MovementHash = FoldMixedHash(
            MovementHash,
            static_cast<uint64>(FMath::RoundToInt64(
              Position.Y * 10.0)));
          Record.Movement.StableHash = static_cast<uint32>(
            MovementHash ^ (MovementHash >> 32));
          if (Record.Movement.StableHash == 0)
            Record.Movement.StableHash = 1;
          Record.Movement.bValid = true;
          PlanHash = FoldMixedHash(
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
    || !Work->bCompleted
    || !Runner.BuildAndSealCommit(
      Work->Plan, {}, Targets, 0.0))
    return false;

  struct FResolvedWrite
  {
    FSlotState* Slot = nullptr;
    FTransformFragment* Transform = nullptr;
    const FCrowdMassCommitRecord* Record = nullptr;
  };
  TArray<FResolvedWrite> Writes;
  Writes.Reserve(Work->Plan.Records.Num());
  for (const FCrowdMassCommitRecord& Record
    : Work->Plan.Records)
  {
    const int32 SlotIndex =
      static_cast<int32>(Record.EntityRef.StableEntityId);
    FMassEntityHandle Entity;
    if (!Slots.IsValidIndex(SlotIndex)
      || !Slots[SlotIndex].bActive
      || Slots[SlotIndex].Facts.StableEntityRef
        != Record.EntityRef
      || !LifecycleWorld.TryGetEntityHandle(
        Record.EntityRef, Entity))
      return false;
    FTransformFragment* Transform =
      EntitySubsystem->GetMutableEntityManager()
        .GetFragmentDataPtr<FTransformFragment>(Entity);
    if (!Transform)
      return false;
    Writes.Add({&Slots[SlotIndex], Transform, &Record});
  }
  if (!Runner.MarkValidated(0.0))
    return false;
  for (const FResolvedWrite& Write : Writes)
  {
    Write.Slot->Location =
      Write.Record->Movement.Position;
    Write.Transform->GetMutableTransform().SetLocation(
      Write.Record->Movement.Position);
  }
  SafetyHoldCount += Work->SafetyHolds;
  LastBoundaryCommitHash =
    Runner.GetCommitEnvelope().StableHash;
  return Runner.MarkCommitted(0.0);
}

bool ACrowdDemoMixedSandboxCoordinator::GetOrBuildFlow(
  const FVector& Objective,
  const FCrowdNavSurfaceFlow*& OutFlow)
{
  OutFlow = nullptr;
  UWorld* World = GetWorld();
  UMassCrowdRuntimeSubsystem* Runtime =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!Runtime || !NavGraphHandle.IsValid()) return false;
  const FCrowdNavGraphResource& Resource = Runtime->GetNavGraphResource();
  if (!Resource.IsReady() || Resource.Graph != NavGraphHandle) return false;
  uint64 GoalNodeId = 0;
  uint32 GoalLayer = 0;
  if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
    *NavGraphHandle, Objective, 350.0f, GoalNodeId, GoalLayer)) return false;
  if (const FCrowdNavFlowHandle* Existing =
    FlowHandleByGoalNode.Find(GoalNodeId))
  {
    const TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> Flow =
      Runtime->ResolveFlow(*Existing);
    OutFlow = Flow.Get();
    return OutFlow != nullptr;
  }
  FCrowdNavFlowHandle Handle;
  const FCrowdNavFlowKey Key{
    Resource.TopologyRevision, GoalNodeId, 1, GoalLayer};
  if (!Runtime->AcquireFlow(Key, Handle))
    return false;
  FlowHandleByGoalNode.Add(GoalNodeId, Handle);
  const TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> Flow =
    Runtime->ResolveFlow(Handle);
  OutFlow = Flow.Get();
  return OutFlow != nullptr;
}

bool ACrowdDemoMixedSandboxCoordinator::RebuildSpatialSafety()
{
  TArray<FCrowdSpatialSafetyAgent> Agents;
  Agents.Reserve(Slots.Num());
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    Agents.Add({
      Slot.Facts.StableEntityRef,
      Slot.Location,
      MinimumSafeSeparationCm * 0.5f});
  }
  return SpatialSafety.Build(Agents, MinimumSafeSeparationCm, 150.0f);
}

bool ACrowdDemoMixedSandboxCoordinator::BuildLifecycleOperation(
  FCrowdDemoContinuousLifecycleOperation& OutOperation)
{
  OutOperation = {};
  OutOperation.Sequence = NextLifecycleSequence;
  OutOperation.RelevantSetRevision = RelevantSetRevision + 1;
  OutOperation.FixedStepIndex = FixedStepIndex;

  if (PendingRespawnSlot != INDEX_NONE)
  {
    const FSlotState& Slot = Slots[PendingRespawnSlot];
    OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Spawn;
    OutOperation.StableEntityId = PendingRespawnSlot;
    OutOperation.LifecycleSerial = Slot.Facts.StableEntityRef.LifecycleSerial + 1;
    OutOperation.NewMembershipKey = MembershipForBehavior(
      MakeAgentFacts(PendingRespawnSlot, OutOperation.LifecycleSerial).ActiveBehavior);
    return true;
  }
  if (PendingCombatDeathSlot != INDEX_NONE && Slots[PendingCombatDeathSlot].bActive)
  {
    const FSlotState& Slot = Slots[PendingCombatDeathSlot];
    OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Despawn;
    OutOperation.StableEntityId = PendingCombatDeathSlot;
    OutOperation.LifecycleSerial = Slot.Facts.StableEntityRef.LifecycleSerial;
    OutOperation.DespawnReason = static_cast<uint8>(ECrowdDespawnReason::Death);
    return true;
  }
  for (int32 Offset = 0; Offset < Config.PopulationLimit; ++Offset)
  {
    const int32 Candidate = 1 + ((MembershipCursor - 1 + Offset) % Config.PopulationLimit);
    const FSlotState& Slot = Slots[Candidate];
    const uint32 Desired = MembershipForBehavior(Slot.Facts.ActiveBehavior);
    if (Slot.bActive && Slot.MembershipKey != Desired)
    {
      OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Membership;
      OutOperation.StableEntityId = Candidate;
      OutOperation.LifecycleSerial = Slot.Facts.StableEntityRef.LifecycleSerial;
      OutOperation.PreviousMembershipKey = Slot.MembershipKey;
      OutOperation.NewMembershipKey = Desired;
      MembershipCursor = 1 + (Candidate % Config.PopulationLimit);
      return true;
    }
  }

  for (int32 Offset = 0; Offset < 4; ++Offset)
  {
    const int32 Candidate = 17 + ((RecycleCursor - 17 + Offset) % 4);
    if (Slots[Candidate].bActive)
    {
      OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Despawn;
      OutOperation.StableEntityId = Candidate;
      OutOperation.LifecycleSerial = Slots[Candidate].Facts.StableEntityRef.LifecycleSerial;
      OutOperation.DespawnReason = static_cast<uint8>(ECrowdDespawnReason::BusinessRecycle);
      RecycleCursor = 17 + ((Candidate - 16) % 4);
      return true;
    }
  }
  return false;
}

bool ACrowdDemoMixedSandboxCoordinator::ApplyLifecycleOperation(
  const FCrowdDemoContinuousLifecycleOperation& Operation)
{
  if (Operation.StableEntityId == 0
    || Operation.StableEntityId >= static_cast<uint64>(Slots.Num())) return false;
  const int32 SlotIndex = static_cast<int32>(Operation.StableEntityId);
  FSlotState& Slot = Slots[SlotIndex];
  const FCrowdStableEntityRef Ref{1, Operation.StableEntityId, Operation.LifecycleSerial};
  const FCrowdLifecycleBatchHeader Header = MakeBatchHeader(Operation);
  ECrowdLifecycleBatchAcceptResult Result = ECrowdLifecycleBatchAcceptResult::RejectedInvalid;

  if (Operation.Kind == ECrowdDemoContinuousLifecycleOperationKind::Spawn)
  {
    FCrowdSpawnBatch Batch;
    Batch.Header = Header;
    Batch.Entries.Add({MakeAgentFacts(SlotIndex, Operation.LifecycleSerial), Operation.NewMembershipKey});
    FCrowdLifecycleBatchTransport::Finalize(Batch);
    Result = LifecycleWorld.ApplyAtBoundary(Batch);
    if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
    {
      InitializeSlotState(SlotIndex, Operation.LifecycleSerial);
      Slot.MembershipKey = Operation.NewMembershipKey;
      PendingRespawnSlot = INDEX_NONE;
      ++SpawnCount;
    }
  }
  else if (Operation.Kind == ECrowdDemoContinuousLifecycleOperationKind::Despawn)
  {
    if (Operation.DespawnReason >= static_cast<uint8>(ECrowdDespawnReason::Count)) return false;
    FCrowdDespawnBatch Batch;
    Batch.Header = Header;
    Batch.Entries.Add({Ref, static_cast<ECrowdDespawnReason>(Operation.DespawnReason)});
    FCrowdLifecycleBatchTransport::Finalize(Batch);
    Result = LifecycleWorld.ApplyAtBoundary(Batch);
    if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
    {
      Slot.bActive = false;
      PendingRespawnSlot = SlotIndex;
      if (PendingCombatDeathSlot == SlotIndex) PendingCombatDeathSlot = INDEX_NONE;
      ++DespawnCount;
    }
  }
  else if (Operation.Kind == ECrowdDemoContinuousLifecycleOperationKind::Membership)
  {
    FCrowdMembershipBatch Batch;
    Batch.Header = Header;
    Batch.Entries.Add({Ref, Operation.PreviousMembershipKey, Operation.NewMembershipKey});
    FCrowdLifecycleBatchTransport::Finalize(Batch);
    Result = LifecycleWorld.ApplyAtBoundary(Batch);
    if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
    {
      Slot.MembershipKey = Operation.NewMembershipKey;
      ++MembershipChangeCount;
    }
  }
  if (Result != ECrowdLifecycleBatchAcceptResult::Accepted) return false;

  NextLifecycleSequence = Operation.Sequence + 1;
  RelevantSetRevision = Operation.RelevantSetRevision;
  MaxObservedPopulation = FMath::Max(
    MaxObservedPopulation, LifecycleWorld.GetActiveEntityCount());
  if (LifecycleWorld.GetActiveEntityCount() > Config.PopulationLimit) return false;
  bVisualSyncPending = !HasAuthority();
  return true;
}

FCrowdLifecycleBatchHeader ACrowdDemoMixedSandboxCoordinator::MakeBatchHeader(
  const FCrowdDemoContinuousLifecycleOperation& Operation) const
{
  FCrowdLifecycleBatchHeader Header;
  Header.BaseSnapshotRevision = Config.SnapshotRevision;
  Header.FixedStepIndex = Operation.FixedStepIndex;
  Header.RelevantSetRevision = Operation.RelevantSetRevision;
  Header.Sequence = Operation.Sequence;
  return Header;
}

void ACrowdDemoMixedSandboxCoordinator::PublishProductStateFrame()
{
  TArray<FCrowdDemoMixedAgentState> States;
  TArray<FCrowdReliableStateRecord> Records;
  TArray<FCrowdMovementCorrectionRecord> Corrections;
  States.Reserve(Config.PopulationLimit);
  Records.Reserve(Config.PopulationLimit);
  Corrections.Reserve(Config.PopulationLimit);
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    States.Add({
      static_cast<uint64>(SlotIndex),
      Slot.Facts.StableEntityRef.LifecycleSerial,
      Slot.MembershipKey,
      Slot.Location,
      static_cast<uint8>(Slot.Facts.ActiveBehavior),
      static_cast<uint8>(FMath::Clamp(Slot.Health, 0, 100)),
      Slot.Facts.TargetRef.ProviderId,
      Slot.Facts.TargetRef.StableEntityId,
      Slot.Facts.TargetRef.LifecycleSerial,
      Slot.Facts.BusinessTaskRef.ProviderId,
      Slot.Facts.BusinessTaskRef.StableEntityId,
      Slot.Facts.BusinessTaskRef.LifecycleSerial});
  }
  for (const FCrowdDemoMixedAgentState& State : States)
  {
    FCrowdReliableStateRecord& Record =
      Records.AddDefaulted_GetRef();
    Record.Sequence = NextStateSequence++;
    Record.Kind = ECrowdReliableStateKind::Behavior;
    Record.EntityRef = {
      1, State.StableEntityId, State.LifecycleSerial};
    Record.Revision = static_cast<uint32>(
      FMath::Max<int64>(1, FixedStepIndex));
    EncodeMixedAgent(
      State, FixedStepIndex, NextLifecycleSequence,
      RelevantSetRevision, Record.Payload);
    Record.StableHash =
      FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
    FCrowdMovementCorrectionRecord& Correction =
      Corrections.AddDefaulted_GetRef();
    Correction.EntityRef = Record.EntityRef;
    Correction.Sequence = static_cast<uint64>(
      FMath::Max<int64>(1, FixedStepIndex + 1));
    Correction.FixedStepIndex = FixedStepIndex;
    Correction.Position = State.Location;
    Correction.StableHash =
      FCrowdReplicationTransport::CalculateMovementCorrectionHash(Correction);
  }
  for (TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
    : ReplicationChannels)
  {
    if (AMassCrowdReplicationActor* Channel = Pair.Value.Get())
    {
      Channel->PublishReliables(Records);
      Channel->PublishMovementCorrections(Corrections);
    }
  }
}

void ACrowdDemoMixedSandboxCoordinator::RefreshReplicationChannels()
{
  UWorld* World = GetWorld();
  if (!World) return;
  for (auto It = ReplicationChannels.CreateIterator(); It; ++It)
  {
    AMassCrowdReplicationActor* Channel = It.Value().Get();
    if (!It.Key().IsValid() || !Channel)
    {
      It.RemoveCurrent();
      continue;
    }
    if (Channel->RequiresNewBaseline())
    {
      Channel->Destroy();
      It.RemoveCurrent();
    }
  }
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
    It; ++It)
  {
    APlayerController* Controller = It->Get();
    if (!Controller || ReplicationChannels.Contains(Controller)) continue;
    AMassCrowdReplicationActor* Channel =
      AMassCrowdReplicationActor::SpawnForController(*Controller);
    if (!Channel || !PublishBaseline(*Channel))
    {
      if (Channel) Channel->Destroy();
      continue;
    }
    ReplicationChannels.Add(Controller, Channel);
  }
}

bool ACrowdDemoMixedSandboxCoordinator::PublishBaseline(
  AMassCrowdReplicationActor& Channel)
{
  TArray<FCrowdRelevantSnapshotEntityPayload> Entities;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdDemoMixedAgentState State{
      static_cast<uint64>(SlotIndex),
      Slot.Facts.StableEntityRef.LifecycleSerial,
      Slot.MembershipKey,
      Slot.Location,
      static_cast<uint8>(Slot.Facts.ActiveBehavior),
      static_cast<uint8>(FMath::Clamp(Slot.Health, 0, 100)),
      Slot.Facts.TargetRef.ProviderId,
      Slot.Facts.TargetRef.StableEntityId,
      Slot.Facts.TargetRef.LifecycleSerial,
      Slot.Facts.BusinessTaskRef.ProviderId,
      Slot.Facts.BusinessTaskRef.StableEntityId,
      Slot.Facts.BusinessTaskRef.LifecycleSerial};
    EncodeMixedAgent(
      State, FixedStepIndex, NextLifecycleSequence,
      RelevantSetRevision,
      Entities.AddDefaulted_GetRef().Bytes);
  }
  FCrowdRelevantSnapshotLimits Limits;
  Limits.MaxEntityCount = Config.PopulationLimit;
  Limits.MaxChunkCount = Config.PopulationLimit;
  Limits.MaxEntitiesPerChunk = 8;
  Limits.MaxChunkPayloadBytes = 4096;
  Limits.MaxTotalPayloadBytes = 64 * 1024;
  Limits.AssemblyTimeoutSeconds = 10.0;
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  if (!FCrowdRelevantSnapshotTransport::Build(
    FMath::Max(1u, Config.SnapshotRevision),
    FixedStepIndex,
    RelevantSetRevision,
    Entities,
    Limits,
    Header,
    Chunks))
    return false;
  return Channel.PublishBaseline(
    Header, Chunks, FMath::Max<uint64>(1, NextStateSequence));
}

void ACrowdDemoMixedSandboxCoordinator::ConsumeProductReplication()
{
  UWorld* World = GetWorld();
  if (!World) return;
  AMassCrowdReplicationActor* Channel = nullptr;
  for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
  {
    if (*It && !It->HasAuthority())
    {
      Channel = *It;
      break;
    }
  }
  if (!Channel || !Channel->IsReady()) return;
  if (Channel->GetCompletedBaselineRevision()
    != LastConsumedBaselineRevision)
  {
    UMassEntitySubsystem* EntitySubsystem =
      World->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem || !LifecycleArchetype.IsValid())
    {
      ++StaleRejectCount;
      return;
    }
    TArray<FCrowdLifecycleSnapshotEntity> Snapshot;
    int64 BaselineStep = INDEX_NONE;
    uint64 LifecycleResume = 0;
    uint32 BaselineRelevantRevision = 0;
    for (FSlotState& Slot : Slots) Slot.bActive = false;
    for (const FCrowdRelevantSnapshotEntityPayload& Entity
      : Channel->GetCompletedBaselineEntities())
    {
      FCrowdDemoMixedAgentState State;
      int64 Step = 0;
      uint64 EntityLifecycleResume = 0;
      uint32 EntityRelevantRevision = 0;
      if (!DecodeMixedAgent(
          Entity.Bytes, State, Step,
          EntityLifecycleResume, EntityRelevantRevision)
        || State.StableEntityId == 0
        || State.StableEntityId >= static_cast<uint64>(Slots.Num())
        || State.Behavior >= static_cast<uint8>(
          ECrowdActiveBehavior::Count)
        || (BaselineStep != INDEX_NONE
          && (BaselineStep != Step
            || LifecycleResume != EntityLifecycleResume
            || BaselineRelevantRevision != EntityRelevantRevision)))
      {
        ++StaleRejectCount;
        return;
      }
      if (BaselineStep == INDEX_NONE)
      {
        BaselineStep = Step;
        LifecycleResume = EntityLifecycleResume;
        BaselineRelevantRevision = EntityRelevantRevision;
      }
      const int32 SlotIndex = static_cast<int32>(State.StableEntityId);
      FSlotState& Slot = Slots[SlotIndex];
      Slot.Facts = MakeAgentFacts(SlotIndex, State.LifecycleSerial);
      Slot.Facts.ActiveBehavior =
        static_cast<ECrowdActiveBehavior>(State.Behavior);
      Slot.Facts.TargetRef = {
        State.TargetProviderId,
        State.TargetStableEntityId,
        State.TargetLifecycleSerial};
      Slot.Facts.BusinessTaskRef = {
        State.TaskProviderId,
        State.TaskStableEntityId,
        State.TaskLifecycleSerial};
      Slot.Location = State.Location;
      Slot.MembershipKey = State.MembershipKey;
      Slot.Health = State.Health;
      Slot.bActive = true;
      Snapshot.Add({Slot.Facts, Slot.MembershipKey});
    }
    if (BaselineStep < 0 || LifecycleResume == 0
      || BaselineRelevantRevision == 0)
    {
      ++StaleRejectCount;
      return;
    }
    Snapshot.Sort([](const auto& A, const auto& B)
    {
      return A.AgentFacts.StableEntityRef < B.AgentFacts.StableEntityRef;
    });
    FCrowdLifecycleBatchLimits LifecycleLimits;
    LifecycleLimits.MaxSnapshotEntities = Config.PopulationLimit;
    LifecycleLimits.MaxEntriesPerBatch = Config.PopulationLimit;
    LifecycleLimits.MaxTrackedSlots = Config.PopulationLimit;
    LifecycleLimits.MaxSequenceHistory = 256;
    if (bPresentationProfileRegistered)
    {
      UMassCrowdPresentationSubsystem* Presentation =
        World->GetSubsystem<UMassCrowdPresentationSubsystem>();
      if (!Presentation || !Presentation->ResetProfile(1))
      {
        ++StaleRejectCount;
        return;
      }
      PresentedEntitiesByStableId.Reset();
    }
    LifecycleWorld.Reset();
    if (!LifecycleWorld.InitializeFromSnapshot(
      EntitySubsystem->GetMutableEntityManager(),
      LifecycleArchetype,
      Channel->GetCompletedBaselineRevision(),
      BaselineStep,
      BaselineRelevantRevision,
      Snapshot,
      LifecycleLimits,
      LifecycleResume))
    {
      ++StaleRejectCount;
      return;
    }
    for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
    {
      if (!Slots[SlotIndex].bActive) continue;
      FMassEntityHandle EntityHandle;
      if (!LifecycleWorld.TryGetEntityHandle(
          Slots[SlotIndex].Facts.StableEntityRef, EntityHandle))
      {
        ++StaleRejectCount;
        return;
      }
      FTransformFragment* Transform =
        EntitySubsystem->GetMutableEntityManager()
          .GetFragmentDataPtr<FTransformFragment>(EntityHandle);
      if (!Transform)
      {
        ++StaleRejectCount;
        return;
      }
      Transform->GetMutableTransform().SetLocation(
        Slots[SlotIndex].Location);
    }
    LastConsumedBaselineRevision = Channel->GetCompletedBaselineRevision();
    LastReceivedFixedStep = BaselineStep;
    NextLifecycleSequence = LifecycleResume;
    RelevantSetRevision = BaselineRelevantRevision;
  }

  TArray<FCrowdReplicationApplyFrame> ApplyFrames;
  if (!Channel->DrainClientApplyFrames(ApplyFrames))
  {
    if (Channel->GetClientState().RequiresResync())
      ++StaleRejectCount;
    return;
  }
  for (const FCrowdReplicationApplyFrame& Frame : ApplyFrames)
  {
    if (Frame.Kind == ECrowdReplicationApplyFrameKind::ReliableState)
    {
      for (const FCrowdReliableStateRecord& Record : Frame.ReliableRecords)
      {
        if (Record.Kind == ECrowdReliableStateKind::Behavior)
        {
          FCrowdDemoMixedAgentState State;
          int64 Step = 0;
          uint64 IgnoredLifecycleResume = 0;
          uint32 IgnoredRelevantRevision = 0;
          if (!DecodeMixedAgent(
              Record.Payload, State, Step,
              IgnoredLifecycleResume, IgnoredRelevantRevision)
            || !ApplyReplicatedAgentState(State, Step))
          {
            ++StaleRejectCount;
            return;
          }
        }
        else if (Record.Kind == ECrowdReliableStateKind::Spawn
          || Record.Kind == ECrowdReliableStateKind::Despawn
          || Record.Kind == ECrowdReliableStateKind::Membership)
        {
          FCrowdDemoContinuousLifecycleOperation Operation;
          if (!DecodeLifecycleOperation(Record.Payload, Operation)
            || !ApplyLifecycleOperation(Operation))
          {
            ++StaleRejectCount;
            return;
          }
        }
        else
        {
          ++StaleRejectCount;
          return;
        }
        LastReceivedStateSequence = Record.Sequence;
      }
    }
    else if (Frame.Kind
      == ECrowdReplicationApplyFrameKind::MovementCorrection)
    {
      for (const FCrowdMovementCorrectionRecord& Correction
        : Frame.Corrections)
      {
        const int32 SlotIndex = static_cast<int32>(
          Correction.EntityRef.StableEntityId);
        if (Slots.IsValidIndex(SlotIndex)
          && Slots[SlotIndex].Facts.StableEntityRef
            == Correction.EntityRef)
        {
          Slots[SlotIndex].Location = Correction.Position;
        }
      }
    }
  }
  LastExpectedEntitySetHash = LifecycleWorld.CalculateEntitySetHash();
  LastExpectedMembershipHash = LifecycleWorld.CalculateMembershipHash();
  bVisualSyncPending = true;
}

bool ACrowdDemoMixedSandboxCoordinator::ApplyReplicatedAgentState(
  const FCrowdDemoMixedAgentState& State,
  const int64 InFixedStepIndex)
{
  if (State.StableEntityId == 0
    || State.StableEntityId >= static_cast<uint64>(Slots.Num())
    || State.Behavior >= static_cast<uint8>(ECrowdActiveBehavior::Count))
  {
    if (!bClientApplyFailureLogged)
    {
      bClientApplyFailureLogged = true;
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoMixedSandboxClientApplyReject reason=invalid_payload entity=%llu lifecycle=%u behavior=%u step=%lld slots=%d"),
        State.StableEntityId, State.LifecycleSerial, State.Behavior,
        InFixedStepIndex, Slots.Num());
    }
    return false;
  }
  FSlotState& Slot = Slots[static_cast<int32>(State.StableEntityId)];
  if (!Slot.bActive
    || Slot.Facts.StableEntityRef.LifecycleSerial != State.LifecycleSerial)
  {
    if (!bClientApplyFailureLogged)
    {
      bClientApplyFailureLogged = true;
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoMixedSandboxClientApplyReject reason=lifecycle entity=%llu incoming=%u local=%u active=%d step=%lld last_step=%lld"),
        State.StableEntityId, State.LifecycleSerial,
        Slot.Facts.StableEntityRef.LifecycleSerial,
        Slot.bActive ? 1 : 0, InFixedStepIndex, LastReceivedFixedStep);
    }
    return false;
  }
  Slot.Location = State.Location;
  Slot.MembershipKey = State.MembershipKey;
  Slot.Facts.ActiveBehavior =
    static_cast<ECrowdActiveBehavior>(State.Behavior);
  Slot.Facts.TargetRef = {
    State.TargetProviderId,
    State.TargetStableEntityId,
    State.TargetLifecycleSerial};
  Slot.Facts.BusinessTaskRef = {
    State.TaskProviderId,
    State.TaskStableEntityId,
    State.TaskLifecycleSerial};
  Slot.Health = State.Health;
  SeenBehaviorBits |= BehaviorBit(Slot.Facts.ActiveBehavior);
  LastReceivedFixedStep = FMath::Max(LastReceivedFixedStep, InFixedStepIndex);
  const bool bApplied = LifecycleWorld.ApplyAgentFactsCorrectionAtBoundary(
    FMath::Max(InFixedStepIndex, LifecycleWorld.GetLastAppliedFixedStep()),
    Slot.Facts);
  if (!bApplied && !bClientApplyFailureLogged)
  {
    bClientApplyFailureLogged = true;
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxClientApplyReject reason=runtime_facts entity=%llu lifecycle=%u behavior=%u well_formed=%d step=%lld world_step=%lld"),
      State.StableEntityId, State.LifecycleSerial, State.Behavior,
      Slot.Facts.IsWellFormed() ? 1 : 0, InFixedStepIndex,
      LifecycleWorld.GetLastAppliedFixedStep());
  }
  return bApplied;
}

void ACrowdDemoMixedSandboxCoordinator::SyncClientVisualsIncremental()
{
  UWorld* World = GetWorld();
  ACrowdDemoReplicator* Replicator = World ? FindMixedVisualHost(*World) : nullptr;
  UMassCrowdPresentationSubsystem* Presentation = World
    ? World->GetSubsystem<UMassCrowdPresentationSubsystem>() : nullptr;
  UInstancedStaticMeshComponent* Instances = Replicator
    ? Replicator->GetCrowdInstancesForClientVisuals() : nullptr;
  UInstancedStaticMeshComponent* HitFlash = Replicator
    ? Replicator->GetCrowdHitFlashInstancesForClientVisuals() : nullptr;
  if (!Replicator || !Presentation || !Instances || !HitFlash) return;

  if (!bClientVisualsInitialized)
  {
    Replicator->ClearCrowdVisualInstances();
    const TSharedRef<FCrowdDemoIsmPresentationSink> Sink =
      MakeShared<FCrowdDemoIsmPresentationSink>(*Instances, *HitFlash);
    if (!Presentation->RegisterProfile(1, Sink))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=presentation_profile"));
      ++StaleRejectCount;
      return;
    }
    bPresentationProfileRegistered = true;
    bClientVisualsInitialized = true;
  }
  const uint64 Sequence = ++PresentationSequence;
  TArray<FCrowdPresentationOperation> Operations;
  TMap<uint64, FCrowdStableEntityRef> NextPresented;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    const uint64 StableId =
      Slot.Facts.StableEntityRef.StableEntityId;
    if (const FCrowdStableEntityRef* Presented =
      PresentedEntitiesByStableId.Find(StableId);
      Presented
      && (!Slot.bActive
        || *Presented != Slot.Facts.StableEntityRef))
    {
      FCrowdPresentationOperation& Despawn =
        Operations.AddDefaulted_GetRef();
      Despawn.Kind =
        ECrowdPresentationOperationKind::Despawn;
      Despawn.EntityRef = *Presented;
      Despawn.ProfileKey = 1;
      Despawn.Sequence = Sequence;
    }
    if (Slot.bActive)
    {
      FCrowdPresentationState State;
      State.EntityRef = Slot.Facts.StableEntityRef;
      State.Transform = FTransform(
        FRotator::ZeroRotator,
        Slot.Location + FVector(0, 0, 45),
        FVector(34));
      State.ProfileKey = 1;
      State.VisualState =
        Slot.Facts.ActiveBehavior == ECrowdActiveBehavior::Attack ? 1 : 0;
      if (Slot.Facts.ActiveBehavior == ECrowdActiveBehavior::HaulDeliver)
        State.CargoRef = Slot.Facts.BusinessTaskRef;
      State.Sequence = Sequence;
      State.SampleServerSeconds = FixedStepIndex * MixedFixedStepSeconds;
      FCrowdPresentationOperation& Operation =
        Operations.AddDefaulted_GetRef();
      Operation.Kind =
        PresentedEntitiesByStableId.Contains(StableId)
          && PresentedEntitiesByStableId.FindChecked(
            StableId) == State.EntityRef
          ? ECrowdPresentationOperationKind::Update
          : ECrowdPresentationOperationKind::Spawn;
      Operation.State = State;
      Operation.EntityRef = State.EntityRef;
      Operation.ProfileKey = 1;
      Operation.Sequence = Sequence;
      NextPresented.Add(StableId, State.EntityRef);
    }
  }
  FCrowdPreparedPresentationFrame PreparedFrame;
  uint64 SourceFrameHash =
    LifecycleWorld.CalculateEntitySetHash();
  SourceFrameHash = FoldMixedHash(
    SourceFrameHash, LastReceivedStateSequence);
  if (!Presentation->PrepareFrame(
      SourceFrameHash == 0 ? 1 : SourceFrameHash,
      Operations, PreparedFrame)
    || !Presentation->ApplyPreparedFrame(PreparedFrame))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=presentation_frame operations=%d"),
      Operations.Num());
    ++StaleRejectCount;
    return;
  }
  PresentedEntitiesByStableId = MoveTemp(NextPresented);
  if (Instances->GetInstanceCount() != LifecycleWorld.GetActiveEntityCount()
    || HitFlash->GetInstanceCount() != LifecycleWorld.GetActiveEntityCount())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=visual_count active=%d visual=%d flash=%d"),
      LifecycleWorld.GetActiveEntityCount(), Instances->GetInstanceCount(), HitFlash->GetInstanceCount());
    ++StaleRejectCount;
    return;
  }
  bVisualSyncPending = false;
}

void ACrowdDemoMixedSandboxCoordinator::LogCheckpoint()
{
  UWorld* World = GetWorld();
  const ACrowdDemoReplicator* Replicator = World ? FindMixedVisualHost(*World) : nullptr;
  const int32 Visible = !HasAuthority() && Replicator
    ? Replicator->GetCrowdVisualInstanceCount() : 0;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoMixedSandboxCheckpoint role=%s fixed_step=%lld state_sequence=%llu active=%d visible=%d transitions=%d pickups=%d deliveries=%d combat_quantity=%d commits=%d duplicate_commits=%d spawned=%d despawned=%d membership=%d max_population=%d safety_holds=%d min_separation_cm=%.2f stale_reject=%d entity_hash=%llu membership_hash=%llu commit_hash=%llu source=MassCrowdBoundaryRunner+MassCrowdNavRuntime+ApplyFrame"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    HasAuthority() ? FixedStepIndex : LastReceivedFixedStep,
    HasAuthority() ? NextStateSequence - 1 : LastReceivedStateSequence,
    LifecycleWorld.GetActiveEntityCount(), Visible, BehaviorTransitionCount,
    BusinessLedger.GetPickupCount(), BusinessLedger.GetDeliveryCount(),
    BusinessLedger.GetCombatHitQuantity(13) + BusinessLedger.GetCombatHitQuantity(14)
      + BusinessLedger.GetCombatHitQuantity(15) + BusinessLedger.GetCombatHitQuantity(16),
    BusinessLedger.GetAppliedCommitCount(), DuplicateCommitCount,
    SpawnCount, DespawnCount, MembershipChangeCount, MaxObservedPopulation,
    SafetyHoldCount, MinimumSeparationCm, StaleRejectCount,
    LifecycleWorld.CalculateEntitySetHash(),
    LifecycleWorld.CalculateMembershipHash(),
    LastBoundaryCommitHash);
}

void ACrowdDemoMixedSandboxCoordinator::TryLogPass()
{
  if (HasAuthority() && !bServerPassLogged && FixedStepIndex >= 600)
  {
    const uint32 RequiredBehaviors =
      BehaviorBit(ECrowdActiveBehavior::HaulPickup)
      | BehaviorBit(ECrowdActiveBehavior::HaulDeliver)
      | BehaviorBit(ECrowdActiveBehavior::Pursue)
      | BehaviorBit(ECrowdActiveBehavior::Attack)
      | BehaviorBit(ECrowdActiveBehavior::Guard)
      | BehaviorBit(ECrowdActiveBehavior::Flee)
      | BehaviorBit(ECrowdActiveBehavior::Wander)
      | BehaviorBit(ECrowdActiveBehavior::MoveTo);
    const int32 CombatQuantity = BusinessLedger.GetCombatHitQuantity(13)
      + BusinessLedger.GetCombatHitQuantity(14)
      + BusinessLedger.GetCombatHitQuantity(15)
      + BusinessLedger.GetCombatHitQuantity(16);
    const bool bPassed = LifecycleWorld.GetActiveEntityCount() == Config.PopulationLimit
      && MaxObservedPopulation == Config.PopulationLimit
      && SpawnCount > 0 && DespawnCount > 0 && MembershipChangeCount > 0
      && BehaviorTransitionCount > 0
      && BusinessLedger.GetPickupCount() > 0
      && BusinessLedger.GetDeliveryCount() > 0
      && CombatQuantity > 0
      && DuplicateCommitCount == BusinessLedger.GetAppliedCommitCount()
      && (SeenBehaviorBits & RequiredBehaviors) == RequiredBehaviors
      && MinimumSeparationCm >= MinimumSafeSeparationCm - 0.5f
      && StaleRejectCount == 0;
    if (bPassed)
    {
      bServerPassLogged = true;
      UE_LOG(LogTemp, Display,
        TEXT("PASS CrowdDemoMixedSandbox role=server fixed_step=%lld active=%d transitions=%d pickups=%d deliveries=%d combat_quantity=%d commits=%d duplicate_commits=%d spawned=%d despawned=%d membership=%d max_population=%d safety_holds=%d min_separation_cm=%.2f fixed_step_ms_p95=%.3f entity_hash=%llu membership_hash=%llu topology_hash=%llu commit_hash=%llu source=MassCrowdBoundaryRunner+MassCrowdNavRuntime+ApplyFrame"),
        FixedStepIndex, LifecycleWorld.GetActiveEntityCount(), BehaviorTransitionCount,
        BusinessLedger.GetPickupCount(), BusinessLedger.GetDeliveryCount(), CombatQuantity,
        BusinessLedger.GetAppliedCommitCount(), DuplicateCommitCount,
        SpawnCount, DespawnCount, MembershipChangeCount, MaxObservedPopulation,
        SafetyHoldCount, MinimumSeparationCm, Percentile95(ServerStepMilliseconds),
        LifecycleWorld.CalculateEntitySetHash(), LifecycleWorld.CalculateMembershipHash(),
        Config.NavTopologyHash,
        LastBoundaryCommitHash);
    }
  }

  if (!HasAuthority() && !bClientPassLogged && LastReceivedFixedStep >= 600
    && bClientVisualsInitialized && !bVisualSyncPending)
  {
    UWorld* World = GetWorld();
    ACrowdDemoReplicator* Replicator = World ? FindMixedVisualHost(*World) : nullptr;
    const int32 Visible = Replicator ? Replicator->GetCrowdVisualInstanceCount() : 0;
    const bool bPassed = Visible == LifecycleWorld.GetActiveEntityCount()
      && LifecycleWorld.GetActiveEntityCount() == Config.PopulationLimit
      && LifecycleWorld.CalculateEntitySetHash() == LastExpectedEntitySetHash
      && LifecycleWorld.CalculateMembershipHash() == LastExpectedMembershipHash
      && StaleRejectCount == 0;
    if (bPassed)
    {
      bClientPassLogged = true;
      UE_LOG(LogTemp, Display,
        TEXT("PASS CrowdDemoMixedSandbox role=client fixed_step=%lld active=%d visible=%d state_sequence=%llu client_frame_ms_p95=%.3f entity_hash=%llu membership_hash=%llu topology_hash=%llu source=LifecycleBehaviorSurfaceFlow"),
        LastReceivedFixedStep, LifecycleWorld.GetActiveEntityCount(), Visible,
        LastReceivedStateSequence, Percentile95(ClientFrameMilliseconds),
        LifecycleWorld.CalculateEntitySetHash(), LifecycleWorld.CalculateMembershipHash(),
        Config.NavTopologyHash);
      if (bCaptureRequested && !bCaptureCompleted && World)
      {
        for (TActorIterator<ACameraActor> It(World); It; ++It)
        {
          if (It->ActorHasTag(TEXT("CrowdNavAcceptanceCamera")))
          {
            if (APlayerController* Controller = World->GetFirstPlayerController())
              Controller->SetViewTarget(*It);
            break;
          }
        }
        CaptureAtWorldSeconds = World->GetTimeSeconds() + 1.0;
      }
    }
  }
}

FVector ACrowdDemoMixedSandboxCoordinator::Marker(
  const FName Tag,
  const FVector& Fallback) const
{
  const FVector* Found = MarkerLocations.Find(Tag);
  return Found ? *Found : Fallback;
}

uint32 ACrowdDemoMixedSandboxCoordinator::MembershipForBehavior(
  const ECrowdActiveBehavior Behavior) const
{
  switch (Behavior)
  {
  case ECrowdActiveBehavior::HaulPickup: return 1;
  case ECrowdActiveBehavior::HaulDeliver: return 2;
  case ECrowdActiveBehavior::Pursue: return 3;
  case ECrowdActiveBehavior::Attack: return 4;
  case ECrowdActiveBehavior::Guard: return 5;
  case ECrowdActiveBehavior::Flee: return 6;
  case ECrowdActiveBehavior::Wander: return 7;
  case ECrowdActiveBehavior::MoveTo: return 8;
  default: return 9;
  }
}

double ACrowdDemoMixedSandboxCoordinator::Percentile95(TArray<double> Values)
{
  if (Values.IsEmpty()) return 0.0;
  Values.Sort();
  const int32 Index = FMath::Clamp(
    FMath::CeilToInt(Values.Num() * 0.95) - 1, 0, Values.Num() - 1);
  return Values[Index];
}
