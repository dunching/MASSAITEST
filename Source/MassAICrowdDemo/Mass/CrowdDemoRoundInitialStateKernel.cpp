#include "Mass/CrowdDemoRoundInitialStateKernel.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime;
    return Hash;
  }

  uint32 FoldQuantized(uint32 Hash, const float Value, const float Quantum)
  {
    return Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Value / Quantum)));
  }

  FVector MakeOffset(
    const int32 FormationIndex,
    const int32 AgentCount,
    const FCrowdDemoRoundRules& Rules)
  {
    if (Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Rules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat)
    {
      const int32 ShooterCount = FMath::Max(1, Rules.RangedCombatSettings.ShooterCount);
      const bool bShooter = FormationIndex < ShooterCount;
      const int32 LocalIndex = bShooter ? FormationIndex : FormationIndex - ShooterCount;
      return FVector(
        (static_cast<float>(LocalIndex) - static_cast<float>(ShooterCount - 1) * 0.5f)
          * Rules.FormationSpacingCm,
        bShooter ? -450.0f : 450.0f,
        0.0f);
    }
    const int32 Columns = Rules.FormationColumns > 0
      ? Rules.FormationColumns
      : FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(1, AgentCount))));
    const int32 Rows = FMath::Max(1, FMath::CeilToInt(
      static_cast<float>(AgentCount) / static_cast<float>(Columns)));
    return FVector(
      (static_cast<float>(FormationIndex % Columns) - static_cast<float>(Columns - 1) * 0.5f)
        * Rules.FormationSpacingCm,
      (static_cast<float>(FormationIndex / Columns) - static_cast<float>(Rows - 1) * 0.5f)
        * Rules.FormationSpacingCm,
      0.0f);
  }

  uint32 HashRules(const FCrowdDemoRoundRules& Rules)
  {
    uint32 Hash = FnvOffset;
    Hash = Fold(Hash, static_cast<uint32>(Rules.RoundStartPolicy));
    Hash = Fold(Hash, static_cast<uint32>(Rules.Scenario));
    Hash = Fold(Hash, static_cast<uint32>(Rules.SoftPressureTestCase));
    Hash = Fold(Hash, static_cast<uint32>(Rules.RandomSeed));
    Hash = FoldQuantized(Hash, Rules.FixedStepSeconds, 0.000001f);
    Hash = FoldQuantized(Hash, Rules.FormationSpacingCm, 0.01f);
    Hash = Fold(Hash, static_cast<uint32>(Rules.FormationColumns));
    Hash = FoldQuantized(Hash, Rules.SpawnOrigin.X, 0.1f);
    Hash = FoldQuantized(Hash, Rules.SpawnOrigin.Y, 0.1f);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.InitialLocation.X, 0.1f);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.InitialLocation.Y, 0.1f);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.LinearVelocity.X, 0.1f);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.LinearVelocity.Y, 0.1f);
    Hash = Fold(Hash, Rules.TargetMotion.bReflectAtMotionBounds);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.MotionBoundsMin.X, 0.1f);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.MotionBoundsMin.Y, 0.1f);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.MotionBoundsMax.X, 0.1f);
    Hash = FoldQuantized(Hash, Rules.TargetMotion.MotionBoundsMax.Y, 0.1f);
    return Hash;
  }
}

bool FCrowdDemoRoundInitialStateKernel::BuildGeneric(
  const TConstArrayView<FCrowdDemoRoundInitialStateAgent> Agents,
  const FCrowdDemoRoundRules& Rules,
  TArray<FCrowdDemoRoundInitialStateResult>& OutStates,
  FCrowdDemoRoundInitialStateSummary& OutSummary)
{
  OutStates.Reset();
  OutSummary = {};
  if (Agents.IsEmpty()) return false;

  TArray<FCrowdDemoRoundInitialStateAgent> Sorted;
  Sorted.Append(Agents.GetData(), Agents.Num());
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    if (Sorted[Index].AgentId < 0 || Sorted[Index].FormationIndex < 0
      || Sorted[Index].RadiusCm <= 0.0f
      || (Index > 0 && Sorted[Index - 1].AgentId == Sorted[Index].AgentId))
    {
      return false;
    }
  }

  uint32 InputHash = HashRules(Rules);
  OutStates.Reserve(Sorted.Num());
  for (const FCrowdDemoRoundInitialStateAgent& Agent : Sorted)
  {
    InputHash = Fold(InputHash, static_cast<uint32>(Agent.AgentId));
    InputHash = Fold(InputHash, static_cast<uint32>(Agent.LifecycleSerial));
    InputHash = Fold(InputHash, static_cast<uint32>(Agent.FormationIndex));
    InputHash = Fold(InputHash, static_cast<uint32>(Agent.CapabilityProfileKey));
    InputHash = FoldQuantized(InputHash, Agent.RadiusCm, 0.01f);

    FCrowdDemoRoundInitialStateResult& State = OutStates.AddDefaulted_GetRef();
    State.AgentId = Agent.AgentId;
    State.LifecycleSerial = Agent.LifecycleSerial;
    State.FormationIndex = Agent.FormationIndex;
    State.CapabilityProfileKey = Agent.CapabilityProfileKey;
    State.Location = FVector(Rules.SpawnOrigin) + MakeOffset(
      Agent.FormationIndex, Sorted.Num(), Rules);
    State.RadiusCm = Agent.RadiusCm;
    State.YawDegrees = Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
      && Agent.FormationIndex >= Rules.RangedCombatSettings.ShooterCount
      ? -90.0f : 90.0f;
  }
  OutSummary.InputHash = InputHash;
  OutSummary.InitialStateHash = HashStates(OutStates);
  OutSummary.bValid = true;
  return true;
}

uint32 FCrowdDemoRoundInitialStateKernel::HashStates(
  const TConstArrayView<FCrowdDemoRoundInitialStateResult> States)
{
  TArray<FCrowdDemoRoundInitialStateResult> Sorted;
  Sorted.Append(States.GetData(), States.Num());
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 Hash = FnvOffset;
  for (const auto& State : Sorted)
  {
    Hash = Fold(Hash, static_cast<uint32>(State.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(State.LifecycleSerial));
    Hash = Fold(Hash, static_cast<uint32>(State.FormationIndex));
    Hash = Fold(Hash, static_cast<uint32>(State.CapabilityProfileKey));
    Hash = FoldQuantized(Hash, State.Location.X, 0.1f);
    Hash = FoldQuantized(Hash, State.Location.Y, 0.1f);
    Hash = FoldQuantized(Hash, State.Location.Z, 0.1f);
    Hash = FoldQuantized(Hash, State.Velocity.X, 0.1f);
    Hash = FoldQuantized(Hash, State.Velocity.Y, 0.1f);
    Hash = FoldQuantized(Hash, State.YawDegrees, 0.01f);
    Hash = FoldQuantized(Hash, State.RadiusCm, 0.01f);
  }
  return Hash;
}
