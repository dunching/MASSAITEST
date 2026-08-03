#include "MassCrowdAsyncSimulationRuntime.h"

#include "HAL/PlatformProcess.h"
#include "MassCrowdWorkerInteractionDomain.h"
#include "MassCrowdWorkerCombatState.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdWorkerResultApply.h"
#include "Misc/ScopeLock.h"

namespace CrowdAsyncSimulationPrivate
{
  constexpr uint64 AsyncFnvOffset64 = 14695981039346656037ull;
  constexpr uint64 AsyncFnvPrime64 = 1099511628211ull;

  uint8 FieldOwnerExecutionRank(const ECrowdWorkerField Field)
  {
    switch (Field)
    {
      case ECrowdWorkerField::Lifecycle:
      case ECrowdWorkerField::InputSnapshot:
      case ECrowdWorkerField::MovementProfile:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::LifecycleInput);
      case ECrowdWorkerField::Behavior:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::Behavior);
      case ECrowdWorkerField::Resource:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::FlowResource);
      case ECrowdWorkerField::Target:
      case ECrowdWorkerField::TargetCohort:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::Target);
      case ECrowdWorkerField::Combat:
      case ECrowdWorkerField::Projectile:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::CombatReactive);
      case ECrowdWorkerField::MovementPlan:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::MovementPlanning);
      case ECrowdWorkerField::Movement:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::Movement);
      case ECrowdWorkerField::Particle:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::ParticleInteraction);
      case ECrowdWorkerField::Facing:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::FacingFinalize);
      case ECrowdWorkerField::Presentation:
        return CrowdWorkerRuntimeV2DomainExecutionRank(
          ECrowdWorkerDomainId::Publish);
      default:
        return MAX_uint8;
    }
  }

  ECrowdWorkerDomainId FieldOwnerDomain(const ECrowdWorkerField Field)
  {
    switch (Field)
    {
      case ECrowdWorkerField::Lifecycle:
      case ECrowdWorkerField::InputSnapshot:
      case ECrowdWorkerField::MovementProfile:
        return ECrowdWorkerDomainId::LifecycleInput;
      case ECrowdWorkerField::Behavior:
        return ECrowdWorkerDomainId::Behavior;
      case ECrowdWorkerField::Resource:
        return ECrowdWorkerDomainId::FlowResource;
      case ECrowdWorkerField::Target:
      case ECrowdWorkerField::TargetCohort:
        return ECrowdWorkerDomainId::Target;
      case ECrowdWorkerField::Combat:
      case ECrowdWorkerField::Projectile:
        return ECrowdWorkerDomainId::CombatReactive;
      case ECrowdWorkerField::MovementPlan:
        return ECrowdWorkerDomainId::MovementPlanning;
      case ECrowdWorkerField::Movement:
        return ECrowdWorkerDomainId::Movement;
      case ECrowdWorkerField::Particle:
        return ECrowdWorkerDomainId::ParticleInteraction;
      case ECrowdWorkerField::Facing:
        return ECrowdWorkerDomainId::FacingFinalize;
      default:
        return ECrowdWorkerDomainId::Publish;
    }
  }

  ECrowdAsyncSimulationInputFailure ToRuntimeFailure(
    const ECrowdWorkerInputFailure Failure)
  {
    switch (Failure)
    {
      case ECrowdWorkerInputFailure::None:
        return ECrowdAsyncSimulationInputFailure::None;
      case ECrowdWorkerInputFailure::InvalidPayload:
        return ECrowdAsyncSimulationInputFailure::InvalidPayload;
      case ECrowdWorkerInputFailure::GenerationMismatch:
        return ECrowdAsyncSimulationInputFailure::Generation;
      case ECrowdWorkerInputFailure::StaleSequence:
        return ECrowdAsyncSimulationInputFailure::StaleSequence;
      case ECrowdWorkerInputFailure::SequenceGap:
        return ECrowdAsyncSimulationInputFailure::SequenceGap;
      case ECrowdWorkerInputFailure::ConflictingDuplicate:
        return ECrowdAsyncSimulationInputFailure::ConflictingDuplicate;
      case ECrowdWorkerInputFailure::TimeRegression:
        return ECrowdAsyncSimulationInputFailure::TimeRegression;
      case ECrowdWorkerInputFailure::ResnapshotRequired:
        return ECrowdAsyncSimulationInputFailure::ResnapshotRequired;
      default:
        return ECrowdAsyncSimulationInputFailure::InvalidPayload;
    }
  }

  void AsyncFoldByte(uint64& Hash, const uint8 Value)
  {
    Hash ^= Value;
    Hash *= AsyncFnvPrime64;
  }

  template<typename T>
  void AsyncFoldUnsigned(uint64& Hash, const T Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      AsyncFoldByte(
        Hash, static_cast<uint8>(Value >> (Byte * 8)));
  }

  void AsyncFoldRef(
    uint64& Hash,
    const FCrowdStableEntityRef& Ref)
  {
    AsyncFoldUnsigned(Hash, Ref.ProviderId);
    AsyncFoldUnsigned(Hash, Ref.StableEntityId);
    AsyncFoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  int64 PackSpatialCell(const FVector& Position)
  {
    constexpr double CellSizeCm = 400.0;
    const int32 X = FMath::FloorToInt(Position.X / CellSizeCm);
    const int32 Y = FMath::FloorToInt(Position.Y / CellSizeCm);
    return static_cast<int64>(
      (static_cast<uint64>(static_cast<uint32>(X)) << 32)
      | static_cast<uint32>(Y));
  }

  FCrowdWorkerAuthorityScopeKey ResolveAuthorityScope(
    const FCrowdWorkerDirtyStateRecord& Record,
    const FCrowdWorkerEntityStateStore& StateStore)
  {
    FCrowdWorkerAuthorityScopeKey Scope;
    Scope.Field = Record.Field;
    if (Record.Field == ECrowdWorkerField::TargetCohort)
    {
      Scope.Kind = ECrowdWorkerAuthorityScopeKind::CohortPlan;
      FCrowdWorkerTargetCohortState State;
      if (FCrowdWorkerTargetCohortStateCodec::Decode(
          Record.Payload, State))
        Scope.ScopeId = State.CohortKey;
      return Scope;
    }
    if (Record.Field == ECrowdWorkerField::Target)
    {
      Scope.Kind = ECrowdWorkerAuthorityScopeKind::EntityGuidance;
      FCrowdWorkerTargetState State;
      if (FCrowdWorkerTargetStateCodec::Decode(Record.Payload, State))
      {
        Scope.ScopeId = static_cast<int64>(
          (static_cast<uint64>(State.CohortKey) << 32)
          | static_cast<uint32>(
            Record.EntityRef.StableEntityId / 128));
      }
      return Scope;
    }
    if (Record.Field == ECrowdWorkerField::Movement
      || Record.Field == ECrowdWorkerField::Particle
      || Record.Field == ECrowdWorkerField::Facing)
    {
      Scope.Kind = ECrowdWorkerAuthorityScopeKind::SpatialCell;
      const FCrowdWorkerDirtyStateRecord* Movement =
        Record.Field == ECrowdWorkerField::Movement
          ? &Record
          : StateStore.Find(
            Record.EntityRef, ECrowdWorkerField::Movement);
      FCrowdWorkerMovementState MovementState;
      if (Movement
        && FCrowdWorkerMovementStateCodec::Decode(
          Movement->Payload, MovementState))
        Scope.ScopeId = PackSpatialCell(MovementState.Position);
      return Scope;
    }
    return Scope;
  }

  void FoldAuthorityState(
    uint64& Hash,
    const FCrowdWorkerDirtyStateRecord& Record)
  {
    AsyncFoldRef(Hash, Record.EntityRef);
    AsyncFoldUnsigned(Hash, static_cast<uint8>(Record.Field));
    // Revisions and source waterlines order local publication, but are not
    // simulated field content. Owner pumps may coalesce work differently on
    // server and client while producing the same state; including them here
    // turns that harmless scheduling difference into a false correction.
    uint64 SemanticPayloadHash = Record.Payload.StableHash;
    if (Record.Field == ECrowdWorkerField::Movement
      || Record.Field == ECrowdWorkerField::Facing)
    {
      FCrowdWorkerMovementState Movement;
      FCrowdWorkerPayload CanonicalPayload;
      if (FCrowdWorkerMovementStateCodec::Decode(
          Record.Payload, Movement))
      {
        Movement.CorrectionRevision = 0;
        if (FCrowdWorkerMovementStateCodec::Encode(
            Movement, CanonicalPayload))
          SemanticPayloadHash = CanonicalPayload.StableHash;
      }
    }
    AsyncFoldUnsigned(Hash, SemanticPayloadHash);
  }

  struct FResourceRecord
  {
    uint64 ResourceId = 0;
    uint64 Revision = 0;
    FCrowdWorkerPayload Payload;
  };

  struct FEntitySlotKey
  {
    uint32 ProviderId = 0;
    uint64 StableEntityId = 0;

    bool operator==(const FEntitySlotKey& Other) const = default;

    friend uint32 GetTypeHash(const FEntitySlotKey& Key)
    {
      return HashCombineFast(
        ::GetTypeHash(Key.ProviderId),
        ::GetTypeHash(Key.StableEntityId));
    }
  };

  struct FWorkerMirror
  {
    TArray<FCrowdStableEntityRef> EntityRefs;
    TArray<FCrowdWorkerPublishedState> States;
    TArray<uint64> LastStateInputSequences;
    TArray<uint64> CorrectionRevisions;
    TArray<FResourceRecord> Resources;
    TArray<FCrowdWorkerCommandDelta> PendingCommands;
    TMap<FCrowdStableEntityRef, int32> EntityIndices;
    TMap<FEntitySlotKey, int32> SlotIndices;
    TMap<FEntitySlotKey, uint32> LastLifecycleBySlot;

    static FEntitySlotKey MakeSlotKey(
      const FCrowdStableEntityRef& Ref)
    {
      return {Ref.ProviderId, Ref.StableEntityId};
    }

    int32 FindEntity(const FCrowdStableEntityRef& Ref) const
    {
      const int32* Index = EntityIndices.Find(Ref);
      return Index ? *Index : INDEX_NONE;
    }

    int32 FindEntitySlot(const FCrowdStableEntityRef& Ref) const
    {
      const int32* Index = SlotIndices.Find(MakeSlotKey(Ref));
      return Index ? *Index : INDEX_NONE;
    }

    void RebuildIndices()
    {
      EntityIndices.Reset();
      SlotIndices.Reset();
      EntityIndices.Reserve(EntityRefs.Num());
      SlotIndices.Reserve(EntityRefs.Num());
      for (int32 Index = 0; Index < EntityRefs.Num(); ++Index)
      {
        EntityIndices.Add(EntityRefs[Index], Index);
        SlotIndices.Add(MakeSlotKey(EntityRefs[Index]), Index);
      }
    }

    void SortEntities()
    {
      TArray<int32> Order;
      Order.Reserve(EntityRefs.Num());
      for (int32 Index = 0; Index < EntityRefs.Num(); ++Index)
        Order.Add(Index);
      Order.Sort([this](const int32 A, const int32 B)
      {
        return EntityRefs[A] < EntityRefs[B];
      });
      TArray<FCrowdStableEntityRef> SortedRefs;
      TArray<FCrowdWorkerPublishedState> SortedStates;
      TArray<uint64> SortedInputSequences;
      TArray<uint64> SortedCorrectionRevisions;
      SortedRefs.Reserve(Order.Num());
      SortedStates.Reserve(Order.Num());
      SortedInputSequences.Reserve(Order.Num());
      SortedCorrectionRevisions.Reserve(Order.Num());
      for (const int32 Index : Order)
      {
        SortedRefs.Add(EntityRefs[Index]);
        SortedStates.Add(States[Index]);
        SortedInputSequences.Add(LastStateInputSequences[Index]);
        SortedCorrectionRevisions.Add(CorrectionRevisions[Index]);
      }
      EntityRefs = MoveTemp(SortedRefs);
      States = MoveTemp(SortedStates);
      LastStateInputSequences = MoveTemp(SortedInputSequences);
      CorrectionRevisions = MoveTemp(SortedCorrectionRevisions);
      RebuildIndices();
    }

    void ConsumeCommandsThrough(
      const double SimulationTimeSeconds)
    {
      PendingCommands.RemoveAll([
        SimulationTimeSeconds](
        const FCrowdWorkerCommandDelta& Command)
      {
        return Command.EffectiveSimulationTimeSeconds
          <= SimulationTimeSeconds + UE_DOUBLE_SMALL_NUMBER;
      });
    }

    bool Spawn(const FCrowdWorkerSpawnDelta& Delta)
    {
      const int32 ActiveSlot = FindEntitySlot(Delta.EntityRef);
      if (ActiveSlot != INDEX_NONE) return false;
      const FEntitySlotKey SlotKey = MakeSlotKey(Delta.EntityRef);
      if (const uint32* LastLifecycle =
        LastLifecycleBySlot.Find(SlotKey))
      {
        if (Delta.EntityRef.LifecycleSerial <= *LastLifecycle)
          return false;
      }
      FCrowdWorkerPublishedState State;
      State.StateRevision = 1;
      State.Payload = Delta.InitialState;
      EntityRefs.Add(Delta.EntityRef);
      States.Add(MoveTemp(State));
      LastStateInputSequences.Add(Delta.InputSequence);
      CorrectionRevisions.Add(0);
      LastLifecycleBySlot.Add(
        SlotKey, Delta.EntityRef.LifecycleSerial);
      const int32 NewIndex = EntityRefs.Num() - 1;
      EntityIndices.Add(Delta.EntityRef, NewIndex);
      SlotIndices.Add(SlotKey, NewIndex);
      return true;
    }

    bool Despawn(const FCrowdWorkerDespawnDelta& Delta)
    {
      const int32 Index = FindEntity(Delta.EntityRef);
      if (Index == INDEX_NONE) return false;
      LastLifecycleBySlot.Add(
        MakeSlotKey(Delta.EntityRef),
        Delta.EntityRef.LifecycleSerial);
      EntityRefs.RemoveAt(Index);
      States.RemoveAt(Index);
      LastStateInputSequences.RemoveAt(Index);
      CorrectionRevisions.RemoveAt(Index);
      RebuildIndices();
      return true;
    }

    bool ApplyState(const FCrowdWorkerExternalGameplayInput& Delta)
    {
      const int32 Index = FindEntity(Delta.EntityRef);
      if (Index == INDEX_NONE
        || Delta.InputSequence <= LastStateInputSequences[Index])
        return false;
      ++States[Index].StateRevision;
      States[Index].Payload = Delta.FullState;
      LastStateInputSequences[Index] = Delta.InputSequence;
      return true;
    }

    bool ApplyResource(const FCrowdWorkerResourceDelta& Delta)
    {
      for (FResourceRecord& Resource : Resources)
      {
        if (Resource.ResourceId != Delta.ResourceId) continue;
        if (Delta.Revision <= Resource.Revision) return false;
        Resource.Revision = Delta.Revision;
        Resource.Payload = Delta.Payload;
        return true;
      }
      Resources.Add({
        Delta.ResourceId, Delta.Revision, Delta.Payload});
      Resources.Sort([](
        const FResourceRecord& A,
        const FResourceRecord& B)
      {
        return A.ResourceId < B.ResourceId;
      });
      return true;
    }

    uint64 CalculateEntitySetHash() const
    {
      uint64 Hash = AsyncFnvOffset64;
      AsyncFoldUnsigned(Hash, uint32{1});
      for (const FCrowdStableEntityRef& Ref : EntityRefs)
        AsyncFoldRef(Hash, Ref);
      return Hash;
    }

    uint64 CalculateResourceHash() const
    {
      uint64 Hash = AsyncFnvOffset64;
      AsyncFoldUnsigned(Hash, uint32{1});
      for (const FResourceRecord& Resource : Resources)
      {
        AsyncFoldUnsigned(Hash, Resource.ResourceId);
        AsyncFoldUnsigned(Hash, Resource.Revision);
        AsyncFoldUnsigned(Hash, Resource.Payload.StableHash);
      }
      return Hash;
    }

    uint64 CalculateStableHash() const
    {
      uint64 Hash = CalculateEntitySetHash();
      for (int32 Index = 0; Index < EntityRefs.Num(); ++Index)
      {
        AsyncFoldUnsigned(Hash, States[Index].StateRevision);
        AsyncFoldUnsigned(Hash, States[Index].Payload.StableHash);
        AsyncFoldUnsigned(Hash, LastStateInputSequences[Index]);
        AsyncFoldUnsigned(Hash, CorrectionRevisions[Index]);
      }
      AsyncFoldUnsigned(Hash, CalculateResourceHash());
      return Hash;
    }
  };

  enum class EInputKind : uint8
  {
    Spawn = 1,
    Despawn,
    Command,
    State,
    Resource
  };

  struct FInputRef
  {
    uint64 Sequence = 0;
    EInputKind Kind = EInputKind::Spawn;
    int32 Index = INDEX_NONE;
  };

  void GatherInputRefs(
    const FCrowdWorkerIntentBatch& Batch,
    TArray<FInputRef>& OutRefs)
  {
    OutRefs.Reset();
    OutRefs.Reserve(Batch.GetRecordCount());
    for (int32 Index = 0; Index < Batch.Spawns.Num(); ++Index)
      OutRefs.Add({
        Batch.Spawns[Index].InputSequence,
        EInputKind::Spawn, Index});
    for (int32 Index = 0; Index < Batch.Despawns.Num(); ++Index)
      OutRefs.Add({
        Batch.Despawns[Index].InputSequence,
        EInputKind::Despawn, Index});
    for (int32 Index = 0; Index < Batch.Commands.Num(); ++Index)
      OutRefs.Add({
        Batch.Commands[Index].InputSequence,
        EInputKind::Command, Index});
    for (int32 Index = 0; Index < Batch.ExternalGameplayInputs.Num(); ++Index)
      OutRefs.Add({
        Batch.ExternalGameplayInputs[Index].InputSequence,
        EInputKind::State, Index});
    for (int32 Index = 0;
      Index < Batch.ResourceDeltas.Num(); ++Index)
      OutRefs.Add({
        Batch.ResourceDeltas[Index].InputSequence,
        EInputKind::Resource, Index});
    OutRefs.Sort([](const FInputRef& A, const FInputRef& B)
    {
      return A.Sequence < B.Sequence;
    });
  }

  bool ApplyInputBatch(
    const FCrowdWorkerIntentBatch& Batch,
    const int32 MaxPendingCommands,
    FWorkerMirror& Mirror)
  {
    TArray<FInputRef> Refs;
    GatherInputRefs(Batch, Refs);
    bool bEntitySetChanged = false;
    for (const FInputRef& Ref : Refs)
    {
      bool bApplied = false;
      switch (Ref.Kind)
      {
        case EInputKind::Spawn:
          bApplied = Mirror.Spawn(Batch.Spawns[Ref.Index]);
          bEntitySetChanged |= bApplied;
          break;
        case EInputKind::Despawn:
          bApplied = Mirror.Despawn(Batch.Despawns[Ref.Index]);
          bEntitySetChanged |= bApplied;
          break;
        case EInputKind::Command:
          if (Mirror.PendingCommands.Num() < MaxPendingCommands)
          {
            Mirror.PendingCommands.Add(Batch.Commands[Ref.Index]);
            bApplied = true;
          }
          break;
        case EInputKind::State:
        {
          const FCrowdWorkerExternalGameplayInput& Input =
            Batch.ExternalGameplayInputs[Ref.Index];
          bApplied = Input.InputTypeId == static_cast<uint16>(
              ECrowdWorkerExternalGameplayInputType::
                MovementProfileRevision)
            ? Mirror.FindEntity(Input.EntityRef) != INDEX_NONE
            : Mirror.ApplyState(Input);
          break;
        }
        case EInputKind::Resource:
          bApplied = Mirror.ApplyResource(
            Batch.ResourceDeltas[Ref.Index]);
          break;
      }
      if (!bApplied) return false;
    }
    if (bEntitySetChanged) Mirror.SortEntities();
    return true;
  }

  bool IsResnapshotBatch(const FCrowdWorkerIntentBatch& Batch)
  {
    return Batch.Despawns.IsEmpty()
      && !Batch.ExternalGameplayInputs.ContainsByPredicate([](
        const FCrowdWorkerExternalGameplayInput& Input)
      {
        return Input.InputTypeId != static_cast<uint16>(
          ECrowdWorkerExternalGameplayInputType::
            MovementProfileRevision);
      });
  }
}

using namespace CrowdAsyncSimulationPrivate;

struct FCrowdAsyncSimulationRuntime::FSharedState
  : public TSharedFromThis<FSharedState, ESPMode::ThreadSafe>
{
  struct FPendingInput
  {
    FCrowdWorkerIntentBatch Batch;
    bool bResnapshot = false;
    uint64 EnqueuedCycles = 0;
  };

  struct FShadowExecution
  {
    uint64 ActualStableHash = 0;
    uint64 StartedCycles = 0;
    uint64 CompletedCycles = 0;
  };

  struct FShadowTaskRecord
  {
    uint64 Generation = 0;
    uint64 WorkSequence = 0;
    uint64 SubmissionOrdinal = 0;
    uint32 KernelId = 0;
    uint64 ExpectedStableHash = 0;
    bool bRequireExpectedStableHash = true;
    uint64 SubmittedCycles = 0;
    TSharedPtr<FShadowExecution, ESPMode::ThreadSafe> Execution;
    UE::Tasks::FTask Task;
  };

  struct FWorkerV2ShardExecution
  {
    uint64 Generation = 0;
    uint64 WorkerEpoch = 0;
    int32 PropagationRound = 0;
    TArray<FCrowdWorkerDomainShardResult> Results;
    TArray<UE::Tasks::FTask> Tasks;
    TSharedPtr<UE::Tasks::FTaskEvent, ESPMode::ThreadSafe>
      OwnerReleased;
    UE::Tasks::FTask Continuation;

    bool IsCompleted() const
    {
      for (const UE::Tasks::FTask& Task : Tasks)
      {
        if (!Task.IsValid() || !Task.IsCompleted())
          return false;
      }
      return true;
    }

    bool IsSettled() const
    {
      return IsCompleted()
        && (!Continuation.IsValid()
          || Continuation.IsCompleted());
    }
  };

  enum class EWorkerV2AdvanceResult : uint8
  {
    Completed,
    Pending,
    Failed
  };

  FCrowdAsyncSimulationRuntimeConfig Config;
  FCrowdWorkerInputSequenceGate AdmissionGate;
  FCrowdWorkerInputSequenceGate SequenceGate;
  FCrowdWorkerPublishedExchange PublishedExchange;
  FCrowdWorkerWorkRing WorkRing;
  FCrowdWorkerTimeWheel TimeWheel;
  FCrowdWorkerDependencyIndex DependencyIndex;
  FCrowdWorkerResourceStore ResourceStore;
  FCrowdWorkerEntityStateStore EntityStateStore;
  FCrowdWorkerCommandStore CommandStore;
  FCrowdWorkerSpatialIndex SpatialIndex;
  FCrowdWorkerDirtyStateStore DirtyStateStore;
  FCrowdWorkerOrderedEventStore OrderedEventStore;
  FCrowdWorkerNetworkStatePublisher NetworkStatePublisher;
  TUniquePtr<FCrowdWorkerDomainRegistry> DomainRegistry;
  FWorkerMirror Mirror;
  TArray<FPendingInput> InputQueue;
  TArray<FCrowdWorkerIntentBatch> RetainedNetworkIntents;
  struct FAuthorityDigestSnapshot
  {
    FCrowdWorkerAuthorityDigestBatch Digest;
    TArray<FCrowdWorkerDirtyStateRecord> States;
  };
  TArray<FAuthorityDigestSnapshot> AuthorityDigestHistory;
  TArray<FCrowdWorkerAuthorityCorrectionBatch>
    AuthorityCorrectionQueue;
  bool bAuthorityCorrectionBarrierPending = false;
  uint64 AuthorityCorrectionBarrierTick = 0;
  uint64 AuthorityCorrectionBarrierInputSequence = 0;
#if WITH_DEV_AUTOMATION_TESTS
  struct FDiagnosticMovementCorruption
  {
    uint64 ExpectedGeneration = 0;
    FCrowdStableEntityRef EntityRef;
    FVector PositionOffset = FVector::ZeroVector;
    FVector VelocityOffset = FVector::ZeroVector;
    float YawOffsetDegrees = 0.0f;
  };
  TOptional<FDiagnosticMovementCorruption>
    PendingDiagnosticMovementCorruption;
#endif
  uint64 NextAuthorityDigestSequence = 1;
  uint64 LastAppliedAuthorityCorrectionSequence = 0;
  uint64 AuthorityDigestCount = 0;
  uint64 AuthorityCorrectionCount = 0;
  uint64 AuthorityCorrectionEntityCount = 0;
  uint64 AuthorityCorrectionScopeCount = 0;
  uint64 ConsecutivePredictionEpochsWithoutCorrection = 0;
  uint64 MaxPredictionEpochsWithoutCorrection = 0;
  double LastCorrectionBeforePositionErrorCm = 0.0;
  double LastCorrectionAfterPositionErrorCm = 0.0;
  double LastCorrectionBeforeVelocityErrorCmps = 0.0;
  double LastCorrectionAfterVelocityErrorCmps = 0.0;
  double LastCorrectionBeforeYawErrorDegrees = 0.0;
  double LastCorrectionAfterYawErrorDegrees = 0.0;
  int32 LastCorrectionBeforeCombatMismatchCount = 0;
  int32 LastCorrectionAfterCombatMismatchCount = 0;
  int32 LastCorrectionEntityCount = 0;
  int32 LastCorrectionScopeCount = 0;
  bool bCorrectionAppliedSinceLastEpoch = false;
  TArray<FShadowTaskRecord> ShadowTasks;
  TMap<uint32, uint64> LastShadowWorkSequences;
  TSharedPtr<FWorkerV2ShardExecution, ESPMode::ThreadSafe>
    ActiveWorkerV2ShardExecution;
  TArray<FCrowdWorkerWorkItem> WorkerV2PendingStageWork;
  TArray<FCrowdWorkerDirtyStateRecord>
    WorkerV2PendingPublishedDirtyStates;
  TArray<FCrowdWorkerGameplayEvent>
    WorkerV2PendingPublishedEvents;
  TMap<FCrowdStableEntityRef, uint64>
    PendingTouchedStateSequences;
  FCrowdWorkerMirrorSnapshot PublishedMirror;
  FCrowdAsyncSimulationRuntimeMetrics Metrics;
  FCriticalSection InputMutex;
  FCriticalSection SnapshotMutex;
  TAtomic<uint8> State{
    static_cast<uint8>(
      ECrowdAsyncSimulationRuntimeState::Stopped)};
  TAtomic<uint64> Generation{0};
  TAtomic<uint64> PendingGeneration{0};
  TAtomic<bool> bRequiresResnapshot{false};
  TAtomic<bool> bWorkPending{false};
  TAtomic<bool> bPublishPending{false};
  TAtomic<uint64> SubmittedShadowWorkCount{0};
  TAtomic<uint64> CompletedShadowWorkCount{0};
  TAtomic<uint64> SubmittedProductionWorkCount{0};
  TAtomic<uint64> CompletedProductionWorkCount{0};
  TAtomic<uint64> ShadowHashMismatchCount{0};
  TAtomic<int32> InFlightShadowWorkCount{0};
  TAtomic<uint64> LastPublishCycles{0};
  TAtomic<uint64> LastAcceptedInputSequence{0};
  TAtomic<uint64> QueuedInputSequenceWatermark{0};
  TAtomic<bool> bOwnerPumpExecuting{false};
  TAtomic<uint8> LastInputFailure{
    static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::None)};
  double SimulationTimeSeconds = 0.0;
  double TargetSimulationTimeSeconds = 0.0;
  uint64 AbsoluteSimulationTick = 0;
  uint64 WorkerEpoch = 0;
  uint64 LastAppliedInputSequence = 0;
  uint64 OwnerPumpCount = 0;
  uint64 ResnapshotCount = 0;
  uint64 FullMirrorSerializationCount = 0;
  uint64 LastFullMirrorGeneration = 0;
  uint64 LastFullMirrorWorkerEpoch = MAX_uint64;
  TAtomic<uint64> RejectedInputCount{0};
  uint64 NextShadowSubmissionOrdinal = 1;
  uint64 NextPublishSequence = 1;
  double OldestInputAgeMs = 0.0;
  double LastOwnerPumpMs = 0.0;
  double MaxOwnerPumpMs = 0.0;
  uint64 PropagationRoundCount = 0;
  uint64 PropagationLimitHitCount = 0;
  int32 WorkerV2EpochPropagationRound = 0;
  uint64 WorkerV2ShardDispatchCount = 0;
  uint64 WorkerV2ShardCompletionCount = 0;
  uint64 WorkerV2ShardMergeCount = 0;
  int32 WorkerV2ShardInFlightHighWatermark = 0;
  uint64 OrderedEventLossCount = 0;
  uint64 WorkerV2PublishedDirtyStateCount = 0;
  uint64 WorkerV2PublishedOrderedEventCount = 0;
  uint64 CoverageAuditFailureCount = 0;
  uint64 ShadowBaselineRebaseCount = 0;
  ECrowdWorkerRuntimeV2Failure LastWorkerV2Failure =
    ECrowdWorkerRuntimeV2Failure::None;

  bool IsWorkerV2Enabled() const
  {
    return Config.WorkerV2.GetEffectiveMode()
      != ECrowdWorkerRuntimeV2Mode::Disabled;
  }

  bool IsWorkerV2Production() const
  {
    return Config.WorkerV2.GetEffectiveMode()
      == ECrowdWorkerRuntimeV2Mode::Production;
  }

  bool ResetWorkerV2State()
  {
    const FCrowdWorkerRuntimeV2Config& V2 = Config.WorkerV2;
    PropagationRoundCount = 0;
    PropagationLimitHitCount = 0;
    WorkerV2EpochPropagationRound = 0;
    WorkerV2ShardDispatchCount = 0;
    WorkerV2ShardCompletionCount = 0;
    WorkerV2ShardMergeCount = 0;
    WorkerV2ShardInFlightHighWatermark = 0;
    ActiveWorkerV2ShardExecution.Reset();
    WorkerV2PendingStageWork.Reset();
    WorkerV2PendingPublishedDirtyStates.Reset();
    WorkerV2PendingPublishedEvents.Reset();
    OrderedEventLossCount = 0;
    WorkerV2PublishedDirtyStateCount = 0;
    WorkerV2PublishedOrderedEventCount = 0;
    CoverageAuditFailureCount = 0;
    ShadowBaselineRebaseCount = 0;
    AuthorityDigestHistory.Reset();
    AuthorityCorrectionQueue.Reset();
    bAuthorityCorrectionBarrierPending = false;
    AuthorityCorrectionBarrierTick = 0;
    AuthorityCorrectionBarrierInputSequence = 0;
    NextAuthorityDigestSequence = 1;
    LastAppliedAuthorityCorrectionSequence = 0;
    AuthorityDigestCount = 0;
    AuthorityCorrectionCount = 0;
    AuthorityCorrectionEntityCount = 0;
    AuthorityCorrectionScopeCount = 0;
    ConsecutivePredictionEpochsWithoutCorrection = 0;
    MaxPredictionEpochsWithoutCorrection = 0;
    LastCorrectionBeforePositionErrorCm = 0.0;
    LastCorrectionAfterPositionErrorCm = 0.0;
    LastCorrectionBeforeVelocityErrorCmps = 0.0;
    LastCorrectionAfterVelocityErrorCmps = 0.0;
    LastCorrectionBeforeYawErrorDegrees = 0.0;
    LastCorrectionAfterYawErrorDegrees = 0.0;
    LastCorrectionBeforeCombatMismatchCount = 0;
    LastCorrectionAfterCombatMismatchCount = 0;
    LastCorrectionEntityCount = 0;
    LastCorrectionScopeCount = 0;
    bCorrectionAppliedSinceLastEpoch = false;
    LastWorkerV2Failure =
      ECrowdWorkerRuntimeV2Failure::None;
    return WorkRing.Reset(V2.MaxWorkItems, 1)
      && TimeWheel.Reset(V2.MaxWakeups)
      && DependencyIndex.Reset(V2.MaxDependencyEdges)
      && ResourceStore.Reset(
        Config.ContractLimits.MaxPayloadBytes)
      && EntityStateStore.Reset(
        V2.MaxDirtyEntities,
        Config.ContractLimits.MaxPayloadBytes)
      && CommandStore.Reset(
        Config.MaxPendingCommands,
        Config.ContractLimits.MaxPayloadBytes)
      && SpatialIndex.Reset(V2.MaxDirtyEntities)
      && DirtyStateStore.Reset(
        V2.MaxDirtyEntities,
        Config.ContractLimits.MaxPayloadBytes)
      && OrderedEventStore.Reset(
        V2.MaxOrderedEvents,
        Config.ContractLimits.MaxPayloadBytes,
        Generation.Load())
      && NetworkStatePublisher.Reset(
        Config.NetworkState, Generation.Load());
  }

  bool EnqueueWorkerV2Input(
    const FCrowdWorkerIntentBatch& Batch)
  {
    const bool bHasMovementPlanningInput =
      DomainRegistry
      && DomainRegistry->HasDomain(
        ECrowdWorkerDomainId::MovementPlanning)
      && Batch.ResourceDeltas.ContainsByPredicate(
        [](const FCrowdWorkerResourceDelta& Delta)
        {
          return Delta.ResourceId
            == CrowdWorkerResourceIds::MovementControl;
        });
    auto EnqueueEntity = [this](
      const ECrowdWorkerDomainId Domain,
      const FCrowdStableEntityRef& EntityRef,
      const uint64 ReasonMask,
      const uint64 CorrectionRevision = 0)
    {
      FCrowdWorkerWorkItem Work;
      Work.Key.Domain = Domain;
      Work.Key.Kind = ECrowdWorkerWorkKind::Entity;
      Work.Key.PrimaryEntity = EntityRef;
      Work.Priority = Domain
          == ECrowdWorkerDomainId::LifecycleInput
        ? ECrowdWorkerWorkPriority::Critical
        : ECrowdWorkerWorkPriority::Normal;
      Work.CorrectionRevision = CorrectionRevision;
      Work.ReasonMask = ReasonMask;
      const ECrowdWorkerQueueResult Result =
        WorkRing.EnqueueCurrent(MoveTemp(Work));
      if (Result != ECrowdWorkerQueueResult::Added
        && Result != ECrowdWorkerQueueResult::MergedDuplicate)
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::WorkQueue;
      return Result == ECrowdWorkerQueueResult::Added
        || Result == ECrowdWorkerQueueResult::MergedDuplicate;
    };

    struct FLifecycleInputRef
    {
      uint64 InputSequence = 0;
      int32 Index = INDEX_NONE;
      bool bSpawn = false;
    };
    TArray<FLifecycleInputRef> LifecycleInputs;
    LifecycleInputs.Reserve(
      Batch.Spawns.Num() + Batch.Despawns.Num());
    for (int32 Index = 0; Index < Batch.Spawns.Num(); ++Index)
      LifecycleInputs.Add({
        Batch.Spawns[Index].InputSequence, Index, true});
    for (int32 Index = 0; Index < Batch.Despawns.Num(); ++Index)
      LifecycleInputs.Add({
        Batch.Despawns[Index].InputSequence, Index, false});
    LifecycleInputs.Sort([](
      const FLifecycleInputRef& A,
      const FLifecycleInputRef& B)
    {
      if (A.InputSequence != B.InputSequence)
        return A.InputSequence < B.InputSequence;
      if (A.bSpawn != B.bSpawn)
        return !A.bSpawn;
      return A.Index < B.Index;
    });
    for (const FLifecycleInputRef& LifecycleInput : LifecycleInputs)
    {
      if (LifecycleInput.bSpawn)
      {
        const FCrowdWorkerSpawnDelta& Delta =
          Batch.Spawns[LifecycleInput.Index];
        const ECrowdWorkerQueueResult StateResult =
          EntityStateStore.Spawn(
            Delta.EntityRef, Batch.Generation,
            Delta.InputSequence, Delta.InitialState);
        if (StateResult != ECrowdWorkerQueueResult::Added
          && StateResult
            != ECrowdWorkerQueueResult::MergedDuplicate)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::DirtyState;
          return false;
        }
        if (StateResult == ECrowdWorkerQueueResult::Added
          && !SpatialIndex.Spawn(EntityStateStore, Delta.EntityRef))
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::SpatialIndex;
          return false;
        }
        if (!EnqueueEntity(
          ECrowdWorkerDomainId::LifecycleInput,
          Delta.EntityRef, 1ull << 0))
          return false;
        if (DomainRegistry
          && DomainRegistry->HasDomain(
            ECrowdWorkerDomainId::Behavior)
          && !EnqueueEntity(
            ECrowdWorkerDomainId::Behavior,
            Delta.EntityRef, 1ull << 7))
          return false;
        if (DomainRegistry
          && DomainRegistry->HasDomain(
            ECrowdWorkerDomainId::Movement)
          && !bHasMovementPlanningInput
          && !EnqueueEntity(
            ECrowdWorkerDomainId::Movement,
            Delta.EntityRef, 1ull << 6))
          return false;
        continue;
      }

      const FCrowdWorkerDespawnDelta& Delta =
        Batch.Despawns[LifecycleInput.Index];
      WorkRing.RemoveEntity(Delta.EntityRef);
      TimeWheel.CancelEntity(Delta.EntityRef);
      DependencyIndex.RemoveEntity(Delta.EntityRef);
      DirtyStateStore.RemoveEntity(Delta.EntityRef);
      CommandStore.RemoveEntity(Delta.EntityRef);
      WorkerV2PendingPublishedDirtyStates.RemoveAll(
        [&Delta](const FCrowdWorkerDirtyStateRecord& Record)
        {
          return Record.EntityRef == Delta.EntityRef;
        });
      WorkerV2PendingPublishedEvents.RemoveAll(
        [&Delta](const FCrowdWorkerGameplayEvent& Event)
        {
          return Event.EntityRef == Delta.EntityRef;
        });
      if (!SpatialIndex.Despawn(Delta.EntityRef))
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::SpatialIndex;
        return false;
      }
      if (!EntityStateStore.Despawn(Delta.EntityRef))
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DirtyState;
        return false;
      }
      if (!EnqueueEntity(
        ECrowdWorkerDomainId::LifecycleInput,
        Delta.EntityRef, 1ull << 1))
        return false;
    }
    for (const FCrowdWorkerCommandDelta& Delta : Batch.Commands)
    {
      if (!EntityStateStore.Contains(Delta.EntityRef))
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DirtyState;
        return false;
      }
      const ECrowdWorkerQueueResult CommandResult =
        CommandStore.Enqueue(Delta);
      if (CommandResult != ECrowdWorkerQueueResult::Added
        && CommandResult
          != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DirtyState;
        return false;
      }
      if (!EnqueueEntity(
        ECrowdWorkerDomainId::Behavior,
        Delta.EntityRef, 1ull << 2))
        return false;
    }
    // Clock is the autonomous Behavior cadence. Production replicas must
    // advance every live entity locally even when the intent carries no
    // per-entity context or command record; WorkRing merges entities that
    // were already scheduled by a changed command or lifecycle input.
    const bool bHasBehaviorClock = DomainRegistry
      && DomainRegistry->HasDomain(
        ECrowdWorkerDomainId::Behavior);
    if (bHasBehaviorClock)
    {
      TArray<FCrowdStableEntityRef> BehaviorEntities;
      EntityStateStore.GetEntities(BehaviorEntities);
      for (const FCrowdStableEntityRef& EntityRef :
        BehaviorEntities)
      {
        if (!EnqueueEntity(
            ECrowdWorkerDomainId::Behavior,
            EntityRef, 1ull << 18))
          return false;
      }
    }
    FCrowdWorkerTargetControlResource EffectiveTargetControl;
    bool bHasTargetControl = false;
    const FCrowdWorkerResourceDelta* TargetDelta =
      Batch.ResourceDeltas.FindByPredicate([](
        const FCrowdWorkerResourceDelta& Delta)
      {
        return Delta.ResourceId
          == CrowdWorkerResourceIds::TargetControl;
      });
    if (TargetDelta)
    {
      bHasTargetControl =
        FCrowdWorkerTargetControlResourceCodec::Decode(
          TargetDelta->Payload, EffectiveTargetControl)
        && EffectiveTargetControl.Revision
          == TargetDelta->Revision;
    }
    else if (const FCrowdWorkerResourceRecord* TargetRecord =
      ResourceStore.FindCurrent(
        CrowdWorkerResourceIds::TargetControl))
    {
      bHasTargetControl =
        FCrowdWorkerTargetControlResourceCodec::Decode(
          TargetRecord->Payload, EffectiveTargetControl)
        && EffectiveTargetControl.Revision
          == TargetRecord->Revision;
    }
    if (TargetDelta && !bHasTargetControl)
    {
      LastWorkerV2Failure =
        ECrowdWorkerRuntimeV2Failure::ResourceValidation;
      return false;
    }
    FCrowdWorkerTargetObjectiveRevision EffectiveTargetObjective;
    bool bHasTargetObjective = false;
    const FCrowdWorkerObjectiveRevisionDelta* TargetObjectiveDelta =
      Batch.ObjectiveRevisions.FindByPredicate([](
        const FCrowdWorkerObjectiveRevisionDelta& Delta)
      {
        return Delta.ObjectiveId
          == CrowdWorkerTargetObjectiveIds::PrimaryTarget;
      });
    if (TargetObjectiveDelta)
    {
      bHasTargetObjective =
        FCrowdWorkerTargetObjectiveRevisionCodec::Decode(
          TargetObjectiveDelta->Payload, EffectiveTargetObjective);
    }
    else if (const FCrowdWorkerResourceRecord* ObjectiveRecord =
      ResourceStore.FindCurrent(
        CrowdWorkerResourceIds::ObjectiveRevision(
          CrowdWorkerTargetObjectiveIds::PrimaryTarget)))
    {
      bHasTargetObjective =
        FCrowdWorkerTargetObjectiveRevisionCodec::Decode(
          ObjectiveRecord->Payload, EffectiveTargetObjective);
    }
    if (TargetObjectiveDelta && !bHasTargetObjective)
    {
      LastWorkerV2Failure =
        ECrowdWorkerRuntimeV2Failure::ResourceValidation;
      return false;
    }
    const bool bHasTargetDomain = DomainRegistry
      && DomainRegistry->HasDomain(ECrowdWorkerDomainId::Target)
      && bHasTargetControl;
    const bool bHasTargetClock = bHasTargetDomain
      && bHasTargetObjective
      && EffectiveTargetObjective.TargetVelocity.SizeSquared()
        > UE_SMALL_NUMBER;
    const auto EnqueueTargetCohorts =
      [this, &EffectiveTargetControl](const uint64 ReasonMask)
    {
      for (const FCrowdWorkerTargetCohortInput& Cohort :
        EffectiveTargetControl.Cohorts)
      {
        FCrowdWorkerWorkItem Work;
        Work.Key.Domain = ECrowdWorkerDomainId::Target;
        Work.Key.Kind = ECrowdWorkerWorkKind::Cohort;
        Work.Key.ScopeKey =
          CrowdWorkerTargetWorkScopes::EncodeCohortKey(
            Cohort.CohortKey);
        Work.Priority = ECrowdWorkerWorkPriority::High;
        Work.ReasonMask = ReasonMask;
        const ECrowdWorkerQueueResult Result =
          WorkRing.EnqueueCurrent(MoveTemp(Work));
        if (Result != ECrowdWorkerQueueResult::Added
          && Result != ECrowdWorkerQueueResult::MergedDuplicate)
          return false;
      }
      return true;
    };
    if (bHasTargetClock)
    {
      if (!EnqueueTargetCohorts(1ull << 19))
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::WorkQueue;
        return false;
      }
    }
    const bool bHasProjectileClock = DomainRegistry
      && DomainRegistry->HasDomain(
        ECrowdWorkerDomainId::CombatReactive)
      && (ResourceStore.FindCurrent(
          CrowdWorkerResourceIds::ProjectileControl) != nullptr
        || Batch.ResourceDeltas.ContainsByPredicate([](
          const FCrowdWorkerResourceDelta& Delta)
        {
          return Delta.ResourceId
            == CrowdWorkerResourceIds::ProjectileControl;
        }));
    if (bHasProjectileClock)
    {
      FCrowdWorkerWorkItem CombatClock;
      CombatClock.Key.Domain =
        ECrowdWorkerDomainId::CombatReactive;
      CombatClock.Key.Kind = ECrowdWorkerWorkKind::Resource;
      CombatClock.Key.ScopeKey =
        CrowdWorkerResourceIds::ProjectileControl;
      CombatClock.Priority = ECrowdWorkerWorkPriority::Critical;
      CombatClock.ReasonMask = CrowdWorkerReasonMasks::CombatClock;
      const ECrowdWorkerQueueResult ClockResult =
        WorkRing.EnqueueCurrent(MoveTemp(CombatClock));
      if (ClockResult != ECrowdWorkerQueueResult::Added
        && ClockResult != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::WorkQueue;
        return false;
      }
    }
    for (const FCrowdWorkerObjectiveRevisionDelta& Delta :
      Batch.ObjectiveRevisions)
    {
      const uint64 ResourceId =
        CrowdWorkerResourceIds::ObjectiveRevision(Delta.ObjectiveId);
      const ECrowdWorkerQueueResult StageResult =
        ResourceStore.StageBuilding({
          ResourceId, Delta.Revision, Delta.Payload});
      if (StageResult != ECrowdWorkerQueueResult::Added
        && StageResult != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::ResourceValidation;
        return false;
      }
      if (bHasTargetDomain
        && Delta.ObjectiveId
          == CrowdWorkerTargetObjectiveIds::PrimaryTarget)
      {
        if (!EnqueueTargetCohorts(1ull << 13))
          return false;
      }
    }
    for (const FCrowdWorkerExternalGameplayInput& Delta : Batch.ExternalGameplayInputs)
    {
      if (Delta.EntityRef.IsUnset())
      {
        const ECrowdWorkerQueueResult StageResult =
          ResourceStore.StageBuilding({
            CrowdWorkerResourceIds::ExternalGameplayInput(
              Delta.InputTypeId),
            Delta.InputSequence,
            Delta.FullState});
        if (StageResult != ECrowdWorkerQueueResult::Added
          && StageResult != ECrowdWorkerQueueResult::MergedDuplicate)
          return false;
        continue;
      }
      if (Delta.InputTypeId == static_cast<uint16>(
          ECrowdWorkerExternalGameplayInputType::
            MovementProfileRevision))
      {
        FCrowdWorkerMovementControlEntry Profile;
        if (!FCrowdWorkerMovementProfileCodec::Decode(
            Delta.FullState, Profile)
          || Profile.EntityRef != Delta.EntityRef)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::DirtyState;
          return false;
        }
        FCrowdWorkerDirtyStateRecord ProfileRecord;
        ProfileRecord.EntityRef = Delta.EntityRef;
        ProfileRecord.Field = ECrowdWorkerField::MovementProfile;
        ProfileRecord.Generation = Batch.Generation;
        // Input-owned fields can arrive before the first Worker epoch. Match
        // ApplyInputState's epoch-one baseline instead of emitting an invalid
        // epoch-zero dirty record during bootstrap.
        ProfileRecord.WorkerEpoch = FMath::Max<uint64>(1, WorkerEpoch);
        ProfileRecord.StateRevision = Delta.InputSequence;
        ProfileRecord.SourceInputSequence = Delta.InputSequence;
        ProfileRecord.Payload = Delta.FullState;
        const ECrowdWorkerQueueResult ProfileResult =
          EntityStateStore.ApplyDirty(ProfileRecord);
        if (ProfileResult != ECrowdWorkerQueueResult::Added
          && ProfileResult != ECrowdWorkerQueueResult::Replaced
          && ProfileResult
            != ECrowdWorkerQueueResult::MergedDuplicate)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::DirtyState;
          return false;
        }
        const bool bHasMovementControl =
          ResourceStore.FindCurrent(
            CrowdWorkerResourceIds::MovementControl) != nullptr
          || Batch.ResourceDeltas.ContainsByPredicate([](
            const FCrowdWorkerResourceDelta& Resource)
          {
            return Resource.ResourceId
              == CrowdWorkerResourceIds::MovementControl;
          });
        if (DomainRegistry && DomainRegistry->HasDomain(
            ECrowdWorkerDomainId::MovementPlanning))
        {
          FCrowdWorkerWorkItem Planning;
          Planning.Key.Domain =
            ECrowdWorkerDomainId::MovementPlanning;
          Planning.Key.Kind = bHasMovementControl
            ? ECrowdWorkerWorkKind::Resource
            : ECrowdWorkerWorkKind::Entity;
          if (bHasMovementControl)
          {
            Planning.Key.ScopeKey =
              CrowdWorkerResourceIds::MovementControl;
          }
          else
          {
            Planning.Key.PrimaryEntity = Delta.EntityRef;
          }
          Planning.Priority = ECrowdWorkerWorkPriority::High;
          Planning.ReasonMask = 1ull << 14;
          const ECrowdWorkerQueueResult PlanningResult =
            WorkRing.EnqueueCurrent(MoveTemp(Planning));
          if (PlanningResult != ECrowdWorkerQueueResult::Added
            && PlanningResult
              != ECrowdWorkerQueueResult::MergedDuplicate)
            return false;
        }
        continue;
      }
      const ECrowdWorkerQueueResult StateResult =
        EntityStateStore.ApplyInputState(
          Delta.EntityRef, Batch.Generation,
          Delta.InputSequence, Delta.FullState);
      if (StateResult != ECrowdWorkerQueueResult::Replaced
        && StateResult
          != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DirtyState;
        return false;
      }
      if (!bHasMovementPlanningInput
        && !EnqueueEntity(
          ECrowdWorkerDomainId::Movement,
          Delta.EntityRef, 1ull << 3))
        return false;
    }
    for (const FCrowdWorkerResourceDelta& Delta :
      Batch.ResourceDeltas)
    {
      const ECrowdWorkerQueueResult StageResult =
        ResourceStore.StageBuilding({
          Delta.ResourceId, Delta.Revision, Delta.Payload});
      if (StageResult != ECrowdWorkerQueueResult::Added
        && StageResult
          != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::ResourceValidation;
        return false;
      }
      FCrowdWorkerWorkItem Work;
      Work.Key.Domain = ECrowdWorkerDomainId::FlowResource;
      Work.Key.Kind = ECrowdWorkerWorkKind::Resource;
      Work.Key.ScopeKey = Delta.ResourceId;
      Work.Priority = ECrowdWorkerWorkPriority::High;
      Work.ReasonMask = 1ull << 5;
      const ECrowdWorkerQueueResult WorkResult =
        WorkRing.EnqueueCurrent(MoveTemp(Work));
      if (WorkResult != ECrowdWorkerQueueResult::Added
        && WorkResult
          != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::WorkQueue;
        return false;
      }
      if (Delta.ResourceId
          == CrowdWorkerResourceIds::MovementControl
        && bHasMovementPlanningInput)
      {
        FCrowdWorkerWorkItem Planning;
        Planning.Key.Domain =
          ECrowdWorkerDomainId::MovementPlanning;
        Planning.Key.Kind = ECrowdWorkerWorkKind::Resource;
        Planning.Key.ScopeKey = Delta.ResourceId;
        Planning.Priority = ECrowdWorkerWorkPriority::High;
        Planning.ReasonMask = 1ull << 9;
        const ECrowdWorkerQueueResult PlanningResult =
          WorkRing.EnqueueCurrent(MoveTemp(Planning));
        if (PlanningResult != ECrowdWorkerQueueResult::Added
          && PlanningResult
            != ECrowdWorkerQueueResult::MergedDuplicate)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::WorkQueue;
          return false;
        }
      }
      if (Delta.ResourceId
          == CrowdWorkerResourceIds::TargetControl
        && DomainRegistry->HasDomain(
          ECrowdWorkerDomainId::Target))
      {
        if (!bHasTargetControl
          || !EnqueueTargetCohorts(1ull << 10))
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::WorkQueue;
          return false;
        }
      }
      if (Delta.ResourceId
          == CrowdWorkerResourceIds::ProjectileControl
        && DomainRegistry->HasDomain(
          ECrowdWorkerDomainId::CombatReactive))
      {
        FCrowdWorkerWorkItem Combat;
        Combat.Key.Domain =
          ECrowdWorkerDomainId::CombatReactive;
        Combat.Key.Kind = ECrowdWorkerWorkKind::Resource;
        Combat.Key.ScopeKey = Delta.ResourceId;
        Combat.Priority = ECrowdWorkerWorkPriority::Critical;
        Combat.ReasonMask = 1ull << 11;
        const ECrowdWorkerQueueResult CombatResult =
          WorkRing.EnqueueCurrent(MoveTemp(Combat));
        if (CombatResult != ECrowdWorkerQueueResult::Added
          && CombatResult
            != ECrowdWorkerQueueResult::MergedDuplicate)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::WorkQueue;
          return false;
        }
      }
    }
    return true;
  }

  bool ApplyWorkerV2MergedOutput(
    FCrowdWorkerDomainOutput& Output)
  {
    for (FCrowdWorkerDependencyDeclaration& Declaration :
      Output.DeclaredDependencies)
    {
      const ECrowdWorkerQueueResult Result =
        DependencyIndex.AddDependency(
          Declaration.Source,
          MoveTemp(Declaration.Dependent));
      if (Result != ECrowdWorkerQueueResult::Added
        && Result != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DependencyIndex;
        return false;
      }
    }
    for (const FCrowdWorkerDependencyObservation& Observation :
      Output.ObservedDependencies)
    {
      if (!Observation.IsValid()
        || !DependencyIndex.ContainsDependency(
          Observation.Source, Observation.Dependent))
      {
        ++CoverageAuditFailureCount;
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::CoverageAudit;
        return false;
      }
    }
    for (FCrowdWorkerWorkItem& Next : Output.NextWork)
    {
      const ECrowdWorkerQueueResult Result =
        WorkRing.EnqueueCurrent(MoveTemp(Next));
      if (Result != ECrowdWorkerQueueResult::Added
        && Result != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::WorkQueue;
        return false;
      }
    }
    for (FCrowdWorkerWakeup& Wakeup : Output.Wakeups)
    {
      const ECrowdWorkerQueueResult Result =
        TimeWheel.Schedule(MoveTemp(Wakeup));
      if (Result != ECrowdWorkerQueueResult::Added
        && Result != ECrowdWorkerQueueResult::Replaced
        && Result != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::WakeupQueue;
        return false;
      }
    }
    for (FCrowdWorkerDirtyStateRecord& Dirty :
      Output.DirtyStates)
    {
      const bool bSpatialStateChanged =
        Dirty.Field == ECrowdWorkerField::Movement
        || Dirty.Field == ECrowdWorkerField::InputSnapshot;
      const ECrowdWorkerQueueResult StateResult =
        EntityStateStore.ApplyDirty(Dirty);
      if (StateResult != ECrowdWorkerQueueResult::Added
        && StateResult != ECrowdWorkerQueueResult::Replaced
        && StateResult
          != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DirtyState;
        return false;
      }
      if (bSpatialStateChanged
        && !SpatialIndex.UpdateEntity(
          EntityStateStore, Dirty.EntityRef))
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::SpatialIndex;
        return false;
      }
      TArray<FCrowdWorkerWorkItem> Dependents;
      FCrowdWorkerDependencyKey DependencySource;
      DependencySource.Kind =
        ECrowdWorkerDependencyKind::Entity;
      DependencySource.EntityRef = Dirty.EntityRef;
      DependencySource.ScopeKey =
        CrowdWorkerRuntimeV2DependencyScopeForField(
          Dirty.Field);
      DependencyIndex.CollectDependents(
        DependencySource, Dependents);
      for (FCrowdWorkerWorkItem& Dependent : Dependents)
      {
        const uint8 SourceRank =
          FieldOwnerExecutionRank(Dirty.Field);
        const uint8 DependentRank =
          CrowdWorkerRuntimeV2DomainExecutionRank(
            Dependent.Key.Domain);
        if (SourceRank == MAX_uint8
          || DependentRank == MAX_uint8)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::DependencyIndex;
          return false;
        }
        const bool bDeferToNextEpoch =
          DependentRank <= SourceRank;
        Dependent.EnqueueEpoch = bDeferToNextEpoch
          ? WorkerEpoch + 1 : WorkerEpoch;
        Dependent.CorrectionRevision = FMath::Max(
          Dependent.CorrectionRevision,
          Dirty.CorrectionRevision);
        Dependent.ReasonMask |= 1ull << 10;
        const ECrowdWorkerQueueResult EnqueueResult =
          bDeferToNextEpoch
            ? WorkRing.EnqueueNext(MoveTemp(Dependent))
            : WorkRing.EnqueueCurrent(MoveTemp(Dependent));
        if (EnqueueResult != ECrowdWorkerQueueResult::Added
          && EnqueueResult
            != ECrowdWorkerQueueResult::MergedDuplicate)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::WorkQueue;
          return false;
        }
      }
      const ECrowdWorkerQueueResult Result =
        DirtyStateStore.MarkDirty(MoveTemp(Dirty));
      if (Result != ECrowdWorkerQueueResult::Added
        && Result != ECrowdWorkerQueueResult::Replaced
        && Result != ECrowdWorkerQueueResult::MergedDuplicate)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DirtyState;
        return false;
      }
    }
    for (FCrowdWorkerGameplayEvent& Event :
      Output.OrderedEvents)
    {
      const ECrowdWorkerQueueResult Result =
        OrderedEventStore.Append(MoveTemp(Event));
      if (Result != ECrowdWorkerQueueResult::Added)
      {
        ++OrderedEventLossCount;
        LastWorkerV2Failure =
          Result == ECrowdWorkerQueueResult::RejectedCapacity
          ? ECrowdWorkerRuntimeV2Failure::OrderedEventCapacity
          : ECrowdWorkerRuntimeV2Failure::OrderedEventSequence;
        return false;
      }
    }
    // Command admission is journaled until every state/event output from the
    // same deterministic merge has passed validation. ACK is the final
    // Owner-barrier action, so a failed apply cannot lose an unapplied input.
    for (const uint64 InputSequence :
      Output.ConsumedCommandInputSequences)
    {
      if (!CommandStore.Acknowledge(InputSequence))
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DirtyState;
        return false;
      }
    }
    return true;
  }

  bool PublishAuthorityDigestAtBarrier()
  {
    struct FScopedState
    {
      FCrowdWorkerAuthorityScopeKey Scope;
      FCrowdWorkerDirtyStateRecord Record;
    };
    TArray<FCrowdWorkerDirtyStateRecord> CompleteStates;
    EntityStateStore.GetStateRecords(CompleteStates);
    TArray<FScopedState> Scoped;
    Scoped.Reserve(CompleteStates.Num());
    for (const FCrowdWorkerDirtyStateRecord& Record : CompleteStates)
      Scoped.Add({ResolveAuthorityScope(Record, EntityStateStore), Record});
    Scoped.Sort([](const FScopedState& A, const FScopedState& B)
    {
      if (!(A.Scope == B.Scope)) return A.Scope < B.Scope;
      if (A.Record.EntityRef != B.Record.EntityRef)
        return A.Record.EntityRef < B.Record.EntityRef;
      return static_cast<uint8>(A.Record.Field)
        < static_cast<uint8>(B.Record.Field);
    });

    FCrowdWorkerAuthorityDigestBatch Digest;
    Digest.Generation = Generation.Load();
    Digest.DigestSequence = NextAuthorityDigestSequence++;
    Digest.SimulationTick = AbsoluteSimulationTick;
    Digest.ThroughInputSequence = LastAppliedInputSequence;
    FCrowdStableEntityRef PreviousMember;
    for (const FScopedState& Value : Scoped)
    {
      if (Digest.Entries.IsEmpty()
        || !(Digest.Entries.Last().Scope == Value.Scope))
      {
        FCrowdWorkerAuthorityDigestEntry& Entry =
          Digest.Entries.AddDefaulted_GetRef();
        Entry.Scope = Value.Scope;
        Entry.SimulationTick = AbsoluteSimulationTick;
        Entry.ThroughInputSequence = LastAppliedInputSequence;
        Entry.StableHash = AsyncFnvOffset64;
        PreviousMember = {};
      }
      FCrowdWorkerAuthorityDigestEntry& Entry = Digest.Entries.Last();
      if (PreviousMember != Value.Record.EntityRef)
      {
        ++Entry.EntityCount;
        PreviousMember = Value.Record.EntityRef;
      }
      FoldAuthorityState(Entry.StableHash, Value.Record);
    }
    Digest.RecalculateStableHash();
    if (!Digest.IsValid(Config.NetworkState)) return false;
    FScopeLock Lock(&SnapshotMutex);
    FAuthorityDigestSnapshot& Snapshot =
      AuthorityDigestHistory.AddDefaulted_GetRef();
    Snapshot.Digest = MoveTemp(Digest);
    Snapshot.States = MoveTemp(CompleteStates);
    while (AuthorityDigestHistory.Num() > 4)
      AuthorityDigestHistory.RemoveAt(0, 1, EAllowShrinking::No);
    ++AuthorityDigestCount;
    return true;
  }

  bool ApplyAuthorityCorrectionsAtBarrier()
  {
    TArray<FCrowdWorkerAuthorityCorrectionBatch> Pending;
    {
      FScopeLock Lock(&InputMutex);
      Pending = MoveTemp(AuthorityCorrectionQueue);
      AuthorityCorrectionQueue.Reset();
    }
    for (const FCrowdWorkerAuthorityCorrectionBatch& Correction : Pending)
    {
      if (Correction.Generation != Generation.Load()
        || Correction.CorrectionSequence
          != LastAppliedAuthorityCorrectionSequence + 1
        || Correction.ApplySimulationTick > AbsoluteSimulationTick
        || !Correction.IsValid(Config.NetworkState))
        return false;
      LastCorrectionBeforePositionErrorCm = 0.0;
      LastCorrectionAfterPositionErrorCm = 0.0;
      LastCorrectionBeforeVelocityErrorCmps = 0.0;
      LastCorrectionAfterVelocityErrorCmps = 0.0;
      LastCorrectionBeforeYawErrorDegrees = 0.0;
      LastCorrectionAfterYawErrorDegrees = 0.0;
      LastCorrectionBeforeCombatMismatchCount = 0;
      LastCorrectionAfterCombatMismatchCount = 0;
      LastCorrectionEntityCount =
        Correction.AuthoritativeMembers.Num();
      LastCorrectionScopeCount = Correction.Scopes.Num();
      const auto AccumulateCorrectionError = [this](
        const FCrowdWorkerDirtyStateRecord* Local,
        const FCrowdWorkerDirtyStateRecord& Authority,
        const bool bAfter)
      {
        if (!Local) return;
        if (Authority.Field == ECrowdWorkerField::Movement)
        {
          FCrowdWorkerMovementState LocalMovement;
          FCrowdWorkerMovementState AuthorityMovement;
          if (!FCrowdWorkerMovementStateCodec::Decode(
              Local->Payload, LocalMovement)
            || !FCrowdWorkerMovementStateCodec::Decode(
              Authority.Payload, AuthorityMovement))
            return;
          const double PositionError = FVector::Distance(
            LocalMovement.Position, AuthorityMovement.Position);
          const double VelocityError = FVector::Distance(
            LocalMovement.Velocity, AuthorityMovement.Velocity);
          const double YawError = FMath::Abs(
            FMath::FindDeltaAngleDegrees(
              LocalMovement.YawDegrees,
              AuthorityMovement.YawDegrees));
          if (bAfter)
          {
            LastCorrectionAfterPositionErrorCm = FMath::Max(
              LastCorrectionAfterPositionErrorCm, PositionError);
            LastCorrectionAfterVelocityErrorCmps = FMath::Max(
              LastCorrectionAfterVelocityErrorCmps, VelocityError);
            LastCorrectionAfterYawErrorDegrees = FMath::Max(
              LastCorrectionAfterYawErrorDegrees, YawError);
          }
          else
          {
            LastCorrectionBeforePositionErrorCm = FMath::Max(
              LastCorrectionBeforePositionErrorCm, PositionError);
            LastCorrectionBeforeVelocityErrorCmps = FMath::Max(
              LastCorrectionBeforeVelocityErrorCmps, VelocityError);
            LastCorrectionBeforeYawErrorDegrees = FMath::Max(
              LastCorrectionBeforeYawErrorDegrees, YawError);
          }
        }
        else if (Authority.Field == ECrowdWorkerField::Combat
          && Local->Payload.StableHash != Authority.Payload.StableHash)
        {
          if (bAfter)
            ++LastCorrectionAfterCombatMismatchCount;
          else
            ++LastCorrectionBeforeCombatMismatchCount;
        }
      };
      TSet<FCrowdStableEntityRef> Members;
      Members.Reserve(Correction.AuthoritativeMembers.Num());
      for (const FCrowdStableEntityRef& Member :
        Correction.AuthoritativeMembers)
        Members.Add(Member);
      TSet<FCrowdStableEntityRef> SpatiallyAffectedEntities;
      TArray<FCrowdWorkerDirtyStateRecord> LocalStates;
      EntityStateStore.GetStateRecords(LocalStates);
      for (const FCrowdWorkerDirtyStateRecord& Local : LocalStates)
      {
        const FCrowdWorkerAuthorityScopeKey LocalScope =
          ResolveAuthorityScope(Local, EntityStateStore);
        if (Correction.Scopes.Contains(LocalScope)
          && !Members.Contains(Local.EntityRef)
          && Local.Field != ECrowdWorkerField::InputSnapshot)
        {
          EntityStateStore.RemoveAuthoritativeField(
            Local.EntityRef, Local.Field);
          if (Local.Field == ECrowdWorkerField::Movement
            || Local.Field == ECrowdWorkerField::InputSnapshot)
            SpatiallyAffectedEntities.Add(Local.EntityRef);
        }
      }
      for (const FCrowdWorkerAuthorityTombstone& Tombstone :
        Correction.Tombstones)
      {
        EntityStateStore.RemoveAuthoritativeField(
          Tombstone.EntityRef, Tombstone.Field);
        if (Tombstone.Field == ECrowdWorkerField::Movement
          || Tombstone.Field == ECrowdWorkerField::InputSnapshot)
          SpatiallyAffectedEntities.Add(Tombstone.EntityRef);
      }
      for (const FCrowdWorkerDirtyStateRecord& Record : Correction.Records)
      {
        AccumulateCorrectionError(
          EntityStateStore.Find(Record.EntityRef, Record.Field),
          Record,
          false);
        WorkRing.InvalidateEntityRevision(
          Record.EntityRef, Record.CorrectionRevision);
        TimeWheel.InvalidateEntityRevision(
          Record.EntityRef, Record.CorrectionRevision);
        DirtyStateStore.InvalidateEntityRevision(
          Record.EntityRef, Record.CorrectionRevision);
        if (!EntityStateStore.ApplyAuthoritativeDirty(Record))
          return false;
        if (Record.Field == ECrowdWorkerField::Movement)
        {
          // Facing, Particle and the local-predictive plan are derived from
          // Movement. Keeping any of them after a sparse Movement correction
          // lets the next planning tick read the pre-correction continuation
          // and immediately reintroduce the error.
          EntityStateStore.RemoveAuthoritativeField(
            Record.EntityRef, ECrowdWorkerField::Facing);
          EntityStateStore.RemoveAuthoritativeField(
            Record.EntityRef, ECrowdWorkerField::Particle);
          EntityStateStore.RemoveAuthoritativeField(
            Record.EntityRef, ECrowdWorkerField::MovementPlan);
          if (DomainRegistry
            && DomainRegistry->HasDomain(
              ECrowdWorkerDomainId::MovementPlanning))
          {
            FCrowdWorkerWorkItem Planning;
            Planning.Key.Domain =
              ECrowdWorkerDomainId::MovementPlanning;
            Planning.Key.Kind = ECrowdWorkerWorkKind::Resource;
            Planning.Key.ScopeKey =
              CrowdWorkerResourceIds::MovementControl;
            Planning.Priority = ECrowdWorkerWorkPriority::Critical;
            Planning.CorrectionRevision =
              Record.CorrectionRevision;
            Planning.ReasonMask = 1ull << 15;
            const ECrowdWorkerQueueResult PlanningResult =
              WorkRing.EnqueueCurrent(MoveTemp(Planning));
            if (PlanningResult != ECrowdWorkerQueueResult::Added
              && PlanningResult
                != ECrowdWorkerQueueResult::MergedDuplicate)
              return false;
          }
        }
        if (Record.Field == ECrowdWorkerField::Movement
          || Record.Field == ECrowdWorkerField::InputSnapshot)
          SpatiallyAffectedEntities.Add(Record.EntityRef);
        AccumulateCorrectionError(
          EntityStateStore.Find(Record.EntityRef, Record.Field),
          Record,
          true);
        FCrowdWorkerDependencyKey DependencySource;
        DependencySource.Kind = ECrowdWorkerDependencyKind::Entity;
        DependencySource.EntityRef = Record.EntityRef;
        DependencySource.ScopeKey =
          CrowdWorkerRuntimeV2DependencyScopeForField(Record.Field);
        TArray<FCrowdWorkerWorkItem> Dependents;
        DependencyIndex.CollectDependents(
          DependencySource, Dependents);
        for (FCrowdWorkerWorkItem& Work : Dependents)
        {
          Work.Priority = ECrowdWorkerWorkPriority::Critical;
          Work.EnqueueEpoch = WorkRing.GetEpoch();
          Work.CorrectionRevision = FMath::Max(
            Work.CorrectionRevision,
            Record.CorrectionRevision);
          Work.ReasonMask |= 1ull << 14;
          const ECrowdWorkerQueueResult Result =
            WorkRing.EnqueueCurrent(MoveTemp(Work));
          if (Result != ECrowdWorkerQueueResult::Added
            && Result != ECrowdWorkerQueueResult::MergedDuplicate)
            return false;
        }
      }
      for (const FCrowdStableEntityRef& EntityRef :
        SpatiallyAffectedEntities)
      {
        const bool bUpdated = EntityStateStore.Contains(EntityRef)
          ? SpatialIndex.UpdateEntity(EntityStateStore, EntityRef)
          : SpatialIndex.Despawn(EntityRef);
        if (!bUpdated) return false;
      }
      LastAppliedAuthorityCorrectionSequence =
        Correction.CorrectionSequence;
#if WITH_DEV_AUTOMATION_TESTS
      {
        FScopeLock Lock(&InputMutex);
        if (PendingDiagnosticMovementCorruption.IsSet()
          && Correction.AuthoritativeMembers.Contains(
            PendingDiagnosticMovementCorruption->EntityRef))
          PendingDiagnosticMovementCorruption.Reset();
      }
#endif
      ++AuthorityCorrectionCount;
      AuthorityCorrectionEntityCount +=
        Correction.AuthoritativeMembers.Num();
      AuthorityCorrectionScopeCount += Correction.Scopes.Num();
      ConsecutivePredictionEpochsWithoutCorrection = 0;
      bCorrectionAppliedSinceLastEpoch = true;
    }
    return true;
  }

#if WITH_DEV_AUTOMATION_TESTS
  bool ApplyDiagnosticMovementCorruptionAtDigestBarrier()
  {
    if (AbsoluteSimulationTick == 0 || AbsoluteSimulationTick % 30 != 0)
      return true;
    TOptional<FDiagnosticMovementCorruption> Pending;
    {
      FScopeLock Lock(&InputMutex);
      Pending = PendingDiagnosticMovementCorruption;
    }
    if (!Pending.IsSet()) return true;
    const FDiagnosticMovementCorruption& Corruption = Pending.GetValue();
    if (Corruption.ExpectedGeneration != Generation.Load()
      || !Corruption.EntityRef.IsValid())
      return false;
    const FCrowdWorkerDirtyStateRecord* Existing = EntityStateStore.Find(
      Corruption.EntityRef, ECrowdWorkerField::Movement);
    if (!Existing) return false;
    FCrowdWorkerMovementState Movement;
    if (!FCrowdWorkerMovementStateCodec::Decode(
        Existing->Payload, Movement))
      return false;
    const auto CorruptWithinSpatialCell = [](
      const float Value,
      const float RequestedOffset)
    {
      if (FMath::IsNearlyZero(RequestedOffset)) return Value;
      constexpr float CellSizeCm = 400.0f;
      const float CellMinimum = FMath::FloorToFloat(Value / CellSizeCm)
        * CellSizeCm;
      const float OffsetWithinCell = Value - CellMinimum;
      // Stay within the same digest cell so this test proves one-scope
      // correction, not a two-cell membership change.
      return CellMinimum + (OffsetWithinCell < CellSizeCm * 0.5f
        ? CellSizeCm * 0.875f : CellSizeCm * 0.125f);
    };
    Movement.Position.X = CorruptWithinSpatialCell(
      Movement.Position.X, Corruption.PositionOffset.X);
    Movement.Position.Y = CorruptWithinSpatialCell(
      Movement.Position.Y, Corruption.PositionOffset.Y);
    Movement.Position.Z += Corruption.PositionOffset.Z;
    Movement.Velocity += Corruption.VelocityOffset;
    Movement.YawDegrees = FRotator::NormalizeAxis(
      Movement.YawDegrees + Corruption.YawOffsetDegrees);
    FCrowdWorkerDirtyStateRecord Corrupted = *Existing;
    Corrupted.WorkerEpoch = WorkerEpoch;
    Corrupted.StateRevision = FMath::Max(
      Existing->StateRevision + 1, WorkerEpoch);
    Corrupted.SourceInputSequence = LastAppliedInputSequence;
    if (!FCrowdWorkerMovementStateCodec::Encode(
        Movement, Corrupted.Payload)
      || !EntityStateStore.ApplyAuthoritativeDirty(Corrupted)
      || !SpatialIndex.UpdateEntity(EntityStateStore, Corruption.EntityRef))
      return false;
    return true;
  }
#endif

  bool CompleteWorkerV2SyntheticEpoch()
  {
    if (bCorrectionAppliedSinceLastEpoch)
    {
      bCorrectionAppliedSinceLastEpoch = false;
    }
    else
    {
      ++ConsecutivePredictionEpochsWithoutCorrection;
      MaxPredictionEpochsWithoutCorrection = FMath::Max(
        MaxPredictionEpochsWithoutCorrection,
        ConsecutivePredictionEpochsWithoutCorrection);
    }
    const bool bPropagationDeferred = !WorkRing.IsCurrentEmpty();
    if (bPropagationDeferred)
    {
      ++PropagationLimitHitCount;
      WorkRing.DeferCurrentToNext();
    }
    TArray<FCrowdWorkerDirtyStateRecord> CompletedDirty;
    DirtyStateStore.Drain(CompletedDirty);
    TArray<FCrowdWorkerGameplayEvent> CompletedEvents;
    OrderedEventStore.Drain(CompletedEvents);
    WorkRing.AdvanceEpoch();
 #if WITH_DEV_AUTOMATION_TESTS
    if (!bPropagationDeferred
      && !ApplyDiagnosticMovementCorruptionAtDigestBarrier())
    {
      LastWorkerV2Failure = ECrowdWorkerRuntimeV2Failure::Publication;
      return false;
    }
 #endif
    if (!bPropagationDeferred
      && AbsoluteSimulationTick % 30 == 0
      && !PublishAuthorityDigestAtBarrier())
    {
      LastWorkerV2Failure =
        ECrowdWorkerRuntimeV2Failure::Publication;
      return false;
    }
    const bool bNetworkPublishDue = WorkerEpoch > 0
      && AbsoluteSimulationTick > 0
      && (WorkerEpoch == 1
        || WorkerEpoch % static_cast<uint64>(
          Config.NetworkPublishIntervalEpochs) == 0);
    if (bNetworkPublishDue)
    {
      FCrowdWorkerNetworkContinuationState Continuation;
      WorkRing.GetSnapshot(Continuation.WorkRing);
      TimeWheel.GetScheduled(Continuation.Wakeups);
      DependencyIndex.GetRecords(Continuation.Dependencies);
      CommandStore.GetRecords(Continuation.Commands);
      EntityStateStore.GetLifecycleWatermarks(
        Continuation.LifecycleWatermarks);
      TArray<FCrowdWorkerDirtyStateRecord> CompleteStates;
      EntityStateStore.GetStateRecords(CompleteStates);
      TArray<FCrowdWorkerResourceRecord> CompleteResources;
      ResourceStore.GetCurrentRecords(CompleteResources);
      FCrowdWorkerCheckpoint Checkpoint;
      Checkpoint.Generation = Generation.Load();
      Checkpoint.WorkerEpoch = WorkerEpoch;
      Checkpoint.AbsoluteSimulationTick = AbsoluteSimulationTick;
      Checkpoint.FixedSimulationQuantumSeconds =
        Config.FixedSimulationQuantumSeconds;
      Checkpoint.LastAppliedInputSequence =
        LastAppliedInputSequence;
      Checkpoint.LastOrderedEventSequence =
        OrderedEventStore.GetLastAcceptedEventSequence();
      Checkpoint.EntityStateHash =
        EntityStateStore.CalculateStableHash();
      Checkpoint.ResourceRevisionHash =
        ResourceStore.CalculateCurrentStableHash();
      Checkpoint.RecalculateStableHash();
      {
        FScopeLock NetworkLock(&SnapshotMutex);
        if (!NetworkStatePublisher.CommitEpoch(
          Checkpoint,
          CompleteStates,
          CompleteResources,
          Continuation))
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::Publication;
          return false;
        }
      }
    }
    if (IsWorkerV2Enabled())
    {
      WorkerV2PendingPublishedDirtyStates.Append(
        MoveTemp(CompletedDirty));
      WorkerV2PendingPublishedEvents.Append(
        MoveTemp(CompletedEvents));
      if (!WorkerV2PendingPublishedDirtyStates.IsEmpty()
        || !WorkerV2PendingPublishedEvents.IsEmpty())
        bPublishPending.Store(true);
    }
    WorkerV2EpochPropagationRound = 0;
    WorkerV2PendingStageWork.Reset();
    return true;
  }

  bool FlushWorkerV2DomainResultsAtBarrier()
  {
    if (WorkerV2PendingPublishedDirtyStates.IsEmpty()
      && WorkerV2PendingPublishedEvents.IsEmpty())
      return true;
    for (const FCrowdWorkerDirtyStateRecord& Dirty :
      WorkerV2PendingPublishedDirtyStates)
    {
      FCrowdWorkerStatePatch Patch;
      Patch.EntityRef = Dirty.EntityRef;
      Patch.StateFieldId =
        1 + static_cast<uint16>(Dirty.Field);
      Patch.Generation = Dirty.Generation;
      Patch.WorkerEpoch = Dirty.WorkerEpoch;
      Patch.SourceInputSequence = Dirty.SourceInputSequence;
      Patch.DirtyMask =
        CrowdWorkerRuntimeV2FieldMask(Dirty.Field);
      Patch.State.StateRevision = Dirty.StateRevision;
      Patch.State.Payload = Dirty.Payload;
      Patch.RecalculateStableHash();
      const ECrowdWorkerAppendResult AppendResult =
        PublishedExchange.AppendStatePatch(Patch);
      if (AppendResult == ECrowdWorkerAppendResult::Violation
        || AppendResult
          == ECrowdWorkerAppendResult::RejectedNotInitialized)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::Publication;
        return false;
      }
      ++WorkerV2PublishedDirtyStateCount;
    }
    WorkerV2PendingPublishedDirtyStates.Reset();
    for (const FCrowdWorkerGameplayEvent& Event :
      WorkerV2PendingPublishedEvents)
    {
      const ECrowdWorkerAppendResult AppendResult =
        PublishedExchange.AppendOrderedEvent(Event);
      if (AppendResult == ECrowdWorkerAppendResult::Violation
        || AppendResult
          == ECrowdWorkerAppendResult::RejectedNotInitialized)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::Publication;
        return false;
      }
      ++WorkerV2PublishedOrderedEventCount;
    }
    WorkerV2PendingPublishedEvents.Reset();

    FCrowdWorkerPublishMetadata Metadata;
    Metadata.Generation = Generation.Load();
    Metadata.PublishSequence = NextPublishSequence;
    Metadata.MinWorkerEpoch = WorkerEpoch;
    Metadata.MaxWorkerEpoch = WorkerEpoch;
    Metadata.LastAppliedInputSequence =
      LastAppliedInputSequence;
    Metadata.PublishedSimulationTimeSeconds =
      SimulationTimeSeconds;
    const ECrowdWorkerPublishResult PublishResult =
      PublishedExchange.TryPublishBuildingBatch(Metadata);
    if (PublishResult == ECrowdWorkerPublishResult::Published)
    {
      LastPublishCycles.Store(FPlatformTime::Cycles64());
      ++NextPublishSequence;
      bPublishPending.Store(false);
      return true;
    }
    if (PublishResult
        == ECrowdWorkerPublishResult::DeferredPublishedOccupied)
    {
      bPublishPending.Store(true);
      return true;
    }
    LastWorkerV2Failure =
      ECrowdWorkerRuntimeV2Failure::Publication;
    return false;
  }

  EWorkerV2AdvanceResult AdvanceWorkerV2SyntheticEpoch()
  {
    if (ActiveWorkerV2ShardExecution)
    {
      if (!ActiveWorkerV2ShardExecution->IsCompleted())
        return EWorkerV2AdvanceResult::Pending;
      if (ActiveWorkerV2ShardExecution->Generation
          != Generation.Load()
        || ActiveWorkerV2ShardExecution->WorkerEpoch
          != WorkerEpoch)
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DomainExecution;
        ActiveWorkerV2ShardExecution.Reset();
        return EWorkerV2AdvanceResult::Failed;
      }
      for (const FCrowdWorkerDomainShardResult& Result :
        ActiveWorkerV2ShardExecution->Results)
      {
        if (!Result.bSucceeded)
        {
          UE_LOG(LogTemp, Error,
            TEXT("CrowdWorkerDomainExecutionRejected domain=%u shard=%u propagation_round=%d generation=%llu epoch=%llu input=%llu shard_count=%d"),
            static_cast<uint32>(Result.Domain),
            Result.ShardOrdinal,
            WorkerV2EpochPropagationRound,
            Generation.Load(),
            WorkerEpoch,
            LastAppliedInputSequence,
            ActiveWorkerV2ShardExecution->Results.Num());
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::DomainExecution;
          ActiveWorkerV2ShardExecution.Reset();
          return EWorkerV2AdvanceResult::Failed;
        }
      }
      WorkerV2ShardCompletionCount +=
        ActiveWorkerV2ShardExecution->Results.Num();
      FCrowdWorkerDomainOutput Output;
      const bool bMerged =
        FCrowdWorkerDeterministicShardPlanner::Merge(
        ActiveWorkerV2ShardExecution->Results,
        Config.WorkerV2.MaxDirtyEntities,
        Config.ContractLimits.MaxPayloadBytes,
        Config.WorkerV2.MaxOrderedEvents,
        Output,
        OrderedEventStore.GetLastAcceptedEventSequence() + 1);
      if (!bMerged)
      {
        for (const FCrowdWorkerDomainShardResult& Result :
          ActiveWorkerV2ShardExecution->Results)
        {
          UE_LOG(LogTemp, Error,
            TEXT("CrowdWorkerShardMergeRejected domain=%u shard=%u succeeded=%d next=%d wakeups=%d dirty=%d events=%d declarations=%d observations=%d"),
            static_cast<uint32>(Result.Domain),
            Result.ShardOrdinal,
            Result.bSucceeded ? 1 : 0,
            Result.Output.NextWork.Num(),
            Result.Output.Wakeups.Num(),
            Result.Output.DirtyStates.Num(),
            Result.Output.OrderedEvents.Num(),
            Result.Output.DeclaredDependencies.Num(),
            Result.Output.ObservedDependencies.Num());
        }
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::DomainExecution;
        ActiveWorkerV2ShardExecution.Reset();
        return EWorkerV2AdvanceResult::Failed;
      }
      if (!ApplyWorkerV2MergedOutput(Output))
      {
        ActiveWorkerV2ShardExecution.Reset();
        return EWorkerV2AdvanceResult::Failed;
      }
      ++WorkerV2ShardMergeCount;
      ActiveWorkerV2ShardExecution.Reset();
    }
    else
    {
      TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
      if (ResourceStore.HasBuildingRevision()
        && !ResourceStore.CommitBuildingAtEpoch(
          WorkerEpoch, ResourceEvents))
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::ResourceValidation;
        return EWorkerV2AdvanceResult::Failed;
      }

      TArray<FCrowdWorkerWakeup> DueWakeups;
      TimeWheel.DrainDue(
        AbsoluteSimulationTick, DueWakeups);
      for (const FCrowdWorkerWakeup& Wakeup : DueWakeups)
      {
        FCrowdWorkerWorkItem Work;
        Work.Key.Domain = Wakeup.Key.Domain;
        Work.Key.Kind = Wakeup.Key.Domain
            == ECrowdWorkerDomainId::Movement
          ? ECrowdWorkerWorkKind::Entity
          : Wakeup.Key.Domain
              == ECrowdWorkerDomainId::MovementPlanning
            ? ECrowdWorkerWorkKind::Resource
            : ECrowdWorkerWorkKind::Timer;
        Work.Key.PrimaryEntity = Wakeup.Key.EntityRef;
        Work.Key.ScopeKey = Work.Key.Kind
            == ECrowdWorkerWorkKind::Entity
          ? 0
          : Wakeup.Key.WakeupId;
        Work.Priority = Wakeup.Priority;
        Work.ReasonMask = Wakeup.ReasonMask;
        const ECrowdWorkerQueueResult Result =
          WorkRing.EnqueueCurrent(MoveTemp(Work));
        if (Result != ECrowdWorkerQueueResult::Added
          && Result != ECrowdWorkerQueueResult::MergedDuplicate)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::WorkQueue;
          return EWorkerV2AdvanceResult::Failed;
        }
      }
    }

    if (WorkerV2PendingStageWork.IsEmpty())
    {
      if (WorkRing.IsCurrentEmpty())
      {
        return CompleteWorkerV2SyntheticEpoch()
          ? EWorkerV2AdvanceResult::Completed
          : EWorkerV2AdvanceResult::Failed;
      }
      if (WorkerV2EpochPropagationRound
        >= Config.WorkerV2.MaxPropagationRoundsPerEpoch)
      {
        return CompleteWorkerV2SyntheticEpoch()
          ? EWorkerV2AdvanceResult::Completed
          : EWorkerV2AdvanceResult::Failed;
      }
      FCrowdWorkerWorkItem Work;
      while (WorkRing.PopCurrent(Work))
        WorkerV2PendingStageWork.Add(MoveTemp(Work));
      ++PropagationRoundCount;
      ++WorkerV2EpochPropagationRound;
    }

    if (!DomainRegistry || !DomainRegistry->IsFrozen())
    {
      return CompleteWorkerV2SyntheticEpoch()
        ? EWorkerV2AdvanceResult::Completed
        : EWorkerV2AdvanceResult::Failed;
    }

    ECrowdWorkerDomainId StageDomain =
      ECrowdWorkerDomainId::Count;
    uint8 StageExecutionRank = MAX_uint8;
    for (const FCrowdWorkerWorkItem& PendingWork :
      WorkerV2PendingStageWork)
    {
      const uint8 CandidateRank =
        CrowdWorkerRuntimeV2DomainExecutionRank(
          PendingWork.Key.Domain);
      if (CandidateRank < StageExecutionRank)
      {
        StageDomain = PendingWork.Key.Domain;
        StageExecutionRank = CandidateRank;
      }
    }
    if (StageDomain == ECrowdWorkerDomainId::MovementPlanning)
    {
      // A MovementControl revision replans the complete closed entity set.
      // Any TimeWheel Movement already due in this epoch is superseded; the
      // planning output emits exactly one replacement Movement per entity.
      // Keeping both would integrate the same simulation tick twice.
      WorkerV2PendingStageWork.RemoveAllSwap(
        [](const FCrowdWorkerWorkItem& PendingWork)
        {
          return PendingWork.Key.Domain
            == ECrowdWorkerDomainId::Movement;
        },
        EAllowShrinking::No);
    }
    TArray<FCrowdWorkerWorkItem> StageWork;
    for (int32 Index = WorkerV2PendingStageWork.Num() - 1;
      Index >= 0; --Index)
    {
      if (WorkerV2PendingStageWork[Index].Key.Domain
        == StageDomain)
      {
        StageWork.Add(MoveTemp(
          WorkerV2PendingStageWork[Index]));
        WorkerV2PendingStageWork.RemoveAtSwap(
          Index, 1, EAllowShrinking::No);
      }
    }
    TArray<FCrowdWorkerDomainShard> Shards;
    if (!FCrowdWorkerDeterministicShardPlanner::Build(
      StageWork,
      Config.WorkerV2.ShardEntityCount,
      Shards))
    {
      LastWorkerV2Failure =
        ECrowdWorkerRuntimeV2Failure::DomainExecution;
      return EWorkerV2AdvanceResult::Failed;
    }

    const TSharedPtr<FWorkerV2ShardExecution,
      ESPMode::ThreadSafe> Execution =
      MakeShared<FWorkerV2ShardExecution,
        ESPMode::ThreadSafe>();
    Execution->Generation = Generation.Load();
    Execution->WorkerEpoch = WorkerEpoch;
    Execution->PropagationRound =
      WorkerV2EpochPropagationRound;
    Execution->Results.SetNum(Shards.Num());
    Execution->Tasks.Reserve(Shards.Num());

    FCrowdWorkerDomainContext Context;
    Context.Generation = Execution->Generation;
    Context.WorkerEpoch = WorkerEpoch;
    Context.AbsoluteSimulationTick =
      AbsoluteSimulationTick;
    Context.CorrectionRevision =
      LastAppliedAuthorityCorrectionSequence;
    for (const FCrowdWorkerWorkItem& Work : StageWork)
    {
      Context.CorrectionRevision = FMath::Max(
        Context.CorrectionRevision,
        Work.CorrectionRevision);
    }
    Context.LastAppliedInputSequence =
      LastAppliedInputSequence;
    Context.NextOrderedEventSequence =
      OrderedEventStore.GetLastAcceptedEventSequence() + 1;
    if (Context.NextOrderedEventSequence == 0)
    {
      LastWorkerV2Failure =
        ECrowdWorkerRuntimeV2Failure::OrderedEventSequence;
      return EWorkerV2AdvanceResult::Failed;
    }
    Context.ResourceRevisionHash =
      ResourceStore.CalculateCurrentStableHash();
    Context.FixedDeltaSeconds =
      Config.FixedSimulationQuantumSeconds;
    Context.SimulationTimeSeconds =
      SimulationTimeSeconds;
    Context.RuntimeMode = Config.WorkerV2.GetEffectiveMode();
    Context.EntityStates = &EntityStateStore;
    Context.Commands = &CommandStore;
    Context.Resources = &ResourceStore;
    Context.SpatialIndex = &SpatialIndex;
    for (int32 Index = 0; Index < Shards.Num(); ++Index)
    {
      FCrowdWorkerDomainShardResult& Result =
        Execution->Results[Index];
      Result.Domain = Shards[Index].Domain;
      Result.ShardOrdinal = Shards[Index].ShardOrdinal;
      TArray<FCrowdWorkerWorkItem> ShardWork =
        MoveTemp(Shards[Index].WorkItems);
      const TSharedPtr<FSharedState, ESPMode::ThreadSafe>
        CapturedState = AsShared();
      Execution->Tasks.Add(UE::Tasks::Launch(
        TEXT("CrowdWorkerV2DomainShard"),
        [CapturedState, Execution, Context, Index,
          ShardWork = MoveTemp(ShardWork)]() mutable
        {
          FCrowdWorkerDomainShardResult& ShardResult =
            Execution->Results[Index];
          ShardResult.bSucceeded =
            CapturedState->DomainRegistry->ExecuteEpoch(
              Context, ShardWork, ShardResult.Output);
        }));
    }
    WorkerV2ShardDispatchCount += Shards.Num();
    WorkerV2ShardInFlightHighWatermark = FMath::Max(
      WorkerV2ShardInFlightHighWatermark, Shards.Num());
    ActiveWorkerV2ShardExecution = Execution;
    const TSharedPtr<FSharedState, ESPMode::ThreadSafe>
      ContinuationState = AsShared();
    Execution->OwnerReleased =
      MakeShared<UE::Tasks::FTaskEvent, ESPMode::ThreadSafe>(
        TEXT("CrowdWorkerV2OwnerReleased"));
    Execution->OwnerReleased->AddPrerequisites(
      Execution->Tasks);
    Execution->Continuation = UE::Tasks::Launch(
      TEXT("CrowdWorkerV2OwnerContinuation"),
      [ContinuationState, Execution]
      {
        ContinuationState->OwnerPump();
      },
      UE::Tasks::Prerequisites(*Execution->OwnerReleased));
    return EWorkerV2AdvanceResult::Pending;
  }

  ECrowdAsyncSimulationRuntimeState GetState() const
  {
    return static_cast<ECrowdAsyncSimulationRuntimeState>(
      State.Load());
  }

  void SetState(const ECrowdAsyncSimulationRuntimeState NewState)
  {
    State.Store(static_cast<uint8>(NewState));
  }

  void FailWorkerV2(const TCHAR* Stage)
  {
    bWorkPending.Store(false);
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdWorkerRuntimeV2Failed stage=%s failure=%u generation=%llu epoch=%llu input=%llu propagation_round=%d current_work=%d pending_stage=%d active_shards=%d"),
      Stage,
      static_cast<uint32>(LastWorkerV2Failure),
      Generation.Load(),
      WorkerEpoch,
      LastAppliedInputSequence,
      WorkerV2EpochPropagationRound,
      WorkRing.GetStats().CurrentDepth,
      WorkerV2PendingStageWork.Num(),
      ActiveWorkerV2ShardExecution
        ? ActiveWorkerV2ShardExecution->Tasks.Num()
        : 0);
    // Publish the failure counters before exposing Failed. Readers use the
    // state transition as the release point for the diagnostic snapshot.
    PublishMirrorSnapshot();
    SetState(ECrowdAsyncSimulationRuntimeState::Failed);
    {
      FScopeLock Lock(&SnapshotMutex);
      PublishedMirror.bValid = false;
    }
  }

  void PublishMirrorSnapshot()
  {
    const uint64 ScanStartCycles = FPlatformTime::Cycles64();
    const uint64 CurrentGeneration = Generation.Load();
    const bool bProduction =
      Config.WorkerV2.GetEffectiveMode()
        == ECrowdWorkerRuntimeV2Mode::Production;
    const bool bCheckpointEpoch = WorkerEpoch <= 1
      || (Config.NetworkPublishIntervalEpochs > 0
        && WorkerEpoch % static_cast<uint64>(
          Config.NetworkPublishIntervalEpochs) == 0);
    const bool bCheckpointNotMaterialized =
      LastFullMirrorGeneration != CurrentGeneration
      || LastFullMirrorWorkerEpoch != WorkerEpoch;
    const bool bMaterializeFullMirror = !bProduction
      || ((bCheckpointEpoch || bRequiresResnapshot.Load())
        && bCheckpointNotMaterialized);
    FCrowdWorkerMirrorSnapshot Snapshot;
    if (bMaterializeFullMirror)
    {
      Snapshot.Generation = CurrentGeneration;
      Snapshot.WorkerEpoch = WorkerEpoch;
      Snapshot.LastAppliedInputSequence =
        LastAppliedInputSequence;
      Snapshot.SimulationTimeSeconds = SimulationTimeSeconds;
      Snapshot.TargetSimulationTimeSeconds =
        TargetSimulationTimeSeconds;
      Snapshot.EntityRefs = Mirror.EntityRefs;
      Snapshot.States = Mirror.States;
      Snapshot.LastStateInputSequences =
        Mirror.LastStateInputSequences;
      Snapshot.CorrectionRevisions =
        Mirror.CorrectionRevisions;
      Snapshot.ResourceIds.Reserve(Mirror.Resources.Num());
      Snapshot.ResourceRevisions.Reserve(Mirror.Resources.Num());
      Snapshot.ResourcePayloadHashes.Reserve(Mirror.Resources.Num());
      for (const FResourceRecord& Resource : Mirror.Resources)
      {
        Snapshot.ResourceIds.Add(Resource.ResourceId);
        Snapshot.ResourceRevisions.Add(Resource.Revision);
        Snapshot.ResourcePayloadHashes.Add(Resource.Payload.StableHash);
      }
      Snapshot.EntitySetHash = Mirror.CalculateEntitySetHash();
      Snapshot.ResourceHash = Mirror.CalculateResourceHash();
      Snapshot.StableHash = Mirror.CalculateStableHash();
      Snapshot.bValid =
        !bRequiresResnapshot.Load()
        && (GetState()
          == ECrowdAsyncSimulationRuntimeState::Running
          || GetState()
            == ECrowdAsyncSimulationRuntimeState::Starting);
      ++FullMirrorSerializationCount;
      LastFullMirrorGeneration = CurrentGeneration;
      LastFullMirrorWorkerEpoch = WorkerEpoch;
    }
    const double ScanCoverageMs =
      FPlatformTime::ToMilliseconds64(
        FPlatformTime::Cycles64() - ScanStartCycles);

    int32 InputQueueDepth = 0;
    {
      FScopeLock InputLock(&InputMutex);
      InputQueueDepth = InputQueue.Num();
    }
    FScopeLock Lock(&SnapshotMutex);
    if (bMaterializeFullMirror)
      PublishedMirror = MoveTemp(Snapshot);
    Metrics.Generation = Generation.Load();
    Metrics.WorkerEpoch = WorkerEpoch;
    Metrics.LastAcceptedInputSequence =
      LastAcceptedInputSequence.Load();
    Metrics.LastAppliedInputSequence =
      LastAppliedInputSequence;
    Metrics.QueuedInputSequenceWatermark =
      QueuedInputSequenceWatermark.Load();
    Metrics.OwnerPumpCount = OwnerPumpCount;
    Metrics.ResnapshotCount = ResnapshotCount;
    Metrics.RejectedInputCount = RejectedInputCount.Load();
    Metrics.SubmittedShadowWorkCount =
      SubmittedShadowWorkCount.Load();
    Metrics.CompletedShadowWorkCount =
      CompletedShadowWorkCount.Load();
    Metrics.SubmittedProductionWorkCount =
      SubmittedProductionWorkCount.Load();
    Metrics.CompletedProductionWorkCount =
      CompletedProductionWorkCount.Load();
    Metrics.ShadowHashMismatchCount =
      ShadowHashMismatchCount.Load();
    Metrics.FullMirrorSerializationCount =
      FullMirrorSerializationCount;
    Metrics.AuthorityDigestCount = AuthorityDigestCount;
    Metrics.AuthorityCorrectionCount = AuthorityCorrectionCount;
    Metrics.AuthorityCorrectionEntityCount =
      AuthorityCorrectionEntityCount;
    Metrics.AuthorityCorrectionScopeCount =
      AuthorityCorrectionScopeCount;
    Metrics.ConsecutivePredictionEpochsWithoutCorrection =
      ConsecutivePredictionEpochsWithoutCorrection;
    Metrics.MaxPredictionEpochsWithoutCorrection =
      MaxPredictionEpochsWithoutCorrection;
    Metrics.LastCorrectionBeforePositionErrorCm =
      LastCorrectionBeforePositionErrorCm;
    Metrics.LastCorrectionAfterPositionErrorCm =
      LastCorrectionAfterPositionErrorCm;
    Metrics.LastCorrectionBeforeVelocityErrorCmps =
      LastCorrectionBeforeVelocityErrorCmps;
    Metrics.LastCorrectionAfterVelocityErrorCmps =
      LastCorrectionAfterVelocityErrorCmps;
    Metrics.LastCorrectionBeforeYawErrorDegrees =
      LastCorrectionBeforeYawErrorDegrees;
    Metrics.LastCorrectionAfterYawErrorDegrees =
      LastCorrectionAfterYawErrorDegrees;
    Metrics.LastCorrectionBeforeCombatMismatchCount =
      LastCorrectionBeforeCombatMismatchCount;
    Metrics.LastCorrectionAfterCombatMismatchCount =
      LastCorrectionAfterCombatMismatchCount;
    Metrics.LastCorrectionEntityCount = LastCorrectionEntityCount;
    Metrics.LastCorrectionScopeCount = LastCorrectionScopeCount;
    Metrics.InputQueueDepth = InputQueueDepth;
    Metrics.InFlightShadowWorkCount =
      InFlightShadowWorkCount.Load();
    Metrics.MirrorEntityCount = Mirror.EntityRefs.Num();
    Metrics.SimulationTimeSeconds = SimulationTimeSeconds;
    Metrics.TargetSimulationTimeSeconds =
      TargetSimulationTimeSeconds;
    Metrics.OldestInputAgeMs = OldestInputAgeMs;
    Metrics.SimulationLagMs = FMath::Max(
      0.0, (TargetSimulationTimeSeconds
        - SimulationTimeSeconds) * 1000.0);
    Metrics.LastOwnerPumpMs = LastOwnerPumpMs;
    Metrics.MaxOwnerPumpMs = MaxOwnerPumpMs;
    Metrics.LastScanCoverageMs = ScanCoverageMs;
    Metrics.MaxScanCoverageMs = FMath::Max(
      Metrics.MaxScanCoverageMs, ScanCoverageMs);
    Metrics.LastInputFailure =
      static_cast<ECrowdAsyncSimulationInputFailure>(
        LastInputFailure.Load());
    Metrics.bRequiresResnapshot =
      bRequiresResnapshot.Load();
    const FCrowdWorkerWorkRingStats WorkStats =
      WorkRing.GetStats();
    Metrics.WorkerV2.WorkCurrentDepth =
      WorkStats.CurrentDepth;
    Metrics.WorkerV2.WorkNextDepth = WorkStats.NextDepth;
    Metrics.WorkerV2.WorkHighWatermark =
      WorkStats.HighWatermark;
    Metrics.WorkerV2.WorkPopBucketProbeCount =
      WorkStats.PopBucketProbeCount;
    Metrics.WorkerV2.WakeupDepth = TimeWheel.Num();
    Metrics.WorkerV2.WakeupHighWatermark =
      TimeWheel.GetHighWatermark();
    Metrics.WorkerV2.TimeWheelScannedBucketCount =
      TimeWheel.GetScannedBucketCount();
    Metrics.WorkerV2.DependencyEdgeCount =
      DependencyIndex.NumEdges();
    Metrics.WorkerV2.DependencyHighWatermark =
      DependencyIndex.GetHighWatermark();
    Metrics.WorkerV2.DirtyEntityCount =
      DirtyStateStore.NumEntities();
    Metrics.WorkerV2.DirtyHighWatermark =
      DirtyStateStore.GetHighWatermark();
    Metrics.WorkerV2.EntityStateCount =
      EntityStateStore.NumEntities();
    Metrics.WorkerV2.PendingCommandCount =
      CommandStore.Num();
    Metrics.WorkerV2.CommandHighWatermark =
      CommandStore.GetHighWatermark();
    Metrics.WorkerV2.SleepingEntityCount = TimeWheel.Num();
    Metrics.WorkerV2.SpatialEntityCount = SpatialIndex.Num();
    Metrics.WorkerV2.SpatialFullRebuildCount =
      SpatialIndex.GetFullRebuildCount();
    Metrics.WorkerV2.SpatialIncrementalUpdateCount =
      SpatialIndex.GetIncrementalUpdateCount();
    Metrics.WorkerV2.SpatialCellMigrationCount =
      SpatialIndex.GetCellMigrationCount();
    Metrics.WorkerV2.PropagationRoundCount =
      PropagationRoundCount;
    Metrics.WorkerV2.PropagationLimitHitCount =
      PropagationLimitHitCount;
    Metrics.WorkerV2.ShardDispatchCount =
      WorkerV2ShardDispatchCount;
    Metrics.WorkerV2.ShardCompletionCount =
      WorkerV2ShardCompletionCount;
    Metrics.WorkerV2.ShardMergeCount =
      WorkerV2ShardMergeCount;
    Metrics.WorkerV2.ShardInFlightCount =
      ActiveWorkerV2ShardExecution
        ? ActiveWorkerV2ShardExecution->Tasks.Num()
        : 0;
    Metrics.WorkerV2.ShardInFlightHighWatermark =
      WorkerV2ShardInFlightHighWatermark;
    Metrics.WorkerV2.WorkCapacityRejectCount =
      WorkStats.CapacityRejectCount;
    Metrics.WorkerV2.OrderedEventLossCount =
      OrderedEventLossCount;
    Metrics.WorkerV2.OrderedEventDepth =
      OrderedEventStore.Num();
    Metrics.WorkerV2.OrderedEventHighWatermark =
      OrderedEventStore.GetHighWatermark();
    Metrics.WorkerV2.LastOrderedEventSequence =
      OrderedEventStore.GetLastAcceptedEventSequence();
    Metrics.WorkerV2.ResourceRevisionHash =
      ResourceStore.CalculateCurrentStableHash();
    Metrics.NetworkState = NetworkStatePublisher.GetMetrics();
    Metrics.WorkerV2.EntityStateHash =
      EntityStateStore.CalculateStableHash();
    Metrics.WorkerV2.PublishedDirtyStateCount =
      WorkerV2PublishedDirtyStateCount;
    Metrics.WorkerV2.PublishedOrderedEventCount =
      WorkerV2PublishedOrderedEventCount;
    Metrics.WorkerV2.CoverageAuditFailureCount =
      CoverageAuditFailureCount;
    Metrics.WorkerV2.ShadowBaselineRebaseCount =
      ShadowBaselineRebaseCount;
    Metrics.WorkerV2.LastFailure = LastWorkerV2Failure;
  }

  bool BuildMirrorFromWorkerStores(
    const FCrowdWorkerEntityStateStore& States,
    const FCrowdWorkerResourceStore& Resources,
    const FCrowdWorkerCommandStore& Commands,
    FWorkerMirror& OutMirror) const
  {
    OutMirror = {};
    TArray<FCrowdStableEntityRef> EntityRefs;
    States.GetEntities(EntityRefs);
    for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
    {
      const FCrowdWorkerDirtyStateRecord* InputState =
        States.Find(EntityRef, ECrowdWorkerField::InputSnapshot);
      if (!InputState) return false;
      OutMirror.EntityRefs.Add(EntityRef);
      FCrowdWorkerPublishedState& Published =
        OutMirror.States.AddDefaulted_GetRef();
      Published.StateRevision = InputState->StateRevision;
      Published.Payload = InputState->Payload;
      OutMirror.LastStateInputSequences.Add(
        InputState->SourceInputSequence);
      uint64 CorrectionRevision = 0;
      for (uint8 FieldValue = 0;
        FieldValue < static_cast<uint8>(ECrowdWorkerField::Count);
        ++FieldValue)
      {
        if (const FCrowdWorkerDirtyStateRecord* FieldState =
          States.Find(
            EntityRef,
            static_cast<ECrowdWorkerField>(FieldValue)))
          CorrectionRevision = FMath::Max(
            CorrectionRevision,
            FieldState->CorrectionRevision);
      }
      OutMirror.CorrectionRevisions.Add(CorrectionRevision);
      OutMirror.LastLifecycleBySlot.Add(
        FWorkerMirror::MakeSlotKey(EntityRef),
        EntityRef.LifecycleSerial);
    }
    OutMirror.RebuildIndices();
    TArray<FCrowdWorkerResourceRecord> ResourceRecords;
    Resources.GetCurrentRecords(ResourceRecords);
    for (const FCrowdWorkerResourceRecord& Resource : ResourceRecords)
      OutMirror.Resources.Add({
        Resource.ResourceId,
        Resource.Revision,
        Resource.Payload});
    TArray<FCrowdWorkerCommandRecord> CommandRecords;
    Commands.GetRecords(CommandRecords);
    for (const FCrowdWorkerCommandRecord& Command : CommandRecords)
    {
      FCrowdWorkerCommandDelta& Pending =
        OutMirror.PendingCommands.AddDefaulted_GetRef();
      Pending.InputSequence = Command.InputSequence;
      Pending.EntityRef = Command.EntityRef;
      Pending.CommandId = Command.CommandId;
      Pending.EffectiveSimulationTimeSeconds =
        Command.EffectiveSimulationTimeSeconds;
      Pending.Payload = Command.Payload;
    }
    return true;
  }

  void LatchResnapshot(
    const ECrowdAsyncSimulationInputFailure Failure)
  {
    LastInputFailure.Store(static_cast<uint8>(Failure));
    bRequiresResnapshot.Store(true);
    bWorkPending.Store(false);
    ++RejectedInputCount;
    PublishMirrorSnapshot();
  }

  void OwnerPump()
  {
    if (bOwnerPumpExecuting.Exchange(true))
      return;
    struct FOwnerExecutionGuard
    {
      TAtomic<bool>& Flag;
      bool bArmed = true;

      void Release()
      {
        if (!bArmed) return;
        Flag.Store(false);
        bArmed = false;
      }

      ~FOwnerExecutionGuard()
      {
        Release();
      }
    } OwnerGuard{bOwnerPumpExecuting};

    const uint64 PumpStartCycles = FPlatformTime::Cycles64();
    ++OwnerPumpCount;
    if (GetState()
        != ECrowdAsyncSimulationRuntimeState::Running
      && GetState()
        != ECrowdAsyncSimulationRuntimeState::Starting)
    {
      bWorkPending.Store(false);
      return;
    }

    bool bCorrectionBarrierPending = false;
    bool bCorrectionReady = false;
    bool bCorrectionContractMismatch = false;
    {
      FScopeLock Lock(&InputMutex);
      bCorrectionBarrierPending =
        bAuthorityCorrectionBarrierPending;
      if (!AuthorityCorrectionQueue.IsEmpty())
      {
        const FCrowdWorkerAuthorityCorrectionBatch& Correction =
          AuthorityCorrectionQueue[0];
        bCorrectionReady =
          Correction.ApplySimulationTick
              == AuthorityCorrectionBarrierTick
          && Correction.ThroughInputSequence
              == AuthorityCorrectionBarrierInputSequence;
        bCorrectionContractMismatch = !bCorrectionReady;
      }
    }
    if (bCorrectionBarrierPending)
    {
      if (bCorrectionContractMismatch)
      {
        LatchResnapshot(
          ECrowdAsyncSimulationInputFailure::ApplyFailure);
        return;
      }
      if (!bCorrectionReady)
      {
        bWorkPending.Store(false);
        return;
      }
      if (ActiveWorkerV2ShardExecution.IsValid()
        || WorkerV2EpochPropagationRound > 0
        || !WorkerV2PendingStageWork.IsEmpty()
        || !ApplyAuthorityCorrectionsAtBarrier())
      {
        LatchResnapshot(
          ECrowdAsyncSimulationInputFailure::ApplyFailure);
        return;
      }
      {
        FScopeLock Lock(&InputMutex);
        bAuthorityCorrectionBarrierPending = false;
        AuthorityCorrectionBarrierTick = 0;
        AuthorityCorrectionBarrierInputSequence = 0;
      }
    }

    // An epoch owns a frozen entity/resource view until every stage has
    // reached its barrier. Inputs accepted while a shard was running remain
    // in the bounded admission queue and may only mutate the Worker stores
    // after this epoch completes.
    if (IsWorkerV2Enabled()
      && (ActiveWorkerV2ShardExecution.IsValid()
        || WorkerV2EpochPropagationRound > 0
        || !WorkerV2PendingStageWork.IsEmpty()))
    {
      const EWorkerV2AdvanceResult ResumeResult =
        AdvanceWorkerV2SyntheticEpoch();
      if (ResumeResult == EWorkerV2AdvanceResult::Failed)
      {
        FailWorkerV2(TEXT("resume_before_input"));
        return;
      }
      if (ResumeResult == EWorkerV2AdvanceResult::Pending)
      {
        LastOwnerPumpMs =
          FPlatformTime::ToMilliseconds64(
            FPlatformTime::Cycles64() - PumpStartCycles);
        MaxOwnerPumpMs = FMath::Max(
          MaxOwnerPumpMs, LastOwnerPumpMs);
        PublishMirrorSnapshot();
        OwnerGuard.Release();
        if (ActiveWorkerV2ShardExecution
          && ActiveWorkerV2ShardExecution->OwnerReleased)
          ActiveWorkerV2ShardExecution->OwnerReleased->Trigger();
        return;
      }
      if (!FlushWorkerV2DomainResultsAtBarrier())
      {
        FailWorkerV2(TEXT("publish_epoch_barrier"));
        return;
      }
    }

    if (!ApplyAuthorityCorrectionsAtBarrier())
    {
      LatchResnapshot(
        ECrowdAsyncSimulationInputFailure::ApplyFailure);
      return;
    }

    TArray<FPendingInput> Pending;
    {
      FScopeLock Lock(&InputMutex);
      const int32 InputCount = FMath::Min(
        Config.MaxInputBatchesPerPump, InputQueue.Num());
      if (InputCount > 0)
      {
        Pending.Append(InputQueue.GetData(), InputCount);
        InputQueue.RemoveAt(
          0, InputCount, EAllowShrinking::No);
      }
    }
    OldestInputAgeMs = 0.0;
    if (!Pending.IsEmpty())
    {
      const uint64 NowCycles = FPlatformTime::Cycles64();
      uint64 OldestCycles = NowCycles;
      for (const FPendingInput& Input : Pending)
        OldestCycles = FMath::Min(
          OldestCycles, Input.EnqueuedCycles);
      OldestInputAgeMs =
        FPlatformTime::ToMilliseconds64(
          NowCycles - OldestCycles);
    }

    FWorkerMirror CandidateMirror = Mirror;
    FCrowdWorkerInputSequenceGate CandidateGate = SequenceGate;
    double CandidateTarget = TargetSimulationTimeSeconds;
    uint64 CandidateLastSequence = LastAppliedInputSequence;
    bool bAppliedResnapshot = false;
    bool bAppliedShadowStateBaseline = false;
    bool bAcceptedForPublish = false;
    TArray<FCrowdWorkerIntentBatch> WorkerV2AppliedBatches;
    for (const FPendingInput& PendingInput : Pending)
    {
      if (PendingInput.bResnapshot)
      {
        CandidateMirror = {};
        CandidateGate = {};
        if (!CandidateGate.ResetForResnapshot(
            Generation.Load(),
            PendingInput.Batch.GetRecordCount() > 0
              ? PendingInput.Batch.FirstInputSequence
              : 1))
        {
          LatchResnapshot(
            ECrowdAsyncSimulationInputFailure::InvalidPayload);
          return;
        }
      }
      const ECrowdWorkerInputAcceptResult AcceptResult =
        CandidateGate.Accept(PendingInput.Batch, Config.ContractLimits);
      if (AcceptResult != ECrowdWorkerInputAcceptResult::Accepted
        && AcceptResult
          != ECrowdWorkerInputAcceptResult::AcceptedEmpty
        && AcceptResult
          != ECrowdWorkerInputAcceptResult::AcceptedDuplicate)
      {
        LatchResnapshot(
          ToRuntimeFailure(CandidateGate.GetLastFailure()));
        return;
      }
      if (AcceptResult
        != ECrowdWorkerInputAcceptResult::AcceptedDuplicate)
      {
        if (!ApplyInputBatch(
            PendingInput.Batch,
            Config.MaxPendingCommands,
            CandidateMirror))
        {
          LatchResnapshot(
            ECrowdAsyncSimulationInputFailure::ApplyFailure);
          return;
        }
        // Clock.SimulationTick is the replicated simulation contract.  The
        // legacy wall-time hint is retained for compatibility at the input
        // boundary, but must never decide how far a Worker replica advances:
        // a client may receive the same intent at a different local frame.
        const double CanonicalTargetTimeSeconds =
          static_cast<double>(
            PendingInput.Batch.Clock.SimulationTick)
          * Config.FixedSimulationQuantumSeconds;
        CandidateTarget = FMath::Max(
          CandidateTarget,
          CanonicalTargetTimeSeconds);
        CandidateLastSequence = FMath::Max(
          CandidateLastSequence,
          PendingInput.Batch.LastInputSequence);
        bAcceptedForPublish = true;
        if (IsWorkerV2Enabled())
          WorkerV2AppliedBatches.Add(PendingInput.Batch);
        bAppliedShadowStateBaseline =
          bAppliedShadowStateBaseline
          || (Config.WorkerV2.GetEffectiveMode()
              == ECrowdWorkerRuntimeV2Mode::Shadow
            && (!PendingInput.Batch.Spawns.IsEmpty()
              || PendingInput.Batch.ExternalGameplayInputs.
                ContainsByPredicate([](
                  const FCrowdWorkerExternalGameplayInput& Input)
                {
                  return Input.InputTypeId == static_cast<uint16>(
                    ECrowdWorkerExternalGameplayInputType::
                      InputSnapshot);
                })));
        for (const FCrowdWorkerSpawnDelta& Delta :
          PendingInput.Batch.Spawns)
          PendingTouchedStateSequences.Add(
            Delta.EntityRef, Delta.InputSequence);
        for (const FCrowdWorkerExternalGameplayInput& Delta :
          PendingInput.Batch.ExternalGameplayInputs)
        {
          if (Delta.InputTypeId == static_cast<uint16>(
              ECrowdWorkerExternalGameplayInputType::InputSnapshot))
            PendingTouchedStateSequences.Add(
              Delta.EntityRef, Delta.InputSequence);
        }
      }
      bAppliedResnapshot |= PendingInput.bResnapshot;
    }

    Mirror = MoveTemp(CandidateMirror);
    SequenceGate = MoveTemp(CandidateGate);
    TargetSimulationTimeSeconds = CandidateTarget;
    LastAppliedInputSequence = CandidateLastSequence;
    if (!Pending.IsEmpty())
    {
      LastInputFailure.Store(static_cast<uint8>(
        ECrowdAsyncSimulationInputFailure::None));
    }
    if (bAppliedResnapshot)
    {
      SimulationTimeSeconds = FMath::Max(
        0.0,
        CandidateTarget
          - Config.FixedSimulationQuantumSeconds);
      AbsoluteSimulationTick = static_cast<uint64>(
        FMath::Max(
          0.0,
          FMath::FloorToDouble(
            (SimulationTimeSeconds
              + UE_DOUBLE_SMALL_NUMBER)
            / Config.FixedSimulationQuantumSeconds)));
      WorkerEpoch = 0;
      bRequiresResnapshot.Store(false);
      ++ResnapshotCount;
      SetState(ECrowdAsyncSimulationRuntimeState::Running);
      if (IsWorkerV2Enabled()
        && !ResetWorkerV2State())
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::InvalidConfiguration;
        FailWorkerV2(TEXT("reset_after_resnapshot"));
        return;
      }
    }
    else if (bAppliedShadowStateBaseline)
    {
      // Shadow state deltas are authoritative Legacy snapshots at the latest
      // target boundary. If several snapshots accumulated behind an in-flight
      // epoch, the entity store already contains only the newest snapshot;
      // replaying all skipped ticks from that newest state double-integrates.
      SimulationTimeSeconds = FMath::Max(
        SimulationTimeSeconds,
        CandidateTarget
          - Config.FixedSimulationQuantumSeconds);
      AbsoluteSimulationTick = static_cast<uint64>(
        FMath::Max(
          0.0,
          FMath::FloorToDouble(
            (SimulationTimeSeconds
              + UE_DOUBLE_SMALL_NUMBER)
            / Config.FixedSimulationQuantumSeconds)));
      ++ShadowBaselineRebaseCount;
    }
    if (IsWorkerV2Enabled())
    {
      for (const FCrowdWorkerIntentBatch& Batch :
        WorkerV2AppliedBatches)
      {
        if (!EnqueueWorkerV2Input(Batch))
        {
          FailWorkerV2(TEXT("enqueue_input"));
          return;
        }
      }
    }
    if (bAcceptedForPublish)
      bPublishPending.Store(true);

    int32 Steps = 0;
    while (Steps < Config.MaxSimulationStepsPerPump
      && (ActiveWorkerV2ShardExecution.IsValid()
        || SimulationTimeSeconds
          + Config.FixedSimulationQuantumSeconds
          <= TargetSimulationTimeSeconds
            + UE_DOUBLE_SMALL_NUMBER))
    {
      const bool bResumingWorkerV2Epoch =
        IsWorkerV2Enabled()
        && ActiveWorkerV2ShardExecution.IsValid();
      if (!bResumingWorkerV2Epoch)
      {
        SimulationTimeSeconds +=
          Config.FixedSimulationQuantumSeconds;
        ++AbsoluteSimulationTick;
        ++WorkerEpoch;
      }
      if (IsWorkerV2Enabled())
      {
        const EWorkerV2AdvanceResult AdvanceResult =
          AdvanceWorkerV2SyntheticEpoch();
        if (AdvanceResult == EWorkerV2AdvanceResult::Failed)
        {
          FailWorkerV2(TEXT("advance_epoch"));
          return;
        }
        if (AdvanceResult == EWorkerV2AdvanceResult::Pending)
        {
          LastOwnerPumpMs =
            FPlatformTime::ToMilliseconds64(
              FPlatformTime::Cycles64() - PumpStartCycles);
          MaxOwnerPumpMs = FMath::Max(
            MaxOwnerPumpMs, LastOwnerPumpMs);
          PublishMirrorSnapshot();
          OwnerGuard.Release();
          if (ActiveWorkerV2ShardExecution
            && ActiveWorkerV2ShardExecution->OwnerReleased)
            ActiveWorkerV2ShardExecution->OwnerReleased->Trigger();
          return;
        }
      }
      ++Steps;
    }
    Mirror.ConsumeCommandsThrough(SimulationTimeSeconds);

    bool bPublishedOrDeferred = false;
    if ((bAcceptedForPublish || bPublishPending.Load())
      && WorkerEpoch > 0)
    {
      for (const FCrowdWorkerDirtyStateRecord& Dirty :
        WorkerV2PendingPublishedDirtyStates)
      {
        FCrowdWorkerStatePatch Patch;
        Patch.EntityRef = Dirty.EntityRef;
        Patch.StateFieldId =
          1 + static_cast<uint16>(Dirty.Field);
        Patch.Generation = Dirty.Generation;
        Patch.WorkerEpoch = Dirty.WorkerEpoch;
        Patch.SourceInputSequence =
          Dirty.SourceInputSequence;
        Patch.DirtyMask =
          CrowdWorkerRuntimeV2FieldMask(Dirty.Field);
        Patch.State.StateRevision = Dirty.StateRevision;
        Patch.State.Payload = Dirty.Payload;
        Patch.RecalculateStableHash();
        const ECrowdWorkerAppendResult AppendResult =
          PublishedExchange.AppendStatePatch(Patch);
        if (AppendResult == ECrowdWorkerAppendResult::Violation
          || AppendResult
            == ECrowdWorkerAppendResult::RejectedNotInitialized)
        {
          UE_LOG(LogTemp, Error,
            TEXT("CrowdWorkerMirrorPatchRejected result=%u entity=%llu field=%u epoch=%llu input=%llu state_revision=%llu dirty=%llu"),
            static_cast<uint32>(AppendResult),
            Patch.EntityRef.StableEntityId,
            Patch.StateFieldId,
            Patch.WorkerEpoch,
            Patch.SourceInputSequence,
            Patch.State.StateRevision,
            Patch.DirtyMask);
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::Publication;
          FailWorkerV2(TEXT("append_dirty_state"));
          return;
        }
        ++WorkerV2PublishedDirtyStateCount;
      }
      WorkerV2PendingPublishedDirtyStates.Reset();
      for (const FCrowdWorkerGameplayEvent& Event :
        WorkerV2PendingPublishedEvents)
      {
        const ECrowdWorkerAppendResult AppendResult =
          PublishedExchange.AppendOrderedEvent(Event);
        if (AppendResult == ECrowdWorkerAppendResult::Violation
          || AppendResult
            == ECrowdWorkerAppendResult::RejectedNotInitialized)
        {
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::Publication;
          FailWorkerV2(TEXT("append_ordered_event"));
          return;
        }
        ++WorkerV2PublishedOrderedEventCount;
      }
      WorkerV2PendingPublishedEvents.Reset();
      for (const TPair<FCrowdStableEntityRef, uint64>& Touched :
        PendingTouchedStateSequences)
      {
        const int32 EntityIndex =
          Mirror.FindEntity(Touched.Key);
        if (EntityIndex == INDEX_NONE)
          continue;
        FCrowdWorkerStatePatch Patch;
        Patch.EntityRef = Touched.Key;
        Patch.Generation = Generation.Load();
        Patch.WorkerEpoch = WorkerEpoch;
        Patch.SourceInputSequence = Touched.Value;
        Patch.DirtyMask =
          CrowdWorkerResultFields::PresentationDiagnosticProxy;
        Patch.State = Mirror.States[EntityIndex];
        Patch.RecalculateStableHash();
        const ECrowdWorkerAppendResult AppendResult =
          PublishedExchange.AppendStatePatch(Patch);
        if (AppendResult == ECrowdWorkerAppendResult::Violation
          || AppendResult
            == ECrowdWorkerAppendResult::RejectedNotInitialized)
        {
          UE_LOG(LogTemp, Error,
            TEXT("CrowdWorkerInputMirrorPatchRejected result=%u entity=%llu epoch=%llu input=%llu state_revision=%llu dirty=%llu"),
            static_cast<uint32>(AppendResult),
            Patch.EntityRef.StableEntityId,
            Patch.WorkerEpoch,
            Patch.SourceInputSequence,
            Patch.State.StateRevision,
            Patch.DirtyMask);
          LastWorkerV2Failure =
            ECrowdWorkerRuntimeV2Failure::Publication;
          FailWorkerV2(TEXT("append_input_mirror"));
          return;
        }
      }
      PendingTouchedStateSequences.Reset();
      FCrowdWorkerPublishMetadata Metadata;
      Metadata.Generation = Generation.Load();
      Metadata.PublishSequence = NextPublishSequence;
      Metadata.MinWorkerEpoch = WorkerEpoch;
      Metadata.MaxWorkerEpoch = WorkerEpoch;
      Metadata.LastAppliedInputSequence =
        LastAppliedInputSequence;
      Metadata.PublishedSimulationTimeSeconds =
        SimulationTimeSeconds;
      const ECrowdWorkerPublishResult PublishResult =
        PublishedExchange.TryPublishBuildingBatch(Metadata);
      if (PublishResult == ECrowdWorkerPublishResult::Published)
      {
        LastPublishCycles.Store(FPlatformTime::Cycles64());
        ++NextPublishSequence;
        bPublishPending.Store(false);
        bPublishedOrDeferred = true;
      }
      else if (PublishResult
        == ECrowdWorkerPublishResult::DeferredPublishedOccupied)
      {
        bPublishPending.Store(true);
        bPublishedOrDeferred = true;
      }
      else
      {
        LastWorkerV2Failure =
          ECrowdWorkerRuntimeV2Failure::Publication;
        FailWorkerV2(TEXT("publish_exchange"));
        return;
      }
    }

    const bool bClockBehind =
      SimulationTimeSeconds
        + Config.FixedSimulationQuantumSeconds
        <= TargetSimulationTimeSeconds + UE_DOUBLE_SMALL_NUMBER;
    {
      FScopeLock Lock(&InputMutex);
      bWorkPending.Store(
        !bRequiresResnapshot.Load()
        && (!InputQueue.IsEmpty() || bClockBehind
          || (bPublishPending.Load() && !bPublishedOrDeferred)));
    }
    LastOwnerPumpMs =
      FPlatformTime::ToMilliseconds64(
        FPlatformTime::Cycles64() - PumpStartCycles);
    MaxOwnerPumpMs = FMath::Max(
      MaxOwnerPumpMs, LastOwnerPumpMs);
    PublishMirrorSnapshot();
  }

  void ResetOwnerState(const uint64 NewGeneration)
  {
    Mirror = {};
    AdmissionGate = {};
    SequenceGate = {};
    SimulationTimeSeconds = 0.0;
    TargetSimulationTimeSeconds = 0.0;
    AbsoluteSimulationTick = 0;
    WorkerEpoch = 0;
    LastAppliedInputSequence = 0;
    LastAcceptedInputSequence.Store(0);
    QueuedInputSequenceWatermark.Store(0);
    LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::None));
    NextPublishSequence = 1;
    ShadowTasks.Reset();
    LastShadowWorkSequences.Reset();
    PendingTouchedStateSequences.Reset();
    InFlightShadowWorkCount.Store(0);
    Generation.Store(NewGeneration);
    bRequiresResnapshot.Store(true);
    bWorkPending.Store(false);
    bPublishPending.Store(false);
    LastPublishCycles.Store(0);
    LastFullMirrorGeneration = 0;
    LastFullMirrorWorkerEpoch = MAX_uint64;
    OldestInputAgeMs = 0.0;
    LastOwnerPumpMs = 0.0;
    MaxOwnerPumpMs = 0.0;
    ResetWorkerV2State();
    PublishedExchange.ResetQuiescent(
      NewGeneration, Config.ContractLimits);
    PublishMirrorSnapshot();
  }
};

FCrowdAsyncSimulationRuntime::FCrowdAsyncSimulationRuntime()
  : PendingDomainRegistry(
    MakeUnique<FCrowdWorkerDomainRegistry>())
{
}

FCrowdAsyncSimulationRuntime::~FCrowdAsyncSimulationRuntime()
{
  if (SharedState
    && GetState() != ECrowdAsyncSimulationRuntimeState::Stopped)
    StopAndDrain(5.0);
}

bool FCrowdAsyncSimulationRuntime::Start(
  const FCrowdAsyncSimulationRuntimeConfig& Config,
  const uint64 InitialGeneration)
{
  if (!Config.IsValid() || InitialGeneration == 0
    || (SharedState
      && GetState()
        != ECrowdAsyncSimulationRuntimeState::Stopped))
    return false;
  SharedState = MakeShared<FSharedState, ESPMode::ThreadSafe>();
  SharedState->Config = Config;
  SharedState->Generation.Store(InitialGeneration);
  if (!SharedState->ResetWorkerV2State())
  {
    SharedState.Reset();
    return false;
  }
  if (PendingDomainRegistry
    && PendingDomainRegistry->Num() > 0)
  {
    if (!PendingDomainRegistry->Freeze())
    {
      SharedState.Reset();
      return false;
    }
    SharedState->DomainRegistry =
      MoveTemp(PendingDomainRegistry);
  }
  SharedState->bRequiresResnapshot.Store(true);
  SharedState->PublishedExchange.ResetQuiescent(
    InitialGeneration, Config.ContractLimits);
  SharedState->SetState(
    ECrowdAsyncSimulationRuntimeState::Starting);
  SharedState->PublishMirrorSnapshot();
  OwnerTask = {};
  bOwnerTaskActive = false;
  return true;
}

bool FCrowdAsyncSimulationRuntime::RegisterDomainExecutor(
  TUniquePtr<ICrowdWorkerDomainExecutor> Executor)
{
  if (SharedState
    && GetState() != ECrowdAsyncSimulationRuntimeState::Stopped)
    return false;
  if (!PendingDomainRegistry)
    PendingDomainRegistry =
      MakeUnique<FCrowdWorkerDomainRegistry>();
  return PendingDomainRegistry->Register(MoveTemp(Executor));
}

ECrowdAsyncSimulationRestoreResult
FCrowdAsyncSimulationRuntime::RestoreNetworkCheckpoint(
  const FCrowdWorkerNetworkCheckpoint& Checkpoint)
{
  if (!SharedState
    || GetState() != ECrowdAsyncSimulationRuntimeState::Starting)
    return ECrowdAsyncSimulationRestoreResult::RejectedState;
  if (Checkpoint.Header.Generation
    != SharedState->Generation.Load())
    return ECrowdAsyncSimulationRestoreResult::RejectedGeneration;
  if (!Checkpoint.IsValid(SharedState->Config.NetworkState)
    || Checkpoint.Header.FixedSimulationQuantumSeconds
      != SharedState->Config.FixedSimulationQuantumSeconds
    || Checkpoint.Header.WorkerEpoch == MAX_uint64
    || Checkpoint.Header.AbsoluteSimulationTick == MAX_uint64
    || Checkpoint.Header.LastAppliedInputSequence == MAX_uint64
    || Checkpoint.EventBaselineSequence == MAX_uint64)
    return ECrowdAsyncSimulationRestoreResult::RejectedCheckpoint;
  {
    FScopeLock InputLock(&SharedState->InputMutex);
    if (!SharedState->InputQueue.IsEmpty()
      || SharedState->bOwnerPumpExecuting.Load()
      || SharedState->ActiveWorkerV2ShardExecution.IsValid()
      || SharedState->bWorkPending.Load())
      return ECrowdAsyncSimulationRestoreResult::RejectedBusy;
    SharedState->RetainedNetworkIntents.Reset();
  }

  if (!SharedState->ResetWorkerV2State()
    || !SharedState->EntityStateStore.RestoreStateRecords(
      Checkpoint.StateRecords)
    || !SharedState->EntityStateStore.RestoreLifecycleWatermarks(
      Checkpoint.Continuation.LifecycleWatermarks)
    || !SharedState->ResourceStore.RestoreCurrentRecords(
      Checkpoint.ResourceRecords)
    || !SharedState->OrderedEventStore.Reset(
      SharedState->Config.WorkerV2.MaxOrderedEvents,
      SharedState->Config.ContractLimits.MaxPayloadBytes,
      Checkpoint.Header.Generation,
      Checkpoint.EventBaselineSequence + 1)
    || !SharedState->WorkRing.RestoreSnapshot(
      Checkpoint.Continuation.WorkRing)
    || !SharedState->TimeWheel.RestoreScheduled(
      Checkpoint.Continuation.Wakeups)
    || !SharedState->DependencyIndex.RestoreRecords(
      Checkpoint.Continuation.Dependencies)
    || !SharedState->CommandStore.RestoreRecords(
      Checkpoint.Continuation.Commands)
    || !SharedState->SpatialIndex.Rebuild(
      SharedState->EntityStateStore))
  {
    SharedState->SetState(
      ECrowdAsyncSimulationRuntimeState::Failed);
    return ECrowdAsyncSimulationRestoreResult::RestoreFailure;
  }

  SharedState->Mirror = {};
  TArray<FCrowdStableEntityRef> EntityRefs;
  SharedState->EntityStateStore.GetEntities(EntityRefs);
  SharedState->Mirror.EntityRefs = EntityRefs;
  SharedState->Mirror.States.Reserve(EntityRefs.Num());
  SharedState->Mirror.LastStateInputSequences.Reserve(
    EntityRefs.Num());
  SharedState->Mirror.CorrectionRevisions.Reserve(
    EntityRefs.Num());
  for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
  {
    const FCrowdWorkerDirtyStateRecord* InputState =
      SharedState->EntityStateStore.Find(
        EntityRef, ECrowdWorkerField::InputSnapshot);
    if (!InputState)
    {
      SharedState->SetState(
        ECrowdAsyncSimulationRuntimeState::Failed);
      return ECrowdAsyncSimulationRestoreResult::RestoreFailure;
    }
    FCrowdWorkerPublishedState& MirrorState =
      SharedState->Mirror.States.AddDefaulted_GetRef();
    MirrorState.StateRevision = InputState->StateRevision;
    MirrorState.Payload = InputState->Payload;
    SharedState->Mirror.LastStateInputSequences.Add(
      InputState->SourceInputSequence);
    uint64 CorrectionRevision = 0;
    for (const FCrowdWorkerDirtyStateRecord& State :
      Checkpoint.StateRecords)
    {
      if (State.EntityRef == EntityRef)
        CorrectionRevision = FMath::Max(
          CorrectionRevision, State.CorrectionRevision);
    }
    SharedState->Mirror.CorrectionRevisions.Add(
      CorrectionRevision);
  }
  SharedState->Mirror.RebuildIndices();
  for (const FCrowdWorkerResourceRecord& Resource :
    Checkpoint.ResourceRecords)
  {
    SharedState->Mirror.Resources.Add({
      Resource.ResourceId,
      Resource.Revision,
      Resource.Payload});
  }

  const uint64 NextInputSequence =
    Checkpoint.Header.LastAppliedInputSequence + 1;
  if (!SharedState->AdmissionGate.ResetForResnapshot(
      Checkpoint.Header.Generation, NextInputSequence)
    || !SharedState->SequenceGate.ResetForResnapshot(
      Checkpoint.Header.Generation, NextInputSequence))
  {
    SharedState->SetState(
      ECrowdAsyncSimulationRuntimeState::Failed);
    return ECrowdAsyncSimulationRestoreResult::RestoreFailure;
  }
  SharedState->LastAppliedInputSequence =
    Checkpoint.Header.LastAppliedInputSequence;
  SharedState->LastAcceptedInputSequence.Store(
    Checkpoint.Header.LastAppliedInputSequence);
  SharedState->QueuedInputSequenceWatermark.Store(
    Checkpoint.Header.LastAppliedInputSequence);
  SharedState->WorkerEpoch = Checkpoint.Header.WorkerEpoch;
  SharedState->AbsoluteSimulationTick =
    Checkpoint.Header.AbsoluteSimulationTick;
  SharedState->SimulationTimeSeconds =
    static_cast<double>(Checkpoint.Header.AbsoluteSimulationTick)
      * Checkpoint.Header.FixedSimulationQuantumSeconds;
  SharedState->TargetSimulationTimeSeconds =
    SharedState->SimulationTimeSeconds;
  SharedState->bRequiresResnapshot.Store(false);
  SharedState->bPublishPending.Store(false);
  SharedState->NextPublishSequence = 1;
  SharedState->PublishedExchange.ResetQuiescent(
    Checkpoint.Header.Generation,
    SharedState->Config.ContractLimits,
    0,
    Checkpoint.EventBaselineSequence,
    0);
  {
    FScopeLock NetworkLock(&SharedState->SnapshotMutex);
    if (!SharedState->NetworkStatePublisher.RestoreCheckpoint(
      Checkpoint))
    {
      SharedState->SetState(
        ECrowdAsyncSimulationRuntimeState::Failed);
      return ECrowdAsyncSimulationRestoreResult::RestoreFailure;
    }
  }
  SharedState->SetState(
    ECrowdAsyncSimulationRuntimeState::Running);
  SharedState->PublishMirrorSnapshot();
  return ECrowdAsyncSimulationRestoreResult::Restored;
}

bool FCrowdAsyncSimulationRuntime::QueueInput(
  const FCrowdWorkerIntentBatch& Batch,
  const bool bResnapshot,
  ECrowdAsyncSimulationSubmitResult& OutResult)
{
  if (!SharedState)
  {
    OutResult = ECrowdAsyncSimulationSubmitResult::RejectedState;
    return false;
  }
  const ECrowdAsyncSimulationRuntimeState State = GetState();
  if ((bResnapshot
      && State != ECrowdAsyncSimulationRuntimeState::Starting)
    || (!bResnapshot
      && State != ECrowdAsyncSimulationRuntimeState::Running))
  {
    SharedState->LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::State));
    ++SharedState->RejectedInputCount;
    OutResult = ECrowdAsyncSimulationSubmitResult::RejectedState;
    return false;
  }
  if (Batch.Generation != SharedState->Generation.Load())
  {
    SharedState->LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::Generation));
    ++SharedState->RejectedInputCount;
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RejectedGeneration;
    return false;
  }
  if (!Batch.IsValid(SharedState->Config.ContractLimits)
    || (bResnapshot && !IsResnapshotBatch(Batch)))
  {
    SharedState->LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::InvalidPayload));
    ++SharedState->RejectedInputCount;
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RejectedInvalidBatch;
    return false;
  }
  if (!bResnapshot && SharedState->bRequiresResnapshot.Load())
  {
    SharedState->LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::ResnapshotRequired));
    ++SharedState->RejectedInputCount;
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RequiresResnapshot;
    return false;
  }

  FScopeLock Lock(&SharedState->InputMutex);
  if (SharedState->InputQueue.Num()
    >= SharedState->Config.MaxQueuedInputBatches)
  {
    SharedState->LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::Capacity));
    ++SharedState->RejectedInputCount;
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RejectedCapacity;
    return false;
  }
  FCrowdWorkerInputSequenceGate CandidateAdmission =
    SharedState->AdmissionGate;
  if (bResnapshot && CandidateAdmission.GetGeneration() == 0)
  {
    if (!CandidateAdmission.ResetForResnapshot(
        Batch.Generation,
        Batch.GetRecordCount() > 0
          ? Batch.FirstInputSequence
          : 1))
    {
      SharedState->LastInputFailure.Store(static_cast<uint8>(
        ECrowdAsyncSimulationInputFailure::InvalidPayload));
      ++SharedState->RejectedInputCount;
      OutResult =
        ECrowdAsyncSimulationSubmitResult::RejectedInvalidBatch;
      return false;
    }
  }
  const ECrowdWorkerInputAcceptResult AdmissionResult =
    CandidateAdmission.Accept(
      Batch, SharedState->Config.ContractLimits);
  if (AdmissionResult
      != ECrowdWorkerInputAcceptResult::Accepted
    && AdmissionResult
      != ECrowdWorkerInputAcceptResult::AcceptedEmpty
    && AdmissionResult
      != ECrowdWorkerInputAcceptResult::AcceptedDuplicate)
  {
    const ECrowdAsyncSimulationInputFailure Failure =
      ToRuntimeFailure(CandidateAdmission.GetLastFailure());
    SharedState->LastInputFailure.Store(
      static_cast<uint8>(Failure));
    ++SharedState->RejectedInputCount;
    if (CandidateAdmission.RequiresResnapshot())
    {
      SharedState->AdmissionGate = MoveTemp(CandidateAdmission);
      SharedState->bRequiresResnapshot.Store(true);
      SharedState->bWorkPending.Store(false);
      OutResult =
        ECrowdAsyncSimulationSubmitResult::RequiresResnapshot;
    }
    else
    {
      OutResult =
        ECrowdAsyncSimulationSubmitResult::RejectedInvalidBatch;
    }
    return false;
  }

  SharedState->AdmissionGate = MoveTemp(CandidateAdmission);
  SharedState->LastInputFailure.Store(static_cast<uint8>(
    ECrowdAsyncSimulationInputFailure::None));
  if (Batch.GetRecordCount() > 0)
  {
    SharedState->LastAcceptedInputSequence.Store(
      Batch.LastInputSequence);
    SharedState->QueuedInputSequenceWatermark.Store(
      Batch.LastInputSequence);
  }
  if (AdmissionResult
    == ECrowdWorkerInputAcceptResult::AcceptedDuplicate)
  {
    OutResult = ECrowdAsyncSimulationSubmitResult::Accepted;
    return true;
  }
  SharedState->InputQueue.Add({
    Batch, bResnapshot, FPlatformTime::Cycles64()});
  if (!bResnapshot)
  {
    SharedState->RetainedNetworkIntents.Add(Batch);
    while (SharedState->RetainedNetworkIntents.Num()
      > SharedState->Config.MaxRetainedIntentBatches)
    {
      SharedState->RetainedNetworkIntents.RemoveAt(
        0, 1, EAllowShrinking::No);
    }
  }
  SharedState->bWorkPending.Store(true);
  OutResult = ECrowdAsyncSimulationSubmitResult::Accepted;
  return true;
}

ECrowdAsyncSimulationSubmitResult
FCrowdAsyncSimulationRuntime::SubmitResnapshot(
  const FCrowdWorkerIntentBatch& Batch)
{
  ECrowdAsyncSimulationSubmitResult Result;
  QueueInput(Batch, true, Result);
  return Result;
}

ECrowdAsyncSimulationSubmitResult
FCrowdAsyncSimulationRuntime::SubmitIntentBatch(
  const FCrowdWorkerIntentBatch& Batch)
{
  ECrowdAsyncSimulationSubmitResult Result;
  QueueInput(Batch, false, Result);
  return Result;
}

ECrowdAsyncShadowWorkSubmitResult
FCrowdAsyncSimulationRuntime::SubmitShadowWork(
  FCrowdAsyncShadowWorkSubmission&& Submission)
{
  check(IsInGameThread());
  if (!SharedState
    || GetState() != ECrowdAsyncSimulationRuntimeState::Running)
    return ECrowdAsyncShadowWorkSubmitResult::RejectedState;
  if (Submission.Generation != SharedState->Generation.Load())
    return ECrowdAsyncShadowWorkSubmitResult::RejectedGeneration;
  if (!Submission.IsValid())
    return ECrowdAsyncShadowWorkSubmitResult::RejectedInvalid;
  const uint64* LastSequence =
    SharedState->LastShadowWorkSequences.Find(Submission.KernelId);
  if (LastSequence && Submission.WorkSequence <= *LastSequence)
    return ECrowdAsyncShadowWorkSubmitResult::RejectedSequence;
  if (SharedState->ShadowTasks.Num()
    >= SharedState->Config.MaxInFlightShadowWorks)
    return ECrowdAsyncShadowWorkSubmitResult::RejectedCapacity;

  FSharedState::FShadowTaskRecord& Record =
    SharedState->ShadowTasks.AddDefaulted_GetRef();
  Record.Generation = Submission.Generation;
  Record.WorkSequence = Submission.WorkSequence;
  Record.SubmissionOrdinal =
    SharedState->NextShadowSubmissionOrdinal++;
  Record.KernelId = Submission.KernelId;
  Record.ExpectedStableHash = Submission.ExpectedStableHash;
  Record.bRequireExpectedStableHash =
    Submission.bRequireExpectedStableHash;
  Record.SubmittedCycles = FPlatformTime::Cycles64();
  Record.Execution =
    MakeShared<FSharedState::FShadowExecution,
      ESPMode::ThreadSafe>();
  const TSharedPtr<FSharedState::FShadowExecution,
    ESPMode::ThreadSafe> Execution = Record.Execution;
  TFunction<uint64()> Execute = MoveTemp(Submission.Execute);
  Record.Task = UE::Tasks::Launch(
    TEXT("CrowdAsyncSimulationShadowWork"),
    [Execution, Execute = MoveTemp(Execute)]() mutable
    {
      Execution->StartedCycles = FPlatformTime::Cycles64();
      Execution->ActualStableHash = Execute();
      Execution->CompletedCycles = FPlatformTime::Cycles64();
    });
  SharedState->LastShadowWorkSequences.Add(
    Record.KernelId, Record.WorkSequence);
  if (Submission.bRequireExpectedStableHash)
    ++SharedState->SubmittedShadowWorkCount;
  else
    ++SharedState->SubmittedProductionWorkCount;
  ++SharedState->InFlightShadowWorkCount;
  return ECrowdAsyncShadowWorkSubmitResult::Accepted;
}

int32 FCrowdAsyncSimulationRuntime::CollectCompletedShadowWork(
  TArray<FCrowdAsyncShadowWorkResult>& OutResults)
{
  check(IsInGameThread());
  OutResults.Reset();
  if (!SharedState) return 0;
  while (!SharedState->ShadowTasks.IsEmpty())
  {
    int32 Index = 0;
    for (int32 Candidate = 1;
      Candidate < SharedState->ShadowTasks.Num(); ++Candidate)
    {
      if (SharedState->ShadowTasks[Candidate].SubmissionOrdinal
        < SharedState->ShadowTasks[Index].SubmissionOrdinal)
        Index = Candidate;
    }
    const FSharedState::FShadowTaskRecord& Record =
      SharedState->ShadowTasks[Index];
    if (!Record.Task.IsValid() || !Record.Task.IsCompleted())
      break;
    FCrowdAsyncShadowWorkResult& Result =
      OutResults.AddDefaulted_GetRef();
    Result.Generation = Record.Generation;
    Result.WorkSequence = Record.WorkSequence;
    Result.SubmissionOrdinal = Record.SubmissionOrdinal;
    Result.KernelId = Record.KernelId;
    Result.ExpectedStableHash = Record.ExpectedStableHash;
    Result.bRequiredExpectedStableHash =
      Record.bRequireExpectedStableHash;
    Result.ActualStableHash =
      Record.Execution->ActualStableHash;
    Result.bSucceeded = Result.ActualStableHash > 0;
    Result.bHashMatch = Result.bSucceeded
      && (!Record.bRequireExpectedStableHash
        || Result.ActualStableHash == Result.ExpectedStableHash);
    if (Record.bRequireExpectedStableHash)
    {
      ++SharedState->CompletedShadowWorkCount;
      if (!Result.bHashMatch)
        ++SharedState->ShadowHashMismatchCount;
    }
    else
    {
      ++SharedState->CompletedProductionWorkCount;
    }
    {
      const double QueueMs =
        FPlatformTime::ToMilliseconds64(
          Record.Execution->StartedCycles
            - Record.SubmittedCycles);
      const double RunMs =
        FPlatformTime::ToMilliseconds64(
          Record.Execution->CompletedCycles
            - Record.Execution->StartedCycles);
      FScopeLock Lock(&SharedState->SnapshotMutex);
      SharedState->Metrics.LastTaskQueueMs = QueueMs;
      SharedState->Metrics.LastTaskRunMs = RunMs;
      SharedState->Metrics.MaxTaskCriticalMs = FMath::Max(
        SharedState->Metrics.MaxTaskCriticalMs,
        QueueMs + RunMs);
    }
    --SharedState->InFlightShadowWorkCount;
    SharedState->ShadowTasks.RemoveAtSwap(
      Index, 1, EAllowShrinking::No);
  }
  return OutResults.Num();
}

void FCrowdAsyncSimulationRuntime::LaunchOwnerPump()
{
  check(SharedState && !bOwnerTaskActive);
  const TSharedPtr<FSharedState, ESPMode::ThreadSafe> Captured =
    SharedState;
  OwnerTask = UE::Tasks::Launch(
    TEXT("CrowdAsyncSimulationOwnerPump"),
    [Captured]
    {
      Captured->OwnerPump();
    });
  bOwnerTaskActive = true;
}

ECrowdAsyncSimulationPollResult
FCrowdAsyncSimulationRuntime::Poll()
{
  if (!SharedState)
    return ECrowdAsyncSimulationPollResult::Stopped;
  if (bOwnerTaskActive)
  {
    if (!OwnerTask.IsValid() || !OwnerTask.IsCompleted())
      return ECrowdAsyncSimulationPollResult::Working;
    OwnerTask = {};
    bOwnerTaskActive = false;
  }

  const ECrowdAsyncSimulationRuntimeState State = GetState();
  if (State == ECrowdAsyncSimulationRuntimeState::Invalidating)
  {
    if (SharedState->ActiveWorkerV2ShardExecution
      && !SharedState->ActiveWorkerV2ShardExecution->IsSettled())
      return ECrowdAsyncSimulationPollResult::Working;
    SharedState->ActiveWorkerV2ShardExecution.Reset();
    for (const FSharedState::FShadowTaskRecord& Record :
      SharedState->ShadowTasks)
    {
      if (Record.Task.IsValid() && !Record.Task.IsCompleted())
        return ECrowdAsyncSimulationPollResult::Working;
    }
    TArray<FCrowdAsyncShadowWorkResult> Discarded;
    CollectCompletedShadowWork(Discarded);
    CompleteInvalidation();
    return ECrowdAsyncSimulationPollResult::StateChanged;
  }
  if (State == ECrowdAsyncSimulationRuntimeState::Draining)
  {
    if (SharedState->ActiveWorkerV2ShardExecution
      && !SharedState->ActiveWorkerV2ShardExecution->IsSettled())
      return ECrowdAsyncSimulationPollResult::Working;
    SharedState->ActiveWorkerV2ShardExecution.Reset();
    for (const FSharedState::FShadowTaskRecord& Record :
      SharedState->ShadowTasks)
    {
      if (Record.Task.IsValid() && !Record.Task.IsCompleted())
        return ECrowdAsyncSimulationPollResult::Working;
    }
    TArray<FCrowdAsyncShadowWorkResult> Discarded;
    CollectCompletedShadowWork(Discarded);
    CompleteStop();
    return ECrowdAsyncSimulationPollResult::Stopped;
  }
  if (State == ECrowdAsyncSimulationRuntimeState::Failed)
    return ECrowdAsyncSimulationPollResult::Failed;
  if (SharedState->bOwnerPumpExecuting.Load())
    return ECrowdAsyncSimulationPollResult::Working;
  if ((State == ECrowdAsyncSimulationRuntimeState::Starting
      || State == ECrowdAsyncSimulationRuntimeState::Running)
    && SharedState->ActiveWorkerV2ShardExecution)
  {
    if (!SharedState->ActiveWorkerV2ShardExecution->IsCompleted())
      return ECrowdAsyncSimulationPollResult::Working;
    if (SharedState->ActiveWorkerV2ShardExecution->
        Continuation.IsValid())
      return ECrowdAsyncSimulationPollResult::Working;
    LaunchOwnerPump();
    return ECrowdAsyncSimulationPollResult::Working;
  }
  if ((State == ECrowdAsyncSimulationRuntimeState::Starting
      || State == ECrowdAsyncSimulationRuntimeState::Running)
    && SharedState->bWorkPending.Load())
  {
    LaunchOwnerPump();
    return ECrowdAsyncSimulationPollResult::Working;
  }
  return ECrowdAsyncSimulationPollResult::Idle;
}

bool FCrowdAsyncSimulationRuntime::Invalidate(
  const uint64 NewGeneration)
{
  if (!SharedState
    || GetState() != ECrowdAsyncSimulationRuntimeState::Running
    || NewGeneration <= SharedState->Generation.Load())
    return false;
  {
    FScopeLock Lock(&SharedState->InputMutex);
    SharedState->InputQueue.Reset();
    SharedState->RetainedNetworkIntents.Reset();
    SharedState->AdmissionGate = {};
    SharedState->LastAcceptedInputSequence.Store(0);
    SharedState->QueuedInputSequenceWatermark.Store(0);
    SharedState->LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::None));
  }
  SharedState->PendingGeneration.Store(NewGeneration);
  SharedState->bWorkPending.Store(false);
  SharedState->SetState(
    ECrowdAsyncSimulationRuntimeState::Invalidating);
  return true;
}

void FCrowdAsyncSimulationRuntime::CompleteInvalidation()
{
  check(SharedState && !bOwnerTaskActive);
  const uint64 NewGeneration =
    SharedState->PendingGeneration.Load();
  if (NewGeneration <= SharedState->Generation.Load())
  {
    SharedState->SetState(
      ECrowdAsyncSimulationRuntimeState::Failed);
    return;
  }
  SharedState->ResetOwnerState(NewGeneration);
  SharedState->PendingGeneration.Store(0);
  SharedState->SetState(
    ECrowdAsyncSimulationRuntimeState::Starting);
}

bool FCrowdAsyncSimulationRuntime::BeginStop()
{
  if (!SharedState) return true;
  const ECrowdAsyncSimulationRuntimeState State = GetState();
  if (State == ECrowdAsyncSimulationRuntimeState::Stopped)
    return true;
  if (State == ECrowdAsyncSimulationRuntimeState::Draining)
    return true;
  {
    FScopeLock Lock(&SharedState->InputMutex);
    SharedState->InputQueue.Reset();
    SharedState->RetainedNetworkIntents.Reset();
    SharedState->AdmissionGate = {};
    SharedState->LastAcceptedInputSequence.Store(0);
    SharedState->QueuedInputSequenceWatermark.Store(0);
    SharedState->LastInputFailure.Store(static_cast<uint8>(
      ECrowdAsyncSimulationInputFailure::None));
  }
  SharedState->bWorkPending.Store(false);
  SharedState->SetState(
    ECrowdAsyncSimulationRuntimeState::Draining);
  return true;
}

void FCrowdAsyncSimulationRuntime::CompleteStop()
{
  check(SharedState && !bOwnerTaskActive);
  const uint64 Generation = SharedState->Generation.Load();
  SharedState->Mirror = {};
  SharedState->AdmissionGate = {};
  SharedState->SequenceGate = {};
  SharedState->LastAcceptedInputSequence.Store(0);
  SharedState->QueuedInputSequenceWatermark.Store(0);
  SharedState->LastInputFailure.Store(static_cast<uint8>(
    ECrowdAsyncSimulationInputFailure::None));
  SharedState->ShadowTasks.Reset();
  SharedState->LastShadowWorkSequences.Reset();
  SharedState->ActiveWorkerV2ShardExecution.Reset();
  SharedState->WorkerV2PendingStageWork.Reset();
  SharedState->WorkerV2PendingPublishedDirtyStates.Reset();
  SharedState->WorkerV2PendingPublishedEvents.Reset();
  SharedState->PendingTouchedStateSequences.Reset();
  SharedState->InFlightShadowWorkCount.Store(0);
  SharedState->PublishedExchange.ResetQuiescent(
    Generation, SharedState->Config.ContractLimits);
  SharedState->bRequiresResnapshot.Store(false);
  SharedState->bPublishPending.Store(false);
  SharedState->PublishMirrorSnapshot();
  SharedState->SetState(
    ECrowdAsyncSimulationRuntimeState::Stopped);
}

bool FCrowdAsyncSimulationRuntime::StopAndDrain(
  const double TimeoutSeconds)
{
  if (!FMath::IsFinite(TimeoutSeconds)
    || TimeoutSeconds <= 0.0
    || !BeginStop())
    return false;
  const double Deadline =
    FPlatformTime::Seconds() + TimeoutSeconds;
  while (FPlatformTime::Seconds() < Deadline)
  {
    const ECrowdAsyncSimulationPollResult Result = Poll();
    if (Result == ECrowdAsyncSimulationPollResult::Stopped)
      return true;
    if (Result == ECrowdAsyncSimulationPollResult::Failed)
      return false;
    FPlatformProcess::SleepNoStats(0.0f);
  }
  if (SharedState)
    SharedState->SetState(
      ECrowdAsyncSimulationRuntimeState::Failed);
  return false;
}

ECrowdAsyncSimulationRuntimeState
FCrowdAsyncSimulationRuntime::GetState() const
{
  return SharedState
    ? SharedState->GetState()
    : ECrowdAsyncSimulationRuntimeState::Stopped;
}

uint64 FCrowdAsyncSimulationRuntime::GetGeneration() const
{
  return SharedState ? SharedState->Generation.Load() : 0;
}

bool FCrowdAsyncSimulationRuntime::RequiresResnapshot() const
{
  return SharedState
    && SharedState->bRequiresResnapshot.Load();
}

bool FCrowdAsyncSimulationRuntime::ReadMirrorSnapshot(
  FCrowdWorkerMirrorSnapshot& OutSnapshot) const
{
  OutSnapshot = {};
  if (!SharedState) return false;
  FScopeLock Lock(&SharedState->SnapshotMutex);
  OutSnapshot = SharedState->PublishedMirror;
  return OutSnapshot.bValid;
}

ECrowdWorkerNetworkReadResult
FCrowdAsyncSimulationRuntime::ReadNetworkCheckpoint(
  const uint64 ExpectedGeneration,
  FCrowdWorkerNetworkCheckpoint& OutCheckpoint) const
{
  OutCheckpoint = {};
  if (!SharedState)
    return ECrowdWorkerNetworkReadResult::NotInitialized;
  FScopeLock Lock(&SharedState->SnapshotMutex);
  return SharedState->NetworkStatePublisher.ReadCheckpoint(
    ExpectedGeneration, OutCheckpoint);
}

ECrowdWorkerNetworkReadResult
FCrowdAsyncSimulationRuntime::ReadNetworkIntents(
  const uint64 ExpectedGeneration,
  const uint64 AfterInputSequence,
  TArray<FCrowdWorkerIntentBatch>& OutBatches) const
{
  OutBatches.Reset();
  if (!SharedState)
    return ECrowdWorkerNetworkReadResult::NotInitialized;
  if (ExpectedGeneration != SharedState->Generation.Load())
    return ECrowdWorkerNetworkReadResult::RejectedGeneration;
  FScopeLock Lock(&SharedState->InputMutex);
  if (AfterInputSequence
      > SharedState->LastAcceptedInputSequence.Load())
    return ECrowdWorkerNetworkReadResult::RejectedSequence;
  if (SharedState->RetainedNetworkIntents.IsEmpty())
    return ECrowdWorkerNetworkReadResult::NoData;
  const uint64 FirstRetained =
    SharedState->RetainedNetworkIntents[0].FirstInputSequence;
  if (AfterInputSequence + 1 < FirstRetained)
    return ECrowdWorkerNetworkReadResult::RequiresCheckpoint;
  for (const FCrowdWorkerIntentBatch& Batch :
    SharedState->RetainedNetworkIntents)
  {
    if (Batch.LastInputSequence > AfterInputSequence)
      OutBatches.Add(Batch);
  }
  return OutBatches.IsEmpty()
    ? ECrowdWorkerNetworkReadResult::NoData
    : ECrowdWorkerNetworkReadResult::Ready;
}

ECrowdWorkerNetworkReadResult
FCrowdAsyncSimulationRuntime::ReadAuthorityDigest(
  const uint64 ExpectedGeneration,
  FCrowdWorkerAuthorityDigestBatch& OutDigest) const
{
  OutDigest = {};
  if (!SharedState)
    return ECrowdWorkerNetworkReadResult::NotInitialized;
  if (ExpectedGeneration != SharedState->Generation.Load())
    return ECrowdWorkerNetworkReadResult::RejectedGeneration;
  FScopeLock Lock(&SharedState->SnapshotMutex);
  if (SharedState->AuthorityDigestHistory.IsEmpty())
    return ECrowdWorkerNetworkReadResult::NoData;
  OutDigest = SharedState->AuthorityDigestHistory.Last().Digest;
  return OutDigest.IsValid(SharedState->Config.NetworkState)
    ? ECrowdWorkerNetworkReadResult::Ready
    : ECrowdWorkerNetworkReadResult::Violation;
}

ECrowdWorkerNetworkReadResult
FCrowdAsyncSimulationRuntime::CompareAuthorityDigest(
  const FCrowdWorkerAuthorityDigestBatch& AuthorityDigest,
  TArray<FCrowdWorkerAuthorityScopeKey>& OutMismatchedScopes) const
{
  OutMismatchedScopes.Reset();
  if (!SharedState)
    return ECrowdWorkerNetworkReadResult::NotInitialized;
  if (AuthorityDigest.Generation != SharedState->Generation.Load())
    return ECrowdWorkerNetworkReadResult::RejectedGeneration;
  if (!AuthorityDigest.IsValid(SharedState->Config.NetworkState))
    return ECrowdWorkerNetworkReadResult::Violation;
  FScopeLock Lock(&SharedState->SnapshotMutex);
  // An owner may publish more than once without a new clock input (for
  // example, after a sparse local patch). Compare the newest state at this
  // ordered input waterline; the first matching snapshot can be stale.
  const FSharedState::FAuthorityDigestSnapshot* LocalSnapshot = nullptr;
  for (int32 Index = SharedState->AuthorityDigestHistory.Num() - 1;
    Index >= 0; --Index)
  {
    const FSharedState::FAuthorityDigestSnapshot& Candidate =
      SharedState->AuthorityDigestHistory[Index];
    if (Candidate.Digest.SimulationTick
          == AuthorityDigest.SimulationTick
      && Candidate.Digest.ThroughInputSequence
          == AuthorityDigest.ThroughInputSequence)
    {
      LocalSnapshot = &Candidate;
      break;
    }
  }
  if (!LocalSnapshot) return ECrowdWorkerNetworkReadResult::NoData;
  const FCrowdWorkerAuthorityDigestBatch& Local =
    LocalSnapshot->Digest;
  int32 LocalIndex = 0;
  int32 AuthorityIndex = 0;
  while (LocalIndex < Local.Entries.Num()
    || AuthorityIndex < AuthorityDigest.Entries.Num())
  {
    if (AuthorityIndex >= AuthorityDigest.Entries.Num()
      || (LocalIndex < Local.Entries.Num()
        && Local.Entries[LocalIndex].Scope
          < AuthorityDigest.Entries[AuthorityIndex].Scope))
    {
      OutMismatchedScopes.Add(Local.Entries[LocalIndex++].Scope);
      continue;
    }
    if (LocalIndex >= Local.Entries.Num()
      || AuthorityDigest.Entries[AuthorityIndex].Scope
        < Local.Entries[LocalIndex].Scope)
    {
      OutMismatchedScopes.Add(
        AuthorityDigest.Entries[AuthorityIndex++].Scope);
      continue;
    }
    const FCrowdWorkerAuthorityDigestEntry& LocalEntry =
      Local.Entries[LocalIndex++];
    const FCrowdWorkerAuthorityDigestEntry& AuthorityEntry =
      AuthorityDigest.Entries[AuthorityIndex++];
    if (LocalEntry.EntityCount != AuthorityEntry.EntityCount
      || LocalEntry.StableHash != AuthorityEntry.StableHash)
      OutMismatchedScopes.Add(AuthorityEntry.Scope);
  }
  return ECrowdWorkerNetworkReadResult::Ready;
}

ECrowdWorkerNetworkReadResult
FCrowdAsyncSimulationRuntime::BuildAuthorityCorrection(
  const uint64 ExpectedGeneration,
  const uint64 AuthorityDigestSequence,
  const uint64 CorrectionSequence,
  const TConstArrayView<FCrowdWorkerAuthorityScopeKey> Scopes,
  FCrowdWorkerAuthorityCorrectionBatch& OutCorrection) const
{
  OutCorrection = {};
  if (!SharedState)
    return ECrowdWorkerNetworkReadResult::NotInitialized;
  if (ExpectedGeneration != SharedState->Generation.Load())
    return ECrowdWorkerNetworkReadResult::RejectedGeneration;
  if (AuthorityDigestSequence == 0
    || CorrectionSequence == 0
    || Scopes.IsEmpty()
    || Scopes.Num() > SharedState->Config.NetworkState.MaxCorrectionScopes)
    return ECrowdWorkerNetworkReadResult::Violation;

  TArray<FCrowdWorkerDirtyStateRecord> States;
  FCrowdWorkerAuthorityDigestBatch Digest;
  {
    FScopeLock Lock(&SharedState->SnapshotMutex);
    const FSharedState::FAuthorityDigestSnapshot* Snapshot =
      SharedState->AuthorityDigestHistory.FindByPredicate(
        [AuthorityDigestSequence](
          const FSharedState::FAuthorityDigestSnapshot& Candidate)
        {
          return Candidate.Digest.DigestSequence
            == AuthorityDigestSequence;
        });
    if (!Snapshot)
      return ECrowdWorkerNetworkReadResult::NoData;
    Digest = Snapshot->Digest;
    States = Snapshot->States;
  }
  FCrowdWorkerEntityStateStore SnapshotStore;
  if (!SnapshotStore.Reset(
      SharedState->Config.WorkerV2.MaxDirtyEntities,
      SharedState->Config.ContractLimits.MaxPayloadBytes)
    || !SnapshotStore.RestoreStateRecords(States))
    return ECrowdWorkerNetworkReadResult::Violation;

  OutCorrection.Generation = ExpectedGeneration;
  OutCorrection.CorrectionSequence = CorrectionSequence;
  OutCorrection.ApplySimulationTick = Digest.SimulationTick;
  OutCorrection.ThroughInputSequence = Digest.ThroughInputSequence;
  OutCorrection.Scopes.Append(Scopes);
  OutCorrection.Scopes.Sort();
  for (int32 Index = OutCorrection.Scopes.Num() - 1;
    Index > 0; --Index)
  {
    if (OutCorrection.Scopes[Index]
        == OutCorrection.Scopes[Index - 1])
      OutCorrection.Scopes.RemoveAt(
        Index, 1, EAllowShrinking::No);
  }
  TSet<FCrowdStableEntityRef> MemberSet;
  for (FCrowdWorkerDirtyStateRecord Record : States)
  {
    const FCrowdWorkerAuthorityScopeKey Scope =
      ResolveAuthorityScope(Record, SnapshotStore);
    if (!OutCorrection.Scopes.Contains(Scope)) continue;
    Record.CorrectionRevision = FMath::Max(
      Record.CorrectionRevision + 1,
      OutCorrection.CorrectionSequence);
    OutCorrection.Records.Add(MoveTemp(Record));
    MemberSet.Add(OutCorrection.Records.Last().EntityRef);
  }
  for (const FCrowdStableEntityRef& Member : MemberSet)
    OutCorrection.AuthoritativeMembers.Add(Member);
  OutCorrection.AuthoritativeMembers.Sort();
  OutCorrection.Records.Sort([](
    const FCrowdWorkerDirtyStateRecord& A,
    const FCrowdWorkerDirtyStateRecord& B)
  {
    if (A.EntityRef != B.EntityRef) return A.EntityRef < B.EntityRef;
    return static_cast<uint8>(A.Field) < static_cast<uint8>(B.Field);
  });
  OutCorrection.RecalculateStableHash();
  return OutCorrection.IsValid(SharedState->Config.NetworkState)
    ? ECrowdWorkerNetworkReadResult::Ready
    : ECrowdWorkerNetworkReadResult::Violation;
}

ECrowdAsyncSimulationCorrectionResult
FCrowdAsyncSimulationRuntime::SubmitAuthorityCorrection(
  const FCrowdWorkerAuthorityCorrectionBatch& Correction)
{
  if (!SharedState
    || GetState() != ECrowdAsyncSimulationRuntimeState::Running)
    return ECrowdAsyncSimulationCorrectionResult::RejectedState;
  if (Correction.Generation != SharedState->Generation.Load())
    return ECrowdAsyncSimulationCorrectionResult::RejectedGeneration;
  if (!Correction.IsValid(SharedState->Config.NetworkState))
    return ECrowdAsyncSimulationCorrectionResult::RejectedContract;
  FScopeLock Lock(&SharedState->InputMutex);
  const uint64 ExpectedSequence =
    SharedState->AuthorityCorrectionQueue.IsEmpty()
      ? SharedState->LastAppliedAuthorityCorrectionSequence + 1
      : SharedState->AuthorityCorrectionQueue.Last()
          .CorrectionSequence + 1;
  if (Correction.CorrectionSequence < ExpectedSequence)
    return ECrowdAsyncSimulationCorrectionResult::Duplicate;
  if (Correction.CorrectionSequence != ExpectedSequence)
    return ECrowdAsyncSimulationCorrectionResult::RejectedSequence;
  if (SharedState->AuthorityCorrectionQueue.Num()
    >= SharedState->Config.NetworkState.MaxCorrectionScopes)
    return ECrowdAsyncSimulationCorrectionResult::RejectedCapacity;
  SharedState->AuthorityCorrectionQueue.Add(Correction);
  SharedState->bWorkPending.Store(true);
  return ECrowdAsyncSimulationCorrectionResult::Accepted;
}

bool FCrowdAsyncSimulationRuntime::BeginAuthorityCorrectionBarrier(
  const uint64 ExpectedGeneration,
  const uint64 ApplySimulationTick,
  const uint64 ThroughInputSequence)
{
  check(IsInGameThread());
  if (!SharedState
    || GetState() != ECrowdAsyncSimulationRuntimeState::Running
    || ExpectedGeneration != SharedState->Generation.Load()
    || ApplySimulationTick == 0
    || ThroughInputSequence == 0)
    return false;
  FScopeLock Lock(&SharedState->InputMutex);
  if (SharedState->bAuthorityCorrectionBarrierPending)
  {
    return SharedState->AuthorityCorrectionBarrierTick
        == ApplySimulationTick
      && SharedState->AuthorityCorrectionBarrierInputSequence
        == ThroughInputSequence;
  }
  SharedState->bAuthorityCorrectionBarrierPending = true;
  SharedState->AuthorityCorrectionBarrierTick = ApplySimulationTick;
  SharedState->AuthorityCorrectionBarrierInputSequence =
    ThroughInputSequence;
  SharedState->bWorkPending.Store(true);
  return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FCrowdAsyncSimulationRuntime::QueueDiagnosticMovementCorruption(
  const uint64 ExpectedGeneration,
  const FCrowdStableEntityRef& EntityRef,
  const FVector& PositionOffset,
  const FVector& VelocityOffset,
  const float YawOffsetDegrees)
{
  check(IsInGameThread());
  if (!SharedState
    || GetState() != ECrowdAsyncSimulationRuntimeState::Running
    || ExpectedGeneration != SharedState->Generation.Load()
    || !EntityRef.IsValid()
    || PositionOffset.IsNearlyZero()
      && VelocityOffset.IsNearlyZero()
      && FMath::IsNearlyZero(YawOffsetDegrees))
    return false;
  {
    FScopeLock Lock(&SharedState->InputMutex);
    if (SharedState->PendingDiagnosticMovementCorruption.IsSet())
      return false;
    SharedState->PendingDiagnosticMovementCorruption = {
      ExpectedGeneration,
      EntityRef,
      PositionOffset,
      VelocityOffset,
      YawOffsetDegrees};
    SharedState->bWorkPending.Store(true);
  }
  // The normal frame may already own the pump. Its next digest barrier will
  // consume the queued diagnostic mutation without creating a second owner.
  if (!bOwnerTaskActive)
    LaunchOwnerPump();
  return true;
}
#endif

FCrowdAsyncSimulationRuntimeMetrics
FCrowdAsyncSimulationRuntime::GetMetrics() const
{
  if (!SharedState) return {};
  FCrowdAsyncSimulationRuntimeMetrics Result;
  {
    FScopeLock Lock(&SharedState->SnapshotMutex);
    Result = SharedState->Metrics;
  }
  {
    FScopeLock Lock(&SharedState->InputMutex);
    Result.InputQueueDepth = SharedState->InputQueue.Num();
  }
  Result.LastAcceptedInputSequence =
    SharedState->LastAcceptedInputSequence.Load();
  Result.QueuedInputSequenceWatermark =
    SharedState->QueuedInputSequenceWatermark.Load();
  Result.RejectedInputCount =
    SharedState->RejectedInputCount.Load();
  Result.LastInputFailure =
    static_cast<ECrowdAsyncSimulationInputFailure>(
      SharedState->LastInputFailure.Load());
  Result.SubmittedShadowWorkCount =
    SharedState->SubmittedShadowWorkCount.Load();
  Result.CompletedShadowWorkCount =
    SharedState->CompletedShadowWorkCount.Load();
  Result.SubmittedProductionWorkCount =
    SharedState->SubmittedProductionWorkCount.Load();
  Result.CompletedProductionWorkCount =
    SharedState->CompletedProductionWorkCount.Load();
  Result.ShadowHashMismatchCount =
    SharedState->ShadowHashMismatchCount.Load();
  Result.InFlightShadowWorkCount =
    SharedState->InFlightShadowWorkCount.Load();
  return Result;
}

ECrowdWorkerExchangeResult
FCrowdAsyncSimulationRuntime::TryExchangePublishedBatch(
  const uint64 ExpectedGeneration,
  const uint64 ConsumerFrameSequence,
  const FCrowdWorkerPublishedBatch*& OutBatch)
{
  if (!SharedState)
  {
    OutBatch = nullptr;
    return ECrowdWorkerExchangeResult::RejectedNotInitialized;
  }
  const ECrowdWorkerExchangeResult Result =
    SharedState->PublishedExchange.TryExchangePublishedBatch(
    ExpectedGeneration, ConsumerFrameSequence, OutBatch);
  if (Result == ECrowdWorkerExchangeResult::Exchanged
    && OutBatch)
  {
    const uint64 PublishCycles =
      SharedState->LastPublishCycles.Load();
    const double PublishToConsumeMs =
      PublishCycles > 0
        ? FPlatformTime::ToMilliseconds64(
          FPlatformTime::Cycles64() - PublishCycles)
        : 0.0;
    {
      FScopeLock Lock(&SharedState->SnapshotMutex);
      SharedState->Metrics.LastPublishToConsumeMs =
        PublishToConsumeMs;
      SharedState->Metrics.MaxPublishToConsumeMs = FMath::Max(
        SharedState->Metrics.MaxPublishToConsumeMs,
        PublishToConsumeMs);
      SharedState->Metrics.LastPublishedPatchCount =
        OutBatch->StatePatches.Num();
      SharedState->Metrics.LastPublishedEventCount =
        OutBatch->OrderedEvents.Num();
    }
    if (SharedState->bPublishPending.Load())
      SharedState->bWorkPending.Store(true);
  }
  return Result;
}
