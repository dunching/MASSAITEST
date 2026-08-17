#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"

namespace
{
  TArray<FCrowdDemoBidirectionalSwapLayoutInput> MakeInputs()
  {
    TArray<FCrowdDemoBidirectionalSwapLayoutInput> Inputs;
    for (int32 Index = 0; Index < FCrowdDemoBidirectionalSwapKernel::AgentCount; ++Index)
    {
      auto& Input = Inputs.AddDefaulted_GetRef();
      Input.AgentId = 100 + Index;
      Input.FormationIndex = Index;
    }
    return Inputs;
  }

  FCrowdDemoBidirectionalSwapProgress RunProductionRollout(
    const FCrowdDemoBidirectionalSwapLayout& Layout,
    TArray<FCrowdDemoBidirectionalSwapStepAgent>& OutFinalAgents,
    const float GoalLateralOffsetCm = -1.0f)
  {
    FCrowdDemoSharedFlowField Fields[2];
    for (int32 CohortId = 0; CohortId < 2; ++CohortId)
    {
      FCrowdDemoSharedFlowFieldConfig Config =
        FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortId);
      if (GoalLateralOffsetCm >= 0.0f)
        Config.GoalLocation.X = CohortId == 0
          ? GoalLateralOffsetCm : -GoalLateralOffsetCm;
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Fields[CohortId]);
    }
    TArray<FCrowdDemoBidirectionalSwapStepAgent> States;
    for (const auto& LayoutAgent : Layout.Agents)
    {
      auto& State = States.AddDefaulted_GetRef();
      State.AgentId = LayoutAgent.AgentId;
      State.FormationIndex = LayoutAgent.FormationIndex;
      State.Location = LayoutAgent.SpawnLocation;
      State.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
    }
    FCrowdDemoParticleConstraintEnvironment Environment;
    Environment.FlowConfig = FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(0);
    Environment.FlowConfig.AgentInflateCm = 52.0f;
    FCrowdDemoParticleConstraintSettings Settings;
    FCrowdDemoBidirectionalSwapProgress Progress;
    for (int32 Step = 0; Step < 900; ++Step)
    {
      TArray<FCrowdDemoParticleConstraintAgent> ParticleAgents;
      for (auto& State : States)
      {
        const int32 CohortId =
          FCrowdDemoBidirectionalSwapKernel::CohortIdForFormationIndex(
            State.FormationIndex);
        const FCrowdDemoSharedFlowSample Sample =
          FCrowdDemoSharedFlowFieldKernel::Sample(Fields[CohortId], State.Location);
        State.FlowStatus = Sample.Status;
        const FVector Goal = FVector(Fields[CohortId].Config.GoalLocation);
        const bool bReached = FVector::DistSquared2D(State.Location, Goal)
          <= FMath::Square(140.0f);
        float Speed = 800.0f;
        if (Sample.GuidanceDistanceCm > 0.0f)
          Speed = FMath::Min(Speed, Sample.GuidanceDistanceCm / Settings.FixedStepSeconds);
        const FVector Desired = bReached || Sample.bUnreachable
          ? FVector::ZeroVector : Sample.FlowDirection * Speed;
        auto& Particle = ParticleAgents.AddDefaulted_GetRef();
        Particle.AgentId = State.AgentId;
        Particle.StartPosition = State.Location;
        Particle.PredictedPosition = State.Location + Desired * Settings.FixedStepSeconds;
      }
      TArray<FCrowdDemoParticleConstraintPair> Pairs;
      TArray<FCrowdDemoParticleConstraintResult> Results;
      FCrowdDemoParticleConstraintSummary Summary;
      FCrowdDemoParticleConstraintKernel::Solve(
        ParticleAgents, Environment, Settings, Pairs, Results, Summary);
      if (!Summary.bValid) break;
      TMap<int32, const FCrowdDemoParticleConstraintResult*> ById;
      for (const auto& Result : Results) ById.Add(Result.AgentId, &Result);
      for (auto& State : States)
        if (const auto* const* Result = ById.Find(State.AgentId))
        {
          State.Location = (*Result)->CorrectedPosition;
          State.Velocity = (*Result)->CorrectedVelocity;
        }
      FCrowdDemoBidirectionalSwapKernel::UpdateProgress(States, Step, Progress);
    }
    OutFinalAgents = MoveTemp(States);
    return Progress;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapLayoutTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.LayoutAndOpposingFlows",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapLayoutTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapLayoutInput> Inputs = MakeInputs();
  const auto Layout = FCrowdDemoBidirectionalSwapKernel::BuildLayout(Inputs);
  Algo::Reverse(Inputs);
  const auto Reversed = FCrowdDemoBidirectionalSwapKernel::BuildLayout(Inputs);
  TestTrue(TEXT("T3 layout valid"), Layout.bValid);
  TestEqual(TEXT("T3 layout agent count"), Layout.Agents.Num(), 20);
  TestEqual(TEXT("T3 reversed layout hash"), Reversed.LayoutHash, Layout.LayoutHash);
  int32 CohortCounts[2] = { 0, 0 };
  for (const auto& Agent : Layout.Agents)
    if (Agent.CohortId >= 0 && Agent.CohortId < 2) ++CohortCounts[Agent.CohortId];
  TestEqual(TEXT("south cohort count"), CohortCounts[0], 10);
  TestEqual(TEXT("north cohort count"), CohortCounts[1], 10);
  for (int32 A = 0; A < Layout.Agents.Num(); ++A)
    for (int32 B = A + 1; B < Layout.Agents.Num(); ++B)
      TestTrue(TEXT("initial hard separation"),
        FVector::Dist2D(Layout.Agents[A].SpawnLocation, Layout.Agents[B].SpawnLocation)
          + KINDA_SMALL_NUMBER >= 94.0f);

  FCrowdDemoSharedFlowField Fields[2];
  for (int32 CohortId = 0; CohortId < 2; ++CohortId)
  {
    const auto Config = FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortId);
    TestTrue(TEXT("opposing flow builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Fields[CohortId]));
    TestEqual(TEXT("open flow has no obstacles"), Config.ObstacleSpecs.Num(), 0);
  }
  TestTrue(TEXT("opposing goals"),
    FVector(Fields[0].Config.GoalLocation).Y > 0.0f
      && FVector(Fields[1].Config.GoalLocation).Y < 0.0f
      && FVector(Fields[0].Config.GoalLocation).X > 0.0f
      && FVector(Fields[1].Config.GoalLocation).X < 0.0f);
  for (const auto& Agent : Layout.Agents)
  {
    const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(
      Fields[Agent.CohortId], Agent.SpawnLocation);
    TestEqual(TEXT("spawn flow reachable"), Sample.Status,
      ECrowdDemoFlowLocationStatus::Reachable);
    TestTrue(FString::Printf(
      TEXT("flow points toward opposing side agent=%d cohort=%d direction=(%.3f,%.3f) status=%d"),
      Agent.AgentId, Agent.CohortId, Sample.FlowDirection.X,
      Sample.FlowDirection.Y, static_cast<int32>(Sample.Status)),
      Sample.FlowDirection.Y * (Agent.CohortId == 0 ? 1.0f : -1.0f) > 0.5f);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapProgressTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.CompletionPlanesAndDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapProgressTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapStepAgent> Samples;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Agent = Samples.AddDefaulted_GetRef();
    Agent.AgentId = 100 + Index;
    Agent.FormationIndex = Index;
    const int32 CohortId = FCrowdDemoBidirectionalSwapKernel::CohortIdForFormationIndex(Index);
    const float Sign = CohortId == 0 ? 1.0f : -1.0f;
    Agent.Location = FVector(0.0f, Sign * 2250.0f, 60.0f);
    Agent.Velocity = FVector(0.0f, Sign * 100.0f, 0.0f);
    Agent.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
  }
  FCrowdDemoBidirectionalSwapProgress Progress;
  FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, 250, Progress);
  Algo::Reverse(Samples);
  FCrowdDemoBidirectionalSwapProgress Reversed;
  FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, 250, Reversed);
  TestTrue(TEXT("progress valid"), Progress.bValid);
  TestEqual(TEXT("all center crossed"), Progress.CenterCrossedAgentIds.Num(), 20);
  TestEqual(TEXT("all completion planes crossed"), Progress.CompletedAgentIds.Num(), 20);
  TestEqual(TEXT("no final deadlock"), Progress.FinalDeadlockAgentIds.Num(), 0);
  TestEqual(TEXT("input order invariant progress hash"),
    Reversed.ProgressHash, Progress.ProgressHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapDeadlockTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.FinalDeadlockSemantics",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapDeadlockTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapStepAgent> Samples;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Agent = Samples.AddDefaulted_GetRef();
    Agent.AgentId = 100 + Index;
    Agent.FormationIndex = Index;
    Agent.Location = FVector(0.0f, Index < 10 ? -1000.0f : 1000.0f, 60.0f);
    Agent.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
  }
  FCrowdDemoBidirectionalSwapProgress Progress;
  for (int32 Step = 0; Step < 90; ++Step)
    FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, Step, Progress);
  TestEqual(TEXT("90 low-forward steps are current deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 20);
  for (auto& Agent : Samples)
    Agent.Velocity.Y = Agent.FormationIndex < 10 ? 100.0f : -100.0f;
  FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, 90, Progress);
  TestEqual(TEXT("recovered agents are not final deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapProductionRolloutTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.ProductionRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapProductionRolloutTest::RunTest(
  const FString& Parameters)
{
  const auto Layout = FCrowdDemoBidirectionalSwapKernel::BuildLayout(MakeInputs());
  TArray<FCrowdDemoBidirectionalSwapStepAgent> FinalAgents;
  const FCrowdDemoBidirectionalSwapProgress Progress =
    RunProductionRollout(Layout, FinalAgents);
  for (const auto& Agent : FinalAgents)
    if (!Progress.CompletedAgentIds.Contains(Agent.AgentId))
      AddInfo(FString::Printf(TEXT("unfinished agent=%d formation=%d location=(%.1f,%.1f) velocity=(%.1f,%.1f) low_steps=%d"),
        Agent.AgentId, Agent.FormationIndex, Agent.Location.X, Agent.Location.Y,
        Agent.Velocity.X, Agent.Velocity.Y,
        Progress.ConsecutiveLowForwardStepsByAgentId.FindRef(Agent.AgentId)));
  TestTrue(TEXT("production rollout progress valid"), Progress.bValid);
  TestEqual(TEXT("production rollout center crossed"),
    Progress.CenterCrossedAgentIds.Num(), 20);
  TestEqual(TEXT("production rollout completion planes"),
    Progress.CompletedAgentIds.Num(), 20);
  TestEqual(TEXT("production rollout final deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 0);
  return true;
}

#endif
