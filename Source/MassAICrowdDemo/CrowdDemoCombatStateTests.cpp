#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoWorkerCombatExtension.h"
#include "MassCrowdWorkerCombatState.h"
#include "MassEntityTemplate.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdDemoCombatAgentState MakeAgent(const int32 AgentId)
  {
    FCrowdDemoCombatAgentState Agent;
    Agent.AgentId = AgentId;
    Agent.LifecycleSerial = 1;
    Agent.VisualPhaseSeed = static_cast<uint32>(AgentId);
    return Agent;
  }

  FCrowdDemoHitFact MakeHit(
    const uint64 EventId,
    const int32 TargetId,
    const float Damage,
    const float Horizontal,
    const float Vertical)
  {
    FCrowdDemoHitFact Hit;
    Hit.HitEventId = EventId;
    Hit.ApplyFixedStep = 10;
    Hit.SourceAgentId = 99;
    Hit.SourceLifecycleSerial = 1;
    Hit.TargetAgentId = TargetId;
    Hit.TargetLifecycleSerial = 1;
    Hit.HitPosition = FVector(100.0f, 0.0f, 60.0f);
    Hit.HitDirection = FVector::ForwardVector;
    Hit.Damage = Damage;
    Hit.HorizontalImpulseCmps = Horizontal;
    Hit.VerticalImpulseCmps = Vertical;
    Hit.HitFlashProfileKey = 7;
    return Hit;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoWorkerCombatCodecRoundTripTest,
  "CrowdDemo.WorkerV2.WA5.CombatCodecRoundTrip",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoWorkerCombatCodecRoundTripTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoWorkerCombatHostInput Input;
  Input.RoundId = 7;
  Input.FixedStepIndex = 31;
  Input.PlanRevision = 4;
  Input.ServerTimeSeconds = 2.0f;
  Input.FixedStepSeconds = 1.0f / 30.0f;
  Input.AttackSettings.bEnabled = 1;
  FCrowdDemoRangedCombatAgent& Agent =
    Input.Agents.AddDefaulted_GetRef();
  Agent.EntityRef = {0x44454d4fu, 11, 2};
  Agent.AgentId = 11;
  Agent.LifecycleSerial = 2;
  Agent.FormationIndex = 3;
  Agent.FactionId = 1;
  Agent.NavLayer = 1;
  Agent.Position = FVector(100.0f, 200.0f, 60.0f);
  Agent.RadiusCm = 42.0f;
  Agent.Combat = MakeAgent(11);
  Agent.Combat.LifecycleSerial = 2;
  Agent.Combat.AttackPhase = ECrowdDemoAttackPhase::Cooldown;
  Agent.Combat.CooldownEndFixedStep = 40;

  FCrowdWorkerPayload InputPayload;
  FCrowdDemoWorkerCombatHostInput DecodedInput;
  TestTrue(TEXT("host input encodes"),
    FCrowdDemoWorkerCombatHostInputCodec::Encode(
      Input, InputPayload));
  TestTrue(TEXT("host input decodes"),
    FCrowdDemoWorkerCombatHostInputCodec::Decode(
      InputPayload, DecodedInput));
  TestEqual(TEXT("cooldown survives codec"),
    DecodedInput.Agents[0].Combat.CooldownEndFixedStep, 40);
  TestEqual(TEXT("stable ref lifecycle survives codec"),
    DecodedInput.Agents[0].EntityRef.LifecycleSerial, 2u);

  FCrowdDemoWorkerCombatHostResult Result;
  Result.FixedStepIndex = Input.FixedStepIndex;
  Result.AttackSummary.bValid = true;
  Result.AttackSummary.TargetAcquiredCount = 1;
  Result.AttackSummary.AttackStateHash = 1234;
  Result.HitSummary.bValid = true;
  Result.HitSummary.AppliedHitCount = 1;
  Result.HitSummary.DeathCount = 1;
  Result.HitSummary.StableHash = 5678;
  FCrowdWorkerPayload ResultPayload;
  FCrowdDemoWorkerCombatHostResult DecodedResult;
  TestTrue(TEXT("host result encodes"),
    FCrowdDemoWorkerCombatHostResultCodec::Encode(
      Result, ResultPayload));
  TestTrue(TEXT("host result decodes"),
    FCrowdDemoWorkerCombatHostResultCodec::Decode(
      ResultPayload, DecodedResult));
  TestEqual(TEXT("attack summary survives codec"),
    DecodedResult.AttackSummary.AttackStateHash, 1234u);
  TestEqual(TEXT("death summary survives codec"),
    DecodedResult.HitSummary.DeathCount, 1);

  FCrowdWorkerCombatState ZeroImpulseReactive;
  ZeroImpulseReactive.SourceFixedStep = Input.FixedStepIndex;
  ZeroImpulseReactive.bAlive = true;
  ZeroImpulseReactive.bReactiveActive = true;
  ZeroImpulseReactive.HostState = ResultPayload;
  TestTrue(TEXT("zero-impulse timed HitReact is valid"),
    ZeroImpulseReactive.IsValid());
  FCrowdWorkerPayload CombatPayload;
  FCrowdWorkerCombatState DecodedCombat;
  TestTrue(TEXT("generic combat state encodes"),
    FCrowdWorkerCombatStateCodec::Encode(
      ZeroImpulseReactive, CombatPayload));
  TestTrue(TEXT("generic combat state decodes"),
    FCrowdWorkerCombatStateCodec::Decode(
      CombatPayload, DecodedCombat));
  TestTrue(TEXT("reactive flag survives generic codec"),
    DecodedCombat.bReactiveActive);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoWorkerMixedCombatDomainTest,
  "CrowdDemo.WorkerV2.WA5.MixedCombatDomain",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoWorkerMixedCombatDomainTest::RunTest(
  const FString& Parameters)
{
  constexpr uint64 Generation = 43;
  const FCrowdStableEntityRef AgentA{1, 1, 1};
  const FCrowdStableEntityRef AgentB{1, 2, 1};
  FCrowdDemoWorkerMixedCombatHostInput HostInput;
  HostInput.FixedStepSeconds = 1.0f / 30.0f;
  FCrowdDemoAttackProfileV1& Melee =
    HostInput.Profiles.AddDefaulted_GetRef();
  Melee.ProfileId = CrowdDemoAttackProfileIds::Melee;
  Melee.PayloadTypeId = CrowdDemoAttackPayloadTypeIds::Melee;
  Melee.EffectProfileId = 1;
  Melee.Archetype = ECrowdDemoAttackArchetype::Melee;
  Melee.WindupFixedSteps = 2;
  Melee.RecoveryFixedSteps = 2;
  Melee.CooldownFixedSteps = 3;
  Melee.MaximumDistanceCm = 300.0f;
  Melee.QueryRadiusCm = 80.0f;
  Melee.MuzzleForwardOffsetCm = 42.0f;
  Melee.Damage = 20;
  FCrowdDemoWorkerMixedCombatAgent& A =
    HostInput.Agents.AddDefaulted_GetRef();
  A.EntityRef = AgentA;
  A.FactionId = 1;
  A.AttackProfileId = Melee.ProfileId;
  A.Position = FVector::ZeroVector;
  A.Facing = FVector::ForwardVector;
  A.Health = 100;
  FCrowdDemoWorkerMixedCombatAgent& B =
    HostInput.Agents.AddDefaulted_GetRef();
  B.EntityRef = AgentB;
  B.FactionId = 2;
  B.AttackProfileId = Melee.ProfileId;
  B.Position = FVector(100.0f, 0.0f, 0.0f);
  B.Facing = -FVector::ForwardVector;
  B.Health = 100;

  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("mixed combat resources reset"),
    Resources.Reset(
      FCrowdWorkerProjectileControlResourceCodec::
        MaxEncodedBytes));
  FCrowdWorkerEntityStateStore EntityStates;
  TestTrue(TEXT("mixed combat entity store resets"),
    EntityStates.Reset(8, 1024 * 1024));
  FCrowdWorkerProjectileDomainExecutor Executor(
    MakeCrowdDemoWorkerCombatExtension());
  FCrowdWorkerDomainContext Context;
  Context.Generation = Generation;
  Context.FixedDeltaSeconds = HostInput.FixedStepSeconds;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.EntityStates = &EntityStates;
  Context.Resources = &Resources;
  FCrowdWorkerWorkItem Work;
  Work.Key.Domain = ECrowdWorkerDomainId::CombatReactive;
  Work.Key.Kind = ECrowdWorkerWorkKind::Resource;
  Work.Key.ScopeKey = CrowdWorkerResourceIds::ProjectileControl;
  TArray<FCrowdWorkerWorkItem> WorkItems{Work};
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  uint64 NextEventSequence = 1;
  int32 TotalMeleeIntents = 0;
  int32 TotalDamage = 0;
  int32 PublishedCombatStates = 0;
  FCrowdWorkerProjectileState LatestProjectileState;
  for (int32 Step = 0; Step < 20; ++Step)
  {
    HostInput.FixedStepIndex = Step;
    FCrowdWorkerProjectileControlResource Control;
    Control.Revision = static_cast<uint64>(Step + 1);
    Control.AnchorEntity = AgentA;
    Control.bReplaceState = Step == 0;
    Control.Input.FixedStepIndex = Step;
    Control.Input.ServerTimeSeconds =
      Step * HostInput.FixedStepSeconds;
    Control.Input.FixedStepSeconds =
      HostInput.FixedStepSeconds;
    FCrowdProjectileProfile& ProjectileProfile =
      Control.Input.Profiles.AddDefaulted_GetRef();
    ProjectileProfile.ProfileId =
      CrowdDemoProjectileSchemas::ProjectileProfileId;
    ProjectileProfile.RadiusCm = 12.0f;
    ProjectileProfile.LifetimeFixedSteps = 60;
    ProjectileProfile.MaxActiveProjectiles = 32;
    ProjectileProfile.RecalculateStableHash();
    FCrowdDemoRangedCombatSettings DamageSettings;
    DamageSettings.bEnabled = 1;
    DamageSettings.Damage = 20.0f;
    Control.EffectProfiles.Add(
      FCrowdDemoProjectileAdapters::BuildEffectProfile(
        DamageSettings));
    TestTrue(TEXT("mixed host input encodes"),
      FCrowdDemoWorkerMixedCombatHostInputCodec::Encode(
        HostInput, Control.HostCombatInput));
    FCrowdWorkerPayload ControlPayload;
    TestTrue(TEXT("mixed control encodes"),
      FCrowdWorkerProjectileControlResourceCodec::Encode(
        Control, ControlPayload));
    TestEqual(TEXT("mixed control stages"),
      Resources.StageBuilding({
        CrowdWorkerResourceIds::ProjectileControl,
        Control.Revision, MoveTemp(ControlPayload)}),
      ECrowdWorkerQueueResult::Added);
    TestTrue(TEXT("mixed control commits"),
      Resources.CommitBuildingAtEpoch(Step + 1, ResourceEvents));
    Context.WorkerEpoch = Step + 1;
    Context.AbsoluteSimulationTick = Step + 1;
    Context.LastAppliedInputSequence = Step + 1;
    Context.NextOrderedEventSequence = NextEventSequence;
    Context.SimulationTimeSeconds =
      (Step + 1) * HostInput.FixedStepSeconds;
    FCrowdWorkerDomainOutput Output;
    TestTrue(TEXT("mixed combat step executes"),
      Executor.Execute(Context, WorkItems, Output));
    if (Output.DirtyStates.IsEmpty()) return false;
    const FCrowdWorkerDirtyStateRecord* Projectile =
      Output.DirtyStates.FindByPredicate([](const auto& Dirty)
      {
        return Dirty.Field == ECrowdWorkerField::Projectile;
      });
    FCrowdWorkerProjectileState ProjectileState;
    FCrowdDemoWorkerMixedCombatHostResult Result;
    TestTrue(TEXT("mixed projectile state decodes"),
      Projectile
        && FCrowdWorkerProjectileStateCodec::Decode(
          Projectile->Payload, ProjectileState));
    TestTrue(TEXT("mixed host result decodes"),
      Projectile
        && FCrowdDemoWorkerMixedCombatHostResultCodec::Decode(
          ProjectileState.HostCombatResult, Result));
    LatestProjectileState = ProjectileState;
    TotalMeleeIntents += Result.MeleeIntentCount;
    TotalDamage += Result.AppliedDamageCount;
    for (const auto& Dirty : Output.DirtyStates)
    {
      if (Dirty.Field != ECrowdWorkerField::Combat) continue;
      FCrowdWorkerCombatState Combat;
      FCrowdDemoWorkerMixedCombatState Mixed;
      TestTrue(TEXT("mixed generic combat decodes"),
        FCrowdWorkerCombatStateCodec::Decode(
          Dirty.Payload, Combat));
      TestTrue(TEXT("mixed host combat decodes"),
        FCrowdDemoWorkerMixedCombatStateCodec::Decode(
          Combat.HostState, Mixed));
      FCrowdDemoWorkerMixedCombatAgent* CheckpointAgent =
        HostInput.Agents.FindByPredicate(
          [&Dirty](const auto& Agent)
          {
            return Agent.EntityRef == Dirty.EntityRef;
          });
      TestNotNull(TEXT("mixed checkpoint agent exists"),
        CheckpointAgent);
      if (!CheckpointAgent) return false;
      CheckpointAgent->Health = Mixed.Health;
      CheckpointAgent->AttackState = Mixed.AttackState;
      ++PublishedCombatStates;
    }
    if (!Output.OrderedEvents.IsEmpty())
    {
      for (int32 Index = 0; Index < Output.OrderedEvents.Num();
        ++Index)
      {
        TestEqual(TEXT("mixed events stay contiguous"),
          Output.OrderedEvents[Index].EventSequence,
          NextEventSequence + static_cast<uint64>(Index));
      }
      NextEventSequence += Output.OrderedEvents.Num();
    }
  }
  TestTrue(TEXT("mixed Worker emits melee intents"),
    TotalMeleeIntents > 0);
  TestTrue(TEXT("mixed Worker applies damage"),
    TotalDamage > 0);
  TestEqual(TEXT("mixed Worker publishes every combat state"),
    PublishedCombatStates, 40);

  // Restore a new pure-C++ executor from the complete Demo combat state,
  // active projectile checkpoint and ordered-event baseline, then require the
  // next atomic domain result to be byte-identical to the uninterrupted path.
  HostInput.FixedStepIndex = 20;
  TArray<FCrowdProjectileState> ActiveCheckpointProjectiles =
    LatestProjectileState.Prepared.States;
  ActiveCheckpointProjectiles.RemoveAll(
    [](const FCrowdProjectileState& State)
    {
      return !State.bActive;
    });
  const auto BuildNextControl =
    [&HostInput, &AgentA, &ActiveCheckpointProjectiles](
      const bool bReplaceState,
      FCrowdWorkerProjectileControlResource& OutControl)
  {
    OutControl = {};
    OutControl.Revision = 21;
    OutControl.AnchorEntity = AgentA;
    OutControl.bReplaceState = bReplaceState;
    OutControl.Input.FixedStepIndex = 20;
    OutControl.Input.ServerTimeSeconds =
      20 * HostInput.FixedStepSeconds;
    OutControl.Input.FixedStepSeconds =
      HostInput.FixedStepSeconds;
    if (bReplaceState)
      OutControl.Input.CurrentStates =
        ActiveCheckpointProjectiles;
    FCrowdProjectileProfile& ProjectileProfile =
      OutControl.Input.Profiles.AddDefaulted_GetRef();
    ProjectileProfile.ProfileId =
      CrowdDemoProjectileSchemas::ProjectileProfileId;
    ProjectileProfile.RadiusCm = 12.0f;
    ProjectileProfile.LifetimeFixedSteps = 60;
    ProjectileProfile.MaxActiveProjectiles = 32;
    ProjectileProfile.RecalculateStableHash();
    FCrowdDemoRangedCombatSettings DamageSettings;
    DamageSettings.bEnabled = 1;
    DamageSettings.Damage = 20.0f;
    OutControl.EffectProfiles.Add(
      FCrowdDemoProjectileAdapters::BuildEffectProfile(
        DamageSettings));
    return FCrowdDemoWorkerMixedCombatHostInputCodec::Encode(
      HostInput, OutControl.HostCombatInput)
      && OutControl.IsValid();
  };
  const auto CommitControl =
    [this](FCrowdWorkerResourceStore& Store,
      FCrowdWorkerProjectileControlResource& Control,
      const uint64 Epoch)
  {
    FCrowdWorkerPayload Payload;
    TArray<FCrowdWorkerResourceRevisionEvent> Events;
    if (!FCrowdWorkerProjectileControlResourceCodec::Encode(
          Control, Payload))
      return false;
    if (Store.StageBuilding({
          CrowdWorkerResourceIds::ProjectileControl,
          Control.Revision, MoveTemp(Payload)})
        != ECrowdWorkerQueueResult::Added)
      return false;
    return Store.CommitBuildingAtEpoch(Epoch, Events);
  };

  FCrowdWorkerProjectileControlResource ContinuedControl;
  TestTrue(TEXT("continued control builds"),
    BuildNextControl(false, ContinuedControl));
  TestTrue(TEXT("continued control commits"),
    CommitControl(Resources, ContinuedControl, 21));
  Context.WorkerEpoch = 21;
  Context.AbsoluteSimulationTick = 21;
  Context.LastAppliedInputSequence = 21;
  Context.NextOrderedEventSequence = NextEventSequence;
  Context.SimulationTimeSeconds =
    21 * HostInput.FixedStepSeconds;
  FCrowdWorkerDomainOutput ContinuedOutput;
  TestTrue(TEXT("continued mixed step executes"),
    Executor.Execute(Context, WorkItems, ContinuedOutput));

  FCrowdWorkerResourceStore ReplayResources;
  TestTrue(TEXT("replay resources reset"),
    ReplayResources.Reset(
      FCrowdWorkerProjectileControlResourceCodec::
        MaxEncodedBytes));
  FCrowdWorkerProjectileDomainExecutor ReplayExecutor(
    MakeCrowdDemoWorkerCombatExtension());
  FCrowdWorkerProjectileControlResource ReplayControl;
  TestTrue(TEXT("replay control builds"),
    BuildNextControl(true, ReplayControl));
  TestTrue(TEXT("replay control commits"),
    CommitControl(ReplayResources, ReplayControl, 21));
  FCrowdWorkerDomainContext ReplayContext = Context;
  ReplayContext.Resources = &ReplayResources;
  FCrowdWorkerDomainOutput ReplayOutput;
  TestTrue(TEXT("restored mixed step executes"),
    ReplayExecutor.Execute(
      ReplayContext, WorkItems, ReplayOutput));

  const auto SortDirty = [](TArray<FCrowdWorkerDirtyStateRecord>& States)
  {
    States.Sort([](const auto& A, const auto& B)
    {
      if (A.EntityRef != B.EntityRef)
        return A.EntityRef < B.EntityRef;
      return static_cast<uint8>(A.Field)
        < static_cast<uint8>(B.Field);
    });
  };
  SortDirty(ContinuedOutput.DirtyStates);
  SortDirty(ReplayOutput.DirtyStates);
  TestEqual(TEXT("checkpoint replay dirty count"),
    ReplayOutput.DirtyStates.Num(),
    ContinuedOutput.DirtyStates.Num());
  for (int32 Index = 0;
    Index < ContinuedOutput.DirtyStates.Num()
      && Index < ReplayOutput.DirtyStates.Num();
    ++Index)
  {
    const auto& Expected = ContinuedOutput.DirtyStates[Index];
    const auto& Actual = ReplayOutput.DirtyStates[Index];
    TestEqual(TEXT("checkpoint replay entity"),
      Actual.EntityRef, Expected.EntityRef);
    TestEqual(TEXT("checkpoint replay field"),
      static_cast<uint8>(Actual.Field),
      static_cast<uint8>(Expected.Field));
    TestEqual(TEXT("checkpoint replay revision"),
      Actual.StateRevision, Expected.StateRevision);
    TestTrue(TEXT("checkpoint replay payload bytes"),
      Actual.Payload == Expected.Payload);
  }
  TestEqual(TEXT("checkpoint replay event count"),
    ReplayOutput.OrderedEvents.Num(),
    ContinuedOutput.OrderedEvents.Num());
  for (int32 Index = 0;
    Index < ContinuedOutput.OrderedEvents.Num()
      && Index < ReplayOutput.OrderedEvents.Num();
    ++Index)
  {
    const auto& Expected = ContinuedOutput.OrderedEvents[Index];
    const auto& Actual = ReplayOutput.OrderedEvents[Index];
    TestEqual(TEXT("checkpoint replay event sequence"),
      Actual.EventSequence, Expected.EventSequence);
    TestEqual(TEXT("checkpoint replay event id"),
      Actual.EventId, Expected.EventId);
    TestTrue(TEXT("checkpoint replay event payload"),
      Actual.Payload == Expected.Payload);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatHitFactDeterminismTest,
  "CrowdDemo.Combat.T7.HitFactDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatHitFactDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoCombatAgentState> Agents = {MakeAgent(1), MakeAgent(2)};
  TArray<FCrowdDemoHitFact> Hits = {
    MakeHit(102, 2, 15.0f, 100.0f, 0.0f),
    MakeHit(101, 1, 20.0f, 200.0f, 300.0f)};
  FCrowdDemoHitResponseSettings Settings;
  FCrowdDemoHitResponseSummary Summary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(10, 1.0f, Hits, Settings, Agents, Summary);
  TestTrue(TEXT("hit facts valid"), Summary.bValid);
  TestEqual(TEXT("applied hits"), Summary.AppliedHitCount, 2);
  TestEqual(TEXT("agent 1 health"), Agents[0].Health, 80.0f);
  TestEqual(TEXT("agent 2 health"), Agents[1].Health, 85.0f);
  TestEqual(TEXT("agent 1 reactive mode"), Agents[0].ReactiveMode,
    ECrowdDemoReactiveMotionMode::KnockUp);
  const uint32 Hash = Summary.StableHash;

  TArray<FCrowdDemoCombatAgentState> ReversedAgents = {MakeAgent(2), MakeAgent(1)};
  Algo::Reverse(Hits);
  FCrowdDemoHitResponseSummary ReversedSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    10, 1.0f, Hits, Settings, ReversedAgents, ReversedSummary);
  TestTrue(TEXT("reversed valid"), ReversedSummary.bValid);
  TestEqual(TEXT("reversed hash"), ReversedSummary.StableHash, Hash);
  TestEqual(TEXT("reversed agent hash"),
    FCrowdDemoCombatStateKernel::HashAgents(ReversedAgents),
    FCrowdDemoCombatStateKernel::HashAgents(Agents));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatHitDedupLifecycleDeathTest,
  "CrowdDemo.Combat.T7.HitDedupLifecycleDeath",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatHitDedupLifecycleDeathTest::RunTest(const FString& Parameters)
{
  FCrowdDemoHitResponseSettings Settings;
  TArray<FCrowdDemoCombatAgentState> Agents = {MakeAgent(1)};
  TArray<FCrowdDemoHitFact> Hits = {
    MakeHit(100, 1, 25.0f, 100.0f, 0.0f),
    MakeHit(100, 1, 25.0f, 100.0f, 0.0f)};
  FCrowdDemoHitResponseSummary Summary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(10, 1.0f, Hits, Settings, Agents, Summary);
  TestTrue(TEXT("dedup valid"), Summary.bValid);
  TestEqual(TEXT("duplicate counted"), Summary.DuplicateHitCount, 1);
  TestEqual(TEXT("damage once"), Agents[0].Health, 75.0f);

  FCrowdDemoHitResponseSummary ReplaySummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(10, 1.0f, Hits, Settings, Agents, ReplaySummary);
  TestEqual(TEXT("replay no damage"), Agents[0].Health, 75.0f);
  TestEqual(TEXT("replay applied none"), ReplaySummary.AppliedHitCount, 0);

  FCrowdDemoHitFact Stale = MakeHit(101, 1, 10.0f, 0.0f, 0.0f);
  Stale.TargetLifecycleSerial = 2;
  FCrowdDemoHitResponseSummary StaleSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    10, 1.0f, MakeArrayView(&Stale, 1), Settings, Agents, StaleSummary);
  TestEqual(TEXT("stale lifecycle"), StaleSummary.StaleLifecycleCount, 1);
  TestEqual(TEXT("stale no damage"), Agents[0].Health, 75.0f);

  FCrowdDemoHitFact Lethal = MakeHit(102, 1, 100.0f, 0.0f, 0.0f);
  FCrowdDemoHitResponseSummary DeathSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    10, 1.0f, MakeArrayView(&Lethal, 1), Settings, Agents, DeathSummary);
  TestEqual(TEXT("death counted"), DeathSummary.DeathCount, 1);
  TestFalse(TEXT("dead not alive"), Agents[0].bAlive);
  TestEqual(TEXT("dead lifecycle"), Agents[0].LifecycleState, ECrowdDemoLifecycleState::Dead);
  TestEqual(TEXT("death visual"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(Agents[0], FVector::ZeroVector),
    ECrowdDemoVisualState::Death);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatBallisticVisualTest,
  "CrowdDemo.Combat.T7.BallisticAndVisual",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatBallisticVisualTest::RunTest(const FString& Parameters)
{
  FCrowdDemoHitResponseSettings Settings;
  FCrowdDemoCombatAgentState Agent = MakeAgent(1);
  Agent.BusinessState = ECrowdDemoBusinessState::Attacking;
  Agent.AttackPhase = ECrowdDemoAttackPhase::Windup;
  Agent.VisualState = ECrowdDemoVisualState::Attack;
  Agent.ReactiveMode = ECrowdDemoReactiveMotionMode::KnockUp;
  Agent.VerticalReactiveVelocityCmps = 500.0f;
  Agent.HorizontalReactiveVelocity = FVector(100.0f, 0.0f, 0.0f);
  Agent.RestoreBusinessState = ECrowdDemoBusinessState::Attacking;
  float Z = Settings.GroundZ;
  int32 ApexEvents = 0;
  int32 LandingEvents = 0;
  for (int32 Step = 10; Step < 200 && Agent.ReactiveMode != ECrowdDemoReactiveMotionMode::LandingRecovery; ++Step)
  {
    const auto Result = FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(Step, Z, Settings, Agent);
    TestTrue(TEXT("ballistic step valid"), Result.bValid);
    Z = Result.NewZ;
    ApexEvents += Result.bReachedApex ? 1 : 0;
    LandingEvents += Result.bLanded ? 1 : 0;
  }
  TestEqual(TEXT("single apex"), ApexEvents, 1);
  TestEqual(TEXT("single landing"), LandingEvents, 1);
  TestEqual(TEXT("stored apex"), Agent.ApexCount, 1);
  TestEqual(TEXT("stored landing"), Agent.LandingCount, 1);
  TestEqual(TEXT("landed on ground"), Z, Settings.GroundZ);

  Agent.HitFlashRevision = 1;
  Agent.HitFlashStartServerTimeSeconds = 1.0f;
  Agent.ReactiveMode = ECrowdDemoReactiveMotionMode::None;
  Agent.BusinessState = ECrowdDemoBusinessState::Attacking;
  Agent.AttackPhase = ECrowdDemoAttackPhase::Windup;
  TestEqual(TEXT("flash does not interrupt attack"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(Agent, FVector::ZeroVector),
    ECrowdDemoVisualState::Attack);
  FCrowdDemoCombatStateKernel::ResolveVisualStateBoundary(
    50, 2.0f, FVector::ZeroVector, Agent);
  TestEqual(TEXT("attack visual stored"), Agent.VisualState, ECrowdDemoVisualState::Attack);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatVatShowcaseMotionTest,
  "CrowdDemo.Combat.T7.ShowcaseMotion",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatVatShowcaseMotionTest::RunTest(const FString& Parameters)
{
  const FVector Anchor(100.0f, -200.0f, 60.0f);
  const auto Idle = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    0, 0, Anchor, Anchor);
  TestTrue(TEXT("idle valid"), Idle.bValid);
  TestFalse(TEXT("idle not moving group"), Idle.bMovingGroup);
  TestTrue(TEXT("idle velocity zero"), Idle.DesiredVelocity.IsNearlyZero());

  const auto MovingStart = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    4, 0, Anchor, Anchor);
  TestTrue(TEXT("moving valid"), MovingStart.bValid);
  TestTrue(TEXT("moving group"), MovingStart.bMovingGroup);
  TestEqual(TEXT("moving positive speed"), MovingStart.DesiredVelocity.X, 60.0);
  TestEqual(TEXT("moving bounded target"), MovingStart.DesiredLocation.X, 112.0);

  const auto MovingReturn = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    7, 6, Anchor + FVector(12.0f, 0.0f, 0.0f), Anchor);
  TestEqual(TEXT("moving return speed"), MovingReturn.DesiredVelocity.X, -60.0);
  TestEqual(TEXT("moving return target"), MovingReturn.DesiredLocation.X, 88.0);
  TestEqual(TEXT("moving deterministic hash"),
    FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
      7, 6, Anchor + FVector(12.0f, 0.0f, 0.0f), Anchor).StableHash,
    MovingReturn.StableHash);

  const auto Attack = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    8, 0, Anchor, Anchor);
  TestFalse(TEXT("attack not locomotion group"), Attack.bMovingGroup);
  TestTrue(TEXT("attack base velocity zero"), Attack.DesiredVelocity.IsNearlyZero());

  FCrowdDemoCombatAgentState ExplicitIdle = MakeAgent(20);
  ExplicitIdle.BusinessState = ECrowdDemoBusinessState::Idle;
  TestEqual(TEXT("generic velocity resolver remains velocity driven"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(
      ExplicitIdle, FVector(60.0f, 0.0f, 0.0f)),
    ECrowdDemoVisualState::Move);
  TestEqual(TEXT("showcase idle ignores particle drift"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(
      ExplicitIdle, FVector(60.0f, 0.0f, 0.0f), true),
    ECrowdDemoVisualState::Idle);
  ExplicitIdle.BusinessState = ECrowdDemoBusinessState::Moving;
  TestEqual(TEXT("showcase moving survives zero velocity"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(
      ExplicitIdle, FVector::ZeroVector, true),
    ECrowdDemoVisualState::Move);

  FCrowdDemoVatShowcaseMotionSettings InvalidSettings;
  InvalidSettings.HalfCycleFixedSteps = 0;
  TestFalse(TEXT("invalid settings rejected"),
    FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
      4, 0, Anchor, Anchor, InvalidSettings).bValid);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatRollbackCompletionGateTest,
  "CrowdDemo.Combat.Rollback.CompletionGate",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatRollbackCompletionGateTest::RunTest(
  const FString& Parameters)
{
  auto* Pipeline = NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  FCrowdDemoRoundPlanPacket Plan;
  Plan.bValid = 1;
  Plan.RoundId = 1;
  Plan.Revision = 1;
  Plan.Rules.Scenario = ECrowdDemoScenario::SimRoundSoftPressure;
  Plan.Rules.FixedStepSeconds = 1.0f / 30.0f;
  Pipeline->ActivatePlan(Plan, 2, false);

  FCrowdMassBoundarySnapshot Boundary;
  Boundary.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  Boundary.PlanRevision = Pipeline->GetCurrentPlanRevision();
  Boundary.bValid = true;
  TArray<FCrowdDemoRoundBoundaryFormationFact> FormationFacts;
  TArray<FCrowdDemoRoundBoundaryFacingFact> FacingFacts;
  for (const int32 AgentId : {10, 20})
  {
    FCrowdMassBoundaryAgentRecord& Agent = Boundary.Agents.AddDefaulted_GetRef();
    Agent.Identity.AgentId = AgentId;
    Agent.Identity.LifecycleSerial = 1;
    FCrowdDemoRoundBoundaryFormationFact& Formation =
      FormationFacts.AddDefaulted_GetRef();
    Formation.AgentId = AgentId;
    FCrowdDemoRoundBoundaryFacingFact& Facing =
      FacingFacts.AddDefaulted_GetRef();
    Facing.AgentId = AgentId;
  }
  TestTrue(TEXT("boundary snapshot accepted"),
    Pipeline->PublishBoundarySnapshot(
      MoveTemp(Boundary), MoveTemp(FormationFacts), MoveTemp(FacingFacts)));

  TArray<FCrowdDemoSoftPressureRollbackAgentState> MovementFacts;
  for (const int32 AgentId : {10, 20})
  {
    FCrowdDemoSoftPressureRollbackAgentState& Agent =
      MovementFacts.AddDefaulted_GetRef();
    Agent.AgentId = AgentId;
    Agent.LifecycleSerial = 1;
  }
  const int32 Step = Pipeline->GetCurrentFixedStepIndex();
  Pipeline->RecordSoftPressureRollbackSnapshot(Step, MoveTemp(MovementFacts));
  const FCrowdDemoSoftPressureRollbackSnapshot* Snapshot =
    Pipeline->FindSoftPressureRollbackSnapshot(Step);
  TestNotNull(TEXT("movement snapshot exists"), Snapshot);
  TestTrue(TEXT("movement facts complete"),
    Snapshot && Snapshot->bMovementFactsComplete);
  TestFalse(TEXT("incomplete snapshot is not replayable"),
    Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(Step));

  TArray<FCrowdDemoPreparedCombatRollbackFact> MissingFacts;
  MissingFacts.AddDefaulted_GetRef().AgentId = 10;
  TestFalse(TEXT("missing combat fact rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, MissingFacts));
  TArray<FCrowdDemoPreparedCombatRollbackFact> DuplicateFacts;
  DuplicateFacts.AddDefaulted_GetRef().AgentId = 10;
  DuplicateFacts.AddDefaulted_GetRef().AgentId = 10;
  TestFalse(TEXT("duplicate combat fact rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, DuplicateFacts));
  TArray<FCrowdDemoPreparedCombatRollbackFact> WrongFacts;
  WrongFacts.AddDefaulted_GetRef().AgentId = 10;
  WrongFacts.AddDefaulted_GetRef().AgentId = 30;
  TestFalse(TEXT("wrong combat AgentId rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, WrongFacts));

  TArray<FCrowdDemoPreparedCombatRollbackFact> CombatFacts;
  FCrowdDemoPreparedCombatRollbackFact& Agent20 = CombatFacts.AddDefaulted_GetRef();
  Agent20.AgentId = 20;
  Agent20.Combat.Health = 80.0f;
  Agent20.Combat.VisualState = ECrowdDemoVisualState::HitReact;
  FCrowdDemoPreparedCombatRollbackFact& Agent10 = CombatFacts.AddDefaulted_GetRef();
  Agent10.AgentId = 10;
  Agent10.Combat.Health = 90.0f;
  Agent10.Combat.VisualState = ECrowdDemoVisualState::Attack;
  TestTrue(TEXT("reverse-order final combat facts accepted"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, CombatFacts));
  TestTrue(TEXT("complete snapshot is replayable"),
    Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(Step));
  Snapshot = Pipeline->FindSoftPressureRollbackSnapshot(Step);
  TestEqual(TEXT("Agent 10 final visual state stored"),
    Snapshot->Agents[0].Combat.VisualState, ECrowdDemoVisualState::Attack);
  TestEqual(TEXT("Agent 20 final health stored"),
    Snapshot->Agents[1].Combat.Health, 80.0f);
  TestFalse(TEXT("duplicate completion rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, CombatFacts));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSf1CorrectionHistorySnapshotTest,
  "CrowdDemo.Networking.GenericCorrectionHistory.SF1Snapshot",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSf1CorrectionHistorySnapshotTest::RunTest(
  const FString& Parameters)
{
  auto* Pipeline = NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  FCrowdDemoRoundPlanPacket Plan;
  Plan.bValid = 1;
  Plan.RoundId = 1;
  Plan.Revision = 1;
  Plan.Rules.Scenario = ECrowdDemoScenario::SimRoundObstacle;
  Plan.Rules.FixedStepSeconds = 1.0f / 30.0f;
  Pipeline->ActivatePlan(Plan, 1, false);

  FCrowdMassBoundarySnapshot Boundary;
  Boundary.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  Boundary.PlanRevision = Pipeline->GetCurrentPlanRevision();
  Boundary.bValid = true;
  FCrowdMassBoundaryAgentRecord& BoundaryAgent =
    Boundary.Agents.AddDefaulted_GetRef();
  BoundaryAgent.Identity.AgentId = 7;
  BoundaryAgent.Identity.LifecycleSerial = 3;
  TArray<FCrowdDemoRoundBoundaryFormationFact> FormationFacts;
  FCrowdDemoRoundBoundaryFormationFact& Formation =
    FormationFacts.AddDefaulted_GetRef();
  Formation.AgentId = 7;
  Formation.RadiusCm = 42.0f;
  TArray<FCrowdDemoRoundBoundaryFacingFact> FacingFacts;
  FacingFacts.Add({7, 4});
  TestTrue(TEXT("SF1 boundary snapshot accepted"),
    Pipeline->PublishBoundarySnapshot(
      MoveTemp(Boundary), MoveTemp(FormationFacts), MoveTemp(FacingFacts)));

  TArray<FCrowdDemoSoftPressureRollbackAgentState> MovementFacts;
  FCrowdDemoSoftPressureRollbackAgentState& Agent =
    MovementFacts.AddDefaulted_GetRef();
  Agent.AgentId = 7;
  Agent.LifecycleSerial = 3;
  Agent.Location = FVector(100.0f, 200.0f, 0.0f);
  Agent.Velocity = FVector(300.0f, 0.0f, 0.0f);
  Agent.RadiusCm = 42.0f;
  const int32 Step = Pipeline->GetCurrentFixedStepIndex();
  Pipeline->RecordSoftPressureRollbackSnapshot(Step, MoveTemp(MovementFacts));

  TArray<FCrowdDemoPreparedCombatRollbackFact> CombatFacts;
  FCrowdDemoPreparedCombatRollbackFact& Combat =
    CombatFacts.AddDefaulted_GetRef();
  Combat.AgentId = 7;
  Combat.Combat.Health = 75.0f;
  TestTrue(TEXT("SF1 combat completion accepted"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, CombatFacts));
  TestTrue(TEXT("SF1 correction history is replay-ready"),
    Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(Step));
  const FCrowdDemoSoftPressureRollbackSnapshot* Snapshot =
    Pipeline->FindSoftPressureRollbackSnapshot(Step);
  TestNotNull(TEXT("SF1 correction history exists"), Snapshot);
  if (Snapshot)
  {
    TestEqual(TEXT("SF1 historical location retained"),
      Snapshot->Agents[0].Location, FVector(100.0f, 200.0f, 0.0f));
    TestEqual(TEXT("SF1 final combat fact retained"),
      Snapshot->Agents[0].Combat.Health, 75.0f);
  }

  Plan.RoundId = 2;
  Plan.Revision = 2;
  Pipeline->ActivatePlan(Plan, 1, false);
  TestNull(TEXT("new SF1 round clears correction history"),
    Pipeline->FindSoftPressureRollbackSnapshot(Step));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCapabilityArchetypeCompositionTest,
  "CrowdDemo.Architecture.CapabilityArchetypeComposition",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCapabilityArchetypeCompositionTest::RunTest(
  const FString& Parameters)
{
  const auto Build = [](const ECrowdDemoMassCapability Capabilities)
  {
    FMassEntityTemplateData TemplateData;
    BuildCrowdDemoAuthorityTemplateData(
      TemplateData, FMassEntityTemplateID(), Capabilities);
    return TemplateData;
  };
  const FMassEntityTemplateData Base = Build(
    ECrowdDemoMassCapability::Base);
  const FMassEntityTemplateData Target = Build(
    ECrowdDemoMassCapability::Base
      | ECrowdDemoMassCapability::Target);
  const FMassEntityTemplateData Combat = Build(
    ECrowdDemoMassCapability::Base
      | ECrowdDemoMassCapability::Combat);
  const FMassEntityTemplateData TargetCombat = Build(
    ECrowdDemoMassCapability::Base
      | ECrowdDemoMassCapability::Target
      | ECrowdDemoMassCapability::Combat);

  const auto TestBase = [this](
    const TCHAR* Name, const FMassEntityTemplateData& TemplateData)
  {
    TestTrue(FString::Printf(TEXT("%s has Base identity"), Name),
      TemplateData.HasFragment<FCrowdDemoMassIdentityFragment>());
    TestTrue(FString::Printf(TEXT("%s has Base movement"), Name),
      TemplateData.HasFragment<FCrowdDemoMassMovementFragment>());
    TestTrue(FString::Printf(TEXT("%s has Base visual"), Name),
      TemplateData.HasFragment<FCrowdDemoMassVisualFragment>());
  };
  TestBase(TEXT("Base"), Base);
  TestBase(TEXT("Target"), Target);
  TestBase(TEXT("Combat"), Combat);
  TestBase(TEXT("TargetCombat"), TargetCombat);

  TestFalse(TEXT("Base has no Target tag"),
    Base.HasTag<FCrowdDemoTargetCapabilityTag>());
  TestFalse(TEXT("Base has no Combat tag"),
    Base.HasTag<FCrowdDemoCombatCapabilityTag>());
  TestFalse(TEXT("Base has no Combat stats"),
    Base.HasFragment<FCrowdDemoMassStatsFragment>());
  TestTrue(TEXT("Target has Target tag"),
    Target.HasTag<FCrowdDemoTargetCapabilityTag>());
  TestFalse(TEXT("Target has no Combat tag"),
    Target.HasTag<FCrowdDemoCombatCapabilityTag>());
  TestFalse(TEXT("Target has no Combat stats"),
    Target.HasFragment<FCrowdDemoMassStatsFragment>());
  TestFalse(TEXT("Combat has no Target tag"),
    Combat.HasTag<FCrowdDemoTargetCapabilityTag>());
  TestTrue(TEXT("Combat has Combat tag"),
    Combat.HasTag<FCrowdDemoCombatCapabilityTag>());
  TestTrue(TEXT("Combat has full Combat bundle"),
    Combat.HasFragment<FCrowdDemoMassStatsFragment>()
      && Combat.HasFragment<FCrowdDemoBusinessStateFragment>()
      && Combat.HasFragment<FCrowdDemoRangedAttackFragment>()
      && Combat.HasFragment<FCrowdDemoReactiveMotionFragment>()
      && Combat.HasFragment<FCrowdDemoHitFlashFragment>());
  TestTrue(TEXT("TargetCombat has both capability tags"),
    TargetCombat.HasTag<FCrowdDemoTargetCapabilityTag>()
      && TargetCombat.HasTag<FCrowdDemoCombatCapabilityTag>());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPostFinalizeMinimalQueryStructureTest,
  "CrowdDemo.Architecture.PostFinalizeMinimalQuery",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPostFinalizeMinimalQueryStructureTest::RunTest(
  const FString& Parameters)
{
  FString ProcessorSource;
  const FString ProcessorPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp"));
  TestTrue(TEXT("processor source is readable"),
    FFileHelper::LoadFileToString(ProcessorSource, *ProcessorPath));

  const FString ConfigureMarker =
    TEXT("void FCrowdDemoRoundPostFinalizeMetricsStage::ConfigureQueries");
  const FString ExecuteMarker =
    TEXT("void FCrowdDemoRoundPostFinalizeMetricsStage::Execute");
  const FString CommitQueryMarker = TEXT(
    "void FCrowdDemoRoundAuthorityCommitStage::Execute");
  const int32 ExecuteStart = ProcessorSource.Find(ExecuteMarker);
  const int32 CommitQueryStart = ProcessorSource.Find(
    CommitQueryMarker, ESearchCase::CaseSensitive,
    ESearchDir::FromStart, ExecuteStart + ExecuteMarker.Len());
  TestFalse(TEXT("post-finalize has no Mass query configuration"),
    ProcessorSource.Contains(ConfigureMarker));
  TestTrue(TEXT("post-finalize execute block found"),
    ExecuteStart != INDEX_NONE && CommitQueryStart > ExecuteStart);
  if (ExecuteStart == INDEX_NONE || CommitQueryStart <= ExecuteStart)
  {
    return false;
  }

  const FString PostFinalizeBlock = ProcessorSource.Mid(
    ExecuteStart, CommitQueryStart - ExecuteStart);
  TestFalse(TEXT("post-finalize execute does not traverse a Mass query"),
    PostFinalizeBlock.Contains(TEXT("EntityQuery.")));
  TestTrue(TEXT("post-finalize consumes prepared final-state records"),
    PostFinalizeBlock.Contains(
      TEXT("GetPreparedPostFinalizeAgentRecords()")));
  TestFalse(TEXT("authority commit has no Mass query configuration"),
    ProcessorSource.Contains(TEXT(
      "void FCrowdDemoRoundAuthorityCommitStage::ConfigureQueries")));
  TestFalse(TEXT("client commit has no Mass query configuration"),
    ProcessorSource.Contains(TEXT(
      "void FCrowdDemoRoundClientPredictionCommitStage::ConfigureQueries")));
  TestFalse(TEXT("legacy duplicate commit traversal is removed"),
    ProcessorSource.Contains(TEXT("CommitRoundState(")));
  TestFalse(TEXT("legacy commit query helper is removed"),
    ProcessorSource.Contains(TEXT("ConfigureCommitQuery(")));
  TestFalse(TEXT("checkpoint publisher has no Mass query configuration"),
    ProcessorSource.Contains(TEXT(
      "void FCrowdDemoRoundCheckpointPublisherStage::ConfigureQueries")));
  const FString CheckpointExecuteMarker = TEXT(
    "void FCrowdDemoRoundCheckpointPublisherStage::Execute");
  const FString WorkerResultStageMarker = TEXT(
    "void FCrowdDemoWorkerResultApplyStage::Execute");
  const int32 CheckpointExecuteStart = ProcessorSource.Find(
    CheckpointExecuteMarker);
  const int32 WorkerResultStageStart = ProcessorSource.Find(
    WorkerResultStageMarker, ESearchCase::CaseSensitive,
    ESearchDir::FromStart,
    CheckpointExecuteStart + CheckpointExecuteMarker.Len());
  TestTrue(TEXT("checkpoint publisher execute block found"),
    CheckpointExecuteStart != INDEX_NONE
      && WorkerResultStageStart > CheckpointExecuteStart);
  if (CheckpointExecuteStart != INDEX_NONE
    && WorkerResultStageStart > CheckpointExecuteStart)
  {
    const FString CheckpointBlock = ProcessorSource.Mid(
      CheckpointExecuteStart,
      WorkerResultStageStart - CheckpointExecuteStart);
    TestFalse(TEXT("checkpoint publisher does not traverse a Mass query"),
      CheckpointBlock.Contains(TEXT("EntityQuery.")));
    TestTrue(TEXT("checkpoint publisher consumes prepared checkpoint states"),
      CheckpointBlock.Contains(TEXT("GetPreparedCheckpointAgentStates()")));
  }
  TestFalse(TEXT("legacy visual-state resolve processor is removed"),
    ProcessorSource.Contains(TEXT(
      "UCrowdDemoRoundVisualStateResolveProcessor")));

  const FString FacingApplyMarker =
    TEXT("bool FCrowdDemoRoundFacingFinalizeStage::ApplyPreparedCommit");
  const int32 FacingApplyStart = ProcessorSource.Find(FacingApplyMarker);
  const FString FacingExecuteMarker =
    TEXT("void FCrowdDemoRoundFacingFinalizeStage::Execute");
  const int32 FacingExecuteStart = ProcessorSource.Find(FacingExecuteMarker);
  if (FacingApplyStart != INDEX_NONE
    && FacingExecuteStart > FacingApplyStart)
  {
    const FString FacingApplyBlock = ProcessorSource.Mid(
      FacingApplyStart, FacingExecuteStart - FacingApplyStart);
    int32 ApplyTraversalCount = 0;
    int32 SearchFrom = 0;
    const FString TraversalMarker = TEXT("EntityQuery->ForEachEntityChunk");
    while (true)
    {
      const int32 Found = FacingApplyBlock.Find(
        TraversalMarker, ESearchCase::CaseSensitive,
        ESearchDir::FromStart, SearchFrom);
      if (Found == INDEX_NONE) break;
      ++ApplyTraversalCount;
      SearchFrom = Found + TraversalMarker.Len();
    }
    TestEqual(TEXT("prepared apply performs exactly one atomic write traversal"),
      ApplyTraversalCount, 1);
    TestTrue(TEXT("prepared apply validates against canonical snapshot"),
      FacingApplyBlock.Contains(TEXT(
        "Pipeline.GetBoundarySnapshot().Agents")));
    TestTrue(TEXT("prepared apply writes engine transform"),
      FacingApplyBlock.Contains(TEXT(
        "Transforms[It].SetTransform(Transform)")));
    TestTrue(TEXT("prepared apply writes engine velocity"),
      FacingApplyBlock.Contains(TEXT(
        "Velocities[It].Value = Movement.Velocity")));
    TestTrue(TEXT("prepared apply resolves final visual state"),
      FacingApplyBlock.Contains(TEXT("ResolveVisualStateBoundary(")));
    TestTrue(TEXT("prepared apply publishes checkpoint states"),
      FacingApplyBlock.Contains(TEXT(
        "SetPreparedCheckpointAgentStates(")));
  }
  if (FacingExecuteStart != INDEX_NONE && ExecuteStart > FacingExecuteStart)
  {
    const FString FacingBlock = ProcessorSource.Mid(
      FacingExecuteStart, ExecuteStart - FacingExecuteStart);
    int32 FacingTraversalCount = 0;
    int32 SearchFrom = 0;
    const FString TraversalMarker = TEXT("EntityQuery->ForEachEntityChunk");
    while (true)
    {
      const int32 Found = FacingBlock.Find(
        TraversalMarker, ESearchCase::CaseSensitive,
        ESearchDir::FromStart, SearchFrom);
      if (Found == INDEX_NONE) break;
      ++FacingTraversalCount;
      SearchFrom = Found + TraversalMarker.Len();
    }
    TestEqual(TEXT("facing preparation performs no intermediate Mass read"),
      FacingTraversalCount, 0);
    TestTrue(TEXT("facing preparation validates against canonical snapshot"),
      FacingBlock.Contains(TEXT(
        "Pipeline->GetBoundarySnapshot().Agents")));
    TestTrue(TEXT("facing preparation dispatches worker work"),
      FacingBlock.Contains(TEXT("DispatchBoundaryFacingWork("))
        && FacingBlock.Contains(
          TEXT("DispatchBoundarySoftPressureWorkGraph(")));
    TestTrue(TEXT("facing preparation consumes worker work"),
      FacingBlock.Contains(TEXT("ConsumeBoundaryFacingWork(")));
    TestTrue(TEXT("facing-finalize consumes boundary facing history"),
      FacingBlock.Contains(TEXT("GetBoundaryFacingFacts()")));
    TestFalse(TEXT("facing-finalize does not gather previous facing from Mass"),
      FacingBlock.Contains(TEXT(
        "PreviousSettleStepsByAgentId.Add(\n        Identities[It].Id")));
  }
  TestTrue(TEXT("post-finalize completes combat rollback facts"),
    PostFinalizeBlock.Contains(TEXT(
      "CompleteSoftPressureRollbackCombatState(")));

  FString PipelineSource;
  const FString PipelinePath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"));
  TestTrue(TEXT("pipeline source is readable"),
    FFileHelper::LoadFileToString(PipelineSource, *PipelinePath));
  const FString BoundaryFrameHeaderPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/Public/MassCrowdBoundaryFrameTransaction.h"));
  TestFalse(TEXT("legacy plugin frame transaction is physically deleted"),
    FPaths::FileExists(BoundaryFrameHeaderPath));
  FString PipelineHeader;
  const FString PipelineHeaderPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h"));
  TestTrue(TEXT("pipeline header is readable"),
    FFileHelper::LoadFileToString(
      PipelineHeader, *PipelineHeaderPath));
  TestFalse(TEXT("project pipeline owns no frame transaction"),
    PipelineHeader.Contains(TEXT("FrameTransaction"))
      || PipelineHeader.Contains(TEXT(
        "MassCrowdBoundaryFrameTransaction")));
  TestTrue(TEXT("SoftPressure DAG runs real shared-flow work"),
    PipelineSource.Contains(
      TEXT("FCrowdMassSharedFlowWork::BuildPreferred(")));
  TestTrue(TEXT("SoftPressure DAG runs real target work chain"),
    PipelineSource.Contains(
      TEXT("FCrowdMassTargetRegionWork::BuildTopology("))
      && PipelineSource.Contains(
        TEXT("FCrowdMassTargetRegionWork::BuildDemand("))
      && PipelineSource.Contains(
        TEXT("FCrowdMassTargetRegionWork::SolvePlan("))
      && PipelineSource.Contains(
        TEXT("FCrowdMassTargetRegionWork::BuildGuidance(")));
  TestTrue(TEXT("SoftPressure DAG runs real movement and particle work"),
    PipelineSource.Contains(
      TEXT("FCrowdMassMovementPipelineWork::Run("))
      && PipelineSource.Contains(
        TEXT("FCrowdMassParticlePipelineWork::Run(")));
  TestFalse(TEXT("Round processors do not use immediate futures"),
    ProcessorSource.Contains(TEXT("TFuture<"))
      || ProcessorSource.Contains(TEXT("Future.Get()"))
      || ProcessorSource.Contains(TEXT("Async(")));
  TestFalse(TEXT("Round production source has no blocking boundary drain"),
    ProcessorSource.Contains(TEXT("WaitAndDrain("))
      || PipelineSource.Contains(TEXT("WaitAndDrain("))
      || ProcessorSource.Contains(TEXT("CompletionEvent->Wait("))
      || PipelineSource.Contains(TEXT("CompletionEvent->Wait(")));
  TestFalse(TEXT("legacy fixed-step Execute is physically deleted"),
    ProcessorSource.Contains(TEXT(
      "void UCrowdDemoRoundSimFixedStepPipelineProcessor::Execute(")));
  TestFalse(TEXT("production source has no manual Mass processor execution"),
    ProcessorSource.Contains(TEXT("CallExecute(")));
  TestFalse(TEXT("dynamic child-processor factory is physically deleted"),
    ProcessorSource.Contains(TEXT("MakeDynamicRoundProcessor")));
  TestFalse(TEXT("dynamic child-processor flags are physically deleted"),
    ProcessorSource.Contains(TEXT("ROUND_DYNAMIC_FLAGS")));
  const int32 AuthorityStageStart = ProcessorSource.Find(TEXT(
    "ExecuteAuthorityInputStage("));
  const int32 ResultStageStart = ProcessorSource.Find(TEXT(
    "ExecuteResultCommitStage("));
  const int32 PlanApplyCall = ProcessorSource.Find(
    TEXT("PlanApply.Execute(EntityManager, Context)"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart,
    AuthorityStageStart);
  const int32 BoundaryPollCall = ProcessorSource.Find(
    TEXT("Pipeline->PollBoundaryWork()"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart,
    ResultStageStart);
  TestTrue(TEXT("authority input and result poll have separate ordered stages"),
    AuthorityStageStart != INDEX_NONE
      && ResultStageStart > AuthorityStageStart
      && PlanApplyCall > AuthorityStageStart
      && PlanApplyCall < ResultStageStart
      && BoundaryPollCall > ResultStageStart);
  TestTrue(TEXT("applied authority frame invalidates the in-flight generation"),
    ProcessorSource.Contains(TEXT(
      "Pipeline->InvalidateInFlightBoundaryForAuthoritativeState()")));
  TestTrue(TEXT("correction arrival reopens plan apply boundary"),
    PipelineSource.Contains(TEXT(
      "LastClaimedPlanApplyBoundarySequence = MAX_uint64;"))
      && PipelineSource.Contains(TEXT(
        "InvalidateInFlightBoundaryForAuthoritativeState()")));
  const int32 PublishStart = PipelineSource.Find(
    TEXT("bool UCrowdDemoRoundSimPipelineSubsystem::PublishBoundarySnapshot"));
  const int32 FindFormationStart = PipelineSource.Find(
    TEXT("UCrowdDemoRoundSimPipelineSubsystem::FindBoundaryFormationFact"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart, PublishStart);
  TestTrue(TEXT("boundary publish block found"),
    PublishStart != INDEX_NONE && FindFormationStart > PublishStart);
  if (PublishStart != INDEX_NONE && FindFormationStart > PublishStart)
  {
    const FString PublishBlock = PipelineSource.Mid(
      PublishStart, FindFormationStart - PublishStart);
    TestTrue(TEXT("post-finalize records reset at every boundary"),
      PublishBlock.Contains(TEXT("PreparedPostFinalizeAgentRecords.Reset()")));
    TestTrue(TEXT("checkpoint states reset at every boundary"),
      PublishBlock.Contains(TEXT("PreparedCheckpointAgentStates.Reset()")));
    TestTrue(TEXT("reactive motion steps reset at every boundary"),
      PublishBlock.Contains(TEXT("PreparedReactiveMotionSteps.Reset()")));
  }

  FString FragmentHeader;
  const FString FragmentPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoMassFragments.h"));
  TestTrue(TEXT("fragment header is readable"),
    FFileHelper::LoadFileToString(FragmentHeader, *FragmentPath));
  TestFalse(TEXT("OpenSpawn fragment is physically deleted"),
    FragmentHeader.Contains(TEXT("FCrowdDemoOpenSpawnRelaxationFragment")));
  TestTrue(TEXT("Target and Combat capability tags are explicit"),
    FragmentHeader.Contains(TEXT(
      "FCrowdDemoTargetCapabilityTag"))
      && FragmentHeader.Contains(TEXT(
        "FCrowdDemoCombatCapabilityTag"))
      && FragmentHeader.Contains(TEXT(
        "ECrowdDemoMassCapability")));

  const TCHAR* DeletedCompatibilityFragments[] = {
    TEXT("FCrowdDemoRoundMoveIntentFragment"),
    TEXT("FCrowdDemoRoundGuidanceCandidatesFragment"),
    TEXT("FCrowdDemoRoundComposedGuidanceFragment"),
    TEXT("FCrowdDemoRoundLocalVelocityFragment"),
    TEXT("FCrowdDemoRoundParticleConstraintFragment"),
    TEXT("FCrowdDemoRoundFacingFragment"),
    TEXT("FCrowdDemoReactiveMotionStepFragment"),
    TEXT("FCrowdDemoTargetCapabilityFragment"),
    TEXT("FCrowdDemoRoundProposedMovementFragment"),
    TEXT("FCrowdDemoRoundObstacleConstraintFragment")
  };
  for (const TCHAR* DeletedFragment : DeletedCompatibilityFragments)
  {
    TestFalse(FString::Printf(TEXT("compatibility fragment %s is physically deleted"),
      DeletedFragment), FragmentHeader.Contains(DeletedFragment));
    TestFalse(FString::Printf(TEXT("processor source does not use %s"),
      DeletedFragment), ProcessorSource.Contains(DeletedFragment));
  }

  FString MassSubsystemSource;
  const FString MassSubsystemPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoMassSubsystem.cpp"));
  TestTrue(TEXT("Mass subsystem source is readable"),
    FFileHelper::LoadFileToString(MassSubsystemSource, *MassSubsystemPath));
  TestTrue(TEXT("authority template cache contains four capability bitsets"),
    MassSubsystemSource.Contains(TEXT(
      "authority_capability_templates=4"))
      && MassSubsystemSource.Contains(TEXT(
        "GetCrowdDemoAuthorityTemplateID(Capabilities)"))
      && MassSubsystemSource.Contains(TEXT(
        "ECrowdDemoMassCapability::Target"))
      && MassSubsystemSource.Contains(TEXT(
        "ECrowdDemoMassCapability::Combat")));
  TestTrue(TEXT("Target and Combat composition are conditional bundles"),
    MassSubsystemSource.Contains(TEXT(
      "TemplateData.AddTag<FCrowdDemoTargetCapabilityTag>()"))
      && MassSubsystemSource.Contains(TEXT(
        "TemplateData.AddTag<FCrowdDemoCombatCapabilityTag>()"))
      && MassSubsystemSource.Contains(TEXT(
        "TemplateData.AddFragment<FCrowdDemoMassStatsFragment>()"))
      && MassSubsystemSource.Contains(TEXT(
        "ResolveAuthorityCapabilityProfile(")));
  TestTrue(TEXT("non-combat profiles do not receive the Combat bundle"),
    MassSubsystemSource.Contains(TEXT(
      "ECrowdDemoMassCapability Capabilities =\n      ECrowdDemoMassCapability::Base;"))
      && MassSubsystemSource.Contains(TEXT(
        "Capabilities |= ECrowdDemoMassCapability::Combat;")));
  TestTrue(TEXT("Round queries consume Combat as an optional capability"),
    ProcessorSource.Contains(TEXT(
      "BusinessFact.bHasCombatCapability = bHasCombatBundle;"))
      && ProcessorSource.Contains(TEXT(
        "EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional"))
      && ProcessorSource.Contains(TEXT(
        "EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional")));
  FString ReplicationSource;
  const FString ReplicationPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoMassReplication.cpp"));
  TestTrue(TEXT("replication source is readable"),
    FFileHelper::LoadFileToString(
      ReplicationSource, *ReplicationPath));
  TestTrue(TEXT("replication accepts missing Combat capability deterministically"),
    ReplicationSource.Contains(TEXT(
      "const bool bHasCombatBundle = Stats.IsValidIndex(EntityIndex)"))
      && ReplicationSource.Contains(TEXT(
        "bHasCombatBundle ? Stats[EntityIndex] : DefaultStats"))
      && ReplicationSource.Contains(TEXT(
        "EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional")));
  TestTrue(TEXT("Worker Input Sync and Result Apply are registered"),
    MassSubsystemSource.Contains(
      TEXT("*WorkerInputSyncProcessor"))
      && MassSubsystemSource.Contains(
        TEXT("*WorkerResultApplyProcessor")));
  TestFalse(TEXT("four legacy Round processors are not registered"),
    MassSubsystemSource.Contains(TEXT("RoundAuthorityInputProcessor"))
      || MassSubsystemSource.Contains(TEXT("RoundResultCommitProcessor"))
      || MassSubsystemSource.Contains(TEXT("RoundPostCommitProcessor"))
      || MassSubsystemSource.Contains(TEXT("RoundRequestSubmitProcessor")));
  TestFalse(TEXT("legacy stage executor is physically deleted"),
    MassSubsystemSource.Contains(TEXT("RoundSimPipelineProcessor"))
      || ProcessorSource.Contains(TEXT(
        "UCrowdDemoRoundSimFixedStepPipelineProcessor")));

  FString ProcessorHeader;
  const FString ProcessorHeaderPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.h"));
  TestTrue(TEXT("Round processor header is readable"),
    FFileHelper::LoadFileToString(
      ProcessorHeader, *ProcessorHeaderPath));
  int32 RegisteredRoundProcessorTypeCount = 0;
  int32 ProcessorTypeSearchFrom = 0;
  const FString ProcessorTypeMarker = TEXT(": public UMassProcessor");
  while (true)
  {
    const int32 Found = ProcessorHeader.Find(
      ProcessorTypeMarker, ESearchCase::CaseSensitive,
      ESearchDir::FromStart, ProcessorTypeSearchFrom);
    if (Found == INDEX_NONE) break;
    ++RegisteredRoundProcessorTypeCount;
    ProcessorTypeSearchFrom =
      Found + ProcessorTypeMarker.Len();
  }
  TestEqual(TEXT("exactly two Worker adapter classes retain Mass scheduling authority"),
    RegisteredRoundProcessorTypeCount, 2);
  TestFalse(TEXT("no UObject stage adapters remain"),
    ProcessorHeader.Contains(TEXT("Stage : public UObject"))
      || ProcessorHeader.Contains(TEXT(
        "UCrowdDemoRoundSimFixedStepPipelineProcessor")));
  TestTrue(TEXT("two active nodes own their exact Mass queries"),
    ProcessorHeader.Contains(TEXT(
      "FMassEntityQuery InputSyncQuery"))
      && ProcessorHeader.Contains(TEXT(
        "FMassEntityQuery ResultCommitQuery"))
      && ProcessorHeader.Contains(TEXT(
        "FMassEntityQuery RequestSubmitQuery")));
  TestTrue(TEXT("bootstrap and autonomous Mass access gates are explicit"),
    PipelineSource.Contains(TEXT(
      "TryRecordCanonicalGatherRead()"))
      && PipelineSource.Contains(TEXT(
        "TryBeginAtomicCommitWrite()"))
      && PipelineSource.Contains(TEXT(
        "IsOrdinaryStepContractSatisfied()"))
      && PipelineSource.Contains(TEXT(
        "bCurrentStepUsedWorkerProxySnapshot"))
      && ProcessorSource.Contains(TEXT(
        "Pipeline->TryRecordCanonicalGatherRead()"))
      && ProcessorSource.Contains(TEXT(
        "TryPublishWorkerProxyBoundarySnapshot()"))
      && ProcessorSource.Contains(TEXT(
        "Pipeline.TryBeginAtomicCommitWrite()")));
  for (const TCHAR* DeletedFragment : DeletedCompatibilityFragments)
  {
    TestFalse(FString::Printf(TEXT("Mass template excludes %s"), DeletedFragment),
      MassSubsystemSource.Contains(DeletedFragment));
  }

  FString RuntimeFragmentHeader;
  const FString RuntimeFragmentPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/Public/MassCrowdRuntimeFragments.h"));
  TestTrue(TEXT("Runtime fragment header is readable"),
    FFileHelper::LoadFileToString(RuntimeFragmentHeader, *RuntimeFragmentPath));
  FString RuntimeTraitSource;
  const FString RuntimeTraitPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/Private/MassCrowdMovementTrait.cpp"));
  TestTrue(TEXT("Runtime movement trait source is readable"),
    FFileHelper::LoadFileToString(RuntimeTraitSource, *RuntimeTraitPath));
  const TCHAR* DeletedRuntimeIntermediateFragments[] = {
    TEXT("FCrowdMassGuidanceCandidatesFragment"),
    TEXT("FCrowdMassComposedGuidanceFragment"),
    TEXT("FCrowdMassLocalVelocityFragment")
  };
  for (const TCHAR* DeletedFragment : DeletedRuntimeIntermediateFragments)
  {
    TestFalse(FString::Printf(
      TEXT("Runtime intermediate fragment %s is physically deleted"),
      DeletedFragment), RuntimeFragmentHeader.Contains(DeletedFragment));
    TestFalse(FString::Printf(
      TEXT("Runtime trait excludes %s"), DeletedFragment),
      RuntimeTraitSource.Contains(DeletedFragment));
    TestFalse(FString::Printf(
      TEXT("Demo Mass template excludes %s"), DeletedFragment),
      MassSubsystemSource.Contains(DeletedFragment));
    TestFalse(FString::Printf(
      TEXT("processor source does not publish %s"), DeletedFragment),
      ProcessorSource.Contains(DeletedFragment));
  }
  TestTrue(TEXT("SF1 obstacle stage consumes prepared predicted movements"),
    ProcessorSource.Contains(
      TEXT("GetPreparedRuntimePredictedMovements()")));
  TestTrue(TEXT("SF1 obstacle stage publishes prepared final kinematics"),
    ProcessorSource.Contains(
      TEXT("SetPreparedRuntimeFinalKinematics")));
  TestTrue(TEXT("Runtime stable identity is initialized at authority spawn"),
    MassSubsystemSource.Contains(
      TEXT("FCrowdMassAgentFragment& RuntimeIdentity"))
      && MassSubsystemSource.Contains(
        TEXT("RuntimeIdentity.AgentId = Identity.Id"))
      && MassSubsystemSource.Contains(
        TEXT("RuntimeIdentity.SetStableEntityRef"))
      && MassSubsystemSource.Contains(
        TEXT("CrowdDemoStableProviderId")));
  TestTrue(TEXT("Runtime stable identity is synchronized at plan activation"),
    ProcessorSource.Contains(
      TEXT("GetMutableFragmentView<FCrowdMassAgentFragment>()"))
      && ProcessorSource.Contains(
        TEXT("RuntimeIdentities[It].AgentId = Identities[It].Id"))
      && ProcessorSource.Contains(
        TEXT("RuntimeIdentities[It].SetStableEntityRef")));
  TestTrue(TEXT("Runtime behavior facts are initialized at authority spawn"),
    MassSubsystemSource.Contains(
      TEXT("TemplateData.AddFragment<FCrowdMassBehaviorFragment>()"))
      && MassSubsystemSource.Contains(
        TEXT("RuntimeBehavior.SetAgentFacts(RuntimeFacts)")));
  TestTrue(TEXT("Runtime persistent state is synchronized at plan activation"),
    ProcessorSource.Contains(
      TEXT("GetMutableFragmentView<FCrowdMassSimulationStateFragment>()"))
      && ProcessorSource.Contains(
        TEXT("RuntimeStates[It].PlanRevision = State.PlanRevision")));
  TestTrue(TEXT("Runtime persistent properties are synchronized at plan activation"),
    ProcessorSource.Contains(
      TEXT("GetMutableFragmentView<FCrowdMassPropertiesFragment>()"))
      && ProcessorSource.Contains(
        TEXT("RuntimeProperties[It].CapabilityProfileKey")));

  TestFalse(TEXT("movement work declares no Mass query"),
    ProcessorSource.Contains(TEXT(
      "FCrowdDemoRoundMovementWorkStage::ConfigureQueries")));
  TestFalse(TEXT("obstacle stage declares no Mass query"),
    ProcessorSource.Contains(TEXT(
      "FCrowdDemoRoundObstacleConstraintStage::ConfigureQueries")));

  const int32 MovementWorkBodyStart = ProcessorSource.Find(
    TEXT("void FCrowdDemoRoundMovementWorkStage::Execute"));
  const int32 PostFinalizeStart = ProcessorSource.Find(
    TEXT("void FCrowdDemoRoundPostFinalizeMetricsStage::Execute"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart,
    MovementWorkBodyStart);
  TestTrue(TEXT("movement work execute block found"),
    MovementWorkBodyStart != INDEX_NONE && PostFinalizeStart > MovementWorkBodyStart);
  if (MovementWorkBodyStart != INDEX_NONE && PostFinalizeStart > MovementWorkBodyStart)
  {
    const FString MovementWorkBody = ProcessorSource.Mid(
      MovementWorkBodyStart, PostFinalizeStart - MovementWorkBodyStart);
    int32 QueryTraversalCount = 0;
    int32 SearchFrom = 0;
    const FString TraversalMarker = TEXT("EntityQuery->ForEachEntityChunk");
    while (true)
    {
      const int32 Found = MovementWorkBody.Find(
        TraversalMarker, ESearchCase::CaseSensitive,
        ESearchDir::FromStart, SearchFrom);
      if (Found == INDEX_NONE) break;
      ++QueryTraversalCount;
      SearchFrom = Found + TraversalMarker.Len();
    }
    TestEqual(TEXT("movement work has no Mass traversal"),
      QueryTraversalCount, 0);
    TestTrue(TEXT("reactive step input uses prepared boundary facts"),
      MovementWorkBody.Contains(TEXT("GetPreparedReactiveMotionSteps()")));
  }

  const int32 ObstacleBodyStart = ProcessorSource.Find(
    TEXT("void FCrowdDemoRoundObstacleConstraintStage::Execute"));
  const int32 FacingFinalizeStart = ProcessorSource.Find(
    TEXT("FCrowdDemoRoundFacingFinalizeStage::"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart, ObstacleBodyStart);
  TestTrue(TEXT("obstacle execute block found"),
    ObstacleBodyStart != INDEX_NONE && FacingFinalizeStart > ObstacleBodyStart);
  if (ObstacleBodyStart != INDEX_NONE && FacingFinalizeStart > ObstacleBodyStart)
  {
    const FString ObstacleBody = ProcessorSource.Mid(
      ObstacleBodyStart, FacingFinalizeStart - ObstacleBodyStart);
    TestFalse(TEXT("obstacle stage has no Mass traversal"),
      ObstacleBody.Contains(TEXT("EntityQuery->ForEachEntityChunk")));
    TestTrue(TEXT("obstacle stage validates canonical boundary snapshot"),
      ObstacleBody.Contains(TEXT("IsBoundarySnapshotCurrent()")));
    TestTrue(TEXT("obstacle stage only stages immutable work"),
      ObstacleBody.Contains(TEXT("StageBoundaryObstacleWork(")));
  }

  const int32 FlowPreferredStart = ProcessorSource.Find(
    TEXT("void FCrowdDemoRoundFlowPreferredVelocityStage::Execute"));
  const int32 BoundaryGatherStart = ProcessorSource.Find(
    TEXT("FCrowdDemoRoundBoundaryGatherStage::"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart, FlowPreferredStart);
  TestTrue(TEXT("flow preferred execute block found"),
    FlowPreferredStart != INDEX_NONE && BoundaryGatherStart > FlowPreferredStart);
  if (FlowPreferredStart != INDEX_NONE && BoundaryGatherStart > FlowPreferredStart)
  {
    const FString FlowPreferredBody = ProcessorSource.Mid(
      FlowPreferredStart, BoundaryGatherStart - FlowPreferredStart);
    int32 TraversalCount = 0;
    int32 SearchFrom = 0;
    const FString TraversalMarker = TEXT("EntityQuery->ForEachEntityChunk");
    while (true)
    {
      const int32 Found = FlowPreferredBody.Find(
        TraversalMarker, ESearchCase::CaseSensitive,
        ESearchDir::FromStart, SearchFrom);
      if (Found == INDEX_NONE) break;
      ++TraversalCount;
      SearchFrom = Found + TraversalMarker.Len();
    }
    TestEqual(TEXT("flow preferred has no pre-validation Mass write"),
      TraversalCount, 0);
    TestTrue(TEXT("flow preferred validates against the boundary snapshot"),
      FlowPreferredBody.Contains(TEXT("GetBoundarySnapshot().Agents")));
    TestTrue(TEXT("flow preferred only stages immutable work"),
      FlowPreferredBody.Contains(TEXT("StageBoundarySharedFlowWork(")));
  }

  const int32 TargetGuidanceStart = ProcessorSource.Find(
    TEXT("void FCrowdDemoRoundTargetRegionGuidanceStage::Execute"));
  const int32 ParticleBoundaryStart = ProcessorSource.Find(
    TEXT("FCrowdDemoRoundParticleConstraintStage::"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart, TargetGuidanceStart);
  TestTrue(TEXT("target guidance execute block found"),
    TargetGuidanceStart != INDEX_NONE
      && ParticleBoundaryStart > TargetGuidanceStart);
  if (TargetGuidanceStart != INDEX_NONE
    && ParticleBoundaryStart > TargetGuidanceStart)
  {
    const FString TargetGuidanceBody = ProcessorSource.Mid(
      TargetGuidanceStart, ParticleBoundaryStart - TargetGuidanceStart);
    TestFalse(TEXT("target guidance does not traverse Mass"),
      TargetGuidanceBody.Contains(TEXT("EntityQuery->ForEachEntityChunk")));
    TestTrue(TEXT("target guidance consumes the canonical boundary snapshot"),
      TargetGuidanceBody.Contains(TEXT("GetBoundarySnapshot().Agents")));
  }
  TestFalse(TEXT("target guidance declares no Mass query"),
    ProcessorSource.Contains(TEXT(
      "FCrowdDemoRoundTargetRegionGuidanceStage::ConfigureQueries")));

  const FString CombatWorkMarker =
    TEXT("FCrowdDemoBoundaryBusinessWorkOutput RunBoundaryBusinessWork");
  const FString CorrectionIntervalMarker =
    TEXT("constexpr float CorrectionIntervalSeconds");
  const int32 CombatWorkStart = PipelineSource.Find(CombatWorkMarker);
  const int32 CorrectionIntervalStart = PipelineSource.Find(
    CorrectionIntervalMarker, ESearchCase::CaseSensitive,
    ESearchDir::FromStart, CombatWorkStart + CombatWorkMarker.Len());
  TestTrue(TEXT("combined combat boundary work block found"),
    CombatWorkStart != INDEX_NONE
      && CorrectionIntervalStart > CombatWorkStart);
  if (CombatWorkStart != INDEX_NONE
    && CorrectionIntervalStart > CombatWorkStart)
  {
    const FString CombatBlock = PipelineSource.Mid(
      CombatWorkStart, CorrectionIntervalStart - CombatWorkStart);
    int32 QueryTraversalCount = 0;
    int32 SearchFrom = 0;
    const FString TraversalMarker = TEXT("EntityQuery->ForEachEntityChunk");
    while (true)
    {
      const int32 Found = CombatBlock.Find(
        TraversalMarker, ESearchCase::CaseSensitive,
        ESearchDir::FromStart, SearchFrom);
      if (Found == INDEX_NONE) break;
      ++QueryTraversalCount;
      SearchFrom = Found + TraversalMarker.Len();
    }
    TestEqual(TEXT("combat prepare has no independent Mass write traversal"),
      QueryTraversalCount, 0);
    TestTrue(TEXT("combat prepare consumes immutable canonical business facts"),
      CombatBlock.Contains(TEXT("Input.Facts")));
    const int32 ProjectileStage = CombatBlock.Find(
      TEXT("PrepareProjectileBoundary("));
    const int32 HitStage = CombatBlock.Find(TEXT("ResolveHitFacts("));
    const int32 ReactiveStage = CombatBlock.Find(TEXT("AdvanceReactiveMotion("));
    TestTrue(TEXT("combat transaction preserves ranged then hit then reactive order"),
      ProjectileStage != INDEX_NONE && HitStage > ProjectileStage
        && ReactiveStage > HitStage);
  }
  TestFalse(TEXT("old combined combat processor is physically deleted"),
    ProcessorSource.Contains(
      TEXT("UCrowdDemoRoundCombatBoundaryProcessor")));
  TestFalse(TEXT("old ranged processor is physically deleted"),
    ProcessorSource.Contains(TEXT("UCrowdDemoRoundRangedCombatProcessor")));
  TestFalse(TEXT("old hit processor is physically deleted"),
    ProcessorSource.Contains(
      TEXT("UCrowdDemoRoundHitResponseBoundaryApplyProcessor")));
  TestFalse(TEXT("old reactive processor is physically deleted"),
    ProcessorSource.Contains(
      TEXT("UCrowdDemoRoundReactiveMotionIntentComposeProcessor")));
  TestFalse(TEXT("cross-processor pending hit bridge is deleted"),
    ProcessorSource.Contains(TEXT("SetPendingProjectileHitFacts"))
      || ProcessorSource.Contains(TEXT("ConsumePendingProjectileHitFacts")));
  TestTrue(TEXT("final boundary writer applies prepared combat state"),
    ProcessorSource.Contains(
      TEXT("CombatAgent.Combat, Stats[It], Businesses[It]")));
  TestTrue(TEXT("final boundary writer publishes Mass-authoritative projectile state"),
    ProcessorSource.Contains(TEXT(
      "Pipeline.ApplyProjectileFinalState(")));
  TestFalse(TEXT("pipeline no longer owns projectile authority array"),
    PipelineSource.Contains(TEXT("PreparedProjectiles")));
  TestFalse(TEXT("projectile mirror compatibility path is deleted"),
    MassSubsystemSource.Contains(TEXT("MirrorProjectileStates")));

  TestFalse(TEXT("particle processor has no Mass query seam"),
    ProcessorSource.Contains(
      TEXT("void FCrowdDemoRoundParticleConstraintStage::ConfigureQueries")));
  const FString ParticleExecuteMarker =
    TEXT("void FCrowdDemoRoundParticleConstraintStage::Execute");
  const FString ObstacleConstructorMarker =
    TEXT("FCrowdDemoRoundObstacleConstraintStage::");
  const int32 ParticleExecuteStart =
    ProcessorSource.Find(ParticleExecuteMarker);
  const int32 ObstacleConstructorStart = ProcessorSource.Find(
    ObstacleConstructorMarker, ESearchCase::CaseSensitive,
    ESearchDir::FromStart, ParticleExecuteStart + ParticleExecuteMarker.Len());
  TestTrue(TEXT("particle execute block found"),
    ParticleExecuteStart != INDEX_NONE
      && ObstacleConstructorStart > ParticleExecuteStart);
  if (ParticleExecuteStart != INDEX_NONE
    && ObstacleConstructorStart > ParticleExecuteStart)
  {
    const FString ParticleExecuteBlock = ProcessorSource.Mid(
      ParticleExecuteStart, ObstacleConstructorStart - ParticleExecuteStart);
    const TCHAR* DeferredParticleSideEffects[] = {
      TEXT("RecordParticleConstraintSummary("),
      TEXT("RecordParticleFailureFixture("),
      TEXT("RecordCrossProfileParticleViolations("),
      TEXT("RecordSoftPressureRouteStep("),
      TEXT("RecordTargetStabilityStep("),
      TEXT("RecordOpenSpawnRelaxationParticleStep(")
    };
    for (const TCHAR* SideEffect : DeferredParticleSideEffects)
      TestFalse(FString::Printf(TEXT("particle solve defers %s"), SideEffect),
        ParticleExecuteBlock.Contains(SideEffect));
    TestTrue(TEXT("particle solve prepares one diagnostic commit"),
      ParticleExecuteBlock.Contains(
        TEXT("SetPreparedParticleDiagnosticCommit(")));
  }
  TestTrue(TEXT("post-finalize commits prepared particle diagnostics"),
    ProcessorSource.Contains(
      TEXT("FCrowdDemoRoundPostFinalizeMetricsStage::Execute"))
      && ProcessorSource.Contains(
        TEXT("CommitPreparedParticleDiagnostics()")));

  TestFalse(TEXT("transient runtime particle fragment is physically deleted"),
    RuntimeFragmentHeader.Contains(TEXT("FCrowdMassParticleConstraintFragment")));

  TestFalse(TEXT("runtime movement trait excludes transient particle fragment"),
    RuntimeTraitSource.Contains(TEXT("FCrowdMassParticleConstraintFragment")));
  return true;
}

#endif
