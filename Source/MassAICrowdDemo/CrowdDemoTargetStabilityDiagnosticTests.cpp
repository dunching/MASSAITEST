#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoTargetStabilityDiagnosticKernel.h"
#include "Algo/Reverse.h"

namespace
{
FCrowdDemoTargetStabilityAgentSample MakeAgent(
  const int32 AgentId, const int32 NextCellKey,
  const ECrowdDemoTargetRegionGuidanceMode Mode)
{
  FCrowdDemoTargetStabilityAgentSample Agent;
  Agent.AgentId = AgentId;
  Agent.CurrentCellKey = AgentId;
  Agent.NextCellKey = NextCellKey;
  Agent.CurrentRegionKey = AgentId;
  Agent.GuidanceMode = Mode;
  Agent.DesiredVelocity = FVector2f(100.0f, 0.0f);
  Agent.AppliedVelocity = FVector2f(100.0f, 0.0f);
  Agent.Velocity = Agent.AppliedVelocity;
  return Agent;
}

FCrowdDemoTargetStabilityStepSample MakeStep(const int32 StepIndex)
{
  FCrowdDemoTargetStabilityStepSample Step;
  Step.FixedStepIndex = StepIndex;
  Step.TargetRevision = 7;
  Step.FeasibleGraphHash = 1234;
  Step.InsideBandCount = 2;
  Step.CoverageCount = 2;
  Step.RequiredCoverageCount = 2;
  Step.ParticleSoftErrorCmP95 = 5.0f;
  Step.ParticleMaxActualCorrectionCm = 0.5f;
  Step.Agents = {
    MakeAgent(1, 20, ECrowdDemoTargetRegionGuidanceMode::Transport),
    MakeAgent(2, 20, ECrowdDemoTargetRegionGuidanceMode::Transport)};
  return Step;
}

FCrowdDemoTargetStabilityRegionSample MakeRegion(
  const int32 RegionKey, const int32 Current, const int32 Desired)
{
  FCrowdDemoTargetStabilityRegionSample Region;
  Region.RegionKey = RegionKey;
  Region.bFeasible = true;
  Region.AvailableCapacity = 2;
  Region.CurrentPopulation = Current;
  Region.DesiredPopulation = Desired;
  Region.Deficit = FMath::Max(0, Desired - Current);
  Region.Surplus = FMath::Max(0, Current - Desired);
  return Region;
}

FCrowdDemoTargetStabilityRuntime MakeRuntime()
{
  FCrowdDemoTargetStabilityRuntime Runtime;
  Runtime.Settings.ExpectedAgentCount = 2;
  Runtime.Settings.StableWindowSteps = 90;
  Runtime.Settings.PositionWindowSteps = 3;
  Runtime.Settings.MergeBlockedSteps = 3;
  Runtime.Settings.TerminalChatterWindowSteps = 3;
  Runtime.Settings.ParticleSettlingSteps = 3;
  return Runtime;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetStabilityDiagnosticTest,
  "CrowdDemo.SF.TargetStabilityDiagnostic",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetStabilityDiagnosticTest::RunTest(const FString& Parameters)
{
  {
    auto Runtime = MakeRuntime();
    for (int32 StepIndex = 1; StepIndex <= 3; ++StepIndex)
      FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(MakeStep(StepIndex), Runtime);
    FCrowdDemoTargetStabilitySummary Summary;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Summary);
    TestTrue(TEXT("contended progress remains valid"), Summary.bValid);
    TestEqual(TEXT("contention is observed"), Summary.ContendedStepCount, 3);
    TestEqual(TEXT("progress is not merge blocking"), Summary.MergeBlockedAgentCount, 0);
    TestEqual(TEXT("settled cause"), static_cast<int32>(Summary.PrimaryCause),
      static_cast<int32>(ECrowdDemoTargetStabilityPrimaryCause::Stable));

    auto ReverseRuntime = MakeRuntime();
    for (int32 StepIndex = 1; StepIndex <= 3; ++StepIndex)
    {
      auto Step = MakeStep(StepIndex);
      Algo::Reverse(Step.Agents);
      FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Step, ReverseRuntime);
    }
    FCrowdDemoTargetStabilitySummary ReverseSummary;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(
      ReverseRuntime, ReverseSummary);
    TestEqual(TEXT("input order stable hash"), ReverseSummary.StableHash, Summary.StableHash);
  }
  {
    auto Runtime = MakeRuntime();
    for (int32 StepIndex = 1; StepIndex <= 3; ++StepIndex)
    {
      auto Step = MakeStep(StepIndex);
      for (auto& Agent : Step.Agents)
      {
        Agent.AppliedVelocity = FVector2f::ZeroVector;
        Agent.TotalParticleCorrection = FVector2f(-4.0f, 0.0f);
      }
      FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Step, Runtime);
    }
    FCrowdDemoTargetStabilitySummary Summary;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Summary);
    TestEqual(TEXT("blocked agents"), Summary.MergeBlockedAgentCount, 2);
    TestEqual(TEXT("merge cause"), static_cast<int32>(Summary.PrimaryCause),
      static_cast<int32>(ECrowdDemoTargetStabilityPrimaryCause::MergeCapacity));
  }
  {
    auto Runtime = MakeRuntime();
    auto Terminal = MakeStep(1);
    auto Supply = MakeStep(2);
    auto TerminalAgain = MakeStep(3);
    for (auto& Agent : Terminal.Agents)
      Agent.GuidanceMode = ECrowdDemoTargetRegionGuidanceMode::TerminalSettle;
    for (auto& Agent : TerminalAgain.Agents)
      Agent.GuidanceMode = ECrowdDemoTargetRegionGuidanceMode::TerminalSettle;
    for (auto& Agent : Supply.Agents)
    {
      Agent.NextCellKey = 10 + Agent.AgentId;
      Agent.RegionSurplusCount = 0;
      Agent.bSupply = false;
    }
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Terminal, Runtime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Supply, Runtime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(TerminalAgain, Runtime);
    FCrowdDemoTargetStabilitySummary Summary;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Summary);
    TestEqual(TEXT("terminal chatter agents"), Summary.TerminalChatterAgentCount, 2);
    TestEqual(TEXT("terminal chatter cause"), static_cast<int32>(Summary.PrimaryCause),
      static_cast<int32>(ECrowdDemoTargetStabilityPrimaryCause::TerminalChatter));

    auto ExplicitRuntime = MakeRuntime();
    for (auto& Agent : Supply.Agents)
    {
      Agent.bSupply = true;
      Agent.RegionSurplusCount = 1;
    }
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Terminal, ExplicitRuntime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Supply, ExplicitRuntime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(TerminalAgain, ExplicitRuntime);
    FCrowdDemoTargetStabilitySummary ExplicitSummary;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(ExplicitRuntime, ExplicitSummary);
    TestEqual(TEXT("explicit surplus is not chatter"), ExplicitSummary.TerminalChatterCount, 0);
  }
  {
    auto Runtime = MakeRuntime();
    for (int32 StepIndex = 1; StepIndex <= 3; ++StepIndex)
    {
      auto Step = MakeStep(StepIndex);
      Step.ParticleMaxActualCorrectionCm = 2.0f;
      for (auto& Agent : Step.Agents)
      {
        Agent.Location = FVector2f(static_cast<float>(StepIndex * 5), 0.0f);
        Agent.TargetLocation = FVector2f(static_cast<float>(StepIndex * 5), 0.0f);
        Agent.TargetVelocity = FVector2f(5.0f, 0.0f);
        Agent.AppliedVelocity = FVector2f(5.0f, 0.0f);
      }
      FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Step, Runtime);
    }
    const auto Checkpoint =
      FCrowdDemoTargetStabilityDiagnosticKernel::MakeCheckpoint(Runtime);
    FCrowdDemoTargetStabilitySummary Control;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Control);
    TestEqual(TEXT("particle not settled cause"), static_cast<int32>(Control.PrimaryCause),
      static_cast<int32>(ECrowdDemoTargetStabilityPrimaryCause::ParticleNotSettled));
    TestTrue(TEXT("target relative motion removes target translation"),
      Control.PositionPeakToPeakCmMax < 0.01f);
    Runtime = {};
    FCrowdDemoTargetStabilityDiagnosticKernel::RestoreCheckpoint(Checkpoint, Runtime);
    FCrowdDemoTargetStabilitySummary Restored;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Restored);
    TestEqual(TEXT("checkpoint summary hash"), Restored.StableHash, Control.StableHash);
  }
  {
    auto Runtime = MakeRuntime();
    for (int32 StepIndex = 1; StepIndex <= 3; ++StepIndex)
    {
      auto Step = MakeStep(StepIndex);
      Step.FixedStepSeconds = 1.0f / 30.0f;
      Step.Agents[0].bSupply = true;
      Step.Agents[0].LocalVelocity = FVector2f(14.0f, 0.0f);
      Step.Agents[0].PredictedVelocity = Step.Agents[0].LocalVelocity;
      Step.Agents[0].AppliedVelocity = FVector2f::ZeroVector;
      Step.Agents[1].bSupply = true;
      Step.Agents[1].LocalVelocity = FVector2f(15.0f, 0.0f);
      Step.Agents[1].PredictedVelocity = Step.Agents[1].LocalVelocity;
      Step.Agents[1].AppliedVelocity = Step.Agents[1].LocalVelocity;
      FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Step, Runtime);
    }
    FCrowdDemoTargetStabilitySummary Summary;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Summary);
    TestEqual(TEXT("one supply velocity is below the executable lattice threshold"),
      Summary.FinalSubQuantumSupplyAgentCount, 1);
    TestEqual(TEXT("first sub-quantum supply is stable by AgentId"),
      Summary.FirstSubQuantumSupplyAgentId, 1);
    TestTrue(TEXT("one centimetre at 30Hz requires fifteen centimetres per second"),
      FMath::IsNearlyEqual(Summary.MinimumExecutableSpeedCmps, 15.0f, 0.001f));
  }
  {
    auto BuildLoss = [&](const ECrowdDemoTargetRegionCoverageLossStage Stage)
    {
      auto Runtime = MakeRuntime();
      for (int32 StepIndex = 1; StepIndex <= 3; ++StepIndex)
      {
        auto Step = MakeStep(StepIndex);
        auto Occupied = MakeRegion(0, 2, 1);
        Occupied.TerminalAgentIds = {1, 2};
        Occupied.TerminalSettleAgentIds = {1, 2};
        auto Missing = MakeRegion(1, 0,
          Stage == ECrowdDemoTargetRegionCoverageLossStage::Demand ? 0 : 1);
        if (static_cast<int32>(Stage)
          >= static_cast<int32>(ECrowdDemoTargetRegionCoverageLossStage::Guidance))
          Missing.PrimaryIncomingPlanQuota = 1;
        if (Stage == ECrowdDemoTargetRegionCoverageLossStage::TerminalRetention)
        {
          Missing.PrimaryIncomingConsumedQuota = 1;
          Missing.GuidanceTargetCount = 1;
        }
        Step.Regions = {Missing, Occupied};
        FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Step, Runtime);
      }
      FCrowdDemoTargetStabilitySummary Summary;
      FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Summary);
      return Summary;
    };
    const auto Demand = BuildLoss(ECrowdDemoTargetRegionCoverageLossStage::Demand);
    const auto Plan = BuildLoss(ECrowdDemoTargetRegionCoverageLossStage::PlanQuota);
    const auto Guidance = BuildLoss(ECrowdDemoTargetRegionCoverageLossStage::Guidance);
    const auto Retention = BuildLoss(
      ECrowdDemoTargetRegionCoverageLossStage::TerminalRetention);
    TestEqual(TEXT("demand loss stage"), static_cast<int32>(Demand.FirstMissingRegionStage),
      static_cast<int32>(ECrowdDemoTargetRegionCoverageLossStage::Demand));
    TestEqual(TEXT("plan loss stage"), static_cast<int32>(Plan.FirstMissingRegionStage),
      static_cast<int32>(ECrowdDemoTargetRegionCoverageLossStage::PlanQuota));
    TestEqual(TEXT("guidance loss stage"),
      static_cast<int32>(Guidance.FirstMissingRegionStage),
      static_cast<int32>(ECrowdDemoTargetRegionCoverageLossStage::Guidance));
    TestEqual(TEXT("retention loss stage"),
      static_cast<int32>(Retention.FirstMissingRegionStage),
      static_cast<int32>(ECrowdDemoTargetRegionCoverageLossStage::TerminalRetention));
    TestEqual(TEXT("missing region count"), Retention.FinalMissingRegionCount, 1);
    TestEqual(TEXT("missing region key"), Retention.FirstMissingRegionKey, 1);
  }
  {
    auto Runtime = MakeRuntime();
    auto First = MakeStep(1);
    auto Region0 = MakeRegion(0, 1, 1);
    Region0.TerminalAgentIds = {1};
    auto Region1 = MakeRegion(1, 1, 1);
    Region1.TerminalAgentIds = {2};
    First.Regions = {Region0, Region1};
    FCrowdDemoTargetStabilityEdgeSample EdgeA;
    EdgeA.FromCellKey = 10;
    EdgeA.ToCellKey = 11;
    EdgeA.FromRegionKey = 0;
    EdgeA.ToRegionKey = 1;
    EdgeA.AgentQuota = 1;
    EdgeA.ConsumedQuota = 1;
    FCrowdDemoTargetStabilityEdgeSample EdgeB = EdgeA;
    EdgeB.FromCellKey = 11;
    EdgeB.ToCellKey = 12;
    EdgeB.bToTerminal = true;
    First.Edges = {EdgeB, EdgeA};
    auto Second = MakeStep(2);
    Region0.TerminalAgentIds = {};
    Region0.CurrentPopulation = 0;
    Region1.TerminalAgentIds = {2, 1};
    Region1.CurrentPopulation = 2;
    Second.Regions = {Region1, Region0};
    Second.Edges = {EdgeA, EdgeB};
    auto Third = Second;
    Third.FixedStepIndex = 3;
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(First, Runtime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Second, Runtime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Third, Runtime);
    FCrowdDemoTargetStabilitySummary Summary;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(Runtime, Summary);
    TestEqual(TEXT("region transition enter"), Summary.RegionTerminalEnterCount, 1);
    TestEqual(TEXT("region transition exit"), Summary.RegionTerminalExitCount, 1);

    auto ReversedRuntime = MakeRuntime();
    Algo::Reverse(First.Regions);
    Algo::Reverse(Second.Regions);
    Algo::Reverse(Third.Regions);
    Algo::Reverse(First.Edges);
    Algo::Reverse(Second.Edges);
    Algo::Reverse(Third.Edges);
    for (auto& Region : Second.Regions) Algo::Reverse(Region.TerminalAgentIds);
    for (auto& Region : Third.Regions) Algo::Reverse(Region.TerminalAgentIds);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(First, ReversedRuntime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Second, ReversedRuntime);
    FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Third, ReversedRuntime);
    FCrowdDemoTargetStabilitySummary Reversed;
    FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(ReversedRuntime, Reversed);
    TestEqual(TEXT("region input order stable hash"), Reversed.StableHash, Summary.StableHash);
  }
  return true;
}

#endif
