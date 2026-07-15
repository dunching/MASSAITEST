#include "CrowdDemoTargetSlotLayoutKernel.h"

namespace CrowdDemoTargetSlotLayoutPrivate
{
constexpr uint32 FnvOffset = 2166136261u;
constexpr uint32 FnvPrime = 16777619u;

uint32 Fold(uint32 Hash, uint32 Value)
{
  Hash ^= Value;
  Hash *= FnvPrime;
  return Hash;
}

int32 Quantize(float Value, float Quantum)
{
  return FMath::RoundToInt(Value / FMath::Max(Quantum, KINDA_SMALL_NUMBER));
}

float Canonical(float Value, float Quantum)
{
  return static_cast<float>(Quantize(Value, Quantum)) * Quantum;
}

uint32 StableSlotHash(int32 TargetId, ECrowdDemoTargetSlotKind Kind,
  int32 BandId, int32 AngularIndex)
{
  uint32 Hash = Fold(FnvOffset, static_cast<uint32>(TargetId));
  Hash = Fold(Hash, static_cast<uint32>(Kind));
  Hash = Fold(Hash, static_cast<uint32>(BandId));
  return Fold(Hash, static_cast<uint32>(AngularIndex));
}

int32 StableSlotId(int32 TargetId, ECrowdDemoTargetSlotKind Kind,
  int32 BandId, int32 AngularIndex)
{
  return static_cast<int32>(StableSlotHash(TargetId, Kind, BandId, AngularIndex) & 0x7fffffffu);
}

enum class EReject : uint8
{
  None,
  TargetClearance,
  PairSpacing,
  Obstacle,
  Bounds,
  Unreachable,
  IngressSegment
};

struct FCandidate
{
  FCrowdDemoTargetSlotSpec Slot;
  int32 BandPriority = 0;
};

bool SameValidIdentitySet(const FCrowdDemoTargetSlotLayout& A,
  const FCrowdDemoTargetSlotLayout& B)
{
  if (A.TargetId != B.TargetId || A.TargetRevision != B.TargetRevision
    || A.Slots.Num() != B.Slots.Num())
    return false;
  for (int32 Index = 0; Index < A.Slots.Num(); ++Index)
  {
    if (A.Slots[Index].SlotId != B.Slots[Index].SlotId
      || A.Slots[Index].Kind != B.Slots[Index].Kind)
      return false;
  }
  return true;
}
}

void FCrowdDemoTargetSlotLayoutKernel::Build(
  const FCrowdDemoTargetSlotLayoutInput& Input,
  const FCrowdDemoTargetSlotLayout* PreviousLayout,
  FCrowdDemoTargetSlotLayout& OutLayout,
  FCrowdDemoTargetSlotLayoutSummary& OutSummary)
{
  using namespace CrowdDemoTargetSlotLayoutPrivate;
  OutLayout = FCrowdDemoTargetSlotLayout();
  OutSummary = FCrowdDemoTargetSlotLayoutSummary();
  OutLayout.TargetId = Input.Target.TargetId;
  OutLayout.TargetRevision = Input.Target.TargetRevision;

  const float PositionQuantum = Input.Settings.PositionQuantumCm;
  const float AngleQuantum = Input.Settings.AngleQuantumDegrees;
  bool bValid = Input.Target.TargetId != INDEX_NONE
    && Input.Target.TargetRevision != INDEX_NONE
    && Input.Settings.SourceRevision >= 0
    && PositionQuantum > 0.0f && AngleQuantum > 0.0f
    && Input.ParticleProfile.PhysicalRadiusCm >= 0.0f
    && Input.ParticleProfile.HardSafetyGapCm >= 0.0f
    && Input.Settings.TargetHardSafetyGapCm >= 0.0f
    && Input.FlowField != nullptr && Input.FlowField->IsValid();

  TArray<FCrowdDemoTargetSlotBandRule> Bands = Input.Settings.Bands;
  Bands.Sort([](const auto& A, const auto& B)
  {
    if (A.bFunctional != B.bFunctional)
      return A.bFunctional > B.bFunctional;
    if (A.StablePriorityBase != B.StablePriorityBase)
      return A.StablePriorityBase < B.StablePriorityBase;
    return A.BandId < B.BandId;
  });

  TSet<int32> SeenBandIds;
  TArray<FCandidate> Candidates;
  uint32 TopologyHash = Fold(FnvOffset, static_cast<uint32>(Input.Target.TargetId));
  TopologyHash = Fold(TopologyHash, static_cast<uint32>(Input.Target.TargetRevision));
  TopologyHash = Fold(TopologyHash, static_cast<uint32>(Input.Settings.SourceRevision));
  for (const FCrowdDemoTargetSlotBandRule& Band : Bands)
  {
    const ECrowdDemoTargetSlotKind Kind = Band.bFunctional != 0
      ? ECrowdDemoTargetSlotKind::Functional : ECrowdDemoTargetSlotKind::Fill;
    const float CenterRadius = Canonical(Input.Target.PhysicalRadiusCm
      + Band.PreferredSurfaceDistanceCm, PositionQuantum);
    if (Band.BandId == INDEX_NONE || SeenBandIds.Contains(Band.BandId)
      || Band.Capacity < 0 || Band.MinimumCenterDistanceCm < 0.0f
      || Band.MaximumCenterDistanceCm < Band.MinimumCenterDistanceCm)
      bValid = false;
    SeenBandIds.Add(Band.BandId);
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Band.BandId));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Kind));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Band.Capacity));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Quantize(
      Band.PreferredSurfaceDistanceCm, PositionQuantum)));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Quantize(
      Band.MinimumCenterDistanceCm, PositionQuantum)));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Quantize(
      Band.MaximumCenterDistanceCm, PositionQuantum)));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Quantize(
      Band.StartAngleDegrees, AngleQuantum)));
    TopologyHash = Fold(TopologyHash, Band.RequiredCapabilityMask);
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Band.StablePriorityBase));
    if (CenterRadius < Band.MinimumCenterDistanceCm - KINDA_SMALL_NUMBER
      || CenterRadius > Band.MaximumCenterDistanceCm + KINDA_SMALL_NUMBER)
      continue;
    for (int32 AngularIndex = 0; AngularIndex < Band.Capacity; ++AngularIndex)
    {
      const float Angle = Canonical(Band.StartAngleDegrees
        + 360.0f * static_cast<float>(AngularIndex) /
          static_cast<float>(FMath::Max(1, Band.Capacity)), AngleQuantum);
      const float Radians = FMath::DegreesToRadians(Angle);
      FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
      Candidate.BandPriority = Band.StablePriorityBase;
      Candidate.Slot.SlotId = StableSlotId(Input.Target.TargetId, Kind,
        Band.BandId, AngularIndex);
      Candidate.Slot.Kind = Kind;
      Candidate.Slot.BandId = Band.BandId;
      Candidate.Slot.AngularIndex = AngularIndex;
      Candidate.Slot.CenterDistanceCm = CenterRadius;
      Candidate.Slot.TargetRelativeOffset = FVector2f(
        Canonical(FMath::Cos(Radians) * CenterRadius, PositionQuantum),
        Canonical(FMath::Sin(Radians) * CenterRadius, PositionQuantum));
      Candidate.Slot.RequiredCapabilityMask = Band.RequiredCapabilityMask;
      Candidate.Slot.StablePriority = Band.StablePriorityBase + AngularIndex;
    }
  }
  Candidates.Sort([](const FCandidate& A, const FCandidate& B)
  {
    if (A.Slot.Kind != B.Slot.Kind)
      return A.Slot.Kind < B.Slot.Kind;
    if (A.BandPriority != B.BandPriority)
      return A.BandPriority < B.BandPriority;
    if (A.Slot.AngularIndex != B.Slot.AngularIndex)
      return A.Slot.AngularIndex < B.Slot.AngularIndex;
    return A.Slot.SlotId < B.Slot.SlotId;
  });
  OutSummary.GeneratedCandidateCount = Candidates.Num();

  TSet<int32> SeenSlotIds;
  FCrowdDemoSharedFlowFieldConfig ValidationConfig = Input.FlowConfig;
  const float AgentClearance = Input.ParticleProfile.GetNavigationHardClearanceCm();
  ValidationConfig.AgentInflateCm = AgentClearance;
  const float PairHardDistance = 2.0f * Input.ParticleProfile.PhysicalRadiusCm
    + Input.ParticleProfile.HardSafetyGapCm;
  const float TargetHardDistance = Input.Target.PhysicalRadiusCm
    + Input.ParticleProfile.PhysicalRadiusCm
    + FMath::Max(Input.Settings.TargetHardSafetyGapCm,
      Input.ParticleProfile.HardSafetyGapCm);
  uint32 WorldHash = FnvOffset;
  for (const FCandidate& Candidate : Candidates)
  {
    if (SeenSlotIds.Contains(Candidate.Slot.SlotId))
      bValid = false;
    SeenSlotIds.Add(Candidate.Slot.SlotId);
    const FVector2f World2 = FCrowdDemoTargetApproachKernel::TransformTargetRelativePoint(
      Input.Target, Candidate.Slot.TargetRelativeOffset);
    const FVector World(World2.X, World2.Y, Input.FlowConfig.GoalLocation.Z);
    EReject Reject = EReject::None;
    if (FVector2f::Distance(World2, Input.Target.Location) + KINDA_SMALL_NUMBER
      < TargetHardDistance)
      Reject = EReject::TargetClearance;
    else if (World.X < Input.FlowConfig.BoundsMin.X + AgentClearance
      || World.X > Input.FlowConfig.BoundsMax.X - AgentClearance
      || World.Y < Input.FlowConfig.BoundsMin.Y + AgentClearance
      || World.Y > Input.FlowConfig.BoundsMax.Y - AgentClearance)
      Reject = EReject::Bounds;
    else if (FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
      ValidationConfig, World))
      Reject = EReject::Obstacle;
    else if (FCrowdDemoSharedFlowFieldKernel::Sample(*Input.FlowField, World).Status
      != ECrowdDemoFlowLocationStatus::Reachable)
      Reject = EReject::Unreachable;
    else
    {
      const FVector2f Direction = Candidate.Slot.TargetRelativeOffset.GetSafeNormal();
      const FVector2f Handoff2 = Input.Target.Location
        + Direction * FMath::Max(0.0f, Input.TransitionRingRadiusCm);
      const FVector Handoff(Handoff2.X, Handoff2.Y, World.Z);
      if (!FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
        ValidationConfig, Handoff, World))
        Reject = EReject::IngressSegment;
    }
    if (Reject == EReject::None)
    {
      for (const FCrowdDemoTargetSlotSpec& Accepted : OutLayout.Slots)
      {
        const FVector2f AcceptedWorld = FCrowdDemoTargetApproachKernel::TransformTargetRelativePoint(
          Input.Target, Accepted.TargetRelativeOffset);
        if (FVector2f::Distance(World2, AcceptedWorld) + KINDA_SMALL_NUMBER
          < PairHardDistance)
        {
          Reject = EReject::PairSpacing;
          break;
        }
      }
    }
    WorldHash = Fold(WorldHash, static_cast<uint32>(Candidate.Slot.SlotId));
    WorldHash = Fold(WorldHash, static_cast<uint32>(Reject));
    WorldHash = Fold(WorldHash, static_cast<uint32>(Quantize(World.X, PositionQuantum)));
    WorldHash = Fold(WorldHash, static_cast<uint32>(Quantize(World.Y, PositionQuantum)));
    switch (Reject)
    {
    case EReject::None:
      OutLayout.Slots.Add(Candidate.Slot);
      if (Candidate.Slot.Kind == ECrowdDemoTargetSlotKind::Functional)
        ++OutSummary.AcceptedFunctionalCount;
      else
        ++OutSummary.AcceptedFillCount;
      break;
    case EReject::TargetClearance: ++OutSummary.RejectedTargetClearanceCount; break;
    case EReject::PairSpacing: ++OutSummary.RejectedPairSpacingCount; break;
    case EReject::Obstacle: ++OutSummary.RejectedObstacleCount; break;
    case EReject::Bounds: ++OutSummary.RejectedBoundsCount; break;
    case EReject::Unreachable: ++OutSummary.RejectedUnreachableCount; break;
    case EReject::IngressSegment: ++OutSummary.RejectedIngressSegmentCount; break;
    }
  }

  for (const FCrowdDemoTargetSlotSpec& Slot : OutLayout.Slots)
  {
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Slot.SlotId));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Slot.Kind));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Slot.BandId));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Slot.AngularIndex));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Quantize(
      Slot.TargetRelativeOffset.X, PositionQuantum)));
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Quantize(
      Slot.TargetRelativeOffset.Y, PositionQuantum)));
    TopologyHash = Fold(TopologyHash, Slot.RequiredCapabilityMask);
    TopologyHash = Fold(TopologyHash, static_cast<uint32>(Slot.StablePriority));
  }
  WorldHash = Fold(WorldHash, static_cast<uint32>(Quantize(
    Input.Target.Location.X, PositionQuantum)));
  WorldHash = Fold(WorldHash, static_cast<uint32>(Quantize(
    Input.Target.Location.Y, PositionQuantum)));
  WorldHash = Fold(WorldHash, static_cast<uint32>(Quantize(
    Input.Target.YawDegrees, AngleQuantum)));
  WorldHash = Fold(WorldHash, Input.FlowField ? Input.FlowField->BuildHash : 0u);
  uint32 FullHash = Fold(TopologyHash, WorldHash);
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(
    Input.Target.PhysicalRadiusCm, PositionQuantum)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(
    Input.ParticleProfile.PhysicalRadiusCm, PositionQuantum)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(
    Input.ParticleProfile.HardSafetyGapCm, PositionQuantum)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(
    Input.ParticleProfile.SoftMarginCm, PositionQuantum)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(
    Input.ParticleProfile.Mobility, 0.0001f)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(
    Input.Settings.TargetHardSafetyGapCm, PositionQuantum)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(PositionQuantum, 0.0001f)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(AngleQuantum, 0.0001f)));
  FullHash = Fold(FullHash, static_cast<uint32>(Quantize(
    Input.TransitionRingRadiusCm, PositionQuantum)));
  FullHash = Fold(FullHash, static_cast<uint32>(Input.FlowConfig.Revision));

  OutLayout.TopologyHash = TopologyHash;
  OutLayout.WorldValidationHash = WorldHash;
  OutLayout.FullInputHash = FullHash;
  OutLayout.bValid = bValid;
  if (PreviousLayout != nullptr && PreviousLayout->bValid
    && SameValidIdentitySet(*PreviousLayout, OutLayout))
    OutLayout.SlotLayoutRevision = PreviousLayout->SlotLayoutRevision;
  else
    OutLayout.SlotLayoutRevision = PreviousLayout && PreviousLayout->SlotLayoutRevision >= 0
      ? PreviousLayout->SlotLayoutRevision + 1 : 1;
  OutSummary.TopologyHash = TopologyHash;
  OutSummary.WorldValidationHash = WorldHash;
  OutSummary.FullInputHash = FullHash;
  OutSummary.bValid = bValid;
}
