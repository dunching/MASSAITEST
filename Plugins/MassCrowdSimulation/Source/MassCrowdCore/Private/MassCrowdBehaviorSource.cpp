#include "MassCrowdBehaviorSource.h"

#define FnvOffset64 BehaviorSource_FnvOffset64
#define FnvPrime64 BehaviorSource_FnvPrime64
#define FoldUnsigned BehaviorSource_FoldUnsigned
#define FoldRef BehaviorSource_FoldRef
#define FoldFloat BehaviorSource_FoldFloat
#define FoldVector BehaviorSource_FoldVector
#define IsFiniteVector BehaviorSource_IsFiniteVector

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  template <typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
    {
      Hash ^= static_cast<uint8>(Value >> (ByteIndex * 8));
      Hash *= FnvPrime64;
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldUnsigned(Hash, Ref.ProviderId);
    FoldUnsigned(Hash, Ref.StableEntityId);
    FoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  void FoldFloat(uint64& Hash, const float Value)
  {
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    FoldUnsigned(Hash, Bits);
  }

  void FoldVector(uint64& Hash, const FVector& Value)
  {
    FoldFloat(Hash, static_cast<float>(Value.X));
    FoldFloat(Hash, static_cast<float>(Value.Y));
    FoldFloat(Hash, static_cast<float>(Value.Z));
  }

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  float QuantizeFloat(const double Value)
  {
    return static_cast<float>(FMath::RoundToDouble(Value * 1000.0) / 1000.0);
  }

  FVector QuantizeVector(const FVector& Value)
  {
    return FVector(
      QuantizeFloat(Value.X),
      QuantizeFloat(Value.Y),
      QuantizeFloat(Value.Z));
  }

  bool IsStrictlySortedCapabilities(
    const TConstArrayView<FCrowdCapabilityId> Values)
  {
    for (int32 Index = 0; Index < Values.Num(); ++Index)
    {
      if (!Values[Index].IsValid()
        || (Index > 0 && !(Values[Index - 1] < Values[Index])))
        return false;
    }
    return true;
  }

  bool ContributionLess(
    const FCrowdBehaviorContributionKey& A,
    const FCrowdBehaviorContributionKey& B)
  {
    if (A.Priority != B.Priority) return A.Priority > B.Priority;
    if (A.SourceTypeId != B.SourceTypeId)
      return A.SourceTypeId < B.SourceTypeId;
    if (A.ControllerId != B.ControllerId)
      return A.ControllerId < B.ControllerId;
    return A.SourceSequence < B.SourceSequence;
  }

  template <typename T>
  void SortContributions(TArray<T>& Values)
  {
    Values.Sort([](const T& A, const T& B)
    {
      return ContributionLess(A.Key, B.Key);
    });
  }

  void FoldContributionKey(
    uint64& Hash, const FCrowdBehaviorContributionKey& Key)
  {
    FoldUnsigned(Hash, static_cast<uint16>(Key.Priority));
    FoldUnsigned(Hash, Key.SourceTypeId.Value);
    FoldUnsigned(Hash, Key.ControllerId.Value);
    FoldUnsigned(Hash, Key.SourceSequence);
  }

  bool ValidateContributionKeys(
    TConstArrayView<FCrowdBehaviorContributionKey> Keys)
  {
    for (const FCrowdBehaviorContributionKey& Key : Keys)
      if (!Key.IsValid()) return false;
    return true;
  }

  FCrowdBehaviorControllerCursor* FindCursor(
    FCrowdBehaviorSourceSet& Set,
    const FCrowdBehaviorControllerId ControllerId)
  {
    for (FCrowdBehaviorControllerCursor& Cursor : Set.ControllerCursors)
      if (Cursor.ControllerId == ControllerId) return &Cursor;
    return nullptr;
  }

  FCrowdBehaviorSourceInstance* FindInstance(
    FCrowdBehaviorSourceSet& Set,
    const FCrowdBehaviorSourceHandle& Handle)
  {
    for (FCrowdBehaviorSourceInstance& Instance : Set.Instances)
      if (Instance.Handle == Handle) return &Instance;
    return nullptr;
  }

  bool HasRequiredCapabilities(
    const FCrowdBehaviorSourceSpec& Spec,
    const FCrowdResolvedCapabilitySet& Capabilities)
  {
    return Capabilities.ContainsAll(MakeArrayView(
      Spec.RequiredCapabilities,
      static_cast<int32>(Spec.RequiredCapabilityCount)));
  }
}

bool FCrowdCapabilityBinding::IsValid() const
{
  if (!ProfileKey.IsValid()
    || ModifierCount > CrowdBehavior::MaxCapabilityModifiers)
    return false;
  FCrowdCapabilityId Previous;
  for (uint8 Index = 0; Index < ModifierCount; ++Index)
  {
    if (!Modifiers[Index].IsValid()) return false;
    if (Index > 0
      && !(Previous < Modifiers[Index].CapabilityId))
      return false;
    Previous = Modifiers[Index].CapabilityId;
  }
  return true;
}

bool FCrowdResolvedCapabilitySet::IsValid() const
{
  return Count <= CrowdBehavior::MaxResolvedCapabilities
    && IsStrictlySortedCapabilities(
      MakeArrayView(CapabilityIds, static_cast<int32>(Count)));
}

bool FCrowdResolvedCapabilitySet::Has(
  const FCrowdCapabilityId CapabilityId) const
{
  if (!CapabilityId.IsValid()) return false;
  int32 Low = 0;
  int32 High = Count - 1;
  while (Low <= High)
  {
    const int32 Middle = Low + (High - Low) / 2;
    if (CapabilityIds[Middle] == CapabilityId) return true;
    if (CapabilityIds[Middle] < CapabilityId) Low = Middle + 1;
    else High = Middle - 1;
  }
  return false;
}

bool FCrowdResolvedCapabilitySet::ContainsAll(
  const TConstArrayView<FCrowdCapabilityId> Required) const
{
  if (!IsValid()) return false;
  for (const FCrowdCapabilityId CapabilityId : Required)
    if (!Has(CapabilityId)) return false;
  return true;
}

uint64 FCrowdResolvedCapabilitySet::CalculateStableHash() const
{
  if (!IsValid()) return 0;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, Count);
  for (uint8 Index = 0; Index < Count; ++Index)
    FoldUnsigned(Hash, CapabilityIds[Index].Value);
  return Hash;
}

bool FCrowdCapabilityProfile::IsValid() const
{
  return Key.IsValid()
    && CapabilityIds.Num() <= CrowdBehavior::MaxCapabilitiesPerProfile
    && IsStrictlySortedCapabilities(CapabilityIds);
}

bool FCrowdCapabilityProfileRegistry::Register(
  FCrowdCapabilityProfile Profile)
{
  if (bFrozen || !Profile.IsValid() || Profiles.Contains(Profile.Key))
    return false;
  Profiles.Add(Profile.Key, MoveTemp(Profile));
  return true;
}

bool FCrowdCapabilityProfileRegistry::Freeze()
{
  if (bFrozen || Profiles.IsEmpty()) return false;
  bFrozen = true;
  return true;
}

bool FCrowdCapabilityProfileRegistry::Resolve(
  const FCrowdCapabilityBinding& Binding,
  FCrowdResolvedCapabilitySet& OutCapabilities) const
{
  OutCapabilities = {};
  if (!bFrozen || !Binding.IsValid()) return false;
  const FCrowdCapabilityProfile* Profile =
    Profiles.Find(Binding.ProfileKey);
  if (!Profile || !Profile->IsValid()) return false;

  TArray<FCrowdCapabilityId> Values = Profile->CapabilityIds;
  for (uint8 Index = 0; Index < Binding.ModifierCount; ++Index)
  {
    const FCrowdCapabilityModifier& Modifier = Binding.Modifiers[Index];
    if (Modifier.Operation == ECrowdCapabilityModifierOperation::Add)
      Values.AddUnique(Modifier.CapabilityId);
    else
      Values.Remove(Modifier.CapabilityId);
  }
  Values.Sort();
  if (Values.Num() > CrowdBehavior::MaxResolvedCapabilities
    || !IsStrictlySortedCapabilities(Values))
    return false;
  OutCapabilities.Count = static_cast<uint8>(Values.Num());
  for (int32 Index = 0; Index < Values.Num(); ++Index)
    OutCapabilities.CapabilityIds[Index] = Values[Index];
  return OutCapabilities.IsValid();
}

uint64 FCrowdCapabilityProfileRegistry::CalculateStableHash() const
{
  if (!bFrozen) return 0;
  TArray<FCrowdCapabilityProfileKey> Keys;
  Profiles.GetKeys(Keys);
  Keys.Sort();
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, static_cast<uint32>(Keys.Num()));
  for (const FCrowdCapabilityProfileKey Key : Keys)
  {
    const FCrowdCapabilityProfile& Profile = Profiles[Key];
    FoldUnsigned(Hash, Key.Value);
    FoldUnsigned(Hash, static_cast<uint32>(Profile.CapabilityIds.Num()));
    for (const FCrowdCapabilityId Id : Profile.CapabilityIds)
      FoldUnsigned(Hash, Id.Value);
  }
  return Hash;
}

bool FCrowdBehaviorSourcePayload::operator==(
  const FCrowdBehaviorSourcePayload& Other) const
{
  return SchemaId == Other.SchemaId
    && Size == Other.Size
    && FMemory::Memcmp(Bytes, Other.Bytes, Size) == 0;
}

uint64 FCrowdBehaviorSourcePayload::CalculateStableHash() const
{
  if (!IsValid()) return 0;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, SchemaId);
  FoldUnsigned(Hash, Size);
  for (uint16 Index = 0; Index < Size; ++Index)
    FoldUnsigned(Hash, Bytes[Index]);
  return Hash;
}

bool FCrowdBehaviorSourceState::operator==(
  const FCrowdBehaviorSourceState& Other) const
{
  return SchemaId == Other.SchemaId
    && Size == Other.Size
    && FMemory::Memcmp(Bytes, Other.Bytes, Size) == 0;
}

uint64 FCrowdBehaviorSourceState::CalculateStableHash() const
{
  if (!IsValid()) return 0;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, SchemaId);
  FoldUnsigned(Hash, Size);
  for (uint16 Index = 0; Index < Size; ++Index)
    FoldUnsigned(Hash, Bytes[Index]);
  return Hash;
}

uint64 FCrowdBehaviorContextRecord::CalculateStableHash() const
{
  if (!IsValid()) return 0;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, TypeId.Value);
  FoldUnsigned(Hash, SchemaVersion);
  FoldUnsigned(Hash, Size);
  for (uint16 Index = 0; Index < Size; ++Index)
    FoldUnsigned(Hash, Bytes[Index]);
  return Hash;
}

bool FCrowdBehaviorSourceSpec::IsValid() const
{
  const uint16 KnownChannelMask =
    (uint16{1} << static_cast<uint8>(ECrowdBehaviorChannel::Count)) - 1;
  if (!TypeId.IsValid()
    || Version == 0
    || ChannelMask == 0
    || (ChannelMask & ~KnownChannelMask) != 0
    || MaxLifetimeSteps < 0
    || PayloadSchemaId == 0
    || ReplicationPolicy
      >= ECrowdBehaviorSourceReplicationPolicy::Count
    || RequiredCapabilityCount
      > CrowdBehavior::MaxRequiredCapabilitiesPerSource)
    return false;
  return IsStrictlySortedCapabilities(MakeArrayView(
    RequiredCapabilities,
    static_cast<int32>(RequiredCapabilityCount)));
}

uint64 FCrowdBehaviorSourceSpec::CalculateStableHash() const
{
  if (!IsValid()) return 0;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, TypeId.Value);
  FoldUnsigned(Hash, Version);
  FoldUnsigned(Hash, ChannelMask);
  FoldUnsigned(Hash, static_cast<uint16>(DefaultPriority));
  FoldUnsigned(Hash, ExclusiveGroup);
  FoldUnsigned(Hash, static_cast<uint32>(MaxLifetimeSteps));
  FoldUnsigned(Hash, PayloadSchemaId);
  FoldUnsigned(Hash, StateSchemaId);
  FoldUnsigned(Hash, static_cast<uint8>(ReplicationPolicy));
  FoldUnsigned(Hash, RequiredCapabilityCount);
  for (uint8 Index = 0; Index < RequiredCapabilityCount; ++Index)
    FoldUnsigned(Hash, RequiredCapabilities[Index].Value);
  return Hash;
}

bool FCrowdBehaviorSourceSpecRegistry::Register(
  const FCrowdBehaviorSourceSpec& Spec)
{
  if (bFrozen || !Spec.IsValid() || Specs.Contains(Spec.TypeId))
    return false;
  Specs.Add(Spec.TypeId, Spec);
  return true;
}

bool FCrowdBehaviorSourceSpecRegistry::Freeze()
{
  if (bFrozen || Specs.IsEmpty()) return false;
  bFrozen = true;
  return true;
}

const FCrowdBehaviorSourceSpec* FCrowdBehaviorSourceSpecRegistry::Find(
  const FCrowdBehaviorSourceTypeId TypeId) const
{
  return TypeId.IsValid() ? Specs.Find(TypeId) : nullptr;
}

uint64 FCrowdBehaviorSourceSpecRegistry::CalculateStableHash() const
{
  if (!bFrozen) return 0;
  TArray<FCrowdBehaviorSourceTypeId> Keys;
  Specs.GetKeys(Keys);
  Keys.Sort();
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, static_cast<uint32>(Keys.Num()));
  for (const FCrowdBehaviorSourceTypeId Key : Keys)
    FoldUnsigned(Hash, Specs[Key].CalculateStableHash());
  return Hash;
}

bool FCrowdBehaviorSourceCommand::IsValid() const
{
  return EffectiveFixedStep >= 0
    && Handle.IsValid()
    && CommandSequence != 0
    && Kind < ECrowdBehaviorSourceCommandKind::Count
    && SourceTypeId.IsValid()
    && LifetimeSteps >= 0
    && Payload.IsValid();
}

uint64 FCrowdBehaviorSourceCommand::CalculateStableHash() const
{
  if (!IsValid()) return 0;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, static_cast<uint64>(EffectiveFixedStep));
  FoldRef(Hash, Handle.EntityRef);
  FoldUnsigned(Hash, Handle.ControllerId.Value);
  FoldUnsigned(Hash, Handle.SourceSequence);
  FoldUnsigned(Hash, CommandSequence);
  FoldUnsigned(Hash, static_cast<uint8>(Kind));
  FoldUnsigned(Hash, SourceTypeId.Value);
  FoldUnsigned(Hash, static_cast<uint16>(Priority));
  FoldUnsigned(Hash, static_cast<uint32>(LifetimeSteps));
  FoldUnsigned(Hash, Payload.CalculateStableHash());
  return Hash;
}

bool FCrowdBehaviorSourceInstance::IsValid() const
{
  return Handle.IsValid()
    && SourceTypeId.IsValid()
    && SourceVersion != 0
    && StartFixedStep >= 0
    && LastUpdateFixedStep >= StartFixedStep
    && (ExpireFixedStep == INDEX_NONE
      || ExpireFixedStep > LastUpdateFixedStep)
    && ReplicationPolicy
      < ECrowdBehaviorSourceReplicationPolicy::Count
    && Payload.IsValid()
    && State.IsValid();
}

uint64 FCrowdBehaviorSourceInstance::CalculateStableHash() const
{
  if (!IsValid()) return 0;
  uint64 Hash = FnvOffset64;
  FoldRef(Hash, Handle.EntityRef);
  FoldUnsigned(Hash, Handle.ControllerId.Value);
  FoldUnsigned(Hash, Handle.SourceSequence);
  FoldUnsigned(Hash, SourceTypeId.Value);
  FoldUnsigned(Hash, SourceVersion);
  FoldUnsigned(Hash, static_cast<uint16>(Priority));
  FoldUnsigned(Hash, ExclusiveGroup);
  FoldUnsigned(Hash, static_cast<uint64>(StartFixedStep));
  FoldUnsigned(Hash, static_cast<uint64>(LastUpdateFixedStep));
  FoldUnsigned(Hash, static_cast<uint64>(ExpireFixedStep));
  FoldUnsigned(Hash, static_cast<uint8>(ReplicationPolicy));
  FoldUnsigned(Hash, Payload.CalculateStableHash());
  FoldUnsigned(Hash, State.CalculateStableHash());
  return Hash;
}

bool FCrowdBehaviorSourceSet::IsValid() const
{
  if (!EntityRef.IsValid()
    || !CapabilityBinding.IsValid()
    || Revision == 0
    || Instances.Num() > CrowdBehavior::MaxSourcesPerEntity
    || ControllerCursors.Num() > CrowdBehavior::MaxControllersPerEntity
    || StableHash == 0)
    return false;
  for (int32 Index = 0; Index < Instances.Num(); ++Index)
  {
    if (!Instances[Index].IsValid()
      || Instances[Index].Handle.EntityRef != EntityRef
      || (Index > 0
        && !(Instances[Index - 1].Handle < Instances[Index].Handle)))
      return false;
  }
  for (int32 Index = 0; Index < ControllerCursors.Num(); ++Index)
  {
    if (!ControllerCursors[Index].IsValid()
      || (Index > 0
        && !(ControllerCursors[Index - 1].ControllerId
          < ControllerCursors[Index].ControllerId)))
      return false;
  }
  FCrowdBehaviorSourceSet Copy = *this;
  Copy.RecalculateStableHash();
  return StableHash == Copy.StableHash;
}

void FCrowdBehaviorSourceSet::RecalculateStableHash()
{
  Instances.Sort([](const auto& A, const auto& B)
  {
    return A.Handle < B.Handle;
  });
  ControllerCursors.Sort([](const auto& A, const auto& B)
  {
    return A.ControllerId < B.ControllerId;
  });
  uint64 Hash = FnvOffset64;
  FoldRef(Hash, EntityRef);
  FoldUnsigned(Hash, CapabilityBinding.ProfileKey.Value);
  FoldUnsigned(Hash, CapabilityBinding.ModifierRevision);
  FoldUnsigned(Hash, CapabilityBinding.ModifierCount);
  for (uint8 Index = 0; Index < CapabilityBinding.ModifierCount; ++Index)
  {
    FoldUnsigned(Hash,
      CapabilityBinding.Modifiers[Index].CapabilityId.Value);
    FoldUnsigned(Hash, static_cast<uint8>(
      CapabilityBinding.Modifiers[Index].Operation));
  }
  FoldUnsigned(Hash, Revision);
  FoldUnsigned(Hash, static_cast<uint32>(Instances.Num()));
  for (const FCrowdBehaviorSourceInstance& Instance : Instances)
    FoldUnsigned(Hash, Instance.CalculateStableHash());
  FoldUnsigned(Hash, static_cast<uint32>(ControllerCursors.Num()));
  for (const FCrowdBehaviorControllerCursor& Cursor : ControllerCursors)
  {
    FoldUnsigned(Hash, Cursor.ControllerId.Value);
    FoldUnsigned(Hash, Cursor.LastCommandSequence);
    FoldUnsigned(Hash, Cursor.LastCommandHash);
  }
  StableHash = Hash;
}

bool FCrowdBehaviorSourceStateMachine::Apply(
  const FCrowdBehaviorSourceSet& Current,
  const TConstArrayView<FCrowdBehaviorSourceCommand> Commands,
  const int64 FixedStepIndex,
  const FCrowdBehaviorSourceSpecRegistry& Specs,
  const FCrowdResolvedCapabilitySet& Capabilities,
  FCrowdBehaviorSourceSet& OutStaged,
  TArray<FCrowdBehaviorSourceEvent>& OutEvents,
  uint64& OutCommandBatchHash)
{
  OutStaged = {};
  OutEvents.Reset();
  OutCommandBatchHash = 0;
  if (FixedStepIndex < 0 || !Specs.IsFrozen()
    || !Capabilities.IsValid() || !Current.IsValid())
    return false;

  TArray<FCrowdBehaviorSourceCommand> Sorted(Commands);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.EffectiveFixedStep != B.EffectiveFixedStep)
      return A.EffectiveFixedStep < B.EffectiveFixedStep;
    if (A.Handle.EntityRef != B.Handle.EntityRef)
      return A.Handle.EntityRef < B.Handle.EntityRef;
    if (A.Handle.ControllerId != B.Handle.ControllerId)
      return A.Handle.ControllerId < B.Handle.ControllerId;
    return A.CommandSequence < B.CommandSequence;
  });

  FCrowdBehaviorSourceSet Candidate = Current;
  TArray<FCrowdBehaviorSourceEvent> CandidateEvents;
  bool bChanged = false;
  uint64 BatchHash = FnvOffset64;
  FoldUnsigned(BatchHash, static_cast<uint32>(Sorted.Num()));
  for (const FCrowdBehaviorSourceCommand& Command : Sorted)
  {
    const uint64 CommandHash = Command.CalculateStableHash();
    if (!Command.IsValid()
      || Command.EffectiveFixedStep > FixedStepIndex
      || Command.Handle.EntityRef != Current.EntityRef
      || CommandHash == 0)
      return false;
    FoldUnsigned(BatchHash, CommandHash);

    FCrowdBehaviorControllerCursor* Cursor =
      FindCursor(Candidate, Command.Handle.ControllerId);
    if (Cursor)
    {
      if (Command.CommandSequence == Cursor->LastCommandSequence
        && CommandHash == Cursor->LastCommandHash)
        continue;
      if (Command.CommandSequence != Cursor->LastCommandSequence + 1)
        return false;
    }
    else
    {
      if (Command.CommandSequence != 1
        || Candidate.ControllerCursors.Num()
          >= CrowdBehavior::MaxControllersPerEntity)
        return false;
      Cursor = &Candidate.ControllerCursors.AddDefaulted_GetRef();
      Cursor->ControllerId = Command.Handle.ControllerId;
    }

    const FCrowdBehaviorSourceSpec* Spec =
      Specs.Find(Command.SourceTypeId);
    if (!Spec
      || Spec->PayloadSchemaId != Command.Payload.SchemaId
      || !HasRequiredCapabilities(*Spec, Capabilities))
      return false;
    FCrowdBehaviorSourceInstance* Instance =
      FindInstance(Candidate, Command.Handle);
    if (Command.Kind == ECrowdBehaviorSourceCommandKind::Start)
    {
      if (Instance
        || Candidate.Instances.Num()
          >= CrowdBehavior::MaxSourcesPerEntity)
        return false;
      const int32 Lifetime = Command.LifetimeSteps > 0
        ? Command.LifetimeSteps : Spec->MaxLifetimeSteps;
      if (Spec->MaxLifetimeSteps > 0
        && (Lifetime <= 0 || Lifetime > Spec->MaxLifetimeSteps))
        return false;
      FCrowdBehaviorSourceInstance& Added =
        Candidate.Instances.AddDefaulted_GetRef();
      Added.Handle = Command.Handle;
      Added.SourceTypeId = Command.SourceTypeId;
      Added.SourceVersion = Spec->Version;
      Added.Priority = Command.Priority != 0
        ? Command.Priority : Spec->DefaultPriority;
      Added.ExclusiveGroup = Spec->ExclusiveGroup;
      Added.StartFixedStep = Command.EffectiveFixedStep;
      Added.LastUpdateFixedStep = Command.EffectiveFixedStep;
      Added.ExpireFixedStep = Lifetime > 0
        ? Command.EffectiveFixedStep + Lifetime : INDEX_NONE;
      Added.ReplicationPolicy = Spec->ReplicationPolicy;
      Added.Payload = Command.Payload;
      Added.State.SchemaId = Spec->StateSchemaId;
      CandidateEvents.Add({ECrowdBehaviorSourceEventKind::Started,
        FixedStepIndex, Added.Handle, Added.SourceTypeId});
    }
    else if (Command.Kind == ECrowdBehaviorSourceCommandKind::Update)
    {
      if (!Instance || Instance->SourceTypeId != Command.SourceTypeId)
        return false;
      const int32 Lifetime = Command.LifetimeSteps > 0
        ? Command.LifetimeSteps : Spec->MaxLifetimeSteps;
      if (Spec->MaxLifetimeSteps > 0
        && (Lifetime <= 0 || Lifetime > Spec->MaxLifetimeSteps))
        return false;
      Instance->LastUpdateFixedStep = Command.EffectiveFixedStep;
      Instance->Priority = Command.Priority != 0
        ? Command.Priority : Spec->DefaultPriority;
      Instance->ExpireFixedStep = Lifetime > 0
        ? Command.EffectiveFixedStep + Lifetime : INDEX_NONE;
      Instance->Payload = Command.Payload;
      CandidateEvents.Add({ECrowdBehaviorSourceEventKind::Updated,
        FixedStepIndex, Instance->Handle, Instance->SourceTypeId});
    }
    else
    {
      if (!Instance || Instance->SourceTypeId != Command.SourceTypeId)
        return false;
      const FCrowdBehaviorSourceHandle Handle = Instance->Handle;
      const FCrowdBehaviorSourceTypeId TypeId = Instance->SourceTypeId;
      Candidate.Instances.RemoveAll([&](const auto& Existing)
      {
        return Existing.Handle == Handle;
      });
      CandidateEvents.Add({ECrowdBehaviorSourceEventKind::Stopped,
        FixedStepIndex, Handle, TypeId});
    }
    Cursor->LastCommandSequence = Command.CommandSequence;
    Cursor->LastCommandHash = CommandHash;
    bChanged = true;
  }

  for (int32 Index = Candidate.Instances.Num() - 1; Index >= 0; --Index)
  {
    const FCrowdBehaviorSourceInstance& Instance =
      Candidate.Instances[Index];
    const FCrowdBehaviorSourceSpec* Spec = Specs.Find(
      Instance.SourceTypeId);
    if (!Spec
      || Instance.SourceVersion != Spec->Version
      || Instance.Payload.SchemaId != Spec->PayloadSchemaId
      || Instance.State.SchemaId != Spec->StateSchemaId)
      return false;
    const bool bExpired = Instance.ExpireFixedStep != INDEX_NONE
      && Instance.ExpireFixedStep <= FixedStepIndex;
    const bool bCapabilityRevoked =
      !HasRequiredCapabilities(*Spec, Capabilities);
    if (!bExpired && !bCapabilityRevoked) continue;
    CandidateEvents.Add({
      bExpired ? ECrowdBehaviorSourceEventKind::Expired
        : ECrowdBehaviorSourceEventKind::CapabilityRevoked,
      FixedStepIndex, Instance.Handle, Instance.SourceTypeId});
    Candidate.Instances.RemoveAt(Index);
    bChanged = true;
  }

  if (bChanged)
  {
    ++Candidate.Revision;
    if (Candidate.Revision == 0) Candidate.Revision = 1;
  }
  Candidate.RecalculateStableHash();
  if (!Candidate.IsValid()) return false;
  OutStaged = MoveTemp(Candidate);
  OutEvents = MoveTemp(CandidateEvents);
  OutCommandBatchHash = BatchHash;
  return true;
}

bool FCrowdBehaviorContributions::IsWithinLimits() const
{
  return Movement.Num() <= CrowdBehavior::MaxContributionsPerChannel
    && Facing.Num() <= CrowdBehavior::MaxContributionsPerChannel
    && Constraints.Num() <= CrowdBehavior::MaxContributionsPerChannel
    && Interactions.Num() <= CrowdBehavior::MaxContributionsPerChannel
    && Business.Num() <= CrowdBehavior::MaxContributionsPerChannel
    && Presentation.Num() <= CrowdBehavior::MaxContributionsPerChannel;
}

bool FCrowdBehaviorResolver::Resolve(
  const FCrowdBehaviorContributions& Contributions,
  FCrowdResolvedBehaviorChannels& OutResolved)
{
  OutResolved = {};
  if (!Contributions.IsWithinLimits()) return false;

  TArray<FCrowdMovementContribution> Movement = Contributions.Movement;
  TArray<FCrowdFacingContribution> Facing = Contributions.Facing;
  TArray<FCrowdConstraintContribution> Constraints =
    Contributions.Constraints;
  TArray<FCrowdInteractionContribution> Interactions =
    Contributions.Interactions;
  TArray<FCrowdBusinessContribution> Business = Contributions.Business;
  TArray<FCrowdPresentationContribution> Presentation =
    Contributions.Presentation;
  SortContributions(Movement);
  SortContributions(Facing);
  SortContributions(Constraints);
  SortContributions(Interactions);
  SortContributions(Business);
  SortContributions(Presentation);

  uint64 MovementHash = FnvOffset64;
  FVector BaseVelocity = FVector::ZeroVector;
  bool bHasOverride = false;
  FVector WeightedVelocity = FVector::ZeroVector;
  int64 TotalWeight = 0;
  FVector AdditiveVelocity = FVector::ZeroVector;
  for (const FCrowdMovementContribution& Value : Movement)
  {
    if (!Value.Key.IsValid()
      || !IsFiniteVector(Value.DesiredVelocity)
      || !Value.Goal.IsValid()
      || (Value.BlendMode != ECrowdBehaviorBlendMode::Override
        && Value.BlendMode != ECrowdBehaviorBlendMode::WeightedAdd
        && Value.BlendMode != ECrowdBehaviorBlendMode::Additive)
      || (Value.BlendMode == ECrowdBehaviorBlendMode::WeightedAdd
        && Value.WeightQ15 == 0)
      || (Value.BlendMode != ECrowdBehaviorBlendMode::Override
        && Value.Goal.bHasGoal))
      return false;
    FoldContributionKey(MovementHash, Value.Key);
    FoldUnsigned(MovementHash, static_cast<uint8>(Value.BlendMode));
    FoldUnsigned(MovementHash, Value.WeightQ15);
    FoldVector(MovementHash, Value.DesiredVelocity);
    FoldUnsigned(MovementHash, static_cast<uint8>(Value.Goal.bHasGoal));
    FoldVector(MovementHash, Value.Goal.Location);
    FoldRef(MovementHash, Value.Goal.TargetRef);
    FoldUnsigned(MovementHash, Value.Goal.FactRevision);
    if (Value.BlendMode == ECrowdBehaviorBlendMode::Override)
    {
      if (!bHasOverride)
      {
        BaseVelocity = Value.DesiredVelocity;
        OutResolved.MovementGoal = Value.Goal;
        bHasOverride = true;
      }
    }
    else if (Value.BlendMode == ECrowdBehaviorBlendMode::WeightedAdd)
    {
      WeightedVelocity += Value.DesiredVelocity * Value.WeightQ15;
      TotalWeight += Value.WeightQ15;
    }
    else
      AdditiveVelocity += Value.DesiredVelocity;
  }
  if (!bHasOverride && TotalWeight > 0)
    BaseVelocity = WeightedVelocity / static_cast<double>(TotalWeight);
  OutResolved.DesiredVelocity =
    QuantizeVector(BaseVelocity + AdditiveVelocity);

  uint64 FacingHash = FnvOffset64;
  FVector FacingBase = FVector::ZeroVector;
  bool bFacingOverride = false;
  FVector WeightedFacing = FVector::ZeroVector;
  int64 FacingWeight = 0;
  for (const FCrowdFacingContribution& Value : Facing)
  {
    if (!Value.Key.IsValid()
      || !IsFiniteVector(Value.DesiredDirection)
      || Value.DesiredDirection.IsNearlyZero()
      || (Value.BlendMode != ECrowdBehaviorBlendMode::Override
        && Value.BlendMode != ECrowdBehaviorBlendMode::WeightedAdd)
      || (Value.BlendMode == ECrowdBehaviorBlendMode::WeightedAdd
        && Value.WeightQ15 == 0))
      return false;
    FoldContributionKey(FacingHash, Value.Key);
    FoldUnsigned(FacingHash, static_cast<uint8>(Value.BlendMode));
    FoldUnsigned(FacingHash, Value.WeightQ15);
    FoldVector(FacingHash, Value.DesiredDirection);
    if (Value.BlendMode == ECrowdBehaviorBlendMode::Override)
    {
      if (!bFacingOverride)
      {
        FacingBase = Value.DesiredDirection;
        bFacingOverride = true;
      }
    }
    else
    {
      WeightedFacing += Value.DesiredDirection * Value.WeightQ15;
      FacingWeight += Value.WeightQ15;
    }
  }
  if (!bFacingOverride && FacingWeight > 0)
    FacingBase = WeightedFacing / static_cast<double>(FacingWeight);
  if (!FacingBase.IsNearlyZero())
    OutResolved.DesiredFacing =
      QuantizeVector(FacingBase.GetSafeNormal());

  uint64 ConstraintHash = FnvOffset64;
  float MinLimit = TNumericLimits<float>::Max();
  float MaxFloor = 0.0f;
  bool bHasMaxFloor = false;
  for (const FCrowdConstraintContribution& Value : Constraints)
  {
    if (!Value.Key.IsValid()
      || !FMath::IsFinite(Value.SpeedLimitCmps)
      || Value.SpeedLimitCmps < 0.0f
      || (Value.BlendMode != ECrowdBehaviorBlendMode::MinLimit
        && Value.BlendMode != ECrowdBehaviorBlendMode::MaxLimit
        && Value.BlendMode != ECrowdBehaviorBlendMode::Override))
      return false;
    FoldContributionKey(ConstraintHash, Value.Key);
    FoldUnsigned(ConstraintHash, static_cast<uint8>(Value.BlendMode));
    FoldFloat(ConstraintHash, Value.SpeedLimitCmps);
    FoldUnsigned(ConstraintHash, Value.AllowedNavLayerMask);
    FoldUnsigned(ConstraintHash, static_cast<uint8>(Value.bLockMovement));
    if (Value.BlendMode == ECrowdBehaviorBlendMode::MinLimit)
      MinLimit = FMath::Min(MinLimit, Value.SpeedLimitCmps);
    else if (Value.BlendMode == ECrowdBehaviorBlendMode::MaxLimit)
    {
      MaxFloor = FMath::Max(MaxFloor, Value.SpeedLimitCmps);
      bHasMaxFloor = true;
    }
    else if (OutResolved.SpeedLimitCmps
      == TNumericLimits<float>::Max())
      OutResolved.SpeedLimitCmps = Value.SpeedLimitCmps;
    OutResolved.AllowedNavLayerMask &= Value.AllowedNavLayerMask;
    OutResolved.bMovementLocked |= Value.bLockMovement;
  }
  if (OutResolved.SpeedLimitCmps == TNumericLimits<float>::Max())
    OutResolved.SpeedLimitCmps = MinLimit;
  if (bHasMaxFloor)
    OutResolved.SpeedLimitCmps = FMath::Max(
      OutResolved.SpeedLimitCmps, MaxFloor);
  if (OutResolved.bMovementLocked)
    OutResolved.DesiredVelocity = FVector::ZeroVector;
  else if (OutResolved.SpeedLimitCmps < TNumericLimits<float>::Max())
    OutResolved.DesiredVelocity =
      OutResolved.DesiredVelocity.GetClampedToMaxSize(
        OutResolved.SpeedLimitCmps);
  if (OutResolved.SpeedLimitCmps < TNumericLimits<float>::Max())
    OutResolved.SpeedLimitCmps =
      QuantizeFloat(OutResolved.SpeedLimitCmps);

  uint64 InteractionHash = FnvOffset64;
  for (const FCrowdInteractionContribution& Value : Interactions)
  {
    if (!Value.Key.IsValid()
      || Value.BlendMode != ECrowdBehaviorBlendMode::Exclusive
      || Value.IntentTypeId == 0
      || (Value.TargetRef.IsUnset() == false
        && !Value.TargetRef.IsValid()))
      return false;
    FoldContributionKey(InteractionHash, Value.Key);
    FoldUnsigned(InteractionHash, Value.IntentTypeId);
    FoldRef(InteractionHash, Value.TargetRef);
    FoldUnsigned(InteractionHash, Value.PayloadTypeId);
    FoldUnsigned(InteractionHash, Value.PayloadKey);
  }
  if (!Interactions.IsEmpty())
  {
    OutResolved.bHasInteraction = true;
    OutResolved.Interaction = Interactions[0];
  }

  uint64 BusinessHash = FnvOffset64;
  TSet<uint64> BusinessConflicts;
  for (const FCrowdBusinessContribution& Value : Business)
  {
    if (!Value.Key.IsValid()
      || Value.BlendMode
        != ECrowdBehaviorBlendMode::RejectOnConflict
      || Value.AdapterId == 0 || Value.CommitId == 0
      || !Value.InstigatorRef.IsValid()
      || (!Value.TargetRef.IsUnset() && !Value.TargetRef.IsValid())
      || Value.PayloadTypeId == 0 || Value.Quantity <= 0)
      return false;
    const uint64 ConflictKey =
      (static_cast<uint64>(Value.AdapterId) << 16)
      | Value.ExclusiveGroup;
    if (Value.ExclusiveGroup != 0
      && BusinessConflicts.Contains(ConflictKey))
      return false;
    BusinessConflicts.Add(ConflictKey);
    FoldContributionKey(BusinessHash, Value.Key);
    FoldUnsigned(BusinessHash, Value.AdapterId);
    FoldUnsigned(BusinessHash, Value.ExclusiveGroup);
    FoldUnsigned(BusinessHash, Value.CommitId);
    FoldRef(BusinessHash, Value.InstigatorRef);
    FoldRef(BusinessHash, Value.TargetRef);
    FoldUnsigned(BusinessHash, Value.PayloadTypeId);
    FoldUnsigned(BusinessHash, static_cast<uint32>(Value.Quantity));
  }
  OutResolved.Business = MoveTemp(Business);

  uint64 PresentationHash = FnvOffset64;
  TSet<uint32> SeenPresentationOverrides;
  for (const FCrowdPresentationContribution& Value : Presentation)
  {
    if (!Value.Key.IsValid()
      || (Value.BlendMode != ECrowdBehaviorBlendMode::Override
        && Value.BlendMode != ECrowdBehaviorBlendMode::Additive)
      || Value.PropertyId == 0)
      return false;
    FoldContributionKey(PresentationHash, Value.Key);
    FoldUnsigned(
      PresentationHash, static_cast<uint8>(Value.BlendMode));
    FoldUnsigned(PresentationHash, Value.PropertyId);
    FoldUnsigned(PresentationHash, Value.Value);
    if (Value.BlendMode == ECrowdBehaviorBlendMode::Additive)
      OutResolved.Presentation.Add(Value);
    else if (!SeenPresentationOverrides.Contains(Value.PropertyId))
    {
      OutResolved.Presentation.Add(Value);
      SeenPresentationOverrides.Add(Value.PropertyId);
    }
  }

  OutResolved.MovementHash = MovementHash;
  OutResolved.FacingHash = FacingHash;
  OutResolved.ConstraintHash = ConstraintHash;
  OutResolved.InteractionHash = InteractionHash;
  OutResolved.BusinessHash = BusinessHash;
  OutResolved.PresentationHash = PresentationHash;
  uint64 StableHash = FnvOffset64;
  FoldUnsigned(StableHash, MovementHash);
  FoldUnsigned(StableHash, FacingHash);
  FoldUnsigned(StableHash, ConstraintHash);
  FoldUnsigned(StableHash, InteractionHash);
  FoldUnsigned(StableHash, BusinessHash);
  FoldUnsigned(StableHash, PresentationHash);
  FoldVector(StableHash, OutResolved.DesiredVelocity);
  FoldUnsigned(
    StableHash,
    static_cast<uint8>(OutResolved.MovementGoal.bHasGoal));
  FoldVector(StableHash, OutResolved.MovementGoal.Location);
  FoldRef(StableHash, OutResolved.MovementGoal.TargetRef);
  FoldUnsigned(StableHash, OutResolved.MovementGoal.FactRevision);
  FoldVector(StableHash, OutResolved.DesiredFacing);
  FoldFloat(StableHash, OutResolved.SpeedLimitCmps);
  FoldUnsigned(StableHash, OutResolved.AllowedNavLayerMask);
  FoldUnsigned(StableHash,
    static_cast<uint8>(OutResolved.bMovementLocked));
  OutResolved.StableHash = StableHash;
  OutResolved.bValid = StableHash != 0;
  return OutResolved.bValid;
}

#undef IsFiniteVector
#undef FoldVector
#undef FoldFloat
#undef FoldRef
#undef FoldUnsigned
#undef FnvPrime64
#undef FnvOffset64
