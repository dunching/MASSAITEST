#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoAttackPlanner.h"
#include "MassCrowdCombatFacts.h"
#include "MassCrowdProjectileTypes.h"
#include "MassCrowdSpatialQuery.h"

struct FCrowdDemoAttackTargetSnapshot
{
  FCrowdSpatialBodySnapshot Body;
  uint32 FactionId = 0;

  bool IsValid() const
  {
    return Body.IsValid();
  }
};

struct FCrowdDemoPreparedAttackBoundary
{
  int64 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdImpactFact> ImmediateImpacts;
  TArray<FCrowdProjectileSpawnRequest> ProjectileRequests;
  int32 MeleeIntentCount = 0;
  int32 MidRangeIntentCount = 0;
  int32 RangedIntentCount = 0;
  int32 MissCount = 0;
  int32 EnvironmentImpactCount = 0;
  uint64 StableHash = 0;
  bool bValid = false;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct FCrowdDemoAttackHealthState
{
  FCrowdStableEntityRef EntityRef;
  uint32 FactionId = 0;
  int32 Health = 0;
  bool bAlive = false;
};

struct FCrowdDemoPreparedAttackHealthPatch
{
  int64 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoAttackHealthState> States;
  int32 AppliedDamageCount = 0;
  int32 DuplicateHitCount = 0;
  int32 FriendlyFireCount = 0;
  int32 DeathCount = 0;
  uint64 StableHash = 0;
  bool bValid = false;

  bool IsValid() const;
  void RecalculateStableHash();
};

class MASSAICROWDDEMO_API FCrowdDemoAttackHostAdapter
{
public:
  static bool Prepare(
    int64 FixedStepIndex,
    TConstArrayView<FCrowdDemoAttackIntent> Intents,
    TConstArrayView<FCrowdDemoAttackTargetSnapshot> Targets,
    TConstArrayView<FCrowdSpatialEnvironmentBody> Environment,
    FCrowdDemoPreparedAttackBoundary& OutPrepared);

  static bool PrepareHealthPatch(
    int64 FixedStepIndex,
    TConstArrayView<FCrowdHitFact> Hits,
    TConstArrayView<FCrowdDemoAttackHealthState> CurrentStates,
    FCrowdDemoPreparedAttackHealthPatch& OutPatch);
};
