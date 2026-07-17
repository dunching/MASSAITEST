#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoCombatStateKernel.h"

struct FCrowdDemoRangedCombatAgent
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  int32 FormationIndex = INDEX_NONE;
  FVector Position = FVector::ZeroVector;
  float RadiusCm = 42.0f;
  bool bAlive = true;
  FCrowdDemoCombatAgentState Combat;
};

struct FCrowdDemoProjectileSpawnRequest
{
  uint64 ProjectileId = 0;
  int32 FixedStepIndex = INDEX_NONE;
  int32 SourceAgentId = INDEX_NONE;
  int32 SourceLifecycleSerial = 0;
  int32 TargetAgentId = INDEX_NONE;
  int32 TargetLifecycleSerial = 0;
  int32 FireSequence = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
};

struct FCrowdDemoProjectileState
{
  uint64 ProjectileId = 0;
  int32 SourceAgentId = INDEX_NONE;
  int32 SourceLifecycleSerial = 0;
  int32 TargetAgentId = INDEX_NONE;
  int32 TargetLifecycleSerial = 0;
  int32 FireSequence = 0;
  int32 SpawnFixedStep = INDEX_NONE;
  int32 AgeFixedSteps = 0;
  FVector PreviousPosition = FVector::ZeroVector;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float RadiusCm = 12.0f;
  bool bActive = false;
  bool bImpacted = false;
  bool bExpired = false;
};

struct FCrowdDemoProjectileStepSummary
{
  bool bValid = false;
  int32 TargetAcquiredCount = 0;
  int32 CompletedWindupCount = 0;
  int32 SpawnedCount = 0;
  int32 ActiveCount = 0;
  int32 ImpactedCount = 0;
  int32 ExpiredCount = 0;
  int32 DuplicateFireCount = 0;
  int32 DuplicateHitCount = 0;
  int32 InvalidTargetLifecycleCount = 0;
  int32 InvalidProjectileCount = 0;
  uint32 AttackStateHash = 2166136261u;
  uint32 ProjectileStateHash = 2166136261u;
  uint32 EventHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoProjectileKernel
{
public:
  static bool ValidateSettings(const FCrowdDemoRangedCombatSettings& Settings);

  static void AdvanceAttackPhases(
    int32 RoundId,
    int32 FixedStepIndex,
    const FCrowdDemoRangedCombatSettings& Settings,
    TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
    TArray<FCrowdDemoProjectileSpawnRequest>& OutSpawnRequests,
    FCrowdDemoProjectileStepSummary& InOutSummary);

  static void SpawnProjectiles(
    int32 FixedStepIndex,
    float ServerTimeSeconds,
    const FCrowdDemoRangedCombatSettings& Settings,
    TConstArrayView<FCrowdDemoProjectileSpawnRequest> Requests,
    TArray<FCrowdDemoProjectileState>& InOutProjectiles,
    TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
    FCrowdDemoProjectileStepSummary& InOutSummary);

  static void AdvanceProjectiles(
    int32 FixedStepIndex,
    float ServerTimeSeconds,
    float FixedStepSeconds,
    const FCrowdDemoRangedCombatSettings& Settings,
    TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
    TArray<FCrowdDemoProjectileState>& InOutProjectiles,
    TArray<FCrowdDemoHitFact>& OutHitFacts,
    TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
    FCrowdDemoProjectileStepSummary& InOutSummary);

  static uint32 HashAttackStates(TConstArrayView<FCrowdDemoRangedCombatAgent> Agents);
  static uint32 HashProjectileStates(TConstArrayView<FCrowdDemoProjectileState> Projectiles);
  static uint32 HashEvents(TConstArrayView<FCrowdDemoProjectileVisualEvent> Events);
};
