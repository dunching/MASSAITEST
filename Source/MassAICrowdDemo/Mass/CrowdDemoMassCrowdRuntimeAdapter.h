#pragma once

#include "CoreMinimal.h"
#include "CrowdLocalPredictiveInteractionKernel.h"
#include "CrowdParticleConstraintKernel.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "CrowdTargetRegionTransportKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoTargetRegionTransportKernel.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "MassCrowdRuntimeBridge.h"

class MASSAICROWDDEMO_API FCrowdDemoMassCrowdRuntimeAdapter
{
public:
  static bool BuildBoundaryAgentRecord(
    const FCrowdDemoMassIdentityFragment& Identity,
    const FCrowdMassAgentFragment& RuntimeIdentity,
    const FCrowdMassBehaviorFragment& RuntimeBehavior,
    const FCrowdDemoRoundSimStateFragment& State,
    const FCrowdDemoMassMovementFragment& Movement,
    const FCrowdDemoParticlePropertiesFragment& Particle,
    FCrowdMassBoundaryAgentRecord& OutRecord);

  static FCrowdMassCommitTarget BuildCommitTarget(
    const FCrowdDemoMassIdentityFragment& Identity,
    const FCrowdMassAgentFragment& RuntimeIdentity);

  static FCrowdDemoComposedGuidance BuildDemoComposedGuidance(
    const FCrowdComposedGuidance& Source);

  static FCrowdDemoGuidanceCandidate BuildDemoGuidanceCandidate(
    const FCrowdGuidanceCandidate& Source);

  static FCrowdGuidanceCandidate BuildCoreGuidanceCandidate(
    const FCrowdDemoGuidanceCandidate& Source);

  static FCrowdSharedFlowFieldConfig BuildCoreFlowConfig(
    const FCrowdDemoSharedFlowFieldConfig& Source);

  static FCrowdDemoSharedFlowField BuildDemoFlowField(
    const FCrowdSharedFlowField& Source);

  static FCrowdDemoSharedFlowSample BuildDemoFlowSample(
    const FCrowdSharedFlowSample& Source);

  static FCrowdTargetRegionTransportSettings BuildCoreTargetRegionSettings(
    const FCrowdDemoTargetRegionTransportSettings& Source);
  static FCrowdTargetRegionTransportAgent BuildCoreTargetRegionAgent(
    const FCrowdDemoTargetRegionTransportAgent& Source);
  static FCrowdTargetPolarTopology BuildCoreTargetRegionTopology(
    const FCrowdDemoTargetPolarTopology& Source);
  static FCrowdTargetRegionDemandResult BuildCoreTargetRegionDemand(
    const FCrowdDemoTargetRegionDemandResult& Source);
  static FCrowdTargetRegionFlowPlan BuildCoreTargetRegionPlan(
    const FCrowdDemoTargetRegionFlowPlan& Source);
  static FCrowdTargetRegionQuotaExecutionState BuildCoreTargetRegionExecution(
    const FCrowdDemoTargetRegionQuotaExecutionState& Source);

  static FCrowdDemoTargetPolarTopology BuildDemoTargetRegionTopology(
    const FCrowdTargetPolarTopology& Source);
  static FCrowdDemoTargetPolarTopologySummary BuildDemoTargetRegionTopologySummary(
    const FCrowdTargetPolarTopologySummary& Source);
  static FCrowdDemoTargetRegionDemandResult BuildDemoTargetRegionDemand(
    const FCrowdTargetRegionDemandResult& Source);
  static FCrowdDemoTargetRegionFlowPlan BuildDemoTargetRegionPlan(
    const FCrowdTargetRegionFlowPlan& Source);
  static FCrowdDemoTargetRegionQuotaExecutionState BuildDemoTargetRegionExecution(
    const FCrowdTargetRegionQuotaExecutionState& Source);
  static FCrowdDemoTargetRegionPlanReplacementSummary BuildDemoTargetRegionReplacement(
    const FCrowdTargetRegionPlanReplacementSummary& Source);
  static FCrowdDemoTargetRegionPlanValidationResult BuildDemoTargetRegionValidation(
    const FCrowdTargetRegionPlanValidationResult& Source);
  static FCrowdDemoTargetRegionGuidanceResult BuildDemoTargetRegionGuidance(
    const FCrowdTargetRegionGuidanceResult& Source);
  static FCrowdDemoTargetRegionGuidanceSummary BuildDemoTargetRegionGuidanceSummary(
    const FCrowdTargetRegionGuidanceSummary& Source);

  static FCrowdLocalPredictiveSettings BuildCoreLocalPredictiveSettings(
    const FCrowdDemoLocalPredictiveSettings& Source);

  static FCrowdLocalPredictiveGrantState BuildCoreLocalPredictiveGrant(
    const FCrowdDemoLocalPredictiveGrantState& Source);

  static FCrowdDemoLocalPredictiveAgent BuildDemoLocalPredictiveAgent(
    const FCrowdLocalPredictiveAgent& Source);

  static FCrowdDemoLocalPredictivePair BuildDemoLocalPredictivePair(
    const FCrowdLocalPredictivePair& Source);

  static FCrowdDemoLocalPredictiveGrantState BuildDemoLocalPredictiveGrant(
    const FCrowdLocalPredictiveGrantState& Source);

  static FCrowdDemoLocalPredictiveResult BuildDemoLocalPredictiveResult(
    const FCrowdLocalPredictiveResult& Source);

  static FCrowdDemoLocalPredictiveSummary BuildDemoLocalPredictiveSummary(
    const FCrowdLocalPredictiveSummary& Source);

  static FCrowdDemoLocalPredictiveDiagnosticTrace
    BuildDemoLocalPredictiveTrace(
      const FCrowdLocalPredictiveDiagnosticTrace& Source);

  static FCrowdParticleConstraintEnvironment BuildCoreParticleEnvironment(
    const FCrowdDemoParticleConstraintEnvironment& Source);

  static FCrowdParticleConstraintSettings BuildCoreParticleSettings(
    const FCrowdDemoParticleConstraintSettings& Source);

  static bool BuildCoreParticleAgent(
    const FCrowdMassAgentFragment& Identity,
    const FCrowdMassPropertiesFragment& Properties,
    const FVector& StartPosition,
    const FVector& PredictedPosition,
    float EnvironmentHardClearanceCm,
    FCrowdParticleConstraintAgent& OutAgent);

  static FCrowdDemoParticleConstraintAgent BuildDemoParticleAgent(
    const FCrowdParticleConstraintAgent& Source);

  static FCrowdDemoParticleConstraintPair BuildDemoParticlePair(
    const FCrowdParticleConstraintPair& Source);

  static FCrowdDemoParticleConstraintResult BuildDemoParticleResult(
    const FCrowdParticleConstraintResult& Source);

  static FCrowdDemoParticleConstraintSummary BuildDemoParticleSummary(
    const FCrowdParticleConstraintSummary& Source);

  static FCrowdDemoParticleConstraintTrace BuildDemoParticleTrace(
    const FCrowdParticleConstraintTrace& Source);

  static bool ApplyCommitRecord(
    const FCrowdMassCommitRecord& Record,
    const FCrowdDemoMassIdentityFragment& Identity,
    const FCrowdMassAgentFragment& RuntimeIdentity,
    FCrowdDemoRoundSimStateFragment& InOutState);
};
