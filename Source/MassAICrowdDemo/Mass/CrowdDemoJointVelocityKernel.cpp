#include "Mass/CrowdDemoJointVelocityKernel.h"

namespace
{
  uint32 Fold(uint32 Hash, const int32 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    return Hash * 16777619u;
  }

  uint64 PairKey(const int32 A, const int32 B)
  {
    const int32 MinId = FMath::Min(A, B);
    const int32 MaxId = FMath::Max(A, B);
    return (static_cast<uint64>(static_cast<uint32>(MinId)) << 32)
      | static_cast<uint32>(MaxId);
  }

  FVector2f Quantize(const FVector2f Value, const float Quantum)
  {
    const float Q = FMath::Max(0.001f, Quantum);
    return FVector2f(
      FMath::RoundToFloat(Value.X / Q) * Q,
      FMath::RoundToFloat(Value.Y / Q) * Q);
  }

  FVector2f ClampSpeed(const FVector2f Value, const float MaxSpeed)
  {
    const float Size = Value.Size();
    return Size > MaxSpeed && Size > UE_SMALL_NUMBER
      ? Value * (MaxSpeed / Size) : Value;
  }

  FVector2f StablePairNormal(
    const FCrowdDemoJointVelocityAgent& A,
    const FCrowdDemoJointVelocityAgent& B,
    int32& InOutDegenerateCount)
  {
    const FVector2f Delta = B.Position - A.Position;
    if (Delta.SizeSquared() > KINDA_SMALL_NUMBER)
    {
      const FVector2f Unit = Delta.GetSafeNormal();
      const FVector2f Q(
        FMath::RoundToFloat(Unit.X * 32767.0f) / 32767.0f,
        FMath::RoundToFloat(Unit.Y * 32767.0f) / 32767.0f);
      return Q.GetSafeNormal();
    }
    ++InOutDegenerateCount;
    const int32 MinId = FMath::Min(A.AgentId, B.AgentId);
    const int32 MaxId = FMath::Max(A.AgentId, B.AgentId);
    const uint32 H = static_cast<uint32>(MinId) * 73856093u
      ^ static_cast<uint32>(MaxId) * 19349663u;
    const float Angle = static_cast<float>(H % 4096u) * (2.0f * PI / 4096.0f);
    const FVector2f Base(FMath::Cos(Angle), FMath::Sin(Angle));
    return A.AgentId == MinId ? Base : -Base;
  }

  float Mobility(const FCrowdDemoJointVelocityAgent& Agent)
  {
    return Agent.bExternalVelocityFixed ? 0.0f
      : 1.0f / static_cast<float>(FMath::Max(1, Agent.MotionWeightQ8));
  }

  bool IsFinite(const FVector2f Value)
  {
    return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
  }

  FVector2f ClosestPointOnSegment(
    const FVector2f Point, const FVector2f Start, const FVector2f End)
  {
    const FVector2f Delta = End - Start;
    const float LengthSquared = Delta.SizeSquared();
    if (LengthSquared <= UE_SMALL_NUMBER) return Start;
    const float T = FMath::Clamp(FVector2f::DotProduct(Point - Start, Delta)
      / LengthSquared, 0.0f, 1.0f);
    return Start + Delta * T;
  }

  struct FTimeAlignedClearance
  {
    FVector2f RelativeAtClosest = FVector2f::ZeroVector;
    float ClosestTimeSeconds = 0.0f;
    float DistanceCm = 0.0f;
  };

  FTimeAlignedClearance EvaluateTimeAlignedClearance(
    const FVector2f TransitPosition,
    const FVector2f TransitVelocity,
    const FVector2f YieldingPosition,
    const FVector2f YieldingVelocity,
    const float HorizonSeconds)
  {
    FTimeAlignedClearance Result;
    const float Horizon = FMath::Max(0.0f, HorizonSeconds);
    const FVector2f RelativePosition = YieldingPosition - TransitPosition;
    const FVector2f RelativeVelocity = YieldingVelocity - TransitVelocity;
    const float RelativeSpeedSquared = RelativeVelocity.SizeSquared();
    Result.ClosestTimeSeconds = RelativeSpeedSquared > UE_SMALL_NUMBER
      ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
        / RelativeSpeedSquared, 0.0f, Horizon)
      : 0.0f;
    Result.RelativeAtClosest = RelativePosition
      + RelativeVelocity * Result.ClosestTimeSeconds;
    Result.DistanceCm = Result.RelativeAtClosest.Size();
    return Result;
  }

  FVector2f StableClearanceNormal(
    const int32 TransitAgentId, const int32 YieldingAgentId,
    const FVector2f CapsuleDirection)
  {
    FVector2f Perpendicular(-CapsuleDirection.Y, CapsuleDirection.X);
    if (Perpendicular.IsNearlyZero()) Perpendicular = FVector2f(0.0f, 1.0f);
    Perpendicular.Normalize();
    return YieldingAgentId < TransitAgentId ? -Perpendicular : Perpendicular;
  }
}

void FCrowdDemoJointVelocityKernel::EvaluateTransitAperture(
  const FCrowdDemoTransitCapacitySettings& Settings,
  const float CurrentPairDistanceCm,
  FCrowdDemoTransitApertureResult& OutResult)
{
  OutResult = {};
  const float Values[] = { Settings.PhysicalRadiusACm, Settings.PhysicalRadiusBCm,
    Settings.NominalTransitRadiusCm, Settings.HardSafetyGapCm,
    Settings.YieldBudgetACm, Settings.YieldBudgetBCm, CurrentPairDistanceCm };
  for (const float Value : Values)
    if (!FMath::IsFinite(Value) || Value < 0.0f) return;

  const double RadiusA = Settings.PhysicalRadiusACm;
  const double RadiusB = Settings.PhysicalRadiusBCm;
  const double HardGap = Settings.HardSafetyGapCm;
  const double YieldA = Settings.YieldBudgetACm;
  const double YieldB = Settings.YieldBudgetBCm;
  const double Required = RadiusA + RadiusB
    + 2.0 * Settings.NominalTransitRadiusCm + 2.0 * HardGap;
  const double Hard = RadiusA + RadiusB + HardGap;
  const double Baseline = FMath::Max(Hard, Required - YieldA - YieldB);
  const double AvailableDiameter = FMath::Max(0.0,
    static_cast<double>(CurrentPairDistanceCm) + YieldA + YieldB
      - RadiusA - RadiusB - 2.0 * HardGap);
  OutResult.HardPairDistanceCm = FMath::RoundToInt(Hard);
  OutResult.RequiredTransitApertureCm = FMath::RoundToInt(Required);
  OutResult.BaselinePairDistanceCm = FMath::RoundToInt(Baseline);
  OutResult.PreferredSpacingGapCm = FMath::Max(0,
    OutResult.BaselinePairDistanceCm - OutResult.HardPairDistanceCm);
  OutResult.AvailableTransitRadiusCm = FMath::FloorToInt(AvailableDiameter * 0.5);
  OutResult.ApertureDeficitCm = FMath::Max(0,
    OutResult.RequiredTransitApertureCm
      - FMath::RoundToInt(CurrentPairDistanceCm + YieldA + YieldB));
  OutResult.YieldBudgetRequiredCm = FMath::Max(0,
    OutResult.RequiredTransitApertureCm - FMath::RoundToInt(CurrentPairDistanceCm));
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, OutResult.HardPairDistanceCm);
  Hash = Fold(Hash, OutResult.RequiredTransitApertureCm);
  Hash = Fold(Hash, OutResult.BaselinePairDistanceCm);
  Hash = Fold(Hash, OutResult.PreferredSpacingGapCm);
  Hash = Fold(Hash, OutResult.AvailableTransitRadiusCm);
  Hash = Fold(Hash, OutResult.ApertureDeficitCm);
  Hash = Fold(Hash, OutResult.YieldBudgetRequiredCm);
  OutResult.StableHash = Hash;
  OutResult.bValid = true;
}

void FCrowdDemoJointVelocityKernel::EvaluateTransitCapacity(
  const FCrowdDemoTransitCapacitySettings& Settings,
  const TConstArrayView<FCrowdDemoTransitCapacityCandidate> InputPositions,
  const TConstArrayView<FCrowdDemoTransitCapacityCandidate> InputHoldings,
  FCrowdDemoTransitCapacityResult& OutResult)
{
  OutResult = {};
  EvaluateTransitAperture(Settings, 0.0f, OutResult.Aperture);
  if (!OutResult.Aperture.bValid || Settings.RequiredPositionCapacity < 0
    || Settings.RequiredHoldingCapacity < 0) return;
  const float Quantum = FMath::Max(0.001f, Settings.PositionQuantumCm);
  const float MinimumDistance = static_cast<float>(OutResult.Aperture.BaselinePairDistanceCm);
  const auto Select = [&](const TConstArrayView<FCrowdDemoTransitCapacityCandidate> Input,
    TArray<int32>& OutIds)
  {
    TArray<FCrowdDemoTransitCapacityCandidate> Sorted(Input);
    Sorted.Sort([](const auto& A, const auto& B) { return A.StableId < B.StableId; });
    TArray<FVector2f> Accepted;
    int32 PreviousId = INDEX_NONE;
    for (FCrowdDemoTransitCapacityCandidate Candidate : Sorted)
    {
      if (Candidate.StableId == INDEX_NONE || Candidate.StableId == PreviousId
        || !IsFinite(Candidate.Location)) return false;
      PreviousId = Candidate.StableId;
      Candidate.Location = Quantize(Candidate.Location, Quantum);
      bool bClear = true;
      for (const FVector2f Existing : Accepted)
        if ((Candidate.Location - Existing).SizeSquared()
          < FMath::Square(MinimumDistance)) { bClear = false; break; }
      if (bClear)
      {
        OutIds.Add(Candidate.StableId);
        Accepted.Add(Candidate.Location);
      }
    }
    return true;
  };
  if (!Select(InputPositions, OutResult.SelectedPositionIds)
    || !Select(InputHoldings, OutResult.SelectedHoldingIds)) return;
  OutResult.PositionCapacity = OutResult.SelectedPositionIds.Num();
  OutResult.HoldingCapacity = OutResult.SelectedHoldingIds.Num();
  OutResult.PositionCapacityDeficit = FMath::Max(0,
    Settings.RequiredPositionCapacity - OutResult.PositionCapacity);
  OutResult.HoldingCapacityDeficit = FMath::Max(0,
    Settings.RequiredHoldingCapacity - OutResult.HoldingCapacity);
  uint32 Hash = OutResult.Aperture.StableHash;
  Hash = Fold(Hash, OutResult.PositionCapacity);
  Hash = Fold(Hash, OutResult.HoldingCapacity);
  Hash = Fold(Hash, OutResult.PositionCapacityDeficit);
  Hash = Fold(Hash, OutResult.HoldingCapacityDeficit);
  for (const int32 Id : OutResult.SelectedPositionIds) Hash = Fold(Hash, Id);
  for (const int32 Id : OutResult.SelectedHoldingIds) Hash = Fold(Hash, Id);
  OutResult.CapacityHash = Hash;
  OutResult.bValid = true;
}

bool FCrowdDemoJointVelocityKernel::BuildTransitIntents(
  const TConstArrayView<FCrowdDemoJointVelocityAgent> InputAgents,
  const FCrowdDemoAdaptiveSpacingSettings& Settings,
  TArray<FCrowdDemoTransitIntent>& OutIntents,
  uint32& OutHash)
{
  OutIntents.Reset();
  OutHash = 2166136261u;
  TArray<FCrowdDemoJointVelocityAgent> Agents(InputAgents);
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  int32 PreviousId = INDEX_NONE;
  const float Horizon = FMath::Max(Settings.FixedStepSeconds,
    Settings.TransitPredictionHorizonSeconds);
  for (const FCrowdDemoJointVelocityAgent& Agent : Agents)
  {
    if (Agent.AgentId == INDEX_NONE || Agent.AgentId == PreviousId
      || !IsFinite(Agent.Position) || !IsFinite(Agent.PreferredVelocity)
      || !FMath::IsFinite(Agent.PhysicalRadiusCm) || Agent.PhysicalRadiusCm < 0.0f)
      return false;
    PreviousId = Agent.AgentId;
    if (!Agent.bTransitSeed) continue;
    FCrowdDemoTransitIntent& Intent = OutIntents.AddDefaulted_GetRef();
    Intent.AgentId = Agent.AgentId;
    Intent.Position = Quantize(Agent.Position, Settings.PositionQuantumCm);
    Intent.PreferredVelocity = Quantize(ClampSpeed(
      Agent.PreferredVelocity, Agent.MaxSpeedCmps), Settings.VelocityQuantumCmps);
    Intent.PredictionHorizonSeconds = Horizon;
    Intent.PredictedEnd = Quantize(Intent.Position
      + Intent.PreferredVelocity * Horizon, Settings.PositionQuantumCm);
    Intent.PhysicalRadiusCm = Agent.PhysicalRadiusCm;
    Intent.NominalClearanceRadiusCm = FMath::Max(0.0f,
      Settings.NominalTransitRadiusCm);
    Intent.PriorityQ8 = FMath::Max(0, Agent.MotionWeightQ8);
    uint32 Hash = 2166136261u;
    Hash = Fold(Hash, Intent.AgentId);
    Hash = Fold(Hash, FMath::RoundToInt(Intent.Position.X));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.Position.Y));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.PreferredVelocity.X));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.PreferredVelocity.Y));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.PredictedEnd.X));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.PredictedEnd.Y));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.PhysicalRadiusCm));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.NominalClearanceRadiusCm));
    Hash = Fold(Hash, FMath::RoundToInt(Intent.PredictionHorizonSeconds * 1000.0f));
    Hash = Fold(Hash, Intent.PriorityQ8);
    Intent.StableHash = Hash;
    Intent.bValid = true;
    OutHash = Fold(OutHash, Intent.AgentId);
    OutHash = Fold(OutHash, static_cast<int32>(Intent.StableHash));
  }
  return true;
}

bool FCrowdDemoJointVelocityKernel::ValidateComponentEnvironment(
  const TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
  const FCrowdDemoJointVelocityComponentResult& Result,
  const FCrowdDemoAdaptiveSpacingSettings& Settings,
  const FCrowdDemoJointVelocityEnvironment& Environment,
  FCrowdDemoJointVelocityComponentResult& OutValidatedResult)
{
  OutValidatedResult = Result;
  TMap<int32, const FCrowdDemoJointVelocityAgent*> AgentById;
  for (const FCrowdDemoJointVelocityAgent& Agent : Agents)
    AgentById.Add(Agent.AgentId, &Agent);
  const float Dt = FMath::Max(0.001f, Settings.FixedStepSeconds);
  for (const FCrowdDemoJointVelocityAgentResult& AgentResult : Result.Agents)
  {
    const FCrowdDemoJointVelocityAgent* const* Found = AgentById.Find(AgentResult.AgentId);
    if (!Found) return false;
    const FVector2f Start = (*Found)->Position;
    const auto ValidateVelocity = [&](const FVector2f Velocity,
      int32& OutFlowCount, int32& OutObstacleCount, int32& OutTargetCount)
    {
      if (!IsFinite(Velocity))
      {
        ++OutFlowCount;
        ++OutObstacleCount;
        ++OutTargetCount;
        return;
      }
      const FVector2f End = Start + Velocity * Dt;
      if (Environment.bValidateFlowAndObstacles)
      {
        const FCrowdDemoSharedFlowConstraintDiagnostic Diagnostic =
          FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
            Environment.FlowConfig, FVector(Start.X, Start.Y, 0.0f),
            FVector(End.X, End.Y, 0.0f), true);
        OutFlowCount += Diagnostic.FlowBoundsReprojectDeltaCm > 0.01f ? 1 : 0;
        OutObstacleCount += !Diagnostic.bDirectSegmentClear ? 1 : 0;
      }
      if (Environment.bValidateTargetExclusion
        && (End - Environment.TargetLocation).Size()
          < Environment.TargetExclusionRadiusCm - 0.01f)
        ++OutTargetCount;
    };
    ValidateVelocity(AgentResult.Velocity,
      OutValidatedResult.FlowBoundsViolationCount,
      OutValidatedResult.ObstacleViolationCount,
      OutValidatedResult.TargetViolationCount);
    ValidateVelocity(AgentResult.JointCandidateVelocity,
      OutValidatedResult.JointCandidateFlowBoundsViolationCount,
      OutValidatedResult.JointCandidateObstacleViolationCount,
      OutValidatedResult.JointCandidateTargetViolationCount);
    ValidateVelocity(AgentResult.BaselineVelocity,
      OutValidatedResult.BaselineFallbackFlowBoundsViolationCount,
      OutValidatedResult.BaselineFallbackObstacleViolationCount,
      OutValidatedResult.BaselineFallbackTargetViolationCount);
  }
  return OutValidatedResult.FlowBoundsViolationCount == 0
    && OutValidatedResult.ObstacleViolationCount == 0
    && OutValidatedResult.TargetViolationCount == 0;
}

float FCrowdDemoJointVelocityKernel::HardPairDistanceCm(
  const float PhysicalRadiusACm,
  const float PhysicalRadiusBCm,
  const float PairHardSafetyGapCm)
{
  return FMath::Max(0.0f, PhysicalRadiusACm)
    + FMath::Max(0.0f, PhysicalRadiusBCm)
    + FMath::Max(0.0f, PairHardSafetyGapCm);
}

float FCrowdDemoJointVelocityKernel::PreferredPairDistanceCm(
  const float HardPairDistance,
  const float PairPreferredSpacingGapCm,
  const int32 ContextScaleQ15)
{
  const int32 Scale = FMath::Clamp(ContextScaleQ15, 0, 32767);
  const double Soft = static_cast<double>(FMath::Max(0.0f, PairPreferredSpacingGapCm))
    * static_cast<double>(Scale) / 32767.0;
  return FMath::Max(0.0f, HardPairDistance) + static_cast<float>(Soft);
}

bool FCrowdDemoJointVelocityKernel::BuildPair(
  const FCrowdDemoJointVelocityAgent& InputA,
  const FCrowdDemoJointVelocityAgent& InputB,
  const FCrowdDemoAdaptiveSpacingSettings& SpacingSettings,
  const FCrowdDemoOrcaSettings& OrcaSettings,
  FCrowdDemoJointVelocityPair& OutPair)
{
  if (InputA.AgentId == INDEX_NONE || InputB.AgentId == INDEX_NONE
    || InputA.AgentId == InputB.AgentId) return false;
  const FCrowdDemoJointVelocityAgent& A = InputA.AgentId < InputB.AgentId ? InputA : InputB;
  const FCrowdDemoJointVelocityAgent& B = InputA.AgentId < InputB.AgentId ? InputB : InputA;
  FCrowdDemoOrcaAgent OrcaA;
  OrcaA.AgentId = A.AgentId; OrcaA.Position = A.Position; OrcaA.Velocity = A.Velocity;
  OrcaA.PreferredVelocity = A.PreferredVelocity; OrcaA.RadiusCm = A.PhysicalRadiusCm;
  OrcaA.MaxSpeedCmps = A.MaxSpeedCmps;
  FCrowdDemoOrcaAgent OrcaB;
  OrcaB.AgentId = B.AgentId; OrcaB.Position = B.Position; OrcaB.Velocity = B.Velocity;
  OrcaB.PreferredVelocity = B.PreferredVelocity; OrcaB.RadiusCm = B.PhysicalRadiusCm;
  OrcaB.MaxSpeedCmps = B.MaxSpeedCmps;
  OutPair = FCrowdDemoJointVelocityPair();
  OutPair.AgentAId = A.AgentId;
  OutPair.AgentBId = B.AgentId;
  OutPair.HardSafetyGapCm = FMath::Max(0.0f, SpacingSettings.HardSafetyGapCm);
  OutPair.PreferredSpacingGapCm = FMath::Max(0.0f, SpacingSettings.PreferredSpacingGapCm);
  OutPair.ContextScaleQ15 = FMath::Clamp(SpacingSettings.DefaultContextScaleQ15, 0, 32767);
  OutPair.RequestedOwnerMask = static_cast<uint8>(ECrowdDemoSpacingPairOwner::JointSolver);
  return FCrowdDemoDeterministicOrcaKernel::BuildCanonicalPairGeometry(
    OrcaA, OrcaB, OrcaSettings, SpacingSettings.FixedStepSeconds, OutPair.Canonical);
}

bool FCrowdDemoJointVelocityKernel::BuildLocalComponents(
  const TConstArrayView<FCrowdDemoJointVelocityAgent> InputAgents,
  TArray<FCrowdDemoJointVelocityPair>& InOutPairs,
  const FCrowdDemoAdaptiveSpacingSettings& Settings,
  TArray<FCrowdDemoJointVelocityComponent>& OutComponents,
  FCrowdDemoJointVelocitySummary& OutSummary)
{
  OutComponents.Reset();
  OutSummary = FCrowdDemoJointVelocitySummary();
  TArray<FCrowdDemoJointVelocityAgent> Agents(InputAgents);
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TMap<int32, int32> AgentIndex;
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
  {
    const FCrowdDemoJointVelocityAgent& Agent = Agents[Index];
    const float InputLimit = FMath::Max(1.0f, Settings.MaximumInputMagnitude);
    const bool bFiniteInput = IsFinite(Agent.Position) && IsFinite(Agent.Velocity)
      && IsFinite(Agent.PreferredVelocity) && IsFinite(Agent.BaselinePriorityOrcaVelocity)
      && IsFinite(Agent.AssignedPosition) && IsFinite(Agent.ExternalVelocity)
      && FMath::IsFinite(Agent.PhysicalRadiusCm) && FMath::IsFinite(Agent.MaxSpeedCmps)
      && FMath::Abs(Agent.Position.X) <= InputLimit
      && FMath::Abs(Agent.Position.Y) <= InputLimit
      && FMath::Abs(Agent.Velocity.X) <= InputLimit
      && FMath::Abs(Agent.Velocity.Y) <= InputLimit
      && FMath::Abs(Agent.PreferredVelocity.X) <= InputLimit
      && FMath::Abs(Agent.PreferredVelocity.Y) <= InputLimit
      && FMath::Abs(Agent.BaselinePriorityOrcaVelocity.X) <= InputLimit
      && FMath::Abs(Agent.BaselinePriorityOrcaVelocity.Y) <= InputLimit
      && FMath::Abs(Agent.AssignedPosition.X) <= InputLimit
      && FMath::Abs(Agent.AssignedPosition.Y) <= InputLimit
      && FMath::Abs(Agent.ExternalVelocity.X) <= InputLimit
      && FMath::Abs(Agent.ExternalVelocity.Y) <= InputLimit
      && Agent.PhysicalRadiusCm >= 0.0f && Agent.PhysicalRadiusCm <= InputLimit
      && Agent.MaxSpeedCmps >= 0.0f && Agent.MaxSpeedCmps <= InputLimit
      && Agent.MotionWeightQ8 >= 0 && Agent.MotionWeightQ8 <= Settings.MaximumWeightQ8
      && Agent.RecoveryWeightQ8 >= 0 && Agent.RecoveryWeightQ8 <= Settings.MaximumWeightQ8;
    if (Agent.AgentId == INDEX_NONE || AgentIndex.Contains(Agent.AgentId) || !bFiniteInput)
    {
      ++OutSummary.InvalidInputCount;
      return false;
    }
    AgentIndex.Add(Agent.AgentId, Index);
  }
  InOutPairs.Sort([](const auto& A, const auto& B)
  {
    const int32 AMin = FMath::Min(A.AgentAId, A.AgentBId);
    const int32 BMin = FMath::Min(B.AgentAId, B.AgentBId);
    if (AMin != BMin) return AMin < BMin;
    return FMath::Max(A.AgentAId, A.AgentBId) < FMath::Max(B.AgentAId, B.AgentBId);
  });
  TMap<uint64, int32> UniquePairIndex;
  TArray<int32> UniquePairIndexes;
  for (int32 PairIndex = 0; PairIndex < InOutPairs.Num(); ++PairIndex)
  {
    FCrowdDemoJointVelocityPair& Pair = InOutPairs[PairIndex];
    if (Pair.AgentAId > Pair.AgentBId) Swap(Pair.AgentAId, Pair.AgentBId);
    const uint64 Key = PairKey(Pair.AgentAId, Pair.AgentBId);
    const bool bDoubleOwner = (Pair.RequestedOwnerMask
      & static_cast<uint8>(ECrowdDemoSpacingPairOwner::JointSolver)) != 0
      && (Pair.RequestedOwnerMask
        & static_cast<uint8>(ECrowdDemoSpacingPairOwner::SoftSeparation)) != 0;
    if (bDoubleOwner || UniquePairIndex.Contains(Key))
    {
      ++OutSummary.SpacingPairDoubleOwnerCount;
      continue;
    }
    if (!AgentIndex.Contains(Pair.AgentAId) || !AgentIndex.Contains(Pair.AgentBId)
      || !Pair.Canonical.bValid || !IsFinite(Pair.Canonical.RelativeVelocityPoint)
      || !IsFinite(Pair.Canonical.Normal)
      || Pair.SpacingWeightQ8 < 0 || Pair.SpacingWeightQ8 > Settings.MaximumWeightQ8)
    {
      ++OutSummary.InvalidInputCount;
      continue;
    }
    UniquePairIndex.Add(Key, PairIndex);
    UniquePairIndexes.Add(PairIndex);
  }
  TArray<FCrowdDemoTransitIntent> TransitIntents;
  uint32 TransitIntentHash = 2166136261u;
  if (!BuildTransitIntents(Agents, Settings, TransitIntents, TransitIntentHash))
  {
    ++OutSummary.InvalidInputCount;
    return false;
  }
  struct FWorkingComponent
  {
    TSet<int32> AgentIds;
    TSet<int32> DirectIds;
    TSet<int32> ClosureIds;
  };
  TArray<FWorkingComponent> WorkingComponents;
  const float Dt = FMath::Max(0.001f, Settings.FixedStepSeconds);
  const float RelevanceMargin = FMath::Max(0.0f, Settings.YieldBudgetCm);
  const auto IsHardSafetyRelevant = [&](const FCrowdDemoJointVelocityPair& Pair)
  {
    const int32* AIndex = AgentIndex.Find(Pair.AgentAId);
    const int32* BIndex = AgentIndex.Find(Pair.AgentBId);
    if (!AIndex || !BIndex) return false;
    const FCrowdDemoJointVelocityAgent& A = Agents[*AIndex];
    const FCrowdDemoJointVelocityAgent& B = Agents[*BIndex];
    const float HardDistance = HardPairDistanceCm(
      A.PhysicalRadiusCm, B.PhysicalRadiusCm, Pair.HardSafetyGapCm);
    const float ReachableClosingDistance =
      (A.MaxSpeedCmps + B.MaxSpeedCmps) * Dt;
    return (A.Position - B.Position).Size()
      <= HardDistance + ReachableClosingDistance + Settings.PositionQuantumCm;
  };
  for (const FCrowdDemoTransitIntent& Intent : TransitIntents)
  {
    FWorkingComponent Working;
    Working.AgentIds.Add(Intent.AgentId);
    Working.DirectIds.Add(Intent.AgentId);
    for (const FCrowdDemoJointVelocityAgent& Agent : Agents)
    {
      if (Agent.AgentId == Intent.AgentId) continue;
      const FTimeAlignedClearance BaselineClearance = EvaluateTimeAlignedClearance(
        Intent.Position, Intent.PreferredVelocity, Agent.Position,
        Agent.BaselinePriorityOrcaVelocity, Intent.PredictionHorizonSeconds);
      const float Required = Intent.NominalClearanceRadiusCm
        + Agent.PhysicalRadiusCm + Settings.HardSafetyGapCm;
      if (BaselineClearance.DistanceCm <= Required + RelevanceMargin)
      {
        Working.AgentIds.Add(Agent.AgentId);
        Working.DirectIds.Add(Agent.AgentId);
      }
    }
    bool bExpanded = true;
    while (bExpanded)
    {
      bExpanded = false;
      for (const int32 PairIndex : UniquePairIndexes)
      {
        const FCrowdDemoJointVelocityPair& Pair = InOutPairs[PairIndex];
        const bool bHasA = Working.AgentIds.Contains(Pair.AgentAId);
        const bool bHasB = Working.AgentIds.Contains(Pair.AgentBId);
        if (bHasA == bHasB || !IsHardSafetyRelevant(Pair)) continue;
        const int32 AddedId = bHasA ? Pair.AgentBId : Pair.AgentAId;
        Working.AgentIds.Add(AddedId);
        if (!Working.DirectIds.Contains(AddedId)) Working.ClosureIds.Add(AddedId);
        bExpanded = true;
      }
    }
    WorkingComponents.Add(MoveTemp(Working));
  }
  bool bMerged = true;
  while (bMerged)
  {
    bMerged = false;
    for (int32 AIndex = 0; AIndex < WorkingComponents.Num() && !bMerged; ++AIndex)
      for (int32 BIndex = AIndex + 1; BIndex < WorkingComponents.Num(); ++BIndex)
      {
        bool bIntersects = false;
        for (const FCrowdDemoJointVelocityAgent& Agent : Agents)
          if (WorkingComponents[AIndex].AgentIds.Contains(Agent.AgentId)
            && WorkingComponents[BIndex].AgentIds.Contains(Agent.AgentId))
          { bIntersects = true; break; }
        if (!bIntersects) continue;
        for (const FCrowdDemoJointVelocityAgent& Agent : Agents)
        {
          if (WorkingComponents[BIndex].AgentIds.Contains(Agent.AgentId))
            WorkingComponents[AIndex].AgentIds.Add(Agent.AgentId);
          if (WorkingComponents[BIndex].DirectIds.Contains(Agent.AgentId))
            WorkingComponents[AIndex].DirectIds.Add(Agent.AgentId);
          if (WorkingComponents[BIndex].ClosureIds.Contains(Agent.AgentId))
            WorkingComponents[AIndex].ClosureIds.Add(Agent.AgentId);
        }
        WorkingComponents.RemoveAt(BIndex);
        bMerged = true;
        break;
      }
  }
  for (const FWorkingComponent& Working : WorkingComponents)
  {
    FCrowdDemoJointVelocityComponent Component;
    for (const FCrowdDemoJointVelocityAgent& Agent : Agents)
    {
      if (!Working.AgentIds.Contains(Agent.AgentId)) continue;
      Component.AgentIds.Add(Agent.AgentId);
      if (Working.DirectIds.Contains(Agent.AgentId))
        Component.DirectTransitRelevantAgentIds.Add(Agent.AgentId);
      else if (Working.ClosureIds.Contains(Agent.AgentId))
        Component.HardSafetyClosureAgentIds.Add(Agent.AgentId);
    }
    if (Component.AgentIds.IsEmpty()) continue;
    Component.ComponentId = Component.AgentIds[0];
    for (const int32 PairIndex : UniquePairIndexes)
    {
      const FCrowdDemoJointVelocityPair& Pair = InOutPairs[PairIndex];
      if (Working.AgentIds.Contains(Pair.AgentAId)
        && Working.AgentIds.Contains(Pair.AgentBId)) Component.PairIndexes.Add(PairIndex);
    }
    Component.PairIndexes.Sort([&](const int32 A, const int32 B)
    {
      const auto& PA = InOutPairs[A]; const auto& PB = InOutPairs[B];
      return PA.AgentAId != PB.AgentAId ? PA.AgentAId < PB.AgentAId
        : PA.AgentBId < PB.AgentBId;
    });
    Component.bOversize = Component.AgentIds.Num()
      > FMath::Max(1, Settings.MaximumComponentAgents);
    uint32 Hash = 2166136261u;
    for (const int32 Id : Component.AgentIds) Hash = Fold(Hash, Id);
    Hash = Fold(Hash, -1);
    for (const int32 Id : Component.DirectTransitRelevantAgentIds) Hash = Fold(Hash, Id);
    Hash = Fold(Hash, -2);
    for (const int32 Id : Component.HardSafetyClosureAgentIds) Hash = Fold(Hash, Id);
    for (const int32 Index : Component.PairIndexes)
    {
      InOutPairs[Index].Owner = ECrowdDemoSpacingPairOwner::JointSolver;
      Hash = Fold(Hash, InOutPairs[Index].AgentAId);
      Hash = Fold(Hash, InOutPairs[Index].AgentBId);
    }
    Component.StableHash = Hash;
    OutComponents.Add(MoveTemp(Component));
  }
  OutComponents.Sort([](const auto& A, const auto& B) { return A.ComponentId < B.ComponentId; });
  TSet<int32> JointPairIndexes;
  for (const auto& Component : OutComponents)
    for (const int32 PairIndex : Component.PairIndexes) JointPairIndexes.Add(PairIndex);
  uint32 OwnerHash = 2166136261u;
  for (int32 PairIndex = 0; PairIndex < InOutPairs.Num(); ++PairIndex)
  {
    FCrowdDemoJointVelocityPair& Pair = InOutPairs[PairIndex];
    if (!JointPairIndexes.Contains(PairIndex) && Pair.Owner == ECrowdDemoSpacingPairOwner::None)
      Pair.Owner = ECrowdDemoSpacingPairOwner::SoftSeparation;
    OwnerHash = Fold(OwnerHash, Pair.AgentAId);
    OwnerHash = Fold(OwnerHash, Pair.AgentBId);
    OwnerHash = Fold(OwnerHash, static_cast<int32>(Pair.Owner));
  }
  OutSummary.PairOwnerHash = OwnerHash;
  OutSummary.ComponentCount = OutComponents.Num();
  for (const auto& Component : OutComponents)
  {
    OutSummary.MaximumComponentSize = FMath::Max(
      OutSummary.MaximumComponentSize, Component.AgentIds.Num());
    OutSummary.TransitDirectRelevantAgentCount +=
      Component.DirectTransitRelevantAgentIds.Num();
    OutSummary.HardSafetyClosureAgentCount +=
      Component.HardSafetyClosureAgentIds.Num();
    OutSummary.OversizeCount += Component.bOversize ? 1 : 0;
  }
  return OutSummary.InvalidInputCount == 0
    && OutSummary.SpacingPairDoubleOwnerCount == 0;
}

void FCrowdDemoJointVelocityKernel::Solve(
  const TConstArrayView<FCrowdDemoJointVelocityAgent> InputAgents,
  const TConstArrayView<FCrowdDemoJointVelocityPair> Pairs,
  const TConstArrayView<FCrowdDemoJointVelocityComponent> Components,
  const FCrowdDemoAdaptiveSpacingSettings& Settings,
  TArray<FCrowdDemoJointVelocityComponentResult>& OutResults,
  FCrowdDemoJointVelocitySummary& InOutSummary)
{
  OutResults.Reset();
  TArray<FCrowdDemoJointVelocityAgent> Agents(InputAgents);
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (FCrowdDemoJointVelocityAgent& Agent : Agents)
  {
    Agent.Position = Quantize(Agent.Position, Settings.PositionQuantumCm);
    Agent.AssignedPosition = Quantize(Agent.AssignedPosition, Settings.PositionQuantumCm);
  }
  TMap<int32, int32> GlobalIndex;
  for (int32 Index = 0; Index < Agents.Num(); ++Index) GlobalIndex.Add(Agents[Index].AgentId, Index);
  const float Dt = FMath::Max(0.001f, Settings.FixedStepSeconds);
  const float Relaxation = static_cast<float>(FMath::Clamp(Settings.RelaxationQ15, 1, 32767)) / 32767.0f;
  uint32 SummaryHash = 2166136261u;
  for (const FCrowdDemoJointVelocityComponent& Component : Components)
  {
    FCrowdDemoJointVelocityComponentResult Result;
    Result.ComponentId = Component.ComponentId;
    Result.PairCount = Component.PairIndexes.Num();
    TArray<const FCrowdDemoJointVelocityAgent*> LocalAgents;
    TMap<int32, int32> LocalIndex;
    bool bInvalid = false;
    for (const int32 AgentId : Component.AgentIds)
    {
      const int32* Index = GlobalIndex.Find(AgentId);
      if (!Index) { bInvalid = true; break; }
      LocalIndex.Add(AgentId, LocalAgents.Num());
      LocalAgents.Add(&Agents[*Index]);
    }
    if (bInvalid)
    {
      Result.Status = ECrowdDemoJointVelocityStatus::InvalidInput;
      ++InOutSummary.InvalidInputCount;
      OutResults.Add(MoveTemp(Result));
      continue;
    }
    if (Component.bOversize)
    {
      Result.Status = ECrowdDemoJointVelocityStatus::OversizeFallback;
      for (const auto* Agent : LocalAgents)
      {
        FCrowdDemoJointVelocityAgentResult& AgentResult = Result.Agents.AddDefaulted_GetRef();
        AgentResult.AgentId = Agent->AgentId;
        AgentResult.Velocity = Agent->BaselinePriorityOrcaVelocity;
        AgentResult.bUsedJointVelocity = false;
      }
      uint32 Hash = 2166136261u;
      Hash = Fold(Hash, Result.ComponentId);
      Hash = Fold(Hash, static_cast<int32>(Result.Status));
      for (const FCrowdDemoJointVelocityAgentResult& AgentResult : Result.Agents)
      {
        Hash = Fold(Hash, AgentResult.AgentId);
        Hash = Fold(Hash, FMath::RoundToInt(AgentResult.Velocity.X));
        Hash = Fold(Hash, FMath::RoundToInt(AgentResult.Velocity.Y));
      }
      Result.StableHash = Hash;
      SummaryHash = Fold(SummaryHash, Result.ComponentId);
      SummaryHash = Fold(SummaryHash, static_cast<int32>(Result.Status));
      SummaryHash = Fold(SummaryHash, Result.StableHash);
      OutResults.Add(MoveTemp(Result));
      continue;
    }
    TArray<FVector2f> Desired, Velocities;
    Desired.SetNum(LocalAgents.Num()); Velocities.SetNum(LocalAgents.Num());
    for (int32 Index = 0; Index < LocalAgents.Num(); ++Index)
    {
      const auto& Agent = *LocalAgents[Index];
      if (Agent.bExternalVelocityFixed)
        Desired[Index] = Velocities[Index] = ClampSpeed(Agent.ExternalVelocity, Agent.MaxSpeedCmps);
      else
      {
        FVector2f Preferred = ClampSpeed(Agent.PreferredVelocity, Agent.MaxSpeedCmps);
        if (Agent.bHasAssignedPosition && Agent.RecoveryWeightQ8 > 0)
        {
          const FVector2f Recovery = ClampSpeed(
            (Agent.AssignedPosition - Agent.Position) / Dt, Agent.MaxSpeedCmps);
          const float Motion = static_cast<float>(FMath::Max(1, Agent.MotionWeightQ8));
          const float RecoveryWeight = static_cast<float>(Agent.RecoveryWeightQ8);
          Preferred = (Preferred * Motion + Recovery * RecoveryWeight)
            / (Motion + RecoveryWeight);
        }
        Desired[Index] = Velocities[Index] = Preferred;
      }
    }
    TArray<FCrowdDemoJointVelocityAgent> LocalAgentValues;
    for (const FCrowdDemoJointVelocityAgent* Agent : LocalAgents)
      LocalAgentValues.Add(*Agent);
    TArray<FCrowdDemoTransitIntent> TransitIntents;
    uint32 TransitIntentHash = 2166136261u;
    if (!BuildTransitIntents(LocalAgentValues, Settings, TransitIntents, TransitIntentHash))
    {
      Result.Status = ECrowdDemoJointVelocityStatus::InvalidInput;
      ++InOutSummary.InvalidInputCount;
      OutResults.Add(MoveTemp(Result));
      continue;
    }
    Result.TransitClearancePairCount = TransitIntents.Num()
      * FMath::Max(0, LocalAgents.Num() - 1);
    InOutSummary.TransitIntentCount += TransitIntents.Num();
    InOutSummary.TransitClearancePairCount += Result.TransitClearancePairCount;
    auto MaximumTimeAlignedClearanceDeficit =
      [&](const TArray<FVector2f>& CandidateVelocities)
      {
        float MaximumDeficitCm = 0.0f;
        for (const FCrowdDemoTransitIntent& Intent : TransitIntents)
        {
          const int32 SeedIndex = LocalIndex.FindChecked(Intent.AgentId);
          for (int32 YieldIndex = 0; YieldIndex < LocalAgents.Num(); ++YieldIndex)
          {
            if (YieldIndex == SeedIndex || LocalAgents[YieldIndex]->bTransitSeed) continue;
            const FTimeAlignedClearance Clearance = EvaluateTimeAlignedClearance(
              Intent.Position, CandidateVelocities[SeedIndex],
              LocalAgents[YieldIndex]->Position, CandidateVelocities[YieldIndex],
              Intent.PredictionHorizonSeconds);
            const float Required = Intent.NominalClearanceRadiusCm
              + LocalAgents[YieldIndex]->PhysicalRadiusCm + Settings.HardSafetyGapCm;
            MaximumDeficitCm = FMath::Max(MaximumDeficitCm,
              Required - Clearance.DistanceCm);
          }
        }
        return FMath::Max(0.0f, MaximumDeficitCm);
      };
    TSet<int32> TransitYieldedAgentIds;
    ECrowdDemoJointVelocityStatus Failure = ECrowdDemoJointVelocityStatus::Solved;
    bool bObviousHardInfeasible = false;
    for (const int32 PairIndex : Component.PairIndexes)
    {
      if (!Pairs.IsValidIndex(PairIndex)) { Failure = ECrowdDemoJointVelocityStatus::InvalidInput; break; }
      const auto& Pair = Pairs[PairIndex];
      const int32* IA = LocalIndex.Find(Pair.AgentAId);
      const int32* IB = LocalIndex.Find(Pair.AgentBId);
      if (!IA || !IB || !Pair.Canonical.bValid) { Failure = ECrowdDemoJointVelocityStatus::InvalidInput; break; }
      const auto& A = *LocalAgents[*IA]; const auto& B = *LocalAgents[*IB];
      const float MaximumRelativeSpeed = A.MaxSpeedCmps + B.MaxSpeedCmps;
      if (FVector2f::DotProduct(Pair.Canonical.RelativeVelocityPoint,
          Pair.Canonical.Normal) > MaximumRelativeSpeed + Settings.ConstraintEpsilonCmps)
        bObviousHardInfeasible = true;
      int32 Dummy = 0;
      const FVector2f SeparationNormal = StablePairNormal(A, B, Dummy);
      const float CurrentProjection = FVector2f::DotProduct(B.Position - A.Position, SeparationNormal);
      const float HardDistance = HardPairDistanceCm(
        A.PhysicalRadiusCm, B.PhysicalRadiusCm, Pair.HardSafetyGapCm);
      if ((HardDistance - CurrentProjection) / Dt
        > MaximumRelativeSpeed + Settings.ConstraintEpsilonCmps) bObviousHardInfeasible = true;
    }
    if (Failure == ECrowdDemoJointVelocityStatus::Solved)
    {
      const int32 ObjectiveIterationCount = FMath::Max(1, Settings.SolverIterations);
      const int32 FeasibilityPolishCount = FMath::Max(0, Settings.FeasibilityPolishIterations);
      for (int32 Iteration = 0;
        Iteration < ObjectiveIterationCount + FeasibilityPolishCount; ++Iteration)
      {
        const bool bObjectiveIteration = Iteration < ObjectiveIterationCount;
        if (bObjectiveIteration)
        {
          for (int32 Index = 0; Index < LocalAgents.Num(); ++Index)
            if (!LocalAgents[Index]->bExternalVelocityFixed)
              Velocities[Index] += (Desired[Index] - Velocities[Index]) * Relaxation;
        }
        if (Settings.TransitClearanceWeightQ8 > 0)
        for (const FCrowdDemoTransitIntent& Intent : TransitIntents)
        {
          const int32 SeedIndex = LocalIndex.FindChecked(Intent.AgentId);
          const FVector2f CapsuleDirection = Intent.PredictedEnd - Intent.Position;
          for (int32 YieldIndex = 0; YieldIndex < LocalAgents.Num(); ++YieldIndex)
          {
            if (YieldIndex == SeedIndex) continue;
            const FCrowdDemoJointVelocityAgent& Seed = *LocalAgents[SeedIndex];
            const FCrowdDemoJointVelocityAgent& Yielding = *LocalAgents[YieldIndex];
            if (Yielding.bTransitSeed) continue;
            const FTimeAlignedClearance Clearance = EvaluateTimeAlignedClearance(
              Intent.Position, Velocities[SeedIndex], Yielding.Position,
              Velocities[YieldIndex], Intent.PredictionHorizonSeconds);
            const float Required = Intent.NominalClearanceRadiusCm
              + Yielding.PhysicalRadiusCm + Settings.HardSafetyGapCm;
            const float Deficit = Required - Clearance.DistanceCm;
            if (Deficit <= Settings.ConstraintEpsilonCmps) continue;
            const FVector2f Normal = Clearance.DistanceCm > UE_SMALL_NUMBER
              ? Clearance.RelativeAtClosest / Clearance.DistanceCm : StableClearanceNormal(
                Intent.AgentId, Yielding.AgentId, CapsuleDirection);
            const float SeedMobility = Mobility(Seed);
            const float YieldingMobility = Mobility(Yielding);
            const float MobilitySum = SeedMobility + YieldingMobility;
            if (MobilitySum <= UE_SMALL_NUMBER)
            {
              bObviousHardInfeasible = true;
              continue;
            }
            // Clearance is a hard relative-velocity constraint. MotionWeight only
            // allocates responsibility; it must not scale away the required correction.
            const float NeededRelativeSpeed = Deficit
              / FMath::Max(Dt, Clearance.ClosestTimeSeconds)
              + 2.0f * Settings.ConstraintEpsilonCmps;
            Velocities[SeedIndex] = Seed.bExternalVelocityFixed
              ? ClampSpeed(Seed.ExternalVelocity, Seed.MaxSpeedCmps)
              : ClampSpeed(Velocities[SeedIndex] - Normal
                * (NeededRelativeSpeed * SeedMobility / MobilitySum), Seed.MaxSpeedCmps);
            Velocities[YieldIndex] = Yielding.bExternalVelocityFixed
              ? ClampSpeed(Yielding.ExternalVelocity, Yielding.MaxSpeedCmps)
              : ClampSpeed(Velocities[YieldIndex] + Normal
                * (NeededRelativeSpeed * YieldingMobility / MobilitySum),
                Yielding.MaxSpeedCmps * static_cast<float>(FMath::Clamp(
                  Settings.TransitClearanceSpeedLimitQ15, 0, 32767)) / 32767.0f);
            TransitYieldedAgentIds.Add(Yielding.AgentId);
          }
        }
        for (const int32 PairIndex : Component.PairIndexes)
        {
          const auto& Pair = Pairs[PairIndex];
          const int32 IA = LocalIndex.FindChecked(Pair.AgentAId);
          const int32 IB = LocalIndex.FindChecked(Pair.AgentBId);
          const auto& A = *LocalAgents[IA]; const auto& B = *LocalAgents[IB];
          const FVector2f Normal = StablePairNormal(A, B, InOutSummary.DegenerateNormalCount);
          const float CurrentProjection = FVector2f::DotProduct(B.Position - A.Position, Normal);
          const float HardDistance = HardPairDistanceCm(
            A.PhysicalRadiusCm, B.PhysicalRadiusCm, Pair.HardSafetyGapCm);
          const float PreferredDistance = PreferredPairDistanceCm(
            HardDistance, Pair.PreferredSpacingGapCm, Pair.ContextScaleQ15);
          const float PredictedProjection = CurrentProjection
            + FVector2f::DotProduct(Velocities[IB] - Velocities[IA], Normal) * Dt;
          const float SpacingDeficit = PreferredDistance - PredictedProjection;
          if (bObjectiveIteration && SpacingDeficit > 0.0f && Pair.SpacingWeightQ8 > 0)
          {
            const float MA = Mobility(A), MB = Mobility(B), Sum = MA + MB;
            if (Sum > UE_SMALL_NUMBER)
            {
              const float SpacingWeight = FMath::Clamp(
                static_cast<float>(Pair.SpacingWeightQ8) / 256.0f, 0.0f, 4.0f);
              const float CorrectionSpeed = SpacingDeficit / Dt
                * FMath::Min(1.0f, Relaxation * SpacingWeight);
              Velocities[IA] -= Normal * (CorrectionSpeed * MA / Sum);
              Velocities[IB] += Normal * (CorrectionSpeed * MB / Sum);
            }
          }
          const float HardRequired = (HardDistance - CurrentProjection) / Dt;
          float HardScalar = FVector2f::DotProduct(Velocities[IB] - Velocities[IA], Normal)
            - HardRequired;
          if (HardScalar < -Settings.ConstraintEpsilonCmps)
          {
            const float MA = Mobility(A), MB = Mobility(B), Sum = MA + MB;
            if (Sum <= UE_SMALL_NUMBER) { bObviousHardInfeasible = true; continue; }
            const float Needed = -HardScalar
              + 2.0f * Settings.ConstraintEpsilonCmps;
            Velocities[IA] -= Normal * (Needed * MA / Sum);
            Velocities[IB] += Normal * (Needed * MB / Sum);
          }
          const FVector2f Relative = Velocities[IA] - Velocities[IB];
          const float CanonicalScalar = FVector2f::DotProduct(
            Relative - Pair.Canonical.RelativeVelocityPoint, Pair.Canonical.Normal);
          if (CanonicalScalar < -Settings.ConstraintEpsilonCmps)
          {
            const float MA = Mobility(A), MB = Mobility(B), Sum = MA + MB;
            if (Sum <= UE_SMALL_NUMBER) { bObviousHardInfeasible = true; continue; }
            const float Needed = -CanonicalScalar
              + 2.0f * Settings.ConstraintEpsilonCmps;
            Velocities[IA] += Pair.Canonical.Normal * (Needed * MA / Sum);
            Velocities[IB] -= Pair.Canonical.Normal * (Needed * MB / Sum);
          }
          Velocities[IA] = A.bExternalVelocityFixed
            ? ClampSpeed(A.ExternalVelocity, A.MaxSpeedCmps)
            : ClampSpeed(Velocities[IA], A.MaxSpeedCmps);
          Velocities[IB] = B.bExternalVelocityFixed
            ? ClampSpeed(B.ExternalVelocity, B.MaxSpeedCmps)
            : ClampSpeed(Velocities[IB], B.MaxSpeedCmps);
        }
      }
    }
    bool bNumericallyValid = true;
    for (const FVector2f Velocity : Velocities)
      bNumericallyValid &= IsFinite(Velocity);
    bool bContinuousValid = Failure == ECrowdDemoJointVelocityStatus::Solved
      && bNumericallyValid;
    for (const int32 PairIndex : Component.PairIndexes)
    {
      const auto& Pair = Pairs[PairIndex];
      const int32 IA = LocalIndex.FindChecked(Pair.AgentAId);
      const int32 IB = LocalIndex.FindChecked(Pair.AgentBId);
      const auto& A = *LocalAgents[IA]; const auto& B = *LocalAgents[IB];
      const FVector2f Normal = StablePairNormal(A, B, InOutSummary.DegenerateNormalCount);
      const float HardDistance = HardPairDistanceCm(
        A.PhysicalRadiusCm, B.PhysicalRadiusCm, Pair.HardSafetyGapCm);
      const float NextProjection = FVector2f::DotProduct(
        (B.Position + Velocities[IB] * Dt) - (A.Position + Velocities[IA] * Dt), Normal);
      bContinuousValid &= NextProjection + Settings.ConstraintEpsilonCmps >= HardDistance;
      bContinuousValid &= FVector2f::DotProduct(
        (Velocities[IA] - Velocities[IB]) - Pair.Canonical.RelativeVelocityPoint,
        Pair.Canonical.Normal) >= -Settings.ConstraintEpsilonCmps;
    }
    if (!bContinuousValid)
    {
      Failure = !bNumericallyValid ? ECrowdDemoJointVelocityStatus::NumericalFailure
        : (bObviousHardInfeasible ? ECrowdDemoJointVelocityStatus::HardInfeasible
          : (Failure == ECrowdDemoJointVelocityStatus::InvalidInput
            ? Failure : ECrowdDemoJointVelocityStatus::IterationLimit));
    }
    else if (Failure == ECrowdDemoJointVelocityStatus::Solved
      && Settings.TransitClearanceWeightQ8 > 0
      && MaximumTimeAlignedClearanceDeficit(Velocities) > Settings.PositionQuantumCm)
    {
      Failure = ECrowdDemoJointVelocityStatus::ClearanceNotAchieved;
    }
    TArray<FVector2f> Quantized = Velocities;
    if (Failure == ECrowdDemoJointVelocityStatus::Solved)
    {
      for (int32 Index = 0; Index < LocalAgents.Num(); ++Index)
        Quantized[Index] = Quantize(Velocities[Index], Settings.VelocityQuantumCmps);
      auto IsPairValid = [&](const TArray<FVector2f>& Candidate, const int32 PairIndex)
      {
        const auto& Pair = Pairs[PairIndex];
        const int32 IA = LocalIndex.FindChecked(Pair.AgentAId);
        const int32 IB = LocalIndex.FindChecked(Pair.AgentBId);
        const auto& A = *LocalAgents[IA]; const auto& B = *LocalAgents[IB];
        int32 IgnoredDegenerateCount = 0;
        const FVector2f Normal = StablePairNormal(A, B, IgnoredDegenerateCount);
        const float HardDistance = HardPairDistanceCm(
          A.PhysicalRadiusCm, B.PhysicalRadiusCm, Pair.HardSafetyGapCm);
        const float NextProjection = FVector2f::DotProduct(
          (B.Position + Candidate[IB] * Dt) - (A.Position + Candidate[IA] * Dt), Normal);
        return NextProjection + Settings.ConstraintEpsilonCmps >= HardDistance
          && FVector2f::DotProduct(
            (Candidate[IA] - Candidate[IB]) - Pair.Canonical.RelativeVelocityPoint,
            Pair.Canonical.Normal) >= -Settings.ConstraintEpsilonCmps;
      };
      auto AreAllQuantizedVelocitiesValid = [&](const TArray<FVector2f>& Candidate)
      {
        for (int32 Index = 0; Index < LocalAgents.Num(); ++Index)
          if (!IsFinite(Candidate[Index]) || Candidate[Index].Size()
            > LocalAgents[Index]->MaxSpeedCmps + Settings.ConstraintEpsilonCmps) return false;
        for (const int32 PairIndex : Component.PairIndexes)
          if (!IsPairValid(Candidate, PairIndex)) return false;
        if (Settings.TransitClearanceWeightQ8 > 0
          && MaximumTimeAlignedClearanceDeficit(Candidate) > Settings.PositionQuantumCm)
          return false;
        return true;
      };
      bool bQuantizedValid = AreAllQuantizedVelocitiesValid(Quantized);
      if (!bQuantizedValid)
      {
        const float Quantum = FMath::Max(0.001f, Settings.VelocityQuantumCmps);
        TArray<TArray<FVector2f>> CandidateSets;
        CandidateSets.SetNum(LocalAgents.Num());
        for (int32 Index = 0; Index < LocalAgents.Num(); ++Index)
        {
          const FVector2f Center = Quantize(Velocities[Index], Quantum);
          TArray<FVector2f>& Candidates = CandidateSets[Index];
          const int32 Radius = LocalAgents[Index]->bExternalVelocityFixed ? 0 : 1;
          for (int32 DX = -Radius; DX <= Radius; ++DX)
          {
            for (int32 DY = -Radius; DY <= Radius; ++DY)
            {
              const FVector2f Candidate = Center + FVector2f(DX * Quantum, DY * Quantum);
              if (!IsFinite(Candidate) || Candidate.Size()
                > LocalAgents[Index]->MaxSpeedCmps + Settings.ConstraintEpsilonCmps) continue;
              if (!Candidates.ContainsByPredicate([&](const FVector2f Existing)
                { return Existing.Equals(Candidate, 0.0001f); })) Candidates.Add(Candidate);
            }
          }
          Candidates.Sort([&](const FVector2f Left, const FVector2f Right)
          {
            const float LeftDistance = (Left - Velocities[Index]).SizeSquared();
            const float RightDistance = (Right - Velocities[Index]).SizeSquared();
            if (!FMath::IsNearlyEqual(LeftDistance, RightDistance, 0.0001f))
              return LeftDistance < RightDistance;
            if (!FMath::IsNearlyEqual(Left.X, Right.X, 0.0001f)) return Left.X < Right.X;
            return Left.Y < Right.Y;
          });
        }
        TArray<FVector2f> Working;
        Working.SetNumZeroed(LocalAgents.Num());
        int32 CandidateAttempts = 0;
        bool bSearchExhausted = false;
        const int32 CandidateLimit = FMath::Max(1, Settings.QuantizationRepairCandidateLimit);
        TFunction<bool(int32)> Search = [&](const int32 AgentIndex)
        {
          if (AgentIndex == LocalAgents.Num()) return AreAllQuantizedVelocitiesValid(Working);
          for (const FVector2f Candidate : CandidateSets[AgentIndex])
          {
            if (++CandidateAttempts > CandidateLimit)
            {
              bSearchExhausted = true;
              return false;
            }
            Working[AgentIndex] = Candidate;
            bool bPartialValid = true;
            for (const int32 PairIndex : Component.PairIndexes)
            {
              const auto& Pair = Pairs[PairIndex];
              const int32 IA = LocalIndex.FindChecked(Pair.AgentAId);
              const int32 IB = LocalIndex.FindChecked(Pair.AgentBId);
              if (IA <= AgentIndex && IB <= AgentIndex && !IsPairValid(Working, PairIndex))
              {
                bPartialValid = false;
                break;
              }
            }
            if (bPartialValid && Search(AgentIndex + 1)) return true;
            if (bSearchExhausted) return false;
          }
          return false;
        };
        bQuantizedValid = Search(0);
        if (bQuantizedValid)
        {
          Quantized = MoveTemp(Working);
          ++InOutSummary.QuantizationRepairCount;
        }
        else if (bSearchExhausted)
        {
          ++InOutSummary.QuantizationRepairSearchExhaustedCount;
        }
      }
      if (!bQuantizedValid) Failure = ECrowdDemoJointVelocityStatus::QuantizedValidationFailure;
    }
    TArray<FVector2f> CandidateVelocities = Failure == ECrowdDemoJointVelocityStatus::Solved
      ? Quantized : Velocities;
    for (int32 Index = 0; Index < CandidateVelocities.Num(); ++Index)
    {
      if (IsFinite(CandidateVelocities[Index]))
        CandidateVelocities[Index] = Quantize(ClampSpeed(CandidateVelocities[Index],
          LocalAgents[Index]->MaxSpeedCmps), Settings.VelocityQuantumCmps);
    }
    float CandidateClearanceDeficitCmMax = 0.0f;
    int32 CandidateForwardCmps = 0;
    int32 BaselineForwardCmps = 0;
    int32 ActiveForwardIntentCount = 0;
    for (const FCrowdDemoTransitIntent& Intent : TransitIntents)
    {
      const int32 SeedIndex = LocalIndex.FindChecked(Intent.AgentId);
      const FVector2f Forward = Intent.PreferredVelocity.GetSafeNormal();
      if (!Forward.IsNearlyZero())
      {
        ++ActiveForwardIntentCount;
        CandidateForwardCmps += FMath::Max(0, FMath::RoundToInt(FVector2f::DotProduct(
          CandidateVelocities[SeedIndex], Forward)));
        BaselineForwardCmps += FMath::Max(0, FMath::RoundToInt(FVector2f::DotProduct(
          LocalAgents[SeedIndex]->BaselinePriorityOrcaVelocity, Forward)));
      }
      for (int32 YieldIndex = 0; YieldIndex < LocalAgents.Num(); ++YieldIndex)
      {
        if (YieldIndex == SeedIndex || LocalAgents[YieldIndex]->bTransitSeed) continue;
        const FTimeAlignedClearance Clearance = EvaluateTimeAlignedClearance(
          Intent.Position, CandidateVelocities[SeedIndex],
          LocalAgents[YieldIndex]->Position, CandidateVelocities[YieldIndex],
          Intent.PredictionHorizonSeconds);
        const float Required = Intent.NominalClearanceRadiusCm
          + LocalAgents[YieldIndex]->PhysicalRadiusCm + Settings.HardSafetyGapCm;
        CandidateClearanceDeficitCmMax = FMath::Max(
          CandidateClearanceDeficitCmMax, Required - Clearance.DistanceCm);
      }
    }
    CandidateClearanceDeficitCmMax = FMath::Max(0.0f, CandidateClearanceDeficitCmMax);
    if (Failure == ECrowdDemoJointVelocityStatus::Solved
      && Settings.TransitClearanceWeightQ8 > 0
      && CandidateClearanceDeficitCmMax > Settings.PositionQuantumCm)
      Failure = ECrowdDemoJointVelocityStatus::ClearanceNotAchieved;
    else if (Failure == ECrowdDemoJointVelocityStatus::Solved
      && Settings.TransitClearanceWeightQ8 > 0
      && ActiveForwardIntentCount > 0
      && CandidateForwardCmps <= BaselineForwardCmps)
      Failure = ECrowdDemoJointVelocityStatus::NoForwardGain;
    Result.Status = Failure;
    const bool bSolved = Failure == ECrowdDemoJointVelocityStatus::Solved;
    for (int32 Index = 0; Index < LocalAgents.Num(); ++Index)
    {
      const auto& Agent = *LocalAgents[Index];
      const FVector2f Output = bSolved ? Quantized[Index] : Agent.BaselinePriorityOrcaVelocity;
      FCrowdDemoJointVelocityAgentResult& AgentResult = Result.Agents.AddDefaulted_GetRef();
      AgentResult.AgentId = Agent.AgentId;
      AgentResult.Velocity = Output;
      AgentResult.JointCandidateVelocity = CandidateVelocities[Index];
      AgentResult.BaselineVelocity = Agent.BaselinePriorityOrcaVelocity;
      AgentResult.bUsedJointVelocity = bSolved;
      AgentResult.bJointCandidateFinite = IsFinite(CandidateVelocities[Index]);
      Result.YieldingAgentCount += bSolved
        && (Output - Agent.PreferredVelocity).Size() > Settings.VelocityQuantumCmps ? 1 : 0;
    }
    for (const int32 PairIndex : Component.PairIndexes)
    {
      const auto& Pair = Pairs[PairIndex];
      const int32 IA = LocalIndex.FindChecked(Pair.AgentAId);
      const int32 IB = LocalIndex.FindChecked(Pair.AgentBId);
      const auto& A = *LocalAgents[IA]; const auto& B = *LocalAgents[IB];
      const FVector2f VA = bSolved ? Quantized[IA] : A.BaselinePriorityOrcaVelocity;
      const FVector2f VB = bSolved ? Quantized[IB] : B.BaselinePriorityOrcaVelocity;
      const FVector2f JointVA = CandidateVelocities[IA];
      const FVector2f JointVB = CandidateVelocities[IB];
      const FVector2f BaselineVA = A.BaselinePriorityOrcaVelocity;
      const FVector2f BaselineVB = B.BaselinePriorityOrcaVelocity;
      const FVector2f Normal = StablePairNormal(A, B, InOutSummary.DegenerateNormalCount);
      const float HardDistance = HardPairDistanceCm(
        A.PhysicalRadiusCm, B.PhysicalRadiusCm, Pair.HardSafetyGapCm);
      const float PreferredDistance = PreferredPairDistanceCm(
        HardDistance, Pair.PreferredSpacingGapCm, Pair.ContextScaleQ15);
      const float NextProjection = FVector2f::DotProduct(
        (B.Position + VB * Dt) - (A.Position + VA * Dt), Normal);
      const float JointNextProjection = FVector2f::DotProduct(
        (B.Position + JointVB * Dt) - (A.Position + JointVA * Dt), Normal);
      const float BaselineNextProjection = FVector2f::DotProduct(
        (B.Position + BaselineVB * Dt) - (A.Position + BaselineVA * Dt), Normal);
      const float JointCanonical = FVector2f::DotProduct(
        (JointVA - JointVB) - Pair.Canonical.RelativeVelocityPoint,
        Pair.Canonical.Normal);
      const float BaselineCanonical = FVector2f::DotProduct(
        (BaselineVA - BaselineVB) - Pair.Canonical.RelativeVelocityPoint,
        Pair.Canonical.Normal);
      Result.HardPairDistanceViolationCount +=
        NextProjection + Settings.ConstraintEpsilonCmps < HardDistance ? 1 : 0;
      Result.JointCandidateHardPairViolationCount +=
        !IsFinite(JointVA) || !IsFinite(JointVB)
        || JointNextProjection + Settings.ConstraintEpsilonCmps < HardDistance ? 1 : 0;
      Result.BaselineFallbackHardPairViolationCount +=
        BaselineNextProjection + Settings.ConstraintEpsilonCmps < HardDistance ? 1 : 0;
      FCrowdDemoJointVelocityPairResidual& Residual = Result.PairResiduals.AddDefaulted_GetRef();
      Residual.AgentAId = Pair.AgentAId;
      Residual.AgentBId = Pair.AgentBId;
      Residual.JointHardDeficitCm = FMath::Max(0.0f, HardDistance - JointNextProjection);
      Residual.BaselineHardDeficitCm = FMath::Max(0.0f, HardDistance - BaselineNextProjection);
      Residual.JointCanonicalDeficitCmps = FMath::Max(0.0f, -JointCanonical);
      Residual.BaselineCanonicalDeficitCmps = FMath::Max(0.0f, -BaselineCanonical);
      Residual.JointPreferredSpacingDeficitCm = FMath::Max(
        0.0f, PreferredDistance - JointNextProjection);
      Residual.BaselinePreferredSpacingDeficitCm = FMath::Max(
        0.0f, PreferredDistance - BaselineNextProjection);
      if (NextProjection + Settings.ConstraintEpsilonCmps >= PreferredDistance)
        ++Result.PreferredSpacingSatisfiedPairCount;
      else
      {
        ++Result.SpacingCompressedPairCount;
        Result.PreferredSpacingDeficitCmMax = FMath::Max(
          Result.PreferredSpacingDeficitCmMax, PreferredDistance - NextProjection);
      }
    }
    for (const FCrowdDemoTransitIntent& Intent : TransitIntents)
    {
      const int32 SeedIndex = LocalIndex.FindChecked(Intent.AgentId);
      const FVector2f SeedVelocity = bSolved ? Quantized[SeedIndex]
        : LocalAgents[SeedIndex]->BaselinePriorityOrcaVelocity;
      const FVector2f JointSeedVelocity = CandidateVelocities[SeedIndex];
      const FVector2f BaselineSeedVelocity =
        LocalAgents[SeedIndex]->BaselinePriorityOrcaVelocity;
      for (int32 YieldIndex = 0; YieldIndex < LocalAgents.Num(); ++YieldIndex)
      {
        if (YieldIndex == SeedIndex) continue;
        const FCrowdDemoJointVelocityAgent& Yielding = *LocalAgents[YieldIndex];
        if (Yielding.bTransitSeed) continue;
        const FVector2f YieldVelocity = bSolved ? Quantized[YieldIndex]
          : Yielding.BaselinePriorityOrcaVelocity;
        const FVector2f JointYieldVelocity = CandidateVelocities[YieldIndex];
        const FVector2f BaselineYieldVelocity = Yielding.BaselinePriorityOrcaVelocity;
        const FTimeAlignedClearance OutputClearance = EvaluateTimeAlignedClearance(
          Intent.Position, SeedVelocity, Yielding.Position, YieldVelocity,
          Intent.PredictionHorizonSeconds);
        const FTimeAlignedClearance JointClearance = EvaluateTimeAlignedClearance(
          Intent.Position, JointSeedVelocity, Yielding.Position, JointYieldVelocity,
          Intent.PredictionHorizonSeconds);
        const FTimeAlignedClearance BaselineClearance = EvaluateTimeAlignedClearance(
          Intent.Position, BaselineSeedVelocity, Yielding.Position, BaselineYieldVelocity,
          Intent.PredictionHorizonSeconds);
        const float Required = Intent.NominalClearanceRadiusCm
          + Yielding.PhysicalRadiusCm + Settings.HardSafetyGapCm;
        Result.TransitCapsuleClearanceDeficitCmMax = FMath::Max(
          Result.TransitCapsuleClearanceDeficitCmMax,
          FMath::Max(0.0f, Required - OutputClearance.DistanceCm));
        Result.JointCandidateClearanceDeficitCmMax = FMath::Max(
          Result.JointCandidateClearanceDeficitCmMax,
          FMath::Max(0.0f, Required - JointClearance.DistanceCm));
        Result.BaselineFallbackClearanceDeficitCmMax = FMath::Max(
          Result.BaselineFallbackClearanceDeficitCmMax,
          FMath::Max(0.0f, Required - BaselineClearance.DistanceCm));
        Result.MaximumYieldDisplacementCm = FMath::Max(
          Result.MaximumYieldDisplacementCm, YieldVelocity.Size() * Dt);
      }
    }
    Result.TransitYieldingAgentCount = TransitYieldedAgentIds.Num();
    uint32 Hash = 2166136261u;
    Hash = Fold(Hash, Result.ComponentId);
    Hash = Fold(Hash, static_cast<int32>(Result.Status));
    Hash = Fold(Hash, static_cast<int32>(TransitIntentHash));
    Hash = Fold(Hash, Result.TransitClearancePairCount);
    Hash = Fold(Hash, Result.TransitYieldingAgentCount);
    Hash = Fold(Hash, FMath::RoundToInt(Result.TransitCapsuleClearanceDeficitCmMax));
    Hash = Fold(Hash, Result.JointCandidateHardPairViolationCount);
    Hash = Fold(Hash, Result.BaselineFallbackHardPairViolationCount);
    Hash = Fold(Hash, FMath::RoundToInt(Result.JointCandidateClearanceDeficitCmMax));
    Hash = Fold(Hash, FMath::RoundToInt(Result.BaselineFallbackClearanceDeficitCmMax));
    Hash = Fold(Hash, FMath::RoundToInt(Result.MaximumYieldDisplacementCm));
    for (const auto& Agent : Result.Agents)
    {
      Hash = Fold(Hash, Agent.AgentId);
      Hash = Fold(Hash, FMath::RoundToInt(Agent.Velocity.X));
      Hash = Fold(Hash, FMath::RoundToInt(Agent.Velocity.Y));
      Hash = Fold(Hash, FMath::RoundToInt(Agent.JointCandidateVelocity.X));
      Hash = Fold(Hash, FMath::RoundToInt(Agent.JointCandidateVelocity.Y));
      Hash = Fold(Hash, FMath::RoundToInt(Agent.BaselineVelocity.X));
      Hash = Fold(Hash, FMath::RoundToInt(Agent.BaselineVelocity.Y));
    }
    Result.StableHash = Hash;
    switch (Result.Status)
    {
    case ECrowdDemoJointVelocityStatus::Solved: ++InOutSummary.SolvedCount; break;
    case ECrowdDemoJointVelocityStatus::HardInfeasible: ++InOutSummary.HardInfeasibleCount; break;
    case ECrowdDemoJointVelocityStatus::IterationLimit: ++InOutSummary.IterationLimitCount; break;
    case ECrowdDemoJointVelocityStatus::ClearanceNotAchieved:
      ++InOutSummary.ClearanceNotAchievedCount; break;
    case ECrowdDemoJointVelocityStatus::NoForwardGain:
      ++InOutSummary.NoForwardGainCount; break;
    case ECrowdDemoJointVelocityStatus::NumericalFailure: ++InOutSummary.NumericalFailureCount; break;
    case ECrowdDemoJointVelocityStatus::QuantizedValidationFailure:
      ++InOutSummary.QuantizedValidationFailureCount; break;
    case ECrowdDemoJointVelocityStatus::InvalidInput: ++InOutSummary.InvalidInputCount; break;
    default: break;
    }
    InOutSummary.YieldingAgentCount += Result.YieldingAgentCount;
    InOutSummary.HardPairDistanceViolationCount += Result.HardPairDistanceViolationCount;
    InOutSummary.JointCandidateHardPairViolationCount +=
      Result.JointCandidateHardPairViolationCount;
    InOutSummary.BaselineFallbackHardPairViolationCount +=
      Result.BaselineFallbackHardPairViolationCount;
    InOutSummary.PreferredSpacingSatisfiedPairCount += Result.PreferredSpacingSatisfiedPairCount;
    InOutSummary.SpacingCompressedPairCount += Result.SpacingCompressedPairCount;
    InOutSummary.TransitYieldingAgentCount += Result.TransitYieldingAgentCount;
    InOutSummary.TransitCapsuleClearanceDeficitCmMax = FMath::Max(
      InOutSummary.TransitCapsuleClearanceDeficitCmMax,
      Result.TransitCapsuleClearanceDeficitCmMax);
    InOutSummary.JointCandidateClearanceDeficitCmMax = FMath::Max(
      InOutSummary.JointCandidateClearanceDeficitCmMax,
      Result.JointCandidateClearanceDeficitCmMax);
    InOutSummary.BaselineFallbackClearanceDeficitCmMax = FMath::Max(
      InOutSummary.BaselineFallbackClearanceDeficitCmMax,
      Result.BaselineFallbackClearanceDeficitCmMax);
    InOutSummary.MaximumYieldDisplacementCm = FMath::Max(
      InOutSummary.MaximumYieldDisplacementCm, Result.MaximumYieldDisplacementCm);
    SummaryHash = Fold(SummaryHash, Result.ComponentId);
    SummaryHash = Fold(SummaryHash, static_cast<int32>(Result.Status));
    SummaryHash = Fold(SummaryHash, Result.StableHash);
    OutResults.Add(MoveTemp(Result));
  }
  InOutSummary.StableHash = Fold(SummaryHash, static_cast<int32>(InOutSummary.PairOwnerHash));
}

void FCrowdDemoJointVelocityKernel::BuildTransitCapacityFailureFixture(
  const TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
  const TConstArrayView<FCrowdDemoJointVelocityPair> Pairs,
  const TConstArrayView<FCrowdDemoJointVelocityComponent> Components,
  const TConstArrayView<FCrowdDemoJointVelocityComponentResult> Results,
  const FCrowdDemoAdaptiveSpacingSettings& Settings,
  const FCrowdDemoJointVelocityEnvironment& Environment,
  FCrowdDemoTransitCapacityFailureFixture& OutFixture)
{
  OutFixture = FCrowdDemoTransitCapacityFailureFixture();
  TMap<int32, const FCrowdDemoJointVelocityComponentResult*> ResultByComponent;
  for (const FCrowdDemoJointVelocityComponentResult& Result : Results)
    ResultByComponent.Add(Result.ComponentId, &Result);
  TArray<const FCrowdDemoJointVelocityComponent*> Failed;
  for (const FCrowdDemoJointVelocityComponent& Component : Components)
  {
    const FCrowdDemoJointVelocityComponentResult* const* Found =
      ResultByComponent.Find(Component.ComponentId);
    if (!Found) continue;
    const FCrowdDemoJointVelocityComponentResult& Result = **Found;
    const bool bFailed = Result.Status != ECrowdDemoJointVelocityStatus::Solved
      || Result.JointCandidateHardPairViolationCount > 0
      || Result.JointCandidateFlowBoundsViolationCount > 0
      || Result.JointCandidateObstacleViolationCount > 0
      || Result.JointCandidateTargetViolationCount > 0
      || Result.JointCandidateClearanceDeficitCmMax > Settings.PositionQuantumCm;
    if (bFailed) Failed.Add(&Component);
  }
  Failed.Sort([](const FCrowdDemoJointVelocityComponent& A,
    const FCrowdDemoJointVelocityComponent& B)
  {
    if (A.AgentIds.Num() != B.AgentIds.Num()) return A.AgentIds.Num() < B.AgentIds.Num();
    return A.ComponentId < B.ComponentId;
  });
  if (Failed.IsEmpty()) return;
  OutFixture.Component = *Failed[0];
  const FCrowdDemoJointVelocityComponentResult* const* SelectedResult =
    ResultByComponent.Find(OutFixture.Component.ComponentId);
  if (!SelectedResult) return;
  OutFixture.Result = **SelectedResult;
  TSet<int32> IncludedAgentIds;
  for (const int32 AgentId : OutFixture.Component.AgentIds) IncludedAgentIds.Add(AgentId);
  for (const FCrowdDemoJointVelocityAgent& Agent : Agents)
    if (IncludedAgentIds.Contains(Agent.AgentId)) OutFixture.Agents.Add(Agent);
  OutFixture.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (const int32 PairIndex : OutFixture.Component.PairIndexes)
  {
    if (!Pairs.IsValidIndex(PairIndex)) return;
    OutFixture.Pairs.Add(Pairs[PairIndex]);
  }
  OutFixture.Pairs.Sort([](const auto& A, const auto& B)
  {
    if (A.AgentAId != B.AgentAId) return A.AgentAId < B.AgentAId;
    return A.AgentBId < B.AgentBId;
  });
  uint32 IntentHash = 2166136261u;
  if (!BuildTransitIntents(OutFixture.Agents, Settings,
    OutFixture.TransitIntents, IntentHash)) return;
  TMap<int32, const FCrowdDemoJointVelocityAgent*> AgentById;
  for (const FCrowdDemoJointVelocityAgent& Agent : OutFixture.Agents)
    AgentById.Add(Agent.AgentId, &Agent);
  const float Dt = FMath::Max(0.001f, Settings.FixedStepSeconds);
  for (const FCrowdDemoJointVelocityAgentResult& AgentResult : OutFixture.Result.Agents)
  {
    const FCrowdDemoJointVelocityAgent* const* Agent = AgentById.Find(AgentResult.AgentId);
    if (!Agent) return;
    FCrowdDemoTransitCapacityEnvironmentDiagnostic& Diagnostic =
      OutFixture.EnvironmentDiagnostics.AddDefaulted_GetRef();
    Diagnostic.AgentId = AgentResult.AgentId;
    const FVector2f Start = (*Agent)->Position;
    const FVector2f CandidateEnd = Start + AgentResult.JointCandidateVelocity * Dt;
    const FVector2f BaselineEnd = Start + AgentResult.BaselineVelocity * Dt;
    Diagnostic.JointCandidateConstraint =
      FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
        Environment.FlowConfig, FVector(Start.X, Start.Y, 0.0f),
        FVector(CandidateEnd.X, CandidateEnd.Y, 0.0f), true);
    Diagnostic.BaselineConstraint =
      FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
        Environment.FlowConfig, FVector(Start.X, Start.Y, 0.0f),
        FVector(BaselineEnd.X, BaselineEnd.Y, 0.0f), true);
    Diagnostic.bJointCandidateTargetViolation = Environment.bValidateTargetExclusion
      && (CandidateEnd - Environment.TargetLocation).Size()
        < Environment.TargetExclusionRadiusCm - 0.01f;
    Diagnostic.bBaselineTargetViolation = Environment.bValidateTargetExclusion
      && (BaselineEnd - Environment.TargetLocation).Size()
        < Environment.TargetExclusionRadiusCm - 0.01f;
  }
  OutFixture.EnvironmentDiagnostics.Sort([](const auto& A, const auto& B)
    { return A.AgentId < B.AgentId; });
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, OutFixture.Component.ComponentId);
  Hash = Fold(Hash, static_cast<int32>(OutFixture.Result.Status));
  Hash = Fold(Hash, static_cast<int32>(IntentHash));
  for (const FCrowdDemoJointVelocityAgent& Agent : OutFixture.Agents)
  {
    Hash = Fold(Hash, Agent.AgentId);
    Hash = Fold(Hash, FMath::RoundToInt(Agent.Position.X));
    Hash = Fold(Hash, FMath::RoundToInt(Agent.Position.Y));
    Hash = Fold(Hash, FMath::RoundToInt(Agent.PreferredVelocity.X));
    Hash = Fold(Hash, FMath::RoundToInt(Agent.PreferredVelocity.Y));
  }
  for (const FCrowdDemoJointVelocityPair& Pair : OutFixture.Pairs)
  {
    Hash = Fold(Hash, Pair.AgentAId);
    Hash = Fold(Hash, Pair.AgentBId);
    Hash = Fold(Hash, FMath::RoundToInt(Pair.Canonical.RelativeVelocityPoint.X));
    Hash = Fold(Hash, FMath::RoundToInt(Pair.Canonical.RelativeVelocityPoint.Y));
    Hash = Fold(Hash, FMath::RoundToInt(Pair.Canonical.Normal.X * 32767.0f));
    Hash = Fold(Hash, FMath::RoundToInt(Pair.Canonical.Normal.Y * 32767.0f));
  }
  for (const FCrowdDemoJointVelocityPairResidual& Residual : OutFixture.Result.PairResiduals)
  {
    Hash = Fold(Hash, Residual.AgentAId);
    Hash = Fold(Hash, Residual.AgentBId);
    Hash = Fold(Hash, FMath::RoundToInt(Residual.JointHardDeficitCm));
    Hash = Fold(Hash, FMath::RoundToInt(Residual.BaselineHardDeficitCm));
    Hash = Fold(Hash, FMath::RoundToInt(Residual.JointCanonicalDeficitCmps));
    Hash = Fold(Hash, FMath::RoundToInt(Residual.BaselineCanonicalDeficitCmps));
  }
  for (const FCrowdDemoTransitCapacityEnvironmentDiagnostic& Diagnostic
    : OutFixture.EnvironmentDiagnostics)
  {
    Hash = Fold(Hash, Diagnostic.AgentId);
    Hash = Fold(Hash, static_cast<int32>(Diagnostic.JointCandidateConstraint.StableHash));
    Hash = Fold(Hash, static_cast<int32>(Diagnostic.BaselineConstraint.StableHash));
    Hash = Fold(Hash, Diagnostic.bJointCandidateTargetViolation ? 1 : 0);
    Hash = Fold(Hash, Diagnostic.bBaselineTargetViolation ? 1 : 0);
  }
  OutFixture.StableHash = Hash;
  OutFixture.bValid = OutFixture.Agents.Num() == OutFixture.Component.AgentIds.Num()
    && !OutFixture.Agents.IsEmpty() && !OutFixture.TransitIntents.IsEmpty()
    && OutFixture.Result.ComponentId == OutFixture.Component.ComponentId;
}

void FCrowdDemoJointVelocityKernel::BuildDiagnosticFixture(
  const int32 PrimaryAgentId,
  const TConstArrayView<FCrowdDemoTransitJointDiagnosticAgent> InputAgents,
  const TConstArrayView<FCrowdDemoJointVelocityPair> InputPairs,
  const FCrowdDemoAdaptiveSpacingSettings& Settings,
  FCrowdDemoTransitJointDiagnosticFixture& OutFixture)
{
  OutFixture = FCrowdDemoTransitJointDiagnosticFixture();
  OutFixture.Summary.PrimaryAgentId = PrimaryAgentId;
  TArray<FCrowdDemoTransitJointDiagnosticAgent> Agents(InputAgents);
  Agents.Sort([](const auto& A, const auto& B)
  {
    return A.JointAgent.AgentId < B.JointAgent.AgentId;
  });
  if (!Agents.ContainsByPredicate([&](const auto& Agent)
    { return Agent.JointAgent.AgentId == PrimaryAgentId; })) return;
  TArray<FCrowdDemoJointVelocityAgent> JointAgents;
  JointAgents.Reserve(Agents.Num());
  for (FCrowdDemoTransitJointDiagnosticAgent& Agent : Agents)
  {
    Agent.JointAgent.bTransitSeed = Agent.JointAgent.AgentId == PrimaryAgentId;
    Agent.PriorityConstraints.Sort([](const auto& A, const auto& B)
    {
      if (A.StableConstraintOrder != B.StableConstraintOrder)
        return A.StableConstraintOrder < B.StableConstraintOrder;
      if (A.OtherAgentId != B.OtherAgentId) return A.OtherAgentId < B.OtherAgentId;
      if (!FMath::IsNearlyEqual(A.Point.X, B.Point.X, 0.0001f)) return A.Point.X < B.Point.X;
      if (!FMath::IsNearlyEqual(A.Point.Y, B.Point.Y, 0.0001f)) return A.Point.Y < B.Point.Y;
      if (!FMath::IsNearlyEqual(A.Normal.X, B.Normal.X, 0.0001f)) return A.Normal.X < B.Normal.X;
      return A.Normal.Y < B.Normal.Y;
    });
    JointAgents.Add(Agent.JointAgent);
  }
  TArray<FCrowdDemoJointVelocityPair> Pairs(InputPairs);
  TArray<FCrowdDemoJointVelocityComponent> Components;
  FCrowdDemoJointVelocitySummary JointSummary;
  if (!BuildLocalComponents(JointAgents, Pairs, Settings, Components, JointSummary)) return;
  const FCrowdDemoJointVelocityComponent* Component = Components.FindByPredicate(
    [&](const auto& Item) { return Item.AgentIds.Contains(PrimaryAgentId); });
  if (!Component) return;
  OutFixture.Summary.ComponentAgentCount = Component->AgentIds.Num();
  OutFixture.Summary.ComponentPairCount = Component->PairIndexes.Num();
  OutFixture.Summary.bFixtureTooLarge = Component->bOversize;
  if (Component->bOversize)
    OutFixture.Summary.JointStatus = ECrowdDemoJointVelocityStatus::OversizeFallback;
  const FCrowdDemoTransitJointDiagnosticAgent* Primary = Agents.FindByPredicate(
    [&](const auto& Agent) { return Agent.JointAgent.AgentId == PrimaryAgentId; });
  if (!Primary) return;
  OutFixture.Summary.ConstraintCount = Primary->PriorityConstraints.Num();
  const FVector2f Forward = Primary->JointAgent.PreferredVelocity.GetSafeNormal();
  OutFixture.Summary.PriorityForwardSpeedCmps = FMath::RoundToInt(
    FVector2f::DotProduct(Primary->PriorityOrcaVelocity, Forward));
  OutFixture.Summary.PredictedSpeedCmps = FMath::RoundToInt(Primary->PredictedVelocity.Size());
  OutFixture.Summary.ObstacleSpeedCmps = FMath::RoundToInt(Primary->ObstacleVelocity.Size());
  OutFixture.Summary.PbdSpeedCmps = FMath::RoundToInt(Primary->PbdVelocity.Size());
  OutFixture.Summary.ReprojectSpeedCmps = FMath::RoundToInt(Primary->ReprojectVelocity.Size());
  OutFixture.Summary.FinalSpeedCmps = FMath::RoundToInt(Primary->FinalVelocity.Size());
  const bool bPriorityNonZero = Primary->PriorityOrcaVelocity.Size() > 1.0f;
  OutFixture.Summary.bPriorityNonZeroDownstreamZero =
    bPriorityNonZero && Primary->FinalVelocity.Size() <= 1.0f;
  if (bPriorityNonZero)
  {
    if (Primary->PredictedVelocity.Size() <= 1.0f)
      OutFixture.Summary.DownstreamZeroStage = ECrowdDemoTransitDownstreamZeroStage::MovementPredict;
    else if (Primary->ObstacleVelocity.Size() <= 1.0f)
      OutFixture.Summary.DownstreamZeroStage = ECrowdDemoTransitDownstreamZeroStage::ObstacleConstraint;
    else if (Primary->PbdVelocity.Size() <= 1.0f)
      OutFixture.Summary.DownstreamZeroStage = ECrowdDemoTransitDownstreamZeroStage::HardPbd;
    else if (Primary->ReprojectVelocity.Size() <= 1.0f)
      OutFixture.Summary.DownstreamZeroStage = ECrowdDemoTransitDownstreamZeroStage::ObstacleReproject;
    else if (Primary->FinalVelocity.Size() <= 1.0f)
      OutFixture.Summary.DownstreamZeroStage = ECrowdDemoTransitDownstreamZeroStage::MovementFinalize;
  }
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, PrimaryAgentId);
  Hash = Fold(Hash, Component->AgentIds.Num());
  Hash = Fold(Hash, Component->PairIndexes.Num());
  const auto FoldVector = [&](const FVector2f Value)
  {
    Hash = Fold(Hash, FMath::RoundToInt(Value.X));
    Hash = Fold(Hash, FMath::RoundToInt(Value.Y));
  };
  for (const int32 AgentId : Component->AgentIds)
  {
    const FCrowdDemoTransitJointDiagnosticAgent* Agent = Agents.FindByPredicate(
      [&](const auto& Item) { return Item.JointAgent.AgentId == AgentId; });
    if (!Agent) return;
    Hash = Fold(Hash, AgentId);
    Hash = Fold(Hash, Agent->SteeringState);
    FoldVector(Agent->StartLocation);
    FoldVector(Agent->JointAgent.PreferredVelocity);
    FoldVector(Agent->PriorityOrcaVelocity);
    FoldVector(Agent->PredictedVelocity);
    FoldVector(Agent->ObstacleVelocity);
    FoldVector(Agent->PbdVelocity);
    FoldVector(Agent->ReprojectVelocity);
    FoldVector(Agent->FinalVelocity);
    FoldVector(Agent->PbdCorrection);
    FoldVector(Agent->ObstacleReprojectDelta);
    Hash = Fold(Hash, Agent->PriorityConstraints.Num());
    for (const FCrowdDemoOrcaConstraint& Constraint : Agent->PriorityConstraints)
    {
      Hash = Fold(Hash, Constraint.OtherAgentId);
      FoldVector(Constraint.Point);
      FoldVector(Constraint.Normal * 32767.0f);
      Hash = Fold(Hash, static_cast<int32>(Constraint.Kind));
      Hash = Fold(Hash, FMath::RoundToInt(Constraint.Responsibility * 100.0f));
    }
    if (!Component->bOversize) OutFixture.Agents.Add(*Agent);
  }
  for (const int32 PairIndex : Component->PairIndexes)
  {
    if (!Pairs.IsValidIndex(PairIndex)) return;
    const FCrowdDemoJointVelocityPair& Pair = Pairs[PairIndex];
    Hash = Fold(Hash, Pair.AgentAId);
    Hash = Fold(Hash, Pair.AgentBId);
    FoldVector(Pair.Canonical.RelativeVelocityPoint);
    FoldVector(Pair.Canonical.Normal * 32767.0f);
    Hash = Fold(Hash, FMath::RoundToInt(Pair.HardSafetyGapCm));
    Hash = Fold(Hash, FMath::RoundToInt(Pair.PreferredSpacingGapCm));
    Hash = Fold(Hash, Pair.ContextScaleQ15);
    if (!Component->bOversize) OutFixture.Pairs.Add(Pair);
  }
  if (!Component->bOversize)
  {
    TArray<FCrowdDemoJointVelocityComponent> TargetComponents = {*Component};
    TArray<FCrowdDemoJointVelocityComponentResult> Results;
    Solve(JointAgents, Pairs, TargetComponents, Settings, Results, JointSummary);
    if (Results.Num() != 1) return;
    OutFixture.JointResult = MoveTemp(Results[0]);
    OutFixture.Summary.JointStatus = OutFixture.JointResult.Status;
    OutFixture.Summary.JointHardViolationCount =
      OutFixture.JointResult.HardPairDistanceViolationCount;
    if (const FCrowdDemoJointVelocityAgentResult* JointPrimary =
      OutFixture.JointResult.Agents.FindByPredicate(
        [&](const auto& Agent) { return Agent.AgentId == PrimaryAgentId; }))
      OutFixture.Summary.JointForwardSpeedCmps = FMath::RoundToInt(
        FVector2f::DotProduct(JointPrimary->Velocity, Forward));
    OutFixture.Summary.bJointQuantizedSafeForward =
      OutFixture.JointResult.Status == ECrowdDemoJointVelocityStatus::Solved
      && OutFixture.JointResult.HardPairDistanceViolationCount == 0
      && OutFixture.Summary.JointForwardSpeedCmps
        >= OutFixture.Summary.PriorityForwardSpeedCmps + 30;
    Hash = Fold(Hash, static_cast<int32>(OutFixture.JointResult.Status));
    Hash = Fold(Hash, OutFixture.Summary.JointForwardSpeedCmps);
    Hash = Fold(Hash, OutFixture.JointResult.HardPairDistanceViolationCount);
    Hash = Fold(Hash, static_cast<int32>(OutFixture.JointResult.StableHash));
  }
  Hash = Fold(Hash, static_cast<int32>(OutFixture.Summary.DownstreamZeroStage));
  Hash = Fold(Hash, OutFixture.Summary.bFixtureTooLarge ? 1 : 0);
  OutFixture.StableHash = Hash;
  OutFixture.bValid = true;
}
