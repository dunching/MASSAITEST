#include "Mass/CrowdDemoElasticShadowKernel.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;

  uint32 Fold(const uint32 Hash, const int32 Value)
  {
    return (Hash ^ static_cast<uint32>(Value)) * 16777619u;
  }

  int32 Q(const float Value)
  {
    return FMath::RoundToInt(Value);
  }

  FVector2f QuantizeVelocity(FVector2f Velocity, const float Quantum,
    const float MaxSpeed)
  {
    if (Velocity.SizeSquared() > FMath::Square(MaxSpeed) && !Velocity.IsNearlyZero())
      Velocity = Velocity.GetSafeNormal() * MaxSpeed;
    const float SafeQuantum = FMath::Max(0.001f, Quantum);
    return FVector2f(FMath::RoundToFloat(Velocity.X / SafeQuantum) * SafeQuantum,
      FMath::RoundToFloat(Velocity.Y / SafeQuantum) * SafeQuantum);
  }

  int32 StageIndex(const ECrowdDemoElasticShadowStage Stage)
  {
    return static_cast<int32>(Stage);
  }

  const FCrowdDemoElasticShadowAgentStage* FindStageAgent(
    const FCrowdDemoElasticShadowBranchResult& Branch,
    const ECrowdDemoElasticShadowStage Stage, const int32 AgentId)
  {
    return Branch.Stages[StageIndex(Stage)].FindByPredicate(
      [AgentId](const FCrowdDemoElasticShadowAgentStage& Item)
      { return Item.AgentId == AgentId; });
  }

  FVector2f RecoveryTarget(const FCrowdDemoElasticShadowAgentInput& Agent)
  {
    return Agent.SteeringState == ECrowdDemoPursuitSteeringState::Holding
      || Agent.SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold
      ? Agent.HoldingLocation : Agent.AssignedPosition;
  }

  bool IsRecoveryEligible(const FCrowdDemoElasticShadowAgentInput& Agent)
  {
    return Agent.bHasAssignment
      && Agent.SteeringState != ECrowdDemoPursuitSteeringState::Pursuit
      && Agent.SteeringState != ECrowdDemoPursuitSteeringState::Reacquire;
  }

  void BuildStageSummary(
    const FCrowdDemoElasticShadowStepInput& Input,
    const ECrowdDemoElasticShadowStage Stage,
    TConstArrayView<FCrowdDemoElasticShadowAgentStage> Agents,
    FCrowdDemoElasticShadowStageSummary& Out)
  {
    Out = {};
    Out.Stage = Stage;
    TMap<int32, const FCrowdDemoElasticShadowAgentInput*> Inputs;
    for (const FCrowdDemoElasticShadowAgentInput& Agent : Input.Agents)
      Inputs.Add(Agent.Agent.AgentId, &Agent);

    uint32 Hash = Fold(FnvOffset, StageIndex(Stage));
    for (int32 A = 0; A < Agents.Num(); ++A)
    {
      const FCrowdDemoElasticShadowAgentInput* const* InputAgent =
        Inputs.Find(Agents[A].AgentId);
      if (!InputAgent) continue;
      const float RequiredTarget = Input.Environment.TargetExclusionRadiusCm
        + (*InputAgent)->Agent.PhysicalRadiusCm;
      if (Input.Environment.bValidateTargetExclusion
        && (Agents[A].Position - Input.Environment.TargetLocation).Size()
          < RequiredTarget - 0.5f)
        ++Out.TargetViolationCount;

      if ((*InputAgent)->Agent.bTransitSource)
      {
        FVector2f Direction = (*InputAgent)->Agent.TransitDirection.GetSafeNormal();
        if (Direction.IsNearlyZero())
          Direction = (*InputAgent)->Agent.BasePreferredVelocity.GetSafeNormal();
        const int32 Desired = FMath::Max(0, Q(FVector2f::DotProduct(
          (*InputAgent)->Agent.BasePreferredVelocity, Direction)));
        if (Desired > 0)
        {
          ++Out.ValidSourceSampleCount;
          Out.DesiredSourceForwardCmps += Desired;
          Out.ActualSourceForwardCmps += FMath::Max(0, Q(FVector2f::DotProduct(
            Agents[A].Velocity, Direction)));
        }
        else
        {
          ++Out.ZeroDesiredSourceSampleCount;
        }
      }

      Hash = Fold(Hash, Agents[A].AgentId);
      Hash = Fold(Hash, Q(Agents[A].Position.X));
      Hash = Fold(Hash, Q(Agents[A].Position.Y));
      Hash = Fold(Hash, Q(Agents[A].Velocity.X));
      Hash = Fold(Hash, Q(Agents[A].Velocity.Y));
      for (int32 B = A + 1; B < Agents.Num(); ++B)
      {
        const FCrowdDemoElasticShadowAgentInput* const* OtherInput =
          Inputs.Find(Agents[B].AgentId);
        if (!OtherInput) continue;
        const float Required = (*InputAgent)->Agent.PhysicalRadiusCm
          + (*OtherInput)->Agent.PhysicalRadiusCm
          + Input.ElasticSettings.HardSafetyGapCm;
        const float Distance = (Agents[A].Position - Agents[B].Position).Size();
        const float Margin = Distance - Required;
        Out.MinimumHardPairMarginCm = FMath::Min(
          Out.MinimumHardPairMarginCm, Margin);
        Out.MaximumHardPairPenetrationCm = FMath::Max(
          Out.MaximumHardPairPenetrationCm, -Margin);
        if (Margin < -0.5f)
        {
          ++Out.HardPairViolationCount;
          if (Out.FirstHardPair.MinAgentId == INDEX_NONE)
          {
            Out.FirstHardPair.MinAgentId = FMath::Min(Agents[A].AgentId, Agents[B].AgentId);
            Out.FirstHardPair.MaxAgentId = FMath::Max(Agents[A].AgentId, Agents[B].AgentId);
            Out.FirstHardPair.RequiredClearanceCm = Required;
            Out.FirstHardPair.ActualDistanceCm = Distance;
            Out.FirstHardPair.MarginCm = Margin;
          }
        }
      }
    }
    if (Out.MinimumHardPairMarginCm == MAX_flt)
      Out.MinimumHardPairMarginCm = 0.0f;
    Out.SourceForwardRatioQ15 = Out.DesiredSourceForwardCmps > 0
      ? FMath::Clamp(FMath::RoundToInt(static_cast<double>(Out.ActualSourceForwardCmps)
        * 32767.0 / static_cast<double>(Out.DesiredSourceForwardCmps)), 0, 131068)
      : 32767;
    Hash = Fold(Hash, Out.HardPairViolationCount);
    Hash = Fold(Hash, Out.TargetViolationCount);
    Hash = Fold(Hash, Out.SourceForwardRatioQ15);
    Out.StableHash = Hash;
  }

  void MakeStage(
    const ECrowdDemoElasticShadowStage Stage,
    TConstArrayView<FCrowdDemoElasticShadowAgentInput> Inputs,
    TConstArrayView<FVector2f> Positions,
    TConstArrayView<FVector2f> Velocities,
    TConstArrayView<FVector2f> Preferred,
    TArray<FCrowdDemoElasticShadowAgentStage>& Out)
  {
    Out.Reset(Inputs.Num());
    for (int32 Index = 0; Index < Inputs.Num(); ++Index)
    {
      FCrowdDemoElasticShadowAgentStage& Item = Out.AddDefaulted_GetRef();
      Item.AgentId = Inputs[Index].Agent.AgentId;
      Item.Position = Positions[Index];
      Item.Velocity = Velocities[Index];
      Item.PreferredVelocity = Preferred[Index];
      uint32 Hash = Fold(FnvOffset, StageIndex(Stage));
      Hash = Fold(Hash, Item.AgentId);
      Hash = Fold(Hash, Q(Item.Position.X)); Hash = Fold(Hash, Q(Item.Position.Y));
      Hash = Fold(Hash, Q(Item.Velocity.X)); Hash = Fold(Hash, Q(Item.Velocity.Y));
      Hash = Fold(Hash, Q(Item.PreferredVelocity.X));
      Hash = Fold(Hash, Q(Item.PreferredVelocity.Y));
      Item.StableHash = Hash;
    }
  }

  const FCrowdDemoOrcaResult* FindOrca(
    TConstArrayView<FCrowdDemoOrcaResult> Results, const int32 AgentId)
  {
    return Results.FindByPredicate([AgentId](const FCrowdDemoOrcaResult& Result)
      { return Result.AgentId == AgentId; });
  }

  const FCrowdDemoHardSeparationPbdResult* FindPbd(
    TConstArrayView<FCrowdDemoHardSeparationPbdResult> Results, const int32 AgentId)
  {
    return Results.FindByPredicate([AgentId](const FCrowdDemoHardSeparationPbdResult& Result)
      { return Result.AgentId == AgentId; });
  }
}

bool FCrowdDemoElasticShadowKernel::PolishReprojectHardPairs(
  const FCrowdDemoElasticShadowStepInput& SourceInput,
  const TConstArrayView<FCrowdDemoElasticShadowSafetyPolishAgent> SourceAgents,
  FCrowdDemoElasticShadowSafetyPolishSummary& OutSummary)
{
  OutSummary = {};
  FCrowdDemoElasticShadowStepInput Input = SourceInput;
  Input.Agents.Sort([](const auto& A, const auto& B)
    { return A.Agent.AgentId < B.Agent.AgentId; });
  OutSummary.Agents = TArray<FCrowdDemoElasticShadowSafetyPolishAgent>(SourceAgents);
  OutSummary.Agents.Sort([](const auto& A, const auto& B)
    { return A.AgentId < B.AgentId; });
  if (Input.Agents.Num() != OutSummary.Agents.Num() || Input.Agents.IsEmpty()) return false;
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
    if (Input.Agents[Index].Agent.AgentId != OutSummary.Agents[Index].AgentId) return false;

  struct FScore
  {
    int32 ViolationCount = 0;
    float MaximumPenetrationCm = 0.0f;
    double TotalPenetrationCm = 0.0;
  };
  const auto Score = [&](TConstArrayView<FCrowdDemoElasticShadowSafetyPolishAgent> Agents)
  {
    FScore Result;
    for (int32 A = 0; A < Agents.Num(); ++A)
      for (int32 B = A + 1; B < Agents.Num(); ++B)
      {
        const float Required = Input.Agents[A].Agent.PhysicalRadiusCm
          + Input.Agents[B].Agent.PhysicalRadiusCm
          + Input.ElasticSettings.HardSafetyGapCm;
        const float Penetration = FMath::Max(0.0f,
          Required - (Agents[A].Position - Agents[B].Position).Size());
        if (Penetration > 0.5f) ++Result.ViolationCount;
        Result.MaximumPenetrationCm = FMath::Max(Result.MaximumPenetrationCm, Penetration);
        Result.TotalPenetrationCm += Penetration;
      }
    return Result;
  };
  const auto IsBetter = [](const FScore& A, const FScore& B)
  {
    if (A.ViolationCount != B.ViolationCount) return A.ViolationCount < B.ViolationCount;
    const int32 AMax = FMath::RoundToInt(A.MaximumPenetrationCm * 1000.0f);
    const int32 BMax = FMath::RoundToInt(B.MaximumPenetrationCm * 1000.0f);
    if (AMax != BMax) return AMax < BMax;
    return FMath::RoundToInt64(A.TotalPenetrationCm * 1000.0)
      < FMath::RoundToInt64(B.TotalPenetrationCm * 1000.0);
  };
  const auto TargetClear = [&](const int32 Index, const FVector2f Position)
  {
    if (!Input.Environment.bValidateTargetExclusion) return true;
    const float Required = Input.Environment.TargetExclusionRadiusCm
      + Input.Agents[Index].Agent.PhysicalRadiusCm;
    return (Position - Input.Environment.TargetLocation).Size() >= Required - 0.5f;
  };
  const auto Constrain = [&](const int32 Index, const FVector2f Proposed,
    FVector2f& OutPosition)
  {
    const FVector2f Start2 = Input.Agents[Index].Agent.Position;
    const auto Result = FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
      Input.Environment.FlowConfig, FVector(Start2.X, Start2.Y, 0.0f),
      FVector(Proposed.X, Proposed.Y, 0.0f), Input.FixedStepSeconds,
      Input.Environment.bConstrainToFlowBounds);
    OutPosition = FVector2f(Result.Location.X, Result.Location.Y);
    return !Result.bPenetrating
      && !FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        Input.Environment.FlowConfig, Result.Location)
      && OutPosition.Equals(Proposed, 0.01f);
  };

  const FScore Before = Score(OutSummary.Agents);
  OutSummary.BeforeHardPairViolationCount = Before.ViolationCount;
  OutSummary.BeforeMaximumPenetrationCm = Before.MaximumPenetrationCm;
  const float PairCap = FMath::Max(0.0f,
    Input.PbdSettings.MaxPairCorrectionPerIterationCm);
  const int32 Iterations = FMath::Max(1, Input.PbdSettings.IterationCount);
  for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
  {
    bool bApplied = false;
    for (int32 A = 0; A < OutSummary.Agents.Num(); ++A)
      for (int32 B = A + 1; B < OutSummary.Agents.Num(); ++B)
      {
        const float Required = Input.Agents[A].Agent.PhysicalRadiusCm
          + Input.Agents[B].Agent.PhysicalRadiusCm
          + Input.ElasticSettings.HardSafetyGapCm;
        const FVector2f Delta = OutSummary.Agents[B].Position - OutSummary.Agents[A].Position;
        const float Distance = Delta.Size();
        if (Distance >= Required - 0.5f) continue;
        const FVector2f Normal = Distance > KINDA_SMALL_NUMBER
          ? Delta / Distance
          : FVector2f(((OutSummary.Agents[A].AgentId ^ OutSummary.Agents[B].AgentId) & 1)
            ? 1.0f : 0.0f, ((OutSummary.Agents[A].AgentId ^ OutSummary.Agents[B].AgentId) & 1)
            ? 0.0f : 1.0f);
        const float Correction = FMath::Min(Required - Distance, PairCap);
        const FScore CurrentScore = Score(OutSummary.Agents);
        TArray<FCrowdDemoElasticShadowSafetyPolishAgent> Best = OutSummary.Agents;
        FScore BestScore = CurrentScore;
        int32 BestMode = INDEX_NONE;
        const float Shares[3][2] = {{0.5f, 0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
        for (int32 Mode = 0; Mode < 3; ++Mode)
        {
          TArray<FCrowdDemoElasticShadowSafetyPolishAgent> Candidate = OutSummary.Agents;
          const FVector2f ProposedA = Candidate[A].Position - Normal * Correction * Shares[Mode][0];
          const FVector2f ProposedB = Candidate[B].Position + Normal * Correction * Shares[Mode][1];
          FVector2f ConstrainedA, ConstrainedB;
          if (!Constrain(A, ProposedA, ConstrainedA) || !Constrain(B, ProposedB, ConstrainedB))
          {
            ++OutSummary.ObstacleRejectedCandidateCount;
            continue;
          }
          if (!TargetClear(A, ConstrainedA) || !TargetClear(B, ConstrainedB))
          {
            ++OutSummary.TargetRejectedCandidateCount;
            continue;
          }
          Candidate[A].Position = ConstrainedA;
          Candidate[B].Position = ConstrainedB;
          const FScore CandidateScore = Score(Candidate);
          if (IsBetter(CandidateScore, BestScore))
          {
            Best = MoveTemp(Candidate);
            BestScore = CandidateScore;
            BestMode = Mode;
          }
        }
        if (BestMode != INDEX_NONE)
        {
          OutSummary.Agents = MoveTemp(Best);
          ++OutSummary.AppliedPairCount;
          OutSummary.OneSidedCorrectionCount += BestMode == 0 ? 0 : 1;
          bApplied = true;
        }
      }
    if (!bApplied) break;
  }
  const FScore After = Score(OutSummary.Agents);
  OutSummary.AfterHardPairViolationCount = After.ViolationCount;
  OutSummary.AfterMaximumPenetrationCm = After.MaximumPenetrationCm;
  uint32 Hash = Fold(FnvOffset, OutSummary.BeforeHardPairViolationCount);
  Hash = Fold(Hash, OutSummary.AfterHardPairViolationCount);
  Hash = Fold(Hash, OutSummary.AppliedPairCount);
  Hash = Fold(Hash, OutSummary.OneSidedCorrectionCount);
  Hash = Fold(Hash, OutSummary.ObstacleRejectedCandidateCount);
  Hash = Fold(Hash, OutSummary.TargetRejectedCandidateCount);
  for (const auto& Agent : OutSummary.Agents)
  {
    Hash = Fold(Hash, Agent.AgentId);
    Hash = Fold(Hash, Q(Agent.Position.X));
    Hash = Fold(Hash, Q(Agent.Position.Y));
  }
  OutSummary.StableHash = Hash;
  OutSummary.bValid = true;
  return true;
}

bool FCrowdDemoElasticShadowKernel::RunBranch(
  const FCrowdDemoElasticShadowStepInput& SourceInput,
  const bool bApplyElastic,
  FCrowdDemoElasticShadowBranchResult& OutResult)
{
  OutResult = {};
  FCrowdDemoElasticShadowStepInput Input = SourceInput;
  Input.Agents.Sort([](const auto& A, const auto& B)
    { return A.Agent.AgentId < B.Agent.AgentId; });
  if (Input.Agents.IsEmpty() || Input.FixedStepSeconds <= 0.0f) return false;

  TArray<FCrowdDemoElasticCrowdAgent> ElasticAgents;
  ElasticAgents.Reserve(Input.Agents.Num());
  for (const auto& Agent : Input.Agents) ElasticAgents.Add(Agent.Agent);
  if (!FCrowdDemoElasticCrowdKernel::Solve(ElasticAgents, Input.ElasticSettings,
    Input.Environment, OutResult.ElasticResults, OutResult.ElasticSummary))
    return false;

  TArray<FVector2f> InitialPositions, SelectedPreferred;
  InitialPositions.Reserve(Input.Agents.Num());
  SelectedPreferred.Reserve(Input.Agents.Num());
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
  {
    InitialPositions.Add(Input.Agents[Index].Agent.Position);
    const FVector2f Preferred = bApplyElastic
      ? OutResult.ElasticResults[Index].AdjustedPreferredVelocity
      : Input.Agents[Index].Agent.BasePreferredVelocity;
    SelectedPreferred.Add(QuantizeVelocity(Preferred,
      Input.ElasticSettings.VelocityQuantumCmps,
      Input.Agents[Index].Agent.MaxSpeedCmps));
  }
  MakeStage(ECrowdDemoElasticShadowStage::Preferred, Input.Agents,
    InitialPositions, SelectedPreferred, SelectedPreferred,
    OutResult.Stages[StageIndex(ECrowdDemoElasticShadowStage::Preferred)]);

  TArray<FCrowdDemoOrcaAgent> OrcaAgents;
  OrcaAgents.Reserve(Input.Agents.Num());
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
  {
    const auto& Shadow = Input.Agents[Index];
    FCrowdDemoOrcaAgent& Agent = OrcaAgents.AddDefaulted_GetRef();
    Agent.AgentId = Shadow.Agent.AgentId;
    Agent.Position = Shadow.Agent.Position;
    Agent.Velocity = Shadow.Agent.Velocity;
    Agent.PreferredVelocity = SelectedPreferred[Index];
    Agent.FlowDirection = Shadow.FlowDirection;
    Agent.PortalDirection = Shadow.FlowDirection;
    Agent.RadiusCm = Shadow.Agent.PhysicalRadiusCm;
    Agent.MaxSpeedCmps = Shadow.Agent.MaxSpeedCmps;
    Agent.IntegrationCost = Shadow.FlowStatus == ECrowdDemoFlowLocationStatus::Reachable
      ? 0 : MAX_int32;
    if (Shadow.SteeringState == ECrowdDemoPursuitSteeringState::Commit)
      Agent.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Committed;
    else if (Shadow.SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
      || Shadow.SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold)
      Agent.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Yielding;
  }
  FCrowdDemoDeterministicOrcaKernel::Solve(OrcaAgents, Input.OrcaSettings,
    Input.FixedStepSeconds, OutResult.OrcaResults, OutResult.OrcaSummary);
  TArray<FVector2f> OrcaVelocities, PredictedPositions;
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
  {
    const FCrowdDemoOrcaResult* Orca = FindOrca(
      OutResult.OrcaResults, Input.Agents[Index].Agent.AgentId);
    const FVector2f Velocity = Orca ? Orca->Velocity : SelectedPreferred[Index];
    OrcaVelocities.Add(Velocity);
    PredictedPositions.Add(InitialPositions[Index] + Velocity * Input.FixedStepSeconds);
  }
  MakeStage(ECrowdDemoElasticShadowStage::Orca, Input.Agents,
    InitialPositions, OrcaVelocities, SelectedPreferred,
    OutResult.Stages[StageIndex(ECrowdDemoElasticShadowStage::Orca)]);
  MakeStage(ECrowdDemoElasticShadowStage::Predict, Input.Agents,
    PredictedPositions, OrcaVelocities, SelectedPreferred,
    OutResult.Stages[StageIndex(ECrowdDemoElasticShadowStage::Predict)]);

  TArray<FVector2f> ObstaclePositions, ObstacleVelocities;
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
  {
    const FVector Start(InitialPositions[Index].X, InitialPositions[Index].Y, 0.0f);
    const FVector Proposed(PredictedPositions[Index].X, PredictedPositions[Index].Y, 0.0f);
    FCrowdDemoElasticShadowObstacleDiagnostic& Diagnostic =
      OutResult.ObstacleDiagnostics.AddDefaulted_GetRef();
    Diagnostic.AgentId = Input.Agents[Index].Agent.AgentId;
    Diagnostic.Constraint = FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
      Input.Environment.FlowConfig, Start, Proposed,
      Input.Environment.bConstrainToFlowBounds);
    const FCrowdDemoSharedFlowConstraintResult Constrained =
      FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
        Input.Environment.FlowConfig, Start, Proposed, Input.FixedStepSeconds,
        Input.Environment.bConstrainToFlowBounds);
    Diagnostic.ConstrainedPosition = FVector2f(Constrained.Location.X, Constrained.Location.Y);
    Diagnostic.ConstrainedVelocity = FVector2f(Constrained.Velocity.X, Constrained.Velocity.Y);
    Diagnostic.PositionDeltaCm = (Constrained.Location - Proposed).Size2D();
    Diagnostic.VelocityDeltaCmps = (Diagnostic.ConstrainedVelocity - OrcaVelocities[Index]).Size();
    Diagnostic.bPenetrating = Constrained.bPenetrating
      || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        Input.Environment.FlowConfig, Constrained.Location);
    Diagnostic.bClipped = Diagnostic.PositionDeltaCm > 0.5f
      || Diagnostic.VelocityDeltaCmps > 0.5f;
    Diagnostic.bUsedSlideX = Diagnostic.bClipped && !Diagnostic.Constraint.bDirectSegmentClear
      && Diagnostic.Constraint.bSlideXClear
      && FVector::Dist2D(Constrained.Location, Diagnostic.Constraint.SlideX) <= 0.5f;
    Diagnostic.bUsedSlideY = Diagnostic.bClipped && !Diagnostic.Constraint.bDirectSegmentClear
      && Diagnostic.Constraint.bSlideYClear
      && FVector::Dist2D(Constrained.Location, Diagnostic.Constraint.SlideY) <= 0.5f;
    Diagnostic.bStopped = OrcaVelocities[Index].Size() >= 1.0f
      && Diagnostic.ConstrainedVelocity.Size() < 1.0f;
    ObstaclePositions.Add(Diagnostic.ConstrainedPosition);
    ObstacleVelocities.Add(Diagnostic.ConstrainedVelocity);
  }
  MakeStage(ECrowdDemoElasticShadowStage::Obstacle, Input.Agents,
    ObstaclePositions, ObstacleVelocities, SelectedPreferred,
    OutResult.Stages[StageIndex(ECrowdDemoElasticShadowStage::Obstacle)]);

  TArray<FCrowdDemoHardSeparationPbdAgent> PbdAgents;
  const float HalfGap = Input.ElasticSettings.HardSafetyGapCm * 0.5f;
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
  {
    FCrowdDemoHardSeparationPbdAgent& Agent = PbdAgents.AddDefaulted_GetRef();
    Agent.AgentId = Input.Agents[Index].Agent.AgentId;
    Agent.Location = FVector(ObstaclePositions[Index].X, ObstaclePositions[Index].Y, 0.0f);
    Agent.RadiusCm = Input.Agents[Index].Agent.PhysicalRadiusCm + HalfGap;
  }
  TArray<FCrowdDemoHardSeparationPbdResult> PbdResults;
  FCrowdDemoHardSeparationPbdSummary PbdSummary;
  FCrowdDemoHardSeparationPbdKernel::Solve(PbdAgents, Input.PbdSettings,
    OutResult.PbdPairs, PbdResults, PbdSummary, &OutResult.PbdIterations);

  TArray<FVector2f> PreviousPbdPositions = ObstaclePositions;
  for (int32 Iteration = 0; Iteration < 3; ++Iteration)
  {
    TArray<FVector2f> Positions = PreviousPbdPositions;
    if (OutResult.PbdIterations.IsValidIndex(Iteration))
    {
      for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
      {
        const FCrowdDemoHardSeparationPbdResult* Pbd = FindPbd(
          OutResult.PbdIterations[Iteration].AgentResults,
          Input.Agents[Index].Agent.AgentId);
        if (Pbd) Positions[Index] = FVector2f(
          Pbd->CorrectedLocation.X, Pbd->CorrectedLocation.Y);
      }
    }
    TArray<FVector2f> Velocities;
    for (int32 Index = 0; Index < Positions.Num(); ++Index)
      Velocities.Add((Positions[Index] - InitialPositions[Index]) / Input.FixedStepSeconds);
    const ECrowdDemoElasticShadowStage Stage = static_cast<ECrowdDemoElasticShadowStage>(
      StageIndex(ECrowdDemoElasticShadowStage::Pbd1) + Iteration);
    MakeStage(Stage, Input.Agents, Positions, Velocities, SelectedPreferred,
      OutResult.Stages[StageIndex(Stage)]);
    PreviousPbdPositions = MoveTemp(Positions);
  }

  TArray<FVector2f> ReprojectPositions, ReprojectVelocities;
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
  {
    const FVector Start(InitialPositions[Index].X, InitialPositions[Index].Y, 0.0f);
    const FVector Proposed(PreviousPbdPositions[Index].X, PreviousPbdPositions[Index].Y, 0.0f);
    FCrowdDemoElasticShadowObstacleDiagnostic& Diagnostic =
      OutResult.ReprojectDiagnostics.AddDefaulted_GetRef();
    Diagnostic.AgentId = Input.Agents[Index].Agent.AgentId;
    Diagnostic.Constraint = FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
      Input.Environment.FlowConfig, Start, Proposed,
      Input.Environment.bConstrainToFlowBounds);
    const FCrowdDemoSharedFlowConstraintResult Constrained =
      FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
        Input.Environment.FlowConfig, Start, Proposed, Input.FixedStepSeconds,
        Input.Environment.bConstrainToFlowBounds);
    Diagnostic.ConstrainedPosition = FVector2f(Constrained.Location.X, Constrained.Location.Y);
    Diagnostic.ConstrainedVelocity = FVector2f(
      (Constrained.Location.X - Start.X) / Input.FixedStepSeconds,
      (Constrained.Location.Y - Start.Y) / Input.FixedStepSeconds);
    Diagnostic.PositionDeltaCm = (Constrained.Location - Proposed).Size2D();
    const FVector2f PreVelocity = (PreviousPbdPositions[Index] - InitialPositions[Index])
      / Input.FixedStepSeconds;
    Diagnostic.VelocityDeltaCmps = (Diagnostic.ConstrainedVelocity - PreVelocity).Size();
    Diagnostic.bPenetrating = Constrained.bPenetrating
      || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        Input.Environment.FlowConfig, Constrained.Location);
    Diagnostic.bClipped = Diagnostic.PositionDeltaCm > 0.5f
      || Diagnostic.VelocityDeltaCmps > 0.5f;
    Diagnostic.bUsedSlideX = Diagnostic.bClipped && !Diagnostic.Constraint.bDirectSegmentClear
      && Diagnostic.Constraint.bSlideXClear
      && FVector::Dist2D(Constrained.Location, Diagnostic.Constraint.SlideX) <= 0.5f;
    Diagnostic.bUsedSlideY = Diagnostic.bClipped && !Diagnostic.Constraint.bDirectSegmentClear
      && Diagnostic.Constraint.bSlideYClear
      && FVector::Dist2D(Constrained.Location, Diagnostic.Constraint.SlideY) <= 0.5f;
    Diagnostic.bStopped = PreVelocity.Size() >= 1.0f
      && Diagnostic.ConstrainedVelocity.Size() < 1.0f;
    ReprojectPositions.Add(Diagnostic.ConstrainedPosition);
    ReprojectVelocities.Add(Diagnostic.ConstrainedVelocity);
  }
  TArray<FCrowdDemoElasticShadowSafetyPolishAgent> SafetyAgents;
  SafetyAgents.Reserve(Input.Agents.Num());
  for (int32 Index = 0; Index < Input.Agents.Num(); ++Index)
  {
    auto& Agent = SafetyAgents.AddDefaulted_GetRef();
    Agent.AgentId = Input.Agents[Index].Agent.AgentId;
    Agent.Position = ReprojectPositions[Index];
  }
  if (!PolishReprojectHardPairs(Input, SafetyAgents, OutResult.SafetyPolish))
    return false;
  for (int32 Index = 0; Index < OutResult.SafetyPolish.Agents.Num(); ++Index)
  {
    ReprojectPositions[Index] = OutResult.SafetyPolish.Agents[Index].Position;
    ReprojectVelocities[Index] =
      (ReprojectPositions[Index] - InitialPositions[Index]) / Input.FixedStepSeconds;
  }
  MakeStage(ECrowdDemoElasticShadowStage::Reproject, Input.Agents,
    ReprojectPositions, ReprojectVelocities, SelectedPreferred,
    OutResult.Stages[StageIndex(ECrowdDemoElasticShadowStage::Reproject)]);

  uint32 BranchHash = Fold(FnvOffset, bApplyElastic ? 1 : 0);
  for (int32 Index = 0; Index < StageIndex(ECrowdDemoElasticShadowStage::Count); ++Index)
  {
    const ECrowdDemoElasticShadowStage Stage =
      static_cast<ECrowdDemoElasticShadowStage>(Index);
    BuildStageSummary(Input, Stage, OutResult.Stages[Index],
      OutResult.StageSummaries[Index]);
    if (Stage == ECrowdDemoElasticShadowStage::Obstacle
      || Stage == ECrowdDemoElasticShadowStage::Reproject)
    {
      const auto& Diagnostics = Stage == ECrowdDemoElasticShadowStage::Obstacle
        ? OutResult.ObstacleDiagnostics : OutResult.ReprojectDiagnostics;
      for (const FCrowdDemoElasticShadowObstacleDiagnostic& Diagnostic : Diagnostics)
      {
        auto& Summary = OutResult.StageSummaries[Index];
        Summary.ObstaclePenetrationCount += Diagnostic.bPenetrating ? 1 : 0;
        Summary.ObstacleClippedCount += Diagnostic.bClipped ? 1 : 0;
        Summary.ObstacleSlideCount += Diagnostic.bUsedSlideX || Diagnostic.bUsedSlideY ? 1 : 0;
        Summary.ObstacleStoppedCount += Diagnostic.bStopped ? 1 : 0;
        Summary.FlowBoundsHitCount += Diagnostic.Constraint.bHitFlowBounds ? 1 : 0;
        Summary.MaximumObstacleDeltaCm = FMath::Max(
          Summary.MaximumObstacleDeltaCm, Diagnostic.PositionDeltaCm);
      }
    }
    BranchHash = Fold(BranchHash, static_cast<int32>(OutResult.StageSummaries[Index].StableHash));
    BranchHash = Fold(BranchHash, OutResult.StageSummaries[Index].ObstaclePenetrationCount);
    BranchHash = Fold(BranchHash, OutResult.StageSummaries[Index].ObstacleClippedCount);
  }
  BranchHash = Fold(BranchHash, static_cast<int32>(OutResult.OrcaSummary.VelocityHash));
  BranchHash = Fold(BranchHash, static_cast<int32>(OutResult.ElasticSummary.StableHash));
  for (const FCrowdDemoOrcaResult& Result : OutResult.OrcaResults)
  {
    BranchHash = Fold(BranchHash, Result.AgentId);
    BranchHash = Fold(BranchHash, Result.FallbackStage);
    BranchHash = Fold(BranchHash, static_cast<int32>(Result.Feasibility));
    for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
    {
      BranchHash = Fold(BranchHash, Constraint.OtherAgentId);
      BranchHash = Fold(BranchHash, Q(Constraint.Point.X));
      BranchHash = Fold(BranchHash, Q(Constraint.Point.Y));
      BranchHash = Fold(BranchHash, Q(Constraint.Normal.X * 32767.0f));
      BranchHash = Fold(BranchHash, Q(Constraint.Normal.Y * 32767.0f));
    }
  }
  for (const auto& Diagnostic : OutResult.ObstacleDiagnostics)
    BranchHash = Fold(BranchHash, static_cast<int32>(Diagnostic.Constraint.StableHash));
  for (const auto& Iteration : OutResult.PbdIterations)
    BranchHash = Fold(BranchHash, static_cast<int32>(Iteration.StableHash));
  for (const auto& Diagnostic : OutResult.ReprojectDiagnostics)
    BranchHash = Fold(BranchHash, static_cast<int32>(Diagnostic.Constraint.StableHash));
  BranchHash = Fold(BranchHash, static_cast<int32>(OutResult.SafetyPolish.StableHash));
  OutResult.StableHash = BranchHash;
  OutResult.bValid = true;
  return true;
}

bool FCrowdDemoElasticShadowKernel::RunTwinStep(
  const FCrowdDemoElasticShadowStepInput& Input,
  FCrowdDemoElasticShadowTwinResult& OutResult)
{
  OutResult = {};
  if (!RunBranch(Input, false, OutResult.Baseline)
    || !RunBranch(Input, true, OutResult.Elastic)) return false;
  uint32 Hash = Fold(FnvOffset, Input.FixedStepIndex);
  Hash = Fold(Hash, static_cast<int32>(OutResult.Baseline.StableHash));
  Hash = Fold(Hash, static_cast<int32>(OutResult.Elastic.StableHash));
  OutResult.StableHash = Hash;
  OutResult.bValid = true;
  return true;
}

bool FCrowdDemoElasticShadowKernel::BuildFirstFailureFixture(
  const FCrowdDemoElasticShadowStepInput& Input,
  const FCrowdDemoElasticShadowTwinResult& Twin,
  const TConstArrayView<int32> ZeroProgressAgentIds,
  const int32 ZeroProgressStepMax,
  FCrowdDemoElasticShadowFailureFixture& OutFixture)
{
  OutFixture = {};
  if (!Twin.bValid) return false;
  int32 Primary = INDEX_NONE, Other = INDEX_NONE;
  ECrowdDemoElasticShadowStage FailureStage = ECrowdDemoElasticShadowStage::Preferred;
  ECrowdDemoElasticShadowFailureKind Kind = ECrowdDemoElasticShadowFailureKind::None;
  ECrowdDemoElasticShadowAttribution Attribution = ECrowdDemoElasticShadowAttribution::None;
  for (int32 Index = 0; Index < StageIndex(ECrowdDemoElasticShadowStage::Count)
    && Kind == ECrowdDemoElasticShadowFailureKind::None; ++Index)
  {
    const auto& B = Twin.Baseline.StageSummaries[Index];
    const auto& E = Twin.Elastic.StageSummaries[Index];
    if (E.HardPairViolationCount > 0)
    {
      const ECrowdDemoElasticShadowAttribution CandidateAttribution = Index == 0
        ? ECrowdDemoElasticShadowAttribution::InheritedAtStepStart
        : (B.HardPairViolationCount == 0
          ? ECrowdDemoElasticShadowAttribution::ElasticIntroduced
          : (E.MaximumHardPairPenetrationCm > B.MaximumHardPairPenetrationCm + 0.5f
            ? ECrowdDemoElasticShadowAttribution::ElasticWorsened
            : ECrowdDemoElasticShadowAttribution::SharedByBoth));
      if (CandidateAttribution == ECrowdDemoElasticShadowAttribution::ElasticIntroduced
        || CandidateAttribution == ECrowdDemoElasticShadowAttribution::ElasticWorsened)
      {
        Kind = ECrowdDemoElasticShadowFailureKind::HardPair;
        FailureStage = static_cast<ECrowdDemoElasticShadowStage>(Index);
        Primary = E.FirstHardPair.MinAgentId; Other = E.FirstHardPair.MaxAgentId;
        Attribution = CandidateAttribution;
      }
    }
    if (Kind == ECrowdDemoElasticShadowFailureKind::None
      && E.ObstaclePenetrationCount > B.ObstaclePenetrationCount)
    {
      Kind = ECrowdDemoElasticShadowFailureKind::ObstaclePenetration;
      FailureStage = static_cast<ECrowdDemoElasticShadowStage>(Index);
      const auto& Diagnostics = FailureStage == ECrowdDemoElasticShadowStage::Obstacle
        ? Twin.Elastic.ObstacleDiagnostics : Twin.Elastic.ReprojectDiagnostics;
      for (const auto& Diagnostic : Diagnostics) if (Diagnostic.bPenetrating)
      { Primary = Diagnostic.AgentId; break; }
      Attribution = B.ObstaclePenetrationCount == 0
        ? ECrowdDemoElasticShadowAttribution::ElasticIntroduced
        : ECrowdDemoElasticShadowAttribution::ElasticWorsened;
    }
    if (Kind == ECrowdDemoElasticShadowFailureKind::None
      && E.TargetViolationCount > B.TargetViolationCount)
    {
      Kind = ECrowdDemoElasticShadowFailureKind::TargetExclusion;
      FailureStage = static_cast<ECrowdDemoElasticShadowStage>(Index);
      for (const auto& Agent : Twin.Elastic.Stages[Index])
      {
        const auto* Source = Input.Agents.FindByPredicate([&](const auto& Item)
          { return Item.Agent.AgentId == Agent.AgentId; });
        if (Source && (Agent.Position - Input.Environment.TargetLocation).Size()
          < Input.Environment.TargetExclusionRadiusCm
            + Source->Agent.PhysicalRadiusCm - 0.5f)
        { Primary = Agent.AgentId; break; }
      }
      Attribution = B.TargetViolationCount == 0
        ? ECrowdDemoElasticShadowAttribution::ElasticIntroduced
        : ECrowdDemoElasticShadowAttribution::ElasticWorsened;
    }
    if (Kind == ECrowdDemoElasticShadowFailureKind::None
      && Index == StageIndex(ECrowdDemoElasticShadowStage::Orca)
      && Twin.Elastic.OrcaSummary.StopViolatesConstraintCount
        > Twin.Baseline.OrcaSummary.StopViolatesConstraintCount)
    {
      Kind = ECrowdDemoElasticShadowFailureKind::OrcaStopViolation;
      FailureStage = ECrowdDemoElasticShadowStage::Orca;
      for (const auto& Result : Twin.Elastic.OrcaResults)
        if (!Result.bStopSatisfiesConstraints && Result.bInfeasible)
        { Primary = Result.AgentId; break; }
      Attribution = Twin.Baseline.OrcaSummary.StopViolatesConstraintCount == 0
        ? ECrowdDemoElasticShadowAttribution::ElasticIntroduced
        : ECrowdDemoElasticShadowAttribution::ElasticWorsened;
    }
  }
  const int32 Final = StageIndex(ECrowdDemoElasticShadowStage::Reproject);
  if (Kind == ECrowdDemoElasticShadowFailureKind::None
    && Twin.Elastic.StageSummaries[Final].ActualSourceForwardCmps
      < Twin.Baseline.StageSummaries[Final].ActualSourceForwardCmps)
  {
    Kind = ECrowdDemoElasticShadowFailureKind::SourceForwardRegression;
    FailureStage = ECrowdDemoElasticShadowStage::Reproject;
    Attribution = ECrowdDemoElasticShadowAttribution::ElasticWorsened;
    for (const auto& Agent : Input.Agents) if (Agent.Agent.bTransitSource)
    { Primary = Agent.Agent.AgentId; break; }
  }
  if (Kind == ECrowdDemoElasticShadowFailureKind::None
    && ZeroProgressStepMax >= 15 && !ZeroProgressAgentIds.IsEmpty())
  {
    Kind = ECrowdDemoElasticShadowFailureKind::SourceZeroProgress;
    FailureStage = ECrowdDemoElasticShadowStage::Reproject;
    Attribution = ECrowdDemoElasticShadowAttribution::ElasticWorsened;
    Primary = ZeroProgressAgentIds[0];
  }
  if (Kind == ECrowdDemoElasticShadowFailureKind::None) return false;

  TSet<int32> Closure;
  if (Primary != INDEX_NONE) Closure.Add(Primary);
  if (Other != INDEX_NONE) Closure.Add(Other);
  bool bChanged = true;
  while (bChanged)
  {
    bChanged = false;
    for (const FCrowdDemoOrcaResult& Result : Twin.Elastic.OrcaResults)
      for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
        if (Closure.Contains(Result.AgentId) || Closure.Contains(Constraint.OtherAgentId))
        {
          const int32 Old = Closure.Num(); Closure.Add(Result.AgentId);
          Closure.Add(Constraint.OtherAgentId); bChanged |= Closure.Num() != Old;
        }
    for (const FCrowdDemoHardSeparationPbdPair& Pair : Twin.Elastic.PbdPairs)
      if (Closure.Contains(Pair.MinAgentId) || Closure.Contains(Pair.MaxAgentId))
      {
        const int32 Old = Closure.Num(); Closure.Add(Pair.MinAgentId);
        Closure.Add(Pair.MaxAgentId); bChanged |= Closure.Num() != Old;
      }
    if (Closure.Num() > 20) break;
  }
  if (Closure.Num() == 1)
    for (const auto& Agent : Input.Agents) if (!Closure.Contains(Agent.Agent.AgentId))
    { Closure.Add(Agent.Agent.AgentId); break; }

  OutFixture.bValid = true;
  OutFixture.bFixtureTooLarge = Closure.Num() > 20;
  OutFixture.FixedStepIndex = Input.FixedStepIndex;
  OutFixture.Stage = FailureStage;
  OutFixture.FailureKind = Kind;
  OutFixture.Attribution = Attribution;
  OutFixture.PrimaryAgentId = Primary;
  OutFixture.OtherAgentId = Other;
  OutFixture.ClosureAgentCount = Closure.Num();
  OutFixture.ZeroProgressStepMax = ZeroProgressStepMax;
  OutFixture.OrcaConstraintEpsilonCmps = Input.OrcaSettings.ConstraintEpsilonCmps;
  OutFixture.OrcaVelocityQuantumCmps = Input.OrcaSettings.VelocityQuantumCmps;
  OutFixture.BaselineHash = Twin.Baseline.StableHash;
  OutFixture.ElasticHash = Twin.Elastic.StableHash;
  if (!OutFixture.bFixtureTooLarge)
  {
    TArray<int32> Ids = Closure.Array(); Ids.Sort();
    for (const int32 AgentId : Ids)
    {
      const auto* Source = Input.Agents.FindByPredicate([AgentId](const auto& Item)
        { return Item.Agent.AgentId == AgentId; });
      if (!Source) continue;
      auto& Item = OutFixture.Agents.AddDefaulted_GetRef(); Item.Input = *Source;
      for (int32 Index = 0; Index < StageIndex(ECrowdDemoElasticShadowStage::Count); ++Index)
      {
        if (const auto* B = FindStageAgent(Twin.Baseline,
          static_cast<ECrowdDemoElasticShadowStage>(Index), AgentId))
          Item.BaselineStages.Add(*B);
        if (const auto* E = FindStageAgent(Twin.Elastic,
          static_cast<ECrowdDemoElasticShadowStage>(Index), AgentId))
          Item.ElasticStages.Add(*E);
      }
      if (const auto* B = FindOrca(Twin.Baseline.OrcaResults, AgentId))
      {
        Item.BaselineOrcaResult = *B;
        Item.BaselineConstraints = B->Constraints;
      }
      if (const auto* E = FindOrca(Twin.Elastic.OrcaResults, AgentId))
      {
        Item.ElasticOrcaResult = *E;
        Item.ElasticConstraints = E->Constraints;
      }
      if (const auto* D = Twin.Baseline.ObstacleDiagnostics.FindByPredicate(
        [AgentId](const auto& Value) { return Value.AgentId == AgentId; }))
        Item.BaselineObstacle = *D;
      if (const auto* D = Twin.Elastic.ObstacleDiagnostics.FindByPredicate(
        [AgentId](const auto& Value) { return Value.AgentId == AgentId; }))
        Item.ElasticObstacle = *D;
      if (const auto* D = Twin.Baseline.ReprojectDiagnostics.FindByPredicate(
        [AgentId](const auto& Value) { return Value.AgentId == AgentId; }))
        Item.BaselineReproject = *D;
      if (const auto* D = Twin.Elastic.ReprojectDiagnostics.FindByPredicate(
        [AgentId](const auto& Value) { return Value.AgentId == AgentId; }))
        Item.ElasticReproject = *D;
    }
    OutFixture.BaselinePbdIterations = Twin.Baseline.PbdIterations;
    OutFixture.ElasticPbdIterations = Twin.Elastic.PbdIterations;
    OutFixture.BaselineSafetyPolish = Twin.Baseline.SafetyPolish;
    OutFixture.ElasticSafetyPolish = Twin.Elastic.SafetyPolish;
    for (int32 Index = 0; Index < StageIndex(ECrowdDemoElasticShadowStage::Count); ++Index)
    {
      OutFixture.BaselineStageSummaries.Add(Twin.Baseline.StageSummaries[Index]);
      OutFixture.ElasticStageSummaries.Add(Twin.Elastic.StageSummaries[Index]);
    }

    const FCrowdDemoElasticShadowFixtureAgent* PrimaryFixture =
      OutFixture.Agents.FindByPredicate([Primary](const auto& Item)
        { return Item.Input.Agent.AgentId == Primary; });
    if (PrimaryFixture)
    {
      auto& Replay = OutFixture.OrcaReplay;
      Replay.bConstraintCountsMatch = PrimaryFixture->BaselineConstraints.Num()
        == PrimaryFixture->ElasticConstraints.Num();
      Replay.bConstraintsExactlyMatch = Replay.bConstraintCountsMatch;
      if (Replay.bConstraintCountsMatch)
      {
        for (int32 Index = 0; Index < PrimaryFixture->BaselineConstraints.Num(); ++Index)
        {
          const auto& B = PrimaryFixture->BaselineConstraints[Index];
          const auto& E = PrimaryFixture->ElasticConstraints[Index];
          const bool bSame = B.OtherAgentId == E.OtherAgentId
            && B.StableConstraintOrder == E.StableConstraintOrder
            && B.Kind == E.Kind
            && B.Point.X == E.Point.X && B.Point.Y == E.Point.Y
            && B.Normal.X == E.Normal.X && B.Normal.Y == E.Normal.Y;
          if (!bSame)
          {
            Replay.bConstraintsExactlyMatch = false;
            Replay.FirstConstraintMismatchIndex = Index;
            break;
          }
        }
      }
      else
      {
        Replay.FirstConstraintMismatchIndex = FMath::Min(
          PrimaryFixture->BaselineConstraints.Num(),
          PrimaryFixture->ElasticConstraints.Num());
      }

      const float MaxSpeed = PrimaryFixture->Input.Agent.MaxSpeedCmps;
      const float Epsilon = Input.OrcaSettings.ConstraintEpsilonCmps;
      const FVector2f KnownVelocity = PrimaryFixture->BaselineOrcaResult.Velocity;
      Replay.bBaselineVelocityInsideElasticSpeedCircle = KnownVelocity.SizeSquared()
        <= FMath::Square(MaxSpeed + Epsilon);
      Replay.BaselineVelocityMinimumElasticResidualCmps = MAX_flt;
      for (const auto& Constraint : PrimaryFixture->ElasticConstraints)
      {
        Replay.BaselineVelocityMinimumElasticResidualCmps = FMath::Min(
          Replay.BaselineVelocityMinimumElasticResidualCmps,
          FVector2f::DotProduct(KnownVelocity - Constraint.Point, Constraint.Normal));
      }
      if (PrimaryFixture->ElasticConstraints.IsEmpty())
        Replay.BaselineVelocityMinimumElasticResidualCmps = 0.0f;
      const auto ElasticSolveInput = FCrowdDemoDeterministicOrcaKernel::MakeContinuousSolveInput(
        PrimaryFixture->ElasticStages[StageIndex(ECrowdDemoElasticShadowStage::Preferred)].PreferredVelocity,
        MaxSpeed, PrimaryFixture->ElasticConstraints, Epsilon);
      Replay.bBaselineVelocitySatisfiesElasticConstraints =
        FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(
          ElasticSolveInput, KnownVelocity);
      const auto Continuous = FCrowdDemoDeterministicOrcaKernel::SolveContinuousExact(
        ElasticSolveInput);
      Replay.ElasticContinuousStatus = Continuous.Status;
      Replay.ElasticContinuousVelocity = Continuous.Velocity;
      if (Continuous.Status == ECrowdDemoOrcaSolveStatus::PreferredFeasible
        || Continuous.Status == ECrowdDemoOrcaSolveStatus::ExactFeasible)
      {
        Replay.ElasticQuantizationResult =
          FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
            Continuous.Velocity,
            PrimaryFixture->ElasticStages[StageIndex(ECrowdDemoElasticShadowStage::Preferred)].PreferredVelocity,
            MaxSpeed, PrimaryFixture->ElasticConstraints, Input.OrcaSettings,
            Replay.ElasticQuantizedVelocity);
      }
      Replay.ElasticFallbackStage = PrimaryFixture->ElasticOrcaResult.FallbackStage;
    }
  }
  uint32 Hash = Fold(FnvOffset, Input.FixedStepIndex);
  Hash = Fold(Hash, StageIndex(FailureStage)); Hash = Fold(Hash, static_cast<int32>(Kind));
  Hash = Fold(Hash, static_cast<int32>(Attribution)); Hash = Fold(Hash, Primary);
  Hash = Fold(Hash, Other); Hash = Fold(Hash, Closure.Num());
  for (const auto& Agent : OutFixture.Agents)
  {
    Hash = Fold(Hash, Agent.Input.Agent.AgentId);
    for (const auto& Stage : Agent.BaselineStages) Hash = Fold(Hash, static_cast<int32>(Stage.StableHash));
    for (const auto& Stage : Agent.ElasticStages) Hash = Fold(Hash, static_cast<int32>(Stage.StableHash));
    Hash = Fold(Hash, static_cast<int32>(Agent.BaselineOrcaResult.Feasibility));
    Hash = Fold(Hash, static_cast<int32>(Agent.ElasticOrcaResult.Feasibility));
    Hash = Fold(Hash, Agent.BaselineOrcaResult.bStopSatisfiesConstraints ? 1 : 0);
    Hash = Fold(Hash, Agent.ElasticOrcaResult.bStopSatisfiesConstraints ? 1 : 0);
    for (const auto& Constraint : Agent.BaselineConstraints)
    {
      Hash = Fold(Hash, Constraint.OtherAgentId);
      Hash = Fold(Hash, Q(Constraint.Point.X)); Hash = Fold(Hash, Q(Constraint.Point.Y));
      Hash = Fold(Hash, Q(Constraint.Normal.X * 32767.0f));
      Hash = Fold(Hash, Q(Constraint.Normal.Y * 32767.0f));
    }
    for (const auto& Constraint : Agent.ElasticConstraints)
    {
      Hash = Fold(Hash, Constraint.OtherAgentId);
      Hash = Fold(Hash, Q(Constraint.Point.X)); Hash = Fold(Hash, Q(Constraint.Point.Y));
      Hash = Fold(Hash, Q(Constraint.Normal.X * 32767.0f));
      Hash = Fold(Hash, Q(Constraint.Normal.Y * 32767.0f));
    }
    Hash = Fold(Hash, static_cast<int32>(Agent.BaselineObstacle.Constraint.StableHash));
    Hash = Fold(Hash, static_cast<int32>(Agent.ElasticObstacle.Constraint.StableHash));
    Hash = Fold(Hash, static_cast<int32>(Agent.BaselineReproject.Constraint.StableHash));
    Hash = Fold(Hash, static_cast<int32>(Agent.ElasticReproject.Constraint.StableHash));
  }
  for (const auto& Iteration : OutFixture.BaselinePbdIterations)
    Hash = Fold(Hash, static_cast<int32>(Iteration.StableHash));
  for (const auto& Iteration : OutFixture.ElasticPbdIterations)
    Hash = Fold(Hash, static_cast<int32>(Iteration.StableHash));
  for (const auto& Summary : OutFixture.BaselineStageSummaries)
    Hash = Fold(Hash, static_cast<int32>(Summary.StableHash));
  for (const auto& Summary : OutFixture.ElasticStageSummaries)
    Hash = Fold(Hash, static_cast<int32>(Summary.StableHash));
  Hash = Fold(Hash, static_cast<int32>(OutFixture.BaselineSafetyPolish.StableHash));
  Hash = Fold(Hash, static_cast<int32>(OutFixture.ElasticSafetyPolish.StableHash));
  OutFixture.StableHash = Hash;
  return true;
}

bool FCrowdDemoElasticShadowKernel::InitializeParallelRollout(
  const FCrowdDemoElasticShadowStepInput& Input,
  const FCrowdDemoSharedFlowField& FlowField,
  const FCrowdDemoPursuitPositioningSettings& PositioningSettings,
  FCrowdDemoElasticShadowParallelState& OutState)
{
  OutState = {};
  if (!Input.Agents.ContainsByPredicate([](const auto& Agent)
    { return Agent.Agent.bTransitSource; })) return false;
  OutState.bActive = true;
  OutState.StartFixedStepIndex = Input.FixedStepIndex;
  OutState.BaselineAgents = Input.Agents;
  OutState.ElasticAgents = Input.Agents;
  OutState.BaselineAgents.Sort([](const auto& A, const auto& B)
    { return A.Agent.AgentId < B.Agent.AgentId; });
  OutState.ElasticAgents = OutState.BaselineAgents;
  OutState.FrozenFlowField = FlowField;
  OutState.PositioningSettings = PositioningSettings;
  return true;
}

bool FCrowdDemoElasticShadowKernel::AdvanceParallelRollout(
  const FCrowdDemoElasticShadowStepInput& TemplateInput,
  FCrowdDemoElasticShadowParallelState& State,
  FCrowdDemoElasticShadowTwinResult& OutStep)
{
  OutStep = {};
  if (!State.bActive || State.bCompleted || State.StepIndex >= 180) return false;
  const auto Prepare = [&](TArray<FCrowdDemoElasticShadowAgentInput>& Agents)
  {
    for (auto& Item : Agents)
    {
      const FCrowdDemoSharedFlowSample Sample = FCrowdDemoSharedFlowFieldKernel::Sample(
        State.FrozenFlowField, FVector(Item.Agent.Position.X, Item.Agent.Position.Y, 0.0f));
      Item.FlowStatus = Sample.Status;
      Item.FlowDirection = FVector2f(Sample.FlowDirection.X, Sample.FlowDirection.Y);
      const FVector2f FlowPreferred = Sample.Status == ECrowdDemoFlowLocationStatus::Reachable
        ? Item.FlowDirection.GetSafeNormal() * Item.Agent.MaxSpeedCmps : FVector2f::ZeroVector;
      Item.Agent.BasePreferredVelocity =
        FCrowdDemoPursuitPositioningKernel::BuildSteeringFirstPreferredVelocity(
          Item.SteeringState, Item.Agent.Position, FlowPreferred,
          Item.HoldingLocation, Item.AssignedPosition, Item.Agent.MaxSpeedCmps,
          State.PositioningSettings);
      Item.Agent.TransitDirection = Item.Agent.BasePreferredVelocity.GetSafeNormal();
      if (Item.Agent.TransitDirection.IsNearlyZero())
        Item.Agent.TransitDirection = Item.Agent.Velocity.GetSafeNormal();
    }
  };
  Prepare(State.BaselineAgents); Prepare(State.ElasticAgents);

  FCrowdDemoElasticShadowStepInput BaselineInput = TemplateInput;
  BaselineInput.FixedStepIndex = State.StartFixedStepIndex + State.StepIndex;
  BaselineInput.Agents = State.BaselineAgents;
  FCrowdDemoElasticShadowStepInput ElasticInput = TemplateInput;
  ElasticInput.FixedStepIndex = BaselineInput.FixedStepIndex;
  ElasticInput.Agents = State.ElasticAgents;
  if (State.StepIndex >= 90)
  {
    ElasticInput.ElasticSettings.TransitGainPerSecond = 0.0f;
    ElasticInput.ElasticSettings.MaxTransitYieldSpeedCmps = 0.0f;
  }
  if (!RunBranch(BaselineInput, false, OutStep.Baseline)
    || !RunBranch(ElasticInput, true, OutStep.Elastic)) return false;
  OutStep.bValid = true;
  OutStep.StableHash = Fold(Fold(FnvOffset,
    static_cast<int32>(OutStep.Baseline.StableHash)),
    static_cast<int32>(OutStep.Elastic.StableHash));

  const int32 ObstacleStage = StageIndex(ECrowdDemoElasticShadowStage::Obstacle);
  const int32 ReprojectStage = StageIndex(ECrowdDemoElasticShadowStage::Reproject);
  State.Summary.BaselineHardPairViolationCount +=
    OutStep.Baseline.StageSummaries[ReprojectStage].HardPairViolationCount;
  State.Summary.ElasticHardPairViolationCount +=
    OutStep.Elastic.StageSummaries[ReprojectStage].HardPairViolationCount;
  State.Summary.BaselineObstaclePenetrationCount +=
    OutStep.Baseline.StageSummaries[ObstacleStage].ObstaclePenetrationCount
      + OutStep.Baseline.StageSummaries[ReprojectStage].ObstaclePenetrationCount;
  State.Summary.ElasticObstaclePenetrationCount +=
    OutStep.Elastic.StageSummaries[ObstacleStage].ObstaclePenetrationCount
      + OutStep.Elastic.StageSummaries[ReprojectStage].ObstaclePenetrationCount;
  State.Summary.BaselineTargetViolationCount +=
    OutStep.Baseline.StageSummaries[ReprojectStage].TargetViolationCount;
  State.Summary.ElasticTargetViolationCount +=
    OutStep.Elastic.StageSummaries[ReprojectStage].TargetViolationCount;
  State.Summary.BaselineOrcaStopViolationCount +=
    OutStep.Baseline.OrcaSummary.StopViolatesConstraintCount;
  State.Summary.ElasticOrcaStopViolationCount +=
    OutStep.Elastic.OrcaSummary.StopViolatesConstraintCount;
  State.Summary.BaselineDesiredSourceForwardCmps +=
    OutStep.Baseline.StageSummaries[ReprojectStage].DesiredSourceForwardCmps;
  State.Summary.BaselineActualSourceForwardCmps +=
    OutStep.Baseline.StageSummaries[ReprojectStage].ActualSourceForwardCmps;
  State.Summary.ElasticDesiredSourceForwardCmps +=
    OutStep.Elastic.StageSummaries[ReprojectStage].DesiredSourceForwardCmps;
  State.Summary.ElasticActualSourceForwardCmps +=
    OutStep.Elastic.StageSummaries[ReprojectStage].ActualSourceForwardCmps;

  const int32 Final = StageIndex(ECrowdDemoElasticShadowStage::Reproject);
  for (int32 Index = 0; Index < State.BaselineAgents.Num(); ++Index)
  {
    const auto& B = OutStep.Baseline.Stages[Final][Index];
    const auto& E = OutStep.Elastic.Stages[Final][Index];
    State.BaselineAgents[Index].Agent.Position = B.Position;
    State.BaselineAgents[Index].Agent.Velocity = B.Velocity;
    State.ElasticAgents[Index].Agent.Position = E.Position;
    State.ElasticAgents[Index].Agent.Velocity = E.Velocity;
  }

  if (State.StepIndex == 90)
  {
    for (int32 Index = 0; Index < State.BaselineAgents.Num(); ++Index)
    {
      const auto& B = State.BaselineAgents[Index]; const auto& E = State.ElasticAgents[Index];
      if (!IsRecoveryEligible(B)) continue;
      State.BaselineRecoveryStartError.Add(B.Agent.AgentId,
        (B.Agent.Position - RecoveryTarget(B)).Size());
      State.ElasticRecoveryStartError.Add(E.Agent.AgentId,
        (E.Agent.Position - RecoveryTarget(E)).Size());
    }
  }
  if (State.StepIndex >= 90)
  {
    for (int32 Index = 0; Index < State.BaselineAgents.Num(); ++Index)
    {
      const auto& B = State.BaselineAgents[Index]; const auto& E = State.ElasticAgents[Index];
      if (!IsRecoveryEligible(B)) continue;
      if (!State.BaselineRecoveryCompletionStep.Contains(B.Agent.AgentId)
        && (B.Agent.Position - RecoveryTarget(B)).Size() <= 30.0f
        && B.Agent.Velocity.Size() <= 20.0f)
        State.BaselineRecoveryCompletionStep.Add(B.Agent.AgentId, State.StepIndex);
      if (!State.ElasticRecoveryCompletionStep.Contains(E.Agent.AgentId)
        && (E.Agent.Position - RecoveryTarget(E)).Size() <= 30.0f
        && E.Agent.Velocity.Size() <= 20.0f)
        State.ElasticRecoveryCompletionStep.Add(E.Agent.AgentId, State.StepIndex);
    }
  }
  ++State.StepIndex;
  State.Summary.CompletedStepCount = State.StepIndex;
  if (State.StepIndex == 180)
  {
    State.bCompleted = true;
    State.Summary.CompletedStepCount = 180;
    uint32 Hash = Fold(FnvOffset, State.StartFixedStepIndex);
    for (int32 Index = 0; Index < State.BaselineAgents.Num(); ++Index)
    {
      const auto& B = State.BaselineAgents[Index]; const auto& E = State.ElasticAgents[Index];
      if (!IsRecoveryEligible(B)) continue;
      ++State.Summary.EligibleRecoveryAgentCount;
      const float BError = (B.Agent.Position - RecoveryTarget(B)).Size();
      const float EError = (E.Agent.Position - RecoveryTarget(E)).Size();
      State.Summary.BaselineEndErrorsCm.Add(BError);
      State.Summary.ElasticEndErrorsCm.Add(EError);
      const int32* BStep = State.BaselineRecoveryCompletionStep.Find(B.Agent.AgentId);
      const int32* EStep = State.ElasticRecoveryCompletionStep.Find(E.Agent.AgentId);
      if (BStep) { ++State.Summary.BaselineRecoveryCompletedCount;
        State.Summary.BaselineRecoveryTimesSeconds.Add((*BStep - 90) * TemplateInput.FixedStepSeconds); }
      else ++State.Summary.BaselinePermanentHoleCount;
      if (EStep) { ++State.Summary.ElasticRecoveryCompletedCount;
        State.Summary.ElasticRecoveryTimesSeconds.Add((*EStep - 90) * TemplateInput.FixedStepSeconds); }
      else ++State.Summary.ElasticPermanentHoleCount;
      const float BStart = State.BaselineRecoveryStartError.FindRef(B.Agent.AgentId);
      const float EStart = State.ElasticRecoveryStartError.FindRef(E.Agent.AgentId);
      State.Summary.BaselineImprovedCount += BError <= BStart - 1.0f ? 1 : 0;
      State.Summary.ElasticImprovedCount += EError <= EStart - 1.0f ? 1 : 0;
      Hash = Fold(Hash, B.Agent.AgentId); Hash = Fold(Hash, Q(BError)); Hash = Fold(Hash, Q(EError));
    }
    State.Summary.StableHash = Hash;
  }
  return true;
}
