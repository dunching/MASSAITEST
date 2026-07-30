#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerContractLimits
{
  int32 MaxPayloadBytes = 0;
  int32 MaxInputRecordsPerBatch = 0;
  int32 MaxStatePatchesPerSlot = 0;
  int32 MaxPendingOrderedEvents = 0;

  bool IsValid() const
  {
    return MaxPayloadBytes > 0
      && MaxInputRecordsPerBatch > 0
      && MaxStatePatchesPerSlot > 0
      && MaxPendingOrderedEvents > 0;
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerPayload
{
  uint32 SchemaId = 0;
  uint16 SchemaVersion = 0;
  TArray<uint8> Bytes;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid(int32 MaxPayloadBytes) const;

  bool operator==(const FCrowdWorkerPayload& Other) const = default;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerSpawnDelta
{
  uint64 InputSequence = 0;
  FCrowdStableEntityRef EntityRef;
  FCrowdWorkerPayload InitialState;

  bool IsValid(int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDespawnDelta
{
  uint64 InputSequence = 0;
  FCrowdStableEntityRef EntityRef;
  uint32 ReasonId = 0;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerCommandDelta
{
  uint64 InputSequence = 0;
  FCrowdStableEntityRef EntityRef;
  uint32 CommandId = 0;
  double EffectiveSimulationTimeSeconds = 0.0;
  FCrowdWorkerPayload Payload;

  bool IsValid(int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerStateDelta
{
  uint64 InputSequence = 0;
  FCrowdStableEntityRef EntityRef;
  uint64 DirtyMask = 0;
  FCrowdWorkerPayload FullState;

  bool IsValid(int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerResourceDelta
{
  uint64 InputSequence = 0;
  uint64 ResourceId = 0;
  uint64 Revision = 0;
  FCrowdWorkerPayload Payload;

  bool IsValid(int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerCorrectionDelta
{
  uint64 InputSequence = 0;
  FCrowdStableEntityRef EntityRef;
  uint64 CorrectionRevision = 0;
  uint64 DirtyMask = 0;
  FCrowdWorkerPayload FullState;

  bool IsValid(int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerInputBatch
{
  static constexpr uint32 CurrentVersion = 1;

  uint32 Version = CurrentVersion;
  uint64 Generation = 0;
  uint64 FirstInputSequence = 0;
  uint64 LastInputSequence = 0;
  double TargetSimulationTimeSeconds = 0.0;
  TArray<FCrowdWorkerSpawnDelta> Spawns;
  TArray<FCrowdWorkerDespawnDelta> Despawns;
  TArray<FCrowdWorkerCommandDelta> Commands;
  TArray<FCrowdWorkerStateDelta> StateDeltas;
  TArray<FCrowdWorkerResourceDelta> ResourceDeltas;
  TArray<FCrowdWorkerCorrectionDelta> Corrections;
  uint64 StableHash = 0;

  int32 GetRecordCount() const;
  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid(const FCrowdWorkerContractLimits& Limits) const;
};

enum class ECrowdWorkerInputAcceptResult : uint8
{
  Accepted = 0,
  AcceptedEmpty,
  AcceptedDuplicate,
  RejectedGeneration,
  RejectedStale,
  RequiresResnapshot
};

class MASSCROWDRUNTIME_API FCrowdWorkerInputSequenceGate
{
public:
  bool ResetForResnapshot(
    uint64 InGeneration,
    uint64 FirstExpectedInputSequence = 1);

  ECrowdWorkerInputAcceptResult Accept(
    const FCrowdWorkerInputBatch& Batch,
    const FCrowdWorkerContractLimits& Limits);

  uint64 GetGeneration() const { return Generation; }
  uint64 GetNextExpectedInputSequence() const { return NextExpectedSequence; }
  bool RequiresResnapshot() const { return bRequiresResnapshot; }

private:
  uint64 Generation = 0;
  uint64 NextExpectedSequence = 1;
  uint64 LastAcceptedFirstSequence = 0;
  uint64 LastAcceptedLastSequence = 0;
  uint64 LastAcceptedHash = 0;
  double LastTargetSimulationTimeSeconds = 0.0;
  bool bHasAcceptedBatch = false;
  bool bRequiresResnapshot = false;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerPublishedState
{
  uint64 StateRevision = 0;
  FCrowdWorkerPayload Payload;

  uint64 CalculateStableHash() const;
  bool IsValid(int32 MaxPayloadBytes) const;

  bool operator==(const FCrowdWorkerPublishedState& Other) const = default;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerStatePatch
{
  FCrowdStableEntityRef EntityRef;
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 SourceInputSequence = 0;
  uint64 DirtyMask = 0;
  FCrowdWorkerPublishedState State;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid(
    uint64 ExpectedGeneration,
    int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerGameplayEvent
{
  FCrowdStableEntityRef EntityRef;
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 SourceInputSequence = 0;
  uint64 EventSequence = 0;
  uint64 EventId = 0;
  FCrowdWorkerPayload Payload;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid(
    uint64 ExpectedGeneration,
    int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerPublishedBatch
{
  static constexpr uint32 CurrentVersion = 1;

  uint32 Version = CurrentVersion;
  uint64 Generation = 0;
  uint64 PublishSequence = 0;
  uint64 MinWorkerEpoch = 0;
  uint64 MaxWorkerEpoch = 0;
  uint64 LastAppliedInputSequence = 0;
  double PublishedSimulationTimeSeconds = 0.0;
  TArray<FCrowdWorkerStatePatch> StatePatches;
  TArray<FCrowdWorkerGameplayEvent> OrderedEvents;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
};

enum class ECrowdWorkerPublishedValidationResult : uint8
{
  Valid = 0,
  RejectedStructure,
  RejectedGeneration,
  RejectedPublishSequence,
  RejectedHash
};

class MASSCROWDRUNTIME_API FCrowdWorkerPublishedBatchValidator
{
public:
  static ECrowdWorkerPublishedValidationResult Validate(
    const FCrowdWorkerPublishedBatch& Batch,
    const FCrowdWorkerContractLimits& Limits,
    uint64 ExpectedGeneration,
    uint64 LastConsumedPublishSequence);
};
