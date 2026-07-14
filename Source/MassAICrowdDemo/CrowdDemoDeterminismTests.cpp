#if WITH_DEV_AUTOMATION_TESTS

#include "CrowdDemoScenarioRegistry.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoHardSeparationPbdKernel.h"
#include "Mass/CrowdDemoElasticCrowdKernel.h"
#include "Mass/CrowdDemoElasticShadowKernel.h"
#include "Mass/CrowdDemoJointVelocityKernel.h"
#include "Mass/CrowdDemoRoundCheckpointTransport.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoRoundSimProcessors.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4HoldingMatchingTest,
  "CrowdDemo.SF4.HoldingMatching.MinCostMaxFlow",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4HoldingMatchingTest::RunTest(const FString& Parameters)
{
  FCrowdDemoHoldingMatchingInput Input; Input.TargetRevision = 7;
  for (int32 H = 1; H <= 2; ++H)
  { auto& C=Input.Holdings.AddDefaulted_GetRef();C.HoldingId=H;C.TargetRevision=7;C.bReachable=C.bClearanceValid=true; }
  auto AddAgent=[&](int32 Id,int32 Position){auto&A=Input.Agents.AddDefaulted_GetRef();A.AgentId=Id;
    A.PositionId=Position;A.ExistingTargetRevision=7;A.bPositionValid=true;return &A;};
  AddAgent(10,100);AddAgent(20,200);
  auto AddEdge=[&](int32 H,int32 P,int32 Cost){auto&E=Input.Compatibility.AddDefaulted_GetRef();
    E.HoldingId=H;E.PositionId=P;E.QuantizedRouteCostCm=Cost;E.bCompatible=true;E.StableHash=H*1000+P;};
  AddEdge(1,100,1);AddEdge(2,100,2);AddEdge(1,200,1);
  FCrowdDemoHoldingMatchingResult Result;
  FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Input,Result);
  TestTrue(TEXT("matching result valid"),Result.bValid);
  TestEqual(TEXT("augmenting path fixes greedy 1/2"),Result.MaximumCardinality,2);
  TestTrue(TEXT("A moves to H2 for B"),Result.Assignments.ContainsByPredicate([](const auto&A)
    {return A.AgentId==10&&A.HoldingId==2;}));
  TestTrue(TEXT("B receives H1"),Result.Assignments.ContainsByPredicate([](const auto&A)
    {return A.AgentId==20&&A.HoldingId==1;}));
  const uint32 Hash=Result.MatchingHash;Algo::Reverse(Input.Agents);Algo::Reverse(Input.Holdings);
  Algo::Reverse(Input.Compatibility);FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Input,Result);
  TestEqual(TEXT("input reversal preserves hash"),Result.MatchingHash,Hash);
  auto* Hard=Input.Agents.FindByPredicate([](const auto&A){return A.AgentId==20;});
  Hard->ExistingState=ECrowdDemoPursuitSteeringState::Commit;Hard->ExistingHoldingId=1;
  Hard->bExistingOwnerHardValid=true;FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Input,Result);
  TestTrue(TEXT("commit owner hard locked"),Result.Assignments.ContainsByPredicate([](const auto&A)
    {return A.AgentId==20&&A.HoldingId==1&&A.bReused;}));
  for (const ECrowdDemoPursuitSteeringState HardState :
    {ECrowdDemoPursuitSteeringState::StableOccupied,
     ECrowdDemoPursuitSteeringState::ReserveHold})
  {
    Hard->ExistingState=HardState;
    FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Input,Result);
    TestTrue(TEXT("completed owner hard locked"),Result.Assignments.ContainsByPredicate(
      [&](const auto&A){return A.AgentId==20&&A.HoldingId==1&&A.State==HardState;}));
  }
  Hard->ExistingState=ECrowdDemoPursuitSteeringState::Holding;
  FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Input,Result);
  TestTrue(TEXT("soft owner retained at equal cardinality"),Result.Assignments.ContainsByPredicate(
    [](const auto&A){return A.AgentId==20&&A.HoldingId==1&&A.bReused;}));
  FCrowdDemoHoldingMatchingInput Fair;Fair.TargetRevision=7;
  auto& Only=Fair.Holdings.AddDefaulted_GetRef();Only.HoldingId=1;Only.TargetRevision=7;
  Only.bReachable=Only.bClearanceValid=true;
  auto& Low=Fair.Agents.AddDefaulted_GetRef();Low.AgentId=1;Low.PositionId=10;Low.WaitEpoch=1;
  Low.ExistingTargetRevision=7;Low.bPositionValid=true;
  auto& High=Fair.Agents.AddDefaulted_GetRef();High.AgentId=2;High.PositionId=20;High.WaitEpoch=9;
  High.ExistingTargetRevision=7;High.bPositionValid=true;
  auto& LE=Fair.Compatibility.AddDefaulted_GetRef();LE.HoldingId=1;LE.PositionId=10;
  LE.QuantizedRouteCostCm=1;LE.bCompatible=true;LE.StableHash=10;
  auto& HE=Fair.Compatibility.AddDefaulted_GetRef();HE.HoldingId=1;HE.PositionId=20;
  HE.QuantizedRouteCostCm=100;HE.bCompatible=true;HE.StableHash=20;
  FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Fair,Result);
  TestTrue(TEXT("WaitEpoch wins when cardinality is partial"),Result.Assignments.ContainsByPredicate(
    [](const auto&A){return A.AgentId==2&&A.HoldingId==1;}));
  Hard->ExistingState=ECrowdDemoPursuitSteeringState::Commit;
  Hard->ExistingTargetRevision=6;
  FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Input,Result);
  TestTrue(TEXT("stale hard owner releases to reacquire"),Result.Assignments.ContainsByPredicate(
    [](const auto&A){return A.AgentId==20&&A.State==ECrowdDemoPursuitSteeringState::Reacquire;}));
  Input.Agents.RemoveAll([](const auto&A){return A.AgentId==20;});
  FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(Input,Result);
  TestFalse(TEXT("membership removal leaves no owner ghost"),Result.Assignments.ContainsByPredicate(
    [](const auto&A){return A.AgentId==20;}));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4HoldingHallDeficiencyTest,
  "CrowdDemo.SF4.HoldingHall.MinimalDeficiency",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4HoldingHallDeficiencyTest::RunTest(const FString& Parameters)
{
  FCrowdDemoHoldingMatchingInput Input; Input.TargetRevision = 4;
  for (const int32 HoldingId : {10, 20})
  {
    auto& H = Input.Holdings.AddDefaulted_GetRef(); H.HoldingId = HoldingId;
    H.TargetRevision = 4; H.bReachable = H.bClearanceValid = true;
  }
  for (int32 Index = 0; Index < 3; ++Index)
  {
    auto& A = Input.Agents.AddDefaulted_GetRef(); A.AgentId = Index + 1;
    A.PositionId = 100 + Index; A.ExistingTargetRevision = 4; A.bPositionValid = true;
  }
  TArray<FCrowdDemoHoldingHallEdge> Edges;
  const auto Add = [&](const int32 AgentId, const int32 PositionId,
    const int32 HoldingId, const bool bCompatible, const int32 RejectKind)
  {
    auto& E = Edges.AddDefaulted_GetRef(); E.AgentId = AgentId;
    E.PositionId = PositionId; E.HoldingId = HoldingId;
    E.bCompatibilityRecordPresent = true;
    E.bFlowClear = E.bTargetClear = E.bObstacleClear = E.bRevisionValid = true;
    if (RejectKind == 1) E.bFlowClear = false;
    if (RejectKind == 2) E.bTargetClear = false;
    if (RejectKind == 3) E.bObstacleClear = false;
    E.bCompatible = bCompatible;
  };
  Add(1, 100, 10, true, 0); Add(1, 100, 20, false, 1);
  Add(2, 101, 10, true, 0); Add(2, 101, 20, false, 2);
  Add(3, 102, 10, false, 3); Add(3, 102, 20, true, 0);
  FCrowdDemoHoldingHallFixture Forward;
  FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallDeficiency(Input, Edges, Forward);
  TestTrue(TEXT("Hall fixture is exact and valid"), Forward.Summary.bExact && Forward.Summary.bValid);
  TestEqual(TEXT("Graph maximum matching is two"), Forward.Summary.CurrentMatchingCount, 2);
  TestEqual(TEXT("Minimum Hall set has two agents"), Forward.Summary.HallAgentCount, 2);
  TestEqual(TEXT("Minimum Hall set has one neighbor"), Forward.Summary.HallAvailableHoldingCount, 1);
  TestEqual(TEXT("Hall deficiency is one"), Forward.Summary.HallDeficiency, 1);
  TestTrue(TEXT("Lexicographic deficient set is agents 1,2"),
    Forward.AgentIds == TArray<int32>({1, 2}));
  TestTrue(TEXT("All usable Holdings are listed"),
    Forward.AvailableHoldingIds == TArray<int32>({10}));
  TestEqual(TEXT("Flow rejected missing edge classified"), Forward.Summary.FlowRejectCount, 1);
  TestEqual(TEXT("Target rejected missing edge classified"), Forward.Summary.TargetRejectCount, 1);
  const uint32 Hash = Forward.Summary.StableHash;
  Algo::Reverse(Input.Agents); Algo::Reverse(Input.Holdings); Algo::Reverse(Edges);
  FCrowdDemoHoldingHallFixture Reverse;
  FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallDeficiency(Input, Edges, Reverse);
  TestEqual(TEXT("Reversal preserves Hall hash"), Reverse.Summary.StableHash, Hash);
  TestTrue(TEXT("Reversal preserves Hall Agent set"), Reverse.AgentIds == Forward.AgentIds);

  for (const ECrowdDemoPursuitSteeringState HardState :
    {ECrowdDemoPursuitSteeringState::StableOccupied,
     ECrowdDemoPursuitSteeringState::ReserveHold,
     ECrowdDemoPursuitSteeringState::Commit})
  {
    FCrowdDemoHoldingMatchingInput OwnerInput; OwnerInput.TargetRevision = 9;
    for (const int32 HoldingId : {1, 2})
    { auto& H = OwnerInput.Holdings.AddDefaulted_GetRef(); H.HoldingId = HoldingId; H.TargetRevision = 9; }
    auto& Owner = OwnerInput.Agents.AddDefaulted_GetRef(); Owner.AgentId = 1;
    Owner.PositionId = 11; Owner.ExistingHoldingId = 1; Owner.ExistingTargetRevision = 9;
    Owner.ExistingState = HardState; Owner.bPositionValid = Owner.bExistingOwnerHardValid = true;
    auto& Waiting = OwnerInput.Agents.AddDefaulted_GetRef(); Waiting.AgentId = 2;
    Waiting.PositionId = 22; Waiting.ExistingTargetRevision = 9; Waiting.bPositionValid = true;
    TArray<FCrowdDemoHoldingHallEdge> OwnerEdges;
    const auto OwnerEdge = [&](const int32 AgentId, const int32 PositionId, const int32 HoldingId)
    { auto& E = OwnerEdges.AddDefaulted_GetRef(); E.AgentId = AgentId; E.PositionId = PositionId;
      E.HoldingId = HoldingId; E.bCompatibilityRecordPresent = E.bFlowClear = E.bTargetClear
        = E.bObstacleClear = E.bRevisionValid = E.bCompatible = true; };
    OwnerEdge(1, 11, 1); OwnerEdge(1, 11, 2); OwnerEdge(2, 22, 1);
    FCrowdDemoHoldingHallFixture OwnerFixture;
    FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallDeficiency(
      OwnerInput, OwnerEdges, OwnerFixture);
    TestEqual(TEXT("Hard owner keeps current cardinality at one"),
      OwnerFixture.Summary.CurrentMatchingCount, 1);
    const int32 Released = HardState == ECrowdDemoPursuitSteeringState::StableOccupied
      ? OwnerFixture.Summary.NoStableOwnerMatchingCount
      : HardState == ECrowdDemoPursuitSteeringState::ReserveHold
        ? OwnerFixture.Summary.NoReserveOwnerMatchingCount
        : OwnerFixture.Summary.NoCommitOwnerMatchingCount;
    TestEqual(TEXT("Releasing selected owner class restores two"), Released, 2);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4HallGeometryTest,
  "CrowdDemo.SF4.HoldingHall.Geometry",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4HallGeometryTest::RunTest(const FString& Parameters)
{
  FCrowdDemoPursuitTargetFact Target; Target.TargetId = 1; Target.Revision = 7;
  Target.Location = FVector2f(500.0f, 500.0f); Target.RadiusCm = 20.0f;
  FCrowdDemoPursuitPositioningSettings Settings; Settings.SafetyGapCm = 0.0f;
  FCrowdDemoPositionCandidate Position; Position.PositionId = 100;
  Position.TargetId = 1; Position.WorldLocation = FVector2f(100.0f, 0.0f);
  FCrowdDemoHoldingCandidate Holding; Holding.HoldingId = 10; Holding.TargetId = 1;
  Holding.TargetRevision = 7; Holding.WorldLocation = FVector2f(0.0f, 0.0f);
  FCrowdDemoHoldingPositionCompatibility Formal; Formal.HoldingId = 10;
  Formal.PositionId = 100; Formal.bFlowReachable = Formal.bTargetClear
    = Formal.bObstacleClear = Formal.bStableBlockerClear = Formal.bCompatible = true;
  const auto Analyze = [&](TArray<FCrowdDemoPositionIngressBlocker> Blockers,
    const bool bFormalClear)
  {
    Formal.bStableBlockerClear = bFormalClear;
    Formal.bCompatible = bFormalClear;
    FCrowdDemoHallGeometryFixture Fixture;
    FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallGeometry(
      5, Position, 10.0f, Target, Settings, MakeArrayView(&Holding, 1),
      Blockers, MakeArrayView(&Formal, 1), Fixture);
    return Fixture;
  };
  FCrowdDemoPositionIngressBlocker Safe; Safe.AgentId = 2; Safe.PositionId = 200;
  Safe.TargetRevision = 7; Safe.State = ECrowdDemoPursuitPositionState::StableOccupied;
  Safe.Location = FVector2f(50.0f, 25.0f); Safe.RadiusCm = 10.0f;
  const FCrowdDemoHallGeometryFixture SafeFixture = Analyze({Safe}, true);
  TestTrue(TEXT("Safe segment-circle margin is positive"),
    SafeFixture.BestClearanceMarginCm > 0.0f);
  TestEqual(TEXT("Safe Holding remains geometrically available"),
    SafeFixture.NonNegativeMarginHoldingCount, 1);
  TestEqual(TEXT("Safe formal and margin classifications agree"),
    SafeFixture.FormalClassificationMismatchCount, 0);
  FCrowdDemoPositionIngressBlocker Intersect = Safe; Intersect.Location = FVector2f(50.0f, 15.0f);
  const FCrowdDemoHallGeometryFixture IntersectFixture = Analyze({Intersect}, false);
  TestTrue(TEXT("Intersecting segment-circle margin is negative"),
    IntersectFixture.BestClearanceMarginCm < 0.0f);
  TestEqual(TEXT("Intersecting Holding is rejected"),
    IntersectFixture.NonNegativeMarginHoldingCount, 0);
  TestEqual(TEXT("Intersect formal and margin classifications agree"),
    IntersectFixture.FormalClassificationMismatchCount, 0);
  FCrowdDemoPositionIngressBlocker Tangent = Safe; Tangent.Location = FVector2f(50.0f, 20.0f);
  const FCrowdDemoHallGeometryFixture TangentFixture = Analyze({Tangent}, false);
  TestTrue(TEXT("Exact tangent uses nonnegative margin rule"),
    TangentFixture.BestClearanceMarginCm >= -0.001f);
  TestEqual(TEXT("Exact tangent exposes old inclusive formal rule"),
    TangentFixture.FormalClassificationMismatchCount, 1);
  FCrowdDemoPositionIngressBlocker Endpoint = Intersect; Endpoint.Location = FVector2f(-5.0f, 0.0f);
  const FCrowdDemoHallGeometryFixture EndpointFixture = Analyze({Endpoint}, false);
  TestTrue(TEXT("Start endpoint collision is classified"),
    EndpointFixture.BestFact.bEndpointContact && EndpointFixture.BestClearanceMarginCm < 0.0f);
  Endpoint.Location = FVector2f(105.0f, 0.0f);
  const FCrowdDemoHallGeometryFixture EndFixture = Analyze({Endpoint}, false);
  TestTrue(TEXT("End endpoint collision is classified"),
    EndFixture.BestFact.bEndpointContact && EndFixture.BestClearanceMarginCm < 0.0f);
  FCrowdDemoPositionIngressBlocker Self = Safe; Self.AgentId = 5;
  FCrowdDemoPositionIngressBlocker Stale = Safe; Stale.AgentId = 3; Stale.TargetRevision = 6;
  TArray<FCrowdDemoPositionIngressBlocker> Audit = {Safe, Safe, Self, Stale};
  const FCrowdDemoHallGeometryFixture AuditForward = Analyze(Audit, true);
  TestEqual(TEXT("Self blocker is detected and excluded"), AuditForward.SelfBlockerCount, 1);
  TestEqual(TEXT("Duplicate blocker is detected"), AuditForward.DuplicateBlockerCount, 1);
  TestEqual(TEXT("Stale blocker is detected and excluded"), AuditForward.StaleBlockerCount, 1);
  Algo::Reverse(Audit);
  const FCrowdDemoHallGeometryFixture AuditReverse = Analyze(Audit, true);
  TestEqual(TEXT("Input reversal preserves geometry fixture hash"),
    AuditReverse.FixtureHash, AuditForward.FixtureHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4JointPositioningTest,
  "CrowdDemo.SF4.HoldingHall.JointPositioning",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4JointPositioningTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoJointPositioningAgent> Agents;
  for (int32 Id=1; Id<=2; ++Id)
  { auto& A=Agents.AddDefaulted_GetRef();A.AgentId=Id;A.TargetRevision=3;
    A.ExistingHoldingId=Id==1?10:INDEX_NONE;A.ExistingPositionId=Id==1?100:200; }
  TArray<FCrowdDemoHoldingCandidate> Holdings;
  for (const int32 Id : {10,20}) {auto&H=Holdings.AddDefaulted_GetRef();H.HoldingId=Id;H.TargetRevision=3;
    H.WorldLocation=FVector2f(Id==10?900.0f:1900.0f,0.0f);}
  TArray<FCrowdDemoPositionCandidate> Positions;
  for (const int32 Id : {100,200}) {auto&P=Positions.AddDefaulted_GetRef();P.PositionId=Id;
    P.WorldLocation=FVector2f(Id==100?1000.0f:2000.0f,0.0f);}
  TArray<FCrowdDemoJointAgentHoldingEdge> AH;
  const auto AddAH=[&](int32 A,int32 H,int32 C){auto&E=AH.AddDefaulted_GetRef();E.AgentId=A;
    E.HoldingId=H;E.QuantizedCurrentToHoldingCostCm=C;E.bLocallyReachable=true;};
  AddAH(1,10,1);AddAH(1,20,2);AddAH(2,10,1);
  TArray<FCrowdDemoHoldingPositionCompatibility> HP;
  const auto AddHP=[&](int32 H,int32 P,int32 C){auto&E=HP.AddDefaulted_GetRef();E.HoldingId=H;
    E.PositionId=P;E.QuantizedRouteCostCm=C;E.bCompatible=true;};
  AddHP(10,100,1);AddHP(10,200,1);AddHP(20,100,1);
  FCrowdDemoJointPositioningResult Forward;
  FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
    3,Agents,Holdings,Positions,AH,HP,Forward);
  TestTrue(TEXT("Joint planner valid"),Forward.bValid);
  TestEqual(TEXT("Joint planner reaches full cardinality"),Forward.MaximumCardinality,2);
  TestEqual(TEXT("Joint planner keeps Holding uniqueness"),Forward.DuplicateHoldingCount,0);
  TestEqual(TEXT("Joint planner keeps Position uniqueness"),Forward.DuplicatePositionCount,0);
  const uint32 Hash=Forward.StableHash;
  Algo::Reverse(Agents);Algo::Reverse(Holdings);Algo::Reverse(Positions);Algo::Reverse(AH);Algo::Reverse(HP);
  FCrowdDemoJointPositioningResult Reverse;
  FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
    3,Agents,Holdings,Positions,AH,HP,Reverse);
  TestEqual(TEXT("Joint input reversal preserves hash"),Reverse.StableHash,Hash);
  FCrowdDemoPursuitPositioningSettings ResidualSettings;ResidualSettings.SafetyGapCm=10.0f;
  FCrowdDemoJointCommitResidualResult Residual;
  FCrowdDemoPursuitPositioningKernel::EvaluateJointCommitResidualProtection(
    3,ResidualSettings,Agents,Holdings,Positions,AH,HP,Reverse,Residual);
  TestTrue(TEXT("Commit residual fixture valid"),Residual.bValid);
  TestEqual(TEXT("Only capacity-preserving grant is feasible"),Residual.FeasibleCount,1);
  TestEqual(TEXT("Future Stable blocker rejects capacity-losing grant"),Residual.InfeasibleCount,1);
  const uint32 ResidualHash=Residual.StableHash;
  Algo::Reverse(Agents);Algo::Reverse(Holdings);Algo::Reverse(Positions);Algo::Reverse(AH);Algo::Reverse(HP);
  FCrowdDemoJointPositioningResult Replanned;
  FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
    3,Agents,Holdings,Positions,AH,HP,Replanned);
  FCrowdDemoJointCommitResidualResult ResidualReverse;
  FCrowdDemoPursuitPositioningKernel::EvaluateJointCommitResidualProtection(
    3,ResidualSettings,Agents,Holdings,Positions,AH,HP,Replanned,ResidualReverse);
  TestEqual(TEXT("Commit residual input reversal preserves hash"),
    ResidualReverse.StableHash,ResidualHash);
  FCrowdDemoCommitGateResult Gate;Gate.ReadyGrantedCount=2;Gate.GrantedAgentIds={1,2};
  for(const int32 Id:{1,2}){auto&D=Gate.Decisions.AddDefaulted_GetRef();D.AgentId=Id;
    D.Decision=ECrowdDemoCommitDecision::Granted;}
  FCrowdDemoJointCommitResidualResult Sequential;
  FCrowdDemoPursuitPositioningKernel::ApplyJointResidualCommitGate(
    3,ResidualSettings,Agents,Holdings,Positions,AH,HP,Replanned,Gate,Sequential);
  TestEqual(TEXT("Sequential gate keeps only capacity-safe grant"),Gate.GrantedAgentIds.Num(),1);
  TestEqual(TEXT("Sequential gate reports one held capacity loss"),Sequential.InfeasibleCount,1);
  auto* Hard=Agents.FindByPredicate([](const auto&A){return A.AgentId==1;});
  Hard->State=ECrowdDemoPursuitSteeringState::Commit;Hard->bExistingHardOwnerValid=true;
  FCrowdDemoJointPositioningResult Locked;
  FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
    3,Agents,Holdings,Positions,AH,HP,Locked);
  TestTrue(TEXT("Commit Holding and Position stay hard locked"),
    Locked.Assignments.ContainsByPredicate([](const auto&A)
      {return A.AgentId==1&&A.HoldingId==10&&A.PositionId==100&&A.bHardLocked;}));
  TArray<FCrowdDemoJointPositioningAgent> ComboAgents;
  for(int32 Id=1;Id<=2;++Id){auto&A=ComboAgents.AddDefaulted_GetRef();A.AgentId=Id;
    A.TargetRevision=3;A.ExistingHoldingId=10;A.ExistingPositionId=Id==1?100:200;}
  TArray<FCrowdDemoJointAgentHoldingEdge> ComboAH;
  for(int32 A=1;A<=2;++A)for(const int32 H:{10,20}){auto&E=ComboAH.AddDefaulted_GetRef();
    E.AgentId=A;E.HoldingId=H;E.QuantizedCurrentToHoldingCostCm=1;E.bLocallyReachable=true;}
  TArray<FCrowdDemoHoldingPositionCompatibility> ComboHP;
  for(const int32 H:{10,20})for(const int32 P:{100,200}){auto&E=ComboHP.AddDefaulted_GetRef();
    E.HoldingId=H;E.PositionId=P;E.QuantizedRouteCostCm=1;E.bCompatible=true;}
  FCrowdDemoJointPositioningResult ComboResult;
  FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
    3,ComboAgents,Holdings,Positions,ComboAH,ComboHP,ComboResult);
  TestEqual(TEXT("Strict solver retains maximum feasible complete combos"),
    ComboResult.ReusedCombinationCount,1);
  TestEqual(TEXT("Strict combo objective preserves full cardinality"),
    ComboResult.MaximumCardinality,2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4ResidualCapacityDeterminismTest,
  "CrowdDemo.SF4.ResidualCapacity.DeterministicMatching",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4ResidualCapacityDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoResidualPositioningAgent> Agents;
  for (int32 Id = 1; Id <= 3; ++Id)
  {
    auto& A = Agents.AddDefaulted_GetRef(); A.AgentId = Id; A.PositionId = Id;
    A.HoldingId = 100 + Id; A.TargetRevision = 7; A.bHasHolding = true;
  }
  const TArray<int32> Positions = {1, 2, 3};
  TArray<FCrowdDemoResidualPositioningEdge> Edges;
  const auto AddEdge = [&](const int32 AgentId, const int32 PositionId,
    const int32 HoldingId, const int32 StableBlocker)
  {
    auto& E = Edges.AddDefaulted_GetRef(); E.AgentId = AgentId;
    E.PositionId = PositionId; E.HoldingId = HoldingId;
    E.bCurrentToHoldingReachable = E.bFlowClear = E.bTargetClear
      = E.bObstacleClear = E.bRevisionValid = true;
    if (StableBlocker != INDEX_NONE) E.StableBlockerAgentIds.Add(StableBlocker);
  };
  AddEdge(1, 1, 101, 10);
  AddEdge(2, 2, 102, INDEX_NONE);
  AddEdge(3, 3, 103, INDEX_NONE);
  AddEdge(2, 1, 104, 11); // Removing blocker 11 is non-critical.
  FCrowdDemoResidualPositioningSummary Forward;
  FCrowdDemoPursuitPositioningKernel::AnalyzeResidualPositioning(
    Agents, Positions, Edges, Forward);
  TestEqual(TEXT("Current matching exposes one blocked residual agent"),
    Forward.CurrentMatching, 2);
  TestEqual(TEXT("Removing Stable blockers restores full capacity"),
    Forward.NoStableMatching, 3);
  TestEqual(TEXT("Only one blocker is critical"), Forward.BlockerCriticalCount, 1);
  TestEqual(TEXT("Best single removal restores one match"),
    Forward.BestSingleBlockerRemovalGain, 1);
  Algo::Reverse(Agents); Algo::Reverse(Edges);
  for (auto& E : Edges) Algo::Reverse(E.StableBlockerAgentIds);
  TArray<int32> ReversedPositions = Positions; Algo::Reverse(ReversedPositions);
  FCrowdDemoResidualPositioningSummary Reverse;
  FCrowdDemoPursuitPositioningKernel::AnalyzeResidualPositioning(
    Agents, ReversedPositions, Edges, Reverse);
  TestEqual(TEXT("Input reversal preserves matching"), Reverse.CurrentMatching,
    Forward.CurrentMatching);
  TestEqual(TEXT("Input reversal preserves counterfactual"), Reverse.NoStableMatching,
    Forward.NoStableMatching);
  TestEqual(TEXT("Input reversal preserves hash"), Reverse.ResidualCapacityHash,
    Forward.ResidualCapacityHash);
  return true;
}
#include "Mass/CrowdDemoTrafficSchedulingKernel.h"
#include "Mass/CrowdDemoDeterministicOrcaKernel.h"
#include "Mass/CrowdDemoPursuitPositioningKernel.h"
#include "Mass/CrowdDemoSf3DeterminismHash.h"
#include "ThirdParty/Reference/RVO2/CrowdDemoRvo2ReferenceSolver.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4SteeringFirstMassIntegrationContractTest,
  "CrowdDemo.SF4.Integration.SteeringFirstMassContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4SteeringFirstMassIntegrationContractTest::RunTest(const FString& Parameters)
{
  TestTrue(TEXT("SF4 selects steering-first production pipeline"),
    CrowdDemoUsesSteeringFirstSf4Pipeline(
      ECrowdDemoScenario::SimRoundPursuitPositioning));
  TestFalse(TEXT("SF4 legacy reservation production pipeline is bypassed"),
    CrowdDemoUsesLegacySf4ReservationPipeline(
      ECrowdDemoScenario::SimRoundPursuitPositioning));
  TestFalse(TEXT("SF1 does not select SF4 steering-first pipeline"),
    CrowdDemoUsesSteeringFirstSf4Pipeline(ECrowdDemoScenario::SimRoundObstacle));
  TestFalse(TEXT("SF2 does not select SF4 steering-first pipeline"),
    CrowdDemoUsesSteeringFirstSf4Pipeline(ECrowdDemoScenario::SimRoundSoftPressure));
  TestFalse(TEXT("SF3 does not select SF4 steering-first pipeline"),
    CrowdDemoUsesSteeringFirstSf4Pipeline(ECrowdDemoScenario::SimRoundCrowdTraffic));

  FCrowdDemoSf3RollbackAgentState Original;
  Original.AgentId = 17;
  Original.PursuitSteering.SteeringState = ECrowdDemoPursuitSteeringState::Commit;
  Original.PursuitSteering.HoldingId = 101;
  Original.PursuitSteering.AssignedPositionId = 202;
  Original.PursuitSteering.TargetRevision = 3;
  Original.PursuitSteering.StateRevision = 9;
  Original.PursuitSteering.CommitDecisionRevision = 88;
  Original.PursuitSteering.WaitEpoch = 4;
  Original.PursuitSteering.LastProgressBucket = 33;
  Original.PursuitSteering.LastProgressFixedStep = 87;
  const FCrowdDemoSf3RollbackAgentState Restored = Original;
  TestEqual(TEXT("rollback restores steering state"), Restored.PursuitSteering.SteeringState,
    ECrowdDemoPursuitSteeringState::Commit);
  TestEqual(TEXT("rollback restores holding owner"), Restored.PursuitSteering.HoldingId, 101);
  TestEqual(TEXT("rollback restores position owner"), Restored.PursuitSteering.AssignedPositionId, 202);
  TestEqual(TEXT("rollback restores decision revision"),
    Restored.PursuitSteering.CommitDecisionRevision, 88);

  FCrowdDemoSf3RollbackSnapshot Snapshot;
  Snapshot.HoldingSummary.CandidateHash = 11;
  Snapshot.HoldingSummary.AssignmentHash = 22;
  Snapshot.CommitGateResult.DecisionHash = 33;
  Snapshot.SteeringStateHash = 44;
  Snapshot.JointAssignmentInputHash = 55;
  Snapshot.JointPositioningResult.StableHash = 66;
  Snapshot.JointCommitResidualResult.StableHash = 77;
  Snapshot.UnfinishedBoundaryFixture.StableHash = 88;
  Snapshot.PhysicalUnsatisfiedBoundaryFixture.StableHash = 89;
  Snapshot.PhysicalUnsatisfiedBoundaryFixture.TotalAgentCount = 20;
  Snapshot.PhysicalUnsatisfiedBoundaryFixture.PhysicallySatisfiedCount = 16;
  Snapshot.PhysicalUnsatisfiedBoundaryFixture.bCountClosed = true;
  Snapshot.PhysicalUnsatisfiedBoundaryFixture.Agents.AddDefaulted_GetRef().AgentId = 18;
  Snapshot.TransitCapacityShadowSummary.StableHash = 90;
  Snapshot.TransitCapacityShadowSummary.ComponentCount = 2;
  Snapshot.TransitCapacityShadowAgents.AddDefaulted_GetRef().AgentId = 19;
  Snapshot.TransitCapacityShadowPairs.AddDefaulted_GetRef().AgentAId = 19;
  Snapshot.TransitCapacityShadowComponents.AddDefaulted_GetRef().ComponentId = 19;
  Snapshot.TransitCapacityShadowResults.AddDefaulted_GetRef().ComponentId = 19;
  Snapshot.TransitCapacityShadowSolverMsSampleCount = 7;
  Snapshot.TransitCapacityFailureFixture.bValid = true;
  Snapshot.TransitCapacityFailureFixture.StableHash = 91;
  Snapshot.TransitCapacityFailureFixture.Component.ComponentId = 19;
  Snapshot.ElasticParallelState.bActive = true;
  Snapshot.ElasticParallelState.StepIndex = 91;
  Snapshot.ElasticParallelState.Summary.StableHash = 92;
  Snapshot.ElasticFailureFixture.bValid = true;
  Snapshot.ElasticFailureFixture.FixedStepIndex = 77;
  Snapshot.ElasticFailureFixture.StableHash = 93;
  Snapshot.ElasticBaselineDesiredForward[0] = 100;
  Snapshot.ElasticBaselineActualForward[0] = 60;
  Snapshot.ElasticTwinDesiredForward[0] = 100;
  Snapshot.ElasticTwinActualForward[0] = 80;
  Snapshot.PriorityOrcaRoundHash = 99;
  Snapshot.TrafficMetrics.PriorityOrcaHash = 100;
  Snapshot.TrafficMetrics.PriorityOrcaAsymmetricPairCount = 101;
  Snapshot.TrafficMetrics.StablePhysicalDisplacedCount = 2;
  Snapshot.TrafficMetrics.StablePhysicalDisplacementCmP95 = 31.0f;
  Snapshot.TrafficMetrics.ReservePhysicalDisplacedCount = 3;
  Snapshot.TrafficMetrics.PhysicallySatisfiedPositionCount = 15;
  Snapshot.UnfinishedBoundaryFixture.Agents.AddDefaulted_GetRef().AgentId = 17;
  auto& SnapshotPosition=Snapshot.PositionAssignments.AddDefaulted_GetRef();
  SnapshotPosition.AgentId=17;SnapshotPosition.PositionId=202;
  auto& SnapshotHolding=Snapshot.HoldingAssignments.AddDefaulted_GetRef();
  SnapshotHolding.AgentId=17;SnapshotHolding.HoldingId=101;SnapshotHolding.PositionId=202;
  Snapshot.Agents.Add(Original);
  const FCrowdDemoSf3RollbackSnapshot Replay = Snapshot;
  TestEqual(TEXT("rollback restores candidate hash"), Replay.HoldingSummary.CandidateHash, 11u);
  TestEqual(TEXT("rollback restores assignment hash"), Replay.HoldingSummary.AssignmentHash, 22u);
  TestEqual(TEXT("rollback restores commit hash"), Replay.CommitGateResult.DecisionHash, 33u);
  TestEqual(TEXT("rollback restores steering hash"), Replay.SteeringStateHash, 44u);
  TestEqual(TEXT("rollback restores joint input hash"),Replay.JointAssignmentInputHash,55u);
  TestEqual(TEXT("rollback restores joint assignment hash"),Replay.JointPositioningResult.StableHash,66u);
  TestEqual(TEXT("rollback restores joint residual hash"),Replay.JointCommitResidualResult.StableHash,77u);
  TestEqual(TEXT("rollback restores unfinished boundary hash"),
    Replay.UnfinishedBoundaryFixture.StableHash,88u);
  TestEqual(TEXT("rollback restores unfinished boundary agents"),
    Replay.UnfinishedBoundaryFixture.Agents.Num(),1);
  TestEqual(TEXT("rollback restores physical-unsatisfied boundary hash"),
    Replay.PhysicalUnsatisfiedBoundaryFixture.StableHash,89u);
  TestEqual(TEXT("rollback restores physical-unsatisfied total"),
    Replay.PhysicalUnsatisfiedBoundaryFixture.TotalAgentCount,20);
  TestEqual(TEXT("rollback restores physical-unsatisfied satisfied count"),
    Replay.PhysicalUnsatisfiedBoundaryFixture.PhysicallySatisfiedCount,16);
  TestTrue(TEXT("rollback restores physical-unsatisfied count closure"),
    Replay.PhysicalUnsatisfiedBoundaryFixture.bCountClosed);
  TestEqual(TEXT("rollback restores transit shadow hash"),
    Replay.TransitCapacityShadowSummary.StableHash,90u);
  TestEqual(TEXT("rollback restores transit shadow prepared agents"),
    Replay.TransitCapacityShadowAgents.Num(),1);
  TestEqual(TEXT("rollback restores transit shadow prepared pairs"),
    Replay.TransitCapacityShadowPairs.Num(),1);
  TestEqual(TEXT("rollback restores transit shadow prepared components"),
    Replay.TransitCapacityShadowComponents.Num(),1);
  TestEqual(TEXT("rollback restores transit shadow prepared results"),
    Replay.TransitCapacityShadowResults.Num(),1);
  TestEqual(TEXT("rollback restores transit shadow sample length"),
    Replay.TransitCapacityShadowSolverMsSampleCount,7);
  TestEqual(TEXT("rollback restores transit capacity failure fixture hash"),
    Replay.TransitCapacityFailureFixture.StableHash,91u);
  TestTrue(TEXT("rollback restores elastic parallel active state"),
    Replay.ElasticParallelState.bActive);
  TestEqual(TEXT("rollback restores elastic parallel step"),
    Replay.ElasticParallelState.StepIndex,91);
  TestEqual(TEXT("rollback restores elastic parallel summary hash"),
    Replay.ElasticParallelState.Summary.StableHash,92u);
  TestEqual(TEXT("rollback restores elastic first fixture hash"),
    Replay.ElasticFailureFixture.StableHash,93u);
  TestEqual(TEXT("rollback restores elastic baseline forward aggregate"),
    Replay.ElasticBaselineActualForward[0],static_cast<int64>(60));
  TestEqual(TEXT("rollback restores elastic twin forward aggregate"),
    Replay.ElasticTwinActualForward[0],static_cast<int64>(80));
  TestEqual(TEXT("rollback restores priority round hash"), Replay.PriorityOrcaRoundHash, 99u);
  TestEqual(TEXT("rollback restores priority metric hash"),
    Replay.TrafficMetrics.PriorityOrcaHash, 100u);
  TestEqual(TEXT("rollback restores asymmetric priority samples"),
    Replay.TrafficMetrics.PriorityOrcaAsymmetricPairCount, 101);
  TestEqual(TEXT("rollback restores stable physical displacement samples"),
    Replay.TrafficMetrics.StablePhysicalDisplacedCount, 2);
  TestEqual(TEXT("rollback restores reserve physical displacement samples"),
    Replay.TrafficMetrics.ReservePhysicalDisplacedCount, 3);
  TestEqual(TEXT("rollback restores physical satisfaction count"),
    Replay.TrafficMetrics.PhysicallySatisfiedPositionCount, 15);
  TestEqual(TEXT("rollback restores atomic Position owner"),Replay.PositionAssignments[0].PositionId,202);
  TestEqual(TEXT("rollback restores atomic Holding owner"),Replay.HoldingAssignments[0].HoldingId,101);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4PursuitPositioningKernelTest,
  "CrowdDemo.SF4.Positioning.CandidatesAndDeferredAssignment",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4PursuitPositioningKernelTest::RunTest(const FString& Parameters)
{
  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("SF1 flow fixture builds"), FCrowdDemoSharedFlowFieldKernel::Build(
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1), Field));
  const FVector PrecisionStart(2200.0f, 1600.0f, 60.0f);
  const FVector PrecisionProposed = PrecisionStart + FVector(0.348f, 0.0f, 0.0f);
  const FCrowdDemoSharedFlowConstraintResult ContinuousPrecision =
    FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
      Field.Config, PrecisionStart, PrecisionProposed, 1.0f / 30.0f, true);
  const FVector RoundedPrecision(
    FMath::RoundToDouble(PrecisionProposed.X),
    FMath::RoundToDouble(PrecisionProposed.Y), PrecisionProposed.Z);
  const FCrowdDemoSharedFlowConstraintResult IntegerPrecision =
    FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
      Field.Config, PrecisionStart, RoundedPrecision, 1.0f / 30.0f, true);
  TestTrue(TEXT("SF4 continuous reproject preserves sub-centimeter locomotion"),
    ContinuousPrecision.Velocity.Size2D() > 10.0f);
  TestTrue(TEXT("legacy integer reproject erases sub-centimeter locomotion"),
    IntegerPrecision.Velocity.IsNearlyZero());
  FCrowdDemoPursuitTargetFact Target;
  Target.TargetId = 7;
  Target.Location = FVector2f(2200.0f, 1600.0f);
  Target.RadiusCm = 80.0f;
  FCrowdDemoPursuitPositioningSettings Settings;
  Settings.AllowedDistanceMaxCm = 1000.0f;
  Settings.PreferredDistanceMaxCm = 480.0f;
  TArray<FCrowdDemoPositionCandidate> CandidatesA, CandidatesB;
  FCrowdDemoPositioningSummary CandidateSummaryA, CandidateSummaryB;
  FCrowdDemoPursuitPositioningKernel::BuildCandidates(
    Target, 42.0f, Settings, Field, CandidatesA, CandidateSummaryA);
  FCrowdDemoPursuitPositioningKernel::BuildCandidates(
    Target, 42.0f, Settings, Field, CandidatesB, CandidateSummaryB);
  TestTrue(TEXT("candidate capacity covers 20"), CandidatesA.Num() >= 20);
  TestTrue(TEXT("front and reserve are both generated"),
    CandidateSummaryA.FrontCapacity > 0 && CandidateSummaryA.ReserveCapacity > 0);
  int32 SingleFrontRadialBand = INDEX_NONE;
  for (const FCrowdDemoPositionCandidate& Candidate : CandidatesA)
  {
    if (Candidate.Role == ECrowdDemoPositionRole::Front)
    {
      if (SingleFrontRadialBand == INDEX_NONE) SingleFrontRadialBand = Candidate.RadialBand;
      TestEqual(TEXT("all Front candidates use one radial band"),
        Candidate.RadialBand, SingleFrontRadialBand);
    }
  }
  TestTrue(TEXT("single Front radial band exists"), SingleFrontRadialBand != INDEX_NONE);
  for (const FCrowdDemoPositionCandidate& Candidate : CandidatesA)
  {
    if (Candidate.Role == ECrowdDemoPositionRole::Reserve)
      TestTrue(TEXT("Reserve candidates remain outside the Front band"),
        Candidate.RadialBand > SingleFrontRadialBand);
  }
  TestEqual(TEXT("candidate generation hash repeats"),
    CandidateSummaryB.CandidateHash, CandidateSummaryA.CandidateHash);
  TestEqual(TEXT("candidate unreachable count"), CandidateSummaryA.CandidateUnreachableCount, 0);
  TSet<int32> PositionIds;
  for (int32 A = 0; A < CandidatesA.Num(); ++A)
  {
    PositionIds.Add(CandidatesA[A].PositionId);
    const FCrowdDemoSharedFlowSample CandidateSample = FCrowdDemoSharedFlowFieldKernel::Sample(
      Field, FVector(CandidatesA[A].WorldLocation.X, CandidatesA[A].WorldLocation.Y, 60.0f));
    TestEqual(TEXT("candidate remains in reachable raster"),
      CandidateSample.Status, ECrowdDemoFlowLocationStatus::Reachable);
    FCrowdDemoSharedFlowFieldConfig ClearanceConfig = Field.Config;
    ClearanceConfig.AgentInflateCm = 52.0f;
    TestFalse(TEXT("candidate clearance rejects inflated obstacle"),
      FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        ClearanceConfig, FVector(CandidatesA[A].WorldLocation.X, CandidatesA[A].WorldLocation.Y, 60.0f)));
    for (int32 B = A + 1; B < CandidatesA.Num(); ++B)
      TestTrue(TEXT("candidate spacing"),
        (CandidatesA[A].WorldLocation - CandidatesA[B].WorldLocation).Size() >= 94.0f);
  }
  TestEqual(TEXT("position ids unique"), PositionIds.Num(), CandidatesA.Num());

  TArray<FCrowdDemoPositioningAgent> Agents;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    FCrowdDemoPositioningAgent& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = 100 + Index;
    Agent.Location = FVector2f(-500.0f + (Index % 5) * 95.0f, -2800.0f + (Index / 5) * 95.0f);
    Agent.PreferredApproachSector = Index % Settings.AngularSectorCount;
  }
  TArray<FCrowdDemoPositionAssignment> AssignmentsA, AssignmentsB;
  FCrowdDemoPositioningSummary AssignmentSummaryA, AssignmentSummaryB;
  FCrowdDemoPursuitPositioningKernel::Assign(
    Agents, CandidatesA, Target, Settings, AssignmentsA, AssignmentSummaryA);
  TestEqual(TEXT("all 20 assigned"), AssignmentSummaryA.AssignedCount, 20);
  TSet<int32> AssignedPositions;
  for (const FCrowdDemoPositionAssignment& Assignment : AssignmentsA)
    AssignedPositions.Add(Assignment.PositionId);
  TestEqual(TEXT("assignments are unique"), AssignedPositions.Num(), 20);
  Algo::Reverse(Agents);
  Algo::Reverse(CandidatesB);
  FCrowdDemoPursuitPositioningKernel::Assign(
    Agents, CandidatesB, Target, Settings, AssignmentsB, AssignmentSummaryB);
  TestEqual(TEXT("agent/candidate input order does not change assignment hash"),
    AssignmentSummaryB.AssignmentHash, AssignmentSummaryA.AssignmentHash);
  for (int32 Index = 0; Index < AssignmentsA.Num(); ++Index)
  {
    TestEqual(TEXT("assignment agent order stable"), AssignmentsB[Index].AgentId, AssignmentsA[Index].AgentId);
    TestEqual(TEXT("assignment position stable"), AssignmentsB[Index].PositionId, AssignmentsA[Index].PositionId);
  }

  for (FCrowdDemoPositioningAgent& Agent : Agents)
  {
    const FCrowdDemoPositionAssignment* Existing = AssignmentsA.FindByPredicate(
      [&](const auto& Assignment){ return Assignment.AgentId == Agent.AgentId; });
    Agent.ExistingPositionId = Existing ? Existing->PositionId : INDEX_NONE;
    Agent.ExistingRole = Existing ? Existing->Role : ECrowdDemoPositionRole::Reserve;
    Agent.ExistingState = Existing && Existing->Role == ECrowdDemoPositionRole::Front
      ? ECrowdDemoPursuitPositionState::StableOccupied
      : ECrowdDemoPursuitPositionState::ReserveHold;
  }
  TArray<FCrowdDemoPositionAssignment> Reused;
  FCrowdDemoPositioningSummary ReuseSummary;
  FCrowdDemoPursuitPositioningKernel::Assign(
    Agents, CandidatesA, Target, Settings, Reused, ReuseSummary);
  TestEqual(TEXT("all existing assignments reused"), ReuseSummary.ReusedCount, 20);

  TArray<FCrowdDemoPositionCandidate> PromotionCandidates;
  int32 PromotionFrontCandidates = 0;
  int32 PromotionReserveCandidates = 0;
  for (const FCrowdDemoPositionCandidate& Candidate : CandidatesA)
  {
    int32& RoleCount = Candidate.Role == ECrowdDemoPositionRole::Front
      ? PromotionFrontCandidates : PromotionReserveCandidates;
    if (RoleCount < 4)
    {
      PromotionCandidates.Add(Candidate);
      ++RoleCount;
    }
  }
  TArray<FCrowdDemoPositioningAgent> PromotionAgents;
  for (int32 Index = 0; Index < 6; ++Index)
  {
    FCrowdDemoPositioningAgent& Agent = PromotionAgents.AddDefaulted_GetRef();
    Agent.AgentId = 500 + Index;
    Agent.Location = Target.Location + FVector2f(-800.0f, (Index - 3) * 95.0f);
  }
  TArray<FCrowdDemoPositionAssignment> BeforePromotion;
  FCrowdDemoPositioningSummary BeforePromotionSummary;
  FCrowdDemoPursuitPositioningKernel::Assign(PromotionAgents, PromotionCandidates,
    Target, Settings, BeforePromotion, BeforePromotionSummary);
  int32 FrontOwnerCount = 0;
  int32 ReserveOwnerCount = 0;
  for (const FCrowdDemoPositionAssignment& Assignment : BeforePromotion)
  {
    FrontOwnerCount += Assignment.Role == ECrowdDemoPositionRole::Front ? 1 : 0;
    ReserveOwnerCount += Assignment.Role == ECrowdDemoPositionRole::Reserve ? 1 : 0;
  }
  TestEqual(TEXT("promotion fixture has four front owners"), FrontOwnerCount, 4);
  TestEqual(TEXT("promotion fixture has two reserve owners"), ReserveOwnerCount, 2);
  for (FCrowdDemoPositioningAgent& Agent : PromotionAgents)
  {
    const FCrowdDemoPositionAssignment* Existing = BeforePromotion.FindByPredicate(
      [&](const auto& Assignment){ return Assignment.AgentId == Agent.AgentId; });
    Agent.ExistingPositionId = Existing ? Existing->PositionId : INDEX_NONE;
    Agent.ExistingRole = Existing ? Existing->Role : ECrowdDemoPositionRole::Reserve;
    Agent.ExistingState = Existing && Existing->Role == ECrowdDemoPositionRole::Front
      ? ECrowdDemoPursuitPositionState::StableOccupied
      : ECrowdDemoPursuitPositionState::ReserveHold;
  }
  const int32 VacantFront = BeforePromotion.FindByPredicate([](const auto& Assignment)
  { return Assignment.Role == ECrowdDemoPositionRole::Front; })->PositionId;
  PromotionAgents.RemoveAll([&](const auto& Agent)
  { return Agent.ExistingPositionId == VacantFront; });
  TArray<FCrowdDemoPositionAssignment> Promoted;
  FCrowdDemoPositioningSummary PromotionSummary;
  FCrowdDemoPursuitPositioningKernel::Assign(
    PromotionAgents, PromotionCandidates, Target, Settings, Promoted, PromotionSummary);
  TestTrue(TEXT("vacancy is filled"), Promoted.ContainsByPredicate(
    [&](const auto& Assignment){ return Assignment.PositionId == VacantFront; }));
  TestTrue(TEXT("vacancy promotion is counted"), PromotionSummary.PromotionCount > 0);

  TArray<FCrowdDemoPositionCandidate> Invalidated = PromotionCandidates;
  Invalidated.RemoveAll([&](const auto& Candidate){ return Candidate.PositionId == VacantFront; });
  TArray<FCrowdDemoPositionAssignment> AfterInvalidation;
  FCrowdDemoPositioningSummary InvalidationSummary;
  FCrowdDemoPursuitPositioningKernel::Assign(
    PromotionAgents, Invalidated, Target, Settings, AfterInvalidation, InvalidationSummary);
  TestFalse(TEXT("invalidated position is released"), AfterInvalidation.ContainsByPredicate(
    [&](const auto& Assignment){ return Assignment.PositionId == VacantFront; }));

  TArray<FCrowdDemoPositionCandidate> TieCandidates;
  FCrowdDemoPositionCandidate Left;
  Left.PositionId = 10;
  Left.TargetId = Target.TargetId;
  Left.Role = ECrowdDemoPositionRole::Front;
  Left.LocalOffset = FVector2f(-300.0f, 0.0f);
  Left.WorldLocation = Target.Location + Left.LocalOffset;
  Left.bReachable = Left.bClearanceValid = true;
  TieCandidates.Add(Left);
  FCrowdDemoPositionCandidate Right = Left;
  Right.PositionId = 20;
  Right.LocalOffset = FVector2f(300.0f, 0.0f);
  Right.WorldLocation = Target.Location + Right.LocalOffset;
  TieCandidates.Add(Right);
  TArray<FCrowdDemoPositioningAgent> TieAgents;
  FCrowdDemoPositioningAgent TieHigh;
  TieHigh.AgentId = 2;
  TieHigh.Location = Target.Location;
  TieAgents.Add(TieHigh);
  FCrowdDemoPositioningAgent TieLow = TieHigh;
  TieLow.AgentId = 1;
  TieAgents.Add(TieLow);
  TArray<FCrowdDemoPositionAssignment> TieAssignments;
  FCrowdDemoPositioningSummary TieSummary;
  FCrowdDemoPursuitPositioningKernel::Assign(
    TieAgents, TieCandidates, Target, Settings, TieAssignments, TieSummary);
  TestEqual(TEXT("equal-cost lower AgentId wins lower PositionId"),
    TieAssignments[0].PositionId, 10);
  TestEqual(TEXT("equal-cost remaining Agent gets stable next PositionId"),
    TieAssignments[1].PositionId, 20);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4PositionIngressDiagnosticTest,
  "CrowdDemo.SF4.Positioning.IngressDiagnostic",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4PositionIngressDiagnosticTest::RunTest(const FString& Parameters)
{
  TestTrue(TEXT("segment crosses safety circle"),
    FCrowdDemoPursuitPositioningKernel::SegmentIntersectsSafetyCircle(
      FVector2f(-300, 0), FVector2f(300, 0), FVector2f::ZeroVector, 100.0f));
  TestFalse(TEXT("separated segment misses safety circle"),
    FCrowdDemoPursuitPositioningKernel::SegmentIntersectsSafetyCircle(
      FVector2f(-300, 200), FVector2f(300, 200), FVector2f::ZeroVector, 100.0f));

  FCrowdDemoPursuitTargetFact Target;
  Target.TargetId = 9;
  Target.Location = FVector2f::ZeroVector;
  Target.RadiusCm = 80.0f;
  FCrowdDemoPursuitPositioningSettings Settings;
  Settings.AngularSectorCount = 8;
  TArray<FCrowdDemoPositionCandidate> Candidates;
  FCrowdDemoPositionCandidate& Assigned = Candidates.AddDefaulted_GetRef();
  Assigned.PositionId = 100;
  Assigned.TargetId = Target.TargetId;
  Assigned.Role = ECrowdDemoPositionRole::Front;
  Assigned.LocalOffset = Assigned.WorldLocation = FVector2f(300, 0);
  Assigned.AngularSector = 0;
  Assigned.bReachable = Assigned.bClearanceValid = true;
  FCrowdDemoPositionCandidate& Alternative = Candidates.AddDefaulted_GetRef();
  Alternative = Assigned;
  Alternative.PositionId = 101;
  Alternative.LocalOffset = Alternative.WorldLocation = FVector2f(0, -300);
  Alternative.AngularSector = 6;

  TArray<FCrowdDemoPositionIngressAgent> Agents;
  FCrowdDemoPositionIngressAgent& Moving = Agents.AddDefaulted_GetRef();
  Moving.AgentId = 10;
  Moving.Location = FVector2f(-300, 0);
  Moving.PositionId = Assigned.PositionId;
  Moving.AssignedLocation = Assigned.WorldLocation;
  Moving.Role = ECrowdDemoPositionRole::Front;
  Moving.State = ECrowdDemoPursuitPositionState::SlotCommit;
  Moving.PreferredVelocity = FVector2f(800, 0);
  Moving.OrcaVelocity = FVector2f(4, 0);
  Moving.FinalVelocity = FVector2f(4, 0);
  Moving.OrcaConstraintOtherAgentIds = {20, 30, 40};
  FCrowdDemoPositionIngressAgent& Stable = Agents.AddDefaulted_GetRef();
  Stable.AgentId = 20;
  Stable.Location = FVector2f(120, 0);
  Stable.PositionId = 200;
  Stable.State = ECrowdDemoPursuitPositionState::StableOccupied;
  FCrowdDemoPositionIngressAgent& Reserve = Agents.AddDefaulted_GetRef();
  Reserve.AgentId = 30;
  Reserve.Location = FVector2f(-80, 0);
  Reserve.PositionId = 300;
  Reserve.State = ECrowdDemoPursuitPositionState::ReserveHold;
  FCrowdDemoPositionIngressAgent& Commit = Agents.AddDefaulted_GetRef();
  Commit.AgentId = 40;
  Commit.Location = FVector2f(220, 0);
  Commit.PositionId = 400;
  Commit.State = ECrowdDemoPursuitPositionState::SlotCommit;

  TArray<FCrowdDemoPositionIngressEvaluation> EvaluationsA, EvaluationsB;
  FCrowdDemoPositionIngressSummary SummaryA, SummaryB;
  FCrowdDemoPositionIngressFixture FixtureA, FixtureB;
  FCrowdDemoPursuitPositioningKernel::EvaluateIngress(
    Target, Settings, Candidates, Agents, EvaluationsA, SummaryA, FixtureA);
  TestEqual(TEXT("one assigned SlotCommit evaluated"), SummaryA.SlotCommitCount, 1);
  TestEqual(TEXT("target exclusion crossing classified"), SummaryA.TargetExclusionCrossingCount, 1);
  TestEqual(TEXT("stable blocker classified"), SummaryA.DirectPathStableBlockedCount, 1);
  TestEqual(TEXT("reserve blocker classified"), SummaryA.DirectPathReserveBlockedCount, 1);
  TestEqual(TEXT("commit blocker classified"), SummaryA.DirectPathCommitBlockedCount, 1);
  TestEqual(TEXT("ORCA stable source classified"), SummaryA.OrcaConstraintsFromStableCount, 1);
  TestEqual(TEXT("ORCA reserve source classified"), SummaryA.OrcaConstraintsFromReserveCount, 1);
  TestEqual(TEXT("ORCA commit source classified"), SummaryA.OrcaConstraintsFromCommitCount, 1);
  TestTrue(TEXT("minimum fixture is bounded and valid"), FixtureA.bValid && FixtureA.Blockers.Num() == 3);
  Algo::Reverse(Agents);
  Algo::Reverse(Candidates);
  FCrowdDemoPursuitPositioningKernel::EvaluateIngress(
    Target, Settings, Candidates, Agents, EvaluationsB, SummaryB, FixtureB);
  TestEqual(TEXT("input order preserves diagnostic hash"), SummaryB.EvaluationHash, SummaryA.EvaluationHash);
  TestEqual(TEXT("input order preserves fixture hash"), FixtureB.StableHash, FixtureA.StableHash);

  FCrowdDemoSharedFlowField ApproachField;
  TestTrue(TEXT("approach flow fixture builds"), FCrowdDemoSharedFlowFieldKernel::Build(
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1), ApproachField));
  FCrowdDemoPursuitTargetFact ApproachTarget;
  ApproachTarget.TargetId = 11;
  ApproachTarget.Location = FVector2f(2200, 1600);
  ApproachTarget.RadiusCm = 80.0f;
  FCrowdDemoPositionCandidate ApproachCandidate;
  ApproachCandidate.PositionId = 700;
  ApproachCandidate.TargetId = ApproachTarget.TargetId;
  ApproachCandidate.Role = ECrowdDemoPositionRole::Front;
  ApproachCandidate.LocalOffset = FVector2f(0, 300);
  ApproachCandidate.WorldLocation = ApproachTarget.Location + ApproachCandidate.LocalOffset;
  ApproachCandidate.AngularSector = 8;
  ApproachCandidate.bReachable = ApproachCandidate.bClearanceValid = true;
  FCrowdDemoPositionIngressBlocker ApproachBlocker;
  ApproachBlocker.AgentId = 77;
  ApproachBlocker.State = ECrowdDemoPursuitPositionState::StableOccupied;
  ApproachBlocker.Location = FVector2f(2200, 1700);
  ApproachBlocker.RadiusCm = 42.0f;
  TArray<FCrowdDemoPositionIngressBlocker> ApproachBlockers = {ApproachBlocker};
  TestTrue(TEXT("old direct-to-slot path crosses target exclusion"),
    FCrowdDemoPursuitPositioningKernel::SegmentIntersectsSafetyCircle(
      FVector2f(2200, 800), ApproachCandidate.WorldLocation, ApproachTarget.Location, 132.0f));
  const FCrowdDemoFrontApproachRoute ApproachRouteA =
    FCrowdDemoPursuitPositioningKernel::BuildFrontApproachRoute(
      ApproachTarget, Settings, ApproachField, 55, 42.0f, FVector2f(2200, 800),
      ApproachCandidate, ApproachBlockers, 800.0f, 1);
  TestTrue(TEXT("target-aware polar route has reachable gate"), ApproachRouteA.bGateReachable);
  TestTrue(TEXT("target-aware polar route has reachable arc"), ApproachRouteA.bArcReachable);
  TestTrue(TEXT("target-aware radial commit avoids stable blocker"), ApproachRouteA.bRadialCommitClear);
  TestTrue(TEXT("target-aware radial commit avoids target exclusion"), ApproachRouteA.bTargetExclusionClear);
  TestTrue(TEXT("target-aware route never falls back to direct line"),
    ApproachRouteA.Phase != ECrowdDemoFrontApproachPhase::None);
  TArray<FCrowdDemoPositionIngressBlocker> ReorderedBlockers = {ApproachBlocker};
  Algo::Reverse(ReorderedBlockers);
  const FCrowdDemoFrontApproachRoute ApproachRouteB =
    FCrowdDemoPursuitPositioningKernel::BuildFrontApproachRoute(
      ApproachTarget, Settings, ApproachField, 55, 42.0f, FVector2f(2200, 800),
      ApproachCandidate, ReorderedBlockers, 800.0f, 1);
  TestEqual(TEXT("blocker order preserves approach route hash"),
    ApproachRouteB.RouteHash, ApproachRouteA.RouteHash);
  FCrowdDemoPositionCandidate InvalidGateCandidate = ApproachCandidate;
  InvalidGateCandidate.PositionId = 701;
  InvalidGateCandidate.LocalOffset = FVector2f(10000, 0);
  InvalidGateCandidate.WorldLocation = ApproachTarget.Location + InvalidGateCandidate.LocalOffset;
  const FCrowdDemoFrontApproachRoute InvalidGateRoute =
    FCrowdDemoPursuitPositioningKernel::BuildFrontApproachRoute(
      ApproachTarget, Settings, ApproachField, 55, 42.0f, FVector2f(2200, 800),
      InvalidGateCandidate, {}, 800.0f, 1);
  TestFalse(TEXT("invalid gate does not fall back to direct slot guidance"),
    InvalidGateRoute.bGateReachable || InvalidGateRoute.Phase != ECrowdDemoFrontApproachPhase::None);

  TArray<FCrowdDemoPositionCandidate> AdmissionCandidates;
  FCrowdDemoPositionCandidate FarCandidate = Assigned;
  FarCandidate.PositionId = 501;
  FarCandidate.LocalOffset = FarCandidate.WorldLocation = FVector2f(0, 300);
  FarCandidate.AngularSector = 2;
  AdmissionCandidates.Add(FarCandidate);
  FCrowdDemoPositionCandidate NearCandidate = Assigned;
  NearCandidate.PositionId = 502;
  NearCandidate.LocalOffset = NearCandidate.WorldLocation = FVector2f(0, -300);
  NearCandidate.AngularSector = 6;
  AdmissionCandidates.Add(NearCandidate);
  TArray<FCrowdDemoFrontAdmissionAgent> AdmissionAgents;
  FCrowdDemoFrontAdmissionAgent FarAgent;
  FarAgent.AgentId = 2;
  FarAgent.Location = FVector2f(-200, -800);
  FarAgent.PositionId = FarCandidate.PositionId;
  FarAgent.Role = ECrowdDemoPositionRole::Front;
  FarAgent.State = ECrowdDemoPursuitPositionState::FrontAssignedWaiting;
  FarAgent.bRouteValid = true;
  FarAgent.RoutePoints = {FarAgent.Location, FVector2f(0, 0), FarCandidate.WorldLocation};
  AdmissionAgents.Add(FarAgent);
  FCrowdDemoFrontAdmissionAgent NearAgent = FarAgent;
  NearAgent.AgentId = 1;
  NearAgent.Location = FVector2f(200, -800);
  NearAgent.PositionId = NearCandidate.PositionId;
  NearAgent.bRouteValid = true;
  NearAgent.RoutePoints = {NearAgent.Location, FVector2f(0, 0), NearCandidate.WorldLocation};
  AdmissionAgents.Add(NearAgent);
  FCrowdDemoPursuitPositioningSettings AdmissionSettings = Settings;
  AdmissionSettings.FrontAdmissionWaveSize = 1;
  FCrowdDemoFrontAdmissionResult AdmissionResultA, AdmissionResultB;
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultA);
  TestEqual(TEXT("far-side candidate is admitted before entry-side candidate"),
    AdmissionResultA.GrantedAgentIds[0], 2);
  Algo::Reverse(AdmissionCandidates);
  Algo::Reverse(AdmissionAgents);
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultB);
  TestEqual(TEXT("admission input order preserves decision hash"),
    AdmissionResultB.DecisionHash, AdmissionResultA.DecisionHash);

  AdmissionSettings.FrontAdmissionWaveSize = 2;
  AdmissionAgents[0].Location = FVector2f(-300, -300);
  AdmissionAgents[0].PositionId = FarCandidate.PositionId;
  AdmissionAgents[1].Location = FVector2f(300, -300);
  AdmissionAgents[1].PositionId = NearCandidate.PositionId;
  for (FCrowdDemoPositionCandidate& Candidate : AdmissionCandidates)
  {
    if (Candidate.PositionId == FarCandidate.PositionId)
      Candidate.LocalOffset = Candidate.WorldLocation = FVector2f(300, 300);
    else
      Candidate.LocalOffset = Candidate.WorldLocation = FVector2f(-300, 300);
  }
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultA);
  TestEqual(TEXT("crossed ingress paths are split across waves"),
    AdmissionResultA.GrantedAgentIds.Num(), 1);

  FCrowdDemoPositionCandidate ActiveCandidate = FarCandidate;
  ActiveCandidate.PositionId = 503;
  ActiveCandidate.LocalOffset = ActiveCandidate.WorldLocation = FVector2f(-600, 600);
  FCrowdDemoPositionCandidate ParallelCandidate = NearCandidate;
  ParallelCandidate.PositionId = 504;
  ParallelCandidate.LocalOffset = ParallelCandidate.WorldLocation = FVector2f(600, 600);
  FCrowdDemoPositionCandidate ConflictingCandidate = NearCandidate;
  ConflictingCandidate.PositionId = 505;
  ConflictingCandidate.LocalOffset = ConflictingCandidate.WorldLocation = FVector2f(-600, 600);
  AdmissionCandidates = {ActiveCandidate, ParallelCandidate, ConflictingCandidate};
  FCrowdDemoFrontAdmissionAgent ActiveAgent = FarAgent;
  ActiveAgent.AgentId = 10;
  ActiveAgent.Location = FVector2f(-600, -600);
  ActiveAgent.PositionId = ActiveCandidate.PositionId;
  ActiveAgent.State = ECrowdDemoPursuitPositionState::SlotCommit;
  ActiveAgent.RoutePoints = {ActiveAgent.Location, ActiveCandidate.WorldLocation};
  FCrowdDemoFrontAdmissionAgent ParallelAgent = FarAgent;
  ParallelAgent.AgentId = 11;
  ParallelAgent.Location = FVector2f(600, -600);
  ParallelAgent.PositionId = ParallelCandidate.PositionId;
  ParallelAgent.RoutePoints = {ParallelAgent.Location, ParallelCandidate.WorldLocation};
  FCrowdDemoFrontAdmissionAgent ConflictingAgent = FarAgent;
  ConflictingAgent.AgentId = 12;
  ConflictingAgent.Location = FVector2f(600, -600);
  ConflictingAgent.PositionId = ConflictingCandidate.PositionId;
  ConflictingAgent.RoutePoints = {ConflictingAgent.Location, ConflictingCandidate.WorldLocation};
  AdmissionAgents = {ActiveAgent, ParallelAgent, ConflictingAgent};
  AdmissionSettings.FrontApproachNoProgressTimeoutSteps = 180;
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultA);
  TestTrue(TEXT("active route permits a non-conflicting waiting route"),
    AdmissionResultA.GrantedAgentIds.Contains(ParallelAgent.AgentId));
  TestFalse(TEXT("active route blocks a conflicting waiting route"),
    AdmissionResultA.GrantedAgentIds.Contains(ConflictingAgent.AgentId));
  Algo::Reverse(AdmissionCandidates);
  Algo::Reverse(AdmissionAgents);
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultB);
  TestEqual(TEXT("active-route admission is input-order invariant"),
    AdmissionResultB.DecisionHash, AdmissionResultA.DecisionHash);

  TestTrue(TEXT("active radial approach owns compose outside distance gate"),
    FCrowdDemoPursuitPositioningKernel::ShouldComposePositionGuidance(
      ECrowdDemoPursuitPositionState::FrontCommitGranted,
      ECrowdDemoFrontApproachPhase::RadialStage, false, 5000.0f, 1200.0f));
  TestTrue(TEXT("slot radial commit owns compose outside distance gate"),
    FCrowdDemoPursuitPositioningKernel::ShouldComposePositionGuidance(
      ECrowdDemoPursuitPositionState::SlotCommit,
      ECrowdDemoFrontApproachPhase::RadialCommit, false, 5000.0f, 1200.0f));
  TestFalse(TEXT("waiting agent outside distance gate stays on traffic guidance"),
    FCrowdDemoPursuitPositioningKernel::ShouldComposePositionGuidance(
      ECrowdDemoPursuitPositionState::FrontAssignedWaiting,
      ECrowdDemoFrontApproachPhase::None, false, 5000.0f, 1200.0f));
  TestTrue(TEXT("waiting agent inside distance gate composes holding guidance"),
    FCrowdDemoPursuitPositioningKernel::ShouldComposePositionGuidance(
      ECrowdDemoPursuitPositionState::FrontAssignedWaiting,
      ECrowdDemoFrontApproachPhase::None, false, 1000.0f, 1200.0f));
  TestFalse(TEXT("portal ownership precedes active approach compose"),
    FCrowdDemoPursuitPositioningKernel::ShouldComposePositionGuidance(
      ECrowdDemoPursuitPositionState::FrontCommitGranted,
      ECrowdDemoFrontApproachPhase::RadialStage, true, 1000.0f, 1200.0f));

  FCrowdDemoPositionCandidate EntryCandidate = FarCandidate;
  EntryCandidate.PositionId = 506;
  EntryCandidate.LocalOffset = EntryCandidate.WorldLocation = FVector2f(0.0f, 0.0f);
  FCrowdDemoFrontAdmissionAgent OutsideEntryAgent = FarAgent;
  OutsideEntryAgent.AgentId = 13;
  OutsideEntryAgent.PositionId = EntryCandidate.PositionId;
  OutsideEntryAgent.Location = FVector2f(0.0f, -1201.0f);
  OutsideEntryAgent.RoutePoints = {OutsideEntryAgent.Location, EntryCandidate.WorldLocation};
  FCrowdDemoFrontAdmissionAgent InsideEntryAgent = OutsideEntryAgent;
  InsideEntryAgent.AgentId = 14;
  InsideEntryAgent.Location = FVector2f(0.0f, -1200.0f);
  InsideEntryAgent.RoutePoints = {InsideEntryAgent.Location, EntryCandidate.WorldLocation};
  AdmissionCandidates = {EntryCandidate};
  AdmissionAgents = {OutsideEntryAgent};
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultA);
  TestEqual(TEXT("waiting route outside approach envelope is not granted"),
    AdmissionResultA.GrantedAgentIds.Num(), 0);
  AdmissionAgents = {InsideEntryAgent};
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultA);
  TestTrue(TEXT("waiting route at approach envelope is eligible"),
    AdmissionResultA.GrantedAgentIds.Contains(InsideEntryAgent.AgentId));

  FCrowdDemoOrcaAgent ActiveRouteOrcaAgent;
  ActiveRouteOrcaAgent.AgentId = 21;
  ActiveRouteOrcaAgent.Position = FVector2f(0.0f, 0.0f);
  ActiveRouteOrcaAgent.Velocity = FVector2f(100.0f, 0.0f);
  ActiveRouteOrcaAgent.PreferredVelocity = FVector2f(800.0f, 0.0f);
  ActiveRouteOrcaAgent.RadiusCm = 42.0f;
  ActiveRouteOrcaAgent.Sf4RouteMode = ECrowdDemoOrcaRouteMode::Active;
  ActiveRouteOrcaAgent.Sf4RouteSafetyGapCm = 10.0f;
  ActiveRouteOrcaAgent.Sf4RoutePoints = {
    FVector2f(0.0f, 0.0f), FVector2f(1000.0f, 0.0f)};
  FCrowdDemoOrcaAgent YieldingRouteOrcaAgent;
  YieldingRouteOrcaAgent.AgentId = 22;
  YieldingRouteOrcaAgent.Position = FVector2f(500.0f, 20.0f);
  YieldingRouteOrcaAgent.RadiusCm = 42.0f;
  YieldingRouteOrcaAgent.Sf4RouteMode = ECrowdDemoOrcaRouteMode::Yielding;
  YieldingRouteOrcaAgent.Sf4RouteSafetyGapCm = 10.0f;
  const FCrowdDemoOrcaRoutePairPolicy ActivePolicy =
    FCrowdDemoDeterministicOrcaKernel::EvaluateSf4RoutePairPolicy(
      ActiveRouteOrcaAgent, YieldingRouteOrcaAgent);
  TestTrue(TEXT("active route conflict overrides default ORCA pair policy"),
    ActivePolicy.bOverridesDefault);
  TestFalse(TEXT("active route does not take waiting-agent avoidance constraint"),
    ActivePolicy.bIncludeConstraint);
  const FCrowdDemoOrcaRoutePairPolicy YieldingPolicy =
    FCrowdDemoDeterministicOrcaKernel::EvaluateSf4RoutePairPolicy(
      YieldingRouteOrcaAgent, ActiveRouteOrcaAgent);
  TestTrue(TEXT("waiting agent takes active-route avoidance constraint"),
    YieldingPolicy.bIncludeConstraint);
  TestEqual(TEXT("waiting agent takes full active-route responsibility"),
    YieldingPolicy.Responsibility, 1.0f);
  FCrowdDemoOrcaAgent NonConflictingWaiting = YieldingRouteOrcaAgent;
  NonConflictingWaiting.Position = FVector2f(500.0f, 500.0f);
  TestFalse(TEXT("non-conflicting waiting agent keeps default ORCA pair policy"),
    FCrowdDemoDeterministicOrcaKernel::EvaluateSf4RoutePairPolicy(
      ActiveRouteOrcaAgent, NonConflictingWaiting).bOverridesDefault);
  FCrowdDemoOrcaAgent YieldingReserveOrcaAgent = YieldingRouteOrcaAgent;
  YieldingReserveOrcaAgent.AgentId = 23;
  TestTrue(TEXT("yielding reserve uses the same active-route conflict policy"),
    FCrowdDemoDeterministicOrcaKernel::EvaluateSf4RoutePairPolicy(
      YieldingReserveOrcaAgent, ActiveRouteOrcaAgent).bOverridesDefault);
  FCrowdDemoOrcaConstraint RouteConstraint;
  FCrowdDemoOrcaSettings RouteOrcaSettings;
  TestTrue(TEXT("generic ORCA keeps the active-side constraint"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      ActiveRouteOrcaAgent, YieldingRouteOrcaAgent, RouteOrcaSettings,
      1.0f / 30.0f, 0, RouteConstraint));
  TestEqual(TEXT("legacy route mode does not override generic responsibility"),
    RouteConstraint.Responsibility, 0.5f);
  TestTrue(TEXT("generic ORCA keeps the yielding-side constraint"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      YieldingRouteOrcaAgent, ActiveRouteOrcaAgent, RouteOrcaSettings,
      1.0f / 30.0f, 0, RouteConstraint));
  TestEqual(TEXT("generic reciprocal responsibility remains equal by default"),
    RouteConstraint.Responsibility, 0.5f);
  AdmissionAgents.SetNum(1);
  AdmissionAgents[0].State = ECrowdDemoPursuitPositionState::SlotCommit;
  AdmissionAgents[0].NoProgressSteps = 10;
  AdmissionSettings.FrontApproachNoProgressTimeoutSteps = 10;
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    FVector2f(0, -1), 10, AdmissionSettings,
    AdmissionCandidates, AdmissionAgents, AdmissionResultA);
  TestTrue(TEXT("timed-out commit is deterministically requeued"),
    AdmissionResultA.RequeuedAgentIds.Contains(AdmissionAgents[0].AgentId));

  const auto MakePhaseReservation = [](const int32 AgentId,
    const ECrowdDemoFrontApproachPhase RequestedPhase,
    const int32 CommitGrantedStep,
    const FVector2f CurrentA, const FVector2f CurrentB,
    const FVector2f RequestedA, const FVector2f RequestedB)
  {
    FCrowdDemoFrontPhaseReservationRequest Request;
    Request.AgentId = AgentId;
    Request.CommitGrantedStep = CommitGrantedStep;
    Request.CurrentPhase = ECrowdDemoFrontApproachPhase::RadialStage;
    Request.RequestedPhase = RequestedPhase;
    Request.bHasRequest = true;
    Request.bRequestValid = true;
    Request.bTargetExclusionClear = true;
    Request.CurrentReservationPoints = {CurrentA, CurrentB};
    Request.RequestedReservationPoints = {RequestedA, RequestedB};
    return Request;
  };

  TArray<FCrowdDemoFrontPhaseReservationRequest> PhaseRequests;
  PhaseRequests.Add(MakePhaseReservation(31, ECrowdDemoFrontApproachPhase::AngularAlign, 10,
    FVector2f(0, 0), FVector2f(100, 0),
    FVector2f(0, 300), FVector2f(100, 300)));
  PhaseRequests.Add(MakePhaseReservation(32, ECrowdDemoFrontApproachPhase::AngularAlign, 11,
    FVector2f(0, 600), FVector2f(100, 600),
    FVector2f(0, 900), FVector2f(100, 900)));
  FCrowdDemoFrontPhaseReservationResult PhaseResultA, PhaseResultB;
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
    Settings, PhaseRequests, PhaseResultA);
  TestEqual(TEXT("non-conflicting next-phase reservations grant concurrently"),
    PhaseResultA.GrantedAgentIds.Num(), 2);
  Algo::Reverse(PhaseRequests);
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
    Settings, PhaseRequests, PhaseResultB);
  TestEqual(TEXT("phase-reservation input order preserves decisions"),
    PhaseResultB.GrantedAgentIds, PhaseResultA.GrantedAgentIds);
  TestEqual(TEXT("phase-reservation input order preserves decision hash"),
    PhaseResultB.DecisionHash, PhaseResultA.DecisionHash);

  PhaseRequests.Reset();
  PhaseRequests.Add(MakePhaseReservation(41, ECrowdDemoFrontApproachPhase::RadialCommit, 10,
    FVector2f(0, 0), FVector2f(0, 100),
    FVector2f(500, 0), FVector2f(500, 100)));
  PhaseRequests.Add(MakePhaseReservation(42, ECrowdDemoFrontApproachPhase::AngularAlign, 10,
    FVector2f(500, 0), FVector2f(500, 100),
    FVector2f(1000, 0), FVector2f(1000, 100)));
  PhaseRequests.Add(MakePhaseReservation(43, ECrowdDemoFrontApproachPhase::RadialStage, 10,
    FVector2f(1500, 0), FVector2f(1500, 100),
    FVector2f(0, 0), FVector2f(0, 100)));
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
    Settings, PhaseRequests, PhaseResultA);
  TestTrue(TEXT("request conflicting with another current reservation is held"),
    PhaseResultA.HeldAgentIds.Contains(41));
  TestTrue(TEXT("held transition retains its current reservation for later requests"),
    PhaseResultA.HeldAgentIds.Contains(43));
  TestTrue(TEXT("non-conflicting owner may advance and replace its current reservation"),
    PhaseResultA.GrantedAgentIds.Contains(42));

  PhaseRequests.Reset();
  PhaseRequests.Add(MakePhaseReservation(51, ECrowdDemoFrontApproachPhase::RadialStage, 1,
    FVector2f(-500, 0), FVector2f(-500, 100),
    FVector2f(0, 0), FVector2f(0, 100)));
  PhaseRequests.Add(MakePhaseReservation(52, ECrowdDemoFrontApproachPhase::RadialCommit, 99,
    FVector2f(500, 0), FVector2f(500, 100),
    FVector2f(0, 0), FVector2f(0, 100)));
  FCrowdDemoFrontPhaseReservationRequest UnsafeRequest = MakePhaseReservation(
    53, ECrowdDemoFrontApproachPhase::RadialCommit, 0,
    FVector2f(1000, 0), FVector2f(1000, 100),
    FVector2f(1200, 0), FVector2f(1200, 100));
  UnsafeRequest.bTargetExclusionClear = false;
  PhaseRequests.Add(UnsafeRequest);
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
    Settings, PhaseRequests, PhaseResultA);
  TestTrue(TEXT("later phase wins before commit age for conflicting requests"),
    PhaseResultA.GrantedAgentIds.Contains(52));
  TestTrue(TEXT("lower phase remains held when its requested reservation conflicts"),
    PhaseResultA.HeldAgentIds.Contains(51));
  TestTrue(TEXT("target-exclusion failure invalidates phase transition"),
    PhaseResultA.InvalidAgentIds.Contains(53));
  Algo::Reverse(PhaseRequests);
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
    Settings, PhaseRequests, PhaseResultB);
  TestEqual(TEXT("priority and target gate remain stable under reversed input"),
    PhaseResultB.DecisionHash, PhaseResultA.DecisionHash);

  PhaseRequests.Reset();
  FCrowdDemoFrontPhaseReservationRequest CurrentOnly = MakePhaseReservation(
    61, ECrowdDemoFrontApproachPhase::AngularAlign, 1,
    FVector2f(0, 0), FVector2f(0, 100),
    FVector2f(500, 0), FVector2f(500, 100));
  CurrentOnly.bHasRequest = false;
  PhaseRequests.Add(CurrentOnly);
  FCrowdDemoFrontPhaseReservationRequest InitialEntry = MakePhaseReservation(
    62, ECrowdDemoFrontApproachPhase::RadialStage, INDEX_NONE,
    FVector2f(1000, 0), FVector2f(1000, 100),
    FVector2f(0, 0), FVector2f(0, 100));
  InitialEntry.CurrentPhase = ECrowdDemoFrontApproachPhase::None;
  InitialEntry.CurrentReservationPoints.Reset();
  PhaseRequests.Add(InitialEntry);
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
    Settings, PhaseRequests, PhaseResultA);
  TestEqual(TEXT("current-only owner is not counted as a phase request"),
    PhaseResultA.GrantedAgentIds.Num(), 0);
  TestTrue(TEXT("current-only reservation blocks conflicting initial RadialStage"),
    PhaseResultA.HeldAgentIds.Contains(62));

  FCrowdDemoFrontPhaseReservationState AppliedState;
  FCrowdDemoFrontPhaseReservationDecisionRecord AppliedDecision;
  AppliedDecision.AgentId = 70;
  AppliedDecision.CurrentPhase = ECrowdDemoFrontApproachPhase::None;
  AppliedDecision.RequestedPhase = ECrowdDemoFrontApproachPhase::RadialStage;
  AppliedDecision.Decision = ECrowdDemoFrontPhaseReservationDecision::Held;
  AppliedDecision.Reason = ECrowdDemoFrontPhaseReservationReason::RouteConflict;
  TestFalse(TEXT("Held does not transition the committed phase"),
    FCrowdDemoPursuitPositioningKernel::ApplyFrontPhaseReservationDecision(
      AppliedDecision, 100, AppliedState));
  TestEqual(TEXT("Held keeps the old committed phase"), AppliedState.CurrentPhase,
    ECrowdDemoFrontApproachPhase::None);
  TestEqual(TEXT("Held increments persistent held steps"), AppliedState.HeldSteps, 1);
  AppliedDecision.Decision = ECrowdDemoFrontPhaseReservationDecision::Granted;
  AppliedDecision.Reason = ECrowdDemoFrontPhaseReservationReason::None;
  TestTrue(TEXT("initial None to RadialStage changes only after Granted"),
    FCrowdDemoPursuitPositioningKernel::ApplyFrontPhaseReservationDecision(
      AppliedDecision, 101, AppliedState));
  TestEqual(TEXT("Granted commits RadialStage"), AppliedState.CurrentPhase,
    ECrowdDemoFrontApproachPhase::RadialStage);
  TestFalse(TEXT("duplicate boundary revision cannot apply the same request twice"),
    FCrowdDemoPursuitPositioningKernel::ApplyFrontPhaseReservationDecision(
      AppliedDecision, 101, AppliedState));
  AppliedDecision.CurrentPhase = ECrowdDemoFrontApproachPhase::RadialStage;
  AppliedDecision.RequestedPhase = ECrowdDemoFrontApproachPhase::AngularAlign;
  AppliedDecision.Decision = ECrowdDemoFrontPhaseReservationDecision::Invalid;
  AppliedDecision.Reason = ECrowdDemoFrontPhaseReservationReason::TargetExclusion;
  TestFalse(TEXT("Invalid request does not enter the requested phase"),
    FCrowdDemoPursuitPositioningKernel::ApplyFrontPhaseReservationDecision(
      AppliedDecision, 102, AppliedState));
  TestEqual(TEXT("Invalid keeps RadialStage committed"), AppliedState.CurrentPhase,
    ECrowdDemoFrontApproachPhase::RadialStage);
  AppliedDecision.Decision = ECrowdDemoFrontPhaseReservationDecision::Granted;
  AppliedDecision.Reason = ECrowdDemoFrontPhaseReservationReason::None;
  TestTrue(TEXT("RadialStage transitions to AngularAlign only after Granted"),
    FCrowdDemoPursuitPositioningKernel::ApplyFrontPhaseReservationDecision(
      AppliedDecision, 103, AppliedState));
  AppliedDecision.CurrentPhase = ECrowdDemoFrontApproachPhase::AngularAlign;
  AppliedDecision.RequestedPhase = ECrowdDemoFrontApproachPhase::RadialCommit;
  TestTrue(TEXT("AngularAlign transitions to RadialCommit only after Granted"),
    FCrowdDemoPursuitPositioningKernel::ApplyFrontPhaseReservationDecision(
      AppliedDecision, 104, AppliedState));

  FCrowdDemoFrontApproachRoute GuidanceRoute;
  GuidanceRoute.Phase = ECrowdDemoFrontApproachPhase::AngularAlign;
  GuidanceRoute.DesiredVelocity = FVector2f(0, 500);
  GuidanceRoute.OuterGate = FVector2f(0, 300);
  GuidanceRoute.RoutePoints = {FVector2f(300, 0), FVector2f(0, 300), FVector2f(0, 100)};
  const FVector2f HeldRadialGuidance =
    FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseDesiredVelocity(
      GuidanceRoute, ECrowdDemoFrontApproachPhase::RadialStage,
      FVector2f::ZeroVector, 800.0f);
  TestTrue(TEXT("Held RadialStage guidance targets its committed radial endpoint"),
    HeldRadialGuidance.X > 0.0f && FMath::IsNearlyZero(HeldRadialGuidance.Y));
  TArray<FVector2f> CommittedPoints;
  FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseReservationPoints(
    GuidanceRoute, ECrowdDemoFrontApproachPhase::RadialStage,
    FVector2f::ZeroVector, CommittedPoints);
  TestEqual(TEXT("committed RadialStage reservation excludes requested arc"),
    CommittedPoints.Num(), 2);

  TArray<FCrowdDemoFrontReservationWaitAgent> WaitAgents;
  FCrowdDemoFrontReservationWaitAgent WaitA;
  WaitA.AgentId = 81;
  WaitA.CurrentPhase = ECrowdDemoFrontApproachPhase::RadialStage;
  WaitA.RequestedPhase = ECrowdDemoFrontApproachPhase::AngularAlign;
  WaitA.Decision = ECrowdDemoFrontPhaseReservationDecision::Held;
  WaitA.bHasRequest = true;
  WaitA.bRequestValid = true;
  WaitA.bTargetExclusionClear = true;
  WaitA.HeldSteps = 40;
  WaitA.NoProgressSteps = 35;
  WaitA.RouteForwardVelocityBucket = 0;
  WaitA.CurrentReservationPoints = {FVector2f(0, 0), FVector2f(100, 0)};
  WaitA.RequestedReservationPoints = {FVector2f(0, 0), FVector2f(100, 100)};
  WaitAgents.Add(WaitA);
  FCrowdDemoFrontReservationWaitAgent WaitB = WaitA;
  WaitB.AgentId = 82;
  WaitB.CurrentPhase = ECrowdDemoFrontApproachPhase::AngularAlign;
  WaitB.RequestedPhase = ECrowdDemoFrontApproachPhase::RadialCommit;
  WaitB.HeldSteps = 20;
  WaitB.NoProgressSteps = 0;
  WaitB.RouteForwardVelocityBucket = 100;
  WaitB.RequestedReservationPoints = {FVector2f(500, 0), FVector2f(600, 100)};
  WaitAgents.Add(WaitB);
  TArray<FCrowdDemoFrontPhaseReservationBlockPair> WaitPairs;
  FCrowdDemoFrontPhaseReservationBlockPair WaitPairAB;
  WaitPairAB.RequesterAgentId = 81;
  WaitPairAB.BlockerAgentId = 82;
  WaitPairs.Add(WaitPairAB);
  FCrowdDemoFrontPhaseReservationBlockPair WaitPairBA;
  WaitPairBA.RequesterAgentId = 82;
  WaitPairBA.BlockerAgentId = 81;
  WaitPairs.Add(WaitPairBA);
  TArray<FCrowdDemoFrontReservationWaitEdge> WaitEdgesA, WaitEdgesB;
  FCrowdDemoFrontReservationWaitGraphSummary WaitSummaryA, WaitSummaryB;
  FCrowdDemoFrontReservationWaitGraphFixture WaitFixtureA, WaitFixtureB;
  FCrowdDemoPursuitTargetFact WaitTarget;
  WaitTarget.TargetId = 1;
  FCrowdDemoPursuitPositioningKernel::AnalyzeFrontReservationWaitGraph(
    WaitTarget, Settings, WaitAgents, WaitPairs, WaitEdgesA, WaitSummaryA, WaitFixtureA);
  TestEqual(TEXT("reciprocal wait edges form one stable SCC cycle"),
    WaitSummaryA.CycleCount, 1);
  TestEqual(TEXT("wait cycle has two owners"), WaitSummaryA.MaxCycleSize, 2);
  TestEqual(TEXT("cycle requested paths form one safe atomic handoff set"),
    WaitSummaryA.AtomicHandoffCycleCount, 1);
  TestEqual(TEXT("safe atomic handoff set contains both owners"),
    WaitSummaryA.MaxAtomicHandoffSetSize, 2);
  TestEqual(TEXT("reciprocal edge pair counted once"),
    WaitSummaryA.ReciprocalEdgeCount, 1);
  TestEqual(TEXT("wait graph classifies one stalled blocker"),
    WaitSummaryA.StalledBlockerCount, 1);
  TestEqual(TEXT("wait graph classifies one progressing blocker"),
    WaitSummaryA.ProgressingBlockerCount, 1);
  TestEqual(TEXT("wait graph records radial blocker phase"),
    WaitSummaryA.BlockerRadialCount, 1);
  TestEqual(TEXT("wait graph records angular blocker phase"),
    WaitSummaryA.BlockerAngularCount, 1);
  TestTrue(TEXT("minimal wait fixture contains only the selected relation"),
    WaitFixtureA.bValid && WaitFixtureA.Agents.Num() == 2 && WaitFixtureA.Edges.Num() == 1);
  Algo::Reverse(WaitAgents);
  Algo::Reverse(WaitPairs);
  FCrowdDemoPursuitPositioningKernel::AnalyzeFrontReservationWaitGraph(
    WaitTarget, Settings, WaitAgents, WaitPairs, WaitEdgesB, WaitSummaryB, WaitFixtureB);
  TestEqual(TEXT("wait graph input reversal preserves hash"),
    WaitSummaryB.WaitGraphHash, WaitSummaryA.WaitGraphHash);
  TestEqual(TEXT("wait fixture input reversal preserves hash"),
    WaitFixtureB.StableHash, WaitFixtureA.StableHash);
  TestEqual(TEXT("repeat analysis does not accumulate rollback edges"),
    WaitEdgesB.Num(), WaitEdgesA.Num());

  WaitPairs.SetNum(1);
  FCrowdDemoPursuitPositioningKernel::AnalyzeFrontReservationWaitGraph(
    WaitTarget, Settings, WaitAgents, WaitPairs, WaitEdgesB, WaitSummaryB, WaitFixtureB);
  TestEqual(TEXT("single direction dependency has no cycle"), WaitSummaryB.CycleCount, 0);
  WaitPairs[0].RequesterAgentId = 81;
  WaitPairs[0].BlockerAgentId = 999;
  FCrowdDemoPursuitPositioningKernel::AnalyzeFrontReservationWaitGraph(
    WaitTarget, Settings, WaitAgents, WaitPairs, WaitEdgesB, WaitSummaryB, WaitFixtureB);
  TestEqual(TEXT("missing blocker owner is classified stale"), WaitSummaryB.StaleOwnerCount, 1);

  FCrowdDemoSf4SourcedOrcaConstraint StableForwardBlock;
  StableForwardBlock.Source = ECrowdDemoSf4RouteConstraintSource::Stable;
  StableForwardBlock.Constraint.OtherAgentId = 90;
  StableForwardBlock.Constraint.Point = FVector2f::ZeroVector;
  StableForwardBlock.Constraint.Normal = FVector2f(-1, 0);
  StableForwardBlock.Constraint.StableConstraintOrder = 0;
  TArray<FCrowdDemoSf4SourcedOrcaConstraint> RouteConstraints = {StableForwardBlock};
  FCrowdDemoSf4RouteForwardFeasibilityResult RouteFeasibility =
    FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4RouteForwardFeasibility(
      FVector2f(100, 0), 100.0f, 30.0f, RouteConstraints, RouteOrcaSettings);
  TestFalse(TEXT("stable half-plane can eliminate all positive route-forward velocity"),
    RouteFeasibility.bContinuousFeasible);
  TestFalse(TEXT("blocked continuous fixture is also quantized infeasible"),
    RouteFeasibility.bQuantizedFeasible);
  TestTrue(TEXT("removing stable source restores route-forward feasibility"),
    RouteFeasibility.bFeasibleWithoutStable);
  RouteConstraints[0].Constraint.Normal = FVector2f(1, 0);
  RouteFeasibility = FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4RouteForwardFeasibility(
    FVector2f(100, 0), 100.0f, 30.0f, RouteConstraints, RouteOrcaSettings);
  TestTrue(TEXT("compatible half-plane preserves continuous route-forward velocity"),
    RouteFeasibility.bContinuousFeasible);
  TestTrue(TEXT("compatible route-forward witness survives quantization"),
    RouteFeasibility.bQuantizedFeasible);

  FCrowdDemoSf4SourcedOrcaConstraint RedundantConstraint;
  RedundantConstraint.Source = ECrowdDemoSf4RouteConstraintSource::Other;
  RedundantConstraint.Constraint.OtherAgentId = 91;
  RedundantConstraint.Constraint.Point = FVector2f(-100, 0);
  RedundantConstraint.Constraint.Normal = FVector2f(1, 0);
  RedundantConstraint.Constraint.StableConstraintOrder = 1;
  StableForwardBlock.Constraint.Normal = FVector2f(-1, 0);
  RouteConstraints = {RedundantConstraint, StableForwardBlock};
  const FCrowdDemoSf4RouteForwardFeasibilityResult CoreForward =
    FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4RouteForwardFeasibility(
      FVector2f(100, 0), 100.0f, 30.0f, RouteConstraints, RouteOrcaSettings);
  Algo::Reverse(RouteConstraints);
  const FCrowdDemoSf4RouteForwardFeasibilityResult CoreReverse =
    FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4RouteForwardFeasibility(
      FVector2f(100, 0), 100.0f, 30.0f, RouteConstraints, RouteOrcaSettings);
  TestEqual(TEXT("irreducible core removes a redundant half-plane"),
    CoreForward.IrreducibleCoreConstraintCount, 1);
  TestEqual(TEXT("route feasibility input reversal preserves hash"),
    CoreReverse.StableHash, CoreForward.StableHash);

  FCrowdDemoSf4ReservationOrcaFixtureAgent PrimaryFixtureAgent;
  PrimaryFixtureAgent.Agent.AgentId = 200;
  PrimaryFixtureAgent.Agent.Position = FVector2f(0, 0);
  PrimaryFixtureAgent.Agent.PreferredVelocity = FVector2f(100, 0);
  PrimaryFixtureAgent.Agent.RadiusCm = 10.0f;
  PrimaryFixtureAgent.Agent.MaxSpeedCmps = 100.0f;
  PrimaryFixtureAgent.Agent.Sf4RouteMode = ECrowdDemoOrcaRouteMode::Active;
  PrimaryFixtureAgent.Agent.Sf4RoutePoints = {FVector2f(0, 0), FVector2f(1000, 0)};
  PrimaryFixtureAgent.PositionState = ECrowdDemoPursuitPositionState::FrontCommitGranted;
  PrimaryFixtureAgent.CurrentPhase = ECrowdDemoFrontApproachPhase::RadialStage;
  PrimaryFixtureAgent.BaselineVelocity = FVector2f::ZeroVector;
  StableForwardBlock.Source = ECrowdDemoSf4RouteConstraintSource::Active;
  StableForwardBlock.Constraint.OtherAgentId = 201;
  StableForwardBlock.Constraint.StableConstraintOrder = 0;
  PrimaryFixtureAgent.Constraints = {StableForwardBlock};
  FCrowdDemoSf4ReservationOrcaFixtureAgent OtherFixtureAgent;
  OtherFixtureAgent.Agent.AgentId = 201;
  OtherFixtureAgent.Agent.Position = FVector2f(0, 200);
  OtherFixtureAgent.Agent.PreferredVelocity = FVector2f(100, 0);
  OtherFixtureAgent.Agent.RadiusCm = 10.0f;
  OtherFixtureAgent.Agent.MaxSpeedCmps = 100.0f;
  OtherFixtureAgent.Agent.Sf4RouteMode = ECrowdDemoOrcaRouteMode::Active;
  OtherFixtureAgent.Agent.Sf4RoutePoints = {FVector2f(0, 200), FVector2f(1000, 200)};
  OtherFixtureAgent.PositionState = ECrowdDemoPursuitPositionState::FrontCommitGranted;
  OtherFixtureAgent.CurrentPhase = ECrowdDemoFrontApproachPhase::RadialStage;
  OtherFixtureAgent.BaselineVelocity = FVector2f(100, 0);
  TArray<FCrowdDemoSf4ReservationOrcaFixtureAgent> ReservationAgents =
    {OtherFixtureAgent, PrimaryFixtureAgent};
  FCrowdDemoPursuitTargetFact FarTarget;
  FarTarget.TargetId = 7;
  FarTarget.Location = FVector2f(10000, 10000);
  FarTarget.RadiusCm = 50.0f;
  FCrowdDemoSf4ReservationOrcaDiagnosticFixture ReservationFixture;
  FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4ReservationOrcaDiagnostic(
    FarTarget, 10.0f, 1.0f / 30.0f, 30.0f, ReservationAgents, 200,
    RouteOrcaSettings, ReservationFixture);
  TestTrue(TEXT("disjoint active route fixture is valid"), ReservationFixture.bValid);
  TestEqual(TEXT("disjoint contained active constraint is classified"),
    ReservationFixture.Summary.ActiveRouteDisjointContainedCount, 1);
  TestTrue(TEXT("removing only disjoint contained active restores feasibility"),
    ReservationFixture.Summary.bOnlyDisjointContainedActiveRestoresFeasibility);
  const uint32 ReservationHash = ReservationFixture.StableHash;
  Algo::Reverse(ReservationAgents);
  FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4ReservationOrcaDiagnostic(
    FarTarget, 10.0f, 1.0f / 30.0f, 30.0f, ReservationAgents, 200,
    RouteOrcaSettings, ReservationFixture);
  TestEqual(TEXT("reservation fixture input reversal preserves hash"),
    ReservationFixture.StableHash, ReservationHash);
  TestTrue(TEXT("single-step velocity remains inside the first route segment"),
    FCrowdDemoDeterministicOrcaKernel::IsSf4ReservationStepContained(
      PrimaryFixtureAgent.Agent, FVector2f(100, 0), 1.0f / 30.0f, 10.0f));
  TestFalse(TEXT("lateral single-step velocity leaves the route corridor"),
    FCrowdDemoDeterministicOrcaKernel::IsSf4ReservationStepContained(
      PrimaryFixtureAgent.Agent, FVector2f(0, 200), 1.0f / 30.0f, 10.0f));
  TestTrue(TEXT("separated reservation steps remain pair safe"),
    FCrowdDemoDeterministicOrcaKernel::AreSf4ReservationStepsPairSafe(
      PrimaryFixtureAgent.Agent, FVector2f(100, 0), OtherFixtureAgent.Agent,
      FVector2f(100, 0), 1.0f / 30.0f));
  OtherFixtureAgent.Agent.Position = FVector2f(0, 20);
  TestFalse(TEXT("overlapping swept reservation steps are rejected"),
    FCrowdDemoDeterministicOrcaKernel::AreSf4ReservationStepsPairSafe(
      PrimaryFixtureAgent.Agent, FVector2f(100, 0), OtherFixtureAgent.Agent,
      FVector2f(100, 0), 1.0f / 30.0f));
  OtherFixtureAgent.Agent.Position = FVector2f(0, 210);
  ReservationAgents = {PrimaryFixtureAgent, OtherFixtureAgent};
  FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4ReservationOrcaDiagnostic(
    FarTarget, 10.0f, 1.0f / 30.0f, 30.0f, ReservationAgents, 200,
    RouteOrcaSettings, ReservationFixture);
  TestEqual(TEXT("disjoint active agent outside corridor is classified"),
    ReservationFixture.Summary.ActiveRouteDisjointOutsideCorridorCount, 1);
  OtherFixtureAgent.Agent.Position = FVector2f(500, -100);
  OtherFixtureAgent.Agent.Sf4RoutePoints = {FVector2f(500, -100), FVector2f(500, 100)};
  ReservationAgents = {PrimaryFixtureAgent, OtherFixtureAgent};
  FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4ReservationOrcaDiagnostic(
    FarTarget, 10.0f, 1.0f / 30.0f, 30.0f, ReservationAgents, 200,
    RouteOrcaSettings, ReservationFixture);
  TestEqual(TEXT("intersecting active reservation paths are classified as conflict"),
    ReservationFixture.Summary.ActiveRouteConflictCount, 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf4SteeringFirstHoldingCommitTest,
  "CrowdDemo.SF4.Positioning.SteeringFirstHoldingCommit",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf4SteeringFirstHoldingCommitTest::RunTest(const FString& Parameters)
{
  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("Small shared flow fixture builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1), Field));
  FCrowdDemoPursuitTargetFact Target;
  Target.TargetId = 700;
  Target.Revision = 3;
  Target.Location = FVector2f(2200, 1600);
  Target.RadiusCm = 80.0f;
  FCrowdDemoPursuitPositioningSettings Settings;
  constexpr float AgentRadiusCm = 42.0f;
  TArray<FCrowdDemoPositionCandidate> Positions;
  FCrowdDemoPositioningSummary PositionSummary;
  FCrowdDemoPursuitPositioningKernel::BuildCandidates(
    Target, AgentRadiusCm, Settings, Field, Positions, PositionSummary);
  TestTrue(TEXT("Small position fixture covers 20 assignments"), Positions.Num() >= 20);
  TSet<int32> UniquePositionIds;
  for (const FCrowdDemoPositionCandidate& Position : Positions)
  {
    TestFalse(TEXT("position candidate id is collision free"),
      UniquePositionIds.Contains(Position.PositionId));
    UniquePositionIds.Add(Position.PositionId);
  }
  FCrowdDemoPositionCandidate EntrySidePosition;
  EntrySidePosition.WorldLocation = Target.Location + FVector2f(0, -300);
  FCrowdDemoPositionCandidate FarSidePosition;
  FarSidePosition.WorldLocation = Target.Location + FVector2f(0, 300);
  TestTrue(TEXT("stable fill cost prioritizes far side before entry side"),
    FCrowdDemoPursuitPositioningKernel::ComputePositionFillCost(
      Target, FVector2f(0, -1), FarSidePosition)
      < FCrowdDemoPursuitPositioningKernel::ComputePositionFillCost(
        Target, FVector2f(0, -1), EntrySidePosition));

  TArray<FCrowdDemoHoldingCandidate> HoldingsA, HoldingsB;
  FCrowdDemoHoldingSummary HoldingSummaryA, HoldingSummaryB;
  FCrowdDemoPursuitPositioningKernel::BuildHoldingCandidates(
    Target, AgentRadiusCm, Settings, Field, Positions, HoldingsA, HoldingSummaryA);
  TestTrue(TEXT("Small geometry produces at least 20 legal holding candidates"),
    HoldingsA.Num() >= 20);
  TSet<int32> UniqueHoldingCandidateIds;
  const float HoldingSpacing = 2.0f * AgentRadiusCm + Settings.HoldingGapCm;
  for (int32 A = 0; A < HoldingsA.Num(); ++A)
  {
    const FCrowdDemoHoldingCandidate& Candidate = HoldingsA[A];
    TestFalse(TEXT("holding candidate id is collision free"),
      UniqueHoldingCandidateIds.Contains(Candidate.HoldingId));
    UniqueHoldingCandidateIds.Add(Candidate.HoldingId);
    TestTrue(TEXT("holding candidate remains reachable and clear"),
      Candidate.bReachable && Candidate.bClearanceValid);
    TestTrue(TEXT("holding candidate remains outside target exclusion"),
      (Candidate.WorldLocation - Target.Location).Size()
        >= Target.RadiusCm + AgentRadiusCm + Settings.SafetyGapCm);
    for (int32 B = A + 1; B < HoldingsA.Num(); ++B)
      TestTrue(TEXT("holding candidates have no pair overlap"),
        (Candidate.WorldLocation - HoldingsA[B].WorldLocation).Size()
          >= HoldingSpacing);
  }
  TArray<FCrowdDemoPositionCandidate> ReversedPositions = Positions;
  Algo::Reverse(ReversedPositions);
  FCrowdDemoPursuitPositioningKernel::BuildHoldingCandidates(
    Target, AgentRadiusCm, Settings, Field, ReversedPositions, HoldingsB, HoldingSummaryB);
  TestEqual(TEXT("position input reversal preserves holding candidate hash"),
    HoldingSummaryB.CandidateHash, HoldingSummaryA.CandidateHash);
  TestEqual(TEXT("position input reversal preserves holding candidate order"),
    HoldingsB.Num(), HoldingsA.Num());
  for (int32 Index = 0; Index < FMath::Min(HoldingsA.Num(), HoldingsB.Num()); ++Index)
    TestEqual(TEXT("holding id order remains stable"),
      HoldingsB[Index].HoldingId, HoldingsA[Index].HoldingId);

  TArray<FCrowdDemoPositionCandidate> AssignedPositions;
  for (int32 Index = 0; Index < 20 && Index < Positions.Num(); ++Index)
    AssignedPositions.Add(Positions[Index]);
  TArray<FCrowdDemoHoldingPositionCompatibility> Compatibility;
  TMap<int32, int32> CompatibleCountByPosition;
  bool bFoundTargetBlockedPair = false;
  for (const FCrowdDemoPositionCandidate& Position : AssignedPositions)
  {
    for (const FCrowdDemoHoldingCandidate& Holding : HoldingsA)
    {
      FCrowdDemoHoldingPositionCompatibility Edge =
        FCrowdDemoPursuitPositioningKernel::EvaluateHoldingPositionCompatibility(
          Target, AgentRadiusCm, Settings, Field, Holding, Position, {});
      if (Edge.bCompatible)
      {
        Compatibility.Add(Edge);
        ++CompatibleCountByPosition.FindOrAdd(Position.PositionId);
      }
      bFoundTargetBlockedPair |= !Edge.bTargetClear;
    }
    TestTrue(TEXT("each assigned position has a legal holding edge"),
      CompatibleCountByPosition.FindRef(Position.PositionId) > 0);
  }
  TestTrue(TEXT("incompatible target-crossing straight line is rejected"),
    bFoundTargetBlockedPair);

  TArray<FCrowdDemoHoldingAgent> HoldingAgents;
  for (int32 Index = 0; Index < AssignedPositions.Num(); ++Index)
  {
    const FCrowdDemoPositionCandidate& Position = AssignedPositions[Index];
    FCrowdDemoHoldingAgent& Agent = HoldingAgents.AddDefaulted_GetRef();
    Agent.AgentId = 1000 + Index;
    Agent.Location = FVector2f(2200, 0);
    Agent.RadiusCm = AgentRadiusCm;
    Agent.WaitEpoch = 20 - Index;
    Agent.PositionId = Position.PositionId;
    Agent.AssignedPosition = Position.WorldLocation;
    Agent.PositionRole = Position.Role;
    Agent.PositionIngressCost =
      FCrowdDemoPursuitPositioningKernel::ComputePositionFillCost(
        Target, FVector2f(0, -1), Position);
    Agent.bPositionValid = true;
  }
  TArray<FCrowdDemoHoldingAssignment> AssignmentsA, AssignmentsB;
  FCrowdDemoPursuitPositioningKernel::AssignHoldingPositions(
    Target, HoldingAgents, HoldingsA, Compatibility, AssignmentsA, HoldingSummaryA);
  TestEqual(TEXT("all 20 agents receive one holding assignment"), AssignmentsA.Num(), 20);
  TSet<int32> AssignedHoldingIds;
  for (const FCrowdDemoHoldingAssignment& Assignment : AssignmentsA)
  {
    TestTrue(TEXT("holding assignment carries selected compatibility proof"),
      Assignment.bCompatibilityValid && Assignment.CompatibilityHash != 0);
    TestFalse(TEXT("one holding has only one owner"),
      AssignedHoldingIds.Contains(Assignment.HoldingId));
    AssignedHoldingIds.Add(Assignment.HoldingId);
  }
  TestEqual(TEXT("selected compatibility proofs are all valid"),
    HoldingSummaryA.SelectedCompatibilityValidCount, 20);
  TestEqual(TEXT("selected compatibility proofs contain no invalid edge"),
    HoldingSummaryA.SelectedCompatibilityInvalidCount, 0);
  for (FCrowdDemoHoldingAgent& Agent : HoldingAgents)
  {
    const FCrowdDemoHoldingAssignment* Existing = AssignmentsA.FindByPredicate(
      [&](const auto& Assignment) { return Assignment.AgentId == Agent.AgentId; });
    if (!Existing) continue;
    Agent.ExistingHoldingId = Existing->HoldingId;
    Agent.ExistingTargetRevision = Target.Revision;
    Agent.ExistingState = ECrowdDemoPursuitSteeringState::Holding;
  }
  FCrowdDemoPursuitPositioningKernel::AssignHoldingPositions(
    Target, HoldingAgents, HoldingsA, Compatibility, AssignmentsB, HoldingSummaryB);
  TestEqual(TEXT("repeat assignment preserves all 20 owners"), AssignmentsB.Num(), 20);
  TestEqual(TEXT("repeat assignment reuses every legal owner"),
    HoldingSummaryB.ReusedCount, 20);
  const uint32 StableAssignmentHash = HoldingSummaryB.AssignmentHash;
  Algo::Reverse(HoldingAgents);
  TArray<FCrowdDemoHoldingCandidate> ReversedHoldings = HoldingsA;
  Algo::Reverse(ReversedHoldings);
  TArray<FCrowdDemoHoldingPositionCompatibility> ReversedCompatibility = Compatibility;
  Algo::Reverse(ReversedCompatibility);
  FCrowdDemoPursuitPositioningKernel::AssignHoldingPositions(
    Target, HoldingAgents, ReversedHoldings, ReversedCompatibility,
    AssignmentsB, HoldingSummaryB);
  TestEqual(TEXT("holding assignment hash ignores input order"),
    HoldingSummaryB.AssignmentHash, StableAssignmentHash);
  TArray<FCrowdDemoHoldingAgent> StaleRevisionAgents = HoldingAgents;
  for (FCrowdDemoHoldingAgent& Agent : StaleRevisionAgents)
    Agent.ExistingTargetRevision = Target.Revision - 1;
  FCrowdDemoPursuitPositioningKernel::AssignHoldingPositions(
    Target, StaleRevisionAgents, HoldingsA, Compatibility,
    AssignmentsB, HoldingSummaryB);
  TestEqual(TEXT("target revision invalidates all old holding ownership"),
    HoldingSummaryB.ReusedCount, 0);

  const FCrowdDemoHoldingAssignment& BlockedAssignment = AssignmentsA[0];
  const FCrowdDemoHoldingCandidate* BlockedHolding = HoldingsA.FindByPredicate(
    [&](const auto& Holding) { return Holding.HoldingId == BlockedAssignment.HoldingId; });
  const FCrowdDemoPositionCandidate* BlockedPosition = AssignedPositions.FindByPredicate(
    [&](const auto& Position) { return Position.PositionId == BlockedAssignment.PositionId; });
  TestNotNull(TEXT("assigned holding exists"), BlockedHolding);
  TestNotNull(TEXT("assigned position exists"), BlockedPosition);
  if (BlockedHolding && BlockedPosition)
  {
    FCrowdDemoPositionIngressBlocker StableBlocker;
    StableBlocker.AgentId = 9999;
    StableBlocker.State = ECrowdDemoPursuitPositionState::StableOccupied;
    StableBlocker.Location = (BlockedHolding->WorldLocation + BlockedPosition->WorldLocation) * 0.5f;
    StableBlocker.RadiusCm = AgentRadiusCm;
    const FCrowdDemoHoldingPositionCompatibility BlockedEdge =
      FCrowdDemoPursuitPositioningKernel::EvaluateHoldingPositionCompatibility(
        Target, AgentRadiusCm, Settings, Field, *BlockedHolding, *BlockedPosition,
        MakeArrayView(&StableBlocker, 1));
    TestFalse(TEXT("stable blocker invalidates holding-position compatibility"),
      BlockedEdge.bCompatible);
    TestFalse(TEXT("stable blocker is explicitly classified"),
      BlockedEdge.bStableBlockerClear);
    TArray<FCrowdDemoHoldingPositionCompatibility> RebuiltCompatibility = Compatibility;
    const int32 BlockedEdgeIndex = RebuiltCompatibility.IndexOfByPredicate(
      [&](const auto& Edge)
      {
        return Edge.HoldingId == BlockedEdge.HoldingId
          && Edge.PositionId == BlockedEdge.PositionId;
      });
    TestTrue(TEXT("selected compatibility edge exists before blocker rebuild"),
      BlockedEdgeIndex != INDEX_NONE);
    if (BlockedEdgeIndex != INDEX_NONE)
      RebuiltCompatibility[BlockedEdgeIndex] = BlockedEdge;
    TArray<FCrowdDemoHoldingAssignment> ReassignedAfterBlocker;
    FCrowdDemoHoldingSummary ReassignedSummary;
    FCrowdDemoPursuitPositioningKernel::AssignHoldingPositions(
      Target, HoldingAgents, HoldingsA, RebuiltCompatibility,
      ReassignedAfterBlocker, ReassignedSummary);
    const FCrowdDemoHoldingAssignment* ReassignedBlockedAgent =
      ReassignedAfterBlocker.FindByPredicate([&](const auto& Assignment)
      { return Assignment.AgentId == BlockedAssignment.AgentId; });
    TestNotNull(TEXT("blocked owner receives a deterministic alternative"), ReassignedBlockedAgent);
    if (ReassignedBlockedAgent)
      TestNotEqual(TEXT("blocked owner releases stale holding edge"),
        ReassignedBlockedAgent->HoldingId, BlockedAssignment.HoldingId);
  }

  TArray<FCrowdDemoCommitRequest> CommitRequests;
  for (int32 Index = 0; Index < FMath::Min(8, AssignmentsA.Num()); ++Index)
  {
    const FCrowdDemoHoldingAssignment& Assignment = AssignmentsA[Index];
    FCrowdDemoCommitRequest& Request = CommitRequests.AddDefaulted_GetRef();
    Request.AgentId = Assignment.AgentId;
    Request.HoldingId = Assignment.HoldingId;
    Request.PositionId = Assignment.PositionId;
    Request.TargetRevision = Target.Revision;
    Request.WaitEpoch = 100 - Index;
    Request.PositionFillCost = HoldingAgents.FindByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Assignment.AgentId; })->PositionIngressCost;
    Request.QuantizedCommitCostCm = Assignment.IntegerCost;
    Request.Location = Request.HoldingLocation = Assignment.HoldingLocation;
    Request.AssignedPosition = Assignment.AssignedPosition;
    Request.RadiusCm = AgentRadiusCm;
    Request.bPositionValid = true;
    Request.bCompatibilityFound = true;
    Request.bCompatibilityValid = true;
  }
  FCrowdDemoCommitGateResult CommitA, CommitB;
  FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
    Target, Settings, Field, CommitRequests, {}, CommitA);
  TestTrue(TEXT("commit gate grants at least one compatible ready request"),
    !CommitA.GrantedAgentIds.IsEmpty());
  TestEqual(TEXT("ready grants are classified"), CommitA.ReadyGrantedCount,
    CommitA.GrantedAgentIds.Num());
  TestEqual(TEXT("all held requests in ready fixture are conflicts"),
    CommitA.ReadyConflictHeldCount, CommitA.HeldCount);
  TestEqual(TEXT("ready fixture conflicts are selected-this-boundary conflicts"),
    CommitA.ReadySelectedConflictCount, CommitA.HeldCount);
  for (int32 A = 0; A < CommitA.GrantedAgentIds.Num(); ++A)
    for (int32 B = A + 1; B < CommitA.GrantedAgentIds.Num(); ++B)
    {
      const FCrowdDemoCommitRequest* RequestA = CommitRequests.FindByPredicate(
        [&](const auto& Request) { return Request.AgentId == CommitA.GrantedAgentIds[A]; });
      const FCrowdDemoCommitRequest* RequestB = CommitRequests.FindByPredicate(
        [&](const auto& Request) { return Request.AgentId == CommitA.GrantedAgentIds[B]; });
      TestFalse(TEXT("granted commit segments are pairwise non-conflicting"),
        FCrowdDemoPursuitPositioningKernel::FrontReservationPathsConflict(
          Settings, TArray<FVector2f>{RequestA->Location, RequestA->AssignedPosition},
          RequestA->RadiusCm,
          TArray<FVector2f>{RequestB->Location, RequestB->AssignedPosition},
          RequestB->RadiusCm));
    }
  Algo::Reverse(CommitRequests);
  FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
    Target, Settings, Field, CommitRequests, {}, CommitB);
  TestEqual(TEXT("commit gate input reversal preserves decision hash"),
    CommitB.DecisionHash, CommitA.DecisionHash);

  if (!CommitRequests.IsEmpty())
  {
    FCrowdDemoCommitRequest HighPriority = CommitRequests[0];
    FCrowdDemoCommitRequest LowPriority = CommitRequests[0];
    HighPriority.AgentId = 7701;
    HighPriority.WaitEpoch = 10;
    LowPriority.AgentId = 7702;
    LowPriority.WaitEpoch = 9;
    TArray<FCrowdDemoCommitRequest> PriorityRequests = {LowPriority, HighPriority};
    FCrowdDemoCommitGateResult PriorityResult;
    FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
      Target, Settings, Field, PriorityRequests, {}, PriorityResult);
    TestTrue(TEXT("higher WaitEpoch wins a conflicting commit segment"),
      PriorityResult.GrantedAgentIds.Contains(HighPriority.AgentId));
    TestFalse(TEXT("lower WaitEpoch remains held behind the selected segment"),
      PriorityResult.GrantedAgentIds.Contains(LowPriority.AgentId));
  }

  if (!CommitA.GrantedAgentIds.IsEmpty())
  {
    const int32 GrantedId = CommitA.GrantedAgentIds[0];
    const FCrowdDemoCommitRequest* GrantedRequest = CommitRequests.FindByPredicate(
      [&](const auto& Request) { return Request.AgentId == GrantedId; });
    if (GrantedRequest)
    {
      FCrowdDemoCommitRequest FutureWaiting = *GrantedRequest;
      FutureWaiting.AgentId = 7777;
      FutureWaiting.WaitEpoch = MAX_int32;
      FutureWaiting.HoldingLocation += FVector2f(1000, 0);
      TArray<FCrowdDemoCommitRequest> FutureDoesNotReserve = {FutureWaiting, *GrantedRequest};
      FCrowdDemoCommitGateResult FutureResult;
      FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
        Target, Settings, Field, FutureDoesNotReserve, {}, FutureResult);
      TestTrue(TEXT("non-ready waiting future path does not occupy commit resource"),
        FutureResult.GrantedAgentIds.Contains(GrantedRequest->AgentId));

      FCrowdDemoCommitRequest ActiveOwner = *GrantedRequest;
      ActiveOwner.AgentId = 7778;
      ActiveOwner.bAlreadyCommit = true;
      FCrowdDemoCommitRequest WaitingOwner = *GrantedRequest;
      WaitingOwner.AgentId = 7779;
      TArray<FCrowdDemoCommitRequest> WithOwner = {ActiveOwner, WaitingOwner};
      FCrowdDemoCommitGateResult WithOwnerResult;
      FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
        Target, Settings, Field, WithOwner, {}, WithOwnerResult);
      TestFalse(TEXT("active commit owner blocks conflicting ready request"),
        WithOwnerResult.GrantedAgentIds.Contains(WaitingOwner.AgentId));
      TestEqual(TEXT("active commit conflict source classified"),
        WithOwnerResult.ReadyActiveCommitConflictCount, 1);
      FCrowdDemoCommitGateResult ReleasedOwnerResult;
      FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
        Target, Settings, Field, MakeArrayView(&WaitingOwner, 1), {}, ReleasedOwnerResult);
      TestTrue(TEXT("owner removal releases commit segment without ghost ownership"),
        ReleasedOwnerResult.GrantedAgentIds.Contains(WaitingOwner.AgentId));

      FCrowdDemoPositionIngressBlocker YieldableBlocker;
      YieldableBlocker.AgentId = 7780;
      YieldableBlocker.State = ECrowdDemoPursuitPositionState::StableOccupied;
      YieldableBlocker.Location = (WaitingOwner.Location + WaitingOwner.AssignedPosition) * 0.5f;
      YieldableBlocker.RadiusCm = WaitingOwner.RadiusCm;
      FCrowdDemoCommitGateResult YieldableStableResult;
      FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
        Target, Settings, Field, MakeArrayView(&WaitingOwner, 1),
        MakeArrayView(&YieldableBlocker, 1), YieldableStableResult);
      TestTrue(TEXT("stable blocker is yieldable and does not reject commit"),
        YieldableStableResult.GrantedAgentIds.Contains(WaitingOwner.AgentId));
      TestEqual(TEXT("stable blocker is counted as yieldable"),
        YieldableStableResult.YieldableStableConflictCount, 1);
      TestEqual(TEXT("stable blocker is absent from hard reject mask"),
        YieldableStableResult.Decisions[0].RejectReasonMask
          & static_cast<uint32>(ECrowdDemoCommitRejectReason::StableBlocker), 0u);
      TestTrue(TEXT("stable blocker remains observable in yieldable mask"),
        (YieldableStableResult.Decisions[0].YieldableConflictMask
          & static_cast<uint32>(ECrowdDemoCommitRejectReason::StableBlocker)) != 0);
      TestNotEqual(TEXT("yieldable conflict participates in decision hash"),
        YieldableStableResult.DecisionHash, ReleasedOwnerResult.DecisionHash);

      YieldableBlocker.State = ECrowdDemoPursuitPositionState::ReserveHold;
      FCrowdDemoCommitGateResult YieldableReserveResult;
      FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
        Target, Settings, Field, MakeArrayView(&WaitingOwner, 1),
        MakeArrayView(&YieldableBlocker, 1), YieldableReserveResult);
      TestTrue(TEXT("reserve blocker is yieldable and does not reject commit"),
        YieldableReserveResult.GrantedAgentIds.Contains(WaitingOwner.AgentId));
      TestEqual(TEXT("reserve blocker is counted as yieldable"),
        YieldableReserveResult.YieldableReserveConflictCount, 1);
      TestTrue(TEXT("reserve blocker remains observable in yieldable mask"),
        (YieldableReserveResult.Decisions[0].YieldableConflictMask
          & static_cast<uint32>(ECrowdDemoCommitRejectReason::ReserveBlocker)) != 0);
    }
  }

  if (!CommitRequests.IsEmpty())
  {
    FCrowdDemoCommitRequest TargetCrossing = CommitRequests[0];
    TargetCrossing.AgentId = 8800;
    TargetCrossing.Location = Target.Location + FVector2f(-1000.0f, 0.0f);
    TargetCrossing.HoldingLocation = TargetCrossing.Location;
    TargetCrossing.AssignedPosition = Target.Location + FVector2f(1000.0f, 0.0f);
    FCrowdDemoCommitGateResult TargetRejectResult;
    FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
      Target, Settings, Field, MakeArrayView(&TargetCrossing, 1), {}, TargetRejectResult);
    TestEqual(TEXT("target exclusion commit rejection classified"),
      TargetRejectResult.ReadyTargetRejectCount, 1);

    FCrowdDemoCommitRequest DistanceNotReady = CommitRequests[0];
    DistanceNotReady.Location = DistanceNotReady.HoldingLocation
      + FVector2f(Settings.HoldingToleranceCm + 1.0f, 0.0f);
    FCrowdDemoCommitRequest SpeedNotReady = CommitRequests[0];
    SpeedNotReady.AgentId += 100000;
    SpeedNotReady.Velocity = FVector2f(Settings.HoldingReadinessSpeedCmps + 1.0f, 0.0f);
    FCrowdDemoCommitGateResult ReadinessResult;
    FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
      Target, Settings, Field, TArray<FCrowdDemoCommitRequest>{DistanceNotReady, SpeedNotReady},
      {}, ReadinessResult);
    TestEqual(TEXT("distance readiness rejection classified"),
      ReadinessResult.HoldingDistanceNotReadyCount, 1);
    TestEqual(TEXT("speed readiness rejection classified"),
      ReadinessResult.HoldingSpeedNotReadyCount, 1);
    TestTrue(TEXT("distance reject reason retained per agent"),
      (ReadinessResult.Decisions[0].RejectReasonMask
        & static_cast<uint32>(ECrowdDemoCommitRejectReason::HoldingDistance)) != 0);
    TestTrue(TEXT("speed reject reason retained per agent"),
      (ReadinessResult.Decisions[1].RejectReasonMask
        & static_cast<uint32>(ECrowdDemoCommitRejectReason::HoldingSpeed)) != 0);

    FCrowdDemoCommitRequest InvalidPosition = CommitRequests[0];
    InvalidPosition.AgentId = 8888;
    InvalidPosition.bPositionValid = false;
    FCrowdDemoCommitGateResult InvalidResult;
    FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
      Target, Settings, Field, MakeArrayView(&InvalidPosition, 1), {}, InvalidResult);
    TestEqual(TEXT("invalid position enters Reacquire"),
      InvalidResult.Decisions[0].Decision, ECrowdDemoCommitDecision::Reacquire);
    TestTrue(TEXT("invalid position reject reason retained"),
      (InvalidResult.Decisions[0].RejectReasonMask
        & static_cast<uint32>(ECrowdDemoCommitRejectReason::InvalidPosition)) != 0);
    InvalidPosition.bPositionValid = true;
    InvalidPosition.TargetRevision = Target.Revision - 1;
    FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
      Target, Settings, Field, MakeArrayView(&InvalidPosition, 1), {}, InvalidResult);
    TestEqual(TEXT("target revision change invalidates old commit"),
      InvalidResult.Decisions[0].Decision, ECrowdDemoCommitDecision::Reacquire);
  }

  const FVector2f FlowVelocity(0, 500);
  const FVector2f HoldingLocation(1000, 0);
  const FVector2f PositionLocation(2000, 0);
  const FVector2f HoldingVelocity =
    FCrowdDemoPursuitPositioningKernel::BuildSteeringFirstPreferredVelocity(
      ECrowdDemoPursuitSteeringState::Holding, FVector2f::ZeroVector,
      FlowVelocity, HoldingLocation, PositionLocation, 800.0f, Settings);
  TestTrue(TEXT("holding guidance points to holding rather than goal flow"),
    HoldingVelocity.X > 0.0f && FMath::IsNearlyZero(HoldingVelocity.Y));
  TestTrue(TEXT("holding guidance stops inside tolerance"),
    FCrowdDemoPursuitPositioningKernel::BuildSteeringFirstPreferredVelocity(
      ECrowdDemoPursuitSteeringState::Holding,
      HoldingLocation + FVector2f(Settings.HoldingToleranceCm - 1.0f, 0),
      FlowVelocity, HoldingLocation, PositionLocation, 800.0f, Settings).IsNearlyZero());
  const FVector2f CommitVelocity =
    FCrowdDemoPursuitPositioningKernel::BuildSteeringFirstPreferredVelocity(
      ECrowdDemoPursuitSteeringState::Commit, FVector2f::ZeroVector,
      FlowVelocity, HoldingLocation, PositionLocation, 800.0f, Settings);
  TestTrue(TEXT("commit guidance points only to assigned position"),
    CommitVelocity.X > 0.0f && FMath::IsNearlyZero(CommitVelocity.Y));
  TestTrue(TEXT("steering outputs remain speed limited"),
    CommitVelocity.Size() <= 800.0f);
  TestFalse(TEXT("far agent remains on shared flow before holding handoff"),
    FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
      FVector2f::ZeroVector, FVector2f(Settings.FrontAdmissionHoldRangeCm + 1.0f, 0.0f),
      ECrowdDemoFlowLocationStatus::Reachable, Settings, Field.Config));
  TestTrue(TEXT("reachable agent at handoff boundary may enter holding"),
    FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
      FVector2f(1200.0f, 1000.0f),
      FVector2f(1200.0f + Settings.FrontAdmissionHoldRangeCm, 1000.0f),
      ECrowdDemoFlowLocationStatus::Reachable, Settings, Field.Config));
  TestFalse(TEXT("invalid flow sample cannot claim holding handoff"),
    FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
      FVector2f::ZeroVector, FVector2f(1.0f, 0.0f),
      ECrowdDemoFlowLocationStatus::UnreachableFreeCell, Settings, Field.Config));
  TestFalse(TEXT("reachable handoff cannot cross inflated obstacle 109"),
    FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
      FVector2f(100.0f, 451.0f), FVector2f(150.0f, 950.0f),
      ECrowdDemoFlowLocationStatus::Reachable, Settings, Field.Config));
  TestTrue(TEXT("reachable handoff may use the clear gap beside obstacle 109"),
    FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
      FVector2f(1500.0f, 451.0f), FVector2f(1500.0f, 950.0f),
      ECrowdDemoFlowLocationStatus::Reachable, Settings, Field.Config));

  TArray<FCrowdDemoSf4UnfinishedAgentDiagnosticInput> UnfinishedInputs;
  {
    auto& Holding = UnfinishedInputs.AddDefaulted_GetRef();
    Holding.AgentId = 22;
    Holding.State = ECrowdDemoPursuitSteeringState::Holding;
    Holding.Location = FVector2f(10.2f, 20.4f);
    Holding.Destination = FVector2f(110.2f, 20.4f);
    Holding.PreferredVelocity = FVector2f(80.4f, -1.2f);
    Holding.OrcaVelocity = FVector2f(20.4f, 2.4f);
    Holding.FinalVelocity = FVector2f(19.6f, 2.4f);
    Holding.OrcaConstraintSourceCounts = {1,2,3,4,5,6};
    Holding.CommitRejectReasonMask = static_cast<uint32>(
      ECrowdDemoCommitRejectReason::HoldingDistance);
    Holding.NoProgressSteps = 31;
    auto& Stable = UnfinishedInputs.AddDefaulted_GetRef();
    Stable.AgentId = 11;
    Stable.State = ECrowdDemoPursuitSteeringState::StableOccupied;
  }
  FCrowdDemoSf4UnfinishedBoundaryFixture UnfinishedForward;
  FCrowdDemoPursuitPositioningKernel::BuildUnfinishedBoundaryFixture(
    UnfinishedInputs, UnfinishedForward);
  TestTrue(TEXT("unfinished final-boundary fixture valid"), UnfinishedForward.bValid);
  TestEqual(TEXT("completed states excluded from unfinished fixture"),
    UnfinishedForward.Agents.Num(), 1);
  TestEqual(TEXT("unfinished distance quantized to centimeters"),
    UnfinishedForward.Agents[0].DistanceCm, 100);
  TestEqual(TEXT("unfinished preferred velocity quantized"),
    UnfinishedForward.Agents[0].PreferredVelocityCmps, FIntPoint(80,-1));
  const uint32 UnfinishedHash = UnfinishedForward.StableHash;
  Algo::Reverse(UnfinishedInputs);
  FCrowdDemoSf4UnfinishedBoundaryFixture UnfinishedReverse;
  FCrowdDemoPursuitPositioningKernel::BuildUnfinishedBoundaryFixture(
    UnfinishedInputs, UnfinishedReverse);
  TestEqual(TEXT("unfinished fixture input reversal preserves hash"),
    UnfinishedReverse.StableHash, UnfinishedHash);

  TArray<FCrowdDemoSf4PhysicalUnsatisfiedAgentInput> PhysicalInputs;
  {
    auto& Stable = PhysicalInputs.AddDefaulted_GetRef();
    Stable.AgentId = 31;
    Stable.State = ECrowdDemoPursuitSteeringState::StableOccupied;
    Stable.PositionId = 1031;
    Stable.HoldingId = 2031;
    Stable.Location = FVector2f(10.0f, 10.0f);
    Stable.Destination = FVector2f(20.0f, 10.0f);
    Stable.bPhysicallySatisfied = true;
    auto& Reserve = PhysicalInputs.AddDefaulted_GetRef();
    Reserve.AgentId = 12;
    Reserve.State = ECrowdDemoPursuitSteeringState::ReserveHold;
    Reserve.PositionId = 1012;
    Reserve.HoldingId = 2012;
    Reserve.Location = FVector2f::ZeroVector;
    Reserve.Destination = FVector2f(44.6f, 0.0f);
    Reserve.PreferredVelocity = FVector2f(1.2f, -2.6f);
    Reserve.OrcaVelocity = FVector2f(3.4f, 4.6f);
    Reserve.ObstacleVelocity = FVector2f(5.4f, 6.6f);
    Reserve.PbdVelocity = FVector2f(7.4f, 8.6f);
    Reserve.ReprojectVelocity = FVector2f(9.4f, 10.6f);
    Reserve.FinalVelocity = FVector2f(11.4f, 12.6f);
    Reserve.CommitRejectReasonMask = 17;
    Reserve.CommitYieldableConflictMask = 23;
    Reserve.InvalidReason = 4;
    auto& Holding = PhysicalInputs.AddDefaulted_GetRef();
    Holding.AgentId = 22;
    Holding.State = ECrowdDemoPursuitSteeringState::Holding;
    Holding.PositionId = 1022;
    Holding.HoldingId = 2022;
    Holding.Destination = FVector2f(100.0f, 0.0f);
  }
  FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture PhysicalForward;
  FCrowdDemoPursuitPositioningKernel::BuildPhysicalUnsatisfiedBoundaryFixture(
    PhysicalInputs, PhysicalForward);
  TestTrue(TEXT("physical-unsatisfied fixture valid"), PhysicalForward.bValid);
  TestTrue(TEXT("physical-unsatisfied fixture closes total minus satisfied"),
    PhysicalForward.bCountClosed);
  TestEqual(TEXT("physical-unsatisfied fixture keeps displaced completed states"),
    PhysicalForward.Agents.Num(), 2);
  TestEqual(TEXT("physical-unsatisfied fixture records total agents"),
    PhysicalForward.TotalAgentCount, 3);
  TestEqual(TEXT("physical-unsatisfied fixture records satisfied agents"),
    PhysicalForward.PhysicallySatisfiedCount, 1);
  TestEqual(TEXT("physical-unsatisfied fixture sorts by AgentId"),
    PhysicalForward.Agents[0].AgentId, 12);
  TestEqual(TEXT("physical-unsatisfied fixture records fixed PositionId"),
    PhysicalForward.Agents[0].PositionId, 1012);
  TestEqual(TEXT("physical-unsatisfied fixture records HoldingId"),
    PhysicalForward.Agents[0].HoldingId, 2012);
  TestEqual(TEXT("physical-unsatisfied fixture records obstacle velocity"),
    PhysicalForward.Agents[0].ObstacleVelocityCmps, FIntPoint(5,7));
  TestEqual(TEXT("physical-unsatisfied fixture records yieldable mask"),
    PhysicalForward.Agents[0].CommitYieldableConflictMask, 23u);
  const uint32 PhysicalHash = PhysicalForward.StableHash;
  Algo::Reverse(PhysicalInputs);
  FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture PhysicalReverse;
  FCrowdDemoPursuitPositioningKernel::BuildPhysicalUnsatisfiedBoundaryFixture(
    PhysicalInputs, PhysicalReverse);
  TestEqual(TEXT("physical-unsatisfied input reversal preserves hash"),
    PhysicalReverse.StableHash, PhysicalHash);

  if (!HoldingAgents.IsEmpty())
  {
    const int32 RemovedAgentId = HoldingAgents[0].AgentId;
    HoldingAgents.RemoveAt(0);
    FCrowdDemoPursuitPositioningKernel::AssignHoldingPositions(
      Target, HoldingAgents, HoldingsA, Compatibility, AssignmentsB, HoldingSummaryB);
    TestFalse(TEXT("removed membership leaves no holding ghost owner"),
      AssignmentsB.ContainsByPredicate([&](const auto& Assignment)
        { return Assignment.AgentId == RemovedAgentId; }));
    TestEqual(TEXT("surviving owners remain assigned after membership removal"),
      AssignmentsB.Num(), 19);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoScenarioRegistryTest,
  "CrowdDemo.SF.Parser.AcceptsOnlyCurrentScenarios",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoScenarioRegistryTest::RunTest(const FString& Parameters)
{
  ECrowdDemoScenario Scenario = ECrowdDemoScenario::SimRoundObstacle;
  TestTrue(TEXT("0"), CrowdDemoScenarioRegistry::TryParse(TEXT("0"), Scenario));
  TestEqual(TEXT("0 maps to SF1"), Scenario, ECrowdDemoScenario::SimRoundObstacle);
  TestTrue(TEXT("SF2 name"), CrowdDemoScenarioRegistry::TryParse(TEXT("SimRoundSoftPressure"), Scenario));
  TestEqual(TEXT("SF2 name maps to SF2"), Scenario, ECrowdDemoScenario::SimRoundSoftPressure);
  TestTrue(TEXT("2"), CrowdDemoScenarioRegistry::TryParse(TEXT("2"), Scenario));
  TestEqual(TEXT("2 maps to SF3"), Scenario, ECrowdDemoScenario::SimRoundCrowdTraffic);
  TestTrue(TEXT("SF3 name"), CrowdDemoScenarioRegistry::TryParse(TEXT("SimRoundCrowdTraffic"), Scenario));
  TestTrue(TEXT("3"), CrowdDemoScenarioRegistry::TryParse(TEXT("3"), Scenario));
  TestEqual(TEXT("3 maps to SF4"), Scenario, ECrowdDemoScenario::SimRoundPursuitPositioning);
  TestTrue(TEXT("SF4 name"), CrowdDemoScenarioRegistry::TryParse(TEXT("SimRoundPursuitPositioning"), Scenario));
  TestFalse(TEXT("legacy numeric rejected"), CrowdDemoScenarioRegistry::TryParse(TEXT("16"), Scenario));
  TestFalse(TEXT("legacy name rejected"), CrowdDemoScenarioRegistry::TryParse(TEXT("PredictiveHeadOn"), Scenario));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPbdGridDeterminismTest,
  "CrowdDemo.SF.PBD.GridMatchesBruteForceAndInputOrder",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPbdGridDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoHardSeparationPbdAgent> Agents;
  for (int32 Index = 0; Index < 64; ++Index)
  {
    FCrowdDemoHardSeparationPbdAgent& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = 1000 + Index * 7;
    Agent.Location = FVector((Index % 8) * 73.0f, (Index / 8) * 79.0f, 60.0f);
    Agent.RadiusCm = 36.0f + static_cast<float>(Index % 4) * 3.0f;
  }
  FCrowdDemoHardSeparationPbdSettings Settings;
  Settings.IterationCount = 3;
  Settings.MaxPairCorrectionPerIterationCm = 24.0f;
  TArray<FCrowdDemoHardSeparationPbdPair> GridPairs;
  TArray<FCrowdDemoHardSeparationPbdResult> Results;
  FCrowdDemoHardSeparationPbdSummary Summary;
  FCrowdDemoHardSeparationPbdKernel::Solve(Agents, Settings, GridPairs, Results, Summary);

  TSet<uint64> ExpectedPairs;
  const float Margin = Settings.MaxPairCorrectionPerIterationCm * Settings.IterationCount * 4.0f;
  for (int32 A = 0; A < Agents.Num(); ++A)
  {
    for (int32 B = A + 1; B < Agents.Num(); ++B)
    {
      const float CandidateDistance = Agents[A].RadiusCm + Agents[B].RadiusCm + Margin;
      if (FVector::DistSquared2D(Agents[A].Location, Agents[B].Location) <= FMath::Square(CandidateDistance))
      {
        const uint32 MinId = static_cast<uint32>(FMath::Min(Agents[A].AgentId, Agents[B].AgentId));
        const uint32 MaxId = static_cast<uint32>(FMath::Max(Agents[A].AgentId, Agents[B].AgentId));
        ExpectedPairs.Add((static_cast<uint64>(MinId) << 32) | MaxId);
      }
    }
  }
  TSet<uint64> ActualPairs;
  for (const FCrowdDemoHardSeparationPbdPair& Pair : GridPairs)
  {
    ActualPairs.Add((static_cast<uint64>(static_cast<uint32>(Pair.MinAgentId)) << 32)
      | static_cast<uint32>(Pair.MaxAgentId));
  }
  bool bPairSetsMatch = ActualPairs.Num() == ExpectedPairs.Num();
  for (const uint64 PairKey : ExpectedPairs)
  {
    bPairSetsMatch = bPairSetsMatch && ActualPairs.Contains(PairKey);
  }
  TestTrue(TEXT("grid candidate set equals brute force"), bPairSetsMatch);

  Algo::Reverse(Agents);
  TArray<FCrowdDemoHardSeparationPbdPair> ShuffledPairs;
  TArray<FCrowdDemoHardSeparationPbdResult> ShuffledResults;
  FCrowdDemoHardSeparationPbdSummary ShuffledSummary;
  FCrowdDemoHardSeparationPbdKernel::Solve(Agents, Settings, ShuffledPairs, ShuffledResults, ShuffledSummary);
  TestEqual(TEXT("pair count independent of input order"), ShuffledPairs.Num(), GridPairs.Num());
  TestEqual(TEXT("corrected pair count independent of input order"), ShuffledSummary.CorrectedPairCount, Summary.CorrectedPairCount);
  TestEqual(TEXT("pair correction metric has cap semantics"), Summary.MaxPairCorrectionCm, 24.0f);
  TestTrue(TEXT("agent total metric is independently populated"), Summary.MaxAgentTotalCorrectionCm > 0.0f);
  for (int32 Index = 0; Index < Results.Num(); ++Index)
  {
    TestEqual(TEXT("stable result id"), ShuffledResults[Index].AgentId, Results[Index].AgentId);
    TestTrue(TEXT("stable corrected location"), ShuffledResults[Index].CorrectedLocation.Equals(Results[Index].CorrectedLocation, 0.001f));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOverlapGridDeterminismTest,
  "CrowdDemo.SF3.Overlap.GridMatchesBruteForceAndInputOrder",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOverlapGridDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoHardSeparationPbdAgent> Agents;
  for (int32 Index = 0; Index < 80; ++Index)
  {
    FCrowdDemoHardSeparationPbdAgent& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = 500 + Index * 3;
    Agent.Location = FVector((Index % 10) * 55.0f, (Index / 10) * 61.0f, 60.0f);
  }
  constexpr float Threshold = 78.0f;
  TArray<FCrowdDemoHardSeparationPbdPair> GridPairs;
  FCrowdDemoHardSeparationPbdKernel::BuildOverlapPairs(Agents, Threshold, GridPairs);
  TSet<uint64> BruteForce;
  for (int32 A = 0; A < Agents.Num(); ++A)
  {
    for (int32 B = A + 1; B < Agents.Num(); ++B)
    {
      if (FVector::DistSquared2D(Agents[A].Location, Agents[B].Location) < FMath::Square(Threshold))
      {
        const uint32 MinId = FMath::Min(Agents[A].AgentId, Agents[B].AgentId);
        const uint32 MaxId = FMath::Max(Agents[A].AgentId, Agents[B].AgentId);
        BruteForce.Add((static_cast<uint64>(MinId) << 32) | MaxId);
      }
    }
  }
  TestEqual(TEXT("overlap grid pair count equals brute force"), GridPairs.Num(), BruteForce.Num());
  for (const FCrowdDemoHardSeparationPbdPair& Pair : GridPairs)
  {
    const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Pair.MinAgentId)) << 32)
      | static_cast<uint32>(Pair.MaxAgentId);
    TestTrue(TEXT("overlap grid pair exists in brute force"), BruteForce.Contains(Key));
  }
  Algo::Reverse(Agents);
  TArray<FCrowdDemoHardSeparationPbdPair> ReorderedPairs;
  FCrowdDemoHardSeparationPbdKernel::BuildOverlapPairs(Agents, Threshold, ReorderedPairs);
  TestEqual(TEXT("overlap pair count ignores input order"), ReorderedPairs.Num(), GridPairs.Num());
  for (int32 Index = 0; Index < GridPairs.Num(); ++Index)
  {
    TestEqual(TEXT("overlap MinAgentId stable"), ReorderedPairs[Index].MinAgentId, GridPairs[Index].MinAgentId);
    TestEqual(TEXT("overlap MaxAgentId stable"), ReorderedPairs[Index].MaxAgentId, GridPairs[Index].MaxAgentId);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPipelineBoundaryAndFormationCacheTest,
  "CrowdDemo.SF.Pipeline.PlanApplyBoundaryAndFormationCache",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPipelineBoundaryAndFormationCacheTest::RunTest(const FString& Parameters)
{
  UCrowdDemoRoundSimPipelineSubsystem* DuePipeline = NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  FCrowdDemoRoundPlanPacket FuturePlan;
  FuturePlan.bValid = 1;
  FuturePlan.RoundId = 1;
  FuturePlan.Revision = 1;
  FuturePlan.StartServerTimeSeconds = 10.0f;
  DuePipeline->QueueRoundPlan(FuturePlan);
  TestFalse(TEXT("future plan is not due"), DuePipeline->HasDueRoundPlan(9.0f));
  TestTrue(TEXT("future plan becomes due at its start"), DuePipeline->HasDueRoundPlan(10.0f));

  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  TestTrue(TEXT("first claim at boundary succeeds"), Pipeline->TryClaimPlanApplyBoundary());
  TestFalse(TEXT("second claim at same boundary is rejected"), Pipeline->TryClaimPlanApplyBoundary());

  FCrowdDemoRoundPlanPacket Plan;
  Plan.RoundId = 1;
  Plan.Revision = 1;
  Plan.StartServerTimeSeconds = 0.0f;
  Plan.DurationSeconds = 1.0f;
  Plan.Rules.FixedStepSeconds = 1.0f / 30.0f;
  Pipeline->ActivatePlan(Plan, 3, false);
  TestTrue(TEXT("fixed step begins"), Pipeline->TryBeginFixedStep(1.0f));
  Pipeline->FinishFixedStep();
  TestTrue(TEXT("claim succeeds after next fixed-step boundary"), Pipeline->TryClaimPlanApplyBoundary());
  TestFalse(TEXT("new boundary also accepts only one claim"), Pipeline->TryClaimPlanApplyBoundary());
  FCrowdDemoRoundResultPacket LateResult;
  LateResult.bValid = 1;
  LateResult.RoundId = 1;
  LateResult.Revision = 1;
  LateResult.CheckpointRevision = 1;
  Pipeline->QueueRoundResult(LateResult);
  TestTrue(TEXT("late result reopens a stationary boundary"), Pipeline->TryClaimPlanApplyBoundary());
  TestFalse(TEXT("late result work is claimed once"), Pipeline->TryClaimPlanApplyBoundary());

  const TArray<int32> AgentIds = {30, 10, 20};
  Pipeline->EnsureFormationIndexCache(AgentIds);
  TestEqual(TEXT("initial cache rebuild"), Pipeline->GetFormationCacheRebuildCount(), 1);
  TestEqual(TEXT("stable sorted formation index"), Pipeline->GetFormationIndexByAgentId().FindRef(10), 0);
  const TArray<int32> ReorderedAgentIds = {20, 30, 10};
  Pipeline->EnsureFormationIndexCache(ReorderedAgentIds);
  TestEqual(TEXT("input reorder does not rebuild cache"), Pipeline->GetFormationCacheRebuildCount(), 1);
  const TArray<int32> ChangedAgentIds = {20, 30, 40};
  Pipeline->EnsureFormationIndexCache(ChangedAgentIds);
  TestEqual(TEXT("membership change rebuilds cache"), Pipeline->GetFormationCacheRebuildCount(), 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCrossRoundErrorTrendTest,
  "CrowdDemo.SF.Correction.CrossRoundErrorTrend",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCrossRoundErrorTrendTest::RunTest(const FString& Parameters)
{
  FCrowdDemoRoundErrorSeries StableSeries;
  StableSeries.Record(0.40f);
  StableSeries.Record(0.45f);
  StableSeries.Record(0.42f);
  TestFalse(TEXT("sub-centimeter stable sequence does not expand past tolerance"), StableSeries.IsExpanding(0.10f));
  TestEqual(TEXT("stable series max"), StableSeries.GetMax(), 0.45f);

  FCrowdDemoRoundErrorSeries ExpandingSeries;
  ExpandingSeries.Record(0.20f);
  ExpandingSeries.Record(0.55f);
  ExpandingSeries.Record(1.10f);
  TestTrue(TEXT("expanding sequence is detected"), ExpandingSeries.IsExpanding(0.10f));
  TestEqual(TEXT("expansion is measured from first checkpoint"), ExpandingSeries.GetExpansionFromFirst(), 0.90f);
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
  }
  Frame.AgentCount = Frame.AgentStates.Num();

  FCrowdDemoCorrectionFrameHeader Header;
  TArray<FCrowdDemoCorrectionFrameChunk> Chunks;
  FCrowdDemoRoundCheckpointTransport::BuildChunks(Frame, 100, Header, Chunks);
  TestEqual(TEXT("500 states produce five chunks"), Chunks.Num(), 5);
  TestEqual(TEXT("header preserves checkpoint kind"), Header.FrameKind, ECrowdDemoRoundFrameKind::RoundResultCheckpoint);

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
  }

  TArray<FCrowdDemoCorrectionFrameChunk> MissingChunks = Chunks;
  MissingChunks.RemoveAll([](const FCrowdDemoCorrectionFrameChunk& Chunk) { return Chunk.ChunkIndex == 2; });
  TestFalse(TEXT("missing chunk is rejected"),
    FCrowdDemoRoundCheckpointTransport::TryAssemble(Header, MissingChunks, Assembled));
  TArray<FCrowdDemoCorrectionFrameChunk> MismatchedChunks = Chunks;
  MismatchedChunks[0].RoundRevision = 4;
  TestFalse(TEXT("revision mismatch is rejected"),
    FCrowdDemoRoundCheckpointTransport::TryAssemble(Header, MismatchedChunks, Assembled));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTrafficFieldDeterminismTest,
  "CrowdDemo.SF3.Traffic.FieldAndPortalDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTrafficFieldDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoTrafficAgent> Agents;
  for (int32 Index=0; Index<64; ++Index)
  {
    FCrowdDemoTrafficAgent& Agent=Agents.AddDefaulted_GetRef();
    Agent.AgentId=100+Index;
    Agent.Position=FVector2f(-1200.0f+(Index%16)*90.0f,-2800.0f+(Index/16)*90.0f);
    Agent.Velocity=FVector2f(10.0f+Index,20.0f-Index);
    Agent.FlowDirection=FVector2f(0.0f,1.0f);
  }
  const FCrowdDemoSharedFlowFieldConfig Config=FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  FCrowdDemoTrafficSettings Settings;
  TArray<FCrowdDemoTrafficCell> CellsA,CellsB;
  uint32 HashA=0,HashB=0;
  FCrowdDemoTrafficSchedulingKernel::BuildTrafficCells(Agents,Config,Settings,CellsA,HashA);
  Algo::Reverse(Agents);
  FCrowdDemoTrafficSchedulingKernel::BuildTrafficCells(Agents,Config,Settings,CellsB,HashB);
  TestEqual(TEXT("traffic hash ignores input order"),HashB,HashA);
  TestEqual(TEXT("traffic cells ignore input order"),CellsB.Num(),CellsA.Num());

  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("SF1 flow builds"),FCrowdDemoSharedFlowFieldKernel::Build(Config,Field));
  TArray<FCrowdDemoTrafficPortal> PortalsA,PortalsB;
  FCrowdDemoPortalExtractionSummary PortalSummaryA, PortalSummaryB;
  FCrowdDemoTrafficSchedulingKernel::ExtractPortals(Field,Settings,PortalsA,&PortalSummaryA,1);
  FCrowdDemoTrafficSchedulingKernel::ExtractPortals(Field,Settings,PortalsB,&PortalSummaryB,1);
  TestEqual(TEXT("SF3 obstacle geometry has three physical portals"),PortalsA.Num(),3);
  TestEqual(TEXT("SF3 portal geometry golden hash"),PortalSummaryA.GeometryHash,1962319733u);
  const int32 ExpectedPortalIds[] = {322861801,344220896,416677724};
  TestEqual(TEXT("portal extraction stable count"),PortalsB.Num(),PortalsA.Num());
  TestEqual(TEXT("portal extraction stable geometry hash"),PortalSummaryB.GeometryHash,PortalSummaryA.GeometryHash);
  for (int32 Index=0;Index<PortalsA.Num();++Index)
  {
    TestEqual(TEXT("portal id matches geometry golden"),PortalsA[Index].PortalId,ExpectedPortalIds[Index]);
    TestEqual(TEXT("portal id stable"),PortalsB[Index].PortalId,PortalsA[Index].PortalId);
    TestTrue(TEXT("portal is a strict local clearance minimum"),
      PortalsA[Index].UpstreamWidthCells > PortalsA[Index].WidthCells
        && PortalsA[Index].DownstreamWidthCells > PortalsA[Index].WidthCells);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoFlowReachabilityDomainTest,
  "CrowdDemo.SF3.Flow.ReachabilityDomainClassification",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoFlowReachabilityDomainTest::RunTest(const FString& Parameters)
{
  FCrowdDemoSharedFlowFieldConfig RasterConfig;
  RasterConfig.BoundsMin=FVector(0,0,0);
  RasterConfig.BoundsMax=FVector(300,300,0);
  RasterConfig.CellSizeCm=100.0f;
  RasterConfig.AgentInflateCm=0.0f;
  RasterConfig.GoalLocation=FVector(250,250,0);
  FCrowdDemoSharedFlowObstacleSpec& Obstacle=RasterConfig.ObstacleSpecs.AddDefaulted_GetRef();
  Obstacle.ObstacleId=1;
  Obstacle.Center=FVector(150,150,0);
  Obstacle.Extent=FVector(40,40,10);
  FCrowdDemoSharedFlowField RasterField;
  TestTrue(TEXT("raster mismatch field builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(RasterConfig,RasterField));
  const FVector Start(50,50,0);
  const FVector ContinuousLegal(101,101,0);
  const auto Continuous=FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
    RasterConfig,Start,ContinuousLegal,1.0f/30.0f);
  const auto Blocked=FCrowdDemoSharedFlowFieldKernel::Sample(RasterField,Continuous.Location);
  TestFalse(TEXT("continuous domain accepts raster-blocked world point"),Continuous.bHitObstacle);
  TestEqual(TEXT("raster-blocked status is explicit"),Blocked.Status,
    ECrowdDemoFlowLocationStatus::BlockedRasterCell);
  TestFalse(TEXT("raster-blocked world point is not continuously penetrating"),
    FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(RasterConfig,Continuous.Location));

  TestEqual(TEXT("PBD correction can enter blocked raster cell"),
    FCrowdDemoSharedFlowFieldKernel::Sample(RasterField,FVector(101,101,0)).Status,
    ECrowdDemoFlowLocationStatus::BlockedRasterCell);

  const auto OutOfBoundsConstraint=FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
    RasterConfig,FVector(250,50,0),FVector(350,50,0),1.0f/30.0f);
  TestFalse(TEXT("continuous obstacle constraint does not enforce flow bounds"),
    OutOfBoundsConstraint.bHitObstacle);
  TestEqual(TEXT("out of bounds status is explicit"),
    FCrowdDemoSharedFlowFieldKernel::Sample(RasterField,OutOfBoundsConstraint.Location).Status,
    ECrowdDemoFlowLocationStatus::OutOfBounds);
  const auto BoundsConstrained=FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
    RasterConfig,FVector(250,50,0),FVector(350,70,0),1.0f/30.0f,true);
  TestTrue(TEXT("flow bounds constraint reports reproject"),BoundsConstrained.bHitFlowBounds);
  TestTrue(TEXT("flow bounds constraint has positive delta"),
    BoundsConstrained.FlowBoundsReprojectDeltaCm>49.0f);
  TestTrue(TEXT("flow bounds constraint preserves tangential component"),
    FMath::IsNearlyEqual(BoundsConstrained.Location.Y,70.0f,0.01f));
  TestEqual(TEXT("flow bounds constrained position stays in domain"),
    FCrowdDemoSharedFlowFieldKernel::Sample(RasterField,BoundsConstrained.Location).Status,
    ECrowdDemoFlowLocationStatus::Reachable);

  FCrowdDemoSharedFlowFieldConfig DiagnosticConfig;
  DiagnosticConfig.BoundsMin = FVector(0, 0, 0);
  DiagnosticConfig.BoundsMax = FVector(1000, 1000, 0);
  DiagnosticConfig.AgentInflateCm = 50.0f;
  FCrowdDemoSharedFlowObstacleSpec& DiagnosticObstacle =
    DiagnosticConfig.ObstacleSpecs.AddDefaulted_GetRef();
  DiagnosticObstacle.ObstacleId = 109;
  DiagnosticObstacle.Center = FVector(500, 500, 0);
  DiagnosticObstacle.Extent = FVector(100, 100, 10);
  const FCrowdDemoSharedFlowConstraintDiagnostic ConstraintDiagnostic =
    FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
      DiagnosticConfig, FVector(300, 500, 0), FVector(700, 520, 0), true);
  TestTrue(TEXT("obstacle constraint diagnostic valid"), ConstraintDiagnostic.bValid);
  TestEqual(TEXT("obstacle constraint diagnostic identifies stable obstacle id"),
    ConstraintDiagnostic.SelectedObstacleId, 109);
  TestFalse(TEXT("obstacle constraint diagnostic direct segment blocked"),
    ConstraintDiagnostic.bDirectSegmentClear);
  TestFalse(TEXT("diagnostic start remains outside inflated obstacle"),
    ConstraintDiagnostic.bStartInsideSelectedObstacle);
  TestFalse(TEXT("diagnostic end remains outside inflated obstacle"),
    ConstraintDiagnostic.bEndInsideSelectedObstacle);
  TestTrue(TEXT("diagnostic records entry interval"),
    ConstraintDiagnostic.SelectedSegmentEntryT >= 0.0f
      && ConstraintDiagnostic.SelectedSegmentEntryT
        < ConstraintDiagnostic.SelectedSegmentExitT);
  TestFalse(TEXT("diagnostic rejects blocked X slide"), ConstraintDiagnostic.bSlideXClear);
  TestTrue(TEXT("diagnostic accepts clear Y slide"), ConstraintDiagnostic.bSlideYClear);
  TestEqual(TEXT("diagnostic records intersected obstacle ids"),
    ConstraintDiagnostic.IntersectedObstacleIds.Num(), 1);
  const uint32 ConstraintDiagnosticHash = ConstraintDiagnostic.StableHash;
  FCrowdDemoSharedFlowObstacleSpec& FarObstacle =
    DiagnosticConfig.ObstacleSpecs.AddDefaulted_GetRef();
  FarObstacle.ObstacleId = 101;
  FarObstacle.Center = FVector(900, 900, 0);
  FarObstacle.Extent = FVector(10, 10, 10);
  Algo::Reverse(DiagnosticConfig.ObstacleSpecs);
  const FCrowdDemoSharedFlowConstraintDiagnostic ReorderedConstraintDiagnostic =
    FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
      DiagnosticConfig, FVector(300, 500, 0), FVector(700, 520, 0), true);
  TestEqual(TEXT("diagnostic hash ignores nonintersecting obstacle order"),
    ReorderedConstraintDiagnostic.StableHash, ConstraintDiagnosticHash);

  FCrowdDemoSharedFlowFieldConfig IslandConfig;
  IslandConfig.BoundsMin=FVector(0,0,0);
  IslandConfig.BoundsMax=FVector(500,300,0);
  IslandConfig.CellSizeCm=100.0f;
  IslandConfig.AgentInflateCm=0.0f;
  IslandConfig.GoalLocation=FVector(450,150,0);
  FCrowdDemoSharedFlowObstacleSpec& Barrier=IslandConfig.ObstacleSpecs.AddDefaulted_GetRef();
  Barrier.ObstacleId=2;
  Barrier.Center=FVector(250,150,0);
  Barrier.Extent=FVector(49,149,10);
  FCrowdDemoSharedFlowField IslandField;
  TestTrue(TEXT("free island field builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(IslandConfig,IslandField));
  const auto Island=FCrowdDemoSharedFlowFieldKernel::Sample(IslandField,FVector(50,150,0));
  TestEqual(TEXT("free island is distinct from blocked raster"),Island.Status,
    ECrowdDemoFlowLocationStatus::UnreachableFreeCell);
  TestFalse(TEXT("free island cell is not blocked"),Island.bBlocked);

  const auto SearchA=FCrowdDemoSharedFlowFieldKernel::FindNearestReachableCell(
    RasterField,ContinuousLegal,4);
  const auto SearchB=FCrowdDemoSharedFlowFieldKernel::FindNearestReachableCell(
    RasterField,ContinuousLegal,4);
  TestTrue(TEXT("nearest reachable recovery candidate exists"),SearchA.bFound);
  TestEqual(TEXT("nearest reachable stable key repeats"),SearchB.StableCellKey,SearchA.StableCellKey);
  FCrowdDemoSharedFlowField Sf1;
  TestTrue(TEXT("SF1 golden field builds"),FCrowdDemoSharedFlowFieldKernel::Build(
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1),Sf1));
  TestEqual(TEXT("SF1 build hash remains golden"),Sf1.BuildHash,267519150u);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPortalExtractionGeometryTest,
  "CrowdDemo.SF3.Traffic.PortalExtractionGeometry",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPortalExtractionGeometryTest::RunTest(const FString& Parameters)
{
  auto BuildSyntheticField = [](const TArray<int32>& WidthByColumn, const int32 Height)
  {
    FCrowdDemoSharedFlowField Field;
    Field.Config.BoundsMin = FVector::ZeroVector;
    Field.Config.BoundsMax = FVector(WidthByColumn.Num() * 100.0f, Height * 100.0f, 0.0f);
    Field.Config.CellSizeCm = 100.0f;
    Field.Width = WidthByColumn.Num();
    Field.Height = Height;
    const int32 Count = Field.Width * Field.Height;
    Field.Blocked.Init(true, Count);
    Field.NextCellIndex.Init(INDEX_NONE, Count);
    Field.IntegrationCost.Init(0, Count);
    Field.FlowDirection.Init(FVector(1,0,0), Count);
    Field.Unreachable.Init(false, Count);
    for (int32 X = 0; X < Field.Width; ++X)
    {
      const int32 SpanWidth = WidthByColumn[X];
      const int32 MinY = (Height - SpanWidth) / 2;
      for (int32 Y = MinY; Y < MinY + SpanWidth; ++Y)
      {
        Field.Blocked[Y * Field.Width + X] = false;
      }
    }
    for (int32 X = 0; X + 1 < Field.Width; ++X)
    {
      for (int32 Y = 0; Y < Height; ++Y)
      {
        const int32 Key = Y * Field.Width + X;
        const int32 Next = Key + 1;
        if (!Field.Blocked[Key] && !Field.Blocked[Next]) Field.NextCellIndex[Key] = Next;
      }
    }
    Field.GoalCellIndex = Height / 2 * Field.Width + Field.Width - 1;
    return Field;
  };

  FCrowdDemoTrafficSettings Settings;
  Settings.PortalMaxWidthCells = 8;
  Settings.PortalClearanceWindowCells = 3;
  TArray<FCrowdDemoTrafficPortal> Portals;
  FCrowdDemoPortalExtractionSummary Summary;
  FCrowdDemoSharedFlowField Single = BuildSyntheticField({9,9,9,3,3,3,3,9,9,9}, 11);
  FCrowdDemoTrafficSchedulingKernel::ExtractPortals(Single, Settings, Portals, &Summary, 0);
  TestEqual(TEXT("single plateau produces one portal"), Portals.Num(), 1);
  TestTrue(TEXT("plateau merges multiple cross sections"), Portals.Num() == 1 && Portals[0].MergedCandidateCount > 1);

  FCrowdDemoSharedFlowField Open = BuildSyntheticField({9,9,9,9,9,9,9}, 11);
  FCrowdDemoTrafficSchedulingKernel::ExtractPortals(Open, Settings, Portals, &Summary, 0);
  TestEqual(TEXT("open area produces no portal"), Portals.Num(), 0);

  FCrowdDemoSharedFlowField Two = BuildSyntheticField(
    {9,9,9,3,3,9,9,9,9,4,4,9,9,9}, 11);
  FCrowdDemoTrafficSchedulingKernel::ExtractPortals(Two, Settings, Portals, &Summary, 0);
  TestEqual(TEXT("two independent bottlenecks produce two portals"), Portals.Num(), 2);
  const uint32 FirstHash = Summary.GeometryHash;
  TArray<FCrowdDemoTrafficPortal> Rebuilt;
  FCrowdDemoPortalExtractionSummary RebuiltSummary;
  FCrowdDemoTrafficSchedulingKernel::ExtractPortals(Two, Settings, Rebuilt, &RebuiltSummary, 0);
  TestEqual(TEXT("two rebuilds preserve geometry hash"), RebuiltSummary.GeometryHash, FirstHash);
  TestEqual(TEXT("two rebuilds preserve portal count"), Rebuilt.Num(), Portals.Num());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPortalBindingHoldingGuidanceTest,
  "CrowdDemo.SF3.Traffic.BindingHoldingAndBandGuidance",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPortalBindingHoldingGuidanceTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTrafficSettings Settings;
  TArray<FCrowdDemoTrafficPortalRuntime> Portals;
  FCrowdDemoTrafficPortalRuntime& PortalA = Portals.AddDefaulted_GetRef();
  PortalA.Portal.PortalId = 10;
  PortalA.Portal.Axis = 1;
  PortalA.Portal.Center = FVector2f::ZeroVector;
  PortalA.Portal.WidthCells = 6;
  PortalA.Portal.Capacity = 3;
  PortalA.ActiveDirectionKey = 1;
  FCrowdDemoTrafficPortalRuntime& PortalB = Portals.AddDefaulted_GetRef();
  PortalB = PortalA;
  PortalB.Portal.PortalId = 20;
  PortalB.Portal.Center = FVector2f(180.0f, 0.0f);

  FCrowdDemoTrafficAgent Locked;
  Locked.AgentId = 100;
  Locked.Position = FVector2f(170.0f, -250.0f);
  Locked.FlowDirection = FVector2f(0,1);
  Locked.PreviousPortalId = 10;
  Locked.PreviousDirectionKey = 1;
  Locked.AdmissionState = ECrowdDemoPortalAdmissionState::Waiting;
  TArray<FCrowdDemoPortalCandidate> Candidates;
  FCrowdDemoPortalCandidateBuildSummary CandidateSummary;
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(
    {Locked}, Portals, Settings, Candidates, &CandidateSummary);
  TestEqual(TEXT("locked waiting agent remains on original portal"), Candidates.Num(), 1);
  TestEqual(TEXT("nearby portal does not cause rebind"), Candidates[0].PortalId, 10);
  TestEqual(TEXT("stable portal direction uses axis and direction key"), Candidates[0].PortalDirection, FVector2f(0,1));
  TestEqual(TEXT("valid lock has no rebind"), CandidateSummary.RebindCount, 0);

  FCrowdDemoTrafficAgent Behind = Locked;
  Behind.AgentId = 101;
  Behind.PreviousPortalId = INDEX_NONE;
  Behind.AdmissionState = ECrowdDemoPortalAdmissionState::None;
  Behind.Position = FVector2f(0,100);
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(
    {Behind}, Portals, Settings, Candidates, &CandidateSummary);
  TestEqual(TEXT("agent behind portal is not entry candidate"), Candidates.Num(), 0);
  TestTrue(TEXT("invalid side is counted"), CandidateSummary.InvalidSideCandidateCount > 0);

  FCrowdDemoTrafficAgent WrongSpan = Behind;
  WrongSpan.AgentId = 102;
  WrongSpan.Position = FVector2f(1000,-100);
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(
    {WrongSpan}, Portals, Settings, Candidates, &CandidateSummary);
  TestEqual(TEXT("agent outside span is not candidate"), Candidates.Num(), 0);
  TestTrue(TEXT("wrong span is counted"), CandidateSummary.WrongSpanCandidateCount > 0);

  FCrowdDemoSharedFlowField HoldingField;
  HoldingField.Config.BoundsMin = FVector(-2000,-3000,0);
  HoldingField.Config.BoundsMax = FVector(2000,2000,0);
  HoldingField.Config.CellSizeCm = 100.0f;
  HoldingField.Width = 40;
  HoldingField.Height = 50;
  const int32 CellCount = HoldingField.Width * HoldingField.Height;
  HoldingField.Blocked.Init(false, CellCount);
  HoldingField.Unreachable.Init(false, CellCount);
  HoldingField.NextCellIndex.Init(INDEX_NONE, CellCount);
  HoldingField.IntegrationCost.Init(0, CellCount);
  HoldingField.FlowDirection.Init(FVector(0,1,0), CellCount);
  HoldingField.GoalCellIndex = CellCount - 1;
  TArray<FCrowdDemoTrafficAgent> WaitingAgents;
  TArray<FCrowdDemoPortalDecision> Decisions;
  for (int32 Index = 0; Index < 8; ++Index)
  {
    FCrowdDemoTrafficAgent& Agent = WaitingAgents.AddDefaulted_GetRef();
    Agent.AgentId = 200 + Index;
    Agent.Position = FVector2f((Index - 4) * 15.0f, -150.0f);
    Agent.FlowDirection = FVector2f(0,1);
    Agent.RadiusCm = 42.0f;
    Agent.WaitSteps = Index * 30;
    FCrowdDemoPortalDecision& Decision = Decisions.AddDefaulted_GetRef();
    Decision.AgentId = Agent.AgentId;
    Decision.PortalId = 10;
    Decision.DirectionKey = 1;
    Decision.PortalDirection = FVector2f(0,1);
    Decision.State = ECrowdDemoPortalAdmissionState::Waiting;
  }
  FCrowdDemoTrafficStepSummary HoldingSummary;
  FCrowdDemoTrafficSchedulingKernel::BuildHoldingTargets(
    WaitingAgents, Portals, HoldingField, Settings, Decisions, HoldingSummary);
  TestEqual(TEXT("all waiting agents receive holding targets"), HoldingSummary.HoldingTargetCount, WaitingAgents.Num());
  TestEqual(TEXT("holding targets do not overlap"), HoldingSummary.HoldingTargetOverlapCount, 0);
  TestEqual(TEXT("holding target allocation succeeds"), HoldingSummary.HoldingTargetAllocationFailureCount, 0);
  TMap<int32, FVector2f> TargetsById;
  for (const FCrowdDemoPortalDecision& Decision : Decisions)
  {
    TestTrue(TEXT("holding target remains on entry side"), Decision.HoldingTarget.Y < 0.0f);
    TestTrue(TEXT("holding target leaves central transit lane"), FMath::Abs(Decision.HoldingTarget.X) > 47.0f);
    TargetsById.Add(Decision.AgentId, Decision.HoldingTarget);
  }
  Algo::Reverse(WaitingAgents);
  Algo::Reverse(Decisions);
  FCrowdDemoTrafficStepSummary ReorderedHoldingSummary;
  FCrowdDemoTrafficSchedulingKernel::BuildHoldingTargets(
    WaitingAgents, Portals, HoldingField, Settings, Decisions, ReorderedHoldingSummary);
  for (const FCrowdDemoPortalDecision& Decision : Decisions)
    TestEqual(TEXT("holding target ignores input order"), Decision.HoldingTarget, TargetsById.FindChecked(Decision.AgentId));

  FCrowdDemoTrafficCell DenseCell;
  DenseCell.AgentCount = Settings.DensitySaturationCount;
  FCrowdDemoTrafficAgent GuidanceAgent = WaitingAgents[0];
  GuidanceAgent.Position = FVector2f::ZeroVector;
  FCrowdDemoPortalDecision CenterBand;
  CenterBand.State = ECrowdDemoPortalAdmissionState::Approach;
  CenterBand.BandId = 1;
  CenterBand.PortalDirection = FVector2f(0,1);
  const FVector2f CenterVelocity = FCrowdDemoTrafficSchedulingKernel::ApplyDensityAndBandGuidance(
    GuidanceAgent, &DenseCell, &Portals[0], &CenterBand, Settings, 800.0f);
  TestEqual(TEXT("center band has zero lateral velocity"), CenterVelocity.X, 0.0f);
  TestEqual(TEXT("center band lateral error is zero"), CenterBand.BandLateralErrorCm, 0.0f);

  FCrowdDemoPortalDecision Reserved = CenterBand;
  Reserved.State = ECrowdDemoPortalAdmissionState::Reserved;
  const FVector2f ReservedVelocity = FCrowdDemoTrafficSchedulingKernel::ApplyDensityAndBandGuidance(
    GuidanceAgent, &DenseCell, &Portals[0], &Reserved, Settings, 800.0f);
  TestTrue(TEXT("reserved density priority preserves clearing speed"), ReservedVelocity.Y >= 400.0f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf3StageHashReplayTest,
  "CrowdDemo.SF3.Determinism.StageHashInputOrderAndTwoRoundReplay",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf3StageHashReplayTest::RunTest(const FString& Parameters)
{
  auto BuildHash = [](TArray<FCrowdDemoRoundAgentState> States, const int32 Step)
  {
    States.Sort([](const FCrowdDemoRoundAgentState& A, const FCrowdDemoRoundAgentState& B)
    {
      return A.AgentId < B.AgentId;
    });
    FCrowdDemoSf3DeterminismHashBuilder Hash(Step, States.Num());
    for (const FCrowdDemoRoundAgentState& State : States)
    {
      Hash.AddInt(State.AgentId);
      Hash.AddPosition(FVector(State.Location));
      Hash.AddVelocity(FVector(State.Velocity));
      Hash.AddInt(FMath::RoundToInt(State.YawDegrees));
      Hash.AddInt(FMath::RoundToInt(State.RadiusCm));
    }
    return Hash.Finalize();
  };
  TArray<FCrowdDemoRoundAgentState> Initial;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    FCrowdDemoRoundAgentState& State = Initial.AddDefaulted_GetRef();
    State.AgentId = 1000 + Index;
    State.Location = FVector((Index % 5) * 90.0f, (Index / 5) * 90.0f, 60.0f);
    State.Velocity = FVector(0.0f, 300.0f, 0.0f);
  }
  TArray<FCrowdDemoRoundAgentState> Reordered = Initial;
  Algo::Reverse(Reordered);
  TestEqual(TEXT("stage hash ignores entity input order"), BuildHash(Reordered, 0), BuildHash(Initial, 0));

  TArray<uint32> RoundHashes[2];
  for (int32 Round = 0; Round < 2; ++Round)
  {
    TArray<FCrowdDemoRoundAgentState> States = Initial;
    for (int32 Step = 0; Step < 180; ++Step)
    {
      for (FCrowdDemoRoundAgentState& State : States)
      {
        State.Location = FVector(State.Location) + FVector(State.Velocity) / 30.0f;
      }
      RoundHashes[Round].Add(BuildHash(States, Step));
    }
  }
  TestEqual(TEXT("two replay rounds have same step count"), RoundHashes[1].Num(), RoundHashes[0].Num());
  for (int32 Step = 0; Step < RoundHashes[0].Num(); ++Step)
  {
    TestEqual(TEXT("two replay rounds have identical per-step hash"), RoundHashes[1][Step], RoundHashes[0][Step]);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPortalScheduleDeterminismTest,
  "CrowdDemo.SF3.Traffic.AdmissionAndBandDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPortalScheduleDeterminismTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTrafficSettings Settings;
  Settings.MaxBandCount=3;
  TArray<FCrowdDemoTrafficPortalRuntime> Portals;
  FCrowdDemoTrafficPortalRuntime& Portal=Portals.AddDefaulted_GetRef();
  Portal.Portal.PortalId=77;
  Portal.Portal.Capacity=2;
  Portal.ActiveDirectionKey=1;
  TArray<FCrowdDemoPortalCandidate> Candidates;
  Candidates.Add({30,0,77,1,0,4,0,FVector2f(0,1)});
  Candidates.Add({10,0,77,1,3,8,90,FVector2f(0,1)});
  Candidates.Add({20,0,77,1,3,8,90,FVector2f(0,1)});
  Candidates.Sort([](const FCrowdDemoPortalCandidate&A,const FCrowdDemoPortalCandidate&B)
  {
    if(A.WaitEpoch!=B.WaitEpoch)return A.WaitEpoch>B.WaitEpoch;
    if(A.DistanceBucket!=B.DistanceBucket)return A.DistanceBucket<B.DistanceBucket;
    return A.AgentId<B.AgentId;
  });
  TArray<FCrowdDemoTrafficAgent> TrafficAgents;
  for (const FCrowdDemoPortalCandidate& Candidate : Candidates)
  {
    FCrowdDemoTrafficAgent& Agent = TrafficAgents.AddDefaulted_GetRef();
    Agent.AgentId = Candidate.AgentId;
    Agent.AdmissionState = ECrowdDemoPortalAdmissionState::Waiting;
    Agent.PreviousPortalId = Candidate.PortalId;
    Agent.PreviousDirectionKey = Candidate.DirectionKey;
    Agent.PreviousDirectionEpoch = 0;
    Agent.WaitSteps = Candidate.WaitSteps;
  }
  TArray<FCrowdDemoPortalDecision> DecisionsA,DecisionsB;
  FCrowdDemoTrafficStepSummary SummaryA,SummaryB;
  TArray<FCrowdDemoTrafficPortalRuntime> PortalsCopy=Portals;
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(Portals,TrafficAgents,Candidates,Settings,1,DecisionsA,SummaryA);
  Algo::Reverse(Candidates);
  Candidates.Sort([](const FCrowdDemoPortalCandidate&A,const FCrowdDemoPortalCandidate&B)
  {
    if(A.WaitEpoch!=B.WaitEpoch)return A.WaitEpoch>B.WaitEpoch;
    if(A.DistanceBucket!=B.DistanceBucket)return A.DistanceBucket<B.DistanceBucket;
    return A.AgentId<B.AgentId;
  });
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(PortalsCopy,TrafficAgents,Candidates,Settings,1,DecisionsB,SummaryB);
  TestEqual(TEXT("quota does not exceed capacity"),SummaryA.AdmissionGrantedCount,2);
  TestEqual(TEXT("oldest candidate wins"),DecisionsA[0].AgentId,10);
  TestEqual(TEXT("AgentId breaks ties"),DecisionsA[1].AgentId,20);
  TestEqual(TEXT("decision hash stable"),SummaryB.PortalDecisionHash,SummaryA.PortalDecisionHash);
  TestEqual(TEXT("band assignment stable"),DecisionsB[0].BandId,DecisionsA[0].BandId);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPortalPersistentStateMachineTest,
  "CrowdDemo.SF3.Traffic.PersistentPortalStateMachine",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPortalPersistentStateMachineTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTrafficSettings Settings;
  TArray<FCrowdDemoTrafficPortalRuntime> Portals;
  FCrowdDemoTrafficPortalRuntime& Portal = Portals.AddDefaulted_GetRef();
  Portal.Portal.PortalId = 9;
  Portal.Portal.Capacity = 1;
  Portal.Portal.Axis = 1;
  Portal.Portal.Center = FVector2f::ZeroVector;
  Portal.ActiveDirectionKey = 1;

  FCrowdDemoTrafficAgent Waiting;
  Waiting.AgentId = 10;
  Waiting.Position = FVector2f(0.0f, -100.0f);
  Waiting.FlowDirection = FVector2f(0.0f, 1.0f);
  Waiting.AdmissionState = ECrowdDemoPortalAdmissionState::Waiting;
  Waiting.PreviousPortalId = 9;
  Waiting.PreviousDirectionKey = 1;
  Waiting.PreviousDirectionEpoch = 0;
  Waiting.PreviousBandId = 1;
  TArray<FCrowdDemoTrafficAgent> Agents = { Waiting };
  TArray<FCrowdDemoPortalCandidate> Candidates;
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(Agents, Portals, Settings, Candidates);
  TArray<FCrowdDemoPortalDecision> Decisions;
  FCrowdDemoTrafficStepSummary Summary;
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
    Portals, Agents, Candidates, Settings, 10, Decisions, Summary);
  TestEqual(TEXT("first waiting agent receives token"), Summary.AdmissionGrantedCount, 1);
  TestEqual(TEXT("token state is reserved"), Decisions[0].State, ECrowdDemoPortalAdmissionState::Reserved);
  TestEqual(TEXT("existing band remains stable"), Decisions[0].BandId, static_cast<int16>(1));

  Agents[0].AdmissionState = ECrowdDemoPortalAdmissionState::Reserved;
  Agents[0].TokenGrantedStep = Decisions[0].TokenGrantedStep;
  Agents[0].PreviousDirectionEpoch = Decisions[0].DirectionEpoch;
  Candidates.Reset();
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(Agents, Portals, Settings, Candidates);
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
    Portals, Agents, Candidates, Settings, 11, Decisions, Summary);
  TestEqual(TEXT("reserved token is not issued twice"), Summary.AdmissionGrantedCount, 0);
  TestEqual(TEXT("reserved count remains one"), Portals[0].ReservedCount, 1);
  TestEqual(TEXT("capacity remains valid"), Summary.CapacityViolationCount, 0);

  Agents[0].Position = FVector2f(0.0f, -100.0f);
  Agents[0].TokenGrantedStep = 0;
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
    Portals, Agents, {}, Settings, 60, Decisions, Summary);
  TestEqual(TEXT("reservation timeout releases token"), Summary.ReservationTimeoutCount, 1);
  TestEqual(TEXT("reservation timeout requeues"), Decisions[0].State, ECrowdDemoPortalAdmissionState::Waiting);

  Agents[0].AdmissionState = ECrowdDemoPortalAdmissionState::Inside;
  Agents[0].EnteredPortalStep = 0;
  Agents[0].TokenGrantedStep = 0;
  Agents[0].Position = FVector2f::ZeroVector;
  Portals[0].GreenSteps = 100;
  TArray<FCrowdDemoTrafficAgent> OppositeAgents = Agents;
  FCrowdDemoTrafficAgent Opposite;
  Opposite.AgentId = 20;
  Opposite.Position = FVector2f(0.0f, 100.0f);
  Opposite.FlowDirection = FVector2f(0.0f, -1.0f);
  Opposite.AdmissionState = ECrowdDemoPortalAdmissionState::Waiting;
  Opposite.PreviousPortalId = 9;
  Opposite.PreviousDirectionKey = -1;
  OppositeAgents.Add(Opposite);
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(OppositeAgents, Portals, Settings, Candidates);
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
    Portals, OppositeAgents, Candidates, Settings, 100, Decisions, Summary);
  TestEqual(TEXT("occupied portal blocks direction switch"), Portals[0].ActiveDirectionKey, 1);
  TestEqual(TEXT("epoch unchanged while occupied"), Portals[0].DirectionEpoch, 0);

  Agents[0].EnteredPortalStep = 0;
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
    Portals, Agents, {}, Settings, 120, Decisions, Summary);
  TestEqual(TEXT("transit timeout releases inside agent"), Summary.TransitTimeoutCount, 1);
  TestEqual(TEXT("transit timeout exits"), Decisions[0].State, ECrowdDemoPortalAdmissionState::Exited);

  TArray<FCrowdDemoTrafficPortalRuntime> SwitchPortals;
  FCrowdDemoTrafficPortalRuntime& SwitchPortal = SwitchPortals.AddDefaulted_GetRef();
  SwitchPortal.Portal = Portal.Portal;
  SwitchPortal.ActiveDirectionKey = 1;
  SwitchPortal.GreenSteps = Settings.MaxGreenSteps;
  TArray<FCrowdDemoTrafficAgent> SwitchAgents = { Opposite };
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(SwitchAgents, SwitchPortals, Settings, Candidates);
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
    SwitchPortals, SwitchAgents, Candidates, Settings, 200, Decisions, Summary);
  TestEqual(TEXT("clear portal changes to waiting direction"), SwitchPortals[0].ActiveDirectionKey, -1);
  TestEqual(TEXT("direction epoch increments only on switch"), SwitchPortals[0].DirectionEpoch, 1);
  TestEqual(TEXT("clearance blocks token on switch step"), Summary.AdmissionGrantedCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaDeterminismTest,
  "CrowdDemo.SF3.ORCA.GridResultAndFallbackDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoOrcaAgent> Agents;
  for(int32 Index=0;Index<48;++Index)
  {
    FCrowdDemoOrcaAgent& Agent=Agents.AddDefaulted_GetRef();
    Agent.AgentId=Index+1;
    Agent.Position=FVector2f((Index%8)*70.0f,(Index/8)*70.0f);
    Agent.Velocity=FVector2f::ZeroVector;
    Agent.PreferredVelocity=FVector2f(Index%2?300.0f:-300.0f,200.0f);
    Agent.FlowDirection=FVector2f(0,1);
    Agent.PortalDirection=FVector2f(0,1);
    Agent.AdmissionState=Index%3==0?ECrowdDemoPortalAdmissionState::Reserved:ECrowdDemoPortalAdmissionState::Approach;
  }
  FCrowdDemoOrcaSettings Settings;
  TArray<TArray<FCrowdDemoOrcaNeighbor>> GridNeighbors;
  FCrowdDemoDeterministicOrcaKernel::BuildNeighbors(Agents, Settings, GridNeighbors);
  TArray<FCrowdDemoOrcaAgent> SortedForBruteForce = Agents;
  SortedForBruteForce.Sort([](const FCrowdDemoOrcaAgent& A, const FCrowdDemoOrcaAgent& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 A = 0; A < SortedForBruteForce.Num(); ++A)
  {
    TArray<FCrowdDemoOrcaNeighbor> Expected;
    for (int32 B = 0; B < SortedForBruteForce.Num(); ++B)
    {
      if (A == B) continue;
      const float DistanceSq = (SortedForBruteForce[B].Position - SortedForBruteForce[A].Position).SizeSquared();
      if (DistanceSq <= FMath::Square(Settings.NeighborDistanceCm))
      {
        Expected.Add({SortedForBruteForce[B].AgentId,
          FMath::FloorToInt(FMath::Sqrt(DistanceSq) / Settings.DistanceBucketCm), DistanceSq});
      }
    }
    Expected.Sort([](const FCrowdDemoOrcaNeighbor& Left, const FCrowdDemoOrcaNeighbor& Right)
    {
      if (Left.DistanceBucket != Right.DistanceBucket) return Left.DistanceBucket < Right.DistanceBucket;
      return Left.AgentId < Right.AgentId;
    });
    if (Expected.Num() > Settings.MaxNeighbors) Expected.SetNum(Settings.MaxNeighbors);
    TestEqual(TEXT("ORCA grid neighbor count equals brute force"), GridNeighbors[A].Num(), Expected.Num());
    for (int32 Index = 0; Index < Expected.Num(); ++Index)
    {
      TestEqual(TEXT("ORCA grid neighbor order equals brute force"), GridNeighbors[A][Index].AgentId, Expected[Index].AgentId);
    }
  }
  TArray<FCrowdDemoOrcaResult> ResultsA,ResultsB;
  FCrowdDemoOrcaSummary SummaryA,SummaryB;
  FCrowdDemoDeterministicOrcaKernel::Solve(Agents,Settings,1.0f/30.0f,ResultsA,SummaryA);
  Algo::Reverse(Agents);
  FCrowdDemoDeterministicOrcaKernel::Solve(Agents,Settings,1.0f/30.0f,ResultsB,SummaryB);
  TestEqual(TEXT("formal ORCA path never invokes diagnostic oracle"), SummaryA.OracleInvocationCount, 0);
  TestEqual(TEXT("formal ORCA path never consumes oracle witness"), SummaryA.OracleQuantizedWitnessUsedCount, 0);
  TestEqual(TEXT("ORCA velocity hash ignores input order"),SummaryB.VelocityHash,SummaryA.VelocityHash);
  TestEqual(TEXT("ORCA result count"),ResultsB.Num(),ResultsA.Num());
  for(int32 Index=0;Index<ResultsA.Num();++Index)
  {
    TestEqual(TEXT("ORCA result id stable"),ResultsB[Index].AgentId,ResultsA[Index].AgentId);
    TestTrue(TEXT("ORCA result velocity stable"),ResultsB[Index].Velocity.Equals(ResultsA[Index].Velocity,0.01f));
    TestTrue(TEXT("ORCA fallback stage is unused or in declared order"),
      ResultsA[Index].FallbackStage <= 4);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaParallelNumericalRegressionTest,
  "CrowdDemo.SF3.ORCA.ParallelNumericalRegression",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaParallelNumericalRegressionTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  const FVector2f FixtureNormal(0.989176510f,-0.146730474f);
  TArray<FCrowdDemoOrcaConstraint> ProductionFixture;
  ProductionFixture.Add({1,FVector2f::ZeroVector,FixtureNormal,0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  ProductionFixture.Add({2,FVector2f(-58.692189782f,-395.670603986f),FixtureNormal,0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,1});
  const auto Oracle=FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
    FVector2f(-400,400),800.0f,ProductionFixture,Settings);
  TestTrue(TEXT("production parallel fixture keeps zero feasible"),Oracle.bZeroVelocityFeasible);
  FVector2f ForwardVelocity,ReverseVelocity;
  TestTrue(TEXT("production parallel fixture formal solve"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f(-400,400),800.0f,ProductionFixture,Settings,ForwardVelocity));
  Algo::Reverse(ProductionFixture);
  TestTrue(TEXT("production fixture reverse input remains feasible"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f(-400,400),800.0f,ProductionFixture,Settings,ReverseVelocity));
  TestTrue(TEXT("production fixture reverse input velocity stable"),
    ForwardVelocity.Equals(ReverseVelocity,0.01f));

  // The production fixture produced a +1.82e-7 cm/s scalar residual after two
  // analytically coincident boundaries were evaluated through different float
  // operation paths.  Express the residual directly here so FVector2f literal
  // rounding cannot accidentally erase the failure before the clipper sees it.
  FCrowdDemoOrcaConstraint WithinFloatResidual;
  WithinFloatResidual.Point=FVector2f(0,0.10000018f);
  WithinFloatResidual.Normal=FVector2f(0,1);
  float Minimum=-800.0f,Maximum=800.0f;
  TestTrue(TEXT("parallel floating residual remains numerically feasible"),
    FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
      FVector2f::ZeroVector,FVector2f(1,0),WithinFloatResidual,0.1f,Minimum,Maximum));

  const float Residuals[] = {-1.0e-6f,-1.0e-7f,-1.0e-8f,1.0e-8f,1.0e-7f,1.0e-6f};
  for (const float Residual : Residuals)
  {
    FCrowdDemoOrcaConstraint Constraint;
    Constraint.Point=FVector2f(0,0.1f+Residual);
    Constraint.Normal=FVector2f(0,1);
    Minimum=-800.0f; Maximum=800.0f;
    TestTrue(FString::Printf(TEXT("parallel residual %.9g accepted"),Residual),
      FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
        FVector2f::ZeroVector,FVector2f(1,0),Constraint,0.1f,Minimum,Maximum));
  }
  const auto Tolerances=FCrowdDemoDeterministicOrcaKernel::ComputeNumericalTolerances(800.0);
  for (const float Residual : {
    static_cast<float>(2.0*Tolerances.ResidualToleranceCmps),
    static_cast<float>(10.0*Tolerances.ResidualToleranceCmps),0.01f,0.1f})
  {
    FCrowdDemoOrcaConstraint Constraint;
    Constraint.Point=FVector2f(0,0.1f+Residual);
    Constraint.Normal=FVector2f(0,1);
    Minimum=-800.0f; Maximum=800.0f;
    TestFalse(FString::Printf(TEXT("true contradiction %.3f rejected"),Residual),
      FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
        FVector2f::ZeroVector,FVector2f(1,0),Constraint,0.1f,Minimum,Maximum));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaNearParallelStabilityTest,
  "CrowdDemo.SF3.ORCA.NearParallelStability",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaNearParallelStabilityTest::RunTest(const FString& Parameters)
{
  const double Angles[] = {0.0,1.0e-8,-1.0e-8,1.0e-7,-1.0e-7,1.0e-6,-1.0e-6,1.0e-5,-1.0e-5};
  for (const double Angle : Angles)
  {
    FCrowdDemoOrcaConstraint Constraint;
    Constraint.Point=FVector2f(0,0.1f);
    Constraint.Normal=FVector2f(static_cast<float>(FMath::Sin(Angle)),static_cast<float>(FMath::Cos(Angle)));
    float Minimum=-800.0f,Maximum=800.0f;
    FCrowdDemoOrcaNumericalSummary Summary;
    TestTrue(FString::Printf(TEXT("near parallel angle %.9g remains feasible"),Angle),
      FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
        FVector2f::ZeroVector,FVector2f(1,0),Constraint,0.1f,Minimum,Maximum,&Summary));
    TestTrue(TEXT("near parallel interval finite"),FMath::IsFinite(Minimum)&&FMath::IsFinite(Maximum));
    if (FMath::Abs(Angle)<=1.0e-6)
      TestEqual(TEXT("small angle selects parallel branch"),Summary.ParallelBranchCount,1);
    else
      TestEqual(TEXT("larger angle selects non-parallel branch"),Summary.ParallelBranchCount,0);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaFormalOracleMatrixTest,
  "CrowdDemo.SF3.ORCA.FormalSolverMatchesOracleMatrix",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaFormalOracleMatrixTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  for (int32 Count=1;Count<=24;++Count)
  {
    TArray<FCrowdDemoOrcaConstraint> Constraints;
    for (int32 Index=0;Index<Count;++Index)
    {
      const double Angle=2.0*PI*static_cast<double>(Index)/static_cast<double>(Count);
      FCrowdDemoOrcaConstraint Constraint;
      Constraint.OtherAgentId=Index+1;
      Constraint.Normal=FVector2f(static_cast<float>(FMath::Cos(Angle)),static_cast<float>(FMath::Sin(Angle)));
      Constraint.Point=-Constraint.Normal*100.0f;
      Constraint.StableConstraintOrder=Index;
      Constraints.Add(Constraint);
    }
    FVector2f Continuous;
    const bool bFormal=FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f(700,-300),800.0f,Constraints,Settings,Continuous);
    const auto Oracle=FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
      FVector2f(700,-300),800.0f,Constraints,Settings);
    TestEqual(FString::Printf(TEXT("formal/oracle feasible match for %d constraints"),Count),
      bFormal,Oracle.bFoundFeasibleWitness);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaReferenceDifferentialFixturesTest,
  "CrowdDemo.SF3.ORCA.ReferenceDifferentialFixtures",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaReferenceDifferentialFixturesTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  auto Compare = [&](const TCHAR* Label, const FVector2f Preferred,
    const TArray<FCrowdDemoOrcaConstraint>& Constraints, const bool bExpectedFeasible)
  {
    const FCrowdDemoOrcaContinuousSolveInput Input =
      FCrowdDemoDeterministicOrcaKernel::MakeContinuousSolveInput(
        Preferred,800.0f,Constraints,Settings.ConstraintEpsilonCmps);
    const auto Current=FCrowdDemoDeterministicOrcaKernel::SolveContinuousExact(Input);
    const auto Reference=FCrowdDemoRvo2ReferenceSolver::Solve(Input);
    const auto Oracle=FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
      Preferred,800.0f,Constraints,Settings);
    const bool bCurrent=Current.Status==ECrowdDemoOrcaSolveStatus::PreferredFeasible
      || Current.Status==ECrowdDemoOrcaSolveStatus::ExactFeasible;
    const bool bReference=Reference.Status==ECrowdDemoOrcaSolveStatus::PreferredFeasible
      || Reference.Status==ECrowdDemoOrcaSolveStatus::ExactFeasible;
    TestEqual(FString::Printf(TEXT("%s current feasibility"),Label),bCurrent,bExpectedFeasible);
    TestEqual(FString::Printf(TEXT("%s reference feasibility"),Label),bReference,bExpectedFeasible);
    TestEqual(FString::Printf(TEXT("%s oracle feasibility"),Label),
      Oracle.bFoundFeasibleWitness,bExpectedFeasible);
    if (bCurrent) TestTrue(FString::Printf(TEXT("%s current validates"),Label),
      FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(Input,Current.Velocity));
    if (bReference) TestTrue(FString::Printf(TEXT("%s reference validates"),Label),
      FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(Input,Reference.Velocity));
    return TPair<bool,bool>(bCurrent,bReference);
  };

  TArray<FCrowdDemoOrcaConstraint> Direction;
  Direction.Add({1,FVector2f(100,0),FVector2f(1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  Compare(TEXT("single direction"),FVector2f::ZeroVector,Direction,true);
  const auto DirectionInput=FCrowdDemoDeterministicOrcaKernel::MakeContinuousSolveInput(
    FVector2f::ZeroVector,800.0f,Direction,0.1f);
  const auto DirectionReference=FCrowdDemoRvo2ReferenceSolver::Solve(DirectionInput);
  TestTrue(TEXT("reference line direction maps positive normal side"),DirectionReference.Velocity.X>99.0f);

  TArray<FCrowdDemoOrcaConstraint> Parallel;
  Parallel.Add({1,FVector2f::ZeroVector,FVector2f(0.989176510f,-0.146730474f),
    0.5f,84,200,1.25f,ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  Parallel.Add({2,FVector2f(-58.692189782f,-395.670603986f),
    FVector2f(0.989176510f,-0.146730474f),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,1});
  Compare(TEXT("equivalent parallel"),FVector2f(-400,400),Parallel,true);
  Algo::Reverse(Parallel);
  Compare(TEXT("equivalent parallel reversed"),FVector2f(-400,400),Parallel,true);

  for (const float Residual : {-1.0e-6f,-1.0e-7f,1.0e-7f,1.0e-6f})
  {
    TArray<FCrowdDemoOrcaConstraint> ResidualFixture;
    ResidualFixture.Add({1,FVector2f(0,0.1f),FVector2f(0,1)});
    ResidualFixture.Add({2,FVector2f(0,0.1f+Residual),FVector2f(0,1)});
    Compare(TEXT("parallel residual"),FVector2f(10,-10),ResidualFixture,true);
  }

  TArray<FCrowdDemoOrcaConstraint> Contradiction;
  Contradiction.Add({1,FVector2f(1.1f,0),FVector2f(1,0)});
  Contradiction.Add({2,FVector2f(-1.1f,0),FVector2f(-1,0)});
  Compare(TEXT("true opposite contradiction"),FVector2f::ZeroVector,Contradiction,false);

  TArray<FCrowdDemoOrcaConstraint> Tangent;
  Tangent.Add({1,FVector2f(800,0),FVector2f(1,0)});
  Compare(TEXT("speed circle tangent"),FVector2f::ZeroVector,Tangent,true);

  TArray<FCrowdDemoOrcaConstraint> QuantizedStrip;
  QuantizedStrip.Add({1,FVector2f(0.45f,0),FVector2f(1,0)});
  QuantizedStrip.Add({2,FVector2f(0.55f,0),FVector2f(-1,0)});
  const auto StripPair=Compare(TEXT("continuous-only quantized strip"),
    FVector2f(-10,0),QuantizedStrip,true);
  FVector2f Continuous,Quantized;
  TestTrue(TEXT("strip formal continuous exists"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f(-10,0),800.0f,QuantizedStrip,Settings,Continuous));
  TestEqual(TEXT("strip has no project 1cm quantized solution"),
    FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
      Continuous,FVector2f(-10,0),800.0f,QuantizedStrip,Settings,Quantized),
    ECrowdDemoOrcaQuantizationResult::NoSolution);
  TestTrue(TEXT("both solvers agree strip continuous"),StripPair.Key&&StripPair.Value);

  for (int32 Count=1;Count<=24;++Count)
  {
    TArray<FCrowdDemoOrcaConstraint> Matrix;
    for (int32 Index=0;Index<Count;++Index)
    {
      const float Angle=2.0f*PI*static_cast<float>(Index)/static_cast<float>(Count);
      const FVector2f Normal(FMath::Cos(Angle),FMath::Sin(Angle));
      FCrowdDemoOrcaConstraint Constraint;
      Constraint.OtherAgentId=Index+1;
      Constraint.Point=-Normal*100.0f;
      Constraint.Normal=Normal;
      Constraint.StableConstraintOrder=Index;
      Matrix.Add(Constraint);
    }
    const FString Label=FString::Printf(TEXT("fixed matrix count %d"),Count);
    const auto Pair=Compare(*Label,FVector2f(700,-300),Matrix,true);
    TestEqual(FString::Printf(TEXT("matrix current/reference agree count %d"),Count),Pair.Key,Pair.Value);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaHalfPlaneIntervalClippingTest,
  "CrowdDemo.SF3.ORCA.HalfPlaneIntervalClipping",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaHalfPlaneIntervalClippingTest::RunTest(const FString& Parameters)
{
  const auto Circle = FCrowdDemoDeterministicOrcaKernel::ComputeLineCircleInterval(
    FVector2f(0,100),FVector2f(1,0),800.0f,0.1f);
  TestTrue(TEXT("line intersects speed circle"),Circle.bFeasible);
  TestTrue(TEXT("circle interval symmetric"),FMath::IsNearlyEqual(Circle.MinimumT,-Circle.MaximumT,0.001f));
  const auto Outside = FCrowdDemoDeterministicOrcaKernel::ComputeLineCircleInterval(
    FVector2f(0,900),FVector2f(1,0),800.0f,0.1f);
  TestFalse(TEXT("outside line excludes speed circle"),Outside.bFeasible);

  FCrowdDemoOrcaConstraint Lower;
  Lower.Point=FVector2f(0,10); Lower.Normal=FVector2f(0,1);
  float Minimum=-100.0f,Maximum=100.0f;
  TestTrue(TEXT("line clips against half-plane"),
    FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
      FVector2f::ZeroVector,FVector2f(0,1),Lower,0.1f,Minimum,Maximum));
  TestTrue(TEXT("lower interval bound"),FMath::IsNearlyEqual(Minimum,9.9f,0.001f));

  FCrowdDemoOrcaConstraint ParallelSatisfied;
  ParallelSatisfied.Point=FVector2f(0,-1); ParallelSatisfied.Normal=FVector2f(0,1);
  Minimum=-100;Maximum=100;
  TestTrue(TEXT("parallel satisfied constraint is redundant"),
    FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
      FVector2f::ZeroVector,FVector2f(1,0),ParallelSatisfied,0.1f,Minimum,Maximum));
  FCrowdDemoOrcaConstraint ParallelContradictory;
  ParallelContradictory.Point=FVector2f(0,1); ParallelContradictory.Normal=FVector2f(0,1);
  TestFalse(TEXT("parallel contradictory constraint rejects interval"),
    FCrowdDemoDeterministicOrcaKernel::ClipLineIntervalAgainstHalfPlane(
      FVector2f::ZeroVector,FVector2f(1,0),ParallelContradictory,0.1f,Minimum,Maximum));

  FCrowdDemoOrcaSettings Settings;
  TArray<FCrowdDemoOrcaConstraint> QuantizedRecovery;
  QuantizedRecovery.Add({1,FVector2f(0.55f,0),FVector2f(1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  FVector2f Continuous,Quantized;
  TestTrue(TEXT("continuous narrow boundary solves"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f(-10,0),800.0f,QuantizedRecovery,Settings,Continuous));
  TestEqual(TEXT("3x3 recovers quantized point"),
    FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
      Continuous,FVector2f(-10,0),800.0f,QuantizedRecovery,Settings,Quantized),
    ECrowdDemoOrcaQuantizationResult::NeighborhoodRecovered);

  TArray<FCrowdDemoOrcaConstraint> QuantizedEmpty;
  QuantizedEmpty.Add({1,FVector2f(0.55f,0),FVector2f(1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  QuantizedEmpty.Add({2,FVector2f(0.45f,0),FVector2f(-1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::RightLeg,1});
  TestTrue(TEXT("sub-centimeter strip has continuous solution"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f(-10,0),800.0f,QuantizedEmpty,Settings,Continuous));
  TestEqual(TEXT("sub-centimeter strip has no 1cm grid solution"),
    FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
      Continuous,FVector2f(-10,0),800.0f,QuantizedEmpty,Settings,Quantized),
    ECrowdDemoOrcaQuantizationResult::NoSolution);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoElasticStep29OrcaQuantizationReplayTest,
  "CrowdDemo.SF4.Elastic.Step29OrcaQuantizationReplay",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoElasticStep29OrcaQuantizationReplayTest::RunTest(const FString& Parameters)
{
  struct FConstraintFact
  {
    int32 OtherId;
    int32 PointX;
    int32 PointY;
    int32 NormalXQ15;
    int32 NormalYQ15;
    ECrowdDemoOrcaConstraintKind Kind;
  };
  const FConstraintFact Facts[] = {
    {2,616,445,17555,-27667,ECrowdDemoOrcaConstraintKind::RightLeg},
    {3,610,508,-19912,-26023,ECrowdDemoOrcaConstraintKind::CutoffCircle},
    {0,253,346,30330,12399,ECrowdDemoOrcaConstraintKind::RightLeg},
    {11,294,483,32767,156,ECrowdDemoOrcaConstraintKind::RightLeg},
    {4,605,555,-6569,-32102,ECrowdDemoOrcaConstraintKind::CutoffCircle},
    {12,413,552,30630,-11639,ECrowdDemoOrcaConstraintKind::RightLeg},
    {13,643,575,-16290,-28431,ECrowdDemoOrcaConstraintKind::CutoffCircle},
    {10,484,444,30635,11626,ECrowdDemoOrcaConstraintKind::RightLeg},
    {5,690,530,-29671,-13904,ECrowdDemoOrcaConstraintKind::CutoffCircle},
    {14,700,409,-26977,18598,ECrowdDemoOrcaConstraintKind::LeftLeg},
    {15,714,367,-23728,22598,ECrowdDemoOrcaConstraintKind::CutoffCircle},
    {16,616,445,-17542,27676,ECrowdDemoOrcaConstraintKind::LeftLeg},
    {6,737,540,-30555,-11835,ECrowdDemoOrcaConstraintKind::CutoffCircle},
    {7,784,549,-31031,-10523,ECrowdDemoOrcaConstraintKind::CutoffCircle}};
  TArray<FCrowdDemoOrcaConstraint> Constraints;
  for (int32 Index = 0; Index < UE_ARRAY_COUNT(Facts); ++Index)
  {
    const auto& Fact = Facts[Index];
    auto& Constraint = Constraints.AddDefaulted_GetRef();
    Constraint.OtherAgentId = Fact.OtherId;
    Constraint.Point = FVector2f(Fact.PointX, Fact.PointY);
    Constraint.Normal = FVector2f(
      static_cast<float>(Fact.NormalXQ15) / 32767.0f,
      static_cast<float>(Fact.NormalYQ15) / 32767.0f).GetSafeNormal();
    Constraint.Kind = Fact.Kind;
    Constraint.StableConstraintOrder = Index;
  }
  FCrowdDemoOrcaSettings Settings;
  Settings.ConstraintEpsilonCmps = 0.1f;
  Settings.VelocityQuantumCmps = 1.0f;
  const FVector2f BaselineVelocity(643,462);
  const auto Input = FCrowdDemoDeterministicOrcaKernel::MakeContinuousSolveInput(
    FVector2f(593,517),800.0f,Constraints,Settings.ConstraintEpsilonCmps);
  TestTrue(TEXT("4215325188 baseline velocity is an elastic witness"),
    FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(Input,BaselineVelocity));
  const auto Continuous = FCrowdDemoDeterministicOrcaKernel::SolveContinuousExact(Input);
  TestEqual(TEXT("4215325188 continuous LP is feasible"),Continuous.Status,
    ECrowdDemoOrcaSolveStatus::ExactFeasible);
  FVector2f Quantized;
  TestEqual(TEXT("4215325188 global geometry recovers 1cm/s witness"),
    FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
      Continuous.Velocity,Input.PreferredVelocity,Input.MaxSpeedCmps,
      Constraints,Settings,Quantized),
    ECrowdDemoOrcaQuantizationResult::GeometryRecovered);
  TestTrue(TEXT("4215325188 recovered witness is nonzero"),Quantized.SizeSquared()>1.0f);
  TestTrue(TEXT("4215325188 recovered witness satisfies all constraints"),
    FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(Input,Quantized));

  TArray<FCrowdDemoOrcaConstraint> Reversed = Constraints;
  Algo::Reverse(Reversed);
  FVector2f ReversedVelocity;
  TestTrue(TEXT("4215325188 reversed constraint input solves"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityForConstraints(
      Input.PreferredVelocity,Input.MaxSpeedCmps,Reversed,Settings,ReversedVelocity));
  TestTrue(TEXT("4215325188 reversed input keeps stable witness"),
    ReversedVelocity.Equals(Quantized,0.0f));
  for (const FVector2f Preferred : {FVector2f(592,516),FVector2f(593,517),FVector2f(594,518)})
  {
    FVector2f Velocity;
    TestTrue(TEXT("small preferred perturbation remains feasible"),
      FCrowdDemoDeterministicOrcaKernel::SolveVelocityForConstraints(
        Preferred,800.0f,Constraints,Settings,Velocity));
    TestTrue(TEXT("small preferred perturbation must not collapse to stop"),
      Velocity.SizeSquared()>1.0f);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaZeroFeasibleRegressionTest,
  "CrowdDemo.SF3.ORCA.ZeroFeasibleRegression",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaZeroFeasibleRegressionTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  TArray<FCrowdDemoOrcaConstraint> Constraints;
  Constraints.Add({1,FVector2f(-92.426221594f,-38.175955283f),
    FVector2f(-0.382683432f,0.923879533f),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  Constraints.Add({2,FVector2f(-369.532678833f,-153.119566923f),
    FVector2f(0.382683432f,-0.923879533f),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::RightLeg,1});
  const auto Oracle = FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
    FVector2f(-800,0),800.0f,Constraints,Settings);
  TestTrue(TEXT("fixture zero is feasible"), Oracle.bZeroVelocityFeasible);
  FVector2f Continuous;
  TestTrue(TEXT("formal interval solver must not miss epsilon-feasible strip"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f(-800,0),800.0f,Constraints,Settings,Continuous));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf3GoalDiagnosticOracleTest,
  "CrowdDemo.SF3.Diagnostic.GoalBucketsAndFeasibilityOracle",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf3GoalDiagnosticOracleTest::RunTest(const FString& Parameters)
{
  TestEqual(TEXT("distance bucket lower"),
    UCrowdDemoRoundSimPipelineSubsystem::Sf3GoalDistanceBucket(0.0f), 0);
  TestEqual(TEXT("distance bucket 100 boundary"),
    UCrowdDemoRoundSimPipelineSubsystem::Sf3GoalDistanceBucket(100.0f), 1);
  TestEqual(TEXT("distance bucket 1200 boundary"),
    UCrowdDemoRoundSimPipelineSubsystem::Sf3GoalDistanceBucket(1200.0f), 5);
  TestEqual(TEXT("goal cell region"),
    UCrowdDemoRoundSimPipelineSubsystem::Sf3FlowRegionBucket(FVector::ZeroVector, 0, 10.0f), 0);
  TestEqual(TEXT("goal near region"),
    UCrowdDemoRoundSimPipelineSubsystem::Sf3FlowRegionBucket(FVector::ZeroVector, 10, 400.0f), 1);
  TestEqual(TEXT("corridor region"),
    UCrowdDemoRoundSimPipelineSubsystem::Sf3FlowRegionBucket(FVector(0,-1000,0), 100, 1000.0f), 3);

  FCrowdDemoOrcaSettings Settings;
  TArray<FCrowdDemoOrcaConstraint> ZeroFeasible;
  ZeroFeasible.Add({1,FVector2f(-10,0),FVector2f(1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::CutoffCircle,0});
  const auto ZeroResult = FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
    FVector2f(100,0),800.0f,ZeroFeasible,Settings);
  TestTrue(TEXT("zero feasible is explicit"), ZeroResult.bZeroVelocityFeasible);
  TestTrue(TEXT("zero feasible is a witness"), ZeroResult.bFoundFeasibleWitness);

  TArray<FCrowdDemoOrcaConstraint> IntersectionOnly;
  IntersectionOnly.Add({1,FVector2f(100,0),FVector2f(1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  IntersectionOnly.Add({2,FVector2f(0,100),FVector2f(0,1),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::RightLeg,1});
  const auto IntersectionResult = FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
    FVector2f::ZeroVector,800.0f,IntersectionOnly,Settings);
  TestTrue(TEXT("oracle finds half-plane intersection witness"),
    IntersectionResult.bFoundFeasibleWitness);
  TestTrue(TEXT("intersection witness satisfies x"), IntersectionResult.WitnessVelocity.X >= 99.9f);
  TestTrue(TEXT("intersection witness satisfies y"), IntersectionResult.WitnessVelocity.Y >= 99.9f);

  TArray<FCrowdDemoOrcaConstraint> Contradictory;
  Contradictory.Add({1,FVector2f(100,0),FVector2f(1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::LeftLeg,0});
  Contradictory.Add({2,FVector2f(-100,0),FVector2f(-1,0),0.5f,84,200,1.25f,
    ECrowdDemoOrcaConstraintKind::RightLeg,1});
  const auto EmptyResult = FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
    FVector2f::ZeroVector,800.0f,Contradictory,Settings);
  TestFalse(TEXT("oracle rejects contradictory half-planes"), EmptyResult.bFoundFeasibleWitness);

  Algo::Reverse(IntersectionOnly);
  const auto Reordered = FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
    FVector2f::ZeroVector,800.0f,IntersectionOnly,Settings);
  TestEqual(TEXT("oracle feasibility ignores input order"),
    Reordered.bFoundFeasibleWitness, IntersectionResult.bFoundFeasibleWitness);
  TestTrue(TEXT("oracle witness ignores input order"),
    Reordered.WitnessVelocity.Equals(IntersectionResult.WitnessVelocity, 0.001f));

  FCrowdDemoOrcaAgent Reached;
  Reached.AgentId = 1;
  Reached.Position = FVector2f::ZeroVector;
  Reached.PreferredVelocity = FVector2f::ZeroVector;
  FCrowdDemoOrcaAgent Approaching = Reached;
  Approaching.AgentId = 2;
  Approaching.Position = FVector2f(100,0);
  TArray<FCrowdDemoOrcaAgent> Agents = {Reached, Approaching};
  TArray<TArray<FCrowdDemoOrcaNeighbor>> Neighbors;
  FCrowdDemoDeterministicOrcaKernel::BuildNeighbors(Agents, Settings, Neighbors);
  TestEqual(TEXT("zero-preferred reached entity remains in ORCA grid"), Neighbors[0].Num(), 1);
  TestEqual(TEXT("other entity sees reached entity as neighbor"), Neighbors[1].Num(), 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaPairConstraintGeometryTest,
  "CrowdDemo.SF3.ORCA.PairConstraintGeometry",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaPairConstraintGeometryTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  auto MakeAgent = [](const int32 Id, const FVector2f Position, const FVector2f Velocity)
  {
    FCrowdDemoOrcaAgent Agent;
    Agent.AgentId = Id;
    Agent.Position = Position;
    Agent.Velocity = Velocity;
    Agent.PreferredVelocity = Velocity;
    return Agent;
  };

  const FCrowdDemoOrcaAgent Agent = MakeAgent(1, FVector2f(0, 0), FVector2f(300, 0));
  const FCrowdDemoOrcaAgent Other = MakeAgent(2, FVector2f(300, 100), FVector2f::ZeroVector);
  FCrowdDemoOrcaConstraint Constraint;
  TestTrue(TEXT("approaching pair creates a constraint"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      Agent, Other, Settings, 1.0f / 30.0f, 0, Constraint));
  TestTrue(TEXT("non-overlap constraint selects a standard VO feature"),
    Constraint.Kind == ECrowdDemoOrcaConstraintKind::CutoffCircle
      || Constraint.Kind == ECrowdDemoOrcaConstraintKind::LeftLeg
      || Constraint.Kind == ECrowdDemoOrcaConstraintKind::RightLeg);
  TestTrue(TEXT("pair metadata records combined radius"),
    FMath::IsNearlyEqual(Constraint.CombinedRadiusCm, Agent.RadiusCm + Other.RadiusCm));
  TestEqual(TEXT("stable constraint order is retained"), Constraint.StableConstraintOrder, 0);

  FCrowdDemoOrcaConstraint Reverse;
  TestTrue(TEXT("reverse pair creates reciprocal constraint"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      Other, Agent, Settings, 1.0f / 30.0f, 0, Reverse));
  TestTrue(TEXT("equal priority responsibility sums to one"),
    FMath::IsNearlyEqual(Constraint.Responsibility + Reverse.Responsibility, 1.0f));

  FCrowdDemoOrcaAgent Overlap = Other;
  Overlap.Position = FVector2f(40, 0);
  TestTrue(TEXT("overlap creates a constraint"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      Agent, Overlap, Settings, 1.0f / 30.0f, 0, Constraint));
  TestEqual(TEXT("overlap uses fixed-step penetration feature"),
    Constraint.Kind, ECrowdDemoOrcaConstraintKind::Penetration);
  TestTrue(TEXT("penetration relative correction is capped"),
    (Constraint.Point - Agent.Velocity).Size()
      <= Agent.MaxSpeedCmps + Overlap.MaxSpeedCmps + Settings.VelocityQuantumCmps);

  const FCrowdDemoOrcaAgent ParallelA = MakeAgent(10, FVector2f(0, 0), FVector2f(200, 0));
  const FCrowdDemoOrcaAgent ParallelB = MakeAgent(11, FVector2f(0, 300), FVector2f(200, 0));
  TestTrue(TEXT("parallel pair produces auditable VO boundary"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      ParallelA, ParallelB, Settings, 1.0f / 30.0f, 0, Constraint));
  TestTrue(TEXT("parallel same-speed current velocity remains feasible"),
    FVector2f::DotProduct(ParallelA.Velocity - Constraint.Point, Constraint.Normal)
      >= -Settings.ConstraintEpsilonCmps);

  FCrowdDemoOrcaAgent CoincidentA = MakeAgent(20, FVector2f::ZeroVector, FVector2f::ZeroVector);
  FCrowdDemoOrcaAgent CoincidentB = MakeAgent(21, FVector2f::ZeroVector, FVector2f::ZeroVector);
  FCrowdDemoOrcaConstraint CoincidentAB, CoincidentBA;
  TestTrue(TEXT("coincident AB constraint is finite"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      CoincidentA, CoincidentB, Settings, 1.0f / 30.0f, 0, CoincidentAB));
  TestTrue(TEXT("coincident BA constraint is finite"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      CoincidentB, CoincidentA, Settings, 1.0f / 30.0f, 0, CoincidentBA));
  TestTrue(TEXT("coincident pair normals are reciprocal"),
    CoincidentAB.Normal.Equals(-CoincidentBA.Normal, 0.001f));
  TestTrue(TEXT("coincident pair points are finite"),
    FMath::IsFinite(CoincidentAB.Point.X) && FMath::IsFinite(CoincidentAB.Point.Y)
      && FMath::IsFinite(CoincidentBA.Point.X) && FMath::IsFinite(CoincidentBA.Point.Y));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaReciprocalPriorityTest,
  "CrowdDemo.SF3.ORCA.ReciprocalPriority",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaReciprocalPriorityTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  auto MakeAgent = [](const int32 Id, const ECrowdDemoPortalAdmissionState State)
  {
    FCrowdDemoOrcaAgent Agent;
    Agent.AgentId = Id;
    Agent.Position = Id == 1 ? FVector2f(-60, 0) : FVector2f(60, 0);
    Agent.Velocity = Id == 1 ? FVector2f(200, 0) : FVector2f(-200, 0);
    Agent.AdmissionState = State;
    return Agent;
  };
  const auto CheckPair = [this, &Settings, &MakeAgent](
    const ECrowdDemoPortalAdmissionState AState,
    const ECrowdDemoPortalAdmissionState BState,
    const float ExpectedA,
    const float ExpectedB)
  {
    const FCrowdDemoOrcaAgent A = MakeAgent(1, AState);
    const FCrowdDemoOrcaAgent B = MakeAgent(2, BState);
    FCrowdDemoOrcaConstraint AB, BA;
    TestTrue(TEXT("priority AB constraint"),
      FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(A, B, Settings, 1.0f / 30.0f, 0, AB));
    TestTrue(TEXT("priority BA constraint"),
      FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(B, A, Settings, 1.0f / 30.0f, 0, BA));
    TestTrue(TEXT("A responsibility"), FMath::IsNearlyEqual(AB.Responsibility, ExpectedA));
    TestTrue(TEXT("B responsibility"), FMath::IsNearlyEqual(BA.Responsibility, ExpectedB));
    TestTrue(TEXT("pair responsibilities sum to one"),
      FMath::IsNearlyEqual(AB.Responsibility + BA.Responsibility, 1.0f));
  };
  CheckPair(ECrowdDemoPortalAdmissionState::Approach,
    ECrowdDemoPortalAdmissionState::Approach, 0.5f, 0.5f);
  CheckPair(ECrowdDemoPortalAdmissionState::Reserved,
    ECrowdDemoPortalAdmissionState::Waiting, 0.25f, 0.75f);
  CheckPair(ECrowdDemoPortalAdmissionState::Inside,
    ECrowdDemoPortalAdmissionState::Approach, 0.25f, 0.75f);

  FCrowdDemoOrcaAgent Committed = MakeAgent(1, ECrowdDemoPortalAdmissionState::None);
  FCrowdDemoOrcaAgent Yielding = MakeAgent(2, ECrowdDemoPortalAdmissionState::None);
  Committed.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Committed;
  Yielding.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Yielding;
  FCrowdDemoOrcaConstraint CommittedConstraint, YieldingConstraint;
  TestTrue(TEXT("committed local-priority constraint is retained"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      Committed, Yielding, Settings, 1.0f / 30.0f, 0, CommittedConstraint));
  TestTrue(TEXT("yielding local-priority constraint is retained"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      Yielding, Committed, Settings, 1.0f / 30.0f, 0, YieldingConstraint));
  TestEqual(TEXT("committed side takes 25 percent responsibility"),
    CommittedConstraint.Responsibility, 0.25f);
  TestEqual(TEXT("yielding side takes 75 percent responsibility"),
    YieldingConstraint.Responsibility, 0.75f);
  FCrowdDemoOrcaAgent EqualCommitted = Committed;
  EqualCommitted.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Normal;
  FCrowdDemoOrcaConstraint EqualConstraint;
  TestTrue(TEXT("equal-priority comparison constraint is retained"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      EqualCommitted, Yielding, Settings, 1.0f / 30.0f, 0, EqualConstraint));
  TestEqual(TEXT("priority does not change combined radius"),
    CommittedConstraint.CombinedRadiusCm, EqualConstraint.CombinedRadiusCm);
  TestEqual(TEXT("priority does not change constraint kind"),
    CommittedConstraint.Kind, EqualConstraint.Kind);
  TestTrue(TEXT("local-priority reciprocal responsibility sums to one"),
    FMath::IsNearlyEqual(CommittedConstraint.Responsibility
      + YieldingConstraint.Responsibility, 1.0f));

  FCrowdDemoOrcaAgent PortalHigherYielding = Committed;
  PortalHigherYielding.AdmissionState = ECrowdDemoPortalAdmissionState::Reserved;
  PortalHigherYielding.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Yielding;
  FCrowdDemoOrcaAgent PortalLowerCommitted = Yielding;
  PortalLowerCommitted.AdmissionState = ECrowdDemoPortalAdmissionState::Waiting;
  PortalLowerCommitted.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Committed;
  TestTrue(TEXT("portal-priority constraint is built"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      PortalHigherYielding, PortalLowerCommitted, Settings,
      1.0f / 30.0f, 0, CommittedConstraint));
  TestEqual(TEXT("portal priority lexicographically outranks local priority"),
    CommittedConstraint.Responsibility, 0.25f);

  TArray<FCrowdDemoOrcaResult> ForwardResults, ReverseResults;
  FCrowdDemoOrcaSummary ForwardSummary, ReverseSummary;
  FCrowdDemoDeterministicOrcaKernel::Solve(
    TArray<FCrowdDemoOrcaAgent>{Committed, Yielding}, Settings,
    1.0f / 30.0f, ForwardResults, ForwardSummary);
  FCrowdDemoDeterministicOrcaKernel::Solve(
    TArray<FCrowdDemoOrcaAgent>{Yielding, Committed}, Settings,
    1.0f / 30.0f, ReverseResults, ReverseSummary);
  TestEqual(TEXT("priority hash is input-order invariant"),
    ForwardSummary.PriorityHash, ReverseSummary.PriorityHash);
  TestTrue(TEXT("asymmetric pair is observed"),
    ForwardSummary.PriorityAsymmetricPairCount > 0);
  TestEqual(TEXT("reciprocal responsibility audit has no violation"),
    ForwardSummary.PriorityResponsibilitySumViolationCount, 0);

  FCrowdDemoOrcaAgent BaselineCommit = Committed;
  FCrowdDemoOrcaAgent BaselineStable = Yielding;
  BaselineCommit.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Normal;
  BaselineStable.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Normal;
  BaselineCommit.Position = FVector2f(0, 0);
  BaselineStable.Position = FVector2f(100, 0);
  BaselineCommit.Velocity = FVector2f(200, 0);
  BaselineStable.Velocity = FVector2f::ZeroVector;
  BaselineCommit.PreferredVelocity = FVector2f(400, 0);
  BaselineStable.PreferredVelocity = FVector2f::ZeroVector;
  FCrowdDemoOrcaAgent PriorityCommit = BaselineCommit;
  FCrowdDemoOrcaAgent PriorityStable = BaselineStable;
  PriorityCommit.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Committed;
  PriorityStable.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Yielding;
  TArray<FCrowdDemoOrcaResult> BaselineResults, PriorityResults;
  FCrowdDemoOrcaSummary BaselineSummary, PrioritySummary;
  FCrowdDemoDeterministicOrcaKernel::Solve(
    TArray<FCrowdDemoOrcaAgent>{BaselineCommit, BaselineStable}, Settings,
    1.0f / 30.0f, BaselineResults, BaselineSummary);
  FCrowdDemoDeterministicOrcaKernel::Solve(
    TArray<FCrowdDemoOrcaAgent>{PriorityCommit, PriorityStable}, Settings,
    1.0f / 30.0f, PriorityResults, PrioritySummary);
  const FCrowdDemoOrcaResult* BaselineCommitResult = BaselineResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 1; });
  const FCrowdDemoOrcaResult* BaselineStableResult = BaselineResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 2; });
  const FCrowdDemoOrcaResult* PriorityCommitResult = PriorityResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 1; });
  const FCrowdDemoOrcaResult* PriorityStableResult = PriorityResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 2; });
  TestNotNull(TEXT("baseline commit result exists"), BaselineCommitResult);
  TestNotNull(TEXT("baseline stable result exists"), BaselineStableResult);
  TestNotNull(TEXT("priority commit result exists"), PriorityCommitResult);
  TestNotNull(TEXT("priority stable result exists"), PriorityStableResult);
  if (BaselineCommitResult && BaselineStableResult && PriorityCommitResult && PriorityStableResult)
  {
    TestTrue(TEXT("committed agent keeps at least baseline forward speed"),
      PriorityCommitResult->Velocity.X >= BaselineCommitResult->Velocity.X);
    TestTrue(TEXT("yielding stable takes at least baseline velocity correction"),
      (PriorityStableResult->Velocity - BaselineStable.PreferredVelocity).Size()
        >= (BaselineStableResult->Velocity - BaselineStable.PreferredVelocity).Size());
    TestTrue(TEXT("priority commit result remains inside speed circle"),
      PriorityCommitResult->Velocity.Size() <= PriorityCommit.MaxSpeedCmps
        + Settings.ConstraintEpsilonCmps);
    TestTrue(TEXT("priority stable result remains inside speed circle"),
      PriorityStableResult->Velocity.Size() <= PriorityStable.MaxSpeedCmps
        + Settings.ConstraintEpsilonCmps);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOrcaHalfPlaneLpTest,
  "CrowdDemo.SF3.ORCA.HalfPlaneLpCorrectness",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOrcaHalfPlaneLpTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  TArray<FCrowdDemoOrcaConstraint> Constraints;
  Constraints.Add({1,FVector2f(100,0),FVector2f(1,0)});
  Constraints.Add({2,FVector2f(0,50),FVector2f(0,1)});
  Constraints.Add({3,FVector2f(200,200),FVector2f(0.70710677f,0.70710677f)});
  FVector2f Velocity;
  TestTrue(TEXT("incremental LP finds feasible velocity"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityForConstraints(
      FVector2f(-200,-200),800.0f,Constraints,Settings,Velocity));
  TestTrue(TEXT("LP result stays in speed circle"),Velocity.Size()<=800.0f+Settings.ConstraintEpsilonCmps);
  for (const FCrowdDemoOrcaConstraint& Constraint : Constraints)
  {
    TestTrue(TEXT("later constraint does not break prior half-plane"),
      FVector2f::DotProduct(Velocity-Constraint.Point,Constraint.Normal)>=-Settings.ConstraintEpsilonCmps);
  }

  TArray<FCrowdDemoOrcaConstraint> Impossible;
  Impossible.Add({1,FVector2f(700,0),FVector2f(1,0)});
  Impossible.Add({2,FVector2f(-700,0),FVector2f(-1,0)});
  TestFalse(TEXT("contradictory half-planes report infeasible"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityForConstraints(
      FVector2f::ZeroVector,800.0f,Impossible,Settings,Velocity));

  TArray<FCrowdDemoOrcaConstraint> OutsideCircle;
  OutsideCircle.Add({1,FVector2f(900,0),FVector2f(1,0)});
  FVector2f Continuous;
  TestFalse(TEXT("single half-plane outside speed circle is infeasible"),
    FCrowdDemoDeterministicOrcaKernel::SolveVelocityHalfPlanes(
      FVector2f::ZeroVector,800.0f,OutsideCircle,Settings,Continuous));

  auto MakeAgent = [](const int32 Id, const FVector2f Position, const FVector2f VelocityValue,
    const FVector2f Preferred, const ECrowdDemoPortalAdmissionState State)
  {
    FCrowdDemoOrcaAgent Agent;
    Agent.AgentId=Id;
    Agent.Position=Position;
    Agent.Velocity=VelocityValue;
    Agent.PreferredVelocity=Preferred;
    Agent.FlowDirection=Preferred.GetSafeNormal();
    Agent.PortalDirection=FVector2f(0,1);
    Agent.AdmissionState=State;
    return Agent;
  };
  TArray<TArray<FCrowdDemoOrcaAgent>> Scenarios;
  Scenarios.Add({
    MakeAgent(1,FVector2f(-60,0),FVector2f(200,0),FVector2f(300,0),ECrowdDemoPortalAdmissionState::Approach),
    MakeAgent(2,FVector2f(60,0),FVector2f(-200,0),FVector2f(-300,0),ECrowdDemoPortalAdmissionState::Approach)});
  Scenarios.Add({
    MakeAgent(1,FVector2f(-80,0),FVector2f(200,0),FVector2f(300,0),ECrowdDemoPortalAdmissionState::Approach),
    MakeAgent(2,FVector2f(0,-80),FVector2f(0,200),FVector2f(0,300),ECrowdDemoPortalAdmissionState::Waiting)});
  Scenarios.Add({
    MakeAgent(1,FVector2f(0,0),FVector2f(100,0),FVector2f(300,0),ECrowdDemoPortalAdmissionState::Reserved),
    MakeAgent(2,FVector2f(60,0),FVector2f(50,0),FVector2f(200,0),ECrowdDemoPortalAdmissionState::Waiting),
    MakeAgent(3,FVector2f(-60,0),FVector2f(-50,0),FVector2f(-200,0),ECrowdDemoPortalAdmissionState::Waiting)});
  for (int32 ScenarioIndex=0;ScenarioIndex<Scenarios.Num();++ScenarioIndex)
  {
    TArray<FCrowdDemoOrcaResult> Results;
    FCrowdDemoOrcaSummary Summary;
    FCrowdDemoDeterministicOrcaKernel::Solve(
      Scenarios[ScenarioIndex],Settings,1.0f/30.0f,Results,Summary);
    TestEqual(TEXT("scenario returns one result per agent"),Results.Num(),Scenarios[ScenarioIndex].Num());
    for (const FCrowdDemoOrcaResult& Result : Results)
    {
      bool bVerified = Result.Velocity.Size()<=800.0f+Settings.ConstraintEpsilonCmps;
      for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
      {
        bVerified &= FVector2f::DotProduct(Result.Velocity-Constraint.Point,Constraint.Normal)
          >=-Settings.ConstraintEpsilonCmps;
      }
      TestEqual(TEXT("result constraint flag matches independent verification"),
        Result.bOutputSatisfiesConstraints,bVerified);
    }
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoAdaptiveSpacingTest,
  "CrowdDemo.SF4.Transit.AdaptiveSpacing",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoAdaptiveSpacingTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTransitCapacitySettings CapacitySettings;
  FCrowdDemoTransitApertureResult P0Aperture;
  FCrowdDemoJointVelocityKernel::EvaluateTransitAperture(
    CapacitySettings, 128.0f, P0Aperture);
  TestTrue(TEXT("P0 transit aperture formula is valid"), P0Aperture.bValid);
  TestEqual(TEXT("P0 hard pair distance is 94cm"),
    P0Aperture.HardPairDistanceCm, 94);
  TestEqual(TEXT("P0 required transit aperture is 188cm"),
    P0Aperture.RequiredTransitApertureCm, 188);
  TestEqual(TEXT("P0 baseline pair distance is 128cm"),
    P0Aperture.BaselinePairDistanceCm, 128);
  TestEqual(TEXT("P0 preferred spacing gap is 34cm"),
    P0Aperture.PreferredSpacingGapCm, 34);
  TestEqual(TEXT("P0 30cm bilateral yield exposes a 42cm transit radius"),
    P0Aperture.AvailableTransitRadiusCm, 42);
  TestEqual(TEXT("P0 baseline plus bilateral yield has no aperture deficit"),
    P0Aperture.ApertureDeficitCm, 0);
  TestEqual(TEXT("P0 baseline requires 60cm aggregate yield"),
    P0Aperture.YieldBudgetRequiredCm, 60);

  FCrowdDemoTransitApertureResult HardPacked;
  FCrowdDemoJointVelocityKernel::EvaluateTransitAperture(
    CapacitySettings, 94.0f, HardPacked);
  TestEqual(TEXT("hard-packed pair cannot pass a nominal agent"),
    HardPacked.ApertureDeficitCm, 34);
  FCrowdDemoTransitCapacitySettings InsufficientYield = CapacitySettings;
  InsufficientYield.YieldBudgetBCm = 10.0f;
  FCrowdDemoTransitApertureResult YieldDeficit;
  FCrowdDemoJointVelocityKernel::EvaluateTransitAperture(
    InsufficientYield, 128.0f, YieldDeficit);
  TestEqual(TEXT("insufficient unilateral yield reports exact deficit"),
    YieldDeficit.ApertureDeficitCm, 20);
  TestEqual(TEXT("insufficient yield increases constructed baseline spacing"),
    YieldDeficit.BaselinePairDistanceCm, 148);
  FCrowdDemoTransitCapacitySettings UnequalCapacity = CapacitySettings;
  UnequalCapacity.PhysicalRadiusBCm = 30.0f;
  UnequalCapacity.NominalTransitRadiusCm = 55.0f;
  FCrowdDemoTransitApertureResult UnequalAperture;
  FCrowdDemoJointVelocityKernel::EvaluateTransitAperture(
    UnequalCapacity, 142.0f, UnequalAperture);
  TestEqual(TEXT("unequal blockers preserve individual radii"),
    UnequalAperture.HardPairDistanceCm, 82);
  TestEqual(TEXT("different transit radius changes required aperture"),
    UnequalAperture.RequiredTransitApertureCm, 202);

  const float HardDistance = FCrowdDemoJointVelocityKernel::HardPairDistanceCm(
    42.0f, 30.0f, 10.0f);
  TestEqual(TEXT("hard distance uses unequal physical radii and pair gap"), HardDistance, 82.0f);
  const float PreferredZero = FCrowdDemoJointVelocityKernel::PreferredPairDistanceCm(
    HardDistance, 20.0f, 0);
  const float PreferredHalf = FCrowdDemoJointVelocityKernel::PreferredPairDistanceCm(
    HardDistance, 20.0f, 16384);
  const float PreferredFull = FCrowdDemoJointVelocityKernel::PreferredPairDistanceCm(
    HardDistance, 20.0f, 32767);
  TestEqual(TEXT("zero context compresses only soft spacing"), PreferredZero, HardDistance);
  TestTrue(TEXT("half context lies strictly between hard and full spacing"),
    PreferredHalf > PreferredZero && PreferredHalf < PreferredFull);
  TestEqual(TEXT("full context preserves all preferred spacing"), PreferredFull, 102.0f);

  FCrowdDemoSharedFlowField CapacityField;
  TestTrue(TEXT("capacity fixture builds SF1 field"),
    FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(20), CapacityField));
  FCrowdDemoPursuitTargetFact CapacityTarget;
  CapacityTarget.TargetId = 7;
  CapacityTarget.Location = FVector2f(2200.0f, 1600.0f);
  CapacityTarget.RadiusCm = 80.0f;
  FCrowdDemoPursuitPositioningSettings PositionSettings;
  PositionSettings.AllowedDistanceMaxCm = 1000.0f;
  PositionSettings.PreferredDistanceMaxCm = 480.0f;
  TArray<FCrowdDemoPositionCandidate> PositionCandidates;
  FCrowdDemoPositioningSummary PositionSummary;
  FCrowdDemoPursuitPositioningKernel::BuildCandidates(CapacityTarget, 42.0f,
    PositionSettings, CapacityField, PositionCandidates, PositionSummary);
  TArray<FCrowdDemoHoldingCandidate> HoldingCandidates;
  FCrowdDemoHoldingSummary HoldingSummary;
  FCrowdDemoPursuitPositioningKernel::BuildHoldingCandidates(CapacityTarget, 42.0f,
    PositionSettings, CapacityField, PositionCandidates, HoldingCandidates, HoldingSummary);
  TArray<FCrowdDemoTransitCapacityCandidate> CapacityPositions;
  for (const FCrowdDemoPositionCandidate& Candidate : PositionCandidates)
    CapacityPositions.Add({Candidate.PositionId, Candidate.WorldLocation});
  TArray<FCrowdDemoTransitCapacityCandidate> CapacityHoldings;
  for (const FCrowdDemoHoldingCandidate& Candidate : HoldingCandidates)
    CapacityHoldings.Add({Candidate.HoldingId, Candidate.WorldLocation});
  FCrowdDemoTransitCapacityResult CapacityForward;
  FCrowdDemoJointVelocityKernel::EvaluateTransitCapacity(CapacitySettings,
    CapacityPositions, CapacityHoldings, CapacityForward);
  TestTrue(TEXT("Static Small constructed capacity is valid"), CapacityForward.bValid);
  TestTrue(TEXT("Static Small has at least twenty 128cm Position candidates"),
    CapacityForward.PositionCapacity >= 20);
  TestTrue(TEXT("Static Small has at least twenty 128cm Holding candidates"),
    CapacityForward.HoldingCapacity >= 20);
  TestEqual(TEXT("Static Small Position capacity deficit is truthful zero"),
    CapacityForward.PositionCapacityDeficit, 0);
  TestEqual(TEXT("Static Small Holding capacity deficit is truthful zero"),
    CapacityForward.HoldingCapacityDeficit, 0);
  const auto ValidateSelectedSpacing = [&](const TArray<int32>& Selected,
    const TArray<FCrowdDemoTransitCapacityCandidate>& Source)
  {
    for (int32 AIndex = 0; AIndex < Selected.Num(); ++AIndex)
      for (int32 BIndex = AIndex + 1; BIndex < Selected.Num(); ++BIndex)
      {
        const auto* A = Source.FindByPredicate([&](const auto& C)
          { return C.StableId == Selected[AIndex]; });
        const auto* B = Source.FindByPredicate([&](const auto& C)
          { return C.StableId == Selected[BIndex]; });
        if (!A || !B || (A->Location - B->Location).Size() < 128.0f - 0.01f)
          return false;
      }
    return true;
  };
  TestTrue(TEXT("selected Position candidates maintain 128cm spacing"),
    ValidateSelectedSpacing(CapacityForward.SelectedPositionIds, CapacityPositions));
  TestTrue(TEXT("selected Holding candidates maintain 128cm spacing"),
    ValidateSelectedSpacing(CapacityForward.SelectedHoldingIds, CapacityHoldings));
  const TSet<int32> SelectedPositionIds(CapacityForward.SelectedPositionIds);
  const TSet<int32> SelectedHoldingIds(CapacityForward.SelectedHoldingIds);
  TArray<FCrowdDemoPositionCandidate> FormalPositions = PositionCandidates;
  FormalPositions.RemoveAll([&](const FCrowdDemoPositionCandidate& Candidate)
    { return !SelectedPositionIds.Contains(Candidate.PositionId); });
  TArray<FCrowdDemoHoldingCandidate> FormalHoldings = HoldingCandidates;
  FormalHoldings.RemoveAll([&](const FCrowdDemoHoldingCandidate& Candidate)
    { return !SelectedHoldingIds.Contains(Candidate.HoldingId); });
  TestEqual(TEXT("formal Position SoA consumes every selected id"),
    FormalPositions.Num(), CapacityForward.PositionCapacity);
  TestEqual(TEXT("formal Holding SoA consumes every selected id"),
    FormalHoldings.Num(), CapacityForward.HoldingCapacity);
  TArray<FCrowdDemoHoldingPositionCompatibility> FormalCompatibility;
  for (const FCrowdDemoHoldingCandidate& Holding : FormalHoldings)
    for (const FCrowdDemoPositionCandidate& Position : FormalPositions)
      FormalCompatibility.Add(
        FCrowdDemoPursuitPositioningKernel::EvaluateHoldingPositionCompatibility(
          CapacityTarget, 42.0f, PositionSettings, CapacityField,
          Holding, Position, {}));
  TArray<FCrowdDemoJointPositioningAgent> FormalAgents;
  TArray<FCrowdDemoJointAgentHoldingEdge> FormalAgentHoldingEdges;
  for (int32 AgentId = 0; AgentId < 20; ++AgentId)
  {
    FCrowdDemoJointPositioningAgent& Agent = FormalAgents.AddDefaulted_GetRef();
    Agent.AgentId = AgentId;
    Agent.Location = FVector2f(0.0f, -2800.0f + 100.0f * AgentId);
    Agent.TargetRevision = CapacityTarget.Revision;
    for (const FCrowdDemoHoldingCandidate& Holding : FormalHoldings)
      FormalAgentHoldingEdges.Add({AgentId, Holding.HoldingId,
        FMath::RoundToInt((Holding.WorldLocation - Agent.Location).Size()), true});
  }
  FCrowdDemoJointPositioningResult FormalJoint;
  FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
    CapacityTarget.Revision, FormalAgents, FormalHoldings, FormalPositions,
    FormalAgentHoldingEdges, FormalCompatibility, FormalJoint);
  TestTrue(TEXT("formal 128cm candidate pools preserve a valid joint plan"),
    FormalJoint.bValid);
  TestEqual(TEXT("formal 128cm candidate pools jointly assign twenty agents"),
    FormalJoint.MaximumCardinality, 20);
  const uint32 CapacityHash = CapacityForward.CapacityHash;
  Algo::Reverse(CapacityPositions);
  Algo::Reverse(CapacityHoldings);
  FCrowdDemoTransitCapacityResult CapacityReverse;
  FCrowdDemoJointVelocityKernel::EvaluateTransitCapacity(CapacitySettings,
    CapacityPositions, CapacityHoldings, CapacityReverse);
  TestEqual(TEXT("capacity input reversal preserves stable hash"),
    CapacityReverse.CapacityHash, CapacityHash);
  AddInfo(FString::Printf(TEXT("CapacityByConstruction hard=%d required=%d baseline=%d preferred_gap=%d position=%d holding=%d hash=%u"),
    CapacityForward.Aperture.HardPairDistanceCm,
    CapacityForward.Aperture.RequiredTransitApertureCm,
    CapacityForward.Aperture.BaselinePairDistanceCm,
    CapacityForward.Aperture.PreferredSpacingGapCm,
    CapacityForward.PositionCapacity, CapacityForward.HoldingCapacity,
    CapacityForward.CapacityHash));

  FCrowdDemoJointVelocityAgent A;
  A.AgentId = 1;
  A.Position = FVector2f(0.0f, 0.0f);
  A.Velocity = FVector2f(121.0f, -37.0f);
  A.PreferredVelocity = FVector2f(400.0f, 0.0f);
  A.PhysicalRadiusCm = 42.0f;
  A.bTransitSeed = true;
  A.MotionWeightQ8 = 2048;
  A.BaselinePriorityOrcaVelocity = FVector2f::ZeroVector;
  FCrowdDemoJointVelocityAgent B = A;
  B.AgentId = 2;
  B.Position = FVector2f(100.0f, 0.0f);
  B.Velocity = FVector2f(-11.0f, 17.0f);
  B.PreferredVelocity = FVector2f::ZeroVector;
  B.MotionWeightQ8 = 256;
  B.bTransitSeed = false;

  FCrowdDemoAdaptiveSpacingSettings Spacing;
  Spacing.HardSafetyGapCm = 10.0f;
  Spacing.PreferredSpacingGapCm = 40.0f;
  Spacing.DefaultContextScaleQ15 = 32767;
  FCrowdDemoOrcaSettings Orca;
  FCrowdDemoJointVelocityPair Pair;
  TestTrue(TEXT("canonical joint pair builds"),
    FCrowdDemoJointVelocityKernel::BuildPair(A, B, Spacing, Orca, Pair));
  TestTrue(TEXT("canonical joint pair is valid"), Pair.Canonical.bValid);
  TestEqual(TEXT("pair hard distance preserves physical radius"),
    FCrowdDemoJointVelocityKernel::HardPairDistanceCm(
      A.PhysicalRadiusCm, B.PhysicalRadiusCm, Pair.HardSafetyGapCm), 94.0f);

  FCrowdDemoOrcaAgent OrcaA;
  OrcaA.AgentId = A.AgentId;
  OrcaA.Position = A.Position;
  OrcaA.Velocity = A.Velocity;
  OrcaA.PreferredVelocity = A.PreferredVelocity;
  OrcaA.RadiusCm = A.PhysicalRadiusCm;
  OrcaA.MaxSpeedCmps = A.MaxSpeedCmps;
  OrcaA.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Committed;
  FCrowdDemoOrcaAgent OrcaB;
  OrcaB.AgentId = B.AgentId;
  OrcaB.Position = B.Position;
  OrcaB.Velocity = B.Velocity;
  OrcaB.PreferredVelocity = B.PreferredVelocity;
  OrcaB.RadiusCm = B.PhysicalRadiusCm;
  OrcaB.MaxSpeedCmps = B.MaxSpeedCmps;
  OrcaB.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Yielding;
  FCrowdDemoOrcaCanonicalPairGeometry Canonical;
  FCrowdDemoOrcaConstraint PriorityConstraint;
  TestTrue(TEXT("responsibility-neutral canonical geometry builds"),
    FCrowdDemoDeterministicOrcaKernel::BuildCanonicalPairGeometry(
      OrcaA, OrcaB, Orca, Spacing.FixedStepSeconds, Canonical));
  TestTrue(TEXT("priority constraint reconstructs from canonical geometry"),
    FCrowdDemoDeterministicOrcaKernel::BuildPairConstraint(
      OrcaA, OrcaB, Orca, Spacing.FixedStepSeconds, 0, PriorityConstraint));
  const FVector2f ExpectedPoint(
    FMath::RoundToFloat((Canonical.QuantizedAgentVelocity.X
      + Canonical.Correction.X * PriorityConstraint.Responsibility)
      / Orca.VelocityQuantumCmps) * Orca.VelocityQuantumCmps,
    FMath::RoundToFloat((Canonical.QuantizedAgentVelocity.Y
      + Canonical.Correction.Y * PriorityConstraint.Responsibility)
      / Orca.VelocityQuantumCmps) * Orca.VelocityQuantumCmps);
  TestTrue(TEXT("canonical reconstruction preserves priority point exactly"),
    PriorityConstraint.Point.Equals(ExpectedPoint, 0.001f));
  TestTrue(TEXT("canonical reconstruction preserves normal exactly"),
    PriorityConstraint.Normal.Equals(Canonical.Normal, 0.00001f));
  TestEqual(TEXT("canonical reconstruction preserves kind"),
    static_cast<uint8>(PriorityConstraint.Kind), static_cast<uint8>(Canonical.Kind));

  TArray<FCrowdDemoJointVelocityAgent> Agents = {A, B};
  TArray<FCrowdDemoJointVelocityPair> Pairs = {Pair};
  Pairs[0].RequestedOwnerMask = static_cast<uint8>(ECrowdDemoSpacingPairOwner::JointSolver)
    | static_cast<uint8>(ECrowdDemoSpacingPairOwner::SoftSeparation);
  TArray<FCrowdDemoJointVelocityComponent> Components;
  FCrowdDemoJointVelocitySummary Summary;
  TestFalse(TEXT("one pair cannot have joint and soft spacing owners"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      Agents, Pairs, Spacing, Components, Summary));
  TestEqual(TEXT("double owner is explicitly counted"), Summary.SpacingPairDoubleOwnerCount, 1);

  auto SolveSpacingFixture = [&](const float InitialDistanceCm, const int32 ContextScaleQ15,
    FCrowdDemoJointVelocityComponentResult& OutResult)
  {
    FCrowdDemoJointVelocityAgent Left;
    Left.AgentId = 11;
    Left.Position = FVector2f(0.0f, 0.0f);
    Left.PhysicalRadiusCm = 20.0f;
    Left.MaxSpeedCmps = 800.0f;
    Left.bTransitSeed = true;
    FCrowdDemoJointVelocityAgent Right = Left;
    Right.AgentId = 12;
    Right.Position = FVector2f(InitialDistanceCm, 0.0f);
    Right.bTransitSeed = false;
    FCrowdDemoJointVelocityPair SpacingPair;
    SpacingPair.AgentAId = 11;
    SpacingPair.AgentBId = 12;
    SpacingPair.PreferredSpacingGapCm = 20.0f;
    SpacingPair.ContextScaleQ15 = ContextScaleQ15;
    SpacingPair.SpacingWeightQ8 = 256;
    SpacingPair.RequestedOwnerMask = static_cast<uint8>(ECrowdDemoSpacingPairOwner::JointSolver);
    SpacingPair.Canonical.AgentId = 11;
    SpacingPair.Canonical.OtherAgentId = 12;
    SpacingPair.Canonical.RelativeVelocityPoint = FVector2f(0.0f, -10000.0f);
    SpacingPair.Canonical.Normal = FVector2f(0.0f, 1.0f);
    SpacingPair.Canonical.bValid = true;
    TArray<FCrowdDemoJointVelocityAgent> SpacingAgents = {Left, Right};
    TArray<FCrowdDemoJointVelocityPair> SpacingPairs = {SpacingPair};
    TArray<FCrowdDemoJointVelocityComponent> SpacingComponents;
    FCrowdDemoJointVelocitySummary SpacingSummary;
    if (!FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      SpacingAgents, SpacingPairs, Spacing, SpacingComponents, SpacingSummary)) return false;
    TArray<FCrowdDemoJointVelocityComponentResult> SpacingResults;
    FCrowdDemoJointVelocityKernel::Solve(
      SpacingAgents, SpacingPairs, SpacingComponents, Spacing, SpacingResults, SpacingSummary);
    if (SpacingResults.Num() != 1) return false;
    OutResult = MoveTemp(SpacingResults[0]);
    return true;
  };
  FCrowdDemoJointVelocityComponentResult OpenSpacingResult;
  TestTrue(TEXT("open-area spacing fixture solves"),
    SolveSpacingFixture(60.0f, 32767, OpenSpacingResult));
  TestEqual(TEXT("open area restores preferred spacing"),
    OpenSpacingResult.PreferredSpacingSatisfiedPairCount, 1);
  TestEqual(TEXT("open-area spacing never violates hard distance"),
    OpenSpacingResult.HardPairDistanceViolationCount, 0);
  FCrowdDemoJointVelocityComponentResult CompressedSpacingResult;
  TestTrue(TEXT("narrow-context spacing fixture solves"),
    SolveSpacingFixture(40.0f, 0, CompressedSpacingResult));
  TestEqual(TEXT("narrow context cancels only the soft spacing deficit"),
    CompressedSpacingResult.PreferredSpacingSatisfiedPairCount, 1);
  TestEqual(TEXT("narrow context still preserves hard distance"),
    CompressedSpacingResult.HardPairDistanceViolationCount, 0);
  FCrowdDemoJointVelocityComponentResult RecoveringSpacingResult;
  TestTrue(TEXT("open-area spacing recovery fixture solves"),
    SolveSpacingFixture(50.0f, 32767, RecoveringSpacingResult));
  TestEqual(TEXT("one-step soft recovery reports remaining compressible deficit"),
    RecoveringSpacingResult.SpacingCompressedPairCount, 1);
  TestEqual(TEXT("soft recovery never compresses hard distance"),
    RecoveringSpacingResult.HardPairDistanceViolationCount, 0);
  TestTrue(TEXT("open-area deficit produces deterministic separating velocity"),
    RecoveringSpacingResult.Agents[0].Velocity.Size()
      > CompressedSpacingResult.Agents[0].Velocity.Size() + 1.0f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoJointVelocityTest,
  "CrowdDemo.SF4.Transit.JointVelocity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoJointVelocityTest::RunTest(const FString& Parameters)
{
  FCrowdDemoAdaptiveSpacingSettings Settings;
  Settings.HardSafetyGapCm = 0.0f;
  Settings.PreferredSpacingGapCm = 0.0f;
  Settings.DefaultContextScaleQ15 = 0;
  Settings.RelaxationQ15 = 8192;
  Settings.TransitClearanceWeightQ8 = 256;
  Settings.SolverIterations = 128;
  FCrowdDemoJointVelocityAgent Transit;
  Transit.AgentId = 1;
  Transit.Position = FVector2f(0.0f, 0.0f);
  Transit.PreferredVelocity = FVector2f(400.0f, 0.0f);
  Transit.BaselinePriorityOrcaVelocity = FVector2f::ZeroVector;
  Transit.PhysicalRadiusCm = 42.0f;
  Transit.MotionWeightQ8 = 2560;
  Transit.bTransitSeed = true;
  FCrowdDemoJointVelocityAgent Yielding = Transit;
  Yielding.AgentId = 2;
  Yielding.Position = FVector2f(84.0f, 0.0f);
  Yielding.PreferredVelocity = FVector2f::ZeroVector;
  Yielding.MotionWeightQ8 = 256;
  Yielding.bTransitSeed = false;

  TArray<FCrowdDemoJointVelocityAgent> IntentAgents = {Yielding, Transit};
  TArray<FCrowdDemoTransitIntent> IntentsForward;
  uint32 IntentHashForward = 0;
  TestTrue(TEXT("Commit-style transit intent builds"),
    FCrowdDemoJointVelocityKernel::BuildTransitIntents(
      IntentAgents, Settings, IntentsForward, IntentHashForward));
  TestEqual(TEXT("only transit seed produces a swept capsule"),
    IntentsForward.Num(), 1);
  TestTrue(TEXT("swept capsule has a predicted end"),
    IntentsForward[0].PredictedEnd.X > IntentsForward[0].Position.X);
  Algo::Reverse(IntentAgents);
  TArray<FCrowdDemoTransitIntent> IntentsReverse;
  uint32 IntentHashReverse = 0;
  TestTrue(TEXT("reversed transit intent input builds"),
    FCrowdDemoJointVelocityKernel::BuildTransitIntents(
      IntentAgents, Settings, IntentsReverse, IntentHashReverse));
  TestEqual(TEXT("transit intent input reversal preserves hash"),
    IntentHashReverse, IntentHashForward);

  FCrowdDemoOrcaConstraint PerAgentStop;
  PerAgentStop.OtherAgentId = 2;
  PerAgentStop.Point = FVector2f::ZeroVector;
  PerAgentStop.Normal = FVector2f(-1.0f, 0.0f);
  FCrowdDemoOrcaContinuousSolveInput PriorityInput;
  PriorityInput.PreferredVelocity = Transit.PreferredVelocity;
  PriorityInput.MaxSpeedCmps = Transit.MaxSpeedCmps;
  PriorityInput.BehaviorEpsilonCmps = 0.1f;
  PriorityInput.HalfPlanes.Add({PerAgentStop.Point, PerAgentStop.Normal, 0});
  const FCrowdDemoOrcaContinuousSolveResult PriorityResult =
    FCrowdDemoDeterministicOrcaKernel::SolveContinuousExact(PriorityInput);
  TestTrue(TEXT("per-agent priority half-plane is feasible"),
    PriorityResult.bSatisfiesAllHalfPlanes);
  TestTrue(TEXT("per-agent priority half-plane stops forward motion"),
    PriorityResult.Velocity.X <= 0.1f);
  Transit.BaselinePriorityOrcaVelocity = PriorityResult.Velocity;

  FCrowdDemoJointVelocityPair Pair;
  Pair.AgentAId = 1;
  Pair.AgentBId = 2;
  Pair.HardSafetyGapCm = 0.0f;
  Pair.PreferredSpacingGapCm = 0.0f;
  Pair.ContextScaleQ15 = 0;
  Pair.SpacingWeightQ8 = 256;
  Pair.RequestedOwnerMask = static_cast<uint8>(ECrowdDemoSpacingPairOwner::JointSolver);
  Pair.Canonical.AgentId = 1;
  Pair.Canonical.OtherAgentId = 2;
  Pair.Canonical.RelativeVelocityPoint = FVector2f::ZeroVector;
  Pair.Canonical.Normal = FVector2f(-1.0f, 0.0f);
  Pair.Canonical.CombinedRadiusCm = 84.0f;
  Pair.Canonical.bValid = true;
  TArray<FCrowdDemoJointVelocityAgent> Agents = {Transit, Yielding};
  TArray<FCrowdDemoJointVelocityPair> Pairs = {Pair};
  TArray<FCrowdDemoJointVelocityComponent> Components;
  FCrowdDemoJointVelocitySummary Summary;
  TestTrue(TEXT("local joint component builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      Agents, Pairs, Settings, Components, Summary));
  TArray<FCrowdDemoJointVelocityComponentResult> Results;
  FCrowdDemoJointVelocityKernel::Solve(
    Agents, Pairs, Components, Settings, Results, Summary);
  AddInfo(FString::Printf(TEXT("JointTrailing status=%d hard=%d quantized_failures=%d iteration_failures=%d transit_deficit=%.3f yielding=%d"),
    Results.IsEmpty() ? -1 : static_cast<int32>(Results[0].Status),
    Results.IsEmpty() ? -1 : Results[0].HardPairDistanceViolationCount,
    Summary.QuantizedValidationFailureCount, Summary.IterationLimitCount,
    Results.IsEmpty() ? -1.0f : Results[0].TransitCapsuleClearanceDeficitCmMax,
    Results.IsEmpty() ? -1 : Results[0].TransitYieldingAgentCount));
  TestEqual(TEXT("trailing fixture produces one component"), Results.Num(), 1);
  TestEqual(TEXT("trailing fixture is solved"),
    static_cast<uint8>(Results[0].Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::Solved));
  const FCrowdDemoJointVelocityAgentResult* TransitResult =
    Results[0].Agents.FindByPredicate([](const auto& Item) { return Item.AgentId == 1; });
  const FCrowdDemoJointVelocityAgentResult* YieldingResult =
    Results[0].Agents.FindByPredicate([](const auto& Item) { return Item.AgentId == 2; });
  TestNotNull(TEXT("transit result exists"), TransitResult);
  TestNotNull(TEXT("yielding result exists"), YieldingResult);
  if (TransitResult && YieldingResult)
  {
    TestTrue(TEXT("joint solve retains more forward speed than per-agent priority stop"),
      TransitResult->Velocity.X > PriorityResult.Velocity.X + 20.0f);
    TestTrue(TEXT("yielding neighbor moves to create safe forward space"),
      YieldingResult->Velocity.X > 20.0f);
    TestTrue(TEXT("hard pair relation remains satisfied"),
      YieldingResult->Velocity.X + Settings.ConstraintEpsilonCmps
        >= TransitResult->Velocity.X);
  }
  TestEqual(TEXT("joint solve has no hard distance violation"),
    Results[0].HardPairDistanceViolationCount, 0);
  TestEqual(TEXT("joint-owned pair leaves no soft-separation owner"),
    Pairs[0].Owner, ECrowdDemoSpacingPairOwner::JointSolver);

  const uint32 StableHash = Summary.StableHash;
  Algo::Reverse(Agents);
  FCrowdDemoJointVelocitySummary ReversedSummary;
  TArray<FCrowdDemoJointVelocityComponent> ReversedComponents;
  TestTrue(TEXT("reversed input component builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      Agents, Pairs, Settings, ReversedComponents, ReversedSummary));
  FCrowdDemoJointVelocityKernel::Solve(
    Agents, Pairs, ReversedComponents, Settings, Results, ReversedSummary);
  TestEqual(TEXT("input reversal preserves joint result hash"),
    ReversedSummary.StableHash, StableHash);

  TArray<FCrowdDemoJointVelocityAgent> MergedAgents;
  for (int32 Id = 1; Id <= 3; ++Id)
  {
    FCrowdDemoJointVelocityAgent& Agent = MergedAgents.AddDefaulted_GetRef();
    Agent.AgentId = Id;
    Agent.Position = FVector2f(static_cast<float>((Id - 1) * 80), 0.0f);
    Agent.PhysicalRadiusCm = 10.0f;
    Agent.bTransitSeed = Id != 2;
  }
  auto MakeLoosePair = [](const int32 AId, const int32 BId)
  {
    FCrowdDemoJointVelocityPair Result;
    Result.AgentAId = AId;
    Result.AgentBId = BId;
    Result.RequestedOwnerMask = static_cast<uint8>(ECrowdDemoSpacingPairOwner::JointSolver);
    Result.Canonical.AgentId = AId;
    Result.Canonical.OtherAgentId = BId;
    Result.Canonical.RelativeVelocityPoint = FVector2f(0.0f, -10000.0f);
    Result.Canonical.Normal = FVector2f(0.0f, 1.0f);
    Result.Canonical.bValid = true;
    return Result;
  };
  TArray<FCrowdDemoJointVelocityPair> MergedPairs = {
    MakeLoosePair(1, 2), MakeLoosePair(2, 3)};
  FCrowdDemoJointVelocitySummary MergedSummary;
  TArray<FCrowdDemoJointVelocityComponent> MergedComponents;
  TestTrue(TEXT("two transit seeds sharing a neighbor merge deterministically"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      MergedAgents, MergedPairs, Settings, MergedComponents, MergedSummary));
  TestEqual(TEXT("overlapping seed closures produce one component"), MergedComponents.Num(), 1);
  TestEqual(TEXT("merged component contains all agents"), MergedComponents[0].AgentIds.Num(), 3);
  TestEqual(TEXT("merged component classifies all shared-capsule agents as direct"),
    MergedComponents[0].DirectTransitRelevantAgentIds.Num(), 3);
  TArray<FCrowdDemoJointVelocityComponentResult> MergedResults;
  FCrowdDemoJointVelocityKernel::Solve(
    MergedAgents, MergedPairs, MergedComponents, Settings, MergedResults, MergedSummary);
  const uint32 MergedHash = MergedSummary.StableHash;
  Algo::Reverse(MergedAgents);
  Algo::Reverse(MergedPairs);
  FCrowdDemoJointVelocitySummary MergedReverseSummary;
  TestTrue(TEXT("fully reversed agent and pair inputs build"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      MergedAgents, MergedPairs, Settings, MergedComponents, MergedReverseSummary));
  FCrowdDemoJointVelocityKernel::Solve(
    MergedAgents, MergedPairs, MergedComponents, Settings, MergedResults, MergedReverseSummary);
  TestEqual(TEXT("fully reversed agent and pair inputs preserve hash"),
    MergedReverseSummary.StableHash, MergedHash);

  TArray<FCrowdDemoJointVelocityAgent> FarGraphAgents;
  for (int32 Id = 1; Id <= 3; ++Id)
  {
    FCrowdDemoJointVelocityAgent& Agent = FarGraphAgents.AddDefaulted_GetRef();
    Agent.AgentId = Id;
    Agent.Position = FVector2f(Id == 1 ? 0.0f : 900.0f + 100.0f * Id, 0.0f);
    Agent.PhysicalRadiusCm = 10.0f;
    Agent.bTransitSeed = Id == 1;
  }
  TArray<FCrowdDemoJointVelocityPair> FarGraphPairs = {
    MakeLoosePair(1, 2), MakeLoosePair(2, 3)};
  FCrowdDemoJointVelocitySummary FarGraphSummary;
  TArray<FCrowdDemoJointVelocityComponent> FarGraphComponents;
  TestTrue(TEXT("far ORCA pair graph remains a valid component input"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      FarGraphAgents, FarGraphPairs, Settings, FarGraphComponents, FarGraphSummary));
  TestEqual(TEXT("far ORCA chain does not expand a transit component"),
    FarGraphComponents[0].AgentIds.Num(), 1);
  TestEqual(TEXT("far ORCA chain keeps no irrelevant pair in the component"),
    FarGraphComponents[0].PairIndexes.Num(), 0);

  TArray<FCrowdDemoJointVelocityAgent> ClosureAgents;
  FCrowdDemoJointVelocityAgent ClosureSeed;
  ClosureSeed.AgentId = 1;
  ClosureSeed.Position = FVector2f::ZeroVector;
  ClosureSeed.PreferredVelocity = FVector2f(800.0f, 0.0f);
  ClosureSeed.bTransitSeed = true;
  ClosureAgents.Add(ClosureSeed);
  FCrowdDemoJointVelocityAgent DirectAgent = ClosureSeed;
  DirectAgent.AgentId = 2;
  DirectAgent.Position = FVector2f(300.0f, 80.0f);
  DirectAgent.PreferredVelocity = FVector2f::ZeroVector;
  DirectAgent.bTransitSeed = false;
  ClosureAgents.Add(DirectAgent);
  FCrowdDemoJointVelocityAgent ClosureAgent = DirectAgent;
  ClosureAgent.AgentId = 3;
  ClosureAgent.Position = FVector2f(300.0f, 170.0f);
  ClosureAgents.Add(ClosureAgent);
  TArray<FCrowdDemoJointVelocityPair> ClosurePairs = {MakeLoosePair(2, 3)};
  ClosurePairs[0].HardSafetyGapCm = 10.0f;
  FCrowdDemoJointVelocitySummary ClosureSummary;
  TArray<FCrowdDemoJointVelocityComponent> ClosureComponents;
  TestTrue(TEXT("hard-safety closure component builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      ClosureAgents, ClosurePairs, Settings, ClosureComponents, ClosureSummary));
  TestEqual(TEXT("capsule direct relevance plus hard closure contains three agents"),
    ClosureComponents[0].AgentIds.Num(), 3);
  TestEqual(TEXT("capsule direct relevance is classified separately"),
    ClosureComponents[0].DirectTransitRelevantAgentIds.Num(), 2);
  TestEqual(TEXT("hard-safety closure adds exactly one agent"),
    ClosureComponents[0].HardSafetyClosureAgentIds.Num(), 1);

  for (const int32 ComponentSize : {2, 5, 8, 12, 20})
  {
    FCrowdDemoAdaptiveSpacingSettings ScaleSettings = Settings;
    ScaleSettings.MaximumComponentAgents = ComponentSize;
    ScaleSettings.TransitPredictionHorizonSeconds = 2.0f;
    TArray<FCrowdDemoJointVelocityAgent> ScaleAgents;
    TArray<FCrowdDemoJointVelocityPair> ScalePairs;
    for (int32 Id = 1; Id <= ComponentSize; ++Id)
    {
      FCrowdDemoJointVelocityAgent Agent;
      Agent.AgentId = Id;
      Agent.Position = FVector2f(static_cast<float>((Id - 1) * 45),
        Id == 1 ? 1000.0f : 1070.0f);
      Agent.PreferredVelocity = FVector2f(800.0f, 0.0f);
      Agent.PhysicalRadiusCm = 20.0f;
      Agent.bTransitSeed = Id == 1;
      ScaleAgents.Add(Agent);
      if (Id > 1) ScalePairs.Add(MakeLoosePair(Id - 1, Id));
    }
    TArray<FCrowdDemoJointVelocityComponent> ScaleComponents;
    FCrowdDemoJointVelocitySummary ScaleSummary;
    TestTrue(*FString::Printf(TEXT("component scale %d builds"), ComponentSize),
      FCrowdDemoJointVelocityKernel::BuildLocalComponents(
        ScaleAgents, ScalePairs, ScaleSettings, ScaleComponents, ScaleSummary));
    TArray<FCrowdDemoJointVelocityComponentResult> ScaleResults;
    FCrowdDemoJointVelocityKernel::Solve(
      ScaleAgents, ScalePairs, ScaleComponents, ScaleSettings, ScaleResults, ScaleSummary);
    AddInfo(FString::Printf(TEXT("TimeAlignedScale size=%d status=%d clearance=%.3f forward_candidates=%d"),
      ComponentSize, static_cast<int32>(ScaleResults[0].Status),
      ScaleResults[0].JointCandidateClearanceDeficitCmMax,
      ScaleResults[0].Agents.Num()));
    TestEqual(*FString::Printf(TEXT("component scale %d has no truncation"), ComponentSize),
      ScaleResults[0].Agents.Num(), ComponentSize);
    TestEqual(*FString::Printf(TEXT("component scale %d solves"), ComponentSize),
      static_cast<uint8>(ScaleResults[0].Status),
      static_cast<uint8>(ECrowdDemoJointVelocityStatus::Solved));
    TestEqual(*FString::Printf(TEXT("component scale %d stays hard safe"), ComponentSize),
      ScaleResults[0].HardPairDistanceViolationCount, 0);
    const uint32 ScaleHash = ScaleSummary.StableHash;
    Algo::Reverse(ScaleAgents);
    Algo::Reverse(ScalePairs);
    FCrowdDemoJointVelocitySummary ScaleReverseSummary;
    TestTrue(*FString::Printf(TEXT("component scale %d reversed builds"), ComponentSize),
      FCrowdDemoJointVelocityKernel::BuildLocalComponents(
        ScaleAgents, ScalePairs, ScaleSettings, ScaleComponents, ScaleReverseSummary));
    FCrowdDemoJointVelocityKernel::Solve(
      ScaleAgents, ScalePairs, ScaleComponents, ScaleSettings,
      ScaleResults, ScaleReverseSummary);
    TestEqual(*FString::Printf(TEXT("component scale %d reversal preserves hash"), ComponentSize),
      ScaleReverseSummary.StableHash, ScaleHash);
  }

  FCrowdDemoSharedFlowField EnvironmentField;
  TestTrue(TEXT("joint environment fixture builds flow"),
    FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1), EnvironmentField));
  FCrowdDemoJointVelocityAgent EnvironmentAgent;
  EnvironmentAgent.AgentId = 901;
  EnvironmentAgent.Position = FVector2f(100.0f, 451.0f);
  FCrowdDemoJointVelocityComponentResult UnsafeEnvironmentResult;
  UnsafeEnvironmentResult.ComponentId = 901;
  auto& UnsafeVelocity = UnsafeEnvironmentResult.Agents.AddDefaulted_GetRef();
  UnsafeVelocity.AgentId = 901;
  UnsafeVelocity.Velocity = FVector2f(0.0f, 800.0f);
  UnsafeVelocity.JointCandidateVelocity = FVector2f(0.0f, 800.0f);
  UnsafeVelocity.BaselineVelocity = FVector2f::ZeroVector;
  UnsafeVelocity.bJointCandidateFinite = true;
  FCrowdDemoJointVelocityEnvironment Environment;
  Environment.FlowConfig = EnvironmentField.Config;
  Environment.bValidateFlowAndObstacles = true;
  FCrowdDemoJointVelocityComponentResult ValidatedEnvironment;
  TArray<FCrowdDemoJointVelocityAgent> EnvironmentAgents = {EnvironmentAgent};
  TestFalse(TEXT("joint velocity crossing obstacle 109 is rejected before production"),
    FCrowdDemoJointVelocityKernel::ValidateComponentEnvironment(
      EnvironmentAgents, UnsafeEnvironmentResult, Settings,
      Environment, ValidatedEnvironment));
  TestEqual(TEXT("joint environment validator classifies obstacle violation"),
    ValidatedEnvironment.ObstacleViolationCount, 1);
  TestEqual(TEXT("joint candidate obstacle violation is counted separately"),
    ValidatedEnvironment.JointCandidateObstacleViolationCount, 1);
  TestEqual(TEXT("baseline fallback remains clear in obstacle fixture"),
    ValidatedEnvironment.BaselineFallbackObstacleViolationCount, 0);
  EnvironmentAgent.Position = FVector2f(1500.0f, 451.0f);
  EnvironmentAgents[0] = EnvironmentAgent;
  TestTrue(TEXT("joint velocity through clear gap passes obstacle validation"),
    FCrowdDemoJointVelocityKernel::ValidateComponentEnvironment(
      EnvironmentAgents, UnsafeEnvironmentResult, Settings,
      Environment, ValidatedEnvironment));
  Environment.bValidateFlowAndObstacles = false;
  Environment.bValidateTargetExclusion = true;
  Environment.TargetLocation = FVector2f(1500.0f, 520.0f);
  Environment.TargetExclusionRadiusCm = 50.0f;
  TestFalse(TEXT("joint velocity entering Target exclusion is rejected"),
    FCrowdDemoJointVelocityKernel::ValidateComponentEnvironment(
      EnvironmentAgents, UnsafeEnvironmentResult, Settings,
      Environment, ValidatedEnvironment));
  TestEqual(TEXT("joint environment validator classifies Target violation"),
    ValidatedEnvironment.TargetViolationCount, 1);
  TestEqual(TEXT("joint candidate Target violation is counted separately"),
    ValidatedEnvironment.JointCandidateTargetViolationCount, 1);
  TestEqual(TEXT("baseline fallback remains outside Target exclusion"),
    ValidatedEnvironment.BaselineFallbackTargetViolationCount, 0);

  TArray<FCrowdDemoJointVelocityAgent> OversizeAgents;
  TArray<FCrowdDemoJointVelocityPair> OversizePairs;
  for (int32 Id = 1; Id <= 9; ++Id)
  {
    FCrowdDemoJointVelocityAgent Agent;
    Agent.AgentId = Id;
    Agent.Position = FVector2f(static_cast<float>(Id * 100), 0.0f);
    Agent.PhysicalRadiusCm = 10.0f;
    Agent.bTransitSeed = Id == 1;
    Agent.PreferredVelocity = FVector2f(800.0f, 0.0f);
    Agent.BaselinePriorityOrcaVelocity = FVector2f(static_cast<float>(Id), 0.0f);
    OversizeAgents.Add(Agent);
    if (Id > 1) OversizePairs.Add(MakeLoosePair(Id - 1, Id));
  }
  FCrowdDemoJointVelocitySummary OversizeSummary;
  TArray<FCrowdDemoJointVelocityComponent> OversizeComponents;
  FCrowdDemoAdaptiveSpacingSettings OversizeSettings = Settings;
  OversizeSettings.TransitPredictionHorizonSeconds = 2.0f;
  TestTrue(TEXT("oversize component remains a valid deterministic input"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      OversizeAgents, OversizePairs, OversizeSettings, OversizeComponents, OversizeSummary));
  TestEqual(TEXT("oversize component counted"), OversizeSummary.OversizeCount, 1);
  FCrowdDemoJointVelocityKernel::Solve(
    OversizeAgents, OversizePairs, OversizeComponents, OversizeSettings, Results, OversizeSummary);
  TestEqual(TEXT("oversize component atomically falls back"),
    static_cast<uint8>(Results[0].Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::OversizeFallback));
  TestTrue(TEXT("oversize fallback preserves every baseline velocity"),
    Results[0].Agents.ContainsByPredicate([](const auto& Item)
      { return Item.AgentId == 9 && Item.Velocity.Equals(FVector2f(9.0f, 0.0f)); }));

  FCrowdDemoJointVelocityAgent Recovery;
  Recovery.AgentId = 40;
  Recovery.Position = FVector2f::ZeroVector;
  Recovery.AssignedPosition = FVector2f(100.0f, 0.0f);
  Recovery.bHasAssignedPosition = true;
  Recovery.RecoveryWeightQ8 = 1024;
  Recovery.MotionWeightQ8 = 256;
  Recovery.bTransitSeed = true;
  TArray<FCrowdDemoJointVelocityAgent> RecoveryAgents = {Recovery};
  TArray<FCrowdDemoJointVelocityPair> NoPairs;
  TArray<FCrowdDemoJointVelocityComponent> RecoveryComponents;
  FCrowdDemoJointVelocitySummary RecoverySummary;
  TestTrue(TEXT("assigned recovery component builds without a pair"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      RecoveryAgents, NoPairs, Settings, RecoveryComponents, RecoverySummary));
  FCrowdDemoJointVelocityKernel::Solve(
    RecoveryAgents, NoPairs, RecoveryComponents, Settings, Results, RecoverySummary);
  TestEqual(TEXT("assigned recovery is solved"),
    static_cast<uint8>(Results[0].Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::Solved));
  TestTrue(TEXT("assigned recovery produces forward return velocity"),
    Results[0].Agents[0].Velocity.X > 100.0f);

  FCrowdDemoJointVelocityAgent QuantizedA;
  QuantizedA.AgentId = 51;
  QuantizedA.Position = FVector2f::ZeroVector;
  QuantizedA.PreferredVelocity = FVector2f(0.4f, 0.0f);
  QuantizedA.MaxSpeedCmps = 0.4f;
  QuantizedA.PhysicalRadiusCm = 0.0f;
  QuantizedA.bTransitSeed = true;
  FCrowdDemoJointVelocityAgent QuantizedB = QuantizedA;
  QuantizedB.AgentId = 52;
  QuantizedB.Position = FVector2f(70.0f, 0.0f);
  QuantizedB.PreferredVelocity = FVector2f::ZeroVector;
  QuantizedB.MaxSpeedCmps = 0.0f;
  QuantizedB.bTransitSeed = false;
  QuantizedB.bExternalVelocityFixed = true;
  FCrowdDemoJointVelocityPair QuantizedPair = MakeLoosePair(51, 52);
  QuantizedPair.Canonical.RelativeVelocityPoint = FVector2f(0.25f, 0.0f);
  QuantizedPair.Canonical.Normal = FVector2f(1.0f, 0.0f);
  TArray<FCrowdDemoJointVelocityAgent> QuantizedAgents = {QuantizedA, QuantizedB};
  TArray<FCrowdDemoJointVelocityPair> QuantizedPairs = {QuantizedPair};
  TArray<FCrowdDemoJointVelocityComponent> QuantizedComponents;
  FCrowdDemoJointVelocitySummary QuantizedSummary;
  TestTrue(TEXT("quantization fixture builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      QuantizedAgents, QuantizedPairs, Settings, QuantizedComponents, QuantizedSummary));
  FCrowdDemoJointVelocityKernel::Solve(
    QuantizedAgents, QuantizedPairs, QuantizedComponents, Settings, Results, QuantizedSummary);
  TestEqual(TEXT("quantization destroyed feasibility is explicit"),
    static_cast<uint8>(Results[0].Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::QuantizedValidationFailure));

  FCrowdDemoJointVelocityAgent RepairAgent;
  RepairAgent.AgentId = 61;
  RepairAgent.PreferredVelocity = FVector2f(0.7f, 0.7f);
  RepairAgent.MaxSpeedCmps = 1.0f;
  RepairAgent.PhysicalRadiusCm = 0.0f;
  RepairAgent.bTransitSeed = true;
  TArray<FCrowdDemoJointVelocityAgent> RepairAgents = {RepairAgent};
  TArray<FCrowdDemoJointVelocityComponent> RepairComponents;
  FCrowdDemoJointVelocitySummary RepairSummary;
  TestTrue(TEXT("quantized repair fixture builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      RepairAgents, NoPairs, Settings, RepairComponents, RepairSummary));
  FCrowdDemoJointVelocityKernel::Solve(
    RepairAgents, NoPairs, RepairComponents, Settings, Results, RepairSummary);
  TestEqual(TEXT("bounded 3x3 search repairs rounded speed-circle violation"),
    static_cast<uint8>(Results[0].Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::Solved));
  TestEqual(TEXT("quantized repair is counted"), RepairSummary.QuantizationRepairCount, 1);
  TestTrue(TEXT("repaired quantized velocity remains in speed circle"),
    Results[0].Agents[0].Velocity.Size() <= 1.1f);

  auto RunRingFixture = [&](const bool bStartsInside, const bool bExternalFixed,
    FCrowdDemoJointVelocityComponentResult& OutResult,
    FCrowdDemoJointVelocitySummary& OutSummary)
  {
    TArray<FCrowdDemoJointVelocityAgent> RingAgents;
    FCrowdDemoJointVelocityAgent High;
    High.AgentId = 100;
    High.Position = bStartsInside ? FVector2f::ZeroVector : FVector2f(-160.0f, 0.0f);
    High.PreferredVelocity = FVector2f(300.0f, 0.0f);
    High.ExternalVelocity = FVector2f(300.0f, 0.0f);
    High.PhysicalRadiusCm = 40.0f;
    High.MotionWeightQ8 = 2560;
    High.bTransitSeed = true;
    High.bExternalVelocityFixed = bExternalFixed;
    RingAgents.Add(High);
    for (int32 RingIndex = 0; RingIndex < 5; ++RingIndex)
    {
      const float Angle = 2.0f * PI * static_cast<float>(RingIndex) / 5.0f;
      FCrowdDemoJointVelocityAgent Low;
      Low.AgentId = 101 + RingIndex;
      Low.Position = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * 80.0f;
      Low.PhysicalRadiusCm = 20.0f;
      Low.MotionWeightQ8 = 256;
      RingAgents.Add(Low);
    }
    TArray<FCrowdDemoJointVelocityPair> RingPairs;
    for (int32 AIndex = 0; AIndex < RingAgents.Num(); ++AIndex)
    {
      for (int32 BIndex = AIndex + 1; BIndex < RingAgents.Num(); ++BIndex)
      {
        FCrowdDemoJointVelocityPair RingPair = MakeLoosePair(
          RingAgents[AIndex].AgentId, RingAgents[BIndex].AgentId);
        RingPair.PreferredSpacingGapCm = AIndex == 0 ? 30.0f : 0.0f;
        RingPair.ContextScaleQ15 = AIndex == 0 ? 32767 : 0;
        RingPairs.Add(RingPair);
      }
    }
    TArray<FCrowdDemoJointVelocityComponent> RingComponents;
    if (!FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      RingAgents, RingPairs, Settings, RingComponents, OutSummary)) return false;
    TArray<FCrowdDemoJointVelocityComponentResult> RingResults;
    FCrowdDemoJointVelocityKernel::Solve(
      RingAgents, RingPairs, RingComponents, Settings, RingResults, OutSummary);
    if (RingResults.Num() != 1) return false;
    OutResult = MoveTemp(RingResults[0]);
    return true;
  };
  FCrowdDemoJointVelocityComponentResult EnterRingResult;
  FCrowdDemoJointVelocitySummary EnterRingSummary;
  TestTrue(TEXT("large high-priority entity entering five-agent ring is evaluated"),
    RunRingFixture(false, false, EnterRingResult, EnterRingSummary));
  AddInfo(FString::Printf(TEXT("TimeAlignedRing enter_status=%d clearance=%.3f yielding=%d"),
    static_cast<int32>(EnterRingResult.Status),
    EnterRingResult.JointCandidateClearanceDeficitCmMax, EnterRingResult.YieldingAgentCount));
  TestEqual(TEXT("ring-entry fixture remains hard safe"),
    EnterRingResult.HardPairDistanceViolationCount, 0);
  TestEqual(TEXT("ring entry satisfies hard time-aligned clearance"),
    static_cast<uint8>(EnterRingResult.Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::Solved));
  TestTrue(TEXT("ring entry leaves no clearance deficit above the position quantum"),
    EnterRingResult.JointCandidateClearanceDeficitCmMax <= Settings.PositionQuantumCm);
  const FCrowdDemoJointVelocityAgentResult* FrontYieldResult =
    EnterRingResult.Agents.FindByPredicate([](const auto& Item) { return Item.AgentId == 101; });
  TestNotNull(TEXT("ring entry front yielding result exists"), FrontYieldResult);
  if (FrontYieldResult)
    TestTrue(TEXT("hard clearance projection is not softened by preferred pull"),
      FrontYieldResult->JointCandidateVelocity.X > 50.0f);

  TArray<FCrowdDemoJointVelocityAgent> CanonicalFixtureAgents;
  const auto AddCanonicalFixtureAgent = [&](const int32 AgentId, const FVector2f Position,
    const FVector2f Preferred, const bool bTransitSeed)
  {
    FCrowdDemoJointVelocityAgent& Agent = CanonicalFixtureAgents.AddDefaulted_GetRef();
    Agent.AgentId = AgentId;
    Agent.Position = Position;
    Agent.PreferredVelocity = Preferred;
    Agent.PhysicalRadiusCm = 42.0f;
    Agent.MaxSpeedCmps = 800.0f;
    Agent.MotionWeightQ8 = bTransitSeed ? 2560 : 512;
    Agent.bTransitSeed = bTransitSeed;
  };
  AddCanonicalFixtureAgent(5, FVector2f(252.0f, 2025.0f), FVector2f::ZeroVector, false);
  AddCanonicalFixtureAgent(14, FVector2f(113.0f, 1761.0f), FVector2f(216.0f, 770.0f), true);
  AddCanonicalFixtureAgent(15, FVector2f(151.0f, 2180.0f), FVector2f::ZeroVector, false);
  TArray<FCrowdDemoJointVelocityPair> CanonicalFixturePairs;
  const auto AddCanonicalFixturePair = [&](const int32 A, const int32 B,
    const FVector2f Point, const FVector2f NormalQ15)
  {
    FCrowdDemoJointVelocityPair& Pair = CanonicalFixturePairs.AddDefaulted_GetRef();
    Pair.AgentAId = A;
    Pair.AgentBId = B;
    Pair.HardSafetyGapCm = 10.0f;
    Pair.PreferredSpacingGapCm = 34.0f;
    Pair.ContextScaleQ15 = 32767;
    Pair.SpacingWeightQ8 = 256;
    Pair.RequestedOwnerMask = 1;
    Pair.Owner = ECrowdDemoSpacingPairOwner::JointSolver;
    Pair.Canonical.RelativeVelocityPoint = Point;
    Pair.Canonical.Normal = (NormalQ15 / 32767.0f).GetSafeNormal();
    Pair.Canonical.bValid = true;
  };
  AddCanonicalFixturePair(5, 14, FVector2f(-80.0f, -152.0f), FVector2f(15266.0f, 28994.0f));
  AddCanonicalFixturePair(5, 15, FVector2f(-44.0f, 68.0f), FVector2f(17889.0f, -27453.0f));
  AddCanonicalFixturePair(14, 15, FVector2f(24.0f, 268.0f), FVector2f(-2960.0f, -32633.0f));
  FCrowdDemoAdaptiveSpacingSettings CanonicalFixtureSettings = Settings;
  CanonicalFixtureSettings.HardSafetyGapCm = 10.0f;
  CanonicalFixtureSettings.PreferredSpacingGapCm = 34.0f;
  CanonicalFixtureSettings.DefaultContextScaleQ15 = 32767;
  TArray<FCrowdDemoJointVelocityComponent> CanonicalFixtureComponents;
  FCrowdDemoJointVelocitySummary CanonicalFixtureSummary;
  TestTrue(TEXT("8346 compact canonical fixture component builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(CanonicalFixtureAgents,
      CanonicalFixturePairs, CanonicalFixtureSettings,
      CanonicalFixtureComponents, CanonicalFixtureSummary));
  TArray<FCrowdDemoJointVelocityComponentResult> CanonicalFixtureResults;
  FCrowdDemoJointVelocityKernel::Solve(CanonicalFixtureAgents, CanonicalFixturePairs,
    CanonicalFixtureComponents, CanonicalFixtureSettings,
    CanonicalFixtureResults, CanonicalFixtureSummary);
  TestEqual(TEXT("8346 compact canonical fixture is solved after hard polish"),
    static_cast<uint8>(CanonicalFixtureResults[0].Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::Solved));
  TestTrue(TEXT("8346 compact fixture leaves no canonical residual"),
    !CanonicalFixtureResults[0].PairResiduals.ContainsByPredicate(
      [&](const FCrowdDemoJointVelocityPairResidual& Residual)
      {
        return Residual.JointCanonicalDeficitCmps
          > CanonicalFixtureSettings.ConstraintEpsilonCmps;
      }));
  FCrowdDemoJointVelocityComponentResult ExitRingResult;
  FCrowdDemoJointVelocitySummary ExitRingSummary;
  TestTrue(TEXT("high-priority entity exiting five-agent ring solves"),
    RunRingFixture(true, false, ExitRingResult, ExitRingSummary));
  AddInfo(FString::Printf(TEXT("TimeAlignedRing exit_status=%d clearance=%.3f yielding=%d"),
    static_cast<int32>(ExitRingResult.Status),
    ExitRingResult.JointCandidateClearanceDeficitCmMax, ExitRingResult.YieldingAgentCount));
  TestEqual(TEXT("ring-exit fixture remains hard safe"),
    ExitRingResult.HardPairDistanceViolationCount, 0);
  TestTrue(TEXT("ring exit coordinates at least two yielding agents"),
    ExitRingResult.YieldingAgentCount >= 2);
  FCrowdDemoJointVelocityComponentResult ExternalRingResult;
  FCrowdDemoJointVelocitySummary ExternalRingSummary;
  TestTrue(TEXT("fixed external velocity through ordinary ring solves"),
    RunRingFixture(true, true, ExternalRingResult, ExternalRingSummary));
  AddInfo(FString::Printf(TEXT("TimeAlignedRing external_status=%d clearance=%.3f yielding=%d"),
    static_cast<int32>(ExternalRingResult.Status),
    ExternalRingResult.JointCandidateClearanceDeficitCmMax, ExternalRingResult.YieldingAgentCount));
  const FCrowdDemoJointVelocityAgentResult* FixedResult =
    ExternalRingResult.Agents.FindByPredicate([](const auto& Item) { return Item.AgentId == 100; });
  TestNotNull(TEXT("external transit result exists"), FixedResult);
  if (FixedResult)
    TestTrue(TEXT("external transit velocity remains fixed"),
      FixedResult->Velocity.Equals(FVector2f(300.0f, 0.0f), 0.001f));
  TestEqual(TEXT("external transit remains hard safe"),
    ExternalRingResult.HardPairDistanceViolationCount, 0);

  FCrowdDemoJointVelocityAgent CoincidentA;
  CoincidentA.AgentId = 71;
  CoincidentA.PhysicalRadiusCm = 0.0f;
  CoincidentA.bTransitSeed = true;
  FCrowdDemoJointVelocityAgent CoincidentB = CoincidentA;
  CoincidentB.AgentId = 72;
  CoincidentB.bTransitSeed = false;
  TArray<FCrowdDemoJointVelocityAgent> CoincidentAgents = {CoincidentA, CoincidentB};
  TArray<FCrowdDemoJointVelocityPair> CoincidentPairs = {MakeLoosePair(71, 72)};
  FCrowdDemoAdaptiveSpacingSettings CoincidentSettings = Settings;
  CoincidentSettings.TransitClearanceWeightQ8 = 0;
  CoincidentSettings.NominalTransitRadiusCm = 0.0f;
  TArray<FCrowdDemoJointVelocityComponent> CoincidentComponents;
  FCrowdDemoJointVelocitySummary CoincidentSummary;
  TestTrue(TEXT("coincident-center fixture builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      CoincidentAgents, CoincidentPairs, CoincidentSettings,
      CoincidentComponents, CoincidentSummary));
  FCrowdDemoJointVelocityKernel::Solve(
    CoincidentAgents, CoincidentPairs, CoincidentComponents,
    CoincidentSettings, Results, CoincidentSummary);
  TestEqual(TEXT("coincident-center fixture solves with stable derived normal"),
    static_cast<uint8>(Results[0].Status),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::Solved));
  TestTrue(TEXT("coincident-center path records degenerate normal use"),
    CoincidentSummary.DegenerateNormalCount > 0);
  const uint32 CoincidentHash = CoincidentSummary.StableHash;
  Algo::Reverse(CoincidentAgents);
  FCrowdDemoJointVelocitySummary CoincidentReverseSummary;
  TestTrue(TEXT("reversed coincident input builds"),
    FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      CoincidentAgents, CoincidentPairs, CoincidentSettings,
      CoincidentComponents, CoincidentReverseSummary));
  FCrowdDemoJointVelocityKernel::Solve(
    CoincidentAgents, CoincidentPairs, CoincidentComponents,
    CoincidentSettings, Results, CoincidentReverseSummary);
  TestEqual(TEXT("coincident input reversal preserves stable hash"),
    CoincidentReverseSummary.StableHash, CoincidentHash);

  FCrowdDemoTransitJointDiagnosticAgent DiagnosticPrimary;
  DiagnosticPrimary.JointAgent = Transit;
  DiagnosticPrimary.StartLocation = Transit.Position;
  DiagnosticPrimary.PriorityOrcaVelocity = FVector2f(0.0f, 50.0f);
  DiagnosticPrimary.PredictedVelocity = FVector2f(0.0f, 50.0f);
  DiagnosticPrimary.ObstacleVelocity = FVector2f::ZeroVector;
  DiagnosticPrimary.PbdVelocity = FVector2f::ZeroVector;
  DiagnosticPrimary.ReprojectVelocity = FVector2f::ZeroVector;
  DiagnosticPrimary.FinalVelocity = FVector2f::ZeroVector;
  DiagnosticPrimary.PriorityConstraints.Add(PerAgentStop);
  FCrowdDemoTransitJointDiagnosticAgent DiagnosticOther;
  DiagnosticOther.JointAgent = Yielding;
  DiagnosticOther.StartLocation = Yielding.Position;
  TArray<FCrowdDemoTransitJointDiagnosticAgent> DiagnosticAgents = {
    DiagnosticPrimary, DiagnosticOther};
  TArray<FCrowdDemoJointVelocityPair> DiagnosticPairs = {Pair};
  FCrowdDemoTransitJointDiagnosticFixture DiagnosticFixture;
  FCrowdDemoJointVelocityKernel::BuildDiagnosticFixture(
    1, DiagnosticAgents, DiagnosticPairs, Settings, DiagnosticFixture);
  TestTrue(TEXT("final-boundary diagnostic fixture is valid"), DiagnosticFixture.bValid);
  TestEqual(TEXT("final-boundary diagnostic captures full local component"),
    DiagnosticFixture.Summary.ComponentAgentCount, 2);
  TestTrue(TEXT("nonzero ORCA followed by zero final is classified"),
    DiagnosticFixture.Summary.bPriorityNonZeroDownstreamZero);
  TestEqual(TEXT("first downstream zero is obstacle constraint"),
    static_cast<uint8>(DiagnosticFixture.Summary.DownstreamZeroStage),
    static_cast<uint8>(ECrowdDemoTransitDownstreamZeroStage::ObstacleConstraint));
  const uint32 DiagnosticHash = DiagnosticFixture.StableHash;
  Algo::Reverse(DiagnosticAgents);
  Algo::Reverse(DiagnosticPairs);
  FCrowdDemoJointVelocityKernel::BuildDiagnosticFixture(
    1, DiagnosticAgents, DiagnosticPairs, Settings, DiagnosticFixture);
  TestEqual(TEXT("diagnostic fixture input reversal preserves hash"),
    DiagnosticFixture.StableHash, DiagnosticHash);

  TArray<FCrowdDemoTransitJointDiagnosticAgent> OversizeDiagnosticAgents;
  for (const FCrowdDemoJointVelocityAgent& Agent : OversizeAgents)
  {
    FCrowdDemoTransitJointDiagnosticAgent& Diagnostic =
      OversizeDiagnosticAgents.AddDefaulted_GetRef();
    Diagnostic.JointAgent = Agent;
    Diagnostic.StartLocation = Agent.Position;
  }
  FCrowdDemoJointVelocityKernel::BuildDiagnosticFixture(
    1, OversizeDiagnosticAgents, OversizePairs, OversizeSettings, DiagnosticFixture);
  TestTrue(TEXT("oversize diagnostic remains a valid compact classification"),
    DiagnosticFixture.bValid);
  TestTrue(TEXT("oversize diagnostic is explicitly marked too large"),
    DiagnosticFixture.Summary.bFixtureTooLarge);
  TestEqual(TEXT("oversize diagnostic reports the explicit fallback status"),
    static_cast<uint8>(DiagnosticFixture.Summary.JointStatus),
    static_cast<uint8>(ECrowdDemoJointVelocityStatus::OversizeFallback));
  TestEqual(TEXT("oversize diagnostic does not serialize a truncated agent subset"),
    DiagnosticFixture.Agents.Num(), 0);

  FCrowdDemoTransitCapacityFailureFixture CapacityFailureFixture;
  FCrowdDemoJointVelocityEnvironment NoEnvironmentValidation;
  TArray<FCrowdDemoJointVelocityComponentResult> IterationFailureResults = Results;
  IterationFailureResults[0].Status = ECrowdDemoJointVelocityStatus::IterationLimit;
  FCrowdDemoJointVelocityKernel::BuildTransitCapacityFailureFixture(
    CoincidentAgents, CoincidentPairs, CoincidentComponents,
    IterationFailureResults, CoincidentSettings,
    NoEnvironmentValidation, CapacityFailureFixture);
  TestTrue(TEXT("capacity failure fixture captures a real failed component"),
    CapacityFailureFixture.bValid);
  TestEqual(TEXT("capacity failure fixture does not truncate the component"),
    CapacityFailureFixture.Agents.Num(), 2);
  TestEqual(TEXT("capacity failure fixture retains every pair residual"),
    CapacityFailureFixture.Result.PairResiduals.Num(), CoincidentPairs.Num());
  const uint32 CapacityFailureHash = CapacityFailureFixture.StableHash;
  TArray<FCrowdDemoJointVelocityAgent> ReversedFailureAgents = CoincidentAgents;
  TArray<FCrowdDemoJointVelocityPair> ReversedFailurePairs = CoincidentPairs;
  Algo::Reverse(ReversedFailureAgents);
  Algo::Reverse(ReversedFailurePairs);
  FCrowdDemoJointVelocityKernel::BuildTransitCapacityFailureFixture(
    ReversedFailureAgents, ReversedFailurePairs, CoincidentComponents,
    IterationFailureResults, CoincidentSettings, NoEnvironmentValidation,
    CapacityFailureFixture);
  TestEqual(TEXT("capacity failure fixture input reversal preserves hash"),
    CapacityFailureFixture.StableHash, CapacityFailureHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoElasticCrowdRolloutTest,
  "CrowdDemo.SF4.Elastic.Rollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoElasticCrowdRolloutTest::RunTest(const FString& Parameters)
{
  FCrowdDemoElasticCrowdSettings Settings;
  FCrowdDemoElasticCrowdEnvironment Environment;
  auto Agent = [](const int32 Id, const FVector2f Position, const FVector2f Preferred,
    const bool bSource = false, const float Radius = 42.0f)
  {
    FCrowdDemoElasticCrowdAgent A;
    A.AgentId = Id; A.Position = Position; A.BasePreferredVelocity = Preferred;
    A.PhysicalRadiusCm = Radius; A.TransitSourceRadiusCm = Radius;
    A.MaxSpeedCmps = 800.0f; A.bTransitSource = bSource;
    A.TransitDirection = Preferred.GetSafeNormal();
    return A;
  };
  auto Solve = [&](TArray<FCrowdDemoElasticCrowdAgent> Input,
    TArray<FCrowdDemoElasticCrowdResult>& Results,
    FCrowdDemoElasticCrowdSummary& Summary)
  {
    return FCrowdDemoElasticCrowdKernel::Solve(
      Input, Settings, Environment, Results, Summary);
  };

  TArray<FCrowdDemoElasticCrowdAgent> Array128 = {
    Agent(1, FVector2f(0, 0), FVector2f(120, 0)),
    Agent(2, FVector2f(128, 0), FVector2f(120, 0)),
    Agent(3, FVector2f(256, 0), FVector2f(120, 0))};
  TArray<FCrowdDemoElasticCrowdResult> Results;
  FCrowdDemoElasticCrowdSummary Summary;
  TestTrue(TEXT("128cm moving array solves"), Solve(Array128, Results, Summary));
  TestEqual(TEXT("128cm moving array has no spacing pressure"), Summary.SpacingPairCount, 0);
  TestTrue(TEXT("128cm moving array preserves base preferred"),
    Results[1].AdjustedPreferredVelocity.Equals(FVector2f(120, 0), 0.01f));

  TArray<FCrowdDemoElasticCrowdAgent> Compressed = {
    Agent(10, FVector2f(0, 0), FVector2f::ZeroVector),
    Agent(11, FVector2f(100, 0), FVector2f::ZeroVector)};
  TestTrue(TEXT("compressed pair solves"), Solve(Compressed, Results, Summary));
  TestEqual(TEXT("compressed pair has one stable owner"), Summary.SpacingPairCount, 1);
  TestTrue(TEXT("compressed pair responds symmetrically"),
    Results[0].SpacingDeltaVelocity.X < 0 && Results[1].SpacingDeltaVelocity.X > 0);
  const uint32 ForwardHash = Summary.StableHash;
  Algo::Reverse(Compressed);
  TestTrue(TEXT("reversed compressed pair solves"), Solve(Compressed, Results, Summary));
  TestEqual(TEXT("agent reversal preserves elastic hash"), Summary.StableHash, ForwardHash);

  Compressed[0].ContextScaleQ15 = 0;
  Compressed[1].ContextScaleQ15 = 0;
  TestTrue(TEXT("narrow context solves"), Solve(Compressed, Results, Summary));
  TestEqual(TEXT("narrow context compresses only soft gap"), Summary.SpacingPairCount, 0);
  Compressed[0].ContextScaleQ15 = 32767;
  Compressed[1].ContextScaleQ15 = 32767;
  float PreviousDistance = (Compressed[0].Position - Compressed[1].Position).Size();
  for (int32 Step = 0; Step < 45; ++Step)
  {
    TestTrue(TEXT("spacing recovery rollout solves"), Solve(Compressed, Results, Summary));
    for (auto& A : Compressed)
    {
      const auto* R = Results.FindByPredicate([&](const auto& Item) { return Item.AgentId == A.AgentId; });
      A.Position += R->AdjustedPreferredVelocity * Settings.FixedStepSeconds;
    }
  }
  const float RecoveredDistance = (Compressed[0].Position - Compressed[1].Position).Size();
  TestTrue(TEXT("soft spacing recovers after narrow context"), RecoveredDistance > PreviousDistance + 20.0f);
  TestTrue(TEXT("recovery does not expand indefinitely"), RecoveredDistance < 130.0f);

  auto MakeRing = [&](const bool bStartInside, const float SourceRadius)
  {
    TArray<FCrowdDemoElasticCrowdAgent> Ring;
    Ring.Add(Agent(100, bStartInside ? FVector2f::ZeroVector : FVector2f(-180, 0),
      FVector2f(220, 0), true, SourceRadius));
    for (int32 Index = 0; Index < 5; ++Index)
    {
      const float Angle = 2.0f * PI * static_cast<float>(Index) / 5.0f;
      Ring.Add(Agent(101 + Index,
        FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * 100.0f,
        FVector2f::ZeroVector, false, 20.0f));
    }
    return Ring;
  };
  auto RunRing = [&](TArray<FCrowdDemoElasticCrowdAgent> Ring,
    float& OutProgress, float& OutInitialDeficit, float& OutFinalDeficit,
    int32& OutMaxLayer)
  {
    const FVector2f Start = Ring[0].Position;
    OutInitialDeficit = OutFinalDeficit = 0.0f; OutMaxLayer = 0;
    int32 ZeroProgressSteps = 0;
    for (int32 Step = 0; Step < 90; ++Step)
    {
      if (!Solve(Ring, Results, Summary))
      { AddInfo(FString::Printf(TEXT("ElasticRing fail=kernel step=%d"), Step)); return false; }
      if (Step == 0) OutInitialDeficit = Summary.MaxTransitDeficitCm;
      OutFinalDeficit = Summary.MaxTransitDeficitCm;
      OutMaxLayer = FMath::Max(OutMaxLayer, Summary.PropagationLayerCount);
      const FVector2f Previous = Ring[0].Position;
      TArray<FCrowdDemoOrcaAgent> OrcaAgents;
      for (const auto& A : Ring)
      {
        const auto* R = Results.FindByPredicate([&](const auto& Item)
          { return Item.AgentId == A.AgentId; });
        auto& O = OrcaAgents.AddDefaulted_GetRef();
        O.AgentId = A.AgentId; O.Position = A.Position; O.Velocity = A.Velocity;
        O.PreferredVelocity = R->AdjustedPreferredVelocity;
        O.FlowDirection = O.PreferredVelocity.GetSafeNormal();
        O.PortalDirection = O.FlowDirection; O.RadiusCm = A.PhysicalRadiusCm;
        O.MaxSpeedCmps = A.MaxSpeedCmps;
        O.LocalAvoidancePriority = A.bTransitSource
          ? ECrowdDemoOrcaLocalPriority::Committed
          : ECrowdDemoOrcaLocalPriority::Yielding;
      }
      TArray<FCrowdDemoOrcaResult> OrcaResults;
      FCrowdDemoOrcaSummary OrcaSummary;
      FCrowdDemoOrcaSettings OrcaSettings;
      FCrowdDemoDeterministicOrcaKernel::Solve(OrcaAgents, OrcaSettings,
        Settings.FixedStepSeconds, OrcaResults, OrcaSummary);
      if (OrcaSummary.StopViolationCount > 0)
      { AddInfo(FString::Printf(TEXT("ElasticRing fail=orca_stop step=%d count=%d"),
          Step, OrcaSummary.StopViolationCount)); return false; }
      TArray<FCrowdDemoHardSeparationPbdAgent> PbdAgents;
      for (const auto& A : Ring)
      {
        const auto* R = OrcaResults.FindByPredicate([&](const auto& Item)
          { return Item.AgentId == A.AgentId; });
        if (!R)
        { AddInfo(FString::Printf(TEXT("ElasticRing fail=missing_orca step=%d agent=%d"),
            Step, A.AgentId)); return false; }
        auto& P = PbdAgents.AddDefaulted_GetRef(); P.AgentId = A.AgentId;
        const FVector2f Predicted = A.Position + R->Velocity * Settings.FixedStepSeconds;
        P.Location = FVector(Predicted.X, Predicted.Y, 0.0f);
        P.RadiusCm = A.PhysicalRadiusCm + Settings.HardSafetyGapCm * 0.5f;
      }
      FCrowdDemoHardSeparationPbdSettings PbdSettings;
      PbdSettings.IterationCount = 3;
      PbdSettings.MaxPairCorrectionPerIterationCm = 24.0f;
      TArray<FCrowdDemoHardSeparationPbdPair> PbdPairs;
      TArray<FCrowdDemoHardSeparationPbdResult> PbdResults;
      FCrowdDemoHardSeparationPbdSummary PbdSummary;
      FCrowdDemoHardSeparationPbdKernel::Solve(
        PbdAgents, PbdSettings, PbdPairs, PbdResults, PbdSummary);
      for (auto& A : Ring)
      {
        const auto* P = PbdResults.FindByPredicate([&](const auto& Item)
          { return Item.AgentId == A.AgentId; });
        if (!P) return false;
        const FVector2f Corrected(P->CorrectedLocation.X, P->CorrectedLocation.Y);
        A.Velocity = (Corrected - A.Position) / Settings.FixedStepSeconds;
        A.Position = Corrected;
      }
      ZeroProgressSteps = (Ring[0].Position.X - Previous.X) < 0.01f
        ? ZeroProgressSteps + 1 : 0;
      if (ZeroProgressSteps > 15)
      { AddInfo(FString::Printf(TEXT("ElasticRing fail=zero_progress step=%d"), Step)); return false; }
      for (int32 A = 0; A < Ring.Num(); ++A) for (int32 B = A + 1; B < Ring.Num(); ++B)
      {
        const float Hard = Ring[A].PhysicalRadiusCm + Ring[B].PhysicalRadiusCm
          + Settings.HardSafetyGapCm;
        const float Distance = (Ring[A].Position - Ring[B].Position).Size();
        if (Distance + 1.0f < Hard)
        { AddInfo(FString::Printf(TEXT("ElasticRing fail=hard step=%d pair=%d-%d distance=%.3f hard=%.3f"),
            Step, Ring[A].AgentId, Ring[B].AgentId, Distance, Hard)); return false; }
      }
    }
    OutProgress = Ring[0].Position.X - Start.X;
    return true;
  };
  float Progress = 0, InitialDeficit = 0, FinalDeficit = 0; int32 MaxLayer = 0;
  TestTrue(TEXT("single source enters ring over multiple fixed steps"),
    RunRing(MakeRing(false, 40.0f), Progress, InitialDeficit, FinalDeficit, MaxLayer));
  TestTrue(TEXT("ring entry source keeps forward progress"), Progress > 400.0f);
  TestTrue(TEXT("ring entry pressure eventually clears"), FinalDeficit < InitialDeficit);
  TestTrue(TEXT("single source exits ring over multiple fixed steps"),
    RunRing(MakeRing(true, 40.0f), Progress, InitialDeficit, FinalDeficit, MaxLayer));
  TestTrue(TEXT("large source traverses small-agent ring"),
    RunRing(MakeRing(false, 60.0f), Progress, InitialDeficit, FinalDeficit, MaxLayer));

  TArray<FCrowdDemoElasticCrowdAgent> Propagation = {
    Agent(150, FVector2f(-100, 0), FVector2f(180, 0), true, 42),
    Agent(151, FVector2f(0, 60), FVector2f::ZeroVector, false, 20),
    Agent(152, FVector2f(0, 140), FVector2f::ZeroVector, false, 20)};
  TestTrue(TEXT("two-layer propagation fixture solves"), Solve(Propagation, Results, Summary));
  TestTrue(TEXT("transit pressure reaches a second spacing layer"),
    Summary.PropagationLayerCount >= 2);

  TArray<FCrowdDemoElasticCrowdAgent> Crossing = {
    Agent(200, FVector2f(-150, 0), FVector2f(200, 0), true, 40),
    Agent(201, FVector2f(0, -150), FVector2f(0, 200), true, 40),
    Agent(202, FVector2f(0, 0), FVector2f::ZeroVector, false, 20)};
  TestTrue(TEXT("two crossing sources solve deterministically"), Solve(Crossing, Results, Summary));
  TestEqual(TEXT("ordinary agent sees both crossing sources"),
    Results.FindByPredicate([](const auto& R) { return R.AgentId == 202; })->TransitSourceCount, 2);
  const uint32 CrossingHash = Summary.StableHash;
  Algo::Reverse(Crossing);
  TestTrue(TEXT("reversed crossing sources solve"), Solve(Crossing, Results, Summary));
  TestEqual(TEXT("source reversal preserves hash"), Summary.StableHash, CrossingHash);

  TArray<FCrowdDemoElasticCrowdAgent> Overtake = {
    Agent(300, FVector2f(-120, 0), FVector2f(240, 0), true, 42),
    Agent(301, FVector2f(0, 0), FVector2f(60, 0), false, 42),
    Agent(302, FVector2f(128, 0), FVector2f(60, 0), false, 42)};
  TestTrue(TEXT("rear source overtaking queue solves"), Solve(Overtake, Results, Summary));
  TestTrue(TEXT("rear source keeps its base preferred"),
    Results.FindByPredicate([](const auto& R) { return R.AgentId == 300; })
      ->AdjustedPreferredVelocity.X >= 239.0f);

  TArray<FCrowdDemoElasticCrowdAgent> Centerline = {
    Agent(400, FVector2f(-100, 0), FVector2f(200, 0), true, 42),
    Agent(401, FVector2f(0, 0), FVector2f::ZeroVector, false, 20)};
  TestTrue(TEXT("centerline tie solves"), Solve(Centerline, Results, Summary));
  const FVector2f CenterlineYield = Results[1].TransitDeltaVelocity;
  TestTrue(TEXT("centerline tie chooses a stable lateral side"),
    FMath::Abs(CenterlineYield.Y) > FMath::Abs(CenterlineYield.X));
  TestTrue(TEXT("centerline repeated solve does not oscillate"), Solve(Centerline, Results, Summary));
  TestTrue(TEXT("centerline side remains stable"),
    Results[1].TransitDeltaVelocity.Equals(CenterlineYield, 0.01f));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoElasticShadowPbdDiagnosticTest,
  "CrowdDemo.SF4.Elastic.Shadow.PbdDiagnosticEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoElasticShadowPbdDiagnosticTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoHardSeparationPbdAgent> Agents;
  for (int32 Index = 0; Index < 3; ++Index)
  {
    auto& Agent = Agents.AddDefaulted_GetRef(); Agent.AgentId = 10 + Index;
    Agent.Location = FVector(Index * 70.0f, 0.0f, 0.0f); Agent.RadiusCm = 42.0f;
  }
  FCrowdDemoHardSeparationPbdSettings Settings;
  Settings.IterationCount = 3; Settings.MaxPairCorrectionPerIterationCm = 24.0f;
  TArray<FCrowdDemoHardSeparationPbdPair> PairsA, PairsB;
  TArray<FCrowdDemoHardSeparationPbdResult> ResultsA, ResultsB;
  TArray<FCrowdDemoHardSeparationPbdIterationDiagnostic> Diagnostics;
  FCrowdDemoHardSeparationPbdSummary SummaryA, SummaryB;
  FCrowdDemoHardSeparationPbdKernel::Solve(Agents, Settings, PairsA, ResultsA, SummaryA);
  FCrowdDemoHardSeparationPbdKernel::Solve(
    Agents, Settings, PairsB, ResultsB, SummaryB, &Diagnostics);
  TestEqual(TEXT("diagnostic path keeps pair count"), PairsB.Num(), PairsA.Num());
  TestEqual(TEXT("diagnostic path keeps result count"), ResultsB.Num(), ResultsA.Num());
  TestEqual(TEXT("diagnostic path emits exact iteration count"), Diagnostics.Num(), 3);
  for (int32 Index = 0; Index < ResultsA.Num(); ++Index)
  {
    TestEqual(TEXT("diagnostic path keeps AgentId"), ResultsB[Index].AgentId, ResultsA[Index].AgentId);
    TestTrue(TEXT("diagnostic path keeps final position"),
      ResultsB[Index].CorrectedLocation.Equals(ResultsA[Index].CorrectedLocation, 0.0f));
    TestTrue(TEXT("diagnostic path keeps total correction"),
      ResultsB[Index].Correction.Equals(ResultsA[Index].Correction, 0.0f));
  }
  TestEqual(TEXT("diagnostic path keeps corrected pair count"),
    SummaryB.CorrectedPairCount, SummaryA.CorrectedPairCount);
  TestEqual(TEXT("diagnostic path keeps max correction"),
    SummaryB.MaxPairCorrectionCm, SummaryA.MaxPairCorrectionCm);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoElasticStep40ReprojectHardPairReplayTest,
  "CrowdDemo.SF4.Elastic.Step40ReprojectHardPairReplay",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoElasticStep40ReprojectHardPairReplayTest::RunTest(const FString& Parameters)
{
  FCrowdDemoElasticShadowStepInput Input;
  Input.FixedStepIndex = 40;
  Input.FixedStepSeconds = 1.0f / 30.0f;
  Input.ElasticSettings.FixedStepSeconds = Input.FixedStepSeconds;
  Input.ElasticSettings.HardSafetyGapCm = 10.0f;
  Input.PbdSettings.IterationCount = 3;
  Input.PbdSettings.MaxPairCorrectionPerIterationCm = 24.0f;
  Input.Environment.FlowConfig = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  Input.Environment.bConstrainToFlowBounds = true;
  Input.Environment.bValidateTargetExclusion = true;
  Input.Environment.TargetLocation = FVector2f(600.0f, 3200.0f);
  Input.Environment.TargetExclusionRadiusCm = 260.0f;
  const FVector2f Starts[] = {
    {-22,-2374},{174,-2523},{74,-2350},{204,-2362},{330,-2391},
    {504,-2428},{700,-2341},{818,-2347},{985,-2335},{1148,-2252},
    {-19,-2250},{90,-2249},{219,-2250},{312,-2262},{412,-2251},
    {567,-2252},{702,-2250},{835,-2250},{1005,-2249},{1179,-2121}};
  const FVector2f Reproject[] = {
    {-26,-2363},{182,-2518},{76,-2350},{205,-2361},{339,-2387},
    {512,-2406},{700,-2341},{829,-2348},{1003,-2334},{1166,-2234},
    {-14,-2250},{103,-2249},{219,-2250},{312,-2262},{412,-2251},
    {580,-2252},{721,-2250},{856,-2250},{1032,-2249},{1197,-2103}};
  TArray<FCrowdDemoElasticShadowSafetyPolishAgent> Agents;
  for (int32 AgentId = 0; AgentId < UE_ARRAY_COUNT(Starts); ++AgentId)
  {
    auto& InputAgent = Input.Agents.AddDefaulted_GetRef();
    InputAgent.Agent.AgentId = AgentId;
    InputAgent.Agent.Position = Starts[AgentId];
    InputAgent.Agent.PhysicalRadiusCm = 42.0f;
    InputAgent.Agent.MaxSpeedCmps = 800.0f;
    auto& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = AgentId;
    Agent.Position = Reproject[AgentId];
  }
  FCrowdDemoElasticShadowSafetyPolishSummary Forward;
  TestTrue(TEXT("410502020 polish solves"),
    FCrowdDemoElasticShadowKernel::PolishReprojectHardPairs(Input, Agents, Forward));
  TestEqual(TEXT("410502020 quantized JSON has the primary residual plus 6-16 tolerance edge"),
    Forward.BeforeHardPairViolationCount, 2);
  const float QuantizedSixteenMargin =
    (Reproject[6] - Reproject[16]).Size() - 94.0f;
  TestTrue(TEXT("410502020 second quantized pair is only a sub-centimeter JSON artifact"),
    QuantizedSixteenMargin < -0.5f && QuantizedSixteenMargin > -1.0f);
  TestEqual(TEXT("410502020 resolves all residual hard pairs"),
    Forward.AfterHardPairViolationCount, 0);
  TestEqual(TEXT("410502020 requires one-sided realizable correction"),
    Forward.OneSidedCorrectionCount, 1);
  const auto* Agent8 = Forward.Agents.FindByPredicate(
    [](const auto& Agent) { return Agent.AgentId == 8; });
  const auto* Agent18 = Forward.Agents.FindByPredicate(
    [](const auto& Agent) { return Agent.AgentId == 18; });
  TestNotNull(TEXT("410502020 Agent 8 result exists"), Agent8);
  TestNotNull(TEXT("410502020 Agent 18 result exists"), Agent18);
  if (Agent8 && Agent18)
  {
    TestTrue(TEXT("410502020 transfers correction to obstacle-free Agent 8"),
      !Agent8->Position.Equals(Reproject[8], 0.01f));
    TestTrue(TEXT("410502020 leaves wall-constrained Agent 18 unchanged"),
      Agent18->Position.Equals(Reproject[18], 0.01f));
    TestTrue(TEXT("410502020 final pair meets 94cm clearance"),
      (Agent8->Position - Agent18->Position).Size() >= 93.5f);
  }
  for (const auto& Agent : Forward.Agents)
    TestFalse(TEXT("410502020 polished position remains obstacle clear"),
      FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        Input.Environment.FlowConfig, FVector(Agent.Position.X, Agent.Position.Y, 0.0f)));

  const uint32 Hash = Forward.StableHash;
  Algo::Reverse(Input.Agents);
  Algo::Reverse(Agents);
  FCrowdDemoElasticShadowSafetyPolishSummary Reversed;
  TestTrue(TEXT("410502020 reversed polish solves"),
    FCrowdDemoElasticShadowKernel::PolishReprojectHardPairs(Input, Agents, Reversed));
  TestEqual(TEXT("410502020 input reversal preserves polish hash"),
    Reversed.StableHash, Hash);
  return true;
}

namespace
{
  FCrowdDemoElasticShadowStepInput MakeElasticShadowTestInput()
  {
    FCrowdDemoElasticShadowStepInput Input;
    Input.FixedStepIndex = 7; Input.FixedStepSeconds = 1.0f / 30.0f;
    Input.ElasticSettings.FixedStepSeconds = Input.FixedStepSeconds;
    Input.PbdSettings.IterationCount = 3;
    Input.PbdSettings.MaxPairCorrectionPerIterationCm = 24.0f;
    Input.Environment.bValidateFlowAndObstacles = false;
    Input.Environment.bConstrainToFlowBounds = false;
    Input.Environment.bValidateTargetExclusion = false;
    auto Add = [&](const int32 Id, const FVector2f Position,
      const FVector2f Preferred, const bool bSource)
    {
      auto& Item = Input.Agents.AddDefaulted_GetRef();
      Item.Agent.AgentId = Id; Item.Agent.Position = Position;
      Item.Agent.BasePreferredVelocity = Preferred;
      Item.Agent.PhysicalRadiusCm = 20.0f; Item.Agent.TransitSourceRadiusCm = 20.0f;
      Item.Agent.MaxSpeedCmps = 260.0f; Item.Agent.bTransitSource = bSource;
      Item.Agent.TransitDirection = Preferred.GetSafeNormal();
      Item.FlowDirection = Preferred.GetSafeNormal();
      Item.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
      Item.SteeringState = bSource ? ECrowdDemoPursuitSteeringState::Commit
        : ECrowdDemoPursuitSteeringState::StableOccupied;
      Item.HoldingLocation = Position; Item.AssignedPosition = Position;
      Item.bHasAssignment = true;
    };
    Add(1, FVector2f(-200, 0), FVector2f(120, 0), true);
    Add(2, FVector2f(0, 100), FVector2f::ZeroVector, false);
    Add(3, FVector2f(0, -100), FVector2f::ZeroVector, false);
    return Input;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoElasticShadowTwinStepTest,
  "CrowdDemo.SF4.Elastic.Shadow.TwinStep",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoElasticShadowTwinStepTest::RunTest(const FString& Parameters)
{
  FCrowdDemoElasticShadowStepInput Input = MakeElasticShadowTestInput();
  const TArray<FCrowdDemoElasticShadowAgentInput> Original = Input.Agents;
  FCrowdDemoElasticShadowTwinResult Forward;
  TestTrue(TEXT("twin step solves"), FCrowdDemoElasticShadowKernel::RunTwinStep(Input, Forward));
  TestTrue(TEXT("twin result valid"), Forward.bValid);
  TestEqual(TEXT("both branches expose eight stages"),
    Forward.Elastic.Stages.Num(), static_cast<int32>(ECrowdDemoElasticShadowStage::Count));
  const int32 Preferred = static_cast<int32>(ECrowdDemoElasticShadowStage::Preferred);
  const auto* SourceResult = Forward.Elastic.ElasticResults.FindByPredicate(
    [](const auto& Result) { return Result.AgentId == 1; });
  TestNotNull(TEXT("elastic source result exists"), SourceResult);
  if (SourceResult)
    TestTrue(TEXT("elastic never rewrites source preferred"),
      SourceResult->AdjustedPreferredVelocity.Equals(FVector2f(120, 0), 0.0f));
  for (int32 Index = 0; Index < Original.Num(); ++Index)
  {
    TestTrue(TEXT("twin step does not mutate input position"),
      Input.Agents[Index].Agent.Position.Equals(Original[Index].Agent.Position, 0.0f));
    TestTrue(TEXT("baseline and elastic share the exact start position"),
      Forward.Baseline.Stages[Preferred][Index].Position.Equals(
        Forward.Elastic.Stages[Preferred][Index].Position, 0.0f));
  }
  const uint32 ForwardHash = Forward.StableHash;
  Algo::Reverse(Input.Agents);
  FCrowdDemoElasticShadowTwinResult Reversed;
  TestTrue(TEXT("reversed twin step solves"),
    FCrowdDemoElasticShadowKernel::RunTwinStep(Input, Reversed));
  TestEqual(TEXT("input reversal preserves twin hash"), Reversed.StableHash, ForwardHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoElasticShadowFirstFailureTest,
  "CrowdDemo.SF4.Elastic.Shadow.FirstFailure",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoElasticShadowFirstFailureTest::RunTest(const FString& Parameters)
{
  FCrowdDemoElasticShadowStepInput Input = MakeElasticShadowTestInput();
  FCrowdDemoElasticShadowTwinResult Twin;
  TestTrue(TEXT("failure twin solves"), FCrowdDemoElasticShadowKernel::RunTwinStep(Input, Twin));
  FCrowdDemoElasticShadowFailureFixture Fixture;
  TArray<int32> ZeroProgressIds = {1};
  TestTrue(TEXT("first failure fixture captured"),
    FCrowdDemoElasticShadowKernel::BuildFirstFailureFixture(
      Input, Twin, ZeroProgressIds, 15, Fixture));
  TestEqual(TEXT("zero progress fails at reproject stage"),
    static_cast<int32>(Fixture.Stage),
    static_cast<int32>(ECrowdDemoElasticShadowStage::Reproject));
  TestEqual(TEXT("failure kind is source zero progress"),
    static_cast<int32>(Fixture.FailureKind),
    static_cast<int32>(ECrowdDemoElasticShadowFailureKind::SourceZeroProgress));
  TestEqual(TEXT("source zero progress attribution is explicit"),
    static_cast<int32>(Fixture.Attribution),
    static_cast<int32>(ECrowdDemoElasticShadowAttribution::ElasticWorsened));
  TestTrue(TEXT("fixture closure is bounded"),
    Fixture.ClosureAgentCount >= 2 && Fixture.ClosureAgentCount <= 20);
  const uint32 Hash = Fixture.StableHash;
  Algo::Reverse(Input.Agents);
  TestTrue(TEXT("reversed failure twin solves"), FCrowdDemoElasticShadowKernel::RunTwinStep(Input, Twin));
  FCrowdDemoElasticShadowFailureFixture ReversedFixture;
  TestTrue(TEXT("reversed fixture captured"),
    FCrowdDemoElasticShadowKernel::BuildFirstFailureFixture(
      Input, Twin, ZeroProgressIds, 15, ReversedFixture));
  TestEqual(TEXT("input reversal preserves fixture hash"), ReversedFixture.StableHash, Hash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoElasticShadowParallelTest,
  "CrowdDemo.SF4.Elastic.Shadow.ParallelRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoElasticShadowParallelTest::RunTest(const FString& Parameters)
{
  FCrowdDemoElasticShadowStepInput Input = MakeElasticShadowTestInput();
  Input.Agents[0].AssignedPosition = FVector2f(300, 0);
  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("parallel fixture flow builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(), Field));
  FCrowdDemoPursuitPositioningSettings Positioning;
  FCrowdDemoElasticShadowParallelState State;
  TestTrue(TEXT("parallel rollout initializes on source"),
    FCrowdDemoElasticShadowKernel::InitializeParallelRollout(
      Input, Field, Positioning, State));
  const FVector2f FormalStart = Input.Agents[0].Agent.Position;
  FCrowdDemoElasticShadowTwinResult Step;
  for (int32 Index = 0; Index < 180; ++Index)
    TestTrue(TEXT("parallel rollout step solves"),
      FCrowdDemoElasticShadowKernel::AdvanceParallelRollout(Input, State, Step));
  TestTrue(TEXT("parallel rollout completes exactly 180 steps"), State.bCompleted);
  TestEqual(TEXT("parallel rollout step count"), State.StepIndex, 180);
  TestTrue(TEXT("parallel rollout never mutates formal input"),
    Input.Agents[0].Agent.Position.Equals(FormalStart, 0.0f));
  TestTrue(TEXT("parallel rollout owns independent branch state"),
    !State.BaselineAgents[0].Agent.Position.Equals(FormalStart, 0.0f));
  TestTrue(TEXT("parallel summary has a stable hash"), State.Summary.StableHash != 2166136261u);
  return true;
}

#endif
