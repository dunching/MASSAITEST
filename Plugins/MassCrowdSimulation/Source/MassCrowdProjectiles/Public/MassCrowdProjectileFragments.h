#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassCrowdAgentFacts.h"
#include "MassCrowdProjectileFragments.generated.h"

USTRUCT()
struct MASSCROWDPROJECTILES_API FCrowdMassProjectileTag : public FMassTag
{
  GENERATED_BODY()
};

USTRUCT()
struct MASSCROWDPROJECTILES_API FCrowdMassProjectileFragment
  : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) uint64 ProjectileId = 0;
  UPROPERTY(Transient) uint32 InstigatorProviderId = 0;
  UPROPERTY(Transient) uint64 InstigatorStableEntityId = 0;
  UPROPERTY(Transient) uint32 InstigatorLifecycleSerial = 0;
  UPROPERTY(Transient) uint32 TargetProviderId = 0;
  UPROPERTY(Transient) uint64 TargetStableEntityId = 0;
  UPROPERTY(Transient) uint32 TargetLifecycleSerial = 0;
  UPROPERTY(Transient) uint32 LastHitTargetProviderId = 0;
  UPROPERTY(Transient) uint64 LastHitTargetStableEntityId = 0;
  UPROPERTY(Transient) uint32 LastHitTargetLifecycleSerial = 0;
  UPROPERTY(Transient) uint32 FireSequence = 0;
  UPROPERTY(Transient) int64 SpawnFixedStep = INDEX_NONE;
  UPROPERTY(Transient) int32 AgeFixedSteps = 0;
  UPROPERTY(Transient) int32 RemainingPierces = 0;
  UPROPERTY(Transient) uint32 SourceFactionId = 0;
  UPROPERTY(Transient) uint32 NavLayer = 0;
  UPROPERTY(Transient) uint32 ProjectileProfileId = 0;
  UPROPERTY(Transient) uint32 CollisionProfileId = 0;
  UPROPERTY(Transient) uint32 EffectProfileId = 0;
  UPROPERTY(Transient) FVector PreviousPosition = FVector::ZeroVector;
  UPROPERTY(Transient) FVector Position = FVector::ZeroVector;
  UPROPERTY(Transient) FVector Velocity = FVector::ZeroVector;
  UPROPERTY(Transient) float RadiusCm = 0.0f;
  UPROPERTY(Transient) bool bActive = false;
  UPROPERTY(Transient) bool bImpacted = false;
  UPROPERTY(Transient) bool bExpired = false;

  FCrowdStableEntityRef GetInstigator() const;
  FCrowdStableEntityRef GetTarget() const;
  FCrowdStableEntityRef GetLastHitTarget() const;
};
