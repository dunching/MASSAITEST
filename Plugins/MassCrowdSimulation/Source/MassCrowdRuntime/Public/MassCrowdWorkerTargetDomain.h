#pragma once

#include "CoreMinimal.h"
#include "MassCrowdTargetRegionWork.h"
#include "MassCrowdWorkerRuntimeV2.h"

namespace CrowdWorkerResourceIds
{
  constexpr uint64 TargetControl = 0x4357544152474554ull;
}

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetAgentInput
{
  FCrowdStableEntityRef EntityRef;
  FCrowdTargetRegionTransportAgent Agent;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetCohortInput
{
  uint32 CohortKey = 0;
  uint32 TopologyRevision = 0;
  int32 TargetRevision = INDEX_NONE;
  int32 FixedStepIndex = INDEX_NONE;
  FCrowdTargetRegionTransportSettings Settings;
  FCrowdSharedFlowFieldConfig FlowConfig;
  TArray<FCrowdWorkerTargetAgentInput> Agents;
  TArray<FCrowdTargetRegionTransportAgent> ExternalAgents;
  FCrowdTargetRegionFlowPlan BootstrapPlan;
  FCrowdTargetRegionQuotaExecutionState BootstrapExecution;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetControlResource
{
  uint64 Revision = 0;
  TArray<FCrowdWorkerTargetCohortInput> Cohorts;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTargetControlResourceCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575443u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerTargetControlResource& Resource,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerTargetControlResource& OutResource);
  static uint32 CalculateTopologyRevision(
    const FCrowdTargetPolarTopology& Topology);
};

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetState
{
  uint32 CohortKey = 0;
  int32 TargetRevision = INDEX_NONE;
  int32 CurrentCellKey = INDEX_NONE;
  int32 NextCellKey = INDEX_NONE;
  int32 DemandRegionKey = INDEX_NONE;
  ECrowdTargetRegionGuidanceMode Mode =
    ECrowdTargetRegionGuidanceMode::Unrouted;
  FVector DesiredVelocity = FVector::ZeroVector;
  uint32 ExecutionHash = 0;
  uint32 GuidanceHash = 0;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTargetStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575453u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerTargetState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerTargetState& OutState);
};

// Checkpointed cohort authority. Topology is a deterministic cache rebuilt
// from versioned resources; plan/execution are simulation state and therefore
// live in the entity state store instead of an Executor member.
struct MASSCROWDRUNTIME_API FCrowdWorkerTargetCohortState
{
  uint32 CohortKey = 0;
  uint32 TopologyRevision = 0;
  int32 TargetRevision = INDEX_NONE;
  FCrowdTargetRegionFlowPlan Plan;
  FCrowdTargetRegionQuotaExecutionState Execution;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTargetCohortStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575448u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerTargetCohortState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerTargetCohortState& OutState);
};

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetDomainMetrics
{
  uint64 TopologyBuildCount = 0;
  uint64 PlanBuildCount = 0;
  uint64 PlanCacheHitCount = 0;
  uint64 MembershipChangeCount = 0;
  uint64 PublishedPatchCount = 0;
  uint64 GuidanceShardCount = 0;
  int32 GuidanceMaxShardSize = 0;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTargetDomainExecutor final
  : public ICrowdWorkerDomainExecutor
{
public:
  ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::Target;
  }

  void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const override;
  bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;
  FCrowdWorkerTargetDomainMetrics GetMetrics() const;

private:
  struct FCohortRuntime
  {
    uint32 TopologyRevision = 0;
    FCrowdMassTargetRegionTopologyOutput Topology;
  };

  mutable FCriticalSection StateMutex;
  uint64 StateGeneration = 0;
  TMap<uint32, FCohortRuntime> Cohorts;
  FCrowdWorkerTargetDomainMetrics Metrics;
};
