#include "Misc/AutomationTest.h"

#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCapabilityProfileContractTest,
  "CrowdDemo.SoftPressure.Capability.ProfileContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCapabilityProfileContractTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoCapabilityProfile> Profiles;
  FCrowdDemoCapabilityProfileKernel::BuildP0Profiles(Profiles);
  TestEqual(TEXT("P0 profile count"), Profiles.Num(), 7);
  for (const auto& Profile : Profiles)
    TestTrue(TEXT("P0 profile valid"), Profile.bValid);
  TestEqual(TEXT("melee keeps strict distance band"),
    Profiles[0].TargetDistanceResponsePolicy,
    ECrowdDemoTargetDistanceResponsePolicy::StrictBand);
  TestEqual(TEXT("ranged uses acquire then hold"),
    Profiles[1].TargetDistanceResponsePolicy,
    ECrowdDemoTargetDistanceResponsePolicy::AcquireThenHold);

  TestEqual(TEXT("small-large hard distance"),
    FCrowdDemoCapabilityProfileKernel::ComputePairHardDistanceCm(
      Profiles[0].Particle, Profiles[5].Particle), 100.0f);
  TestEqual(TEXT("large-large hard distance"),
    FCrowdDemoCapabilityProfileKernel::ComputePairHardDistanceCm(
      Profiles[5].Particle, Profiles[6].Particle), 130.0f);
  TestEqual(TEXT("small-large soft distance"),
    FCrowdDemoCapabilityProfileKernel::ComputePairSoftDistanceCm(
      Profiles[0].Particle, Profiles[5].Particle), 134.0f);

  float ShareA = 0.0f;
  float ShareB = 0.0f;
  TestTrue(TEXT("1:3 mobility valid"),
    FCrowdDemoCapabilityProfileKernel::ComputeMobilityShares(1.0f, 3.0f, ShareA, ShareB));
  TestEqual(TEXT("1:3 share A"), ShareA, 0.25f);
  TestEqual(TEXT("1:3 share B"), ShareB, 0.75f);
  TestTrue(TEXT("zero mobility valid"),
    FCrowdDemoCapabilityProfileKernel::ComputeMobilityShares(0.0f, 2.0f, ShareA, ShareB));
  TestEqual(TEXT("zero mobility immovable"), ShareA, 0.0f);
  TestFalse(TEXT("negative mobility invalid"),
    FCrowdDemoCapabilityProfileKernel::ComputeMobilityShares(-1.0f, 1.0f, ShareA, ShareB));

  TestFalse(TEXT("large profile cannot fit 139cm corridor"),
    FCrowdDemoCapabilityProfileKernel::IsStraightCorridorFeasible(139.0f, Profiles[5].Particle));
  TestTrue(TEXT("large profile fits 140cm corridor"),
    FCrowdDemoCapabilityProfileKernel::IsStraightCorridorFeasible(140.0f, Profiles[5].Particle));

  FCrowdDemoCapabilityProfile Normalized = Profiles[5];
  Normalized.MinimumCenterDistanceCm = 100.0f;
  TestTrue(TEXT("normalize below target hard distance"),
    FCrowdDemoCapabilityProfileKernel::NormalizeProfile(Normalized));
  TestEqual(TEXT("large target hard distance"), Normalized.NormalizedMinimumCenterDistanceCm, 170.0f);
  TestTrue(TEXT("normalization counted"), Normalized.bMinimumNormalizedToHardDistance);

  FCrowdDemoCapabilityProfile Invalid = Profiles[0];
  Invalid.Particle.Mobility = -0.1f;
  TestFalse(TEXT("negative profile mobility rejected"),
    FCrowdDemoCapabilityProfileKernel::NormalizeProfile(Invalid));

  const uint32 BaseKey = Profiles[0].CapabilityProfileKey;
  auto ExpectChangedKey = [this, &Profiles, BaseKey](auto Mutator, const TCHAR* Label)
  {
    FCrowdDemoCapabilityProfile Changed = Profiles[0];
    Mutator(Changed);
    TestTrue(Label, FCrowdDemoCapabilityProfileKernel::NormalizeProfile(Changed));
    TestNotEqual(Label, Changed.CapabilityProfileKey, BaseKey);
  };
  ExpectChangedKey([](auto& P) { P.Particle.PhysicalRadiusCm += 1.0f; }, TEXT("radius changes key"));
  ExpectChangedKey([](auto& P) { P.Particle.HardSafetyGapCm += 1.0f; }, TEXT("hard gap changes key"));
  ExpectChangedKey([](auto& P) { P.Particle.SoftMarginCm += 1.0f; }, TEXT("soft margin changes key"));
  ExpectChangedKey([](auto& P) { P.Particle.Mobility += 0.001f; }, TEXT("mobility changes key"));
  ExpectChangedKey([](auto& P) { P.MinimumCenterDistanceCm += 1.0f; }, TEXT("minimum changes key"));
  ExpectChangedKey([](auto& P) { P.MaximumCenterDistanceCm += 1.0f; }, TEXT("maximum changes key"));
  ExpectChangedKey([](auto& P) { P.TargetPhysicalRadiusCm += 1.0f; }, TEXT("target radius changes key"));
  ExpectChangedKey([](auto& P) { P.TargetHardSafetyGapCm += 1.0f; }, TEXT("target gap changes key"));
  ExpectChangedKey([](auto& P)
  {
    P.TargetDistanceResponsePolicy = ECrowdDemoTargetDistanceResponsePolicy::AcquireThenHold;
  }, TEXT("distance response policy changes key"));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCapabilityProfileMembershipTest,
  "CrowdDemo.SoftPressure.Capability.Membership",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCapabilityProfileMembershipTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoCapabilityProfile> Profiles;
  TArray<FCrowdDemoCapabilityAgentAssignment> Assignments;
  FCrowdDemoCapabilityProfileKernel::BuildP0Profiles(Profiles);
  FCrowdDemoCapabilityProfileKernel::BuildP0Assignments(100, Assignments);
  const int32 ExpectedCounts[7] = {3, 3, 3, 2, 3, 3, 3};
  for (int32 Index = 0; Index < Assignments.Num(); ++Index)
  {
    TestEqual(TEXT("formation index stable"), Assignments[Index].FormationIndex, Index);
    TestEqual(TEXT("agent id stable"), Assignments[Index].AgentId, 100 + Index);
  }
  TArray<FCrowdDemoCapabilityCohort> Cohorts;
  FCrowdDemoCapabilityProfileSummary Summary;
  FCrowdDemoCapabilityProfileKernel::BuildCohorts(Profiles, Assignments, Cohorts, Summary);
  TestTrue(TEXT("P0 membership valid"), Summary.bValid);
  TestEqual(TEXT("P0 agents"), Summary.AgentCount, 20);
  TestEqual(TEXT("P0 cohorts"), Cohorts.Num(), 7);
  TMap<int32, int32> CountByProfileId;
  for (const auto& Cohort : Cohorts)
    CountByProfileId.Add(Cohort.Profile.ProfileId, Cohort.AgentIds.Num());
  for (int32 ProfileId = 0; ProfileId < 7; ++ProfileId)
    TestEqual(TEXT("fixed P0 profile count"), CountByProfileId.FindRef(ProfileId), ExpectedCounts[ProfileId]);

  Algo::Reverse(Profiles);
  Algo::Reverse(Assignments);
  TArray<FCrowdDemoCapabilityCohort> ReversedCohorts;
  FCrowdDemoCapabilityProfileSummary ReversedSummary;
  FCrowdDemoCapabilityProfileKernel::BuildCohorts(
    Profiles, Assignments, ReversedCohorts, ReversedSummary);
  TestTrue(TEXT("reversed membership valid"), ReversedSummary.bValid);
  TestEqual(TEXT("reversed membership hash"),
    ReversedSummary.MembershipHash, Summary.MembershipHash);
  TestEqual(TEXT("reversed cohort count"), ReversedCohorts.Num(), Cohorts.Num());
  for (int32 Index = 0; Index < Cohorts.Num(); ++Index)
  {
    TestEqual(TEXT("reversed cohort key"),
      ReversedCohorts[Index].CapabilityProfileKey, Cohorts[Index].CapabilityProfileKey);
    TestEqual(TEXT("reversed cohort hash"),
      ReversedCohorts[Index].MembershipHash, Cohorts[Index].MembershipHash);
  }

  TArray<FCrowdDemoCapabilityDemandPhase> Phases;
  uint32 PhaseHash = 0;
  TestTrue(TEXT("P0 demand phases valid"),
    FCrowdDemoCapabilityProfileKernel::BuildDemandRegionPhaseOffsets(
      Cohorts, 16, Phases, PhaseHash));
  TestEqual(TEXT("one demand phase per cohort"), Phases.Num(), Cohorts.Num());
  TMap<FIntPoint, TArray<int32>> PhaseOffsetsByBand;
  for (const FCrowdDemoCapabilityDemandPhase& Phase : Phases)
  {
    const FCrowdDemoCapabilityCohort* Cohort = Cohorts.FindByPredicate(
      [&Phase](const FCrowdDemoCapabilityCohort& Candidate)
      {
        return Candidate.CapabilityProfileKey == Phase.CapabilityProfileKey;
      });
    TestNotNull(TEXT("phase references a cohort"), Cohort);
    if (Cohort)
    {
      const FIntPoint Band(
        FMath::RoundToInt(Cohort->Profile.NormalizedMinimumCenterDistanceCm),
        FMath::RoundToInt(Cohort->Profile.NormalizedMaximumCenterDistanceCm));
      PhaseOffsetsByBand.FindOrAdd(Band).Add(Phase.DemandRegionPhaseOffset);
    }
  }
  TestEqual(TEXT("P0 has three distance bands"), PhaseOffsetsByBand.Num(), 3);
  for (auto& Pair : PhaseOffsetsByBand)
  {
    Pair.Value.Sort();
    if (Pair.Value.Num() == 3)
    {
      TestEqual(TEXT("three-cohort band phase zero"), Pair.Value[0], 0);
      TestEqual(TEXT("three-cohort band phase five"), Pair.Value[1], 5);
      TestEqual(TEXT("three-cohort band phase ten"), Pair.Value[2], 10);
    }
    else
    {
      TestEqual(TEXT("single-cohort band count"), Pair.Value.Num(), 1);
      TestEqual(TEXT("single-cohort phase zero"), Pair.Value[0], 0);
    }
  }
  TestTrue(TEXT("same Ranged band is shared"),
    FCrowdDemoCapabilityProfileKernel::ShareTargetDistanceBand(Profiles[1], Profiles[4]));
  TestTrue(TEXT("same Melee band is shared"),
    FCrowdDemoCapabilityProfileKernel::ShareTargetDistanceBand(Profiles[0], Profiles[5]));
  TestFalse(TEXT("Melee and Ranged bands are isolated"),
    FCrowdDemoCapabilityProfileKernel::ShareTargetDistanceBand(Profiles[0], Profiles[1]));
  TestFalse(TEXT("Melee and MidRange bands are isolated"),
    FCrowdDemoCapabilityProfileKernel::ShareTargetDistanceBand(Profiles[2], Profiles[3]));
  TArray<FCrowdDemoCapabilityDemandPhase> ReversedPhases;
  uint32 ReversedPhaseHash = 0;
  Algo::Reverse(ReversedCohorts);
  TestTrue(TEXT("reversed demand phases valid"),
    FCrowdDemoCapabilityProfileKernel::BuildDemandRegionPhaseOffsets(
      ReversedCohorts, 16, ReversedPhases, ReversedPhaseHash));
  TestEqual(TEXT("reversed demand phase hash"), ReversedPhaseHash, PhaseHash);
  TestEqual(TEXT("reversed demand phase count"), ReversedPhases.Num(), Phases.Num());
  for (int32 Index = 0; Index < Phases.Num(); ++Index)
  {
    TestEqual(TEXT("reversed phase key"),
      ReversedPhases[Index].CapabilityProfileKey, Phases[Index].CapabilityProfileKey);
    TestEqual(TEXT("reversed phase offset"),
      ReversedPhases[Index].DemandRegionPhaseOffset,
      Phases[Index].DemandRegionPhaseOffset);
  }

  FCrowdDemoCapabilityProfile Alias = Profiles[0];
  Alias.ProfileId = 99;
  Profiles.Add(Alias);
  FCrowdDemoCapabilityAgentAssignment AliasAssignment;
  AliasAssignment.AgentId = 999;
  AliasAssignment.FormationIndex = 999;
  AliasAssignment.ProfileId = 99;
  AliasAssignment.CapabilityProfileKey = Alias.CapabilityProfileKey;
  Assignments.Add(AliasAssignment);
  FCrowdDemoCapabilityProfileKernel::BuildCohorts(Profiles, Assignments, Cohorts, Summary);
  TestTrue(TEXT("same facts alias remains valid"), Summary.bValid);
  TestEqual(TEXT("same facts merge into seven cohorts"), Cohorts.Num(), 7);

  Assignments.Add(AliasAssignment);
  FCrowdDemoCapabilityProfileKernel::BuildCohorts(Profiles, Assignments, Cohorts, Summary);
  TestFalse(TEXT("duplicate agent rejected"), Summary.bValid);
  TestEqual(TEXT("duplicate agent counted"), Summary.DuplicateAgentIdCount, 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCapabilityParticleIntegrationTest,
  "CrowdDemo.SoftPressure.Capability.ParticleIntegration",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCapabilityParticleIntegrationTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoCapabilityProfile> Profiles;
  FCrowdDemoCapabilityProfileKernel::BuildP0Profiles(Profiles);
  const auto& Small = Profiles[0].Particle;
  const auto& Large = Profiles[5].Particle;

  FCrowdDemoRoundRules EnvironmentRules;
  EnvironmentRules.ParticleProfile = Small;
  EnvironmentRules.FlowFieldConfig.AgentInflateCm = 70.0f;
  TestEqual(TEXT("homogeneous environment clearance remains physical"),
    EnvironmentRules.GetParticleEnvironmentHardClearanceCm(), 40.0f);
  EnvironmentRules.bEnableHeterogeneousProfiles = 1;
  TestEqual(TEXT("heterogeneous environment clearance follows shared navigation domain"),
    EnvironmentRules.GetParticleEnvironmentHardClearanceCm(), 70.0f);

  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  FCrowdDemoParticleConstraintAgent& AgentA = Agents.AddDefaulted_GetRef();
  AgentA.AgentId = 10;
  AgentA.StartPosition = FVector(0.0f, 0.0f, 60.0f);
  AgentA.PredictedPosition = FVector(5.0f, 0.0f, 60.0f);
  AgentA.PhysicalRadiusCm = Small.PhysicalRadiusCm;
  AgentA.HardSafetyGapCm = Small.HardSafetyGapCm;
  AgentA.SoftMarginCm = Small.SoftMarginCm;
  AgentA.Mobility = Small.Mobility;
  FCrowdDemoParticleConstraintAgent& AgentB = Agents.AddDefaulted_GetRef();
  AgentB.AgentId = 20;
  AgentB.StartPosition = FVector(100.0f, 0.0f, 60.0f);
  AgentB.PredictedPosition = FVector(95.0f, 0.0f, 60.0f);
  AgentB.PhysicalRadiusCm = Large.PhysicalRadiusCm;
  AgentB.HardSafetyGapCm = Large.HardSafetyGapCm;
  AgentB.SoftMarginCm = Large.SoftMarginCm;
  AgentB.Mobility = Large.Mobility;

  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.bConstrainToFlowBounds = false;
  FCrowdDemoParticleConstraintSettings Settings;
  Settings.SoftResponsePerSecond = 0.0f;
  TArray<FCrowdDemoParticleConstraintPair> Pairs;
  TArray<FCrowdDemoParticleConstraintResult> Results;
  FCrowdDemoParticleConstraintSummary Summary;
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, Environment, Settings, Pairs, Results, Summary);
  TestTrue(TEXT("heterogeneous pair solve valid"), Summary.bValid);
  TestEqual(TEXT("heterogeneous hard violation zero"), Summary.HardPairViolationCount, 0);
  TestEqual(TEXT("heterogeneous swept violation zero"), Summary.SweptPairViolationCount, 0);
  TestEqual(TEXT("heterogeneous result count"), Results.Num(), 2);
  if (Results.Num() == 2)
  {
    const float SmallMove = FVector::Dist2D(Results[0].CorrectedPosition, AgentA.PredictedPosition);
    const float LargeMove = FVector::Dist2D(Results[1].CorrectedPosition, AgentB.PredictedPosition);
    TestTrue(TEXT("higher mobility moves farther"), SmallMove > LargeMove);
    TestEqual(TEXT("quantized mobility correction ratio"),
      FMath::RoundToInt(SmallMove), 4 * FMath::RoundToInt(LargeMove));
  }

  FCrowdDemoParticleConstraintEnvironment ContactEnvironment;
  ContactEnvironment.FlowConfig.BoundsMin = FVector(-200.0f, -200.0f, 0.0f);
  ContactEnvironment.FlowConfig.BoundsMax = FVector(200.0f, 200.0f, 0.0f);
  FCrowdDemoSharedFlowObstacleSpec Obstacle;
  Obstacle.ObstacleId = 7;
  Obstacle.Center = FVector::ZeroVector;
  Obstacle.Extent = FVector(50.0f, 50.0f, 100.0f);
  ContactEnvironment.FlowConfig.ObstacleSpecs = {Obstacle};
  Agents[0].PredictedPosition = FVector(105.0f, 0.0f, 60.0f);
  Agents[1].PredictedPosition = FVector(105.0f, 0.0f, 60.0f);
  TArray<FVector> ContactPositions = {Agents[0].PredictedPosition, Agents[1].PredictedPosition};
  TArray<FCrowdDemoParticleEnvironmentContact> Contacts;
  TestTrue(TEXT("heterogeneous obstacle contacts valid"),
    FCrowdDemoParticleConstraintKernel::BuildEnvironmentContacts(
      Agents, ContactPositions, ContactEnvironment, Contacts));
  float SmallObstacleDeficit = 0.0f;
  float LargeObstacleDeficit = 0.0f;
  for (const auto& Contact : Contacts)
  {
    if (Contact.ContactKind != ECrowdDemoParticleEnvironmentContactKind::ObstacleEndpoint)
      continue;
    if (Contact.AgentId == 10) SmallObstacleDeficit = FMath::Max(SmallObstacleDeficit, Contact.HardDeficitCm);
    if (Contact.AgentId == 20) LargeObstacleDeficit = FMath::Max(LargeObstacleDeficit, Contact.HardDeficitCm);
  }
  TestEqual(TEXT("small obstacle hard deficit"), SmallObstacleDeficit, 0.0f);
  TestTrue(TEXT("large obstacle uses larger inflation"), LargeObstacleDeficit > SmallObstacleDeficit);

  ContactPositions[0] = FVector(150.0f, 150.0f, 60.0f);
  ContactPositions[1] = ContactPositions[0];
  TestTrue(TEXT("heterogeneous bounds contacts valid"),
    FCrowdDemoParticleConstraintKernel::BuildEnvironmentContacts(
      Agents, ContactPositions, ContactEnvironment, Contacts));
  float SmallBoundsDeficit = 0.0f;
  float LargeBoundsDeficit = 0.0f;
  for (const auto& Contact : Contacts)
  {
    if (Contact.ContactKind != ECrowdDemoParticleEnvironmentContactKind::FlowBounds)
      continue;
    if (Contact.AgentId == 10) SmallBoundsDeficit = FMath::Max(SmallBoundsDeficit, Contact.HardDeficitCm);
    if (Contact.AgentId == 20) LargeBoundsDeficit = FMath::Max(LargeBoundsDeficit, Contact.HardDeficitCm);
  }
  TestEqual(TEXT("small bounds hard deficit"), SmallBoundsDeficit, 0.0f);
  TestTrue(TEXT("large bounds uses larger inflation"), LargeBoundsDeficit > SmallBoundsDeficit);

  TArray<FCrowdDemoParticleConstraintPair> PhysicalHashPairs;
  TArray<FCrowdDemoParticleConstraintResult> PhysicalHashResults;
  FCrowdDemoParticleConstraintSummary PhysicalHashSummary;
  FCrowdDemoParticleConstraintEnvironment HashEnvironment;
  HashEnvironment.bConstrainToFlowBounds = false;
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, HashEnvironment, Settings,
    PhysicalHashPairs, PhysicalHashResults, PhysicalHashSummary);

  Agents[0].EnvironmentHardClearanceCm = 70.0f;
  ContactPositions[0] = FVector(105.0f, 0.0f, 60.0f);
  ContactPositions[1] = FVector(105.0f, 0.0f, 60.0f);
  TestTrue(TEXT("shared navigation clearance contacts valid"),
    FCrowdDemoParticleConstraintKernel::BuildEnvironmentContacts(
      Agents, ContactPositions, ContactEnvironment, Contacts));
  float SmallSharedHardDistance = 0.0f;
  float SmallSharedObstacleDeficit = 0.0f;
  for (const auto& Contact : Contacts)
  {
    if (Contact.AgentId != 10
      || Contact.ContactKind != ECrowdDemoParticleEnvironmentContactKind::ObstacleEndpoint)
      continue;
    SmallSharedHardDistance = FMath::Max(SmallSharedHardDistance, Contact.HardDistanceCm);
    SmallSharedObstacleDeficit = FMath::Max(SmallSharedObstacleDeficit, Contact.HardDeficitCm);
  }
  TestEqual(TEXT("small shared environment hard clearance"), SmallSharedHardDistance, 70.0f);
  TestTrue(TEXT("small shared environment obstacle contact active"), SmallSharedObstacleDeficit > 0.0f);
  TArray<FCrowdDemoParticleConstraintPair> SharedPairs;
  TArray<FCrowdDemoParticleConstraintResult> SharedResults;
  FCrowdDemoParticleConstraintSummary SharedSummary;
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, HashEnvironment, Settings, SharedPairs, SharedResults, SharedSummary);
  TestNotEqual(TEXT("environment clearance changes candidate contract hash"),
    SharedSummary.CandidateHash, PhysicalHashSummary.CandidateHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoHeterogeneousTransitProductionRolloutTest,
  "CrowdDemo.SF.T6.HeterogeneousTransit.ProductionRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoHeterogeneousTransitProductionRolloutTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdDemoCapabilityProfile> Profiles;
  TArray<FCrowdDemoCapabilityAgentAssignment> Assignments;
  FCrowdDemoCapabilityProfileKernel::BuildP0Profiles(Profiles);
  FCrowdDemoCapabilityProfileKernel::BuildP0Assignments(100, Assignments);
  TMap<int32, const FCrowdDemoCapabilityProfile*> ProfileByFormation;
  for (const auto& Assignment : Assignments)
    ProfileByFormation.Add(Assignment.FormationIndex, &Profiles[Assignment.ProfileId]);

  TArray<FCrowdDemoValidCorridorTransitLayoutInput> LayoutInputs;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Input = LayoutInputs.AddDefaulted_GetRef();
    Input.AgentId = 100 + Index;
    Input.FormationIndex = Index;
  }
  const auto Layout = FCrowdDemoValidCorridorTransitKernel::BuildLayout(
    LayoutInputs, 60.0f, 10.0f, FVector(0.0f, -2850.0f, 60.0f), 140.0f);
  TestTrue(TEXT("T6A heterogeneous layout valid"), Layout.bValid);
  TestEqual(TEXT("T6A heterogeneous layout agents"), Layout.Agents.Num(), 20);
  for (int32 A = 0; A < Layout.Agents.Num(); ++A)
    for (int32 B = A + 1; B < Layout.Agents.Num(); ++B)
    {
      const auto* ProfileA = ProfileByFormation.FindRef(Layout.Agents[A].FormationIndex);
      const auto* ProfileB = ProfileByFormation.FindRef(Layout.Agents[B].FormationIndex);
      const float HardDistance = FCrowdDemoCapabilityProfileKernel::ComputePairHardDistanceCm(
        ProfileA->Particle, ProfileB->Particle);
      TestTrue(TEXT("T6A initial heterogeneous hard separation"),
        FVector::Dist2D(Layout.Agents[A].SpawnLocation, Layout.Agents[B].SpawnLocation)
          + KINDA_SMALL_NUMBER >= HardDistance);
    }

  auto Config = FCrowdDemoValidCorridorTransitKernel::MakeFlowConfig();
  Config.AgentInflateCm = 70.0f;
  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("T6A largest-clearance flow builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));

  TArray<FCrowdDemoValidCorridorTransitStepAgent> States;
  for (const auto& LayoutAgent : Layout.Agents)
  {
    auto& State = States.AddDefaulted_GetRef();
    State.AgentId = LayoutAgent.AgentId;
    State.Location = LayoutAgent.SpawnLocation;
  }
  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.FlowConfig = Config;
  FCrowdDemoParticleConstraintSettings Settings;
  FCrowdDemoValidCorridorTransitProgress Progress;
  TMap<int32, int32> UnreachableByAgentId;
  bool bAllSafetyValid = true;
  for (int32 Step = 0; Step < 900; ++Step)
  {
    const bool bExitHold =
      FCrowdDemoValidCorridorTransitKernel::ShouldHoldCompletedGroup(Progress);
    TArray<FCrowdDemoParticleConstraintAgent> ParticleAgents;
    for (auto& State : States)
    {
      const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(Field, State.Location);
      State.FlowStatus = Sample.Status;
      if (Sample.Status != ECrowdDemoFlowLocationStatus::Reachable)
        ++UnreachableByAgentId.FindOrAdd(State.AgentId);
      float Speed = 800.0f;
      if (Sample.GuidanceDistanceCm > 0.0f)
        Speed = FMath::Min(Speed, Sample.GuidanceDistanceCm / Settings.FixedStepSeconds);
      const FVector Desired = Sample.bUnreachable || bExitHold
        ? FVector::ZeroVector : Sample.FlowDirection * Speed;
      const int32 FormationIndex = State.AgentId - 100;
      const auto* Profile = ProfileByFormation.FindRef(FormationIndex);
      auto& Particle = ParticleAgents.AddDefaulted_GetRef();
      Particle.AgentId = State.AgentId;
      Particle.StartPosition = State.Location;
      Particle.PredictedPosition = State.Location + Desired * Settings.FixedStepSeconds;
      Particle.PhysicalRadiusCm = Profile->Particle.PhysicalRadiusCm;
      Particle.HardSafetyGapCm = Profile->Particle.HardSafetyGapCm;
      Particle.EnvironmentHardClearanceCm = Config.AgentInflateCm;
      Particle.SoftMarginCm = Profile->Particle.SoftMarginCm;
      Particle.Mobility = Profile->Particle.Mobility;
    }
    TArray<FCrowdDemoParticleConstraintPair> Pairs;
    TArray<FCrowdDemoParticleConstraintResult> Results;
    FCrowdDemoParticleConstraintSummary Summary;
    FCrowdDemoParticleConstraintKernel::Solve(
      ParticleAgents, Environment, Settings, Pairs, Results, Summary);
    bAllSafetyValid = bAllSafetyValid && Summary.bValid
      && Summary.HardPairViolationCount == 0
      && Summary.SweptPairViolationCount == 0
      && Summary.ObstaclePenetrationCount == 0
      && Summary.BoundsViolationCount == 0;
    if (!Summary.bValid) break;
    TMap<int32, const FCrowdDemoParticleConstraintResult*> ById;
    for (const auto& Result : Results) ById.Add(Result.AgentId, &Result);
    for (auto& State : States)
      if (const auto* const* Result = ById.Find(State.AgentId))
      {
        State.Location = (*Result)->CorrectedPosition;
        State.Velocity = (*Result)->CorrectedVelocity;
      }
    FCrowdDemoValidCorridorTransitKernel::UpdateProgress(States, Step, Progress);
  }
  TestTrue(TEXT("T6A production rollout safety"), bAllSafetyValid);
  TestTrue(TEXT("T6A production rollout progress valid"), Progress.bValid);
  TestEqual(TEXT("T6A production rollout wall passed"),
    Progress.WallPassedAgentIds.Num(), 20);
  TestEqual(TEXT("T6A production rollout corridor exited"),
    Progress.CorridorExitedAgentIds.Num(), 20);
  TestEqual(TEXT("T6A production rollout completed"),
    Progress.CompletedAgentIds.Num(), 20);
  TestEqual(TEXT("T6A production rollout final settled"),
    Progress.FinalSettledAgentIds.Num(), 20);
  TestTrue(TEXT("T6A production rollout group settle step"),
    Progress.GroupSettledStep != INDEX_NONE);
  TestEqual(TEXT("T6A production rollout final deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 0);
  TestEqual(TEXT("T6A production rollout unreachable"),
    Progress.UnreachableSampleCount, 0);
  if (Progress.CompletedAgentIds.Num() != 20 || Progress.UnreachableSampleCount != 0)
  {
    for (const auto& State : States)
    {
      if (Progress.CompletedAgentIds.Contains(State.AgentId)
        && UnreachableByAgentId.FindRef(State.AgentId) == 0)
        continue;
      const int32 FormationIndex = State.AgentId - 100;
      const auto* Profile = ProfileByFormation.FindRef(FormationIndex);
      AddInfo(FString::Printf(
        TEXT("T6A fixture agent=%d formation=%d profile=%d radius=%.0f mobility=%.2f location=(%.0f,%.0f) status=%d unreachable_steps=%d low_steps=%d completed=%d"),
        State.AgentId, FormationIndex, Profile ? Profile->ProfileId : INDEX_NONE,
        Profile ? Profile->Particle.PhysicalRadiusCm : 0.0f,
        Profile ? Profile->Particle.Mobility : 0.0f,
        State.Location.X, State.Location.Y, static_cast<int32>(State.FlowStatus),
        UnreachableByAgentId.FindRef(State.AgentId),
        Progress.ConsecutiveLowSpeedStepsByAgentId.FindRef(State.AgentId),
        Progress.CompletedAgentIds.Contains(State.AgentId) ? 1 : 0));
    }
  }
  return true;
}

#endif
