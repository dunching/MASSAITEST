#include "Mass/CrowdDemoOpenSpawnStateTranslation.h"

bool FCrowdDemoOpenSpawnStateTranslation::Translate(
  const ECrowdDemoOpenSpawnRelaxationPhase Phase,
  const FCrowdDemoOpenSpawnRelaxationAgentState& Agent,
  const int32 RemovedParticleAgentId,
  ECrowdWorkerLifecyclePhase& OutLifecycle,
  ECrowdSemanticBehaviorState& OutBehavior,
  const ECrowdWorkerLifecyclePhase* CurrentLifecycle)
{
  OutLifecycle = ECrowdWorkerLifecyclePhase::SpawnPending;
  OutBehavior = ECrowdSemanticBehaviorState::Waiting;
  if (Agent.AgentId == INDEX_NONE
    || Agent.FormationIndex == INDEX_NONE
    || Phase > ECrowdDemoOpenSpawnRelaxationPhase::Failed)
    return false;

  const auto SetStagedLifecycle = [&]()
  {
    OutLifecycle = CurrentLifecycle
        && (*CurrentLifecycle == ECrowdWorkerLifecyclePhase::Active
          || *CurrentLifecycle
            == ECrowdWorkerLifecyclePhase::Suspended)
      ? ECrowdWorkerLifecyclePhase::Suspended
      : ECrowdWorkerLifecyclePhase::SpawnPending;
  };

  switch (Phase)
  {
  case ECrowdDemoOpenSpawnRelaxationPhase::Staging:
    SetStagedLifecycle();
    return !Agent.bParticleActive;

  case ECrowdDemoOpenSpawnRelaxationPhase::BatchActivation:
  case ECrowdDemoOpenSpawnRelaxationPhase::SourceInsertion:
    if (Agent.bParticleActive)
    {
      OutLifecycle = ECrowdWorkerLifecyclePhase::Active;
      OutBehavior = ECrowdSemanticBehaviorState::Relaxing;
    }
    else
      SetStagedLifecycle();
    return true;

  case ECrowdDemoOpenSpawnRelaxationPhase::
      PropagationAndInsertSettle:
  case ECrowdDemoOpenSpawnRelaxationPhase::Removal:
    OutLifecycle = ECrowdWorkerLifecyclePhase::Active;
    OutBehavior = ECrowdSemanticBehaviorState::Relaxing;
    return Agent.bParticleActive;

  case ECrowdDemoOpenSpawnRelaxationPhase::PostRemovalSettle:
    OutLifecycle = ECrowdWorkerLifecyclePhase::Active;
    OutBehavior = Agent.AgentId == RemovedParticleAgentId
      ? ECrowdSemanticBehaviorState::Waiting
      : ECrowdSemanticBehaviorState::Settling;
    return (Agent.AgentId == RemovedParticleAgentId)
      != Agent.bParticleActive;

  case ECrowdDemoOpenSpawnRelaxationPhase::Completed:
    // T1's old "removed" Agent remains a valid entity. Particle participation
    // is intentionally left to a later slice, so Lifecycle Removed is wrong.
    OutLifecycle = ECrowdWorkerLifecyclePhase::Active;
    OutBehavior = ECrowdSemanticBehaviorState::Waiting;
    return true;

  case ECrowdDemoOpenSpawnRelaxationPhase::Failed:
    if (Agent.bParticleActive)
      OutLifecycle = ECrowdWorkerLifecyclePhase::Suspended;
    else
      SetStagedLifecycle();
    OutBehavior = ECrowdSemanticBehaviorState::Waiting;
    return true;

  default:
    return false;
  }
}
