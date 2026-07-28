#pragma once

#include "CoreMinimal.h"
#include "CrowdGuidanceComposeKernel.h"
#include "MassCrowdRuntimeFragments.h"

struct FCrowdMassGuidanceCandidates
{
  FCrowdGuidanceCandidate SharedFlow;
  FCrowdGuidanceCandidate TargetRegion;
  FCrowdGuidanceCandidate BusinessOverride;
};

struct FCrowdMassGatherRecord
{
  FCrowdMassAgentFragment Identity;
  FCrowdAgentFacts AgentFacts;
  FCrowdMassSimulationStateFragment State;
  FCrowdMassPropertiesFragment Properties;
  FCrowdMassGuidanceCandidates Guidance;
};

// Immutable base motion facts gathered once at the beginning of a fixed-step
// boundary. Guidance and other stage-owned derived facts deliberately do not
// live here: they are appended by later WORK stages.
struct FCrowdMassBoundaryAgentRecord
{
  FCrowdMassAgentFragment Identity;
  FCrowdAgentFacts AgentFacts;
  FCrowdMassSimulationStateFragment State;
  FCrowdMassPropertiesFragment Properties;
};

struct FCrowdMassBoundarySnapshot
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMassBoundaryAgentRecord> Agents;
  uint64 StableHash = 14695981039346656037ull;
  bool bValid = false;
};

struct FCrowdMassWorkBatch
{
  uint32 CapabilityProfileKey = 0;
  FCrowdRoundWorkInput WorkInput;
  uint32 GatherHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdMassWorkBatchOutput
{
  uint32 CapabilityProfileKey = 0;
  FCrowdRoundWorkOutput WorkOutput;
  TArray<FCrowdStableEntityRef> EntityRefs;
};

struct FCrowdMassCommitRecord
{
  FCrowdStableEntityRef EntityRef;
  uint32 CapabilityProfileKey = 0;
  int32 PlanRevision = INDEX_NONE;
  FCrowdMovementOutput Movement;
};

struct FCrowdMassCommitTarget
{
  FCrowdStableEntityRef EntityRef;
  int32 AgentId = INDEX_NONE;
  uint32 LifecycleSerial = 0;
};

struct FCrowdMassCommitPlan
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMassCommitRecord> Records;
  uint64 StableHash = 14695981039346656037ull;
  bool bValid = false;
};

class MASSCROWDRUNTIME_API FCrowdMassRuntimeBridge
{
public:
  static void BuildBoundarySnapshot(
    int32 FixedStepIndex,
    int32 PlanRevision,
    TConstArrayView<FCrowdMassBoundaryAgentRecord> Records,
    FCrowdMassBoundarySnapshot& OutSnapshot);

  static bool BuildGuidanceRecords(
    const FCrowdMassBoundarySnapshot& Snapshot,
    TConstArrayView<FCrowdGuidanceCandidate> SharedFlowCandidates,
    TConstArrayView<FCrowdGuidanceCandidate> TargetRegionCandidates,
    TConstArrayView<FCrowdGuidanceCandidate> BusinessCandidates,
    TArray<FCrowdMassGatherRecord>& OutRecords);

  static void BuildWorkBatches(
    int32 FixedStepIndex,
    int32 PlanRevision,
    TConstArrayView<FCrowdSimulationProfile> Profiles,
    const FCrowdEnvironmentSnapshot& Environment,
    const FCrowdTargetInput& Target,
    TConstArrayView<FCrowdMassGatherRecord> Records,
    TArray<FCrowdMassWorkBatch>& OutBatches);

  static void MergeWorkOutputs(
    TConstArrayView<FCrowdMassWorkBatchOutput> BatchOutputs,
    FCrowdMassCommitPlan& OutPlan);

  static bool ValidateCommitTargets(
    const FCrowdMassCommitPlan& Plan,
    TConstArrayView<FCrowdMassCommitTarget> Targets);

  // Callers must validate the complete plan against the complete target set
  // before applying any individual record. This function deliberately does
  // not turn a partial record loop into an atomic transaction by itself.
  static bool ApplyMovementToState(
    const FCrowdMassCommitRecord& Record,
    const FCrowdMassCommitTarget& Target,
    FCrowdMassSimulationStateFragment& InOutState,
    FCrowdMassMovementOutputFragment& OutMovement);
};
