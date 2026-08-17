#pragma once

#include "CoreMinimal.h"
#include "MassCrowdMovementFinalizeWork.h"
#include "MassCrowdMovementPredictWork.h"
#include "MassCrowdParticleWork.h"

struct FCrowdMassParticlePublishRecord
{
  int32 AgentId = INDEX_NONE;
  FCrowdParticleConstraintResult Result;
  bool bParticleActive = true;
  bool bUsedSolverResult = false;
  bool bAppliedStateSample = false;
};

struct FCrowdMassParticlePublishPlan
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMassParticlePublishRecord> Records;
  TArray<FCrowdParticleConstraintResult> PreparedResults;
  TArray<FCrowdMassFinalKinematicState> FinalKinematics;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdMassParticlePipelineWorkInput
{
  FCrowdMassParticleWorkInput Particle;
  FCrowdMassBoundarySnapshot Snapshot;
  TArray<FCrowdMassPredictedMovement> PredictedMovements;
  int32 ExpectedExternalAgentCount = 0;
};

struct FCrowdMassParticlePipelineWorkOutput
{
  FCrowdMassParticleWorkOutput Particle;
  FCrowdMassParticlePublishPlan PublishPlan;
  uint32 StableHash = 2166136261u;
  bool bCompleted = false;
};

class MASSCROWDRUNTIME_API FCrowdMassParticlePipelineWork
{
public:
  static FCrowdMassParticlePipelineWorkOutput Run(
    const FCrowdMassParticlePipelineWorkInput& Input);
};
