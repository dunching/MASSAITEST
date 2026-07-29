#include "CrowdDemoFriendlyLogisticsTestDirector.h"

FCrowdDemoFriendlyDirectorDecision
FCrowdDemoFriendlyLogisticsTestDirector::Evaluate(
  const FCrowdDemoFriendlyDirectorInput& Input)
{
  FCrowdDemoFriendlyDirectorDecision Decision;
  if (Input.TaskState == ECrowdLogisticsTaskState::Created)
  {
    if (!Input.bTransitionDelayElapsed)
      return Decision;
    Decision.Action = Input.UnreachableBackoffCount < 2
      ? ECrowdDemoFriendlyDirectorAction::IncrementBackoff
      : ECrowdDemoFriendlyDirectorAction::
        ClaimCompetitionWinner;
    if (Decision.Action
      == ECrowdDemoFriendlyDirectorAction::IncrementBackoff)
      return Decision;
  }
  else if (Input.TaskState == ECrowdLogisticsTaskState::Picked
    && !Input.bDeathInjected)
  {
    if (Input.PickupObservedFixedStep == INDEX_NONE)
    {
      Decision.Action =
        ECrowdDemoFriendlyDirectorAction::ObservePickup;
      return Decision;
    }
    if (Input.FixedStepIndex
      - Input.PickupObservedFixedStep < 45)
      return Decision;
    Decision.Action =
      ECrowdDemoFriendlyDirectorAction::RequeueDeadCarrier;
  }
  else if (Input.TaskState
      == ECrowdLogisticsTaskState::Requeued
    && !Input.bFallbackApplied)
  {
    Decision.Action =
      ECrowdDemoFriendlyDirectorAction::ApplyFallbackSink;
  }
  else if (Input.TaskState
    == ECrowdLogisticsTaskState::Requeued)
  {
    Decision.Action =
      ECrowdDemoFriendlyDirectorAction::ClaimRecoveryCarrier;
  }
  Decision.bRunCancellationFixture =
    Input.CancellationCount == 0
    && Input.CancellationTaskState
      == ECrowdLogisticsTaskState::Created;
  return Decision;
}
