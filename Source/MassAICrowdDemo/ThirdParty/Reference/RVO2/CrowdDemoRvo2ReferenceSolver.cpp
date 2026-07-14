/*
 * Adapted from RVO2 src/Agent.cc linearProgram1/linearProgram2.
 * Upstream: https://github.com/snape/RVO2
 * Commit: b577921d2bc1281a6b721c2d4778f397d37da97d
 * SPDX-FileCopyrightText: 2008 University of North Carolina at Chapel Hill
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modifications: replaced RVO Vector2/Line and std::vector with FVector2f and
 * TArray; removed Simulator/Agent/KdTree/OpenMP and direction optimization;
 * added deterministic stable-order normalization and project result types.
 */

#include "ThirdParty/Reference/RVO2/CrowdDemoRvo2ReferenceSolver.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  constexpr float RvoEpsilon = 0.00001f;

  struct FReferenceLine
  {
    FVector2f Point = FVector2f::ZeroVector;
    FVector2f Direction = FVector2f::ZeroVector;
    int32 StableOrder = INDEX_NONE;
  };

  float Det(const FVector2f A, const FVector2f B)
  {
    return A.X * B.Y - A.Y * B.X;
  }

  bool LinearProgram1(
    const TConstArrayView<FReferenceLine> Lines,
    const int32 LineIndex,
    const float Radius,
    const FVector2f OptVelocity,
    FVector2f& Result)
  {
    const FReferenceLine& Active = Lines[LineIndex];
    const float DotProduct = FVector2f::DotProduct(Active.Point, Active.Direction);
    const float Discriminant = DotProduct * DotProduct + Radius * Radius - Active.Point.SizeSquared();
    if (Discriminant < 0.0f) return false;
    const float Root = FMath::Sqrt(Discriminant);
    float Left = -DotProduct - Root;
    float Right = -DotProduct + Root;
    for (int32 Index = 0; Index < LineIndex; ++Index)
    {
      const float Denominator = Det(Active.Direction, Lines[Index].Direction);
      const float Numerator = Det(Lines[Index].Direction, Active.Point - Lines[Index].Point);
      if (FMath::Abs(Denominator) <= RvoEpsilon)
      {
        if (Numerator < 0.0f) return false;
        continue;
      }
      const float T = Numerator / Denominator;
      if (Denominator >= 0.0f) Right = FMath::Min(Right, T);
      else Left = FMath::Max(Left, T);
      if (Left > Right) return false;
    }
    const float PreferredT = FVector2f::DotProduct(Active.Direction, OptVelocity - Active.Point);
    Result = Active.Point + FMath::Clamp(PreferredT, Left, Right) * Active.Direction;
    return true;
  }

  int32 LinearProgram2(
    const TConstArrayView<FReferenceLine> Lines,
    const float Radius,
    const FVector2f OptVelocity,
    FVector2f& Result)
  {
    Result = OptVelocity.SizeSquared() > Radius * Radius
      ? OptVelocity.GetSafeNormal() * Radius
      : OptVelocity;
    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
      if (Det(Lines[Index].Direction, Lines[Index].Point - Result) <= 0.0f) continue;
      const FVector2f Previous = Result;
      if (!LinearProgram1(Lines, Index, Radius, OptVelocity, Result))
      {
        Result = Previous;
        return Index;
      }
    }
    return Lines.Num();
  }
}

FCrowdDemoOrcaContinuousSolveResult FCrowdDemoRvo2ReferenceSolver::Solve(
  const FCrowdDemoOrcaContinuousSolveInput& Input)
{
  TArray<FReferenceLine> Lines;
  Lines.Reserve(Input.HalfPlanes.Num());
  for (const FCrowdDemoOrcaHalfPlane& HalfPlane : Input.HalfPlanes)
  {
    const FVector2f Normal = HalfPlane.Normal.GetSafeNormal();
    FReferenceLine& Line = Lines.AddDefaulted_GetRef();
    Line.Point = HalfPlane.Point - Normal * Input.BehaviorEpsilonCmps;
    // RVO2 considers det(Direction, Point - Velocity) <= 0 feasible.
    Line.Direction = FVector2f(Normal.Y, -Normal.X);
    Line.StableOrder = HalfPlane.StableOrder;
  }
  Lines.Sort([](const FReferenceLine& A, const FReferenceLine& B)
  {
    return A.StableOrder < B.StableOrder;
  });
  FCrowdDemoOrcaContinuousSolveResult Result;
  Result.FailedConstraintIndex = LinearProgram2(
    Lines, Input.MaxSpeedCmps, Input.PreferredVelocity, Result.Velocity);
  Result.bSatisfiesAllHalfPlanes = Result.FailedConstraintIndex == Lines.Num()
    && FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(Input, Result.Velocity);
  Result.Status = Result.bSatisfiesAllHalfPlanes
    ? (Result.Velocity.Equals(Input.PreferredVelocity, 0.001f)
      ? ECrowdDemoOrcaSolveStatus::PreferredFeasible
      : ECrowdDemoOrcaSolveStatus::ExactFeasible)
    : ECrowdDemoOrcaSolveStatus::ProvenInfeasible;
  return Result;
}

#endif
