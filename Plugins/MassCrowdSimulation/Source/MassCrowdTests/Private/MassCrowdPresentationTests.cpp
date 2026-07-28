#include "Misc/AutomationTest.h"

#include "MassCrowdPresentationSubsystem.h"

namespace
{
  class FFakePresentationSink final : public ICrowdPresentationInstanceSink
  {
  public:
    TArray<FCrowdPresentationState> Instances;

    virtual int32 AddInstance(
      const FCrowdPresentationState& State) override
    {
      return Instances.Add(State);
    }

    virtual bool UpdateInstance(
      const int32 Slot,
      const FCrowdPresentationState& State) override
    {
      if (!Instances.IsValidIndex(Slot)) return false;
      Instances[Slot] = State;
      return true;
    }

    virtual bool RemoveInstanceSwap(
      const int32 Slot,
      const int32 LastSlot) override
    {
      if (!Instances.IsValidIndex(Slot) || LastSlot != Instances.Num() - 1)
        return false;
      Instances.RemoveAtSwap(Slot, EAllowShrinking::No);
      return true;
    }
  };

  FCrowdPresentationState MakePresentation(
    const uint64 Id, const uint64 Sequence)
  {
    FCrowdPresentationState State;
    State.EntityRef = {1, Id, 1};
    State.ProfileKey = 10;
    State.Sequence = Sequence;
    State.Transform.SetLocation(FVector(Id * 10.0, 0.0, 0.0));
    State.SampleServerSeconds = Sequence / 30.0;
    return State;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdPresentationSlotTableTest,
  "MassCrowd.Presentation.SlotTableSwapRemove",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdPresentationSlotTableTest::RunTest(
  const FString& Parameters)
{
  const TSharedRef<FFakePresentationSink> Sink =
    MakeShared<FFakePresentationSink>();
  FCrowdPresentationSlotTable Table(Sink);
  const FCrowdPresentationState A = MakePresentation(1, 1);
  const FCrowdPresentationState B = MakePresentation(2, 1);
  const FCrowdPresentationState C = MakePresentation(3, 1);
  TestTrue(TEXT("three unique spawns apply"),
    Table.ApplySpawn(A) == ECrowdPresentationApplyResult::Applied
      && Table.ApplySpawn(B) == ECrowdPresentationApplyResult::Applied
      && Table.ApplySpawn(C) == ECrowdPresentationApplyResult::Applied);
  TestTrue(TEXT("duplicate spawn does not duplicate instance"),
    Table.ApplySpawn(A) == ECrowdPresentationApplyResult::Duplicate
      && Table.Num() == 3 && Sink->Instances.Num() == 3);
  TestTrue(TEXT("middle removal applies"),
    Table.ApplyDespawn(B.EntityRef, 2)
      == ECrowdPresentationApplyResult::Applied);
  TestTrue(TEXT("swap-removed reverse slot follows moved entity"),
    Table.FindSlot(C.EntityRef) == 1 && Table.ValidateBijection());
  FCrowdPresentationState UpdatedC = C;
  UpdatedC.Sequence = 3;
  UpdatedC.CargoRef = {2, 50, 1};
  TestTrue(TEXT("cargo attachment update applies once"),
    Table.ApplyUpdate(UpdatedC)
      == ECrowdPresentationApplyResult::Applied);
  TestTrue(TEXT("duplicate cargo update is idempotent"),
    Table.ApplyUpdate(UpdatedC)
      == ECrowdPresentationApplyResult::Duplicate
      && Table.Num() == Sink->Instances.Num());
  TestTrue(TEXT("duplicate despawn is idempotent"),
    Table.ApplyDespawn(B.EntityRef, 2)
      == ECrowdPresentationApplyResult::Duplicate);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdPresentationAtomicFrameTest,
  "MassCrowd.Presentation.AtomicFrame",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdPresentationAtomicFrameTest::RunTest(
  const FString& Parameters)
{
  const TSharedRef<FFakePresentationSink> Sink =
    MakeShared<FFakePresentationSink>();
  UMassCrowdPresentationSubsystem* Subsystem =
    NewObject<UMassCrowdPresentationSubsystem>();
  TestNotNull(TEXT("presentation subsystem fixture creates"), Subsystem);
  TestTrue(TEXT("profile registers"), Subsystem->RegisterProfile(10, Sink));

  const FCrowdPresentationState A = MakePresentation(1, 1);
  const FCrowdPresentationState B = MakePresentation(2, 1);
  FCrowdPresentationOperation SpawnA;
  SpawnA.Kind = ECrowdPresentationOperationKind::Spawn;
  SpawnA.State = A;
  SpawnA.EntityRef = A.EntityRef;
  SpawnA.ProfileKey = A.ProfileKey;
  SpawnA.Sequence = A.Sequence;
  FCrowdPresentationOperation SpawnB = SpawnA;
  SpawnB.State = B;
  SpawnB.EntityRef = B.EntityRef;
  const FCrowdPresentationOperation Spawns[] = {SpawnB, SpawnA};
  FCrowdPreparedPresentationFrame Prepared;
  TestTrue(TEXT("whole presentation frame prepares"),
    Subsystem->PrepareFrame(0x1234, Spawns, Prepared));
  TestEqual(TEXT("prepare has zero sink side effects"),
    Sink->Instances.Num(), 0);
  TestTrue(TEXT("prepared frame applies"), 
    Subsystem->ApplyPreparedFrame(Prepared));
  TestEqual(TEXT("both instances publish together"),
    Sink->Instances.Num(), 2);

  FCrowdPresentationOperation ValidUpdate;
  ValidUpdate.Kind = ECrowdPresentationOperationKind::Update;
  ValidUpdate.State = A;
  ValidUpdate.State.Sequence = 2;
  ValidUpdate.State.Transform.SetLocation(FVector(50.0, 0.0, 0.0));
  ValidUpdate.EntityRef = A.EntityRef;
  ValidUpdate.ProfileKey = A.ProfileKey;
  ValidUpdate.Sequence = 2;
  FCrowdPresentationOperation InvalidUpdate = ValidUpdate;
  InvalidUpdate.State = MakePresentation(999, 2);
  InvalidUpdate.EntityRef = InvalidUpdate.State.EntityRef;
  const FCrowdPresentationOperation Mixed[] = {
    ValidUpdate, InvalidUpdate};
  FCrowdPreparedPresentationFrame Rejected;
  TestFalse(TEXT("one missing entity rejects entire frame"),
    Subsystem->PrepareFrame(0x5678, Mixed, Rejected));
  TestEqual(TEXT("failed prepare leaves all instances unchanged"),
    Sink->Instances.Num(), 2);
  return true;
}
