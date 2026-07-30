#include "MassCrowdAsyncSimulationRuntime.h"

#include "HAL/PlatformProcess.h"
#include "MassCrowdWorkerResultApply.h"
#include "Misc/ScopeLock.h"

namespace CrowdAsyncSimulationPrivate
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  void FoldByte(uint64& Hash, const uint8 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime64;
  }

  template<typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      FoldByte(Hash, static_cast<uint8>(Value >> (Byte * 8)));
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldUnsigned(Hash, Ref.ProviderId);
    FoldUnsigned(Hash, Ref.StableEntityId);
    FoldUnsigned(Hash, Ref.LifecycleSerial);
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

    bool ApplyState(const FCrowdWorkerStateDelta& Delta)
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

    bool ApplyCorrection(
      const FCrowdWorkerCorrectionDelta& Delta)
    {
      const int32 Index = FindEntity(Delta.EntityRef);
      if (Index == INDEX_NONE
        || Delta.CorrectionRevision <= CorrectionRevisions[Index])
        return false;
      ++States[Index].StateRevision;
      States[Index].Payload = Delta.FullState;
      LastStateInputSequences[Index] = Delta.InputSequence;
      CorrectionRevisions[Index] = Delta.CorrectionRevision;
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
      uint64 Hash = FnvOffset64;
      FoldUnsigned(Hash, uint32{1});
      for (const FCrowdStableEntityRef& Ref : EntityRefs)
        FoldRef(Hash, Ref);
      return Hash;
    }

    uint64 CalculateResourceHash() const
    {
      uint64 Hash = FnvOffset64;
      FoldUnsigned(Hash, uint32{1});
      for (const FResourceRecord& Resource : Resources)
      {
        FoldUnsigned(Hash, Resource.ResourceId);
        FoldUnsigned(Hash, Resource.Revision);
        FoldUnsigned(Hash, Resource.Payload.StableHash);
      }
      return Hash;
    }

    uint64 CalculateStableHash() const
    {
      uint64 Hash = CalculateEntitySetHash();
      for (int32 Index = 0; Index < EntityRefs.Num(); ++Index)
      {
        FoldUnsigned(Hash, States[Index].StateRevision);
        FoldUnsigned(Hash, States[Index].Payload.StableHash);
        FoldUnsigned(Hash, LastStateInputSequences[Index]);
        FoldUnsigned(Hash, CorrectionRevisions[Index]);
      }
      FoldUnsigned(Hash, CalculateResourceHash());
      return Hash;
    }
  };

  enum class EInputKind : uint8
  {
    Spawn = 1,
    Despawn,
    Command,
    State,
    Resource,
    Correction
  };

  struct FInputRef
  {
    uint64 Sequence = 0;
    EInputKind Kind = EInputKind::Spawn;
    int32 Index = INDEX_NONE;
  };

  void GatherInputRefs(
    const FCrowdWorkerInputBatch& Batch,
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
    for (int32 Index = 0; Index < Batch.StateDeltas.Num(); ++Index)
      OutRefs.Add({
        Batch.StateDeltas[Index].InputSequence,
        EInputKind::State, Index});
    for (int32 Index = 0;
      Index < Batch.ResourceDeltas.Num(); ++Index)
      OutRefs.Add({
        Batch.ResourceDeltas[Index].InputSequence,
        EInputKind::Resource, Index});
    for (int32 Index = 0; Index < Batch.Corrections.Num(); ++Index)
      OutRefs.Add({
        Batch.Corrections[Index].InputSequence,
        EInputKind::Correction, Index});
    OutRefs.Sort([](const FInputRef& A, const FInputRef& B)
    {
      return A.Sequence < B.Sequence;
    });
  }

  bool ApplyInputBatch(
    const FCrowdWorkerInputBatch& Batch,
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
          bApplied = Mirror.ApplyState(Batch.StateDeltas[Ref.Index]);
          break;
        case EInputKind::Resource:
          bApplied = Mirror.ApplyResource(
            Batch.ResourceDeltas[Ref.Index]);
          break;
        case EInputKind::Correction:
          bApplied = Mirror.ApplyCorrection(
            Batch.Corrections[Ref.Index]);
          break;
      }
      if (!bApplied) return false;
    }
    if (bEntitySetChanged) Mirror.SortEntities();
    return true;
  }

  bool IsResnapshotBatch(const FCrowdWorkerInputBatch& Batch)
  {
    return Batch.Despawns.IsEmpty()
      && Batch.Commands.IsEmpty()
      && Batch.StateDeltas.IsEmpty()
      && Batch.Corrections.IsEmpty();
  }
}

using namespace CrowdAsyncSimulationPrivate;

struct FCrowdAsyncSimulationRuntime::FSharedState
{
  struct FPendingInput
  {
    FCrowdWorkerInputBatch Batch;
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

  FCrowdAsyncSimulationRuntimeConfig Config;
  FCrowdWorkerInputSequenceGate SequenceGate;
  FCrowdWorkerPublishedExchange PublishedExchange;
  FWorkerMirror Mirror;
  TArray<FPendingInput> InputQueue;
  TArray<FShadowTaskRecord> ShadowTasks;
  TMap<uint32, uint64> LastShadowWorkSequences;
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
  double SimulationTimeSeconds = 0.0;
  double TargetSimulationTimeSeconds = 0.0;
  uint64 WorkerEpoch = 0;
  uint64 LastAppliedInputSequence = 0;
  uint64 OwnerPumpCount = 0;
  uint64 ResnapshotCount = 0;
  uint64 RejectedInputCount = 0;
  uint64 NextShadowSubmissionOrdinal = 1;
  uint64 NextPublishSequence = 1;
  double OldestInputAgeMs = 0.0;
  double LastOwnerPumpMs = 0.0;
  double MaxOwnerPumpMs = 0.0;

  ECrowdAsyncSimulationRuntimeState GetState() const
  {
    return static_cast<ECrowdAsyncSimulationRuntimeState>(
      State.Load());
  }

  void SetState(const ECrowdAsyncSimulationRuntimeState NewState)
  {
    State.Store(static_cast<uint8>(NewState));
  }

  void PublishMirrorSnapshot()
  {
    const uint64 ScanStartCycles = FPlatformTime::Cycles64();
    FCrowdWorkerMirrorSnapshot Snapshot;
    Snapshot.Generation = Generation.Load();
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
    const double ScanCoverageMs =
      FPlatformTime::ToMilliseconds64(
        FPlatformTime::Cycles64() - ScanStartCycles);

    int32 InputQueueDepth = 0;
    {
      FScopeLock InputLock(&InputMutex);
      InputQueueDepth = InputQueue.Num();
    }
    FScopeLock Lock(&SnapshotMutex);
    PublishedMirror = MoveTemp(Snapshot);
    Metrics.Generation = Generation.Load();
    Metrics.WorkerEpoch = WorkerEpoch;
    Metrics.LastAppliedInputSequence =
      LastAppliedInputSequence;
    Metrics.OwnerPumpCount = OwnerPumpCount;
    Metrics.ResnapshotCount = ResnapshotCount;
    Metrics.RejectedInputCount = RejectedInputCount;
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
    Metrics.bRequiresResnapshot =
      bRequiresResnapshot.Load();
  }

  void LatchResnapshot()
  {
    bRequiresResnapshot.Store(true);
    bWorkPending.Store(false);
    ++RejectedInputCount;
    PublishMirrorSnapshot();
  }

  void OwnerPump()
  {
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

    TArray<FPendingInput> Pending;
    {
      FScopeLock Lock(&InputMutex);
      const int32 BatchCount = FMath::Min(
        Config.MaxInputBatchesPerPump, InputQueue.Num());
      if (BatchCount > 0)
      {
        Pending.Append(InputQueue.GetData(), BatchCount);
        InputQueue.RemoveAt(
          0, BatchCount, EAllowShrinking::No);
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
    bool bAcceptedForPublish = false;
    TMap<FCrowdStableEntityRef, uint64> TouchedStateSequences;
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
          LatchResnapshot();
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
        LatchResnapshot();
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
          LatchResnapshot();
          return;
        }
        CandidateTarget = FMath::Max(
          CandidateTarget,
          PendingInput.Batch.TargetSimulationTimeSeconds);
        CandidateLastSequence = FMath::Max(
          CandidateLastSequence,
          PendingInput.Batch.LastInputSequence);
        bAcceptedForPublish = true;
        for (const FCrowdWorkerSpawnDelta& Delta :
          PendingInput.Batch.Spawns)
          TouchedStateSequences.Add(
            Delta.EntityRef, Delta.InputSequence);
        for (const FCrowdWorkerStateDelta& Delta :
          PendingInput.Batch.StateDeltas)
          TouchedStateSequences.Add(
            Delta.EntityRef, Delta.InputSequence);
        for (const FCrowdWorkerCorrectionDelta& Delta :
          PendingInput.Batch.Corrections)
          TouchedStateSequences.Add(
            Delta.EntityRef, Delta.InputSequence);
      }
      bAppliedResnapshot |= PendingInput.bResnapshot;
    }

    Mirror = MoveTemp(CandidateMirror);
    SequenceGate = MoveTemp(CandidateGate);
    TargetSimulationTimeSeconds = CandidateTarget;
    LastAppliedInputSequence = CandidateLastSequence;
    if (bAppliedResnapshot)
    {
      bRequiresResnapshot.Store(false);
      ++ResnapshotCount;
      SetState(ECrowdAsyncSimulationRuntimeState::Running);
    }

    int32 Steps = 0;
    while (Steps < Config.MaxSimulationStepsPerPump
      && SimulationTimeSeconds
        + Config.FixedSimulationQuantumSeconds
        <= TargetSimulationTimeSeconds + UE_DOUBLE_SMALL_NUMBER)
    {
      SimulationTimeSeconds +=
        Config.FixedSimulationQuantumSeconds;
      ++WorkerEpoch;
      ++Steps;
    }

    bool bPublishedOrDeferred = false;
    if ((bAcceptedForPublish || bPublishPending.Load())
      && WorkerEpoch > 0)
    {
      for (const TPair<FCrowdStableEntityRef, uint64>& Touched :
        TouchedStateSequences)
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
          SetState(ECrowdAsyncSimulationRuntimeState::Failed);
          bWorkPending.Store(false);
          return;
        }
      }
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
        SetState(ECrowdAsyncSimulationRuntimeState::Failed);
        bWorkPending.Store(false);
        return;
      }
    }

    bool bQueueNotEmpty = false;
    {
      FScopeLock Lock(&InputMutex);
      bQueueNotEmpty = !InputQueue.IsEmpty();
    }
    const bool bClockBehind =
      SimulationTimeSeconds
        + Config.FixedSimulationQuantumSeconds
        <= TargetSimulationTimeSeconds + UE_DOUBLE_SMALL_NUMBER;
    bWorkPending.Store(
      !bRequiresResnapshot.Load()
      && (bQueueNotEmpty || bClockBehind
        || (bPublishPending.Load() && !bPublishedOrDeferred)));
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
    SequenceGate = {};
    SimulationTimeSeconds = 0.0;
    TargetSimulationTimeSeconds = 0.0;
    WorkerEpoch = 0;
    LastAppliedInputSequence = 0;
    NextPublishSequence = 1;
    ShadowTasks.Reset();
    LastShadowWorkSequences.Reset();
    InFlightShadowWorkCount.Store(0);
    Generation.Store(NewGeneration);
    bRequiresResnapshot.Store(true);
    bWorkPending.Store(false);
    bPublishPending.Store(false);
    LastPublishCycles.Store(0);
    OldestInputAgeMs = 0.0;
    LastOwnerPumpMs = 0.0;
    MaxOwnerPumpMs = 0.0;
    PublishedExchange.ResetQuiescent(
      NewGeneration, Config.ContractLimits);
    PublishMirrorSnapshot();
  }
};

FCrowdAsyncSimulationRuntime::FCrowdAsyncSimulationRuntime() = default;

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

bool FCrowdAsyncSimulationRuntime::QueueInput(
  const FCrowdWorkerInputBatch& Batch,
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
    OutResult = ECrowdAsyncSimulationSubmitResult::RejectedState;
    return false;
  }
  if (Batch.Generation != SharedState->Generation.Load())
  {
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RejectedGeneration;
    return false;
  }
  if (!Batch.IsValid(SharedState->Config.ContractLimits)
    || (bResnapshot && !IsResnapshotBatch(Batch)))
  {
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RejectedInvalidBatch;
    return false;
  }
  if (!bResnapshot && SharedState->bRequiresResnapshot.Load())
  {
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RequiresResnapshot;
    return false;
  }

  FScopeLock Lock(&SharedState->InputMutex);
  if (SharedState->InputQueue.Num()
    >= SharedState->Config.MaxQueuedInputBatches)
  {
    OutResult =
      ECrowdAsyncSimulationSubmitResult::RejectedCapacity;
    return false;
  }
  SharedState->InputQueue.Add({
    Batch, bResnapshot, FPlatformTime::Cycles64()});
  SharedState->bWorkPending.Store(true);
  OutResult = ECrowdAsyncSimulationSubmitResult::Accepted;
  return true;
}

ECrowdAsyncSimulationSubmitResult
FCrowdAsyncSimulationRuntime::SubmitResnapshot(
  const FCrowdWorkerInputBatch& Batch)
{
  ECrowdAsyncSimulationSubmitResult Result;
  QueueInput(Batch, true, Result);
  return Result;
}

ECrowdAsyncSimulationSubmitResult
FCrowdAsyncSimulationRuntime::SubmitInput(
  const FCrowdWorkerInputBatch& Batch)
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
  SharedState->SequenceGate = {};
  SharedState->ShadowTasks.Reset();
  SharedState->LastShadowWorkSequences.Reset();
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

FCrowdAsyncSimulationRuntimeMetrics
FCrowdAsyncSimulationRuntime::GetMetrics() const
{
  if (!SharedState) return {};
  FScopeLock Lock(&SharedState->SnapshotMutex);
  FCrowdAsyncSimulationRuntimeMetrics Result =
    SharedState->Metrics;
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
