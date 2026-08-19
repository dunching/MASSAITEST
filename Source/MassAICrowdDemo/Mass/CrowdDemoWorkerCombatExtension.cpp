#include "Mass/CrowdDemoWorkerCombatExtension.h"

#include "CrowdDemoVatShowcasePlanner.h"
#include "MassCrowdWorkerMovementDomain.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
  constexpr int32 MaxAgents = 100000;

  void SerializeRef(FArchive& Ar, FCrowdStableEntityRef& Ref)
  {
    Ar << Ref.ProviderId;
    Ar << Ref.StableEntityId;
    Ar << Ref.LifecycleSerial;
  }

  template<typename T>
  void SerializeEnum8(FArchive& Ar, T& Value)
  {
    uint8 Raw = static_cast<uint8>(Value);
    Ar << Raw;
    if (Ar.IsLoading()) Value = static_cast<T>(Raw);
  }

  void SerializeCombatState(
    FArchive& Ar,
    FCrowdDemoCombatAgentState& State)
  {
    Ar << State.AgentId;
    Ar << State.LifecycleSerial;
    Ar << State.Health;
    Ar << State.MaxHealth;
    SerializeEnum8(Ar, State.LifecycleState);
    uint8 Alive = State.bAlive ? 1 : 0;
    Ar << Alive;
    if (Ar.IsLoading()) State.bAlive = Alive != 0;
    SerializeEnum8(Ar, State.BusinessState);
    Ar << State.BusinessStateRevision;
    Ar << State.BusinessStateEnterFixedStep;
    Ar << State.TargetAgentId;
    Ar << State.TargetLifecycleSerial;
    SerializeEnum8(Ar, State.AttackPhase);
    Ar << State.AttackPhaseEnterFixedStep;
    Ar << State.CooldownEndFixedStep;
    Ar << State.LockedTargetAgentId;
    Ar << State.LockedTargetLifecycleSerial;
    Ar << State.LockedTargetLocation;
    Ar << State.FireSequence;
    uint8 FireIssued = State.bFireRequestIssued ? 1 : 0;
    Ar << FireIssued;
    if (Ar.IsLoading()) State.bFireRequestIssued = FireIssued != 0;
    SerializeEnum8(Ar, State.ReactiveMode);
    Ar << State.HorizontalReactiveVelocity;
    Ar << State.VerticalReactiveVelocityCmps;
    Ar << State.ReactiveStartFixedStep;
    Ar << State.ReactiveEndFixedStep;
    Ar << State.ReactiveRevision;
    SerializeEnum8(Ar, State.RestoreBusinessState);
    Ar << State.ApexCount;
    Ar << State.LandingCount;
    Ar << State.HitFlashRevision;
    Ar << State.HitFlashStartServerTimeSeconds;
    Ar << State.HitFlashDurationSeconds;
    Ar << State.HitFlashProfileKey;
    Ar << State.HitFlashPeakIntensity;
    Ar << State.LastConsumedHitEventId;
    SerializeEnum8(Ar, State.VisualState);
    Ar << State.VisualRevision;
    Ar << State.VisualStateStartServerTimeSeconds;
    Ar << State.VisualPhaseSeed;
  }

  void SerializeAttackSettings(
    FArchive& Ar,
    FCrowdDemoRangedCombatSettings& Value)
  {
    Ar << Value.bEnabled;
    Ar << Value.ShooterCount;
    Ar << Value.WindupFixedSteps;
    Ar << Value.RecoveryFixedSteps;
    Ar << Value.CooldownFixedSteps;
    Ar << Value.ProjectileSpeedCmps;
    Ar << Value.ProjectileRadiusCm;
    Ar << Value.ProjectileLifetimeFixedSteps;
    Ar << Value.ProjectilePierceCount;
    Ar << Value.MuzzleForwardOffsetCm;
    Ar << Value.Damage;
    Ar << Value.HorizontalImpulseCmps;
    Ar << Value.VerticalImpulseCmps;
    Ar << Value.PositionQuantumCm;
    Ar << Value.VelocityQuantumCmps;
  }

  void SerializeHitSettings(
    FArchive& Ar,
    FCrowdDemoHitResponseSettings& Value)
  {
    Ar << Value.MaximumHorizontalImpulseCmps;
    Ar << Value.MaximumVerticalImpulseCmps;
    Ar << Value.ReactiveDurationFixedSteps;
    Ar << Value.LandingRecoveryFixedSteps;
    Ar << Value.HitFlashDurationSeconds;
    Ar << Value.HitFlashPeakIntensity;
    Ar << Value.GravityCmps2;
    Ar << Value.GroundZ;
    Ar << Value.FixedStepSeconds;
  }

  void SerializeInjectedHitCommand(
    FArchive& Ar,
    FCrowdDemoWorkerInjectedHitCommand& Value)
  {
    Ar << Value.ApplyFixedStep;
    SerializeRef(Ar, Value.TargetEntity);
    Ar << Value.HitEventId;
    Ar << Value.Damage;
    Ar << Value.HorizontalImpulseCmps;
    Ar << Value.VerticalImpulseCmps;
    Ar << Value.HitFlashProfileKey;
  }

  bool IsFiniteInjectedHitCommand(
    const FCrowdDemoWorkerInjectedHitCommand& Value)
  {
    return Value.ApplyFixedStep >= 0
      && Value.TargetEntity.IsValid()
      && Value.HitEventId != 0
      && FMath::IsFinite(Value.Damage)
      && FMath::IsFinite(Value.HorizontalImpulseCmps)
      && FMath::IsFinite(Value.VerticalImpulseCmps)
      && Value.Damage >= 0.0f
      && Value.HorizontalImpulseCmps >= 0.0f
      && Value.VerticalImpulseCmps >= 0.0f
      && Value.HitFlashProfileKey != 0;
  }

  void SerializeProjectileSummary(
    FArchive& Ar,
    FCrowdDemoProjectileStepSummary& Value)
  {
    uint8 Valid = Value.bValid ? 1 : 0;
    Ar << Valid;
    if (Ar.IsLoading()) Value.bValid = Valid != 0;
    Ar << Value.TargetAcquiredCount;
    Ar << Value.CompletedWindupCount;
    Ar << Value.SpawnedCount;
    Ar << Value.ActiveCount;
    Ar << Value.ImpactedCount;
    Ar << Value.ExpiredCount;
    Ar << Value.DuplicateFireCount;
    Ar << Value.DuplicateHitCount;
    Ar << Value.InvalidTargetLifecycleCount;
    Ar << Value.InvalidProjectileCount;
    Ar << Value.EnvironmentImpactCount;
    Ar << Value.BroadphaseCandidateCount;
    Ar << Value.SweepTestCount;
    Ar << Value.AttackStateHash;
    Ar << Value.ProjectileStateHash;
    Ar << Value.EventHash;
  }

  void SerializeHitSummary(
    FArchive& Ar,
    FCrowdDemoHitResponseSummary& Value)
  {
    uint8 Valid = Value.bValid ? 1 : 0;
    Ar << Valid;
    if (Ar.IsLoading()) Value.bValid = Valid != 0;
    Ar << Value.InputHitCount;
    Ar << Value.AppliedHitCount;
    Ar << Value.DuplicateHitCount;
    Ar << Value.StaleLifecycleCount;
    Ar << Value.MissingTargetCount;
    Ar << Value.AlreadyDeadCount;
    Ar << Value.DamageAppliedAgentCount;
    Ar << Value.ReactiveAgentCount;
    Ar << Value.DeathCount;
    Ar << Value.StableHash;
  }

  void SerializeAttackProfile(
    FArchive& Ar,
    FCrowdDemoAttackProfileV1& Value)
  {
    Ar << Value.ProfileId;
    Ar << Value.PayloadTypeId;
    Ar << Value.EffectProfileId;
    SerializeEnum8(Ar, Value.Archetype);
    Ar << Value.WindupFixedSteps;
    Ar << Value.RecoveryFixedSteps;
    Ar << Value.CooldownFixedSteps;
    Ar << Value.MinimumDistanceCm;
    Ar << Value.MaximumDistanceCm;
    Ar << Value.QueryRadiusCm;
    Ar << Value.MuzzleForwardOffsetCm;
    Ar << Value.ProjectileSpeedCmps;
    Ar << Value.PositionQuantumCm;
    Ar << Value.VelocityQuantumCmps;
    Ar << Value.Damage;
  }

  void SerializeAttackState(
    FArchive& Ar,
    FCrowdDemoAttackState& Value)
  {
    SerializeEnum8(Ar, Value.Phase);
    Ar << Value.PhaseEnterFixedStep;
    Ar << Value.CooldownEndFixedStep;
    SerializeRef(Ar, Value.TargetRef);
    SerializeRef(Ar, Value.LockedTargetRef);
    Ar << Value.LockedTargetLocation;
    Ar << Value.Revision;
    Ar << Value.FireSequence;
    uint8 CommitIssued = Value.bCommitIssued ? 1 : 0;
    Ar << CommitIssued;
    if (Ar.IsLoading()) Value.bCommitIssued = CommitIssued != 0;
  }

  void SerializeMixedAgent(
    FArchive& Ar,
    FCrowdDemoWorkerMixedCombatAgent& Value)
  {
    SerializeRef(Ar, Value.EntityRef);
    Ar << Value.FactionId;
    Ar << Value.NavLayer;
    Ar << Value.AttackProfileId;
    Ar << Value.Position;
    Ar << Value.Velocity;
    Ar << Value.Facing;
    Ar << Value.Health;
    SerializeAttackState(Ar, Value.AttackState);
  }

  void SerializeMixedPlanSummary(
    FArchive& Ar,
    FCrowdDemoAttackPlanSummary& Value)
  {
    uint8 Valid = Value.bValid ? 1 : 0;
    Ar << Valid;
    if (Ar.IsLoading()) Value.bValid = Valid != 0;
    Ar << Value.TargetAcquiredCount;
    Ar << Value.CompletedWindupCount;
    Ar << Value.InvalidTargetLifecycleCount;
    Ar << Value.OutOfRangeCount;
  }

  bool IsFiniteMixedAgent(
    const FCrowdDemoWorkerMixedCombatAgent& Agent)
  {
    return Agent.EntityRef.IsValid()
      && Agent.AttackProfileId != 0
      && !Agent.Position.ContainsNaN()
      && !Agent.Velocity.ContainsNaN()
      && !Agent.Facing.ContainsNaN()
      && Agent.Health >= 0
      && Agent.AttackState.IsValid();
  }

  void SerializeAgent(
    FArchive& Ar,
    FCrowdDemoRangedCombatAgent& Agent)
  {
    SerializeRef(Ar, Agent.EntityRef);
    Ar << Agent.AgentId;
    Ar << Agent.LifecycleSerial;
    Ar << Agent.FormationIndex;
    Ar << Agent.FactionId;
    Ar << Agent.NavLayer;
    Ar << Agent.Position;
    Ar << Agent.Velocity;
    Ar << Agent.RadiusCm;
    uint8 Alive = Agent.bAlive ? 1 : 0;
    Ar << Alive;
    if (Ar.IsLoading()) Agent.bAlive = Alive != 0;
    SerializeCombatState(Ar, Agent.Combat);
  }

  bool IsFiniteAgent(const FCrowdDemoRangedCombatAgent& Agent)
  {
    return Agent.EntityRef.IsValid()
      && Agent.AgentId != INDEX_NONE
      && Agent.LifecycleSerial
        == static_cast<int32>(Agent.EntityRef.LifecycleSerial)
      && !Agent.Position.ContainsNaN()
      && !Agent.Velocity.ContainsNaN()
      && FMath::IsFinite(Agent.RadiusCm)
      && Agent.RadiusCm > 0.0f;
  }

  class FCrowdDemoWorkerCombatExtension final
    : public ICrowdWorkerCombatExtension
  {
  public:
    bool BeginStep(
      const FCrowdWorkerDomainContext& Context,
      const FCrowdWorkerPayload& HostInput,
      const bool bReplaceState,
      FCrowdProjectileBoundaryInput& InOutProjectileInput,
      TArray<FCrowdImpactFact>& OutImmediateImpacts) override
    {
      OutImmediateImpacts.Reset();
      if (HostInput.SchemaId
          == FCrowdDemoWorkerMixedCombatHostInputCodec::SchemaId)
      {
        return BeginMixedStep(
          Context, HostInput, bReplaceState,
          InOutProjectileInput, OutImmediateImpacts);
      }
      FCrowdDemoWorkerCombatHostInput Input;
      if (!FCrowdDemoWorkerCombatHostInputCodec::Decode(
          HostInput, Input)
        || !Context.EntityStates)
        return false;
      // ProjectileControl is a versioned configuration snapshot. The Worker
      // time wheel owns continuation time, so a timer wakeup must not require
      // a new GT-authored HostInput merely to advance one fixed step.
      Input.FixedStepIndex = static_cast<int32>(
        InOutProjectileInput.FixedStepIndex);
      Input.ServerTimeSeconds =
        InOutProjectileInput.ServerTimeSeconds;
      Input.FixedStepSeconds =
        InOutProjectileInput.FixedStepSeconds;
      CurrentKind = EHostKind::Round;
      if (Generation != Context.Generation || bReplaceState)
      {
        Generation = Context.Generation;
        Agents = Input.Agents;
        if (Input.bVatShowcase)
        {
          for (FCrowdDemoRangedCombatAgent& Agent : Agents)
          {
            Agent.Combat.BusinessState =
              static_cast<ECrowdDemoBusinessState>(
                FCrowdDemoVatShowcasePlanner::ResolveInitialState(
                  Agent.FormationIndex));
            Agent.Combat.AttackPhase =
              Agent.Combat.BusinessState
                == ECrowdDemoBusinessState::Attacking
              ? ECrowdDemoAttackPhase::Windup
              : ECrowdDemoAttackPhase::None;
            Agent.Combat.BusinessStateRevision = 1;
            Agent.Combat.BusinessStateEnterFixedStep = 0;
          }
        }
      }
      else
      {
        if (Agents.Num() != Input.Agents.Num()) return false;
        TMap<FCrowdStableEntityRef,
          const FCrowdDemoRangedCombatAgent*> ExternalByRef;
        for (const FCrowdDemoRangedCombatAgent& External :
          Input.Agents)
          ExternalByRef.Add(External.EntityRef, &External);
        for (FCrowdDemoRangedCombatAgent& Agent : Agents)
        {
          const FCrowdDemoRangedCombatAgent* const* External =
            ExternalByRef.Find(Agent.EntityRef);
          if (!External) return false;
          Agent.FormationIndex = (*External)->FormationIndex;
          Agent.FactionId = (*External)->FactionId;
          Agent.NavLayer = (*External)->NavLayer;
          Agent.RadiusCm = (*External)->RadiusCm;
          // Presentation resolves after the prior Combat stage. It is an
          // explicit read-only external projection, not a Combat writer.
          Agent.Combat.VisualState =
            (*External)->Combat.VisualState;
          Agent.Combat.VisualRevision =
            (*External)->Combat.VisualRevision;
          Agent.Combat.VisualStateStartServerTimeSeconds =
            (*External)->Combat.VisualStateStartServerTimeSeconds;
          Agent.Combat.VisualPhaseSeed =
            (*External)->Combat.VisualPhaseSeed;
        }
      }
      for (FCrowdDemoRangedCombatAgent& Agent : Agents)
      {
        const FCrowdWorkerDirtyStateRecord* Movement =
          Context.EntityStates->Find(
            Agent.EntityRef, ECrowdWorkerField::Facing);
        if (!Movement)
          Movement = Context.EntityStates->Find(
            Agent.EntityRef, ECrowdWorkerField::Movement);
        if (Movement)
        {
          FCrowdWorkerMovementState MovementState;
          if (!FCrowdWorkerMovementStateCodec::Decode(
              Movement->Payload, MovementState))
            return false;
          Agent.Position = MovementState.Position;
          Agent.Velocity = MovementState.Velocity;
        }
        Agent.bAlive = Agent.Combat.bAlive;
      }
      TArray<FCrowdProjectileSpawnRequest> SpawnRequests;
      FCrowdDemoProjectileStepSummary Summary;
      if (Input.bVatShowcase)
      {
        Summary.bValid = true;
      }
      else if (!FCrowdDemoProjectileAdapters::BuildRangedAttackPlan(
        Input.RoundId, Input.FixedStepIndex,
        Input.AttackSettings, Agents,
        SpawnRequests, Summary))
      {
        return false;
      }
      if (!FCrowdDemoProjectileAdapters::BuildTargetSnapshots(
          Input.FixedStepSeconds, Agents,
          InOutProjectileInput.Targets))
        return false;
      InOutProjectileInput.SpawnRequests = MoveTemp(SpawnRequests);
      CurrentAttackSummary = Summary;
      CurrentInput = MoveTemp(Input);
      return true;
    }

    bool FinishStep(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdHitFact> Hits,
      TArray<FCrowdWorkerCombatExtensionPatch>& OutPatches,
      FCrowdWorkerPayload& OutHostResult) override
    {
      if (CurrentKind == EHostKind::Mixed)
      {
        return FinishMixedStep(
          Context, Hits, OutPatches, OutHostResult);
      }
      if (Generation != Context.Generation
        || CurrentInput.FixedStepIndex < 0)
        return false;
      TArray<FCrowdDemoHitFact> DemoHits;
      if (!FCrowdDemoProjectileAdapters::BuildDemoHitFacts(
          Hits, Agents, DemoHits))
        return false;
      if (CurrentInput.bVatShowcase)
      {
        TMap<FCrowdStableEntityRef,
          const FCrowdDemoRangedCombatAgent*> AgentByRef;
        for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
          AgentByRef.Add(Agent.EntityRef, &Agent);
        for (const FCrowdDemoWorkerInjectedHitCommand& Command :
          CurrentInput.InjectedHitCommands)
        {
          if (Command.ApplyFixedStep
              != CurrentInput.FixedStepIndex)
            continue;
          const FCrowdDemoRangedCombatAgent* const* Target =
            AgentByRef.Find(Command.TargetEntity);
          if (!Target) return false;
          FCrowdDemoHitFact& Fact = DemoHits.AddDefaulted_GetRef();
          Fact.HitEventId = Command.HitEventId;
          Fact.ApplyFixedStep = Command.ApplyFixedStep;
          Fact.TargetAgentId = (*Target)->AgentId;
          Fact.TargetLifecycleSerial = (*Target)->LifecycleSerial;
          Fact.HitPosition = (*Target)->Position;
          Fact.HitDirection = FVector::ForwardVector;
          Fact.Damage = Command.Damage;
          Fact.HorizontalImpulseCmps =
            Command.HorizontalImpulseCmps;
          Fact.VerticalImpulseCmps =
            Command.VerticalImpulseCmps;
          Fact.HitFlashProfileKey = Command.HitFlashProfileKey;
        }
      }
      TArray<FCrowdDemoCombatAgentState> CombatStates;
      for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
        CombatStates.Add(Agent.Combat);
      FCrowdDemoHitResponseSummary HitSummary;
      FCrowdDemoCombatStateKernel::ResolveHitFacts(
        CurrentInput.FixedStepIndex,
        CurrentInput.ServerTimeSeconds,
        DemoHits, CurrentInput.HitSettings,
        CombatStates, HitSummary);
      if (!HitSummary.bValid) return false;
      CombatStates.Sort([](const auto& A, const auto& B)
      {
        return A.AgentId < B.AgentId;
      });
      Agents.Sort([](const auto& A, const auto& B)
      {
        return A.AgentId < B.AgentId;
      });
      if (CombatStates.Num() != Agents.Num()) return false;
      OutPatches.Reset();
      OutPatches.Reserve(Agents.Num());
      for (int32 Index = 0; Index < Agents.Num(); ++Index)
      {
        if (CombatStates[Index].AgentId != Agents[Index].AgentId)
          return false;
        Agents[Index].Combat = CombatStates[Index];
        Agents[Index].bAlive = CombatStates[Index].bAlive;
        const FCrowdDemoReactiveMotionStepResult Reactive =
          FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(
            CurrentInput.FixedStepIndex,
            Agents[Index].Position.Z,
            CurrentInput.HitSettings,
            Agents[Index].Combat);
        if (!Reactive.bValid) return false;
        FCrowdWorkerCombatExtensionPatch& Patch =
          OutPatches.AddDefaulted_GetRef();
        Patch.EntityRef = Agents[Index].EntityRef;
        Patch.State.SourceFixedStep =
          CurrentInput.FixedStepIndex;
        Patch.State.bAlive = Agents[Index].Combat.bAlive;
        Patch.State.bReactiveActive =
          Agents[Index].Combat.ReactiveMode
            != ECrowdDemoReactiveMotionMode::None;
        Patch.State.HorizontalReactiveVelocity =
          Patch.State.bReactiveActive
            ? Reactive.HorizontalVelocity : FVector::ZeroVector;
        Patch.State.ProposedZ = Patch.State.bReactiveActive
          ? Reactive.NewZ : 0.0f;
        Patch.State.VerticalVelocityCmps =
          Patch.State.bReactiveActive
            ? Reactive.NewVerticalVelocityCmps : 0.0f;
        Patch.State.LastConsumedHitEventId =
          Agents[Index].Combat.LastConsumedHitEventId;
        if (!FCrowdDemoWorkerCombatStatePayloadCodec::Encode(
          Agents[Index].Combat, Patch.State.HostState))
          return false;
      }
      FCrowdDemoWorkerCombatHostResult Result;
      Result.FixedStepIndex = CurrentInput.FixedStepIndex;
      Result.AttackSummary = CurrentAttackSummary;
      Result.HitSummary = HitSummary;
      return FCrowdDemoWorkerCombatHostResultCodec::Encode(
        Result, OutHostResult);
    }

    bool ApplyAuthorityCorrection(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdWorkerDirtyStateRecord> Records) override
    {
      if (Context.Generation == 0)
        return false;
      const bool bHasCombatCorrection = Records.ContainsByPredicate(
        [](const FCrowdWorkerDirtyStateRecord& Record)
        {
          return Record.Field == ECrowdWorkerField::Combat;
        });
      if (!bHasCombatCorrection)
        return true;
      if (!Context.EntityStates)
        return false;
      if (CurrentKind == EHostKind::Round)
      {
        for (FCrowdDemoRangedCombatAgent& Agent : Agents)
        {
          const FCrowdWorkerDirtyStateRecord* Record =
            Context.EntityStates->Find(
              Agent.EntityRef, ECrowdWorkerField::Combat);
          FCrowdWorkerCombatState State;
          FCrowdDemoCombatAgentState HostState;
          if (!Record
            || !FCrowdWorkerCombatStateCodec::Decode(
              Record->Payload, State)
            || !FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
              State.HostState, HostState))
            return false;
          Agent.Combat = MoveTemp(HostState);
          Agent.bAlive = State.bAlive;
        }
      }
      else if (CurrentKind == EHostKind::Mixed)
      {
        for (FCrowdDemoWorkerMixedCombatAgent& Agent : MixedAgents)
        {
          const FCrowdWorkerDirtyStateRecord* Record =
            Context.EntityStates->Find(
              Agent.EntityRef, ECrowdWorkerField::Combat);
          FCrowdWorkerCombatState State;
          FCrowdDemoWorkerMixedCombatState HostState;
          if (!Record
            || !FCrowdWorkerCombatStateCodec::Decode(
              Record->Payload, State)
            || !FCrowdDemoWorkerMixedCombatStateCodec::Decode(
              State.HostState, HostState))
            return false;
          Agent.Health = HostState.Health;
          Agent.AttackState = MoveTemp(HostState.AttackState);
        }
      }
      Generation = Context.Generation;
      return true;
    }

  private:
    enum class EHostKind : uint8
    {
      None = 0,
      Round,
      Mixed
    };

    bool BeginMixedStep(
      const FCrowdWorkerDomainContext& Context,
      const FCrowdWorkerPayload& HostInput,
      const bool bReplaceState,
      FCrowdProjectileBoundaryInput& InOutProjectileInput,
      TArray<FCrowdImpactFact>& OutImmediateImpacts)
    {
      FCrowdDemoWorkerMixedCombatHostInput Input;
      if (!FCrowdDemoWorkerMixedCombatHostInputCodec::Decode(
          HostInput, Input)
        || !Context.EntityStates)
        return false;
      Input.FixedStepIndex = static_cast<int32>(
        InOutProjectileInput.FixedStepIndex);
      Input.FixedStepSeconds =
        InOutProjectileInput.FixedStepSeconds;
      if (Generation != Context.Generation || bReplaceState)
      {
        Generation = Context.Generation;
        MixedAgents = Input.Agents;
      }
      else
      {
        if (MixedAgents.Num() != Input.Agents.Num()) return false;
        TMap<FCrowdStableEntityRef,
          const FCrowdDemoWorkerMixedCombatAgent*> ExternalByRef;
        for (const auto& External : Input.Agents)
          ExternalByRef.Add(External.EntityRef, &External);
        for (auto& Agent : MixedAgents)
        {
          const auto* const* External =
            ExternalByRef.Find(Agent.EntityRef);
          if (!External) return false;
          Agent.FactionId = (*External)->FactionId;
          Agent.NavLayer = (*External)->NavLayer;
          Agent.AttackProfileId = (*External)->AttackProfileId;
          if (Context.RuntimeMode
            != ECrowdWorkerRuntimeV2Mode::Production)
          {
            // During independent Combat shadow/canary migration, Movement is
            // still a Legacy-owned external fact. Keep combat state
            // persistent, but refresh kinematics from the frozen host input.
            Agent.Position = (*External)->Position;
            Agent.Velocity = (*External)->Velocity;
            Agent.Facing = (*External)->Facing;
          }
        }
      }
      if (Context.RuntimeMode
        == ECrowdWorkerRuntimeV2Mode::Production)
      {
        for (auto& Agent : MixedAgents)
        {
          const FCrowdWorkerDirtyStateRecord* Movement =
            Context.EntityStates->Find(
              Agent.EntityRef, ECrowdWorkerField::Facing);
          if (!Movement)
            Movement = Context.EntityStates->Find(
              Agent.EntityRef, ECrowdWorkerField::Movement);
          if (Movement)
          {
            FCrowdWorkerMovementState MovementState;
            if (!FCrowdWorkerMovementStateCodec::Decode(
                Movement->Payload, MovementState))
              return false;
            Agent.Position = MovementState.Position;
            Agent.Velocity = MovementState.Velocity;
            Agent.Facing =
              FRotator(0.0f, MovementState.YawDegrees, 0.0f).
                Vector();
          }
        }
      }
      TArray<FCrowdDemoAttackAgent> PlannerAgents;
      PlannerAgents.Reserve(MixedAgents.Num());
      for (const auto& Source : MixedAgents)
      {
        FCrowdDemoAttackAgent& Agent =
          PlannerAgents.AddDefaulted_GetRef();
        Agent.EntityRef = Source.EntityRef;
        Agent.FactionId = Source.FactionId;
        Agent.NavLayer = Source.NavLayer;
        Agent.AttackProfileId = Source.AttackProfileId;
        Agent.Position = Source.Position;
        Agent.Velocity = Source.Velocity
          * Input.FixedStepSeconds;
        Agent.Facing = Source.Facing;
        Agent.Health = Source.Health;
        Agent.bAlive = Source.Health > 0;
        Agent.State = Source.AttackState;
      }
      TArray<FCrowdDemoAttackIntent> Intents;
      FCrowdDemoAttackPlanSummary PlanSummary;
      if (!FCrowdDemoAttackPlanner::Advance(
          9, Input.FixedStepIndex, Input.Profiles,
          PlannerAgents, Intents, PlanSummary)
        || !PlanSummary.bValid)
        return false;
      int32 TargetSwitchCount = 0;
      TMap<FCrowdStableEntityRef,
        FCrowdDemoWorkerMixedCombatAgent*> AgentByRef;
      for (auto& Agent : MixedAgents)
        AgentByRef.Add(Agent.EntityRef, &Agent);
      for (const FCrowdDemoAttackAgent& Planner : PlannerAgents)
      {
        auto* const* Stored = AgentByRef.Find(Planner.EntityRef);
        if (!Stored) return false;
        if ((*Stored)->AttackState.TargetRef.IsValid()
          && (*Stored)->AttackState.TargetRef
            != Planner.State.TargetRef)
          ++TargetSwitchCount;
        (*Stored)->AttackState = Planner.State;
      }

      TArray<FCrowdDemoAttackTargetSnapshot> AttackTargets;
      TArray<FCrowdProjectileTargetSnapshot> ProjectileTargets;
      CurrentMixedHealthStates.Reset();
      for (const auto& Agent : MixedAgents)
      {
        const FVector PreviousPosition =
          Agent.Position - Agent.Velocity * Input.FixedStepSeconds;
        if (Agent.Health > 0)
        {
          auto& Target = AttackTargets.AddDefaulted_GetRef();
          Target.Body.EntityRef = Agent.EntityRef;
          Target.Body.StartPosition = PreviousPosition;
          Target.Body.EndPosition = Agent.Position;
          Target.Body.RadiusCm = 42.0f;
          Target.Body.NavLayer = Agent.NavLayer;
          Target.Body.RecalculateStableHash();
          Target.FactionId = Agent.FactionId;
        }
        auto& ProjectileTarget =
          ProjectileTargets.AddDefaulted_GetRef();
        ProjectileTarget.EntityRef = Agent.EntityRef;
        ProjectileTarget.FactionId = Agent.FactionId;
        ProjectileTarget.NavLayer = Agent.NavLayer;
        ProjectileTarget.PreviousPosition = PreviousPosition;
        ProjectileTarget.Position = Agent.Position;
        ProjectileTarget.RadiusCm = 42.0f;
        ProjectileTarget.bAlive = Agent.Health > 0;
        ProjectileTarget.RecalculateStableHash();
        CurrentMixedHealthStates.Add({
          Agent.EntityRef, Agent.FactionId,
          Agent.Health, Agent.Health > 0});
      }
      FCrowdDemoPreparedAttackBoundary Attack;
      const TArray<FCrowdSpatialEnvironmentBody> Environment;
      if (!FCrowdDemoAttackHostAdapter::Prepare(
          Input.FixedStepIndex, Intents, AttackTargets,
          Environment, Attack))
        return false;
      OutImmediateImpacts = Attack.ImmediateImpacts;
      InOutProjectileInput.SpawnRequests =
        Attack.ProjectileRequests;
      InOutProjectileInput.Targets =
        MoveTemp(ProjectileTargets);
      CurrentMixedInput = MoveTemp(Input);
      CurrentMixedPlanSummary = PlanSummary;
      CurrentMixedAttack = MoveTemp(Attack);
      CurrentMixedTargetSwitchCount = TargetSwitchCount;
      CurrentKind = EHostKind::Mixed;
      return true;
    }

    bool FinishMixedStep(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdHitFact> Hits,
      TArray<FCrowdWorkerCombatExtensionPatch>& OutPatches,
      FCrowdWorkerPayload& OutHostResult)
    {
      if (Generation != Context.Generation
        || CurrentMixedInput.FixedStepIndex < 0)
        return false;
      FCrowdDemoPreparedAttackHealthPatch HealthPatch;
      if (!FCrowdDemoAttackHostAdapter::PrepareHealthPatch(
          CurrentMixedInput.FixedStepIndex, Hits,
          CurrentMixedHealthStates, HealthPatch))
        return false;
      TMap<FCrowdStableEntityRef,
        const FCrowdDemoAttackHealthState*> HealthByRef;
      for (const auto& Health : HealthPatch.States)
        HealthByRef.Add(Health.EntityRef, &Health);
      MixedAgents.Sort([](const auto& A, const auto& B)
      {
        return A.EntityRef < B.EntityRef;
      });
      OutPatches.Reset();
      OutPatches.Reserve(MixedAgents.Num());
      for (auto& Agent : MixedAgents)
      {
        const auto* const* Health = HealthByRef.Find(Agent.EntityRef);
        if (!Health) return false;
        Agent.Health = (*Health)->Health;
        auto& Patch = OutPatches.AddDefaulted_GetRef();
        Patch.EntityRef = Agent.EntityRef;
        Patch.State.SourceFixedStep =
          CurrentMixedInput.FixedStepIndex;
        Patch.State.bAlive = (*Health)->bAlive;
        Patch.State.bMovementLocked =
          (*Health)->bAlive
          && Agent.AttackState.Phase
            == ECrowdDemoAttackPlannerPhase::Commit;
        FCrowdDemoWorkerMixedCombatState HostState;
        HostState.Health = Agent.Health;
        HostState.bAlive = (*Health)->bAlive;
        HostState.AttackState = Agent.AttackState;
        if (!FCrowdDemoWorkerMixedCombatStateCodec::Encode(
          HostState, Patch.State.HostState))
          return false;
      }
      FCrowdDemoWorkerMixedCombatHostResult Result;
      Result.FixedStepIndex =
        CurrentMixedInput.FixedStepIndex;
      Result.AttackPlanSummary = CurrentMixedPlanSummary;
      Result.MeleeIntentCount =
        CurrentMixedAttack.MeleeIntentCount;
      Result.MidRangeIntentCount =
        CurrentMixedAttack.MidRangeIntentCount;
      Result.RangedIntentCount =
        CurrentMixedAttack.RangedIntentCount;
      Result.MissCount = CurrentMixedAttack.MissCount;
      Result.EnvironmentImpactCount =
        CurrentMixedAttack.EnvironmentImpactCount;
      Result.AppliedDamageCount =
        HealthPatch.AppliedDamageCount;
      Result.DuplicateHitCount =
        HealthPatch.DuplicateHitCount;
      Result.FriendlyFireCount =
        HealthPatch.FriendlyFireCount;
      Result.DeathCount = HealthPatch.DeathCount;
      Result.TargetSwitchCount =
        CurrentMixedTargetSwitchCount;
      return FCrowdDemoWorkerMixedCombatHostResultCodec::Encode(
        Result, OutHostResult);
    }

    EHostKind CurrentKind = EHostKind::None;
    uint64 Generation = 0;
    TArray<FCrowdDemoRangedCombatAgent> Agents;
    FCrowdDemoWorkerCombatHostInput CurrentInput;
    FCrowdDemoProjectileStepSummary CurrentAttackSummary;
    TArray<FCrowdDemoWorkerMixedCombatAgent> MixedAgents;
    TArray<FCrowdDemoAttackHealthState> CurrentMixedHealthStates;
    FCrowdDemoWorkerMixedCombatHostInput CurrentMixedInput;
    FCrowdDemoAttackPlanSummary CurrentMixedPlanSummary;
    FCrowdDemoPreparedAttackBoundary CurrentMixedAttack;
    int32 CurrentMixedTargetSwitchCount = 0;
  };
}

bool FCrowdDemoWorkerCombatHostInputCodec::Encode(
  const FCrowdDemoWorkerCombatHostInput& Input,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (Input.RoundId < 0 || Input.FixedStepIndex < 0
    || Input.PlanRevision < 0
    || !FMath::IsFinite(Input.ServerTimeSeconds)
    || !FMath::IsFinite(Input.FixedStepSeconds)
    || Input.FixedStepSeconds <= 0.0f
    || Input.Agents.IsEmpty()
    || Input.Agents.Num() > MaxAgents
    || (!Input.bVatShowcase
      && !Input.InjectedHitCommands.IsEmpty()))
    return false;
  TSet<FCrowdStableEntityRef> AgentRefs;
  for (const FCrowdDemoRangedCombatAgent& Agent : Input.Agents)
    AgentRefs.Add(Agent.EntityRef);
  for (const FCrowdDemoWorkerInjectedHitCommand& Command :
    Input.InjectedHitCommands)
    if (!AgentRefs.Contains(Command.TargetEntity))
      return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  int32 RoundId = Input.RoundId;
  int32 FixedStepIndex = Input.FixedStepIndex;
  int32 PlanRevision = Input.PlanRevision;
  float ServerTime = Input.ServerTimeSeconds;
  float FixedStep = Input.FixedStepSeconds;
  auto Attack = Input.AttackSettings;
  auto Hit = Input.HitSettings;
  uint8 VatShowcase = Input.bVatShowcase ? 1 : 0;
  TArray<FCrowdDemoWorkerInjectedHitCommand> Commands =
    Input.InjectedHitCommands;
  TArray<FCrowdDemoRangedCombatAgent> Agents = Input.Agents;
  Writer << RoundId << FixedStepIndex << PlanRevision;
  Writer << ServerTime << FixedStep;
  SerializeAttackSettings(Writer, Attack);
  SerializeHitSettings(Writer, Hit);
  Writer << VatShowcase;
  Commands.Sort([](
    const FCrowdDemoWorkerInjectedHitCommand& A,
    const FCrowdDemoWorkerInjectedHitCommand& B)
  {
    if (A.ApplyFixedStep != B.ApplyFixedStep)
      return A.ApplyFixedStep < B.ApplyFixedStep;
    if (A.TargetEntity != B.TargetEntity)
      return A.TargetEntity < B.TargetEntity;
    return A.HitEventId < B.HitEventId;
  });
  int32 CommandCount = Commands.Num();
  Writer << CommandCount;
  TSet<uint64> HitEventIds;
  for (FCrowdDemoWorkerInjectedHitCommand& Command : Commands)
  {
    if (!IsFiniteInjectedHitCommand(Command)
      || HitEventIds.Contains(Command.HitEventId))
      return false;
    HitEventIds.Add(Command.HitEventId);
    SerializeInjectedHitCommand(Writer, Command);
  }
  int32 Count = Agents.Num();
  Writer << Count;
  for (FCrowdDemoRangedCombatAgent& Agent : Agents)
  {
    if (!IsFiniteAgent(Agent)) return false;
    SerializeAgent(Writer, Agent);
  }
  if (Writer.IsError() || Bytes.IsEmpty()
    || Bytes.Num() > MaxEncodedBytes)
    return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes = MoveTemp(Bytes);
  OutPayload.RecalculateStableHash();
  return OutPayload.StableHash != 0;
}

bool FCrowdDemoWorkerCombatHostInputCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdDemoWorkerCombatHostInput& OutInput)
{
  OutInput = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  Reader << OutInput.RoundId
    << OutInput.FixedStepIndex
    << OutInput.PlanRevision;
  Reader << OutInput.ServerTimeSeconds
    << OutInput.FixedStepSeconds;
  SerializeAttackSettings(Reader, OutInput.AttackSettings);
  SerializeHitSettings(Reader, OutInput.HitSettings);
  uint8 VatShowcase = 0;
  Reader << VatShowcase;
  if (VatShowcase > 1) return false;
  OutInput.bVatShowcase = VatShowcase != 0;
  int32 CommandCount = 0;
  Reader << CommandCount;
  if (CommandCount < 0 || CommandCount > MaxAgents)
    return false;
  OutInput.InjectedHitCommands.SetNum(CommandCount);
  TSet<uint64> HitEventIds;
  for (FCrowdDemoWorkerInjectedHitCommand& Command :
    OutInput.InjectedHitCommands)
  {
    SerializeInjectedHitCommand(Reader, Command);
    if (!IsFiniteInjectedHitCommand(Command)
      || HitEventIds.Contains(Command.HitEventId))
      return false;
    HitEventIds.Add(Command.HitEventId);
  }
  int32 Count = 0;
  Reader << Count;
  if (Count <= 0 || Count > MaxAgents) return false;
  OutInput.Agents.SetNum(Count);
  for (FCrowdDemoRangedCombatAgent& Agent : OutInput.Agents)
  {
    SerializeAgent(Reader, Agent);
    if (!IsFiniteAgent(Agent)) return false;
  }
  if (!OutInput.bVatShowcase
    && !OutInput.InjectedHitCommands.IsEmpty())
    return false;
  TSet<FCrowdStableEntityRef> AgentRefs;
  for (const FCrowdDemoRangedCombatAgent& Agent : OutInput.Agents)
    AgentRefs.Add(Agent.EntityRef);
  for (const FCrowdDemoWorkerInjectedHitCommand& Command :
    OutInput.InjectedHitCommands)
    if (!AgentRefs.Contains(Command.TargetEntity))
      return false;
  return !Reader.IsError() && Reader.AtEnd()
    && OutInput.FixedStepIndex >= 0
    && OutInput.FixedStepSeconds > 0.0f;
}

bool CalculateCrowdDemoWorkerProjectileControlSemanticHash(
  const FCrowdWorkerProjectileControlResource& Control,
  uint64& OutSemanticHash)
{
  OutSemanticHash = 0;
  if (!Control.IsValid()) return false;

  FCrowdWorkerProjectileControlResource Semantic = Control;
  Semantic.Revision = 1;
  Semantic.bReplaceState = false;
  Semantic.Input.FixedStepIndex = 0;
  Semantic.Input.ServerTimeSeconds = 0.0f;
  Semantic.Input.SpawnRequests.Reset();
  Semantic.Input.Targets.Reset();
  Semantic.Input.CurrentStates.Reset();

  if (Semantic.HostCombatInput.SchemaId
      == FCrowdDemoWorkerCombatHostInputCodec::SchemaId)
  {
    FCrowdDemoWorkerCombatHostInput Host;
    if (!FCrowdDemoWorkerCombatHostInputCodec::Decode(
        Semantic.HostCombatInput, Host))
      return false;
    Host.FixedStepIndex = 0;
    Host.ServerTimeSeconds = 0.0f;
    for (FCrowdDemoRangedCombatAgent& Agent : Host.Agents)
    {
      Agent.Position = FVector::ZeroVector;
      Agent.Velocity = FVector::ZeroVector;
      Agent.bAlive = true;
      const int32 MaximumHealth = Agent.Combat.MaxHealth;
      Agent.Combat = {};
      Agent.Combat.AgentId = Agent.AgentId;
      Agent.Combat.LifecycleSerial = Agent.LifecycleSerial;
      Agent.Combat.Health = MaximumHealth;
      Agent.Combat.MaxHealth = MaximumHealth;
      Agent.Combat.bAlive = true;
    }
    if (!FCrowdDemoWorkerCombatHostInputCodec::Encode(
        Host, Semantic.HostCombatInput))
      return false;
  }
  else if (Semantic.HostCombatInput.SchemaId
      == FCrowdDemoWorkerMixedCombatHostInputCodec::SchemaId)
  {
    FCrowdDemoWorkerMixedCombatHostInput Host;
    if (!FCrowdDemoWorkerMixedCombatHostInputCodec::Decode(
        Semantic.HostCombatInput, Host))
      return false;
    Host.FixedStepIndex = 0;
    for (FCrowdDemoWorkerMixedCombatAgent& Agent : Host.Agents)
    {
      Agent.Position = FVector::ZeroVector;
      Agent.Velocity = FVector::ZeroVector;
      Agent.Facing = FVector::ForwardVector;
      Agent.Health = 100;
      Agent.AttackState = {};
    }
    if (!FCrowdDemoWorkerMixedCombatHostInputCodec::Encode(
        Host, Semantic.HostCombatInput))
      return false;
  }

  FCrowdWorkerPayload Payload;
  if (!FCrowdWorkerProjectileControlResourceCodec::Encode(
      Semantic, Payload))
    return false;
  OutSemanticHash = Payload.StableHash;
  return OutSemanticHash != 0;
}

bool FCrowdDemoWorkerCombatStatePayloadCodec::Encode(
  const FCrowdDemoCombatAgentState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  FCrowdDemoCombatAgentState Copy = State;
  SerializeCombatState(Writer, Copy);
  if (Writer.IsError() || Bytes.IsEmpty()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes = MoveTemp(Bytes);
  OutPayload.RecalculateStableHash();
  return OutPayload.StableHash != 0;
}

bool FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdDemoCombatAgentState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  SerializeCombatState(Reader, OutState);
  return !Reader.IsError() && Reader.AtEnd()
    && OutState.AgentId != INDEX_NONE
    && OutState.LifecycleSerial > 0;
}

bool FCrowdDemoWorkerCombatHostResultCodec::Encode(
  const FCrowdDemoWorkerCombatHostResult& Result,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (Result.FixedStepIndex < 0
    || !Result.AttackSummary.bValid
    || !Result.HitSummary.bValid)
    return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  int32 FixedStepIndex = Result.FixedStepIndex;
  auto AttackSummary = Result.AttackSummary;
  auto HitSummary = Result.HitSummary;
  Writer << FixedStepIndex;
  SerializeProjectileSummary(Writer, AttackSummary);
  SerializeHitSummary(Writer, HitSummary);
  if (Writer.IsError() || Bytes.IsEmpty()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes = MoveTemp(Bytes);
  OutPayload.RecalculateStableHash();
  return OutPayload.StableHash != 0;
}

bool FCrowdDemoWorkerCombatHostResultCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdDemoWorkerCombatHostResult& OutResult)
{
  OutResult = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  Reader << OutResult.FixedStepIndex;
  SerializeProjectileSummary(Reader, OutResult.AttackSummary);
  SerializeHitSummary(Reader, OutResult.HitSummary);
  return !Reader.IsError() && Reader.AtEnd()
    && OutResult.FixedStepIndex >= 0
    && OutResult.AttackSummary.bValid
    && OutResult.HitSummary.bValid;
}

bool FCrowdDemoWorkerMixedCombatHostInputCodec::Encode(
  const FCrowdDemoWorkerMixedCombatHostInput& Input,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (Input.FixedStepIndex < 0
    || !FMath::IsFinite(Input.FixedStepSeconds)
    || Input.FixedStepSeconds <= 0.0f
    || Input.Profiles.IsEmpty()
    || Input.Agents.IsEmpty()
    || Input.Agents.Num() > MaxAgents)
    return false;
  for (const auto& Profile : Input.Profiles)
    if (!Profile.IsValid()) return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  int32 FixedStepIndex = Input.FixedStepIndex;
  float FixedStepSeconds = Input.FixedStepSeconds;
  TArray<FCrowdDemoAttackProfileV1> Profiles = Input.Profiles;
  TArray<FCrowdDemoWorkerMixedCombatAgent> Agents = Input.Agents;
  Writer << FixedStepIndex << FixedStepSeconds;
  int32 ProfileCount = Profiles.Num();
  Writer << ProfileCount;
  for (auto& Profile : Profiles)
    SerializeAttackProfile(Writer, Profile);
  int32 AgentCount = Agents.Num();
  Writer << AgentCount;
  for (auto& Agent : Agents)
  {
    if (!IsFiniteMixedAgent(Agent)) return false;
    SerializeMixedAgent(Writer, Agent);
  }
  if (Writer.IsError() || Bytes.IsEmpty()
    || Bytes.Num() > MaxEncodedBytes)
    return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes = MoveTemp(Bytes);
  OutPayload.RecalculateStableHash();
  return OutPayload.StableHash != 0;
}

bool FCrowdDemoWorkerMixedCombatHostInputCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdDemoWorkerMixedCombatHostInput& OutInput)
{
  OutInput = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  Reader << OutInput.FixedStepIndex
    << OutInput.FixedStepSeconds;
  int32 ProfileCount = 0;
  Reader << ProfileCount;
  if (ProfileCount <= 0 || ProfileCount > 64) return false;
  OutInput.Profiles.SetNum(ProfileCount);
  for (auto& Profile : OutInput.Profiles)
  {
    SerializeAttackProfile(Reader, Profile);
    if (!Profile.IsValid()) return false;
  }
  int32 AgentCount = 0;
  Reader << AgentCount;
  if (AgentCount <= 0 || AgentCount > MaxAgents) return false;
  OutInput.Agents.SetNum(AgentCount);
  for (auto& Agent : OutInput.Agents)
  {
    SerializeMixedAgent(Reader, Agent);
    if (!IsFiniteMixedAgent(Agent)) return false;
  }
  return !Reader.IsError() && Reader.AtEnd()
    && OutInput.FixedStepIndex >= 0
    && OutInput.FixedStepSeconds > 0.0f;
}

bool FCrowdDemoWorkerMixedCombatStateCodec::Encode(
  const FCrowdDemoWorkerMixedCombatState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (State.Health < 0
    || State.bAlive != (State.Health > 0)
    || !State.AttackState.IsValid())
    return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  int32 Health = State.Health;
  uint8 Alive = State.bAlive ? 1 : 0;
  FCrowdDemoAttackState AttackState = State.AttackState;
  Writer << Health << Alive;
  SerializeAttackState(Writer, AttackState);
  if (Writer.IsError() || Bytes.IsEmpty()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes = MoveTemp(Bytes);
  OutPayload.RecalculateStableHash();
  return OutPayload.StableHash != 0;
}

bool FCrowdDemoWorkerMixedCombatStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdDemoWorkerMixedCombatState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  uint8 Alive = 0;
  Reader << OutState.Health << Alive;
  OutState.bAlive = Alive != 0;
  SerializeAttackState(Reader, OutState.AttackState);
  return !Reader.IsError() && Reader.AtEnd()
    && Alive <= 1
    && OutState.Health >= 0
    && OutState.bAlive == (OutState.Health > 0)
    && OutState.AttackState.IsValid();
}

bool FCrowdDemoWorkerMixedCombatHostResultCodec::Encode(
  const FCrowdDemoWorkerMixedCombatHostResult& Result,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (Result.FixedStepIndex < 0
    || !Result.AttackPlanSummary.bValid
    || Result.MeleeIntentCount < 0
    || Result.MidRangeIntentCount < 0
    || Result.RangedIntentCount < 0
    || Result.MissCount < 0
    || Result.EnvironmentImpactCount < 0
    || Result.AppliedDamageCount < 0
    || Result.DuplicateHitCount < 0
    || Result.FriendlyFireCount < 0
    || Result.DeathCount < 0
    || Result.TargetSwitchCount < 0)
    return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  int32 FixedStepIndex = Result.FixedStepIndex;
  auto PlanSummary = Result.AttackPlanSummary;
  Writer << FixedStepIndex;
  SerializeMixedPlanSummary(Writer, PlanSummary);
  int32 Values[] = {
    Result.MeleeIntentCount,
    Result.MidRangeIntentCount,
    Result.RangedIntentCount,
    Result.MissCount,
    Result.EnvironmentImpactCount,
    Result.AppliedDamageCount,
    Result.DuplicateHitCount,
    Result.FriendlyFireCount,
    Result.DeathCount,
    Result.TargetSwitchCount};
  for (int32& Value : Values) Writer << Value;
  if (Writer.IsError() || Bytes.IsEmpty()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes = MoveTemp(Bytes);
  OutPayload.RecalculateStableHash();
  return OutPayload.StableHash != 0;
}

bool FCrowdDemoWorkerMixedCombatHostResultCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdDemoWorkerMixedCombatHostResult& OutResult)
{
  OutResult = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  Reader << OutResult.FixedStepIndex;
  SerializeMixedPlanSummary(
    Reader, OutResult.AttackPlanSummary);
  int32* Values[] = {
    &OutResult.MeleeIntentCount,
    &OutResult.MidRangeIntentCount,
    &OutResult.RangedIntentCount,
    &OutResult.MissCount,
    &OutResult.EnvironmentImpactCount,
    &OutResult.AppliedDamageCount,
    &OutResult.DuplicateHitCount,
    &OutResult.FriendlyFireCount,
    &OutResult.DeathCount,
    &OutResult.TargetSwitchCount};
  for (int32* Value : Values) Reader << *Value;
  return !Reader.IsError() && Reader.AtEnd()
    && OutResult.FixedStepIndex >= 0
    && OutResult.AttackPlanSummary.bValid
    && OutResult.MeleeIntentCount >= 0
    && OutResult.MidRangeIntentCount >= 0
    && OutResult.RangedIntentCount >= 0
    && OutResult.MissCount >= 0
    && OutResult.EnvironmentImpactCount >= 0
    && OutResult.AppliedDamageCount >= 0
    && OutResult.DuplicateHitCount >= 0
    && OutResult.FriendlyFireCount >= 0
    && OutResult.DeathCount >= 0
    && OutResult.TargetSwitchCount >= 0;
}

TUniquePtr<ICrowdWorkerCombatExtension>
  MakeCrowdDemoWorkerCombatExtension()
{
  return MakeUnique<FCrowdDemoWorkerCombatExtension>();
}
