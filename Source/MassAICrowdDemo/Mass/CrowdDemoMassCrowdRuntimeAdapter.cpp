#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"

namespace
{
  FCrowdGuidanceCandidate ToCoreCandidate(
    const FCrowdDemoGuidanceCandidate& Source)
  {
    FCrowdGuidanceCandidate Target;
    Target.AgentId = Source.AgentId;
    Target.Provider = static_cast<ECrowdGuidanceProvider>(
      static_cast<uint8>(Source.Provider));
    Target.PlanRevision = Source.PlanRevision;
    Target.PreferredVelocity = Source.PreferredVelocity;
    Target.DesiredLocation = Source.DesiredLocation;
    Target.DesiredYawDegrees = Source.DesiredYawDegrees;
    Target.StableHash = Source.StableHash;
    Target.bValid = Source.bValid;
    return Target;
  }

  FCrowdDemoLocalPredictiveTraceVelocity ToDemoTraceVelocity(
    const FCrowdLocalPredictiveTraceVelocity& Source)
  {
    FCrowdDemoLocalPredictiveTraceVelocity Target;
    Target.AgentId = Source.AgentId;
    Target.Velocity = Source.Velocity;
    return Target;
  }

  FCrowdDemoLocalPredictiveComponentTrace ToDemoComponentTrace(
    const FCrowdLocalPredictiveComponentTrace& Source)
  {
    FCrowdDemoLocalPredictiveComponentTrace Target;
    Target.ComponentKey = Source.ComponentKey;
    Target.AgentIds = Source.AgentIds;
    Target.GrantedAgentId = Source.GrantedAgentId;
    Target.CommonVelocity = Source.CommonVelocity;
    Target.SafeAlphaQ15 = Source.SafeAlphaQ15;
    Target.bCommonVelocityValid = Source.bCommonVelocityValid;
    Target.bFullJointVelocitySafe = Source.bFullJointVelocitySafe;
    Target.bCoherentTranslationApplied = Source.bCoherentTranslationApplied;
    Target.CoherentTranslation = Source.CoherentTranslation;
    Target.bJointPreferredRecoveryApplied =
      Source.bJointPreferredRecoveryApplied;
    for (const auto& Value : Source.PreTranslationVelocities)
      Target.PreTranslationVelocities.Add(ToDemoTraceVelocity(Value));
    for (const auto& Value : Source.PreRecoveryVelocities)
      Target.PreRecoveryVelocities.Add(ToDemoTraceVelocity(Value));
    for (const auto& Value : Source.RecoveredVelocities)
      Target.RecoveredVelocities.Add(ToDemoTraceVelocity(Value));
    for (const auto& Value : Source.JointProjectedVelocities)
      Target.JointProjectedVelocities.Add(ToDemoTraceVelocity(Value));
    for (const auto& Value : Source.FinalVelocities)
      Target.FinalVelocities.Add(ToDemoTraceVelocity(Value));
    return Target;
  }

  FCrowdDemoParticleEnvironmentContact ToDemoParticleContact(
    const FCrowdParticleEnvironmentContact& Source)
  {
    FCrowdDemoParticleEnvironmentContact Target;
    Target.AgentId = Source.AgentId;
    Target.AgentIndex = Source.AgentIndex;
    Target.EnvironmentId = Source.EnvironmentId;
    Target.ContactKind = static_cast<ECrowdDemoParticleEnvironmentContactKind>(
      static_cast<uint8>(Source.ContactKind));
    Target.Face = static_cast<ECrowdDemoParticleEnvironmentFace>(
      static_cast<uint8>(Source.Face));
    Target.ClosestPoint = Source.ClosestPoint;
    Target.CorrectionNormal = Source.CorrectionNormal;
    Target.HardDistanceCm = Source.HardDistanceCm;
    Target.SoftDistanceCm = Source.SoftDistanceCm;
    Target.SoftErrorCm = Source.SoftErrorCm;
    Target.HardDeficitCm = Source.HardDeficitCm;
    Target.SweptTime = Source.SweptTime;
    Target.ConstraintThreshold = Source.ConstraintThreshold;
    return Target;
  }

  FCrowdDemoParticleHardConstraint ToDemoParticleConstraint(
    const FCrowdParticleHardConstraint& Source)
  {
    FCrowdDemoParticleHardConstraint Target;
    Target.Kind = static_cast<ECrowdDemoParticleHardConstraintKind>(
      static_cast<uint8>(Source.Kind));
    Target.MinAgentId = Source.MinAgentId;
    Target.MaxAgentId = Source.MaxAgentId;
    Target.MinAgentIndex = Source.MinAgentIndex;
    Target.MaxAgentIndex = Source.MaxAgentIndex;
    Target.EnvironmentId = Source.EnvironmentId;
    Target.Face = static_cast<ECrowdDemoParticleEnvironmentFace>(
      static_cast<uint8>(Source.Face));
    Target.Normal = Source.Normal;
    Target.CoefficientScale = Source.CoefficientScale;
    Target.Threshold = Source.Threshold;
    Target.InitialDeficitCm = Source.InitialDeficitCm;
    return Target;
  }

  FCrowdDemoParticleSafetyStageTrace ToDemoParticleSafetyStage(
    const FCrowdParticleSafetyStageTrace& Source)
  {
    FCrowdDemoParticleSafetyStageTrace Target;
    Target.Iteration = Source.Iteration;
    Target.Stage = static_cast<ECrowdDemoParticleSafetyStage>(
      static_cast<uint8>(Source.Stage));
    Target.HardPairViolationCount = Source.HardPairViolationCount;
    Target.SweptPairViolationCount = Source.SweptPairViolationCount;
    Target.ObstacleViolationCount = Source.ObstacleViolationCount;
    Target.BoundsViolationCount = Source.BoundsViolationCount;
    Target.MinimumEndpointMarginCm = Source.MinimumEndpointMarginCm;
    Target.MinimumSweptMarginCm = Source.MinimumSweptMarginCm;
    Target.MaximumEnvironmentDeficitCm = Source.MaximumEnvironmentDeficitCm;
    Target.PositionHash = Source.PositionHash;
    return Target;
  }

  FCrowdDemoParticleSoftPairInfluence ToDemoParticleInfluence(
    const FCrowdParticleSoftPairInfluence& Source)
  {
    FCrowdDemoParticleSoftPairInfluence Target;
    Target.MinAgentId = Source.MinAgentId;
    Target.MaxAgentId = Source.MaxAgentId;
    Target.RequestedCorrectionA = Source.RequestedCorrectionA;
    Target.RequestedCorrectionB = Source.RequestedCorrectionB;
    Target.RealizedCorrectionA = Source.RealizedCorrectionA;
    Target.RealizedCorrectionB = Source.RealizedCorrectionB;
    return Target;
  }
}

bool FCrowdDemoMassCrowdRuntimeAdapter::BuildBoundaryAgentRecord(
  const FCrowdDemoMassIdentityFragment& Identity,
  const FCrowdDemoRoundSimStateFragment& State,
  const FCrowdDemoMassMovementFragment& Movement,
  const FCrowdDemoParticlePropertiesFragment& Particle,
  FCrowdMassBoundaryAgentRecord& OutRecord)
{
  OutRecord = {};
  if (Identity.Id == INDEX_NONE || Identity.LifecycleSerial <= 0
    || !State.bInitialized
    || !FMath::IsFinite(State.Location.X)
    || !FMath::IsFinite(State.Location.Y)
    || !FMath::IsFinite(State.Location.Z)
    || !FMath::IsFinite(State.Velocity.X)
    || !FMath::IsFinite(State.Velocity.Y)
    || !FMath::IsFinite(State.Velocity.Z)
    || !FMath::IsFinite(State.YawDegrees)
    || !FMath::IsFinite(Particle.PhysicalRadiusCm)
    || !FMath::IsFinite(Particle.HardSafetyGapCm)
    || !FMath::IsFinite(Particle.SoftMarginCm)
    || !FMath::IsFinite(Particle.Mobility)
    || !FMath::IsFinite(Movement.MaxSpeedCmPerSecond)
    || Particle.PhysicalRadiusCm <= 0.0f
    || Particle.HardSafetyGapCm < 0.0f
    || Particle.SoftMarginCm < 0.0f
    || Particle.Mobility < 0.0f
    || Movement.MaxSpeedCmPerSecond < 0.0f)
    return false;
  OutRecord.Identity.AgentId = Identity.Id;
  OutRecord.Identity.LifecycleSerial = Identity.LifecycleSerial;
  OutRecord.State.Position = State.Location;
  OutRecord.State.Velocity = State.Velocity;
  OutRecord.State.YawDegrees = State.YawDegrees;
  OutRecord.State.PlanRevision = State.PlanRevision;
  OutRecord.State.bInitialized = State.bInitialized;
  OutRecord.Properties.PhysicalRadiusCm = Particle.PhysicalRadiusCm;
  OutRecord.Properties.HardSafetyGapCm = Particle.HardSafetyGapCm;
  OutRecord.Properties.SoftMarginCm = Particle.SoftMarginCm;
  OutRecord.Properties.Mobility = Particle.Mobility;
  OutRecord.Properties.MaximumSpeedCmps = Movement.MaxSpeedCmPerSecond;
  OutRecord.Properties.CapabilityProfileKey = Particle.CapabilityProfileKey;
  return true;
}

bool FCrowdDemoMassCrowdRuntimeAdapter::BuildGatherRecord(
  const FCrowdDemoMassIdentityFragment& Identity,
  const FCrowdDemoRoundSimStateFragment& State,
  const FCrowdDemoMassMovementFragment& Movement,
  const FCrowdDemoParticlePropertiesFragment& Particle,
  const FCrowdDemoRoundGuidanceCandidatesFragment& Guidance,
  FCrowdMassGatherRecord& OutRecord)
{
  OutRecord = {};
  FCrowdMassBoundaryAgentRecord Base;
  if (!BuildBoundaryAgentRecord(Identity, State, Movement, Particle, Base))
    return false;
  OutRecord.Identity = Base.Identity;
  OutRecord.State = Base.State;
  OutRecord.Properties = Base.Properties;
  OutRecord.Guidance.SharedFlow = ToCoreCandidate(Guidance.SharedFlow);
  OutRecord.Guidance.TargetRegion = ToCoreCandidate(Guidance.TargetRegion);
  OutRecord.Guidance.BusinessOverride = ToCoreCandidate(
    Guidance.BusinessOverride);
  return true;
}

FCrowdMassCommitTarget FCrowdDemoMassCrowdRuntimeAdapter::BuildCommitTarget(
  const FCrowdDemoMassIdentityFragment& Identity)
{
  FCrowdMassCommitTarget Target;
  Target.AgentId = Identity.Id;
  Target.LifecycleSerial = static_cast<uint32>(Identity.LifecycleSerial);
  return Target;
}

FCrowdDemoGuidanceCandidate
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoGuidanceCandidate(
  const FCrowdGuidanceCandidate& Source)
{
  FCrowdDemoGuidanceCandidate Target;
  Target.AgentId = Source.AgentId;
  Target.Provider = static_cast<ECrowdDemoGuidanceProvider>(
    static_cast<uint8>(Source.Provider));
  Target.PlanRevision = Source.PlanRevision;
  Target.PreferredVelocity = Source.PreferredVelocity;
  Target.DesiredLocation = Source.DesiredLocation;
  Target.DesiredYawDegrees = Source.DesiredYawDegrees;
  Target.StableHash = Source.StableHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdGuidanceCandidate
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
  const FCrowdDemoGuidanceCandidate& Source)
{
  return ToCoreCandidate(Source);
}

FCrowdSharedFlowFieldConfig
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
  const FCrowdDemoSharedFlowFieldConfig& Source)
{
  FCrowdSharedFlowFieldConfig Target;
  Target.Revision = Source.Revision;
  Target.BoundsMin = Source.BoundsMin;
  Target.BoundsMax = Source.BoundsMax;
  Target.CellSizeCm = Source.CellSizeCm;
  Target.AgentInflateCm = Source.AgentInflateCm;
  Target.ConnectivityContractVersion = Source.ConnectivityContractVersion;
  Target.GoalLocation = Source.GoalLocation;
  for (const FCrowdDemoSharedFlowObstacleSpec& Obstacle : Source.ObstacleSpecs)
  {
    FCrowdSharedFlowObstacleSpec& Added =
      Target.ObstacleSpecs.AddDefaulted_GetRef();
    Added.ObstacleId = Obstacle.ObstacleId;
    Added.Center = Obstacle.Center;
    Added.Extent = Obstacle.Extent;
  }
  return Target;
}

FCrowdDemoSharedFlowField
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(
  const FCrowdSharedFlowField& Source)
{
  FCrowdDemoSharedFlowField Target;
  Target.Config.Revision = Source.Config.Revision;
  Target.Config.BoundsMin = Source.Config.BoundsMin;
  Target.Config.BoundsMax = Source.Config.BoundsMax;
  Target.Config.CellSizeCm = Source.Config.CellSizeCm;
  Target.Config.AgentInflateCm = Source.Config.AgentInflateCm;
  Target.Config.ConnectivityContractVersion =
    Source.Config.ConnectivityContractVersion;
  Target.Config.GoalLocation = Source.Config.GoalLocation;
  for (const FCrowdSharedFlowObstacleSpec& Obstacle : Source.Config.ObstacleSpecs)
  {
    FCrowdDemoSharedFlowObstacleSpec& Added =
      Target.Config.ObstacleSpecs.AddDefaulted_GetRef();
    Added.ObstacleId = Obstacle.ObstacleId;
    Added.Center = Obstacle.Center;
    Added.Extent = Obstacle.Extent;
  }
  Target.IntegrationCost = Source.IntegrationCost;
  Target.FlowDirection = Source.FlowDirection;
  Target.NextCellIndex = Source.NextCellIndex;
  Target.Blocked = Source.Blocked;
  Target.Unreachable = Source.Unreachable;
  for (const FCrowdNavigationSafeInterval& Value : Source.NavigationSafeIntervals)
  {
    FCrowdDemoNavigationSafeInterval& Added =
      Target.NavigationSafeIntervals.AddDefaulted_GetRef();
    Added.Kind = static_cast<ECrowdDemoNavigationNodeKind>(Value.Kind);
    Added.PrimaryCellKey = Value.PrimaryCellKey;
    Added.SecondaryCellKey = Value.SecondaryCellKey;
    Added.IntervalOrdinal = Value.IntervalOrdinal;
    Added.QuantizedMinCm = Value.QuantizedMinCm;
    Added.QuantizedMaxCm = Value.QuantizedMaxCm;
  }
  for (const FCrowdNavigationNode& Value : Source.NavigationNodes)
  {
    FCrowdDemoNavigationNode& Added = Target.NavigationNodes.AddDefaulted_GetRef();
    Added.StableNodeKey = Value.StableNodeKey;
    Added.Kind = static_cast<ECrowdDemoNavigationNodeKind>(Value.Kind);
    Added.PrimaryCellKey = Value.PrimaryCellKey;
    Added.SecondaryCellKey = Value.SecondaryCellKey;
    Added.IntervalOrdinal = Value.IntervalOrdinal;
    Added.QuantizedLocationCm = Value.QuantizedLocationCm;
  }
  Target.NavigationCellNodes = Source.NavigationCellNodes;
  for (const FCrowdNavigationEdge& Value : Source.NavigationEdges)
  {
    FCrowdDemoNavigationEdge& Added = Target.NavigationEdges.AddDefaulted_GetRef();
    Added.MinNodeKey = Value.MinNodeKey;
    Added.MaxNodeKey = Value.MaxNodeKey;
    Added.QuantizedCost = Value.QuantizedCost;
  }
  Target.NavigationIntegrationCost = Source.NavigationIntegrationCost;
  Target.NavigationNextNodeIndex = Source.NavigationNextNodeIndex;
  Target.GoalAttachmentNodeIndices = Source.GoalAttachmentNodeIndices;
  Target.Width = Source.Width;
  Target.Height = Source.Height;
  Target.GoalCellIndex = Source.GoalCellIndex;
  Target.BlockedCellCount = Source.BlockedCellCount;
  Target.ValidDirectedEdgeCount = Source.ValidDirectedEdgeCount;
  Target.NavigationCenterAnchorCount = Source.NavigationCenterAnchorCount;
  Target.NavigationConnectionPointCount = Source.NavigationConnectionPointCount;
  Target.NavigationSafeIntervalCount = Source.NavigationSafeIntervalCount;
  Target.NavigationInternalEdgeCount = Source.NavigationInternalEdgeCount;
  Target.CenterInvalidButConnectedCellCount =
    Source.CenterInvalidButConnectedCellCount;
  Target.GoalAttachmentCount = Source.GoalAttachmentCount;
  Target.TopologyHash = Source.TopologyHash;
  Target.IntegrationHash = Source.IntegrationHash;
  Target.BuildHash = Source.BuildHash;
  return Target;
}

FCrowdDemoSharedFlowSample
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowSample(
  const FCrowdSharedFlowSample& Source)
{
  FCrowdDemoSharedFlowSample Target;
  Target.CellIndex = Source.CellIndex;
  Target.StableCellKey = Source.StableCellKey;
  Target.Status = static_cast<ECrowdDemoFlowLocationStatus>(Source.Status);
  Target.FlowDirection = Source.FlowDirection;
  Target.IntegrationCost = Source.IntegrationCost;
  Target.NavigationNodeKey = Source.NavigationNodeKey;
  Target.NextNavigationNodeKey = Source.NextNavigationNodeKey;
  Target.GuidanceDistanceCm = Source.GuidanceDistanceCm;
  Target.bBlocked = Source.bBlocked;
  Target.bUnreachable = Source.bUnreachable;
  Target.bRecoveredFromRasterMismatch = Source.bRecoveredFromRasterMismatch;
  Target.bSourceAttached = Source.bSourceAttached;
  return Target;
}

FCrowdTargetRegionTransportSettings
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(
  const FCrowdDemoTargetRegionTransportSettings& Source)
{
  FCrowdTargetRegionTransportSettings Target;
  Target.TargetLocation = Source.TargetLocation;
  Target.TargetVelocity = Source.TargetVelocity;
  Target.TargetPhysicalRadiusCm = Source.TargetPhysicalRadiusCm;
  Target.TargetHardSafetyGapCm = Source.TargetHardSafetyGapCm;
  Target.PhysicalRadiusCm = Source.PhysicalRadiusCm;
  Target.HardSafetyGapCm = Source.HardSafetyGapCm;
  Target.SoftMarginCm = Source.SoftMarginCm;
  Target.MinimumCenterDistanceCm = Source.MinimumCenterDistanceCm;
  Target.MaximumCenterDistanceCm = Source.MaximumCenterDistanceCm;
  Target.InfluenceBlendWidthCm = Source.InfluenceBlendWidthCm;
  Target.RadialBandWidthCm = Source.RadialBandWidthCm;
  Target.TransportSpeedCmps = Source.TransportSpeedCmps;
  Target.RadialGainPerSecond = Source.RadialGainPerSecond;
  Target.DemandRegionCount = Source.DemandRegionCount;
  Target.DemandRegionPhaseOffset = Source.DemandRegionPhaseOffset;
  Target.PlanLifetimeSteps = Source.PlanLifetimeSteps;
  Target.PositionQuantumCm = Source.PositionQuantumCm;
  Target.VelocityQuantumCmps = Source.VelocityQuantumCmps;
  Target.DistanceResponsePolicy = static_cast<ECrowdTargetDistanceResponsePolicy>(
    static_cast<uint8>(Source.DistanceResponsePolicy));
  Target.AcquireThenHoldReleaseHysteresisCm =
    Source.AcquireThenHoldReleaseHysteresisCm;
  return Target;
}

FCrowdTargetRegionTransportAgent
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionAgent(
  const FCrowdDemoTargetRegionTransportAgent& Source)
{
  FCrowdTargetRegionTransportAgent Target;
  Target.AgentId = Source.AgentId;
  Target.Location = Source.Location;
  Target.Velocity = Source.Velocity;
  Target.FarFlowPreferredVelocity = Source.FarFlowPreferredVelocity;
  Target.MaxSpeedCmps = Source.MaxSpeedCmps;
  Target.PhysicalRadiusCm = Source.PhysicalRadiusCm;
  Target.HardSafetyGapCm = Source.HardSafetyGapCm;
  Target.SoftMarginCm = Source.SoftMarginCm;
  Target.bEngagedHold = Source.bEngagedHold;
  return Target;
}

FCrowdTargetPolarTopology
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionTopology(
  const FCrowdDemoTargetPolarTopology& Source)
{
  FCrowdTargetPolarTopology Target;
  Target.BandCellOffsets = Source.BandCellOffsets;
  Target.BandSectorCounts = Source.BandSectorCounts;
  for (const auto& Value : Source.Cells)
  {
    auto& Out = Target.Cells.AddDefaulted_GetRef();
    Out.StableCellKey = Value.StableCellKey;
    Out.RadialBand = Value.RadialBand;
    Out.AngularSector = Value.AngularSector;
    Out.SectorCount = Value.SectorCount;
    Out.PrimaryDemandRegionKey = Value.PrimaryDemandRegionKey;
    Out.RelativeAnchorCm = Value.RelativeAnchorCm;
    Out.WorldAnchorCm = Value.WorldAnchorCm;
    Out.bFeasible = Value.bFeasible;
    Out.bTerminal = Value.bTerminal;
    Out.bBoundsBlocked = Value.bBoundsBlocked;
    Out.bObstacleBlocked = Value.bObstacleBlocked;
    Out.bTargetBlocked = Value.bTargetBlocked;
  }
  for (const auto& Value : Source.RegionLinks)
    Target.RegionLinks.Add({Value.CellKey, Value.RegionKey,
      Value.AngularOverlapQ15, Value.bTerminal});
  for (const auto& Value : Source.Edges)
    Target.Edges.Add({Value.FromCellKey, Value.ToCellKey,
      Value.GeometryCostCm, Value.SoftClearancePenaltyCm,
      Value.RadialDeviationPenaltyCm, Value.bCrossBand});
  Target.FeasibleGraphHash = Source.FeasibleGraphHash;
  Target.EnvironmentHash = Source.EnvironmentHash;
  Target.TopologyHash = Source.TopologyHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdTargetRegionDemandResult
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionDemand(
  const FCrowdDemoTargetRegionDemandResult& Source)
{
  FCrowdTargetRegionDemandResult Target;
  for (const auto& Value : Source.Regions)
    Target.Regions.Add({Value.StableRegionKey, Value.AvailableCapacity,
      Value.CurrentPopulation, Value.DesiredPopulation, Value.Deficit,
      Value.Surplus, Value.bFeasible});
  for (const auto& Value : Source.AgentStates)
    Target.AgentStates.Add({Value.AgentId, Value.CurrentCellKey,
      Value.CurrentRegionKey, Value.bTerminal, Value.bTerminalStay,
      Value.bSupply, Value.bSourceAttached, Value.bEngagedHold});
  Target.ExternalPopulationByCell = Source.ExternalPopulationByCell;
  Target.ExternalCongestionCostByCellCm = Source.ExternalCongestionCostByCellCm;
  Target.FeasibleRegionCount = Source.FeasibleRegionCount;
  Target.DesiredPopulationTotal = Source.DesiredPopulationTotal;
  Target.CurrentTerminalPopulation = Source.CurrentTerminalPopulation;
  Target.TotalDeficit = Source.TotalDeficit;
  Target.TotalSurplus = Source.TotalSurplus;
  Target.SupplyAgentCount = Source.SupplyAgentCount;
  Target.SourceAttachmentFailureCount = Source.SourceAttachmentFailureCount;
  Target.ExternalPopulationAgentCount = Source.ExternalPopulationAgentCount;
  Target.ExternalOccupiedCellCount = Source.ExternalOccupiedCellCount;
  Target.ExternalPopulationHash = Source.ExternalPopulationHash;
  Target.MembershipHash = Source.MembershipHash;
  Target.DemandHash = Source.DemandHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdTargetRegionFlowPlan
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionPlan(
  const FCrowdDemoTargetRegionFlowPlan& Source)
{
  FCrowdTargetRegionFlowPlan Target;
  Target.PlanEpoch = Source.PlanEpoch;
  Target.BuildFixedStepIndex = Source.BuildFixedStepIndex;
  Target.TargetRevision = Source.TargetRevision;
  Target.FeasibleGraphHash = Source.FeasibleGraphHash;
  Target.EnvironmentHash = Source.EnvironmentHash;
  Target.MembershipHash = Source.MembershipHash;
  Target.ExternalPopulationHash = Source.ExternalPopulationHash;
  for (const auto& Value : Source.EdgeFlows)
    Target.EdgeFlows.Add({Value.FromCellKey, Value.ToCellKey,
      Value.AgentQuota, Value.ReusedQuota});
  Target.RoutedAgentCount = Source.RoutedAgentCount;
  Target.UnroutedAgentCount = Source.UnroutedAgentCount;
  Target.TotalPhysicalCost = Source.TotalPhysicalCost;
  Target.ChangedQuotaUnitCount = Source.ChangedQuotaUnitCount;
  Target.TransportHash = Source.TransportHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdTargetRegionQuotaExecutionState
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionExecution(
  const FCrowdDemoTargetRegionQuotaExecutionState& Source)
{
  FCrowdTargetRegionQuotaExecutionState Target;
  Target.PlanEpoch = Source.PlanEpoch;
  Target.PlanTransportHash = Source.PlanTransportHash;
  for (const auto& Value : Source.Edges)
    Target.Edges.Add({Value.FromCellKey, Value.ToCellKey,
      Value.InitialQuota, Value.ConsumedQuota});
  for (const auto& Value : Source.ActiveClaims)
    Target.ActiveClaims.Add({Value.AgentId, Value.FromCellKey, Value.ToCellKey});
  Target.CompletedTransitionCount = Source.CompletedTransitionCount;
  Target.ExecutionHash = Source.ExecutionHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdDemoTargetPolarTopology
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionTopology(
  const FCrowdTargetPolarTopology& Source)
{
  FCrowdDemoTargetPolarTopology Target;
  Target.BandCellOffsets = Source.BandCellOffsets;
  Target.BandSectorCounts = Source.BandSectorCounts;
  for (const auto& Value : Source.Cells)
  {
    auto& Out = Target.Cells.AddDefaulted_GetRef();
    Out.StableCellKey = Value.StableCellKey;
    Out.RadialBand = Value.RadialBand;
    Out.AngularSector = Value.AngularSector;
    Out.SectorCount = Value.SectorCount;
    Out.PrimaryDemandRegionKey = Value.PrimaryDemandRegionKey;
    Out.RelativeAnchorCm = Value.RelativeAnchorCm;
    Out.WorldAnchorCm = Value.WorldAnchorCm;
    Out.bFeasible = Value.bFeasible;
    Out.bTerminal = Value.bTerminal;
    Out.bBoundsBlocked = Value.bBoundsBlocked;
    Out.bObstacleBlocked = Value.bObstacleBlocked;
    Out.bTargetBlocked = Value.bTargetBlocked;
  }
  for (const auto& Value : Source.RegionLinks)
    Target.RegionLinks.Add({Value.CellKey, Value.RegionKey,
      Value.AngularOverlapQ15, Value.bTerminal});
  for (const auto& Value : Source.Edges)
    Target.Edges.Add({Value.FromCellKey, Value.ToCellKey,
      Value.GeometryCostCm, Value.SoftClearancePenaltyCm,
      Value.RadialDeviationPenaltyCm, Value.bCrossBand});
  Target.FeasibleGraphHash = Source.FeasibleGraphHash;
  Target.EnvironmentHash = Source.EnvironmentHash;
  Target.TopologyHash = Source.TopologyHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdDemoTargetPolarTopologySummary
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionTopologySummary(
  const FCrowdTargetPolarTopologySummary& Source)
{
  return {Source.CellCount, Source.FeasibleCellCount, Source.EdgeCount,
    Source.CrossBandEdgeCount, Source.BoundsBlockedCellCount,
    Source.ObstacleBlockedCellCount, Source.TargetBlockedCellCount,
    Source.FeasibleGraphHash, Source.EnvironmentHash, Source.TopologyHash,
    Source.bValid};
}

FCrowdDemoTargetRegionDemandResult
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionDemand(
  const FCrowdTargetRegionDemandResult& Source)
{
  FCrowdDemoTargetRegionDemandResult Target;
  for (const auto& Value : Source.Regions)
    Target.Regions.Add({Value.StableRegionKey, Value.AvailableCapacity,
      Value.CurrentPopulation, Value.DesiredPopulation, Value.Deficit,
      Value.Surplus, Value.bFeasible});
  for (const auto& Value : Source.AgentStates)
    Target.AgentStates.Add({Value.AgentId, Value.CurrentCellKey,
      Value.CurrentRegionKey, Value.bTerminal, Value.bTerminalStay,
      Value.bSupply, Value.bSourceAttached, Value.bEngagedHold});
  Target.ExternalPopulationByCell = Source.ExternalPopulationByCell;
  Target.ExternalCongestionCostByCellCm = Source.ExternalCongestionCostByCellCm;
  Target.FeasibleRegionCount = Source.FeasibleRegionCount;
  Target.DesiredPopulationTotal = Source.DesiredPopulationTotal;
  Target.CurrentTerminalPopulation = Source.CurrentTerminalPopulation;
  Target.TotalDeficit = Source.TotalDeficit;
  Target.TotalSurplus = Source.TotalSurplus;
  Target.SupplyAgentCount = Source.SupplyAgentCount;
  Target.SourceAttachmentFailureCount = Source.SourceAttachmentFailureCount;
  Target.ExternalPopulationAgentCount = Source.ExternalPopulationAgentCount;
  Target.ExternalOccupiedCellCount = Source.ExternalOccupiedCellCount;
  Target.ExternalPopulationHash = Source.ExternalPopulationHash;
  Target.MembershipHash = Source.MembershipHash;
  Target.DemandHash = Source.DemandHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdDemoTargetRegionFlowPlan
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionPlan(
  const FCrowdTargetRegionFlowPlan& Source)
{
  FCrowdDemoTargetRegionFlowPlan Target;
  Target.PlanEpoch = Source.PlanEpoch;
  Target.BuildFixedStepIndex = Source.BuildFixedStepIndex;
  Target.TargetRevision = Source.TargetRevision;
  Target.FeasibleGraphHash = Source.FeasibleGraphHash;
  Target.EnvironmentHash = Source.EnvironmentHash;
  Target.MembershipHash = Source.MembershipHash;
  Target.ExternalPopulationHash = Source.ExternalPopulationHash;
  for (const auto& Value : Source.EdgeFlows)
    Target.EdgeFlows.Add({Value.FromCellKey, Value.ToCellKey,
      Value.AgentQuota, Value.ReusedQuota});
  Target.RoutedAgentCount = Source.RoutedAgentCount;
  Target.UnroutedAgentCount = Source.UnroutedAgentCount;
  Target.TotalPhysicalCost = Source.TotalPhysicalCost;
  Target.ChangedQuotaUnitCount = Source.ChangedQuotaUnitCount;
  Target.TransportHash = Source.TransportHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdDemoTargetRegionQuotaExecutionState
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionExecution(
  const FCrowdTargetRegionQuotaExecutionState& Source)
{
  FCrowdDemoTargetRegionQuotaExecutionState Target;
  Target.PlanEpoch = Source.PlanEpoch;
  Target.PlanTransportHash = Source.PlanTransportHash;
  for (const auto& Value : Source.Edges)
    Target.Edges.Add({Value.FromCellKey, Value.ToCellKey,
      Value.InitialQuota, Value.ConsumedQuota});
  for (const auto& Value : Source.ActiveClaims)
    Target.ActiveClaims.Add({Value.AgentId, Value.FromCellKey, Value.ToCellKey});
  Target.CompletedTransitionCount = Source.CompletedTransitionCount;
  Target.ExecutionHash = Source.ExecutionHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdDemoTargetRegionPlanReplacementSummary
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionReplacement(
  const FCrowdTargetRegionPlanReplacementSummary& Source)
{
  return {Source.PreviousClaimCount, Source.GeometryEligibleClaimCount,
    Source.MigratedClaimCount, Source.ReleasedClaimCount,
    Source.CompletedAtReplacementCount, Source.ReplacementHash, Source.bValid};
}

FCrowdDemoTargetRegionPlanValidationResult
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionValidation(
  const FCrowdTargetRegionPlanValidationResult& Source)
{
  return {Source.bValid, Source.MissingEdgeCount, Source.InfeasibleEdgeCount,
    Source.InvalidCellCount, Source.InsufficientOutgoingQuotaCellCount,
    Source.FlowConservationFailureCount, Source.UnreachableDeficitCount,
    Source.FirstFailureCellKey, Source.FirstFailureAgentId,
    Source.ValidationHash};
}

FCrowdDemoTargetRegionGuidanceResult
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionGuidance(
  const FCrowdTargetRegionGuidanceResult& Source)
{
  FCrowdDemoTargetRegionGuidanceResult Target;
  Target.AgentId = Source.AgentId;
  Target.CurrentCellKey = Source.CurrentCellKey;
  Target.NextCellKey = Source.NextCellKey;
  Target.DemandRegionKey = Source.DemandRegionKey;
  Target.Mode = static_cast<ECrowdDemoTargetRegionGuidanceMode>(
    static_cast<uint8>(Source.Mode));
  Target.DesiredVelocity = Source.DesiredVelocity;
  return Target;
}

FCrowdDemoTargetRegionGuidanceSummary
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionGuidanceSummary(
  const FCrowdTargetRegionGuidanceSummary& Source)
{
  FCrowdDemoTargetRegionGuidanceSummary Target;
  Target.FarFlowAgentCount = Source.FarFlowAgentCount;
  Target.TransportAgentCount = Source.TransportAgentCount;
  Target.TerminalSettleAgentCount = Source.TerminalSettleAgentCount;
  Target.EngagedHoldAgentCount = Source.EngagedHoldAgentCount;
  Target.UnroutedAgentCount = Source.UnroutedAgentCount;
  Target.FirstUnroutedAgentId = Source.FirstUnroutedAgentId;
  Target.FirstUnroutedCellKey = Source.FirstUnroutedCellKey;
  for (const auto& Value : Source.Consumption)
    Target.Consumption.Add({Value.FromCellKey, Value.ToCellKey,
      Value.AgentQuota, Value.ConsumedQuota});
  Target.ExecutionHash = Source.ExecutionHash;
  Target.GuidanceHash = Source.GuidanceHash;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdLocalPredictiveSettings
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreLocalPredictiveSettings(
  const FCrowdDemoLocalPredictiveSettings& Source)
{
  FCrowdLocalPredictiveSettings Target;
  Target.FixedStepSeconds = Source.FixedStepSeconds;
  Target.TimeHorizonSeconds = Source.TimeHorizonSeconds;
  Target.SpatialCellSizeCm = Source.SpatialCellSizeCm;
  Target.VelocityQuantumCmps = Source.VelocityQuantumCmps;
  Target.ConstraintEpsilonCmps = Source.ConstraintEpsilonCmps;
  Target.RequestedProgressThresholdCmps =
    Source.RequestedProgressThresholdCmps;
  Target.BlockedProgressThresholdCmps = Source.BlockedProgressThresholdCmps;
  Target.GrantedResponsibility = Source.GrantedResponsibility;
  Target.GrantDurationSteps = Source.GrantDurationSteps;
  Target.JointIterationCount = Source.JointIterationCount;
  return Target;
}

bool FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreLocalPredictiveAgent(
  const FCrowdMassAgentFragment& Identity,
  const FCrowdMassSimulationStateFragment& State,
  const FCrowdMassPropertiesFragment& Properties,
  const FCrowdMassComposedGuidanceFragment& Composed,
  const float MaximumSpeedCmps,
  const int32 BlockedAgeSteps,
  FCrowdLocalPredictiveAgent& OutAgent)
{
  OutAgent = {};
  if (Identity.AgentId == INDEX_NONE || Identity.LifecycleSerial <= 0
    || !State.bInitialized || !Composed.Value.bValid
    || Composed.Value.AgentId != Identity.AgentId
    || Composed.Value.PlanRevision != State.PlanRevision
    || !FMath::IsFinite(State.Position.X)
    || !FMath::IsFinite(State.Position.Y)
    || !FMath::IsFinite(State.Velocity.X)
    || !FMath::IsFinite(State.Velocity.Y)
    || !FMath::IsFinite(Composed.Value.AutonomousPreferredVelocity.X)
    || !FMath::IsFinite(Composed.Value.AutonomousPreferredVelocity.Y)
    || !FMath::IsFinite(Properties.PhysicalRadiusCm)
    || !FMath::IsFinite(Properties.HardSafetyGapCm)
    || !FMath::IsFinite(MaximumSpeedCmps)
    || Properties.PhysicalRadiusCm <= 0.0f
    || Properties.HardSafetyGapCm < 0.0f || MaximumSpeedCmps < 0.0f)
    return false;
  OutAgent.AgentId = Identity.AgentId;
  OutAgent.Position = FVector2f(State.Position.X, State.Position.Y);
  OutAgent.Velocity = FVector2f(State.Velocity.X, State.Velocity.Y);
  OutAgent.PreferredVelocity = FVector2f(
    Composed.Value.AutonomousPreferredVelocity.X,
    Composed.Value.AutonomousPreferredVelocity.Y);
  OutAgent.PhysicalRadiusCm = Properties.PhysicalRadiusCm;
  OutAgent.HardSafetyGapCm = Properties.HardSafetyGapCm;
  OutAgent.MaxSpeedCmps = MaximumSpeedCmps;
  OutAgent.BlockedAgeSteps = FMath::Max(0, BlockedAgeSteps);
  return true;
}

FCrowdLocalPredictiveGrantState
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreLocalPredictiveGrant(
  const FCrowdDemoLocalPredictiveGrantState& Source)
{
  return {Source.ComponentKey, Source.GrantedAgentId,
    Source.GrantEpoch, Source.RemainingSteps};
}

FCrowdDemoLocalPredictiveAgent
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveAgent(
  const FCrowdLocalPredictiveAgent& Source)
{
  FCrowdDemoLocalPredictiveAgent Target;
  Target.AgentId = Source.AgentId;
  Target.Position = Source.Position;
  Target.Velocity = Source.Velocity;
  Target.PreferredVelocity = Source.PreferredVelocity;
  Target.PhysicalRadiusCm = Source.PhysicalRadiusCm;
  Target.HardSafetyGapCm = Source.HardSafetyGapCm;
  Target.MaxSpeedCmps = Source.MaxSpeedCmps;
  Target.BlockedAgeSteps = Source.BlockedAgeSteps;
  return Target;
}

FCrowdDemoLocalPredictivePair
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictivePair(
  const FCrowdLocalPredictivePair& Source)
{
  FCrowdDemoLocalPredictivePair Target;
  Target.MinAgentId = Source.MinAgentId;
  Target.MaxAgentId = Source.MaxAgentId;
  Target.MinAgentIndex = Source.MinAgentIndex;
  Target.MaxAgentIndex = Source.MaxAgentIndex;
  Target.DistanceBucket = Source.DistanceBucket;
  Target.ClosestTimeSeconds = Source.ClosestTimeSeconds;
  Target.PredictedSeparationCm = Source.PredictedSeparationCm;
  Target.RequiredSeparationCm = Source.RequiredSeparationCm;
  Target.MinAgentResponsibility = Source.MinAgentResponsibility;
  Target.MaxAgentResponsibility = Source.MaxAgentResponsibility;
  return Target;
}

FCrowdDemoLocalPredictiveGrantState
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveGrant(
  const FCrowdLocalPredictiveGrantState& Source)
{
  return {Source.ComponentKey, Source.GrantedAgentId,
    Source.GrantEpoch, Source.RemainingSteps};
}

FCrowdDemoLocalPredictiveResult
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveResult(
  const FCrowdLocalPredictiveResult& Source)
{
  FCrowdDemoLocalPredictiveResult Target;
  Target.AgentId = Source.AgentId;
  Target.Velocity = Source.Velocity;
  Target.NeighborCount = Source.NeighborCount;
  Target.ConstraintCount = Source.ConstraintCount;
  Target.NextBlockedAgeSteps = Source.NextBlockedAgeSteps;
  Target.ComponentKey = Source.ComponentKey;
  Target.GrantEpoch = Source.GrantEpoch;
  Target.bAdjusted = Source.bAdjusted;
  Target.bGranted = Source.bGranted;
  Target.bYielding = Source.bYielding;
  Target.bValid = Source.bValid;
  return Target;
}

FCrowdDemoLocalPredictiveSummary
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveSummary(
  const FCrowdLocalPredictiveSummary& Source)
{
  FCrowdDemoLocalPredictiveSummary Target;
  Target.bValid = Source.bValid;
  Target.CandidateHash = Source.CandidateHash;
  Target.ProcessedAgentCount = Source.ProcessedAgentCount;
  Target.CandidatePairCount = Source.CandidatePairCount;
  Target.ConflictPairCount = Source.ConflictPairCount;
  Target.ComponentCount = Source.ComponentCount;
  Target.MaxComponentSize = Source.MaxComponentSize;
  Target.AdjustedAgentCount = Source.AdjustedAgentCount;
  Target.GrantedAgentCount = Source.GrantedAgentCount;
  Target.YieldingAgentCount = Source.YieldingAgentCount;
  Target.InfeasibleAgentCount = Source.InfeasibleAgentCount;
  Target.QuantizationFailureCount = Source.QuantizationFailureCount;
  Target.JointValidationFailureCount = Source.JointValidationFailureCount;
  Target.JointComponentResolutionCount = Source.JointComponentResolutionCount;
  Target.CoherentTranslationComponentCount =
    Source.CoherentTranslationComponentCount;
  Target.CoherentTranslationAgentCount = Source.CoherentTranslationAgentCount;
  Target.CoherentTranslationMaxCmps = Source.CoherentTranslationMaxCmps;
  Target.JointPreferredRecoveryComponentCount =
    Source.JointPreferredRecoveryComponentCount;
  Target.JointPreferredRecoveryAgentCount =
    Source.JointPreferredRecoveryAgentCount;
  Target.JointPreferredRecoveryMaxGainCmps =
    Source.JointPreferredRecoveryMaxGainCmps;
  Target.EnvironmentConstraintCount = Source.EnvironmentConstraintCount;
  Target.GrantSwitchCount = Source.GrantSwitchCount;
  Target.BlockedAgeMax = Source.BlockedAgeMax;
  return Target;
}

FCrowdDemoLocalPredictiveDiagnosticTrace
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveTrace(
  const FCrowdLocalPredictiveDiagnosticTrace& Source)
{
  FCrowdDemoLocalPredictiveDiagnosticTrace Target;
  for (const auto& Value : Source.InitialIndependentResults)
    Target.InitialIndependentResults.Add(BuildDemoLocalPredictiveResult(Value));
  for (const auto& Value : Source.CompletedIndependentResults)
    Target.CompletedIndependentResults.Add(BuildDemoLocalPredictiveResult(Value));
  for (const auto& Value : Source.Components)
    Target.Components.Add(ToDemoComponentTrace(Value));
  return Target;
}

FCrowdParticleConstraintEnvironment
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleEnvironment(
  const FCrowdDemoParticleConstraintEnvironment& Source)
{
  FCrowdParticleConstraintEnvironment Target;
  Target.FlowConfig = BuildCoreFlowConfig(Source.FlowConfig);
  Target.bConstrainToFlowBounds = Source.bConstrainToFlowBounds;
  return Target;
}

FCrowdParticleConstraintSettings
FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleSettings(
  const FCrowdDemoParticleConstraintSettings& Source)
{
  FCrowdParticleConstraintSettings Target;
  Target.FixedStepSeconds = Source.FixedStepSeconds;
  Target.IterationCount = Source.IterationCount;
  Target.SafetyIterationCount = Source.SafetyIterationCount;
  Target.SoftResponsePerSecond = Source.SoftResponsePerSecond;
  Target.SoftMaxPairCorrectionPerIterationCm =
    Source.SoftMaxPairCorrectionPerIterationCm;
  Target.SoftMaxEnvironmentCorrectionPerIterationCm =
    Source.SoftMaxEnvironmentCorrectionPerIterationCm;
  Target.HardMaxPairCorrectionPerIterationCm =
    Source.HardMaxPairCorrectionPerIterationCm;
  Target.PositionQuantumCm = Source.PositionQuantumCm;
  Target.VelocityQuantumCmps = Source.VelocityQuantumCmps;
  Target.bCaptureSafetyStageTrace = Source.bCaptureSafetyStageTrace;
  Target.bCaptureRouteDiagnostic = Source.bCaptureRouteDiagnostic;
  return Target;
}

bool FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleAgent(
  const FCrowdMassAgentFragment& Identity,
  const FCrowdMassPropertiesFragment& Properties,
  const FVector& StartPosition,
  const FVector& PredictedPosition,
  const float EnvironmentHardClearanceCm,
  FCrowdParticleConstraintAgent& OutAgent)
{
  OutAgent = {};
  if (Identity.AgentId == INDEX_NONE || Identity.LifecycleSerial <= 0
    || !FMath::IsFinite(StartPosition.X)
    || !FMath::IsFinite(StartPosition.Y)
    || !FMath::IsFinite(StartPosition.Z)
    || !FMath::IsFinite(PredictedPosition.X)
    || !FMath::IsFinite(PredictedPosition.Y)
    || !FMath::IsFinite(PredictedPosition.Z)
    || !FMath::IsFinite(Properties.PhysicalRadiusCm)
    || !FMath::IsFinite(Properties.HardSafetyGapCm)
    || !FMath::IsFinite(Properties.SoftMarginCm)
    || !FMath::IsFinite(Properties.Mobility)
    || !FMath::IsFinite(EnvironmentHardClearanceCm)
    || Properties.PhysicalRadiusCm <= 0.0f
    || Properties.HardSafetyGapCm < 0.0f
    || Properties.SoftMarginCm < 0.0f
    || Properties.Mobility < 0.0f
    || EnvironmentHardClearanceCm < 0.0f)
    return false;
  OutAgent.AgentId = Identity.AgentId;
  OutAgent.StartPosition = StartPosition;
  OutAgent.PredictedPosition = PredictedPosition;
  OutAgent.PhysicalRadiusCm = Properties.PhysicalRadiusCm;
  OutAgent.HardSafetyGapCm = Properties.HardSafetyGapCm;
  OutAgent.EnvironmentHardClearanceCm = EnvironmentHardClearanceCm;
  OutAgent.SoftMarginCm = Properties.SoftMarginCm;
  OutAgent.Mobility = Properties.Mobility;
  return true;
}

FCrowdDemoParticleConstraintAgent
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleAgent(
  const FCrowdParticleConstraintAgent& Source)
{
  FCrowdDemoParticleConstraintAgent Target;
  Target.AgentId = Source.AgentId;
  Target.StartPosition = Source.StartPosition;
  Target.PredictedPosition = Source.PredictedPosition;
  Target.PhysicalRadiusCm = Source.PhysicalRadiusCm;
  Target.HardSafetyGapCm = Source.HardSafetyGapCm;
  Target.EnvironmentHardClearanceCm = Source.EnvironmentHardClearanceCm;
  Target.SoftMarginCm = Source.SoftMarginCm;
  Target.Mobility = Source.Mobility;
  return Target;
}

FCrowdDemoParticleConstraintPair
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticlePair(
  const FCrowdParticleConstraintPair& Source)
{
  FCrowdDemoParticleConstraintPair Target;
  Target.MinAgentId = Source.MinAgentId;
  Target.MaxAgentId = Source.MaxAgentId;
  Target.MinAgentIndex = Source.MinAgentIndex;
  Target.MaxAgentIndex = Source.MaxAgentIndex;
  return Target;
}

FCrowdDemoParticleConstraintResult
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleResult(
  const FCrowdParticleConstraintResult& Source)
{
  FCrowdDemoParticleConstraintResult Target;
  Target.AgentId = Source.AgentId;
  Target.CorrectedPosition = Source.CorrectedPosition;
  Target.CorrectedVelocity = Source.CorrectedVelocity;
  Target.RealizedCorrection = Source.RealizedCorrection;
  Target.FirstInfluencedIteration = Source.FirstInfluencedIteration;
  Target.CorrectedPairCount = Source.CorrectedPairCount;
  return Target;
}

FCrowdDemoParticleConstraintSummary
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleSummary(
  const FCrowdParticleConstraintSummary& Source)
{
  FCrowdDemoParticleConstraintSummary Target;
  Target.bValid = Source.bValid;
  Target.CandidatePairCount = Source.CandidatePairCount;
  Target.SoftPairCount = Source.SoftPairCount;
  Target.SoftViolatingPairCount = Source.SoftViolatingPairCount;
  Target.HardPairViolationCount = Source.HardPairViolationCount;
  Target.SweptPairViolationCount = Source.SweptPairViolationCount;
  Target.ObstaclePenetrationCount = Source.ObstaclePenetrationCount;
  Target.BoundsViolationCount = Source.BoundsViolationCount;
  Target.EnvironmentSoftContactCount = Source.EnvironmentSoftContactCount;
  Target.EnvironmentSoftAppliedAgentCount =
    Source.EnvironmentSoftAppliedAgentCount;
  Target.UnifiedHardConstraintCount = Source.UnifiedHardConstraintCount;
  Target.UnifiedHardInfeasibleCount = Source.UnifiedHardInfeasibleCount;
  Target.PressureInfluencedAgentCount = Source.PressureInfluencedAgentCount;
  Target.FirstInfluencedIterationMax = Source.FirstInfluencedIterationMax;
  Target.CorrectedAgentCount = Source.CorrectedAgentCount;
  Target.SoftErrorCmP50 = Source.SoftErrorCmP50;
  Target.SoftErrorCmP95 = Source.SoftErrorCmP95;
  Target.SoftErrorCmMax = Source.SoftErrorCmMax;
  Target.EnvironmentSoftErrorCmP50 = Source.EnvironmentSoftErrorCmP50;
  Target.EnvironmentSoftErrorCmP95 = Source.EnvironmentSoftErrorCmP95;
  Target.EnvironmentSoftErrorCmMax = Source.EnvironmentSoftErrorCmMax;
  Target.EnvironmentSoftRequestedCorrectionCmMax =
    Source.EnvironmentSoftRequestedCorrectionCmMax;
  Target.EnvironmentSoftRealizedCorrectionCmMax =
    Source.EnvironmentSoftRealizedCorrectionCmMax;
  Target.UnifiedHardResidualCmMax = Source.UnifiedHardResidualCmMax;
  Target.MaxAgentCorrectionCm = Source.MaxAgentCorrectionCm;
  Target.CandidateHash = Source.CandidateHash;
  return Target;
}

FCrowdDemoParticleConstraintTrace
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleTrace(
  const FCrowdParticleConstraintTrace& Source)
{
  FCrowdDemoParticleConstraintTrace Target;
  Target.AgentIds = Source.AgentIds;
  Target.StartPositions = Source.StartPositions;
  Target.PredictPositions = Source.PredictPositions;
  Target.SoftPositions = Source.SoftPositions;
  Target.EnvironmentSoftPositions = Source.EnvironmentSoftPositions;
  Target.UnifiedHardPositions = Source.UnifiedHardPositions;
  Target.HardPositions = Source.HardPositions;
  Target.SweptPositions = Source.SweptPositions;
  Target.ObstaclePositions = Source.ObstaclePositions;
  Target.QuantizedPositions = Source.QuantizedPositions;
  Target.FinalSafetyPositions = Source.FinalSafetyPositions;
  for (const auto& Value : Source.FinalEnvironmentContacts)
    Target.FinalEnvironmentContacts.Add(ToDemoParticleContact(Value));
  for (const auto& Value : Source.FinalHardConstraints)
    Target.FinalHardConstraints.Add(ToDemoParticleConstraint(Value));
  for (const auto& Value : Source.SafetyStages)
    Target.SafetyStages.Add(ToDemoParticleSafetyStage(Value));
  Target.PairSoftRequestedCorrections = Source.PairSoftRequestedCorrections;
  Target.PairSoftRealizedCorrections = Source.PairSoftRealizedCorrections;
  Target.EnvironmentSoftRequestedCorrections =
    Source.EnvironmentSoftRequestedCorrections;
  Target.EnvironmentSoftRealizedCorrections =
    Source.EnvironmentSoftRealizedCorrections;
  Target.UnifiedHardCorrections = Source.UnifiedHardCorrections;
  Target.ActiveNeighborAgentIds = Source.ActiveNeighborAgentIds;
  for (const auto& Value : Source.SoftPairInfluences)
    Target.SoftPairInfluences.Add(ToDemoParticleInfluence(Value));
  return Target;
}

FCrowdDemoComposedGuidance
FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoComposedGuidance(
  const FCrowdComposedGuidance& Source)
{
  FCrowdDemoComposedGuidance Target;
  Target.AgentId = Source.AgentId;
  Target.SelectedProvider = static_cast<ECrowdDemoGuidanceProvider>(
    static_cast<uint8>(Source.SelectedProvider));
  Target.PlanRevision = Source.PlanRevision;
  Target.AutonomousPreferredVelocity = Source.AutonomousPreferredVelocity;
  Target.DesiredLocation = Source.DesiredLocation;
  Target.DesiredYawDegrees = Source.DesiredYawDegrees;
  Target.CandidateSetHash = Source.CandidateSetHash;
  Target.StableHash = Source.StableHash;
  Target.bValid = Source.bValid;
  return Target;
}

bool FCrowdDemoMassCrowdRuntimeAdapter::ApplyCommitRecord(
  const FCrowdMassCommitRecord& Record,
  const FCrowdDemoMassIdentityFragment& Identity,
  FCrowdDemoRoundSimStateFragment& InOutState)
{
  const FCrowdMassCommitTarget Target = BuildCommitTarget(Identity);
  if (!Record.Movement.bValid
    || Record.Movement.AgentId != Target.AgentId
    || Record.Movement.LifecycleSerial != Target.LifecycleSerial)
    return false;
  InOutState.Location = Record.Movement.Position;
  InOutState.Velocity = Record.Movement.Velocity;
  InOutState.YawDegrees = Record.Movement.YawDegrees;
  InOutState.PlanRevision = Record.PlanRevision;
  InOutState.bInitialized = true;
  return true;
}
