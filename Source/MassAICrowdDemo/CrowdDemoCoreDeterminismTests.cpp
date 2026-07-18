#if WITH_DEV_AUTOMATION_TESTS

#include "CrowdDemoScenarioRegistry.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoRoundCheckpointTransport.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoFacingKernel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoFacingContractTest,
  "CrowdDemo.SF.Facing.AutonomousThenFinalTarget",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoFacingContractTest::RunTest(const FString& Parameters)
{
  FCrowdDemoFacingSettings Settings;
  FCrowdDemoFacingInput Moving;
  Moving.AgentId = 2;
  Moving.CurrentYawDegrees = 0.0f;
  Moving.AutonomousPreferredVelocity = FVector2f(0.0f, 300.0f);
  Moving.Location = FVector2f::ZeroVector;
  Moving.TargetLocation = FVector2f(-100.0f, 0.0f);
  Moving.bHasTarget = true;
  Moving.bFinalPositionSettled = false;
  FCrowdDemoFacingInput Settled = Moving;
  Settled.AgentId = 1;
  Settled.bFinalPositionSettled = true;

  const TArray<FCrowdDemoFacingInput> Inputs = {Moving, Settled};
  FCrowdDemoFacingSummary Summary;
  FCrowdDemoFacingKernel::Resolve(Inputs, Settings, Summary);
  TestTrue(TEXT("facing solve valid"), Summary.bValid);
  TestEqual(TEXT("stable agent order"), Summary.Results[0].AgentId, 1);
  TestEqual(TEXT("turn rate limits one 30Hz step to 12 degrees"),
    Summary.Results[0].ResolvedYawDegrees, 12.0f);
  TestTrue(TEXT("settled entity faces target"), Summary.Results[0].bFacingTarget);
  TestFalse(TEXT("moving entity ignores target and faces autonomous vector"),
    Summary.Results[1].bFacingTarget);
  TestEqual(TEXT("moving autonomous turn is also rate limited"),
    Summary.Results[1].ResolvedYawDegrees, 12.0f);

  TArray<FCrowdDemoFacingInput> Reversed = {Settled, Moving};
  FCrowdDemoFacingSummary ReversedSummary;
  FCrowdDemoFacingKernel::Resolve(Reversed, Settings, ReversedSummary);
  TestEqual(TEXT("input order does not change facing hash"),
    ReversedSummary.StableHash, Summary.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoScenarioRegistryTest,
  "CrowdDemo.SF.Parser.AcceptsOnlyCurrentScenarios",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoScenarioRegistryTest::RunTest(const FString& Parameters)
{
  ECrowdDemoScenario Scenario = ECrowdDemoScenario::SimRoundObstacle;
  TestTrue(TEXT("numeric SF1 accepted"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("0"), Scenario));
  TestEqual(TEXT("numeric SF1 maps to obstacle"),
    Scenario, ECrowdDemoScenario::SimRoundObstacle);
  TestTrue(TEXT("named SF1 accepted"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("SimRoundObstacle"), Scenario));
  TestTrue(TEXT("numeric SoftPressure accepted"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("1"), Scenario));
  TestEqual(TEXT("numeric SoftPressure maps to current scenario"),
    Scenario, ECrowdDemoScenario::SimRoundSoftPressure);
  TestTrue(TEXT("named SoftPressure accepted"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("SimRoundSoftPressure"), Scenario));
  TestFalse(TEXT("legacy SF3 numeric rejected"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("2"), Scenario));
  TestFalse(TEXT("legacy SF4 numeric rejected"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("3"), Scenario));
  TestFalse(TEXT("legacy SF3 name rejected"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("SimRoundCrowdTraffic"), Scenario));
  TestFalse(TEXT("legacy SF4 name rejected"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("SimRoundPursuitPositioning"), Scenario));
  TestFalse(TEXT("legacy SF3 config value rejected"),
    CrowdDemoScenarioRegistry::IsValidValue(2));
  TestFalse(TEXT("legacy SF4 config value rejected"),
    CrowdDemoScenarioRegistry::IsValidValue(3));
  TestFalse(TEXT("older predictive name rejected"),
    CrowdDemoScenarioRegistry::TryParse(TEXT("PredictiveHeadOn"), Scenario));

  const auto Sf1Timing = CrowdDemoScenarioRegistry::ResolveRoundTiming(
    ECrowdDemoScenario::SimRoundObstacle,
    ECrowdDemoSoftPressureTestCase::CorridorRoute);
  TestEqual(TEXT("SF1 keeps its nominal duration"),
    Sf1Timing.NominalDurationSeconds, 20.0f);
  TestEqual(TEXT("SF1 has no completion grace"),
    Sf1Timing.CompletionGraceSeconds, 0.0f);

  const auto T5Timing = CrowdDemoScenarioRegistry::ResolveRoundTiming(
    ECrowdDemoScenario::SimRoundSoftPressure,
    ECrowdDemoSoftPressureTestCase::PursuitAndSettleMoving);
  TestEqual(TEXT("ordinary SoftPressure keeps thirty second total"),
    T5Timing.GetTotalDurationSeconds(), 30.0f);

  const auto T6MovingTiming = CrowdDemoScenarioRegistry::ResolveRoundTiming(
    ECrowdDemoScenario::SimRoundSoftPressure,
    ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving);
  TestEqual(TEXT("T6 moving nominal observation remains thirty seconds"),
    T6MovingTiming.NominalDurationSeconds, 30.0f);
  TestEqual(TEXT("T6 moving declares fifteen second completion grace"),
    T6MovingTiming.CompletionGraceSeconds, 15.0f);
  TestEqual(TEXT("T6 moving total duration is forty five seconds"),
    T6MovingTiming.GetTotalDurationSeconds(), 45.0f);
  const auto T6TransitTiming = CrowdDemoScenarioRegistry::ResolveRoundTiming(
    ECrowdDemoScenario::SimRoundSoftPressure,
    ECrowdDemoSoftPressureTestCase::HeterogeneousTransit);
  TestEqual(TEXT("combined T6 transit keeps thirty second nominal observation"),
    T6TransitTiming.NominalDurationSeconds, 30.0f);
  TestEqual(TEXT("combined T6 transit receives fifteen second settle grace"),
    T6TransitTiming.CompletionGraceSeconds, 15.0f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPipelineBoundaryAndFormationCacheTest,
  "CrowdDemo.SF.Pipeline.PlanApplyBoundaryAndFormationCache",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPipelineBoundaryAndFormationCacheTest::RunTest(const FString& Parameters)
{
  UCrowdDemoRoundSimPipelineSubsystem* DuePipeline =
    NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  FCrowdDemoRoundPlanPacket FuturePlan;
  FuturePlan.bValid = 1;
  FuturePlan.RoundId = 1;
  FuturePlan.Revision = 1;
  FuturePlan.StartServerTimeSeconds = 10.0f;
  DuePipeline->QueueRoundPlan(FuturePlan);
  TestFalse(TEXT("future plan is not due"), DuePipeline->HasDueRoundPlan(9.0f));
  TestTrue(TEXT("future plan becomes due at start"), DuePipeline->HasDueRoundPlan(10.0f));

  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  TestTrue(TEXT("first boundary claim succeeds"), Pipeline->TryClaimPlanApplyBoundary());
  TestFalse(TEXT("second boundary claim fails"), Pipeline->TryClaimPlanApplyBoundary());
  FCrowdDemoRoundPlanPacket Plan;
  Plan.RoundId = 1;
  Plan.Revision = 1;
  Plan.DurationSeconds = 1.0f;
  Plan.Rules.FixedStepSeconds = 1.0f / 30.0f;
  Pipeline->ActivatePlan(Plan, 3, false);
  TestTrue(TEXT("fixed step begins"), Pipeline->TryBeginFixedStep(1.0f));
  Pipeline->FinishFixedStep();
  TestTrue(TEXT("next boundary claim succeeds"), Pipeline->TryClaimPlanApplyBoundary());
  TestFalse(TEXT("next boundary is also single-claim"), Pipeline->TryClaimPlanApplyBoundary());

  const TArray<int32> AgentIds = {30, 10, 20};
  Pipeline->EnsureFormationIndexCache(AgentIds);
  TestEqual(TEXT("initial formation cache rebuild"),
    Pipeline->GetFormationCacheRebuildCount(), 1);
  TestEqual(TEXT("formation index uses stable AgentId order"),
    Pipeline->GetFormationIndexByAgentId().FindRef(10), 0);
  Pipeline->EnsureFormationIndexCache({20, 30, 10});
  TestEqual(TEXT("input reorder does not rebuild cache"),
    Pipeline->GetFormationCacheRebuildCount(), 1);
  Pipeline->EnsureFormationIndexCache({20, 30, 40});
  TestEqual(TEXT("membership change rebuilds cache"),
    Pipeline->GetFormationCacheRebuildCount(), 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCrossRoundErrorTrendTest,
  "CrowdDemo.SF.Correction.CrossRoundErrorTrend",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCrossRoundErrorTrendTest::RunTest(const FString& Parameters)
{
  FCrowdDemoRoundErrorSeries Stable;
  Stable.Record(0.40f);
  Stable.Record(0.45f);
  Stable.Record(0.42f);
  TestFalse(TEXT("stable sub-centimeter series does not expand"), Stable.IsExpanding(0.10f));
  FCrowdDemoRoundErrorSeries Expanding;
  Expanding.Record(0.20f);
  Expanding.Record(0.55f);
  Expanding.Record(1.10f);
  TestTrue(TEXT("expanding series detected"), Expanding.IsExpanding(0.10f));
  TestEqual(TEXT("expansion measured from first checkpoint"),
    Expanding.GetExpansionFromFirst(), 0.90f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRoundCheckpointTransportTest,
  "CrowdDemo.SF.Transport.RoundCheckpointChunks",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRoundCheckpointTransportTest::RunTest(const FString& Parameters)
{
  FCrowdDemoCorrectionFrame Frame;
  Frame.bValid = 1;
  Frame.FrameKind = ECrowdDemoRoundFrameKind::RoundResultCheckpoint;
  Frame.CorrectionRevision = 77;
  Frame.RoundId = 3;
  Frame.RoundRevision = 3;
  Frame.SourceCheckpointRevision = 3;
  for (int32 Index = 0; Index < 500; ++Index)
  {
    FCrowdDemoRoundAgentState& State = Frame.AgentStates.AddDefaulted_GetRef();
    State.AgentId = 1000 + Index;
    State.Location = FVector(Index * 2.0f, -Index * 3.0f, 60.0f);
    State.TargetApproach.bValid = 1;
    State.TargetApproach.State = static_cast<uint8>(Index % 4);
    State.TargetApproach.TargetRevision = 23;
    State.TargetApproach.AssignedSlotId = Index % 3 == 0 ? 200 + Index : INDEX_NONE;
  }
  Frame.AgentCount = Frame.AgentStates.Num();

  FCrowdDemoCorrectionFrameHeader Header;
  TArray<FCrowdDemoCorrectionFrameChunk> Chunks;
  FCrowdDemoRoundCheckpointTransport::BuildChunks(Frame, 100, Header, Chunks);
  TestEqual(TEXT("500 states produce five chunks"), Chunks.Num(), 5);
  TestEqual(TEXT("header preserves checkpoint kind"),
    Header.FrameKind, ECrowdDemoRoundFrameKind::RoundResultCheckpoint);
  Algo::Reverse(Chunks);
  const FCrowdDemoCorrectionFrameChunk DuplicateChunk = Chunks[0];
  Chunks.Add(DuplicateChunk);
  TArray<FCrowdDemoRoundAgentState> Assembled;
  TestTrue(TEXT("reordered chunks with duplicate assemble"),
    FCrowdDemoRoundCheckpointTransport::TryAssemble(Header, Chunks, Assembled));
  TestEqual(TEXT("assembled agent count"), Assembled.Num(), 500);
  for (int32 Index = 0; Index < Assembled.Num(); ++Index)
  {
    TestEqual(TEXT("stable assembled id"), Assembled[Index].AgentId, 1000 + Index);
    TestEqual(TEXT("target approach state survives assembly"),
      Assembled[Index].TargetApproach.State, static_cast<uint8>(Index % 4));
  }
  TArray<FCrowdDemoCorrectionFrameChunk> Missing = Chunks;
  Missing.RemoveAll([](const FCrowdDemoCorrectionFrameChunk& Chunk)
    { return Chunk.ChunkIndex == 2; });
  TestFalse(TEXT("missing chunk rejected"),
    FCrowdDemoRoundCheckpointTransport::TryAssemble(Header, Missing, Assembled));
  TArray<FCrowdDemoCorrectionFrameChunk> Mismatched = Chunks;
  Mismatched[0].RoundRevision = 4;
  TestFalse(TEXT("revision mismatch rejected"),
    FCrowdDemoRoundCheckpointTransport::TryAssemble(Header, Mismatched, Assembled));
  return true;
}

#endif
