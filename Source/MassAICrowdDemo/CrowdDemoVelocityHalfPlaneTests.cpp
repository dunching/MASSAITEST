#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoDeterministicOrcaKernel.h"
#include "Mass/CrowdDemoVelocityHalfPlaneKernel.h"

namespace
{
FCrowdDemoVelocityHalfPlaneInput MakeGenericInput(
  const FVector2f Preferred,
  const float MaxSpeed,
  const TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
  const FCrowdDemoOrcaSettings& Settings)
{
  FCrowdDemoVelocityHalfPlaneInput Input;
  Input.PreferredVelocity = Preferred;
  Input.Settings.MaxSpeedCmps = MaxSpeed;
  Input.Settings.BehaviorEpsilonCmps = Settings.ConstraintEpsilonCmps;
  Input.Settings.VelocityQuantumCmps = Settings.VelocityQuantumCmps;
  for (const FCrowdDemoOrcaConstraint& Constraint : Constraints)
  {
    Input.HalfPlanes.Add({
      Constraint.Point, Constraint.Normal, Constraint.StableConstraintOrder});
  }
  return Input;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoVelocityHalfPlaneParityTest,
  "CrowdDemo.SF.LocalPredictive.HalfPlaneParity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoVelocityHalfPlaneParityTest::RunTest(const FString& Parameters)
{
  FCrowdDemoOrcaSettings Settings;
  Settings.ConstraintEpsilonCmps = 0.1f;
  Settings.VelocityQuantumCmps = 1.0f;
  const float MaxSpeed = 800.0f;

  const auto Compare = [&](const TCHAR* Label, const FVector2f Preferred,
    const TArray<FCrowdDemoOrcaConstraint>& Constraints,
    const bool bExpectedContinuous,
    const bool bExpectedQuantized)
  {
    const auto LegacyInput = FCrowdDemoDeterministicOrcaKernel::MakeContinuousSolveInput(
      Preferred, MaxSpeed, Constraints, Settings.ConstraintEpsilonCmps);
    const auto LegacyResult = FCrowdDemoDeterministicOrcaKernel::SolveContinuousExact(LegacyInput);
    const bool bLegacyContinuous =
      LegacyResult.Status == ECrowdDemoOrcaSolveStatus::PreferredFeasible
      || LegacyResult.Status == ECrowdDemoOrcaSolveStatus::ExactFeasible;

    const FCrowdDemoVelocityHalfPlaneInput GenericInput = MakeGenericInput(
      Preferred, MaxSpeed, Constraints, Settings);
    const auto GenericResult = FCrowdDemoVelocityHalfPlaneKernel::Solve(GenericInput);
    const bool bGenericContinuous = GenericResult.bContinuousValid;
    TestEqual(FString::Printf(TEXT("%s legacy expected feasibility"), Label),
      bLegacyContinuous, bExpectedContinuous);
    TestEqual(FString::Printf(TEXT("%s generic expected feasibility"), Label),
      bGenericContinuous, bExpectedContinuous);
    TestEqual(FString::Printf(TEXT("%s legacy/generic feasibility parity"), Label),
      bGenericContinuous, bLegacyContinuous);
    if (!bLegacyContinuous || !bGenericContinuous) return;

    TestTrue(FString::Printf(TEXT("%s continuous velocity parity"), Label),
      GenericResult.ContinuousVelocity.Equals(LegacyResult.Velocity, 0.001f));
    TestTrue(FString::Printf(TEXT("%s generic continuous validates"), Label),
      FCrowdDemoVelocityHalfPlaneKernel::ValidateVelocity(
        GenericInput, GenericResult.ContinuousVelocity));

    FVector2f LegacyQuantized = FVector2f::ZeroVector;
    const auto LegacyQuantization =
      FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
        LegacyResult.Velocity, Preferred, MaxSpeed, Constraints, Settings, LegacyQuantized);
    const bool bLegacyQuantized =
      LegacyQuantization != ECrowdDemoOrcaQuantizationResult::NoSolution;
    TestEqual(FString::Printf(TEXT("%s legacy expected quantization"), Label),
      bLegacyQuantized, bExpectedQuantized);
    TestEqual(FString::Printf(TEXT("%s generic expected quantization"), Label),
      GenericResult.bQuantizedValid, bExpectedQuantized);
    TestEqual(FString::Printf(TEXT("%s legacy/generic quantization parity"), Label),
      GenericResult.bQuantizedValid, bLegacyQuantized);
    if (bLegacyQuantized && GenericResult.bQuantizedValid)
    {
      TestTrue(FString::Printf(TEXT("%s quantized velocity parity"), Label),
        GenericResult.QuantizedVelocity.Equals(LegacyQuantized, 0.001f));
    }
  };

  Compare(TEXT("no constraints"), FVector2f(300, -100), {}, true, true);

  TArray<FCrowdDemoOrcaConstraint> Direction;
  Direction.Add({1, FVector2f(100, 0), FVector2f(1, 0), 0.5f,
    84, 200, 1.25f, ECrowdDemoOrcaConstraintKind::LeftLeg, 0});
  Compare(TEXT("single direction"), FVector2f::ZeroVector, Direction, true, true);

  TArray<FCrowdDemoOrcaConstraint> Parallel;
  Parallel.Add({1, FVector2f::ZeroVector,
    FVector2f(0.989176510f, -0.146730474f), 0.5f,
    84, 200, 1.25f, ECrowdDemoOrcaConstraintKind::LeftLeg, 0});
  Parallel.Add({2, FVector2f(-58.692189782f, -395.670603986f),
    FVector2f(0.989176510f, -0.146730474f), 0.5f,
    84, 200, 1.25f, ECrowdDemoOrcaConstraintKind::LeftLeg, 1});
  Compare(TEXT("parallel production regression"), FVector2f(-400, 400),
    Parallel, true, true);
  Algo::Reverse(Parallel);
  Compare(TEXT("parallel reversed"), FVector2f(-400, 400), Parallel, true, true);

  TArray<FCrowdDemoOrcaConstraint> Contradiction;
  Contradiction.Add({1, FVector2f(1.1f, 0), FVector2f(1, 0), 0.5f,
    84, 200, 1.25f, ECrowdDemoOrcaConstraintKind::LeftLeg, 0});
  Contradiction.Add({2, FVector2f(-1.1f, 0), FVector2f(-1, 0), 0.5f,
    84, 200, 1.25f, ECrowdDemoOrcaConstraintKind::RightLeg, 1});
  Compare(TEXT("true contradiction"), FVector2f::ZeroVector,
    Contradiction, false, false);

  TArray<FCrowdDemoOrcaConstraint> QuantizedStrip;
  QuantizedStrip.Add({1, FVector2f(0.45f, 0), FVector2f(1, 0), 0.5f,
    84, 200, 1.25f, ECrowdDemoOrcaConstraintKind::LeftLeg, 0});
  QuantizedStrip.Add({2, FVector2f(0.55f, 0), FVector2f(-1, 0), 0.5f,
    84, 200, 1.25f, ECrowdDemoOrcaConstraintKind::RightLeg, 1});
  Compare(TEXT("continuous only strip"), FVector2f(-10, 0),
    QuantizedStrip, true, false);

  FCrowdDemoVelocityHalfPlaneInput Invalid;
  Invalid.Settings.MaxSpeedCmps = MaxSpeed;
  Invalid.HalfPlanes.Add({FVector2f::ZeroVector, FVector2f::ZeroVector, 0});
  TestEqual(TEXT("zero normal is invalid"),
    FCrowdDemoVelocityHalfPlaneKernel::Solve(Invalid).Status,
    ECrowdDemoVelocityHalfPlaneSolveStatus::InvalidInput);
  return true;
}

#endif
