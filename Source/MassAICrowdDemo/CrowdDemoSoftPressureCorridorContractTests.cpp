#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSoftPressureAgent0CorridorFixtureTest,
  "CrowdDemo.SoftPressure.Corridor.Agent0BaselineAndClearance",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSoftPressureAgent0CorridorFixtureTest::RunTest(const FString& Parameters)
{
  constexpr int32 AgentId = 0;
  constexpr float FixedStep = 1.0f / 30.0f;
  const FVector Initial(2449.0f, -956.0f, 60.0f);
  const FVector DesiredVelocity(7.547f, 799.964f, 0.0f);

  FCrowdDemoSharedFlowFieldConfig Flow48Config =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  FCrowdDemoSharedFlowField Flow48;
  TestTrue(TEXT("48cm flow field builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(Flow48Config, Flow48));
  const FCrowdDemoSharedFlowSample Flow48Sample =
    FCrowdDemoSharedFlowFieldKernel::Sample(Flow48, Initial);
  TestEqual(TEXT("Agent 0 fixture cell"), Flow48Sample.StableCellKey, 1246);
  TestEqual(TEXT("48cm raster treats fixture as reachable"), Flow48Sample.Status,
    ECrowdDemoFlowLocationStatus::Reachable);

  FCrowdDemoSharedFlowFieldConfig Flow52Config = Flow48Config;
  Flow52Config.AgentInflateCm = 52.0f;
  FCrowdDemoSharedFlowField Flow52;
  TestTrue(TEXT("52cm diagnostic flow field builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(Flow52Config, Flow52));
  const FCrowdDemoSharedFlowSample Flow52Sample =
    FCrowdDemoSharedFlowFieldKernel::Sample(Flow52, Initial);
  TestEqual(TEXT("52cm raster blocks the fixture cell"), Flow52Sample.Status,
    ECrowdDemoFlowLocationStatus::BlockedRasterCell);
  TestFalse(TEXT("continuous 52cm hard domain keeps the fixture start legal"),
    FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(Flow52Config, Initial));

  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.FlowConfig = Flow48Config;
  Environment.bConstrainToFlowBounds = true;
  FCrowdDemoParticleConstraintSettings Settings;
  Settings.bCaptureRouteDiagnostic = true;
  FVector Position = Initial;
  float MaximumStepProgressCm = 0.0f;
  float MaximumDiscardedTangentCm = 0.0f;
  float MaximumDiscardedNormalCm = 0.0f;
  int32 MaximumPairCount = 0;
  int32 ContactNormalCount = 0;
  uint32 RolloutHash = 2166136261u;
  for (int32 Step = 0; Step < 60; ++Step)
  {
    FCrowdDemoParticleConstraintAgent Agent;
    Agent.AgentId = AgentId;
    Agent.StartPosition = Position;
    Agent.PredictedPosition = Position + DesiredVelocity * FixedStep;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.SoftMarginCm = 17.0f;
    Agent.Mobility = 1.0f;
    TArray<FCrowdDemoParticleConstraintPair> Pairs;
    TArray<FCrowdDemoParticleConstraintResult> Results;
    FCrowdDemoParticleConstraintSummary Summary;
    FCrowdDemoParticleConstraintTrace Trace;
    FCrowdDemoParticleConstraintKernel::Solve(
      {Agent}, Environment, Settings, Pairs, Results, Summary, &Trace);
    TestTrue(TEXT("Agent 0 baseline candidate remains hard safe"), Summary.bValid);
    TestEqual(TEXT("Agent 0 baseline has no pair blocker"), Pairs.Num(), 0);
    if (Results.IsEmpty() || Trace.UnifiedHardPositions.IsEmpty()
      || Trace.QuantizedPositions.IsEmpty())
      return false;
    const FVector Previous = Position;
    Position = Results[0].CorrectedPosition;
    MaximumStepProgressCm = FMath::Max(MaximumStepProgressCm,
      FVector::Dist2D(Previous, Position));
    const FVector Discarded = Trace.UnifiedHardPositions[0] - Trace.QuantizedPositions[0];
    MaximumDiscardedTangentCm = FMath::Max(
      MaximumDiscardedTangentCm, FMath::Abs(Discarded.X));
    MaximumDiscardedNormalCm = FMath::Max(
      MaximumDiscardedNormalCm, FMath::Abs(Discarded.Y));
    MaximumPairCount = FMath::Max(MaximumPairCount, Pairs.Num());
    for (const auto& Contact : Trace.FinalEnvironmentContacts)
      if (Contact.AgentId == AgentId && !Contact.CorrectionNormal.IsNearlyZero())
        ++ContactNormalCount;
    RolloutHash ^= static_cast<uint32>(FMath::RoundToInt(Position.X));
    RolloutHash *= 16777619u;
    RolloutHash ^= static_cast<uint32>(FMath::RoundToInt(Position.Y));
    RolloutHash *= 16777619u;
  }
  AddInfo(FString::Printf(
    TEXT("Agent0 baseline initial=(%.3f,%.3f) final=(%.3f,%.3f) max_step=%.3f discarded_tangent_max=%.3f discarded_normal_max=%.3f pairs=%d contacts=%d hash=%u flow48_status=%d flow52_status=%d"),
    Initial.X, Initial.Y, Position.X, Position.Y, MaximumStepProgressCm,
    MaximumDiscardedTangentCm, MaximumDiscardedNormalCm, MaximumPairCount,
    ContactNormalCount, RolloutHash, static_cast<int32>(Flow48Sample.Status),
    static_cast<int32>(Flow52Sample.Status)));
  TestTrue(TEXT("baseline remains in the quantized corridor dead zone"),
    FVector::Dist2D(Position, Initial) < 1.0f);
  TestTrue(TEXT("baseline discards a legal sub-centimeter tangent"),
    MaximumDiscardedTangentCm > 0.1f && MaximumDiscardedTangentCm < 1.0f);
  TestTrue(TEXT("baseline observes an environment normal"), ContactNormalCount > 0);

  FVector ResidualPosition = Initial;
  FVector ValidResidual = FVector::ZeroVector;
  int32 FirstPositiveXStep = INDEX_NONE;
  for (int32 Step = 0; Step < 60; ++Step)
  {
    FCrowdDemoParticleConstraintAgent Agent;
    Agent.AgentId = AgentId;
    Agent.StartPosition = ResidualPosition;
    Agent.PredictedPosition = ResidualPosition + DesiredVelocity * FixedStep + ValidResidual;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.SoftMarginCm = 17.0f;
    Agent.Mobility = 1.0f;
    TArray<FCrowdDemoParticleConstraintPair> Pairs;
    TArray<FCrowdDemoParticleConstraintResult> Results;
    FCrowdDemoParticleConstraintSummary Summary;
    FCrowdDemoParticleConstraintTrace Trace;
    FCrowdDemoParticleConstraintKernel::Solve(
      {Agent}, Environment, Settings, Pairs, Results, Summary, &Trace);
    TestTrue(TEXT("residual rollout remains hard safe"), Summary.bValid);
    if (Results.IsEmpty() || Trace.UnifiedHardPositions.IsEmpty()) return false;
    const FVector Before = ResidualPosition;
    ResidualPosition = Results[0].CorrectedPosition;
    const FVector RawResidual = Trace.UnifiedHardPositions[0] - ResidualPosition;
    ValidResidual = FVector(RawResidual.X, 0.0f, 0.0f);
    if (FirstPositiveXStep == INDEX_NONE && ResidualPosition.X > Before.X)
      FirstPositiveXStep = Step;
  }
  AddInfo(FString::Printf(
    TEXT("Agent0 residual final=(%.3f,%.3f) residual=(%.3f,%.3f) first_positive_x_step=%d"),
    ResidualPosition.X, ResidualPosition.Y, ValidResidual.X, ValidResidual.Y,
    FirstPositiveXStep));
  TestTrue(TEXT("sub-centimeter tangent accumulates onto the 1cm lattice"),
    FirstPositiveXStep != INDEX_NONE);
  TestTrue(TEXT("residual rollout crosses obstacle 106 hard X boundary"),
    ResidualPosition.X > 2452.0f);
  return true;
}

#endif
