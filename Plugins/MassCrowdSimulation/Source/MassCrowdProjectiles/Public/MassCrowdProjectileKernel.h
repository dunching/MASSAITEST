#pragma once

#include "CoreMinimal.h"
#include "MassCrowdCombatFacts.h"
#include "MassCrowdProjectileTypes.h"
#include "MassCrowdSpatialQuery.h"

class MASSCROWDPROJECTILES_API FCrowdProjectileKernel
{
public:
  static bool Spawn(
    int64 FixedStepIndex,
    float ServerTimeSeconds,
    TConstArrayView<FCrowdProjectileProfile> Profiles,
    TConstArrayView<FCrowdProjectileSpawnRequest> Requests,
    TArray<FCrowdProjectileState>& InOutProjectiles,
    TArray<FCrowdProjectileLifecycleEvent>& OutEvents,
    FCrowdProjectileStepSummary& InOutSummary);

  static bool Advance(
    int64 FixedStepIndex,
    float ServerTimeSeconds,
    float FixedStepSeconds,
    TConstArrayView<FCrowdProjectileProfile> Profiles,
    TConstArrayView<FCrowdProjectileTargetSnapshot> Targets,
    TConstArrayView<FCrowdSpatialEnvironmentBody> EnvironmentBodies,
    TArray<FCrowdProjectileState>& InOutProjectiles,
    TArray<FCrowdImpactFact>& OutImpacts,
    TArray<FCrowdProjectileLifecycleEvent>& OutEvents,
    FCrowdProjectileStepSummary& InOutSummary);

  static uint32 HashStates(
    TConstArrayView<FCrowdProjectileState> Projectiles);
  static uint32 HashEvents(
    TConstArrayView<FCrowdProjectileLifecycleEvent> Events);
};
