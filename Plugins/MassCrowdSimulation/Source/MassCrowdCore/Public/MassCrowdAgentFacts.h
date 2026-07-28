#pragma once

#include "CoreTypes.h"
#include "Templates/TypeHash.h"

struct FCrowdStableEntityRef
{
  uint32 ProviderId = 0;
  uint64 StableEntityId = 0;
  uint32 LifecycleSerial = 0;

  bool IsUnset() const
  {
    return ProviderId == 0 && StableEntityId == 0 && LifecycleSerial == 0;
  }

  bool IsValid() const
  {
    return ProviderId != 0 && StableEntityId != 0 && LifecycleSerial != 0;
  }

  bool IsSameEntitySlot(const FCrowdStableEntityRef& Other) const
  {
    return ProviderId == Other.ProviderId
      && StableEntityId == Other.StableEntityId;
  }

  bool operator==(const FCrowdStableEntityRef& Other) const = default;

  bool operator<(const FCrowdStableEntityRef& Other) const
  {
    if (ProviderId != Other.ProviderId) return ProviderId < Other.ProviderId;
    if (StableEntityId != Other.StableEntityId)
      return StableEntityId < Other.StableEntityId;
    return LifecycleSerial < Other.LifecycleSerial;
  }

  friend uint32 GetTypeHash(const FCrowdStableEntityRef& Ref)
  {
    uint32 Hash = HashCombineFast(
      ::GetTypeHash(Ref.ProviderId), ::GetTypeHash(Ref.StableEntityId));
    return HashCombineFast(Hash, ::GetTypeHash(Ref.LifecycleSerial));
  }
};

enum class ECrowdCapability : uint8
{
  Move = 0,
  Wander,
  MoveTo,
  Pursue,
  Haul,
  Attack,
  Guard,
  Flee,
  UseRangedAttack,
  UseNavLayer,
  Count
};

enum class ECrowdActiveBehavior : uint8
{
  Idle = 0,
  Wander,
  MoveTo,
  Pursue,
  HaulPickup,
  HaulDeliver,
  Attack,
  Guard,
  Flee,
  Dead,
  Count
};

struct FCrowdCapabilitySet
{
  uint64 Bits = 0;

  static constexpr uint64 Bit(const ECrowdCapability Capability)
  {
    const uint8 Index = static_cast<uint8>(Capability);
    return Index < static_cast<uint8>(ECrowdCapability::Count)
      ? uint64{1} << Index
      : 0;
  }

  static constexpr uint64 KnownBits()
  {
    return (uint64{1} << static_cast<uint8>(ECrowdCapability::Count)) - 1;
  }

  bool IsValid() const
  {
    return (Bits & ~KnownBits()) == 0;
  }

  bool Has(const ECrowdCapability Capability) const
  {
    const uint64 CapabilityBit = Bit(Capability);
    return CapabilityBit != 0 && (Bits & CapabilityBit) != 0;
  }

  void Add(const ECrowdCapability Capability)
  {
    Bits |= Bit(Capability);
  }

  void Remove(const ECrowdCapability Capability)
  {
    Bits &= ~Bit(Capability);
  }

  bool ContainsAll(const FCrowdCapabilitySet& Required) const
  {
    return Required.IsValid() && (Bits & Required.Bits) == Required.Bits;
  }

  bool CanActivate(const ECrowdActiveBehavior Behavior) const
  {
    switch (Behavior)
    {
    case ECrowdActiveBehavior::Idle:
    case ECrowdActiveBehavior::Dead:
      return true;
    case ECrowdActiveBehavior::Wander:
      return Has(ECrowdCapability::Wander);
    case ECrowdActiveBehavior::MoveTo:
      return Has(ECrowdCapability::MoveTo);
    case ECrowdActiveBehavior::Pursue:
      return Has(ECrowdCapability::Pursue);
    case ECrowdActiveBehavior::HaulPickup:
    case ECrowdActiveBehavior::HaulDeliver:
      return Has(ECrowdCapability::Haul);
    case ECrowdActiveBehavior::Attack:
      return Has(ECrowdCapability::Attack);
    case ECrowdActiveBehavior::Guard:
      return Has(ECrowdCapability::Guard);
    case ECrowdActiveBehavior::Flee:
      return Has(ECrowdCapability::Flee);
    default:
      return false;
    }
  }
};

struct FCrowdAgentFacts
{
  FCrowdStableEntityRef StableEntityRef;
  uint32 FactionKey = 0;
  FCrowdCapabilitySet CapabilitySet;
  ECrowdActiveBehavior ActiveBehavior = ECrowdActiveBehavior::Idle;
  FCrowdStableEntityRef BusinessTaskRef;
  FCrowdStableEntityRef TargetRef;
  uint32 MovementProfileKey = 0;
  uint32 PresentationProfileKey = 0;
  uint32 RuntimeState = 0;

  bool IsWellFormed() const
  {
    return StableEntityRef.IsValid()
      && CapabilitySet.IsValid()
      && ActiveBehavior < ECrowdActiveBehavior::Count
      && CapabilitySet.CanActivate(ActiveBehavior)
      && (BusinessTaskRef.IsUnset() || BusinessTaskRef.IsValid())
      && (TargetRef.IsUnset() || TargetRef.IsValid());
  }
};
