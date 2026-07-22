#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdParticleConstraintKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "MassCrowdParticleWork.h"
#include "Misc/AutomationTest.h"

namespace
{
  TArray<FCrowdDemoParticleConstraintAgent> MakeLegacy8372Agents()
  {
    TArray<FCrowdDemoParticleConstraintAgent> Agents;
    const auto AddAgent = [&Agents](
      const int32 AgentId, const FVector& Start, const FVector& Predict)
    {
      FCrowdDemoParticleConstraintAgent Agent;
      Agent.AgentId = AgentId;
      Agent.StartPosition = Start;
      Agent.PredictedPosition = Predict;
      Agent.PhysicalRadiusCm = 42.0f;
      Agent.HardSafetyGapCm = 10.0f;
      Agent.SoftMarginCm = 17.0f;
      Agent.Mobility = 1.0f;
      Agents.Add(Agent);
    };
    AddAgent(0, FVector(1874,-961,60), FVector(1900.392f,-957.180f,60));
    AddAgent(1, FVector(2065,-959,60), FVector(2091.518f,-956.192f,60));
    AddAgent(2, FVector(2254,-953,60), FVector(2280.654f,-952.167f,60));
    AddAgent(3, FVector(2275,-1049,60), FVector(2291.103f,-1027.744f,60));
    AddAgent(4, FVector(2399,-1044,60), FVector(2411.717f,-1020.561f,60));
    AddAgent(5, FVector(2463,-820,60), FVector(2458.131f,-793.782f,60));
    AddAgent(6, FVector(2538,-968,60), FVector(2522.058f,-946.623f,60));
    AddAgent(7, FVector(2356,-549,60), FVector(2336.511f,-530.798f,60));
    AddAgent(8, FVector(2204,-393,60), FVector(2194.579f,-368.053f,60));
    AddAgent(9, FVector(2037,-252,60), FVector(2019.695f,-231.711f,60));
    AddAgent(10, FVector(1969,-960,60), FVector(1995.466f,-956.733f,60));
    AddAgent(11, FVector(2160,-966,60), FVector(2186.255f,-961.332f,60));
    AddAgent(12, FVector(2349,-957,60), FVector(2375.603f,-955.156f,60));
    AddAgent(13, FVector(2444,-953,60), FVector(2445.551f,-926.378f,60));
    AddAgent(14, FVector(2462,-719,60), FVector(2457.431f,-692.728f,60));
    AddAgent(15, FVector(2436,-625,60), FVector(2415.902f,-607.473f,60));
    AddAgent(16, FVector(2276,-475,60), FVector(2257.069f,-456.219f,60));
    AddAgent(17, FVector(2148,-297,60), FVector(2133.208f,-274.812f,60));
    AddAgent(18, FVector(1998,-129,60), FVector(1974.475f,-116.443f,60));
    AddAgent(19, FVector(1875,-73,60), FVector(1855.992f,-54.296f,60));
    return Agents;
  }

  FCrowdParticleConstraintAgent ToCoreAgent(
    const FCrowdDemoParticleConstraintAgent& Source)
  {
    FCrowdParticleConstraintAgent Result;
    Result.AgentId = Source.AgentId;
    Result.StartPosition = Source.StartPosition;
    Result.PredictedPosition = Source.PredictedPosition;
    Result.PhysicalRadiusCm = Source.PhysicalRadiusCm;
    Result.HardSafetyGapCm = Source.HardSafetyGapCm;
    Result.EnvironmentHardClearanceCm = Source.EnvironmentHardClearanceCm;
    Result.SoftMarginCm = Source.SoftMarginCm;
    Result.Mobility = Source.Mobility;
    return Result;
  }

  void CompareVectorArray(
    FAutomationTestBase& Test,
    const TCHAR* Label,
    TConstArrayView<FVector> Legacy,
    TConstArrayView<FVector> Core)
  {
    Test.TestEqual(FString::Printf(TEXT("%s count"), Label),
      Core.Num(), Legacy.Num());
    const int32 Count = FMath::Min(Core.Num(), Legacy.Num());
    for (int32 Index = 0; Index < Count; ++Index)
      Test.TestTrue(FString::Printf(TEXT("%s value %d"), Label, Index),
        Core[Index].Equals(Legacy[Index], 0.0f));
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleConstraintPluginEquivalenceTest,
  "CrowdDemo.SoftPressure.Particle.PluginCoreEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleConstraintPluginEquivalenceTest::RunTest(
  const FString& Parameters)
{
  const TArray<FCrowdDemoParticleConstraintAgent> LegacyAgents =
    MakeLegacy8372Agents();
  TArray<FCrowdParticleConstraintAgent> CoreAgents;
  for (const FCrowdDemoParticleConstraintAgent& Agent : LegacyAgents)
    CoreAgents.Add(ToCoreAgent(Agent));

  FCrowdDemoParticleConstraintEnvironment LegacyEnvironment;
  LegacyEnvironment.FlowConfig =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  LegacyEnvironment.bConstrainToFlowBounds = true;
  FCrowdParticleConstraintEnvironment CoreEnvironment;
  CoreEnvironment.FlowConfig = FCrowdSharedFlowFieldKernel::MakeSf1Config(1);
  CoreEnvironment.bConstrainToFlowBounds = true;

  FCrowdDemoParticleConstraintSettings LegacySettings;
  LegacySettings.bCaptureSafetyStageTrace = true;
  LegacySettings.bCaptureRouteDiagnostic = true;
  FCrowdParticleConstraintSettings CoreSettings;
  CoreSettings.bCaptureSafetyStageTrace = true;
  CoreSettings.bCaptureRouteDiagnostic = true;

  TArray<FCrowdDemoParticleConstraintPair> LegacyPairs;
  TArray<FCrowdDemoParticleConstraintResult> LegacyResults;
  FCrowdDemoParticleConstraintSummary LegacySummary;
  FCrowdDemoParticleConstraintTrace LegacyTrace;
  FCrowdDemoParticleConstraintKernel::Solve(
    LegacyAgents, LegacyEnvironment, LegacySettings, LegacyPairs,
    LegacyResults, LegacySummary, &LegacyTrace);

  TArray<FCrowdParticleConstraintPair> CorePairs;
  TArray<FCrowdParticleConstraintResult> CoreResults;
  FCrowdParticleConstraintSummary CoreSummary;
  FCrowdParticleConstraintTrace CoreTrace;
  FCrowdParticleConstraintKernel::Solve(
    CoreAgents, CoreEnvironment, CoreSettings, CorePairs, CoreResults,
    CoreSummary, &CoreTrace);

  FCrowdMassParticleWorkInput RuntimeInput;
  RuntimeInput.FixedStepIndex = 155;
  RuntimeInput.PlanRevision = 1;
  RuntimeInput.Environment = CoreEnvironment;
  RuntimeInput.Settings = CoreSettings;
  RuntimeInput.Agents = CoreAgents;
  RuntimeInput.bCaptureTrace = true;
  const FCrowdMassParticleWorkOutput RuntimeForward =
    FCrowdMassParticleWork::Solve(RuntimeInput);
  TestTrue(TEXT("Runtime particle WORK completed"), RuntimeForward.bCompleted);
  TestEqual(TEXT("Runtime candidate hash"),
    RuntimeForward.Summary.CandidateHash, CoreSummary.CandidateHash);
  Algo::Reverse(RuntimeInput.Agents);
  const FCrowdMassParticleWorkOutput RuntimeReverse =
    FCrowdMassParticleWork::Solve(RuntimeInput);
  TestEqual(TEXT("Runtime input reversal stable hash"),
    RuntimeReverse.StableHash, RuntimeForward.StableHash);
  const FCrowdDemoParticleConstraintSummary AdaptedSummary =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleSummary(
      RuntimeForward.Summary);
  TestEqual(TEXT("Runtime summary adapts to legacy hash"),
    AdaptedSummary.CandidateHash, LegacySummary.CandidateHash);
  const FCrowdDemoParticleConstraintTrace AdaptedTrace =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleTrace(
      RuntimeForward.Trace);
  CompareVectorArray(*this, TEXT("Runtime adapted final safety"),
    LegacyTrace.FinalSafetyPositions, AdaptedTrace.FinalSafetyPositions);

  TestEqual(TEXT("summary validity"), CoreSummary.bValid, LegacySummary.bValid);
  TestEqual(TEXT("candidate hash"),
    CoreSummary.CandidateHash, LegacySummary.CandidateHash);
  TestEqual(TEXT("candidate pair count"),
    CoreSummary.CandidatePairCount, LegacySummary.CandidatePairCount);
  TestEqual(TEXT("soft pair count"),
    CoreSummary.SoftPairCount, LegacySummary.SoftPairCount);
  TestEqual(TEXT("hard violations"),
    CoreSummary.HardPairViolationCount,
    LegacySummary.HardPairViolationCount);
  TestEqual(TEXT("swept violations"),
    CoreSummary.SweptPairViolationCount,
    LegacySummary.SweptPairViolationCount);
  TestEqual(TEXT("obstacle violations"),
    CoreSummary.ObstaclePenetrationCount,
    LegacySummary.ObstaclePenetrationCount);
  TestEqual(TEXT("bounds violations"),
    CoreSummary.BoundsViolationCount, LegacySummary.BoundsViolationCount);
  TestEqual(TEXT("unified constraint count"),
    CoreSummary.UnifiedHardConstraintCount,
    LegacySummary.UnifiedHardConstraintCount);
  TestEqual(TEXT("unified infeasible count"),
    CoreSummary.UnifiedHardInfeasibleCount,
    LegacySummary.UnifiedHardInfeasibleCount);
  TestEqual(TEXT("soft error p95"),
    CoreSummary.SoftErrorCmP95, LegacySummary.SoftErrorCmP95);
  TestEqual(TEXT("maximum correction"),
    CoreSummary.MaxAgentCorrectionCm, LegacySummary.MaxAgentCorrectionCm);

  TestEqual(TEXT("pair count"), CorePairs.Num(), LegacyPairs.Num());
  for (int32 Index = 0; Index < LegacyPairs.Num(); ++Index)
  {
    TestEqual(TEXT("pair min agent"),
      CorePairs[Index].MinAgentId, LegacyPairs[Index].MinAgentId);
    TestEqual(TEXT("pair max agent"),
      CorePairs[Index].MaxAgentId, LegacyPairs[Index].MaxAgentId);
    TestEqual(TEXT("pair min index"),
      CorePairs[Index].MinAgentIndex, LegacyPairs[Index].MinAgentIndex);
    TestEqual(TEXT("pair max index"),
      CorePairs[Index].MaxAgentIndex, LegacyPairs[Index].MaxAgentIndex);
  }
  TestEqual(TEXT("result count"), CoreResults.Num(), LegacyResults.Num());
  for (int32 Index = 0; Index < LegacyResults.Num(); ++Index)
  {
    TestEqual(TEXT("result agent"),
      CoreResults[Index].AgentId, LegacyResults[Index].AgentId);
    TestTrue(TEXT("result position"),
      CoreResults[Index].CorrectedPosition.Equals(
        LegacyResults[Index].CorrectedPosition, 0.0f));
    TestTrue(TEXT("result velocity"),
      CoreResults[Index].CorrectedVelocity.Equals(
        LegacyResults[Index].CorrectedVelocity, 0.0f));
    TestTrue(TEXT("result realized correction"),
      CoreResults[Index].RealizedCorrection.Equals(
        LegacyResults[Index].RealizedCorrection, 0.0f));
    TestEqual(TEXT("result first influence"),
      CoreResults[Index].FirstInfluencedIteration,
      LegacyResults[Index].FirstInfluencedIteration);
    TestEqual(TEXT("result corrected pair count"),
      CoreResults[Index].CorrectedPairCount,
      LegacyResults[Index].CorrectedPairCount);
  }

  CompareVectorArray(*this, TEXT("predict"),
    LegacyTrace.PredictPositions, CoreTrace.PredictPositions);
  CompareVectorArray(*this, TEXT("soft"),
    LegacyTrace.SoftPositions, CoreTrace.SoftPositions);
  CompareVectorArray(*this, TEXT("environment soft"),
    LegacyTrace.EnvironmentSoftPositions,
    CoreTrace.EnvironmentSoftPositions);
  CompareVectorArray(*this, TEXT("unified hard"),
    LegacyTrace.UnifiedHardPositions, CoreTrace.UnifiedHardPositions);
  CompareVectorArray(*this, TEXT("quantized"),
    LegacyTrace.QuantizedPositions, CoreTrace.QuantizedPositions);
  CompareVectorArray(*this, TEXT("final safety"),
    LegacyTrace.FinalSafetyPositions, CoreTrace.FinalSafetyPositions);
  TestEqual(TEXT("safety stage count"),
    CoreTrace.SafetyStages.Num(), LegacyTrace.SafetyStages.Num());
  TestEqual(TEXT("final environment contact count"),
    CoreTrace.FinalEnvironmentContacts.Num(),
    LegacyTrace.FinalEnvironmentContacts.Num());
  TestEqual(TEXT("final hard constraint count"),
    CoreTrace.FinalHardConstraints.Num(),
    LegacyTrace.FinalHardConstraints.Num());

  TArray<FCrowdDemoParticleAppliedState> LegacyApplied;
  TArray<FCrowdParticleAppliedState> CoreApplied;
  for (int32 Index = 0; Index < LegacyResults.Num(); ++Index)
  {
    LegacyApplied.Add({LegacyResults[Index].AgentId,
      LegacyResults[Index].CorrectedPosition,
      LegacyResults[Index].CorrectedVelocity});
    CoreApplied.Add({CoreResults[Index].AgentId,
      CoreResults[Index].CorrectedPosition,
      CoreResults[Index].CorrectedVelocity});
  }
  FCrowdDemoParticleConstraintSummary LegacyAppliedSummary;
  FCrowdParticleConstraintSummary CoreAppliedSummary;
  uint32 LegacyAppliedHash = 0;
  uint32 CoreAppliedHash = 0;
  FCrowdDemoParticleConstraintKernel::EvaluateAppliedState(
    LegacyAgents, LegacyApplied, LegacyEnvironment,
    LegacyAppliedSummary, LegacyAppliedHash);
  FCrowdParticleConstraintKernel::EvaluateAppliedState(
    CoreAgents, CoreApplied, CoreEnvironment,
    CoreAppliedSummary, CoreAppliedHash);
  TestEqual(TEXT("applied state hash"), CoreAppliedHash, LegacyAppliedHash);
  TestEqual(TEXT("Runtime WORK applied state hash"),
    RuntimeForward.AppliedStateHash, LegacyAppliedHash);
  TestEqual(TEXT("applied state validity"),
    CoreAppliedSummary.bValid, LegacyAppliedSummary.bValid);
  return true;
}

#endif
