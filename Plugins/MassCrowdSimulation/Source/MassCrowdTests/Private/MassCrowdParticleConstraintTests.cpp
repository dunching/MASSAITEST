#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdParticleConstraintKernel.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdParticleConstraintAgent MakeAgent(
    const int32 AgentId,
    const FVector& Start,
    const FVector& Predicted)
  {
    FCrowdParticleConstraintAgent Agent;
    Agent.AgentId = AgentId;
    Agent.StartPosition = Start;
    Agent.PredictedPosition = Predicted;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.SoftMarginCm = 17.0f;
    Agent.Mobility = 1.0f;
    return Agent;
  }

  FCrowdParticleConstraintEnvironment MakeOpenEnvironment()
  {
    FCrowdParticleConstraintEnvironment Environment;
    Environment.FlowConfig.BoundsMin = FVector(-2000, -2000, 0);
    Environment.FlowConfig.BoundsMax = FVector(2000, 2000, 0);
    Environment.FlowConfig.ObstacleSpecs.Reset();
    Environment.bConstrainToFlowBounds = true;
    return Environment;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleConstraintDeterminismTest,
  "MassCrowd.Core.ParticleConstraint.Determinism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleConstraintDeterminismTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdParticleConstraintAgent> Agents = {
    MakeAgent(1, FVector(-80, 0, 60), FVector(-20, 0, 60)),
    MakeAgent(2, FVector(80, 0, 60), FVector(20, 0, 60))};
  const FCrowdParticleConstraintEnvironment Environment = MakeOpenEnvironment();
  FCrowdParticleConstraintSettings Settings;
  TArray<FCrowdParticleConstraintPair> ForwardPairs;
  TArray<FCrowdParticleConstraintResult> ForwardResults;
  FCrowdParticleConstraintSummary ForwardSummary;
  FCrowdParticleConstraintKernel::Solve(
    Agents, Environment, Settings, ForwardPairs, ForwardResults,
    ForwardSummary);
  TestTrue(TEXT("pair solve valid"), ForwardSummary.bValid);
  TestEqual(TEXT("hard violation zero"),
    ForwardSummary.HardPairViolationCount, 0);
  TestEqual(TEXT("swept violation zero"),
    ForwardSummary.SweptPairViolationCount, 0);
  TestEqual(TEXT("obstacle violation zero"),
    ForwardSummary.ObstaclePenetrationCount, 0);
  TestEqual(TEXT("result count"), ForwardResults.Num(), 2);
  TestTrue(TEXT("final hard distance"),
    FVector::Dist2D(ForwardResults[0].CorrectedPosition,
      ForwardResults[1].CorrectedPosition) + 0.01f >= 94.0f);

  Algo::Reverse(Agents);
  TArray<FCrowdParticleConstraintPair> ReversePairs;
  TArray<FCrowdParticleConstraintResult> ReverseResults;
  FCrowdParticleConstraintSummary ReverseSummary;
  FCrowdParticleConstraintKernel::Solve(
    Agents, Environment, Settings, ReversePairs, ReverseResults,
    ReverseSummary);
  TestTrue(TEXT("reverse solve valid"), ReverseSummary.bValid);
  TestEqual(TEXT("reverse hash stable"),
    ReverseSummary.CandidateHash, ForwardSummary.CandidateHash);
  TestEqual(TEXT("reverse result count"),
    ReverseResults.Num(), ForwardResults.Num());
  for (int32 Index = 0; Index < ForwardResults.Num(); ++Index)
  {
    TestEqual(TEXT("reverse result agent"),
      ReverseResults[Index].AgentId, ForwardResults[Index].AgentId);
    TestTrue(TEXT("reverse result position"),
      ReverseResults[Index].CorrectedPosition.Equals(
        ForwardResults[Index].CorrectedPosition, 0.0f));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleConstraintInteractionLayerTest,
  "MassCrowd.Core.ParticleConstraint.InteractionLayer",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleConstraintInteractionLayerTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdParticleConstraintAgent> Agents = {
    MakeAgent(1, FVector(-80, 0, 60), FVector::ZeroVector),
    MakeAgent(2, FVector(80, 0, 460), FVector::ZeroVector)};
  Agents[0].InteractionLayer = 3;
  Agents[1].InteractionLayer = 4;
  TArray<FCrowdParticleConstraintPair> Pairs;
  TArray<FCrowdParticleConstraintResult> Results;
  FCrowdParticleConstraintSummary Summary;
  FCrowdParticleConstraintSettings Settings;
  FCrowdParticleConstraintKernel::Solve(
    Agents, MakeOpenEnvironment(), Settings,
    Pairs, Results, Summary);
  TestTrue(TEXT("layered solve valid"), Summary.bValid);
  TestEqual(TEXT("different layers do not generate pairs"),
    Pairs.Num(), 0);
  TestEqual(TEXT("layered result count"), Results.Num(), 2);
  TestTrue(TEXT("first prediction preserved"),
    Results[0].CorrectedPosition.Equals(
      Agents[0].PredictedPosition, 0.0f));
  TestTrue(TEXT("second prediction preserved"),
    Results[1].CorrectedPosition.Equals(
      Agents[1].PredictedPosition, 0.0f));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleSettlingTrackerTest,
  "MassCrowd.Core.ParticleConstraint.SettlingTracker",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleSettlingTrackerTest::RunTest(
  const FString& Parameters)
{
  FCrowdParticleSettlingTracker Tracker;
  for (int32 Step = 0; Step < 16; ++Step)
    FCrowdParticleConstraintKernel::AdvanceSettlingTracker(
      Tracker, 1.0f, 12.0f);
  TestEqual(TEXT("tracker step count"), Tracker.StepCount, 16);
  TestEqual(TEXT("fifteen comparable settled samples"),
    Tracker.ConsecutiveSettledSampleCount, 15);
  TestEqual(TEXT("settling begins at second sample"),
    Tracker.SettlingSteps, 2);
  FCrowdParticleConstraintKernel::AdvanceSettlingTracker(
    Tracker, 1.01f, 12.0f);
  TestEqual(TEXT("excess correction resets window"),
    Tracker.ConsecutiveSettledSampleCount, 0);
  return true;
}

#endif
