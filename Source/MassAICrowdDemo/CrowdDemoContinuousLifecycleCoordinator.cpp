#include "CrowdDemoContinuousLifecycleCoordinator.h"

#include "CrowdDemoReplicator.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "MassCrowdRuntimeFragments.h"
#include "Mass/CrowdDemoPresentationAdapter.h"
#include "MassCrowdPresentationSubsystem.h"
#include "MassCrowdReplicationActor.h"
#include "MassEntitySubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
  constexpr double ContinuousFixedStepSeconds = 1.0 / 30.0;
  constexpr int32 OperationIntervalSteps = 15;

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

  void EncodeContinuousEntity(
    const int64 FixedStepIndex,
    const uint64 ResumeSequence,
    const uint32 RelevantSetRevision,
    const uint64 StableEntityId,
    const uint32 LifecycleSerial,
    const uint32 MembershipKey,
    TArray<uint8>& OutBytes)
  {
    OutBytes.Reset();
    WriteU64(OutBytes, static_cast<uint64>(FixedStepIndex));
    WriteU64(OutBytes, ResumeSequence);
    WriteU32(OutBytes, RelevantSetRevision);
    WriteU64(OutBytes, StableEntityId);
    WriteU32(OutBytes, LifecycleSerial);
    WriteU32(OutBytes, MembershipKey);
  }

  bool DecodeContinuousEntity(
    const TConstArrayView<uint8> Bytes,
    int64& OutFixedStepIndex,
    uint64& OutResumeSequence,
    uint32& OutRelevantSetRevision,
    uint64& OutStableEntityId,
    uint32& OutLifecycleSerial,
    uint32& OutMembershipKey)
  {
    uint64 Step = 0;
    int32 Offset = 0;
    if (!ReadU64(Bytes, Offset, Step)
      || !ReadU64(Bytes, Offset, OutResumeSequence)
      || !ReadU32(Bytes, Offset, OutRelevantSetRevision)
      || !ReadU64(Bytes, Offset, OutStableEntityId)
      || !ReadU32(Bytes, Offset, OutLifecycleSerial)
      || !ReadU32(Bytes, Offset, OutMembershipKey)
      || Offset != Bytes.Num())
      return false;
    OutFixedStepIndex = static_cast<int64>(Step);
    return true;
  }

  void EncodeContinuousOperation(
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

  bool DecodeContinuousOperation(
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

  ACrowdDemoReplicator* FindContinuousVisualHost(UWorld& World)
  {
    for (TActorIterator<ACrowdDemoReplicator> It(&World); It; ++It)
    {
      if (*It && !It->IsLocalVisualHostOnly()) return *It;
    }
    return nullptr;
  }

  bool IsMassEntityManagerIdle(UWorld& World)
  {
    const UMassEntitySubsystem* EntitySubsystem =
      World.GetSubsystem<UMassEntitySubsystem>();
    return EntitySubsystem
      && !EntitySubsystem->GetEntityManager().IsProcessing();
  }
}

ACrowdDemoContinuousLifecycleCoordinator::ACrowdDemoContinuousLifecycleCoordinator()
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.TickGroup = TG_PostUpdateWork;
  PrimaryActorTick.EndTickGroup = TG_PostUpdateWork;
  bReplicates = true;
  bAlwaysRelevant = true;
  SetNetUpdateFrequency(30.0f);
  SetMinNetUpdateFrequency(15.0f);
}

void ACrowdDemoContinuousLifecycleCoordinator::BeginPlay()
{
  Super::BeginPlay();
  FParse::Value(
    FCommandLine::Get(),
    TEXT("CrowdDemoContinuousStartDelay="),
    StartDelaySeconds);
  StartDelaySeconds = FMath::Max(StartDelaySeconds, 1.0f);
  if (!HasAuthority()) return;

  Config.bValid = 1;
  Config.InitialEntityCount = 10;
  Config.PopulationLimit = 20;
  Config.SnapshotRevision = 1;
  Config.InitialRelevantSetRevision = 1;
  Config.InitialFixedStepIndex = 0;
  if (!InitializeLifecycleWorld())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoContinuousLifecycle role=server stage=initialize"));
    Config.bValid = 0;
  }
  ForceNetUpdate();
}

void ACrowdDemoContinuousLifecycleCoordinator::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  UWorld* World = GetWorld();
  if (!World || !bWorldInitialized) return;

  if (HasAuthority())
  {
    RefreshReplicationChannels();
  }
  else
  {
    ConsumeProductReplication();
  }

  if (HasAuthority() && World->GetTimeSeconds() >= StartDelaySeconds)
  {
    FixedStepAccumulatorSeconds += FMath::Max(DeltaSeconds, 0.0f);
    int32 Steps = 0;
    while (FixedStepAccumulatorSeconds >= ContinuousFixedStepSeconds && Steps < 8)
    {
      FixedStepAccumulatorSeconds -= ContinuousFixedStepSeconds;
      AdvanceServerFixedStep();
      ++Steps;
    }
  }

  if (!HasAuthority() && bVisualSyncPending)
  {
    SyncClientVisualsIncremental();
  }
  if (World->GetTimeSeconds() - LastCheckpointWorldSeconds >= 5.0)
  {
    LastCheckpointWorldSeconds = World->GetTimeSeconds();
    LogCheckpoint();
  }
}

void ACrowdDemoContinuousLifecycleCoordinator::GetLifetimeReplicatedProps(
  TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(ACrowdDemoContinuousLifecycleCoordinator, Config);
}

void ACrowdDemoContinuousLifecycleCoordinator::OnRep_Config()
{
  if (!HasAuthority() && Config.bValid != 0 && !bWorldInitialized)
  {
    if (!InitializeLifecycleWorld())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoContinuousLifecycle role=client stage=initialize"));
    }
  }
}

bool ACrowdDemoContinuousLifecycleCoordinator::InitializeLifecycleWorld()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem || Config.bValid == 0 || Config.PopulationLimit <= 0) return false;

  FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
  const TArray<const UScriptStruct*> Types = {
    FCrowdMassAgentFragment::StaticStruct(),
    FCrowdMassBehaviorFragment::StaticStruct(),
    FCrowdMassMembershipFragment::StaticStruct(),
    FCrowdMassAgentTag::StaticStruct()};
  const FMassArchetypeHandle Archetype = EntityManager.CreateArchetype(Types);
  if (!Archetype.IsValid()) return false;
  LifecycleArchetype = Archetype;

  Slots.SetNum(Config.PopulationLimit + 1);
  TArray<FCrowdLifecycleSnapshotEntity> Snapshot;
  for (int32 SlotIndex = 1; SlotIndex <= Config.InitialEntityCount; ++SlotIndex)
  {
    FSlotState& Slot = Slots[SlotIndex];
    Slot.LifecycleSerial = 1;
    Slot.MembershipKey = 1 + static_cast<uint32>(SlotIndex % 3);
    Slot.bActive = true;
    Snapshot.Add(FCrowdLifecycleSnapshotEntity{
      MakeAgentFacts(SlotIndex, Slot.LifecycleSerial), Slot.MembershipKey});
  }
  FCrowdLifecycleBatchLimits Limits;
  Limits.MaxSnapshotEntities = Config.PopulationLimit;
  Limits.MaxEntriesPerBatch = Config.PopulationLimit;
  Limits.MaxTrackedSlots = Config.PopulationLimit;
  Limits.MaxSequenceHistory = 128;
  if (!LifecycleWorld.InitializeFromSnapshot(
    EntityManager,
    Archetype,
    Config.SnapshotRevision,
    Config.InitialFixedStepIndex,
    Config.InitialRelevantSetRevision,
    Snapshot,
    Limits)) return false;

  FixedStepIndex = Config.InitialFixedStepIndex;
  RelevantSetRevision = Config.InitialRelevantSetRevision;
  NextSequence = 1;
  MaxObservedPopulation = Config.InitialEntityCount;
  bWorldInitialized = true;
  bVisualSyncPending = !HasAuthority();
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoContinuousLifecycle role=%s stage=initialized active=%d limit=%d hash=%llu source=RuntimeLifecycle"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    LifecycleWorld.GetActiveEntityCount(),
    Config.PopulationLimit,
    LifecycleWorld.CalculateEntitySetHash());
  return true;
}

void ACrowdDemoContinuousLifecycleCoordinator::RefreshReplicationChannels()
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
  for (FConstPlayerControllerIterator It =
    World->GetPlayerControllerIterator(); It; ++It)
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

bool ACrowdDemoContinuousLifecycleCoordinator::PublishBaseline(
  AMassCrowdReplicationActor& Channel)
{
  TArray<FCrowdRelevantSnapshotEntityPayload> Entities;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    EncodeContinuousEntity(
      FixedStepIndex,
      FMath::Max<uint64>(1, NextSequence),
      RelevantSetRevision,
      static_cast<uint64>(SlotIndex),
      Slot.LifecycleSerial,
      Slot.MembershipKey,
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
    Header, Chunks, FMath::Max<uint64>(1, NextSequence));
}

void ACrowdDemoContinuousLifecycleCoordinator::PublishLifecycleOperation(
  const FCrowdDemoContinuousLifecycleOperation& Operation)
{
  FCrowdReliableStateRecord Record;
  Record.Sequence = Operation.Sequence;
  Record.EntityRef = {
    1, Operation.StableEntityId, Operation.LifecycleSerial};
  Record.Revision = Operation.RelevantSetRevision;
  switch (Operation.Kind)
  {
  case ECrowdDemoContinuousLifecycleOperationKind::Spawn:
    Record.Kind = ECrowdReliableStateKind::Spawn;
    break;
  case ECrowdDemoContinuousLifecycleOperationKind::Despawn:
    Record.Kind = ECrowdReliableStateKind::Despawn;
    break;
  case ECrowdDemoContinuousLifecycleOperationKind::Membership:
    Record.Kind = ECrowdReliableStateKind::Membership;
    break;
  default:
    return;
  }
  EncodeContinuousOperation(Operation, Record.Payload);
  Record.StableHash =
    FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
  for (const TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
    : ReplicationChannels)
    if (AMassCrowdReplicationActor* Channel = Pair.Value.Get())
      Channel->PublishReliable(Record);
}

void ACrowdDemoContinuousLifecycleCoordinator::ConsumeProductReplication()
{
  UWorld* World = GetWorld();
  if (!World || !bWorldInitialized
    || !IsMassEntityManagerIdle(*World)) return;
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
    uint64 ResumeSequence = 0;
    uint32 BaselineRelevantRevision = 0;
    for (FSlotState& Slot : Slots) Slot.bActive = false;
    for (const FCrowdRelevantSnapshotEntityPayload& Entity
      : Channel->GetCompletedBaselineEntities())
    {
      int64 EntityStep = 0;
      uint64 EntityResume = 0;
      uint32 EntityRelevantRevision = 0;
      uint64 StableEntityId = 0;
      uint32 LifecycleSerial = 0;
      uint32 MembershipKey = 0;
      if (!DecodeContinuousEntity(
          Entity.Bytes,
          EntityStep,
          EntityResume,
          EntityRelevantRevision,
          StableEntityId,
          LifecycleSerial,
          MembershipKey)
        || StableEntityId == 0
        || StableEntityId >= static_cast<uint64>(Slots.Num())
        || LifecycleSerial == 0
        || MembershipKey == 0
        || (BaselineStep != INDEX_NONE
          && (BaselineStep != EntityStep
            || ResumeSequence != EntityResume
            || BaselineRelevantRevision
              != EntityRelevantRevision)))
      {
        ++StaleRejectCount;
        return;
      }
      if (BaselineStep == INDEX_NONE)
      {
        BaselineStep = EntityStep;
        ResumeSequence = EntityResume;
        BaselineRelevantRevision = EntityRelevantRevision;
      }
      FSlotState& Slot = Slots[static_cast<int32>(StableEntityId)];
      Slot.LifecycleSerial = LifecycleSerial;
      Slot.MembershipKey = MembershipKey;
      Slot.bActive = true;
      Snapshot.Add({
        MakeAgentFacts(
          static_cast<int32>(StableEntityId), LifecycleSerial),
        MembershipKey});
    }
    if (BaselineStep < 0 || ResumeSequence == 0
      || BaselineRelevantRevision == 0)
    {
      ++StaleRejectCount;
      return;
    }
    Snapshot.Sort([](const auto& A, const auto& B)
    {
      return A.AgentFacts.StableEntityRef
        < B.AgentFacts.StableEntityRef;
    });
    FCrowdLifecycleBatchLimits Limits;
    Limits.MaxSnapshotEntities = Config.PopulationLimit;
    Limits.MaxEntriesPerBatch = Config.PopulationLimit;
    Limits.MaxTrackedSlots = Config.PopulationLimit;
    Limits.MaxSequenceHistory = 128;
    if (bPresentationProfileRegistered)
    {
      UMassCrowdPresentationSubsystem* Presentation =
        World->GetSubsystem<UMassCrowdPresentationSubsystem>();
      if (!Presentation || !Presentation->ResetProfile(1))
      {
        ++StaleRejectCount;
        return;
      }
    }
    LifecycleWorld.Reset();
    if (!LifecycleWorld.InitializeFromSnapshot(
        EntitySubsystem->GetMutableEntityManager(),
        LifecycleArchetype,
        Channel->GetCompletedBaselineRevision(),
        BaselineStep,
        BaselineRelevantRevision,
        Snapshot,
        Limits,
        ResumeSequence))
    {
      ++StaleRejectCount;
      return;
    }
    FixedStepIndex = BaselineStep;
    NextSequence = ResumeSequence;
    RelevantSetRevision = BaselineRelevantRevision;
    LastConsumedBaselineRevision =
      Channel->GetCompletedBaselineRevision();
    bVisualSyncPending = true;
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
    if (Frame.Kind != ECrowdReplicationApplyFrameKind::ReliableState)
      continue;
    for (const FCrowdReliableStateRecord& Record : Frame.ReliableRecords)
    {
      if (Record.Kind != ECrowdReliableStateKind::Spawn
        && Record.Kind != ECrowdReliableStateKind::Despawn
        && Record.Kind != ECrowdReliableStateKind::Membership)
      {
        ++StaleRejectCount;
        return;
      }
      FCrowdDemoContinuousLifecycleOperation Operation;
      if (!DecodeContinuousOperation(Record.Payload, Operation)
        || Operation.Sequence != Record.Sequence
        || !ApplyOperation(Operation))
      {
        ++StaleRejectCount;
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoContinuousLifecycle role=client stage=product_channel sequence=%llu"),
          Record.Sequence);
        return;
      }
    }
  }
}

void ACrowdDemoContinuousLifecycleCoordinator::AdvanceServerFixedStep()
{
  ++FixedStepIndex;
  if (FixedStepIndex % OperationIntervalSteps != 0) return;
  UWorld* World = GetWorld();
  if (!World || !IsMassEntityManagerIdle(*World)) return;
  FCrowdDemoContinuousLifecycleOperation Operation;
  if (!BuildNextServerOperation(Operation)) return;
  if (!ApplyOperation(Operation))
  {
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoContinuousLifecycle role=server stage=apply sequence=%llu kind=%d fixed_step=%lld"),
      Operation.Sequence,
      static_cast<int32>(Operation.Kind),
      Operation.FixedStepIndex);
    return;
  }
  PublishLifecycleOperation(Operation);
}

bool ACrowdDemoContinuousLifecycleCoordinator::BuildNextServerOperation(
  FCrowdDemoContinuousLifecycleOperation& OutOperation)
{
  OutOperation = {};
  OutOperation.Sequence = NextSequence;
  OutOperation.RelevantSetRevision = RelevantSetRevision + 1;
  OutOperation.FixedStepIndex = FixedStepIndex;

  if (PendingRespawnSlot != INDEX_NONE)
  {
    FSlotState& Slot = Slots[PendingRespawnSlot];
    OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Spawn;
    OutOperation.StableEntityId = static_cast<uint64>(PendingRespawnSlot);
    OutOperation.LifecycleSerial = Slot.LifecycleSerial + 1;
    OutOperation.NewMembershipKey = 1 + static_cast<uint32>(PendingRespawnSlot % 3);
    return true;
  }

  if (LifecycleWorld.GetActiveEntityCount() < Config.PopulationLimit)
  {
    for (int32 SlotIndex = 1; SlotIndex <= Config.PopulationLimit; ++SlotIndex)
    {
      if (!Slots[SlotIndex].bActive && Slots[SlotIndex].LifecycleSerial == 0)
      {
        OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Spawn;
        OutOperation.StableEntityId = static_cast<uint64>(SlotIndex);
        OutOperation.LifecycleSerial = 1;
        OutOperation.NewMembershipKey = 1 + static_cast<uint32>(SlotIndex % 3);
        return true;
      }
    }
  }

  int32 SlotIndex = INDEX_NONE;
  for (int32 Offset = 0; Offset < Config.PopulationLimit; ++Offset)
  {
    const int32 Candidate = 1 + ((DespawnCursor + Offset) % Config.PopulationLimit);
    if (Slots[Candidate].bActive)
    {
      SlotIndex = Candidate;
      DespawnCursor = Candidate % Config.PopulationLimit;
      break;
    }
  }
  if (SlotIndex == INDEX_NONE) return false;
  const FSlotState& Slot = Slots[SlotIndex];
  OutOperation.StableEntityId = static_cast<uint64>(SlotIndex);
  OutOperation.LifecycleSerial = Slot.LifecycleSerial;
  if ((OperationPhase++ % 2) == 0)
  {
    OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Membership;
    OutOperation.PreviousMembershipKey = Slot.MembershipKey;
    OutOperation.NewMembershipKey = 1 + (Slot.MembershipKey % 3);
  }
  else
  {
    OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Despawn;
    OutOperation.DespawnReason = static_cast<uint8>((DespawnCount % 2) == 0
      ? ECrowdDespawnReason::Death
      : ECrowdDespawnReason::BusinessRecycle);
  }
  return true;
}

bool ACrowdDemoContinuousLifecycleCoordinator::ApplyOperation(
  const FCrowdDemoContinuousLifecycleOperation& Operation)
{
  if (Operation.StableEntityId == 0
    || Operation.StableEntityId >= static_cast<uint64>(Slots.Num())) return false;
  const int32 SlotIndex = static_cast<int32>(Operation.StableEntityId);
  FSlotState& Slot = Slots[SlotIndex];
  const FCrowdStableEntityRef Ref{1, Operation.StableEntityId, Operation.LifecycleSerial};
  const FCrowdLifecycleBatchHeader Header = MakeBatchHeader(Operation);
  ECrowdLifecycleBatchAcceptResult Result = ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  switch (Operation.Kind)
  {
  case ECrowdDemoContinuousLifecycleOperationKind::Spawn:
    {
      FCrowdSpawnBatch Batch;
      Batch.Header = Header;
      Batch.Entries.Add(FCrowdSpawnEntry{
        MakeAgentFacts(SlotIndex, Operation.LifecycleSerial), Operation.NewMembershipKey});
      FCrowdLifecycleBatchTransport::Finalize(Batch);
      Result = LifecycleWorld.ApplyAtBoundary(Batch);
      if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
      {
        Slot.LifecycleSerial = Operation.LifecycleSerial;
        Slot.MembershipKey = Operation.NewMembershipKey;
        Slot.bActive = true;
        PendingRespawnSlot = INDEX_NONE;
        ++SpawnCount;
      }
      break;
    }
  case ECrowdDemoContinuousLifecycleOperationKind::Despawn:
    {
      if (Operation.DespawnReason >= static_cast<uint8>(ECrowdDespawnReason::Count)) return false;
      FCrowdDespawnBatch Batch;
      Batch.Header = Header;
      Batch.Entries.Add(FCrowdDespawnEntry{
        Ref, static_cast<ECrowdDespawnReason>(Operation.DespawnReason)});
      FCrowdLifecycleBatchTransport::Finalize(Batch);
      Result = LifecycleWorld.ApplyAtBoundary(Batch);
      if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
      {
        Slot.bActive = false;
        PendingRespawnSlot = SlotIndex;
        ++DespawnCount;
      }
      break;
    }
  case ECrowdDemoContinuousLifecycleOperationKind::Membership:
    {
      FCrowdMembershipBatch Batch;
      Batch.Header = Header;
      Batch.Entries.Add(FCrowdMembershipEntry{
        Ref, Operation.PreviousMembershipKey, Operation.NewMembershipKey});
      FCrowdLifecycleBatchTransport::Finalize(Batch);
      Result = LifecycleWorld.ApplyAtBoundary(Batch);
      if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
      {
        Slot.MembershipKey = Operation.NewMembershipKey;
        ++MembershipChangeCount;
      }
      break;
    }
  default:
    return false;
  }
  if (Result != ECrowdLifecycleBatchAcceptResult::Accepted) return false;

  NextSequence = Operation.Sequence + 1;
  RelevantSetRevision = Operation.RelevantSetRevision;
  FixedStepIndex = Operation.FixedStepIndex;
  MaxObservedPopulation = FMath::Max(
    MaxObservedPopulation, LifecycleWorld.GetActiveEntityCount());
  if (LifecycleWorld.GetActiveEntityCount() > Config.PopulationLimit) return false;
  bVisualSyncPending = !HasAuthority();
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoContinuousLifecycle role=%s stage=operation kind=%d sequence=%llu fixed_step=%lld slot=%llu lifecycle_serial=%u despawn_reason=%d active=%d hash=%llu source=RuntimeLifecycle"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    static_cast<int32>(Operation.Kind),
    Operation.Sequence,
    Operation.FixedStepIndex,
    Operation.StableEntityId,
    Operation.LifecycleSerial,
    static_cast<int32>(Operation.DespawnReason),
    LifecycleWorld.GetActiveEntityCount(),
    LifecycleWorld.CalculateEntitySetHash());
  return true;
}

void ACrowdDemoContinuousLifecycleCoordinator::SyncClientVisualsIncremental()
{
  UWorld* World = GetWorld();
  ACrowdDemoReplicator* Replicator = World ? FindContinuousVisualHost(*World) : nullptr;
  UMassCrowdPresentationSubsystem* Presentation = World
    ? World->GetSubsystem<UMassCrowdPresentationSubsystem>() : nullptr;
  UInstancedStaticMeshComponent* Instances = Replicator
    ? Replicator->GetCrowdInstancesForClientVisuals() : nullptr;
  UInstancedStaticMeshComponent* HitFlashInstances = Replicator
    ? Replicator->GetCrowdHitFlashInstancesForClientVisuals() : nullptr;
  if (!Replicator || !Presentation || !Instances || !HitFlashInstances)
    return;

  if (!bClientVisualsInitialized)
  {
    Replicator->ClearCrowdVisualInstances();
    const TSharedRef<FCrowdDemoIsmPresentationSink> Sink =
      MakeShared<FCrowdDemoIsmPresentationSink>(
        *Instances, *HitFlashInstances);
    if (!Presentation->RegisterProfile(1, Sink))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoContinuousLifecycle role=client stage=presentation_profile"));
      return;
    }
    bPresentationProfileRegistered = true;
    bClientVisualsInitialized = true;
  }

  const uint64 Sequence = FMath::Max<uint64>(1, NextSequence);
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    if (Slots[SlotIndex].bActive)
    {
      FCrowdPresentationState State;
      State.EntityRef = MakeAgentFacts(
        SlotIndex, Slots[SlotIndex].LifecycleSerial).StableEntityRef;
      State.Transform = MakeVisualTransform(SlotIndex);
      State.ProfileKey = 1;
      State.Sequence = Sequence;
      State.SampleServerSeconds = FixedStepIndex * ContinuousFixedStepSeconds;
      ECrowdPresentationApplyResult Result =
        Presentation->ApplyUpdate(State);
      if (Result == ECrowdPresentationApplyResult::MissingEntity)
        Result = Presentation->ApplySpawn(State);
      if (Result != ECrowdPresentationApplyResult::Applied
        && Result != ECrowdPresentationApplyResult::Duplicate
        && Result != ECrowdPresentationApplyResult::IgnoredStale)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoContinuousLifecycle role=client stage=presentation_apply slot=%d result=%d"),
          SlotIndex, static_cast<int32>(Result));
        return;
      }
      continue;
    }

    if (Slots[SlotIndex].LifecycleSerial == 0) continue;
    const FCrowdStableEntityRef Ref =
      MakeAgentFacts(
        SlotIndex, Slots[SlotIndex].LifecycleSerial).StableEntityRef;
    const ECrowdPresentationApplyResult Result =
      Presentation->ApplyDespawn(Ref, 1, Sequence);
    if (Result != ECrowdPresentationApplyResult::Applied
      && Result != ECrowdPresentationApplyResult::Duplicate
      && Result != ECrowdPresentationApplyResult::IgnoredStale
      && Result != ECrowdPresentationApplyResult::MissingEntity)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoContinuousLifecycle role=client stage=presentation_despawn slot=%d result=%d"),
        SlotIndex, static_cast<int32>(Result));
      return;
    }
  }
  if (Instances->GetInstanceCount() != LifecycleWorld.GetActiveEntityCount()
    || HitFlashInstances->GetInstanceCount() != LifecycleWorld.GetActiveEntityCount()
    || Presentation->GetInstanceCount(1)
      != LifecycleWorld.GetActiveEntityCount())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoContinuousLifecycle role=client stage=visual_count active=%d visual=%d hit_flash=%d tracked=%d"),
      LifecycleWorld.GetActiveEntityCount(),
      Instances->GetInstanceCount(),
      HitFlashInstances->GetInstanceCount(),
      Presentation->GetInstanceCount(1));
    return;
  }
  bVisualSyncPending = false;
}

FTransform ACrowdDemoContinuousLifecycleCoordinator::MakeVisualTransform(
  const int32 SlotIndex) const
{
  const int32 Column = (SlotIndex - 1) % 5;
  const int32 Row = (SlotIndex - 1) / 5;
  const float MembershipOffset = Slots.IsValidIndex(SlotIndex)
    ? static_cast<float>(Slots[SlotIndex].MembershipKey - 1) * 30.0f
    : 0.0f;
  return FTransform(
    FRotator::ZeroRotator,
    FVector(Column * 180.0f, Row * 180.0f + MembershipOffset, 60.0f),
    FVector(34.0f));
}

void ACrowdDemoContinuousLifecycleCoordinator::LogCheckpoint()
{
  UWorld* World = GetWorld();
  const ACrowdDemoReplicator* Replicator = World ? FindContinuousVisualHost(*World) : nullptr;
  const int32 VisibleCount = !HasAuthority() && Replicator
    ? Replicator->GetCrowdVisualInstanceCount() : 0;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoContinuousLifecycleCheckpoint role=%s sequence=%llu fixed_step=%lld active=%d visible=%d spawned=%d despawned=%d membership=%d max_population=%d stale_reject=%d entity_set_hash=%llu source=RuntimeLifecycle"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    NextSequence - 1,
    FixedStepIndex,
    LifecycleWorld.GetActiveEntityCount(),
    VisibleCount,
    SpawnCount,
    DespawnCount,
    MembershipChangeCount,
    MaxObservedPopulation,
    StaleRejectCount,
    LifecycleWorld.CalculateEntitySetHash());
}

FCrowdAgentFacts ACrowdDemoContinuousLifecycleCoordinator::MakeAgentFacts(
  const int32 SlotIndex,
  const uint32 LifecycleSerial) const
{
  FCrowdAgentFacts Facts;
  Facts.StableEntityRef = {1, static_cast<uint64>(SlotIndex), LifecycleSerial};
  Facts.FactionKey = static_cast<uint32>(SlotIndex % 2) + 1;
  Facts.CapabilitySet.Add(ECrowdCapability::Move);
  Facts.CapabilitySet.Add(ECrowdCapability::Wander);
  Facts.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::Wander);
  Facts.MovementProfileKey = 1;
  Facts.PresentationProfileKey = 1;
  Facts.RuntimeState = 1;
  return Facts;
}

FCrowdLifecycleBatchHeader ACrowdDemoContinuousLifecycleCoordinator::MakeBatchHeader(
  const FCrowdDemoContinuousLifecycleOperation& Operation) const
{
  FCrowdLifecycleBatchHeader Header;
  Header.BaseSnapshotRevision = Config.SnapshotRevision;
  Header.FixedStepIndex = Operation.FixedStepIndex;
  Header.RelevantSetRevision = Operation.RelevantSetRevision;
  Header.Sequence = Operation.Sequence;
  return Header;
}
