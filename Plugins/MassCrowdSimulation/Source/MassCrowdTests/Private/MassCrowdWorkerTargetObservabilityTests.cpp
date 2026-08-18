#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "MassCrowdWorkerTargetObservability.h"
#include "Misc/FileHelper.h"

namespace CrowdWorkerTargetObservabilityTests
{
  FCrowdWorkerStatePatch MakeTargetObservationPatch(
    const FCrowdStableEntityRef& EntityRef,
    const ECrowdWorkerField Field,
    const FCrowdWorkerPayload& Payload)
  {
    FCrowdWorkerStatePatch Patch;
    Patch.EntityRef = EntityRef;
    Patch.StateFieldId =
      1 + static_cast<uint16>(Field);
    Patch.Generation = 7;
    Patch.WorkerEpoch = 12;
    Patch.SourceInputSequence = 44;
    Patch.DirtyMask = CrowdWorkerRuntimeV2FieldMask(Field);
    Patch.State.StateRevision = 12;
    Patch.State.Payload = Payload;
    Patch.RecalculateStableHash();
    return Patch;
  }

  bool BuildTargetPayloads(
    const ECrowdTargetRegionGuidanceMode Mode,
    FCrowdWorkerPayload& OutTarget,
    FCrowdWorkerPayload& OutCohort)
  {
    FCrowdWorkerTargetCohortState Cohort;
    Cohort.CohortKey = 3;
    Cohort.TopologyRevision = 77;
    Cohort.TargetRevision = 9;
    Cohort.Plan.PlanEpoch = 4;
    Cohort.Plan.BuildFixedStepIndex = 11;
    Cohort.Plan.TargetRevision = 9;
    Cohort.Plan.FeasibleGraphHash = 101;
    Cohort.Plan.MembershipHash = 102;
    Cohort.Plan.ExternalPopulationHash = 103;
    Cohort.Plan.RoutedAgentCount = 2;
    Cohort.Plan.UnroutedAgentCount = 0;
    Cohort.Plan.TotalFeasibleCapacity = 8;
    Cohort.Plan.AssignablePopulation = 2;
    Cohort.Plan.OverflowPopulation = 0;
    Cohort.Plan.TransportHash = 104;
    Cohort.Plan.bValid = true;
    Cohort.Execution.PlanEpoch = 4;
    Cohort.Execution.PlanTransportHash = 104;
    Cohort.Execution.ExecutionHash = 105;
    Cohort.Execution.bValid = true;
    FCrowdWorkerTargetState Target;
    Target.CohortKey = 3;
    Target.TargetRevision = 9;
    Target.CurrentCellKey = 1;
    Target.NextCellKey = 2;
    Target.DemandRegionKey = 3;
    Target.Mode = Mode;
    Target.DesiredVelocity = FVector(100.0, 0.0, 0.0);
    Target.ExecutionHash = 105;
    Target.GuidanceHash = 106;
    return FCrowdWorkerTargetStateCodec::Encode(Target, OutTarget)
      && FCrowdWorkerTargetCohortStateCodec::Encode(
        Cohort, OutCohort);
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerTargetObservationReadOnlyTest,
  "MassCrowd.RuntimeV2.TargetObservability.ReadOnlyResultApplyProjection",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerTargetObservationReadOnlyTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerTargetObservabilityTests;
  (void)Parameters;
  FCrowdWorkerPayload TargetPayload;
  FCrowdWorkerPayload CohortPayload;
  TestTrue(TEXT("Target observation payloads encode"),
    BuildTargetPayloads(
      ECrowdTargetRegionGuidanceMode::Transport,
      TargetPayload, CohortPayload));

  FCrowdWorkerContractLimits Limits;
  Limits.MaxPayloadBytes = 64 * 1024;
  Limits.MaxInputRecordsPerBatch = 16;
  Limits.MaxStatePatchesPerSlot = 16;
  Limits.MaxPendingOrderedEvents = 16;
  FCrowdWorkerResultApplyProxy Proxy;
  TestTrue(TEXT("Target observation proxy initializes"),
    Proxy.ResetQuiescent(7, Limits));
  const FCrowdStableEntityRef EntityRefs[] = {
    {1, 10, 1}, {1, 20, 1}};
  TestTrue(TEXT("Target observation stable view initializes"),
    Proxy.UpdateCurrentEntities(7, EntityRefs));

  FCrowdWorkerPublishedBatch Batch;
  Batch.Generation = 7;
  Batch.PublishSequence = 5;
  Batch.MinWorkerEpoch = 12;
  Batch.MaxWorkerEpoch = 12;
  Batch.LastAppliedInputSequence = 44;
  Batch.PublishedSimulationTimeSeconds = 0.4;
  for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
  {
    Batch.StatePatches.Add(MakeTargetObservationPatch(
      EntityRef, ECrowdWorkerField::Target, TargetPayload));
    Batch.StatePatches.Add(MakeTargetObservationPatch(
      EntityRef, ECrowdWorkerField::TargetCohort, CohortPayload));
  }
  Batch.RecalculateStableHash();
  TestEqual(TEXT("Target observation batch applies"),
    Proxy.Apply(Batch), ECrowdWorkerResultApplyResult::Applied);

  const FCrowdWorkerDomainProxyState* BeforeTarget =
    Proxy.FindDomain(EntityRefs[0], ECrowdWorkerField::Target);
  const FCrowdWorkerDomainProxyState* BeforeCohort =
    Proxy.FindDomain(EntityRefs[0], ECrowdWorkerField::TargetCohort);
  TestNotNull(TEXT("Target state retained before observation"),
    BeforeTarget);
  TestNotNull(TEXT("Target cohort retained before observation"),
    BeforeCohort);
  if (!BeforeTarget || !BeforeCohort) return false;
  const uint64 TargetPayloadHash = BeforeTarget->State.Payload.StableHash;
  const uint64 CohortPayloadHash = BeforeCohort->State.Payload.StableHash;
  const FCrowdWorkerResultApplyMetrics MetricsBefore =
    Proxy.GetMetrics();

  FCrowdWorkerTargetObservation First;
  TestTrue(TEXT("Worker Target observation is valid"),
    FCrowdWorkerTargetObserver::Build(Proxy, 2, First));
  TestEqual(TEXT("Worker Target observation sees generation"),
    First.Generation, uint64{7});
  TestEqual(TEXT("Worker Target observation sees input"),
    First.LastAppliedInputSequence, uint64{44});
  TestEqual(TEXT("Worker Target observation sees publish"),
    First.PublishSequence, uint64{5});
  TestEqual(TEXT("Worker Target observation sees agents"),
    First.TargetAgentCount, 2);
  TestEqual(TEXT("Worker Target observation deduplicates cohort"),
    First.Cohorts.Num(), 1);
  if (First.Cohorts.Num() != 1) return false;
  TestEqual(TEXT("Worker Target observation sees feasible graph"),
    First.Cohorts[0].FeasibleGraphHash, uint32{101});
  TestEqual(TEXT("Worker Target observation sees transport hash"),
    First.Cohorts[0].TransportHash, uint32{104});
  TestEqual(TEXT("Worker Target observation sees execution hash"),
    First.Cohorts[0].ExecutionHash, uint32{105});
  TestEqual(TEXT("Worker Target observation sees guidance hash"),
    First.Cohorts[0].GuidanceHash, uint32{106});
  TestEqual(TEXT("Worker Target observation has no unrouted state"),
    First.UnroutedTargetStateCount, 0);
  TestEqual(TEXT("Worker Target observation exposes capacity"),
    First.TotalFeasibleCapacity, 8);
  TestEqual(TEXT("Worker Target observation exposes assigned population"),
    First.AssignablePopulation, 2);
  TestEqual(TEXT("Worker Target observation exposes overflow"),
    First.OverflowPopulation, 0);
  TestEqual(TEXT("Worker Target observation has no capacity hold"),
    First.CapacityHoldTargetStateCount, 0);

  FCrowdWorkerTargetObservation Repeat;
  TestTrue(TEXT("Repeated Worker Target observation is valid"),
    FCrowdWorkerTargetObserver::Build(Proxy, 2, Repeat));
  TestEqual(TEXT("Repeated observation hash is deterministic"),
    Repeat.StableHash, First.StableHash);
  TestEqual(TEXT("Observation does not consume a batch"),
    Proxy.GetMetrics().AppliedBatchCount,
    MetricsBefore.AppliedBatchCount);
  TestEqual(TEXT("Observation does not change publish sequence"),
    Proxy.GetMetrics().LastConsumedPublishSequence,
    MetricsBefore.LastConsumedPublishSequence);
  TestEqual(TEXT("Observation does not write Target payload"),
    Proxy.FindDomain(EntityRefs[0], ECrowdWorkerField::Target)
      ->State.Payload.StableHash,
    TargetPayloadHash);
  TestEqual(TEXT("Observation does not write TargetCohort payload"),
    Proxy.FindDomain(EntityRefs[0], ECrowdWorkerField::TargetCohort)
      ->State.Payload.StableHash,
    CohortPayloadHash);

  FCrowdWorkerTargetObservation WrongExpectedCount;
  TestFalse(TEXT("Expected Target membership mismatch fails closed"),
    FCrowdWorkerTargetObserver::Build(
      Proxy, 3, WrongExpectedCount));
  TestFalse(TEXT("Mismatched observation is invalid"),
    WrongExpectedCount.bValid);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerTargetRejectionDiagnosticContractTest,
  "MassCrowd.RuntimeV2.TargetObservability.RejectionDiagnosticContract",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerTargetRejectionDiagnosticContractTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const TSharedPtr<IPlugin> Plugin =
    IPluginManager::Get().FindPlugin(TEXT("MassCrowdSimulation"));
  if (!TestTrue(TEXT("MassCrowdSimulation plugin is found"),
      Plugin.IsValid()))
    return false;
  FString Source;
  TestTrue(TEXT("Worker Target source is readable"),
    FFileHelper::LoadFileToString(Source, *(
      Plugin->GetBaseDir()
      / TEXT("Source/MassCrowdRuntime/Private/MassCrowdWorkerTargetDomain.cpp"))));
  TestTrue(TEXT("Demand rejection carries replay coordinates"),
    Source.Contains(TEXT(
      "CrowdWorkerTargetDemandRejected fixed_step=%llu generation=%llu epoch=%llu input=%llu cohort=%u target_revision=%d"))
    && Source.Contains(TEXT(
      "target_velocity_x=%.3f target_velocity_y=%.3f flow_revision=%llu flow_build_hash=%u")));
  TestTrue(TEXT("All Target stage rejections carry replay coordinates"),
    Source.Contains(TEXT(
      "CrowdWorkerTargetDomainRejected stage=%s fixed_step=%llu generation=%llu epoch=%llu input=%llu"))
    && Source.Contains(TEXT(
      "target_context_valid=%d target_revision=%d target_x=%.3f target_y=%.3f target_velocity_x=%.3f target_velocity_y=%.3f")));
  return true;
}

#endif
