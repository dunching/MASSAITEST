#include "Mass/CrowdDemoCapabilityProfileKernel.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const int64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>((static_cast<uint64>(Value) >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  int32 Quantize(const float Value, const float Quantum)
  {
    return FMath::RoundToInt(Value / FMath::Max(Quantum, 0.001f));
  }

  int32 QuantizeMobility(const float Value)
  {
    return FMath::RoundToInt(Value * 1000.0f);
  }

  bool SameProfileFacts(
    const FCrowdDemoCapabilityProfile& A,
    const FCrowdDemoCapabilityProfile& B)
  {
    return Quantize(A.Particle.PhysicalRadiusCm, 1.0f) == Quantize(B.Particle.PhysicalRadiusCm, 1.0f)
      && Quantize(A.Particle.HardSafetyGapCm, 1.0f) == Quantize(B.Particle.HardSafetyGapCm, 1.0f)
      && Quantize(A.Particle.SoftMarginCm, 1.0f) == Quantize(B.Particle.SoftMarginCm, 1.0f)
      && QuantizeMobility(A.Particle.Mobility) == QuantizeMobility(B.Particle.Mobility)
      && Quantize(A.NormalizedMinimumCenterDistanceCm, 1.0f)
        == Quantize(B.NormalizedMinimumCenterDistanceCm, 1.0f)
      && Quantize(A.NormalizedMaximumCenterDistanceCm, 1.0f)
        == Quantize(B.NormalizedMaximumCenterDistanceCm, 1.0f)
      && Quantize(A.TargetPhysicalRadiusCm, 1.0f) == Quantize(B.TargetPhysicalRadiusCm, 1.0f)
      && Quantize(A.TargetHardSafetyGapCm, 1.0f) == Quantize(B.TargetHardSafetyGapCm, 1.0f);
  }

  FCrowdDemoCapabilityProfile MakeProfile(
    const int32 ProfileId,
    const ECrowdDemoParticleProfileId ParticleId,
    const ECrowdDemoTargetDistanceCapability TargetCapability)
  {
    FCrowdDemoCapabilityProfile Result;
    Result.ProfileId = ProfileId;
    Result.ParticleProfileId = ParticleId;
    Result.TargetCapability = TargetCapability;
    switch (ParticleId)
    {
    case ECrowdDemoParticleProfileId::SmallLight:
      Result.Particle.PhysicalRadiusCm = 30.0f;
      Result.Particle.Mobility = 2.0f;
      break;
    case ECrowdDemoParticleProfileId::LargeHeavy:
      Result.Particle.PhysicalRadiusCm = 60.0f;
      Result.Particle.Mobility = 0.5f;
      break;
    default:
      Result.Particle.PhysicalRadiusCm = 42.0f;
      Result.Particle.Mobility = 1.0f;
      break;
    }
    Result.Particle.HardSafetyGapCm = 10.0f;
    Result.Particle.SoftMarginCm = 17.0f;
    switch (TargetCapability)
    {
    case ECrowdDemoTargetDistanceCapability::MidRange:
      Result.MinimumCenterDistanceCm = 400.0f;
      Result.MaximumCenterDistanceCm = 600.0f;
      break;
    case ECrowdDemoTargetDistanceCapability::Ranged:
      Result.MinimumCenterDistanceCm = 700.0f;
      Result.MaximumCenterDistanceCm = 850.0f;
      break;
    default:
      Result.MinimumCenterDistanceCm = 170.0f;
      Result.MaximumCenterDistanceCm = 300.0f;
      break;
    }
    FCrowdDemoCapabilityProfileKernel::NormalizeProfile(Result);
    return Result;
  }
}

float FCrowdDemoCapabilityProfileKernel::ComputeTargetHardDistanceCm(
  const FCrowdDemoCapabilityProfile& Profile)
{
  return Profile.TargetPhysicalRadiusCm + Profile.Particle.PhysicalRadiusCm
    + FMath::Max(Profile.TargetHardSafetyGapCm, Profile.Particle.HardSafetyGapCm);
}

float FCrowdDemoCapabilityProfileKernel::ComputePairHardDistanceCm(
  const FCrowdDemoParticleProfile& A,
  const FCrowdDemoParticleProfile& B)
{
  return A.PhysicalRadiusCm + B.PhysicalRadiusCm
    + FMath::Max(A.HardSafetyGapCm, B.HardSafetyGapCm);
}

float FCrowdDemoCapabilityProfileKernel::ComputePairSoftDistanceCm(
  const FCrowdDemoParticleProfile& A,
  const FCrowdDemoParticleProfile& B)
{
  return ComputePairHardDistanceCm(A, B) + A.SoftMarginCm + B.SoftMarginCm;
}

bool FCrowdDemoCapabilityProfileKernel::ComputeMobilityShares(
  const float MobilityA,
  const float MobilityB,
  float& OutShareA,
  float& OutShareB)
{
  OutShareA = 0.0f;
  OutShareB = 0.0f;
  if (!FMath::IsFinite(MobilityA) || !FMath::IsFinite(MobilityB)
    || MobilityA < 0.0f || MobilityB < 0.0f)
  {
    return false;
  }
  const float Total = MobilityA + MobilityB;
  if (Total <= UE_SMALL_NUMBER)
  {
    return true;
  }
  OutShareA = MobilityA / Total;
  OutShareB = MobilityB / Total;
  return true;
}

bool FCrowdDemoCapabilityProfileKernel::IsStraightCorridorFeasible(
  const float CorridorWidthCm,
  const FCrowdDemoParticleProfile& Profile)
{
  if (!FMath::IsFinite(CorridorWidthCm) || CorridorWidthCm < 0.0f)
  {
    return false;
  }
  return CorridorWidthCm + KINDA_SMALL_NUMBER
    >= 2.0f * (Profile.PhysicalRadiusCm + Profile.HardSafetyGapCm);
}

bool FCrowdDemoCapabilityProfileKernel::NormalizeProfile(
  FCrowdDemoCapabilityProfile& InOutProfile,
  const float PositionQuantumCm)
{
  InOutProfile.bValid = false;
  InOutProfile.CapabilityProfileKey = 0;
  const FCrowdDemoParticleProfile& Particle = InOutProfile.Particle;
  if (!FMath::IsFinite(Particle.PhysicalRadiusCm)
    || !FMath::IsFinite(Particle.HardSafetyGapCm)
    || !FMath::IsFinite(Particle.SoftMarginCm)
    || !FMath::IsFinite(Particle.Mobility)
    || !FMath::IsFinite(InOutProfile.MinimumCenterDistanceCm)
    || !FMath::IsFinite(InOutProfile.MaximumCenterDistanceCm)
    || !FMath::IsFinite(InOutProfile.TargetPhysicalRadiusCm)
    || !FMath::IsFinite(InOutProfile.TargetHardSafetyGapCm)
    || PositionQuantumCm <= 0.0f
    || Particle.PhysicalRadiusCm <= 0.0f
    || Particle.HardSafetyGapCm < 0.0f
    || Particle.SoftMarginCm < 0.0f
    || Particle.Mobility < 0.0f
    || InOutProfile.TargetPhysicalRadiusCm < 0.0f
    || InOutProfile.TargetHardSafetyGapCm < 0.0f
    || InOutProfile.MaximumCenterDistanceCm < 0.0f)
  {
    return false;
  }
  const float HardDistance = ComputeTargetHardDistanceCm(InOutProfile);
  const float QuantizedHardDistance = Quantize(HardDistance, PositionQuantumCm) * PositionQuantumCm;
  const float QuantizedMinimum = Quantize(
    FMath::Max(InOutProfile.MinimumCenterDistanceCm, QuantizedHardDistance),
    PositionQuantumCm) * PositionQuantumCm;
  const float QuantizedMaximum = Quantize(
    FMath::Max(InOutProfile.MaximumCenterDistanceCm, QuantizedMinimum),
    PositionQuantumCm) * PositionQuantumCm;
  InOutProfile.NormalizedMinimumCenterDistanceCm = QuantizedMinimum;
  InOutProfile.NormalizedMaximumCenterDistanceCm = QuantizedMaximum;
  InOutProfile.bMinimumNormalizedToHardDistance =
    QuantizedMinimum > InOutProfile.MinimumCenterDistanceCm + KINDA_SMALL_NUMBER;
  InOutProfile.CapabilityProfileKey = ComputeCapabilityProfileKey(InOutProfile, PositionQuantumCm);
  InOutProfile.bValid = InOutProfile.CapabilityProfileKey != 0;
  return InOutProfile.bValid;
}

uint32 FCrowdDemoCapabilityProfileKernel::ComputeCapabilityProfileKey(
  const FCrowdDemoCapabilityProfile& Profile,
  const float PositionQuantumCm)
{
  if (PositionQuantumCm <= 0.0f)
  {
    return 0;
  }
  uint32 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, Quantize(Profile.Particle.PhysicalRadiusCm, PositionQuantumCm));
  Hash = Fold(Hash, Quantize(Profile.Particle.HardSafetyGapCm, PositionQuantumCm));
  Hash = Fold(Hash, Quantize(Profile.Particle.SoftMarginCm, PositionQuantumCm));
  Hash = Fold(Hash, QuantizeMobility(Profile.Particle.Mobility));
  Hash = Fold(Hash, Quantize(Profile.NormalizedMinimumCenterDistanceCm, PositionQuantumCm));
  Hash = Fold(Hash, Quantize(Profile.NormalizedMaximumCenterDistanceCm, PositionQuantumCm));
  Hash = Fold(Hash, Quantize(Profile.TargetPhysicalRadiusCm, PositionQuantumCm));
  Hash = Fold(Hash, Quantize(Profile.TargetHardSafetyGapCm, PositionQuantumCm));
  return Hash == 0 ? 1u : Hash;
}

void FCrowdDemoCapabilityProfileKernel::BuildP0Profiles(
  TArray<FCrowdDemoCapabilityProfile>& OutProfiles)
{
  OutProfiles.Reset();
  OutProfiles.Add(MakeProfile(0, ECrowdDemoParticleProfileId::SmallLight,
    ECrowdDemoTargetDistanceCapability::Melee));
  OutProfiles.Add(MakeProfile(1, ECrowdDemoParticleProfileId::SmallLight,
    ECrowdDemoTargetDistanceCapability::Ranged));
  OutProfiles.Add(MakeProfile(2, ECrowdDemoParticleProfileId::Standard,
    ECrowdDemoTargetDistanceCapability::Melee));
  OutProfiles.Add(MakeProfile(3, ECrowdDemoParticleProfileId::Standard,
    ECrowdDemoTargetDistanceCapability::MidRange));
  OutProfiles.Add(MakeProfile(4, ECrowdDemoParticleProfileId::Standard,
    ECrowdDemoTargetDistanceCapability::Ranged));
  OutProfiles.Add(MakeProfile(5, ECrowdDemoParticleProfileId::LargeHeavy,
    ECrowdDemoTargetDistanceCapability::Melee));
  OutProfiles.Add(MakeProfile(6, ECrowdDemoParticleProfileId::LargeHeavy,
    ECrowdDemoTargetDistanceCapability::Ranged));
}

void FCrowdDemoCapabilityProfileKernel::BuildP0Assignments(
  const int32 FirstAgentId,
  TArray<FCrowdDemoCapabilityAgentAssignment>& OutAssignments)
{
  static constexpr int32 ProfileByFormation[20] = {
    0, 0, 0,
    1, 1, 1,
    2, 2, 2,
    3, 3,
    4, 4, 4,
    5, 5, 5,
    6, 6, 6,
  };
  TArray<FCrowdDemoCapabilityProfile> Profiles;
  BuildP0Profiles(Profiles);
  OutAssignments.Reset(20);
  for (int32 FormationIndex = 0; FormationIndex < 20; ++FormationIndex)
  {
    const int32 ProfileId = ProfileByFormation[FormationIndex];
    FCrowdDemoCapabilityAgentAssignment& Assignment = OutAssignments.AddDefaulted_GetRef();
    Assignment.AgentId = FirstAgentId + FormationIndex;
    Assignment.FormationIndex = FormationIndex;
    Assignment.ProfileId = ProfileId;
    Assignment.CapabilityProfileKey = Profiles[ProfileId].CapabilityProfileKey;
  }
}

void FCrowdDemoCapabilityProfileKernel::BuildCohorts(
  const TConstArrayView<FCrowdDemoCapabilityProfile> Profiles,
  const TConstArrayView<FCrowdDemoCapabilityAgentAssignment> Assignments,
  TArray<FCrowdDemoCapabilityCohort>& OutCohorts,
  FCrowdDemoCapabilityProfileSummary& OutSummary)
{
  OutCohorts.Reset();
  OutSummary = FCrowdDemoCapabilityProfileSummary();

  TArray<FCrowdDemoCapabilityProfile> SortedProfiles(Profiles);
  SortedProfiles.Sort([](const auto& A, const auto& B)
  {
    if (A.CapabilityProfileKey != B.CapabilityProfileKey)
      return A.CapabilityProfileKey < B.CapabilityProfileKey;
    return A.ProfileId < B.ProfileId;
  });
  TMap<int32, FCrowdDemoCapabilityProfile> ProfileById;
  TMap<uint32, FCrowdDemoCapabilityProfile> FactsByKey;
  for (const FCrowdDemoCapabilityProfile& Profile : SortedProfiles)
  {
    if (!Profile.bValid || Profile.CapabilityProfileKey == 0)
    {
      ++OutSummary.InvalidProfileCount;
      continue;
    }
    if (const FCrowdDemoCapabilityProfile* Existing = FactsByKey.Find(Profile.CapabilityProfileKey))
    {
      if (!SameProfileFacts(*Existing, Profile))
        ++OutSummary.ProfileKeyCollisionCount;
    }
    else
    {
      FactsByKey.Add(Profile.CapabilityProfileKey, Profile);
    }
    ProfileById.Add(Profile.ProfileId, Profile);
    OutSummary.MinimumNormalizedCount += Profile.bMinimumNormalizedToHardDistance ? 1 : 0;
  }

  TArray<FCrowdDemoCapabilityAgentAssignment> SortedAssignments(Assignments);
  SortedAssignments.Sort([](const auto& A, const auto& B)
  {
    if (A.AgentId != B.AgentId) return A.AgentId < B.AgentId;
    return A.FormationIndex < B.FormationIndex;
  });
  int32 PreviousAgentId = INDEX_NONE;
  TMap<uint32, int32> CohortIndexByKey;
  for (const FCrowdDemoCapabilityAgentAssignment& Assignment : SortedAssignments)
  {
    if (Assignment.AgentId == PreviousAgentId)
    {
      ++OutSummary.DuplicateAgentIdCount;
      continue;
    }
    PreviousAgentId = Assignment.AgentId;
    const FCrowdDemoCapabilityProfile* Profile = ProfileById.Find(Assignment.ProfileId);
    if (!Profile || !Profile->bValid
      || Assignment.CapabilityProfileKey != Profile->CapabilityProfileKey)
    {
      ++OutSummary.MissingProfileCount;
      continue;
    }
    int32* ExistingIndex = CohortIndexByKey.Find(Profile->CapabilityProfileKey);
    int32 CohortIndex = INDEX_NONE;
    if (!ExistingIndex)
    {
      CohortIndex = OutCohorts.AddDefaulted();
      CohortIndexByKey.Add(Profile->CapabilityProfileKey, CohortIndex);
      OutCohorts[CohortIndex].CapabilityProfileKey = Profile->CapabilityProfileKey;
      OutCohorts[CohortIndex].Profile = *Profile;
    }
    else
    {
      CohortIndex = *ExistingIndex;
    }
    OutCohorts[CohortIndex].AgentIds.Add(Assignment.AgentId);
  }
  OutCohorts.Sort([](const auto& A, const auto& B)
  {
    return A.CapabilityProfileKey < B.CapabilityProfileKey;
  });
  uint32 MembershipHash = Fold(FnvOffset, OutCohorts.Num());
  for (FCrowdDemoCapabilityCohort& Cohort : OutCohorts)
  {
    Cohort.AgentIds.Sort();
    uint32 CohortHash = Fold(FnvOffset, Cohort.CapabilityProfileKey);
    CohortHash = Fold(CohortHash, Cohort.AgentIds.Num());
    for (const int32 AgentId : Cohort.AgentIds)
    {
      CohortHash = Fold(CohortHash, AgentId);
      MembershipHash = Fold(MembershipHash, Cohort.CapabilityProfileKey);
      MembershipHash = Fold(MembershipHash, AgentId);
    }
    Cohort.MembershipHash = CohortHash;
  }
  OutSummary.ProfileCount = OutCohorts.Num();
  OutSummary.AgentCount = SortedAssignments.Num() - OutSummary.DuplicateAgentIdCount
    - OutSummary.MissingProfileCount;
  OutSummary.MembershipHash = MembershipHash;
  OutSummary.bValid = OutSummary.InvalidProfileCount == 0
    && OutSummary.DuplicateAgentIdCount == 0
    && OutSummary.MissingProfileCount == 0
    && OutSummary.ProfileKeyCollisionCount == 0
    && OutSummary.AgentCount == SortedAssignments.Num();
}

bool FCrowdDemoCapabilityProfileKernel::BuildDemandRegionPhaseOffsets(
  const TConstArrayView<FCrowdDemoCapabilityCohort> Cohorts,
  const int32 DemandRegionCount,
  TArray<FCrowdDemoCapabilityDemandPhase>& OutPhases,
  uint32& OutHash)
{
  OutPhases.Reset();
  OutHash = FnvOffset;
  if (DemandRegionCount <= 0 || Cohorts.IsEmpty())
  {
    return false;
  }

  struct FPhaseFact
  {
    uint32 CapabilityProfileKey = 0;
    int32 MinimumDistanceCm = 0;
    int32 MaximumDistanceCm = 0;
  };
  TArray<FPhaseFact> Facts;
  Facts.Reserve(Cohorts.Num());
  for (const FCrowdDemoCapabilityCohort& Cohort : Cohorts)
  {
    if (Cohort.CapabilityProfileKey == 0 || !Cohort.Profile.bValid)
    {
      return false;
    }
    FPhaseFact& Fact = Facts.AddDefaulted_GetRef();
    Fact.CapabilityProfileKey = Cohort.CapabilityProfileKey;
    Fact.MinimumDistanceCm = Quantize(
      Cohort.Profile.NormalizedMinimumCenterDistanceCm, 1.0f);
    Fact.MaximumDistanceCm = Quantize(
      Cohort.Profile.NormalizedMaximumCenterDistanceCm, 1.0f);
  }
  Facts.Sort([](const FPhaseFact& A, const FPhaseFact& B)
  {
    if (A.MinimumDistanceCm != B.MinimumDistanceCm)
      return A.MinimumDistanceCm < B.MinimumDistanceCm;
    if (A.MaximumDistanceCm != B.MaximumDistanceCm)
      return A.MaximumDistanceCm < B.MaximumDistanceCm;
    return A.CapabilityProfileKey < B.CapabilityProfileKey;
  });
  for (int32 A = 0; A < Facts.Num(); ++A)
    for (int32 B = A + 1; B < Facts.Num(); ++B)
      if (Facts[A].CapabilityProfileKey == Facts[B].CapabilityProfileKey)
        return false;

  for (int32 GroupStart = 0; GroupStart < Facts.Num();)
  {
    int32 GroupEnd = GroupStart + 1;
    while (GroupEnd < Facts.Num()
      && Facts[GroupEnd].MinimumDistanceCm == Facts[GroupStart].MinimumDistanceCm
      && Facts[GroupEnd].MaximumDistanceCm == Facts[GroupStart].MaximumDistanceCm)
    {
      ++GroupEnd;
    }
    const int32 GroupSize = GroupEnd - GroupStart;
    for (int32 Rank = 0; Rank < GroupSize; ++Rank)
    {
      FCrowdDemoCapabilityDemandPhase& Phase = OutPhases.AddDefaulted_GetRef();
      Phase.CapabilityProfileKey = Facts[GroupStart + Rank].CapabilityProfileKey;
      Phase.DemandRegionPhaseOffset = Rank * DemandRegionCount / GroupSize;
    }
    GroupStart = GroupEnd;
  }
  OutPhases.Sort([](const auto& A, const auto& B)
  {
    return A.CapabilityProfileKey < B.CapabilityProfileKey;
  });
  uint32 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, DemandRegionCount);
  Hash = Fold(Hash, OutPhases.Num());
  for (const FCrowdDemoCapabilityDemandPhase& Phase : OutPhases)
  {
    Hash = Fold(Hash, Phase.CapabilityProfileKey);
    Hash = Fold(Hash, Phase.DemandRegionPhaseOffset);
  }
  OutHash = Hash;
  return OutPhases.Num() == Cohorts.Num();
}

bool FCrowdDemoCapabilityProfileKernel::ShareTargetDistanceBand(
  const FCrowdDemoCapabilityProfile& A,
  const FCrowdDemoCapabilityProfile& B)
{
  return A.bValid && B.bValid
    && Quantize(A.NormalizedMinimumCenterDistanceCm, 1.0f)
      == Quantize(B.NormalizedMinimumCenterDistanceCm, 1.0f)
    && Quantize(A.NormalizedMaximumCenterDistanceCm, 1.0f)
      == Quantize(B.NormalizedMaximumCenterDistanceCm, 1.0f);
}
