#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
#include "MassCrowdWorkerContracts.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerPublishMetadata
{
  uint64 Generation = 0;
  uint64 PublishSequence = 0;
  uint64 MinWorkerEpoch = 0;
  uint64 MaxWorkerEpoch = 0;
  uint64 LastAppliedInputSequence = 0;
  double PublishedSimulationTimeSeconds = 0.0;
};

enum class ECrowdWorkerAppendResult : uint8
{
  Appended = 0,
  ReplacedNewer,
  MergedEquivalent,
  IgnoredStale,
  RejectedNotInitialized,
  Violation
};

enum class ECrowdWorkerPublishResult : uint8
{
  Published = 0,
  DeferredPublishedOccupied,
  RejectedNotInitialized,
  Violation
};

enum class ECrowdWorkerExchangeResult : uint8
{
  Exchanged = 0,
  NoPublishedBatch,
  RejectedNotInitialized,
  RejectedGeneration,
  RejectedConsumerFrame,
  Violation
};

class MASSCROWDRUNTIME_API FCrowdWorkerPublishedExchange
{
public:
  FCrowdWorkerPublishedExchange() = default;

  FCrowdWorkerPublishedExchange(
    const FCrowdWorkerPublishedExchange&) = delete;
  FCrowdWorkerPublishedExchange& operator=(
    const FCrowdWorkerPublishedExchange&) = delete;

  // The caller must quiesce the single producer and single consumer before
  // reset. Existing consuming pointers become invalid.
  bool ResetQuiescent(
    uint64 InGeneration,
    const FCrowdWorkerContractLimits& InLimits);

  ECrowdWorkerAppendResult AppendStatePatch(
    const FCrowdWorkerStatePatch& Patch);
  ECrowdWorkerAppendResult AppendOrderedEvent(
    const FCrowdWorkerGameplayEvent& Event);

  ECrowdWorkerPublishResult TryPublishBuildingBatch(
    const FCrowdWorkerPublishMetadata& Metadata);

  // OutBatch remains immutable and valid until the next exchange attempt or
  // ResetQuiescent. ConsumerFrameSequence is a monotonically increasing GT
  // frame identity; every value may be attempted only once.
  ECrowdWorkerExchangeResult TryExchangePublishedBatch(
    uint64 ExpectedGeneration,
    uint64 ConsumerFrameSequence,
    const FCrowdWorkerPublishedBatch*& OutBatch);

  bool IsInitialized() const { return bInitialized; }
  bool HasViolation() const { return bViolation.Load(); }
  uint64 GetGeneration() const { return Generation; }

private:
  static bool IsNewerPatch(
    const FCrowdWorkerStatePatch& Candidate,
    const FCrowdWorkerStatePatch& Existing);
  int32 FindFreeSlot() const;
  void LatchViolation();

  FCrowdWorkerPublishedBatch Buffers[3];
  TMap<FCrowdStableEntityRef, int32> BuildingPatchIndices;
  FCrowdWorkerContractLimits Limits;
  mutable FCriticalSection ExchangeMutex;
  TAtomic<bool> bViolation{false};
  uint64 Generation = 0;
  uint64 LastPublishedSequence = 0;
  uint64 LastAcceptedEventSequence = 0;
  uint64 LastConsumerFrameSequence = 0;
  int32 BuildingIndex = 0;
  int32 PublishedIndex = INDEX_NONE;
  int32 ConsumingIndex = INDEX_NONE;
  int32 PendingOrderedEventCount = 0;
  bool bInitialized = false;
  bool bHasConsumerAttempt = false;
};
