#include "Mass/CrowdDemoFacingKernel.h"

namespace
{
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const int32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= static_cast<uint8>((static_cast<uint32>(Value) >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  float QuantizeAngle(const float Angle, const float Quantum)
  {
    return FMath::UnwindDegrees(
      FMath::RoundToFloat(FMath::UnwindDegrees(Angle) / Quantum) * Quantum);
  }
}

void FCrowdDemoFacingKernel::Resolve(
  const TConstArrayView<FCrowdDemoFacingInput> InputView,
  const FCrowdDemoFacingSettings& Settings,
  FCrowdDemoFacingSummary& OutSummary)
{
  OutSummary = {};
  if (!FMath::IsFinite(Settings.FixedStepSeconds)
    || !FMath::IsFinite(Settings.MaximumTurnRateDegreesPerSecond)
    || !FMath::IsFinite(Settings.AutonomousSpeedEpsilonCmps)
    || !FMath::IsFinite(Settings.AngleQuantumDegrees)
    || Settings.FixedStepSeconds <= 0.0f
    || Settings.MaximumTurnRateDegreesPerSecond < 0.0f
    || Settings.AutonomousSpeedEpsilonCmps < 0.0f
    || Settings.AngleQuantumDegrees <= 0.0f)
    return;

  TArray<FCrowdDemoFacingInput> Inputs(InputView);
  Inputs.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  int32 PreviousAgentId = INDEX_NONE;
  const float MaximumStepDegrees =
    Settings.MaximumTurnRateDegreesPerSecond * Settings.FixedStepSeconds;
  uint32 Hash = Fold(2166136261u, 1);
  for (const FCrowdDemoFacingInput& Input : Inputs)
  {
    if (Input.AgentId == INDEX_NONE || Input.AgentId <= PreviousAgentId
      || !FMath::IsFinite(Input.CurrentYawDegrees)
      || !FMath::IsFinite(Input.AutonomousPreferredVelocity.X)
      || !FMath::IsFinite(Input.AutonomousPreferredVelocity.Y)
      || !FMath::IsFinite(Input.Location.X) || !FMath::IsFinite(Input.Location.Y)
      || !FMath::IsFinite(Input.TargetLocation.X)
      || !FMath::IsFinite(Input.TargetLocation.Y))
      return;
    PreviousAgentId = Input.AgentId;
    FCrowdDemoFacingResult& Result = OutSummary.Results.AddDefaulted_GetRef();
    Result.AgentId = Input.AgentId;
    FVector2f Direction = FVector2f::ZeroVector;
    if (Input.bFinalPositionSettled && Input.bHasTarget)
    {
      Direction = Input.TargetLocation - Input.Location;
      Result.bFacingTarget = !Direction.IsNearlyZero();
    }
    if (!Result.bFacingTarget
      && Input.AutonomousPreferredVelocity.SizeSquared()
        >= FMath::Square(Settings.AutonomousSpeedEpsilonCmps))
      Direction = Input.AutonomousPreferredVelocity;
    if (Direction.IsNearlyZero())
    {
      Result.DesiredYawDegrees = FMath::UnwindDegrees(Input.CurrentYawDegrees);
      Result.ResolvedYawDegrees = QuantizeAngle(
        Result.DesiredYawDegrees, Settings.AngleQuantumDegrees);
      Result.bHeldCurrentYaw = true;
      ++OutSummary.HeldYawAgentCount;
    }
    else
    {
      Result.DesiredYawDegrees = FMath::RadiansToDegrees(
        FMath::Atan2(Direction.Y, Direction.X));
      const float Delta = FMath::FindDeltaAngleDegrees(
        Input.CurrentYawDegrees, Result.DesiredYawDegrees);
      Result.AppliedYawDeltaDegrees = FMath::Clamp(
        Delta, -MaximumStepDegrees, MaximumStepDegrees);
      Result.ResolvedYawDegrees = QuantizeAngle(
        Input.CurrentYawDegrees + Result.AppliedYawDeltaDegrees,
        Settings.AngleQuantumDegrees);
      if (Result.bFacingTarget) ++OutSummary.TargetFacingAgentCount;
      else ++OutSummary.AutonomousFacingAgentCount;
    }
    OutSummary.MaximumAppliedYawDeltaDegrees = FMath::Max(
      OutSummary.MaximumAppliedYawDeltaDegrees,
      FMath::Abs(Result.AppliedYawDeltaDegrees));
    Hash = Fold(Hash, Result.AgentId);
    Hash = Fold(Hash, FMath::RoundToInt(Result.DesiredYawDegrees
      / Settings.AngleQuantumDegrees));
    Hash = Fold(Hash, FMath::RoundToInt(Result.ResolvedYawDegrees
      / Settings.AngleQuantumDegrees));
    Hash = Fold(Hash, Result.bFacingTarget ? 1 : 0);
    Hash = Fold(Hash, Result.bHeldCurrentYaw ? 1 : 0);
  }
  OutSummary.StableHash = Hash;
  OutSummary.bValid = OutSummary.Results.Num() == Inputs.Num();
}
