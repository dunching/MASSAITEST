#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSource.h"

struct MASSCROWDRUNTIME_API FCrowdBehaviorSourceEvaluationContext
{
  int64 FixedStepIndex = INDEX_NONE;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  FVector Facing = FVector::ForwardVector;
  FCrowdResolvedCapabilitySet Capabilities;
  FCrowdBehaviorSourceInstance Instance;
  TConstArrayView<FCrowdBehaviorContextRecord> ContextRecords;

  const FCrowdBehaviorContextRecord* FindContext(
    FCrowdBehaviorContextTypeId TypeId) const;
};

struct FCrowdBehaviorContextSchema
{
  FCrowdBehaviorContextTypeId TypeId;
  uint16 Version = 0;
  uint16 Size = 0;

  bool IsValid() const
  {
    return TypeId.IsValid() && Version != 0
      && Size <= CrowdBehavior::MaxContextRecordBytes;
  }
  bool operator==(const FCrowdBehaviorContextSchema& Other) const = default;
};

struct MASSCROWDRUNTIME_API FCrowdBehaviorEntityEvaluationContext
{
  FCrowdStableEntityRef EntityRef;
  int64 FixedStepIndex = INDEX_NONE;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  FVector Facing = FVector::ForwardVector;
  TArray<FCrowdBehaviorContextRecord, TInlineAllocator<
    CrowdBehavior::MaxContextRecordsPerEntity>> Records;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

class MASSCROWDRUNTIME_API FCrowdBehaviorContributionWriter
{
public:
  FCrowdBehaviorContributionWriter(
    const FCrowdBehaviorSourceSpec& Spec,
    const FCrowdBehaviorSourceInstance& Instance,
    FCrowdBehaviorContributions& OutContributions);

  bool AddMovement(FCrowdMovementContribution Contribution);
  bool AddFacing(FCrowdFacingContribution Contribution);
  bool AddConstraint(FCrowdConstraintContribution Contribution);
  bool AddInteraction(FCrowdInteractionContribution Contribution);
  bool AddBusiness(FCrowdBusinessContribution Contribution);
  bool AddPresentation(FCrowdPresentationContribution Contribution);
  bool SetNextState(const FCrowdBehaviorSourceState& State);

  bool Succeeded() const { return bSucceeded; }
  bool HasNextState() const { return bHasNextState; }
  const FCrowdBehaviorSourceState& GetNextState() const
  {
    return NextState;
  }

private:
  bool CanWrite(ECrowdBehaviorChannel Channel, int32 CurrentCount);

  const FCrowdBehaviorSourceSpec& Spec;
  FCrowdBehaviorContributionKey Key;
  FCrowdBehaviorContributions& Out;
  FCrowdBehaviorSourceState NextState;
  bool bHasNextState = false;
  bool bSucceeded = true;
};

class MASSCROWDRUNTIME_API ICrowdBehaviorSourceEvaluator
{
public:
  virtual ~ICrowdBehaviorSourceEvaluator() = default;
  virtual bool Evaluate(
    const FCrowdBehaviorSourceEvaluationContext& Context,
    FCrowdBehaviorContributionWriter& Writer) const = 0;
};

class MASSCROWDRUNTIME_API FCrowdBehaviorSourceEvaluatorRegistry
{
public:
  bool Register(
    const FCrowdBehaviorSourceSpec& Spec,
    TSharedRef<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
      Evaluator);
  bool Freeze();

  const FCrowdBehaviorSourceSpec* FindSpec(
    FCrowdBehaviorSourceTypeId TypeId) const;
  TSharedPtr<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
    FindEvaluator(FCrowdBehaviorSourceTypeId TypeId) const;

  const FCrowdBehaviorSourceSpecRegistry& GetSpecs() const
  {
    return Specs;
  }
  bool IsFrozen() const { return bFrozen; }
  uint64 CalculateStableHash() const;

private:
  FCrowdBehaviorSourceSpecRegistry Specs;
  TMap<
    FCrowdBehaviorSourceTypeId,
    TSharedPtr<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>>
      Evaluators;
  bool bFrozen = false;
};

class MASSCROWDRUNTIME_API FCrowdBehaviorRegistryBuilder
{
public:
  FCrowdBehaviorRegistryBuilder(
    FCrowdCapabilityProfileRegistry& InProfiles,
    FCrowdBehaviorSourceEvaluatorRegistry& InEvaluators,
    TMap<FCrowdBehaviorContextTypeId, FCrowdBehaviorContextSchema>&
      InContextSchemas)
    : Profiles(InProfiles)
    , Evaluators(InEvaluators)
    , ContextSchemas(InContextSchemas)
  {
  }

  bool RegisterProfile(FCrowdCapabilityProfile Profile);
  bool RegisterSource(
    const FCrowdBehaviorSourceSpec& Spec,
    TSharedRef<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
      Evaluator);
  bool RegisterContextSchema(const FCrowdBehaviorContextSchema& Schema);

private:
  FCrowdCapabilityProfileRegistry& Profiles;
  FCrowdBehaviorSourceEvaluatorRegistry& Evaluators;
  TMap<FCrowdBehaviorContextTypeId, FCrowdBehaviorContextSchema>&
    ContextSchemas;
};

class MASSCROWDRUNTIME_API ICrowdBehaviorSourceProvider
{
public:
  virtual ~ICrowdBehaviorSourceProvider() = default;
  virtual FCrowdBehaviorProviderId GetProviderId() const = 0;
  virtual bool Register(FCrowdBehaviorRegistryBuilder& Builder) const = 0;
};

MASSCROWDRUNTIME_API bool RegisterCrowdBehaviorSourceProvider(
  TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
    Provider);
MASSCROWDRUNTIME_API bool UnregisterCrowdBehaviorSourceProvider(
  FCrowdBehaviorProviderId ProviderId);

struct FCrowdBehaviorCapabilityBindingUpdate
{
  int64 EffectiveFixedStep = INDEX_NONE;
  FCrowdStableEntityRef EntityRef;
  FCrowdCapabilityBinding Binding;
  uint64 StableHash = 0;

  bool IsValid() const;
};

struct FCrowdBehaviorPreparedEntity
{
  FCrowdStableEntityRef EntityRef;
  uint64 BaseSourceSetHash = 0;
  uint64 EvaluationContextHash = 0;
  FCrowdBehaviorSourceSet StagedSourceSet;
  FCrowdResolvedBehaviorChannels ResolvedChannels;
  TArray<FCrowdBehaviorSourceEvent> Events;
  uint64 CommandBatchHash = 0;
  uint64 StableHash = 0;
};

struct FCrowdBehaviorPreparedBoundary
{
  int64 FixedStepIndex = INDEX_NONE;
  uint64 RegistryHash = 0;
  TArray<FCrowdBehaviorPreparedEntity> Entities;
  uint64 SourceSetHash = 0;
  uint64 CommandBatchHash = 0;
  uint64 ResolvedChannelHash = 0;
  uint64 StableHash = 0;
  bool bValid = false;
};

class MASSCROWDRUNTIME_API FCrowdBehaviorSourceRuntime
{
public:
  bool InitializeFromRegisteredProviders();
  void Reset();

  bool RegisterEntity(
    FCrowdStableEntityRef EntityRef,
    const FCrowdCapabilityBinding& Binding);
  bool RemoveEntity(FCrowdStableEntityRef EntityRef);
  bool QueueCommand(const FCrowdBehaviorSourceCommand& Command);
  bool QueueCapabilityBinding(
    int64 EffectiveFixedStep,
    FCrowdStableEntityRef EntityRef,
    const FCrowdCapabilityBinding& Binding);
  bool SetEvaluationContext(
    const FCrowdBehaviorEntityEvaluationContext& Context);

  bool PrepareBoundary(
    int64 FixedStepIndex,
    FCrowdBehaviorPreparedBoundary& OutPrepared) const;
  bool ValidatePrepared(
    const FCrowdBehaviorPreparedBoundary& Prepared) const;
  bool CommitPrepared(
    const FCrowdBehaviorPreparedBoundary& Prepared);

  int32 GetPendingCommandCount() const
  {
    return PendingCommands.Num();
  }
  void RollbackPendingCommandsTo(int32 Count);

  const FCrowdBehaviorSourceSet* FindSourceSet(
    FCrowdStableEntityRef EntityRef) const;
  bool ApplyReplicatedSourceSet(
    const FCrowdBehaviorSourceSet& ReplicatedSet);
  const FCrowdResolvedBehaviorChannels* FindResolvedChannels(
    FCrowdStableEntityRef EntityRef) const;
  bool IsSourceActive(
    const FCrowdBehaviorSourceHandle& Handle) const;
  bool HasCommittedEvent(
    const FCrowdBehaviorSourceHandle& Handle,
    ECrowdBehaviorSourceEventKind Kind,
    int64 MinimumFixedStep) const;

  const FCrowdCapabilityProfileRegistry& GetCapabilityProfiles() const
  {
    return CapabilityProfiles;
  }
  const FCrowdBehaviorSourceEvaluatorRegistry& GetEvaluators() const
  {
    return Evaluators;
  }
  uint64 GetRegistryHash() const { return RegistryHash; }
  uint64 GetContextSchemaHash() const { return ContextSchemaHash; }

private:
  FCrowdCapabilityProfileRegistry CapabilityProfiles;
  FCrowdBehaviorSourceEvaluatorRegistry Evaluators;
  TMap<FCrowdBehaviorContextTypeId, FCrowdBehaviorContextSchema>
    ContextSchemas;
  TMap<FCrowdStableEntityRef, FCrowdBehaviorEntityEvaluationContext>
    EvaluationContexts;
  TMap<FCrowdStableEntityRef, FCrowdBehaviorSourceSet> SourceSets;
  TMap<FCrowdStableEntityRef, FCrowdResolvedBehaviorChannels>
    LastResolvedChannels;
  TArray<FCrowdBehaviorSourceCommand> PendingCommands;
  TArray<FCrowdBehaviorCapabilityBindingUpdate> PendingBindingUpdates;
  TArray<FCrowdBehaviorSourceEvent> LastCommittedEvents;
  uint64 RegistryHash = 0;
  uint64 ContextSchemaHash = 0;
  bool bInitialized = false;
};
