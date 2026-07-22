#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdLocalPredictiveInteractionKernel.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "CrowdVelocityHalfPlaneKernel.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdLocalPredictiveAgent MakeAgent(
    const int32 AgentId,
    const FVector2f Position,
    const FVector2f Preferred,
    const int32 BlockedAge = 0)
  {
    FCrowdLocalPredictiveAgent Agent;
    Agent.AgentId = AgentId;
    Agent.Position = Position;
    Agent.PreferredVelocity = Preferred;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.MaxSpeedCmps = 800.0f;
    Agent.BlockedAgeSteps = BlockedAge;
    return Agent;
  }

  struct FSolveResult
  {
    TArray<FCrowdLocalPredictivePair> Pairs;
    TArray<FCrowdLocalPredictiveGrantState> Grants;
    TArray<FCrowdLocalPredictiveResult> Results;
    FCrowdLocalPredictiveSummary Summary;
    FCrowdLocalPredictiveDiagnosticTrace Trace;
  };

  FSolveResult Solve(TConstArrayView<FCrowdLocalPredictiveAgent> Agents)
  {
    FSolveResult Result;
    FCrowdLocalPredictiveSettings Settings;
    FCrowdLocalPredictiveInteractionKernel::Solve(
      Agents, FCrowdSharedFlowFieldKernel::MakeSf1Config(1), Settings, {},
      Result.Pairs, Result.Grants, Result.Results, Result.Summary,
      &Result.Trace);
    return Result;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdVelocityHalfPlaneTest,
  "MassCrowd.Core.LocalPredictive.VelocityHalfPlane",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdVelocityHalfPlaneTest::RunTest(const FString& Parameters)
{
  FCrowdVelocityHalfPlaneInput Input;
  Input.PreferredVelocity = FVector2f::ZeroVector;
  Input.Settings.MaxSpeedCmps = 300.0f;
  Input.Settings.BehaviorEpsilonCmps = 0.1f;
  Input.Settings.VelocityQuantumCmps = 1.0f;
  Input.HalfPlanes = {
    {FVector2f(50.0f, 0.0f), FVector2f(1.0f, 0.0f), 2},
    {FVector2f(0.0f, 25.0f), FVector2f(0.0f, 1.0f), 1}};

  const FCrowdVelocityHalfPlaneResult Result =
    FCrowdVelocityHalfPlaneKernel::Solve(Input);
  TestTrue(TEXT("continuous solution valid"), Result.bContinuousValid);
  TestTrue(TEXT("quantized solution valid"), Result.bQuantizedValid);
  TestTrue(TEXT("quantized solution satisfies constraints"),
    FCrowdVelocityHalfPlaneKernel::ValidateVelocity(
      Input, Result.QuantizedVelocity));
  TestTrue(TEXT("x lower bound"), Result.QuantizedVelocity.X >= 49.9f);
  TestTrue(TEXT("y lower bound"), Result.QuantizedVelocity.Y >= 24.9f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdLocalPredictiveDeterminismTest,
  "MassCrowd.Core.LocalPredictive.Determinism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdLocalPredictiveDeterminismTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdLocalPredictiveAgent> Agents = {
    MakeAgent(5, FVector2f(224,1896), FVector2f(111,279), 285),
    MakeAgent(8, FVector2f(206,2131), FVector2f(0,0), 0),
    MakeAgent(12, FVector2f(349,1912), FVector2f(0,0), 0),
    MakeAgent(14, FVector2f(256,2016), FVector2f(0,0), 0),
    MakeAgent(15, FVector2f(394,2066), FVector2f(0,0), 0),
    MakeAgent(19, FVector2f(325,2169), FVector2f(-124,-273), 273)};
  const FSolveResult Forward = Solve(Agents);
  TestTrue(TEXT("six-agent solve valid"), Forward.Summary.bValid);
  TestEqual(TEXT("six conflict pairs"),
    Forward.Summary.ConflictPairCount, 6);
  TestEqual(TEXT("one preferred recovery component"),
    Forward.Summary.JointPreferredRecoveryComponentCount, 1);
  TestEqual(TEXT("all six agents recovered"),
    Forward.Summary.JointPreferredRecoveryAgentCount, 6);

  Algo::Reverse(Agents);
  const FSolveResult Reversed = Solve(Agents);
  TestEqual(TEXT("reverse input preserves candidate hash"),
    Reversed.Summary.CandidateHash, Forward.Summary.CandidateHash);
  TestEqual(TEXT("reverse input preserves result count"),
    Reversed.Results.Num(), Forward.Results.Num());
  for (int32 Index = 0; Index < Forward.Results.Num(); ++Index)
  {
    TestEqual(TEXT("result id stable"),
      Reversed.Results[Index].AgentId, Forward.Results[Index].AgentId);
    TestTrue(TEXT("result velocity stable"),
      Reversed.Results[Index].Velocity.Equals(
        Forward.Results[Index].Velocity, 0.0f));
  }
  return true;
}

#endif
