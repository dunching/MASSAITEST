#pragma once

#include "CoreMinimal.h"
#include "MassCrowdCombatResolver.h"
#include "MassCrowdProjectileBoundary.h"
#include "MassCrowdWorkerCombatState.h"
#include "MassCrowdWorkerRuntimeV2.h"

namespace CrowdWorkerProjectileEventTypeIds
{
  constexpr uint32 Lifecycle = 0x4357504cu;
  constexpr uint32 Hit = 0x43575048u;
}

struct MASSCROWDPROJECTILES_API FCrowdWorkerProjectileControlResource
{
  uint64 Revision = 0;
  FCrowdStableEntityRef AnchorEntity;
  FCrowdProjectileBoundaryInput Input;
  TArray<FCrowdEffectProfile> EffectProfiles;
  FCrowdWorkerPayload HostCombatInput;
  bool bReplaceState = false;

  bool IsValid() const;
};

class MASSCROWDPROJECTILES_API
  FCrowdWorkerProjectileControlResourceCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575043u;
  static constexpr uint16 SchemaVersion = 2;
  static constexpr int32 MaxEncodedBytes = 16 * 1024 * 1024;

  static bool Encode(
    const FCrowdWorkerProjectileControlResource& Resource,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerProjectileControlResource& OutResource);
};

struct MASSCROWDPROJECTILES_API FCrowdWorkerProjectileState
{
  uint64 ControlRevision = 0;
  FCrowdPreparedProjectileBoundary Prepared;
  FCrowdHitResolveResult ResolvedHits;
  FCrowdWorkerPayload HostCombatResult;

  bool IsValid() const;
};

class MASSCROWDPROJECTILES_API FCrowdWorkerProjectileStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575053u;
  static constexpr uint16 SchemaVersion = 2;
  static constexpr int32 MaxEncodedBytes = 16 * 1024 * 1024;

  static bool Encode(
    const FCrowdWorkerProjectileState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerProjectileState& OutState);
};

struct MASSCROWDPROJECTILES_API FCrowdWorkerProjectileDomainMetrics
{
  uint64 ExecutedStepCount = 0;
  uint64 DuplicateStepCount = 0;
  uint64 PublishedStateCount = 0;
  uint64 PublishedLifecycleEventCount = 0;
  uint64 PublishedHitEventCount = 0;
  uint64 ScheduledWakeupCount = 0;
  uint64 PublishedCombatStateCount = 0;
};

struct MASSCROWDPROJECTILES_API FCrowdWorkerCombatExtensionPatch
{
  FCrowdStableEntityRef EntityRef;
  FCrowdWorkerCombatState State;
};

// Pure C++ host rule adapter. It executes on the Worker and may not access
// UWorld, Mass fragments, UObject state, or hidden queries.
class MASSCROWDPROJECTILES_API ICrowdWorkerCombatExtension
{
public:
  virtual ~ICrowdWorkerCombatExtension() = default;
  virtual bool BeginStep(
    const FCrowdWorkerDomainContext& Context,
    const FCrowdWorkerPayload& HostInput,
    bool bReplaceState,
    FCrowdProjectileBoundaryInput& InOutProjectileInput,
    TArray<FCrowdImpactFact>& OutImmediateImpacts) = 0;
  virtual bool FinishStep(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdHitFact> Hits,
    TArray<FCrowdWorkerCombatExtensionPatch>& OutPatches,
    FCrowdWorkerPayload& OutHostResult) = 0;
  virtual bool ApplyAuthorityCorrection(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerDirtyStateRecord> Records)
  {
    return true;
  }
};

class MASSCROWDPROJECTILES_API
  FCrowdWorkerProjectileDomainExecutor final
  : public ICrowdWorkerDomainExecutor
{
public:
  explicit FCrowdWorkerProjectileDomainExecutor(
    TUniquePtr<ICrowdWorkerCombatExtension> InCombatExtension = {});

  ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::CombatReactive;
  }

  void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const override;
  bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;
  bool ApplyAuthorityCorrection(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerDirtyStateRecord> Records) override;
  FCrowdWorkerProjectileDomainMetrics GetMetrics() const;

private:
  mutable FCriticalSection StateMutex;
  uint64 StateGeneration = 0;
  uint64 LastControlRevision = 0;
  int64 LastFixedStepIndex = INDEX_NONE;
  TArray<FCrowdProjectileState> Projectiles;
  TUniquePtr<ICrowdWorkerCombatExtension> CombatExtension;
  FCrowdWorkerProjectileDomainMetrics Metrics;
};
