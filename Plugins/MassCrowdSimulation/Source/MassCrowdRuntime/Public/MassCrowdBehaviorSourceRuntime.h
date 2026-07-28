#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSource.h"
#include "MassCrowdRuntimeBehavior.h"

namespace CrowdBuiltinCapabilityIds
{
  inline constexpr FCrowdCapabilityId Move{1};
  inline constexpr FCrowdCapabilityId Wander{2};
  inline constexpr FCrowdCapabilityId MoveTo{3};
  inline constexpr FCrowdCapabilityId Pursue{4};
  inline constexpr FCrowdCapabilityId Haul{5};
  inline constexpr FCrowdCapabilityId Attack{6};
  inline constexpr FCrowdCapabilityId Guard{7};
  inline constexpr FCrowdCapabilityId Flee{8};
  inline constexpr FCrowdCapabilityId RangedAttack{9};
  inline constexpr FCrowdCapabilityId NavLayer{10};
  inline constexpr FCrowdCapabilityId Face{11};
  inline constexpr FCrowdCapabilityId Formation{12};
  inline constexpr FCrowdCapabilityId CarryCargo{13};
  inline constexpr FCrowdCapabilityId React{14};
}

namespace CrowdBuiltinSourceTypeIds
{
  inline constexpr FCrowdBehaviorSourceTypeId MoveToSink{1001};
  inline constexpr FCrowdBehaviorSourceTypeId SharedFlow{1002};
  inline constexpr FCrowdBehaviorSourceTypeId FaceMovement{1101};
  inline constexpr FCrowdBehaviorSourceTypeId FaceTarget{1102};
  inline constexpr FCrowdBehaviorSourceTypeId Formation{1201};
  inline constexpr FCrowdBehaviorSourceTypeId CarryCargo{1301};
  inline constexpr FCrowdBehaviorSourceTypeId PickupInteraction{1401};
  inline constexpr FCrowdBehaviorSourceTypeId DeliverInteraction{1402};
  inline constexpr FCrowdBehaviorSourceTypeId AttackTarget{1501};
  inline constexpr FCrowdBehaviorSourceTypeId HitReaction{1601};
  inline constexpr FCrowdBehaviorSourceTypeId StunConstraint{1602};
  inline constexpr FCrowdBehaviorSourceTypeId DeathConstraint{1603};
}

namespace CrowdBuiltinBehaviorSchemas
{
  inline constexpr uint32 Standard = 1;
  inline constexpr FCrowdCapabilityProfileKey LegacyFullProfile{1};
}

struct FCrowdBuiltinBehaviorSourcePayload
{
  FVector Vector = FVector::ZeroVector;
  FCrowdStableEntityRef TargetRef;
  uint64 CommitId = 0;
  uint32 PrimaryId = 0;
  uint32 SecondaryId = 0;
  int32 Quantity = 0;
  uint32 Flags = 0;
};

static_assert(
  std::is_trivially_copyable_v<FCrowdBuiltinBehaviorSourcePayload>);
static_assert(
  sizeof(FCrowdBuiltinBehaviorSourcePayload)
  <= CrowdBehavior::MaxPayloadBytes);

struct FCrowdBehaviorSourceEvaluationContext
{
  int64 FixedStepIndex = INDEX_NONE;
  FCrowdResolvedCapabilitySet Capabilities;
  FCrowdBehaviorSourceInstance Instance;
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

  bool Succeeded() const { return bSucceeded; }

private:
  bool CanWrite(ECrowdBehaviorChannel Channel, int32 CurrentCount);

  const FCrowdBehaviorSourceSpec& Spec;
  FCrowdBehaviorContributionKey Key;
  FCrowdBehaviorContributions& Out;
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
  FCrowdBehaviorSourceSet StagedSourceSet;
  FCrowdResolvedBehaviorChannels ResolvedChannels;
  TArray<FCrowdBehaviorSourceEvent> Events;
  uint64 CommandBatchHash = 0;
  uint64 StableHash = 0;
};

struct FCrowdBehaviorPreparedBoundary
{
  int64 FixedStepIndex = INDEX_NONE;
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
  bool InitializeBuiltins();
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

private:
  FCrowdCapabilityProfileRegistry CapabilityProfiles;
  FCrowdBehaviorSourceEvaluatorRegistry Evaluators;
  TMap<FCrowdStableEntityRef, FCrowdBehaviorSourceSet> SourceSets;
  TMap<FCrowdStableEntityRef, FCrowdResolvedBehaviorChannels>
    LastResolvedChannels;
  TArray<FCrowdBehaviorSourceCommand> PendingCommands;
  TArray<FCrowdBehaviorCapabilityBindingUpdate> PendingBindingUpdates;
  TArray<FCrowdBehaviorSourceEvent> LastCommittedEvents;
  bool bInitialized = false;
};

class MASSCROWDRUNTIME_API FCrowdLegacyBehaviorRecipe
{
public:
  static bool BuildTransitionCommands(
    const FCrowdRuntimeBehaviorContext& Context,
    const FCrowdBehaviorSourceSet& CurrentSet,
    FCrowdBehaviorControllerId ControllerId,
    uint32& InOutNextCommandSequence,
    uint32& InOutNextSourceSequence,
    TArray<FCrowdBehaviorSourceCommand>& OutCommands);

  static FCrowdBehaviorSourceTypeId GetPrimarySourceType(
    ECrowdActiveBehavior Behavior);
};
