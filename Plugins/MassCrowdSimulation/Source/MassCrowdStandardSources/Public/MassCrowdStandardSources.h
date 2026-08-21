#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSourceRuntime.h"

#include <type_traits>

namespace CrowdStandardSources
{
  inline constexpr FCrowdBehaviorProviderId ProviderId{100};

  inline constexpr FCrowdCapabilityId MoveCapability{10001};
  inline constexpr FCrowdCapabilityId FaceCapability{10002};
  inline constexpr FCrowdCapabilityId FormationCapability{10003};
  inline constexpr FCrowdCapabilityId ImpulseCapability{10004};
  inline constexpr FCrowdCapabilityId SemanticStateCapability{10005};

  inline constexpr FCrowdBehaviorContextTypeId
    TargetKinematicsContextType{10101};
  inline constexpr FCrowdBehaviorContextTypeId
    FormationAnchorContextType{10102};
  inline constexpr uint16 ContextSchemaVersion = 1;

  inline constexpr FCrowdBehaviorSourceTypeId MoveToLocation{11001};
  inline constexpr FCrowdBehaviorSourceTypeId ArriveAtLocation{11002};
  inline constexpr FCrowdBehaviorSourceTypeId FollowEntity{11003};
  inline constexpr FCrowdBehaviorSourceTypeId PursueEntity{11004};
  inline constexpr FCrowdBehaviorSourceTypeId FleeFromEntity{11005};
  inline constexpr FCrowdBehaviorSourceTypeId MaintainDistance{11006};
  inline constexpr FCrowdBehaviorSourceTypeId FaceMovement{11101};
  inline constexpr FCrowdBehaviorSourceTypeId FaceEntity{11102};
  inline constexpr FCrowdBehaviorSourceTypeId MovementLock{11201};
  inline constexpr FCrowdBehaviorSourceTypeId SpeedLimit{11202};
  inline constexpr FCrowdBehaviorSourceTypeId WanderSteering{11301};
  inline constexpr FCrowdBehaviorSourceTypeId FormationOffset{11302};
  inline constexpr FCrowdBehaviorSourceTypeId TimedImpulse{11303};
  inline constexpr FCrowdBehaviorSourceTypeId SemanticState{11401};

  constexpr uint32 PayloadSchema(
    const FCrowdBehaviorSourceTypeId TypeId)
  {
    return TypeId.Value + 1000u;
  }

  inline constexpr uint32 MaintainDistanceStateSchema = 13006;
  inline constexpr uint32 WanderStateSchema = 13301;
}

struct FCrowdTargetKinematicsV1
{
  FCrowdStableEntityRef TargetRef;
  FVector3f Position = FVector3f::ZeroVector;
  FVector3f Velocity = FVector3f::ZeroVector;
  FVector3f Facing = FVector3f::ForwardVector;
  uint32 NavLayer = 0;
  uint64 FactRevision = 0;
};

struct FCrowdFormationAnchorV1
{
  FCrowdStableEntityRef AnchorRef;
  FVector3f Position = FVector3f::ZeroVector;
  FVector3f Velocity = FVector3f::ZeroVector;
  FVector3f Facing = FVector3f::ForwardVector;
  FVector3f LocalSlotOffset = FVector3f::ZeroVector;
  uint32 NavLayer = 0;
  uint64 FactRevision = 0;
};

struct FCrowdMoveToLocationPayload
{
  FVector3f TargetLocation = FVector3f::ZeroVector;
  float MaximumSpeedCmps = 0.0f;
  float AcceptanceRadiusCm = 0.0f;
};

struct FCrowdArriveAtLocationPayload
{
  FVector3f TargetLocation = FVector3f::ZeroVector;
  float MaximumSpeedCmps = 0.0f;
  float AcceptanceRadiusCm = 0.0f;
  float SlowdownRadiusCm = 0.0f;
};

struct FCrowdFollowEntityPayload
{
  FCrowdStableEntityRef TargetRef;
  FVector3f LocalOffset = FVector3f::ZeroVector;
  float MaximumSpeedCmps = 0.0f;
  float AcceptanceRadiusCm = 0.0f;
  float PositionGain = 0.0f;
};

struct FCrowdPursueEntityPayload
{
  FCrowdStableEntityRef TargetRef;
  float MaximumSpeedCmps = 0.0f;
  float AcceptanceRadiusCm = 0.0f;
  float MaximumPredictionSeconds = 0.0f;
};

struct FCrowdFleeFromEntityPayload
{
  FCrowdStableEntityRef TargetRef;
  float MaximumSpeedCmps = 0.0f;
  float SafeDistanceCm = 0.0f;
  float MaximumPredictionSeconds = 0.0f;
};

struct FCrowdMaintainDistancePayload
{
  FCrowdStableEntityRef TargetRef;
  float MinimumDistanceCm = 0.0f;
  float MaximumDistanceCm = 0.0f;
  float HysteresisCm = 0.0f;
  float MaximumCorrectionSpeedCmps = 0.0f;
};

enum class ECrowdMaintainDistanceMode : uint8
{
  Hold = 0,
  Approach,
  Retreat
};

struct FCrowdMaintainDistanceState
{
  ECrowdMaintainDistanceMode Mode = ECrowdMaintainDistanceMode::Hold;
  uint8 Reserved[7] = {};
  uint64 LastTargetRevision = 0;
};

struct FCrowdFaceMovementPayload
{
  float MinimumSpeedCmps = 0.0f;
};

struct FCrowdFaceEntityPayload
{
  FCrowdStableEntityRef TargetRef;
};

struct FCrowdMovementLockPayload
{
  uint8 bLockMovement = 1;
};

struct FCrowdSpeedLimitPayload
{
  float MaximumSpeedCmps = 0.0f;
  uint64 AllowedNavLayerMask = MAX_uint64;
};

struct FCrowdWanderSteeringPayload
{
  float SpeedCmps = 0.0f;
  uint32 ReselectIntervalSteps = 0;
};

struct FCrowdWanderSteeringState
{
  uint32 RandomState = 0;
  uint32 Reserved0 = 0;
  int64 NextReselectFixedStep = INDEX_NONE;
  uint8 DirectionIndex = 0;
  uint8 Reserved1[7] = {};
};

struct FCrowdFormationOffsetPayload
{
  float PositionGain = 0.0f;
  float MaximumCorrectionSpeedCmps = 0.0f;
};

enum class ECrowdImpulseDecayMode : uint8
{
  Constant = 0,
  Linear
};

struct FCrowdTimedImpulsePayload
{
  FVector3f InitialVelocity = FVector3f::ZeroVector;
  ECrowdImpulseDecayMode DecayMode = ECrowdImpulseDecayMode::Linear;
};

// A Worker-owned semantic state marker. It deliberately contributes no
// Movement, Constraint, Particle, or Presentation output; consumers observe
// it through the ordered Behavior source set.
enum class ECrowdSemanticBehaviorState : uint8
{
  Waiting = 0,
  Relaxing,
  Settling,
  Count
};

struct FCrowdSemanticBehaviorStatePayload
{
  ECrowdSemanticBehaviorState State =
    ECrowdSemanticBehaviorState::Waiting;
  uint8 Reserved[7] = {};
};

#define CROWD_STANDARD_POD(Type) \
  static_assert(std::is_trivially_copyable_v<Type>); \
  static_assert(sizeof(Type) <= CrowdBehavior::MaxPayloadBytes)

CROWD_STANDARD_POD(FCrowdTargetKinematicsV1);
CROWD_STANDARD_POD(FCrowdFormationAnchorV1);
CROWD_STANDARD_POD(FCrowdMoveToLocationPayload);
CROWD_STANDARD_POD(FCrowdArriveAtLocationPayload);
CROWD_STANDARD_POD(FCrowdFollowEntityPayload);
CROWD_STANDARD_POD(FCrowdPursueEntityPayload);
CROWD_STANDARD_POD(FCrowdFleeFromEntityPayload);
CROWD_STANDARD_POD(FCrowdMaintainDistancePayload);
CROWD_STANDARD_POD(FCrowdMaintainDistanceState);
CROWD_STANDARD_POD(FCrowdFaceMovementPayload);
CROWD_STANDARD_POD(FCrowdFaceEntityPayload);
CROWD_STANDARD_POD(FCrowdMovementLockPayload);
CROWD_STANDARD_POD(FCrowdSpeedLimitPayload);
CROWD_STANDARD_POD(FCrowdWanderSteeringPayload);
CROWD_STANDARD_POD(FCrowdWanderSteeringState);
CROWD_STANDARD_POD(FCrowdFormationOffsetPayload);
CROWD_STANDARD_POD(FCrowdTimedImpulsePayload);
CROWD_STANDARD_POD(FCrowdSemanticBehaviorStatePayload);

#undef CROWD_STANDARD_POD

MASSCROWDSTANDARDSOURCES_API
TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
CreateCrowdStandardSourcesProvider();
