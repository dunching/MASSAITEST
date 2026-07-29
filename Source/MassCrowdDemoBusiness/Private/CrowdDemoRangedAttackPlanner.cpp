#include "CrowdDemoRangedAttackPlanner.h"

#include "CrowdDemoAttackPlanner.h"

namespace CrowdDemoRangedAttackPlannerPrivate
{
  ECrowdDemoAttackPlannerPhase ToGenericPhase(
    const ECrowdDemoRangedAttackPhase Phase)
  {
    switch (Phase)
    {
    case ECrowdDemoRangedAttackPhase::AcquireTarget:
      return ECrowdDemoAttackPlannerPhase::AcquireTarget;
    case ECrowdDemoRangedAttackPhase::Windup:
      return ECrowdDemoAttackPlannerPhase::Windup;
    case ECrowdDemoRangedAttackPhase::Fire:
      return ECrowdDemoAttackPlannerPhase::Commit;
    case ECrowdDemoRangedAttackPhase::Recovery:
      return ECrowdDemoAttackPlannerPhase::Recovery;
    case ECrowdDemoRangedAttackPhase::Cooldown:
      return ECrowdDemoAttackPlannerPhase::Cooldown;
    default:
      return ECrowdDemoAttackPlannerPhase::None;
    }
  }

  ECrowdDemoRangedAttackPhase ToLegacyPhase(
    const ECrowdDemoAttackPlannerPhase Phase)
  {
    switch (Phase)
    {
    case ECrowdDemoAttackPlannerPhase::AcquireTarget:
      return ECrowdDemoRangedAttackPhase::AcquireTarget;
    case ECrowdDemoAttackPlannerPhase::Windup:
      return ECrowdDemoRangedAttackPhase::Windup;
    case ECrowdDemoAttackPlannerPhase::Commit:
      return ECrowdDemoRangedAttackPhase::Fire;
    case ECrowdDemoAttackPlannerPhase::Recovery:
      return ECrowdDemoRangedAttackPhase::Recovery;
    case ECrowdDemoAttackPlannerPhase::Cooldown:
      return ECrowdDemoRangedAttackPhase::Cooldown;
    default:
      return ECrowdDemoRangedAttackPhase::None;
    }
  }

  const FCrowdDemoRangedAttackAgent* FindByAgentId(
    const TArray<FCrowdDemoRangedAttackAgent>& Agents,
    const int32 AgentId)
  {
    return Agents.FindByPredicate(
      [AgentId](const FCrowdDemoRangedAttackAgent& Agent)
      {
        return Agent.AgentId == AgentId;
      });
  }

  const FCrowdDemoRangedAttackAgent* FindByFormation(
    const TArray<FCrowdDemoRangedAttackAgent>& Agents,
    const int32 FormationIndex)
  {
    return Agents.FindByPredicate(
      [FormationIndex](const FCrowdDemoRangedAttackAgent& Agent)
      {
        return Agent.FormationIndex == FormationIndex;
      });
  }

  const FCrowdDemoRangedAttackAgent* FindByRef(
    const TArray<FCrowdDemoRangedAttackAgent>& Agents,
    const FCrowdStableEntityRef& Ref)
  {
    return Agents.FindByPredicate(
      [&Ref](const FCrowdDemoRangedAttackAgent& Agent)
      {
        return Agent.EntityRef == Ref;
      });
  }

  uint64 MakeLegacyProjectileId(
    const int32 RoundId,
    const int32 SourceAgentId,
    const int32 FireSequence)
  {
    return
      (static_cast<uint64>(
        static_cast<uint32>(RoundId) & 0xffffu) << 48)
      | (static_cast<uint64>(
        static_cast<uint32>(SourceAgentId) & 0xffffffu) << 24)
      | static_cast<uint64>(
        static_cast<uint32>(FireSequence) & 0xffffffu);
  }
}

using namespace CrowdDemoRangedAttackPlannerPrivate;

bool FCrowdDemoRangedAttackSettings::IsValid() const
{
  return ShooterCount > 0
    && WindupFixedSteps > 0
    && RecoveryFixedSteps >= 0
    && CooldownFixedSteps >= 0
    && ProjectileSpeedCmps > 0.0f
    && MuzzleForwardOffsetCm >= 0.0f
    && PositionQuantumCm > 0.0f
    && VelocityQuantumCmps > 0.0f;
}

bool FCrowdDemoRangedAttackAgent::IsValid() const
{
  return EntityRef.IsValid()
    && AgentId >= 0
    && LifecycleSerial > 0
    && StateLifecycleSerial > 0
    && FormationIndex >= 0
    && !Position.ContainsNaN()
    && !State.LockedTargetLocation.ContainsNaN()
    && State.BusinessStateRevision >= 0
    && State.FireSequence >= 0;
}

bool FCrowdDemoFireIntent::IsValid() const
{
  return ProjectileId != 0
    && FixedStepIndex >= 0
    && Instigator.IsValid()
    && Target.IsValid()
    && FireSequence > 0
    && !Position.ContainsNaN()
    && !Velocity.ContainsNaN()
    && !Velocity.IsNearlyZero();
}

bool FCrowdDemoRangedAttackPlanner::Advance(
  const int32 RoundId,
  const int32 FixedStepIndex,
  const FCrowdDemoRangedAttackSettings& Settings,
  TArray<FCrowdDemoRangedAttackAgent>& InOutAgents,
  TArray<FCrowdDemoFireIntent>& OutFireIntents,
  FCrowdDemoRangedAttackPlanSummary& OutSummary)
{
  OutFireIntents.Reset();
  OutSummary = {};
  if (!Settings.IsValid())
    return false;

  InOutAgents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < InOutAgents.Num(); ++Index)
  {
    if (!InOutAgents[Index].IsValid()
      || (Index > 0 && InOutAgents[Index - 1].AgentId
        == InOutAgents[Index].AgentId))
      return false;
  }

  FCrowdDemoAttackProfileV1 Profile;
  Profile.ProfileId = CrowdDemoAttackProfileIds::Ranged;
  Profile.PayloadTypeId = CrowdDemoAttackPayloadTypeIds::Ranged;
  Profile.EffectProfileId = 1;
  Profile.Archetype = ECrowdDemoAttackArchetype::Ranged;
  Profile.WindupFixedSteps = Settings.WindupFixedSteps;
  Profile.RecoveryFixedSteps = Settings.RecoveryFixedSteps;
  Profile.CooldownFixedSteps = Settings.CooldownFixedSteps;
  Profile.MinimumDistanceCm = 0.0f;
  Profile.MaximumDistanceCm = 1000000.0f;
  Profile.QueryRadiusCm = 12.0f;
  Profile.MuzzleForwardOffsetCm =
    Settings.MuzzleForwardOffsetCm;
  Profile.ProjectileSpeedCmps = Settings.ProjectileSpeedCmps;
  Profile.PositionQuantumCm = Settings.PositionQuantumCm;
  Profile.VelocityQuantumCmps = Settings.VelocityQuantumCmps;
  Profile.Damage = 20;

  TArray<FCrowdDemoAttackAgent> GenericAgents;
  GenericAgents.Reserve(InOutAgents.Num());
  for (const FCrowdDemoRangedAttackAgent& Source : InOutAgents)
  {
    FCrowdDemoAttackAgent& Target =
      GenericAgents.AddDefaulted_GetRef();
    Target.EntityRef = Source.EntityRef;
    Target.FactionId = Source.FactionId;
    Target.NavLayer = Source.NavLayer;
    Target.AttackProfileId = Profile.ProfileId;
    Target.Position = Source.Position;
    Target.Health = Source.bAlive && Source.bStateAlive ? 100 : 0;
    Target.bAlive = Source.bAlive && Source.bStateAlive;
    Target.bCanAttack =
      Source.FormationIndex < Settings.ShooterCount;
    Target.bRequirePreferredTarget = Target.bCanAttack;
    const FCrowdDemoRangedAttackAgent* Preferred = nullptr;
    if (Source.FormationIndex < Settings.ShooterCount)
    {
      Preferred = FindByFormation(
        InOutAgents,
        Settings.ShooterCount + Source.FormationIndex);
      if (Preferred)
        Target.PreferredTargetRef = Preferred->EntityRef;
    }
    const bool bHasLockedPhase =
      Source.State.AttackPhase
        != ECrowdDemoRangedAttackPhase::None
      && Source.State.AttackPhase
        != ECrowdDemoRangedAttackPhase::AcquireTarget;
    if (bHasLockedPhase
      && (!Preferred || !Preferred->bAlive
        || !Preferred->bStateAlive
        || Preferred->LifecycleSerial
          != Preferred->StateLifecycleSerial
        || Source.State.LockedTargetAgentId
          != Preferred->AgentId
        || Source.State.LockedTargetLifecycleSerial
          != Preferred->LifecycleSerial))
    {
      // Preserve the legacy T8 contract: a stale fixed-pair lock returns to
      // Acquire for this Boundary and cannot immediately reacquire/fire.
      Target.bCanAttack = false;
    }
    if (const FCrowdDemoRangedAttackAgent* ExistingTarget =
        FindByAgentId(InOutAgents, Source.State.TargetAgentId))
    {
      Target.State.TargetRef = ExistingTarget->EntityRef;
      Target.State.TargetRef.LifecycleSerial =
        static_cast<uint32>(
          Source.State.TargetLifecycleSerial);
    }
    if (const FCrowdDemoRangedAttackAgent* ExistingLocked =
        FindByAgentId(
          InOutAgents, Source.State.LockedTargetAgentId))
    {
      Target.State.LockedTargetRef = ExistingLocked->EntityRef;
      Target.State.LockedTargetRef.LifecycleSerial =
        static_cast<uint32>(
          Source.State.LockedTargetLifecycleSerial);
    }
    Target.State.Phase =
      ToGenericPhase(Source.State.AttackPhase);
    Target.State.PhaseEnterFixedStep =
      Source.State.AttackPhaseEnterFixedStep;
    Target.State.CooldownEndFixedStep =
      Source.State.CooldownEndFixedStep;
    Target.State.LockedTargetLocation =
      Source.State.LockedTargetLocation;
    Target.State.Revision =
      static_cast<uint32>(Source.State.BusinessStateRevision);
    Target.State.FireSequence =
      static_cast<uint32>(Source.State.FireSequence);
    Target.State.bCommitIssued =
      Source.State.bFireRequestIssued;
  }

  TArray<FCrowdDemoAttackIntent> GenericIntents;
  FCrowdDemoAttackPlanSummary GenericSummary;
  if (!FCrowdDemoAttackPlanner::Advance(
      RoundId, FixedStepIndex, MakeArrayView(&Profile, 1),
      GenericAgents, GenericIntents, GenericSummary))
    return false;

  for (FCrowdDemoRangedAttackAgent& Target : InOutAgents)
  {
    const FCrowdDemoAttackAgent* Source =
      GenericAgents.FindByPredicate(
        [&Target](const FCrowdDemoAttackAgent& Candidate)
        {
          return Candidate.EntityRef == Target.EntityRef;
        });
    if (!Source) return false;
    Target.State.AttackPhase = ToLegacyPhase(Source->State.Phase);
    Target.State.AttackPhaseEnterFixedStep =
      static_cast<int32>(Source->State.PhaseEnterFixedStep);
    Target.State.CooldownEndFixedStep =
      static_cast<int32>(Source->State.CooldownEndFixedStep);
    Target.State.LockedTargetLocation =
      Source->State.LockedTargetLocation;
    Target.State.FireSequence =
      static_cast<int32>(Source->State.FireSequence);
    Target.State.bFireRequestIssued =
      Source->State.bCommitIssued;
    Target.State.BusinessStateRevision =
      static_cast<int32>(Source->State.Revision);
    if (const FCrowdDemoRangedAttackAgent* StateTarget =
        FindByRef(InOutAgents, Source->State.TargetRef))
    {
      Target.State.TargetAgentId = StateTarget->AgentId;
      Target.State.TargetLifecycleSerial =
        StateTarget->LifecycleSerial;
    }
    else
    {
      Target.State.TargetAgentId = INDEX_NONE;
      Target.State.TargetLifecycleSerial = 0;
    }
    if (const FCrowdDemoRangedAttackAgent* LockedTarget =
        FindByRef(InOutAgents, Source->State.LockedTargetRef))
    {
      Target.State.LockedTargetAgentId = LockedTarget->AgentId;
      Target.State.LockedTargetLifecycleSerial =
        LockedTarget->LifecycleSerial;
    }
    else
    {
      Target.State.LockedTargetAgentId = INDEX_NONE;
      Target.State.LockedTargetLifecycleSerial = 0;
    }
    Target.State.BusinessState =
      Source->State.Phase == ECrowdDemoAttackPlannerPhase::None
        || Source->State.Phase
          == ECrowdDemoAttackPlannerPhase::AcquireTarget
      ? ECrowdDemoRangedBusinessState::Idle
      : ECrowdDemoRangedBusinessState::Attacking;
    Target.State.BusinessStateEnterFixedStep =
      Target.State.AttackPhaseEnterFixedStep;
  }

  for (const FCrowdDemoAttackIntent& Source : GenericIntents)
  {
    const FCrowdDemoRangedAttackAgent* Instigator =
      FindByRef(InOutAgents, Source.Instigator);
    if (!Instigator) return false;
    FCrowdDemoFireIntent& Target =
      OutFireIntents.AddDefaulted_GetRef();
    // T8's persisted hit ledger orders events by this monotonic identity.
    // The generic commit identity is intentionally opaque, so the legacy
    // ranged adapter must retain the versioned projectile identity contract.
    Target.ProjectileId = MakeLegacyProjectileId(
      RoundId, Instigator->AgentId,
      static_cast<int32>(Source.FireSequence));
    Target.FixedStepIndex = Source.FixedStepIndex;
    Target.Instigator = Source.Instigator;
    Target.Target = Source.Target;
    Target.FireSequence =
      static_cast<int32>(Source.FireSequence);
    Target.SourceFactionId = Source.SourceFactionId;
    Target.NavLayer = Source.NavLayer;
    Target.Position = Source.Position;
    Target.Velocity =
      Source.Direction * Source.ProjectileSpeedCmps;
    if (!Target.IsValid()) return false;
  }
  OutSummary.bValid = GenericSummary.bValid;
  OutSummary.TargetAcquiredCount =
    GenericSummary.TargetAcquiredCount;
  OutSummary.CompletedWindupCount =
    GenericSummary.CompletedWindupCount;
  OutSummary.InvalidTargetLifecycleCount =
    GenericSummary.InvalidTargetLifecycleCount;
  return OutSummary.bValid;
}
