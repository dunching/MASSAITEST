#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoBusinessPlanner.h"

struct FCrowdDemoScenarioAgentFact
{
  FCrowdStableEntityRef EntityRef;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  int32 Health = 100;
  uint32 Revision = 1;

  bool IsValid() const;
};

struct FCrowdDemoFriendlyLogisticsPlanningFact
{
  FCrowdStableEntityRef EntityRef;
  FCrowdStableEntityRef TaskRef;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  FVector SourceLocation = FVector::ZeroVector;
  FVector SinkLocation = FVector::ZeroVector;
  int64 LastLogisticsFixedStep = -1000;
  uint32 TransitionRevision = 1;
  bool bCarrying = false;

  bool IsValid() const;
};

class MASSCROWDDEMOBUSINESS_API
  FCrowdDemoBusinessScenarioContract
{
public:
  static bool EvaluateNoBusiness(
    int64 FixedStepIndex,
    uint64& OutDecisionHash);

  static bool EvaluateAssigned(
    FCrowdDemoBusinessScenarioId ScenarioId,
    FCrowdDemoBusinessPlannerId PlannerId,
    int64 FixedStepIndex,
    uint64 FactRevision,
    TConstArrayView<FCrowdDemoScenarioAgentFact> Agents,
    FCrowdDemoPlannerDecisionBatch& OutBatch);

  static bool EvaluateFriendlyLogistics(
    int64 FixedStepIndex,
    uint64 FactRevision,
    const FCrowdDemoFriendlyLogisticsPlanningFact& Fact,
    FCrowdDemoPlannerDecision& OutDecision);
};
