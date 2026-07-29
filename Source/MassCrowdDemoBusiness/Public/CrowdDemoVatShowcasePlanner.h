#pragma once

#include "CoreMinimal.h"

enum class ECrowdDemoVatPlannedState : uint8
{
  Idle = 0,
  Moving = 1,
  Attacking = 2
};

enum class ECrowdDemoVatInjectedHit : uint8
{
  None = 0,
  Knockback,
  KnockUp,
  Death
};

struct FCrowdDemoVatMotionSettings
{
  int32 FirstMovingFormationIndex = 4;
  int32 MovingAgentCount = 4;
  int32 HalfCycleFixedSteps = 6;
  float MoveSpeedCmps = 60.0f;
  float MaximumAnchorOffsetCm = 12.0f;
};

struct FCrowdDemoVatMotionDecision
{
  bool bValid = false;
  bool bMovingGroup = false;
  FVector DesiredVelocity = FVector::ZeroVector;
  FVector DesiredLocation = FVector::ZeroVector;
  uint32 StableHash = 2166136261u;
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoVatShowcasePlanner
{
public:
  static ECrowdDemoVatPlannedState ResolveInitialState(
    int32 FormationIndex);

  static ECrowdDemoVatInjectedHit SelectInjectedHit(
    int32 FormationIndex,
    int32 FixedStepIndex);

  static FCrowdDemoVatMotionDecision BuildMotion(
    int32 FormationIndex,
    int32 FixedStepIndex,
    const FVector& CurrentLocation,
    const FVector& AnchorLocation,
    const FCrowdDemoVatMotionSettings& Settings = {});
};
