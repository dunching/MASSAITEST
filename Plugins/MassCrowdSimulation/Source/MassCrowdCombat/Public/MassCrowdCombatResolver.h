#pragma once

#include "CoreMinimal.h"
#include "MassCrowdCombatFacts.h"

struct MASSCROWDCOMBAT_API FCrowdEffectProfile
{
  uint32 EffectProfileId = 0;
  uint32 PayloadTypeId = 0;
  FCrowdBehaviorSourcePayload Payload;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDCOMBAT_API FCrowdHitResolveResult
{
  int64 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdHitFact> Hits;
  int32 EnvironmentImpactCount = 0;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

class MASSCROWDCOMBAT_API FCrowdCombatResolver
{
public:
  static bool Resolve(
    TConstArrayView<FCrowdImpactFact> Impacts,
    TConstArrayView<FCrowdEffectProfile> Profiles,
    FCrowdHitResolveResult& OutResult);
};

struct MASSCROWDCOMBAT_API FCrowdPreparedHostHitCommit
{
  int64 FixedStepIndex = INDEX_NONE;
  uint64 SourceResolveHash = 0;
  TArray<FCrowdHitFact> Hits;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

class MASSCROWDCOMBAT_API ICrowdPreparedHostHitAdapter
{
public:
  virtual ~ICrowdPreparedHostHitAdapter() = default;

  virtual bool Prepare(
    const FCrowdHitResolveResult& ResolveResult,
    FCrowdPreparedHostHitCommit& OutCommit) const = 0;
  virtual bool Validate(
    const FCrowdPreparedHostHitCommit& Commit) const = 0;
  virtual void ApplyValidated(
    const FCrowdPreparedHostHitCommit& Commit) = 0;
};
