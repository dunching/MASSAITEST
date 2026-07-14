#include "Mass/CrowdDemoTrafficSchedulingKernel.h"

namespace
{
  uint32 TrafficHashInt(uint32 Hash, const int32 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    return Hash * 16777619u;
  }

  FVector2f QuantizedDirection(const FVector2f V)
  {
    const float Size = V.Size();
    if (Size <= KINDA_SMALL_NUMBER)
    {
      return FVector2f::ZeroVector;
    }
    const FVector2f N = V / Size;
    return FVector2f(
      FMath::RoundToFloat(N.X * 32767.0f) / 32767.0f,
      FMath::RoundToFloat(N.Y * 32767.0f) / 32767.0f);
  }

  int32 CellKey(const FCrowdDemoSharedFlowFieldConfig& Config, const float CellSize, const FVector2f Position)
  {
    const int32 Width = FMath::Max(1, FMath::CeilToInt((Config.BoundsMax.X - Config.BoundsMin.X) / CellSize));
    const int32 X = FMath::Clamp(FMath::FloorToInt((Position.X - Config.BoundsMin.X) / CellSize), 0, Width - 1);
    const int32 Height = FMath::Max(1, FMath::CeilToInt((Config.BoundsMax.Y - Config.BoundsMin.Y) / CellSize));
    const int32 Y = FMath::Clamp(FMath::FloorToInt((Position.Y - Config.BoundsMin.Y) / CellSize), 0, Height - 1);
    return Y * Width + X;
  }
}

uint32 FCrowdDemoTrafficSchedulingKernel::StableHash(const int32 A, const int32 B, const int32 C)
{
  uint32 Hash = 2166136261u;
  Hash = TrafficHashInt(Hash, A);
  Hash = TrafficHashInt(Hash, B);
  return TrafficHashInt(Hash, C);
}

void FCrowdDemoTrafficSchedulingKernel::BuildTrafficCells(
  const TConstArrayView<FCrowdDemoTrafficAgent> Agents,
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const FCrowdDemoTrafficSettings& Settings,
  TArray<FCrowdDemoTrafficCell>& OutCells,
  uint32& OutHash)
{
  TArray<FCrowdDemoTrafficAgent> Sorted(Agents);
  Sorted.Sort([](const FCrowdDemoTrafficAgent& A, const FCrowdDemoTrafficAgent& B) { return A.AgentId < B.AgentId; });
  struct FAccum { int32 Count=0; int32 Reserved=0; FVector2f Velocity=FVector2f::ZeroVector; FVector2f Direction=FVector2f::ZeroVector; };
  TMap<int32, FAccum> ByKey;
  for (const FCrowdDemoTrafficAgent& Agent : Sorted)
  {
    FAccum& Accum = ByKey.FindOrAdd(CellKey(Config, Settings.CellSizeCm, Agent.Position));
    ++Accum.Count;
    Accum.Reserved += Agent.AdmissionState == ECrowdDemoPortalAdmissionState::Reserved ? 1 : 0;
    Accum.Velocity += FVector2f(FMath::RoundToFloat(Agent.Velocity.X), FMath::RoundToFloat(Agent.Velocity.Y));
    Accum.Direction += QuantizedDirection(Agent.FlowDirection);
  }
  TArray<int32> Keys;
  ByKey.GetKeys(Keys);
  Keys.Sort();
  OutCells.Reset(Keys.Num());
  OutHash = 2166136261u;
  for (const int32 Key : Keys)
  {
    const FAccum& Accum = ByKey.FindChecked(Key);
    FCrowdDemoTrafficCell& Cell = OutCells.AddDefaulted_GetRef();
    Cell.StableCellKey = Key;
    Cell.AgentCount = Accum.Count;
    Cell.ReservedAgentCount = Accum.Reserved;
    Cell.MeanVelocity = Accum.Count > 0
      ? FVector2f(FMath::RoundToFloat(Accum.Velocity.X / Accum.Count), FMath::RoundToFloat(Accum.Velocity.Y / Accum.Count))
      : FVector2f::ZeroVector;
    Cell.DominantDirection = QuantizedDirection(Accum.Direction);
    OutHash = TrafficHashInt(OutHash, Key);
    OutHash = TrafficHashInt(OutHash, Cell.AgentCount);
    OutHash = TrafficHashInt(OutHash, Cell.ReservedAgentCount);
    OutHash = TrafficHashInt(OutHash, FMath::RoundToInt(Cell.MeanVelocity.X));
    OutHash = TrafficHashInt(OutHash, FMath::RoundToInt(Cell.MeanVelocity.Y));
    OutHash = TrafficHashInt(OutHash, FMath::RoundToInt(Cell.DominantDirection.X * 32767.0f));
    OutHash = TrafficHashInt(OutHash, FMath::RoundToInt(Cell.DominantDirection.Y * 32767.0f));
  }
}

void FCrowdDemoTrafficSchedulingKernel::ExtractPortals(
  const FCrowdDemoSharedFlowField& FlowField,
  const FCrowdDemoTrafficSettings& Settings,
  TArray<FCrowdDemoTrafficPortal>& OutPortals,
  FCrowdDemoPortalExtractionSummary* OutSummary,
  const int32 PreferredAxis)
{
  struct FCandidate
  {
    int32 Axis = 0;
    int32 Cross = 0;
    int32 Min = 0;
    int32 Max = 0;
    int32 Width = 0;
    int32 Key = INDEX_NONE;
    int32 MergedCount = 1;
    int32 UpstreamWidth = 0;
    int32 DownstreamWidth = 0;
  };
  FCrowdDemoPortalExtractionSummary Summary;
  const bool bDiagnostic = FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoSf3PortalDiagnostic"));
  TArray<FCandidate> RawCandidates;
  for (int32 CellIndex = 0; CellIndex < FlowField.Blocked.Num(); ++CellIndex)
  {
    if (FlowField.Blocked[CellIndex] || !FlowField.NextCellIndex.IsValidIndex(CellIndex)) continue;
    const int32 Next = FlowField.NextCellIndex[CellIndex];
    if (Next == INDEX_NONE) continue;
    const int32 X = CellIndex % FlowField.Width;
    const int32 Y = CellIndex / FlowField.Width;
    const int32 NX = Next % FlowField.Width;
    const int32 NY = Next / FlowField.Width;
    if (FMath::Abs(NX-X) + FMath::Abs(NY-Y) != 1) continue;
    const int32 Axis = NX != X ? 0 : 1;
    const int32 Cross = Axis == 0 ? FMath::Min(X, NX) : FMath::Min(Y, NY);
    int32 Min = Axis == 0 ? Y : X;
    int32 Max = Min;
    auto IsFreeOnBothSides = [&](const int32 Along)
    {
      const int32 AX = Axis == 0 ? Cross : Along;
      const int32 AY = Axis == 0 ? Along : Cross;
      const int32 BX = Axis == 0 ? Cross + 1 : Along;
      const int32 BY = Axis == 0 ? Along : Cross + 1;
      if (AX < 0 || AX >= FlowField.Width || BX < 0 || BX >= FlowField.Width
        || AY < 0 || AY >= FlowField.Height || BY < 0 || BY >= FlowField.Height)
      {
        return false;
      }
      const int32 AKey = AY * FlowField.Width + AX;
      const int32 BKey = BY * FlowField.Width + BX;
      return !FlowField.Blocked[AKey] && !FlowField.Blocked[BKey];
    };
    while (IsFreeOnBothSides(Min-1)) --Min;
    while (IsFreeOnBothSides(Max+1)) ++Max;
    const int32 Width = Max-Min+1;
    RawCandidates.Add({Axis, Cross, Min, Max, Width, CellIndex, 1});
  }
  Summary.RawCrossSectionCandidateCount = RawCandidates.Num();
  RawCandidates.Sort([](const FCandidate& A, const FCandidate& B)
  {
    if (A.Axis != B.Axis) return A.Axis < B.Axis;
    if (A.Cross != B.Cross) return A.Cross < B.Cross;
    if (A.Min != B.Min) return A.Min < B.Min;
    if (A.Max != B.Max) return A.Max < B.Max;
    return A.Key < B.Key;
  });

  TArray<FCandidate> Unique;
  for (const FCandidate& Candidate : RawCandidates)
  {
    if (!Unique.IsEmpty())
    {
      FCandidate& Last = Unique.Last();
      if (Last.Axis == Candidate.Axis && Last.Cross == Candidate.Cross
        && Last.Min == Candidate.Min && Last.Max == Candidate.Max)
      {
        Last.Key = FMath::Min(Last.Key, Candidate.Key);
        ++Last.MergedCount;
        continue;
      }
    }
    Unique.Add(Candidate);
  }
  Summary.UniqueCrossSectionCandidateCount = Unique.Num();
  Summary.DuplicateRejectedCount = RawCandidates.Num() - Unique.Num();

  TArray<bool> Consumed;
  Consumed.Init(false, Unique.Num());
  TArray<FCandidate> LocalMinima;
  const int32 Window = FMath::Max(1, Settings.PortalClearanceWindowCells);
  for (int32 SeedIndex = 0; SeedIndex < Unique.Num(); ++SeedIndex)
  {
    if (Consumed[SeedIndex]) continue;
    if (PreferredAxis != INDEX_NONE && Unique[SeedIndex].Axis != PreferredAxis)
    {
      Consumed[SeedIndex] = true;
      continue;
    }
    TArray<int32> Plateau;
    Plateau.Add(SeedIndex);
    Consumed[SeedIndex] = true;
    int32 LastCross = Unique[SeedIndex].Cross;
    int32 SpanMin = Unique[SeedIndex].Min;
    int32 SpanMax = Unique[SeedIndex].Max;
    for (;;)
    {
      int32 BestNext = INDEX_NONE;
      for (int32 Index = SeedIndex + 1; Index < Unique.Num(); ++Index)
      {
        if (Consumed[Index]) continue;
        const FCandidate& Candidate = Unique[Index];
        if (Candidate.Axis != Unique[SeedIndex].Axis || Candidate.Cross > LastCross + Window) break;
        if (Candidate.Cross > LastCross && Candidate.Cross <= LastCross + Window
          && Candidate.Width == Unique[SeedIndex].Width
          && !(Candidate.Max < SpanMin || Candidate.Min > SpanMax))
        {
          BestNext = Index;
          break;
        }
      }
      if (BestNext == INDEX_NONE) break;
      Consumed[BestNext] = true;
      Plateau.Add(BestNext);
      LastCross = Unique[BestNext].Cross;
      SpanMin = FMath::Max(SpanMin, Unique[BestNext].Min);
      SpanMax = FMath::Min(SpanMax, Unique[BestNext].Max);
    }

    const int32 StartCross = Unique[Plateau[0]].Cross;
    const int32 EndCross = Unique[Plateau.Last()].Cross;
    auto FindNeighborWidth = [&](const int32 Direction)
    {
      int32 Result = MAX_int32;
      for (int32 Offset = 1; Offset <= Window; ++Offset)
      {
        const int32 TargetCross = Direction < 0 ? StartCross - Offset : EndCross + Offset;
        for (const FCandidate& Candidate : Unique)
        {
          if (Candidate.Axis == Unique[SeedIndex].Axis && Candidate.Cross == TargetCross
            && !(Candidate.Max < SpanMin || Candidate.Min > SpanMax))
          {
            Result = FMath::Min(Result, Candidate.Width);
          }
        }
      }
      return Result;
    };
    const int32 UpstreamWidth = FindNeighborWidth(-1);
    const int32 DownstreamWidth = FindNeighborWidth(1);
    const int32 PerpendicularSize = Unique[SeedIndex].Axis == 0 ? FlowField.Height : FlowField.Width;
    const bool bTouchesFieldBoundary = SpanMin <= 0 || SpanMax >= PerpendicularSize - 1;
    if (UpstreamWidth == MAX_int32 || DownstreamWidth == MAX_int32
      || UpstreamWidth <= Unique[SeedIndex].Width || DownstreamWidth <= Unique[SeedIndex].Width
      || bTouchesFieldBoundary)
    {
      if (bDiagnostic && Unique[SeedIndex].Width <= Settings.PortalMaxWidthCells)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf3PortalPlateau rejected=1 axis=%d cross_min=%d cross_max=%d span_min=%d span_max=%d width=%d upstream_width=%d downstream_width=%d boundary=%d source=TrafficKernel"),
          Unique[SeedIndex].Axis, StartCross, EndCross, SpanMin, SpanMax,
          Unique[SeedIndex].Width, UpstreamWidth, DownstreamWidth, bTouchesFieldBoundary ? 1 : 0);
      }
      ++Summary.PlateauRejectedCount;
      continue;
    }

    const float MidCross = 0.5f * static_cast<float>(StartCross + EndCross);
    int32 RepresentativeIndex = Plateau[0];
    for (const int32 Index : Plateau)
    {
      const float Distance = FMath::Abs(static_cast<float>(Unique[Index].Cross) - MidCross);
      const float BestDistance = FMath::Abs(static_cast<float>(Unique[RepresentativeIndex].Cross) - MidCross);
      if (Distance < BestDistance || (Distance == BestDistance && Unique[Index].Key < Unique[RepresentativeIndex].Key))
      {
        RepresentativeIndex = Index;
      }
    }
    FCandidate Candidate = Unique[RepresentativeIndex];
    Candidate.Min = SpanMin;
    Candidate.Max = SpanMax;
    Candidate.Width = SpanMax - SpanMin + 1;
    Candidate.MergedCount = 0;
    for (const int32 Index : Plateau) Candidate.MergedCount += Unique[Index].MergedCount;
    Candidate.Key = Unique[RepresentativeIndex].Key;
    Candidate.UpstreamWidth = UpstreamWidth;
    Candidate.DownstreamWidth = DownstreamWidth;
    if (Candidate.Width > Settings.PortalMaxWidthCells)
    {
      ++Summary.PlateauRejectedCount;
      continue;
    }
    LocalMinima.Add(Candidate);
    FCandidate& Added = LocalMinima.Last();
    Added.MergedCount = FMath::Max(Added.MergedCount, 1);
    // Store the two neighbor widths temporarily in fields not used for sorting below.
    Added.Min = Candidate.Min;
    Added.Max = Candidate.Max;
  }
  Summary.LocalMinimumCandidateCount = LocalMinima.Num();
  LocalMinima.Sort([](const FCandidate& A, const FCandidate& B)
  {
    if (A.Width != B.Width) return A.Width < B.Width;
    if (A.Axis != B.Axis) return A.Axis < B.Axis;
    if (A.Cross != B.Cross) return A.Cross < B.Cross;
    return A.Key < B.Key;
  });

  OutPortals.Reset();
  for (const FCandidate& Candidate : LocalMinima)
  {
    bool bMerged = false;
    for (const FCrowdDemoTrafficPortal& Existing : OutPortals)
    {
      if (Existing.Axis == Candidate.Axis
        && FMath::Abs(Existing.CrossSectionCoordinate-Candidate.Cross) <= Settings.PortalMergeDistanceCells
        && !(Candidate.Max < Existing.SpanMin || Candidate.Min > Existing.SpanMax))
      {
        bMerged = true;
        break;
      }
    }
    if (bMerged) continue;
    FCrowdDemoTrafficPortal& Portal = OutPortals.AddDefaulted_GetRef();
    Portal.Axis = Candidate.Axis;
    Portal.CrossSectionCoordinate = Candidate.Cross;
    Portal.SpanMin = Candidate.Min;
    Portal.SpanMax = Candidate.Max;
    Portal.WidthCells = Candidate.Width;
    Portal.Capacity = FMath::Clamp(Candidate.Width-1, 1, 6);
    Portal.StableCellKey = Candidate.Key;
    Portal.MergedCandidateCount = Candidate.MergedCount;
    // Recompute diagnostic neighbor widths around the selected representative.
    Portal.UpstreamWidthCells = Candidate.UpstreamWidth;
    Portal.DownstreamWidthCells = Candidate.DownstreamWidth;
    Portal.PortalId = static_cast<int32>(StableHash(Portal.Axis, Portal.CrossSectionCoordinate, Portal.SpanMin*4096+Portal.SpanMax) & 0x7fffffffu);
    const FVector BoundsMin = FVector(FlowField.Config.BoundsMin);
    const float CrossWorld = (static_cast<float>(Candidate.Cross) + 1.0f) * FlowField.Config.CellSizeCm;
    const float SpanWorld = (0.5f * static_cast<float>(Candidate.Min + Candidate.Max) + 0.5f) * FlowField.Config.CellSizeCm;
    Portal.Center = Candidate.Axis == 0
      ? FVector2f(BoundsMin.X + CrossWorld, BoundsMin.Y + SpanWorld)
      : FVector2f(BoundsMin.X + SpanWorld, BoundsMin.Y + CrossWorld);
  }
  OutPortals.Sort([](const FCrowdDemoTrafficPortal& A, const FCrowdDemoTrafficPortal& B) { return A.PortalId < B.PortalId; });
  Summary.ExtractedPortalCount = OutPortals.Num();
  for (const FCrowdDemoTrafficPortal& Portal : OutPortals)
  {
    Summary.GeometryHash = TrafficHashInt(Summary.GeometryHash, Portal.PortalId);
    Summary.GeometryHash = TrafficHashInt(Summary.GeometryHash, Portal.Axis);
    Summary.GeometryHash = TrafficHashInt(Summary.GeometryHash, Portal.CrossSectionCoordinate);
    Summary.GeometryHash = TrafficHashInt(Summary.GeometryHash, Portal.SpanMin);
    Summary.GeometryHash = TrafficHashInt(Summary.GeometryHash, Portal.SpanMax);
  }
  if (OutSummary) *OutSummary = Summary;
}

void FCrowdDemoTrafficSchedulingKernel::BuildCandidates(
  const TConstArrayView<FCrowdDemoTrafficAgent> Agents,
  const TConstArrayView<FCrowdDemoTrafficPortalRuntime> Portals,
  const FCrowdDemoTrafficSettings& Settings,
  TArray<FCrowdDemoPortalCandidate>& OutCandidates,
  FCrowdDemoPortalCandidateBuildSummary* OutSummary)
{
  OutCandidates.Reset();
  FCrowdDemoPortalCandidateBuildSummary Summary;
  const float ApproachDistance = Settings.PortalApproachDepthCells * Settings.CellSizeCm;
  TArray<FCrowdDemoTrafficAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const FCrowdDemoTrafficAgent& A, const FCrowdDemoTrafficAgent& B)
  {
    return A.AgentId < B.AgentId;
  });
  TMap<int32, int32> LockedCountByPortal;
  for (const FCrowdDemoTrafficAgent& Agent : SortedAgents)
  {
    if (Agent.PreviousPortalId != INDEX_NONE
      && (Agent.AdmissionState == ECrowdDemoPortalAdmissionState::Approach
        || Agent.AdmissionState == ECrowdDemoPortalAdmissionState::Waiting))
    {
      ++LockedCountByPortal.FindOrAdd(Agent.PreviousPortalId);
    }
  }
  for (const FCrowdDemoTrafficAgent& Agent : SortedAgents)
  {
    if (Agent.AdmissionState != ECrowdDemoPortalAdmissionState::None
      && Agent.AdmissionState != ECrowdDemoPortalAdmissionState::Approach
      && Agent.AdmissionState != ECrowdDemoPortalAdmissionState::Waiting)
    {
      continue;
    }
    const FCrowdDemoTrafficPortalRuntime* Best = nullptr;
    float BestDistance = MAX_flt;
    int32 DirectionKey = Agent.PreviousDirectionKey;
    bool bRebinding = false;
    if (Agent.PreviousPortalId != INDEX_NONE)
    {
      const FCrowdDemoTrafficPortalRuntime* Locked = Portals.FindByPredicate(
        [&](const FCrowdDemoTrafficPortalRuntime& Portal)
        {
          return Portal.Portal.PortalId == Agent.PreviousPortalId;
        });
      if (Locked && DirectionKey != 0)
      {
        const FVector2f Axis = Locked->Portal.Axis == 0 ? FVector2f(1,0) : FVector2f(0,1);
        const FVector2f PortalDirection = Axis * static_cast<float>(DirectionKey);
        const FVector2f Delta = Agent.Position - Locked->Portal.Center;
        const float SignedAxial = FVector2f::DotProduct(Delta, PortalDirection);
        const float Lateral = FMath::Abs(FVector2f::DotProduct(Delta, FVector2f(-PortalDirection.Y, PortalDirection.X)));
        const int32 LockedCount = LockedCountByPortal.FindRef(Agent.PreviousPortalId);
        const float Spacing = Agent.RadiusCm * 2.0f + Settings.HoldingGapCm;
        const float ReleaseDepth = FMath::Max(ApproachDistance,
          static_cast<float>(FMath::DivideAndRoundUp(FMath::Max(1, LockedCount), 2)) * Spacing) + Spacing;
        const float HalfSpan = Locked->Portal.WidthCells * Settings.CellSizeCm * 0.5f;
        const bool bDirectionValid = FVector2f::DotProduct(QuantizedDirection(Agent.FlowDirection), PortalDirection) > 0.0f;
        const bool bInsideEnvelope = SignedAxial >= -ReleaseDepth
          && SignedAxial <= Settings.PortalExitDepthCells * Settings.CellSizeCm
          && Lateral <= HalfSpan + 2.0f * Spacing;
        if (bDirectionValid && bInsideEnvelope)
        {
          Best = Locked;
          BestDistance = FMath::Abs(SignedAxial);
        }
      }
      if (!Best)
      {
        ++Summary.ReleaseCount;
      }
    }
    const bool bPortalLocked = Best != nullptr;
    for (const FCrowdDemoTrafficPortalRuntime& Portal : Portals)
    {
      if (bPortalLocked) break;
      const FVector2f Axis = Portal.Portal.Axis == 0 ? FVector2f(1,0) : FVector2f(0,1);
      const float AxisFlow = FVector2f::DotProduct(QuantizedDirection(Agent.FlowDirection), Axis);
      if (FMath::Abs(AxisFlow) <= KINDA_SMALL_NUMBER)
      {
        ++Summary.InvalidSideCandidateCount;
        continue;
      }
      const int32 CandidateDirectionKey = AxisFlow >= 0.0f ? 1 : -1;
      const FVector2f PortalDirection = Axis * static_cast<float>(CandidateDirectionKey);
      const FVector2f Delta = Agent.Position - Portal.Portal.Center;
      const float SignedAxial = FVector2f::DotProduct(Delta, PortalDirection);
      if (SignedAxial > 0.0f || SignedAxial < -ApproachDistance)
      {
        ++Summary.InvalidSideCandidateCount;
        continue;
      }
      const float Lateral = FMath::Abs(FVector2f::DotProduct(Delta, FVector2f(-PortalDirection.Y, PortalDirection.X)));
      const float HalfSpan = Portal.Portal.WidthCells * Settings.CellSizeCm * 0.5f;
      if (Lateral > HalfSpan + Agent.RadiusCm)
      {
        ++Summary.WrongSpanCandidateCount;
        continue;
      }
      const float Distance = FMath::Abs(SignedAxial);
      if (Distance < BestDistance || (Distance == BestDistance
        && (!Best || Portal.Portal.PortalId < Best->Portal.PortalId)))
      {
        Best = &Portal;
        BestDistance = Distance;
        DirectionKey = CandidateDirectionKey;
      }
    }
    if (!Best) continue;
    if (Agent.PreviousPortalId != INDEX_NONE && Agent.PreviousPortalId != Best->Portal.PortalId)
    {
      bRebinding = true;
      ++Summary.RebindCount;
    }
    else if (Agent.PreviousPortalId == INDEX_NONE)
    {
      ++Summary.BindCount;
    }
    FCrowdDemoPortalCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
    Candidate.AgentId = Agent.AgentId;
    Candidate.CohortId = Agent.CohortId;
    Candidate.PortalId = Best->Portal.PortalId;
    Candidate.DirectionKey = DirectionKey;
    Candidate.WaitSteps = Agent.WaitSteps;
    Candidate.WaitEpoch = Settings.WaitEpochSteps > 0 ? Agent.WaitSteps / Settings.WaitEpochSteps : 0;
    Candidate.DistanceBucket = FMath::RoundToInt(BestDistance / 10.0f);
    Candidate.FlowDirection = QuantizedDirection(Agent.FlowDirection);
    const FVector2f Axis = Best->Portal.Axis == 0 ? FVector2f(1,0) : FVector2f(0,1);
    Candidate.PortalDirection = Axis * static_cast<float>(DirectionKey);
    const FVector2f Delta = Agent.Position - Best->Portal.Center;
    Candidate.SignedAxialDistanceCm = FVector2f::DotProduct(Delta, Candidate.PortalDirection);
    Candidate.LateralDistanceCm = FVector2f::DotProduct(
      Delta, FVector2f(-Candidate.PortalDirection.Y, Candidate.PortalDirection.X));
    Candidate.bNewBinding = Agent.PreviousPortalId == INDEX_NONE;
    Candidate.bRebinding = bRebinding;
  }
  OutCandidates.Sort([](const FCrowdDemoPortalCandidate& A, const FCrowdDemoPortalCandidate& B)
  {
    if (A.PortalId != B.PortalId) return A.PortalId < B.PortalId;
    if (A.WaitEpoch != B.WaitEpoch) return A.WaitEpoch > B.WaitEpoch;
    if (A.DistanceBucket != B.DistanceBucket) return A.DistanceBucket < B.DistanceBucket;
    return A.AgentId < B.AgentId;
  });
  if (OutSummary) *OutSummary = Summary;
}

void FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
  TArray<FCrowdDemoTrafficPortalRuntime>& InOutPortals,
  const TConstArrayView<FCrowdDemoTrafficAgent> Agents,
  const TConstArrayView<FCrowdDemoPortalCandidate> Candidates,
  const FCrowdDemoTrafficSettings& Settings,
  const int32 FixedStepIndex,
  TArray<FCrowdDemoPortalDecision>& OutDecisions,
  FCrowdDemoTrafficStepSummary& OutSummary)
{
  OutDecisions.Reset();
  OutSummary = FCrowdDemoTrafficStepSummary();
  OutSummary.PortalCount = InOutPortals.Num();
  uint32 DecisionHash = 2166136261u;
  TArray<FCrowdDemoTrafficAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const FCrowdDemoTrafficAgent& A, const FCrowdDemoTrafficAgent& B)
  {
    return A.AgentId < B.AgentId;
  });
  InOutPortals.Sort([](const FCrowdDemoTrafficPortalRuntime& A, const FCrowdDemoTrafficPortalRuntime& B)
  {
    return A.Portal.PortalId < B.Portal.PortalId;
  });
  for (FCrowdDemoTrafficPortalRuntime& Portal : InOutPortals)
  {
    TArray<const FCrowdDemoPortalCandidate*> Queue;
    for (const FCrowdDemoPortalCandidate& Candidate : Candidates)
      if (Candidate.PortalId == Portal.Portal.PortalId) Queue.Add(&Candidate);
    OutSummary.QueuedCount += Queue.Num();
    Portal.OccupiedCount = 0;
    Portal.ReservedCount = 0;
    if (Portal.ClearanceStepsRemaining > 0)
    {
      --Portal.ClearanceStepsRemaining;
    }

    auto FillPersistentDecision = [&](const FCrowdDemoTrafficAgent& Agent)
    {
      FCrowdDemoPortalDecision& Decision = OutDecisions.AddDefaulted_GetRef();
      Decision.AgentId = Agent.AgentId;
      Decision.PortalId = Agent.PreviousPortalId;
      Decision.DirectionKey = Agent.PreviousDirectionKey;
      Decision.DirectionEpoch = Agent.PreviousDirectionEpoch;
      Decision.BandId = Agent.PreviousBandId;
      Decision.State = Agent.AdmissionState;
      Decision.TokenGrantedStep = Agent.TokenGrantedStep;
      Decision.EnteredPortalStep = Agent.EnteredPortalStep;
      Decision.LastTransitionStep = Agent.LastTransitionStep;
      const FVector2f Axis = Portal.Portal.Axis == 0 ? FVector2f(1,0) : FVector2f(0,1);
      Decision.PortalDirection = Axis * static_cast<float>(Agent.PreviousDirectionKey);
      const float Axial = Portal.Portal.Axis == 0
        ? Agent.Position.X - Portal.Portal.Center.X
        : Agent.Position.Y - Portal.Portal.Center.Y;
      const float SignedAxial = Axial * static_cast<float>(Agent.PreviousDirectionKey);
      const bool bEpochInvalid = Agent.PreviousDirectionEpoch != Portal.DirectionEpoch
        || Agent.PreviousDirectionKey != Portal.ActiveDirectionKey;
      if (Decision.State == ECrowdDemoPortalAdmissionState::Reserved)
      {
        const bool bTimeout = Agent.TokenGrantedStep != INDEX_NONE
          && FixedStepIndex - Agent.TokenGrantedStep >= Settings.ReservationTimeoutSteps;
        if (bEpochInvalid || bTimeout)
        {
          Decision.State = ECrowdDemoPortalAdmissionState::Waiting;
          Decision.TokenGrantedStep = INDEX_NONE;
          Decision.EnteredPortalStep = INDEX_NONE;
          Decision.LastTransitionStep = FixedStepIndex;
          Decision.bReservationTimedOut = bTimeout;
          OutSummary.ReservationTimeoutCount += bTimeout ? 1 : 0;
        }
        else if (FMath::Abs(Axial) <= Settings.CellSizeCm * 0.5f)
        {
          Decision.State = ECrowdDemoPortalAdmissionState::Inside;
          Decision.EnteredPortalStep = FixedStepIndex;
          Decision.LastTransitionStep = FixedStepIndex;
          ++OutSummary.ReservedToInsideCount;
        }
      }
      if (Decision.State == ECrowdDemoPortalAdmissionState::Inside)
      {
        const bool bTimeout = Agent.EnteredPortalStep != INDEX_NONE
          && FixedStepIndex - Agent.EnteredPortalStep >= Settings.TransitTimeoutSteps;
        const bool bExited = SignedAxial >= Settings.PortalExitDepthCells * Settings.CellSizeCm;
        if (bEpochInvalid || bTimeout || bExited)
        {
          Decision.State = ECrowdDemoPortalAdmissionState::Exited;
          Decision.TokenGrantedStep = INDEX_NONE;
          Decision.EnteredPortalStep = INDEX_NONE;
          Decision.LastTransitionStep = FixedStepIndex;
          Decision.bTransitTimedOut = bTimeout;
          OutSummary.TransitTimeoutCount += bTimeout ? 1 : 0;
          ++OutSummary.InsideToExitedCount;
        }
      }
      if (Decision.State == ECrowdDemoPortalAdmissionState::Exited)
      {
        if (Decision.LastTransitionStep < FixedStepIndex)
        {
          Decision.State = ECrowdDemoPortalAdmissionState::None;
          Decision.PortalId = INDEX_NONE;
          Decision.DirectionKey = 0;
          Decision.DirectionEpoch = 0;
          Decision.BandId = INDEX_NONE;
          Decision.LastTransitionStep = FixedStepIndex;
        }
      }
      Portal.ReservedCount += Decision.State == ECrowdDemoPortalAdmissionState::Reserved ? 1 : 0;
      Portal.OccupiedCount += Decision.State == ECrowdDemoPortalAdmissionState::Inside ? 1 : 0;
    };
    for (const FCrowdDemoTrafficAgent& Agent : SortedAgents)
    {
      if (Agent.PreviousPortalId == Portal.Portal.PortalId
        && (Agent.AdmissionState == ECrowdDemoPortalAdmissionState::Reserved
          || Agent.AdmissionState == ECrowdDemoPortalAdmissionState::Inside
          || Agent.AdmissionState == ECrowdDemoPortalAdmissionState::Exited))
      {
        FillPersistentDecision(Agent);
      }
    }
    Portal.GreenSteps++;
    if (!Queue.IsEmpty() && Portal.ActiveDirectionKey == 0)
      Portal.ActiveDirectionKey = Queue[0]->DirectionKey;
    bool bHasActiveDirection = false;
    bool bHasOpposite = false;
    for (const FCrowdDemoPortalCandidate* Candidate : Queue)
    {
      bHasActiveDirection |= Candidate->DirectionKey == Portal.ActiveDirectionKey;
      bHasOpposite |= Candidate->DirectionKey != Portal.ActiveDirectionKey;
    }
    if (Portal.OccupiedCount == 0 && Portal.ReservedCount == 0
      && Portal.GreenSteps >= Settings.MinGreenSteps
      && ((!bHasActiveDirection && bHasOpposite) || (bHasOpposite && Portal.GreenSteps >= Settings.MaxGreenSteps)))
    {
      const FCrowdDemoPortalCandidate* BestOpposite = nullptr;
      for (const FCrowdDemoPortalCandidate* Candidate : Queue)
        if (Candidate->DirectionKey != Portal.ActiveDirectionKey && !BestOpposite) BestOpposite = Candidate;
      if (BestOpposite)
      {
        Portal.ActiveDirectionKey = BestOpposite->DirectionKey;
        ++Portal.DirectionEpoch;
        Portal.GreenSteps = 0;
        Portal.ClearanceStepsRemaining = Settings.ClearanceSteps;
        ++OutSummary.DirectionEpochChangeCount;
      }
    }
    int32 Available = Portal.ClearanceStepsRemaining == 0
      ? FMath::Max(0, Portal.Portal.Capacity-Portal.OccupiedCount-Portal.ReservedCount) : 0;
    for (const FCrowdDemoPortalCandidate* Candidate : Queue)
    {
      const FCrowdDemoTrafficAgent* Agent = SortedAgents.FindByPredicate(
        [&](const FCrowdDemoTrafficAgent& Value) { return Value.AgentId == Candidate->AgentId; });
      if (!Agent)
      {
        continue;
      }
      FCrowdDemoPortalDecision& Decision = OutDecisions.AddDefaulted_GetRef();
      Decision.AgentId = Candidate->AgentId;
      Decision.PortalId = Portal.Portal.PortalId;
      Decision.DirectionKey = Candidate->DirectionKey;
      Decision.DirectionEpoch = Portal.DirectionEpoch;
      Decision.PortalDirection = Candidate->PortalDirection;
      Decision.bBound = Candidate->bNewBinding;
      Decision.bRebound = Candidate->bRebinding;
      Decision.TokenGrantedStep = Agent->TokenGrantedStep;
      Decision.EnteredPortalStep = Agent->EnteredPortalStep;
      Decision.LastTransitionStep = Agent->LastTransitionStep;
      if (Agent->AdmissionState == ECrowdDemoPortalAdmissionState::None)
      {
        Decision.State = ECrowdDemoPortalAdmissionState::Approach;
        Decision.LastTransitionStep = FixedStepIndex;
      }
      else if (Agent->AdmissionState == ECrowdDemoPortalAdmissionState::Approach)
      {
        Decision.State = ECrowdDemoPortalAdmissionState::Waiting;
        Decision.LastTransitionStep = FixedStepIndex;
      }
      else
      {
        Decision.bGranted = Candidate->DirectionKey == Portal.ActiveDirectionKey && Available > 0;
        Decision.State = Decision.bGranted ? ECrowdDemoPortalAdmissionState::Reserved : ECrowdDemoPortalAdmissionState::Waiting;
      }
      if (Decision.bGranted)
      {
        --Available;
        ++Portal.ReservedCount;
        ++OutSummary.AdmissionGrantedCount;
        Decision.TokenGrantedStep = FixedStepIndex;
        Decision.LastTransitionStep = FixedStepIndex;
      }
      else if (Agent->AdmissionState == ECrowdDemoPortalAdmissionState::Waiting)
      {
        ++OutSummary.AdmissionDeniedCount;
      }
      const int32 BandCount = FMath::Max(1, FMath::Min(Settings.MaxBandCount, Portal.Portal.Capacity));
      const bool bNeedsBand = Agent->PreviousBandId == INDEX_NONE
        || Agent->PreviousPortalId != Decision.PortalId
        || Agent->PreviousDirectionEpoch != Decision.DirectionEpoch;
      Decision.BandId = bNeedsBand
        ? static_cast<int16>(StableHash(Decision.PortalId, Decision.DirectionEpoch, Decision.AgentId) % BandCount)
        : Agent->PreviousBandId;
      Decision.bBandAssigned = bNeedsBand && Agent->PreviousBandId == INDEX_NONE;
      Decision.bBandReassigned = bNeedsBand && Agent->PreviousBandId != INDEX_NONE;
      OutSummary.BandAssignmentCount += Decision.bBandAssigned ? 1 : 0;
      OutSummary.BandReassignmentCount += Decision.bBandReassigned ? 1 : 0;
      DecisionHash = TrafficHashInt(DecisionHash, Decision.AgentId);
      DecisionHash = TrafficHashInt(DecisionHash, Decision.PortalId);
      DecisionHash = TrafficHashInt(DecisionHash, Decision.DirectionEpoch);
      DecisionHash = TrafficHashInt(DecisionHash, Decision.BandId);
      DecisionHash = TrafficHashInt(DecisionHash, Decision.bGranted ? 1 : 0);
    }
    if (Portal.OccupiedCount + Portal.ReservedCount > Portal.Portal.Capacity)
    {
      ++OutSummary.CapacityViolationCount;
    }
    OutSummary.OccupiedCount += Portal.OccupiedCount;
  }
  OutDecisions.Sort([](const FCrowdDemoPortalDecision& A, const FCrowdDemoPortalDecision& B)
  {
    return A.AgentId < B.AgentId;
  });
  OutSummary.PortalDecisionHash = TrafficHashInt(DecisionHash, FixedStepIndex);
}

void FCrowdDemoTrafficSchedulingKernel::BuildHoldingTargets(
  const TConstArrayView<FCrowdDemoTrafficAgent> Agents,
  const TConstArrayView<FCrowdDemoTrafficPortalRuntime> Portals,
  const FCrowdDemoSharedFlowField& FlowField,
  const FCrowdDemoTrafficSettings& Settings,
  TArray<FCrowdDemoPortalDecision>& InOutDecisions,
  FCrowdDemoTrafficStepSummary& InOutSummary)
{
  TMap<int32, const FCrowdDemoTrafficAgent*> AgentById;
  for (const FCrowdDemoTrafficAgent& Agent : Agents) AgentById.Add(Agent.AgentId, &Agent);
  struct FAssignedTarget { FVector2f Position=FVector2f::ZeroVector; float RadiusCm=0.0f; };
  TArray<FAssignedTarget> Assigned;
  for (const FCrowdDemoTrafficPortalRuntime& Portal : Portals)
  {
    for (const int32 DirectionKey : { -1, 1 })
    {
      TArray<FCrowdDemoPortalDecision*> Waiting;
      for (FCrowdDemoPortalDecision& Decision : InOutDecisions)
      {
        if (Decision.PortalId == Portal.Portal.PortalId && Decision.DirectionKey == DirectionKey
          && Decision.State == ECrowdDemoPortalAdmissionState::Waiting)
        {
          Waiting.Add(&Decision);
        }
      }
      Waiting.Sort([&](const FCrowdDemoPortalDecision& A, const FCrowdDemoPortalDecision& B)
      {
        const FCrowdDemoTrafficAgent* const* AgentA = AgentById.Find(A.AgentId);
        const FCrowdDemoTrafficAgent* const* AgentB = AgentById.Find(B.AgentId);
        const int32 EpochA = AgentA ? (*AgentA)->WaitSteps / FMath::Max(1, Settings.WaitEpochSteps) : 0;
        const int32 EpochB = AgentB ? (*AgentB)->WaitSteps / FMath::Max(1, Settings.WaitEpochSteps) : 0;
        if (EpochA != EpochB) return EpochA > EpochB;
        const float DistanceA = AgentA ? FVector2f::Distance((*AgentA)->Position, Portal.Portal.Center) : MAX_flt;
        const float DistanceB = AgentB ? FVector2f::Distance((*AgentB)->Position, Portal.Portal.Center) : MAX_flt;
        const int32 BucketA = FMath::RoundToInt(DistanceA / 10.0f);
        const int32 BucketB = FMath::RoundToInt(DistanceB / 10.0f);
        if (BucketA != BucketB) return BucketA < BucketB;
        return A.AgentId < B.AgentId;
      });
      float MaxRadius = 0.0f;
      for (const FCrowdDemoPortalDecision* Decision : Waiting)
      {
        if (const FCrowdDemoTrafficAgent* const* Agent = AgentById.Find(Decision->AgentId))
          MaxRadius = FMath::Max(MaxRadius, (*Agent)->RadiusCm);
      }
      const float Spacing = FMath::Max(1.0f, 2.0f * MaxRadius + Settings.HoldingGapCm);
      const float CenterLaneHalfWidth = MaxRadius + 0.5f * Settings.HoldingGapCm;
      const FVector2f Axis = Portal.Portal.Axis == 0 ? FVector2f(1,0) : FVector2f(0,1);
      const FVector2f PortalDirection = Axis * static_cast<float>(DirectionKey);
      const FVector2f Perpendicular(-PortalDirection.Y, PortalDirection.X);
      for (FCrowdDemoPortalDecision* Decision : Waiting)
      {
        const FCrowdDemoTrafficAgent* const* FoundAgent = AgentById.Find(Decision->AgentId);
        if (!FoundAgent) continue;
        const FCrowdDemoTrafficAgent& Agent = **FoundAgent;
        bool bAssigned = false;
        const int32 MaxRows = FMath::Max(4, Waiting.Num() + Settings.PortalApproachDepthCells * 2);
        for (int32 Row = 0; Row < MaxRows && !bAssigned; ++Row)
        {
          const float AxialDistance = (static_cast<float>(Row) + 1.0f) * Spacing;
          for (int32 Lane = 0; Lane <= Waiting.Num() && !bAssigned; ++Lane)
          {
            const float LateralMagnitude = CenterLaneHalfWidth + Agent.RadiusCm
              + Settings.HoldingGapCm + static_cast<float>(Lane) * Spacing;
            for (const int32 Side : { -1, 1 })
            {
              const FVector2f Target = Portal.Portal.Center - PortalDirection * AxialDistance
                + Perpendicular * (static_cast<float>(Side) * LateralMagnitude);
              const int32 CellIndex = FlowField.LocationToCellIndex(FVector(Target.X, Target.Y, 0.0f));
              if (!FlowField.Blocked.IsValidIndex(CellIndex) || FlowField.Blocked[CellIndex]
                || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
                  FlowField.Config, FVector(Target.X, Target.Y, 0.0f)))
              {
                continue;
              }
              bool bOverlaps = false;
              for (const FAssignedTarget& Existing : Assigned)
              {
                if (FVector2f::Distance(Target, Existing.Position)
                  < Agent.RadiusCm + Existing.RadiusCm + Settings.HoldingGapCm)
                {
                  bOverlaps = true;
                  break;
                }
              }
              if (bOverlaps) continue;
              Decision->HoldingTarget = Target;
              Decision->bHasHoldingTarget = true;
              Assigned.Add({Target, Agent.RadiusCm});
              ++InOutSummary.HoldingTargetCount;
              bAssigned = true;
              break;
            }
          }
        }
        if (!bAssigned) ++InOutSummary.HoldingTargetAllocationFailureCount;
      }
    }
  }
  for (int32 A = 0; A < Assigned.Num(); ++A)
  {
    for (int32 B = A + 1; B < Assigned.Num(); ++B)
    {
      if (FVector2f::Distance(Assigned[A].Position, Assigned[B].Position)
        < Assigned[A].RadiusCm + Assigned[B].RadiusCm)
      {
        ++InOutSummary.HoldingTargetOverlapCount;
      }
    }
  }
}

FVector2f FCrowdDemoTrafficSchedulingKernel::ApplyDensityAndBandGuidance(
  const FCrowdDemoTrafficAgent& Agent,
  const FCrowdDemoTrafficCell* Cell,
  const FCrowdDemoTrafficPortalRuntime* Portal,
  FCrowdDemoPortalDecision* Decision,
  const FCrowdDemoTrafficSettings& Settings,
  const float MaxSpeedCmps)
{
  float Scale = 1.0f;
  if (Cell && Cell->AgentCount > Settings.DensityComfortCount)
  {
    const float T = FMath::Clamp(
      static_cast<float>(Cell->AgentCount-Settings.DensityComfortCount)
        / FMath::Max(1, Settings.DensitySaturationCount-Settings.DensityComfortCount), 0.0f, 1.0f);
    Scale = FMath::Lerp(1.0f, Settings.DensityMinimumSpeedScale, T);
  }
  if (Decision && Decision->State == ECrowdDemoPortalAdmissionState::Waiting)
  {
    if (Decision->bHasHoldingTarget)
    {
      const FVector2f Error = Decision->HoldingTarget - Agent.Position;
      const float Distance = Error.Size();
      if (Distance <= Settings.HoldingStopToleranceCm) return FVector2f::ZeroVector;
      const float RawSpeed = FMath::Min(MaxSpeedCmps, Distance * Settings.HoldingTargetGainPerSecond);
      const float SlowdownRange = FMath::Max(
        Settings.HoldingStopToleranceCm + 0.01f, Settings.HoldingSlowdownDistanceCm);
      const float Slowdown = FMath::Clamp(
        (Distance - Settings.HoldingStopToleranceCm)
          / (SlowdownRange - Settings.HoldingStopToleranceCm), 0.0f, 1.0f);
      return QuantizedDirection(Error) * (RawSpeed * Slowdown);
    }
    return QuantizedDirection(Agent.FlowDirection)
      * (MaxSpeedCmps * FMath::Min(Scale, Settings.DensityMinimumSpeedScale));
  }
  const bool bClearing = Decision
    && (Decision->State == ECrowdDemoPortalAdmissionState::Reserved
      || Decision->State == ECrowdDemoPortalAdmissionState::Inside);
  const float PreferredScale = bClearing
    ? FMath::Max(Scale, Settings.PortalClearingMinimumSpeedScale)
    : Scale;
  FVector2f PreferredDirection = QuantizedDirection(Agent.FlowDirection);
  if (bClearing && Decision && !Decision->PortalDirection.IsNearlyZero())
  {
    PreferredDirection = QuantizedDirection(Decision->PortalDirection);
  }
  FVector2f Preferred = PreferredDirection * (MaxSpeedCmps * PreferredScale);
  if (Portal && Decision && Decision->BandId != INDEX_NONE)
  {
    const int32 BandCount = FMath::Max(1, FMath::Min(Settings.MaxBandCount, Portal->Portal.Capacity));
    const float TargetOffset = (static_cast<float>(Decision->BandId)
      - 0.5f * static_cast<float>(BandCount-1)) * Settings.BandSpacingCm;
    const FVector2f PortalDirection = Decision->PortalDirection.IsNearlyZero()
      ? QuantizedDirection(Agent.FlowDirection)
      : QuantizedDirection(Decision->PortalDirection);
    const FVector2f Perpendicular(-PortalDirection.Y, PortalDirection.X);
    const float CurrentOffset = FVector2f::DotProduct(Agent.Position - Portal->Portal.Center, Perpendicular);
    const float LateralError = TargetOffset - CurrentOffset;
    Decision->BandLateralErrorCm = LateralError;
    const float LateralVelocity = FMath::Clamp(
      LateralError * Settings.BandLateralGainPerSecond,
      -Settings.BandLateralSpeedCmps, Settings.BandLateralSpeedCmps);
    Preferred += Perpendicular * LateralVelocity;
  }
  const float Size = Preferred.Size();
  return Size > MaxSpeedCmps ? Preferred*(MaxSpeedCmps/Size) : Preferred;
}
