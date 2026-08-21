#pragma once

#include "CoreMinimal.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "MassCrowdStandardSources.h"
#include "MassCrowdWorkerAgentState.h"

// Stateless T1 fixture adapter. The output types are generic Worker contracts;
// no T1 phase or scenario identity crosses the authority boundary.
class MASSAICROWDDEMO_API FCrowdDemoOpenSpawnStateTranslation
{
public:
  static bool Translate(
    ECrowdDemoOpenSpawnRelaxationPhase Phase,
    const FCrowdDemoOpenSpawnRelaxationAgentState& Agent,
    int32 RemovedParticleAgentId,
    ECrowdWorkerLifecyclePhase& OutLifecycle,
    ECrowdSemanticBehaviorState& OutBehavior,
    const ECrowdWorkerLifecyclePhase* CurrentLifecycle = nullptr);
};
