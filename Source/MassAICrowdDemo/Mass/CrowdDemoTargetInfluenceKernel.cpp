#include "Mass/CrowdDemoTargetInfluenceKernel.h"

namespace CrowdDemoTargetInfluencePrivate
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, uint32 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime;
    return Hash;
  }

  int32 Quantize(float Value, float Quantum)
  {
    return FMath::RoundToInt(Value / FMath::Max(Quantum, KINDA_SMALL_NUMBER));
  }

  FVector2f QuantizeVector(const FVector2f& Value, float Quantum)
  {
    return FVector2f(
      Quantize(Value.X, Quantum) * Quantum,
      Quantize(Value.Y, Quantum) * Quantum);
  }

  FVector2f StableDirectionFromAgentId(int32 AgentId)
  {
    uint32 Hash = Fold(FnvOffset, static_cast<uint32>(AgentId));
    Hash = Fold(Hash, 0x9e3779b9u);
    const float Angle = static_cast<float>(Hash % 65536u) * (2.0f * PI / 65536.0f);
    return FVector2f(FMath::Cos(Angle), FMath::Sin(Angle));
  }

  int32 StableTieDirection(int32 AgentId)
  {
    const uint32 Hash = Fold(Fold(FnvOffset, static_cast<uint32>(AgentId)), 0x85ebca6bu);
    return (Hash & 1u) == 0u ? -1 : 1;
  }

  FVector2f ClampMagnitude(const FVector2f& Value, float Maximum)
  {
    const float SafeMaximum = FMath::Max(0.0f, Maximum);
    const float SizeSquared = Value.SizeSquared();
    if (SizeSquared <= FMath::Square(SafeMaximum) || SizeSquared <= SMALL_NUMBER)
      return Value;
    return Value * (SafeMaximum / FMath::Sqrt(SizeSquared));
  }

  FVector2f QuantizeWithinMagnitude(const FVector2f& Value, float Maximum, float Quantum)
  {
    FVector2f Quantized = QuantizeVector(ClampMagnitude(Value, Maximum), Quantum);
    if (Quantized.SizeSquared() <= FMath::Square(FMath::Max(0.0f, Maximum)) + KINDA_SMALL_NUMBER)
      return Quantized;
    const float RoundingMargin = 0.708f * Quantum;
    const float SafeRadius = FMath::Max(0.0f, Maximum - RoundingMargin);
    Quantized = QuantizeVector(ClampMagnitude(Quantized, SafeRadius), Quantum);
    return Quantized.SizeSquared() <= FMath::Square(FMath::Max(0.0f, Maximum)) + KINDA_SMALL_NUMBER
      ? Quantized : FVector2f::ZeroVector;
  }

  float Percentile(TArray<float> Values, float Alpha)
  {
    if (Values.IsEmpty()) return 0.0f;
    Values.Sort();
    const int32 Index = FMath::Clamp(
      FMath::CeilToInt(Alpha * static_cast<float>(Values.Num())) - 1,
      0, Values.Num() - 1);
    return Values[Index];
  }

  int32 SectorForOffset(const FVector2f& Offset, int32 SectorCount)
  {
    float Angle = FMath::Atan2(Offset.Y, Offset.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    return FMath::Clamp(
      FMath::FloorToInt(Angle / (2.0f * PI) * SectorCount), 0, SectorCount - 1);
  }

  int32 LargestCircularEmptyRun(const TArray<int32>& Population)
  {
    if (Population.IsEmpty()) return 0;
    int32 Occupied = 0;
    for (const int32 Value : Population) Occupied += Value > 0 ? 1 : 0;
    if (Occupied == 0) return Population.Num();
    if (Occupied == Population.Num()) return 0;
    int32 Best = 0;
    int32 Current = 0;
    for (int32 Index = 0; Index < Population.Num() * 2; ++Index)
    {
      if (Population[Index % Population.Num()] == 0)
      {
        Current = FMath::Min(Current + 1, Population.Num());
        Best = FMath::Max(Best, Current);
      }
      else Current = 0;
    }
    return Best;
  }

  bool AreSettingsValid(const FCrowdDemoTargetInfluenceSettings& Settings)
  {
    return Settings.bEnabled
      && Settings.InfluenceBlendWidthCm > 0.0f
      && Settings.RadialGainPerSecond >= 0.0f
      && Settings.MaxRadialSpeedCmps >= 0.0f
      && Settings.FixedStepSeconds > 0.0f
      && Settings.PositionQuantumCm > 0.0f
      && Settings.VelocityQuantumCmps > 0.0f
      && Settings.AngularSectorCount > 2
      && Settings.RadialBandWidthCm > 0.0f
      && Settings.DensitySmoothingPassCount == 1
      && Settings.DensityMinimumDifference >= 1
      && Settings.DensitySpeedPerExcessAgentCmps >= 0.0f
      && Settings.MaximumDensityTangentialSpeedCmps >= 0.0f;
  }

  uint32 HashSettings(const FCrowdDemoTargetInfluenceSettings& Settings)
  {
    uint32 Hash = Fold(FnvOffset, 2u);
    Hash = Fold(Hash, Settings.bEnabled ? 1u : 0u);
    Hash = Fold(Hash, Quantize(Settings.InfluenceBlendWidthCm, 0.01f));
    Hash = Fold(Hash, Quantize(Settings.RadialGainPerSecond, 0.0001f));
    Hash = Fold(Hash, Quantize(Settings.MaxRadialSpeedCmps, 0.01f));
    Hash = Fold(Hash, Quantize(Settings.FixedStepSeconds, 0.000001f));
    Hash = Fold(Hash, Quantize(Settings.PositionQuantumCm, 0.001f));
    Hash = Fold(Hash, Quantize(Settings.VelocityQuantumCmps, 0.001f));
    Hash = Fold(Hash, static_cast<uint32>(Settings.AngularSectorCount));
    Hash = Fold(Hash, Quantize(Settings.RadialBandWidthCm, 0.01f));
    Hash = Fold(Hash, static_cast<uint32>(Settings.DensitySmoothingPassCount));
    Hash = Fold(Hash, static_cast<uint32>(Settings.DensityMinimumDifference));
    Hash = Fold(Hash, Quantize(Settings.DensitySpeedPerExcessAgentCmps, 0.01f));
    Hash = Fold(Hash, Quantize(Settings.MaximumDensityTangentialSpeedCmps, 0.01f));
    return Hash;
  }
}

void FCrowdDemoTargetInfluenceKernel::BuildPolarDensityField(
  TConstArrayView<FCrowdDemoTargetInfluenceAgent> AgentView,
  const FCrowdDemoTargetInfluenceSettings& Settings,
  FCrowdDemoTargetDensityField& OutField,
  FCrowdDemoTargetDensitySummary& OutSummary)
{
  using namespace CrowdDemoTargetInfluencePrivate;
  OutField = FCrowdDemoTargetDensityField();
  OutSummary = FCrowdDemoTargetDensitySummary();
  TArray<FCrowdDemoTargetInfluenceAgent> Agents(AgentView);
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  OutField.AngularSectorCount = Settings.AngularSectorCount;
  OutField.RadialBandWidthCm = Settings.RadialBandWidthCm;
  bool bValid = AreSettingsValid(Settings);
  float MaximumInfluenceRadius = 0.0f;
  int32 PreviousAgentId = INDEX_NONE;
  FVector2f SharedTarget = FVector2f::ZeroVector;
  bool bHasSharedTarget = false;
  for (const FCrowdDemoTargetInfluenceAgent& Agent : Agents)
  {
    bValid = bValid && Agent.AgentId != INDEX_NONE && Agent.AgentId != PreviousAgentId
      && Agent.MaximumCombatCenterDistanceCm >= 0.0f;
    PreviousAgentId = Agent.AgentId;
    const FVector2f QuantizedTarget = QuantizeVector(Agent.TargetLocation, Settings.PositionQuantumCm);
    if (!bHasSharedTarget) { SharedTarget = QuantizedTarget; bHasSharedTarget = true; }
    else bValid = bValid && QuantizedTarget.Equals(SharedTarget, KINDA_SMALL_NUMBER);
    MaximumInfluenceRadius = FMath::Max(MaximumInfluenceRadius,
      Agent.MaximumCombatCenterDistanceCm + Settings.InfluenceBlendWidthCm);
  }
  OutField.RadialBandCount = FMath::Max(1,
    FMath::CeilToInt(MaximumInfluenceRadius / FMath::Max(Settings.RadialBandWidthCm, 1.0f)));
  const int64 CellCount64 = static_cast<int64>(OutField.RadialBandCount)
    * static_cast<int64>(FMath::Max(Settings.AngularSectorCount, 0));
  bValid = bValid && CellCount64 > 0 && CellCount64 <= MAX_int32;
  if (!bValid)
  {
    OutField.bValid = false;
    OutField.FieldHash = Fold(HashSettings(Settings), 0u);
    OutSummary.FieldHash = OutField.FieldHash;
    return;
  }
  OutField.Cells.SetNum(static_cast<int32>(CellCount64));
  for (int32 Radial = 0; Radial < OutField.RadialBandCount; ++Radial)
  {
    for (int32 Angular = 0; Angular < OutField.AngularSectorCount; ++Angular)
    {
      auto& Cell = OutField.Cells[Radial * OutField.AngularSectorCount + Angular];
      Cell.RadialBandIndex = Radial;
      Cell.AngularSectorIndex = Angular;
    }
  }
  uint32 FieldHash = HashSettings(Settings);
  FieldHash = Fold(FieldHash, static_cast<uint32>(OutField.RadialBandCount));
  FieldHash = Fold(FieldHash, static_cast<uint32>(OutField.AngularSectorCount));
  for (const FCrowdDemoTargetInfluenceAgent& Agent : Agents)
  {
    const FVector2f Location = QuantizeVector(Agent.Location, Settings.PositionQuantumCm);
    const FVector2f Target = QuantizeVector(Agent.TargetLocation, Settings.PositionQuantumCm);
    const FVector2f Offset = Location - Target;
    const float Distance = Offset.Size();
    if (Distance > Agent.MaximumCombatCenterDistanceCm + Settings.InfluenceBlendWidthCm)
      continue;
    const int32 Radial = FMath::Clamp(FMath::FloorToInt(Distance / Settings.RadialBandWidthCm),
      0, OutField.RadialBandCount - 1);
    const int32 Angular = SectorForOffset(Offset, OutField.AngularSectorCount);
    ++OutField.Cells[Radial * OutField.AngularSectorCount + Angular].AgentCount;
    ++OutSummary.ContributingAgentCount;
    FieldHash = Fold(FieldHash, static_cast<uint32>(Agent.AgentId));
    FieldHash = Fold(FieldHash, static_cast<uint32>(Radial));
    FieldHash = Fold(FieldHash, static_cast<uint32>(Angular));
  }
  for (int32 Radial = 0; Radial < OutField.RadialBandCount; ++Radial)
  {
    for (int32 Angular = 0; Angular < OutField.AngularSectorCount; ++Angular)
    {
      const int32 Left = (Angular - 1 + OutField.AngularSectorCount) % OutField.AngularSectorCount;
      const int32 Right = (Angular + 1) % OutField.AngularSectorCount;
      auto& Cell = OutField.Cells[Radial * OutField.AngularSectorCount + Angular];
      Cell.SmoothedWeight =
        OutField.Cells[Radial * OutField.AngularSectorCount + Left].AgentCount
        + 2 * Cell.AgentCount
        + OutField.Cells[Radial * OutField.AngularSectorCount + Right].AgentCount;
    }
  }
  for (const auto& Cell : OutField.Cells)
  {
    OutSummary.OccupiedCellCount += Cell.AgentCount > 0 ? 1 : 0;
    OutSummary.MaximumCellPopulation = FMath::Max(
      OutSummary.MaximumCellPopulation, Cell.AgentCount);
    FieldHash = Fold(FieldHash, static_cast<uint32>(Cell.RadialBandIndex));
    FieldHash = Fold(FieldHash, static_cast<uint32>(Cell.AngularSectorIndex));
    FieldHash = Fold(FieldHash, static_cast<uint32>(Cell.AgentCount));
    FieldHash = Fold(FieldHash, static_cast<uint32>(Cell.SmoothedWeight));
  }
  OutField.FieldHash = FieldHash;
  OutField.bValid = true;
  OutSummary.FieldHash = FieldHash;
}

void FCrowdDemoTargetInfluenceKernel::Solve(
  TConstArrayView<FCrowdDemoTargetInfluenceAgent> AgentView,
  const FCrowdDemoTargetInfluenceSettings& Settings,
  TArray<FCrowdDemoTargetInfluenceResult>& OutResults,
  FCrowdDemoTargetInfluenceSummary& OutSummary)
{
  using namespace CrowdDemoTargetInfluencePrivate;
  OutResults.Reset();
  OutSummary = FCrowdDemoTargetInfluenceSummary();
  TArray<FCrowdDemoTargetInfluenceAgent> Agents(AgentView);
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  FCrowdDemoTargetDensityField DensityField;
  BuildPolarDensityField(Agents, Settings, DensityField, OutSummary.Density);
  bool bValid = AreSettingsValid(Settings) && DensityField.bValid;
  TArray<float> RadialErrors;
  TArray<float> RelativeSpeeds;
  TArray<float> FollowLags;
  TArray<float> TangentialSpeeds;
  TArray<int32> SectorPopulation;
  SectorPopulation.Init(0, FMath::Max(1, Settings.AngularSectorCount));
  TArray<bool> OccupiedRadialBands;
  OccupiedRadialBands.Init(false, DensityField.RadialBandCount);
  uint32 Hash = Fold(HashSettings(Settings), DensityField.FieldHash);
  int32 PreviousAgentId = INDEX_NONE;

  for (const FCrowdDemoTargetInfluenceAgent& Agent : Agents)
  {
    FCrowdDemoTargetInfluenceResult& Result = OutResults.AddDefaulted_GetRef();
    Result.AgentId = Agent.AgentId;
    const bool bAgentValid = Agent.AgentId != INDEX_NONE && Agent.AgentId != PreviousAgentId
      && Agent.MaxSpeedCmps >= 0.0f && Agent.PhysicalRadiusCm >= 0.0f
      && Agent.HardSafetyGapCm >= 0.0f && Agent.TargetPhysicalRadiusCm >= 0.0f
      && Agent.TargetHardSafetyGapCm >= 0.0f
      && Agent.MinimumCombatCenterDistanceCm >= 0.0f
      && Agent.MaximumCombatCenterDistanceCm >= 0.0f;
    PreviousAgentId = Agent.AgentId;
    const float TargetHardDistance = Agent.TargetPhysicalRadiusCm + Agent.PhysicalRadiusCm
      + FMath::Max(Agent.TargetHardSafetyGapCm, Agent.HardSafetyGapCm);
    Result.NormalizedMinimumDistanceCm = FMath::Max(
      Agent.MinimumCombatCenterDistanceCm, TargetHardDistance);
    const bool bDistanceValid = Agent.MaximumCombatCenterDistanceCm
      >= Result.NormalizedMinimumDistanceCm;
    const FVector2f QuantizedLocation = QuantizeVector(Agent.Location, Settings.PositionQuantumCm);
    const FVector2f QuantizedTarget = QuantizeVector(Agent.TargetLocation, Settings.PositionQuantumCm);
    const FVector2f Offset = QuantizedLocation - QuantizedTarget;
    Result.DistanceToTargetCm = Offset.Size();
    const FVector2f Normal = Result.DistanceToTargetCm > KINDA_SMALL_NUMBER
      ? Offset / Result.DistanceToTargetCm : StableDirectionFromAgentId(Agent.AgentId);
    Result.bInsideMinimum = Result.DistanceToTargetCm < Result.NormalizedMinimumDistanceCm;
    Result.bOutsideMaximum = Result.DistanceToTargetCm > Agent.MaximumCombatCenterDistanceCm;
    Result.bInsideEffectiveBand = !Result.bInsideMinimum && !Result.bOutsideMaximum;
    Result.RadialErrorCm = Result.bInsideMinimum
      ? Result.NormalizedMinimumDistanceCm - Result.DistanceToTargetCm
      : (Result.bOutsideMaximum ? Result.DistanceToTargetCm - Agent.MaximumCombatCenterDistanceCm : 0.0f);
    Result.FollowLagCm = Result.bOutsideMaximum ? Result.RadialErrorCm : 0.0f;
    const float RadialSpeed = FMath::Min(Result.RadialErrorCm * Settings.RadialGainPerSecond,
      Settings.MaxRadialSpeedCmps);
    Result.RadialCorrection = Result.bInsideMinimum ? Normal * RadialSpeed
      : (Result.bOutsideMaximum ? -Normal * RadialSpeed : FVector2f::ZeroVector);
    const float LinearWeight = FMath::Clamp(
      (Agent.MaximumCombatCenterDistanceCm + Settings.InfluenceBlendWidthCm
        - Result.DistanceToTargetCm) / Settings.InfluenceBlendWidthCm, 0.0f, 1.0f);
    const float SmoothWeight = LinearWeight * LinearWeight * (3.0f - 2.0f * LinearWeight);
    Result.InfluenceWeightQ15 = FMath::Clamp(FMath::RoundToInt(SmoothWeight * 32767.0f), 0, 32767);
    const float QuantizedWeight = static_cast<float>(Result.InfluenceWeightQ15) / 32767.0f;
    Result.RadialBandIndex = FMath::Clamp(FMath::FloorToInt(
      Result.DistanceToTargetCm / Settings.RadialBandWidthCm), 0, DensityField.RadialBandCount - 1);
    Result.AngularSectorIndex = SectorForOffset(Offset, Settings.AngularSectorCount);
    if (Result.bInsideEffectiveBand)
    {
      const int32 LeftSector = (Result.AngularSectorIndex - 1 + Settings.AngularSectorCount)
        % Settings.AngularSectorCount;
      const int32 RightSector = (Result.AngularSectorIndex + 1) % Settings.AngularSectorCount;
      const int32 Base = Result.RadialBandIndex * Settings.AngularSectorCount;
      const int32 Current = DensityField.Cells[Base + Result.AngularSectorIndex].SmoothedWeight;
      const int32 Left = DensityField.Cells[Base + LeftSector].SmoothedWeight;
      const int32 Right = DensityField.Cells[Base + RightSector].SmoothedWeight;
      Result.DensityLeftWeight = Left;
      Result.DensityCurrentWeight = Current;
      Result.DensityRightWeight = Right;
      int32 Chosen = Current;
      if (Left < Right && Left < Current) { Result.DensityDirectionSign = 1; Chosen = Left; }
      else if (Right < Left && Right < Current) { Result.DensityDirectionSign = -1; Chosen = Right; }
      else if (Left == Right && Left < Current)
      {
        Result.DensityDirectionSign = StableTieDirection(Agent.AgentId);
        Chosen = Left;
      }
      Result.DensityDifference = FMath::Max(0, Current - Chosen);
      const int32 Excess = FMath::Max(0,
        Result.DensityDifference - Settings.DensityMinimumDifference + 1);
      Result.TangentialSpeedCmps = FMath::Min(
        static_cast<float>(Excess) * Settings.DensitySpeedPerExcessAgentCmps,
        Settings.MaximumDensityTangentialSpeedCmps);
      if (Result.DensityDirectionSign != 0 && Result.TangentialSpeedCmps > 0.0f)
      {
        const FVector2f CounterClockwise(-Normal.Y, Normal.X);
        Result.DensityVelocity = CounterClockwise
          * static_cast<float>(Result.DensityDirectionSign) * Result.TangentialSpeedCmps;
        ++OutSummary.Density.DensityGuidedAgentCount;
        OutSummary.Density.ClockwiseAgentCount += Result.DensityDirectionSign < 0 ? 1 : 0;
        OutSummary.Density.CounterClockwiseAgentCount += Result.DensityDirectionSign > 0 ? 1 : 0;
      }
    }
    const FVector2f TargetPreferred = Agent.TargetVelocity + Result.RadialCorrection;
    const FVector2f BaseDesired = FMath::Lerp(
      Agent.FarFlowPreferredVelocity, TargetPreferred, QuantizedWeight);
    Result.DesiredVelocity = QuantizeWithinMagnitude(
      BaseDesired + Result.DensityVelocity * QuantizedWeight,
      Agent.MaxSpeedCmps, Settings.VelocityQuantumCmps);
    Result.RadialCorrection = QuantizeVector(Result.RadialCorrection, Settings.VelocityQuantumCmps);
    Result.DensityVelocity = QuantizeVector(Result.DensityVelocity, Settings.VelocityQuantumCmps);
    Result.TangentialSpeedCmps = Result.DensityVelocity.Size();
    Result.RelativeSpeedCmps = (Agent.Velocity - Agent.TargetVelocity).Size();
    Result.bValid = bValid && bAgentValid && bDistanceValid;
    bValid = bValid && Result.bValid;
    if (Agent.MinimumCombatCenterDistanceCm < Result.NormalizedMinimumDistanceCm)
      ++OutSummary.NormalizedMinimumRaisedCount;
    OutSummary.InfluenceAgentCount += Result.InfluenceWeightQ15 > 0 ? 1 : 0;
    OutSummary.InsideEffectiveBandCount += Result.bInsideEffectiveBand ? 1 : 0;
    OutSummary.OutsideMaximumCount += Result.bOutsideMaximum ? 1 : 0;
    OutSummary.InsideMinimumCount += Result.bInsideMinimum ? 1 : 0;
    RadialErrors.Add(Result.RadialErrorCm);
    RelativeSpeeds.Add(Result.RelativeSpeedCmps);
    FollowLags.Add(Result.FollowLagCm);
    TangentialSpeeds.Add(Result.TangentialSpeedCmps);
    ++SectorPopulation[Result.AngularSectorIndex];
    OccupiedRadialBands[Result.RadialBandIndex] = true;

    uint32 ResultHash = Fold(HashSettings(Settings), static_cast<uint32>(Agent.AgentId));
    const auto FoldVector = [&](const FVector2f& Value, float Quantum)
    {
      ResultHash = Fold(ResultHash, static_cast<uint32>(Quantize(Value.X, Quantum)));
      ResultHash = Fold(ResultHash, static_cast<uint32>(Quantize(Value.Y, Quantum)));
    };
    FoldVector(Agent.Location, Settings.PositionQuantumCm);
    FoldVector(Agent.Velocity, Settings.VelocityQuantumCmps);
    FoldVector(Agent.FarFlowPreferredVelocity, Settings.VelocityQuantumCmps);
    FoldVector(Agent.TargetLocation, Settings.PositionQuantumCm);
    FoldVector(Agent.TargetVelocity, Settings.VelocityQuantumCmps);
    ResultHash = Fold(ResultHash, Quantize(Agent.MaxSpeedCmps, Settings.VelocityQuantumCmps));
    ResultHash = Fold(ResultHash, Quantize(Agent.PhysicalRadiusCm, Settings.PositionQuantumCm));
    ResultHash = Fold(ResultHash, Quantize(Agent.HardSafetyGapCm, Settings.PositionQuantumCm));
    ResultHash = Fold(ResultHash, Quantize(Agent.TargetPhysicalRadiusCm, Settings.PositionQuantumCm));
    ResultHash = Fold(ResultHash, Quantize(Agent.TargetHardSafetyGapCm, Settings.PositionQuantumCm));
    ResultHash = Fold(ResultHash, Quantize(Agent.MinimumCombatCenterDistanceCm, Settings.PositionQuantumCm));
    ResultHash = Fold(ResultHash, Quantize(Agent.MaximumCombatCenterDistanceCm, Settings.PositionQuantumCm));
    ResultHash = Fold(ResultHash, static_cast<uint32>(Result.RadialBandIndex));
    ResultHash = Fold(ResultHash, static_cast<uint32>(Result.AngularSectorIndex));
    ResultHash = Fold(ResultHash, static_cast<uint32>(Result.DensityDirectionSign));
    ResultHash = Fold(ResultHash, static_cast<uint32>(Result.DensityDifference));
    ResultHash = Fold(ResultHash, static_cast<uint32>(Result.InfluenceWeightQ15));
    FoldVector(Result.RadialCorrection, Settings.VelocityQuantumCmps);
    FoldVector(Result.DensityVelocity, Settings.VelocityQuantumCmps);
    FoldVector(Result.DesiredVelocity, Settings.VelocityQuantumCmps);
    ResultHash = Fold(ResultHash, Result.bValid ? 1u : 0u);
    Result.StableHash = ResultHash;
    Hash = Fold(Hash, ResultHash);
  }

  OutSummary.AgentCount = Agents.Num();
  OutSummary.RadialErrorCmP50 = Percentile(RadialErrors, 0.50f);
  OutSummary.RadialErrorCmP95 = Percentile(RadialErrors, 0.95f);
  for (const float Value : RadialErrors)
    OutSummary.RadialErrorCmMax = FMath::Max(OutSummary.RadialErrorCmMax, Value);
  OutSummary.RelativeSpeedCmpsP95 = Percentile(RelativeSpeeds, 0.95f);
  OutSummary.FollowLagCmP95 = Percentile(FollowLags, 0.95f);
  OutSummary.Density.TangentialSpeedCmpsP95 = Percentile(TangentialSpeeds, 0.95f);
  for (const float Value : TangentialSpeeds)
    OutSummary.Density.MaximumTangentialSpeedCmps = FMath::Max(
      OutSummary.Density.MaximumTangentialSpeedCmps, Value);
  for (const int32 Population : SectorPopulation)
  {
    OutSummary.OccupiedAngularSectorCount += Population > 0 ? 1 : 0;
    OutSummary.MaxAngularSectorPopulation = FMath::Max(
      OutSummary.MaxAngularSectorPopulation, Population);
  }
  OutSummary.AngularCoverageQ15 = FMath::RoundToInt(
    static_cast<float>(OutSummary.OccupiedAngularSectorCount)
      / FMath::Max(1, Settings.AngularSectorCount) * 32767.0f);
  for (const bool bOccupied : OccupiedRadialBands)
    OutSummary.OccupiedRadialBandCount += bOccupied ? 1 : 0;
  OutSummary.Density.OccupiedAngularSectorCount = OutSummary.OccupiedAngularSectorCount;
  OutSummary.Density.MaximumAngularSectorPopulation = OutSummary.MaxAngularSectorPopulation;
  OutSummary.Density.LargestEmptySectorRun = LargestCircularEmptyRun(SectorPopulation);
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.OccupiedAngularSectorCount));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.OccupiedRadialBandCount));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.Density.DensityGuidedAgentCount));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.Density.ClockwiseAgentCount));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.Density.CounterClockwiseAgentCount));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.Density.LargestEmptySectorRun));
  OutSummary.StableHash = Hash;
  OutSummary.bValid = bValid && OutResults.Num() == Agents.Num();
}
