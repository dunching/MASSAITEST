#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CrowdSharedFlowFieldKernel.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdSharedFlowGoldenTest,
  "MassCrowd.Core.SharedFlow.Golden",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdSharedFlowGoldenTest::RunTest(const FString& Parameters)
{
  FCrowdSharedFlowField Field;
  const FCrowdSharedFlowFieldConfig Config =
    FCrowdSharedFlowFieldKernel::MakeSf1Config(1);
  TestTrue(TEXT("SF1 field builds in plugin Core"),
    FCrowdSharedFlowFieldKernel::Build(Config, Field));
  TestEqual(TEXT("SF1 build hash remains golden"), Field.BuildHash, 267519150u);
  TestEqual(TEXT("SF1 obstacle count remains stable"), Config.ObstacleSpecs.Num(), 10);

  const FVector Agent0(2449.0f, -956.0f, 60.0f);
  const FCrowdSharedFlowSample Sample =
    FCrowdSharedFlowFieldKernel::Sample(Field, Agent0);
  TestEqual(TEXT("Agent0 stable cell remains golden"), Sample.StableCellKey, 1246);
  TestEqual(TEXT("Agent0 remains reachable"), Sample.Status,
    ECrowdFlowLocationStatus::Reachable);
  TestFalse(TEXT("Agent0 cell remains unblocked"), Sample.bBlocked);
  TestFalse(TEXT("Agent0 cell remains reachable"), Sample.bUnreachable);
  TestTrue(TEXT("Agent0 receives non-zero guidance"),
    !Sample.FlowDirection.IsNearlyZero());

  FCrowdSharedFlowFieldConfig Reordered = Config;
  Algo::Reverse(Reordered.ObstacleSpecs);
  FCrowdSharedFlowField ReorderedField;
  TestTrue(TEXT("reordered obstacle field builds"),
    FCrowdSharedFlowFieldKernel::Build(Reordered, ReorderedField));
  TestEqual(TEXT("obstacle input order does not change build hash"),
    ReorderedField.BuildHash, Field.BuildHash);
  return true;
}

#endif
