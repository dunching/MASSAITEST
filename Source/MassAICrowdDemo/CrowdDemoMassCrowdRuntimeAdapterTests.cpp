#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CrowdGuidanceComposeKernel.h"
#include "Mass/CrowdDemoGuidanceComposeKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "Mass/CrowdDemoRoundWorkKernel.h"
#include "MassCrowdGuidanceWork.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoMassCrowdRuntimeAdapterTest,
  "CrowdDemo.SF.Runtime.PluginAdapterEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoMassCrowdRuntimeAdapterTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoMassIdentityFragment Identity;
  Identity.Id = 8;
  Identity.LifecycleSerial = 17;
  FCrowdMassAgentFragment RuntimeIdentity;
  RuntimeIdentity.AgentId = Identity.Id;
  RuntimeIdentity.SetStableEntityRef({
    1u, static_cast<uint64>(Identity.Id) + 1u,
    static_cast<uint32>(Identity.LifecycleSerial)});
  FCrowdAgentFacts RuntimeFacts;
  RuntimeFacts.StableEntityRef = RuntimeIdentity.GetStableEntityRef();
  RuntimeFacts.CapabilitySet.Add(ECrowdCapability::Move);
  RuntimeFacts.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::Idle);
  FCrowdMassBehaviorFragment RuntimeBehavior;
  RuntimeBehavior.SetAgentFacts(RuntimeFacts);
  FCrowdDemoRoundSimStateFragment State;
  State.Location = FVector(123.0f, 456.0f, 60.0f);
  State.Velocity = FVector(70.0f, 80.0f, 0.0f);
  State.YawDegrees = 48.8f;
  State.PlanRevision = 5;
  State.SimulatedServerTimeSeconds = 99.0f;
  State.bInitialized = true;
  FCrowdDemoMassMovementFragment Movement;
  Movement.MaxSpeedCmPerSecond = 300.0f;
  FCrowdDemoParticlePropertiesFragment Particle;
  Particle.PhysicalRadiusCm = 42.0f;
  Particle.HardSafetyGapCm = 13.0f;
  Particle.SoftMarginCm = 19.0f;
  Particle.Mobility = 0.75f;
  Particle.CapabilityProfileKey = 0x12345678u;
  FCrowdDemoGuidanceCandidate SharedFlow =
    FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    Identity.Id, ECrowdDemoGuidanceProvider::SharedFlow, 5,
    FVector(300.0f, 0.0f, 0.0f), State.Location, 0.0f, true);
  FCrowdDemoGuidanceCandidate TargetRegion =
    FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    Identity.Id, ECrowdDemoGuidanceProvider::TargetRegion, 5,
    FVector(0.0f, 200.0f, 0.0f), FVector(500.0f, 500.0f, 60.0f),
    90.0f, true);
  const FCrowdDemoGuidanceCandidate BusinessOverride;

  FCrowdMassBoundaryAgentRecord Boundary;
  TestTrue(TEXT("Demo base facts adapt to Runtime boundary record"),
    FCrowdDemoMassCrowdRuntimeAdapter::BuildBoundaryAgentRecord(
      Identity, RuntimeIdentity, RuntimeBehavior,
      State, Movement, Particle, Boundary));
  FCrowdMassGatherRecord Gathered;
  Gathered.Identity = Boundary.Identity;
  Gathered.AgentFacts = Boundary.AgentFacts;
  Gathered.State = Boundary.State;
  Gathered.Properties = Boundary.Properties;
  Gathered.Guidance.SharedFlow =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(SharedFlow);
  Gathered.Guidance.TargetRegion =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(TargetRegion);
  Gathered.Guidance.BusinessOverride =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
      BusinessOverride);
  TestEqual(TEXT("identity preserved"), Gathered.Identity.AgentId, Identity.Id);
  TestEqual(TEXT("lifecycle preserved"),
    Gathered.Identity.LifecycleSerial, Identity.LifecycleSerial);
  TestTrue(TEXT("position preserved"),
    Gathered.State.Position.Equals(State.Location));
  TestEqual(TEXT("profile key preserved"),
    Gathered.Properties.CapabilityProfileKey,
    Particle.CapabilityProfileKey);
  TestEqual(TEXT("hard safety gap preserved"),
    Gathered.Properties.HardSafetyGapCm, Particle.HardSafetyGapCm);
  TestEqual(TEXT("soft margin preserved"),
    Gathered.Properties.SoftMarginCm, Particle.SoftMarginCm);
  TestEqual(TEXT("mobility preserved"),
    Gathered.Properties.Mobility, Particle.Mobility);
  TestEqual(TEXT("candidate hash preserved"),
    Gathered.Guidance.TargetRegion.StableHash,
    TargetRegion.StableHash);

  const FCrowdDemoGuidanceCandidate LegacyCandidates[] = {
    SharedFlow, TargetRegion, BusinessOverride};
  const FCrowdGuidanceCandidate CoreCandidates[] = {
    Gathered.Guidance.SharedFlow,
    Gathered.Guidance.TargetRegion,
    Gathered.Guidance.BusinessOverride};
  const FCrowdDemoComposedGuidance Legacy =
    FCrowdDemoGuidanceComposeKernel::Compose(
      Identity.Id, 5, LegacyCandidates, State.Location, State.YawDegrees);
  const FCrowdComposedGuidance Core = FCrowdGuidanceComposeKernel::Compose(
    Identity.Id, 5, CoreCandidates, State.Location, State.YawDegrees);
  TestEqual(TEXT("provider selection unchanged by adapter"),
    static_cast<uint8>(Core.SelectedProvider),
    static_cast<uint8>(Legacy.SelectedProvider));
  TestTrue(TEXT("autonomous velocity unchanged by adapter"),
    Core.AutonomousPreferredVelocity.Equals(
      Legacy.AutonomousPreferredVelocity));
  TestEqual(TEXT("compose hash unchanged by adapter"),
    Core.StableHash, Legacy.StableHash);
  const FCrowdDemoComposedGuidance AdaptedComposed =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoComposedGuidance(Core);
  TestEqual(TEXT("composed agent id adapts back"),
    AdaptedComposed.AgentId, Legacy.AgentId);
  TestEqual(TEXT("composed provider adapts back"),
    static_cast<uint8>(AdaptedComposed.SelectedProvider),
    static_cast<uint8>(Legacy.SelectedProvider));
  TestTrue(TEXT("composed autonomous velocity adapts back"),
    AdaptedComposed.AutonomousPreferredVelocity.Equals(
      Legacy.AutonomousPreferredVelocity));
  TestEqual(TEXT("composed candidate set hash adapts back"),
    AdaptedComposed.CandidateSetHash, Legacy.CandidateSetHash);
  TestEqual(TEXT("composed stable hash adapts back"),
    AdaptedComposed.StableHash, Legacy.StableHash);

  FCrowdDemoRoundWorkInput LegacyWorkInput;
  LegacyWorkInput.FixedStepIndex = 12;
  FCrowdDemoRoundWorkAgentInput& LegacyWorkAgent =
    LegacyWorkInput.Agents.AddDefaulted_GetRef();
  LegacyWorkAgent.AgentId = Identity.Id;
  LegacyWorkAgent.PlanRevision = 5;
  LegacyWorkAgent.StopLocation = State.Location;
  LegacyWorkAgent.StopYawDegrees = State.YawDegrees;
  LegacyWorkAgent.SharedFlow = SharedFlow;
  LegacyWorkAgent.TargetRegion = TargetRegion;
  FCrowdMassGuidanceWorkInput RuntimeWorkInput;
  RuntimeWorkInput.FixedStepIndex = LegacyWorkInput.FixedStepIndex;
  RuntimeWorkInput.PlanRevision = 5;
  RuntimeWorkInput.Records.Add(Gathered);
  const FCrowdDemoRoundWorkOutput LegacyWork =
    FCrowdDemoRoundWorkKernel::ComposeGuidance(LegacyWorkInput);
  const FCrowdMassGuidanceWorkOutput RuntimeWork =
    FCrowdMassGuidanceWork::Compose(RuntimeWorkInput);
  TestTrue(TEXT("legacy WORK fixture valid"), LegacyWork.bValid);
  TestTrue(TEXT("Runtime WORK fixture valid"), RuntimeWork.bValid);
  TestEqual(TEXT("production WORK migration preserves batch hash"),
    RuntimeWork.StableHash, LegacyWork.StableHash);
  TestEqual(TEXT("production WORK migration preserves result hash"),
    RuntimeWork.ComposedGuidance[0].StableHash,
    LegacyWork.ComposedGuidance[0].StableHash);

  const FVector OriginalGatherPosition = Gathered.State.Position;
  const uint32 OriginalGuidanceHash = Gathered.Guidance.TargetRegion.StableHash;
  State.Location += FVector(10.0f, -20.0f, 0.0f);
  TargetRegion = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    Identity.Id, ECrowdDemoGuidanceProvider::TargetRegion, 5,
    FVector(-100.0f, 180.0f, 0.0f), FVector(420.0f, 540.0f, 60.0f),
    110.0f, true);
  FCrowdMassBoundaryAgentRecord RebuiltBoundary;
  TestTrue(TEXT("Runtime boundary rebuilds from current Demo facts"),
    FCrowdDemoMassCrowdRuntimeAdapter::BuildBoundaryAgentRecord(
      Identity, RuntimeIdentity, RuntimeBehavior,
      State, Movement, Particle, RebuiltBoundary));
  FCrowdMassGatherRecord RebuiltGather;
  RebuiltGather.Identity = RebuiltBoundary.Identity;
  RebuiltGather.AgentFacts = RebuiltBoundary.AgentFacts;
  RebuiltGather.State = RebuiltBoundary.State;
  RebuiltGather.Properties = RebuiltBoundary.Properties;
  RebuiltGather.Guidance.TargetRegion =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(TargetRegion);
  TestFalse(TEXT("rebuilt mirror does not retain stale position"),
    RebuiltGather.State.Position.Equals(OriginalGatherPosition));
  TestNotEqual(TEXT("rebuilt mirror does not retain stale guidance"),
    RebuiltGather.Guidance.TargetRegion.StableHash, OriginalGuidanceHash);

  FCrowdMassCommitRecord Commit;
  Commit.EntityRef = RuntimeIdentity.GetStableEntityRef();
  Commit.CapabilityProfileKey = Particle.CapabilityProfileKey;
  Commit.PlanRevision = 6;
  Commit.Movement.AgentId = Identity.Id;
  Commit.Movement.LifecycleSerial = Identity.LifecycleSerial;
  Commit.Movement.Position = FVector(200.0f, 300.0f, 60.0f);
  Commit.Movement.Velocity = FVector(100.0f, 0.0f, 0.0f);
  Commit.Movement.YawDegrees = 15.0f;
  Commit.Movement.bValid = true;
  const float OriginalServerTime = State.SimulatedServerTimeSeconds;
  TestTrue(TEXT("valid commit adapts back to Demo state"),
    FCrowdDemoMassCrowdRuntimeAdapter::ApplyCommitRecord(
      Commit, Identity, RuntimeIdentity, State));
  TestTrue(TEXT("commit position applied"),
    State.Location.Equals(Commit.Movement.Position));
  TestEqual(TEXT("commit revision applied"), State.PlanRevision, 6);
  TestEqual(TEXT("Demo server time remains Demo-owned"),
    State.SimulatedServerTimeSeconds, OriginalServerTime);

  FCrowdDemoMassIdentityFragment WrongIdentity = Identity;
  ++WrongIdentity.LifecycleSerial;
  const FVector BeforeRejectedCommit = State.Location;
  TestFalse(TEXT("lifecycle mismatch rejects adapter commit"),
    FCrowdDemoMassCrowdRuntimeAdapter::ApplyCommitRecord(
      Commit, WrongIdentity, RuntimeIdentity, State));
  TestTrue(TEXT("rejected commit does not partially write"),
    State.Location.Equals(BeforeRejectedCommit));
  return true;
}

#endif
