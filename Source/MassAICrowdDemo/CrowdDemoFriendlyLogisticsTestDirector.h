#pragma once

#include "CoreMinimal.h"
#include "MassCrowdLogistics.h"

enum class ECrowdDemoFriendlyDirectorAction : uint8
{
  None = 0,
  IncrementBackoff,
  ClaimCompetitionWinner,
  ObservePickup,
  RequeueDeadCarrier,
  ApplyFallbackSink,
  ClaimRecoveryCarrier
};

struct FCrowdDemoFriendlyDirectorInput
{
  ECrowdLogisticsTaskState TaskState =
    ECrowdLogisticsTaskState::Created;
  ECrowdLogisticsTaskState CancellationTaskState =
    ECrowdLogisticsTaskState::Created;
  int32 FixedStepIndex = 0;
  int32 PickupObservedFixedStep = INDEX_NONE;
  int32 UnreachableBackoffCount = 0;
  int32 CancellationCount = 0;
  bool bTransitionDelayElapsed = false;
  bool bDeathInjected = false;
  bool bFallbackApplied = false;
};

struct FCrowdDemoFriendlyDirectorDecision
{
  ECrowdDemoFriendlyDirectorAction Action =
    ECrowdDemoFriendlyDirectorAction::None;
  bool bRunCancellationFixture = false;
};

class FCrowdDemoFriendlyLogisticsTestDirector
{
public:
  static FCrowdDemoFriendlyDirectorDecision Evaluate(
    const FCrowdDemoFriendlyDirectorInput& Input);
};
