#pragma once

#include "CoreMinimal.h"

struct FCrowdVelocityHalfPlane
{
  FVector2f Point = FVector2f::ZeroVector;
  FVector2f Normal = FVector2f::ZeroVector;
  int32 StableOrder = INDEX_NONE;
};

enum class ECrowdVelocityHalfPlaneSolveStatus : uint8
{
  PreferredFeasible,
  ExactFeasible,
  ProvenInfeasible,
  InvalidInput,
  NumericalFailure,
};

enum class ECrowdVelocityQuantizationResult : uint8
{
  CenterFeasible,
  NeighborhoodRecovered,
  GeometryRecovered,
  NoSolution,
};

struct FCrowdVelocityHalfPlaneSettings
{
  float MaxSpeedCmps = 0.0f;
  float BehaviorEpsilonCmps = 0.1f;
  float VelocityQuantumCmps = 1.0f;
};

struct FCrowdVelocityHalfPlaneInput
{
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FCrowdVelocityHalfPlaneSettings Settings;
  TArray<FCrowdVelocityHalfPlane> HalfPlanes;
};

struct FCrowdVelocityHalfPlaneResult
{
  ECrowdVelocityHalfPlaneSolveStatus Status =
    ECrowdVelocityHalfPlaneSolveStatus::InvalidInput;
  ECrowdVelocityQuantizationResult Quantization =
    ECrowdVelocityQuantizationResult::NoSolution;
  FVector2f ContinuousVelocity = FVector2f::ZeroVector;
  FVector2f QuantizedVelocity = FVector2f::ZeroVector;
  bool bContinuousValid = false;
  bool bQuantizedValid = false;
};

struct FCrowdVelocityHalfPlaneNumericalSummary
{
  int32 ParallelBranchCount = 0;
  int32 NearParallelBranchCount = 0;
  int32 RedundantParallelCount = 0;
  int32 StricterParallelCount = 0;
  int32 TrueParallelContradictionCount = 0;
  int32 NumericalToleranceAcceptanceCount = 0;
};

class MASSCROWDCORE_API FCrowdVelocityHalfPlaneKernel
{
public:
  static bool CanonicalizeInput(
    const FCrowdVelocityHalfPlaneInput& Input,
    FCrowdVelocityHalfPlaneInput& OutCanonical);

  static FCrowdVelocityHalfPlaneResult Solve(
    const FCrowdVelocityHalfPlaneInput& Input,
    FCrowdVelocityHalfPlaneNumericalSummary* OutNumericalSummary = nullptr);

  static bool SolveContinuous(
    const FCrowdVelocityHalfPlaneInput& Input,
    FVector2f& OutVelocity,
    FCrowdVelocityHalfPlaneNumericalSummary* OutNumericalSummary = nullptr);

  static bool ValidateVelocity(
    const FCrowdVelocityHalfPlaneInput& Input,
    FVector2f Velocity);

  static ECrowdVelocityQuantizationResult QuantizeAndValidate(
    const FCrowdVelocityHalfPlaneInput& Input,
    FVector2f ContinuousVelocity,
    FVector2f& OutVelocity);
};
