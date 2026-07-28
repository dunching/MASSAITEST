#include "Mass/CrowdDemoBehaviorAdapters.h"

ECrowdDemoBusinessCommitAcceptResult FCrowdDemoBusinessCommitLedger::Apply(
  const FCrowdBusinessCommitRequest& Request)
{
  if (!Request.IsValid() || Request.Kind == ECrowdBusinessCommitKind::None)
    return ECrowdDemoBusinessCommitAcceptResult::Rejected;
  if (AppliedCommitIds.Contains(Request.CommitId))
    return ECrowdDemoBusinessCommitAcceptResult::Duplicate;

  switch (Request.Kind)
  {
  case ECrowdBusinessCommitKind::CargoPickup:
    {
      if (!Request.TaskRef.IsValid())
        return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      const uint64* ExistingCarrier =
        CargoCarrierByTask.Find(Request.TaskRef.StableEntityId);
      if (ExistingCarrier && *ExistingCarrier != 0)
        return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      CargoCarrierByTask.Add(
        Request.TaskRef.StableEntityId,
        Request.AgentRef.StableEntityId);
      ++PickupCount;
      break;
    }
  case ECrowdBusinessCommitKind::CargoDeliver:
    {
      if (!Request.TaskRef.IsValid())
        return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      const uint64* ExistingCarrier =
        CargoCarrierByTask.Find(Request.TaskRef.StableEntityId);
      if (!ExistingCarrier
        || *ExistingCarrier != Request.AgentRef.StableEntityId)
        return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      CargoCarrierByTask.Add(Request.TaskRef.StableEntityId, 0);
      ++DeliveryCount;
      break;
    }
  case ECrowdBusinessCommitKind::CombatHit:
    if (!Request.TargetRef.IsValid())
      return ECrowdDemoBusinessCommitAcceptResult::Rejected;
    CombatHitQuantityByTarget.FindOrAdd(
      Request.TargetRef.StableEntityId) += Request.Quantity;
    break;
  default:
    return ECrowdDemoBusinessCommitAcceptResult::Rejected;
  }
  AppliedCommitIds.Add(Request.CommitId);
  return ECrowdDemoBusinessCommitAcceptResult::Applied;
}

uint64 FCrowdDemoBusinessCommitLedger::GetCargoCarrier(
  const uint64 CargoStableEntityId) const
{
  const uint64* Carrier =
    CargoCarrierByTask.Find(CargoStableEntityId);
  return Carrier ? *Carrier : 0;
}

int32 FCrowdDemoBusinessCommitLedger::GetCombatHitQuantity(
  const uint64 TargetStableEntityId) const
{
  const int32* Quantity =
    CombatHitQuantityByTarget.Find(TargetStableEntityId);
  return Quantity ? *Quantity : 0;
}
