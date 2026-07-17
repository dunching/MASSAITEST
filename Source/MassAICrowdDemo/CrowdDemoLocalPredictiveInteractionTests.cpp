#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

namespace
{
FCrowdDemoSharedFlowFieldConfig MakeOpenEnvironment()
{
  FCrowdDemoSharedFlowFieldConfig Config;
  Config.BoundsMin = FVector(-2000, -2000, 0);
  Config.BoundsMax = FVector(2000, 2000, 0);
  Config.ObstacleSpecs.Reset();
  return Config;
}

FCrowdDemoLocalPredictiveAgent MakeAgent(
  const int32 AgentId,
  const FVector2f Position,
  const FVector2f Preferred,
  const int32 BlockedAge = 0,
  const float Radius = 42.0f)
{
  FCrowdDemoLocalPredictiveAgent Agent;
  Agent.AgentId = AgentId;
  Agent.Position = Position;
  Agent.PreferredVelocity = Preferred;
  Agent.PhysicalRadiusCm = Radius;
  Agent.HardSafetyGapCm = 10.0f;
  Agent.MaxSpeedCmps = 800.0f;
  Agent.BlockedAgeSteps = BlockedAge;
  return Agent;
}

struct FSolveFixture
{
  TArray<FCrowdDemoLocalPredictivePair> Pairs;
  TArray<FCrowdDemoLocalPredictiveGrantState> Grants;
  TArray<FCrowdDemoLocalPredictiveResult> Results;
  FCrowdDemoLocalPredictiveSummary Summary;
  FCrowdDemoLocalPredictiveDiagnosticTrace Trace;
};

FSolveFixture Solve(
  const TArray<FCrowdDemoLocalPredictiveAgent>& Agents,
  const FCrowdDemoSharedFlowFieldConfig& Environment,
  const TArray<FCrowdDemoLocalPredictiveGrantState>& Previous = {})
{
  FSolveFixture Fixture;
  FCrowdDemoLocalPredictiveSettings Settings;
  FCrowdDemoLocalPredictiveInteractionKernel::Solve(
    Agents, Environment, Settings, Previous,
    Fixture.Pairs, Fixture.Grants, Fixture.Results, Fixture.Summary,
    &Fixture.Trace);
  return Fixture;
}

const FCrowdDemoLocalPredictiveResult* FindResult(
  const FSolveFixture& Fixture, const int32 AgentId)
{
  return Fixture.Results.FindByPredicate(
    [&](const auto& Result) { return Result.AgentId == AgentId; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoLocalPredictiveInteractionTest,
  "CrowdDemo.SF.LocalPredictive.GenericInteraction",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoLocalPredictiveInteractionTest::RunTest(const FString& Parameters)
{
  const FCrowdDemoSharedFlowFieldConfig Open = MakeOpenEnvironment();
  {
    const TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(1, FVector2f(0, -200), FVector2f(300, 0)),
      MakeAgent(2, FVector2f(0, 200), FVector2f(300, 0))};
    const FSolveFixture Fixture = Solve(Agents, Open);
    TestTrue(TEXT("parallel no-conflict fixture valid"), Fixture.Summary.bValid);
    TestEqual(TEXT("parallel creates no conflict"), Fixture.Summary.ConflictPairCount, 0);
    TestEqual(TEXT("parallel creates no grant"), Fixture.Summary.GrantedAgentCount, 0);
    for (const auto& Result : Fixture.Results)
      TestTrue(TEXT("parallel preserves preferred"),
        Result.Velocity.Equals(FVector2f(300, 0), 0.0f));
  }
  {
    TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(10, FVector2f(-200, 0), FVector2f(300, 0), 12),
      MakeAgent(11, FVector2f(200, 0), FVector2f(-300, 0), 2)};
    const FSolveFixture Forward = Solve(Agents, Open);
    TestTrue(TEXT("head-on fixture valid"), Forward.Summary.bValid);
    TestEqual(TEXT("head-on creates one conflict"), Forward.Summary.ConflictPairCount, 1);
    TestEqual(TEXT("head-on creates one component"), Forward.Summary.ComponentCount, 1);
    TestNotNull(TEXT("head-on result A"), FindResult(Forward, 10));
    TestNotNull(TEXT("head-on result B"), FindResult(Forward, 11));
    TestEqual(TEXT("older blocked agent receives grant"),
      Forward.Grants[0].GrantedAgentId, 10);
    TestTrue(TEXT("granted agent keeps nonzero progress"),
      FVector2f::DotProduct(FindResult(Forward, 10)->Velocity, FVector2f(1, 0)) > 0.0f);
    FCrowdDemoLocalPredictiveSettings FixtureSettings;
    FCrowdDemoLocalPredictiveComponentFixture ForwardComponent;
    TestTrue(TEXT("head-on component fixture builds"),
      FCrowdDemoLocalPredictiveInteractionKernel::BuildComponentFixture(
        17, Agents, Open, FixtureSettings, {}, Forward.Pairs, Forward.Grants,
        Forward.Results, Forward.Summary, Forward.Trace, {10}, ForwardComponent));
    TestEqual(TEXT("head-on component fixture includes both agents"),
      ForwardComponent.Agents.Num(), 2);
    Algo::Reverse(Agents);
    const FSolveFixture Reversed = Solve(Agents, Open);
    TestTrue(TEXT("head-on reverse fixture valid"), Reversed.Summary.bValid);
    TestEqual(TEXT("head-on reverse hash stable"),
      Reversed.Summary.CandidateHash, Forward.Summary.CandidateHash);
    for (int32 Index = 0; Index < Forward.Results.Num(); ++Index)
    {
      TestEqual(TEXT("head-on result id stable"),
        Reversed.Results[Index].AgentId, Forward.Results[Index].AgentId);
      TestTrue(TEXT("head-on result velocity stable"),
        Reversed.Results[Index].Velocity.Equals(Forward.Results[Index].Velocity, 0.0f));
    }
    FCrowdDemoLocalPredictiveComponentFixture ReversedComponent;
    TestTrue(TEXT("head-on reversed component fixture builds"),
      FCrowdDemoLocalPredictiveInteractionKernel::BuildComponentFixture(
        17, Agents, Open, FixtureSettings, {}, Reversed.Pairs, Reversed.Grants,
        Reversed.Results, Reversed.Summary, Reversed.Trace, {10}, ReversedComponent));
    TestEqual(TEXT("component fixture input-order hash stable"),
      ReversedComponent.StableHash, ForwardComponent.StableHash);
  }
  {
    const TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(20, FVector2f(-200, 0), FVector2f(300, 0)),
      MakeAgent(21, FVector2f(0, -200), FVector2f(0, 300))};
    const FSolveFixture Fixture = Solve(Agents, Open);
    TestTrue(TEXT("crossing fixture valid"), Fixture.Summary.bValid);
    TestEqual(TEXT("crossing conflict"), Fixture.Summary.ConflictPairCount, 1);
  }
  {
    TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(22, FVector2f(-250, 0), FVector2f(400, 0)),
      MakeAgent(23, FVector2f(0, 0), FVector2f(100, 0))};
    Agents[0].Velocity = FVector2f(400, 0);
    Agents[1].Velocity = FVector2f(100, 0);
    const FSolveFixture Fixture = Solve(Agents, Open);
    TestTrue(TEXT("overtake fixture valid"), Fixture.Summary.bValid);
    TestEqual(TEXT("overtake conflict"), Fixture.Summary.ConflictPairCount, 1);
  }
  {
    const TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(24, FVector2f(-240, 0), FVector2f(300, 0)),
      MakeAgent(25, FVector2f(120, -208), FVector2f(-150, 260)),
      MakeAgent(26, FVector2f(120, 208), FVector2f(-150, -260))};
    const FSolveFixture Fixture = Solve(Agents, Open);
    TestTrue(TEXT("three-way crossing fixture valid"), Fixture.Summary.bValid);
    TestEqual(TEXT("three-way crossing component"), Fixture.Summary.ComponentCount, 1);
    TestEqual(TEXT("three-way crossing component size"), Fixture.Summary.MaxComponentSize, 3);
  }
  {
    const TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(30, FVector2f(-160, -40), FVector2f(300, 80)),
      MakeAgent(31, FVector2f(160, 40), FVector2f(-300, -80))};
    const FSolveFixture Fixture = Solve(Agents, Open);
    TestTrue(TEXT("shared target approach valid"), Fixture.Summary.bValid);
    TestEqual(TEXT("shared target approach conflict"), Fixture.Summary.ConflictPairCount, 1);
  }
  {
    FCrowdDemoSharedFlowFieldConfig WithWall = Open;
    FCrowdDemoSharedFlowObstacleSpec Wall;
    Wall.ObstacleId = 100;
    Wall.Center = FVector(0, 0, 0);
    Wall.Extent = FVector(100, 500, 100);
    WithWall.ObstacleSpecs.Add(Wall);
    const TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(40, FVector2f(-160, 0), FVector2f(300, 0))};
    const FSolveFixture Fixture = Solve(Agents, WithWall);
    TestTrue(TEXT("wall-constrained fixture valid"), Fixture.Summary.bValid);
    TestTrue(TEXT("wall adds environment constraint"),
      Fixture.Summary.EnvironmentConstraintCount > 0);
    TestTrue(TEXT("wall clips forward velocity"),
      FindResult(Fixture, 40)->Velocity.X <= 240.0f);
  }
  {
    const TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(50, FVector2f(-100, 0), FVector2f(800, 0), 0, 10),
      MakeAgent(51, FVector2f(100, 0), FVector2f(-800, 0), 0, 10)};
    const FSolveFixture Fixture = Solve(Agents, Open);
    TestTrue(TEXT("high-speed exchange fixture valid"), Fixture.Summary.bValid);
    TestEqual(TEXT("high-speed exchange candidate retained"),
      Fixture.Summary.ConflictPairCount, 1);
  }
  {
    FCrowdDemoSharedFlowFieldConfig Corridor = Open;
    FCrowdDemoSharedFlowObstacleSpec Upper;
    Upper.ObstacleId = 200;
    Upper.Center = FVector(0, 250, 0);
    Upper.Extent = FVector(600, 100, 100);
    FCrowdDemoSharedFlowObstacleSpec Lower = Upper;
    Lower.ObstacleId = 201;
    Lower.Center.Y = -250;
    Corridor.ObstacleSpecs = {Upper, Lower};
    const TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(52, FVector2f(-250, 0), FVector2f(300, 0), 5),
      MakeAgent(53, FVector2f(250, 0), FVector2f(-300, 0), 4)};
    const FSolveFixture Fixture = Solve(Agents, Corridor);
    TestTrue(TEXT("untagged corridor fixture valid"), Fixture.Summary.bValid);
    TestEqual(TEXT("untagged corridor uses generic component"),
      Fixture.Summary.ComponentCount, 1);
  }
  {
    TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
      MakeAgent(60, FVector2f(-200, 0), FVector2f(300, 0), 2),
      MakeAgent(61, FVector2f(200, 0), FVector2f(-300, 0), 20)};
    FSolveFixture Fixture = Solve(Agents, Open);
    TestEqual(TEXT("higher blocked age wins first grant"),
      Fixture.Grants[0].GrantedAgentId, 61);
    TArray<FCrowdDemoLocalPredictiveGrantState> Expired = Fixture.Grants;
    Expired[0].RemainingSteps = 0;
    Agents[0].BlockedAgeSteps = 40;
    Agents[1].BlockedAgeSteps = 0;
    const FSolveFixture Next = Solve(Agents, Open, Expired);
    TestEqual(TEXT("expired grant rotates by blocked age"),
      Next.Grants[0].GrantedAgentId, 60);
    TestTrue(TEXT("grant epoch advances"),
      Next.Grants[0].GrantEpoch > Fixture.Grants[0].GrantEpoch);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoLocalPredictiveT5FinalComponentFixtureTest,
  "CrowdDemo.SF.LocalPredictive.T5FinalComponentFixture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoLocalPredictiveT5FinalComponentFixtureTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
    MakeAgent(1, FVector2f(275,1787), FVector2f(0,0), 0),
    MakeAgent(8, FVector2f(399,1781), FVector2f(-222,202), 295),
    MakeAgent(10, FVector2f(237,1906), FVector2f(0,0), 0),
    MakeAgent(11, FVector2f(373,1904), FVector2f(0,0), 0),
    MakeAgent(13, FVector2f(246,2032), FVector2f(0,0), 0),
    MakeAgent(14, FVector2f(254,2205), FVector2f(0,0), 0),
    MakeAgent(15, FVector2f(122,2036), FVector2f(0,0), 0),
    MakeAgent(16, FVector2f(344,2111), FVector2f(-286,-92), 348)};
  const FCrowdDemoSharedFlowFieldConfig Environment =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  const FSolveFixture Forward = Solve(Agents, Environment);
  TestTrue(TEXT("8517 component solve remains safe"), Forward.Summary.bValid);
  TestEqual(TEXT("8517 final conflict pair count"),
    Forward.Summary.ConflictPairCount, 8);
  TestEqual(TEXT("8517 applies one coherent translation"),
    Forward.Summary.CoherentTranslationComponentCount, 1);
  TestEqual(TEXT("8517 translates complete eight-agent component"),
    Forward.Summary.CoherentTranslationAgentCount, 8);
  TestEqual(TEXT("8517 trace contains translated component"),
    Forward.Trace.Components.Num(), 1);
  const auto* Pre8 = Forward.Trace.CompletedIndependentResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 8; });
  const auto* Pre16 = Forward.Trace.CompletedIndependentResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 16; });
  TestNotNull(TEXT("8517 pre-translation Agent 8"), Pre8);
  TestNotNull(TEXT("8517 pre-translation Agent 16"), Pre16);
  if (Pre8) TestTrue(TEXT("8517 reproduces Agent 8 independent low speed"),
    Pre8->Velocity.Equals(FVector2f(-10,9), 0.0f));
  if (Pre16) TestTrue(TEXT("8517 reproduces Agent 16 completion low speed"),
    Pre16->Velocity.Equals(FVector2f(-9,1), 0.0f));
  const auto* Final8 = FindResult(Forward, 8);
  const auto* Final16 = FindResult(Forward, 16);
  TestNotNull(TEXT("8517 final Agent 8"), Final8);
  TestNotNull(TEXT("8517 final Agent 16"), Final16);
  if (Final8)
    TestTrue(TEXT("coherent translation restores Agent 8 forward speed"),
      FVector2f::DotProduct(Final8->Velocity,
        FVector2f(-222,202).GetSafeNormal()) >= 60.0f);
  if (Final16)
    TestTrue(TEXT("coherent translation restores Agent 16 forward speed"),
      FVector2f::DotProduct(Final16->Velocity,
        FVector2f(-286,-92).GetSafeNormal()) >= 60.0f);
  TArray<FCrowdDemoLocalPredictivePair> CandidatePairs;
  FCrowdDemoLocalPredictiveSettings Settings;
  FCrowdDemoLocalPredictiveInteractionKernel::BuildCandidatePairs(
    Agents, Settings, CandidatePairs);
  TestTrue(TEXT("translated 8517 result validates against every candidate pair"),
    FCrowdDemoLocalPredictiveInteractionKernel::ValidateJointResult(
      Agents, Environment, Settings, CandidatePairs, Forward.Results));

  FCrowdDemoLocalPredictiveComponentFixture ComponentFixture;
  TestTrue(TEXT("8517 component fixture builds"),
    FCrowdDemoLocalPredictiveInteractionKernel::BuildComponentFixture(
      900, Agents, Environment, Settings, {}, Forward.Pairs, Forward.Grants,
      Forward.Results, Forward.Summary, Forward.Trace, {8,16}, ComponentFixture));
  TestEqual(TEXT("8517 fixture closure retains all eight agents"),
    ComponentFixture.Agents.Num(), 8);
  Algo::Reverse(Agents);
  const FSolveFixture Reversed = Solve(Agents, Environment);
  TestEqual(TEXT("8517 reversed solve hash stable"),
    Reversed.Summary.CandidateHash, Forward.Summary.CandidateHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoLocalPredictiveT5SixAgentJointRecoveryFixtureTest,
  "CrowdDemo.SF.LocalPredictive.T5SixAgentJointRecoveryFixture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoLocalPredictiveT5SixAgentJointRecoveryFixtureTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdDemoLocalPredictiveAgent> Agents = {
    MakeAgent(5, FVector2f(224,1896), FVector2f(111,279), 285),
    MakeAgent(8, FVector2f(206,2131), FVector2f(0,0), 0),
    MakeAgent(12, FVector2f(349,1912), FVector2f(0,0), 0),
    MakeAgent(14, FVector2f(256,2016), FVector2f(0,0), 0),
    MakeAgent(15, FVector2f(394,2066), FVector2f(0,0), 0),
    MakeAgent(19, FVector2f(325,2169), FVector2f(-124,-273), 273)};
  const FCrowdDemoSharedFlowFieldConfig Environment =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  const FSolveFixture Forward = Solve(Agents, Environment);
  TestTrue(TEXT("8518 six-agent joint solve remains safe"), Forward.Summary.bValid);
  TestEqual(TEXT("8518 reproduces six final conflict pairs"),
    Forward.Summary.ConflictPairCount, 6);
  TestEqual(TEXT("8518 applies one joint preferred recovery"),
    Forward.Summary.JointPreferredRecoveryComponentCount, 1);
  TestEqual(TEXT("8518 recovers complete six-agent component"),
    Forward.Summary.JointPreferredRecoveryAgentCount, 6);
  const auto* Pre5 = Forward.Trace.CompletedIndependentResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 5; });
  const auto* Final5 = FindResult(Forward, 5);
  const auto* Final19 = FindResult(Forward, 19);
  TestNotNull(TEXT("8518 pre-recovery Agent 5"), Pre5);
  TestNotNull(TEXT("8518 final Agent 5"), Final5);
  TestNotNull(TEXT("8518 final Agent 19"), Final19);
  if (Pre5) TestTrue(TEXT("8518 reproduces independent low-speed local optimum"),
    Pre5->Velocity.Equals(FVector2f(5,4), 0.0f));
  if (Final5)
    TestTrue(TEXT("joint recovery restores substantial Agent 5 progress"),
      FVector2f::DotProduct(Final5->Velocity,
        FVector2f(111,279).GetSafeNormal()) >= 200.0f);
  if (Final19)
    TestTrue(TEXT("joint recovery preserves substantial Agent 19 progress"),
      FVector2f::DotProduct(Final19->Velocity,
        FVector2f(-124,-273).GetSafeNormal()) >= 200.0f);
  TArray<FCrowdDemoLocalPredictivePair> CandidatePairs;
  FCrowdDemoLocalPredictiveSettings Settings;
  FCrowdDemoLocalPredictiveInteractionKernel::BuildCandidatePairs(
    Agents, Settings, CandidatePairs);
  TestTrue(TEXT("8518 recovered result validates every candidate pair and environment"),
    FCrowdDemoLocalPredictiveInteractionKernel::ValidateJointResult(
      Agents, Environment, Settings, CandidatePairs, Forward.Results));

  Algo::Reverse(Agents);
  const FSolveFixture Reversed = Solve(Agents, Environment);
  TestEqual(TEXT("8518 reversed solve hash stable"),
    Reversed.Summary.CandidateHash, Forward.Summary.CandidateHash);
  for (int32 Index = 0; Index < Forward.Results.Num(); ++Index)
  {
    TestEqual(TEXT("8518 reversed result id stable"),
      Reversed.Results[Index].AgentId, Forward.Results[Index].AgentId);
    TestTrue(TEXT("8518 reversed result velocity stable"),
      Reversed.Results[Index].Velocity.Equals(Forward.Results[Index].Velocity, 0.0f));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoLocalPredictiveRollbackStateTest,
  "CrowdDemo.SF.LocalPredictive.RollbackRestoresPreparedState",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoLocalPredictiveRollbackStateTest::RunTest(const FString& Parameters)
{
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  FCrowdDemoSoftPressureRollbackSnapshot Snapshot;
  FCrowdDemoLocalPredictiveResult Result;
  Result.AgentId = 7;
  Result.Velocity = FVector2f(123.0f, -45.0f);
  Result.NextBlockedAgeSteps = 9;
  Result.ComponentKey = 77u;
  Result.GrantEpoch = 4;
  Result.bValid = true;
  Snapshot.LocalPredictiveResults = {Result};
  FCrowdDemoLocalPredictiveGrantState Grant;
  Grant.ComponentKey = 77u;
  Grant.GrantedAgentId = 7;
  Grant.GrantEpoch = 4;
  Grant.RemainingSteps = 17;
  Snapshot.LocalPredictiveGrantStates = {Grant};
  Snapshot.LocalPredictiveSummary.bValid = true;
  Snapshot.LocalPredictiveSummary.CandidateHash = 123456u;
  Snapshot.LocalPredictiveSummary.ProcessedAgentCount = 1;
  Snapshot.LocalPredictiveRoundHash = 654321u;
  Snapshot.LocalPredictiveSampleCount = 18;
  Snapshot.LocalPredictiveInvalidStepCount = 2;

  Pipeline->RestoreSoftPressureRuntime(Snapshot);
  TestEqual(TEXT("rollback result count"),
    Pipeline->GetPreparedLocalPredictiveResults().Num(), 1);
  TestEqual(TEXT("rollback result agent"),
    Pipeline->GetPreparedLocalPredictiveResults()[0].AgentId, 7);
  TestTrue(TEXT("rollback result velocity"),
    Pipeline->GetPreparedLocalPredictiveResults()[0].Velocity.Equals(
      FVector2f(123.0f, -45.0f), 0.0f));
  TestEqual(TEXT("rollback grant owner"),
    Pipeline->GetLocalPredictiveGrantStates()[0].GrantedAgentId, 7);
  TestEqual(TEXT("rollback summary hash"),
    Pipeline->GetLastLocalPredictiveSummary().CandidateHash, 123456u);
  TestEqual(TEXT("rollback round hash"), Pipeline->GetLocalPredictiveRoundHash(), 654321u);
  TestEqual(TEXT("rollback samples"), Pipeline->GetLocalPredictiveSampleCount(), 18);
  TestEqual(TEXT("rollback invalid steps"),
    Pipeline->GetLocalPredictiveInvalidStepCount(), 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoLocalPredictiveT3InitialFixtureTest,
  "CrowdDemo.SF.LocalPredictive.T3InitialFixture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoLocalPredictiveT3InitialFixtureTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapLayoutInput> LayoutInputs;
  for (int32 Index = 0; Index < FCrowdDemoBidirectionalSwapKernel::AgentCount; ++Index)
    LayoutInputs.Add({Index + 1, Index});
  const FCrowdDemoBidirectionalSwapLayout Layout =
    FCrowdDemoBidirectionalSwapKernel::BuildLayout(LayoutInputs);
  FCrowdDemoSharedFlowField Fields[2];
  for (int32 CohortId = 0; CohortId < 2; ++CohortId)
    FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortId), Fields[CohortId]);
  TArray<FCrowdDemoLocalPredictiveAgent> Agents;
  for (const FCrowdDemoBidirectionalSwapLayoutAgent& LayoutAgent : Layout.Agents)
  {
    const FCrowdDemoSharedFlowSample Sample = FCrowdDemoSharedFlowFieldKernel::Sample(
      Fields[LayoutAgent.CohortId], LayoutAgent.SpawnLocation);
    FCrowdDemoLocalPredictiveAgent Agent = MakeAgent(
      LayoutAgent.AgentId,
      FVector2f(LayoutAgent.SpawnLocation.X, LayoutAgent.SpawnLocation.Y),
      FVector2f(Sample.FlowDirection.X, Sample.FlowDirection.Y) * 800.0f);
    Agent.MaxSpeedCmps = 800.0f;
    Agents.Add(Agent);
  }
  const FSolveFixture Fixture = Solve(
    Agents, FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(0));
  AddInfo(FString::Printf(
    TEXT("T3 initial local summary valid=%d candidates=%d conflicts=%d infeasible=%d quantization=%d joint=%d hash=%u"),
    Fixture.Summary.bValid ? 1 : 0, Fixture.Summary.CandidatePairCount,
    Fixture.Summary.ConflictPairCount, Fixture.Summary.InfeasibleAgentCount,
    Fixture.Summary.QuantizationFailureCount,
    Fixture.Summary.JointValidationFailureCount, Fixture.Summary.CandidateHash));
  for (const FCrowdDemoLocalPredictivePair& Pair : Fixture.Pairs)
  {
    const FCrowdDemoLocalPredictiveAgent& A = Agents[Pair.MinAgentIndex];
    const FCrowdDemoLocalPredictiveAgent& B = Agents[Pair.MaxAgentIndex];
    const FCrowdDemoLocalPredictiveResult* RA = FindResult(Fixture, Pair.MinAgentId);
    const FCrowdDemoLocalPredictiveResult* RB = FindResult(Fixture, Pair.MaxAgentId);
    if (!RA || !RB) continue;
    const FVector2f RelativePosition = B.Position - A.Position;
    const FVector2f RelativeVelocity = RB->Velocity - RA->Velocity;
    const float SpeedSquared = RelativeVelocity.SizeSquared();
    const float Time = SpeedSquared > KINDA_SMALL_NUMBER
      ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
          / SpeedSquared, 0.0f, 1.25f)
      : 0.0f;
    const float Separation = (RelativePosition + RelativeVelocity * Time).Size();
    if (Separation + 0.5f < Pair.RequiredSeparationCm)
      AddInfo(FString::Printf(
        TEXT("T3 unsafe pair=%d,%d time=%.4f separation=%.4f required=%.4f velocities=(%.1f,%.1f)/(%.1f,%.1f)"),
        Pair.MinAgentId, Pair.MaxAgentId, Time, Separation,
        Pair.RequiredSeparationCm, RA->Velocity.X, RA->Velocity.Y,
        RB->Velocity.X, RB->Velocity.Y));
  }
  TestTrue(TEXT("T3 initial local solve valid"), Fixture.Summary.bValid);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoLocalPredictiveT3RolloutTest,
  "CrowdDemo.SF.LocalPredictive.T3ProductionRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoLocalPredictiveT3RolloutTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapLayoutInput> LayoutInputs;
  for (int32 Index = 0; Index < FCrowdDemoBidirectionalSwapKernel::AgentCount; ++Index)
    LayoutInputs.Add({Index + 1, Index});
  const FCrowdDemoBidirectionalSwapLayout Layout =
    FCrowdDemoBidirectionalSwapKernel::BuildLayout(LayoutInputs);
  FCrowdDemoSharedFlowField Fields[2];
  for (int32 CohortId = 0; CohortId < 2; ++CohortId)
    FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortId), Fields[CohortId]);
  TArray<FCrowdDemoBidirectionalSwapStepAgent> States;
  for (const FCrowdDemoBidirectionalSwapLayoutAgent& LayoutAgent : Layout.Agents)
  {
    FCrowdDemoBidirectionalSwapStepAgent& State = States.AddDefaulted_GetRef();
    State.AgentId = LayoutAgent.AgentId;
    State.FormationIndex = LayoutAgent.FormationIndex;
    State.Location = LayoutAgent.SpawnLocation;
  }
  FCrowdDemoLocalPredictiveSettings LocalSettings;
  FCrowdDemoParticleConstraintSettings ParticleSettings;
  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.FlowConfig = FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(0);
  Environment.FlowConfig.AgentInflateCm = 52.0f;
  TArray<FCrowdDemoLocalPredictiveGrantState> PreviousGrants;
  TMap<int32, int32> BlockedAges;
  int32 FirstInvalidStep = INDEX_NONE;
  for (int32 Step = 0; Step < 900; ++Step)
  {
    TArray<FCrowdDemoLocalPredictiveAgent> LocalAgents;
    for (const FCrowdDemoBidirectionalSwapStepAgent& State : States)
    {
      const int32 CohortId =
        FCrowdDemoBidirectionalSwapKernel::CohortIdForFormationIndex(
          State.FormationIndex);
      const FCrowdDemoSharedFlowSample Sample =
        FCrowdDemoSharedFlowFieldKernel::Sample(Fields[CohortId], State.Location);
      const FVector Preferred = Sample.bUnreachable
        ? FVector::ZeroVector : Sample.FlowDirection * 800.0f;
      FCrowdDemoLocalPredictiveAgent Agent = MakeAgent(
        State.AgentId, FVector2f(State.Location.X, State.Location.Y),
        FVector2f(Preferred.X, Preferred.Y), BlockedAges.FindRef(State.AgentId));
      Agent.Velocity = FVector2f(State.Velocity.X, State.Velocity.Y);
      Agent.MaxSpeedCmps = 800.0f;
      LocalAgents.Add(Agent);
    }
    TArray<FCrowdDemoLocalPredictivePair> LocalPairs;
    TArray<FCrowdDemoLocalPredictiveGrantState> NextGrants;
    TArray<FCrowdDemoLocalPredictiveResult> LocalResults;
    FCrowdDemoLocalPredictiveSummary LocalSummary;
    FCrowdDemoLocalPredictiveInteractionKernel::Solve(
      LocalAgents, Environment.FlowConfig, LocalSettings, PreviousGrants,
      LocalPairs, NextGrants, LocalResults, LocalSummary);
    if (!LocalSummary.bValid)
    {
      FirstInvalidStep = Step;
      AddInfo(FString::Printf(
        TEXT("T3 rollout local invalid step=%d conflicts=%d infeasible=%d quantization=%d joint=%d joint_resolved=%d hash=%u"),
        Step, LocalSummary.ConflictPairCount, LocalSummary.InfeasibleAgentCount,
        LocalSummary.QuantizationFailureCount,
        LocalSummary.JointValidationFailureCount,
        LocalSummary.JointComponentResolutionCount, LocalSummary.CandidateHash));
      TArray<FCrowdDemoLocalPredictivePair> Candidates;
      FCrowdDemoLocalPredictiveInteractionKernel::BuildCandidatePairs(
        LocalAgents, LocalSettings, Candidates);
      TMap<int32, const FCrowdDemoLocalPredictiveAgent*> AgentById;
      TMap<int32, const FCrowdDemoLocalPredictiveResult*> ResultById;
      for (const auto& Agent : LocalAgents) AgentById.Add(Agent.AgentId, &Agent);
      for (const auto& Result : LocalResults) ResultById.Add(Result.AgentId, &Result);
      const float Horizon = FMath::Max(
        LocalSettings.FixedStepSeconds, LocalSettings.TimeHorizonSeconds);
      for (const auto& Pair : Candidates)
      {
        const auto* const* A = AgentById.Find(Pair.MinAgentId);
        const auto* const* B = AgentById.Find(Pair.MaxAgentId);
        const auto* const* RA = ResultById.Find(Pair.MinAgentId);
        const auto* const* RB = ResultById.Find(Pair.MaxAgentId);
        if (!A || !B || !RA || !RB || !(*RA)->bValid || !(*RB)->bValid) continue;
        const FVector2f RelativePosition = (*B)->Position - (*A)->Position;
        const FVector2f RelativeVelocity = (*RB)->Velocity - (*RA)->Velocity;
        const float SpeedSquared = RelativeVelocity.SizeSquared();
        const float Time = SpeedSquared > KINDA_SMALL_NUMBER
          ? FMath::Clamp(-FVector2f::DotProduct(RelativePosition, RelativeVelocity)
              / SpeedSquared, 0.0f, Horizon)
          : 0.0f;
        const float Separation = (RelativePosition + RelativeVelocity * Time).Size();
        if (Separation + 0.5f < Pair.RequiredSeparationCm)
        {
          AddInfo(FString::Printf(
            TEXT("T3 rollout unsafe candidate pair=%d,%d current=%.3f separation=%.3f time=%.3f required=%.3f positions=(%.1f,%.1f)/(%.1f,%.1f) velocities=(%.1f,%.1f)/(%.1f,%.1f)"),
            Pair.MinAgentId, Pair.MaxAgentId, RelativePosition.Size(), Separation,
            Time, Pair.RequiredSeparationCm,
            (*A)->Position.X, (*A)->Position.Y, (*B)->Position.X, (*B)->Position.Y,
            (*RA)->Velocity.X, (*RA)->Velocity.Y, (*RB)->Velocity.X, (*RB)->Velocity.Y));
          break;
        }
      }
      break;
    }
    PreviousGrants = MoveTemp(NextGrants);
    TMap<int32, const FCrowdDemoLocalPredictiveResult*> LocalById;
    for (const FCrowdDemoLocalPredictiveResult& Result : LocalResults)
    {
      LocalById.Add(Result.AgentId, &Result);
      BlockedAges.Add(Result.AgentId, Result.NextBlockedAgeSteps);
    }
    TArray<FCrowdDemoParticleConstraintAgent> ParticleAgents;
    for (const FCrowdDemoBidirectionalSwapStepAgent& State : States)
    {
      const FCrowdDemoLocalPredictiveResult* const* Local = LocalById.Find(State.AgentId);
      if (!Local) continue;
      FCrowdDemoParticleConstraintAgent& Agent = ParticleAgents.AddDefaulted_GetRef();
      Agent.AgentId = State.AgentId;
      Agent.StartPosition = State.Location;
      Agent.PredictedPosition = State.Location
        + FVector((*Local)->Velocity.X, (*Local)->Velocity.Y, 0.0f)
          * ParticleSettings.FixedStepSeconds;
    }
    TArray<FCrowdDemoParticleConstraintPair> ParticlePairs;
    TArray<FCrowdDemoParticleConstraintResult> ParticleResults;
    FCrowdDemoParticleConstraintSummary ParticleSummary;
    FCrowdDemoParticleConstraintKernel::Solve(
      ParticleAgents, Environment, ParticleSettings, ParticlePairs,
      ParticleResults, ParticleSummary);
    if (!ParticleSummary.bValid)
    {
      FirstInvalidStep = Step;
      AddInfo(FString::Printf(TEXT("T3 rollout particle invalid step=%d"), Step));
      break;
    }
    TMap<int32, const FCrowdDemoParticleConstraintResult*> ParticleById;
    for (const FCrowdDemoParticleConstraintResult& Result : ParticleResults)
      ParticleById.Add(Result.AgentId, &Result);
    for (FCrowdDemoBidirectionalSwapStepAgent& State : States)
      if (const FCrowdDemoParticleConstraintResult* const* Result =
        ParticleById.Find(State.AgentId))
      {
        State.Location = (*Result)->CorrectedPosition;
        State.Velocity = (*Result)->CorrectedVelocity;
      }
  }
  TestEqual(TEXT("T3 rollout has no local/particle invalid step"),
    FirstInvalidStep, INDEX_NONE);
  return true;
}

#endif
