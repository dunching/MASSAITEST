#include "MassCrowdWorkerTargetObservability.h"

namespace CrowdWorkerTargetObservabilityPrivate
{
  constexpr uint64 ObservationFnvOffset = 1469598103934665603ull;
  constexpr uint64 ObservationFnvPrime = 1099511628211ull;

  void FoldObservationValue(uint64& Hash, const uint64 Value)
  {
    for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
    {
      Hash ^= (Value >> (ByteIndex * 8)) & 0xffull;
      Hash *= ObservationFnvPrime;
    }
  }

  void FoldObservationEntity(
    uint64& Hash,
    const FCrowdStableEntityRef& EntityRef)
  {
    FoldObservationValue(Hash, EntityRef.ProviderId);
    FoldObservationValue(Hash, EntityRef.StableEntityId);
    FoldObservationValue(Hash, EntityRef.LifecycleSerial);
  }

  struct FCohortBuilder
  {
    FCrowdWorkerTargetCohortState State;
    FCrowdWorkerPayload Payload;
    uint32 GuidanceHash = 0;
    int32 TargetStateCount = 0;
    int32 UnroutedTargetStateCount = 0;
    int32 CapacityHoldTargetStateCount = 0;
    FCrowdStableEntityRef FirstUnroutedEntityRef;
    bool bHasState = false;
    bool bInvalid = false;
  };
}

bool FCrowdWorkerTargetObserver::Build(
  const FCrowdWorkerResultApplyProxy& Proxy,
  const int32 ExpectedTargetAgentCount,
  FCrowdWorkerTargetObservation& OutObservation)
{
  using namespace CrowdWorkerTargetObservabilityPrivate;

  OutObservation = {};
  OutObservation.ExpectedTargetAgentCount = ExpectedTargetAgentCount;
  const FCrowdWorkerResultApplyMetrics& ProxyMetrics = Proxy.GetMetrics();
  OutObservation.Generation = ProxyMetrics.Generation;
  OutObservation.LastAppliedInputSequence =
    ProxyMetrics.LastAppliedInputSequence;
  OutObservation.PublishSequence =
    ProxyMetrics.LastConsumedPublishSequence;

  TMap<uint32, FCohortBuilder> CohortBuilders;
  bool bInvalid = ProxyMetrics.bViolation
    || ProxyMetrics.Generation == 0
    || ProxyMetrics.LastConsumedPublishSequence == 0
    || ExpectedTargetAgentCount <= 0;
  auto MarkInvalid = [&OutObservation, &bInvalid](
    const FCrowdStableEntityRef& EntityRef)
  {
    bInvalid = true;
    if (OutObservation.FirstInvalidEntityRef.IsUnset())
      OutObservation.FirstInvalidEntityRef = EntityRef;
  };

  for (const FCrowdStableEntityRef& EntityRef :
    Proxy.GetStableEntityView())
  {
    const FCrowdWorkerDomainProxyState* TargetProxy =
      Proxy.FindDomain(EntityRef, ECrowdWorkerField::Target);
    if (!TargetProxy) continue;
    ++OutObservation.TargetAgentCount;
    OutObservation.WorkerEpoch = FMath::Max(
      OutObservation.WorkerEpoch, TargetProxy->WorkerEpoch);

    FCrowdWorkerTargetState TargetState;
    if (!FCrowdWorkerTargetStateCodec::Decode(
        TargetProxy->State.Payload, TargetState)
      || !TargetState.IsValid())
    {
      MarkInvalid(EntityRef);
      continue;
    }
    ++OutObservation.ValidTargetStateCount;
    if (OutObservation.TargetRevision == INDEX_NONE)
      OutObservation.TargetRevision = TargetState.TargetRevision;
    else if (OutObservation.TargetRevision != TargetState.TargetRevision)
      MarkInvalid(EntityRef);

    const FCrowdWorkerDomainProxyState* CohortProxy =
      Proxy.FindDomain(EntityRef, ECrowdWorkerField::TargetCohort);
    FCrowdWorkerTargetCohortState CohortState;
    if (!CohortProxy
      || !FCrowdWorkerTargetCohortStateCodec::Decode(
        CohortProxy->State.Payload, CohortState)
      || !CohortState.IsValid()
      || CohortState.CohortKey != TargetState.CohortKey
      || CohortState.TargetRevision != TargetState.TargetRevision
      || CohortState.Execution.ExecutionHash
        != TargetState.ExecutionHash)
    {
      MarkInvalid(EntityRef);
      continue;
    }

    FCohortBuilder& Builder =
      CohortBuilders.FindOrAdd(TargetState.CohortKey);
    if (!Builder.bHasState)
    {
      Builder.State = CohortState;
      Builder.Payload = CohortProxy->State.Payload;
      Builder.GuidanceHash = TargetState.GuidanceHash;
      Builder.bHasState = true;
    }
    else if (!(Builder.Payload == CohortProxy->State.Payload)
      || Builder.GuidanceHash != TargetState.GuidanceHash)
    {
      Builder.bInvalid = true;
      MarkInvalid(EntityRef);
    }
    ++Builder.TargetStateCount;
    if (TargetState.Mode ==
      ECrowdTargetRegionGuidanceMode::Unrouted)
    {
      ++Builder.UnroutedTargetStateCount;
      ++OutObservation.UnroutedTargetStateCount;
      if (Builder.FirstUnroutedEntityRef.IsUnset())
        Builder.FirstUnroutedEntityRef = EntityRef;
      if (OutObservation.FirstUnroutedEntityRef.IsUnset())
        OutObservation.FirstUnroutedEntityRef = EntityRef;
    }
    else if (TargetState.Mode ==
      ECrowdTargetRegionGuidanceMode::CapacityHold)
    {
      ++Builder.CapacityHoldTargetStateCount;
      ++OutObservation.CapacityHoldTargetStateCount;
    }
  }

  TArray<uint32> CohortKeys;
  CohortBuilders.GetKeys(CohortKeys);
  CohortKeys.Sort();
  OutObservation.Cohorts.Reserve(CohortKeys.Num());
  for (const uint32 CohortKey : CohortKeys)
  {
    const FCohortBuilder& Builder =
      CohortBuilders.FindChecked(CohortKey);
    FCrowdWorkerTargetCohortObservation& Cohort =
      OutObservation.Cohorts.AddDefaulted_GetRef();
    Cohort.CohortKey = CohortKey;
    Cohort.TopologyRevision = Builder.State.TopologyRevision;
    Cohort.TargetRevision = Builder.State.TargetRevision;
    Cohort.PlanEpoch = Builder.State.Plan.PlanEpoch;
    Cohort.PlanBuildFixedStep =
      Builder.State.Plan.BuildFixedStepIndex;
    Cohort.FeasibleGraphHash =
      Builder.State.Plan.FeasibleGraphHash;
    Cohort.MembershipHash = Builder.State.Plan.MembershipHash;
    Cohort.ExternalPopulationHash =
      Builder.State.Plan.ExternalPopulationHash;
    Cohort.TransportHash = Builder.State.Plan.TransportHash;
    Cohort.RoutedAgentCount = Builder.State.Plan.RoutedAgentCount;
    Cohort.PlanUnroutedAgentCount =
      Builder.State.Plan.UnroutedAgentCount;
    Cohort.TotalFeasibleCapacity =
      Builder.State.Plan.TotalFeasibleCapacity;
    Cohort.AssignablePopulation =
      Builder.State.Plan.AssignablePopulation;
    Cohort.OverflowPopulation =
      Builder.State.Plan.OverflowPopulation;
    OutObservation.TotalFeasibleCapacity +=
      Cohort.TotalFeasibleCapacity;
    OutObservation.AssignablePopulation +=
      Cohort.AssignablePopulation;
    OutObservation.OverflowPopulation += Cohort.OverflowPopulation;
    Cohort.ExecutionHash = Builder.State.Execution.ExecutionHash;
    Cohort.GuidanceHash = Builder.GuidanceHash;
    Cohort.TargetStateCount = Builder.TargetStateCount;
    Cohort.UnroutedTargetStateCount =
      Builder.UnroutedTargetStateCount;
    Cohort.CapacityHoldTargetStateCount =
      Builder.CapacityHoldTargetStateCount;
    Cohort.FirstUnroutedEntityRef =
      Builder.FirstUnroutedEntityRef;
    Cohort.bValid = Builder.bHasState
      && !Builder.bInvalid
      && Builder.State.IsValid()
      && Builder.TargetStateCount > 0;
    bInvalid |= !Cohort.bValid;
  }

  OutObservation.bValid = !bInvalid
    && OutObservation.TargetAgentCount == ExpectedTargetAgentCount
    && OutObservation.ValidTargetStateCount
      == ExpectedTargetAgentCount
    && !OutObservation.Cohorts.IsEmpty()
    && OutObservation.TargetRevision >= 0;

  uint64 StableHash = ObservationFnvOffset;
  FoldObservationValue(StableHash, OutObservation.Generation);
  FoldObservationValue(StableHash, OutObservation.WorkerEpoch);
  FoldObservationValue(
    StableHash, OutObservation.LastAppliedInputSequence);
  FoldObservationValue(StableHash, OutObservation.PublishSequence);
  FoldObservationValue(
    StableHash, static_cast<uint64>(OutObservation.TargetRevision));
  FoldObservationValue(
    StableHash, OutObservation.ExpectedTargetAgentCount);
  FoldObservationValue(StableHash, OutObservation.TargetAgentCount);
  FoldObservationValue(
    StableHash, OutObservation.UnroutedTargetStateCount);
  FoldObservationValue(StableHash, OutObservation.TotalFeasibleCapacity);
  FoldObservationValue(StableHash, OutObservation.AssignablePopulation);
  FoldObservationValue(StableHash, OutObservation.OverflowPopulation);
  FoldObservationValue(
    StableHash, OutObservation.CapacityHoldTargetStateCount);
  FoldObservationEntity(StableHash, OutObservation.FirstInvalidEntityRef);
  FoldObservationEntity(StableHash, OutObservation.FirstUnroutedEntityRef);
  for (const FCrowdWorkerTargetCohortObservation& Cohort :
    OutObservation.Cohorts)
  {
    FoldObservationValue(StableHash, Cohort.CohortKey);
    FoldObservationValue(StableHash, Cohort.TopologyRevision);
    FoldObservationValue(
      StableHash, static_cast<uint64>(Cohort.TargetRevision));
    FoldObservationValue(
      StableHash, static_cast<uint64>(Cohort.PlanEpoch));
    FoldObservationValue(
      StableHash, static_cast<uint64>(Cohort.PlanBuildFixedStep));
    FoldObservationValue(StableHash, Cohort.FeasibleGraphHash);
    FoldObservationValue(StableHash, Cohort.MembershipHash);
    FoldObservationValue(StableHash, Cohort.ExternalPopulationHash);
    FoldObservationValue(StableHash, Cohort.TransportHash);
    FoldObservationValue(StableHash, Cohort.ExecutionHash);
    FoldObservationValue(StableHash, Cohort.GuidanceHash);
    FoldObservationValue(StableHash, Cohort.TargetStateCount);
    FoldObservationValue(StableHash, Cohort.UnroutedTargetStateCount);
    FoldObservationValue(StableHash, Cohort.TotalFeasibleCapacity);
    FoldObservationValue(StableHash, Cohort.AssignablePopulation);
    FoldObservationValue(StableHash, Cohort.OverflowPopulation);
    FoldObservationValue(
      StableHash, Cohort.CapacityHoldTargetStateCount);
  }
  FoldObservationValue(StableHash, OutObservation.bValid ? 1 : 0);
  OutObservation.StableHash = StableHash;
  return OutObservation.bValid;
}
