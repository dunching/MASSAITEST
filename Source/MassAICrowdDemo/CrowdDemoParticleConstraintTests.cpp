#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

namespace
{
  FCrowdDemoParticleConstraintEnvironment MakeOpenEnvironment(const float HalfExtent = 10000.0f)
  {
    FCrowdDemoParticleConstraintEnvironment Environment;
    Environment.FlowConfig.BoundsMin = FVector(-HalfExtent, -HalfExtent, 0.0f);
    Environment.FlowConfig.BoundsMax = FVector(HalfExtent, HalfExtent, 0.0f);
    Environment.FlowConfig.ObstacleSpecs.Reset();
    Environment.bConstrainToFlowBounds = true;
    return Environment;
  }

  FCrowdDemoParticleConstraintAgent MakeAgent(
    const int32 AgentId,
    const FVector& Start,
    const FVector& Predicted)
  {
    FCrowdDemoParticleConstraintAgent Agent;
    Agent.AgentId = AgentId;
    Agent.StartPosition = Start;
    Agent.PredictedPosition = Predicted;
    return Agent;
  }

  const FCrowdDemoParticleConstraintResult* FindResult(
    TConstArrayView<FCrowdDemoParticleConstraintResult> Results,
    const int32 AgentId)
  {
    return Results.FindByPredicate([AgentId](const auto& Result) { return Result.AgentId == AgentId; });
  }

  FCrowdDemoParticleConstraintSummary SolveAgents(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    const FCrowdDemoParticleConstraintEnvironment& Environment,
    TArray<FCrowdDemoParticleConstraintResult>& OutResults,
    TArray<FCrowdDemoParticleConstraintPair>* OutPairs = nullptr)
  {
    FCrowdDemoParticleConstraintSettings Settings;
    TArray<FCrowdDemoParticleConstraintPair> LocalPairs;
    FCrowdDemoParticleConstraintSummary Summary;
    FCrowdDemoParticleConstraintKernel::Solve(
      Agents, Environment, Settings, LocalPairs, OutResults, Summary);
    if (OutPairs) *OutPairs = MoveTemp(LocalPairs);
    return Summary;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticlePairMobilityTest,
  "CrowdDemo.SoftPressure.Particle.PairMobility",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticlePairMobilityTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  Agents.Add(MakeAgent(10, FVector(-50.0f, 0.0f, 60.0f), FVector(-50.0f, 0.0f, 60.0f)));
  Agents.Add(MakeAgent(20, FVector(50.0f, 0.0f, 60.0f), FVector(50.0f, 0.0f, 60.0f)));
  TArray<FCrowdDemoParticleConstraintResult> Results;
  auto Summary = SolveAgents(Agents, Environment, Results);
  TestTrue(TEXT("equal mobility valid"), Summary.bValid);
  const auto* EqualA = FindResult(Results, 10);
  const auto* EqualB = FindResult(Results, 20);
  TestNotNull(TEXT("equal A"), EqualA);
  TestNotNull(TEXT("equal B"), EqualB);
  if (EqualA && EqualB)
  {
    TestTrue(TEXT("equal symmetric"), FMath::IsNearlyEqual(
      FMath::Abs(EqualA->RealizedCorrection.X), FMath::Abs(EqualB->RealizedCorrection.X), 0.01f));
    const float Distance = FVector::Dist2D(EqualA->CorrectedPosition, EqualB->CorrectedPosition);
    TestTrue(TEXT("soft response advances"), Distance > 100.0f);
    TestTrue(TEXT("soft response remains compliant"), Distance < 127.0f);
  }

  Agents[0].Mobility = 1.0f;
  Agents[1].Mobility = 3.0f;
  Summary = SolveAgents(Agents, Environment, Results);
  const auto* RatioA = FindResult(Results, 10);
  const auto* RatioB = FindResult(Results, 20);
  TestTrue(TEXT("ratio valid"), Summary.bValid);
  if (RatioA && RatioB)
  {
    TestTrue(TEXT("mobility ratio"), FMath::IsNearlyEqual(
      RatioB->RealizedCorrection.Size2D(), 3.0f * RatioA->RealizedCorrection.Size2D(), 1.1f));
  }

  Agents[0].Mobility = 0.0f;
  Agents[1].Mobility = 1.0f;
  Summary = SolveAgents(Agents, Environment, Results);
  const auto* Fixed = FindResult(Results, 10);
  TestTrue(TEXT("zero mobility valid"), Summary.bValid);
  if (Fixed) TestTrue(TEXT("zero mobility immovable"), Fixed->RealizedCorrection.IsNearlyZero(0.01f));

  Agents[0] = MakeAgent(10, FVector::ZeroVector, FVector::ZeroVector);
  Agents[1] = MakeAgent(20, FVector::ZeroVector, FVector::ZeroVector);
  Summary = SolveAgents(Agents, Environment, Results);
  TestTrue(TEXT("zero distance resolved"), Summary.HardPairViolationCount == 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleGridDeterminismTest,
  "CrowdDemo.SoftPressure.Particle.GridDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleGridDeterminismTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  for (int32 Index = 0; Index < 24; ++Index)
  {
    const FVector Start(
      static_cast<float>((Index * 173) % 900 - 450),
      static_cast<float>((Index * 277) % 700 - 350), 60.0f);
    const FVector Delta(
      static_cast<float>((Index * 41) % 120 - 60),
      static_cast<float>((Index * 67) % 120 - 60), 0.0f);
    Agents.Add(MakeAgent(100 + Index, Start, Start + Delta));
  }
  TArray<FVector> Ends;
  for (const auto& Agent : Agents) Ends.Add(Agent.PredictedPosition);
  TArray<FCrowdDemoParticleConstraintPair> GridPairs;
  FCrowdDemoParticleConstraintKernel::BuildCandidatePairs(Agents, Ends, GridPairs);
  TArray<TPair<int32, int32>> BrutePairs;
  for (int32 A = 0; A < Agents.Num(); ++A)
  {
    for (int32 B = A + 1; B < Agents.Num(); ++B)
    {
      const float Hard = Agents[A].PhysicalRadiusCm + Agents[B].PhysicalRadiusCm
        + FMath::Max(Agents[A].HardSafetyGapCm, Agents[B].HardSafetyGapCm);
      const float Soft = Hard + Agents[A].SoftMarginCm + Agents[B].SoftMarginCm;
      const FVector RelativeStart = Agents[A].StartPosition - Agents[B].StartPosition;
      const FVector RelativeDelta = (Ends[A] - Agents[A].StartPosition)
        - (Ends[B] - Agents[B].StartPosition);
      const float Denominator = RelativeDelta.SizeSquared2D();
      const float Time = Denominator > SMALL_NUMBER
        ? FMath::Clamp(-FVector::DotProduct(RelativeStart, RelativeDelta) / Denominator, 0.0f, 1.0f)
        : 0.0f;
      const float SweptDistance = (RelativeStart + RelativeDelta * Time).Size2D();
      if (FVector::Dist2D(Ends[A], Ends[B]) < Soft || SweptDistance < Hard)
        BrutePairs.Add(TPair<int32, int32>(Agents[A].AgentId, Agents[B].AgentId));
    }
  }
  TestEqual(TEXT("grid/brute pair count"), GridPairs.Num(), BrutePairs.Num());
  for (int32 Index = 0; Index < FMath::Min(GridPairs.Num(), BrutePairs.Num()); ++Index)
  {
    TestEqual(TEXT("pair min"), GridPairs[Index].MinAgentId, BrutePairs[Index].Key);
    TestEqual(TEXT("pair max"), GridPairs[Index].MaxAgentId, BrutePairs[Index].Value);
  }

  TArray<FCrowdDemoParticleConstraintResult> ResultsA, ResultsB;
  const auto SummaryA = SolveAgents(Agents, Environment, ResultsA);
  Algo::Reverse(Agents);
  const auto SummaryB = SolveAgents(Agents, Environment, ResultsB);
  TestEqual(TEXT("reverse hash"), SummaryA.CandidateHash, SummaryB.CandidateHash);
  TestEqual(TEXT("reverse valid"), SummaryA.bValid, SummaryB.bValid);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticlePropagationTest,
  "CrowdDemo.SoftPressure.Particle.PropagationAndRemoval",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticlePropagationTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  TMap<int32, FVector> InitialPositions;
  auto InsertSource = MakeAgent(1, FVector(0, 0, 60), FVector(0, 0, 60));
  InsertSource.Mobility = 0.0f;
  Agents.Add(InsertSource);
  int32 AgentId = 2;
  for (const float Sign : {-1.0f, 1.0f})
  {
    Agents.Add(MakeAgent(AgentId++, FVector(Sign * 100, 0, 60), FVector(Sign * 100, 0, 60)));
    Agents.Add(MakeAgent(AgentId++, FVector(Sign * 229, 0, 60), FVector(Sign * 229, 0, 60)));
    Agents.Add(MakeAgent(AgentId++, FVector(Sign * 358, 0, 60), FVector(Sign * 358, 0, 60)));
  }
  for (const auto& Agent : Agents) InitialPositions.Add(Agent.AgentId, Agent.StartPosition);
  TArray<FCrowdDemoParticleConstraintResult> Results;
  FCrowdDemoParticleConstraintSummary Summary;
  for (int32 Step = 0; Step < 300; ++Step)
  {
    Summary = SolveAgents(Agents, Environment, Results);
    TestTrue(TEXT("propagation rollout remains valid"), Summary.bValid);
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
      Agents[Index].StartPosition = Results[Index].CorrectedPosition;
      Agents[Index].PredictedPosition = Results[Index].CorrectedPosition;
    }
  }
  int32 DisplacedAgentCount = 0;
  for (const auto& Result : Results)
  {
    const FVector* Initial = InitialPositions.Find(Result.AgentId);
    if (Initial && FVector::Dist2D(*Initial, Result.CorrectedPosition) > 1.0f)
      ++DisplacedAgentCount;
  }
  AddInfo(FString::Printf(TEXT("propagation displaced=%d final_soft_p95=%.3f final_soft_max=%.3f"),
    DisplacedAgentCount, Summary.SoftErrorCmP95, Summary.SoftErrorCmMax));
  TestTrue(TEXT("pressure propagates spatially over fixed steps"), DisplacedAgentCount >= 5);
  TestTrue(TEXT("iteration metric is diagnostic only"), Summary.FirstInfluencedIterationMax >= 1);

  TArray<FCrowdDemoParticleConstraintAgent> Removed;
  for (const auto& Result : Results)
  {
    if (Result.AgentId == 1) continue;
    Removed.Add(MakeAgent(Result.AgentId, Result.CorrectedPosition, Result.CorrectedPosition));
  }
  TArray<FCrowdDemoParticleConstraintResult> RemovedResults;
  const auto RemovedSummary = SolveAgents(Removed, Environment, RemovedResults);
  TestTrue(TEXT("removal stays safe"), RemovedSummary.bValid);
  TestTrue(TEXT("no formation restoration owner"), RemovedSummary.CandidateHash != 0u);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleSweptObstacleTest,
  "CrowdDemo.SoftPressure.Particle.SweptObstacleAndNarrowGate",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleSweptObstacleTest::RunTest(const FString& Parameters)
{
  auto Environment = MakeOpenEnvironment(1000.0f);
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  auto A = MakeAgent(1, FVector(-30, 0, 60), FVector(36, 0, 60));
  auto B = MakeAgent(2, FVector(30, 0, 60), FVector(-36, 0, 60));
  A.PhysicalRadiusCm = B.PhysicalRadiusCm = 10.0f;
  A.SoftMarginCm = B.SoftMarginCm = 0.0f;
  Agents = {A, B};
  TArray<FCrowdDemoParticleConstraintResult> Results;
  auto Summary = SolveAgents(Agents, Environment, Results);
  AddInfo(FString::Printf(TEXT("head_on hard=%d swept=%d obstacle=%d soft=%.3f hash=%u"),
    Summary.HardPairViolationCount, Summary.SweptPairViolationCount,
    Summary.ObstaclePenetrationCount, Summary.SoftErrorCmMax, Summary.CandidateHash));
  for (const auto& Result : Results)
    AddInfo(FString::Printf(TEXT("head_on agent=%d end=(%.1f,%.1f) correction=(%.1f,%.1f)"),
      Result.AgentId, Result.CorrectedPosition.X, Result.CorrectedPosition.Y,
      Result.RealizedCorrection.X, Result.RealizedCorrection.Y));
  TestTrue(TEXT("fast head-on valid"), Summary.bValid);
  TestEqual(TEXT("fast head-on swept violations"), Summary.SweptPairViolationCount, 0);

  FCrowdDemoSharedFlowObstacleSpec Obstacle;
  Obstacle.ObstacleId = 77;
  Obstacle.Center = FVector(0, 0, 60);
  Obstacle.Extent = FVector(10, 300, 100);
  Environment.FlowConfig.ObstacleSpecs = {Obstacle};
  Agents.Reset();
  Agents.Add(MakeAgent(10, FVector(-120, 0, 60), FVector(120, 0, 60)));
  Summary = SolveAgents(Agents, Environment, Results);
  TestTrue(TEXT("obstacle constrained"), Summary.ObstaclePenetrationCount == 0);
  const auto* ObstacleResult = FindResult(Results, 10);
  if (ObstacleResult) TestTrue(TEXT("cannot cross wall"), ObstacleResult->CorrectedPosition.X < -60.0f);

  Environment = MakeOpenEnvironment(55.0f);
  Agents.Reset();
  auto Narrow = MakeAgent(20, FVector(0, -40, 60), FVector(0, 40, 60));
  Narrow.PhysicalRadiusCm = 42.0f;
  Narrow.HardSafetyGapCm = 10.0f;
  Narrow.SoftMarginCm = 17.0f;
  Agents.Add(Narrow);
  Summary = SolveAgents(Agents, Environment, Results);
  TestTrue(TEXT("110cm gate hard safe"), Summary.bValid);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleBoxAndRingTest,
  "CrowdDemo.SoftPressure.Particle.BoxAndRing",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleBoxAndRingTest::RunTest(const FString& Parameters)
{
  const auto BoxEnvironment = MakeOpenEnvironment(650.0f);
  TArray<FCrowdDemoParticleConstraintAgent> Box;
  int32 AgentId = 1;
  for (int32 Y = 0; Y < 10; ++Y)
    for (int32 X = 0; X < 10; ++X)
    {
      const FVector Position(-450.0f + X * 100.0f, -450.0f + Y * 100.0f, 60.0f);
      Box.Add(MakeAgent(AgentId++, Position, Position));
    }
  TArray<FCrowdDemoParticleConstraintResult> Results;
  auto InitialSummary = SolveAgents(Box, BoxEnvironment, Results);
  TestTrue(TEXT("box hard safe"), InitialSummary.bValid);
  const float InitialP95 = InitialSummary.SoftErrorCmP95;
  for (int32 Step = 0; Step < 180; ++Step)
  {
    for (int32 Index = 0; Index < Box.Num(); ++Index)
    {
      Box[Index].StartPosition = Results[Index].CorrectedPosition;
      Box[Index].PredictedPosition = Results[Index].CorrectedPosition;
    }
    InitialSummary = SolveAgents(Box, BoxEnvironment, Results);
  }
  TestTrue(TEXT("box remains hard safe"), InitialSummary.bValid);
  AddInfo(FString::Printf(TEXT("box initial_p95=%.3f final_p95=%.3f final_max=%.3f influenced=%d"),
    InitialP95, InitialSummary.SoftErrorCmP95, InitialSummary.SoftErrorCmMax,
    InitialSummary.PressureInfluencedAgentCount));
  TestTrue(TEXT("uniform box pressure is processed"),
    InitialSummary.PressureInfluencedAgentCount == Box.Num());

  const auto RingEnvironment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Ring;
  auto Center = MakeAgent(1000, FVector::ZeroVector, FVector::ZeroVector);
  Center.PhysicalRadiusCm = 30.0f;
  Center.Mobility = 0.25f;
  Ring.Add(Center);
  for (int32 Index = 0; Index < 8; ++Index)
  {
    const float Angle = 2.0f * PI * static_cast<float>(Index) / 8.0f;
    auto Outer = MakeAgent(1100 + Index,
      FVector(FMath::Cos(Angle) * 70.0f, FMath::Sin(Angle) * 70.0f, 0.0f),
      FVector(FMath::Cos(Angle) * 70.0f, FMath::Sin(Angle) * 70.0f, 0.0f));
    Outer.PhysicalRadiusCm = 20.0f;
    Ring.Add(Outer);
  }
  const auto RingSummary = SolveAgents(Ring, RingEnvironment, Results);
  TestTrue(TEXT("ring hard safe"), RingSummary.bValid);
  TestTrue(TEXT("ring pressure applied"), RingSummary.PressureInfluencedAgentCount >= 8);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleSoftResponseComplianceTest,
  "CrowdDemo.SoftPressure.Particle.SoftResponseCompliance",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleSoftResponseComplianceTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  Agents.Add(MakeAgent(1, FVector(-50.0f, 0.0f, 60.0f), FVector(-50.0f, 0.0f, 60.0f)));
  Agents.Add(MakeAgent(2, FVector(50.0f, 0.0f, 60.0f), FVector(50.0f, 0.0f, 60.0f)));
  TArray<FCrowdDemoParticleConstraintResult> Results;
  const FCrowdDemoParticleConstraintSummary Summary = SolveAgents(Agents, Environment, Results);
  const auto* A = FindResult(Results, 1);
  const auto* B = FindResult(Results, 2);
  TestTrue(TEXT("soft-only candidate remains hard-safe"), Summary.bValid);
  TestNotNull(TEXT("soft response A"), A);
  TestNotNull(TEXT("soft response B"), B);
  if (A && B)
  {
    const float Distance = FVector::Dist2D(A->CorrectedPosition, B->CorrectedPosition);
    TestTrue(TEXT("soft response makes positive progress"), Distance > 100.0f);
    TestTrue(TEXT("one fixed step does not force full 128cm shell"), Distance < 127.0f);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleHighPressure95CmStressTest,
  "CrowdDemo.SoftPressure.Particle.HighPressure95cmStressGate",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleHighPressure95CmStressTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  int32 AgentId = 1;
  for (int32 Y = 0; Y < 4; ++Y)
    for (int32 X = 0; X < 5; ++X)
    {
      const FVector Position((X - 2) * 95.0f, (Y - 1.5f) * 95.0f, 60.0f);
      Agents.Add(MakeAgent(AgentId++, Position, Position));
    }

  TArray<FCrowdDemoParticleConstraintResult> Results;
  FCrowdDemoParticleConstraintSummary Summary;
  for (int32 Step = 0; Step < 30; ++Step)
  {
    Summary = SolveAgents(Agents, Environment, Results);
    TestTrue(TEXT("95cm stress remains hard-safe"), Summary.bValid);
    if (!Summary.bValid) break;
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
      Agents[Index].StartPosition = Results[Index].CorrectedPosition;
      Agents[Index].PredictedPosition = Results[Index].CorrectedPosition;
    }
  }
  TestEqual(TEXT("95cm stress hard violations"), Summary.HardPairViolationCount, 0);
  TestEqual(TEXT("95cm stress swept violations"), Summary.SweptPairViolationCount, 0);
  TestEqual(TEXT("95cm stress obstacle violations"), Summary.ObstaclePenetrationCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleBox100InsertRemoveTest,
  "CrowdDemo.SoftPressure.Particle.Box100InsertRemove",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleBox100InsertRemoveTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment(750.0f);
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  int32 AgentId = 1;
  for (int32 Y = 0; Y < 10; ++Y)
    for (int32 X = 0; X < 10; ++X)
    {
      const FVector Position(-603.0f + X * 134.0f, -603.0f + Y * 134.0f, 60.0f);
      Agents.Add(MakeAgent(AgentId++, Position, Position));
    }
  TMap<int32, FVector> InitialPositions;
  for (const auto& Agent : Agents) InitialPositions.Add(Agent.AgentId, Agent.StartPosition);
  // The four nearest agents are 94.75cm away: outside the 94cm hard shell and
  // inside the 128cm soft shell. The original 134cm grid has no soft error.
  Agents.Add(MakeAgent(1001, FVector::ZeroVector, FVector::ZeroVector));
  TArray<FCrowdDemoParticleConstraintResult> Results;
  FCrowdDemoParticleConstraintSummary InsertSummary;
  for (int32 Step = 0; Step < 120; ++Step)
  {
    InsertSummary = SolveAgents(Agents, Environment, Results);
    TestTrue(TEXT("box insert rollout remains hard safe"), InsertSummary.bValid);
    if (!InsertSummary.bValid) break;
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
      Agents[Index].StartPosition = Results[Index].CorrectedPosition;
      Agents[Index].PredictedPosition = Results[Index].CorrectedPosition;
    }
  }
  int32 DisplacedOriginalCount = 0;
  for (const auto& Result : Results)
  {
    const FVector* Initial = InitialPositions.Find(Result.AgentId);
    if (Initial && FVector::Dist2D(*Initial, Result.CorrectedPosition) > 1.0f)
      ++DisplacedOriginalCount;
  }
  AddInfo(FString::Printf(TEXT("box_insert displaced_original=%d soft_p95=%.3f soft_max=%.3f"),
    DisplacedOriginalCount, InsertSummary.SoftErrorCmP95, InsertSummary.SoftErrorCmMax));
  TestTrue(TEXT("box insert propagates beyond four direct neighbours"), DisplacedOriginalCount > 4);

  TArray<FCrowdDemoParticleConstraintAgent> Removed;
  for (const auto& Result : Results)
    if (Result.AgentId != 1001)
      Removed.Add(MakeAgent(Result.AgentId, Result.CorrectedPosition, Result.CorrectedPosition));
  FCrowdDemoParticleConstraintSummary RemoveSummary;
  FCrowdDemoParticleSettlingTracker SettlingTracker;
  for (int32 Step = 0; Step < 180 && SettlingTracker.SettlingSteps == INDEX_NONE; ++Step)
  {
    RemoveSummary = SolveAgents(Removed, Environment, Results);
    TestTrue(TEXT("box removal rollout remains hard safe"), RemoveSummary.bValid);
    if (!RemoveSummary.bValid) break;
    FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
      SettlingTracker, RemoveSummary.MaxAgentCorrectionCm, RemoveSummary.SoftErrorCmP95);
    for (int32 Index = 0; Index < Removed.Num(); ++Index)
    {
      Removed[Index].StartPosition = Results[Index].CorrectedPosition;
      Removed[Index].PredictedPosition = Results[Index].CorrectedPosition;
    }
  }
  TestTrue(TEXT("box removal reaches a continuous 15-step equilibrium"),
    SettlingTracker.SettlingSteps != INDEX_NONE);
  bool bNewEquilibriumDiffersFromOriginal = false;
  for (const auto& Result : Results)
  {
    const FVector* Original = InitialPositions.Find(Result.AgentId);
    bNewEquilibriumDiffersFromOriginal |= Original
      && FVector::Dist2D(*Original, Result.CorrectedPosition) > 1.0f;
  }
  TestTrue(TEXT("box removal does not restore the original matrix"),
    bNewEquilibriumDiffersFromOriginal);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleOpenInsertRemoveTest,
  "CrowdDemo.SoftPressure.Particle.OpenInsertRemove",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleOpenInsertRemoveTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  for (int32 Index = -2; Index <= 2; ++Index)
  {
    const FVector Position(Index * 128.0f, 0.0f, 60.0f);
    Agents.Add(MakeAgent(Index + 10, Position, Position));
  }
  // 98.6cm from the two nearest agents: inside the 128cm soft shell but
  // outside the 94cm hard shell, so this fixture tests pressure rather than
  // an inherited t=0 penetration.
  Agents.Add(MakeAgent(1001, FVector(64.0f, 75.0f, 60.0f), FVector(64.0f, 75.0f, 60.0f)));
  TArray<FCrowdDemoParticleConstraintResult> Results;
  const auto InsertSummary = SolveAgents(Agents, Environment, Results);
  TestTrue(TEXT("open insert hard safe"), InsertSummary.bValid);

  TArray<FCrowdDemoParticleConstraintAgent> Removed;
  bool bAnyDisplacedFromOriginal = false;
  for (const auto& Result : Results)
  {
    if (Result.AgentId == 1001) continue;
    const float OriginalX = static_cast<float>(Result.AgentId - 10) * 128.0f;
    bAnyDisplacedFromOriginal |= FMath::Abs(Result.CorrectedPosition.X - OriginalX) > 0.5f;
    auto RemovedAgent = MakeAgent(
      Result.AgentId, Result.CorrectedPosition, Result.CorrectedPosition);
    RemovedAgent.SoftMarginCm = 0.0f;
    Removed.Add(RemovedAgent);
  }
  TMap<int32, FVector> RemovalPositions;
  for (const auto& Agent : Removed) RemovalPositions.Add(Agent.AgentId, Agent.StartPosition);
  FCrowdDemoParticleConstraintSummary RemoveSummary;
  for (int32 Step = 0; Step < 30; ++Step)
  {
    RemoveSummary = SolveAgents(Removed, Environment, Results);
    TestTrue(TEXT("open removal remains safe"), RemoveSummary.bValid);
    TestEqual(TEXT("open no-soft-error rollout has no influenced agents"),
      RemoveSummary.PressureInfluencedAgentCount, 0);
    for (const auto& Result : Results)
      TestTrue(TEXT("open no-soft-error rollout does not restore old positions"),
        Result.CorrectedPosition.Equals(RemovalPositions.FindChecked(Result.AgentId), 0.01f));
    for (int32 Index = 0; Index < Removed.Num(); ++Index)
    {
      Removed[Index].StartPosition = Results[Index].CorrectedPosition;
      Removed[Index].PredictedPosition = Results[Index].CorrectedPosition;
    }
  }
  TestTrue(TEXT("open removal has no implicit original-position restore"), bAnyDisplacedFromOriginal);
  return true;
}

namespace
{
  bool RunRingRollout(
    FAutomationTestBase& Test,
    const bool bExit,
    float& OutOuterDisplacement,
    float& OutCenterProgress,
    int32& OutMovedOuterCount)
  {
    const auto Environment = MakeOpenEnvironment();
    TArray<FCrowdDemoParticleConstraintAgent> Agents;
    auto Center = MakeAgent(1000,
      bExit ? FVector::ZeroVector : FVector(-220.0f, 0.0f, 60.0f),
      bExit ? FVector::ZeroVector : FVector(-220.0f, 0.0f, 60.0f));
    Center.PhysicalRadiusCm = 30.0f;
    Center.SoftMarginCm = 17.0f;
    Agents.Add(Center);
    TMap<int32, FVector> OriginalOuter;
    for (int32 Index = 0; Index < 8; ++Index)
    {
      const float Angle = 2.0f * PI * static_cast<float>(Index) / 8.0f;
      const FVector Position(FMath::Cos(Angle) * 110.0f, FMath::Sin(Angle) * 110.0f, 60.0f);
      auto Outer = MakeAgent(1100 + Index, Position, Position);
      Outer.PhysicalRadiusCm = 20.0f;
      Agents.Add(Outer);
      OriginalOuter.Add(Outer.AgentId, Position);
    }

    const FVector InitialCenter = Agents[0].StartPosition;
    TArray<FCrowdDemoParticleConstraintResult> Results;
    for (int32 Step = 0; Step < 16; ++Step)
    {
      Agents[0].PredictedPosition = Agents[0].StartPosition + FVector(20.0f, 0.0f, 0.0f);
      for (int32 Index = 1; Index < Agents.Num(); ++Index)
        Agents[Index].PredictedPosition = Agents[Index].StartPosition;
      const auto Summary = SolveAgents(Agents, Environment, Results);
      if (!Test.TestTrue(*FString::Printf(TEXT("ring %s step %d hard safe"),
        bExit ? TEXT("exit") : TEXT("entry"), Step), Summary.bValid))
        return false;
      for (int32 Index = 0; Index < Agents.Num(); ++Index)
      {
        const auto* Result = FindResult(Results, Agents[Index].AgentId);
        if (!Result) return false;
        Agents[Index].StartPosition = Result->CorrectedPosition;
      }
    }
    OutOuterDisplacement = 0.0f;
    OutMovedOuterCount = 0;
    for (int32 Index = 1; Index < Agents.Num(); ++Index)
    {
      const float Displacement =
        FVector::Dist2D(Agents[Index].StartPosition, OriginalOuter.FindChecked(Agents[Index].AgentId));
      OutOuterDisplacement = FMath::Max(OutOuterDisplacement,
        Displacement);
      OutMovedOuterCount += Displacement > 1.0f ? 1 : 0;
    }
    OutCenterProgress = Agents[0].StartPosition.X - InitialCenter.X;
    return true;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleRingEntryRolloutTest,
  "CrowdDemo.SoftPressure.Particle.RingEntryRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleRingEntryRolloutTest::RunTest(const FString& Parameters)
{
  float OuterDisplacement = 0.0f;
  float CenterProgress = 0.0f;
  int32 MovedOuterCount = 0;
  const bool bValid = RunRingRollout(
    *this, false, OuterDisplacement, CenterProgress, MovedOuterCount);
  TestTrue(TEXT("ring entry moves local crowd"), OuterDisplacement > 1.0f);
  TestTrue(TEXT("ring entry transit has positive progress"), CenterProgress > 50.0f);
  TestTrue(TEXT("ring entry produces local yielding"), MovedOuterCount > 0);
  return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleRingExitRolloutTest,
  "CrowdDemo.SoftPressure.Particle.RingExitRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleRingExitRolloutTest::RunTest(const FString& Parameters)
{
  float OuterDisplacement = 0.0f;
  float CenterProgress = 0.0f;
  int32 MovedOuterCount = 0;
  const bool bValid = RunRingRollout(
    *this, true, OuterDisplacement, CenterProgress, MovedOuterCount);
  TestTrue(TEXT("ring exit transit has positive progress"), CenterProgress > 50.0f);
  TestTrue(TEXT("ring exit produces local yielding"), MovedOuterCount > 0);
  return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleObstaclePairConflictTest,
  "CrowdDemo.SoftPressure.Particle.ObstaclePairConflict",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleObstaclePairConflictTest::RunTest(const FString& Parameters)
{
  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.FlowConfig = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  Environment.bConstrainToFlowBounds = true;
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  const auto AddAgent = [&Agents](const int32 AgentId, const FVector& Start, const FVector& Predict)
  {
    auto Agent = MakeAgent(AgentId, Start, Predict);
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.SoftMarginCm = 17.0f;
    Agent.Mobility = 1.0f;
    Agents.Add(Agent);
  };
  AddAgent(0, FVector(1284.000f, -2032.000f, 60.000f), FVector(1300.720f, -2011.226f, 60.000f));
  AddAgent(1, FVector(1452.000f, -1847.000f, 60.000f), FVector(1470.953f, -1828.241f, 60.000f));
  AddAgent(2, FVector(1616.000f, -1852.000f, 60.000f), FVector(1624.433f, -1826.702f, 60.000f));
  AddAgent(3, FVector(1646.000f, -1734.000f, 60.000f), FVector(1647.268f, -1707.364f, 60.000f));
  AddAgent(4, FVector(1647.000f, -1608.000f, 60.000f), FVector(1648.377f, -1581.369f, 60.000f));
  AddAgent(5, FVector(1647.000f, -1487.000f, 60.000f), FVector(1647.584f, -1460.340f, 60.000f));
  AddAgent(6, FVector(1647.000f, -1365.000f, 60.000f), FVector(1647.695f, -1338.342f, 60.000f));
  AddAgent(7, FVector(1647.000f, -1244.000f, 60.000f), FVector(1647.851f, -1217.347f, 60.000f));
  AddAgent(8, FVector(1649.000f, -1083.000f, 60.000f), FVector(1665.127f, -1061.763f, 60.000f));
  AddAgent(9, FVector(1791.000f, -963.000f, 60.000f), FVector(1817.042f, -957.262f, 60.000f));
  AddAgent(10, FVector(1379.000f, -1949.000f, 60.000f), FVector(1394.541f, -1927.330f, 60.000f));
  AddAgent(11, FVector(1535.000f, -1765.000f, 60.000f), FVector(1553.856f, -1746.144f, 60.000f));
  AddAgent(12, FVector(1573.000f, -1667.000f, 60.000f), FVector(1587.660f, -1644.725f, 60.000f));
  AddAgent(13, FVector(1572.000f, -1548.000f, 60.000f), FVector(1588.607f, -1527.135f, 60.000f));
  AddAgent(14, FVector(1573.000f, -1426.000f, 60.000f), FVector(1591.979f, -1407.267f, 60.000f));
  AddAgent(15, FVector(1572.000f, -1305.000f, 60.000f), FVector(1593.794f, -1289.633f, 60.000f));
  AddAgent(16, FVector(1597.000f, -1163.000f, 60.000f), FVector(1608.324f, -1138.857f, 60.000f));
  AddAgent(17, FVector(1700.000f, -1002.000f, 60.000f), FVector(1725.196f, -993.266f, 60.000f));
  AddAgent(18, FVector(1896.000f, -954.000f, 60.000f), FVector(1922.594f, -952.030f, 60.000f));
  AddAgent(19, FVector(2010.000f, -953.000f, 60.000f), FVector(2036.661f, -952.429f, 60.000f));
  TArray<FCrowdDemoParticleConstraintResult> Results;
  const auto Summary = SolveAgents(Agents, Environment, Results);
  const auto* ResultA = FindResult(Results, 4);
  const auto* ResultB = FindResult(Results, 12);
  TestNotNull(TEXT("8368 Agent 4 result"), ResultA);
  TestNotNull(TEXT("8368 Agent 12 result"), ResultB);
  TestTrue(TEXT("8368 obstacle/pair closure valid"), Summary.bValid);
  TestEqual(TEXT("8368 no obstacle penetration"), Summary.ObstaclePenetrationCount, 0);
  TestEqual(TEXT("8368 no bounds violation"), Summary.BoundsViolationCount, 0);
  TestEqual(TEXT("8368 no hard violation"), Summary.HardPairViolationCount, 0);
  TestEqual(TEXT("8368 no swept violation"), Summary.SweptPairViolationCount, 0);
  if (ResultA && ResultB)
  {
    const float EndpointDistance = FVector::Dist2D(
      ResultA->CorrectedPosition, ResultB->CorrectedPosition);
    AddInfo(FString::Printf(TEXT("8368 endpoint_distance=%.3f candidate_hash=%u"),
      EndpointDistance, Summary.CandidateHash));
    TestTrue(TEXT("8368 keeps 94cm hard distance"), EndpointDistance + 0.01f >= 94.0f);
  }
  if (!Summary.bValid)
  {
    TArray<FVector> EndPositions;
    for (const auto& Agent : Agents)
    {
      const auto* Result = FindResult(Results, Agent.AgentId);
      EndPositions.Add(Result ? Result->CorrectedPosition : Agent.StartPosition);
    }
    TArray<FCrowdDemoParticleConstraintPair> FailurePairs;
    FCrowdDemoParticleConstraintKernel::BuildCandidatePairs(Agents, EndPositions, FailurePairs);
    for (const auto& Pair : FailurePairs)
    {
      const auto& PairA = Agents[Pair.MinAgentIndex];
      const auto& PairB = Agents[Pair.MaxAgentIndex];
      const float Required = PairA.PhysicalRadiusCm + PairB.PhysicalRadiusCm
        + FMath::Max(PairA.HardSafetyGapCm, PairB.HardSafetyGapCm);
      const float Distance = FVector::Dist2D(
        EndPositions[Pair.MinAgentIndex], EndPositions[Pair.MaxAgentIndex]);
      if (Distance + 0.01f < Required || Pair.MinAgentId == 4 || Pair.MaxAgentId == 4
        || Pair.MinAgentId == 12 || Pair.MaxAgentId == 12)
      {
        AddInfo(FString::Printf(
          TEXT("8368 pair=%d,%d distance=%.3f required=%.3f A=(%.1f,%.1f) B=(%.1f,%.1f)"),
          Pair.MinAgentId, Pair.MaxAgentId, Distance, Required,
          EndPositions[Pair.MinAgentIndex].X, EndPositions[Pair.MinAgentIndex].Y,
          EndPositions[Pair.MaxAgentIndex].X, EndPositions[Pair.MaxAgentIndex].Y));
      }
    }
  }

  const uint32 ForwardHash = Summary.CandidateHash;
  Algo::Reverse(Agents);
  const auto ReverseSummary = SolveAgents(Agents, Environment, Results);
  TestTrue(TEXT("8368 reverse closure valid"), ReverseSummary.bValid);
  TestEqual(TEXT("8368 reverse hash"), ReverseSummary.CandidateHash, ForwardHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticle8371UnifiedClosureRedTest,
  "CrowdDemo.SoftPressure.Particle.8371UnifiedClosure",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticle8371UnifiedClosureRedTest::RunTest(const FString& Parameters)
{
  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.FlowConfig = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  Environment.bConstrainToFlowBounds = true;
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  const auto AddAgent = [&Agents](const int32 AgentId, const FVector& Start, const FVector& Predict)
  {
    auto Agent = MakeAgent(AgentId, Start, Predict);
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.SoftMarginCm = 17.0f;
    Agent.Mobility = 1.0f;
    Agents.Add(Agent);
  };
  AddAgent(0, FVector(1982.000f, -953.000f, 60.000f), FVector(2008.641f, -951.825f, 60.000f));
  AddAgent(1, FVector(2170.000f, -953.000f, 60.000f), FVector(2196.648f, -952.001f, 60.000f));
  AddAgent(2, FVector(2292.000f, -1045.000f, 60.000f), FVector(2305.896f, -1022.240f, 60.000f));
  AddAgent(3, FVector(2390.000f, -1043.000f, 60.000f), FVector(2404.457f, -1020.592f, 60.000f));
  AddAgent(4, FVector(2495.000f, -1037.000f, 60.000f), FVector(2482.749f, -1013.314f, 60.000f));
  AddAgent(5, FVector(2541.000f, -916.000f, 60.000f), FVector(2519.413f, -900.344f, 60.000f));
  AddAgent(6, FVector(2455.000f, -679.000f, 60.000f), FVector(2438.166f, -658.318f, 60.000f));
  AddAgent(7, FVector(2328.000f, -502.000f, 60.000f), FVector(2305.812f, -487.208f, 60.000f));
  AddAgent(8, FVector(2174.000f, -349.000f, 60.000f), FVector(2153.160f, -332.362f, 60.000f));
  AddAgent(9, FVector(1999.000f, -199.000f, 60.000f), FVector(1980.144f, -180.144f, 60.000f));
  AddAgent(10, FVector(2076.000f, -953.000f, 60.000f), FVector(2102.645f, -951.920f, 60.000f));
  AddAgent(11, FVector(2264.000f, -953.000f, 60.000f), FVector(2290.650f, -952.070f, 60.000f));
  AddAgent(12, FVector(2358.000f, -953.000f, 60.000f), FVector(2384.653f, -952.131f, 60.000f));
  AddAgent(13, FVector(2452.000f, -953.000f, 60.000f), FVector(2451.482f, -926.338f, 60.000f));
  AddAgent(14, FVector(2464.000f, -811.000f, 60.000f), FVector(2458.035f, -785.009f, 60.000f));
  AddAgent(15, FVector(2396.000f, -587.000f, 60.000f), FVector(2376.554f, -568.753f, 60.000f));
  AddAgent(16, FVector(2267.000f, -412.000f, 60.000f), FVector(2243.437f, -399.514f, 60.000f));
  AddAgent(17, FVector(2112.000f, -249.000f, 60.000f), FVector(2097.846f, -226.400f, 60.000f));
  AddAgent(18, FVector(1913.000f, -113.000f, 60.000f), FVector(1894.144f, -94.144f, 60.000f));
  AddAgent(19, FVector(1872.000f, 1.000f, 60.000f), FVector(1855.106f, 21.633f, 60.000f));

  TArray<FCrowdDemoParticleConstraintResult> Results;
  const FCrowdDemoParticleConstraintSummary Summary = SolveAgents(Agents, Environment, Results);
  const FCrowdDemoParticleConstraintResult* ResultA = FindResult(Results, 5);
  const FCrowdDemoParticleConstraintResult* ResultB = FindResult(Results, 13);
  AddInfo(FString::Printf(TEXT("8371 summary valid=%d hard=%d swept=%d obstacle=%d bounds=%d constraints=%d infeasible=%d residual=%.3f env_soft=%d"),
    Summary.bValid ? 1 : 0, Summary.HardPairViolationCount, Summary.SweptPairViolationCount,
    Summary.ObstaclePenetrationCount, Summary.BoundsViolationCount,
    Summary.UnifiedHardConstraintCount, Summary.UnifiedHardInfeasibleCount,
    Summary.UnifiedHardResidualCmMax, Summary.EnvironmentSoftContactCount));
  TestNotNull(TEXT("8371 Agent 5 result"), ResultA);
  TestNotNull(TEXT("8371 Agent 13 result"), ResultB);
  TestFalse(TEXT("8371 old sequential closure is invalid"), Summary.bValid);
  TestTrue(TEXT("8371 old closure has endpoint violation"), Summary.HardPairViolationCount > 0);
  TestTrue(TEXT("8371 old closure has swept violation"), Summary.SweptPairViolationCount > 0);
  if (ResultA && ResultB)
  {
    const float Distance = FVector::Dist2D(ResultA->CorrectedPosition, ResultB->CorrectedPosition);
    AddInfo(FString::Printf(TEXT("8371 red endpoint_distance=%.3f candidate_hash=%u"),
      Distance, Summary.CandidateHash));
    TestTrue(TEXT("8371 red reproduces 93.021cm"), FMath::IsNearlyEqual(Distance, 93.021f, 0.01f));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticlePostQuantizationSafetyTest,
  "CrowdDemo.SoftPressure.Particle.PostQuantizationSafety",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticlePostQuantizationSafetyTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  auto A = MakeAgent(1, FVector(-30, 0, 60), FVector(-9.6f, 0, 60));
  auto B = MakeAgent(2, FVector(30, 0, 60), FVector(9.6f, 0, 60));
  A.PhysicalRadiusCm = B.PhysicalRadiusCm = 10.0f;
  A.HardSafetyGapCm = B.HardSafetyGapCm = 0.0f;
  A.SoftMarginCm = B.SoftMarginCm = 0.0f;
  TArray<FCrowdDemoParticleConstraintAgent> Agents = {A, B};
  TArray<FCrowdDemoParticleConstraintResult> Results;
  const auto Summary = SolveAgents(Agents, Environment, Results);
  TestTrue(TEXT("post-quantization safety valid"), Summary.bValid);
  for (const auto& Result : Results)
  {
    TestTrue(TEXT("final X is 1cm quantized"), FMath::IsNearlyEqual(
      Result.CorrectedPosition.X, FMath::RoundToFloat(Result.CorrectedPosition.X), 0.01f));
    TestTrue(TEXT("final Y is 1cm quantized"), FMath::IsNearlyEqual(
      Result.CorrectedPosition.Y, FMath::RoundToFloat(Result.CorrectedPosition.Y), 0.01f));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleInvalidCandidateAppliedTest,
  "CrowdDemo.SoftPressure.Particle.InvalidCandidateDoesNotMasqueradeAsApplied",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleInvalidCandidateAppliedTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  Agents.Add(MakeAgent(1, FVector(-100, 0, 60), FVector::ZeroVector));
  Agents.Add(MakeAgent(2, FVector(100, 0, 60), FVector::ZeroVector));
  FCrowdDemoParticleConstraintSettings Settings;
  Settings.IterationCount = 1;
  Settings.SafetyIterationCount = 1;
  Settings.SoftResponsePerSecond = 0.0f;
  Settings.HardMaxPairCorrectionPerIterationCm = 0.0f;
  TArray<FCrowdDemoParticleConstraintPair> Pairs;
  TArray<FCrowdDemoParticleConstraintResult> CandidateResults;
  FCrowdDemoParticleConstraintSummary CandidateSummary;
  FCrowdDemoParticleConstraintTrace Trace;
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, Environment, Settings, Pairs, CandidateResults, CandidateSummary, &Trace);
  TestFalse(TEXT("deliberately underpowered candidate invalid"), CandidateSummary.bValid);
  TArray<FCrowdDemoParticleConstraintResult> NoTraceResults;
  FCrowdDemoParticleConstraintSummary NoTraceSummary;
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, Environment, Settings, Pairs, NoTraceResults, NoTraceSummary);
  TestEqual(TEXT("trace does not change candidate hash"),
    CandidateSummary.CandidateHash, NoTraceSummary.CandidateHash);

  TArray<FCrowdDemoParticleAppliedState> AppliedStates;
  for (const auto& Agent : Agents)
  {
    FCrowdDemoParticleAppliedState& State = AppliedStates.AddDefaulted_GetRef();
    State.AgentId = Agent.AgentId;
    State.Position = Agent.StartPosition;
    State.Velocity = FVector::ZeroVector;
  }
  FCrowdDemoParticleConstraintSummary AppliedSummary;
  uint32 AppliedHash = 0;
  FCrowdDemoParticleConstraintKernel::EvaluateAppliedState(
    Agents, AppliedStates, Environment, AppliedSummary, AppliedHash);
  TestTrue(TEXT("fallback applied state independently valid"), AppliedSummary.bValid);
  TestNotEqual(TEXT("candidate and applied hashes are distinct objects"),
    CandidateSummary.CandidateHash, AppliedHash);
  FCrowdDemoParticleFailureFixture Fixture;
  FCrowdDemoParticleConstraintKernel::BuildFailureFixture(
    Agents, AppliedStates, Trace, 7, CandidateSummary.CandidateHash, AppliedHash, Fixture);
  TestTrue(TEXT("invalid candidate produces a two-agent fixture"), Fixture.bValid);
  TestEqual(TEXT("fixture agent count"), Fixture.Agents.Num(), 2);
  TestTrue(TEXT("fixture captures hard or swept witness"),
    Fixture.bHardViolation || Fixture.bSweptViolation);
  TestEqual(TEXT("fixture fixed step"), Fixture.FixedStepIndex, 7);
  TestNotEqual(TEXT("fixture hash"), Fixture.FixtureHash, 0u);

  Algo::Reverse(Agents);
  Algo::Reverse(AppliedStates);
  FCrowdDemoParticleConstraintSummary ReverseSummary;
  FCrowdDemoParticleConstraintTrace ReverseTrace;
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, Environment, Settings, Pairs, CandidateResults, ReverseSummary, &ReverseTrace);
  FCrowdDemoParticleFailureFixture ReverseFixture;
  FCrowdDemoParticleConstraintKernel::BuildFailureFixture(
    Agents, AppliedStates, ReverseTrace, 7, ReverseSummary.CandidateHash, AppliedHash, ReverseFixture);
  TestEqual(TEXT("fixture hash ignores input order"), Fixture.FixtureHash, ReverseFixture.FixtureHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleSettlingTrackerTest,
  "CrowdDemo.SoftPressure.Particle.SettlingTracker",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleSettlingTrackerTest::RunTest(const FString& Parameters)
{
  FCrowdDemoParticleSettlingTracker Tracker;
  for (int32 Step = 0; Step < 15; ++Step)
    FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(Tracker, 0.5f, 4.0f);
  TestEqual(TEXT("first sample only establishes the soft-error baseline"),
    Tracker.SettlingSteps, INDEX_NONE);
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(Tracker, 0.5f, 4.0f);
  TestEqual(TEXT("fifteenth consecutive settled sample triggers"), Tracker.SettlingSteps, 2);

  FCrowdDemoParticleSettlingTracker CorrectionReset;
  for (int32 Step = 0; Step < 8; ++Step)
    FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(CorrectionReset, 0.5f, 2.0f);
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(CorrectionReset, 1.1f, 2.0f);
  TestEqual(TEXT("actual correction over one centimeter resets window"),
    CorrectionReset.ConsecutiveSettledSampleCount, 0);
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(CorrectionReset, 0.5f, 3.1f);
  TestEqual(TEXT("soft-error change over one centimeter resets window"),
    CorrectionReset.ConsecutiveSettledSampleCount, 0);

  const FCrowdDemoParticleSettlingTracker Snapshot = Tracker;
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(Tracker, 0.0f, 4.0f);
  Tracker = Snapshot;
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(Tracker, 0.0f, 4.0f);
  TestEqual(TEXT("rollback replay advances exactly once"),
    Tracker.StepCount, Snapshot.StepCount + 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleHashContractTest,
  "CrowdDemo.SoftPressure.Particle.HashContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleHashContractTest::RunTest(const FString& Parameters)
{
  auto Environment = MakeOpenEnvironment();
  FCrowdDemoSharedFlowObstacleSpec Obstacle;
  Obstacle.ObstacleId = 7;
  Obstacle.Center = FVector(500, 600, 60);
  Obstacle.Extent = FVector(20, 30, 100);
  Environment.FlowConfig.ObstacleSpecs = {Obstacle};
  TArray<FCrowdDemoParticleConstraintAgent> Agents = {
    MakeAgent(1, FVector(-100, 0, 60), FVector(-90, 0, 60)),
    MakeAgent(2, FVector(100, 0, 60), FVector(90, 0, 60))};
  FCrowdDemoParticleConstraintSettings Settings;
  const auto SolveHash = [](TConstArrayView<FCrowdDemoParticleConstraintAgent> InputAgents,
    const FCrowdDemoParticleConstraintEnvironment& InputEnvironment,
    const FCrowdDemoParticleConstraintSettings& InputSettings)
  {
    TArray<FCrowdDemoParticleConstraintPair> Pairs;
    TArray<FCrowdDemoParticleConstraintResult> Results;
    FCrowdDemoParticleConstraintSummary Summary;
    FCrowdDemoParticleConstraintKernel::Solve(
      InputAgents, InputEnvironment, InputSettings, Pairs, Results, Summary);
    return Summary.CandidateHash;
  };
  const uint32 Base = SolveHash(Agents, Environment, Settings);
  TArray<FCrowdDemoParticleConstraintAgent> ReversedAgents = Agents;
  Algo::Reverse(ReversedAgents);
  auto ReversedEnvironment = Environment;
  Algo::Reverse(ReversedEnvironment.FlowConfig.ObstacleSpecs);
  TestEqual(TEXT("candidate hash ignores input order"),
    SolveHash(ReversedAgents, ReversedEnvironment, Settings), Base);
  auto ChangedSettings = Settings;
  ChangedSettings.PositionQuantumCm = 0.5f;
  TestNotEqual(TEXT("settings alter candidate hash"),
    SolveHash(Agents, Environment, ChangedSettings), Base);
  auto ChangedEnvironment = Environment;
  ChangedEnvironment.FlowConfig.BoundsMax.X += 1.0f;
  TestNotEqual(TEXT("environment alters candidate hash"),
    SolveHash(Agents, ChangedEnvironment, Settings), Base);
  ChangedEnvironment = Environment;
  ChangedEnvironment.FlowConfig.ObstacleSpecs[0].Extent.X += 1.0f;
  TestNotEqual(TEXT("obstacle alters candidate hash"),
    SolveHash(Agents, ChangedEnvironment, Settings), Base);
  auto ChangedAgents = Agents;
  ChangedAgents[0].SoftMarginCm += 1.0f;
  TestNotEqual(TEXT("agent contract alters candidate hash"),
    SolveHash(ChangedAgents, Environment, Settings), Base);

  TArray<FCrowdDemoParticleAppliedRoundSimState> States;
  for (const auto& Agent : Agents)
  {
    FCrowdDemoParticleAppliedRoundSimState& State = States.AddDefaulted_GetRef();
    State.AgentId = Agent.AgentId;
    State.LifecycleSerial = 10 + Agent.AgentId;
    State.Position = Agent.StartPosition;
    State.Velocity = FVector(3, 4, 0);
    State.YawDegrees = 45.0f;
    State.RadiusCm = Agent.PhysicalRadiusCm;
    State.bInitialized = true;
  }
  const uint32 AppliedBase = FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(
    1, 2, 3, 4.0f, States);
  Algo::Reverse(States);
  TestEqual(TEXT("applied hash ignores input order"),
    FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(1, 2, 3, 4.0f, States),
    AppliedBase);
  States[0].Velocity.Z += 1.0f;
  TestNotEqual(TEXT("full XYZ velocity alters applied hash"),
    FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(1, 2, 3, 4.0f, States),
    AppliedBase);
  TestNotEqual(TEXT("boundary context alters applied hash"),
    FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(1, 2, 4, 4.0f, States),
    FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(1, 2, 3, 4.0f, States));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoParticleCorrectionReplayTest,
  "CrowdDemo.SoftPressure.Particle.CorrectionReplay",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoParticleCorrectionReplayTest::RunTest(const FString& Parameters)
{
  const auto Environment = MakeOpenEnvironment();
  TArray<FCrowdDemoParticleConstraintAgent> Initial;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    const FVector Start((Index % 5) * 110.0f, (Index / 5) * 110.0f, 60.0f);
    Initial.Add(MakeAgent(Index, Start, Start));
  }
  auto Advance = [&Environment](TArray<FCrowdDemoParticleConstraintAgent>& Agents,
    FCrowdDemoParticleSettlingTracker& Tracker, TArray<uint32>& Samples,
    const int32 FirstStep, const int32 LastStep)
  {
    TArray<FCrowdDemoParticleConstraintResult> Results;
    for (int32 Step = FirstStep; Step <= LastStep; ++Step)
    {
      for (auto& Agent : Agents)
        Agent.PredictedPosition = Agent.StartPosition + FVector(2.0f, 0.0f, 0.0f);
      const auto Summary = SolveAgents(Agents, Environment, Results);
      if (!Summary.bValid) return false;
      Samples.Add(Summary.CandidateHash);
      FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
        Tracker, Summary.MaxAgentCorrectionCm, Summary.SoftErrorCmP95);
      for (int32 Index = 0; Index < Agents.Num(); ++Index)
        Agents[Index].StartPosition = Results[Index].CorrectedPosition;
    }
    return true;
  };

  TArray<FCrowdDemoParticleConstraintAgent> Control = Initial;
  FCrowdDemoParticleSettlingTracker ControlTracker;
  TArray<uint32> ControlSamples;
  TestTrue(TEXT("control prefix valid"),
    Advance(Control, ControlTracker, ControlSamples, 1, 3));
  const TArray<FCrowdDemoParticleConstraintAgent> BoundarySnapshot = Control;
  const FCrowdDemoParticleSettlingTracker TrackerSnapshot = ControlTracker;
  const int32 SampleLengthSnapshot = ControlSamples.Num();
  TestTrue(TEXT("control suffix valid"),
    Advance(Control, ControlTracker, ControlSamples, 4, 8));

  TArray<FCrowdDemoParticleConstraintAgent> Replayed = BoundarySnapshot;
  FCrowdDemoParticleSettlingTracker ReplayTracker = TrackerSnapshot;
  TArray<uint32> ReplaySamples = ControlSamples;
  ReplaySamples.SetNum(SampleLengthSnapshot);
  TestTrue(TEXT("replayed suffix valid"),
    Advance(Replayed, ReplayTracker, ReplaySamples, 4, 8));
  TestEqual(TEXT("replay sample length is not duplicated"),
    ReplaySamples.Num(), ControlSamples.Num());
  for (int32 Index = 0; Index < FMath::Min(ReplaySamples.Num(), ControlSamples.Num()); ++Index)
    TestEqual(TEXT("replay candidate hash"), ReplaySamples[Index], ControlSamples[Index]);
  TestEqual(TEXT("replay settling step count"), ReplayTracker.StepCount, ControlTracker.StepCount);
  TestEqual(TEXT("replay settling window"), ReplayTracker.ConsecutiveSettledSampleCount,
    ControlTracker.ConsecutiveSettledSampleCount);
  for (int32 Index = 0; Index < Control.Num(); ++Index)
  {
    TestEqual(TEXT("replay AgentId"), Replayed[Index].AgentId, Control[Index].AgentId);
    TestTrue(TEXT("replay final state"),
      Replayed[Index].StartPosition.Equals(Control[Index].StartPosition, 0.01f));
  }
  return true;
}

#endif
