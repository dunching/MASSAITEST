#include "MassCrowdWorkerShadowSync.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"

namespace CrowdWorkerShadowSyncPrivate
{
  constexpr uint64 ShadowFnvOffset64 =
    14695981039346656037ull;
  constexpr uint64 ShadowFnvPrime64 = 1099511628211ull;

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

  bool ReadInt32(
    const TConstArrayView<uint8> Bytes,
    const int32 Offset,
    int32& OutValue)
  {
    if (Offset < 0 || Offset + static_cast<int32>(
        sizeof(uint32)) > Bytes.Num())
      return false;
    uint32 Value = 0;
    for (uint32 Byte = 0; Byte < sizeof(uint32); ++Byte)
      Value |= static_cast<uint32>(Bytes[Offset + Byte])
        << (Byte * 8);
    OutValue = static_cast<int32>(Value);
    return true;
  }

  template<typename T>
  bool ReadUnsignedSequential(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    T& OutValue)
  {
    static_assert(std::is_unsigned_v<T>);
    if (Offset < 0
      || Offset + static_cast<int32>(sizeof(T)) > Bytes.Num())
      return false;
    T Value = 0;
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Value |= static_cast<T>(Bytes[Offset + Byte])
        << (Byte * 8);
    Offset += sizeof(T);
    OutValue = Value;
    return true;
  }

  bool ReadInt32Sequential(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int32& OutValue)
  {
    uint32 Value = 0;
    if (!ReadUnsignedSequential(Bytes, Offset, Value))
      return false;
    OutValue = static_cast<int32>(Value);
    return true;
  }

  bool ReadInt64Sequential(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int64& OutValue)
  {
    uint64 Value = 0;
    if (!ReadUnsignedSequential(Bytes, Offset, Value))
      return false;
    OutValue = static_cast<int64>(Value);
    return true;
  }

  bool ReadRefSequential(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdStableEntityRef& OutRef)
  {
    return ReadUnsignedSequential(
        Bytes, Offset, OutRef.ProviderId)
      && ReadUnsignedSequential(
        Bytes, Offset, OutRef.StableEntityId)
      && ReadUnsignedSequential(
        Bytes, Offset, OutRef.LifecycleSerial);
  }

  template<typename T>
  void ShadowFoldUnsigned(uint64& Hash, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= ShadowFnvPrime64;
    }
  }

  void ShadowFoldRef(
    uint64& Hash,
    const FCrowdStableEntityRef& Ref)
  {
    ShadowFoldUnsigned(Hash, Ref.ProviderId);
    ShadowFoldUnsigned(Hash, Ref.StableEntityId);
    ShadowFoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  uint64 CalculateEntitySetHash(
    const TConstArrayView<FCrowdStableEntityRef> Refs)
  {
    uint64 Hash = ShadowFnvOffset64;
    ShadowFoldUnsigned(Hash, uint32{1});
    for (const FCrowdStableEntityRef& Ref : Refs)
      ShadowFoldRef(Hash, Ref);
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
  Bytes.Reserve(EncodedStateByteCount);
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
  return Bytes.Num() == EncodedStateByteCount;
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

bool FCrowdWorkerBoundaryStateCodec::DecodeSnapshotResource(
  const FCrowdWorkerPayload& Payload,
  int32& OutFixedStepIndex,
  int32& OutPlanRevision)
{
  OutFixedStepIndex = INDEX_NONE;
  OutPlanRevision = INDEX_NONE;
  if (Payload.SchemaId != SnapshotResourceSchemaId
    || Payload.SchemaVersion != SnapshotResourceSchemaVersion
    || Payload.Bytes.Num() != 20
    || Payload.StableHash != Payload.CalculateStableHash()
    || !ReadInt32(Payload.Bytes, 0, OutFixedStepIndex)
    || !ReadInt32(Payload.Bytes, 4, OutPlanRevision))
    return false;
  uint64 SnapshotHash = 0;
  uint32 EntityCount = 0;
  int32 Offset = 8;
  return ReadUnsignedSequential(
      Payload.Bytes, Offset, SnapshotHash)
    && ReadUnsignedSequential(
      Payload.Bytes, Offset, EntityCount)
    && Offset == Payload.Bytes.Num()
    && OutFixedStepIndex >= 0
    && OutPlanRevision >= 0
    && SnapshotHash != 0
    && EntityCount > 0;
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

bool FCrowdWorkerBoundaryStateCodec::DecodeBehaviorCommand(
  const FCrowdWorkerPayload& Payload,
  FCrowdBehaviorSourceCommand& OutCommand)
{
  OutCommand = {};
  if (Payload.SchemaId != BehaviorCommandSchemaId
    || Payload.SchemaVersion != BehaviorCommandSchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBehaviorCommandBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint32 ControllerId = 0;
  uint8 Kind = 0;
  uint16 Priority = 0;
  uint16 PayloadSize = 0;
  if (!ReadInt64Sequential(
      Payload.Bytes, Offset, OutCommand.EffectiveFixedStep)
    || !ReadRefSequential(
      Payload.Bytes, Offset, OutCommand.Handle.EntityRef)
    || !ReadUnsignedSequential(
      Payload.Bytes, Offset, ControllerId)
    || !ReadUnsignedSequential(
      Payload.Bytes, Offset,
      OutCommand.Handle.SourceSequence)
    || !ReadUnsignedSequential(
      Payload.Bytes, Offset, OutCommand.CommandSequence)
    || !ReadUnsignedSequential(Payload.Bytes, Offset, Kind)
    || !ReadUnsignedSequential(
      Payload.Bytes, Offset, OutCommand.SourceTypeId.Value)
    || !ReadUnsignedSequential(
      Payload.Bytes, Offset, Priority)
    || !ReadInt32Sequential(
      Payload.Bytes, Offset, OutCommand.LifetimeSteps)
    || !ReadUnsignedSequential(
      Payload.Bytes, Offset, OutCommand.Payload.SchemaId)
    || !ReadUnsignedSequential(
      Payload.Bytes, Offset, PayloadSize)
    || PayloadSize > CrowdBehavior::MaxPayloadBytes
    || Offset + PayloadSize != Payload.Bytes.Num())
    return false;
  OutCommand.Handle.ControllerId.Value = ControllerId;
  OutCommand.Kind =
    static_cast<ECrowdBehaviorSourceCommandKind>(Kind);
  OutCommand.Priority = static_cast<int16>(Priority);
  OutCommand.Payload.Size = PayloadSize;
  if (PayloadSize > 0)
  {
    FMemory::Memcpy(
      OutCommand.Payload.Bytes,
      Payload.Bytes.GetData() + Offset,
      PayloadSize);
  }
  return OutCommand.IsValid();
}

bool FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerBoundaryKinematicState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != StateSchemaId
    || Payload.SchemaVersion != StateSchemaVersion
    || Payload.Bytes.Num() != EncodedStateByteCount
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 PositionX = 0;
  int32 PositionY = 0;
  int32 PositionZ = 0;
  int32 VelocityX = 0;
  int32 VelocityY = 0;
  int32 VelocityZ = 0;
  int32 Yaw = 0;
  int32 PlanRevision = INDEX_NONE;
  int32 PhysicalRadius = 0;
  int32 HardSafetyGap = 0;
  int32 SoftMargin = 0;
  int32 Mobility = 0;
  int32 MaximumSpeed = 0;
  int32 CapabilityProfileKey = 0;
  if (!ReadInt32(Payload.Bytes, 80, PositionX)
    || !ReadInt32(Payload.Bytes, 84, PositionY)
    || !ReadInt32(Payload.Bytes, 88, PositionZ)
    || !ReadInt32(Payload.Bytes, 92, VelocityX)
    || !ReadInt32(Payload.Bytes, 96, VelocityY)
    || !ReadInt32(Payload.Bytes, 100, VelocityZ)
    || !ReadInt32(Payload.Bytes, 104, Yaw)
    || !ReadInt32(Payload.Bytes, 108, PlanRevision)
    || !ReadInt32(Payload.Bytes, 112, PhysicalRadius)
    || !ReadInt32(Payload.Bytes, 116, HardSafetyGap)
    || !ReadInt32(Payload.Bytes, 120, SoftMargin)
    || !ReadInt32(Payload.Bytes, 124, Mobility)
    || !ReadInt32(Payload.Bytes, 128, MaximumSpeed)
    || !ReadInt32(Payload.Bytes, 132, CapabilityProfileKey))
    return false;
  OutState.Position = FVector(
    PositionX / 100.0,
    PositionY / 100.0,
    PositionZ / 100.0);
  OutState.Velocity = FVector(
    VelocityX / 100.0,
    VelocityY / 100.0,
    VelocityZ / 100.0);
  OutState.YawDegrees = Yaw / 100.0f;
  OutState.PlanRevision = PlanRevision;
  OutState.PhysicalRadiusCm = PhysicalRadius / 100.0f;
  OutState.HardSafetyGapCm = HardSafetyGap / 100.0f;
  OutState.SoftMarginCm = SoftMargin / 100.0f;
  OutState.Mobility = Mobility / 100.0f;
  OutState.MaximumSpeedCmps = MaximumSpeed / 100.0f;
  OutState.CapabilityProfileKey =
    static_cast<uint32>(CapabilityProfileKey);
  return !OutState.Position.ContainsNaN()
    && !OutState.Velocity.ContainsNaN()
    && FMath::IsFinite(OutState.YawDegrees)
    && OutState.PlanRevision >= 0
    && FMath::IsFinite(OutState.PhysicalRadiusCm)
    && OutState.PhysicalRadiusCm > 0.0f
    && FMath::IsFinite(OutState.HardSafetyGapCm)
    && OutState.HardSafetyGapCm >= 0.0f
    && FMath::IsFinite(OutState.SoftMarginCm)
    && OutState.SoftMarginCm >= 0.0f
    && FMath::IsFinite(OutState.Mobility)
    && OutState.Mobility >= 0.0f
    && FMath::IsFinite(OutState.MaximumSpeedCmps)
    && OutState.MaximumSpeedCmps >= 0.0f;
}

uint64 FCrowdWorkerBoundaryStateCodec::CalculateStateHash(
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs,
  const TConstArrayView<uint64> PayloadHashes)
{
  if (EntityRefs.Num() != PayloadHashes.Num())
    return 0;
  uint64 Hash = ShadowFnvOffset64;
  ShadowFoldUnsigned(Hash, uint32{1});
  for (int32 Index = 0; Index < EntityRefs.Num(); ++Index)
  {
    ShadowFoldRef(Hash, EntityRefs[Index]);
    ShadowFoldUnsigned(Hash, PayloadHashes[Index]);
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
  LastSnapshotResourcePayloadHash = 0;
  PreviousAgents.Reset();
  PreviousSourceSnapshot = {};
  PreviousVersionedResources.Reset();
  Expectations.Reset();
  Metrics = {};
  Metrics.Generation = Generation;
  bStarted = true;
  bSubmittedResnapshot = false;
  return true;
}

bool FCrowdWorkerBoundaryShadowSync::StartFromNetworkCheckpoint(
  FCrowdAsyncSimulationRuntime& Runtime,
  const FCrowdWorkerShadowSyncConfig& InConfig,
  const FCrowdWorkerNetworkCheckpoint& Checkpoint)
{
  if (bStarted || !InConfig.IsValid()
    || !Checkpoint.IsValid(InConfig.RuntimeConfig.NetworkState))
    return false;

  TArray<FEncodedAgent> RestoredAgents;
  RestoredAgents.Reserve(Checkpoint.StateRecords.Num());
  for (const FCrowdWorkerDirtyStateRecord& State :
    Checkpoint.StateRecords)
  {
    if (State.Field != ECrowdWorkerField::InputSnapshot)
      continue;
    if (!RestoredAgents.IsEmpty()
      && !(RestoredAgents.Last().EntityRef < State.EntityRef))
      return false;
    RestoredAgents.Add({State.EntityRef, State.Payload});
  }
  if (RestoredAgents.IsEmpty()) return false;

  uint64 RestoredSnapshotRevision = 0;
  uint64 RestoredSnapshotPayloadHash = 0;
  TMap<uint64, FCrowdWorkerResourceRecord> RestoredResources;
  for (const FCrowdWorkerResourceRecord& Resource :
    Checkpoint.ResourceRecords)
  {
    if (Resource.ResourceId
      == FCrowdWorkerBoundaryStateCodec::SnapshotResourceId)
    {
      RestoredSnapshotRevision = Resource.Revision;
      RestoredSnapshotPayloadHash = Resource.Payload.StableHash;
    }
    else
      RestoredResources.Add(Resource.ResourceId, Resource);
  }
  if (RestoredSnapshotRevision == 0
    || RestoredSnapshotPayloadHash == 0)
    return false;
  if (!Runtime.Start(
      InConfig.RuntimeConfig, Checkpoint.Header.Generation))
    return false;
  if (Runtime.RestoreNetworkCheckpoint(Checkpoint)
    != ECrowdAsyncSimulationRestoreResult::Restored)
  {
    Runtime.StopAndDrain(5.0);
    return false;
  }

  Config = InConfig;
  Generation = Checkpoint.Header.Generation;
  NextInputSequence = Checkpoint.Header.LastAppliedInputSequence + 1;
  SnapshotResourceRevision = RestoredSnapshotRevision;
  LastSnapshotResourcePayloadHash = RestoredSnapshotPayloadHash;
  PreviousAgents = MoveTemp(RestoredAgents);
  PreviousSourceSnapshot = {};
  PreviousVersionedResources = MoveTemp(RestoredResources);
  Expectations.Reset();
  Metrics = {};
  Metrics.Generation = Generation;
  Metrics.LastSubmittedInputSequence =
    Checkpoint.Header.LastAppliedInputSequence;
  Metrics.LastComparedInputSequence =
    Checkpoint.Header.LastAppliedInputSequence;
  bStarted = true;
  bSubmittedResnapshot = true;
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
    PendingBehaviorCommands,
  const TConstArrayView<
    FCrowdBehaviorEntityEvaluationContext>
      BehaviorContexts,
  const TConstArrayView<FCrowdWorkerVersionedResourceInput>
    VersionedResources,
  const TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
    PendingBehaviorBindingUpdates)
{
  Metrics.LastSubmitFailure =
    ECrowdWorkerShadowSubmitFailure::None;
  Metrics.LastRejectedResourceId = 0;
  Metrics.LastRejectedPreviousRevision = 0;
  Metrics.LastRejectedSubmittedRevision = 0;
  Metrics.LastRejectedPreviousPayloadHash = 0;
  Metrics.LastRejectedSubmittedPayloadHash = 0;
  const auto Reject = [this](
    const ECrowdWorkerShadowSubmitResult Result,
    const ECrowdWorkerShadowSubmitFailure Failure)
  {
    Metrics.LastSubmitFailure = Failure;
    return Result;
  };
  if (!bStarted || Metrics.bViolation
    || Runtime.GetGeneration() != Generation)
    return Reject(ECrowdWorkerShadowSubmitResult::RejectedState,
      ECrowdWorkerShadowSubmitFailure::InvalidState);
  if (!FMath::IsFinite(TargetSimulationTimeSeconds)
    || TargetSimulationTimeSeconds < 0.0)
    return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
      ECrowdWorkerShadowSubmitFailure::InvalidTime);

  TArray<FEncodedAgent> CurrentAgents;
  FCrowdWorkerPayload SnapshotResource;
  if (!EncodeSnapshot(
      Snapshot, CurrentAgents, SnapshotResource))
    return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
      ECrowdWorkerShadowSubmitFailure::SnapshotEncoding);
  Metrics.FullStateSerializationCount += CurrentAgents.Num();
  ++Metrics.SnapshotResourceSerializationCount;

  FCrowdWorkerIntentBatch Batch;
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
        FCrowdWorkerExternalGameplayInput& State =
          Batch.ExternalGameplayInputs.AddDefaulted_GetRef();
        State.InputSequence = AllocateSequence();
        State.EntityRef = CurrentAgents[CurrentIndex].EntityRef;
        State.DirtyMask = MAX_uint64;
        State.FullState = CurrentAgents[CurrentIndex].Payload;
      }
      ++PreviousIndex;
      ++CurrentIndex;
    }

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
  TArray<FCrowdBehaviorEntityEvaluationContext>
    SortedContexts(BehaviorContexts);
  SortedContexts.Sort([](
    const FCrowdBehaviorEntityEvaluationContext& A,
    const FCrowdBehaviorEntityEvaluationContext& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  TArray<FCrowdBehaviorCapabilityBindingUpdate> SortedBindings(
    PendingBehaviorBindingUpdates);
  SortedBindings.Sort([](
    const FCrowdBehaviorCapabilityBindingUpdate& A,
    const FCrowdBehaviorCapabilityBindingUpdate& B)
  {
    if (A.EffectiveFixedStep != B.EffectiveFixedStep)
      return A.EffectiveFixedStep < B.EffectiveFixedStep;
    if (A.EntityRef != B.EntityRef)
      return A.EntityRef < B.EntityRef;
    return A.StableHash < B.StableHash;
  });
  Batch.Commands.Reserve(
    SortedBindings.Num() + SortedCommands.Num()
      + SortedContexts.Num());
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update
    : SortedBindings)
  {
    FCrowdWorkerPayload BindingPayload;
    if (!FCrowdWorkerBehaviorBindingInputCodec::Encode(
        Update, BindingPayload))
      return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
        ECrowdWorkerShadowSubmitFailure::BindingEncoding);
    FCrowdWorkerCommandDelta& Command =
      Batch.Commands.AddDefaulted_GetRef();
    Command.InputSequence = AllocateSequence();
    Command.EntityRef = Update.EntityRef;
    Command.CommandId =
      FCrowdWorkerBehaviorBindingInputCodec::SchemaId;
    Command.EffectiveSimulationTimeSeconds =
      FMath::Max<int64>(0, Update.EffectiveFixedStep)
      * Config.RuntimeConfig.FixedSimulationQuantumSeconds;
    Command.Payload = MoveTemp(BindingPayload);
  }
  for (const FCrowdBehaviorSourceCommand& SourceCommand
    : SortedCommands)
  {
    FCrowdWorkerPayload CommandPayload;
    if (!FCrowdWorkerBoundaryStateCodec::EncodeBehaviorCommand(
        SourceCommand, CommandPayload))
      return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
        ECrowdWorkerShadowSubmitFailure::CommandEncoding);
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
  FCrowdStableEntityRef PreviousContextRef;
  for (const FCrowdBehaviorEntityEvaluationContext& Context :
    SortedContexts)
  {
    if (!Context.IsValid()
      || (!PreviousContextRef.IsUnset()
        && !(PreviousContextRef < Context.EntityRef)))
      return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
        ECrowdWorkerShadowSubmitFailure::InvalidContext);
    FCrowdWorkerPayload ContextPayload;
    if (!FCrowdWorkerBehaviorInputCodec::Encode(
        Context, ContextPayload))
      return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
        ECrowdWorkerShadowSubmitFailure::ContextEncoding);
    FCrowdWorkerCommandDelta& Command =
      Batch.Commands.AddDefaulted_GetRef();
    Command.InputSequence = AllocateSequence();
    Command.EntityRef = Context.EntityRef;
    Command.CommandId = FCrowdWorkerBehaviorInputCodec::SchemaId;
    Command.EffectiveSimulationTimeSeconds =
      FMath::Max<int64>(0, Context.FixedStepIndex)
      * Config.RuntimeConfig.FixedSimulationQuantumSeconds;
    Command.Payload = MoveTemp(ContextPayload);
    PreviousContextRef = Context.EntityRef;
  }

  FCrowdWorkerResourceDelta& Resource =
    Batch.ResourceDeltas.AddDefaulted_GetRef();
  Resource.InputSequence = AllocateSequence();
  Resource.ResourceId =
    FCrowdWorkerBoundaryStateCodec::SnapshotResourceId;
  Resource.Revision = SnapshotResourceRevision + 1;
  Resource.Payload = SnapshotResource;
  TArray<FCrowdWorkerVersionedResourceInput> SortedResources(
    VersionedResources);
  SortedResources.Sort([](
    const FCrowdWorkerVersionedResourceInput& A,
    const FCrowdWorkerVersionedResourceInput& B)
  {
    return A.ResourceId < B.ResourceId;
  });
  TMap<uint64, FCrowdWorkerResourceRecord>
    CandidateVersionedResources = PreviousVersionedResources;
  uint64 PreviousResourceId = 0;
  for (const FCrowdWorkerVersionedResourceInput& Input :
    SortedResources)
  {
    if (!Input.IsValid(
        Config.RuntimeConfig.ContractLimits.MaxPayloadBytes))
      return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
        ECrowdWorkerShadowSubmitFailure::InvalidVersionedResource);
    if (Input.ResourceId == PreviousResourceId)
      return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
        ECrowdWorkerShadowSubmitFailure::DuplicateVersionedResource);
    PreviousResourceId = Input.ResourceId;
    const FCrowdWorkerResourceRecord* Previous =
      PreviousVersionedResources.Find(Input.ResourceId);
    if (Previous)
    {
      if (Input.Revision < Previous->Revision)
      {
        Metrics.LastRejectedResourceId = Input.ResourceId;
        Metrics.LastRejectedPreviousRevision = Previous->Revision;
        Metrics.LastRejectedSubmittedRevision = Input.Revision;
        Metrics.LastRejectedPreviousPayloadHash =
          Previous->Payload.StableHash;
        Metrics.LastRejectedSubmittedPayloadHash =
          Input.Payload.StableHash;
        return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
          ECrowdWorkerShadowSubmitFailure::ResourceRevisionRegression);
      }
      if (Input.Revision == Previous->Revision
        && !(Input.Payload == Previous->Payload))
      {
        Metrics.LastRejectedResourceId = Input.ResourceId;
        Metrics.LastRejectedPreviousRevision = Previous->Revision;
        Metrics.LastRejectedSubmittedRevision = Input.Revision;
        Metrics.LastRejectedPreviousPayloadHash =
          Previous->Payload.StableHash;
        Metrics.LastRejectedSubmittedPayloadHash =
          Input.Payload.StableHash;
        return Reject(ECrowdWorkerShadowSubmitResult::RejectedSnapshot,
          ECrowdWorkerShadowSubmitFailure::ConflictingResourceRevision);
      }
      if (Input.Revision == Previous->Revision)
        continue;
    }
    FCrowdWorkerResourceDelta& Delta =
      Batch.ResourceDeltas.AddDefaulted_GetRef();
    Delta.InputSequence = AllocateSequence();
    Delta.ResourceId = Input.ResourceId;
    Delta.Revision = Input.Revision;
    Delta.Payload = Input.Payload;
    CandidateVersionedResources.Add(
      Input.ResourceId,
      FCrowdWorkerResourceRecord{
        Input.ResourceId, Input.Revision, Input.Payload});
  }
  Batch.Clock.InputSequence = AllocateSequence();
  Batch.Clock.SimulationTick = FMath::Max<uint64>(
    1,
    static_cast<uint64>(FMath::RoundToInt64(
      TargetSimulationTimeSeconds
        / Config.RuntimeConfig.FixedSimulationQuantumSeconds)));
  Batch.FirstInputSequence = NextInputSequence;
  Batch.LastInputSequence = CandidateNextSequence - 1;
  Batch.RecalculateStableHash();
  if (!Batch.IsValid(Config.RuntimeConfig.ContractLimits))
    return Reject(ECrowdWorkerShadowSubmitResult::RejectedCapacity,
      ECrowdWorkerShadowSubmitFailure::InvalidBatch);
  if (Expectations.Num() >= Config.MaxPendingExpectations)
    return Reject(ECrowdWorkerShadowSubmitResult::RejectedCapacity,
      ECrowdWorkerShadowSubmitFailure::ExpectationCapacity);

  const ECrowdAsyncSimulationSubmitResult SubmitResult =
    bSubmittedResnapshot
      ? Runtime.SubmitIntentBatch(Batch)
      : Runtime.SubmitResnapshot(Batch);
  if (SubmitResult
      == ECrowdAsyncSimulationSubmitResult::RejectedCapacity)
    return Reject(ECrowdWorkerShadowSubmitResult::RejectedCapacity,
      ECrowdWorkerShadowSubmitFailure::RuntimeCapacity);
  if (SubmitResult
      == ECrowdAsyncSimulationSubmitResult::RequiresResnapshot)
    return Reject(ECrowdWorkerShadowSubmitResult::RequiresResnapshot,
      ECrowdWorkerShadowSubmitFailure::RuntimeRequiresResnapshot);
  if (SubmitResult != ECrowdAsyncSimulationSubmitResult::Accepted)
  {
    LatchViolation();
    return Reject(ECrowdWorkerShadowSubmitResult::Violation,
      ECrowdWorkerShadowSubmitFailure::RuntimeViolation);
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
  PreviousSourceSnapshot = Snapshot;
  PreviousVersionedResources =
    MoveTemp(CandidateVersionedResources);
  NextInputSequence = CandidateNextSequence;
  ++SnapshotResourceRevision;
  LastSnapshotResourcePayloadHash = SnapshotResource.StableHash;
  bSubmittedResnapshot = true;
  ++Metrics.SubmittedSnapshotCount;
  Metrics.SubmittedInputRecordCount += Batch.GetRecordCount();
  Metrics.SubmittedCommandRecordCount += Batch.Commands.Num();
  Metrics.LastSubmittedCommandRecordCount =
    SortedCommands.Num();
  Metrics.LastSubmittedBindingRecordCount =
    SortedBindings.Num();
  Metrics.FullResnapshotCount =
    FMath::Max<uint64>(Metrics.FullResnapshotCount, 1);
  Metrics.LastSubmittedInputSequence =
    Batch.LastInputSequence;
  Metrics.LastSourceSnapshotHash = Snapshot.StableHash;
  Metrics.PendingExpectationCount = Expectations.Num();
  return ECrowdWorkerShadowSubmitResult::Accepted;
}

ECrowdWorkerShadowSubmitResult
FCrowdWorkerBoundaryShadowSync::SubmitAutonomousFrame(
  FCrowdAsyncSimulationRuntime& Runtime,
  const int32 FixedStepIndex,
  const int32 PlanRevision,
  const double TargetSimulationTimeSeconds,
  const TConstArrayView<FCrowdBehaviorSourceCommand>
    PendingBehaviorCommands,
  const TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
    BehaviorContexts,
  const TConstArrayView<FCrowdWorkerVersionedResourceInput>
    VersionedResources,
  const TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
    PendingBehaviorBindingUpdates)
{
  if (!bStarted || !bSubmittedResnapshot
    || !PreviousSourceSnapshot.bValid
    || PreviousSourceSnapshot.Agents.IsEmpty()
    || FixedStepIndex < 0 || PlanRevision < 0)
  {
    Metrics.LastSubmitFailure =
      ECrowdWorkerShadowSubmitFailure::InvalidState;
    return ECrowdWorkerShadowSubmitResult::RejectedState;
  }
  if (!FMath::IsFinite(TargetSimulationTimeSeconds)
    || TargetSimulationTimeSeconds < 0.0
    || Runtime.GetGeneration() != Generation
    || Metrics.bViolation
    || LastSnapshotResourcePayloadHash == 0)
  {
    Metrics.LastSubmitFailure =
      ECrowdWorkerShadowSubmitFailure::InvalidTime;
    return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
  }

  Metrics.LastSubmitFailure =
    ECrowdWorkerShadowSubmitFailure::None;
  Metrics.LastRejectedResourceId = 0;
  Metrics.LastRejectedPreviousRevision = 0;
  Metrics.LastRejectedSubmittedRevision = 0;
  Metrics.LastRejectedPreviousPayloadHash = 0;
  Metrics.LastRejectedSubmittedPayloadHash = 0;
  FCrowdWorkerIntentBatch Batch;
  Batch.Generation = Generation;
  Batch.TargetSimulationTimeSeconds = TargetSimulationTimeSeconds;
  uint64 CandidateNextSequence = NextInputSequence;
  const auto AllocateSequence = [&CandidateNextSequence]()
  {
    return CandidateNextSequence++;
  };

  TArray<FCrowdBehaviorCapabilityBindingUpdate> SortedBindings(
    PendingBehaviorBindingUpdates);
  SortedBindings.Sort([](
    const FCrowdBehaviorCapabilityBindingUpdate& A,
    const FCrowdBehaviorCapabilityBindingUpdate& B)
  {
    if (A.EffectiveFixedStep != B.EffectiveFixedStep)
      return A.EffectiveFixedStep < B.EffectiveFixedStep;
    if (A.EntityRef != B.EntityRef)
      return A.EntityRef < B.EntityRef;
    return A.StableHash < B.StableHash;
  });
  TArray<FCrowdBehaviorSourceCommand> SortedCommands(
    PendingBehaviorCommands);
  SortedCommands.Sort([](
    const FCrowdBehaviorSourceCommand& A,
    const FCrowdBehaviorSourceCommand& B)
  {
    if (A.EffectiveFixedStep != B.EffectiveFixedStep)
      return A.EffectiveFixedStep < B.EffectiveFixedStep;
    if (A.Handle != B.Handle) return A.Handle < B.Handle;
    if (A.CommandSequence != B.CommandSequence)
      return A.CommandSequence < B.CommandSequence;
    return A.Kind < B.Kind;
  });
  TArray<FCrowdBehaviorEntityEvaluationContext> SortedContexts(
    BehaviorContexts);
  SortedContexts.Sort([](
    const FCrowdBehaviorEntityEvaluationContext& A,
    const FCrowdBehaviorEntityEvaluationContext& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  Batch.Commands.Reserve(
    SortedBindings.Num() + SortedCommands.Num()
      + SortedContexts.Num());
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update :
    SortedBindings)
  {
    FCrowdWorkerCommandDelta& Command =
      Batch.Commands.AddDefaulted_GetRef();
    Command.InputSequence = AllocateSequence();
    Command.EntityRef = Update.EntityRef;
    Command.CommandId =
      FCrowdWorkerBehaviorBindingInputCodec::SchemaId;
    Command.EffectiveSimulationTimeSeconds =
      FMath::Max<int64>(0, Update.EffectiveFixedStep)
        * Config.RuntimeConfig.FixedSimulationQuantumSeconds;
    if (!FCrowdWorkerBehaviorBindingInputCodec::Encode(
        Update, Command.Payload))
      return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
  }
  for (const FCrowdBehaviorSourceCommand& SourceCommand :
    SortedCommands)
  {
    FCrowdWorkerCommandDelta& Command =
      Batch.Commands.AddDefaulted_GetRef();
    Command.InputSequence = AllocateSequence();
    Command.EntityRef = SourceCommand.Handle.EntityRef;
    Command.CommandId =
      FCrowdWorkerBoundaryStateCodec::BehaviorCommandSchemaId;
    Command.EffectiveSimulationTimeSeconds =
      FMath::Max<int64>(0, SourceCommand.EffectiveFixedStep)
        * Config.RuntimeConfig.FixedSimulationQuantumSeconds;
    if (!FCrowdWorkerBoundaryStateCodec::EncodeBehaviorCommand(
        SourceCommand, Command.Payload))
      return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
  }
  FCrowdStableEntityRef PreviousContextRef;
  for (const FCrowdBehaviorEntityEvaluationContext& Context :
    SortedContexts)
  {
    if (!Context.IsValid()
      || (!PreviousContextRef.IsUnset()
        && !(PreviousContextRef < Context.EntityRef)))
      return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
    FCrowdWorkerCommandDelta& Command =
      Batch.Commands.AddDefaulted_GetRef();
    Command.InputSequence = AllocateSequence();
    Command.EntityRef = Context.EntityRef;
    Command.CommandId = FCrowdWorkerBehaviorInputCodec::SchemaId;
    Command.EffectiveSimulationTimeSeconds =
      FMath::Max<int64>(0, Context.FixedStepIndex)
        * Config.RuntimeConfig.FixedSimulationQuantumSeconds;
    if (!FCrowdWorkerBehaviorInputCodec::Encode(
        Context, Command.Payload))
      return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
    PreviousContextRef = Context.EntityRef;
  }

  TArray<FCrowdWorkerVersionedResourceInput> SortedResources(
    VersionedResources);
  SortedResources.Sort([](
    const FCrowdWorkerVersionedResourceInput& A,
    const FCrowdWorkerVersionedResourceInput& B)
  {
    return A.ResourceId < B.ResourceId;
  });
  TMap<uint64, FCrowdWorkerResourceRecord>
    CandidateVersionedResources = PreviousVersionedResources;
  uint64 PreviousResourceId = 0;
  for (const FCrowdWorkerVersionedResourceInput& Input :
    SortedResources)
  {
    if (!Input.IsValid(
        Config.RuntimeConfig.ContractLimits.MaxPayloadBytes)
      || Input.ResourceId == PreviousResourceId)
      return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
    PreviousResourceId = Input.ResourceId;
    const FCrowdWorkerResourceRecord* Previous =
      PreviousVersionedResources.Find(Input.ResourceId);
    if (Previous)
    {
      if (Input.Revision < Previous->Revision
        || (Input.Revision == Previous->Revision
          && !(Input.Payload == Previous->Payload)))
        return ECrowdWorkerShadowSubmitResult::RejectedSnapshot;
      if (Input.Revision == Previous->Revision) continue;
    }
    FCrowdWorkerResourceDelta& Delta =
      Batch.ResourceDeltas.AddDefaulted_GetRef();
    Delta.InputSequence = AllocateSequence();
    Delta.ResourceId = Input.ResourceId;
    Delta.Revision = Input.Revision;
    Delta.Payload = Input.Payload;
    CandidateVersionedResources.Add(
      Input.ResourceId,
      {Input.ResourceId, Input.Revision, Input.Payload});
  }

  Batch.Clock.InputSequence = AllocateSequence();
  Batch.Clock.SimulationTick = FMath::Max<uint64>(
    1,
    static_cast<uint64>(FMath::RoundToInt64(
      TargetSimulationTimeSeconds
        / Config.RuntimeConfig.FixedSimulationQuantumSeconds)));
  Batch.FirstInputSequence = NextInputSequence;
  Batch.LastInputSequence = CandidateNextSequence - 1;
  Batch.RecalculateStableHash();
  const bool bCompareFullMirror =
    Config.RuntimeConfig.WorkerV2.GetEffectiveMode()
      != ECrowdWorkerRuntimeV2Mode::Production;
  if (!Batch.IsValid(Config.RuntimeConfig.ContractLimits)
    || (bCompareFullMirror
      && Expectations.Num() >= Config.MaxPendingExpectations))
    return ECrowdWorkerShadowSubmitResult::RejectedCapacity;
  const ECrowdAsyncSimulationSubmitResult SubmitResult =
    Runtime.SubmitIntentBatch(Batch);
  if (SubmitResult != ECrowdAsyncSimulationSubmitResult::Accepted)
  {
    if (SubmitResult !=
        ECrowdAsyncSimulationSubmitResult::RejectedCapacity
      && SubmitResult !=
        ECrowdAsyncSimulationSubmitResult::RequiresResnapshot)
      LatchViolation();
    return SubmitResult
        == ECrowdAsyncSimulationSubmitResult::RejectedCapacity
      ? ECrowdWorkerShadowSubmitResult::RejectedCapacity
      : SubmitResult
          == ECrowdAsyncSimulationSubmitResult::RequiresResnapshot
        ? ECrowdWorkerShadowSubmitResult::RequiresResnapshot
        : ECrowdWorkerShadowSubmitResult::Violation;
  }

  if (bCompareFullMirror)
  {
    FExpectation Expectation;
    Expectation.LastInputSequence = Batch.LastInputSequence;
    Expectation.SourceSnapshotHash = PreviousSourceSnapshot.StableHash;
    Expectation.SnapshotResourcePayloadHash =
      LastSnapshotResourcePayloadHash;
    TArray<uint64> PayloadHashes;
    Expectation.EntityRefs.Reserve(PreviousAgents.Num());
    PayloadHashes.Reserve(PreviousAgents.Num());
    for (const FEncodedAgent& Agent : PreviousAgents)
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
  }
  PreviousVersionedResources = MoveTemp(CandidateVersionedResources);
  NextInputSequence = CandidateNextSequence;
  ++Metrics.SubmittedSnapshotCount;
  Metrics.SubmittedInputRecordCount += Batch.GetRecordCount();
  Metrics.SubmittedCommandRecordCount += Batch.Commands.Num();
  Metrics.LastSubmittedCommandRecordCount = SortedCommands.Num();
  Metrics.LastSubmittedBindingRecordCount = SortedBindings.Num();
  Metrics.LastSubmittedInputSequence = Batch.LastInputSequence;
  Metrics.LastSourceSnapshotHash = PreviousSourceSnapshot.StableHash;
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

  if (Config.RuntimeConfig.WorkerV2.GetEffectiveMode()
      == ECrowdWorkerRuntimeV2Mode::Production)
  {
    const FCrowdAsyncSimulationRuntimeMetrics RuntimeMetrics =
      Runtime.GetMetrics();
    if (RuntimeMetrics.LastAppliedInputSequence
        <= Metrics.LastComparedInputSequence)
    {
      return PollResult == ECrowdAsyncSimulationPollResult::Working
        ? ECrowdWorkerShadowCompareResult::Working
        : ECrowdWorkerShadowCompareResult::NoProgress;
    }
    Metrics.LastComparedInputSequence =
      RuntimeMetrics.LastAppliedInputSequence;
    Metrics.LastComparedSourceSnapshotHash =
      PreviousSourceSnapshot.StableHash;
    ++Metrics.ComparedSnapshotCount;
    Metrics.SupersededExpectationCount += Expectations.Num();
    Expectations.Reset();
    Metrics.PendingExpectationCount = 0;
    return ECrowdWorkerShadowCompareResult::Match;
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
  PreviousSourceSnapshot = {};
  PreviousVersionedResources.Reset();
  Expectations.Reset();
  Metrics = {};
  Generation = 0;
  NextInputSequence = 1;
  SnapshotResourceRevision = 0;
  LastSnapshotResourcePayloadHash = 0;
  bStarted = false;
  bSubmittedResnapshot = false;
  return true;
}

uint64 FCrowdWorkerBoundaryShadowSync::
ResolveInputSequenceForSnapshotHash(
  const uint64 SourceSnapshotHash) const
{
  if (SourceSnapshotHash == 0) return 0;
  if (Metrics.LastComparedSourceSnapshotHash
      == SourceSnapshotHash)
    return Metrics.LastComparedInputSequence;
  for (const FExpectation& Expectation : Expectations)
  {
    if (Expectation.SourceSnapshotHash
        == SourceSnapshotHash)
      return Expectation.LastInputSequence;
  }
  return 0;
}
