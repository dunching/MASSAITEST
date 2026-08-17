#include "Misc/AutomationTest.h"
#include "MassCrowdCombatResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  struct FCombatTestPayload
  {
    int32 DamageQ = 0;
  };

  FCrowdImpactFact MakeCombatImpact(
    const uint64 ProjectileId,
    const FCrowdStableEntityRef& Target)
  {
    FCrowdImpactFact Impact;
    Impact.ImpactId = ProjectileId;
    Impact.ImpactTypeId = CrowdImpactTypeIds::Projectile;
    Impact.FixedStepIndex = 7;
    Impact.Instigator = {1, 1, 1};
    Impact.Target = Target;
    Impact.Position = FVector(100.0, 0.0, 0.0);
    Impact.Normal = FVector(-1.0, 0.0, 0.0);
    Impact.CollisionProfileId = 1;
    Impact.EffectProfileId = 4;
    Impact.TimeOfImpactQ = 500000;
    Impact.RecalculateStableHash();
    return Impact;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdCombatPureResolverTest,
  "MassCrowd.Combat.PureResolverAndPreparedCommit",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdCombatPureResolverTest::RunTest(
  const FString& Parameters)
{
  FCrowdEffectProfile Profile;
  Profile.EffectProfileId = 4;
  Profile.PayloadTypeId = 8;
  const FCombatTestPayload Payload{2500};
  TestTrue(TEXT("payload fits"),
    Profile.Payload.Set(9, Payload));
  Profile.RecalculateStableHash();
  TestTrue(TEXT("profile validates"), Profile.IsValid());

  const FCrowdImpactFact TargetImpact =
    MakeCombatImpact(11, {1, 2, 1});
  const FCrowdImpactFact EnvironmentImpact =
    MakeCombatImpact(12, {});
  FCrowdHitResolveResult Result;
  TestTrue(TEXT("pure resolver accepts target and environment"),
    FCrowdCombatResolver::Resolve(
      {TargetImpact, EnvironmentImpact}, {Profile}, Result));
  TestEqual(TEXT("one target hit"), Result.Hits.Num(), 1);
  TestEqual(TEXT("environment does not damage"),
    Result.EnvironmentImpactCount, 1);
  TestTrue(TEXT("result hash validates"), Result.IsValid());

  FCrowdPreparedHostHitCommit Commit;
  Commit.FixedStepIndex = Result.FixedStepIndex;
  Commit.SourceResolveHash = Result.StableHash;
  Commit.Hits = Result.Hits;
  Commit.RecalculateStableHash();
  TestTrue(TEXT("prepared commit validates"), Commit.IsValid());
  Commit.Hits[0].PayloadTypeId = 99;
  TestFalse(TEXT("tampered prepared commit is rejected"),
    Commit.IsValid());

  FCrowdHitResolveResult DuplicateResult;
  TestFalse(TEXT("duplicate impact is rejected"),
    FCrowdCombatResolver::Resolve(
      {TargetImpact, TargetImpact}, {Profile}, DuplicateResult));
  return true;
}

#endif
