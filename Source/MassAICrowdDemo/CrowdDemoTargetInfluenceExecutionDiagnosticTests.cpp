#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoTargetInfluenceExecutionDiagnosticKernel.h"

namespace
{
  FCrowdDemoSharedFlowFieldConfig MakeEnvironment()
  {
    FCrowdDemoSharedFlowFieldConfig Config;
    Config.BoundsMin = FVector(-500.0f, -500.0f, 0.0f);
    Config.BoundsMax = FVector(500.0f, 500.0f, 0.0f);
    FCrowdDemoSharedFlowObstacleSpec Obstacle;
    Obstacle.ObstacleId = 7;
    Obstacle.Center = FVector(231.0f, 96.0f, 0.0f);
    Obstacle.Extent = FVector(25.0f, 25.0f, 100.0f);
    Config.ObstacleSpecs.Add(Obstacle);
    return Config;
  }

  FCrowdDemoTargetInfluenceExecutionSample MakeSample(
    const int32 AgentId, const int32 Step, const FVector2f& Location,
    const FVector2f& Request, const FVector2f& Predict, const FVector2f& Applied)
  {
    FCrowdDemoTargetInfluenceExecutionSample Sample;
    Sample.AgentId = AgentId;
    Sample.TargetRevision = 3;
    Sample.FixedStepIndex = Step;
    Sample.Location = Location;
    Sample.TargetLocation = FVector2f::ZeroVector;
    Sample.DensityRequestedVelocity = Request;
    Sample.InfluenceDesiredVelocity = Request;
    Sample.MovementPredictVelocity = Predict;
    Sample.AppliedVelocity = Applied;
    Sample.FixedStepSeconds = 1.0f / 30.0f;
    Sample.RadialBandIndex = FMath::FloorToInt(Location.Size() / 100.0f);
    float Angle = FMath::Atan2(Location.Y, Location.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    Sample.AngularSectorIndex = FMath::Clamp(
      FMath::FloorToInt(Angle / (2.0f * PI) * 8.0f), 0, 7);
    Sample.DensityDirectionSign = 1;
    Sample.DensityLeftWeight = 1;
    Sample.DensityCurrentWeight = 4;
    Sample.DensityRightWeight = 2;
    return Sample;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetInfluenceExecutionDiagnosticTest,
  "CrowdDemo.SF.TargetInfluenceExecutionDiagnostic",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetInfluenceExecutionDiagnosticTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTargetPolarEnvironmentSummary Environment;
  const TArray<int32> Occupied{0, 1, 8};
  FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::BuildEnvironmentFeasibility(
    FVector2f::ZeroVector, 8, 6, 100.0f, 52.0f, MakeEnvironment(), Occupied, Environment);
  TestTrue(TEXT("Environment feasibility is valid"), Environment.bValid);
  TestEqual(TEXT("One feasibility count per radial band"),
    Environment.FeasibleSectorCountByRadialBand.Num(), 6);
  TestTrue(TEXT("Bounds exclude outer polar cells"),
    Environment.FlowBoundsInfeasibleCellCount > 0);
  TestTrue(TEXT("Obstacle excludes at least one polar cell"),
    Environment.ObstacleInfeasibleCellCount > 0);
  TestTrue(TEXT("Circular empty feasible run is measured"),
    Environment.LargestEmptyFeasibleSectorRun > 0);

  TArray<FCrowdDemoTargetInfluenceExecutionSample> StepOne;
  StepOne.Add(MakeSample(2, 10, FVector2f(0.0f, 150.0f),
    FVector2f(-80.0f, 0.0f), FVector2f(-60.0f, 0.0f), FVector2f(-20.0f, 0.0f)));
  StepOne.Add(MakeSample(1, 10, FVector2f(150.0f, 0.0f),
    FVector2f(0.0f, 100.0f), FVector2f(0.0f, 80.0f), FVector2f(0.0f, 40.0f)));
  StepOne[1].EnvironmentSoftCorrection = FVector2f(0.0f, -3.0f);
  StepOne[0].PairSoftCorrection = FVector2f(1.0f, 0.0f);

  FCrowdDemoTargetInfluenceExecutionRuntime Runtime;
  FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::RecordStep(StepOne, Environment, Runtime);
  const auto Checkpoint =
    FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::MakeCheckpoint(Runtime);
  TArray<FCrowdDemoTargetInfluenceExecutionSample> StepTwo;
  StepTwo.Add(MakeSample(1, 11, FVector2f(0.0f, 250.0f),
    FVector2f(100.0f, 0.0f), FVector2f(70.0f, 0.0f), FVector2f(30.0f, 0.0f)));
  StepTwo[0].DensityDirectionSign = -1;
  StepTwo[0].EnvironmentSoftCorrection = FVector2f(-4.0f, 0.0f);
  FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::RecordStep(StepTwo, Environment, Runtime);
  FCrowdDemoTargetInfluenceExecutionSummary Summary;
  FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::BuildSummary(Runtime, Summary);
  TestTrue(TEXT("Execution summary is valid"), Summary.bValid);
  TestEqual(TEXT("Requested agents"), Summary.RequestedAgentCount, 2);
  TestTrue(TEXT("Requested motion reaches predict stage"),
    Summary.MovementPredictTangentialCmpsP50 > 0.0f);
  TestTrue(TEXT("Requested motion is attenuated by applied state"),
    Summary.LostTangentialCmpsMax > 0.0f);
  TestEqual(TEXT("Direction flip counted once"), Summary.DirectionFlipCount, 1);
  TestEqual(TEXT("Actual sector transition counted once"),
    Summary.AngularSectorTransitionCount, 1);
  TestEqual(TEXT("Actual radial transition counted once"),
    Summary.RadialBandTransitionCount, 1);
  TestTrue(TEXT("Environment opposition is attributed"),
    Summary.EnvironmentOpposedAgentCount > 0);

  TArray<FCrowdDemoTargetInfluenceExecutionSample> Reversed = StepOne;
  Algo::Reverse(Reversed);
  FCrowdDemoTargetInfluenceExecutionRuntime ReverseRuntime;
  FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::RecordStep(
    Reversed, Environment, ReverseRuntime);
  TestEqual(TEXT("Input reversal preserves diagnostic hash"),
    ReverseRuntime.RollingHash, Checkpoint.RollingHash);

  FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::RestoreCheckpoint(Checkpoint, Runtime);
  TestEqual(TEXT("Rollback restores sample count"), Runtime.ValidSampleCount, 2);
  TestEqual(TEXT("Rollback restores hash"), Runtime.RollingHash, Checkpoint.RollingHash);
  TestEqual(TEXT("Rollback truncates requested samples"),
    Runtime.RequestedTangentialSamples.Num(), Checkpoint.RequestedSampleCount);
  return true;
}

#endif
