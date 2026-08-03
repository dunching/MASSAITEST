#pragma once

#include "CoreMinimal.h"
#include "CrowdLocalPredictiveInteractionKernel.h"
#include "CrowdParticleConstraintKernel.h"
#include "MassCrowdWorkerContracts.h"

namespace CrowdWorkerResourceIds
{
  constexpr uint64 MovementControl = 0x43574D4F5643544Cull;
}

class FCrowdWorkerEntityStateStore;
struct FCrowdWorkerMovementControlResource;

struct MASSCROWDRUNTIME_API FCrowdWorkerMovementControlEntry
{
  FCrowdStableEntityRef EntityRef;
  int32 AgentId = INDEX_NONE;
  uint32 InteractionLayer = 0;
  int32 PreviousBlockedAgeSteps = 0;
  float MaximumSpeedCmps = 0.0f;
  float ParticleEnvironmentHardClearanceCm = 0.0f;
  float ParticlePhysicalRadiusCm = 0.0f;
  float ParticleHardSafetyGapCm = 0.0f;
  float ParticleSoftMarginCm = 0.0f;
  float ParticleMobility = 0.0f;
  FVector AutonomousPreferredVelocity = FVector::ZeroVector;
  bool bUseLocalVelocity = false;
  FVector LocalVelocity = FVector::ZeroVector;
  bool bLocalVelocityValid = false;
  bool bFreezeAtBoundaryLocation = false;
  FVector BoundaryLocation = FVector::ZeroVector;
  bool bVerticalOverride = false;
  float ProposedZ = 0.0f;
  float VerticalVelocityCmps = 0.0f;
  bool bParticleActive = true;
  bool bUseWorkerTargetGuidance = false;
  bool bUseAuthoritativePreferredVelocity = false;

  bool IsValid() const;
};

// Per-entity static/profile input. This payload travels as an ordered
// MovementProfileRevision intent and is stored independently from the global
// MovementControl resource so lifecycle/profile changes remain O(changes).
class MASSCROWDRUNTIME_API FCrowdWorkerMovementProfileCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574D52u;
  static constexpr uint16 SchemaVersion = 2;

  static bool Encode(
    const FCrowdWorkerMovementControlEntry& Profile,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerMovementControlEntry& OutProfile);
};

MASSCROWDRUNTIME_API bool CrowdWorkerResolveMovementProfiles(
  const FCrowdWorkerEntityStateStore& EntityStates,
  uint64 LastAppliedInputSequence,
  const FCrowdWorkerMovementControlResource& FrozenControl,
  TArray<FCrowdWorkerMovementControlEntry>& OutProfiles);

struct MASSCROWDRUNTIME_API FCrowdWorkerMovementControlResource
{
  uint64 Revision = 0;
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  bool bRunLocalPredictive = false;
  bool bApplyEnvironmentMovementConstraint = false;
  bool bRunParticleInteraction = false;
  FCrowdSharedFlowFieldConfig Environment;
  FCrowdLocalPredictiveSettings LocalPredictiveSettings;
  TArray<FCrowdLocalPredictiveGrantState> PreviousGrantStates;
  FCrowdParticleConstraintSettings ParticleSettings;
  bool bParticleConstrainToFlowBounds = true;
  TArray<FCrowdParticleConstraintAgent> ExternalParticleAgents;
  TArray<FCrowdWorkerMovementControlEntry> Entries;

  const FCrowdWorkerMovementControlEntry* Find(
    const FCrowdStableEntityRef& EntityRef) const;
  bool IsValid() const;
};

class MASSCROWDRUNTIME_API
FCrowdWorkerMovementControlResourceCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574D43u;
  static constexpr uint16 SchemaVersion = 9;

  static bool Encode(
    const FCrowdWorkerMovementControlResource& Resource,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerMovementControlResource& OutResource);
};
