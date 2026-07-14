#include "Mass/CrowdDemoElasticCrowdKernel.h"

namespace
{
  uint32 Fold(uint32 Hash, const int32 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    return Hash * 16777619u;
  }

  int64 CellKey(const FIntPoint Cell)
  {
    return static_cast<int64>((static_cast<uint64>(static_cast<uint32>(Cell.X)) << 32)
      | static_cast<uint32>(Cell.Y));
  }

  FVector2f Quantize(const FVector2f Value, const float Quantum)
  {
    const float Q = FMath::Max(0.001f, Quantum);
    return FVector2f(FMath::RoundToFloat(Value.X / Q) * Q,
      FMath::RoundToFloat(Value.Y / Q) * Q);
  }

  FVector2f ClampMagnitude(const FVector2f Value, const float Maximum)
  {
    const float Size = Value.Size();
    return Size > Maximum && Size > UE_SMALL_NUMBER ? Value * (Maximum / Size) : Value;
  }

  FVector2f StablePairNormal(const int32 A, const int32 B)
  {
    const uint32 H = static_cast<uint32>(FMath::Min(A, B)) * 73856093u
      ^ static_cast<uint32>(FMath::Max(A, B)) * 19349663u;
    const float Angle = static_cast<float>(H % 4096u) * (2.0f * PI / 4096.0f);
    const FVector2f Base(FMath::Cos(Angle), FMath::Sin(Angle));
    return A < B ? Base : -Base;
  }

  FVector2f ClosestPoint(const FVector2f Point, const FVector2f Start, const FVector2f End)
  {
    const FVector2f Delta = End - Start;
    const float LengthSquared = Delta.SizeSquared();
    if (LengthSquared <= UE_SMALL_NUMBER) return Start;
    const float T = FMath::Clamp(FVector2f::DotProduct(Point - Start, Delta)
      / LengthSquared, 0.0f, 1.0f);
    return Start + Delta * T;
  }

  int32 EnvironmentScore(const FCrowdDemoElasticCrowdAgent& Agent,
    const FVector2f Direction, const FCrowdDemoElasticCrowdSettings& Settings,
    const FCrowdDemoElasticCrowdEnvironment& Environment)
  {
    const FVector Start(Agent.Position.X, Agent.Position.Y, 0.0f);
    const FVector Proposed = Start + FVector(Direction.X, Direction.Y, 0.0f)
      * FMath::Max(1.0f, Settings.PositionQuantumCm);
    int32 Score = 0;
    if (Environment.bValidateFlowAndObstacles)
    {
      const auto Constraint = FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
        Environment.FlowConfig, Start, Proposed, Settings.FixedStepSeconds,
        Environment.bConstrainToFlowBounds);
      const float Delta = (Constraint.Location - Proposed).Size2D();
      Score += Delta <= Settings.PositionQuantumCm ? 1000 : -FMath::RoundToInt(Delta);
    }
    if (Environment.bValidateTargetExclusion)
    {
      const FVector2f End(Proposed.X, Proposed.Y);
      const float Required = Environment.TargetExclusionRadiusCm + Agent.PhysicalRadiusCm;
      Score += (End - Environment.TargetLocation).Size() + Settings.PositionQuantumCm >= Required
        ? 100 : -1000;
    }
    return Score;
  }
}

bool FCrowdDemoElasticCrowdKernel::Solve(
  const TConstArrayView<FCrowdDemoElasticCrowdAgent> Agents,
  const FCrowdDemoElasticCrowdSettings& Settings,
  const FCrowdDemoElasticCrowdEnvironment& Environment,
  TArray<FCrowdDemoElasticCrowdResult>& OutResults,
  FCrowdDemoElasticCrowdSummary& OutSummary)
{
  OutResults.Reset();
  OutSummary = {};
  OutSummary.AgentCount = Agents.Num();
  if (Agents.IsEmpty()) { OutSummary.bValid = true; return true; }
  const float Scalars[] = {Settings.FixedStepSeconds, Settings.HardSafetyGapCm,
    Settings.PreferredSpacingGapCm, Settings.SpacingGainPerSecond,
    Settings.MaxSpacingResponseCmps, Settings.TransitHorizonSeconds,
    Settings.TransitInfluenceFalloffCm, Settings.TransitGainPerSecond,
    Settings.MaxTransitYieldSpeedCmps, Settings.PositionQuantumCm,
    Settings.VelocityQuantumCmps};
  for (const float Value : Scalars)
    if (!FMath::IsFinite(Value) || Value < 0.0f) { ++OutSummary.InvalidInputCount; return false; }

  TArray<FCrowdDemoElasticCrowdAgent> Sorted(Agents);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    auto& Agent = Sorted[Index];
    if (Agent.AgentId == INDEX_NONE || (Index > 0 && Sorted[Index - 1].AgentId == Agent.AgentId)
      || !FMath::IsFinite(Agent.Position.X) || !FMath::IsFinite(Agent.Position.Y)
      || !FMath::IsFinite(Agent.BasePreferredVelocity.X)
      || !FMath::IsFinite(Agent.BasePreferredVelocity.Y)
      || !FMath::IsFinite(Agent.PhysicalRadiusCm) || Agent.PhysicalRadiusCm <= 0.0f
      || !FMath::IsFinite(Agent.MaxSpeedCmps) || Agent.MaxSpeedCmps <= 0.0f)
    { ++OutSummary.InvalidInputCount; return false; }
    Agent.Position = Quantize(Agent.Position, Settings.PositionQuantumCm);
    Agent.BasePreferredVelocity = Quantize(
      ClampMagnitude(Agent.BasePreferredVelocity, Agent.MaxSpeedCmps),
      Settings.VelocityQuantumCmps);
    Agent.ContextScaleQ15 = FMath::Clamp(Agent.ContextScaleQ15, 0, 32767);
    if (Agent.bTransitSource) ++OutSummary.SourceCount;
  }

  OutResults.SetNum(Sorted.Num());
  TMap<int32, int32> IndexById;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    IndexById.Add(Sorted[Index].AgentId, Index);
    OutResults[Index].AgentId = Sorted[Index].AgentId;
    OutResults[Index].BasePreferredVelocity = Sorted[Index].BasePreferredVelocity;
  }

  TArray<TPair<int32, int32>> ActiveSpacingEdges;
  float MaximumRadius = 1.0f;
  for (const auto& Agent : Sorted) MaximumRadius = FMath::Max(MaximumRadius, Agent.PhysicalRadiusCm);
  const float CellSize = FMath::Max(1.0f, MaximumRadius * 2.0f
    + Settings.HardSafetyGapCm + Settings.PreferredSpacingGapCm);
  const auto MakeCell = [CellSize](const FVector2f P)
  { return FIntPoint(FMath::FloorToInt(P.X / CellSize), FMath::FloorToInt(P.Y / CellSize)); };
  TMap<int64, TArray<int32>> Grid;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
    Grid.FindOrAdd(CellKey(MakeCell(Sorted[Index].Position))).Add(Index);
  for (auto& Item : Grid) Item.Value.Sort([&](const int32 A, const int32 B)
    { return Sorted[A].AgentId < Sorted[B].AgentId; });

  for (int32 AIndex = 0; AIndex < Sorted.Num(); ++AIndex)
  {
    const auto& A = Sorted[AIndex];
    if (A.bTransitSource) continue;
    const FIntPoint Cell = MakeCell(A.Position);
    TArray<int64, TInlineAllocator<9>> Keys;
    for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
      Keys.Add(CellKey(Cell + FIntPoint(X, Y)));
    Keys.Sort();
    for (const int64 Key : Keys)
    {
      const TArray<int32>* Neighbors = Grid.Find(Key);
      if (!Neighbors) continue;
      for (const int32 BIndex : *Neighbors)
      {
        const auto& B = Sorted[BIndex];
        if (B.AgentId <= A.AgentId || B.bTransitSource) continue;
        FVector2f Delta = B.Position - A.Position;
        float Distance = Delta.Size();
        const float ContextScale = static_cast<float>(FMath::Min(
          A.ContextScaleQ15, B.ContextScaleQ15)) / 32767.0f;
        const float Preferred = A.PhysicalRadiusCm + B.PhysicalRadiusCm
          + Settings.HardSafetyGapCm + Settings.PreferredSpacingGapCm * ContextScale;
        const float Hard = A.PhysicalRadiusCm + B.PhysicalRadiusCm + Settings.HardSafetyGapCm;
        if (Distance < Hard - Settings.PositionQuantumCm) ++OutSummary.HardPairViolationCount;
        const float Deficit = Preferred - Distance;
        if (Deficit <= 0.0f) continue;
        if (Distance <= UE_SMALL_NUMBER) Delta = StablePairNormal(A.AgentId, B.AgentId);
        else Delta /= Distance;
        const float Speed = FMath::Min(Settings.MaxSpacingResponseCmps,
          Deficit * Settings.SpacingGainPerSecond);
        OutResults[AIndex].SpacingDeltaVelocity -= Delta * (Speed * 0.5f);
        OutResults[BIndex].SpacingDeltaVelocity += Delta * (Speed * 0.5f);
        ++OutResults[AIndex].SpacingNeighborCount;
        ++OutResults[BIndex].SpacingNeighborCount;
        OutResults[AIndex].MaxSpacingDeficitCm = FMath::Max(
          OutResults[AIndex].MaxSpacingDeficitCm, Deficit);
        OutResults[BIndex].MaxSpacingDeficitCm = FMath::Max(
          OutResults[BIndex].MaxSpacingDeficitCm, Deficit);
        OutSummary.MaxSpacingDeficitCm = FMath::Max(OutSummary.MaxSpacingDeficitCm, Deficit);
        ++OutSummary.SpacingPairCount;
        ActiveSpacingEdges.Emplace(A.AgentId, B.AgentId);
      }
    }
  }

  TArray<TPair<int32, int32>> InfluenceEdges;
  for (int32 SourceIndex = 0; SourceIndex < Sorted.Num(); ++SourceIndex)
  {
    const auto& Source = Sorted[SourceIndex];
    if (!Source.bTransitSource) continue;
    FVector2f SourceVelocity = Source.BasePreferredVelocity;
    if (!Source.TransitDirection.IsNearlyZero())
      SourceVelocity = Source.TransitDirection.GetSafeNormal() * Source.BasePreferredVelocity.Size();
    const FVector2f End = Source.Position + SourceVelocity * Settings.TransitHorizonSeconds;
    for (int32 AgentIndex = 0; AgentIndex < Sorted.Num(); ++AgentIndex)
    {
      auto& Agent = Sorted[AgentIndex];
      if (AgentIndex == SourceIndex || Agent.bTransitSource) continue;
      const FVector2f Nearest = ClosestPoint(Agent.Position, Source.Position, End);
      FVector2f Delta = Agent.Position - Nearest;
      float Distance = Delta.Size();
      const float ContextScale = static_cast<float>(Agent.ContextScaleQ15) / 32767.0f;
      const float Required = FMath::Max(Source.PhysicalRadiusCm, Source.TransitSourceRadiusCm)
        + Agent.PhysicalRadiusCm + Settings.HardSafetyGapCm
        + Settings.PreferredSpacingGapCm * ContextScale;
      const float InfluenceLimit = Required + Settings.TransitInfluenceFalloffCm;
      if (Distance >= InfluenceLimit) continue;
      const float Deficit = FMath::Max(0.0f, Required - Distance);
      FVector2f Forward = SourceVelocity.GetSafeNormal();
      if (Forward.IsNearlyZero()) Forward = FVector2f(1.0f, 0.0f);
      const FVector2f Left(-Forward.Y, Forward.X);
      const float LateralError = FVector2f::DotProduct(
        Agent.Position - Source.Position, Left);
      if (FMath::Abs(LateralError) <= Settings.PositionQuantumCm)
      {
        const int32 LeftScore = EnvironmentScore(Agent, Left, Settings, Environment);
        const int32 RightScore = EnvironmentScore(Agent, -Left, Settings, Environment);
        if (LeftScore != RightScore) Delta = LeftScore > RightScore ? Left : -Left;
        else
        {
          uint32 H = 2166136261u; H = Fold(H, Source.AgentId); H = Fold(H, Agent.AgentId);
          Delta = (H & 1u) == 0 ? Left : -Left;
        }
      }
      else Delta /= Distance;
      const float ResponseDepth = FMath::Max(0.0f, InfluenceLimit - Distance);
      const float Falloff = Settings.TransitInfluenceFalloffCm > UE_SMALL_NUMBER
        ? FMath::Clamp(ResponseDepth / Settings.TransitInfluenceFalloffCm, 0.0f, 1.0f)
        : 1.0f;
      const float Speed = FMath::Min(Settings.MaxTransitYieldSpeedCmps,
        ResponseDepth * Settings.TransitGainPerSecond * Falloff);
      OutResults[AgentIndex].TransitDeltaVelocity += Delta * Speed;
      ++OutResults[AgentIndex].TransitSourceCount;
      OutResults[AgentIndex].MaxTransitDeficitCm = FMath::Max(
        OutResults[AgentIndex].MaxTransitDeficitCm, Deficit);
      OutSummary.MaxTransitDeficitCm = FMath::Max(OutSummary.MaxTransitDeficitCm, Deficit);
      InfluenceEdges.Emplace(Source.AgentId, Agent.AgentId);
    }
  }

  TMap<int32, int32> Layers;
  TArray<int32> Frontier;
  for (const auto& Edge : InfluenceEdges)
    if (!Layers.Contains(Edge.Value)) { Layers.Add(Edge.Value, 1); Frontier.Add(Edge.Value); }
  for (int32 Layer = 2; Layer <= 3 && !Frontier.IsEmpty(); ++Layer)
  {
    TArray<int32> Next;
    Frontier.Sort();
    for (const int32 Current : Frontier)
      for (const auto& Edge : ActiveSpacingEdges)
      {
        const int32 Other = Edge.Key == Current ? Edge.Value : (Edge.Value == Current ? Edge.Key : INDEX_NONE);
        if (Other != INDEX_NONE && !Layers.Contains(Other)) { Layers.Add(Other, Layer); Next.Add(Other); }
      }
    Frontier = MoveTemp(Next);
  }

  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, FMath::RoundToInt(Settings.FixedStepSeconds * 1000000.0f));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.HardSafetyGapCm));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.PreferredSpacingGapCm));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.SpacingGainPerSecond * 1000.0f));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.MaxSpacingResponseCmps));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.TransitHorizonSeconds * 1000.0f));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.TransitInfluenceFalloffCm));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.TransitGainPerSecond * 1000.0f));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.MaxTransitYieldSpeedCmps));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.PositionQuantumCm * 1000.0f));
  Hash = Fold(Hash, FMath::RoundToInt(Settings.VelocityQuantumCmps * 1000.0f));
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    auto& Result = OutResults[Index];
    Result.SpacingDeltaVelocity = Quantize(ClampMagnitude(Result.SpacingDeltaVelocity,
      Settings.MaxSpacingResponseCmps), Settings.VelocityQuantumCmps);
    Result.TransitDeltaVelocity = Quantize(ClampMagnitude(Result.TransitDeltaVelocity,
      Settings.MaxTransitYieldSpeedCmps), Settings.VelocityQuantumCmps);
    Result.AdjustedPreferredVelocity = Quantize(ClampMagnitude(Result.BasePreferredVelocity
      + Result.SpacingDeltaVelocity + Result.TransitDeltaVelocity,
      Sorted[Index].MaxSpeedCmps), Settings.VelocityQuantumCmps);
    Result.PropagationLayer = Layers.FindRef(Result.AgentId);
    const float Response = (Result.AdjustedPreferredVelocity - Result.BasePreferredVelocity).Size();
    if (Response > Settings.VelocityQuantumCmps * 0.5f) ++OutSummary.InfluencedAgentCount;
    OutSummary.PropagationLayerCount = FMath::Max(OutSummary.PropagationLayerCount,
      Result.PropagationLayer);
    OutSummary.MaxResponseSpeedCmps = FMath::Max(OutSummary.MaxResponseSpeedCmps, Response);
    const FVector Start(Sorted[Index].Position.X, Sorted[Index].Position.Y, 0.0f);
    const FVector Proposed = Start + FVector(Result.AdjustedPreferredVelocity.X,
      Result.AdjustedPreferredVelocity.Y, 0.0f) * Settings.FixedStepSeconds;
    if (Environment.bValidateFlowAndObstacles)
    {
      const auto Constraint = FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
        Environment.FlowConfig, Start, Proposed, Settings.FixedStepSeconds,
        Environment.bConstrainToFlowBounds);
      OutSummary.ObstacleViolationCount += Constraint.bHitObstacle ? 1 : 0;
      OutSummary.FlowBoundsViolationCount += Constraint.bHitFlowBounds ? 1 : 0;
    }
    if (Environment.bValidateTargetExclusion)
    {
      const FVector2f EndPosition(Proposed.X, Proposed.Y);
      const float Required = Environment.TargetExclusionRadiusCm + Sorted[Index].PhysicalRadiusCm;
      OutSummary.TargetViolationCount += (EndPosition - Environment.TargetLocation).Size()
        + Settings.PositionQuantumCm < Required ? 1 : 0;
    }
    Hash = Fold(Hash, Result.AgentId);
    Hash = Fold(Hash, FMath::RoundToInt(Sorted[Index].Position.X));
    Hash = Fold(Hash, FMath::RoundToInt(Sorted[Index].Position.Y));
    Hash = Fold(Hash, FMath::RoundToInt(Result.AdjustedPreferredVelocity.X));
    Hash = Fold(Hash, FMath::RoundToInt(Result.AdjustedPreferredVelocity.Y));
    Hash = Fold(Hash, Result.SpacingNeighborCount);
    Hash = Fold(Hash, Result.TransitSourceCount);
    Hash = Fold(Hash, Result.PropagationLayer);
  }
  Hash = Fold(Hash, OutSummary.SourceCount);
  Hash = Fold(Hash, OutSummary.SpacingPairCount);
  OutSummary.StableHash = Hash;
  OutSummary.bValid = true;
  return true;
}
