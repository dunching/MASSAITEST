#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoAttackPlanner.h"
#include "Mass/CrowdDemoAttackHostAdapter.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoProjectileAdapters.h"
#include "MassCrowdWorkerProjectileDomain.h"

struct FCrowdDemoWorkerCombatHostInput
{
  int32 RoundId = INDEX_NONE;
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  float ServerTimeSeconds = 0.0f;
  float FixedStepSeconds = 0.0f;
  FCrowdDemoRangedCombatSettings AttackSettings;
  FCrowdDemoHitResponseSettings HitSettings;
  TArray<FCrowdDemoRangedCombatAgent> Agents;
};

struct FCrowdDemoWorkerCombatHostResult
{
  int32 FixedStepIndex = INDEX_NONE;
  FCrowdDemoProjectileStepSummary AttackSummary;
  FCrowdDemoHitResponseSummary HitSummary;
};

struct FCrowdDemoWorkerMixedCombatAgent
{
  FCrowdStableEntityRef EntityRef;
  uint32 FactionId = 0;
  uint32 NavLayer = 0;
  uint32 AttackProfileId = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  FVector Facing = FVector::ForwardVector;
  int32 Health = 0;
  FCrowdDemoAttackState AttackState;
};

struct FCrowdDemoWorkerMixedCombatHostInput
{
  int32 FixedStepIndex = INDEX_NONE;
  float FixedStepSeconds = 0.0f;
  TArray<FCrowdDemoAttackProfileV1> Profiles;
  TArray<FCrowdDemoWorkerMixedCombatAgent> Agents;
};

struct FCrowdDemoWorkerMixedCombatState
{
  int32 Health = 0;
  bool bAlive = false;
  FCrowdDemoAttackState AttackState;
};

struct FCrowdDemoWorkerMixedCombatHostResult
{
  int32 FixedStepIndex = INDEX_NONE;
  FCrowdDemoAttackPlanSummary AttackPlanSummary;
  int32 MeleeIntentCount = 0;
  int32 MidRangeIntentCount = 0;
  int32 RangedIntentCount = 0;
  int32 MissCount = 0;
  int32 EnvironmentImpactCount = 0;
  int32 AppliedDamageCount = 0;
  int32 DuplicateHitCount = 0;
  int32 FriendlyFireCount = 0;
  int32 DeathCount = 0;
  int32 TargetSwitchCount = 0;
};

class FCrowdDemoWorkerCombatHostInputCodec
{
public:
  static constexpr uint32 SchemaId = 0x44435749u;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr int32 MaxEncodedBytes = 4 * 1024 * 1024;

  static bool Encode(
    const FCrowdDemoWorkerCombatHostInput& Input,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdDemoWorkerCombatHostInput& OutInput);
};

class FCrowdDemoWorkerCombatStatePayloadCodec
{
public:
  static constexpr uint32 SchemaId = 0x44435753u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdDemoCombatAgentState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdDemoCombatAgentState& OutState);
};

class FCrowdDemoWorkerCombatHostResultCodec
{
public:
  static constexpr uint32 SchemaId = 0x44435752u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdDemoWorkerCombatHostResult& Result,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdDemoWorkerCombatHostResult& OutResult);
};

class FCrowdDemoWorkerMixedCombatHostInputCodec
{
public:
  static constexpr uint32 SchemaId = 0x444d5749u;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr int32 MaxEncodedBytes = 4 * 1024 * 1024;

  static bool Encode(
    const FCrowdDemoWorkerMixedCombatHostInput& Input,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdDemoWorkerMixedCombatHostInput& OutInput);
};

class FCrowdDemoWorkerMixedCombatStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x444d5753u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdDemoWorkerMixedCombatState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdDemoWorkerMixedCombatState& OutState);
};

class FCrowdDemoWorkerMixedCombatHostResultCodec
{
public:
  static constexpr uint32 SchemaId = 0x444d5752u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdDemoWorkerMixedCombatHostResult& Result,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdDemoWorkerMixedCombatHostResult& OutResult);
};

TUniquePtr<ICrowdWorkerCombatExtension>
  MakeCrowdDemoWorkerCombatExtension();
