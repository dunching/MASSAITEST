#pragma once

#include "CoreMinimal.h"

enum class ECrowdWorkerConsistencyDomain : uint8
{
  ParticleInteractionIsland = 0,
  TargetCohort,
  CombatEventBoundary
};

enum class ECrowdWorkerConsistencyDecision : uint8
{
  KeepBoundary = 0,
  EligibleForWorkerPatch
};

enum class ECrowdWorkerConsistencyFailure : uint8
{
  None = 0,
  InvalidEvidence,
  MissingStableMembership,
  MissingInputEpoch,
  OpenInteractionBoundary,
  CrossDomainDependency,
  MissingEnvironmentRevision,
  MissingAtomicPlan,
  MissingOrderedEvents,
  MissingIdempotency,
  MissingRollbackProof,
  MissingNetworkSemantics
};

struct MASSCROWDRUNTIME_API FCrowdWorkerConsistencyEvidence
{
  ECrowdWorkerConsistencyDomain Domain =
    ECrowdWorkerConsistencyDomain::ParticleInteractionIsland;
  uint64 Generation = 0;
  uint64 DomainKey = 0;
  uint64 InputEpoch = 0;
  uint64 EnvironmentRevision = 0;
  int32 EntityCount = 0;
  int32 ExternalDependencyCount = 0;
  uint64 FirstEventSequence = 0;
  uint64 LastEventSequence = 0;
  int32 EventCount = 0;
  bool bStableMembership = false;
  bool bClosedInteractionBoundary = false;
  bool bAtomicPlan = false;
  bool bOrderedEvents = false;
  bool bIdempotencyProven = false;
  bool bRollbackProven = false;
  bool bNetworkSemanticsFrozen = false;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerConsistencyEvaluation
{
  ECrowdWorkerConsistencyDomain Domain =
    ECrowdWorkerConsistencyDomain::ParticleInteractionIsland;
  ECrowdWorkerConsistencyDecision Decision =
    ECrowdWorkerConsistencyDecision::KeepBoundary;
  ECrowdWorkerConsistencyFailure Failure =
    ECrowdWorkerConsistencyFailure::InvalidEvidence;
  uint64 Generation = 0;
  uint64 DomainKey = 0;
  uint64 InputEpoch = 0;
  bool bValid = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerConsistencyDomainEvaluator
{
public:
  static FCrowdWorkerConsistencyEvaluation Evaluate(
    const FCrowdWorkerConsistencyEvidence& Evidence);
};
