#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

enum class ECrowdDemoT7ExpectedStage : uint8
{
  Idle,
  Move,
  Attack,
  IdleBeforeKnockback,
  Knockback,
  RecoveredFromKnockback,
  IdleBeforeKnockUp,
  KnockUp,
  LandingRecovery,
  RecoveredFromKnockUp,
  IdleBeforeDeath,
  Death
};

struct FCrowdDemoT7AcceptanceEvaluation
{
  bool bValid = false;
  bool bMatches = false;
  ECrowdDemoT7ExpectedStage ExpectedStage =
    ECrowdDemoT7ExpectedStage::Idle;
  FString ExpectedLabel;
  FString ActualLabel;
};

class MASSAICROWDDEMO_API FCrowdDemoT7AcceptanceOracle
{
public:
  static FCrowdDemoT7AcceptanceEvaluation Evaluate(
    int32 FormationIndex,
    int32 FixedStepIndex,
    const FCrowdDemoCombatNetState& Actual);
};
