#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"

#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

namespace
{
  constexpr uint32 HashOffset = 2166136261u;
  constexpr uint32 HashPrime = 16777619u;
  constexpr int32 BatchIntervalSteps = 15;

  uint32 Fold(uint32 Hash, const int32 Value)
  {
    const uint32 Bits = static_cast<uint32>(Value);
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= (Bits >> (Byte * 8)) & 0xffu;
      Hash *= HashPrime;
    }
    return Hash;
  }

  uint32 FoldVector(uint32 Hash, const FVector& Value)
  {
    Hash = Fold(Hash, FMath::RoundToInt(Value.X));
    Hash = Fold(Hash, FMath::RoundToInt(Value.Y));
    return Fold(Hash, FMath::RoundToInt(Value.Z));
  }

  void SortRuntimeAgents(FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
  {
    Runtime.Agents.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
  }

  void SetPhase(
    FCrowdDemoOpenSpawnRelaxationRuntime& Runtime,
    const ECrowdDemoOpenSpawnRelaxationPhase Phase,
    const int32 Step)
  {
    if (Runtime.Phase == Phase)
      return;
    Runtime.Phase = Phase;
    Runtime.PhaseStartStep = Step;
    ++Runtime.PhaseTransitionCount;
  }

  int32 ActiveCount(const FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
  {
    int32 Count = 0;
    for (const auto& Agent : Runtime.Agents)
      Count += Agent.bParticleActive ? 1 : 0;
    return Count;
  }

  void AppendActiveCount(FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
  {
    const int32 Count = ActiveCount(Runtime);
    if (Runtime.ActiveCountTransitions.IsEmpty() || Runtime.ActiveCountTransitions.Last() != Count)
      Runtime.ActiveCountTransitions.Add(Count);
  }

  void ActivateFirst(
    FCrowdDemoOpenSpawnRelaxationRuntime& Runtime,
    const FCrowdDemoOpenSpawnRelaxationLayout& Layout,
    const int32 DesiredCount)
  {
    TArray<const FCrowdDemoOpenSpawnRelaxationLayoutAgent*> Ordered;
    for (const auto& Agent : Layout.Agents)
      if (!Agent.bInsertionSource)
        Ordered.Add(&Agent);
    Ordered.Sort([](const auto& A, const auto& B)
    {
      if (A.FormationIndex != B.FormationIndex)
        return A.FormationIndex < B.FormationIndex;
      return A.AgentId < B.AgentId;
    });

    for (int32 Index = 0; Index < FMath::Min(DesiredCount, Ordered.Num()); ++Index)
    {
      const auto* LayoutAgent = Ordered[Index];
      if (auto* State = Runtime.Agents.FindByPredicate([&](const auto& Candidate)
        { return Candidate.AgentId == LayoutAgent->AgentId; }))
      {
        if (!State->bParticleActive)
        {
          State->bParticleActive = true;
          State->bPendingBoundaryReset = true;
          State->BoundaryResetLocation = LayoutAgent->ActiveLocation;
        }
      }
    }
    AppendActiveCount(Runtime);
  }

  void RebuildPropagation(FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
  {
    Runtime.CumulativeInfluenceEdges.Sort([](const auto& A, const auto& B)
    {
      if (A.MinAgentId != B.MinAgentId) return A.MinAgentId < B.MinAgentId;
      return A.MaxAgentId < B.MaxAgentId;
    });
    int32 UniqueCount = 0;
    for (int32 Index = 0; Index < Runtime.CumulativeInfluenceEdges.Num(); ++Index)
    {
      if (UniqueCount == 0 ||
          Runtime.CumulativeInfluenceEdges[Index].MinAgentId != Runtime.CumulativeInfluenceEdges[UniqueCount - 1].MinAgentId ||
          Runtime.CumulativeInfluenceEdges[Index].MaxAgentId != Runtime.CumulativeInfluenceEdges[UniqueCount - 1].MaxAgentId)
      {
        Runtime.CumulativeInfluenceEdges[UniqueCount++] = Runtime.CumulativeInfluenceEdges[Index];
      }
    }
    Runtime.CumulativeInfluenceEdges.SetNum(UniqueCount);

    SortRuntimeAgents(Runtime);
    Runtime.PropagationLayersByAgent.Init(INDEX_NONE, Runtime.Agents.Num());
    const int32 SourceIndex = Runtime.Agents.IndexOfByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Runtime.SourceAgentId; });
    if (SourceIndex == INDEX_NONE)
      return;

    TArray<int32> Queue;
    Runtime.PropagationLayersByAgent[SourceIndex] = 0;
    Queue.Add(Runtime.SourceAgentId);
    int32 ReadIndex = 0;
    while (ReadIndex < Queue.Num())
    {
      const int32 AgentId = Queue[ReadIndex++];
      const int32 AgentIndex = Runtime.Agents.IndexOfByPredicate([&](const auto& Agent)
        { return Agent.AgentId == AgentId; });
      const int32 ParentLayer = Runtime.PropagationLayersByAgent[AgentIndex];
      for (const auto& Edge : Runtime.CumulativeInfluenceEdges)
      {
        int32 OtherId = INDEX_NONE;
        if (Edge.MinAgentId == AgentId) OtherId = Edge.MaxAgentId;
        else if (Edge.MaxAgentId == AgentId) OtherId = Edge.MinAgentId;
        if (OtherId == INDEX_NONE) continue;
        const int32 OtherIndex = Runtime.Agents.IndexOfByPredicate([&](const auto& Agent)
          { return Agent.AgentId == OtherId; });
        if (OtherIndex != INDEX_NONE && Runtime.PropagationLayersByAgent[OtherIndex] == INDEX_NONE)
        {
          Runtime.PropagationLayersByAgent[OtherIndex] = ParentLayer + 1;
          Queue.Add(OtherId);
        }
      }
    }

    Runtime.PressurePropagationLayerMax = 0;
    for (const int32 Layer : Runtime.PropagationLayersByAgent)
      if (Layer != INDEX_NONE)
        Runtime.PressurePropagationLayerMax = FMath::Max(Runtime.PressurePropagationLayerMax, Layer);
    Runtime.LayerAgentCounts.Init(0, Runtime.PressurePropagationLayerMax + 1);
    for (const int32 Layer : Runtime.PropagationLayersByAgent)
      if (Layer != INDEX_NONE)
        ++Runtime.LayerAgentCounts[Layer];
  }
}

FCrowdDemoSharedFlowFieldConfig FCrowdDemoOpenSpawnRelaxationKernel::MakeOpenFlowConfig()
{
  FCrowdDemoSharedFlowFieldConfig Config = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  Config.ObstacleSpecs.Reset();
  Config.BoundsMin = FVector(-4000.0f, -4000.0f, 0.0f);
  Config.BoundsMax = FVector(4000.0f, 4000.0f, 0.0f);
  Config.GoalLocation = FVector::ZeroVector;
  return Config;
}

FCrowdDemoOpenSpawnRelaxationLayout FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(
  TConstArrayView<FCrowdDemoOpenSpawnRelaxationLayoutInput> Inputs,
  const float PhysicalRadiusCm,
  const float HardSafetyGapCm,
  const float SoftMarginCm)
{
  FCrowdDemoOpenSpawnRelaxationLayout Out;
  TArray<FCrowdDemoOpenSpawnRelaxationLayoutInput> Sorted(Inputs);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.FormationIndex != B.FormationIndex) return A.FormationIndex < B.FormationIndex;
    return A.AgentId < B.AgentId;
  });
  if (Sorted.Num() != 20)
    return Out;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
    if (Sorted[Index].AgentId == INDEX_NONE || Sorted[Index].FormationIndex != Index ||
      (Index > 0 && Sorted[Index - 1].AgentId == Sorted[Index].AgentId))
      return Out;

  const float Radii[] = {100.0f, 229.0f, 358.0f};
  const int32 RadialFormationStarts[] = {10, 5, 0};
  for (const auto& Input : Sorted)
  {
    FCrowdDemoOpenSpawnRelaxationLayoutAgent Agent;
    Agent.AgentId = Input.AgentId;
    Agent.FormationIndex = Input.FormationIndex;
    Agent.StagingLocation = FVector(
      -3400.0f + static_cast<float>(Input.FormationIndex % 5) * 150.0f,
      -3400.0f + static_cast<float>(Input.FormationIndex / 5) * 150.0f,
      0.0f);
    int32 RadialLayer = INDEX_NONE;
    int32 RadialSpoke = INDEX_NONE;
    for (int32 Layer = 0; Layer < 3; ++Layer)
    {
      if (Input.FormationIndex >= RadialFormationStarts[Layer]
        && Input.FormationIndex < RadialFormationStarts[Layer] + 4)
      {
        RadialLayer = Layer;
        RadialSpoke = Input.FormationIndex - RadialFormationStarts[Layer];
        break;
      }
    }
    if (RadialLayer != INDEX_NONE)
    {
      const float Angle = static_cast<float>(RadialSpoke) * UE_TWO_PI / 4.0f;
      Agent.ActiveLocation = FVector(FMath::Cos(Angle) * Radii[RadialLayer],
        FMath::Sin(Angle) * Radii[RadialLayer], 0.0f);
    }
    else if (Input.FormationIndex != 19)
    {
      static const int32 ExtraIndices[] = {4, 9, 14, 15, 16, 17, 18};
      int32 ExtraOrdinal = INDEX_NONE;
      for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExtraIndices); ++Index)
        if (ExtraIndices[Index] == Input.FormationIndex) ExtraOrdinal = Index;
      if (ExtraOrdinal == INDEX_NONE) return Out;
      const float Angle = (static_cast<float>(ExtraOrdinal) + 0.5f)
        * UE_TWO_PI / static_cast<float>(UE_ARRAY_COUNT(ExtraIndices));
      Agent.ActiveLocation = FVector(FMath::Cos(Angle) * 700.0f,
        FMath::Sin(Angle) * 700.0f, 0.0f);
    }
    else
    {
      Agent.ActiveLocation = FVector::ZeroVector;
      Agent.bInsertionSource = true;
    }
    Agent.bRemovalAgent = Input.FormationIndex == 10;
    if (Agent.bInsertionSource) Out.SourceAgentId = Agent.AgentId;
    if (Agent.bRemovalAgent) Out.RemovedAgentId = Agent.AgentId;
    Out.Agents.Add(Agent);
  }

  const float HardDistance = PhysicalRadiusCm * 2.0f + HardSafetyGapCm;
  const float SoftDistance = HardDistance + SoftMarginCm * 2.0f;
  bool bSourceSoft = false;
  for (int32 A = 0; A < Out.Agents.Num(); ++A)
  {
    const auto& AgentA = Out.Agents[A];
    const FVector2f StagingA(AgentA.StagingLocation.X, AgentA.StagingLocation.Y);
    const FVector2f ActiveA(AgentA.ActiveLocation.X, AgentA.ActiveLocation.Y);
    if (FMath::Abs(StagingA.X) > 3948.0f || FMath::Abs(StagingA.Y) > 3948.0f ||
        FMath::Abs(ActiveA.X) > 3948.0f || FMath::Abs(ActiveA.Y) > 3948.0f)
      return Out;
    for (int32 B = A + 1; B < Out.Agents.Num(); ++B)
    {
      const auto& AgentB = Out.Agents[B];
      if (FVector::Dist2D(AgentA.StagingLocation, AgentB.StagingLocation) + 0.001f < HardDistance)
        return Out;
      const float ActiveDistance = FVector::Dist2D(AgentA.ActiveLocation, AgentB.ActiveLocation);
      if (ActiveDistance + 0.001f < HardDistance)
        return Out;
      if ((AgentA.bInsertionSource || AgentB.bInsertionSource) && ActiveDistance < SoftDistance)
        bSourceSoft = true;
    }
  }
  if (!bSourceSoft)
    return Out;

  Out.LayoutHash = HashOffset;
  for (const auto& Agent : Out.Agents)
  {
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.AgentId);
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.FormationIndex);
    Out.LayoutHash = FoldVector(Out.LayoutHash, Agent.StagingLocation);
    Out.LayoutHash = FoldVector(Out.LayoutHash, Agent.ActiveLocation);
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.bInsertionSource ? 1 : 0);
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.bRemovalAgent ? 1 : 0);
  }
  Out.bValid = true;
  return Out;
}

FCrowdDemoOpenSpawnRelaxationRuntime FCrowdDemoOpenSpawnRelaxationKernel::InitializeRuntime(
  const FCrowdDemoOpenSpawnRelaxationLayout& Layout)
{
  FCrowdDemoOpenSpawnRelaxationRuntime Runtime;
  Runtime.bValid = Layout.bValid;
  Runtime.SourceAgentId = Layout.SourceAgentId;
  Runtime.RemovedAgentId = Layout.RemovedAgentId;
  Runtime.ActiveCountTransitions.Add(0);
  for (const auto& LayoutAgent : Layout.Agents)
  {
    FCrowdDemoOpenSpawnRelaxationAgentState State;
    State.AgentId = LayoutAgent.AgentId;
    State.FormationIndex = LayoutAgent.FormationIndex;
    State.BoundaryResetLocation = LayoutAgent.StagingLocation;
    Runtime.Agents.Add(State);
  }
  SortRuntimeAgents(Runtime);
  Runtime.PreInsertLocationsByAgent.Reset(Runtime.Agents.Num());
  for (const auto& State : Runtime.Agents)
  {
    const auto* LayoutAgent = Layout.Agents.FindByPredicate([&](const auto& Candidate)
      { return Candidate.AgentId == State.AgentId; });
    if (!LayoutAgent)
    {
      Runtime.bValid = false;
      Runtime.PreInsertLocationsByAgent.Add(FVector::ZeroVector);
    }
    else
    {
      Runtime.PreInsertLocationsByAgent.Add(LayoutAgent->ActiveLocation);
    }
  }
  RebuildHashes(Runtime);
  return Runtime;
}

void FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(
  const int32 FixedStepIndex,
  const FCrowdDemoOpenSpawnRelaxationLayout& Layout,
  FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
{
  if (!Runtime.bValid || Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::Completed ||
      Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::Failed)
    return;

  if (Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::Staging)
  {
    SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::BatchActivation, FixedStepIndex);
    ActivateFirst(Runtime, Layout, 5);
    ++Runtime.BatchActivationCount;
  }
  else if (Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::BatchActivation &&
           FixedStepIndex - Runtime.PhaseStartStep >= BatchIntervalSteps)
  {
    const int32 Current = ActiveCount(Runtime);
    const int32 Desired = Current < 10 ? 10 : Current < 15 ? 15 : 19;
    ActivateFirst(Runtime, Layout, Desired);
    ++Runtime.BatchActivationCount;
    Runtime.PhaseStartStep = FixedStepIndex;
    if (Desired == 19)
      SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::SourceInsertion, FixedStepIndex);
  }
  else if (Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::SourceInsertion &&
           FixedStepIndex - Runtime.PhaseStartStep >= BatchIntervalSteps)
  {
    const auto* LayoutAgent = Layout.Agents.FindByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Runtime.SourceAgentId; });
    auto* State = Runtime.Agents.FindByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Runtime.SourceAgentId; });
    if (!LayoutAgent || !State)
    {
      Runtime.bValid = false;
      SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::Failed, FixedStepIndex);
    }
    else
    {
      State->bParticleActive = true;
      State->bPendingBoundaryReset = true;
      State->BoundaryResetLocation = LayoutAgent->ActiveLocation;
      AppendActiveCount(Runtime);
      Runtime.InsertSettling = {};
      Runtime.CumulativeInfluenceEdges.Reset();
      SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::PropagationAndInsertSettle, FixedStepIndex);
    }
  }
  else if (Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::Removal)
  {
    const auto* LayoutAgent = Layout.Agents.FindByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Runtime.RemovedAgentId; });
    auto* State = Runtime.Agents.FindByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Runtime.RemovedAgentId; });
    if (!LayoutAgent || !State)
    {
      Runtime.bValid = false;
      SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::Failed, FixedStepIndex);
    }
    else
    {
      State->bParticleActive = false;
      State->bPendingBoundaryReset = true;
      State->BoundaryResetLocation = LayoutAgent->StagingLocation;
      AppendActiveCount(Runtime);
      Runtime.PostRemovalSettling = {};
      SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::PostRemovalSettle, FixedStepIndex);
    }
  }
  RebuildHashes(Runtime);
}

void FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
  const int32 FixedStepIndex,
  TConstArrayView<FCrowdDemoParticleSoftPairInfluence> Influences,
  const float MaxActualCorrectionCm,
  const float SoftErrorCmP95,
  FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
{
  if (!Runtime.bValid)
    return;
  if (Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::PropagationAndInsertSettle)
  {
    for (const auto& Influence : Influences)
    {
      if (Influence.RealizedCorrectionA.Size2D() <= KINDA_SMALL_NUMBER &&
          Influence.RealizedCorrectionB.Size2D() <= KINDA_SMALL_NUMBER)
        continue;
      FCrowdDemoOpenSpawnRelaxationEdge Edge;
      Edge.MinAgentId = FMath::Min(Influence.MinAgentId, Influence.MaxAgentId);
      Edge.MaxAgentId = FMath::Max(Influence.MinAgentId, Influence.MaxAgentId);
      Runtime.CumulativeInfluenceEdges.Add(Edge);
    }
    RebuildPropagation(Runtime);
    FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
      Runtime.InsertSettling, MaxActualCorrectionCm, SoftErrorCmP95);
    if (Runtime.InsertSettling.SettlingSteps != INDEX_NONE &&
        Runtime.PressurePropagationLayerMax >= 3)
    {
      Runtime.InsertSettlingStep = FixedStepIndex;
      SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::Removal, FixedStepIndex);
    }
  }
  else if (Runtime.Phase == ECrowdDemoOpenSpawnRelaxationPhase::PostRemovalSettle)
  {
    FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
      Runtime.PostRemovalSettling, MaxActualCorrectionCm, SoftErrorCmP95);
    if (Runtime.PostRemovalSettling.SettlingSteps != INDEX_NONE)
    {
      Runtime.PostRemovalSettlingStep = FixedStepIndex;
      SetPhase(Runtime, ECrowdDemoOpenSpawnRelaxationPhase::Completed, FixedStepIndex);
    }
  }
  RebuildHashes(Runtime);
}

void FCrowdDemoOpenSpawnRelaxationKernel::RecordFinalLocations(
  TConstArrayView<int32> AgentIds,
  TConstArrayView<FVector> Locations,
  FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
{
  if (AgentIds.Num() != Locations.Num() || Runtime.PreInsertLocationsByAgent.Num() != Runtime.Agents.Num())
    return;
  Runtime.OldLayoutReturnedAgentCount = 0;
  Runtime.NewEquilibriumDisplacedAgentCount = 0;
  for (int32 Index = 0; Index < Runtime.Agents.Num(); ++Index)
  {
    const auto& Agent = Runtime.Agents[Index];
    if (!Agent.bParticleActive) continue;
    const int32 InputIndex = AgentIds.IndexOfByKey(Agent.AgentId);
    if (InputIndex != INDEX_NONE
      && FVector::Dist2D(Locations[InputIndex], Runtime.PreInsertLocationsByAgent[Index]) > 1.0f)
      ++Runtime.NewEquilibriumDisplacedAgentCount;
  }
  RebuildHashes(Runtime);
}

void FCrowdDemoOpenSpawnRelaxationKernel::RebuildHashes(
  FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
{
  SortRuntimeAgents(Runtime);
  Runtime.ParticipationHash = HashOffset;
  Runtime.PhaseHash = HashOffset;
  for (const auto& Agent : Runtime.Agents)
  {
    Runtime.ParticipationHash = Fold(Runtime.ParticipationHash, Agent.AgentId);
    Runtime.ParticipationHash = Fold(Runtime.ParticipationHash, Agent.FormationIndex);
    Runtime.ParticipationHash = Fold(Runtime.ParticipationHash, Agent.bParticleActive ? 1 : 0);
    Runtime.ParticipationHash = Fold(Runtime.ParticipationHash, Agent.bPendingBoundaryReset ? 1 : 0);
    Runtime.ParticipationHash = FoldVector(Runtime.ParticipationHash, Agent.BoundaryResetLocation);
  }
  Runtime.PropagationHash = HashOffset;
  for (const auto& Edge : Runtime.CumulativeInfluenceEdges)
  {
    Runtime.PropagationHash = Fold(Runtime.PropagationHash, Edge.MinAgentId);
    Runtime.PropagationHash = Fold(Runtime.PropagationHash, Edge.MaxAgentId);
  }
  for (const int32 Layer : Runtime.PropagationLayersByAgent)
    Runtime.PropagationHash = Fold(Runtime.PropagationHash, Layer);
  Runtime.PhaseHash = Fold(Runtime.PhaseHash, static_cast<int32>(Runtime.Phase));
  Runtime.PhaseHash = Fold(Runtime.PhaseHash, Runtime.PhaseStartStep);
  Runtime.PhaseHash = Fold(Runtime.PhaseHash, Runtime.PhaseTransitionCount);
  Runtime.PhaseHash = Fold(Runtime.PhaseHash, Runtime.InsertSettling.StepCount);
  Runtime.PhaseHash = Fold(Runtime.PhaseHash, Runtime.PostRemovalSettling.StepCount);
  Runtime.PhaseHash = Fold(Runtime.PhaseHash, static_cast<int32>(Runtime.ParticipationHash));
  Runtime.PhaseHash = Fold(Runtime.PhaseHash, static_cast<int32>(Runtime.PropagationHash));
}

bool FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
  const int32 FixedStepIndex,
  const TConstArrayView<int32> ExpectedAgentIds,
  const FCrowdDemoOpenSpawnRelaxationRuntime& Runtime,
  TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact>& OutFacts)
{
  OutFacts.Reset();
  if (!Runtime.bValid || FixedStepIndex < 0
    || Runtime.Agents.Num() != ExpectedAgentIds.Num())
    return false;

  TArray<int32> SortedExpected(ExpectedAgentIds);
  SortedExpected.Sort();
  for (int32 Index = 0; Index < SortedExpected.Num(); ++Index)
    if (SortedExpected[Index] == INDEX_NONE
      || (Index > 0 && SortedExpected[Index - 1] == SortedExpected[Index]))
      return false;

  TArray<FCrowdDemoOpenSpawnRelaxationAgentState> SortedAgents = Runtime.Agents;
  SortedAgents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
  {
    const FCrowdDemoOpenSpawnRelaxationAgentState& Agent = SortedAgents[Index];
    if (Agent.AgentId == INDEX_NONE || Agent.AgentId != SortedExpected[Index]
      || (Index > 0 && SortedAgents[Index - 1].AgentId == Agent.AgentId))
    {
      OutFacts.Reset();
      return false;
    }
    FCrowdDemoPreparedOpenSpawnBoundaryFact& Fact = OutFacts.AddDefaulted_GetRef();
    Fact.AgentId = Agent.AgentId;
    Fact.FormationIndex = Agent.FormationIndex;
    Fact.FixedStepIndex = FixedStepIndex;
    Fact.bParticleActive = Agent.bParticleActive;
    Fact.bPendingBoundaryReset = Agent.bPendingBoundaryReset;
    Fact.BoundaryResetLocation = Agent.BoundaryResetLocation;
  }
  return ValidatePreparedBoundaryFacts(FixedStepIndex, ExpectedAgentIds, OutFacts);
}

bool FCrowdDemoOpenSpawnRelaxationKernel::ValidatePreparedBoundaryFacts(
  const int32 FixedStepIndex,
  const TConstArrayView<int32> ExpectedAgentIds,
  const TConstArrayView<FCrowdDemoPreparedOpenSpawnBoundaryFact> Facts)
{
  if (FixedStepIndex < 0 || ExpectedAgentIds.Num() != Facts.Num()) return false;

  TArray<int32> SortedExpected(ExpectedAgentIds);
  SortedExpected.Sort();
  for (int32 Index = 0; Index < SortedExpected.Num(); ++Index)
  {
    if (SortedExpected[Index] == INDEX_NONE
      || (Index > 0 && SortedExpected[Index - 1] == SortedExpected[Index]))
      return false;
  }

  TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact> SortedFacts(Facts);
  SortedFacts.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < SortedFacts.Num(); ++Index)
  {
    if (SortedFacts[Index].AgentId != SortedExpected[Index]
      || SortedFacts[Index].FixedStepIndex != FixedStepIndex
      || (Index > 0 && SortedFacts[Index - 1].AgentId == SortedFacts[Index].AgentId))
      return false;
  }
  return true;
}

bool FCrowdDemoOpenSpawnRelaxationKernel::ConsumePendingBoundaryResets(
  const TConstArrayView<int32> AgentIds,
  FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
{
  if (!Runtime.bValid) return false;
  TArray<int32> SortedIds(AgentIds);
  SortedIds.Sort();
  for (int32 Index = 0; Index < SortedIds.Num(); ++Index)
  {
    if (SortedIds[Index] == INDEX_NONE
      || (Index > 0 && SortedIds[Index - 1] == SortedIds[Index]))
      return false;
    const FCrowdDemoOpenSpawnRelaxationAgentState* Agent =
      Runtime.Agents.FindByPredicate([&](const auto& Value)
      {
        return Value.AgentId == SortedIds[Index];
      });
    if (!Agent || !Agent->bPendingBoundaryReset) return false;
  }
  for (const int32 AgentId : SortedIds)
    Runtime.Agents.FindByPredicate([&](const auto& Value)
    {
      return Value.AgentId == AgentId;
    })->bPendingBoundaryReset = false;
  return true;
}
