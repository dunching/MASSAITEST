#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "MassCrowdProjectileFacts.h"

struct FCrowdDemoRangedCombatAgent
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  int32 FormationIndex = INDEX_NONE;
  uint32 FactionId = 0;
  uint32 NavLayer = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
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
  uint32 SourceFactionId = 0;
  uint32 NavLayer = 0;
  uint32 CollisionProfileId = 1;
  uint32 EffectProfileId = 1;
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
  int32 RemainingPierces = 0;
  int32 LastHitTargetAgentId = INDEX_NONE;
  uint32 SourceFactionId = 0;
  uint32 NavLayer = 0;
  uint32 CollisionProfileId = 1;
  uint32 EffectProfileId = 1;
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
  int32 EnvironmentImpactCount = 0;
  int32 BroadphaseCandidateCount = 0;
  int32 SweepTestCount = 0;
  uint32 AttackStateHash = 2166136261u;
  uint32 ProjectileStateHash = 2166136261u;
  uint32 EventHash = 2166136261u;
};

struct FCrowdDemoProjectileHitPayload
{
  float Damage = 0.0f;
  float HorizontalImpulseCmps = 0.0f;
  float VerticalImpulseCmps = 0.0f;
  uint32 HitFlashProfileKey = 0;
};

namespace CrowdDemoProjectileSchemas
{
  inline constexpr uint32 HitPayloadTypeId = 0x44504801u;
  inline constexpr uint32 HitPayloadSchemaId = 0x44505301u;
}

class MASSAICROWDDEMO_API FCrowdDemoHostHitResolver final
  : public ICrowdHostHitResolver
{
public:
  explicit FCrowdDemoHostHitResolver(
    const FCrowdDemoRangedCombatSettings& InSettings)
    : Settings(InSettings) {}

  virtual bool Resolve(
    TConstArrayView<FCrowdImpactFact> Impacts,
    TArray<FCrowdHitFact>& OutHits) const override;

  static bool BuildDemoHitFacts(
    TConstArrayView<FCrowdHitFact> Hits,
    TArray<FCrowdDemoHitFact>& OutFacts);

private:
  FCrowdDemoRangedCombatSettings Settings;
};

class MASSAICROWDDEMO_API
  FCrowdDemoFlowObstacleCollisionSnapshotProvider final
  : public ICrowdEnvironmentCollisionSnapshotProvider
{
public:
  explicit FCrowdDemoFlowObstacleCollisionSnapshotProvider(
    const FCrowdDemoSharedFlowFieldConfig& InConfig)
    : Config(InConfig) {}

  virtual bool Gather(
    int64 FixedStepIndex,
    TArray<FCrowdProjectileEnvironmentBody>& OutBodies)
    const override;

private:
  FCrowdDemoSharedFlowFieldConfig Config;
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
    TArray<FCrowdImpactFact>& OutImpacts,
    TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
    FCrowdDemoProjectileStepSummary& InOutSummary);

  static void AdvanceProjectiles(
    int32 FixedStepIndex,
    float ServerTimeSeconds,
    float FixedStepSeconds,
    const FCrowdDemoRangedCombatSettings& Settings,
    TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
    TConstArrayView<FCrowdProjectileEnvironmentBody> EnvironmentBodies,
    TArray<FCrowdDemoProjectileState>& InOutProjectiles,
    TArray<FCrowdImpactFact>& OutImpacts,
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
