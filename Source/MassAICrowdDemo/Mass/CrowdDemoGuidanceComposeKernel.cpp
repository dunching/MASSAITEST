#include "Mass/CrowdDemoGuidanceComposeKernel.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime;
    return Hash;
  }

  uint32 FoldSigned(uint32 Hash, const int32 Value)
  {
    return Fold(Hash, static_cast<uint32>(Value));
  }

  uint32 FoldVector(uint32 Hash, const FVector& Value)
  {
    Hash = FoldSigned(Hash, FMath::RoundToInt(Value.X));
    Hash = FoldSigned(Hash, FMath::RoundToInt(Value.Y));
    return FoldSigned(Hash, FMath::RoundToInt(Value.Z));
  }

  int32 Priority(const ECrowdDemoGuidanceProvider Provider)
  {
    switch (Provider)
    {
    case ECrowdDemoGuidanceProvider::BusinessOverride: return 3;
    case ECrowdDemoGuidanceProvider::TargetRegion: return 2;
    case ECrowdDemoGuidanceProvider::SharedFlow: return 1;
    default: return 0;
    }
  }
}

FCrowdDemoGuidanceCandidate FCrowdDemoGuidanceComposeKernel::BuildCandidate(
  const int32 AgentId,
  const ECrowdDemoGuidanceProvider Provider,
  const int32 PlanRevision,
  const FVector& PreferredVelocity,
  const FVector& DesiredLocation,
  const float DesiredYawDegrees,
  const bool bValid)
{
  FCrowdDemoGuidanceCandidate Result;
  Result.AgentId = AgentId;
  Result.Provider = Provider;
  Result.PlanRevision = PlanRevision;
  Result.PreferredVelocity = PreferredVelocity;
  Result.DesiredLocation = DesiredLocation;
  Result.DesiredYawDegrees = DesiredYawDegrees;
  Result.bValid = bValid && AgentId != INDEX_NONE
    && !PreferredVelocity.ContainsNaN() && !DesiredLocation.ContainsNaN()
    && FMath::IsFinite(DesiredYawDegrees);
  uint32 Hash = FnvOffset;
  Hash = FoldSigned(Hash, Result.AgentId);
  Hash = Fold(Hash, static_cast<uint32>(Result.Provider));
  Hash = FoldSigned(Hash, Result.PlanRevision);
  Hash = FoldVector(Hash, Result.PreferredVelocity);
  Hash = FoldVector(Hash, Result.DesiredLocation);
  Hash = FoldSigned(Hash, FMath::RoundToInt(Result.DesiredYawDegrees * 100.0f));
  Hash = Fold(Hash, Result.bValid ? 1u : 0u);
  Result.StableHash = Hash;
  return Result;
}

FCrowdDemoComposedGuidance FCrowdDemoGuidanceComposeKernel::Compose(
  const int32 AgentId,
  const int32 PlanRevision,
  TConstArrayView<FCrowdDemoGuidanceCandidate> Candidates,
  const FVector& StopLocation,
  const float StopYawDegrees)
{
  FCrowdDemoComposedGuidance Result;
  Result.AgentId = AgentId;
  Result.PlanRevision = PlanRevision;
  Result.DesiredLocation = StopLocation;
  Result.DesiredYawDegrees = StopYawDegrees;

  TArray<FCrowdDemoGuidanceCandidate> StableCandidates(Candidates);
  StableCandidates.Sort([](const auto& A, const auto& B)
  {
    if (A.Provider != B.Provider)
      return static_cast<uint8>(A.Provider) < static_cast<uint8>(B.Provider);
    if (A.AgentId != B.AgentId) return A.AgentId < B.AgentId;
    return A.StableHash < B.StableHash;
  });

  uint32 CandidateHash = FnvOffset;
  const FCrowdDemoGuidanceCandidate* Selected = nullptr;
  for (const FCrowdDemoGuidanceCandidate& Candidate : StableCandidates)
  {
    CandidateHash = Fold(CandidateHash, Candidate.StableHash);
    if (!Candidate.bValid || Candidate.AgentId != AgentId
      || Candidate.PlanRevision != PlanRevision)
      continue;
    if (!Selected || Priority(Candidate.Provider) > Priority(Selected->Provider))
      Selected = &Candidate;
  }
  Result.CandidateSetHash = CandidateHash;
  if (Selected)
  {
    Result.SelectedProvider = Selected->Provider;
    Result.AutonomousPreferredVelocity = Selected->PreferredVelocity;
    Result.DesiredLocation = Selected->DesiredLocation;
    Result.DesiredYawDegrees = Selected->DesiredYawDegrees;
  }
  else
  {
    Result.SelectedProvider = ECrowdDemoGuidanceProvider::Stop;
  }
  Result.bValid = AgentId != INDEX_NONE && !StopLocation.ContainsNaN()
    && FMath::IsFinite(StopYawDegrees);
  uint32 Hash = FnvOffset;
  Hash = FoldSigned(Hash, Result.AgentId);
  Hash = FoldSigned(Hash, Result.PlanRevision);
  Hash = Fold(Hash, static_cast<uint32>(Result.SelectedProvider));
  Hash = Fold(Hash, Result.CandidateSetHash);
  Hash = FoldVector(Hash, Result.AutonomousPreferredVelocity);
  Hash = FoldVector(Hash, Result.DesiredLocation);
  Hash = FoldSigned(Hash, FMath::RoundToInt(Result.DesiredYawDegrees * 100.0f));
  Hash = Fold(Hash, Result.bValid ? 1u : 0u);
  Result.StableHash = Hash;
  return Result;
}
