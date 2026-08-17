#pragma once

#include "CoreMinimal.h"

struct FCrowdDemoTargetFact
{
  int32 TargetId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  int32 MotionStep = 0;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  float YawDegrees = 0.0f;
  float PhysicalRadiusCm = 0.0f;
};

struct FCrowdDemoTargetFactKernel
{
  static FCrowdDemoTargetFact BuildLinearMotionFact(
    int32 TargetId,
    int32 TargetRevision,
    int32 MotionStep,
    const FVector2f& InitialLocation,
    const FVector2f& LinearVelocity,
    float InitialYawDegrees,
    float YawRateDegreesPerSecond,
    float PhysicalRadiusCm,
    float FixedStepSeconds,
    float PositionQuantumCm,
    float VelocityQuantumCmps);

  static FCrowdDemoTargetFact BuildReflectedLinearMotionFact(
    int32 TargetId,
    int32 TargetRevision,
    int32 MotionStep,
    const FVector2f& InitialLocation,
    const FVector2f& LinearVelocity,
    const FVector2f& MotionBoundsMin,
    const FVector2f& MotionBoundsMax,
    float InitialYawDegrees,
    float YawRateDegreesPerSecond,
    float PhysicalRadiusCm,
    float FixedStepSeconds,
    float PositionQuantumCm,
    float VelocityQuantumCmps);
};
