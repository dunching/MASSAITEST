#include "Mass/CrowdDemoDeterministicOrcaKernel.h"

#include <limits>

namespace
{
  uint32 OrcaHashInt(uint32 Hash, const int32 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    return Hash * 16777619u;
  }

  FVector2f Quantize(const FVector2f V, const float Quantum)
  {
    const float Q = FMath::Max(0.001f, Quantum);
    return FVector2f(FMath::RoundToFloat(V.X/Q)*Q, FMath::RoundToFloat(V.Y/Q)*Q);
  }

  FVector2f NormalizeStable(const FVector2f V, const int32 A, const int32 B)
  {
    if (V.SizeSquared() <= KINDA_SMALL_NUMBER)
    {
      const uint32 H = static_cast<uint32>(A)*73856093u ^ static_cast<uint32>(B)*19349663u;
      const float Angle = static_cast<float>(H % 4096u) * (2.0f*PI/4096.0f);
      return FVector2f(FMath::Cos(Angle), FMath::Sin(Angle));
    }
    const FVector2f N = V.GetSafeNormal();
    return FVector2f(FMath::RoundToFloat(N.X*32767.0f)/32767.0f, FMath::RoundToFloat(N.Y*32767.0f)/32767.0f);
  }

  FVector2f NormalizeQ15(const FVector2f V)
  {
    if (V.SizeSquared() <= KINDA_SMALL_NUMBER) return FVector2f(1.0f, 0.0f);
    const FVector2f Unit = V.GetSafeNormal();
    const FVector2f Quantized(
      FMath::RoundToFloat(Unit.X * 32767.0f) / 32767.0f,
      FMath::RoundToFloat(Unit.Y * 32767.0f) / 32767.0f);
    return Quantized.GetSafeNormal();
  }

  FVector2f StableCoincidentPairDirection(const int32 AgentId, const int32 OtherAgentId)
  {
    const int32 MinId = FMath::Min(AgentId, OtherAgentId);
    const int32 MaxId = FMath::Max(AgentId, OtherAgentId);
    const uint32 H = static_cast<uint32>(MinId) * 73856093u
      ^ static_cast<uint32>(MaxId) * 19349663u;
    const float Angle = static_cast<float>(H % 4096u) * (2.0f * PI / 4096.0f);
    const FVector2f Base(FMath::Cos(Angle), FMath::Sin(Angle));
    return AgentId == MinId ? Base : -Base;
  }

  float Determinant(const FVector2f A, const FVector2f B)
  {
    return A.X * B.Y - A.Y * B.X;
  }

  int32 Priority(const ECrowdDemoPortalAdmissionState State)
  {
    switch (State)
    {
    case ECrowdDemoPortalAdmissionState::Inside:
    case ECrowdDemoPortalAdmissionState::Reserved: return 2;
    case ECrowdDemoPortalAdmissionState::Approach: return 1;
    default: return 0;
    }
  }

  FVector2f ClampSpeed(const FVector2f V, const float MaxSpeed)
  {
    const float Size = V.Size();
    return Size > MaxSpeed && Size > SMALL_NUMBER ? V*(MaxSpeed/Size) : V;
  }

  bool Satisfies(const FVector2f V, TConstArrayView<FCrowdDemoOrcaConstraint> Constraints, const float Epsilon)
  {
    for (const FCrowdDemoOrcaConstraint& Constraint : Constraints)
      if (FVector2f::DotProduct(V-Constraint.Point, Constraint.Normal) < -Epsilon) return false;
    return true;
  }

  bool SatisfiesFormalNumerically(
    const FVector2f V,
    const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const float BehaviorEpsilon,
    const float VelocityScale)
  {
    const FCrowdDemoOrcaNumericalTolerances Tolerances =
      FCrowdDemoDeterministicOrcaKernel::ComputeNumericalTolerances(VelocityScale);
    for (const FCrowdDemoOrcaConstraint& Constraint : Constraints)
    {
      const double DeltaX = static_cast<double>(V.X) - Constraint.Point.X;
      const double DeltaY = static_cast<double>(V.Y) - Constraint.Point.Y;
      const double Scalar = DeltaX * Constraint.Normal.X + DeltaY * Constraint.Normal.Y;
      if (Scalar < -static_cast<double>(BehaviorEpsilon) - Tolerances.ResidualToleranceCmps)
        return false;
    }
    return true;
  }

  bool InsideSpeedCircle(const FVector2f V, const float MaxSpeed, const float Epsilon)
  {
    return V.SizeSquared() <= FMath::Square(MaxSpeed + Epsilon);
  }

  float PointSegmentDistanceSquared(
    const FVector2f Point, const FVector2f Start, const FVector2f End)
  {
    const FVector2f Segment = End - Start;
    const float LengthSquared = Segment.SizeSquared();
    const float T = LengthSquared > KINDA_SMALL_NUMBER
      ? FMath::Clamp(FVector2f::DotProduct(Point - Start, Segment) / LengthSquared, 0.0f, 1.0f)
      : 0.0f;
    return (Point - (Start + Segment * T)).SizeSquared();
  }

  float SegmentDistanceSquared(
    const FVector2f A0, const FVector2f A1, const FVector2f B0, const FVector2f B1)
  {
    const auto Cross = [](const FVector2f P0, const FVector2f P1, const FVector2f P2)
    {
      return static_cast<double>(P1.X - P0.X) * static_cast<double>(P2.Y - P0.Y)
        - static_cast<double>(P1.Y - P0.Y) * static_cast<double>(P2.X - P0.X);
    };
    const double C1 = Cross(A0, A1, B0);
    const double C2 = Cross(A0, A1, B1);
    const double C3 = Cross(B0, B1, A0);
    const double C4 = Cross(B0, B1, A1);
    const float EndpointDistance = FMath::Min(
      FMath::Min(PointSegmentDistanceSquared(A0, B0, B1),
        PointSegmentDistanceSquared(A1, B0, B1)),
      FMath::Min(PointSegmentDistanceSquared(B0, A0, A1),
        PointSegmentDistanceSquared(B1, A0, A1)));
    if (EndpointDistance <= KINDA_SMALL_NUMBER
      || (C1 * C2 < 0.0 && C3 * C4 < 0.0)) return 0.0f;
    return EndpointDistance;
  }

  bool FindFirstReservationSegment(
    const FCrowdDemoOrcaAgent& Agent, FVector2f& OutStart, FVector2f& OutEnd)
  {
    for (int32 Index = 0; Index + 1 < Agent.Sf4RoutePoints.Num(); ++Index)
    {
      if ((Agent.Sf4RoutePoints[Index + 1] - Agent.Sf4RoutePoints[Index]).SizeSquared()
        <= KINDA_SMALL_NUMBER) continue;
      OutStart = Agent.Sf4RoutePoints[Index];
      OutEnd = Agent.Sf4RoutePoints[Index + 1];
      return true;
    }
    return false;
  }

  bool SourcedConstraintLess(
    const FCrowdDemoSf4SourcedOrcaConstraint& A,
    const FCrowdDemoSf4SourcedOrcaConstraint& B)
  {
    if (A.Constraint.StableConstraintOrder != B.Constraint.StableConstraintOrder)
      return A.Constraint.StableConstraintOrder < B.Constraint.StableConstraintOrder;
    if (A.Constraint.OtherAgentId != B.Constraint.OtherAgentId)
      return A.Constraint.OtherAgentId < B.Constraint.OtherAgentId;
    const int32 AX = FMath::RoundToInt(A.Constraint.Point.X);
    const int32 BX = FMath::RoundToInt(B.Constraint.Point.X);
    if (AX != BX) return AX < BX;
    const int32 AY = FMath::RoundToInt(A.Constraint.Point.Y);
    const int32 BY = FMath::RoundToInt(B.Constraint.Point.Y);
    if (AY != BY) return AY < BY;
    const int32 ANX = FMath::RoundToInt(A.Constraint.Normal.X * 32767.0f);
    const int32 BNX = FMath::RoundToInt(B.Constraint.Normal.X * 32767.0f);
    if (ANX != BNX) return ANX < BNX;
    return FMath::RoundToInt(A.Constraint.Normal.Y * 32767.0f)
      < FMath::RoundToInt(B.Constraint.Normal.Y * 32767.0f);
  }

  bool SolveOnConstraintBoundary(
    const int32 ConstraintIndex,
    const FVector2f Preferred,
    const float MaxSpeed,
    const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const float Epsilon,
    FVector2f& OutVelocity,
    FCrowdDemoOrcaNumericalSummary* NumericalSummary)
  {
    const FCrowdDemoOrcaConstraint& Active = Constraints[ConstraintIndex];
    const FVector2f BoundaryPoint = Active.Point - Active.Normal * Epsilon;
    const FVector2f Direction(-Active.Normal.Y, Active.Normal.X);
    const FCrowdDemoOrcaLineInterval CircleInterval =
      FCrowdDemoDeterministicOrcaKernel::ComputeLineCircleInterval(
        BoundaryPoint, Direction, MaxSpeed, Epsilon);
    if (!CircleInterval.bFeasible) return false;
    float Left = CircleInterval.MinimumT;
    float Right = CircleInterval.MaximumT;
    for (int32 PreviousIndex = 0; PreviousIndex < ConstraintIndex; ++PreviousIndex)
    {
      const FCrowdDemoOrcaConstraint& Previous = Constraints[PreviousIndex];
      if (!FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
        BoundaryPoint, Direction, Previous, Epsilon, Left, Right, NumericalSummary)) return false;
    }
    const float PreferredT = FVector2f::DotProduct(Preferred - BoundaryPoint, Direction);
    const float T = FMath::Clamp(PreferredT, Left, Right);
    OutVelocity = BoundaryPoint + Direction * T;
    return FMath::IsFinite(OutVelocity.X) && FMath::IsFinite(OutVelocity.Y)
      && InsideSpeedCircle(OutVelocity, MaxSpeed, Epsilon)
      && SatisfiesFormalNumerically(
        OutVelocity, Constraints.Left(ConstraintIndex + 1), Epsilon, MaxSpeed);
  }

  bool FindQuantizedFeasibleVelocity(
    const FVector2f Continuous,
    const FVector2f Preferred,
    const float MaxSpeed,
    const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings,
    FVector2f& OutVelocity)
  {
    const float Quantum = FMath::Max(0.001f, Settings.VelocityQuantumCmps);
    const FVector2f Rounded = Quantize(Continuous, Quantum);
    bool bFound = false;
    float BestContinuousDistance = MAX_flt;
    float BestPreferredDistance = MAX_flt;
    FVector2f Best = FVector2f::ZeroVector;
    for (int32 DY = -1; DY <= 1; ++DY)
    {
      for (int32 DX = -1; DX <= 1; ++DX)
      {
        const FVector2f Candidate = Rounded + FVector2f(DX * Quantum, DY * Quantum);
        if (!InsideSpeedCircle(Candidate, MaxSpeed, Settings.ConstraintEpsilonCmps)
          || !Satisfies(Candidate, Constraints, Settings.ConstraintEpsilonCmps))
        {
          continue;
        }
        const float ContinuousDistance = (Candidate - Continuous).SizeSquared();
        const float PreferredDistance = (Candidate - Preferred).SizeSquared();
        const bool bBetter = !bFound || ContinuousDistance < BestContinuousDistance
          || (ContinuousDistance == BestContinuousDistance && PreferredDistance < BestPreferredDistance)
          || (ContinuousDistance == BestContinuousDistance && PreferredDistance == BestPreferredDistance
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

  bool FindGeometryQuantizedFeasibleVelocity(
    const FVector2f Continuous,
    const FVector2f Preferred,
    const float MaxSpeed,
    const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings,
    FVector2f& OutVelocity)
  {
    const float Quantum = FMath::Max(0.001f, Settings.VelocityQuantumCmps);
    const float Epsilon = Settings.ConstraintEpsilonCmps;
    TArray<FVector2f> Centers;
    Centers.Reserve(2 + Constraints.Num() * 3
      + Constraints.Num() * FMath::Max(0, Constraints.Num() - 1) / 2);
    Centers.Add(Continuous);
    Centers.Add(ClampSpeed(Quantize(Preferred, Quantum), MaxSpeed));
    for (int32 Index = 0; Index < Constraints.Num(); ++Index)
    {
      const auto& Constraint = Constraints[Index];
      const FVector2f BoundaryPoint = Constraint.Point - Constraint.Normal * Epsilon;
      const FVector2f Direction(-Constraint.Normal.Y, Constraint.Normal.X);
      const float PreferredT = FVector2f::DotProduct(Preferred - BoundaryPoint, Direction);
      Centers.Add(BoundaryPoint + Direction * PreferredT);
      const FCrowdDemoOrcaLineInterval Circle =
        FCrowdDemoDeterministicOrcaKernel::ComputeLineCircleInterval(
          BoundaryPoint, Direction, MaxSpeed, Epsilon);
      if (Circle.bFeasible)
      {
        Centers.Add(BoundaryPoint + Direction * Circle.MinimumT);
        Centers.Add(BoundaryPoint + Direction * Circle.MaximumT);
      }
      const double C0 = static_cast<double>(Constraint.Point.X) * Constraint.Normal.X
        + static_cast<double>(Constraint.Point.Y) * Constraint.Normal.Y - Epsilon;
      for (int32 OtherIndex = Index + 1; OtherIndex < Constraints.Num(); ++OtherIndex)
      {
        const auto& Other = Constraints[OtherIndex];
        const double DeterminantValue = static_cast<double>(Constraint.Normal.X) * Other.Normal.Y
          - static_cast<double>(Constraint.Normal.Y) * Other.Normal.X;
        const double ParallelTolerance =
          FCrowdDemoDeterministicOrcaKernel::ComputeNumericalTolerances(MaxSpeed)
            .ParallelAngularTolerance;
        if (FMath::Abs(DeterminantValue) <= ParallelTolerance) continue;
        const double C1 = static_cast<double>(Other.Point.X) * Other.Normal.X
          + static_cast<double>(Other.Point.Y) * Other.Normal.Y - Epsilon;
        Centers.Add(FVector2f(
          static_cast<float>((C0 * Other.Normal.Y - Constraint.Normal.Y * C1)
            / DeterminantValue),
          static_cast<float>((Constraint.Normal.X * C1 - C0 * Other.Normal.X)
            / DeterminantValue)));
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
          if (!InsideSpeedCircle(Candidate, MaxSpeed, Epsilon)
            || !Satisfies(Candidate, Constraints, Epsilon)) continue;
          const double CDX = static_cast<double>(Candidate.X) - Continuous.X;
          const double CDY = static_cast<double>(Candidate.Y) - Continuous.Y;
          const double PDX = static_cast<double>(Candidate.X) - Preferred.X;
          const double PDY = static_cast<double>(Candidate.Y) - Preferred.Y;
          const double ContinuousDistance = CDX * CDX + CDY * CDY;
          const double PreferredDistance = PDX * PDX + PDY * PDY;
          const bool bBetter = !bFound || ContinuousDistance < BestContinuousDistance
            || (ContinuousDistance == BestContinuousDistance
              && PreferredDistance < BestPreferredDistance)
            || (ContinuousDistance == BestContinuousDistance
              && PreferredDistance == BestPreferredDistance
              && (Candidate.X < Best.X
                || (Candidate.X == Best.X && Candidate.Y < Best.Y)));
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

FCrowdDemoOrcaRoutePairPolicy FCrowdDemoDeterministicOrcaKernel::EvaluateSf4RoutePairPolicy(
  const FCrowdDemoOrcaAgent& Agent,
  const FCrowdDemoOrcaAgent& Other)
{
  FCrowdDemoOrcaRoutePairPolicy Policy;
  const FCrowdDemoOrcaAgent* Active = nullptr;
  if (Agent.Sf4RouteMode == ECrowdDemoOrcaRouteMode::Active
    && Other.Sf4RouteMode == ECrowdDemoOrcaRouteMode::Yielding)
    Active = &Agent;
  else if (Agent.Sf4RouteMode == ECrowdDemoOrcaRouteMode::Yielding
    && Other.Sf4RouteMode == ECrowdDemoOrcaRouteMode::Active)
    Active = &Other;
  if (!Active || Active->Sf4RoutePoints.Num() < 2) return Policy;
  const FCrowdDemoOrcaAgent& Yielding = Active == &Agent ? Other : Agent;
  const float SafetyRadius = Active->RadiusCm + Yielding.RadiusCm
    + FMath::Max(Active->Sf4RouteSafetyGapCm, Yielding.Sf4RouteSafetyGapCm);
  bool bRouteConflicts = false;
  for (int32 PointIndex = 0; PointIndex + 1 < Active->Sf4RoutePoints.Num(); ++PointIndex)
  {
    const FVector2f Start = Active->Sf4RoutePoints[PointIndex];
    const FVector2f End = Active->Sf4RoutePoints[PointIndex + 1];
    const FVector2f Segment = End - Start;
    const float SegmentLengthSquared = Segment.SizeSquared();
    const float T = SegmentLengthSquared > KINDA_SMALL_NUMBER
      ? FMath::Clamp(FVector2f::DotProduct(Yielding.Position - Start, Segment)
          / SegmentLengthSquared, 0.0f, 1.0f)
      : 0.0f;
    if ((Start + Segment * T - Yielding.Position).SizeSquared()
      <= FMath::Square(SafetyRadius))
    {
      bRouteConflicts = true;
      break;
    }
  }
  if (!bRouteConflicts) return Policy;
  Policy.bOverridesDefault = true;
  Policy.bIncludeConstraint = Agent.Sf4RouteMode != ECrowdDemoOrcaRouteMode::Active;
  Policy.Responsibility = Policy.bIncludeConstraint ? 1.0f : 0.0f;
  return Policy;
}

FCrowdDemoOrcaPriorityKey FCrowdDemoDeterministicOrcaKernel::MakePriorityKey(
  const FCrowdDemoOrcaAgent& Agent)
{
  FCrowdDemoOrcaPriorityKey Key;
  Key.PortalPriority = static_cast<uint8>(Priority(Agent.AdmissionState));
  Key.LocalPriority = Agent.LocalAvoidancePriority;
  return Key;
}

int32 FCrowdDemoDeterministicOrcaKernel::ComparePriorityKeys(
  const FCrowdDemoOrcaPriorityKey& A, const FCrowdDemoOrcaPriorityKey& B)
{
  if (A.PortalPriority != B.PortalPriority)
    return A.PortalPriority < B.PortalPriority ? -1 : 1;
  const uint8 LocalA = static_cast<uint8>(A.LocalPriority);
  const uint8 LocalB = static_cast<uint8>(B.LocalPriority);
  return LocalA == LocalB ? 0 : (LocalA < LocalB ? -1 : 1);
}

FCrowdDemoOrcaLineInterval FCrowdDemoDeterministicOrcaKernel::ComputeLineCircleInterval(
  const FVector2f LinePoint,
  const FVector2f LineDirection,
  const float MaxSpeedCmps,
  const float EpsilonCmps)
{
  FCrowdDemoOrcaLineInterval Result;
  const double DirectionLengthSquared = static_cast<double>(LineDirection.X) * LineDirection.X
    + static_cast<double>(LineDirection.Y) * LineDirection.Y;
  if (!FMath::IsFinite(DirectionLengthSquared) || DirectionLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
    return Result;
  const double B = static_cast<double>(LinePoint.X) * LineDirection.X
    + static_cast<double>(LinePoint.Y) * LineDirection.Y;
  const double C = static_cast<double>(LinePoint.X) * LinePoint.X
    + static_cast<double>(LinePoint.Y) * LinePoint.Y
    - static_cast<double>(MaxSpeedCmps) * MaxSpeedCmps;
  const double Discriminant = B * B - DirectionLengthSquared * C;
  const double VelocityScale = FMath::Max(static_cast<double>(MaxSpeedCmps),
    FMath::Sqrt(FMath::Max(0.0, C)));
  const FCrowdDemoOrcaNumericalTolerances Tolerances = ComputeNumericalTolerances(VelocityScale);
  const double DiscriminantTolerance = 2.0 * FMath::Max(1.0, VelocityScale)
    * Tolerances.ResidualToleranceCmps * DirectionLengthSquared;
  if (!FMath::IsFinite(Discriminant)
    || Discriminant < -DiscriminantTolerance) return Result;
  const double Root = FMath::Sqrt(FMath::Max(0.0, Discriminant));
  Result.bFeasible = true;
  Result.MinimumT = static_cast<float>((-B - Root) / DirectionLengthSquared);
  Result.MaximumT = static_cast<float>((-B + Root) / DirectionLengthSquared);
  return Result;
}

FCrowdDemoOrcaNumericalTolerances FCrowdDemoDeterministicOrcaKernel::ComputeNumericalTolerances(
  const double RelevantVelocityScaleCmps)
{
  const double FloatEpsilon = static_cast<double>(std::numeric_limits<float>::epsilon());
  const double Scale = FMath::Max(1.0, FMath::Abs(RelevantVelocityScaleCmps));
  FCrowdDemoOrcaNumericalTolerances Result;
  Result.ParallelAngularTolerance = 64.0 * FloatEpsilon;
  Result.ResidualToleranceCmps = FMath::Max(1.0e-6, 16.0 * FloatEpsilon * Scale);
  Result.ParameterTolerance = FMath::Max(1.0e-6, 8.0 * FloatEpsilon * Scale);
  return Result;
}

bool FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
  const FVector2f LinePoint,
  const FVector2f LineDirection,
  const FCrowdDemoOrcaConstraint& Constraint,
  const float EpsilonCmps,
  float& InOutMinimumT,
  float& InOutMaximumT,
  FCrowdDemoOrcaNumericalSummary* OutNumericalSummary)
{
  const double Denominator = static_cast<double>(LineDirection.X) * Constraint.Normal.X
    + static_cast<double>(LineDirection.Y) * Constraint.Normal.Y;
  const double DeltaX = static_cast<double>(LinePoint.X) - Constraint.Point.X;
  const double DeltaY = static_cast<double>(LinePoint.Y) - Constraint.Point.Y;
  const double Numerator = -static_cast<double>(EpsilonCmps)
    - (DeltaX * Constraint.Normal.X + DeltaY * Constraint.Normal.Y);
  if (!FMath::IsFinite(Denominator) || !FMath::IsFinite(Numerator)) return false;
  const double RelevantScale = FMath::Max3(
    FMath::Max(FMath::Abs(static_cast<double>(InOutMinimumT)), FMath::Abs(static_cast<double>(InOutMaximumT))),
    FMath::Max(FMath::Abs(static_cast<double>(LinePoint.X)), FMath::Abs(static_cast<double>(LinePoint.Y))),
    FMath::Max(FMath::Abs(static_cast<double>(Constraint.Point.X)), FMath::Abs(static_cast<double>(Constraint.Point.Y))));
  const FCrowdDemoOrcaNumericalTolerances Tolerances = ComputeNumericalTolerances(RelevantScale);
  if (FMath::Abs(Denominator) <= Tolerances.ParallelAngularTolerance)
  {
    if (OutNumericalSummary)
    {
      ++OutNumericalSummary->ParallelBranchCount;
      if (FMath::Abs(Denominator) > 0.0) ++OutNumericalSummary->NearParallelBranchCount;
    }
    if (Numerator <= 0.0)
    {
      if (OutNumericalSummary) ++OutNumericalSummary->RedundantParallelCount;
      return true;
    }
    if (Numerator <= Tolerances.ResidualToleranceCmps)
    {
      if (OutNumericalSummary)
      {
        ++OutNumericalSummary->RedundantParallelCount;
        ++OutNumericalSummary->StricterParallelCount;
        ++OutNumericalSummary->NumericalToleranceAcceptanceCount;
      }
      return true;
    }
    if (OutNumericalSummary) ++OutNumericalSummary->TrueParallelContradictionCount;
    return false;
  }
  const double Bound = Numerator / Denominator;
  if (!FMath::IsFinite(Bound)) return false;
  if (Denominator > 0.0)
  {
    InOutMinimumT = static_cast<float>(FMath::Max(static_cast<double>(InOutMinimumT), Bound));
  }
  else
  {
    InOutMaximumT = static_cast<float>(FMath::Min(static_cast<double>(InOutMaximumT), Bound));
  }
  return static_cast<double>(InOutMinimumT) <= static_cast<double>(InOutMaximumT) + Tolerances.ParameterTolerance;
}

bool FCrowdDemoDeterministicOrcaKernel::SolveVelocityForConstraints(
  const FVector2f PreferredVelocity,
  const float MaxSpeedCmps,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings,
  FVector2f& OutVelocity)
{
  FVector2f Continuous;
  if (!SolveVelocityHalfPlanes(
    PreferredVelocity, MaxSpeedCmps, Constraints, Settings, Continuous)) return false;
  return QuantizeAndValidateVelocity(
    Continuous, PreferredVelocity, MaxSpeedCmps, Constraints, Settings, OutVelocity);
}

FCrowdDemoSf4RouteForwardFeasibilityResult
FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4RouteForwardFeasibility(
  const FVector2f PreferredVelocity,
  const float MaxSpeedCmps,
  const float MinimumForwardSpeedCmps,
  const TConstArrayView<FCrowdDemoSf4SourcedOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings)
{
  FCrowdDemoSf4RouteForwardFeasibilityResult Result;
  TArray<FCrowdDemoSf4SourcedOrcaConstraint> SortedConstraints(Constraints);
  SortedConstraints.Sort(SourcedConstraintLess);
  Result.ConstraintCount = SortedConstraints.Num();
  const FVector2f Forward = NormalizeQ15(PreferredVelocity);
  const auto SolveList = [&](const TConstArrayView<FCrowdDemoSf4SourcedOrcaConstraint> Input,
    FVector2f* OutContinuous, FVector2f* OutQuantized)
  {
    TArray<FCrowdDemoOrcaConstraint> Filtered;
    int32 MaxOrder = 0;
    for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : Input)
    {
      Filtered.Add(Sourced.Constraint);
      MaxOrder = FMath::Max(MaxOrder, Sourced.Constraint.StableConstraintOrder);
    }
    FCrowdDemoOrcaConstraint ForwardConstraint;
    ForwardConstraint.OtherAgentId = INDEX_NONE;
    ForwardConstraint.Point = Forward * MinimumForwardSpeedCmps;
    ForwardConstraint.Normal = Forward;
    ForwardConstraint.StableConstraintOrder = MaxOrder + 1;
    Filtered.Add(ForwardConstraint);
    FVector2f Continuous;
    if (!SolveVelocityHalfPlanes(PreferredVelocity, MaxSpeedCmps,
      Filtered, Settings, Continuous)) return TPair<bool, bool>(false, false);
    FVector2f Quantized;
    const bool bQuantized = QuantizeAndValidateVelocity(
      Continuous, PreferredVelocity, MaxSpeedCmps, Filtered, Settings, Quantized);
    if (OutContinuous) *OutContinuous = Continuous;
    if (OutQuantized) *OutQuantized = Quantized;
    return TPair<bool, bool>(true, bQuantized);
  };
  const auto SolveFiltered = [&](const ECrowdDemoSf4RouteConstraintSource Excluded)
  {
    TArray<FCrowdDemoSf4SourcedOrcaConstraint> Filtered;
    for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : SortedConstraints)
      if (Sourced.Source != Excluded) Filtered.Add(Sourced);
    return SolveList(Filtered, nullptr, nullptr);
  };
  const TPair<bool, bool> Full = SolveList(SortedConstraints, &Result.ContinuousVelocity,
    &Result.QuantizedVelocity);
  Result.bContinuousFeasible = Full.Key;
  Result.bQuantizedFeasible = Full.Value;
  Result.bFeasibleWithoutActive = SolveFiltered(ECrowdDemoSf4RouteConstraintSource::Active).Value;
  Result.bFeasibleWithoutWaiting = SolveFiltered(ECrowdDemoSf4RouteConstraintSource::Waiting).Value;
  Result.bFeasibleWithoutStable = SolveFiltered(ECrowdDemoSf4RouteConstraintSource::Stable).Value;
  Result.bFeasibleWithoutOther = SolveFiltered(ECrowdDemoSf4RouteConstraintSource::Other).Value;
  if (!Result.bQuantizedFeasible)
  {
    TArray<FCrowdDemoSf4SourcedOrcaConstraint> Core = SortedConstraints;
    for (int32 Index = 0; Index < Core.Num();)
    {
      TArray<FCrowdDemoSf4SourcedOrcaConstraint> Candidate = Core;
      Candidate.RemoveAt(Index, 1, EAllowShrinking::No);
      if (!SolveList(Candidate, nullptr, nullptr).Value)
      {
        Core = MoveTemp(Candidate);
      }
      else
      {
        ++Index;
      }
    }
    TSet<int32> OtherAgentIds;
    for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : Core)
    {
      Result.IrreducibleCoreConstraintOrders.Add(
        Sourced.Constraint.StableConstraintOrder);
      if (Sourced.Constraint.OtherAgentId != INDEX_NONE)
        OtherAgentIds.Add(Sourced.Constraint.OtherAgentId);
    }
    Result.IrreducibleCoreConstraintCount = Core.Num();
    Result.IrreducibleCoreOtherAgentCount = OtherAgentIds.Num();
    Result.bFixtureTooLarge = OtherAgentIds.Num() > 4;
  }
  uint32 Hash = 2166136261u;
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(Forward.X * 32767.0f));
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(Forward.Y * 32767.0f));
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(MaxSpeedCmps));
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(MinimumForwardSpeedCmps));
  for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : SortedConstraints)
  {
    Hash = OrcaHashInt(Hash, Sourced.Constraint.OtherAgentId);
    Hash = OrcaHashInt(Hash, Sourced.Constraint.StableConstraintOrder);
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Sourced.Constraint.Point.X));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Sourced.Constraint.Point.Y));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Sourced.Constraint.Normal.X * 32767.0f));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Sourced.Constraint.Normal.Y * 32767.0f));
    Hash = OrcaHashInt(Hash, static_cast<int32>(Sourced.Source));
  }
  Hash = OrcaHashInt(Hash, Result.bContinuousFeasible ? 1 : 0);
  Hash = OrcaHashInt(Hash, Result.bQuantizedFeasible ? 1 : 0);
  Hash = OrcaHashInt(Hash, Result.bFeasibleWithoutActive ? 1 : 0);
  Hash = OrcaHashInt(Hash, Result.bFeasibleWithoutWaiting ? 1 : 0);
  Hash = OrcaHashInt(Hash, Result.bFeasibleWithoutStable ? 1 : 0);
  Hash = OrcaHashInt(Hash, Result.bFeasibleWithoutOther ? 1 : 0);
  Hash = OrcaHashInt(Hash, Result.IrreducibleCoreConstraintCount);
  Hash = OrcaHashInt(Hash, Result.IrreducibleCoreOtherAgentCount);
  Hash = OrcaHashInt(Hash, Result.bFixtureTooLarge ? 1 : 0);
  for (const int32 Order : Result.IrreducibleCoreConstraintOrders)
    Hash = OrcaHashInt(Hash, Order);
  Result.StableHash = Hash;
  return Result;
}

bool FCrowdDemoDeterministicOrcaKernel::IsSf4ReservationStepContained(
  const FCrowdDemoOrcaAgent& Agent,
  const FVector2f CandidateVelocity,
  const float FixedStepSeconds,
  const float SafetyGapCm)
{
  FVector2f SegmentStart;
  FVector2f SegmentEnd;
  if (!FindFirstReservationSegment(Agent, SegmentStart, SegmentEnd)) return false;
  const float CorridorRadius = FMath::FloorToFloat(
    FMath::Max(0.0f, (SafetyGapCm - 1.0f) * 0.5f));
  const FVector2f Next = Agent.Position + CandidateVelocity * FixedStepSeconds;
  return PointSegmentDistanceSquared(Agent.Position, SegmentStart, SegmentEnd)
      <= FMath::Square(CorridorRadius)
    && PointSegmentDistanceSquared(Next, SegmentStart, SegmentEnd)
      <= FMath::Square(CorridorRadius);
}

bool FCrowdDemoDeterministicOrcaKernel::AreSf4ReservationStepsPairSafe(
  const FCrowdDemoOrcaAgent& A,
  const FVector2f AVelocity,
  const FCrowdDemoOrcaAgent& B,
  const FVector2f BVelocity,
  const float FixedStepSeconds)
{
  const FVector2f ANext = A.Position + AVelocity * FixedStepSeconds;
  const FVector2f BNext = B.Position + BVelocity * FixedStepSeconds;
  const float RequiredDistance = A.RadiusCm + B.RadiusCm + 1.0f;
  return SegmentDistanceSquared(A.Position, ANext, B.Position, BNext)
    >= FMath::Square(RequiredDistance);
}

void FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4ReservationOrcaDiagnostic(
  const FCrowdDemoPursuitTargetFact& Target,
  const float SafetyGapCm,
  const float FixedStepSeconds,
  const float MinimumForwardSpeedCmps,
  const TConstArrayView<FCrowdDemoSf4ReservationOrcaFixtureAgent> Agents,
  const int32 PrimaryAgentId,
  const FCrowdDemoOrcaSettings& Settings,
  FCrowdDemoSf4ReservationOrcaDiagnosticFixture& OutFixture)
{
  OutFixture = {};
  OutFixture.Target = Target;
  OutFixture.SafetyGapCm = SafetyGapCm;
  OutFixture.FixedStepSeconds = FixedStepSeconds;
  OutFixture.MinimumForwardSpeedCmps = MinimumForwardSpeedCmps;
  TArray<FCrowdDemoSf4ReservationOrcaFixtureAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B)
  {
    return A.Agent.AgentId < B.Agent.AgentId;
  });
  const FCrowdDemoSf4ReservationOrcaFixtureAgent* Primary = SortedAgents.FindByPredicate(
    [&](const auto& Candidate) { return Candidate.Agent.AgentId == PrimaryAgentId; });
  if (!Primary || Primary->Agent.Sf4RouteMode != ECrowdDemoOrcaRouteMode::Active
    || Primary->Constraints.IsEmpty()) return;

  TArray<FCrowdDemoSf4SourcedOrcaConstraint> PrimaryConstraints = Primary->Constraints;
  PrimaryConstraints.Sort(SourcedConstraintLess);
  const FCrowdDemoSf4RouteForwardFeasibilityResult Feasibility =
    AnalyzeSf4RouteForwardFeasibility(Primary->Agent.PreferredVelocity,
      Primary->Agent.MaxSpeedCmps, MinimumForwardSpeedCmps,
      PrimaryConstraints, Settings);
  OutFixture.Summary.PrimaryAgentId = PrimaryAgentId;
  OutFixture.Summary.bContinuousFeasible = Feasibility.bContinuousFeasible;
  OutFixture.Summary.bQuantizedFeasible = Feasibility.bQuantizedFeasible;
  OutFixture.Summary.CoreConstraintCount = Feasibility.IrreducibleCoreConstraintCount;
  OutFixture.Summary.bFixtureTooLarge = Feasibility.bFixtureTooLarge;
  if (Feasibility.bQuantizedFeasible || Feasibility.bFixtureTooLarge
    || Feasibility.IrreducibleCoreConstraintCount <= 0) return;

  const auto SolveWithoutActive = [&](const FCrowdDemoSf4ReservationOrcaFixtureAgent& Input)
  {
    TArray<FCrowdDemoSf4SourcedOrcaConstraint> Filtered;
    for (const FCrowdDemoSf4SourcedOrcaConstraint& Constraint : Input.Constraints)
      if (Constraint.Source != ECrowdDemoSf4RouteConstraintSource::Active)
        Filtered.Add(Constraint);
    return AnalyzeSf4RouteForwardFeasibility(Input.Agent.PreferredVelocity,
      Input.Agent.MaxSpeedCmps, MinimumForwardSpeedCmps, Filtered, Settings);
  };
  TMap<int32, FVector2f> CandidateVelocityByAgentId;
  for (const FCrowdDemoSf4ReservationOrcaFixtureAgent& Agent : SortedAgents)
  {
    const FCrowdDemoSf4RouteForwardFeasibilityResult Relaxed = SolveWithoutActive(Agent);
    CandidateVelocityByAgentId.Add(Agent.Agent.AgentId,
      Relaxed.bQuantizedFeasible ? Relaxed.QuantizedVelocity : Agent.BaselineVelocity);
  }

  FCrowdDemoPursuitPositioningSettings PositionSettings;
  PositionSettings.SafetyGapCm = SafetyGapCm;
  TMap<int32, ECrowdDemoSf4ReservationConstraintClass> ClassByOrder;
  for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : PrimaryConstraints)
  {
    ECrowdDemoSf4ReservationConstraintClass Classification =
      ECrowdDemoSf4ReservationConstraintClass::Other;
    if (Sourced.Source == ECrowdDemoSf4RouteConstraintSource::Waiting)
      Classification = ECrowdDemoSf4ReservationConstraintClass::Waiting;
    else if (Sourced.Source == ECrowdDemoSf4RouteConstraintSource::Stable)
      Classification = ECrowdDemoSf4ReservationConstraintClass::Stable;
    else if (Sourced.Source == ECrowdDemoSf4RouteConstraintSource::Other)
      Classification = ECrowdDemoSf4ReservationConstraintClass::Other;
    else
    {
      const FCrowdDemoSf4ReservationOrcaFixtureAgent* Other = SortedAgents.FindByPredicate(
        [&](const auto& Candidate)
        {
          return Candidate.Agent.AgentId == Sourced.Constraint.OtherAgentId;
        });
      if (!Other || FCrowdDemoPursuitPositioningKernel::FrontReservationPathsConflict(
          PositionSettings, Primary->Agent.Sf4RoutePoints, Primary->Agent.RadiusCm,
          Other->Agent.Sf4RoutePoints, Other->Agent.RadiusCm))
      {
        Classification = ECrowdDemoSf4ReservationConstraintClass::ActiveRouteConflict;
      }
      else
      {
        const FVector2f PrimaryVelocity = CandidateVelocityByAgentId.FindRef(PrimaryAgentId);
        const FVector2f OtherVelocity = CandidateVelocityByAgentId.FindRef(Other->Agent.AgentId);
        const FVector2f PrimaryNext = Primary->Agent.Position + PrimaryVelocity * FixedStepSeconds;
        const FVector2f OtherNext = Other->Agent.Position + OtherVelocity * FixedStepSeconds;
        const float PrimaryExclusion = Target.RadiusCm + Primary->Agent.RadiusCm + SafetyGapCm;
        const float OtherExclusion = Target.RadiusCm + Other->Agent.RadiusCm + SafetyGapCm;
        const bool bContained = IsSf4ReservationStepContained(
            Primary->Agent, PrimaryVelocity, FixedStepSeconds, SafetyGapCm)
          && IsSf4ReservationStepContained(
            Other->Agent, OtherVelocity, FixedStepSeconds, SafetyGapCm)
          && AreSf4ReservationStepsPairSafe(Primary->Agent, PrimaryVelocity,
            Other->Agent, OtherVelocity, FixedStepSeconds)
          && (PrimaryNext - Target.Location).SizeSquared() >= FMath::Square(PrimaryExclusion)
          && (OtherNext - Target.Location).SizeSquared() >= FMath::Square(OtherExclusion);
        Classification = bContained
          ? ECrowdDemoSf4ReservationConstraintClass::ActiveRouteDisjointContained
          : ECrowdDemoSf4ReservationConstraintClass::ActiveRouteDisjointOutsideCorridor;
      }
    }
    ClassByOrder.Add(Sourced.Constraint.StableConstraintOrder, Classification);
  }

  TSet<int32> FixtureAgentIds;
  FixtureAgentIds.Add(PrimaryAgentId);
  for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : PrimaryConstraints)
  {
    if (!Feasibility.IrreducibleCoreConstraintOrders.Contains(
      Sourced.Constraint.StableConstraintOrder)) continue;
    FCrowdDemoSf4ClassifiedReservationConstraint& Core =
      OutFixture.CoreConstraints.AddDefaulted_GetRef();
    Core.PrimaryAgentId = PrimaryAgentId;
    Core.OtherAgentId = Sourced.Constraint.OtherAgentId;
    Core.StableConstraintOrder = Sourced.Constraint.StableConstraintOrder;
    Core.Classification = ClassByOrder.FindRef(Sourced.Constraint.StableConstraintOrder);
    if (Core.OtherAgentId != INDEX_NONE) FixtureAgentIds.Add(Core.OtherAgentId);
    switch (Core.Classification)
    {
    case ECrowdDemoSf4ReservationConstraintClass::ActiveRouteConflict:
      ++OutFixture.Summary.ActiveRouteConflictCount; break;
    case ECrowdDemoSf4ReservationConstraintClass::ActiveRouteDisjointContained:
      ++OutFixture.Summary.ActiveRouteDisjointContainedCount; break;
    case ECrowdDemoSf4ReservationConstraintClass::ActiveRouteDisjointOutsideCorridor:
      ++OutFixture.Summary.ActiveRouteDisjointOutsideCorridorCount; break;
    case ECrowdDemoSf4ReservationConstraintClass::Waiting:
      ++OutFixture.Summary.WaitingCount; break;
    case ECrowdDemoSf4ReservationConstraintClass::Stable:
      ++OutFixture.Summary.StableCount; break;
    default: ++OutFixture.Summary.OtherCount; break;
    }
  }
  if (FixtureAgentIds.Num() < 2 || FixtureAgentIds.Num() > 5)
  {
    OutFixture.Summary.bFixtureTooLarge = FixtureAgentIds.Num() > 5;
    return;
  }
  for (const FCrowdDemoSf4ReservationOrcaFixtureAgent& Agent : SortedAgents)
  {
    if (!FixtureAgentIds.Contains(Agent.Agent.AgentId)) continue;
    FCrowdDemoSf4ReservationOrcaFixtureAgent Stored = Agent;
    if (Stored.Agent.AgentId == PrimaryAgentId)
    {
      Stored.Constraints.RemoveAll([&](const FCrowdDemoSf4SourcedOrcaConstraint& Constraint)
      {
        return !Feasibility.IrreducibleCoreConstraintOrders.Contains(
          Constraint.Constraint.StableConstraintOrder);
      });
      Stored.Constraints.Sort(SourcedConstraintLess);
    }
    else
    {
      Stored.Constraints.Reset();
    }
    OutFixture.Agents.Add(MoveTemp(Stored));
  }

  const auto FeasibleAfterRemoving = [&](const ECrowdDemoSf4ReservationConstraintClass Removed)
  {
    TArray<FCrowdDemoSf4SourcedOrcaConstraint> Filtered;
    for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : PrimaryConstraints)
      if (ClassByOrder.FindRef(Sourced.Constraint.StableConstraintOrder) != Removed)
        Filtered.Add(Sourced);
    const FCrowdDemoSf4RouteForwardFeasibilityResult Candidate =
      AnalyzeSf4RouteForwardFeasibility(Primary->Agent.PreferredVelocity,
        Primary->Agent.MaxSpeedCmps, MinimumForwardSpeedCmps, Filtered, Settings);
    return Candidate.bContinuousFeasible && Candidate.bQuantizedFeasible;
  };
  OutFixture.Summary.bOnlyDisjointContainedActiveRestoresFeasibility =
    OutFixture.Summary.ActiveRouteDisjointContainedCount > 0
    && OutFixture.Summary.ActiveRouteConflictCount == 0
    && OutFixture.Summary.ActiveRouteDisjointOutsideCorridorCount == 0
    && FeasibleAfterRemoving(
      ECrowdDemoSf4ReservationConstraintClass::ActiveRouteDisjointContained);
  OutFixture.Summary.bConflictActiveRestoresFeasibility =
    OutFixture.Summary.ActiveRouteConflictCount > 0
    && FeasibleAfterRemoving(ECrowdDemoSf4ReservationConstraintClass::ActiveRouteConflict);
  OutFixture.Summary.bOutsideCorridorActiveRestoresFeasibility =
    OutFixture.Summary.ActiveRouteDisjointOutsideCorridorCount > 0
    && OutFixture.Summary.ActiveRouteConflictCount == 0
    && FeasibleAfterRemoving(
      ECrowdDemoSf4ReservationConstraintClass::ActiveRouteDisjointOutsideCorridor);

  uint32 Hash = 2166136261u;
  Hash = OrcaHashInt(Hash, Target.TargetId);
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(Target.Location.X));
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(Target.Location.Y));
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(Target.RadiusCm));
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(SafetyGapCm));
  Hash = OrcaHashInt(Hash, FMath::RoundToInt(FixedStepSeconds * 30000.0f));
  for (const FCrowdDemoSf4ReservationOrcaFixtureAgent& Agent : OutFixture.Agents)
  {
    Hash = OrcaHashInt(Hash, Agent.Agent.AgentId);
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.Position.X));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.Position.Y));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.Velocity.X));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.Velocity.Y));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.PreferredVelocity.X));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.PreferredVelocity.Y));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.BaselineVelocity.X));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.BaselineVelocity.Y));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.RadiusCm));
    Hash = OrcaHashInt(Hash, FMath::RoundToInt(Agent.Agent.MaxSpeedCmps));
    Hash = OrcaHashInt(Hash, static_cast<int32>(Agent.PositionState));
    Hash = OrcaHashInt(Hash, static_cast<int32>(Agent.CurrentPhase));
    for (const FVector2f Point : Agent.Agent.Sf4RoutePoints)
    {
      Hash = OrcaHashInt(Hash, FMath::RoundToInt(Point.X));
      Hash = OrcaHashInt(Hash, FMath::RoundToInt(Point.Y));
    }
    for (const FCrowdDemoSf4SourcedOrcaConstraint& Sourced : Agent.Constraints)
    {
      Hash = OrcaHashInt(Hash, Sourced.Constraint.OtherAgentId);
      Hash = OrcaHashInt(Hash, Sourced.Constraint.StableConstraintOrder);
      Hash = OrcaHashInt(Hash, FMath::RoundToInt(Sourced.Constraint.Point.X));
      Hash = OrcaHashInt(Hash, FMath::RoundToInt(Sourced.Constraint.Point.Y));
      Hash = OrcaHashInt(Hash,
        FMath::RoundToInt(Sourced.Constraint.Normal.X * 32767.0f));
      Hash = OrcaHashInt(Hash,
        FMath::RoundToInt(Sourced.Constraint.Normal.Y * 32767.0f));
      Hash = OrcaHashInt(Hash, static_cast<int32>(Sourced.Source));
    }
  }
  for (const FCrowdDemoSf4ClassifiedReservationConstraint& Core : OutFixture.CoreConstraints)
  {
    Hash = OrcaHashInt(Hash, Core.PrimaryAgentId);
    Hash = OrcaHashInt(Hash, Core.OtherAgentId);
    Hash = OrcaHashInt(Hash, Core.StableConstraintOrder);
    Hash = OrcaHashInt(Hash, static_cast<int32>(Core.Classification));
  }
  OutFixture.bValid = true;
  OutFixture.Summary.bValid = true;
  OutFixture.Summary.FixtureAgentCount = OutFixture.Agents.Num();
  OutFixture.Summary.StableHash = Hash;
  OutFixture.StableHash = Hash;
}

#if WITH_DEV_AUTOMATION_TESTS
FCrowdDemoOrcaFeasibilityOracleResult FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
  const FVector2f PreferredVelocity,
  const float MaxSpeedCmps,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings)
{
  struct FOracleCandidate
  {
    FVector2f Velocity = FVector2f::ZeroVector;
    ECrowdDemoOrcaOracleCandidateKind Kind = ECrowdDemoOrcaOracleCandidateKind::Zero;
    int32 FirstOrder = INDEX_NONE;
    int32 SecondOrder = INDEX_NONE;
  };
  TArray<FOracleCandidate> Candidates;
  const FVector2f Preferred = ClampSpeed(
    Quantize(PreferredVelocity, Settings.VelocityQuantumCmps), MaxSpeedCmps);
  Candidates.Add({FVector2f::ZeroVector, ECrowdDemoOrcaOracleCandidateKind::Zero, -2, -2});
  Candidates.Add({Preferred, ECrowdDemoOrcaOracleCandidateKind::Preferred, -1, -1});
  for (int32 Index = 0; Index < Constraints.Num(); ++Index)
  {
    const FCrowdDemoOrcaConstraint& Constraint = Constraints[Index];
    const float SignedDistance = FVector2f::DotProduct(
      Preferred - Constraint.Point, Constraint.Normal);
    Candidates.Add({Preferred - Constraint.Normal * SignedDistance,
      ECrowdDemoOrcaOracleCandidateKind::LineProjection,
      Constraint.StableConstraintOrder, INDEX_NONE});
    const FVector2f Direction(-Constraint.Normal.Y, Constraint.Normal.X);
    const float Projection = FVector2f::DotProduct(Constraint.Point, Direction);
    const float Discriminant = FMath::Square(Projection)
      + FMath::Square(MaxSpeedCmps) - Constraint.Point.SizeSquared();
    if (Discriminant >= -Settings.ConstraintEpsilonCmps)
    {
      const float Root = FMath::Sqrt(FMath::Max(0.0f, Discriminant));
      Candidates.Add({Constraint.Point + Direction * (-Projection - Root),
        ECrowdDemoOrcaOracleCandidateKind::LineCircleIntersection,
        Constraint.StableConstraintOrder, 0});
      Candidates.Add({Constraint.Point + Direction * (-Projection + Root),
        ECrowdDemoOrcaOracleCandidateKind::LineCircleIntersection,
        Constraint.StableConstraintOrder, 1});
    }
    for (int32 OtherIndex = Index + 1; OtherIndex < Constraints.Num(); ++OtherIndex)
    {
      const FCrowdDemoOrcaConstraint& Other = Constraints[OtherIndex];
      const float DeterminantValue = Determinant(Constraint.Normal, Other.Normal);
      if (FMath::Abs(DeterminantValue) <= 1.0e-6f) continue;
      const float C0 = FVector2f::DotProduct(Constraint.Point, Constraint.Normal);
      const float C1 = FVector2f::DotProduct(Other.Point, Other.Normal);
      const FVector2f Intersection(
        (C0 * Other.Normal.Y - Constraint.Normal.Y * C1) / DeterminantValue,
        (Constraint.Normal.X * C1 - C0 * Other.Normal.X) / DeterminantValue);
      Candidates.Add({Intersection, ECrowdDemoOrcaOracleCandidateKind::LineIntersection,
        Constraint.StableConstraintOrder, Other.StableConstraintOrder});
    }
  }
  Candidates.Sort([&](const FOracleCandidate& A, const FOracleCandidate& B)
  {
    const bool bAFeasible = FMath::IsFinite(A.Velocity.X) && FMath::IsFinite(A.Velocity.Y)
      && InsideSpeedCircle(A.Velocity, MaxSpeedCmps, Settings.ConstraintEpsilonCmps)
      && Satisfies(A.Velocity, Constraints, Settings.ConstraintEpsilonCmps);
    const bool bBFeasible = FMath::IsFinite(B.Velocity.X) && FMath::IsFinite(B.Velocity.Y)
      && InsideSpeedCircle(B.Velocity, MaxSpeedCmps, Settings.ConstraintEpsilonCmps)
      && Satisfies(B.Velocity, Constraints, Settings.ConstraintEpsilonCmps);
    if (bAFeasible != bBFeasible) return bAFeasible;
    const float ADistance = (A.Velocity - Preferred).SizeSquared();
    const float BDistance = (B.Velocity - Preferred).SizeSquared();
    if (!FMath::IsNearlyEqual(ADistance, BDistance, 1.0e-4f)) return ADistance < BDistance;
    if (A.Kind != B.Kind) return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
    if (A.FirstOrder != B.FirstOrder) return A.FirstOrder < B.FirstOrder;
    if (A.SecondOrder != B.SecondOrder) return A.SecondOrder < B.SecondOrder;
    if (A.Velocity.X != B.Velocity.X) return A.Velocity.X < B.Velocity.X;
    return A.Velocity.Y < B.Velocity.Y;
  });
  FCrowdDemoOrcaFeasibilityOracleResult Result;
  Result.CandidateCount = Candidates.Num();
  Result.bZeroVelocityFeasible = InsideSpeedCircle(
    FVector2f::ZeroVector, MaxSpeedCmps, Settings.ConstraintEpsilonCmps)
    && Satisfies(FVector2f::ZeroVector, Constraints, Settings.ConstraintEpsilonCmps);
  for (const FOracleCandidate& Candidate : Candidates)
  {
    if (!FMath::IsFinite(Candidate.Velocity.X) || !FMath::IsFinite(Candidate.Velocity.Y)
      || !InsideSpeedCircle(Candidate.Velocity, MaxSpeedCmps, Settings.ConstraintEpsilonCmps)
      || !Satisfies(Candidate.Velocity, Constraints, Settings.ConstraintEpsilonCmps)) continue;
    Result.bFoundFeasibleWitness = true;
    Result.WitnessVelocity = Candidate.Velocity;
    Result.WitnessKind = Candidate.Kind;
    break;
  }
  for (const FOracleCandidate& Candidate : Candidates)
  {
    if (!FMath::IsFinite(Candidate.Velocity.X) || !FMath::IsFinite(Candidate.Velocity.Y)) continue;
    FVector2f QuantizedWitness;
    if (FindQuantizedFeasibleVelocity(
      Candidate.Velocity, Preferred, MaxSpeedCmps, Constraints, Settings, QuantizedWitness))
    {
      Result.bFoundQuantizedWitness = true;
      Result.QuantizedWitnessVelocity = QuantizedWitness;
      break;
    }
  }
  return Result;
}
#endif

FCrowdDemoOrcaContinuousSolveInput FCrowdDemoDeterministicOrcaKernel::MakeContinuousSolveInput(
  const FVector2f PreferredVelocity,
  const float MaxSpeedCmps,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const float BehaviorEpsilonCmps)
{
  FCrowdDemoOrcaContinuousSolveInput Input;
  Input.PreferredVelocity = PreferredVelocity;
  Input.MaxSpeedCmps = MaxSpeedCmps;
  Input.BehaviorEpsilonCmps = BehaviorEpsilonCmps;
  Input.HalfPlanes.Reserve(Constraints.Num());
  for (const FCrowdDemoOrcaConstraint& Constraint : Constraints)
    Input.HalfPlanes.Add({Constraint.Point, Constraint.Normal, Constraint.StableConstraintOrder});
  Input.HalfPlanes.Sort([](const FCrowdDemoOrcaHalfPlane& A, const FCrowdDemoOrcaHalfPlane& B)
  {
    return A.StableOrder < B.StableOrder;
  });
  return Input;
}

FCrowdDemoOrcaContinuousSolveResult FCrowdDemoDeterministicOrcaKernel::SolveContinuousExact(
  const FCrowdDemoOrcaContinuousSolveInput& Input,
  FCrowdDemoOrcaNumericalSummary* OutNumericalSummary)
{
  TArray<FCrowdDemoOrcaConstraint> Constraints;
  Constraints.Reserve(Input.HalfPlanes.Num());
  for (const FCrowdDemoOrcaHalfPlane& HalfPlane : Input.HalfPlanes)
  {
    FCrowdDemoOrcaConstraint& Constraint = Constraints.AddDefaulted_GetRef();
    Constraint.Point = HalfPlane.Point;
    Constraint.Normal = HalfPlane.Normal;
    Constraint.StableConstraintOrder = HalfPlane.StableOrder;
  }
  FCrowdDemoOrcaSettings Settings;
  Settings.ConstraintEpsilonCmps = Input.BehaviorEpsilonCmps;
  FCrowdDemoOrcaContinuousSolveResult Result;
  if (InsideSpeedCircle(Input.PreferredVelocity, Input.MaxSpeedCmps, Input.BehaviorEpsilonCmps)
    && Satisfies(Input.PreferredVelocity, Constraints, Input.BehaviorEpsilonCmps))
  {
    Result.Status = ECrowdDemoOrcaSolveStatus::PreferredFeasible;
    Result.Velocity = Input.PreferredVelocity;
    Result.bSatisfiesAllHalfPlanes = true;
    return Result;
  }
  if (SolveVelocityHalfPlanes(Input.PreferredVelocity, Input.MaxSpeedCmps,
    Constraints, Settings, Result.Velocity, OutNumericalSummary))
  {
    Result.Status = ECrowdDemoOrcaSolveStatus::ExactFeasible;
    Result.bSatisfiesAllHalfPlanes = ValidateContinuousVelocity(Input, Result.Velocity);
    if (!Result.bSatisfiesAllHalfPlanes) Result.Status = ECrowdDemoOrcaSolveStatus::NumericalFailure;
    return Result;
  }
  Result.Status = ECrowdDemoOrcaSolveStatus::ProvenInfeasible;
  return Result;
}

bool FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(
  const FCrowdDemoOrcaContinuousSolveInput& Input,
  const FVector2f Velocity)
{
  const FCrowdDemoOrcaNumericalTolerances Tolerances =
    ComputeNumericalTolerances(Input.MaxSpeedCmps);
  if (!FMath::IsFinite(Velocity.X) || !FMath::IsFinite(Velocity.Y)
    || !InsideSpeedCircle(Velocity, Input.MaxSpeedCmps, Input.BehaviorEpsilonCmps)) return false;
  for (const FCrowdDemoOrcaHalfPlane& HalfPlane : Input.HalfPlanes)
  {
    const double DeltaX = static_cast<double>(Velocity.X) - HalfPlane.Point.X;
    const double DeltaY = static_cast<double>(Velocity.Y) - HalfPlane.Point.Y;
    const double Scalar = DeltaX * HalfPlane.Normal.X + DeltaY * HalfPlane.Normal.Y;
    if (Scalar < -static_cast<double>(Input.BehaviorEpsilonCmps)
      - Tolerances.ResidualToleranceCmps) return false;
  }
  return true;
}

bool FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
  const FVector2f PreferredVelocity,
  const float MaxSpeedCmps,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings,
  FVector2f& OutContinuousVelocity,
  FCrowdDemoOrcaNumericalSummary* OutNumericalSummary)
{
  const FVector2f Preferred = ClampSpeed(
    Quantize(PreferredVelocity, Settings.VelocityQuantumCmps), MaxSpeedCmps);
  FVector2f Continuous = Preferred;
  for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
  {
    const FCrowdDemoOrcaConstraint& Constraint = Constraints[ConstraintIndex];
    const double DeltaX = static_cast<double>(Continuous.X) - Constraint.Point.X;
    const double DeltaY = static_cast<double>(Continuous.Y) - Constraint.Point.Y;
    const double Scalar = DeltaX * Constraint.Normal.X + DeltaY * Constraint.Normal.Y;
    const FCrowdDemoOrcaNumericalTolerances Tolerances = ComputeNumericalTolerances(MaxSpeedCmps);
    if (Scalar >= -static_cast<double>(Settings.ConstraintEpsilonCmps)
      - Tolerances.ResidualToleranceCmps)
    {
      continue;
    }
    if (!SolveOnConstraintBoundary(
      ConstraintIndex, Preferred, MaxSpeedCmps, Constraints,
      Settings.ConstraintEpsilonCmps, Continuous, OutNumericalSummary))
    {
      return false;
    }
  }
  if (!SatisfiesFormalNumerically(
      Continuous, Constraints, Settings.ConstraintEpsilonCmps, MaxSpeedCmps)
    || !InsideSpeedCircle(Continuous, MaxSpeedCmps, Settings.ConstraintEpsilonCmps))
  {
    return false;
  }
  OutContinuousVelocity = Continuous;
  return true;
}

bool FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocity(
  const FVector2f ContinuousVelocity,
  const FVector2f PreferredVelocity,
  const float MaxSpeedCmps,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings,
  FVector2f& OutVelocity)
{
  return QuantizeAndValidateVelocityDetailed(
    ContinuousVelocity, PreferredVelocity, MaxSpeedCmps, Constraints, Settings, OutVelocity)
    != ECrowdDemoOrcaQuantizationResult::NoSolution;
}

ECrowdDemoOrcaQuantizationResult FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
  const FVector2f ContinuousVelocity,
  const FVector2f PreferredVelocity,
  const float MaxSpeedCmps,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings,
  FVector2f& OutVelocity)
{
  const FVector2f Preferred = ClampSpeed(
    Quantize(PreferredVelocity, Settings.VelocityQuantumCmps), MaxSpeedCmps);
  const FVector2f Rounded = Quantize(ContinuousVelocity, Settings.VelocityQuantumCmps);
  if (InsideSpeedCircle(Rounded, MaxSpeedCmps, Settings.ConstraintEpsilonCmps)
    && Satisfies(Rounded, Constraints, Settings.ConstraintEpsilonCmps))
  {
    OutVelocity = Rounded;
    return ECrowdDemoOrcaQuantizationResult::CenterFeasible;
  }
  if (FindQuantizedFeasibleVelocity(
    ContinuousVelocity, Preferred, MaxSpeedCmps, Constraints, Settings, OutVelocity))
    return ECrowdDemoOrcaQuantizationResult::NeighborhoodRecovered;
  if (FindGeometryQuantizedFeasibleVelocity(
    ContinuousVelocity, Preferred, MaxSpeedCmps, Constraints, Settings, OutVelocity))
    return ECrowdDemoOrcaQuantizationResult::GeometryRecovered;
  return ECrowdDemoOrcaQuantizationResult::NoSolution;
}

bool FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
  const FCrowdDemoOrcaAgent& Agent,
  const FCrowdDemoOrcaAgent& Other,
  const FCrowdDemoOrcaSettings& Settings,
  const float FixedStepSeconds,
  const int32 StableConstraintOrder,
  FCrowdDemoOrcaConstraint& OutConstraint)
{
  FCrowdDemoOrcaCanonicalPairGeometry Geometry;
  if (!BuildCanonicalPairGeometry(
      Agent, Other, Settings, FixedStepSeconds, Geometry)) return false;
  const int32 PriorityComparison = ComparePriorityKeys(
    MakePriorityKey(Agent), MakePriorityKey(Other));
  const float Responsibility = PriorityComparison == 0 ? 0.5f
    : (PriorityComparison > 0 ? 0.25f : 0.75f);
  OutConstraint = FCrowdDemoOrcaConstraint();
  OutConstraint.OtherAgentId = Other.AgentId;
  OutConstraint.Normal = Geometry.Normal;
  OutConstraint.Point = Quantize(
    Geometry.QuantizedAgentVelocity + Geometry.Correction * Responsibility,
    Settings.VelocityQuantumCmps);
  OutConstraint.Responsibility = Responsibility;
  OutConstraint.CombinedRadiusCm = Geometry.CombinedRadiusCm;
  OutConstraint.DistanceCm = Geometry.DistanceCm;
  OutConstraint.TimeHorizonSeconds = Geometry.TimeHorizonSeconds;
  OutConstraint.Kind = Geometry.Kind;
  OutConstraint.StableConstraintOrder = StableConstraintOrder;
  return true;
}

bool FCrowdDemoDeterministicOrcaKernel::BuildCanonicalPairGeometry(
  const FCrowdDemoOrcaAgent& Agent,
  const FCrowdDemoOrcaAgent& Other,
  const FCrowdDemoOrcaSettings& Settings,
  const float FixedStepSeconds,
  FCrowdDemoOrcaCanonicalPairGeometry& OutGeometry)
{
  OutGeometry = FCrowdDemoOrcaCanonicalPairGeometry();
  const FVector2f AgentPosition = Quantize(Agent.Position, 1.0f);
  const FVector2f OtherPosition = Quantize(Other.Position, 1.0f);
  const FVector2f AgentVelocity = Quantize(Agent.Velocity, Settings.VelocityQuantumCmps);
  const FVector2f OtherVelocity = Quantize(Other.Velocity, Settings.VelocityQuantumCmps);
  FVector2f RelativePosition = OtherPosition - AgentPosition;
  const FVector2f RelativeVelocity = AgentVelocity - OtherVelocity;
  float DistanceSquared = RelativePosition.SizeSquared();
  const float CombinedRadius = Agent.RadiusCm + Other.RadiusCm;
  const float CombinedRadiusSquared = FMath::Square(CombinedRadius);
  const float Horizon = FMath::Max(FixedStepSeconds, Settings.TimeHorizonSeconds);
  FVector2f LineDirection = FVector2f::ZeroVector;
  FVector2f Correction = FVector2f::ZeroVector;
  ECrowdDemoOrcaConstraintKind Kind = ECrowdDemoOrcaConstraintKind::None;

  if (DistanceSquared > CombinedRadiusSquared)
  {
    const float InverseHorizon = 1.0f / Horizon;
    const FVector2f W = RelativeVelocity - RelativePosition * InverseHorizon;
    const float WLengthSquared = W.SizeSquared();
    const float Dot = FVector2f::DotProduct(W, RelativePosition);
    if (Dot < 0.0f && FMath::Square(Dot) > CombinedRadiusSquared * WLengthSquared)
    {
      const float WLength = FMath::Sqrt(FMath::Max(WLengthSquared, KINDA_SMALL_NUMBER));
      const FVector2f UnitW = WLengthSquared > KINDA_SMALL_NUMBER
        ? W / WLength
        : StableCoincidentPairDirection(Agent.AgentId, Other.AgentId);
      LineDirection = FVector2f(UnitW.Y, -UnitW.X);
      Correction = UnitW * (CombinedRadius * InverseHorizon - WLength);
      Kind = ECrowdDemoOrcaConstraintKind::CutoffCircle;
    }
    else
    {
      const float Leg = FMath::Sqrt(FMath::Max(0.0f, DistanceSquared - CombinedRadiusSquared));
      if (Determinant(RelativePosition, W) > 0.0f)
      {
        LineDirection = FVector2f(
          RelativePosition.X * Leg - RelativePosition.Y * CombinedRadius,
          RelativePosition.X * CombinedRadius + RelativePosition.Y * Leg) / DistanceSquared;
        Kind = ECrowdDemoOrcaConstraintKind::LeftLeg;
      }
      else
      {
        LineDirection = -FVector2f(
          RelativePosition.X * Leg + RelativePosition.Y * CombinedRadius,
          -RelativePosition.X * CombinedRadius + RelativePosition.Y * Leg) / DistanceSquared;
        Kind = ECrowdDemoOrcaConstraintKind::RightLeg;
      }
      const float Projection = FVector2f::DotProduct(RelativeVelocity, LineDirection);
      Correction = LineDirection * Projection - RelativeVelocity;
    }
  }
  else
  {
    if (DistanceSquared <= KINDA_SMALL_NUMBER)
    {
      RelativePosition = StableCoincidentPairDirection(Agent.AgentId, Other.AgentId);
      DistanceSquared = 0.0f;
    }
    const float InverseStep = 1.0f / FMath::Max(FixedStepSeconds, 0.001f);
    const FVector2f W = RelativeVelocity - RelativePosition * InverseStep;
    const float WLength = W.Size();
    const FVector2f UnitW = WLength > KINDA_SMALL_NUMBER
      ? W / WLength
      : StableCoincidentPairDirection(Agent.AgentId, Other.AgentId);
    LineDirection = FVector2f(UnitW.Y, -UnitW.X);
    Correction = UnitW * (CombinedRadius * InverseStep - WLength);
    const float MaximumRelativeCorrection = Agent.MaxSpeedCmps + Other.MaxSpeedCmps;
    if (Correction.SizeSquared() > FMath::Square(MaximumRelativeCorrection))
      Correction = Correction.GetSafeNormal() * MaximumRelativeCorrection;
    Kind = ECrowdDemoOrcaConstraintKind::Penetration;
  }

  OutGeometry.AgentId = Agent.AgentId;
  OutGeometry.OtherAgentId = Other.AgentId;
  OutGeometry.QuantizedAgentVelocity = AgentVelocity;
  OutGeometry.QuantizedOtherVelocity = OtherVelocity;
  OutGeometry.Correction = Correction;
  OutGeometry.RelativeVelocityPoint = AgentVelocity - OtherVelocity + Correction;
  OutGeometry.Normal = NormalizeQ15(FVector2f(-LineDirection.Y, LineDirection.X));
  OutGeometry.CombinedRadiusCm = CombinedRadius;
  OutGeometry.DistanceCm = FMath::Sqrt(FMath::Max(0.0f, DistanceSquared));
  OutGeometry.TimeHorizonSeconds = Kind == ECrowdDemoOrcaConstraintKind::Penetration
    ? FixedStepSeconds : Horizon;
  OutGeometry.Kind = Kind;
  OutGeometry.bValid = FMath::IsFinite(OutGeometry.RelativeVelocityPoint.X)
    && FMath::IsFinite(OutGeometry.RelativeVelocityPoint.Y)
    && FMath::IsFinite(OutGeometry.Normal.X) && FMath::IsFinite(OutGeometry.Normal.Y);
  if (!OutGeometry.bValid) return false;
  return true;
}

void FCrowdDemoDeterministicOrcaKernel::BuildAgentConstraints(
  const FCrowdDemoOrcaAgent& Agent,
  const TConstArrayView<FCrowdDemoOrcaAgent> SortedAgents,
  const TConstArrayView<FCrowdDemoOrcaNeighbor> Neighbors,
  const FCrowdDemoOrcaSettings& Settings,
  const float FixedStepSeconds,
  TArray<FCrowdDemoOrcaConstraint>& OutConstraints)
{
  OutConstraints.Reset(Neighbors.Num());
  TMap<int32, const FCrowdDemoOrcaAgent*> AgentById;
  for (const FCrowdDemoOrcaAgent& Candidate : SortedAgents)
    AgentById.Add(Candidate.AgentId, &Candidate);
  for (int32 NeighborIndex = 0; NeighborIndex < Neighbors.Num(); ++NeighborIndex)
  {
    const FCrowdDemoOrcaAgent* const* Other = AgentById.Find(Neighbors[NeighborIndex].AgentId);
    if (!Other) continue;
    FCrowdDemoOrcaConstraint Constraint;
    if (BuildPairConstraint(
      Agent, **Other, Settings, FixedStepSeconds, NeighborIndex, Constraint))
      OutConstraints.Add(Constraint);
  }
}

uint8 FCrowdDemoDeterministicOrcaKernel::SelectFallback(
  const FCrowdDemoOrcaAgent& Agent,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings,
  FVector2f& OutVelocity,
  ECrowdDemoOrcaFeasibility& OutFeasibility)
{
  const FVector2f Candidates[] = {
    ClampSpeed(Quantize(Agent.PreferredVelocity, Settings.VelocityQuantumCmps), Agent.MaxSpeedCmps),
    Quantize(NormalizeStable(Agent.FlowDirection, Agent.AgentId, 1)
      * (Agent.MaxSpeedCmps * 0.5f), Settings.VelocityQuantumCmps),
    Quantize(NormalizeStable(Agent.PortalDirection, Agent.AgentId, 2)
      * (Agent.MaxSpeedCmps * 0.35f), Settings.VelocityQuantumCmps),
    FVector2f::ZeroVector };
  for (uint8 CandidateIndex = 0; CandidateIndex < UE_ARRAY_COUNT(Candidates); ++CandidateIndex)
  {
    if (!InsideSpeedCircle(Candidates[CandidateIndex], Agent.MaxSpeedCmps,
      Settings.ConstraintEpsilonCmps)
      || !Satisfies(Candidates[CandidateIndex], Constraints, Settings.ConstraintEpsilonCmps))
      continue;
    OutVelocity = Candidates[CandidateIndex];
    if (CandidateIndex == 1) OutFeasibility = ECrowdDemoOrcaFeasibility::FallbackFlowFeasible;
    else if (CandidateIndex == 2) OutFeasibility = ECrowdDemoOrcaFeasibility::FallbackPortalFeasible;
    else if (CandidateIndex == 3) OutFeasibility = ECrowdDemoOrcaFeasibility::StopFeasible;
    return CandidateIndex + 1;
  }
  OutVelocity = FVector2f::ZeroVector;
  OutFeasibility = ECrowdDemoOrcaFeasibility::StopViolation;
  return 4;
}

void FCrowdDemoDeterministicOrcaKernel::BuildNeighbors(
  const TConstArrayView<FCrowdDemoOrcaAgent> Agents,
  const FCrowdDemoOrcaSettings& Settings,
  TArray<TArray<FCrowdDemoOrcaNeighbor>>& OutNeighbors)
{
  TArray<FCrowdDemoOrcaAgent> Sorted(Agents);
  Sorted.Sort([](const FCrowdDemoOrcaAgent& A, const FCrowdDemoOrcaAgent& B) { return A.AgentId < B.AgentId; });
  const float CellSize = FMath::Max(1.0f, Settings.NeighborDistanceCm);
  TMap<FIntPoint, TArray<int32>> Grid;
  for (int32 Index=0; Index<Sorted.Num(); ++Index)
  {
    const FIntPoint Key(FMath::FloorToInt(Sorted[Index].Position.X/CellSize), FMath::FloorToInt(Sorted[Index].Position.Y/CellSize));
    Grid.FindOrAdd(Key).Add(Index);
  }
  for (TPair<FIntPoint,TArray<int32>>& Pair : Grid)
    Pair.Value.Sort([&](const int32 A,const int32 B){return Sorted[A].AgentId<Sorted[B].AgentId;});
  OutNeighbors.SetNum(Sorted.Num());
  const float MaxDistanceSq = FMath::Square(Settings.NeighborDistanceCm);
  for (int32 Index=0; Index<Sorted.Num(); ++Index)
  {
    const FCrowdDemoOrcaAgent& Agent = Sorted[Index];
    const FIntPoint Base(FMath::FloorToInt(Agent.Position.X/CellSize), FMath::FloorToInt(Agent.Position.Y/CellSize));
    TArray<FCrowdDemoOrcaNeighbor>& Neighbors = OutNeighbors[Index];
    for (int32 DY=-1; DY<=1; ++DY) for (int32 DX=-1; DX<=1; ++DX)
    {
      const TArray<int32>* Cell = Grid.Find(Base+FIntPoint(DX,DY));
      if (!Cell) continue;
      for (const int32 OtherIndex : *Cell)
      {
        if (OtherIndex==Index) continue;
        const float DistanceSq = (Sorted[OtherIndex].Position-Agent.Position).SizeSquared();
        if (DistanceSq <= MaxDistanceSq)
          Neighbors.Add({Sorted[OtherIndex].AgentId, FMath::FloorToInt(FMath::Sqrt(DistanceSq)/FMath::Max(0.1f,Settings.DistanceBucketCm)), DistanceSq});
      }
    }
    Neighbors.Sort([](const FCrowdDemoOrcaNeighbor& A,const FCrowdDemoOrcaNeighbor& B)
    {
      if (A.DistanceBucket!=B.DistanceBucket) return A.DistanceBucket<B.DistanceBucket;
      return A.AgentId<B.AgentId;
    });
    if (Neighbors.Num()>Settings.MaxNeighbors) Neighbors.SetNum(Settings.MaxNeighbors);
  }
}

void FCrowdDemoDeterministicOrcaKernel::Solve(
  const TConstArrayView<FCrowdDemoOrcaAgent> Agents,
  const FCrowdDemoOrcaSettings& Settings,
  const float FixedStepSeconds,
  TArray<FCrowdDemoOrcaResult>& OutResults,
  FCrowdDemoOrcaSummary& OutSummary)
{
  TArray<FCrowdDemoOrcaAgent> Sorted(Agents);
  Sorted.Sort([](const FCrowdDemoOrcaAgent& A,const FCrowdDemoOrcaAgent& B){return A.AgentId<B.AgentId;});
  TArray<TArray<FCrowdDemoOrcaNeighbor>> Neighbors;
  BuildNeighbors(Sorted,Settings,Neighbors);
  OutResults.Reset(Sorted.Num());
  OutSummary = FCrowdDemoOrcaSummary();
  OutSummary.VelocityHash=2166136261u;
  for (int32 Index=0;Index<Sorted.Num();++Index)
  {
    const FCrowdDemoOrcaAgent& Agent=Sorted[Index];
    TArray<FCrowdDemoOrcaConstraint> Constraints;
    BuildAgentConstraints(Agent, Sorted, Neighbors[Index], Settings, FixedStepSeconds, Constraints);
    FVector2f Velocity = FVector2f::ZeroVector;
    ECrowdDemoOrcaFeasibility Feasibility = Constraints.IsEmpty()
      ? ECrowdDemoOrcaFeasibility::NoConstraint
      : ECrowdDemoOrcaFeasibility::PreferredFeasible;
    const FVector2f Preferred = ClampSpeed(
      Quantize(Agent.PreferredVelocity, Settings.VelocityQuantumCmps), Agent.MaxSpeedCmps);
    bool bSolved = false;
    bool bNeighborhood3x3Recovered = false;
    bool bGeometryQuantizedRecovered = false;
    if (Constraints.IsEmpty()
      || (InsideSpeedCircle(Preferred, Agent.MaxSpeedCmps, Settings.ConstraintEpsilonCmps)
        && Satisfies(Preferred, Constraints, Settings.ConstraintEpsilonCmps)))
    {
      Velocity = Preferred;
      bSolved = true;
    }
    else
    {
      FVector2f Continuous;
      FCrowdDemoOrcaNumericalSummary NumericalSummary;
      const bool bContinuousSolved = SolveVelocityHalfPlanes(
        Agent.PreferredVelocity, Agent.MaxSpeedCmps, Constraints, Settings, Continuous, &NumericalSummary);
      OutSummary.ParallelBranchCount += NumericalSummary.ParallelBranchCount;
      OutSummary.NearParallelBranchCount += NumericalSummary.NearParallelBranchCount;
      OutSummary.RedundantParallelCount += NumericalSummary.RedundantParallelCount;
      OutSummary.StricterParallelCount += NumericalSummary.StricterParallelCount;
      OutSummary.TrueParallelContradictionCount += NumericalSummary.TrueParallelContradictionCount;
      OutSummary.NumericalToleranceAcceptanceCount += NumericalSummary.NumericalToleranceAcceptanceCount;
      if (bContinuousSolved)
      {
        const ECrowdDemoOrcaQuantizationResult Quantization = QuantizeAndValidateVelocityDetailed(
          Continuous, Agent.PreferredVelocity, Agent.MaxSpeedCmps, Constraints, Settings, Velocity);
        if (Quantization == ECrowdDemoOrcaQuantizationResult::CenterFeasible)
        {
          Feasibility = ECrowdDemoOrcaFeasibility::FormalLpFeasible;
          bSolved = true;
        }
        else if (Quantization == ECrowdDemoOrcaQuantizationResult::NeighborhoodRecovered)
        {
          Feasibility = ECrowdDemoOrcaFeasibility::FormalLpQuantizedRecovered;
          bNeighborhood3x3Recovered = true;
          bSolved = true;
        }
        else if (Quantization == ECrowdDemoOrcaQuantizationResult::GeometryRecovered)
        {
          Feasibility = ECrowdDemoOrcaFeasibility::FormalLpQuantizedGeometryRecovered;
          bGeometryQuantizedRecovered = true;
          bSolved = true;
        }
        else
        {
          Feasibility = ECrowdDemoOrcaFeasibility::ContinuousFeasibleQuantizedEmpty;
        }
      }
      else
      {
        Feasibility = Constraints.Num() == 1
          ? ECrowdDemoOrcaFeasibility::SingleConstraintOutsideSpeedCircle
          : ECrowdDemoOrcaFeasibility::MultiConstraintEmptyIntersection;
      }
    }
    ECrowdDemoOrcaFeasibility FailureReason = Feasibility;
    uint8 Fallback=0;
    if (!bSolved)
    {
      Fallback = SelectFallback(Agent, Constraints, Settings, Velocity, Feasibility);
      bSolved = Feasibility != ECrowdDemoOrcaFeasibility::StopViolation;
    }
    const bool bInfeasible = !bSolved;
    if (bInfeasible)
    {
      FailureReason = ECrowdDemoOrcaFeasibility::TrueNoFeasibleWitness;
      Feasibility = ECrowdDemoOrcaFeasibility::StopViolation;
    }
    FCrowdDemoOrcaResult& Result=OutResults.AddDefaulted_GetRef();
    Result.AgentId=Agent.AgentId;
    Result.Velocity=Velocity;
    Result.NeighborCount=Neighbors[Index].Num();
    Result.ConstraintCount=Constraints.Num();
    Result.FallbackStage=Fallback;
    Result.Feasibility=Feasibility;
    Result.FailureReason=FailureReason;
    Result.bAdjusted=!Velocity.Equals(Agent.PreferredVelocity,0.5f);
    Result.bInfeasible=bInfeasible;
    Result.bOutputSatisfiesConstraints = Satisfies(
      Velocity,Constraints,Settings.ConstraintEpsilonCmps)
      && InsideSpeedCircle(Velocity,Agent.MaxSpeedCmps,Settings.ConstraintEpsilonCmps);
    Result.bStopSatisfiesConstraints = Fallback == 4 && Result.bOutputSatisfiesConstraints;
    Result.bNeighborhood3x3Recovered = bNeighborhood3x3Recovered;
    Result.bGeometryQuantizedRecovered = bGeometryQuantizedRecovered;
    Result.Constraints = Constraints;
    ++OutSummary.ProcessedAgentCount;
    const int32 AdmissionIndex = FMath::Clamp(
      static_cast<int32>(Agent.AdmissionState), 0, 5);
    ++OutSummary.ProcessedByAdmissionState[AdmissionIndex];
    OutSummary.AdjustedAgentCount+=Result.bAdjusted?1:0;
    OutSummary.InfeasibleAgentCount+=bInfeasible?1:0;
    OutSummary.FallbackStopCount+=Fallback==4?1:0;
    OutSummary.StopSatisfiesConstraintCount += Result.bStopSatisfiesConstraints ? 1 : 0;
    OutSummary.StopViolatesConstraintCount += Fallback == 4 && !Result.bOutputSatisfiesConstraints ? 1 : 0;
    OutSummary.Neighborhood3x3RecoveredCount += bNeighborhood3x3Recovered ? 1 : 0;
    if (FailureReason == ECrowdDemoOrcaFeasibility::FormalLpMissedZeroRecovered
      || FailureReason == ECrowdDemoOrcaFeasibility::FormalLpMissedOracleRecovered)
      ++OutSummary.FormalLpMissedByAdmissionState[AdmissionIndex];
    if (FailureReason == ECrowdDemoOrcaFeasibility::ContinuousFeasibleQuantizedEmpty)
      ++OutSummary.QuantizedEmptyByAdmissionState[AdmissionIndex];
    if (bInfeasible) ++OutSummary.InfeasibleByAdmissionState[AdmissionIndex];
    if (bInfeasible)
    {
      switch (Agent.AdmissionState)
      {
      case ECrowdDemoPortalAdmissionState::Waiting: ++OutSummary.WaitingInfeasibleCount; break;
      case ECrowdDemoPortalAdmissionState::Approach: ++OutSummary.ApproachInfeasibleCount; break;
      case ECrowdDemoPortalAdmissionState::Reserved: ++OutSummary.ReservedInfeasibleCount; break;
      case ECrowdDemoPortalAdmissionState::Inside: ++OutSummary.InsideInfeasibleCount; break;
      default: break;
      }
    }
    if (Fallback == 4)
    {
      switch (Agent.AdmissionState)
      {
      case ECrowdDemoPortalAdmissionState::Waiting: ++OutSummary.WaitingFallbackStopCount; break;
      case ECrowdDemoPortalAdmissionState::Approach: ++OutSummary.ApproachFallbackStopCount; break;
      case ECrowdDemoPortalAdmissionState::Reserved: ++OutSummary.ReservedFallbackStopCount; break;
      case ECrowdDemoPortalAdmissionState::Inside: ++OutSummary.InsideFallbackStopCount; break;
      default: break;
      }
    }
    OutSummary.NeighborCountMax=FMath::Max(OutSummary.NeighborCountMax,Result.NeighborCount);
    OutSummary.ConstraintCountMax=FMath::Max(OutSummary.ConstraintCountMax,Result.ConstraintCount);
    OutSummary.NeighborCounts.Add(Result.NeighborCount);
    OutSummary.ConstraintCounts.Add(Result.ConstraintCount);
    for (const FCrowdDemoOrcaConstraint& Constraint : Constraints)
    {
      switch (Constraint.Kind)
      {
      case ECrowdDemoOrcaConstraintKind::CutoffCircle: ++OutSummary.CutoffCircleConstraintCount; break;
      case ECrowdDemoOrcaConstraintKind::LeftLeg: ++OutSummary.LeftLegConstraintCount; break;
      case ECrowdDemoOrcaConstraintKind::RightLeg: ++OutSummary.RightLegConstraintCount; break;
      case ECrowdDemoOrcaConstraintKind::Penetration: ++OutSummary.PenetrationConstraintCount; break;
      default: break;
      }
    }
    switch (FailureReason)
    {
    case ECrowdDemoOrcaFeasibility::NoConstraint: ++OutSummary.NoConstraintCount; break;
    case ECrowdDemoOrcaFeasibility::PreferredFeasible: ++OutSummary.PreferredFeasibleCount; break;
    case ECrowdDemoOrcaFeasibility::LpFeasible: ++OutSummary.LpFeasibleCount; break;
    case ECrowdDemoOrcaFeasibility::SingleConstraintOutsideSpeedCircle: ++OutSummary.SingleConstraintOutsideSpeedCircleCount; break;
    case ECrowdDemoOrcaFeasibility::MultiConstraintEmptyIntersection: ++OutSummary.MultiConstraintEmptyIntersectionCount; break;
    case ECrowdDemoOrcaFeasibility::QuantizationDestroyedFeasibility: ++OutSummary.QuantizationDestroyedFeasibilityCount; break;
    case ECrowdDemoOrcaFeasibility::FormalLpFeasible: ++OutSummary.FormalLpFeasibleCount; break;
    case ECrowdDemoOrcaFeasibility::FormalLpQuantizedRecovered: ++OutSummary.FormalLpQuantizedRecoveredCount; break;
    case ECrowdDemoOrcaFeasibility::FormalLpQuantizedGeometryRecovered:
      ++OutSummary.FormalLpQuantizedGeometryRecoveredCount; break;
    case ECrowdDemoOrcaFeasibility::FormalLpMissedOracleRecovered: ++OutSummary.FormalLpMissedOracleRecoveredCount; break;
    case ECrowdDemoOrcaFeasibility::FormalLpMissedZeroRecovered: ++OutSummary.FormalLpMissedZeroRecoveredCount; break;
    case ECrowdDemoOrcaFeasibility::ContinuousFeasibleQuantizedEmpty: ++OutSummary.ContinuousFeasibleQuantizedEmptyCount; break;
    case ECrowdDemoOrcaFeasibility::TrueNoFeasibleWitness: ++OutSummary.TrueNoFeasibleWitnessCount; break;
    default: break;
    }
    if (Feasibility == ECrowdDemoOrcaFeasibility::FallbackFlowFeasible) ++OutSummary.FallbackFlowFeasibleCount;
    else if (Feasibility == ECrowdDemoOrcaFeasibility::FallbackPortalFeasible) ++OutSummary.FallbackPortalFeasibleCount;
    else if (Feasibility == ECrowdDemoOrcaFeasibility::StopFeasible) ++OutSummary.StopFeasibleCount;
    else if (Feasibility == ECrowdDemoOrcaFeasibility::StopViolation) ++OutSummary.StopViolationCount;
    OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,Result.AgentId);
    OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,FMath::RoundToInt(Result.Velocity.X));
    OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,FMath::RoundToInt(Result.Velocity.Y));
    OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,Result.FallbackStage);
    OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,static_cast<int32>(Result.Feasibility));
    OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,static_cast<int32>(Result.FailureReason));
    OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,Result.bOutputSatisfiesConstraints?1:0);
    for (const FCrowdDemoOrcaConstraint& Constraint : Constraints)
    {
      OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,Constraint.OtherAgentId);
      OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,FMath::RoundToInt(Constraint.Point.X));
      OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,FMath::RoundToInt(Constraint.Point.Y));
      OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,FMath::RoundToInt(Constraint.Normal.X*32767.0f));
      OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,FMath::RoundToInt(Constraint.Normal.Y*32767.0f));
      OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,static_cast<int32>(Constraint.Kind));
      OutSummary.VelocityHash=OrcaHashInt(OutSummary.VelocityHash,Constraint.StableConstraintOrder);
    }
  }
  TMap<int32, const FCrowdDemoOrcaAgent*> AgentById;
  for (const auto& Agent : Sorted) AgentById.Add(Agent.AgentId, &Agent);
  TMap<uint64, FIntPoint> ResponsibilityAudit;
  TSet<uint64> CountedPriorityPairs;
  for (const FCrowdDemoOrcaResult& Result : OutResults)
  {
    const FCrowdDemoOrcaAgent* const* SelfPtr = AgentById.Find(Result.AgentId);
    if (!SelfPtr) continue;
    const FCrowdDemoOrcaPriorityKey SelfKey = MakePriorityKey(**SelfPtr);
    OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash, Result.AgentId);
    OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash, SelfKey.PortalPriority);
    OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
      static_cast<int32>(SelfKey.LocalPriority));
    for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
    {
      const FCrowdDemoOrcaAgent* const* OtherPtr = AgentById.Find(Constraint.OtherAgentId);
      if (!OtherPtr) continue;
      const FCrowdDemoOrcaPriorityKey OtherKey = MakePriorityKey(**OtherPtr);
      const int32 Comparison = ComparePriorityKeys(SelfKey, OtherKey);
      const int32 ResponsibilityQ100 = FMath::RoundToInt(Constraint.Responsibility * 100.0f);
      const int32 MinId = FMath::Min(Result.AgentId, Constraint.OtherAgentId);
      const int32 MaxId = FMath::Max(Result.AgentId, Constraint.OtherAgentId);
      const uint64 PairKey = (static_cast<uint64>(static_cast<uint32>(MinId)) << 32)
        | static_cast<uint32>(MaxId);
      if (!CountedPriorityPairs.Contains(PairKey))
      {
        CountedPriorityPairs.Add(PairKey);
        OutSummary.PriorityEqualPairCount += Comparison == 0 ? 1 : 0;
        OutSummary.PriorityAsymmetricPairCount += Comparison != 0 ? 1 : 0;
      }
      OutSummary.PriorityHighSide25Count += Comparison > 0 && ResponsibilityQ100 == 25 ? 1 : 0;
      OutSummary.PriorityLowSide75Count += Comparison < 0 && ResponsibilityQ100 == 75 ? 1 : 0;
      FIntPoint& Audit = ResponsibilityAudit.FindOrAdd(PairKey);
      Audit.X += ResponsibilityQ100;
      ++Audit.Y;
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash, Constraint.OtherAgentId);
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash, SelfKey.PortalPriority);
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
        static_cast<int32>(SelfKey.LocalPriority));
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash, OtherKey.PortalPriority);
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
        static_cast<int32>(OtherKey.LocalPriority));
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash, ResponsibilityQ100);
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
        static_cast<int32>(Constraint.Kind));
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
        FMath::RoundToInt(Constraint.Point.X));
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
        FMath::RoundToInt(Constraint.Point.Y));
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
        FMath::RoundToInt(Constraint.Normal.X * 32767.0f));
      OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
        FMath::RoundToInt(Constraint.Normal.Y * 32767.0f));
    }
    OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
      FMath::RoundToInt(Result.Velocity.X));
    OutSummary.PriorityHash = OrcaHashInt(OutSummary.PriorityHash,
      FMath::RoundToInt(Result.Velocity.Y));
  }
  for (const TPair<uint64, FIntPoint>& Pair : ResponsibilityAudit)
    OutSummary.PriorityResponsibilitySumViolationCount +=
      Pair.Value.Y == 2 && Pair.Value.X != 100 ? 1 : 0;
}
