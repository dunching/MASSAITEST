#include "MassCrowdWorkerShadowSync.h"

namespace CrowdWorkerShadowSyncPrivate
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  void AppendByte(TArray<uint8>& Bytes, const uint8 Value)
  {
    Bytes.Add(Value);
  }

  template<typename T>
  void AppendUnsigned(TArray<uint8>& Bytes, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      AppendByte(Bytes, static_cast<uint8>(Value >> (Byte * 8)));
  }

  void AppendInt32(TArray<uint8>& Bytes, const int32 Value)
  {
    AppendUnsigned(Bytes, static_cast<uint32>(Value));
  }

  void AppendInt64(TArray<uint8>& Bytes, const int64 Value)
  {
    AppendUnsigned(Bytes, static_cast<uint64>(Value));
  }

  void AppendQuantizedFloat(
    TArray<uint8>& Bytes,
    const double Value)
  {
    AppendInt32(
      Bytes,
      FMath::RoundToInt(static_cast<float>(Value) * 100.0f));
  }

  void AppendRef(
    TArray<uint8>& Bytes,
    const FCrowdStableEntityRef& Ref)
  {
    AppendUnsigned(Bytes, Ref.ProviderId);
    AppendUnsigned(Bytes, Ref.StableEntityId);
    AppendUnsigned(Bytes, Ref.LifecycleSerial);
  }

  template<typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= FnvPrime64;
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldUnsigned(Hash, Ref.ProviderId);
    FoldUnsigned(Hash, Ref.StableEntityId);
    FoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  uint64 CalculateEntitySetHash(
    const TConstArrayView<FCrowdStableEntityRef> Refs)
  {
    uint64 Hash = FnvOffset64;
    FoldUnsigned(Hash, uint32{1});
    for (const FCrowdStableEntityRef& Ref : Refs)
      FoldRef(Hash, Ref);
    return Hash;
  }

  bool FindSnapshotResource(
    const FCrowdWorkerMirrorSnapshot& Snapshot,
    uint64& OutPayloadHash)
  {
    if (Snapshot.ResourceIds.Num()
        != Snapshot.ResourceRevisions.Num()
      || Snapshot.ResourceIds.Num()
        != Snapshot.ResourcePayloadHashes.Num())
      return false;
    for (int32 Index = 0;
      Index < Snapshot.ResourceIds.Num(); ++Index)
    {
      if (Snapshot.ResourceIds[Index]
          != FCrowdWorkerBoundaryStateCodec::SnapshotResourceId)
        continue;
      OutPayloadHash = Snapshot.ResourcePayloadHashes[Index];
      return Snapshot.ResourceRevisions[Index] > 0;
    }
    return false;
  }
}

using namespace CrowdWorkerShadowSyncPrivate;

bool FCrowdWorkerBoundaryStateCodec::EncodeState(
  const FCrowdMassBoundaryAgentRecord& Record,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Record.AgentFacts.StableEntityRef.IsValid()
    || Record.Identity.GetStableEntityRef()
      != Record.AgentFacts.StableEntityRef
    || !Record.State.bInitialized)
    return false;

  OutPayload.SchemaId = StateSchemaId;
  OutPayload.SchemaVersion = StateSchemaVersion;
  TArray<uint8>& Bytes = OutPayload.Bytes;
  Bytes.Reserve(132);
  AppendRef(Bytes, Record.AgentFacts.StableEntityRef);
  AppendInt32(Bytes, Record.Identity.AgentId);
  AppendUnsigned(Bytes, Record.AgentFacts.FactionKey);
  AppendUnsigned(Bytes, Record.AgentFacts.CapabilitySet.Bits);
  AppendUnsigned(Bytes, Record.AgentFacts.DerivedBehaviorLabel);
  AppendRef(Bytes, Record.AgentFacts.BusinessTaskRef);
  AppendRef(Bytes, Record.AgentFacts.TargetRef);
  AppendUnsigned(Bytes, Record.AgentFacts.MovementProfileKey);
  AppendUnsigned(Bytes, Record.AgentFacts.PresentationProfileKey);
  AppendUnsigned(Bytes, Record.AgentFacts.RuntimeState);
  AppendQuantizedFloat(Bytes, Record.State.Position.X);
  AppendQuantizedFloat(Bytes, Record.State.Position.Y);
  AppendQuantizedFloat(Bytes, Record.State.Position.Z);
  AppendQuantizedFloat(Bytes, Record.State.Velocity.X);
  AppendQuantizedFloat(Bytes, Record.State.Velocity.Y);
  AppendQuantizedFloat(Bytes, Record.State.Velocity.Z);
  AppendQuantizedFloat(Bytes, Record.State.YawDegrees);
  AppendInt32(Bytes, Record.State.PlanRevision);
  AppendQuantizedFloat(Bytes, Record.Properties.PhysicalRadiusCm);
  AppendQuantizedFloat(Bytes, Record.Properties.HardSafetyGapCm);
  AppendQuantizedFloat(Bytes, Record.Properties.SoftMarginCm);
  AppendQuantizedFloat(Bytes, Record.Properties.Mobility);
  AppendQuantizedFloat(Bytes, Record.Properties.MaximumSpeedCmps);
  AppendUnsigned(Bytes, Record.Properties.CapabilityProfileKey);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerBoundaryStateCodec::EncodeSnapshotResource(
  const FCrowdMassBoundarySnapshot& Snapshot,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Snapshot.bValid || Snapshot.FixedStepIndex < 0
    || Snapshot.PlanRevision < 0 || Snapshot.Agents.IsEmpty())
    return false;
  OutPayload.SchemaId = SnapshotResourceSchemaId;
  OutPayload.SchemaVersion = SnapshotResourceSchemaVersion;
  AppendInt32(OutPayload.Bytes, Snapshot.FixedStepIndex);
  AppendInt32(OutPayload.Bytes, Snapshot.PlanRevision);
  AppendUnsigned(OutPayload.Bytes, Snapshot.StableHash);
  AppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Snapshot.Agents.Num()));
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerBoundaryStateCodec::EncodeBehaviorCommand(
  const FCrowdBehaviorSourceCommand& Command,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Command.IsValid())
    return false;
  OutPayload.SchemaId = BehaviorCommandSchemaId;
  OutPayload.SchemaVersion = BehaviorCommandSchemaVersion;
  TArray<uint8>& Bytes = OutPayload.Bytes;
  Bytes.Reserve(MaxEncodedBehaviorCommandBytes);
  AppendInt64(Bytes, Command.EffectiveFixedStep);
  AppendRef(Bytes, Command.Handle.EntityRef);
  AppendUnsigned(Bytes, Command.Handle.ControllerId.Value);
  AppendUnsigned(Bytes, Command.Handle.SourceSequence);
  AppendUnsigned(Bytes, Command.CommandSequence);
  AppendUnsigned(Bytes, static_cast<uint8>(Command.Kind));
  AppendUnsigned(Bytes, Command.SourceTypeId.Value);
  AppendUnsigned(Bytes, static_cast<uint16>(Command.Priority));
  AppendInt32(Bytes, Command.LifetimeSteps);
  AppendUnsigned(Bytes, Command.Payload.SchemaId);
  AppendUnsigned(Bytes, Command.Payload.Size);
  for (uint16 Index = 0; Index < Command.Payload.Size; ++Index)
    AppendByte(Bytes, Command.Payload.Bytes[Index]);
  if (Bytes.Num() > MaxEncodedBehaviorCommandBytes)
  {
    OutPayload = {};
    return false;
  }
  OutPayload.RecalculateStableHash();
  return true;
}

uint64 FCrowdWorkerBoundaryStateCodec::CalculateStateHash(
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs,
  const TConstArrayView<uint64> PayloadHashes)
{
  if (EntityRefs.Num() != PayloadHashes.Num())
    return 0;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, uint32{1});
  for (int32 Index = 0; Index < EntityRefs.Num(); ++Index)
  {
    FoldRef(Hash, EntityRefs[Index]);
    FoldUnsigned(Hash, PayloadHashes[Index]);
  }
  return Hash;
}

bool FCrowdWorkerBoundaryShadowSync::Start(
  FCrowdAsyncSimulationRuntime& Runtime,
  const FCrowdWorkerShadowSyncConfig& InConfig,
  const uint64 InGeneration)
{
  if (bStarted || !InConfig.IsValid() || InGeneration == 0
    || !Runtime.Start(InConfig.RuntimeConfig, InGeneration))
    return false;
  Config = InConfig;
  Generation = InGeneration;
  NextInputSequence = 1;
  SnapshotResourceRevision = 0;
  PreviousAgents.Reset();
  Expectations.Reset();
  Metrics = {};
  Metrics.Generation = Generation;
  bStarted = true;
  bSubmittedResnapshot = false;
  return true;
}

bool FCrowdWorkerBoundaryShadowSync::EncodeSnapshot(
  const FCrowdMassBoundarySnapshot& Snapshot,
  TArray<FEncodedAgent>& OutAgents,
  FCrowdWorkerPayload& OutSnapshotResource) const
{
  OutAgents.Reset();
  if (!Snapshot.bValid || Snapshot.Agents.IsEmpty()
    || !FCrowdWorkerBoundaryStateCodec::EncodeSnapshotResource(
      Snapshot, OutSnapshotResource))
    return false;
  OutAgents.Reserve(Snapshot.Agents.Num());
  FCrowdStableEntityRef PreviousRef;
  for (const FCrowdMassBoundaryAgentRecord& Record : Snapshot.Agents)
  {
    FEncodedAgent Agent;
    Agent.EntityRef = Record.AgentFacts.StableEntityRef;
    if (!Agent.EntityRef.IsValid()
      || (!PreviousRef.IsUnset()
        && !(PreviousRef < Agent.EntityRef))
      || !FCrowdWorkerBoundaryStateCodec::EncodeState(
        Record, Agent.Payload)
      || !Agent.Payload.IsValid(
        Config.RuntimeConfig.ContractLimits.MaxPayloadBytes))
      return false;
    PreviousRef = Agent.EntityRef;
    OutAgents.Add(MoveTemp(Agent));
  }
  return OutSnapshotResource.IsValid(
    Config.RuntimeConfig.ContractLimits.MaxPayloadBytes);
}

ECrowdWorkerShadowSubmitResult
FCrowdWorkerBoundaryShadowSync::SubmitSnapshot(
  FCrowdAsyncSimulationRuntime& Runtime,
  const FCrowdMassBoundarySnapshot& Snapshot,
  const double TargetSimulationTimeSeconds,
  const TConstArrayView<FCrowdBehaviorSourceCommand>
    PendingBehaviorCommands)
{
  if (!bStarted || Metrics.bViolation
    || Runtime.GetGeneration() != Generation)
    return ECrowdWorkerShadowSubmitResult::RejectedState;
  if (!FMath::IsFinite(TargetSimulationTimeSeconds)
    || TargetSimulationTimeSeconds < 0.0)
    return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;

  TArray<FEncodedAgent> CurrentAgents;
  FCrowdWorkerPayload SnapshotResource;
  if (!EncodeSnapshot(
      Snapshot, CurrentAgents, SnapshotResource))
    return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;

  FCrowdWorkerInputBatch Batch;
  Batch.Generation = Generation;
  Batch.TargetSimulationTimeSeconds =
    TargetSimulationTimeSeconds;
  uint64 CandidateNextSequence = NextInputSequence;
  const auto AllocateSequence = [&CandidateNextSequence]()
  {
    return CandidateNextSequence++;
  };

  if (!bSubmittedResnapshot)
  {
    Batch.Spawns.Reserve(CurrentAgents.Num());
    for (const FEncodedAgent& Agent : CurrentAgents)
    {
      FCrowdWorkerSpawnDelta& Spawn =
        Batch.Spawns.AddDefaulted_GetRef();
      Spawn.InputSequence = AllocateSequence();
      Spawn.EntityRef = Agent.EntityRef;
      Spawn.InitialState = Agent.Payload;
    }
  }
  else
  {
    int32 PreviousIndex = 0;
    int32 CurrentIndex = 0;
    while (PreviousIndex < PreviousAgents.Num()
      || CurrentIndex < CurrentAgents.Num())
    {
      if (CurrentIndex >= CurrentAgents.Num()
        || (PreviousIndex < PreviousAgents.Num()
          && PreviousAgents[PreviousIndex].EntityRef
            < CurrentAgents[CurrentIndex].EntityRef))
      {
        FCrowdWorkerDespawnDelta& Despawn =
          Batch.Despawns.AddDefaulted_GetRef();
        Despawn.InputSequence = AllocateSequence();
        Despawn.EntityRef =
          PreviousAgents[PreviousIndex].EntityRef;
        Despawn.ReasonId = 1;
        ++PreviousIndex;
        continue;
      }
      if (PreviousIndex >= PreviousAgents.Num()
        || CurrentAgents[CurrentIndex].EntityRef
          < PreviousAgents[PreviousIndex].EntityRef)
      {
        FCrowdWorkerSpawnDelta& Spawn =
          Batch.Spawns.AddDefaulted_GetRef();
        Spawn.InputSequence = AllocateSequence();
        Spawn.EntityRef = CurrentAgents[CurrentIndex].EntityRef;
        Spawn.InitialState = CurrentAgents[CurrentIndex].Payload;
        ++CurrentIndex;
        continue;
      }
      if (!(PreviousAgents[PreviousIndex].Payload
          == CurrentAgents[CurrentIndex].Payload))
      {
        FCrowdWorkerStateDelta& State =
          Batch.StateDeltas.AddDefaulted_GetRef();
        State.InputSequence = AllocateSequence();
        State.EntityRef = CurrentAgents[CurrentIndex].EntityRef;
        State.DirtyMask = MAX_uint64;
        State.FullState = CurrentAgents[CurrentIndex].Payload;
      }
      ++PreviousIndex;
      ++CurrentIndex;
    }

    TArray<FCrowdBehaviorSourceCommand> SortedCommands(
      PendingBehaviorCommands);
    SortedCommands.Sort([](
      const FCrowdBehaviorSourceCommand& A,
      const FCrowdBehaviorSourceCommand& B)
    {
      if (A.EffectiveFixedStep != B.EffectiveFixedStep)
        return A.EffectiveFixedStep < B.EffectiveFixedStep;
      if (A.Handle != B.Handle)
        return A.Handle < B.Handle;
      if (A.CommandSequence != B.CommandSequence)
        return A.CommandSequence < B.CommandSequence;
      return A.Kind < B.Kind;
    });
    Batch.Commands.Reserve(SortedCommands.Num());
    for (const FCrowdBehaviorSourceCommand& SourceCommand
      : SortedCommands)
    {
      FCrowdWorkerPayload CommandPayload;
      if (!FCrowdWorkerBoundaryStateCodec::EncodeBehaviorCommand(
          SourceCommand, CommandPayload))
        return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
      FCrowdWorkerCommandDelta& Command =
        Batch.Commands.AddDefaulted_GetRef();
      Command.InputSequence = AllocateSequence();
      Command.EntityRef = SourceCommand.Handle.EntityRef;
      Command.CommandId =
        FCrowdWorkerBoundaryStateCodec::BehaviorCommandSchemaId;
      Command.EffectiveSimulationTimeSeconds =
        FMath::Max<int64>(0, SourceCommand.EffectiveFixedStep)
        * Config.RuntimeConfig.FixedSimulationQuantumSeconds;
      Command.Payload = MoveTemp(CommandPayload);
    }
  }

  FCrowdWorkerResourceDelta& Resource =
    Batch.ResourceDeltas.AddDefaulted_GetRef();
  Resource.InputSequence = AllocateSequence();
  Resource.ResourceId =
    FCrowdWorkerBoundaryStateCodec::SnapshotResourceId;
  Resource.Revision = SnapshotResourceRevision + 1;
  Resource.Payload = SnapshotResource;
  Batch.FirstInputSequence = NextInputSequence;
  Batch.LastInputSequence = CandidateNextSequence - 1;
  Batch.RecalculateStableHash();
  if (!Batch.IsValid(Config.RuntimeConfig.ContractLimits))
    return ECrowdWorkerShadowSubmitResult::RejectedCapacity;
  if (Expectations.Num() >= Config.MaxPendingExpectations)
    return ECrowdWorkerShadowSubmitResult::RejectedCapacity;

  const ECrowdAsyncSimulationSubmitResult SubmitResult =
    bSubmittedResnapshot
      ? Runtime.SubmitInput(Batch)
      : Runtime.SubmitResnapshot(Batch);
  if (SubmitResult
      == ECrowdAsyncSimulationSubmitResult::RejectedCapacity)
    return ECrowdWorkerShadowSubmitResult::RejectedCapacity;
  if (SubmitResult
      == ECrowdAsyncSimulationSubmitResult::RequiresResnapshot)
    return ECrowdWorkerShadowSubmitResult::RequiresResnapshot;
  if (SubmitResult != ECrowdAsyncSimulationSubmitResult::Accepted)
  {
    LatchViolation();
    return ECrowdWorkerShadowSubmitResult::Violation;
  }

  FExpectation Expectation;
  Expectation.LastInputSequence = Batch.LastInputSequence;
  Expectation.SourceSnapshotHash = Snapshot.StableHash;
  Expectation.SnapshotResourcePayloadHash =
    SnapshotResource.StableHash;
  Expectation.EntityRefs.Reserve(CurrentAgents.Num());
  TArray<uint64> PayloadHashes;
  PayloadHashes.Reserve(CurrentAgents.Num());
  for (const FEncodedAgent& Agent : CurrentAgents)
  {
    Expectation.EntityRefs.Add(Agent.EntityRef);
    PayloadHashes.Add(Agent.Payload.StableHash);
  }
  Expectation.EntitySetHash =
    CalculateEntitySetHash(Expectation.EntityRefs);
  Expectation.StateHash =
    FCrowdWorkerBoundaryStateCodec::CalculateStateHash(
      Expectation.EntityRefs, PayloadHashes);
  Expectations.Add(MoveTemp(Expectation));
  PreviousAgents = MoveTemp(CurrentAgents);
  NextInputSequence = CandidateNextSequence;
  ++SnapshotResourceRevision;
  bSubmittedResnapshot = true;
  ++Metrics.SubmittedSnapshotCount;
  Metrics.SubmittedInputRecordCount += Batch.GetRecordCount();
  Metrics.SubmittedCommandRecordCount += Batch.Commands.Num();
  Metrics.LastSubmittedCommandRecordCount = Batch.Commands.Num();
  Metrics.FullResnapshotCount =
    FMath::Max<uint64>(Metrics.FullResnapshotCount, 1);
  Metrics.LastSubmittedInputSequence =
    Batch.LastInputSequence;
  Metrics.LastSourceSnapshotHash = Snapshot.StableHash;
  Metrics.PendingExpectationCount = Expectations.Num();
  return ECrowdWorkerShadowSubmitResult::Accepted;
}

ECrowdWorkerShadowCompareResult
FCrowdWorkerBoundaryShadowSync::PollAndCompare(
  FCrowdAsyncSimulationRuntime& Runtime)
{
  if (!bStarted || Metrics.bViolation
    || Runtime.GetGeneration() != Generation)
    return ECrowdWorkerShadowCompareResult::Violation;
  const ECrowdAsyncSimulationPollResult PollResult =
    Runtime.Poll();
  if (PollResult == ECrowdAsyncSimulationPollResult::Failed)
  {
    LatchViolation();
    return ECrowdWorkerShadowCompareResult::Violation;
  }

  FCrowdWorkerMirrorSnapshot Mirror;
  if (!Runtime.ReadMirrorSnapshot(Mirror) || !Mirror.bValid
    || Mirror.LastAppliedInputSequence
      <= Metrics.LastComparedInputSequence)
  {
    return PollResult == ECrowdAsyncSimulationPollResult::Working
      ? ECrowdWorkerShadowCompareResult::Working
      : ECrowdWorkerShadowCompareResult::NoProgress;
  }

  int32 MatchIndex = INDEX_NONE;
  for (int32 Index = 0; Index < Expectations.Num(); ++Index)
  {
    if (Expectations[Index].LastInputSequence
        == Mirror.LastAppliedInputSequence)
    {
      MatchIndex = Index;
      break;
    }
  }
  if (MatchIndex == INDEX_NONE)
  {
    LatchViolation();
    return ECrowdWorkerShadowCompareResult::Violation;
  }

  const FExpectation& Expected = Expectations[MatchIndex];
  TArray<uint64> ObservedPayloadHashes;
  ObservedPayloadHashes.Reserve(Mirror.States.Num());
  for (const FCrowdWorkerPublishedState& State : Mirror.States)
    ObservedPayloadHashes.Add(State.Payload.StableHash);
  const uint64 ObservedStateHash =
    FCrowdWorkerBoundaryStateCodec::CalculateStateHash(
      Mirror.EntityRefs, ObservedPayloadHashes);
  uint64 ObservedSnapshotResourceHash = 0;
  const bool bHasSnapshotResource = FindSnapshotResource(
    Mirror, ObservedSnapshotResourceHash);

  Metrics.LastExpectedEntitySetHash = Expected.EntitySetHash;
  Metrics.LastObservedEntitySetHash = Mirror.EntitySetHash;
  Metrics.LastExpectedStateHash = Expected.StateHash;
  Metrics.LastObservedStateHash = ObservedStateHash;
  if (Mirror.Generation != Generation
    || Mirror.EntityRefs != Expected.EntityRefs
    || Mirror.EntitySetHash != Expected.EntitySetHash
    || ObservedStateHash != Expected.StateHash
    || !bHasSnapshotResource
    || ObservedSnapshotResourceHash
      != Expected.SnapshotResourcePayloadHash)
  {
    LatchViolation();
    return ECrowdWorkerShadowCompareResult::Violation;
  }

  Metrics.SupersededExpectationCount += MatchIndex;
  Metrics.LastComparedInputSequence =
    Expected.LastInputSequence;
  Metrics.LastComparedSourceSnapshotHash =
    Expected.SourceSnapshotHash;
  ++Metrics.ComparedSnapshotCount;
  Expectations.RemoveAt(0, MatchIndex + 1);
  Metrics.PendingExpectationCount = Expectations.Num();
  return ECrowdWorkerShadowCompareResult::Match;
}

void FCrowdWorkerBoundaryShadowSync::LatchViolation()
{
  Metrics.bViolation = true;
}

bool FCrowdWorkerBoundaryShadowSync::ResetQuiescent()
{
  Config = {};
  PreviousAgents.Reset();
  Expectations.Reset();
  Metrics = {};
  Generation = 0;
  NextInputSequence = 1;
  SnapshotResourceRevision = 0;
  bStarted = false;
  bSubmittedResnapshot = false;
  return true;
}
