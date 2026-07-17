#pragma once

#include "CoreMinimal.h"

struct FCrowdDemoVelocityHalfPlane
{
  FVector2f Point = FVector2f::ZeroVector;
  FVector2f Normal = FVector2f::ZeroVector;
  int32 StableOrder = INDEX_NONE;
};

enum class ECrowdDemoVelocityHalfPlaneSolveStatus : uint8
{
  PreferredFeasible,
  ExactFeasible,
  ProvenInfeasible,
  InvalidInput,
  NumericalFailure,
};

enum class ECrowdDemoVelocityQuantizationResult : uint8
{
  CenterFeasible,
  NeighborhoodRecovered,
  GeometryRecovered,
  NoSolution,
};

struct FCrowdDemoVelocityHalfPlaneSettings
{
  float MaxSpeedCmps = 0.0f;
  float BehaviorEpsilonCmps = 0.1f;
  float VelocityQuantumCmps = 1.0f;
};

struct FCrowdDemoVelocityHalfPlaneInput
{
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FCrowdDemoVelocityHalfPlaneSettings Settings;
  TArray<FCrowdDemoVelocityHalfPlane> HalfPlanes;
};

struct FCrowdDemoVelocityHalfPlaneResult
{
  ECrowdDemoVelocityHalfPlaneSolveStatus Status =
    ECrowdDemoVelocityHalfPlaneSolveStatus::InvalidInput;
  ECrowdDemoVelocityQuantizationResult Quantization =
    ECrowdDemoVelocityQuantizationResult::NoSolution;
  FVector2f ContinuousVelocity = FVector2f::ZeroVector;
  FVector2f QuantizedVelocity = FVector2f::ZeroVector;
  bool bContinuousValid = false;
  bool bQuantizedValid = false;
};

struct FCrowdDemoVelocityHalfPlaneNumericalSummary
{
  int32 ParallelBranchCount = 0;
  int32 NearParallelBranchCount = 0;
  int32 RedundantParallelCount = 0;
  int32 StricterParallelCount = 0;
  int32 TrueParallelContradictionCount = 0;
  int32 NumericalToleranceAcceptanceCount = 0;
};

class MASSAICROWDDEMO_API FCrowdDemoVelocityHalfPlaneKernel
{
public:
  static bool CanonicalizeInput(
    const FCrowdDemoVelocityHalfPlaneInput& Input,
    FCrowdDemoVelocityHalfPlaneInput& OutCanonical);

  static FCrowdDemoVelocityHalfPlaneResult Solve(
    const FCrowdDemoVelocityHalfPlaneInput& Input,
    FCrowdDemoVelocityHalfPlaneNumericalSummary* OutNumericalSummary = nullptr);

  static bool SolveContinuous(
    const FCrowdDemoVelocityHalfPlaneInput& Input,
    FVector2f& OutVelocity,
    FCrowdDemoVelocityHalfPlaneNumericalSummary* OutNumericalSummary = nullptr);

  static bool ValidateVelocity(
    const FCrowdDemoVelocityHalfPlaneInput& Input,
    FVector2f Velocity);

  static ECrowdDemoVelocityQuantizationResult QuantizeAndValidate(
    const FCrowdDemoVelocityHalfPlaneInput& Input,
    FVector2f ContinuousVelocity,
    FVector2f& OutVelocity);
};
