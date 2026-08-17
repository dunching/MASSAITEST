#pragma once

#include "CoreMinimal.h"

struct FCrowdFacingSettings
{
  float FixedStepSeconds = 1.0f / 30.0f;
  float MaximumTurnRateDegreesPerSecond = 360.0f;
  float AutonomousSpeedEpsilonCmps = 1.0f;
  float AngleQuantumDegrees = 0.01f;
};

struct FCrowdFacingInput
{
  int32 AgentId = INDEX_NONE;
  float CurrentYawDegrees = 0.0f;
  FVector2f AutonomousPreferredVelocity = FVector2f::ZeroVector;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  bool bHasTarget = false;
  bool bFinalPositionSettled = false;
};

struct FCrowdFacingResult
{
  int32 AgentId = INDEX_NONE;
  float DesiredYawDegrees = 0.0f;
  float ResolvedYawDegrees = 0.0f;
  float AppliedYawDeltaDegrees = 0.0f;
  bool bFacingTarget = false;
  bool bHeldCurrentYaw = false;
};

struct FCrowdFacingSummary
{
  TArray<FCrowdFacingResult> Results;
  int32 TargetFacingAgentCount = 0;
  int32 AutonomousFacingAgentCount = 0;
  int32 HeldYawAgentCount = 0;
  float MaximumAppliedYawDeltaDegrees = 0.0f;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSCROWDCORE_API FCrowdFacingKernel
{
public:
  static void Resolve(
    TConstArrayView<FCrowdFacingInput> Inputs,
    const FCrowdFacingSettings& Settings,
    FCrowdFacingSummary& OutSummary);
};
