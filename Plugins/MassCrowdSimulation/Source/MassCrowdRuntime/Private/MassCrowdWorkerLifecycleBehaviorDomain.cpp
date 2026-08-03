#include "MassCrowdWorkerLifecycleBehaviorDomain.h"

#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerShadowSync.h"

namespace CrowdWorkerLifecycleBehaviorPrivate
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;
  constexpr uint64 BehaviorWakeupId = 0x4245484156494F52ull;

  template<typename T>
  void WritePod(TArray<uint8>& Bytes, const T& Value)
  {
    static_assert(std::is_trivially_copyable_v<T>);
    const int32 Offset = Bytes.AddUninitialized(sizeof(T));
    FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(T));
  }

  template<typename T>
  bool ReadPod(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    T& OutValue)
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (Offset < 0 || Offset + sizeof(T) > Bytes.Num())
      return false;
    FMemory::Memcpy(&OutValue, Bytes.GetData() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
  }

  void WriteRef(
    TArray<uint8>& Bytes,
    const FCrowdStableEntityRef& Ref)
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

  void WriteVector(
    TArray<uint8>& Bytes,
    const FVector& Value)
  {
    WritePod(Bytes, Value.X);
    WritePod(Bytes, Value.Y);
    WritePod(Bytes, Value.Z);
  }

  bool ReadVector(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FVector& OutValue)
  {
    return ReadPod(Bytes, Offset, OutValue.X)
      && ReadPod(Bytes, Offset, OutValue.Y)
      && ReadPod(Bytes, Offset, OutValue.Z)
      && !OutValue.ContainsNaN();
  }

  void Fold(uint64& Hash, const uint64 Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(Value); ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= FnvPrime64;
    }
  }

  uint64 CalculateCommandBatchHash(
    const TConstArrayView<FCrowdBehaviorSourceCommand> Commands)
  {
    uint64 Hash = FnvOffset64;
    Fold(Hash, static_cast<uint32>(Commands.Num()));
    for (const FCrowdBehaviorSourceCommand& Command : Commands)
      Fold(Hash, Command.CalculateStableHash());
    return Hash;
  }

  bool AreBindingsEqual(
    const FCrowdCapabilityBinding& A,
    const FCrowdCapabilityBinding& B)
  {
    if (A.ProfileKey != B.ProfileKey
      || A.ModifierRevision != B.ModifierRevision
      || A.ModifierCount != B.ModifierCount)
      return false;
    for (uint8 Index = 0; Index < A.ModifierCount; ++Index)
      if (!(A.Modifiers[Index] == B.Modifiers[Index]))
        return false;
    return true;
  }

  uint64 CalculateBehaviorEventId(
    const FCrowdBehaviorSourceEvent& Event,
    const uint32 StableOrdinal)
  {
    uint64 Hash = FnvOffset64;
    Fold(Hash, static_cast<uint64>(Event.Kind));
    Fold(Hash, static_cast<uint64>(Event.FixedStepIndex));
    Fold(Hash, Event.Handle.EntityRef.ProviderId);
    Fold(Hash, Event.Handle.EntityRef.StableEntityId);
    Fold(Hash, Event.Handle.EntityRef.LifecycleSerial);
    Fold(Hash, Event.Handle.ControllerId.Value);
    Fold(Hash, Event.Handle.SourceSequence);
    Fold(Hash, Event.SourceTypeId.Value);
    Fold(Hash, StableOrdinal);
    return Hash == 0 ? 1 : Hash;
  }

  void CalculateSourceSetTraceHashes(
    const FCrowdBehaviorSourceSet& SourceSet,
    uint64& OutStateHash,
    uint64& OutTimelineHash,
    uint64& OutCursorHash)
  {
    OutStateHash = FnvOffset64;
    OutTimelineHash = FnvOffset64;
    OutCursorHash = FnvOffset64;
    for (const FCrowdBehaviorSourceInstance& Instance :
      SourceSet.Instances)
    {
      Fold(OutStateHash, Instance.SourceTypeId.Value);
      Fold(OutStateHash, Instance.State.CalculateStableHash());
      Fold(OutTimelineHash, Instance.SourceTypeId.Value);
      Fold(OutTimelineHash,
        static_cast<uint64>(Instance.StartFixedStep));
      Fold(OutTimelineHash,
        static_cast<uint64>(Instance.LastUpdateFixedStep));
      Fold(OutTimelineHash,
        static_cast<uint64>(Instance.ExpireFixedStep));
    }
    for (const FCrowdBehaviorControllerCursor& Cursor :
      SourceSet.ControllerCursors)
    {
      Fold(OutCursorHash, Cursor.ControllerId.Value);
      Fold(OutCursorHash, Cursor.LastCommandSequence);
      Fold(OutCursorHash, Cursor.LastCommandHash);
    }
  }

  uint64 CalculateSourceSetContentHash(
    const FCrowdBehaviorSourceSet& SourceSet)
  {
    FCrowdBehaviorSourceSet Canonical = SourceSet;
    Canonical.Revision = 1;
    Canonical.RecalculateStableHash();
    return Canonical.StableHash;
  }

  uint64 CalculateSourceSetControlHash(
    const FCrowdBehaviorSourceSet& SourceSet)
  {
    FCrowdBehaviorSourceSet Canonical = SourceSet;
    Canonical.Revision = 1;
    for (FCrowdBehaviorSourceInstance& Instance :
      Canonical.Instances)
      Instance.State = {};
    Canonical.RecalculateStableHash();
    return Canonical.StableHash;
  }

  void WriteBinding(
    TArray<uint8>& Bytes,
    const FCrowdCapabilityBinding& Binding)
  {
    WritePod(Bytes, Binding.ProfileKey.Value);
    WritePod(Bytes, Binding.ModifierRevision);
    WritePod(Bytes, Binding.ModifierCount);
    for (uint8 Index = 0;
      Index < Binding.ModifierCount; ++Index)
    {
      WritePod(
        Bytes, Binding.Modifiers[Index].CapabilityId.Value);
      WritePod(Bytes, static_cast<uint8>(
        Binding.Modifiers[Index].Operation));
    }
  }

  bool ReadBinding(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdCapabilityBinding& OutBinding)
  {
    OutBinding = {};
    if (!ReadPod(
        Bytes, Offset, OutBinding.ProfileKey.Value)
      || !ReadPod(
        Bytes, Offset, OutBinding.ModifierRevision)
      || !ReadPod(
        Bytes, Offset, OutBinding.ModifierCount)
      || OutBinding.ModifierCount
        > CrowdBehavior::MaxCapabilityModifiers)
      return false;
    for (uint8 Index = 0;
      Index < OutBinding.ModifierCount; ++Index)
    {
      uint8 Operation = 0;
      if (!ReadPod(
          Bytes, Offset,
          OutBinding.Modifiers[Index].CapabilityId.Value)
        || !ReadPod(Bytes, Offset, Operation))
        return false;
      OutBinding.Modifiers[Index].Operation =
        static_cast<ECrowdCapabilityModifierOperation>(
          Operation);
    }
    return OutBinding.IsValid();
  }

  void WriteSourcePayload(
    TArray<uint8>& Bytes,
    const FCrowdBehaviorSourcePayload& Payload)
  {
    WritePod(Bytes, Payload.SchemaId);
    WritePod(Bytes, Payload.Size);
    const int32 Offset = Bytes.AddUninitialized(Payload.Size);
    if (Payload.Size > 0)
      FMemory::Memcpy(
        Bytes.GetData() + Offset,
        Payload.Bytes,
        Payload.Size);
  }

  bool ReadSourcePayload(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdBehaviorSourcePayload& OutPayload)
  {
    OutPayload = {};
    if (!ReadPod(Bytes, Offset, OutPayload.SchemaId)
      || !ReadPod(Bytes, Offset, OutPayload.Size)
      || OutPayload.Size > CrowdBehavior::MaxPayloadBytes
      || Offset + OutPayload.Size > Bytes.Num())
      return false;
    if (OutPayload.Size > 0)
      FMemory::Memcpy(
        OutPayload.Bytes,
        Bytes.GetData() + Offset,
        OutPayload.Size);
    Offset += OutPayload.Size;
    return OutPayload.IsValid();
  }

  void WriteSourceState(
    TArray<uint8>& Bytes,
    const FCrowdBehaviorSourceState& State)
  {
    WritePod(Bytes, State.SchemaId);
    WritePod(Bytes, State.Size);
    const int32 Offset = Bytes.AddUninitialized(State.Size);
    if (State.Size > 0)
      FMemory::Memcpy(
        Bytes.GetData() + Offset,
        State.Bytes,
        State.Size);
  }

  bool ReadSourceState(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdBehaviorSourceState& OutState)
  {
    OutState = {};
    if (!ReadPod(Bytes, Offset, OutState.SchemaId)
      || !ReadPod(Bytes, Offset, OutState.Size)
      || OutState.Size > CrowdBehavior::MaxStateBytes
      || Offset + OutState.Size > Bytes.Num())
      return false;
    if (OutState.Size > 0)
      FMemory::Memcpy(
        OutState.Bytes,
        Bytes.GetData() + Offset,
        OutState.Size);
    Offset += OutState.Size;
    return OutState.IsValid();
  }

  void WriteContributionKey(
    TArray<uint8>& Bytes,
    const FCrowdBehaviorContributionKey& Key)
  {
    WritePod(Bytes, Key.Priority);
    WritePod(Bytes, Key.SourceTypeId.Value);
    WritePod(Bytes, Key.ControllerId.Value);
    WritePod(Bytes, Key.SourceSequence);
  }

  bool ReadContributionKey(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdBehaviorContributionKey& OutKey)
  {
    return ReadPod(Bytes, Offset, OutKey.Priority)
      && ReadPod(
        Bytes, Offset, OutKey.SourceTypeId.Value)
      && ReadPod(
        Bytes, Offset, OutKey.ControllerId.Value)
      && ReadPod(Bytes, Offset, OutKey.SourceSequence)
      && OutKey.IsValid();
  }

  void WriteResolvedChannels(
    TArray<uint8>& Bytes,
    const FCrowdResolvedBehaviorChannels& Resolved)
  {
    WriteVector(Bytes, Resolved.DesiredVelocity);
    WriteVector(Bytes, Resolved.MovementGoal.Location);
    WriteRef(Bytes, Resolved.MovementGoal.TargetRef);
    WritePod(Bytes, Resolved.MovementGoal.FactRevision);
    WritePod(Bytes, Resolved.MovementGoal.bHasGoal);
    WriteVector(Bytes, Resolved.DesiredFacing);
    WritePod(Bytes, Resolved.SpeedLimitCmps);
    WritePod(Bytes, Resolved.AllowedNavLayerMask);
    WritePod(Bytes, Resolved.bMovementLocked);
    WritePod(Bytes, Resolved.bHasInteraction);
    if (Resolved.bHasInteraction)
    {
      WriteContributionKey(
        Bytes, Resolved.Interaction.Key);
      WritePod(Bytes, static_cast<uint8>(
        Resolved.Interaction.BlendMode));
      WritePod(Bytes, Resolved.Interaction.IntentTypeId);
      WriteRef(Bytes, Resolved.Interaction.TargetRef);
      WritePod(Bytes, Resolved.Interaction.PayloadTypeId);
      WritePod(Bytes, Resolved.Interaction.PayloadKey);
    }
    WritePod(
      Bytes, static_cast<uint8>(Resolved.Business.Num()));
    for (const FCrowdBusinessContribution& Business :
      Resolved.Business)
    {
      WriteContributionKey(Bytes, Business.Key);
      WritePod(Bytes, static_cast<uint8>(
        Business.BlendMode));
      WritePod(Bytes, Business.AdapterId);
      WritePod(Bytes, Business.ExclusiveGroup);
      WritePod(Bytes, Business.CommitId);
      WriteRef(Bytes, Business.InstigatorRef);
      WriteRef(Bytes, Business.TargetRef);
      WritePod(Bytes, Business.PayloadTypeId);
      WritePod(Bytes, Business.Quantity);
    }
    WritePod(
      Bytes, static_cast<uint8>(Resolved.Presentation.Num()));
    for (const FCrowdPresentationContribution& Presentation :
      Resolved.Presentation)
    {
      WriteContributionKey(Bytes, Presentation.Key);
      WritePod(Bytes, static_cast<uint8>(
        Presentation.BlendMode));
      WritePod(Bytes, Presentation.PropertyId);
      WritePod(Bytes, Presentation.Value);
    }
    WritePod(Bytes, Resolved.MovementHash);
    WritePod(Bytes, Resolved.FacingHash);
    WritePod(Bytes, Resolved.ConstraintHash);
    WritePod(Bytes, Resolved.InteractionHash);
    WritePod(Bytes, Resolved.BusinessHash);
    WritePod(Bytes, Resolved.PresentationHash);
    WritePod(Bytes, Resolved.StableHash);
    WritePod(Bytes, Resolved.bValid);
  }

  bool ReadResolvedChannels(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdResolvedBehaviorChannels& OutResolved)
  {
    OutResolved = {};
    uint8 InteractionBlend = 0;
    if (!ReadVector(
        Bytes, Offset, OutResolved.DesiredVelocity)
      || !ReadVector(
        Bytes, Offset, OutResolved.MovementGoal.Location)
      || !ReadRef(
        Bytes, Offset, OutResolved.MovementGoal.TargetRef)
      || !ReadPod(
        Bytes, Offset,
        OutResolved.MovementGoal.FactRevision)
      || !ReadPod(
        Bytes, Offset,
        OutResolved.MovementGoal.bHasGoal)
      || !ReadVector(
        Bytes, Offset, OutResolved.DesiredFacing)
      || !ReadPod(
        Bytes, Offset, OutResolved.SpeedLimitCmps)
      || !ReadPod(
        Bytes, Offset, OutResolved.AllowedNavLayerMask)
      || !ReadPod(
        Bytes, Offset, OutResolved.bMovementLocked)
      || !ReadPod(
        Bytes, Offset, OutResolved.bHasInteraction))
      return false;
    if (OutResolved.bHasInteraction)
    {
      if (!ReadContributionKey(
          Bytes, Offset, OutResolved.Interaction.Key)
        || !ReadPod(Bytes, Offset, InteractionBlend)
        || !ReadPod(
          Bytes, Offset,
          OutResolved.Interaction.IntentTypeId)
        || !ReadRef(
          Bytes, Offset,
          OutResolved.Interaction.TargetRef)
        || !ReadPod(
          Bytes, Offset,
          OutResolved.Interaction.PayloadTypeId)
        || !ReadPod(
          Bytes, Offset,
          OutResolved.Interaction.PayloadKey))
        return false;
      OutResolved.Interaction.BlendMode =
        static_cast<ECrowdBehaviorBlendMode>(
          InteractionBlend);
    }
    uint8 BusinessCount = 0;
    if (!ReadPod(Bytes, Offset, BusinessCount)
      || BusinessCount
        > CrowdBehavior::MaxContributionsPerChannel)
      return false;
    OutResolved.Business.Reserve(BusinessCount);
    for (uint8 Index = 0; Index < BusinessCount; ++Index)
    {
      FCrowdBusinessContribution& Business =
        OutResolved.Business.AddDefaulted_GetRef();
      uint8 Blend = 0;
      if (!ReadContributionKey(
          Bytes, Offset, Business.Key)
        || !ReadPod(Bytes, Offset, Blend)
        || !ReadPod(Bytes, Offset, Business.AdapterId)
        || !ReadPod(
          Bytes, Offset, Business.ExclusiveGroup)
        || !ReadPod(Bytes, Offset, Business.CommitId)
        || !ReadRef(
          Bytes, Offset, Business.InstigatorRef)
        || !ReadRef(Bytes, Offset, Business.TargetRef)
        || !ReadPod(
          Bytes, Offset, Business.PayloadTypeId)
        || !ReadPod(Bytes, Offset, Business.Quantity))
        return false;
      Business.BlendMode =
        static_cast<ECrowdBehaviorBlendMode>(Blend);
    }
    uint8 PresentationCount = 0;
    if (!ReadPod(Bytes, Offset, PresentationCount)
      || PresentationCount
        > CrowdBehavior::MaxContributionsPerChannel)
      return false;
    OutResolved.Presentation.Reserve(PresentationCount);
    for (uint8 Index = 0;
      Index < PresentationCount; ++Index)
    {
      FCrowdPresentationContribution& Presentation =
        OutResolved.Presentation.AddDefaulted_GetRef();
      uint8 Blend = 0;
      if (!ReadContributionKey(
          Bytes, Offset, Presentation.Key)
        || !ReadPod(Bytes, Offset, Blend)
        || !ReadPod(
          Bytes, Offset, Presentation.PropertyId)
        || !ReadPod(Bytes, Offset, Presentation.Value))
        return false;
      Presentation.BlendMode =
        static_cast<ECrowdBehaviorBlendMode>(Blend);
    }
    return ReadPod(
        Bytes, Offset, OutResolved.MovementHash)
      && ReadPod(Bytes, Offset, OutResolved.FacingHash)
      && ReadPod(
        Bytes, Offset, OutResolved.ConstraintHash)
      && ReadPod(
        Bytes, Offset, OutResolved.InteractionHash)
      && ReadPod(Bytes, Offset, OutResolved.BusinessHash)
      && ReadPod(
        Bytes, Offset, OutResolved.PresentationHash)
      && ReadPod(Bytes, Offset, OutResolved.StableHash)
      && ReadPod(Bytes, Offset, OutResolved.bValid)
      && OutResolved.MovementGoal.IsValid()
      && OutResolved.bValid
      && OutResolved.StableHash != 0;
  }

  bool InitializeBehaviorState(
    const FCrowdWorkerDomainContext& Context,
    const FCrowdStableEntityRef& EntityRef,
    const FCrowdCapabilityProfileRegistry& CapabilityProfiles,
    const FCrowdCapabilityBinding* BootstrapBinding,
    const int64 InitialFixedStep,
    FCrowdWorkerBehaviorState& OutState)
  {
    const FCrowdWorkerDirtyStateRecord* Input =
      Context.EntityStates
      ? Context.EntityStates->Find(
          EntityRef, ECrowdWorkerField::InputSnapshot)
      : nullptr;
    FCrowdWorkerBoundaryKinematicState Kinematic;
    if (!Input
      || !FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
        Input->Payload, Kinematic))
      return false;
    FCrowdCapabilityBinding Binding;
    if (BootstrapBinding)
      Binding = *BootstrapBinding;
    else
      Binding.ProfileKey.Value =
        Kinematic.CapabilityProfileKey;
    FCrowdResolvedCapabilitySet Resolved;
    if (!Binding.IsValid()
      || !CapabilityProfiles.Resolve(Binding, Resolved))
      return false;
    OutState = {};
    OutState.LastFixedStep = FMath::Max<int64>(
      0, InitialFixedStep - 1);
    OutState.LastAbsoluteSimulationTick =
      Context.AbsoluteSimulationTick;
    OutState.BusinessCommitLedgerHash = FnvOffset64;
    OutState.EvaluationContext.EntityRef = EntityRef;
    OutState.EvaluationContext.FixedStepIndex =
      OutState.LastFixedStep;
    OutState.EvaluationContext.Position = Kinematic.Position;
    OutState.EvaluationContext.Velocity = Kinematic.Velocity;
    OutState.EvaluationContext.Facing =
      FRotator(0.0, Kinematic.YawDegrees, 0.0).
        Vector();
    OutState.EvaluationContext.RecalculateStableHash();
    OutState.SourceSet.EntityRef = EntityRef;
    OutState.SourceSet.CapabilityBinding = Binding;
    OutState.SourceSet.Revision = 1;
    OutState.SourceSet.RecalculateStableHash();
    FCrowdBehaviorContributions EmptyContributions;
    if (!FCrowdBehaviorResolver::Resolve(
        EmptyContributions, OutState.ResolvedChannels))
      return false;
    return OutState.IsValid();
  }

  bool RefreshEvaluationKinematics(
    const FCrowdWorkerDomainContext& Context,
    const FCrowdStableEntityRef& EntityRef,
    const int64 FixedStepIndex,
    FCrowdBehaviorEntityEvaluationContext& InOutContext)
  {
    if (!Context.EntityStates) return false;

    FVector Position = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    float YawDegrees = 0.0f;
    if (const FCrowdWorkerDirtyStateRecord* Movement =
        Context.EntityStates->Find(
          EntityRef, ECrowdWorkerField::Movement))
    {
      FCrowdWorkerMovementState State;
      if (!FCrowdWorkerMovementStateCodec::Decode(
          Movement->Payload, State))
        return false;
      Position = State.Position;
      Velocity = State.Velocity;
      YawDegrees = State.YawDegrees;
    }
    else
    {
      const FCrowdWorkerDirtyStateRecord* Input =
        Context.EntityStates->Find(
          EntityRef, ECrowdWorkerField::InputSnapshot);
      FCrowdWorkerBoundaryKinematicState State;
      if (!Input
        || !FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
          Input->Payload, State))
        return false;
      Position = State.Position;
      Velocity = State.Velocity;
      YawDegrees = State.YawDegrees;
    }

    InOutContext.EntityRef = EntityRef;
    InOutContext.FixedStepIndex = FixedStepIndex;
    InOutContext.Position = Position;
    InOutContext.Velocity = Velocity;
    InOutContext.Facing =
      FRotator(0.0f, YawDegrees, 0.0f).Vector();
    InOutContext.RecalculateStableHash();
    return InOutContext.IsValid();
  }
}

using namespace CrowdWorkerLifecycleBehaviorPrivate;

bool FCrowdWorkerLifecycleStateCodec::Encode(
  const FCrowdWorkerLifecycleState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  WriteRef(OutPayload.Bytes, State.EntityRef);
  WritePod(OutPayload.Bytes, State.SourceInputSequence);
  WritePod(OutPayload.Bytes, State.InitialStateHash);
  OutPayload.RecalculateStableHash();
  return OutPayload.Bytes.Num() == EncodedByteCount;
}

bool FCrowdWorkerLifecycleStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerLifecycleState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != EncodedByteCount
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  return ReadRef(Payload.Bytes, Offset, OutState.EntityRef)
    && ReadPod(
      Payload.Bytes, Offset, OutState.SourceInputSequence)
    && ReadPod(
      Payload.Bytes, Offset, OutState.InitialStateHash)
    && Offset == Payload.Bytes.Num()
    && OutState.IsValid();
}

bool FCrowdWorkerBehaviorInputCodec::Encode(
  const FCrowdBehaviorEntityEvaluationContext& Context,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Context.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  WriteRef(OutPayload.Bytes, Context.EntityRef);
  WritePod(OutPayload.Bytes, Context.FixedStepIndex);
  WriteVector(OutPayload.Bytes, Context.Position);
  WriteVector(OutPayload.Bytes, Context.Velocity);
  WriteVector(OutPayload.Bytes, Context.Facing);
  WritePod(
    OutPayload.Bytes,
    static_cast<uint8>(Context.Records.Num()));
  for (const FCrowdBehaviorContextRecord& Record :
    Context.Records)
  {
    WritePod(OutPayload.Bytes, Record.TypeId.Value);
    WritePod(OutPayload.Bytes, Record.SchemaVersion);
    WritePod(OutPayload.Bytes, Record.Size);
    const int32 Offset =
      OutPayload.Bytes.AddUninitialized(Record.Size);
    if (Record.Size > 0)
      FMemory::Memcpy(
        OutPayload.Bytes.GetData() + Offset,
        Record.Bytes,
        Record.Size);
  }
  WritePod(OutPayload.Bytes, Context.StableHash);
  if (OutPayload.Bytes.Num() > MaxEncodedBytes)
  {
    OutPayload = {};
    return false;
  }
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerBehaviorInputCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdBehaviorEntityEvaluationContext& OutContext)
{
  OutContext = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint8 RecordCount = 0;
  if (!ReadRef(Payload.Bytes, Offset, OutContext.EntityRef)
    || !ReadPod(
      Payload.Bytes, Offset, OutContext.FixedStepIndex)
    || !ReadVector(
      Payload.Bytes, Offset, OutContext.Position)
    || !ReadVector(
      Payload.Bytes, Offset, OutContext.Velocity)
    || !ReadVector(
      Payload.Bytes, Offset, OutContext.Facing)
    || !ReadPod(Payload.Bytes, Offset, RecordCount)
    || RecordCount
      > CrowdBehavior::MaxContextRecordsPerEntity)
    return false;
  OutContext.Records.Reserve(RecordCount);
  for (uint8 Index = 0; Index < RecordCount; ++Index)
  {
    FCrowdBehaviorContextRecord& Record =
      OutContext.Records.AddDefaulted_GetRef();
    if (!ReadPod(
        Payload.Bytes, Offset, Record.TypeId.Value)
      || !ReadPod(
        Payload.Bytes, Offset, Record.SchemaVersion)
      || !ReadPod(Payload.Bytes, Offset, Record.Size)
      || Record.Size > CrowdBehavior::MaxContextRecordBytes
      || Offset + Record.Size > Payload.Bytes.Num())
      return false;
    if (Record.Size > 0)
      FMemory::Memcpy(
        Record.Bytes,
        Payload.Bytes.GetData() + Offset,
        Record.Size);
    Offset += Record.Size;
  }
  if (!ReadPod(
      Payload.Bytes, Offset, OutContext.StableHash)
    || Offset != Payload.Bytes.Num()
    || !OutContext.IsValid())
    return false;
  FCrowdBehaviorEntityEvaluationContext Copy = OutContext;
  Copy.RecalculateStableHash();
  return Copy.StableHash == OutContext.StableHash;
}

bool FCrowdWorkerBehaviorBindingInputCodec::Encode(
  const FCrowdBehaviorCapabilityBindingUpdate& Update,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Update.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  WritePod(OutPayload.Bytes, Update.EffectiveFixedStep);
  WriteRef(OutPayload.Bytes, Update.EntityRef);
  WriteBinding(OutPayload.Bytes, Update.Binding);
  WritePod(OutPayload.Bytes, Update.StableHash);
  if (OutPayload.Bytes.Num() > MaxEncodedBytes)
  {
    OutPayload = {};
    return false;
  }
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerBehaviorBindingInputCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdBehaviorCapabilityBindingUpdate& OutUpdate)
{
  OutUpdate = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  return ReadPod(
      Payload.Bytes, Offset, OutUpdate.EffectiveFixedStep)
    && ReadRef(Payload.Bytes, Offset, OutUpdate.EntityRef)
    && ReadBinding(Payload.Bytes, Offset, OutUpdate.Binding)
    && ReadPod(Payload.Bytes, Offset, OutUpdate.StableHash)
    && Offset == Payload.Bytes.Num()
    && OutUpdate.IsValid();
}

bool FCrowdWorkerBehaviorEventCodec::Encode(
  const FCrowdBehaviorSourceEvent& Event,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (Event.Kind >= ECrowdBehaviorSourceEventKind::Count
    || Event.FixedStepIndex < 0
    || !Event.Handle.IsValid()
    || !Event.SourceTypeId.IsValid())
    return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  WritePod(OutPayload.Bytes, static_cast<uint8>(Event.Kind));
  WritePod(OutPayload.Bytes, Event.FixedStepIndex);
  WriteRef(OutPayload.Bytes, Event.Handle.EntityRef);
  WritePod(OutPayload.Bytes, Event.Handle.ControllerId.Value);
  WritePod(OutPayload.Bytes, Event.Handle.SourceSequence);
  WritePod(OutPayload.Bytes, Event.SourceTypeId.Value);
  if (OutPayload.Bytes.Num() > MaxEncodedBytes)
  {
    OutPayload = {};
    return false;
  }
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerBehaviorEventCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdBehaviorSourceEvent& OutEvent)
{
  OutEvent = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint8 Kind = 0;
  if (!ReadPod(Payload.Bytes, Offset, Kind)
    || Kind >= static_cast<uint8>(
      ECrowdBehaviorSourceEventKind::Count)
    || !ReadPod(Payload.Bytes, Offset, OutEvent.FixedStepIndex)
    || !ReadRef(Payload.Bytes, Offset, OutEvent.Handle.EntityRef)
    || !ReadPod(
      Payload.Bytes, Offset, OutEvent.Handle.ControllerId.Value)
    || !ReadPod(
      Payload.Bytes, Offset, OutEvent.Handle.SourceSequence)
    || !ReadPod(
      Payload.Bytes, Offset, OutEvent.SourceTypeId.Value)
    || Offset != Payload.Bytes.Num())
    return false;
  OutEvent.Kind = static_cast<ECrowdBehaviorSourceEventKind>(Kind);
  return OutEvent.FixedStepIndex >= 0
    && OutEvent.Handle.IsValid()
    && OutEvent.SourceTypeId.IsValid();
}

bool FCrowdWorkerBusinessCommitEventCodec::Encode(
  const FCrowdBusinessContribution& Contribution,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Contribution.Key.IsValid()
    || Contribution.BlendMode
      != ECrowdBehaviorBlendMode::RejectOnConflict
    || Contribution.AdapterId == 0
    || Contribution.CommitId == 0
    || !Contribution.InstigatorRef.IsValid()
    || (!Contribution.TargetRef.IsUnset()
      && !Contribution.TargetRef.IsValid())
    || Contribution.PayloadTypeId == 0
    || Contribution.Quantity <= 0)
    return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  WritePod(OutPayload.Bytes, Contribution.Key.Priority);
  WritePod(OutPayload.Bytes, Contribution.Key.SourceTypeId.Value);
  WritePod(OutPayload.Bytes, Contribution.Key.ControllerId.Value);
  WritePod(OutPayload.Bytes, Contribution.Key.SourceSequence);
  WritePod(OutPayload.Bytes,
    static_cast<uint8>(Contribution.BlendMode));
  WritePod(OutPayload.Bytes, Contribution.AdapterId);
  WritePod(OutPayload.Bytes, Contribution.ExclusiveGroup);
  WritePod(OutPayload.Bytes, Contribution.CommitId);
  WriteRef(OutPayload.Bytes, Contribution.InstigatorRef);
  WriteRef(OutPayload.Bytes, Contribution.TargetRef);
  WritePod(OutPayload.Bytes, Contribution.PayloadTypeId);
  WritePod(OutPayload.Bytes, Contribution.Quantity);
  if (OutPayload.Bytes.Num() > MaxEncodedBytes)
  {
    OutPayload = {};
    return false;
  }
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerBusinessCommitEventCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdBusinessContribution& OutContribution)
{
  OutContribution = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint8 BlendMode = 0;
  if (!ReadPod(
      Payload.Bytes, Offset, OutContribution.Key.Priority)
    || !ReadPod(Payload.Bytes, Offset,
      OutContribution.Key.SourceTypeId.Value)
    || !ReadPod(Payload.Bytes, Offset,
      OutContribution.Key.ControllerId.Value)
    || !ReadPod(Payload.Bytes, Offset,
      OutContribution.Key.SourceSequence)
    || !ReadPod(Payload.Bytes, Offset, BlendMode)
    || !ReadPod(Payload.Bytes, Offset, OutContribution.AdapterId)
    || !ReadPod(Payload.Bytes, Offset, OutContribution.ExclusiveGroup)
    || !ReadPod(Payload.Bytes, Offset, OutContribution.CommitId)
    || !ReadRef(Payload.Bytes, Offset, OutContribution.InstigatorRef)
    || !ReadRef(Payload.Bytes, Offset, OutContribution.TargetRef)
    || !ReadPod(Payload.Bytes, Offset, OutContribution.PayloadTypeId)
    || !ReadPod(Payload.Bytes, Offset, OutContribution.Quantity)
    || Offset != Payload.Bytes.Num())
    return false;
  OutContribution.BlendMode =
    static_cast<ECrowdBehaviorBlendMode>(BlendMode);
  FCrowdWorkerPayload Canonical;
  return Encode(OutContribution, Canonical)
    && Canonical == Payload;
}

bool FCrowdWorkerBehaviorStateCodec::Encode(
  const FCrowdWorkerBehaviorState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  TArray<uint8>& Bytes = OutPayload.Bytes;
  WritePod(Bytes, State.LastFixedStep);
  WritePod(Bytes, State.LastAbsoluteSimulationTick);
  WritePod(Bytes, State.LastConsumedCommandInputSequence);
  WritePod(Bytes, State.LastCommandBatchHash);
  WritePod(Bytes, State.BusinessCommitLedgerHash);
  WritePod(Bytes,
    static_cast<uint8>(State.AppliedBusinessCommitIds.Num()));
  for (const uint64 CommitId : State.AppliedBusinessCommitIds)
    WritePod(Bytes, CommitId);
  FCrowdWorkerPayload ContextPayload;
  if (!FCrowdWorkerBehaviorInputCodec::Encode(
      State.EvaluationContext, ContextPayload))
    return false;
  WritePod(
    Bytes, static_cast<uint16>(ContextPayload.Bytes.Num()));
  Bytes.Append(ContextPayload.Bytes);
  WriteRef(Bytes, State.SourceSet.EntityRef);
  WriteBinding(Bytes, State.SourceSet.CapabilityBinding);
  WritePod(Bytes, State.SourceSet.Revision);
  WritePod(
    Bytes, static_cast<uint8>(State.SourceSet.Instances.Num()));
  for (const FCrowdBehaviorSourceInstance& Instance :
    State.SourceSet.Instances)
  {
    WriteRef(Bytes, Instance.Handle.EntityRef);
    WritePod(Bytes, Instance.Handle.ControllerId.Value);
    WritePod(Bytes, Instance.Handle.SourceSequence);
    WritePod(Bytes, Instance.SourceTypeId.Value);
    WritePod(Bytes, Instance.SourceVersion);
    WritePod(Bytes, Instance.Priority);
    WritePod(Bytes, Instance.ExclusiveGroup);
    WritePod(Bytes, Instance.StartFixedStep);
    WritePod(Bytes, Instance.LastUpdateFixedStep);
    WritePod(Bytes, Instance.ExpireFixedStep);
    WritePod(Bytes, static_cast<uint8>(
      Instance.ReplicationPolicy));
    WriteSourcePayload(Bytes, Instance.Payload);
    WriteSourceState(Bytes, Instance.State);
  }
  WritePod(
    Bytes,
    static_cast<uint8>(
      State.SourceSet.ControllerCursors.Num()));
  for (const FCrowdBehaviorControllerCursor& Cursor :
    State.SourceSet.ControllerCursors)
  {
    WritePod(Bytes, Cursor.ControllerId.Value);
    WritePod(Bytes, Cursor.LastCommandSequence);
    WritePod(Bytes, Cursor.LastCommandHash);
  }
  WritePod(Bytes, State.SourceSet.StableHash);
  WriteResolvedChannels(Bytes, State.ResolvedChannels);
  if (Bytes.Num() > MaxEncodedBytes)
  {
    OutPayload = {};
    return false;
  }
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerBehaviorStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerBehaviorState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint16 ContextByteCount = 0;
  uint8 BusinessCommitCount = 0;
  uint8 InstanceCount = 0;
  if (!ReadPod(
      Payload.Bytes, Offset, OutState.LastFixedStep)
    || !ReadPod(
      Payload.Bytes, Offset,
      OutState.LastAbsoluteSimulationTick)
    || !ReadPod(
      Payload.Bytes, Offset,
      OutState.LastConsumedCommandInputSequence)
    || !ReadPod(
      Payload.Bytes, Offset, OutState.LastCommandBatchHash)
    || !ReadPod(
      Payload.Bytes, Offset,
      OutState.BusinessCommitLedgerHash)
    || !ReadPod(
      Payload.Bytes, Offset, BusinessCommitCount)
    || BusinessCommitCount
      > FCrowdWorkerBehaviorState::MaxBusinessCommitIds)
    return false;
  OutState.AppliedBusinessCommitIds.Reserve(
    BusinessCommitCount);
  for (uint8 Index = 0; Index < BusinessCommitCount; ++Index)
  {
    uint64 CommitId = 0;
    if (!ReadPod(Payload.Bytes, Offset, CommitId)
      || CommitId == 0
      || OutState.AppliedBusinessCommitIds.Contains(CommitId))
      return false;
    OutState.AppliedBusinessCommitIds.Add(CommitId);
  }
  if (!ReadPod(Payload.Bytes, Offset, ContextByteCount)
    || ContextByteCount == 0
    || ContextByteCount
      > FCrowdWorkerBehaviorInputCodec::MaxEncodedBytes
    || Offset + ContextByteCount > Payload.Bytes.Num())
    return false;
  FCrowdWorkerPayload ContextPayload;
  ContextPayload.SchemaId =
    FCrowdWorkerBehaviorInputCodec::SchemaId;
  ContextPayload.SchemaVersion =
    FCrowdWorkerBehaviorInputCodec::SchemaVersion;
  ContextPayload.Bytes.Append(
    Payload.Bytes.GetData() + Offset, ContextByteCount);
  ContextPayload.RecalculateStableHash();
  Offset += ContextByteCount;
  if (!FCrowdWorkerBehaviorInputCodec::Decode(
      ContextPayload, OutState.EvaluationContext)
    || !ReadRef(
      Payload.Bytes, Offset, OutState.SourceSet.EntityRef)
    || !ReadBinding(
      Payload.Bytes, Offset,
      OutState.SourceSet.CapabilityBinding)
    || !ReadPod(
      Payload.Bytes, Offset, OutState.SourceSet.Revision)
    || !ReadPod(Payload.Bytes, Offset, InstanceCount)
    || InstanceCount > CrowdBehavior::MaxSourcesPerEntity)
    return false;
  OutState.SourceSet.Instances.Reserve(InstanceCount);
  for (uint8 Index = 0; Index < InstanceCount; ++Index)
  {
    FCrowdBehaviorSourceInstance& Instance =
      OutState.SourceSet.Instances.AddDefaulted_GetRef();
    uint8 ReplicationPolicy = 0;
    if (!ReadRef(
        Payload.Bytes, Offset, Instance.Handle.EntityRef)
      || !ReadPod(
        Payload.Bytes, Offset,
        Instance.Handle.ControllerId.Value)
      || !ReadPod(
        Payload.Bytes, Offset,
        Instance.Handle.SourceSequence)
      || !ReadPod(
        Payload.Bytes, Offset, Instance.SourceTypeId.Value)
      || !ReadPod(
        Payload.Bytes, Offset, Instance.SourceVersion)
      || !ReadPod(Payload.Bytes, Offset, Instance.Priority)
      || !ReadPod(
        Payload.Bytes, Offset, Instance.ExclusiveGroup)
      || !ReadPod(
        Payload.Bytes, Offset, Instance.StartFixedStep)
      || !ReadPod(
        Payload.Bytes, Offset, Instance.LastUpdateFixedStep)
      || !ReadPod(
        Payload.Bytes, Offset, Instance.ExpireFixedStep)
      || !ReadPod(
        Payload.Bytes, Offset, ReplicationPolicy)
      || !ReadSourcePayload(
        Payload.Bytes, Offset, Instance.Payload)
      || !ReadSourceState(
        Payload.Bytes, Offset, Instance.State))
      return false;
    Instance.ReplicationPolicy =
      static_cast<ECrowdBehaviorSourceReplicationPolicy>(
        ReplicationPolicy);
  }
  uint8 CursorCount = 0;
  if (!ReadPod(Payload.Bytes, Offset, CursorCount)
    || CursorCount > CrowdBehavior::MaxControllersPerEntity)
    return false;
  OutState.SourceSet.ControllerCursors.Reserve(CursorCount);
  for (uint8 Index = 0; Index < CursorCount; ++Index)
  {
    FCrowdBehaviorControllerCursor& Cursor =
      OutState.SourceSet.ControllerCursors.
        AddDefaulted_GetRef();
    if (!ReadPod(
        Payload.Bytes, Offset, Cursor.ControllerId.Value)
      || !ReadPod(
        Payload.Bytes, Offset, Cursor.LastCommandSequence)
      || !ReadPod(
        Payload.Bytes, Offset, Cursor.LastCommandHash))
      return false;
  }
  if (!ReadPod(
      Payload.Bytes, Offset, OutState.SourceSet.StableHash)
    || !ReadResolvedChannels(
      Payload.Bytes, Offset, OutState.ResolvedChannels)
    || Offset != Payload.Bytes.Num())
    return false;
  return OutState.IsValid();
}

bool FCrowdWorkerLifecycleDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  if (!Context.EntityStates) return false;
  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (Work.Key.Domain != GetDomainId()
      || Work.Key.Kind != ECrowdWorkerWorkKind::Entity)
      return false;
    const FCrowdWorkerDirtyStateRecord* Input =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity,
        ECrowdWorkerField::InputSnapshot);
    if (!Input)
    {
      // Despawn already invalidated every state owned by this lifecycle.
      continue;
    }
    FCrowdWorkerLifecycleState State;
    State.EntityRef = Work.Key.PrimaryEntity;
    State.SourceInputSequence =
      Input->SourceInputSequence;
    State.InitialStateHash = Input->Payload.StableHash;
    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = State.EntityRef;
    Dirty.Field = ECrowdWorkerField::Lifecycle;
    Dirty.Generation = Context.Generation;
    Dirty.WorkerEpoch = Context.WorkerEpoch;
    Dirty.StateRevision = State.SourceInputSequence;
    Dirty.SourceInputSequence =
      State.SourceInputSequence;
    if (!FCrowdWorkerLifecycleStateCodec::Encode(
        State, Dirty.Payload))
      return false;
    OutOutput.DirtyStates.Add(MoveTemp(Dirty));
  }
  return true;
}

FCrowdWorkerBehaviorDomainExecutor::
FCrowdWorkerBehaviorDomainExecutor(
  const FCrowdCapabilityProfileRegistry& InCapabilityProfiles,
  const FCrowdBehaviorSourceEvaluatorRegistry& InEvaluators)
  : CapabilityProfiles(InCapabilityProfiles)
  , Evaluators(InEvaluators)
{
}

bool FCrowdWorkerBehaviorDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  auto Reject = [&Context](
    const TCHAR* Stage,
    const FCrowdStableEntityRef& EntityRef,
    const int32 CommandCount,
    const uint32 ProfileKey,
    const uint32 SourceType,
    const uint32 CommandSequence)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerBehaviorDomainRejected stage=%s entity=%u:%llu:%u tick=%llu epoch=%llu input=%llu commands=%d profile=%u source_type=%u command_sequence=%u"),
      Stage,
      EntityRef.ProviderId,
      EntityRef.StableEntityId,
      EntityRef.LifecycleSerial,
      Context.AbsoluteSimulationTick,
      Context.WorkerEpoch,
      Context.LastAppliedInputSequence,
      CommandCount,
      ProfileKey,
      SourceType,
      CommandSequence);
    return false;
  };
  if (!Context.EntityStates || !Context.Commands
    || !CapabilityProfiles.IsFrozen()
    || !Evaluators.IsFrozen()
    || !FMath::IsFinite(Context.FixedDeltaSeconds)
    || Context.FixedDeltaSeconds <= 0.0)
    return Reject(
      TEXT("context"), {}, 0, 0, 0, 0);
  int32 BoundaryFixedStep = INDEX_NONE;
  int32 BoundaryPlanRevision = INDEX_NONE;
  if (Context.Resources)
  {
    if (const FCrowdWorkerResourceRecord* BoundaryResource =
      Context.Resources->FindCurrent(
        FCrowdWorkerBoundaryStateCodec::SnapshotResourceId))
    {
      if (!FCrowdWorkerBoundaryStateCodec::DecodeSnapshotResource(
          BoundaryResource->Payload,
          BoundaryFixedStep, BoundaryPlanRevision))
        return Reject(
          TEXT("boundary_clock"), {}, 0, 0, 0, 0);
    }
  }
  // The bootstrap boundary resource remains in the versioned resource store
  // during autonomous epochs. It is a state baseline, not a clock ceiling.
  // Ordered Clock intents own forward progress after bootstrap.
  const int64 EpochBehaviorInputCeiling = FMath::Max<int64>(
    BoundaryFixedStep,
    static_cast<int64>(Context.AbsoluteSimulationTick));
  const int64 EpochBehaviorFixedStep = FMath::Max<int64>(
    BoundaryFixedStep,
    Context.AbsoluteSimulationTick > 0
      ? static_cast<int64>(Context.AbsoluteSimulationTick - 1)
      : 0);
  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (Work.Key.Domain != GetDomainId()
      || (Work.Key.Kind != ECrowdWorkerWorkKind::Entity
        && Work.Key.Kind != ECrowdWorkerWorkKind::Timer))
      return Reject(
        TEXT("work"), Work.Key.PrimaryEntity,
        0, 0, 0, 0);
    const FCrowdStableEntityRef EntityRef =
      Work.Key.PrimaryEntity;
    if (!Context.EntityStates->Contains(EntityRef))
      continue;

    TArray<FCrowdWorkerCommandRecord> DueRecords;
    Context.Commands->CollectEntity(
      EntityRef,
      Context.SimulationTimeSeconds
        + Context.FixedDeltaSeconds * 0.5,
      DueRecords);

    FCrowdWorkerBehaviorState Current;
    bool bHadState = false;
    uint64 CurrentBehaviorStateRevision = 0;
    if (const FCrowdWorkerDirtyStateRecord* Existing =
      Context.EntityStates->Find(
        EntityRef, ECrowdWorkerField::Behavior))
    {
      if (!FCrowdWorkerBehaviorStateCodec::Decode(
          Existing->Payload, Current)
        || Current.SourceSet.EntityRef != EntityRef
        || Current.LastAbsoluteSimulationTick
          > Context.AbsoluteSimulationTick)
        return Reject(
          TEXT("decode_state"), EntityRef,
          0, 0, 0, 0);
      bHadState = true;
      CurrentBehaviorStateRevision = Existing->StateRevision;
    }
    else
    {
      TArray<FCrowdBehaviorCapabilityBindingUpdate>
        BootstrapUpdates;
      for (const FCrowdWorkerCommandRecord& Record : DueRecords)
      {
        if (Record.CommandId
            != FCrowdWorkerBehaviorBindingInputCodec::SchemaId)
          continue;
        FCrowdBehaviorCapabilityBindingUpdate Update;
        if (!FCrowdWorkerBehaviorBindingInputCodec::Decode(
              Record.Payload, Update)
          || Update.EntityRef != EntityRef
          || Update.EffectiveFixedStep
            > EpochBehaviorInputCeiling)
          return Reject(
            TEXT("bootstrap_binding"), EntityRef,
            DueRecords.Num(), 0, 0, 0);
        BootstrapUpdates.Add(MoveTemp(Update));
      }
      BootstrapUpdates.Sort([](
        const FCrowdBehaviorCapabilityBindingUpdate& A,
        const FCrowdBehaviorCapabilityBindingUpdate& B)
      {
        if (A.EffectiveFixedStep != B.EffectiveFixedStep)
          return A.EffectiveFixedStep < B.EffectiveFixedStep;
        return A.StableHash < B.StableHash;
      });
      for (int32 Index = 1; Index < BootstrapUpdates.Num(); ++Index)
      {
        if (BootstrapUpdates[Index - 1].EffectiveFixedStep
            == BootstrapUpdates[Index].EffectiveFixedStep
          && BootstrapUpdates[Index - 1].StableHash
            != BootstrapUpdates[Index].StableHash)
          return Reject(
            TEXT("conflicting_bootstrap_binding"), EntityRef,
            DueRecords.Num(), 0, 0, 0);
      }
      const FCrowdCapabilityBinding* BootstrapBinding =
        BootstrapUpdates.IsEmpty()
          ? nullptr : &BootstrapUpdates.Last().Binding;
      if (!InitializeBehaviorState(
          Context, EntityRef, CapabilityProfiles,
          BootstrapBinding, EpochBehaviorFixedStep, Current))
        return Reject(
          TEXT("initialize"), EntityRef,
          DueRecords.Num(),
          BootstrapBinding
            ? BootstrapBinding->ProfileKey.Value : 0,
          0, 0);
    }
    TArray<FCrowdBehaviorSourceCommand> Commands;
    Commands.Reserve(DueRecords.Num());
    TArray<FCrowdBehaviorCapabilityBindingUpdate>
      BindingUpdates;
    BindingUpdates.Reserve(DueRecords.Num());
    FCrowdBehaviorEntityEvaluationContext
      EvaluationContext = Current.EvaluationContext;
    bool bReceivedEvaluationContext = false;
    uint64 LastConsumed =
      Current.LastConsumedCommandInputSequence;
    for (const FCrowdWorkerCommandRecord& Record :
      DueRecords)
    {
      if (Record.InputSequence <= LastConsumed)
        return Reject(
          TEXT("stale_command_input"), EntityRef,
          DueRecords.Num(),
          Current.SourceSet.CapabilityBinding.ProfileKey.Value,
          0, 0);
      if (Record.CommandId
          == FCrowdWorkerBehaviorInputCodec::SchemaId)
      {
        FCrowdBehaviorEntityEvaluationContext
          InputContext;
        if (!FCrowdWorkerBehaviorInputCodec::Decode(
            Record.Payload, InputContext)
          || InputContext.EntityRef != EntityRef
          || InputContext.FixedStepIndex
            > EpochBehaviorInputCeiling)
          return Reject(
            TEXT("decode_context"), EntityRef,
            DueRecords.Num(),
            Current.SourceSet.CapabilityBinding.
              ProfileKey.Value,
            0, 0);
        EvaluationContext = MoveTemp(InputContext);
        bReceivedEvaluationContext = true;
        LastConsumed = Record.InputSequence;
        continue;
      }
      if (Record.CommandId
          == FCrowdWorkerBehaviorBindingInputCodec::SchemaId)
      {
        FCrowdBehaviorCapabilityBindingUpdate Update;
        if (!FCrowdWorkerBehaviorBindingInputCodec::Decode(
            Record.Payload, Update)
          || Update.EntityRef != EntityRef
          || Update.EffectiveFixedStep
            > EpochBehaviorInputCeiling)
          return Reject(
            TEXT("decode_binding"), EntityRef,
            DueRecords.Num(),
            Current.SourceSet.CapabilityBinding.ProfileKey.Value,
            0, 0);
        BindingUpdates.Add(MoveTemp(Update));
        LastConsumed = Record.InputSequence;
        continue;
      }
      FCrowdBehaviorSourceCommand Command;
      if (Record.CommandId
          != FCrowdWorkerBoundaryStateCodec::
            BehaviorCommandSchemaId
        || !FCrowdWorkerBoundaryStateCodec::
          DecodeBehaviorCommand(Record.Payload, Command)
        || Command.Handle.EntityRef != EntityRef)
        return Reject(
          TEXT("decode_command"), EntityRef,
          DueRecords.Num(),
          Current.SourceSet.CapabilityBinding.ProfileKey.Value,
          Command.SourceTypeId.Value,
          Command.CommandSequence);
      Commands.Add(MoveTemp(Command));
      LastConsumed = Record.InputSequence;
    }

    const uint64 ElapsedAbsoluteTicks =
      Context.AbsoluteSimulationTick
        - Current.LastAbsoluteSimulationTick;
    int64 BehaviorFixedStep = Current.LastFixedStep
      + static_cast<int64>(ElapsedAbsoluteTicks);
    BehaviorFixedStep = FMath::Max(
      BehaviorFixedStep, EpochBehaviorFixedStep);
    if (bReceivedEvaluationContext)
      BehaviorFixedStep = EvaluationContext.FixedStepIndex;
    for (const FCrowdBehaviorSourceCommand& Command : Commands)
      BehaviorFixedStep = FMath::Max(
        BehaviorFixedStep, Command.EffectiveFixedStep);
    for (const FCrowdBehaviorCapabilityBindingUpdate& Update
      : BindingUpdates)
      BehaviorFixedStep = FMath::Max(
        BehaviorFixedStep, Update.EffectiveFixedStep);
    if (BehaviorFixedStep < Current.LastFixedStep)
      return Reject(
        TEXT("fixed_step_regression"), EntityRef,
        Commands.Num(),
        Current.SourceSet.CapabilityBinding.ProfileKey.Value,
        0, 0);
    if (!RefreshEvaluationKinematics(
        Context, EntityRef, BehaviorFixedStep,
        EvaluationContext))
      return Reject(
        TEXT("refresh_evaluation_kinematics"), EntityRef,
        Commands.Num(),
        Current.SourceSet.CapabilityBinding.ProfileKey.Value,
        0, 0);

    FCrowdBehaviorSourceSet ApplyBase = Current.SourceSet;
    BindingUpdates.Sort([](
      const FCrowdBehaviorCapabilityBindingUpdate& A,
      const FCrowdBehaviorCapabilityBindingUpdate& B)
    {
      if (A.EffectiveFixedStep != B.EffectiveFixedStep)
        return A.EffectiveFixedStep < B.EffectiveFixedStep;
      return A.StableHash < B.StableHash;
    });
    for (int32 Index = 1; Index < BindingUpdates.Num(); ++Index)
    {
      if (BindingUpdates[Index - 1].EffectiveFixedStep
          == BindingUpdates[Index].EffectiveFixedStep
        && BindingUpdates[Index - 1].StableHash
          != BindingUpdates[Index].StableHash)
        return Reject(
          TEXT("conflicting_binding"), EntityRef,
          DueRecords.Num(),
          Current.SourceSet.CapabilityBinding.ProfileKey.Value,
          0, 0);
    }
    bool bBindingChanged = false;
    for (const FCrowdBehaviorCapabilityBindingUpdate& Update
      : BindingUpdates)
    {
      if (!AreBindingsEqual(
          ApplyBase.CapabilityBinding, Update.Binding))
      {
        ApplyBase.CapabilityBinding = Update.Binding;
        bBindingChanged = true;
      }
    }
    if (bBindingChanged)
      ApplyBase.RecalculateStableHash();
    FCrowdResolvedCapabilitySet Capabilities;
    if (!CapabilityProfiles.Resolve(
      ApplyBase.CapabilityBinding, Capabilities))
      return Reject(
        TEXT("resolve_capabilities"), EntityRef,
        Commands.Num(),
        ApplyBase.CapabilityBinding.ProfileKey.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].SourceTypeId.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].CommandSequence);
    TArray<FCrowdBehaviorSourceCommand> ApplyCommands;
    ApplyCommands.Reserve(Commands.Num());
    bool bConsumedExpiredStop = false;
    for (const FCrowdBehaviorSourceCommand& Command : Commands)
    {
      FCrowdBehaviorSourceInstance* MatchingInstance =
        ApplyBase.Instances.FindByPredicate(
          [&Command](
            const FCrowdBehaviorSourceInstance& Instance)
          {
            return Instance.Handle == Command.Handle;
          });
      const bool bEarlierSameHandle =
        ApplyCommands.ContainsByPredicate(
          [&Command](
            const FCrowdBehaviorSourceCommand& Earlier)
          {
            return Earlier.Handle == Command.Handle;
          });
      if (Command.Kind
          != ECrowdBehaviorSourceCommandKind::Stop
        || MatchingInstance
        || bEarlierSameHandle)
      {
        ApplyCommands.Add(Command);
        continue;
      }
      FCrowdBehaviorControllerCursor* Cursor =
        ApplyBase.ControllerCursors.FindByPredicate(
          [&Command](
            const FCrowdBehaviorControllerCursor& Candidate)
          {
            return Candidate.ControllerId
              == Command.Handle.ControllerId;
          });
      const FCrowdBehaviorSourceSpec* Spec =
        Evaluators.GetSpecs().Find(Command.SourceTypeId);
      if (!Cursor
        || Cursor->LastCommandSequence == MAX_uint32
        || Command.CommandSequence
          != Cursor->LastCommandSequence + 1
        || !Spec
        || Spec->PayloadSchemaId
          != Command.Payload.SchemaId
        || !Capabilities.ContainsAll(MakeArrayView(
          Spec->RequiredCapabilities,
          static_cast<int32>(
            Spec->RequiredCapabilityCount))))
      {
        ApplyCommands.Add(Command);
        continue;
      }
      Cursor->LastCommandSequence = Command.CommandSequence;
      Cursor->LastCommandHash = Command.CalculateStableHash();
      ++ApplyBase.Revision;
      if (ApplyBase.Revision == 0)
        ApplyBase.Revision = 1;
      ApplyBase.RecalculateStableHash();
      bConsumedExpiredStop = true;
    }

    FCrowdBehaviorSourceSet Staged;
    TArray<FCrowdBehaviorSourceEvent> Events;
    uint64 CommandBatchHash = 0;
    if (!FCrowdBehaviorSourceStateMachine::Apply(
        ApplyBase,
        ApplyCommands,
        BehaviorFixedStep,
        Evaluators.GetSpecs(),
        Capabilities,
        Staged,
        Events,
        CommandBatchHash))
    {
      uint32 CursorSequence = 0;
      uint64 CursorHash = 0;
      uint32 ExistingSourceType = 0;
      if (!Commands.IsEmpty())
      {
        for (const FCrowdBehaviorControllerCursor& Cursor :
          Current.SourceSet.ControllerCursors)
        {
          if (Cursor.ControllerId
              == Commands[0].Handle.ControllerId)
          {
            CursorSequence = Cursor.LastCommandSequence;
            CursorHash = Cursor.LastCommandHash;
            break;
          }
        }
        for (const FCrowdBehaviorSourceInstance& Instance :
          Current.SourceSet.Instances)
        {
          if (Instance.Handle == Commands[0].Handle)
          {
            ExistingSourceType = Instance.SourceTypeId.Value;
            break;
          }
        }
      }
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerBehaviorStateMachineContext entity=%u:%llu:%u instances=%d cursors=%d first_kind=%u first_effective_step=%lld first_controller=%u first_source_sequence=%u cursor_sequence=%u cursor_hash=%llu existing_source_type=%u first_source_type=%u first_command_hash=%llu"),
        EntityRef.ProviderId,
        EntityRef.StableEntityId,
        EntityRef.LifecycleSerial,
        Current.SourceSet.Instances.Num(),
        Current.SourceSet.ControllerCursors.Num(),
        Commands.IsEmpty()
          ? MAX_uint8
          : static_cast<uint8>(Commands[0].Kind),
        Commands.IsEmpty()
          ? static_cast<int64>(INDEX_NONE)
          : Commands[0].EffectiveFixedStep,
        Commands.IsEmpty()
          ? 0 : Commands[0].Handle.ControllerId.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].Handle.SourceSequence,
        CursorSequence,
        CursorHash,
        ExistingSourceType,
        Commands.IsEmpty()
          ? 0 : Commands[0].SourceTypeId.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].CalculateStableHash());
      return Reject(
        TEXT("state_machine"), EntityRef,
        Commands.Num(),
        Current.SourceSet.CapabilityBinding.ProfileKey.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].SourceTypeId.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].CommandSequence);
    }
    if (bConsumedExpiredStop)
      CommandBatchHash = CalculateCommandBatchHash(Commands);

    FCrowdBehaviorContributions Contributions;
    bool bSourceStateChanged = false;
    for (FCrowdBehaviorSourceInstance& Instance :
      Staged.Instances)
    {
      const FCrowdBehaviorSourceSpec* Spec =
        Evaluators.FindSpec(Instance.SourceTypeId);
      const TSharedPtr<
        const ICrowdBehaviorSourceEvaluator,
        ESPMode::ThreadSafe> Evaluator =
          Evaluators.FindEvaluator(Instance.SourceTypeId);
      if (!Spec || !Evaluator.IsValid())
        return Reject(
          TEXT("evaluator_lookup"), EntityRef,
          Commands.Num(),
          Current.SourceSet.CapabilityBinding.
            ProfileKey.Value,
          Instance.SourceTypeId.Value, 0);
      FCrowdBehaviorSourceEvaluationContext
        SourceContext;
      SourceContext.FixedStepIndex =
        BehaviorFixedStep;
      SourceContext.Position = EvaluationContext.Position;
      SourceContext.Velocity = EvaluationContext.Velocity;
      SourceContext.Facing = EvaluationContext.Facing;
      SourceContext.Capabilities = Capabilities;
      SourceContext.Instance = Instance;
      SourceContext.ContextRecords =
        EvaluationContext.Records;
      FCrowdBehaviorContributionWriter Writer(
        *Spec, Instance, Contributions);
      if (!Evaluator->Evaluate(SourceContext, Writer)
        || !Writer.Succeeded())
      {
        const FCrowdBehaviorContextRecord* FirstRecord =
          EvaluationContext.Records.IsEmpty()
            ? nullptr : &EvaluationContext.Records[0];
        FCrowdStableEntityRef ContextSubject;
        FCrowdStableEntityRef SourceSubject;
        if (FirstRecord
          && FirstRecord->Size >= sizeof(FCrowdStableEntityRef))
          FMemory::Memcpy(
            &ContextSubject, FirstRecord->Bytes,
            sizeof(FCrowdStableEntityRef));
        if (Instance.Payload.Size
          >= sizeof(FCrowdStableEntityRef))
          FMemory::Memcpy(
            &SourceSubject, Instance.Payload.Bytes,
            sizeof(FCrowdStableEntityRef));
        UE_LOG(LogTemp, Error,
          TEXT("CrowdWorkerBehaviorEvaluateContext entity=%u:%llu:%u tick=%llu context_step=%lld context_hash=%llu records=%d first_type=%u first_version=%u first_size=%u context_subject=%u:%llu:%u source_type=%u payload_schema=%u payload_size=%u payload_hash=%llu source_subject=%u:%llu:%u"),
          EntityRef.ProviderId,
          EntityRef.StableEntityId,
          EntityRef.LifecycleSerial,
          Context.AbsoluteSimulationTick,
          EvaluationContext.FixedStepIndex,
          EvaluationContext.StableHash,
          EvaluationContext.Records.Num(),
          FirstRecord ? FirstRecord->TypeId.Value : 0,
          FirstRecord ? FirstRecord->SchemaVersion : 0,
          FirstRecord ? FirstRecord->Size : 0,
          ContextSubject.ProviderId,
          ContextSubject.StableEntityId,
          ContextSubject.LifecycleSerial,
          Instance.SourceTypeId.Value,
          Instance.Payload.SchemaId,
          Instance.Payload.Size,
          Instance.Payload.CalculateStableHash(),
          SourceSubject.ProviderId,
          SourceSubject.StableEntityId,
          SourceSubject.LifecycleSerial);
        return Reject(
          TEXT("evaluate"), EntityRef,
          Commands.Num(),
          Current.SourceSet.CapabilityBinding.
            ProfileKey.Value,
          Instance.SourceTypeId.Value, 0);
      }
      if (Writer.HasNextState()
        && !(Writer.GetNextState() == Instance.State))
      {
        Instance.State = Writer.GetNextState();
        bSourceStateChanged = true;
      }
    }
    if (bSourceStateChanged)
    {
      if (Staged.Revision == Current.SourceSet.Revision)
      {
        ++Staged.Revision;
        if (Staged.Revision == 0)
          Staged.Revision = 1;
      }
      Staged.RecalculateStableHash();
    }
    if (bBindingChanged
      && Staged.Revision == Current.SourceSet.Revision)
    {
      ++Staged.Revision;
      if (Staged.Revision == 0)
        Staged.Revision = 1;
      Staged.RecalculateStableHash();
    }
    FCrowdResolvedBehaviorChannels ResolvedChannels;
    if (!FCrowdBehaviorResolver::Resolve(
        Contributions, ResolvedChannels))
      return Reject(
        TEXT("resolve_channels"), EntityRef,
        Commands.Num(),
        Current.SourceSet.CapabilityBinding.ProfileKey.Value,
        0, 0);

    FCrowdWorkerBehaviorState Next = Current;
    Next.LastFixedStep = BehaviorFixedStep;
    Next.LastAbsoluteSimulationTick =
      Context.AbsoluteSimulationTick;
    Next.EvaluationContext = MoveTemp(EvaluationContext);
    Next.SourceSet = MoveTemp(Staged);
    Next.ResolvedChannels = MoveTemp(ResolvedChannels);
    TArray<FCrowdBusinessContribution> NewBusinessCommits;
    for (const FCrowdBusinessContribution& Contribution
      : Next.ResolvedChannels.Business)
    {
      if (Next.AppliedBusinessCommitIds.Contains(
          Contribution.CommitId))
        continue;
      if (Next.AppliedBusinessCommitIds.Num()
          >= FCrowdWorkerBehaviorState::MaxBusinessCommitIds)
        return Reject(
          TEXT("business_ledger_capacity"), EntityRef,
          Commands.Num(),
          Next.SourceSet.CapabilityBinding.ProfileKey.Value,
          Contribution.Key.SourceTypeId.Value, 0);
      Next.AppliedBusinessCommitIds.Add(Contribution.CommitId);
      Fold(Next.BusinessCommitLedgerHash, Contribution.CommitId);
      NewBusinessCommits.Add(Contribution);
    }
    if (!DueRecords.IsEmpty())
    {
      Next.LastConsumedCommandInputSequence = LastConsumed;
      Next.LastCommandBatchHash = CommandBatchHash;
      for (const FCrowdWorkerCommandRecord& Record :
        DueRecords)
        OutOutput.ConsumedCommandInputSequences.Add(
          Record.InputSequence);
    }

    Events.Sort([](
      const FCrowdBehaviorSourceEvent& A,
      const FCrowdBehaviorSourceEvent& B)
    {
      if (A.FixedStepIndex != B.FixedStepIndex)
        return A.FixedStepIndex < B.FixedStepIndex;
      if (A.Handle != B.Handle)
        return A.Handle < B.Handle;
      if (A.Kind != B.Kind) return A.Kind < B.Kind;
      return A.SourceTypeId < B.SourceTypeId;
    });
    uint64 NextLocalEventSequence =
      Context.NextOrderedEventSequence;
    for (int32 EventIndex = 0;
      EventIndex < Events.Num(); ++EventIndex)
    {
      const FCrowdBehaviorSourceEvent& SourceEvent =
        Events[EventIndex];
      FCrowdWorkerGameplayEvent& Ordered =
        OutOutput.OrderedEvents.AddDefaulted_GetRef();
      Ordered.EntityRef = EntityRef;
      Ordered.Generation = Context.Generation;
      Ordered.WorkerEpoch = Context.WorkerEpoch;
      Ordered.SourceInputSequence =
        Context.LastAppliedInputSequence;
      Ordered.EventSequence = NextLocalEventSequence++;
      Ordered.EventId = CalculateBehaviorEventId(
        SourceEvent, static_cast<uint32>(EventIndex));
      if (NextLocalEventSequence == 0
        || !FCrowdWorkerBehaviorEventCodec::Encode(
          SourceEvent, Ordered.Payload))
        return Reject(
          TEXT("encode_event"), EntityRef,
          Commands.Num(),
          Next.SourceSet.CapabilityBinding.ProfileKey.Value,
          SourceEvent.SourceTypeId.Value, 0);
      Ordered.RecalculateStableHash();
    }
    for (int32 CommitIndex = 0;
      CommitIndex < NewBusinessCommits.Num(); ++CommitIndex)
    {
      const FCrowdBusinessContribution& Contribution =
        NewBusinessCommits[CommitIndex];
      FCrowdWorkerGameplayEvent& Ordered =
        OutOutput.OrderedEvents.AddDefaulted_GetRef();
      Ordered.EntityRef = Contribution.InstigatorRef;
      Ordered.Generation = Context.Generation;
      Ordered.WorkerEpoch = Context.WorkerEpoch;
      Ordered.SourceInputSequence =
        Context.LastAppliedInputSequence;
      Ordered.EventSequence = NextLocalEventSequence++;
      Ordered.EventId = Contribution.CommitId;
      if (NextLocalEventSequence == 0
        || !FCrowdWorkerBusinessCommitEventCodec::Encode(
          Contribution, Ordered.Payload))
        return Reject(
          TEXT("encode_business_commit"), EntityRef,
          Commands.Num(),
          Next.SourceSet.CapabilityBinding.ProfileKey.Value,
          Contribution.Key.SourceTypeId.Value, 0);
      Ordered.RecalculateStableHash();
    }

    FCrowdWorkerPayload NextPayload;
    if (!FCrowdWorkerBehaviorStateCodec::Encode(
        Next, NextPayload))
      return Reject(
        TEXT("encode_state"), EntityRef,
        Commands.Num(),
        Next.SourceSet.CapabilityBinding.ProfileKey.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].SourceTypeId.Value,
        Commands.IsEmpty()
          ? 0 : Commands[0].CommandSequence);
    FCrowdWorkerPayload CurrentPayload;
    const bool bChanged = !bHadState
      || !FCrowdWorkerBehaviorStateCodec::Encode(
        Current, CurrentPayload)
      || CurrentPayload != NextPayload;
    if (bChanged)
    {
      FCrowdWorkerDirtyStateRecord Dirty;
      Dirty.EntityRef = EntityRef;
      Dirty.Field = ECrowdWorkerField::Behavior;
      Dirty.Generation = Context.Generation;
      Dirty.WorkerEpoch = Context.WorkerEpoch;
      Dirty.StateRevision = FMath::Max3<uint64>(
        CurrentBehaviorStateRevision + 1,
        Context.WorkerEpoch,
        Next.LastConsumedCommandInputSequence);
      Dirty.SourceInputSequence =
        Context.LastAppliedInputSequence;
      Dirty.Payload = MoveTemp(NextPayload);
      OutOutput.DirtyStates.Add(MoveTemp(Dirty));
    }

    uint64 NextWakeupTick = MAX_uint64;
    TArray<FCrowdWorkerCommandRecord> FutureRecords;
    Context.Commands->CollectEntity(
      EntityRef, TNumericLimits<double>::Max(),
      FutureRecords);
    for (const FCrowdWorkerCommandRecord& Record :
      FutureRecords)
    {
      if (Record.EffectiveSimulationTimeSeconds
        > Context.SimulationTimeSeconds)
      {
        const uint64 Tick = static_cast<uint64>(
          FMath::CeilToDouble(
            Record.EffectiveSimulationTimeSeconds
              / Context.FixedDeltaSeconds));
        NextWakeupTick = FMath::Min(
          NextWakeupTick, FMath::Max<uint64>(
            Context.AbsoluteSimulationTick + 1, Tick));
      }
    }
    for (const FCrowdBehaviorSourceInstance& Instance :
      Next.SourceSet.Instances)
    {
      if (Instance.ExpireFixedStep != INDEX_NONE
        && Instance.ExpireFixedStep
          > BehaviorFixedStep)
      {
        const uint64 RemainingTicks = static_cast<uint64>(
          Instance.ExpireFixedStep - BehaviorFixedStep);
        NextWakeupTick = FMath::Min(
          NextWakeupTick,
          Context.AbsoluteSimulationTick + RemainingTicks);
      }
    }
    if (NextWakeupTick != MAX_uint64)
    {
      FCrowdWorkerWakeup Wakeup;
      Wakeup.Key.Domain = GetDomainId();
      Wakeup.Key.EntityRef = EntityRef;
      Wakeup.Key.WakeupId = BehaviorWakeupId;
      Wakeup.AbsoluteSimulationTick = NextWakeupTick;
      Wakeup.Revision = FMath::Max<uint64>(
        1, Next.SourceSet.Revision);
      Wakeup.Priority = ECrowdWorkerWorkPriority::High;
      Wakeup.ReasonMask = 1ull << 12;
      OutOutput.Wakeups.Add(MoveTemp(Wakeup));
    }
  }
  return true;
}

bool FCrowdWorkerBehaviorAuthority::ResetQuiescent(
  const uint64 Generation,
  const ECrowdWorkerBehaviorAuthorityMode InMode,
  const TConstArrayView<FCrowdStableEntityRef> InCanaryEntities,
  const int32 InMaxPendingExpectations)
{
  CurrentEntities.Reset();
  BoundEntities.Reset();
  CanaryEntities.Reset();
  Expectations.Reset();
  PendingBehaviorEvents.Reset();
  PendingBusinessCommits.Reset();
  MatchedEventBatches.Reset();
  Metrics = {};
  Mode = InMode;
  MaxPendingExpectations = InMaxPendingExpectations;
  LastIngestedBehaviorEventSequence = 0;
  bInitialized = Generation != 0
    && InMaxPendingExpectations > 0;
  if (!bInitialized) return false;
  Metrics.Generation = Generation;
  for (const FCrowdStableEntityRef& EntityRef : InCanaryEntities)
  {
    if (!EntityRef.IsValid()
      || CanaryEntities.Contains(EntityRef))
    {
      LatchViolation();
      return false;
    }
    CanaryEntities.Add(EntityRef);
  }
  if (Mode == ECrowdWorkerBehaviorAuthorityMode::Canary
    && CanaryEntities.IsEmpty())
  {
    LatchViolation();
    return false;
  }
  Metrics.CanaryEntityCount = CanaryEntities.Num();
  return true;
}

bool FCrowdWorkerBehaviorAuthority::UpdateCurrentEntities(
  const uint64 Generation,
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs)
{
  if (!bInitialized || Metrics.bViolation
    || Generation != Metrics.Generation)
    return false;
  TSet<FCrowdStableEntityRef> Candidate;
  for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
  {
    if (!EntityRef.IsValid() || Candidate.Contains(EntityRef))
    {
      LatchViolation();
      return false;
    }
    Candidate.Add(EntityRef);
  }
  for (const FCrowdStableEntityRef& Canary : CanaryEntities)
  {
    if (!Candidate.Contains(Canary))
    {
      LatchViolation();
      return false;
    }
  }
  CurrentEntities = MoveTemp(Candidate);
  for (auto It = BoundEntities.CreateIterator(); It; ++It)
  {
    if (!CurrentEntities.Contains(*It))
      It.RemoveCurrent();
  }
  return true;
}

bool FCrowdWorkerBehaviorAuthority::
GetEntitiesRequiringInitialBinding(
  const uint64 Generation,
  TArray<FCrowdStableEntityRef>& OutEntityRefs) const
{
  OutEntityRefs.Reset();
  if (!bInitialized || Metrics.bViolation
    || Generation != Metrics.Generation)
    return false;
  for (const FCrowdStableEntityRef& EntityRef : CurrentEntities)
  {
    if (!BoundEntities.Contains(EntityRef))
      OutEntityRefs.Add(EntityRef);
  }
  OutEntityRefs.Sort();
  return true;
}

bool FCrowdWorkerBehaviorAuthority::MarkSubmittedBindings(
  const uint64 Generation,
  const TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
    BindingUpdates)
{
  if (!bInitialized || Metrics.bViolation
    || Generation != Metrics.Generation)
    return false;
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update :
    BindingUpdates)
  {
    if (!Update.IsValid()
      || !CurrentEntities.Contains(Update.EntityRef))
    {
      LatchViolation();
      return false;
    }
  }
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update :
    BindingUpdates)
    BoundEntities.Add(Update.EntityRef);
  return true;
}

bool FCrowdWorkerBehaviorAuthority::QueueCommittedExpectation(
  const uint64 Generation,
  const uint64 InputSequence,
  const FCrowdBehaviorSourceRuntime& Runtime,
  const TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
    CommittedContexts,
  const bool bRequireKinematicParity,
  const bool bRequireSourceStateParity)
{
  if (!bInitialized || Metrics.bViolation
    || Generation != Metrics.Generation || InputSequence == 0
    || Expectations.Num() >= MaxPendingExpectations
    || (!Expectations.IsEmpty()
      && InputSequence <= Expectations.Last().InputSequence))
  {
    LatchViolation();
    return false;
  }
  TMap<FCrowdStableEntityRef,
    const FCrowdBehaviorEntityEvaluationContext*> LatestContexts;
  for (const FCrowdBehaviorEntityEvaluationContext& Context :
    CommittedContexts)
  {
    if (!Context.IsValid()
      || !CurrentEntities.Contains(Context.EntityRef))
    {
      LatchViolation();
      return false;
    }
    const FCrowdBehaviorEntityEvaluationContext* const* Previous =
      LatestContexts.Find(Context.EntityRef);
    if (Previous
      && (*Previous)->FixedStepIndex >= Context.FixedStepIndex)
    {
      LatchViolation();
      return false;
    }
    LatestContexts.Add(Context.EntityRef, &Context);
  }
  if (LatestContexts.IsEmpty()) return true;

  FExpectation Expectation;
  Expectation.InputSequence = InputSequence;
  Expectation.bRequireKinematicParity = bRequireKinematicParity;
  Expectation.bRequireSourceStateParity =
    bRequireSourceStateParity;
  TArray<FCrowdStableEntityRef> SortedRefs;
  LatestContexts.GetKeys(SortedRefs);
  SortedRefs.Sort();
  for (const FCrowdStableEntityRef& EntityRef : SortedRefs)
  {
    const FCrowdBehaviorSourceSet* SourceSet =
      Runtime.FindSourceSet(EntityRef);
    const FCrowdResolvedBehaviorChannels* Resolved =
      Runtime.FindResolvedChannels(EntityRef);
    const FCrowdBehaviorEntityEvaluationContext* const* Context =
      LatestContexts.Find(EntityRef);
    if (!SourceSet || !Resolved || !Resolved->bValid
      || !Context || !*Context)
    {
      LatchViolation();
      return false;
    }
    FExpectedEntity& Entity =
      Expectation.Entities.AddDefaulted_GetRef();
    Entity.EntityRef = EntityRef;
    Entity.SourceSetHash = SourceSet->StableHash;
    Entity.SourceSetContentHash =
      CalculateSourceSetContentHash(*SourceSet);
    Entity.SourceSetControlHash =
      CalculateSourceSetControlHash(*SourceSet);
    CalculateSourceSetTraceHashes(
      *SourceSet,
      Entity.SourceStateTraceHash,
      Entity.SourceTimelineTraceHash,
      Entity.SourceCursorTraceHash);
    Entity.SourceSetRevision = SourceSet->Revision;
    Entity.SourceInstanceCount = SourceSet->Instances.Num();
    Entity.ResolvedChannelsHash = Resolved->StableHash;
    Entity.EvaluationContextHash = (*Context)->StableHash;
    Entity.EvaluationPosition = (*Context)->Position;
    Entity.EvaluationVelocity = (*Context)->Velocity;
    Entity.FirstContextRecordHash = (*Context)->Records.IsEmpty()
      ? 0 : (*Context)->Records[0].CalculateStableHash();
    Entity.EvaluationContextRecordCount = (*Context)->Records.Num();
  }
  Expectations.Add(MoveTemp(Expectation));
  ++Metrics.QueuedExpectationCount;
  Metrics.PendingExpectationCount = Expectations.Num();
  return true;
}

bool FCrowdWorkerBehaviorAuthority::QueuePreparedExpectation(
  const uint64 Generation,
  const uint64 InputSequence,
  const FCrowdBehaviorPreparedBoundary& Prepared,
  const TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
    StagedContexts,
  const bool bRequireKinematicParity,
  const bool bRequireSourceParity)
{
  if (!bInitialized || Metrics.bViolation
    || Generation != Metrics.Generation || InputSequence == 0
    || !Prepared.bValid
    || Expectations.Num() >= MaxPendingExpectations
    || (!Expectations.IsEmpty()
      && InputSequence <= Expectations.Last().InputSequence))
  {
    LatchViolation();
    return false;
  }
  TMap<FCrowdStableEntityRef,
    const FCrowdBehaviorEntityEvaluationContext*> ContextByRef;
  for (const FCrowdBehaviorEntityEvaluationContext& Context :
    StagedContexts)
  {
    if (!Context.IsValid()
      || Context.FixedStepIndex != Prepared.FixedStepIndex
      || !CurrentEntities.Contains(Context.EntityRef)
      || ContextByRef.Contains(Context.EntityRef))
    {
      LatchViolation();
      return false;
    }
    ContextByRef.Add(Context.EntityRef, &Context);
  }
  FExpectation Expectation;
  Expectation.InputSequence = InputSequence;
  Expectation.bRequireKinematicParity = bRequireKinematicParity;
  Expectation.bRequireSourceParity = bRequireSourceParity;
  Expectation.Entities.Reserve(StagedContexts.Num());
  for (const FCrowdBehaviorPreparedEntity& PreparedEntity :
    Prepared.Entities)
  {
    const FCrowdBehaviorEntityEvaluationContext* const* Context =
      ContextByRef.Find(PreparedEntity.EntityRef);
    if (!CurrentEntities.Contains(PreparedEntity.EntityRef)
      || (Context && *Context
        && PreparedEntity.EvaluationContextHash
          != (*Context)->StableHash))
    {
      LatchViolation();
      return false;
    }
    if (!Context) continue;
    FExpectedEntity& Entity =
      Expectation.Entities.AddDefaulted_GetRef();
    Entity.EntityRef = PreparedEntity.EntityRef;
    Entity.SourceSetHash =
      PreparedEntity.StagedSourceSet.StableHash;
    Entity.SourceSetContentHash = CalculateSourceSetContentHash(
      PreparedEntity.StagedSourceSet);
    CalculateSourceSetTraceHashes(
      PreparedEntity.StagedSourceSet,
      Entity.SourceStateTraceHash,
      Entity.SourceTimelineTraceHash,
      Entity.SourceCursorTraceHash);
    Entity.SourceSetRevision =
      PreparedEntity.StagedSourceSet.Revision;
    Entity.SourceInstanceCount =
      PreparedEntity.StagedSourceSet.Instances.Num();
    Entity.ResolvedChannelsHash =
      PreparedEntity.ResolvedChannels.StableHash;
    Entity.EvaluationContextHash =
      PreparedEntity.EvaluationContextHash;
  }
  if (Expectation.Entities.Num() != ContextByRef.Num())
  {
    LatchViolation();
    return false;
  }
  Expectations.Add(MoveTemp(Expectation));
  ++Metrics.QueuedExpectationCount;
  Metrics.PendingExpectationCount = Expectations.Num();
  return true;
}

bool FCrowdWorkerBehaviorAuthority::IngestOrderedEvents(
  const TConstArrayView<FCrowdWorkerGameplayEvent> Events)
{
  if (!bInitialized || Metrics.bViolation)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerBehaviorAuthorityIngestReject reason=authority_state initialized=%d violation=%d generation=%llu events=%d"),
      bInitialized ? 1 : 0,
      Metrics.bViolation ? 1 : 0,
      Metrics.Generation,
      Events.Num());
    return false;
  }
  if (Mode != ECrowdWorkerBehaviorAuthorityMode::Production)
    return true;
  for (const FCrowdWorkerGameplayEvent& Ordered : Events)
  {
    const bool bSourceEvent = Ordered.Payload.SchemaId
      == FCrowdWorkerBehaviorEventCodec::SchemaId;
    const bool bBusinessCommit = Ordered.Payload.SchemaId
      == FCrowdWorkerBusinessCommitEventCodec::SchemaId;
    if (!bSourceEvent && !bBusinessCommit)
      continue;
    if (Ordered.Generation != Metrics.Generation
      || Ordered.SourceInputSequence == 0
      || Ordered.EventSequence
        <= LastIngestedBehaviorEventSequence
      || !CurrentEntities.Contains(Ordered.EntityRef)
      || PendingBehaviorEvents.Num()
          + PendingBusinessCommits.Num() >= 64000)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerBehaviorAuthorityIngestReject reason=admission event_generation=%llu authority_generation=%llu source_input=%llu event_sequence=%llu previous_event_sequence=%llu entity=%u:%llu:%u entity_current=%d pending_source=%d pending_business=%d schema=%u"),
        Ordered.Generation,
        Metrics.Generation,
        Ordered.SourceInputSequence,
        Ordered.EventSequence,
        LastIngestedBehaviorEventSequence,
        Ordered.EntityRef.ProviderId,
        Ordered.EntityRef.StableEntityId,
        Ordered.EntityRef.LifecycleSerial,
        CurrentEntities.Contains(Ordered.EntityRef) ? 1 : 0,
        PendingBehaviorEvents.Num(),
        PendingBusinessCommits.Num(),
        Ordered.Payload.SchemaId);
      LatchViolation();
      return false;
    }
    if (bSourceEvent)
    {
      FCrowdBehaviorSourceEvent SourceEvent;
      if (!FCrowdWorkerBehaviorEventCodec::Decode(
          Ordered.Payload, SourceEvent)
        || Ordered.EntityRef != SourceEvent.Handle.EntityRef)
      {
        UE_LOG(LogTemp, Error,
          TEXT("CrowdWorkerBehaviorAuthorityIngestReject reason=source_decode event_sequence=%llu entity=%u:%llu:%u"),
          Ordered.EventSequence,
          Ordered.EntityRef.ProviderId,
          Ordered.EntityRef.StableEntityId,
          Ordered.EntityRef.LifecycleSerial);
        LatchViolation();
        return false;
      }
      FPendingBehaviorEvent& Pending =
        PendingBehaviorEvents.AddDefaulted_GetRef();
      Pending.SourceInputSequence = Ordered.SourceInputSequence;
      Pending.EventSequence = Ordered.EventSequence;
      Pending.Event = MoveTemp(SourceEvent);
    }
    else
    {
      FCrowdBusinessContribution Contribution;
      if (!FCrowdWorkerBusinessCommitEventCodec::Decode(
          Ordered.Payload, Contribution)
        || Ordered.EntityRef != Contribution.InstigatorRef)
      {
        UE_LOG(LogTemp, Error,
          TEXT("CrowdWorkerBehaviorAuthorityIngestReject reason=business_decode event_sequence=%llu entity=%u:%llu:%u"),
          Ordered.EventSequence,
          Ordered.EntityRef.ProviderId,
          Ordered.EntityRef.StableEntityId,
          Ordered.EntityRef.LifecycleSerial);
        LatchViolation();
        return false;
      }
      FPendingBusinessCommit& Pending =
        PendingBusinessCommits.AddDefaulted_GetRef();
      Pending.SourceInputSequence = Ordered.SourceInputSequence;
      Pending.EventSequence = Ordered.EventSequence;
      Pending.Contribution = MoveTemp(Contribution);
    }
    LastIngestedBehaviorEventSequence = Ordered.EventSequence;
    ++Metrics.IngestedOrderedEventCount;
  }
  return true;
}

bool FCrowdWorkerBehaviorAuthority::QueueAutonomousExpectation(
  const uint64 Generation,
  const uint64 InputSequence,
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs,
  const bool bCaptureEvents)
{
  if (!bInitialized || Metrics.bViolation
    || Mode != ECrowdWorkerBehaviorAuthorityMode::Production
    || Generation != Metrics.Generation || InputSequence == 0
    || Expectations.Num() >= MaxPendingExpectations
    || (!Expectations.IsEmpty()
      && InputSequence <= Expectations.Last().InputSequence))
  {
    LatchViolation();
    return false;
  }
  FExpectation Expectation;
  Expectation.InputSequence = InputSequence;
  Expectation.bRequireContent = false;
  Expectation.bCaptureEvents = bCaptureEvents;
  FCrowdStableEntityRef PreviousRef;
  for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
  {
    if (!EntityRef.IsValid()
      || (!PreviousRef.IsUnset() && !(PreviousRef < EntityRef))
      || !CurrentEntities.Contains(EntityRef))
    {
      LatchViolation();
      return false;
    }
    FExpectedEntity& Entity =
      Expectation.Entities.AddDefaulted_GetRef();
    Entity.EntityRef = EntityRef;
    PreviousRef = EntityRef;
  }
  Expectations.Add(MoveTemp(Expectation));
  ++Metrics.QueuedExpectationCount;
  Metrics.PendingExpectationCount = Expectations.Num();
  return true;
}

bool FCrowdWorkerBehaviorAuthority::PeekMatchedEvents(
  const uint64 InputSequence,
  TArray<FCrowdBehaviorSourceEvent>& OutEvents,
  TArray<FCrowdBusinessContribution>& OutBusinessCommits) const
{
  OutEvents.Reset();
  OutBusinessCommits.Reset();
  if (!bInitialized || Metrics.bViolation
    || Mode != ECrowdWorkerBehaviorAuthorityMode::Production
    || MatchedEventBatches.IsEmpty()
    || MatchedEventBatches[0].InputSequence != InputSequence)
    return false;
  OutEvents = MatchedEventBatches[0].Events;
  OutBusinessCommits = MatchedEventBatches[0].BusinessCommits;
  return true;
}

bool FCrowdWorkerBehaviorAuthority::AcknowledgeMatchedEvents(
  const uint64 InputSequence)
{
  if (!bInitialized || Metrics.bViolation
    || Mode != ECrowdWorkerBehaviorAuthorityMode::Production
    || MatchedEventBatches.IsEmpty()
    || MatchedEventBatches[0].InputSequence != InputSequence)
    return false;
  Metrics.ConsumedOrderedEventCount +=
    MatchedEventBatches[0].Events.Num()
      + MatchedEventBatches[0].BusinessCommits.Num();
  MatchedEventBatches.RemoveAt(0, 1, EAllowShrinking::No);
  return true;
}

ECrowdWorkerBehaviorValidationResult
FCrowdWorkerBehaviorAuthority::ValidateAvailable(
  const FCrowdWorkerResultApplyProxy& Proxy)
{
  if (!bInitialized || Metrics.bViolation)
    return ECrowdWorkerBehaviorValidationResult::Violation;
  if (Expectations.IsEmpty())
    return ECrowdWorkerBehaviorValidationResult::NoExpectation;
  const FExpectation& Expectation = Expectations[0];
  if (Proxy.GetMetrics().LastAppliedInputSequence
      < Expectation.InputSequence)
    return ECrowdWorkerBehaviorValidationResult::Pending;
  for (const FExpectedEntity& Expected : Expectation.Entities)
  {
    const FCrowdWorkerDomainProxyState* State = Proxy.FindDomain(
      Expected.EntityRef, ECrowdWorkerField::Behavior);
    if (!State)
      return ECrowdWorkerBehaviorValidationResult::Pending;
    if (Expectation.bRequireContent
      && State->SourceInputSequence < Expectation.InputSequence)
      return ECrowdWorkerBehaviorValidationResult::Pending;
    if (Expectation.bRequireContent
      && State->SourceInputSequence != Expectation.InputSequence)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerBehaviorAuthorityMismatch reason=input_sequence entity=%u:%llu:%u expected_input=%llu actual_input=%llu"),
        Expected.EntityRef.ProviderId,
        Expected.EntityRef.StableEntityId,
        Expected.EntityRef.LifecycleSerial,
        Expectation.InputSequence,
        State->SourceInputSequence);
      LatchViolation();
      return ECrowdWorkerBehaviorValidationResult::Violation;
    }
    FCrowdWorkerBehaviorState Actual;
    const bool bDecoded = FCrowdWorkerBehaviorStateCodec::Decode(
      State->State.Payload, Actual);
    if (!bDecoded
      || Actual.SourceSet.EntityRef != Expected.EntityRef)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerBehaviorAuthorityMismatch reason=decode entity=%u:%llu:%u input=%llu"),
        Expected.EntityRef.ProviderId,
        Expected.EntityRef.StableEntityId,
        Expected.EntityRef.LifecycleSerial,
        Expectation.InputSequence);
      LatchViolation();
      return ECrowdWorkerBehaviorValidationResult::Violation;
    }
    if (!Expectation.bRequireContent) continue;
    const bool bExactSource = bDecoded
      && Actual.SourceSet.StableHash == Expected.SourceSetHash;
    const bool bProductionEquivalentSource = bDecoded
      && Mode == ECrowdWorkerBehaviorAuthorityMode::Production
      && Actual.SourceSet.Revision >= Expected.SourceSetRevision
      && CalculateSourceSetContentHash(Actual.SourceSet)
        == Expected.SourceSetContentHash;
    const bool bPredictedControlEquivalentSource = bDecoded
      && !Expectation.bRequireSourceStateParity
      && CalculateSourceSetControlHash(Actual.SourceSet)
        == Expected.SourceSetControlHash;
    if ((Expectation.bRequireSourceParity
        && !bExactSource && !bProductionEquivalentSource
        && !bPredictedControlEquivalentSource)
      || (Expectation.bRequireKinematicParity
        && (Actual.ResolvedChannels.StableHash
            != Expected.ResolvedChannelsHash
          || Actual.EvaluationContext.StableHash
            != Expected.EvaluationContextHash)))
    {
      uint64 ActualStateTrace = 0;
      uint64 ActualTimelineTrace = 0;
      uint64 ActualCursorTrace = 0;
      CalculateSourceSetTraceHashes(
        Actual.SourceSet,
        ActualStateTrace,
        ActualTimelineTrace,
        ActualCursorTrace);
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerBehaviorAuthorityMismatch reason=state entity=%u:%llu:%u input=%llu expected_source=%llu actual_source=%llu expected_content=%llu actual_content=%llu expected_revision=%u actual_revision=%u expected_instances=%d actual_instances=%d expected_state_trace=%llu actual_state_trace=%llu expected_timeline_trace=%llu actual_timeline_trace=%llu expected_cursor_trace=%llu actual_cursor_trace=%llu expected_resolved=%llu actual_resolved=%llu expected_context=%llu actual_context=%llu"),
        Expected.EntityRef.ProviderId,
        Expected.EntityRef.StableEntityId,
        Expected.EntityRef.LifecycleSerial,
        Expectation.InputSequence,
        Expected.SourceSetHash,
        Actual.SourceSet.StableHash,
        Expected.SourceSetContentHash,
        CalculateSourceSetContentHash(Actual.SourceSet),
        Expected.SourceSetRevision,
        Actual.SourceSet.Revision,
        Expected.SourceInstanceCount,
        Actual.SourceSet.Instances.Num(),
        Expected.SourceStateTraceHash,
        ActualStateTrace,
        Expected.SourceTimelineTraceHash,
        ActualTimelineTrace,
        Expected.SourceCursorTraceHash,
        ActualCursorTrace,
        Expected.ResolvedChannelsHash,
        Actual.ResolvedChannels.StableHash,
        Expected.EvaluationContextHash,
        Actual.EvaluationContext.StableHash);
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerBehaviorAuthorityContextMismatch entity=%u:%llu:%u input=%llu expected_position=(%.3f,%.3f,%.3f) actual_position=(%.3f,%.3f,%.3f) expected_velocity=(%.3f,%.3f,%.3f) actual_velocity=(%.3f,%.3f,%.3f) expected_records=%d actual_records=%d expected_first_record=%llu actual_first_record=%llu"),
        Expected.EntityRef.ProviderId,
        Expected.EntityRef.StableEntityId,
        Expected.EntityRef.LifecycleSerial,
        Expectation.InputSequence,
        Expected.EvaluationPosition.X,
        Expected.EvaluationPosition.Y,
        Expected.EvaluationPosition.Z,
        Actual.EvaluationContext.Position.X,
        Actual.EvaluationContext.Position.Y,
        Actual.EvaluationContext.Position.Z,
        Expected.EvaluationVelocity.X,
        Expected.EvaluationVelocity.Y,
        Expected.EvaluationVelocity.Z,
        Actual.EvaluationContext.Velocity.X,
        Actual.EvaluationContext.Velocity.Y,
        Actual.EvaluationContext.Velocity.Z,
        Expected.EvaluationContextRecordCount,
        Actual.EvaluationContext.Records.Num(),
        Expected.FirstContextRecordHash,
        Actual.EvaluationContext.Records.IsEmpty()
          ? 0
          : Actual.EvaluationContext.Records[0].CalculateStableHash());
      LatchViolation();
      return ECrowdWorkerBehaviorValidationResult::Violation;
    }
  }
  Metrics.LastMatchedInputSequence = Expectation.InputSequence;
  ++Metrics.MatchedExpectationCount;
  if (Mode == ECrowdWorkerBehaviorAuthorityMode::Production
    && Expectation.bCaptureEvents)
  {
    if (MatchedEventBatches.Num() >= MaxPendingExpectations)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerBehaviorAuthorityValidationReject reason=matched_event_capacity input=%llu matched_event_batches=%d capacity=%d pending_source=%d pending_business=%d"),
        Expectation.InputSequence,
        MatchedEventBatches.Num(),
        MaxPendingExpectations,
        PendingBehaviorEvents.Num(),
        PendingBusinessCommits.Num());
      LatchViolation();
      return ECrowdWorkerBehaviorValidationResult::Violation;
    }
    FMatchedEventBatch& Matched =
      MatchedEventBatches.AddDefaulted_GetRef();
    Matched.InputSequence = Expectation.InputSequence;
    int32 ConsumeCount = 0;
    while (ConsumeCount < PendingBehaviorEvents.Num()
      && PendingBehaviorEvents[ConsumeCount].SourceInputSequence
        <= Expectation.InputSequence)
    {
      Matched.Events.Add(
        MoveTemp(PendingBehaviorEvents[ConsumeCount].Event));
      ++ConsumeCount;
    }
    if (ConsumeCount > 0)
      PendingBehaviorEvents.RemoveAt(
        0, ConsumeCount, EAllowShrinking::No);
    int32 BusinessConsumeCount = 0;
    while (BusinessConsumeCount < PendingBusinessCommits.Num()
      && PendingBusinessCommits[BusinessConsumeCount]
        .SourceInputSequence <= Expectation.InputSequence)
    {
      Matched.BusinessCommits.Add(MoveTemp(
        PendingBusinessCommits[BusinessConsumeCount]
          .Contribution));
      ++BusinessConsumeCount;
    }
    if (BusinessConsumeCount > 0)
      PendingBusinessCommits.RemoveAt(
        0, BusinessConsumeCount, EAllowShrinking::No);
  }
  Expectations.RemoveAt(0, 1, EAllowShrinking::No);
  Metrics.PendingExpectationCount = Expectations.Num();
  return ECrowdWorkerBehaviorValidationResult::Matched;
}

bool FCrowdWorkerBehaviorAuthority::IsWorkerOwner(
  const FCrowdStableEntityRef& EntityRef) const
{
  if (!bInitialized || Metrics.bViolation
    || !CurrentEntities.Contains(EntityRef))
    return false;
  return Mode == ECrowdWorkerBehaviorAuthorityMode::Production
    || (Mode == ECrowdWorkerBehaviorAuthorityMode::Canary
      && CanaryEntities.Contains(EntityRef));
}

void FCrowdWorkerBehaviorAuthority::LatchViolation()
{
  ++Metrics.MismatchCount;
  Metrics.bViolation = true;
}
