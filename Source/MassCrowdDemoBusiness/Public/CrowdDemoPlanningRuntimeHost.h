#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoBusinessPlanner.h"

struct FCrowdDemoPlanningRuntimeEntityFact
{
  FCrowdStableEntityRef EntityRef;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  FVector Facing = FVector::ForwardVector;
  uint32 NavLayer = 0;

  bool IsValid() const;
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoPlanningRuntimeHost
{
public:
  static bool Stage(
    FCrowdBehaviorSourceRuntime& Runtime,
    int64 FixedStepIndex,
    TConstArrayView<FCrowdDemoPlanningRuntimeEntityFact>
      EntityFacts,
    TConstArrayView<FCrowdDemoPlannerDecision> Decisions);
};
