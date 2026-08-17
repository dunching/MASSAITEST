#include "CrowdDemoVatShowcasePlanner.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(
        (Value >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  int32 Quantize(const float Value)
  {
    return FMath::RoundToInt(Value);
  }
}

ECrowdDemoVatPlannedState
FCrowdDemoVatShowcasePlanner::ResolveInitialState(
  const int32 FormationIndex)
{
  if (FormationIndex >= 8 && FormationIndex < 12)
    return ECrowdDemoVatPlannedState::Attacking;
  if (FormationIndex >= 4 && FormationIndex < 8)
    return ECrowdDemoVatPlannedState::Moving;
  return ECrowdDemoVatPlannedState::Idle;
}

ECrowdDemoVatInjectedHit
FCrowdDemoVatShowcasePlanner::SelectInjectedHit(
  const int32 FormationIndex,
  const int32 FixedStepIndex)
{
  if (FixedStepIndex == 30
    && FormationIndex >= 12 && FormationIndex < 14)
    return ECrowdDemoVatInjectedHit::Knockback;
  if (FixedStepIndex == 60
    && FormationIndex >= 14 && FormationIndex < 16)
    return ECrowdDemoVatInjectedHit::KnockUp;
  if (FixedStepIndex == 90 && FormationIndex >= 16)
    return ECrowdDemoVatInjectedHit::Death;
  return ECrowdDemoVatInjectedHit::None;
}

FCrowdDemoVatMotionDecision
FCrowdDemoVatShowcasePlanner::BuildMotion(
  const int32 FormationIndex,
  const int32 FixedStepIndex,
  const FVector& CurrentLocation,
  const FVector& AnchorLocation,
  const FCrowdDemoVatMotionSettings& Settings)
{
  FCrowdDemoVatMotionDecision Result;
  Result.DesiredLocation = CurrentLocation;
  const bool bFinite = !CurrentLocation.ContainsNaN()
    && !AnchorLocation.ContainsNaN()
    && FMath::IsFinite(Settings.MoveSpeedCmps)
    && FMath::IsFinite(Settings.MaximumAnchorOffsetCm);
  if (!bFinite || FormationIndex < 0 || FixedStepIndex < 0
    || Settings.FirstMovingFormationIndex < 0
    || Settings.MovingAgentCount < 0
    || Settings.HalfCycleFixedSteps <= 0
    || Settings.MoveSpeedCmps < 0.0f
    || Settings.MaximumAnchorOffsetCm < 0.0f)
    return Result;
  Result.bMovingGroup =
    FormationIndex >= Settings.FirstMovingFormationIndex
    && FormationIndex
      < Settings.FirstMovingFormationIndex
        + Settings.MovingAgentCount;
  if (Result.bMovingGroup && Settings.MoveSpeedCmps > 0.0f
    && Settings.MaximumAnchorOffsetCm > 0.0f)
  {
    const int32 HalfCycle =
      FixedStepIndex / Settings.HalfCycleFixedSteps;
    float Direction = (HalfCycle & 1) == 0 ? 1.0f : -1.0f;
    const float OffsetX =
      CurrentLocation.X - AnchorLocation.X;
    if (OffsetX >= Settings.MaximumAnchorOffsetCm
      && Direction > 0.0f)
      Direction = -1.0f;
    else if (OffsetX <= -Settings.MaximumAnchorOffsetCm
      && Direction < 0.0f)
      Direction = 1.0f;
    Result.DesiredVelocity =
      FVector(Direction * Settings.MoveSpeedCmps, 0.0f, 0.0f);
    Result.DesiredLocation = AnchorLocation
      + FVector(
        Direction * Settings.MaximumAnchorOffsetCm,
        0.0f, 0.0f);
  }
  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = Fold(Hash, static_cast<uint64>(FormationIndex));
  Hash = Fold(Hash, static_cast<uint64>(FixedStepIndex));
  Hash = Fold(Hash, static_cast<uint64>(
    Quantize(CurrentLocation.X)));
  Hash = Fold(Hash, static_cast<uint64>(
    Quantize(CurrentLocation.Y)));
  Hash = Fold(Hash, static_cast<uint64>(
    Quantize(AnchorLocation.X)));
  Hash = Fold(Hash, static_cast<uint64>(
    Quantize(AnchorLocation.Y)));
  Hash = Fold(Hash, Result.bMovingGroup ? 1u : 0u);
  Hash = Fold(Hash, static_cast<uint64>(
    Quantize(Result.DesiredVelocity.X)));
  Hash = Fold(Hash, static_cast<uint64>(
    Quantize(Result.DesiredVelocity.Y)));
  Result.StableHash = Hash;
  Result.bValid = true;
  return Result;
}
