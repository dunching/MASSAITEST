#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

enum class ECrowdBehaviorTargetKind : uint8
{
  None = 0,
  Location,
  Entity,
  Count
};

enum class ECrowdInteractionIntentKind : uint8
{
  None = 0,
  Move,
  Pursue,
  Pickup,
  Deliver,
  Attack,
  Guard,
  Flee,
  Count
};

enum class ECrowdBusinessCommitKind : uint8
{
  None = 0,
  CargoPickup,
  CargoDeliver,
  CombatHit,
  Count
};

struct MASSCROWDRUNTIME_API FCrowdBehaviorTarget
{
  ECrowdBehaviorTargetKind Kind = ECrowdBehaviorTargetKind::None;
  FCrowdStableEntityRef EntityRef;
  FVector Location = FVector::ZeroVector;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdBehaviorObjective
{
  uint32 ObjectiveKey = 0;
  FVector Location = FVector::ZeroVector;
  uint32 AcceptanceRadiusCm = 0;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdInteractionIntent
{
  ECrowdInteractionIntentKind Kind = ECrowdInteractionIntentKind::None;
  FCrowdStableEntityRef TargetRef;
  uint32 PayloadKey = 0;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdBusinessCommitRequest
{
  ECrowdBusinessCommitKind Kind = ECrowdBusinessCommitKind::None;
  uint64 CommitId = 0;
  int64 FixedStepIndex = INDEX_NONE;
  uint32 TransitionRevision = 0;
  FCrowdStableEntityRef AgentRef;
  FCrowdStableEntityRef TaskRef;
  FCrowdStableEntityRef TargetRef;
  uint32 PayloadKey = 0;
  int32 Quantity = 0;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdRuntimeBehaviorContext
{
  FCrowdAgentFacts AgentFacts;
  ECrowdActiveBehavior RequestedBehavior = ECrowdActiveBehavior::Idle;
  int64 FixedStepIndex = 0;
  uint32 TransitionRevision = 1;
  FCrowdStableEntityRef TaskRef;
  FCrowdStableEntityRef TargetRef;
  FVector TargetLocation = FVector::ZeroVector;
  uint32 ObjectiveKey = 0;
  uint32 MovementProfileKey = 0;
  uint32 InteractionPayloadKey = 0;
  int32 InteractionQuantity = 0;
  uint64 ExternalCommitId = 0;
  bool bInteractionReady = false;
};

struct MASSCROWDRUNTIME_API FCrowdRuntimeBehaviorOutput
{
  ECrowdActiveBehavior Behavior = ECrowdActiveBehavior::Idle;
  FCrowdBehaviorTarget Target;
  FCrowdBehaviorObjective Objective;
  uint32 MovementProfileKey = 0;
  FCrowdInteractionIntent InteractionIntent;
  FCrowdBusinessCommitRequest BusinessCommitRequest;

  bool IsValidFor(const FCrowdRuntimeBehaviorContext& Context) const;
};

class MASSCROWDRUNTIME_API ICrowdRuntimeBehaviorProvider
{
public:
  virtual ~ICrowdRuntimeBehaviorProvider() = default;
  virtual bool Supports(ECrowdActiveBehavior Behavior) const = 0;
  virtual bool Evaluate(
    const FCrowdRuntimeBehaviorContext& Context,
    FCrowdRuntimeBehaviorOutput& OutOutput) const = 0;
};

class MASSCROWDRUNTIME_API FCrowdRuntimeBasicBehaviorProvider final
  : public ICrowdRuntimeBehaviorProvider
{
public:
  virtual bool Supports(ECrowdActiveBehavior Behavior) const override;
  virtual bool Evaluate(
    const FCrowdRuntimeBehaviorContext& Context,
    FCrowdRuntimeBehaviorOutput& OutOutput) const override;
};

class MASSCROWDRUNTIME_API FCrowdRuntimeBehaviorTransition
{
public:
  static bool Evaluate(
    const ICrowdRuntimeBehaviorProvider& Provider,
    const FCrowdRuntimeBehaviorContext& Context,
    FCrowdRuntimeBehaviorOutput& OutOutput);

  static bool Commit(
    const FCrowdRuntimeBehaviorOutput& Output,
    FCrowdAgentFacts& InOutAgentFacts);

  static uint64 MakeCommitId(
    ECrowdBusinessCommitKind Kind,
    const FCrowdRuntimeBehaviorContext& Context);
};
