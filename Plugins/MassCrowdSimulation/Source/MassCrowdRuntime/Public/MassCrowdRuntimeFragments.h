#pragma once

#include "CoreMinimal.h"
#include "CrowdGuidanceComposeKernel.h"
#include "CrowdFacingKernel.h"
#include "CrowdLocalPredictiveInteractionKernel.h"
#include "CrowdParticleConstraintKernel.h"
#include "MassCrowdSimulationTypes.h"
#include "MassEntityTypes.h"
#include "MassCrowdRuntimeFragments.generated.h"

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassAgentTag : public FMassTag
{
  GENERATED_BODY()
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassAgentFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) int32 AgentId = INDEX_NONE;
  UPROPERTY(Transient) int32 LifecycleSerial = 1;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassSimulationStateFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) FVector Position = FVector::ZeroVector;
  UPROPERTY(Transient) FVector Velocity = FVector::ZeroVector;
  UPROPERTY(Transient) float YawDegrees = 0.0f;
  UPROPERTY(Transient) int32 PlanRevision = INDEX_NONE;
  UPROPERTY(Transient) bool bInitialized = false;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassPropertiesFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) float PhysicalRadiusCm = 42.0f;
  UPROPERTY(Transient) float HardSafetyGapCm = 10.0f;
  UPROPERTY(Transient) float SoftMarginCm = 17.0f;
  UPROPERTY(Transient) float Mobility = 1.0f;
  UPROPERTY(Transient) float MaximumSpeedCmps = 300.0f;
  UPROPERTY(Transient) uint32 CapabilityProfileKey = 0;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassGuidanceCandidatesFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdGuidanceCandidate SharedFlow;
  FCrowdGuidanceCandidate TargetRegion;
  FCrowdGuidanceCandidate BusinessOverride;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassComposedGuidanceFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdComposedGuidance Value;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassLocalVelocityFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdLocalPredictiveResult Value;
  UPROPERTY(Transient) int32 PlanRevision = INDEX_NONE;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassParticleConstraintFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdParticleConstraintResult Value;
  UPROPERTY(Transient) int32 PlanRevision = INDEX_NONE;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassFacingFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdFacingResult Value;
  UPROPERTY(Transient) int32 PlanRevision = INDEX_NONE;
  UPROPERTY(Transient) int32 ConsecutiveFinalSettleSteps = 0;
  UPROPERTY(Transient) bool bFinalPositionSettled = false;
};

USTRUCT()
struct MASSCROWDRUNTIME_API FCrowdMassMovementOutputFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdMovementOutput Value;
};
