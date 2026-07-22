#pragma once

#include "CoreMinimal.h"

struct FCrowdMassMovementPredictAgent
{
  int32 AgentId = INDEX_NONE;
  FVector StartPosition = FVector::ZeroVector;
  FVector AutonomousPreferredVelocity = FVector::ZeroVector;
  FVector LocalVelocity = FVector::ZeroVector;
  float MaximumSpeedCmps = 0.0f;
  bool bUseLocalVelocity = false;
  bool bLocalVelocityValid = false;
  bool bFreezeAtBoundaryLocation = false;
  FVector BoundaryLocation = FVector::ZeroVector;
  bool bVerticalOverride = false;
  float ProposedZ = 0.0f;
  float VerticalVelocityCmps = 0.0f;
  bool bParticleActive = true;
};

struct FCrowdMassPredictedMovement
{
  int32 AgentId = INDEX_NONE;
  FVector StartPosition = FVector::ZeroVector;
  FVector PredictedPosition = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  bool bParticleActive = true;
  bool bValid = false;
};

struct FCrowdMassMovementPredictWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  float FixedStepSeconds = 0.0f;
  TArray<FCrowdMassMovementPredictAgent> Agents;
};

struct FCrowdMassMovementPredictWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMassPredictedMovement> Results;
  uint32 StableHash = 2166136261u;
  bool bCompleted = false;
};

class MASSCROWDRUNTIME_API FCrowdMassMovementPredictWork
{
public:
  static FCrowdMassMovementPredictWorkOutput Predict(
    const FCrowdMassMovementPredictWorkInput& Input);
};
