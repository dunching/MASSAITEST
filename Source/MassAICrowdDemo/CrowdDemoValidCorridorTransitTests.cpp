#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"

namespace
{
  TArray<FCrowdDemoValidCorridorTransitLayoutInput> MakeT4Inputs()
  {
    TArray<FCrowdDemoValidCorridorTransitLayoutInput> Inputs;
    for (int32 Index = 0; Index < FCrowdDemoValidCorridorTransitKernel::AgentCount; ++Index)
    {
      auto& Input = Inputs.AddDefaulted_GetRef();
      Input.AgentId = 100 + Index;
      Input.FormationIndex = Index;
    }
    return Inputs;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoValidCorridorTransitLayoutTest,
  "CrowdDemo.SF.T4.ValidCorridorTransit.LayoutAndFlow",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoValidCorridorTransitLayoutTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoValidCorridorTransitLayoutInput> Inputs = MakeT4Inputs();
  const auto Layout = FCrowdDemoValidCorridorTransitKernel::BuildLayout(Inputs);
  Algo::Reverse(Inputs);
  const auto Reversed = FCrowdDemoValidCorridorTransitKernel::BuildLayout(Inputs);
  TestTrue(TEXT("T4 layout valid"), Layout.bValid);
  TestEqual(TEXT("T4 layout agent count"), Layout.Agents.Num(), 20);
  TestEqual(TEXT("T4 reversed layout hash"), Reversed.LayoutHash, Layout.LayoutHash);
  for (int32 A = 0; A < Layout.Agents.Num(); ++A)
    for (int32 B = A + 1; B < Layout.Agents.Num(); ++B)
      TestTrue(TEXT("T4 initial hard separation"),
        FVector::Dist2D(Layout.Agents[A].SpawnLocation, Layout.Agents[B].SpawnLocation)
          + KINDA_SMALL_NUMBER >= 94.0f);

  const auto Config = FCrowdDemoValidCorridorTransitKernel::MakeFlowConfig();
  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("T4 Shared Flow V2 builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));
  TestEqual(TEXT("T4 retains SF1 obstacle count"), Config.ObstacleSpecs.Num(), 10);
  TestEqual(TEXT("T4 uses V2 connectivity"), Config.ConnectivityContractVersion, 2);
  for (const auto& Agent : Layout.Agents)
  {
    const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(Field, Agent.SpawnLocation);
    TestEqual(TEXT("T4 spawn reachable"), Sample.Status,
      ECrowdDemoFlowLocationStatus::Reachable);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoValidCorridorTransitProgressTest,
  "CrowdDemo.SF.T4.ValidCorridorTransit.ExitPlaneAndDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoValidCorridorTransitProgressTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoValidCorridorTransitStepAgent> Agents;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = 100 + Index;
    Agent.Location = FVector((Index - 10) * 50.0f, 800.0f, 60.0f);
    Agent.Velocity = FVector(0.0f, 100.0f, 0.0f);
    Agent.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
  }
  FCrowdDemoValidCorridorTransitProgress Progress;
  FCrowdDemoValidCorridorTransitKernel::UpdateProgress(Agents, 200, Progress);
  Algo::Reverse(Agents);
  FCrowdDemoValidCorridorTransitProgress Reversed;
  FCrowdDemoValidCorridorTransitKernel::UpdateProgress(Agents, 200, Reversed);
  TestTrue(TEXT("T4 progress valid"), Progress.bValid);
  TestEqual(TEXT("T4 wall passed"), Progress.WallPassedAgentIds.Num(), 20);
  TestEqual(TEXT("T4 corridor exited"), Progress.CorridorExitedAgentIds.Num(), 20);
  TestEqual(TEXT("T4 completion plane crossed"), Progress.CompletedAgentIds.Num(), 20);
  TestEqual(TEXT("T4 no final deadlock"), Progress.FinalDeadlockAgentIds.Num(), 0);
  TestEqual(TEXT("T4 input order invariant progress hash"),
    Reversed.ProgressHash, Progress.ProgressHash);
  TestTrue(TEXT("T4 completion plane remains outside point-goal radius"),
    FVector::Dist2D(FVector(0.0f, 800.0f, 60.0f),
      FVector(FCrowdDemoValidCorridorTransitKernel::MakeFlowConfig().GoalLocation))
      > 140.0f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoValidCorridorTransitProductionRolloutTest,
  "CrowdDemo.SF.T4.ValidCorridorTransit.ProductionRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoValidCorridorTransitProductionRolloutTest::RunTest(
  const FString& Parameters)
{
  const auto Layout = FCrowdDemoValidCorridorTransitKernel::BuildLayout(MakeT4Inputs());
  FCrowdDemoSharedFlowField Field;
  const auto Config = FCrowdDemoValidCorridorTransitKernel::MakeFlowConfig();
  TestTrue(TEXT("T4 rollout field builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));

  TArray<FCrowdDemoValidCorridorTransitStepAgent> States;
  for (const auto& LayoutAgent : Layout.Agents)
  {
    auto& State = States.AddDefaulted_GetRef();
    State.AgentId = LayoutAgent.AgentId;
    State.Location = LayoutAgent.SpawnLocation;
  }
  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.FlowConfig = Config;
  FCrowdDemoParticleConstraintSettings Settings;
  FCrowdDemoLocalPredictiveSettings LocalSettings;
  TArray<FCrowdDemoLocalPredictiveGrantState> PreviousGrants;
  TMap<int32, int32> BlockedAges;
  FCrowdDemoValidCorridorTransitProgress Progress;
  bool bAllSafetyValid = true;
  bool bAllLocalValid = true;
  int32 FirstLocalInvalidStep = INDEX_NONE;
  for (int32 Step = 0; Step < 900; ++Step)
  {
    TArray<FCrowdDemoLocalPredictiveAgent> LocalAgents;
    for (auto& State : States)
    {
      const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(Field, State.Location);
      State.FlowStatus = Sample.Status;
      float Speed = 800.0f;
      if (Sample.GuidanceDistanceCm > 0.0f)
        Speed = FMath::Min(Speed, Sample.GuidanceDistanceCm / Settings.FixedStepSeconds);
      const FVector Desired = Sample.bUnreachable
        ? FVector::ZeroVector : Sample.FlowDirection * Speed;
      FCrowdDemoLocalPredictiveAgent& Local = LocalAgents.AddDefaulted_GetRef();
      Local.AgentId = State.AgentId;
      Local.Position = FVector2f(State.Location.X, State.Location.Y);
      Local.Velocity = FVector2f(State.Velocity.X, State.Velocity.Y);
      Local.PreferredVelocity = FVector2f(Desired.X, Desired.Y);
      Local.BlockedAgeSteps = BlockedAges.FindRef(State.AgentId);
    }
    TArray<FCrowdDemoLocalPredictivePair> LocalPairs;
    TArray<FCrowdDemoLocalPredictiveGrantState> NextGrants;
    TArray<FCrowdDemoLocalPredictiveResult> LocalResults;
    FCrowdDemoLocalPredictiveSummary LocalSummary;
    FCrowdDemoLocalPredictiveInteractionKernel::Solve(
      LocalAgents, Config, LocalSettings, PreviousGrants,
      LocalPairs, NextGrants, LocalResults, LocalSummary);
    bAllLocalValid = bAllLocalValid && LocalSummary.bValid;
    if (!LocalSummary.bValid)
    {
      FirstLocalInvalidStep = Step;
      AddInfo(FString::Printf(
        TEXT("T4 local invalid step=%d conflicts=%d infeasible=%d joint=%d resolved=%d hash=%u"),
        Step, LocalSummary.ConflictPairCount, LocalSummary.InfeasibleAgentCount,
        LocalSummary.JointValidationFailureCount,
        LocalSummary.JointComponentResolutionCount, LocalSummary.CandidateHash));
      break;
    }
    PreviousGrants = MoveTemp(NextGrants);
    TMap<int32, const FCrowdDemoLocalPredictiveResult*> LocalById;
    for (const auto& Result : LocalResults)
    {
      LocalById.Add(Result.AgentId, &Result);
      BlockedAges.Add(Result.AgentId, Result.NextBlockedAgeSteps);
    }
    TArray<FCrowdDemoParticleConstraintAgent> ParticleAgents;
    for (auto& State : States)
    {
      const FCrowdDemoLocalPredictiveResult* const* Local = LocalById.Find(State.AgentId);
      if (!Local) continue;
      auto& Particle = ParticleAgents.AddDefaulted_GetRef();
      Particle.AgentId = State.AgentId;
      Particle.StartPosition = State.Location;
      Particle.PredictedPosition = State.Location
        + FVector((*Local)->Velocity.X, (*Local)->Velocity.Y, 0.0f)
          * Settings.FixedStepSeconds;
    }
    TArray<FCrowdDemoParticleConstraintPair> Pairs;
    TArray<FCrowdDemoParticleConstraintResult> Results;
    FCrowdDemoParticleConstraintSummary Summary;
    FCrowdDemoParticleConstraintKernel::Solve(
      ParticleAgents, Environment, Settings, Pairs, Results, Summary);
    bAllSafetyValid = bAllSafetyValid && Summary.bValid
      && Summary.HardPairViolationCount == 0
      && Summary.SweptPairViolationCount == 0
      && Summary.ObstaclePenetrationCount == 0
      && Summary.BoundsViolationCount == 0;
    if (!Summary.bValid) break;
    TMap<int32, const FCrowdDemoParticleConstraintResult*> ById;
    for (const auto& Result : Results) ById.Add(Result.AgentId, &Result);
    for (auto& State : States)
      if (const auto* const* Result = ById.Find(State.AgentId))
      {
        State.Location = (*Result)->CorrectedPosition;
        State.Velocity = (*Result)->CorrectedVelocity;
      }
    FCrowdDemoValidCorridorTransitKernel::UpdateProgress(States, Step, Progress);
  }
  TestTrue(TEXT("T4 production rollout local predictive"), bAllLocalValid);
  TestEqual(TEXT("T4 production rollout first local invalid"),
    FirstLocalInvalidStep, INDEX_NONE);
  TestTrue(TEXT("T4 production rollout safety"), bAllSafetyValid);
  TestTrue(TEXT("T4 production rollout progress valid"), Progress.bValid);
  TestEqual(TEXT("T4 production rollout wall passed"),
    Progress.WallPassedAgentIds.Num(), 20);
  TestEqual(TEXT("T4 production rollout corridor exited"),
    Progress.CorridorExitedAgentIds.Num(), 20);
  TestEqual(TEXT("T4 production rollout completion plane"),
    Progress.CompletedAgentIds.Num(), 20);
  TestEqual(TEXT("T4 production rollout final deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 0);
  TestEqual(TEXT("T4 production rollout unreachable"),
    Progress.UnreachableSampleCount, 0);
  return true;
}

#endif
