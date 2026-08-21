#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSource.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdWorkerAgentState.h"
#include "MassCrowdWorkerResultApply.h"
#include "MassCrowdWorkerRuntimeV2.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerBehaviorState
{
  static constexpr int32 MaxBusinessCommitIds = 64;
  int64 LastFixedStep = INDEX_NONE;
  uint64 LastAbsoluteSimulationTick = 0;
  uint64 LastConsumedCommandInputSequence = 0;
  uint64 LastCommandBatchHash = 0;
  uint64 BusinessCommitLedgerHash = 0;
  TArray<uint64> AppliedBusinessCommitIds;
  FCrowdBehaviorEntityEvaluationContext EvaluationContext;
  FCrowdBehaviorSourceSet SourceSet;
  FCrowdResolvedBehaviorChannels ResolvedChannels;

  bool IsValid() const
  {
    if (AppliedBusinessCommitIds.Num() > MaxBusinessCommitIds)
      return false;
    for (int32 Index = 0;
      Index < AppliedBusinessCommitIds.Num(); ++Index)
    {
      if (AppliedBusinessCommitIds[Index] == 0)
        return false;
      for (int32 Previous = 0; Previous < Index; ++Previous)
        if (AppliedBusinessCommitIds[Previous]
            == AppliedBusinessCommitIds[Index])
          return false;
    }
    return LastFixedStep >= 0
      && LastAbsoluteSimulationTick != 0
      && BusinessCommitLedgerHash != 0
      && EvaluationContext.IsValid()
      && EvaluationContext.EntityRef == SourceSet.EntityRef
      && SourceSet.IsValid()
      && ResolvedChannels.bValid
      && ResolvedChannels.StableHash != 0;
  }
};

class MASSCROWDRUNTIME_API FCrowdWorkerBehaviorInputCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574249u;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr int32 MaxEncodedBytes = 2048;

  static bool Encode(
    const FCrowdBehaviorEntityEvaluationContext& Context,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdBehaviorEntityEvaluationContext& OutContext);
};

class MASSCROWDRUNTIME_API FCrowdWorkerBehaviorBindingInputCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574242u;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr int32 MaxEncodedBytes = 256;

  static bool Encode(
    const FCrowdBehaviorCapabilityBindingUpdate& Update,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdBehaviorCapabilityBindingUpdate& OutUpdate);
};

class MASSCROWDRUNTIME_API FCrowdWorkerBehaviorEventCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574245u;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr int32 MaxEncodedBytes = 64;

  static bool Encode(
    const FCrowdBehaviorSourceEvent& Event,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdBehaviorSourceEvent& OutEvent);
};

class MASSCROWDRUNTIME_API FCrowdWorkerBehaviorStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574256u;
  static constexpr uint16 SchemaVersion = 3;
  static constexpr int32 MaxEncodedBytes = 8192;

  static bool Encode(
    const FCrowdWorkerBehaviorState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerBehaviorState& OutState);
};

class MASSCROWDRUNTIME_API FCrowdWorkerBusinessCommitEventCodec
{
public:
  static constexpr uint32 SchemaId = 0x4357424Du;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr int32 MaxEncodedBytes = 128;

  static bool Encode(
    const FCrowdBusinessContribution& Contribution,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdBusinessContribution& OutContribution);
};

class MASSCROWDRUNTIME_API
  FCrowdWorkerLifecycleDomainExecutor final
    : public ICrowdWorkerDomainExecutor
{
public:
  virtual ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::LifecycleInput;
  }

  virtual void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies)
    const override
  {
    OutDependencies.Reset();
  }

  virtual bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;
};

class MASSCROWDRUNTIME_API
  FCrowdWorkerBehaviorDomainExecutor final
    : public ICrowdWorkerDomainExecutor
{
public:
  FCrowdWorkerBehaviorDomainExecutor(
    const FCrowdCapabilityProfileRegistry& InCapabilityProfiles,
    const FCrowdBehaviorSourceEvaluatorRegistry& InEvaluators);

  virtual ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::Behavior;
  }

  virtual void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies)
    const override
  {
    OutDependencies = {
      ECrowdWorkerDomainId::LifecycleInput};
  }

  virtual bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;

private:
  FCrowdCapabilityProfileRegistry CapabilityProfiles;
  FCrowdBehaviorSourceEvaluatorRegistry Evaluators;
};

enum class ECrowdWorkerBehaviorAuthorityMode : uint8
{
  Shadow = 0,
  Canary,
  Production
};

enum class ECrowdWorkerBehaviorValidationResult : uint8
{
  NoExpectation = 0,
  Pending,
  Matched,
  Violation
};

struct MASSCROWDRUNTIME_API FCrowdWorkerBehaviorAuthorityMetrics
{
  uint64 Generation = 0;
  uint64 QueuedExpectationCount = 0;
  uint64 MatchedExpectationCount = 0;
  uint64 LastMatchedInputSequence = 0;
  uint64 MismatchCount = 0;
  uint64 IngestedOrderedEventCount = 0;
  uint64 ConsumedOrderedEventCount = 0;
  int32 PendingExpectationCount = 0;
  int32 CanaryEntityCount = 0;
  bool bViolation = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerBehaviorAuthority
{
public:
  bool ResetQuiescent(
    uint64 Generation,
    ECrowdWorkerBehaviorAuthorityMode Mode,
    TConstArrayView<FCrowdStableEntityRef> CanaryEntities = {},
    int32 MaxPendingExpectations = 16);

  bool UpdateCurrentEntities(
    uint64 Generation,
    TConstArrayView<FCrowdStableEntityRef> EntityRefs);

  bool GetEntitiesRequiringInitialBinding(
    uint64 Generation,
    TArray<FCrowdStableEntityRef>& OutEntityRefs) const;

  bool MarkSubmittedBindings(
    uint64 Generation,
    TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
      BindingUpdates);

  bool QueueCommittedExpectation(
    uint64 Generation,
    uint64 InputSequence,
    const FCrowdBehaviorSourceRuntime& Runtime,
    TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
      CommittedContexts,
    bool bRequireKinematicParity = true,
    bool bRequireSourceStateParity = true);

  bool QueuePreparedExpectation(
    uint64 Generation,
    uint64 InputSequence,
    const FCrowdBehaviorPreparedBoundary& Prepared,
    TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
      StagedContexts,
    bool bRequireKinematicParity = true,
    bool bRequireSourceParity = true);

  bool QueueAutonomousExpectation(
    uint64 Generation,
    uint64 InputSequence,
    TConstArrayView<FCrowdStableEntityRef> EntityRefs,
    bool bCaptureEvents = true);

  bool IngestOrderedEvents(
    TConstArrayView<FCrowdWorkerGameplayEvent> Events);

  bool PeekMatchedEvents(
    uint64 InputSequence,
    TArray<FCrowdBehaviorSourceEvent>& OutEvents,
    TArray<FCrowdBusinessContribution>& OutBusinessCommits) const;
  bool AcknowledgeMatchedEvents(uint64 InputSequence);

  ECrowdWorkerBehaviorValidationResult ValidateAvailable(
    const FCrowdWorkerResultApplyProxy& Proxy);

  bool IsWorkerOwner(const FCrowdStableEntityRef& EntityRef) const;
  ECrowdWorkerBehaviorAuthorityMode GetMode() const
  {
    return Mode;
  }
  const FCrowdWorkerBehaviorAuthorityMetrics& GetMetrics() const
  {
    return Metrics;
  }

private:
  struct FExpectedEntity
  {
    FCrowdStableEntityRef EntityRef;
    uint64 SourceSetHash = 0;
    uint64 SourceSetContentHash = 0;
    uint64 SourceSetControlHash = 0;
    uint64 SourceStateTraceHash = 0;
    uint64 SourceTimelineTraceHash = 0;
    uint64 SourceCursorTraceHash = 0;
    uint64 ResolvedChannelsHash = 0;
    uint64 EvaluationContextHash = 0;
    FVector EvaluationPosition = FVector::ZeroVector;
    FVector EvaluationVelocity = FVector::ZeroVector;
    uint64 FirstContextRecordHash = 0;
    int32 EvaluationContextRecordCount = 0;
    uint32 SourceSetRevision = 0;
    int32 SourceInstanceCount = 0;
  };

  struct FExpectation
  {
    uint64 InputSequence = 0;
    TArray<FExpectedEntity> Entities;
    bool bRequireContent = true;
    bool bRequireKinematicParity = true;
    bool bRequireSourceParity = true;
    bool bRequireSourceStateParity = true;
    bool bCaptureEvents = true;
  };

  struct FPendingBehaviorEvent
  {
    uint64 SourceInputSequence = 0;
    uint64 EventSequence = 0;
    FCrowdBehaviorSourceEvent Event;
  };

  struct FMatchedEventBatch
  {
    uint64 InputSequence = 0;
    TArray<FCrowdBehaviorSourceEvent> Events;
    TArray<FCrowdBusinessContribution> BusinessCommits;
  };

  struct FPendingBusinessCommit
  {
    uint64 SourceInputSequence = 0;
    uint64 EventSequence = 0;
    FCrowdBusinessContribution Contribution;
  };

  void LatchViolation();

  TSet<FCrowdStableEntityRef> CurrentEntities;
  TSet<FCrowdStableEntityRef> BoundEntities;
  TSet<FCrowdStableEntityRef> CanaryEntities;
  TArray<FExpectation> Expectations;
  TArray<FPendingBehaviorEvent> PendingBehaviorEvents;
  TArray<FPendingBusinessCommit> PendingBusinessCommits;
  TArray<FMatchedEventBatch> MatchedEventBatches;
  FCrowdWorkerBehaviorAuthorityMetrics Metrics;
  ECrowdWorkerBehaviorAuthorityMode Mode =
    ECrowdWorkerBehaviorAuthorityMode::Shadow;
  int32 MaxPendingExpectations = 0;
  uint64 LastIngestedBehaviorEventSequence = 0;
  bool bInitialized = false;
};
