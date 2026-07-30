#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdGuidanceComposeKernel.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassCrowdMovementTrait.h"
#include "MassCrowdFacingWork.h"
#include "MassCrowdFacingFinalizeWork.h"
#include "MassCrowdGuidanceWork.h"
#include "MassCrowdLocalPredictiveWork.h"
#include "MassCrowdMovementFinalizeWork.h"
#include "MassCrowdMovementPipelineWork.h"
#include "MassCrowdMovementPredictWork.h"
#include "MassCrowdParticleWork.h"
#include "MassCrowdParticlePipelineWork.h"
#include "MassCrowdRuntimeBridge.h"
#include "MassCrowdSharedFlowWork.h"
#include "MassEntityManager.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdEnvironmentSnapshot MakeEnvironment()
  {
    FCrowdEnvironmentSnapshot Environment;
    Environment.Revision = 5;
    Environment.SimulationBounds = FBox(
      FVector(-1000.0f), FVector(1000.0f));
    Environment.StableHash = 0x11223344u;
    Environment.bValid = true;
    return Environment;
  }

  FCrowdTargetInput MakeTarget()
  {
    FCrowdTargetInput Target;
    Target.TargetId = 77;
    Target.Revision = 9;
    Target.Position = FVector(400.0f, 500.0f, 60.0f);
    Target.Velocity = FVector(20.0f, 0.0f, 0.0f);
    Target.PhysicalRadiusCm = 100.0f;
    Target.bValid = true;
    return Target;
  }

  FCrowdSimulationProfile MakeProfile(const uint32 Key)
  {
    FCrowdSimulationProfile Profile;
    Profile.StableProfileKey = Key;
    Profile.FixedStepSeconds = 1.0f / 30.0f;
    Profile.HardSafetyGapCm = 10.0f;
    Profile.SoftMarginCm = 17.0f;
    Profile.SolverIterationCount = 8;
    return Profile;
  }

  FCrowdMassGatherRecord MakeRecord(
    const int32 AgentId, const uint32 InProfileKey)
  {
    FCrowdMassGatherRecord Record;
    Record.Identity.AgentId = AgentId;
    Record.Identity.SetStableEntityRef({
      1u, static_cast<uint64>(AgentId) + 100u,
      static_cast<uint32>(AgentId + 10)});
    Record.AgentFacts.StableEntityRef =
      Record.Identity.GetStableEntityRef();
    Record.AgentFacts.CapabilitySet.Add(ECrowdCapability::Move);
    Record.AgentFacts.DerivedBehaviorLabel =
      static_cast<uint32>(ECrowdActiveBehavior::Idle);
    Record.AgentFacts.MovementProfileKey = InProfileKey;
    Record.State.Position = FVector(
      static_cast<float>(AgentId * 100), 10.0f, 60.0f);
    Record.State.Velocity = FVector(30.0f, 0.0f, 0.0f);
    Record.State.YawDegrees = 5.0f;
    Record.State.PlanRevision = 3;
    Record.State.bInitialized = true;
    Record.Properties.PhysicalRadiusCm = 42.0f;
    Record.Properties.MaximumSpeedCmps = 300.0f;
    Record.Properties.CapabilityProfileKey = InProfileKey;
    Record.Guidance.SharedFlow = FCrowdGuidanceComposeKernel::BuildCandidate(
      AgentId, ECrowdGuidanceProvider::SharedFlow, 3,
      FVector(300.0f, 0.0f, 0.0f), Record.State.Position, 0.0f, true);
    Record.Guidance.TargetRegion = FCrowdGuidanceComposeKernel::BuildCandidate(
      AgentId, ECrowdGuidanceProvider::TargetRegion, 3,
      FVector(0.0f, 200.0f, 0.0f), FVector(400.0f, 500.0f, 60.0f),
      90.0f, AgentId == 1);
    return Record;
  }

  FCrowdMovementOutput MakeMovement(const FCrowdAgentInput& Agent)
  {
    FCrowdMovementOutput Movement;
    Movement.AgentId = Agent.AgentId;
    Movement.LifecycleSerial = Agent.LifecycleSerial;
    Movement.Position = Agent.Position + FVector(10.0f, 20.0f, 0.0f);
    Movement.Velocity = FVector(100.0f, 200.0f, 0.0f);
    Movement.YawDegrees = 63.43f;
    Movement.StableHash = 0x90000000u + static_cast<uint32>(Agent.AgentId);
    Movement.bValid = true;
    return Movement;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRuntimeGatherMergeCommitTest,
  "MassCrowd.Runtime.GatherMergeCommit",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRuntimeGatherMergeCommitTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdSimulationProfile> Profiles = {MakeProfile(7), MakeProfile(0)};
  TArray<FCrowdMassGatherRecord> Records = {
    MakeRecord(3, 7), MakeRecord(1, 0), MakeRecord(2, 7)};
  TArray<FCrowdMassBoundaryAgentRecord> BoundaryRecords;
  for (const FCrowdMassGatherRecord& Record : Records)
    BoundaryRecords.Add({
      Record.Identity, Record.AgentFacts, Record.State, Record.Properties});
  FCrowdMassBoundarySnapshot BoundaryForward;
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    11, 3, BoundaryRecords, BoundaryForward);
  TestTrue(TEXT("boundary snapshot valid"), BoundaryForward.bValid);
  TestEqual(TEXT("boundary agents stable sorted"),
    BoundaryForward.Agents[0].Identity.AgentId, 1);
  Algo::Reverse(BoundaryRecords);
  FCrowdMassBoundarySnapshot BoundaryReverse;
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    11, 3, BoundaryRecords, BoundaryReverse);
  TestEqual(TEXT("boundary snapshot order independent"),
    BoundaryReverse.StableHash, BoundaryForward.StableHash);
  TArray<FCrowdGuidanceCandidate> FlowCandidates;
  TArray<FCrowdGuidanceCandidate> TargetCandidates;
  TArray<FCrowdGuidanceCandidate> BusinessCandidates;
  for (const FCrowdMassGatherRecord& Record : Records)
  {
    FlowCandidates.Add(Record.Guidance.SharedFlow);
    if (Record.Guidance.TargetRegion.bValid)
      TargetCandidates.Add(Record.Guidance.TargetRegion);
  }
  BusinessCandidates.Add(FCrowdGuidanceComposeKernel::BuildCandidate(
    3, ECrowdGuidanceProvider::BusinessOverride, 3,
    FVector::ZeroVector, FVector(300.0f, 10.0f, 60.0f), 5.0f, true));
  TArray<FCrowdMassGatherRecord> OverlayForward;
  TestTrue(TEXT("boundary guidance overlay builds complete records"),
    FCrowdMassRuntimeBridge::BuildGuidanceRecords(
      BoundaryForward, FlowCandidates, TargetCandidates,
      BusinessCandidates, OverlayForward));
  TestEqual(TEXT("boundary guidance records stay agent sorted"),
    OverlayForward[0].Identity.AgentId, 1);
  TestTrue(TEXT("optional target candidate overlays matching agent"),
    OverlayForward[0].Guidance.TargetRegion.bValid);
  TestTrue(TEXT("optional business candidate overlays matching agent"),
    OverlayForward[2].Guidance.BusinessOverride.bValid);
  Algo::Reverse(FlowCandidates);
  Algo::Reverse(TargetCandidates);
  TArray<FCrowdMassGatherRecord> OverlayReverse;
  TestTrue(TEXT("reversed boundary guidance overlay builds"),
    FCrowdMassRuntimeBridge::BuildGuidanceRecords(
      BoundaryForward, FlowCandidates, TargetCandidates,
      BusinessCandidates, OverlayReverse));
  FCrowdMassGuidanceWorkInput OverlayForwardInput;
  OverlayForwardInput.FixedStepIndex = 11;
  OverlayForwardInput.PlanRevision = 3;
  OverlayForwardInput.Records = OverlayForward;
  FCrowdMassGuidanceWorkInput OverlayReverseInput = OverlayForwardInput;
  OverlayReverseInput.Records = OverlayReverse;
  TestEqual(TEXT("boundary overlay order preserves composed hash"),
    FCrowdMassGuidanceWork::Compose(OverlayReverseInput).StableHash,
    FCrowdMassGuidanceWork::Compose(OverlayForwardInput).StableHash);
  const FCrowdGuidanceCandidate DuplicateTarget = TargetCandidates[0];
  TargetCandidates.Add(DuplicateTarget);
  TArray<FCrowdMassGatherRecord> InvalidOverlay;
  TestFalse(TEXT("boundary guidance overlay rejects duplicate provider agent"),
    FCrowdMassRuntimeBridge::BuildGuidanceRecords(
      BoundaryForward, FlowCandidates, TargetCandidates,
      BusinessCandidates, InvalidOverlay));
  TargetCandidates.Pop();
  FlowCandidates.Pop();
  TestFalse(TEXT("boundary guidance overlay requires shared flow for every agent"),
    FCrowdMassRuntimeBridge::BuildGuidanceRecords(
      BoundaryForward, FlowCandidates, TargetCandidates,
      BusinessCandidates, InvalidOverlay));
  const FCrowdMassBoundaryAgentRecord DuplicateBoundaryRecord =
    BoundaryRecords[0];
  BoundaryRecords.Add(DuplicateBoundaryRecord);
  FCrowdMassBoundarySnapshot BoundaryDuplicate;
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    11, 3, BoundaryRecords, BoundaryDuplicate);
  TestFalse(TEXT("boundary snapshot rejects duplicate agent"),
    BoundaryDuplicate.bValid);
  TArray<FCrowdMassWorkBatch> Forward;
  FCrowdMassRuntimeBridge::BuildWorkBatches(
    11, 3, Profiles, MakeEnvironment(), MakeTarget(), Records, Forward);
  TestEqual(TEXT("two capability batches gathered"), Forward.Num(), 2);
  TestTrue(TEXT("zero profile key is a valid base profile"),
    Forward[0].bValid && Forward[0].CapabilityProfileKey == 0);
  TestEqual(TEXT("base profile has one agent"),
    Forward[0].WorkInput.Agents.Num(), 1);
  TestEqual(TEXT("second profile has two agents"),
    Forward[1].WorkInput.Agents.Num(), 2);
  TestEqual(TEXT("agents are stable inside profile"),
    Forward[1].WorkInput.Agents[0].AgentId, 2);

  Algo::Reverse(Profiles);
  Algo::Reverse(Records);
  TArray<FCrowdMassWorkBatch> Reversed;
  FCrowdMassRuntimeBridge::BuildWorkBatches(
    11, 3, Profiles, MakeEnvironment(), MakeTarget(), Records, Reversed);
  TestEqual(TEXT("reversed gather count"), Reversed.Num(), Forward.Num());
  for (int32 Index = 0; Index < Forward.Num(); ++Index)
    TestEqual(FString::Printf(TEXT("gather hash %d"), Index),
      Reversed[Index].GatherHash, Forward[Index].GatherHash);

  TArray<FCrowdMassWorkBatchOutput> Outputs;
  for (const FCrowdMassWorkBatch& Batch : Forward)
  {
    FCrowdMassWorkBatchOutput& Output = Outputs.AddDefaulted_GetRef();
    Output.CapabilityProfileKey = Batch.CapabilityProfileKey;
    Output.WorkOutput.FixedStepIndex = Batch.WorkInput.FixedStepIndex;
    Output.WorkOutput.PlanRevision = Batch.WorkInput.PlanRevision;
    Output.WorkOutput.StableHash = Batch.GatherHash ^ 0x55aa55aau;
    Output.WorkOutput.bValid = true;
    for (const FCrowdAgentInput& Agent : Batch.WorkInput.Agents)
    {
      Output.WorkOutput.Movements.Add(MakeMovement(Agent));
      const FCrowdMassGatherRecord* Source = Records.FindByPredicate(
        [&Agent](const FCrowdMassGatherRecord& Candidate)
        {
          return Candidate.Identity.AgentId == Agent.AgentId;
        });
      Output.EntityRefs.Add(Source
        ? Source->AgentFacts.StableEntityRef : FCrowdStableEntityRef{});
    }
    Algo::Reverse(Output.WorkOutput.Movements);
    Algo::Reverse(Output.EntityRefs);
  }
  FCrowdMassCommitPlan ForwardPlan;
  FCrowdMassRuntimeBridge::MergeWorkOutputs(Outputs, ForwardPlan);
  TestTrue(TEXT("commit plan valid"), ForwardPlan.bValid);
  TestEqual(TEXT("commit records sorted globally"),
    ForwardPlan.Records[0].Movement.AgentId, 1);
  TestEqual(TEXT("all agents appear once"), ForwardPlan.Records.Num(), 3);

  Algo::Reverse(Outputs);
  FCrowdMassCommitPlan ReversedPlan;
  FCrowdMassRuntimeBridge::MergeWorkOutputs(Outputs, ReversedPlan);
  TestTrue(TEXT("reversed merge valid"), ReversedPlan.bValid);
  TestEqual(TEXT("merge hash stable across cohort order"),
    ReversedPlan.StableHash, ForwardPlan.StableHash);

  TArray<FCrowdMassCommitTarget> Targets;
  for (const FCrowdMassCommitRecord& Record : ForwardPlan.Records)
    Targets.Add({
      Record.EntityRef,
      Record.Movement.AgentId,
      Record.Movement.LifecycleSerial});
  Algo::Reverse(Targets);
  TestTrue(TEXT("full target set validates before any write"),
    FCrowdMassRuntimeBridge::ValidateCommitTargets(ForwardPlan, Targets));
  Targets[0].LifecycleSerial += 1;
  TestFalse(TEXT("lifecycle mismatch rejects whole commit plan"),
    FCrowdMassRuntimeBridge::ValidateCommitTargets(ForwardPlan, Targets));

  FCrowdMassSimulationStateFragment State;
  FCrowdMassMovementOutputFragment Applied;
  const FCrowdMassCommitRecord& Record = ForwardPlan.Records[0];
  const FCrowdMassCommitTarget Target = {
    Record.EntityRef,
    Record.Movement.AgentId,
    Record.Movement.LifecycleSerial};
  TestTrue(TEXT("validated record applies"),
    FCrowdMassRuntimeBridge::ApplyMovementToState(
      Record, Target, State, Applied));
  TestTrue(TEXT("position committed"),
    State.Position.Equals(Record.Movement.Position));
  TestEqual(TEXT("plan revision committed"),
    State.PlanRevision, ForwardPlan.PlanRevision);
  TestEqual(TEXT("movement output retained"),
    Applied.Value.StableHash, Record.Movement.StableHash);

  TArray<FCrowdMassWorkBatchOutput> Duplicate = Outputs;
  Duplicate.Add(Outputs[0]);
  FCrowdMassCommitPlan Invalid;
  FCrowdMassRuntimeBridge::MergeWorkOutputs(Duplicate, Invalid);
  TestFalse(TEXT("duplicate cohort output rejected"), Invalid.bValid);

  FCrowdMassGuidanceWorkInput GuidanceInput;
  GuidanceInput.FixedStepIndex = 11;
  GuidanceInput.PlanRevision = 3;
  GuidanceInput.Records = Records;
  const FCrowdMassGuidanceWorkOutput GuidanceForward =
    FCrowdMassGuidanceWork::Compose(GuidanceInput);
  TestTrue(TEXT("Runtime guidance WORK valid"), GuidanceForward.bValid);
  Algo::Reverse(GuidanceInput.Records);
  const FCrowdMassGuidanceWorkOutput GuidanceReverse =
    FCrowdMassGuidanceWork::Compose(GuidanceInput);
  TestEqual(TEXT("Runtime guidance WORK input order stable"),
    GuidanceReverse.StableHash, GuidanceForward.StableHash);

  FCrowdMassSharedFlowResource FlowResource;
  FCrowdMassSharedFlowBuildInput FlowBuildInput;
  FlowBuildInput.Config = FCrowdSharedFlowFieldKernel::MakeSf1Config(1);
  const FCrowdMassSharedFlowBuildOutput FlowBuild =
    FCrowdMassSharedFlowWork::EnsureResource(FlowBuildInput, FlowResource);
  TestTrue(TEXT("Runtime shared flow resource builds"), FlowBuild.bValid);
  TestEqual(TEXT("Runtime shared flow golden hash retained"),
    FlowResource.Field.BuildHash, 267519150u);
  const FCrowdMassSharedFlowBuildOutput FlowCacheHit =
    FCrowdMassSharedFlowWork::EnsureResource(FlowBuildInput, FlowResource);
  TestTrue(TEXT("Runtime shared flow cache hit stays valid"),
    FlowCacheHit.bValid && !FlowCacheHit.bFieldRebuilt);
  FCrowdMassSharedFlowResource DynamicFlowResource;
  FCrowdMassSharedFlowBuildInput DynamicFlowInput;
  DynamicFlowInput.Config.Revision = 91;
  DynamicFlowInput.Config.BoundsMin = FVector(0.0f, 0.0f, 0.0f);
  DynamicFlowInput.Config.BoundsMax = FVector(1000.0f, 500.0f, 0.0f);
  DynamicFlowInput.Config.CellSizeCm = 100.0f;
  DynamicFlowInput.Config.AgentInflateCm = 0.0f;
  DynamicFlowInput.Config.ConnectivityContractVersion = 2;
  DynamicFlowInput.Config.GoalLocation = FVector(950.0f, 250.0f, 60.0f);
  DynamicFlowInput.bDynamicTarget = true;
  DynamicFlowInput.TargetLocation = FVector(950.0f, 250.0f, 60.0f);
  const FCrowdMassSharedFlowBuildOutput DynamicFirst =
    FCrowdMassSharedFlowWork::EnsureResource(
      DynamicFlowInput, DynamicFlowResource);
  const uint32 DynamicTopologyHash = DynamicFlowResource.Field.TopologyHash;
  DynamicFlowInput.TargetLocation = FVector(50.0f, 250.0f, 60.0f);
  const FCrowdMassSharedFlowBuildOutput DynamicSecond =
    FCrowdMassSharedFlowWork::EnsureResource(
      DynamicFlowInput, DynamicFlowResource);
  TestTrue(TEXT("Runtime dynamic anchor rebuild succeeds"),
    DynamicFirst.bValid && DynamicSecond.bValid
      && DynamicSecond.bIntegrationRebuilt);
  TestEqual(TEXT("Runtime dynamic anchor keeps topology"),
    DynamicFlowResource.Field.TopologyHash, DynamicTopologyHash);
  const int32 SemanticRebuildCount =
    DynamicFlowResource.IntegrationRebuildCount;
  DynamicFlowInput.bForceIntegrationRefresh = true;
  const FCrowdMassSharedFlowBuildOutput DynamicReplay =
    FCrowdMassSharedFlowWork::EnsureResource(
      DynamicFlowInput, DynamicFlowResource);
  TestTrue(TEXT("Runtime dynamic rollback refresh rebuilds integration"),
    DynamicReplay.bValid && DynamicReplay.bIntegrationRebuilt);
  TestEqual(TEXT("Runtime dynamic rollback refresh does not add semantic rebuild"),
    DynamicFlowResource.IntegrationRebuildCount, SemanticRebuildCount);

  FCrowdMassSharedFlowSampleInput FlowInput;
  FlowInput.FixedStepIndex = 12;
  FlowInput.PlanRevision = 3;
  FlowInput.FixedStepSeconds = 1.0f / 30.0f;
  FlowInput.Fields.Add(&FlowResource.Field);
  FCrowdMassSharedFlowAgentInput FlowA;
  FlowA.AgentId = 1;
  FlowA.LifecycleSerial = 11;
  FlowA.FieldIndex = 0;
  FlowA.Location = FVector(-2200.0f, -2200.0f, 60.0f);
  FlowA.GoalLocation = FlowBuildInput.Config.GoalLocation;
  FlowA.MaximumSpeedCmps = 300.0f;
  FCrowdMassSharedFlowAgentInput FlowB = FlowA;
  FlowB.AgentId = 2;
  FlowB.LifecycleSerial = 12;
  FlowB.Location = FVector(-1200.0f, 100.0f, 60.0f);
  FlowInput.Agents = {FlowB, FlowA};
  const FCrowdMassSharedFlowSampleOutput FlowForward =
    FCrowdMassSharedFlowWork::BuildPreferred(FlowInput);
  TestTrue(TEXT("Runtime shared flow preferred WORK completes"),
    FlowForward.bValid);
  Algo::Reverse(FlowInput.Agents);
  const FCrowdMassSharedFlowSampleOutput FlowReverse =
    FCrowdMassSharedFlowWork::BuildPreferred(FlowInput);
  TestEqual(TEXT("Runtime shared flow preferred input order stable"),
    FlowReverse.StableHash, FlowForward.StableHash);
  for (const int32 ShardSize : {1, 2, 7})
  {
    const FCrowdMassSharedFlowSampleOutput ShardedForward =
      FCrowdMassSharedFlowWork::BuildPreferredSharded(
        FlowInput, ShardSize, false);
    const FCrowdMassSharedFlowSampleOutput ShardedReverse =
      FCrowdMassSharedFlowWork::BuildPreferredSharded(
        FlowInput, ShardSize, true);
    TestTrue(TEXT("Runtime shared flow shard completes"),
      ShardedForward.bValid && ShardedReverse.bValid);
    TestEqual(TEXT("Runtime shared flow shard size stable"),
      ShardedForward.StableHash, FlowForward.StableHash);
    TestEqual(TEXT("Runtime shared flow dispatch order stable"),
      ShardedReverse.StableHash, FlowForward.StableHash);
  }
  const FCrowdMassSharedFlowAgentInput DuplicateFlowAgent =
    FlowInput.Agents[0];
  FlowInput.Agents.Add(DuplicateFlowAgent);
  TestFalse(TEXT("Runtime shared flow preferred rejects duplicate agent"),
    FCrowdMassSharedFlowWork::BuildPreferred(FlowInput).bValid);

  FCrowdMassLocalPredictiveWorkInput LocalInput;
  LocalInput.FixedStepIndex = 12;
  LocalInput.PlanRevision = 3;
  LocalInput.Environment = FCrowdSharedFlowFieldKernel::MakeSf1Config(1);
  FCrowdLocalPredictiveAgent LocalA;
  LocalA.AgentId = 1;
  LocalA.Position = FVector2f(-500.0f, 0.0f);
  LocalA.PreferredVelocity = FVector2f(200.0f, 0.0f);
  LocalA.PhysicalRadiusCm = 42.0f;
  LocalA.HardSafetyGapCm = 10.0f;
  LocalA.MaxSpeedCmps = 300.0f;
  FCrowdLocalPredictiveAgent LocalB = LocalA;
  LocalB.AgentId = 2;
  LocalB.Position = FVector2f(500.0f, 0.0f);
  LocalInput.Agents = {LocalB, LocalA};
  const FCrowdMassLocalPredictiveWorkOutput LocalForward =
    FCrowdMassLocalPredictiveWork::Solve(LocalInput);
  TestTrue(TEXT("Runtime local predictive WORK completes"),
    LocalForward.bCompleted);
  Algo::Reverse(LocalInput.Agents);
  const FCrowdMassLocalPredictiveWorkOutput LocalReverse =
    FCrowdMassLocalPredictiveWork::Solve(LocalInput);
  TestEqual(TEXT("Runtime local predictive WORK input order stable"),
    LocalReverse.StableHash, LocalForward.StableHash);
  const FCrowdLocalPredictiveAgent DuplicateLocalAgent = LocalInput.Agents[0];
  LocalInput.Agents.Add(DuplicateLocalAgent);
  TestFalse(TEXT("Runtime local predictive WORK rejects duplicate agent"),
    FCrowdMassLocalPredictiveWork::Solve(LocalInput).bCompleted);

  FCrowdMassMovementPredictWorkInput PredictInput;
  PredictInput.FixedStepIndex = 13;
  PredictInput.PlanRevision = 3;
  PredictInput.FixedStepSeconds = 1.0f / 30.0f;
  FCrowdMassMovementPredictAgent PredictA;
  PredictA.AgentId = 1;
  PredictA.StartPosition = FVector(100.0f, 200.0f, 60.0f);
  PredictA.AutonomousPreferredVelocity = FVector(300.0f, 0.0f, 25.0f);
  PredictA.MaximumSpeedCmps = 300.0f;
  FCrowdMassMovementPredictAgent PredictB = PredictA;
  PredictB.AgentId = 2;
  PredictB.bUseLocalVelocity = true;
  PredictB.bLocalVelocityValid = true;
  PredictB.LocalVelocity = FVector(600.0f, 0.0f, 0.0f);
  PredictB.bParticleActive = false;
  FCrowdMassMovementPredictAgent PredictC = PredictA;
  PredictC.AgentId = 3;
  PredictC.bUseLocalVelocity = true;
  PredictC.bLocalVelocityValid = false;
  FCrowdMassMovementPredictAgent PredictD = PredictA;
  PredictD.AgentId = 4;
  PredictD.bFreezeAtBoundaryLocation = true;
  PredictD.BoundaryLocation = FVector(-50.0f, 25.0f, 70.0f);
  FCrowdMassMovementPredictAgent PredictE = PredictA;
  PredictE.AgentId = 5;
  PredictE.bVerticalOverride = true;
  PredictE.ProposedZ = 75.0f;
  PredictE.VerticalVelocityCmps = 45.0f;
  PredictInput.Agents = {PredictE, PredictC, PredictA, PredictD, PredictB};
  const FCrowdMassMovementPredictWorkOutput PredictForward =
    FCrowdMassMovementPredictWork::Predict(PredictInput);
  TestTrue(TEXT("Runtime movement predict WORK completes"),
    PredictForward.bCompleted && PredictForward.Results.Num() == 5);
  TestEqual(TEXT("Runtime movement predict globally sorts agents"),
    PredictForward.Results[0].AgentId, 1);
  TestTrue(TEXT("autonomous prediction integrates planar velocity"),
    PredictForward.Results[0].PredictedPosition.Equals(
      FVector(110.0f, 200.0f, 60.0f))
    && PredictForward.Results[0].Velocity.Equals(
      FVector(300.0f, 0.0f, 0.0f)));
  TestTrue(TEXT("valid local velocity is clamped to maximum speed"),
    PredictForward.Results[1].Velocity.Equals(FVector(300.0f, 0.0f, 0.0f))
    && !PredictForward.Results[1].bParticleActive);
  TestTrue(TEXT("invalid local velocity fails closed"),
    PredictForward.Results[2].Velocity.IsNearlyZero()
    && PredictForward.Results[2].PredictedPosition.Equals(
      PredictC.StartPosition));
  TestTrue(TEXT("boundary freeze overrides velocity and start"),
    PredictForward.Results[3].Velocity.IsNearlyZero()
    && PredictForward.Results[3].StartPosition.Equals(
      PredictD.BoundaryLocation)
    && PredictForward.Results[3].PredictedPosition.Equals(
      PredictD.BoundaryLocation));
  TestTrue(TEXT("vertical override preserves explicit reactive motion"),
    FMath::IsNearlyEqual(PredictForward.Results[4].PredictedPosition.Z, 75.0f)
    && FMath::IsNearlyEqual(PredictForward.Results[4].Velocity.Z, 45.0f));
  Algo::Reverse(PredictInput.Agents);
  const FCrowdMassMovementPredictWorkOutput PredictReverse =
    FCrowdMassMovementPredictWork::Predict(PredictInput);
  TestEqual(TEXT("Runtime movement predict input order stable"),
    PredictReverse.StableHash, PredictForward.StableHash);
  const FCrowdMassMovementPredictAgent DuplicatePredictAgent =
    PredictInput.Agents[0];
  PredictInput.Agents.Add(DuplicatePredictAgent);
  TestFalse(TEXT("Runtime movement predict rejects duplicate agent"),
    FCrowdMassMovementPredictWork::Predict(PredictInput).bCompleted);

  FCrowdMassParticleWorkInput ParticleInput;
  ParticleInput.FixedStepIndex = 13;
  ParticleInput.PlanRevision = 3;
  ParticleInput.Environment.FlowConfig =
    FCrowdSharedFlowFieldKernel::MakeSf1Config(1);
  FCrowdParticleConstraintAgent ParticleA;
  ParticleA.AgentId = 1;
  ParticleA.StartPosition = FVector(-50.0f, 0.0f, 60.0f);
  ParticleA.PredictedPosition = FVector(-40.0f, 0.0f, 60.0f);
  FCrowdParticleConstraintAgent ParticleB = ParticleA;
  ParticleB.AgentId = 2;
  ParticleB.StartPosition = FVector(50.0f, 0.0f, 60.0f);
  ParticleB.PredictedPosition = FVector(40.0f, 0.0f, 60.0f);
  ParticleInput.Agents = {ParticleB, ParticleA};
  const FCrowdMassParticleWorkOutput ParticleForward =
    FCrowdMassParticleWork::Solve(ParticleInput);
  TestTrue(TEXT("Runtime particle WORK completes"), ParticleForward.bCompleted);
  Algo::Reverse(ParticleInput.Agents);
  const FCrowdMassParticleWorkOutput ParticleReverse =
    FCrowdMassParticleWork::Solve(ParticleInput);
  TestEqual(TEXT("Runtime particle WORK input order stable"),
    ParticleReverse.StableHash, ParticleForward.StableHash);
  const FCrowdParticleConstraintAgent DuplicateParticleAgent =
    ParticleInput.Agents[0];
  ParticleInput.Agents.Add(DuplicateParticleAgent);
  TestFalse(TEXT("Runtime particle WORK rejects duplicate agent"),
    FCrowdMassParticleWork::Solve(ParticleInput).bCompleted);

  FCrowdMassFacingWorkInput FacingInput;
  FacingInput.FixedStepIndex = 14;
  FacingInput.PlanRevision = 3;
  FCrowdFacingInput FacingA;
  FacingA.AgentId = 1;
  FacingA.CurrentYawDegrees = 0.0f;
  FacingA.AutonomousPreferredVelocity = FVector2f(100.0f, 0.0f);
  FCrowdFacingInput FacingB = FacingA;
  FacingB.AgentId = 2;
  FacingB.CurrentYawDegrees = 90.0f;
  FacingB.bHasTarget = true;
  FacingB.bFinalPositionSettled = true;
  FacingB.Location = FVector2f(100.0f, 0.0f);
  FacingB.TargetLocation = FVector2f::ZeroVector;
  FacingInput.Agents = {FacingB, FacingA};
  const FCrowdMassFacingWorkOutput FacingForward =
    FCrowdMassFacingWork::Resolve(FacingInput);
  TestTrue(TEXT("Runtime facing WORK completes"), FacingForward.bCompleted);
  Algo::Reverse(FacingInput.Agents);
  const FCrowdMassFacingWorkOutput FacingReverse =
    FCrowdMassFacingWork::Resolve(FacingInput);
  TestEqual(TEXT("Runtime facing WORK input order stable"),
    FacingReverse.StableHash, FacingForward.StableHash);
  const FCrowdFacingInput DuplicateFacingAgent = FacingInput.Agents[0];
  FacingInput.Agents.Add(DuplicateFacingAgent);
  TestFalse(TEXT("Runtime facing WORK rejects duplicate agent"),
    FCrowdMassFacingWork::Resolve(FacingInput).bCompleted);

  TArray<FCrowdMassFinalKinematicState> PreparedKinematics;
  TArray<FCrowdFacingResult> PreparedFacings;
  for (const FCrowdMassBoundaryAgentRecord& Agent : BoundaryForward.Agents)
  {
    FCrowdMassFinalKinematicState& Kinematic =
      PreparedKinematics.AddDefaulted_GetRef();
    Kinematic.AgentId = Agent.Identity.AgentId;
    Kinematic.Position = Agent.State.Position + FVector(5.0f, 6.0f, 0.0f);
    Kinematic.Velocity = FVector(150.0f, 20.0f, 0.0f);
    Kinematic.bValid = true;
    FCrowdFacingResult& Facing = PreparedFacings.AddDefaulted_GetRef();
    Facing.AgentId = Agent.Identity.AgentId;
    Facing.ResolvedYawDegrees = 17.0f + Agent.Identity.AgentId;
  }
  Algo::Reverse(PreparedKinematics);
  FCrowdMassMovementFinalizeWorkInput PreparedFinalizeInput;
  TArray<FCrowdMassCommitTarget> PreparedCommitTargets;
  TestTrue(TEXT("Runtime movement finalize builds from prepared chain"),
    FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
      BoundaryForward, PreparedKinematics, PreparedFacings,
      PreparedFinalizeInput, PreparedCommitTargets));
  TestEqual(TEXT("prepared finalize records follow boundary AgentId order"),
    PreparedFinalizeInput.Records[0].AgentId, 1);
  TestEqual(TEXT("prepared finalize preserves lifecycle targets"),
    PreparedCommitTargets[0].LifecycleSerial,
    static_cast<uint32>(BoundaryForward.Agents[0].Identity.LifecycleSerial));
  Algo::Reverse(PreparedFacings);
  FCrowdMassMovementFinalizeWorkInput ReversedPreparedFinalizeInput;
  TArray<FCrowdMassCommitTarget> ReversedPreparedCommitTargets;
  TestTrue(TEXT("prepared finalize accepts reversed stage results"),
    FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
      BoundaryForward, PreparedKinematics, PreparedFacings,
      ReversedPreparedFinalizeInput, ReversedPreparedCommitTargets));
  TestEqual(TEXT("prepared finalize reverse is result equivalent"),
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(
      ReversedPreparedFinalizeInput).StableHash,
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(
      PreparedFinalizeInput).StableHash);
  PreparedFacings.Pop();
  TestFalse(TEXT("prepared finalize rejects incomplete stage results"),
    FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
      BoundaryForward, PreparedKinematics, PreparedFacings,
      ReversedPreparedFinalizeInput, ReversedPreparedCommitTargets));

  FCrowdMassMovementFinalizeWorkInput FinalizeInput;
  FinalizeInput.FixedStepIndex = 15;
  FinalizeInput.PlanRevision = 3;
  for (const FCrowdMassGatherRecord& Gather : Records)
  {
    FCrowdMassMovementFinalizeRecord& Final =
      FinalizeInput.Records.AddDefaulted_GetRef();
    Final.EntityRef = Gather.AgentFacts.StableEntityRef;
    Final.AgentId = Gather.Identity.AgentId;
    Final.LifecycleSerial = static_cast<uint32>(
      Gather.Identity.LifecycleSerial);
    Final.CapabilityProfileKey = Gather.Properties.CapabilityProfileKey;
    Final.Position = Gather.State.Position + FVector(10.0f, 20.0f, 0.0f);
    Final.Velocity = FVector(100.0f, 200.0f, 0.0f);
    Final.YawDegrees = 63.43f;
  }
  const FCrowdMassMovementFinalizeWorkOutput FinalizeForward =
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(FinalizeInput);
  TestTrue(TEXT("Runtime movement finalize WORK completes"),
    FinalizeForward.bCompleted && FinalizeForward.CommitPlan.bValid);
  TestEqual(TEXT("Runtime movement finalize globally sorts agents"),
    FinalizeForward.CommitPlan.Records[0].Movement.AgentId, 1);
  Algo::Reverse(FinalizeInput.Records);
  const FCrowdMassMovementFinalizeWorkOutput FinalizeReverse =
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(FinalizeInput);
  TestEqual(TEXT("Runtime movement finalize input order stable"),
    FinalizeReverse.StableHash, FinalizeForward.StableHash);
  const FCrowdMassMovementFinalizeRecord DuplicateFinalizeRecord =
    FinalizeInput.Records[0];
  FinalizeInput.Records.Add(DuplicateFinalizeRecord);
  TestFalse(TEXT("Runtime movement finalize rejects duplicate agent"),
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(FinalizeInput).bCompleted);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRuntimeMovementPipelineWorkTest,
  "MassCrowd.Runtime.MovementPipelineWork",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRuntimeMovementPipelineWorkTest::RunTest(
  const FString& Parameters)
{
  FCrowdMassMovementPipelineWorkInput CombinedInput;
  CombinedInput.Guidance.FixedStepIndex = 21;
  CombinedInput.Guidance.PlanRevision = 3;
  CombinedInput.Guidance.Records = {
    MakeRecord(3, 0), MakeRecord(1, 0), MakeRecord(2, 0)};
  CombinedInput.Environment = FCrowdSharedFlowFieldKernel::MakeSf1Config(1);
  CombinedInput.LocalPredictiveSettings.FixedStepSeconds = 1.0f / 30.0f;
  CombinedInput.LocalPredictiveSettings.TimeHorizonSeconds = 1.25f;
  CombinedInput.LocalPredictiveSettings.SpatialCellSizeCm = 600.0f;
  CombinedInput.LocalPredictiveSettings.VelocityQuantumCmps = 1.0f;
  CombinedInput.LocalPredictiveSettings.ConstraintEpsilonCmps = 0.1f;
  CombinedInput.FixedStepSeconds = 1.0f / 30.0f;
  CombinedInput.bRunLocalPredictive = true;
  for (const FCrowdMassGatherRecord& Record : CombinedInput.Guidance.Records)
  {
    FCrowdMassMovementPipelineAgentOverlay& Overlay =
      CombinedInput.AgentOverlays.AddDefaulted_GetRef();
    Overlay.AgentId = Record.Identity.AgentId;
    Overlay.MaximumSpeedCmps = 300.0f;
    Overlay.PreviousBlockedAgeSteps = Record.Identity.AgentId;
  }

  const FCrowdMassMovementPipelineWorkOutput CombinedForward =
    FCrowdMassMovementPipelineWork::Run(CombinedInput);
  TestTrue(TEXT("combined movement WORK completes"),
    CombinedForward.bCompleted);

  FCrowdMassGuidanceWorkInput GuidanceInput = CombinedInput.Guidance;
  const FCrowdMassGuidanceWorkOutput GuidanceLegacy =
    FCrowdMassGuidanceWork::Compose(GuidanceInput);
  FCrowdMassLocalPredictiveWorkInput LocalInput;
  LocalInput.FixedStepIndex = GuidanceInput.FixedStepIndex;
  LocalInput.PlanRevision = GuidanceInput.PlanRevision;
  LocalInput.Environment = CombinedInput.Environment;
  LocalInput.Settings = CombinedInput.LocalPredictiveSettings;
  TArray<FCrowdMassGatherRecord> SortedRecords = GuidanceInput.Records;
  SortedRecords.Sort([](const auto& A, const auto& B)
  {
    return A.Identity.AgentId < B.Identity.AgentId;
  });
  TArray<FCrowdMassMovementPipelineAgentOverlay> SortedOverlays =
    CombinedInput.AgentOverlays;
  SortedOverlays.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < SortedRecords.Num(); ++Index)
  {
    const FCrowdMassGatherRecord& Record = SortedRecords[Index];
    const FCrowdComposedGuidance& Composed =
      GuidanceLegacy.ComposedGuidance[Index];
    FCrowdLocalPredictiveAgent& Agent =
      LocalInput.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Record.Identity.AgentId;
    Agent.Position = FVector2f(Record.State.Position.X, Record.State.Position.Y);
    Agent.Velocity = FVector2f(Record.State.Velocity.X, Record.State.Velocity.Y);
    Agent.PreferredVelocity = FVector2f(
      Composed.AutonomousPreferredVelocity.X,
      Composed.AutonomousPreferredVelocity.Y);
    Agent.PhysicalRadiusCm = Record.Properties.PhysicalRadiusCm;
    Agent.HardSafetyGapCm = Record.Properties.HardSafetyGapCm;
    Agent.MaxSpeedCmps = SortedOverlays[Index].MaximumSpeedCmps;
    Agent.BlockedAgeSteps = SortedOverlays[Index].PreviousBlockedAgeSteps;
  }
  const FCrowdMassLocalPredictiveWorkOutput LocalLegacy =
    FCrowdMassLocalPredictiveWork::Solve(LocalInput);
  FCrowdMassMovementPredictWorkInput PredictInput;
  PredictInput.FixedStepIndex = GuidanceInput.FixedStepIndex;
  PredictInput.PlanRevision = GuidanceInput.PlanRevision;
  PredictInput.FixedStepSeconds = CombinedInput.FixedStepSeconds;
  for (int32 Index = 0; Index < SortedRecords.Num(); ++Index)
  {
    FCrowdMassMovementPredictAgent& Agent =
      PredictInput.Agents.AddDefaulted_GetRef();
    Agent.AgentId = SortedRecords[Index].Identity.AgentId;
    Agent.StartPosition = SortedRecords[Index].State.Position;
    Agent.AutonomousPreferredVelocity =
      GuidanceLegacy.ComposedGuidance[Index].AutonomousPreferredVelocity;
    Agent.LocalVelocity = FVector(
      LocalLegacy.Results[Index].Velocity.X,
      LocalLegacy.Results[Index].Velocity.Y, 0.0f);
    Agent.MaximumSpeedCmps = SortedOverlays[Index].MaximumSpeedCmps;
    Agent.bUseLocalVelocity = true;
    Agent.bLocalVelocityValid = LocalLegacy.Results[Index].bValid;
  }
  const FCrowdMassMovementPredictWorkOutput PredictLegacy =
    FCrowdMassMovementPredictWork::Predict(PredictInput);
  TestEqual(TEXT("compose hash unchanged by task merge"),
    CombinedForward.Guidance.StableHash, GuidanceLegacy.StableHash);
  TestEqual(TEXT("local hash unchanged by task merge"),
    CombinedForward.LocalPredictive.StableHash, LocalLegacy.StableHash);
  TestEqual(TEXT("predict hash unchanged by task merge"),
    CombinedForward.MovementPredict.StableHash, PredictLegacy.StableHash);

  Algo::Reverse(CombinedInput.Guidance.Records);
  Algo::Reverse(CombinedInput.AgentOverlays);
  const FCrowdMassMovementPipelineWorkOutput CombinedReverse =
    FCrowdMassMovementPipelineWork::Run(CombinedInput);
  TestEqual(TEXT("combined movement WORK input order stable"),
    CombinedReverse.StableHash, CombinedForward.StableHash);
  const FCrowdMassMovementPipelineAgentOverlay DuplicateOverlay =
    CombinedInput.AgentOverlays[0];
  CombinedInput.AgentOverlays.Add(DuplicateOverlay);
  TestFalse(TEXT("combined movement WORK rejects duplicate overlay"),
    FCrowdMassMovementPipelineWork::Run(CombinedInput).bCompleted);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRuntimeFacingFinalizeWorkTest,
  "MassCrowd.Runtime.FacingFinalizeWork",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRuntimeFacingFinalizeWorkTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdMassBoundaryAgentRecord> BoundaryRecords;
  for (const int32 AgentId : {3, 1, 2})
  {
    const FCrowdMassGatherRecord Gather = MakeRecord(AgentId, AgentId % 2);
    FCrowdMassBoundaryAgentRecord& Boundary =
      BoundaryRecords.AddDefaulted_GetRef();
    Boundary.Identity = Gather.Identity;
    Boundary.AgentFacts = Gather.AgentFacts;
    Boundary.State = Gather.State;
    Boundary.Properties = Gather.Properties;
  }
  FCrowdMassFacingFinalizeWorkInput Input;
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    31, 3, BoundaryRecords, Input.Snapshot);
  Input.Facing.FixedStepIndex = 31;
  Input.Facing.PlanRevision = 3;
  Input.Facing.Settings.FixedStepSeconds = 1.0f / 30.0f;
  for (const FCrowdMassBoundaryAgentRecord& Agent : Input.Snapshot.Agents)
  {
    FCrowdFacingInput& Facing = Input.Facing.Agents.AddDefaulted_GetRef();
    Facing.AgentId = Agent.Identity.AgentId;
    Facing.CurrentYawDegrees = Agent.State.YawDegrees;
    Facing.AutonomousPreferredVelocity = FVector2f(100.0f, 20.0f);
    Facing.Location = FVector2f(Agent.State.Position.X, Agent.State.Position.Y);
    Facing.TargetLocation = FVector2f(500.0f, 0.0f);
    Facing.bHasTarget = Agent.Identity.AgentId == 2;
    Facing.bFinalPositionSettled = Agent.Identity.AgentId == 2;

    FCrowdMassFinalKinematicState& Kinematic =
      Input.Kinematics.AddDefaulted_GetRef();
    Kinematic.AgentId = Agent.Identity.AgentId;
    Kinematic.Position = Agent.State.Position + FVector(4.0f, 5.0f, 0.0f);
    Kinematic.Velocity = FVector(120.0f, 30.0f, 0.0f);
    Kinematic.bValid = true;
  }

  const FCrowdMassFacingFinalizeWorkOutput CombinedForward =
    FCrowdMassFacingFinalizeWork::Run(Input);
  TestTrue(TEXT("combined facing/finalize WORK completes"),
    CombinedForward.bCompleted);

  const FCrowdMassFacingWorkOutput FacingLegacy =
    FCrowdMassFacingWork::Resolve(Input.Facing);
  for (const int32 ShardSize : {1, 2, 7})
  {
    const FCrowdMassFacingWorkOutput ShardedForward =
      FCrowdMassFacingWork::ResolveSharded(
        Input.Facing, ShardSize, false);
    const FCrowdMassFacingWorkOutput ShardedReverse =
      FCrowdMassFacingWork::ResolveSharded(
        Input.Facing, ShardSize, true);
    TestTrue(TEXT("facing shard completes"),
      ShardedForward.bCompleted && ShardedReverse.bCompleted);
    TestEqual(TEXT("facing shard size stable"),
      ShardedForward.StableHash, FacingLegacy.StableHash);
    TestEqual(TEXT("facing dispatch order stable"),
      ShardedReverse.StableHash, FacingLegacy.StableHash);
  }
  FCrowdMassMovementFinalizeWorkInput FinalizeLegacyInput;
  TArray<FCrowdMassCommitTarget> LegacyTargets;
  TestTrue(TEXT("legacy facing output builds finalize input"),
    FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
      Input.Snapshot, Input.Kinematics, FacingLegacy.Summary.Results,
      FinalizeLegacyInput, LegacyTargets));
  const FCrowdMassMovementFinalizeWorkOutput FinalizeLegacy =
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(FinalizeLegacyInput);
  TestEqual(TEXT("facing stage hash unchanged by task merge"),
    CombinedForward.Facing.StableHash, FacingLegacy.StableHash);
  TestEqual(TEXT("finalize stage hash unchanged by task merge"),
    CombinedForward.Finalize.StableHash, FinalizeLegacy.StableHash);

  Algo::Reverse(Input.Facing.Agents);
  Algo::Reverse(Input.Kinematics);
  const FCrowdMassFacingFinalizeWorkOutput CombinedReverse =
    FCrowdMassFacingFinalizeWork::Run(Input);
  TestEqual(TEXT("combined facing/finalize input order stable"),
    CombinedReverse.StableHash, CombinedForward.StableHash);

  Input.Facing.PlanRevision = 4;
  TestFalse(TEXT("combined facing/finalize rejects revision mismatch"),
    FCrowdMassFacingFinalizeWork::Run(Input).bCompleted);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRuntimeParticlePipelineWorkTest,
  "MassCrowd.Runtime.ParticlePipelineWork",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRuntimeParticlePipelineWorkTest::RunTest(
  const FString& Parameters)
{
  FCrowdMassParticlePipelineWorkInput Input;
  TArray<FCrowdMassBoundaryAgentRecord> BoundaryRecords;
  for (const int32 AgentId : {3, 1, 2})
  {
    const FCrowdMassGatherRecord Gather = MakeRecord(AgentId, 0);
    FCrowdMassBoundaryAgentRecord& Boundary =
      BoundaryRecords.AddDefaulted_GetRef();
    Boundary.Identity = Gather.Identity;
    Boundary.AgentFacts = Gather.AgentFacts;
    Boundary.State = Gather.State;
    Boundary.Properties = Gather.Properties;
  }
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    41, 3, BoundaryRecords, Input.Snapshot);
  Input.Particle.FixedStepIndex = 41;
  Input.Particle.PlanRevision = 3;
  Input.Particle.Environment.FlowConfig =
    FCrowdSharedFlowFieldKernel::MakeSf1Config(1);
  Input.Particle.Environment.FlowConfig.ObstacleSpecs.Reset();
  Input.Particle.Environment.FlowConfig.BoundsMin =
    FVector(-2000.0f, -2000.0f, 0.0f);
  Input.Particle.Environment.FlowConfig.BoundsMax =
    FVector(2000.0f, 2000.0f, 0.0f);
  Input.ExpectedExternalAgentCount = 1;
  for (const FCrowdMassBoundaryAgentRecord& Agent : Input.Snapshot.Agents)
  {
    FCrowdMassPredictedMovement& Predicted =
      Input.PredictedMovements.AddDefaulted_GetRef();
    Predicted.AgentId = Agent.Identity.AgentId;
    Predicted.StartPosition = Agent.State.Position;
    Predicted.PredictedPosition = Agent.State.Position + FVector(5.0f, 0.0f, 0.0f);
    Predicted.Velocity = FVector(150.0f, 0.0f, 0.0f);
    Predicted.bParticleActive = Agent.Identity.AgentId != 2;
    Predicted.bValid = true;
    if (Predicted.bParticleActive)
    {
      FCrowdParticleConstraintAgent& Particle =
        Input.Particle.Agents.AddDefaulted_GetRef();
      Particle.AgentId = Predicted.AgentId;
      Particle.StartPosition = Predicted.StartPosition;
      Particle.PredictedPosition = Predicted.PredictedPosition;
      Particle.PhysicalRadiusCm = 10.0f;
      Particle.HardSafetyGapCm = 1.0f;
      Particle.SoftMarginCm = 1.0f;
    }
  }
  FCrowdParticleConstraintAgent& External =
    Input.Particle.Agents.AddDefaulted_GetRef();
  External.AgentId = -100;
  External.StartPosition = FVector(500.0f, 500.0f, 60.0f);
  External.PredictedPosition = External.StartPosition;
  External.PhysicalRadiusCm = 20.0f;
  External.Mobility = 0.0f;

  const FCrowdMassParticlePipelineWorkOutput Forward =
    FCrowdMassParticlePipelineWork::Run(Input);
  TestTrue(TEXT("particle pipeline WORK completes"), Forward.bCompleted);
  TestEqual(TEXT("publish plan covers every boundary agent"),
    Forward.PublishPlan.Records.Num(), 3);
  TestEqual(TEXT("prepared results retain external and inactive facts"),
    Forward.PublishPlan.PreparedResults.Num(), 4);
  const FCrowdMassParticlePublishRecord* Inactive =
    Forward.PublishPlan.Records.FindByPredicate([](const auto& Record)
    {
      return Record.AgentId == 2;
    });
  TestTrue(TEXT("inactive particle publishes predicted position and zero speed"),
    Inactive && !Inactive->bParticleActive && !Inactive->bAppliedStateSample
      && Inactive->Result.CorrectedPosition.Equals(
        Input.PredictedMovements[1].PredictedPosition)
      && Inactive->Result.CorrectedVelocity.IsNearlyZero());

  Algo::Reverse(Input.PredictedMovements);
  Algo::Reverse(Input.Particle.Agents);
  const FCrowdMassParticlePipelineWorkOutput Reverse =
    FCrowdMassParticlePipelineWork::Run(Input);
  TestEqual(TEXT("particle pipeline input order stable"),
    Reverse.StableHash, Forward.StableHash);
  Input.ExpectedExternalAgentCount = 0;
  TestFalse(TEXT("particle pipeline rejects external count mismatch"),
    FCrowdMassParticlePipelineWork::Run(Input).bCompleted);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRuntimeMinimalMassWorldTest,
  "MassCrowd.Runtime.MinimalMassWorld",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRuntimeMinimalMassWorldTest::RunTest(const FString& Parameters)
{
  UWorld* TestWorld = nullptr;
  if (GEngine)
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
      if (Context.World()
        && (Context.WorldType == EWorldType::Editor
          || Context.WorldType == EWorldType::Game
          || Context.WorldType == EWorldType::PIE))
      {
        TestWorld = Context.World();
        break;
      }
  if (!TestNotNull(TEXT("test world is available"), TestWorld)) return false;
  TSharedRef<FMassEntityManager> EntityManager =
    MakeShared<FMassEntityManager>(TestWorld);
  EntityManager->SetDebugName(TEXT("MassCrowdRuntimeMinimalWorld"));
  EntityManager->Initialize();
  EntityManager->PostInitialize();

  const TArray<const UScriptStruct*> Types = {
    FCrowdMassAgentFragment::StaticStruct(),
    FCrowdMassBehaviorFragment::StaticStruct(),
    FCrowdMassSimulationStateFragment::StaticStruct(),
    FCrowdMassPropertiesFragment::StaticStruct(),
    FCrowdMassFacingFragment::StaticStruct(),
    FCrowdMassMovementOutputFragment::StaticStruct(),
    FCrowdMassAgentTag::StaticStruct()};
  const FMassArchetypeHandle Archetype = EntityManager->CreateArchetype(Types);
  TestTrue(TEXT("base movement archetype created"), Archetype.IsValid());
  const FMassEntityHandle Entity = EntityManager->CreateEntity(Archetype);
  TestTrue(TEXT("entity created"), EntityManager->IsEntityValid(Entity));

  FCrowdMassAgentFragment& Identity =
    EntityManager->GetFragmentDataChecked<FCrowdMassAgentFragment>(Entity);
  FCrowdMassSimulationStateFragment& State =
    EntityManager->GetFragmentDataChecked<FCrowdMassSimulationStateFragment>(Entity);
  FCrowdMassPropertiesFragment& Properties =
    EntityManager->GetFragmentDataChecked<FCrowdMassPropertiesFragment>(Entity);
  FCrowdMassBehaviorFragment& Behavior =
    EntityManager->GetFragmentDataChecked<FCrowdMassBehaviorFragment>(Entity);
  Identity.AgentId = 12;
  Identity.SetStableEntityRef({1, 12, 4});
  FCrowdCapabilitySet Capabilities;
  Capabilities.Add(ECrowdCapability::Move);
  Capabilities.Add(ECrowdCapability::MoveTo);
  Behavior.CapabilityBits = Capabilities.Bits;
  Behavior.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::MoveTo);
  State.Position = FVector(100.0f, 200.0f, 60.0f);
  State.bInitialized = true;
  Properties.CapabilityProfileKey = 0;

  FCrowdMassGatherRecord Record;
  Record.Identity = Identity;
  Record.AgentFacts = Behavior.GetAgentFacts(Identity);
  Record.State = State;
  Record.Properties = Properties;
  TArray<FCrowdMassWorkBatch> Batches;
  const FCrowdSimulationProfile Profile = MakeProfile(0);
  FCrowdMassRuntimeBridge::BuildWorkBatches(
    1, 1, MakeArrayView(&Profile, 1), MakeEnvironment(), MakeTarget(),
    MakeArrayView(&Record, 1), Batches);
  TestTrue(TEXT("Mass fragment facts gather into Core work batch"),
    Batches.Num() == 1 && Batches[0].bValid);
  TestNotNull(TEXT("movement trait class loads"),
    UMassCrowdMovementTrait::StaticClass());

  EntityManager->Deinitialize();
  return true;
}

#endif
