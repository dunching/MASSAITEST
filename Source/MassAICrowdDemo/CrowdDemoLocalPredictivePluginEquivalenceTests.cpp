#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CrowdLocalPredictiveInteractionKernel.h"
#include "CrowdVelocityHalfPlaneKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoVelocityHalfPlaneKernel.h"
#include "MassCrowdLocalPredictiveWork.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdDemoLocalPredictiveAgent MakeLegacyAgent(
    const int32 AgentId,
    const FVector2f Position,
    const FVector2f Preferred,
    const int32 BlockedAge)
  {
    FCrowdDemoLocalPredictiveAgent Agent;
    Agent.AgentId = AgentId;
    Agent.Position = Position;
    Agent.PreferredVelocity = Preferred;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.MaxSpeedCmps = 800.0f;
    Agent.BlockedAgeSteps = BlockedAge;
    return Agent;
  }

  FCrowdLocalPredictiveAgent ToCoreAgent(
    const FCrowdDemoLocalPredictiveAgent& Source)
  {
    FCrowdLocalPredictiveAgent Result;
    Result.AgentId = Source.AgentId;
    Result.Position = Source.Position;
    Result.Velocity = Source.Velocity;
    Result.PreferredVelocity = Source.PreferredVelocity;
    Result.PhysicalRadiusCm = Source.PhysicalRadiusCm;
    Result.HardSafetyGapCm = Source.HardSafetyGapCm;
    Result.MaxSpeedCmps = Source.MaxSpeedCmps;
    Result.BlockedAgeSteps = Source.BlockedAgeSteps;
    return Result;
  }

  void CompareHalfPlaneResult(
    FAutomationTestBase& Test,
    const FCrowdDemoVelocityHalfPlaneResult& Legacy,
    const FCrowdVelocityHalfPlaneResult& Core)
  {
    Test.TestEqual(TEXT("half-plane status"),
      static_cast<uint8>(Core.Status), static_cast<uint8>(Legacy.Status));
    Test.TestEqual(TEXT("half-plane quantization"),
      static_cast<uint8>(Core.Quantization),
      static_cast<uint8>(Legacy.Quantization));
    Test.TestTrue(TEXT("half-plane continuous velocity"),
      Core.ContinuousVelocity.Equals(Legacy.ContinuousVelocity, 0.0f));
    Test.TestTrue(TEXT("half-plane quantized velocity"),
      Core.QuantizedVelocity.Equals(Legacy.QuantizedVelocity, 0.0f));
    Test.TestEqual(TEXT("half-plane continuous validity"),
      Core.bContinuousValid, Legacy.bContinuousValid);
    Test.TestEqual(TEXT("half-plane quantized validity"),
      Core.bQuantizedValid, Legacy.bQuantizedValid);
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoLocalPredictivePluginEquivalenceTest,
  "CrowdDemo.SF.LocalPredictive.PluginCoreEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoLocalPredictivePluginEquivalenceTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoVelocityHalfPlaneInput LegacyHalfPlane;
  LegacyHalfPlane.PreferredVelocity = FVector2f(240.0f, -80.0f);
  LegacyHalfPlane.Settings.MaxSpeedCmps = 300.0f;
  LegacyHalfPlane.Settings.BehaviorEpsilonCmps = 0.1f;
  LegacyHalfPlane.Settings.VelocityQuantumCmps = 1.0f;
  LegacyHalfPlane.HalfPlanes = {
    {FVector2f(40.0f, 0.0f), FVector2f(1.0f, 0.0f), 3},
    {FVector2f(0.0f, 25.0f), FVector2f(0.0f, 1.0f), 1},
    {FVector2f(180.0f, 0.0f), FVector2f(-1.0f, 0.0f), 2}};
  FCrowdVelocityHalfPlaneInput CoreHalfPlane;
  CoreHalfPlane.PreferredVelocity = LegacyHalfPlane.PreferredVelocity;
  CoreHalfPlane.Settings.MaxSpeedCmps =
    LegacyHalfPlane.Settings.MaxSpeedCmps;
  CoreHalfPlane.Settings.BehaviorEpsilonCmps =
    LegacyHalfPlane.Settings.BehaviorEpsilonCmps;
  CoreHalfPlane.Settings.VelocityQuantumCmps =
    LegacyHalfPlane.Settings.VelocityQuantumCmps;
  for (const FCrowdDemoVelocityHalfPlane& Plane :
    LegacyHalfPlane.HalfPlanes)
  {
    CoreHalfPlane.HalfPlanes.Add(
      {Plane.Point, Plane.Normal, Plane.StableOrder});
  }
  CompareHalfPlaneResult(*this,
    FCrowdDemoVelocityHalfPlaneKernel::Solve(LegacyHalfPlane),
    FCrowdVelocityHalfPlaneKernel::Solve(CoreHalfPlane));

  TArray<FCrowdDemoLocalPredictiveAgent> LegacyAgents = {
    MakeLegacyAgent(5, FVector2f(224,1896), FVector2f(111,279), 285),
    MakeLegacyAgent(8, FVector2f(206,2131), FVector2f(0,0), 0),
    MakeLegacyAgent(12, FVector2f(349,1912), FVector2f(0,0), 0),
    MakeLegacyAgent(14, FVector2f(256,2016), FVector2f(0,0), 0),
    MakeLegacyAgent(15, FVector2f(394,2066), FVector2f(0,0), 0),
    MakeLegacyAgent(19, FVector2f(325,2169), FVector2f(-124,-273), 273)};
  TArray<FCrowdLocalPredictiveAgent> CoreAgents;
  for (const FCrowdDemoLocalPredictiveAgent& Agent : LegacyAgents)
    CoreAgents.Add(ToCoreAgent(Agent));

  FCrowdDemoLocalPredictiveSettings LegacySettings;
  FCrowdLocalPredictiveSettings CoreSettings;
  const FCrowdDemoSharedFlowFieldConfig LegacyFlow =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  const FCrowdSharedFlowFieldConfig CoreFlow =
    FCrowdSharedFlowFieldKernel::MakeSf1Config(1);

  TArray<FCrowdDemoLocalPredictivePair> LegacyPairs;
  TArray<FCrowdDemoLocalPredictiveGrantState> LegacyGrants;
  TArray<FCrowdDemoLocalPredictiveResult> LegacyResults;
  FCrowdDemoLocalPredictiveSummary LegacySummary;
  FCrowdDemoLocalPredictiveDiagnosticTrace LegacyTrace;
  FCrowdDemoLocalPredictiveInteractionKernel::Solve(
    LegacyAgents, LegacyFlow, LegacySettings, {}, LegacyPairs,
    LegacyGrants, LegacyResults, LegacySummary, &LegacyTrace);

  TArray<FCrowdLocalPredictivePair> CorePairs;
  TArray<FCrowdLocalPredictiveGrantState> CoreGrants;
  TArray<FCrowdLocalPredictiveResult> CoreResults;
  FCrowdLocalPredictiveSummary CoreSummary;
  FCrowdLocalPredictiveDiagnosticTrace CoreTrace;
  FCrowdLocalPredictiveInteractionKernel::Solve(
    CoreAgents, CoreFlow, CoreSettings, {}, CorePairs, CoreGrants,
    CoreResults, CoreSummary, &CoreTrace);

  FCrowdMassLocalPredictiveWorkInput RuntimeInput;
  RuntimeInput.FixedStepIndex = 900;
  RuntimeInput.PlanRevision = 17;
  RuntimeInput.Environment = CoreFlow;
  RuntimeInput.Settings = CoreSettings;
  RuntimeInput.Agents = CoreAgents;
  RuntimeInput.bCaptureDiagnostic = true;
  const FCrowdMassLocalPredictiveWorkOutput RuntimeOutput =
    FCrowdMassLocalPredictiveWork::Solve(RuntimeInput);
  TestTrue(TEXT("Runtime WORK completes equivalent fixture"),
    RuntimeOutput.bCompleted);
  TestEqual(TEXT("Runtime WORK preserves candidate hash"),
    RuntimeOutput.Summary.CandidateHash, LegacySummary.CandidateHash);
  TestEqual(TEXT("Runtime WORK preserves result count"),
    RuntimeOutput.Results.Num(), LegacyResults.Num());

  TestEqual(TEXT("summary valid"), CoreSummary.bValid, LegacySummary.bValid);
  TestEqual(TEXT("summary candidate hash"),
    CoreSummary.CandidateHash, LegacySummary.CandidateHash);
  TestEqual(TEXT("summary conflict pairs"),
    CoreSummary.ConflictPairCount, LegacySummary.ConflictPairCount);
  TestEqual(TEXT("summary component count"),
    CoreSummary.ComponentCount, LegacySummary.ComponentCount);
  TestEqual(TEXT("summary joint recovery components"),
    CoreSummary.JointPreferredRecoveryComponentCount,
    LegacySummary.JointPreferredRecoveryComponentCount);
  TestEqual(TEXT("summary joint recovery agents"),
    CoreSummary.JointPreferredRecoveryAgentCount,
    LegacySummary.JointPreferredRecoveryAgentCount);

  TestEqual(TEXT("pair count"), CorePairs.Num(), LegacyPairs.Num());
  for (int32 Index = 0; Index < LegacyPairs.Num(); ++Index)
  {
    TestEqual(TEXT("pair min id"),
      CorePairs[Index].MinAgentId, LegacyPairs[Index].MinAgentId);
    TestEqual(TEXT("pair max id"),
      CorePairs[Index].MaxAgentId, LegacyPairs[Index].MaxAgentId);
    TestEqual(TEXT("pair bucket"),
      CorePairs[Index].DistanceBucket, LegacyPairs[Index].DistanceBucket);
    TestEqual(TEXT("pair closest time"),
      CorePairs[Index].ClosestTimeSeconds,
      LegacyPairs[Index].ClosestTimeSeconds);
    TestEqual(TEXT("pair separation"),
      CorePairs[Index].PredictedSeparationCm,
      LegacyPairs[Index].PredictedSeparationCm);
  }
  TestEqual(TEXT("grant count"), CoreGrants.Num(), LegacyGrants.Num());
  for (int32 Index = 0; Index < LegacyGrants.Num(); ++Index)
  {
    TestEqual(TEXT("grant component"),
      CoreGrants[Index].ComponentKey, LegacyGrants[Index].ComponentKey);
    TestEqual(TEXT("granted agent"),
      CoreGrants[Index].GrantedAgentId,
      LegacyGrants[Index].GrantedAgentId);
    TestEqual(TEXT("grant epoch"),
      CoreGrants[Index].GrantEpoch, LegacyGrants[Index].GrantEpoch);
    TestEqual(TEXT("grant remaining"),
      CoreGrants[Index].RemainingSteps,
      LegacyGrants[Index].RemainingSteps);
  }
  TestEqual(TEXT("result count"), CoreResults.Num(), LegacyResults.Num());
  for (int32 Index = 0; Index < LegacyResults.Num(); ++Index)
  {
    TestEqual(TEXT("result agent"),
      CoreResults[Index].AgentId, LegacyResults[Index].AgentId);
    TestTrue(TEXT("result velocity"),
      CoreResults[Index].Velocity.Equals(
        LegacyResults[Index].Velocity, 0.0f));
    TestEqual(TEXT("result component"),
      CoreResults[Index].ComponentKey,
      LegacyResults[Index].ComponentKey);
    TestEqual(TEXT("result valid"),
      CoreResults[Index].bValid, LegacyResults[Index].bValid);
    TestTrue(TEXT("Runtime WORK result velocity"),
      RuntimeOutput.Results[Index].Velocity.Equals(
        LegacyResults[Index].Velocity, 0.0f));
  }

  const FCrowdDemoLocalPredictiveSummary AdaptedSummary =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveSummary(
      RuntimeOutput.Summary);
  TestEqual(TEXT("Runtime summary adapts to Demo hash"),
    AdaptedSummary.CandidateHash, LegacySummary.CandidateHash);
  TestEqual(TEXT("Runtime summary adapts joint recovery count"),
    AdaptedSummary.JointPreferredRecoveryAgentCount,
    LegacySummary.JointPreferredRecoveryAgentCount);
  Algo::Reverse(RuntimeInput.Agents);
  const FCrowdMassLocalPredictiveWorkOutput RuntimeReversed =
    FCrowdMassLocalPredictiveWork::Solve(RuntimeInput);
  TestEqual(TEXT("Runtime WORK order-independent hash"),
    RuntimeReversed.StableHash, RuntimeOutput.StableHash);

  FCrowdDemoLocalPredictiveComponentFixture LegacyFixture;
  FCrowdLocalPredictiveComponentFixture CoreFixture;
  TestTrue(TEXT("legacy component fixture builds"),
    FCrowdDemoLocalPredictiveInteractionKernel::BuildComponentFixture(
      900, LegacyAgents, LegacyFlow, LegacySettings, {}, LegacyPairs,
      LegacyGrants, LegacyResults, LegacySummary, LegacyTrace, {5,19},
      LegacyFixture));
  TestTrue(TEXT("core component fixture builds"),
    FCrowdLocalPredictiveInteractionKernel::BuildComponentFixture(
      900, CoreAgents, CoreFlow, CoreSettings, {}, CorePairs, CoreGrants,
      CoreResults, CoreSummary, CoreTrace, {5,19}, CoreFixture));
  TestEqual(TEXT("component fixture hash"),
    CoreFixture.StableHash, LegacyFixture.StableHash);
  TestEqual(TEXT("component fixture agent count"),
    CoreFixture.Agents.Num(), LegacyFixture.Agents.Num());
  return true;
}

#endif
