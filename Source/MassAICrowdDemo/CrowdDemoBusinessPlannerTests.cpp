#include "CrowdDemoBusinessPlanner.h"
#include "CrowdDemoRangedAttackPlanner.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CrowdDemoBusinessPlannerTestsPrivate
{
  FCrowdDemoPlanningSnapshot BuildMixedSnapshot(
    const bool bReverseInput)
  {
    FCrowdDemoPlanningSnapshot Snapshot;
    Snapshot.ScenarioId = CrowdDemoBusinessScenarios::Mixed;
    Snapshot.FixedStepIndex = 60;
    Snapshot.FactRevision = 61;
    Snapshot.Settings.PopulationLimit = 20;
    Snapshot.Settings.InteractionRadiusCm = 10000.0f;
    for (int32 SlotIndex = 1; SlotIndex <= 20; ++SlotIndex)
    {
      FCrowdDemoPlannerAgentFact Agent;
      Agent.EntityRef = {
        1, static_cast<uint64>(SlotIndex), 1};
      FCrowdDemoBusinessPlannerRunner::BuildMixedAssignment(
        SlotIndex, Agent.Assignment);
      Agent.Position = FVector(
        static_cast<double>(SlotIndex) * 100.0, 0.0, 0.0);
      Agent.Facing = FVector::ForwardVector;
      Agent.TransitionRevision = 1;
      Agent.LastAttackFixedStep = -1000;
      if (Agent.Assignment.PlannerId
        == CrowdDemoBusinessPlanners::Logistics)
      {
        Agent.TaskRef = {
          2, static_cast<uint64>(SlotIndex), 1};
        Snapshot.Objectives.Add({
          CrowdDemoBusinessObjectives::LogisticsSource,
          Agent.EntityRef, FVector(-1000.0, 0.0, 0.0), 61});
        Snapshot.Objectives.Add({
          CrowdDemoBusinessObjectives::LogisticsSink,
          Agent.EntityRef, FVector(1000.0, 0.0, 0.0), 61});
      }
      else if (Agent.Assignment.PlannerId
        == CrowdDemoBusinessPlanners::Roam)
      {
        Snapshot.Objectives.Add({
          CrowdDemoBusinessObjectives::RoamRoute,
          Agent.EntityRef, FVector(0.0, 1000.0, 0.0), 61});
      }
      Snapshot.Agents.Add(Agent);
    }
    if (bReverseInput)
    {
      Algo::Reverse(Snapshot.Agents);
      Algo::Reverse(Snapshot.Objectives);
    }
    Snapshot.Finalize();
    return Snapshot;
  }

  class FDuplicatePlanner final : public ICrowdDemoBusinessPlanner
  {
  public:
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return CrowdDemoBusinessPlanners::Logistics;
    }

    bool Evaluate(
      const FCrowdDemoPlanningSnapshot&,
      const FCrowdDemoPlannerAgentFact&,
      FCrowdDemoPlannerWriter&) const override
    {
      return true;
    }
  };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBusinessPlannerRegistryTest,
  "CrowdDemo.BusinessPlanner.RegistryAndAssignment",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBusinessPlannerRegistryTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoBusinessPlannerRegistry Registry;
  TestTrue(TEXT("default registry freezes"),
    FCrowdDemoBusinessPlannerRunner::BuildDefaultRegistry(Registry));
  TestTrue(TEXT("registry frozen"), Registry.IsFrozen());
  TestNotEqual(TEXT("registry hash"), Registry.GetStableHash(), 0ull);
  TestFalse(TEXT("frozen registry rejects registration"),
    Registry.Register(
      MakeShared<
        CrowdDemoBusinessPlannerTestsPrivate::FDuplicatePlanner,
        ESPMode::ThreadSafe>()));

  TMap<uint32, int32> Counts;
  for (int32 SlotIndex = 1; SlotIndex <= 20; ++SlotIndex)
  {
    FCrowdDemoPlannerAssignment Assignment;
    TestTrue(TEXT("assignment is valid"),
      FCrowdDemoBusinessPlannerRunner::BuildMixedAssignment(
        SlotIndex, Assignment));
    ++Counts.FindOrAdd(Assignment.PlannerId.Value);
  }
  TestEqual(TEXT("logistics count"),
    Counts.FindRef(CrowdDemoBusinessPlanners::Logistics.Value), 6);
  TestEqual(TEXT("pursue count"),
    Counts.FindRef(CrowdDemoBusinessPlanners::PursueAttack.Value), 4);
  TestEqual(TEXT("guard count"),
    Counts.FindRef(CrowdDemoBusinessPlanners::GuardFlee.Value), 4);
  TestEqual(TEXT("roam count"),
    Counts.FindRef(CrowdDemoBusinessPlanners::Roam.Value), 2);
  TestEqual(TEXT("escort count"),
    Counts.FindRef(CrowdDemoBusinessPlanners::Escort.Value), 4);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBusinessPlannerDeterminismTest,
  "CrowdDemo.BusinessPlanner.MixedDeterminism",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBusinessPlannerDeterminismTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdDemoBusinessPlannerTestsPrivate;
  FCrowdDemoPlanningSnapshot Forward = BuildMixedSnapshot(false);
  FCrowdDemoPlanningSnapshot Reverse = BuildMixedSnapshot(true);
  TestTrue(TEXT("forward snapshot"), Forward.bValid);
  TestTrue(TEXT("reverse snapshot"), Reverse.bValid);
  TestEqual(TEXT("snapshot order independent"),
    Forward.StableHash, Reverse.StableHash);

  FCrowdDemoBusinessPlannerRegistry Registry;
  TestTrue(TEXT("registry"),
    FCrowdDemoBusinessPlannerRunner::BuildDefaultRegistry(Registry));
  FCrowdDemoPlannerDecisionBatch ForwardBatch;
  FCrowdDemoPlannerDecisionBatch ReverseBatch;
  TestTrue(TEXT("forward evaluate"),
    FCrowdDemoBusinessPlannerRunner::Evaluate(
      Registry, Forward, ForwardBatch));
  TestTrue(TEXT("reverse evaluate"),
    FCrowdDemoBusinessPlannerRunner::Evaluate(
      Registry, Reverse, ReverseBatch));
  TestEqual(TEXT("all agents planned"),
    ForwardBatch.Decisions.Num(), 20);
  TestEqual(TEXT("decision order independent"),
    ForwardBatch.StableHash, ReverseBatch.StableHash);
  for (const int32 ShardSize : {1, 3, 7, 20})
  {
    FCrowdDemoPlannerDecisionBatch ShardedForward;
    FCrowdDemoPlannerDecisionBatch ShardedReverse;
    TestTrue(TEXT("business shard evaluates"),
      FCrowdDemoBusinessPlannerRunner::EvaluateSharded(
        Registry, Forward, ShardSize, ShardedForward, false));
    TestTrue(TEXT("business reverse shard evaluates"),
      FCrowdDemoBusinessPlannerRunner::EvaluateSharded(
        Registry, Forward, ShardSize, ShardedReverse, true));
    TestEqual(TEXT("business shard size stable"),
      ShardedForward.StableHash, ForwardBatch.StableHash);
    TestEqual(TEXT("business dispatch order stable"),
      ShardedReverse.StableHash, ForwardBatch.StableHash);
  }
  int32 AttackIntentCount = 0;
  for (const FCrowdDemoPlannerDecision& Decision
    : ForwardBatch.Decisions)
    for (const FCrowdDemoHostIntent& Intent
      : Decision.HostIntents)
      AttackIntentCount += Intent.ActionTypeId
        == CrowdDemoBusinessActions::Attack ? 1 : 0;
  TestTrue(TEXT("classic pursue attack uses host intent"),
    AttackIntentCount > 0);

  FCrowdDemoPlanningSnapshot MissingObjective = Forward;
  MissingObjective.Objectives.RemoveAt(0);
  TestTrue(TEXT("changed snapshot finalizes"),
    MissingObjective.Finalize());
  FCrowdDemoPlannerDecisionBatch RejectedBatch;
  TestFalse(TEXT("missing logistics objective rejects batch"),
    FCrowdDemoBusinessPlannerRunner::Evaluate(
      Registry, MissingObjective, RejectedBatch));

  FCrowdDemoPlanningSnapshot NoBusiness;
  NoBusiness.ScenarioId = CrowdDemoBusinessScenarios::NoBusiness;
  NoBusiness.FixedStepIndex = 1;
  NoBusiness.FactRevision = 1;
  TestTrue(TEXT("no-business snapshot"), NoBusiness.Finalize());
  TestTrue(TEXT("no-business evaluates"),
    FCrowdDemoBusinessPlannerRunner::Evaluate(
      Registry, NoBusiness, RejectedBatch));
  TestTrue(TEXT("no-business decision is empty"),
    RejectedBatch.Decisions.IsEmpty());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRangedAttackBusinessPlannerTest,
  "CrowdDemo.BusinessPlanner.RangedAttackLifecycle",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRangedAttackBusinessPlannerTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoRangedAttackSettings Settings;
  Settings.ShooterCount = 1;
  Settings.WindupFixedSteps = 1;
  Settings.RecoveryFixedSteps = 1;
  Settings.CooldownFixedSteps = 1;
  Settings.ProjectileSpeedCmps = 1000.0f;
  Settings.MuzzleForwardOffsetCm = 50.0f;
  Settings.PositionQuantumCm = 1.0f;
  Settings.VelocityQuantumCmps = 1.0f;
  TArray<FCrowdDemoRangedAttackAgent> Agents;
  FCrowdDemoRangedAttackAgent& Shooter =
    Agents.AddDefaulted_GetRef();
  Shooter.EntityRef = {1, 1, 1};
  Shooter.AgentId = 1;
  Shooter.LifecycleSerial = 1;
  Shooter.StateLifecycleSerial = 1;
  Shooter.FormationIndex = 0;
  Shooter.FactionId = 1;
  Shooter.Position = FVector::ZeroVector;
  FCrowdDemoRangedAttackAgent& Target =
    Agents.AddDefaulted_GetRef();
  Target.EntityRef = {1, 2, 1};
  Target.AgentId = 2;
  Target.LifecycleSerial = 1;
  Target.StateLifecycleSerial = 1;
  Target.FormationIndex = 1;
  Target.FactionId = 2;
  Target.Position = FVector(1000.0, 0.0, 0.0);
  TArray<FCrowdDemoFireIntent> Intents;
  FCrowdDemoRangedAttackPlanSummary Summary;
  TestTrue(TEXT("target acquire"),
    FCrowdDemoRangedAttackPlanner::Advance(
      7, 0, Settings, Agents, Intents, Summary));
  TestEqual(TEXT("one target acquired"),
    Summary.TargetAcquiredCount, 1);
  TestTrue(TEXT("acquire does not fire"), Intents.IsEmpty());
  TestTrue(TEXT("windup emits fire intent"),
    FCrowdDemoRangedAttackPlanner::Advance(
      7, 1, Settings, Agents, Intents, Summary));
  TestEqual(TEXT("one fire intent"), Intents.Num(), 1);
  TestTrue(TEXT("fire intent valid"),
    Intents[0].IsValid());
  const uint64 FirstProjectileId = Intents[0].ProjectileId;
  TestTrue(TEXT("fire advances to recovery"),
    FCrowdDemoRangedAttackPlanner::Advance(
      7, 2, Settings, Agents, Intents, Summary));
  TestTrue(TEXT("recovery emits no duplicate fire"),
    Intents.IsEmpty());
  TestNotEqual(TEXT("stable projectile id exists"),
    FirstProjectileId, 0ull);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoGenericAttackPlannerTest,
  "CrowdDemo.BusinessPlanner.GenericAttackProfilesAndReplay",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoGenericAttackPlannerTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdDemoAttackProfileV1> Profiles;
  const auto AddProfile = [&Profiles](
    const uint32 ProfileId, const uint32 PayloadTypeId,
    const ECrowdDemoAttackArchetype Archetype,
    const float Range)
  {
    FCrowdDemoAttackProfileV1& Profile =
      Profiles.AddDefaulted_GetRef();
    Profile.ProfileId = ProfileId;
    Profile.PayloadTypeId = PayloadTypeId;
    Profile.EffectProfileId = 1;
    Profile.Archetype = Archetype;
    Profile.WindupFixedSteps = 1;
    Profile.RecoveryFixedSteps = 1;
    Profile.CooldownFixedSteps = 1;
    Profile.MaximumDistanceCm = Range;
    Profile.QueryRadiusCm = 20.0f;
    Profile.MuzzleForwardOffsetCm = 10.0f;
    Profile.ProjectileSpeedCmps =
      Archetype == ECrowdDemoAttackArchetype::Ranged
        ? 1000.0f : 0.0f;
    Profile.Damage = 20;
  };
  AddProfile(
    CrowdDemoAttackProfileIds::Melee,
    CrowdDemoAttackPayloadTypeIds::Melee,
    ECrowdDemoAttackArchetype::Melee, 300.0f);
  AddProfile(
    CrowdDemoAttackProfileIds::MidRange,
    CrowdDemoAttackPayloadTypeIds::MidRange,
    ECrowdDemoAttackArchetype::MidRange, 600.0f);
  AddProfile(
    CrowdDemoAttackProfileIds::Ranged,
    CrowdDemoAttackPayloadTypeIds::Ranged,
    ECrowdDemoAttackArchetype::Ranged, 1000.0f);

  TArray<FCrowdDemoAttackAgent> Forward;
  for (int32 Pair = 0; Pair < 3; ++Pair)
  {
    const float Range = Pair == 0 ? 200.0f
      : Pair == 1 ? 500.0f : 900.0f;
    const uint32 ProfileId = Pair == 0
      ? CrowdDemoAttackProfileIds::Melee
      : Pair == 1
        ? CrowdDemoAttackProfileIds::MidRange
        : CrowdDemoAttackProfileIds::Ranged;
    FCrowdDemoAttackAgent& Source =
      Forward.AddDefaulted_GetRef();
    Source.EntityRef = {
      1, static_cast<uint64>(Pair + 1), 1};
    Source.FactionId = 1;
    Source.AttackProfileId = ProfileId;
    Source.Position = FVector(0.0, Pair * 5000.0, 0.0);
    Source.Facing = FVector::ForwardVector;
    FCrowdDemoAttackAgent& Target =
      Forward.AddDefaulted_GetRef();
    Target.EntityRef = {
      1, static_cast<uint64>(Pair + 11), 1};
    Target.FactionId = 2;
    Target.AttackProfileId = ProfileId;
    Target.Position = FVector(Range, Pair * 5000.0, 0.0);
    Target.Facing = -FVector::ForwardVector;
  }
  TArray<FCrowdDemoAttackAgent> Reverse = Forward;
  Algo::Reverse(Reverse);
  TArray<FCrowdDemoAttackIntent> ForwardIntents;
  TArray<FCrowdDemoAttackIntent> ReverseIntents;
  FCrowdDemoAttackPlanSummary ForwardSummary;
  FCrowdDemoAttackPlanSummary ReverseSummary;
  TestTrue(TEXT("forward acquire"),
    FCrowdDemoAttackPlanner::Advance(
      9, 0, Profiles, Forward,
      ForwardIntents, ForwardSummary));
  TestTrue(TEXT("reverse acquire"),
    FCrowdDemoAttackPlanner::Advance(
      9, 0, Profiles, Reverse,
      ReverseIntents, ReverseSummary));
  TestTrue(TEXT("forward commit"),
    FCrowdDemoAttackPlanner::Advance(
      9, 1, Profiles, Forward,
      ForwardIntents, ForwardSummary));
  TestTrue(TEXT("reverse commit"),
    FCrowdDemoAttackPlanner::Advance(
      9, 1, Profiles, Reverse,
      ReverseIntents, ReverseSummary));
  TestEqual(TEXT("all six agents commit"), ForwardIntents.Num(), 6);
  TestEqual(TEXT("reverse intent count"),
    ReverseIntents.Num(), ForwardIntents.Num());
  for (int32 Index = 0;
    Index < ForwardIntents.Num(); ++Index)
  {
    TestEqual(TEXT("stable impact id"),
      ReverseIntents[Index].ImpactId,
      ForwardIntents[Index].ImpactId);
    TestEqual(TEXT("stable payload type"),
      ReverseIntents[Index].PayloadTypeId,
      ForwardIntents[Index].PayloadTypeId);
  }

  const FCrowdStableEntityRef DeadTarget = {1, 11, 1};
  for (FCrowdDemoAttackAgent& Agent : Forward)
  {
    if (Agent.EntityRef == DeadTarget)
    {
      Agent.Health = 0;
      Agent.bAlive = false;
    }
  }
  TestTrue(TEXT("dead target invalidates lock"),
    FCrowdDemoAttackPlanner::Advance(
      9, 2, Profiles, Forward,
      ForwardIntents, ForwardSummary));
  const FCrowdDemoAttackAgent* Source = Forward.FindByPredicate(
    [](const FCrowdDemoAttackAgent& Agent)
    {
      return Agent.EntityRef
        == FCrowdStableEntityRef{1, 1, 1};
    });
  TestNotNull(TEXT("melee source remains"), Source);
  if (Source)
  {
    TestTrue(TEXT("dead target reference cleared"),
      Source->State.TargetRef.IsUnset()
        && Source->State.LockedTargetRef.IsUnset());
  }
  TestTrue(TEXT("invalid lifecycle counted"),
    ForwardSummary.InvalidTargetLifecycleCount > 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBusinessModuleStructureTest,
  "CrowdDemo.BusinessPlanner.ModuleStructure",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBusinessModuleStructureTest::RunTest(
  const FString& Parameters)
{
  FString BuildRules;
  FString Mixed;
  FString Friendly;
  FString ProjectileAdapter;
  const FString ProjectDir = FPaths::ProjectDir();
  TestTrue(TEXT("business Build.cs readable"),
    FFileHelper::LoadFileToString(
      BuildRules,
      *FPaths::Combine(ProjectDir,
        TEXT("Source/MassCrowdDemoBusiness/"
          "MassCrowdDemoBusiness.Build.cs"))));
  TestTrue(TEXT("mixed coordinator readable"),
    FFileHelper::LoadFileToString(
      Mixed,
      *FPaths::Combine(ProjectDir,
        TEXT("Source/MassAICrowdDemo/"
          "CrowdDemoMixedSandboxCoordinator.cpp"))));
  TestTrue(TEXT("friendly coordinator readable"),
    FFileHelper::LoadFileToString(
      Friendly,
      *FPaths::Combine(ProjectDir,
        TEXT("Source/MassAICrowdDemo/"
          "CrowdDemoFriendlyLogisticsCoordinator.cpp"))));
  TestTrue(TEXT("projectile adapter readable"),
    FFileHelper::LoadFileToString(
      ProjectileAdapter,
      *FPaths::Combine(ProjectDir,
        TEXT("Source/MassAICrowdDemo/Mass/"
          "CrowdDemoProjectileAdapters.cpp"))));

  TestTrue(TEXT("business module owns only allowed dependencies"),
    BuildRules.Contains(TEXT("\"Core\""))
      && BuildRules.Contains(TEXT("\"MassCrowdCore\""))
      && BuildRules.Contains(TEXT("\"MassCrowdRuntime\""))
      && BuildRules.Contains(TEXT("\"MassCrowdStandardSources\""))
      && !BuildRules.Contains(TEXT("\"Engine\""))
      && !BuildRules.Contains(TEXT("\"MassEntity\""))
      && !BuildRules.Contains(TEXT("\"MassCrowdNetworking\""))
      && !BuildRules.Contains(TEXT("\"MassCrowdSpatial\""))
      && !BuildRules.Contains(TEXT("\"MassCrowdCombat\""))
      && !BuildRules.Contains(TEXT("\"MassCrowdProjectiles\""))
      && !BuildRules.Contains(TEXT("\"MassAICrowdDemo\"")));
  TestTrue(TEXT("coordinators share planning runtime host"),
    Mixed.Contains(TEXT("FCrowdDemoPlanningRuntimeHost::Stage"))
      && Friendly.Contains(TEXT(
        "FCrowdDemoPlanningRuntimeHost::Stage")));
  TestFalse(TEXT("mixed coordinator contains no role or source dispatch"),
    Mixed.Contains(TEXT("CohortRole"))
      || Mixed.Contains(TEXT("SourceTypeId"))
      || Mixed.Contains(TEXT("EvaluateSlotBehavior"))
      || Mixed.Contains(TEXT("ApplyPlannerDecision(")));
  TestFalse(TEXT("projectile adapter no longer advances attack phases"),
    ProjectileAdapter.Contains(TEXT("AdvanceAttackPhases")));
  TestFalse(TEXT("Runtime migration domain API is absent"),
    FPaths::FileExists(*FPaths::Combine(
      ProjectDir,
      TEXT("Plugins/MassCrowdSimulation/Source/"
        "MassCrowdRuntime/Public/"
        "MassCrowdRuntimeBehavior.h"))));
  return true;
}

#endif
