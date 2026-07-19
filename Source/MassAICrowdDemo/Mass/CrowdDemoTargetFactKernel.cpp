#include "Mass/CrowdDemoTargetFactKernel.h"

namespace CrowdDemoTargetFactPrivate
{
FVector2f QuantizeVector(const FVector2f& Value, const float Quantum)
{
  const float SafeQuantum = FMath::Max(Quantum, KINDA_SMALL_NUMBER);
  return FVector2f(
    static_cast<float>(FMath::RoundToInt(Value.X / SafeQuantum)) * SafeQuantum,
    static_cast<float>(FMath::RoundToInt(Value.Y / SafeQuantum)) * SafeQuantum);
}
}

FCrowdDemoTargetFact FCrowdDemoTargetFactKernel::BuildLinearMotionFact(
  const int32 TargetId,
  const int32 TargetRevision,
  const int32 MotionStep,
  const FVector2f& InitialLocation,
  const FVector2f& LinearVelocity,
  const float InitialYawDegrees,
  const float YawRateDegreesPerSecond,
  const float PhysicalRadiusCm,
  const float FixedStepSeconds,
  const float PositionQuantumCm,
  const float VelocityQuantumCmps)
{
  using namespace CrowdDemoTargetFactPrivate;
  FCrowdDemoTargetFact Target;
  Target.TargetId = TargetId;
  Target.TargetRevision = TargetRevision;
  Target.MotionStep = MotionStep;
  Target.PhysicalRadiusCm = PhysicalRadiusCm;
  const float SafeStepSeconds = FMath::Max(FixedStepSeconds, KINDA_SMALL_NUMBER);
  const float CurrentSeconds = static_cast<float>(MotionStep) * SafeStepSeconds;
  const float PreviousSeconds = static_cast<float>(MotionStep - 1) * SafeStepSeconds;
  Target.Location = QuantizeVector(InitialLocation + LinearVelocity * CurrentSeconds,
    PositionQuantumCm);
  const FVector2f PreviousLocation = QuantizeVector(
    InitialLocation + LinearVelocity * PreviousSeconds, PositionQuantumCm);
  Target.Velocity = QuantizeVector((Target.Location - PreviousLocation) / SafeStepSeconds,
    VelocityQuantumCmps);
  Target.YawDegrees = InitialYawDegrees + YawRateDegreesPerSecond * CurrentSeconds;
  return Target;
}

FCrowdDemoTargetFact FCrowdDemoTargetFactKernel::BuildReflectedLinearMotionFact(
  const int32 TargetId,
  const int32 TargetRevision,
  const int32 MotionStep,
  const FVector2f& InitialLocation,
  const FVector2f& LinearVelocity,
  const FVector2f& MotionBoundsMin,
  const FVector2f& MotionBoundsMax,
  const float InitialYawDegrees,
  const float YawRateDegreesPerSecond,
  const float PhysicalRadiusCm,
  const float FixedStepSeconds,
  const float PositionQuantumCm,
  const float VelocityQuantumCmps)
{
  using namespace CrowdDemoTargetFactPrivate;
  const auto ReflectAxis = [](const float Initial, const float Velocity,
    const float Seconds, const float Minimum, const float Maximum)
  {
    const float Extent = Maximum - Minimum;
    if (!FMath::IsFinite(Initial) || !FMath::IsFinite(Velocity)
      || !FMath::IsFinite(Seconds) || !FMath::IsFinite(Minimum)
      || !FMath::IsFinite(Maximum) || Extent <= KINDA_SMALL_NUMBER)
      return Initial + Velocity * Seconds;
    const float Period = 2.0f * Extent;
    float Phase = FMath::Fmod((Initial - Minimum) + Velocity * Seconds, Period);
    if (Phase < 0.0f) Phase += Period;
    return Minimum + (Phase <= Extent ? Phase : Period - Phase);
  };
  const auto ResolveLocation = [&](const float Seconds)
  {
    return FVector2f(
      ReflectAxis(InitialLocation.X, LinearVelocity.X, Seconds,
        MotionBoundsMin.X, MotionBoundsMax.X),
      ReflectAxis(InitialLocation.Y, LinearVelocity.Y, Seconds,
        MotionBoundsMin.Y, MotionBoundsMax.Y));
  };

  FCrowdDemoTargetFact Target;
  Target.TargetId = TargetId;
  Target.TargetRevision = TargetRevision;
  Target.MotionStep = MotionStep;
  Target.PhysicalRadiusCm = PhysicalRadiusCm;
  const float SafeStepSeconds = FMath::Max(FixedStepSeconds, KINDA_SMALL_NUMBER);
  const float CurrentSeconds = static_cast<float>(MotionStep) * SafeStepSeconds;
  const float PreviousSeconds = static_cast<float>(MotionStep - 1) * SafeStepSeconds;
  Target.Location = QuantizeVector(ResolveLocation(CurrentSeconds), PositionQuantumCm);
  const FVector2f PreviousLocation = QuantizeVector(
    ResolveLocation(PreviousSeconds), PositionQuantumCm);
  Target.Velocity = QuantizeVector((Target.Location - PreviousLocation) / SafeStepSeconds,
    VelocityQuantumCmps);
  Target.YawDegrees = InitialYawDegrees + YawRateDegreesPerSecond * CurrentSeconds;
  return Target;
}
