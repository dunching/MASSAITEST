#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"

#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"

namespace
{
  class FCrowdDemoBoundaryHashPayload final
    : public ICrowdBoundaryPreparedPatchPayload
  {
  };

  uint64 FoldBoundaryHash(uint64 Hash, const uint64 Value)
  {
    for (int32 Shift = 0; Shift < 64; Shift += 8)
    {
      Hash ^= static_cast<uint8>(Value >> Shift);
      Hash *= 1099511628211ull;
    }
    return Hash;
  }

  FCrowdDemoCombatAgentState BuildBoundaryCombatState(
    const FCrowdDemoRoundBoundaryBusinessFact& Fact)
  {
    FCrowdDemoCombatAgentState Result;
    Result.AgentId = Fact.AgentId;
    Result.LifecycleSerial =
      static_cast<int32>(Fact.EntityRef.LifecycleSerial);
    Result.Health = Fact.Stats.Health;
    Result.MaxHealth = Fact.Stats.MaxHealth;
    Result.LifecycleState = Fact.Stats.LifecycleState;
    Result.bAlive = Fact.Stats.bAlive;
    Result.BusinessState = Fact.Business.State;
    Result.BusinessStateRevision = Fact.Business.StateRevision;
    Result.BusinessStateEnterFixedStep =
      Fact.Business.StateEnterFixedStep;
    Result.TargetAgentId = Fact.Business.TargetAgentId;
    Result.TargetLifecycleSerial = Fact.Business.TargetLifecycleSerial;
    Result.LastConsumedHitEventId =
      Fact.Business.LastConsumedHitEventId;
    Result.AttackPhase = Fact.Attack.Phase;
    Result.AttackPhaseEnterFixedStep =
      Fact.Attack.PhaseEnterFixedStep;
    Result.CooldownEndFixedStep = Fact.Attack.CooldownEndFixedStep;
    Result.LockedTargetAgentId = Fact.Attack.LockedTargetAgentId;
    Result.LockedTargetLifecycleSerial =
      Fact.Attack.LockedTargetLifecycleSerial;
    Result.LockedTargetLocation = Fact.Attack.LockedTargetLocation;
    Result.FireSequence = Fact.Attack.FireSequence;
    Result.bFireRequestIssued = Fact.Attack.bFireRequestIssued;
    Result.ReactiveMode = Fact.ReactiveMotion.Mode;
    Result.HorizontalReactiveVelocity =
      Fact.ReactiveMotion.HorizontalVelocity;
    Result.VerticalReactiveVelocityCmps =
      Fact.ReactiveMotion.VerticalVelocityCmps;
    Result.ReactiveStartFixedStep =
      Fact.ReactiveMotion.StartFixedStep;
    Result.ReactiveEndFixedStep = Fact.ReactiveMotion.EndFixedStep;
    Result.ReactiveRevision = Fact.ReactiveMotion.ReactiveRevision;
    Result.RestoreBusinessState =
      Fact.ReactiveMotion.RestoreBusinessState;
    Result.ApexCount = Fact.ReactiveMotion.ApexCount;
    Result.LandingCount = Fact.ReactiveMotion.LandingCount;
    Result.HitFlashRevision = Fact.HitFlash.FlashRevision;
    Result.HitFlashStartServerTimeSeconds =
      Fact.HitFlash.StartServerTimeSeconds;
    Result.HitFlashDurationSeconds = Fact.HitFlash.DurationSeconds;
    Result.HitFlashProfileKey = Fact.HitFlash.ProfileKey;
    Result.HitFlashPeakIntensity = Fact.HitFlash.PeakIntensity;
    Result.VisualState = Fact.Visual.VisualState;
    Result.VisualRevision = Fact.Visual.VisualRevision;
    Result.VisualStateStartServerTimeSeconds =
      Fact.Visual.StateStartServerTimeSeconds;
    Result.VisualPhaseSeed = Fact.Visual.PhaseSeed;
    return Result;
  }

  FCrowdDemoBoundaryBusinessWorkOutput RunBoundaryBusinessWork(
    const FCrowdDemoBoundaryBusinessWorkInput& Input)
  {
    FCrowdDemoBoundaryBusinessWorkOutput Output;
    const bool bProjectileCombat =
      Input.Rules.RangedCombatSettings.bEnabled != 0;
    const bool bShowcase =
      Input.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Input.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
    Output.bRequiresCommit = bProjectileCombat || bShowcase;
    if (!Input.Snapshot.bValid
      || Input.FixedStepIndex != Input.Snapshot.FixedStepIndex
      || Input.PlanRevision != Input.Snapshot.PlanRevision
      || Input.Facts.Num() != Input.Snapshot.Agents.Num()
      || Input.FixedStepSeconds <= 0.0f)
      return Output;

    uint64 StableHash = FoldBoundaryHash(
      Input.Snapshot.StableHash,
      static_cast<uint32>(Input.FixedStepIndex));
    if (!Output.bRequiresCommit)
    {
      Output.StableHash = FoldBoundaryHash(StableHash, 0);
      Output.bCompleted = Output.StableHash != 0;
      return Output;
    }

    TArray<FCrowdDemoRangedCombatAgent> Agents;
    TMap<int32, FVector> LocalOffsetByAgentId;
    TMap<int32, float> YawByAgentId;
    Agents.Reserve(Input.Facts.Num());
    for (int32 Index = 0; Index < Input.Facts.Num(); ++Index)
    {
      const FCrowdDemoRoundBoundaryBusinessFact& Fact =
        Input.Facts[Index];
      const FCrowdMassBoundaryAgentRecord& Base =
        Input.Snapshot.Agents[Index];
      if (Fact.EntityRef != Base.AgentFacts.StableEntityRef
        || Fact.AgentId != Base.Identity.AgentId)
        return Output;
      FCrowdDemoRangedCombatAgent& Agent =
        Agents.AddDefaulted_GetRef();
      Agent.AgentId = Fact.AgentId;
      Agent.LifecycleSerial =
        static_cast<int32>(Fact.EntityRef.LifecycleSerial);
      Agent.FormationIndex = Fact.FormationIndex;
      Agent.Position = Base.State.Position;
      Agent.RadiusCm = Fact.RadiusCm;
      Agent.bAlive = Fact.Stats.bAlive;
      Agent.Combat = BuildBoundaryCombatState(Fact);
      LocalOffsetByAgentId.Add(Agent.AgentId, Fact.LocalOffset);
      YawByAgentId.Add(Agent.AgentId, Fact.YawDegrees);
    }
    Agents.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    if (Agents.Num() != Input.Snapshot.Agents.Num())
      return Output;
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
      if (Agents[Index].AgentId
          != Input.Snapshot.Agents[Index].Identity.AgentId
        || (Index > 0
          && Agents[Index - 1].AgentId >= Agents[Index].AgentId))
        return Output;
    }

    if (bShowcase && Input.FixedStepIndex == 0)
    {
      for (FCrowdDemoRangedCombatAgent& Agent : Agents)
      {
        if (Agent.Combat.BusinessStateRevision != 0) continue;
        Agent.Combat.BusinessState = Agent.FormationIndex < 4
          ? ECrowdDemoBusinessState::Idle
          : Agent.FormationIndex < 8
            ? ECrowdDemoBusinessState::Moving
            : Agent.FormationIndex < 12
              ? ECrowdDemoBusinessState::Attacking
              : ECrowdDemoBusinessState::Idle;
        Agent.Combat.AttackPhase =
          Agent.Combat.BusinessState
            == ECrowdDemoBusinessState::Attacking
          ? ECrowdDemoAttackPhase::Windup
          : ECrowdDemoAttackPhase::None;
        Agent.Combat.BusinessStateRevision = 1;
        Agent.Combat.BusinessStateEnterFixedStep = 0;
      }
    }

    TArray<FCrowdDemoHitFact> HitFacts;
    FCrowdDemoProjectileStepSummary ProjectileSummary;
    TArray<FCrowdDemoProjectileVisualEvent> ProjectileEvents;
    TArray<FCrowdDemoProjectileState> NextProjectiles =
      Input.Projectiles;
    if (bProjectileCombat)
    {
      TArray<FCrowdDemoProjectileSpawnRequest> Requests;
      FCrowdDemoProjectileKernel::AdvanceAttackPhases(
        Input.RoundId, Input.FixedStepIndex,
        Input.Rules.RangedCombatSettings, Agents, Requests,
        ProjectileSummary);
      FCrowdDemoProjectileKernel::SpawnProjectiles(
        Input.FixedStepIndex, Input.StepEndServerTimeSeconds,
        Input.Rules.RangedCombatSettings, Requests, NextProjectiles,
        ProjectileEvents, ProjectileSummary);
      FCrowdDemoProjectileKernel::AdvanceProjectiles(
        Input.FixedStepIndex, Input.StepEndServerTimeSeconds,
        Input.FixedStepSeconds, Input.Rules.RangedCombatSettings,
        Agents, NextProjectiles, HitFacts, ProjectileEvents,
        ProjectileSummary);
    }
    if (bShowcase)
    {
      for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
      {
        const bool bKnockback = Input.FixedStepIndex == 30
          && Agent.FormationIndex >= 12 && Agent.FormationIndex < 14;
        const bool bKnockUp = Input.FixedStepIndex == 60
          && Agent.FormationIndex >= 14 && Agent.FormationIndex < 16;
        const bool bDeath = Input.FixedStepIndex == 90
          && Agent.FormationIndex >= 16;
        if (!bKnockback && !bKnockUp && !bDeath) continue;
        FCrowdDemoHitFact& Fact = HitFacts.AddDefaulted_GetRef();
        Fact.HitEventId =
          (static_cast<uint64>(Input.RoundId) << 32)
          | (static_cast<uint64>(Input.FixedStepIndex) << 16)
          | static_cast<uint32>(Agent.AgentId);
        Fact.ApplyFixedStep = Input.FixedStepIndex;
        Fact.TargetAgentId = Agent.AgentId;
        Fact.TargetLifecycleSerial = Agent.Combat.LifecycleSerial;
        Fact.HitPosition = Agent.Position;
        Fact.HitDirection = FVector::ForwardVector;
        Fact.Damage = bDeath ? 1000.0f : 10.0f;
        Fact.HorizontalImpulseCmps = bKnockback ? 500.0f : 0.0f;
        Fact.VerticalImpulseCmps = bKnockUp ? 650.0f : 0.0f;
        Fact.HitFlashProfileKey = 1;
      }
    }

    TArray<FCrowdDemoCombatAgentState> CombatStates;
    CombatStates.Reserve(Agents.Num());
    for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
      CombatStates.Add(Agent.Combat);
    FCrowdDemoHitResponseSettings HitSettings;
    HitSettings.FixedStepSeconds = Input.FixedStepSeconds;
    FCrowdDemoHitResponseSummary HitSummary;
    FCrowdDemoCombatStateKernel::ResolveHitFacts(
      Input.FixedStepIndex, Input.StepEndServerTimeSeconds, HitFacts,
      HitSettings, CombatStates, HitSummary);
    TMap<int32, const FCrowdDemoCombatAgentState*> CombatByAgentId;
    for (const FCrowdDemoCombatAgentState& Combat : CombatStates)
      CombatByAgentId.Add(Combat.AgentId, &Combat);
    for (FCrowdDemoRangedCombatAgent& Agent : Agents)
    {
      const FCrowdDemoCombatAgentState* const* Combat =
        CombatByAgentId.Find(Agent.AgentId);
      if (!Combat) return Output;
      Agent.Combat = **Combat;

      const float YawDegrees =
        YawByAgentId.FindRef(Agent.AgentId);
      FCrowdDemoGuidanceCandidate BusinessCandidate;
      if (bProjectileCombat)
      {
        BusinessCandidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Agent.AgentId,
            ECrowdDemoGuidanceProvider::BusinessOverride,
            Input.PlanRevision, FVector::ZeroVector,
            Agent.Position, YawDegrees, true);
      }
      else
      {
        const FVector Anchor = FVector(Input.Rules.SpawnOrigin)
          + LocalOffsetByAgentId.FindRef(Agent.AgentId);
        const FCrowdDemoVatShowcaseMotionResult Showcase =
          FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
            Agent.FormationIndex, Input.FixedStepIndex,
            Agent.Position, Anchor);
        if (Showcase.bValid)
        {
          BusinessCandidate =
            FCrowdDemoGuidanceComposeKernel::BuildCandidate(
              Agent.AgentId,
              ECrowdDemoGuidanceProvider::BusinessOverride,
              Input.PlanRevision, Showcase.DesiredVelocity,
              Showcase.DesiredLocation,
              Showcase.DesiredVelocity.IsNearlyZero()
                ? YawDegrees
                : Showcase.DesiredVelocity.Rotation().Yaw,
              true);
        }
      }
      const FCrowdDemoReactiveMotionStepResult StepResult =
        FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(
          Input.FixedStepIndex, Agent.Position.Z, HitSettings,
          Agent.Combat);
      FCrowdDemoPreparedReactiveMotionStep& ReactiveStep =
        Output.ReactiveSteps.AddDefaulted_GetRef();
      ReactiveStep.AgentId = Agent.AgentId;
      ReactiveStep.LifecycleSerial = Agent.LifecycleSerial;
      if (!Agent.Combat.bAlive)
      {
        BusinessCandidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Agent.AgentId,
            ECrowdDemoGuidanceProvider::BusinessOverride,
            Input.PlanRevision, FVector::ZeroVector,
            Agent.Position, YawDegrees, true);
      }
      else if (StepResult.bValid
        && Agent.Combat.ReactiveMode
          != ECrowdDemoReactiveMotionMode::None)
      {
        const FVector ReactiveVelocity(
          StepResult.HorizontalVelocity.X,
          StepResult.HorizontalVelocity.Y, 0.0f);
        const FVector DesiredLocation = BusinessCandidate.bValid
          ? BusinessCandidate.DesiredLocation : Agent.Position;
        BusinessCandidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Agent.AgentId,
            ECrowdDemoGuidanceProvider::BusinessOverride,
            Input.PlanRevision, ReactiveVelocity, DesiredLocation,
            ReactiveVelocity.IsNearlyZero()
              ? YawDegrees : ReactiveVelocity.Rotation().Yaw,
            true);
        ReactiveStep.bActive = true;
        ReactiveStep.ProposedZ = StepResult.NewZ;
        ReactiveStep.VerticalVelocityCmps =
          StepResult.NewVerticalVelocityCmps;
      }
      if (BusinessCandidate.bValid)
      {
        Output.GuidanceCandidates.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::
            BuildCoreGuidanceCandidate(BusinessCandidate));
      }
    }
    Output.GuidanceCandidates.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    Output.ReactiveSteps.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });

    StableHash = FoldBoundaryHash(StableHash, 1);
    for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
    {
      StableHash = FoldBoundaryHash(
        StableHash, static_cast<uint32>(Agent.AgentId));
      StableHash = FoldBoundaryHash(
        StableHash, static_cast<uint32>(Agent.LifecycleSerial));
      StableHash = FoldBoundaryHash(
        StableHash,
        static_cast<uint32>(Agent.Combat.BusinessStateRevision));
      StableHash = FoldBoundaryHash(
        StableHash,
        static_cast<uint32>(Agent.Combat.ReactiveRevision));
      StableHash = FoldBoundaryHash(
        StableHash, Agent.Combat.LastConsumedHitEventId);
    }
    StableHash = FoldBoundaryHash(
      StableHash, ProjectileSummary.AttackStateHash);
    StableHash = FoldBoundaryHash(
      StableHash, ProjectileSummary.ProjectileStateHash);
    StableHash = FoldBoundaryHash(
      StableHash, ProjectileSummary.EventHash);
    StableHash = FoldBoundaryHash(
      StableHash, static_cast<uint32>(HitSummary.AppliedHitCount));
    StableHash = FoldBoundaryHash(
      StableHash, static_cast<uint32>(HitSummary.DuplicateHitCount));

    Output.Commit.FixedStepIndex = Input.FixedStepIndex;
    Output.Commit.PlanRevision = Input.PlanRevision;
    Output.Commit.Agents = MoveTemp(Agents);
    Output.Commit.Projectiles = MoveTemp(NextProjectiles);
    Output.Commit.ProjectileEvents = MoveTemp(ProjectileEvents);
    Output.Commit.ProjectileSummary = ProjectileSummary;
    Output.Commit.HitSummary = HitSummary;
    Output.Commit.StableHash = StableHash;
    Output.Commit.bProjectileCombat = bProjectileCombat;
    Output.Commit.bValid = StableHash != 0;
    Output.StableHash = StableHash;
    Output.bCompleted = Output.Commit.bValid
      && Output.ReactiveSteps.Num() == Input.Snapshot.Agents.Num();
    return Output;
  }

  constexpr float CorrectionIntervalSeconds = 0.5f;
  constexpr float CorrectionMaxAgeMs = 1000.0f;
  constexpr float OverlapRadiusCm = 78.0f;
  constexpr float SevereOverlapRadiusCm = 42.0f;

  bool IsFlowScenario(const ECrowdDemoScenario Scenario)
  {
    return Scenario == ECrowdDemoScenario::SimRoundObstacle
      || Scenario == ECrowdDemoScenario::SimRoundSoftPressure;
  }

  uint32 FoldHash(uint32 Hash, const uint32 Value)
  {
    for (int32 Shift = 0; Shift < 32; Shift += 8)
    {
      Hash ^= (Value >> Shift) & 0xffu;
      Hash *= 16777619u;
    }
    return Hash;
  }

  uint64 MakeTargetPlanResourceKey(
    const uint32 CapabilityProfileKey,
    const FCrowdDemoTargetRegionFlowPlan& Plan)
  {
    uint32 Low = 2166136261u;
    Low = FoldHash(Low, CapabilityProfileKey);
    Low = FoldHash(Low, Plan.TransportHash);
    Low = FoldHash(Low, static_cast<uint32>(Plan.PlanEpoch));
    uint32 High = 2166136261u;
    High = FoldHash(High, static_cast<uint32>(Plan.BuildFixedStepIndex));
    High = FoldHash(High, static_cast<uint32>(Plan.TargetRevision));
    High = FoldHash(High, Plan.FeasibleGraphHash);
    return (static_cast<uint64>(High) << 32) | Low;
  }

  uint32 BuildAppliedStateDifferenceMask(
    const FCrowdDemoRoundAgentState& Local,
    const FCrowdDemoRoundAgentState& Server)
  {
    uint32 Mask = 0;
    const auto QuantizedEqual = [](const float A, const float B, const float Scale)
    {
      return FMath::RoundToInt(A * Scale) == FMath::RoundToInt(B * Scale);
    };
    if (Local.LifecycleSerial != Server.LifecycleSerial
      || !QuantizedEqual(Local.Location.X, Server.Location.X, 1000.0f)
      || !QuantizedEqual(Local.Location.Y, Server.Location.Y, 1000.0f)
      || !QuantizedEqual(Local.Location.Z, Server.Location.Z, 1000.0f)
      || !QuantizedEqual(Local.Velocity.X, Server.Velocity.X, 1000.0f)
      || !QuantizedEqual(Local.Velocity.Y, Server.Velocity.Y, 1000.0f)
      || !QuantizedEqual(Local.Velocity.Z, Server.Velocity.Z, 1000.0f)
      || !QuantizedEqual(Local.YawDegrees, Server.YawDegrees, 1000.0f)
      || !QuantizedEqual(Local.RadiusCm, Server.RadiusCm, 1000.0f))
      Mask |= 1u << 0;
    if (!QuantizedEqual(Local.Combat.Health, Server.Combat.Health, 100.0f)
      || !QuantizedEqual(Local.Combat.MaxHealth, Server.Combat.MaxHealth, 100.0f)
      || Local.Combat.LifecycleState != Server.Combat.LifecycleState
      || Local.Combat.bAlive != Server.Combat.bAlive)
      Mask |= 1u << 1;
    if (Local.Combat.BusinessState != Server.Combat.BusinessState
      || Local.Combat.BusinessStateRevision != Server.Combat.BusinessStateRevision
      || Local.Combat.BusinessStateEnterFixedStep != Server.Combat.BusinessStateEnterFixedStep
      || Local.Combat.TargetAgentId != Server.Combat.TargetAgentId
      || Local.Combat.TargetLifecycleSerial != Server.Combat.TargetLifecycleSerial)
      Mask |= 1u << 2;
    if (Local.Combat.AttackPhase != Server.Combat.AttackPhase
      || Local.Combat.AttackPhaseEnterFixedStep != Server.Combat.AttackPhaseEnterFixedStep
      || Local.Combat.CooldownEndFixedStep != Server.Combat.CooldownEndFixedStep
      || Local.Combat.LockedTargetAgentId != Server.Combat.LockedTargetAgentId
      || Local.Combat.LockedTargetLifecycleSerial != Server.Combat.LockedTargetLifecycleSerial
      || Local.Combat.FireSequence != Server.Combat.FireSequence
      || Local.Combat.bFireRequestIssued != Server.Combat.bFireRequestIssued)
      Mask |= 1u << 3;
    if (Local.Combat.ReactiveMode != Server.Combat.ReactiveMode
      || !QuantizedEqual(Local.Combat.HorizontalReactiveVelocity.X,
        Server.Combat.HorizontalReactiveVelocity.X, 10.0f)
      || !QuantizedEqual(Local.Combat.HorizontalReactiveVelocity.Y,
        Server.Combat.HorizontalReactiveVelocity.Y, 10.0f)
      || !QuantizedEqual(Local.Combat.VerticalReactiveVelocityCmps,
        Server.Combat.VerticalReactiveVelocityCmps, 10.0f)
      || Local.Combat.ReactiveStartFixedStep != Server.Combat.ReactiveStartFixedStep
      || Local.Combat.ReactiveEndFixedStep != Server.Combat.ReactiveEndFixedStep
      || Local.Combat.ReactiveRevision != Server.Combat.ReactiveRevision
      || Local.Combat.ApexCount != Server.Combat.ApexCount
      || Local.Combat.LandingCount != Server.Combat.LandingCount)
      Mask |= 1u << 4;
    if (Local.Combat.HitFlashRevision != Server.Combat.HitFlashRevision
      || Local.Combat.HitFlashProfileKey != Server.Combat.HitFlashProfileKey
      || Local.Combat.LastConsumedHitEventId != Server.Combat.LastConsumedHitEventId)
      Mask |= 1u << 5;
    if (Local.Combat.VisualState != Server.Combat.VisualState
      || Local.Combat.VisualRevision != Server.Combat.VisualRevision
      || Local.Combat.VisualPhaseSeed != Server.Combat.VisualPhaseSeed)
      Mask |= 1u << 6;
    return Mask;
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueBootstrap(const FCrowdDemoRoundBootstrapPacket& Packet)
{
  if (Packet.bValid != 0 && (!bBootstrapApplied || Packet.Revision >= PendingBootstrap.Revision))
  {
    PendingBootstrap = Packet;
    if (!bPlanActive)
    {
      LastClaimedPlanApplyBoundarySequence = MAX_uint64;
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueRoundPlan(const FCrowdDemoRoundPlanPacket& Packet)
{
  if (Packet.bValid != 0 && (!bPlanActive || Packet.Revision > ActivePlan.Revision))
  {
    PendingPlans.Add(Packet.Revision, Packet);
    LastCompareMetrics.RoundPlanRevisionSeen = FMath::Max(LastCompareMetrics.RoundPlanRevisionSeen, Packet.Revision);
    // The old round-end boundary may already have been inspected before the
    // next plan arrives. Re-open it just as late RoundResult arrival does.
    LastClaimedPlanApplyBoundarySequence = MAX_uint64;
  }
}

float FCrowdDemoRoundErrorSeries::GetMax() const
{
  float Maximum = -1.0f;
  for (const float Sample : CheckpointP95Samples)
  {
    Maximum = FMath::Max(Maximum, Sample);
  }
  return Maximum;
}

float FCrowdDemoRoundErrorSeries::GetExpansionFromFirst() const
{
  if (CheckpointP95Samples.Num() < 2)
  {
    return 0.0f;
  }
  float LaterMax = CheckpointP95Samples[1];
  for (int32 Index = 2; Index < CheckpointP95Samples.Num(); ++Index)
  {
    LaterMax = FMath::Max(LaterMax, CheckpointP95Samples[Index]);
  }
  return FMath::Max(0.0f, LaterMax - CheckpointP95Samples[0]);
}

bool UCrowdDemoRoundSimPipelineSubsystem::TryClaimPlanApplyBoundary()
{
  if (LastClaimedPlanApplyBoundarySequence == PlanApplyBoundarySequence)
  {
    return false;
  }
  LastClaimedPlanApplyBoundarySequence = PlanApplyBoundarySequence;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::EnsureFormationIndexCache(
  const TConstArrayView<int32> AgentIds)
{
  uint64 MembershipHash = 1469598103934665603ull ^ static_cast<uint64>(AgentIds.Num());
  for (const int32 AgentId : AgentIds)
  {
    uint64 Mixed = static_cast<uint64>(static_cast<uint32>(AgentId)) + 0x9e3779b97f4a7c15ull;
    Mixed = (Mixed ^ (Mixed >> 30)) * 0xbf58476d1ce4e5b9ull;
    Mixed = (Mixed ^ (Mixed >> 27)) * 0x94d049bb133111ebull;
    Mixed ^= Mixed >> 31;
    MembershipHash ^= Mixed;
  }
  if (FormationMembershipCount == AgentIds.Num()
    && FormationMembershipHash == MembershipHash
    && FormationIndexByAgentId.Num() == AgentIds.Num())
  {
    return;
  }

  TArray<int32> SortedAgentIds;
  SortedAgentIds.Append(AgentIds.GetData(), AgentIds.Num());
  SortedAgentIds.Sort();
  FormationIndexByAgentId.Empty(SortedAgentIds.Num());
  for (int32 Index = 0; Index < SortedAgentIds.Num(); ++Index)
  {
    FormationIndexByAgentId.Add(SortedAgentIds[Index], Index);
  }
  FormationMembershipCount = AgentIds.Num();
  FormationMembershipHash = MembershipHash;
  ++FormationCacheRebuildCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueRoundResult(const FCrowdDemoRoundResultPacket& Packet)
{
  if (Packet.bValid != 0)
  {
    const bool bNewResult = !PendingResults.Contains(Packet.CheckpointRevision);
    PendingResults.Add(Packet.CheckpointRevision, Packet);
    if (!bNewResult)
    {
      return;
    }
    ++RoundResultPipelineQueuedCount;
    // Network work may arrive after the stationary round-end boundary was
    // already inspected. Re-open that boundary without advancing simulation.
    LastClaimedPlanApplyBoundarySequence = MAX_uint64;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoRoundResultTransport role=client stage=pipeline_queued round_id=%d checkpoint_revision=%d agents=%d pipeline_queued_count=%d source=MassPipeline"),
      Packet.RoundId,
      Packet.CheckpointRevision,
      Packet.Agents.Num(),
      RoundResultPipelineQueuedCount);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueCorrectionFrame(
  const FCrowdDemoCorrectionFrame& Frame,
  const float ReceiveServerTimeSeconds)
{
  if (Frame.bValid != 0 && Frame.CorrectionRevision > LastAppliedCorrectionRevision)
  {
    PendingCorrections.Add(Frame.CorrectionRevision, TPair<FCrowdDemoCorrectionFrame, float>(Frame, ReceiveServerTimeSeconds));
    LastCorrectionMetrics.CorrectionFrameLatestRevisionSeen = FMath::Max(
      LastCorrectionMetrics.CorrectionFrameLatestRevisionSeen,
      Frame.CorrectionRevision);
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::PeekBootstrap(FCrowdDemoRoundBootstrapPacket& OutPacket) const
{
  if (bBootstrapApplied || PendingBootstrap.bValid == 0)
  {
    return false;
  }
  OutPacket = PendingBootstrap;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkBootstrapApplied(const int32 AgentCount)
{
  bBootstrapApplied = true;
  LastCompareMetrics.RoundBootstrapAgentCount = AgentCount;
  RoundInputHash = 0;
  RoundInitialStateHash = 0;
  RoundResetCount = 0;
  RoundTransitionOrderViolationCount = 0;
  DynamicFlowAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.DynamicAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.IntegrationRebuildCount = 0;
  DynamicFlowIntegrationRebuildCount = 0;
  DynamicFlowRoundHash = 2166136261u;
  bDynamicFlowIntegrationCacheInvalidated = false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopDueRoundPlan(
  const float BoundaryServerTimeSeconds,
  FCrowdDemoRoundPlanPacket& OutPacket)
{
  if (bPlanActive && GetWorld()
    && SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER
      >= ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds)
  {
    const bool bOldRoundFrozen = GetWorld()->GetNetMode() == NM_Client
      ? LastCompareMetrics.CompletedRoundCount >= ActivePlan.RoundId
      : LastBuiltResultRoundId >= ActivePlan.RoundId;
    if (!bOldRoundFrozen)
    {
      return false;
    }
  }
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, FCrowdDemoRoundPlanPacket>& Pair : PendingPlans)
  {
    if (Pair.Value.StartServerTimeSeconds <= BoundaryServerTimeSeconds + KINDA_SMALL_NUMBER
      && (SelectedRevision == INDEX_NONE || Pair.Key < SelectedRevision))
    {
      SelectedRevision = Pair.Key;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  OutPacket = PendingPlans.FindAndRemoveChecked(SelectedRevision);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopCorrectionForBoundary(
  FCrowdDemoCorrectionFrame& OutFrame,
  float& OutReceiveServerTimeSeconds)
{
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, TPair<FCrowdDemoCorrectionFrame, float>>& Pair : PendingCorrections)
  {
    const FCrowdDemoCorrectionFrame& Frame = Pair.Value.Key;
    if (Frame.CorrectionRevision > LastAppliedCorrectionRevision
      && Frame.RoundId == GetCurrentRoundId()
      && Frame.RoundRevision == GetCurrentPlanRevision()
      && Frame.ServerTimeSeconds <= SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER
      && Pair.Key > SelectedRevision)
    {
      SelectedRevision = Pair.Key;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  TPair<FCrowdDemoCorrectionFrame, float> Pair = PendingCorrections.FindAndRemoveChecked(SelectedRevision);
  OutFrame = MoveTemp(Pair.Key);
  OutReceiveServerTimeSeconds = Pair.Value;
  TArray<int32> OldRevisions;
  for (const TPair<int32, TPair<FCrowdDemoCorrectionFrame, float>>& Pending : PendingCorrections)
  {
    if (Pending.Key < SelectedRevision)
    {
      OldRevisions.Add(Pending.Key);
    }
  }
  for (const int32 Revision : OldRevisions)
  {
    PendingCorrections.Remove(Revision);
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopRoundResultForBoundary(FCrowdDemoRoundResultPacket& OutPacket)
{
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, FCrowdDemoRoundResultPacket>& Pair : PendingResults)
  {
    if (Pair.Value.RoundId == GetCurrentRoundId()
      && Pair.Value.Revision == GetCurrentPlanRevision()
      && Pair.Value.EndServerTimeSeconds <= SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER)
    {
      SelectedRevision = Pair.Key;
      break;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  OutPacket = PendingResults.FindAndRemoveChecked(SelectedRevision);
  ++RoundResultBoundaryAppliedCount;
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundResultTransport role=client stage=boundary_applied round_id=%d checkpoint_revision=%d agents=%d pipeline_queued_count=%d boundary_applied_count=%d source=MassPipeline"),
    OutPacket.RoundId,
    OutPacket.CheckpointRevision,
    OutPacket.Agents.Num(),
    RoundResultPipelineQueuedCount,
    RoundResultBoundaryAppliedCount);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::HasDueRoundPlan(const float BoundaryServerTimeSeconds) const
{
  for (const TPair<int32, FCrowdDemoRoundPlanPacket>& Pair : PendingPlans)
  {
    if (Pair.Value.StartServerTimeSeconds <= BoundaryServerTimeSeconds + KINDA_SMALL_NUMBER)
    {
      return true;
    }
  }
  return false;
}

void UCrowdDemoRoundSimPipelineSubsystem::ActivatePlan(
  const FCrowdDemoRoundPlanPacket& Packet,
  const int32 AgentCount,
  const bool bLate)
{
  if (bPlanActive && Packet.Revision > ActivePlan.Revision + 1)
  {
    LastCompareMetrics.RoundPlanGapCount += Packet.Revision - ActivePlan.Revision - 1;
  }
  ActivePlan = Packet;
  bPlanActive = true;
  BoundarySnapshot = {};
  BoundaryFormationFacts.Reset();
  BoundaryFacingFacts.Reset();
  BoundaryBusinessFacts.Reset();
  BoundaryOrchestrator.Reset();
  BoundaryFacingWorkState.Reset();
  PreparedTargetResourceSlots.Reset();
  PreparedMovementCommitPlan = {};
  PreparedMovementBoundaryCommit = {};
  PreparedBoundaryCommitEnvelope = {};
  PreparedRuntimeSharedFlowOutputs.Reset();
  PreparedObstacleMaxReprojectDeltaCm = -1.0f;
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedReactiveMotionSteps.Reset();
  PreparedCombatBoundaryCommit = {};
  PreparedRuntimeComposedGuidance.Reset();
  PreparedRuntimePredictedMovements.Reset();
  PreparedRuntimeParticleResults.Reset();
  PreparedRuntimeFinalKinematics.Reset();
  PreparedRuntimeFacingResults.Reset();
  PreparedFacingRollbackFacts.Reset();
  PreparedPostFinalizeAgentRecords.Reset();
  PreparedCheckpointAgentStates.Reset();
  PreparedParticleDiagnosticCommit = {};
  PreparedOpenSpawnBoundaryFacts.Reset();
  PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
  for (TArray<float>& Samples : RoundPerformanceStageMsSamples)
  {
    Samples.Reset();
  }
  FixedStepPipelineMsSamples.Reset();
  FixedStepsPerGameFrameSamples.Reset();
  RollbackReplayMsSamples.Reset();
  PerformanceCatchupFrameCount = 0;
  PerformanceCatchupCpuBudgetHitCount = 0;
  PerformanceCatchupCpuBudgetConsecutiveCount = 0;
  PerformanceCatchupCpuBudgetConsecutiveMax = 0;
  PerformanceMaxFixedStepsPerFrameHitCount = 0;
  PerformanceFixedStepBacklogMsMax = 0.0f;
  PerformanceRoundWallStartSeconds = FPlatformTime::Seconds();
  PerformanceRoundSimStartSeconds = Packet.StartServerTimeSeconds;
  PendingRollbackReplaySteps = 0;
  PendingRollbackReplayMilliseconds = 0.0f;
  PerformanceZeroErrorRollbackReplayCount = 0;
  PerformanceTargetTopologyBuildCount = 0;
  PerformanceTargetTopologyCacheHitCount = 0;
  PerformanceTargetDemandFullBuildCount = 0;
  PerformanceTargetDemandPopulationUpdateCount = 0;
  RoundInputHash = 0;
  RoundInitialStateHash = 0;
  RoundResetCount = 0;
  RoundTransitionOrderViolationCount = 0;
  DynamicFlowAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.DynamicAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.IntegrationRebuildCount = 0;
  DynamicFlowIntegrationRebuildCount = 0;
  DynamicFlowRoundHash = 2166136261u;
  bDynamicFlowIntegrationCacheInvalidated = false;
  bTargetStabilityDiagnosticPlanEnabled = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoTargetStabilityDiagnostic"))
    && Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && Packet.Rules.TargetRegionTransportSettings.bEnabled != 0;
  bTargetRegionPlanLifecycleDiagnosticPlanEnabled = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoTargetRegionPlanLifecycleDiagnostic"))
    && Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && Packet.Rules.TargetRegionTransportSettings.bEnabled != 0;
  CurrentFixedStepSeconds = FMath::Max(1.0f / 120.0f, Packet.Rules.FixedStepSeconds);
  if (LastCompareMetrics.RoundPlanAppliedCount == 0 || SimulatedServerTimeSeconds < Packet.StartServerTimeSeconds)
  {
    SimulatedServerTimeSeconds = Packet.StartServerTimeSeconds;
  }
  ++LastCompareMetrics.RoundPlanAppliedCount;
  LastCompareMetrics.CurrentRoundId = Packet.RoundId;
  LastCompareMetrics.RoundId = Packet.RoundId;
  LastCompareMetrics.Revision = Packet.Revision;
  LastCompareMetrics.RoundBootstrapAgentCount = AgentCount;
  if (bLate)
  {
    ++LastCompareMetrics.RoundPlanLateCount;
  }
  LastBuiltResultRoundId = FMath::Min(LastBuiltResultRoundId, Packet.RoundId - 1);
  CorrectionIntervalPositionP95Samples.Reset();
  CorrectionIntervalPositionMaxSamples.Reset();
  LastCompareMetrics.CorrectionIntervalPositionErrorCmP95 = -1.0f;
  LastCompareMetrics.CorrectionIntervalPositionErrorCmMax = -1.0f;
  if (IsFlowScenario(Packet.Rules.Scenario))
  {
    SoftPressureRollbackHistory.Reset();
    SoftPressureRollbackSnapshotHitCount = 0;
    SoftPressureRollbackSnapshotMissCount = 0;
    SoftPressureRollbackAgentMismatchCount = 0;
    SoftPressureRollbackReplayedStepCount = 0;
    FlowGoalReachedAgentIds.Reset();
    FlowWallPassAgentIds.Reset();
    FlowCorridorExitAgentIds.Reset();
    FlowTurnExitAgentIds.Reset();
    FlowLowSpeedSecondsByAgentId.Reset();
    FlowCorridorDeadlockAgentIds.Reset();
    LastCompareMetrics.FlowUnreachableAgentCount = 0;
    LastCompareMetrics.FlowGoalReachedCount = 0;
    LastCompareMetrics.FlowWallPassCount = 0;
    LastCompareMetrics.FlowCorridorExitCount = 0;
    LastCompareMetrics.FlowTurnExitCount = 0;
    LastCompareMetrics.ServerObstaclePenetrationCount = 0;
    LastCompareMetrics.ClientSimObstaclePenetrationCount = 0;
    LastCompareMetrics.CorridorDeadlockAgentCount = 0;
  }
  if (Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    TargetRegionPlanResources.Reset();
    ParticleSolverMillisecondsSamples.Reset();
    LastParticleCandidateSummary = FCrowdDemoParticleConstraintSummary();
    LastParticleAppliedSummary = FCrowdDemoParticleConstraintSummary();
    PreparedLocalPredictiveResults.Reset();
    LocalPredictiveGrantStates.Reset();
    LastLocalPredictiveSummary = FCrowdDemoLocalPredictiveSummary();
    LocalPredictiveDiagnosticFrame = FCrowdDemoLocalPredictiveDiagnosticFrame();
    LocalPredictiveComponentFixture = FCrowdDemoLocalPredictiveComponentFixture();
    LocalPredictiveRoundHash = 2166136261u;
    LocalPredictiveSampleCount = 0;
    LocalPredictiveInvalidStepCount = 0;
    GuidanceCandidateRoundHash = 2166136261u;
    GuidanceComposeRoundHash = 2166136261u;
    GuidanceComposeSampleCount = 0;
    ParticleCandidateStateHash = 2166136261u;
    ParticleAppliedStateHash = 2166136261u;
    ParticleInvalidStepCount = 0;
    ParticleGlobalFallbackStepCount = 0;
    ParticleStepCount = 0;
    CrossProfileHardViolationCount = 0;
    CrossProfileSweptViolationCount = 0;
    ParticleSettlingWindowCount = 0;
    ParticleSettlingSteps = INDEX_NONE;
    ParticlePreviousSoftErrorP95 = -1.0f;
    bParticleConstraintRunFailure = false;
    ParticleFailureFixture = FCrowdDemoParticleFailureFixture();
    PreparedProjectiles.Reset();
    OutgoingProjectileVisualEvents.Reset();
    ProjectileMetrics = FCrowdDemoProjectileMetrics();
    ProjectileMetrics.bValid = 1;
    OpenSpawnRelaxationLayout = {};
    OpenSpawnRelaxationRuntime = {};
    OpenCohortMovementLayout = {};
    OpenCohortMovementProgress = {};
    BidirectionalSwapLayout = {};
    BidirectionalSwapProgress = {};
    ValidCorridorTransitLayout = {};
    ValidCorridorTransitProgress = {};
    SoftPressureRouteDiagnosticRuntime = {};
    SoftPressureRouteDiagnosticSummary = {};
    TargetFact = FCrowdDemoTargetFact();
    TargetStabilityRuntime = {};
    TargetStabilityRuntime.Settings.ExpectedAgentCount = AgentCount;
    TargetStabilityRuntime.Settings.PositionQuantumCm =
      Packet.Rules.ParticlePositionQuantumCm;
    TargetStabilitySummary = {};
    PreparedTargetRegionTopology = {};
    TargetRegionTopologySummary = {};
    PreparedTargetRegionAgents.Reset();
    PreparedTargetRegionDemand = {};
    PreparedTargetRegionPlan = {};
    TargetRegionQuotaExecution = {};
    TargetRegionPlanValidation = {};
    PreparedTargetRegionGuidance.Reset();
    TargetRegionGuidanceSummary = {};
    TargetRegionTopologyRoundHash = 2166136261u;
    TargetRegionDemandRoundHash = 2166136261u;
    TargetRegionTransportRoundHash = 2166136261u;
    TargetRegionGuidanceRoundHash = 2166136261u;
    TargetRegionPlanRebuildCount = 0;
    TargetRegionLifetimeRebuildCount = 0;
    TargetRegionTargetRebuildCount = 0;
    TargetRegionEnvironmentRebuildCount = 0;
    TargetRegionMembershipRebuildCount = 0;
    TargetRegionDemandSatisfiedRebuildCount = 0;
    TargetRegionPathInvalidRebuildCount = 0;
    TargetRegionSolverMillisecondsSamples.Reset();
    bTargetRegionRoundValid = true;
    TargetRegionInvalidStepCount = 0;
    TargetRegionLastInvalidStep = INDEX_NONE;
    TargetRegionValidationFailureCount = 0;
    TargetRegionValidationRoundHash = 2166136261u;
    TargetRegionGuidanceUnroutedStepCount = 0;
    TargetRegionGuidanceUnroutedAgentSampleCount = 0;
    TargetRegionGuidanceUnroutedAgentMax = 0;
    TargetRegionGuidanceFirstFailureStep = INDEX_NONE;
    TargetRegionGuidanceFirstFailureAgentId = INDEX_NONE;
    bTargetRegionFailureFixtureValid = false;
    TargetRegionFailureFixtureStep = INDEX_NONE;
    TargetRegionFailureFixtureKind = 0;
    TargetRegionFailureFixtureAgentId = INDEX_NONE;
    TargetRegionFailureFixtureCellKey = INDEX_NONE;
    TargetRegionFailureFixtureHash = 0;
    TargetRegionCapabilityCohorts.Reset();
    CapabilityProfileSummary = {};
    CapabilityCohortRebuildCount = 0;
    TargetRegionPlanLifecycleSummary = {};
    TargetRegionPlanLifecycleFixture = {};
    LastCompareMetrics.InitialOverlapPairCount = 0;
  }
  const UWorld* World = GetWorld();
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundInit role=%s round_id=%d revision=%d previous_checkpoint_revision=%d agents=%d start_server_time=%.3f duration=%.3f nominal_duration=%.3f completion_grace=%.3f fixed_step=%.4f scenario=%d plan_late=%d source=MassPipeline"),
    World && World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
    Packet.RoundId,
    Packet.Revision,
    Packet.PreviousCheckpointRevision,
    AgentCount,
    Packet.StartServerTimeSeconds,
    Packet.DurationSeconds,
    Packet.NominalDurationSeconds,
    Packet.CompletionGraceSeconds,
    CurrentFixedStepSeconds,
    static_cast<int32>(Packet.Rules.Scenario),
    bLate ? 1 : 0);
}

bool UCrowdDemoRoundSimPipelineSubsystem::EnsureSharedFlowField(
  const FCrowdDemoSharedFlowFieldConfig& Config)
{
  FCrowdMassSharedFlowBuildInput Input;
  Input.Config = FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(Config);
  const FCrowdMassSharedFlowBuildOutput Output =
    FCrowdMassSharedFlowWork::EnsureResource(
      Input, RuntimeSharedFlowResource);
  if (!Output.bValid) return false;
  if (Output.bFieldRebuilt)
  {
    SharedFlowField = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(
      RuntimeSharedFlowResource.Field);
    SharedFlowFieldRebuildCount = RuntimeSharedFlowResource.FieldRebuildCount;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSharedFlowField: role=%s revision=%d hash=%u rebuild_count=%d cells=%d blocked=%d goal_cell=%d"),
      GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
      Config.Revision,
      SharedFlowField.BuildHash,
      SharedFlowFieldRebuildCount,
      SharedFlowField.Width * SharedFlowField.Height,
      SharedFlowField.BlockedCellCount,
      SharedFlowField.GoalCellIndex);
  }
  else if (!SharedFlowField.IsValid()
    || SharedFlowField.BuildHash != RuntimeSharedFlowResource.Field.BuildHash)
  {
    SharedFlowField = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(
      RuntimeSharedFlowResource.Field);
  }
  LastCompareMetrics.FlowFieldRevision = SharedFlowField.Config.Revision;
  LastCompareMetrics.FlowFieldBuildHash = SharedFlowField.BuildHash;
  LastCompareMetrics.FlowFieldRebuildCount = SharedFlowFieldRebuildCount;
  LastCompareMetrics.FlowBlockedCellCount = SharedFlowField.BlockedCellCount;
  return SharedFlowField.IsValid();
}

bool UCrowdDemoRoundSimPipelineSubsystem::PublishBoundarySnapshot(
  FCrowdMassBoundarySnapshot&& Snapshot,
  TArray<FCrowdDemoRoundBoundaryFormationFact>&& FormationFacts,
  TArray<FCrowdDemoRoundBoundaryFacingFact>&& FacingFacts)
{
  TArray<FCrowdDemoRoundBoundaryBusinessFact> BusinessFacts;
  BusinessFacts.Reserve(Snapshot.Agents.Num());
  for (FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
  {
    if (!Agent.AgentFacts.StableEntityRef.IsValid())
    {
      const FCrowdStableEntityRef FixtureRef = {
        1u,
        static_cast<uint64>(FMath::Max(0, Agent.Identity.AgentId)) + 1u,
        FMath::Max<uint32>(1u, Agent.Identity.LifecycleSerial)};
      Agent.Identity.SetStableEntityRef(FixtureRef);
      Agent.AgentFacts.StableEntityRef = FixtureRef;
    }
    FCrowdDemoRoundBoundaryBusinessFact& Fact =
      BusinessFacts.AddDefaulted_GetRef();
    Fact.EntityRef = Agent.AgentFacts.StableEntityRef;
    Fact.AgentId = Agent.Identity.AgentId;
  }
  return PublishBoundarySnapshot(
    MoveTemp(Snapshot), MoveTemp(FormationFacts), MoveTemp(FacingFacts),
    MoveTemp(BusinessFacts));
}

bool UCrowdDemoRoundSimPipelineSubsystem::PublishBoundarySnapshot(
  FCrowdMassBoundarySnapshot&& Snapshot,
  TArray<FCrowdDemoRoundBoundaryFormationFact>&& FormationFacts,
  TArray<FCrowdDemoRoundBoundaryFacingFact>&& FacingFacts,
  TArray<FCrowdDemoRoundBoundaryBusinessFact>&& BusinessFacts)
{
  if (!Snapshot.bValid
    || Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Snapshot.PlanRevision != GetCurrentPlanRevision()
    || Snapshot.Agents.Num() != FormationFacts.Num()
    || Snapshot.Agents.Num() != FacingFacts.Num()
    || Snapshot.Agents.Num() != BusinessFacts.Num())
    return false;
  FormationFacts.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  FacingFacts.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  BusinessFacts.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  for (int32 Index = 0; Index < Snapshot.Agents.Num(); ++Index)
    if (FormationFacts[Index].AgentId
        != Snapshot.Agents[Index].Identity.AgentId
      || FacingFacts[Index].AgentId
        != Snapshot.Agents[Index].Identity.AgentId
      || BusinessFacts[Index].EntityRef
        != Snapshot.Agents[Index].AgentFacts.StableEntityRef
      || BusinessFacts[Index].AgentId
        != Snapshot.Agents[Index].Identity.AgentId
      || FacingFacts[Index].ConsecutiveFinalSettleSteps < 0)
      return false;
  BoundarySnapshot = MoveTemp(Snapshot);
  MovementFinalizeAppliedFixedStepIndex = INDEX_NONE;
  BoundaryFormationFacts = MoveTemp(FormationFacts);
  BoundaryFacingFacts = MoveTemp(FacingFacts);
  BoundaryBusinessFacts = MoveTemp(BusinessFacts);
  BoundaryOrchestrator.Reset();
  BoundaryFacingWorkState.Reset();
  PreparedTargetResourceSlots.Reset();
  PreparedMovementCommitPlan = {};
  PreparedMovementBoundaryCommit = {};
  PreparedBoundaryCommitEnvelope = {};
  PreparedRuntimeSharedFlowOutputs.Reset();
  PreparedObstacleMaxReprojectDeltaCm = -1.0f;
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedReactiveMotionSteps.Reset();
  PreparedRuntimeComposedGuidance.Reset();
  PreparedRuntimePredictedMovements.Reset();
  PreparedRuntimeParticleResults.Reset();
  PreparedRuntimeFinalKinematics.Reset();
  PreparedRuntimeFacingResults.Reset();
  PreparedFacingRollbackFacts.Reset();
  PreparedPostFinalizeAgentRecords.Reset();
  PreparedCheckpointAgentStates.Reset();
  PreparedOpenSpawnBoundaryFacts.Reset();
  PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::BeginBoundaryTransaction(
  const double GatherMilliseconds)
{
  if (!IsInGameThread() || !IsBoundarySnapshotCurrent()
    || BoundaryOrchestrator.IsValid())
    return false;
  BoundaryOrchestrator =
    MakeUnique<FCrowdMassBoundaryRunner>();
  if (!BoundaryOrchestrator->Begin(BoundarySnapshot, GatherMilliseconds))
    return false;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryBusinessWork()
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || !IsBoundarySnapshotCurrent())
    return false;
  if (!BoundaryFacingWorkState.IsValid())
  {
    BoundaryFacingWorkState =
      MakeShared<FCrowdDemoBoundaryFacingWorkState,
        ESPMode::ThreadSafe>();
  }
  if (BoundaryFacingWorkState->bBusinessStaged)
    return false;
  FCrowdDemoBoundaryBusinessWorkInput& Input =
    BoundaryFacingWorkState->BusinessInput;
  Input.Snapshot = BoundarySnapshot;
  Input.Facts = BoundaryBusinessFacts;
  Input.Rules = ActivePlan.Rules;
  Input.Projectiles = PreparedProjectiles;
  Input.RoundId = GetCurrentRoundId();
  Input.FixedStepIndex = GetCurrentFixedStepIndex();
  Input.PlanRevision = GetCurrentPlanRevision();
  Input.StepEndServerTimeSeconds =
    GetCurrentStepEndServerTimeSeconds();
  Input.FixedStepSeconds = GetCurrentFixedStepSeconds();
  BoundaryFacingWorkState->bBusinessStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundarySharedFlowWork(
  const FCrowdMassSharedFlowSampleInput& Input)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || Input.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.PlanRevision != GetCurrentPlanRevision()
    || Input.Agents.Num() != BoundarySnapshot.Agents.Num())
    return false;
  if (!BoundaryFacingWorkState.IsValid())
  {
    BoundaryFacingWorkState =
      MakeShared<FCrowdDemoBoundaryFacingWorkState,
        ESPMode::ThreadSafe>();
  }
  if (BoundaryFacingWorkState->bSharedFlowStaged)
    return false;
  BoundaryFacingWorkState->GraphInput.SharedFlow = Input;
  BoundaryFacingWorkState->bSharedFlowStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetTopologyWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionTopologyInput& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bSharedFlowStaged
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering)
    return false;
  for (const auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
    if (Slot.CohortKey == CohortKey)
      return false;
  auto& Slot =
    BoundaryFacingWorkState->TargetTopologySlots.AddDefaulted_GetRef();
  Slot.CohortKey = CohortKey;
  Slot.Input = Input;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetDemandWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionDemandInput& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid())
    return false;
  for (auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (Slot.CohortKey != CohortKey) continue;
    if (Slot.bDemandStaged) return false;
    Slot.DemandInput = Input;
    Slot.bDemandStaged = true;
    return true;
  }
  return false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetPlanWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionPlanInput& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid())
    return false;
  for (auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (Slot.CohortKey != CohortKey) continue;
    if (!Slot.bDemandStaged || Slot.bPlanStaged) return false;
    Slot.PlanInput = Input;
    Slot.bPlanStaged = true;
    return true;
  }
  return false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetGuidanceWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionGuidanceInput& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid())
    return false;
  for (auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (Slot.CohortKey != CohortKey) continue;
    if (!Slot.bPlanStaged || Slot.bGuidanceStaged) return false;
    Slot.GuidanceInput = Input;
    Slot.bGuidanceStaged = true;
    return true;
  }
  return false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryMovementWork(
  FCrowdMassMovementPipelineWorkInput&& Input)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || (BoundaryFacingWorkState.IsValid()
      && !BoundaryFacingWorkState->bSharedFlowStaged)
    || (BoundaryFacingWorkState.IsValid()
      && BoundaryFacingWorkState->bMovementStaged)
    || Input.Guidance.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Guidance.PlanRevision != GetCurrentPlanRevision())
    return false;
  if (!BoundaryFacingWorkState.IsValid())
  {
    BoundaryFacingWorkState =
      MakeShared<FCrowdDemoBoundaryFacingWorkState, ESPMode::ThreadSafe>();
  }
  BoundaryFacingWorkState->GraphInput.Movement = MoveTemp(Input);
  BoundaryFacingWorkState->bMovementStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryMovementWork(
  FCrowdMassMovementPipelineWorkOutput& OutOutput)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || BoundaryFacingWorkState->bMovementConsumed
    || !BoundaryFacingWorkState->GraphOutput.Movement.bCompleted
    || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;
  OutOutput = MoveTemp(BoundaryFacingWorkState->GraphOutput.Movement);
  BoundaryFacingWorkState->bMovementConsumed = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryParticleWork(
  FCrowdMassParticlePipelineWorkInput&& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bMovementStaged
    || BoundaryFacingWorkState->bParticleStaged
    || Input.Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Snapshot.PlanRevision != GetCurrentPlanRevision())
    return false;
  Input.PredictedMovements.Reset();
  BoundaryFacingWorkState->GraphInput.ParticleTemplate = MoveTemp(Input);
  BoundaryFacingWorkState->bParticleStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryObstacleWork(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const float FixedStepSeconds)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bMovementStaged
    || BoundaryFacingWorkState->bObstacleStaged
    || FixedStepSeconds <= 0.0f)
    return false;
  BoundaryFacingWorkState->ObstacleConfig = Config;
  BoundaryFacingWorkState->ObstacleFixedStepSeconds =
    FixedStepSeconds;
  BoundaryFacingWorkState->bObstacleStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryParticleWork(
  FCrowdMassParticlePipelineWorkOutput& OutOutput)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || BoundaryFacingWorkState->bParticleConsumed
    || !BoundaryFacingWorkState->GraphOutput.Particle.bCompleted
    || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;
  OutOutput = MoveTemp(BoundaryFacingWorkState->GraphOutput.Particle);
  BoundaryFacingWorkState->bParticleConsumed = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  DispatchBoundarySoftPressureWorkGraph(
    FCrowdMassFacingFinalizeWorkInput&& Input,
    TMap<int32, int32>&& PreviousSettleStepsByAgentId,
    TMap<int32, bool>&& TerminalOwnerByAgentId)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bBusinessStaged
    || !BoundaryFacingWorkState->bSharedFlowStaged
    || !BoundaryFacingWorkState->bMovementStaged
    || !BoundaryFacingWorkState->bParticleStaged
    || Input.Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Snapshot.PlanRevision != GetCurrentPlanRevision()
    || PreviousSettleStepsByAgentId.Num() != BoundarySnapshot.Agents.Num()
    || TerminalOwnerByAgentId.Num() != BoundarySnapshot.Agents.Num())
    return false;

  const auto State = BoundaryFacingWorkState;
  State->PreviousSettleStepsByAgentId =
    MoveTemp(PreviousSettleStepsByAgentId);
  State->TerminalOwnerByAgentId = MoveTemp(TerminalOwnerByAgentId);
  State->GraphInput.FacingSettings = Input.Facing.Settings;
  State->GraphInput.FacingTemplates.Reserve(Input.Facing.Agents.Num());
  for (FCrowdFacingInput& FacingInput : Input.Facing.Agents)
  {
    FCrowdMassBoundaryFacingTemplate& Template =
      State->GraphInput.FacingTemplates.AddDefaulted_GetRef();
    Template.Input = MoveTemp(FacingInput);
  }

  const uint64 SnapshotHash = BoundarySnapshot.StableHash;
  const auto AddHashTask = [this, SnapshotHash](
    const FCrowdBoundaryTaskKey Key,
    const TConstArrayView<FCrowdBoundaryTaskKey> Prerequisites)
  {
    return BoundaryOrchestrator->AddTask(
      Key, Prerequisites,
      [SnapshotHash, Key]
      {
        uint64 Hash = FoldBoundaryHash(SnapshotHash, 1);
        Hash = FoldBoundaryHash(
          Hash, static_cast<uint8>(Key.Stage));
        Hash = FoldBoundaryHash(Hash, Key.CohortKey);
        return FCrowdBoundaryTaskResult::Success(Hash);
      },
      false);
  };
  const FCrowdBoundaryTaskKey Business = {
    ECrowdBoundaryTaskStage::BusinessPrepare, 0};
  const FCrowdBoundaryTaskKey SharedFlow = {
    ECrowdBoundaryTaskStage::SharedFlow, 0};
  const FCrowdBoundaryTaskKey Movement = {
    ECrowdBoundaryTaskStage::Movement, 0};
  const FCrowdBoundaryTaskKey Particle = {
    ECrowdBoundaryTaskStage::Particle, 0};
  const FCrowdBoundaryTaskKey Facing = {
    ECrowdBoundaryTaskStage::FacingFinalize, 0};
  TArray<FCrowdBoundaryTaskKey> MovementDeps = {Business, SharedFlow};
  const bool bHasTargetWork = !State->TargetTopologySlots.IsEmpty();
  const FCrowdBoundaryTaskKey ParticleDeps[] = {Movement};
  const FCrowdBoundaryTaskKey FacingDeps[] = {Particle};
  bool bRegistered =
    BoundaryOrchestrator->AddTask(
      Business, {},
      [State]
      {
        State->BusinessOutput =
          RunBoundaryBusinessWork(State->BusinessInput);
        return State->BusinessOutput.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->BusinessOutput.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      SharedFlow, {},
      [State]
      {
        State->GraphOutput.SharedFlow =
          FCrowdMassSharedFlowWork::BuildPreferred(
            State->GraphInput.SharedFlow);
        return State->GraphOutput.SharedFlow.bValid
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.SharedFlow.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      });
  if (bRegistered && bHasTargetWork)
  {
    State->TargetTopologySlots.Sort([](const auto& A, const auto& B)
    {
      return A.CohortKey < B.CohortKey;
    });
    for (int32 SlotIndex = 0;
      bRegistered && SlotIndex < State->TargetTopologySlots.Num();
      ++SlotIndex)
    {
      const uint32 CohortKey =
        State->TargetTopologySlots[SlotIndex].CohortKey;
      const FCrowdBoundaryTaskKey Topology = {
        ECrowdBoundaryTaskStage::TargetTopology, CohortKey};
      const FCrowdBoundaryTaskKey Demand = {
        ECrowdBoundaryTaskStage::TargetDemand, CohortKey};
      const FCrowdBoundaryTaskKey Plan = {
        ECrowdBoundaryTaskStage::TargetPlan, CohortKey};
      const FCrowdBoundaryTaskKey Guidance = {
        ECrowdBoundaryTaskStage::TargetGuidance, CohortKey};
      const FCrowdBoundaryTaskKey DemandDeps[] = {
        SharedFlow, Topology};
      const FCrowdBoundaryTaskKey PlanDeps[] = {Demand};
      const FCrowdBoundaryTaskKey GuidanceDeps[] = {Plan};
      MovementDeps.Add(Guidance);
      bRegistered = BoundaryOrchestrator->AddTask(
        Topology, {},
        [State, SlotIndex]
        {
          auto& Slot = State->TargetTopologySlots[SlotIndex];
          Slot.Output =
            FCrowdMassTargetRegionWork::BuildTopology(Slot.Input);
          return Slot.Output.bValid
            ? FCrowdBoundaryTaskResult::Success(
                FoldBoundaryHash(
                  Slot.Output.Summary.TopologyHash,
                  Slot.CohortKey))
            : FCrowdBoundaryTaskResult::Failure();
        })
        && BoundaryOrchestrator->AddTask(
          Demand, DemandDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bDemandStaged)
              return FCrowdBoundaryTaskResult::Failure();
            FCrowdMassTargetRegionDemandInput DemandInput =
              Slot.DemandInput;
            DemandInput.Topology = Slot.Output.Topology;
            TMap<int32, const FCrowdMassSharedFlowAgentOutput*>
              FlowById;
            for (const FCrowdMassSharedFlowAgentOutput& Flow
              : State->GraphOutput.SharedFlow.Agents)
              FlowById.Add(Flow.AgentId, &Flow);
            const auto JoinFarFlow =
              [&FlowById, &DemandInput](
                FCrowdTargetRegionTransportAgent& Agent)
            {
              const FCrowdMassSharedFlowAgentOutput* const* Flow =
                FlowById.Find(Agent.AgentId);
              if (!Flow) return false;
              Agent.FarFlowPreferredVelocity =
                FCrowdTargetRegionTransportKernel::
                  ComposeTargetAdvectedFarFlowVelocity(
                    FVector2f(
                      (*Flow)->Candidate.PreferredVelocity.X,
                      (*Flow)->Candidate.PreferredVelocity.Y),
                    DemandInput.Settings.TargetVelocity,
                    Agent.MaxSpeedCmps);
              return true;
            };
            for (FCrowdTargetRegionTransportAgent& Agent
              : DemandInput.Agents)
              if (!JoinFarFlow(Agent))
                return FCrowdBoundaryTaskResult::Failure();
            for (FCrowdTargetRegionTransportAgent& Agent
              : DemandInput.ExternalAgents)
              if (!JoinFarFlow(Agent))
                return FCrowdBoundaryTaskResult::Failure();
            Slot.DemandOutput =
              FCrowdMassTargetRegionWork::BuildDemand(DemandInput);
            return Slot.DemandOutput.bValid
              ? FCrowdBoundaryTaskResult::Success(
                  Slot.DemandOutput.Demand.DemandHash)
              : FCrowdBoundaryTaskResult::Failure();
          })
        && BoundaryOrchestrator->AddTask(
          Plan, PlanDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bPlanStaged)
              return FCrowdBoundaryTaskResult::Failure();
            FCrowdMassTargetRegionPlanInput PlanInput =
              Slot.PlanInput;
            PlanInput.Topology = Slot.Output.Topology;
            PlanInput.Demand = Slot.DemandOutput.Demand;
            Slot.PlanOutput =
              FCrowdMassTargetRegionWork::SolvePlan(PlanInput);
            return Slot.PlanOutput.bValid
              ? FCrowdBoundaryTaskResult::Success(
                  FoldBoundaryHash(
                    Slot.PlanOutput.Plan.TransportHash,
                    Slot.PlanOutput.Execution.ExecutionHash))
              : FCrowdBoundaryTaskResult::Failure();
          })
        && BoundaryOrchestrator->AddTask(
          Guidance, GuidanceDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bGuidanceStaged)
              return FCrowdBoundaryTaskResult::Failure();
            FCrowdMassTargetRegionGuidanceInput GuidanceInput =
              Slot.GuidanceInput;
            GuidanceInput.Topology = Slot.Output.Topology;
            GuidanceInput.Demand = Slot.DemandOutput.Demand;
            GuidanceInput.Plan = Slot.PlanOutput.Plan;
            GuidanceInput.Execution = Slot.PlanOutput.Execution;
            Slot.GuidanceOutput =
              FCrowdMassTargetRegionWork::BuildGuidance(
                GuidanceInput);
            return Slot.GuidanceOutput.bValid
              ? FCrowdBoundaryTaskResult::Success(
                  Slot.GuidanceOutput.Summary.GuidanceHash)
              : FCrowdBoundaryTaskResult::Failure();
          });
    }
  }
  bRegistered = bRegistered
    && BoundaryOrchestrator->AddTask(
      Movement, MovementDeps,
      [State]
      {
        FCrowdMassMovementPipelineWorkInput MovementInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildMovementInput(
            State->GraphInput, State->GraphOutput.SharedFlow,
            MovementInput))
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, FCrowdMassGatherRecord*> RecordById;
        for (FCrowdMassGatherRecord& Record
          : MovementInput.Guidance.Records)
          RecordById.Add(Record.Identity.AgentId, &Record);
        for (const FCrowdGuidanceCandidate& Candidate
          : State->BusinessOutput.GuidanceCandidates)
        {
          FCrowdMassGatherRecord* const* Record =
            RecordById.Find(Candidate.AgentId);
          if (!Record)
            return FCrowdBoundaryTaskResult::Failure();
          (*Record)->Guidance.BusinessOverride = Candidate;
        }
        TMap<int32, FCrowdMassMovementPipelineAgentOverlay*>
          OverlayById;
        for (FCrowdMassMovementPipelineAgentOverlay& Overlay
          : MovementInput.AgentOverlays)
          OverlayById.Add(Overlay.AgentId, &Overlay);
        for (const FCrowdDemoPreparedReactiveMotionStep& Step
          : State->BusinessOutput.ReactiveSteps)
        {
          FCrowdMassMovementPipelineAgentOverlay* const* Overlay =
            OverlayById.Find(Step.AgentId);
          if (!Overlay)
            return FCrowdBoundaryTaskResult::Failure();
          if (Step.bActive)
          {
            (*Overlay)->bVerticalOverride = true;
            (*Overlay)->ProposedZ = Step.ProposedZ;
            (*Overlay)->VerticalVelocityCmps =
              Step.VerticalVelocityCmps;
          }
        }
        if (!State->TargetTopologySlots.IsEmpty())
        {
          int32 JoinedTargetCount = 0;
          for (const auto& Slot : State->TargetTopologySlots)
          {
            for (const FCrowdTargetRegionGuidanceResult& Guidance
              : Slot.GuidanceOutput.Results)
            {
              FCrowdMassGatherRecord* const* Record =
                RecordById.Find(Guidance.AgentId);
              if (!Record)
                return FCrowdBoundaryTaskResult::Failure();
              const FVector DesiredVelocity(
                Guidance.DesiredVelocity.X,
                Guidance.DesiredVelocity.Y, 0.0f);
              const FVector DesiredLocation(
                Slot.GuidanceInput.Settings.TargetLocation.X,
                Slot.GuidanceInput.Settings.TargetLocation.Y,
                (*Record)->State.Position.Z);
              const float DesiredYaw = DesiredVelocity.IsNearlyZero()
                ? (*Record)->State.YawDegrees
                : DesiredVelocity.Rotation().Yaw;
              (*Record)->Guidance.TargetRegion =
                FCrowdGuidanceComposeKernel::BuildCandidate(
                  Guidance.AgentId,
                  ECrowdGuidanceProvider::TargetRegion,
                  MovementInput.Guidance.PlanRevision,
                  DesiredVelocity, DesiredLocation, DesiredYaw,
                  Guidance.Mode
                    != ECrowdTargetRegionGuidanceMode::Unrouted);
              ++JoinedTargetCount;
            }
          }
          if (JoinedTargetCount != RecordById.Num())
            return FCrowdBoundaryTaskResult::Failure();
        }
        State->GraphOutput.Movement =
          FCrowdMassMovementPipelineWork::Run(
            MovementInput);
        return State->GraphOutput.Movement.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.Movement.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Particle, ParticleDeps,
      [State]
      {
        FCrowdMassParticlePipelineWorkInput ParticleInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildParticleInput(
            State->GraphInput, State->GraphOutput.Movement,
            ParticleInput))
          return FCrowdBoundaryTaskResult::Failure();
        State->GraphOutput.Particle =
          FCrowdMassParticlePipelineWork::Run(ParticleInput);
        return State->GraphOutput.Particle.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.Particle.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Facing, FacingDeps,
      [State]
      {
        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildFacingInput(
            State->GraphInput, State->GraphOutput.Movement,
            State->GraphOutput.Particle, FacingInput))
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, const FCrowdParticleConstraintResult*> ParticleById;
        for (const FCrowdParticleConstraintResult& Result
          : State->GraphOutput.Particle.PublishPlan.PreparedResults)
          ParticleById.Add(Result.AgentId, &Result);
        for (FCrowdFacingInput& Agent : FacingInput.Facing.Agents)
        {
          const FCrowdParticleConstraintResult* const* ParticleResult =
            ParticleById.Find(Agent.AgentId);
          const int32* Previous =
            State->PreviousSettleStepsByAgentId.Find(Agent.AgentId);
          const bool* bTerminal =
            State->TerminalOwnerByAgentId.Find(Agent.AgentId);
          if (!ParticleResult || !Previous || !bTerminal)
            return FCrowdBoundaryTaskResult::Failure();
          const bool bSettledThisStep = *bTerminal
            && FVector2f((*ParticleResult)->CorrectedVelocity.X,
              (*ParticleResult)->CorrectedVelocity.Y).Size() <= 20.0f
            && (*ParticleResult)->RealizedCorrection.Size2D() <= 1.0f;
          const int32 Consecutive =
            bSettledThisStep ? *Previous + 1 : 0;
          Agent.bFinalPositionSettled = Consecutive >= 15;
          State->ConsecutiveSettleStepsByAgentId.Add(
            Agent.AgentId, Consecutive);
          State->FinalSettledByAgentId.Add(
            Agent.AgentId, Agent.bFinalPositionSettled);
        }
        State->GraphOutput.FacingFinalize =
          FCrowdMassFacingFinalizeWork::Run(FacingInput);
        State->Output = State->GraphOutput.FacingFinalize;
        State->bCompleted = State->Output.bCompleted;
        return State->bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->Output.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      });
  if (!bRegistered || !BoundaryOrchestrator->Dispatch())
  {
    BoundaryOrchestrator->Fail();
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return false;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::DispatchBoundaryFacingWork(
  FCrowdMassFacingFinalizeWorkInput&& Input,
  TMap<int32, int32>&& ConsecutiveSettleStepsByAgentId,
  TMap<int32, bool>&& FinalSettledByAgentId)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bBusinessStaged
    || !BoundaryFacingWorkState->bSharedFlowStaged
    || !BoundaryFacingWorkState->bMovementStaged
    || !BoundaryFacingWorkState->bObstacleStaged
    || Input.Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Snapshot.PlanRevision != GetCurrentPlanRevision())
    return false;

  BoundaryFacingWorkState->ConsecutiveSettleStepsByAgentId =
    MoveTemp(ConsecutiveSettleStepsByAgentId);
  BoundaryFacingWorkState->FinalSettledByAgentId =
    MoveTemp(FinalSettledByAgentId);
  const auto State = BoundaryFacingWorkState;
  State->GraphInput.FacingSettings = Input.Facing.Settings;
  State->GraphInput.FacingTemplates.Reserve(Input.Facing.Agents.Num());
  for (FCrowdFacingInput& FacingInput : Input.Facing.Agents)
  {
    FCrowdMassBoundaryFacingTemplate& Template =
      State->GraphInput.FacingTemplates.AddDefaulted_GetRef();
    Template.Input = MoveTemp(FacingInput);
  }
  const FCrowdBoundaryTaskKey Business = {
    ECrowdBoundaryTaskStage::BusinessPrepare, 0};
  const FCrowdBoundaryTaskKey SharedFlow = {
    ECrowdBoundaryTaskStage::SharedFlow, 0};
  const FCrowdBoundaryTaskKey Movement = {
    ECrowdBoundaryTaskStage::Movement, 0};
  const FCrowdBoundaryTaskKey Constraint = {
    ECrowdBoundaryTaskStage::HostConstraint, 0};
  const FCrowdBoundaryTaskKey Facing = {
    ECrowdBoundaryTaskStage::FacingFinalize, 0};
  const FCrowdBoundaryTaskKey MovementDeps[] = {Business, SharedFlow};
  const FCrowdBoundaryTaskKey ConstraintDeps[] = {Movement};
  const FCrowdBoundaryTaskKey FacingDeps[] = {Constraint};
  const bool bRegistered =
    BoundaryOrchestrator->AddTask(
      Business, {},
      [State]
      {
        State->BusinessOutput =
          RunBoundaryBusinessWork(State->BusinessInput);
        return State->BusinessOutput.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->BusinessOutput.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      SharedFlow, {},
      [State]
      {
        State->GraphOutput.SharedFlow =
          FCrowdMassSharedFlowWork::BuildPreferred(
            State->GraphInput.SharedFlow);
        return State->GraphOutput.SharedFlow.bValid
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.SharedFlow.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Movement, MovementDeps,
      [State]
      {
        FCrowdMassMovementPipelineWorkInput MovementInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildMovementInput(
            State->GraphInput, State->GraphOutput.SharedFlow,
            MovementInput))
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, FCrowdMassGatherRecord*> RecordById;
        for (FCrowdMassGatherRecord& Record
          : MovementInput.Guidance.Records)
          RecordById.Add(Record.Identity.AgentId, &Record);
        for (const FCrowdGuidanceCandidate& Candidate
          : State->BusinessOutput.GuidanceCandidates)
        {
          FCrowdMassGatherRecord* const* Record =
            RecordById.Find(Candidate.AgentId);
          if (!Record) return FCrowdBoundaryTaskResult::Failure();
          (*Record)->Guidance.BusinessOverride = Candidate;
        }
        State->GraphOutput.Movement =
          FCrowdMassMovementPipelineWork::Run(MovementInput);
        return State->GraphOutput.Movement.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.Movement.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Constraint, ConstraintDeps,
      [State]
      {
        const auto& Predicted =
          State->GraphOutput.Movement.MovementPredict.Results;
        if (Predicted.Num()
          != State->BusinessInput.Snapshot.Agents.Num())
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, const FCrowdMassPredictedMovement*> ById;
        for (const FCrowdMassPredictedMovement& Value : Predicted)
        {
          if (!Value.bValid || ById.Contains(Value.AgentId))
            return FCrowdBoundaryTaskResult::Failure();
          ById.Add(Value.AgentId, &Value);
        }
        uint64 Hash = 14695981039346656037ull;
        State->ObstacleKinematics.Reset();
        for (const FCrowdMassBoundaryAgentRecord& Agent
          : State->BusinessInput.Snapshot.Agents)
        {
          const FCrowdMassPredictedMovement* const* Value =
            ById.Find(Agent.Identity.AgentId);
          if (!Value) return FCrowdBoundaryTaskResult::Failure();
          const FCrowdDemoSharedFlowConstraintResult Result =
            FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
              State->ObstacleConfig, (*Value)->StartPosition,
              (*Value)->PredictedPosition,
              State->ObstacleFixedStepSeconds, false);
          FCrowdMassFinalKinematicState& Kinematic =
            State->ObstacleKinematics.AddDefaulted_GetRef();
          Kinematic.AgentId = Agent.Identity.AgentId;
          Kinematic.Position = Result.Location;
          Kinematic.Velocity = Result.Velocity;
          Kinematic.bValid = true;
          State->ObstacleMaxReprojectDeltaCm = FMath::Max(
            State->ObstacleMaxReprojectDeltaCm,
            Result.FlowBoundsReprojectDeltaCm);
          Hash = FoldBoundaryHash(
            Hash, static_cast<uint32>(Kinematic.AgentId));
          Hash = FoldBoundaryHash(
            Hash, GetTypeHash(Kinematic.Position));
        }
        State->ObstacleKinematics.Sort([](const auto& A,
          const auto& B)
        {
          return A.AgentId < B.AgentId;
        });
        return State->ObstacleKinematics.Num() == Predicted.Num()
          ? FCrowdBoundaryTaskResult::Success(Hash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Facing, FacingDeps,
      [State]
      {
        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdMassBoundaryWorkGraph::
          BuildFacingInputFromKinematics(
            State->GraphInput, State->BusinessInput.Snapshot,
            State->GraphOutput.Movement,
            State->ObstacleKinematics, FacingInput))
          return FCrowdBoundaryTaskResult::Failure();
        State->Output =
          FCrowdMassFacingFinalizeWork::Run(FacingInput);
        State->GraphOutput.FacingFinalize = State->Output;
        State->bCompleted = State->Output.bCompleted;
        const uint64 Hash = FoldBoundaryHash(
          State->Output.Finalize.CommitPlan.StableHash,
          State->Output.Facing.StableHash);
        return State->bCompleted
          ? FCrowdBoundaryTaskResult::Success(Hash)
          : FCrowdBoundaryTaskResult::Failure();
      });
  if (!bRegistered || !BoundaryOrchestrator->Dispatch())
  {
    BoundaryOrchestrator->Fail();
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return false;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::WaitBoundaryWork()
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || !BoundaryFacingWorkState.IsValid())
    return false;
  if (!BoundaryOrchestrator->WaitAndDrain())
  {
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryWorkWaitFailed step=%d state=%d tasks=%d"),
      GetCurrentFixedStepIndex(),
      static_cast<int32>(LastBoundaryTransactionResult.State),
      LastBoundaryTransactionResult.Tasks.Num());
    for (const FCrowdBoundaryCompletedTask& Task
      : LastBoundaryTransactionResult.Tasks)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryTaskFailed step=%d stage=%d cohort=%u succeeded=%d off_gt=%d hash=%llu"),
        GetCurrentFixedStepIndex(), static_cast<int32>(Task.Key.Stage),
        Task.Key.CohortKey, Task.Result.bSucceeded ? 1 : 0,
        Task.Result.bRanOffGameThread ? 1 : 0,
        static_cast<unsigned long long>(Task.Result.StableHash));
    }
    return false;
  }
  if (!BoundaryFacingWorkState->bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingWorkIncomplete step=%d facing=%d finalize=%d"),
      GetCurrentFixedStepIndex(),
      BoundaryFacingWorkState->Output.Facing.bCompleted ? 1 : 0,
      BoundaryFacingWorkState->Output.Finalize.bCompleted ? 1 : 0);
    return false;
  }
  if (!BoundaryFacingWorkState->BusinessOutput.bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBusinessWorkIncomplete step=%d"),
      GetCurrentFixedStepIndex());
    return false;
  }
  PreparedBusinessGuidanceCandidates =
    BoundaryFacingWorkState->BusinessOutput.GuidanceCandidates;
  PreparedReactiveMotionSteps =
    BoundaryFacingWorkState->BusinessOutput.ReactiveSteps;
  PreparedRuntimeSharedFlowOutputs =
    BoundaryFacingWorkState->GraphOutput.SharedFlow.Agents;
  PreparedTargetRegionGuidanceCandidates.Reset();
  for (const auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (!Slot.Output.bValid || !Slot.DemandOutput.bValid
      || !Slot.PlanOutput.bValid || !Slot.GuidanceOutput.bValid)
      return false;
    if (ActivePlan.Rules.bEnableHeterogeneousProfiles != 0)
    {
      const FCrowdDemoTargetRegionCapabilityCohortRuntime* Runtime =
        TargetRegionCapabilityCohorts.FindByPredicate(
          [&Slot](const auto& Value)
          {
            return Value.Cohort.CapabilityProfileKey
              == Slot.CohortKey;
          });
      if (!Runtime) return false;
    }
    for (const auto& Result : Slot.GuidanceOutput.Results)
    {
      const FVector DesiredVelocity(
        Result.DesiredVelocity.X, Result.DesiredVelocity.Y, 0.0f);
      const FCrowdMassBoundaryAgentRecord* Base =
        BoundarySnapshot.Agents.FindByPredicate(
          [&Result](const auto& Agent)
          {
            return Agent.Identity.AgentId == Result.AgentId;
          });
      if (!Base) return false;
      PreparedTargetRegionGuidanceCandidates.Add(
        FCrowdGuidanceComposeKernel::BuildCandidate(
          Result.AgentId, ECrowdGuidanceProvider::TargetRegion,
          GetCurrentPlanRevision(), DesiredVelocity,
          FVector(
            Slot.GuidanceInput.Settings.TargetLocation.X,
            Slot.GuidanceInput.Settings.TargetLocation.Y,
            Base->State.Position.Z),
          DesiredVelocity.IsNearlyZero()
            ? Base->State.YawDegrees
            : DesiredVelocity.Rotation().Yaw,
          Result.Mode != ECrowdTargetRegionGuidanceMode::Unrouted));
    }
  }
  PreparedTargetRegionGuidanceCandidates.Sort(
    [](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
  PreparedTargetResourceSlots =
    BoundaryFacingWorkState->TargetTopologySlots;
  if (BoundaryFacingWorkState->bObstacleStaged)
  {
    PreparedObstacleMaxReprojectDeltaCm =
      BoundaryFacingWorkState->ObstacleMaxReprojectDeltaCm;
    PreparedRuntimeComposedGuidance =
      BoundaryFacingWorkState->GraphOutput.Movement.Guidance.
        ComposedGuidance;
    PreparedRuntimePredictedMovements =
      BoundaryFacingWorkState->GraphOutput.Movement.MovementPredict.
        Results;
    PreparedRuntimeFinalKinematics =
      BoundaryFacingWorkState->ObstacleKinematics;
  }
  if (BoundaryFacingWorkState->BusinessOutput.bRequiresCommit
    && !SetPreparedCombatBoundaryCommit(
      MoveTemp(BoundaryFacingWorkState->BusinessOutput.Commit)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBusinessPreparedCommitRejected step=%d"),
      GetCurrentFixedStepIndex());
    return false;
  }
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::
  ApplyPreparedBoundaryResourcePatches()
{
  check(IsInGameThread());
  for (const auto& Slot : PreparedTargetResourceSlots)
  {
    auto PublishTargetOutputs = [&Slot](auto& Runtime)
    {
      Runtime.Topology =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionTopology(Slot.Output.Topology);
      Runtime.TopologySummary =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionTopologySummary(Slot.Output.Summary);
      Runtime.Demand =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionDemand(Slot.DemandOutput.Demand);
      Runtime.Plan =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionPlan(Slot.PlanOutput.Plan);
      Runtime.QuotaExecution =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionExecution(Slot.PlanOutput.Execution);
      Runtime.LastPlanReplacement =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionReplacement(
            Slot.PlanOutput.Replacement);
      Runtime.Validation =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionValidation(
            Slot.PlanOutput.Validation);
      Runtime.Guidance.Reset(Slot.GuidanceOutput.Results.Num());
      for (const auto& Result : Slot.GuidanceOutput.Results)
      {
        Runtime.Guidance.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::
            BuildDemoTargetRegionGuidance(Result));
      }
      Runtime.GuidanceSummary =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionGuidanceSummary(
            Slot.GuidanceOutput.Summary);
    };
    if (ActivePlan.Rules.bEnableHeterogeneousProfiles != 0)
    {
      FCrowdDemoTargetRegionCapabilityCohortRuntime* Runtime =
        TargetRegionCapabilityCohorts.FindByPredicate(
          [&Slot](const auto& Value)
          {
            return Value.Cohort.CapabilityProfileKey
              == Slot.CohortKey;
          });
      checkf(Runtime,
        TEXT("Prepared target resource cohort disappeared after validation"));
      PublishTargetOutputs(*Runtime);
    }
    else
    {
      PreparedTargetRegionTopology =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionTopology(Slot.Output.Topology);
      TargetRegionTopologySummary =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionTopologySummary(Slot.Output.Summary);
      PreparedTargetRegionDemand =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionDemand(Slot.DemandOutput.Demand);
      PreparedTargetRegionPlan =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionPlan(Slot.PlanOutput.Plan);
      TargetRegionQuotaExecution =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionExecution(Slot.PlanOutput.Execution);
      TargetRegionPlanValidation =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionValidation(
            Slot.PlanOutput.Validation);
      PreparedTargetRegionGuidance.Reset(
        Slot.GuidanceOutput.Results.Num());
      for (const auto& Result : Slot.GuidanceOutput.Results)
      {
        PreparedTargetRegionGuidance.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::
            BuildDemoTargetRegionGuidance(Result));
      }
      TargetRegionGuidanceSummary =
        FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionGuidanceSummary(
            Slot.GuidanceOutput.Summary);
    }
  }
  PreparedTargetResourceSlots.Reset();
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryFacingWork(
  FCrowdMassFacingFinalizeWorkOutput& OutOutput,
  TMap<int32, int32>& OutConsecutiveSettleStepsByAgentId,
  TMap<int32, bool>& OutFinalSettledByAgentId)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;
  OutOutput = MoveTemp(BoundaryFacingWorkState->Output);
  OutConsecutiveSettleStepsByAgentId = MoveTemp(
    BoundaryFacingWorkState->ConsecutiveSettleStepsByAgentId);
  OutFinalSettledByAgentId = MoveTemp(
    BoundaryFacingWorkState->FinalSettledByAgentId);
  BoundaryFacingWorkState.Reset();
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedMovementCommitPlan(
  const FCrowdMassCommitPlan& CommitPlan)
{
  if (!IsBoundarySnapshotCurrent()
    || PreparedMovementCommitPlan.bValid
    || !CommitPlan.bValid
    || CommitPlan.FixedStepIndex != GetCurrentFixedStepIndex()
    || CommitPlan.PlanRevision != GetCurrentPlanRevision()
    || CommitPlan.Records.Num() != BoundarySnapshot.Agents.Num())
    return false;
  PreparedMovementCommitPlan = CommitPlan;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedMovementBoundaryCommit(
  FCrowdDemoPreparedMovementBoundaryCommit&& Commit)
{
  if (PreparedMovementBoundaryCommit.bValid
    || PreparedMovementCommitPlan.bValid
    || !Commit.bValid
    || Commit.StableHash == 0
    || Commit.FixedStepIndex != GetCurrentFixedStepIndex()
    || Commit.PlanRevision != GetCurrentPlanRevision()
    || !Commit.Facing.bCompleted
    || !Commit.Finalize.bCompleted
    || !Commit.Finalize.CommitPlan.bValid
    || Commit.Finalize.CommitPlan.FixedStepIndex
      != GetCurrentFixedStepIndex()
    || Commit.Finalize.CommitPlan.PlanRevision
      != GetCurrentPlanRevision()
    || Commit.Finalize.CommitPlan.Records.Num()
      != BoundarySnapshot.Agents.Num()
    || Commit.ConsecutiveSettleStepsByAgentId.Num()
      != BoundarySnapshot.Agents.Num()
    || Commit.FinalSettledByAgentId.Num()
      != BoundarySnapshot.Agents.Num())
    return false;
  PreparedMovementCommitPlan = Commit.Finalize.CommitPlan;
  PreparedMovementBoundaryCommit = MoveTemp(Commit);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PrepareBoundaryCommit(
  const TConstArrayView<FCrowdMassCommitTarget> ResolvedTargets)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || !PreparedMovementCommitPlan.bValid
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;

  TArray<FCrowdBoundaryPreparedPatch> Patches;
  const auto AddPatch = [this, &Patches](
    const FName AdapterId, const uint64 StableHash)
  {
    if (StableHash == 0) return false;
    FCrowdBoundaryPreparedPatch& Patch =
      Patches.AddDefaulted_GetRef();
    Patch.AdapterId = AdapterId;
    Patch.FixedStepIndex = GetCurrentFixedStepIndex();
    Patch.PlanRevision = GetCurrentPlanRevision();
    for (const FCrowdMassBoundaryAgentRecord& Agent
      : BoundarySnapshot.Agents)
      Patch.EntityRefs.Add(Agent.AgentFacts.StableEntityRef);
    Patch.StableHash = StableHash;
    Patch.Payload =
      MakeShared<FCrowdDemoBoundaryHashPayload, ESPMode::ThreadSafe>();
    Patch.bValid = true;
    return true;
  };
  if (!IsPreparedMovementBoundaryCommitCurrent()
    || !AddPatch(TEXT("FacingVisual"),
      PreparedMovementBoundaryCommit.StableHash))
    return false;
  if (IsPreparedCombatBoundaryCommitCurrent()
    && !AddPatch(TEXT("Combat"),
      PreparedCombatBoundaryCommit.StableHash))
    return false;
  if (IsPreparedCombatBoundaryCommitCurrent()
    && (!AddPatch(TEXT("Projectile"),
          PreparedCombatBoundaryCommit.StableHash)
      || !AddPatch(TEXT("EventsMetrics"),
          PreparedCombatBoundaryCommit.StableHash)))
    return false;
  uint64 FlowDiagnosticHash = 14695981039346656037ull;
  for (const FCrowdMassSharedFlowAgentOutput& Output
    : PreparedRuntimeSharedFlowOutputs)
  {
    FlowDiagnosticHash = FoldBoundaryHash(
      FlowDiagnosticHash, static_cast<uint64>(Output.AgentId));
    FlowDiagnosticHash = FoldBoundaryHash(
      FlowDiagnosticHash,
      static_cast<uint64>(Output.Sample.StableCellKey));
    FlowDiagnosticHash = FoldBoundaryHash(
      FlowDiagnosticHash,
      static_cast<uint64>(Output.Candidate.StableHash));
  }
  if (!AddPatch(TEXT("FlowDiagnostic"), FlowDiagnosticHash))
    return false;
  if (!PreparedTargetResourceSlots.IsEmpty())
  {
    uint64 TargetResourceHash = 14695981039346656037ull;
    for (const auto& Slot : PreparedTargetResourceSlots)
    {
      TargetResourceHash = FoldBoundaryHash(
        TargetResourceHash, Slot.CohortKey);
      TargetResourceHash = FoldBoundaryHash(
        TargetResourceHash, Slot.Output.Topology.TopologyHash);
      TargetResourceHash = FoldBoundaryHash(
        TargetResourceHash, Slot.DemandOutput.Demand.DemandHash);
      TargetResourceHash = FoldBoundaryHash(
        TargetResourceHash, Slot.PlanOutput.Plan.TransportHash);
      TargetResourceHash = FoldBoundaryHash(
        TargetResourceHash,
        Slot.GuidanceOutput.Summary.GuidanceHash);
    }
    if (!AddPatch(TEXT("TargetResource"), TargetResourceHash))
      return false;
  }
  if (IsPreparedParticleDiagnosticCommitCurrent())
  {
    uint64 ParticleHash = FoldBoundaryHash(
      14695981039346656037ull,
      PreparedParticleDiagnosticCommit.AppliedStateHash);
    ParticleHash = FoldBoundaryHash(
      ParticleHash,
      PreparedParticleDiagnosticCommit.CandidateSummary.CandidateHash);
    if (!AddPatch(TEXT("ParticleDiagnostic"), ParticleHash))
      return false;
  }
  const double MergeStartSeconds = FPlatformTime::Seconds();
  const double MergeMilliseconds =
    (FPlatformTime::Seconds() - MergeStartSeconds) * 1000.0;
  if (!BoundaryOrchestrator->BuildAndSealCommit(
      PreparedMovementCommitPlan, Patches, ResolvedTargets,
      MergeMilliseconds))
  {
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return false;
  }
  PreparedBoundaryCommitEnvelope =
    BoundaryOrchestrator->GetCommitEnvelope();
  const double ValidateStartSeconds = FPlatformTime::Seconds();
  const bool bValidated = BoundaryOrchestrator->MarkValidated(
    (FPlatformTime::Seconds() - ValidateStartSeconds) * 1000.0);
  if (!bValidated)
  {
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return false;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::MarkBoundaryCommitted(
  const double CommitMilliseconds)
{
  if (!BoundaryOrchestrator.IsValid()
    || !BoundaryOrchestrator->MarkCommitted(CommitMilliseconds))
    return false;
  LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
  if (LastBoundaryTransactionResult.bSucceeded
    && PreparedObstacleMaxReprojectDeltaCm >= 0.0f)
  {
    RecordNavigationDomainReprojectDelta(
      PreparedObstacleMaxReprojectDeltaCm);
  }
  if (LastBoundaryTransactionResult.bSucceeded
    && (GetCurrentFixedStepIndex() == 0
      || GetCurrentFixedStepIndex() % 300 == 0))
  {
    const FCrowdBoundaryPhaseTimings& Timings =
      LastBoundaryTransactionResult.Timings;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoBoundaryTransaction step=%d plan_revision=%d snapshot_hash=%llu commit_hash=%llu gather_ms=%.3f queue_ms=%.3f work_ms=%.3f wait_ms=%.3f merge_ms=%.3f validate_ms=%.3f commit_ms=%.3f tasks=%d source=MassPipeline"),
      GetCurrentFixedStepIndex(), GetCurrentPlanRevision(),
      static_cast<unsigned long long>(
        LastBoundaryTransactionResult.SnapshotHash),
      static_cast<unsigned long long>(
        LastBoundaryTransactionResult.CommitPlanHash),
      Timings.GatherMilliseconds, Timings.QueueMilliseconds,
      Timings.WorkMilliseconds, Timings.WaitMilliseconds,
      Timings.MergeMilliseconds, Timings.ValidateMilliseconds,
      Timings.CommitMilliseconds,
      LastBoundaryTransactionResult.Tasks.Num());
  }
  return LastBoundaryTransactionResult.bSucceeded;
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedCombatBoundaryCommit(
  FCrowdDemoPreparedCombatBoundaryCommit&& Commit)
{
  if (PreparedCombatBoundaryCommit.bValid
    || !Commit.bValid
    || Commit.StableHash == 0
    || Commit.FixedStepIndex != GetCurrentFixedStepIndex()
    || Commit.PlanRevision != GetCurrentPlanRevision()
    || Commit.Agents.Num() != BoundarySnapshot.Agents.Num())
    return false;
  Commit.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Commit.Agents.Num(); ++Index)
  {
    if (Commit.Agents[Index].AgentId
        != BoundarySnapshot.Agents[Index].Identity.AgentId
      || static_cast<uint32>(Commit.Agents[Index].LifecycleSerial)
        != BoundarySnapshot.Agents[Index].AgentFacts.StableEntityRef.LifecycleSerial
      || (Index > 0
        && Commit.Agents[Index - 1].AgentId
          >= Commit.Agents[Index].AgentId))
      return false;
  }
  PreparedCombatBoundaryCommit = MoveTemp(Commit);
  return true;
}

const FCrowdDemoRoundBoundaryFormationFact*
UCrowdDemoRoundSimPipelineSubsystem::FindBoundaryFormationFact(
  const int32 AgentId) const
{
  return BoundaryFormationFacts.FindByPredicate(
    [AgentId](const FCrowdDemoRoundBoundaryFormationFact& Value)
    {
      return Value.AgentId == AgentId;
    });
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedPostFinalizeAgentRecords(
  TArray<FCrowdDemoPreparedPostFinalizeAgentRecord>&& Values)
{
  if (Values.Num() != BoundarySnapshot.Agents.Num()
    || !PreparedPostFinalizeAgentRecords.IsEmpty())
    return false;
  Values.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Values.Num(); ++Index)
  {
    const FCrowdDemoPreparedPostFinalizeAgentRecord& Value = Values[Index];
    const FCrowdMassBoundaryAgentRecord& Boundary =
      BoundarySnapshot.Agents[Index];
    if (Value.AgentId != Boundary.Identity.AgentId
      || Value.LifecycleSerial != static_cast<int32>(
        Boundary.Identity.LifecycleSerial)
      || !Value.State.bInitialized
      || Value.State.PlanRevision != GetCurrentPlanRevision())
      return false;
  }
  PreparedPostFinalizeAgentRecords = MoveTemp(Values);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedCheckpointAgentStates(
  TArray<FCrowdDemoRoundAgentState>&& Values)
{
  if (Values.Num() != PreparedPostFinalizeAgentRecords.Num()
    || Values.Num() != BoundaryFormationFacts.Num()
    || !PreparedCheckpointAgentStates.IsEmpty())
    return false;
  Values.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Values.Num(); ++Index)
  {
    const FCrowdDemoRoundAgentState& Value = Values[Index];
    const FCrowdDemoPreparedPostFinalizeAgentRecord& Final =
      PreparedPostFinalizeAgentRecords[Index];
    const FCrowdDemoRoundBoundaryFormationFact& Formation =
      BoundaryFormationFacts[Index];
    if (Value.AgentId != Final.AgentId
      || Value.AgentId != Formation.AgentId
      || Value.LifecycleSerial != Final.LifecycleSerial
      || !FVector(Value.Location).Equals(Final.State.Location, 0.051f)
      || !FVector(Value.Velocity).Equals(Final.State.Velocity, 0.051f)
      || FMath::Abs(FMath::FindDeltaAngleDegrees(
        Value.YawDegrees, Final.State.YawDegrees)) > 0.01f
      || !FMath::IsNearlyEqual(
        Value.RadiusCm, Formation.RadiusCm, 0.01f))
      return false;
  }
  PreparedCheckpointAgentStates = MoveTemp(Values);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::EnsureDynamicSharedFlowField(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const FVector& TargetLocation)
{
  FCrowdMassSharedFlowBuildInput Input;
  Input.Config = FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(Config);
  Input.TargetLocation = TargetLocation;
  Input.bDynamicTarget = true;
  Input.bForceIntegrationRefresh = bDynamicFlowIntegrationCacheInvalidated;
  const FCrowdMassSharedFlowBuildOutput Output =
    FCrowdMassSharedFlowWork::EnsureResource(
      Input, RuntimeSharedFlowResource);
  if (!Output.bValid) return false;
  DynamicFlowAnchorCellKey = Output.DynamicAnchorCellKey;
  SharedFlowFieldRebuildCount = RuntimeSharedFlowResource.FieldRebuildCount;
  DynamicFlowIntegrationRebuildCount =
    RuntimeSharedFlowResource.IntegrationRebuildCount;
  bDynamicFlowIntegrationCacheInvalidated = false;
  if (Output.bFieldRebuilt || Output.bIntegrationRebuilt
    || !SharedFlowField.IsValid()
    || SharedFlowField.BuildHash != RuntimeSharedFlowResource.Field.BuildHash)
  {
    SharedFlowField = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(
      RuntimeSharedFlowResource.Field);
  }

  auto Fold = [](uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= 16777619u;
    return Hash;
  };
  DynamicFlowRoundHash = Fold(
    DynamicFlowRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex()));
  DynamicFlowRoundHash = Fold(DynamicFlowRoundHash, static_cast<uint32>(DynamicFlowAnchorCellKey));
  DynamicFlowRoundHash = Fold(DynamicFlowRoundHash, SharedFlowField.TopologyHash);
  DynamicFlowRoundHash = Fold(DynamicFlowRoundHash, SharedFlowField.IntegrationHash);
  LastCompareMetrics.FlowFieldRevision = SharedFlowField.Config.Revision;
  LastCompareMetrics.FlowFieldBuildHash = SharedFlowField.BuildHash;
  LastCompareMetrics.FlowFieldRebuildCount = SharedFlowFieldRebuildCount;
  LastCompareMetrics.FlowBlockedCellCount = SharedFlowField.BlockedCellCount;
  return SharedFlowField.IsValid();
}


bool UCrowdDemoRoundSimPipelineSubsystem::EnsureBidirectionalSwapFlowFields()
{
  if (!IsBidirectionalSwap()) return false;
  bool bAllValid = true;
  for (int32 CohortId = 0; CohortId < 2; ++CohortId)
  {
    const FCrowdDemoSharedFlowFieldConfig Config =
      FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortId);
    FCrowdMassSharedFlowBuildInput Input;
    Input.Config = FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(Config);
    FCrowdMassSharedFlowResource& Resource =
      RuntimeBidirectionalSwapFlowResources[CohortId];
    const FCrowdMassSharedFlowBuildOutput Output =
      FCrowdMassSharedFlowWork::EnsureResource(Input, Resource);
    bAllValid &= Output.bValid;
    FCrowdDemoSharedFlowField& Field = BidirectionalSwapFlowFields[CohortId];
    if (Output.bValid && (Output.bFieldRebuilt || !Field.IsValid()
      || Field.BuildHash != Resource.Field.BuildHash))
      Field = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(Resource.Field);
  }
  return bAllValid;
}

const FCrowdSharedFlowField*
UCrowdDemoRoundSimPipelineSubsystem::FindRuntimeBidirectionalSwapFlowField(
  const int32 FormationIndex) const
{
  const int32 CohortId =
    FCrowdDemoBidirectionalSwapKernel::CohortIdForFormationIndex(FormationIndex);
  return CohortId >= 0 && CohortId < RuntimeBidirectionalSwapFlowResources.Num()
    ? &RuntimeBidirectionalSwapFlowResources[CohortId].Field : nullptr;
}

const FCrowdDemoSharedFlowField*
UCrowdDemoRoundSimPipelineSubsystem::FindBidirectionalSwapFlowField(
  const int32 FormationIndex) const
{
  const int32 CohortId =
    FCrowdDemoBidirectionalSwapKernel::CohortIdForFormationIndex(FormationIndex);
  return CohortId >= 0 && CohortId < BidirectionalSwapFlowFields.Num()
    ? &BidirectionalSwapFlowFields[CohortId] : nullptr;
}


void UCrowdDemoRoundSimPipelineSubsystem::RecordFlowConnectivityStep(
  const int32 RecoveredCount,
  const int32 DesiredSegmentViolationCount,
  const int32 SourceAttachmentSuccessCount,
  const int32 UnreachableSampleCount)
{
  FCrowdDemoSharedFlowMetrics& Metrics = LastCompareMetrics.SharedFlowMetrics;
  Metrics.FlowRecoveredFromRasterMismatchCount += FMath::Max(0, RecoveredCount);
  Metrics.FlowDesiredSegmentHardObstacleViolationCount +=
    FMath::Max(0, DesiredSegmentViolationCount);
  Metrics.SourceAttachmentSuccessCount += FMath::Max(0, SourceAttachmentSuccessCount);
  Metrics.NavigationUnreachableSampleCount += FMath::Max(0, UnreachableSampleCount);
}

FCrowdDemoSharedFlowMetrics UCrowdDemoRoundSimPipelineSubsystem::BuildSharedFlowMetrics(
  const TConstArrayView<FCrowdDemoRoundAgentState> States) const
{
  FCrowdDemoSharedFlowMetrics Metrics = LastCompareMetrics.SharedFlowMetrics;
  Metrics.SharedFlowFieldBuildHash = SharedFlowField.BuildHash;
  Metrics.SharedFlowConnectivityContractVersion =
    SharedFlowField.Config.ConnectivityContractVersion;
  Metrics.SharedFlowValidDirectedEdgeCount = SharedFlowField.ValidDirectedEdgeCount;
  Metrics.NavigationHardClearanceCm = GetRules().GetParticleEnvironmentHardClearanceCm();
  Metrics.NavigationCenterAnchorCount = SharedFlowField.NavigationCenterAnchorCount;
  Metrics.NavigationConnectionPointCount = SharedFlowField.NavigationConnectionPointCount;
  Metrics.NavigationSafeIntervalCount = SharedFlowField.NavigationSafeIntervalCount;
  Metrics.NavigationInternalEdgeCount = SharedFlowField.NavigationInternalEdgeCount;
  Metrics.NavigationDirectedEdgeCount = SharedFlowField.ValidDirectedEdgeCount;
  Metrics.CenterInvalidButConnectedCellCount =
    SharedFlowField.CenterInvalidButConnectedCellCount;
  Metrics.GoalAttachmentCount = SharedFlowField.GoalAttachmentCount;
  Metrics.NavigationV2Hash = SharedFlowField.Config.ConnectivityContractVersion >= 2
    ? SharedFlowField.BuildHash : 0;
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoRoundAgentState& State : States)
  {
    Hash = FoldHash(Hash, static_cast<uint32>(State.AgentId));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Location.X)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Location.Y)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Velocity.X)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Velocity.Y)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.YawDegrees)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.RadiusCm)));
  }
  Metrics.AgentStateHash = Hash;
  return Metrics;
}

int32 UCrowdDemoRoundSimPipelineSubsystem::GetCurrentFixedStepIndex() const
{
  if (!IsActive() || CurrentFixedStepSeconds <= 0.0f)
  {
    return INDEX_NONE;
  }
  return FMath::Max(0, FMath::RoundToInt(
    ((bStepInProgress ? CurrentStepStartServerTimeSeconds : SimulatedServerTimeSeconds)
      - ActivePlan.StartServerTimeSeconds)
    / CurrentFixedStepSeconds));
}
void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackSnapshot(
  const int32 FixedStepIndex,
  TArray<FCrowdDemoSoftPressureRollbackAgentState>&& Agents)
{
  if (!IsActive() || !IsFlowScenario(GetRules().Scenario))
    return;
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  FCrowdDemoSoftPressureRollbackSnapshot& Snapshot =
    SoftPressureRollbackHistory.FindOrAdd(FixedStepIndex);
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.bMovementFactsComplete = false;
  Snapshot.bCombatFactsComplete = false;
  Snapshot.bSnapshotReadyForReplay = false;
  Snapshot.Agents = MoveTemp(Agents);
  bool bAgentSetValid = Snapshot.Agents.Num() == BoundarySnapshot.Agents.Num();
  if (!bAgentSetValid)
  {
    Snapshot.bMovementFactsComplete = false;
    return;
  }
  for (int32 Index = 0; Index < Snapshot.Agents.Num(); ++Index)
  {
    bAgentSetValid &= Snapshot.Agents[Index].AgentId != INDEX_NONE
      && Snapshot.Agents[Index].AgentId
        == BoundarySnapshot.Agents[Index].Identity.AgentId
      && (Index == 0
        || Snapshot.Agents[Index - 1].AgentId < Snapshot.Agents[Index].AgentId);
  }
  Snapshot.bMovementFactsComplete = bAgentSetValid;
  Snapshot.LocalPredictiveResults = PreparedLocalPredictiveResults;
  Snapshot.LocalPredictiveGrantStates = LocalPredictiveGrantStates;
  Snapshot.LocalPredictiveSummary = LastLocalPredictiveSummary;
  Snapshot.LocalPredictiveRoundHash = LocalPredictiveRoundHash;
  Snapshot.LocalPredictiveSampleCount = LocalPredictiveSampleCount;
  Snapshot.LocalPredictiveInvalidStepCount = LocalPredictiveInvalidStepCount;
  Snapshot.GuidanceCandidateRoundHash = GuidanceCandidateRoundHash;
  Snapshot.GuidanceComposeRoundHash = GuidanceComposeRoundHash;
  Snapshot.GuidanceComposeSampleCount = GuidanceComposeSampleCount;
  Snapshot.ParticleCandidateSummary = LastParticleCandidateSummary;
  Snapshot.ParticleAppliedSummary = LastParticleAppliedSummary;
  Snapshot.ParticleSolverMsSampleCount = ParticleSolverMillisecondsSamples.Num();
  Snapshot.ParticleCandidateStateHash = ParticleCandidateStateHash;
  Snapshot.ParticleAppliedStateHash = ParticleAppliedStateHash;
  Snapshot.ParticleInvalidStepCount = ParticleInvalidStepCount;
  Snapshot.ParticleGlobalFallbackStepCount = ParticleGlobalFallbackStepCount;
  Snapshot.ParticleStepCount = ParticleStepCount;
  Snapshot.CrossProfileHardViolationCount = CrossProfileHardViolationCount;
  Snapshot.CrossProfileSweptViolationCount = CrossProfileSweptViolationCount;
  Snapshot.ParticleSettlingWindowCount = ParticleSettlingWindowCount;
  Snapshot.ParticleSettlingSteps = ParticleSettlingSteps;
  Snapshot.ParticlePreviousSoftErrorP95 = ParticlePreviousSoftErrorP95;
  Snapshot.bParticleConstraintRunFailure = bParticleConstraintRunFailure;
  Snapshot.ParticleFailureFixture = ParticleFailureFixture;
  Snapshot.OpenSpawnRelaxationRuntime = OpenSpawnRelaxationRuntime;
  Snapshot.OpenCohortMovementProgress = OpenCohortMovementProgress;
  Snapshot.BidirectionalSwapProgress = BidirectionalSwapProgress;
  Snapshot.ValidCorridorTransitProgress = ValidCorridorTransitProgress;
  Snapshot.TargetFact = TargetFact;
  Snapshot.DynamicFlowAnchorCellKey = DynamicFlowAnchorCellKey;
  Snapshot.DynamicFlowIntegrationRebuildCount = DynamicFlowIntegrationRebuildCount;
  Snapshot.DynamicFlowRoundHash = DynamicFlowRoundHash;
  Snapshot.TargetRegionPlanResourceKey = PreparedTargetRegionPlan.bValid
    ? MakeTargetPlanResourceKey(0, PreparedTargetRegionPlan)
    : 0;
  if (PreparedTargetRegionPlan.bValid)
    TargetRegionPlanResources.FindOrAdd(Snapshot.TargetRegionPlanResourceKey) =
      PreparedTargetRegionPlan;
  Snapshot.TargetRegionQuotaExecution = TargetRegionQuotaExecution;
  Snapshot.TargetRegionPlanValidation = TargetRegionPlanValidation;
  Snapshot.TargetRegionTopologyRoundHash = TargetRegionTopologyRoundHash;
  Snapshot.TargetRegionDemandRoundHash = TargetRegionDemandRoundHash;
  Snapshot.TargetRegionTransportRoundHash = TargetRegionTransportRoundHash;
  Snapshot.TargetRegionGuidanceRoundHash = TargetRegionGuidanceRoundHash;
  Snapshot.TargetRegionPlanRebuildCount = TargetRegionPlanRebuildCount;
  Snapshot.TargetRegionLifetimeRebuildCount = TargetRegionLifetimeRebuildCount;
  Snapshot.TargetRegionTargetRebuildCount = TargetRegionTargetRebuildCount;
  Snapshot.TargetRegionEnvironmentRebuildCount = TargetRegionEnvironmentRebuildCount;
  Snapshot.TargetRegionMembershipRebuildCount = TargetRegionMembershipRebuildCount;
  Snapshot.TargetRegionDemandSatisfiedRebuildCount = TargetRegionDemandSatisfiedRebuildCount;
  Snapshot.TargetRegionPathInvalidRebuildCount = TargetRegionPathInvalidRebuildCount;
  Snapshot.TargetRegionSolverMsSampleCount = TargetRegionSolverMillisecondsSamples.Num();
  Snapshot.bTargetRegionRoundValid = bTargetRegionRoundValid;
  Snapshot.TargetRegionInvalidStepCount = TargetRegionInvalidStepCount;
  Snapshot.TargetRegionLastInvalidStep = TargetRegionLastInvalidStep;
  Snapshot.TargetRegionValidationFailureCount = TargetRegionValidationFailureCount;
  Snapshot.TargetRegionValidationRoundHash = TargetRegionValidationRoundHash;
  Snapshot.TargetRegionGuidanceUnroutedStepCount = TargetRegionGuidanceUnroutedStepCount;
  Snapshot.TargetRegionGuidanceUnroutedAgentSampleCount = TargetRegionGuidanceUnroutedAgentSampleCount;
  Snapshot.TargetRegionGuidanceUnroutedAgentMax = TargetRegionGuidanceUnroutedAgentMax;
  Snapshot.TargetRegionGuidanceFirstFailureStep = TargetRegionGuidanceFirstFailureStep;
  Snapshot.TargetRegionGuidanceFirstFailureAgentId = TargetRegionGuidanceFirstFailureAgentId;
  Snapshot.bTargetRegionFailureFixtureValid = bTargetRegionFailureFixtureValid;
  Snapshot.TargetRegionFailureFixtureStep = TargetRegionFailureFixtureStep;
  Snapshot.TargetRegionFailureFixtureKind = TargetRegionFailureFixtureKind;
  Snapshot.TargetRegionFailureFixtureAgentId = TargetRegionFailureFixtureAgentId;
  Snapshot.TargetRegionFailureFixtureCellKey = TargetRegionFailureFixtureCellKey;
  Snapshot.TargetRegionFailureFixtureHash = TargetRegionFailureFixtureHash;
  Snapshot.TargetRegionCapabilityCohorts.Reset(TargetRegionCapabilityCohorts.Num());
  for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime
    : TargetRegionCapabilityCohorts)
  {
    FCrowdDemoTargetRegionCapabilityCohortRollbackState& State =
      Snapshot.TargetRegionCapabilityCohorts.AddDefaulted_GetRef();
    State.Cohort = Runtime.Cohort;
    State.DemandRegionPhaseOffset = Runtime.DemandRegionPhaseOffset;
    State.PlanResourceKey = Runtime.Plan.bValid
      ? MakeTargetPlanResourceKey(Runtime.Cohort.CapabilityProfileKey, Runtime.Plan)
      : 0;
    if (Runtime.Plan.bValid)
      TargetRegionPlanResources.FindOrAdd(State.PlanResourceKey) = Runtime.Plan;
    State.QuotaExecution = Runtime.QuotaExecution;
    State.LastPlanReplacement = Runtime.LastPlanReplacement;
    State.Validation = Runtime.Validation;
    State.TopologyRoundHash = Runtime.TopologyRoundHash;
    State.DemandRoundHash = Runtime.DemandRoundHash;
    State.TransportRoundHash = Runtime.TransportRoundHash;
    State.GuidanceRoundHash = Runtime.GuidanceRoundHash;
    State.ValidationRoundHash = Runtime.ValidationRoundHash;
    State.PlanRebuildCount = Runtime.PlanRebuildCount;
    State.InvalidStepCount = Runtime.InvalidStepCount;
    State.ValidationFailureCount = Runtime.ValidationFailureCount;
    State.GuidanceUnroutedStepCount = Runtime.GuidanceUnroutedStepCount;
    State.LastInvalidStep = Runtime.LastInvalidStep;
    State.SolverMsSampleCount = Runtime.SolverMillisecondsSamples.Num();
    State.PlanLifecycle = Runtime.PlanLifecycle;
    State.TargetEngagedHoldAgentIds = Runtime.TargetEngagedHoldAgentIds;
    State.TargetEngagementAcquireCount = Runtime.TargetEngagementAcquireCount;
    State.TargetEngagementReleaseCount = Runtime.TargetEngagementReleaseCount;
    State.TargetEngagementSuppressedRetreatCount =
      Runtime.TargetEngagementSuppressedRetreatCount;
    State.bRoundValid = Runtime.bRoundValid;
  }
  Snapshot.CapabilityProfileSummary = CapabilityProfileSummary;
  Snapshot.CapabilityCohortRebuildCount = CapabilityCohortRebuildCount;
  Snapshot.TargetRegionPlanLifecycleSummary = TargetRegionPlanLifecycleSummary;
  Snapshot.TargetRegionPlanLifecycleFixture = TargetRegionPlanLifecycleFixture;
  Snapshot.FlowGoalReachedAgentIds = FlowGoalReachedAgentIds;
  Snapshot.FlowWallPassAgentIds = FlowWallPassAgentIds;
  Snapshot.FlowCorridorExitAgentIds = FlowCorridorExitAgentIds;
  Snapshot.FlowTurnExitAgentIds = FlowTurnExitAgentIds;
  Snapshot.FlowLowSpeedSecondsByAgentId = FlowLowSpeedSecondsByAgentId;
  Snapshot.FlowCorridorDeadlockAgentIds = FlowCorridorDeadlockAgentIds;
  Snapshot.CompareMetrics = LastCompareMetrics;
  Snapshot.Projectiles = PreparedProjectiles;
  Snapshot.ProjectileMetrics = ProjectileMetrics;
  if (IsSoftPressureRouteDiagnosticEnabled())
    Snapshot.RouteDiagnosticCheckpoint =
      FCrowdDemoSoftPressureRouteDiagnosticKernel::MakeCheckpoint(
        SoftPressureRouteDiagnosticRuntime);
  if (IsTargetStabilityDiagnosticEnabled())
    Snapshot.TargetStabilityCheckpoint =
      FCrowdDemoTargetStabilityDiagnosticKernel::MakeCheckpoint(
        TargetStabilityRuntime);
  SoftPressureRollbackHistory.Remove(FixedStepIndex - 128);
}

bool UCrowdDemoRoundSimPipelineSubsystem::CompleteSoftPressureRollbackCombatState(
  const int32 FixedStepIndex,
  const TConstArrayView<FCrowdDemoPreparedCombatRollbackFact> CombatStates)
{
  FCrowdDemoSoftPressureRollbackSnapshot* Snapshot =
    SoftPressureRollbackHistory.Find(FixedStepIndex);
  if (!Snapshot || !Snapshot->bMovementFactsComplete
    || Snapshot->bCombatFactsComplete || Snapshot->bSnapshotReadyForReplay
    || Snapshot->Agents.Num() != CombatStates.Num())
    return false;

  TArray<FCrowdDemoPreparedCombatRollbackFact> SortedStates(CombatStates);
  SortedStates.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < SortedStates.Num(); ++Index)
  {
    if (SortedStates[Index].AgentId == INDEX_NONE
      || Snapshot->Agents[Index].AgentId != SortedStates[Index].AgentId
      || (Index > 0 && SortedStates[Index - 1].AgentId == SortedStates[Index].AgentId))
      return false;
  }
  for (int32 Index = 0; Index < SortedStates.Num(); ++Index)
    Snapshot->Agents[Index].Combat = SortedStates[Index].Combat;
  Snapshot->bCombatFactsComplete = true;
  Snapshot->bSnapshotReadyForReplay = true;
  return true;
}

const FCrowdDemoSoftPressureRollbackSnapshot*
UCrowdDemoRoundSimPipelineSubsystem::FindSoftPressureRollbackSnapshot(
  const int32 FixedStepIndex) const
{
  return SoftPressureRollbackHistory.Find(FixedStepIndex);
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSoftPressureRollbackSnapshotReadyForReplay(
  const int32 FixedStepIndex) const
{
  const FCrowdDemoSoftPressureRollbackSnapshot* Snapshot =
    SoftPressureRollbackHistory.Find(FixedStepIndex);
  return Snapshot && Snapshot->bMovementFactsComplete
    && Snapshot->bCombatFactsComplete && Snapshot->bSnapshotReadyForReplay;
}

void UCrowdDemoRoundSimPipelineSubsystem::RestoreSoftPressureRuntime(
  const FCrowdDemoSoftPressureRollbackSnapshot& Snapshot)
{
  PreparedLocalPredictiveResults = Snapshot.LocalPredictiveResults;
  LocalPredictiveGrantStates = Snapshot.LocalPredictiveGrantStates;
  LastLocalPredictiveSummary = Snapshot.LocalPredictiveSummary;
  LocalPredictiveRoundHash = Snapshot.LocalPredictiveRoundHash;
  LocalPredictiveSampleCount = Snapshot.LocalPredictiveSampleCount;
  LocalPredictiveInvalidStepCount = Snapshot.LocalPredictiveInvalidStepCount;
  GuidanceCandidateRoundHash = Snapshot.GuidanceCandidateRoundHash;
  GuidanceComposeRoundHash = Snapshot.GuidanceComposeRoundHash;
  GuidanceComposeSampleCount = Snapshot.GuidanceComposeSampleCount;
  LastParticleCandidateSummary = Snapshot.ParticleCandidateSummary;
  LastParticleAppliedSummary = Snapshot.ParticleAppliedSummary;
  ParticleSolverMillisecondsSamples.SetNum(FMath::Min(
    ParticleSolverMillisecondsSamples.Num(), Snapshot.ParticleSolverMsSampleCount));
  ParticleCandidateStateHash = Snapshot.ParticleCandidateStateHash;
  ParticleAppliedStateHash = Snapshot.ParticleAppliedStateHash;
  ParticleInvalidStepCount = Snapshot.ParticleInvalidStepCount;
  ParticleGlobalFallbackStepCount = Snapshot.ParticleGlobalFallbackStepCount;
  ParticleStepCount = Snapshot.ParticleStepCount;
  CrossProfileHardViolationCount = Snapshot.CrossProfileHardViolationCount;
  CrossProfileSweptViolationCount = Snapshot.CrossProfileSweptViolationCount;
  ParticleSettlingWindowCount = Snapshot.ParticleSettlingWindowCount;
  ParticleSettlingSteps = Snapshot.ParticleSettlingSteps;
  ParticlePreviousSoftErrorP95 = Snapshot.ParticlePreviousSoftErrorP95;
  bParticleConstraintRunFailure = Snapshot.bParticleConstraintRunFailure;
  ParticleFailureFixture = Snapshot.ParticleFailureFixture;
  OpenSpawnRelaxationRuntime = Snapshot.OpenSpawnRelaxationRuntime;
  PreparedOpenSpawnBoundaryFacts.Reset();
  PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
  OpenCohortMovementProgress = Snapshot.OpenCohortMovementProgress;
  BidirectionalSwapProgress = Snapshot.BidirectionalSwapProgress;
  ValidCorridorTransitProgress = Snapshot.ValidCorridorTransitProgress;
  TargetFact = Snapshot.TargetFact;
  DynamicFlowAnchorCellKey = Snapshot.DynamicFlowAnchorCellKey;
  RuntimeSharedFlowResource.DynamicAnchorCellKey =
    Snapshot.DynamicFlowAnchorCellKey;
  DynamicFlowIntegrationRebuildCount = Snapshot.DynamicFlowIntegrationRebuildCount;
  RuntimeSharedFlowResource.IntegrationRebuildCount =
    Snapshot.DynamicFlowIntegrationRebuildCount;
  DynamicFlowRoundHash = Snapshot.DynamicFlowRoundHash;
  bDynamicFlowIntegrationCacheInvalidated = true;
  PreparedTargetRegionTopology = {};
  TargetRegionTopologySummary = {};
  PreparedTargetRegionAgents.Reset();
  PreparedTargetRegionDemand = {};
  PreparedTargetRegionPlan = {};
  if (const FCrowdDemoTargetRegionFlowPlan* Resource =
    TargetRegionPlanResources.Find(Snapshot.TargetRegionPlanResourceKey))
  {
    PreparedTargetRegionPlan = *Resource;
  }
  else if (Snapshot.TargetRegionPlanResourceKey != 0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoRollbackPlanResourceMissing key=%llu step=%d"),
      static_cast<unsigned long long>(Snapshot.TargetRegionPlanResourceKey),
      Snapshot.FixedStepIndex);
  }
  TargetRegionQuotaExecution = Snapshot.TargetRegionQuotaExecution;
  TargetRegionPlanValidation = Snapshot.TargetRegionPlanValidation;
  PreparedTargetRegionGuidance.Reset();
  TargetRegionGuidanceSummary = {};
  TargetRegionTopologyRoundHash = Snapshot.TargetRegionTopologyRoundHash;
  TargetRegionDemandRoundHash = Snapshot.TargetRegionDemandRoundHash;
  TargetRegionTransportRoundHash = Snapshot.TargetRegionTransportRoundHash;
  TargetRegionGuidanceRoundHash = Snapshot.TargetRegionGuidanceRoundHash;
  TargetRegionPlanRebuildCount = Snapshot.TargetRegionPlanRebuildCount;
  TargetRegionLifetimeRebuildCount = Snapshot.TargetRegionLifetimeRebuildCount;
  TargetRegionTargetRebuildCount = Snapshot.TargetRegionTargetRebuildCount;
  TargetRegionEnvironmentRebuildCount = Snapshot.TargetRegionEnvironmentRebuildCount;
  TargetRegionMembershipRebuildCount = Snapshot.TargetRegionMembershipRebuildCount;
  TargetRegionDemandSatisfiedRebuildCount = Snapshot.TargetRegionDemandSatisfiedRebuildCount;
  TargetRegionPathInvalidRebuildCount = Snapshot.TargetRegionPathInvalidRebuildCount;
  TargetRegionSolverMillisecondsSamples.SetNum(FMath::Min(
    TargetRegionSolverMillisecondsSamples.Num(), Snapshot.TargetRegionSolverMsSampleCount));
  bTargetRegionRoundValid = Snapshot.bTargetRegionRoundValid;
  TargetRegionInvalidStepCount = Snapshot.TargetRegionInvalidStepCount;
  TargetRegionLastInvalidStep = Snapshot.TargetRegionLastInvalidStep;
  TargetRegionValidationFailureCount = Snapshot.TargetRegionValidationFailureCount;
  TargetRegionValidationRoundHash = Snapshot.TargetRegionValidationRoundHash;
  TargetRegionGuidanceUnroutedStepCount = Snapshot.TargetRegionGuidanceUnroutedStepCount;
  TargetRegionGuidanceUnroutedAgentSampleCount = Snapshot.TargetRegionGuidanceUnroutedAgentSampleCount;
  TargetRegionGuidanceUnroutedAgentMax = Snapshot.TargetRegionGuidanceUnroutedAgentMax;
  TargetRegionGuidanceFirstFailureStep = Snapshot.TargetRegionGuidanceFirstFailureStep;
  TargetRegionGuidanceFirstFailureAgentId = Snapshot.TargetRegionGuidanceFirstFailureAgentId;
  bTargetRegionFailureFixtureValid = Snapshot.bTargetRegionFailureFixtureValid;
  TargetRegionFailureFixtureStep = Snapshot.TargetRegionFailureFixtureStep;
  TargetRegionFailureFixtureKind = Snapshot.TargetRegionFailureFixtureKind;
  TargetRegionFailureFixtureAgentId = Snapshot.TargetRegionFailureFixtureAgentId;
  TargetRegionFailureFixtureCellKey = Snapshot.TargetRegionFailureFixtureCellKey;
  TargetRegionFailureFixtureHash = Snapshot.TargetRegionFailureFixtureHash;
  TMap<uint32, TArray<float>> SolverSamplesByProfile;
  for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Existing
    : TargetRegionCapabilityCohorts)
  {
    SolverSamplesByProfile.Add(
      Existing.Cohort.CapabilityProfileKey, Existing.SolverMillisecondsSamples);
  }
  TargetRegionCapabilityCohorts.Reset(Snapshot.TargetRegionCapabilityCohorts.Num());
  for (const FCrowdDemoTargetRegionCapabilityCohortRollbackState& State
    : Snapshot.TargetRegionCapabilityCohorts)
  {
    FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime =
      TargetRegionCapabilityCohorts.AddDefaulted_GetRef();
    Runtime.Cohort = State.Cohort;
    Runtime.DemandRegionPhaseOffset = State.DemandRegionPhaseOffset;
    if (const FCrowdDemoTargetRegionFlowPlan* Resource =
      TargetRegionPlanResources.Find(State.PlanResourceKey))
      Runtime.Plan = *Resource;
    else if (State.PlanResourceKey != 0)
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRollbackCohortPlanResourceMissing profile=%u key=%llu step=%d"),
        State.Cohort.CapabilityProfileKey,
        static_cast<unsigned long long>(State.PlanResourceKey),
        Snapshot.FixedStepIndex);
    Runtime.QuotaExecution = State.QuotaExecution;
    Runtime.LastPlanReplacement = State.LastPlanReplacement;
    Runtime.Validation = State.Validation;
    Runtime.TopologyRoundHash = State.TopologyRoundHash;
    Runtime.DemandRoundHash = State.DemandRoundHash;
    Runtime.TransportRoundHash = State.TransportRoundHash;
    Runtime.GuidanceRoundHash = State.GuidanceRoundHash;
    Runtime.ValidationRoundHash = State.ValidationRoundHash;
    Runtime.PlanRebuildCount = State.PlanRebuildCount;
    Runtime.InvalidStepCount = State.InvalidStepCount;
    Runtime.ValidationFailureCount = State.ValidationFailureCount;
    Runtime.GuidanceUnroutedStepCount = State.GuidanceUnroutedStepCount;
    Runtime.LastInvalidStep = State.LastInvalidStep;
    if (TArray<float>* Samples = SolverSamplesByProfile.Find(
      State.Cohort.CapabilityProfileKey))
    {
      Samples->SetNum(FMath::Min(Samples->Num(), State.SolverMsSampleCount));
      Runtime.SolverMillisecondsSamples = MoveTemp(*Samples);
    }
    Runtime.PlanLifecycle = State.PlanLifecycle;
    Runtime.TargetEngagedHoldAgentIds = State.TargetEngagedHoldAgentIds;
    Runtime.TargetEngagementAcquireCount = State.TargetEngagementAcquireCount;
    Runtime.TargetEngagementReleaseCount = State.TargetEngagementReleaseCount;
    Runtime.TargetEngagementSuppressedRetreatCount =
      State.TargetEngagementSuppressedRetreatCount;
    Runtime.bRoundValid = State.bRoundValid;
  }
  CapabilityProfileSummary = Snapshot.CapabilityProfileSummary;
  CapabilityCohortRebuildCount = Snapshot.CapabilityCohortRebuildCount;
  TargetRegionPlanLifecycleSummary = Snapshot.TargetRegionPlanLifecycleSummary;
  TargetRegionPlanLifecycleFixture = Snapshot.TargetRegionPlanLifecycleFixture;
  FlowGoalReachedAgentIds = Snapshot.FlowGoalReachedAgentIds;
  FlowWallPassAgentIds = Snapshot.FlowWallPassAgentIds;
  FlowCorridorExitAgentIds = Snapshot.FlowCorridorExitAgentIds;
  FlowTurnExitAgentIds = Snapshot.FlowTurnExitAgentIds;
  FlowLowSpeedSecondsByAgentId = Snapshot.FlowLowSpeedSecondsByAgentId;
  FlowCorridorDeadlockAgentIds = Snapshot.FlowCorridorDeadlockAgentIds;
  LastCompareMetrics = Snapshot.CompareMetrics;
  PreparedProjectiles = Snapshot.Projectiles;
  ProjectileMetrics = Snapshot.ProjectileMetrics;
  if (IsSoftPressureRouteDiagnosticEnabled())
  {
    FCrowdDemoSoftPressureRouteDiagnosticKernel::RestoreCheckpoint(
      Snapshot.RouteDiagnosticCheckpoint, SoftPressureRouteDiagnosticRuntime);
    SoftPressureRouteDiagnosticSummary = {};
  }
  if (IsTargetStabilityDiagnosticEnabled())
  {
    FCrowdDemoTargetStabilityDiagnosticKernel::RestoreCheckpoint(
      Snapshot.TargetStabilityCheckpoint, TargetStabilityRuntime);
    TargetStabilitySummary = {};
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::InitializeOpenSpawnRelaxation(
  const FCrowdDemoOpenSpawnRelaxationLayout& Layout)
{
  OpenSpawnRelaxationLayout = Layout;
  OpenSpawnRelaxationRuntime =
    FCrowdDemoOpenSpawnRelaxationKernel::InitializeRuntime(Layout);
}

bool UCrowdDemoRoundSimPipelineSubsystem::PrepareOpenSpawnRelaxationBoundary()
{
  if (!IsOpenSpawnRelaxation())
    return false;
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(
    GetCurrentFixedStepIndex(), OpenSpawnRelaxationLayout, OpenSpawnRelaxationRuntime);
  TArray<int32> ExpectedAgentIds;
  ExpectedAgentIds.Reserve(BoundarySnapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent : BoundarySnapshot.Agents)
    ExpectedAgentIds.Add(Agent.Identity.AgentId);
  if (!FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
      GetCurrentFixedStepIndex(), ExpectedAgentIds, OpenSpawnRelaxationRuntime,
      PreparedOpenSpawnBoundaryFacts))
  {
    PreparedOpenSpawnBoundaryFacts.Reset();
    PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
    return false;
  }
  PreparedOpenSpawnBoundaryFixedStepIndex = GetCurrentFixedStepIndex();
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeOpenSpawnBoundaryResets(
  const TConstArrayView<int32> AgentIds)
{
  if (!ArePreparedOpenSpawnBoundaryFactsCurrent()) return false;
  for (const int32 AgentId : AgentIds)
  {
    const FCrowdDemoPreparedOpenSpawnBoundaryFact* Fact =
      FindPreparedOpenSpawnBoundaryFact(AgentId);
    if (!Fact || !Fact->bPendingBoundaryReset) return false;
  }
  return FCrowdDemoOpenSpawnRelaxationKernel::ConsumePendingBoundaryResets(
    AgentIds, OpenSpawnRelaxationRuntime);
}

bool UCrowdDemoRoundSimPipelineSubsystem::ArePreparedOpenSpawnBoundaryFactsCurrent() const
{
  if (!IsOpenSpawnRelaxation()) return true;
  if (PreparedOpenSpawnBoundaryFixedStepIndex != GetCurrentFixedStepIndex()) return false;
  TArray<int32> ExpectedAgentIds;
  ExpectedAgentIds.Reserve(BoundarySnapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent : BoundarySnapshot.Agents)
    ExpectedAgentIds.Add(Agent.Identity.AgentId);
  return FCrowdDemoOpenSpawnRelaxationKernel::ValidatePreparedBoundaryFacts(
    GetCurrentFixedStepIndex(), ExpectedAgentIds, PreparedOpenSpawnBoundaryFacts);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordOpenSpawnRelaxationParticleStep(
  TConstArrayView<FCrowdDemoParticleSoftPairInfluence> Influences,
  const float MaxActualCorrectionCm,
  const float SoftErrorCmP95)
{
  if (!IsOpenSpawnRelaxation())
    return;
  FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
    GetCurrentFixedStepIndex(), Influences, MaxActualCorrectionCm,
    SoftErrorCmP95, OpenSpawnRelaxationRuntime);
}

void UCrowdDemoRoundSimPipelineSubsystem::SetCapabilityCohorts(
  TArray<FCrowdDemoCapabilityCohort>&& Cohorts,
  const FCrowdDemoCapabilityProfileSummary& Summary)
{
  TargetRegionCapabilityCohorts.Reset(Cohorts.Num());
  Cohorts.Sort([](const auto& A, const auto& B)
  {
    return A.CapabilityProfileKey < B.CapabilityProfileKey;
  });
  TArray<FCrowdDemoCapabilityDemandPhase> Phases;
  uint32 PhaseHash = 0;
  const bool bPhasesValid =
    FCrowdDemoCapabilityProfileKernel::BuildDemandRegionPhaseOffsets(
      Cohorts, GetRules().TargetRegionTransportSettings.DemandRegionCount,
      Phases, PhaseHash);
  for (FCrowdDemoCapabilityCohort& Cohort : Cohorts)
  {
    FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime =
      TargetRegionCapabilityCohorts.AddDefaulted_GetRef();
    Runtime.Cohort = MoveTemp(Cohort);
    if (bPhasesValid)
    {
      const FCrowdDemoCapabilityDemandPhase* Phase = Phases.FindByPredicate(
        [&Runtime](const FCrowdDemoCapabilityDemandPhase& Candidate)
        {
          return Candidate.CapabilityProfileKey == Runtime.Cohort.CapabilityProfileKey;
        });
      Runtime.DemandRegionPhaseOffset = Phase
        ? Phase->DemandRegionPhaseOffset : 0;
    }
  }
  CapabilityProfileSummary = Summary;
  if (!bPhasesValid)
  {
    CapabilityProfileSummary.bValid = false;
    ++CapabilityProfileSummary.InvalidProfileCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCapabilityDemandPhaseInvalid cohorts=%d regions=%d hash=%u"),
      Cohorts.Num(), GetRules().TargetRegionTransportSettings.DemandRegionCount, PhaseHash);
  }
  ++CapabilityCohortRebuildCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::FinalizeTargetRegionPlanLifecycleDiagnostic()
{
  if (!IsTargetRegionPlanLifecycleDiagnosticEnabled())
  {
    TargetRegionPlanLifecycleSummary = {};
    TargetRegionPlanLifecycleFixture = {};
    return;
  }
  uint32 MissingCohortKey = 0;
  int32 MissingRegionKey = INDEX_NONE;
  bool bFoundMissing = false;
  TArray<FCrowdDemoTargetRegionPlanLifecycleRuntime> Runtimes;
  int32 ExpectedRebuildCount = 0;
  Runtimes.Reserve(TargetRegionCapabilityCohorts.Num());
  for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime
    : TargetRegionCapabilityCohorts)
  {
    Runtimes.Add(Runtime.PlanLifecycle);
    ExpectedRebuildCount += Runtime.PlanRebuildCount;
    if (!bFoundMissing)
    {
      for (const FCrowdDemoTargetDemandRegion& Region : Runtime.Demand.Regions)
      {
        if (Region.bFeasible
          && Region.CurrentPopulation < Region.DesiredPopulation)
        {
          MissingCohortKey = Runtime.Cohort.CapabilityProfileKey;
          MissingRegionKey = Region.StableRegionKey;
          bFoundMissing = true;
          break;
        }
      }
    }
  }
  TargetRegionPlanLifecycleSummary =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::BuildAggregateSummary(
      Runtimes, MissingCohortKey, MissingRegionKey,
      TargetRegionPlanLifecycleFixture);
  TargetRegionPlanLifecycleSummary.bValid =
    TargetRegionPlanLifecycleSummary.bValid
    && TargetRegionPlanLifecycleSummary.RebuildCount == ExpectedRebuildCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionTopologyStep()
{
  TargetRegionTopologyRoundHash = FoldHash(
    FoldHash(TargetRegionTopologyRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    PreparedTargetRegionTopology.TopologyHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionDemandStep()
{
  TargetRegionDemandRoundHash = FoldHash(
    FoldHash(TargetRegionDemandRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    PreparedTargetRegionDemand.DemandHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionTransportStep(
  const float SolverMilliseconds, const int32 RebuildReason)
{
  TargetRegionTransportRoundHash = FoldHash(
    FoldHash(TargetRegionTransportRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    FoldHash(PreparedTargetRegionPlan.TransportHash,
      TargetRegionQuotaExecution.ExecutionHash));
  if (RebuildReason != 0)
  {
    ++TargetRegionPlanRebuildCount;
    switch (RebuildReason)
    {
      case 1: ++TargetRegionLifetimeRebuildCount; break;
      case 2: ++TargetRegionTargetRebuildCount; break;
      case 3: ++TargetRegionEnvironmentRebuildCount; break;
      case 4: ++TargetRegionMembershipRebuildCount; break;
      case 5: ++TargetRegionDemandSatisfiedRebuildCount; break;
      case 6: ++TargetRegionPathInvalidRebuildCount; break;
      default: break;
    }
    TargetRegionSolverMillisecondsSamples.Add(FMath::Max(0.0f, SolverMilliseconds));
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionGuidanceStep()
{
  TargetRegionGuidanceRoundHash = FoldHash(
    FoldHash(TargetRegionGuidanceRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    TargetRegionGuidanceSummary.GuidanceHash);
  if (!TargetRegionGuidanceSummary.bValid)
  {
    bTargetRegionRoundValid = false;
    if (TargetRegionLastInvalidStep != GetCurrentFixedStepIndex())
    {
      TargetRegionLastInvalidStep = GetCurrentFixedStepIndex();
      ++TargetRegionInvalidStepCount;
    }
    ++TargetRegionGuidanceUnroutedStepCount;
    TargetRegionGuidanceUnroutedAgentSampleCount += TargetRegionGuidanceSummary.UnroutedAgentCount;
    TargetRegionGuidanceUnroutedAgentMax = FMath::Max(
      TargetRegionGuidanceUnroutedAgentMax, TargetRegionGuidanceSummary.UnroutedAgentCount);
    if (TargetRegionGuidanceFirstFailureStep == INDEX_NONE)
    {
      TargetRegionGuidanceFirstFailureStep = GetCurrentFixedStepIndex();
      TargetRegionGuidanceFirstFailureAgentId = TargetRegionGuidanceSummary.FirstUnroutedAgentId;
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionValidationStep()
{
  TargetRegionValidationRoundHash = FoldHash(
    FoldHash(TargetRegionValidationRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    TargetRegionPlanValidation.ValidationHash);
  if (!TargetRegionPlanValidation.bValid)
  {
    bTargetRegionRoundValid = false;
    if (TargetRegionLastInvalidStep != GetCurrentFixedStepIndex())
    {
      TargetRegionLastInvalidStep = GetCurrentFixedStepIndex();
      ++TargetRegionInvalidStepCount;
    }
    ++TargetRegionValidationFailureCount;
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::PinTargetRegionFailureFixture(
  const int32 Kind, const int32 AgentId, const int32 CellKey, const uint32 FixtureHash)
{
  if (bTargetRegionFailureFixtureValid || FixtureHash == 0) return;
  bTargetRegionFailureFixtureValid = true;
  TargetRegionFailureFixtureStep = GetCurrentFixedStepIndex();
  TargetRegionFailureFixtureKind = Kind;
  TargetRegionFailureFixtureAgentId = AgentId;
  TargetRegionFailureFixtureCellKey = CellKey;
  TargetRegionFailureFixtureHash = FixtureHash;
}

float UCrowdDemoRoundSimPipelineSubsystem::GetTargetRegionSolverMsP95() const
{
  return Percentile(TargetRegionSolverMillisecondsSamples, 0.95f);
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsTargetStabilityDiagnosticEnabled() const
{
  return bTargetStabilityDiagnosticPlanEnabled && IsActive();
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetStabilityStep(
  const FCrowdDemoTargetStabilityStepSample& Step)
{
  if (!IsTargetStabilityDiagnosticEnabled()) return;
  FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Step, TargetStabilityRuntime);
  TargetStabilitySummary = {};
}

void UCrowdDemoRoundSimPipelineSubsystem::FinalizeTargetStabilityDiagnostic()
{
  if (!IsTargetStabilityDiagnosticEnabled()) return;
  FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(
    TargetStabilityRuntime, TargetStabilitySummary);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackOutcome(
  const bool bHit,
  const bool bAgentMismatch,
  const int32 ReplayedSteps)
{
  SoftPressureRollbackSnapshotHitCount += bHit ? 1 : 0;
  SoftPressureRollbackSnapshotMissCount += bHit ? 0 : 1;
  SoftPressureRollbackAgentMismatchCount += bAgentMismatch ? 1 : 0;
  SoftPressureRollbackReplayedStepCount += FMath::Max(0, ReplayedSteps);
  const TCHAR* Role = GetWorld() && GetWorld()->GetNetMode() == NM_Client
    ? TEXT("client") : TEXT("server");
  if (bHit && !bAgentMismatch)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoRoundRollback role=%s round_id=%d hit=1 miss=0 mismatch=0 replayed_steps=%d totals=%d,%d,%d,%d"),
      Role, GetCurrentRoundId(), FMath::Max(0, ReplayedSteps),
      SoftPressureRollbackSnapshotHitCount, SoftPressureRollbackSnapshotMissCount,
      SoftPressureRollbackAgentMismatchCount, SoftPressureRollbackReplayedStepCount);
  }
  else
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoRoundRollback role=%s round_id=%d hit=0 miss=1 mismatch=%d replayed_steps=0 totals=%d,%d,%d,%d VIOLATION"),
      Role, GetCurrentRoundId(), bAgentMismatch ? 1 : 0,
      SoftPressureRollbackSnapshotHitCount, SoftPressureRollbackSnapshotMissCount,
      SoftPressureRollbackAgentMismatchCount, SoftPressureRollbackReplayedStepCount);
  }
}



void UCrowdDemoRoundSimPipelineSubsystem::RecordFlowAgentSamples(
  const TConstArrayView<FCrowdDemoRoundFlowAgentSample> Samples,
  const bool bClient)
{
  int32 UnreachableCount = 0;
  int32 PenetrationCount = 0;
  const FVector Goal = FVector(GetRules().FlowFieldConfig.GoalLocation);
  for (const FCrowdDemoRoundFlowAgentSample& Sample : Samples)
  {
    UnreachableCount += Sample.bUnreachable ? 1 : 0;
    PenetrationCount += Sample.bPenetrating ? 1 : 0;
    const bool bInsideCorridorZone = Sample.Location.Y > -2050.0f
      && Sample.Location.Y < -650.0f;
    float& LowSpeedSeconds = FlowLowSpeedSecondsByAgentId.FindOrAdd(Sample.AgentId);
    if (bInsideCorridorZone && Sample.Velocity.Size2D() < 10.0f)
    {
      LowSpeedSeconds += GetCurrentFixedStepSeconds();
      if (LowSpeedSeconds > 1.5f)
      {
        FlowCorridorDeadlockAgentIds.Add(Sample.AgentId);
      }
    }
    else
    {
      LowSpeedSeconds = 0.0f;
    }
    if (FVector::DistSquared2D(Sample.Location, Goal) <= FMath::Square(140.0f))
    {
      FlowGoalReachedAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > -1950.0f)
    {
      FlowWallPassAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > -650.0f)
    {
      FlowCorridorExitAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > 750.0f)
    {
      FlowTurnExitAgentIds.Add(Sample.AgentId);
    }
  }
  LastCompareMetrics.FlowUnreachableAgentCount = UnreachableCount;
  LastCompareMetrics.FlowGoalReachedCount = FlowGoalReachedAgentIds.Num();
  LastCompareMetrics.FlowWallPassCount = FlowWallPassAgentIds.Num();
  LastCompareMetrics.FlowCorridorExitCount = FlowCorridorExitAgentIds.Num();
  LastCompareMetrics.FlowTurnExitCount = FlowTurnExitAgentIds.Num();
  LastCompareMetrics.CorridorDeadlockAgentCount = FlowCorridorDeadlockAgentIds.Num();
  if (bClient)
  {
    LastCompareMetrics.ClientSimObstaclePenetrationCount += PenetrationCount;
  }
  else
  {
    LastCompareMetrics.ServerObstaclePenetrationCount += PenetrationCount;
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::TryBeginFixedStep(const float TargetServerTimeSeconds)
{
  if (!bPlanActive || bStepInProgress)
  {
    return false;
  }
  const float RoundEnd = ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds;
  if (SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER >= RoundEnd)
  {
    return false;
  }
  const float StepEnd = FMath::Min(SimulatedServerTimeSeconds + CurrentFixedStepSeconds, RoundEnd);
  if (StepEnd > TargetServerTimeSeconds + KINDA_SMALL_NUMBER)
  {
    return false;
  }
  CurrentStepStartServerTimeSeconds = SimulatedServerTimeSeconds;
  CurrentStepEndServerTimeSeconds = StepEnd;
  bStepInProgress = true;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::FinishFixedStep()
{
  if (bStepInProgress)
  {
    SimulatedServerTimeSeconds = CurrentStepEndServerTimeSeconds;
    ++PlanApplyBoundarySequence;
    bStepInProgress = false;
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsRoundSimScenarioActive() const
{
  return bPlanActive && IsCrowdDemoRoundSimScenario(ActivePlan.Rules.Scenario);
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedParticleDiagnosticCommit(
  FCrowdDemoPreparedParticleDiagnosticCommit&& Commit)
{
  if (!Commit.bValid
    || Commit.FixedStepIndex != GetCurrentFixedStepIndex()
    || Commit.PlanRevision != GetCurrentPlanRevision()
    || Commit.AgentCount != BoundarySnapshot.Agents.Num()
    || PreparedParticleDiagnosticCommit.bValid)
    return false;
  PreparedParticleDiagnosticCommit = MoveTemp(Commit);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::CommitPreparedParticleDiagnostics()
{
  if (!IsMovementFinalizeAppliedCurrent()
    || !IsPreparedParticleDiagnosticCommitCurrent())
    return false;

  FCrowdDemoPreparedParticleDiagnosticCommit Commit =
    MoveTemp(PreparedParticleDiagnosticCommit);
  PreparedParticleDiagnosticCommit = {};
  if (Commit.bRecordStabilityStep)
    RecordTargetStabilityStep(Commit.StabilityStep);
  if (Commit.bFinalizeStabilityDiagnostic)
    FinalizeTargetStabilityDiagnostic();
  if (Commit.bRecordCrossProfileViolations)
    RecordCrossProfileParticleViolations(
      Commit.CrossProfileHardViolationCount,
      Commit.CrossProfileSweptViolationCount);
  if (Commit.bRecordRouteStep)
    RecordSoftPressureRouteStep(Commit.RouteSamples);
  if (Commit.bFinalizeRouteDiagnostic)
    FinalizeSoftPressureRouteDiagnostic(Commit.RouteCounterfactual);
  if (Commit.bRecordOpenSpawnStep)
    RecordOpenSpawnRelaxationParticleStep(
      Commit.OpenSpawnSoftPairInfluences,
      Commit.OpenSpawnMaxAgentCorrectionCm,
      Commit.OpenSpawnSoftErrorCmP95);
  if (Commit.bRecordFailureFixture)
    RecordParticleFailureFixture(Commit.FailureFixture);
  RecordParticleConstraintSummary(
    Commit.CandidateSummary, Commit.AppliedSummary,
    Commit.AppliedStateHash, !Commit.CandidateSummary.bValid,
    Commit.SolverMilliseconds);
  return true;
}


void UCrowdDemoRoundSimPipelineSubsystem::RecordParticleConstraintSummary(
  const FCrowdDemoParticleConstraintSummary& CandidateSummary,
  const FCrowdDemoParticleConstraintSummary& AppliedSummary,
  const uint32 AppliedStateHash,
  const bool bGlobalFallback,
  const float SolverMilliseconds)
{
  if (!IsActive() || GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure)
    return;
  LastParticleCandidateSummary = CandidateSummary;
  LastParticleAppliedSummary = AppliedSummary;
  ParticleSolverMillisecondsSamples.Add(FMath::Max(0.0f, SolverMilliseconds));
  ParticleCandidateStateHash = CandidateSummary.CandidateHash;
  ParticleAppliedStateHash = AppliedStateHash;
  if (!CandidateSummary.bValid || !AppliedSummary.bValid)
  {
    ++ParticleInvalidStepCount;
    if (!bParticleConstraintRunFailure)
    {
      bParticleConstraintRunFailure = true;
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoParticleInvalid role=%s round_id=%d fixed_step=%d hard=%d swept=%d obstacle=%d bounds=%d candidate_hash=%u applied_hash=%u action=early_round_failure VIOLATION"),
        GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
        GetCurrentRoundId(), GetCurrentFixedStepIndex(),
        CandidateSummary.HardPairViolationCount + AppliedSummary.HardPairViolationCount,
        CandidateSummary.SweptPairViolationCount + AppliedSummary.SweptPairViolationCount,
        CandidateSummary.ObstaclePenetrationCount + AppliedSummary.ObstaclePenetrationCount,
        CandidateSummary.BoundsViolationCount + AppliedSummary.BoundsViolationCount,
        CandidateSummary.CandidateHash, AppliedStateHash);
    }
  }
  if (bGlobalFallback) ++ParticleGlobalFallbackStepCount;

  FCrowdDemoParticleSettlingTracker SettlingTracker;
  SettlingTracker.StepCount = ParticleStepCount;
  SettlingTracker.ConsecutiveSettledSampleCount = ParticleSettlingWindowCount;
  SettlingTracker.SettlingSteps = ParticleSettlingSteps;
  SettlingTracker.PreviousSoftErrorCmP95 = ParticlePreviousSoftErrorP95;
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
    SettlingTracker, AppliedSummary.MaxAgentCorrectionCm, AppliedSummary.SoftErrorCmP95);
  ParticleStepCount = SettlingTracker.StepCount;
  ParticleSettlingWindowCount = SettlingTracker.ConsecutiveSettledSampleCount;
  ParticleSettlingSteps = SettlingTracker.SettlingSteps;
  ParticlePreviousSoftErrorP95 = SettlingTracker.PreviousSoftErrorCmP95;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordLocalPredictiveStep(
  TArray<FCrowdDemoLocalPredictiveResult>&& Results,
  TArray<FCrowdDemoLocalPredictiveGrantState>&& GrantStates,
  const FCrowdDemoLocalPredictiveSummary& Summary)
{
  if (!IsActive()
    || GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || GetRules().LocalPredictiveSettings.bEnabled == 0)
    return;

  Results.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  GrantStates.Sort([](const auto& A, const auto& B)
  {
    return A.ComponentKey != B.ComponentKey
      ? A.ComponentKey < B.ComponentKey
      : A.GrantedAgentId < B.GrantedAgentId;
  });
  PreparedLocalPredictiveResults = MoveTemp(Results);
  LocalPredictiveGrantStates = MoveTemp(GrantStates);
  LastLocalPredictiveSummary = Summary;
  LocalPredictiveRoundHash = FoldHash(
    FoldHash(LocalPredictiveRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    Summary.CandidateHash);
  ++LocalPredictiveSampleCount;
  if (!Summary.bValid)
  {
    const bool bFirstInvalidStep = LocalPredictiveInvalidStepCount == 0;
    ++LocalPredictiveInvalidStepCount;
    if (bFirstInvalidStep)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoLocalPredictiveInvalid role=%s round_id=%d fixed_step=%d infeasible=%d quantization=%d joint=%d hash=%u VIOLATION"),
        GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
        GetCurrentRoundId(), GetCurrentFixedStepIndex(), Summary.InfeasibleAgentCount,
        Summary.QuantizationFailureCount, Summary.JointValidationFailureCount,
        Summary.CandidateHash);
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordGuidanceComposeStep(
  TArray<FCrowdDemoComposedGuidance>&& Results)
{
  if (!IsActive()) return;
  Results.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 CandidateStepHash = 2166136261u;
  uint32 ComposeStepHash = 2166136261u;
  for (const FCrowdDemoComposedGuidance& Result : Results)
  {
    CandidateStepHash = FoldHash(CandidateStepHash, Result.CandidateSetHash);
    ComposeStepHash = FoldHash(ComposeStepHash, Result.StableHash);
  }
  GuidanceCandidateRoundHash = FoldHash(
    FoldHash(GuidanceCandidateRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    CandidateStepHash);
  GuidanceComposeRoundHash = FoldHash(
    FoldHash(GuidanceComposeRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    ComposeStepHash);
  ++GuidanceComposeSampleCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordLocalPredictiveDiagnosticFrame(
  FCrowdDemoLocalPredictiveDiagnosticFrame&& Frame)
{
  if (!IsTargetStabilityDiagnosticEnabled()) return;
  LocalPredictiveDiagnosticFrame = MoveTemp(Frame);
}

bool UCrowdDemoRoundSimPipelineSubsystem::BuildCurrentLocalPredictiveComponentFixture(
  const TConstArrayView<int32> WitnessAgentIds,
  FCrowdDemoLocalPredictiveComponentFixture& OutFixture) const
{
  const FCrowdDemoLocalPredictiveDiagnosticFrame& Frame =
    LocalPredictiveDiagnosticFrame;
  return FCrowdDemoLocalPredictiveInteractionKernel::BuildComponentFixture(
    Frame.FixedStepIndex, Frame.Agents, GetRules().FlowFieldConfig, Frame.Settings,
    Frame.PreviousGrantStates, Frame.ConflictPairs, Frame.GrantStates,
    Frame.Results, Frame.Summary, Frame.Trace, WitnessAgentIds, OutFixture);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordProjectileStep(
  const FCrowdDemoProjectileStepSummary& Summary,
  const TConstArrayView<FCrowdDemoProjectileVisualEvent> Events)
{
  if (!IsRangedProjectileCombat())
    return;
  ProjectileMetrics.bValid = ProjectileMetrics.bValid != 0 && Summary.bValid ? 1 : 0;
  ProjectileMetrics.TargetAcquiredCount += Summary.TargetAcquiredCount;
  ProjectileMetrics.CompletedWindupCount += Summary.CompletedWindupCount;
  ProjectileMetrics.ProjectileSpawnedCount += Summary.SpawnedCount;
  ProjectileMetrics.ProjectileActiveCount = Summary.ActiveCount;
  ProjectileMetrics.ProjectileImpactedCount += Summary.ImpactedCount;
  ProjectileMetrics.ProjectileExpiredCount += Summary.ExpiredCount;
  ProjectileMetrics.DuplicateFireCount += Summary.DuplicateFireCount;
  ProjectileMetrics.DuplicateHitCount += Summary.DuplicateHitCount;
  ProjectileMetrics.InvalidTargetLifecycleCount += Summary.InvalidTargetLifecycleCount;
  ProjectileMetrics.InvalidProjectileCount += Summary.InvalidProjectileCount;
  const uint32 Step = static_cast<uint32>(GetCurrentFixedStepIndex());
  ProjectileMetrics.AttackStateHash = FoldHash(
    FoldHash(ProjectileMetrics.AttackStateHash, Step), Summary.AttackStateHash);
  ProjectileMetrics.ProjectileStateHash = FoldHash(
    FoldHash(ProjectileMetrics.ProjectileStateHash, Step), Summary.ProjectileStateHash);
  ProjectileMetrics.EventHash = FoldHash(
    FoldHash(ProjectileMetrics.EventHash, Step), Summary.EventHash);
  for (const FCrowdDemoProjectileVisualEvent& Event : Events)
  {
    switch (Event.Kind)
    {
      case ECrowdDemoProjectileVisualEventKind::Spawn:
        ++ProjectileMetrics.VisualSpawnEventCount;
        break;
      case ECrowdDemoProjectileVisualEventKind::Impact:
        ++ProjectileMetrics.VisualImpactEventCount;
        break;
      case ECrowdDemoProjectileVisualEventKind::Expire:
        ++ProjectileMetrics.VisualExpireEventCount;
        break;
      default:
        ProjectileMetrics.bValid = 0;
        break;
    }
  }
  if (GetWorld() && GetWorld()->GetNetMode() != NM_Client && !Events.IsEmpty())
    OutgoingProjectileVisualEvents.Append(Events.GetData(), Events.Num());
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordProjectileHitResponse(
  const FCrowdDemoHitResponseSummary& Summary)
{
  if (!IsRangedProjectileCombat())
    return;
  ProjectileMetrics.bValid = ProjectileMetrics.bValid != 0 && Summary.bValid ? 1 : 0;
  ProjectileMetrics.DuplicateHitCount += Summary.DuplicateHitCount;
  ProjectileMetrics.DamageAppliedCount += Summary.AppliedHitCount;
}

FCrowdDemoProjectileMetrics
UCrowdDemoRoundSimPipelineSubsystem::BuildProjectileMetrics() const
{
  FCrowdDemoProjectileMetrics Result = ProjectileMetrics;
  const bool bLifecycleConserved =
    Result.ProjectileSpawnedCount
      == Result.ProjectileActiveCount + Result.ProjectileImpactedCount
        + Result.ProjectileExpiredCount;
  const bool bEventsConserved =
    Result.VisualSpawnEventCount == Result.ProjectileSpawnedCount
    && Result.VisualImpactEventCount == Result.ProjectileImpactedCount
    && Result.VisualExpireEventCount == Result.ProjectileExpiredCount;
  Result.bValid = Result.bValid != 0
    && Result.CompletedWindupCount == Result.ProjectileSpawnedCount
    && bLifecycleConserved && bEventsConserved
    && Result.DuplicateFireCount == 0
    && Result.DuplicateHitCount == 0
    && Result.InvalidProjectileCount == 0 ? 1 : 0;
  return Result;
}

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueProjectileVisualEvents(
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents)
{
  if (OutgoingProjectileVisualEvents.IsEmpty())
    return false;
  OutEvents = MoveTemp(OutgoingProjectileVisualEvents);
  OutgoingProjectileVisualEvents.Reset();
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordParticleFailureFixture(
  const FCrowdDemoParticleFailureFixture& Fixture)
{
  if (!ParticleFailureFixture.bValid && Fixture.bValid)
    ParticleFailureFixture = Fixture;
}

void UCrowdDemoRoundSimPipelineSubsystem::StopAfterParticleConstraintFailure()
{
  if (GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && bParticleConstraintRunFailure)
  {
    bPlanActive = false;
    bStepInProgress = false;
  }
}

float UCrowdDemoRoundSimPipelineSubsystem::GetParticleSolverMsP95() const
{
  return Percentile(ParticleSolverMillisecondsSamples, 0.95f);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPerformanceStage(
  const ECrowdDemoRoundPerformanceStage Stage,
  const float Milliseconds)
{
  const uint8 Index = static_cast<uint8>(Stage);
  if (Index < static_cast<uint8>(ECrowdDemoRoundPerformanceStage::Count)
    && Milliseconds >= 0.0f)
  {
    RoundPerformanceStageMsSamples[Index].Add(Milliseconds);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetTopologyPerformance(
  const bool bBuilt)
{
  if (bBuilt) ++PerformanceTargetTopologyBuildCount;
  else ++PerformanceTargetTopologyCacheHitCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetDemandPerformance(
  const bool bFullBuild)
{
  if (bFullBuild) ++PerformanceTargetDemandFullBuildCount;
  else ++PerformanceTargetDemandPopulationUpdateCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordFixedStepPerformance(
  const float Milliseconds)
{
  if (Milliseconds < 0.0f)
  {
    return;
  }
  FixedStepPipelineMsSamples.Add(Milliseconds);
  if (PendingRollbackReplaySteps > 0)
  {
    PendingRollbackReplayMilliseconds += Milliseconds;
    --PendingRollbackReplaySteps;
    if (PendingRollbackReplaySteps == 0)
    {
      RollbackReplayMsSamples.Add(PendingRollbackReplayMilliseconds);
      PendingRollbackReplayMilliseconds = 0.0f;
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPipelineFramePerformance(
  const int32 ExecutedSteps,
  const float TargetServerTimeSeconds,
  const bool bHitFixedStepLimit,
  const bool bHitCatchupCpuBudget)
{
  if (!IsActive())
  {
    return;
  }
  FixedStepsPerGameFrameSamples.Add(static_cast<float>(FMath::Max(0, ExecutedSteps)));
  if (ExecutedSteps > 1)
  {
    ++PerformanceCatchupFrameCount;
  }
  if (bHitFixedStepLimit)
  {
    ++PerformanceMaxFixedStepsPerFrameHitCount;
  }
  if (bHitCatchupCpuBudget)
  {
    ++PerformanceCatchupCpuBudgetHitCount;
    ++PerformanceCatchupCpuBudgetConsecutiveCount;
    PerformanceCatchupCpuBudgetConsecutiveMax = FMath::Max(
      PerformanceCatchupCpuBudgetConsecutiveMax,
      PerformanceCatchupCpuBudgetConsecutiveCount);
  }
  else
  {
    PerformanceCatchupCpuBudgetConsecutiveCount = 0;
  }
  PerformanceFixedStepBacklogMsMax = FMath::Max(
    PerformanceFixedStepBacklogMsMax,
    FMath::Max(0.0f, TargetServerTimeSeconds - SimulatedServerTimeSeconds) * 1000.0f);
}

void UCrowdDemoRoundSimPipelineSubsystem::BeginRollbackReplayPerformance(
  const int32 ReplayedSteps,
  const float ApplyMilliseconds,
  const bool bZeroErrorReplay)
{
  if (PendingRollbackReplaySteps > 0)
  {
    RollbackReplayMsSamples.Add(PendingRollbackReplayMilliseconds);
  }
  PendingRollbackReplaySteps = FMath::Max(0, ReplayedSteps);
  PendingRollbackReplayMilliseconds = FMath::Max(0.0f, ApplyMilliseconds);
  if (bZeroErrorReplay)
  {
    ++PerformanceZeroErrorRollbackReplayCount;
  }
  if (PendingRollbackReplaySteps == 0)
  {
    RollbackReplayMsSamples.Add(PendingRollbackReplayMilliseconds);
    PendingRollbackReplayMilliseconds = 0.0f;
  }
}

FCrowdDemoRoundPerformanceMetrics
UCrowdDemoRoundSimPipelineSubsystem::BuildRoundPerformanceMetrics() const
{
  FCrowdDemoRoundPerformanceMetrics Result;
  const auto MaxSample = [](const TArray<float>& Samples)
  {
    float MaxValue = -1.0f;
    for (const float Value : Samples)
    {
      MaxValue = FMath::Max(MaxValue, Value);
    }
    return MaxValue;
  };
  const auto FillStage = [this, &MaxSample](
      const ECrowdDemoRoundPerformanceStage Stage, float& OutP95, float& OutMax)
  {
    const TArray<float>& Samples =
      RoundPerformanceStageMsSamples[static_cast<uint8>(Stage)];
    OutP95 = Percentile(Samples, 0.95f);
    OutMax = MaxSample(Samples);
  };

  Result.FixedStepPipelineMsP50 = Percentile(FixedStepPipelineMsSamples, 0.50f);
  Result.FixedStepPipelineMsP95 = Percentile(FixedStepPipelineMsSamples, 0.95f);
  Result.FixedStepPipelineMsMax = MaxSample(FixedStepPipelineMsSamples);
  FillStage(ECrowdDemoRoundPerformanceStage::BusinessPrepare,
    Result.BusinessPrepareStageMsP95, Result.BusinessPrepareStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::SharedFlow,
    Result.SharedFlowStageMsP95, Result.SharedFlowStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetTopology,
    Result.TargetTopologyStageMsP95, Result.TargetTopologyStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetDemand,
    Result.TargetDemandStageMsP95, Result.TargetDemandStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetPlan,
    Result.TargetPlanStageMsP95, Result.TargetPlanStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetGuidance,
    Result.TargetGuidanceStageMsP95, Result.TargetGuidanceStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::GuidanceCompose,
    Result.GuidanceComposeStageMsP95, Result.GuidanceComposeStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::LocalPredictive,
    Result.LocalPredictiveStageMsP95, Result.LocalPredictiveStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::Particle,
    Result.ParticleStageMsP95, Result.ParticleStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::FacingFinalize,
    Result.FacingFinalizeStageMsP95, Result.FacingFinalizeStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::Commit,
    Result.CommitStageMsP95, Result.CommitStageMsMax);
  Result.TargetTopologyBuildCount = PerformanceTargetTopologyBuildCount;
  Result.TargetTopologyCacheHitCount = PerformanceTargetTopologyCacheHitCount;
  Result.TargetDemandFullBuildCount = PerformanceTargetDemandFullBuildCount;
  Result.TargetDemandPopulationUpdateCount = PerformanceTargetDemandPopulationUpdateCount;
  Result.FixedStepsPerGameFrameP50 = Percentile(FixedStepsPerGameFrameSamples, 0.50f);
  Result.FixedStepsPerGameFrameP95 = Percentile(FixedStepsPerGameFrameSamples, 0.95f);
  Result.FixedStepsPerGameFrameMax = FMath::Max(
    0, FMath::RoundToInt(MaxSample(FixedStepsPerGameFrameSamples)));
  Result.CatchupFrameCount = PerformanceCatchupFrameCount;
  Result.CatchupCpuBudgetHitCount = PerformanceCatchupCpuBudgetHitCount;
  Result.CatchupCpuBudgetConsecutiveMax = PerformanceCatchupCpuBudgetConsecutiveMax;
  Result.MaxFixedStepsPerFrameHitCount = PerformanceMaxFixedStepsPerFrameHitCount;
  Result.FixedStepBacklogMsMax = PerformanceFixedStepBacklogMsMax;
  const double WallElapsedSeconds = FPlatformTime::Seconds() - PerformanceRoundWallStartSeconds;
  if (WallElapsedSeconds > UE_SMALL_NUMBER)
  {
    Result.SimulationRealtimeFactor = FMath::Max(
      0.0f, SimulatedServerTimeSeconds - PerformanceRoundSimStartSeconds)
      / static_cast<float>(WallElapsedSeconds);
  }
  Result.RollbackReplayMsP95 = Percentile(RollbackReplayMsSamples, 0.95f);
  Result.RollbackReplayMsMax = MaxSample(RollbackReplayMsSamples);
  Result.RollbackReplaySampleCount = RollbackReplayMsSamples.Num();
  Result.ZeroErrorRollbackReplayCount = PerformanceZeroErrorRollbackReplayCount;
  return Result;
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSoftPressureRouteDiagnosticEnabled() const
{
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSoftPressureRouteDiagnostic"));
  return IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && (bEnabled || IsOpenCohortMovement());
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRouteStep(
  const TConstArrayView<FCrowdDemoSoftPressureRouteStepSample> Samples)
{
  if (!IsSoftPressureRouteDiagnosticEnabled()) return;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
    Samples, SoftPressureRouteDiagnosticRuntime);
  SoftPressureRouteDiagnosticSummary = {};
}

void UCrowdDemoRoundSimPipelineSubsystem::FinalizeSoftPressureRouteDiagnostic(
  const FCrowdDemoSoftPressureRouteCounterfactual& Counterfactual)
{
  if (!IsSoftPressureRouteDiagnosticEnabled()
    || SoftPressureRouteDiagnosticSummary.bValid) return;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    SoftPressureRouteDiagnosticRuntime, Counterfactual,
    SoftPressureRouteDiagnosticSummary);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundStart(TConstArrayView<FCrowdDemoRoundAgentState> States)
{
  RoundInitialOverlapPairCount = CountOverlapPairs(States, OverlapRadiusCm);
  RoundInitialSevereOverlapPairCount = CountOverlapPairs(States, SevereOverlapRadiusCm);
  LastCompareMetrics.InitialOverlapPairCount = RoundInitialOverlapPairCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordCorrectionComparisonAndApplied(
  TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
  const FCrowdDemoCorrectionFrame& Frame,
  const float CurrentServerTimeSeconds)
{
  TMap<int32, const FCrowdDemoRoundAgentState*> ClientById;
  for (const FCrowdDemoRoundAgentState& State : ClientStates)
  {
    ClientById.Add(State.AgentId, &State);
  }
  TArray<float> PositionErrors;
  TArray<float> YawErrors;
  TArray<float> VelocityErrors;
  float MaxPositionError = 0.0f;
  int32 MaxAgentId = INDEX_NONE;
  for (const FCrowdDemoRoundAgentState& ServerState : Frame.AgentStates)
  {
    const FCrowdDemoRoundAgentState* const* ClientStatePtr = ClientById.Find(ServerState.AgentId);
    if (!ClientStatePtr)
    {
      continue;
    }
    const FCrowdDemoRoundAgentState& ClientState = **ClientStatePtr;
    const float PositionError = FVector::Dist2D(FVector(ClientState.Location), FVector(ServerState.Location));
    PositionErrors.Add(PositionError);
    YawErrors.Add(FMath::Abs(FMath::FindDeltaAngleDegrees(ClientState.YawDegrees, ServerState.YawDegrees)));
    VelocityErrors.Add(FVector::Dist2D(FVector(ClientState.Velocity), FVector(ServerState.Velocity)));
    if (PositionError > MaxPositionError)
    {
      MaxPositionError = PositionError;
      MaxAgentId = ServerState.AgentId;
    }
  }
  LastCorrectionMetrics.CorrectionFrameRevision = Frame.CorrectionRevision;
  LastCorrectionMetrics.CorrectionFrameLatestRevisionApplied = Frame.CorrectionRevision;
  LastCorrectionMetrics.CorrectionAgentCount = Frame.AgentStates.Num();
  LastCorrectionMetrics.CorrectionPositionErrorCmP50 = Percentile(PositionErrors, 0.50f);
  LastCorrectionMetrics.CorrectionPositionErrorCmP95 = Percentile(PositionErrors, 0.95f);
  LastCorrectionMetrics.CorrectionPositionErrorCmMax = MaxPositionError;
  LastCorrectionMetrics.CorrectionYawErrorDegP95 = Percentile(YawErrors, 0.95f);
  LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95 = Percentile(VelocityErrors, 0.95f);
  LastCorrectionMetrics.CorrectionErrorAgentIdMax = MaxAgentId;
  LastCorrectionMetrics.RoundTimeDeltaMs = FMath::Max(0.0f, (CurrentServerTimeSeconds - Frame.ServerTimeSeconds) * 1000.0f);
  LastCorrectionMetrics.CorrectionFrameAgeMsP95 = LastCorrectionMetrics.RoundTimeDeltaMs;
  // Transport age is not CPU replay cost. Real replay work is measured by the
  // fixed-step performance tracker after rollback restores the old boundary.
  LastCorrectionMetrics.CorrectionFrameReplayMsP95 = -1.0f;
  if (Frame.RoundId > 0)
  {
    CorrectionIntervalPositionP95Samples.Add(LastCorrectionMetrics.CorrectionPositionErrorCmP95);
    CorrectionIntervalPositionMaxSamples.Add(LastCorrectionMetrics.CorrectionPositionErrorCmMax);
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95 = Percentile(
      CorrectionIntervalPositionP95Samples,
      0.95f);
    LastCompareMetrics.CorrectionIntervalPositionErrorCmMax = -1.0f;
    for (const float Sample : CorrectionIntervalPositionMaxSamples)
    {
      LastCompareMetrics.CorrectionIntervalPositionErrorCmMax = FMath::Max(
        LastCompareMetrics.CorrectionIntervalPositionErrorCmMax,
        Sample);
    }
    ++LastCorrectionMetrics.CorrectionFrameAppliedCount;
    ++LastCorrectionMetrics.CorrectionFrameReplayToNowCount;
    LastAppliedCorrectionRevision = Frame.CorrectionRevision;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoCorrectionFrame role=client revision=%d round_id=%d applied_count=%d agents=%d correction_position_error_cm_p50=%.3f correction_position_error_cm_p95=%.3f correction_position_error_cm_max=%.3f correction_yaw_error_deg_p95=%.3f correction_velocity_error_cmps_p95=%.3f round_time_delta_ms=%.3f apply_boundary_time=%.3f source=MassPipeline"),
      Frame.CorrectionRevision,
      Frame.RoundId,
      LastCorrectionMetrics.CorrectionFrameAppliedCount,
      Frame.AgentStates.Num(),
      LastCorrectionMetrics.CorrectionPositionErrorCmP50,
      LastCorrectionMetrics.CorrectionPositionErrorCmP95,
      LastCorrectionMetrics.CorrectionPositionErrorCmMax,
      LastCorrectionMetrics.CorrectionYawErrorDegP95,
      LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95,
      LastCorrectionMetrics.RoundTimeDeltaMs,
      SimulatedServerTimeSeconds);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundResultComparisonAndApplied(
  TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
  const FCrowdDemoRoundResultPacket& Packet)
{
  if (GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    const bool bRoundInitialStateMatch =
      RoundInputHash == Packet.ParticleMetrics.RoundInputHash
      && RoundInitialStateHash == Packet.ParticleMetrics.RoundInitialStateHash
      && RoundResetCount == Packet.ParticleMetrics.RoundResetCount
      && RoundTransitionOrderViolationCount
        == Packet.ParticleMetrics.RoundTransitionOrderViolationCount;
    const bool bDynamicFlowMatch =
      SharedFlowField.TopologyHash == Packet.ParticleMetrics.DynamicFlowTopologyHash
      && DynamicFlowAnchorCellKey == Packet.ParticleMetrics.DynamicFlowAnchorCellKey
      && SharedFlowField.IntegrationHash
        == Packet.ParticleMetrics.DynamicFlowIntegrationHash
      && DynamicFlowIntegrationRebuildCount
        == Packet.ParticleMetrics.DynamicFlowIntegrationRebuildCount
      && DynamicFlowRoundHash == Packet.ParticleMetrics.DynamicFlowRoundHash;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoRoundInputCheckpoint role=client round_id=%d input_hash=%u/%u initial_hash=%u/%u reset_count=%d/%d transition_violations=%d/%d match=%d%s"),
      Packet.RoundId, RoundInputHash, Packet.ParticleMetrics.RoundInputHash,
      RoundInitialStateHash, Packet.ParticleMetrics.RoundInitialStateHash,
      RoundResetCount, Packet.ParticleMetrics.RoundResetCount,
      RoundTransitionOrderViolationCount,
      Packet.ParticleMetrics.RoundTransitionOrderViolationCount,
      bRoundInitialStateMatch ? 1 : 0,
      bRoundInitialStateMatch ? TEXT("") : TEXT(" VIOLATION"));
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoDynamicFlowCheckpoint role=client round_id=%d topology_hash=%u/%u anchor_cell=%d/%d integration_hash=%u/%u rebuilds=%d/%d round_hash=%u/%u match=%d%s"),
      Packet.RoundId, SharedFlowField.TopologyHash,
      Packet.ParticleMetrics.DynamicFlowTopologyHash, DynamicFlowAnchorCellKey,
      Packet.ParticleMetrics.DynamicFlowAnchorCellKey,
      SharedFlowField.IntegrationHash,
      Packet.ParticleMetrics.DynamicFlowIntegrationHash,
      DynamicFlowIntegrationRebuildCount,
      Packet.ParticleMetrics.DynamicFlowIntegrationRebuildCount,
      DynamicFlowRoundHash, Packet.ParticleMetrics.DynamicFlowRoundHash,
      bDynamicFlowMatch ? 1 : 0,
      bDynamicFlowMatch ? TEXT("") : TEXT(" VIOLATION"));
    const FCrowdDemoSharedFlowMetrics LocalFlowMetrics = BuildSharedFlowMetrics(ClientStates);
    const FCrowdDemoSharedFlowMetrics& ServerFlowMetrics = Packet.SharedFlowMetrics;
    const bool bFlowConnectivityMatch =
      LocalFlowMetrics.SharedFlowFieldBuildHash
        == ServerFlowMetrics.SharedFlowFieldBuildHash
      && LocalFlowMetrics.SharedFlowConnectivityContractVersion
        == ServerFlowMetrics.SharedFlowConnectivityContractVersion
      && LocalFlowMetrics.SharedFlowValidDirectedEdgeCount
        == ServerFlowMetrics.SharedFlowValidDirectedEdgeCount
      && FMath::IsNearlyEqual(
        LocalFlowMetrics.NavigationHardClearanceCm,
        ServerFlowMetrics.NavigationHardClearanceCm, 0.001f)
      && LocalFlowMetrics.FlowRecoveredFromRasterMismatchCount
        == ServerFlowMetrics.FlowRecoveredFromRasterMismatchCount
      && LocalFlowMetrics.FlowDesiredSegmentHardObstacleViolationCount
        == ServerFlowMetrics.FlowDesiredSegmentHardObstacleViolationCount
      && LocalFlowMetrics.NavigationCenterAnchorCount
        == ServerFlowMetrics.NavigationCenterAnchorCount
      && LocalFlowMetrics.NavigationConnectionPointCount
        == ServerFlowMetrics.NavigationConnectionPointCount
      && LocalFlowMetrics.NavigationSafeIntervalCount
        == ServerFlowMetrics.NavigationSafeIntervalCount
      && LocalFlowMetrics.NavigationInternalEdgeCount
        == ServerFlowMetrics.NavigationInternalEdgeCount
      && LocalFlowMetrics.NavigationDirectedEdgeCount
        == ServerFlowMetrics.NavigationDirectedEdgeCount
      && LocalFlowMetrics.CenterInvalidButConnectedCellCount
        == ServerFlowMetrics.CenterInvalidButConnectedCellCount
      && LocalFlowMetrics.SourceAttachmentSuccessCount
        == ServerFlowMetrics.SourceAttachmentSuccessCount
      && LocalFlowMetrics.GoalAttachmentCount
        == ServerFlowMetrics.GoalAttachmentCount
      && LocalFlowMetrics.NavigationUnreachableSampleCount
        == ServerFlowMetrics.NavigationUnreachableSampleCount
      && LocalFlowMetrics.AgentStateHash
        == ServerFlowMetrics.AgentStateHash
      && LocalFlowMetrics.NavigationV2Hash
        == ServerFlowMetrics.NavigationV2Hash;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoFlowConnectivityHash role=client round_id=%d local_hash=%u server_hash=%u local_contract=%d server_contract=%d local_edges=%d server_edges=%d local_clearance_cm=%.3f server_clearance_cm=%.3f local_recovered=%d server_recovered=%d local_desired_segment_violations=%d server_desired_segment_violations=%d match=%d source=MassPipeline%s"),
      Packet.RoundId, LocalFlowMetrics.SharedFlowFieldBuildHash,
      ServerFlowMetrics.SharedFlowFieldBuildHash,
      LocalFlowMetrics.SharedFlowConnectivityContractVersion,
      ServerFlowMetrics.SharedFlowConnectivityContractVersion,
      LocalFlowMetrics.SharedFlowValidDirectedEdgeCount,
      ServerFlowMetrics.SharedFlowValidDirectedEdgeCount,
      LocalFlowMetrics.NavigationHardClearanceCm,
      ServerFlowMetrics.NavigationHardClearanceCm,
      LocalFlowMetrics.FlowRecoveredFromRasterMismatchCount,
      ServerFlowMetrics.FlowRecoveredFromRasterMismatchCount,
      LocalFlowMetrics.FlowDesiredSegmentHardObstacleViolationCount,
      ServerFlowMetrics.FlowDesiredSegmentHardObstacleViolationCount,
      bFlowConnectivityMatch ? 1 : 0,
      bFlowConnectivityMatch ? TEXT("") : TEXT(" VIOLATION"));
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoFlowV2Metrics role=client round_id=%d agent_state_hash=%u/%u anchors=%d connections=%d safe_intervals=%d internal_edges=%d directed_edges=%d center_invalid_connected=%d source_attachment_success=%d goal_attachments=%d navigation_unreachable_samples=%d navigation_v2_hash=%u match=%d source=MassPipeline%s"),
      Packet.RoundId, LocalFlowMetrics.AgentStateHash,
      ServerFlowMetrics.AgentStateHash,
      LocalFlowMetrics.NavigationCenterAnchorCount,
      LocalFlowMetrics.NavigationConnectionPointCount,
      LocalFlowMetrics.NavigationSafeIntervalCount,
      LocalFlowMetrics.NavigationInternalEdgeCount,
      LocalFlowMetrics.NavigationDirectedEdgeCount,
      LocalFlowMetrics.CenterInvalidButConnectedCellCount,
      LocalFlowMetrics.SourceAttachmentSuccessCount,
      LocalFlowMetrics.GoalAttachmentCount,
      LocalFlowMetrics.NavigationUnreachableSampleCount,
      LocalFlowMetrics.NavigationV2Hash, bFlowConnectivityMatch ? 1 : 0,
      bFlowConnectivityMatch ? TEXT("") : TEXT(" VIOLATION"));
    if (!bFlowConnectivityMatch)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoFlowConnectivityHash role=client round_id=%d match=0 VIOLATION"),
        Packet.RoundId);
    }
    LastCompareMetrics.ParticleMetrics = Packet.ParticleMetrics;
    LastCompareMetrics.ParticleMetrics.RollbackSnapshotHitCount =
      SoftPressureRollbackSnapshotHitCount;
    LastCompareMetrics.ParticleMetrics.RollbackSnapshotMissCount =
      SoftPressureRollbackSnapshotMissCount;
    LastCompareMetrics.ParticleMetrics.RollbackAgentMismatchCount =
      SoftPressureRollbackAgentMismatchCount;
    LastCompareMetrics.ParticleMetrics.RollbackReplayedStepCount =
      SoftPressureRollbackReplayedStepCount;
    LastCompareMetrics.ParticleMetrics.Performance.ZeroErrorRollbackReplayCount =
      PerformanceZeroErrorRollbackReplayCount;
    const bool bRollbackValidationRequested = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoRequireParticleCorrectionReplay"));
    const bool bRollbackValidationPass = SoftPressureRollbackSnapshotHitCount > 0
      && (SoftPressureRollbackReplayedStepCount > 0
        || PerformanceZeroErrorRollbackReplayCount > 0)
      && SoftPressureRollbackSnapshotMissCount == 0
      && SoftPressureRollbackAgentMismatchCount == 0;
    UE_LOG(LogTemp,
      Display,
      TEXT("CrowdDemoRoundRollbackSummary role=client round_id=%d hit=%d miss=%d mismatch=%d replayed_steps=%d zero_error_fast_path=%d processed_corrections=%d validation_required=%d pass=%d source=MassPipeline"),
      Packet.RoundId, SoftPressureRollbackSnapshotHitCount,
      SoftPressureRollbackSnapshotMissCount, SoftPressureRollbackAgentMismatchCount,
      SoftPressureRollbackReplayedStepCount, PerformanceZeroErrorRollbackReplayCount,
      SoftPressureRollbackReplayedStepCount + PerformanceZeroErrorRollbackReplayCount,
      bRollbackValidationRequested ? 1 : 0,
      bRollbackValidationPass ? 1 : 0);
    if (bRollbackValidationRequested && !bRollbackValidationPass)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoRoundRollbackSummary role=client round_id=%d validation_failed=1 VIOLATION"),
        Packet.RoundId);
    }
    const bool bCandidateHashMatch = ParticleCandidateStateHash
      == Packet.ParticleMetrics.ParticleCandidateHash;
    const bool bAppliedHashMatch = ParticleAppliedStateHash
      == Packet.ParticleMetrics.ParticleAppliedStateHash;
    const bool bGuidanceHashMatch = GuidanceCandidateRoundHash
        == Packet.ParticleMetrics.GuidanceCandidateHash
      && GuidanceComposeRoundHash == Packet.ParticleMetrics.GuidanceComposeHash
      && GuidanceComposeSampleCount
        == Packet.ParticleMetrics.GuidanceComposeSampleCount;
    const bool bLocalPredictiveHashMatch =
      GetRules().LocalPredictiveSettings.bEnabled == 0
      || (LocalPredictiveRoundHash == Packet.ParticleMetrics.LocalPredictiveHash
        && LocalPredictiveSampleCount == Packet.ParticleMetrics.LocalPredictiveSampleCount
        && LocalPredictiveInvalidStepCount
          == Packet.ParticleMetrics.LocalPredictiveInvalidStepCount);
    const bool bT1HashMatch = Packet.ParticleMetrics.bT1Valid == 0
      || (OpenSpawnRelaxationRuntime.bValid
        && OpenSpawnRelaxationRuntime.ParticipationHash == Packet.ParticleMetrics.T1ParticipationHash
        && OpenSpawnRelaxationRuntime.PropagationHash == Packet.ParticleMetrics.T1PropagationHash
        && OpenSpawnRelaxationRuntime.PhaseHash == Packet.ParticleMetrics.T1PhaseHash);
    if (Packet.ParticleMetrics.bT1Valid != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT1Checkpoint role=client round_id=%d phase=%d active=%d layer_max=%d insert_settle=%d post_remove_settle=%d participation_hash=%u/%u propagation_hash=%u/%u phase_hash=%u/%u match=%d%s"),
        Packet.RoundId, Packet.ParticleMetrics.T1Phase,
        Packet.ParticleMetrics.T1ActiveAgentCount,
        Packet.ParticleMetrics.T1PressurePropagationLayerMax,
        Packet.ParticleMetrics.T1InsertSettlingStep,
        Packet.ParticleMetrics.T1PostRemovalSettlingStep,
        OpenSpawnRelaxationRuntime.ParticipationHash,
        Packet.ParticleMetrics.T1ParticipationHash,
        OpenSpawnRelaxationRuntime.PropagationHash,
        Packet.ParticleMetrics.T1PropagationHash,
        OpenSpawnRelaxationRuntime.PhaseHash,
        Packet.ParticleMetrics.T1PhaseHash,
        bT1HashMatch ? 1 : 0, bT1HashMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bT1HashMatch)
        UE_LOG(LogTemp, Error, TEXT("CrowdDemoT1Checkpoint role=client hash_mismatch=1 VIOLATION"));
    }
    const bool bT2HashMatch = Packet.ParticleMetrics.T2LayoutHash == 0
      || (OpenCohortMovementLayout.bValid
        && OpenCohortMovementProgress.bValid
        && OpenCohortMovementLayout.LayoutHash == Packet.ParticleMetrics.T2LayoutHash
        && OpenCohortMovementProgress.ProgressHash == Packet.ParticleMetrics.T2ProgressHash);
    if (Packet.ParticleMetrics.T2LayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT2Checkpoint role=client round_id=%d valid=%d layout_hash=%u/%u route_hash=%u/%u progress_hash=%u/%u flow_approach_entered_count=%d transport_handoff_count=%d inside_effective_band_count=%d feasible_region_count=%d feasible_region_coverage_count=%d plan_unrouted_count=%d guidance_unrouted_count=%d transport_validation_failure_count=%d terminal_settled_count=%d terminal_settled_step=%d applied_forward_cmps_p50=%.3f applied_forward_cmps_p95=%.3f flow_contract_violation_count=%d final_deadlock_agent_count=%d match=%d%s"),
        Packet.RoundId, Packet.ParticleMetrics.bT2Valid,
        OpenCohortMovementLayout.LayoutHash,
        Packet.ParticleMetrics.T2LayoutHash,
        SoftPressureRouteDiagnosticSummary.StableHash,
        Packet.ParticleMetrics.T2RouteDiagnosticHash,
        OpenCohortMovementProgress.ProgressHash,
        Packet.ParticleMetrics.T2ProgressHash,
        Packet.ParticleMetrics.T2FlowApproachEnteredCount,
        Packet.ParticleMetrics.T2TransportHandoffCount,
        Packet.ParticleMetrics.T2InsideEffectiveBandCount,
        Packet.ParticleMetrics.T2FeasibleRegionCount,
        Packet.ParticleMetrics.T2FeasibleRegionCoverageCount,
        Packet.ParticleMetrics.T2PlanUnroutedCount,
        Packet.ParticleMetrics.T2GuidanceUnroutedCount,
        Packet.ParticleMetrics.T2TransportValidationFailureCount,
        Packet.ParticleMetrics.T2TerminalSettledCount,
        Packet.ParticleMetrics.T2TerminalSettledStep,
        Packet.ParticleMetrics.T2AppliedForwardCmpsP50,
        Packet.ParticleMetrics.T2AppliedForwardCmpsP95,
        Packet.ParticleMetrics.T2FlowContractViolationCount,
        Packet.ParticleMetrics.T2FinalDeadlockAgentCount,
        bT2HashMatch ? 1 : 0, bT2HashMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bT2HashMatch)
        UE_LOG(LogTemp, Error, TEXT("CrowdDemoT2Checkpoint role=client hash_mismatch=1 VIOLATION"));
    }
    const FCrowdDemoSharedFlowField* T3Cohort0Field =
      FindBidirectionalSwapFlowField(0);
    const FCrowdDemoSharedFlowField* T3Cohort1Field =
      FindBidirectionalSwapFlowField(10);
    const bool bT3HashMatch = Packet.ParticleMetrics.T3LayoutHash == 0
      || (BidirectionalSwapLayout.bValid
        && BidirectionalSwapProgress.bValid
        && T3Cohort0Field && T3Cohort1Field
        && BidirectionalSwapLayout.LayoutHash == Packet.ParticleMetrics.T3LayoutHash
        && BidirectionalSwapProgress.ProgressHash == Packet.ParticleMetrics.T3ProgressHash
        && T3Cohort0Field->BuildHash == Packet.ParticleMetrics.T3Cohort0FlowHash
        && T3Cohort1Field->BuildHash == Packet.ParticleMetrics.T3Cohort1FlowHash);
    if (Packet.ParticleMetrics.T3LayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT3Checkpoint role=client round_id=%d valid=%d layout_hash=%u/%u flow_hashes=%u,%u/%u,%u progress_hash=%u/%u cohort_agents=%d,%d center_crossed=%d,%d completed=%d,%d total_completed=%d throughput_difference=%d final_deadlock=%d unreachable_samples=%d completion_step_max=%d match=%d%s"),
        Packet.RoundId, Packet.ParticleMetrics.bT3Valid,
        BidirectionalSwapLayout.LayoutHash, Packet.ParticleMetrics.T3LayoutHash,
        T3Cohort0Field ? T3Cohort0Field->BuildHash : 0,
        T3Cohort1Field ? T3Cohort1Field->BuildHash : 0,
        Packet.ParticleMetrics.T3Cohort0FlowHash,
        Packet.ParticleMetrics.T3Cohort1FlowHash,
        BidirectionalSwapProgress.ProgressHash,
        Packet.ParticleMetrics.T3ProgressHash,
        Packet.ParticleMetrics.T3Cohort0AgentCount,
        Packet.ParticleMetrics.T3Cohort1AgentCount,
        Packet.ParticleMetrics.T3Cohort0CenterCrossedCount,
        Packet.ParticleMetrics.T3Cohort1CenterCrossedCount,
        Packet.ParticleMetrics.T3Cohort0CompletedCount,
        Packet.ParticleMetrics.T3Cohort1CompletedCount,
        Packet.ParticleMetrics.T3CompletedCount,
        Packet.ParticleMetrics.T3ThroughputDifference,
        Packet.ParticleMetrics.T3FinalDeadlockAgentCount,
        Packet.ParticleMetrics.T3UnreachableSampleCount,
        Packet.ParticleMetrics.T3CompletionStepMax,
        bT3HashMatch ? 1 : 0, bT3HashMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bT3HashMatch)
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoT3Checkpoint role=client hash_mismatch=1 VIOLATION"));
    }
    const bool bT4HashMatch = Packet.ParticleMetrics.T4LayoutHash == 0
      || (ValidCorridorTransitLayout.bValid
        && ValidCorridorTransitProgress.bValid
        && SharedFlowField.IsValid()
        && ValidCorridorTransitLayout.LayoutHash
          == Packet.ParticleMetrics.T4LayoutHash
        && ValidCorridorTransitProgress.ProgressHash
          == Packet.ParticleMetrics.T4ProgressHash
        && SharedFlowField.BuildHash == Packet.ParticleMetrics.T4FlowHash);
    if (Packet.ParticleMetrics.T4LayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT4Checkpoint role=client round_id=%d valid=%d layout_hash=%u/%u flow_hash=%u/%u progress_hash=%u/%u wall_passed=%d corridor_exited=%d completed=%d final_settled=%d final_deadlock=%d unreachable_samples=%d completion_step_max=%d group_completion_step=%d group_settled_step=%d match=%d%s"),
        Packet.RoundId, Packet.ParticleMetrics.bT4Valid,
        ValidCorridorTransitLayout.LayoutHash, Packet.ParticleMetrics.T4LayoutHash,
        SharedFlowField.BuildHash, Packet.ParticleMetrics.T4FlowHash,
        ValidCorridorTransitProgress.ProgressHash,
        Packet.ParticleMetrics.T4ProgressHash,
        Packet.ParticleMetrics.T4WallPassedCount,
        Packet.ParticleMetrics.T4CorridorExitedCount,
        Packet.ParticleMetrics.T4CompletedCount,
        Packet.ParticleMetrics.T4FinalSettledCount,
        Packet.ParticleMetrics.T4FinalDeadlockAgentCount,
        Packet.ParticleMetrics.T4UnreachableSampleCount,
        Packet.ParticleMetrics.T4CompletionStepMax,
        Packet.ParticleMetrics.T4GroupCompletionStep,
        Packet.ParticleMetrics.T4GroupSettledStep,
        bT4HashMatch ? 1 : 0, bT4HashMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bT4HashMatch)
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoT4Checkpoint role=client hash_mismatch=1 VIOLATION"));
    }
    const bool bT6TransitHashMatch = Packet.ParticleMetrics.T6TransitLayoutHash == 0
      || (ValidCorridorTransitLayout.bValid
        && ValidCorridorTransitProgress.bValid
        && SharedFlowField.IsValid()
        && ValidCorridorTransitLayout.LayoutHash
          == Packet.ParticleMetrics.T6TransitLayoutHash
        && ValidCorridorTransitProgress.ProgressHash
          == Packet.ParticleMetrics.T6TransitProgressHash
        && SharedFlowField.BuildHash == Packet.ParticleMetrics.T6TransitFlowHash
        && CapabilityProfileSummary.bValid
        && CapabilityProfileSummary.MembershipHash
          == Packet.ParticleMetrics.CapabilityMembershipHash);
    if (Packet.ParticleMetrics.T6TransitLayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT6TransitCheckpoint role=client round_id=%d valid=%d layout_hash=%u/%u flow_hash=%u/%u progress_hash=%u/%u capability_profiles=%d membership_hash=%u/%u wall_passed=%d corridor_exited=%d completed=%d final_settled=%d final_deadlock=%d unreachable_samples=%d cross_profile_hard=%d cross_profile_swept=%d completion_step_max=%d group_completion_step=%d group_settled_step=%d match=%d%s"),
        Packet.RoundId, Packet.ParticleMetrics.bT6TransitValid,
        ValidCorridorTransitLayout.LayoutHash,
        Packet.ParticleMetrics.T6TransitLayoutHash,
        SharedFlowField.BuildHash, Packet.ParticleMetrics.T6TransitFlowHash,
        ValidCorridorTransitProgress.ProgressHash,
        Packet.ParticleMetrics.T6TransitProgressHash,
        Packet.ParticleMetrics.CapabilityProfileCount,
        CapabilityProfileSummary.MembershipHash,
        Packet.ParticleMetrics.CapabilityMembershipHash,
        Packet.ParticleMetrics.T6TransitWallPassedCount,
        Packet.ParticleMetrics.T6TransitCorridorExitedCount,
        Packet.ParticleMetrics.T6TransitCompletedCount,
        Packet.ParticleMetrics.T6TransitFinalSettledCount,
        Packet.ParticleMetrics.T6TransitFinalDeadlockAgentCount,
        Packet.ParticleMetrics.T6TransitUnreachableSampleCount,
        Packet.ParticleMetrics.CrossProfileHardViolationCount,
        Packet.ParticleMetrics.CrossProfileSweptViolationCount,
        Packet.ParticleMetrics.T6TransitCompletionStepMax,
        Packet.ParticleMetrics.T6TransitGroupCompletionStep,
        Packet.ParticleMetrics.T6TransitGroupSettledStep,
        bT6TransitHashMatch ? 1 : 0,
        bT6TransitHashMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bT6TransitHashMatch)
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoT6TransitCheckpoint role=client hash_mismatch=1 VIOLATION"));
    }
    const bool bTargetTransportEnabled =
      GetRules().TargetRegionTransportSettings.bEnabled != 0;
    if (IsTargetRegionPlanLifecycleDiagnosticEnabled())
      FinalizeTargetRegionPlanLifecycleDiagnostic();
    uint32 LocalTransportTopologyHash = TargetRegionTopologyRoundHash;
    uint32 LocalTransportDemandHash = TargetRegionDemandRoundHash;
    uint32 LocalTransportPlanHash = TargetRegionTransportRoundHash;
    uint32 LocalTransportGuidanceHash = TargetRegionGuidanceRoundHash;
    uint32 LocalTransportValidationHash = TargetRegionValidationRoundHash;
    bool bCapabilityProfilesMatch = true;
    if (bTargetTransportEnabled && GetRules().bEnableHeterogeneousProfiles != 0)
    {
      const auto Fold = [](const uint32 Hash, const uint32 Value)
      {
        return (Hash ^ Value) * 16777619u;
      };
      LocalTransportTopologyHash = 2166136261u;
      LocalTransportDemandHash = 2166136261u;
      LocalTransportPlanHash = 2166136261u;
      LocalTransportGuidanceHash = 2166136261u;
      LocalTransportValidationHash = 2166136261u;
      bCapabilityProfilesMatch = CapabilityProfileSummary.bValid
        && Packet.ParticleMetrics.bCapabilityProfilesValid != 0
        && CapabilityProfileSummary.MembershipHash
          == Packet.ParticleMetrics.CapabilityMembershipHash
        && TargetRegionCapabilityCohorts.Num()
          == Packet.ParticleMetrics.CapabilityProfiles.Num();
      for (int32 Index = 0;
        bCapabilityProfilesMatch && Index < TargetRegionCapabilityCohorts.Num(); ++Index)
      {
        const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime =
          TargetRegionCapabilityCohorts[Index];
        const FCrowdDemoCapabilityProfileMetrics& ServerProfile =
          Packet.ParticleMetrics.CapabilityProfiles[Index];
        const uint32 Key = Runtime.Cohort.CapabilityProfileKey;
        LocalTransportTopologyHash = Fold(Fold(LocalTransportTopologyHash, Key),
          Runtime.TopologyRoundHash);
        LocalTransportDemandHash = Fold(Fold(LocalTransportDemandHash, Key),
          Runtime.DemandRoundHash);
        LocalTransportPlanHash = Fold(Fold(LocalTransportPlanHash, Key),
          Runtime.TransportRoundHash);
        LocalTransportGuidanceHash = Fold(Fold(LocalTransportGuidanceHash, Key),
          Runtime.GuidanceRoundHash);
        LocalTransportValidationHash = Fold(Fold(LocalTransportValidationHash, Key),
          Runtime.ValidationRoundHash);
        int32 LocalFeasibleCoverageCount = 0;
        int32 LocalMaximumRegionPopulation = 0;
        for (const FCrowdDemoTargetDemandRegion& Region : Runtime.Demand.Regions)
        {
          LocalFeasibleCoverageCount += Region.bFeasible && Region.CurrentPopulation > 0 ? 1 : 0;
          LocalMaximumRegionPopulation = FMath::Max(
            LocalMaximumRegionPopulation, Region.CurrentPopulation);
        }
        const FVector2f TargetLocation(
          GetTargetFact().Location.X, GetTargetFact().Location.Y);
        const FVector2f TargetVelocity(
          GetTargetFact().Velocity.X, GetTargetFact().Velocity.Y);
        int32 LocalDistanceBandInsideCount = 0;
        int32 LocalBelowBandCount = 0;
        int32 LocalAboveBandCount = 0;
        float LocalOutsideBandErrorCmMax = 0.0f;
        float LocalOutsideBandProgressCmpsMin = 0.0f;
        float LocalOutsideBandProgressCmpsMax = 0.0f;
        bool bHasLocalOutsideProgress = false;
        for (const FCrowdDemoTargetRegionTransportAgent& Agent : Runtime.Agents)
        {
          const FVector2f Delta = Agent.Location - TargetLocation;
          const float Distance = Delta.Size();
          float ErrorCm = 0.0f;
          float ProgressCmps = 0.0f;
          if (Distance < Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm)
          {
            ++LocalBelowBandCount;
            ErrorCm = Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm - Distance;
            ProgressCmps = Distance > UE_SMALL_NUMBER
              ? FVector2f::DotProduct(Agent.Velocity - TargetVelocity, Delta / Distance)
              : 0.0f;
          }
          else if (Distance > Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm)
          {
            ++LocalAboveBandCount;
            ErrorCm = Distance - Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm;
            ProgressCmps = Distance > UE_SMALL_NUMBER
              ? -FVector2f::DotProduct(Agent.Velocity - TargetVelocity, Delta / Distance)
              : 0.0f;
          }
          else
          {
            ++LocalDistanceBandInsideCount;
            continue;
          }
          LocalOutsideBandErrorCmMax = FMath::Max(LocalOutsideBandErrorCmMax, ErrorCm);
          if (!bHasLocalOutsideProgress)
          {
            LocalOutsideBandProgressCmpsMin = ProgressCmps;
            LocalOutsideBandProgressCmpsMax = ProgressCmps;
            bHasLocalOutsideProgress = true;
          }
          else
          {
            LocalOutsideBandProgressCmpsMin = FMath::Min(
              LocalOutsideBandProgressCmpsMin, ProgressCmps);
            LocalOutsideBandProgressCmpsMax = FMath::Max(
              LocalOutsideBandProgressCmpsMax, ProgressCmps);
          }
        }
        bCapabilityProfilesMatch = ServerProfile.CapabilityProfileKey == Key
          && ServerProfile.DemandRegionPhaseOffset == Runtime.DemandRegionPhaseOffset
          && ServerProfile.AgentCount == Runtime.Cohort.AgentIds.Num()
          && ServerProfile.FeasibleRegionCount == Runtime.Demand.FeasibleRegionCount
          && ServerProfile.FeasibleRegionCoverageCount
            == LocalFeasibleCoverageCount
          && ServerProfile.InsideBandCount == Runtime.Demand.CurrentTerminalPopulation
          && ServerProfile.DistanceBandInsideCount == LocalDistanceBandInsideCount
          && ServerProfile.BelowBandCount == LocalBelowBandCount
          && ServerProfile.AboveBandCount == LocalAboveBandCount
          && FMath::IsNearlyEqual(
            ServerProfile.OutsideBandErrorCmMax, LocalOutsideBandErrorCmMax, 0.01f)
          && FMath::IsNearlyEqual(ServerProfile.OutsideBandProgressCmpsMin,
            LocalOutsideBandProgressCmpsMin, 0.01f)
          && FMath::IsNearlyEqual(ServerProfile.OutsideBandProgressCmpsMax,
            LocalOutsideBandProgressCmpsMax, 0.01f)
          && ServerProfile.RoutedAgentCount == Runtime.Plan.RoutedAgentCount
          && ServerProfile.UnroutedAgentCount == Runtime.Plan.UnroutedAgentCount
          && ServerProfile.MaximumRegionPopulation == LocalMaximumRegionPopulation
          && ServerProfile.TopologyHash == Runtime.TopologyRoundHash
          && ServerProfile.DemandHash == Runtime.DemandRoundHash
          && ServerProfile.TransportHash == Runtime.TransportRoundHash
          && ServerProfile.GuidanceHash == Runtime.GuidanceRoundHash
          && ServerProfile.ValidationHash == Runtime.ValidationRoundHash;
      }
    }
    const bool bTargetTransportHashMatch = !bTargetTransportEnabled
      || (Packet.ParticleMetrics.bTargetRegionTransportValid != 0
        && LocalTransportTopologyHash
          == Packet.ParticleMetrics.TargetTransportTopologyHash
        && LocalTransportDemandHash
          == Packet.ParticleMetrics.TargetTransportDemandHash
        && LocalTransportPlanHash
          == Packet.ParticleMetrics.TargetTransportPlanHash
        && LocalTransportGuidanceHash
          == Packet.ParticleMetrics.TargetTransportGuidanceHash
        && LocalTransportValidationHash
          == Packet.ParticleMetrics.TargetTransportValidationHash
        && bCapabilityProfilesMatch
        && (bTargetRegionFailureFixtureValid ? 1 : 0)
          == Packet.ParticleMetrics.bTargetTransportFailureFixtureValid
        && TargetRegionFailureFixtureHash
          == Packet.ParticleMetrics.TargetTransportFailureFixtureHash);
    const bool bTargetPlanLifecycleMatch =
      Packet.ParticleMetrics.bTargetPlanLifecycleDiagnosticValid == 0
      || (TargetRegionPlanLifecycleSummary.bValid
        && TargetRegionPlanLifecycleSummary.StableHash
          == Packet.ParticleMetrics.TargetPlanLifecycleHash
        && TargetRegionPlanLifecycleSummary.SampleBoundaryCount
          == Packet.ParticleMetrics.TargetPlanLifecycleSampleBoundaryCount
        && TargetRegionPlanLifecycleSummary.RebuildCount
          == Packet.ParticleMetrics.TargetTransportPlanRebuildCount
        && TargetRegionPlanLifecycleSummary.FixtureHash
          == Packet.ParticleMetrics.TargetPlanLifecycleFixtureHash);
    if (bTargetTransportEnabled)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTargetRegionTransportCheckpoint role=client round_id=%d valid=%d topology_hash=%u/%u demand_hash=%u/%u transport_hash=%u/%u guidance_hash=%u/%u validation_hash=%u/%u feasible_cells=%d feasible_regions=%d coverage=%d inside_band=%d max_region_population=%d plan_routed=%d plan_unrouted=%d guidance_unrouted_steps=%d guidance_unrouted_samples=%d guidance_unrouted_max=%d invalid_steps=%d validation_failures=%d epoch=%d rebuilds=%d solver_ms_p95=%.3f match=%d source=MassPipeline%s"),
        Packet.RoundId, Packet.ParticleMetrics.bTargetRegionTransportValid,
        LocalTransportTopologyHash, Packet.ParticleMetrics.TargetTransportTopologyHash,
        LocalTransportDemandHash, Packet.ParticleMetrics.TargetTransportDemandHash,
        LocalTransportPlanHash, Packet.ParticleMetrics.TargetTransportPlanHash,
        LocalTransportGuidanceHash, Packet.ParticleMetrics.TargetTransportGuidanceHash,
        LocalTransportValidationHash, Packet.ParticleMetrics.TargetTransportValidationHash,
        Packet.ParticleMetrics.TargetTransportFeasibleCellCount,
        Packet.ParticleMetrics.TargetTransportFeasibleRegionCount,
        Packet.ParticleMetrics.TargetTransportFeasibleRegionCoverageCount,
        Packet.ParticleMetrics.TargetTransportInsideEffectiveBandCount,
        Packet.ParticleMetrics.TargetTransportMaximumRegionPopulation,
        Packet.ParticleMetrics.TargetTransportRoutedAgentCount,
        Packet.ParticleMetrics.TargetTransportUnroutedAgentCount,
        Packet.ParticleMetrics.TargetGuidanceUnroutedStepCount,
        Packet.ParticleMetrics.TargetGuidanceUnroutedAgentSampleCount,
        Packet.ParticleMetrics.TargetGuidanceUnroutedAgentMax,
        Packet.ParticleMetrics.TargetTransportInvalidStepCount,
        Packet.ParticleMetrics.TargetTransportValidationFailureCount,
        Packet.ParticleMetrics.TargetTransportPlanEpoch,
        Packet.ParticleMetrics.TargetTransportPlanRebuildCount,
        Packet.ParticleMetrics.TargetTransportSolverMsP95,
        bTargetTransportHashMatch ? 1 : 0,
        bTargetTransportHashMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (GetRules().bEnableHeterogeneousProfiles != 0)
      {
        FString Profiles;
        for (int32 Index = 0; Index < Packet.ParticleMetrics.CapabilityProfiles.Num(); ++Index)
        {
          const FCrowdDemoCapabilityProfileMetrics& Profile =
            Packet.ParticleMetrics.CapabilityProfiles[Index];
          if (Index > 0) Profiles += TEXT(";");
          Profiles += FString::Printf(
            TEXT("%u:%d:%d:%d:%d:%d:%d:%d:%d:%.1f:%.1f:%.1f:%d:%d:%d:%u:%u:%u:%u:%u"),
            Profile.CapabilityProfileKey, Profile.DemandRegionPhaseOffset, Profile.AgentCount,
            Profile.FeasibleRegionCount, Profile.FeasibleRegionCoverageCount,
            Profile.InsideBandCount, Profile.DistanceBandInsideCount,
            Profile.BelowBandCount, Profile.AboveBandCount,
            Profile.OutsideBandErrorCmMax,
            Profile.OutsideBandProgressCmpsMin,
            Profile.OutsideBandProgressCmpsMax,
            Profile.RoutedAgentCount,
            Profile.UnroutedAgentCount, Profile.MaximumRegionPopulation,
            Profile.TopologyHash, Profile.DemandHash, Profile.TransportHash,
            Profile.GuidanceHash, Profile.ValidationHash);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoT6TargetCheckpoint role=client round_id=%d testcase=%d capability_profiles=%d membership_hash=%u/%u profiles=[%s] match=%d%s"),
          Packet.RoundId, static_cast<int32>(GetRules().SoftPressureTestCase),
          Packet.ParticleMetrics.CapabilityProfileCount,
          CapabilityProfileSummary.MembershipHash,
          Packet.ParticleMetrics.CapabilityMembershipHash, *Profiles,
          bTargetTransportHashMatch ? 1 : 0,
          bTargetTransportHashMatch ? TEXT("") : TEXT(" VIOLATION"));
      }
      if (Packet.ParticleMetrics.bTargetPlanLifecycleDiagnosticValid != 0)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetRegionPlanLifecycle role=client round_id=%d local_hash=%u server_hash=%u local_samples=%d server_samples=%d local_rebuilds=%d server_rebuilds=%d local_fixture=%u server_fixture=%u match=%d source=MassPipeline%s"),
          Packet.RoundId, TargetRegionPlanLifecycleSummary.StableHash,
          Packet.ParticleMetrics.TargetPlanLifecycleHash,
          TargetRegionPlanLifecycleSummary.SampleBoundaryCount,
          Packet.ParticleMetrics.TargetPlanLifecycleSampleBoundaryCount,
          TargetRegionPlanLifecycleSummary.RebuildCount,
          Packet.ParticleMetrics.TargetTransportPlanRebuildCount,
          TargetRegionPlanLifecycleSummary.FixtureHash,
          Packet.ParticleMetrics.TargetPlanLifecycleFixtureHash,
          bTargetPlanLifecycleMatch ? 1 : 0,
          bTargetPlanLifecycleMatch ? TEXT("") : TEXT(" VIOLATION"));
      }
    }
    const bool bCrossProfileViolationMatch = GetRules().bEnableHeterogeneousProfiles == 0
      || (CrossProfileHardViolationCount
          == Packet.ParticleMetrics.CrossProfileHardViolationCount
        && CrossProfileSweptViolationCount
          == Packet.ParticleMetrics.CrossProfileSweptViolationCount);
    const bool bParticleHashMatch = bCandidateHashMatch && bAppliedHashMatch
      && bGuidanceHashMatch
      && bLocalPredictiveHashMatch && bTargetTransportHashMatch
      && bTargetPlanLifecycleMatch
      && bCrossProfileViolationMatch && bRoundInitialStateMatch && bDynamicFlowMatch;
    LastCompareMetrics.ServerClientParticleHashMatch = bParticleHashMatch ? 1 : 0;
    if (!bAppliedHashMatch)
    {
      TArray<FCrowdDemoRoundAgentState> SortedServerStates(Packet.Agents);
      SortedServerStates.Sort([](const auto& A, const auto& B)
      {
        return A.AgentId < B.AgentId;
      });
      int32 FirstDifferentAgentId = INDEX_NONE;
      uint32 FirstDifferenceMask = 0;
      for (const FCrowdDemoRoundAgentState& ServerState : SortedServerStates)
      {
        const FCrowdDemoRoundAgentState* LocalState = ClientStates.FindByPredicate(
          [&](const FCrowdDemoRoundAgentState& Candidate)
          {
            return Candidate.AgentId == ServerState.AgentId;
          });
        const uint32 DifferenceMask = LocalState
          ? BuildAppliedStateDifferenceMask(*LocalState, ServerState)
          : MAX_uint32;
        if (DifferenceMask != 0)
        {
          FirstDifferentAgentId = ServerState.AgentId;
          FirstDifferenceMask = DifferenceMask;
          break;
        }
      }
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoAppliedStateHashDiagnostic role=client round_id=%d first_different_agent=%d difference_mask=%u mask_contract=[physical:1 lifecycle:2 business:4 attack:8 reactive:16 flash:32 visual:64] final_state_difference=%d source=MassPipeline"),
        Packet.RoundId, FirstDifferentAgentId, FirstDifferenceMask,
        FirstDifferentAgentId != INDEX_NONE ? 1 : 0);
      if (FirstDifferentAgentId != INDEX_NONE)
      {
        const FCrowdDemoRoundAgentState* LocalState = ClientStates.FindByPredicate(
          [&](const FCrowdDemoRoundAgentState& Candidate)
          {
            return Candidate.AgentId == FirstDifferentAgentId;
          });
        const FCrowdDemoRoundAgentState* ServerState = SortedServerStates.FindByPredicate(
          [&](const FCrowdDemoRoundAgentState& Candidate)
          {
            return Candidate.AgentId == FirstDifferentAgentId;
          });
        if (LocalState && ServerState)
        {
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoAppliedCombatDiagnostic role=client round_id=%d agent=%d business=[%d,%d revision=%d,%d enter=%d,%d] visual=[%d,%d revision=%d,%d seed=%u,%u] source=MassPipeline"),
            Packet.RoundId, FirstDifferentAgentId,
            static_cast<int32>(LocalState->Combat.BusinessState),
            static_cast<int32>(ServerState->Combat.BusinessState),
            LocalState->Combat.BusinessStateRevision,
            ServerState->Combat.BusinessStateRevision,
            LocalState->Combat.BusinessStateEnterFixedStep,
            ServerState->Combat.BusinessStateEnterFixedStep,
            static_cast<int32>(LocalState->Combat.VisualState),
            static_cast<int32>(ServerState->Combat.VisualState),
            LocalState->Combat.VisualRevision,
            ServerState->Combat.VisualRevision,
            LocalState->Combat.VisualPhaseSeed,
            ServerState->Combat.VisualPhaseSeed);
        }
      }
    }
    if (bParticleHashMatch)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoParticleStateHash role=client round_id=%d candidate_local=%u candidate_server=%u applied_local=%u applied_server=%u match=1 source=MassPipeline"),
        Packet.RoundId, ParticleCandidateStateHash,
        Packet.ParticleMetrics.ParticleCandidateHash, ParticleAppliedStateHash,
        Packet.ParticleMetrics.ParticleAppliedStateHash);
    }
    else
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoParticleStateHash role=client round_id=%d candidate_local=%u candidate_server=%u candidate_match=%d applied_local=%u applied_server=%u applied_match=%d match=0 source=MassPipeline VIOLATION"),
        Packet.RoundId, ParticleCandidateStateHash,
        Packet.ParticleMetrics.ParticleCandidateHash, bCandidateHashMatch ? 1 : 0,
        ParticleAppliedStateHash, Packet.ParticleMetrics.ParticleAppliedStateHash,
        bAppliedHashMatch ? 1 : 0);
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoGuidanceComposeHash role=client round_id=%d candidate=%u/%u composed=%u/%u samples=%d/%d match=%d source=MassPipeline%s"),
      Packet.RoundId, GuidanceCandidateRoundHash,
      Packet.ParticleMetrics.GuidanceCandidateHash, GuidanceComposeRoundHash,
      Packet.ParticleMetrics.GuidanceComposeHash, GuidanceComposeSampleCount,
      Packet.ParticleMetrics.GuidanceComposeSampleCount,
      bGuidanceHashMatch ? 1 : 0,
      bGuidanceHashMatch ? TEXT("") : TEXT(" VIOLATION"));
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoLocalPredictiveHash role=client round_id=%d local=%u server=%u samples=%d/%d invalid=%d/%d match=%d source=MassPipeline%s"),
      Packet.RoundId, LocalPredictiveRoundHash,
      Packet.ParticleMetrics.LocalPredictiveHash, LocalPredictiveSampleCount,
      Packet.ParticleMetrics.LocalPredictiveSampleCount,
      LocalPredictiveInvalidStepCount,
      Packet.ParticleMetrics.LocalPredictiveInvalidStepCount,
      bLocalPredictiveHashMatch ? 1 : 0,
      bLocalPredictiveHashMatch ? TEXT("") : TEXT(" VIOLATION"));
    if (IsRangedProjectileCombat()
      || Packet.ProjectileMetrics.ProjectileSpawnedCount > 0)
    {
      const FCrowdDemoProjectileMetrics LocalProjectile = BuildProjectileMetrics();
      const FCrowdDemoProjectileMetrics& ServerProjectile = Packet.ProjectileMetrics;
      const bool bProjectileMatch = LocalProjectile.bValid == ServerProjectile.bValid
        && LocalProjectile.TargetAcquiredCount == ServerProjectile.TargetAcquiredCount
        && LocalProjectile.CompletedWindupCount == ServerProjectile.CompletedWindupCount
        && LocalProjectile.ProjectileSpawnedCount == ServerProjectile.ProjectileSpawnedCount
        && LocalProjectile.ProjectileActiveCount == ServerProjectile.ProjectileActiveCount
        && LocalProjectile.ProjectileImpactedCount == ServerProjectile.ProjectileImpactedCount
        && LocalProjectile.ProjectileExpiredCount == ServerProjectile.ProjectileExpiredCount
        && LocalProjectile.DuplicateFireCount == ServerProjectile.DuplicateFireCount
        && LocalProjectile.DuplicateHitCount == ServerProjectile.DuplicateHitCount
        && LocalProjectile.DamageAppliedCount == ServerProjectile.DamageAppliedCount
        && LocalProjectile.AttackStateHash == ServerProjectile.AttackStateHash
        && LocalProjectile.ProjectileStateHash == ServerProjectile.ProjectileStateHash
        && LocalProjectile.EventHash == ServerProjectile.EventHash;
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoProjectileCheckpoint role=client round_id=%d valid=%d/%d acquired=%d/%d windup=%d/%d spawned=%d/%d active=%d/%d impacted=%d/%d expired=%d/%d damage=%d/%d attack_hash=%u/%u projectile_hash=%u/%u event_hash=%u/%u match=%d source=MassPipeline%s"),
        Packet.RoundId, LocalProjectile.bValid, ServerProjectile.bValid,
        LocalProjectile.TargetAcquiredCount, ServerProjectile.TargetAcquiredCount,
        LocalProjectile.CompletedWindupCount, ServerProjectile.CompletedWindupCount,
        LocalProjectile.ProjectileSpawnedCount, ServerProjectile.ProjectileSpawnedCount,
        LocalProjectile.ProjectileActiveCount, ServerProjectile.ProjectileActiveCount,
        LocalProjectile.ProjectileImpactedCount, ServerProjectile.ProjectileImpactedCount,
        LocalProjectile.ProjectileExpiredCount, ServerProjectile.ProjectileExpiredCount,
        LocalProjectile.DamageAppliedCount, ServerProjectile.DamageAppliedCount,
        LocalProjectile.AttackStateHash, ServerProjectile.AttackStateHash,
        LocalProjectile.ProjectileStateHash, ServerProjectile.ProjectileStateHash,
        LocalProjectile.EventHash, ServerProjectile.EventHash,
        bProjectileMatch ? 1 : 0,
        bProjectileMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bProjectileMatch)
      {
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoProjectileCheckpoint role=client round_id=%d match=0 VIOLATION"),
          Packet.RoundId);
      }
    }
    if (IsTargetStabilityDiagnosticEnabled())
    {
      FinalizeTargetStabilityDiagnostic();
      const auto& Server = Packet.ParticleMetrics;
      const bool bMatch = TargetStabilitySummary.bValid
        && Server.bTargetStabilityDiagnosticValid != 0
        && TargetStabilitySummary.StableHash == Server.TargetStabilityDiagnosticHash;
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTargetStabilityDiagnostic role=client round_id=%d valid=%d/%d cause=%d/%d samples=%d/%d merge_blocked=%d/%d chatter=%d/%d settled_windows=%d/%d speed_p95=%.3f/%.3f jitter_p95=%.3f/%.3f missing_regions=%d/%d first_missing=%u,%d,%d/%u,%d,%d gap_steps=%d,%d,%d,%d/%d,%d,%d,%d enter_exit=%d,%d/%d,%d sub_quantum_supply=%d,%d/%d,%d minimum_executable_cmps=%.3f/%.3f hash=%u/%u match=%d source=MassPipeline%s"),
        Packet.RoundId, TargetStabilitySummary.bValid ? 1 : 0,
        Server.bTargetStabilityDiagnosticValid,
        static_cast<int32>(TargetStabilitySummary.PrimaryCause),
        Server.TargetStabilityPrimaryCause,
        TargetStabilitySummary.SampleStepCount, Server.TargetStabilitySampleStepCount,
        TargetStabilitySummary.MergeBlockedAgentCount,
        Server.TargetStabilityMergeBlockedAgentCount,
        TargetStabilitySummary.TerminalChatterCount,
        Server.TargetStabilityTerminalChatterCount,
        TargetStabilitySummary.ParticleSettledWindowCount,
        Server.TargetStabilityParticleSettledWindowCount,
        TargetStabilitySummary.TargetRelativeSpeedCmpsP95,
        Server.TargetStabilityTargetRelativeSpeedCmpsP95,
        TargetStabilitySummary.PositionPeakToPeakCmP95,
        Server.TargetStabilityPositionPeakToPeakCmP95,
        TargetStabilitySummary.FinalMissingRegionCount,
        Server.TargetStabilityFinalMissingRegionCount,
        TargetStabilitySummary.FirstMissingCohortKey,
        TargetStabilitySummary.FirstMissingRegionKey,
        static_cast<int32>(TargetStabilitySummary.FirstMissingRegionStage),
        Server.TargetStabilityFirstMissingCohortKey,
        Server.TargetStabilityFirstMissingRegionKey,
        Server.TargetStabilityFirstMissingRegionStage,
        TargetStabilitySummary.RegionDemandGapStepCount,
        TargetStabilitySummary.RegionPlanQuotaGapStepCount,
        TargetStabilitySummary.RegionGuidanceGapStepCount,
        TargetStabilitySummary.RegionTerminalRetentionGapStepCount,
        Server.TargetStabilityRegionDemandGapStepCount,
        Server.TargetStabilityRegionPlanQuotaGapStepCount,
        Server.TargetStabilityRegionGuidanceGapStepCount,
        Server.TargetStabilityRegionTerminalRetentionGapStepCount,
        TargetStabilitySummary.RegionTerminalEnterCount,
        TargetStabilitySummary.RegionTerminalExitCount,
        Server.TargetStabilityRegionTerminalEnterCount,
        Server.TargetStabilityRegionTerminalExitCount,
        TargetStabilitySummary.FinalSubQuantumSupplyAgentCount,
        TargetStabilitySummary.FirstSubQuantumSupplyAgentId,
        Server.TargetStabilityFinalSubQuantumSupplyAgentCount,
        Server.TargetStabilityFirstSubQuantumSupplyAgentId,
        TargetStabilitySummary.MinimumExecutableSpeedCmps,
        Server.TargetStabilityMinimumExecutableSpeedCmps,
        TargetStabilitySummary.StableHash, Server.TargetStabilityDiagnosticHash,
        bMatch ? 1 : 0, bMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bMatch)
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoTargetStabilityDiagnostic role=client round_id=%d match=0 VIOLATION"),
          Packet.RoundId);
      const auto& Counterfactual = TargetStabilitySummary.Counterfactual;
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTargetRegionCounterfactual role=client round_id=%d valid=%d cohort=%u region=%d missing_steps=%d attachment_inflight_steps=%d attachment_recovered_guidance_steps=%d attachment_final_inflight=%d attachment_final_agent=%d attachment_remaining_edges=%d attachment_edges_min_max=%d,%d attachment_edge_transitions=%d,%d,%d attachment_final_relative_cmps=%.3f attachment_changes_final=%d terminal_hold_transitions=%d terminal_recovered_steps=%d terminal_final_held=%d terminal_cross_region_rejects=%d population_violations=%d terminal_restores_final=%d outcome=%d hash=%u target_stability_match=%d source=MassPipeline"),
        Packet.RoundId, Counterfactual.bValid ? 1 : 0,
        Counterfactual.CohortKey, Counterfactual.RegionKey,
        Counterfactual.BaselineMissingStepCount,
        Counterfactual.AttachmentObservedInFlightStepCount,
        Counterfactual.AttachmentRecoveredGuidanceStepCount,
        Counterfactual.AttachmentFinalInFlightAgentCount,
        Counterfactual.AttachmentFinalInFlightAgentId,
        Counterfactual.AttachmentFinalMinimumRemainingEdgeCount,
        Counterfactual.AttachmentRemainingEdgeCountMin,
        Counterfactual.AttachmentRemainingEdgeCountMax,
        Counterfactual.AttachmentRemainingEdgeDecreaseCount,
        Counterfactual.AttachmentRemainingEdgeIncreaseCount,
        Counterfactual.AttachmentRemainingEdgeUnchangedCount,
        Counterfactual.AttachmentFinalRelativeSpeedCmps,
        Counterfactual.bAttachmentChangesFinalGuidance ? 1 : 0,
        Counterfactual.TerminalEligibleHoldTransitionCount,
        Counterfactual.TerminalRecoveredCoverageStepCount,
        Counterfactual.TerminalFinalHeldAgentCount,
        Counterfactual.TerminalCrossRegionRejectCount,
        Counterfactual.PopulationConservationViolationCount,
        Counterfactual.bTerminalRestoresFinalObservedCoverage ? 1 : 0,
        static_cast<int32>(Counterfactual.Outcome),
        Counterfactual.StableHash, bMatch ? 1 : 0);

      TArray<int32> SupplyWitnessIds;
      for (const auto& Region : TargetStabilitySummary.FinalRegions)
        for (const int32 AgentId : Region.SupplyAgentIds)
          SupplyWitnessIds.AddUnique(AgentId);
      SupplyWitnessIds.Sort();
      FCrowdDemoLocalPredictiveComponentFixture Fixture;
      const bool bFixtureBuilt = !SupplyWitnessIds.IsEmpty()
        && BuildCurrentLocalPredictiveComponentFixture(SupplyWitnessIds, Fixture);
      const bool bServerFixtureValid =
        Server.bLocalPredictiveComponentFixtureValid != 0;
      const bool bBothFixturesAbsent = !bFixtureBuilt && !bServerFixtureValid
        && Server.LocalPredictiveComponentFixtureAgentCount == 0
        && Server.LocalPredictiveComponentFixtureWitnessCount == 0;
      const bool bFixtureMatch = bBothFixturesAbsent
        || (bFixtureBuilt && bServerFixtureValid
          && Fixture.StableHash == Server.LocalPredictiveComponentFixtureHash
          && Fixture.Agents.Num() == Server.LocalPredictiveComponentFixtureAgentCount
          && Fixture.WitnessAgentIds.Num()
            == Server.LocalPredictiveComponentFixtureWitnessCount);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoLocalPredictiveComponentFixture role=client round_id=%d valid=%d/%d agents=%d/%d witnesses=%d/%d hash=%u/%u match=%d source=MassPipeline%s"),
        Packet.RoundId, bFixtureBuilt ? 1 : 0,
        Server.bLocalPredictiveComponentFixtureValid,
        Fixture.Agents.Num(), Server.LocalPredictiveComponentFixtureAgentCount,
        Fixture.WitnessAgentIds.Num(),
        Server.LocalPredictiveComponentFixtureWitnessCount,
        Fixture.StableHash, Server.LocalPredictiveComponentFixtureHash,
        bFixtureMatch ? 1 : 0,
        bFixtureMatch ? TEXT("") : TEXT(" VIOLATION"));
      if (!bFixtureMatch)
      {
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoLocalPredictiveComponentFixture role=client round_id=%d match=0 VIOLATION"),
          Packet.RoundId);
      }
      else if (bFixtureBuilt)
      {
        LocalPredictiveComponentFixture = MoveTemp(Fixture);
      }
      else
      {
        LocalPredictiveComponentFixture = FCrowdDemoLocalPredictiveComponentFixture();
      }
    }
    if (IsSoftPressureRouteDiagnosticEnabled())
    {
      const auto& ServerRoute = Packet.ParticleMetrics.RouteMetrics;
      const bool bRouteHashMatch = SoftPressureRouteDiagnosticSummary.bValid
        && ServerRoute.bValid
        && SoftPressureRouteDiagnosticSummary.StableHash == ServerRoute.DiagnosticHash;
      if (bRouteHashMatch)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSoftPressureRouteHash role=client round_id=%d local_valid=1 server_valid=1 local=%u server=%u match=1 branch_local=%d branch_server=%d source=MassPipeline"),
          Packet.RoundId, SoftPressureRouteDiagnosticSummary.StableHash,
          ServerRoute.DiagnosticHash,
          static_cast<int32>(SoftPressureRouteDiagnosticSummary.SelectedBranch),
          ServerRoute.SelectedBranch);
      }
      else
      {
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoSoftPressureRouteHash role=client round_id=%d local_valid=%d server_valid=%d local=%u server=%u match=0 branch_local=%d branch_server=%d source=MassPipeline VIOLATION"),
          Packet.RoundId, SoftPressureRouteDiagnosticSummary.bValid ? 1 : 0,
          ServerRoute.bValid, SoftPressureRouteDiagnosticSummary.StableHash,
          ServerRoute.DiagnosticHash,
          static_cast<int32>(SoftPressureRouteDiagnosticSummary.SelectedBranch),
          ServerRoute.SelectedBranch);
      }
    }
    const uint32 LocalFixtureHash = ParticleFailureFixture.FixtureHash;
    const uint32 ServerFixtureHash = Packet.ParticleMetrics.ParticleFailureFixtureHash;
    const bool bFixtureHashMatch = LocalFixtureHash == ServerFixtureHash;
    if (bFixtureHashMatch)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoParticleFailureFixtureHash role=client round_id=%d local=%u server=%u match=1 step=%d pair=%d,%d source=MassPipeline"),
        Packet.RoundId, LocalFixtureHash, ServerFixtureHash,
        Packet.ParticleMetrics.ParticleFailureFixtureStep,
        Packet.ParticleMetrics.ParticleFailureMinAgentId,
        Packet.ParticleMetrics.ParticleFailureMaxAgentId);
    }
    else
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoParticleFailureFixtureHash role=client round_id=%d local=%u server=%u match=0 step=%d pair=%d,%d source=MassPipeline VIOLATION"),
        Packet.RoundId, LocalFixtureHash, ServerFixtureHash,
        Packet.ParticleMetrics.ParticleFailureFixtureStep,
        Packet.ParticleMetrics.ParticleFailureMinAgentId,
        Packet.ParticleMetrics.ParticleFailureMaxAgentId);
    }
    if (Packet.ParticleMetrics.ParticleInvalidStepCount > 0)
      StopAfterParticleConstraintFailure();
  }
  const FCrowdDemoCorrectionFrameMetrics PreviousCorrectionMetrics = LastCorrectionMetrics;
  const int32 PreviousAppliedCorrectionRevision = LastAppliedCorrectionRevision;
  FCrowdDemoCorrectionFrame Frame;
  Frame.AgentStates = Packet.Agents;
  Frame.CorrectionRevision = Packet.CheckpointRevision;
  RecordCorrectionComparisonAndApplied(ClientStates, Frame, Packet.EndServerTimeSeconds);
  LastCompareMetrics.RoundId = Packet.RoundId;
  LastCompareMetrics.Revision = Packet.Revision;
  LastCompareMetrics.CheckpointRevision = Packet.CheckpointRevision;
  LastCompareMetrics.SimPositionErrorCmP50 = LastCorrectionMetrics.CorrectionPositionErrorCmP50;
  LastCompareMetrics.SimPositionErrorCmP95 = LastCorrectionMetrics.CorrectionPositionErrorCmP95;
  LastCompareMetrics.SimPositionErrorCmMax = LastCorrectionMetrics.CorrectionPositionErrorCmMax;
  CrossRoundPositionErrorSeries.Record(LastCompareMetrics.SimPositionErrorCmP95);
  LastCompareMetrics.CrossRoundPositionErrorCmP95Max = CrossRoundPositionErrorSeries.GetMax();
  LastCompareMetrics.CrossRoundPositionErrorGrowthCm = CrossRoundPositionErrorSeries.GetExpansionFromFirst();
  CrossRoundCorrectionIntervalErrorSeries.Record(
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95);
  LastCompareMetrics.CrossRoundCorrectionIntervalErrorCmP95Max =
    CrossRoundCorrectionIntervalErrorSeries.GetMax();
  LastCompareMetrics.CrossRoundCorrectionIntervalErrorGrowthCm =
    CrossRoundCorrectionIntervalErrorSeries.GetExpansionFromFirst();
  LastCompareMetrics.SimYawErrorDegP95 = LastCorrectionMetrics.CorrectionYawErrorDegP95;
  LastCompareMetrics.SimVelocityErrorCmpsP95 = LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95;
  LastCompareMetrics.SimOverlapPairDelta = CountOverlapPairs(ClientStates, OverlapRadiusCm) - Packet.OverlapPairCount;
  LastCorrectionMetrics = PreviousCorrectionMetrics;
  LastAppliedCorrectionRevision = PreviousAppliedCorrectionRevision;
  ++LastCompareMetrics.CompletedRoundCount;
  ++LastCompareMetrics.CorrectionAppliedCount;
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundCheckpoint role=client round_id=%d revision=%d checkpoint_revision=%d completed_round_count=%d correction_applied_count=%d sim_position_error_cm_p50=%.3f sim_position_error_cm_p95=%.3f sim_position_error_cm_max=%.3f correction_interval_position_error_cm_p95=%.3f correction_interval_position_error_cm_max=%.3f cross_round_position_error_cm_p95_max=%.3f cross_round_position_error_growth_cm=%.3f cross_round_correction_interval_error_cm_p95_max=%.3f cross_round_correction_interval_error_growth_cm=%.3f sim_yaw_error_deg_p95=%.3f sim_velocity_error_cmps_p95=%.3f sim_overlap_pair_delta=%d source=MassPipeline"),
    Packet.RoundId,
    Packet.Revision,
    Packet.CheckpointRevision,
    LastCompareMetrics.CompletedRoundCount,
    LastCompareMetrics.CorrectionAppliedCount,
    LastCompareMetrics.SimPositionErrorCmP50,
    LastCompareMetrics.SimPositionErrorCmP95,
    LastCompareMetrics.SimPositionErrorCmMax,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmMax,
    LastCompareMetrics.CrossRoundPositionErrorCmP95Max,
    LastCompareMetrics.CrossRoundPositionErrorGrowthCm,
    LastCompareMetrics.CrossRoundCorrectionIntervalErrorCmP95Max,
    LastCompareMetrics.CrossRoundCorrectionIntervalErrorGrowthCm,
    LastCompareMetrics.SimYawErrorDegP95,
    LastCompareMetrics.SimVelocityErrorCmpsP95,
    LastCompareMetrics.SimOverlapPairDelta);
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoFlowCheckpoint role=client round_id=%d agents=%d flow_field_revision=%d flow_field_build_hash=%u flow_field_rebuild_count=%d flow_unreachable_agent_count=%d flow_goal_reached_count=%d flow_wall_pass_count=%d flow_corridor_exit_count=%d flow_turn_exit_count=%d corridor_deadlock_agent_count=%d client_sim_obstacle_penetration_count=%d sim_position_error_cm_p95=%.3f correction_interval_position_error_cm_p95=%.3f cross_round_position_error_growth_cm=%.3f source=MassPipeline"),
    Packet.RoundId,
    Packet.Agents.Num(),
    LastCompareMetrics.FlowFieldRevision,
    LastCompareMetrics.FlowFieldBuildHash,
    LastCompareMetrics.FlowFieldRebuildCount,
    LastCompareMetrics.FlowUnreachableAgentCount,
    LastCompareMetrics.FlowGoalReachedCount,
    LastCompareMetrics.FlowWallPassCount,
    LastCompareMetrics.FlowCorridorExitCount,
    LastCompareMetrics.FlowTurnExitCount,
    LastCompareMetrics.CorridorDeadlockAgentCount,
    LastCompareMetrics.ClientSimObstaclePenetrationCount,
    LastCompareMetrics.SimPositionErrorCmP95,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95,
    LastCompareMetrics.CrossRoundPositionErrorGrowthCm);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundInitialState(
  const uint32 InputHash,
  const uint32 InitialStateHash)
{
  RoundInputHash = InputHash;
  RoundInitialStateHash = InitialStateHash;
  ++RoundResetCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordNavigationDomainReprojectDelta(const float DeltaCm)
{
  LastCompareMetrics.SharedFlowMetrics.NavigationDomainReprojectDeltaCmMax = FMath::Max(
    LastCompareMetrics.SharedFlowMetrics.NavigationDomainReprojectDeltaCmMax, DeltaCm);
}

bool UCrowdDemoRoundSimPipelineSubsystem::ShouldBuildCorrectionFrame() const
{
  return bPlanActive && SimulatedServerTimeSeconds - LastCorrectionBuildServerTimeSeconds >= CorrectionIntervalSeconds;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ShouldBuildRoundResult() const
{
  const float BoundaryTime = bStepInProgress ? CurrentStepEndServerTimeSeconds : SimulatedServerTimeSeconds;
  return bPlanActive
    && LastBuiltResultRoundId < ActivePlan.RoundId
    && (bParticleConstraintRunFailure
      || BoundaryTime + KINDA_SMALL_NUMBER >= ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds);
}

void UCrowdDemoRoundSimPipelineSubsystem::EnqueueOutgoingCorrectionFrame(FCrowdDemoCorrectionFrame&& Frame)
{
  OutgoingCorrectionFrames.Add(MoveTemp(Frame));
}

void UCrowdDemoRoundSimPipelineSubsystem::EnqueueOutgoingRoundResult(FCrowdDemoRoundResultPacket&& Packet)
{
  OutgoingRoundResults.Add(MoveTemp(Packet));
}

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueOutgoingCorrectionFrame(FCrowdDemoCorrectionFrame& OutFrame)
{
  if (OutgoingCorrectionFrames.IsEmpty())
  {
    return false;
  }
  OutFrame = MoveTemp(OutgoingCorrectionFrames[0]);
  OutgoingCorrectionFrames.RemoveAt(0, 1, EAllowShrinking::No);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueOutgoingRoundResult(FCrowdDemoRoundResultPacket& OutPacket)
{
  if (OutgoingRoundResults.IsEmpty())
  {
    return false;
  }
  OutPacket = MoveTemp(OutgoingRoundResults[0]);
  OutgoingRoundResults.RemoveAt(0, 1, EAllowShrinking::No);
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkCorrectionFrameBuilt(const float ServerTimeSeconds)
{
  LastCorrectionBuildServerTimeSeconds = ServerTimeSeconds;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkRoundResultBuilt(const int32 CheckpointRevision)
{
  LastBuiltResultRoundId = ActivePlan.RoundId;
  LastCheckpointRevision = CheckpointRevision;
  ++LastCompareMetrics.CompletedRoundCount;
  LastCompareMetrics.CheckpointRevision = CheckpointRevision;
  LastCompletedRoundMetrics = LastCompareMetrics;
}

void UCrowdDemoRoundSimPipelineSubsystem::MergeNetworkCorrectionMetrics(
  const FCrowdDemoCorrectionFrameMetrics& NetworkMetrics)
{
  LastCorrectionMetrics.CorrectionFrameHeaderReceivedCount = NetworkMetrics.CorrectionFrameHeaderReceivedCount;
  LastCorrectionMetrics.CorrectionFrameChunkReceivedCount = NetworkMetrics.CorrectionFrameChunkReceivedCount;
  LastCorrectionMetrics.CorrectionFrameCompleteCount = NetworkMetrics.CorrectionFrameCompleteCount;
  LastCorrectionMetrics.CorrectionFramePublishedCount = NetworkMetrics.CorrectionFramePublishedCount;
  LastCorrectionMetrics.CorrectionFrameReceivedCount = NetworkMetrics.CorrectionFrameReceivedCount;
  LastCorrectionMetrics.CorrectionFrameRevisionGapCount = NetworkMetrics.CorrectionFrameRevisionGapCount;
  LastCorrectionMetrics.CorrectionFrameChunksPerFrame = NetworkMetrics.CorrectionFrameChunksPerFrame;
  LastCorrectionMetrics.CorrectionFrameChunkSize = NetworkMetrics.CorrectionFrameChunkSize;
  LastCorrectionMetrics.CorrectionIntervalMsP95 = NetworkMetrics.CorrectionIntervalMsP95;
  LastCorrectionMetrics.CorrectionFrameAssemblyMsP95 = NetworkMetrics.CorrectionFrameAssemblyMsP95;
}

void UCrowdDemoRoundSimPipelineSubsystem::LogStageOnce(const TCHAR* StageName, const int32 AgentCount)
{
  const FName StageKey(StageName);
  if (LoggedStages.Contains(StageKey))
  {
    return;
  }
  LoggedStages.Add(StageKey);
  const UWorld* World = GetWorld();
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundPipeline role=%s stage=%s order_revision=%d fixed_step=%.4f agents=%d source=MassProcessor"),
    World && World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
    StageName,
    GetCurrentPlanRevision(),
    CurrentFixedStepSeconds,
    AgentCount);
}

int32 UCrowdDemoRoundSimPipelineSubsystem::CountOverlapPairs(
  TConstArrayView<FCrowdDemoRoundAgentState> States,
  const float RadiusCm)
{
  int32 Count = 0;
  const float RadiusSquared = FMath::Square(RadiusCm);
  for (int32 A = 0; A < States.Num(); ++A)
  {
    for (int32 B = A + 1; B < States.Num(); ++B)
    {
      if (FVector::DistSquared2D(FVector(States[A].Location), FVector(States[B].Location)) < RadiusSquared)
      {
        ++Count;
      }
    }
  }
  return Count;
}

float UCrowdDemoRoundSimPipelineSubsystem::Percentile(TArray<float> Values, const float Quantile)
{
  if (Values.IsEmpty())
  {
    return -1.0f;
  }
  Values.Sort();
  const int32 Index = FMath::Clamp(FMath::CeilToInt(Values.Num() * Quantile) - 1, 0, Values.Num() - 1);
  return Values[Index];
}
