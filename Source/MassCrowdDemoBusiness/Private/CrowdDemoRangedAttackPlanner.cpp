#include "CrowdDemoRangedAttackPlanner.h"

namespace CrowdDemoRangedAttackPlannerPrivate
{
  int32 QuantizeScalar(const float Value, const float Quantum)
  {
    return FMath::RoundToInt(
      Value / FMath::Max(Quantum, SMALL_NUMBER));
  }

  FVector QuantizeVector(
    const FVector& Value, const float Quantum)
  {
    return FVector(
      QuantizeScalar(Value.X, Quantum),
      QuantizeScalar(Value.Y, Quantum),
      QuantizeScalar(Value.Z, Quantum)) * Quantum;
  }

  uint64 MakeProjectileId(
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

  const FCrowdDemoRangedAttackAgent* FindTargetByFormation(
    const TArray<FCrowdDemoRangedAttackAgent>& Agents,
    const int32 FormationIndex)
  {
    return Agents.FindByPredicate(
      [FormationIndex](const FCrowdDemoRangedAttackAgent& Agent)
      {
        return Agent.FormationIndex == FormationIndex;
      });
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
  InOutAgents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  if (RoundId <= 0 || FixedStepIndex < 0
    || !Settings.IsValid())
    return false;
  for (int32 Index = 0; Index < InOutAgents.Num(); ++Index)
  {
    if (!InOutAgents[Index].IsValid()
      || (Index > 0
        && InOutAgents[Index - 1].AgentId
          == InOutAgents[Index].AgentId))
      return false;
  }

  for (FCrowdDemoRangedAttackAgent& Shooter : InOutAgents)
  {
    if (Shooter.FormationIndex >= Settings.ShooterCount
      || !Shooter.bAlive || !Shooter.bStateAlive)
      continue;
    const FCrowdDemoRangedAttackAgent* Target =
      FindTargetByFormation(
        InOutAgents,
        Settings.ShooterCount + Shooter.FormationIndex);
    const bool bTargetValid =
      Target && Target->bAlive && Target->bStateAlive
      && Target->LifecycleSerial
        == Target->StateLifecycleSerial;
    if (Shooter.State.AttackPhase
        == ECrowdDemoRangedAttackPhase::None
      || Shooter.State.AttackPhase
        == ECrowdDemoRangedAttackPhase::AcquireTarget)
    {
      if (!bTargetValid)
      {
        const bool bHadTargetIdentity =
          Shooter.State.LockedTargetAgentId != INDEX_NONE
          || Shooter.State.TargetAgentId != INDEX_NONE;
        Shooter.State.AttackPhase =
          ECrowdDemoRangedAttackPhase::AcquireTarget;
        Shooter.State.LockedTargetAgentId = INDEX_NONE;
        Shooter.State.LockedTargetLifecycleSerial = 0;
        Shooter.State.TargetAgentId = INDEX_NONE;
        Shooter.State.TargetLifecycleSerial = 0;
        OutSummary.InvalidTargetLifecycleCount +=
          bHadTargetIdentity ? 1 : 0;
        continue;
      }
      Shooter.State.BusinessState =
        ECrowdDemoRangedBusinessState::Attacking;
      Shooter.State.TargetAgentId = Target->AgentId;
      Shooter.State.TargetLifecycleSerial =
        Target->LifecycleSerial;
      Shooter.State.LockedTargetAgentId = Target->AgentId;
      Shooter.State.LockedTargetLifecycleSerial =
        Target->LifecycleSerial;
      Shooter.State.LockedTargetLocation = Target->Position;
      Shooter.State.AttackPhase =
        ECrowdDemoRangedAttackPhase::Windup;
      Shooter.State.AttackPhaseEnterFixedStep = FixedStepIndex;
      Shooter.State.bFireRequestIssued = false;
      ++Shooter.State.BusinessStateRevision;
      Shooter.State.BusinessStateEnterFixedStep = FixedStepIndex;
      ++OutSummary.TargetAcquiredCount;
      continue;
    }

    const bool bLockedTargetValid =
      bTargetValid
      && Target->AgentId
        == Shooter.State.LockedTargetAgentId
      && Target->LifecycleSerial
        == Shooter.State.LockedTargetLifecycleSerial;
    if (!bLockedTargetValid)
    {
      Shooter.State.AttackPhase =
        ECrowdDemoRangedAttackPhase::AcquireTarget;
      Shooter.State.AttackPhaseEnterFixedStep = FixedStepIndex;
      Shooter.State.LockedTargetAgentId = INDEX_NONE;
      Shooter.State.LockedTargetLifecycleSerial = 0;
      Shooter.State.TargetAgentId = INDEX_NONE;
      Shooter.State.TargetLifecycleSerial = 0;
      Shooter.State.bFireRequestIssued = false;
      ++OutSummary.InvalidTargetLifecycleCount;
      continue;
    }

    switch (Shooter.State.AttackPhase)
    {
    case ECrowdDemoRangedAttackPhase::Windup:
      if (!Shooter.State.bFireRequestIssued
        && FixedStepIndex
          - Shooter.State.AttackPhaseEnterFixedStep
          >= Settings.WindupFixedSteps)
      {
        const FVector Direction =
          (Shooter.State.LockedTargetLocation
            - Shooter.Position).GetSafeNormal();
        if (Direction.IsNearlyZero())
          return false;
        ++Shooter.State.FireSequence;
        Shooter.State.bFireRequestIssued = true;
        Shooter.State.AttackPhase =
          ECrowdDemoRangedAttackPhase::Fire;
        Shooter.State.AttackPhaseEnterFixedStep = FixedStepIndex;
        FCrowdDemoFireIntent& Intent =
          OutFireIntents.AddDefaulted_GetRef();
        Intent.ProjectileId = MakeProjectileId(
          RoundId, Shooter.AgentId, Shooter.State.FireSequence);
        Intent.FixedStepIndex = FixedStepIndex;
        Intent.Instigator = Shooter.EntityRef;
        Intent.Target = Target->EntityRef;
        Intent.FireSequence = Shooter.State.FireSequence;
        Intent.SourceFactionId = Shooter.FactionId;
        Intent.NavLayer = Shooter.NavLayer;
        Intent.Position = QuantizeVector(
          Shooter.Position
            + Direction * Settings.MuzzleForwardOffsetCm,
          Settings.PositionQuantumCm);
        Intent.Velocity = QuantizeVector(
          Direction * Settings.ProjectileSpeedCmps,
          Settings.VelocityQuantumCmps);
        if (!Intent.IsValid()) return false;
        ++OutSummary.CompletedWindupCount;
      }
      break;
    case ECrowdDemoRangedAttackPhase::Fire:
      Shooter.State.AttackPhase =
        ECrowdDemoRangedAttackPhase::Recovery;
      Shooter.State.AttackPhaseEnterFixedStep = FixedStepIndex;
      break;
    case ECrowdDemoRangedAttackPhase::Recovery:
      if (FixedStepIndex
        - Shooter.State.AttackPhaseEnterFixedStep
        >= Settings.RecoveryFixedSteps)
      {
        Shooter.State.AttackPhase =
          ECrowdDemoRangedAttackPhase::Cooldown;
        Shooter.State.AttackPhaseEnterFixedStep =
          FixedStepIndex;
        Shooter.State.CooldownEndFixedStep =
          FixedStepIndex + Settings.CooldownFixedSteps;
      }
      break;
    case ECrowdDemoRangedAttackPhase::Cooldown:
      if (FixedStepIndex
        >= Shooter.State.CooldownEndFixedStep)
      {
        Shooter.State.AttackPhase =
          ECrowdDemoRangedAttackPhase::AcquireTarget;
        Shooter.State.AttackPhaseEnterFixedStep =
          FixedStepIndex;
        Shooter.State.bFireRequestIssued = false;
      }
      break;
    default:
      break;
    }
  }
  OutFireIntents.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.Instigator != B.Instigator)
      return A.Instigator < B.Instigator;
    return A.FireSequence < B.FireSequence;
  });
  OutSummary.bValid = true;
  return true;
}
