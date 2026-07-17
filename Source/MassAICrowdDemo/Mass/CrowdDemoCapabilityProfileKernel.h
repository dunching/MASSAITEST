#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

enum class ECrowdDemoParticleProfileId : uint8
{
  SmallLight = 0,
  Standard = 1,
  LargeHeavy = 2,
};

enum class ECrowdDemoTargetDistanceCapability : uint8
{
  Melee = 0,
  MidRange = 1,
  Ranged = 2,
};

struct FCrowdDemoCapabilityProfile
{
  int32 ProfileId = INDEX_NONE;
  ECrowdDemoParticleProfileId ParticleProfileId = ECrowdDemoParticleProfileId::Standard;
  ECrowdDemoTargetDistanceCapability TargetCapability =
    ECrowdDemoTargetDistanceCapability::Melee;
  FCrowdDemoParticleProfile Particle;
  float MinimumCenterDistanceCm = 0.0f;
  float MaximumCenterDistanceCm = 0.0f;
  float TargetPhysicalRadiusCm = 100.0f;
  float TargetHardSafetyGapCm = 10.0f;
  float NormalizedMinimumCenterDistanceCm = 0.0f;
  float NormalizedMaximumCenterDistanceCm = 0.0f;
  uint32 CapabilityProfileKey = 0;
  bool bMinimumNormalizedToHardDistance = false;
  bool bValid = false;
};

struct FCrowdDemoCapabilityAgentAssignment
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  int32 ProfileId = INDEX_NONE;
  uint32 CapabilityProfileKey = 0;
};

struct FCrowdDemoCapabilityCohort
{
  uint32 CapabilityProfileKey = 0;
  FCrowdDemoCapabilityProfile Profile;
  TArray<int32> AgentIds;
  uint32 MembershipHash = 2166136261u;
};

struct FCrowdDemoCapabilityProfileSummary
{
  bool bValid = false;
  int32 ProfileCount = 0;
  int32 AgentCount = 0;
  int32 MinimumNormalizedCount = 0;
  int32 InvalidProfileCount = 0;
  int32 DuplicateAgentIdCount = 0;
  int32 MissingProfileCount = 0;
  int32 ProfileKeyCollisionCount = 0;
  uint32 MembershipHash = 2166136261u;
};

struct FCrowdDemoCapabilityDemandPhase
{
  uint32 CapabilityProfileKey = 0;
  int32 DemandRegionPhaseOffset = 0;
};

class MASSAICROWDDEMO_API FCrowdDemoCapabilityProfileKernel
{
public:
  static float ComputeTargetHardDistanceCm(const FCrowdDemoCapabilityProfile& Profile);

  static float ComputePairHardDistanceCm(
    const FCrowdDemoParticleProfile& A,
    const FCrowdDemoParticleProfile& B);

  static float ComputePairSoftDistanceCm(
    const FCrowdDemoParticleProfile& A,
    const FCrowdDemoParticleProfile& B);

  static bool ComputeMobilityShares(
    float MobilityA,
    float MobilityB,
    float& OutShareA,
    float& OutShareB);

  static bool IsStraightCorridorFeasible(
    float CorridorWidthCm,
    const FCrowdDemoParticleProfile& Profile);

  static bool NormalizeProfile(
    FCrowdDemoCapabilityProfile& InOutProfile,
    float PositionQuantumCm = 1.0f);

  static uint32 ComputeCapabilityProfileKey(
    const FCrowdDemoCapabilityProfile& NormalizedProfile,
    float PositionQuantumCm = 1.0f);

  static void BuildP0Profiles(TArray<FCrowdDemoCapabilityProfile>& OutProfiles);

  static void BuildP0Assignments(
    int32 FirstAgentId,
    TArray<FCrowdDemoCapabilityAgentAssignment>& OutAssignments);

  static void BuildCohorts(
    TConstArrayView<FCrowdDemoCapabilityProfile> Profiles,
    TConstArrayView<FCrowdDemoCapabilityAgentAssignment> Assignments,
    TArray<FCrowdDemoCapabilityCohort>& OutCohorts,
    FCrowdDemoCapabilityProfileSummary& OutSummary);

  static bool BuildDemandRegionPhaseOffsets(
    TConstArrayView<FCrowdDemoCapabilityCohort> Cohorts,
    int32 DemandRegionCount,
    TArray<FCrowdDemoCapabilityDemandPhase>& OutPhases,
    uint32& OutHash);

  static bool ShareTargetDistanceBand(
    const FCrowdDemoCapabilityProfile& A,
    const FCrowdDemoCapabilityProfile& B);
};
