#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoParticleConstraintKernel.h"

enum class ECrowdDemoOpenSpawnRelaxationPhase : uint8
{
  Staging = 0,
  BatchActivation = 1,
  SourceInsertion = 2,
  PropagationAndInsertSettle = 3,
  Removal = 4,
  PostRemovalSettle = 5,
  Completed = 6,
  Failed = 7,
};

struct FCrowdDemoOpenSpawnRelaxationLayoutInput
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
};

struct FCrowdDemoOpenSpawnRelaxationLayoutAgent
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  FVector StagingLocation = FVector::ZeroVector;
  FVector ActiveLocation = FVector::ZeroVector;
  bool bInsertionSource = false;
  bool bRemovalAgent = false;
};

struct FCrowdDemoOpenSpawnRelaxationLayout
{
  bool bValid = false;
  int32 SourceAgentId = INDEX_NONE;
  int32 RemovedAgentId = INDEX_NONE;
  uint32 LayoutHash = 2166136261u;
  TArray<FCrowdDemoOpenSpawnRelaxationLayoutAgent> Agents;
};

struct FCrowdDemoOpenSpawnRelaxationAgentState
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  bool bParticleActive = false;
  bool bPendingBoundaryReset = false;
  FVector BoundaryResetLocation = FVector::ZeroVector;
};

struct FCrowdDemoOpenSpawnRelaxationEdge
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
};

struct FCrowdDemoOpenSpawnRelaxationRuntime
{
  bool bValid = false;
  ECrowdDemoOpenSpawnRelaxationPhase Phase =
    ECrowdDemoOpenSpawnRelaxationPhase::Staging;
  int32 PhaseStartStep = 0;
  int32 PhaseTransitionCount = 0;
  int32 BatchActivationCount = 0;
  int32 SourceAgentId = INDEX_NONE;
  int32 RemovedAgentId = INDEX_NONE;
  int32 PressurePropagationLayerMax = INDEX_NONE;
  int32 InsertSettlingStep = INDEX_NONE;
  int32 PostRemovalSettlingStep = INDEX_NONE;
  int32 OldLayoutReturnedAgentCount = 0;
  int32 NewEquilibriumDisplacedAgentCount = 0;
  int32 ExternalPreferredNonzeroCount = 0;
  FCrowdDemoParticleSettlingTracker InsertSettling;
  FCrowdDemoParticleSettlingTracker PostRemovalSettling;
  TArray<FCrowdDemoOpenSpawnRelaxationAgentState> Agents;
  TArray<FCrowdDemoOpenSpawnRelaxationEdge> CumulativeInfluenceEdges;
  TArray<int32> PropagationLayersByAgent;
  TArray<int32> LayerAgentCounts;
  TArray<int32> ActiveCountTransitions;
  TArray<FVector> PreInsertLocationsByAgent;
  uint32 ParticipationHash = 2166136261u;
  uint32 PropagationHash = 2166136261u;
  uint32 PhaseHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoOpenSpawnRelaxationKernel
{
public:
  static FCrowdDemoSharedFlowFieldConfig MakeOpenFlowConfig();

  static FCrowdDemoOpenSpawnRelaxationLayout BuildLayout(
    TConstArrayView<FCrowdDemoOpenSpawnRelaxationLayoutInput> Inputs,
    float PhysicalRadiusCm = 42.0f,
    float HardSafetyGapCm = 10.0f,
    float SoftMarginCm = 17.0f);

  static FCrowdDemoOpenSpawnRelaxationRuntime InitializeRuntime(
    const FCrowdDemoOpenSpawnRelaxationLayout& Layout);

  static void PrepareBoundary(
    int32 FixedStepIndex,
    const FCrowdDemoOpenSpawnRelaxationLayout& Layout,
    FCrowdDemoOpenSpawnRelaxationRuntime& Runtime);

  static void RecordParticleStep(
    int32 FixedStepIndex,
    TConstArrayView<FCrowdDemoParticleSoftPairInfluence> Influences,
    float MaxActualCorrectionCm,
    float SoftErrorCmP95,
    FCrowdDemoOpenSpawnRelaxationRuntime& Runtime);

  static void RecordFinalLocations(
    TConstArrayView<int32> AgentIds,
    TConstArrayView<FVector> Locations,
    FCrowdDemoOpenSpawnRelaxationRuntime& Runtime);

  static void RebuildHashes(FCrowdDemoOpenSpawnRelaxationRuntime& Runtime);
};
