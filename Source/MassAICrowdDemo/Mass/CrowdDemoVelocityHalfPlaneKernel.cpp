#include "Mass/CrowdDemoVelocityHalfPlaneKernel.h"

#include <limits>

namespace
{
struct FNumericalTolerances
{
  double ParallelAngularTolerance = 0.0;
  double ResidualToleranceCmps = 0.0;
  double ParameterTolerance = 0.0;
};

struct FLineInterval
{
  bool bFeasible = false;
  float MinimumT = 0.0f;
  float MaximumT = 0.0f;
};

FNumericalTolerances ComputeTolerances(const double ScaleValue)
{
  const double FloatEpsilon = static_cast<double>(std::numeric_limits<float>::epsilon());
  const double Scale = FMath::Max(1.0, FMath::Abs(ScaleValue));
  FNumericalTolerances Result;
  Result.ParallelAngularTolerance = 64.0 * FloatEpsilon;
  Result.ResidualToleranceCmps = FMath::Max(1.0e-6, 16.0 * FloatEpsilon * Scale);
  Result.ParameterTolerance = FMath::Max(1.0e-6, 8.0 * FloatEpsilon * Scale);
  return Result;
}

FVector2f Quantize(const FVector2f Value, const float QuantumValue)
{
  const float Quantum = FMath::Max(0.001f, QuantumValue);
  return FVector2f(
    FMath::RoundToFloat(Value.X / Quantum) * Quantum,
    FMath::RoundToFloat(Value.Y / Quantum) * Quantum);
}

FVector2f ClampSpeed(const FVector2f Value, const float MaxSpeed)
{
  const float Size = Value.Size();
  return Size > MaxSpeed && Size > SMALL_NUMBER ? Value * (MaxSpeed / Size) : Value;
}

bool InsideSpeedCircle(
  const FVector2f Value, const float MaxSpeed, const float Epsilon)
{
  return Value.SizeSquared() <= FMath::Square(MaxSpeed + Epsilon);
}

bool Satisfies(
  const FVector2f Value,
  const TConstArrayView<FCrowdDemoVelocityHalfPlane> HalfPlanes,
  const float Epsilon,
  const float VelocityScale)
{
  const FNumericalTolerances Tolerances = ComputeTolerances(VelocityScale);
  for (const FCrowdDemoVelocityHalfPlane& HalfPlane : HalfPlanes)
  {
    const double DeltaX = static_cast<double>(Value.X) - HalfPlane.Point.X;
    const double DeltaY = static_cast<double>(Value.Y) - HalfPlane.Point.Y;
    const double Scalar = DeltaX * HalfPlane.Normal.X + DeltaY * HalfPlane.Normal.Y;
    if (Scalar < -static_cast<double>(Epsilon) - Tolerances.ResidualToleranceCmps)
      return false;
  }
  return true;
}

FLineInterval ComputeLineCircleInterval(
  const FVector2f LinePoint,
  const FVector2f LineDirection,
  const float MaxSpeed)
{
  FLineInterval Result;
  const double DirectionLengthSquared = static_cast<double>(LineDirection.X) * LineDirection.X
    + static_cast<double>(LineDirection.Y) * LineDirection.Y;
  if (!FMath::IsFinite(DirectionLengthSquared)
    || DirectionLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
    return Result;
  const double B = static_cast<double>(LinePoint.X) * LineDirection.X
    + static_cast<double>(LinePoint.Y) * LineDirection.Y;
  const double C = static_cast<double>(LinePoint.X) * LinePoint.X
    + static_cast<double>(LinePoint.Y) * LinePoint.Y
    - static_cast<double>(MaxSpeed) * MaxSpeed;
  const double Discriminant = B * B - DirectionLengthSquared * C;
  const double VelocityScale = FMath::Max(
    static_cast<double>(MaxSpeed), FMath::Sqrt(FMath::Max(0.0, C)));
  const FNumericalTolerances Tolerances = ComputeTolerances(VelocityScale);
  const double DiscriminantTolerance = 2.0 * FMath::Max(1.0, VelocityScale)
    * Tolerances.ResidualToleranceCmps * DirectionLengthSquared;
  if (!FMath::IsFinite(Discriminant) || Discriminant < -DiscriminantTolerance)
    return Result;
  const double Root = FMath::Sqrt(FMath::Max(0.0, Discriminant));
  Result.bFeasible = true;
  Result.MinimumT = static_cast<float>((-B - Root) / DirectionLengthSquared);
  Result.MaximumT = static_cast<float>((-B + Root) / DirectionLengthSquared);
  return Result;
}

bool ClipLineInterval(
  const FVector2f LinePoint,
  const FVector2f LineDirection,
  const FCrowdDemoVelocityHalfPlane& HalfPlane,
  const float Epsilon,
  float& InOutMinimumT,
  float& InOutMaximumT,
  FCrowdDemoVelocityHalfPlaneNumericalSummary* OutSummary)
{
  const double Denominator = static_cast<double>(LineDirection.X) * HalfPlane.Normal.X
    + static_cast<double>(LineDirection.Y) * HalfPlane.Normal.Y;
  const double DeltaX = static_cast<double>(LinePoint.X) - HalfPlane.Point.X;
  const double DeltaY = static_cast<double>(LinePoint.Y) - HalfPlane.Point.Y;
  const double Numerator = -static_cast<double>(Epsilon)
    - (DeltaX * HalfPlane.Normal.X + DeltaY * HalfPlane.Normal.Y);
  if (!FMath::IsFinite(Denominator) || !FMath::IsFinite(Numerator)) return false;
  const double RelevantScale = FMath::Max3(
    FMath::Max(FMath::Abs(static_cast<double>(InOutMinimumT)),
      FMath::Abs(static_cast<double>(InOutMaximumT))),
    FMath::Max(FMath::Abs(static_cast<double>(LinePoint.X)),
      FMath::Abs(static_cast<double>(LinePoint.Y))),
    FMath::Max(FMath::Abs(static_cast<double>(HalfPlane.Point.X)),
      FMath::Abs(static_cast<double>(HalfPlane.Point.Y))));
  const FNumericalTolerances Tolerances = ComputeTolerances(RelevantScale);
  if (FMath::Abs(Denominator) <= Tolerances.ParallelAngularTolerance)
  {
    if (OutSummary)
    {
      ++OutSummary->ParallelBranchCount;
      if (FMath::Abs(Denominator) > 0.0) ++OutSummary->NearParallelBranchCount;
    }
    if (Numerator <= 0.0)
    {
      if (OutSummary) ++OutSummary->RedundantParallelCount;
      return true;
    }
    if (Numerator <= Tolerances.ResidualToleranceCmps)
    {
      if (OutSummary)
      {
        ++OutSummary->RedundantParallelCount;
        ++OutSummary->StricterParallelCount;
        ++OutSummary->NumericalToleranceAcceptanceCount;
      }
      return true;
    }
    if (OutSummary) ++OutSummary->TrueParallelContradictionCount;
    return false;
  }
  const double Bound = Numerator / Denominator;
  if (!FMath::IsFinite(Bound)) return false;
  if (Denominator > 0.0)
    InOutMinimumT = static_cast<float>(FMath::Max(static_cast<double>(InOutMinimumT), Bound));
  else
    InOutMaximumT = static_cast<float>(FMath::Min(static_cast<double>(InOutMaximumT), Bound));
  return static_cast<double>(InOutMinimumT)
    <= static_cast<double>(InOutMaximumT) + Tolerances.ParameterTolerance;
}

bool SolveOnBoundary(
  const int32 ActiveIndex,
  const FVector2f Preferred,
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  FVector2f& OutVelocity,
  FCrowdDemoVelocityHalfPlaneNumericalSummary* OutSummary)
{
  const auto& Settings = Input.Settings;
  const FCrowdDemoVelocityHalfPlane& Active = Input.HalfPlanes[ActiveIndex];
  const FVector2f BoundaryPoint = Active.Point - Active.Normal * Settings.BehaviorEpsilonCmps;
  const FVector2f Direction(-Active.Normal.Y, Active.Normal.X);
  const FLineInterval Circle = ComputeLineCircleInterval(
    BoundaryPoint, Direction, Settings.MaxSpeedCmps);
  if (!Circle.bFeasible) return false;
  float Left = Circle.MinimumT;
  float Right = Circle.MaximumT;
  for (int32 PreviousIndex = 0; PreviousIndex < ActiveIndex; ++PreviousIndex)
  {
    if (!ClipLineInterval(BoundaryPoint, Direction, Input.HalfPlanes[PreviousIndex],
      Settings.BehaviorEpsilonCmps, Left, Right, OutSummary))
      return false;
  }
  const float PreferredT = FVector2f::DotProduct(Preferred - BoundaryPoint, Direction);
  OutVelocity = BoundaryPoint + Direction * FMath::Clamp(PreferredT, Left, Right);
  return FMath::IsFinite(OutVelocity.X) && FMath::IsFinite(OutVelocity.Y)
    && InsideSpeedCircle(OutVelocity, Settings.MaxSpeedCmps, Settings.BehaviorEpsilonCmps)
    && Satisfies(OutVelocity,
      MakeArrayView(Input.HalfPlanes.GetData(), ActiveIndex + 1),
      Settings.BehaviorEpsilonCmps, Settings.MaxSpeedCmps);
}

bool FindNeighborhoodQuantized(
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  const FVector2f Continuous,
  const FVector2f Preferred,
  FVector2f& OutVelocity)
{
  const auto& Settings = Input.Settings;
  const float Quantum = FMath::Max(0.001f, Settings.VelocityQuantumCmps);
  const FVector2f Rounded = Quantize(Continuous, Quantum);
  bool bFound = false;
  double BestContinuousDistance = TNumericLimits<double>::Max();
  double BestPreferredDistance = TNumericLimits<double>::Max();
  FVector2f Best = FVector2f::ZeroVector;
  for (int32 DY = -1; DY <= 1; ++DY)
  {
    for (int32 DX = -1; DX <= 1; ++DX)
    {
      const FVector2f Candidate = Rounded + FVector2f(DX * Quantum, DY * Quantum);
      if (!InsideSpeedCircle(Candidate, Settings.MaxSpeedCmps, Settings.BehaviorEpsilonCmps)
        || !Satisfies(Candidate, Input.HalfPlanes,
          Settings.BehaviorEpsilonCmps, Settings.MaxSpeedCmps))
        continue;
      const double ContinuousDistance = static_cast<double>((Candidate - Continuous).SizeSquared());
      const double PreferredDistance = static_cast<double>((Candidate - Preferred).SizeSquared());
      const bool bBetter = !bFound || ContinuousDistance < BestContinuousDistance
        || (ContinuousDistance == BestContinuousDistance
          && PreferredDistance < BestPreferredDistance)
        || (ContinuousDistance == BestContinuousDistance
          && PreferredDistance == BestPreferredDistance
          && (Candidate.X < Best.X || (Candidate.X == Best.X && Candidate.Y < Best.Y)));
      if (bBetter)
      {
        bFound = true;
        BestContinuousDistance = ContinuousDistance;
        BestPreferredDistance = PreferredDistance;
        Best = Candidate;
      }
    }
  }
  if (bFound) OutVelocity = Best;
  return bFound;
}

bool FindGeometryQuantized(
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  const FVector2f Continuous,
  const FVector2f Preferred,
  FVector2f& OutVelocity)
{
  const auto& Settings = Input.Settings;
  const float Quantum = FMath::Max(0.001f, Settings.VelocityQuantumCmps);
  const float Epsilon = Settings.BehaviorEpsilonCmps;
  TArray<FVector2f> Centers;
  Centers.Reserve(2 + Input.HalfPlanes.Num() * 3
    + Input.HalfPlanes.Num() * FMath::Max(0, Input.HalfPlanes.Num() - 1) / 2);
  Centers.Add(Continuous);
  Centers.Add(ClampSpeed(Quantize(Preferred, Quantum), Settings.MaxSpeedCmps));
  for (int32 Index = 0; Index < Input.HalfPlanes.Num(); ++Index)
  {
    const FCrowdDemoVelocityHalfPlane& HalfPlane = Input.HalfPlanes[Index];
    const FVector2f BoundaryPoint = HalfPlane.Point - HalfPlane.Normal * Epsilon;
    const FVector2f Direction(-HalfPlane.Normal.Y, HalfPlane.Normal.X);
    Centers.Add(BoundaryPoint + Direction
      * FVector2f::DotProduct(Preferred - BoundaryPoint, Direction));
    const FLineInterval Circle = ComputeLineCircleInterval(
      BoundaryPoint, Direction, Settings.MaxSpeedCmps);
    if (Circle.bFeasible)
    {
      Centers.Add(BoundaryPoint + Direction * Circle.MinimumT);
      Centers.Add(BoundaryPoint + Direction * Circle.MaximumT);
    }
    const double C0 = static_cast<double>(HalfPlane.Point.X) * HalfPlane.Normal.X
      + static_cast<double>(HalfPlane.Point.Y) * HalfPlane.Normal.Y - Epsilon;
    for (int32 OtherIndex = Index + 1; OtherIndex < Input.HalfPlanes.Num(); ++OtherIndex)
    {
      const FCrowdDemoVelocityHalfPlane& Other = Input.HalfPlanes[OtherIndex];
      const double Determinant = static_cast<double>(HalfPlane.Normal.X) * Other.Normal.Y
        - static_cast<double>(HalfPlane.Normal.Y) * Other.Normal.X;
      if (FMath::Abs(Determinant)
        <= ComputeTolerances(Settings.MaxSpeedCmps).ParallelAngularTolerance)
        continue;
      const double C1 = static_cast<double>(Other.Point.X) * Other.Normal.X
        + static_cast<double>(Other.Point.Y) * Other.Normal.Y - Epsilon;
      Centers.Add(FVector2f(
        static_cast<float>((C0 * Other.Normal.Y - HalfPlane.Normal.Y * C1) / Determinant),
        static_cast<float>((HalfPlane.Normal.X * C1 - C0 * Other.Normal.X) / Determinant)));
    }
  }

  bool bFound = false;
  double BestContinuousDistance = TNumericLimits<double>::Max();
  double BestPreferredDistance = TNumericLimits<double>::Max();
  FVector2f Best = FVector2f::ZeroVector;
  for (const FVector2f Center : Centers)
  {
    if (!FMath::IsFinite(Center.X) || !FMath::IsFinite(Center.Y)) continue;
    const FVector2f Rounded = Quantize(Center, Quantum);
    for (int32 DY = -1; DY <= 1; ++DY)
    {
      for (int32 DX = -1; DX <= 1; ++DX)
      {
        const FVector2f Candidate = Rounded + FVector2f(DX * Quantum, DY * Quantum);
        if (!InsideSpeedCircle(Candidate, Settings.MaxSpeedCmps, Epsilon)
          || !Satisfies(Candidate, Input.HalfPlanes, Epsilon, Settings.MaxSpeedCmps))
          continue;
        const double ContinuousDistance = static_cast<double>((Candidate - Continuous).SizeSquared());
        const double PreferredDistance = static_cast<double>((Candidate - Preferred).SizeSquared());
        const bool bBetter = !bFound || ContinuousDistance < BestContinuousDistance
          || (ContinuousDistance == BestContinuousDistance
            && PreferredDistance < BestPreferredDistance)
          || (ContinuousDistance == BestContinuousDistance
            && PreferredDistance == BestPreferredDistance
            && (Candidate.X < Best.X || (Candidate.X == Best.X && Candidate.Y < Best.Y)));
        if (bBetter)
        {
          bFound = true;
          BestContinuousDistance = ContinuousDistance;
          BestPreferredDistance = PreferredDistance;
          Best = Candidate;
        }
      }
    }
  }
  if (bFound) OutVelocity = Best;
  return bFound;
}
}

bool FCrowdDemoVelocityHalfPlaneKernel::CanonicalizeInput(
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  FCrowdDemoVelocityHalfPlaneInput& OutCanonical)
{
  OutCanonical = Input;
  const auto& Settings = OutCanonical.Settings;
  if (!FMath::IsFinite(Input.PreferredVelocity.X)
    || !FMath::IsFinite(Input.PreferredVelocity.Y)
    || !FMath::IsFinite(Settings.MaxSpeedCmps) || Settings.MaxSpeedCmps < 0.0f
    || !FMath::IsFinite(Settings.BehaviorEpsilonCmps) || Settings.BehaviorEpsilonCmps < 0.0f
    || !FMath::IsFinite(Settings.VelocityQuantumCmps) || Settings.VelocityQuantumCmps <= 0.0f)
    return false;
  for (const FCrowdDemoVelocityHalfPlane& HalfPlane : OutCanonical.HalfPlanes)
  {
    if (!FMath::IsFinite(HalfPlane.Point.X) || !FMath::IsFinite(HalfPlane.Point.Y)
      || !FMath::IsFinite(HalfPlane.Normal.X) || !FMath::IsFinite(HalfPlane.Normal.Y)
      || HalfPlane.Normal.SizeSquared() <= KINDA_SMALL_NUMBER)
      return false;
  }
  OutCanonical.HalfPlanes.Sort([](
    const FCrowdDemoVelocityHalfPlane& A,
    const FCrowdDemoVelocityHalfPlane& B)
  {
    if (A.StableOrder != B.StableOrder) return A.StableOrder < B.StableOrder;
    const int32 APX = FMath::RoundToInt(A.Point.X * 1000.0f);
    const int32 BPX = FMath::RoundToInt(B.Point.X * 1000.0f);
    if (APX != BPX) return APX < BPX;
    const int32 APY = FMath::RoundToInt(A.Point.Y * 1000.0f);
    const int32 BPY = FMath::RoundToInt(B.Point.Y * 1000.0f);
    if (APY != BPY) return APY < BPY;
    const int32 ANX = FMath::RoundToInt(A.Normal.X * 32767.0f);
    const int32 BNX = FMath::RoundToInt(B.Normal.X * 32767.0f);
    if (ANX != BNX) return ANX < BNX;
    return FMath::RoundToInt(A.Normal.Y * 32767.0f)
      < FMath::RoundToInt(B.Normal.Y * 32767.0f);
  });
  return true;
}

bool FCrowdDemoVelocityHalfPlaneKernel::SolveContinuous(
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  FVector2f& OutVelocity,
  FCrowdDemoVelocityHalfPlaneNumericalSummary* OutNumericalSummary)
{
  FCrowdDemoVelocityHalfPlaneInput Canonical;
  if (!CanonicalizeInput(Input, Canonical)) return false;
  const auto& Settings = Canonical.Settings;
  const FVector2f Preferred = ClampSpeed(
    Quantize(Canonical.PreferredVelocity, Settings.VelocityQuantumCmps),
    Settings.MaxSpeedCmps);
  FVector2f Continuous = Preferred;
  for (int32 ConstraintIndex = 0; ConstraintIndex < Canonical.HalfPlanes.Num(); ++ConstraintIndex)
  {
    const FCrowdDemoVelocityHalfPlane& HalfPlane = Canonical.HalfPlanes[ConstraintIndex];
    const double DeltaX = static_cast<double>(Continuous.X) - HalfPlane.Point.X;
    const double DeltaY = static_cast<double>(Continuous.Y) - HalfPlane.Point.Y;
    const double Scalar = DeltaX * HalfPlane.Normal.X + DeltaY * HalfPlane.Normal.Y;
    const FNumericalTolerances Tolerances = ComputeTolerances(Settings.MaxSpeedCmps);
    if (Scalar >= -static_cast<double>(Settings.BehaviorEpsilonCmps)
      - Tolerances.ResidualToleranceCmps)
      continue;
    if (!SolveOnBoundary(ConstraintIndex, Preferred, Canonical,
      Continuous, OutNumericalSummary))
      return false;
  }
  if (!InsideSpeedCircle(Continuous, Settings.MaxSpeedCmps, Settings.BehaviorEpsilonCmps)
    || !Satisfies(Continuous, Canonical.HalfPlanes,
      Settings.BehaviorEpsilonCmps, Settings.MaxSpeedCmps))
    return false;
  OutVelocity = Continuous;
  return true;
}

bool FCrowdDemoVelocityHalfPlaneKernel::ValidateVelocity(
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  const FVector2f Velocity)
{
  FCrowdDemoVelocityHalfPlaneInput Canonical;
  if (!CanonicalizeInput(Input, Canonical)
    || !FMath::IsFinite(Velocity.X) || !FMath::IsFinite(Velocity.Y))
    return false;
  return InsideSpeedCircle(Velocity, Canonical.Settings.MaxSpeedCmps,
      Canonical.Settings.BehaviorEpsilonCmps)
    && Satisfies(Velocity, Canonical.HalfPlanes,
      Canonical.Settings.BehaviorEpsilonCmps, Canonical.Settings.MaxSpeedCmps);
}

ECrowdDemoVelocityQuantizationResult FCrowdDemoVelocityHalfPlaneKernel::QuantizeAndValidate(
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  const FVector2f ContinuousVelocity,
  FVector2f& OutVelocity)
{
  FCrowdDemoVelocityHalfPlaneInput Canonical;
  if (!CanonicalizeInput(Input, Canonical))
    return ECrowdDemoVelocityQuantizationResult::NoSolution;
  const auto& Settings = Canonical.Settings;
  const FVector2f Preferred = ClampSpeed(
    Quantize(Canonical.PreferredVelocity, Settings.VelocityQuantumCmps),
    Settings.MaxSpeedCmps);
  const FVector2f Rounded = Quantize(ContinuousVelocity, Settings.VelocityQuantumCmps);
  if (InsideSpeedCircle(Rounded, Settings.MaxSpeedCmps, Settings.BehaviorEpsilonCmps)
    && Satisfies(Rounded, Canonical.HalfPlanes,
      Settings.BehaviorEpsilonCmps, Settings.MaxSpeedCmps))
  {
    OutVelocity = Rounded;
    return ECrowdDemoVelocityQuantizationResult::CenterFeasible;
  }
  if (FindNeighborhoodQuantized(Canonical, ContinuousVelocity, Preferred, OutVelocity))
    return ECrowdDemoVelocityQuantizationResult::NeighborhoodRecovered;
  if (FindGeometryQuantized(Canonical, ContinuousVelocity, Preferred, OutVelocity))
    return ECrowdDemoVelocityQuantizationResult::GeometryRecovered;
  return ECrowdDemoVelocityQuantizationResult::NoSolution;
}

FCrowdDemoVelocityHalfPlaneResult FCrowdDemoVelocityHalfPlaneKernel::Solve(
  const FCrowdDemoVelocityHalfPlaneInput& Input,
  FCrowdDemoVelocityHalfPlaneNumericalSummary* OutNumericalSummary)
{
  FCrowdDemoVelocityHalfPlaneResult Result;
  FCrowdDemoVelocityHalfPlaneInput Canonical;
  if (!CanonicalizeInput(Input, Canonical)) return Result;
  const auto& Settings = Canonical.Settings;
  if (InsideSpeedCircle(Canonical.PreferredVelocity, Settings.MaxSpeedCmps,
      Settings.BehaviorEpsilonCmps)
    && Satisfies(Canonical.PreferredVelocity, Canonical.HalfPlanes,
      Settings.BehaviorEpsilonCmps, Settings.MaxSpeedCmps))
  {
    Result.Status = ECrowdDemoVelocityHalfPlaneSolveStatus::PreferredFeasible;
    Result.ContinuousVelocity = Canonical.PreferredVelocity;
    Result.bContinuousValid = true;
  }
  else if (SolveContinuous(Canonical, Result.ContinuousVelocity, OutNumericalSummary))
  {
    Result.Status = ECrowdDemoVelocityHalfPlaneSolveStatus::ExactFeasible;
    Result.bContinuousValid = ValidateVelocity(Canonical, Result.ContinuousVelocity);
    if (!Result.bContinuousValid)
      Result.Status = ECrowdDemoVelocityHalfPlaneSolveStatus::NumericalFailure;
  }
  else
  {
    Result.Status = ECrowdDemoVelocityHalfPlaneSolveStatus::ProvenInfeasible;
    return Result;
  }
  Result.Quantization = QuantizeAndValidate(
    Canonical, Result.ContinuousVelocity, Result.QuantizedVelocity);
  Result.bQuantizedValid = Result.Quantization != ECrowdDemoVelocityQuantizationResult::NoSolution
    && ValidateVelocity(Canonical, Result.QuantizedVelocity);
  return Result;
}
