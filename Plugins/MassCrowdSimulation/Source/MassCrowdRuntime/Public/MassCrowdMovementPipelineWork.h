#pragma once

#include "CoreMinimal.h"
#include "MassCrowdGuidanceWork.h"
#include "MassCrowdLocalPredictiveWork.h"
#include "MassCrowdMovementPredictWork.h"

// GT-owned facts which are not part of the reusable boundary snapshot. They
// are gathered once, sorted by AgentId, and remain immutable for the WORK task.
struct FCrowdMassMovementPipelineAgentOverlay
{
  int32 AgentId = INDEX_NONE;
  int32 PreviousBlockedAgeSteps = 0;
  float MaximumSpeedCmps = 0.0f;
  bool bFreezeAtBoundaryLocation = false;
  FVector BoundaryLocation = FVector::ZeroVector;
  bool bVerticalOverride = false;
  float ProposedZ = 0.0f;
  float VerticalVelocityCmps = 0.0f;
  bool bParticleActive = true;
};

struct FCrowdMassMovementPipelineWorkInput
{
  FCrowdMassGuidanceWorkInput Guidance;
  FCrowdSharedFlowFieldConfig Environment;
  FCrowdLocalPredictiveSettings LocalPredictiveSettings;
  TArray<FCrowdLocalPredictiveGrantState> PreviousGrantStates;
  TArray<FCrowdMassMovementPipelineAgentOverlay> AgentOverlays;
  float FixedStepSeconds = 0.0f;
  bool bRunLocalPredictive = false;
  bool bCaptureLocalPredictiveDiagnostic = false;
};

struct FCrowdMassMovementPipelineWorkOutput
{
  FCrowdMassGuidanceWorkOutput Guidance;
  FCrowdMassLocalPredictiveWorkOutput LocalPredictive;
  FCrowdMassMovementPredictWorkOutput MovementPredict;
  TArray<FCrowdLocalPredictiveAgent> LocalPredictiveAgents;
  uint32 StableHash = 2166136261u;
  float GuidanceWorkMilliseconds = 0.0f;
  float LocalPredictiveWorkMilliseconds = 0.0f;
  float MovementPredictWorkMilliseconds = 0.0f;
  bool bCompleted = false;
};

// One ThreadPool dispatch owns the adjacent algorithm chain. It does not touch
// UObject, Mass EntityManager, or mutable pipeline state.
class MASSCROWDRUNTIME_API FCrowdMassMovementPipelineWork
{
public:
  static FCrowdMassMovementPipelineWorkOutput Run(
    const FCrowdMassMovementPipelineWorkInput& Input);
};
