#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoWorkerCombatExtension.h"
#include "MassCrowdWorkerCombatState.h"
#include "MassCrowdWorkerContracts.h"
#include "MassCrowdWorkerExchange.h"
#include "MassCrowdWorkerResultApply.h"
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

  FCrowdWorkerPayload MakeOwnerBarrierPayload(
    const uint32 Value,
    const uint32 SchemaId)
  {
    FCrowdWorkerPayload Payload;
    Payload.SchemaId = SchemaId;
    Payload.SchemaVersion = 1;
    Payload.Bytes.SetNumUninitialized(sizeof(Value));
    FMemory::Memcpy(Payload.Bytes.GetData(), &Value, sizeof(Value));
    Payload.RecalculateStableHash();
    return Payload;
  }

  FCrowdWorkerContractLimits MakeOwnerBarrierLimits()
  {
    FCrowdWorkerContractLimits Limits;
    Limits.MaxPayloadBytes = 64;
    Limits.MaxInputRecordsPerBatch = 8;
    Limits.MaxStatePatchesPerSlot = 8;
    Limits.MaxPendingOrderedEvents = 8;
    return Limits;
  }

  bool BuildOwnerBarrierBatch(FCrowdWorkerPublishedBatch& OutBatch)
  {
    const FCrowdWorkerContractLimits Limits = MakeOwnerBarrierLimits();
    FCrowdWorkerPublishedExchange Exchange;
    if (!Exchange.ResetQuiescent(7, Limits))
      return false;
    FCrowdWorkerStatePatch Patch;
    Patch.EntityRef = {1, 10, 1};
    Patch.Generation = 7;
    Patch.WorkerEpoch = 1;
    Patch.SourceInputSequence = 1;
    Patch.DirtyMask = CrowdWorkerRuntimeV2FieldMask(
      ECrowdWorkerField::Movement);
    Patch.StateFieldId =
      1 + static_cast<uint16>(ECrowdWorkerField::Movement);
    Patch.State.StateRevision = 1;
    Patch.State.Payload = MakeOwnerBarrierPayload(101, 4001);
    Patch.RecalculateStableHash();
    if (Exchange.AppendStatePatch(Patch)
        != ECrowdWorkerAppendResult::Appended)
      return false;

    FCrowdWorkerGameplayEvent Event;
    Event.EntityRef = Patch.EntityRef;
    Event.Generation = 7;
    Event.WorkerEpoch = 1;
    Event.SourceInputSequence = 1;
    Event.EventSequence = 1;
    Event.EventId = 5001;
    Event.Payload = MakeOwnerBarrierPayload(202, 5001);
    Event.RecalculateStableHash();
    if (Exchange.AppendOrderedEvent(Event)
        != ECrowdWorkerAppendResult::Appended)
      return false;

    FCrowdWorkerPublishMetadata Metadata;
    Metadata.Generation = 7;
    Metadata.PublishSequence = 1;
    Metadata.MinWorkerEpoch = 1;
    Metadata.MaxWorkerEpoch = 1;
    Metadata.LastAppliedInputSequence = 1;
    Metadata.PublishedSimulationTimeSeconds = 1.0 / 30.0;
    if (Exchange.TryPublishBuildingBatch(Metadata)
        != ECrowdWorkerPublishResult::Published)
      return false;
    const FCrowdWorkerPublishedBatch* Published = nullptr;
    if (Exchange.TryExchangePublishedBatch(7, 1, Published)
        != ECrowdWorkerExchangeResult::Exchanged
      || !Published)
      return false;
    OutBatch = *Published;
    return true;
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
  ZeroImpulseReactive.bMovementLocked = true;
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
  TestTrue(TEXT("movement-lock flag survives generic codec"),
    DecodedCombat.bMovementLocked);
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
  uint64 FrozenControlSemanticHash = 0;
  int32 TotalMeleeIntents = 0;
  int32 TotalDamage = 0;
  int32 PublishedCombatStates = 0;
  bool bObservedMovementLock = false;
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
    uint64 SemanticHash = 0;
    TestTrue(TEXT("mixed control semantic hash builds"),
      CalculateCrowdDemoWorkerProjectileControlSemanticHash(
        Control, SemanticHash));
    if (Step == 0)
      FrozenControlSemanticHash = SemanticHash;
    else
      TestEqual(TEXT("dynamic combat state does not revise control"),
        SemanticHash, FrozenControlSemanticHash);
    if (Step == 0)
    {
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
        Resources.CommitBuildingAtEpoch(1, ResourceEvents));
    }
    Context.WorkerEpoch = Step + 1;
    Context.AbsoluteSimulationTick = Step + 1;
    Context.LastAppliedInputSequence = Step + 1;
    Context.NextOrderedEventSequence = NextEventSequence;
    Context.SimulationTimeSeconds =
      (Step + 1) * HostInput.FixedStepSeconds;
    FCrowdWorkerDomainOutput Output;
    FCrowdWorkerWorkItem StepWork = Work;
    if (Step > 0)
      StepWork.ReasonMask = CrowdWorkerReasonMasks::CombatClock;
    TestTrue(TEXT("mixed combat step executes"),
      Executor.Execute(
        Context,
        TArray<FCrowdWorkerWorkItem>{StepWork}, Output));
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
      const bool bExpectedMovementLock =
        Mixed.bAlive
        && Mixed.AttackState.Phase
          == ECrowdDemoAttackPlannerPhase::Commit;
      TestEqual(TEXT("generic combat projects attack movement lock"),
        Combat.bMovementLocked, bExpectedMovementLock);
      bObservedMovementLock |= Combat.bMovementLocked;
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
  TestTrue(TEXT("mixed Worker publishes an attack movement lock"),
    bObservedMovementLock);
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
  FCrowdDemoPreparedRoundCommitAdapterAtomicityTest,
  "CrowdDemo.WorkerV2.WA8R.PreparedRoundCommitAdapterAtomicity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPreparedRoundCommitAdapterAtomicityTest::RunTest(
  const FString& Parameters)
{
  FCrowdWorkerPublishedBatch Batch;
  TestTrue(TEXT("owner barrier fixture builds"),
    BuildOwnerBarrierBatch(Batch));
  if (Batch.Generation == 0 || Batch.PublishSequence == 0)
    return false;
  const FCrowdWorkerContractLimits Limits = MakeOwnerBarrierLimits();
  const FCrowdStableEntityRef CurrentRefs[] = {{1, 10, 1}};
  auto PrepareProxy = [&](FCrowdWorkerResultApplyProxy& Proxy,
      FCrowdWorkerPreparedResultApply& Prepared)
  {
    return Proxy.ResetQuiescent(7, Limits)
      && Proxy.UpdateCurrentEntities(7, CurrentRefs)
      && Proxy.Prepare(Batch, Prepared)
        == ECrowdWorkerResultApplyResult::Applied;
  };

  FCrowdWorkerResultApplyProxy LifecycleProxy;
  FCrowdWorkerPreparedResultApply LifecyclePrepared;
  TestTrue(TEXT("lifecycle fault fixture prepares"),
    PrepareProxy(LifecycleProxy, LifecyclePrepared));
  const FCrowdWorkerResultCommitToken LifecycleToken =
    FCrowdWorkerResultCommitToken::FromPrepared(LifecyclePrepared);
  const uint64 LifecyclePublishWatermark = LifecycleProxy.GetMetrics()
    .LastConsumedPublishSequence;
  const uint64 LifecycleEventWatermark = LifecycleProxy.GetMetrics()
    .LastAppliedEventSequence;
  const FCrowdStableEntityRef ReusedRefs[] = {{1, 10, 2}};
  TestTrue(TEXT("lifecycle changes before final barrier"),
    LifecycleProxy.UpdateCurrentEntities(7, ReusedRefs));
  int32 LifecycleMassApplyCount = 0;
  int32 LifecycleSideEffectCount = 0;
  TestEqual(TEXT("final barrier rejects stale lifecycle view"),
    FCrowdWorkerResultOwnerCommitBarrier::Commit(
      LifecycleProxy, LifecyclePrepared, LifecycleToken,
      []() { return true; },
      [&]() { ++LifecycleMassApplyCount; },
      [&]() { ++LifecycleSideEffectCount; }),
    ECrowdWorkerResultOwnerCommitResult::RejectedProxyState);
  TestEqual(TEXT("lifecycle rejection does not write Mass"),
    LifecycleMassApplyCount, 0);
  TestEqual(TEXT("lifecycle rejection does not publish side effects"),
    LifecycleSideEffectCount, 0);
  TestEqual(TEXT("lifecycle rejection preserves publish watermark"),
    LifecycleProxy.GetMetrics().LastConsumedPublishSequence,
    LifecyclePublishWatermark);
  TestEqual(TEXT("lifecycle rejection preserves event watermark"),
    LifecycleProxy.GetMetrics().LastAppliedEventSequence,
    LifecycleEventWatermark);
  TestNull(TEXT("lifecycle rejection does not publish Dirty Batch"),
    LifecycleProxy.PeekDirtyBatch());

  FCrowdWorkerResultApplyProxy TokenProxy;
  FCrowdWorkerPreparedResultApply TokenPrepared;
  TestTrue(TEXT("stale token fixture prepares"),
    PrepareProxy(TokenProxy, TokenPrepared));
  FCrowdWorkerResultCommitToken StaleToken =
    FCrowdWorkerResultCommitToken::FromPrepared(TokenPrepared);
  ++StaleToken.BaseStableEntityViewRevision;
  int32 TokenMassApplyCount = 0;
  int32 TokenSideEffectCount = 0;
  TestEqual(TEXT("stale Commit Token is rejected completely"),
    FCrowdWorkerResultOwnerCommitBarrier::Commit(
      TokenProxy, TokenPrepared, StaleToken,
      []() { return true; },
      [&]() { ++TokenMassApplyCount; },
      [&]() { ++TokenSideEffectCount; }),
    ECrowdWorkerResultOwnerCommitResult::RejectedCandidate);
  TestEqual(TEXT("stale token does not write Mass"),
    TokenMassApplyCount, 0);
  TestEqual(TEXT("stale token does not publish side effects"),
    TokenSideEffectCount, 0);
  TestEqual(TEXT("stale token does not advance Proxy"),
    TokenProxy.GetMetrics().AppliedBatchCount, uint64{0});

  FCrowdWorkerResultApplyProxy FragmentProxy;
  FCrowdWorkerPreparedResultApply FragmentPrepared;
  TestTrue(TEXT("missing fragment fixture prepares"),
    PrepareProxy(FragmentProxy, FragmentPrepared));
  const FCrowdWorkerResultCommitToken FragmentToken =
    FCrowdWorkerResultCommitToken::FromPrepared(FragmentPrepared);
  int32 FragmentMassApplyCount = 0;
  int32 FragmentSideEffectCount = 0;
  TestEqual(TEXT("final fragment validation rejects before commit"),
    FCrowdWorkerResultOwnerCommitBarrier::Commit(
      FragmentProxy, FragmentPrepared, FragmentToken,
      []() { return false; },
      [&]() { ++FragmentMassApplyCount; },
      [&]() { ++FragmentSideEffectCount; }),
    ECrowdWorkerResultOwnerCommitResult::RejectedHostState);
  TestEqual(TEXT("fragment rejection does not write Mass"),
    FragmentMassApplyCount, 0);
  TestEqual(TEXT("fragment rejection does not commit Proxy"),
    FragmentProxy.GetMetrics().AppliedBatchCount, uint64{0});
  TestEqual(TEXT("fragment rejection does not publish side effects"),
    FragmentSideEffectCount, 0);

  FCrowdWorkerResultApplyProxy InvalidOwnerProxy;
  TestTrue(TEXT("invalid owner proxy initializes"),
    InvalidOwnerProxy.ResetQuiescent(7, Limits)
      && InvalidOwnerProxy.UpdateCurrentEntities(7, CurrentRefs));
  FCrowdWorkerPublishedBatch InvalidOwnerBatch = Batch;
  InvalidOwnerBatch.StatePatches[0].DirtyMask = 1ull << 63;
  InvalidOwnerBatch.StatePatches[0].RecalculateStableHash();
  InvalidOwnerBatch.RecalculateStableHash();
  FCrowdWorkerPreparedResultApply InvalidOwnerPrepared;
  TestEqual(TEXT("illegal field owner fails during Prepare"),
    InvalidOwnerProxy.Prepare(InvalidOwnerBatch, InvalidOwnerPrepared),
    ECrowdWorkerResultApplyResult::RejectedOwnerMask);
  TestFalse(TEXT("illegal owner never creates a commit candidate"),
    InvalidOwnerPrepared.IsValid());

  FCrowdWorkerResultApplyProxy DuplicateFieldProxy;
  TestTrue(TEXT("duplicate field proxy initializes"),
    DuplicateFieldProxy.ResetQuiescent(7, Limits)
      && DuplicateFieldProxy.UpdateCurrentEntities(7, CurrentRefs));
  FCrowdWorkerPublishedBatch DuplicateFieldBatch = Batch;
  const FCrowdWorkerStatePatch DuplicatePatch =
    DuplicateFieldBatch.StatePatches[0];
  DuplicateFieldBatch.StatePatches.Add(DuplicatePatch);
  DuplicateFieldBatch.RecalculateStableHash();
  FCrowdWorkerPreparedResultApply DuplicateFieldPrepared;
  const ECrowdWorkerResultApplyResult DuplicateFieldResult =
    DuplicateFieldProxy.Prepare(
      DuplicateFieldBatch, DuplicateFieldPrepared);
  TestTrue(TEXT("duplicate entity-field fails during Prepare"),
    DuplicateFieldResult != ECrowdWorkerResultApplyResult::Applied
      && DuplicateFieldResult
        != ECrowdWorkerResultApplyResult::AppliedEmpty);
  TestFalse(TEXT("duplicate entity-field never reaches final barrier"),
    DuplicateFieldPrepared.IsValid());

  FCrowdWorkerResultApplyProxy SuccessProxy;
  FCrowdWorkerPreparedResultApply SuccessPrepared;
  TestTrue(TEXT("success fixture prepares"),
    PrepareProxy(SuccessProxy, SuccessPrepared));
  const FCrowdWorkerResultCommitToken SuccessToken =
    FCrowdWorkerResultCommitToken::FromPrepared(SuccessPrepared);
  const int32 PreparedMassPlanBuildCount = 1;
  int32 PreparedMassPlanApplyCount = 0;
  int32 SuccessSideEffectCount = 0;
  bool bProxyCommittedBeforeSideEffects = false;
  TestEqual(TEXT("owner barrier commits success path"),
    FCrowdWorkerResultOwnerCommitBarrier::Commit(
      SuccessProxy, SuccessPrepared, SuccessToken,
       [&]()
       {
         return PreparedMassPlanBuildCount == 1;
       },
      [&]() { ++PreparedMassPlanApplyCount; },
      [&]()
      {
         bProxyCommittedBeforeSideEffects =
           SuccessProxy.GetMetrics().AppliedBatchCount == 1;
         ++SuccessSideEffectCount;
      }),
    ECrowdWorkerResultOwnerCommitResult::Committed);
  TestEqual(TEXT("Prepared Mass Plan is built once"),
    PreparedMassPlanBuildCount, 1);
  TestEqual(TEXT("Prepared Mass Plan is applied once"),
    PreparedMassPlanApplyCount, 1);
  TestEqual(TEXT("Proxy commits once"),
    SuccessProxy.GetMetrics().AppliedBatchCount, uint64{1});
  TestTrue(TEXT("side effects observe committed Mass and Proxy"),
    bProxyCommittedBeforeSideEffects
      && PreparedMassPlanApplyCount == 1);
  TestEqual(TEXT("side effects publish once"),
    SuccessSideEffectCount, 1);
  TestEqual(TEXT("ordered event applies once"),
    SuccessProxy.GetMetrics().AppliedEventCount, uint64{1});
  TestEqual(TEXT("ordered event watermark is exact"),
    SuccessProxy.GetMetrics().LastAppliedEventSequence, uint64{1});
  const FCrowdWorkerResultApplyDirtyBatch* DirtyBatch =
    SuccessProxy.PeekDirtyBatch();
  TestNotNull(TEXT("successful Proxy commit publishes Dirty Batch"),
    DirtyBatch);
  if (DirtyBatch)
  {
    const uint64 DirtyPublishSequence = DirtyBatch->PublishSequence;
    TestTrue(TEXT("Dirty Batch ACK succeeds once"),
      SuccessProxy.AcknowledgeDirtyBatch(DirtyPublishSequence));
    TestFalse(TEXT("Dirty Batch duplicate ACK is rejected"),
      SuccessProxy.AcknowledgeDirtyBatch(DirtyPublishSequence));
  }
  TestEqual(TEXT("Dirty Batch ACK count is one"),
    SuccessProxy.GetMetrics().ConsumedDirtyBatchCount, uint64{1});
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPersistentWorkerProductionStructureTest,
  "CrowdDemo.Architecture.PersistentWorkerProductionStructure",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPersistentWorkerProductionStructureTest::RunTest(
  const FString& Parameters)
{
  FString ProcessorSource;
  FString ProcessorHeader;
  FString PipelineSource;
  FString PipelineHeader;
  FString OwnerBarrierSource;
  TestTrue(TEXT("processor source is readable"),
    FFileHelper::LoadFileToString(ProcessorSource, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp"))));
  TestTrue(TEXT("processor header is readable"),
    FFileHelper::LoadFileToString(ProcessorHeader, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.h"))));
  TestTrue(TEXT("pipeline source is readable"),
    FFileHelper::LoadFileToString(PipelineSource, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"))));
  TestTrue(TEXT("pipeline header is readable"),
    FFileHelper::LoadFileToString(PipelineHeader, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h"))));
  TestTrue(TEXT("Runtime Owner Barrier source is readable"),
    FFileHelper::LoadFileToString(OwnerBarrierSource, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/Private/MassCrowdWorkerResultApply.cpp"))));

  int32 ProcessorClassCount = 0;
  int32 SearchFrom = 0;
  const FString ProcessorMarker = TEXT("public UMassProcessor");
  while (true)
  {
    const int32 Found = ProcessorHeader.Find(
      ProcessorMarker, ESearchCase::CaseSensitive,
      ESearchDir::FromStart, SearchFrom);
    if (Found == INDEX_NONE) break;
    ++ProcessorClassCount;
    SearchFrom = Found + ProcessorMarker.Len();
  }
  TestEqual(TEXT("Demo runtime owns exactly InputSync and ResultApply processors"),
    ProcessorClassCount, 2);

  const FString ProductionSource = ProcessorSource + PipelineSource
    + ProcessorHeader + PipelineHeader;
  const TCHAR* RetiredSymbols[] = {
    TEXT("FCrowdDemoRoundWorkBatch"),
    TEXT("BeginBoundaryTransaction"),
    TEXT("TryPrepareRoundApply"),
    TEXT("BoundaryOrchestrator"),
    TEXT("ECrowdBoundaryTransactionState"),
    TEXT("FCrowdDemoPreparedMovementBoundaryCommit"),
    TEXT("FCrowdDemoPreparedTargetResourcePlan"),
    TEXT("FCrowdDemoPreparedParticleDiagnosticCommit"),
    TEXT("ConsumeBoundaryMovementWork"),
    TEXT("ConsumeBoundaryParticleWork"),
    TEXT("ConsumeBoundaryFacingWork"),
    TEXT("FCrowdDemoRoundPostFinalizeMetricsStage"),
    TEXT("FCrowdDemoRoundAuthorityCommitStage"),
    TEXT("FCrowdDemoRoundClientPredictionCommitStage")
  };
  for (const TCHAR* Symbol : RetiredSymbols)
  {
    TestFalse(FString::Printf(TEXT("retired production symbol %s is absent"),
      Symbol), ProductionSource.Contains(Symbol));
  }
  TestTrue(TEXT("bootstrap preparation is one-shot and synchronous"),
    PipelineSource.Contains(TEXT("FCrowdDemoBootstrapSynchronousGraph"))
      && PipelineSource.Contains(TEXT("BeginWorkerBootstrapPreparation("))
      && PipelineSource.Contains(TEXT("SubmitPreparedWorkerBootstrapInput()"))
      && !PipelineSource.Contains(TEXT("CrowdDemoRoundWork\"")));
  TestTrue(TEXT("server commit remains behind Runtime Owner Barrier"),
    ProcessorSource.Contains(TEXT(
      "FCrowdWorkerResultOwnerCommitBarrier::Commit("))
      && ProcessorSource.Contains(TEXT(
        "ApplyValidatedWorkerMassDirtyPlan("))
      && ProcessorSource.Contains(TEXT(
        "MarkCurrentStepWorkerDirtyMassApplied(")));
  const int32 OwnerCommit = ProcessorSource.Find(TEXT(
    "FCrowdWorkerResultOwnerCommitBarrier::Commit("));
  const int32 Checkpoint = ProcessorSource.Find(TEXT(
    "ExecuteRoundCheckpointPublisher(EntityManager, Context)"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart, OwnerCommit);
  TestTrue(TEXT("checkpoint publication follows Worker owner commit"),
    OwnerCommit != INDEX_NONE && Checkpoint > OwnerCommit);
  TestTrue(TEXT("ordinary Production submits intent without rebuilding Round DAG"),
    PipelineSource.Contains(TEXT("TrySubmitFullWorkerProductionIntent()"))
      && PipelineSource.Contains(TEXT(
        "FCrowdDemoWorkerInputSync::SubmitIntentBatch(")));
  TestTrue(TEXT("Runtime barrier commits host write before Proxy and side effects"),
    OwnerBarrierSource.Contains(TEXT("HostApplyNoFail()"))
      && OwnerBarrierSource.Contains(TEXT("CommitPreparedValidated(")));
  return true;
}


#endif
