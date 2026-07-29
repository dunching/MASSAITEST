#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

#include <type_traits>

namespace CrowdBehavior
{
  static constexpr int32 MaxCapabilityModifiers = 8;
  static constexpr int32 MaxCapabilitiesPerProfile = 32;
  static constexpr int32 MaxResolvedCapabilities =
    MaxCapabilitiesPerProfile + MaxCapabilityModifiers;
  static constexpr int32 MaxSourcesPerEntity = 16;
  static constexpr int32 MaxControllersPerEntity = 8;
  static constexpr int32 MaxRequiredCapabilitiesPerSource = 8;
  static constexpr int32 MaxPayloadBytes = 96;
  static constexpr int32 MaxStateBytes = 96;
  static constexpr int32 MaxContextRecordsPerEntity = 8;
  static constexpr int32 MaxContextRecordBytes = 96;
  static constexpr int32 MaxContributionsPerChannel = 32;
  static constexpr uint16 FullQ15Weight = 32767;
}

struct FCrowdBehaviorProviderId
{
  uint32 Value = 0;

  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBehaviorProviderId& Other) const = default;
  bool operator<(const FCrowdBehaviorProviderId& Other) const
  {
    return Value < Other.Value;
  }

  friend uint32 GetTypeHash(const FCrowdBehaviorProviderId& Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdBehaviorContextTypeId
{
  uint32 Value = 0;

  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBehaviorContextTypeId& Other) const = default;
  bool operator<(const FCrowdBehaviorContextTypeId& Other) const
  {
    return Value < Other.Value;
  }

  friend uint32 GetTypeHash(const FCrowdBehaviorContextTypeId& Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdCapabilityId
{
  uint32 Value = 0;

  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdCapabilityId& Other) const = default;
  bool operator<(const FCrowdCapabilityId& Other) const
  {
    return Value < Other.Value;
  }

  friend uint32 GetTypeHash(const FCrowdCapabilityId& Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdCapabilityProfileKey
{
  uint32 Value = 0;

  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdCapabilityProfileKey& Other) const = default;
  bool operator<(const FCrowdCapabilityProfileKey& Other) const
  {
    return Value < Other.Value;
  }

  friend uint32 GetTypeHash(const FCrowdCapabilityProfileKey& Key)
  {
    return ::GetTypeHash(Key.Value);
  }
};

enum class ECrowdCapabilityModifierOperation : uint8
{
  Add = 0,
  Remove,
  Count
};

struct FCrowdCapabilityModifier
{
  FCrowdCapabilityId CapabilityId;
  ECrowdCapabilityModifierOperation Operation =
    ECrowdCapabilityModifierOperation::Add;

  bool IsValid() const
  {
    return CapabilityId.IsValid()
      && Operation < ECrowdCapabilityModifierOperation::Count;
  }
  bool operator==(const FCrowdCapabilityModifier& Other) const = default;
};

struct MASSCROWDCORE_API FCrowdCapabilityBinding
{
  FCrowdCapabilityProfileKey ProfileKey;
  uint32 ModifierRevision = 0;
  uint8 ModifierCount = 0;
  FCrowdCapabilityModifier
    Modifiers[CrowdBehavior::MaxCapabilityModifiers] = {};

  bool IsValid() const;
};

struct MASSCROWDCORE_API FCrowdResolvedCapabilitySet
{
  uint8 Count = 0;
  FCrowdCapabilityId
    CapabilityIds[CrowdBehavior::MaxResolvedCapabilities] = {};

  bool IsValid() const;
  bool Has(FCrowdCapabilityId CapabilityId) const;
  bool ContainsAll(TConstArrayView<FCrowdCapabilityId> Required) const;
  uint64 CalculateStableHash() const;
};

struct MASSCROWDCORE_API FCrowdCapabilityProfile
{
  FCrowdCapabilityProfileKey Key;
  TArray<FCrowdCapabilityId> CapabilityIds;

  bool IsValid() const;
};

class MASSCROWDCORE_API FCrowdCapabilityProfileRegistry
{
public:
  bool Register(FCrowdCapabilityProfile Profile);
  bool Freeze();
  bool Resolve(
    const FCrowdCapabilityBinding& Binding,
    FCrowdResolvedCapabilitySet& OutCapabilities) const;

  bool IsFrozen() const { return bFrozen; }
  int32 Num() const { return Profiles.Num(); }
  uint64 CalculateStableHash() const;

private:
  TMap<FCrowdCapabilityProfileKey, FCrowdCapabilityProfile> Profiles;
  bool bFrozen = false;
};

struct FCrowdBehaviorControllerId
{
  uint32 Value = 0;

  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBehaviorControllerId& Other) const = default;
  bool operator<(const FCrowdBehaviorControllerId& Other) const
  {
    return Value < Other.Value;
  }

  friend uint32 GetTypeHash(const FCrowdBehaviorControllerId& Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdBehaviorSourceTypeId
{
  uint32 Value = 0;

  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBehaviorSourceTypeId& Other) const = default;
  bool operator<(const FCrowdBehaviorSourceTypeId& Other) const
  {
    return Value < Other.Value;
  }

  friend uint32 GetTypeHash(const FCrowdBehaviorSourceTypeId& Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdBehaviorSourceHandle
{
  FCrowdStableEntityRef EntityRef;
  FCrowdBehaviorControllerId ControllerId;
  uint32 SourceSequence = 0;

  bool IsValid() const
  {
    return EntityRef.IsValid()
      && ControllerId.IsValid()
      && SourceSequence != 0;
  }
  bool operator==(const FCrowdBehaviorSourceHandle& Other) const = default;
  bool operator<(const FCrowdBehaviorSourceHandle& Other) const
  {
    if (EntityRef != Other.EntityRef) return EntityRef < Other.EntityRef;
    if (ControllerId != Other.ControllerId)
      return ControllerId < Other.ControllerId;
    return SourceSequence < Other.SourceSequence;
  }

  friend uint32 GetTypeHash(const FCrowdBehaviorSourceHandle& Handle)
  {
    uint32 Hash = HashCombineFast(
      GetTypeHash(Handle.EntityRef), GetTypeHash(Handle.ControllerId));
    return HashCombineFast(Hash, GetTypeHash(Handle.SourceSequence));
  }
};

enum class ECrowdBehaviorSourceReplicationPolicy : uint8
{
  ServerOnly = 0,
  ResolvedOnly,
  Predictable,
  Count
};

enum class ECrowdBehaviorChannel : uint8
{
  Movement = 0,
  Facing,
  Constraint,
  Interaction,
  Business,
  Presentation,
  Count
};

constexpr uint16 CrowdBehaviorChannelBit(const ECrowdBehaviorChannel Channel)
{
  return static_cast<uint8>(Channel)
      < static_cast<uint8>(ECrowdBehaviorChannel::Count)
    ? uint16{1} << static_cast<uint8>(Channel)
    : 0;
}

enum class ECrowdBehaviorBlendMode : uint8
{
  Override = 0,
  WeightedAdd,
  Additive,
  MinLimit,
  MaxLimit,
  Exclusive,
  RejectOnConflict,
  Count
};

struct MASSCROWDCORE_API FCrowdBehaviorSourcePayload
{
  uint32 SchemaId = 0;
  uint16 Size = 0;
  uint8 Bytes[CrowdBehavior::MaxPayloadBytes] = {};

  bool IsValid() const
  {
    return SchemaId != 0
      && Size <= CrowdBehavior::MaxPayloadBytes;
  }

  template <typename T>
  bool Set(const uint32 InSchemaId, const T& Value)
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (InSchemaId == 0 || sizeof(T) > CrowdBehavior::MaxPayloadBytes)
      return false;
    *this = {};
    SchemaId = InSchemaId;
    Size = sizeof(T);
    FMemory::Memcpy(Bytes, &Value, sizeof(T));
    return true;
  }

  bool SetBytes(
    const uint32 InSchemaId,
    TConstArrayView<uint8> InBytes)
  {
    if (InSchemaId == 0
      || InBytes.Num() > CrowdBehavior::MaxPayloadBytes)
      return false;
    *this = {};
    SchemaId = InSchemaId;
    Size = static_cast<uint16>(InBytes.Num());
    if (!InBytes.IsEmpty())
      FMemory::Memcpy(Bytes, InBytes.GetData(), InBytes.Num());
    return true;
  }

  template <typename T>
  bool Get(const uint32 ExpectedSchemaId, T& OutValue) const
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (SchemaId != ExpectedSchemaId || Size != sizeof(T))
      return false;
    FMemory::Memcpy(&OutValue, Bytes, sizeof(T));
    return true;
  }

  bool operator==(const FCrowdBehaviorSourcePayload& Other) const;
  uint64 CalculateStableHash() const;
};

struct MASSCROWDCORE_API FCrowdBehaviorSourceState
{
  uint32 SchemaId = 0;
  uint16 Size = 0;
  uint8 Bytes[CrowdBehavior::MaxStateBytes] = {};

  bool IsValid() const
  {
    return (SchemaId == 0 && Size == 0)
      || (SchemaId != 0 && Size <= CrowdBehavior::MaxStateBytes);
  }

  template <typename T>
  bool Set(const uint32 InSchemaId, const T& Value)
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (InSchemaId == 0 || sizeof(T) > CrowdBehavior::MaxStateBytes)
      return false;
    *this = {};
    SchemaId = InSchemaId;
    Size = sizeof(T);
    FMemory::Memcpy(Bytes, &Value, sizeof(T));
    return true;
  }

  template <typename T>
  bool Get(const uint32 ExpectedSchemaId, T& OutValue) const
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (SchemaId != ExpectedSchemaId || Size != sizeof(T))
      return false;
    FMemory::Memcpy(&OutValue, Bytes, sizeof(T));
    return true;
  }

  bool operator==(const FCrowdBehaviorSourceState& Other) const;
  uint64 CalculateStableHash() const;
};

struct MASSCROWDCORE_API FCrowdBehaviorContextRecord
{
  FCrowdBehaviorContextTypeId TypeId;
  uint16 SchemaVersion = 0;
  uint16 Size = 0;
  uint8 Bytes[CrowdBehavior::MaxContextRecordBytes] = {};

  bool IsValid() const
  {
    return TypeId.IsValid()
      && SchemaVersion != 0
      && Size <= CrowdBehavior::MaxContextRecordBytes;
  }

  template <typename T>
  bool Set(
    const FCrowdBehaviorContextTypeId InTypeId,
    const uint16 InSchemaVersion,
    const T& Value)
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (!InTypeId.IsValid() || InSchemaVersion == 0
      || sizeof(T) > CrowdBehavior::MaxContextRecordBytes)
      return false;
    *this = {};
    TypeId = InTypeId;
    SchemaVersion = InSchemaVersion;
    Size = sizeof(T);
    FMemory::Memcpy(Bytes, &Value, sizeof(T));
    return true;
  }

  template <typename T>
  bool Get(
    const FCrowdBehaviorContextTypeId ExpectedTypeId,
    const uint16 ExpectedSchemaVersion,
    T& OutValue) const
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (!(TypeId == ExpectedTypeId)
      || SchemaVersion != ExpectedSchemaVersion
      || Size != sizeof(T))
      return false;
    FMemory::Memcpy(&OutValue, Bytes, sizeof(T));
    return true;
  }

  uint64 CalculateStableHash() const;
};

struct MASSCROWDCORE_API FCrowdBehaviorSourceSpec
{
  FCrowdBehaviorSourceTypeId TypeId;
  uint16 Version = 1;
  uint16 ChannelMask = 0;
  int16 DefaultPriority = 0;
  uint16 ExclusiveGroup = 0;
  int32 MaxLifetimeSteps = 0;
  uint32 PayloadSchemaId = 0;
  uint32 StateSchemaId = 0;
  ECrowdBehaviorSourceReplicationPolicy ReplicationPolicy =
    ECrowdBehaviorSourceReplicationPolicy::ServerOnly;
  uint8 RequiredCapabilityCount = 0;
  FCrowdCapabilityId RequiredCapabilities[
    CrowdBehavior::MaxRequiredCapabilitiesPerSource] = {};

  bool IsValid() const;
  uint64 CalculateStableHash() const;
};

class MASSCROWDCORE_API FCrowdBehaviorSourceSpecRegistry
{
public:
  bool Register(const FCrowdBehaviorSourceSpec& Spec);
  bool Freeze();
  const FCrowdBehaviorSourceSpec* Find(
    FCrowdBehaviorSourceTypeId TypeId) const;

  bool IsFrozen() const { return bFrozen; }
  int32 Num() const { return Specs.Num(); }
  uint64 CalculateStableHash() const;

private:
  TMap<FCrowdBehaviorSourceTypeId, FCrowdBehaviorSourceSpec> Specs;
  bool bFrozen = false;
};

enum class ECrowdBehaviorSourceCommandKind : uint8
{
  Start = 0,
  Update,
  Stop,
  Count
};

struct MASSCROWDCORE_API FCrowdBehaviorSourceCommand
{
  int64 EffectiveFixedStep = INDEX_NONE;
  FCrowdBehaviorSourceHandle Handle;
  uint32 CommandSequence = 0;
  ECrowdBehaviorSourceCommandKind Kind =
    ECrowdBehaviorSourceCommandKind::Start;
  FCrowdBehaviorSourceTypeId SourceTypeId;
  int16 Priority = 0;
  int32 LifetimeSteps = 0;
  FCrowdBehaviorSourcePayload Payload;

  bool IsValid() const;
  uint64 CalculateStableHash() const;
};

struct FCrowdBehaviorControllerCursor
{
  FCrowdBehaviorControllerId ControllerId;
  uint32 LastCommandSequence = 0;
  uint64 LastCommandHash = 0;

  bool IsValid() const
  {
    return ControllerId.IsValid()
      && LastCommandSequence != 0
      && LastCommandHash != 0;
  }
};

struct MASSCROWDCORE_API FCrowdBehaviorSourceInstance
{
  FCrowdBehaviorSourceHandle Handle;
  FCrowdBehaviorSourceTypeId SourceTypeId;
  uint16 SourceVersion = 0;
  int16 Priority = 0;
  uint16 ExclusiveGroup = 0;
  int64 StartFixedStep = INDEX_NONE;
  int64 LastUpdateFixedStep = INDEX_NONE;
  int64 ExpireFixedStep = INDEX_NONE;
  ECrowdBehaviorSourceReplicationPolicy ReplicationPolicy =
    ECrowdBehaviorSourceReplicationPolicy::ServerOnly;
  FCrowdBehaviorSourcePayload Payload;
  FCrowdBehaviorSourceState State;

  bool IsValid() const;
  uint64 CalculateStableHash() const;
};

struct MASSCROWDCORE_API FCrowdBehaviorSourceSet
{
  FCrowdStableEntityRef EntityRef;
  FCrowdCapabilityBinding CapabilityBinding;
  uint32 Revision = 0;
  TArray<FCrowdBehaviorSourceInstance> Instances;
  TArray<FCrowdBehaviorControllerCursor> ControllerCursors;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

enum class ECrowdBehaviorSourceEventKind : uint8
{
  Started = 0,
  Updated,
  Stopped,
  Expired,
  CapabilityRevoked,
  Count
};

struct FCrowdBehaviorSourceEvent
{
  ECrowdBehaviorSourceEventKind Kind =
    ECrowdBehaviorSourceEventKind::Started;
  int64 FixedStepIndex = INDEX_NONE;
  FCrowdBehaviorSourceHandle Handle;
  FCrowdBehaviorSourceTypeId SourceTypeId;
};

class MASSCROWDCORE_API FCrowdBehaviorSourceStateMachine
{
public:
  static bool Apply(
    const FCrowdBehaviorSourceSet& Current,
    TConstArrayView<FCrowdBehaviorSourceCommand> Commands,
    int64 FixedStepIndex,
    const FCrowdBehaviorSourceSpecRegistry& Specs,
    const FCrowdResolvedCapabilitySet& Capabilities,
    FCrowdBehaviorSourceSet& OutStaged,
    TArray<FCrowdBehaviorSourceEvent>& OutEvents,
    uint64& OutCommandBatchHash);
};

struct FCrowdBehaviorContributionKey
{
  int16 Priority = 0;
  FCrowdBehaviorSourceTypeId SourceTypeId;
  FCrowdBehaviorControllerId ControllerId;
  uint32 SourceSequence = 0;

  bool IsValid() const
  {
    return SourceTypeId.IsValid()
      && ControllerId.IsValid()
      && SourceSequence != 0;
  }
  bool operator==(const FCrowdBehaviorContributionKey& Other) const = default;
};

struct FCrowdResolvedMovementGoal
{
  FVector Location = FVector::ZeroVector;
  FCrowdStableEntityRef TargetRef;
  uint64 FactRevision = 0;
  bool bHasGoal = false;

  bool IsValid() const
  {
    return !bHasGoal
      || (FMath::IsFinite(Location.X)
        && FMath::IsFinite(Location.Y)
        && FMath::IsFinite(Location.Z)
        && (TargetRef.IsUnset() || TargetRef.IsValid())
        && FactRevision != 0);
  }
  bool operator==(const FCrowdResolvedMovementGoal& Other) const = default;
};

struct FCrowdMovementContribution
{
  FCrowdBehaviorContributionKey Key;
  ECrowdBehaviorBlendMode BlendMode = ECrowdBehaviorBlendMode::Override;
  uint16 WeightQ15 = CrowdBehavior::FullQ15Weight;
  FVector DesiredVelocity = FVector::ZeroVector;
  FCrowdResolvedMovementGoal Goal;
};

struct FCrowdFacingContribution
{
  FCrowdBehaviorContributionKey Key;
  ECrowdBehaviorBlendMode BlendMode = ECrowdBehaviorBlendMode::Override;
  uint16 WeightQ15 = CrowdBehavior::FullQ15Weight;
  FVector DesiredDirection = FVector::ForwardVector;
};

struct FCrowdConstraintContribution
{
  FCrowdBehaviorContributionKey Key;
  ECrowdBehaviorBlendMode BlendMode = ECrowdBehaviorBlendMode::MinLimit;
  float SpeedLimitCmps = TNumericLimits<float>::Max();
  uint64 AllowedNavLayerMask = MAX_uint64;
  bool bLockMovement = false;
};

struct FCrowdInteractionContribution
{
  FCrowdBehaviorContributionKey Key;
  ECrowdBehaviorBlendMode BlendMode = ECrowdBehaviorBlendMode::Exclusive;
  uint32 IntentTypeId = 0;
  FCrowdStableEntityRef TargetRef;
  uint32 PayloadTypeId = 0;
  uint32 PayloadKey = 0;
};

struct FCrowdBusinessContribution
{
  FCrowdBehaviorContributionKey Key;
  ECrowdBehaviorBlendMode BlendMode =
    ECrowdBehaviorBlendMode::RejectOnConflict;
  uint32 AdapterId = 0;
  uint16 ExclusiveGroup = 0;
  uint64 CommitId = 0;
  FCrowdStableEntityRef InstigatorRef;
  FCrowdStableEntityRef TargetRef;
  uint32 PayloadTypeId = 0;
  int32 Quantity = 0;
};

struct FCrowdPresentationContribution
{
  FCrowdBehaviorContributionKey Key;
  ECrowdBehaviorBlendMode BlendMode = ECrowdBehaviorBlendMode::Override;
  uint32 PropertyId = 0;
  uint32 Value = 0;
};

struct MASSCROWDCORE_API FCrowdBehaviorContributions
{
  TArray<FCrowdMovementContribution> Movement;
  TArray<FCrowdFacingContribution> Facing;
  TArray<FCrowdConstraintContribution> Constraints;
  TArray<FCrowdInteractionContribution> Interactions;
  TArray<FCrowdBusinessContribution> Business;
  TArray<FCrowdPresentationContribution> Presentation;

  bool IsWithinLimits() const;
};

struct FCrowdResolvedBehaviorChannels
{
  FVector DesiredVelocity = FVector::ZeroVector;
  FCrowdResolvedMovementGoal MovementGoal;
  FVector DesiredFacing = FVector::ForwardVector;
  float SpeedLimitCmps = TNumericLimits<float>::Max();
  uint64 AllowedNavLayerMask = MAX_uint64;
  bool bMovementLocked = false;
  bool bHasInteraction = false;
  FCrowdInteractionContribution Interaction;
  TArray<FCrowdBusinessContribution> Business;
  TArray<FCrowdPresentationContribution> Presentation;
  uint64 MovementHash = 0;
  uint64 FacingHash = 0;
  uint64 ConstraintHash = 0;
  uint64 InteractionHash = 0;
  uint64 BusinessHash = 0;
  uint64 PresentationHash = 0;
  uint64 StableHash = 0;
  bool bValid = false;
};

class MASSCROWDCORE_API FCrowdBehaviorResolver
{
public:
  static bool Resolve(
    const FCrowdBehaviorContributions& Contributions,
    FCrowdResolvedBehaviorChannels& OutResolved);
};
