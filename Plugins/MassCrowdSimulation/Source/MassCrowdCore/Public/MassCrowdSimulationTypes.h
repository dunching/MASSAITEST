#pragma once

#include "Containers/Array.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Templates/Function.h"

enum class ECrowdGuidanceProvider : uint8
{
  SharedFlow,
  TargetRegion,
  BusinessOverride,
  Stop
};

struct FCrowdAgentInput
{
  int32 AgentId = INDEX_NONE;
  uint32 LifecycleSerial = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float PhysicalRadiusCm = 0.0f;
  float HardSafetyGapCm = 0.0f;
  float SoftMarginCm = 0.0f;
  float Mobility = 1.0f;
  float MaximumSpeedCmps = 0.0f;
  uint32 CapabilityProfileKey = 0;
};

struct FCrowdEnvironmentObstacle
{
  int32 EnvironmentId = INDEX_NONE;
  FBox Bounds = FBox(EForceInit::ForceInit);
};

struct FCrowdEnvironmentSnapshot
{
  int32 Revision = INDEX_NONE;
  FBox SimulationBounds = FBox(EForceInit::ForceInit);
  TArray<FCrowdEnvironmentObstacle> Obstacles;
  uint32 StableHash = 0;
  bool bValid = false;
};

struct FCrowdTargetInput
{
  int32 TargetId = INDEX_NONE;
  int32 Revision = INDEX_NONE;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float PhysicalRadiusCm = 0.0f;
  bool bValid = false;
};

struct FCrowdGuidanceCandidate
{
  int32 AgentId = INDEX_NONE;
  ECrowdGuidanceProvider Provider = ECrowdGuidanceProvider::Stop;
  int32 PlanRevision = INDEX_NONE;
  FVector PreferredVelocity = FVector::ZeroVector;
  FVector DesiredLocation = FVector::ZeroVector;
  float DesiredYawDegrees = 0.0f;
  uint32 StableHash = 0;
  bool bValid = false;
};

struct FCrowdSimulationProfile
{
  uint32 StableProfileKey = 0;
  float FixedStepSeconds = 1.0f / 30.0f;
  float HardSafetyGapCm = 0.0f;
  float SoftMarginCm = 0.0f;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
  int32 SolverIterationCount = 0;
};

struct FCrowdMovementOutput
{
  int32 AgentId = INDEX_NONE;
  uint32 LifecycleSerial = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  uint32 StableHash = 0;
  bool bValid = false;
};

struct FCrowdRoundWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  FCrowdSimulationProfile SimulationProfile;
  FCrowdEnvironmentSnapshot Environment;
  FCrowdTargetInput Target;
  TArray<FCrowdAgentInput> Agents;
  TArray<FCrowdGuidanceCandidate> GuidanceCandidates;
};

struct FCrowdRoundWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMovementOutput> Movements;
  uint32 StableHash = 0;
  bool bValid = false;
};

class ICrowdEnvironmentProvider
{
public:
  virtual ~ICrowdEnvironmentProvider() = default;
  virtual bool BuildEnvironmentSnapshot(
    int32 RequestedRevision, FCrowdEnvironmentSnapshot& OutSnapshot) const = 0;
};

class ICrowdBusinessGuidanceProvider
{
public:
  virtual ~ICrowdBusinessGuidanceProvider() = default;
  virtual void BuildGuidanceCandidates(
    TConstArrayView<FCrowdAgentInput> Agents,
    const FCrowdTargetInput& Target,
    int32 PlanRevision,
    TArray<FCrowdGuidanceCandidate>& OutCandidates) const = 0;
};
