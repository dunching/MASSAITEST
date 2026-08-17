#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "MassCrowdSharedFlowWork.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSharedFlowPluginAdapterTest,
  "CrowdDemo.SF.SharedFlow.PluginCoreEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSharedFlowPluginAdapterTest::RunTest(const FString& Parameters)
{
  const FCrowdDemoSharedFlowFieldConfig DemoConfig =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(17);
  const FCrowdSharedFlowFieldConfig CoreConfig =
    FCrowdSharedFlowFieldKernel::MakeSf1Config(17);

  FCrowdDemoSharedFlowField DemoField;
  FCrowdSharedFlowField CoreField;
  TestTrue(TEXT("Demo legacy build succeeds"),
    FCrowdDemoSharedFlowFieldKernel::Build(DemoConfig, DemoField));
  TestTrue(TEXT("Core build succeeds"),
    FCrowdSharedFlowFieldKernel::Build(CoreConfig, CoreField));
  TestEqual(TEXT("legacy build hash equals Core"),
    DemoField.BuildHash, CoreField.BuildHash);
  TestEqual(TEXT("legacy topology hash equals Core"),
    DemoField.TopologyHash, CoreField.TopologyHash);
  TestEqual(TEXT("legacy integration hash equals Core"),
    DemoField.IntegrationHash, CoreField.IntegrationHash);
  TestEqual(TEXT("legacy blocked cell count equals Core"),
    DemoField.BlockedCellCount, CoreField.BlockedCellCount);
  TestEqual(TEXT("legacy directed edge count equals Core"),
    DemoField.ValidDirectedEdgeCount, CoreField.ValidDirectedEdgeCount);
  const FCrowdDemoSharedFlowField RuntimeMirror =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(CoreField);
  TestTrue(TEXT("Runtime compatibility mirror remains valid"),
    RuntimeMirror.IsValid());
  TestEqual(TEXT("Runtime compatibility mirror hash"),
    RuntimeMirror.BuildHash, DemoField.BuildHash);

  const FVector Samples[] = {
    FVector(2449.0f, -956.0f, 60.0f),
    FVector(1450.0f, -1550.0f, 60.0f),
    FVector(-1200.0f, 100.0f, 60.0f),
    FVector(0.0f, 1800.0f, 60.0f)
  };
  for (int32 Index = 0; Index < UE_ARRAY_COUNT(Samples); ++Index)
  {
    const FCrowdDemoSharedFlowSample DemoSample =
      FCrowdDemoSharedFlowFieldKernel::Sample(DemoField, Samples[Index]);
    const FCrowdSharedFlowSample CoreSample =
      FCrowdSharedFlowFieldKernel::Sample(CoreField, Samples[Index]);
    TestEqual(FString::Printf(TEXT("sample %d status"), Index),
      static_cast<uint8>(DemoSample.Status), static_cast<uint8>(CoreSample.Status));
    TestEqual(FString::Printf(TEXT("sample %d cell"), Index),
      DemoSample.StableCellKey, CoreSample.StableCellKey);
    TestEqual(FString::Printf(TEXT("sample %d integration"), Index),
      DemoSample.IntegrationCost, CoreSample.IntegrationCost);
    TestTrue(FString::Printf(TEXT("sample %d direction"), Index),
      DemoSample.FlowDirection.Equals(CoreSample.FlowDirection, KINDA_SMALL_NUMBER));
  }

  const FVector Start(-2200.0f, -2200.0f, 60.0f);
  const FVector Proposed(-500.0f, -2200.0f, 60.0f);
  const auto DemoConstraint = FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
    DemoConfig, Start, Proposed, 1.0f / 30.0f, true);
  const auto CoreConstraint = FCrowdSharedFlowFieldKernel::ConstrainMovement(
    CoreConfig, Start, Proposed, 1.0f / 30.0f, true);
  TestTrue(TEXT("legacy constrained location equals Core"),
    DemoConstraint.Location.Equals(CoreConstraint.Location, KINDA_SMALL_NUMBER));
  TestEqual(TEXT("legacy obstacle hit equals Core"),
    DemoConstraint.bHitObstacle, CoreConstraint.bHitObstacle);
  TestEqual(TEXT("legacy penetration equals Core"),
    DemoConstraint.bPenetrating, CoreConstraint.bPenetrating);

  FCrowdMassSharedFlowResource RuntimeResource;
  FCrowdMassSharedFlowBuildInput RuntimeBuild;
  RuntimeBuild.Config =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(DemoConfig);
  const auto RuntimeBuildResult = FCrowdMassSharedFlowWork::EnsureResource(
    RuntimeBuild, RuntimeResource);
  TestTrue(TEXT("Runtime WORK resource build succeeds"),
    RuntimeBuildResult.bValid);
  TestEqual(TEXT("Runtime WORK resource equals legacy hash"),
    RuntimeResource.Field.BuildHash, DemoField.BuildHash);

  FCrowdMassSharedFlowSampleInput WorkInput;
  WorkInput.FixedStepIndex = 9;
  WorkInput.PlanRevision = 17;
  WorkInput.FixedStepSeconds = 1.0f / 30.0f;
  WorkInput.Fields.Add(&RuntimeResource.Field);
  for (int32 Index = 0; Index < 2; ++Index)
  {
    FCrowdMassSharedFlowAgentInput& Agent =
      WorkInput.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Index + 1;
    Agent.LifecycleSerial = Index + 10;
    Agent.FieldIndex = 0;
    Agent.Location = Samples[Index];
    Agent.GoalLocation = DemoConfig.GoalLocation;
    Agent.CurrentYawDegrees = 15.0f;
    Agent.MaximumSpeedCmps = 300.0f;
  }
  const FCrowdMassSharedFlowSampleOutput RuntimeForward =
    FCrowdMassSharedFlowWork::BuildPreferred(WorkInput);
  TestTrue(TEXT("Runtime preferred WORK succeeds"), RuntimeForward.bValid);
  Algo::Reverse(WorkInput.Agents);
  const FCrowdMassSharedFlowSampleOutput RuntimeReverse =
    FCrowdMassSharedFlowWork::BuildPreferred(WorkInput);
  TestEqual(TEXT("Runtime preferred WORK order stable"),
    RuntimeReverse.StableHash, RuntimeForward.StableHash);
  for (const FCrowdMassSharedFlowAgentOutput& Result : RuntimeForward.Agents)
  {
    const FCrowdDemoSharedFlowSample LegacySample =
      FCrowdDemoSharedFlowFieldKernel::Sample(
        DemoField, Samples[Result.AgentId - 1]);
    TestEqual(TEXT("Runtime preferred cell equals legacy"),
      Result.Sample.StableCellKey, LegacySample.StableCellKey);
    TestTrue(TEXT("Runtime preferred velocity equals legacy direction"),
      Result.Candidate.PreferredVelocity.GetSafeNormal2D().Equals(
        LegacySample.FlowDirection.GetSafeNormal2D(), KINDA_SMALL_NUMBER));
  }
  return true;
}

#endif
