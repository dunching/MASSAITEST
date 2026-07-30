#include "Mass/CrowdDemoT7AcceptanceOracle.h"

namespace
{
  const TCHAR* BusinessCode(const ECrowdDemoBusinessState State)
  {
    switch (State)
    {
      case ECrowdDemoBusinessState::Idle: return TEXT("Idle");
      case ECrowdDemoBusinessState::Moving: return TEXT("Move");
      case ECrowdDemoBusinessState::Attacking: return TEXT("Attack");
      case ECrowdDemoBusinessState::HitReact: return TEXT("Hit");
      case ECrowdDemoBusinessState::Dead: return TEXT("Dead");
      default: return TEXT("?");
    }
  }

  const TCHAR* AttackCode(const ECrowdDemoAttackPhase Phase)
  {
    switch (Phase)
    {
      case ECrowdDemoAttackPhase::None: return TEXT("-");
      case ECrowdDemoAttackPhase::AcquireTarget: return TEXT("Acquire");
      case ECrowdDemoAttackPhase::Windup: return TEXT("Windup");
      case ECrowdDemoAttackPhase::Fire: return TEXT("Fire");
      case ECrowdDemoAttackPhase::Recovery: return TEXT("Recovery");
      case ECrowdDemoAttackPhase::Cooldown: return TEXT("Cooldown");
      default: return TEXT("?");
    }
  }

  const TCHAR* ReactiveCode(const ECrowdDemoReactiveMotionMode Mode)
  {
    switch (Mode)
    {
      case ECrowdDemoReactiveMotionMode::None: return TEXT("-");
      case ECrowdDemoReactiveMotionMode::Knockback: return TEXT("KB");
      case ECrowdDemoReactiveMotionMode::KnockUp: return TEXT("KU");
      case ECrowdDemoReactiveMotionMode::LandingRecovery: return TEXT("Land");
      default: return TEXT("?");
    }
  }

  const TCHAR* VisualCode(const ECrowdDemoVisualState State)
  {
    switch (State)
    {
      case ECrowdDemoVisualState::Idle: return TEXT("Idle");
      case ECrowdDemoVisualState::Move: return TEXT("Move");
      case ECrowdDemoVisualState::Attack: return TEXT("Attack");
      case ECrowdDemoVisualState::HitReact: return TEXT("Hit");
      case ECrowdDemoVisualState::Death: return TEXT("Death");
      default: return TEXT("?");
    }
  }

  bool IsIdle(const FCrowdDemoCombatNetState& Actual)
  {
    return Actual.bAlive != 0
      && Actual.BusinessState == ECrowdDemoBusinessState::Idle
      && Actual.ReactiveMode == ECrowdDemoReactiveMotionMode::None
      && Actual.VisualState == ECrowdDemoVisualState::Idle;
  }
}

FCrowdDemoT7AcceptanceEvaluation FCrowdDemoT7AcceptanceOracle::Evaluate(
  const int32 FormationIndex,
  const int32 FixedStepIndex,
  const FCrowdDemoCombatNetState& Actual)
{
  FCrowdDemoT7AcceptanceEvaluation Result;
  if (FormationIndex < 0 || FormationIndex >= 20 || FixedStepIndex < 0)
  {
    return Result;
  }

  Result.ActualLabel = FString::Printf(
    TEXT("A:%s/%s/%s/%s H%.0f ap%d ld%d"),
    BusinessCode(Actual.BusinessState),
    AttackCode(Actual.AttackPhase),
    ReactiveCode(Actual.ReactiveMode),
    VisualCode(Actual.VisualState),
    Actual.Health,
    Actual.ApexCount,
    Actual.LandingCount);

  if (FormationIndex < 4)
  {
    Result.ExpectedStage = ECrowdDemoT7ExpectedStage::Idle;
    Result.ExpectedLabel = TEXT("E:Idle");
    Result.bMatches = IsIdle(Actual);
  }
  else if (FormationIndex < 8)
  {
    Result.ExpectedStage = ECrowdDemoT7ExpectedStage::Move;
    Result.ExpectedLabel = TEXT("E:Move");
    Result.bMatches = Actual.bAlive != 0
      && Actual.BusinessState == ECrowdDemoBusinessState::Moving
      && Actual.ReactiveMode == ECrowdDemoReactiveMotionMode::None
      && Actual.VisualState == ECrowdDemoVisualState::Move;
  }
  else if (FormationIndex < 12)
  {
    Result.ExpectedStage = ECrowdDemoT7ExpectedStage::Attack;
    Result.ExpectedLabel = TEXT("E:Attack");
    Result.bMatches = Actual.bAlive != 0
      && Actual.BusinessState == ECrowdDemoBusinessState::Attacking
      && Actual.ReactiveMode == ECrowdDemoReactiveMotionMode::None
      && Actual.VisualState == ECrowdDemoVisualState::Attack;
  }
  else if (FormationIndex < 14)
  {
    if (FixedStepIndex < 30)
    {
      Result.ExpectedStage =
        ECrowdDemoT7ExpectedStage::IdleBeforeKnockback;
      Result.ExpectedLabel = TEXT("E:Idle -> KB@30");
      Result.bMatches = IsIdle(Actual);
    }
    else if (FixedStepIndex < 45)
    {
      Result.ExpectedStage = ECrowdDemoT7ExpectedStage::Knockback;
      Result.ExpectedLabel = TEXT("E:Hit/Knockback");
      Result.bMatches = Actual.bAlive != 0
        && Actual.BusinessState == ECrowdDemoBusinessState::HitReact
        && Actual.ReactiveMode == ECrowdDemoReactiveMotionMode::Knockback
        && Actual.VisualState == ECrowdDemoVisualState::HitReact;
    }
    else
    {
      Result.ExpectedStage =
        ECrowdDemoT7ExpectedStage::RecoveredFromKnockback;
      Result.ExpectedLabel = TEXT("E:Recovered/Idle");
      Result.bMatches = IsIdle(Actual);
    }
  }
  else if (FormationIndex < 16)
  {
    if (FixedStepIndex < 60)
    {
      Result.ExpectedStage =
        ECrowdDemoT7ExpectedStage::IdleBeforeKnockUp;
      Result.ExpectedLabel = TEXT("E:Idle -> KU@60");
      Result.bMatches = IsIdle(Actual);
    }
    else if (FixedStepIndex < 98)
    {
      Result.ExpectedStage = ECrowdDemoT7ExpectedStage::KnockUp;
      Result.ExpectedLabel = TEXT("E:Hit/KnockUp");
      const int32 ExpectedApexCount = FixedStepIndex >= 79 ? 1 : 0;
      Result.bMatches = Actual.bAlive != 0
        && Actual.BusinessState == ECrowdDemoBusinessState::HitReact
        && Actual.ReactiveMode == ECrowdDemoReactiveMotionMode::KnockUp
        && Actual.VisualState == ECrowdDemoVisualState::HitReact
        && Actual.ApexCount == ExpectedApexCount
        && Actual.LandingCount == 0;
    }
    else if (FixedStepIndex < 104)
    {
      Result.ExpectedStage = ECrowdDemoT7ExpectedStage::LandingRecovery;
      Result.ExpectedLabel = TEXT("E:Hit/LandingRecovery");
      Result.bMatches = Actual.bAlive != 0
        && Actual.BusinessState == ECrowdDemoBusinessState::HitReact
        && Actual.ReactiveMode
          == ECrowdDemoReactiveMotionMode::LandingRecovery
        && Actual.VisualState == ECrowdDemoVisualState::HitReact
        && Actual.ApexCount == 1
        && Actual.LandingCount == 1;
    }
    else
    {
      Result.ExpectedStage =
        ECrowdDemoT7ExpectedStage::RecoveredFromKnockUp;
      Result.ExpectedLabel = TEXT("E:Recovered/Idle");
      Result.bMatches = IsIdle(Actual)
        && Actual.ApexCount == 1
        && Actual.LandingCount == 1;
    }
  }
  else if (FixedStepIndex < 90)
  {
    Result.ExpectedStage = ECrowdDemoT7ExpectedStage::IdleBeforeDeath;
    Result.ExpectedLabel = TEXT("E:Idle -> Death@90");
    Result.bMatches = IsIdle(Actual);
  }
  else
  {
    Result.ExpectedStage = ECrowdDemoT7ExpectedStage::Death;
    Result.ExpectedLabel = TEXT("E:Dead/Death");
    Result.bMatches = Actual.bAlive == 0
      && Actual.Health <= 0.0f
      && Actual.BusinessState == ECrowdDemoBusinessState::Dead
      && Actual.ReactiveMode == ECrowdDemoReactiveMotionMode::None
      && Actual.VisualState == ECrowdDemoVisualState::Death;
  }

  Result.bValid = true;
  return Result;
}
