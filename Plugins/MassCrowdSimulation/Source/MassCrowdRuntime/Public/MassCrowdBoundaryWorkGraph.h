#pragma once

#include "CoreMinimal.h"
#include "MassCrowdFacingFinalizeWork.h"
#include "MassCrowdMovementPipelineWork.h"
#include "MassCrowdParticlePipelineWork.h"
#include "MassCrowdSharedFlowWork.h"

// Immutable templates gathered on GT. Fields produced by an upstream WORK
// stage are deliberately left unset and are joined by Stable AgentId below.
struct FCrowdMassBoundaryFacingTemplate
{
  FCrowdFacingInput Input;
};

struct FCrowdMassBoundaryWorkGraphInput
{
  FCrowdMassSharedFlowSampleInput SharedFlow;
  FCrowdMassMovementPipelineWorkInput Movement;
  FCrowdMassParticlePipelineWorkInput ParticleTemplate;
  FCrowdFacingSettings FacingSettings;
  TArray<FCrowdMassBoundaryFacingTemplate> FacingTemplates;
};

struct FCrowdMassBoundaryWorkGraphOutput
{
  FCrowdMassSharedFlowSampleOutput SharedFlow;
  FCrowdMassMovementPipelineWorkOutput Movement;
  FCrowdMassParticlePipelineWorkOutput Particle;
  FCrowdMassFacingFinalizeWorkOutput FacingFinalize;
  uint64 StableHash = 0;
  bool bCompleted = false;
};

// Typed dependency joins used by the boundary orchestrator. These functions
// only consume immutable POD and never touch UObject, World, or EntityManager.
class MASSCROWDRUNTIME_API FCrowdMassBoundaryWorkGraph
{
public:
  static bool BuildMovementInput(
    const FCrowdMassBoundaryWorkGraphInput& Input,
    const FCrowdMassSharedFlowSampleOutput& SharedFlow,
    FCrowdMassMovementPipelineWorkInput& OutMovement);

  static bool BuildParticleInput(
    const FCrowdMassBoundaryWorkGraphInput& Input,
    const FCrowdMassMovementPipelineWorkOutput& Movement,
    FCrowdMassParticlePipelineWorkInput& OutParticle);

  static bool BuildFacingInput(
    const FCrowdMassBoundaryWorkGraphInput& Input,
    const FCrowdMassMovementPipelineWorkOutput& Movement,
    const FCrowdMassParticlePipelineWorkOutput& Particle,
    FCrowdMassFacingFinalizeWorkInput& OutFacing);

  static bool BuildFacingInputFromKinematics(
    const FCrowdMassBoundaryWorkGraphInput& Input,
    const FCrowdMassBoundarySnapshot& Snapshot,
    const FCrowdMassMovementPipelineWorkOutput& Movement,
    TConstArrayView<FCrowdMassFinalKinematicState> Kinematics,
    FCrowdMassFacingFinalizeWorkInput& OutFacing);

  static FCrowdMassBoundaryWorkGraphOutput Run(
    const FCrowdMassBoundaryWorkGraphInput& Input);
};
