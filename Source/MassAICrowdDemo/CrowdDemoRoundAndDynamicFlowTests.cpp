#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoRoundInitialStateKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoTargetFactKernel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRoundStableInitialStateTest,
  "CrowdDemo.SF.RoundContract.StableInitialState",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRoundStableInitialStateTest::RunTest(const FString& Parameters)
{
  FCrowdDemoRoundRules Rules;
  Rules.Scenario = ECrowdDemoScenario::SimRoundSoftPressure;
  Rules.SoftPressureTestCase = ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving;
  Rules.RoundStartPolicy = ECrowdDemoRoundStartPolicy::ResetToStableInitialState;
  Rules.FormationColumns = 5;
  Rules.FormationSpacingCm = 128.0f;
  Rules.SpawnOrigin = FVector(-800.0f, -1200.0f, 60.0f);
  Rules.TargetMotion.InitialLocation = FVector(0.0f, 900.0f, 60.0f);
  Rules.TargetMotion.LinearVelocity = FVector(-80.0f, 0.0f, 0.0f);

  TArray<FCrowdDemoRoundInitialStateAgent> Agents;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = 100 + Index;
    Agent.LifecycleSerial = 7;
    Agent.FormationIndex = Index;
    Agent.CapabilityProfileKey = Index % 2;
    Agent.RadiusCm = Index % 2 == 0 ? 42.0f : 34.0f;
  }
  TArray<FCrowdDemoRoundInitialStateResult> Round1;
  TArray<FCrowdDemoRoundInitialStateResult> Round2;
  FCrowdDemoRoundInitialStateSummary Summary1;
  FCrowdDemoRoundInitialStateSummary Summary2;
  TestTrue(TEXT("Round 1 state builds"),
    FCrowdDemoRoundInitialStateKernel::BuildGeneric(Agents, Rules, Round1, Summary1));
  Algo::Reverse(Agents);
  TestTrue(TEXT("Round 2 reversed input builds"),
    FCrowdDemoRoundInitialStateKernel::BuildGeneric(Agents, Rules, Round2, Summary2));
  TestEqual(TEXT("Round input hash ignores input order"), Summary2.InputHash, Summary1.InputHash);
  TestEqual(TEXT("Round initial state hash ignores input order"),
    Summary2.InitialStateHash, Summary1.InitialStateHash);
  TestEqual(TEXT("Round 1 and Round 2 have equal full state count"), Round2.Num(), Round1.Num());

  Rules.TargetMotion.InitialLocation.X += 100.0f;
  FCrowdDemoRoundInitialStateSummary ChangedSummary;
  TArray<FCrowdDemoRoundInitialStateResult> Changed;
  TestTrue(TEXT("changed target input builds"),
    FCrowdDemoRoundInitialStateKernel::BuildGeneric(Agents, Rules, Changed, ChangedSummary));
  TestNotEqual(TEXT("target initial fact changes round input hash"),
    ChangedSummary.InputHash, Summary1.InputHash);
  TestEqual(TEXT("target fact does not silently move formation"),
    ChangedSummary.InitialStateHash, Summary1.InitialStateHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoReflectedTargetMotionTest,
  "CrowdDemo.SF.RoundContract.ReflectedTargetMotion",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoReflectedTargetMotionTest::RunTest(const FString& Parameters)
{
  constexpr float FixedStepSeconds = 1.0f / 30.0f;
  const FVector2f Initial(0.0f, 1900.0f);
  const FVector2f Velocity(-80.0f, 0.0f);
  const FVector2f Minimum(-2480.0f, -3180.0f);
  const FVector2f Maximum(2480.0f, 2180.0f);
  const auto Build = [&](const int32 Step)
  {
    return FCrowdDemoTargetFactKernel::BuildReflectedLinearMotionFact(
      1, 1, Step, Initial, Velocity, Minimum, Maximum,
      0.0f, 0.0f, 100.0f, FixedStepSeconds, 1.0f, 1.0f);
  };

  const FCrowdDemoTargetFact BeforeReflection = Build(900);
  TestEqual(TEXT("bounded motion matches original linear path at thirty seconds"),
    BeforeReflection.Location.X, -2400.0f);
  TestTrue(TEXT("target still moves left before reflection"),
    BeforeReflection.Velocity.X < 0.0f);

  const FCrowdDemoTargetFact AfterReflection = Build(945);
  TestEqual(TEXT("target reflects back from the declared minimum"),
    AfterReflection.Location.X, -2440.0f);
  TestTrue(TEXT("target velocity reverses after reflection"),
    AfterReflection.Velocity.X > 0.0f);

  for (int32 Step = 0; Step <= 1350; ++Step)
  {
    const FCrowdDemoTargetFact Fact = Build(Step);
    TestTrue(TEXT("target remains inside declared X motion bounds"),
      Fact.Location.X >= Minimum.X && Fact.Location.X <= Maximum.X);
    TestTrue(TEXT("zero Y velocity preserves target Y"),
      FMath::IsNearlyEqual(Fact.Location.Y, Initial.Y, 0.01f));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoDynamicSharedFlowAnchorTest,
  "CrowdDemo.SF.Flow.DynamicGoalAnchorIntegration",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoDynamicSharedFlowAnchorTest::RunTest(const FString& Parameters)
{
  FCrowdDemoSharedFlowFieldConfig Config;
  Config.Revision = 91;
  Config.BoundsMin = FVector(0.0f, 0.0f, 0.0f);
  Config.BoundsMax = FVector(1000.0f, 500.0f, 0.0f);
  Config.CellSizeCm = 100.0f;
  Config.AgentInflateCm = 0.0f;
  Config.ConnectivityContractVersion = 2;
  Config.GoalLocation = FVector(950.0f, 250.0f, 60.0f);

  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("V2 topology builds"),
    FCrowdDemoSharedFlowFieldKernel::BuildTopology(Config, Field));
  const uint32 TopologyHash = Field.TopologyHash;
  int32 RightAnchor = INDEX_NONE;
  FVector RightLocation;
  TestTrue(TEXT("right goal anchor resolves"),
    FCrowdDemoSharedFlowFieldKernel::ResolveGoalAnchor(
      Field, FVector(950.0f, 250.0f, 60.0f), RightAnchor, RightLocation));
  TestTrue(TEXT("right integration builds"),
    FCrowdDemoSharedFlowFieldKernel::BuildIntegrationForAnchor(
      RightAnchor, RightLocation, Field));
  const uint32 RightIntegrationHash = Field.IntegrationHash;
  const FVector Probe(450.0f, 250.0f, 60.0f);
  const FVector RightDirection =
    FCrowdDemoSharedFlowFieldKernel::Sample(Field, Probe).FlowDirection;

  int32 LeftAnchor = INDEX_NONE;
  FVector LeftLocation;
  TestTrue(TEXT("left goal anchor resolves"),
    FCrowdDemoSharedFlowFieldKernel::ResolveGoalAnchor(
      Field, FVector(50.0f, 250.0f, 60.0f), LeftAnchor, LeftLocation));
  TestNotEqual(TEXT("moving target crosses stable anchor cell"), LeftAnchor, RightAnchor);
  TestTrue(TEXT("left integration builds"),
    FCrowdDemoSharedFlowFieldKernel::BuildIntegrationForAnchor(
      LeftAnchor, LeftLocation, Field));
  const FVector LeftDirection =
    FCrowdDemoSharedFlowFieldKernel::Sample(Field, Probe).FlowDirection;
  TestEqual(TEXT("dynamic integration preserves topology"), Field.TopologyHash, TopologyHash);
  TestNotEqual(TEXT("dynamic anchor changes integration hash"),
    Field.IntegrationHash, RightIntegrationHash);
  TestTrue(TEXT("far-field direction changes with spatial anchor"),
    FVector::DotProduct(RightDirection, LeftDirection) < -0.5f);
  return true;
}

#endif
