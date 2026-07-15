#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoTargetInfluenceKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"

namespace CrowdDemoTargetInfluenceTests
{
  FCrowdDemoTargetInfluenceSettings MakeSettings()
  {
    FCrowdDemoTargetInfluenceSettings Settings;
    Settings.bEnabled = true;
    Settings.InfluenceBlendWidthCm = 300.0f;
    Settings.RadialGainPerSecond = 2.0f;
    Settings.MaxRadialSpeedCmps = 300.0f;
    Settings.FixedStepSeconds = 1.0f / 30.0f;
    Settings.PositionQuantumCm = 1.0f;
    Settings.VelocityQuantumCmps = 1.0f;
    Settings.AngularSectorCount = 16;
    Settings.RadialBandWidthCm = 100.0f;
    Settings.DensitySmoothingPassCount = 1;
    Settings.DensityMinimumDifference = 1;
    Settings.DensitySpeedPerExcessAgentCmps = 20.0f;
    Settings.MaximumDensityTangentialSpeedCmps = 120.0f;
    return Settings;
  }

  FCrowdDemoTargetInfluenceAgent MakeAgent(int32 Id, float Distance)
  {
    FCrowdDemoTargetInfluenceAgent Agent;
    Agent.AgentId = Id;
    Agent.Location = FVector2f(Distance, 0.0f);
    Agent.Velocity = FVector2f::ZeroVector;
    Agent.MaxSpeedCmps = 800.0f;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.FarFlowPreferredVelocity = FVector2f(-600.0f, 0.0f);
    Agent.TargetLocation = FVector2f::ZeroVector;
    Agent.TargetVelocity = FVector2f::ZeroVector;
    Agent.TargetPhysicalRadiusCm = 100.0f;
    Agent.TargetHardSafetyGapCm = 10.0f;
    Agent.MinimumCombatCenterDistanceCm = 100.0f;
    Agent.MaximumCombatCenterDistanceCm = 850.0f;
    return Agent;
  }

  FCrowdDemoTargetInfluenceResult SolveOne(
    const FCrowdDemoTargetInfluenceAgent& Agent,
    const FCrowdDemoTargetInfluenceSettings& Settings,
    FCrowdDemoTargetInfluenceSummary* SummaryOut = nullptr)
  {
    TArray<FCrowdDemoTargetInfluenceResult> Results;
    FCrowdDemoTargetInfluenceSummary Summary;
    FCrowdDemoTargetInfluenceKernel::Solve({Agent}, Settings, Results, Summary);
    if (SummaryOut) *SummaryOut = Summary;
    return Results.IsEmpty() ? FCrowdDemoTargetInfluenceResult() : Results[0];
  }

  FCrowdDemoTargetInfluenceAgent MakePolarAgent(int32 Id, int32 Sector, float Distance = 550.0f)
  {
    auto Agent = MakeAgent(Id, Distance);
    const float Angle = (static_cast<float>(Sector) + 0.25f) * (2.0f * PI / 16.0f);
    Agent.Location = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
    Agent.FarFlowPreferredVelocity = FVector2f::ZeroVector;
    return Agent;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetPolarDensityTest,
  "CrowdDemo.SoftPressure.TargetInfluence.PolarDensity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetPolarDensityTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetInfluenceTests;
  const auto Settings = MakeSettings();
  TArray<FCrowdDemoTargetInfluenceAgent> Uniform;
  for (int32 Sector = 0; Sector < 16; ++Sector)
    Uniform.Add(MakePolarAgent(Sector + 1, Sector));
  TArray<FCrowdDemoTargetInfluenceResult> Results;
  FCrowdDemoTargetInfluenceSummary Summary;
  FCrowdDemoTargetInfluenceKernel::Solve(Uniform, Settings, Results, Summary);
  TestTrue(TEXT("Uniform field is valid"), Summary.bValid);
  TestEqual(TEXT("Uniform field occupies every sector"),
    Summary.Density.OccupiedAngularSectorCount, 16);
  TestEqual(TEXT("Uniform field has no guided agents"),
    Summary.Density.DensityGuidedAgentCount, 0);

  TArray<FCrowdDemoTargetInfluenceAgent> Congested;
  for (int32 Index = 0; Index < 8; ++Index)
    Congested.Add(MakePolarAgent(Index + 1, 0));
  FCrowdDemoTargetInfluenceKernel::Solve(Congested, Settings, Results, Summary);
  TestTrue(TEXT("Congested sector guides agents"), Summary.Density.DensityGuidedAgentCount > 0);
  TestTrue(TEXT("Stable tie splits clockwise and counter-clockwise"),
    Summary.Density.ClockwiseAgentCount > 0 && Summary.Density.CounterClockwiseAgentCount > 0);

  TArray<FCrowdDemoTargetInfluenceAgent> LeftOpen = {
    MakePolarAgent(1, 0), MakePolarAgent(2, 0), MakePolarAgent(3, 1) };
  FCrowdDemoTargetInfluenceKernel::Solve(LeftOpen, Settings, Results, Summary);
  TestEqual(TEXT("Lower-density left selects counter-clockwise"),
    Results[0].DensityDirectionSign, 1);
  TArray<FCrowdDemoTargetInfluenceAgent> RightOpen = {
    MakePolarAgent(1, 0), MakePolarAgent(2, 0), MakePolarAgent(3, 15) };
  FCrowdDemoTargetInfluenceKernel::Solve(RightOpen, Settings, Results, Summary);
  TestEqual(TEXT("Lower-density right selects clockwise"),
    Results[0].DensityDirectionSign, -1);

  FCrowdDemoTargetDensityField WrapField;
  FCrowdDemoTargetDensitySummary WrapSummary;
  TArray<FCrowdDemoTargetInfluenceAgent> WrapAgents = { MakePolarAgent(1, 15) };
  FCrowdDemoTargetInfluenceKernel::BuildPolarDensityField(
    WrapAgents, Settings, WrapField, WrapSummary);
  const int32 Band = 5;
  TestEqual(TEXT("Sector 15 smooths across the ring into sector 0"),
    WrapField.Cells[Band * 16].SmoothedWeight, 1);

  TArray<FCrowdDemoTargetInfluenceAgent> BandA = {
    MakePolarAgent(1, 0, 550.0f), MakePolarAgent(2, 0, 550.0f) };
  FCrowdDemoTargetInfluenceKernel::Solve(BandA, Settings, Results, Summary);
  const FVector2f BandABaseline = Results[0].DensityVelocity;
  BandA.Add(MakePolarAgent(3, 1, 650.0f));
  FCrowdDemoTargetInfluenceKernel::Solve(BandA, Settings, Results, Summary);
  TestTrue(TEXT("Different radial bands do not contaminate density guidance"),
    Results[0].DensityVelocity.Equals(BandABaseline, 0.001f));

  auto Outside = MakePolarAgent(21, 0, 900.0f);
  TestTrue(TEXT("Outside maximum remains radial-only"),
    SolveOne(Outside, Settings).DensityVelocity.IsNearlyZero());
  auto Inside = MakePolarAgent(22, 0, 140.0f);
  const auto InsideResult = SolveOne(Inside, Settings);
  TestTrue(TEXT("Inside minimum remains outward radial-only"),
    InsideResult.bInsideMinimum && InsideResult.DensityVelocity.IsNearlyZero()
      && FVector2f::DotProduct(InsideResult.RadialCorrection, Inside.Location) > 0.0f);

  auto TranslatedA = MakePolarAgent(31, 3, 500.0f);
  const auto RelativeA = SolveOne(TranslatedA, Settings);
  auto TranslatedB = TranslatedA;
  TranslatedB.TargetLocation += FVector2f(300.0f, -200.0f);
  TranslatedB.Location += FVector2f(300.0f, -200.0f);
  const auto RelativeB = SolveOne(TranslatedB, Settings);
  TestTrue(TEXT("Static target translation preserves target-relative output"),
    RelativeA.DesiredVelocity.Equals(RelativeB.DesiredVelocity, 0.001f));

  TArray<FCrowdDemoTargetInfluenceAgent> Forward = Congested;
  TArray<FCrowdDemoTargetInfluenceResult> ForwardResults;
  FCrowdDemoTargetInfluenceSummary ForwardSummary;
  FCrowdDemoTargetInfluenceKernel::Solve(Forward, Settings, ForwardResults, ForwardSummary);
  Algo::Reverse(Forward);
  TArray<FCrowdDemoTargetInfluenceResult> ReverseResults;
  FCrowdDemoTargetInfluenceSummary ReverseSummary;
  FCrowdDemoTargetInfluenceKernel::Solve(Forward, Settings, ReverseResults, ReverseSummary);
  TestEqual(TEXT("Reversed input preserves field hash"),
    ForwardSummary.Density.FieldHash, ReverseSummary.Density.FieldHash);
  TestEqual(TEXT("Reversed input preserves solve hash"),
    ForwardSummary.StableHash, ReverseSummary.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetInfluenceAnalyticTest,
  "CrowdDemo.SoftPressure.TargetInfluence.AnalyticBand",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetInfluenceAnalyticTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetInfluenceTests;
  const auto Settings = MakeSettings();

  auto Agent = MakeAgent(1, 1200.0f);
  auto Result = SolveOne(Agent, Settings);
  TestTrue(TEXT("Far zone is valid"), Result.bValid);
  TestEqual(TEXT("Far zone has zero influence"), Result.InfluenceWeightQ15, 0);
  TestTrue(TEXT("Far zone preserves flow"),
    Result.DesiredVelocity.Equals(Agent.FarFlowPreferredVelocity, 0.001f));

  Agent.Location.X = 1000.0f;
  const auto BlendA = SolveOne(Agent, Settings);
  Agent.Location.X = 999.0f;
  const auto BlendB = SolveOne(Agent, Settings);
  TestTrue(TEXT("Blend is continuous across adjacent quantized positions"),
    (BlendA.DesiredVelocity - BlendB.DesiredVelocity).Size() < 10.0f);

  Agent.Location.X = 900.0f;
  Result = SolveOne(Agent, Settings);
  TestTrue(TEXT("Outside maximum corrects inward"),
    Result.bOutsideMaximum && Result.RadialCorrection.X < 0.0f);
  Agent.Location.X = 500.0f;
  Result = SolveOne(Agent, Settings);
  TestTrue(TEXT("Effective band is flat"), Result.bInsideEffectiveBand
    && Result.RadialCorrection.IsNearlyZero(0.001f));
  Agent.Location.X = 140.0f;
  Result = SolveOne(Agent, Settings);
  TestTrue(TEXT("Inside minimum corrects outward"),
    Result.bInsideMinimum && Result.RadialCorrection.X > 0.0f);
  TestEqual(TEXT("Hard distance raises configured minimum"),
    Result.NormalizedMinimumDistanceCm, 152.0f);

  auto LongRange = MakeAgent(2, 900.0f);
  LongRange.MinimumCombatCenterDistanceCm = 850.0f;
  LongRange.MaximumCombatCenterDistanceCm = 1000.0f;
  const auto LongResult = SolveOne(LongRange, Settings);
  TestTrue(TEXT("100cm and 850cm profiles share the same valid kernel"),
    Result.bValid && LongResult.bValid && LongResult.bInsideEffectiveBand);

  Agent = MakeAgent(3, 500.0f);
  Agent.TargetVelocity = FVector2f(80.0f, 0.0f);
  Result = SolveOne(Agent, Settings);
  TestEqual(TEXT("Moving target feed-forward preserves target X velocity"),
    Result.DesiredVelocity.X, Agent.TargetVelocity.X);
  Agent.TargetVelocity = FVector2f(200.0f, 0.0f);
  Agent.MaxSpeedCmps = 100.0f;
  Result = SolveOne(Agent, Settings);
  TestTrue(TEXT("Target faster than agent is clamped and may escape"),
    Result.DesiredVelocity.Size() <= 100.001f);

  Agent = MakeAgent(17, 0.0f);
  Result = SolveOne(Agent, Settings);
  TestTrue(TEXT("Zero distance uses stable non-zero outward direction"),
    Result.bValid && Result.RadialCorrection.Size() > 0.0f);
  auto Repeated = SolveOne(Agent, Settings);
  TestEqual(TEXT("Identical solve hash"), Result.StableHash, Repeated.StableHash);

  auto Invalid = Agent;
  Invalid.MinimumCombatCenterDistanceCm = 900.0f;
  Invalid.MaximumCombatCenterDistanceCm = 800.0f;
  TestFalse(TEXT("Invalid band is rejected"), SolveOne(Invalid, Settings).bValid);

  TArray<FCrowdDemoTargetInfluenceAgent> Agents = {
    MakeAgent(3, 900.0f), MakeAgent(1, 600.0f), MakeAgent(2, 300.0f) };
  TArray<FCrowdDemoTargetInfluenceResult> ForwardResults;
  FCrowdDemoTargetInfluenceSummary ForwardSummary;
  FCrowdDemoTargetInfluenceKernel::Solve(Agents, Settings, ForwardResults, ForwardSummary);
  Algo::Reverse(Agents);
  TArray<FCrowdDemoTargetInfluenceResult> ReverseResults;
  FCrowdDemoTargetInfluenceSummary ReverseSummary;
  FCrowdDemoTargetInfluenceKernel::Solve(Agents, Settings, ReverseResults, ReverseSummary);
  TestEqual(TEXT("Input order does not affect hash"),
    ForwardSummary.StableHash, ReverseSummary.StableHash);
  TestTrue(TEXT("Input order does not affect results"),
    ForwardResults.Num() == ReverseResults.Num()
      && ForwardResults[0].DesiredVelocity.Equals(ReverseResults[0].DesiredVelocity, 0.001f));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetInfluenceParticleIntegrationTest,
  "CrowdDemo.SoftPressure.TargetInfluence.ParticleIntegration",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetInfluenceParticleIntegrationTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetInfluenceTests;
  const auto InfluenceSettings = MakeSettings();
  TArray<FCrowdDemoParticleConstraintAgent> ParticleAgents;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    const int32 Column = Index % 10;
    const int32 Row = Index / 10;
    FCrowdDemoParticleConstraintAgent& Particle = ParticleAgents.AddDefaulted_GetRef();
    Particle.AgentId = Index + 1;
    Particle.StartPosition = FVector((Column - 4.5f) * 128.0f,
      -1800.0f + Row * 128.0f, 60.0f);
    Particle.PredictedPosition = Particle.StartPosition;
    Particle.PhysicalRadiusCm = 42.0f;
    Particle.HardSafetyGapCm = 10.0f;
    Particle.SoftMarginCm = Index == 0 ? 60.0f : 17.0f;
    Particle.Mobility = 1.0f;
  }
  TArray<int32> InitialSectorPopulation;
  InitialSectorPopulation.Init(0, 16);
  for (int32 Index = 0; Index < 20; ++Index)
  {
    const FVector Position = ParticleAgents[Index].StartPosition;
    float Angle = FMath::Atan2(Position.Y, Position.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    const int32 Sector = FMath::Clamp(
      FMath::FloorToInt(Angle / (2.0f * PI) * 16.0f), 0, 15);
    ++InitialSectorPopulation[Sector];
  }
  int32 InitialOccupiedSectors = 0;
  int32 InitialMaximumSectorPopulation = 0;
  for (const int32 Population : InitialSectorPopulation)
  {
    InitialOccupiedSectors += Population > 0 ? 1 : 0;
    InitialMaximumSectorPopulation = FMath::Max(InitialMaximumSectorPopulation, Population);
  }
  FCrowdDemoParticleConstraintAgent& Target = ParticleAgents.AddDefaulted_GetRef();
  Target.AgentId = -1000000001;
  Target.StartPosition = FVector(0.0f, 0.0f, 60.0f);
  Target.PredictedPosition = Target.StartPosition;
  Target.PhysicalRadiusCm = 100.0f;
  Target.HardSafetyGapCm = 10.0f;
  Target.SoftMarginCm = 17.0f;
  Target.Mobility = 0.0f;

  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.bConstrainToFlowBounds = false;
  FCrowdDemoParticleConstraintSettings ParticleSettings;
  ParticleSettings.FixedStepSeconds = 1.0f / 30.0f;
  ParticleSettings.IterationCount = 8;
  ParticleSettings.SafetyIterationCount = 8;
  ParticleSettings.SoftResponsePerSecond = 8.0f;
  ParticleSettings.SoftMaxPairCorrectionPerIterationCm = 8.0f;
  ParticleSettings.HardMaxPairCorrectionPerIterationCm = 24.0f;
  ParticleSettings.PositionQuantumCm = 1.0f;
  ParticleSettings.VelocityQuantumCmps = 1.0f;

  FCrowdDemoParticleConstraintSummary FinalSummary;
  for (int32 Step = 0; Step < 120; ++Step)
  {
    TArray<FCrowdDemoTargetInfluenceAgent> InfluenceAgents;
    for (int32 Index = 0; Index < 20; ++Index)
    {
      auto Agent = MakeAgent(ParticleAgents[Index].AgentId, 0.0f);
      Agent.Location = FVector2f(ParticleAgents[Index].StartPosition.X,
        ParticleAgents[Index].StartPosition.Y);
      Agent.Velocity = FVector2f(
        (ParticleAgents[Index].PredictedPosition.X - ParticleAgents[Index].StartPosition.X)
          / ParticleSettings.FixedStepSeconds,
        (ParticleAgents[Index].PredictedPosition.Y - ParticleAgents[Index].StartPosition.Y)
          / ParticleSettings.FixedStepSeconds);
      Agent.PhysicalRadiusCm = ParticleAgents[Index].PhysicalRadiusCm;
      Agent.FarFlowPreferredVelocity = FVector2f(0.0f, 600.0f);
      InfluenceAgents.Add(Agent);
    }
    TArray<FCrowdDemoTargetInfluenceResult> Guidance;
    FCrowdDemoTargetInfluenceSummary GuidanceSummary;
    FCrowdDemoTargetInfluenceKernel::Solve(
      InfluenceAgents, InfluenceSettings, Guidance, GuidanceSummary);
    for (int32 Index = 0; Index < 20; ++Index)
    {
      ParticleAgents[Index].PredictedPosition = ParticleAgents[Index].StartPosition
        + FVector(Guidance[Index].DesiredVelocity.X, Guidance[Index].DesiredVelocity.Y, 0.0f)
          * ParticleSettings.FixedStepSeconds;
    }
    TArray<FCrowdDemoParticleConstraintPair> Pairs;
    TArray<FCrowdDemoParticleConstraintResult> Results;
    FCrowdDemoParticleConstraintKernel::Solve(ParticleAgents, Environment,
      ParticleSettings, Pairs, Results, FinalSummary);
    TestTrue(TEXT("Each rollout step remains particle-safe"), FinalSummary.bValid);
    if (!FinalSummary.bValid) return false;
    TMap<int32, const FCrowdDemoParticleConstraintResult*> ResultsById;
    for (const auto& Result : Results) ResultsById.Add(Result.AgentId, &Result);
    for (int32 Index = 0; Index < 20; ++Index)
    {
      const auto* const* Found = ResultsById.Find(ParticleAgents[Index].AgentId);
      if (!Found) return false;
      ParticleAgents[Index].StartPosition = (*Found)->CorrectedPosition;
      ParticleAgents[Index].PredictedPosition = (*Found)->CorrectedPosition;
    }
  }
  TestEqual(TEXT("Final hard violations"), FinalSummary.HardPairViolationCount, 0);
  TestEqual(TEXT("Final swept violations"), FinalSummary.SweptPairViolationCount, 0);
  TestEqual(TEXT("Final obstacle violations"), FinalSummary.ObstaclePenetrationCount, 0);
  TestEqual(TEXT("Final bounds violations"), FinalSummary.BoundsViolationCount, 0);

  TSet<int32> QuantizedRadii;
  TSet<int32> AngularSectors;
  TArray<int32> FinalSectorPopulation;
  FinalSectorPopulation.Init(0, 16);
  for (int32 Index = 0; Index < 20; ++Index)
  {
    const FVector Position = ParticleAgents[Index].StartPosition;
    QuantizedRadii.Add(FMath::RoundToInt(Position.Size2D() / 10.0f));
    float Angle = FMath::Atan2(Position.Y, Position.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    const int32 Sector = FMath::Clamp(
      FMath::FloorToInt(Angle / (2.0f * PI) * 16.0f), 0, 15);
    AngularSectors.Add(Sector);
    ++FinalSectorPopulation[Sector];
  }
  int32 FinalMaximumSectorPopulation = 0;
  for (const int32 Population : FinalSectorPopulation)
    FinalMaximumSectorPopulation = FMath::Max(FinalMaximumSectorPopulation, Population);
  TestTrue(TEXT("Cohort does not collapse onto one exact ring"), QuantizedRadii.Num() > 1);
  TestTrue(TEXT("Concentrated cohort increases occupied sectors"),
    AngularSectors.Num() > InitialOccupiedSectors);
  TestTrue(TEXT("Concentrated cohort reduces maximum sector population"),
    FinalMaximumSectorPopulation < InitialMaximumSectorPopulation);
  return true;
}

#endif
