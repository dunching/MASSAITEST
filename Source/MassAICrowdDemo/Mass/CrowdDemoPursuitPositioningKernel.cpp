#include "Mass/CrowdDemoPursuitPositioningKernel.h"
#include "Algo/Unique.h"

namespace
{
  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    return Hash * 16777619u;
  }

  int32 CyclicSectorDistance(const int32 A, const int32 B, const int32 Count)
  {
    if (A == INDEX_NONE || B == INDEX_NONE || Count <= 0) return 0;
    const int32 Delta = FMath::Abs(A - B);
    return FMath::Min(Delta, Count - Delta);
  }
}

void FCrowdDemoPursuitPositioningKernel::BuildCandidates(
  const FCrowdDemoPursuitTargetFact& Target,
  const float AgentRadiusCm,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const FCrowdDemoSharedFlowField& FlowField,
  TArray<FCrowdDemoPositionCandidate>& OutCandidates,
  FCrowdDemoPositioningSummary& OutSummary)
{
  OutCandidates.Reset();
  OutSummary = {};
  if (!FlowField.IsValid() || Target.TargetId == INDEX_NONE) return;
  const float Quantum = FMath::Max(0.1f, Settings.PositionQuantumCm);
  const float MinimumSpacing = 2.0f * AgentRadiusCm + Settings.SafetyGapCm;
  FCrowdDemoSharedFlowFieldConfig ClearanceConfig = FlowField.Config;
  ClearanceConfig.AgentInflateCm = AgentRadiusCm + Settings.SafetyGapCm;
  TArray<FCrowdDemoPositionCandidate> Raw;
  int32 InnermostPreferredRadialBand = MAX_int32;
  for (int32 CellIndex = 0; CellIndex < FlowField.Width * FlowField.Height; ++CellIndex)
  {
    if (!FlowField.IntegrationCost.IsValidIndex(CellIndex)
      || FlowField.Blocked[CellIndex] || FlowField.Unreachable[CellIndex])
    {
      continue;
    }
    const FVector Center3 = FlowField.CellCenter(CellIndex);
    const FVector2f Center(Center3.X, Center3.Y);
    const FVector2f Local = Center - Target.Location;
    const float SurfaceDistance = Local.Size() - Target.RadiusCm - AgentRadiusCm;
    if (SurfaceDistance < Settings.AllowedDistanceMinCm
      || SurfaceDistance > Settings.AllowedDistanceMaxCm)
    {
      continue;
    }
    const bool bClearance = !FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
      ClearanceConfig, Center3);
    if (!bClearance) continue;
    FCrowdDemoPositionCandidate& Candidate = Raw.AddDefaulted_GetRef();
    Candidate.TargetId = Target.TargetId;
    Candidate.LocalOffset = FVector2f(
      FMath::RoundToFloat(Local.X / Quantum) * Quantum,
      FMath::RoundToFloat(Local.Y / Quantum) * Quantum);
    Candidate.WorldLocation = Target.Location + Candidate.LocalOffset;
    Candidate.StableCellKey = CellIndex;
    Candidate.RadialBand = FMath::FloorToInt(
      SurfaceDistance / FMath::Max(1.0f, FlowField.Config.CellSizeCm));
    if (SurfaceDistance >= Settings.PreferredDistanceMinCm
      && SurfaceDistance <= Settings.PreferredDistanceMaxCm)
    {
      InnermostPreferredRadialBand = FMath::Min(
        InnermostPreferredRadialBand, Candidate.RadialBand);
    }
    float Angle = FMath::Atan2(Local.Y, Local.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    Candidate.AngularSector = FMath::Clamp(FMath::FloorToInt(
      Angle / (2.0f * PI) * FMath::Max(1, Settings.AngularSectorCount)),
      0, FMath::Max(0, Settings.AngularSectorCount - 1));
    Candidate.Capacity = 1;
    Candidate.bReachable = true;
    Candidate.bClearanceValid = true;
  }
  if (InnermostPreferredRadialBand == MAX_int32)
  {
    return;
  }
  Raw.RemoveAll([InnermostPreferredRadialBand](const FCrowdDemoPositionCandidate& Candidate)
  {
    return Candidate.RadialBand < InnermostPreferredRadialBand;
  });
  for (FCrowdDemoPositionCandidate& Candidate : Raw)
  {
    Candidate.Role = Candidate.RadialBand == InnermostPreferredRadialBand
      ? ECrowdDemoPositionRole::Front
      : ECrowdDemoPositionRole::Reserve;
    // StableCellKey is collision free inside the shared field. TargetId and
    // TargetRevision scope the candidate set, so a lossy 31-bit hash is not a
    // valid ownership identity here.
    Candidate.PositionId = Candidate.StableCellKey;
  }
  Raw.Sort([](const FCrowdDemoPositionCandidate& A, const FCrowdDemoPositionCandidate& B)
  {
    if (A.Role != B.Role) return A.Role < B.Role;
    if (A.RadialBand != B.RadialBand) return A.RadialBand < B.RadialBand;
    if (A.AngularSector != B.AngularSector) return A.AngularSector < B.AngularSector;
    return A.StableCellKey < B.StableCellKey;
  });
  for (const FCrowdDemoPositionCandidate& Candidate : Raw)
  {
    bool bOverlaps = false;
    for (const FCrowdDemoPositionCandidate& Accepted : OutCandidates)
    {
      if ((Candidate.WorldLocation - Accepted.WorldLocation).SizeSquared()
        < FMath::Square(MinimumSpacing))
      {
        bOverlaps = true;
        break;
      }
    }
    if (!bOverlaps) OutCandidates.Add(Candidate);
  }
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoPositionCandidate& Candidate : OutCandidates)
  {
    ++OutSummary.CandidateCount;
    OutSummary.FrontCapacity += Candidate.Role == ECrowdDemoPositionRole::Front ? 1 : 0;
    OutSummary.ReserveCapacity += Candidate.Role == ECrowdDemoPositionRole::Reserve ? 1 : 0;
    OutSummary.CandidateUnreachableCount += !Candidate.bReachable || !Candidate.bClearanceValid ? 1 : 0;
    Hash = Fold(Hash, static_cast<uint32>(Candidate.PositionId));
    Hash = Fold(Hash, static_cast<uint32>(Candidate.StableCellKey));
    Hash = Fold(Hash, static_cast<uint32>(Candidate.Role));
  }
  OutSummary.CandidateHash = Hash;
}

void FCrowdDemoPursuitPositioningKernel::Assign(
  const TConstArrayView<FCrowdDemoPositioningAgent> Agents,
  const TConstArrayView<FCrowdDemoPositionCandidate> Candidates,
  const FCrowdDemoPursuitTargetFact& Target,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  TArray<FCrowdDemoPositionAssignment>& OutAssignments,
  FCrowdDemoPositioningSummary& OutSummary)
{
  struct FProposal { int32 CandidateIndex; int32 Cost; bool bExistingOwner; };
  struct FWorkingAgent { FCrowdDemoPositioningAgent Agent; TArray<FProposal> Proposals; int32 Next = 0; };
  TArray<FCrowdDemoPositionCandidate> SortedCandidates(Candidates);
  SortedCandidates.Sort([](const auto& A, const auto& B){ return A.PositionId < B.PositionId; });
  TArray<FWorkingAgent> Working;
  for (const FCrowdDemoPositioningAgent& Agent : Agents)
  {
    FWorkingAgent& Item = Working.AddDefaulted_GetRef();
    Item.Agent = Agent;
    for (int32 Index = 0; Index < SortedCandidates.Num(); ++Index)
    {
      const FCrowdDemoPositionCandidate& Candidate = SortedCandidates[Index];
      if (!Candidate.bReachable || !Candidate.bClearanceValid || Candidate.Capacity != 1) continue;
      const bool bExisting = Agent.ExistingPositionId == Candidate.PositionId;
      const int32 Travel = FMath::RoundToInt((Candidate.WorldLocation - Agent.Location).Size());
      const float SurfaceDistance = Candidate.LocalOffset.Size() - Target.RadiusCm - Agent.RadiusCm;
      int32 RangePenalty = 0;
      if (SurfaceDistance < Settings.PreferredDistanceMinCm)
        RangePenalty = FMath::RoundToInt(Settings.PreferredDistanceMinCm - SurfaceDistance);
      else if (SurfaceDistance > Settings.PreferredDistanceMaxCm)
        RangePenalty = FMath::RoundToInt(SurfaceDistance - Settings.PreferredDistanceMaxCm);
      const int32 SectorPenalty = CyclicSectorDistance(
        Agent.PreferredApproachSector, Candidate.AngularSector,
        Settings.AngularSectorCount) * Settings.ApproachSectorChangePenalty;
      int32 Cost = Travel + RangePenalty + SectorPenalty;
      Cost += Candidate.Role == ECrowdDemoPositionRole::Reserve ? Settings.ReserveRolePenalty : 0;
      if (Agent.ExistingPositionId != INDEX_NONE && !bExisting) Cost += Settings.ReassignmentPenalty;
      if (bExisting) Cost -= Settings.ExistingAssignmentReuseBonus;
      Item.Proposals.Add({Index, Cost, bExisting});
    }
    Item.Proposals.Sort([&](const FProposal& A, const FProposal& B)
    {
      if (A.Cost != B.Cost) return A.Cost < B.Cost;
      if (A.bExistingOwner != B.bExistingOwner) return A.bExistingOwner;
      return SortedCandidates[A.CandidateIndex].PositionId < SortedCandidates[B.CandidateIndex].PositionId;
    });
  }
  Working.Sort([](const FWorkingAgent& A, const FWorkingAgent& B)
  { return A.Agent.AgentId < B.Agent.AgentId; });
  TArray<int32> HeldBy;
  HeldBy.Init(INDEX_NONE, SortedCandidates.Num());
  TArray<int32> AssignedCandidate;
  AssignedCandidate.Init(INDEX_NONE, Working.Num());
  for (int32 Round = 0; Round < FMath::Max(1, Settings.MaxAssignmentProposalRounds); ++Round)
  {
    bool bProposed = false;
    for (int32 AgentIndex = 0; AgentIndex < Working.Num(); ++AgentIndex)
    {
      if (AssignedCandidate[AgentIndex] != INDEX_NONE
        || Working[AgentIndex].Next >= Working[AgentIndex].Proposals.Num()) continue;
      bProposed = true;
      const FProposal Proposal = Working[AgentIndex].Proposals[Working[AgentIndex].Next++];
      const int32 HeldAgent = HeldBy[Proposal.CandidateIndex];
      bool bWins = HeldAgent == INDEX_NONE;
      if (!bWins)
      {
        const FProposal* HeldProposal = Working[HeldAgent].Proposals.FindByPredicate(
          [&](const FProposal& P){ return P.CandidateIndex == Proposal.CandidateIndex; });
        check(HeldProposal);
        if (Proposal.Cost != HeldProposal->Cost) bWins = Proposal.Cost < HeldProposal->Cost;
        else if (Proposal.bExistingOwner != HeldProposal->bExistingOwner) bWins = Proposal.bExistingOwner;
        else bWins = Working[AgentIndex].Agent.AgentId < Working[HeldAgent].Agent.AgentId;
      }
      if (bWins)
      {
        if (HeldAgent != INDEX_NONE) AssignedCandidate[HeldAgent] = INDEX_NONE;
        HeldBy[Proposal.CandidateIndex] = AgentIndex;
        AssignedCandidate[AgentIndex] = Proposal.CandidateIndex;
      }
    }
    if (!bProposed) break;
  }
  // Promotion is a second deterministic batch over the completed matching.
  // It does not mutate occupancy while proposals are being evaluated.
  int32 PromotionCount = 0;
  for (int32 FrontIndex = 0; FrontIndex < SortedCandidates.Num(); ++FrontIndex)
  {
    if (SortedCandidates[FrontIndex].Role != ECrowdDemoPositionRole::Front
      || HeldBy[FrontIndex] != INDEX_NONE)
    {
      continue;
    }
    int32 BestAgentIndex = INDEX_NONE;
    int32 BestCost = MAX_int32;
    for (int32 AgentIndex = 0; AgentIndex < Working.Num(); ++AgentIndex)
    {
      const int32 CurrentCandidateIndex = AssignedCandidate[AgentIndex];
      if (CurrentCandidateIndex == INDEX_NONE
        || SortedCandidates[CurrentCandidateIndex].Role != ECrowdDemoPositionRole::Reserve)
      {
        continue;
      }
      const FCrowdDemoPositioningAgent& Agent = Working[AgentIndex].Agent;
      const int32 Cost = FMath::RoundToInt(
        (SortedCandidates[FrontIndex].WorldLocation - Agent.Location).Size())
        + CyclicSectorDistance(Agent.PreferredApproachSector,
          SortedCandidates[FrontIndex].AngularSector, Settings.AngularSectorCount)
          * Settings.ApproachSectorChangePenalty;
      if (Cost < BestCost
        || (Cost == BestCost && (BestAgentIndex == INDEX_NONE
          || Agent.AgentId < Working[BestAgentIndex].Agent.AgentId)))
      {
        BestCost = Cost;
        BestAgentIndex = AgentIndex;
      }
    }
    if (BestAgentIndex != INDEX_NONE)
    {
      const int32 PreviousCandidateIndex = AssignedCandidate[BestAgentIndex];
      HeldBy[PreviousCandidateIndex] = INDEX_NONE;
      HeldBy[FrontIndex] = BestAgentIndex;
      AssignedCandidate[BestAgentIndex] = FrontIndex;
      ++PromotionCount;
    }
  }
  OutAssignments.Reset(Working.Num());
  OutSummary.AssignedCount = 0;
  OutSummary.UnassignedCount = 0;
  OutSummary.ReusedCount = 0;
  OutSummary.ChangedCount = 0;
  OutSummary.PromotionCount = PromotionCount;
  uint32 Hash = 2166136261u;
  for (int32 AgentIndex = 0; AgentIndex < Working.Num(); ++AgentIndex)
  {
    FCrowdDemoPositionAssignment& Assignment = OutAssignments.AddDefaulted_GetRef();
    Assignment.AgentId = Working[AgentIndex].Agent.AgentId;
    const int32 CandidateIndex = AssignedCandidate[AgentIndex];
    if (CandidateIndex == INDEX_NONE)
    {
      ++OutSummary.UnassignedCount;
      Hash = Fold(Hash, static_cast<uint32>(Assignment.AgentId));
      Hash = Fold(Hash, static_cast<uint32>(INDEX_NONE));
      continue;
    }
    const FCrowdDemoPositionCandidate& Candidate = SortedCandidates[CandidateIndex];
    Assignment.PositionId = Candidate.PositionId;
    Assignment.Role = Candidate.Role;
    Assignment.State = Candidate.Role == ECrowdDemoPositionRole::Front
      ? ECrowdDemoPursuitPositionState::FrontAssignedWaiting
      : ECrowdDemoPursuitPositionState::ReserveCommit;
    Assignment.DesiredLocation = Candidate.WorldLocation;
    const FProposal* Proposal = Working[AgentIndex].Proposals.FindByPredicate(
      [&](const FProposal& P){ return P.CandidateIndex == CandidateIndex; });
    Assignment.IntegerCost = Proposal ? Proposal->Cost : MAX_int32;
    Assignment.bReused = Working[AgentIndex].Agent.ExistingPositionId == Candidate.PositionId;
    ++OutSummary.AssignedCount;
    OutSummary.ReusedCount += Assignment.bReused ? 1 : 0;
    OutSummary.ChangedCount += !Assignment.bReused
      && Working[AgentIndex].Agent.ExistingPositionId != INDEX_NONE ? 1 : 0;
    Hash = Fold(Hash, static_cast<uint32>(Assignment.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Assignment.PositionId));
    Hash = Fold(Hash, static_cast<uint32>(Assignment.Role));
    Hash = Fold(Hash, static_cast<uint32>(Assignment.IntegerCost));
  }
  OutSummary.AssignmentHash = Hash;
}


void FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(
  const FCrowdDemoHoldingMatchingInput& Input,
  FCrowdDemoHoldingMatchingResult& Out)
{
  Out = {};
  TArray<FCrowdDemoHoldingAgent> Agents = Input.Agents;
  TArray<FCrowdDemoHoldingCandidate> Holdings = Input.Holdings;
  TArray<FCrowdDemoHoldingPositionCompatibility> Edges = Input.Compatibility;
  Agents.Sort([](const auto& A,const auto& B){return A.AgentId<B.AgentId;});
  Holdings.Sort([](const auto& A,const auto& B){return A.HoldingId<B.HoldingId;});
  Edges.Sort([](const auto& A,const auto& B){return A.HoldingId!=B.HoldingId
    ? A.HoldingId<B.HoldingId:A.PositionId<B.PositionId;});
  const auto HoldingById=[&](int32 Id){return Holdings.FindByPredicate(
    [&](const auto& H){return H.HoldingId==Id;});};
  const auto EdgeByIds=[&](int32 H,int32 P){return Edges.FindByPredicate(
    [&](const auto& E){return E.HoldingId==H&&E.PositionId==P;});};
  const auto Hard=[](ECrowdDemoPursuitSteeringState S){return
    S==ECrowdDemoPursuitSteeringState::StableOccupied||
    S==ECrowdDemoPursuitSteeringState::ReserveHold||
    S==ECrowdDemoPursuitSteeringState::Commit;};
  TSet<int32> UsedHoldings, DoneAgents;
  for(const auto& Agent:Agents)
  {
    if(!Hard(Agent.ExistingState)) continue;
    const auto* H=HoldingById(Agent.ExistingHoldingId);
    const auto* E=EdgeByIds(Agent.ExistingHoldingId,Agent.PositionId);
    const bool Valid=Agent.bPositionValid&&Agent.bExistingOwnerHardValid&&H&&E
      &&Agent.ExistingTargetRevision==Input.TargetRevision;
    if(!Valid)
    {
      auto& A=Out.Assignments.AddDefaulted_GetRef(); A.AgentId=Agent.AgentId;
      A.PositionId=Agent.PositionId; A.State=ECrowdDemoPursuitSteeringState::Reacquire;
      ++Out.InvalidHardOwnerCount; ++Out.UnmatchedAgentCount; DoneAgents.Add(Agent.AgentId); continue;
    }
    if(UsedHoldings.Contains(H->HoldingId)){Out.Assignments.Reset();Out.InvalidHardOwnerCount=1;return;}
    UsedHoldings.Add(H->HoldingId);DoneAgents.Add(Agent.AgentId);
    auto& A=Out.Assignments.AddDefaulted_GetRef(); A.AgentId=Agent.AgentId;
    A.HoldingId=H->HoldingId;A.PositionId=Agent.PositionId;A.HoldingLocation=H->WorldLocation;
    A.AssignedPosition=Agent.AssignedPosition;A.State=Agent.ExistingState;
    A.IntegerCost=E->QuantizedRouteCostCm;A.CompatibilityHash=E->StableHash;
    A.bCompatibilityValid=true;A.bReused=true;++Out.HardLockedOwnerCount;++Out.ReusedOwnerCount;
    Out.TotalRouteCost+=FMath::Max(0,E->QuantizedRouteCostCm);
  }
  TArray<FCrowdDemoHoldingAgent> Soft;
  for(const auto& Agent:Agents) if(!DoneAgents.Contains(Agent.AgentId))
  {
    if(Agent.bPositionValid) Soft.Add(Agent);
    else {auto& A=Out.Assignments.AddDefaulted_GetRef();A.AgentId=Agent.AgentId;
      A.PositionId=Agent.PositionId;A.State=ECrowdDemoPursuitSteeringState::Reacquire;
      ++Out.UnmatchedAgentCount;}
  }
  TArray<FCrowdDemoHoldingCandidate> Free;
  for(const auto& H:Holdings)if(!UsedHoldings.Contains(H.HoldingId))Free.Add(H);
  TArray<int32> RankOrder;for(int32 I=0;I<Soft.Num();++I)RankOrder.Add(I);
  RankOrder.Sort([&](int32 A,int32 B){const auto&X=Soft[A];const auto&Y=Soft[B];
    if(X.WaitEpoch!=Y.WaitEpoch)return X.WaitEpoch>Y.WaitEpoch;
    if(X.PositionIngressCost!=Y.PositionIngressCost)return X.PositionIngressCost<Y.PositionIngressCost;
    return X.AgentId<Y.AgentId;});
  TArray<int32> Rank;Rank.SetNumZeroed(Soft.Num());for(int32 I=0;I<RankOrder.Num();++I)Rank[RankOrder[I]]=I;
  int64 MaxRoute=0;for(const auto&E:Edges)if(E.bCompatible)MaxRoute=FMath::Max(MaxRoute,(int64)FMath::Max(0,E.QuantizedRouteCostCm));
  const int64 N=Soft.Num(),RouteTotal=N*MaxRoute,WFair=RouteTotal+1,FairTotal=N*FMath::Max<int64>(0,N-1);
  if(WFair<=0||FairTotal>(TNumericLimits<int64>::Max()-RouteTotal-1)/WFair)return;
  const int64 WChange=FairTotal*WFair+RouteTotal+1;
  struct FArc{int32 To,Rev,Cap,Order;int64 Cost;};
  const int32 Source=0,ABase=1,HBase=ABase+Soft.Num(),Sink=HBase+Free.Num();
  TArray<TArray<FArc>> G;G.SetNum(Sink+1);int32 Order=0;
  auto Add=[&](int32 U,int32 V,int64 C){int32 VR=G[V].Num(),UR=G[U].Num();
    G[U].Add({V,VR,1,Order++,C});G[V].Add({U,UR,0,Order++,-C});};
  for(int32 A=0;A<Soft.Num();++A)Add(Source,ABase+A,(int64)Rank[A]*WFair);
  for(int32 H=0;H<Free.Num();++H)Add(HBase+H,Sink,0);
  for(int32 A=0;A<Soft.Num();++A)for(int32 H=0;H<Free.Num();++H)
  {const auto*E=EdgeByIds(Free[H].HoldingId,Soft[A].PositionId);if(!E||!E->bCompatible)continue;
    int64 Route=FMath::Max(0,E->QuantizedRouteCostCm),Change=Soft[A].ExistingHoldingId==Free[H].HoldingId
      &&Soft[A].ExistingTargetRevision==Input.TargetRevision?0:1;if(Change&&WChange>TNumericLimits<int64>::Max()-Route)return;
    Add(ABase+A,HBase+H,Change*WChange+Route);}
  const int64 Inf=TNumericLimits<int64>::Max()/4;int32 Flow=0;
  for(;;)
  {TArray<int64>D;D.Init(Inf,G.Num());D[Source]=0;TArray<int32>PN,PE;PN.Init(INDEX_NONE,G.Num());PE.Init(INDEX_NONE,G.Num());
    for(int32 Pass=0;Pass<G.Num()-1;++Pass){bool Changed=false;for(int32 U=0;U<G.Num();++U)if(D[U]!=Inf)
      for(int32 E=0;E<G[U].Num();++E){const auto&Arc=G[U][E];if(Arc.Cap<=0)continue;int64 C=D[U]+Arc.Cost;
        bool Tie=C==D[Arc.To]&&(PN[Arc.To]==INDEX_NONE||U<PN[Arc.To]||(U==PN[Arc.To]&&Arc.Order<G[PN[Arc.To]][PE[Arc.To]].Order));
        if(C<D[Arc.To]||Tie){D[Arc.To]=C;PN[Arc.To]=U;PE[Arc.To]=E;Changed=true;}}
      if(!Changed)break;}if(PN[Sink]==INDEX_NONE)break;
    for(int32 V=Sink;V!=Source;V=PN[V]){auto&Arc=G[PN[V]][PE[V]];--Arc.Cap;++G[V][Arc.Rev].Cap;}++Flow;}
  Out.MaximumCardinality=Out.HardLockedOwnerCount+Flow;
  for(int32 A=0;A<Soft.Num();++A)
  {int32 Match=INDEX_NONE;for(int32 E=0;E<G[ABase+A].Num();++E){const auto&Arc=G[ABase+A][E];
      if(Arc.To>=HBase&&Arc.To<Sink&&Arc.Cap==0){Match=Arc.To-HBase;break;}}
    if(Match==INDEX_NONE){auto&X=Out.Assignments.AddDefaulted_GetRef();X.AgentId=Soft[A].AgentId;
      X.PositionId=Soft[A].PositionId;X.State=ECrowdDemoPursuitSteeringState::Reacquire;++Out.UnmatchedAgentCount;continue;}
    const auto&H=Free[Match];const auto*E=EdgeByIds(H.HoldingId,Soft[A].PositionId);auto&X=Out.Assignments.AddDefaulted_GetRef();
    X.AgentId=Soft[A].AgentId;X.HoldingId=H.HoldingId;X.PositionId=Soft[A].PositionId;X.HoldingLocation=H.WorldLocation;
    X.AssignedPosition=Soft[A].AssignedPosition;X.State=ECrowdDemoPursuitSteeringState::Holding;X.IntegerCost=E->QuantizedRouteCostCm;
    X.CompatibilityHash=E->StableHash;X.bCompatibilityValid=true;X.bReused=Soft[A].ExistingHoldingId==H.HoldingId
      &&Soft[A].ExistingTargetRevision==Input.TargetRevision;Out.ReusedOwnerCount+=X.bReused?1:0;
    Out.SoftOwnerMovedCount+=!X.bReused&&Soft[A].ExistingHoldingId!=INDEX_NONE?1:0;Out.TotalRouteCost+=FMath::Max(0,E->QuantizedRouteCostCm);}
  Out.Assignments.Sort([](const auto&A,const auto&B){return A.AgentId<B.AgentId;});uint32 Hash=2166136261u;
  Hash=Fold(Hash,(uint32)Input.TargetRevision);for(const auto&A:Out.Assignments){Hash=Fold(Hash,(uint32)A.AgentId);
    Hash=Fold(Hash,(uint32)A.PositionId);Hash=Fold(Hash,(uint32)A.HoldingId);Hash=Fold(Hash,(uint32)A.State);
  Hash=Fold(Hash,A.CompatibilityHash);Hash=Fold(Hash,A.bReused?1u:0u);}Out.MatchingHash=Hash;Out.bValid=true;
}

void FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallDeficiency(
  const FCrowdDemoHoldingMatchingInput& MatchingInput,
  const TConstArrayView<FCrowdDemoHoldingHallEdge> DiagnosticEdges,
  FCrowdDemoHoldingHallFixture& Out)
{
  Out = {};
  Out.TargetRevision = MatchingInput.TargetRevision;
  TArray<FCrowdDemoHoldingAgent> Agents = MatchingInput.Agents;
  TArray<FCrowdDemoHoldingCandidate> Holdings = MatchingInput.Holdings;
  TArray<FCrowdDemoHoldingHallEdge> Edges(DiagnosticEdges);
  Agents.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  Holdings.Sort([](const auto& A, const auto& B){ return A.HoldingId < B.HoldingId; });
  Edges.Sort([](const auto& A, const auto& B)
  {
    if (A.AgentId != B.AgentId) return A.AgentId < B.AgentId;
    if (A.PositionId != B.PositionId) return A.PositionId < B.PositionId;
    return A.HoldingId < B.HoldingId;
  });
  for (FCrowdDemoHoldingHallEdge& Edge : Edges)
  {
    Edge.StableBlockerAgentIds.Sort();
    Edge.ReserveBlockerAgentIds.Sort();
  }
  const auto IsHard = [](const ECrowdDemoPursuitSteeringState State)
  {
    return State == ECrowdDemoPursuitSteeringState::StableOccupied
      || State == ECrowdDemoPursuitSteeringState::ReserveHold
      || State == ECrowdDemoPursuitSteeringState::Commit;
  };
  const auto IsReleasedClass = [](const ECrowdDemoPursuitSteeringState State,
    const ECrowdDemoPursuitSteeringState ReleasedClass)
  {
    return State == ReleasedClass;
  };
  const auto CardinalityFor = [&](const ECrowdDemoPursuitSteeringState ReleasedClass,
    const bool bGrandfatherReleasedOwner, const bool bIgnoreStableBlockers,
    const bool bIgnoreReserveBlockers, TArray<int32>* OutSoftAgents,
    TArray<int32>* OutFreeHoldings)
  {
    TSet<int32> LockedHoldings;
    int32 LockedCount = 0;
    TArray<const FCrowdDemoHoldingAgent*> SoftAgents;
    for (const FCrowdDemoHoldingAgent& Agent : Agents)
    {
      const bool bHard = IsHard(Agent.ExistingState)
        && !IsReleasedClass(Agent.ExistingState, ReleasedClass);
      if (bHard)
      {
        if (Agent.bPositionValid && Agent.bExistingOwnerHardValid
          && Agent.ExistingTargetRevision == MatchingInput.TargetRevision
          && Agent.ExistingHoldingId != INDEX_NONE
          && !LockedHoldings.Contains(Agent.ExistingHoldingId))
        {
          LockedHoldings.Add(Agent.ExistingHoldingId);
          ++LockedCount;
        }
        continue;
      }
      if (Agent.bPositionValid) SoftAgents.Add(&Agent);
    }
    TArray<int32> FreeHoldingIds;
    for (const FCrowdDemoHoldingCandidate& Holding : Holdings)
      if (!LockedHoldings.Contains(Holding.HoldingId)) FreeHoldingIds.Add(Holding.HoldingId);
    TMap<int32, int32> HoldingOwner;
    TFunction<bool(int32, TSet<int32>&)> Augment = [&](const int32 AgentIndex,
      TSet<int32>& Visited)
    {
      const FCrowdDemoHoldingAgent& Agent = *SoftAgents[AgentIndex];
      for (const int32 HoldingId : FreeHoldingIds)
      {
        if (Visited.Contains(HoldingId)) continue;
        const FCrowdDemoHoldingHallEdge* Edge = Edges.FindByPredicate([&](const auto& E)
        { return E.AgentId == Agent.AgentId && E.PositionId == Agent.PositionId
            && E.HoldingId == HoldingId; });
        const bool bBaseClear = Edge && Edge->bCompatibilityRecordPresent
          && Edge->bFlowClear && Edge->bTargetClear && Edge->bObstacleClear
          && Edge->bRevisionValid
          && (bIgnoreStableBlockers || Edge->StableBlockerAgentIds.IsEmpty())
          && (bIgnoreReserveBlockers || Edge->ReserveBlockerAgentIds.IsEmpty());
        const bool bGrandfathered = Edge && bGrandfatherReleasedOwner
          && Agent.ExistingState == ReleasedClass
          && Agent.bExistingOwnerHardValid
          && Agent.ExistingTargetRevision == MatchingInput.TargetRevision
          && Agent.ExistingHoldingId == HoldingId;
        if (!bBaseClear && !bGrandfathered) continue;
        Visited.Add(HoldingId);
        const int32* Previous = HoldingOwner.Find(HoldingId);
        if (!Previous || Augment(*Previous, Visited))
        {
          HoldingOwner.Add(HoldingId, AgentIndex);
          return true;
        }
      }
      return false;
    };
    int32 SoftMatched = 0;
    for (int32 AgentIndex = 0; AgentIndex < SoftAgents.Num(); ++AgentIndex)
    {
      TSet<int32> Visited;
      SoftMatched += Augment(AgentIndex, Visited) ? 1 : 0;
    }
    if (OutSoftAgents)
    {
      OutSoftAgents->Reset();
      for (const auto* Agent : SoftAgents) OutSoftAgents->Add(Agent->AgentId);
    }
    if (OutFreeHoldings) *OutFreeHoldings = MoveTemp(FreeHoldingIds);
    return LockedCount + SoftMatched;
  };

  Out.Summary.CurrentMatchingCount = CardinalityFor(
    ECrowdDemoPursuitSteeringState::Reacquire, false, false, false, nullptr, nullptr);
  Out.Summary.OwnerReleaseStableMatchingCount = CardinalityFor(
    ECrowdDemoPursuitSteeringState::StableOccupied, true, false, false, nullptr, nullptr);
  Out.Summary.OwnerReleaseReserveMatchingCount = CardinalityFor(
    ECrowdDemoPursuitSteeringState::ReserveHold, true, false, false, nullptr, nullptr);
  Out.Summary.OwnerReleaseCommitMatchingCount = CardinalityFor(
    ECrowdDemoPursuitSteeringState::Commit, true, false, false, nullptr, nullptr);
  Out.Summary.PhysicalStableBlockerRemovalMatchingCount = CardinalityFor(
    ECrowdDemoPursuitSteeringState::Reacquire, false, true, false, nullptr, nullptr);
  Out.Summary.PhysicalReserveBlockerRemovalMatchingCount = CardinalityFor(
    ECrowdDemoPursuitSteeringState::Reacquire, false, false, true, nullptr, nullptr);
  Out.Summary.NoStableOwnerMatchingCount = Out.Summary.OwnerReleaseStableMatchingCount;
  Out.Summary.NoReserveOwnerMatchingCount = Out.Summary.OwnerReleaseReserveMatchingCount;
  Out.Summary.NoCommitOwnerMatchingCount = Out.Summary.OwnerReleaseCommitMatchingCount;
  int32 ValidAgentCount = 0;
  for (const FCrowdDemoHoldingAgent& Agent : Agents) ValidAgentCount += Agent.bPositionValid ? 1 : 0;
  Out.Summary.FullHallDeficiency = FMath::Max(0,
    ValidAgentCount - Out.Summary.CurrentMatchingCount);

  TArray<int32> SoftAgentIds, FreeHoldingIds;
  CardinalityFor(ECrowdDemoPursuitSteeringState::Reacquire, false, false, false,
    &SoftAgentIds, &FreeHoldingIds);
  if (SoftAgentIds.Num() > 20)
  {
    Out.Summary.bValid = false;
    Out.Summary.bExact = false;
    return;
  }
  TArray<int32> BestAgents, BestNeighbors;
  const auto LexLess = [](const TArray<int32>& A, const TArray<int32>& B)
  {
    const int32 Count = FMath::Min(A.Num(), B.Num());
    for (int32 Index = 0; Index < Count; ++Index)
      if (A[Index] != B[Index]) return A[Index] < B[Index];
    return A.Num() < B.Num();
  };
  const uint64 Limit = uint64(1) << SoftAgentIds.Num();
  for (uint64 Mask = 1; Mask < Limit; ++Mask)
  {
    int32 Count = 0;
    for (uint64 Bits = Mask; Bits != 0; Bits >>= 1) Count += static_cast<int32>(Bits & 1u);
    if (!BestAgents.IsEmpty() && Count > BestAgents.Num()) continue;
    TArray<int32> Subset, Neighbors;
    for (int32 Index = 0; Index < SoftAgentIds.Num(); ++Index)
      if ((Mask & (uint64(1) << Index)) != 0) Subset.Add(SoftAgentIds[Index]);
    for (const int32 HoldingId : FreeHoldingIds)
    {
      if (Edges.ContainsByPredicate([&](const auto& Edge)
        { return Subset.Contains(Edge.AgentId) && Edge.HoldingId == HoldingId
            && Edge.bCompatible; }))
      {
        Neighbors.Add(HoldingId);
      }
    }
    if (Neighbors.Num() >= Subset.Num()) continue;
    if (BestAgents.IsEmpty() || Subset.Num() < BestAgents.Num()
      || (Subset.Num() == BestAgents.Num() && LexLess(Subset, BestAgents)))
    {
      BestAgents = MoveTemp(Subset);
      BestNeighbors = MoveTemp(Neighbors);
    }
  }
  Out.AgentIds = BestAgents;
  Out.AvailableHoldingIds = BestNeighbors;
  Out.Summary.HallAgentCount = BestAgents.Num();
  Out.Summary.HallAvailableHoldingCount = BestNeighbors.Num();
  Out.Summary.HallDeficiency = BestAgents.Num() - BestNeighbors.Num();
  for (const FCrowdDemoHoldingHallEdge& Edge : Edges)
  {
    if (!BestAgents.Contains(Edge.AgentId)) continue;
    Out.Edges.Add(Edge);
    Out.Summary.MissingCompatibilityRecordCount += !Edge.bCompatibilityRecordPresent ? 1 : 0;
    Out.Summary.FlowRejectCount += Edge.bCompatibilityRecordPresent && !Edge.bFlowClear ? 1 : 0;
    Out.Summary.TargetRejectCount += Edge.bCompatibilityRecordPresent && !Edge.bTargetClear ? 1 : 0;
    Out.Summary.ObstacleRejectCount += Edge.bCompatibilityRecordPresent && !Edge.bObstacleClear ? 1 : 0;
    Out.Summary.RevisionRejectCount += !Edge.bRevisionValid ? 1 : 0;
    Out.Summary.StableOwnerRejectCount += Edge.StableBlockerAgentIds.IsEmpty() ? 0 : 1;
    Out.Summary.ReserveOwnerRejectCount += Edge.ReserveBlockerAgentIds.IsEmpty() ? 0 : 1;
  }
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, static_cast<uint32>(Out.TargetRevision));
  Hash = Fold(Hash, static_cast<uint32>(Out.Summary.CurrentMatchingCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.Summary.NoStableOwnerMatchingCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.Summary.NoReserveOwnerMatchingCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.Summary.NoCommitOwnerMatchingCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.Summary.FullHallDeficiency));
  Hash = Fold(Hash, static_cast<uint32>(Out.Summary.PhysicalStableBlockerRemovalMatchingCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.Summary.PhysicalReserveBlockerRemovalMatchingCount));
  for (const int32 AgentId : Out.AgentIds) Hash = Fold(Hash, static_cast<uint32>(AgentId));
  for (const int32 HoldingId : Out.AvailableHoldingIds) Hash = Fold(Hash, static_cast<uint32>(HoldingId));
  for (const FCrowdDemoHoldingHallEdge& Edge : Out.Edges)
  {
    Hash = Fold(Hash, static_cast<uint32>(Edge.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Edge.PositionId));
    Hash = Fold(Hash, static_cast<uint32>(Edge.HoldingId));
    Hash = Fold(Hash, Edge.bCompatibilityRecordPresent ? 1u : 0u);
    Hash = Fold(Hash, Edge.bFlowClear ? 1u : 0u);
    Hash = Fold(Hash, Edge.bTargetClear ? 1u : 0u);
    Hash = Fold(Hash, Edge.bObstacleClear ? 1u : 0u);
    Hash = Fold(Hash, Edge.bRevisionValid ? 1u : 0u);
    for (const int32 Id : Edge.StableBlockerAgentIds) Hash = Fold(Hash, static_cast<uint32>(Id));
    for (const int32 Id : Edge.ReserveBlockerAgentIds) Hash = Fold(Hash, static_cast<uint32>(Id));
    Hash = Fold(Hash, Edge.bCompatible ? 1u : 0u);
  }
  Out.Summary.StableHash = Hash;
  Out.Summary.bExact = true;
  Out.Summary.bValid = !BestAgents.IsEmpty() && Out.Summary.HallDeficiency > 0;
}

void FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallGeometry(
  const int32 AgentId,
  const FCrowdDemoPositionCandidate& Position,
  const float AgentRadiusCm,
  const FCrowdDemoPursuitTargetFact& Target,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
  const TConstArrayView<FCrowdDemoPositionIngressBlocker> Blockers,
  const TConstArrayView<FCrowdDemoHoldingPositionCompatibility> FormalCompatibility,
  FCrowdDemoHallGeometryFixture& Out)
{
  Out = {};
  Out.AgentId = AgentId;
  Out.PositionId = Position.PositionId;
  Out.PositionLocation = FVector2f(FMath::RoundToFloat(Position.WorldLocation.X),
    FMath::RoundToFloat(Position.WorldLocation.Y));
  TArray<FCrowdDemoHoldingCandidate> SortedHoldings(Holdings);
  SortedHoldings.Sort([](const auto& A, const auto& B){ return A.HoldingId < B.HoldingId; });
  TArray<FCrowdDemoPositionIngressBlocker> SortedBlockers(Blockers);
  SortedBlockers.Sort([](const auto& A, const auto& B)
  {
    if (A.AgentId != B.AgentId) return A.AgentId < B.AgentId;
    if (A.PositionId != B.PositionId) return A.PositionId < B.PositionId;
    if (A.TargetRevision != B.TargetRevision) return A.TargetRevision < B.TargetRevision;
    if (A.State != B.State) return static_cast<int32>(A.State) < static_cast<int32>(B.State);
    if (A.Location.X != B.Location.X) return A.Location.X < B.Location.X;
    if (A.Location.Y != B.Location.Y) return A.Location.Y < B.Location.Y;
    return A.RadiusCm < B.RadiusCm;
  });
  TSet<int32> SeenBlockerIds;
  TArray<FCrowdDemoPositionIngressBlocker> UniqueBlockers;
  for (const FCrowdDemoPositionIngressBlocker& Blocker : SortedBlockers)
  {
    Out.SelfBlockerCount += Blocker.AgentId == AgentId ? 1 : 0;
    Out.BlockerUsesWitnessPositionCount += Blocker.PositionId == Position.PositionId ? 1 : 0;
    Out.StaleBlockerCount += Blocker.TargetRevision != Target.Revision ? 1 : 0;
    const bool bDuplicate = SeenBlockerIds.Contains(Blocker.AgentId);
    Out.DuplicateBlockerCount += bDuplicate ? 1 : 0;
    if (!bDuplicate) UniqueBlockers.Add(Blocker);
    SeenBlockerIds.Add(Blocker.AgentId);
  }
  Out.HoldingCandidateCount = SortedHoldings.Num();
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, static_cast<uint32>(AgentId));
  Hash = Fold(Hash, static_cast<uint32>(Position.PositionId));
  Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Out.PositionLocation.X)));
  Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Out.PositionLocation.Y)));
  constexpr float MarginEpsilonCm = 0.001f;
  for (const FCrowdDemoHoldingCandidate& Holding : SortedHoldings)
  {
    const FCrowdDemoHoldingPositionCompatibility* Formal =
      FormalCompatibility.FindByPredicate([&](const auto& Edge)
      { return Edge.HoldingId == Holding.HoldingId && Edge.PositionId == Position.PositionId; });
    const bool bTargetRejected = Formal && !Formal->bTargetClear;
    FCrowdDemoHoldingPathBlockerFact Worst;
    Worst.HoldingId = Holding.HoldingId;
    Worst.PositionId = Position.PositionId;
    Worst.ClearanceMarginCm = MAX_flt;
    bool bHasBlocker = false;
    bool bStableRejected = false;
    bool bReserveRejected = false;
    for (const FCrowdDemoPositionIngressBlocker& Blocker : UniqueBlockers)
    {
      if (Blocker.AgentId == AgentId || Blocker.TargetRevision != Target.Revision) continue;
      FCrowdDemoHoldingPathBlockerFact Fact;
      Fact.HoldingId = Holding.HoldingId;
      Fact.PositionId = Position.PositionId;
      Fact.BlockerAgentId = Blocker.AgentId;
      Fact.BlockerPositionId = Blocker.PositionId;
      Fact.BlockerState = Blocker.State;
      Fact.SegmentStart = FVector2f(FMath::RoundToFloat(Holding.WorldLocation.X),
        FMath::RoundToFloat(Holding.WorldLocation.Y));
      Fact.SegmentEnd = Out.PositionLocation;
      Fact.BlockerCenter = FVector2f(FMath::RoundToFloat(Blocker.Location.X),
        FMath::RoundToFloat(Blocker.Location.Y));
      Fact.AgentRadiusCm = FMath::RoundToFloat(AgentRadiusCm);
      Fact.BlockerRadiusCm = FMath::RoundToFloat(Blocker.RadiusCm);
      Fact.SafetyGapCm = FMath::RoundToFloat(Settings.SafetyGapCm);
      Fact.RequiredClearanceCm = Fact.AgentRadiusCm + Fact.BlockerRadiusCm
        + Fact.SafetyGapCm;
      const FVector2f Delta = Fact.SegmentEnd - Fact.SegmentStart;
      const double LengthSquared = static_cast<double>(Delta.SizeSquared());
      double T = 0.0;
      if (LengthSquared > UE_DOUBLE_SMALL_NUMBER)
        T = FMath::Clamp(static_cast<double>(FVector2f::DotProduct(
          Fact.BlockerCenter - Fact.SegmentStart, Delta)) / LengthSquared, 0.0, 1.0);
      Fact.ClosestPointT = static_cast<float>(T);
      const FVector2f Closest = Fact.SegmentStart + Delta * Fact.ClosestPointT;
      Fact.ActualClosestDistanceCm = FMath::Sqrt((Closest - Fact.BlockerCenter).SizeSquared());
      Fact.ClearanceMarginCm = Fact.ActualClosestDistanceCm - Fact.RequiredClearanceCm;
      Fact.bEndpointContact = Fact.ClosestPointT <= MarginEpsilonCm
        || Fact.ClosestPointT >= 1.0f - MarginEpsilonCm;
      Fact.bTargetRejected = bTargetRejected;
      const bool bMarginRejected = Fact.ClearanceMarginCm < -MarginEpsilonCm;
      Fact.bStableRejected = bMarginRejected
        && Blocker.State == ECrowdDemoPursuitPositionState::StableOccupied;
      Fact.bReserveRejected = bMarginRejected
        && Blocker.State == ECrowdDemoPursuitPositionState::ReserveHold;
      const bool bFormalRejected = PositioningSegmentConflictsWithBlocker(
        Fact.SegmentStart, Fact.SegmentEnd, Fact.AgentRadiusCm, Settings, Blocker);
      Fact.bFormalClassificationMismatch = bFormalRejected != bMarginRejected;
      uint32 FactHash = 2166136261u;
      FactHash = Fold(FactHash, static_cast<uint32>(Fact.HoldingId));
      FactHash = Fold(FactHash, static_cast<uint32>(Fact.PositionId));
      FactHash = Fold(FactHash, static_cast<uint32>(Fact.BlockerAgentId));
      FactHash = Fold(FactHash, static_cast<uint32>(Fact.BlockerPositionId));
      FactHash = Fold(FactHash, static_cast<uint32>(Fact.BlockerState));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.SegmentStart.X)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.SegmentStart.Y)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.SegmentEnd.X)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.SegmentEnd.Y)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.BlockerCenter.X)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.BlockerCenter.Y)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.RequiredClearanceCm)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.ClearanceMarginCm * 1000.0f)));
      FactHash = Fold(FactHash, static_cast<uint32>(FMath::RoundToInt(Fact.ClosestPointT * 100000.0f)));
      FactHash = Fold(FactHash, Fact.bTargetRejected ? 1u : 0u);
      FactHash = Fold(FactHash, Fact.bStableRejected ? 1u : 0u);
      FactHash = Fold(FactHash, Fact.bReserveRejected ? 1u : 0u);
      Fact.StableHash = FactHash;
      bStableRejected |= Fact.bStableRejected;
      bReserveRejected |= Fact.bReserveRejected;
      Out.EndpointContactCount += Fact.bEndpointContact && bMarginRejected ? 1 : 0;
      Out.FormalClassificationMismatchCount += Fact.bFormalClassificationMismatch ? 1 : 0;
      Out.RadiusSemanticsErrorCount += !FMath::IsNearlyEqual(Fact.RequiredClearanceCm,
        Fact.AgentRadiusCm + Fact.BlockerRadiusCm + Fact.SafetyGapCm, MarginEpsilonCm) ? 1 : 0;
      if (!bHasBlocker || Fact.ClearanceMarginCm < Worst.ClearanceMarginCm
        || (FMath::IsNearlyEqual(Fact.ClearanceMarginCm, Worst.ClearanceMarginCm, MarginEpsilonCm)
          && Fact.BlockerAgentId < Worst.BlockerAgentId))
      {
        Worst = Fact;
        bHasBlocker = true;
      }
    }
    if (!bHasBlocker) Worst.ClearanceMarginCm = MAX_flt;
    Out.NonNegativeMarginHoldingCount += Worst.ClearanceMarginCm >= -MarginEpsilonCm ? 1 : 0;
    Out.RejectedByStableCount += bStableRejected ? 1 : 0;
    Out.RejectedByReserveCount += bReserveRejected ? 1 : 0;
    Out.RejectedByTargetCount += bTargetRejected ? 1 : 0;
    Out.TargetOnlyRejectCount += bTargetRejected && !bStableRejected && !bReserveRejected ? 1 : 0;
    Out.StableOnlyRejectCount += !bTargetRejected && bStableRejected && !bReserveRejected ? 1 : 0;
    Out.MultiLabelRejectCount += (static_cast<int32>(bTargetRejected)
      + static_cast<int32>(bStableRejected) + static_cast<int32>(bReserveRejected)) > 1 ? 1 : 0;
    if (Out.BestHoldingId == INDEX_NONE || Worst.ClearanceMarginCm > Out.BestClearanceMarginCm
      || (FMath::IsNearlyEqual(Worst.ClearanceMarginCm, Out.BestClearanceMarginCm, MarginEpsilonCm)
        && (Holding.HoldingId < Out.BestHoldingId
          || (Holding.HoldingId == Out.BestHoldingId
            && Worst.BlockerAgentId < Out.BestBlockerAgentId))))
    {
      Out.BestHoldingId = Holding.HoldingId;
      Out.BestClearanceMarginCm = Worst.ClearanceMarginCm;
      Out.BestBlockerAgentId = Worst.BlockerAgentId;
      Out.BestFact = Worst;
    }
    Hash = Fold(Hash, static_cast<uint32>(Holding.HoldingId));
    Hash = Fold(Hash, Worst.StableHash);
  }
  Hash = Fold(Hash, static_cast<uint32>(Out.SelfBlockerCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.DuplicateBlockerCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.StaleBlockerCount));
  Hash = Fold(Hash, static_cast<uint32>(Out.NonNegativeMarginHoldingCount));
  Out.FixtureHash = Hash;
  Out.bValid = Position.PositionId != INDEX_NONE && !SortedHoldings.IsEmpty()
    && Out.BestHoldingId != INDEX_NONE;
}

static void PlanJointHoldingPositionsCore(
  const int32 TargetRevision,
  const TConstArrayView<FCrowdDemoJointPositioningAgent> InputAgents,
  const TConstArrayView<FCrowdDemoHoldingCandidate> InputHoldings,
  const TConstArrayView<FCrowdDemoPositionCandidate> InputPositions,
  const TConstArrayView<FCrowdDemoJointAgentHoldingEdge> InputAgentHoldingEdges,
  const TConstArrayView<FCrowdDemoHoldingPositionCompatibility> InputHoldingPositionEdges,
  FCrowdDemoJointPositioningResult& Out)
{
  Out = {};
  TArray<FCrowdDemoJointPositioningAgent> Agents(InputAgents);
  TArray<FCrowdDemoHoldingCandidate> Holdings(InputHoldings);
  TArray<FCrowdDemoPositionCandidate> Positions(InputPositions);
  TArray<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges(InputAgentHoldingEdges);
  TArray<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges(InputHoldingPositionEdges);
  Agents.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  Holdings.Sort([](const auto& A, const auto& B){ return A.HoldingId < B.HoldingId; });
  Positions.Sort([](const auto& A, const auto& B){ return A.PositionId < B.PositionId; });
  AgentHoldingEdges.Sort([](const auto& A, const auto& B)
  { return A.AgentId != B.AgentId ? A.AgentId < B.AgentId : A.HoldingId < B.HoldingId; });
  HoldingPositionEdges.Sort([](const auto& A, const auto& B)
  { return A.HoldingId != B.HoldingId ? A.HoldingId < B.HoldingId : A.PositionId < B.PositionId; });
  const auto PairKey = [](const int32 A, const int32 B)
  { return (static_cast<uint64>(static_cast<uint32>(A)) << 32) | static_cast<uint32>(B); };
  TMap<uint64, const FCrowdDemoJointAgentHoldingEdge*> AgentHoldingEdgeByKey;
  AgentHoldingEdgeByKey.Reserve(AgentHoldingEdges.Num());
  for (const auto& Edge : AgentHoldingEdges)
    AgentHoldingEdgeByKey.Add(PairKey(Edge.AgentId, Edge.HoldingId), &Edge);
  TMap<uint64, const FCrowdDemoHoldingPositionCompatibility*> HoldingPositionEdgeByKey;
  HoldingPositionEdgeByKey.Reserve(HoldingPositionEdges.Num());
  for (const auto& Edge : HoldingPositionEdges)
    HoldingPositionEdgeByKey.Add(PairKey(Edge.HoldingId, Edge.PositionId), &Edge);
  TMap<int32, const FCrowdDemoJointPositioningAgent*> AgentById;
  AgentById.Reserve(Agents.Num());
  for (const auto& Agent : Agents) AgentById.Add(Agent.AgentId, &Agent);
  const auto IsHard = [](const ECrowdDemoPursuitSteeringState State)
  { return State == ECrowdDemoPursuitSteeringState::StableOccupied
      || State == ECrowdDemoPursuitSteeringState::ReserveHold
      || State == ECrowdDemoPursuitSteeringState::Commit; };
  TSet<int32> UsedHoldings, UsedPositions, HardAgents;
  TMap<int32, int32> ExistingOwnerByHolding;
  for (const FCrowdDemoJointPositioningAgent& Agent : Agents)
    if (Agent.ExistingHoldingId != INDEX_NONE) ExistingOwnerByHolding.Add(Agent.ExistingHoldingId, Agent.AgentId);
  for (const FCrowdDemoJointPositioningAgent& Agent : Agents)
  {
    if (!IsHard(Agent.State)) continue;
    const bool bCandidateValid = Holdings.ContainsByPredicate([&](const auto& H)
      { return H.HoldingId == Agent.ExistingHoldingId; })
      && Positions.ContainsByPredicate([&](const auto& P)
      { return P.PositionId == Agent.ExistingPositionId; });
    if (!Agent.bExistingHardOwnerValid || Agent.TargetRevision != TargetRevision
      || !bCandidateValid || UsedHoldings.Contains(Agent.ExistingHoldingId)
      || UsedPositions.Contains(Agent.ExistingPositionId))
    {
      return;
    }
    UsedHoldings.Add(Agent.ExistingHoldingId);
    UsedPositions.Add(Agent.ExistingPositionId);
    HardAgents.Add(Agent.AgentId);
    auto& Assignment = Out.Assignments.AddDefaulted_GetRef();
    Assignment.AgentId = Agent.AgentId;
    Assignment.HoldingId = Agent.ExistingHoldingId;
    Assignment.PositionId = Agent.ExistingPositionId;
    Assignment.bHardLocked = Assignment.bReusedHolding = Assignment.bReusedPosition = true;
    ++Out.HardLockedCount;
    ++Out.ReusedCombinationCount;
  }
  TArray<FCrowdDemoJointPositioningAgent> SoftAgents;
  for (const auto& Agent : Agents) if (!HardAgents.Contains(Agent.AgentId)) SoftAgents.Add(Agent);
  TArray<FCrowdDemoHoldingCandidate> FreeHoldings;
  for (const auto& Holding : Holdings) if (!UsedHoldings.Contains(Holding.HoldingId)) FreeHoldings.Add(Holding);
  TArray<FCrowdDemoPositionCandidate> FreePositions;
  for (const auto& Position : Positions) if (!UsedPositions.Contains(Position.PositionId)) FreePositions.Add(Position);
  TMap<int32, int32> PositionCompatibilityCount;
  for (const auto& Edge : HoldingPositionEdges)
    if (Edge.bCompatible && !UsedHoldings.Contains(Edge.HoldingId)
      && !UsedPositions.Contains(Edge.PositionId))
      PositionCompatibilityCount.FindOrAdd(Edge.PositionId)++;
  TArray<int32> FairOrder;
  for (int32 Index = 0; Index < SoftAgents.Num(); ++Index) FairOrder.Add(Index);
  FairOrder.Sort([&](const int32 A, const int32 B)
  {
    if (SoftAgents[A].WaitEpoch != SoftAgents[B].WaitEpoch)
      return SoftAgents[A].WaitEpoch > SoftAgents[B].WaitEpoch;
    return SoftAgents[A].AgentId < SoftAgents[B].AgentId;
  });
  TArray<int32> FairRank; FairRank.SetNumZeroed(SoftAgents.Num());
  for (int32 Rank = 0; Rank < FairOrder.Num(); ++Rank) FairRank[FairOrder[Rank]] = Rank;
  struct FLexCost
  {
    int64 V[6] = {0,0,0,0,0,0};
  };
  const auto AddCost = [](const FLexCost& A, const FLexCost& B)
  { FLexCost R; for (int32 I=0; I<6; ++I) R.V[I]=A.V[I]+B.V[I]; return R; };
  const auto NegCost = [](const FLexCost& A)
  { FLexCost R; for (int32 I=0; I<6; ++I) R.V[I]=-A.V[I]; return R; };
  const auto LessCost = [](const FLexCost& A, const FLexCost& B)
  { for (int32 I=0; I<6; ++I) if (A.V[I]!=B.V[I]) return A.V[I]<B.V[I]; return false; };
  struct FArc { int32 To=0, Rev=0, Cap=0, Order=0; FLexCost Cost; };
  const int32 Source=0, AgentBase=1, HoldingInBase=AgentBase+SoftAgents.Num();
  const int32 HoldingOutBase=HoldingInBase+FreeHoldings.Num();
  const int32 PositionBase=HoldingOutBase+FreeHoldings.Num();
  const int32 Sink=PositionBase+FreePositions.Num();
  TArray<TArray<FArc>> Graph; Graph.SetNum(Sink+1); int32 Order=0;
  const auto AddArc = [&](const int32 From, const int32 To, const FLexCost Cost)
  {
    const int32 ForwardIndex=Graph[From].Num(), ReverseIndex=Graph[To].Num();
    Graph[From].Add({To,ReverseIndex,1,Order++,Cost});
    Graph[To].Add({From,ForwardIndex,0,Order++,NegCost(Cost)});
  };
  for (int32 AgentIndex=0; AgentIndex<SoftAgents.Num(); ++AgentIndex)
  { FLexCost C; C.V[1]=FairRank[AgentIndex]; AddArc(Source,AgentBase+AgentIndex,C); }
  for (int32 HoldingIndex=0; HoldingIndex<FreeHoldings.Num(); ++HoldingIndex)
  {
    AddArc(HoldingInBase+HoldingIndex,HoldingOutBase+HoldingIndex,FLexCost());
    for (int32 PositionIndex=0; PositionIndex<FreePositions.Num(); ++PositionIndex)
    {
      const auto* const* EdgePtr=HoldingPositionEdgeByKey.Find(
        PairKey(FreeHoldings[HoldingIndex].HoldingId, FreePositions[PositionIndex].PositionId));
      const auto* Edge=EdgePtr ? *EdgePtr : nullptr;
      if (!Edge || !Edge->bCompatible) continue;
      FLexCost C;
      const int32* ExistingOwner=ExistingOwnerByHolding.Find(FreeHoldings[HoldingIndex].HoldingId);
      const auto* const* OwnerPtr=ExistingOwner ? AgentById.Find(*ExistingOwner) : nullptr;
      const auto* Owner=OwnerPtr ? *OwnerPtr : nullptr;
      C.V[0]=Owner && Owner->ExistingPositionId==FreePositions[PositionIndex].PositionId ? 0 : 1;
      C.V[2]=PositionCompatibilityCount.FindRef(FreePositions[PositionIndex].PositionId);
      C.V[4]=FMath::Max(0,Edge->QuantizedRouteCostCm);
      C.V[5]=static_cast<int64>(HoldingIndex)*(FreePositions.Num()+1)+PositionIndex;
      AddArc(HoldingOutBase+HoldingIndex,PositionBase+PositionIndex,C);
    }
  }
  for (int32 PositionIndex=0; PositionIndex<FreePositions.Num(); ++PositionIndex)
    AddArc(PositionBase+PositionIndex,Sink,FLexCost());
  for (int32 AgentIndex=0; AgentIndex<SoftAgents.Num(); ++AgentIndex)
    for (int32 HoldingIndex=0; HoldingIndex<FreeHoldings.Num(); ++HoldingIndex)
    {
      const auto* const* EdgePtr=AgentHoldingEdgeByKey.Find(
        PairKey(SoftAgents[AgentIndex].AgentId, FreeHoldings[HoldingIndex].HoldingId));
      const auto* Edge=EdgePtr ? *EdgePtr : nullptr;
      if (!Edge || !Edge->bLocallyReachable) continue;
      FLexCost C;
      C.V[0]=SoftAgents[AgentIndex].ExistingHoldingId==FreeHoldings[HoldingIndex].HoldingId ? 0 : 1;
      C.V[3]=FMath::Max(0,Edge->QuantizedCurrentToHoldingCostCm);
      C.V[5]=HoldingIndex;
      AddArc(AgentBase+AgentIndex,HoldingInBase+HoldingIndex,C);
    }
  int32 Flow=0;
  for (;;)
  {
    TArray<FLexCost> Distance; Distance.SetNum(Graph.Num());
    TArray<bool> Reachable; Reachable.Init(false,Graph.Num()); Reachable[Source]=true;
    TArray<int32> PreviousNode,PreviousEdge; PreviousNode.Init(INDEX_NONE,Graph.Num());
    PreviousEdge.Init(INDEX_NONE,Graph.Num());
    TArray<int32> Queue;Queue.Add(Source);int32 QueueHead=0;
    TArray<bool> InQueue;InQueue.Init(false,Graph.Num());InQueue[Source]=true;
    while(QueueHead<Queue.Num())
    {
      const int32 Node=Queue[QueueHead++];InQueue[Node]=false;
      for(int32 EdgeIndex=0;EdgeIndex<Graph[Node].Num();++EdgeIndex)
      {
        const FArc& Arc=Graph[Node][EdgeIndex];if(Arc.Cap<=0)continue;
        const FLexCost Candidate=AddCost(Distance[Node],Arc.Cost);
        if(!Reachable[Arc.To]||LessCost(Candidate,Distance[Arc.To]))
        {Reachable[Arc.To]=true;Distance[Arc.To]=Candidate;PreviousNode[Arc.To]=Node;
          PreviousEdge[Arc.To]=EdgeIndex;if(!InQueue[Arc.To]){Queue.Add(Arc.To);InQueue[Arc.To]=true;}}
      }
    }
    if (!Reachable[Sink]) break;
    for (int32 Node=Sink; Node!=Source; Node=PreviousNode[Node])
    { FArc& Arc=Graph[PreviousNode[Node]][PreviousEdge[Node]];--Arc.Cap;
      ++Graph[Node][Arc.Rev].Cap; }
    ++Flow;
  }
  for (int32 AgentIndex=0; AgentIndex<SoftAgents.Num(); ++AgentIndex)
  {
    int32 HoldingIndex=INDEX_NONE;
    for (const FArc& Arc : Graph[AgentBase+AgentIndex])
      if (Arc.To>=HoldingInBase && Arc.To<HoldingOutBase && Arc.Cap==0)
      { HoldingIndex=Arc.To-HoldingInBase; break; }
    if (HoldingIndex==INDEX_NONE) continue;
    int32 PositionIndex=INDEX_NONE;
    for (const FArc& Arc : Graph[HoldingOutBase+HoldingIndex])
      if (Arc.To>=PositionBase && Arc.To<Sink && Arc.Cap==0)
      { PositionIndex=Arc.To-PositionBase; break; }
    if (PositionIndex==INDEX_NONE) return;
    auto& Assignment=Out.Assignments.AddDefaulted_GetRef();
    Assignment.AgentId=SoftAgents[AgentIndex].AgentId;
    Assignment.HoldingId=FreeHoldings[HoldingIndex].HoldingId;
    Assignment.PositionId=FreePositions[PositionIndex].PositionId;
    Assignment.bReusedHolding=Assignment.HoldingId==SoftAgents[AgentIndex].ExistingHoldingId;
    Assignment.bReusedPosition=Assignment.PositionId==SoftAgents[AgentIndex].ExistingPositionId;
    Out.ReusedCombinationCount += Assignment.bReusedHolding && Assignment.bReusedPosition ? 1 : 0;
  }
  Out.Assignments.Sort([](const auto& A,const auto& B){return A.AgentId<B.AgentId;});
  Out.MaximumCardinality=Out.HardLockedCount+Flow;
  Out.UnmatchedAgentCount=Agents.Num()-Out.MaximumCardinality;
  TSet<int32> FinalHoldings,FinalPositions;uint32 Hash=2166136261u;
  Hash=Fold(Hash,static_cast<uint32>(TargetRevision));
  for(const auto& Assignment:Out.Assignments)
  {
    Out.DuplicateHoldingCount += FinalHoldings.Contains(Assignment.HoldingId)?1:0;
    Out.DuplicatePositionCount += FinalPositions.Contains(Assignment.PositionId)?1:0;
    FinalHoldings.Add(Assignment.HoldingId);FinalPositions.Add(Assignment.PositionId);
    Hash=Fold(Hash,static_cast<uint32>(Assignment.AgentId));
    Hash=Fold(Hash,static_cast<uint32>(Assignment.HoldingId));
    Hash=Fold(Hash,static_cast<uint32>(Assignment.PositionId));
    Hash=Fold(Hash,Assignment.bHardLocked?1u:0u);
  }
  Out.StableHash=Hash;
  Out.bValid=Out.DuplicateHoldingCount==0&&Out.DuplicatePositionCount==0
    &&Out.Assignments.Num()==Out.MaximumCardinality;
}

void FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
  const int32 TargetRevision,
  const TConstArrayView<FCrowdDemoJointPositioningAgent> InputAgents,
  const TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
  const TConstArrayView<FCrowdDemoPositionCandidate> Positions,
  const TConstArrayView<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges,
  const TConstArrayView<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges,
  FCrowdDemoJointPositioningResult& Out)
{
  FCrowdDemoJointPositioningResult Base;
  PlanJointHoldingPositionsCore(TargetRevision,InputAgents,Holdings,Positions,
    AgentHoldingEdges,HoldingPositionEdges,Base);
  if(!Base.bValid){Out=MoveTemp(Base);return;}
  const auto IsHard=[](const ECrowdDemoPursuitSteeringState State)
  {return State==ECrowdDemoPursuitSteeringState::StableOccupied
    ||State==ECrowdDemoPursuitSteeringState::ReserveHold
    ||State==ECrowdDemoPursuitSteeringState::Commit;};
  TArray<FCrowdDemoJointPositioningAgent> Agents(InputAgents);
  Agents.Sort([](const auto&A,const auto&B){return A.AgentId<B.AgentId;});
  TArray<int32> ReusableAgentIndices;
  for(int32 Index=0;Index<Agents.Num();++Index)
  {
    const auto& Agent=Agents[Index];
    if(IsHard(Agent.State)||Agent.ExistingHoldingId==INDEX_NONE
      ||Agent.ExistingPositionId==INDEX_NONE||Agent.TargetRevision!=TargetRevision)continue;
    const bool bAgentEdge=AgentHoldingEdges.ContainsByPredicate([&](const auto&E)
      {return E.AgentId==Agent.AgentId&&E.HoldingId==Agent.ExistingHoldingId&&E.bLocallyReachable;});
    const bool bPositionEdge=HoldingPositionEdges.ContainsByPredicate([&](const auto&E)
      {return E.HoldingId==Agent.ExistingHoldingId&&E.PositionId==Agent.ExistingPositionId&&E.bCompatible;});
    if(bAgentEdge&&bPositionEdge)ReusableAgentIndices.Add(Index);
  }
  if(ReusableAgentIndices.Num()>20){Out={};return;}
  FCrowdDemoJointPositioningResult Best=Base;
  int32 BestForcedReuse=-1;
  uint64 BestMask=0;
  const uint64 Limit=uint64(1)<<ReusableAgentIndices.Num();
  TArray<FCrowdDemoJointPositioningAgent> AllForced=Agents;
  for(const int32 Index:ReusableAgentIndices)
  {AllForced[Index].State=ECrowdDemoPursuitSteeringState::Commit;
    AllForced[Index].bExistingHardOwnerValid=true;}
  FCrowdDemoJointPositioningResult AllForcedResult;
  PlanJointHoldingPositionsCore(TargetRevision,AllForced,Holdings,Positions,
    AgentHoldingEdges,HoldingPositionEdges,AllForcedResult);
  if(AllForcedResult.bValid&&AllForcedResult.MaximumCardinality==Base.MaximumCardinality)
  {Best=MoveTemp(AllForcedResult);BestForcedReuse=ReusableAgentIndices.Num();BestMask=Limit-1;}
  else if(ReusableAgentIndices.Num()>12){Out={};return;}
  else for(uint64 Mask=0;Mask<Limit;++Mask)
  {
    int32 Count=0;for(uint64 Bits=Mask;Bits;Bits>>=1)Count+=static_cast<int32>(Bits&1u);
    if(Count<BestForcedReuse)continue;
    TArray<FCrowdDemoJointPositioningAgent> Modified=Agents;
    for(int32 Bit=0;Bit<ReusableAgentIndices.Num();++Bit)if((Mask&(uint64(1)<<Bit))!=0)
    {auto&A=Modified[ReusableAgentIndices[Bit]];A.State=ECrowdDemoPursuitSteeringState::Commit;
      A.bExistingHardOwnerValid=true;}
    FCrowdDemoJointPositioningResult Candidate;
    PlanJointHoldingPositionsCore(TargetRevision,Modified,Holdings,Positions,
      AgentHoldingEdges,HoldingPositionEdges,Candidate);
    if(!Candidate.bValid||Candidate.MaximumCardinality!=Base.MaximumCardinality)continue;
    if(Count>BestForcedReuse||(Count==BestForcedReuse&&Mask<BestMask))
    {BestForcedReuse=Count;BestMask=Mask;Best=MoveTemp(Candidate);}
  }
  int32 ActualHardCount=0,ActualReuseCount=0;
  for(auto& Assignment:Best.Assignments)
  {
    const auto* Original=Agents.FindByPredicate([&](const auto&A){return A.AgentId==Assignment.AgentId;});
    const bool bOriginallyHard=Original&&IsHard(Original->State);
    Assignment.bHardLocked=bOriginallyHard;
    ActualHardCount+=bOriginallyHard?1:0;
    Assignment.bReusedHolding=Original&&Assignment.HoldingId==Original->ExistingHoldingId;
    Assignment.bReusedPosition=Original&&Assignment.PositionId==Original->ExistingPositionId;
    ActualReuseCount+=Assignment.bReusedHolding&&Assignment.bReusedPosition?1:0;
  }
  Best.HardLockedCount=ActualHardCount;
  Best.ReusedCombinationCount=ActualReuseCount;
  uint32 Hash=2166136261u;Hash=Fold(Hash,static_cast<uint32>(TargetRevision));
  Hash=Fold(Hash,static_cast<uint32>(BestForcedReuse));
  for(const auto&A:Best.Assignments){Hash=Fold(Hash,static_cast<uint32>(A.AgentId));
    Hash=Fold(Hash,static_cast<uint32>(A.HoldingId));Hash=Fold(Hash,static_cast<uint32>(A.PositionId));
    Hash=Fold(Hash,A.bHardLocked?1u:0u);Hash=Fold(Hash,A.bReusedHolding&&A.bReusedPosition?1u:0u);}
  Best.StableHash=Hash;Out=MoveTemp(Best);
}

static int32 ComputeJointCardinalityCore(const int32 TargetRevision,
  TConstArrayView<FCrowdDemoJointPositioningAgent> InputAgents,
  TConstArrayView<FCrowdDemoHoldingCandidate> InputHoldings,
  TConstArrayView<FCrowdDemoPositionCandidate> InputPositions,
  TConstArrayView<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges,
  TConstArrayView<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges,
  int32& OutHardCount)
{
  OutHardCount=0;TArray<FCrowdDemoJointPositioningAgent> Agents(InputAgents);
  TArray<FCrowdDemoHoldingCandidate> Holdings(InputHoldings);
  TArray<FCrowdDemoPositionCandidate> Positions(InputPositions);
  Agents.Sort([](const auto&A,const auto&B){return A.AgentId<B.AgentId;});
  Holdings.Sort([](const auto&A,const auto&B){return A.HoldingId<B.HoldingId;});
  Positions.Sort([](const auto&A,const auto&B){return A.PositionId<B.PositionId;});
  const auto IsHard=[](const ECrowdDemoPursuitSteeringState S)
  {return S==ECrowdDemoPursuitSteeringState::StableOccupied
    ||S==ECrowdDemoPursuitSteeringState::ReserveHold||S==ECrowdDemoPursuitSteeringState::Commit;};
  TSet<int32> UsedH,UsedP;TArray<FCrowdDemoJointPositioningAgent> Soft;
  for(const auto&A:Agents)if(IsHard(A.State))
  {if(!A.bExistingHardOwnerValid||A.TargetRevision!=TargetRevision
      ||UsedH.Contains(A.ExistingHoldingId)||UsedP.Contains(A.ExistingPositionId))return INDEX_NONE;
    UsedH.Add(A.ExistingHoldingId);UsedP.Add(A.ExistingPositionId);++OutHardCount;}
  else Soft.Add(A);
  TArray<int32> FreeH,FreeP;for(const auto&H:Holdings)if(!UsedH.Contains(H.HoldingId))FreeH.Add(H.HoldingId);
  for(const auto&P:Positions)if(!UsedP.Contains(P.PositionId))FreeP.Add(P.PositionId);
  TSet<uint64> CompatibleHpKeys;for(const auto&X:HoldingPositionEdges)if(X.bCompatible)
    CompatibleHpKeys.Add((static_cast<uint64>(static_cast<uint32>(X.HoldingId))<<32)
      |static_cast<uint32>(X.PositionId));
  TSet<uint64> ReachableAhKeys;for(const auto&X:AgentHoldingEdges)if(X.bLocallyReachable)
    ReachableAhKeys.Add((static_cast<uint64>(static_cast<uint32>(X.AgentId))<<32)
      |static_cast<uint32>(X.HoldingId));
  struct E{int32 To,Rev,Cap;};const int32 S=0,AB=1,HIB=AB+Soft.Num(),HOB=HIB+FreeH.Num();
  const int32 PB=HOB+FreeH.Num(),T=PB+FreeP.Num();TArray<TArray<E>> G;G.SetNum(T+1);
  const auto Add=[&](int32 U,int32 V){int32 UR=G[U].Num(),VR=G[V].Num();G[U].Add({V,VR,1});G[V].Add({U,UR,0});};
  for(int32 A=0;A<Soft.Num();++A)Add(S,AB+A);
  for(int32 H=0;H<FreeH.Num();++H){Add(HIB+H,HOB+H);
    for(int32 P=0;P<FreeP.Num();++P)if(CompatibleHpKeys.Contains(
      (static_cast<uint64>(static_cast<uint32>(FreeH[H]))<<32)|static_cast<uint32>(FreeP[P])))Add(HOB+H,PB+P);}
  for(int32 P=0;P<FreeP.Num();++P)Add(PB+P,T);
  for(int32 A=0;A<Soft.Num();++A)for(int32 H=0;H<FreeH.Num();++H)
    if(ReachableAhKeys.Contains((static_cast<uint64>(static_cast<uint32>(Soft[A].AgentId))<<32)
      |static_cast<uint32>(FreeH[H])))Add(AB+A,HIB+H);
  int32 Flow=0;for(;;){TArray<int32>L;L.Init(-1,G.Num());TArray<int32>Q;Q.Add(S);L[S]=0;
    for(int32 Head=0;Head<Q.Num();++Head)for(const auto&X:G[Q[Head]])if(X.Cap>0&&L[X.To]<0){L[X.To]=L[Q[Head]]+1;Q.Add(X.To);}
    if(L[T]<0)break;TArray<int32>It;It.Init(0,G.Num());TFunction<int32(int32)>Dfs=[&](int32 U)
    {if(U==T)return 1;for(int32&I=It[U];I<G[U].Num();++I){auto&X=G[U][I];if(X.Cap>0&&L[X.To]==L[U]+1&&Dfs(X.To))
      {--X.Cap;++G[X.To][X.Rev].Cap;return 1;}}return 0;};while(Dfs(S))++Flow;}
  return OutHardCount+Flow;
}

void FCrowdDemoPursuitPositioningKernel::EvaluateJointCommitResidualProtection(
  const int32 TargetRevision,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FCrowdDemoJointPositioningAgent> InputAgents,
  const TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
  const TConstArrayView<FCrowdDemoPositionCandidate> Positions,
  const TConstArrayView<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges,
  const TConstArrayView<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges,
  const FCrowdDemoJointPositioningResult& JointPlan,
  FCrowdDemoJointCommitResidualResult& Out)
{
  Out = {};
  if (!JointPlan.bValid || JointPlan.MaximumCardinality != InputAgents.Num()) return;
  TArray<FCrowdDemoJointPositioningAgent> Agents(InputAgents);
  Agents.Sort([](const auto& A,const auto& B){return A.AgentId<B.AgentId;});
  TMap<int32,const FCrowdDemoHoldingCandidate*> HoldingById;
  TMap<int32,const FCrowdDemoPositionCandidate*> PositionById;
  for(const auto& H:Holdings)HoldingById.Add(H.HoldingId,&H);
  for(const auto& P:Positions)PositionById.Add(P.PositionId,&P);
  TArray<FCrowdDemoJointPositioningAssignment> Candidates;
  for (const auto& Assignment : JointPlan.Assignments)
    if (!Assignment.bHardLocked) Candidates.Add(Assignment);
  const auto CompatibilityCountForPosition = [&](const int32 PositionId)
  {
    int32 Count=0;
    for(const auto& Edge:HoldingPositionEdges)
      Count += Edge.PositionId==PositionId&&Edge.bCompatible?1:0;
    return Count;
  };
  Candidates.Sort([&](const auto& A,const auto& B)
  {
    const auto* AA=Agents.FindByPredicate([&](const auto& X){return X.AgentId==A.AgentId;});
    const auto* BA=Agents.FindByPredicate([&](const auto& X){return X.AgentId==B.AgentId;});
    const int32 AW=AA?AA->WaitEpoch:0,BW=BA?BA->WaitEpoch:0;
    if(AW!=BW)return AW>BW;
    const int32 AC=CompatibilityCountForPosition(A.PositionId),BC=CompatibilityCountForPosition(B.PositionId);
    if(AC!=BC)return AC<BC;
    if(A.PositionId!=B.PositionId)return A.PositionId<B.PositionId;
    return A.AgentId<B.AgentId;
  });
  uint32 Hash=2166136261u;
  for(const auto& Candidate:Candidates)
  {
    TArray<FCrowdDemoJointPositioningAgent> ModifiedAgents=Agents;
    auto* CandidateAgent=ModifiedAgents.FindByPredicate(
      [&](const auto& A){return A.AgentId==Candidate.AgentId;});
    const auto* CandidatePosition=Positions.FindByPredicate(
      [&](const auto& P){return P.PositionId==Candidate.PositionId;});
    if(!CandidateAgent||!CandidatePosition)continue;
    CandidateAgent->ExistingHoldingId=Candidate.HoldingId;
    CandidateAgent->ExistingPositionId=Candidate.PositionId;
    CandidateAgent->TargetRevision=TargetRevision;
    CandidateAgent->State=ECrowdDemoPursuitSteeringState::Commit;
    CandidateAgent->bExistingHardOwnerValid=true;
    FCrowdDemoPositionIngressBlocker FutureBlocker;
    FutureBlocker.AgentId=Candidate.AgentId;
    FutureBlocker.PositionId=Candidate.PositionId;
    FutureBlocker.TargetRevision=TargetRevision;
    FutureBlocker.State=ECrowdDemoPursuitPositionState::StableOccupied;
    FutureBlocker.Location=CandidatePosition->WorldLocation;
    FutureBlocker.RadiusCm=CandidateAgent->RadiusCm;
    TArray<FCrowdDemoHoldingPositionCompatibility> ModifiedEdges(HoldingPositionEdges);
    for(auto& Edge:ModifiedEdges)
    {
      if(!Edge.bCompatible)continue;
      const auto* const* Holding=HoldingById.Find(Edge.HoldingId);
      const auto* const* Position=PositionById.Find(Edge.PositionId);
      if(Holding&&Position&&PositioningSegmentConflictsWithBlocker(
        (*Holding)->WorldLocation,(*Position)->WorldLocation,CandidateAgent->RadiusCm,Settings,FutureBlocker))
      {Edge.bStableBlockerClear=false;Edge.bCompatible=false;}
    }
    int32 ReplannedHardCount=0;
    const int32 ReplannedCardinality=ComputeJointCardinalityCore(TargetRevision,
      ModifiedAgents,Holdings,Positions,AgentHoldingEdges,ModifiedEdges,ReplannedHardCount);
    auto& Decision=Out.Decisions.AddDefaulted_GetRef();
    Decision.AgentId=Candidate.AgentId;Decision.HoldingId=Candidate.HoldingId;
    Decision.PositionId=Candidate.PositionId;
    Decision.RemainingAgentCountAfterGrant=Agents.Num()-ReplannedHardCount;
    Decision.ResidualMatchingAfterGrant=ReplannedCardinality-ReplannedHardCount;
    Decision.bGrantFeasible=ReplannedCardinality!=INDEX_NONE
      &&Decision.ResidualMatchingAfterGrant==Decision.RemainingAgentCountAfterGrant;
    Out.FeasibleCount += Decision.bGrantFeasible?1:0;
    Out.InfeasibleCount += Decision.bGrantFeasible?0:1;
    Hash=Fold(Hash,static_cast<uint32>(Decision.AgentId));
    Hash=Fold(Hash,static_cast<uint32>(Decision.HoldingId));
    Hash=Fold(Hash,static_cast<uint32>(Decision.PositionId));
    Hash=Fold(Hash,static_cast<uint32>(Decision.RemainingAgentCountAfterGrant));
    Hash=Fold(Hash,static_cast<uint32>(Decision.ResidualMatchingAfterGrant));
    Hash=Fold(Hash,Decision.bGrantFeasible?1u:0u);
  }
  Out.CandidateCount=Out.Decisions.Num();Out.StableHash=Hash;Out.bValid=true;
}

void FCrowdDemoPursuitPositioningKernel::ApplyJointResidualCommitGate(
  const int32 TargetRevision,const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FCrowdDemoJointPositioningAgent> InputAgents,
  const TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
  const TConstArrayView<FCrowdDemoPositionCandidate> Positions,
  const TConstArrayView<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges,
  const TConstArrayView<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges,
  const FCrowdDemoJointPositioningResult& JointPlan,FCrowdDemoCommitGateResult& Gate,
  FCrowdDemoJointCommitResidualResult& Out)
{
  Out={};if(!JointPlan.bValid||JointPlan.MaximumCardinality!=InputAgents.Num())return;
  TArray<FCrowdDemoJointPositioningAgent> WorkingAgents(InputAgents);
  TArray<FCrowdDemoHoldingPositionCompatibility> WorkingEdges(HoldingPositionEdges);
  TMap<int32,const FCrowdDemoHoldingCandidate*> HoldingById;
  TMap<int32,const FCrowdDemoPositionCandidate*> PositionById;
  for(const auto& H:Holdings)HoldingById.Add(H.HoldingId,&H);
  for(const auto& P:Positions)PositionById.Add(P.PositionId,&P);
  TArray<int32> Granted=Gate.GrantedAgentIds;Granted.Sort();
  uint32 Hash=2166136261u;
  for(const int32 AgentId:Granted)
  {
    const auto* Assignment=JointPlan.Assignments.FindByPredicate(
      [&](const auto&A){return A.AgentId==AgentId;});
    auto* Agent=WorkingAgents.FindByPredicate([&](const auto&A){return A.AgentId==AgentId;});
    const auto* const* PositionPtr=Assignment?PositionById.Find(Assignment->PositionId):nullptr;
    const auto* Position=PositionPtr?*PositionPtr:nullptr;
    if(!Assignment||!Agent||!Position)continue;
    TArray<FCrowdDemoJointPositioningAgent> CandidateAgents=WorkingAgents;
    auto* CandidateAgent=CandidateAgents.FindByPredicate([&](const auto&A){return A.AgentId==AgentId;});
    CandidateAgent->ExistingHoldingId=Assignment->HoldingId;
    CandidateAgent->ExistingPositionId=Assignment->PositionId;
    CandidateAgent->TargetRevision=TargetRevision;
    CandidateAgent->State=ECrowdDemoPursuitSteeringState::Commit;
    CandidateAgent->bExistingHardOwnerValid=true;
    FCrowdDemoPositionIngressBlocker Future;Future.AgentId=AgentId;
    Future.PositionId=Assignment->PositionId;Future.TargetRevision=TargetRevision;
    Future.State=ECrowdDemoPursuitPositionState::StableOccupied;
    Future.Location=Position->WorldLocation;Future.RadiusCm=CandidateAgent->RadiusCm;
    TArray<FCrowdDemoHoldingPositionCompatibility> CandidateEdges=WorkingEdges;
    for(auto& Edge:CandidateEdges)if(Edge.bCompatible)
    {const auto* const* H=HoldingById.Find(Edge.HoldingId);const auto* const* P=PositionById.Find(Edge.PositionId);
      if(H&&P&&PositioningSegmentConflictsWithBlocker((*H)->WorldLocation,(*P)->WorldLocation,
        CandidateAgent->RadiusCm,Settings,Future)){Edge.bStableBlockerClear=false;Edge.bCompatible=false;}}
    int32 ReplannedHardCount=0;
    const int32 ReplannedCardinality=ComputeJointCardinalityCore(TargetRevision,
      CandidateAgents,Holdings,Positions,AgentHoldingEdges,CandidateEdges,ReplannedHardCount);
    FCrowdDemoJointCommitResidualDecision R;R.AgentId=AgentId;R.HoldingId=Assignment->HoldingId;
    R.PositionId=Assignment->PositionId;R.RemainingAgentCountAfterGrant=InputAgents.Num()-ReplannedHardCount;
    R.ResidualMatchingAfterGrant=ReplannedCardinality-ReplannedHardCount;
    R.bGrantFeasible=ReplannedCardinality!=INDEX_NONE&&R.ResidualMatchingAfterGrant==R.RemainingAgentCountAfterGrant;
    Out.Decisions.Add(R);Out.FeasibleCount+=R.bGrantFeasible?1:0;Out.InfeasibleCount+=R.bGrantFeasible?0:1;
    if(R.bGrantFeasible){WorkingAgents=MoveTemp(CandidateAgents);WorkingEdges=MoveTemp(CandidateEdges);}
    else
    {if(auto* D=Gate.Decisions.FindByPredicate([&](const auto& X){return X.AgentId==AgentId;}))
      {D->Decision=ECrowdDemoCommitDecision::Held;
       D->RejectReasonMask|=static_cast<uint32>(ECrowdDemoCommitRejectReason::JointResidualCapacity);}
      Gate.GrantedAgentIds.Remove(AgentId);--Gate.ReadyGrantedCount;++Gate.HeldCount;++Gate.ReadyConflictHeldCount;}
    Hash=Fold(Hash,static_cast<uint32>(R.AgentId));Hash=Fold(Hash,static_cast<uint32>(R.PositionId));
    Hash=Fold(Hash,static_cast<uint32>(R.ResidualMatchingAfterGrant));Hash=Fold(Hash,R.bGrantFeasible?1u:0u);
  }
  Gate.Decisions.Sort([](const auto&A,const auto&B){return A.AgentId<B.AgentId;});
  Gate.GrantedAgentIds.Sort();uint32 GateHash=2166136261u;
  for(const auto&D:Gate.Decisions){GateHash=Fold(GateHash,static_cast<uint32>(D.AgentId));
    GateHash=Fold(GateHash,static_cast<uint32>(D.Decision));
    GateHash=Fold(GateHash,D.RejectReasonMask);GateHash=Fold(GateHash,D.YieldableConflictMask);}
  Gate.DecisionHash=GateHash;
  Out.CandidateCount=Out.Decisions.Num();Out.StableHash=Hash;Out.bValid=true;
}

bool FCrowdDemoPursuitPositioningKernel::SegmentIntersectsSafetyCircle(
  const FVector2f SegmentStart,
  const FVector2f SegmentEnd,
  const FVector2f CircleCenter,
  const float SafetyRadiusCm)
{
  const FVector2f Start(FMath::RoundToFloat(SegmentStart.X), FMath::RoundToFloat(SegmentStart.Y));
  const FVector2f End(FMath::RoundToFloat(SegmentEnd.X), FMath::RoundToFloat(SegmentEnd.Y));
  const FVector2f Center(FMath::RoundToFloat(CircleCenter.X), FMath::RoundToFloat(CircleCenter.Y));
  const double Radius = FMath::Max(0.0, static_cast<double>(FMath::RoundToFloat(SafetyRadiusCm)));
  const FVector2f Delta = End - Start;
  const double LengthSquared = static_cast<double>(Delta.SizeSquared());
  double T = 0.0;
  if (LengthSquared > UE_DOUBLE_SMALL_NUMBER)
  {
    T = FMath::Clamp(static_cast<double>(FVector2f::DotProduct(Center - Start, Delta))
      / LengthSquared, 0.0, 1.0);
  }
  const FVector2f Closest = Start + Delta * static_cast<float>(T);
  return static_cast<double>((Closest - Center).SizeSquared()) <= Radius * Radius;
}

void FCrowdDemoPursuitPositioningKernel::EvaluateIngress(
  const FCrowdDemoPursuitTargetFact& Target,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FCrowdDemoPositionCandidate> Candidates,
  const TConstArrayView<FCrowdDemoPositionIngressAgent> Agents,
  TArray<FCrowdDemoPositionIngressEvaluation>& OutEvaluations,
  FCrowdDemoPositionIngressSummary& OutSummary,
  FCrowdDemoPositionIngressFixture& OutMinimumFixture)
{
  OutEvaluations.Reset();
  OutSummary = {};
  OutMinimumFixture = {};
  TArray<FCrowdDemoPositionIngressAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  TArray<FCrowdDemoPositionCandidate> SortedCandidates(Candidates);
  SortedCandidates.Sort([](const auto& A, const auto& B){ return A.PositionId < B.PositionId; });
  TMap<int32, const FCrowdDemoPositionIngressAgent*> AgentById;
  TMap<int32, const FCrowdDemoPositionCandidate*> CandidateById;
  TMap<int32, const FCrowdDemoPositionIngressAgent*> OwnerByPositionId;
  FVector2f EntryAxis = FVector2f::ZeroVector;
  const FVector2f QuantizedTarget(FMath::RoundToFloat(Target.Location.X), FMath::RoundToFloat(Target.Location.Y));
  for (const auto& Agent : SortedAgents)
  {
    AgentById.Add(Agent.AgentId, &Agent);
    if (Agent.PositionId != INDEX_NONE) OwnerByPositionId.Add(Agent.PositionId, &Agent);
    EntryAxis += FVector2f(FMath::RoundToFloat(Agent.Location.X), FMath::RoundToFloat(Agent.Location.Y))
      - QuantizedTarget;
  }
  EntryAxis = EntryAxis.GetSafeNormal();
  if (EntryAxis.IsNearlyZero()) EntryAxis = FVector2f(0.0f, -1.0f);
  for (const auto& Candidate : SortedCandidates) CandidateById.Add(Candidate.PositionId, &Candidate);

  const auto StatePriority = [](const ECrowdDemoPursuitPositionState State)
  {
    switch (State)
    {
    case ECrowdDemoPursuitPositionState::StableOccupied: return 0;
    case ECrowdDemoPursuitPositionState::ReserveHold: return 1;
    case ECrowdDemoPursuitPositionState::SlotCommit:
    case ECrowdDemoPursuitPositionState::ReserveCommit: return 2;
    default: return 3;
    }
  };
  const auto SectorFor = [&](const FVector2f Offset)
  {
    float Angle = FMath::Atan2(Offset.Y, Offset.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    return FMath::Clamp(FMath::FloorToInt(Angle / (2.0f * PI)
      * FMath::Max(1, Settings.AngularSectorCount)), 0,
      FMath::Max(0, Settings.AngularSectorCount - 1));
  };
  const auto Percentile = [](TArray<float> Values, const float Quantile)
  {
    if (Values.IsEmpty()) return 0.0f;
    Values.Sort();
    return Values[FMath::Clamp(FMath::CeilToInt(Values.Num() * Quantile) - 1, 0, Values.Num() - 1)];
  };
  TArray<float> SectorDeltas, RadialDeltas, PreferredSpeeds, OrcaSpeeds, ObstacleSpeeds, FinalSpeeds;
  uint32 EvaluationHash = 2166136261u;
  for (const FCrowdDemoPositionIngressAgent& Agent : SortedAgents)
  {
    if (Agent.State != ECrowdDemoPursuitPositionState::SlotCommit) continue;
    const FCrowdDemoPositionCandidate* const* AssignedPtr = CandidateById.Find(Agent.PositionId);
    if (!AssignedPtr) continue;
    const FCrowdDemoPositionCandidate& Assigned = **AssignedPtr;
    FCrowdDemoPositionIngressEvaluation& Evaluation = OutEvaluations.AddDefaulted_GetRef();
    Evaluation.AgentId = Agent.AgentId;
    Evaluation.AssignedPositionId = Agent.PositionId;
    const FVector2f Start(FMath::RoundToFloat(Agent.Location.X), FMath::RoundToFloat(Agent.Location.Y));
    const FVector2f End(FMath::RoundToFloat(Assigned.WorldLocation.X), FMath::RoundToFloat(Assigned.WorldLocation.Y));
    Evaluation.DirectPathLengthCm = FMath::RoundToFloat((End - Start).Size());
    const int32 CurrentSector = SectorFor(Start - QuantizedTarget);
    Evaluation.AssignedSectorDelta = static_cast<float>(CyclicSectorDistance(
      CurrentSector, Assigned.AngularSector, Settings.AngularSectorCount));
    Evaluation.AssignedRadialDeltaCm = FMath::RoundToFloat(FMath::Abs(
      (Start - QuantizedTarget).Size() - (End - QuantizedTarget).Size()));
    const float TargetSafetyRadius = Target.RadiusCm + Agent.RadiusCm + Settings.SafetyGapCm;
    Evaluation.bTargetBlocked = SegmentIntersectsSafetyCircle(
      Start, End, QuantizedTarget, TargetSafetyRadius);
    for (const FCrowdDemoPositionIngressAgent& Other : SortedAgents)
    {
      if (Other.AgentId == Agent.AgentId) continue;
      if (Other.State != ECrowdDemoPursuitPositionState::StableOccupied
        && Other.State != ECrowdDemoPursuitPositionState::ReserveHold
        && Other.State != ECrowdDemoPursuitPositionState::SlotCommit
        && Other.State != ECrowdDemoPursuitPositionState::ReserveCommit)
      {
        continue;
      }
      if (!SegmentIntersectsSafetyCircle(Start, End, Other.Location,
        Agent.RadiusCm + Other.RadiusCm + Settings.SafetyGapCm))
      {
        continue;
      }
      FCrowdDemoPositionIngressBlocker& Blocker = Evaluation.DirectBlockers.AddDefaulted_GetRef();
      Blocker.AgentId = Other.AgentId;
      Blocker.State = Other.State;
      Blocker.Location = FVector2f(FMath::RoundToFloat(Other.Location.X), FMath::RoundToFloat(Other.Location.Y));
      Blocker.RadiusCm = FMath::RoundToFloat(Other.RadiusCm);
      if (Other.State == ECrowdDemoPursuitPositionState::StableOccupied)
      {
        Evaluation.bStableBlocked = true;
        ++OutSummary.StableBlockerPairCount;
        const FCrowdDemoPositionCandidate* const* OtherCandidate = CandidateById.Find(Other.PositionId);
        if (OtherCandidate
          && FVector2f::DotProduct((*OtherCandidate)->LocalOffset, EntryAxis)
            > FVector2f::DotProduct(Assigned.LocalOffset, EntryAxis) + 1.0f)
        {
          Evaluation.bIngressOrderInversion = true;
        }
      }
      else if (Other.State == ECrowdDemoPursuitPositionState::ReserveHold)
      {
        Evaluation.bReserveBlocked = true;
        ++OutSummary.ReserveBlockerPairCount;
      }
      else
      {
        Evaluation.bCommitBlocked = true;
        ++OutSummary.CommitBlockerPairCount;
      }
    }
    Evaluation.DirectBlockers.Sort([&](const auto& A, const auto& B)
    {
      const int32 APriority = StatePriority(A.State), BPriority = StatePriority(B.State);
      return APriority != BPriority ? APriority < BPriority : A.AgentId < B.AgentId;
    });
    for (const int32 OtherAgentId : Agent.OrcaConstraintOtherAgentIds)
    {
      const FCrowdDemoPositionIngressAgent* const* Other = AgentById.Find(OtherAgentId);
      if (!Other) { ++Evaluation.OrcaConstraintsFromOther; continue; }
      switch ((*Other)->State)
      {
      case ECrowdDemoPursuitPositionState::StableOccupied: ++Evaluation.OrcaConstraintsFromStable; break;
      case ECrowdDemoPursuitPositionState::ReserveHold: ++Evaluation.OrcaConstraintsFromReserve; break;
      case ECrowdDemoPursuitPositionState::SlotCommit:
      case ECrowdDemoPursuitPositionState::ReserveCommit: ++Evaluation.OrcaConstraintsFromCommit; break;
      default: ++Evaluation.OrcaConstraintsFromOther; break;
      }
    }
    for (const FCrowdDemoPositionCandidate& Alternative : SortedCandidates)
    {
      if (Alternative.Role != ECrowdDemoPositionRole::Front
        || Alternative.PositionId == Assigned.PositionId) continue;
      if (const FCrowdDemoPositionIngressAgent* const* Owner = OwnerByPositionId.Find(Alternative.PositionId))
      {
        if ((*Owner)->AgentId < Agent.AgentId
          && CyclicSectorDistance(Assigned.AngularSector, Alternative.AngularSector,
            Settings.AngularSectorCount) <= FMath::Max(1, Settings.AngularSectorCount / 4))
        {
          Evaluation.bHasSameSideOccupiedFront = true;
        }
        continue;
      }
      bool bBlocked = SegmentIntersectsSafetyCircle(Start, Alternative.WorldLocation,
        QuantizedTarget, TargetSafetyRadius);
      for (const FCrowdDemoPositionIngressAgent& Other : SortedAgents)
      {
        if (bBlocked || Other.AgentId == Agent.AgentId) continue;
        bBlocked = SegmentIntersectsSafetyCircle(Start, Alternative.WorldLocation, Other.Location,
          Agent.RadiusCm + Other.RadiusCm + Settings.SafetyGapCm);
      }
      if (!bBlocked) Evaluation.bHasUnblockedAlternativeFront = true;
    }
    const FVector2f ToAssigned = (End - Start).GetSafeNormal();
    Evaluation.bPbdPushesAway = FVector2f::DotProduct(Agent.PbdCorrection, ToAssigned) < -0.5f;
    Evaluation.bObstaclePushesAway = FVector2f::DotProduct(Agent.ObstacleCorrection, ToAssigned) < -0.5f;
    Evaluation.PreferredSpeedCmps = Agent.PreferredVelocity.Size();
    Evaluation.OrcaSpeedCmps = Agent.OrcaVelocity.Size();
    Evaluation.ObstacleSpeedCmps = Agent.ObstacleVelocity.Size();
    Evaluation.FinalSpeedCmps = Agent.FinalVelocity.Size();
    Evaluation.LowSpeedSteps = Evaluation.FinalSpeedCmps < 10.0f
      ? Agent.PreviousLowSpeedSteps + 1 : 0;

    ++OutSummary.SlotCommitCount;
    OutSummary.SlotCommitErrorOver300Count += Evaluation.DirectPathLengthCm > 300.0f ? 1 : 0;
    OutSummary.DirectPathTargetBlockedCount += Evaluation.bTargetBlocked ? 1 : 0;
    OutSummary.DirectPathStableBlockedCount += Evaluation.bStableBlocked ? 1 : 0;
    OutSummary.DirectPathReserveBlockedCount += Evaluation.bReserveBlocked ? 1 : 0;
    OutSummary.DirectPathCommitBlockedCount += Evaluation.bCommitBlocked ? 1 : 0;
    OutSummary.UnblockedAlternativeFrontCount += Evaluation.bHasUnblockedAlternativeFront ? 1 : 0;
    OutSummary.SameSideAlternativeFrontCount += Evaluation.bHasSameSideOccupiedFront ? 1 : 0;
    OutSummary.NoAlternativeFrontCount += !Evaluation.bHasUnblockedAlternativeFront ? 1 : 0;
    OutSummary.OrcaConstraintsFromStableCount += Evaluation.OrcaConstraintsFromStable;
    OutSummary.OrcaConstraintsFromReserveCount += Evaluation.OrcaConstraintsFromReserve;
    OutSummary.OrcaConstraintsFromCommitCount += Evaluation.OrcaConstraintsFromCommit;
    OutSummary.OrcaConstraintsFromOtherCount += Evaluation.OrcaConstraintsFromOther;
    OutSummary.TargetExclusionCrossingCount += Evaluation.bTargetBlocked ? 1 : 0;
    OutSummary.IngressOrderInversionCount += Evaluation.bIngressOrderInversion ? 1 : 0;
    OutSummary.PbdPushAwayCount += Evaluation.bPbdPushesAway ? 1 : 0;
    OutSummary.ObstaclePushAwayCount += Evaluation.bObstaclePushesAway ? 1 : 0;
    OutSummary.SlotCommitLowSpeedStepsMax = FMath::Max(
      OutSummary.SlotCommitLowSpeedStepsMax, Evaluation.LowSpeedSteps);
    SectorDeltas.Add(Evaluation.AssignedSectorDelta);
    RadialDeltas.Add(Evaluation.AssignedRadialDeltaCm);
    PreferredSpeeds.Add(Evaluation.PreferredSpeedCmps);
    OrcaSpeeds.Add(Evaluation.OrcaSpeedCmps);
    ObstacleSpeeds.Add(Evaluation.ObstacleSpeedCmps);
    FinalSpeeds.Add(Evaluation.FinalSpeedCmps);
    EvaluationHash = Fold(EvaluationHash, static_cast<uint32>(Evaluation.AgentId));
    EvaluationHash = Fold(EvaluationHash, static_cast<uint32>(Evaluation.AssignedPositionId));
    EvaluationHash = Fold(EvaluationHash, Evaluation.bTargetBlocked ? 1u : 0u);
    for (const FCrowdDemoPositionIngressBlocker& Blocker : Evaluation.DirectBlockers)
    {
      EvaluationHash = Fold(EvaluationHash, static_cast<uint32>(Blocker.AgentId));
      EvaluationHash = Fold(EvaluationHash, static_cast<uint32>(Blocker.State));
    }

    FCrowdDemoPositionIngressFixture Fixture;
    Fixture.bValid = true;
    Fixture.Target = Target;
    Fixture.Agent = Agent;
    Fixture.AssignedCandidate = Assigned;
    Fixture.Blockers = Evaluation.DirectBlockers;
    Fixture.ConstraintCount = Agent.OrcaConstraintOtherAgentIds.Num();
    uint32 FixtureHash = 2166136261u;
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(Target.TargetId));
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(Agent.AgentId));
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(Assigned.PositionId));
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Start.X)));
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Start.Y)));
    FixtureHash = Fold(FixtureHash, Evaluation.bTargetBlocked ? 1u : 0u);
    for (const auto& Blocker : Fixture.Blockers)
    {
      FixtureHash = Fold(FixtureHash, static_cast<uint32>(Blocker.AgentId));
      FixtureHash = Fold(FixtureHash, static_cast<uint32>(Blocker.State));
      FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Blocker.Location.X)));
      FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Blocker.Location.Y)));
    }
    TArray<int32> ConstraintIds = Agent.OrcaConstraintOtherAgentIds;
    ConstraintIds.Sort();
    for (const int32 Id : ConstraintIds) FixtureHash = Fold(FixtureHash, static_cast<uint32>(Id));
    Fixture.StableHash = FixtureHash;
    if (!OutMinimumFixture.bValid
      || Fixture.ConstraintCount < OutMinimumFixture.ConstraintCount
      || (Fixture.ConstraintCount == OutMinimumFixture.ConstraintCount
        && Fixture.StableHash < OutMinimumFixture.StableHash))
    {
      OutMinimumFixture = MoveTemp(Fixture);
    }
  }
  OutSummary.AssignedSectorDeltaP50 = Percentile(SectorDeltas, 0.50f);
  OutSummary.AssignedSectorDeltaP95 = Percentile(SectorDeltas, 0.95f);
  OutSummary.AssignedSectorDeltaMax = SectorDeltas.IsEmpty() ? 0.0f : FMath::Max(SectorDeltas);
  OutSummary.AssignedRadialDeltaP50 = Percentile(RadialDeltas, 0.50f);
  OutSummary.AssignedRadialDeltaP95 = Percentile(RadialDeltas, 0.95f);
  OutSummary.AssignedRadialDeltaMax = RadialDeltas.IsEmpty() ? 0.0f : FMath::Max(RadialDeltas);
  OutSummary.SlotCommitPreferredSpeedP95 = Percentile(PreferredSpeeds, 0.95f);
  OutSummary.SlotCommitOrcaSpeedP95 = Percentile(OrcaSpeeds, 0.95f);
  OutSummary.SlotCommitObstacleSpeedP95 = Percentile(ObstacleSpeeds, 0.95f);
  OutSummary.SlotCommitFinalSpeedP95 = Percentile(FinalSpeeds, 0.95f);
  OutSummary.MinimumFixtureHash = OutMinimumFixture.StableHash;
  OutSummary.MinimumFixtureConstraintCount = OutMinimumFixture.ConstraintCount;
  OutSummary.EvaluationHash = EvaluationHash;
}

void FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
  FVector2f EntryAxis,
  const int32 FixedStepIndex,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FCrowdDemoPositionCandidate> Candidates,
  const TConstArrayView<FCrowdDemoFrontAdmissionAgent> Agents,
  FCrowdDemoFrontAdmissionResult& OutResult)
{
  OutResult = {};
  EntryAxis = FVector2f(FMath::RoundToFloat(EntryAxis.X * 32767.0f),
    FMath::RoundToFloat(EntryAxis.Y * 32767.0f)).GetSafeNormal();
  if (EntryAxis.IsNearlyZero()) EntryAxis = FVector2f(0.0f, -1.0f);
  TMap<int32, FCrowdDemoPositionCandidate> CandidateById;
  for (const FCrowdDemoPositionCandidate& Candidate : Candidates)
    CandidateById.Add(Candidate.PositionId, Candidate);
  TArray<FCrowdDemoFrontAdmissionAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  TSet<int32> Requeued;
  for (const FCrowdDemoFrontAdmissionAgent& Agent : SortedAgents)
  {
    if (Agent.Role != ECrowdDemoPositionRole::Front) continue;
    if (Agent.State == ECrowdDemoPursuitPositionState::FrontAssignedWaiting)
      ++OutResult.WaitingCount;
    if (Agent.State != ECrowdDemoPursuitPositionState::FrontCommitGranted
      && Agent.State != ECrowdDemoPursuitPositionState::SlotCommit) continue;
    if (Agent.NoProgressSteps >= Settings.FrontApproachNoProgressTimeoutSteps)
    {
      OutResult.RequeuedAgentIds.Add(Agent.AgentId);
      Requeued.Add(Agent.AgentId);
      ++OutResult.WaitingCount;
    }
    else
    {
      ++OutResult.ActiveCommitCount;
    }
  }
  struct FWaiting
  {
    FCrowdDemoFrontAdmissionAgent Agent;
    FCrowdDemoPositionCandidate Candidate;
    int32 Depth = 0;
    int32 Travel = 0;
  };
  TArray<FWaiting> Waiting, Active;
  for (const FCrowdDemoFrontAdmissionAgent& Agent : SortedAgents)
  {
    if (Agent.Role != ECrowdDemoPositionRole::Front || Requeued.Contains(Agent.AgentId)
      || !Agent.bRouteValid) continue;
    const FCrowdDemoPositionCandidate* Candidate = CandidateById.Find(Agent.PositionId);
    if (!Candidate) continue;
    FWaiting Item;
    Item.Agent = Agent;
    Item.Candidate = *Candidate;
    Item.Depth = FMath::RoundToInt(FVector2f::DotProduct(Candidate->LocalOffset, EntryAxis));
    Item.Travel = FMath::RoundToInt((Candidate->WorldLocation - Agent.Location).Size());
    if (Agent.State == ECrowdDemoPursuitPositionState::FrontAssignedWaiting
      && Item.Travel <= FMath::RoundToInt(Settings.FrontAdmissionHoldRangeCm)) Waiting.Add(Item);
    else if (Agent.State == ECrowdDemoPursuitPositionState::FrontCommitGranted
      || Agent.State == ECrowdDemoPursuitPositionState::SlotCommit) Active.Add(Item);
  }
  Waiting.Sort([](const FWaiting& A, const FWaiting& B)
  {
    if (A.Depth != B.Depth) return A.Depth < B.Depth;
    if (A.Candidate.AngularSector != B.Candidate.AngularSector)
      return A.Candidate.AngularSector < B.Candidate.AngularSector;
    if (A.Candidate.PositionId != B.Candidate.PositionId)
      return A.Candidate.PositionId < B.Candidate.PositionId;
    if (A.Travel != B.Travel) return A.Travel < B.Travel;
    return A.Agent.AgentId < B.Agent.AgentId;
  });
  Active.Sort([](const FWaiting& A, const FWaiting& B){ return A.Agent.AgentId < B.Agent.AgentId; });
  const auto Cross = [](const FVector2f A, const FVector2f B, const FVector2f C)
  {
    return static_cast<double>(B.X - A.X) * static_cast<double>(C.Y - A.Y)
      - static_cast<double>(B.Y - A.Y) * static_cast<double>(C.X - A.X);
  };
  const auto RoutesConflict = [&](const FWaiting& A, const FWaiting& B)
  {
    const float Safety = A.Agent.RadiusCm + B.Agent.RadiusCm + Settings.SafetyGapCm;
    TArray<FVector2f> APoints = A.Agent.RoutePoints;
    TArray<FVector2f> BPoints = B.Agent.RoutePoints;
    if (APoints.Num() < 2) APoints = {A.Agent.Location, A.Candidate.WorldLocation};
    if (BPoints.Num() < 2) BPoints = {B.Agent.Location, B.Candidate.WorldLocation};
    for (int32 AI = 0; AI + 1 < APoints.Num(); ++AI)
    {
      for (int32 BI = 0; BI + 1 < BPoints.Num(); ++BI)
      {
        const FVector2f A0 = APoints[AI], A1 = APoints[AI + 1];
        const FVector2f B0 = BPoints[BI], B1 = BPoints[BI + 1];
        const double C1 = Cross(A0, A1, B0), C2 = Cross(A0, A1, B1);
        const double C3 = Cross(B0, B1, A0), C4 = Cross(B0, B1, A1);
        if ((((C1 > 0.0 && C2 < 0.0) || (C1 < 0.0 && C2 > 0.0))
            && ((C3 > 0.0 && C4 < 0.0) || (C3 < 0.0 && C4 > 0.0)))
          || SegmentIntersectsSafetyCircle(A0, A1, B0, Safety)
          || SegmentIntersectsSafetyCircle(A0, A1, B1, Safety)
          || SegmentIntersectsSafetyCircle(B0, B1, A0, Safety)
          || SegmentIntersectsSafetyCircle(B0, B1, A1, Safety)) return true;
      }
    }
    return false;
  };
  TArray<FWaiting> Selected;
  for (const FWaiting& Item : Waiting)
  {
    bool bConflicts = false;
    for (const FWaiting& Existing : Active) bConflicts |= RoutesConflict(Item, Existing);
    for (const FWaiting& Existing : Selected) bConflicts |= RoutesConflict(Item, Existing);
    if (bConflicts) continue;
    Selected.Add(Item);
    OutResult.GrantedAgentIds.Add(Item.Agent.AgentId);
  }
  OutResult.GrantedAgentIds.Sort();
  OutResult.RequeuedAgentIds.Sort();
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, static_cast<uint32>(Settings.FrontApproachNoProgressTimeoutSteps));
  Hash = Fold(Hash, static_cast<uint32>(OutResult.ActiveCommitCount));
  Hash = Fold(Hash, static_cast<uint32>(OutResult.WaitingCount));
  for (const int32 AgentId : OutResult.GrantedAgentIds)
  {
    Hash = Fold(Hash, 1u);
    Hash = Fold(Hash, static_cast<uint32>(AgentId));
  }
  for (const int32 AgentId : OutResult.RequeuedAgentIds)
  {
    Hash = Fold(Hash, 2u);
    Hash = Fold(Hash, static_cast<uint32>(AgentId));
  }
  OutResult.DecisionHash = Hash;
}

bool FCrowdDemoPursuitPositioningKernel::ShouldComposePositionGuidance(
  const ECrowdDemoPursuitPositionState State,
  const ECrowdDemoFrontApproachPhase ApproachPhase,
  const bool bPortalOwns,
  const float DistanceToAssignedPositionCm,
  const float ComposeRangeCm)
{
  if (bPortalOwns) return false;
  const bool bApproachPhaseOwns =
    (State == ECrowdDemoPursuitPositionState::FrontCommitGranted
      || State == ECrowdDemoPursuitPositionState::SlotCommit)
    && ApproachPhase != ECrowdDemoFrontApproachPhase::None;
  return bApproachPhaseOwns || DistanceToAssignedPositionCm <= ComposeRangeCm;
}

bool FCrowdDemoPursuitPositioningKernel::FrontReservationPathsConflict(
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FVector2f> A,
  const float ARadiusCm,
  const TConstArrayView<FVector2f> B,
  const float BRadiusCm)
{
  const float Safety = ARadiusCm + BRadiusCm + Settings.SafetyGapCm;
  const auto Cross = [](const FVector2f P0, const FVector2f P1, const FVector2f P2)
  {
    return static_cast<double>(P1.X - P0.X) * static_cast<double>(P2.Y - P0.Y)
      - static_cast<double>(P1.Y - P0.Y) * static_cast<double>(P2.X - P0.X);
  };
  for (int32 AI = 0; AI + 1 < A.Num(); ++AI)
    for (int32 BI = 0; BI + 1 < B.Num(); ++BI)
    {
      const double C1 = Cross(A[AI], A[AI + 1], B[BI]);
      const double C2 = Cross(A[AI], A[AI + 1], B[BI + 1]);
      const double C3 = Cross(B[BI], B[BI + 1], A[AI]);
      const double C4 = Cross(B[BI], B[BI + 1], A[AI + 1]);
      if ((((C1 > 0.0 && C2 < 0.0) || (C1 < 0.0 && C2 > 0.0))
          && ((C3 > 0.0 && C4 < 0.0) || (C3 < 0.0 && C4 > 0.0)))
        || SegmentIntersectsSafetyCircle(A[AI], A[AI + 1], B[BI], Safety)
        || SegmentIntersectsSafetyCircle(A[AI], A[AI + 1], B[BI + 1], Safety)
        || SegmentIntersectsSafetyCircle(B[BI], B[BI + 1], A[AI], Safety)
        || SegmentIntersectsSafetyCircle(B[BI], B[BI + 1], A[AI + 1], Safety))
        return true;
    }
  return false;
}

void FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FCrowdDemoFrontPhaseReservationRequest> Requests,
  FCrowdDemoFrontPhaseReservationResult& OutResult)
{
  OutResult = {};
  TArray<FCrowdDemoFrontPhaseReservationRequest> SortedRequests(Requests);
  const auto PhaseRank = [](const ECrowdDemoFrontApproachPhase Phase)
  {
    switch (Phase)
    {
    case ECrowdDemoFrontApproachPhase::RadialCommit: return 3;
    case ECrowdDemoFrontApproachPhase::AngularAlign: return 2;
    case ECrowdDemoFrontApproachPhase::RadialStage: return 1;
    default: return 0;
    }
  };
  SortedRequests.Sort([&](const auto& A, const auto& B)
  {
    const int32 ARank = PhaseRank(A.RequestedPhase);
    const int32 BRank = PhaseRank(B.RequestedPhase);
    if (ARank != BRank) return ARank > BRank;
    if (A.CommitGrantedStep != B.CommitGrantedStep)
      return A.CommitGrantedStep < B.CommitGrantedStep;
    return A.AgentId < B.AgentId;
  });
  TMap<int32, TArray<FVector2f>> OccupiedPaths;
  TMap<int32, float> RadiusByAgentId;
  TMap<int32, bool> OccupiedUsesGrantedPath;
  for (const FCrowdDemoFrontPhaseReservationRequest& Request : SortedRequests)
  {
    RadiusByAgentId.Add(Request.AgentId, Request.RadiusCm);
    if (Request.CurrentReservationPoints.Num() >= 2)
    {
      OccupiedPaths.Add(Request.AgentId, Request.CurrentReservationPoints);
      OccupiedUsesGrantedPath.Add(Request.AgentId, false);
    }
  }
  for (const FCrowdDemoFrontPhaseReservationRequest& Request : SortedRequests)
  {
    if (!Request.bHasRequest) continue;
    if (!Request.bTargetExclusionClear || Request.RequestedPhase == ECrowdDemoFrontApproachPhase::None
      || !Request.bRequestValid || Request.RequestedReservationPoints.Num() < 2)
    {
      OutResult.InvalidAgentIds.Add(Request.AgentId);
      continue;
    }
    bool bConflict = false;
    for (const TPair<int32, TArray<FVector2f>>& Occupied : OccupiedPaths)
    {
      if (Occupied.Key == Request.AgentId) continue;
      if (FrontReservationPathsConflict(Settings, Request.RequestedReservationPoints,
        Request.RadiusCm, Occupied.Value, RadiusByAgentId.FindRef(Occupied.Key)))
      {
        bConflict = true;
        FCrowdDemoFrontPhaseReservationBlockPair& Pair =
          OutResult.BlockingPairs.AddDefaulted_GetRef();
        Pair.RequesterAgentId = Request.AgentId;
        Pair.BlockerAgentId = Occupied.Key;
        Pair.bBlockerGrantedPath = OccupiedUsesGrantedPath.FindRef(Occupied.Key);
      }
    }
    if (bConflict)
    {
      OutResult.HeldAgentIds.Add(Request.AgentId);
      continue;
    }
    OccupiedPaths.Add(Request.AgentId, Request.RequestedReservationPoints);
    OccupiedUsesGrantedPath.Add(Request.AgentId, true);
    OutResult.GrantedAgentIds.Add(Request.AgentId);
  }
  OutResult.GrantedAgentIds.Sort();
  OutResult.HeldAgentIds.Sort();
  OutResult.InvalidAgentIds.Sort();
  OutResult.BlockingPairs.Sort([](const auto& A, const auto& B)
  {
    if (A.RequesterAgentId != B.RequesterAgentId)
      return A.RequesterAgentId < B.RequesterAgentId;
    if (A.BlockerAgentId != B.BlockerAgentId)
      return A.BlockerAgentId < B.BlockerAgentId;
    return static_cast<int32>(A.bBlockerGrantedPath)
      < static_cast<int32>(B.bBlockerGrantedPath);
  });
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Settings.SafetyGapCm)));
  for (const FCrowdDemoFrontPhaseReservationRequest& Request : SortedRequests)
  {
    Hash = Fold(Hash, static_cast<uint32>(Request.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Request.CommitGrantedStep));
    Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Request.RadiusCm)));
    Hash = Fold(Hash, static_cast<uint32>(Request.CurrentPhase));
    Hash = Fold(Hash, static_cast<uint32>(Request.RequestedPhase));
    Hash = Fold(Hash, Request.bHasRequest ? 1u : 0u);
    Hash = Fold(Hash, Request.bRequestValid ? 1u : 0u);
    Hash = Fold(Hash, Request.bTargetExclusionClear ? 1u : 0u);
    Hash = Fold(Hash, static_cast<uint32>(Request.CurrentReservationPoints.Num()));
    for (const FVector2f Point : Request.CurrentReservationPoints)
    {
      Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Point.X)));
      Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Point.Y)));
    }
    Hash = Fold(Hash, static_cast<uint32>(Request.RequestedReservationPoints.Num()));
    for (const FVector2f Point : Request.RequestedReservationPoints)
    {
      Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Point.X)));
      Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Point.Y)));
    }
  }
  for (const int32 AgentId : OutResult.GrantedAgentIds)
    Hash = Fold(Fold(Hash, 1u), static_cast<uint32>(AgentId));
  for (const int32 AgentId : OutResult.HeldAgentIds)
    Hash = Fold(Fold(Hash, 2u), static_cast<uint32>(AgentId));
  for (const int32 AgentId : OutResult.InvalidAgentIds)
    Hash = Fold(Fold(Hash, 3u), static_cast<uint32>(AgentId));
  for (const FCrowdDemoFrontPhaseReservationBlockPair& Pair : OutResult.BlockingPairs)
  {
    Hash = Fold(Hash, static_cast<uint32>(Pair.RequesterAgentId));
    Hash = Fold(Hash, static_cast<uint32>(Pair.BlockerAgentId));
    Hash = Fold(Hash, Pair.bBlockerGrantedPath ? 1u : 0u);
  }
  OutResult.DecisionHash = Hash;
}

void FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseReservationPoints(
  const FCrowdDemoFrontApproachRoute& Route,
  const ECrowdDemoFrontApproachPhase Phase,
  FVector2f CurrentLocation,
  TArray<FVector2f>& OutPoints)
{
  OutPoints.Reset();
  CurrentLocation = FVector2f(FMath::RoundToFloat(CurrentLocation.X),
    FMath::RoundToFloat(CurrentLocation.Y));
  if (Phase == ECrowdDemoFrontApproachPhase::None || Route.RoutePoints.IsEmpty()) return;
  OutPoints.Add(CurrentLocation);
  if (Phase == ECrowdDemoFrontApproachPhase::RadialStage)
  {
    OutPoints.Add(Route.RoutePoints[0]);
  }
  else if (Phase == ECrowdDemoFrontApproachPhase::AngularAlign)
  {
    const int32 ArcEnd = FMath::Max(1, Route.RoutePoints.Num() - 1);
    for (int32 Index = 0; Index < ArcEnd; ++Index)
      OutPoints.Add(Route.RoutePoints[Index]);
  }
  else if (Phase == ECrowdDemoFrontApproachPhase::RadialCommit)
  {
    OutPoints.Add(Route.RoutePoints.Last());
  }
}

FVector2f FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseDesiredVelocity(
  const FCrowdDemoFrontApproachRoute& Route,
  const ECrowdDemoFrontApproachPhase CommittedPhase,
  const FVector2f CurrentLocation,
  const float MaxSpeedCmps)
{
  if (CommittedPhase == ECrowdDemoFrontApproachPhase::None
    || Route.RoutePoints.IsEmpty()) return FVector2f::ZeroVector;
  FVector2f Target = Route.RoutePoints[0];
  if (CommittedPhase == ECrowdDemoFrontApproachPhase::AngularAlign)
  {
    Target = Route.OuterGate;
    for (int32 Index = 0; Index + 1 < Route.RoutePoints.Num(); ++Index)
    {
      if ((Route.RoutePoints[Index] - CurrentLocation).SizeSquared() > 100.0f)
      {
        Target = Route.RoutePoints[Index];
        break;
      }
    }
  }
  else if (CommittedPhase == ECrowdDemoFrontApproachPhase::RadialCommit)
    Target = Route.RoutePoints.Last();
  const FVector2f Error = Target - CurrentLocation;
  if (Error.SizeSquared() <= 1.0f) return FVector2f::ZeroVector;
  return Error.GetSafeNormal() * FMath::Min(MaxSpeedCmps, Error.Size() * 2.0f);
}

bool FCrowdDemoPursuitPositioningKernel::ApplyFrontPhaseReservationDecision(
  const FCrowdDemoFrontPhaseReservationDecisionRecord& Decision,
  const int32 Revision,
  FCrowdDemoFrontPhaseReservationState& InOutState)
{
  if (InOutState.AppliedRevision == Revision) return false;
  InOutState.RequestedPhase = Decision.RequestedPhase;
  InOutState.Decision = Decision.Decision;
  InOutState.InvalidReason = Decision.Reason;
  InOutState.AppliedRevision = Revision;
  if (Decision.Decision == ECrowdDemoFrontPhaseReservationDecision::Held)
  {
    ++InOutState.HeldSteps;
    return false;
  }
  if (Decision.Decision != ECrowdDemoFrontPhaseReservationDecision::Granted)
    return false;
  const bool bTransition = InOutState.CurrentPhase != Decision.RequestedPhase;
  InOutState.CurrentPhase = Decision.RequestedPhase;
  InOutState.HeldSteps = 0;
  InOutState.InvalidReason = ECrowdDemoFrontPhaseReservationReason::None;
  return bTransition;
}

void FCrowdDemoPursuitPositioningKernel::AnalyzeFrontReservationWaitGraph(
  const FCrowdDemoPursuitTargetFact& Target,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const TConstArrayView<FCrowdDemoFrontReservationWaitAgent> Agents,
  const TConstArrayView<FCrowdDemoFrontPhaseReservationBlockPair> BlockingPairs,
  TArray<FCrowdDemoFrontReservationWaitEdge>& OutEdges,
  FCrowdDemoFrontReservationWaitGraphSummary& OutSummary,
  FCrowdDemoFrontReservationWaitGraphFixture& OutFixture)
{
  OutEdges.Reset();
  OutSummary = {};
  OutFixture = {};
  TArray<FCrowdDemoFrontReservationWaitAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  TMap<int32, const FCrowdDemoFrontReservationWaitAgent*> AgentById;
  for (const FCrowdDemoFrontReservationWaitAgent& Agent : SortedAgents)
    AgentById.Add(Agent.AgentId, &Agent);
  TArray<FCrowdDemoFrontPhaseReservationBlockPair> SortedPairs(BlockingPairs);
  SortedPairs.Sort([](const auto& A, const auto& B)
  {
    if (A.RequesterAgentId != B.RequesterAgentId)
      return A.RequesterAgentId < B.RequesterAgentId;
    if (A.BlockerAgentId != B.BlockerAgentId)
      return A.BlockerAgentId < B.BlockerAgentId;
    return static_cast<int32>(A.bBlockerGrantedPath)
      < static_cast<int32>(B.bBlockerGrantedPath);
  });
  for (const FCrowdDemoFrontPhaseReservationBlockPair& Pair : SortedPairs)
  {
    const FCrowdDemoFrontReservationWaitAgent* const* Requester =
      AgentById.Find(Pair.RequesterAgentId);
    if (!Requester || (*Requester)->Decision != ECrowdDemoFrontPhaseReservationDecision::Held)
      continue;
    const FCrowdDemoFrontReservationWaitAgent* const* Blocker = AgentById.Find(Pair.BlockerAgentId);
    FCrowdDemoFrontReservationWaitEdge& Edge = OutEdges.AddDefaulted_GetRef();
    Edge.RequesterAgentId = Pair.RequesterAgentId;
    Edge.RequesterCurrentPhase = (*Requester)->CurrentPhase;
    Edge.RequesterRequestedPhase = (*Requester)->RequestedPhase;
    Edge.BlockerAgentId = Pair.BlockerAgentId;
    Edge.RequesterHeldSteps = (*Requester)->HeldSteps;
    Edge.SegmentKind = Pair.bBlockerGrantedPath
      ? ECrowdDemoFrontReservationConflictSegmentKind::RequestedVsGranted
      : ECrowdDemoFrontReservationConflictSegmentKind::RequestedVsCurrent;
    if (Blocker)
    {
      Edge.BlockerCurrentPhase = (*Blocker)->CurrentPhase;
      Edge.bBlockerHasRequest = (*Blocker)->bHasRequest;
      Edge.BlockerNoProgressSteps = (*Blocker)->NoProgressSteps;
      Edge.BlockerRouteForwardVelocityBucket = (*Blocker)->RouteForwardVelocityBucket;
    }
  }
  OutEdges.Sort([](const auto& A, const auto& B)
  {
    if (A.RequesterAgentId != B.RequesterAgentId)
      return A.RequesterAgentId < B.RequesterAgentId;
    if (A.BlockerAgentId != B.BlockerAgentId)
      return A.BlockerAgentId < B.BlockerAgentId;
    return static_cast<uint8>(A.SegmentKind) < static_cast<uint8>(B.SegmentKind);
  });
  TArray<int32> RequesterIds, BlockerIds;
  for (const FCrowdDemoFrontReservationWaitEdge& Edge : OutEdges)
  {
    RequesterIds.AddUnique(Edge.RequesterAgentId);
    BlockerIds.AddUnique(Edge.BlockerAgentId);
  }
  RequesterIds.Sort();
  BlockerIds.Sort();
  OutSummary.UniqueBlockedRequestCount = RequesterIds.Num();
  OutSummary.UniqueBlockerCount = BlockerIds.Num();
  OutSummary.WaitEdgeCount = OutEdges.Num();
  TSet<uint64> DirectedEdges;
  for (const FCrowdDemoFrontReservationWaitEdge& Edge : OutEdges)
    DirectedEdges.Add((static_cast<uint64>(static_cast<uint32>(Edge.RequesterAgentId)) << 32)
      | static_cast<uint32>(Edge.BlockerAgentId));
  for (const FCrowdDemoFrontReservationWaitEdge& Edge : OutEdges)
  {
    if (Edge.RequesterAgentId >= Edge.BlockerAgentId) continue;
    const uint64 Reverse =
      (static_cast<uint64>(static_cast<uint32>(Edge.BlockerAgentId)) << 32)
      | static_cast<uint32>(Edge.RequesterAgentId);
    OutSummary.ReciprocalEdgeCount += DirectedEdges.Contains(Reverse) ? 1 : 0;
  }
  for (const int32 BlockerId : BlockerIds)
  {
    const FCrowdDemoFrontReservationWaitAgent* const* Blocker = AgentById.Find(BlockerId);
    if (!Blocker || !(*Blocker)->bActiveMember)
    {
      ++OutSummary.StaleOwnerCount;
      continue;
    }
    const bool bStalled = (*Blocker)->NoProgressSteps >= 30
      || (*Blocker)->RouteForwardVelocityBucket <= 10;
    OutSummary.StalledBlockerCount += bStalled ? 1 : 0;
    OutSummary.ProgressingBlockerCount += bStalled ? 0 : 1;
    OutSummary.BlockerRadialCount += (*Blocker)->CurrentPhase
      == ECrowdDemoFrontApproachPhase::RadialStage ? 1 : 0;
    OutSummary.BlockerAngularCount += (*Blocker)->CurrentPhase
      == ECrowdDemoFrontApproachPhase::AngularAlign ? 1 : 0;
    OutSummary.BlockerRadialCommitCount += (*Blocker)->CurrentPhase
      == ECrowdDemoFrontApproachPhase::RadialCommit ? 1 : 0;
  }
  TMap<int32, TArray<int32>> Adjacency;
  TArray<int32> Nodes;
  for (const FCrowdDemoFrontReservationWaitEdge& Edge : OutEdges)
  {
    Adjacency.FindOrAdd(Edge.RequesterAgentId).AddUnique(Edge.BlockerAgentId);
    Nodes.AddUnique(Edge.RequesterAgentId);
    Nodes.AddUnique(Edge.BlockerAgentId);
  }
  Nodes.Sort();
  for (TPair<int32, TArray<int32>>& Entry : Adjacency) Entry.Value.Sort();
  TMap<int32, int32> IndexByNode, LowByNode;
  TArray<int32> Stack;
  TSet<int32> OnStack;
  TArray<TArray<int32>> CycleComponents;
  int32 NextIndex = 0;
  TFunction<void(int32)> StrongConnect = [&](const int32 Node)
  {
    IndexByNode.Add(Node, NextIndex);
    LowByNode.Add(Node, NextIndex++);
    Stack.Add(Node);
    OnStack.Add(Node);
    if (const TArray<int32>* Neighbors = Adjacency.Find(Node))
    {
      for (const int32 Other : *Neighbors)
      {
        if (!IndexByNode.Contains(Other))
        {
          StrongConnect(Other);
          LowByNode[Node] = FMath::Min(LowByNode[Node], LowByNode[Other]);
        }
        else if (OnStack.Contains(Other))
        {
          LowByNode[Node] = FMath::Min(LowByNode[Node], IndexByNode[Other]);
        }
      }
    }
    if (LowByNode[Node] != IndexByNode[Node]) return;
    TArray<int32> Component;
    int32 Popped = INDEX_NONE;
    do
    {
      Popped = Stack.Pop(EAllowShrinking::No);
      OnStack.Remove(Popped);
      Component.Add(Popped);
    } while (Popped != Node);
    const uint64 SelfKey = (static_cast<uint64>(static_cast<uint32>(Node)) << 32)
      | static_cast<uint32>(Node);
    if (Component.Num() > 1 || DirectedEdges.Contains(SelfKey))
    {
      Component.Sort();
      CycleComponents.Add(Component);
      ++OutSummary.CycleCount;
      OutSummary.MaxCycleSize = FMath::Max(OutSummary.MaxCycleSize, Component.Num());
    }
  };
  for (const int32 Node : Nodes)
    if (!IndexByNode.Contains(Node)) StrongConnect(Node);
  CycleComponents.Sort([](const TArray<int32>& A, const TArray<int32>& B)
  {
    if (A.Num() != B.Num()) return A.Num() < B.Num();
    return A.IsEmpty() || (!B.IsEmpty() && A[0] < B[0]);
  });
  for (const TArray<int32>& Component : CycleComponents)
  {
    bool bSafeAtomicSet = !Component.IsEmpty();
    for (const int32 AgentId : Component)
    {
      const FCrowdDemoFrontReservationWaitAgent* const* Agent = AgentById.Find(AgentId);
      bSafeAtomicSet &= Agent && (*Agent)->bActiveMember && (*Agent)->bHasRequest
        && (*Agent)->bRequestValid && (*Agent)->bTargetExclusionClear
        && (*Agent)->Decision == ECrowdDemoFrontPhaseReservationDecision::Held
        && (*Agent)->RequestedReservationPoints.Num() >= 2;
    }
    for (int32 AIndex = 0; bSafeAtomicSet && AIndex < Component.Num(); ++AIndex)
    {
      const FCrowdDemoFrontReservationWaitAgent& A = **AgentById.Find(Component[AIndex]);
      for (int32 BIndex = AIndex + 1; BIndex < Component.Num(); ++BIndex)
      {
        const FCrowdDemoFrontReservationWaitAgent& B = **AgentById.Find(Component[BIndex]);
        if (FrontReservationPathsConflict(Settings, A.RequestedReservationPoints, A.RadiusCm,
          B.RequestedReservationPoints, B.RadiusCm))
        {
          bSafeAtomicSet = false;
          break;
        }
      }
    }
    if (bSafeAtomicSet)
    {
      ++OutSummary.AtomicHandoffCycleCount;
      OutSummary.MaxAtomicHandoffSetSize = FMath::Max(
        OutSummary.MaxAtomicHandoffSetSize, Component.Num());
    }
  }
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoFrontReservationWaitAgent& Agent : SortedAgents)
  {
    Hash = Fold(Hash, static_cast<uint32>(Agent.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Agent.CurrentPhase));
    Hash = Fold(Hash, static_cast<uint32>(Agent.RequestedPhase));
    Hash = Fold(Hash, static_cast<uint32>(Agent.Decision));
    Hash = Fold(Hash, Agent.bHasRequest ? 1u : 0u);
    Hash = Fold(Hash, Agent.bRequestValid ? 1u : 0u);
    Hash = Fold(Hash, Agent.bTargetExclusionClear ? 1u : 0u);
    Hash = Fold(Hash, Agent.bActiveMember ? 1u : 0u);
    Hash = Fold(Hash, static_cast<uint32>(Agent.HeldSteps));
    Hash = Fold(Hash, static_cast<uint32>(Agent.NoProgressSteps));
    Hash = Fold(Hash, static_cast<uint32>(Agent.RouteForwardVelocityBucket));
  }
  for (const FCrowdDemoFrontReservationWaitEdge& Edge : OutEdges)
  {
    Hash = Fold(Hash, static_cast<uint32>(Edge.RequesterAgentId));
    Hash = Fold(Hash, static_cast<uint32>(Edge.BlockerAgentId));
    Hash = Fold(Hash, static_cast<uint32>(Edge.SegmentKind));
  }
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.CycleCount));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.MaxCycleSize));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.AtomicHandoffCycleCount));
  Hash = Fold(Hash, static_cast<uint32>(OutSummary.MaxAtomicHandoffSetSize));
  OutSummary.WaitGraphHash = Hash;
  if (!OutEdges.IsEmpty())
  {
    TArray<FCrowdDemoFrontReservationWaitEdge> RankedEdges = OutEdges;
    RankedEdges.Sort([](const auto& A, const auto& B)
    {
      if (A.RequesterHeldSteps != B.RequesterHeldSteps)
        return A.RequesterHeldSteps > B.RequesterHeldSteps;
      if (A.RequesterAgentId != B.RequesterAgentId)
        return A.RequesterAgentId < B.RequesterAgentId;
      return A.BlockerAgentId < B.BlockerAgentId;
    });
    const FCrowdDemoFrontReservationWaitEdge& Selected = RankedEdges[0];
    OutFixture.bValid = true;
    OutFixture.Target = Target;
    OutFixture.Settings = Settings;
    OutFixture.Edges.Add(Selected);
    if (const FCrowdDemoFrontReservationWaitAgent* const* Agent =
      AgentById.Find(Selected.RequesterAgentId)) OutFixture.Agents.Add(**Agent);
    if (const FCrowdDemoFrontReservationWaitAgent* const* Agent =
      AgentById.Find(Selected.BlockerAgentId)) OutFixture.Agents.Add(**Agent);
    OutFixture.Agents.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
    uint32 FixtureHash = 2166136261u;
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(Target.TargetId));
    for (const FCrowdDemoFrontReservationWaitAgent& Agent : OutFixture.Agents)
    {
      FixtureHash = Fold(FixtureHash, static_cast<uint32>(Agent.AgentId));
      FixtureHash = Fold(FixtureHash, static_cast<uint32>(Agent.CurrentPhase));
      FixtureHash = Fold(FixtureHash, static_cast<uint32>(Agent.RequestedPhase));
      for (const FVector2f Point : Agent.CurrentReservationPoints)
      {
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Point.X)));
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Point.Y)));
      }
      for (const FVector2f Point : Agent.RequestedReservationPoints)
      {
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Point.X)));
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Point.Y)));
      }
    }
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(Selected.RequesterAgentId));
    FixtureHash = Fold(FixtureHash, static_cast<uint32>(Selected.BlockerAgentId));
    OutFixture.StableHash = FixtureHash;
  }
}

FCrowdDemoFrontApproachRoute FCrowdDemoPursuitPositioningKernel::BuildFrontApproachRoute(
  const FCrowdDemoPursuitTargetFact& Target,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const FCrowdDemoSharedFlowField& FlowField,
  const int32 AgentId,
  const float AgentRadiusCm,
  FVector2f CurrentLocation,
  const FCrowdDemoPositionCandidate& Candidate,
  const TConstArrayView<FCrowdDemoPositionIngressBlocker> OccupiedBlockers,
  const float MaxSpeedCmps,
  const int32 RouteRevision)
{
  FCrowdDemoFrontApproachRoute Route;
  Route.AgentId = AgentId;
  Route.PositionId = Candidate.PositionId;
  Route.RouteRevision = RouteRevision;
  CurrentLocation = FVector2f(FMath::RoundToFloat(CurrentLocation.X), FMath::RoundToFloat(CurrentLocation.Y));
  const FVector2f TargetLocation(FMath::RoundToFloat(Target.Location.X), FMath::RoundToFloat(Target.Location.Y));
  Route.TargetToCandidateDirection = Candidate.LocalOffset.GetSafeNormal();
  Route.EntryAxis = (CurrentLocation - TargetLocation).GetSafeNormal();
  if (Route.EntryAxis.IsNearlyZero()) Route.EntryAxis = -Route.TargetToCandidateDirection;
  const float ExclusionRadius = FMath::RoundToFloat(
    Target.RadiusCm + AgentRadiusCm + Settings.SafetyGapCm);
  const float GateClearance = FMath::Max(
    2.0f * AgentRadiusCm + Settings.SafetyGapCm,
    FlowField.Config.CellSizeCm);
  const float CandidateRadius = Candidate.LocalOffset.Size();
  const float OuterGateRadius = FMath::RoundToFloat(FMath::Max(
    CandidateRadius + GateClearance, ExclusionRadius + GateClearance));
  Route.OuterGate = TargetLocation + Route.TargetToCandidateDirection * OuterGateRadius;
  Route.CandidateSector = Candidate.AngularSector;
  const FVector2f CurrentOffset = CurrentLocation - TargetLocation;
  const float CurrentRadius = CurrentOffset.Size();
  const FVector2f CurrentDirection = CurrentOffset.IsNearlyZero()
    ? Route.EntryAxis : CurrentOffset.GetSafeNormal();
  float CurrentAngle = FMath::Atan2(CurrentDirection.Y, CurrentDirection.X);
  if (CurrentAngle < 0.0f) CurrentAngle += 2.0f * PI;
  float CandidateAngle = FMath::Atan2(
    Route.TargetToCandidateDirection.Y, Route.TargetToCandidateDirection.X);
  if (CandidateAngle < 0.0f) CandidateAngle += 2.0f * PI;
  Route.CurrentSector = FMath::Clamp(FMath::FloorToInt(CurrentAngle / (2.0f * PI)
    * FMath::Max(1, Settings.AngularSectorCount)), 0,
    FMath::Max(0, Settings.AngularSectorCount - 1));
  const float CcwDistance = FMath::Fmod(CandidateAngle - CurrentAngle + 2.0f * PI, 2.0f * PI);
  const float CwDistance = 2.0f * PI - CcwDistance;
  const uint32 TieHash = Fold(Fold(2166136261u, static_cast<uint32>(AgentId)),
    static_cast<uint32>(Candidate.PositionId));
  TArray<TPair<int32, float>> Turns;
  if (FMath::IsNearlyEqual(CcwDistance, CwDistance, 0.0001f))
  {
    const int32 First = (TieHash & 1u) == 0u ? -1 : 1;
    Turns.Add({First, PI});
    Turns.Add({-First, PI});
  }
  else if (CcwDistance < CwDistance)
  {
    Turns.Add({1, CcwDistance});
    Turns.Add({-1, CwDistance});
  }
  else
  {
    Turns.Add({-1, CwDistance});
    Turns.Add({1, CcwDistance});
  }
  FCrowdDemoSharedFlowFieldConfig ClearanceConfig = FlowField.Config;
  ClearanceConfig.AgentInflateCm = AgentRadiusCm + Settings.SafetyGapCm;
  const auto PointValid = [&](const FVector2f Point)
  {
    const FVector Point3(Point.X, Point.Y, 60.0f);
    if (FCrowdDemoSharedFlowFieldKernel::Sample(FlowField, Point3).Status
      != ECrowdDemoFlowLocationStatus::Reachable) return false;
    if (FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(ClearanceConfig, Point3)) return false;
    for (const FCrowdDemoPositionIngressBlocker& Blocker : OccupiedBlockers)
    {
      if ((Point - Blocker.Location).SizeSquared() < FMath::Square(
        AgentRadiusCm + Blocker.RadiusCm + Settings.SafetyGapCm)) return false;
    }
    return true;
  };
  Route.bGateReachable = PointValid(Route.OuterGate);
  TArray<FVector2f> ChosenArc;
  for (const TPair<int32, float>& Turn : Turns)
  {
    TArray<FVector2f> Arc;
    bool bValid = true;
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(
      Turn.Value / (2.0f * PI) * FMath::Max(8, Settings.AngularSectorCount)));
    for (int32 Step = 1; Step <= Steps; ++Step)
    {
      const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
      const float Angle = CurrentAngle + static_cast<float>(Turn.Key) * Turn.Value * Alpha;
      const FVector2f Point = TargetLocation + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle))
        * OuterGateRadius;
      const FVector2f Quantized(FMath::RoundToFloat(Point.X), FMath::RoundToFloat(Point.Y));
      if (!PointValid(Quantized)) { bValid = false; break; }
      Arc.Add(Quantized);
    }
    if (bValid)
    {
      Route.TurnDirectionKey = Turn.Key;
      Route.AngularErrorRadians = Turn.Value;
      ChosenArc = MoveTemp(Arc);
      Route.bArcReachable = true;
      break;
    }
  }
  Route.bRadialCommitClear = true;
  Route.bTargetExclusionClear = !SegmentIntersectsSafetyCircle(
    Route.OuterGate, Candidate.WorldLocation, TargetLocation, ExclusionRadius - 1.0f);
  for (const FCrowdDemoPositionIngressBlocker& Blocker : OccupiedBlockers)
  {
    if (SegmentIntersectsSafetyCircle(Route.OuterGate, Candidate.WorldLocation,
      Blocker.Location, AgentRadiusCm + Blocker.RadiusCm + Settings.SafetyGapCm))
    {
      Route.bRadialCommitClear = false;
      break;
    }
  }
  const FVector2f RadialStagePoint = TargetLocation + CurrentDirection * OuterGateRadius;
  Route.RoutePoints.Add(FVector2f(FMath::RoundToFloat(RadialStagePoint.X),
    FMath::RoundToFloat(RadialStagePoint.Y)));
  Route.RoutePoints.Append(ChosenArc);
  Route.RoutePoints.Add(Candidate.WorldLocation);
  Route.RadialErrorCm = FMath::RoundToFloat(FMath::Abs(CurrentRadius - OuterGateRadius));
  const bool bRouteValid = Route.bGateReachable && Route.bArcReachable
    && Route.bRadialCommitClear && Route.bTargetExclusionClear;
  if (!bRouteValid)
  {
    Route.Phase = ECrowdDemoFrontApproachPhase::None;
    Route.DesiredVelocity = FVector2f::ZeroVector;
    Route.RouteErrorBucket = MAX_int32;
  }
  else if (Route.RadialErrorCm
    > Settings.FrontApproachRadialToleranceCm + Settings.SafetyGapCm)
  {
    Route.Phase = ECrowdDemoFrontApproachPhase::RadialStage;
    const float Sign = CurrentRadius > OuterGateRadius ? -1.0f : 1.0f;
    Route.DesiredVelocity = CurrentDirection * Sign * FMath::Min(MaxSpeedCmps,
      FMath::Max(60.0f, Route.RadialErrorCm * 2.0f));
    Route.RouteErrorBucket = FMath::RoundToInt(Route.RadialErrorCm);
  }
  else if (Route.AngularErrorRadians > Settings.FrontApproachAngularCommitToleranceRadians)
  {
    Route.Phase = ECrowdDemoFrontApproachPhase::AngularAlign;
    const FVector2f Tangent = Route.TurnDirectionKey > 0
      ? FVector2f(-CurrentDirection.Y, CurrentDirection.X)
      : FVector2f(CurrentDirection.Y, -CurrentDirection.X);
    const FVector2f RadialCorrection = CurrentDirection
      * FMath::Clamp(OuterGateRadius - CurrentRadius, -MaxSpeedCmps * 0.5f, MaxSpeedCmps * 0.5f);
    Route.DesiredVelocity = (Tangent * (MaxSpeedCmps * 0.75f) + RadialCorrection)
      .GetClampedToMaxSize(MaxSpeedCmps);
    Route.RouteErrorBucket = FMath::RoundToInt(Route.AngularErrorRadians * 1000.0f)
      + FMath::RoundToInt(Route.RadialErrorCm);
  }
  else
  {
    Route.Phase = ECrowdDemoFrontApproachPhase::RadialCommit;
    const FVector2f Error = Candidate.WorldLocation - CurrentLocation;
    Route.DesiredVelocity = Error.GetSafeNormal()
      * FMath::Min(MaxSpeedCmps, FMath::Max(30.0f, Error.Size() * 2.0f));
    Route.RouteErrorBucket = FMath::RoundToInt(Error.Size());
  }
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, static_cast<uint32>(AgentId));
  Hash = Fold(Hash, static_cast<uint32>(Candidate.PositionId));
  Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(ExclusionRadius)));
  Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(OuterGateRadius)));
  Hash = Fold(Hash, static_cast<uint32>(Route.TurnDirectionKey));
  Hash = Fold(Hash, static_cast<uint32>(Route.Phase));
  Hash = Fold(Hash, Route.bGateReachable ? 1u : 0u);
  Hash = Fold(Hash, Route.bArcReachable ? 1u : 0u);
  Hash = Fold(Hash, Route.bRadialCommitClear ? 1u : 0u);
  Hash = Fold(Hash, Route.bTargetExclusionClear ? 1u : 0u);
  for (const FVector2f Point : Route.RoutePoints)
  {
    Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Point.X)));
    Hash = Fold(Hash, static_cast<uint32>(FMath::RoundToInt(Point.Y)));
  }
  Route.RouteHash = Hash;
  return Route;
}

void FCrowdDemoPursuitPositioningKernel::BuildHoldingCandidates(
  const FCrowdDemoPursuitTargetFact& Target,
  const float AgentRadiusCm,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const FCrowdDemoSharedFlowField& FlowField,
  const TConstArrayView<FCrowdDemoPositionCandidate> PositionCandidates,
  TArray<FCrowdDemoHoldingCandidate>& OutCandidates,
  FCrowdDemoHoldingSummary& OutSummary)
{
  OutCandidates.Reset();
  OutSummary = {};
  if (!FlowField.IsValid() || Target.TargetId == INDEX_NONE) return;
  const float Quantum = FMath::Max(0.1f, Settings.PositionQuantumCm);
  const float CellSize = FMath::Max(1.0f, FlowField.Config.CellSizeCm);
  const float MinimumSurfaceDistance = Settings.AllowedDistanceMaxCm + CellSize;
  const float MaximumSurfaceDistance = MinimumSurfaceDistance
    + FMath::Max(1, Settings.HoldingRadialBandCount) * CellSize;
  const float MinimumSpacing = 2.0f * AgentRadiusCm + Settings.HoldingGapCm;
  const float TargetExclusion = Target.RadiusCm + AgentRadiusCm + Settings.SafetyGapCm;
  FCrowdDemoSharedFlowFieldConfig ClearanceConfig = FlowField.Config;
  ClearanceConfig.AgentInflateCm = AgentRadiusCm + Settings.SafetyGapCm;
  TArray<FCrowdDemoHoldingCandidate> Raw;
  for (int32 CellIndex = 0; CellIndex < FlowField.Width * FlowField.Height; ++CellIndex)
  {
    if (!FlowField.IntegrationCost.IsValidIndex(CellIndex)
      || FlowField.Blocked[CellIndex] || FlowField.Unreachable[CellIndex]) continue;
    const FVector Center3 = FlowField.CellCenter(CellIndex);
    const FVector2f Center(Center3.X, Center3.Y);
    const FVector2f Offset = Center - Target.Location;
    const float SurfaceDistance = Offset.Size() - Target.RadiusCm - AgentRadiusCm;
    if (SurfaceDistance < MinimumSurfaceDistance || SurfaceDistance > MaximumSurfaceDistance
      || Offset.Size() < TargetExclusion) continue;
    if (FCrowdDemoSharedFlowFieldKernel::Sample(FlowField, Center3).Status
      != ECrowdDemoFlowLocationStatus::Reachable) continue;
    if (FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
      ClearanceConfig, Center3)) continue;
    bool bOverlapsPosition = false;
    for (const FCrowdDemoPositionCandidate& Position : PositionCandidates)
    {
      if ((Center - Position.WorldLocation).SizeSquared() < FMath::Square(MinimumSpacing))
      {
        bOverlapsPosition = true;
        break;
      }
    }
    if (bOverlapsPosition) continue;
    FCrowdDemoHoldingCandidate& Candidate = Raw.AddDefaulted_GetRef();
    Candidate.TargetId = Target.TargetId;
    Candidate.TargetRevision = Target.Revision;
    Candidate.StableCellKey = CellIndex;
    Candidate.WorldLocation = FVector2f(
      FMath::RoundToFloat(Center.X / Quantum) * Quantum,
      FMath::RoundToFloat(Center.Y / Quantum) * Quantum);
    Candidate.RadialBand = FMath::Clamp(FMath::FloorToInt(
      (SurfaceDistance - MinimumSurfaceDistance) / CellSize), 0,
      FMath::Max(0, Settings.HoldingRadialBandCount - 1));
    float Angle = FMath::Atan2(Offset.Y, Offset.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    Candidate.AngularSector = FMath::Clamp(FMath::FloorToInt(
      Angle / (2.0f * PI) * FMath::Max(1, Settings.AngularSectorCount)),
      0, FMath::Max(0, Settings.AngularSectorCount - 1));
    // StableCellKey is already unique inside the deterministic shared field.
    // TargetRevision scopes ownership across rebuilds, so hashing the key into
    // a signed 31-bit id only introduces avoidable collisions.
    Candidate.HoldingId = CellIndex;
    Candidate.bReachable = true;
    Candidate.bClearanceValid = true;
  }
  Raw.Sort([](const auto& A, const auto& B)
  {
    if (A.RadialBand != B.RadialBand) return A.RadialBand < B.RadialBand;
    if (A.AngularSector != B.AngularSector) return A.AngularSector < B.AngularSector;
    if (A.StableCellKey != B.StableCellKey) return A.StableCellKey < B.StableCellKey;
    if (A.WorldLocation.X != B.WorldLocation.X) return A.WorldLocation.X < B.WorldLocation.X;
    return A.WorldLocation.Y < B.WorldLocation.Y;
  });
  for (const FCrowdDemoHoldingCandidate& Candidate : Raw)
  {
    bool bOverlap = false;
    for (const FCrowdDemoHoldingCandidate& Accepted : OutCandidates)
      if ((Candidate.WorldLocation - Accepted.WorldLocation).SizeSquared()
        < FMath::Square(MinimumSpacing)) { bOverlap = true; break; }
    if (!bOverlap) OutCandidates.Add(Candidate);
  }
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoHoldingCandidate& Candidate : OutCandidates)
  {
    Hash = Fold(Hash, static_cast<uint32>(Candidate.HoldingId));
    Hash = Fold(Hash, static_cast<uint32>(Candidate.RadialBand));
    Hash = Fold(Hash, static_cast<uint32>(Candidate.AngularSector));
    Hash = Fold(Hash, static_cast<uint32>(Candidate.StableCellKey));
  }
  OutSummary.CandidateCount = OutCandidates.Num();
  OutSummary.CandidateHash = Hash;
}

FCrowdDemoHoldingPositionCompatibility
FCrowdDemoPursuitPositioningKernel::EvaluateHoldingPositionCompatibility(
  const FCrowdDemoPursuitTargetFact& Target,
  const float AgentRadiusCm,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const FCrowdDemoSharedFlowField& FlowField,
  const FCrowdDemoHoldingCandidate& Holding,
  const FCrowdDemoPositionCandidate& Position,
  const TConstArrayView<FCrowdDemoPositionIngressBlocker> OccupiedBlockers)
{
  FCrowdDemoHoldingPositionCompatibility Result;
  Result.HoldingId = Holding.HoldingId;
  Result.PositionId = Position.PositionId;
  Result.QuantizedRouteCostCm = FMath::RoundToInt(
    (Position.WorldLocation - Holding.WorldLocation).Size());
  Result.PositionIngressCost = Position.RadialBand * 1000 + Position.AngularSector;
  if (!FlowField.IsValid() || Holding.TargetId != Target.TargetId
    || Holding.TargetRevision != Target.Revision || Position.TargetId != Target.TargetId)
    return Result;
  FCrowdDemoSharedFlowFieldConfig ClearanceConfig = FlowField.Config;
  ClearanceConfig.AgentInflateCm = AgentRadiusCm + Settings.SafetyGapCm;
  const float TargetExclusion = Target.RadiusCm + AgentRadiusCm + Settings.SafetyGapCm;
  const float SampleSpacing = FMath::Max(25.0f, FlowField.Config.CellSizeCm * 0.5f);
  const int32 SampleCount = FMath::Max(1, FMath::CeilToInt(
    static_cast<float>(Result.QuantizedRouteCostCm) / SampleSpacing));
  Result.bFlowReachable = true;
  Result.bTargetClear = true;
  Result.bObstacleClear = true;
  for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
  {
    const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount);
    const FVector2f Point = FMath::Lerp(Holding.WorldLocation, Position.WorldLocation, Alpha);
    const FVector Point3(Point.X, Point.Y, 60.0f);
    const FCrowdDemoSharedFlowSample FlowSample =
      FCrowdDemoSharedFlowFieldKernel::Sample(FlowField, Point3);
    if (FlowSample.Status != ECrowdDemoFlowLocationStatus::Reachable)
      Result.bFlowReachable = false;
    if (FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(ClearanceConfig, Point3))
      Result.bObstacleClear = false;
    if ((Point - Target.Location).SizeSquared() < FMath::Square(TargetExclusion))
      Result.bTargetClear = false;
  }
  Result.bStableBlockerClear = true;
  for (const FCrowdDemoPositionIngressBlocker& Blocker : OccupiedBlockers)
  {
    if (Blocker.State != ECrowdDemoPursuitPositionState::StableOccupied
      && Blocker.State != ECrowdDemoPursuitPositionState::ReserveHold) continue;
    if (SegmentIntersectsSafetyCircle(Holding.WorldLocation, Position.WorldLocation,
      Blocker.Location, AgentRadiusCm + Blocker.RadiusCm + Settings.SafetyGapCm))
    {
      Result.bStableBlockerClear = false;
      break;
    }
  }
  Result.bCompatible = Result.bFlowReachable && Result.bTargetClear
    && Result.bObstacleClear && Result.bStableBlockerClear;
  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, static_cast<uint32>(Result.HoldingId));
  Hash = Fold(Hash, static_cast<uint32>(Result.PositionId));
  Hash = Fold(Hash, static_cast<uint32>(Result.QuantizedRouteCostCm));
  Hash = Fold(Hash, Result.bFlowReachable ? 1u : 0u);
  Hash = Fold(Hash, Result.bTargetClear ? 1u : 0u);
  Hash = Fold(Hash, Result.bObstacleClear ? 1u : 0u);
  Hash = Fold(Hash, Result.bStableBlockerClear ? 1u : 0u);
  Result.StableHash = Hash;
  return Result;
}

void FCrowdDemoPursuitPositioningKernel::AssignHoldingPositions(
  const FCrowdDemoPursuitTargetFact& Target,
  const TConstArrayView<FCrowdDemoHoldingAgent> Agents,
  const TConstArrayView<FCrowdDemoHoldingCandidate> HoldingCandidates,
  const TConstArrayView<FCrowdDemoHoldingPositionCompatibility> Compatibility,
  TArray<FCrowdDemoHoldingAssignment>& OutAssignments,
  FCrowdDemoHoldingSummary& OutSummary)
{
  OutAssignments.Reset();
  OutSummary.AssignedCount = OutSummary.UnassignedCount = OutSummary.ReusedCount = 0;
  OutSummary.ReacquireCount = 0;
  OutSummary.SelectedCompatibilityValidCount = 0;
  OutSummary.SelectedCompatibilityInvalidCount = 0;
  OutSummary.DuplicateCompatibilityKeyCount = 0;
  TArray<FCrowdDemoHoldingAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B)
  {
    if (A.WaitEpoch != B.WaitEpoch) return A.WaitEpoch > B.WaitEpoch;
    if (A.PositionIngressCost != B.PositionIngressCost)
      return A.PositionIngressCost < B.PositionIngressCost;
    if (A.PositionId != B.PositionId) return A.PositionId < B.PositionId;
    return A.AgentId < B.AgentId;
  });
  TArray<FCrowdDemoHoldingCandidate> SortedHoldings(HoldingCandidates);
  SortedHoldings.Sort([](const auto& A, const auto& B)
  {
    return A.HoldingId < B.HoldingId;
  });
  TArray<FCrowdDemoHoldingPositionCompatibility> SortedCompatibility(Compatibility);
  SortedCompatibility.Sort([](const auto& A, const auto& B)
  {
    if (A.PositionId != B.PositionId) return A.PositionId < B.PositionId;
    if (A.QuantizedRouteCostCm != B.QuantizedRouteCostCm)
      return A.QuantizedRouteCostCm < B.QuantizedRouteCostCm;
    return A.HoldingId < B.HoldingId;
  });
  TSet<uint64> CompatibilityKeys;
  for (const FCrowdDemoHoldingPositionCompatibility& Edge : SortedCompatibility)
  {
    const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Edge.HoldingId)) << 32)
      | static_cast<uint32>(Edge.PositionId);
    if (CompatibilityKeys.Contains(Key)) ++OutSummary.DuplicateCompatibilityKeyCount;
    else CompatibilityKeys.Add(Key);
  }
  const auto FindCompatibility = [&](const int32 HoldingId, const int32 PositionId)
    -> const FCrowdDemoHoldingPositionCompatibility*
  {
    return SortedCompatibility.FindByPredicate([&](const auto& Item)
    {
      return Item.HoldingId == HoldingId && Item.PositionId == PositionId;
    });
  };
  TSet<int32> OwnedHoldingIds;
  TSet<int32> CompletedAgentIds;
  for (const FCrowdDemoHoldingAgent& Agent : SortedAgents)
  {
    if (!Agent.bPositionValid || Agent.ExistingTargetRevision != Target.Revision
      || Agent.ExistingHoldingId == INDEX_NONE) continue;
    const FCrowdDemoHoldingCandidate* Holding = SortedHoldings.FindByPredicate(
      [&](const auto& Item) { return Item.HoldingId == Agent.ExistingHoldingId; });
    const FCrowdDemoHoldingPositionCompatibility* Compatible =
      FindCompatibility(Agent.ExistingHoldingId, Agent.PositionId);
    const bool bCompleted = Agent.ExistingState == ECrowdDemoPursuitSteeringState::StableOccupied
      || Agent.ExistingState == ECrowdDemoPursuitSteeringState::ReserveHold;
    if (!Holding || (!bCompleted && (!Compatible || !Compatible->bCompatible))
      || OwnedHoldingIds.Contains(Holding->HoldingId)) continue;
    FCrowdDemoHoldingAssignment& Assignment = OutAssignments.AddDefaulted_GetRef();
    Assignment.AgentId = Agent.AgentId;
    Assignment.HoldingId = Holding->HoldingId;
    Assignment.PositionId = Agent.PositionId;
    Assignment.HoldingLocation = Holding->WorldLocation;
    Assignment.AssignedPosition = Agent.AssignedPosition;
    Assignment.State = bCompleted ? Agent.ExistingState : ECrowdDemoPursuitSteeringState::Holding;
    Assignment.IntegerCost = Compatible ? Compatible->QuantizedRouteCostCm : 0;
    Assignment.CompatibilityHash = Compatible ? Compatible->StableHash : 2166136261u;
    Assignment.bCompatibilityValid = bCompleted || (Compatible && Compatible->bCompatible);
    Assignment.bReused = true;
    OwnedHoldingIds.Add(Holding->HoldingId);
    CompletedAgentIds.Add(Agent.AgentId);
    ++OutSummary.ReusedCount;
  }
  for (const FCrowdDemoHoldingAgent& Agent : SortedAgents)
  {
    if (CompletedAgentIds.Contains(Agent.AgentId)) continue;
    if (!Agent.bPositionValid)
    {
      FCrowdDemoHoldingAssignment& Assignment = OutAssignments.AddDefaulted_GetRef();
      Assignment.AgentId = Agent.AgentId;
      Assignment.PositionId = Agent.PositionId;
      Assignment.State = ECrowdDemoPursuitSteeringState::Reacquire;
      CompletedAgentIds.Add(Agent.AgentId);
      ++OutSummary.ReacquireCount;
      continue;
    }
    const FCrowdDemoHoldingCandidate* BestHolding = nullptr;
    const FCrowdDemoHoldingPositionCompatibility* BestCompatibility = nullptr;
    for (const FCrowdDemoHoldingCandidate& Holding : SortedHoldings)
    {
      if (OwnedHoldingIds.Contains(Holding.HoldingId)) continue;
      const FCrowdDemoHoldingPositionCompatibility* Compatible =
        FindCompatibility(Holding.HoldingId, Agent.PositionId);
      if (!Compatible || !Compatible->bCompatible) continue;
      if (!BestCompatibility
        || Compatible->QuantizedRouteCostCm < BestCompatibility->QuantizedRouteCostCm
        || (Compatible->QuantizedRouteCostCm == BestCompatibility->QuantizedRouteCostCm
          && Holding.HoldingId < BestHolding->HoldingId))
      {
        BestHolding = &Holding;
        BestCompatibility = Compatible;
      }
    }
    if (!BestHolding)
    {
      continue;
    }
    FCrowdDemoHoldingAssignment& Assignment = OutAssignments.AddDefaulted_GetRef();
    Assignment.AgentId = Agent.AgentId;
    Assignment.HoldingId = BestHolding->HoldingId;
    Assignment.PositionId = Agent.PositionId;
    Assignment.HoldingLocation = BestHolding->WorldLocation;
    Assignment.AssignedPosition = Agent.AssignedPosition;
    Assignment.State = ECrowdDemoPursuitSteeringState::Holding;
    Assignment.IntegerCost = BestCompatibility->QuantizedRouteCostCm;
    Assignment.CompatibilityHash = BestCompatibility->StableHash;
    Assignment.bCompatibilityValid = BestCompatibility->bCompatible;
    OwnedHoldingIds.Add(BestHolding->HoldingId);
  }
  OutAssignments.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoHoldingAssignment& Assignment : OutAssignments)
  {
    Hash = Fold(Hash, static_cast<uint32>(Assignment.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Assignment.HoldingId));
    Hash = Fold(Hash, static_cast<uint32>(Assignment.PositionId));
    Hash = Fold(Hash, static_cast<uint32>(Assignment.State));
    Hash = Fold(Hash, Assignment.CompatibilityHash);
    Hash = Fold(Hash, Assignment.bCompatibilityValid ? 1u : 0u);
    Hash = Fold(Hash, Assignment.bReused ? 1u : 0u);
    OutSummary.SelectedCompatibilityValidCount += Assignment.bCompatibilityValid ? 1 : 0;
    OutSummary.SelectedCompatibilityInvalidCount += Assignment.bCompatibilityValid ? 0 : 1;
  }
  OutSummary.AssignedCount = OutAssignments.Num() - OutSummary.ReacquireCount;
  OutSummary.UnassignedCount += Agents.Num() - OutAssignments.Num();
  OutSummary.AssignmentHash = Hash;
}

bool FCrowdDemoPursuitPositioningKernel::PositioningSegmentConflictsWithBlocker(
  const FVector2f Start,
  const FVector2f End,
  const float AgentRadiusCm,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const FCrowdDemoPositionIngressBlocker& Blocker)
{
  return SegmentIntersectsSafetyCircle(Start, End, Blocker.Location,
    AgentRadiusCm + Blocker.RadiusCm + Settings.SafetyGapCm);
}

void FCrowdDemoPursuitPositioningKernel::AnalyzeResidualPositioning(
  const TConstArrayView<FCrowdDemoResidualPositioningAgent> Agents,
  const TConstArrayView<int32> RemainingPositionIds,
  const TConstArrayView<FCrowdDemoResidualPositioningEdge> Edges,
  FCrowdDemoResidualPositioningSummary& OutSummary)
{
  OutSummary = {};
  TArray<FCrowdDemoResidualPositioningAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TArray<int32> SortedPositions(RemainingPositionIds);
  SortedPositions.Sort();
  SortedPositions.SetNum(Algo::Unique(SortedPositions));
  TArray<FCrowdDemoResidualPositioningEdge> SortedEdges(Edges);
  for (FCrowdDemoResidualPositioningEdge& Edge : SortedEdges)
  {
    Edge.StableBlockerAgentIds.Sort();
    Edge.StableBlockerAgentIds.SetNum(Algo::Unique(Edge.StableBlockerAgentIds));
    Edge.ReserveBlockerAgentIds.Sort();
    Edge.ReserveBlockerAgentIds.SetNum(Algo::Unique(Edge.ReserveBlockerAgentIds));
  }
  SortedEdges.Sort([](const auto& A, const auto& B)
  {
    if (A.AgentId != B.AgentId) return A.AgentId < B.AgentId;
    if (A.PositionId != B.PositionId) return A.PositionId < B.PositionId;
    return A.HoldingId < B.HoldingId;
  });
  enum class EMode : uint8 { Current, NoStable, NoReserve, NoTarget, NoObstacle, NoFlow };
  const auto EdgeValid = [](const FCrowdDemoResidualPositioningEdge& Edge,
    const EMode Mode, const int32 RemovedBlocker)
  {
    if (!Edge.bCurrentToHoldingReachable || !Edge.bRevisionValid) return false;
    if (Mode != EMode::NoFlow && !Edge.bFlowClear) return false;
    if (Mode != EMode::NoTarget && !Edge.bTargetClear) return false;
    if (Mode != EMode::NoObstacle && !Edge.bObstacleClear) return false;
    if (Mode != EMode::NoStable)
      for (const int32 Id : Edge.StableBlockerAgentIds)
        if (Id != RemovedBlocker) return false;
    if (Mode != EMode::NoReserve)
      for (const int32 Id : Edge.ReserveBlockerAgentIds)
        if (Id != RemovedBlocker) return false;
    return true;
  };
  const auto MaximumMatching = [&](const EMode Mode, const int32 RemovedBlocker)
  {
    TMap<int32, int32> PositionOwner;
    TFunction<bool(int32, TSet<int32>&)> Augment = [&](const int32 AgentId,
      TSet<int32>& SeenPositions)
    {
      TArray<int32> CandidatePositions;
      for (const FCrowdDemoResidualPositioningEdge& Edge : SortedEdges)
        if (Edge.AgentId == AgentId && EdgeValid(Edge, Mode, RemovedBlocker))
          CandidatePositions.AddUnique(Edge.PositionId);
      CandidatePositions.Sort();
      for (const int32 PositionId : CandidatePositions)
      {
        if (SeenPositions.Contains(PositionId)) continue;
        SeenPositions.Add(PositionId);
        const int32* Owner = PositionOwner.Find(PositionId);
        if (!Owner || Augment(*Owner, SeenPositions))
        {
          PositionOwner.Add(PositionId, AgentId);
          return true;
        }
      }
      return false;
    };
    int32 Count = 0;
    for (const FCrowdDemoResidualPositioningAgent& Agent : SortedAgents)
    {
      TSet<int32> Seen;
      Count += Augment(Agent.AgentId, Seen) ? 1 : 0;
    }
    return Count;
  };
  OutSummary.UnfinishedAgentCount = SortedAgents.Num();
  OutSummary.RemainingPositionCount = SortedPositions.Num();
  for (const FCrowdDemoResidualPositioningAgent& Agent : SortedAgents)
  {
    OutSummary.AgentWithoutHoldingCount += !Agent.bHasHolding ? 1 : 0;
    bool bAnyPosition = false;
    bool bAnyRoute = false;
    for (const FCrowdDemoResidualPositioningEdge& Edge : SortedEdges)
    {
      if (Edge.AgentId != Agent.AgentId) continue;
      bAnyPosition = true;
      bAnyRoute |= EdgeValid(Edge, EMode::Current, INDEX_NONE);
    }
    OutSummary.AgentWithoutPositionEdgeCount += !bAnyPosition ? 1 : 0;
    OutSummary.AgentWithoutCommitRouteCount += !bAnyRoute ? 1 : 0;
  }
  for (const FCrowdDemoResidualPositioningEdge& Edge : SortedEdges)
  {
    const bool bValid = EdgeValid(Edge, EMode::Current, INDEX_NONE);
    OutSummary.CompatibleEdgeCount += bValid ? 1 : 0;
    OutSummary.StableBlockerEdgeRejectCount += !Edge.StableBlockerAgentIds.IsEmpty() ? 1 : 0;
    OutSummary.ReserveBlockerEdgeRejectCount += !Edge.ReserveBlockerAgentIds.IsEmpty() ? 1 : 0;
    OutSummary.TargetRejectCount += !Edge.bTargetClear ? 1 : 0;
    OutSummary.ObstacleRejectCount += !Edge.bObstacleClear ? 1 : 0;
    OutSummary.FlowRejectCount += (!Edge.bFlowClear || !Edge.bCurrentToHoldingReachable) ? 1 : 0;
    OutSummary.RevisionRejectCount += !Edge.bRevisionValid ? 1 : 0;
  }
  OutSummary.CurrentMatching = MaximumMatching(EMode::Current, INDEX_NONE);
  OutSummary.MaximumMatchingCount = OutSummary.CurrentMatching;
  OutSummary.NoStableMatching = MaximumMatching(EMode::NoStable, INDEX_NONE);
  OutSummary.NoReserveMatching = MaximumMatching(EMode::NoReserve, INDEX_NONE);
  TSet<int32> BlockerIds;
  for (const FCrowdDemoResidualPositioningEdge& Edge : SortedEdges)
  {
    BlockerIds.Append(Edge.StableBlockerAgentIds);
    BlockerIds.Append(Edge.ReserveBlockerAgentIds);
  }
  TArray<int32> SortedBlockers = BlockerIds.Array(); SortedBlockers.Sort();
  for (const int32 BlockerId : SortedBlockers)
  {
    const int32 Gain = MaximumMatching(EMode::Current, BlockerId) - OutSummary.CurrentMatching;
    OutSummary.BestSingleBlockerRemovalGain = FMath::Max(OutSummary.BestSingleBlockerRemovalGain, Gain);
    OutSummary.BlockerCriticalCount += Gain > 0 ? 1 : 0;
  }
  OutSummary.TargetLimitedCount = FMath::Max(0,
    MaximumMatching(EMode::NoTarget, INDEX_NONE) - OutSummary.CurrentMatching);
  const int32 NoObstacle = MaximumMatching(EMode::NoObstacle, INDEX_NONE);
  const int32 NoFlow = MaximumMatching(EMode::NoFlow, INDEX_NONE);
  OutSummary.GeometryLimitedCount = FMath::Max(0,
    FMath::Max(NoObstacle, NoFlow) - OutSummary.CurrentMatching);
  uint32 Hash = 2166136261u;
  const auto HashInt = [&](const int32 Value) { Hash = Fold(Hash, static_cast<uint32>(Value)); };
  for (const FCrowdDemoResidualPositioningAgent& Agent : SortedAgents)
  {
    HashInt(Agent.AgentId); HashInt(Agent.PositionId); HashInt(Agent.HoldingId);
    HashInt(Agent.TargetRevision); HashInt(static_cast<int32>(Agent.State));
  }
  for (const int32 PositionId : SortedPositions) HashInt(PositionId);
  for (const FCrowdDemoResidualPositioningEdge& Edge : SortedEdges)
  {
    HashInt(Edge.AgentId); HashInt(Edge.PositionId); HashInt(Edge.HoldingId);
    HashInt(Edge.bCurrentToHoldingReachable); HashInt(Edge.bFlowClear);
    HashInt(Edge.bTargetClear); HashInt(Edge.bObstacleClear); HashInt(Edge.bRevisionValid);
    for (const int32 Id : Edge.StableBlockerAgentIds) HashInt(Id);
    HashInt(INDEX_NONE);
    for (const int32 Id : Edge.ReserveBlockerAgentIds) HashInt(Id);
    HashInt(INDEX_NONE);
  }
  HashInt(OutSummary.CurrentMatching); HashInt(OutSummary.NoStableMatching);
  HashInt(OutSummary.NoReserveMatching); HashInt(OutSummary.BestSingleBlockerRemovalGain);
  HashInt(OutSummary.BlockerCriticalCount); HashInt(OutSummary.TargetLimitedCount);
  HashInt(OutSummary.GeometryLimitedCount);
  OutSummary.ResidualCapacityHash = Hash;
}

void FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
  const FCrowdDemoPursuitTargetFact& Target,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const FCrowdDemoSharedFlowField& FlowField,
  const TConstArrayView<FCrowdDemoCommitRequest> Requests,
  const TConstArrayView<FCrowdDemoPositionIngressBlocker> OccupiedBlockers,
  FCrowdDemoCommitGateResult& OutResult)
{
  OutResult = {};
  const auto RejectBit = [](const ECrowdDemoCommitRejectReason Reason)
  { return static_cast<uint32>(Reason); };
  TArray<FCrowdDemoCommitRequest> SortedRequests(Requests);
  SortedRequests.Sort([](const auto& A, const auto& B)
  {
    if (A.bAlreadyCommit != B.bAlreadyCommit) return A.bAlreadyCommit;
    if (A.WaitEpoch != B.WaitEpoch) return A.WaitEpoch > B.WaitEpoch;
    if (A.PositionFillCost != B.PositionFillCost)
      return A.PositionFillCost < B.PositionFillCost;
    if (A.QuantizedCommitCostCm != B.QuantizedCommitCostCm)
      return A.QuantizedCommitCostCm < B.QuantizedCommitCostCm;
    if (A.PositionId != B.PositionId) return A.PositionId < B.PositionId;
    return A.AgentId < B.AgentId;
  });
  TArray<FCrowdDemoCommitRequest> Active;
  TArray<FCrowdDemoCommitRequest> Selected;
  struct FCommitSegmentValidity
  {
    bool bTargetClear = true;
    bool bFlowClear = true;
    bool bObstacleClear = true;
    bool bStableClear = true;
    bool bReserveClear = true;
    bool IsHardValid() const { return bTargetClear && bFlowClear && bObstacleClear; }
  };
  const auto EvaluateSegment = [&](const FCrowdDemoCommitRequest& Request)
  {
    FCommitSegmentValidity Validity;
    const float TargetExclusion = Target.RadiusCm + Request.RadiusCm + Settings.SafetyGapCm;
    if (SegmentIntersectsSafetyCircle(Request.Location, Request.AssignedPosition,
      Target.Location, TargetExclusion - 1.0f)) Validity.bTargetClear = false;
    FCrowdDemoSharedFlowFieldConfig ClearanceConfig = FlowField.Config;
    ClearanceConfig.AgentInflateCm = Request.RadiusCm + Settings.SafetyGapCm;
    const int32 LengthCm = FMath::RoundToInt(
      (Request.AssignedPosition - Request.Location).Size());
    const float Spacing = FMath::Max(25.0f, FlowField.Config.CellSizeCm * 0.5f);
    const int32 Samples = FMath::Max(1, FMath::CeilToInt(LengthCm / Spacing));
    for (int32 Index = 0; Index <= Samples; ++Index)
    {
      const FVector2f Point = FMath::Lerp(Request.Location, Request.AssignedPosition,
        static_cast<float>(Index) / static_cast<float>(Samples));
      const FVector Point3(Point.X, Point.Y, 60.0f);
      if (FCrowdDemoSharedFlowFieldKernel::Sample(FlowField, Point3).Status
        != ECrowdDemoFlowLocationStatus::Reachable) Validity.bFlowClear = false;
      if (FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        ClearanceConfig, Point3)) Validity.bObstacleClear = false;
    }
    for (const FCrowdDemoPositionIngressBlocker& Blocker : OccupiedBlockers)
    {
      if (Blocker.State != ECrowdDemoPursuitPositionState::StableOccupied
        && Blocker.State != ECrowdDemoPursuitPositionState::ReserveHold) continue;
      if (SegmentIntersectsSafetyCircle(Request.Location, Request.AssignedPosition,
        Blocker.Location, Request.RadiusCm + Blocker.RadiusCm + Settings.SafetyGapCm))
      {
        if (Blocker.State == ECrowdDemoPursuitPositionState::StableOccupied)
          Validity.bStableClear = false;
        else Validity.bReserveClear = false;
      }
    }
    return Validity;
  };
  const auto Conflicts = [&](const FCrowdDemoCommitRequest& A,
    const FCrowdDemoCommitRequest& B)
  {
    const TArray<FVector2f> ARoute = {A.Location, A.AssignedPosition};
    const TArray<FVector2f> BRoute = {B.Location, B.AssignedPosition};
    return FrontReservationPathsConflict(Settings, ARoute, A.RadiusCm,
      BRoute, B.RadiusCm);
  };
  for (const FCrowdDemoCommitRequest& Request : SortedRequests)
  {
    FCrowdDemoCommitDecisionRecord Record;
    Record.AgentId = Request.AgentId;
    if (!Request.bPositionValid || Request.TargetRevision != Target.Revision
      || !Request.bCompatibilityFound || !Request.bCompatibilityValid)
    {
      Record.Decision = ECrowdDemoCommitDecision::Reacquire;
      if (!Request.bPositionValid)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::InvalidPosition);
      if (Request.bPositionValid && Request.TargetRevision != Target.Revision)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::TargetRevision);
      if (Request.bPositionValid && Request.TargetRevision == Target.Revision
        && !Request.bCompatibilityFound)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::CompatibilityMissing);
      if (Request.bPositionValid && Request.TargetRevision == Target.Revision
        && Request.bCompatibilityFound && !Request.bCompatibilityValid)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::CompatibilityRejected);
      ++OutResult.ReacquireCount;
      OutResult.InvalidPositionCount += !Request.bPositionValid ? 1 : 0;
      OutResult.TargetRevisionMismatchCount += Request.bPositionValid
        && Request.TargetRevision != Target.Revision ? 1 : 0;
      OutResult.CompatibilityMissingCount += Request.bPositionValid
        && Request.TargetRevision == Target.Revision && !Request.bCompatibilityFound ? 1 : 0;
      OutResult.CompatibilityRejectedCount += Request.bPositionValid
        && Request.TargetRevision == Target.Revision && Request.bCompatibilityFound
        && !Request.bCompatibilityValid ? 1 : 0;
      OutResult.Decisions.Add(Record);
      continue;
    }
    if (Request.bAlreadyCommit)
    {
      Record.Decision = ECrowdDemoCommitDecision::Granted;
      Active.Add(Request);
      ++OutResult.ActiveCommitCount;
      OutResult.GrantedAgentIds.Add(Request.AgentId);
      OutResult.Decisions.Add(Record);
      continue;
    }
    const bool bDistanceReady = (Request.Location - Request.HoldingLocation).Size()
      <= Settings.HoldingToleranceCm;
    const bool bSpeedReady = Request.Velocity.Size() <= Settings.HoldingReadinessSpeedCmps;
    const bool bReady = bDistanceReady && bSpeedReady;
    if (!bReady)
    {
      Record.Decision = ECrowdDemoCommitDecision::Held;
      if (!bDistanceReady)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::HoldingDistance);
      if (!bSpeedReady)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::HoldingSpeed);
      ++OutResult.HeldCount;
      OutResult.HoldingDistanceNotReadyCount += !bDistanceReady ? 1 : 0;
      OutResult.HoldingSpeedNotReadyCount += !bSpeedReady ? 1 : 0;
      OutResult.Decisions.Add(Record);
      continue;
    }
    ++OutResult.ReadyRequestCount;
    const FCommitSegmentValidity Segment = EvaluateSegment(Request);
    Record.YieldableConflictMask |= !Segment.bStableClear
      ? RejectBit(ECrowdDemoCommitRejectReason::StableBlocker) : 0u;
    Record.YieldableConflictMask |= !Segment.bReserveClear
      ? RejectBit(ECrowdDemoCommitRejectReason::ReserveBlocker) : 0u;
    OutResult.YieldableStableConflictCount += !Segment.bStableClear ? 1 : 0;
    OutResult.YieldableReserveConflictCount += !Segment.bReserveClear ? 1 : 0;
    bool bConflict = !Segment.IsHardValid();
    bool bActiveConflict = false;
    bool bSelectedConflict = false;
    if (!bConflict)
      for (const FCrowdDemoCommitRequest& Occupied : Active)
        if (Conflicts(Request, Occupied)) { bActiveConflict = true; bConflict = true; break; }
    if (!bConflict)
      for (const FCrowdDemoCommitRequest& Occupied : Selected)
        if (Conflicts(Request, Occupied)) { bSelectedConflict = true; bConflict = true; break; }
    if (bConflict)
    {
      Record.Decision = ECrowdDemoCommitDecision::Held;
      if (!Segment.bTargetClear)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::TargetExclusion);
      if (!Segment.bFlowClear)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::Flow);
      if (!Segment.bObstacleClear)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::Obstacle);
      if (bActiveConflict)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::ActiveCommitConflict);
      if (bSelectedConflict)
        Record.RejectReasonMask |= RejectBit(ECrowdDemoCommitRejectReason::SelectedCommitConflict);
      ++OutResult.HeldCount;
      ++OutResult.ReadyConflictHeldCount;
      OutResult.ReadyTargetRejectCount += !Segment.bTargetClear ? 1 : 0;
      OutResult.ReadyFlowRejectCount += !Segment.bFlowClear ? 1 : 0;
      OutResult.ReadyObstacleRejectCount += !Segment.bObstacleClear ? 1 : 0;
      OutResult.ReadyActiveCommitConflictCount += bActiveConflict ? 1 : 0;
      OutResult.ReadySelectedConflictCount += bSelectedConflict ? 1 : 0;
      ++OutResult.HardConflictHeldCount;
    }
    else
    {
      Record.Decision = ECrowdDemoCommitDecision::Granted;
      ++OutResult.ReadyGrantedCount;
      Selected.Add(Request);
      OutResult.GrantedAgentIds.Add(Request.AgentId);
    }
    OutResult.Decisions.Add(Record);
  }
  OutResult.Decisions.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  OutResult.GrantedAgentIds.Sort();
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoCommitDecisionRecord& Decision : OutResult.Decisions)
  {
    Hash = Fold(Hash, static_cast<uint32>(Decision.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Decision.Decision));
    Hash = Fold(Hash, Decision.RejectReasonMask);
    Hash = Fold(Hash, Decision.YieldableConflictMask);
  }
  OutResult.DecisionHash = Hash;
}

void FCrowdDemoPursuitPositioningKernel::BuildUnfinishedBoundaryFixture(
  const TConstArrayView<FCrowdDemoSf4UnfinishedAgentDiagnosticInput> Inputs,
  FCrowdDemoSf4UnfinishedBoundaryFixture& OutFixture)
{
  OutFixture = {};
  TArray<FCrowdDemoSf4UnfinishedAgentDiagnosticInput> Sorted(Inputs);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 Hash = 2166136261u;
  int32 PreviousAgentId = INDEX_NONE;
  for (const auto& Input : Sorted)
  {
    if (Input.AgentId == INDEX_NONE || Input.AgentId == PreviousAgentId)
      return;
    PreviousAgentId = Input.AgentId;
    if (Input.State == ECrowdDemoPursuitSteeringState::StableOccupied
      || Input.State == ECrowdDemoPursuitSteeringState::ReserveHold)
      continue;
    auto& Agent = OutFixture.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Input.AgentId;
    Agent.State = Input.State;
    Agent.DistanceCm = FMath::RoundToInt((Input.Destination - Input.Location).Size());
    Agent.PreferredVelocityCmps = FIntPoint(FMath::RoundToInt(Input.PreferredVelocity.X),
      FMath::RoundToInt(Input.PreferredVelocity.Y));
    Agent.OrcaVelocityCmps = FIntPoint(FMath::RoundToInt(Input.OrcaVelocity.X),
      FMath::RoundToInt(Input.OrcaVelocity.Y));
    Agent.FinalVelocityCmps = FIntPoint(FMath::RoundToInt(Input.FinalVelocity.X),
      FMath::RoundToInt(Input.FinalVelocity.Y));
    Agent.OrcaConstraintSourceCounts = Input.OrcaConstraintSourceCounts;
    Agent.OrcaConstraintSourceCounts.SetNum(6);
    Agent.CommitRejectReasonMask = Input.CommitRejectReasonMask;
    Agent.NoProgressSteps = FMath::Max(0, Input.NoProgressSteps);
    Hash = Fold(Hash, static_cast<uint32>(Agent.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Agent.State));
    Hash = Fold(Hash, static_cast<uint32>(Agent.DistanceCm));
    Hash = Fold(Hash, static_cast<uint32>(Agent.PreferredVelocityCmps.X));
    Hash = Fold(Hash, static_cast<uint32>(Agent.PreferredVelocityCmps.Y));
    Hash = Fold(Hash, static_cast<uint32>(Agent.OrcaVelocityCmps.X));
    Hash = Fold(Hash, static_cast<uint32>(Agent.OrcaVelocityCmps.Y));
    Hash = Fold(Hash, static_cast<uint32>(Agent.FinalVelocityCmps.X));
    Hash = Fold(Hash, static_cast<uint32>(Agent.FinalVelocityCmps.Y));
    for (const int32 Count : Agent.OrcaConstraintSourceCounts)
      Hash = Fold(Hash, static_cast<uint32>(Count));
    Hash = Fold(Hash, Agent.CommitRejectReasonMask);
    Hash = Fold(Hash, static_cast<uint32>(Agent.NoProgressSteps));
  }
  OutFixture.StableHash = Hash;
  OutFixture.bValid = !OutFixture.Agents.IsEmpty();
}

void FCrowdDemoPursuitPositioningKernel::BuildPhysicalUnsatisfiedBoundaryFixture(
  const TConstArrayView<FCrowdDemoSf4PhysicalUnsatisfiedAgentInput> Inputs,
  FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture& OutFixture)
{
  OutFixture = {};
  TArray<FCrowdDemoSf4PhysicalUnsatisfiedAgentInput> Sorted(Inputs);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  OutFixture.TotalAgentCount = Sorted.Num();
  uint32 Hash = 2166136261u;
  int32 PreviousAgentId = INDEX_NONE;
  const auto QuantizeVelocity = [](const FVector2f Value)
  {
    return FIntPoint(FMath::RoundToInt(Value.X), FMath::RoundToInt(Value.Y));
  };
  for (const FCrowdDemoSf4PhysicalUnsatisfiedAgentInput& Input : Sorted)
  {
    if (Input.AgentId == INDEX_NONE || Input.AgentId == PreviousAgentId)
      return;
    PreviousAgentId = Input.AgentId;
    OutFixture.PhysicallySatisfiedCount += Input.bPhysicallySatisfied ? 1 : 0;
    if (Input.bPhysicallySatisfied)
      continue;

    FCrowdDemoSf4PhysicalUnsatisfiedAgentDiagnostic& Agent =
      OutFixture.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Input.AgentId;
    Agent.State = Input.State;
    Agent.PositionId = Input.PositionId;
    Agent.HoldingId = Input.HoldingId;
    Agent.InvalidReason = Input.InvalidReason;
    Agent.DistanceCm = FMath::RoundToInt((Input.Destination - Input.Location).Size());
    Agent.PreferredVelocityCmps = QuantizeVelocity(Input.PreferredVelocity);
    Agent.OrcaVelocityCmps = QuantizeVelocity(Input.OrcaVelocity);
    Agent.ObstacleVelocityCmps = QuantizeVelocity(Input.ObstacleVelocity);
    Agent.PbdVelocityCmps = QuantizeVelocity(Input.PbdVelocity);
    Agent.ReprojectVelocityCmps = QuantizeVelocity(Input.ReprojectVelocity);
    Agent.FinalVelocityCmps = QuantizeVelocity(Input.FinalVelocity);
    Agent.CommitRejectReasonMask = Input.CommitRejectReasonMask;
    Agent.CommitYieldableConflictMask = Input.CommitYieldableConflictMask;

    Hash = Fold(Hash, static_cast<uint32>(Agent.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Agent.State));
    Hash = Fold(Hash, static_cast<uint32>(Agent.PositionId));
    Hash = Fold(Hash, static_cast<uint32>(Agent.HoldingId));
    Hash = Fold(Hash, static_cast<uint32>(Agent.InvalidReason));
    Hash = Fold(Hash, static_cast<uint32>(Agent.DistanceCm));
    const FIntPoint Velocities[] = { Agent.PreferredVelocityCmps,
      Agent.OrcaVelocityCmps, Agent.ObstacleVelocityCmps, Agent.PbdVelocityCmps,
      Agent.ReprojectVelocityCmps, Agent.FinalVelocityCmps };
    for (const FIntPoint Velocity : Velocities)
    {
      Hash = Fold(Hash, static_cast<uint32>(Velocity.X));
      Hash = Fold(Hash, static_cast<uint32>(Velocity.Y));
    }
    Hash = Fold(Hash, Agent.CommitRejectReasonMask);
    Hash = Fold(Hash, Agent.CommitYieldableConflictMask);
  }
  Hash = Fold(Hash, static_cast<uint32>(OutFixture.TotalAgentCount));
  Hash = Fold(Hash, static_cast<uint32>(OutFixture.PhysicallySatisfiedCount));
  Hash = Fold(Hash, static_cast<uint32>(OutFixture.Agents.Num()));
  OutFixture.bCountClosed = OutFixture.Agents.Num()
    == OutFixture.TotalAgentCount - OutFixture.PhysicallySatisfiedCount;
  Hash = Fold(Hash, OutFixture.bCountClosed ? 1u : 0u);
  OutFixture.StableHash = Hash;
  OutFixture.bValid = OutFixture.TotalAgentCount > 0 && OutFixture.bCountClosed;
}

FVector2f FCrowdDemoPursuitPositioningKernel::BuildSteeringFirstPreferredVelocity(
  const ECrowdDemoPursuitSteeringState State,
  const FVector2f CurrentLocation,
  const FVector2f FlowPreferredVelocity,
  const FVector2f HoldingLocation,
  const FVector2f AssignedPosition,
  const float MaxSpeedCmps,
  const FCrowdDemoPursuitPositioningSettings& Settings)
{
  const float MaxSpeed = FMath::Max(0.0f, MaxSpeedCmps);
  const auto ClampAndQuantize = [&](FVector2f Velocity)
  {
    if (Velocity.SizeSquared() > FMath::Square(MaxSpeed) && !Velocity.IsNearlyZero())
      Velocity = Velocity.GetSafeNormal() * MaxSpeed;
    const float Quantum = FMath::Max(0.001f, Settings.SteeringVelocityQuantumCmps);
    return FVector2f(FMath::RoundToFloat(Velocity.X / Quantum) * Quantum,
      FMath::RoundToFloat(Velocity.Y / Quantum) * Quantum);
  };
  const auto Arrive = [&](const FVector2f Destination, const bool bLowGain)
  {
    const FVector2f Delta = Destination - CurrentLocation;
    const float Distance = Delta.Size();
    if (Distance <= Settings.HoldingToleranceCm || Distance <= KINDA_SMALL_NUMBER)
      return FVector2f::ZeroVector;
    const float Speed = bLowGain
      ? FMath::Min(MaxSpeed, Distance * Settings.StableHoldGainPerSecond)
      : FMath::Min(MaxSpeed, MaxSpeed * Distance
        / FMath::Max(1.0f, Settings.HoldingArriveSlowdownDistanceCm));
    return ClampAndQuantize(Delta / Distance * Speed);
  };
  switch (State)
  {
  case ECrowdDemoPursuitSteeringState::Holding:
    return Arrive(HoldingLocation, false);
  case ECrowdDemoPursuitSteeringState::Commit:
    return Arrive(AssignedPosition, false);
  case ECrowdDemoPursuitSteeringState::StableOccupied:
    return Arrive(AssignedPosition, true);
  case ECrowdDemoPursuitSteeringState::ReserveHold:
    return Arrive(HoldingLocation, true);
  case ECrowdDemoPursuitSteeringState::Pursuit:
  case ECrowdDemoPursuitSteeringState::Reacquire:
  default:
    return ClampAndQuantize(FlowPreferredVelocity);
  }
}

bool FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
  const FVector2f CurrentLocation,
  const FVector2f HoldingLocation,
  const ECrowdDemoFlowLocationStatus FlowStatus,
  const FCrowdDemoPursuitPositioningSettings& Settings,
  const FCrowdDemoSharedFlowFieldConfig& FlowConfig)
{
  if (FlowStatus != ECrowdDemoFlowLocationStatus::Reachable) return false;
  const float HandoffRange = FMath::Max(0.0f, Settings.FrontAdmissionHoldRangeCm);
  if ((HoldingLocation - CurrentLocation).SizeSquared() > FMath::Square(HandoffRange))
    return false;
  const FCrowdDemoSharedFlowConstraintDiagnostic Diagnostic =
    FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
      FlowConfig,
      FVector(CurrentLocation.X, CurrentLocation.Y, 0.0f),
      FVector(HoldingLocation.X, HoldingLocation.Y, 0.0f),
      true);
  return Diagnostic.bValid
    && !Diagnostic.bHitFlowBounds
    && Diagnostic.bDirectSegmentClear
    && !Diagnostic.bEndInsideAnyObstacle;
}

int32 FCrowdDemoPursuitPositioningKernel::ComputePositionFillCost(
  const FCrowdDemoPursuitTargetFact& Target,
  FVector2f EntryAxis,
  const FCrowdDemoPositionCandidate& Position)
{
  EntryAxis = EntryAxis.GetSafeNormal();
  if (EntryAxis.IsNearlyZero()) EntryAxis = FVector2f(0.0f, -1.0f);
  const FVector2f QuantizedOffset(
    FMath::RoundToFloat(Position.WorldLocation.X - Target.Location.X),
    FMath::RoundToFloat(Position.WorldLocation.Y - Target.Location.Y));
  return FMath::RoundToInt(FVector2f::DotProduct(QuantizedOffset, EntryAxis));
}
