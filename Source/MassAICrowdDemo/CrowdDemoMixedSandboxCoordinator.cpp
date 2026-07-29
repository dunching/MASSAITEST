#include "CrowdDemoMixedSandboxCoordinator.h"

#include "Camera/CameraActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CrowdDemoBehaviorSourceProvider.h"
#include "CrowdDemoReplicator.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HighResScreenshot.h"
#include "MassCommonFragments.h"
#include "Mass/CrowdDemoPresentationAdapter.h"
#include "MassCrowdPresentationSubsystem.h"
#include "MassCrowdBoundaryWorkGraph.h"
#include "MassCrowdFacingFinalizeWork.h"
#include "MassCrowdMovementFinalizeWork.h"
#include "MassCrowdMovementPipelineWork.h"
#include "MassCrowdParticlePipelineWork.h"
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
  constexpr int32 DefaultMixedPopulation = 20;
  constexpr int32 MaximumMixedPopulation = 500;
  constexpr int32 LifecycleIntervalSteps = 45;
  constexpr float MixedStartDelaySeconds = 5.0f;
  constexpr float MinimumSafeSeparationCm = 70.0f;
  constexpr float MixedInteractionRadiusCm = 900.0f;
  constexpr float MixedScaleInteractionRadiusCm = 2000.0f;

  double DistanceSquaredToNavNode(
    const FVector& Location,
    const FCrowdNavSurfaceNode& Node)
  {
    const FVector Normal =
      Node.SurfaceNormal.GetSafeNormal();
    const double PlaneDistance =
      FVector::DotProduct(
        Location - Node.Vertices[0], Normal);
    const FVector Projected =
      Location - Normal * PlaneDistance;
    bool bInside = true;
    double WindingSign = 0.0;
    for (int32 Index = 0;
      Index < Node.Vertices.Num(); ++Index)
    {
      const FVector& A = Node.Vertices[Index];
      const FVector& B =
        Node.Vertices[
          (Index + 1) % Node.Vertices.Num()];
      const double Side = FVector::DotProduct(
        FVector::CrossProduct(B - A, Projected - A),
        Normal);
      if (FMath::Abs(Side)
        <= UE_DOUBLE_KINDA_SMALL_NUMBER)
        continue;
      if (WindingSign == 0.0)
        WindingSign = Side;
      else if ((Side > 0.0)
        != (WindingSign > 0.0))
      {
        bInside = false;
        break;
      }
    }
    if (bInside)
      return FMath::Square(PlaneDistance);

    double BestDistanceSquared =
      TNumericLimits<double>::Max();
    for (int32 Index = 0;
      Index < Node.Vertices.Num(); ++Index)
    {
      const FVector& A = Node.Vertices[Index];
      const FVector& B =
        Node.Vertices[
          (Index + 1) % Node.Vertices.Num()];
      BestDistanceSquared = FMath::Min(
        BestDistanceSquared,
        FVector::DistSquared(
          Location,
          FMath::ClosestPointOnSegment(
            Location, A, B)));
    }
    return BestDistanceSquared;
  }

  int32 ResolveMixedPopulation()
  {
    int32 Population = DefaultMixedPopulation;
    FParse::Value(
      FCommandLine::Get(),
      TEXT("CrowdDemoEntityCount="), Population);
    return FMath::Clamp(
      Population, 1, MaximumMixedPopulation);
  }

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

  ECrowdActiveBehavior DeriveDiagnosticBehavior(
    const FCrowdBehaviorSourceSet& SourceSet)
  {
    ECrowdActiveBehavior Result = ECrowdActiveBehavior::Idle;
    uint8 ResultRank = 0;
    const auto Promote = [&](
      const uint8 Rank, const ECrowdActiveBehavior Label)
    {
      if (Rank <= ResultRank) return;
      ResultRank = Rank;
      Result = Label;
    };
    for (const FCrowdBehaviorSourceInstance& Instance :
      SourceSet.Instances)
    {
      if (Instance.SourceTypeId
          == CrowdStandardSources::MovementLock
        && Instance.Handle.ControllerId
          == CrowdDemoBehaviorControllerIds::Reaction
        && Instance.Handle.SourceSequence == 2
        && Instance.ExpireFixedStep == INDEX_NONE)
        return ECrowdActiveBehavior::Dead;
      if (Instance.SourceTypeId
          == CrowdDemoSourceTypeIds::DeliverInteraction
        || Instance.SourceTypeId
          == CrowdDemoSourceTypeIds::CarryCargo)
        Promote(9, ECrowdActiveBehavior::HaulDeliver);
      else if (Instance.SourceTypeId
          == CrowdDemoSourceTypeIds::PickupInteraction
        || Instance.SourceTypeId
          == CrowdStandardSources::ArriveAtLocation)
        Promote(8, ECrowdActiveBehavior::HaulPickup);
      else if (Instance.SourceTypeId
          == CrowdDemoSourceTypeIds::AttackTarget)
        Promote(7, ECrowdActiveBehavior::Attack);
      else if (Instance.SourceTypeId
          == CrowdStandardSources::PursueEntity)
        Promote(6, ECrowdActiveBehavior::Pursue);
      else if (Instance.SourceTypeId
          == CrowdStandardSources::FleeFromEntity)
        Promote(5, ECrowdActiveBehavior::Flee);
      else if (Instance.SourceTypeId
          == CrowdStandardSources::WanderSteering)
        Promote(4, ECrowdActiveBehavior::Wander);
      else if (Instance.SourceTypeId
          == CrowdStandardSources::FollowEntity
        || Instance.SourceTypeId
          == CrowdStandardSources::FormationOffset)
        Promote(3, ECrowdActiveBehavior::MoveTo);
      else if (Instance.SourceTypeId
          == CrowdStandardSources::MoveToLocation)
        Promote(2, Instance.Priority == 99
          ? ECrowdActiveBehavior::Guard
          : ECrowdActiveBehavior::MoveTo);
    }
    return Result;
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

  uint64 CalculateMixedHostFactHash(
    const FCrowdDemoMixedAgentState& State)
  {
    uint64 Hash = 14695981039346656037ull;
    Hash = FoldMixedHash(Hash, State.StableEntityId);
    Hash = FoldMixedHash(Hash, State.LifecycleSerial);
    Hash = FoldMixedHash(Hash, State.MembershipKey);
    Hash = FoldMixedHash(Hash, State.DerivedBehaviorLabel);
    Hash = FoldMixedHash(Hash, State.Health);
    Hash = FoldMixedHash(Hash, State.TargetProviderId);
    Hash = FoldMixedHash(Hash, State.TargetStableEntityId);
    Hash = FoldMixedHash(Hash, State.TargetLifecycleSerial);
    Hash = FoldMixedHash(Hash, State.TaskProviderId);
    Hash = FoldMixedHash(Hash, State.TaskStableEntityId);
    Hash = FoldMixedHash(Hash, State.TaskLifecycleSerial);
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
    WriteU32(OutBytes, State.DerivedBehaviorLabel);
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
      || Offset + 5 > Bytes.Num())
      return false;
    if (!ReadU32(Bytes, Offset, OutState.DerivedBehaviorLabel))
      return false;
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
  if (UWorld* World = GetWorld())
    if (UMassCrowdRuntimeSubsystem* Runtime =
      World->GetSubsystem<UMassCrowdRuntimeSubsystem>())
      BehaviorSourceRuntime =
        &Runtime->GetBehaviorSourceRuntime();
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
      if (BehaviorSourceRuntime)
      {
        for (const FSlotState& Slot : Slots)
        {
          if (Slot.Facts.StableEntityRef.IsValid())
            BehaviorSourceRuntime->RemoveEntity(
              Slot.Facts.StableEntityRef);
        }
      }
    }
  }
  BehaviorSourceRuntime = nullptr;
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
  Config.PopulationLimit = ResolveMixedPopulation();
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
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!EntitySubsystem || !RuntimeSubsystem || Config.bValid == 0
    || Config.PopulationLimit <= 0
    || Config.PopulationLimit > MaximumMixedPopulation)
    return false;
  BehaviorSourceRuntime =
    &RuntimeSubsystem->GetBehaviorSourceRuntime();

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
    FCrowdCapabilityBinding Binding;
    Binding.ProfileKey =
      CrowdDemoBehaviorSchemas::FullProfile;
    if (!BehaviorSourceRuntime->RegisterEntity(
        Slots[SlotIndex].Facts.StableEntityRef, Binding))
      return false;
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
  Slot.MembershipKey = MembershipForDiagnosticLabel(
    static_cast<ECrowdActiveBehavior>(Slot.Facts.DerivedBehaviorLabel));
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
  if (Config.PopulationLimit > DefaultMixedPopulation)
  {
    constexpr double ScaleSpawnSpacingCm = 120.0;
    FBox GroundBounds(EForceInit::ForceInit);
    for (const FCrowdNavSurfaceNode& Node : Graph.Nodes)
      if (Node.Center.Z < 200.0)
        GroundBounds += Node.Center;
    if (GroundBounds.IsValid)
    {
      const int32 ColumnCount = FMath::Max(
        1, FMath::FloorToInt(
          (GroundBounds.Max.X - GroundBounds.Min.X)
          / ScaleSpawnSpacingCm) + 1);
      const int32 RowCount = FMath::Max(
        1, FMath::FloorToInt(
          (GroundBounds.Max.Y - GroundBounds.Min.Y)
          / ScaleSpawnSpacingCm) + 1);
      const int32 CellCount = ColumnCount * RowCount;
      for (int32 Offset = 0; Offset < CellCount; ++Offset)
      {
        const int32 Cell =
          (SlotIndex - 1 + Offset) % CellCount;
        FVector Candidate(
          GroundBounds.Min.X
            + static_cast<double>(Cell % ColumnCount)
              * ScaleSpawnSpacingCm,
          GroundBounds.Min.Y
            + static_cast<double>(Cell / ColumnCount)
              * ScaleSpawnSpacingCm,
          0.0);
        uint64 NodeId = 0;
        uint32 NavLayer = 0;
        if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
            Graph, Candidate, 350.0f, NodeId, NavLayer))
          continue;
        const int32 NodeIndex = Graph.FindNodeIndex(NodeId);
        if (!Graph.Nodes.IsValidIndex(NodeIndex)
          || (GoalFlow
            && GoalFlow->Nodes[NodeIndex].IntegrationCostQ
              == MAX_uint32))
          continue;
        Candidate.Z = Graph.Nodes[NodeIndex].Center.Z;
        bool bSeparated = true;
        for (int32 Other = 1; Other < Slots.Num(); ++Other)
        {
          if (Other != SlotIndex
            && Slots[Other].bActive
            && FVector::Distance(
              Candidate, Slots[Other].Location)
              < ScaleSpawnSpacingCm)
          {
            bSeparated = false;
            break;
          }
        }
        if (bSeparated)
        {
          Slot.Location = Candidate;
          Slot.AttachedNavNodeId = NodeId;
          Slot.InteractionLayer = NavLayer;
          return;
        }
      }
    }
  }
  const int32 Start = (SlotIndex * 17) % Graph.Nodes.Num();
  for (int32 Offset = 0; Offset < Graph.Nodes.Num(); ++Offset)
  {
    const int32 NodeIndex = (Start + Offset) % Graph.Nodes.Num();
    if (GoalFlow && GoalFlow->Nodes[NodeIndex].IntegrationCostQ == MAX_uint32) continue;
    for (int32 Ring = 0; Ring <= 3; ++Ring)
    {
      const int32 RingPoints = Ring == 0 ? 1 : Ring * 6;
      for (int32 RingPoint = 0;
        RingPoint < RingPoints; ++RingPoint)
      {
        FVector Candidate = Graph.Nodes[NodeIndex].Center;
        if (Ring > 0)
        {
          const double Angle =
            UE_TWO_PI * static_cast<double>(RingPoint)
            / static_cast<double>(RingPoints);
          Candidate += FVector(
            FMath::Cos(Angle), FMath::Sin(Angle), 0.0)
            * (80.0 * Ring);
        }
        bool bSeparated = true;
        for (int32 Other = 1;
          Other < Slots.Num(); ++Other)
        {
          if (Other != SlotIndex
            && Slots[Other].bActive
            && FVector::Distance(
              Candidate, Slots[Other].Location)
              < 160.0f)
          {
            bSeparated = false;
            break;
          }
        }
        if (bSeparated)
        {
          Slot.Location = Candidate;
          Slot.AttachedNavNodeId =
            Graph.Nodes[NodeIndex].StableNodeId;
          Slot.InteractionLayer =
            Graph.Nodes[NodeIndex].NavLayer;
          return;
        }
      }
    }
  }
  Slot.Location = Graph.Nodes[Start].Center
    + FVector(
      static_cast<double>(SlotIndex) * 80.0,
      0.0, 0.0);
  Slot.AttachedNavNodeId = Graph.Nodes[Start].StableNodeId;
  Slot.InteractionLayer = Graph.Nodes[Start].NavLayer;
}

FCrowdAgentFacts ACrowdDemoMixedSandboxCoordinator::MakeAgentFacts(
  const int32 SlotIndex,
  const uint32 LifecycleSerial) const
{
  FCrowdAgentFacts Facts;
  const int32 CohortRole = ((SlotIndex - 1) % 20) + 1;
  Facts.StableEntityRef = {1, static_cast<uint64>(SlotIndex), LifecycleSerial};
  Facts.FactionKey = static_cast<uint32>((SlotIndex % 3) + 1);
  Facts.CapabilitySet.Add(ECrowdCapability::Move);
  Facts.CapabilitySet.Add(ECrowdCapability::UseNavLayer);
  Facts.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::Idle);
  if (CohortRole <= 6)
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Haul);
  }
  else if (CohortRole <= 10)
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Pursue);
    Facts.CapabilitySet.Add(ECrowdCapability::Attack);
  }
  else if (CohortRole <= 14)
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Guard);
    Facts.CapabilitySet.Add(ECrowdCapability::Flee);
  }
  else
  {
    Facts.CapabilitySet.Add(ECrowdCapability::Wander);
    Facts.CapabilitySet.Add(ECrowdCapability::MoveTo);
  }
  Facts.MovementProfileKey = 1;
  Facts.PresentationProfileKey = 1;
  Facts.RuntimeState = 1;
  return Facts;
}

void ACrowdDemoMixedSandboxCoordinator::AdvanceServerFixedStep()
{
  const double StartSeconds = FPlatformTime::Seconds();
  double EvaluationEndSeconds = StartSeconds;
  double PrepareEndSeconds = StartSeconds;
  double MovementEndSeconds = StartSeconds;
  ++FixedStepIndex;
  if (!RebuildSpatialSafety())
  {
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=spatial_safety fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  TArray<FSlotState> StagedSlots = Slots;
  FCrowdDemoBusinessCommitLedger StagedBusinessLedger =
    BusinessLedger;
  int32 StagedBehaviorTransitionCount =
    BehaviorTransitionCount;
  int32 StagedDuplicateCommitCount =
    DuplicateCommitCount;
  uint32 StagedSeenBehaviorBits = SeenBehaviorBits;
  int32 StagedPendingCombatDeathSlot =
    PendingCombatDeathSlot;
  const int32 OriginalPendingSourceCommandCount =
    BehaviorSourceRuntime->GetPendingCommandCount();
  for (int32 SlotIndex = 1;
    SlotIndex < StagedSlots.Num(); ++SlotIndex)
  {
    if (StagedSlots[SlotIndex].bActive
      && !EvaluateSlotBehavior(
        SlotIndex, StagedSlots))
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      ++StaleRejectCount;
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=behavior slot=%d fixed_step=%lld"),
        SlotIndex, FixedStepIndex);
      return;
    }
  }
  EvaluationEndSeconds = FPlatformTime::Seconds();
  FCrowdBehaviorPreparedBoundary PreparedBehavior;
  if (!BehaviorSourceRuntime->PrepareBoundary(
      FixedStepIndex, PreparedBehavior)
    || !BehaviorSourceRuntime->ValidatePrepared(PreparedBehavior))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=source_prepare fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  for (const FCrowdBehaviorPreparedEntity& Entity
    : PreparedBehavior.Entities)
  {
    const int32 SlotIndex =
      static_cast<int32>(Entity.EntityRef.StableEntityId);
    if (!StagedSlots.IsValidIndex(SlotIndex)
      || !StagedSlots[SlotIndex].bActive
      || StagedSlots[SlotIndex].Facts.StableEntityRef
        != Entity.EntityRef)
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      ++StaleRejectCount;
      return;
    }
    const ECrowdActiveBehavior Label =
      DeriveDiagnosticBehavior(Entity.StagedSourceSet);
    if (StagedSlots[SlotIndex].Facts.DerivedBehaviorLabel
      != static_cast<uint32>(Label))
    {
      ++StagedBehaviorTransitionCount;
      ++StagedSlots[SlotIndex].TransitionRevision;
    }
    StagedSlots[SlotIndex].Facts.DerivedBehaviorLabel =
      static_cast<uint32>(Label);
    StagedSeenBehaviorBits |= BehaviorBit(Label);
  }
  if (!ApplyPreparedBehaviorBusiness(
      PreparedBehavior, StagedSlots,
      StagedBusinessLedger,
      StagedDuplicateCommitCount,
      StagedPendingCombatDeathSlot))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=source_prepare fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  PrepareEndSeconds = FPlatformTime::Seconds();
  TArray<FCrowdAgentFacts> PreparedFacts;
  for (int32 SlotIndex = 1;
    SlotIndex < StagedSlots.Num(); ++SlotIndex)
    if (StagedSlots[SlotIndex].bActive)
      PreparedFacts.Add(StagedSlots[SlotIndex].Facts);
  PreparedFacts.Sort([](const auto& A, const auto& B)
  {
    return A.StableEntityRef < B.StableEntityRef;
  });
  if (!LifecycleWorld.ValidateAgentFactsCorrectionsAtBoundary(
      FixedStepIndex, PreparedFacts))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=business_commit fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  int32 StagedSafetyHolds = 0;
  uint64 StagedBoundaryCommitHash = 0;
  if (!RunProductMovementBoundary(
      PreparedBehavior, StagedSlots,
      StagedSafetyHolds, StagedBoundaryCommitHash))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=product_boundary fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  MovementEndSeconds = FPlatformTime::Seconds();
  LifecycleWorld.ApplyValidatedAgentFactsCorrectionsAtBoundary(
    FixedStepIndex, PreparedFacts);
  checkf(BehaviorSourceRuntime->CommitPrepared(PreparedBehavior),
    TEXT("Validated behavior source transaction changed before final apply"));
  Slots = MoveTemp(StagedSlots);
  BusinessLedger = MoveTemp(StagedBusinessLedger);
  BehaviorTransitionCount = StagedBehaviorTransitionCount;
  DuplicateCommitCount = StagedDuplicateCommitCount;
  SeenBehaviorBits = StagedSeenBehaviorBits;
  PendingCombatDeathSlot = StagedPendingCombatDeathSlot;
  SafetyHoldCount += StagedSafetyHolds;
  LastBoundaryCommitHash = StagedBoundaryCommitHash;

  // Final Apply already rejects every unsafe candidate against the updated
  // spatial index. Sample the exact minimum once per simulation second for
  // telemetry instead of turning an audit metric into per-step production
  // work; retain the minimum observed across the complete run.
  if (FixedStepIndex <= 5 || FixedStepIndex % 30 == 0)
    MinimumSeparationCm = FMath::Min(
      MinimumSeparationCm,
      SpatialSafety.CalculateMinimumSeparationCm());

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
  const double EndSeconds = FPlatformTime::Seconds();
  const double TotalMilliseconds =
    (EndSeconds - StartSeconds) * 1000.0;
  ServerStepMilliseconds.Add(TotalMilliseconds);
  if (Config.PopulationLimit == MaximumMixedPopulation
    && TotalMilliseconds > 33.333)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxSlowStep fixed_step=%lld evaluate_ms=%.3f prepare_ms=%.3f movement_ms=%.3f apply_publish_ms=%.3f total_ms=%.3f"),
      FixedStepIndex,
      (EvaluationEndSeconds - StartSeconds) * 1000.0,
      (PrepareEndSeconds - EvaluationEndSeconds) * 1000.0,
      (MovementEndSeconds - PrepareEndSeconds) * 1000.0,
      (EndSeconds - MovementEndSeconds) * 1000.0,
      TotalMilliseconds);
  }
  if (Config.PopulationLimit == MaximumMixedPopulation
    && (FixedStepIndex <= 5 || FixedStepIndex % 150 == 0))
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxPhases fixed_step=%lld evaluate_ms=%.3f prepare_ms=%.3f movement_ms=%.3f apply_publish_ms=%.3f total_ms=%.3f"),
      FixedStepIndex,
      (EvaluationEndSeconds - StartSeconds) * 1000.0,
      (PrepareEndSeconds - EvaluationEndSeconds) * 1000.0,
      (MovementEndSeconds - PrepareEndSeconds) * 1000.0,
      (EndSeconds - MovementEndSeconds) * 1000.0,
      (EndSeconds - StartSeconds) * 1000.0);
  }
  if (ServerStepMilliseconds.Num() > 2048) ServerStepMilliseconds.RemoveAt(0, 512);
}

bool ACrowdDemoMixedSandboxCoordinator::EvaluateSlotBehavior(
  const int32 SlotIndex,
  TArray<FSlotState>& InOutSlots)
{
  const auto RejectBehavior = [this, SlotIndex](const TCHAR* Reason)
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxBehaviorReject reason=%s slot=%d fixed_step=%lld"),
      Reason, SlotIndex, FixedStepIndex);
    return false;
  };
  FSlotState& Slot = InOutSlots[SlotIndex];
  const int32 CohortBase = ((SlotIndex - 1) / 20) * 20;
  const int32 CohortRole = ((SlotIndex - 1) % 20) + 1;
  const auto ActiveRef = [&](const int32 Candidate)
  {
    return InOutSlots.IsValidIndex(Candidate)
      && InOutSlots[Candidate].bActive
      ? InOutSlots[Candidate].Facts.StableEntityRef
      : FCrowdStableEntityRef{};
  };
  const auto DockingLocation = [&](const FName MarkerTag)
  {
    const FVector MarkerLocation =
      Marker(MarkerTag, FVector::ZeroVector);
    if (!NavGraphHandle.IsValid())
      return MarkerLocation;
    TArray<const FCrowdNavSurfaceNode*> Candidates;
    for (const FCrowdNavSurfaceNode& Node : NavGraphHandle->Nodes)
    {
      const bool bScaleCandidate =
        Config.PopulationLimit > DefaultMixedPopulation
        && Node.Center.Z < 800.0;
      if (bScaleCandidate
        || FVector::Distance(Node.Center, MarkerLocation) <= 400.0)
        Candidates.Add(&Node);
    }
    Candidates.Sort(
      [&MarkerLocation](
        const FCrowdNavSurfaceNode& A,
        const FCrowdNavSurfaceNode& B)
      {
        const double DistanceA =
          FVector::DistSquared(A.Center, MarkerLocation);
        const double DistanceB =
          FVector::DistSquared(B.Center, MarkerLocation);
        return DistanceA != DistanceB
          ? DistanceA < DistanceB
          : A.StableNodeId < B.StableNodeId;
      });
    if (Candidates.IsEmpty())
      return MarkerLocation;
    const int32 MarkerOffset =
      MarkerTag == TEXT("CrowdNavHigh")
      ? Candidates.Num() / 2
      : MarkerTag == TEXT("CrowdNavRouteB")
        ? Candidates.Num() / 3
        : 0;
    return Candidates[
      (MarkerOffset
        + (CohortBase / 20 * 6 + CohortRole - 1) * 17)
        % Candidates.Num()]
      ->Center;
  };

  TArray<FCrowdDemoDesiredSource, TInlineAllocator<8>> Desired;
  const auto AddStandard =
    [&]<typename PayloadType>(
      const FCrowdBehaviorControllerId ControllerId,
      const uint32 Sequence,
      const FCrowdBehaviorSourceTypeId TypeId,
      const PayloadType& Payload,
      const int32 LifetimeSteps = 0,
      const int16 Priority = 0)
  {
    FCrowdDemoDesiredSource& Entry =
      Desired.AddDefaulted_GetRef();
    Entry.ControllerId = ControllerId;
    Entry.SourceSequence = Sequence;
    Entry.SourceTypeId = TypeId;
    Entry.Priority = Priority;
    Entry.LifetimeSteps = LifetimeSteps;
    return Entry.Payload.Set(
      CrowdStandardSources::PayloadSchema(TypeId), Payload);
  };
  const auto AddDemo =
    [&](const FCrowdBehaviorControllerId ControllerId,
      const uint32 Sequence,
      const FCrowdBehaviorSourceTypeId TypeId,
      const FCrowdDemoBehaviorSourcePayload& Payload,
      const int32 LifetimeSteps = 0)
  {
    FCrowdDemoDesiredSource& Entry =
      Desired.AddDefaulted_GetRef();
    Entry.ControllerId = ControllerId;
    Entry.SourceSequence = Sequence;
    Entry.SourceTypeId = TypeId;
    Entry.LifetimeSteps = LifetimeSteps;
    return Entry.Payload.Set(
      CrowdDemoBehaviorSchemas::Standard, Payload);
  };
  const auto AddFaceMovement = [&]()
  {
    FCrowdFaceMovementPayload Facing;
    Facing.MinimumSpeedCmps = 1.0f;
    return AddStandard(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceMovement, Facing);
  };
  const auto AddFaceEntity =
    [&](const FCrowdStableEntityRef TargetRef)
  {
    FCrowdFaceEntityPayload Facing;
    Facing.TargetRef = TargetRef;
    return AddStandard(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceEntity, Facing);
  };
  const auto AddSpeedLimit = [&]()
  {
    FCrowdSpeedLimitPayload Limit;
    // Keep the 500-agent gate on the same production pipeline without
    // turning its small validation map into an artificial convergence
    // stress test. Particle/local-predictive stress has dedicated kernels;
    // this gate measures the full Standard Source composition path.
    Limit.MaximumSpeedCmps =
      Config.PopulationLimit >= 500 ? 10.0f : 500.0f;
    Limit.AllowedNavLayerMask = MAX_uint64;
    return AddStandard(
      CrowdDemoBehaviorControllerIds::Navigation, 3,
      CrowdStandardSources::SpeedLimit, Limit);
  };

  ECrowdActiveBehavior DiagnosticLabel = ECrowdActiveBehavior::Idle;
  FVector Objective = Slot.Location;
  FCrowdStableEntityRef TargetRef;
  FCrowdStableEntityRef TaskRef;
  FCrowdStableEntityRef FormationAnchorRef;
  FVector FormationLocalOffset = FVector::ZeroVector;
  bool bInteractionReady = false;
  ECrowdBusinessCommitKind CommitKind =
    ECrowdBusinessCommitKind::None;

  if (Slot.Health <= 0)
  {
    DiagnosticLabel = ECrowdActiveBehavior::Dead;
    FCrowdMovementLockPayload Lock;
    if (!AddStandard(
        CrowdDemoBehaviorControllerIds::Reaction, 2,
        CrowdStandardSources::MovementLock, Lock))
      return RejectBehavior(TEXT("death_lock"));
  }
  else if (CohortRole <= 6)
  {
    const bool bCarrying =
      BusinessLedger.GetCargoCarrier(
        static_cast<uint64>(SlotIndex))
      == static_cast<uint64>(SlotIndex);
    DiagnosticLabel = bCarrying
      ? ECrowdActiveBehavior::HaulDeliver
      : ECrowdActiveBehavior::HaulPickup;
    Objective = bCarrying
      ? DockingLocation(TEXT("CrowdNavHigh"))
      : DockingLocation(TEXT("CrowdNavLower"));
    FCrowdArriveAtLocationPayload Move;
    Move.TargetLocation = FVector3f(Objective);
    Move.MaximumSpeedCmps = 500.0f;
    Move.AcceptanceRadiusCm = 80.0f;
    Move.SlowdownRadiusCm = 300.0f;
    if (!AddStandard(
        CrowdDemoBehaviorControllerIds::Navigation, 1,
        CrowdStandardSources::ArriveAtLocation, Move)
      || !AddSpeedLimit()
      || !AddFaceMovement())
      return RejectBehavior(TEXT("logistics_sources"));
    TaskRef = {2, static_cast<uint64>(SlotIndex), 1};
    bInteractionReady =
      FVector::Distance(Slot.Location, Objective)
      <= (Config.PopulationLimit > DefaultMixedPopulation
        ? MixedScaleInteractionRadiusCm
        : MixedInteractionRadiusCm)
      && FixedStepIndex - Slot.LastLogisticsFixedStep >= 30;
    CommitKind = bCarrying
      ? ECrowdBusinessCommitKind::CargoDeliver
      : ECrowdBusinessCommitKind::CargoPickup;
    if (bCarrying)
    {
      FCrowdDemoBehaviorSourcePayload Carry;
      Carry.PrimaryId = 1;
      Carry.SecondaryId = 1;
      if (!AddDemo(
          CrowdDemoBehaviorControllerIds::Presentation, 1,
          CrowdDemoSourceTypeIds::CarryCargo, Carry))
        return RejectBehavior(TEXT("carry_source"));
    }
  }
  else if (CohortRole <= 10)
  {
    int32 TargetSlot = INDEX_NONE;
    for (int32 Offset = 0; Offset < 4; ++Offset)
    {
      const int32 Candidate =
        CohortBase + 11
        + ((CohortRole - 7 + Offset) % 4);
      if (ActiveRef(Candidate).IsValid())
      {
        TargetSlot = Candidate;
        break;
      }
    }
    TargetRef = ActiveRef(TargetSlot);
    if (!TargetRef.IsValid())
    {
      // A lost target is a controller fact change, not an evaluator error.
      // Omitting the dependent handles makes the stable diff stop them in
      // this boundary before evaluation sees an absent context.
      DiagnosticLabel = ECrowdActiveBehavior::Idle;
      if (!AddSpeedLimit())
        return RejectBehavior(TEXT("pursue_target_stop"));
    }
    else
    {
      Objective = InOutSlots[TargetSlot].Location;
      const float Distance =
        FVector::Distance(Slot.Location, Objective);
      const float AttackInteractionRadiusCm =
        Config.PopulationLimit > DefaultMixedPopulation
        ? MixedScaleInteractionRadiusCm
        : 180.0f;
      const bool bAttackReady =
        Distance <= AttackInteractionRadiusCm
        && FixedStepIndex - Slot.LastAttackFixedStep >= 30;
      DiagnosticLabel = Distance <= AttackInteractionRadiusCm
        ? ECrowdActiveBehavior::Attack
        : ECrowdActiveBehavior::Pursue;
      FCrowdPursueEntityPayload Pursue;
      Pursue.TargetRef = TargetRef;
      Pursue.MaximumSpeedCmps = 500.0f;
      Pursue.AcceptanceRadiusCm = 140.0f;
      Pursue.MaximumPredictionSeconds = 0.25f;
      FCrowdMaintainDistancePayload DistanceBand;
      DistanceBand.TargetRef = TargetRef;
      DistanceBand.MinimumDistanceCm = 130.0f;
      DistanceBand.MaximumDistanceCm = 180.0f;
      DistanceBand.HysteresisCm = 10.0f;
      DistanceBand.MaximumCorrectionSpeedCmps = 150.0f;
      if (!AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::PursueEntity, Pursue)
        || !AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 2,
          CrowdStandardSources::MaintainDistance, DistanceBand)
        || !AddSpeedLimit()
        || !AddFaceEntity(TargetRef))
        return RejectBehavior(TEXT("pursue_sources"));
      bInteractionReady = bAttackReady;
      CommitKind = ECrowdBusinessCommitKind::CombatHit;
      if (bAttackReady)
      {
        FCrowdMovementLockPayload Lock;
        if (!AddStandard(
            CrowdDemoBehaviorControllerIds::Reaction, 1,
            CrowdStandardSources::MovementLock, Lock, 1))
          return RejectBehavior(TEXT("attack_lock"));
      }
    }
  }
  else if (CohortRole <= 14)
  {
    if (Slot.Health <= 50)
    {
      const int32 TargetSlot =
        [&]()
        {
          for (int32 Offset = 0; Offset < 4; ++Offset)
          {
            const int32 Candidate =
              CohortBase + 7
              + ((CohortRole - 11 + Offset) % 4);
            if (ActiveRef(Candidate).IsValid())
              return Candidate;
          }
          return static_cast<int32>(INDEX_NONE);
        }();
      TargetRef = ActiveRef(TargetSlot);
      if (!TargetRef.IsValid())
      {
        DiagnosticLabel = ECrowdActiveBehavior::Idle;
        if (!AddSpeedLimit())
          return RejectBehavior(TEXT("flee_target_stop"));
      }
      else
      {
        Objective = InOutSlots[TargetSlot].Location;
        DiagnosticLabel = ECrowdActiveBehavior::Flee;
        FCrowdFleeFromEntityPayload Flee;
        Flee.TargetRef = TargetRef;
        Flee.MaximumSpeedCmps = 500.0f;
        Flee.SafeDistanceCm = 1000.0f;
        Flee.MaximumPredictionSeconds = 0.25f;
        if (!AddStandard(
            CrowdDemoBehaviorControllerIds::Navigation, 1,
            CrowdStandardSources::FleeFromEntity, Flee)
          || !AddSpeedLimit()
          || !AddFaceEntity(TargetRef))
          return RejectBehavior(TEXT("flee_sources"));
      }
    }
    else
    {
      DiagnosticLabel = ECrowdActiveBehavior::Guard;
      Objective = Slot.Location;
      FCrowdMoveToLocationPayload Move;
      Move.TargetLocation = FVector3f(Objective);
      Move.MaximumSpeedCmps = 350.0f;
      Move.AcceptanceRadiusCm = 120.0f;
      if (!AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::MoveToLocation, Move,
          0, 99)
        || !AddSpeedLimit()
        || !AddFaceMovement())
        return RejectBehavior(TEXT("guard_sources"));
    }
  }
  else if (CohortRole <= 16)
  {
    const bool bUseMoveTo =
      ((FixedStepIndex / 300) + CohortRole) % 2 == 0;
    DiagnosticLabel = bUseMoveTo
      ? ECrowdActiveBehavior::MoveTo
      : ECrowdActiveBehavior::Wander;
    if (bUseMoveTo)
    {
      Objective = DockingLocation(TEXT("CrowdNavRouteB"));
      FCrowdMoveToLocationPayload Move;
      Move.TargetLocation = FVector3f(Objective);
      Move.MaximumSpeedCmps = 400.0f;
      Move.AcceptanceRadiusCm = 100.0f;
      if (!AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::MoveToLocation, Move))
        return RejectBehavior(TEXT("roam_move"));
    }
    else
    {
      FCrowdWanderSteeringPayload Wander;
      Wander.SpeedCmps = 300.0f;
      Wander.ReselectIntervalSteps = 45;
      if (!AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::WanderSteering, Wander))
        return RejectBehavior(TEXT("roam_wander"));
    }
    if (!AddSpeedLimit() || !AddFaceMovement())
      return RejectBehavior(TEXT("roam_common"));
  }
  else
  {
    const int32 AnchorSlot =
      [&]()
      {
        for (int32 Offset = 0; Offset < 2; ++Offset)
        {
          const int32 Candidate =
            CohortBase + 15
            + ((CohortRole - 17 + Offset) % 2);
          if (ActiveRef(Candidate).IsValid())
            return Candidate;
        }
        return static_cast<int32>(INDEX_NONE);
      }();
    TargetRef = ActiveRef(AnchorSlot);
    FormationAnchorRef = TargetRef;
    if (!TargetRef.IsValid())
    {
      DiagnosticLabel = ECrowdActiveBehavior::Idle;
      if (!AddSpeedLimit())
        return RejectBehavior(TEXT("escort_anchor_stop"));
    }
    else
    {
      Objective = InOutSlots[AnchorSlot].Location;
      DiagnosticLabel = ECrowdActiveBehavior::MoveTo;
      const int32 EscortIndex = CohortRole - 17;
      FormationLocalOffset = FVector(
        -160.0 - 80.0 * (EscortIndex / 2),
        EscortIndex % 2 == 0 ? -100.0 : 100.0,
        0.0);
      FCrowdFollowEntityPayload Follow;
      Follow.TargetRef = TargetRef;
      Follow.LocalOffset = FVector3f(FormationLocalOffset);
      Follow.MaximumSpeedCmps = 450.0f;
      Follow.AcceptanceRadiusCm = 60.0f;
      Follow.PositionGain = 1.5f;
      FCrowdMaintainDistancePayload DistanceBand;
      DistanceBand.TargetRef = TargetRef;
      DistanceBand.MinimumDistanceCm = 120.0f;
      DistanceBand.MaximumDistanceCm = 300.0f;
      DistanceBand.HysteresisCm = 15.0f;
      DistanceBand.MaximumCorrectionSpeedCmps = 100.0f;
      FCrowdFormationOffsetPayload Formation;
      Formation.PositionGain = 1.0f;
      Formation.MaximumCorrectionSpeedCmps = 120.0f;
      if (!AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::FollowEntity, Follow)
        || !AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 2,
          CrowdStandardSources::MaintainDistance, DistanceBand)
        || !AddSpeedLimit()
        || !AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 4,
          CrowdStandardSources::FormationOffset, Formation)
        || !AddFaceMovement())
        return RejectBehavior(TEXT("escort_sources"));
    }
  }

  if (bInteractionReady
    && CommitKind != ECrowdBusinessCommitKind::None)
  {
    FCrowdRuntimeBehaviorContext CommitContext;
    CommitContext.AgentFacts = Slot.Facts;
    CommitContext.RequestedBehavior = DiagnosticLabel;
    CommitContext.FixedStepIndex = FixedStepIndex;
    CommitContext.TransitionRevision = Slot.TransitionRevision;
    CommitContext.TargetRef = TargetRef;
    CommitContext.TaskRef = TaskRef;
    CommitContext.TargetLocation = Objective;
    CommitContext.InteractionPayloadKey =
      static_cast<uint32>(DiagnosticLabel) + 1;
    CommitContext.InteractionQuantity =
      CommitKind == ECrowdBusinessCommitKind::CombatHit ? 25 : 1;
    FCrowdDemoBehaviorSourcePayload Interaction;
    Interaction.PrimaryId =
      CommitKind == ECrowdBusinessCommitKind::CargoPickup
      ? CrowdDemoBehaviorAdapterIds::CargoPickup
      : CommitKind == ECrowdBusinessCommitKind::CargoDeliver
        ? CrowdDemoBehaviorAdapterIds::CargoDeliver
        : CrowdDemoBehaviorAdapterIds::CombatHit;
    Interaction.SecondaryId =
      CommitContext.InteractionPayloadKey;
    Interaction.Quantity = CommitContext.InteractionQuantity;
    Interaction.TargetRef =
      CommitKind == ECrowdBusinessCommitKind::CombatHit
      ? TargetRef : TaskRef;
    Interaction.CommitId =
      FCrowdBehaviorCommitId::Make(
        CommitKind, CommitContext);
    const FCrowdBehaviorSourceTypeId InteractionType =
      CommitKind == ECrowdBusinessCommitKind::CargoPickup
      ? CrowdDemoSourceTypeIds::PickupInteraction
      : CommitKind == ECrowdBusinessCommitKind::CargoDeliver
        ? CrowdDemoSourceTypeIds::DeliverInteraction
        : CrowdDemoSourceTypeIds::AttackTarget;
    if (!AddDemo(
        CrowdDemoBehaviorControllerIds::Interaction, 1,
        InteractionType, Interaction, 1))
      return RejectBehavior(TEXT("interaction_source"));
  }

  if (Slot.HitReactionUntilFixedStep > FixedStepIndex)
  {
    FCrowdTimedImpulsePayload Impulse;
    Impulse.InitialVelocity =
      FVector3f(Slot.HitReactionVelocity);
    Impulse.DecayMode = ECrowdImpulseDecayMode::Linear;
    if (!AddStandard(
        CrowdDemoBehaviorControllerIds::Reaction, 3,
        CrowdStandardSources::TimedImpulse, Impulse,
        static_cast<int32>(
          Slot.HitReactionUntilFixedStep - FixedStepIndex)))
      return RejectBehavior(TEXT("hit_reaction"));
  }

  const FCrowdBehaviorSourceSet* CurrentSet =
    BehaviorSourceRuntime->FindSourceSet(
      Slot.Facts.StableEntityRef);
  if (!CurrentSet)
    return RejectBehavior(TEXT("source_set"));
  TArray<FCrowdBehaviorSourceCommand> Commands;
  if (!FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
      FixedStepIndex, *CurrentSet, Desired, Commands))
    return RejectBehavior(TEXT("source_diff"));
  for (const FCrowdBehaviorSourceCommand& Command : Commands)
    if (!BehaviorSourceRuntime->QueueCommand(Command))
      return RejectBehavior(TEXT("queue_command"));
  FCrowdBehaviorEntityEvaluationContext EvaluationContext;
  EvaluationContext.EntityRef = Slot.Facts.StableEntityRef;
  EvaluationContext.FixedStepIndex = FixedStepIndex;
  EvaluationContext.Position = Slot.Location;
  EvaluationContext.Velocity = Slot.Velocity;
  EvaluationContext.Facing =
    FRotator(0.0f, Slot.YawDegrees, 0.0f).Vector();
  if (TargetRef.IsValid())
  {
    const int32 TargetSlot =
      static_cast<int32>(TargetRef.StableEntityId);
    if (!InOutSlots.IsValidIndex(TargetSlot)
      || !InOutSlots[TargetSlot].bActive
      || InOutSlots[TargetSlot].Facts.StableEntityRef
        != TargetRef)
      return RejectBehavior(TEXT("target_context"));
    FCrowdTargetKinematicsV1 Target;
    Target.TargetRef = TargetRef;
    Target.Position =
      FVector3f(InOutSlots[TargetSlot].Location);
    Target.Velocity =
      FVector3f(InOutSlots[TargetSlot].Velocity);
    Target.Facing = FVector3f(
      FRotator(
        0.0f,
        InOutSlots[TargetSlot].YawDegrees,
        0.0f).Vector());
    Target.NavLayer =
      InOutSlots[TargetSlot].InteractionLayer;
    Target.FactRevision =
      static_cast<uint64>(FixedStepIndex) + 1;
    FCrowdBehaviorContextRecord& Record =
      EvaluationContext.Records.AddDefaulted_GetRef();
    if (!Record.Set(
        CrowdStandardSources::TargetKinematicsContextType,
        CrowdStandardSources::ContextSchemaVersion,
        Target))
      return RejectBehavior(TEXT("target_record"));
  }
  if (FormationAnchorRef.IsValid())
  {
    const int32 AnchorSlot =
      static_cast<int32>(FormationAnchorRef.StableEntityId);
    FCrowdFormationAnchorV1 Anchor;
    Anchor.AnchorRef = FormationAnchorRef;
    Anchor.Position = FVector3f(InOutSlots[AnchorSlot].Location);
    Anchor.Velocity = FVector3f(InOutSlots[AnchorSlot].Velocity);
    Anchor.Facing = FVector3f(
      FRotator(
        0.0f,
        InOutSlots[AnchorSlot].YawDegrees,
        0.0f).Vector());
    Anchor.LocalSlotOffset = FVector3f(FormationLocalOffset);
    Anchor.NavLayer = InOutSlots[AnchorSlot].InteractionLayer;
    Anchor.FactRevision =
      static_cast<uint64>(FixedStepIndex) + 1;
    FCrowdBehaviorContextRecord& Record =
      EvaluationContext.Records.AddDefaulted_GetRef();
    if (!Record.Set(
        CrowdStandardSources::FormationAnchorContextType,
        CrowdStandardSources::ContextSchemaVersion,
        Anchor))
      return RejectBehavior(TEXT("formation_record"));
  }
  EvaluationContext.RecalculateStableHash();
  if (!BehaviorSourceRuntime->SetEvaluationContext(EvaluationContext))
    return RejectBehavior(TEXT("evaluation_context"));

  Slot.Facts.TargetRef = TargetRef;
  Slot.Facts.BusinessTaskRef = TaskRef;
  Slot.Facts.MovementProfileKey = 1;

  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::ApplyPreparedBehaviorBusiness(
  const FCrowdBehaviorPreparedBoundary& Prepared,
  TArray<FSlotState>& InOutSlots,
  FCrowdDemoBusinessCommitLedger& InOutLedger,
  int32& InOutDuplicateCommitCount,
  int32& InOutPendingCombatDeathSlot)
{
  for (const FCrowdBehaviorPreparedEntity& Entity
    : Prepared.Entities)
  {
    const int32 SlotIndex =
      static_cast<int32>(Entity.EntityRef.StableEntityId);
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || !InOutSlots[SlotIndex].bActive
      || InOutSlots[SlotIndex].Facts.StableEntityRef != Entity.EntityRef)
      return false;
    FSlotState& Slot = InOutSlots[SlotIndex];
    for (const FCrowdBusinessContribution& Contribution
      : Entity.ResolvedChannels.Business)
    {
      FCrowdBusinessCommitRequest Request;
      if (Contribution.AdapterId
        == CrowdDemoBehaviorAdapterIds::CargoPickup)
        Request.Kind = ECrowdBusinessCommitKind::CargoPickup;
      else if (Contribution.AdapterId
        == CrowdDemoBehaviorAdapterIds::CargoDeliver)
        Request.Kind = ECrowdBusinessCommitKind::CargoDeliver;
      else if (Contribution.AdapterId
        == CrowdDemoBehaviorAdapterIds::CombatHit)
        Request.Kind = ECrowdBusinessCommitKind::CombatHit;
      else
        return false;
      Request.CommitId = Contribution.CommitId;
      Request.FixedStepIndex = Prepared.FixedStepIndex;
      Request.TransitionRevision = Slot.TransitionRevision;
      Request.AgentRef = Contribution.InstigatorRef;
      Request.PayloadKey = Contribution.PayloadTypeId;
      Request.Quantity = Contribution.Quantity;
      if (Request.Kind == ECrowdBusinessCommitKind::CombatHit)
        Request.TargetRef = Contribution.TargetRef;
      else
        Request.TaskRef = Contribution.TargetRef;

      const ECrowdDemoBusinessCommitAcceptResult First =
        InOutLedger.Apply(Request);
      const ECrowdDemoBusinessCommitAcceptResult Replay =
        InOutLedger.Apply(Request);
      if (First != ECrowdDemoBusinessCommitAcceptResult::Applied
        || Replay != ECrowdDemoBusinessCommitAcceptResult::Duplicate)
        return false;
      ++InOutDuplicateCommitCount;
      if (Request.Kind == ECrowdBusinessCommitKind::CombatHit)
      {
        Slot.LastAttackFixedStep = FixedStepIndex;
        const int32 TargetSlot = static_cast<int32>(
          Request.TargetRef.StableEntityId);
        if (!InOutSlots.IsValidIndex(TargetSlot)
          || !InOutSlots[TargetSlot].bActive)
          return false;
        const int32 InstigatorSlot = static_cast<int32>(
          Request.AgentRef.StableEntityId);
        if (!InOutSlots.IsValidIndex(InstigatorSlot)
          || !InOutSlots[InstigatorSlot].bActive)
          return false;
        InOutSlots[TargetSlot].HitReactionVelocity =
          FVector::ZeroVector;
        InOutSlots[TargetSlot].HitReactionUntilFixedStep =
          FMath::Max(
            InOutSlots[TargetSlot].HitReactionUntilFixedStep,
            FixedStepIndex + 6);
        InOutSlots[TargetSlot].Health = FMath::Max(
          0, InOutSlots[TargetSlot].Health - Request.Quantity);
        if (InOutSlots[TargetSlot].Health == 0
          && InOutPendingCombatDeathSlot == INDEX_NONE)
          InOutPendingCombatDeathSlot = TargetSlot;
      }
      else
      {
        Slot.LastLogisticsFixedStep = FixedStepIndex;
      }
    }
  }
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::RunProductMovementBoundary(
  const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
  TArray<FSlotState>& InOutSlots,
  int32& OutSafetyHolds,
  uint64& OutCommitHash)
{
  const double ProductStartSeconds =
    FPlatformTime::Seconds();
  const auto RejectBoundary = [this](const TCHAR* Reason)
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxBoundaryReject reason=%s fixed_step=%lld"),
      Reason, FixedStepIndex);
    return false;
  };
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassCrowdRuntimeSubsystem* Runtime =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!EntitySubsystem || !Runtime || !NavGraphHandle.IsValid())
    return RejectBoundary(TEXT("missing_runtime_or_nav"));

  TArray<FCrowdMassBoundaryAgentRecord> Gathered;
  TArray<FVector> ResolvedVelocities;
  TArray<FVector> ResolvedFacings;
  TArray<float> ResolvedMaximumSpeeds;
  TArray<uint32> ResolvedInteractionLayers;
  ResolvedVelocities.SetNumZeroed(InOutSlots.Num());
  ResolvedFacings.SetNumZeroed(InOutSlots.Num());
  ResolvedMaximumSpeeds.Init(500.0f, InOutSlots.Num());
  ResolvedInteractionLayers.SetNumZeroed(InOutSlots.Num());
  TArray<FCrowdMassCommitTarget> Targets;
  TArray<const FCrowdBehaviorPreparedEntity*>
    PreparedBehaviorBySlot;
  PreparedBehaviorBySlot.SetNumZeroed(InOutSlots.Num());
  for (const FCrowdBehaviorPreparedEntity& PreparedEntity
    : PreparedBehavior.Entities)
  {
    const int32 PreparedSlotIndex =
      static_cast<int32>(
        PreparedEntity.EntityRef.StableEntityId);
    if (!PreparedBehaviorBySlot.IsValidIndex(
          PreparedSlotIndex)
      || PreparedBehaviorBySlot[PreparedSlotIndex])
      return RejectBoundary(TEXT("prepared_entity_index"));
    PreparedBehaviorBySlot[PreparedSlotIndex] =
      &PreparedEntity;
  }
  for (int32 SlotIndex = 1;
    SlotIndex < InOutSlots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = InOutSlots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdMassBoundaryAgentRecord& Record =
      Gathered.AddDefaulted_GetRef();
    Record.Identity.AgentId = SlotIndex;
    Record.Identity.SetStableEntityRef(
      Slot.Facts.StableEntityRef);
    Record.AgentFacts = Slot.Facts;
    Record.State.Position = Slot.Location;
    Record.State.Velocity = Slot.Velocity;
    Record.State.YawDegrees = Slot.YawDegrees;
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
    uint64 AttachedNodeId =
      Slot.AttachedNavNodeId;
    uint32 AttachedNavLayer =
      Slot.InteractionLayer;
    int32 AttachedNodeIndex =
      NavGraphHandle->FindNodeIndex(AttachedNodeId);
    if (!NavGraphHandle->Nodes.IsValidIndex(
          AttachedNodeIndex)
      || DistanceSquaredToNavNode(
          Slot.Location,
          NavGraphHandle->Nodes[AttachedNodeIndex])
        > FMath::Square(350.0))
    {
      AttachedNodeIndex = INDEX_NONE;
      if (FCrowdNavSurfaceGraphKernel::AttachClosest(
          *NavGraphHandle, Slot.Location, 350.0f,
          AttachedNodeId, AttachedNavLayer))
        AttachedNodeIndex =
          NavGraphHandle->FindNodeIndex(AttachedNodeId);
    }
    if (NavGraphHandle->Nodes.IsValidIndex(
        AttachedNodeIndex))
    {
      InOutSlots[SlotIndex].AttachedNavNodeId =
        AttachedNodeId;
      InOutSlots[SlotIndex].InteractionLayer =
        AttachedNavLayer;
    }
    ResolvedInteractionLayers[SlotIndex] =
      AttachedNavLayer;
    Targets.Add({
      Slot.Facts.StableEntityRef,
      SlotIndex,
      Slot.Facts.StableEntityRef.LifecycleSerial});

    const FCrowdBehaviorPreparedEntity* PreparedPtr =
      PreparedBehaviorBySlot[SlotIndex];
    if (!PreparedPtr
      || PreparedPtr->EntityRef
        != Slot.Facts.StableEntityRef)
      return RejectBoundary(TEXT("missing_prepared_entity"));
    const FCrowdBehaviorPreparedEntity& PreparedEntity =
      *PreparedPtr;
    const FCrowdResolvedBehaviorChannels& Resolved =
      PreparedEntity.ResolvedChannels;
    if (!Resolved.DesiredFacing.IsNearlyZero())
      ResolvedFacings[SlotIndex] =
        Resolved.DesiredFacing;
    const float SpeedLimitCmps =
      FMath::IsFinite(Resolved.SpeedLimitCmps)
      ? FMath::Clamp(Resolved.SpeedLimitCmps, 0.0f, 500.0f)
      : 500.0f;
    FVector DesiredVelocity = Resolved.bMovementLocked
      ? FVector::ZeroVector
      : Resolved.DesiredVelocity.GetClampedToMaxSize(SpeedLimitCmps);
    if ((Resolved.AllowedNavLayerMask
        & (uint64{1} << FMath::Min<uint32>(
          AttachedNavLayer, 63u))) == 0)
      DesiredVelocity = FVector::ZeroVector;
    if (!DesiredVelocity.IsNearlyZero()
      && Resolved.MovementGoal.bHasGoal)
    {
      const FCrowdNavSurfaceFlow* Flow = nullptr;
      if (!GetOrBuildFlow(
          Resolved.MovementGoal.Location, Flow,
          Slot.CachedGoalNodeId,
          &InOutSlots[SlotIndex].CachedGoalNodeId)
        || !Flow)
        DesiredVelocity = FVector::ZeroVector;
      else
      {
        const int32 CurrentIndex =
          AttachedNodeIndex;
        if (CurrentIndex == INDEX_NONE
          || !Flow->Nodes.IsValidIndex(CurrentIndex))
          DesiredVelocity = FVector::ZeroVector;
        else
        {
          FVector FlowDirection =
            Flow->Nodes[CurrentIndex].Direction;
          if (!FlowDirection.IsNearlyZero())
            DesiredVelocity =
              FlowDirection.GetSafeNormal()
                * DesiredVelocity.Size();
        }
      }
    }
    ResolvedVelocities[SlotIndex] =
      DesiredVelocity;
    ResolvedMaximumSpeeds[SlotIndex] =
      Resolved.bMovementLocked ? 0.0f : SpeedLimitCmps;
  }
  FCrowdMassBoundarySnapshot Snapshot;
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    static_cast<int32>(FixedStepIndex),
    static_cast<int32>(FixedStepIndex),
    Gathered,
    Snapshot);
  if (!Snapshot.bValid)
    return RejectBoundary(TEXT("snapshot"));

  FCrowdMassBoundaryWorkGraphInput PipelineInput;
  PipelineInput.Movement.Guidance.FixedStepIndex =
    Snapshot.FixedStepIndex;
  PipelineInput.Movement.Guidance.PlanRevision =
    Snapshot.PlanRevision;
  PipelineInput.Movement.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  PipelineInput.Movement.bRunLocalPredictive = true;
  PipelineInput.Movement.LocalPredictiveSettings.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  PipelineInput.Movement.LocalPredictiveSettings.TimeHorizonSeconds =
    0.05f;
  PipelineInput.Movement.LocalPredictiveSettings.SpatialCellSizeCm =
    Config.PopulationLimit >= 500 ? 120.0f : 200.0f;
  PipelineInput.Movement.LocalPredictiveSettings.JointIterationCount =
    1;
  bool bSourceSetChanged = false;
  for (const FCrowdBehaviorPreparedEntity& Entity
    : PreparedBehavior.Entities)
  {
    bSourceSetChanged |= !Entity.Events.IsEmpty();
  }
  if (!bSourceSetChanged)
    PipelineInput.Movement.PreviousGrantStates =
      MixedLocalPredictiveGrantStates;

  FBox MovementBounds(EForceInit::ForceInit);
  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
    MovementBounds += Agent.State.Position;
  for (const FCrowdNavSurfaceNode& Node : NavGraphHandle->Nodes)
    MovementBounds += Node.Center;
  if (!MovementBounds.IsValid)
    return RejectBoundary(TEXT("movement_bounds"));
  PipelineInput.Movement.Environment.Revision =
    Snapshot.PlanRevision;
  PipelineInput.Movement.Environment.BoundsMin = FVector(
    MovementBounds.Min.X - 2000.0,
    MovementBounds.Min.Y - 2000.0, 0.0);
  PipelineInput.Movement.Environment.BoundsMax = FVector(
    MovementBounds.Max.X + 2000.0,
    MovementBounds.Max.Y + 2000.0, 0.0);
  PipelineInput.Movement.Environment.CellSizeCm = 150.0f;
  PipelineInput.Movement.Environment.AgentInflateCm =
    MinimumSafeSeparationCm * 0.5f;

  PipelineInput.ParticleTemplate.Particle.FixedStepIndex =
    Snapshot.FixedStepIndex;
  PipelineInput.ParticleTemplate.Particle.PlanRevision =
    Snapshot.PlanRevision;
  PipelineInput.ParticleTemplate.Particle.Environment.FlowConfig =
    PipelineInput.Movement.Environment;
  PipelineInput.ParticleTemplate.Particle.Environment
    .bConstrainToFlowBounds = true;
  PipelineInput.ParticleTemplate.Particle.Settings.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  PipelineInput.ParticleTemplate.Particle.Settings.IterationCount =
    Config.PopulationLimit >= 500 ? 1 : 4;
  PipelineInput.ParticleTemplate.Particle.Settings.SafetyIterationCount =
    Config.PopulationLimit >= 500 ? 1 : 4;
  PipelineInput.ParticleTemplate.Particle.Settings.PositionQuantumCm =
    0.1f;
  PipelineInput.ParticleTemplate.Particle.Settings.VelocityQuantumCmps =
    0.1f;
  PipelineInput.FacingSettings.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);

  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
  {
    const FCrowdStableEntityRef Ref =
      Agent.AgentFacts.StableEntityRef;
    const int32 SlotIndex =
      Agent.Identity.AgentId;
    if (!ResolvedVelocities.IsValidIndex(SlotIndex)
      || !ResolvedMaximumSpeeds.IsValidIndex(SlotIndex)
      || !ResolvedInteractionLayers.IsValidIndex(SlotIndex))
      return RejectBoundary(TEXT("resolved_index"));
    const FVector& DesiredVelocity =
      ResolvedVelocities[SlotIndex];
    const float MaximumSpeed =
      ResolvedMaximumSpeeds[SlotIndex];
    const uint32 InteractionLayer =
      ResolvedInteractionLayers[SlotIndex];
    if (DesiredVelocity.ContainsNaN()
      || !FMath::IsFinite(MaximumSpeed)
      || MaximumSpeed < 0.0f)
      return RejectBoundary(TEXT("resolved_movement"));

    FCrowdMassGatherRecord& GuidanceRecord =
      PipelineInput.Movement.Guidance.Records
        .AddDefaulted_GetRef();
    GuidanceRecord.Identity = Agent.Identity;
    GuidanceRecord.AgentFacts = Agent.AgentFacts;
    GuidanceRecord.State = Agent.State;
    GuidanceRecord.Properties = Agent.Properties;
    const FVector& Facing =
      ResolvedFacings[SlotIndex];
    const float DesiredYawDegrees =
      !Facing.IsNearlyZero()
      ? Facing.Rotation().Yaw
      : DesiredVelocity.IsNearlyZero()
        ? Agent.State.YawDegrees
        : DesiredVelocity.Rotation().Yaw;
    GuidanceRecord.Guidance.TargetRegion =
      FCrowdGuidanceComposeKernel::BuildCandidate(
        Agent.Identity.AgentId,
        ECrowdGuidanceProvider::TargetRegion,
        Snapshot.PlanRevision,
        DesiredVelocity,
        Agent.State.Position
          + DesiredVelocity
            * static_cast<float>(MixedFixedStepSeconds),
        DesiredYawDegrees,
        true);

    FCrowdMassMovementPipelineAgentOverlay& Overlay =
      PipelineInput.Movement.AgentOverlays
        .AddDefaulted_GetRef();
    Overlay.AgentId = Agent.Identity.AgentId;
    Overlay.InteractionLayer = InteractionLayer;
    Overlay.PreviousBlockedAgeSteps =
      InOutSlots[
        static_cast<int32>(Ref.StableEntityId)]
        .PreviousBlockedAgeSteps;
    Overlay.MaximumSpeedCmps = MaximumSpeed;
    Overlay.BoundaryLocation = Agent.State.Position;
    Overlay.bVerticalOverride =
      !FMath::IsNearlyZero(DesiredVelocity.Z);
    Overlay.ProposedZ =
      Agent.State.Position.Z
      + DesiredVelocity.Z
        * static_cast<float>(MixedFixedStepSeconds);
    Overlay.VerticalVelocityCmps =
      DesiredVelocity.Z;
    Overlay.bParticleActive = true;

    FCrowdParticleConstraintAgent& ParticleAgent =
      PipelineInput.ParticleTemplate.Particle.Agents
        .AddDefaulted_GetRef();
    ParticleAgent.AgentId = Agent.Identity.AgentId;
    ParticleAgent.InteractionLayer = InteractionLayer;
    ParticleAgent.StartPosition = Agent.State.Position;
    ParticleAgent.PredictedPosition = Agent.State.Position;
    ParticleAgent.PhysicalRadiusCm =
      Agent.Properties.PhysicalRadiusCm;
    ParticleAgent.HardSafetyGapCm =
      Agent.Properties.HardSafetyGapCm;
    ParticleAgent.SoftMarginCm =
      Agent.Properties.SoftMarginCm;
    ParticleAgent.Mobility = Agent.Properties.Mobility;

    FCrowdMassBoundaryFacingTemplate& FacingTemplate =
      PipelineInput.FacingTemplates.AddDefaulted_GetRef();
    FacingTemplate.Input.AgentId = Agent.Identity.AgentId;
    FacingTemplate.Input.CurrentYawDegrees =
      Agent.State.YawDegrees;
    FacingTemplate.Input.Location = FVector2f(
      Agent.State.Position.X, Agent.State.Position.Y);
    if (!Facing.IsNearlyZero())
    {
      const FVector Target =
        Agent.State.Position + Facing.GetSafeNormal() * 100.0;
      FacingTemplate.Input.TargetLocation =
        FVector2f(Target.X, Target.Y);
      FacingTemplate.Input.bHasTarget = true;
    }
  }

  struct FMixedMovementWork
  {
    FCrowdMassCommitPlan Plan;
    TArray<FCrowdLocalPredictiveGrantState> GrantStates;
    TMap<int32, int32> BlockedAgeByAgentId;
    double MovementMilliseconds = 0.0;
    double ParticleMilliseconds = 0.0;
    double FacingMilliseconds = 0.0;
    int32 SafetyHolds = 0;
    int32 FailureCode = 0;
    bool bCompleted = false;
  };
  const TSharedRef<FMixedMovementWork, ESPMode::ThreadSafe> Work =
    MakeShared<FMixedMovementWork, ESPMode::ThreadSafe>();
  const double GatherEndSeconds =
    FPlatformTime::Seconds();
  FCrowdMassBoundaryRunner Runner;
  if (!Runner.Begin(Snapshot, 0.0))
    return RejectBoundary(TEXT("runner_begin"));
  PipelineInput.ParticleTemplate.Snapshot =
    MoveTemp(Snapshot);
  if (!Runner.AddTask(
      {{3}, {301}, 0}, {},
      [PipelineInput = MoveTemp(PipelineInput),
       ResolvedVelocities = MoveTemp(ResolvedVelocities),
       Work]()
      {
        const double MovementStart = FPlatformTime::Seconds();
        const FCrowdMassMovementPipelineWorkOutput Movement =
          FCrowdMassMovementPipelineWork::Run(
            PipelineInput.Movement);
        const double MovementEnd = FPlatformTime::Seconds();
        Work->MovementMilliseconds =
          (MovementEnd - MovementStart) * 1000.0;
        if (!Movement.bCompleted)
        {
          Work->FailureCode = 10;
          return FCrowdBoundaryTaskResult::Failure();
        }

        FCrowdMassParticlePipelineWorkInput ParticleInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildParticleInput(
            PipelineInput, Movement, ParticleInput))
        {
          Work->FailureCode = 11;
          return FCrowdBoundaryTaskResult::Failure();
        }

        const double ParticleStart = FPlatformTime::Seconds();
        const FCrowdMassParticlePipelineWorkOutput Particle =
          FCrowdMassParticlePipelineWork::Run(ParticleInput);
        const double ParticleEnd = FPlatformTime::Seconds();
        Work->ParticleMilliseconds =
          (ParticleEnd - ParticleStart) * 1000.0;
        if (!Particle.bCompleted)
        {
          Work->FailureCode = 12;
          return FCrowdBoundaryTaskResult::Failure();
        }

        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildFacingInput(
            PipelineInput, Movement, Particle, FacingInput))
        {
          Work->FailureCode = 13;
          return FCrowdBoundaryTaskResult::Failure();
        }
        const double FacingStart = FPlatformTime::Seconds();
        const FCrowdMassFacingFinalizeWorkOutput FacingFinalize =
          FCrowdMassFacingFinalizeWork::Run(FacingInput);
        Work->FacingMilliseconds =
          (FPlatformTime::Seconds() - FacingStart) * 1000.0;
        if (!FacingFinalize.bCompleted)
        {
          Work->FailureCode = 14;
          return FCrowdBoundaryTaskResult::Failure();
        }

        Work->Plan = FacingFinalize.Finalize.CommitPlan;
        Work->GrantStates =
          Movement.LocalPredictive.GrantStates;
        for (const FCrowdLocalPredictiveResult& Result
          : Movement.LocalPredictive.Results)
          Work->BlockedAgeByAgentId.Add(
            Result.AgentId, Result.NextBlockedAgeSteps);
        for (const FCrowdMassCommitRecord& Record
          : Work->Plan.Records)
        {
          const int32 AgentId =
            Record.Movement.AgentId;
          if (ResolvedVelocities.IsValidIndex(AgentId)
            && !ResolvedVelocities[AgentId].IsNearlyZero()
            && Record.Movement.Velocity.IsNearlyZero())
            ++Work->SafetyHolds;
        }
        uint64 WorkHash =
          FoldMixedHash(14695981039346656037ull,
            Movement.StableHash);
        WorkHash = FoldMixedHash(
          WorkHash, Particle.StableHash);
        WorkHash = FoldMixedHash(
          WorkHash, FacingFinalize.StableHash);
        WorkHash = FoldMixedHash(
          WorkHash, Work->Plan.StableHash);
        Work->bCompleted = true;
        return FCrowdBoundaryTaskResult::Success(WorkHash);
      }))
    return RejectBoundary(TEXT("runner_add"));
  if (!Runner.Dispatch() || !Runner.WaitAndDrain()
    || !Work->bCompleted
    )
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxBoundaryWorkReject code=%d fixed_step=%lld"),
      Work->FailureCode, FixedStepIndex);
    return RejectBoundary(TEXT("runner_work"));
  }
  const double WorkEndSeconds =
    FPlatformTime::Seconds();
  const double SafetyStartSeconds = FPlatformTime::Seconds();

  FCrowdMassMovementFinalizeWorkInput SafetyFinalizeInput;
  SafetyFinalizeInput.FixedStepIndex = Work->Plan.FixedStepIndex;
  SafetyFinalizeInput.PlanRevision = Work->Plan.PlanRevision;
  SafetyFinalizeInput.Records.Reserve(Work->Plan.Records.Num());
  for (const FCrowdMassCommitRecord& Record : Work->Plan.Records)
  {
    const int32 SlotIndex =
      static_cast<int32>(Record.EntityRef.StableEntityId);
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || !InOutSlots[SlotIndex].bActive
      || InOutSlots[SlotIndex].Facts.StableEntityRef != Record.EntityRef)
      return RejectBoundary(TEXT("safety_target"));
    const FSlotState& Slot = InOutSlots[SlotIndex];
    const bool bCandidateSafe =
      SpatialSafety.IsCandidateSafe(
        Record.EntityRef, Record.Movement.Position,
        MinimumSafeSeparationCm * 0.5f);
    const FVector SafePosition =
      bCandidateSafe ? Record.Movement.Position : Slot.Location;
    const FVector SafeVelocity =
      bCandidateSafe ? Record.Movement.Velocity : FVector::ZeroVector;
    if (!bCandidateSafe)
      ++Work->SafetyHolds;
    if (!ResolvedInteractionLayers.IsValidIndex(SlotIndex)
      || !SpatialSafety.Update(
        Record.EntityRef, SafePosition,
        ResolvedInteractionLayers[SlotIndex]))
      return RejectBoundary(TEXT("safety_update"));
    SafetyFinalizeInput.Records.Add({
      Record.EntityRef,
      Record.Movement.AgentId,
      Record.EntityRef.LifecycleSerial,
      Record.CapabilityProfileKey,
      SafePosition,
      SafeVelocity,
      Record.Movement.YawDegrees});
  }
  const FCrowdMassMovementFinalizeWorkOutput SafetyFinalize =
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(
      SafetyFinalizeInput);
  if (!SafetyFinalize.bCompleted
    || !SafetyFinalize.CommitPlan.bValid)
    return RejectBoundary(TEXT("safety_finalize"));
  Work->Plan = SafetyFinalize.CommitPlan;
  const double SafetyEndSeconds = FPlatformTime::Seconds();

  FCrowdBehaviorBoundaryMetadata BehaviorMetadata;
  for (const FCrowdBehaviorPreparedEntity& Entity
    : PreparedBehavior.Entities)
    BehaviorMetadata.SourceSetRevision = FMath::Max(
      BehaviorMetadata.SourceSetRevision,
      Entity.StagedSourceSet.Revision);
  BehaviorMetadata.SourceSetHash =
    PreparedBehavior.SourceSetHash;
  BehaviorMetadata.CommandBatchHash =
    PreparedBehavior.CommandBatchHash;
  BehaviorMetadata.ResolvedChannelHash =
    PreparedBehavior.ResolvedChannelHash;
  if (!Runner.BuildAndSealCommit(
      Work->Plan, {}, Targets, 0.0, &BehaviorMetadata))
    return RejectBoundary(TEXT("commit_envelope"));

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
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || !InOutSlots[SlotIndex].bActive
      || InOutSlots[SlotIndex].Facts.StableEntityRef
        != Record.EntityRef
      || !LifecycleWorld.TryGetEntityHandle(
        Record.EntityRef, Entity))
      return RejectBoundary(TEXT("commit_target"));
    FTransformFragment* Transform =
      EntitySubsystem->GetMutableEntityManager()
        .GetFragmentDataPtr<FTransformFragment>(Entity);
    if (!Transform)
      return RejectBoundary(TEXT("transform_fragment"));
    Writes.Add({&InOutSlots[SlotIndex], Transform, &Record});
  }
  if (!Runner.MarkValidated(0.0))
    return RejectBoundary(TEXT("mark_validated"));
  for (const FResolvedWrite& Write : Writes)
  {
    Write.Slot->Location =
      Write.Record->Movement.Position;
    Write.Slot->Velocity =
      Write.Record->Movement.Velocity;
    Write.Slot->YawDegrees =
      Write.Record->Movement.YawDegrees;
    if (const int32* BlockedAge =
      Work->BlockedAgeByAgentId.Find(
        Write.Record->Movement.AgentId))
      Write.Slot->PreviousBlockedAgeSteps =
        *BlockedAge;
    Write.Transform->GetMutableTransform().SetLocation(
      Write.Record->Movement.Position);
    Write.Transform->GetMutableTransform().SetRotation(
      FRotator(
        0.0f, Write.Record->Movement.YawDegrees, 0.0f)
        .Quaternion());
  }
  MixedLocalPredictiveGrantStates =
    MoveTemp(Work->GrantStates);
  OutSafetyHolds = Work->SafetyHolds;
  OutCommitHash = Runner.GetCommitEnvelope().StableHash;
  checkf(Runner.MarkCommitted(0.0),
    TEXT("Validated Mixed boundary failed during final apply"));
  if (Config.PopulationLimit == MaximumMixedPopulation
    && (FixedStepIndex <= 5 || FixedStepIndex % 150 == 0))
  {
    const double ProductEndSeconds =
      FPlatformTime::Seconds();
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxMovementPhases fixed_step=%lld gather_ms=%.3f runner_ms=%.3f movement_ms=%.3f particle_ms=%.3f facing_ms=%.3f safety_finalize_ms=%.3f commit_ms=%.3f"),
      FixedStepIndex,
      (GatherEndSeconds - ProductStartSeconds) * 1000.0,
      (WorkEndSeconds - GatherEndSeconds) * 1000.0,
      Work->MovementMilliseconds,
      Work->ParticleMilliseconds,
      Work->FacingMilliseconds,
      (SafetyEndSeconds - SafetyStartSeconds) * 1000.0,
      (ProductEndSeconds - SafetyEndSeconds) * 1000.0);
  }
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::GetOrBuildFlow(
  const FVector& Objective,
  const FCrowdNavSurfaceFlow*& OutFlow,
  const uint64 PreferredGoalNodeId,
  uint64* const OutGoalNodeId)
{
  OutFlow = nullptr;
  UWorld* World = GetWorld();
  UMassCrowdRuntimeSubsystem* Runtime =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!Runtime || !NavGraphHandle.IsValid()) return false;
  const FCrowdNavGraphResource& Resource = Runtime->GetNavGraphResource();
  if (!Resource.IsReady() || Resource.Graph != NavGraphHandle) return false;
  uint64 GoalNodeId = PreferredGoalNodeId;
  uint32 GoalLayer = 0;
  const int32 PreferredIndex =
    NavGraphHandle->FindNodeIndex(
      PreferredGoalNodeId);
  if (NavGraphHandle->Nodes.IsValidIndex(
      PreferredIndex)
    && DistanceSquaredToNavNode(
      Objective,
      NavGraphHandle->Nodes[PreferredIndex])
      <= FMath::Square(350.0))
  {
    GoalLayer =
      NavGraphHandle->Nodes[PreferredIndex].NavLayer;
  }
  else
  {
    GoalNodeId = 0;
    if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
      *NavGraphHandle, Objective, 350.0f,
      GoalNodeId, GoalLayer))
      return false;
  }
  if (OutGoalNodeId)
    *OutGoalNodeId = GoalNodeId;
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
      MinimumSafeSeparationCm * 0.5f,
      Slot.InteractionLayer});
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
    OutOperation.NewMembershipKey = MembershipForDiagnosticLabel(
      static_cast<ECrowdActiveBehavior>(MakeAgentFacts(
        PendingRespawnSlot,
        OutOperation.LifecycleSerial).DerivedBehaviorLabel));
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
    const uint32 Desired = MembershipForDiagnosticLabel(
      static_cast<ECrowdActiveBehavior>(Slot.Facts.DerivedBehaviorLabel));
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
    FCrowdCapabilityBinding Binding;
    Binding.ProfileKey =
        CrowdDemoBehaviorSchemas::FullProfile;
      if (!BehaviorSourceRuntime->RegisterEntity(
          Slot.Facts.StableEntityRef, Binding))
        return false;
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
      if (!BehaviorSourceRuntime->RemoveEntity(Ref))
        return false;
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

  if (Operation.Kind
    != ECrowdDemoContinuousLifecycleOperationKind::Membership)
    MixedLocalPredictiveGrantStates.Reset();
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
  TArray<FCrowdReliableStateRecord> Records;
  TArray<FCrowdMovementCorrectionRecord> Corrections;
  Records.Reserve(Config.PopulationLimit);
  Corrections.Reserve(Config.PopulationLimit);
  constexpr int32 MaximumSourceSetRecordsPerFrame = 32;
  int32 SourceSetRecordCount = 0;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    const FCrowdDemoMixedAgentState State{
      static_cast<uint64>(SlotIndex),
      Slot.Facts.StableEntityRef.LifecycleSerial,
      Slot.MembershipKey,
      Slot.Location,
      Slot.Facts.DerivedBehaviorLabel,
      static_cast<uint8>(FMath::Clamp(Slot.Health, 0, 100)),
      Slot.Facts.TargetRef.ProviderId,
      Slot.Facts.TargetRef.StableEntityId,
      Slot.Facts.TargetRef.LifecycleSerial,
      Slot.Facts.BusinessTaskRef.ProviderId,
      Slot.Facts.BusinessTaskRef.StableEntityId,
      Slot.Facts.BusinessTaskRef.LifecycleSerial};
    const FCrowdStableEntityRef ReplicatedEntityRef{
      1, State.StableEntityId, State.LifecycleSerial};
    const uint64 HostFactHash =
      CalculateMixedHostFactHash(State);
    const uint64* LastHostFactHash =
      LastPublishedHostFactHashes.Find(ReplicatedEntityRef);
    if (!LastHostFactHash || *LastHostFactHash != HostFactHash)
    {
      FCrowdReliableStateRecord& Record =
        Records.AddDefaulted_GetRef();
      Record.Sequence = NextStateSequence++;
      Record.Kind = ECrowdReliableStateKind::HostEvent;
      Record.EntityRef = ReplicatedEntityRef;
      Record.Revision = static_cast<uint32>(
        FMath::Max<int64>(1, FixedStepIndex));
      EncodeMixedAgent(
        State, FixedStepIndex, NextLifecycleSequence,
        RelevantSetRevision, Record.Payload);
      Record.StableHash =
        FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
      LastPublishedHostFactHashes.Add(
        ReplicatedEntityRef, HostFactHash);
    }
    const FCrowdBehaviorSourceSet* SourceSet =
      BehaviorSourceRuntime
        ? BehaviorSourceRuntime->FindSourceSet(
          ReplicatedEntityRef)
        : nullptr;
    const FCrowdResolvedBehaviorChannels* Resolved =
      BehaviorSourceRuntime
        ? BehaviorSourceRuntime->FindResolvedChannels(
          ReplicatedEntityRef)
        : nullptr;
    const uint32* LastPublishedRevision =
      LastPublishedSourceSetRevisions.Find(ReplicatedEntityRef);
    if (SourceSetRecordCount
        < MaximumSourceSetRecordsPerFrame
      && SourceSet && Resolved && Resolved->bValid
      && (!LastPublishedRevision
        || *LastPublishedRevision != SourceSet->Revision))
    {
      FCrowdBehaviorSourceSetReplicationRecord SourceRecord;
      SourceRecord.RegistryHash =
        BehaviorSourceRuntime->GetRegistryHash();
      SourceRecord.ContextSchemaHash =
        BehaviorSourceRuntime->GetContextSchemaHash();
      SourceRecord.SourceSet = *SourceSet;
      SourceRecord.SourceSet.Instances.RemoveAll(
        [](const FCrowdBehaviorSourceInstance& Instance)
        {
          return Instance.ReplicationPolicy
            != ECrowdBehaviorSourceReplicationPolicy::Predictable;
        });
      SourceRecord.SourceSet.RecalculateStableHash();
      SourceRecord.ResolvedBehaviorHash = Resolved->StableHash;
      SourceRecord.DerivedDiagnosticLabel =
        State.DerivedBehaviorLabel;
      TArray<uint8> SourcePayload;
      if (!FCrowdReplicationCodec::EncodeBehaviorSourceSet(
          SourceRecord, SourcePayload))
      {
        ++StaleRejectCount;
        return;
      }
      FCrowdReliableStateRecord& ReplicatedSource =
        Records.AddDefaulted_GetRef();
      ReplicatedSource.Sequence = NextStateSequence++;
      ReplicatedSource.Kind =
        ECrowdReliableStateKind::BehaviorSourceSet;
      ReplicatedSource.EntityRef = ReplicatedEntityRef;
      ReplicatedSource.Revision = SourceSet->Revision;
      ReplicatedSource.Payload = MoveTemp(SourcePayload);
      ReplicatedSource.StableHash =
        FCrowdReplicationTransport::CalculateReliableRecordHash(
          ReplicatedSource);
      LastPublishedSourceSetRevisions.Add(
        ReplicatedEntityRef, SourceSet->Revision);
      ++SourceSetRecordCount;
    }
    FCrowdMovementCorrectionRecord& Correction =
      Corrections.AddDefaulted_GetRef();
    Correction.EntityRef = ReplicatedEntityRef;
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
      if (!Channel->IsServerAwaitingBaselineAck())
        Channel->PublishMovementCorrections(Corrections);
    }
  }
}

void ACrowdDemoMixedSandboxCoordinator::RefreshReplicationChannels()
{
  UWorld* World = GetWorld();
  if (!World) return;
  for (auto It = ReplicationChannelEligibleSeconds.CreateIterator();
    It; ++It)
    if (!It.Key().IsValid())
      It.RemoveCurrent();
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
      LastPublishedSourceSetRevisions.Reset();
      Channel->Destroy();
      It.RemoveCurrent();
    }
  }
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
    It; ++It)
  {
    APlayerController* Controller = It->Get();
    if (!Controller || ReplicationChannels.Contains(Controller)) continue;
    double* EligibleSeconds =
      ReplicationChannelEligibleSeconds.Find(Controller);
    if (!EligibleSeconds)
    {
      ReplicationChannelEligibleSeconds.Add(
        Controller, World->GetTimeSeconds() + 1.0);
      ForceNetUpdate();
      continue;
    }
    if (World->GetTimeSeconds() < *EligibleSeconds)
      continue;
    AMassCrowdReplicationActor* Channel =
      AMassCrowdReplicationActor::SpawnForController(*Controller);
    if (!Channel || !PublishBaseline(*Channel))
    {
      if (Channel) Channel->Destroy();
      *EligibleSeconds = World->GetTimeSeconds() + 1.0;
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoMixedSandboxReplication stage=baseline_retry owner=%s"),
        *GetNameSafe(Controller));
      continue;
    }
    LastPublishedSourceSetRevisions.Reset();
    ReplicationChannels.Add(Controller, Channel);
    ReplicationChannelEligibleSeconds.Remove(Controller);
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
      Slot.Facts.DerivedBehaviorLabel,
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
  Limits.MaxEntitiesPerChunk =
    FMath::Min(128, FMath::Max(1, Config.PopulationLimit));
  Limits.MaxChunkPayloadBytes = 64 * 1024;
  Limits.MaxTotalPayloadBytes = 16 * 1024 * 1024;
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
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxReplication stage=baseline_build_reject entities=%d population_limit=%d chunk_entities=%d"),
      Entities.Num(), Config.PopulationLimit, Limits.MaxEntitiesPerChunk);
    return false;
  }
  if (!Channel.PublishBaseline(
    Header, Chunks, FMath::Max<uint64>(1, NextStateSequence)))
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxReplication stage=baseline_publish_reject revision=%u entities=%d chunks=%d resume=%llu"),
      Header.SnapshotRevision, Header.EntityCount, Header.ChunkCount,
      FMath::Max<uint64>(1, NextStateSequence));
    return false;
  }
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    const FCrowdDemoMixedAgentState State{
      static_cast<uint64>(SlotIndex),
      Slot.Facts.StableEntityRef.LifecycleSerial,
      Slot.MembershipKey,
      Slot.Location,
      Slot.Facts.DerivedBehaviorLabel,
      static_cast<uint8>(FMath::Clamp(Slot.Health, 0, 100)),
      Slot.Facts.TargetRef.ProviderId,
      Slot.Facts.TargetRef.StableEntityId,
      Slot.Facts.TargetRef.LifecycleSerial,
      Slot.Facts.BusinessTaskRef.ProviderId,
      Slot.Facts.BusinessTaskRef.StableEntityId,
      Slot.Facts.BusinessTaskRef.LifecycleSerial};
    LastPublishedHostFactHashes.Add(
      Slot.Facts.StableEntityRef,
      CalculateMixedHostFactHash(State));
  }
  return true;
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
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=begin revision=%u entities=%d"),
      Channel->GetCompletedBaselineRevision(),
      Channel->GetCompletedBaselineEntities().Num());
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
    if (!BehaviorSourceRuntime)
    {
      Channel->RequestResync();
      ++StaleRejectCount;
      return;
    }
    for (FSlotState& Slot : Slots)
    {
      if (Slot.bActive && Slot.Facts.StableEntityRef.IsValid()
        && BehaviorSourceRuntime->FindSourceSet(
          Slot.Facts.StableEntityRef))
        BehaviorSourceRuntime->RemoveEntity(
          Slot.Facts.StableEntityRef);
      Slot.bActive = false;
    }
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
        || State.DerivedBehaviorLabel >= static_cast<uint32>(
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
      Slot.Facts.DerivedBehaviorLabel = State.DerivedBehaviorLabel;
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
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=lifecycle_ready revision=%u"),
      Channel->GetCompletedBaselineRevision());
    for (int32 SlotIndex = 1;
      SlotIndex < Slots.Num(); ++SlotIndex)
    {
      if (!Slots[SlotIndex].bActive) continue;
      FCrowdCapabilityBinding Binding;
      Binding.ProfileKey =
        CrowdDemoBehaviorSchemas::FullProfile;
      if (!BehaviorSourceRuntime->RegisterEntity(
          Slots[SlotIndex].Facts.StableEntityRef, Binding))
      {
        Channel->RequestResync();
        ++StaleRejectCount;
        return;
      }
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=sources_ready revision=%u"),
      Channel->GetCompletedBaselineRevision());
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
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=complete revision=%u fixed_step=%lld"),
      LastConsumedBaselineRevision, LastReceivedFixedStep);
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
        if (Record.Kind == ECrowdReliableStateKind::HostEvent)
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
        else if (Record.Kind
          == ECrowdReliableStateKind::BehaviorSourceSet)
        {
          FCrowdBehaviorSourceSetReplicationRecord SourceRecord;
          if (!BehaviorSourceRuntime
            || !FCrowdReplicationCodec::DecodeBehaviorSourceSet(
              Record.Payload,
              BehaviorSourceRuntime->GetRegistryHash(),
              BehaviorSourceRuntime->GetContextSchemaHash(),
              SourceRecord)
            || SourceRecord.SourceSet.EntityRef
              != Record.EntityRef
            || !BehaviorSourceRuntime->ApplyReplicatedSourceSet(
              SourceRecord.SourceSet))
          {
            Channel->RequestResync();
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
    || State.DerivedBehaviorLabel >=
      static_cast<uint32>(ECrowdActiveBehavior::Count))
  {
    if (!bClientApplyFailureLogged)
    {
      bClientApplyFailureLogged = true;
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoMixedSandboxClientApplyReject reason=invalid_payload entity=%llu lifecycle=%u behavior=%u step=%lld slots=%d"),
        State.StableEntityId, State.LifecycleSerial,
        State.DerivedBehaviorLabel,
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
  Slot.Facts.DerivedBehaviorLabel = State.DerivedBehaviorLabel;
  Slot.Facts.TargetRef = {
    State.TargetProviderId,
    State.TargetStableEntityId,
    State.TargetLifecycleSerial};
  Slot.Facts.BusinessTaskRef = {
    State.TaskProviderId,
    State.TaskStableEntityId,
    State.TaskLifecycleSerial};
  Slot.Health = State.Health;
  SeenBehaviorBits |= BehaviorBit(
    static_cast<ECrowdActiveBehavior>(Slot.Facts.DerivedBehaviorLabel));
  LastReceivedFixedStep = FMath::Max(LastReceivedFixedStep, InFixedStepIndex);
  const bool bApplied = LifecycleWorld.ApplyAgentFactsCorrectionAtBoundary(
    FMath::Max(InFixedStepIndex, LifecycleWorld.GetLastAppliedFixedStep()),
    Slot.Facts);
  if (!bApplied && !bClientApplyFailureLogged)
  {
    bClientApplyFailureLogged = true;
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxClientApplyReject reason=runtime_facts entity=%llu lifecycle=%u behavior=%u well_formed=%d step=%lld world_step=%lld"),
      State.StableEntityId, State.LifecycleSerial,
      State.DerivedBehaviorLabel,
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
        Slot.Facts.DerivedBehaviorLabel ==
          static_cast<uint32>(ECrowdActiveBehavior::Attack) ? 1 : 0;
      if (Slot.Facts.DerivedBehaviorLabel ==
        static_cast<uint32>(ECrowdActiveBehavior::HaulDeliver))
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
  float MinimumHaulObjectiveDistanceCm =
    TNumericLimits<float>::Max();
  float ClosestHaulObjectiveDistance2DCm =
    TNumericLimits<float>::Max();
  float ClosestHaulObjectiveHeightDeltaCm =
    TNumericLimits<float>::Max();
  float MaximumHaulObjectiveDistanceCm = 0.0f;
  int32 MovingHaulAgentCount = 0;
  for (int32 SlotIndex = 1;
    SlotIndex <= 6 && Slots.IsValidIndex(SlotIndex);
    ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    const bool bCarrying =
      BusinessLedger.GetCargoCarrier(
        static_cast<uint64>(SlotIndex))
      == static_cast<uint64>(SlotIndex);
    const FVector Objective = Marker(
      bCarrying ? TEXT("CrowdNavHigh") : TEXT("CrowdNavLower"),
      FVector::ZeroVector);
    const float Distance =
      FVector::Distance(Slot.Location, Objective);
    if (Distance < MinimumHaulObjectiveDistanceCm)
    {
      MinimumHaulObjectiveDistanceCm = Distance;
      ClosestHaulObjectiveDistance2DCm =
        FVector::Dist2D(Slot.Location, Objective);
      ClosestHaulObjectiveHeightDeltaCm =
        static_cast<float>(FMath::Abs(
          Slot.Location.Z - Objective.Z));
    }
    MaximumHaulObjectiveDistanceCm = FMath::Max(
      MaximumHaulObjectiveDistanceCm, Distance);
    if (!Slot.Velocity.IsNearlyZero())
      ++MovingHaulAgentCount;
  }
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoMixedSandboxCheckpoint role=%s fixed_step=%lld state_sequence=%llu active=%d visible=%d transitions=%d seen_behavior_bits=0x%08x pickups=%d deliveries=%d combat_quantity=%d commits=%d duplicate_commits=%d spawned=%d despawned=%d membership=%d max_population=%d safety_holds=%d min_separation_cm=%.2f haul_distance_cm=%.2f..%.2f haul_min_2d_cm=%.2f haul_min_z_cm=%.2f haul_moving=%d stale_reject=%d entity_hash=%llu membership_hash=%llu commit_hash=%llu source=MassCrowdBoundaryRunner+MassCrowdNavRuntime+ApplyFrame"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    HasAuthority() ? FixedStepIndex : LastReceivedFixedStep,
    HasAuthority() ? NextStateSequence - 1 : LastReceivedStateSequence,
    LifecycleWorld.GetActiveEntityCount(), Visible, BehaviorTransitionCount,
    SeenBehaviorBits,
    BusinessLedger.GetPickupCount(), BusinessLedger.GetDeliveryCount(),
    BusinessLedger.GetCombatHitQuantity(13) + BusinessLedger.GetCombatHitQuantity(14)
      + BusinessLedger.GetCombatHitQuantity(15) + BusinessLedger.GetCombatHitQuantity(16),
    BusinessLedger.GetAppliedCommitCount(), DuplicateCommitCount,
    SpawnCount, DespawnCount, MembershipChangeCount, MaxObservedPopulation,
    SafetyHoldCount, MinimumSeparationCm,
    MinimumHaulObjectiveDistanceCm,
    MaximumHaulObjectiveDistanceCm,
    ClosestHaulObjectiveDistance2DCm,
    ClosestHaulObjectiveHeightDeltaCm,
    MovingHaulAgentCount, StaleRejectCount,
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

uint32 ACrowdDemoMixedSandboxCoordinator::MembershipForDiagnosticLabel(
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
