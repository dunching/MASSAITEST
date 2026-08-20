#pragma once

#include "CoreMinimal.h"
#include "MassCrowdTargetRegionWork.h"
#include "MassCrowdWorkerRuntimeV2.h"

namespace CrowdWorkerTargetWorkScopes
{
  constexpr uint64 EncodeCohortKey(const uint32 CohortKey)
  {
    return static_cast<uint64>(CohortKey) + 1;
  }

  constexpr bool DecodeCohortKey(
    const uint64 ScopeKey,
    uint32& OutCohortKey)
  {
    if (ScopeKey == 0
      || ScopeKey > static_cast<uint64>(MAX_uint32) + 1)
      return false;
    OutCohortKey = static_cast<uint32>(ScopeKey - 1);
    return true;
  }
}

namespace CrowdWorkerResourceIds
{
  constexpr uint64 TargetControl = 0x4357544152474554ull;
}

namespace CrowdWorkerTargetObjectiveIds
{
  // Objective ids are wrapped by CrowdWorkerResourceIds::ObjectiveRevision
  // before entering the versioned resource store.
  constexpr uint64 PrimaryTarget = 1;
}

namespace CrowdWorkerTargetConstants
{
  // Stable external Particle identity shared by the Target objective and the
  // Particle domain. Its kinematics come from the per-tick objective revision,
  // never from the frozen MovementControl template.
  constexpr int32 PrimaryTargetParticleAgentId = -1000000001;
}

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetObjectiveClock
{
  // Converts the absolute simulation time carried by a Worker input batch to
  // the same persistent fixed-tick domain used by
  // FCrowdWorkerDomainContext::AbsoluteSimulationTick.
  static bool ResolveEffectiveFixedStepIndex(
    double EffectiveSimulationTimeSeconds,
    double FixedSimulationQuantumSeconds,
    int32& OutEffectiveFixedStepIndex);
};

struct MASSCROWDRUNTIME_API FCrowdWorkerTargetObjectiveRevision
{
  int32 TargetRevision = INDEX_NONE;
  int32 EffectiveFixedStepIndex = INDEX_NONE;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  FVector2f TargetVelocity = FVector2f::ZeroVector;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTargetObjectiveRevisionCodec
{
public:
  static constexpr uint32 SchemaId = 0x4357544Fu;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerTargetObjectiveRevision& Revision,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerTargetObjectiveRevision& OutRevision);
};

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
  static constexpr uint16 SchemaVersion = 2;

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
  static constexpr uint16 SchemaVersion = 2;

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
  // Read-only acceptance summaries from the same authoritative Demand build
  // that produced Plan/Execution. They let ResultApply report heterogeneous
  // Target acceptance without reviving a Host Target simulation mirror.
  int32 FeasibleCellCount = 0;
  int32 EdgeCount = 0;
  int32 FeasibleRegionCount = 0;
  int32 FeasibleRegionCoverageCount = 0;
  int32 CurrentTerminalPopulation = 0;
  int32 MaximumRegionPopulation = 0;
  int32 DesiredPopulationTotal = 0;
  // Cumulative read-only acceptance counters. They do not participate in
  // admission or claim ownership.
  int32 ReleasedClaimCount = 0;
  int32 OverbookedCellCount = 0;
  FCrowdTargetRegionFlowPlan Plan;
  FCrowdTargetRegionQuotaExecutionState Execution;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTargetCohortStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575448u;
  static constexpr uint16 SchemaVersion = 4;

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
    uint64 ObjectiveResourceRevision = 0;
    FCrowdMassTargetRegionTopologyOutput Topology;
  };

  mutable FCriticalSection StateMutex;
  uint64 StateGeneration = 0;
  TMap<uint32, FCohortRuntime> Cohorts;
  FCrowdWorkerTargetDomainMetrics Metrics;
};
