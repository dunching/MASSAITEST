#pragma once

#include "MassCrowdBehaviorSourceRuntime.h"

namespace CrowdBuiltinCapabilityIds
{
  inline constexpr FCrowdCapabilityId Move{90001};
  inline constexpr FCrowdCapabilityId Wander{90002};
  inline constexpr FCrowdCapabilityId MoveTo{90003};
  inline constexpr FCrowdCapabilityId Pursue{90004};
  inline constexpr FCrowdCapabilityId Haul{90005};
  inline constexpr FCrowdCapabilityId Attack{90006};
  inline constexpr FCrowdCapabilityId Guard{90007};
  inline constexpr FCrowdCapabilityId Flee{90008};
  inline constexpr FCrowdCapabilityId RangedAttack{90009};
  inline constexpr FCrowdCapabilityId NavLayer{90010};
  inline constexpr FCrowdCapabilityId Face{90011};
  inline constexpr FCrowdCapabilityId Formation{90012};
  inline constexpr FCrowdCapabilityId CarryCargo{90013};
  inline constexpr FCrowdCapabilityId React{90014};
}

namespace CrowdBuiltinSourceTypeIds
{
  inline constexpr FCrowdBehaviorSourceTypeId MoveToSink{91001};
  inline constexpr FCrowdBehaviorSourceTypeId SharedFlow{91002};
  inline constexpr FCrowdBehaviorSourceTypeId FaceMovement{91101};
  inline constexpr FCrowdBehaviorSourceTypeId FaceTarget{91102};
  inline constexpr FCrowdBehaviorSourceTypeId Formation{91201};
  inline constexpr FCrowdBehaviorSourceTypeId CarryCargo{91301};
  inline constexpr FCrowdBehaviorSourceTypeId PickupInteraction{91401};
  inline constexpr FCrowdBehaviorSourceTypeId DeliverInteraction{91402};
  inline constexpr FCrowdBehaviorSourceTypeId AttackTarget{91501};
  inline constexpr FCrowdBehaviorSourceTypeId HitReaction{91601};
  inline constexpr FCrowdBehaviorSourceTypeId StunConstraint{91602};
  inline constexpr FCrowdBehaviorSourceTypeId DeathConstraint{91603};
}

namespace CrowdBuiltinBehaviorSchemas
{
  inline constexpr uint32 Standard = 90001;
  inline constexpr FCrowdCapabilityProfileKey LegacyFullProfile{90001};
}

struct FCrowdBuiltinBehaviorSourcePayload
{
  FVector Vector = FVector::ZeroVector;
  FCrowdStableEntityRef TargetRef;
  uint64 CommitId = 0;
  uint32 PrimaryId = 0;
  uint32 SecondaryId = 0;
  int32 Quantity = 0;
  uint32 Flags = 0;
};

static_assert(std::is_trivially_copyable_v<
  FCrowdBuiltinBehaviorSourcePayload>);

TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
CreateMassCrowdTestBehaviorProvider();
