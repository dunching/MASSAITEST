#include "CrowdDemoBusinessAdapters.h"
#include "CrowdDemoBusinessSourceProvider.h"

namespace CrowdDemoBusinessAdaptersPrivate
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  template <typename T>
  void Fold(uint64& Hash, const T Value)
  {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    if constexpr (std::is_enum_v<T>)
    {
      Fold(Hash, static_cast<std::underlying_type_t<T>>(Value));
    }
    else
    {
      using UnsignedType = std::make_unsigned_t<T>;
      const UnsignedType Unsigned = static_cast<UnsignedType>(Value);
      for (uint32 Byte = 0; Byte < sizeof(UnsignedType); ++Byte)
      {
        Hash ^= static_cast<uint8>(Unsigned >> (Byte * 8));
        Hash *= FnvPrime;
      }
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    Fold(Hash, Ref.ProviderId);
    Fold(Hash, Ref.StableEntityId);
    Fold(Hash, Ref.LifecycleSerial);
  }

  FCrowdDemoBusinessAgentState* FindAgent(
    TArray<FCrowdDemoBusinessAgentState>& Agents,
    const FCrowdStableEntityRef& Ref)
  {
    return Agents.FindByPredicate(
      [&Ref](const FCrowdDemoBusinessAgentState& Agent)
      {
        return Agent.EntityRef == Ref;
      });
  }
}

using namespace CrowdDemoBusinessAdaptersPrivate;

bool FCrowdDemoBusinessCommitRequest::IsValid() const
{
  if (Kind == ECrowdDemoBusinessCommitKind::None)
    return CommitId == 0 && Quantity == 0;
  return Kind < ECrowdDemoBusinessCommitKind::Count
    && CommitId != 0
    && FixedStepIndex >= 0
    && TransitionRevision != 0
    && AgentRef.IsValid()
    && (TaskRef.IsUnset() || TaskRef.IsValid())
    && (TargetRef.IsUnset() || TargetRef.IsValid())
    && Quantity > 0;
}

uint64 FCrowdDemoBusinessCommitId::Make(
  const ECrowdDemoBusinessCommitKind Kind,
  const int64 FixedStepIndex,
  const uint32 TransitionRevision,
  const FCrowdStableEntityRef& AgentRef,
  const FCrowdStableEntityRef& TaskRef,
  const FCrowdStableEntityRef& TargetRef,
  const uint32 PayloadKey,
  const int32 Quantity,
  const uint64 ExternalCommitId)
{
  if (ExternalCommitId != 0) return ExternalCommitId;
  uint64 Hash = FnvOffset;
  Fold(Hash, Kind);
  Fold(Hash, static_cast<uint64>(FixedStepIndex));
  Fold(Hash, TransitionRevision);
  FoldRef(Hash, AgentRef);
  FoldRef(Hash, TaskRef);
  FoldRef(Hash, TargetRef);
  Fold(Hash, PayloadKey);
  Fold(Hash, static_cast<uint32>(Quantity));
  return Hash == 0 ? 1 : Hash;
}

ECrowdDemoBusinessCommitAcceptResult
FCrowdDemoBusinessCommitLedger::Apply(
  const FCrowdDemoBusinessCommitRequest& Request)
{
  if (!Request.IsValid()
    || Request.Kind == ECrowdDemoBusinessCommitKind::None)
    return ECrowdDemoBusinessCommitAcceptResult::Rejected;
  if (AppliedCommitIds.Contains(Request.CommitId))
    return ECrowdDemoBusinessCommitAcceptResult::Duplicate;

  switch (Request.Kind)
  {
  case ECrowdDemoBusinessCommitKind::CargoPickup:
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
  case ECrowdDemoBusinessCommitKind::CargoDeliver:
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
  case ECrowdDemoBusinessCommitKind::CombatHit:
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

bool FCrowdDemoBusinessPatchAdapter::Prepare(
  const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
  const TConstArrayView<FCrowdDemoBusinessAgentState> CurrentAgents,
  const FCrowdDemoBusinessCommitLedger& CurrentLedger,
  FCrowdDemoPreparedBusinessPatch& OutPatch)
{
  return Prepare(
    PreparedBehavior, {}, CurrentAgents,
    CurrentLedger, OutPatch);
}

bool FCrowdDemoBusinessPatchAdapter::Prepare(
  const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
  const TConstArrayView<FCrowdDemoHostIntent> HostIntents,
  const TConstArrayView<FCrowdDemoBusinessAgentState> CurrentAgents,
  const FCrowdDemoBusinessCommitLedger& CurrentLedger,
  FCrowdDemoPreparedBusinessPatch& OutPatch)
{
  OutPatch = {};
  if (!PreparedBehavior.bValid
    || PreparedBehavior.FixedStepIndex < 0)
    return false;
  OutPatch.FixedStepIndex = PreparedBehavior.FixedStepIndex;
  OutPatch.Agents = CurrentAgents;
  OutPatch.Ledger = CurrentLedger;
  OutPatch.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  for (int32 Index = 0; Index < OutPatch.Agents.Num(); ++Index)
  {
    if (!OutPatch.Agents[Index].bActive
      || !OutPatch.Agents[Index].EntityRef.IsValid()
      || OutPatch.Agents[Index].TransitionRevision == 0
      || (Index > 0
        && !(OutPatch.Agents[Index - 1].EntityRef
          < OutPatch.Agents[Index].EntityRef)))
      return false;
  }

  uint64 Hash = FnvOffset;
  Fold(Hash, static_cast<uint64>(PreparedBehavior.FixedStepIndex));
  for (const FCrowdBehaviorPreparedEntity& Entity
    : PreparedBehavior.Entities)
  {
    FCrowdDemoBusinessAgentState* Instigator =
      FindAgent(OutPatch.Agents, Entity.EntityRef);
    if (!Instigator) return false;
    for (const FCrowdBusinessContribution& Contribution
      : Entity.ResolvedChannels.Business)
    {
      FCrowdDemoBusinessCommitRequest Request;
      if (Contribution.AdapterId
        == CrowdDemoBehaviorAdapterIds::CargoPickup)
        Request.Kind = ECrowdDemoBusinessCommitKind::CargoPickup;
      else if (Contribution.AdapterId
        == CrowdDemoBehaviorAdapterIds::CargoDeliver)
        Request.Kind = ECrowdDemoBusinessCommitKind::CargoDeliver;
      else if (Contribution.AdapterId
        == CrowdDemoBehaviorAdapterIds::CombatHit)
        Request.Kind = ECrowdDemoBusinessCommitKind::CombatHit;
      else
        return false;
      Request.CommitId = Contribution.CommitId;
      Request.FixedStepIndex = PreparedBehavior.FixedStepIndex;
      Request.TransitionRevision = Instigator->TransitionRevision;
      Request.AgentRef = Contribution.InstigatorRef;
      Request.PayloadKey = Contribution.PayloadTypeId;
      Request.Quantity = Contribution.Quantity;
      if (Request.Kind == ECrowdDemoBusinessCommitKind::CombatHit)
        Request.TargetRef = Contribution.TargetRef;
      else
        Request.TaskRef = Contribution.TargetRef;

      const ECrowdDemoBusinessCommitAcceptResult First =
        OutPatch.Ledger.Apply(Request);
      const ECrowdDemoBusinessCommitAcceptResult Replay =
        OutPatch.Ledger.Apply(Request);
      if (First != ECrowdDemoBusinessCommitAcceptResult::Applied
        || Replay != ECrowdDemoBusinessCommitAcceptResult::Duplicate)
        return false;
      ++OutPatch.DuplicateCommitCount;
      if (Request.Kind == ECrowdDemoBusinessCommitKind::CombatHit)
      {
        Instigator->LastAttackFixedStep =
          PreparedBehavior.FixedStepIndex;
        FCrowdDemoBusinessAgentState* Target =
          FindAgent(OutPatch.Agents, Request.TargetRef);
        if (!Target) return false;
        Target->HitReactionVelocity = FVector::ZeroVector;
        Target->HitReactionUntilFixedStep = FMath::Max(
          Target->HitReactionUntilFixedStep,
          PreparedBehavior.FixedStepIndex + 6);
        Target->Health = FMath::Max(
          0, Target->Health - Request.Quantity);
        if (Target->Health == 0
          && !OutPatch.PendingDeathRef.IsValid())
          OutPatch.PendingDeathRef = Target->EntityRef;
      }
      else
      {
        Instigator->LastLogisticsFixedStep =
          PreparedBehavior.FixedStepIndex;
      }
      Fold(Hash, Contribution.CommitId);
      Fold(Hash, Contribution.AdapterId);
    }
  }
  TArray<FCrowdDemoHostIntent> SortedHostIntents(HostIntents);
  SortedHostIntents.Sort([](const auto& A, const auto& B)
  {
    if (A.ActionTypeId != B.ActionTypeId)
      return A.ActionTypeId < B.ActionTypeId;
    if (A.CommitId != B.CommitId)
      return A.CommitId < B.CommitId;
    return A.InstigatorRef < B.InstigatorRef;
  });
  for (int32 IntentIndex = 0;
    IntentIndex < SortedHostIntents.Num(); ++IntentIndex)
  {
    const FCrowdDemoHostIntent& Intent =
      SortedHostIntents[IntentIndex];
    if (!Intent.IsValid()
      || Intent.ActionTypeId
        != CrowdDemoBusinessActions::Attack
      || !Intent.TargetRef.IsValid()
      || (IntentIndex > 0
        && SortedHostIntents[IntentIndex - 1].ActionTypeId
          == Intent.ActionTypeId
        && SortedHostIntents[IntentIndex - 1].CommitId
          == Intent.CommitId))
      return false;
    FCrowdDemoBusinessAgentState* Instigator =
      FindAgent(OutPatch.Agents, Intent.InstigatorRef);
    FCrowdDemoBusinessAgentState* Target =
      FindAgent(OutPatch.Agents, Intent.TargetRef);
    if (!Instigator || !Target
      || Instigator->TransitionRevision
        != Intent.ExpectedRevision)
      return false;
    FCrowdDemoBusinessCommitRequest Request;
    Request.Kind = ECrowdDemoBusinessCommitKind::CombatHit;
    Request.CommitId = Intent.CommitId;
    Request.FixedStepIndex =
      PreparedBehavior.FixedStepIndex;
    Request.TransitionRevision =
      Instigator->TransitionRevision;
    Request.AgentRef = Intent.InstigatorRef;
    Request.TargetRef = Intent.TargetRef;
    Request.PayloadKey = Intent.PayloadTypeId;
    Request.Quantity = Intent.Quantity;
    const ECrowdDemoBusinessCommitAcceptResult First =
      OutPatch.Ledger.Apply(Request);
    const ECrowdDemoBusinessCommitAcceptResult Replay =
      OutPatch.Ledger.Apply(Request);
    if (First != ECrowdDemoBusinessCommitAcceptResult::Applied
      || Replay
        != ECrowdDemoBusinessCommitAcceptResult::Duplicate)
      return false;
    ++OutPatch.DuplicateCommitCount;
    Instigator->LastAttackFixedStep =
      PreparedBehavior.FixedStepIndex;
    Target->HitReactionVelocity = FVector::ZeroVector;
    Target->HitReactionUntilFixedStep = FMath::Max(
      Target->HitReactionUntilFixedStep,
      PreparedBehavior.FixedStepIndex + 6);
    Target->Health = FMath::Max(
      0, Target->Health - Intent.Quantity);
    if (Target->Health == 0
      && !OutPatch.PendingDeathRef.IsValid())
      OutPatch.PendingDeathRef = Target->EntityRef;
    Fold(Hash, Intent.CommitId);
    Fold(Hash, Intent.ActionTypeId);
  }
  for (const FCrowdDemoBusinessAgentState& Agent : OutPatch.Agents)
  {
    FoldRef(Hash, Agent.EntityRef);
    Fold(Hash, static_cast<uint32>(Agent.Health));
    Fold(Hash, static_cast<uint64>(Agent.LastAttackFixedStep));
    Fold(Hash, static_cast<uint64>(Agent.LastLogisticsFixedStep));
  }
  OutPatch.StableHash = Hash == 0 ? 1 : Hash;
  OutPatch.bValid = true;
  return true;
}

void FCrowdDemoBusinessPatchAdapter::ApplyValidated(
  const FCrowdDemoPreparedBusinessPatch& Patch,
  TArray<FCrowdDemoBusinessAgentState>& OutAgents,
  FCrowdDemoBusinessCommitLedger& OutLedger)
{
  check(Patch.bValid);
  check(Patch.FixedStepIndex >= 0);
  check(Patch.StableHash != 0);
  OutAgents = Patch.Agents;
  OutLedger = Patch.Ledger;
}
