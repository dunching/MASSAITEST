#pragma once

#include "CoreMinimal.h"
#include "CrowdParticleConstraintKernel.h"

struct FCrowdMassParticleWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  FCrowdParticleConstraintEnvironment Environment;
  FCrowdParticleConstraintSettings Settings;
  TArray<FCrowdParticleConstraintAgent> Agents;
  bool bCaptureTrace = false;
};

struct FCrowdMassParticleWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdParticleConstraintPair> Pairs;
  TArray<FCrowdParticleConstraintResult> Results;
  FCrowdParticleConstraintSummary Summary;
  FCrowdParticleConstraintSummary AppliedSummary;
  FCrowdParticleConstraintTrace Trace;
  uint32 AppliedStateHash = 2166136261u;
  uint32 StableHash = 2166136261u;
  uint32 FailureTraceReplayCandidateHash = 2166136261u;
  int32 InteractionIslandCount = 0;
  int32 CellShardCount = 0;
  int32 CrossCellPairCount = 0;
  int32 MaxIslandAgentCount = 0;
  bool bUsedIslandSharding = false;
  bool bUsedMonolithicFallback = false;
  bool bFailureTraceReplayAttempted = false;
  bool bFailureTraceReplayMatched = true;
  bool bFailureTraceReplayValid = false;
  bool bCompleted = false;
};

class MASSCROWDRUNTIME_API FCrowdMassParticleWork
{
public:
  static FCrowdMassParticleWorkOutput Solve(
    const FCrowdMassParticleWorkInput& Input);
};
