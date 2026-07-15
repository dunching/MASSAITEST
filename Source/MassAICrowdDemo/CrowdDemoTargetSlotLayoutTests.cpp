#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoTargetSlotLayoutKernel.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"

namespace CrowdDemoTargetSlotLayoutTests
{
FCrowdDemoSharedFlowFieldConfig MakeOpenConfig()
{
  FCrowdDemoSharedFlowFieldConfig Config;
  Config.Revision = 71;
  Config.BoundsMin = FVector(-2000.0f, -2000.0f, 0.0f);
  Config.BoundsMax = FVector(2000.0f, 2000.0f, 0.0f);
  Config.CellSizeCm = 100.0f;
  Config.AgentInflateCm = 52.0f;
  Config.ConnectivityContractVersion = 2;
  Config.GoalLocation = FVector::ZeroVector;
  return Config;
}

FCrowdDemoTargetSlotLayoutInput MakeInput(FCrowdDemoSharedFlowField& Field,
  FCrowdDemoSharedFlowFieldConfig& Config)
{
  Config = MakeOpenConfig();
  FCrowdDemoSharedFlowFieldKernel::Build(Config, Field);
  FCrowdDemoTargetSlotLayoutInput Input;
  Input.Target.TargetId = 7;
  Input.Target.TargetRevision = 3;
  Input.Target.Location = FVector2f::ZeroVector;
  Input.Target.PhysicalRadiusCm = 100.0f;
  Input.FlowConfig = Config;
  Input.FlowField = &Field;
  Input.TransitionRingRadiusCm = 600.0f;
  FCrowdDemoTargetSlotBandRule Functional;
  Functional.BandId = 10;
  Functional.bFunctional = 1;
  Functional.Capacity = 4;
  Functional.PreferredSurfaceDistanceCm = 160.0f;
  Functional.MinimumCenterDistanceCm = 250.0f;
  Functional.MaximumCenterDistanceCm = 270.0f;
  Functional.RequiredCapabilityMask = 1u;
  Functional.StablePriorityBase = 10;
  FCrowdDemoTargetSlotBandRule Fill;
  Fill.BandId = 20;
  Fill.Capacity = 4;
  Fill.PreferredSurfaceDistanceCm = 280.0f;
  Fill.MinimumCenterDistanceCm = 370.0f;
  Fill.MaximumCenterDistanceCm = 390.0f;
  Fill.StartAngleDegrees = 45.0f;
  Fill.StablePriorityBase = 20;
  Input.Settings.SourceRevision = 5;
  Input.Settings.Bands = {Functional, Fill};
  return Input;
}

TArray<int32> SlotIds(const FCrowdDemoTargetSlotLayout& Layout)
{
  TArray<int32> Result;
  for (const auto& Slot : Layout.Slots)
    Result.Add(Slot.SlotId);
  return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetSlotLayoutDeterminismTest,
  "CrowdDemo.SoftPressure.TargetSlotLayout.DeterminismAndRevision",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetSlotLayoutDeterminismTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetSlotLayoutTests;
  FCrowdDemoSharedFlowField Field;
  FCrowdDemoSharedFlowFieldConfig Config;
  FCrowdDemoTargetSlotLayoutInput Input = MakeInput(Field, Config);
  FCrowdDemoTargetSlotLayout A;
  FCrowdDemoTargetSlotLayoutSummary ASummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, A, ASummary);
  TestTrue(TEXT("layout valid"), A.bValid);
  TestEqual(TEXT("functional count"), ASummary.AcceptedFunctionalCount, 4);
  TestEqual(TEXT("fill count"), ASummary.AcceptedFillCount, 4);

  Algo::Reverse(Input.Settings.Bands);
  FCrowdDemoTargetSlotLayout Reverse;
  FCrowdDemoTargetSlotLayoutSummary ReverseSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, Reverse, ReverseSummary);
  TestTrue(TEXT("reversed ids"), SlotIds(Reverse) == SlotIds(A));
  TestEqual(TEXT("reversed topology"), Reverse.TopologyHash, A.TopologyHash);
  TestEqual(TEXT("reversed world"), Reverse.WorldValidationHash, A.WorldValidationHash);
  TestEqual(TEXT("reversed full input"), Reverse.FullInputHash, A.FullInputHash);

  Input.Target.Location = FVector2f(50.0f, 25.0f);
  Input.Target.YawDegrees = 90.0f;
  FCrowdDemoTargetSlotLayout Moved;
  FCrowdDemoTargetSlotLayoutSummary MovedSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, &A, Moved, MovedSummary);
  TestTrue(TEXT("move keeps slot ids"), SlotIds(Moved) == SlotIds(A));
  TestEqual(TEXT("move keeps layout revision"), Moved.SlotLayoutRevision, A.SlotLayoutRevision);
  TestNotEqual(TEXT("move changes world hash"), Moved.WorldValidationHash, A.WorldValidationHash);

  Input.Target.TargetRevision = 4;
  FCrowdDemoTargetSlotLayout Revised;
  FCrowdDemoTargetSlotLayoutSummary RevisedSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, &Moved, Revised, RevisedSummary);
  TestEqual(TEXT("target revision advances layout"), Revised.SlotLayoutRevision,
    Moved.SlotLayoutRevision + 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetSlotLayoutValidationTest,
  "CrowdDemo.SoftPressure.TargetSlotLayout.WorldValidationAndCapacity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetSlotLayoutValidationTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetSlotLayoutTests;
  FCrowdDemoSharedFlowField Field;
  FCrowdDemoSharedFlowFieldConfig Config;
  FCrowdDemoTargetSlotLayoutInput Input = MakeInput(Field, Config);
  Input.Settings.Bands.SetNum(1);
  Input.Settings.Bands[0].Capacity = 20;
  FCrowdDemoTargetSlotLayout Layout;
  FCrowdDemoTargetSlotLayoutSummary Summary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, Layout, Summary);
  TestTrue(TEXT("dense band remains a valid layout"), Layout.bValid);
  TestTrue(TEXT("pair spacing rejects without compression"),
    Summary.RejectedPairSpacingCount > 0);
  TestEqual(TEXT("candidate count remains declared capacity"),
    Summary.GeneratedCandidateCount, 20);
  TestTrue(TEXT("accepted does not expand"), Layout.Slots.Num() < 20);

  Input = MakeInput(Field, Config);
  Input.Target.PhysicalRadiusCm = 240.0f;
  FCrowdDemoTargetSlotLayout RadiusRejected;
  FCrowdDemoTargetSlotLayoutSummary RadiusSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, RadiusRejected, RadiusSummary);
  TestTrue(TEXT("radius changes declared band acceptance"),
    RadiusSummary.GeneratedCandidateCount < 8 || RadiusRejected.Slots.Num() < 8);

  Input = MakeInput(Field, Config);
  Config.BoundsMax = FVector(220.0f, 220.0f, 0.0f);
  Input.FlowConfig = Config;
  FCrowdDemoSharedFlowField TightField;
  FCrowdDemoSharedFlowFieldKernel::Build(Config, TightField);
  Input.FlowField = &TightField;
  FCrowdDemoTargetSlotLayout BoundsRejected;
  FCrowdDemoTargetSlotLayoutSummary BoundsSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, BoundsRejected, BoundsSummary);
  TestTrue(TEXT("bounds rejection recorded"), BoundsSummary.RejectedBoundsCount > 0);

  Input = MakeInput(Field, Config);
  Input.Settings.Bands.SetNum(1);
  Input.Settings.Bands[0].Capacity = 1;
  Input.Settings.Bands[0].PreferredSurfaceDistanceCm = 50.0f;
  Input.Settings.Bands[0].MinimumCenterDistanceCm = 150.0f;
  Input.Settings.Bands[0].MaximumCenterDistanceCm = 150.0f;
  FCrowdDemoTargetSlotLayout TargetRejected;
  FCrowdDemoTargetSlotLayoutSummary TargetSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, TargetRejected, TargetSummary);
  TestEqual(TEXT("target clearance rejection recorded"),
    TargetSummary.RejectedTargetClearanceCount, 1);

  Input = MakeInput(Field, Config);
  Input.Settings.Bands.SetNum(1);
  Input.Settings.Bands[0].Capacity = 1;
  FCrowdDemoSharedFlowObstacleSpec EndpointObstacle;
  EndpointObstacle.ObstacleId = 901;
  EndpointObstacle.Center = FVector(260.0f, 0.0f, 0.0f);
  EndpointObstacle.Extent = FVector(20.0f, 20.0f, 100.0f);
  Config.ObstacleSpecs = {EndpointObstacle};
  FCrowdDemoSharedFlowField ObstacleField;
  FCrowdDemoSharedFlowFieldKernel::Build(Config, ObstacleField);
  Input.FlowConfig = Config;
  Input.FlowField = &ObstacleField;
  FCrowdDemoTargetSlotLayout ObstacleRejected;
  FCrowdDemoTargetSlotLayoutSummary ObstacleSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, ObstacleRejected, ObstacleSummary);
  TestEqual(TEXT("obstacle rejection recorded"), ObstacleSummary.RejectedObstacleCount, 1);

  Input = MakeInput(Field, Config);
  Input.Settings.Bands.SetNum(1);
  Input.Settings.Bands[0].Capacity = 1;
  FCrowdDemoSharedFlowObstacleSpec IngressObstacle;
  IngressObstacle.ObstacleId = 902;
  IngressObstacle.Center = FVector(430.0f, 0.0f, 0.0f);
  IngressObstacle.Extent = FVector(20.0f, 20.0f, 100.0f);
  Config.ObstacleSpecs = {IngressObstacle};
  FCrowdDemoSharedFlowField IngressField;
  FCrowdDemoSharedFlowFieldKernel::Build(Config, IngressField);
  Input.FlowConfig = Config;
  Input.FlowField = &IngressField;
  FCrowdDemoTargetSlotLayout IngressRejected;
  FCrowdDemoTargetSlotLayoutSummary IngressSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input, nullptr, IngressRejected, IngressSummary);
  TestEqual(TEXT("ingress segment rejection recorded"),
    IngressSummary.RejectedIngressSegmentCount, 1);

  Input = MakeInput(Field, Config);
  Input.Settings.Bands.SetNum(1);
  Input.Settings.Bands[0].Capacity = 1;
  for (int32& Cost : Field.NavigationIntegrationCost)
    Cost = MAX_int32;
  Field.Unreachable.Init(true, Field.Unreachable.Num());
  Input.FlowField = &Field;
  FCrowdDemoTargetSlotLayout UnreachableRejected;
  FCrowdDemoTargetSlotLayoutSummary UnreachableSummary;
  FCrowdDemoTargetSlotLayoutKernel::Build(
    Input, nullptr, UnreachableRejected, UnreachableSummary);
  TestEqual(TEXT("unreachable rejection recorded"),
    UnreachableSummary.RejectedUnreachableCount, 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetSlotCapabilityCompatibilityTest,
  "CrowdDemo.SoftPressure.TargetSlotLayout.CapabilityCompatibility",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetSlotCapabilityCompatibilityTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTargetFact Target;
  Target.TargetId = 7;
  Target.TargetRevision = 3;
  Target.PhysicalRadiusCm = 100.0f;
  FCrowdDemoTargetApproachSettings Settings;
  Settings.bEnabled = true;
  Settings.TransitionRingRadiusCm = 600.0f;
  FCrowdDemoTargetSlotSpec Functional;
  Functional.SlotId = 10;
  Functional.Kind = ECrowdDemoTargetSlotKind::Functional;
  Functional.CenterDistanceCm = 260.0f;
  Functional.TargetRelativeOffset = FVector2f(260.0f, 0.0f);
  Functional.RequiredCapabilityMask = 1u;
  FCrowdDemoTargetSlotSpec Fill;
  Fill.SlotId = 20;
  Fill.Kind = ECrowdDemoTargetSlotKind::Fill;
  Fill.CenterDistanceCm = 380.0f;
  Fill.TargetRelativeOffset = FVector2f(-380.0f, 0.0f);

  FCrowdDemoTargetApproachAgent FunctionalAgent;
  FunctionalAgent.AgentId = 1;
  FunctionalAgent.Location = FVector2f(500.0f, 0.0f);
  FunctionalAgent.MaxSpeedCmps = 800.0f;
  FunctionalAgent.CapabilityMask = 1u;
  FunctionalAgent.MinimumFunctionalDistanceCm = 200.0f;
  FunctionalAgent.MaximumFunctionalDistanceCm = 300.0f;
  FunctionalAgent.ExistingTargetRevision = 3;
  FunctionalAgent.ExistingSlotLayoutRevision = 9;
  FCrowdDemoTargetApproachAgent NoCapability = FunctionalAgent;
  NoCapability.AgentId = 2;
  NoCapability.Location = FVector2f(-500.0f, 0.0f);
  NoCapability.CapabilityMask = 0u;

  TArray<FCrowdDemoTargetApproachResult> Results;
  FCrowdDemoTargetApproachSummary Summary;
  FCrowdDemoTargetApproachKernel::Solve(Target, Settings, {Functional, Fill},
    {FunctionalAgent, NoCapability}, 4, Results, Summary, 9);
  const auto* A = Results.FindByPredicate([](const auto& R) { return R.AgentId == 1; });
  const auto* B = Results.FindByPredicate([](const auto& R) { return R.AgentId == 2; });
  TestEqual(TEXT("capability agent receives functional"), A ? A->AssignedSlotId : INDEX_NONE, 10);
  TestEqual(TEXT("missing capability falls through to fill"), B ? B->AssignedSlotId : INDEX_NONE, 20);

  FunctionalAgent.CapabilityMask = 0u;
  FunctionalAgent.ExistingState = ECrowdDemoTargetApproachState::SlotOccupied;
  FunctionalAgent.ExistingSlotId = 10;
  Results.Reset();
  FCrowdDemoTargetApproachKernel::Solve(Target, Settings, {Functional, Fill},
    {FunctionalAgent}, 5, Results, Summary, 9);
  TestTrue(TEXT("invalid owner released in same solve"), Results.Num() == 1
    && Results[0].AssignedSlotId != 10);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetApproachAtomicCommitTest,
  "CrowdDemo.SoftPressure.TargetSlotLayout.AtomicCommitValidation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetApproachAtomicCommitTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTargetSlotSpec Functional;
  Functional.SlotId = 10;
  Functional.Kind = ECrowdDemoTargetSlotKind::Functional;
  Functional.CenterDistanceCm = 260.0f;
  Functional.RequiredCapabilityMask = 1u;
  FCrowdDemoTargetSlotSpec Fill;
  Fill.SlotId = 20;
  Fill.Kind = ECrowdDemoTargetSlotKind::Fill;
  Fill.CenterDistanceCm = 380.0f;
  FCrowdDemoTargetApproachCommitAgent A;
  A.AgentId = 1;
  A.CapabilityMask = 1u;
  A.MinimumFunctionalDistanceCm = 200.0f;
  A.MaximumFunctionalDistanceCm = 300.0f;
  FCrowdDemoTargetApproachCommitAgent B = A;
  B.AgentId = 2;
  FCrowdDemoTargetApproachResult DA;
  DA.AgentId = 1;
  DA.State = ECrowdDemoTargetApproachState::SlotIngress;
  DA.AssignedSlotId = 10;
  DA.SlotLayoutRevision = 4;
  FCrowdDemoTargetApproachResult DB = DA;
  DB.AgentId = 2;
  DB.AssignedSlotId = 20;

  FCrowdDemoTargetApproachCommitValidation Forward;
  FCrowdDemoTargetApproachKernel::ValidateAtomicCommit(
    4, {Functional, Fill}, {A, B}, {DA, DB}, Forward);
  TestTrue(TEXT("complete commit validates"), Forward.bValid);

  FCrowdDemoTargetApproachCommitValidation Reverse;
  FCrowdDemoTargetApproachKernel::ValidateAtomicCommit(
    4, {Fill, Functional}, {B, A}, {DB, DA}, Reverse);
  TestTrue(TEXT("reverse commit validates"), Reverse.bValid);
  TestEqual(TEXT("reverse commit hash"), Reverse.CommitHash, Forward.CommitHash);

  DB.AssignedSlotId = 10;
  FCrowdDemoTargetApproachCommitValidation Duplicate;
  FCrowdDemoTargetApproachKernel::ValidateAtomicCommit(
    4, {Functional, Fill}, {A, B}, {DA, DB}, Duplicate);
  TestFalse(TEXT("duplicate owner rejects whole commit"), Duplicate.bValid);
  TestTrue(TEXT("duplicate owner counted"), Duplicate.OwnerConflictCount > 0);

  DB.AssignedSlotId = 20;
  DB.SlotLayoutRevision = 5;
  FCrowdDemoTargetApproachCommitValidation Mismatch;
  FCrowdDemoTargetApproachKernel::ValidateAtomicCommit(
    4, {Functional, Fill}, {A, B}, {DA, DB}, Mismatch);
  TestFalse(TEXT("revision mismatch rejects whole commit"), Mismatch.bValid);
  TestEqual(TEXT("revision mismatch counted"), Mismatch.RevisionMismatchCount, 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetSlotLayoutRollbackStateTest,
  "CrowdDemo.SoftPressure.TargetSlotLayout.RollbackRestoresAtomicState",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetSlotLayoutRollbackStateTest::RunTest(const FString& Parameters)
{
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  FCrowdDemoSoftPressureRollbackSnapshot Snapshot;
  Snapshot.TargetSlotLayout.bValid = true;
  Snapshot.TargetSlotLayout.TargetId = 7;
  Snapshot.TargetSlotLayout.TargetRevision = 3;
  Snapshot.TargetSlotLayout.SlotLayoutRevision = 11;
  Snapshot.TargetSlotLayout.TopologyHash = 101u;
  Snapshot.TargetSlotLayout.WorldValidationHash = 202u;
  Snapshot.TargetSlotLayout.FullInputHash = 303u;
  Snapshot.TargetSlotLayoutSummary.bValid = true;
  Snapshot.TargetSlotLayoutSummary.AcceptedFunctionalCount = 4;
  FCrowdDemoTargetApproachResult Decision;
  Decision.AgentId = 8;
  Decision.State = ECrowdDemoTargetApproachState::SlotOccupied;
  Decision.AssignedSlotId = 44;
  Decision.SlotLayoutRevision = 11;
  Snapshot.TargetApproachDecisions = {Decision};
  Snapshot.TargetApproachGuidance = {Decision};
  Snapshot.TargetApproachSummary.bValid = true;
  Snapshot.TargetApproachSummary.ScheduleHash = 404u;
  Snapshot.TargetApproachSummary.CommitHash = 505u;
  Snapshot.TargetApproachCommitHash = 505u;

  Pipeline->RestoreSoftPressureRuntime(Snapshot);
  TestEqual(TEXT("rollback layout revision"),
    Pipeline->GetPreparedTargetSlotLayout().SlotLayoutRevision, 11);
  TestEqual(TEXT("rollback topology hash"),
    Pipeline->GetPreparedTargetSlotLayout().TopologyHash, 101u);
  TestEqual(TEXT("rollback decision owner"),
    Pipeline->GetPreparedTargetApproachResults()[0].AssignedSlotId, 44);
  TestEqual(TEXT("rollback guidance owner"),
    Pipeline->GetPreparedTargetApproachGuidance()[0].AssignedSlotId, 44);
  TestEqual(TEXT("rollback schedule hash"),
    Pipeline->GetTargetApproachSummary().ScheduleHash, 404u);
  TestEqual(TEXT("rollback commit hash"), Pipeline->GetTargetApproachCommitHash(), 505u);
  return true;
}

#endif
