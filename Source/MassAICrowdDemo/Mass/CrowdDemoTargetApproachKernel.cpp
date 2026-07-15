#include "CrowdDemoTargetApproachKernel.h"

namespace CrowdDemoTargetApproachPrivate
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

FVector2f QuantizeVector(const FVector2f& Value, float Quantum)
{
  const float SafeQuantum = FMath::Max(Quantum, KINDA_SMALL_NUMBER);
  return FVector2f(
    static_cast<float>(FMath::RoundToInt(Value.X / SafeQuantum)) * SafeQuantum,
    static_cast<float>(FMath::RoundToInt(Value.Y / SafeQuantum)) * SafeQuantum);
}

FVector2f ClampMagnitude(const FVector2f& Value, float Maximum)
{
  const float SizeSquared = Value.SizeSquared();
  if (Maximum <= 0.0f || SizeSquared <= FMath::Square(Maximum))
    return Value;
  return Value.GetSafeNormal() * Maximum;
}

bool IsCompatible(const FCrowdDemoTargetApproachAgent& Agent,
  const FCrowdDemoTargetSlotSpec& Slot)
{
  if (Slot.RequiredCapabilityMask != 0
    && (Agent.CapabilityMask & Slot.RequiredCapabilityMask) != Slot.RequiredCapabilityMask)
    return false;
  return Slot.Kind != ECrowdDemoTargetSlotKind::Functional
    || (Slot.CenterDistanceCm + KINDA_SMALL_NUMBER >= Agent.MinimumFunctionalDistanceCm
      && Slot.CenterDistanceCm <= Agent.MaximumFunctionalDistanceCm + KINDA_SMALL_NUMBER);
}

bool IsSlotState(ECrowdDemoTargetApproachState State)
{
  return State == ECrowdDemoTargetApproachState::SlotIngress
    || State == ECrowdDemoTargetApproachState::SlotOccupied;
}

struct FEdge
{
  int32 SlotIndex = INDEX_NONE;
  int32 QuantizedDistance = MAX_int32;
  int32 StablePriority = MAX_int32;
  int32 SlotId = INDEX_NONE;
};

bool TryAugment(int32 AgentIndex,
  const TArray<TArray<FEdge>>& Edges,
  TArray<int32>& SlotOwner,
  TArray<bool>& VisitedSlots)
{
  for (const FEdge& Edge : Edges[AgentIndex])
  {
    if (VisitedSlots[Edge.SlotIndex])
      continue;
    VisitedSlots[Edge.SlotIndex] = true;
    const int32 ExistingAgent = SlotOwner[Edge.SlotIndex];
    if (ExistingAgent == INDEX_NONE
      || TryAugment(ExistingAgent, Edges, SlotOwner, VisitedSlots))
    {
      SlotOwner[Edge.SlotIndex] = AgentIndex;
      return true;
    }
  }
  return false;
}

void MatchSlotKind(ECrowdDemoTargetSlotKind Kind,
  const FCrowdDemoTargetFact& Target,
  const FCrowdDemoTargetApproachSettings& Settings,
  const TArray<FCrowdDemoTargetApproachAgent>& Agents,
  const TArray<FCrowdDemoTargetSlotSpec>& Slots,
  const TArray<int32>& CandidateAgents,
  const TSet<int32>& ReservedSlotIds,
  TMap<int32, int32>& InOutAgentToSlot)
{
  TArray<int32> AvailableSlots;
  for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    if (Slots[SlotIndex].Kind == Kind && !ReservedSlotIds.Contains(Slots[SlotIndex].SlotId))
      AvailableSlots.Add(SlotIndex);
  }
  if (AvailableSlots.IsEmpty() || CandidateAgents.IsEmpty())
    return;

  TArray<TArray<FEdge>> Edges;
  Edges.SetNum(CandidateAgents.Num());
  for (int32 LocalAgentIndex = 0; LocalAgentIndex < CandidateAgents.Num(); ++LocalAgentIndex)
  {
    const FCrowdDemoTargetApproachAgent& Agent = Agents[CandidateAgents[LocalAgentIndex]];
    for (int32 LocalSlotIndex = 0; LocalSlotIndex < AvailableSlots.Num(); ++LocalSlotIndex)
    {
      const FCrowdDemoTargetSlotSpec& Slot = Slots[AvailableSlots[LocalSlotIndex]];
      if (!IsCompatible(Agent, Slot))
        continue;
      const FVector2f SlotLocation = FCrowdDemoTargetApproachKernel::TransformTargetRelativePoint(
        Target, Slot.TargetRelativeOffset);
      FEdge& Edge = Edges[LocalAgentIndex].AddDefaulted_GetRef();
      Edge.SlotIndex = LocalSlotIndex;
      Edge.QuantizedDistance = Quantize(FVector2f::Distance(Agent.Location, SlotLocation),
        Settings.PositionQuantumCm);
      Edge.StablePriority = Slot.StablePriority;
      Edge.SlotId = Slot.SlotId;
    }
    Edges[LocalAgentIndex].Sort([](const FEdge& A, const FEdge& B)
    {
      if (A.QuantizedDistance != B.QuantizedDistance)
        return A.QuantizedDistance < B.QuantizedDistance;
      if (A.StablePriority != B.StablePriority)
        return A.StablePriority < B.StablePriority;
      return A.SlotId < B.SlotId;
    });
  }

  TArray<int32> SlotOwner;
  SlotOwner.Init(INDEX_NONE, AvailableSlots.Num());
  for (int32 AgentIndex = 0; AgentIndex < CandidateAgents.Num(); ++AgentIndex)
  {
    TArray<bool> VisitedSlots;
    VisitedSlots.Init(false, AvailableSlots.Num());
    TryAugment(AgentIndex, Edges, SlotOwner, VisitedSlots);
  }
  for (int32 LocalSlotIndex = 0; LocalSlotIndex < SlotOwner.Num(); ++LocalSlotIndex)
  {
    if (SlotOwner[LocalSlotIndex] == INDEX_NONE)
      continue;
    const int32 AgentArrayIndex = CandidateAgents[SlotOwner[LocalSlotIndex]];
    InOutAgentToSlot.Add(Agents[AgentArrayIndex].AgentId,
      Slots[AvailableSlots[LocalSlotIndex]].SlotId);
  }
}

const FCrowdDemoTargetSlotSpec* FindSlot(const TArray<FCrowdDemoTargetSlotSpec>& Slots, int32 SlotId)
{
  return Slots.FindByPredicate([SlotId](const FCrowdDemoTargetSlotSpec& Slot)
  {
    return Slot.SlotId == SlotId;
  });
}
}

FVector2f FCrowdDemoTargetApproachKernel::StableDirectionFromAgentId(int32 AgentId)
{
  uint32 Hash = CrowdDemoTargetApproachPrivate::Fold(
    CrowdDemoTargetApproachPrivate::FnvOffset, static_cast<uint32>(AgentId));
  Hash = CrowdDemoTargetApproachPrivate::Fold(Hash, 0x9e3779b9u);
  const float Angle = static_cast<float>(Hash % 65536u) * (2.0f * PI / 65536.0f);
  return FVector2f(FMath::Cos(Angle), FMath::Sin(Angle));
}

FVector2f FCrowdDemoTargetApproachKernel::FindNearestTransitionRingPoint(
  const FCrowdDemoTargetFact& Target,
  const FVector2f& AgentLocation,
  int32 AgentId,
  float TransitionRingRadiusCm)
{
  const FVector2f Delta = AgentLocation - Target.Location;
  const FVector2f Direction = Delta.SizeSquared() > KINDA_SMALL_NUMBER
    ? Delta.GetSafeNormal()
    : StableDirectionFromAgentId(AgentId);
  return Target.Location + Direction * FMath::Max(TransitionRingRadiusCm, 0.0f);
}

FVector2f FCrowdDemoTargetApproachKernel::TransformTargetRelativePoint(
  const FCrowdDemoTargetFact& Target,
  const FVector2f& TargetRelativePoint)
{
  const float Radians = FMath::DegreesToRadians(Target.YawDegrees);
  const float Cos = FMath::Cos(Radians);
  const float Sin = FMath::Sin(Radians);
  return Target.Location + FVector2f(
    TargetRelativePoint.X * Cos - TargetRelativePoint.Y * Sin,
    TargetRelativePoint.X * Sin + TargetRelativePoint.Y * Cos);
}

FCrowdDemoTargetFact FCrowdDemoTargetApproachKernel::BuildLinearMotionFact(
  int32 TargetId,
  int32 TargetRevision,
  int32 MotionStep,
  const FVector2f& InitialLocation,
  const FVector2f& LinearVelocity,
  float InitialYawDegrees,
  float YawRateDegreesPerSecond,
  float PhysicalRadiusCm,
  float FixedStepSeconds,
  float PositionQuantumCm,
  float VelocityQuantumCmps)
{
  using namespace CrowdDemoTargetApproachPrivate;
  FCrowdDemoTargetFact Target;
  Target.TargetId = TargetId;
  Target.TargetRevision = TargetRevision;
  Target.MotionStep = MotionStep;
  Target.PhysicalRadiusCm = PhysicalRadiusCm;
  const float SafeStepSeconds = FMath::Max(FixedStepSeconds, KINDA_SMALL_NUMBER);
  const float CurrentSeconds = static_cast<float>(MotionStep) * SafeStepSeconds;
  const float PreviousSeconds = static_cast<float>(MotionStep - 1) * SafeStepSeconds;
  Target.Location = QuantizeVector(InitialLocation + LinearVelocity * CurrentSeconds,
    PositionQuantumCm);
  const FVector2f PreviousLocation = QuantizeVector(
    InitialLocation + LinearVelocity * PreviousSeconds, PositionQuantumCm);
  Target.Velocity = QuantizeVector((Target.Location - PreviousLocation) / SafeStepSeconds,
    VelocityQuantumCmps);
  Target.YawDegrees = InitialYawDegrees + YawRateDegreesPerSecond * CurrentSeconds;
  return Target;
}

void FCrowdDemoTargetApproachKernel::ValidateAtomicCommit(
  const int32 SlotLayoutRevision,
  TConstArrayView<FCrowdDemoTargetSlotSpec> SlotView,
  TConstArrayView<FCrowdDemoTargetApproachCommitAgent> AgentView,
  TConstArrayView<FCrowdDemoTargetApproachResult> DecisionView,
  FCrowdDemoTargetApproachCommitValidation& OutValidation)
{
  using namespace CrowdDemoTargetApproachPrivate;
  OutValidation = FCrowdDemoTargetApproachCommitValidation();
  TArray<FCrowdDemoTargetSlotSpec> Slots(SlotView);
  TArray<FCrowdDemoTargetApproachCommitAgent> Agents(AgentView);
  TArray<FCrowdDemoTargetApproachResult> Decisions(DecisionView);
  Slots.Sort([](const auto& A, const auto& B) { return A.SlotId < B.SlotId; });
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  Decisions.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  bool bValid = SlotLayoutRevision >= 0 && Agents.Num() == Decisions.Num();
  TSet<int32> OwnerSlotIds;
  uint32 Hash = Fold(FnvOffset, static_cast<uint32>(SlotLayoutRevision));
  for (int32 Index = 0; Index < Decisions.Num(); ++Index)
  {
    const FCrowdDemoTargetApproachResult& Decision = Decisions[Index];
    if (!Agents.IsValidIndex(Index) || Agents[Index].AgentId != Decision.AgentId)
      bValid = false;
    if (Decision.SlotLayoutRevision != SlotLayoutRevision)
    {
      ++OutValidation.RevisionMismatchCount;
      bValid = false;
    }
    const bool bSlotState = IsSlotState(Decision.State);
    if (bSlotState != (Decision.AssignedSlotId != INDEX_NONE))
      bValid = false;
    if (Decision.AssignedSlotId != INDEX_NONE)
    {
      const FCrowdDemoTargetSlotSpec* Slot = FindSlot(Slots, Decision.AssignedSlotId);
      if (Slot == nullptr || OwnerSlotIds.Contains(Decision.AssignedSlotId))
      {
        ++OutValidation.OwnerConflictCount;
        bValid = false;
      }
      else if (Agents.IsValidIndex(Index))
      {
        FCrowdDemoTargetApproachAgent CompatibilityAgent;
        CompatibilityAgent.CapabilityMask = Agents[Index].CapabilityMask;
        CompatibilityAgent.MinimumFunctionalDistanceCm =
          Agents[Index].MinimumFunctionalDistanceCm;
        CompatibilityAgent.MaximumFunctionalDistanceCm =
          Agents[Index].MaximumFunctionalDistanceCm;
        if (!IsCompatible(CompatibilityAgent, *Slot))
          bValid = false;
      }
      OwnerSlotIds.Add(Decision.AssignedSlotId);
    }
    Hash = Fold(Hash, static_cast<uint32>(Decision.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Decision.State));
    Hash = Fold(Hash, static_cast<uint32>(Decision.AssignedSlotId));
    Hash = Fold(Hash, static_cast<uint32>(Decision.RingEnterFixedStep));
    Hash = Fold(Hash, static_cast<uint32>(Decision.StateEnterFixedStep));
  }
  OutValidation.bValid = bValid;
  OutValidation.CommitHash = Hash;
}

void FCrowdDemoTargetApproachKernel::Solve(
  const FCrowdDemoTargetFact& TargetInput,
  const FCrowdDemoTargetApproachSettings& SettingsInput,
  TConstArrayView<FCrowdDemoTargetSlotSpec> SlotView,
  TConstArrayView<FCrowdDemoTargetApproachAgent> AgentView,
  int32 FixedStepIndex,
  TArray<FCrowdDemoTargetApproachResult>& OutResults,
  FCrowdDemoTargetApproachSummary& OutSummary,
  int32 SlotLayoutRevision)
{
  using namespace CrowdDemoTargetApproachPrivate;
  OutResults.Reset();
  OutSummary = FCrowdDemoTargetApproachSummary();

  FCrowdDemoTargetApproachSettings Settings = SettingsInput;
  FCrowdDemoTargetFact Target = TargetInput;
  TArray<FCrowdDemoTargetSlotSpec> Slots(SlotView);
  TArray<FCrowdDemoTargetApproachAgent> Agents(AgentView);
  // The advertised deterministic input contract is PositionQuantum/VelocityQuantum.
  // Canonicalize the working copy before any branch, comparison, matching, or guidance math;
  // otherwise sub-quantum server/client float tails can quantize the final velocity differently.
  if (Settings.bEnabled)
  {
    Settings.PositionQuantumCm = static_cast<float>(
      Quantize(Settings.PositionQuantumCm, 0.0001f)) * 0.0001f;
    Settings.VelocityQuantumCmps = static_cast<float>(
      Quantize(Settings.VelocityQuantumCmps, 0.0001f)) * 0.0001f;
    const auto QuantizePositionSetting = [&Settings](float Value)
    {
      return static_cast<float>(Quantize(Value, Settings.PositionQuantumCm))
        * Settings.PositionQuantumCm;
    };
    const auto QuantizeVelocitySetting = [&Settings](float Value)
    {
      return static_cast<float>(Quantize(Value, Settings.VelocityQuantumCmps))
        * Settings.VelocityQuantumCmps;
    };
    const auto QuantizeGain = [](float Value)
    {
      return static_cast<float>(Quantize(Value, 0.0001f)) * 0.0001f;
    };
    Settings.TransitionRingRadiusCm = QuantizePositionSetting(Settings.TransitionRingRadiusCm);
    Settings.RingEnterToleranceCm = QuantizePositionSetting(Settings.RingEnterToleranceCm);
    Settings.RingExitToleranceCm = QuantizePositionSetting(Settings.RingExitToleranceCm);
    Settings.ApproachSlowdownDistanceCm = QuantizePositionSetting(Settings.ApproachSlowdownDistanceCm);
    Settings.SlotArrivalToleranceCm = QuantizePositionSetting(Settings.SlotArrivalToleranceCm);
    Settings.SlotArrivalSpeedToleranceCmps = QuantizeVelocitySetting(
      Settings.SlotArrivalSpeedToleranceCmps);
    Settings.SlotExitToleranceCm = QuantizePositionSetting(Settings.SlotExitToleranceCm);
    Settings.SlotArriveGainPerSecond = QuantizeGain(Settings.SlotArriveGainPerSecond);
    Settings.SlotOccupiedGainPerSecond = QuantizeGain(Settings.SlotOccupiedGainPerSecond);
    Settings.FreeSettleAttractionGainPerSecond = QuantizeGain(
      Settings.FreeSettleAttractionGainPerSecond);
    Settings.FreeSettleMaxSpeedCmps = QuantizeVelocitySetting(Settings.FreeSettleMaxSpeedCmps);
    Settings.TargetPhysicalRadiusCm = QuantizePositionSetting(Settings.TargetPhysicalRadiusCm);
    Settings.TargetHardSafetyGapCm = QuantizePositionSetting(Settings.TargetHardSafetyGapCm);
    Settings.TargetSoftMarginCm = QuantizePositionSetting(Settings.TargetSoftMarginCm);

    Target.Location = QuantizeVector(Target.Location, Settings.PositionQuantumCm);
    Target.Velocity = QuantizeVector(Target.Velocity, Settings.VelocityQuantumCmps);
    Target.YawDegrees = static_cast<float>(Quantize(Target.YawDegrees, 0.01f)) * 0.01f;
    Target.PhysicalRadiusCm = static_cast<float>(
      Quantize(Target.PhysicalRadiusCm, Settings.PositionQuantumCm)) * Settings.PositionQuantumCm;
    for (FCrowdDemoTargetApproachAgent& Agent : Agents)
    {
      Agent.Location = QuantizeVector(Agent.Location, Settings.PositionQuantumCm);
      Agent.Velocity = QuantizeVector(Agent.Velocity, Settings.VelocityQuantumCmps);
      Agent.PhysicalRadiusCm = QuantizePositionSetting(Agent.PhysicalRadiusCm);
      Agent.MaxSpeedCmps = QuantizeVelocitySetting(Agent.MaxSpeedCmps);
    }
    for (FCrowdDemoTargetSlotSpec& Slot : Slots)
    {
      Slot.TargetRelativeOffset = QuantizeVector(
        Slot.TargetRelativeOffset, Settings.PositionQuantumCm);
    }
  }
  Slots.Sort([](const auto& A, const auto& B) { return A.SlotId < B.SlotId; });
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });

  TSet<int32> SeenAgentIds;
  TSet<int32> SeenSlotIds;
  bool bInputsValid = Target.TargetId != INDEX_NONE && Target.TargetRevision != INDEX_NONE
    && Settings.TransitionRingRadiusCm >= 0.0f && Settings.PositionQuantumCm > 0.0f
    && Settings.VelocityQuantumCmps > 0.0f;
  for (const FCrowdDemoTargetApproachAgent& Agent : Agents)
  {
    if (Agent.AgentId == INDEX_NONE || SeenAgentIds.Contains(Agent.AgentId))
      bInputsValid = false;
    SeenAgentIds.Add(Agent.AgentId);
  }
  for (const FCrowdDemoTargetSlotSpec& Slot : Slots)
  {
    if (Slot.SlotId == INDEX_NONE || SeenSlotIds.Contains(Slot.SlotId))
      bInputsValid = false;
    SeenSlotIds.Add(Slot.SlotId);
    if (Slot.Kind == ECrowdDemoTargetSlotKind::Functional)
      ++OutSummary.FunctionalSlotCapacity;
    else
      ++OutSummary.FillSlotCapacity;
  }
  if (!bInputsValid)
    return;

  uint32 TargetHash = FnvOffset;
  TargetHash = Fold(TargetHash, static_cast<uint32>(Target.TargetId));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Target.TargetRevision));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Target.MotionStep));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Quantize(Target.Location.X, Settings.PositionQuantumCm)));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Quantize(Target.Location.Y, Settings.PositionQuantumCm)));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Quantize(Target.Velocity.X, Settings.VelocityQuantumCmps)));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Quantize(Target.Velocity.Y, Settings.VelocityQuantumCmps)));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Quantize(Target.YawDegrees, 0.01f)));
  TargetHash = Fold(TargetHash, static_cast<uint32>(Quantize(Target.PhysicalRadiusCm, Settings.PositionQuantumCm)));
  OutSummary.TargetFactHash = TargetHash;

  if (!Settings.bEnabled)
  {
    for (const FCrowdDemoTargetApproachAgent& Agent : Agents)
    {
      FCrowdDemoTargetApproachResult& Result = OutResults.AddDefaulted_GetRef();
      Result.AgentId = Agent.AgentId;
      Result.State = Agent.ExistingState;
      Result.AssignedSlotId = Agent.ExistingSlotId;
      Result.SlotLayoutRevision = Agent.ExistingSlotLayoutRevision;
      Result.RingEnterFixedStep = Agent.RingEnterFixedStep;
      Result.StateEnterFixedStep = Agent.StateEnterFixedStep;
      Result.DesiredLocation = Agent.Location;
      Result.DesiredVelocity = Agent.Velocity;
    }
    OutSummary.bValid = true;
  }
  else
  {
    TMap<int32, int32> AgentToSlot;
    TSet<int32> ReservedSlotIds;
    for (const FCrowdDemoTargetApproachAgent& Agent : Agents)
    {
      if (!IsSlotState(Agent.ExistingState) || Agent.ExistingSlotId == INDEX_NONE
        || Agent.ExistingTargetRevision != Target.TargetRevision)
        continue;
      const FCrowdDemoTargetSlotSpec* Slot = FindSlot(Slots, Agent.ExistingSlotId);
      if (Slot == nullptr || !IsCompatible(Agent, *Slot))
      {
        ++OutSummary.InvalidSlotOwnerCount;
        continue;
      }
      if (ReservedSlotIds.Contains(Slot->SlotId))
      {
        ++OutSummary.DuplicateSlotOwnerCount;
        continue;
      }
      ReservedSlotIds.Add(Slot->SlotId);
      AgentToSlot.Add(Agent.AgentId, Slot->SlotId);
    }
    if (OutSummary.DuplicateSlotOwnerCount > 0)
      return;

    TArray<int32> CandidateAgents;
    for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
    {
      const FCrowdDemoTargetApproachAgent& Agent = Agents[AgentIndex];
      if (AgentToSlot.Contains(Agent.AgentId))
        continue;
      const float Radius = FVector2f::Distance(Agent.Location, Target.Location);
      const bool bPreviouslyEntered = Agent.ExistingState != ECrowdDemoTargetApproachState::Approach;
      if (bPreviouslyEntered || Radius <= Settings.TransitionRingRadiusCm + Settings.RingEnterToleranceCm)
        CandidateAgents.Add(AgentIndex);
    }
    CandidateAgents.Sort([&Agents](int32 AIndex, int32 BIndex)
    {
      const auto& A = Agents[AIndex];
      const auto& B = Agents[BIndex];
      if (A.StableBusinessPriority != B.StableBusinessPriority)
        return A.StableBusinessPriority < B.StableBusinessPriority;
      const int32 AStep = A.RingEnterFixedStep == INDEX_NONE ? MAX_int32 : A.RingEnterFixedStep;
      const int32 BStep = B.RingEnterFixedStep == INDEX_NONE ? MAX_int32 : B.RingEnterFixedStep;
      if (AStep != BStep)
        return AStep < BStep;
      return A.AgentId < B.AgentId;
    });

    MatchSlotKind(ECrowdDemoTargetSlotKind::Functional, Target, Settings, Agents, Slots,
      CandidateAgents, ReservedSlotIds, AgentToSlot);
    for (const TPair<int32, int32>& Assignment : AgentToSlot)
      ReservedSlotIds.Add(Assignment.Value);
    TArray<int32> FillCandidates = CandidateAgents.FilterByPredicate(
      [&Agents, &AgentToSlot](int32 AgentIndex)
      {
        return !AgentToSlot.Contains(Agents[AgentIndex].AgentId);
      });
    MatchSlotKind(ECrowdDemoTargetSlotKind::Fill, Target, Settings, Agents, Slots,
      FillCandidates, ReservedSlotIds, AgentToSlot);

    for (const FCrowdDemoTargetApproachAgent& Agent : Agents)
    {
      FCrowdDemoTargetApproachResult& Result = OutResults.AddDefaulted_GetRef();
      Result.AgentId = Agent.AgentId;
      Result.SlotLayoutRevision = SlotLayoutRevision;
      Result.RingEnterFixedStep = Agent.RingEnterFixedStep;
      Result.StateEnterFixedStep = Agent.StateEnterFixedStep;
      const float Radius = FVector2f::Distance(Agent.Location, Target.Location);
      const bool bInsideRing = Radius <= Settings.TransitionRingRadiusCm + Settings.RingEnterToleranceCm;
      const bool bPreviouslyEntered = Agent.ExistingState != ECrowdDemoTargetApproachState::Approach;
      Result.bEnteredRing = !bPreviouslyEntered && bInsideRing;
      if (Result.bEnteredRing)
        Result.RingEnterFixedStep = FixedStepIndex;

      const int32* AssignedSlotId = AgentToSlot.Find(Agent.AgentId);
      if (AssignedSlotId != nullptr)
      {
        Result.AssignedSlotId = *AssignedSlotId;
        const FCrowdDemoTargetSlotSpec* Slot = FindSlot(Slots, *AssignedSlotId);
        check(Slot != nullptr);
        Result.DesiredLocation = TransformTargetRelativePoint(Target, Slot->TargetRelativeOffset);
        const float Distance = FVector2f::Distance(Agent.Location, Result.DesiredLocation);
        const float RelativeSpeed = (Agent.Velocity - Target.Velocity).Size();
        const bool bWasOccupied = Agent.ExistingState == ECrowdDemoTargetApproachState::SlotOccupied
          && Agent.ExistingSlotId == *AssignedSlotId
          && Agent.ExistingTargetRevision == Target.TargetRevision;
        if (bWasOccupied && Distance <= Settings.SlotExitToleranceCm)
          Result.State = ECrowdDemoTargetApproachState::SlotOccupied;
        else if (Distance <= Settings.SlotArrivalToleranceCm
          && RelativeSpeed <= Settings.SlotArrivalSpeedToleranceCmps)
          Result.State = ECrowdDemoTargetApproachState::SlotOccupied;
        else
          Result.State = ECrowdDemoTargetApproachState::SlotIngress;
        const float Gain = Result.State == ECrowdDemoTargetApproachState::SlotOccupied
          ? Settings.SlotOccupiedGainPerSecond : Settings.SlotArriveGainPerSecond;
        Result.DesiredVelocity = ClampMagnitude(Target.Velocity
          + (Result.DesiredLocation - Agent.Location) * Gain, Agent.MaxSpeedCmps);
        Result.bSettled = Result.State == ECrowdDemoTargetApproachState::SlotOccupied;
      }
      else if (bPreviouslyEntered || bInsideRing)
      {
        Result.State = ECrowdDemoTargetApproachState::FreeSettle;
        Result.AssignedSlotId = INDEX_NONE;
        const FVector2f Direction = (Agent.Location - Target.Location).SizeSquared() > KINDA_SMALL_NUMBER
          ? (Agent.Location - Target.Location).GetSafeNormal()
          : StableDirectionFromAgentId(Agent.AgentId);
        const float StandOff = FMath::Max(Target.PhysicalRadiusCm, Settings.TargetPhysicalRadiusCm)
          + Settings.TargetHardSafetyGapCm + Agent.PhysicalRadiusCm;
        Result.DesiredLocation = Target.Location + Direction * StandOff;
        const float Maximum = FMath::Min(Agent.MaxSpeedCmps, Settings.FreeSettleMaxSpeedCmps);
        Result.DesiredVelocity = ClampMagnitude(Target.Velocity
          + (Result.DesiredLocation - Agent.Location)
            * Settings.FreeSettleAttractionGainPerSecond, Maximum);
        Result.bSettled = FVector2f::Distance(Agent.Location, Result.DesiredLocation)
            <= Settings.SlotArrivalToleranceCm
          && (Agent.Velocity - Target.Velocity).Size()
            <= Settings.SlotArrivalSpeedToleranceCmps;
      }
      else
      {
        Result.State = ECrowdDemoTargetApproachState::Approach;
        Result.AssignedSlotId = INDEX_NONE;
        Result.DesiredLocation = FindNearestTransitionRingPoint(Target, Agent.Location, Agent.AgentId,
          Settings.TransitionRingRadiusCm);
        const float Distance = FVector2f::Distance(Agent.Location, Result.DesiredLocation);
        const float SpeedScale = FMath::Clamp(Distance /
          FMath::Max(Settings.ApproachSlowdownDistanceCm, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
        const FVector2f RelativeCorrection = ClampMagnitude(
          Result.DesiredLocation - Agent.Location, Agent.MaxSpeedCmps * SpeedScale);
        Result.DesiredVelocity = ClampMagnitude(Target.Velocity + RelativeCorrection,
          Agent.MaxSpeedCmps);
      }

      Result.DesiredLocation = QuantizeVector(Result.DesiredLocation, Settings.PositionQuantumCm);
      Result.DesiredVelocity = QuantizeVector(Result.DesiredVelocity, Settings.VelocityQuantumCmps);
      Result.bStateChanged = Result.State != Agent.ExistingState
        || Result.AssignedSlotId != Agent.ExistingSlotId;
      if (Result.bStateChanged)
      {
        Result.StateEnterFixedStep = FixedStepIndex;
        ++OutSummary.StateTransitionCount;
      }
      if (Result.State == ECrowdDemoTargetApproachState::Approach)
        ++OutSummary.ApproachAgentCount;
      else
        ++OutSummary.RingEnteredCount;
      if (Result.State == ECrowdDemoTargetApproachState::SlotIngress)
        ++OutSummary.SlotIngressCount;
      else if (Result.State == ECrowdDemoTargetApproachState::SlotOccupied)
        ++OutSummary.SlotOccupiedCount;
      else if (Result.State == ECrowdDemoTargetApproachState::FreeSettle)
      {
        ++OutSummary.FreeSettleCount;
        OutSummary.FreeSettledCount += Result.bSettled ? 1 : 0;
      }
      if (Result.AssignedSlotId != INDEX_NONE)
      {
        const FCrowdDemoTargetSlotSpec* Slot = FindSlot(Slots, Result.AssignedSlotId);
        if (Slot->Kind == ECrowdDemoTargetSlotKind::Functional)
          ++OutSummary.FunctionalSlotOccupied;
        else
          ++OutSummary.FillSlotOccupied;
      }
    }
    OutSummary.bValid = OutResults.Num() == Agents.Num()
      && OutSummary.DuplicateSlotOwnerCount == 0;
  }

  uint32 AgentInputHash = FnvOffset;
  uint32 AgentFineKinematicHash = FnvOffset;
  uint32 AgentConfigHash = FnvOffset;
  uint32 AgentTemporalHash = FnvOffset;
  for (const FCrowdDemoTargetApproachAgent& Agent : Agents)
  {
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Agent.AgentId));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Quantize(Agent.Location.X, Settings.PositionQuantumCm)));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Quantize(Agent.Location.Y, Settings.PositionQuantumCm)));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Quantize(Agent.Velocity.X, Settings.VelocityQuantumCmps)));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Quantize(Agent.Velocity.Y, Settings.VelocityQuantumCmps)));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Agent.ExistingState));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Agent.ExistingSlotId));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Agent.ExistingTargetRevision));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Agent.ExistingSlotLayoutRevision));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Agent.RingEnterFixedStep));
    AgentInputHash = Fold(AgentInputHash, static_cast<uint32>(Agent.StateEnterFixedStep));

    AgentFineKinematicHash = Fold(AgentFineKinematicHash, static_cast<uint32>(Agent.AgentId));
    AgentFineKinematicHash = Fold(AgentFineKinematicHash,
      static_cast<uint32>(Quantize(Agent.Location.X, 0.01f)));
    AgentFineKinematicHash = Fold(AgentFineKinematicHash,
      static_cast<uint32>(Quantize(Agent.Location.Y, 0.01f)));
    AgentFineKinematicHash = Fold(AgentFineKinematicHash,
      static_cast<uint32>(Quantize(Agent.Velocity.X, 0.01f)));
    AgentFineKinematicHash = Fold(AgentFineKinematicHash,
      static_cast<uint32>(Quantize(Agent.Velocity.Y, 0.01f)));

    AgentConfigHash = Fold(AgentConfigHash, static_cast<uint32>(Agent.AgentId));
    AgentConfigHash = Fold(AgentConfigHash,
      static_cast<uint32>(Quantize(Agent.PhysicalRadiusCm, 0.01f)));
    AgentConfigHash = Fold(AgentConfigHash,
      static_cast<uint32>(Quantize(Agent.MaxSpeedCmps, 0.01f)));
    AgentConfigHash = Fold(AgentConfigHash, Agent.CapabilityMask);
    AgentConfigHash = Fold(AgentConfigHash,
      static_cast<uint32>(Quantize(Agent.MinimumFunctionalDistanceCm, 0.01f)));
    AgentConfigHash = Fold(AgentConfigHash,
      static_cast<uint32>(Quantize(Agent.MaximumFunctionalDistanceCm, 0.01f)));
    AgentConfigHash = Fold(AgentConfigHash, static_cast<uint32>(Agent.StableBusinessPriority));

    AgentTemporalHash = Fold(AgentTemporalHash, static_cast<uint32>(Agent.AgentId));
    AgentTemporalHash = Fold(AgentTemporalHash, static_cast<uint32>(Agent.ExistingState));
    AgentTemporalHash = Fold(AgentTemporalHash, static_cast<uint32>(Agent.ExistingSlotId));
    AgentTemporalHash = Fold(AgentTemporalHash, static_cast<uint32>(Agent.ExistingTargetRevision));
    AgentTemporalHash = Fold(AgentTemporalHash,
      static_cast<uint32>(Agent.ExistingSlotLayoutRevision));
    AgentTemporalHash = Fold(AgentTemporalHash, static_cast<uint32>(Agent.RingEnterFixedStep));
    AgentTemporalHash = Fold(AgentTemporalHash, static_cast<uint32>(Agent.StateEnterFixedStep));
  }
  OutSummary.AgentInputHash = AgentInputHash;
  OutSummary.AgentFineKinematicHash = AgentFineKinematicHash;
  OutSummary.AgentConfigHash = AgentConfigHash;
  OutSummary.AgentTemporalHash = AgentTemporalHash;

  uint32 SettingsHash = FnvOffset;
  SettingsHash = Fold(SettingsHash, Settings.bEnabled ? 1u : 0u);
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.TransitionRingRadiusCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.RingEnterToleranceCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.RingExitToleranceCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.ApproachSlowdownDistanceCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.SlotArrivalToleranceCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.SlotArrivalSpeedToleranceCmps, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.SlotExitToleranceCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.SlotArriveGainPerSecond, 0.0001f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.SlotOccupiedGainPerSecond, 0.0001f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.FreeSettleAttractionGainPerSecond, 0.0001f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.FreeSettleMaxSpeedCmps, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.TargetPhysicalRadiusCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.TargetHardSafetyGapCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.TargetSoftMarginCm, 0.01f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.PositionQuantumCm, 0.0001f)));
  SettingsHash = Fold(SettingsHash, static_cast<uint32>(Quantize(Settings.VelocityQuantumCmps, 0.0001f)));
  OutSummary.SettingsHash = SettingsHash;

  uint32 SlotInputHash = FnvOffset;
  for (const FCrowdDemoTargetSlotSpec& Slot : Slots)
  {
    SlotInputHash = Fold(SlotInputHash, static_cast<uint32>(Slot.SlotId));
    SlotInputHash = Fold(SlotInputHash, static_cast<uint32>(Slot.Kind));
    SlotInputHash = Fold(SlotInputHash, static_cast<uint32>(Slot.BandId));
    SlotInputHash = Fold(SlotInputHash, static_cast<uint32>(Slot.AngularIndex));
    SlotInputHash = Fold(SlotInputHash,
      static_cast<uint32>(Quantize(Slot.TargetRelativeOffset.X, Settings.PositionQuantumCm)));
    SlotInputHash = Fold(SlotInputHash,
      static_cast<uint32>(Quantize(Slot.TargetRelativeOffset.Y, Settings.PositionQuantumCm)));
    SlotInputHash = Fold(SlotInputHash, Slot.RequiredCapabilityMask);
    SlotInputHash = Fold(SlotInputHash,
      static_cast<uint32>(Quantize(Slot.CenterDistanceCm, Settings.PositionQuantumCm)));
    SlotInputHash = Fold(SlotInputHash, static_cast<uint32>(Slot.StablePriority));
  }
  OutSummary.SlotInputHash = SlotInputHash;
  uint32 FullInputHash = TargetHash;
  FullInputHash = Fold(FullInputHash, SettingsHash);
  FullInputHash = Fold(FullInputHash, SlotInputHash);
  FullInputHash = Fold(FullInputHash, AgentInputHash);
  FullInputHash = Fold(FullInputHash, AgentConfigHash);
  FullInputHash = Fold(FullInputHash, static_cast<uint32>(FixedStepIndex));
  OutSummary.FullInputHash = FullInputHash;

  uint32 OwnerStateHash = FnvOffset;
  uint32 TransitionHash = FnvOffset;
  uint32 GuidanceHash = FnvOffset;
  uint32 GuidanceLocationHash = FnvOffset;
  uint32 GuidanceVelocityHash = FnvOffset;
  for (const FCrowdDemoTargetApproachResult& Result : OutResults)
  {
    OwnerStateHash = Fold(OwnerStateHash, static_cast<uint32>(Result.AgentId));
    OwnerStateHash = Fold(OwnerStateHash, static_cast<uint32>(Result.State));
    OwnerStateHash = Fold(OwnerStateHash, static_cast<uint32>(Result.AssignedSlotId));
    OwnerStateHash = Fold(OwnerStateHash, static_cast<uint32>(Result.SlotLayoutRevision));
    TransitionHash = Fold(TransitionHash, static_cast<uint32>(Result.AgentId));
    TransitionHash = Fold(TransitionHash, static_cast<uint32>(Result.RingEnterFixedStep));
    TransitionHash = Fold(TransitionHash, static_cast<uint32>(Result.StateEnterFixedStep));
    GuidanceHash = Fold(GuidanceHash, static_cast<uint32>(Result.AgentId));
    GuidanceHash = Fold(GuidanceHash, static_cast<uint32>(Quantize(Result.DesiredLocation.X, Settings.PositionQuantumCm)));
    GuidanceHash = Fold(GuidanceHash, static_cast<uint32>(Quantize(Result.DesiredLocation.Y, Settings.PositionQuantumCm)));
    GuidanceHash = Fold(GuidanceHash, static_cast<uint32>(Quantize(Result.DesiredVelocity.X, Settings.VelocityQuantumCmps)));
    GuidanceHash = Fold(GuidanceHash, static_cast<uint32>(Quantize(Result.DesiredVelocity.Y, Settings.VelocityQuantumCmps)));
    GuidanceLocationHash = Fold(GuidanceLocationHash, static_cast<uint32>(Result.AgentId));
    GuidanceLocationHash = Fold(GuidanceLocationHash,
      static_cast<uint32>(Quantize(Result.DesiredLocation.X, Settings.PositionQuantumCm)));
    GuidanceLocationHash = Fold(GuidanceLocationHash,
      static_cast<uint32>(Quantize(Result.DesiredLocation.Y, Settings.PositionQuantumCm)));
    GuidanceVelocityHash = Fold(GuidanceVelocityHash, static_cast<uint32>(Result.AgentId));
    GuidanceVelocityHash = Fold(GuidanceVelocityHash,
      static_cast<uint32>(Quantize(Result.DesiredVelocity.X, Settings.VelocityQuantumCmps)));
    GuidanceVelocityHash = Fold(GuidanceVelocityHash,
      static_cast<uint32>(Quantize(Result.DesiredVelocity.Y, Settings.VelocityQuantumCmps)));
  }
  OutSummary.OwnerStateHash = OwnerStateHash;
  OutSummary.TransitionHash = TransitionHash;
  OutSummary.GuidanceHash = GuidanceHash;
  OutSummary.GuidanceLocationHash = GuidanceLocationHash;
  OutSummary.GuidanceVelocityHash = GuidanceVelocityHash;

  uint32 Hash = OutSummary.TargetFactHash;
  Hash = Fold(Hash, static_cast<uint32>(Quantize(Settings.TransitionRingRadiusCm, Settings.PositionQuantumCm)));
  Hash = Fold(Hash, static_cast<uint32>(Target.TargetRevision));
  for (const FCrowdDemoTargetSlotSpec& Slot : Slots)
  {
    Hash = Fold(Hash, static_cast<uint32>(Slot.SlotId));
    Hash = Fold(Hash, static_cast<uint32>(Slot.Kind));
    Hash = Fold(Hash, static_cast<uint32>(Slot.BandId));
    Hash = Fold(Hash, static_cast<uint32>(Slot.AngularIndex));
    Hash = Fold(Hash, static_cast<uint32>(Quantize(Slot.TargetRelativeOffset.X, Settings.PositionQuantumCm)));
    Hash = Fold(Hash, static_cast<uint32>(Quantize(Slot.TargetRelativeOffset.Y, Settings.PositionQuantumCm)));
    Hash = Fold(Hash, Slot.RequiredCapabilityMask);
    Hash = Fold(Hash, static_cast<uint32>(Quantize(
      Slot.CenterDistanceCm, Settings.PositionQuantumCm)));
    Hash = Fold(Hash, static_cast<uint32>(Slot.StablePriority));
  }
  for (const FCrowdDemoTargetApproachResult& Result : OutResults)
  {
    Hash = Fold(Hash, static_cast<uint32>(Result.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Result.State));
    Hash = Fold(Hash, static_cast<uint32>(Result.AssignedSlotId));
    Hash = Fold(Hash, static_cast<uint32>(Result.SlotLayoutRevision));
    Hash = Fold(Hash, static_cast<uint32>(Result.RingEnterFixedStep));
    Hash = Fold(Hash, static_cast<uint32>(Result.StateEnterFixedStep));
    Hash = Fold(Hash, static_cast<uint32>(Quantize(Result.DesiredLocation.X, Settings.PositionQuantumCm)));
    Hash = Fold(Hash, static_cast<uint32>(Quantize(Result.DesiredLocation.Y, Settings.PositionQuantumCm)));
    Hash = Fold(Hash, static_cast<uint32>(Quantize(Result.DesiredVelocity.X, Settings.VelocityQuantumCmps)));
    Hash = Fold(Hash, static_cast<uint32>(Quantize(Result.DesiredVelocity.Y, Settings.VelocityQuantumCmps)));
  }
  Hash = Fold(Hash, AgentInputHash);
  Hash = Fold(Hash, OwnerStateHash);
  Hash = Fold(Hash, TransitionHash);
  Hash = Fold(Hash, GuidanceHash);
  OutSummary.ApproachHash = Hash;
}
