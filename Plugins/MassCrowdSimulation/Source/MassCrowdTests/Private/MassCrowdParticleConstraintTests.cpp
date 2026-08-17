#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdParticleConstraintKernel.h"
#include "MassCrowdParticleWork.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdParticleConstraintAgent MakeAgent(
    const int32 AgentId,
    const FVector& Start,
    const FVector& Predicted)
  {
    FCrowdParticleConstraintAgent Agent;
    Agent.AgentId = AgentId;
    Agent.StartPosition = Start;
    Agent.PredictedPosition = Predicted;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.HardSafetyGapCm = 10.0f;
    Agent.SoftMarginCm = 17.0f;
    Agent.Mobility = 1.0f;
    return Agent;
  }

  FCrowdParticleConstraintEnvironment MakeOpenEnvironment()
  {
    FCrowdParticleConstraintEnvironment Environment;
    Environment.FlowConfig.BoundsMin = FVector(-2000, -2000, 0);
    Environment.FlowConfig.BoundsMax = FVector(2000, 2000, 0);
    Environment.FlowConfig.ObstacleSpecs.Reset();
    Environment.bConstrainToFlowBounds = true;
    return Environment;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleConstraintDeterminismTest,
  "MassCrowd.Core.ParticleConstraint.Determinism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleConstraintDeterminismTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdParticleConstraintAgent> Agents = {
    MakeAgent(1, FVector(-80, 0, 60), FVector(-20, 0, 60)),
    MakeAgent(2, FVector(80, 0, 60), FVector(20, 0, 60))};
  const FCrowdParticleConstraintEnvironment Environment = MakeOpenEnvironment();
  FCrowdParticleConstraintSettings Settings;
  TArray<FCrowdParticleConstraintPair> ForwardPairs;
  TArray<FCrowdParticleConstraintResult> ForwardResults;
  FCrowdParticleConstraintSummary ForwardSummary;
  FCrowdParticleConstraintKernel::Solve(
    Agents, Environment, Settings, ForwardPairs, ForwardResults,
    ForwardSummary);
  TestTrue(TEXT("pair solve valid"), ForwardSummary.bValid);
  TestEqual(TEXT("hard violation zero"),
    ForwardSummary.HardPairViolationCount, 0);
  TestEqual(TEXT("swept violation zero"),
    ForwardSummary.SweptPairViolationCount, 0);
  TestEqual(TEXT("obstacle violation zero"),
    ForwardSummary.ObstaclePenetrationCount, 0);
  TestEqual(TEXT("result count"), ForwardResults.Num(), 2);
  TestTrue(TEXT("final hard distance"),
    FVector::Dist2D(ForwardResults[0].CorrectedPosition,
      ForwardResults[1].CorrectedPosition) + 0.01f >= 94.0f);

  Algo::Reverse(Agents);
  TArray<FCrowdParticleConstraintPair> ReversePairs;
  TArray<FCrowdParticleConstraintResult> ReverseResults;
  FCrowdParticleConstraintSummary ReverseSummary;
  FCrowdParticleConstraintKernel::Solve(
    Agents, Environment, Settings, ReversePairs, ReverseResults,
    ReverseSummary);
  TestTrue(TEXT("reverse solve valid"), ReverseSummary.bValid);
  TestEqual(TEXT("reverse hash stable"),
    ReverseSummary.CandidateHash, ForwardSummary.CandidateHash);
  TestEqual(TEXT("reverse result count"),
    ReverseResults.Num(), ForwardResults.Num());
  for (int32 Index = 0; Index < ForwardResults.Num(); ++Index)
  {
    TestEqual(TEXT("reverse result agent"),
      ReverseResults[Index].AgentId, ForwardResults[Index].AgentId);
    TestTrue(TEXT("reverse result position"),
      ReverseResults[Index].CorrectedPosition.Equals(
        ForwardResults[Index].CorrectedPosition, 0.0f));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleConstraintDiagnosticHashIsolationTest,
  "MassCrowd.Core.ParticleConstraint.DiagnosticHashIsolation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleConstraintDiagnosticHashIsolationTest::RunTest(
  const FString& Parameters)
{
  const TArray<FCrowdParticleConstraintAgent> Agents = {
    MakeAgent(1, FVector(-100, 0, 60), FVector::ZeroVector),
    MakeAgent(2, FVector(100, 0, 60), FVector::ZeroVector)};
  const FCrowdParticleConstraintEnvironment Environment = MakeOpenEnvironment();

  FCrowdParticleConstraintSettings Settings;
  Settings.IterationCount = 1;
  Settings.SafetyIterationCount = 1;
  Settings.SoftResponsePerSecond = 0.0f;
  Settings.HardMaxPairCorrectionPerIterationCm = 0.0f;
  TArray<FCrowdParticleConstraintPair> PlainPairs;
  TArray<FCrowdParticleConstraintResult> PlainResults;
  FCrowdParticleConstraintSummary PlainSummary;
  FCrowdParticleConstraintKernel::Solve(
    Agents, Environment, Settings, PlainPairs, PlainResults, PlainSummary);

  Settings.bCaptureRouteDiagnostic = true;
  TArray<FCrowdParticleConstraintPair> DiagnosticPairs;
  TArray<FCrowdParticleConstraintResult> DiagnosticResults;
  FCrowdParticleConstraintSummary DiagnosticSummary;
  FCrowdParticleConstraintTrace Trace;
  FCrowdParticleConstraintKernel::Solve(
    Agents, Environment, Settings, DiagnosticPairs, DiagnosticResults,
    DiagnosticSummary, &Trace);

  TestEqual(TEXT("diagnostic capture preserves validity"),
    DiagnosticSummary.bValid, PlainSummary.bValid);
  TestEqual(TEXT("diagnostic capture preserves candidate hash"),
    DiagnosticSummary.CandidateHash, PlainSummary.CandidateHash);
  TestEqual(TEXT("diagnostic capture preserves result count"),
    DiagnosticResults.Num(), PlainResults.Num());
  for (int32 Index = 0; Index < PlainResults.Num(); ++Index)
  {
    TestEqual(TEXT("diagnostic capture preserves agent"),
      DiagnosticResults[Index].AgentId, PlainResults[Index].AgentId);
    TestTrue(TEXT("diagnostic capture preserves position"),
      DiagnosticResults[Index].CorrectedPosition.Equals(
        PlainResults[Index].CorrectedPosition, 0.0f));
    TestTrue(TEXT("diagnostic capture preserves velocity"),
      DiagnosticResults[Index].CorrectedVelocity.Equals(
        PlainResults[Index].CorrectedVelocity, 0.0f));
  }
  TestTrue(TEXT("diagnostic trace is populated"),
    Trace.AgentIds.Num() == Agents.Num());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleConstraintInheritedOverlapRecoveryTest,
  "MassCrowd.Core.ParticleConstraint.InheritedOverlapRecovery",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleConstraintInheritedOverlapRecoveryTest::RunTest(
  const FString& Parameters)
{
  const TArray<FCrowdParticleConstraintAgent> Agents = {
    MakeAgent(18, FVector(63, 1861, 60), FVector(67, 1858, 60)),
    MakeAgent(19, FVector(-26, 1831, 60), FVector(-30, 1827, 60))};
  TArray<FCrowdParticleAppliedState> RecoveringStates;
  for (const FCrowdParticleConstraintAgent& Agent : Agents)
  {
    FCrowdParticleAppliedState& State = RecoveringStates.AddDefaulted_GetRef();
    State.AgentId = Agent.AgentId;
    State.Position = Agent.PredictedPosition;
  }
  FCrowdParticleConstraintSummary RecoveringSummary;
  uint32 RecoveringHash = 0;
  FCrowdParticleConstraintKernel::EvaluateAppliedState(
    Agents, RecoveringStates, MakeOpenEnvironment(), RecoveringSummary,
    RecoveringHash);
  TestTrue(TEXT("inherited overlap can exit monotonically"),
    RecoveringSummary.bValid);
  TestEqual(TEXT("inherited overlap recovery has no swept violation"),
    RecoveringSummary.SweptPairViolationCount, 0);

  TArray<FCrowdParticleAppliedState> DeepeningStates = RecoveringStates;
  DeepeningStates[0].Position = FVector(62, 1861, 60);
  DeepeningStates[1].Position = FVector(-25, 1831, 60);
  FCrowdParticleConstraintSummary DeepeningSummary;
  uint32 DeepeningHash = 0;
  FCrowdParticleConstraintKernel::EvaluateAppliedState(
    Agents, DeepeningStates, MakeOpenEnvironment(), DeepeningSummary,
    DeepeningHash);
  TestFalse(TEXT("inherited overlap cannot deepen"), DeepeningSummary.bValid);
  TestTrue(TEXT("deepening path reports swept violation"),
    DeepeningSummary.SweptPairViolationCount > 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleConstraintQuantizedSweptClosureRegressionTest,
  "MassCrowd.Core.ParticleConstraint.QuantizedSweptClosureRegression",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleConstraintQuantizedSweptClosureRegressionTest::RunTest(
  const FString& Parameters)
{
  // Exact solve input captured from T5 MovingTarget fixed step 314.  The
  // pre-fix lattice fallback returned endpoint-safe positions whose swept
  // paths for agents 8/17 passed within 89.087 cm instead of the required
  // 94 cm.
  TArray<FCrowdParticleConstraintAgent> Agents = {
    MakeAgent(-1000000001, FVector(-835, 1900, 60), FVector(-837, 1900, 60)),
    MakeAgent(0, FVector(878, 849, 60), FVector(851.367f, 849.900f, 60)),
    MakeAgent(1, FVector(1008, 827, 60), FVector(983.000f, 836.200f, 60)),
    MakeAgent(2, FVector(536, 953, 60), FVector(509.433f, 950.933f, 60)),
    MakeAgent(3, FVector(668, 977, 60), FVector(648.833f, 995.533f, 60)),
    MakeAgent(4, FVector(-15, 1166, 60), FVector(-32.600f, 1183.567f, 60)),
    MakeAgent(5, FVector(326, 1119, 60), FVector(307.767f, 1138.433f, 60)),
    MakeAgent(6, FVector(-300, 1452, 60), FVector(-315.233f, 1467.767f, 60)),
    MakeAgent(7, FVector(-461, 1544, 60), FVector(-479.267f, 1535.800f, 60)),
    MakeAgent(8, FVector(-606, 1662, 60), FVector(-622.933f, 1664.500f, 60)),
    MakeAgent(9, FVector(-726, 1814, 60), FVector(-738.333f, 1824.367f, 60)),
    MakeAgent(10, FVector(1093, 736, 60), FVector(1068.233f, 745.867f, 60)),
    MakeAgent(11, FVector(758, 891, 60), FVector(738.367f, 909.033f, 60)),
    MakeAgent(12, FVector(435, 1049, 60), FVector(408.367f, 1049.700f, 60)),
    MakeAgent(13, FVector(114, 1032, 60), FVector(94.733f, 1050.433f, 60)),
    MakeAgent(14, FVector(-214, 1366, 60), FVector(-232.633f, 1381.300f, 60)),
    MakeAgent(15, FVector(-91, 1337, 60), FVector(-109.267f, 1354.767f, 60)),
    MakeAgent(16, FVector(-358, 1558, 60), FVector(-377.833f, 1563.600f, 60)),
    MakeAgent(17, FVector(-553, 1579, 60), FVector(-557.567f, 1593.500f, 60)),
    MakeAgent(18, FVector(-621, 1770, 60), FVector(-630.833f, 1780.933f, 60)),
    MakeAgent(19, FVector(-830, 1752, 60), FVector(-841.933f, 1754.633f, 60))};
  Agents[0].PhysicalRadiusCm = 100.0f;
  Agents[0].Mobility = 0.0f;

  FCrowdParticleConstraintEnvironment Environment = MakeOpenEnvironment();
  Environment.FlowConfig.BoundsMin = FVector(-5000, -5000, 0);
  Environment.FlowConfig.BoundsMax = FVector(5000, 5000, 0);
  TArray<FCrowdParticleConstraintPair> Pairs;
  TArray<FCrowdParticleConstraintResult> Results;
  FCrowdParticleConstraintSummary Summary;
  FCrowdParticleConstraintKernel::Solve(
    Agents, Environment, FCrowdParticleConstraintSettings(),
    Pairs, Results, Summary);

  TestTrue(TEXT("captured solve closes all hard constraints"), Summary.bValid);
  TestEqual(TEXT("captured solve has no endpoint violation"),
    Summary.HardPairViolationCount, 0);
  TestEqual(TEXT("captured solve has no swept violation"),
    Summary.SweptPairViolationCount, 0);
  TestEqual(TEXT("captured solve preserves membership"),
    Results.Num(), Agents.Num());

  // The first repair exposed a later frame where a fixed target begins in an
  // inherited overlap. A common progress scale cannot both evacuate that
  // overlap and avoid the independent 8/9 swept crossing, so preserve the
  // second complete fixture as a distinct regression.
  TArray<FCrowdParticleConstraintAgent> InheritedOverlapAgents = {
    MakeAgent(-1000000001, FVector(-851, 1900, 60), FVector(-853, 1900, 60)),
    MakeAgent(0, FVector(737, 901, 60), FVector(719.733f, 921.300f, 60)),
    MakeAgent(1, FVector(851, 850, 60), FVector(824.333f, 850.000f, 60)),
    MakeAgent(2, FVector(374, 950, 60), FVector(347.333f, 950.000f, 60)),
    MakeAgent(3, FVector(531, 1049, 60), FVector(504.367f, 1049.800f, 60)),
    MakeAgent(4, FVector(-122, 1269, 60), FVector(-133.233f, 1286.567f, 60)),
    MakeAgent(5, FVector(218, 1222, 60), FVector(202.567f, 1235.500f, 60)),
    MakeAgent(6, FVector(-403, 1501, 60), FVector(-415.633f, 1508.700f, 60)),
    MakeAgent(7, FVector(-538, 1545, 60), FVector(-550.400f, 1550.333f, 60)),
    MakeAgent(8, FVector(-697, 1686, 60), FVector(-708.600f, 1692.600f, 60)),
    MakeAgent(9, FVector(-792, 1855, 60), FVector(-803.867f, 1853.400f, 60)),
    MakeAgent(10, FVector(984, 837, 60), FVector(958.867f, 845.900f, 60)),
    MakeAgent(11, FVector(649, 992, 60), FVector(630.702f, 1011.398f, 60)),
    MakeAgent(12, FVector(312, 1129, 60), FVector(297.400f, 1141.100f, 60)),
    MakeAgent(13, FVector(8, 1135, 60), FVector(-7.900f, 1150.300f, 60)),
    MakeAgent(14, FVector(-304, 1452, 60), FVector(-316.233f, 1465.033f, 60)),
    MakeAgent(15, FVector(-216, 1355, 60), FVector(-234.500f, 1363.533f, 60)),
    MakeAgent(16, FVector(-453, 1618, 60), FVector(-465.167f, 1625.867f, 60)),
    MakeAgent(17, FVector(-633, 1593, 60), FVector(-649.733f, 1593.267f, 60)),
    MakeAgent(18, FVector(-665, 1819, 60), FVector(-665.967f, 1828.933f, 60)),
    MakeAgent(19, FVector(-883, 1763, 60), FVector(-895.000f, 1763.733f, 60))};
  InheritedOverlapAgents[0].PhysicalRadiusCm = 100.0f;
  InheritedOverlapAgents[0].Mobility = 0.0f;
  Pairs.Reset();
  Results.Reset();
  Summary = FCrowdParticleConstraintSummary();
  FCrowdParticleConstraintKernel::Solve(
    InheritedOverlapAgents, Environment, FCrowdParticleConstraintSettings(),
    Pairs, Results, Summary);
  TestTrue(TEXT("inherited-overlap fixture closes all hard constraints"),
    Summary.bValid);
  TestEqual(TEXT("inherited-overlap fixture has no endpoint violation"),
    Summary.HardPairViolationCount, 0);
  TestEqual(TEXT("inherited-overlap fixture has no swept violation"),
    Summary.SweptPairViolationCount, 0);

  TArray<FCrowdParticleConstraintAgent> RotatedDetourAgents = {
    MakeAgent(-1000000001, FVector(-1053, 1900, 60), FVector(-1056, 1900, 60)),
    MakeAgent(0, FVector(-709, 1629, 60), FVector(-715.967f, 1654.333f, 60)),
    MakeAgent(1, FVector(-595, 1646, 60), FVector(-608.000f, 1658.333f, 60)),
    MakeAgent(2, FVector(-840, 1576, 60), FVector(-852.833f, 1576.300f, 60)),
    MakeAgent(3, FVector(-858, 1766, 60), FVector(-875.933f, 1768.100f, 60)),
    MakeAgent(4, FVector(-986, 2018, 60), FVector(-996.533f, 2016.433f, 60)),
    MakeAgent(5, FVector(-954, 1818, 60), FVector(-968.933f, 1822.167f, 60)),
    MakeAgent(6, FVector(-1141, 1740, 60), FVector(-1142.967f, 1737.933f, 60)),
    MakeAgent(7, FVector(-1337, 1841, 60), FVector(-1350.333f, 1841.833f, 60)),
    MakeAgent(8, FVector(-1435, 1934, 60), FVector(-1440.333f, 1936.133f, 60)),
    MakeAgent(9, FVector(-1185, 2178, 60), FVector(-1185.667f, 2176.200f, 60)),
    MakeAgent(10, FVector(-597, 1520, 60), FVector(-605.500f, 1536.000f, 60)),
    MakeAgent(11, FVector(-779, 1705, 60), FVector(-793.500f, 1715.033f, 60)),
    MakeAgent(12, FVector(-951, 1521, 60), FVector(-963.133f, 1524.667f, 60)),
    MakeAgent(13, FVector(-1071, 1618, 60), FVector(-1080.000f, 1607.367f, 60)),
    MakeAgent(14, FVector(-1166, 1879, 60), FVector(-1176.933f, 1872.900f, 60)),
    MakeAgent(15, FVector(-1028, 1902, 60), FVector(-1042.567f, 1907.000f, 60)),
    MakeAgent(16, FVector(-1276, 1966, 60), FVector(-1287.267f, 1961.567f, 60)),
    MakeAgent(17, FVector(-1224, 1512, 60), FVector(-1228.233f, 1512.033f, 60)),
    MakeAgent(18, FVector(-1206, 2064, 60), FVector(-1217.200f, 2070.633f, 60)),
    MakeAgent(19, FVector(-1473, 2079, 60), FVector(-1477.533f, 2081.100f, 60))};
  RotatedDetourAgents[0].PhysicalRadiusCm = 100.0f;
  RotatedDetourAgents[0].Mobility = 0.0f;
  Pairs.Reset();
  Results.Reset();
  Summary = FCrowdParticleConstraintSummary();
  FCrowdParticleConstraintKernel::Solve(
    RotatedDetourAgents, Environment, FCrowdParticleConstraintSettings(),
    Pairs, Results, Summary);
  TestTrue(TEXT("rotated-detour fixture closes all hard constraints"),
    Summary.bValid);
  TestEqual(TEXT("rotated-detour fixture has no endpoint violation"),
    Summary.HardPairViolationCount, 0);
  TestEqual(TEXT("rotated-detour fixture has no swept violation"),
    Summary.SweptPairViolationCount, 0);

  TArray<FCrowdParticleConstraintAgent> DenseDetourAgents = {
    MakeAgent(-1000000001, FVector(-987, 1900, 60), FVector(-989, 1900, 60)),
    MakeAgent(0, FVector(-350, 1383, 60), FVector(-360.833f, 1405.567f, 60)),
    MakeAgent(1, FVector(-244, 1344, 60), FVector(-267.900f, 1344.367f, 60)),
    MakeAgent(2, FVector(-605, 1587, 60), FVector(-614.133f, 1588.633f, 60)),
    MakeAgent(3, FVector(-491, 1568, 60), FVector(-500.633f, 1581.100f, 60)),
    MakeAgent(4, FVector(-758, 1922, 60), FVector(-759.167f, 1925.733f, 60)),
    MakeAgent(5, FVector(-657, 1700, 60), FVector(-666.400f, 1700.667f, 60)),
    MakeAgent(6, FVector(-962, 1669, 60), FVector(-972.067f, 1672.000f, 60)),
    MakeAgent(7, FVector(-1087, 1743, 60), FVector(-1094.367f, 1750.367f, 60)),
    MakeAgent(8, FVector(-1238, 1860, 60), FVector(-1249.033f, 1859.433f, 60)),
    MakeAgent(9, FVector(-1100, 2074, 60), FVector(-1104.333f, 2075.733f, 60)),
    MakeAgent(10, FVector(-206, 1149, 60), FVector(-223.833f, 1167.967f, 60)),
    MakeAgent(11, FVector(-431, 1466, 60), FVector(-444.033f, 1479.367f, 60)),
    MakeAgent(12, FVector(-718, 1482, 60), FVector(-726.567f, 1485.000f, 60)),
    MakeAgent(13, FVector(-786, 1653, 60), FVector(-797.133f, 1653.700f, 60)),
    MakeAgent(14, FVector(-922, 1848, 60), FVector(-925.633f, 1856.400f, 60)),
    MakeAgent(15, FVector(-817, 1790, 60), FVector(-825.733f, 1793.067f, 60)),
    MakeAgent(16, FVector(-1077, 1872, 60), FVector(-1089.200f, 1876.867f, 60)),
    MakeAgent(17, FVector(-1102, 1562, 60), FVector(-1101.667f, 1560.433f, 60)),
    MakeAgent(18, FVector(-1007, 1977, 60), FVector(-1013.033f, 1978.200f, 60)),
    MakeAgent(19, FVector(-1370, 1943, 60), FVector(-1365.467f, 1952.733f, 60))};
  DenseDetourAgents[0].PhysicalRadiusCm = 100.0f;
  DenseDetourAgents[0].Mobility = 0.0f;
  Pairs.Reset();
  Results.Reset();
  Summary = FCrowdParticleConstraintSummary();
  FCrowdParticleConstraintKernel::Solve(
    DenseDetourAgents, Environment, FCrowdParticleConstraintSettings(),
    Pairs, Results, Summary);
  TestTrue(TEXT("dense-detour fixture closes all hard constraints"),
    Summary.bValid);
  TestEqual(TEXT("dense-detour fixture has no endpoint violation"),
    Summary.HardPairViolationCount, 0);
  TestEqual(TEXT("dense-detour fixture has no swept violation"),
    Summary.SweptPairViolationCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleConstraintInteractionLayerTest,
  "MassCrowd.Core.ParticleConstraint.InteractionLayer",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleConstraintInteractionLayerTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdParticleConstraintAgent> Agents = {
    MakeAgent(1, FVector(-80, 0, 60), FVector::ZeroVector),
    MakeAgent(2, FVector(80, 0, 460), FVector::ZeroVector)};
  Agents[0].InteractionLayer = 3;
  Agents[1].InteractionLayer = 4;
  TArray<FCrowdParticleConstraintPair> Pairs;
  TArray<FCrowdParticleConstraintResult> Results;
  FCrowdParticleConstraintSummary Summary;
  FCrowdParticleConstraintSettings Settings;
  FCrowdParticleConstraintKernel::Solve(
    Agents, MakeOpenEnvironment(), Settings,
    Pairs, Results, Summary);
  TestTrue(TEXT("layered solve valid"), Summary.bValid);
  TestEqual(TEXT("different layers do not generate pairs"),
    Pairs.Num(), 0);
  TestEqual(TEXT("layered result count"), Results.Num(), 2);
  TestTrue(TEXT("first prediction preserved"),
    Results[0].CorrectedPosition.Equals(
      Agents[0].PredictedPosition, 0.0f));
  TestTrue(TEXT("second prediction preserved"),
    Results[1].CorrectedPosition.Equals(
      Agents[1].PredictedPosition, 0.0f));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleSettlingTrackerTest,
  "MassCrowd.Core.ParticleConstraint.SettlingTracker",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleSettlingTrackerTest::RunTest(
  const FString& Parameters)
{
  FCrowdParticleSettlingTracker Tracker;
  for (int32 Step = 0; Step < 16; ++Step)
    FCrowdParticleConstraintKernel::AdvanceSettlingTracker(
      Tracker, 1.0f, 12.0f);
  TestEqual(TEXT("tracker step count"), Tracker.StepCount, 16);
  TestEqual(TEXT("fifteen comparable settled samples"),
    Tracker.ConsecutiveSettledSampleCount, 15);
  TestEqual(TEXT("settling begins at second sample"),
    Tracker.SettlingSteps, 2);
  FCrowdParticleConstraintKernel::AdvanceSettlingTracker(
    Tracker, 1.01f, 12.0f);
  TestEqual(TEXT("excess correction resets window"),
    Tracker.ConsecutiveSettledSampleCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdParticleClosedIslandShardTest,
  "MassCrowd.Runtime.ParticleWork.ClosedIslandStableMerge",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdParticleClosedIslandShardTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdMassParticleWorkInput Input;
  Input.FixedStepIndex = 1;
  Input.PlanRevision = 1;
  Input.Environment = MakeOpenEnvironment();
  Input.Agents = {
    MakeAgent(1, FVector(-1050, 0, 0), FVector(-1030, 0, 0)),
    MakeAgent(2, FVector(-1000, 0, 0), FVector(-1000, 0, 0)),
    MakeAgent(3, FVector(-950, 0, 0), FVector(-970, 0, 0)),
    MakeAgent(4, FVector(950, 0, 0), FVector(970, 0, 0)),
    MakeAgent(5, FVector(1000, 0, 0), FVector(1000, 0, 0)),
    MakeAgent(6, FVector(1050, 0, 0), FVector(1030, 0, 0))};
  const FCrowdMassParticleWorkOutput Forward =
    FCrowdMassParticleWork::Solve(Input);
  TestTrue(TEXT("closed island work completes"),
    Forward.bCompleted);
  TestTrue(TEXT("closed island work is valid"),
    Forward.Summary.bValid && Forward.AppliedSummary.bValid);
  TestTrue(TEXT("closed island sharding is used"),
    Forward.bUsedIslandSharding);
  TestFalse(TEXT("closed island needs no monolithic fallback"),
    Forward.bUsedMonolithicFallback);
  TestEqual(TEXT("two conservative interaction islands"),
    Forward.InteractionIslandCount, 2);
  TestEqual(TEXT("largest island has three agents"),
    Forward.MaxIslandAgentCount, 3);
  TestEqual(TEXT("all island results merge"),
    Forward.Results.Num(), 6);
  TestTrue(TEXT("cell ownership has multiple shards"),
    Forward.CellShardCount >= 2);
  Algo::Reverse(Input.Agents);
  const FCrowdMassParticleWorkOutput Reverse =
    FCrowdMassParticleWork::Solve(Input);
  TestTrue(TEXT("reverse closed island work completes"),
    Reverse.bCompleted);
  TestEqual(TEXT("island stable merge ignores input order"),
    Reverse.StableHash, Forward.StableHash);
  TestEqual(TEXT("island count ignores input order"),
    Reverse.InteractionIslandCount,
    Forward.InteractionIslandCount);
  return true;
}

#endif
