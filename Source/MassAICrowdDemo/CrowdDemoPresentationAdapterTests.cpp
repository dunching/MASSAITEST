#include "Misc/AutomationTest.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Mass/CrowdDemoPresentationAdapter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdPresentationState MakePresentationState(
    const int32 Index,
    const float HitFlashIntensity)
  {
    FCrowdPresentationState State;
    State.EntityRef = FCrowdStableEntityRef{
      1,
      static_cast<uint64>(Index + 1),
      1};
    State.Transform = FTransform(
      FRotator::ZeroRotator,
      FVector(static_cast<double>(Index) * 10.0, 0.0, 0.0));
    State.ProfileKey = 1;
    State.CustomData = FVector3f(
      static_cast<float>(Index),
      static_cast<float>(Index) - 1.0f,
      HitFlashIntensity);
    State.Sequence = 1;
    return State;
  }

  float ReadCustomData(
    const UInstancedStaticMeshComponent& Instances,
    const int32 Slot,
    const int32 DataIndex)
  {
    const int32 Offset =
      Slot * Instances.NumCustomDataFloats + DataIndex;
    return Instances.PerInstanceSMCustomData.IsValidIndex(Offset)
      ? Instances.PerInstanceSMCustomData[Offset]
      : -MAX_flt;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSingleIsmPresentationCountsTest,
  "CrowdDemo.Presentation.SingleIsm.CountsAndIsolation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSingleIsmPresentationCountsTest::RunTest(
  const FString& Parameters)
{
  for (const int32 InstanceCount : {0, 1, 10, 9999})
  {
    UInstancedStaticMeshComponent* Instances =
      NewObject<UInstancedStaticMeshComponent>();
    Instances->NumCustomDataFloats = 3;
    FCrowdDemoIsmPresentationSink Sink(*Instances);
    for (int32 Index = 0; Index < InstanceCount; ++Index)
    {
      const float Flash = Index % 7 == 0 ? 1.0f : 0.0f;
      TestEqual(
        TEXT("single ISM slot is contiguous"),
        Sink.AddInstance(MakePresentationState(Index, Flash)),
        Index);
    }
    TestEqual(
      TEXT("single ISM owns exactly one instance per entity"),
      Instances->GetInstanceCount(),
      InstanceCount);
    TestEqual(
      TEXT("single ISM owns exactly three custom floats per entity"),
      Instances->PerInstanceSMCustomData.Num(),
      InstanceCount * 3);
    for (int32 Index = 0; Index < InstanceCount; ++Index)
    {
      const float ExpectedFlash = Index % 7 == 0 ? 1.0f : 0.0f;
      TestEqual(
        TEXT("hit flash is isolated to its own per-instance slot"),
        ReadCustomData(*Instances, Index, 2),
        ExpectedFlash);
    }
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSingleIsmPresentationSwapRemoveTest,
  "CrowdDemo.Presentation.SingleIsm.SwapRemove",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSingleIsmPresentationSwapRemoveTest::RunTest(
  const FString& Parameters)
{
  UInstancedStaticMeshComponent* Instances =
    NewObject<UInstancedStaticMeshComponent>();
  Instances->NumCustomDataFloats = 3;
  FCrowdDemoIsmPresentationSink Sink(*Instances);
  Sink.AddInstance(MakePresentationState(0, 0.0f));
  Sink.AddInstance(MakePresentationState(1, 0.25f));
  Sink.AddInstance(MakePresentationState(2, 0.75f));

  TestTrue(TEXT("swap remove succeeds"), Sink.RemoveInstanceSwap(0, 2));
  TestEqual(TEXT("swap remove conserves count"), Instances->GetInstanceCount(), 2);
  TestEqual(
    TEXT("swap remove copies current frame"),
    ReadCustomData(*Instances, 0, 0),
    2.0f);
  TestEqual(
    TEXT("swap remove copies previous frame"),
    ReadCustomData(*Instances, 0, 1),
    1.0f);
  TestEqual(
    TEXT("swap remove copies hit flash intensity"),
    ReadCustomData(*Instances, 0, 2),
    0.75f);
  return true;
}

#endif
