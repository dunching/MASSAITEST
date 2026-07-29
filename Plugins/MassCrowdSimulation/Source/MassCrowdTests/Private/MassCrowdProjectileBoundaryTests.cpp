#include "Misc/AutomationTest.h"
#include "MassCrowdProjectileBoundary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdProjectileProfile MakeProjectileProfile(
    const int32 Capacity = 8)
  {
    FCrowdProjectileProfile Profile;
    Profile.ProfileId = 1;
    Profile.RadiusCm = 10.0f;
    Profile.LifetimeFixedSteps = 2;
    Profile.MaxActiveProjectiles = Capacity;
    Profile.PositionQuantumCm = 1.0f;
    Profile.VelocityQuantumCmps = 1.0f;
    Profile.GridCellSizeCm = 128.0f;
    Profile.RecalculateStableHash();
    return Profile;
  }

  FCrowdProjectileSpawnRequest MakeProjectileRequest(
    const uint64 ProjectileId)
  {
    FCrowdProjectileSpawnRequest Request;
    Request.ProjectileId = ProjectileId;
    Request.FixedStepIndex = 1;
    Request.Instigator = {1, 1, 1};
    Request.Target = {1, 2, 1};
    Request.FireSequence = static_cast<uint32>(ProjectileId);
    Request.ProjectileProfileId = 1;
    Request.CollisionProfileId = 1;
    Request.EffectProfileId = 1;
    Request.Position = FVector::ZeroVector;
    Request.Velocity = FVector(6000.0, 0.0, 0.0);
    Request.RecalculateStableHash();
    return Request;
  }

  FCrowdProjectileTargetSnapshot MakeProjectileTarget()
  {
    FCrowdProjectileTargetSnapshot Target;
    Target.EntityRef = {1, 2, 1};
    Target.FactionId = 2;
    Target.PreviousPosition = FVector(500.0, 0.0, 0.0);
    Target.Position = Target.PreviousPosition;
    Target.RadiusCm = 40.0f;
    Target.bAlive = true;
    Target.RecalculateStableHash();
    return Target;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdProjectilePreparedBoundaryTest,
  "MassCrowd.Projectiles.PreparedBoundaryAtomicity",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdProjectilePreparedBoundaryTest::RunTest(
  const FString& Parameters)
{
  FCrowdProjectileBoundaryInput Input;
  Input.FixedStepIndex = 1;
  Input.ServerTimeSeconds = 1.0f;
  Input.FixedStepSeconds = 0.1f;
  Input.Profiles = {MakeProjectileProfile()};
  Input.SpawnRequests = {MakeProjectileRequest(10)};
  Input.Targets = {MakeProjectileTarget()};
  FCrowdPreparedProjectileBoundary Prepared;
  TestTrue(TEXT("boundary prepares"),
    FCrowdProjectileBoundaryPipeline::Prepare(Input, Prepared));
  TestTrue(TEXT("prepared boundary validates"),
    FCrowdProjectileBoundaryPipeline::ValidatePrepared(
      Input, Prepared));
  TestEqual(TEXT("one impact"), Prepared.Impacts.Num(), 1);
  TestEqual(TEXT("one active state retires"),
    Prepared.Summary.ActiveCount, 0);
  TestEqual(TEXT("spawn and impact events"),
    Prepared.Events.Num(), 2);

  FCrowdPreparedProjectileBoundary Tampered = Prepared;
  Tampered.States[0].Position.X += 1.0;
  TestFalse(TEXT("tampered prepared state is rejected"),
    FCrowdProjectileBoundaryPipeline::ValidatePrepared(
      Input, Tampered));

  FCrowdProjectileBoundaryInput CapacityInput = Input;
  CapacityInput.Profiles = {MakeProjectileProfile(1)};
  CapacityInput.SpawnRequests = {
    MakeProjectileRequest(10), MakeProjectileRequest(11)};
  FCrowdPreparedProjectileBoundary CapacityPrepared;
  TestFalse(TEXT("capacity overflow rejects whole boundary"),
    FCrowdProjectileBoundaryPipeline::Prepare(
      CapacityInput, CapacityPrepared));
  TestEqual(TEXT("failed boundary publishes no state"),
    CapacityPrepared.States.Num(), 0);

  FCrowdProjectileBoundaryInput UntargetedInput = Input;
  UntargetedInput.SpawnRequests = {MakeProjectileRequest(12)};
  UntargetedInput.SpawnRequests[0].Target = {};
  UntargetedInput.SpawnRequests[0].RecalculateStableHash();
  UntargetedInput.Targets.Reset();
  FCrowdPreparedProjectileBoundary UntargetedPrepared;
  TestTrue(TEXT("untargeted projectile is a valid public use case"),
    FCrowdProjectileBoundaryPipeline::Prepare(
      UntargetedInput, UntargetedPrepared));
  TestTrue(TEXT("untargeted projectile remains authoritative"),
    UntargetedPrepared.States.Num() == 1
      && UntargetedPrepared.States[0].bActive
      && UntargetedPrepared.States[0].Target.IsUnset());
  return true;
}

#endif
