#include "MassCrowdWorkerConsistencyDomains.h"

namespace CrowdWorkerConsistencyDomainsPrivate
{
  FCrowdWorkerConsistencyEvaluation Keep(
    const FCrowdWorkerConsistencyEvidence& Evidence,
    const ECrowdWorkerConsistencyFailure Failure)
  {
    FCrowdWorkerConsistencyEvaluation Result;
    Result.Domain = Evidence.Domain;
    Result.Decision =
      ECrowdWorkerConsistencyDecision::KeepBoundary;
    Result.Failure = Failure;
    Result.Generation = Evidence.Generation;
    Result.DomainKey = Evidence.DomainKey;
    Result.InputEpoch = Evidence.InputEpoch;
    Result.bValid = true;
    return Result;
  }
}

using namespace CrowdWorkerConsistencyDomainsPrivate;

FCrowdWorkerConsistencyEvaluation
FCrowdWorkerConsistencyDomainEvaluator::Evaluate(
  const FCrowdWorkerConsistencyEvidence& Evidence)
{
  if (Evidence.Generation == 0 || Evidence.DomainKey == 0
    || Evidence.EntityCount <= 0)
    return Keep(
      Evidence,
      ECrowdWorkerConsistencyFailure::InvalidEvidence);
  if (!Evidence.bStableMembership)
    return Keep(
      Evidence,
      ECrowdWorkerConsistencyFailure::MissingStableMembership);
  if (Evidence.InputEpoch == 0)
    return Keep(
      Evidence,
      ECrowdWorkerConsistencyFailure::MissingInputEpoch);
  if (Evidence.ExternalDependencyCount != 0)
    return Keep(
      Evidence,
      ECrowdWorkerConsistencyFailure::CrossDomainDependency);
  if (!Evidence.bNetworkSemanticsFrozen)
    return Keep(
      Evidence,
      ECrowdWorkerConsistencyFailure::MissingNetworkSemantics);

  switch (Evidence.Domain)
  {
    case ECrowdWorkerConsistencyDomain::
      ParticleInteractionIsland:
      if (!Evidence.bClosedInteractionBoundary)
        return Keep(
          Evidence,
          ECrowdWorkerConsistencyFailure::
            OpenInteractionBoundary);
      if (Evidence.EnvironmentRevision == 0)
        return Keep(
          Evidence,
          ECrowdWorkerConsistencyFailure::
            MissingEnvironmentRevision);
      break;

    case ECrowdWorkerConsistencyDomain::TargetCohort:
      if (!Evidence.bAtomicPlan)
        return Keep(
          Evidence,
          ECrowdWorkerConsistencyFailure::MissingAtomicPlan);
      if (Evidence.EnvironmentRevision == 0)
        return Keep(
          Evidence,
          ECrowdWorkerConsistencyFailure::
            MissingEnvironmentRevision);
      break;

    case ECrowdWorkerConsistencyDomain::CombatEventBoundary:
      if (!Evidence.bOrderedEvents || Evidence.EventCount <= 0
        || Evidence.FirstEventSequence == 0
        || Evidence.LastEventSequence
          < Evidence.FirstEventSequence
        || Evidence.LastEventSequence
          - Evidence.FirstEventSequence + 1
            != static_cast<uint64>(Evidence.EventCount))
        return Keep(
          Evidence,
          ECrowdWorkerConsistencyFailure::MissingOrderedEvents);
      if (!Evidence.bIdempotencyProven)
        return Keep(
          Evidence,
          ECrowdWorkerConsistencyFailure::MissingIdempotency);
      if (!Evidence.bRollbackProven)
        return Keep(
          Evidence,
          ECrowdWorkerConsistencyFailure::MissingRollbackProof);
      break;
  }

  FCrowdWorkerConsistencyEvaluation Result;
  Result.Domain = Evidence.Domain;
  Result.Decision =
    ECrowdWorkerConsistencyDecision::EligibleForWorkerPatch;
  Result.Failure = ECrowdWorkerConsistencyFailure::None;
  Result.Generation = Evidence.Generation;
  Result.DomainKey = Evidence.DomainKey;
  Result.InputEpoch = Evidence.InputEpoch;
  Result.bValid = true;
  return Result;
}
