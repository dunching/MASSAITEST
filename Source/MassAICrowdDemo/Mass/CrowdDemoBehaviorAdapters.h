#pragma once

#include "Mass/CrowdDemoCombatStateKernel.h"
#include "MassCrowdRuntimeBehavior.h"

class FCrowdDemoLogisticsBehaviorAdapter final
  : public ICrowdRuntimeBehaviorProvider
{
public:
  virtual bool Supports(ECrowdActiveBehavior Behavior) const override;
  virtual bool Evaluate(
    const FCrowdRuntimeBehaviorContext& Context,
    FCrowdRuntimeBehaviorOutput& OutOutput) const override;

};

class FCrowdDemoCombatBehaviorAdapter final
  : public ICrowdRuntimeBehaviorProvider
{
public:
  virtual bool Supports(ECrowdActiveBehavior Behavior) const override;
  virtual bool Evaluate(
    const FCrowdRuntimeBehaviorContext& Context,
    FCrowdRuntimeBehaviorOutput& OutOutput) const override;

  bool BuildContextFromHitFact(
    const FCrowdDemoHitFact& HitFact,
    const FCrowdAgentFacts& SourceFacts,
    uint32 TransitionRevision,
    FCrowdRuntimeBehaviorContext& OutContext) const;
};

class FCrowdDemoBehaviorProviderSet final : public ICrowdRuntimeBehaviorProvider
{
public:
  virtual bool Supports(ECrowdActiveBehavior Behavior) const override;
  virtual bool Evaluate(
    const FCrowdRuntimeBehaviorContext& Context,
    FCrowdRuntimeBehaviorOutput& OutOutput) const override;

private:
  FCrowdRuntimeBasicBehaviorProvider Basic;
  FCrowdDemoLogisticsBehaviorAdapter Logistics;
  FCrowdDemoCombatBehaviorAdapter Combat;
};

enum class ECrowdDemoBusinessCommitAcceptResult : uint8
{
  Applied = 0,
  Duplicate,
  Rejected
};

class FCrowdDemoBusinessCommitLedger
{
public:
  ECrowdDemoBusinessCommitAcceptResult Apply(
    const FCrowdBusinessCommitRequest& Request);

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
