#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoBusinessPlanner.h"
#include "MassCrowdBehaviorSourceRuntime.h"

enum class ECrowdDemoBusinessCommitKind : uint8
{
  None = 0,
  CargoPickup,
  CargoDeliver,
  CombatHit,
  Count
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoBusinessCommitRequest
{
  ECrowdDemoBusinessCommitKind Kind =
    ECrowdDemoBusinessCommitKind::None;
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

class MASSCROWDDEMOBUSINESS_API FCrowdDemoBusinessCommitId
{
public:
  static uint64 Make(
    ECrowdDemoBusinessCommitKind Kind,
    int64 FixedStepIndex,
    uint32 TransitionRevision,
    const FCrowdStableEntityRef& AgentRef,
    const FCrowdStableEntityRef& TaskRef,
    const FCrowdStableEntityRef& TargetRef,
    uint32 PayloadKey,
    int32 Quantity,
    uint64 ExternalCommitId = 0);
};

enum class ECrowdDemoBusinessCommitAcceptResult : uint8
{
  Applied = 0,
  Duplicate,
  Rejected
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoBusinessCommitLedger
{
public:
  ECrowdDemoBusinessCommitAcceptResult Apply(
    const FCrowdDemoBusinessCommitRequest& Request);

  uint64 GetCargoCarrier(uint64 CargoStableEntityId) const;
  int32 GetPickupCount() const { return PickupCount; }
  int32 GetDeliveryCount() const { return DeliveryCount; }
  int32 GetCombatHitQuantity(uint64 TargetStableEntityId) const;
  int32 GetAppliedCommitCount() const { return AppliedCommitIds.Num(); }

private:
  TSet<uint64> AppliedCommitIds;
  TMap<uint64, uint64> CargoCarrierByTask;
  TMap<uint64, int32> CombatHitQuantityByTarget;
  int32 PickupCount = 0;
  int32 DeliveryCount = 0;
};

struct FCrowdDemoBusinessAgentState
{
  FCrowdStableEntityRef EntityRef;
  uint32 TransitionRevision = 0;
  int32 Health = 0;
  int64 LastAttackFixedStep = INDEX_NONE;
  int64 LastLogisticsFixedStep = INDEX_NONE;
  int64 HitReactionUntilFixedStep = INDEX_NONE;
  FVector HitReactionVelocity = FVector::ZeroVector;
  bool bActive = false;
};

struct FCrowdDemoPreparedBusinessPatch
{
  int64 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoBusinessAgentState> Agents;
  FCrowdDemoBusinessCommitLedger Ledger;
  FCrowdStableEntityRef PendingDeathRef;
  int32 DuplicateCommitCount = 0;
  uint64 StableHash = 0;
  bool bValid = false;
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoBusinessPatchAdapter
{
public:
  static bool Prepare(
    const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
    TConstArrayView<FCrowdDemoBusinessAgentState> CurrentAgents,
    const FCrowdDemoBusinessCommitLedger& CurrentLedger,
    FCrowdDemoPreparedBusinessPatch& OutPatch);

  static bool Prepare(
    const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
    TConstArrayView<FCrowdDemoHostIntent> HostIntents,
    TConstArrayView<FCrowdDemoBusinessAgentState> CurrentAgents,
    const FCrowdDemoBusinessCommitLedger& CurrentLedger,
    FCrowdDemoPreparedBusinessPatch& OutPatch);

  static void ApplyValidated(
    const FCrowdDemoPreparedBusinessPatch& Patch,
    TArray<FCrowdDemoBusinessAgentState>& OutAgents,
    FCrowdDemoBusinessCommitLedger& OutLedger);
};
