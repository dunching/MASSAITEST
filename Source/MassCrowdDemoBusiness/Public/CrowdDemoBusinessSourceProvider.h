#pragma once

#include "CoreMinimal.h"
#include "MassCrowdStandardSources.h"
#include "MassCrowdBehaviorSourceRuntime.h"

namespace CrowdDemoCapabilityIds
{
  inline constexpr FCrowdCapabilityId Haul{5};
  inline constexpr FCrowdCapabilityId Attack{6};
  inline constexpr FCrowdCapabilityId RangedAttack{9};
  inline constexpr FCrowdCapabilityId NavLayer{10};
  inline constexpr FCrowdCapabilityId CarryCargo{13};
}

namespace CrowdDemoSourceTypeIds
{
  inline constexpr FCrowdBehaviorSourceTypeId SharedFlow{1002};
  inline constexpr FCrowdBehaviorSourceTypeId CarryCargo{1301};
  inline constexpr FCrowdBehaviorSourceTypeId PickupInteraction{1401};
  inline constexpr FCrowdBehaviorSourceTypeId DeliverInteraction{1402};
  inline constexpr FCrowdBehaviorSourceTypeId AttackTarget{1501};
}

namespace CrowdDemoBehaviorSchemas
{
  inline constexpr FCrowdBehaviorProviderId Provider{1};
  inline constexpr uint32 Standard = 1;
  inline constexpr FCrowdCapabilityProfileKey FullProfile{1};
}

namespace CrowdDemoBehaviorAdapterIds
{
  inline constexpr uint32 CargoPickup = 7101;
  inline constexpr uint32 CargoDeliver = 7102;
  inline constexpr uint32 CombatHit = 7103;
}

namespace CrowdDemoBehaviorControllerIds
{
  inline constexpr FCrowdBehaviorControllerId Navigation{1};
  inline constexpr FCrowdBehaviorControllerId Facing{2};
  inline constexpr FCrowdBehaviorControllerId Interaction{3};
  inline constexpr FCrowdBehaviorControllerId Presentation{4};
  inline constexpr FCrowdBehaviorControllerId Reaction{5};
}

struct FCrowdDemoBehaviorSourcePayload
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
  FCrowdDemoBehaviorSourcePayload>);
static_assert(sizeof(FCrowdDemoBehaviorSourcePayload)
  <= CrowdBehavior::MaxPayloadBytes);

MASSCROWDDEMOBUSINESS_API
TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
CreateCrowdDemoBehaviorSourceProvider();

MASSCROWDDEMOBUSINESS_API ECrowdActiveBehavior
DeriveCrowdDemoDiagnosticBehavior(
  const FCrowdBehaviorSourceSet& SourceSet);

struct FCrowdDemoDesiredSource
{
  FCrowdBehaviorControllerId ControllerId;
  uint32 SourceSequence = 0;
  FCrowdBehaviorSourceTypeId SourceTypeId;
  int16 Priority = 0;
  int32 LifetimeSteps = 0;
  FCrowdBehaviorSourcePayload Payload;

  bool IsValid() const
  {
    return ControllerId.IsValid()
      && SourceSequence != 0
      && SourceTypeId.IsValid()
      && Payload.IsValid()
      && LifetimeSteps >= 0;
  }
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoSourceSetDiff
{
public:
  static bool BuildDesiredSourceDiff(
    int64 EffectiveFixedStep,
    const FCrowdBehaviorSourceSet& CurrentSet,
    TConstArrayView<FCrowdDemoDesiredSource> DesiredSources,
    TArray<FCrowdBehaviorSourceCommand>& OutCommands);
};
