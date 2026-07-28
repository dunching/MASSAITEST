#include "Misc/AutomationTest.h"

#include "MassCrowdLifecycleBatches.h"

#include <type_traits>

#if WITH_DEV_AUTOMATION_TESTS

static_assert(std::is_trivially_copyable_v<FCrowdLifecycleBatchHeader>);
static_assert(std::is_trivially_copyable_v<FCrowdLifecycleSnapshotEntity>);
static_assert(std::is_trivially_copyable_v<FCrowdSpawnEntry>);
static_assert(std::is_trivially_copyable_v<FCrowdDespawnEntry>);
static_assert(std::is_trivially_copyable_v<FCrowdMembershipEntry>);

namespace
{
  FCrowdLifecycleBatchLimits MakeLimits()
  {
    FCrowdLifecycleBatchLimits Limits;
    Limits.MaxSnapshotEntities = 16;
    Limits.MaxEntriesPerBatch = 4;
    Limits.MaxTrackedSlots = 32;
    Limits.MaxSequenceHistory = 8;
    return Limits;
  }

  FCrowdAgentFacts MakeFacts(
    const uint64 StableEntityId,
    const uint32 LifecycleSerial = 1)
  {
    FCrowdAgentFacts Facts;
    Facts.StableEntityRef = FCrowdStableEntityRef{1, StableEntityId, LifecycleSerial};
    Facts.FactionKey = 10;
    Facts.CapabilitySet.Add(ECrowdCapability::Move);
    Facts.CapabilitySet.Add(ECrowdCapability::MoveTo);
    Facts.ActiveBehavior = ECrowdActiveBehavior::MoveTo;
    Facts.MovementProfileKey = 20;
    Facts.PresentationProfileKey = 30;
    Facts.RuntimeState = 40;
    return Facts;
  }

  FCrowdLifecycleBatchHeader MakeHeader(
    const uint64 Sequence,
    const uint32 RelevantSetRevision,
    const int64 FixedStepIndex)
  {
    FCrowdLifecycleBatchHeader Header;
    Header.BaseSnapshotRevision = 7;
    Header.FixedStepIndex = FixedStepIndex;
    Header.RelevantSetRevision = RelevantSetRevision;
    Header.Sequence = Sequence;
    return Header;
  }

  TArray<FCrowdLifecycleSnapshotEntity> MakeSnapshot()
  {
    TArray<FCrowdLifecycleSnapshotEntity> Snapshot;
    Snapshot.Add(FCrowdLifecycleSnapshotEntity{MakeFacts(100), 11});
    Snapshot.Add(FCrowdLifecycleSnapshotEntity{MakeFacts(200), 12});
    return Snapshot;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdLifecycleSnapshotContinuityTest,
  "MassCrowd.Networking.LifecycleBatches.SnapshotContinuity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdLifecycleSnapshotContinuityTest::RunTest(const FString& Parameters)
{
  FCrowdLifecycleDeltaState State;
  const TArray<FCrowdLifecycleSnapshotEntity> Snapshot = MakeSnapshot();
  TestTrue(TEXT("bounded state begins from canonical snapshot"),
    State.BeginFromSnapshot(7, 100, 10, Snapshot, MakeLimits()));
  TestEqual(TEXT("snapshot active count"), State.GetActiveEntityCount(), 2);

  FCrowdSpawnBatch Spawn;
  Spawn.Header = MakeHeader(1, 11, 101);
  Spawn.Entries.Add(FCrowdSpawnEntry{MakeFacts(300), 13});
  TestTrue(TEXT("spawn batch finalizes"), FCrowdLifecycleBatchTransport::Finalize(Spawn));
  TestEqual(TEXT("first delta continues snapshot sequence"),
    State.AcceptSpawnBatch(Spawn), ECrowdLifecycleBatchAcceptResult::Accepted);
  TestTrue(TEXT("spawned StableEntityRef is active"),
    State.Contains(FCrowdStableEntityRef{1, 300, 1}));
  TestEqual(TEXT("active count after spawn"), State.GetActiveEntityCount(), 3);

  TestEqual(TEXT("identical batch duplicate is idempotent"),
    State.AcceptSpawnBatch(Spawn), ECrowdLifecycleBatchAcceptResult::Duplicate);
  TestEqual(TEXT("duplicate does not advance sequence"), State.GetLastSequence(), uint64{1});

  FCrowdSpawnBatch Conflict = Spawn;
  Conflict.Entries[0].MembershipKey = 99;
  FCrowdLifecycleBatchTransport::Finalize(Conflict);
  TestEqual(TEXT("conflicting duplicate sequence is rejected"),
    State.AcceptSpawnBatch(Conflict), ECrowdLifecycleBatchAcceptResult::RejectedConflict);
  TestEqual(TEXT("conflict does not mutate active count"), State.GetActiveEntityCount(), 3);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdLifecycleOrderingBoundsTest,
  "MassCrowd.Networking.LifecycleBatches.OrderingBoundsAndAtomicity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdLifecycleOrderingBoundsTest::RunTest(const FString& Parameters)
{
  const FCrowdLifecycleBatchLimits Limits = MakeLimits();
  TArray<FCrowdLifecycleSnapshotEntity> ReversedSnapshot = MakeSnapshot();
  Swap(ReversedSnapshot[0], ReversedSnapshot[1]);
  FCrowdLifecycleDeltaState InvalidState;
  TestFalse(TEXT("non-canonical snapshot order is rejected"),
    InvalidState.BeginFromSnapshot(7, 100, 10, ReversedSnapshot, Limits));

  FCrowdLifecycleDeltaState State;
  const TArray<FCrowdLifecycleSnapshotEntity> Snapshot = MakeSnapshot();
  TestTrue(TEXT("canonical snapshot begins"),
    State.BeginFromSnapshot(7, 100, 10, Snapshot, Limits));
  const uint64 InitialMembershipHash = State.CalculateMembershipHash();

  FCrowdMembershipBatch MissingSequence;
  MissingSequence.Header = MakeHeader(2, 11, 101);
  MissingSequence.Entries.Add(
    FCrowdMembershipEntry{FCrowdStableEntityRef{1, 100, 1}, 11, 21});
  FCrowdLifecycleBatchTransport::Finalize(MissingSequence);
  TestEqual(TEXT("sequence gap is rejected"),
    State.AcceptMembershipBatch(MissingSequence),
    ECrowdLifecycleBatchAcceptResult::RejectedMissingSequence);

  FCrowdMembershipBatch WrongSnapshot = MissingSequence;
  WrongSnapshot.Header = MakeHeader(1, 11, 101);
  WrongSnapshot.Header.BaseSnapshotRevision = 8;
  FCrowdLifecycleBatchTransport::Finalize(WrongSnapshot);
  TestEqual(TEXT("wrong base snapshot revision is stale"),
    State.AcceptMembershipBatch(WrongSnapshot),
    ECrowdLifecycleBatchAcceptResult::RejectedStale);

  FCrowdMembershipBatch AtomicConflict;
  AtomicConflict.Header = MakeHeader(1, 11, 101);
  AtomicConflict.Entries.Add(
    FCrowdMembershipEntry{FCrowdStableEntityRef{1, 100, 1}, 11, 21});
  AtomicConflict.Entries.Add(
    FCrowdMembershipEntry{FCrowdStableEntityRef{1, 200, 1}, 999, 22});
  FCrowdLifecycleBatchTransport::Finalize(AtomicConflict);
  TestEqual(TEXT("bad membership pre-state rejects whole batch"),
    State.AcceptMembershipBatch(AtomicConflict),
    ECrowdLifecycleBatchAcceptResult::RejectedConflict);
  TestEqual(TEXT("failed batch does not advance sequence"), State.GetLastSequence(), uint64{0});
  TestEqual(TEXT("failed batch preserves membership hash"),
    State.CalculateMembershipHash(), InitialMembershipHash);

  FCrowdSpawnBatch Oversized;
  Oversized.Header = MakeHeader(1, 11, 101);
  for (int32 Index = 0; Index < Limits.MaxEntriesPerBatch + 1; ++Index)
  {
    Oversized.Entries.Add(FCrowdSpawnEntry{MakeFacts(300 + Index), 30});
  }
  FCrowdLifecycleBatchTransport::Finalize(Oversized);
  TestEqual(TEXT("batch entry bound is enforced"),
    State.AcceptSpawnBatch(Oversized), ECrowdLifecycleBatchAcceptResult::RejectedBounds);
  TestEqual(TEXT("bounds rejection preserves state"), State.GetActiveEntityCount(), 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdLifecycleSlotReuseMembershipTest,
  "MassCrowd.Networking.LifecycleBatches.SlotReuseAndMembershipHash",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdLifecycleSlotReuseMembershipTest::RunTest(const FString& Parameters)
{
  FCrowdLifecycleDeltaState State;
  const TArray<FCrowdLifecycleSnapshotEntity> Snapshot = MakeSnapshot();
  TestTrue(TEXT("state begins"), State.BeginFromSnapshot(7, 100, 10, Snapshot, MakeLimits()));

  FCrowdDespawnBatch Despawn;
  Despawn.Header = MakeHeader(1, 11, 101);
  Despawn.Entries.Add(FCrowdDespawnEntry{
    FCrowdStableEntityRef{1, 100, 1}, ECrowdDespawnReason::Death});
  FCrowdLifecycleBatchTransport::Finalize(Despawn);
  TestEqual(TEXT("death despawn accepted"),
    State.AcceptDespawnBatch(Despawn), ECrowdLifecycleBatchAcceptResult::Accepted);
  TestFalse(TEXT("despawned life is inactive"),
    State.Contains(FCrowdStableEntityRef{1, 100, 1}));

  FCrowdSpawnBatch Reuse;
  Reuse.Header = MakeHeader(2, 12, 102);
  Reuse.Entries.Add(FCrowdSpawnEntry{MakeFacts(100, 2), 31});
  FCrowdLifecycleBatchTransport::Finalize(Reuse);
  TestEqual(TEXT("higher serial reuses inactive slot"),
    State.AcceptSpawnBatch(Reuse), ECrowdLifecycleBatchAcceptResult::Accepted);
  TestTrue(TEXT("next lifecycle is active"),
    State.Contains(FCrowdStableEntityRef{1, 100, 2}));
  TestFalse(TEXT("old lifecycle remains stale"),
    State.Contains(FCrowdStableEntityRef{1, 100, 1}));
  TestEqual(TEXT("slot reuse does not grow tracked slots"), State.GetTrackedSlotCount(), 2);

  FCrowdDespawnBatch StaleDespawn;
  StaleDespawn.Header = MakeHeader(3, 13, 103);
  StaleDespawn.Entries.Add(FCrowdDespawnEntry{
    FCrowdStableEntityRef{1, 100, 1}, ECrowdDespawnReason::BusinessRecycle});
  FCrowdLifecycleBatchTransport::Finalize(StaleDespawn);
  TestEqual(TEXT("stale lifecycle despawn is rejected"),
    State.AcceptDespawnBatch(StaleDespawn), ECrowdLifecycleBatchAcceptResult::RejectedStale);

  FCrowdMembershipBatch Membership;
  Membership.Header = MakeHeader(3, 13, 103);
  Membership.Entries.Add(
    FCrowdMembershipEntry{FCrowdStableEntityRef{1, 100, 2}, 31, 41});
  FCrowdLifecycleBatchTransport::Finalize(Membership);
  TestEqual(TEXT("valid membership migration uses unconsumed sequence"),
    State.AcceptMembershipBatch(Membership), ECrowdLifecycleBatchAcceptResult::Accepted);
  uint32 MembershipKey = 0;
  TestTrue(TEXT("membership can be queried"),
    State.TryGetMembership(FCrowdStableEntityRef{1, 100, 2}, MembershipKey));
  TestEqual(TEXT("membership migration committed"), MembershipKey, uint32{41});

  FCrowdLifecycleDeltaState Mirror;
  TestTrue(TEXT("mirror begins from same snapshot"),
    Mirror.BeginFromSnapshot(7, 100, 10, Snapshot, MakeLimits()));
  Mirror.AcceptDespawnBatch(Despawn);
  Mirror.AcceptSpawnBatch(Reuse);
  Mirror.AcceptMembershipBatch(Membership);
  TestEqual(TEXT("same snapshot and delta stream has deterministic membership hash"),
    Mirror.CalculateMembershipHash(), State.CalculateMembershipHash());

  FCrowdMembershipBatch Detour;
  Detour.Header = MakeHeader(4, 14, 104);
  Detour.Entries.Add(
    FCrowdMembershipEntry{FCrowdStableEntityRef{1, 100, 2}, 41, 42});
  FCrowdLifecycleBatchTransport::Finalize(Detour);
  Mirror.AcceptMembershipBatch(Detour);
  FCrowdMembershipBatch Return;
  Return.Header = MakeHeader(5, 15, 105);
  Return.Entries.Add(
    FCrowdMembershipEntry{FCrowdStableEntityRef{1, 100, 2}, 42, 41});
  FCrowdLifecycleBatchTransport::Finalize(Return);
  Mirror.AcceptMembershipBatch(Return);
  TestEqual(TEXT("membership hash is independent of legal arrival history"),
    Mirror.CalculateMembershipHash(), State.CalculateMembershipHash());

  FCrowdDespawnBatch ReasonVariant = Despawn;
  ReasonVariant.Entries[0].Reason = ECrowdDespawnReason::RelevancyExit;
  FCrowdLifecycleBatchTransport::Finalize(ReasonVariant);
  TestNotEqual(TEXT("despawn reason participates in batch hash"),
    ReasonVariant.Header.BatchHash, Despawn.Header.BatchHash);
  return true;
}

#endif
