#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

enum class ECrowdBusinessCommitKind : uint8
{
  None = 0,
  CargoPickup,
  CargoDeliver,
  CombatHit,
  Count
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

// Migration-only recipe input. It is not stored on an entity, replicated, or
// committed as authoritative state.
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

class MASSCROWDRUNTIME_API FCrowdBehaviorCommitId
{
public:
  static uint64 Make(
    ECrowdBusinessCommitKind Kind,
    const FCrowdRuntimeBehaviorContext& Context);
};
