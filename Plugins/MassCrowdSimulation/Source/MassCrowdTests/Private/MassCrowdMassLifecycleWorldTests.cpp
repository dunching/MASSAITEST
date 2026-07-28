#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassCrowdMassLifecycleWorld.h"
#include "MassEntityManager.h"
#include "Misc/AutomationTest.h"

namespace
{
  UWorld* FindLifecycleTestWorld()
  {
    if (!GEngine) return nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
      if (Context.World()
        && (Context.WorldType == EWorldType::Editor
          || Context.WorldType == EWorldType::Game
          || Context.WorldType == EWorldType::PIE))
      {
        return Context.World();
      }
    }
    return nullptr;
  }

  FCrowdLifecycleBatchLimits MakeWorldLimits()
  {
    FCrowdLifecycleBatchLimits Limits;
    Limits.MaxSnapshotEntities = 16;
    Limits.MaxEntriesPerBatch = 8;
    Limits.MaxTrackedSlots = 32;
    Limits.MaxSequenceHistory = 16;
    return Limits;
  }

  FCrowdAgentFacts MakeWorldFacts(
    const uint64 StableEntityId,
    const uint32 LifecycleSerial)
  {
    FCrowdAgentFacts Facts;
    Facts.StableEntityRef = FCrowdStableEntityRef{5, StableEntityId, LifecycleSerial};
    Facts.FactionKey = 2;
    Facts.CapabilitySet.Add(ECrowdCapability::Move);
    Facts.CapabilitySet.Add(ECrowdCapability::MoveTo);
    Facts.DerivedBehaviorLabel =
      static_cast<uint32>(ECrowdActiveBehavior::MoveTo);
    Facts.MovementProfileKey = 7;
    Facts.PresentationProfileKey = 8;
    Facts.RuntimeState = 9;
    return Facts;
  }

  FCrowdLifecycleBatchHeader MakeWorldHeader(
    const uint64 Sequence,
    const uint32 RelevantSetRevision,
    const int64 FixedStepIndex)
  {
    FCrowdLifecycleBatchHeader Header;
    Header.BaseSnapshotRevision = 3;
    Header.Sequence = Sequence;
    Header.RelevantSetRevision = RelevantSetRevision;
    Header.FixedStepIndex = FixedStepIndex;
    return Header;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdMassLifecycleWorldTest,
  "MassCrowd.Runtime.LifecycleWorld.SnapshotDeltaAndSlotReuse",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdMassLifecycleWorldTest::RunTest(const FString& Parameters)
{
  UWorld* TestWorld = FindLifecycleTestWorld();
  if (!TestNotNull(TEXT("test world is available"), TestWorld)) return false;
  TSharedRef<FMassEntityManager> EntityManager = MakeShared<FMassEntityManager>(TestWorld);
  EntityManager->SetDebugName(TEXT("MassCrowdLifecycleWorld"));
  EntityManager->Initialize();
  EntityManager->PostInitialize();

  const TArray<const UScriptStruct*> Types = {
    FCrowdMassAgentFragment::StaticStruct(),
    FCrowdMassBehaviorFragment::StaticStruct(),
    FCrowdMassMembershipFragment::StaticStruct(),
    FCrowdMassAgentTag::StaticStruct()};
  const FMassArchetypeHandle Archetype = EntityManager->CreateArchetype(Types);
  TestTrue(TEXT("lifecycle archetype created"), Archetype.IsValid());

  const FCrowdStableEntityRef FirstLife{5, 100, 1};
  const FCrowdStableEntityRef SecondEntity{5, 200, 1};
  TArray<FCrowdLifecycleSnapshotEntity> Snapshot;
  Snapshot.Add(FCrowdLifecycleSnapshotEntity{MakeWorldFacts(100, 1), 11});
  Snapshot.Add(FCrowdLifecycleSnapshotEntity{MakeWorldFacts(200, 1), 12});
  FCrowdMassLifecycleWorld LifecycleWorld;
  TestTrue(TEXT("snapshot creates real Mass entities"),
    LifecycleWorld.InitializeFromSnapshot(
      *EntityManager, Archetype, 3, 10, 20, Snapshot, MakeWorldLimits()));
  TestEqual(TEXT("snapshot active entity count"), LifecycleWorld.GetActiveEntityCount(), 2);
  TestEqual(TEXT("Mass manager contains snapshot entities"), EntityManager->DebugGetEntityCount(), 2);
  const uint64 SnapshotHash = LifecycleWorld.CalculateEntitySetHash();
  TestTrue(TEXT("complete snapshot entity-set hash is nonzero"), SnapshotHash != 0);

  FMassEntityHandle FirstHandle;
  FMassEntityHandle SecondHandle;
  TestTrue(TEXT("first StableEntityRef resolves"),
    LifecycleWorld.TryGetEntityHandle(FirstLife, FirstHandle));
  TestTrue(TEXT("second StableEntityRef resolves"),
    LifecycleWorld.TryGetEntityHandle(SecondEntity, SecondHandle));
  TestTrue(TEXT("unique StableEntityRefs map to unique Mass entities"),
    FirstHandle != SecondHandle);
  TestNotNull(TEXT("entity has lifecycle membership fragment"),
    EntityManager->GetFragmentDataPtr<FCrowdMassMembershipFragment>(FirstHandle));

  FCrowdSpawnBatch Spawn;
  Spawn.Header = MakeWorldHeader(1, 21, 11);
  Spawn.Entries.Add(FCrowdSpawnEntry{MakeWorldFacts(300, 1), 13});
  FCrowdLifecycleBatchTransport::Finalize(Spawn);
  TestEqual(TEXT("later fixed-step spawn batch applies"),
    LifecycleWorld.ApplyAtBoundary(Spawn), ECrowdLifecycleBatchAcceptResult::Accepted);
  TestEqual(TEXT("spawn creates one Mass entity"), EntityManager->DebugGetEntityCount(), 3);

  FCrowdDespawnBatch Despawn;
  Despawn.Header = MakeWorldHeader(2, 22, 12);
  Despawn.Entries.Add(FCrowdDespawnEntry{FirstLife, ECrowdDespawnReason::Death});
  FCrowdLifecycleBatchTransport::Finalize(Despawn);
  TestEqual(TEXT("despawn batch applies"),
    LifecycleWorld.ApplyAtBoundary(Despawn), ECrowdLifecycleBatchAcceptResult::Accepted);
  TestFalse(TEXT("real Mass entity is destroyed"), EntityManager->IsEntityValid(FirstHandle));
  TestEqual(TEXT("destroy decrements Mass entity count"), EntityManager->DebugGetEntityCount(), 2);

  const FCrowdStableEntityRef NextLife{5, 100, 2};
  FCrowdSpawnBatch Reuse;
  Reuse.Header = MakeWorldHeader(3, 23, 13);
  Reuse.Entries.Add(FCrowdSpawnEntry{MakeWorldFacts(100, 2), 31});
  FCrowdLifecycleBatchTransport::Finalize(Reuse);
  TestEqual(TEXT("higher LifecycleSerial reuses stable slot"),
    LifecycleWorld.ApplyAtBoundary(Reuse), ECrowdLifecycleBatchAcceptResult::Accepted);
  FMassEntityHandle ReusedHandle;
  TestTrue(TEXT("new lifecycle resolves to a valid Mass entity"),
    LifecycleWorld.TryGetEntityHandle(NextLife, ReusedHandle));
  TestTrue(TEXT("new Mass handle serial differs from destroyed handle"),
    ReusedHandle.SerialNumber != FirstHandle.SerialNumber);
  TestFalse(TEXT("old lifecycle no longer resolves"),
    LifecycleWorld.TryGetEntityHandle(FirstLife, FirstHandle));
  TestEqual(TEXT("slot reuse preserves total Mass count"), EntityManager->DebugGetEntityCount(), 3);

  FCrowdMembershipBatch Membership;
  Membership.Header = MakeWorldHeader(4, 24, 14);
  Membership.Entries.Add(FCrowdMembershipEntry{NextLife, 31, 41});
  Membership.Entries.Add(FCrowdMembershipEntry{SecondEntity, 12, 42});
  FCrowdLifecycleBatchTransport::Finalize(Membership);
  TestEqual(TEXT("membership migration applies atomically"),
    LifecycleWorld.ApplyAtBoundary(Membership), ECrowdLifecycleBatchAcceptResult::Accepted);
  TestEqual(TEXT("first migrated membership is stored in Mass fragment"),
    EntityManager->GetFragmentDataChecked<FCrowdMassMembershipFragment>(ReusedHandle).MembershipKey,
    uint32{41});
  TestEqual(TEXT("second migrated membership is stored in Mass fragment"),
    EntityManager->GetFragmentDataChecked<FCrowdMassMembershipFragment>(SecondHandle).MembershipKey,
    uint32{42});
  const uint64 MigratedHash = LifecycleWorld.CalculateEntitySetHash();
  TestNotEqual(TEXT("complete entity-set hash includes membership"), MigratedHash, SnapshotHash);

  FCrowdAgentFacts StaleCorrection = MakeWorldFacts(100, 1);
  StaleCorrection.RuntimeState = 77;
  TestFalse(TEXT("stale lifecycle correction is rejected"),
    LifecycleWorld.ApplyAgentFactsCorrectionAtBoundary(15, StaleCorrection));
  const uint64 BeforeCorrectionHash = LifecycleWorld.CalculateEntitySetHash();
  FCrowdAgentFacts ValidCorrection = MakeWorldFacts(100, 2);
  ValidCorrection.RuntimeState = 77;
  TestTrue(TEXT("matching lifecycle correction applies at boundary"),
    LifecycleWorld.ApplyAgentFactsCorrectionAtBoundary(15, ValidCorrection));
  TestNotEqual(TEXT("complete entity-set hash includes corrected facts"),
    LifecycleWorld.CalculateEntitySetHash(), BeforeCorrectionHash);

  FCrowdDespawnBatch StaleDespawn;
  StaleDespawn.Header = MakeWorldHeader(5, 25, 16);
  StaleDespawn.Entries.Add(FCrowdDespawnEntry{
    FirstLife, ECrowdDespawnReason::BusinessRecycle});
  FCrowdLifecycleBatchTransport::Finalize(StaleDespawn);
  const uint64 BeforeStaleHash = LifecycleWorld.CalculateEntitySetHash();
  TestEqual(TEXT("stale lifecycle despawn is rejected"),
    LifecycleWorld.ApplyAtBoundary(StaleDespawn),
    ECrowdLifecycleBatchAcceptResult::RejectedStale);
  TestTrue(TEXT("stale despawn leaves new lifecycle valid"),
    EntityManager->IsEntityValid(ReusedHandle));
  TestEqual(TEXT("stale rejection preserves complete entity-set hash"),
    LifecycleWorld.CalculateEntitySetHash(), BeforeStaleHash);

  FCrowdMembershipBatch AtomicFailure;
  AtomicFailure.Header = MakeWorldHeader(5, 25, 16);
  AtomicFailure.Entries.Add(FCrowdMembershipEntry{NextLife, 41, 51});
  AtomicFailure.Entries.Add(FCrowdMembershipEntry{SecondEntity, 999, 52});
  FCrowdLifecycleBatchTransport::Finalize(AtomicFailure);
  TestEqual(TEXT("invalid member pre-state rejects complete boundary"),
    LifecycleWorld.ApplyAtBoundary(AtomicFailure),
    ECrowdLifecycleBatchAcceptResult::RejectedConflict);
  TestEqual(TEXT("atomic rejection leaves first Mass membership untouched"),
    EntityManager->GetFragmentDataChecked<FCrowdMassMembershipFragment>(ReusedHandle).MembershipKey,
    uint32{41});
  TestEqual(TEXT("atomic rejection leaves second Mass membership untouched"),
    EntityManager->GetFragmentDataChecked<FCrowdMassMembershipFragment>(SecondHandle).MembershipKey,
    uint32{42});
  TestEqual(TEXT("last committed fixed-step is correction boundary"),
    LifecycleWorld.GetLastAppliedFixedStep(), int64{15});

  EntityManager->Deinitialize();
  return true;
}

#endif
