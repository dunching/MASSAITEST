#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

enum class ECrowdDespawnReason : uint8
{
  Death = 0,
  RelevancyExit,
  BusinessRecycle,
  HostDestroyed,
  Count
};

enum class ECrowdLifecycleBatchKind : uint8
{
  Spawn = 1,
  Despawn = 2,
  Membership = 3
};

enum class ECrowdLifecycleBatchAcceptResult : uint8
{
  Accepted,
  Duplicate,
  RejectedInvalid,
  RejectedBounds,
  RejectedStale,
  RejectedMissingSequence,
  RejectedConflict
};

struct FCrowdLifecycleBatchLimits
{
  int32 MaxSnapshotEntities = 0;
  int32 MaxEntriesPerBatch = 0;
  int32 MaxTrackedSlots = 0;
  int32 MaxSequenceHistory = 0;

  bool IsValid() const;
};

struct FCrowdLifecycleBatchHeader
{
  static constexpr uint16 CurrentProtocolVersion = 1;

  uint16 ProtocolVersion = CurrentProtocolVersion;
  uint32 BaseSnapshotRevision = 0;
  int64 FixedStepIndex = 0;
  uint32 RelevantSetRevision = 0;
  uint64 Sequence = 0;
  int32 EntryCount = 0;
  uint64 BatchHash = 0;
};

struct FCrowdLifecycleSnapshotEntity
{
  FCrowdAgentFacts AgentFacts;
  uint32 MembershipKey = 0;
};

struct FCrowdSpawnEntry
{
  FCrowdAgentFacts AgentFacts;
  uint32 MembershipKey = 0;
};

struct FCrowdDespawnEntry
{
  FCrowdStableEntityRef EntityRef;
  ECrowdDespawnReason Reason = ECrowdDespawnReason::HostDestroyed;
};

struct FCrowdMembershipEntry
{
  FCrowdStableEntityRef EntityRef;
  uint32 PreviousMembershipKey = 0;
  uint32 NewMembershipKey = 0;
};

struct FCrowdSpawnBatch
{
  FCrowdLifecycleBatchHeader Header;
  TArray<FCrowdSpawnEntry> Entries;
};

struct FCrowdDespawnBatch
{
  FCrowdLifecycleBatchHeader Header;
  TArray<FCrowdDespawnEntry> Entries;
};

struct FCrowdMembershipBatch
{
  FCrowdLifecycleBatchHeader Header;
  TArray<FCrowdMembershipEntry> Entries;
};

class MASSCROWDNETWORKING_API FCrowdLifecycleBatchTransport
{
public:
  static uint64 CalculateHash(const FCrowdSpawnBatch& Batch);
  static uint64 CalculateHash(const FCrowdDespawnBatch& Batch);
  static uint64 CalculateHash(const FCrowdMembershipBatch& Batch);

  static bool Finalize(FCrowdSpawnBatch& Batch);
  static bool Finalize(FCrowdDespawnBatch& Batch);
  static bool Finalize(FCrowdMembershipBatch& Batch);
};

class MASSCROWDNETWORKING_API FCrowdLifecycleDeltaState
{
public:
  bool BeginFromSnapshot(
    uint32 SnapshotRevision,
    int64 FixedStepIndex,
    uint32 RelevantSetRevision,
    TConstArrayView<FCrowdLifecycleSnapshotEntity> Entities,
    const FCrowdLifecycleBatchLimits& InLimits,
    uint64 ResumeSequence = 1);

  ECrowdLifecycleBatchAcceptResult AcceptSpawnBatch(const FCrowdSpawnBatch& Batch);
  ECrowdLifecycleBatchAcceptResult AcceptDespawnBatch(const FCrowdDespawnBatch& Batch);
  ECrowdLifecycleBatchAcceptResult AcceptMembershipBatch(const FCrowdMembershipBatch& Batch);

  int32 GetActiveEntityCount() const;
  int32 GetTrackedSlotCount() const { return TrackedEntities.Num(); }
  uint64 GetLastSequence() const { return LastSequence; }
  uint32 GetRelevantSetRevision() const { return CurrentRelevantSetRevision; }
  int64 GetFixedStepIndex() const { return CurrentFixedStepIndex; }
  uint64 CalculateMembershipHash() const;
  bool Contains(const FCrowdStableEntityRef& EntityRef) const;
  bool TryGetMembership(const FCrowdStableEntityRef& EntityRef, uint32& OutMembershipKey) const;

private:
  struct FTrackedEntity
  {
    FCrowdAgentFacts AgentFacts;
    uint32 MembershipKey = 0;
    bool bActive = false;
  };

  struct FAppliedBatch
  {
    ECrowdLifecycleBatchKind Kind = ECrowdLifecycleBatchKind::Spawn;
    uint64 Hash = 0;
  };

  int32 FindSlot(const FCrowdStableEntityRef& EntityRef) const;
  void RecordAppliedBatch(uint64 Sequence, ECrowdLifecycleBatchKind Kind, uint64 Hash);

  TArray<FTrackedEntity> TrackedEntities;
  TMap<uint64, FAppliedBatch> AppliedBatches;
  FCrowdLifecycleBatchLimits Limits;
  uint32 BaseSnapshotRevision = 0;
  uint32 CurrentRelevantSetRevision = 0;
  int64 CurrentFixedStepIndex = 0;
  uint64 LastSequence = 0;
  bool bBegun = false;
};
