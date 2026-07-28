#pragma once

#include "CoreMinimal.h"
#include "CrowdFacingKernel.h"
#include "MassCrowdAgentFacts.h"
#include "MassCrowdSimulationTypes.h"
#include "MassEntityTypes.h"
#include "MassCrowdRuntimeFragments.generated.h"

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassAgentTag : public FMassTag
{
  GENERATED_BODY()
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassAgentFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) int32 AgentId = INDEX_NONE;
  UPROPERTY(Transient) uint32 ProviderId = 0;
  UPROPERTY(Transient) uint64 StableEntityId = 0;
  UPROPERTY(Transient) uint32 LifecycleSerial = 0;

  FCrowdStableEntityRef GetStableEntityRef() const
  {
    return {ProviderId, StableEntityId, LifecycleSerial};
  }

  void SetStableEntityRef(const FCrowdStableEntityRef& Ref)
  {
    ProviderId = Ref.ProviderId;
    StableEntityId = Ref.StableEntityId;
    LifecycleSerial = Ref.LifecycleSerial;
  }
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassBehaviorFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) uint32 FactionKey = 0;
  UPROPERTY(Transient) uint64 CapabilityBits = 0;
  UPROPERTY(Transient) uint8 ActiveBehavior = 0;
  UPROPERTY(Transient) uint32 BusinessTaskProviderId = 0;
  UPROPERTY(Transient) uint64 BusinessTaskStableEntityId = 0;
  UPROPERTY(Transient) uint32 BusinessTaskLifecycleSerial = 0;
  UPROPERTY(Transient) uint32 TargetProviderId = 0;
  UPROPERTY(Transient) uint64 TargetStableEntityId = 0;
  UPROPERTY(Transient) uint32 TargetLifecycleSerial = 0;
  UPROPERTY(Transient) uint32 MovementProfileKey = 0;
  UPROPERTY(Transient) uint32 PresentationProfileKey = 0;
  UPROPERTY(Transient) uint32 RuntimeState = 0;

  FCrowdAgentFacts GetAgentFacts(const FCrowdMassAgentFragment& Identity) const
  {
    FCrowdAgentFacts Facts;
    Facts.StableEntityRef = Identity.GetStableEntityRef();
    Facts.FactionKey = FactionKey;
    Facts.CapabilitySet.Bits = CapabilityBits;
    Facts.ActiveBehavior = static_cast<ECrowdActiveBehavior>(ActiveBehavior);
    Facts.BusinessTaskRef = {
      BusinessTaskProviderId,
      BusinessTaskStableEntityId,
      BusinessTaskLifecycleSerial};
    Facts.TargetRef = {
      TargetProviderId, TargetStableEntityId, TargetLifecycleSerial};
    Facts.MovementProfileKey = MovementProfileKey;
    Facts.PresentationProfileKey = PresentationProfileKey;
    Facts.RuntimeState = RuntimeState;
    return Facts;
  }

  void SetAgentFacts(const FCrowdAgentFacts& Facts)
  {
    FactionKey = Facts.FactionKey;
    CapabilityBits = Facts.CapabilitySet.Bits;
    ActiveBehavior = static_cast<uint8>(Facts.ActiveBehavior);
    BusinessTaskProviderId = Facts.BusinessTaskRef.ProviderId;
    BusinessTaskStableEntityId = Facts.BusinessTaskRef.StableEntityId;
    BusinessTaskLifecycleSerial = Facts.BusinessTaskRef.LifecycleSerial;
    TargetProviderId = Facts.TargetRef.ProviderId;
    TargetStableEntityId = Facts.TargetRef.StableEntityId;
    TargetLifecycleSerial = Facts.TargetRef.LifecycleSerial;
    MovementProfileKey = Facts.MovementProfileKey;
    PresentationProfileKey = Facts.PresentationProfileKey;
    RuntimeState = Facts.RuntimeState;
  }
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassMembershipFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) uint32 MembershipKey = 0;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassSimulationStateFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) FVector Position = FVector::ZeroVector;
  UPROPERTY(Transient) FVector Velocity = FVector::ZeroVector;
  UPROPERTY(Transient) float YawDegrees = 0.0f;
  UPROPERTY(Transient) int32 PlanRevision = INDEX_NONE;
  UPROPERTY(Transient) bool bInitialized = false;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassPropertiesFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) float PhysicalRadiusCm = 42.0f;
  UPROPERTY(Transient) float HardSafetyGapCm = 10.0f;
  UPROPERTY(Transient) float SoftMarginCm = 17.0f;
  UPROPERTY(Transient) float Mobility = 1.0f;
  UPROPERTY(Transient) float MaximumSpeedCmps = 300.0f;
  UPROPERTY(Transient) uint32 CapabilityProfileKey = 0;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassFacingFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdFacingResult Value;
  UPROPERTY(Transient) int32 PlanRevision = INDEX_NONE;
  UPROPERTY(Transient) int32 ConsecutiveFinalSettleSteps = 0;
  UPROPERTY(Transient) bool bFinalPositionSettled = false;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassMovementOutputFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdMovementOutput Value;
};
