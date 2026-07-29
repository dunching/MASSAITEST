#include "CrowdLocalPredictiveInteractionKernel.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"

namespace
{
struct FGridRecord
{
  int32 X = 0;
  int32 Y = 0;
  int32 AgentIndex = INDEX_NONE;
  int32 AgentId = INDEX_NONE;
};

struct FComponent
{
  uint32 Key = 2166136261u;
  TArray<int32> AgentIndices;
  int32 GrantedAgentId = INDEX_NONE;
  int32 GrantEpoch = 0;
  int32 RemainingSteps = 0;
};

struct FEnvironmentHit
{
  bool bHit = false;
  int32 ObstacleId = INDEX_NONE;
  int32 Face = INDEX_NONE;
  float EntryT = 1.0f;
  FVector2f Normal = FVector2f::ZeroVector;
  float BoundaryCoordinate = 0.0f;
};

uint32 HashInt(uint32 Hash, const int32 Value)
{
  Hash ^= static_cast<uint32>(Value);
  return Hash * 16777619u;
}

uint32 HashFloat(uint32 Hash, const float Value, const float Quantum)
{
  const float SafeQuantum = FMath::Max(0.0001f, Quantum);
  return HashInt(Hash, FMath::RoundToInt(Value / SafeQuantum));
}

FVector2f Quantize(const FVector2f Value, const float Quantum)
{
  const float SafeQuantum = FMath::Max(0.001f, Quantum);
  return FVector2f(
    FMath::RoundToFloat(Value.X / SafeQuantum) * SafeQuantum,
    FMath::RoundToFloat(Value.Y / SafeQuantum) * SafeQuantum);
}

FVector2f NormalizeQ15(const FVector2f Value)
{
  if (Value.SizeSquared() <= KINDA_SMALL_NUMBER) return FVector2f(1.0f, 0.0f);
  const FVector2f Unit = Value.GetSafeNormal();
  const FVector2f Quantized(
    FMath::RoundToFloat(Unit.X * 32767.0f) / 32767.0f,
    FMath::RoundToFloat(Unit.Y * 32767.0f) / 32767.0f);
  return Quantized.GetSafeNormal();
}

FVector2f StablePairDirection(const int32 AgentId, const int32 OtherAgentId)
{
  const int32 MinId = FMath::Min(AgentId, OtherAgentId);
  const int32 MaxId = FMath::Max(AgentId, OtherAgentId);
  const uint32 Hash = static_cast<uint32>(MinId) * 73856093u
    ^ static_cast<uint32>(MaxId) * 19349663u;
  const float Angle = static_cast<float>(Hash % 4096u) * (2.0f * PI / 4096.0f);
  const FVector2f Base(FMath::Cos(Angle), FMath::Sin(Angle));
  return AgentId == MinId ? Base : -Base;
}

float Determinant(const FVector2f A, const FVector2f B)
{
  return A.X * B.Y - A.Y * B.X;
}

float RequiredSeparation(
  const FCrowdLocalPredictiveAgent& A,
  const FCrowdLocalPredictiveAgent& B)
{
  return A.PhysicalRadiusCm + B.PhysicalRadiusCm
    + FMath::Max(A.HardSafetyGapCm, B.HardSafetyGapCm);
}

float PairQuantizationSafetyMargin(
  const FCrowdLocalPredictiveSettings& Settings)
{
  return 2.0f * FMath::Max(0.0f, Settings.VelocityQuantumCmps)
    * FMath::Max(Settings.FixedStepSeconds, Settings.TimeHorizonSeconds);
}

void ClosestApproach(
  const FCrowdLocalPredictiveAgent& A,
  const FCrowdLocalPredictiveAgent& B,
  const float Horizon,
  float& OutTime,
  float& OutDistance)
{
  const FVector2f RelativePosition = B.Position - A.Position;
  const FVector2f RelativeVelocity = B.PreferredVelocity - A.PreferredVelocity;
  const float RelativeSpeedSquared = RelativeVelocity.SizeSquared();
  OutTime = RelativeSpeedSquared > KINDA_SMALL_NUMBER
    ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
        / RelativeSpeedSquared, 0.0f, Horizon)
    : 0.0f;
  OutDistance = (RelativePosition + RelativeVelocity * OutTime).Size();
}

bool BuildPairHalfPlane(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdLocalPredictiveAgent& Other,
  const float Responsibility,
  const FCrowdLocalPredictiveSettings& Settings,
  const int32 StableOrder,
  FCrowdVelocityHalfPlane& OutHalfPlane)
{
  const FVector2f AgentPosition = Quantize(Agent.Position, 1.0f);
  const FVector2f OtherPosition = Quantize(Other.Position, 1.0f);
  const FVector2f AgentVelocity = Quantize(Agent.Velocity, Settings.VelocityQuantumCmps);
  const FVector2f OtherVelocity = Quantize(Other.Velocity, Settings.VelocityQuantumCmps);
  FVector2f RelativePosition = OtherPosition - AgentPosition;
  const FVector2f RelativeVelocity = AgentVelocity - OtherVelocity;
  float DistanceSquared = RelativePosition.SizeSquared();
  const float CombinedRadius = RequiredSeparation(Agent, Other)
    + PairQuantizationSafetyMargin(Settings);
  const float CombinedRadiusSquared = FMath::Square(CombinedRadius);
  const float Horizon = FMath::Max(Settings.FixedStepSeconds, Settings.TimeHorizonSeconds);
  FVector2f LineDirection = FVector2f::ZeroVector;
  FVector2f Correction = FVector2f::ZeroVector;

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
        ? W / WLength : StablePairDirection(Agent.AgentId, Other.AgentId);
      LineDirection = FVector2f(UnitW.Y, -UnitW.X);
      Correction = UnitW * (CombinedRadius * InverseHorizon - WLength);
    }
    else
    {
      const float Leg = FMath::Sqrt(FMath::Max(0.0f, DistanceSquared - CombinedRadiusSquared));
      if (Determinant(RelativePosition, W) > 0.0f)
      {
        LineDirection = FVector2f(
          RelativePosition.X * Leg - RelativePosition.Y * CombinedRadius,
          RelativePosition.X * CombinedRadius + RelativePosition.Y * Leg) / DistanceSquared;
      }
      else
      {
        LineDirection = -FVector2f(
          RelativePosition.X * Leg + RelativePosition.Y * CombinedRadius,
          -RelativePosition.X * CombinedRadius + RelativePosition.Y * Leg) / DistanceSquared;
      }
      Correction = LineDirection
        * FVector2f::DotProduct(RelativeVelocity, LineDirection) - RelativeVelocity;
    }
  }
  else
  {
    if (DistanceSquared <= KINDA_SMALL_NUMBER)
    {
      RelativePosition = StablePairDirection(Agent.AgentId, Other.AgentId);
      DistanceSquared = 0.0f;
    }
    const float InverseStep = 1.0f / FMath::Max(Settings.FixedStepSeconds, 0.001f);
    const FVector2f W = RelativeVelocity - RelativePosition * InverseStep;
    const float WLength = W.Size();
    const FVector2f UnitW = WLength > KINDA_SMALL_NUMBER
      ? W / WLength : StablePairDirection(Agent.AgentId, Other.AgentId);
    LineDirection = FVector2f(UnitW.Y, -UnitW.X);
    Correction = UnitW * (CombinedRadius * InverseStep - WLength);
    const float MaxRelativeCorrection = Agent.MaxSpeedCmps + Other.MaxSpeedCmps;
    if (Correction.SizeSquared() > FMath::Square(MaxRelativeCorrection))
      Correction = Correction.GetSafeNormal() * MaxRelativeCorrection;
  }
  OutHalfPlane.Point = Quantize(
    AgentVelocity + Correction * Responsibility, Settings.VelocityQuantumCmps);
  OutHalfPlane.Normal = NormalizeQ15(FVector2f(-LineDirection.Y, LineDirection.X));
  OutHalfPlane.StableOrder = StableOrder;
  return FMath::IsFinite(OutHalfPlane.Point.X) && FMath::IsFinite(OutHalfPlane.Point.Y)
    && FMath::IsFinite(OutHalfPlane.Normal.X) && FMath::IsFinite(OutHalfPlane.Normal.Y);
}

bool IsInsideBounds(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FVector2f Position)
{
  const float Clearance = Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm;
  return Position.X >= FlowConfig.BoundsMin.X + Clearance
    && Position.X <= FlowConfig.BoundsMax.X - Clearance
    && Position.Y >= FlowConfig.BoundsMin.Y + Clearance
    && Position.Y <= FlowConfig.BoundsMax.Y - Clearance;
}

bool IsInsideObstacle(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdSharedFlowObstacleSpec& Obstacle,
  const FVector2f Position)
{
  const float Clearance = Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm;
  return Position.X > Obstacle.Center.X - Obstacle.Extent.X - Clearance
    && Position.X < Obstacle.Center.X + Obstacle.Extent.X + Clearance
    && Position.Y > Obstacle.Center.Y - Obstacle.Extent.Y - Clearance
    && Position.Y < Obstacle.Center.Y + Obstacle.Extent.Y + Clearance;
}

bool SegmentObstacleEntry(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdSharedFlowObstacleSpec& Obstacle,
  const FVector2f Start,
  const FVector2f End,
  FEnvironmentHit& OutHit)
{
  const float Clearance = Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm;
  const FVector2f Minimum(
    Obstacle.Center.X - Obstacle.Extent.X - Clearance,
    Obstacle.Center.Y - Obstacle.Extent.Y - Clearance);
  const FVector2f Maximum(
    Obstacle.Center.X + Obstacle.Extent.X + Clearance,
    Obstacle.Center.Y + Obstacle.Extent.Y + Clearance);
  if (Start.X > Minimum.X && Start.X < Maximum.X
    && Start.Y > Minimum.Y && Start.Y < Maximum.Y)
    return false;
  const FVector2f Delta = End - Start;
  float Entry = 0.0f;
  float Exit = 1.0f;
  int32 EntryFace = INDEX_NONE;
  FVector2f EntryNormal = FVector2f::ZeroVector;
  float Boundary = 0.0f;
  const auto ClipAxis = [&](const float StartValue, const float DeltaValue,
    const float MinValue, const float MaxValue,
    const int32 MinFace, const int32 MaxFace,
    const FVector2f MinNormal, const FVector2f MaxNormal,
    float& InOutEntry, float& InOutExit,
    int32& InOutFace, FVector2f& InOutNormal, float& InOutBoundary)
  {
    if (FMath::Abs(DeltaValue) <= KINDA_SMALL_NUMBER)
      return StartValue >= MinValue && StartValue <= MaxValue;
    float Near = (MinValue - StartValue) / DeltaValue;
    float Far = (MaxValue - StartValue) / DeltaValue;
    int32 NearFace = MinFace;
    FVector2f NearNormal = MinNormal;
    float NearBoundary = MinValue;
    if (Near > Far)
    {
      Swap(Near, Far);
      NearFace = MaxFace;
      NearNormal = MaxNormal;
      NearBoundary = MaxValue;
    }
    if (Near > InOutEntry || (FMath::IsNearlyEqual(Near, InOutEntry) && NearFace < InOutFace))
    {
      InOutEntry = Near;
      InOutFace = NearFace;
      InOutNormal = NearNormal;
      InOutBoundary = NearBoundary;
    }
    InOutExit = FMath::Min(InOutExit, Far);
    return InOutEntry <= InOutExit;
  };
  if (!ClipAxis(Start.X, Delta.X, Minimum.X, Maximum.X,
      0, 1, FVector2f(-1, 0), FVector2f(1, 0),
      Entry, Exit, EntryFace, EntryNormal, Boundary)
    || !ClipAxis(Start.Y, Delta.Y, Minimum.Y, Maximum.Y,
      2, 3, FVector2f(0, -1), FVector2f(0, 1),
      Entry, Exit, EntryFace, EntryNormal, Boundary))
    return false;
  if (EntryFace == INDEX_NONE || Entry < 0.0f || Entry > 1.0f) return false;
  const bool bEndStrictlyInside = End.X > Minimum.X && End.X < Maximum.X
    && End.Y > Minimum.Y && End.Y < Maximum.Y;
  if (Entry >= 1.0f - 1.0e-6f && !bEndStrictlyInside) return false;
  OutHit.bHit = true;
  OutHit.ObstacleId = Obstacle.ObstacleId;
  OutHit.Face = EntryFace;
  OutHit.EntryT = Entry;
  OutHit.Normal = EntryNormal;
  OutHit.BoundaryCoordinate = Boundary;
  return true;
}

bool FindFirstEnvironmentHit(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FVector2f Velocity,
  const float FixedStep,
  FEnvironmentHit& OutHit)
{
  const FVector2f End = Agent.Position + Velocity * FixedStep;
  TArray<FCrowdSharedFlowObstacleSpec> Obstacles = FlowConfig.ObstacleSpecs;
  Obstacles.Sort([](const auto& A, const auto& B) { return A.ObstacleId < B.ObstacleId; });
  bool bFound = false;
  for (const FCrowdSharedFlowObstacleSpec& Obstacle : Obstacles)
  {
    FEnvironmentHit Hit;
    if (!SegmentObstacleEntry(Agent, Obstacle, Agent.Position, End, Hit)) continue;
    if (!bFound || Hit.EntryT < OutHit.EntryT
      || (Hit.EntryT == OutHit.EntryT && Hit.ObstacleId < OutHit.ObstacleId)
      || (Hit.EntryT == OutHit.EntryT && Hit.ObstacleId == OutHit.ObstacleId
        && Hit.Face < OutHit.Face))
    {
      OutHit = Hit;
      bFound = true;
    }
  }
  return bFound;
}

void AddBoundsConstraints(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FCrowdLocalPredictiveSettings& Settings,
  TArray<FCrowdVelocityHalfPlane>& InOutHalfPlanes)
{
  const float Clearance = Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm;
  const float InverseStep = 1.0f / FMath::Max(Settings.FixedStepSeconds, 0.001f);
  const float MinX = (FlowConfig.BoundsMin.X + Clearance - Agent.Position.X) * InverseStep;
  const float MaxX = (FlowConfig.BoundsMax.X - Clearance - Agent.Position.X) * InverseStep;
  const float MinY = (FlowConfig.BoundsMin.Y + Clearance - Agent.Position.Y) * InverseStep;
  const float MaxY = (FlowConfig.BoundsMax.Y - Clearance - Agent.Position.Y) * InverseStep;
  InOutHalfPlanes.Add({FVector2f(MinX, 0), FVector2f(1, 0), 1000000});
  InOutHalfPlanes.Add({FVector2f(MaxX, 0), FVector2f(-1, 0), 1000001});
  InOutHalfPlanes.Add({FVector2f(0, MinY), FVector2f(0, 1), 1000002});
  InOutHalfPlanes.Add({FVector2f(0, MaxY), FVector2f(0, -1), 1000003});
}

FCrowdVelocityHalfPlane BuildEnvironmentHalfPlane(
  const FCrowdLocalPredictiveAgent& Agent,
  const FEnvironmentHit& Hit,
  const FCrowdLocalPredictiveSettings& Settings,
  const int32 StableOrder)
{
  const float StartCoordinate = Hit.Face <= 1 ? Agent.Position.X : Agent.Position.Y;
  const float BoundaryVelocity = (Hit.BoundaryCoordinate - StartCoordinate)
    / FMath::Max(Settings.FixedStepSeconds, 0.001f);
  const float Scalar = (Hit.Face == 0 || Hit.Face == 2)
    ? -BoundaryVelocity : BoundaryVelocity;
  FCrowdVelocityHalfPlane HalfPlane;
  HalfPlane.Point = Hit.Normal * Scalar;
  HalfPlane.Normal = Hit.Normal;
  HalfPlane.StableOrder = StableOrder;
  return HalfPlane;
}

bool ProjectVelocityToEnvironment(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FCrowdLocalPredictiveSettings& Settings,
  FVector2f& InOutVelocity,
  int32& OutEnvironmentConstraintCount)
{
  FCrowdVelocityHalfPlaneInput Input;
  Input.PreferredVelocity = InOutVelocity.GetClampedToMaxSize(Agent.MaxSpeedCmps);
  Input.Settings.MaxSpeedCmps = Agent.MaxSpeedCmps;
  Input.Settings.BehaviorEpsilonCmps = Settings.ConstraintEpsilonCmps;
  Input.Settings.VelocityQuantumCmps = Settings.VelocityQuantumCmps;
  AddBoundsConstraints(Agent, FlowConfig, Settings, Input.HalfPlanes);
  if (!FCrowdVelocityHalfPlaneKernel::SolveContinuous(Input, InOutVelocity))
    return false;
  TSet<uint64> EnvironmentKeys;
  const int32 MaximumPasses = FMath::Max(1, FlowConfig.ObstacleSpecs.Num() + 1);
  for (int32 Pass = 0; Pass < MaximumPasses; ++Pass)
  {
    FEnvironmentHit Hit;
    if (!FindFirstEnvironmentHit(
      Agent, FlowConfig, InOutVelocity, Settings.FixedStepSeconds, Hit)) return true;
    const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Hit.ObstacleId)) << 32)
      | static_cast<uint32>(Hit.Face);
    if (EnvironmentKeys.Contains(Key)) return false;
    EnvironmentKeys.Add(Key);
    Input.HalfPlanes.Add(BuildEnvironmentHalfPlane(
      Agent, Hit, Settings, 3000000 + Pass));
    ++OutEnvironmentConstraintCount;
    Input.PreferredVelocity = InOutVelocity;
    if (!FCrowdVelocityHalfPlaneKernel::SolveContinuous(Input, InOutVelocity))
      return false;
  }
  FEnvironmentHit RemainingHit;
  return !FindFirstEnvironmentHit(
    Agent, FlowConfig, InOutVelocity, Settings.FixedStepSeconds, RemainingHit);
}

bool FindComponentCommonVelocity(
  const TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
  const TConstArrayView<int32> AgentIndices,
  const int32 GrantedAgentId,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FCrowdLocalPredictiveSettings& Settings,
  FVector2f& OutVelocity,
  int32& OutEnvironmentConstraintCount)
{
  if (AgentIndices.IsEmpty()) return false;
  float MaxSpeed = TNumericLimits<float>::Max();
  FVector2f AveragePreferred = FVector2f::ZeroVector;
  FVector2f GrantedPreferred = FVector2f::ZeroVector;
  bool bFoundGrant = false;
  for (const int32 AgentIndex : AgentIndices)
  {
    if (!Agents.IsValidIndex(AgentIndex)) return false;
    const FCrowdLocalPredictiveAgent& Agent = Agents[AgentIndex];
    MaxSpeed = FMath::Min(MaxSpeed, Agent.MaxSpeedCmps);
    AveragePreferred += Agent.PreferredVelocity;
    if (Agent.AgentId == GrantedAgentId)
    {
      GrantedPreferred = Agent.PreferredVelocity;
      bFoundGrant = true;
    }
  }
  FCrowdVelocityHalfPlaneInput Input;
  Input.PreferredVelocity = bFoundGrant
    ? GrantedPreferred
    : AveragePreferred / static_cast<float>(AgentIndices.Num());
  Input.Settings.MaxSpeedCmps = MaxSpeed;
  Input.Settings.BehaviorEpsilonCmps = Settings.ConstraintEpsilonCmps;
  Input.Settings.VelocityQuantumCmps = Settings.VelocityQuantumCmps;
  for (const int32 AgentIndex : AgentIndices)
    AddBoundsConstraints(Agents[AgentIndex], FlowConfig, Settings, Input.HalfPlanes);
  FCrowdVelocityHalfPlaneResult SolveResult =
    FCrowdVelocityHalfPlaneKernel::Solve(Input);
  TSet<uint64> EnvironmentKeys;
  const int32 MaximumPasses = FMath::Max(
    1, AgentIndices.Num() * FMath::Max(1, FlowConfig.ObstacleSpecs.Num()));
  for (int32 Pass = 0; SolveResult.bQuantizedValid && Pass < MaximumPasses; ++Pass)
  {
    FEnvironmentHit BestHit;
    int32 BestAgentIndex = INDEX_NONE;
    for (const int32 AgentIndex : AgentIndices)
    {
      FEnvironmentHit Hit;
      if (!FindFirstEnvironmentHit(Agents[AgentIndex], FlowConfig,
        SolveResult.QuantizedVelocity, Settings.FixedStepSeconds, Hit)) continue;
      if (BestAgentIndex == INDEX_NONE || Hit.EntryT < BestHit.EntryT
        || (Hit.EntryT == BestHit.EntryT
          && Agents[AgentIndex].AgentId < Agents[BestAgentIndex].AgentId)
        || (Hit.EntryT == BestHit.EntryT
          && Agents[AgentIndex].AgentId == Agents[BestAgentIndex].AgentId
          && (Hit.ObstacleId < BestHit.ObstacleId
            || (Hit.ObstacleId == BestHit.ObstacleId && Hit.Face < BestHit.Face))))
      {
        BestHit = Hit;
        BestAgentIndex = AgentIndex;
      }
    }
    if (BestAgentIndex == INDEX_NONE)
    {
      OutVelocity = SolveResult.QuantizedVelocity;
      return true;
    }
    const uint64 Key =
      (static_cast<uint64>(static_cast<uint32>(Agents[BestAgentIndex].AgentId)) << 32)
      ^ (static_cast<uint64>(static_cast<uint32>(BestHit.ObstacleId)) << 4)
      ^ static_cast<uint32>(BestHit.Face);
    if (EnvironmentKeys.Contains(Key)) return false;
    EnvironmentKeys.Add(Key);
    Input.HalfPlanes.Add(BuildEnvironmentHalfPlane(
      Agents[BestAgentIndex], BestHit, Settings, 3000000 + Pass));
    ++OutEnvironmentConstraintCount;
    SolveResult = FCrowdVelocityHalfPlaneKernel::Solve(Input);
  }
  return false;
}

bool IsValidAgentInput(const FCrowdLocalPredictiveAgent& Agent)
{
  return Agent.AgentId != INDEX_NONE
    && FMath::IsFinite(Agent.Position.X) && FMath::IsFinite(Agent.Position.Y)
    && FMath::IsFinite(Agent.Velocity.X) && FMath::IsFinite(Agent.Velocity.Y)
    && FMath::IsFinite(Agent.PreferredVelocity.X) && FMath::IsFinite(Agent.PreferredVelocity.Y)
    && Agent.PhysicalRadiusCm > 0.0f && Agent.HardSafetyGapCm >= 0.0f
    && Agent.MaxSpeedCmps >= 0.0f && Agent.BlockedAgeSteps >= 0;
}

int32 NextBlockedAge(
  const FCrowdLocalPredictiveAgent& Agent,
  const FCrowdLocalPredictiveSettings& Settings)
{
  const float Requested = Agent.PreferredVelocity.Size();
  const FVector2f Forward = Requested > KINDA_SMALL_NUMBER
    ? Agent.PreferredVelocity / Requested : FVector2f::ZeroVector;
  const float Progress = FVector2f::DotProduct(Agent.Velocity, Forward);
  if (Requested >= Settings.RequestedProgressThresholdCmps
    && Progress <= Settings.BlockedProgressThresholdCmps)
    return FMath::Min(MAX_int32 - 1, Agent.BlockedAgeSteps + 1);
  return FMath::Max(0, Agent.BlockedAgeSteps - 1);
}

int32 ProgressDeficit(const FCrowdLocalPredictiveAgent& Agent)
{
  const float Requested = Agent.PreferredVelocity.Size();
  if (Requested <= KINDA_SMALL_NUMBER) return 0;
  const float Progress = FVector2f::DotProduct(
    Agent.Velocity, Agent.PreferredVelocity / Requested);
  return FMath::RoundToInt(FMath::Max(0.0f, Requested - Progress));
}
}

void FCrowdLocalPredictiveInteractionKernel::BuildCandidatePairs(
  const TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
  const FCrowdLocalPredictiveSettings& Settings,
  TArray<FCrowdLocalPredictivePair>& OutPairs)
{
  OutPairs.Reset();
  const float CellSize = FMath::Max(1.0f, Settings.SpatialCellSizeCm);
  const float Horizon = FMath::Max(Settings.FixedStepSeconds, Settings.TimeHorizonSeconds);
  TArray<FGridRecord> Records;
  Records.Reserve(Agents.Num() * 4);
  for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
  {
    const auto& Agent = Agents[AgentIndex];
    const FVector2f End = Agent.Position + Agent.PreferredVelocity * Horizon;
    const float Expand = Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm;
    const int32 MinX = FMath::FloorToInt((FMath::Min(Agent.Position.X, End.X) - Expand) / CellSize);
    const int32 MaxX = FMath::FloorToInt((FMath::Max(Agent.Position.X, End.X) + Expand) / CellSize);
    const int32 MinY = FMath::FloorToInt((FMath::Min(Agent.Position.Y, End.Y) - Expand) / CellSize);
    const int32 MaxY = FMath::FloorToInt((FMath::Max(Agent.Position.Y, End.Y) + Expand) / CellSize);
    for (int32 Y = MinY; Y <= MaxY; ++Y)
      for (int32 X = MinX; X <= MaxX; ++X)
        Records.Add({X, Y, AgentIndex, Agent.AgentId});
  }
  Records.Sort([](const FGridRecord& A, const FGridRecord& B)
  {
    if (A.Y != B.Y) return A.Y < B.Y;
    if (A.X != B.X) return A.X < B.X;
    return A.AgentId < B.AgentId;
  });
  TSet<uint64> PairKeys;
  PairKeys.Reserve(Agents.Num() * 8);
  int32 Begin = 0;
  while (Begin < Records.Num())
  {
    int32 End = Begin + 1;
    while (End < Records.Num() && Records[End].X == Records[Begin].X
      && Records[End].Y == Records[Begin].Y) ++End;
    for (int32 A = Begin; A < End; ++A)
      for (int32 B = A + 1; B < End; ++B)
      {
        const int32 MinId = FMath::Min(Records[A].AgentId, Records[B].AgentId);
        const int32 MaxId = FMath::Max(Records[A].AgentId, Records[B].AgentId);
        PairKeys.Add((static_cast<uint64>(static_cast<uint32>(MinId)) << 32)
          | static_cast<uint32>(MaxId));
      }
    Begin = End;
  }
  TArray<uint64> SortedKeys = PairKeys.Array();
  SortedKeys.Sort();
  TMap<int32, int32> IndexById;
  IndexById.Reserve(Agents.Num());
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
    IndexById.Add(Agents[Index].AgentId, Index);
  OutPairs.Reserve(SortedKeys.Num());
  for (const uint64 Key : SortedKeys)
  {
    const int32 MinId = static_cast<int32>(Key >> 32);
    const int32 MaxId = static_cast<int32>(Key & 0xffffffffu);
    const int32* MinIndex = IndexById.Find(MinId);
    const int32* MaxIndex = IndexById.Find(MaxId);
    if (!MinIndex || !MaxIndex) continue;
    if (Agents[*MinIndex].InteractionLayer
      != Agents[*MaxIndex].InteractionLayer)
      continue;
    FCrowdLocalPredictivePair& Pair = OutPairs.AddDefaulted_GetRef();
    Pair.MinAgentId = MinId;
    Pair.MaxAgentId = MaxId;
    Pair.MinAgentIndex = *MinIndex;
    Pair.MaxAgentIndex = *MaxIndex;
    Pair.DistanceBucket = FMath::RoundToInt(
      (Agents[*MinIndex].Position - Agents[*MaxIndex].Position).Size() / 10.0f);
    ClosestApproach(Agents[*MinIndex], Agents[*MaxIndex], Horizon,
      Pair.ClosestTimeSeconds, Pair.PredictedSeparationCm);
    Pair.RequiredSeparationCm = RequiredSeparation(Agents[*MinIndex], Agents[*MaxIndex]);
  }
}

bool FCrowdLocalPredictiveInteractionKernel::ValidateJointResult(
  const TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FCrowdLocalPredictiveSettings& Settings,
  const TConstArrayView<FCrowdLocalPredictivePair> ConflictPairs,
  const TConstArrayView<FCrowdLocalPredictiveResult> Results)
{
  TMap<int32, const FCrowdLocalPredictiveResult*> ResultById;
  TMap<int32, const FCrowdLocalPredictiveAgent*> AgentById;
  for (const auto& Result : Results) ResultById.Add(Result.AgentId, &Result);
  for (const auto& Agent : Agents) AgentById.Add(Agent.AgentId, &Agent);
  for (const auto& Agent : Agents)
  {
    const auto* const* Result = ResultById.Find(Agent.AgentId);
    if (!Result || !(*Result)->bValid) return false;
    const FVector2f End = Agent.Position + (*Result)->Velocity * Settings.FixedStepSeconds;
    if (!IsInsideBounds(Agent, FlowConfig, Agent.Position)
      || !IsInsideBounds(Agent, FlowConfig, End)) return false;
    for (const auto& Obstacle : FlowConfig.ObstacleSpecs)
    {
      if (IsInsideObstacle(Agent, Obstacle, Agent.Position)
        || IsInsideObstacle(Agent, Obstacle, End)) return false;
      FEnvironmentHit Hit;
      if (SegmentObstacleEntry(Agent, Obstacle, Agent.Position, End, Hit)) return false;
    }
  }
  const float Horizon = FMath::Max(Settings.FixedStepSeconds, Settings.TimeHorizonSeconds);
  for (const auto& Pair : ConflictPairs)
  {
    const auto* const* A = AgentById.Find(Pair.MinAgentId);
    const auto* const* B = AgentById.Find(Pair.MaxAgentId);
    const auto* const* ResultA = ResultById.Find(Pair.MinAgentId);
    const auto* const* ResultB = ResultById.Find(Pair.MaxAgentId);
    if (!A || !B || !ResultA || !ResultB) return false;
    const FVector2f RelativePosition = (*B)->Position - (*A)->Position;
    const FVector2f RelativeVelocity = (*ResultB)->Velocity - (*ResultA)->Velocity;
    const float SpeedSquared = RelativeVelocity.SizeSquared();
    const float Time = SpeedSquared > KINDA_SMALL_NUMBER
      ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
          / SpeedSquared, 0.0f, Horizon)
      : 0.0f;
    const float Separation = (RelativePosition + RelativeVelocity * Time).Size();
    if (Separation + 0.5f < Pair.RequiredSeparationCm) return false;
  }
  return true;
}

void FCrowdLocalPredictiveInteractionKernel::Solve(
  const TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FCrowdLocalPredictiveSettings& Settings,
  const TConstArrayView<FCrowdLocalPredictiveGrantState> PreviousGrantStates,
  TArray<FCrowdLocalPredictivePair>& OutConflictPairs,
  TArray<FCrowdLocalPredictiveGrantState>& OutGrantStates,
  TArray<FCrowdLocalPredictiveResult>& OutResults,
  FCrowdLocalPredictiveSummary& OutSummary,
  FCrowdLocalPredictiveDiagnosticTrace* OutTrace)
{
  OutConflictPairs.Reset();
  OutGrantStates.Reset();
  OutResults.Reset();
  OutSummary = FCrowdLocalPredictiveSummary();
  if (OutTrace) *OutTrace = FCrowdLocalPredictiveDiagnosticTrace();
  TArray<FCrowdLocalPredictiveAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
  {
    if (!IsValidAgentInput(SortedAgents[Index])
      || (Index > 0 && SortedAgents[Index - 1].AgentId == SortedAgents[Index].AgentId))
      return;
  }
  if (Settings.FixedStepSeconds <= 0.0f || Settings.TimeHorizonSeconds <= 0.0f
    || Settings.VelocityQuantumCmps <= 0.0f
    || Settings.GrantedResponsibility < 0.0f || Settings.GrantedResponsibility > 0.5f
    || Settings.GrantDurationSteps <= 0 || Settings.JointIterationCount <= 0)
    return;
  TArray<FCrowdLocalPredictivePair> Candidates;
  BuildCandidatePairs(SortedAgents, Settings, Candidates);
  OutSummary.CandidatePairCount = Candidates.Num();
  const float PairSafetyMargin = PairQuantizationSafetyMargin(Settings);
  for (const auto& Pair : Candidates)
    if (Pair.PredictedSeparationCm < Pair.RequiredSeparationCm + PairSafetyMargin)
      OutConflictPairs.Add(Pair);
  OutSummary.ConflictPairCount = OutConflictPairs.Num();

  TArray<TArray<int32>> Adjacency;
  Adjacency.SetNum(SortedAgents.Num());
  for (const auto& Pair : OutConflictPairs)
  {
    Adjacency[Pair.MinAgentIndex].Add(Pair.MaxAgentIndex);
    Adjacency[Pair.MaxAgentIndex].Add(Pair.MinAgentIndex);
  }
  for (auto& Neighbors : Adjacency)
    Neighbors.Sort([&](const int32 A, const int32 B)
    { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });

  TArray<int32> NextAges;
  NextAges.SetNum(SortedAgents.Num());
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
  {
    NextAges[Index] = NextBlockedAge(SortedAgents[Index], Settings);
    OutSummary.BlockedAgeMax = FMath::Max(OutSummary.BlockedAgeMax, NextAges[Index]);
  }

  TArray<FComponent> Components;
  TArray<bool> Visited;
  Visited.Init(false, SortedAgents.Num());
  TMap<uint32, const FCrowdLocalPredictiveGrantState*>
    PreviousGrantStateByComponent;
  PreviousGrantStateByComponent.Reserve(
    PreviousGrantStates.Num());
  for (const FCrowdLocalPredictiveGrantState& Candidate :
    PreviousGrantStates)
  {
    const FCrowdLocalPredictiveGrantState** Existing =
      PreviousGrantStateByComponent.Find(Candidate.ComponentKey);
    if (!Existing || Candidate.GrantEpoch > (*Existing)->GrantEpoch)
      PreviousGrantStateByComponent.Add(
        Candidate.ComponentKey, &Candidate);
  }
  for (int32 Root = 0; Root < SortedAgents.Num(); ++Root)
  {
    if (Visited[Root] || Adjacency[Root].IsEmpty()) continue;
    FComponent Component;
    TArray<int32> Queue = {Root};
    Visited[Root] = true;
    for (int32 Read = 0; Read < Queue.Num(); ++Read)
    {
      const int32 Current = Queue[Read];
      Component.AgentIndices.Add(Current);
      for (const int32 Neighbor : Adjacency[Current])
      {
        if (Visited[Neighbor]) continue;
        Visited[Neighbor] = true;
        Queue.Add(Neighbor);
      }
    }
    Component.AgentIndices.Sort([&](const int32 A, const int32 B)
    { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });
    for (const int32 Index : Component.AgentIndices)
      Component.Key = HashInt(Component.Key, SortedAgents[Index].AgentId);

    const FCrowdLocalPredictiveGrantState* Previous = nullptr;
    if (const FCrowdLocalPredictiveGrantState* const* Match =
      PreviousGrantStateByComponent.Find(Component.Key))
      Previous = *Match;
    if (Previous && Previous->RemainingSteps > 0
      && Component.AgentIndices.ContainsByPredicate([&](const int32 Index)
        { return SortedAgents[Index].AgentId == Previous->GrantedAgentId; }))
    {
      Component.GrantedAgentId = Previous->GrantedAgentId;
      Component.GrantEpoch = Previous->GrantEpoch;
      Component.RemainingSteps = Previous->RemainingSteps - 1;
    }
    else
    {
      int32 BestIndex = Component.AgentIndices[0];
      for (const int32 Index : Component.AgentIndices)
      {
        const bool bBetter = NextAges[Index] > NextAges[BestIndex]
          || (NextAges[Index] == NextAges[BestIndex]
            && ProgressDeficit(SortedAgents[Index]) > ProgressDeficit(SortedAgents[BestIndex]))
          || (NextAges[Index] == NextAges[BestIndex]
            && ProgressDeficit(SortedAgents[Index]) == ProgressDeficit(SortedAgents[BestIndex])
            && SortedAgents[Index].AgentId < SortedAgents[BestIndex].AgentId);
        if (bBetter) BestIndex = Index;
      }
      Component.GrantedAgentId = SortedAgents[BestIndex].AgentId;
      Component.GrantEpoch = Previous ? Previous->GrantEpoch + 1 : 1;
      Component.RemainingSteps = Settings.GrantDurationSteps - 1;
      if (Previous && Previous->GrantedAgentId != Component.GrantedAgentId)
        ++OutSummary.GrantSwitchCount;
    }
    OutGrantStates.Add({Component.Key, Component.GrantedAgentId,
      Component.GrantEpoch, Component.RemainingSteps});
    Components.Add(Component);
  }
  OutSummary.ComponentCount = Components.Num();
  for (const auto& Component : Components)
    OutSummary.MaxComponentSize = FMath::Max(
      OutSummary.MaxComponentSize, Component.AgentIndices.Num());

  TArray<uint32> ComponentKeyByAgent;
  TArray<int32> GrantByAgent;
  TArray<int32> GrantEpochByAgent;
  ComponentKeyByAgent.Init(0, SortedAgents.Num());
  GrantByAgent.Init(INDEX_NONE, SortedAgents.Num());
  GrantEpochByAgent.Init(0, SortedAgents.Num());
  for (const auto& Component : Components)
    for (const int32 Index : Component.AgentIndices)
    {
      ComponentKeyByAgent[Index] = Component.Key;
      GrantByAgent[Index] = Component.GrantedAgentId;
      GrantEpochByAgent[Index] = Component.GrantEpoch;
    }
  for (auto& Pair : OutConflictPairs)
  {
    const int32 Grant = GrantByAgent[Pair.MinAgentIndex];
    if (Grant == Pair.MinAgentId)
    {
      Pair.MinAgentResponsibility = Settings.GrantedResponsibility;
      Pair.MaxAgentResponsibility = 1.0f - Settings.GrantedResponsibility;
    }
    else if (Grant == Pair.MaxAgentId)
    {
      Pair.MinAgentResponsibility = 1.0f - Settings.GrantedResponsibility;
      Pair.MaxAgentResponsibility = Settings.GrantedResponsibility;
    }
  }

  for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
  {
    const auto& Agent = SortedAgents[AgentIndex];
    FCrowdLocalPredictiveResult Result;
    Result.AgentId = Agent.AgentId;
    Result.NextBlockedAgeSteps = NextAges[AgentIndex];
    Result.ComponentKey = ComponentKeyByAgent[AgentIndex];
    Result.GrantEpoch = GrantEpochByAgent[AgentIndex];
    Result.bGranted = GrantByAgent[AgentIndex] == Agent.AgentId;
    Result.bYielding = GrantByAgent[AgentIndex] != INDEX_NONE && !Result.bGranted;
    TArray<FCrowdVelocityHalfPlane> HalfPlanes;
    int32 PairOrder = 0;
    for (const auto& Pair : OutConflictPairs)
    {
      const bool bMin = Pair.MinAgentIndex == AgentIndex;
      const bool bMax = Pair.MaxAgentIndex == AgentIndex;
      if (!bMin && !bMax) continue;
      const int32 OtherIndex = bMin ? Pair.MaxAgentIndex : Pair.MinAgentIndex;
      const float Responsibility = bMin
        ? Pair.MinAgentResponsibility : Pair.MaxAgentResponsibility;
      FCrowdVelocityHalfPlane HalfPlane;
      if (!BuildPairHalfPlane(Agent, SortedAgents[OtherIndex], Responsibility,
        Settings, PairOrder++, HalfPlane)) continue;
      HalfPlanes.Add(HalfPlane);
      ++Result.NeighborCount;
    }
    AddBoundsConstraints(Agent, FlowConfig, Settings, HalfPlanes);

    FCrowdVelocityHalfPlaneInput Input;
    Input.PreferredVelocity = Agent.PreferredVelocity;
    Input.Settings.MaxSpeedCmps = Agent.MaxSpeedCmps;
    Input.Settings.BehaviorEpsilonCmps = Settings.ConstraintEpsilonCmps;
    Input.Settings.VelocityQuantumCmps = Settings.VelocityQuantumCmps;
    Input.HalfPlanes = HalfPlanes;
    auto SolveResult = FCrowdVelocityHalfPlaneKernel::Solve(Input);
    TSet<uint64> EnvironmentKeys;
    int32 EnvironmentPass = 0;
    while (SolveResult.bQuantizedValid
      && EnvironmentPass <= FlowConfig.ObstacleSpecs.Num())
    {
      FEnvironmentHit Hit;
      if (!FindFirstEnvironmentHit(Agent, FlowConfig,
        SolveResult.QuantizedVelocity, Settings.FixedStepSeconds, Hit)) break;
      const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Hit.ObstacleId)) << 32)
        | static_cast<uint32>(Hit.Face);
      if (EnvironmentKeys.Contains(Key))
      {
        SolveResult.bQuantizedValid = false;
        break;
      }
      EnvironmentKeys.Add(Key);
      Input.HalfPlanes.Add(BuildEnvironmentHalfPlane(
        Agent, Hit, Settings, 2000000 + EnvironmentPass));
      ++OutSummary.EnvironmentConstraintCount;
      SolveResult = FCrowdVelocityHalfPlaneKernel::Solve(Input);
      ++EnvironmentPass;
    }
    Result.ConstraintCount = Input.HalfPlanes.Num();
    Result.bValid = SolveResult.bContinuousValid && SolveResult.bQuantizedValid;
    if (Result.bValid)
    {
      Result.Velocity = SolveResult.QuantizedVelocity;
      Result.bAdjusted = !Result.Velocity.Equals(
        Quantize(Agent.PreferredVelocity.GetClampedToMaxSize(Agent.MaxSpeedCmps),
          Settings.VelocityQuantumCmps), 0.0f);
    }
    else
    {
      ++OutSummary.InfeasibleAgentCount;
      if (SolveResult.bContinuousValid && !SolveResult.bQuantizedValid)
        ++OutSummary.QuantizationFailureCount;
    }
    OutResults.Add(Result);
  }
  if (OutTrace) OutTrace->InitialIndependentResults = OutResults;

  const float ValidationHorizon = FMath::Max(
    Settings.FixedStepSeconds, Settings.TimeHorizonSeconds);
  const auto IsPairSafe = [&](const FCrowdLocalPredictivePair& Pair)
  {
    if (!OutResults.IsValidIndex(Pair.MinAgentIndex)
      || !OutResults.IsValidIndex(Pair.MaxAgentIndex)) return false;
    const FCrowdLocalPredictiveResult& ResultA = OutResults[Pair.MinAgentIndex];
    const FCrowdLocalPredictiveResult& ResultB = OutResults[Pair.MaxAgentIndex];
    if (!ResultA.bValid || !ResultB.bValid) return false;
    const FVector2f RelativePosition =
      SortedAgents[Pair.MaxAgentIndex].Position - SortedAgents[Pair.MinAgentIndex].Position;
    const FVector2f RelativeVelocity = ResultB.Velocity - ResultA.Velocity;
    const float SpeedSquared = RelativeVelocity.SizeSquared();
    const float Time = SpeedSquared > KINDA_SMALL_NUMBER
      ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
          / SpeedSquared, 0.0f, ValidationHorizon)
      : 0.0f;
    const float Separation = (RelativePosition + RelativeVelocity * Time).Size();
    return Separation + 0.5f >= Pair.RequiredSeparationCm;
  };

  const auto ResolveAgentAgainstCurrentPairs = [&](const int32 AgentIndex)
  {
    const FCrowdLocalPredictiveAgent& Agent = SortedAgents[AgentIndex];
    FCrowdLocalPredictiveResult& Result = OutResults[AgentIndex];
    Result.Velocity = FVector2f::ZeroVector;
    Result.NeighborCount = 0;
    Result.ConstraintCount = 0;
    Result.bAdjusted = false;
    Result.bValid = false;
    TArray<FCrowdVelocityHalfPlane> HalfPlanes;
    int32 PairOrder = 0;
    for (const FCrowdLocalPredictivePair& Pair : OutConflictPairs)
    {
      const bool bMin = Pair.MinAgentIndex == AgentIndex;
      const bool bMax = Pair.MaxAgentIndex == AgentIndex;
      if (!bMin && !bMax) continue;
      const int32 OtherIndex = bMin ? Pair.MaxAgentIndex : Pair.MinAgentIndex;
      const float Responsibility = bMin
        ? Pair.MinAgentResponsibility : Pair.MaxAgentResponsibility;
      FCrowdVelocityHalfPlane HalfPlane;
      if (!BuildPairHalfPlane(Agent, SortedAgents[OtherIndex], Responsibility,
        Settings, PairOrder++, HalfPlane)) continue;
      HalfPlanes.Add(HalfPlane);
      ++Result.NeighborCount;
    }
    AddBoundsConstraints(Agent, FlowConfig, Settings, HalfPlanes);
    FCrowdVelocityHalfPlaneInput Input;
    Input.PreferredVelocity = Agent.PreferredVelocity;
    Input.Settings.MaxSpeedCmps = Agent.MaxSpeedCmps;
    Input.Settings.BehaviorEpsilonCmps = Settings.ConstraintEpsilonCmps;
    Input.Settings.VelocityQuantumCmps = Settings.VelocityQuantumCmps;
    Input.HalfPlanes = MoveTemp(HalfPlanes);
    FCrowdVelocityHalfPlaneResult SolveResult =
      FCrowdVelocityHalfPlaneKernel::Solve(Input);
    TSet<uint64> EnvironmentKeys;
    int32 EnvironmentPass = 0;
    while (SolveResult.bQuantizedValid
      && EnvironmentPass <= FlowConfig.ObstacleSpecs.Num())
    {
      FEnvironmentHit Hit;
      if (!FindFirstEnvironmentHit(Agent, FlowConfig,
        SolveResult.QuantizedVelocity, Settings.FixedStepSeconds, Hit)) break;
      const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Hit.ObstacleId)) << 32)
        | static_cast<uint32>(Hit.Face);
      if (EnvironmentKeys.Contains(Key))
      {
        SolveResult.bQuantizedValid = false;
        break;
      }
      EnvironmentKeys.Add(Key);
      Input.HalfPlanes.Add(BuildEnvironmentHalfPlane(
        Agent, Hit, Settings, 2000000 + EnvironmentPass));
      ++OutSummary.EnvironmentConstraintCount;
      SolveResult = FCrowdVelocityHalfPlaneKernel::Solve(Input);
      ++EnvironmentPass;
    }
    Result.ConstraintCount = Input.HalfPlanes.Num();
    Result.bValid = SolveResult.bContinuousValid && SolveResult.bQuantizedValid;
    if (Result.bValid)
    {
      Result.Velocity = SolveResult.QuantizedVelocity;
      Result.bAdjusted = !Result.Velocity.Equals(
        Quantize(Agent.PreferredVelocity.GetClampedToMaxSize(Agent.MaxSpeedCmps),
          Settings.VelocityQuantumCmps), 0.0f);
    }
  };

  // An agent can avoid one neighbor and thereby create a conflict with another
  // candidate that was safe under the original preferred velocities. Add those
  // pair constraints monotonically and re-solve individual velocities; never
  // collapse the whole connected component to one shared velocity.
  for (int32 CompletionPass = 0; CompletionPass <= Candidates.Num(); ++CompletionPass)
  {
    int32 AddedPairCount = 0;
    for (const FCrowdLocalPredictivePair& Candidate : Candidates)
    {
      if (IsPairSafe(Candidate)) continue;
      if (OutConflictPairs.ContainsByPredicate([&](const auto& Existing)
        {
          return Existing.MinAgentId == Candidate.MinAgentId
            && Existing.MaxAgentId == Candidate.MaxAgentId;
        })) continue;
      FCrowdLocalPredictivePair Added = Candidate;
      int32 GrantedIndex = Added.MinAgentIndex;
      const int32 OtherIndex = Added.MaxAgentIndex;
      const bool bOtherBetter = NextAges[OtherIndex] > NextAges[GrantedIndex]
        || (NextAges[OtherIndex] == NextAges[GrantedIndex]
          && ProgressDeficit(SortedAgents[OtherIndex])
            > ProgressDeficit(SortedAgents[GrantedIndex]))
        || (NextAges[OtherIndex] == NextAges[GrantedIndex]
          && ProgressDeficit(SortedAgents[OtherIndex])
            == ProgressDeficit(SortedAgents[GrantedIndex])
          && SortedAgents[OtherIndex].AgentId < SortedAgents[GrantedIndex].AgentId);
      if (bOtherBetter) GrantedIndex = OtherIndex;
      Added.MinAgentResponsibility = GrantedIndex == Added.MinAgentIndex
        ? Settings.GrantedResponsibility : 1.0f - Settings.GrantedResponsibility;
      Added.MaxAgentResponsibility = 1.0f - Added.MinAgentResponsibility;
      OutConflictPairs.Add(Added);
      ++AddedPairCount;
    }
    if (AddedPairCount == 0) break;
    OutConflictPairs.Sort([](const auto& A, const auto& B)
    {
      if (A.MinAgentId != B.MinAgentId) return A.MinAgentId < B.MinAgentId;
      return A.MaxAgentId < B.MaxAgentId;
    });
    for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
      ResolveAgentAgainstCurrentPairs(AgentIndex);
  }
  OutSummary.ConflictPairCount = OutConflictPairs.Num();
  if (OutTrace) OutTrace->CompletedIndependentResults = OutResults;

  // Pair safety depends on relative velocity. Once the independent half-plane
  // pass has produced a safe component, add the same velocity translation to
  // every member to optimize the remaining common degree of freedom. This
  // preserves all pair-relative velocities instead of collapsing the component
  // toward one common velocity, and is accepted only after environment checks.
  {
    TArray<TArray<int32>> TranslationAdjacency;
    TranslationAdjacency.SetNum(SortedAgents.Num());
    for (const FCrowdLocalPredictivePair& Pair : OutConflictPairs)
    {
      TranslationAdjacency[Pair.MinAgentIndex].AddUnique(Pair.MaxAgentIndex);
      TranslationAdjacency[Pair.MaxAgentIndex].AddUnique(Pair.MinAgentIndex);
    }
    for (TArray<int32>& Neighbors : TranslationAdjacency)
      Neighbors.Sort([&](const int32 A, const int32 B)
      { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });

    TArray<bool> TranslationVisited;
    TranslationVisited.Init(false, SortedAgents.Num());
    for (int32 Root = 0; Root < SortedAgents.Num(); ++Root)
    {
      if (TranslationVisited[Root] || TranslationAdjacency[Root].IsEmpty()) continue;
      TArray<int32> ComponentIndices = {Root};
      TranslationVisited[Root] = true;
      for (int32 Read = 0; Read < ComponentIndices.Num(); ++Read)
        for (const int32 Neighbor : TranslationAdjacency[ComponentIndices[Read]])
        {
          if (TranslationVisited[Neighbor]) continue;
          TranslationVisited[Neighbor] = true;
          ComponentIndices.Add(Neighbor);
        }
      ComponentIndices.Sort([&](const int32 A, const int32 B)
      { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });
      if (ComponentIndices.ContainsByPredicate(
        [&](const int32 Index) { return !OutResults[Index].bValid; })) continue;
      bool bCurrentSafe = true;
      for (const FCrowdLocalPredictivePair& Pair : Candidates)
        if (ComponentIndices.Contains(Pair.MinAgentIndex)
          && ComponentIndices.Contains(Pair.MaxAgentIndex) && !IsPairSafe(Pair))
        {
          bCurrentSafe = false;
          break;
        }
      if (!bCurrentSafe) continue;

      FVector2f DesiredTranslation = FVector2f::ZeroVector;
      double BaselineObjective = 0.0;
      for (const int32 AgentIndex : ComponentIndices)
      {
        DesiredTranslation += SortedAgents[AgentIndex].PreferredVelocity
          - OutResults[AgentIndex].Velocity;
        BaselineObjective += static_cast<double>((
          SortedAgents[AgentIndex].PreferredVelocity
          - OutResults[AgentIndex].Velocity).SizeSquared());
      }
      DesiredTranslation = Quantize(
        DesiredTranslation / static_cast<float>(ComponentIndices.Num()),
        Settings.VelocityQuantumCmps);
      if (DesiredTranslation.IsNearlyZero()) continue;

      TArray<FVector2f> CandidateVelocities;
      CandidateVelocities.SetNum(SortedAgents.Num());
      const auto IsTranslationSafe = [&](const int32 AlphaQ15, double* OutObjective)
      {
        const FVector2f Translation = Quantize(
          DesiredTranslation * (static_cast<float>(AlphaQ15) / 32767.0f),
          Settings.VelocityQuantumCmps);
        double Objective = 0.0;
        for (const int32 AgentIndex : ComponentIndices)
        {
          const FVector2f Velocity = Quantize(
            OutResults[AgentIndex].Velocity + Translation,
            Settings.VelocityQuantumCmps);
          if (Velocity.Size() > SortedAgents[AgentIndex].MaxSpeedCmps
            + Settings.ConstraintEpsilonCmps) return false;
          CandidateVelocities[AgentIndex] = Velocity;
          const FVector2f Difference =
            SortedAgents[AgentIndex].PreferredVelocity - Velocity;
          Objective += static_cast<double>(Difference.SizeSquared());
          const FVector2f End = SortedAgents[AgentIndex].Position
            + Velocity * Settings.FixedStepSeconds;
          if (!IsInsideBounds(SortedAgents[AgentIndex], FlowConfig,
              SortedAgents[AgentIndex].Position)
            || !IsInsideBounds(SortedAgents[AgentIndex], FlowConfig, End)) return false;
          FEnvironmentHit Hit;
          if (FindFirstEnvironmentHit(SortedAgents[AgentIndex], FlowConfig,
            Velocity, Settings.FixedStepSeconds, Hit)) return false;
        }
        for (const FCrowdLocalPredictivePair& Pair : Candidates)
        {
          if (!ComponentIndices.Contains(Pair.MinAgentIndex)
            || !ComponentIndices.Contains(Pair.MaxAgentIndex)) continue;
          const FVector2f RelativePosition = SortedAgents[Pair.MaxAgentIndex].Position
            - SortedAgents[Pair.MinAgentIndex].Position;
          const FVector2f RelativeVelocity = CandidateVelocities[Pair.MaxAgentIndex]
            - CandidateVelocities[Pair.MinAgentIndex];
          const float SpeedSquared = RelativeVelocity.SizeSquared();
          const float Time = SpeedSquared > KINDA_SMALL_NUMBER
            ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
                / SpeedSquared, 0.0f, ValidationHorizon)
            : 0.0f;
          if ((RelativePosition + RelativeVelocity * Time).Size() + 0.5f
            < Pair.RequiredSeparationCm) return false;
        }
        if (OutObjective) *OutObjective = Objective;
        return true;
      };

      if (!IsTranslationSafe(0, nullptr)) continue;
      int32 SafeAlphaQ15 = 0;
      int32 UnsafeAlphaQ15 = 32767;
      double TranslatedObjective = BaselineObjective;
      if (IsTranslationSafe(UnsafeAlphaQ15, &TranslatedObjective))
        SafeAlphaQ15 = UnsafeAlphaQ15;
      else
        while (SafeAlphaQ15 + 1 < UnsafeAlphaQ15)
        {
          const int32 CandidateAlpha =
            SafeAlphaQ15 + (UnsafeAlphaQ15 - SafeAlphaQ15) / 2;
          double CandidateObjective = BaselineObjective;
          if (IsTranslationSafe(CandidateAlpha, &CandidateObjective))
          {
            SafeAlphaQ15 = CandidateAlpha;
            TranslatedObjective = CandidateObjective;
          }
          else UnsafeAlphaQ15 = CandidateAlpha;
        }
      if (SafeAlphaQ15 <= 0
        || !IsTranslationSafe(SafeAlphaQ15, &TranslatedObjective)
        || TranslatedObjective + 0.5 >= BaselineObjective) continue;

      const FVector2f AppliedTranslation = Quantize(
        DesiredTranslation * (static_cast<float>(SafeAlphaQ15) / 32767.0f),
        Settings.VelocityQuantumCmps);
      FCrowdLocalPredictiveComponentTrace ComponentTrace;
      for (const int32 AgentIndex : ComponentIndices)
      {
        ComponentTrace.AgentIds.Add(SortedAgents[AgentIndex].AgentId);
        ComponentTrace.ComponentKey = HashInt(
          ComponentTrace.ComponentKey, SortedAgents[AgentIndex].AgentId);
        ComponentTrace.PreTranslationVelocities.Add({
          SortedAgents[AgentIndex].AgentId, OutResults[AgentIndex].Velocity});
        OutResults[AgentIndex].Velocity = CandidateVelocities[AgentIndex];
        OutResults[AgentIndex].bAdjusted = !OutResults[AgentIndex].Velocity.Equals(
          Quantize(SortedAgents[AgentIndex].PreferredVelocity.GetClampedToMaxSize(
            SortedAgents[AgentIndex].MaxSpeedCmps), Settings.VelocityQuantumCmps), 0.0f);
        ComponentTrace.FinalVelocities.Add({
          SortedAgents[AgentIndex].AgentId, OutResults[AgentIndex].Velocity});
      }
      ComponentTrace.bCoherentTranslationApplied = true;
      ComponentTrace.CoherentTranslation = AppliedTranslation;
      ComponentTrace.SafeAlphaQ15 = SafeAlphaQ15;
      ++OutSummary.CoherentTranslationComponentCount;
      OutSummary.CoherentTranslationAgentCount += ComponentIndices.Num();
      OutSummary.CoherentTranslationMaxCmps = FMath::Max(
        OutSummary.CoherentTranslationMaxCmps, AppliedTranslation.Size());
      if (OutTrace) OutTrace->Components.Add(MoveTemp(ComponentTrace));
    }
  }

  // The safe independent solution can still be a poor local optimum because
  // every half-plane was built against frozen neighbor velocities. Recover the
  // remaining component degrees of freedom by projecting the whole preferred
  // velocity vector against the exact time-horizon pair constraints. The
  // candidate is accepted only when the complete quantized result remains safe
  // and improves the component objective; it never introduces a minimum speed.
  {
    TArray<TArray<int32>> RecoveryAdjacency;
    RecoveryAdjacency.SetNum(SortedAgents.Num());
    for (const FCrowdLocalPredictivePair& Pair : OutConflictPairs)
    {
      RecoveryAdjacency[Pair.MinAgentIndex].AddUnique(Pair.MaxAgentIndex);
      RecoveryAdjacency[Pair.MaxAgentIndex].AddUnique(Pair.MinAgentIndex);
    }
    for (TArray<int32>& Neighbors : RecoveryAdjacency)
      Neighbors.Sort([&](const int32 A, const int32 B)
      { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });

    TArray<bool> RecoveryVisited;
    RecoveryVisited.Init(false, SortedAgents.Num());
    for (int32 Root = 0; Root < SortedAgents.Num(); ++Root)
    {
      if (RecoveryVisited[Root] || RecoveryAdjacency[Root].IsEmpty()) continue;
      TArray<int32> ComponentIndices = {Root};
      RecoveryVisited[Root] = true;
      for (int32 Read = 0; Read < ComponentIndices.Num(); ++Read)
        for (const int32 Neighbor : RecoveryAdjacency[ComponentIndices[Read]])
        {
          if (RecoveryVisited[Neighbor]) continue;
          RecoveryVisited[Neighbor] = true;
          ComponentIndices.Add(Neighbor);
        }
      ComponentIndices.Sort([&](const int32 A, const int32 B)
      { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });
      if (ComponentIndices.ContainsByPredicate(
        [&](const int32 Index) { return !OutResults[Index].bValid; })) continue;

      TArray<FVector2f> RecoveryVelocities;
      RecoveryVelocities.SetNum(SortedAgents.Num());
      TArray<FCrowdLocalPredictiveResult> RecoveryResults = OutResults;
      double BaselineObjective = 0.0;
      for (const int32 AgentIndex : ComponentIndices)
      {
        RecoveryVelocities[AgentIndex] = Quantize(
          SortedAgents[AgentIndex].PreferredVelocity.GetClampedToMaxSize(
            SortedAgents[AgentIndex].MaxSpeedCmps), Settings.VelocityQuantumCmps);
        const FVector2f BaselineDifference = SortedAgents[AgentIndex].PreferredVelocity
          - OutResults[AgentIndex].Velocity;
        BaselineObjective += static_cast<double>(BaselineDifference.SizeSquared());
      }

      bool bRecoveryNumericallyValid = true;
      const float RecoveryTargetMargin = PairQuantizationSafetyMargin(Settings);
      for (int32 Iteration = 0;
        Iteration < FMath::Max(1, Settings.JointIterationCount); ++Iteration)
      {
        for (const FCrowdLocalPredictivePair& Candidate : Candidates)
        {
          if (!ComponentIndices.Contains(Candidate.MinAgentIndex)
            || !ComponentIndices.Contains(Candidate.MaxAgentIndex)) continue;
          const FVector2f RelativePosition =
            SortedAgents[Candidate.MaxAgentIndex].Position
              - SortedAgents[Candidate.MinAgentIndex].Position;
          const FVector2f RelativeVelocity =
            RecoveryVelocities[Candidate.MaxAgentIndex]
              - RecoveryVelocities[Candidate.MinAgentIndex];
          const float RelativeSpeedSquared = RelativeVelocity.SizeSquared();
          const float ClosestTime = RelativeSpeedSquared > KINDA_SMALL_NUMBER
            ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
                / RelativeSpeedSquared, 0.0f, ValidationHorizon)
            : 0.0f;
          const FVector2f ClosestDelta =
            RelativePosition + RelativeVelocity * ClosestTime;
          const float ClosestDistance = ClosestDelta.Size();
          const float TargetDistance =
            Candidate.RequiredSeparationCm + RecoveryTargetMargin;
          if (ClosestDistance + Settings.ConstraintEpsilonCmps >= TargetDistance)
            continue;

          const FVector2f Normal = ClosestDistance > KINDA_SMALL_NUMBER
            ? NormalizeQ15(ClosestDelta)
            : StablePairDirection(Candidate.MinAgentId, Candidate.MaxAgentId);
          const float CorrectionSpeed = (TargetDistance - ClosestDistance)
            / FMath::Max(0.05f, ClosestTime);
          if (!FMath::IsFinite(CorrectionSpeed))
          {
            bRecoveryNumericallyValid = false;
            break;
          }

          float MinResponsibility = 0.5f;
          float MaxResponsibility = 0.5f;
          if (const FCrowdLocalPredictivePair* Existing =
            OutConflictPairs.FindByPredicate([&](const auto& Pair)
            {
              return Pair.MinAgentId == Candidate.MinAgentId
                && Pair.MaxAgentId == Candidate.MaxAgentId;
            }))
          {
            MinResponsibility = Existing->MinAgentResponsibility;
            MaxResponsibility = Existing->MaxAgentResponsibility;
          }
          else
          {
            const int32 MinIndex = Candidate.MinAgentIndex;
            const int32 MaxIndex = Candidate.MaxAgentIndex;
            const bool bMaxGranted = NextAges[MaxIndex] > NextAges[MinIndex]
              || (NextAges[MaxIndex] == NextAges[MinIndex]
                && ProgressDeficit(SortedAgents[MaxIndex])
                  > ProgressDeficit(SortedAgents[MinIndex]))
              || (NextAges[MaxIndex] == NextAges[MinIndex]
                && ProgressDeficit(SortedAgents[MaxIndex])
                  == ProgressDeficit(SortedAgents[MinIndex])
                && SortedAgents[MaxIndex].AgentId < SortedAgents[MinIndex].AgentId);
            MinResponsibility = bMaxGranted
              ? 1.0f - Settings.GrantedResponsibility : Settings.GrantedResponsibility;
            MaxResponsibility = 1.0f - MinResponsibility;
          }
          RecoveryVelocities[Candidate.MinAgentIndex] =
            (RecoveryVelocities[Candidate.MinAgentIndex]
              - Normal * CorrectionSpeed * MinResponsibility)
            .GetClampedToMaxSize(SortedAgents[Candidate.MinAgentIndex].MaxSpeedCmps);
          RecoveryVelocities[Candidate.MaxAgentIndex] =
            (RecoveryVelocities[Candidate.MaxAgentIndex]
              + Normal * CorrectionSpeed * MaxResponsibility)
            .GetClampedToMaxSize(SortedAgents[Candidate.MaxAgentIndex].MaxSpeedCmps);
        }
        if (!bRecoveryNumericallyValid) break;
        for (const int32 AgentIndex : ComponentIndices)
          if (!ProjectVelocityToEnvironment(
            SortedAgents[AgentIndex], FlowConfig, Settings,
            RecoveryVelocities[AgentIndex], OutSummary.EnvironmentConstraintCount))
          {
            bRecoveryNumericallyValid = false;
            break;
          }
        if (!bRecoveryNumericallyValid) break;
      }
      if (!bRecoveryNumericallyValid) continue;

      double RecoveryObjective = 0.0;
      bool bGrantedProgressPreserved = true;
      float MaxProgressGain = 0.0f;
      for (const int32 AgentIndex : ComponentIndices)
      {
        RecoveryVelocities[AgentIndex] = Quantize(
          RecoveryVelocities[AgentIndex].GetClampedToMaxSize(
            SortedAgents[AgentIndex].MaxSpeedCmps), Settings.VelocityQuantumCmps);
        RecoveryResults[AgentIndex].Velocity = RecoveryVelocities[AgentIndex];
        RecoveryResults[AgentIndex].bValid = true;
        const FVector2f Difference = SortedAgents[AgentIndex].PreferredVelocity
          - RecoveryVelocities[AgentIndex];
        RecoveryObjective += static_cast<double>(Difference.SizeSquared());
        const FVector2f PreferredDirection =
          SortedAgents[AgentIndex].PreferredVelocity.GetSafeNormal();
        if (!PreferredDirection.IsNearlyZero())
        {
          const float BaselineProgress = FVector2f::DotProduct(
            OutResults[AgentIndex].Velocity, PreferredDirection);
          const float RecoveryProgress = FVector2f::DotProduct(
            RecoveryVelocities[AgentIndex], PreferredDirection);
          MaxProgressGain = FMath::Max(MaxProgressGain,
            RecoveryProgress - BaselineProgress);
          if (OutResults[AgentIndex].bGranted
            && RecoveryProgress + Settings.ConstraintEpsilonCmps < BaselineProgress)
            bGrantedProgressPreserved = false;
        }
      }
      if (!bGrantedProgressPreserved
        || RecoveryObjective + 0.5 >= BaselineObjective
        || !ValidateJointResult(
          SortedAgents, FlowConfig, Settings, Candidates, RecoveryResults)) continue;

      uint32 ComponentKey = 2166136261u;
      FCrowdLocalPredictiveComponentTrace RecoveryTrace;
      for (const int32 AgentIndex : ComponentIndices)
      {
        ComponentKey = HashInt(ComponentKey, SortedAgents[AgentIndex].AgentId);
        RecoveryTrace.AgentIds.Add(SortedAgents[AgentIndex].AgentId);
        RecoveryTrace.PreRecoveryVelocities.Add({
          SortedAgents[AgentIndex].AgentId, OutResults[AgentIndex].Velocity});
        OutResults[AgentIndex].Velocity = RecoveryVelocities[AgentIndex];
        OutResults[AgentIndex].bAdjusted = !OutResults[AgentIndex].Velocity.Equals(
          Quantize(SortedAgents[AgentIndex].PreferredVelocity.GetClampedToMaxSize(
            SortedAgents[AgentIndex].MaxSpeedCmps), Settings.VelocityQuantumCmps), 0.0f);
        RecoveryTrace.RecoveredVelocities.Add({
          SortedAgents[AgentIndex].AgentId, OutResults[AgentIndex].Velocity});
        RecoveryTrace.FinalVelocities.Add({
          SortedAgents[AgentIndex].AgentId, OutResults[AgentIndex].Velocity});
      }
      RecoveryTrace.ComponentKey = ComponentKey;
      RecoveryTrace.bJointPreferredRecoveryApplied = true;
      ++OutSummary.JointPreferredRecoveryComponentCount;
      OutSummary.JointPreferredRecoveryAgentCount += ComponentIndices.Num();
      OutSummary.JointPreferredRecoveryMaxGainCmps = FMath::Max(
        OutSummary.JointPreferredRecoveryMaxGainCmps, MaxProgressGain);
      if (OutTrace)
      {
        const int32 ExistingTraceIndex = OutTrace->Components.IndexOfByPredicate(
          [&](const FCrowdLocalPredictiveComponentTrace& Existing)
          { return Existing.ComponentKey == ComponentKey; });
        if (ExistingTraceIndex == INDEX_NONE)
          OutTrace->Components.Add(MoveTemp(RecoveryTrace));
        else
        {
          auto& Existing = OutTrace->Components[ExistingTraceIndex];
          Existing.bJointPreferredRecoveryApplied = true;
          Existing.PreRecoveryVelocities = MoveTemp(RecoveryTrace.PreRecoveryVelocities);
          Existing.RecoveredVelocities = MoveTemp(RecoveryTrace.RecoveredVelocities);
          Existing.FinalVelocities = MoveTemp(RecoveryTrace.FinalVelocities);
        }
      }
    }
  }

  for (int32 GlobalJointPass = 0;
    GlobalJointPass < FMath::Max(1, SortedAgents.Num()); ++GlobalJointPass)
  {
    const bool bNeedsJointResolution =
      OutResults.ContainsByPredicate([](const auto& Result) { return !Result.bValid; })
      || Candidates.ContainsByPredicate([&](const auto& Pair) { return !IsPairSafe(Pair); });
    if (!bNeedsJointResolution) break;
    for (const FCrowdLocalPredictivePair& Candidate : Candidates)
    {
      if (IsPairSafe(Candidate)
        || OutConflictPairs.ContainsByPredicate([&](const auto& Existing)
          {
            return Existing.MinAgentId == Candidate.MinAgentId
              && Existing.MaxAgentId == Candidate.MaxAgentId;
          })) continue;
      FCrowdLocalPredictivePair Added = Candidate;
      const int32 MinIndex = Added.MinAgentIndex;
      const int32 MaxIndex = Added.MaxAgentIndex;
      const bool bMaxGranted = NextAges[MaxIndex] > NextAges[MinIndex]
        || (NextAges[MaxIndex] == NextAges[MinIndex]
          && ProgressDeficit(SortedAgents[MaxIndex])
            > ProgressDeficit(SortedAgents[MinIndex]))
        || (NextAges[MaxIndex] == NextAges[MinIndex]
          && ProgressDeficit(SortedAgents[MaxIndex])
            == ProgressDeficit(SortedAgents[MinIndex])
          && SortedAgents[MaxIndex].AgentId < SortedAgents[MinIndex].AgentId);
      Added.MinAgentResponsibility = bMaxGranted
        ? 1.0f - Settings.GrantedResponsibility : Settings.GrantedResponsibility;
      Added.MaxAgentResponsibility = 1.0f - Added.MinAgentResponsibility;
      OutConflictPairs.Add(Added);
    }
    OutConflictPairs.Sort([](const auto& A, const auto& B)
    {
      if (A.MinAgentId != B.MinAgentId) return A.MinAgentId < B.MinAgentId;
      return A.MaxAgentId < B.MaxAgentId;
    });
    OutSummary.ConflictPairCount = OutConflictPairs.Num();
    bool bResolvedAnyComponent = false;
    TArray<TArray<int32>> JointAdjacency;
    JointAdjacency.SetNum(SortedAgents.Num());
    for (const FCrowdLocalPredictivePair& Pair : OutConflictPairs)
    {
      JointAdjacency[Pair.MinAgentIndex].AddUnique(Pair.MaxAgentIndex);
      JointAdjacency[Pair.MaxAgentIndex].AddUnique(Pair.MinAgentIndex);
    }
    for (TArray<int32>& Neighbors : JointAdjacency)
      Neighbors.Sort([&](const int32 A, const int32 B)
      { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });

    TArray<bool> JointVisited;
    JointVisited.Init(false, SortedAgents.Num());
    for (int32 Root = 0; Root < SortedAgents.Num(); ++Root)
    {
      if (JointVisited[Root]) continue;
      TArray<int32> ComponentIndices = {Root};
      JointVisited[Root] = true;
      for (int32 Read = 0; Read < ComponentIndices.Num(); ++Read)
        for (const int32 Neighbor : JointAdjacency[ComponentIndices[Read]])
        {
          if (JointVisited[Neighbor]) continue;
          JointVisited[Neighbor] = true;
          ComponentIndices.Add(Neighbor);
        }
      ComponentIndices.Sort([&](const int32 A, const int32 B)
      { return SortedAgents[A].AgentId < SortedAgents[B].AgentId; });

      bool bComponentNeedsJoint = ComponentIndices.ContainsByPredicate(
        [&](const int32 Index) { return !OutResults[Index].bValid; });
      if (!bComponentNeedsJoint)
        for (const FCrowdLocalPredictivePair& Pair : Candidates)
          if (ComponentIndices.Contains(Pair.MinAgentIndex)
            && ComponentIndices.Contains(Pair.MaxAgentIndex) && !IsPairSafe(Pair))
          {
            bComponentNeedsJoint = true;
            break;
          }
      if (!bComponentNeedsJoint) continue;

      TArray<FVector2f> JointVelocities;
      JointVelocities.SetNum(SortedAgents.Num());
      for (const int32 AgentIndex : ComponentIndices)
        JointVelocities[AgentIndex] = OutResults[AgentIndex].bValid
          ? OutResults[AgentIndex].Velocity
          : SortedAgents[AgentIndex].PreferredVelocity.GetClampedToMaxSize(
              SortedAgents[AgentIndex].MaxSpeedCmps);

      bool bJointNumericallyValid = true;
      for (int32 Iteration = 0;
        Iteration < FMath::Max(1, Settings.JointIterationCount); ++Iteration)
      {
        for (const FCrowdLocalPredictivePair& Pair : OutConflictPairs)
        {
          if (!ComponentIndices.Contains(Pair.MinAgentIndex)
            || !ComponentIndices.Contains(Pair.MaxAgentIndex)) continue;
          FCrowdVelocityHalfPlane MinHalfPlane;
          FCrowdVelocityHalfPlane MaxHalfPlane;
          if (!BuildPairHalfPlane(
              SortedAgents[Pair.MinAgentIndex], SortedAgents[Pair.MaxAgentIndex],
              Pair.MinAgentResponsibility, Settings, 0, MinHalfPlane)
            || !BuildPairHalfPlane(
              SortedAgents[Pair.MaxAgentIndex], SortedAgents[Pair.MinAgentIndex],
              Pair.MaxAgentResponsibility, Settings, 0, MaxHalfPlane))
          {
            bJointNumericallyValid = false;
            break;
          }
          const FVector2f Normal = MinHalfPlane.Normal;
          const float RequiredRelativeSpeed = FVector2f::DotProduct(
            MinHalfPlane.Point - MaxHalfPlane.Point, Normal);
          const float CurrentRelativeSpeed = FVector2f::DotProduct(
            JointVelocities[Pair.MinAgentIndex]
              - JointVelocities[Pair.MaxAgentIndex], Normal);
          const float CorrectionSpeed = RequiredRelativeSpeed - CurrentRelativeSpeed;
          if (CorrectionSpeed <= Settings.ConstraintEpsilonCmps) continue;
          JointVelocities[Pair.MinAgentIndex] =
            (JointVelocities[Pair.MinAgentIndex]
              + Normal * CorrectionSpeed * Pair.MinAgentResponsibility)
            .GetClampedToMaxSize(SortedAgents[Pair.MinAgentIndex].MaxSpeedCmps);
          JointVelocities[Pair.MaxAgentIndex] =
            (JointVelocities[Pair.MaxAgentIndex]
              - Normal * CorrectionSpeed * Pair.MaxAgentResponsibility)
            .GetClampedToMaxSize(SortedAgents[Pair.MaxAgentIndex].MaxSpeedCmps);
        }
        if (!bJointNumericallyValid) break;
        for (const int32 AgentIndex : ComponentIndices)
          if (!ProjectVelocityToEnvironment(
            SortedAgents[AgentIndex], FlowConfig, Settings,
            JointVelocities[AgentIndex], OutSummary.EnvironmentConstraintCount))
          {
            bJointNumericallyValid = false;
            break;
          }
        if (!bJointNumericallyValid) break;
      }
      int32 GrantedAgentId = GrantByAgent[ComponentIndices[0]];
      if (GrantedAgentId == INDEX_NONE)
      {
        int32 BestIndex = ComponentIndices[0];
        for (const int32 Index : ComponentIndices)
        {
          const bool bBetter = NextAges[Index] > NextAges[BestIndex]
            || (NextAges[Index] == NextAges[BestIndex]
              && ProgressDeficit(SortedAgents[Index])
                > ProgressDeficit(SortedAgents[BestIndex]))
            || (NextAges[Index] == NextAges[BestIndex]
              && ProgressDeficit(SortedAgents[Index])
                == ProgressDeficit(SortedAgents[BestIndex])
              && SortedAgents[Index].AgentId < SortedAgents[BestIndex].AgentId);
          if (bBetter) BestIndex = Index;
        }
        GrantedAgentId = SortedAgents[BestIndex].AgentId;
      }
      FCrowdLocalPredictiveComponentTrace ComponentTrace;
      for (const int32 AgentIndex : ComponentIndices)
      {
        ComponentTrace.AgentIds.Add(SortedAgents[AgentIndex].AgentId);
        ComponentTrace.ComponentKey = HashInt(
          ComponentTrace.ComponentKey, SortedAgents[AgentIndex].AgentId);
        ComponentTrace.JointProjectedVelocities.Add({
          SortedAgents[AgentIndex].AgentId, JointVelocities[AgentIndex]});
      }
      ComponentTrace.GrantedAgentId = GrantedAgentId;
      FVector2f CommonVelocity = FVector2f::ZeroVector;
      const bool bCommonVelocityValid = FindComponentCommonVelocity(
        SortedAgents, ComponentIndices, GrantedAgentId, FlowConfig, Settings,
        CommonVelocity, OutSummary.EnvironmentConstraintCount);
      if (!bCommonVelocityValid)
        CommonVelocity = FVector2f::ZeroVector;
      ComponentTrace.CommonVelocity = CommonVelocity;
      ComponentTrace.bCommonVelocityValid = bCommonVelocityValid;

      TArray<FVector2f> BlendedVelocities;
      BlendedVelocities.SetNum(SortedAgents.Num());
      const auto IsBlendSafe = [&](const int32 AlphaQ15)
      {
        const float Alpha = static_cast<float>(AlphaQ15) / 32767.0f;
        for (const int32 AgentIndex : ComponentIndices)
        {
          BlendedVelocities[AgentIndex] = Quantize(
            CommonVelocity + (JointVelocities[AgentIndex] - CommonVelocity) * Alpha,
            Settings.VelocityQuantumCmps).GetClampedToMaxSize(
              SortedAgents[AgentIndex].MaxSpeedCmps);
          const FVector2f End = SortedAgents[AgentIndex].Position
            + BlendedVelocities[AgentIndex] * Settings.FixedStepSeconds;
          if (!IsInsideBounds(SortedAgents[AgentIndex], FlowConfig,
              SortedAgents[AgentIndex].Position)
            || !IsInsideBounds(SortedAgents[AgentIndex], FlowConfig, End)) return false;
          FEnvironmentHit Hit;
          if (FindFirstEnvironmentHit(SortedAgents[AgentIndex], FlowConfig,
            BlendedVelocities[AgentIndex], Settings.FixedStepSeconds, Hit)) return false;
        }
        for (const FCrowdLocalPredictivePair& Pair : Candidates)
        {
          if (!ComponentIndices.Contains(Pair.MinAgentIndex)
            || !ComponentIndices.Contains(Pair.MaxAgentIndex)) continue;
          const FVector2f RelativePosition =
            SortedAgents[Pair.MaxAgentIndex].Position
              - SortedAgents[Pair.MinAgentIndex].Position;
          const FVector2f RelativeVelocity =
            BlendedVelocities[Pair.MaxAgentIndex]
              - BlendedVelocities[Pair.MinAgentIndex];
          const float SpeedSquared = RelativeVelocity.SizeSquared();
          const float Time = SpeedSquared > KINDA_SMALL_NUMBER
            ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
                / SpeedSquared, 0.0f, ValidationHorizon)
            : 0.0f;
          if ((RelativePosition + RelativeVelocity * Time).Size() + 0.5f
            < Pair.RequiredSeparationCm) return false;
        }
        return true;
      };
      if (!IsBlendSafe(0)) continue;
      int32 SafeAlphaQ15 = 0;
      int32 UnsafeAlphaQ15 = 32767;
      ComponentTrace.bFullJointVelocitySafe = IsBlendSafe(UnsafeAlphaQ15);
      if (ComponentTrace.bFullJointVelocitySafe)
        SafeAlphaQ15 = UnsafeAlphaQ15;
      else
        while (SafeAlphaQ15 + 1 < UnsafeAlphaQ15)
        {
          const int32 CandidateAlpha =
            SafeAlphaQ15 + (UnsafeAlphaQ15 - SafeAlphaQ15) / 2;
          if (IsBlendSafe(CandidateAlpha)) SafeAlphaQ15 = CandidateAlpha;
          else UnsafeAlphaQ15 = CandidateAlpha;
        }
      IsBlendSafe(SafeAlphaQ15);
      ComponentTrace.SafeAlphaQ15 = SafeAlphaQ15;
      for (const int32 AgentIndex : ComponentIndices)
      {
        JointVelocities[AgentIndex] = BlendedVelocities[AgentIndex];
        ComponentTrace.FinalVelocities.Add({
          SortedAgents[AgentIndex].AgentId, JointVelocities[AgentIndex]});
      }

      if (OutTrace)
      {
        const int32 ExistingIndex = OutTrace->Components.IndexOfByPredicate(
          [&](const FCrowdLocalPredictiveComponentTrace& Existing)
          { return Existing.ComponentKey == ComponentTrace.ComponentKey; });
        if (ExistingIndex == INDEX_NONE) OutTrace->Components.Add(MoveTemp(ComponentTrace));
        else OutTrace->Components[ExistingIndex] = MoveTemp(ComponentTrace);
      }

      for (const int32 AgentIndex : ComponentIndices)
      {
        FCrowdLocalPredictiveResult& Result = OutResults[AgentIndex];
        Result.Velocity = Quantize(
          JointVelocities[AgentIndex].GetClampedToMaxSize(
            SortedAgents[AgentIndex].MaxSpeedCmps), Settings.VelocityQuantumCmps);
        Result.bValid = true;
        Result.bAdjusted = !Result.Velocity.Equals(
          Quantize(SortedAgents[AgentIndex].PreferredVelocity.GetClampedToMaxSize(
            SortedAgents[AgentIndex].MaxSpeedCmps), Settings.VelocityQuantumCmps), 0.0f);
      }
      ++OutSummary.JointComponentResolutionCount;
      bResolvedAnyComponent = true;
    }
    if (!bResolvedAnyComponent) break;
  }

  OutSummary.InfeasibleAgentCount = 0;
  OutSummary.QuantizationFailureCount = 0;
  OutSummary.ProcessedAgentCount = OutResults.Num();
  for (const auto& Result : OutResults)
  {
    OutSummary.InfeasibleAgentCount += Result.bValid ? 0 : 1;
    OutSummary.AdjustedAgentCount += Result.bAdjusted ? 1 : 0;
    OutSummary.GrantedAgentCount += Result.bGranted ? 1 : 0;
    OutSummary.YieldingAgentCount += Result.bYielding ? 1 : 0;
  }
  OutSummary.bValid = OutSummary.InfeasibleAgentCount == 0
    && ValidateJointResult(SortedAgents, FlowConfig, Settings, Candidates, OutResults);
  if (!OutSummary.bValid && OutSummary.InfeasibleAgentCount == 0)
    ++OutSummary.JointValidationFailureCount;

  uint32 Hash = 2166136261u;
  Hash = HashFloat(Hash, Settings.FixedStepSeconds, 0.0001f);
  Hash = HashFloat(Hash, Settings.TimeHorizonSeconds, 0.001f);
  Hash = HashFloat(Hash, Settings.VelocityQuantumCmps, 0.001f);
  Hash = HashFloat(Hash, Settings.GrantedResponsibility, 0.001f);
  Hash = HashInt(Hash, Settings.JointIterationCount);
  Hash = HashInt(Hash, OutSummary.JointComponentResolutionCount);
  Hash = HashInt(Hash, OutSummary.CoherentTranslationComponentCount);
  Hash = HashInt(Hash, OutSummary.CoherentTranslationAgentCount);
  Hash = HashFloat(Hash, OutSummary.CoherentTranslationMaxCmps, 1.0f);
  Hash = HashInt(Hash, OutSummary.JointPreferredRecoveryComponentCount);
  Hash = HashInt(Hash, OutSummary.JointPreferredRecoveryAgentCount);
  Hash = HashFloat(Hash, OutSummary.JointPreferredRecoveryMaxGainCmps, 1.0f);
  const bool bHasExplicitInteractionLayers =
    SortedAgents.ContainsByPredicate([](const auto& Agent)
    {
      return Agent.InteractionLayer != 0;
    });
  if (bHasExplicitInteractionLayers)
    Hash = HashInt(Hash, 0x4c415952);
  for (const auto& Agent : SortedAgents)
  {
    Hash = HashInt(Hash, Agent.AgentId);
    if (bHasExplicitInteractionLayers)
      Hash = HashInt(
        Hash, static_cast<int32>(Agent.InteractionLayer));
    Hash = HashFloat(Hash, Agent.Position.X, 1.0f);
    Hash = HashFloat(Hash, Agent.Position.Y, 1.0f);
    Hash = HashFloat(Hash, Agent.Velocity.X, 1.0f);
    Hash = HashFloat(Hash, Agent.Velocity.Y, 1.0f);
    Hash = HashFloat(Hash, Agent.PreferredVelocity.X, 1.0f);
    Hash = HashFloat(Hash, Agent.PreferredVelocity.Y, 1.0f);
    Hash = HashInt(Hash, Agent.BlockedAgeSteps);
  }
  for (const auto& Pair : OutConflictPairs)
  {
    Hash = HashInt(Hash, Pair.MinAgentId);
    Hash = HashInt(Hash, Pair.MaxAgentId);
    Hash = HashFloat(Hash, Pair.ClosestTimeSeconds, 0.001f);
    Hash = HashFloat(Hash, Pair.MinAgentResponsibility, 0.001f);
    Hash = HashFloat(Hash, Pair.MaxAgentResponsibility, 0.001f);
  }
  for (const auto& State : OutGrantStates)
  {
    Hash = HashInt(Hash, static_cast<int32>(State.ComponentKey));
    Hash = HashInt(Hash, State.GrantedAgentId);
    Hash = HashInt(Hash, State.GrantEpoch);
    Hash = HashInt(Hash, State.RemainingSteps);
  }
  for (const auto& Result : OutResults)
  {
    Hash = HashInt(Hash, Result.AgentId);
    Hash = HashFloat(Hash, Result.Velocity.X, 1.0f);
    Hash = HashFloat(Hash, Result.Velocity.Y, 1.0f);
    Hash = HashInt(Hash, Result.NextBlockedAgeSteps);
    Hash = HashInt(Hash, Result.bValid ? 1 : 0);
  }
  OutSummary.CandidateHash = Hash;
  if (OutTrace)
  {
    OutTrace->InitialIndependentResults.Sort([](const auto& A, const auto& B)
    { return A.AgentId < B.AgentId; });
    OutTrace->CompletedIndependentResults.Sort([](const auto& A, const auto& B)
    { return A.AgentId < B.AgentId; });
    OutTrace->Components.Sort([](const auto& A, const auto& B)
    { return A.ComponentKey < B.ComponentKey; });
  }
}

bool FCrowdLocalPredictiveInteractionKernel::BuildComponentFixture(
  const int32 FixedStepIndex,
  const TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
  const FCrowdSharedFlowFieldConfig& FlowConfig,
  const FCrowdLocalPredictiveSettings& Settings,
  const TConstArrayView<FCrowdLocalPredictiveGrantState> PreviousGrantStates,
  const TConstArrayView<FCrowdLocalPredictivePair> ConflictPairs,
  const TConstArrayView<FCrowdLocalPredictiveGrantState> GrantStates,
  const TConstArrayView<FCrowdLocalPredictiveResult> Results,
  const FCrowdLocalPredictiveSummary& Summary,
  const FCrowdLocalPredictiveDiagnosticTrace& Trace,
  const TConstArrayView<int32> WitnessAgentIds,
  FCrowdLocalPredictiveComponentFixture& OutFixture)
{
  OutFixture = FCrowdLocalPredictiveComponentFixture();
  if (FixedStepIndex < 0 || WitnessAgentIds.IsEmpty()) return false;

  TArray<int32> SortedWitnesses(WitnessAgentIds);
  SortedWitnesses.Sort();
  SortedWitnesses.SetNum(Algo::Unique(SortedWitnesses));
  TSet<int32> IncludedAgentIds;
  for (const int32 AgentId : SortedWitnesses) IncludedAgentIds.Add(AgentId);
  bool bExpanded = true;
  while (bExpanded)
  {
    bExpanded = false;
    for (const FCrowdLocalPredictivePair& Pair : ConflictPairs)
    {
      const bool bMinIncluded = IncludedAgentIds.Contains(Pair.MinAgentId);
      const bool bMaxIncluded = IncludedAgentIds.Contains(Pair.MaxAgentId);
      if (bMinIncluded == bMaxIncluded) continue;
      IncludedAgentIds.Add(bMinIncluded ? Pair.MaxAgentId : Pair.MinAgentId);
      bExpanded = true;
    }
  }
  for (const FCrowdLocalPredictiveComponentTrace& Component : Trace.Components)
  {
    if (!Component.AgentIds.ContainsByPredicate(
      [&](const int32 AgentId) { return SortedWitnesses.Contains(AgentId); })) continue;
    for (const int32 AgentId : Component.AgentIds) IncludedAgentIds.Add(AgentId);
  }
  if (IncludedAgentIds.Num() == SortedWitnesses.Num())
  {
    TSet<uint32> ComponentKeys;
    for (const FCrowdLocalPredictiveResult& Result : Results)
      if (SortedWitnesses.Contains(Result.AgentId) && Result.ComponentKey != 0)
        ComponentKeys.Add(Result.ComponentKey);
    for (const FCrowdLocalPredictiveResult& Result : Results)
      if (ComponentKeys.Contains(Result.ComponentKey)) IncludedAgentIds.Add(Result.AgentId);
  }
  for (const int32 AgentId : SortedWitnesses)
    if (!Agents.ContainsByPredicate([&](const auto& Agent) { return Agent.AgentId == AgentId; }))
      return false;
  if (IncludedAgentIds.Num() < 2 || IncludedAgentIds.Num() > 20) return false;

  OutFixture.FixedStepIndex = FixedStepIndex;
  OutFixture.Settings = Settings;
  OutFixture.FlowConfig = FlowConfig;
  OutFixture.Summary = Summary;
  OutFixture.WitnessAgentIds = MoveTemp(SortedWitnesses);
  for (const auto& Agent : Agents)
    if (IncludedAgentIds.Contains(Agent.AgentId)) OutFixture.Agents.Add(Agent);
  OutFixture.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  if (OutFixture.Agents.Num() != IncludedAgentIds.Num()) return false;

  TMap<int32, int32> FixtureIndexByAgentId;
  for (int32 Index = 0; Index < OutFixture.Agents.Num(); ++Index)
    FixtureIndexByAgentId.Add(OutFixture.Agents[Index].AgentId, Index);
  for (const auto& Pair : ConflictPairs)
  {
    const int32* MinIndex = FixtureIndexByAgentId.Find(Pair.MinAgentId);
    const int32* MaxIndex = FixtureIndexByAgentId.Find(Pair.MaxAgentId);
    if (!MinIndex || !MaxIndex) continue;
    FCrowdLocalPredictivePair Copy = Pair;
    Copy.MinAgentIndex = *MinIndex;
    Copy.MaxAgentIndex = *MaxIndex;
    OutFixture.ConflictPairs.Add(Copy);
  }
  for (const auto& State : PreviousGrantStates)
    if (IncludedAgentIds.Contains(State.GrantedAgentId))
      OutFixture.PreviousGrantStates.Add(State);
  for (const auto& State : GrantStates)
    if (IncludedAgentIds.Contains(State.GrantedAgentId)) OutFixture.GrantStates.Add(State);
  for (const auto& Result : Results)
    if (IncludedAgentIds.Contains(Result.AgentId)) OutFixture.Results.Add(Result);
  for (const auto& Result : Trace.InitialIndependentResults)
    if (IncludedAgentIds.Contains(Result.AgentId))
      OutFixture.Trace.InitialIndependentResults.Add(Result);
  for (const auto& Result : Trace.CompletedIndependentResults)
    if (IncludedAgentIds.Contains(Result.AgentId))
      OutFixture.Trace.CompletedIndependentResults.Add(Result);
  for (const auto& Component : Trace.Components)
    if (Component.AgentIds.ContainsByPredicate(
      [&](const int32 AgentId) { return IncludedAgentIds.Contains(AgentId); }))
      OutFixture.Trace.Components.Add(Component);

  OutFixture.FlowConfig.ObstacleSpecs.Sort([](const auto& A, const auto& B)
  { return A.ObstacleId < B.ObstacleId; });
  OutFixture.ConflictPairs.Sort([](const auto& A, const auto& B)
  { return A.MinAgentId != B.MinAgentId ? A.MinAgentId < B.MinAgentId : A.MaxAgentId < B.MaxAgentId; });
  OutFixture.PreviousGrantStates.Sort([](const auto& A, const auto& B)
  { return A.ComponentKey != B.ComponentKey ? A.ComponentKey < B.ComponentKey : A.GrantedAgentId < B.GrantedAgentId; });
  OutFixture.GrantStates.Sort([](const auto& A, const auto& B)
  { return A.ComponentKey != B.ComponentKey ? A.ComponentKey < B.ComponentKey : A.GrantedAgentId < B.GrantedAgentId; });
  OutFixture.Results.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });

  uint32 Hash = 2166136261u;
  Hash = HashInt(Hash, FixedStepIndex);
  Hash = HashFloat(Hash, Settings.FixedStepSeconds, 0.0001f);
  Hash = HashFloat(Hash, Settings.TimeHorizonSeconds, 0.001f);
  Hash = HashFloat(Hash, Settings.SpatialCellSizeCm, 1.0f);
  Hash = HashFloat(Hash, Settings.VelocityQuantumCmps, 0.001f);
  Hash = HashFloat(Hash, Settings.ConstraintEpsilonCmps, 0.001f);
  Hash = HashFloat(Hash, Settings.RequestedProgressThresholdCmps, 1.0f);
  Hash = HashFloat(Hash, Settings.BlockedProgressThresholdCmps, 1.0f);
  Hash = HashFloat(Hash, Settings.GrantedResponsibility, 0.001f);
  Hash = HashInt(Hash, Settings.GrantDurationSteps);
  Hash = HashInt(Hash, Settings.JointIterationCount);
  Hash = HashInt(Hash, FlowConfig.Revision);
  Hash = HashFloat(Hash, FlowConfig.BoundsMin.X, 1.0f);
  Hash = HashFloat(Hash, FlowConfig.BoundsMin.Y, 1.0f);
  Hash = HashFloat(Hash, FlowConfig.BoundsMax.X, 1.0f);
  Hash = HashFloat(Hash, FlowConfig.BoundsMax.Y, 1.0f);
  for (const auto& Obstacle : OutFixture.FlowConfig.ObstacleSpecs)
  {
    Hash = HashInt(Hash, Obstacle.ObstacleId);
    Hash = HashFloat(Hash, Obstacle.Center.X, 1.0f);
    Hash = HashFloat(Hash, Obstacle.Center.Y, 1.0f);
    Hash = HashFloat(Hash, Obstacle.Extent.X, 1.0f);
    Hash = HashFloat(Hash, Obstacle.Extent.Y, 1.0f);
  }
  for (const auto& Agent : OutFixture.Agents)
  {
    Hash = HashInt(Hash, Agent.AgentId);
    Hash = HashFloat(Hash, Agent.Position.X, 1.0f); Hash = HashFloat(Hash, Agent.Position.Y, 1.0f);
    Hash = HashFloat(Hash, Agent.Velocity.X, 1.0f); Hash = HashFloat(Hash, Agent.Velocity.Y, 1.0f);
    Hash = HashFloat(Hash, Agent.PreferredVelocity.X, 1.0f); Hash = HashFloat(Hash, Agent.PreferredVelocity.Y, 1.0f);
    Hash = HashFloat(Hash, Agent.PhysicalRadiusCm, 1.0f);
    Hash = HashFloat(Hash, Agent.HardSafetyGapCm, 1.0f);
    Hash = HashFloat(Hash, Agent.MaxSpeedCmps, 1.0f);
    Hash = HashInt(Hash, Agent.BlockedAgeSteps);
  }
  for (const auto& Pair : OutFixture.ConflictPairs)
  {
    Hash = HashInt(Hash, Pair.MinAgentId); Hash = HashInt(Hash, Pair.MaxAgentId);
    Hash = HashFloat(Hash, Pair.ClosestTimeSeconds, 0.001f);
    Hash = HashFloat(Hash, Pair.PredictedSeparationCm, 0.01f);
    Hash = HashFloat(Hash, Pair.RequiredSeparationCm, 0.01f);
    Hash = HashFloat(Hash, Pair.MinAgentResponsibility, 0.001f);
  }
  const auto FoldResult = [&](const FCrowdLocalPredictiveResult& Result)
  {
    Hash = HashInt(Hash, Result.AgentId);
    Hash = HashFloat(Hash, Result.Velocity.X, 1.0f); Hash = HashFloat(Hash, Result.Velocity.Y, 1.0f);
    Hash = HashInt(Hash, Result.NeighborCount); Hash = HashInt(Hash, Result.ConstraintCount);
    Hash = HashInt(Hash, Result.NextBlockedAgeSteps); Hash = HashInt(Hash, Result.bValid ? 1 : 0);
  };
  for (const auto& Result : OutFixture.Trace.InitialIndependentResults) FoldResult(Result);
  for (const auto& Result : OutFixture.Trace.CompletedIndependentResults) FoldResult(Result);
  for (const auto& Component : OutFixture.Trace.Components)
  {
    Hash = HashInt(Hash, static_cast<int32>(Component.ComponentKey));
    Hash = HashInt(Hash, Component.GrantedAgentId);
    Hash = HashFloat(Hash, Component.CommonVelocity.X, 1.0f);
    Hash = HashFloat(Hash, Component.CommonVelocity.Y, 1.0f);
    Hash = HashInt(Hash, Component.SafeAlphaQ15);
    Hash = HashInt(Hash, Component.bCommonVelocityValid ? 1 : 0);
    Hash = HashInt(Hash, Component.bFullJointVelocitySafe ? 1 : 0);
    Hash = HashInt(Hash, Component.bCoherentTranslationApplied ? 1 : 0);
    Hash = HashFloat(Hash, Component.CoherentTranslation.X, 1.0f);
    Hash = HashFloat(Hash, Component.CoherentTranslation.Y, 1.0f);
    Hash = HashInt(Hash, Component.bJointPreferredRecoveryApplied ? 1 : 0);
    for (const auto& Velocity : Component.PreRecoveryVelocities)
    {
      Hash = HashInt(Hash, Velocity.AgentId);
      Hash = HashFloat(Hash, Velocity.Velocity.X, 1.0f);
      Hash = HashFloat(Hash, Velocity.Velocity.Y, 1.0f);
    }
    for (const auto& Velocity : Component.RecoveredVelocities)
    {
      Hash = HashInt(Hash, Velocity.AgentId);
      Hash = HashFloat(Hash, Velocity.Velocity.X, 1.0f);
      Hash = HashFloat(Hash, Velocity.Velocity.Y, 1.0f);
    }
    for (const auto& Velocity : Component.PreTranslationVelocities)
    { Hash = HashInt(Hash, Velocity.AgentId); Hash = HashFloat(Hash, Velocity.Velocity.X, 1.0f); Hash = HashFloat(Hash, Velocity.Velocity.Y, 1.0f); }
    for (const auto& Velocity : Component.JointProjectedVelocities)
    { Hash = HashInt(Hash, Velocity.AgentId); Hash = HashFloat(Hash, Velocity.Velocity.X, 1.0f); Hash = HashFloat(Hash, Velocity.Velocity.Y, 1.0f); }
    for (const auto& Velocity : Component.FinalVelocities)
    { Hash = HashInt(Hash, Velocity.AgentId); Hash = HashFloat(Hash, Velocity.Velocity.X, 1.0f); Hash = HashFloat(Hash, Velocity.Velocity.Y, 1.0f); }
  }
  for (const int32 AgentId : OutFixture.WitnessAgentIds) Hash = HashInt(Hash, AgentId);
  OutFixture.StableHash = Hash;
  OutFixture.bValid = true;
  return true;
}
