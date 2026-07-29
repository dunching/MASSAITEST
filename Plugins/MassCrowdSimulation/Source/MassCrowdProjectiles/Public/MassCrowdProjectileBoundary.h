#pragma once

#include "CoreMinimal.h"
#include "MassCrowdProjectileKernel.h"

struct MASSCROWDPROJECTILES_API FCrowdProjectileBoundaryInput
{
  int64 FixedStepIndex = INDEX_NONE;
  float ServerTimeSeconds = 0.0f;
  float FixedStepSeconds = 0.0f;
  TArray<FCrowdProjectileProfile> Profiles;
  TArray<FCrowdProjectileSpawnRequest> SpawnRequests;
  TArray<FCrowdProjectileTargetSnapshot> Targets;
  TArray<FCrowdSpatialEnvironmentBody> EnvironmentBodies;
  TArray<FCrowdProjectileState> CurrentStates;
};

struct MASSCROWDPROJECTILES_API FCrowdPreparedProjectileBoundary
{
  int64 FixedStepIndex = INDEX_NONE;
  uint32 BaseStateHash = 0;
  TArray<FCrowdProjectileState> States;
  TArray<FCrowdImpactFact> Impacts;
  TArray<FCrowdProjectileLifecycleEvent> Events;
  FCrowdProjectileStepSummary Summary;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

class MASSCROWDPROJECTILES_API FCrowdProjectileBoundaryPipeline
{
public:
  static bool Prepare(
    const FCrowdProjectileBoundaryInput& Input,
    FCrowdPreparedProjectileBoundary& OutPrepared);
  static bool ValidatePrepared(
    const FCrowdProjectileBoundaryInput& Input,
    const FCrowdPreparedProjectileBoundary& Prepared);
};

class MASSCROWDPROJECTILES_API ICrowdProjectilePreparedPatchAdapter
{
public:
  virtual ~ICrowdProjectilePreparedPatchAdapter() = default;

  virtual bool Validate(
    const FCrowdPreparedProjectileBoundary& Prepared) const = 0;
  virtual void ApplyValidated(
    const FCrowdPreparedProjectileBoundary& Prepared) = 0;
};
