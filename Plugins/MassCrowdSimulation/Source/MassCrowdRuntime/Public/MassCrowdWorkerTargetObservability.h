#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerResultApply.h"
#include "MassCrowdWorkerTargetDomain.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetCohortObservation
{
  uint32 CohortKey = 0;
  uint32 TopologyRevision = 0;
  int32 TargetRevision = INDEX_NONE;
  int32 PlanEpoch = 0;
  int32 PlanBuildFixedStep = INDEX_NONE;
  uint32 FeasibleGraphHash = 0;
  int32 FeasibleCellCount = 0;
  int32 EdgeCount = 0;
  int32 FeasibleRegionCount = 0;
  int32 FeasibleRegionCoverageCount = 0;
  int32 CurrentTerminalPopulation = 0;
  int32 MaximumRegionPopulation = 0;
  int32 DesiredPopulationTotal = 0;
  uint32 MembershipHash = 0;
  uint32 ExternalPopulationHash = 0;
  uint32 TransportHash = 0;
  int32 RoutedAgentCount = 0;
  int32 PlanUnroutedAgentCount = 0;
  int32 TotalFeasibleCapacity = 0;
  int32 AssignablePopulation = 0;
  int32 OverflowPopulation = 0;
  int32 ActiveClaimCount = 0;
  int32 CompletedTransitionCount = 0;
  int32 ReleasedClaimCount = 0;
  int32 OverbookedCellCount = 0;
  uint32 ExecutionHash = 0;
  uint32 GuidanceHash = 0;
  int32 TargetStateCount = 0;
  int32 UnroutedTargetStateCount = 0;
  int32 CapacityHoldTargetStateCount = 0;
  FCrowdStableEntityRef FirstUnroutedEntityRef;
  bool bValid = false;
};

// Read-only diagnostic projection of the authoritative Target and
// TargetCohort fields retained by Worker ResultApply. It owns no simulation
// state and never writes through the proxy.
struct MASSCROWDRUNTIME_API FCrowdWorkerTargetObservation
{
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 LastAppliedInputSequence = 0;
  uint64 PublishSequence = 0;
  int32 TargetRevision = INDEX_NONE;
  int32 ExpectedTargetAgentCount = 0;
  int32 TargetAgentCount = 0;
  int32 ValidTargetStateCount = 0;
  int32 UnroutedTargetStateCount = 0;
  int32 TotalFeasibleCapacity = 0;
  int32 AssignablePopulation = 0;
  int32 OverflowPopulation = 0;
  int32 ActiveClaimCount = 0;
  int32 CompletedTransitionCount = 0;
  int32 ReleasedClaimCount = 0;
  int32 OverbookedCellCount = 0;
  int32 CapacityHoldTargetStateCount = 0;
  FCrowdStableEntityRef FirstInvalidEntityRef;
  FCrowdStableEntityRef FirstUnroutedEntityRef;
  TArray<FCrowdWorkerTargetCohortObservation> Cohorts;
  uint64 StableHash = 0;
  bool bValid = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTargetObserver
{
public:
  static bool Build(
    const FCrowdWorkerResultApplyProxy& Proxy,
    int32 ExpectedTargetAgentCount,
    FCrowdWorkerTargetObservation& OutObservation);
};
