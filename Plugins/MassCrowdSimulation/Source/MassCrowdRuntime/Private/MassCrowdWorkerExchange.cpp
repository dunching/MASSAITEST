#include "MassCrowdWorkerExchange.h"

#include "Misc/ScopeLock.h"

namespace CrowdWorkerExchangePrivate
{
  bool ExchangeIsFiniteNonNegative(const double Value)
  {
    return FMath::IsFinite(Value) && Value >= 0.0;
  }
}

using namespace CrowdWorkerExchangePrivate;

bool FCrowdWorkerPublishedExchange::ResetQuiescent(
  const uint64 InGeneration,
  const FCrowdWorkerContractLimits& InLimits,
  const uint64 InLastPublishedSequence,
  const uint64 InLastAcceptedEventSequence,
  const uint64 InLastConsumerFrameSequence)
{
  if (InGeneration == 0 || !InLimits.IsValid())
    return false;

  FScopeLock Lock(&ExchangeMutex);
  for (FCrowdWorkerPublishedBatch& Buffer : Buffers)
    Buffer = {};
  BuildingPatchIndices.Reset();
  Limits = InLimits;
  Generation = InGeneration;
  LastPublishedSequence = InLastPublishedSequence;
  LastAcceptedEventSequence = InLastAcceptedEventSequence;
  LastConsumerFrameSequence = InLastConsumerFrameSequence;
  BuildingIndex = 0;
  PublishedIndex = INDEX_NONE;
  ConsumingIndex = INDEX_NONE;
  PendingOrderedEventCount = 0;
  bHasConsumerAttempt = InLastConsumerFrameSequence != 0;
  bViolation.Store(false);
  bInitialized = true;
  return true;
}

bool FCrowdWorkerPublishedExchange::IsNewerPatch(
  const FCrowdWorkerStatePatch& Candidate,
  const FCrowdWorkerStatePatch& Existing)
{
  return Candidate.WorkerEpoch > Existing.WorkerEpoch
    || (Candidate.WorkerEpoch == Existing.WorkerEpoch
      && Candidate.SourceInputSequence
        > Existing.SourceInputSequence);
}

void FCrowdWorkerPublishedExchange::LatchViolation()
{
  bViolation.Store(true);
}

ECrowdWorkerAppendResult
FCrowdWorkerPublishedExchange::AppendStatePatch(
  const FCrowdWorkerStatePatch& Patch)
{
  if (!bInitialized)
    return ECrowdWorkerAppendResult::RejectedNotInitialized;
  if (bViolation.Load())
    return ECrowdWorkerAppendResult::Violation;
  if (Patch.Generation != Generation)
  {
    LatchViolation();
    return ECrowdWorkerAppendResult::Violation;
  }
  if (!Patch.IsValid(Generation, Limits.MaxPayloadBytes))
  {
    LatchViolation();
    return ECrowdWorkerAppendResult::Violation;
  }

  FCrowdWorkerPublishedBatch& Building = Buffers[BuildingIndex];
  const FCrowdWorkerStatePatchKey PatchKey{
    Patch.EntityRef, Patch.StateFieldId};
  if (const int32* ExistingIndex =
    BuildingPatchIndices.Find(PatchKey))
  {
    FCrowdWorkerStatePatch& Existing =
      Building.StatePatches[*ExistingIndex];
    if (IsNewerPatch(Patch, Existing))
    {
      const uint64 CombinedDirtyMask =
        Existing.DirtyMask | Patch.DirtyMask;
      Existing = Patch;
      Existing.DirtyMask = CombinedDirtyMask;
      Existing.RecalculateStableHash();
      return ECrowdWorkerAppendResult::ReplacedNewer;
    }
    if (Patch.WorkerEpoch == Existing.WorkerEpoch
      && Patch.SourceInputSequence == Existing.SourceInputSequence)
    {
      if (Patch.State != Existing.State)
      {
        LatchViolation();
        return ECrowdWorkerAppendResult::Violation;
      }
      Existing.DirtyMask |= Patch.DirtyMask;
      Existing.RecalculateStableHash();
      return ECrowdWorkerAppendResult::MergedEquivalent;
    }
    return ECrowdWorkerAppendResult::IgnoredStale;
  }

  if (Building.StatePatches.Num()
    >= Limits.MaxStatePatchesPerSlot)
  {
    LatchViolation();
    return ECrowdWorkerAppendResult::Violation;
  }
  const int32 NewIndex = Building.StatePatches.Add(Patch);
  BuildingPatchIndices.Add(PatchKey, NewIndex);
  return ECrowdWorkerAppendResult::Appended;
}

ECrowdWorkerAppendResult
FCrowdWorkerPublishedExchange::AppendOrderedEvent(
  const FCrowdWorkerGameplayEvent& Event)
{
  if (!bInitialized)
    return ECrowdWorkerAppendResult::RejectedNotInitialized;
  if (bViolation.Load())
    return ECrowdWorkerAppendResult::Violation;
  if (Event.Generation != Generation)
  {
    LatchViolation();
    return ECrowdWorkerAppendResult::Violation;
  }
  if (!Event.IsValid(Generation, Limits.MaxPayloadBytes))
  {
    LatchViolation();
    return ECrowdWorkerAppendResult::Violation;
  }

  FScopeLock Lock(&ExchangeMutex);
  if (bViolation.Load())
    return ECrowdWorkerAppendResult::Violation;
  if (PendingOrderedEventCount
      >= Limits.MaxPendingOrderedEvents
    || Event.EventSequence != LastAcceptedEventSequence + 1)
  {
    LatchViolation();
    return ECrowdWorkerAppendResult::Violation;
  }
  Buffers[BuildingIndex].OrderedEvents.Add(Event);
  ++PendingOrderedEventCount;
  LastAcceptedEventSequence = Event.EventSequence;
  return ECrowdWorkerAppendResult::Appended;
}

int32 FCrowdWorkerPublishedExchange::FindFreeSlot() const
{
  for (int32 Index = 0; Index < UE_ARRAY_COUNT(Buffers); ++Index)
    if (Index != BuildingIndex
      && Index != PublishedIndex
      && Index != ConsumingIndex)
      return Index;
  return INDEX_NONE;
}

ECrowdWorkerPublishResult
FCrowdWorkerPublishedExchange::TryPublishBuildingBatch(
  const FCrowdWorkerPublishMetadata& Metadata)
{
  if (!bInitialized)
    return ECrowdWorkerPublishResult::RejectedNotInitialized;
  if (bViolation.Load())
    return ECrowdWorkerPublishResult::Violation;
  if (Metadata.Generation != Generation)
  {
    LatchViolation();
    return ECrowdWorkerPublishResult::Violation;
  }
  if (Metadata.PublishSequence != LastPublishedSequence + 1)
  {
    LatchViolation();
    return ECrowdWorkerPublishResult::Violation;
  }
  if (Metadata.MinWorkerEpoch == 0
    || Metadata.MaxWorkerEpoch < Metadata.MinWorkerEpoch
    || !ExchangeIsFiniteNonNegative(
      Metadata.PublishedSimulationTimeSeconds))
  {
    LatchViolation();
    return ECrowdWorkerPublishResult::Violation;
  }

  {
    FScopeLock Lock(&ExchangeMutex);
    if (PublishedIndex != INDEX_NONE)
      return ECrowdWorkerPublishResult::DeferredPublishedOccupied;
  }

  FCrowdWorkerPublishedBatch& Building = Buffers[BuildingIndex];
  Building.StatePatches.Sort([](
    const FCrowdWorkerStatePatch& A,
    const FCrowdWorkerStatePatch& B)
  {
    if (A.EntityRef != B.EntityRef)
      return A.EntityRef < B.EntityRef;
    return A.StateFieldId < B.StateFieldId;
  });
  Building.Version = FCrowdWorkerPublishedBatch::CurrentVersion;
  Building.Generation = Metadata.Generation;
  Building.PublishSequence = Metadata.PublishSequence;
  uint64 BuildingMinEpoch = Metadata.MinWorkerEpoch;
  uint64 BuildingMaxEpoch = Metadata.MaxWorkerEpoch;
  for (const FCrowdWorkerStatePatch& Patch :
    Building.StatePatches)
  {
    BuildingMinEpoch = FMath::Min(
      BuildingMinEpoch, Patch.WorkerEpoch);
    BuildingMaxEpoch = FMath::Max(
      BuildingMaxEpoch, Patch.WorkerEpoch);
  }
  for (const FCrowdWorkerGameplayEvent& Event :
    Building.OrderedEvents)
  {
    BuildingMinEpoch = FMath::Min(
      BuildingMinEpoch, Event.WorkerEpoch);
    BuildingMaxEpoch = FMath::Max(
      BuildingMaxEpoch, Event.WorkerEpoch);
  }
  Building.MinWorkerEpoch = BuildingMinEpoch;
  Building.MaxWorkerEpoch = BuildingMaxEpoch;
  Building.LastAppliedInputSequence =
    Metadata.LastAppliedInputSequence;
  Building.PublishedSimulationTimeSeconds =
    Metadata.PublishedSimulationTimeSeconds;
  Building.RecalculateStableHash();

  const ECrowdWorkerPublishedValidationResult ValidationResult =
    FCrowdWorkerPublishedBatchValidator::Validate(
      Building, Limits, Generation, LastPublishedSequence);
  if (ValidationResult
      != ECrowdWorkerPublishedValidationResult::Valid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerExchangePublishRejected validation=%u generation=%llu publish=%llu epochs=%llu/%llu last_input=%llu patches=%d events=%d stable_hash=%llu"),
      static_cast<uint32>(ValidationResult),
      Building.Generation,
      Building.PublishSequence,
      Building.MinWorkerEpoch,
      Building.MaxWorkerEpoch,
      Building.LastAppliedInputSequence,
      Building.StatePatches.Num(),
      Building.OrderedEvents.Num(),
      Building.StableHash);
    LatchViolation();
    return ECrowdWorkerPublishResult::Violation;
  }

  FScopeLock Lock(&ExchangeMutex);
  if (bViolation.Load())
    return ECrowdWorkerPublishResult::Violation;
  if (PublishedIndex != INDEX_NONE)
    return ECrowdWorkerPublishResult::DeferredPublishedOccupied;
  const int32 FreeIndex = FindFreeSlot();
  if (FreeIndex == INDEX_NONE)
  {
    LatchViolation();
    return ECrowdWorkerPublishResult::Violation;
  }

  PublishedIndex = BuildingIndex;
  BuildingIndex = FreeIndex;
  Buffers[BuildingIndex] = {};
  BuildingPatchIndices.Reset();
  LastPublishedSequence = Metadata.PublishSequence;
  return ECrowdWorkerPublishResult::Published;
}

ECrowdWorkerExchangeResult
FCrowdWorkerPublishedExchange::TryExchangePublishedBatch(
  const uint64 ExpectedGeneration,
  const uint64 ConsumerFrameSequence,
  const FCrowdWorkerPublishedBatch*& OutBatch)
{
  OutBatch = nullptr;
  if (!bInitialized)
    return ECrowdWorkerExchangeResult::RejectedNotInitialized;

  FScopeLock Lock(&ExchangeMutex);
  if (ExpectedGeneration != Generation)
    return ECrowdWorkerExchangeResult::RejectedGeneration;
  if (ConsumerFrameSequence == 0
    || (bHasConsumerAttempt
      && ConsumerFrameSequence <= LastConsumerFrameSequence))
    return ECrowdWorkerExchangeResult::RejectedConsumerFrame;

  bHasConsumerAttempt = true;
  LastConsumerFrameSequence = ConsumerFrameSequence;
  if (ConsumingIndex != INDEX_NONE)
  {
    Buffers[ConsumingIndex] = {};
    ConsumingIndex = INDEX_NONE;
  }
  if (bViolation.Load())
    return ECrowdWorkerExchangeResult::Violation;
  if (PublishedIndex == INDEX_NONE)
    return ECrowdWorkerExchangeResult::NoPublishedBatch;

  ConsumingIndex = PublishedIndex;
  PublishedIndex = INDEX_NONE;
  PendingOrderedEventCount -=
    Buffers[ConsumingIndex].OrderedEvents.Num();
  if (PendingOrderedEventCount < 0)
  {
    PendingOrderedEventCount = 0;
    LatchViolation();
    return ECrowdWorkerExchangeResult::Violation;
  }
  OutBatch = &Buffers[ConsumingIndex];
  return ECrowdWorkerExchangeResult::Exchanged;
}
