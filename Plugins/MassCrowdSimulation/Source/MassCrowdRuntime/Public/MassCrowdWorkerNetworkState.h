#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerRuntimeV2.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerNetworkStateConfig
{
  int32 MaxRetainedIntentBatches = 1024;
  int32 MaxStateRecordsPerCheckpoint = 160000;
  int32 MaxResourceRecordsPerCheckpoint = 1024;
  int32 MaxIntentRecordsPerBatch = 64000;
  int32 MaxDigestScopes = 4096;
  int32 MaxCorrectionScopes = 64;
  int32 MaxCorrectionEntities = 4096;
  int32 MaxWorkItemsPerCheckpoint = 80000;
  int32 MaxWakeupsPerCheckpoint = 40000;
  int32 MaxDependencyEdgesPerCheckpoint = 320000;
  int32 MaxCommandsPerCheckpoint = 64000;
  int32 MaxLifecycleWatermarksPerCheckpoint = 16000;
  int32 MaxPayloadBytes = 4 * 1024 * 1024;
  int32 MaxEncodedCheckpointBytes = 64 * 1024 * 1024;
  int32 MaxEncodedIntentBytes = 16 * 1024 * 1024;
  int32 MaxEncodedCorrectionBytes = 16 * 1024 * 1024;

  bool IsValid() const
  {
    return MaxRetainedIntentBatches > 0
      && MaxStateRecordsPerCheckpoint > 0
      && MaxResourceRecordsPerCheckpoint > 0
      && MaxIntentRecordsPerBatch > 0
      && MaxDigestScopes > 0
      && MaxCorrectionScopes > 0
      && MaxCorrectionEntities > 0
      && MaxWorkItemsPerCheckpoint > 0
      && MaxWakeupsPerCheckpoint > 0
      && MaxDependencyEdgesPerCheckpoint > 0
      && MaxCommandsPerCheckpoint > 0
      && MaxLifecycleWatermarksPerCheckpoint > 0
      && MaxPayloadBytes > 0
      && MaxEncodedCheckpointBytes > 0
      && MaxEncodedIntentBytes > 0
      && MaxEncodedCorrectionBytes > 0;
  }
};

enum class ECrowdWorkerAuthorityScopeKind : uint8
{
  Global = 0,
  SpatialCell,
  CohortPlan,
  EntityGuidance
};

struct MASSCROWDRUNTIME_API FCrowdWorkerAuthorityScopeKey
{
  ECrowdWorkerField Field = ECrowdWorkerField::Presentation;
  ECrowdWorkerAuthorityScopeKind Kind =
    ECrowdWorkerAuthorityScopeKind::Global;
  int64 ScopeId = 0;

  bool IsValid() const
  {
    return Field < ECrowdWorkerField::Count
      && Kind <= ECrowdWorkerAuthorityScopeKind::EntityGuidance;
  }
  bool operator==(const FCrowdWorkerAuthorityScopeKey&) const = default;
  bool operator<(const FCrowdWorkerAuthorityScopeKey& Other) const
  {
    if (Field != Other.Field)
      return static_cast<uint8>(Field)
        < static_cast<uint8>(Other.Field);
    if (Kind != Other.Kind)
      return static_cast<uint8>(Kind)
        < static_cast<uint8>(Other.Kind);
    return ScopeId < Other.ScopeId;
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerAuthorityDigestEntry
{
  FCrowdWorkerAuthorityScopeKey Scope;
  uint64 SimulationTick = 0;
  uint64 ThroughInputSequence = 0;
  uint32 EntityCount = 0;
  uint64 StableHash = 0;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerAuthorityDigestBatch
{
  static constexpr uint16 CurrentVersion = 1;
  uint16 Version = CurrentVersion;
  uint64 Generation = 0;
  uint64 DigestSequence = 0;
  uint64 SimulationTick = 0;
  uint64 ThroughInputSequence = 0;
  TArray<FCrowdWorkerAuthorityDigestEntry> Entries;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid(const FCrowdWorkerNetworkStateConfig& Config) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerAuthorityTombstone
{
  FCrowdStableEntityRef EntityRef;
  ECrowdWorkerField Field = ECrowdWorkerField::Presentation;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerAuthorityCorrectionBatch
{
  static constexpr uint16 CurrentVersion = 1;
  uint16 Version = CurrentVersion;
  uint64 Generation = 0;
  uint64 CorrectionSequence = 0;
  uint64 ApplySimulationTick = 0;
  uint64 ThroughInputSequence = 0;
  TArray<FCrowdWorkerAuthorityScopeKey> Scopes;
  TArray<FCrowdStableEntityRef> AuthoritativeMembers;
  TArray<FCrowdWorkerDirtyStateRecord> Records;
  TArray<FCrowdWorkerAuthorityTombstone> Tombstones;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid(const FCrowdWorkerNetworkStateConfig& Config) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerNetworkContinuationState
{
  FCrowdWorkerWorkRingSnapshot WorkRing;
  TArray<FCrowdWorkerWakeup> Wakeups;
  TArray<FCrowdWorkerDependencyRecord> Dependencies;
  TArray<FCrowdWorkerCommandRecord> Commands;
  TArray<FCrowdWorkerLifecycleWatermark> LifecycleWatermarks;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerNetworkCheckpoint
{
  static constexpr uint16 CurrentVersion = 2;

  uint16 Version = CurrentVersion;
  FCrowdWorkerCheckpoint Header;
  uint64 InputBaselineSequence = 0;
  uint64 EventBaselineSequence = 0;
  uint64 MaxCorrectionRevision = 0;
  TArray<FCrowdWorkerDirtyStateRecord> StateRecords;
  TArray<FCrowdWorkerResourceRecord> ResourceRecords;
  FCrowdWorkerNetworkContinuationState Continuation;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid(const FCrowdWorkerNetworkStateConfig& Config) const;
};

enum class ECrowdWorkerNetworkReadResult : uint8
{
  Ready = 0,
  NoData,
  RequiresCheckpoint,
  RejectedGeneration,
  RejectedSequence,
  NotInitialized,
  Violation
};

struct MASSCROWDRUNTIME_API FCrowdWorkerNetworkStateMetrics
{
  uint64 Generation = 0;
  uint64 CheckpointCount = 0;
  uint64 LatestCheckpointEpoch = 0;
  uint64 LatestEventSequence = 0;
  bool bViolation = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerNetworkStatePublisher
{
public:
  bool Reset(
    const FCrowdWorkerNetworkStateConfig& InConfig,
    uint64 InGeneration);

  bool CommitEpoch(
    FCrowdWorkerCheckpoint Header,
    TConstArrayView<FCrowdWorkerDirtyStateRecord> CompleteStates,
    TConstArrayView<FCrowdWorkerResourceRecord> CompleteResources,
    const FCrowdWorkerNetworkContinuationState& Continuation);
  bool RestoreCheckpoint(
    const FCrowdWorkerNetworkCheckpoint& Checkpoint);

  ECrowdWorkerNetworkReadResult ReadCheckpoint(
    uint64 ExpectedGeneration,
    FCrowdWorkerNetworkCheckpoint& OutCheckpoint) const;
  const FCrowdWorkerNetworkStateMetrics& GetMetrics() const
  {
    return Metrics;
  }

private:
  void LatchViolation();

  FCrowdWorkerNetworkStateConfig Config;
  FCrowdWorkerNetworkCheckpoint LatestCheckpoint;
  FCrowdWorkerNetworkStateMetrics Metrics;
  uint64 Generation = 0;
  bool bInitialized = false;
};
