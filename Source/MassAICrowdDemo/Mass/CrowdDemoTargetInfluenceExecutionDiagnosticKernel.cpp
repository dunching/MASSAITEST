#include "Mass/CrowdDemoTargetInfluenceExecutionDiagnosticKernel.h"

#include "Algo/Sort.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;
  constexpr float RequestedThresholdCmps = 1.0f;

  uint32 Fold(uint32 Hash, const int32 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    return Hash * FnvPrime;
  }

  int32 Q(const float Value, const float Quantum = 0.01f)
  {
    return FMath::RoundToInt(Value / FMath::Max(Quantum, UE_SMALL_NUMBER));
  }

  float Percentile(TArray<float> Values, const float Alpha)
  {
    if (Values.IsEmpty()) return 0.0f;
    Values.Sort();
    const int32 Index = FMath::Clamp(
      FMath::CeilToInt(Alpha * static_cast<float>(Values.Num())) - 1, 0, Values.Num() - 1);
    return Values[Index];
  }

  float Maximum(const TConstArrayView<float> Values)
  {
    float Result = 0.0f;
    for (const float Value : Values) Result = FMath::Max(Result, Value);
    return Result;
  }

  FVector2f StableNormal(const FCrowdDemoTargetInfluenceExecutionSample& Sample)
  {
    const FVector2f Offset = Sample.Location - Sample.TargetLocation;
    if (Offset.SizeSquared() > UE_SMALL_NUMBER) return Offset.GetSafeNormal();
    const uint32 Seed = static_cast<uint32>(Sample.AgentId) * 2654435761u;
    const float Angle = static_cast<float>(Seed & 0xffffu) * (2.0f * PI / 65536.0f);
    return FVector2f(FMath::Cos(Angle), FMath::Sin(Angle));
  }

  int32 SignedDirection(const float Value)
  {
    return Value > RequestedThresholdCmps ? 1 : (Value < -RequestedThresholdCmps ? -1 : 0);
  }

  FCrowdDemoTargetInfluenceExecutionAgentRuntime* FindAgent(
    TArray<FCrowdDemoTargetInfluenceExecutionAgentRuntime>& Agents, const int32 AgentId)
  {
    return Agents.FindByPredicate([AgentId](const auto& Agent) { return Agent.AgentId == AgentId; });
  }
}

void FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::BuildEnvironmentFeasibility(
  const FVector2f& TargetLocation,
  const int32 AngularSectorCount,
  const int32 RadialBandCount,
  const float RadialBandWidthCm,
  const float HardClearanceCm,
  const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
  const TConstArrayView<int32> OccupiedCellKeys,
  FCrowdDemoTargetPolarEnvironmentSummary& OutSummary)
{
  OutSummary = FCrowdDemoTargetPolarEnvironmentSummary();
  if (AngularSectorCount < 1 || RadialBandCount < 1 || RadialBandWidthCm <= 0.0f
    || HardClearanceCm < 0.0f)
    return;

  TArray<int32> Occupied(OccupiedCellKeys);
  Occupied.Sort();
  for (int32 Index = Occupied.Num() - 1; Index > 0; --Index)
    if (Occupied[Index] == Occupied[Index - 1]) Occupied.RemoveAt(Index);

  TArray<FCrowdDemoSharedFlowObstacleSpec> Obstacles = FlowConfig.ObstacleSpecs;
  Obstacles.Sort([](const auto& A, const auto& B) { return A.ObstacleId < B.ObstacleId; });
  OutSummary.FeasibleSectorCountByRadialBand.Init(0, RadialBandCount);
  OutSummary.Cells.Reserve(AngularSectorCount * RadialBandCount);
  uint32 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, AngularSectorCount);
  Hash = Fold(Hash, RadialBandCount);
  Hash = Fold(Hash, Q(RadialBandWidthCm));
  Hash = Fold(Hash, Q(HardClearanceCm));

  for (int32 Band = 0; Band < RadialBandCount; ++Band)
  {
    for (int32 Sector = 0; Sector < AngularSectorCount; ++Sector)
    {
      const float Angle = (static_cast<float>(Sector) + 0.5f)
        * (2.0f * PI / static_cast<float>(AngularSectorCount));
      const float Radius = (static_cast<float>(Band) + 0.5f) * RadialBandWidthCm;
      const FVector2f Center = TargetLocation
        + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
      FCrowdDemoTargetPolarEnvironmentCell& Cell = OutSummary.Cells.AddDefaulted_GetRef();
      Cell.RadialBandIndex = Band;
      Cell.AngularSectorIndex = Sector;
      const int32 CellKey = Band * AngularSectorCount + Sector;
      Cell.bOccupied = Occupied.Contains(CellKey);
      Cell.bFlowBoundsBlocked = Center.X < FlowConfig.BoundsMin.X + HardClearanceCm
        || Center.X > FlowConfig.BoundsMax.X - HardClearanceCm
        || Center.Y < FlowConfig.BoundsMin.Y + HardClearanceCm
        || Center.Y > FlowConfig.BoundsMax.Y - HardClearanceCm;
      for (const FCrowdDemoSharedFlowObstacleSpec& Obstacle : Obstacles)
      {
        const float MinX = Obstacle.Center.X - Obstacle.Extent.X - HardClearanceCm;
        const float MaxX = Obstacle.Center.X + Obstacle.Extent.X + HardClearanceCm;
        const float MinY = Obstacle.Center.Y - Obstacle.Extent.Y - HardClearanceCm;
        const float MaxY = Obstacle.Center.Y + Obstacle.Extent.Y + HardClearanceCm;
        if (Center.X >= MinX && Center.X <= MaxX && Center.Y >= MinY && Center.Y <= MaxY)
        {
          Cell.bObstacleBlocked = true;
          break;
        }
      }
      Cell.bFeasible = !Cell.bFlowBoundsBlocked && !Cell.bObstacleBlocked;
      OutSummary.FlowBoundsInfeasibleCellCount += Cell.bFlowBoundsBlocked ? 1 : 0;
      OutSummary.ObstacleInfeasibleCellCount += Cell.bObstacleBlocked ? 1 : 0;
      OutSummary.FeasibleSectorCountByRadialBand[Band] += Cell.bFeasible ? 1 : 0;
      OutSummary.OccupiedFeasibleSectorCount += Cell.bOccupied && Cell.bFeasible ? 1 : 0;
      OutSummary.OccupiedInfeasiblePolarCellCount += Cell.bOccupied && !Cell.bFeasible ? 1 : 0;
      OutSummary.FeasibleButUnoccupiedSectorCount += !Cell.bOccupied && Cell.bFeasible ? 1 : 0;
      Hash = Fold(Hash, Band);
      Hash = Fold(Hash, Sector);
      Hash = Fold(Hash, Q(Center.X));
      Hash = Fold(Hash, Q(Center.Y));
      Hash = Fold(Hash, Cell.bFeasible ? 1 : 0);
      Hash = Fold(Hash, Cell.bFlowBoundsBlocked ? 1 : 0);
      Hash = Fold(Hash, Cell.bObstacleBlocked ? 1 : 0);
      Hash = Fold(Hash, Cell.bOccupied ? 1 : 0);
    }

    int32 BestRun = 0;
    int32 Run = 0;
    for (int32 Offset = 0; Offset < AngularSectorCount * 2; ++Offset)
    {
      const auto& Cell = OutSummary.Cells[Band * AngularSectorCount + Offset % AngularSectorCount];
      Run = Cell.bFeasible && !Cell.bOccupied ? Run + 1 : 0;
      BestRun = FMath::Max(BestRun, FMath::Min(Run, AngularSectorCount));
    }
    OutSummary.LargestEmptyFeasibleSectorRun = FMath::Max(
      OutSummary.LargestEmptyFeasibleSectorRun, BestRun);
  }
  OutSummary.bValid = true;
  OutSummary.StableHash = Hash;
}

void FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::RecordStep(
  const TConstArrayView<FCrowdDemoTargetInfluenceExecutionSample> InputSamples,
  const FCrowdDemoTargetPolarEnvironmentSummary& Environment,
  FCrowdDemoTargetInfluenceExecutionRuntime& Runtime)
{
  TArray<FCrowdDemoTargetInfluenceExecutionSample> Samples(InputSamples);
  Samples.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  Runtime.Environment = Environment;
  uint32 Hash = Fold(Runtime.RollingHash, Environment.StableHash);
  int32 PreviousAgentId = INDEX_NONE;
  for (const auto& Sample : Samples)
  {
    if (Sample.AgentId <= PreviousAgentId || Sample.FixedStepSeconds <= 0.0f) continue;
    PreviousAgentId = Sample.AgentId;
    FCrowdDemoTargetInfluenceExecutionAgentRuntime* Agent = FindAgent(Runtime.Agents, Sample.AgentId);
    if (!Agent)
    {
      Agent = &Runtime.Agents.AddDefaulted_GetRef();
      Agent->AgentId = Sample.AgentId;
      Runtime.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
      Agent = FindAgent(Runtime.Agents, Sample.AgentId);
    }
    const FVector2f Radial = StableNormal(Sample);
    const FVector2f Tangent(-Radial.Y, Radial.X);
    const float RequestedSigned = FVector2f::DotProduct(Sample.DensityRequestedVelocity, Tangent);
    const float PredictedSigned = FVector2f::DotProduct(Sample.MovementPredictVelocity, Tangent);
    const float AppliedSigned = FVector2f::DotProduct(Sample.AppliedVelocity, Tangent);
    const int32 RequestSign = SignedDirection(RequestedSigned);
    const float Requested = FMath::Abs(RequestedSigned);
    const float Predicted = FMath::Abs(PredictedSigned);
    const float AppliedSameDirection = RequestSign == 0 ? 0.0f
      : FMath::Max(0.0f, AppliedSigned * static_cast<float>(RequestSign));
    const float Lost = FMath::Max(0.0f, Requested - AppliedSameDirection);
    const FVector2f EnvironmentDelta = Sample.EnvironmentSoftCorrection + Sample.FinalSafetyCorrection;
    const float EnvironmentOpposed = RequestSign == 0 ? 0.0f : FMath::Max(0.0f,
      -FVector2f::DotProduct(EnvironmentDelta, Tangent) * static_cast<float>(RequestSign)
        / Sample.FixedStepSeconds);
    const FVector2f ParticleDelta = Sample.PairSoftCorrection + Sample.UnifiedHardCorrection;
    const float ParticleOpposed = RequestSign == 0 ? 0.0f : FMath::Max(0.0f,
      -FVector2f::DotProduct(ParticleDelta, Tangent) * static_cast<float>(RequestSign)
        / Sample.FixedStepSeconds);

    ++Runtime.ValidSampleCount;
    ++Agent->ValidSampleCount;
    if (Requested < RequestedThresholdCmps)
      ++Runtime.RequestedBelowThresholdSampleCount;
    else
    {
      ++Agent->RequestedSampleCount;
      Runtime.RequestedTangentialSamples.Add(Requested);
      Runtime.PredictedTangentialSamples.Add(Predicted);
      Runtime.AppliedTangentialSamples.Add(AppliedSameDirection);
      Runtime.LostTangentialSamples.Add(Lost);
      Agent->RequestedTangentialCmpsQ += Q(Requested);
      Agent->PredictedTangentialCmpsQ += Q(Predicted);
      Agent->AppliedSameDirectionCmpsQ += Q(AppliedSameDirection);
      Agent->LostTangentialCmpsQ += Q(Lost);
      Agent->EnvironmentOpposedCmpsQ += Q(FMath::Min(EnvironmentOpposed, Lost));
      Agent->ParticleOpposedCmpsQ += Q(FMath::Min(ParticleOpposed, Lost));
      if (Agent->PreviousDirectionSign != 0 && RequestSign != 0
        && Agent->PreviousDirectionSign != RequestSign)
      {
        ++Agent->DirectionFlipCount;
        ++Runtime.DirectionFlipCount;
      }
      Agent->PreviousDirectionSign = RequestSign;
    }
    if (Agent->PreviousAngularSector != INDEX_NONE
      && Agent->PreviousAngularSector != Sample.AngularSectorIndex)
    {
      ++Agent->AngularSectorTransitionCount;
      ++Runtime.AngularSectorTransitionCount;
    }
    if (Agent->PreviousRadialBand != INDEX_NONE
      && Agent->PreviousRadialBand != Sample.RadialBandIndex)
    {
      ++Agent->RadialBandTransitionCount;
      ++Runtime.RadialBandTransitionCount;
    }
    Agent->PreviousAngularSector = Sample.AngularSectorIndex;
    Agent->PreviousRadialBand = Sample.RadialBandIndex;
    Agent->LastRadialBand = Sample.RadialBandIndex;
    Agent->LastAngularSector = Sample.AngularSectorIndex;
    Agent->LastDirectionSign = Sample.DensityDirectionSign;
    Agent->LastLeftWeight = Sample.DensityLeftWeight;
    Agent->LastCurrentWeight = Sample.DensityCurrentWeight;
    Agent->LastRightWeight = Sample.DensityRightWeight;
    Agent->LastRequestedTangentialCmps = RequestedSigned;
    Agent->LastPredictedTangentialCmps = PredictedSigned;
    Agent->LastAppliedTangentialCmps = AppliedSigned;
    const int32 CellKey = Sample.RadialBandIndex * FMath::Max(1, Environment.FeasibleSectorCountByRadialBand.Num())
      + Sample.AngularSectorIndex;
    const auto* Cell = Environment.Cells.FindByPredicate([&](const auto& Candidate)
    {
      return Candidate.RadialBandIndex == Sample.RadialBandIndex
        && Candidate.AngularSectorIndex == Sample.AngularSectorIndex;
    });
    Agent->bLastCellFeasible = Cell && Cell->bFeasible;

    Hash = Fold(Hash, Sample.AgentId);
    Hash = Fold(Hash, Sample.TargetRevision);
    Hash = Fold(Hash, Sample.FixedStepIndex);
    Hash = Fold(Hash, Q(RequestedSigned));
    Hash = Fold(Hash, Q(PredictedSigned));
    Hash = Fold(Hash, Q(AppliedSigned));
    Hash = Fold(Hash, Q(EnvironmentOpposed));
    Hash = Fold(Hash, Q(ParticleOpposed));
    Hash = Fold(Hash, Sample.RadialBandIndex);
    Hash = Fold(Hash, Sample.AngularSectorIndex);
    Hash = Fold(Hash, Sample.DensityDirectionSign);
    Hash = Fold(Hash, Sample.DensityLeftWeight);
    Hash = Fold(Hash, Sample.DensityCurrentWeight);
    Hash = Fold(Hash, Sample.DensityRightWeight);
    (void)CellKey;
  }
  Runtime.RollingHash = Hash;
}

void FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::BuildSummary(
  const FCrowdDemoTargetInfluenceExecutionRuntime& Runtime,
  FCrowdDemoTargetInfluenceExecutionSummary& OutSummary)
{
  OutSummary = FCrowdDemoTargetInfluenceExecutionSummary();
  OutSummary.bValid = Runtime.ValidSampleCount > 0 && Runtime.Environment.bValid;
  OutSummary.ValidSampleCount = Runtime.ValidSampleCount;
  OutSummary.RequestedBelowThresholdSampleCount = Runtime.RequestedBelowThresholdSampleCount;
  OutSummary.RequestedTangentialCmpsP50 = Percentile(Runtime.RequestedTangentialSamples, 0.50f);
  OutSummary.RequestedTangentialCmpsP95 = Percentile(Runtime.RequestedTangentialSamples, 0.95f);
  OutSummary.RequestedTangentialCmpsMax = Maximum(Runtime.RequestedTangentialSamples);
  OutSummary.MovementPredictTangentialCmpsP50 = Percentile(Runtime.PredictedTangentialSamples, 0.50f);
  OutSummary.MovementPredictTangentialCmpsP95 = Percentile(Runtime.PredictedTangentialSamples, 0.95f);
  OutSummary.MovementPredictTangentialCmpsMax = Maximum(Runtime.PredictedTangentialSamples);
  OutSummary.AppliedTangentialCmpsP50 = Percentile(Runtime.AppliedTangentialSamples, 0.50f);
  OutSummary.AppliedTangentialCmpsP95 = Percentile(Runtime.AppliedTangentialSamples, 0.95f);
  OutSummary.AppliedTangentialCmpsMax = Maximum(Runtime.AppliedTangentialSamples);
  OutSummary.LostTangentialCmpsP50 = Percentile(Runtime.LostTangentialSamples, 0.50f);
  OutSummary.LostTangentialCmpsP95 = Percentile(Runtime.LostTangentialSamples, 0.95f);
  OutSummary.LostTangentialCmpsMax = Maximum(Runtime.LostTangentialSamples);
  OutSummary.DirectionFlipCount = Runtime.DirectionFlipCount;
  OutSummary.AngularSectorTransitionCount = Runtime.AngularSectorTransitionCount;
  OutSummary.RadialBandTransitionCount = Runtime.RadialBandTransitionCount;
  TArray<float> Ratios;
  for (const auto& Agent : Runtime.Agents)
  {
    if (Agent.RequestedSampleCount <= 0 || Agent.RequestedTangentialCmpsQ <= 0) continue;
    ++OutSummary.RequestedAgentCount;
    Ratios.Add(static_cast<float>(Agent.AppliedSameDirectionCmpsQ)
      / static_cast<float>(Agent.RequestedTangentialCmpsQ));
    OutSummary.DirectionFlipAgentCount += Agent.DirectionFlipCount > 0 ? 1 : 0;
    OutSummary.EnvironmentOpposedAgentCount += Agent.EnvironmentOpposedCmpsQ * 2
      >= Agent.RequestedTangentialCmpsQ ? 1 : 0;
    OutSummary.ParticleOpposedAgentCount += Agent.ParticleOpposedCmpsQ * 2
      >= Agent.RequestedTangentialCmpsQ ? 1 : 0;
  }
  OutSummary.RequestedToAppliedRatioP50 = Percentile(Ratios, 0.50f);
  OutSummary.RequestedToAppliedRatioP95 = Percentile(Ratios, 0.95f);
  OutSummary.Environment = Runtime.Environment;
  OutSummary.DiagnosticHash = Fold(Runtime.RollingHash, Runtime.Environment.StableHash);
}

FCrowdDemoTargetInfluenceExecutionCheckpoint
FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::MakeCheckpoint(
  const FCrowdDemoTargetInfluenceExecutionRuntime& Runtime)
{
  FCrowdDemoTargetInfluenceExecutionCheckpoint Result;
  Result.Agents = Runtime.Agents;
  Result.RequestedSampleCount = Runtime.RequestedTangentialSamples.Num();
  Result.PredictedSampleCount = Runtime.PredictedTangentialSamples.Num();
  Result.AppliedSampleCount = Runtime.AppliedTangentialSamples.Num();
  Result.LostSampleCount = Runtime.LostTangentialSamples.Num();
  Result.ValidSampleCount = Runtime.ValidSampleCount;
  Result.RequestedBelowThresholdSampleCount = Runtime.RequestedBelowThresholdSampleCount;
  Result.AngularSectorTransitionCount = Runtime.AngularSectorTransitionCount;
  Result.RadialBandTransitionCount = Runtime.RadialBandTransitionCount;
  Result.DirectionFlipCount = Runtime.DirectionFlipCount;
  Result.RollingHash = Runtime.RollingHash;
  Result.Environment = Runtime.Environment;
  return Result;
}

void FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::RestoreCheckpoint(
  const FCrowdDemoTargetInfluenceExecutionCheckpoint& Checkpoint,
  FCrowdDemoTargetInfluenceExecutionRuntime& Runtime)
{
  Runtime.Agents = Checkpoint.Agents;
  Runtime.RequestedTangentialSamples.SetNum(FMath::Min(
    Checkpoint.RequestedSampleCount, Runtime.RequestedTangentialSamples.Num()));
  Runtime.PredictedTangentialSamples.SetNum(FMath::Min(
    Checkpoint.PredictedSampleCount, Runtime.PredictedTangentialSamples.Num()));
  Runtime.AppliedTangentialSamples.SetNum(FMath::Min(
    Checkpoint.AppliedSampleCount, Runtime.AppliedTangentialSamples.Num()));
  Runtime.LostTangentialSamples.SetNum(FMath::Min(
    Checkpoint.LostSampleCount, Runtime.LostTangentialSamples.Num()));
  Runtime.ValidSampleCount = Checkpoint.ValidSampleCount;
  Runtime.RequestedBelowThresholdSampleCount = Checkpoint.RequestedBelowThresholdSampleCount;
  Runtime.AngularSectorTransitionCount = Checkpoint.AngularSectorTransitionCount;
  Runtime.RadialBandTransitionCount = Checkpoint.RadialBandTransitionCount;
  Runtime.DirectionFlipCount = Checkpoint.DirectionFlipCount;
  Runtime.RollingHash = Checkpoint.RollingHash;
  Runtime.Environment = Checkpoint.Environment;
}
