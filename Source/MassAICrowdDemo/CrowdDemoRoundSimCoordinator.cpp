#include "CrowdDemoRoundSimCoordinator.h"

#include "CrowdDemoReplicator.h"
#include "CrowdDemoScenarioRegistry.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoRelevantSnapshotAdapter.h"
#include "Mass/CrowdDemoRoundCheckpointTransport.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "MassCrowdReplicationActor.h"
#include "MassCrowdReplicationChannel.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
  constexpr float CrowdDemoRoundDurationSeconds = 6.0f;
  constexpr float CrowdDemoRoundStartLeadSeconds = 0.0f;
  constexpr float CrowdDemoRoundRestartDelaySeconds = 0.0f;
  constexpr float CrowdDemoRoundInitialStartDelaySeconds = 9.0f;
  constexpr float CrowdDemoCorrectionFrameIntervalSeconds = 0.50f;
  constexpr float CrowdDemoCorrectionAssemblyTimeoutSeconds = 3.0f;
  constexpr float CrowdDemoRoundResultAssemblyTimeoutSeconds = 5.0f;
  constexpr float CrowdDemoMaxCorrectionFrameAgeMs = 1000.0f;
  constexpr int32 CrowdDemoCorrectionFrameChunkSize = 100;
  constexpr int32 CrowdDemoCorrectionFrameHistoryRevisions = 4;
  constexpr uint32 CrowdDemoProductPayloadVersion = 1;
  constexpr uint32 CrowdDemoCorrectionHeaderMagic = 0x48435231u;
  constexpr uint32 CrowdDemoCorrectionAgentMagic = 0x41435231u;
  constexpr uint32 CrowdDemoProjectileEventMagic = 0x45565031u;
  constexpr uint32 CrowdDemoRoundResultHeaderMagic = 0x48525231u;

  template <typename T>
  bool EncodeProductPayload(
    const uint32 Magic,
    const T& Value,
    TArray<uint8>& OutBytes)
  {
    OutBytes.Reset();
    FMemoryWriter Writer(OutBytes, true);
    uint32 MutableMagic = Magic;
    uint32 Version = CrowdDemoProductPayloadVersion;
    Writer << MutableMagic;
    Writer << Version;
    T Copy = Value;
    T::StaticStruct()->SerializeItem(Writer, &Copy, nullptr);
    return !Writer.IsError() && OutBytes.Num() <= 4096;
  }

  template <typename T>
  bool DecodeProductPayload(
    const TConstArrayView<uint8> Bytes,
    const uint32 ExpectedMagic,
    T& OutValue)
  {
    TArray<uint8> Copy;
    Copy.Append(Bytes.GetData(), Bytes.Num());
    FMemoryReader Reader(Copy, true);
    uint32 Magic = 0;
    uint32 Version = 0;
    Reader << Magic;
    Reader << Version;
    if (Reader.IsError() || Magic != ExpectedMagic
      || Version != CrowdDemoProductPayloadVersion)
    {
      return false;
    }
    T Value;
    T::StaticStruct()->SerializeItem(Reader, &Value, nullptr);
    if (Reader.IsError() || Reader.Tell() != Reader.TotalSize())
    {
      return false;
    }
    OutValue = MoveTemp(Value);
    return true;
  }

  bool EncodeProductRoundResultHeader(
    const FCrowdDemoRoundResultHeader& Header,
    TArray<uint8>& OutBytes)
  {
    OutBytes.Reset();
    FMemoryWriter Writer(OutBytes, true);
    uint32 Magic = CrowdDemoRoundResultHeaderMagic;
    uint32 Version = CrowdDemoProductPayloadVersion;
    Writer << Magic;
    Writer << Version;
    FCrowdDemoRoundResultHeader Copy = Header;
    bool bSuccess = false;
    Copy.NetSerialize(Writer, nullptr, bSuccess);
    return bSuccess && !Writer.IsError()
      && OutBytes.Num() <= 4096;
  }

  bool DecodeProductRoundResultHeader(
    const TConstArrayView<uint8> Bytes,
    FCrowdDemoRoundResultHeader& OutHeader)
  {
    TArray<uint8> Copy;
    Copy.Append(Bytes.GetData(), Bytes.Num());
    FMemoryReader Reader(Copy, true);
    uint32 Magic = 0;
    uint32 Version = 0;
    Reader << Magic;
    Reader << Version;
    if (Reader.IsError() || Magic != CrowdDemoRoundResultHeaderMagic
      || Version != CrowdDemoProductPayloadVersion)
      return false;
    FCrowdDemoRoundResultHeader Header;
    bool bSuccess = false;
    Header.NetSerialize(Reader, nullptr, bSuccess);
    if (!bSuccess || Reader.IsError()
      || Reader.Tell() != Reader.TotalSize())
      return false;
    OutHeader = MoveTemp(Header);
    return true;
  }

  FString SerializeParticleFailureFixture(const FCrowdDemoParticleFailureFixture& Fixture)
  {
    FString Json = FString::Printf(
      TEXT("{\n  \"contract_version\":2,\n  \"fixture_hash\":%u,\n  \"valid\":%s,\n  \"fixed_step\":%d,\n  \"pair\":[%d,%d],\n  \"hard_violation\":%s,\n  \"swept_violation\":%s,\n  \"required_hard_distance_cm\":%.3f,\n  \"final_endpoint_distance_cm\":%.3f,\n  \"final_swept_distance_cm\":%.3f,\n  \"candidate_hash\":%u,\n  \"applied_state_hash\":%u,\n  \"first_failure_environment_id\":%d,\n  \"first_failure_constraint_kind\":%d,\n"),
      Fixture.FixtureHash, Fixture.bValid ? TEXT("true") : TEXT("false"),
      Fixture.FixedStepIndex, Fixture.MinAgentId, Fixture.MaxAgentId,
      Fixture.bHardViolation ? TEXT("true") : TEXT("false"),
      Fixture.bSweptViolation ? TEXT("true") : TEXT("false"),
      Fixture.RequiredHardDistanceCm, Fixture.FinalEndpointDistanceCm,
      Fixture.FinalSweptDistanceCm, Fixture.CandidateHash, Fixture.AppliedStateHash,
      Fixture.FirstFailureEnvironmentId, Fixture.FirstFailureConstraintKind);
    if (Fixture.bHasFirstFailureContact)
    {
      const auto& C = Fixture.FirstFailureContact;
      Json += FString::Printf(
        TEXT("  \"first_failure_contact\":{\"agent_id\":%d,\"environment_id\":%d,\"kind\":%d,\"face\":%d,\"closest_point\":[%.3f,%.3f],\"normal\":[%.6f,%.6f],\"hard_distance_cm\":%.3f,\"soft_distance_cm\":%.3f,\"soft_error_cm\":%.3f,\"hard_deficit_cm\":%.3f,\"swept_time\":%.6f},\n"),
        C.AgentId, C.EnvironmentId, static_cast<int32>(C.ContactKind),
        static_cast<int32>(C.Face), C.ClosestPoint.X, C.ClosestPoint.Y,
        C.CorrectionNormal.X, C.CorrectionNormal.Y, C.HardDistanceCm,
        C.SoftDistanceCm, C.SoftErrorCm, C.HardDeficitCm, C.SweptTime);
    }
    else
    {
      Json += TEXT("  \"first_failure_contact\":null,\n");
    }
    if (Fixture.bHasFirstFailureConstraint)
    {
      const auto& C = Fixture.FirstFailureConstraint;
      Json += FString::Printf(
        TEXT("  \"first_failure_constraint\":{\"kind\":%d,\"agents\":[%d,%d],\"environment_id\":%d,\"face\":%d,\"normal\":[%.6f,%.6f],\"coefficient_scale\":%.6f,\"threshold\":%.3f,\"initial_deficit_cm\":%.3f},\n"),
        static_cast<int32>(C.Kind), C.MinAgentId, C.MaxAgentId,
        C.EnvironmentId, static_cast<int32>(C.Face), C.Normal.X, C.Normal.Y,
        C.CoefficientScale, C.Threshold, C.InitialDeficitCm);
    }
    else
    {
      Json += TEXT("  \"first_failure_constraint\":null,\n");
    }
    Json += TEXT("  \"solve_agents\":[\n");
    for (int32 Index = 0; Index < Fixture.SolveAgents.Num(); ++Index)
    {
      const auto& A = Fixture.SolveAgents[Index];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"start\":[%.3f,%.3f,%.3f],\"predict\":[%.3f,%.3f,%.3f],\"physical_radius_cm\":%.3f,\"hard_gap_cm\":%.3f,\"soft_margin_cm\":%.3f,\"mobility\":%.6f}%s\n"),
        A.AgentId, A.StartPosition.X, A.StartPosition.Y, A.StartPosition.Z,
        A.PredictedPosition.X, A.PredictedPosition.Y, A.PredictedPosition.Z,
        A.PhysicalRadiusCm, A.HardSafetyGapCm, A.SoftMarginCm, A.Mobility,
        Index + 1 < Fixture.SolveAgents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"agents\":[\n");
    for (int32 Index = 0; Index < Fixture.Agents.Num(); ++Index)
    {
      const auto& A = Fixture.Agents[Index];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"physical_radius_cm\":%.3f,\"hard_gap_cm\":%.3f,\"soft_margin_cm\":%.3f,\"mobility\":%.3f,\"start\":[%d,%d],\"predict\":[%d,%d],\"pair_soft\":[%d,%d],\"environment_soft\":[%d,%d],\"unified_hard\":[%d,%d],\"hard\":[%d,%d],\"swept\":[%d,%d],\"obstacle\":[%d,%d],\"quantized\":[%d,%d],\"final_safety\":[%d,%d],\"applied\":[%d,%d]}%s\n"),
        A.AgentId, A.PhysicalRadiusCm, A.HardSafetyGapCm, A.SoftMarginCm, A.Mobility,
        FMath::RoundToInt(A.Start.X), FMath::RoundToInt(A.Start.Y),
        FMath::RoundToInt(A.Predict.X), FMath::RoundToInt(A.Predict.Y),
        FMath::RoundToInt(A.Soft.X), FMath::RoundToInt(A.Soft.Y),
        FMath::RoundToInt(A.EnvironmentSoft.X), FMath::RoundToInt(A.EnvironmentSoft.Y),
        FMath::RoundToInt(A.UnifiedHard.X), FMath::RoundToInt(A.UnifiedHard.Y),
        FMath::RoundToInt(A.Hard.X), FMath::RoundToInt(A.Hard.Y),
        FMath::RoundToInt(A.Swept.X), FMath::RoundToInt(A.Swept.Y),
        FMath::RoundToInt(A.Obstacle.X), FMath::RoundToInt(A.Obstacle.Y),
        FMath::RoundToInt(A.Quantized.X), FMath::RoundToInt(A.Quantized.Y),
        FMath::RoundToInt(A.FinalSafety.X), FMath::RoundToInt(A.FinalSafety.Y),
        FMath::RoundToInt(A.Applied.X), FMath::RoundToInt(A.Applied.Y),
        Index + 1 < Fixture.Agents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ]\n}\n");
    return Json;
  }


  FVector ComputeRoundCohortCenter(TConstArrayView<FCrowdDemoRoundAgentState> Agents)
  {
    if (Agents.IsEmpty())
      return FVector(0.0f, -2500.0f, 60.0f);
    FVector Center = FVector::ZeroVector;
    for (const FCrowdDemoRoundAgentState& Agent : Agents)
      Center += FVector(Agent.Location);
    Center /= static_cast<float>(Agents.Num());
    Center.Z = 60.0f;
    return Center;
  }

  float ComputeCoordinatorP95(TArray<float> Samples)
  {
    if (Samples.IsEmpty())
      return -1.0f;
    Samples.Sort();
    const int32 Index = FMath::Clamp(
      FMath::CeilToInt(static_cast<float>(Samples.Num()) * 0.95f) - 1,
      0, Samples.Num() - 1);
    return Samples[Index];
  }

  FCrowdDemoRoundResultHeader MakeRoundResultHeader(
    const FCrowdDemoRoundResultPacket& Packet,
    const ECrowdDemoSoftPressureTestCase TestCase)
  {
    FCrowdDemoRoundResultHeader Header;
    Header.ContractVersion = FCrowdDemoRoundResultHeader::CurrentContractVersion;
    Header.PayloadKind = TestCase == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
      ? 2 : 1;
    Header.bValid = Packet.bValid;
    Header.RoundId = Packet.RoundId;
    Header.Revision = Packet.Revision;
    Header.CheckpointRevision = Packet.CheckpointRevision;
    Header.StateFrameRevision = Packet.StateFrameRevision;
    Header.EndServerTimeSeconds = Packet.EndServerTimeSeconds;
    Header.AgentCount = Packet.Agents.Num();
    Header.OverlapPairCount = Packet.OverlapPairCount;
    Header.InitialOverlapPairCount = Packet.InitialOverlapPairCount;
    Header.SevereOverlapPairCount = Packet.SevereOverlapPairCount;
    Header.InitialSevereOverlapPairCount = Packet.InitialSevereOverlapPairCount;
    Header.ObstaclePenetrationCount = Packet.ObstaclePenetrationCount;
    Header.ArrivalCount = Packet.ArrivalCount;
    Header.SharedFlowMetrics = Packet.SharedFlowMetrics;
    Header.ParticleMetrics = Packet.ParticleMetrics;
    Header.ProjectileMetrics = Packet.ProjectileMetrics;
    return Header;
  }

  ACrowdDemoReplicator* FindProjectileVisualHost(UWorld& World)
  {
    ACrowdDemoReplicator* LocalFallback = nullptr;
    for (TActorIterator<ACrowdDemoReplicator> It(&World); It; ++It)
    {
      ACrowdDemoReplicator* Candidate = *It;
      if (!Candidate)
        continue;
      if (!Candidate->IsLocalVisualHostOnly())
        return Candidate;
      LocalFallback = LocalFallback ? LocalFallback : Candidate;
    }
    return LocalFallback;
  }

  FString SerializeLocalPredictiveComponentFixture(
    const FCrowdDemoLocalPredictiveComponentFixture& Fixture)
  {
    FString Json = FString::Printf(
      TEXT("{\n  \"contract_version\":1,\n  \"valid\":%s,\n  \"fixed_step\":%d,\n  \"stable_hash\":%u,\n  \"settings\":{\"fixed_step\":%.6f,\"time_horizon\":%.3f,\"cell_size\":%.1f,\"velocity_quantum\":%.3f,\"epsilon\":%.3f,\"requested_progress\":%.1f,\"blocked_progress\":%.1f,\"granted_responsibility\":%.3f,\"grant_steps\":%d,\"joint_iterations\":%d},\n  \"witness_agent_ids\":["),
      Fixture.bValid ? TEXT("true") : TEXT("false"), Fixture.FixedStepIndex,
      Fixture.StableHash, Fixture.Settings.FixedStepSeconds,
      Fixture.Settings.TimeHorizonSeconds, Fixture.Settings.SpatialCellSizeCm,
      Fixture.Settings.VelocityQuantumCmps,
      Fixture.Settings.ConstraintEpsilonCmps,
      Fixture.Settings.RequestedProgressThresholdCmps,
      Fixture.Settings.BlockedProgressThresholdCmps,
      Fixture.Settings.GrantedResponsibility, Fixture.Settings.GrantDurationSteps,
      Fixture.Settings.JointIterationCount);
    for (int32 Index = 0; Index < Fixture.WitnessAgentIds.Num(); ++Index)
      Json += FString::Printf(TEXT("%s%d"), Index ? TEXT(",") : TEXT(""),
        Fixture.WitnessAgentIds[Index]);
    Json += TEXT("],\n  \"agents\":[\n");
    for (int32 Index = 0; Index < Fixture.Agents.Num(); ++Index)
    {
      const auto& Agent = Fixture.Agents[Index];
      Json += FString::Printf(
        TEXT("    {\"id\":%d,\"position\":[%.1f,%.1f],\"velocity\":[%.1f,%.1f],\"preferred\":[%.1f,%.1f],\"radius\":%.1f,\"hard_gap\":%.1f,\"max_speed\":%.1f,\"blocked_age\":%d}%s\n"),
        Agent.AgentId, Agent.Position.X, Agent.Position.Y,
        Agent.Velocity.X, Agent.Velocity.Y, Agent.PreferredVelocity.X,
        Agent.PreferredVelocity.Y, Agent.PhysicalRadiusCm,
        Agent.HardSafetyGapCm, Agent.MaxSpeedCmps, Agent.BlockedAgeSteps,
        Index + 1 < Fixture.Agents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"pairs\":[\n");
    for (int32 Index = 0; Index < Fixture.ConflictPairs.Num(); ++Index)
    {
      const auto& Pair = Fixture.ConflictPairs[Index];
      Json += FString::Printf(
        TEXT("    {\"agents\":[%d,%d],\"closest_time\":%.4f,\"predicted_separation\":%.3f,\"required_separation\":%.3f,\"responsibility\":[%.3f,%.3f]}%s\n"),
        Pair.MinAgentId, Pair.MaxAgentId, Pair.ClosestTimeSeconds,
        Pair.PredictedSeparationCm, Pair.RequiredSeparationCm,
        Pair.MinAgentResponsibility, Pair.MaxAgentResponsibility,
        Index + 1 < Fixture.ConflictPairs.Num() ? TEXT(",") : TEXT(""));
    }
    const auto AppendResults = [&](const TCHAR* Name,
      const TArray<FCrowdDemoLocalPredictiveResult>& Results)
    {
      Json += FString::Printf(TEXT("  ],\n  \"%s\":[\n"), Name);
      for (int32 Index = 0; Index < Results.Num(); ++Index)
      {
        const auto& Result = Results[Index];
        Json += FString::Printf(
          TEXT("    {\"id\":%d,\"velocity\":[%.1f,%.1f],\"neighbors\":%d,\"constraints\":%d,\"blocked_age\":%d,\"component\":%u,\"granted\":%s,\"yielding\":%s,\"valid\":%s}%s\n"),
          Result.AgentId, Result.Velocity.X, Result.Velocity.Y,
          Result.NeighborCount, Result.ConstraintCount,
          Result.NextBlockedAgeSteps, Result.ComponentKey,
          Result.bGranted ? TEXT("true") : TEXT("false"),
          Result.bYielding ? TEXT("true") : TEXT("false"),
          Result.bValid ? TEXT("true") : TEXT("false"),
          Index + 1 < Results.Num() ? TEXT(",") : TEXT(""));
      }
    };
    AppendResults(TEXT("initial_independent"),
      Fixture.Trace.InitialIndependentResults);
    AppendResults(TEXT("completed_independent"),
      Fixture.Trace.CompletedIndependentResults);
    AppendResults(TEXT("final_results"), Fixture.Results);
    Json += TEXT("  ],\n  \"components\":[\n");
    for (int32 ComponentIndex = 0;
      ComponentIndex < Fixture.Trace.Components.Num(); ++ComponentIndex)
    {
      const auto& Component = Fixture.Trace.Components[ComponentIndex];
      Json += FString::Printf(
        TEXT("    {\"key\":%u,\"granted\":%d,\"common_velocity\":[%.1f,%.1f],\"common_valid\":%s,\"full_joint_safe\":%s,\"coherent_translation_applied\":%s,\"coherent_translation\":[%.1f,%.1f],\"joint_preferred_recovery_applied\":%s,\"safe_alpha_q15\":%d,\"pre_translation\":["),
        Component.ComponentKey, Component.GrantedAgentId,
        Component.CommonVelocity.X, Component.CommonVelocity.Y,
        Component.bCommonVelocityValid ? TEXT("true") : TEXT("false"),
        Component.bFullJointVelocitySafe ? TEXT("true") : TEXT("false"),
        Component.bCoherentTranslationApplied ? TEXT("true") : TEXT("false"),
        Component.CoherentTranslation.X, Component.CoherentTranslation.Y,
        Component.bJointPreferredRecoveryApplied ? TEXT("true") : TEXT("false"),
        Component.SafeAlphaQ15);
      for (int32 Index = 0; Index < Component.PreTranslationVelocities.Num(); ++Index)
      {
        const auto& Velocity = Component.PreTranslationVelocities[Index];
        Json += FString::Printf(TEXT("%s{\"id\":%d,\"v\":[%.1f,%.1f]}"),
          Index ? TEXT(",") : TEXT(""), Velocity.AgentId,
          Velocity.Velocity.X, Velocity.Velocity.Y);
      }
      Json += TEXT("],\"pre_recovery\":[");
      for (int32 Index = 0; Index < Component.PreRecoveryVelocities.Num(); ++Index)
      {
        const auto& Velocity = Component.PreRecoveryVelocities[Index];
        Json += FString::Printf(TEXT("%s{\"id\":%d,\"v\":[%.1f,%.1f]}"),
          Index ? TEXT(",") : TEXT(""), Velocity.AgentId,
          Velocity.Velocity.X, Velocity.Velocity.Y);
      }
      Json += TEXT("],\"recovered\":[");
      for (int32 Index = 0; Index < Component.RecoveredVelocities.Num(); ++Index)
      {
        const auto& Velocity = Component.RecoveredVelocities[Index];
        Json += FString::Printf(TEXT("%s{\"id\":%d,\"v\":[%.1f,%.1f]}"),
          Index ? TEXT(",") : TEXT(""), Velocity.AgentId,
          Velocity.Velocity.X, Velocity.Velocity.Y);
      }
      Json += TEXT("],\"joint\":[");
      for (int32 Index = 0; Index < Component.JointProjectedVelocities.Num(); ++Index)
      {
        const auto& Velocity = Component.JointProjectedVelocities[Index];
        Json += FString::Printf(TEXT("%s{\"id\":%d,\"v\":[%.1f,%.1f]}"),
          Index ? TEXT(",") : TEXT(""), Velocity.AgentId,
          Velocity.Velocity.X, Velocity.Velocity.Y);
      }
      Json += TEXT("],\"final\":[");
      for (int32 Index = 0; Index < Component.FinalVelocities.Num(); ++Index)
      {
        const auto& Velocity = Component.FinalVelocities[Index];
        Json += FString::Printf(TEXT("%s{\"id\":%d,\"v\":[%.1f,%.1f]}"),
          Index ? TEXT(",") : TEXT(""), Velocity.AgentId,
          Velocity.Velocity.X, Velocity.Velocity.Y);
      }
      Json += FString::Printf(TEXT("]}%s\n"),
        ComponentIndex + 1 < Fixture.Trace.Components.Num()
          ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ]\n}\n");
    return Json;
  }

  FString SerializeTargetRegionPlanLifecycleFixture(
    const FCrowdDemoTargetRegionPlanLifecycleFixture& Fixture)
  {
    FString Json = FString::Printf(
      TEXT("{\n  \"contract_version\":2,\n  \"valid\":%s,\n  \"stable_hash\":%u,\n  \"fixed_step\":%d,\n  \"capability_profile_key\":%u,\n  \"final_missing_region_key\":%d,\n  \"observed_deficit_region_key\":%d,\n  \"selection_kind\":%d,\n  \"previous_plan_targets_region\":%s,\n  \"new_plan_targets_region\":%s,\n  \"selected_reason\":%d,\n  \"condition_mask\":%u,\n  \"plan_age_steps\":%d,\n  \"target\":{\"revision\":%d,\"location_cm\":[%d,%d]},\n"),
      Fixture.bValid ? TEXT("true") : TEXT("false"), Fixture.StableHash,
      Fixture.FixedStepIndex, Fixture.CapabilityProfileKey,
      Fixture.FinalMissingRegionKey, Fixture.ObservedDeficitRegionKey,
      static_cast<int32>(Fixture.SelectionKind),
      Fixture.bPreviousPlanTargetsObservedRegion ? TEXT("true") : TEXT("false"),
      Fixture.bNewPlanTargetsObservedRegion ? TEXT("true") : TEXT("false"),
      Fixture.SelectedReason,
      Fixture.ConditionMask, Fixture.PlanAgeSteps,
      Fixture.TargetRevision, Fixture.TargetLocationCm.X, Fixture.TargetLocationCm.Y);
    Json += FString::Printf(
      TEXT("  \"graph\":{\"previous\":[%u,%u,%u],\"current\":[%u,%u,%u]},\n  \"claims\":{\"active\":%d,\"geometry_eligible\":%d,\"supply_eligible\":%d,\"new_plan_eligible\":%d,\"migrated\":%d,\"completed_at_replacement\":%d,\"dropped_still_feasible\":%d},\n  \"execution_invalid\":{\"state_mismatch\":%d,\"claim_off_edge\":%d,\"quota_exceeded\":%d,\"supply_without_outgoing\":%d,\"other\":%d},\n"),
      Fixture.PreviousGraph.CellFeasibilityHash,
      Fixture.PreviousGraph.EdgeSetHash, Fixture.PreviousGraph.EdgeCostHash,
      Fixture.CurrentGraph.CellFeasibilityHash,
      Fixture.CurrentGraph.EdgeSetHash, Fixture.CurrentGraph.EdgeCostHash,
      Fixture.ActiveClaimCount, Fixture.GeometryEligibleClaimCount,
      Fixture.SupplyEligibleClaimCount,
      Fixture.NewPlanEligibleClaimCount, Fixture.MigratedClaimCount,
      Fixture.CompletedAtReplacementClaimCount,
      Fixture.DroppedStillFeasibleClaimCount,
      Fixture.ExecutionInvalid.StateMismatchCount,
      Fixture.ExecutionInvalid.ClaimOffEdgeCount,
      Fixture.ExecutionInvalid.QuotaExceededCount,
      Fixture.ExecutionInvalid.SupplyWithoutOutgoingQuotaCount,
      Fixture.ExecutionInvalid.OtherInvalidCount);
    const auto AppendPlan = [&Json](const TCHAR* Name,
      const FCrowdDemoTargetRegionFlowPlan& Plan)
    {
      Json += FString::Printf(TEXT("  \"%s\":{\"epoch\":%d,\"build_step\":%d,\"target_revision\":%d,\"graph_hash\":%u,\"membership_hash\":%u,\"transport_hash\":%u,\"flows\":["),
        Name, Plan.PlanEpoch, Plan.BuildFixedStepIndex, Plan.TargetRevision,
        Plan.FeasibleGraphHash, Plan.MembershipHash, Plan.TransportHash);
      for (int32 Index = 0; Index < Plan.EdgeFlows.Num(); ++Index)
      {
        const auto& Edge = Plan.EdgeFlows[Index];
        Json += FString::Printf(TEXT("%s{\"from\":%d,\"to\":%d,\"quota\":%d,\"reused\":%d}"),
          Index ? TEXT(",") : TEXT(""), Edge.FromCellKey, Edge.ToCellKey,
          Edge.AgentQuota, Edge.ReusedQuota);
      }
      Json += TEXT("]},\n");
    };
    AppendPlan(TEXT("previous_plan"), Fixture.PreviousPlan);
    AppendPlan(TEXT("new_plan"), Fixture.NewPlan);
    Json += TEXT("  \"previous_execution\":{\"edges\":[");
    for (int32 Index = 0; Index < Fixture.PreviousExecution.Edges.Num(); ++Index)
    {
      const auto& Edge = Fixture.PreviousExecution.Edges[Index];
      Json += FString::Printf(TEXT("%s{\"from\":%d,\"to\":%d,\"initial\":%d,\"consumed\":%d}"),
        Index ? TEXT(",") : TEXT(""), Edge.FromCellKey, Edge.ToCellKey,
        Edge.InitialQuota, Edge.ConsumedQuota);
    }
    Json += TEXT("],\"claims\":[");
    for (int32 Index = 0; Index < Fixture.PreviousExecution.ActiveClaims.Num(); ++Index)
    {
      const auto& Claim = Fixture.PreviousExecution.ActiveClaims[Index];
      Json += FString::Printf(TEXT("%s{\"agent\":%d,\"from\":%d,\"to\":%d}"),
        Index ? TEXT(",") : TEXT(""), Claim.AgentId, Claim.FromCellKey, Claim.ToCellKey);
    }
    Json += TEXT("]},\n  \"new_execution\":{\"edges\":[");
    for (int32 Index = 0; Index < Fixture.NewExecution.Edges.Num(); ++Index)
    {
      const auto& Edge = Fixture.NewExecution.Edges[Index];
      Json += FString::Printf(TEXT("%s{\"from\":%d,\"to\":%d,\"initial\":%d,\"consumed\":%d}"),
        Index ? TEXT(",") : TEXT(""), Edge.FromCellKey, Edge.ToCellKey,
        Edge.InitialQuota, Edge.ConsumedQuota);
    }
    Json += TEXT("],\"claims\":[");
    for (int32 Index = 0; Index < Fixture.NewExecution.ActiveClaims.Num(); ++Index)
    {
      const auto& Claim = Fixture.NewExecution.ActiveClaims[Index];
      Json += FString::Printf(TEXT("%s{\"agent\":%d,\"from\":%d,\"to\":%d}"),
        Index ? TEXT(",") : TEXT(""), Claim.AgentId, Claim.FromCellKey, Claim.ToCellKey);
    }
    Json += TEXT("]},\n  \"agents\":[");
    for (int32 Index = 0; Index < Fixture.Agents.Num(); ++Index)
    {
      const auto& Agent = Fixture.Agents[Index];
      const auto* State = Fixture.Demand.AgentStates.FindByPredicate(
        [&Agent](const auto& Candidate) { return Candidate.AgentId == Agent.AgentId; });
      Json += FString::Printf(TEXT("%s{\"id\":%d,\"location_cm\":[%d,%d],\"velocity_cmps\":[%d,%d],\"cell\":%d,\"region\":%d,\"terminal\":%d,\"supply\":%d}"),
        Index ? TEXT(",") : TEXT(""), Agent.AgentId,
        FMath::RoundToInt(Agent.Location.X), FMath::RoundToInt(Agent.Location.Y),
        FMath::RoundToInt(Agent.Velocity.X), FMath::RoundToInt(Agent.Velocity.Y),
        State ? State->CurrentCellKey : INDEX_NONE,
        State ? State->CurrentRegionKey : INDEX_NONE,
        State && State->bTerminal ? 1 : 0, State && State->bSupply ? 1 : 0);
    }
    Json += TEXT("],\n  \"regions\":[");
    for (int32 Index = 0; Index < Fixture.Demand.Regions.Num(); ++Index)
    {
      const auto& Region = Fixture.Demand.Regions[Index];
      Json += FString::Printf(TEXT("%s{\"key\":%d,\"feasible\":%d,\"current\":%d,\"desired\":%d,\"deficit\":%d,\"surplus\":%d}"),
        Index ? TEXT(",") : TEXT(""), Region.StableRegionKey,
        Region.bFeasible ? 1 : 0, Region.CurrentPopulation,
        Region.DesiredPopulation, Region.Deficit, Region.Surplus);
    }
    Json += TEXT("]\n}\n");
    return Json;
  }

}

ACrowdDemoRoundSimCoordinator::ACrowdDemoRoundSimCoordinator()
{
  PrimaryActorTick.bCanEverTick = true;
  bReplicates = true;
  bAlwaysRelevant = true;
  SetNetUpdateFrequency(10.0f);
  SetMinNetUpdateFrequency(5.0f);
}

void ACrowdDemoRoundSimCoordinator::BeginPlay()
{
  Super::BeginPlay();
  bRequireValidationClientReady = FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoRequireClientReady"));
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoReadyLeadSeconds="), ValidationReadyLeadSeconds);
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoReadyTimeoutSeconds="), ValidationReadyTimeoutSeconds);
  ValidationReadyLeadSeconds = FMath::Clamp(ValidationReadyLeadSeconds, 0.5f, 10.0f);
  ValidationReadyTimeoutSeconds = FMath::Clamp(ValidationReadyTimeoutSeconds, 5.0f, 180.0f);
  UE_LOG(LogTemp, Display, TEXT("CrowdDemoRoundSim: START role=%s source=RoundSimCoordinator"),
    HasAuthority() ? TEXT("server") : TEXT("client"));
}

void ACrowdDemoRoundSimCoordinator::NotifyValidationClientReady(
  const int32 AgentCount,
  const int32 VisibleInstances)
{
  if (!HasAuthority() || !bRequireValidationClientReady || bValidationClientReady)
  {
    return;
  }
  const int32 ExpectedCount = RoundBootstrapPacket.bValid != 0
    ? RoundBootstrapPacket.Agents.Num()
    : (GetWorld() && GetWorld()->GetSubsystem<UCrowdDemoMassSubsystem>()
      ? GetWorld()->GetSubsystem<UCrowdDemoMassSubsystem>()->GetTrackedAgentCount()
      : 0);
  if (AgentCount != ExpectedCount
    || VisibleInstances != ExpectedCount
    || ExpectedCount <= 0)
  {
    UE_LOG(
      LogTemp,
      Warning,
      TEXT("CrowdDemoValidationReady role=server action=reject agents=%d visible_instances=%d expected=%d source=RoundSimCoordinator"),
      AgentCount,
      VisibleInstances,
      ExpectedCount);
    return;
  }
  bValidationClientReady = true;
  ValidationReadyServerTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoValidationReady role=server action=accepted agents=%d visible_instances=%d round_start_lead_seconds=%.3f source=RoundSimCoordinator"),
    AgentCount,
    VisibleInstances,
    ValidationReadyLeadSeconds);
}

void ACrowdDemoRoundSimCoordinator::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  if (HasAuthority())
  {
    TickServer();
  }
  else
  {
    TickClient();
  }
}

void ACrowdDemoRoundSimCoordinator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(ACrowdDemoRoundSimCoordinator, CurrentRoundPlan);
}

bool ACrowdDemoRoundSimCoordinator::IsRoundSimActive() const
{
  const UWorld* World = GetWorld();
  const UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  return Pipeline && Pipeline->IsActive();
}

const FCrowdDemoRoundCompareMetrics& ACrowdDemoRoundSimCoordinator::GetLastCompareMetrics() const
{
  const UWorld* World = GetWorld();
  const UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  return Pipeline ? Pipeline->GetLastCompareMetrics() : LastCompareMetrics;
}

const FCrowdDemoCorrectionFrameMetrics& ACrowdDemoRoundSimCoordinator::GetLastCorrectionFrameMetrics() const
{
  const UWorld* World = GetWorld();
  const UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  return Pipeline ? Pipeline->GetLastCorrectionMetrics() : LastCorrectionFrameMetrics;
}

void ACrowdDemoRoundSimCoordinator::OnRep_CurrentRoundPlan()
{
  QueueClientRoundPlan(CurrentRoundPlan);
}

void ACrowdDemoRoundSimCoordinator::ConsumeProductRoundResultHeader(
  const FCrowdDemoRoundResultHeader& Header)
{
  if (HasAuthority() || Header.bValid == 0)
  {
    return;
  }
  RoundResultHeader = Header;
  ++RoundResultHeaderReceivedCount;
  PendingClientResultHeaders.Add(RoundResultHeader.StateFrameRevision, RoundResultHeader);
  PendingClientResultHeaderReceiveTimes.Add(
    RoundResultHeader.StateFrameRevision,
    GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
  if (RoundResultHeader.ProjectileMetrics.ProjectileSpawnedCount > 0)
  {
    bPendingProjectileVisualValidation = true;
    PendingProjectileVisualRoundId = RoundResultHeader.RoundId;
    PendingProjectileVisualValidationStartSeconds =
      GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
  }
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundResultTransport role=client stage=header_received contract=%u payload=%u bytes=%d round_id=%d checkpoint_revision=%d state_frame_revision=%d agents=%d header_received_count=%d source=RoundSim"),
    RoundResultHeader.ContractVersion,
    RoundResultHeader.PayloadKind,
    RoundResultHeader.SerializedByteCount,
    RoundResultHeader.RoundId,
    RoundResultHeader.CheckpointRevision,
    RoundResultHeader.StateFrameRevision,
    RoundResultHeader.AgentCount,
    RoundResultHeaderReceivedCount);
  TryProcessClientCorrectionAssemblies();
}

void ACrowdDemoRoundSimCoordinator::MulticastRoundPlan_Implementation(const FCrowdDemoRoundPlanPacket& Plan)
{
  if (!HasAuthority())
  {
    QueueClientRoundPlan(Plan);
  }
}

void ACrowdDemoRoundSimCoordinator::TickServer()
{
  UWorld* World = GetWorld();
  UCrowdDemoMassSubsystem* MassSubsystem = World ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!World || !MassSubsystem || !Pipeline || !IsCrowdDemoRoundSimScenario(MassSubsystem->GetScenario()))
  {
    return;
  }

  RefreshProductReplicationChannels();

  TArray<FCrowdDemoProjectileVisualEvent> ProjectileVisualEvents;
  if (Pipeline->DequeueProjectileVisualEvents(ProjectileVisualEvents))
  {
    PublishProductProjectileEvents(ProjectileVisualEvents);
  }

  const float NowSeconds = World->GetTimeSeconds();
  if (RoundBootstrapPacket.bValid == 0)
  {
    if (bRequireValidationClientReady)
    {
      if (!bValidationClientReady)
      {
        if (!bValidationReadyTimeoutLogged && NowSeconds >= ValidationReadyTimeoutSeconds)
        {
          bValidationReadyTimeoutLogged = true;
          UE_LOG(
            LogTemp,
            Error,
            TEXT("VIOLATION CrowdDemoValidationReady role=server action=timeout timeout_seconds=%.3f source=RoundSimCoordinator"),
            ValidationReadyTimeoutSeconds);
        }
        return;
      }
      if (NowSeconds < ValidationReadyServerTimeSeconds + ValidationReadyLeadSeconds)
      {
        return;
      }
    }
    else if (NowSeconds < CrowdDemoRoundInitialStartDelaySeconds)
    {
      return;
    }
    StartServerRound(*MassSubsystem, NowSeconds + CrowdDemoRoundStartLeadSeconds);
    return;
  }

  PublishServerCorrectionFrame();

  if (Pipeline->IsActive() && !bRoundResultPublished)
  {
    const float RoundEndServerTime = CurrentRoundPlan.StartServerTimeSeconds + CurrentRoundPlan.DurationSeconds;
    if (!bNextRoundPlanPublished && NowSeconds >= RoundEndServerTime - 1.0f)
    {
      const FVector NextStart = FVector(CurrentRoundPlan.Rules.FlowFieldConfig.GoalLocation);
      PendingServerRoundPlan = BuildRoundPlanPacket(
        *MassSubsystem,
        CurrentRoundPlan.RoundId + 1,
        Revision + 1,
        LastCheckpointRevision + 1,
        RoundEndServerTime,
        NextStart,
        RoundBootstrapPacket.Agents.Num());
      PublishServerRoundPlan(PendingServerRoundPlan);
      bNextRoundPlanPublished = true;
    }
    PublishServerResult(*MassSubsystem, RoundEndServerTime);
    return;
  }

  if (bRoundResultPublished && NowSeconds - LastRoundCompletedWorldSeconds >= CrowdDemoRoundRestartDelaySeconds)
  {
    if (PendingServerRoundPlan.bValid != 0)
    {
      ActivateServerRoundPlan(*MassSubsystem, PendingServerRoundPlan);
      PendingServerRoundPlan = FCrowdDemoRoundPlanPacket();
    }
  }
}

void ACrowdDemoRoundSimCoordinator::TickClient()
{
  UWorld* World = GetWorld();
  if (!World)
  {
    return;
  }

  ConsumeProductReplicationChannels();
  if (RoundBootstrapPacket.bValid == 0)
  {
    return;
  }

  DropExpiredCorrectionAssemblies();
  TryProcessClientCorrectionAssemblies();
  TryValidateProjectileVisualEvents();
}

void ACrowdDemoRoundSimCoordinator::TryValidateProjectileVisualEvents()
{
  if (!bPendingProjectileVisualValidation || PendingProjectileVisualRoundId == INDEX_NONE)
  {
    return;
  }
  UWorld* World = GetWorld();
  ACrowdDemoReplicator* VisualHost = World ? FindProjectileVisualHost(*World) : nullptr;
  int32 Spawn = 0;
  int32 Impact = 0;
  int32 Expire = 0;
  int32 Active = 0;
  const bool bHasCounts = VisualHost && VisualHost->GetProjectileVisualEventCounts(
    PendingProjectileVisualRoundId, Spawn, Impact, Expire, Active);
  const FCrowdDemoProjectileMetrics& Expected = RoundResultHeader.ProjectileMetrics;
  const bool bMatches = bHasCounts
    && Spawn == Expected.VisualSpawnEventCount
    && Impact == Expected.VisualImpactEventCount
    && Expire == Expected.VisualExpireEventCount
    && Active == Expected.ProjectileActiveCount;
  if (bMatches)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoProjectileVisual role=client round_id=%d valid=1 spawn=%d impact=%d expire=%d active=%d source=ProjectileEventISM"),
      PendingProjectileVisualRoundId, Spawn, Impact, Expire, Active);
    bPendingProjectileVisualValidation = false;
    PendingProjectileVisualRoundId = INDEX_NONE;
    return;
  }

  const double NowSeconds = World ? World->GetTimeSeconds() : 0.0;
  const bool bExceededExpected = Spawn > Expected.VisualSpawnEventCount
    || Impact > Expected.VisualImpactEventCount
    || Expire > Expected.VisualExpireEventCount
    || Active > Expected.ProjectileActiveCount;
  if (bExceededExpected || NowSeconds - PendingProjectileVisualValidationStartSeconds >= 3.0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoProjectileVisual role=client round_id=%d valid=0 spawn=%d/%d impact=%d/%d expire=%d/%d active=%d/%d VIOLATION"),
      PendingProjectileVisualRoundId, Spawn, Expected.VisualSpawnEventCount,
      Impact, Expected.VisualImpactEventCount, Expire, Expected.VisualExpireEventCount,
      Active, Expected.ProjectileActiveCount);
    bPendingProjectileVisualValidation = false;
    PendingProjectileVisualRoundId = INDEX_NONE;
  }
}

void ACrowdDemoRoundSimCoordinator::StartServerRound(UCrowdDemoMassSubsystem& MassSubsystem, const float StartServerTimeSeconds)
{
  RoundBootstrapPacket = FCrowdDemoRoundBootstrapPacket();
  RoundBootstrapPacket.bValid = 1;
  RoundBootstrapPacket.Revision = 1;
  RoundBootstrapPacket.ServerTimeSeconds = StartServerTimeSeconds;
  MassSubsystem.BuildRoundAgentStates(RoundBootstrapPacket.Agents);
  RoundBootstrapPacket.Agents.Sort([](const FCrowdDemoRoundAgentState& A, const FCrowdDemoRoundAgentState& B)
  {
    return A.AgentId < B.AgentId;
  });

  TArray<FCrowdRelevantSnapshotEntityPayload> EntityPayloads;
  FCrowdRelevantSnapshotHeader SnapshotHeader;
  TArray<FCrowdRelevantSnapshotChunk> SnapshotChunks;
  const FCrowdRelevantSnapshotLimits SnapshotLimits = FCrowdDemoRelevantSnapshotAdapter::MakeLimits();
  const bool bBuiltSnapshot = FCrowdDemoRelevantSnapshotAdapter::EncodeAgents(
      RoundBootstrapPacket.Agents,
      EntityPayloads)
    && FCrowdRelevantSnapshotTransport::Build(
      static_cast<uint32>(RoundBootstrapPacket.Revision),
      0,
      static_cast<uint32>(RoundBootstrapPacket.Revision),
      EntityPayloads,
      SnapshotLimits,
      SnapshotHeader,
      SnapshotChunks);
  if (!bBuiltSnapshot)
  {
    UE_LOG(
      LogTemp,
      Error,
      TEXT("VIOLATION CrowdDemoBootstrapSnapshot role=server stage=build revision=%d agents=%d source=RelevantSnapshot"),
      RoundBootstrapPacket.Revision,
      RoundBootstrapPacket.Agents.Num());
    RoundBootstrapPacket = FCrowdDemoRoundBootstrapPacket();
    return;
  }

  BootstrapSnapshotMetadata = FCrowdDemoBootstrapSnapshotMetadata();
  BootstrapSnapshotMetadata.bValid = 1;
  BootstrapSnapshotMetadata.ServerTimeSeconds = StartServerTimeSeconds;
  BootstrapSnapshotMetadata.SnapshotHeader = SnapshotHeader;
  CurrentProductBootstrapChunks = MoveTemp(SnapshotChunks);
  for (TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
    : ProductReplicationChannels)
  {
    if (AMassCrowdReplicationActor* Channel = Pair.Value.Get())
      PublishProductBaseline(*Channel);
  }

  const FVector StartLocation = ComputeRoundCohortCenter(RoundBootstrapPacket.Agents);
  const FCrowdDemoRoundPlanPacket FirstPlan = BuildRoundPlanPacket(
    MassSubsystem,
    1,
    1,
    0,
    StartServerTimeSeconds,
    StartLocation,
    RoundBootstrapPacket.Agents.Num());
  PublishServerRoundPlan(FirstPlan);
  if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = GetWorld()->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
  {
    Pipeline->QueueBootstrap(RoundBootstrapPacket);
  }
  ActivateServerRoundPlan(MassSubsystem, FirstPlan);
}

void ACrowdDemoRoundSimCoordinator::ActivateServerRoundPlan(
  UCrowdDemoMassSubsystem& MassSubsystem,
  const FCrowdDemoRoundPlanPacket& Plan)
{
  CurrentRoundPlan = Plan;
  if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = GetWorld()->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
  {
    Pipeline->QueueRoundPlan(Plan);
  }
  Revision = Plan.Revision;
  NextRoundId = Plan.RoundId + 1;
  bHasContinuousRoundPlan = true;
  bNextRoundPlanPublished = false;
  bRoundResultPublished = false;
  bClientComparedLatestResult = false;
  LastCorrectionFrameWorldSeconds = -1000.0;
  RefreshLastCompareCounters();
  RefreshLastCorrectionCounters();

  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundInit role=server round_id=%d revision=%d previous_checkpoint_revision=%d agents=%d start_server_time=%.3f duration=%.3f nominal_duration=%.3f completion_grace=%.3f fixed_step=%.4f scenario=%d source=RoundPlan"),
    Plan.RoundId,
    Plan.Revision,
    Plan.PreviousCheckpointRevision,
    RoundBootstrapPacket.Agents.Num(),
    Plan.StartServerTimeSeconds,
    Plan.DurationSeconds,
    Plan.NominalDurationSeconds,
    Plan.CompletionGraceSeconds,
    Plan.Rules.FixedStepSeconds,
    static_cast<int32>(Plan.Rules.Scenario));
}

void ACrowdDemoRoundSimCoordinator::PublishServerRoundPlan(const FCrowdDemoRoundPlanPacket& Plan)
{
  // Publication is a network operation only. The authority queues the plan in
  // ActivateServerRoundPlan after the old result/checkpoint has been frozen.
  MulticastRoundPlan(Plan);
  ForceNetUpdate();
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundPlan role=server round_id=%d revision=%d previous_checkpoint_revision=%d start_server_time=%.3f duration=%.3f nominal_duration=%.3f completion_grace=%.3f agents=%d action=published source=RoundPlan"),
    Plan.RoundId,
    Plan.Revision,
    Plan.PreviousCheckpointRevision,
    Plan.StartServerTimeSeconds,
    Plan.DurationSeconds,
    Plan.NominalDurationSeconds,
    Plan.CompletionGraceSeconds,
    RoundBootstrapPacket.Agents.Num());
}

void ACrowdDemoRoundSimCoordinator::PublishServerResult(UCrowdDemoMassSubsystem& MassSubsystem, const float EndServerTimeSeconds)
{
  UWorld* World = GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline || !Pipeline->DequeueOutgoingRoundResult(RoundResultPacket))
  {
    return;
  }
  LastCheckpointRevision = RoundResultPacket.CheckpointRevision;
  RoundResultHeader = MakeRoundResultHeader(
    RoundResultPacket, CurrentRoundPlan.Rules.SoftPressureTestCase);
  ++RoundResultBuiltCount;
  ++RoundResultHeaderPublishedCount;
  bRoundResultPublished = true;
  ++CompletedRoundCount;
  RefreshLastCompareCounters();
  LastRoundCompletedWorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
  PublishProductRoundResultHeader(RoundResultHeader);
  ForceNetUpdate();

  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundResultTransport role=server stage=header_published round_id=%d checkpoint_revision=%d state_frame_revision=%d agents=%d built_count=%d header_published_count=%d source=RoundSim"),
    RoundResultHeader.RoundId,
    RoundResultHeader.CheckpointRevision,
    RoundResultHeader.StateFrameRevision,
    RoundResultHeader.AgentCount,
    RoundResultBuiltCount,
    RoundResultHeaderPublishedCount);

  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundCheckpoint role=server round_id=%d revision=%d checkpoint_revision=%d completed_round_count=%d agents=%d initial_overlap_pair_count=%d overlap_pair_count=%d overlap_reduction=%d initial_severe_overlap_pair_count=%d severe_overlap_pair_count=%d severe_overlap_reduction=%d obstacle_penetration_count=%d arrival_count=%d end_server_time=%.3f source=RoundSim"),
    RoundResultPacket.RoundId,
    RoundResultPacket.Revision,
    RoundResultPacket.CheckpointRevision,
    CompletedRoundCount,
    RoundResultPacket.Agents.Num(),
    RoundResultPacket.InitialOverlapPairCount,
    RoundResultPacket.OverlapPairCount,
    RoundResultPacket.InitialOverlapPairCount - RoundResultPacket.OverlapPairCount,
    RoundResultPacket.InitialSevereOverlapPairCount,
    RoundResultPacket.SevereOverlapPairCount,
    RoundResultPacket.InitialSevereOverlapPairCount - RoundResultPacket.SevereOverlapPairCount,
    RoundResultPacket.ObstaclePenetrationCount,
    RoundResultPacket.ArrivalCount,
    RoundResultPacket.EndServerTimeSeconds);

  if (RoundResultPacket.ProjectileMetrics.ProjectileSpawnedCount > 0)
  {
    const FCrowdDemoProjectileMetrics& Projectile = RoundResultPacket.ProjectileMetrics;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoProjectileCheckpoint role=server round_id=%d valid=%d acquired=%d windup=%d spawned=%d active=%d impacted=%d expired=%d duplicate_fire=%d duplicate_hit=%d damage=%d visual_spawn=%d visual_impact=%d visual_expire=%d invalid_target_lifecycle=%d invalid_projectile=%d attack_hash=%u projectile_hash=%u event_hash=%u source=RoundSim%s"),
      RoundResultPacket.RoundId, Projectile.bValid, Projectile.TargetAcquiredCount,
      Projectile.CompletedWindupCount, Projectile.ProjectileSpawnedCount,
      Projectile.ProjectileActiveCount, Projectile.ProjectileImpactedCount,
      Projectile.ProjectileExpiredCount, Projectile.DuplicateFireCount,
      Projectile.DuplicateHitCount, Projectile.DamageAppliedCount,
      Projectile.VisualSpawnEventCount, Projectile.VisualImpactEventCount,
      Projectile.VisualExpireEventCount, Projectile.InvalidTargetLifecycleCount,
      Projectile.InvalidProjectileCount, Projectile.AttackStateHash,
      Projectile.ProjectileStateHash, Projectile.EventHash,
      Projectile.bValid ? TEXT("") : TEXT(" VIOLATION"));
    if (!Projectile.bValid)
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoProjectileCheckpoint role=server round_id=%d valid=0 VIOLATION"),
        RoundResultPacket.RoundId);
  }

  if (IsCrowdDemoRoundSimScenario(Pipeline->GetRules().Scenario))
  {
    const FCrowdDemoRoundCompareMetrics& Metrics = Pipeline->GetLastCompletedRoundMetrics();
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoFlowCheckpoint role=server round_id=%d agents=%d flow_field_revision=%d flow_field_build_hash=%u flow_field_rebuild_count=%d flow_unreachable_agent_count=%d flow_goal_reached_count=%d flow_wall_pass_count=%d flow_corridor_exit_count=%d flow_turn_exit_count=%d corridor_deadlock_agent_count=%d server_obstacle_penetration_count=%d source=MassPipeline"),
      RoundResultPacket.RoundId,
      RoundResultPacket.Agents.Num(),
      Metrics.FlowFieldRevision,
      Metrics.FlowFieldBuildHash,
      Metrics.FlowFieldRebuildCount,
      Metrics.FlowUnreachableAgentCount,
      Metrics.FlowGoalReachedCount,
      Metrics.FlowWallPassCount,
      Metrics.FlowCorridorExitCount,
      Metrics.FlowTurnExitCount,
      Metrics.CorridorDeadlockAgentCount,
      Metrics.ServerObstaclePenetrationCount);
  }

  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    const FCrowdDemoRoundCompareMetrics& Metrics = Pipeline->GetLastCompletedRoundMetrics();
    const FCrowdDemoParticleMetrics& Particle = RoundResultPacket.ParticleMetrics;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoParticleCheckpoint role=server round_id=%d agents=%d navigation_hard_clearance_cm=%.3f flow_connectivity_contract_version=%d flow_field_build_hash=%u flow_valid_directed_edges=%d flow_recovered_count=%d desired_segment_hard_obstacle_violation_count=%d soft_pair_count=%d soft_violating_pair_count=%d soft_error_cm_p50=%.3f soft_error_cm_p95=%.3f soft_error_cm_max=%.3f hard_pair_violation_count=%d swept_pair_violation_count=%d pressure_influenced_agent_count=%d first_influenced_iteration_max=%d particle_corrected_agent_count=%d max_agent_correction_cm=%.3f settling_steps=%d obstacle_penetration_count=%d bounds_violation_count=%d environment_soft_contact_count=%d environment_soft_applied_agent_count=%d environment_soft_error_cm_p50=%.3f environment_soft_error_cm_p95=%.3f environment_soft_error_cm_max=%.3f environment_soft_requested_correction_cm_max=%.3f environment_soft_realized_correction_cm_max=%.3f unified_hard_constraint_count=%d unified_hard_residual_cm_max=%.3f unified_hard_infeasible_count=%d invalid_step_count=%d global_fallback_step_count=%d particle_solver_ms_p95=%.3f particle_candidate_hash=%u particle_applied_state_hash=%u failure_fixture_step=%d failure_pair=%d,%d failure_fixture_hash=%u rollback_hit=%d rollback_miss=%d rollback_mismatch=%d rollback_replayed_steps=%d source=MassPipeline"),
      RoundResultPacket.RoundId, RoundResultPacket.Agents.Num(),
      Pipeline->GetRules().GetParticleEnvironmentHardClearanceCm(),
      Pipeline->GetRules().FlowFieldConfig.ConnectivityContractVersion,
      RoundResultPacket.SharedFlowMetrics.SharedFlowFieldBuildHash,
      Pipeline->GetSharedFlowField().ValidDirectedEdgeCount,
      RoundResultPacket.SharedFlowMetrics.FlowRecoveredFromRasterMismatchCount,
      RoundResultPacket.SharedFlowMetrics.FlowDesiredSegmentHardObstacleViolationCount,
      Particle.SoftPairCount, Particle.SoftViolatingPairCount,
      Particle.SoftErrorCmP50, Particle.SoftErrorCmP95, Particle.SoftErrorCmMax,
      Particle.HardPairViolationCount, Particle.SweptPairViolationCount,
      Particle.PressureInfluencedAgentCount, Particle.FirstInfluencedIterationMax,
      Particle.ParticleCorrectedAgentCount, Particle.MaxAgentCorrectionCm,
      Particle.SettlingSteps, Particle.ObstaclePenetrationCount,
      Particle.BoundsViolationCount, Particle.EnvironmentSoftContactCount,
      Particle.EnvironmentSoftAppliedAgentCount, Particle.EnvironmentSoftErrorCmP50,
      Particle.EnvironmentSoftErrorCmP95, Particle.EnvironmentSoftErrorCmMax,
      Particle.EnvironmentSoftRequestedCorrectionCmMax,
      Particle.EnvironmentSoftRealizedCorrectionCmMax,
      Particle.UnifiedHardConstraintCount, Particle.UnifiedHardResidualCmMax,
      Particle.UnifiedHardInfeasibleCount, Particle.ParticleInvalidStepCount,
      Particle.ParticleGlobalFallbackStepCount, Particle.ParticleSolverMsP95,
      Particle.ParticleCandidateHash, Particle.ParticleAppliedStateHash,
      Particle.ParticleFailureFixtureStep, Particle.ParticleFailureMinAgentId,
      Particle.ParticleFailureMaxAgentId, Particle.ParticleFailureFixtureHash,
      Particle.RollbackSnapshotHitCount, Particle.RollbackSnapshotMissCount,
      Particle.RollbackAgentMismatchCount, Particle.RollbackReplayedStepCount);
    const FCrowdDemoRoundPerformanceMetrics& Performance = Particle.Performance;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoPerformanceCheckpoint role=server round_id=%d fixed_step_ms_p50=%.3f fixed_step_ms_p95=%.3f fixed_step_ms_max=%.3f business_prepare_ms_p95=%.3f business_prepare_ms_max=%.3f flow_ms_p95=%.3f flow_ms_max=%.3f topology_ms_p95=%.3f topology_ms_max=%.3f demand_ms_p95=%.3f demand_ms_max=%.3f plan_ms_p95=%.3f plan_ms_max=%.3f guidance_ms_p95=%.3f guidance_ms_max=%.3f guidance_compose_ms_p95=%.3f guidance_compose_ms_max=%.3f local_predictive_ms_p95=%.3f local_predictive_ms_max=%.3f particle_stage_ms_p95=%.3f particle_stage_ms_max=%.3f facing_finalize_ms_p95=%.3f facing_finalize_ms_max=%.3f commit_ms_p95=%.3f commit_ms_max=%.3f topology_builds=%d topology_cache_hits=%d demand_full_builds=%d demand_population_updates=%d steps_per_game_frame_p50=%.3f steps_per_game_frame_p95=%.3f steps_per_game_frame_max=%d catchup_frame_count=%d catchup_cpu_budget_hit_count=%d catchup_cpu_budget_consecutive_max=%d max_step_limit_hit_count=%d backlog_ms_max=%.3f simulation_realtime_factor=%.3f rollback_replay_ms_p95=%.3f rollback_replay_ms_max=%.3f rollback_replay_samples=%d zero_error_rollback_replay_count=%d source=MassPipeline"),
      RoundResultPacket.RoundId,
      Performance.FixedStepPipelineMsP50,
      Performance.FixedStepPipelineMsP95,
      Performance.FixedStepPipelineMsMax,
      Performance.BusinessPrepareStageMsP95,
      Performance.BusinessPrepareStageMsMax,
      Performance.SharedFlowStageMsP95,
      Performance.SharedFlowStageMsMax,
      Performance.TargetTopologyStageMsP95,
      Performance.TargetTopologyStageMsMax,
      Performance.TargetDemandStageMsP95,
      Performance.TargetDemandStageMsMax,
      Performance.TargetPlanStageMsP95,
      Performance.TargetPlanStageMsMax,
      Performance.TargetGuidanceStageMsP95,
      Performance.TargetGuidanceStageMsMax,
      Performance.GuidanceComposeStageMsP95,
      Performance.GuidanceComposeStageMsMax,
      Performance.LocalPredictiveStageMsP95,
      Performance.LocalPredictiveStageMsMax,
      Performance.ParticleStageMsP95,
      Performance.ParticleStageMsMax,
      Performance.FacingFinalizeStageMsP95,
      Performance.FacingFinalizeStageMsMax,
      Performance.CommitStageMsP95,
      Performance.CommitStageMsMax,
      Performance.TargetTopologyBuildCount,
      Performance.TargetTopologyCacheHitCount,
      Performance.TargetDemandFullBuildCount,
      Performance.TargetDemandPopulationUpdateCount,
      Performance.FixedStepsPerGameFrameP50,
      Performance.FixedStepsPerGameFrameP95,
      Performance.FixedStepsPerGameFrameMax,
      Performance.CatchupFrameCount,
      Performance.CatchupCpuBudgetHitCount,
      Performance.CatchupCpuBudgetConsecutiveMax,
      Performance.MaxFixedStepsPerFrameHitCount,
      Performance.FixedStepBacklogMsMax,
      Performance.SimulationRealtimeFactor,
      Performance.RollbackReplayMsP95,
      Performance.RollbackReplayMsMax,
      Performance.RollbackReplaySampleCount,
      Performance.ZeroErrorRollbackReplayCount);
    if (Pipeline->IsTargetStabilityDiagnosticEnabled())
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTargetStabilityDiagnostic role=server round_id=%d valid=%d cause=%d samples=%d window=%d agents=%d inside_min=%d coverage=%d/%d contended_steps=%d contended_groups=%d merge_blocked_agents=%d merge_blocked_max_steps=%d terminal_chatter_agents=%d terminal_chatter=%d attraction_rejection=%d particle_settled_windows=%d particle_settled_max_steps=%d target_relative_speed_p95=%.3f target_relative_speed_max=%.3f position_peak_to_peak_p95=%.3f position_peak_to_peak_max=%.3f witness_step=%d witness_agent=%d witness_next_cell=%d hash=%u source=MassPipeline%s"),
        RoundResultPacket.RoundId, Particle.bTargetStabilityDiagnosticValid,
        Particle.TargetStabilityPrimaryCause, Particle.TargetStabilitySampleStepCount,
        Particle.TargetStabilityWindowStepCount, Particle.TargetStabilityAgentCount,
        Particle.TargetStabilityInsideBandMin, Particle.TargetStabilityCoverageMin,
        Particle.TargetStabilityCoverageRequired, Particle.TargetStabilityContendedStepCount,
        Particle.TargetStabilityContendedGroupCount,
        Particle.TargetStabilityMergeBlockedAgentCount,
        Particle.TargetStabilityMergeBlockedMaxSteps,
        Particle.TargetStabilityTerminalChatterAgentCount,
        Particle.TargetStabilityTerminalChatterCount,
        Particle.TargetStabilityAttractionRejectionCycleCount,
        Particle.TargetStabilityParticleSettledWindowCount,
        Particle.TargetStabilityParticleSettledMaxSteps,
        Particle.TargetStabilityTargetRelativeSpeedCmpsP95,
        Particle.TargetStabilityTargetRelativeSpeedCmpsMax,
        Particle.TargetStabilityPositionPeakToPeakCmP95,
        Particle.TargetStabilityPositionPeakToPeakCmMax,
        Particle.TargetStabilityFirstWitnessStep,
        Particle.TargetStabilityFirstWitnessAgentId,
        Particle.TargetStabilityFirstWitnessNextCellKey,
        Particle.TargetStabilityDiagnosticHash,
        Particle.bTargetStabilityDiagnosticValid ? TEXT("") : TEXT(" VIOLATION"));
      const FCrowdDemoLocalPredictiveComponentFixture& Fixture =
        Pipeline->GetLocalPredictiveComponentFixture();
      FString FixtureOutputPath;
      const bool bHasFixtureOutputPath = FParse::Value(FCommandLine::Get(),
        TEXT("CrowdDemoLocalPredictiveFixtureOutput="), FixtureOutputPath);
      bool bFixtureWritten = false;
      if (Fixture.bValid && bHasFixtureOutputPath && !FixtureOutputPath.IsEmpty())
      {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.CreateDirectoryTree(*FPaths::GetPath(FixtureOutputPath));
        bFixtureWritten = FFileHelper::SaveStringToFile(
          SerializeLocalPredictiveComponentFixture(Fixture), *FixtureOutputPath,
          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
      }
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoLocalPredictiveComponentFixtureFile role=server round_id=%d valid=%d output_path=%d written=%d agents=%d witnesses=%d hash=%u source=MassPipeline%s"),
        RoundResultPacket.RoundId, Fixture.bValid ? 1 : 0,
        bHasFixtureOutputPath ? 1 : 0, bFixtureWritten ? 1 : 0,
        Fixture.Agents.Num(), Fixture.WitnessAgentIds.Num(), Fixture.StableHash,
        Fixture.bValid && bHasFixtureOutputPath && !bFixtureWritten
          ? TEXT(" VIOLATION") : TEXT(""));
      if (Fixture.bValid && bHasFixtureOutputPath && !bFixtureWritten)
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoLocalPredictiveComponentFixtureFile role=server round_id=%d write_failed=1 hash=%u VIOLATION"),
          RoundResultPacket.RoundId, Fixture.StableHash);
    }
    if (Particle.bT1Valid != 0)
    {
      FString ActiveTransitions;
      for (int32 Index = 0; Index < Particle.T1ActiveCountTransitions.Num(); ++Index)
      {
        if (Index) ActiveTransitions += TEXT(",");
        ActiveTransitions += FString::FromInt(Particle.T1ActiveCountTransitions[Index]);
      }
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT1Checkpoint role=server round_id=%d valid=%d phase=%d transitions=%d active=%d active_sequence=%s batches=%d inserted=%d removed=%d layer_max=%d influenced=%d insert_settle=%d post_remove_settle=%d old_layout_returned=%d new_equilibrium_displaced=%d external_preferred_nonzero=%d participation_hash=%u propagation_hash=%u phase_hash=%u source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bT1Valid, Particle.T1Phase,
        Particle.T1PhaseTransitionCount, Particle.T1ActiveAgentCount,
        *ActiveTransitions, Particle.T1BatchActivationCount,
        Particle.T1InsertedAgentId, Particle.T1RemovedAgentId,
        Particle.T1PressurePropagationLayerMax, Particle.T1InfluencedAgentCount,
        Particle.T1InsertSettlingStep, Particle.T1PostRemovalSettlingStep,
        Particle.T1OldLayoutReturnedAgentCount,
        Particle.T1NewEquilibriumDisplacedAgentCount,
        Particle.T1ExternalPreferredNonzeroCount,
        Particle.T1ParticipationHash, Particle.T1PropagationHash,
        Particle.T1PhaseHash);
    }
    if (Particle.T2LayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT2Checkpoint role=server round_id=%d valid=%d layout_hash=%u route_hash=%u progress_hash=%u flow_approach_entered_count=%d transport_handoff_count=%d inside_effective_band_count=%d feasible_region_count=%d feasible_region_coverage_count=%d plan_unrouted_count=%d guidance_unrouted_count=%d transport_validation_failure_count=%d terminal_settled_count=%d terminal_settled_step=%d applied_forward_cmps_p50=%.3f applied_forward_cmps_p95=%.3f flow_contract_violation_count=%d final_deadlock_agent_count=%d topology_hash=%u demand_hash=%u plan_hash=%u guidance_hash=%u validation_hash=%u source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bT2Valid, Particle.T2LayoutHash,
        Particle.T2RouteDiagnosticHash, Particle.T2ProgressHash,
        Particle.T2FlowApproachEnteredCount, Particle.T2TransportHandoffCount,
        Particle.T2InsideEffectiveBandCount, Particle.T2FeasibleRegionCount,
        Particle.T2FeasibleRegionCoverageCount, Particle.T2PlanUnroutedCount,
        Particle.T2GuidanceUnroutedCount,
        Particle.T2TransportValidationFailureCount,
        Particle.T2TerminalSettledCount, Particle.T2TerminalSettledStep,
        Particle.T2AppliedForwardCmpsP50, Particle.T2AppliedForwardCmpsP95,
        Particle.T2FlowContractViolationCount, Particle.T2FinalDeadlockAgentCount,
        Particle.TargetTransportTopologyHash, Particle.TargetTransportDemandHash,
        Particle.TargetTransportPlanHash, Particle.TargetTransportGuidanceHash,
        Particle.TargetTransportValidationHash);
    }
    if (Particle.T3LayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT3Checkpoint role=server round_id=%d valid=%d layout_hash=%u flow_hashes=%u,%u progress_hash=%u cohort_agents=%d,%d center_crossed=%d,%d completed=%d,%d total_completed=%d throughput_difference=%d final_deadlock=%d unreachable_samples=%d last_step=%d completion_step_max=%d source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bT3Valid, Particle.T3LayoutHash,
        Particle.T3Cohort0FlowHash, Particle.T3Cohort1FlowHash,
        Particle.T3ProgressHash,
        Particle.T3Cohort0AgentCount, Particle.T3Cohort1AgentCount,
        Particle.T3Cohort0CenterCrossedCount,
        Particle.T3Cohort1CenterCrossedCount,
        Particle.T3Cohort0CompletedCount, Particle.T3Cohort1CompletedCount,
        Particle.T3CompletedCount, Particle.T3ThroughputDifference,
        Particle.T3FinalDeadlockAgentCount, Particle.T3UnreachableSampleCount,
        Particle.T3LastFixedStep, Particle.T3CompletionStepMax);
    }
    if (Particle.T4LayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT4Checkpoint role=server round_id=%d valid=%d layout_hash=%u flow_hash=%u progress_hash=%u wall_passed=%d corridor_exited=%d completed=%d final_settled=%d final_deadlock=%d unreachable_samples=%d last_step=%d completion_step_max=%d group_completion_step=%d group_settled_step=%d source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bT4Valid, Particle.T4LayoutHash,
        Particle.T4FlowHash, Particle.T4ProgressHash,
        Particle.T4WallPassedCount, Particle.T4CorridorExitedCount,
        Particle.T4CompletedCount, Particle.T4FinalSettledCount,
        Particle.T4FinalDeadlockAgentCount,
        Particle.T4UnreachableSampleCount, Particle.T4LastFixedStep,
        Particle.T4CompletionStepMax, Particle.T4GroupCompletionStep,
        Particle.T4GroupSettledStep);
    }
    if (Particle.T6TransitLayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT6TransitCheckpoint role=server round_id=%d valid=%d layout_hash=%u flow_hash=%u progress_hash=%u capability_profiles=%d membership_hash=%u wall_passed=%d corridor_exited=%d completed=%d final_settled=%d final_deadlock=%d unreachable_samples=%d cross_profile_hard=%d cross_profile_swept=%d last_step=%d completion_step_max=%d group_completion_step=%d group_settled_step=%d source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bT6TransitValid,
        Particle.T6TransitLayoutHash, Particle.T6TransitFlowHash,
        Particle.T6TransitProgressHash, Particle.CapabilityProfileCount,
        Particle.CapabilityMembershipHash, Particle.T6TransitWallPassedCount,
        Particle.T6TransitCorridorExitedCount, Particle.T6TransitCompletedCount,
        Particle.T6TransitFinalSettledCount,
        Particle.T6TransitFinalDeadlockAgentCount,
        Particle.T6TransitUnreachableSampleCount,
        Particle.CrossProfileHardViolationCount,
        Particle.CrossProfileSweptViolationCount,
        Particle.T6TransitLastFixedStep, Particle.T6TransitCompletionStepMax,
        Particle.T6TransitGroupCompletionStep,
        Particle.T6TransitGroupSettledStep);
    }
    if (Pipeline->GetRules().TargetRegionTransportSettings.bEnabled != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTargetRegionTransportCheckpoint role=server round_id=%d valid=%d feasible_cells=%d edges=%d feasible_regions=%d raw_coverage=%d feasible_coverage=%d inside_band=%d max_region_population=%d desired=%d plan_routed=%d plan_unrouted=%d guidance_unrouted_steps=%d guidance_unrouted_samples=%d guidance_unrouted_max=%d first_failure_step=%d first_failure_agent=%d invalid_steps=%d validation_failures=%d total_cost=%lld changed_quota=%lld epoch=%d rebuilds=%d lifetime=%d target=%d environment=%d membership=%d satisfied=%d path_invalid=%d solver_ms_p95=%.3f topology_hash=%u demand_hash=%u transport_hash=%u guidance_hash=%u validation_hash=%u source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bTargetRegionTransportValid,
        Particle.TargetTransportFeasibleCellCount, Particle.TargetTransportEdgeCount,
        Particle.TargetTransportFeasibleRegionCount,
        Particle.TargetTransportRawRegionCoverageCount,
        Particle.TargetTransportFeasibleRegionCoverageCount,
        Particle.TargetTransportInsideEffectiveBandCount,
        Particle.TargetTransportMaximumRegionPopulation,
        Particle.TargetTransportDesiredPopulation,
        Particle.TargetTransportRoutedAgentCount,
        Particle.TargetTransportUnroutedAgentCount,
        Particle.TargetGuidanceUnroutedStepCount,
        Particle.TargetGuidanceUnroutedAgentSampleCount,
        Particle.TargetGuidanceUnroutedAgentMax,
        Particle.TargetGuidanceFirstFailureStep,
        Particle.TargetGuidanceFirstFailureAgentId,
        Particle.TargetTransportInvalidStepCount,
        Particle.TargetTransportValidationFailureCount,
        Particle.TargetTransportTotalPhysicalCost,
        Particle.TargetTransportChangedQuotaUnitCount,
        Particle.TargetTransportPlanEpoch,
        Particle.TargetTransportPlanRebuildCount,
        Particle.TargetTransportLifetimeRebuildCount,
        Particle.TargetTransportTargetRebuildCount,
        Particle.TargetTransportEnvironmentRebuildCount,
        Particle.TargetTransportMembershipRebuildCount,
        Particle.TargetTransportDemandSatisfiedRebuildCount,
        Particle.TargetTransportPathInvalidRebuildCount,
        Particle.TargetTransportSolverMsP95,
        Particle.TargetTransportTopologyHash,
        Particle.TargetTransportDemandHash,
        Particle.TargetTransportPlanHash,
        Particle.TargetTransportGuidanceHash,
        Particle.TargetTransportValidationHash);
      if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
      {
        FString Profiles;
        for (int32 Index = 0; Index < Particle.CapabilityProfiles.Num(); ++Index)
        {
          const FCrowdDemoCapabilityProfileMetrics& Profile = Particle.CapabilityProfiles[Index];
          if (Index > 0) Profiles += TEXT(";");
          Profiles += FString::Printf(
            TEXT("%u:%d:%d:%d:%d:%d:%d:%d:%d:%.1f:%.1f:%.1f:%d:%d:%d:%u:%u:%u:%u:%u"),
            Profile.CapabilityProfileKey, Profile.DemandRegionPhaseOffset, Profile.AgentCount,
            Profile.FeasibleRegionCount, Profile.FeasibleRegionCoverageCount,
            Profile.InsideBandCount, Profile.DistanceBandInsideCount,
            Profile.BelowBandCount, Profile.AboveBandCount,
            Profile.OutsideBandErrorCmMax,
            Profile.OutsideBandProgressCmpsMin,
            Profile.OutsideBandProgressCmpsMax,
            Profile.RoutedAgentCount,
            Profile.UnroutedAgentCount, Profile.MaximumRegionPopulation,
            Profile.TopologyHash, Profile.DemandHash, Profile.TransportHash,
            Profile.GuidanceHash, Profile.ValidationHash);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoT6TargetCheckpoint role=server round_id=%d testcase=%d valid=%d capability_profiles=%d membership_hash=%u profiles=[%s] cross_profile_hard=%d cross_profile_swept=%d source=MassPipeline"),
          RoundResultPacket.RoundId,
          static_cast<int32>(Pipeline->GetRules().SoftPressureTestCase),
          Particle.bCapabilityProfilesValid, Particle.CapabilityProfileCount,
          Particle.CapabilityMembershipHash, *Profiles,
          Particle.CrossProfileHardViolationCount,
          Particle.CrossProfileSweptViolationCount);
      }
      if (Pipeline->IsTargetRegionPlanLifecycleDiagnosticEnabled())
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetRegionPlanLifecycle role=server round_id=%d valid=%d samples=%d hash=%u rebuilds=%d reasons=%d,%d,%d,%d,%d,%d,%d graph_changes=%d,%d,%d premature=%d claims=%d,%d,%d,%d,%d,%d,%d execution_invalid=%d,%d,%d,%d,%d age=%d,%d,%d fixture=%d,%d,%u,%d,%d,%d,%u source=MassPipeline"),
          RoundResultPacket.RoundId, Particle.bTargetPlanLifecycleDiagnosticValid,
          Particle.TargetPlanLifecycleSampleBoundaryCount,
          Particle.TargetPlanLifecycleHash, Particle.TargetTransportPlanRebuildCount,
          Particle.TargetTransportLifetimeRebuildCount,
          Particle.TargetTransportTargetRebuildCount,
          Particle.TargetTransportEnvironmentRebuildCount,
          Particle.TargetTransportMembershipRebuildCount,
          Particle.TargetTransportDemandSatisfiedRebuildCount,
          Particle.TargetTransportPathInvalidRebuildCount,
          Particle.TargetPlanLifecycleInitialInvalidRebuildCount,
          Particle.TargetPlanLifecycleCostOnlyGraphChangeCount,
          Particle.TargetPlanLifecycleCellFeasibilityChangeCount,
          Particle.TargetPlanLifecycleEdgeSetChangeCount,
          Particle.TargetPlanLifecyclePrematureRebuildCount,
          Particle.TargetPlanLifecycleActiveClaimCount,
          Particle.TargetPlanLifecycleGeometryEligibleClaimCount,
          Particle.TargetPlanLifecycleSupplyEligibleClaimCount,
          Particle.TargetPlanLifecycleNewPlanEligibleClaimCount,
          Particle.TargetPlanLifecycleMigratedClaimCount,
          Particle.TargetPlanLifecycleCompletedAtReplacementClaimCount,
          Particle.TargetPlanLifecycleDroppedStillFeasibleClaimCount,
          Particle.TargetPlanLifecycleStateMismatchCount,
          Particle.TargetPlanLifecycleClaimOffEdgeCount,
          Particle.TargetPlanLifecycleQuotaExceededCount,
          Particle.TargetPlanLifecycleSupplyWithoutOutgoingQuotaCount,
          Particle.TargetPlanLifecycleOtherInvalidCount,
          Particle.TargetPlanLifecycleAgeP50, Particle.TargetPlanLifecycleAgeP95,
          Particle.TargetPlanLifecycleAgeMax,
          Particle.bTargetPlanLifecycleFixtureValid,
          Particle.TargetPlanLifecycleFixtureStep,
          Particle.TargetPlanLifecycleFixtureCohortKey,
          Particle.TargetPlanLifecycleFixtureReason,
          Particle.TargetPlanLifecycleFixtureSelectionKind,
          Particle.TargetPlanLifecycleFixtureRegionKey,
          Particle.TargetPlanLifecycleFixtureHash);
        bool bCoverageIncomplete = false;
        if (Particle.bCapabilityProfilesValid != 0
          && !Particle.CapabilityProfiles.IsEmpty())
        {
          for (const FCrowdDemoCapabilityProfileMetrics& Profile :
            Particle.CapabilityProfiles)
          {
            bCoverageIncomplete |= Profile.InsideBandCount < Profile.AgentCount
              || Profile.FeasibleRegionCoverageCount
                < FMath::Min(Profile.AgentCount, Profile.FeasibleRegionCount);
          }
        }
        else
        {
          bCoverageIncomplete =
            Particle.TargetTransportFeasibleRegionCoverageCount
              < Particle.TargetTransportFeasibleRegionCount;
        }
        FString OutputPath;
        const bool bHasOutputPath = FParse::Value(FCommandLine::Get(),
          TEXT("CrowdDemoTargetRegionPlanLifecycleDiagnosticOutput="), OutputPath);
        bool bWritten = false;
        const auto& Fixture = Pipeline->GetTargetRegionPlanLifecycleFixture();
        if (Fixture.bValid && bHasOutputPath && !OutputPath.IsEmpty())
        {
          IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
          PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutputPath));
          bWritten = FFileHelper::SaveStringToFile(
            SerializeTargetRegionPlanLifecycleFixture(Fixture), *OutputPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetRegionPlanLifecycleFile role=server round_id=%d coverage_incomplete=%d coverage_mode=%s fixture_valid=%d output_path=%d written=%d hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, bCoverageIncomplete ? 1 : 0,
          Particle.bCapabilityProfilesValid != 0 ? TEXT("capability_profiles") : TEXT("aggregate"),
          Fixture.bValid ? 1 : 0, bHasOutputPath ? 1 : 0, bWritten ? 1 : 0,
          Fixture.StableHash);
        if (!Particle.bTargetPlanLifecycleDiagnosticValid
          || Particle.TargetPlanLifecycleSampleBoundaryCount <= 0
          || (bCoverageIncomplete && (!Fixture.bValid || !bHasOutputPath || !bWritten)))
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoTargetRegionPlanLifecycleInvalid round_id=%d valid=%d samples=%d coverage_incomplete=%d fixture_valid=%d written=%d"),
            RoundResultPacket.RoundId, Particle.bTargetPlanLifecycleDiagnosticValid,
            Particle.TargetPlanLifecycleSampleBoundaryCount,
            bCoverageIncomplete ? 1 : 0, Fixture.bValid ? 1 : 0,
            bWritten ? 1 : 0);
        }
      }
    }
    const FCrowdDemoSharedFlowMetrics& FlowV2 = RoundResultPacket.SharedFlowMetrics;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoFlowV2Metrics role=server round_id=%d agent_state_hash=%u anchors=%d connections=%d safe_intervals=%d internal_edges=%d directed_edges=%d center_invalid_connected=%d source_attachment_success=%d goal_attachments=%d navigation_unreachable_samples=%d navigation_v2_hash=%u source=MassPipeline"),
      RoundResultPacket.RoundId, FlowV2.AgentStateHash,
      FlowV2.NavigationCenterAnchorCount,
      FlowV2.NavigationConnectionPointCount, FlowV2.NavigationSafeIntervalCount,
      FlowV2.NavigationInternalEdgeCount, FlowV2.NavigationDirectedEdgeCount,
      FlowV2.CenterInvalidButConnectedCellCount,
      FlowV2.SourceAttachmentSuccessCount, FlowV2.GoalAttachmentCount,
      FlowV2.NavigationUnreachableSampleCount, FlowV2.NavigationV2Hash);
    const FCrowdDemoParticleFailureFixture& FailureFixture =
      Pipeline->GetParticleFailureFixture();
    FString OutputPath;
    const bool bHasOutputPath = FParse::Value(FCommandLine::Get(),
      TEXT("CrowdDemoParticleFixtureOutput="), OutputPath);
    bool bWritten = false;
    if (FailureFixture.bValid && bHasOutputPath && !OutputPath.IsEmpty())
    {
      IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
      PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutputPath));
      bWritten = FFileHelper::SaveStringToFile(
        SerializeParticleFailureFixture(FailureFixture), *OutputPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoParticleFailureFixture role=server round_id=%d valid=%d step=%d pair=%d,%d output_path=%d written=%d hash=%u source=MassPipeline"),
      RoundResultPacket.RoundId, FailureFixture.bValid ? 1 : 0,
      FailureFixture.FixedStepIndex, FailureFixture.MinAgentId,
      FailureFixture.MaxAgentId, bHasOutputPath ? 1 : 0, bWritten ? 1 : 0,
      FailureFixture.FixtureHash);
    if (FailureFixture.bValid && bHasOutputPath && !bWritten)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoParticleFailureFixture role=server round_id=%d write_failed=1 hash=%u"),
        RoundResultPacket.RoundId, FailureFixture.FixtureHash);
    }
  }
  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && RoundResultPacket.ParticleMetrics.ParticleInvalidStepCount > 0)
  {
    Pipeline->StopAfterParticleConstraintFailure();
  }
}

void ACrowdDemoRoundSimCoordinator::PublishServerCorrectionFrame()
{
  UWorld* World = GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  FCrowdDemoCorrectionFrame FullFrame;
  if (!World || !Pipeline || !Pipeline->DequeueOutgoingCorrectionFrame(FullFrame))
  {
    return;
  }

  TArray<FCrowdDemoCorrectionFrameChunk> MetricChunks;
  FCrowdDemoRoundCheckpointTransport::BuildChunks(
    FullFrame,
    CrowdDemoCorrectionFrameChunkSize,
    CorrectionFrameHeader,
    MetricChunks);
  const int32 ChunkSize = CorrectionFrameHeader.ChunkSize;
  const int32 ChunkCount = CorrectionFrameHeader.ChunkCount;

  const int32 OldestKeptRevision = FullFrame.CorrectionRevision - CrowdDemoCorrectionFrameHistoryRevisions + 1;
  DroppedCorrectionRevisions.Remove(OldestKeptRevision);

  LastCorrectionFrameWorldSeconds = World->GetTimeSeconds();
  if (LastCorrectionFramePublishWorldSeconds > -999.0)
  {
    CorrectionFramePublishIntervalMsSamples.Add(static_cast<float>((World->GetTimeSeconds() - LastCorrectionFramePublishWorldSeconds) * 1000.0));
  }
  LastCorrectionFramePublishWorldSeconds = World->GetTimeSeconds();
  ++CorrectionFramePublishedCount;
  LastAppliedCorrectionRevision = FullFrame.CorrectionRevision;
  LastServerCorrectionChunkCount = ChunkCount;
  LastServerCorrectionChunkSize = ChunkSize;
  PublishProductCorrectionFrame(FullFrame);
  RefreshLastCorrectionCounters();

  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoCorrectionFrame role=server revision=%d round_id=%d round_revision=%d publish_count=%d chunks=%d chunk_size=%d agents=%d server_time=%.3f plan_phase=%.3f source_checkpoint_revision=%d publish_interval_ms_p95=%.3f source=RoundSim"),
    FullFrame.CorrectionRevision,
    FullFrame.RoundId,
    FullFrame.RoundRevision,
    CorrectionFramePublishedCount,
    ChunkCount,
    ChunkSize,
    FullFrame.AgentCount,
    FullFrame.ServerTimeSeconds,
    FullFrame.CrowdState.PlanPhase,
    FullFrame.SourceCheckpointRevision,
    LastCorrectionFrameMetrics.CorrectionIntervalMsP95);
}

void ACrowdDemoRoundSimCoordinator::RefreshProductReplicationChannels()
{
  UWorld* World = GetWorld();
  if (!HasAuthority() || !World) return;
  for (FConstPlayerControllerIterator It =
      World->GetPlayerControllerIterator(); It; ++It)
  {
    APlayerController* Controller = It->Get();
    if (!Controller || ProductReplicationChannels.Contains(Controller))
      continue;
    AMassCrowdReplicationActor* Channel =
      AMassCrowdReplicationActor::SpawnForController(*Controller);
    if (!Channel) continue;
    ProductReplicationChannels.Add(Controller, Channel);
    NextProductReliableSequence.Add(Channel, 1);
    if (BootstrapSnapshotMetadata.bValid != 0
      && !PublishProductBaseline(*Channel))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=late_join_baseline"));
    }
  }
}

bool ACrowdDemoRoundSimCoordinator::PublishProductBaseline(
  AMassCrowdReplicationActor& Channel)
{
  uint64* NextSequence = NextProductReliableSequence.Find(&Channel);
  return HasAuthority() && NextSequence && *NextSequence > 0
    && BootstrapSnapshotMetadata.bValid != 0
    && !CurrentProductBootstrapChunks.IsEmpty()
    && Channel.PublishBaseline(
      BootstrapSnapshotMetadata.SnapshotHeader,
      CurrentProductBootstrapChunks,
      *NextSequence);
}

bool ACrowdDemoRoundSimCoordinator::PublishProductReliable(
  AMassCrowdReplicationActor& Channel,
  const ECrowdReliableStateKind Kind,
  const FCrowdStableEntityRef& EntityRef,
  const uint32 RevisionValue,
  const TConstArrayView<uint8> Payload)
{
  uint64* NextSequence = NextProductReliableSequence.Find(&Channel);
  if (!NextSequence || *NextSequence == 0 || !EntityRef.IsValid())
    return false;
  FCrowdReliableStateRecord Record;
  Record.Sequence = *NextSequence;
  Record.Kind = Kind;
  Record.EntityRef = EntityRef;
  Record.Revision = RevisionValue;
  Record.Payload.Append(Payload.GetData(), Payload.Num());
  Record.StableHash =
    FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
  if (!Channel.PublishReliable(Record)) return false;
  ++(*NextSequence);
  return true;
}

void ACrowdDemoRoundSimCoordinator::PublishProductCorrectionFrame(
  const FCrowdDemoCorrectionFrame& Frame)
{
  TArray<uint8> HeaderPayload;
  if (!EncodeProductPayload(
      CrowdDemoCorrectionHeaderMagic,
      CorrectionFrameHeader,
      HeaderPayload))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=encode_correction_header revision=%d"),
      Frame.CorrectionRevision);
    return;
  }
  for (TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
    : ProductReplicationChannels)
  {
    AMassCrowdReplicationActor* Channel = Pair.Value.Get();
    if (!Channel) continue;
    TArray<FCrowdMovementCorrectionRecord> Corrections;
    Corrections.Reserve(Frame.AgentStates.Num());
    if (!Channel->IsServerAwaitingBaselineAck())
      for (const FCrowdDemoRoundAgentState& Agent : Frame.AgentStates)
      {
        FCrowdMovementCorrectionRecord Correction;
        Correction.EntityRef = {
          1,
          static_cast<uint64>(FMath::Max(0, Agent.AgentId)) + 1ull,
          static_cast<uint32>(FMath::Max(1, Agent.LifecycleSerial))};
        Correction.Sequence =
          static_cast<uint64>(Frame.CorrectionRevision);
        Correction.FixedStepIndex = Frame.CorrectionRevision;
        Correction.Position = Agent.Location;
        Correction.Velocity = Agent.Velocity;
        Correction.YawDegrees = Agent.YawDegrees;
        Correction.StableHash =
          FCrowdReplicationTransport::CalculateMovementCorrectionHash(
            Correction);
        Corrections.Add(Correction);
      }
    if (!Corrections.IsEmpty()
      && !Channel->PublishMovementCorrections(Corrections))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=publish_correction_batch revision=%d agents=%d"),
        Frame.CorrectionRevision, Corrections.Num());
      continue;
    }
    if (!PublishProductReliable(
      *Channel,
      ECrowdReliableStateKind::Membership,
      {8, static_cast<uint64>(Frame.CorrectionRevision), 1},
      static_cast<uint32>(Frame.CorrectionRevision),
      HeaderPayload))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=publish_correction_header revision=%d"),
        Frame.CorrectionRevision);
      continue;
    }
    for (const FCrowdDemoRoundAgentState& Agent : Frame.AgentStates)
    {
      TArray<uint8> AgentPayload;
      const FCrowdStableEntityRef EntityRef{
        1,
        static_cast<uint64>(FMath::Max(0, Agent.AgentId)) + 1ull,
        static_cast<uint32>(FMath::Max(1, Agent.LifecycleSerial))};
      if (!EncodeProductPayload(
          CrowdDemoCorrectionAgentMagic, Agent, AgentPayload)
        || !PublishProductReliable(
          *Channel,
          ECrowdReliableStateKind::Behavior,
          EntityRef,
          static_cast<uint32>(Frame.CorrectionRevision),
          AgentPayload))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=publish_agent revision=%d agent=%d"),
          Frame.CorrectionRevision, Agent.AgentId);
        break;
      }
    }
  }
}

void ACrowdDemoRoundSimCoordinator::PublishProductRoundResultHeader(
  const FCrowdDemoRoundResultHeader& Header)
{
  TArray<uint8> Payload;
  if (!EncodeProductRoundResultHeader(Header, Payload))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=encode_result_header round_id=%d"),
      Header.RoundId);
    return;
  }
  for (TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
    : ProductReplicationChannels)
  {
    AMassCrowdReplicationActor* Channel = Pair.Value.Get();
    if (!Channel
      || !PublishProductReliable(
        *Channel,
        ECrowdReliableStateKind::HostEvent,
        {9, static_cast<uint64>(FMath::Max(1, Header.RoundId)), 1},
        static_cast<uint32>(
          FMath::Max(1, Header.StateFrameRevision)),
        Payload))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=publish_result_header round_id=%d"),
        Header.RoundId);
    }
  }
}

void ACrowdDemoRoundSimCoordinator::PublishProductProjectileEvents(
  const TConstArrayView<FCrowdDemoProjectileVisualEvent> Events)
{
  for (TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
    : ProductReplicationChannels)
  {
    AMassCrowdReplicationActor* Channel = Pair.Value.Get();
    if (!Channel) continue;
    for (const FCrowdDemoProjectileVisualEvent& Event : Events)
    {
      TArray<uint8> Payload;
      if (!EncodeProductPayload(
          CrowdDemoProjectileEventMagic, Event, Payload)
        || !PublishProductReliable(
          *Channel,
          ECrowdReliableStateKind::PresentationEvent,
          {7, FMath::Max<uint64>(1ull, Event.ProjectileId), 1},
          static_cast<uint32>(FMath::Max(0, Event.FixedStepIndex) + 1),
          Payload))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoRoundProductChannel role=server stage=publish_projectile id=%llu"),
          Event.ProjectileId);
        break;
      }
    }
  }
}

void ACrowdDemoRoundSimCoordinator::
  ConsumeProductReplicationChannels()
{
  UWorld* World = GetWorld();
  if (!World || HasAuthority()) return;
  for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
  {
    AMassCrowdReplicationActor* Channel = *It;
    if (!Channel || !Channel->IsReady()) continue;
    const uint32 BaselineRevision =
      Channel->GetCompletedBaselineRevision();
    if (BaselineRevision > LastConsumedProductBaselineRevision)
    {
      TArray<FCrowdDemoRoundAgentState> Agents;
      if (!FCrowdDemoRelevantSnapshotAdapter::DecodeAgents(
          Channel->GetCompletedBaselineEntities(), Agents))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoRoundProductChannel role=client stage=decode_baseline revision=%u"),
          BaselineRevision);
        continue;
      }
      RoundBootstrapPacket = {};
      RoundBootstrapPacket.bValid = 1;
      RoundBootstrapPacket.Revision =
        static_cast<int32>(BaselineRevision);
      const AGameStateBase* GameState = World->GetGameState();
      RoundBootstrapPacket.ServerTimeSeconds =
        CurrentRoundPlan.bValid != 0
          ? CurrentRoundPlan.StartServerTimeSeconds
          : (GameState
            ? GameState->GetServerWorldTimeSeconds()
            : World->GetTimeSeconds());
      RoundBootstrapPacket.Agents = MoveTemp(Agents);
      LastConsumedProductBaselineRevision = BaselineRevision;
      if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
        World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
      {
        Pipeline->QueueBootstrap(RoundBootstrapPacket);
        if (CurrentRoundPlan.bValid != 0)
          Pipeline->QueueRoundPlan(CurrentRoundPlan);
      }
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoBootstrapSnapshot role=client stage=complete revision=%u agents=%d source=MassCrowdReplicationChannel"),
        BaselineRevision, RoundBootstrapPacket.Agents.Num());
    }

    TArray<FCrowdReplicationApplyFrame> ApplyFrames;
    if (!Channel->DrainClientApplyFrames(ApplyFrames))
      continue;
    for (const FCrowdReplicationApplyFrame& ApplyFrame : ApplyFrames)
    {
      if (ApplyFrame.Kind
        == ECrowdReplicationApplyFrameKind::ReliableState)
      {
        for (const FCrowdReliableStateRecord& Record
          : ApplyFrame.ReliableRecords)
          ConsumeProductReliableRecord(*Channel, Record);
      }
      else if (ApplyFrame.Kind
        == ECrowdReplicationApplyFrameKind::MovementCorrection)
      {
        LatestProductCorrectionCount =
          ApplyFrame.Corrections.Num();
      }
    }
  }
}

void ACrowdDemoRoundSimCoordinator::ConsumeProductReliableRecord(
  AMassCrowdReplicationActor& Channel,
  const FCrowdReliableStateRecord& Record)
{
  FCrowdDemoCorrectionFrameHeader Header;
  if (DecodeProductPayload(
      Record.Payload,
      CrowdDemoCorrectionHeaderMagic,
      Header))
  {
    ProductCorrectionHeaders.Add(
      Header.CorrectionRevision, Header);
    ProductCorrectionAgents.FindOrAdd(
      Header.CorrectionRevision).Reset();
    CacheClientCorrectionHeader(Header);
    return;
  }

  FCrowdDemoRoundAgentState Agent;
  if (DecodeProductPayload(
      Record.Payload,
      CrowdDemoCorrectionAgentMagic,
      Agent))
  {
    TArray<FCrowdDemoRoundAgentState>& Agents =
      ProductCorrectionAgents.FindOrAdd(
        static_cast<int32>(Record.Revision));
    if (!Agents.ContainsByPredicate(
      [&](const FCrowdDemoRoundAgentState& Existing)
      {
        return Existing.AgentId == Agent.AgentId;
      }))
    {
      Agents.Add(Agent);
    }
    TryFinalizeProductCorrection(
      static_cast<int32>(Record.Revision));
    return;
  }

  FCrowdDemoProjectileVisualEvent ProjectileEvent;
  if (DecodeProductPayload(
      Record.Payload,
      CrowdDemoProjectileEventMagic,
      ProjectileEvent))
  {
    UWorld* World = GetWorld();
    if (World && World->GetNetMode() != NM_DedicatedServer)
      if (ACrowdDemoReplicator* VisualHost =
        FindProjectileVisualHost(*World))
      {
        const FCrowdDemoProjectileVisualEvent Events[] = {
          ProjectileEvent};
        VisualHost->ApplyProjectileVisualEvents(Events);
      }
    return;
  }

  FCrowdDemoRoundResultHeader ResultHeader;
  if (DecodeProductRoundResultHeader(
      Record.Payload, ResultHeader))
  {
    ConsumeProductRoundResultHeader(ResultHeader);
    return;
  }

  UE_LOG(LogTemp, Error,
    TEXT("VIOLATION CrowdDemoRoundProductChannel role=client stage=unknown_payload sequence=%llu kind=%d"),
    Record.Sequence, static_cast<int32>(Record.Kind));
}

void ACrowdDemoRoundSimCoordinator::TryFinalizeProductCorrection(
  const int32 CorrectionRevisionValue)
{
  FCrowdDemoCorrectionFrameHeader* Header =
    ProductCorrectionHeaders.Find(CorrectionRevisionValue);
  TArray<FCrowdDemoRoundAgentState>* Agents =
    ProductCorrectionAgents.Find(CorrectionRevisionValue);
  if (!Header || !Agents || Agents->Num() != Header->AgentCount)
    return;
  Agents->Sort([](
    const FCrowdDemoRoundAgentState& A,
    const FCrowdDemoRoundAgentState& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 1; Index < Agents->Num(); ++Index)
    if ((*Agents)[Index - 1].AgentId == (*Agents)[Index].AgentId)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRoundProductChannel role=client stage=duplicate_agent revision=%d"),
        CorrectionRevisionValue);
      return;
    }

  FCrowdDemoCorrectionFrame Frame;
  Frame.bValid = Header->bValid;
  Frame.FrameKind = Header->FrameKind;
  Frame.CorrectionRevision = Header->CorrectionRevision;
  Frame.RoundId = Header->RoundId;
  Frame.RoundRevision = Header->RoundRevision;
  Frame.SourceCheckpointRevision =
    Header->SourceCheckpointRevision;
  Frame.ServerTimeSeconds = Header->ServerTimeSeconds;
  Frame.AgentCount = Header->AgentCount;
  Frame.CrowdState = Header->CrowdState;
  Frame.AgentStates = *Agents;
  FCrowdDemoCorrectionFrameHeader BuiltHeader;
  TArray<FCrowdDemoCorrectionFrameChunk> Chunks;
  FCrowdDemoRoundCheckpointTransport::BuildChunks(
    Frame,
    CrowdDemoCorrectionFrameChunkSize,
    BuiltHeader,
    Chunks);
  for (const FCrowdDemoCorrectionFrameChunk& Chunk : Chunks)
    CacheClientCorrectionChunk(Chunk);
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoRoundProductChannel role=client stage=correction_complete revision=%d agents=%d chunks=%d latest_corrections=%d source=MassCrowdReplicationChannel"),
    CorrectionRevisionValue,
    Agents->Num(),
    Chunks.Num(),
    LatestProductCorrectionCount);
  ProductCorrectionHeaders.Remove(CorrectionRevisionValue);
  ProductCorrectionAgents.Remove(CorrectionRevisionValue);
}

FCrowdDemoRoundPlanPacket ACrowdDemoRoundSimCoordinator::BuildRoundPlanPacket(
  const UCrowdDemoMassSubsystem& MassSubsystem,
  const int32 RoundId,
  const int32 PlanRevision,
  const int32 PreviousCheckpointRevision,
  const float StartServerTimeSeconds,
  const FVector& StartLocation,
  const int32 AgentCount) const
{
  FCrowdDemoRoundPlanPacket Packet;
  Packet.bValid = 1;
  Packet.RoundId = RoundId;
  Packet.Revision = PlanRevision;
  Packet.PreviousCheckpointRevision = PreviousCheckpointRevision;
  Packet.StartServerTimeSeconds = StartServerTimeSeconds;
  Packet.Rules = BuildRoundRules(MassSubsystem, RoundId, StartServerTimeSeconds, StartLocation, AgentCount);
  const CrowdDemoScenarioRegistry::FCrowdDemoRoundTiming Timing =
    CrowdDemoScenarioRegistry::ResolveRoundTiming(
      Packet.Rules.Scenario, Packet.Rules.SoftPressureTestCase);
  Packet.NominalDurationSeconds = Timing.NominalDurationSeconds;
  Packet.CompletionGraceSeconds = Timing.CompletionGraceSeconds;
  Packet.DurationSeconds = Timing.GetTotalDurationSeconds();
  return Packet;
}

FCrowdDemoRoundRules ACrowdDemoRoundSimCoordinator::BuildRoundRules(
  const UCrowdDemoMassSubsystem& MassSubsystem,
  const int32 RoundId,
  const float StartServerTimeSeconds,
  const FVector& StartLocation,
  const int32 AgentCount) const
{
  FCrowdDemoRoundRules CompactRules;
  CompactRules.Scenario = MassSubsystem.GetScenario();
  CompactRules.RoundStartPolicy =
    ECrowdDemoRoundStartPolicy::ResetToStableInitialState;
  CompactRules.RandomSeed = 1337;
  CompactRules.FixedStepSeconds = 1.0f / 30.0f;
  CompactRules.FlowFieldConfig = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  const bool bSf2 = CompactRules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure;
  const int32 SafeAgentCount = FMath::Max(1, AgentCount);
  CompactRules.FormationColumns = bSf2
    ? (SafeAgentCount <= 20 ? 10 : (SafeAgentCount <= 100 ? 25 : 50))
    : (SafeAgentCount > 1 ? FMath::CeilToInt(FMath::Sqrt(static_cast<float>(SafeAgentCount))) : 1);
  CompactRules.FormationSpacingCm = bSf2 ? 128.0f
    : (SafeAgentCount > 1 ? 18.0f : 0.0f);
  CompactRules.MaxSpeedCmPerSecond = 800.0f;
  CompactRules.SpawnOrigin = FVector(0.0f, bSf2 ? -2850.0f : -2900.0f, 60.0f);
  CompactRules.bEnableObstacle = 1;
  CompactRules.ParticleConstraintIterations = 8;
  CompactRules.ParticleSafetyIterations = 8;
  CompactRules.ParticleSoftResponsePerSecond = 8.0f;
  CompactRules.ParticleSoftMaxCorrectionCm = 8.0f;
  CompactRules.ParticleHardMaxCorrectionCm = 24.0f;
  CompactRules.ParticlePositionQuantumCm = 1.0f;
  CompactRules.ParticleVelocityQuantumCmps = 1.0f;
  if (bSf2)
  {
    CompactRules.SoftPressureTestCase = MassSubsystem.GetSoftPressureTestCase();
    if (CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::OpenSpawnRelaxation)
    {
      CompactRules.FlowFieldConfig = FCrowdDemoOpenSpawnRelaxationKernel::MakeOpenFlowConfig();
      CompactRules.SpawnOrigin = FVector::ZeroVector;
      CompactRules.bEnableObstacle = 0;
    }
    else if (CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::OpenCohortMovement)
    {
      CompactRules.FlowFieldConfig = FCrowdDemoOpenCohortMovementKernel::MakeOpenFlowConfig();
      CompactRules.SpawnOrigin = FVector(0.0f, -2850.0f, 60.0f);
      CompactRules.bEnableObstacle = 0;
    }
    else if (CompactRules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::BidirectionalSwap)
    {
      CompactRules.FlowFieldConfig =
        FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(0);
      CompactRules.SpawnOrigin = FVector::ZeroVector;
      CompactRules.FormationColumns = 10;
      CompactRules.FormationSpacingCm = 128.0f;
      CompactRules.bEnableObstacle = 0;
    }
    else if (CompactRules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::ValidCorridorTransit
      || CompactRules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::HeterogeneousTransit)
    {
      CompactRules.FlowFieldConfig =
        FCrowdDemoValidCorridorTransitKernel::MakeFlowConfig();
      CompactRules.SpawnOrigin = FVector(0.0f, -2850.0f, 60.0f);
      CompactRules.FormationColumns = 10;
      CompactRules.FormationSpacingCm = CompactRules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::HeterogeneousTransit ? 140.0f : 128.0f;
      CompactRules.bEnableObstacle = 1;
    }
    else if (CompactRules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat)
    {
      CompactRules.FlowFieldConfig = FCrowdDemoOpenCohortMovementKernel::MakeOpenFlowConfig();
      CompactRules.SpawnOrigin = FVector(0.0f, 0.0f, 60.0f);
      CompactRules.FormationColumns = 10;
      CompactRules.FormationSpacingCm = 128.0f;
      CompactRules.bEnableObstacle = 0;
      CompactRules.RangedCombatSettings.bEnabled = 1;
      CompactRules.RangedCombatSettings.ShooterCount = 10;
      CompactRules.RangedCombatSettings.WindupFixedSteps = 15;
      CompactRules.RangedCombatSettings.RecoveryFixedSteps = 12;
      CompactRules.RangedCombatSettings.CooldownFixedSteps = 30;
      CompactRules.RangedCombatSettings.ProjectileSpeedCmps = 1800.0f;
      CompactRules.RangedCombatSettings.ProjectileRadiusCm = 12.0f;
      CompactRules.RangedCombatSettings.ProjectileLifetimeFixedSteps = 60;
      CompactRules.RangedCombatSettings.MuzzleForwardOffsetCm = 70.0f;
      CompactRules.RangedCombatSettings.Damage = 20.0f;
      CompactRules.RangedCombatSettings.HorizontalImpulseCmps = 0.0f;
      CompactRules.RangedCombatSettings.VerticalImpulseCmps = 0.0f;
    }
    const bool bHeterogeneousProfiles =
      CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTransit
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetStatic
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving;
    CompactRules.bEnableHeterogeneousProfiles = bHeterogeneousProfiles ? 1 : 0;
    if (bHeterogeneousProfiles)
    {
      // P0 LargeHeavy/LargeHeavy requires 130cm. Start all T6 scenes from a
      // strictly non-overlapping layout rather than asking the first solver
      // boundary to repair invalid input.
      CompactRules.FormationSpacingCm = 140.0f;
      // Shared Flow is a common navigation fact. Use the largest P0 hard wall
      // clearance so every heterogeneous member can consume the same field.
      CompactRules.FlowFieldConfig.AgentInflateCm = 70.0f;
    }
    else
    {
    CompactRules.FlowFieldConfig.AgentInflateCm =
      CompactRules.ParticleProfile.GetNavigationHardClearanceCm();
    }
    CompactRules.FlowFieldConfig.ConnectivityContractVersion = 2;
    if (FCrowdDemoOpenCohortMovementKernel::ShouldEnablePolarHandoff(
          CompactRules.SoftPressureTestCase)
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::PursuitAndSettle
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::PursuitAndSettleMoving
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTransit
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetStatic
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving)
    {
      CompactRules.TargetDistanceBandSettings.bEnabled = 1;
      CompactRules.TargetDistanceBandSettings.DefaultMinimumCombatCenterDistanceCm = 100.0f;
      CompactRules.TargetDistanceBandSettings.DefaultMaximumCombatCenterDistanceCm = 850.0f;
      CompactRules.TargetDistanceBandSettings.InfluenceBlendWidthCm = 300.0f;
      CompactRules.TargetDistanceBandSettings.RadialGainPerSecond = 2.0f;
      CompactRules.TargetDistanceBandSettings.MaxRadialSpeedCmps = 300.0f;
      CompactRules.TargetDistanceBandSettings.AngularSectorCount = 16;
      CompactRules.TargetDistanceBandSettings.RadialBandWidthCm = 100.0f;
      CompactRules.TargetRegionTransportSettings.bEnabled = 1;
      CompactRules.TargetRegionTransportSettings.RadialBandWidthCm = 100.0f;
      CompactRules.TargetRegionTransportSettings.TransportSpeedCmps = 300.0f;
      CompactRules.TargetRegionTransportSettings.DemandRegionCount = 16;
      CompactRules.TargetRegionTransportSettings.PlanLifetimeSteps = 15;
      CompactRules.TargetMotion.TargetId = 1;
      CompactRules.TargetMotion.TargetRevision = 1;
      CompactRules.TargetMotion.InitialLocation = CompactRules.FlowFieldConfig.GoalLocation;
      CompactRules.TargetMotion.LinearVelocity =
        (CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::PursuitAndSettleMoving
          || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving)
        ? FVector(-80.0f, 0.0f, 0.0f)
        : FVector::ZeroVector;
      if (CompactRules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving)
      {
        const float TargetHardClearanceCm =
          CompactRules.TargetDistanceBandSettings.TargetPhysicalRadiusCm
          + CompactRules.TargetDistanceBandSettings.TargetHardSafetyGapCm;
        const float MotionSafetyInsetCm = TargetHardClearanceCm + 10.0f;
        CompactRules.TargetMotion.bReflectAtMotionBounds = 1;
        CompactRules.TargetMotion.MotionBoundsMin = FVector(
          FVector(CompactRules.FlowFieldConfig.BoundsMin).X + MotionSafetyInsetCm,
          FVector(CompactRules.FlowFieldConfig.BoundsMin).Y + MotionSafetyInsetCm,
          0.0f);
        CompactRules.TargetMotion.MotionBoundsMax = FVector(
          FVector(CompactRules.FlowFieldConfig.BoundsMax).X - MotionSafetyInsetCm,
          FVector(CompactRules.FlowFieldConfig.BoundsMax).Y - MotionSafetyInsetCm,
          0.0f);
      }
      CompactRules.TargetMotion.InitialYawDegrees = 0.0f;
      CompactRules.TargetMotion.YawRateDegreesPerSecond = 0.0f;
    }
  }
  return CompactRules;

}

void ACrowdDemoRoundSimCoordinator::QueueClientRoundPlan(const FCrowdDemoRoundPlanPacket& Plan)
{
  if (HasAuthority() || Plan.bValid == 0 || Plan.Revision <= LastAppliedRoundPlanRevision)
  {
    return;
  }

  const bool bFirstSeen = !PendingClientRoundPlans.Contains(Plan.Revision);
  PendingClientRoundPlans.Add(Plan.Revision, Plan);
  if (UWorld* World = GetWorld())
  {
    if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
    {
      Pipeline->QueueBootstrap(RoundBootstrapPacket);
      Pipeline->QueueRoundPlan(Plan);
    }
  }
  if (bFirstSeen)
  {
    RoundPlanRevisionSeen = FMath::Max(RoundPlanRevisionSeen, Plan.Revision);
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoRoundPlan role=client round_id=%d revision=%d previous_checkpoint_revision=%d start_server_time=%.3f duration=%.3f nominal_duration=%.3f completion_grace=%.3f action=queued source=RoundPlan"),
      Plan.RoundId,
      Plan.Revision,
      Plan.PreviousCheckpointRevision,
      Plan.StartServerTimeSeconds,
      Plan.DurationSeconds,
      Plan.NominalDurationSeconds,
      Plan.CompletionGraceSeconds);
  }
  RefreshLastCompareCounters();
}

void ACrowdDemoRoundSimCoordinator::TryActivateClientRoundPlans(const float ClientServerTimeSeconds)
{
  // Round plans are consumed only by UCrowdDemoRoundPlanApplyProcessor at a fixed-step boundary.
}

void ACrowdDemoRoundSimCoordinator::ActivateClientRoundPlan(
  const FCrowdDemoRoundPlanPacket& Plan,
  const float ClientServerTimeSeconds,
  const bool bLateJoinBaseline)
{
  if (UWorld* World = GetWorld())
  {
    if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
    {
      Pipeline->QueueRoundPlan(Plan);
    }
  }
}

void ACrowdDemoRoundSimCoordinator::TryProcessClientResult()
{
  TryProcessClientCorrectionAssemblies();
}

bool ACrowdDemoRoundSimCoordinator::TryBuildClientRoundResult(
  const FCrowdDemoPendingCorrectionAssembly& Assembly,
  FCrowdDemoRoundResultPacket& OutResult)
{
  const FCrowdDemoRoundResultHeader* Header = PendingClientResultHeaders.Find(
    Assembly.Header.CorrectionRevision);
  if (!Header)
  {
    return false;
  }
  if (Header->StateFrameRevision != Assembly.Header.CorrectionRevision
    || Header->RoundId != Assembly.Header.RoundId
    || Header->Revision != Assembly.Header.RoundRevision
    || Header->AgentCount != Assembly.ReceivedAgentCount
    || Header->CheckpointRevision != Assembly.Header.SourceCheckpointRevision)
  {
    ++RoundResultRevisionMismatchCount;
    UE_LOG(
      LogTemp,
      Warning,
      TEXT("CrowdDemoRoundResultTransport role=client stage=revision_mismatch round_id=%d checkpoint_revision=%d state_frame_revision=%d assembly_revision=%d header_agents=%d assembly_agents=%d mismatch_count=%d source=RoundSim"),
      Header->RoundId,
      Header->CheckpointRevision,
      Header->StateFrameRevision,
      Assembly.Header.CorrectionRevision,
      Header->AgentCount,
      Assembly.ReceivedAgentCount,
      RoundResultRevisionMismatchCount);
    return false;
  }

  OutResult.bValid = 1;
  OutResult.RoundId = Header->RoundId;
  OutResult.Revision = Header->Revision;
  OutResult.CheckpointRevision = Header->CheckpointRevision;
  OutResult.StateFrameRevision = Header->StateFrameRevision;
  OutResult.EndServerTimeSeconds = Header->EndServerTimeSeconds;
  OutResult.Agents = Assembly.AgentBuffer;
  OutResult.OverlapPairCount = Header->OverlapPairCount;
  OutResult.InitialOverlapPairCount = Header->InitialOverlapPairCount;
  OutResult.SevereOverlapPairCount = Header->SevereOverlapPairCount;
  OutResult.InitialSevereOverlapPairCount = Header->InitialSevereOverlapPairCount;
  OutResult.ObstaclePenetrationCount = Header->ObstaclePenetrationCount;
  OutResult.ArrivalCount = Header->ArrivalCount;
  OutResult.SharedFlowMetrics = Header->SharedFlowMetrics;
  OutResult.ParticleMetrics = Header->ParticleMetrics;
  OutResult.ProjectileMetrics = Header->ProjectileMetrics;
  return true;
}

void ACrowdDemoRoundSimCoordinator::CacheClientCorrectionHeader(const FCrowdDemoCorrectionFrameHeader& Header)
{
  if (HasAuthority() || Header.bValid == 0)
  {
    return;
  }

  if (DroppedCorrectionRevisions.Contains(Header.CorrectionRevision)
    || Header.CorrectionRevision <= LastAppliedCorrectionRevision)
  {
    return;
  }

  FCrowdDemoPendingCorrectionAssembly* ExistingAssembly = PendingCorrectionAssemblies.Find(Header.CorrectionRevision);
  const bool bFirstHeaderForRevision = !ExistingAssembly || ExistingAssembly->Header.bValid == 0;
  if (bFirstHeaderForRevision)
  {
    ++CorrectionFrameHeaderReceivedCount;
    ++CorrectionFrameReceivedCount;
    CorrectionFrameHeader = Header;
    CorrectionFrameLatestRevisionSeen = FMath::Max(CorrectionFrameLatestRevisionSeen, Header.CorrectionRevision);
    CorrectionExpectedChunkCount = Header.ChunkCount;
    if (LastReceivedCorrectionRevision > 0)
    {
      CorrectionFrameRevisionGapCount += FMath::Max(0, Header.CorrectionRevision - LastReceivedCorrectionRevision - 1);
    }
    LastReceivedCorrectionRevision = Header.CorrectionRevision;
  }

  const int32 OldestKeptRevision = CorrectionFrameLatestRevisionSeen - CrowdDemoCorrectionFrameHistoryRevisions + 1;
  TArray<int32> SupersededRevisions;
  for (const TPair<int32, FCrowdDemoPendingCorrectionAssembly>& Pair : PendingCorrectionAssemblies)
  {
    if (Pair.Key < OldestKeptRevision
      && Pair.Value.Header.FrameKind != ECrowdDemoRoundFrameKind::RoundResultCheckpoint)
    {
      SupersededRevisions.Add(Pair.Key);
    }
  }
  if (!SupersededRevisions.IsEmpty())
  {
    CorrectionAssemblySupersededCount += SupersededRevisions.Num();
    for (const int32 SupersededRevision : SupersededRevisions)
    {
      PendingCorrectionAssemblies.Remove(SupersededRevision);
      DroppedCorrectionRevisions.Add(SupersededRevision);
    }
  }

  FCrowdDemoPendingCorrectionAssembly& Assembly = PendingCorrectionAssemblies.FindOrAdd(Header.CorrectionRevision);
  Assembly.Header = Header;
  if (Assembly.ReceivedChunks.Num() < Header.ChunkCount)
  {
    Assembly.ReceivedChunks.SetNumZeroed(FMath::Max(0, Header.ChunkCount));
  }
  else if (Assembly.ReceivedChunks.Num() > Header.ChunkCount)
  {
    Assembly.ReceivedChunks.SetNum(Header.ChunkCount);
  }
  if (Assembly.AgentBuffer.Num() != Header.AgentCount)
  {
    Assembly.AgentBuffer.SetNum(Header.AgentCount);
  }

  if (UWorld* World = GetWorld())
  {
    const double NowSeconds = World->GetTimeSeconds();
    if (Assembly.FirstReceiveWorldSeconds < 0.0)
    {
      Assembly.FirstReceiveWorldSeconds = NowSeconds;
    }
    Assembly.LastReceiveWorldSeconds = NowSeconds;
  }

  RefreshLastCorrectionCounters();
  if (bFirstHeaderForRevision)
  {
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoCorrectionFrameHeader role=client revision=%d round_id=%d round_revision=%d header_received_count=%d chunks=%d chunk_size=%d agents=%d revision_gap_total=%d source=RoundSim"),
      Header.CorrectionRevision,
      Header.RoundId,
      Header.RoundRevision,
      CorrectionFrameHeaderReceivedCount,
      Header.ChunkCount,
      Header.ChunkSize,
      Header.AgentCount,
      CorrectionFrameRevisionGapCount);
  }
}

void ACrowdDemoRoundSimCoordinator::CacheClientCorrectionChunk(const FCrowdDemoCorrectionFrameChunk& Chunk)
{
  if (HasAuthority())
  {
    return;
  }
  if (Chunk.bValid != 0)
  {
    ++CorrectionChunkReceivedCount;
    if (Chunk.Header.FrameKind == ECrowdDemoRoundFrameKind::RoundResultCheckpoint)
    {
      ++RoundResultCheckpointChunkReceivedCount;
    }
    LatestChunkRevisionSeen = FMath::Max(LatestChunkRevisionSeen, Chunk.CorrectionRevision);
  }
  UWorld* World = GetWorld();
  const double NowSeconds = World ? World->GetTimeSeconds() : 0.0;
  bool bCachedAnyChunk = false;
  if (Chunk.bValid != 0
    && Chunk.CorrectionRevision > LastAppliedCorrectionRevision
    && !DroppedCorrectionRevisions.Contains(Chunk.CorrectionRevision)
    && Chunk.ChunkIndex >= 0
    && Chunk.Agents.Num() == Chunk.AgentCountInChunk)
  {
    if (Chunk.Header.bValid != 0)
    {
      CacheClientCorrectionHeader(Chunk.Header);
    }
    FCrowdDemoPendingCorrectionAssembly& Assembly = PendingCorrectionAssemblies.FindOrAdd(Chunk.CorrectionRevision);
    if (Assembly.ReceivedChunks.Num() <= Chunk.ChunkIndex)
    {
      Assembly.ReceivedChunks.SetNumZeroed(Chunk.ChunkIndex + 1);
    }
    if (Assembly.ReceivedChunks[Chunk.ChunkIndex] != 0)
    {
      return;
    }

    const int32 RequiredAgentCount = Chunk.StartAgentIndex + Chunk.AgentCountInChunk;
    if (Assembly.AgentBuffer.Num() < RequiredAgentCount)
    {
      Assembly.AgentBuffer.SetNum(RequiredAgentCount);
    }
    for (int32 AgentOffset = 0; AgentOffset < Chunk.AgentCountInChunk; ++AgentOffset)
    {
      Assembly.AgentBuffer[Chunk.StartAgentIndex + AgentOffset] = Chunk.Agents[AgentOffset];
    }
    Assembly.ReceivedChunks[Chunk.ChunkIndex] = 1;
    ++Assembly.ReceivedChunkCount;
    Assembly.ReceivedAgentCount += Chunk.AgentCountInChunk;
    if (Assembly.FirstReceiveWorldSeconds < 0.0)
    {
      Assembly.FirstReceiveWorldSeconds = NowSeconds;
    }
    Assembly.LastReceiveWorldSeconds = NowSeconds;
    ++CorrectionFrameChunkReceivedCount;
    ++CorrectionUniqueChunkCount;
    bCachedAnyChunk = true;
  }

  if (bCachedAnyChunk || Chunk.bValid != 0)
  {
    RefreshLastCorrectionCounters();
  }
}

void ACrowdDemoRoundSimCoordinator::TryProcessClientCorrectionAssemblies()
{
  const UWorld* World = GetWorld();
  const UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (HasAuthority() || !Pipeline || !Pipeline->IsActive())
  {
    return;
  }

  int32 HighestCompleteRevision = INDEX_NONE;
  int32 CheckpointCompleteRevision = INDEX_NONE;
  TArray<int32> RevisionsToRemove;
  for (const TPair<int32, FCrowdDemoPendingCorrectionAssembly>& Pair : PendingCorrectionAssemblies)
  {
    const int32 PendingRevision = Pair.Key;
    const FCrowdDemoPendingCorrectionAssembly& Assembly = Pair.Value;
    if (PendingRevision <= LastAppliedCorrectionRevision)
    {
      RevisionsToRemove.Add(PendingRevision);
      continue;
    }

    const FCrowdDemoCorrectionFrameHeader& Header = Assembly.Header;
    bool bComplete = Header.bValid != 0
      && Assembly.ReceivedChunkCount == Header.ChunkCount
      && Assembly.ReceivedAgentCount == Header.AgentCount;
    for (int32 ChunkIndex = 0; bComplete && ChunkIndex < Header.ChunkCount; ++ChunkIndex)
    {
      bComplete = Assembly.ReceivedChunks.IsValidIndex(ChunkIndex)
        && Assembly.ReceivedChunks[ChunkIndex] != 0;
    }
    if (bComplete)
    {
      if (Header.FrameKind == ECrowdDemoRoundFrameKind::RoundResultCheckpoint)
      {
        CheckpointCompleteRevision = CheckpointCompleteRevision == INDEX_NONE
          ? PendingRevision
          : FMath::Min(CheckpointCompleteRevision, PendingRevision);
      }
      else
      {
        HighestCompleteRevision = FMath::Max(HighestCompleteRevision, PendingRevision);
      }
    }
  }

  for (const int32 RevisionToRemove : RevisionsToRemove)
  {
    PendingCorrectionAssemblies.Remove(RevisionToRemove);
    DroppedCorrectionRevisions.Add(RevisionToRemove);
  }

  const int32 RevisionToApply = CheckpointCompleteRevision != INDEX_NONE
    ? CheckpointCompleteRevision
    : HighestCompleteRevision;
  if (RevisionToApply != INDEX_NONE)
  {
    FCrowdDemoPendingCorrectionAssembly* Assembly = PendingCorrectionAssemblies.Find(RevisionToApply);
    if (Assembly && TryApplyClientCorrectionAssembly(*Assembly))
    {
      PendingCorrectionAssemblies.Remove(RevisionToApply);
      TArray<int32> SupersededRevisions;
      for (const TPair<int32, FCrowdDemoPendingCorrectionAssembly>& Pair : PendingCorrectionAssemblies)
      {
        if (Pair.Key < RevisionToApply
          && Pair.Value.Header.FrameKind != ECrowdDemoRoundFrameKind::RoundResultCheckpoint)
        {
          SupersededRevisions.Add(Pair.Key);
        }
      }
      CorrectionAssemblySupersededCount += SupersededRevisions.Num();
      for (const int32 SupersededRevision : SupersededRevisions)
      {
        PendingCorrectionAssemblies.Remove(SupersededRevision);
        DroppedCorrectionRevisions.Add(SupersededRevision);
      }
    }
  }

  RefreshLastCorrectionCounters();
}

bool ACrowdDemoRoundSimCoordinator::TryApplyClientCorrectionAssembly(FCrowdDemoPendingCorrectionAssembly& Assembly)
{
  const FCrowdDemoCorrectionFrameHeader& Header = Assembly.Header;
  if (Header.bValid == 0)
  {
    return false;
  }

  UWorld* World = GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline)
  {
    return false;
  }

  if (Header.RoundId > Pipeline->GetCurrentRoundId())
  {
    if (!FuturePendingCorrectionRevisions.Contains(Header.CorrectionRevision))
    {
      FuturePendingCorrectionRevisions.Add(Header.CorrectionRevision);
      ++CorrectionFrameFuturePendingCount;
    }
    return false;
  }

  if (Header.RoundId < Pipeline->GetCurrentRoundId() || Header.RoundRevision != Pipeline->GetCurrentPlanRevision())
  {
    ++CorrectionFrameDroppedMismatchCount;
    DroppedCorrectionRevisions.Add(Header.CorrectionRevision);
    if (DroppedCorrectionWarningCount < 8)
    {
      UE_LOG(
        LogTemp,
        Warning,
        TEXT("CrowdDemoCorrectionFrameMismatch role=client correction_revision=%d frame_round_id=%d frame_round_revision=%d runtime_round_id=%d runtime_revision=%d action=drop source=RoundSim"),
        Header.CorrectionRevision,
        Header.RoundId,
        Header.RoundRevision,
        Pipeline->GetCurrentRoundId(),
        Pipeline->GetCurrentPlanRevision());
      ++DroppedCorrectionWarningCount;
    }
    return true;
  }

  if (Assembly.ReceivedChunkCount != Header.ChunkCount || Assembly.ReceivedAgentCount != Header.AgentCount)
  {
    return false;
  }

  for (int32 ChunkIndex = 0; ChunkIndex < Header.ChunkCount; ++ChunkIndex)
  {
    if (!Assembly.ReceivedChunks.IsValidIndex(ChunkIndex) || Assembly.ReceivedChunks[ChunkIndex] == 0)
    {
      return false;
    }
  }

  if (!CompletedCorrectionRevisions.Contains(Header.CorrectionRevision))
  {
    CompletedCorrectionRevisions.Add(Header.CorrectionRevision);
    ++CorrectionFrameCompleteCount;
    ++CorrectionAssemblyCompleteCount;
  }

  const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
  const float CurrentClientServerTime = GameState
    ? GameState->GetServerWorldTimeSeconds()
    : (World ? World->GetTimeSeconds() : Header.ServerTimeSeconds);
  if (CurrentClientServerTime + KINDA_SMALL_NUMBER < Header.ServerTimeSeconds)
  {
    if (!FuturePendingCorrectionRevisions.Contains(Header.CorrectionRevision))
    {
      FuturePendingCorrectionRevisions.Add(Header.CorrectionRevision);
      ++CorrectionFrameFuturePendingCount;
    }
    return false;
  }

  const float FrameAgeMs = FMath::Max(0.0f, (CurrentClientServerTime - Header.ServerTimeSeconds) * 1000.0f);
  CorrectionFrameAgeMsSamples.Add(FrameAgeMs);
  if (Header.FrameKind == ECrowdDemoRoundFrameKind::Correction
    && FrameAgeMs > CrowdDemoMaxCorrectionFrameAgeMs)
  {
    ++CorrectionFrameStaleDropCount;
    DroppedCorrectionRevisions.Add(Header.CorrectionRevision);
    RefreshLastCorrectionCounters();
    if (DroppedCorrectionWarningCount < 8)
    {
      UE_LOG(
        LogTemp,
        Warning,
        TEXT("CrowdDemoCorrectionFrameStale role=client revision=%d round_id=%d frame_age_ms=%.3f max_age_ms=%.3f chunks=%d agents=%d action=drop source=RoundSim"),
        Header.CorrectionRevision,
        Header.RoundId,
        FrameAgeMs,
        CrowdDemoMaxCorrectionFrameAgeMs,
        Header.ChunkCount,
        Header.AgentCount);
      ++DroppedCorrectionWarningCount;
    }
    return true;
  }

  if (Header.FrameKind == ECrowdDemoRoundFrameKind::RoundResultCheckpoint)
  {
    FCrowdDemoRoundResultPacket Result;
    if (!TryBuildClientRoundResult(Assembly, Result))
    {
      return false;
    }
    Pipeline->QueueRoundResult(Result);
    ++RoundResultAssemblyCompleteCount;
    ++RoundResultPipelineQueuedCount;
    LastAppliedCorrectionRevision = Header.CorrectionRevision;
    PendingClientResultHeaders.Remove(Header.CorrectionRevision);
    PendingClientResultHeaderReceiveTimes.Remove(Header.CorrectionRevision);
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoRoundResultTransport role=client stage=assembly_queued round_id=%d checkpoint_revision=%d state_frame_revision=%d agents=%d chunks=%d assembly_complete_count=%d pipeline_queued_count=%d source=RoundSim"),
      Result.RoundId,
      Result.CheckpointRevision,
      Result.StateFrameRevision,
      Result.Agents.Num(),
      Header.ChunkCount,
      RoundResultAssemblyCompleteCount,
      RoundResultPipelineQueuedCount);
    return true;
  }

  FCrowdDemoCorrectionFrame FullFrame;
  FullFrame.bValid = 1;
  FullFrame.FrameKind = Header.FrameKind;
  FullFrame.CorrectionRevision = Header.CorrectionRevision;
  FullFrame.RoundId = Header.RoundId;
  FullFrame.RoundRevision = Header.RoundRevision;
  FullFrame.SourceCheckpointRevision = Header.SourceCheckpointRevision;
  FullFrame.ServerTimeSeconds = Header.ServerTimeSeconds;
  FullFrame.AgentCount = Header.AgentCount;
  FullFrame.CrowdState = Header.CrowdState;
  FullFrame.AgentStates = Assembly.AgentBuffer;

  Pipeline->QueueCorrectionFrame(FullFrame, CurrentClientServerTime);
  LastAppliedCorrectionRevision = Header.CorrectionRevision;
  RefreshLastCorrectionCounters();

  TArray<int32> OldRevisions;
  for (const TPair<int32, FCrowdDemoPendingCorrectionAssembly>& Pair : PendingCorrectionAssemblies)
  {
    if (Pair.Key < Header.CorrectionRevision)
    {
      OldRevisions.Add(Pair.Key);
    }
  }
  for (const int32 OldRevision : OldRevisions)
  {
    PendingCorrectionAssemblies.Remove(OldRevision);
    DroppedCorrectionRevisions.Add(OldRevision);
  }
  return true;
}

void ACrowdDemoRoundSimCoordinator::DropExpiredCorrectionAssemblies()
{
  if (HasAuthority())
  {
    return;
  }

  UWorld* World = GetWorld();
  if (!World)
  {
    return;
  }

  const double NowSeconds = World->GetTimeSeconds();
  TArray<int32> ExpiredRevisions;
  for (const TPair<int32, FCrowdDemoPendingCorrectionAssembly>& Pair : PendingCorrectionAssemblies)
  {
    const FCrowdDemoPendingCorrectionAssembly& Assembly = Pair.Value;
    const float TimeoutSeconds = Assembly.Header.FrameKind == ECrowdDemoRoundFrameKind::RoundResultCheckpoint
      ? CrowdDemoRoundResultAssemblyTimeoutSeconds
      : CrowdDemoCorrectionAssemblyTimeoutSeconds;
    if (Assembly.FirstReceiveWorldSeconds >= 0.0
      && NowSeconds - Assembly.FirstReceiveWorldSeconds > TimeoutSeconds)
    {
      ExpiredRevisions.Add(Pair.Key);
      if (Assembly.Header.FrameKind == ECrowdDemoRoundFrameKind::RoundResultCheckpoint)
      {
        const bool bHasResultHeader = PendingClientResultHeaders.Contains(Pair.Key);
        if (bHasResultHeader)
        {
          ++RoundResultChunkWaitTimeoutCount;
        }
        else
        {
          ++RoundResultHeaderWaitTimeoutCount;
        }
        UE_LOG(
          LogTemp,
          Error,
          TEXT("VIOLATION CrowdDemoRoundResultTransport role=client stage=%s state_frame_revision=%d chunks=%d/%d agents=%d/%d timeout_count=%d source=RoundSim"),
          bHasResultHeader ? TEXT("chunk_wait_timeout") : TEXT("header_wait_timeout"),
          Pair.Key,
          Assembly.ReceivedChunkCount,
          Assembly.Header.ChunkCount,
          Assembly.ReceivedAgentCount,
          Assembly.Header.AgentCount,
          bHasResultHeader ? RoundResultChunkWaitTimeoutCount : RoundResultHeaderWaitTimeoutCount);
      }
    }
  }

  TArray<int32> ExpiredResultHeaders;
  for (const TPair<int32, double>& Pair : PendingClientResultHeaderReceiveTimes)
  {
    if (!PendingCorrectionAssemblies.Contains(Pair.Key)
      && NowSeconds - Pair.Value > CrowdDemoRoundResultAssemblyTimeoutSeconds)
    {
      ExpiredResultHeaders.Add(Pair.Key);
      ++RoundResultChunkWaitTimeoutCount;
      UE_LOG(
        LogTemp,
        Error,
        TEXT("VIOLATION CrowdDemoRoundResultTransport role=client stage=chunk_wait_timeout state_frame_revision=%d timeout_count=%d source=RoundSim"),
        Pair.Key,
        RoundResultChunkWaitTimeoutCount);
    }
  }

  if (!ExpiredRevisions.IsEmpty())
  {
    CorrectionFrameIncompleteDropCount += ExpiredRevisions.Num();
    for (const int32 RevisionToDrop : ExpiredRevisions)
    {
      PendingCorrectionAssemblies.Remove(RevisionToDrop);
      DroppedCorrectionRevisions.Add(RevisionToDrop);
      PendingClientResultHeaders.Remove(RevisionToDrop);
      PendingClientResultHeaderReceiveTimes.Remove(RevisionToDrop);
    }
    RefreshLastCorrectionCounters();
  }
  for (const int32 RevisionToDrop : ExpiredResultHeaders)
  {
    PendingClientResultHeaders.Remove(RevisionToDrop);
    PendingClientResultHeaderReceiveTimes.Remove(RevisionToDrop);
  }
}

void ACrowdDemoRoundSimCoordinator::RefreshLastCompareCounters()
{
  LastCompareMetrics.CurrentRoundId = CurrentRoundPlan.RoundId;
  LastCompareMetrics.CompletedRoundCount = CompletedRoundCount;
  LastCompareMetrics.CorrectionAppliedCount = CorrectionAppliedCount;
  LastCompareMetrics.CheckpointRevision = LastCheckpointRevision;
  LastCompareMetrics.RoundBoundaryCenterJumpCmP95 = ComputeCoordinatorP95(RoundBoundaryCenterJumpCmSamples);
  LastCompareMetrics.RoundBoundaryYawJumpDegP95 = ComputeCoordinatorP95(RoundBoundaryYawJumpDegSamples);
  LastCompareMetrics.RoundBoundaryVelocityJumpCmpsP95 = ComputeCoordinatorP95(RoundBoundaryVelocityJumpCmpsSamples);
  LastCompareMetrics.RoundPlanRevisionSeen = HasAuthority() ? Revision : RoundPlanRevisionSeen;
  LastCompareMetrics.RoundPlanAppliedCount = HasAuthority() ? CompletedRoundCount + (CurrentRoundPlan.bValid != 0 ? 1 : 0) : RoundPlanAppliedCount;
  LastCompareMetrics.RoundPlanGapCount = RoundPlanGapCount;
  LastCompareMetrics.RoundPlanLateCount = RoundPlanLateCount;
  LastCompareMetrics.RoundBootstrapAgentCount = RoundBootstrapPacket.Agents.Num();
  LastCompareMetrics.SyntheticSkippedCheckpointCount = SyntheticSkippedCheckpointCount;
}

void ACrowdDemoRoundSimCoordinator::RecordRoundBoundaryMetrics(
  const TConstArrayView<FCrowdDemoRoundAgentState> PreviousAgents,
  const TConstArrayView<FCrowdDemoRoundAgentState> NextAgents)
{
  if (PreviousAgents.IsEmpty() || NextAgents.IsEmpty())
  {
    return;
  }

  const FVector PreviousCenter = ComputeRoundCohortCenter(PreviousAgents);
  const FVector NextCenter = ComputeRoundCohortCenter(NextAgents);
  FVector PreviousVelocity = FVector::ZeroVector;
  FVector NextVelocity = FVector::ZeroVector;
  for (const FCrowdDemoRoundAgentState& Agent : PreviousAgents)
  {
    PreviousVelocity += FVector(Agent.Velocity);
  }
  for (const FCrowdDemoRoundAgentState& Agent : NextAgents)
  {
    NextVelocity += FVector(Agent.Velocity);
  }
  PreviousVelocity /= static_cast<float>(PreviousAgents.Num());
  NextVelocity /= static_cast<float>(NextAgents.Num());

  const float CenterJumpCm = FVector::Dist2D(PreviousCenter, NextCenter);
  const float YawJumpDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousAgents[0].YawDegrees, NextAgents[0].YawDegrees));
  const float VelocityJumpCmps = FVector::Dist2D(PreviousVelocity, NextVelocity);
  RoundBoundaryCenterJumpCmSamples.Add(CenterJumpCm);
  RoundBoundaryYawJumpDegSamples.Add(YawJumpDeg);
  RoundBoundaryVelocityJumpCmpsSamples.Add(VelocityJumpCmps);

  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundBoundary role=server round_id=%d center_jump_cm=%.3f yaw_jump_deg=%.3f velocity_jump_cmps=%.3f source=RoundSim"),
    CurrentRoundPlan.RoundId,
    CenterJumpCm,
    YawJumpDeg,
    VelocityJumpCmps);
}

void ACrowdDemoRoundSimCoordinator::RefreshLastCorrectionCounters()
{
  LastCorrectionFrameMetrics.CorrectionFrameRevision = LastAppliedCorrectionRevision;
  LastCorrectionFrameMetrics.CorrectionFrameAppliedCount = CorrectionFrameAppliedCount;
  LastCorrectionFrameMetrics.CorrectionFrameHeaderReceivedCount = CorrectionFrameHeaderReceivedCount;
  LastCorrectionFrameMetrics.CorrectionFrameChunkReceivedCount = CorrectionFrameChunkReceivedCount;
  LastCorrectionFrameMetrics.LatestChunkRevisionSeen = LatestChunkRevisionSeen;
  LastCorrectionFrameMetrics.CorrectionChunkReceivedCount = CorrectionChunkReceivedCount;
  LastCorrectionFrameMetrics.CorrectionUniqueChunkCount = CorrectionUniqueChunkCount;
  LastCorrectionFrameMetrics.CorrectionExpectedChunkCount = CorrectionExpectedChunkCount;
  LastCorrectionFrameMetrics.CorrectionAssemblyCompleteCount = CorrectionAssemblyCompleteCount;
  LastCorrectionFrameMetrics.CorrectionAssemblySupersededCount = CorrectionAssemblySupersededCount;
  LastCorrectionFrameMetrics.CorrectionFrameCompleteCount = CorrectionFrameCompleteCount;
  LastCorrectionFrameMetrics.CorrectionFramePublishedCount = CorrectionFramePublishedCount;
  LastCorrectionFrameMetrics.CorrectionFrameReceivedCount = CorrectionFrameReceivedCount;
  LastCorrectionFrameMetrics.CorrectionFrameDroppedOldCount = CorrectionFrameDroppedOldCount;
  LastCorrectionFrameMetrics.CorrectionFrameDroppedMismatchCount = CorrectionFrameDroppedMismatchCount;
  LastCorrectionFrameMetrics.CorrectionFrameFuturePendingCount = CorrectionFrameFuturePendingCount;
  LastCorrectionFrameMetrics.CorrectionFrameFutureDropCount = CorrectionFrameFutureDropCount;
  LastCorrectionFrameMetrics.CorrectionFrameIncompleteDropCount = CorrectionFrameIncompleteDropCount;
  LastCorrectionFrameMetrics.CorrectionFrameStaleDropCount = CorrectionFrameStaleDropCount;
  LastCorrectionFrameMetrics.CorrectionFrameReplayToNowCount = CorrectionFrameReplayToNowCount;
  LastCorrectionFrameMetrics.CorrectionFrameLatestRevisionSeen = CorrectionFrameLatestRevisionSeen;
  LastCorrectionFrameMetrics.CorrectionFrameLatestRevisionApplied = CorrectionFrameLatestRevisionApplied;
  LastCorrectionFrameMetrics.CorrectionFrameRevisionGapCount = CorrectionFrameRevisionGapCount;
  LastCorrectionFrameMetrics.CorrectionFrameChunksPerFrame = HasAuthority()
    ? LastServerCorrectionChunkCount
    : FMath::Max(0, CorrectionFrameHeader.ChunkCount);
  LastCorrectionFrameMetrics.CorrectionFrameChunkSize = HasAuthority()
    ? LastServerCorrectionChunkSize
    : FMath::Max(0, CorrectionFrameHeader.ChunkSize);
  LastCorrectionFrameMetrics.CorrectionIntervalMsP95 = HasAuthority()
    ? ComputeCoordinatorP95(CorrectionFramePublishIntervalMsSamples)
    : ComputeCoordinatorP95(CorrectionFrameIntervalMsSamples);
  LastCorrectionFrameMetrics.CorrectionFrameAssemblyMsP95 = ComputeCoordinatorP95(CorrectionFrameAssemblyMsSamples);
  LastCorrectionFrameMetrics.CorrectionFrameAgeMsP50 = ComputeCoordinatorP95(CorrectionFrameAgeMsSamples);
  if (!CorrectionFrameAgeMsSamples.IsEmpty())
  {
    TArray<float> AgeSamples = CorrectionFrameAgeMsSamples;
    AgeSamples.Sort();
    const int32 P50Index = FMath::Clamp(FMath::CeilToInt(static_cast<float>(AgeSamples.Num()) * 0.50f) - 1, 0, AgeSamples.Num() - 1);
    const int32 P95Index = FMath::Clamp(FMath::CeilToInt(static_cast<float>(AgeSamples.Num()) * 0.95f) - 1, 0, AgeSamples.Num() - 1);
    LastCorrectionFrameMetrics.CorrectionFrameAgeMsP50 = AgeSamples[P50Index];
    LastCorrectionFrameMetrics.CorrectionFrameAgeMsP95 = AgeSamples[P95Index];
  }
  else
  {
    LastCorrectionFrameMetrics.CorrectionFrameAgeMsP50 = -1.0f;
    LastCorrectionFrameMetrics.CorrectionFrameAgeMsP95 = -1.0f;
  }
  LastCorrectionFrameMetrics.CorrectionFrameReplayMsP95 = ComputeCoordinatorP95(CorrectionFrameReplayMsSamples);
  if (UWorld* World = GetWorld())
  {
    if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
    {
      Pipeline->MergeNetworkCorrectionMetrics(LastCorrectionFrameMetrics);
    }
  }
}
