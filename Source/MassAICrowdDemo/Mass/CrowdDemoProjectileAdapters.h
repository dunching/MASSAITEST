#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "MassCrowdCombatResolver.h"
#include "MassCrowdProjectileBoundary.h"

struct FCrowdDemoRangedCombatAgent
{
  FCrowdStableEntityRef EntityRef;
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
  inline constexpr uint32 ProjectileProfileId = 1;
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
    TArray<FCrowdSpatialEnvironmentBody>& OutBodies)
    const override;

private:
  FCrowdDemoSharedFlowFieldConfig Config;
};

class MASSAICROWDDEMO_API FCrowdDemoProjectileAdapters
{
public:
  static bool ValidateSettings(
    const FCrowdDemoRangedCombatSettings& Settings);
  static FCrowdProjectileProfile BuildProfile(
    const FCrowdDemoRangedCombatSettings& Settings,
    int32 MaximumCapacity);
  static FCrowdEffectProfile BuildEffectProfile(
    const FCrowdDemoRangedCombatSettings& Settings);

  static bool BuildRangedAttackPlan(
    int32 RoundId,
    int32 FixedStepIndex,
    const FCrowdDemoRangedCombatSettings& Settings,
    TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
    TArray<FCrowdProjectileSpawnRequest>& OutSpawnRequests,
    FCrowdDemoProjectileStepSummary& InOutSummary);

  static bool BuildTargetSnapshots(
    float FixedStepSeconds,
    TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
    TArray<FCrowdProjectileTargetSnapshot>& OutTargets);

  static void AppendVisualEvents(
    TConstArrayView<FCrowdProjectileLifecycleEvent> Events,
    TArray<FCrowdDemoProjectileVisualEvent>& OutEvents);

  static void MergeSummary(
    const FCrowdProjectileStepSummary& Source,
    FCrowdDemoProjectileStepSummary& InOutSummary);

  static bool PrepareProjectileBoundary(
    int32 RoundId,
    int32 FixedStepIndex,
    float ServerTimeSeconds,
    float FixedStepSeconds,
    const FCrowdDemoRangedCombatSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
    TConstArrayView<FCrowdProjectileState> CurrentStates,
    FCrowdPreparedProjectileBoundary& OutPrepared,
    TArray<FCrowdDemoHitFact>& OutHitFacts,
    TArray<FCrowdDemoProjectileVisualEvent>& OutVisualEvents,
    FCrowdDemoProjectileStepSummary& OutSummary);

  static bool BuildDemoHitFacts(
    TConstArrayView<FCrowdHitFact> Hits,
    TArray<FCrowdDemoHitFact>& OutFacts);

  static bool BuildDemoHitFacts(
    TConstArrayView<FCrowdHitFact> Hits,
    TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
    TArray<FCrowdDemoHitFact>& OutFacts);

  static uint32 HashAttackStates(
    TConstArrayView<FCrowdDemoRangedCombatAgent> Agents);
};
