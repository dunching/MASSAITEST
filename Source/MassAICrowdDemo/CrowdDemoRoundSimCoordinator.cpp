#include "CrowdDemoRoundSimCoordinator.h"

#include "CrowdDemoReplicator.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoRoundCheckpointTransport.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"

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
  constexpr int32 CrowdDemoCorrectionChunksPerFlush = 1;

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

  FString SerializeSf4HallGeometryFixture(const FCrowdDemoHallGeometryFixture& F)
  {
    const FCrowdDemoHoldingPathBlockerFact& B = F.BestFact;
    return FString::Printf(
      TEXT("{\n  \"fixture_hash\": %u,\n  \"valid\": %s,\n  \"agent_id\": %d,\n  \"position_id\": %d,\n  \"position_cm\": [%d,%d],\n  \"holding_candidate_count\": %d,\n  \"best_holding_id\": %d,\n  \"best_clearance_margin_millimeters\": %d,\n  \"best_blocker_agent_id\": %d,\n  \"nonnegative_margin_holding_count\": %d,\n  \"reject_counts\": {\"stable\":%d,\"target\":%d,\"reserve\":%d,\"target_only\":%d,\"stable_only\":%d,\"multi_label\":%d},\n  \"audit_counts\": {\"self\":%d,\"witness_position\":%d,\"duplicate\":%d,\"stale\":%d,\"radius_semantics\":%d,\"endpoint\":%d,\"formal_mismatch\":%d},\n  \"best_fact\": {\"holding_id\":%d,\"position_id\":%d,\"blocker_agent_id\":%d,\"blocker_position_id\":%d,\"blocker_state\":%d,\"segment_start_cm\":[%d,%d],\"segment_end_cm\":[%d,%d],\"blocker_center_cm\":[%d,%d],\"agent_radius_cm\":%d,\"blocker_radius_cm\":%d,\"safety_gap_cm\":%d,\"required_clearance_cm\":%d,\"actual_distance_millimeters\":%d,\"margin_millimeters\":%d,\"closest_t_q100000\":%d,\"endpoint\":%s,\"target_rejected\":%s,\"stable_rejected\":%s,\"reserve_rejected\":%s,\"formal_mismatch\":%s,\"hash\":%u}\n}\n"),
      F.FixtureHash, F.bValid ? TEXT("true") : TEXT("false"), F.AgentId, F.PositionId,
      FMath::RoundToInt(F.PositionLocation.X), FMath::RoundToInt(F.PositionLocation.Y),
      F.HoldingCandidateCount, F.BestHoldingId,
      FMath::RoundToInt(F.BestClearanceMarginCm * 10.0f), F.BestBlockerAgentId,
      F.NonNegativeMarginHoldingCount, F.RejectedByStableCount, F.RejectedByTargetCount,
      F.RejectedByReserveCount, F.TargetOnlyRejectCount, F.StableOnlyRejectCount,
      F.MultiLabelRejectCount, F.SelfBlockerCount, F.BlockerUsesWitnessPositionCount,
      F.DuplicateBlockerCount, F.StaleBlockerCount, F.RadiusSemanticsErrorCount,
      F.EndpointContactCount, F.FormalClassificationMismatchCount,
      B.HoldingId, B.PositionId, B.BlockerAgentId, B.BlockerPositionId,
      static_cast<int32>(B.BlockerState), FMath::RoundToInt(B.SegmentStart.X),
      FMath::RoundToInt(B.SegmentStart.Y), FMath::RoundToInt(B.SegmentEnd.X),
      FMath::RoundToInt(B.SegmentEnd.Y), FMath::RoundToInt(B.BlockerCenter.X),
      FMath::RoundToInt(B.BlockerCenter.Y), FMath::RoundToInt(B.AgentRadiusCm),
      FMath::RoundToInt(B.BlockerRadiusCm), FMath::RoundToInt(B.SafetyGapCm),
      FMath::RoundToInt(B.RequiredClearanceCm),
      FMath::RoundToInt(B.ActualClosestDistanceCm * 10.0f),
      FMath::RoundToInt(B.ClearanceMarginCm * 10.0f),
      FMath::RoundToInt(B.ClosestPointT * 100000.0f),
      B.bEndpointContact ? TEXT("true") : TEXT("false"),
      B.bTargetRejected ? TEXT("true") : TEXT("false"),
      B.bStableRejected ? TEXT("true") : TEXT("false"),
      B.bReserveRejected ? TEXT("true") : TEXT("false"),
      B.bFormalClassificationMismatch ? TEXT("true") : TEXT("false"), B.StableHash);
  }

  FString SerializeSf4JointPositioningResult(const FCrowdDemoJointPositioningResult& R)
  {
    FString Json = FString::Printf(
      TEXT("{\n  \"hash\":%u,\n  \"valid\":%s,\n  \"maximum_cardinality\":%d,\n  \"hard_locked\":%d,\n  \"reused_combinations\":%d,\n  \"unmatched\":%d,\n  \"duplicate_holdings\":%d,\n  \"duplicate_positions\":%d,\n  \"assignments\":[\n"),
      R.StableHash, R.bValid ? TEXT("true") : TEXT("false"), R.MaximumCardinality,
      R.HardLockedCount, R.ReusedCombinationCount, R.UnmatchedAgentCount,
      R.DuplicateHoldingCount, R.DuplicatePositionCount);
    for (int32 Index=0; Index<R.Assignments.Num(); ++Index)
    {
      const auto& A=R.Assignments[Index];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"holding_id\":%d,\"position_id\":%d,\"hard_locked\":%s,\"reused_holding\":%s,\"reused_position\":%s}%s\n"),
        A.AgentId,A.HoldingId,A.PositionId,A.bHardLocked?TEXT("true"):TEXT("false"),
        A.bReusedHolding?TEXT("true"):TEXT("false"),
        A.bReusedPosition?TEXT("true"):TEXT("false"),
        Index+1<R.Assignments.Num()?TEXT(","):TEXT(""));
    }
    Json += TEXT("  ]\n}\n");
    return Json;
  }

  FString SerializeSf4JointCommitResidualResult(const FCrowdDemoJointCommitResidualResult& R)
  {
    FString Json=FString::Printf(TEXT("{\n  \"hash\":%u,\n  \"valid\":%s,\n  \"candidate_count\":%d,\n  \"feasible_count\":%d,\n  \"infeasible_count\":%d,\n  \"decisions\":[\n"),
      R.StableHash,R.bValid?TEXT("true"):TEXT("false"),R.CandidateCount,R.FeasibleCount,R.InfeasibleCount);
    for(int32 Index=0;Index<R.Decisions.Num();++Index)
    {const auto&D=R.Decisions[Index];Json+=FString::Printf(
      TEXT("    {\"agent_id\":%d,\"holding_id\":%d,\"position_id\":%d,\"remaining\":%d,\"residual_matching\":%d,\"grant_feasible\":%s}%s\n"),
      D.AgentId,D.HoldingId,D.PositionId,D.RemainingAgentCountAfterGrant,
      D.ResidualMatchingAfterGrant,D.bGrantFeasible?TEXT("true"):TEXT("false"),
      Index+1<R.Decisions.Num()?TEXT(","):TEXT(""));}
    Json+=TEXT("  ]\n}\n");return Json;
  }

  FString SerializeSf4HoldingHallFixture(const FCrowdDemoHoldingHallFixture& Fixture)
  {
    const FCrowdDemoHoldingHallSummary& S = Fixture.Summary;
    FString Json = FString::Printf(
      TEXT("{\n  \"target_revision\": %d,\n  \"fixture_hash\": %u,\n  \"valid\": %s,\n  \"exact\": %s,\n  \"matching\": {\"current\": %d, \"without_stable_owner\": %d, \"without_reserve_owner\": %d, \"without_commit_owner\": %d},\n  \"hall\": {\"agent_count\": %d, \"available_holding_count\": %d, \"deficiency\": %d},\n  \"reject_counts\": {\"missing_record\": %d, \"flow\": %d, \"target\": %d, \"obstacle\": %d, \"revision\": %d, \"stable_owner\": %d, \"reserve_owner\": %d},\n  \"agent_ids\": ["),
      Fixture.TargetRevision, S.StableHash, S.bValid ? TEXT("true") : TEXT("false"),
      S.bExact ? TEXT("true") : TEXT("false"), S.CurrentMatchingCount,
      S.NoStableOwnerMatchingCount, S.NoReserveOwnerMatchingCount,
      S.NoCommitOwnerMatchingCount, S.HallAgentCount, S.HallAvailableHoldingCount,
      S.HallDeficiency, S.MissingCompatibilityRecordCount, S.FlowRejectCount,
      S.TargetRejectCount, S.ObstacleRejectCount, S.RevisionRejectCount,
      S.StableOwnerRejectCount, S.ReserveOwnerRejectCount);
    for (int32 Index = 0; Index < Fixture.AgentIds.Num(); ++Index)
      Json += FString::Printf(TEXT("%s%d"), Index == 0 ? TEXT("") : TEXT(","), Fixture.AgentIds[Index]);
    Json += TEXT("],\n  \"available_holding_ids\": [");
    for (int32 Index = 0; Index < Fixture.AvailableHoldingIds.Num(); ++Index)
      Json += FString::Printf(TEXT("%s%d"), Index == 0 ? TEXT("") : TEXT(","), Fixture.AvailableHoldingIds[Index]);
    Json += TEXT("],\n  \"edges\": [\n");
    for (int32 EdgeIndex = 0; EdgeIndex < Fixture.Edges.Num(); ++EdgeIndex)
    {
      const FCrowdDemoHoldingHallEdge& E = Fixture.Edges[EdgeIndex];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"position_id\":%d,\"holding_id\":%d,\"record\":%s,\"flow\":%s,\"target\":%s,\"obstacle\":%s,\"revision\":%s,\"compatible\":%s,\"stable_blockers\":["),
        E.AgentId, E.PositionId, E.HoldingId,
        E.bCompatibilityRecordPresent ? TEXT("true") : TEXT("false"),
        E.bFlowClear ? TEXT("true") : TEXT("false"),
        E.bTargetClear ? TEXT("true") : TEXT("false"),
        E.bObstacleClear ? TEXT("true") : TEXT("false"),
        E.bRevisionValid ? TEXT("true") : TEXT("false"),
        E.bCompatible ? TEXT("true") : TEXT("false"));
      for (int32 Index = 0; Index < E.StableBlockerAgentIds.Num(); ++Index)
        Json += FString::Printf(TEXT("%s%d"), Index == 0 ? TEXT("") : TEXT(","), E.StableBlockerAgentIds[Index]);
      Json += TEXT("],\"reserve_blockers\":[");
      for (int32 Index = 0; Index < E.ReserveBlockerAgentIds.Num(); ++Index)
        Json += FString::Printf(TEXT("%s%d"), Index == 0 ? TEXT("") : TEXT(","), E.ReserveBlockerAgentIds[Index]);
      Json += FString::Printf(TEXT("]}%s\n"), EdgeIndex + 1 < Fixture.Edges.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ]\n}\n");
    return Json;
  }

  FString SerializeSf4ReservationOrcaFixture(
    const FCrowdDemoSf4ReservationOrcaDiagnosticFixture& Fixture)
  {
    FString Json = FString::Printf(
      TEXT("{\n  \"hash\": %u,\n  \"primary_agent_id\": %d,\n  \"safety_gap_cm\": %d,\n  \"fixed_step_micros\": %d,\n  \"minimum_forward_cmps\": %d,\n  \"target\": {\"id\": %d, \"x_cm\": %d, \"y_cm\": %d, \"radius_cm\": %d},\n  \"agents\": [\n"),
      Fixture.StableHash, Fixture.Summary.PrimaryAgentId,
      FMath::RoundToInt(Fixture.SafetyGapCm),
      FMath::RoundToInt(Fixture.FixedStepSeconds * 1000000.0f),
      FMath::RoundToInt(Fixture.MinimumForwardSpeedCmps), Fixture.Target.TargetId,
      FMath::RoundToInt(Fixture.Target.Location.X),
      FMath::RoundToInt(Fixture.Target.Location.Y),
      FMath::RoundToInt(Fixture.Target.RadiusCm));
    for (int32 AgentIndex = 0; AgentIndex < Fixture.Agents.Num(); ++AgentIndex)
    {
      const FCrowdDemoSf4ReservationOrcaFixtureAgent& Agent = Fixture.Agents[AgentIndex];
      Json += FString::Printf(
        TEXT("    {\"agent_id\": %d, \"state\": %d, \"phase\": %d, \"position_cm\": [%d,%d], \"velocity_cmps\": [%d,%d], \"preferred_cmps\": [%d,%d], \"baseline_cmps\": [%d,%d], \"radius_cm\": %d, \"max_speed_cmps\": %d, \"route\": ["),
        Agent.Agent.AgentId, static_cast<int32>(Agent.PositionState),
        static_cast<int32>(Agent.CurrentPhase),
        FMath::RoundToInt(Agent.Agent.Position.X), FMath::RoundToInt(Agent.Agent.Position.Y),
        FMath::RoundToInt(Agent.Agent.Velocity.X), FMath::RoundToInt(Agent.Agent.Velocity.Y),
        FMath::RoundToInt(Agent.Agent.PreferredVelocity.X),
        FMath::RoundToInt(Agent.Agent.PreferredVelocity.Y),
        FMath::RoundToInt(Agent.BaselineVelocity.X),
        FMath::RoundToInt(Agent.BaselineVelocity.Y),
        FMath::RoundToInt(Agent.Agent.RadiusCm), FMath::RoundToInt(Agent.Agent.MaxSpeedCmps));
      for (int32 PointIndex = 0; PointIndex < Agent.Agent.Sf4RoutePoints.Num(); ++PointIndex)
      {
        const FVector2f Point = Agent.Agent.Sf4RoutePoints[PointIndex];
        Json += FString::Printf(TEXT("[%d,%d]%s"), FMath::RoundToInt(Point.X),
          FMath::RoundToInt(Point.Y),
          PointIndex + 1 < Agent.Agent.Sf4RoutePoints.Num() ? TEXT(",") : TEXT(""));
      }
      Json += TEXT("], \"constraints\": [");
      for (int32 ConstraintIndex = 0; ConstraintIndex < Agent.Constraints.Num(); ++ConstraintIndex)
      {
        const FCrowdDemoSf4SourcedOrcaConstraint& Sourced = Agent.Constraints[ConstraintIndex];
        const FCrowdDemoOrcaConstraint& Constraint = Sourced.Constraint;
        Json += FString::Printf(
          TEXT("{\"other_agent_id\":%d,\"order\":%d,\"source\":%d,\"kind\":%d,\"point_cmps\":[%d,%d],\"normal_q15\":[%d,%d],\"responsibility_q1000\":%d}%s"),
          Constraint.OtherAgentId, Constraint.StableConstraintOrder,
          static_cast<int32>(Sourced.Source), static_cast<int32>(Constraint.Kind),
          FMath::RoundToInt(Constraint.Point.X), FMath::RoundToInt(Constraint.Point.Y),
          FMath::RoundToInt(Constraint.Normal.X * 32767.0f),
          FMath::RoundToInt(Constraint.Normal.Y * 32767.0f),
          FMath::RoundToInt(Constraint.Responsibility * 1000.0f),
          ConstraintIndex + 1 < Agent.Constraints.Num() ? TEXT(",") : TEXT(""));
      }
      Json += FString::Printf(TEXT("]}%s\n"),
        AgentIndex + 1 < Fixture.Agents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"core_constraints\": [");
    for (int32 Index = 0; Index < Fixture.CoreConstraints.Num(); ++Index)
    {
      const FCrowdDemoSf4ClassifiedReservationConstraint& Core = Fixture.CoreConstraints[Index];
      Json += FString::Printf(
        TEXT("{\"primary_agent_id\":%d,\"other_agent_id\":%d,\"order\":%d,\"classification\":%d}%s"),
        Core.PrimaryAgentId, Core.OtherAgentId, Core.StableConstraintOrder,
        static_cast<int32>(Core.Classification),
        Index + 1 < Fixture.CoreConstraints.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("]\n}\n");
    return Json;
  }

  FString SerializeTransitJointFixture(
    const FCrowdDemoTransitJointDiagnosticFixture& Fixture)
  {
    const auto Vec = [](const FVector2f Value)
    {
      return FString::Printf(TEXT("[%d,%d]"),
        FMath::RoundToInt(Value.X), FMath::RoundToInt(Value.Y));
    };
    FString Json = FString::Printf(
      TEXT("{\n  \"hash\":%u,\n  \"valid\":%s,\n  \"primary_agent_id\":%d,\n  \"component_agent_count\":%d,\n  \"component_pair_count\":%d,\n  \"constraint_count\":%d,\n  \"fixture_too_large\":%s,\n  \"priority_forward_cmps\":%d,\n  \"joint_forward_cmps\":%d,\n  \"final_speed_cmps\":%d,\n  \"downstream_zero_stage\":%d,\n  \"joint_status\":%d,\n  \"joint_hard_violation_count\":%d,\n  \"agents\":[\n"),
      Fixture.StableHash, Fixture.bValid ? TEXT("true") : TEXT("false"),
      Fixture.Summary.PrimaryAgentId, Fixture.Summary.ComponentAgentCount,
      Fixture.Summary.ComponentPairCount, Fixture.Summary.ConstraintCount,
      Fixture.Summary.bFixtureTooLarge ? TEXT("true") : TEXT("false"),
      Fixture.Summary.PriorityForwardSpeedCmps, Fixture.Summary.JointForwardSpeedCmps,
      Fixture.Summary.FinalSpeedCmps,
      static_cast<int32>(Fixture.Summary.DownstreamZeroStage),
      static_cast<int32>(Fixture.Summary.JointStatus),
      Fixture.Summary.JointHardViolationCount);
    for (int32 AgentIndex = 0; AgentIndex < Fixture.Agents.Num(); ++AgentIndex)
    {
      const FCrowdDemoTransitJointDiagnosticAgent& Agent = Fixture.Agents[AgentIndex];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"steering_state\":%d,\"radius_cm\":%d,\"max_speed_cmps\":%d,\"motion_weight_q8\":%d,\"recovery_weight_q8\":%d,\"start\":%s,\"preferred\":%s,\"priority_orca\":%s,\"predicted_velocity\":%s,\"obstacle_velocity\":%s,\"pbd_velocity\":%s,\"reproject_velocity\":%s,\"final_velocity\":%s,\"predicted_location\":%s,\"obstacle_location\":%s,\"pbd_location\":%s,\"reproject_location\":%s,\"final_location\":%s,\"pbd_correction\":%s,\"reproject_delta\":%s,\"constraints\":["),
        Agent.JointAgent.AgentId, Agent.SteeringState,
        FMath::RoundToInt(Agent.JointAgent.PhysicalRadiusCm),
        FMath::RoundToInt(Agent.JointAgent.MaxSpeedCmps),
        Agent.JointAgent.MotionWeightQ8, Agent.JointAgent.RecoveryWeightQ8,
        *Vec(Agent.StartLocation), *Vec(Agent.JointAgent.PreferredVelocity),
        *Vec(Agent.PriorityOrcaVelocity), *Vec(Agent.PredictedVelocity),
        *Vec(Agent.ObstacleVelocity), *Vec(Agent.PbdVelocity),
        *Vec(Agent.ReprojectVelocity), *Vec(Agent.FinalVelocity),
        *Vec(Agent.PredictedLocation), *Vec(Agent.ObstacleLocation),
        *Vec(Agent.PbdLocation), *Vec(Agent.ReprojectLocation),
        *Vec(Agent.FinalLocation), *Vec(Agent.PbdCorrection),
        *Vec(Agent.ObstacleReprojectDelta));
      for (int32 ConstraintIndex = 0;
        ConstraintIndex < Agent.PriorityConstraints.Num(); ++ConstraintIndex)
      {
        const FCrowdDemoOrcaConstraint& Constraint =
          Agent.PriorityConstraints[ConstraintIndex];
        Json += FString::Printf(
          TEXT("{\"other\":%d,\"point\":%s,\"normal_q15\":%s,\"kind\":%d,\"responsibility_q100\":%d}%s"),
          Constraint.OtherAgentId, *Vec(Constraint.Point),
          *Vec(Constraint.Normal * 32767.0f), static_cast<int32>(Constraint.Kind),
          FMath::RoundToInt(Constraint.Responsibility * 100.0f),
          ConstraintIndex + 1 < Agent.PriorityConstraints.Num() ? TEXT(",") : TEXT(""));
      }
      Json += FString::Printf(TEXT("]}%s\n"),
        AgentIndex + 1 < Fixture.Agents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"pairs\":[\n");
    for (int32 PairIndex = 0; PairIndex < Fixture.Pairs.Num(); ++PairIndex)
    {
      const FCrowdDemoJointVelocityPair& Pair = Fixture.Pairs[PairIndex];
      Json += FString::Printf(
        TEXT("    {\"a\":%d,\"b\":%d,\"relative_point\":%s,\"normal_q15\":%s,\"hard_gap_cm\":%d,\"preferred_gap_cm\":%d,\"context_q15\":%d}%s\n"),
        Pair.AgentAId, Pair.AgentBId, *Vec(Pair.Canonical.RelativeVelocityPoint),
        *Vec(Pair.Canonical.Normal * 32767.0f), FMath::RoundToInt(Pair.HardSafetyGapCm),
        FMath::RoundToInt(Pair.PreferredSpacingGapCm), Pair.ContextScaleQ15,
        PairIndex + 1 < Fixture.Pairs.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"joint_velocities\":[");
    for (int32 Index = 0; Index < Fixture.JointResult.Agents.Num(); ++Index)
    {
      const FCrowdDemoJointVelocityAgentResult& Agent = Fixture.JointResult.Agents[Index];
      Json += FString::Printf(TEXT("{\"agent_id\":%d,\"velocity\":%s}%s"),
        Agent.AgentId, *Vec(Agent.Velocity),
        Index + 1 < Fixture.JointResult.Agents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("]\n}\n");
    return Json;
  }

  FString SerializeTransitCapacityFailureFixture(
    const FCrowdDemoTransitCapacityFailureFixture& Fixture)
  {
    const auto Vec = [](const FVector2f Value)
    {
      return FString::Printf(TEXT("[%d,%d]"),
        FMath::RoundToInt(Value.X), FMath::RoundToInt(Value.Y));
    };
    const auto Ids = [](const TConstArrayView<int32> Values)
    {
      FString Result = TEXT("[");
      for (int32 Index = 0; Index < Values.Num(); ++Index)
      {
        if (Index > 0) Result += TEXT(",");
        Result += FString::FromInt(Values[Index]);
      }
      Result += TEXT("]");
      return Result;
    };
    FString Json = FString::Printf(
      TEXT("{\n  \"hash\":%u,\n  \"valid\":%s,\n  \"component_id\":%d,\n  \"status\":%d,\n  \"agent_count\":%d,\n  \"pair_count\":%d,\n  \"direct_relevant_agent_ids\":%s,\n  \"hard_safety_closure_agent_ids\":%s,\n  \"joint_candidate_hard_violations\":%d,\n  \"baseline_hard_violations\":%d,\n  \"joint_candidate_clearance_deficit_cm\":%d,\n  \"baseline_clearance_deficit_cm\":%d,\n  \"agents\":[\n"),
      Fixture.StableHash, Fixture.bValid ? TEXT("true") : TEXT("false"),
      Fixture.Component.ComponentId, static_cast<int32>(Fixture.Result.Status),
      Fixture.Agents.Num(), Fixture.Pairs.Num(),
      *Ids(Fixture.Component.DirectTransitRelevantAgentIds),
      *Ids(Fixture.Component.HardSafetyClosureAgentIds),
      Fixture.Result.JointCandidateHardPairViolationCount,
      Fixture.Result.BaselineFallbackHardPairViolationCount,
      FMath::RoundToInt(Fixture.Result.JointCandidateClearanceDeficitCmMax),
      FMath::RoundToInt(Fixture.Result.BaselineFallbackClearanceDeficitCmMax));
    for (int32 Index = 0; Index < Fixture.Agents.Num(); ++Index)
    {
      const FCrowdDemoJointVelocityAgent& Agent = Fixture.Agents[Index];
      const FCrowdDemoJointVelocityAgentResult* Result =
        Fixture.Result.Agents.FindByPredicate([&](const auto& Candidate)
          { return Candidate.AgentId == Agent.AgentId; });
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"transit_seed\":%s,\"position\":%s,\"preferred\":%s,\"joint_candidate\":%s,\"baseline\":%s}%s\n"),
        Agent.AgentId, Agent.bTransitSeed ? TEXT("true") : TEXT("false"),
        *Vec(Agent.Position), *Vec(Agent.PreferredVelocity),
        *Vec(Result ? Result->JointCandidateVelocity : FVector2f::ZeroVector),
        *Vec(Result ? Result->BaselineVelocity : FVector2f::ZeroVector),
        Index + 1 < Fixture.Agents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"transit_intents\":[\n");
    for (int32 Index = 0; Index < Fixture.TransitIntents.Num(); ++Index)
    {
      const FCrowdDemoTransitIntent& Intent = Fixture.TransitIntents[Index];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"start\":%s,\"end\":%s,\"horizon_ms\":%d,\"priority_q8\":%d}%s\n"),
        Intent.AgentId, *Vec(Intent.Position), *Vec(Intent.PredictedEnd),
        FMath::RoundToInt(Intent.PredictionHorizonSeconds * 1000.0f),
        Intent.PriorityQ8,
        Index + 1 < Fixture.TransitIntents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"pairs\":[\n");
    for (int32 Index = 0; Index < Fixture.Pairs.Num(); ++Index)
    {
      const FCrowdDemoJointVelocityPair& Pair = Fixture.Pairs[Index];
      const FCrowdDemoJointVelocityPairResidual* Residual =
        Fixture.Result.PairResiduals.FindByPredicate([&](const auto& Candidate)
          { return Candidate.AgentAId == Pair.AgentAId && Candidate.AgentBId == Pair.AgentBId; });
      Json += FString::Printf(
        TEXT("    {\"a\":%d,\"b\":%d,\"point\":%s,\"normal_q15\":%s,\"joint_hard_deficit_cm\":%d,\"baseline_hard_deficit_cm\":%d,\"joint_canonical_deficit_cmps\":%d,\"baseline_canonical_deficit_cmps\":%d}%s\n"),
        Pair.AgentAId, Pair.AgentBId, *Vec(Pair.Canonical.RelativeVelocityPoint),
        *Vec(Pair.Canonical.Normal * 32767.0f),
        FMath::RoundToInt(Residual ? Residual->JointHardDeficitCm : 0.0f),
        FMath::RoundToInt(Residual ? Residual->BaselineHardDeficitCm : 0.0f),
        FMath::RoundToInt(Residual ? Residual->JointCanonicalDeficitCmps : 0.0f),
        FMath::RoundToInt(Residual ? Residual->BaselineCanonicalDeficitCmps : 0.0f),
        Index + 1 < Fixture.Pairs.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"environment\":[\n");
    for (int32 Index = 0; Index < Fixture.EnvironmentDiagnostics.Num(); ++Index)
    {
      const FCrowdDemoTransitCapacityEnvironmentDiagnostic& Diagnostic =
        Fixture.EnvironmentDiagnostics[Index];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"joint_obstacle_id\":%d,\"joint_direct_clear\":%s,\"joint_flow_delta_cm\":%d,\"joint_target_violation\":%s,\"baseline_obstacle_id\":%d,\"baseline_direct_clear\":%s,\"baseline_flow_delta_cm\":%d,\"baseline_target_violation\":%s}%s\n"),
        Diagnostic.AgentId,
        Diagnostic.JointCandidateConstraint.SelectedObstacleId,
        Diagnostic.JointCandidateConstraint.bDirectSegmentClear ? TEXT("true") : TEXT("false"),
        FMath::RoundToInt(Diagnostic.JointCandidateConstraint.FlowBoundsReprojectDeltaCm),
        Diagnostic.bJointCandidateTargetViolation ? TEXT("true") : TEXT("false"),
        Diagnostic.BaselineConstraint.SelectedObstacleId,
        Diagnostic.BaselineConstraint.bDirectSegmentClear ? TEXT("true") : TEXT("false"),
        FMath::RoundToInt(Diagnostic.BaselineConstraint.FlowBoundsReprojectDeltaCm),
        Diagnostic.bBaselineTargetViolation ? TEXT("true") : TEXT("false"),
        Index + 1 < Fixture.EnvironmentDiagnostics.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ]\n}\n");
    return Json;
  }


  FString SerializeElasticShadowFailureFixture(
    const FCrowdDemoElasticShadowFailureFixture& Fixture)
  {
    const auto Vec = [](const FVector2f Value)
    {
      return FString::Printf(TEXT("[%d,%d]"),
        FMath::RoundToInt(Value.X), FMath::RoundToInt(Value.Y));
    };
    const auto NormalQ15 = [](const FVector2f Value)
    {
      return FString::Printf(TEXT("[%d,%d]"),
        FMath::RoundToInt(Value.X * 32767.0f),
        FMath::RoundToInt(Value.Y * 32767.0f));
    };
    const auto Obstacle = [](const FCrowdDemoElasticShadowObstacleDiagnostic& D)
    {
      return FString::Printf(
        TEXT("{\"obstacle_id\":%d,\"start\":[%d,%d],\"proposed\":[%d,%d],\"inflated_min\":[%d,%d],\"inflated_max\":[%d,%d],\"entry_t_q10000\":%d,\"exit_t_q10000\":%d,\"start_inside\":%s,\"end_inside\":%s,\"direct_clear\":%s,\"slide_x_clear\":%s,\"slide_y_clear\":%s,\"used_slide_x\":%s,\"used_slide_y\":%s,\"penetrating\":%s,\"clipped\":%s,\"stopped\":%s,\"flow_bounds_delta_cm\":%d,\"constraint_delta_cm\":%d}"),
        D.Constraint.SelectedObstacleId,
        FMath::RoundToInt(D.Constraint.Start.X), FMath::RoundToInt(D.Constraint.Start.Y),
        FMath::RoundToInt(D.Constraint.Proposed.X), FMath::RoundToInt(D.Constraint.Proposed.Y),
        FMath::RoundToInt(D.Constraint.SelectedInflatedMin.X),
        FMath::RoundToInt(D.Constraint.SelectedInflatedMin.Y),
        FMath::RoundToInt(D.Constraint.SelectedInflatedMax.X),
        FMath::RoundToInt(D.Constraint.SelectedInflatedMax.Y),
        FMath::RoundToInt(D.Constraint.SelectedSegmentEntryT * 10000.0f),
        FMath::RoundToInt(D.Constraint.SelectedSegmentExitT * 10000.0f),
        D.Constraint.bStartInsideSelectedObstacle ? TEXT("true") : TEXT("false"),
        D.Constraint.bEndInsideSelectedObstacle ? TEXT("true") : TEXT("false"),
        D.Constraint.bDirectSegmentClear ? TEXT("true") : TEXT("false"),
        D.Constraint.bSlideXClear ? TEXT("true") : TEXT("false"),
        D.Constraint.bSlideYClear ? TEXT("true") : TEXT("false"),
        D.bUsedSlideX ? TEXT("true") : TEXT("false"),
        D.bUsedSlideY ? TEXT("true") : TEXT("false"),
        D.bPenetrating ? TEXT("true") : TEXT("false"),
        D.bClipped ? TEXT("true") : TEXT("false"),
        D.bStopped ? TEXT("true") : TEXT("false"),
        FMath::RoundToInt(D.Constraint.FlowBoundsReprojectDeltaCm),
        FMath::RoundToInt(D.PositionDeltaCm));
    };
    const auto SafetyPolish = [](const FCrowdDemoElasticShadowSafetyPolishSummary& S)
    {
      return FString::Printf(
        TEXT("{\"valid\":%s,\"before_hard\":%d,\"after_hard\":%d,\"applied_pairs\":%d,\"one_sided\":%d,\"obstacle_rejected\":%d,\"target_rejected\":%d,\"before_max_penetration_cm\":%d,\"after_max_penetration_cm\":%d,\"hash\":%u}"),
        S.bValid ? TEXT("true") : TEXT("false"),
        S.BeforeHardPairViolationCount, S.AfterHardPairViolationCount,
        S.AppliedPairCount, S.OneSidedCorrectionCount,
        S.ObstacleRejectedCandidateCount, S.TargetRejectedCandidateCount,
        FMath::RoundToInt(S.BeforeMaximumPenetrationCm),
        FMath::RoundToInt(S.AfterMaximumPenetrationCm), S.StableHash);
    };
    FString Json = FString::Printf(
      TEXT("{\n  \"hash\":%u,\n  \"valid\":%s,\n  \"fixture_too_large\":%s,\n  \"fixed_step\":%d,\n  \"stage\":%d,\n  \"failure_kind\":%d,\n  \"attribution\":%d,\n  \"primary_agent_id\":%d,\n  \"other_agent_id\":%d,\n  \"closure_agent_count\":%d,\n  \"zero_progress_step_max\":%d,\n  \"orca_constraint_epsilon_cmps\":%.6f,\n  \"orca_velocity_quantum_cmps\":%.6f,\n  \"baseline_hash\":%u,\n  \"elastic_hash\":%u,\n  \"orca_replay\":{\"constraint_counts_match\":%s,\"constraints_exactly_match\":%s,\"first_mismatch_index\":%d,\"baseline_inside_speed_circle\":%s,\"baseline_satisfies_elastic\":%s,\"baseline_min_residual_cmps\":%.6f,\"continuous_status\":%d,\"continuous_velocity\":%s,\"quantization_result\":%d,\"quantized_velocity\":%s,\"fallback_stage\":%d},\n  \"agents\":[\n"),
      Fixture.StableHash, Fixture.bValid ? TEXT("true") : TEXT("false"),
      Fixture.bFixtureTooLarge ? TEXT("true") : TEXT("false"),
      Fixture.FixedStepIndex, static_cast<int32>(Fixture.Stage),
      static_cast<int32>(Fixture.FailureKind), static_cast<int32>(Fixture.Attribution),
      Fixture.PrimaryAgentId, Fixture.OtherAgentId, Fixture.ClosureAgentCount,
      Fixture.ZeroProgressStepMax, Fixture.OrcaConstraintEpsilonCmps,
      Fixture.OrcaVelocityQuantumCmps, Fixture.BaselineHash, Fixture.ElasticHash,
      Fixture.OrcaReplay.bConstraintCountsMatch ? TEXT("true") : TEXT("false"),
      Fixture.OrcaReplay.bConstraintsExactlyMatch ? TEXT("true") : TEXT("false"),
      Fixture.OrcaReplay.FirstConstraintMismatchIndex,
      Fixture.OrcaReplay.bBaselineVelocityInsideElasticSpeedCircle ? TEXT("true") : TEXT("false"),
      Fixture.OrcaReplay.bBaselineVelocitySatisfiesElasticConstraints ? TEXT("true") : TEXT("false"),
      Fixture.OrcaReplay.BaselineVelocityMinimumElasticResidualCmps,
      static_cast<int32>(Fixture.OrcaReplay.ElasticContinuousStatus),
      *Vec(Fixture.OrcaReplay.ElasticContinuousVelocity),
      static_cast<int32>(Fixture.OrcaReplay.ElasticQuantizationResult),
      *Vec(Fixture.OrcaReplay.ElasticQuantizedVelocity),
      Fixture.OrcaReplay.ElasticFallbackStage);
    for (int32 AgentIndex = 0; AgentIndex < Fixture.Agents.Num(); ++AgentIndex)
    {
      const FCrowdDemoElasticShadowFixtureAgent& Agent = Fixture.Agents[AgentIndex];
      Json += FString::Printf(
        TEXT("    {\"agent_id\":%d,\"source\":%s,\"start_position\":%s,\"start_velocity\":%s,\"base_preferred\":%s,\"max_speed_cmps\":%d,\"physical_radius_cm\":%d,\"steering_state\":%d,\"baseline_stages\":["),
        Agent.Input.Agent.AgentId,
        Agent.Input.Agent.bTransitSource ? TEXT("true") : TEXT("false"),
        *Vec(Agent.Input.Agent.Position), *Vec(Agent.Input.Agent.Velocity),
        *Vec(Agent.Input.Agent.BasePreferredVelocity),
        FMath::RoundToInt(Agent.Input.Agent.MaxSpeedCmps),
        FMath::RoundToInt(Agent.Input.Agent.PhysicalRadiusCm),
        static_cast<int32>(Agent.Input.SteeringState));
      for (int32 Index = 0; Index < Agent.BaselineStages.Num(); ++Index)
      {
        const auto& Stage = Agent.BaselineStages[Index];
        Json += FString::Printf(TEXT("{\"stage\":%d,\"position\":%s,\"velocity\":%s,\"preferred\":%s,\"hash\":%u}%s"),
          Index, *Vec(Stage.Position), *Vec(Stage.Velocity),
          *Vec(Stage.PreferredVelocity), Stage.StableHash,
          Index + 1 < Agent.BaselineStages.Num() ? TEXT(",") : TEXT(""));
      }
      Json += TEXT("],\"elastic_stages\":[");
      for (int32 Index = 0; Index < Agent.ElasticStages.Num(); ++Index)
      {
        const auto& Stage = Agent.ElasticStages[Index];
        Json += FString::Printf(TEXT("{\"stage\":%d,\"position\":%s,\"velocity\":%s,\"preferred\":%s,\"hash\":%u}%s"),
          Index, *Vec(Stage.Position), *Vec(Stage.Velocity),
          *Vec(Stage.PreferredVelocity), Stage.StableHash,
          Index + 1 < Agent.ElasticStages.Num() ? TEXT(",") : TEXT(""));
      }
      Json += FString::Printf(
        TEXT("],\"baseline_orca\":{\"velocity\":%s,\"feasibility\":%d,\"failure_reason\":%d,\"fallback_stage\":%d,\"infeasible\":%s,\"output_satisfies\":%s,\"stop_satisfies\":%s},\"elastic_orca\":{\"velocity\":%s,\"feasibility\":%d,\"failure_reason\":%d,\"fallback_stage\":%d,\"infeasible\":%s,\"output_satisfies\":%s,\"stop_satisfies\":%s},\"baseline_obstacle\":%s,\"elastic_obstacle\":%s,\"baseline_reproject\":%s,\"elastic_reproject\":%s,\"baseline_constraints\":["),
        *Vec(Agent.BaselineOrcaResult.Velocity),
        static_cast<int32>(Agent.BaselineOrcaResult.Feasibility),
        static_cast<int32>(Agent.BaselineOrcaResult.FailureReason),
        Agent.BaselineOrcaResult.FallbackStage,
        Agent.BaselineOrcaResult.bInfeasible ? TEXT("true") : TEXT("false"),
        Agent.BaselineOrcaResult.bOutputSatisfiesConstraints ? TEXT("true") : TEXT("false"),
        Agent.BaselineOrcaResult.bStopSatisfiesConstraints ? TEXT("true") : TEXT("false"),
        *Vec(Agent.ElasticOrcaResult.Velocity),
        static_cast<int32>(Agent.ElasticOrcaResult.Feasibility),
        static_cast<int32>(Agent.ElasticOrcaResult.FailureReason),
        Agent.ElasticOrcaResult.FallbackStage,
        Agent.ElasticOrcaResult.bInfeasible ? TEXT("true") : TEXT("false"),
        Agent.ElasticOrcaResult.bOutputSatisfiesConstraints ? TEXT("true") : TEXT("false"),
        Agent.ElasticOrcaResult.bStopSatisfiesConstraints ? TEXT("true") : TEXT("false"),
        *Obstacle(Agent.BaselineObstacle), *Obstacle(Agent.ElasticObstacle),
        *Obstacle(Agent.BaselineReproject), *Obstacle(Agent.ElasticReproject));
      for (int32 Index = 0; Index < Agent.BaselineConstraints.Num(); ++Index)
      {
        const auto& Constraint = Agent.BaselineConstraints[Index];
        Json += FString::Printf(TEXT("{\"other_agent_id\":%d,\"point\":%s,\"normal\":%s,\"kind\":%d,\"order\":%d}%s"),
          Constraint.OtherAgentId, *Vec(Constraint.Point), *NormalQ15(Constraint.Normal),
          static_cast<int32>(Constraint.Kind), Constraint.StableConstraintOrder,
          Index + 1 < Agent.BaselineConstraints.Num() ? TEXT(",") : TEXT(""));
      }
      Json += TEXT("],\"elastic_constraints\":[");
      for (int32 Index = 0; Index < Agent.ElasticConstraints.Num(); ++Index)
      {
        const auto& Constraint = Agent.ElasticConstraints[Index];
        Json += FString::Printf(TEXT("{\"other_agent_id\":%d,\"point\":%s,\"normal\":%s,\"kind\":%d,\"order\":%d}%s"),
          Constraint.OtherAgentId, *Vec(Constraint.Point), *NormalQ15(Constraint.Normal),
          static_cast<int32>(Constraint.Kind), Constraint.StableConstraintOrder,
          Index + 1 < Agent.ElasticConstraints.Num() ? TEXT(",") : TEXT(""));
      }
      Json += FString::Printf(TEXT("]}%s\n"),
        AgentIndex + 1 < Fixture.Agents.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"baseline_pbd_iterations\":[\n");
    for (int32 Iteration = 0; Iteration < Fixture.BaselinePbdIterations.Num(); ++Iteration)
    {
      const auto& Diagnostic = Fixture.BaselinePbdIterations[Iteration];
      Json += FString::Printf(TEXT("    {\"iteration\":%d,\"hash\":%u,\"pair_corrections\":["),
        Diagnostic.IterationIndex, Diagnostic.StableHash);
      for (int32 PairIndex = 0; PairIndex < Diagnostic.PairCorrections.Num(); ++PairIndex)
      {
        const auto& Pair = Diagnostic.PairCorrections[PairIndex];
        Json += FString::Printf(TEXT("{\"min_agent_id\":%d,\"max_agent_id\":%d,\"correction_cm\":%d}%s"),
          Pair.MinAgentId, Pair.MaxAgentId, FMath::RoundToInt(Pair.PairCorrectionCm),
          PairIndex + 1 < Diagnostic.PairCorrections.Num() ? TEXT(",") : TEXT(""));
      }
      Json += FString::Printf(TEXT("]}%s\n"),
        Iteration + 1 < Fixture.BaselinePbdIterations.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ],\n  \"elastic_pbd_iterations\":[\n");
    for (int32 Iteration = 0; Iteration < Fixture.ElasticPbdIterations.Num(); ++Iteration)
    {
      const auto& Diagnostic = Fixture.ElasticPbdIterations[Iteration];
      Json += FString::Printf(TEXT("    {\"iteration\":%d,\"hash\":%u,\"pair_corrections\":["),
        Diagnostic.IterationIndex, Diagnostic.StableHash);
      for (int32 PairIndex = 0; PairIndex < Diagnostic.PairCorrections.Num(); ++PairIndex)
      {
        const auto& Pair = Diagnostic.PairCorrections[PairIndex];
        Json += FString::Printf(TEXT("{\"min_agent_id\":%d,\"max_agent_id\":%d,\"correction_cm\":%d}%s"),
          Pair.MinAgentId, Pair.MaxAgentId, FMath::RoundToInt(Pair.PairCorrectionCm),
          PairIndex + 1 < Diagnostic.PairCorrections.Num() ? TEXT(",") : TEXT(""));
      }
      Json += FString::Printf(TEXT("]}%s\n"),
        Iteration + 1 < Fixture.ElasticPbdIterations.Num() ? TEXT(",") : TEXT(""));
    }
    Json += FString::Printf(
      TEXT("  ],\n  \"baseline_safety_polish\":%s,\n  \"elastic_safety_polish\":%s,\n  \"stage_summaries\":[\n"),
      *SafetyPolish(Fixture.BaselineSafetyPolish),
      *SafetyPolish(Fixture.ElasticSafetyPolish));
    for (int32 Index = 0; Index < Fixture.ElasticStageSummaries.Num(); ++Index)
    {
      const auto& B = Fixture.BaselineStageSummaries[Index];
      const auto& E = Fixture.ElasticStageSummaries[Index];
      Json += FString::Printf(
        TEXT("    {\"stage\":%d,\"baseline_hard\":%d,\"elastic_hard\":%d,\"baseline_target\":%d,\"elastic_target\":%d,\"baseline_source_q15\":%d,\"elastic_source_q15\":%d}%s\n"),
        Index, B.HardPairViolationCount, E.HardPairViolationCount,
        B.TargetViolationCount, E.TargetViolationCount,
        B.SourceForwardRatioQ15, E.SourceForwardRatioQ15,
        Index + 1 < Fixture.ElasticStageSummaries.Num() ? TEXT(",") : TEXT(""));
    }
    Json += TEXT("  ]\n}\n");
    return Json;
  }

  FVector ComputeRoundCohortCenter(TConstArrayView<FCrowdDemoRoundAgentState> Agents)
  {
    if (Agents.IsEmpty())
    {
      return FVector(0.0f, -2500.0f, 60.0f);
    }

    FVector Center = FVector::ZeroVector;
    for (const FCrowdDemoRoundAgentState& Agent : Agents)
    {
      Center += FVector(Agent.Location);
    }
    Center /= static_cast<float>(Agents.Num());
    Center.Z = 60.0f;
    return Center;
  }

  float ComputeCoordinatorP95(TArray<float> Samples)
  {
    if (Samples.IsEmpty())
    {
      return -1.0f;
    }

    Samples.Sort();
    const int32 Index = FMath::Clamp(FMath::CeilToInt(static_cast<float>(Samples.Num()) * 0.95f) - 1, 0, Samples.Num() - 1);
    return Samples[Index];
  }

  FCrowdDemoRoundResultHeader MakeRoundResultHeader(const FCrowdDemoRoundResultPacket& Packet)
  {
    FCrowdDemoRoundResultHeader Header;
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
    Header.SeparationAppliedAgentCount = Packet.SeparationAppliedAgentCount;
    Header.SeparationGridCellCount = Packet.SeparationGridCellCount;
    Header.ObstaclePenetrationCount = Packet.ObstaclePenetrationCount;
    Header.ArrivalCount = Packet.ArrivalCount;
    Header.PbdCorrectedAgentCount = Packet.PbdCorrectedAgentCount;
    Header.PbdCorrectedPairCount = Packet.PbdCorrectedPairCount;
    Header.PbdMaxPairCorrectionCm = Packet.PbdMaxPairCorrectionCm;
    Header.PbdMaxAgentTotalCorrectionCm = Packet.PbdMaxAgentTotalCorrectionCm;
    Header.PbdMaxObstacleReprojectDeltaCm = Packet.PbdMaxObstacleReprojectDeltaCm;
    Header.PbdMaxFinalSafetyDeltaCm = Packet.PbdMaxFinalSafetyDeltaCm;
    Header.PbdSolverMsP95 = Packet.PbdSolverMsP95;
    Header.TrafficMetrics = Packet.TrafficMetrics;
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
      {
        continue;
      }
      if (!Candidate->IsLocalVisualHostOnly())
      {
        return Candidate;
      }
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
  DOREPLIFETIME(ACrowdDemoRoundSimCoordinator, RoundBootstrapPacket);
  DOREPLIFETIME(ACrowdDemoRoundSimCoordinator, CurrentRoundPlan);
  DOREPLIFETIME(ACrowdDemoRoundSimCoordinator, RoundResultHeader);
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

void ACrowdDemoRoundSimCoordinator::OnRep_RoundBootstrapPacket()
{
  if (HasAuthority() || RoundBootstrapPacket.bValid == 0)
  {
    return;
  }

  if (UWorld* World = GetWorld())
  {
    if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
    {
      Pipeline->QueueBootstrap(RoundBootstrapPacket);
      Pipeline->QueueRoundPlan(CurrentRoundPlan);
    }
  }
}

void ACrowdDemoRoundSimCoordinator::OnRep_CurrentRoundPlan()
{
  QueueClientRoundPlan(CurrentRoundPlan);
}

void ACrowdDemoRoundSimCoordinator::OnRep_RoundResultHeader()
{
  if (HasAuthority() || RoundResultHeader.bValid == 0)
  {
    return;
  }
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
    TEXT("CrowdDemoRoundResultTransport role=client stage=header_received round_id=%d checkpoint_revision=%d state_frame_revision=%d agents=%d header_received_count=%d source=RoundSim"),
    RoundResultHeader.RoundId,
    RoundResultHeader.CheckpointRevision,
    RoundResultHeader.StateFrameRevision,
    RoundResultHeader.AgentCount,
    RoundResultHeaderReceivedCount);
  TryProcessClientCorrectionAssemblies();
}

void ACrowdDemoRoundSimCoordinator::MulticastCorrectionFrameChunk_Implementation(const FCrowdDemoCorrectionFrameChunk& Chunk)
{
  if (HasAuthority())
  {
    return;
  }

  CacheClientCorrectionChunk(Chunk);
}

void ACrowdDemoRoundSimCoordinator::MulticastRoundPlan_Implementation(const FCrowdDemoRoundPlanPacket& Plan)
{
  if (!HasAuthority())
  {
    QueueClientRoundPlan(Plan);
  }
}

void ACrowdDemoRoundSimCoordinator::MulticastProjectileVisualEvents_Implementation(
  const TArray<FCrowdDemoProjectileVisualEvent>& Events)
{
  UWorld* World = GetWorld();
  if (!World || World->GetNetMode() == NM_DedicatedServer || Events.IsEmpty())
  {
    return;
  }
  if (ACrowdDemoReplicator* VisualHost = FindProjectileVisualHost(*World))
  {
    VisualHost->ApplyProjectileVisualEvents(Events);
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


  TArray<FCrowdDemoProjectileVisualEvent> ProjectileVisualEvents;
  if (Pipeline->DequeueProjectileVisualEvents(ProjectileVisualEvents))
  {
    MulticastProjectileVisualEvents(ProjectileVisualEvents);
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

  FlushServerCorrectionChunks();
  if (PendingServerCorrectionChunks.IsEmpty())
  {
    PublishServerCorrectionFrame();
  }

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
  if (!World || RoundBootstrapPacket.bValid == 0)
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
    TEXT("CrowdDemoRoundInit role=server round_id=%d revision=%d previous_checkpoint_revision=%d agents=%d start_server_time=%.3f duration=%.3f fixed_step=%.4f scenario=%d source=RoundPlan"),
    Plan.RoundId,
    Plan.Revision,
    Plan.PreviousCheckpointRevision,
    RoundBootstrapPacket.Agents.Num(),
    Plan.StartServerTimeSeconds,
    Plan.DurationSeconds,
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
    TEXT("CrowdDemoRoundPlan role=server round_id=%d revision=%d previous_checkpoint_revision=%d start_server_time=%.3f duration=%.3f agents=%d action=published source=RoundPlan"),
    Plan.RoundId,
    Plan.Revision,
    Plan.PreviousCheckpointRevision,
    Plan.StartServerTimeSeconds,
    Plan.DurationSeconds,
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
  RoundResultHeader = MakeRoundResultHeader(RoundResultPacket);
  ++RoundResultBuiltCount;
  ++RoundResultHeaderPublishedCount;
  bRoundResultPublished = true;
  ++CompletedRoundCount;
  RefreshLastCompareCounters();
  LastRoundCompletedWorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
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
    TEXT("CrowdDemoRoundCheckpoint role=server round_id=%d revision=%d checkpoint_revision=%d completed_round_count=%d agents=%d initial_overlap_pair_count=%d overlap_pair_count=%d overlap_reduction=%d initial_severe_overlap_pair_count=%d severe_overlap_pair_count=%d severe_overlap_reduction=%d separation_applied_agents=%d separation_grid_cells=%d pbd_corrected_agent_count=%d pbd_corrected_pair_count=%d pbd_max_pair_correction_cm=%.3f pbd_max_agent_total_correction_cm=%.3f pbd_max_obstacle_reproject_delta_cm=%.3f pbd_max_final_safety_delta_cm=%.3f pbd_solver_ms_p95=%.3f obstacle_penetration_count=%d arrival_count=%d end_server_time=%.3f source=RoundSim"),
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
    RoundResultPacket.SeparationAppliedAgentCount,
    RoundResultPacket.SeparationGridCellCount,
    RoundResultPacket.PbdCorrectedAgentCount,
    RoundResultPacket.PbdCorrectedPairCount,
    RoundResultPacket.PbdMaxPairCorrectionCm,
    RoundResultPacket.PbdMaxAgentTotalCorrectionCm,
    RoundResultPacket.PbdMaxObstacleReprojectDeltaCm,
    RoundResultPacket.PbdMaxFinalSafetyDeltaCm,
    RoundResultPacket.PbdSolverMsP95,
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
      RoundResultPacket.TrafficMetrics.SharedFlowFieldBuildHash,
      Pipeline->GetSharedFlowField().ValidDirectedEdgeCount,
      RoundResultPacket.TrafficMetrics.FlowRecoveredFromRasterMismatchCount,
      RoundResultPacket.TrafficMetrics.FlowDesiredSegmentHardObstacleViolationCount,
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
        TEXT("CrowdDemoT4Checkpoint role=server round_id=%d valid=%d layout_hash=%u flow_hash=%u progress_hash=%u wall_passed=%d corridor_exited=%d completed=%d final_deadlock=%d unreachable_samples=%d last_step=%d completion_step_max=%d source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bT4Valid, Particle.T4LayoutHash,
        Particle.T4FlowHash, Particle.T4ProgressHash,
        Particle.T4WallPassedCount, Particle.T4CorridorExitedCount,
        Particle.T4CompletedCount, Particle.T4FinalDeadlockAgentCount,
        Particle.T4UnreachableSampleCount, Particle.T4LastFixedStep,
        Particle.T4CompletionStepMax);
    }
    if (Particle.T6TransitLayoutHash != 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT6TransitCheckpoint role=server round_id=%d valid=%d layout_hash=%u flow_hash=%u progress_hash=%u capability_profiles=%d membership_hash=%u wall_passed=%d corridor_exited=%d completed=%d final_deadlock=%d unreachable_samples=%d cross_profile_hard=%d cross_profile_swept=%d last_step=%d completion_step_max=%d source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bT6TransitValid,
        Particle.T6TransitLayoutHash, Particle.T6TransitFlowHash,
        Particle.T6TransitProgressHash, Particle.CapabilityProfileCount,
        Particle.CapabilityMembershipHash, Particle.T6TransitWallPassedCount,
        Particle.T6TransitCorridorExitedCount, Particle.T6TransitCompletedCount,
        Particle.T6TransitFinalDeadlockAgentCount,
        Particle.T6TransitUnreachableSampleCount,
        Particle.CrossProfileHardViolationCount,
        Particle.CrossProfileSweptViolationCount,
        Particle.T6TransitLastFixedStep, Particle.T6TransitCompletionStepMax);
    }
    if (Pipeline->GetRules().TargetInfluenceSettings.bEnabled != 0
      && Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTargetInfluenceCheckpoint role=server round_id=%d valid=%d agents=%d inside_band=%d outside_max=%d inside_min=%d radial_error_cm_p50=%.3f radial_error_cm_p95=%.3f radial_error_cm_max=%.3f relative_speed_cmps_p95=%.3f follow_lag_cm_p95=%.3f occupied_angular_sector_count=%d angular_coverage_q15=%d max_angular_sector_population=%d occupied_radial_band_count=%d target_influence_hash=%u source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.bTargetInfluenceValid,
        Particle.TargetInfluenceAgentCount, Particle.TargetInsideEffectiveBandCount,
        Particle.TargetOutsideMaxCount, Particle.TargetInsideMinCount,
        Particle.TargetRadialErrorCmP50, Particle.TargetRadialErrorCmP95,
        Particle.TargetRadialErrorCmMax, Particle.TargetRelativeSpeedCmpsP95,
        Particle.TargetFollowLagCmP95, Particle.OccupiedAngularSectorCount,
        Particle.AngularCoverageQ15, Particle.MaxAngularSectorPopulation,
        Particle.OccupiedRadialBandCount, Particle.TargetInfluenceHash);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTargetDensityCheckpoint role=server round_id=%d field_hash=%u contributing=%d occupied_cells=%d max_cell_population=%d guided=%d clockwise=%d counter_clockwise=%d tangential_speed_cmps_p95=%.3f tangential_speed_cmps_max=%.3f occupied_angular_sectors=%d max_angular_sector_population=%d largest_empty_sector_run=%d source=MassPipeline"),
        RoundResultPacket.RoundId, Particle.TargetDensityFieldHash,
        Particle.TargetDensityContributingAgentCount,
        Particle.TargetDensityOccupiedCellCount,
        Particle.TargetDensityMaxCellPopulation,
        Particle.TargetDensityGuidedAgentCount,
        Particle.TargetDensityClockwiseAgentCount,
        Particle.TargetDensityCounterClockwiseAgentCount,
        Particle.TargetDensityTangentialSpeedCmpsP95,
        Particle.TargetDensityTangentialSpeedCmpsMax,
        Particle.OccupiedAngularSectorCount,
        Particle.MaxAngularSectorPopulation,
        Particle.TargetLargestEmptySectorRun);
      if (Pipeline->IsTargetInfluenceExecutionDiagnosticEnabled())
      {
        const auto& Diagnostic =
          Pipeline->GetLastCompletedTargetInfluenceExecutionSummary();
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetInfluenceExecutionDiagnostic role=server round_id=%d valid=%d samples=%d requested_agents=%d below_threshold=%d requested_p50=%.3f requested_p95=%.3f requested_max=%.3f predict_p50=%.3f predict_p95=%.3f predict_max=%.3f applied_p50=%.3f applied_p95=%.3f applied_max=%.3f ratio_p50=%.3f ratio_p95=%.3f lost_p50=%.3f lost_p95=%.3f lost_max=%.3f flip_agents=%d flips=%d sector_transitions=%d band_transitions=%d environment_opposed=%d particle_opposed=%d occupied_feasible=%d occupied_infeasible=%d feasible_unoccupied=%d largest_empty_feasible_run=%d flow_bounds_infeasible=%d obstacle_infeasible=%d hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, Diagnostic.bValid ? 1 : 0,
          Diagnostic.ValidSampleCount, Diagnostic.RequestedAgentCount,
          Diagnostic.RequestedBelowThresholdSampleCount,
          Diagnostic.RequestedTangentialCmpsP50,
          Diagnostic.RequestedTangentialCmpsP95,
          Diagnostic.RequestedTangentialCmpsMax,
          Diagnostic.MovementPredictTangentialCmpsP50,
          Diagnostic.MovementPredictTangentialCmpsP95,
          Diagnostic.MovementPredictTangentialCmpsMax,
          Diagnostic.AppliedTangentialCmpsP50,
          Diagnostic.AppliedTangentialCmpsP95,
          Diagnostic.AppliedTangentialCmpsMax,
          Diagnostic.RequestedToAppliedRatioP50,
          Diagnostic.RequestedToAppliedRatioP95,
          Diagnostic.LostTangentialCmpsP50,
          Diagnostic.LostTangentialCmpsP95,
          Diagnostic.LostTangentialCmpsMax,
          Diagnostic.DirectionFlipAgentCount, Diagnostic.DirectionFlipCount,
          Diagnostic.AngularSectorTransitionCount,
          Diagnostic.RadialBandTransitionCount,
          Diagnostic.EnvironmentOpposedAgentCount,
          Diagnostic.ParticleOpposedAgentCount,
          Diagnostic.Environment.OccupiedFeasibleSectorCount,
          Diagnostic.Environment.OccupiedInfeasiblePolarCellCount,
          Diagnostic.Environment.FeasibleButUnoccupiedSectorCount,
          Diagnostic.Environment.LargestEmptyFeasibleSectorRun,
          Diagnostic.Environment.FlowBoundsInfeasibleCellCount,
          Diagnostic.Environment.ObstacleInfeasibleCellCount,
          Diagnostic.DiagnosticHash);
        if (!Diagnostic.bValid || Diagnostic.ValidSampleCount <= 0
          || Diagnostic.RequestedAgentCount <= 0)
        {
          UE_LOG(LogTemp, Error,
            TEXT("CrowdDemoTargetInfluenceExecutionDiagnostic role=server round_id=%d valid=%d samples=%d requested_agents=%d zero_sample_or_request=1 VIOLATION"),
            RoundResultPacket.RoundId, Diagnostic.bValid ? 1 : 0,
            Diagnostic.ValidSampleCount, Diagnostic.RequestedAgentCount);
        }

        FString OutputPath;
        const bool bHasOutputPath = FParse::Value(FCommandLine::Get(),
          TEXT("CrowdDemoTargetInfluenceExecutionDiagnosticOutput="), OutputPath);
        bool bWritten = false;
        if (bHasOutputPath && !OutputPath.IsEmpty())
        {
          FString Json = FString::Printf(
            TEXT("{\n  \"contract_version\":1,\n  \"round_id\":%d,\n  \"valid\":%s,\n  \"diagnostic_hash\":%u,\n  \"requested_tangential_cmps\":{\"p50\":%.3f,\"p95\":%.3f,\"max\":%.3f},\n  \"predict_tangential_cmps\":{\"p50\":%.3f,\"p95\":%.3f,\"max\":%.3f},\n  \"applied_tangential_cmps\":{\"p50\":%.3f,\"p95\":%.3f,\"max\":%.3f},\n  \"lost_tangential_cmps\":{\"p50\":%.3f,\"p95\":%.3f,\"max\":%.3f},\n  \"requested_to_applied_ratio\":{\"p50\":%.6f,\"p95\":%.6f},\n  \"direction_flips\":%d,\n  \"sector_transitions\":%d,\n  \"radial_band_transitions\":%d,\n  \"environment_opposed_agents\":%d,\n  \"particle_opposed_agents\":%d,\n"),
            RoundResultPacket.RoundId, Diagnostic.bValid ? TEXT("true") : TEXT("false"),
            Diagnostic.DiagnosticHash,
            Diagnostic.RequestedTangentialCmpsP50, Diagnostic.RequestedTangentialCmpsP95,
            Diagnostic.RequestedTangentialCmpsMax,
            Diagnostic.MovementPredictTangentialCmpsP50,
            Diagnostic.MovementPredictTangentialCmpsP95,
            Diagnostic.MovementPredictTangentialCmpsMax,
            Diagnostic.AppliedTangentialCmpsP50, Diagnostic.AppliedTangentialCmpsP95,
            Diagnostic.AppliedTangentialCmpsMax,
            Diagnostic.LostTangentialCmpsP50, Diagnostic.LostTangentialCmpsP95,
            Diagnostic.LostTangentialCmpsMax,
            Diagnostic.RequestedToAppliedRatioP50,
            Diagnostic.RequestedToAppliedRatioP95,
            Diagnostic.DirectionFlipCount, Diagnostic.AngularSectorTransitionCount,
            Diagnostic.RadialBandTransitionCount,
            Diagnostic.EnvironmentOpposedAgentCount,
            Diagnostic.ParticleOpposedAgentCount);
          Json += TEXT("  \"feasible_sectors_by_radial_band\":[");
          for (int32 Index = 0;
            Index < Diagnostic.Environment.FeasibleSectorCountByRadialBand.Num(); ++Index)
          {
            Json += FString::FromInt(
              Diagnostic.Environment.FeasibleSectorCountByRadialBand[Index]);
            if (Index + 1 < Diagnostic.Environment.FeasibleSectorCountByRadialBand.Num())
              Json += TEXT(",");
          }
          Json += TEXT("],\n  \"agents\":[\n");
          const auto& Runtime =
            Pipeline->GetLastCompletedTargetInfluenceExecutionRuntime();
          for (int32 Index = 0; Index < Runtime.Agents.Num(); ++Index)
          {
            const auto& Agent = Runtime.Agents[Index];
            const double Ratio = Agent.RequestedTangentialCmpsQ > 0
              ? static_cast<double>(Agent.AppliedSameDirectionCmpsQ)
                / static_cast<double>(Agent.RequestedTangentialCmpsQ) : 0.0;
            Json += FString::Printf(
              TEXT("    {\"agent_id\":%d,\"samples\":%d,\"requested_samples\":%d,\"requested_sum_q\":%lld,\"predict_sum_q\":%lld,\"applied_same_direction_sum_q\":%lld,\"lost_sum_q\":%lld,\"requested_to_applied_ratio\":%.6f,\"environment_opposed_sum_q\":%lld,\"particle_opposed_sum_q\":%lld,\"direction_flips\":%d,\"sector_transitions\":%d,\"band_transitions\":%d,\"last_band\":%d,\"last_sector\":%d,\"last_direction\":%d,\"last_weights\":[%d,%d,%d],\"last_cell_feasible\":%s}%s\n"),
              Agent.AgentId, Agent.ValidSampleCount, Agent.RequestedSampleCount,
              Agent.RequestedTangentialCmpsQ, Agent.PredictedTangentialCmpsQ,
              Agent.AppliedSameDirectionCmpsQ, Agent.LostTangentialCmpsQ, Ratio,
              Agent.EnvironmentOpposedCmpsQ, Agent.ParticleOpposedCmpsQ,
              Agent.DirectionFlipCount, Agent.AngularSectorTransitionCount,
              Agent.RadialBandTransitionCount, Agent.LastRadialBand,
              Agent.LastAngularSector, Agent.LastDirectionSign,
              Agent.LastLeftWeight, Agent.LastCurrentWeight, Agent.LastRightWeight,
              Agent.bLastCellFeasible ? TEXT("true") : TEXT("false"),
              Index + 1 < Runtime.Agents.Num() ? TEXT(",") : TEXT(""));
          }
          Json += TEXT("  ],\n  \"environment_cells\":[\n");
          for (int32 Index = 0; Index < Diagnostic.Environment.Cells.Num(); ++Index)
          {
            const auto& Cell = Diagnostic.Environment.Cells[Index];
            Json += FString::Printf(
              TEXT("    {\"band\":%d,\"sector\":%d,\"feasible\":%s,\"occupied\":%s,\"flow_bounds_blocked\":%s,\"obstacle_blocked\":%s}%s\n"),
              Cell.RadialBandIndex, Cell.AngularSectorIndex,
              Cell.bFeasible ? TEXT("true") : TEXT("false"),
              Cell.bOccupied ? TEXT("true") : TEXT("false"),
              Cell.bFlowBoundsBlocked ? TEXT("true") : TEXT("false"),
              Cell.bObstacleBlocked ? TEXT("true") : TEXT("false"),
              Index + 1 < Diagnostic.Environment.Cells.Num() ? TEXT(",") : TEXT(""));
          }
          Json += TEXT("  ]\n}\n");
          IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
          PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutputPath));
          bWritten = FFileHelper::SaveStringToFile(Json, *OutputPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetInfluenceExecutionDiagnosticFile role=server round_id=%d output_path=%d written=%d hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, bHasOutputPath ? 1 : 0, bWritten ? 1 : 0,
          Diagnostic.DiagnosticHash);
        if (!bHasOutputPath || !bWritten)
          UE_LOG(LogTemp, Error,
            TEXT("CrowdDemoTargetInfluenceExecutionDiagnosticFile role=server round_id=%d write_failed=1 VIOLATION"),
            RoundResultPacket.RoundId);
      }
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
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoTargetApproachCheckpoint role=server round_id=%d valid=%d target_fact_hash=%u target_approach_hash=%u agent_input_hash=%u fine_kinematic_hash=%u agent_config_hash=%u temporal_hash=%u settings_hash=%u slot_input_hash=%u full_input_hash=%u owner_state_hash=%u transition_hash=%u guidance_hash=%u guidance_location_hash=%u guidance_velocity_hash=%u ring_entered=%d ring_waiting=%d functional_capacity=%d functional_occupied=%d fill_capacity=%d fill_occupied=%d slot_ingress=%d slot_occupied=%d free_settle=%d free_settled=%d duplicate_owner=%d invalid_owner=%d state_transitions=%d source=MassPipeline"),
      RoundResultPacket.RoundId, Particle.bTargetApproachValid,
      Particle.TargetFactHash, Particle.TargetApproachHash,
      Particle.TargetAgentInputHash, Particle.TargetAgentFineKinematicHash,
      Particle.TargetAgentConfigHash, Particle.TargetAgentTemporalHash,
      Particle.TargetSettingsHash, Particle.TargetSlotInputHash,
      Particle.TargetFullInputHash, Particle.TargetOwnerStateHash,
      Particle.TargetTransitionHash, Particle.TargetGuidanceHash,
      Particle.TargetGuidanceLocationHash, Particle.TargetGuidanceVelocityHash,
      Particle.RingEnteredCount, Particle.RingWaitingCount,
      Particle.FunctionalSlotCapacity, Particle.FunctionalSlotOccupied,
      Particle.FillSlotCapacity, Particle.FillSlotOccupied,
      Particle.SlotIngressCount, Particle.SlotOccupiedCount,
      Particle.FreeSettleCount, Particle.FreeSettledCount,
      Particle.DuplicateSlotOwnerCount, Particle.InvalidSlotOwnerCount,
      Particle.TargetApproachStateTransitionCount);
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoTargetSlotLayout role=server round_id=%d revision=%d topology_hash=%u world_hash=%u full_input_hash=%u candidates=%d functional=%d fill=%d rejected_target=%d rejected_pair=%d rejected_obstacle=%d rejected_bounds=%d rejected_unreachable=%d rejected_ingress=%d schedule_hash=%u commit_hash=%u owner_release=%d owner_reused=%d owner_conflict=%d revision_mismatch=%d source=MassPipeline"),
      RoundResultPacket.RoundId, Particle.TargetSlotLayoutRevision,
      Particle.TargetSlotLayoutTopologyHash, Particle.TargetSlotLayoutWorldHash,
      Particle.TargetSlotLayoutFullInputHash, Particle.SlotLayoutCandidateCount,
      Particle.SlotLayoutFunctionalCount, Particle.SlotLayoutFillCount,
      Particle.SlotRejectedTargetClearanceCount, Particle.SlotRejectedPairSpacingCount,
      Particle.SlotRejectedObstacleCount, Particle.SlotRejectedBoundsCount,
      Particle.SlotRejectedUnreachableCount, Particle.SlotRejectedIngressSegmentCount,
      Particle.TargetApproachScheduleHash, Particle.TargetApproachCommitHash,
      Particle.SlotOwnerReleaseCount, Particle.SlotOwnerReusedCount,
      Particle.SlotOwnerConflictCount, Particle.SlotLayoutRevisionMismatchCount);
    const FCrowdDemoTrafficMetrics& FlowV2 = RoundResultPacket.TrafficMetrics;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoFlowV2Metrics role=server round_id=%d anchors=%d connections=%d safe_intervals=%d internal_edges=%d directed_edges=%d center_invalid_connected=%d source_attachment_success=%d goal_attachments=%d navigation_unreachable_samples=%d navigation_v2_hash=%u source=MassPipeline"),
      RoundResultPacket.RoundId, FlowV2.NavigationCenterAnchorCount,
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
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoSf2Checkpoint role=server round_id=%d agents=%d initial_overlap_pair_count=%d overlap_pair_count_p50=%.3f overlap_pair_count_p95=%.3f overlap_pair_count_max=%d severe_overlap_pair_count_p50=%.3f severe_overlap_pair_count_p95=%.3f severe_overlap_pair_count_max=%d soft_separation_applied_agent_count=%d pbd_corrected_agent_count=%d pbd_corrected_pair_count=%d pbd_max_pair_correction_cm=%.3f pbd_max_agent_total_correction_cm=%.3f pbd_max_obstacle_reproject_delta_cm=%.3f pbd_max_final_safety_delta_cm=%.3f pbd_solver_ms_p95=%.3f flow_goal_reached_count=%d flow_corridor_exit_count=%d corridor_deadlock_agent_count=%d server_obstacle_penetration_count=%d client_sim_obstacle_penetration_count=%d sim_position_error_cm_p95=%.3f correction_frame_applied_count=%d source=MassPipeline"),
      RoundResultPacket.RoundId,
      RoundResultPacket.Agents.Num(),
      Metrics.InitialOverlapPairCount,
      Metrics.OverlapPairCountP50,
      Metrics.OverlapPairCountP95,
      Metrics.OverlapPairCountMax,
      Metrics.SevereOverlapPairCountP50,
      Metrics.SevereOverlapPairCountP95,
      Metrics.SevereOverlapPairCountMax,
      Metrics.SoftSeparationAppliedAgentCount,
      Metrics.PbdCorrectedAgentCount,
      Metrics.PbdCorrectedPairCount,
      Metrics.PbdMaxPairCorrectionCm,
      Metrics.PbdMaxAgentTotalCorrectionCm,
      Metrics.PbdMaxObstacleReprojectDeltaCm,
      Metrics.PbdMaxFinalSafetyDeltaCm,
      Metrics.PbdSolverMsP95,
      Metrics.FlowGoalReachedCount,
      Metrics.FlowCorridorExitCount,
      Metrics.CorridorDeadlockAgentCount,
      Metrics.ServerObstaclePenetrationCount,
      Metrics.ClientSimObstaclePenetrationCount,
      Metrics.SimPositionErrorCmP95,
      Pipeline->GetLastCorrectionMetrics().CorrectionFrameAppliedCount);
  }
  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic
    || Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning)
  {
    const FCrowdDemoRoundCompareMetrics& Metrics = Pipeline->GetLastCompletedRoundMetrics();
    const FCrowdDemoTrafficMetrics& Traffic = RoundResultPacket.TrafficMetrics;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3Checkpoint role=server round_id=%d agents=%d traffic_hash=%u portal_hash=%u orca_hash=%u agent_state_hash=%u portals=%d queue_p95=%.3f queue_max=%d admissions_granted=%d admissions_denied=%d reservation_timeouts=%d transit_timeouts=%d capacity_violations=%d band_reassignments=%d direction_epoch_changes=%d orca_neighbors_p95=%.3f orca_infeasible=%d orca_fallback_stop=%d orca_solver_ms_p95=%.3f overlap_p50=%.3f overlap_p95=%.3f overlap_max=%d severe_p50=%.3f severe_p95=%.3f severe_max=%d residual_pbd_penetration_pairs=%d final_obstacle_penetrations=%d goal=%d corridor=%d deadlock=%d obstacle_penetration=%d source=MassPipeline"),
      RoundResultPacket.RoundId, RoundResultPacket.Agents.Num(), Traffic.TrafficFieldHash,
      Traffic.PortalDecisionHash, Traffic.OrcaVelocityHash, Traffic.AgentStateHash,
      Traffic.TrafficPortalCount, Traffic.TrafficPortalQueueCountP95,
      Traffic.TrafficPortalQueueCountMax, Traffic.TrafficAdmissionGrantedCount,
      Traffic.TrafficAdmissionDeniedCount, Traffic.ReservationTimeoutCount,
      Traffic.TransitTimeoutCount, Traffic.PortalCapacityViolationCount,
      Traffic.TrafficBandReassignmentCount,
      Traffic.TrafficDirectionEpochChangeCount, Traffic.OrcaNeighborCountP95,
      Traffic.OrcaInfeasibleAgentCount, Traffic.OrcaFallbackStopCount,
      Traffic.OrcaSolverMsP95, Metrics.OverlapPairCountP50,
      Metrics.OverlapPairCountP95, Metrics.OverlapPairCountMax,
      Metrics.SevereOverlapPairCountP50, Metrics.SevereOverlapPairCountP95,
      Metrics.SevereOverlapPairCountMax, Traffic.ResidualPbdPenetrationPairCount,
      Traffic.FinalObstaclePenetrationCount, Metrics.FlowGoalReachedCount,
      Metrics.FlowCorridorExitCount, Metrics.CorridorDeadlockAgentCount,
      Metrics.ServerObstaclePenetrationCount);
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf3TrafficDetail role=server round_id=%d raw_portal_candidates=%d extracted_portals=%d binds=%d rebinds=%d releases=%d invalid_side=%d wrong_span=%d granted=%d denied=%d reserved_to_inside=%d inside_to_exited=%d reservation_timeouts=%d transit_timeouts=%d zero_throughput_steps=%d capacity_violations=%d holding_targets=%d holding_failures=%d holding_overlaps=%d band_error_p50=%.3f band_error_p95=%.3f band_error_max=%.3f reserved_positive_axial=%d reserved_zero_velocity=%d waiting_infeasible=%d approach_infeasible=%d reserved_infeasible=%d inside_infeasible=%d waiting_fallback_stop=%d approach_fallback_stop=%d reserved_fallback_stop=%d inside_fallback_stop=%d stop_satisfies=%d stop_violates=%d source=MassPipeline"),
      RoundResultPacket.RoundId, Traffic.RawPortalCandidateCount, Traffic.ExtractedPortalCount,
      Traffic.PortalBindCount, Traffic.PortalRebindCount, Traffic.PortalReleaseCount,
      Traffic.InvalidSideCandidateCount, Traffic.WrongSpanCandidateCount,
      Traffic.TrafficAdmissionGrantedCount, Traffic.TrafficAdmissionDeniedCount,
      Traffic.ReservedToInsideCount, Traffic.InsideToExitedCount,
      Traffic.ReservationTimeoutCount, Traffic.TransitTimeoutCount,
      Traffic.PortalZeroThroughputStepCount, Traffic.PortalCapacityViolationCount,
      Traffic.HoldingTargetCount, Traffic.HoldingTargetAllocationFailureCount,
      Traffic.HoldingTargetOverlapCount, Traffic.BandLateralErrorP50,
      Traffic.BandLateralErrorP95, Traffic.BandLateralErrorMax,
      Traffic.ReservedPositiveAxialVelocityCount, Traffic.ReservedZeroVelocityCount,
      Traffic.WaitingOrcaInfeasibleCount, Traffic.ApproachOrcaInfeasibleCount,
      Traffic.ReservedOrcaInfeasibleCount, Traffic.InsideOrcaInfeasibleCount,
      Traffic.WaitingOrcaFallbackStopCount, Traffic.ApproachOrcaFallbackStopCount,
      Traffic.ReservedOrcaFallbackStopCount, Traffic.InsideOrcaFallbackStopCount,
      Traffic.OrcaStopSatisfiesConstraintCount, Traffic.OrcaStopViolatesConstraintCount);
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4PriorityOrca role=server round_id=%d hash=%u equal_pairs=%d asymmetric_pairs=%d high_side_25=%d low_side_75=%d responsibility_sum_violations=%d yieldable_stable=%d yieldable_reserve=%d hard_conflict_held=%d source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.PriorityOrcaHash,
        Traffic.PriorityOrcaEqualPairCount, Traffic.PriorityOrcaAsymmetricPairCount,
        Traffic.PriorityOrcaHighSide25Count, Traffic.PriorityOrcaLowSide75Count,
        Traffic.PriorityOrcaResponsibilitySumViolationCount,
        Traffic.CommitGateYieldableStableConflictCount,
        Traffic.CommitGateYieldableReserveConflictCount,
        Traffic.CommitGateHardConflictHeldCount);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4PhysicalSatisfaction role=server round_id=%d physically_satisfied=%d commit_preferred_nonzero_orca_zero=%d commit_route_forward_p50=%.3f commit_route_forward_p95=%.3f stable_displaced=%d stable_displacement_p95=%.3f stable_displacement_max=%.3f reserve_displaced=%d reserve_displacement_p95=%.3f reserve_displacement_max=%.3f source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.PhysicallySatisfiedPositionCount,
        Traffic.CommitPreferredNonzeroOrcaZeroCount,
        Traffic.CommitRouteForwardSpeedCmpsP50,
        Traffic.CommitRouteForwardSpeedCmpsP95,
        Traffic.StablePhysicalDisplacedCount,
        Traffic.StablePhysicalDisplacementCmP95,
        Traffic.StablePhysicalDisplacementCmMax,
        Traffic.ReservePhysicalDisplacedCount,
        Traffic.ReservePhysicalDisplacementCmP95,
        Traffic.ReservePhysicalDisplacementCmMax);
      if (Pipeline->IsTransitCapacityShadowEnabled())
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTransitCapacityShadow role=server round_id=%d components=%d max_component=%d component_2=%d component_5=%d component_8=%d component_12=%d component_20=%d oversize=%d solved=%d infeasible=%d hard_infeasible=%d iteration_limit=%d clearance_failed=%d no_forward_gain=%d invalid_input=%d numerical=%d quantized=%d yielding=%d direct_relevant=%d hard_closure=%d legacy_output_hard=%d joint_candidate_hard=%d baseline_hard=%d legacy_output_obstacle=%d legacy_output_flow=%d legacy_output_target=%d joint_candidate_obstacle=%d joint_candidate_flow=%d joint_candidate_target=%d baseline_obstacle=%d baseline_flow=%d baseline_target=%d pair_double_owner=%d forward_ratio_q15=%d spacing_deficit_cm=%.3f aperture_deficit_cm=%.3f legacy_output_clearance_cm=%.3f joint_candidate_clearance_cm=%.3f baseline_clearance_cm=%.3f max_yield_cm=%.3f solver_ms_p95=%.3f fixture_agents=%d fixture_pairs=%d fixture_status=%d fixture_hash=%u hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, Traffic.TransitCapacityShadowComponentCount,
          Traffic.TransitCapacityShadowMaximumComponentSize,
          Traffic.TransitCapacityShadowComponent2Count,
          Traffic.TransitCapacityShadowComponent5Count,
          Traffic.TransitCapacityShadowComponent8Count,
          Traffic.TransitCapacityShadowComponent12Count,
          Traffic.TransitCapacityShadowComponent20Count,
          Traffic.TransitCapacityShadowOversizeCount,
          Traffic.TransitCapacityShadowSolvedCount,
          Traffic.TransitCapacityShadowInfeasibleCount,
          Traffic.TransitCapacityShadowHardInfeasibleCount,
          Traffic.TransitCapacityShadowIterationLimitCount,
          Traffic.TransitCapacityShadowClearanceNotAchievedCount,
          Traffic.TransitCapacityShadowNoForwardGainCount,
          Traffic.TransitCapacityShadowInvalidInputCount,
          Traffic.TransitCapacityShadowNumericalFailureCount,
          Traffic.TransitCapacityShadowQuantizedFailureCount,
          Traffic.TransitCapacityShadowYieldingAgentCount,
          Traffic.TransitCapacityShadowDirectRelevantAgentCount,
          Traffic.TransitCapacityShadowHardSafetyClosureAgentCount,
          Traffic.TransitCapacityShadowHardPairViolationCount,
          Traffic.TransitCapacityShadowJointCandidateHardPairViolationCount,
          Traffic.TransitCapacityShadowBaselineHardPairViolationCount,
          Traffic.TransitCapacityShadowObstacleViolationCount,
          Traffic.TransitCapacityShadowFlowBoundsViolationCount,
          Traffic.TransitCapacityShadowTargetViolationCount,
          Traffic.TransitCapacityShadowJointCandidateObstacleViolationCount,
          Traffic.TransitCapacityShadowJointCandidateFlowBoundsViolationCount,
          Traffic.TransitCapacityShadowJointCandidateTargetViolationCount,
          Traffic.TransitCapacityShadowBaselineObstacleViolationCount,
          Traffic.TransitCapacityShadowBaselineFlowBoundsViolationCount,
          Traffic.TransitCapacityShadowBaselineTargetViolationCount,
          Traffic.TransitCapacityShadowPairDoubleOwnerCount,
          Traffic.TransitCapacityShadowForwardSpeedRatioQ15,
          Traffic.TransitCapacityShadowPreferredSpacingDeficitCmMax,
          Traffic.TransitCapacityShadowApertureDeficitCmMax,
          Traffic.TransitCapacityShadowClearanceDeficitCmMax,
          Traffic.TransitCapacityShadowJointCandidateClearanceDeficitCmMax,
          Traffic.TransitCapacityShadowBaselineClearanceDeficitCmMax,
          Traffic.TransitCapacityShadowMaximumYieldDisplacementCm,
          Traffic.TransitCapacityShadowSolverMsP95,
          Traffic.TransitCapacityFailureFixtureAgentCount,
          Traffic.TransitCapacityFailureFixturePairCount,
          Traffic.TransitCapacityFailureFixtureStatus,
          Traffic.TransitCapacityFailureFixtureHash,
          Traffic.TransitCapacityShadowHash);
        const FCrowdDemoTransitCapacityFailureFixture& FailureFixture =
          Pipeline->GetTransitCapacityFailureFixture();
        FString OutputPath;
        const bool bHasOutputPath = FParse::Value(FCommandLine::Get(),
          TEXT("CrowdDemoTransitCapacityFixtureOutput="), OutputPath);
        bool bWritten = false;
        if (FailureFixture.bValid && bHasOutputPath && !OutputPath.IsEmpty())
        {
          IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
          PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutputPath));
          bWritten = FFileHelper::SaveStringToFile(
            SerializeTransitCapacityFailureFixture(FailureFixture), *OutputPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTransitCapacityFailureFixture role=server round_id=%d valid=%d agents=%d pairs=%d status=%d output_path=%d written=%d hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, FailureFixture.bValid ? 1 : 0,
          FailureFixture.Agents.Num(), FailureFixture.Pairs.Num(),
          static_cast<int32>(FailureFixture.Result.Status),
          bHasOutputPath ? 1 : 0, bWritten ? 1 : 0, FailureFixture.StableHash);
        if (FailureFixture.bValid && bHasOutputPath && !bWritten)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoTransitCapacityFailureFixture role=server round_id=%d write_failed=1 hash=%u"),
            RoundResultPacket.RoundId, FailureFixture.StableHash);
        }
      }
      if (Pipeline->IsElasticCrowdShadowEnabled())
      {
        const FCrowdDemoElasticShadowFailureFixture& FailureFixture =
          Pipeline->GetElasticCrowdFailureFixture();
        FString OutputPath;
        const bool bHasOutputPath = FParse::Value(FCommandLine::Get(),
          TEXT("CrowdDemoElasticCrowdFixtureOutput="), OutputPath);
        bool bWritten = false;
        if (FailureFixture.bValid && bHasOutputPath && !OutputPath.IsEmpty())
        {
          IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
          PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutputPath));
          bWritten = FFileHelper::SaveStringToFile(
            SerializeElasticShadowFailureFixture(FailureFixture), *OutputPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoElasticCrowdFailureFixture role=server round_id=%d valid=%d agents=%d step=%d stage=%d kind=%d attribution=%d too_large=%d zero_progress_steps=%d output_path=%d written=%d hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, FailureFixture.bValid ? 1 : 0,
          FailureFixture.ClosureAgentCount, FailureFixture.FixedStepIndex,
          static_cast<int32>(FailureFixture.Stage),
          static_cast<int32>(FailureFixture.FailureKind),
          static_cast<int32>(FailureFixture.Attribution),
          FailureFixture.bFixtureTooLarge ? 1 : 0,
          FailureFixture.ZeroProgressStepMax, bHasOutputPath ? 1 : 0,
          bWritten ? 1 : 0, FailureFixture.StableHash);
        if (FailureFixture.bValid && bHasOutputPath && !bWritten)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoElasticCrowdFailureFixture role=server round_id=%d write_failed=1 hash=%u"),
            RoundResultPacket.RoundId, FailureFixture.StableHash);
        }
      }
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoTransitCapacitySelection role=server round_id=%d applied=%d position=%d holding=%d position_deficit=%d holding_deficit=%d hash=%u source=RoundResult"),
        RoundResultPacket.RoundId, Traffic.bTransitCapacitySelectionApplied,
        Traffic.TransitCapacityPositionCount, Traffic.TransitCapacityHoldingCount,
        Traffic.TransitCapacityPositionDeficit, Traffic.TransitCapacityHoldingDeficit,
        Traffic.TransitCapacitySelectionHash);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4Positioning role=server round_id=%d candidates=%d front_capacity=%d reserve_capacity=%d assigned=%d unassigned=%d stable_occupied=%d reserve_hold=%d reused=%d changed=%d churn=%d invalidated=%d promotion_transitions=%d promotion_agents=%d front_admission_grants=%d front_admission_requeues=%d front_admission_hash=%u phase_reservation_requests=%d phase_reservation_granted=%d phase_reservation_held=%d phase_reservation_invalid=%d phase_reservation_target_reject=%d phase_reservation_route_conflict=%d phase_reservation_transitions=%d phase_reservation_held_steps_p95=%.3f phase_reservation_hash=%u phase_reservation_client_hash_match=%d wait_unique_requests=%d wait_unique_blockers=%d wait_edges=%d wait_reciprocal=%d wait_cycles=%d wait_max_cycle=%d wait_stalled_blockers=%d wait_progressing_blockers=%d wait_stale_owners=%d wait_blocker_radial=%d wait_blocker_angular=%d wait_blocker_radial_commit=%d wait_atomic_cycles=%d wait_atomic_max_set=%d wait_graph_hash=%u wait_graph_client_hash_match=%d wait_fixture_hash=%u wait_fixture_agents=%d wait_fixture_edges=%d arrival_error_p95=%.3f candidate_overlap=%d candidate_unreachable=%d candidate_hash=%u assignment_hash=%u target_hash=%u source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.PositionCandidateCount,
        Traffic.PositionFrontCapacity, Traffic.PositionReserveCapacity,
        Traffic.PositionAssignedCount, Traffic.PositionUnassignedCount,
        Traffic.PositionStableOccupiedCount, Traffic.PositionReserveHoldCount,
        Traffic.PositionAssignmentReusedCount, Traffic.PositionAssignmentChangedCount,
        Traffic.PositionAssignmentChurnCount, Traffic.PositionInvalidatedCount,
        Traffic.PositionPromotionTransitionCount, Traffic.PositionPromotionAgentCount,
        Traffic.PositionFrontAdmissionGrantCount, Traffic.PositionFrontAdmissionRequeueCount,
        Traffic.PositionFrontAdmissionDecisionHash,
        Traffic.PhaseReservationRequestCount, Traffic.PhaseReservationGrantedCount,
        Traffic.PhaseReservationHeldCount, Traffic.PhaseReservationInvalidCount,
        Traffic.PhaseReservationTargetExclusionRejectCount,
        Traffic.PhaseReservationRouteConflictCount,
        Traffic.PhaseReservationTransitionCount, Traffic.PhaseReservationHeldStepsP95,
        Traffic.PhaseReservationDecisionHash, Traffic.PhaseReservationClientHashMatch,
        Traffic.PhaseReservationUniqueBlockedRequestCount,
        Traffic.PhaseReservationUniqueBlockerCount,
        Traffic.PhaseReservationWaitEdgeCount,
        Traffic.PhaseReservationReciprocalEdgeCount,
        Traffic.PhaseReservationCycleCount, Traffic.PhaseReservationMaxCycleSize,
        Traffic.PhaseReservationStalledBlockerCount,
        Traffic.PhaseReservationProgressingBlockerCount,
        Traffic.PhaseReservationStaleOwnerCount,
        Traffic.PhaseReservationBlockerRadialCount,
        Traffic.PhaseReservationBlockerAngularCount,
        Traffic.PhaseReservationBlockerRadialCommitCount,
        Traffic.PhaseReservationAtomicHandoffCycleCount,
        Traffic.PhaseReservationMaxAtomicHandoffSetSize,
        Traffic.PhaseReservationWaitGraphHash,
        Traffic.PhaseReservationWaitGraphClientHashMatch,
        Traffic.PhaseReservationWaitGraphFixtureHash,
        Traffic.PhaseReservationWaitGraphFixtureAgentCount,
        Traffic.PhaseReservationWaitGraphFixtureEdgeCount,
        Traffic.PositionArrivalErrorCmP95,
        Traffic.PositionCandidateOverlapCount, Traffic.PositionCandidateUnreachableCount,
        Traffic.PositionCandidateHash, Traffic.PositionAssignmentHash, Traffic.TargetFactHash);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4SteeringFirst role=server round_id=%d holding_candidates=%d compatibility_edges=%d holding_assigned=%d holding_arrived=%d holding_failures=%d selected_compatibility_valid=%d selected_compatibility_invalid=%d duplicate_compatibility_keys=%d commit_requests=%d commit_granted=%d commit_held=%d commit_invalid=%d invalid_position=%d target_revision_mismatch=%d compatibility_missing=%d compatibility_rejected=%d states=%d,%d,%d,%d,%d,%d holding_releases=%d commit_releases=%d ghost_owners=%d no_progress=%d hashes=%u,%u,%u,%u source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.HoldingCandidateCount,
        Traffic.HoldingCompatibilityEdgeCount, Traffic.HoldingAssignedAgentCount,
        Traffic.HoldingArrivedAgentCount, Traffic.HoldingAllocationFailureCount,
        Traffic.HoldingSelectedCompatibilityValidCount,
        Traffic.HoldingSelectedCompatibilityInvalidCount,
        Traffic.HoldingDuplicateCompatibilityKeyCount,
        Traffic.CommitRequestCount, Traffic.CommitGrantedCount, Traffic.CommitHeldCount,
        Traffic.CommitInvalidCount, Traffic.CommitInvalidPositionCount,
        Traffic.CommitTargetRevisionMismatchCount, Traffic.CommitCompatibilityMissingCount,
        Traffic.CommitCompatibilityRejectedCount, Traffic.SteeringStatePursuitCount,
        Traffic.SteeringStateHoldingCount, Traffic.SteeringStateCommitCount,
        Traffic.SteeringStateStableCount, Traffic.SteeringStateReserveCount,
        Traffic.SteeringStateReacquireCount, Traffic.HoldingReleaseCount,
        Traffic.CommitReleaseCount, Traffic.GhostOwnerCount,
        Traffic.PositioningNoProgressAgentCount, Traffic.HoldingCandidateHash,
        Traffic.HoldingAssignmentHash, Traffic.CommitDecisionHash,
        Traffic.SteeringStateHash);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4ResidualCapacity role=server round_id=%d unfinished=%d remaining_positions=%d compatible_edges=%d matching=%d no_stable=%d no_reserve=%d without_holding=%d without_position_edge=%d without_commit_route=%d stable_reject=%d reserve_reject=%d target_reject=%d obstacle_reject=%d flow_reject=%d revision_reject=%d best_single_gain=%d critical_blockers=%d target_limited=%d geometry_limited=%d hash=%u source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.ResidualUnfinishedAgentCount,
        Traffic.ResidualRemainingPositionCount, Traffic.ResidualCompatibleEdgeCount,
        Traffic.ResidualMaximumMatchingCount, Traffic.ResidualNoStableMatching,
        Traffic.ResidualNoReserveMatching, Traffic.ResidualAgentWithoutHoldingCount,
        Traffic.ResidualAgentWithoutPositionEdgeCount,
        Traffic.ResidualAgentWithoutCommitRouteCount,
        Traffic.ResidualStableBlockerEdgeRejectCount,
        Traffic.ResidualReserveBlockerEdgeRejectCount, Traffic.ResidualTargetRejectCount,
        Traffic.ResidualObstacleRejectCount, Traffic.ResidualFlowRejectCount,
        Traffic.ResidualRevisionRejectCount, Traffic.ResidualBestSingleBlockerRemovalGain,
        Traffic.ResidualBlockerCriticalCount, Traffic.ResidualTargetLimitedCount,
        Traffic.ResidualGeometryLimitedCount, Traffic.ResidualCapacityHash);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4HoldingMatching role=server round_id=%d position_valid=%d greedy=%d matching=%d joint=%d hash=%u source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.ResidualPositionValidCount,
        Traffic.ResidualGreedyHoldingCount, Traffic.ResidualHoldingMatchingCount,
        Traffic.ResidualJointFeasibleCount, Traffic.ResidualHoldingMatchingHash);
      if (RoundResultPacket.RoundId == 1)
      {
        const FCrowdDemoHoldingHallFixture& Fixture = Pipeline->GetLastCompletedHoldingHallFixture();
        const FCrowdDemoHoldingHallSummary& Hall = Fixture.Summary;
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf4HoldingHall role=server round_id=%d valid=%d exact=%d current=%d owner_release_stable=%d owner_release_reserve=%d owner_release_commit=%d physical_remove_stable=%d physical_remove_reserve=%d hall_agents=%d available_holdings=%d minimum_deficiency=%d full_deficiency=%d missing_record=%d flow_reject=%d target_reject=%d obstacle_reject=%d revision_reject=%d stable_owner_reject=%d reserve_owner_reject=%d fixture_hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, Hall.bValid ? 1 : 0, Hall.bExact ? 1 : 0,
          Hall.CurrentMatchingCount, Hall.OwnerReleaseStableMatchingCount,
          Hall.OwnerReleaseReserveMatchingCount, Hall.OwnerReleaseCommitMatchingCount,
          Hall.PhysicalStableBlockerRemovalMatchingCount,
          Hall.PhysicalReserveBlockerRemovalMatchingCount,
          Hall.HallAgentCount, Hall.HallAvailableHoldingCount, Hall.HallDeficiency,
          Hall.FullHallDeficiency,
          Hall.MissingCompatibilityRecordCount, Hall.FlowRejectCount,
          Hall.TargetRejectCount, Hall.ObstacleRejectCount, Hall.RevisionRejectCount,
          Hall.StableOwnerRejectCount, Hall.ReserveOwnerRejectCount, Hall.StableHash);
        FString AbsoluteLogPath;
        FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrowdDemo"));
        if (FParse::Value(FCommandLine::Get(), TEXT("AbsLog="), AbsoluteLogPath)
          && !AbsoluteLogPath.IsEmpty())
          OutputDirectory = FPaths::GetPath(AbsoluteLogPath.TrimQuotes());
        const FString OutputPath = FPaths::Combine(OutputDirectory,
          TEXT("sf4_holding_hall_fixture.json"));
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.CreateDirectoryTree(*OutputDirectory);
        const bool bWritten = FFileHelper::SaveStringToFile(
          SerializeSf4HoldingHallFixture(Fixture), *OutputPath,
          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (bWritten)
        {
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoSf4HoldingHallFixture role=server round_id=%d written=1 path=%s fixture_hash=%u"),
            RoundResultPacket.RoundId, *OutputPath, Hall.StableHash);
        }
        else
        {
          UE_LOG(LogTemp, Warning,
            TEXT("CrowdDemoSf4HoldingHallFixture role=server round_id=%d written=0 path=%s fixture_hash=%u"),
            RoundResultPacket.RoundId, *OutputPath, Hall.StableHash);
        }
        const FCrowdDemoHallGeometryFixture& Geometry =
          Pipeline->GetLastCompletedHallGeometryFixture();
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf4HallGeometry role=server round_id=%d valid=%d agent=%d position=%d holdings=%d best_holding=%d best_margin_cm=%.3f best_blocker=%d nonnegative_margin_holdings=%d target_only=%d stable_only=%d multi_label=%d self=%d witness_position=%d duplicate=%d stale=%d radius_error=%d endpoint=%d formal_mismatch=%d fixture_hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, Geometry.bValid ? 1 : 0, Geometry.AgentId,
          Geometry.PositionId, Geometry.HoldingCandidateCount, Geometry.BestHoldingId,
          Geometry.BestClearanceMarginCm, Geometry.BestBlockerAgentId,
          Geometry.NonNegativeMarginHoldingCount, Geometry.TargetOnlyRejectCount,
          Geometry.StableOnlyRejectCount, Geometry.MultiLabelRejectCount,
          Geometry.SelfBlockerCount, Geometry.BlockerUsesWitnessPositionCount,
          Geometry.DuplicateBlockerCount, Geometry.StaleBlockerCount,
          Geometry.RadiusSemanticsErrorCount, Geometry.EndpointContactCount,
          Geometry.FormalClassificationMismatchCount, Geometry.FixtureHash);
        const FString GeometryOutputPath = FPaths::Combine(OutputDirectory,
          TEXT("sf4_hall_geometry_fixture.json"));
        const bool bGeometryWritten = FFileHelper::SaveStringToFile(
          SerializeSf4HallGeometryFixture(Geometry), *GeometryOutputPath,
          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf4HallGeometryFixture role=server round_id=%d written=%d path=%s fixture_hash=%u"),
          RoundResultPacket.RoundId, bGeometryWritten ? 1 : 0,
          *GeometryOutputPath, Geometry.FixtureHash);
        const FCrowdDemoJointPositioningResult& Joint =
          Pipeline->GetLastCompletedJointPositioningResult();
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf4JointPositioning role=server round_id=%d valid=%d maximum=%d hard_locked=%d reused=%d unmatched=%d duplicate_holdings=%d duplicate_positions=%d hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, Joint.bValid?1:0, Joint.MaximumCardinality,
          Joint.HardLockedCount, Joint.ReusedCombinationCount, Joint.UnmatchedAgentCount,
          Joint.DuplicateHoldingCount, Joint.DuplicatePositionCount, Joint.StableHash);
        const FString JointOutputPath = FPaths::Combine(OutputDirectory,
          TEXT("sf4_joint_positioning_fixture.json"));
        const bool bJointWritten = FFileHelper::SaveStringToFile(
          SerializeSf4JointPositioningResult(Joint), *JointOutputPath,
          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf4JointPositioningFixture role=server round_id=%d written=%d path=%s hash=%u"),
          RoundResultPacket.RoundId,bJointWritten?1:0,*JointOutputPath,Joint.StableHash);
        const FCrowdDemoJointCommitResidualResult& Residual=
          Pipeline->GetLastCompletedJointCommitResidualResult();
        UE_LOG(LogTemp,Display,
          TEXT("CrowdDemoSf4JointCommitResidual role=server round_id=%d valid=%d candidates=%d feasible=%d infeasible=%d hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId,Residual.bValid?1:0,Residual.CandidateCount,
          Residual.FeasibleCount,Residual.InfeasibleCount,Residual.StableHash);
        const FString ResidualOutputPath=FPaths::Combine(OutputDirectory,
          TEXT("sf4_joint_commit_residual_fixture.json"));
        FFileHelper::SaveStringToFile(SerializeSf4JointCommitResidualResult(Residual),
          *ResidualOutputPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
      }
      const auto MetricAt = [](const TArray<float>& Values, const int32 Index)
      {
        return Values.IsValidIndex(Index) ? Values[Index] : 0.0f;
      };
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4SteeringStateDiagnostic role=server round_id=%d pursuit_outside_handoff=%d pursuit_invalid_flow=%d holding_distance_not_ready=%d holding_speed_not_ready=%d holding_ready_conflict=%d holding_ready_granted=%d commit_reject_target=%d commit_reject_flow=%d commit_reject_obstacle=%d commit_reject_stable=%d commit_reject_reserve=%d commit_conflict_active=%d commit_conflict_selected=%d distance_p50=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f distance_p95=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f preferred_forward_p50=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f orca_forward_p50=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f final_forward_p50=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.PursuitOutsideHandoffCount,
        Traffic.PursuitInvalidFlowCount, Traffic.HoldingFinalDistanceNotReadyCount,
        Traffic.HoldingFinalSpeedNotReadyCount, Traffic.HoldingFinalReadyConflictCount,
        Traffic.HoldingFinalReadyGrantedCount, Traffic.HoldingFinalTargetRejectCount,
        Traffic.HoldingFinalFlowRejectCount, Traffic.HoldingFinalObstacleRejectCount,
        Traffic.HoldingFinalStableBlockerRejectCount,
        Traffic.HoldingFinalReserveBlockerRejectCount,
        Traffic.HoldingFinalActiveCommitConflictCount,
        Traffic.HoldingFinalSelectedConflictCount,
        MetricAt(Traffic.SteeringStateDistanceCmP50,0), MetricAt(Traffic.SteeringStateDistanceCmP50,1),
        MetricAt(Traffic.SteeringStateDistanceCmP50,2), MetricAt(Traffic.SteeringStateDistanceCmP50,3),
        MetricAt(Traffic.SteeringStateDistanceCmP50,4), MetricAt(Traffic.SteeringStateDistanceCmP50,5),
        MetricAt(Traffic.SteeringStateDistanceCmP95,0), MetricAt(Traffic.SteeringStateDistanceCmP95,1),
        MetricAt(Traffic.SteeringStateDistanceCmP95,2), MetricAt(Traffic.SteeringStateDistanceCmP95,3),
        MetricAt(Traffic.SteeringStateDistanceCmP95,4), MetricAt(Traffic.SteeringStateDistanceCmP95,5),
        MetricAt(Traffic.SteeringStatePreferredForwardCmpsP50,0), MetricAt(Traffic.SteeringStatePreferredForwardCmpsP50,1),
        MetricAt(Traffic.SteeringStatePreferredForwardCmpsP50,2), MetricAt(Traffic.SteeringStatePreferredForwardCmpsP50,3),
        MetricAt(Traffic.SteeringStatePreferredForwardCmpsP50,4), MetricAt(Traffic.SteeringStatePreferredForwardCmpsP50,5),
        MetricAt(Traffic.SteeringStateOrcaForwardCmpsP50,0), MetricAt(Traffic.SteeringStateOrcaForwardCmpsP50,1),
        MetricAt(Traffic.SteeringStateOrcaForwardCmpsP50,2), MetricAt(Traffic.SteeringStateOrcaForwardCmpsP50,3),
        MetricAt(Traffic.SteeringStateOrcaForwardCmpsP50,4), MetricAt(Traffic.SteeringStateOrcaForwardCmpsP50,5),
        MetricAt(Traffic.SteeringStateFinalForwardCmpsP50,0), MetricAt(Traffic.SteeringStateFinalForwardCmpsP50,1),
        MetricAt(Traffic.SteeringStateFinalForwardCmpsP50,2), MetricAt(Traffic.SteeringStateFinalForwardCmpsP50,3),
        MetricAt(Traffic.SteeringStateFinalForwardCmpsP50,4), MetricAt(Traffic.SteeringStateFinalForwardCmpsP50,5));
      const auto CountAt = [](const TArray<int32>& Values, const int32 Index)
      {
        return Values.IsValidIndex(Index) ? Values[Index] : 0;
      };
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4SteeringOrcaSource role=server round_id=%d pursuit_constraints_from=%d,%d,%d,%d,%d,%d holding_constraints_from=%d,%d,%d,%d,%d,%d infeasible_by_state=%d,%d,%d,%d,%d,%d fallback_stop_by_state=%d,%d,%d,%d,%d,%d source=MassPipeline"),
        RoundResultPacket.RoundId,
        CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,0), CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,1),
        CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,2), CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,3),
        CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,4), CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,5),
        CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,6), CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,7),
        CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,8), CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,9),
        CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,10), CountAt(Traffic.SteeringStateOrcaConstraintSourceMatrix,11),
        CountAt(Traffic.SteeringStateOrcaInfeasibleCounts,0), CountAt(Traffic.SteeringStateOrcaInfeasibleCounts,1),
        CountAt(Traffic.SteeringStateOrcaInfeasibleCounts,2), CountAt(Traffic.SteeringStateOrcaInfeasibleCounts,3),
        CountAt(Traffic.SteeringStateOrcaInfeasibleCounts,4), CountAt(Traffic.SteeringStateOrcaInfeasibleCounts,5),
        CountAt(Traffic.SteeringStateOrcaFallbackStopCounts,0), CountAt(Traffic.SteeringStateOrcaFallbackStopCounts,1),
        CountAt(Traffic.SteeringStateOrcaFallbackStopCounts,2), CountAt(Traffic.SteeringStateOrcaFallbackStopCounts,3),
        CountAt(Traffic.SteeringStateOrcaFallbackStopCounts,4), CountAt(Traffic.SteeringStateOrcaFallbackStopCounts,5));
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4StateTail role=server round_id=%d reacquire_reasons_none_target_position_holding_compat_owner_noprogress=%d,%d,%d,%d,%d,%d,%d commit_arrival_error_p95=%.3f commit_no_progress_steps_max=%d commit_obstacle_correction_p95=%.3f commit_pbd_correction_p95=%.3f source=MassPipeline"),
        RoundResultPacket.RoundId,
        CountAt(Traffic.SteeringReacquireReasonCounts,0), CountAt(Traffic.SteeringReacquireReasonCounts,1),
        CountAt(Traffic.SteeringReacquireReasonCounts,2), CountAt(Traffic.SteeringReacquireReasonCounts,3),
        CountAt(Traffic.SteeringReacquireReasonCounts,4), CountAt(Traffic.SteeringReacquireReasonCounts,5),
        CountAt(Traffic.SteeringReacquireReasonCounts,6), Traffic.SteeringCommitArrivalErrorCmP95,
        Traffic.SteeringCommitNoProgressStepsMax, Traffic.SteeringCommitObstacleCorrectionCmP95,
        Traffic.SteeringCommitPbdCorrectionCmP95);
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4SteeringStateDiagnosticP95 role=server round_id=%d preferred_forward_p95=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f orca_forward_p95=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f final_forward_p95=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f source=MassPipeline"),
        RoundResultPacket.RoundId,
        MetricAt(Traffic.SteeringStatePreferredForwardCmpsP95,0), MetricAt(Traffic.SteeringStatePreferredForwardCmpsP95,1),
        MetricAt(Traffic.SteeringStatePreferredForwardCmpsP95,2), MetricAt(Traffic.SteeringStatePreferredForwardCmpsP95,3),
        MetricAt(Traffic.SteeringStatePreferredForwardCmpsP95,4), MetricAt(Traffic.SteeringStatePreferredForwardCmpsP95,5),
        MetricAt(Traffic.SteeringStateOrcaForwardCmpsP95,0), MetricAt(Traffic.SteeringStateOrcaForwardCmpsP95,1),
        MetricAt(Traffic.SteeringStateOrcaForwardCmpsP95,2), MetricAt(Traffic.SteeringStateOrcaForwardCmpsP95,3),
        MetricAt(Traffic.SteeringStateOrcaForwardCmpsP95,4), MetricAt(Traffic.SteeringStateOrcaForwardCmpsP95,5),
        MetricAt(Traffic.SteeringStateFinalForwardCmpsP95,0), MetricAt(Traffic.SteeringStateFinalForwardCmpsP95,1),
        MetricAt(Traffic.SteeringStateFinalForwardCmpsP95,2), MetricAt(Traffic.SteeringStateFinalForwardCmpsP95,3),
        MetricAt(Traffic.SteeringStateFinalForwardCmpsP95,4), MetricAt(Traffic.SteeringStateFinalForwardCmpsP95,5));
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4UnsettledDiagnostic role=server round_id=%d slot_commit=%d reserve_commit=%d portal_owned=%d outside_compose_range=%d guidance_active=%d arrival_speed_rejected=%d error_le30=%d error_31_100=%d error_101_300=%d error_over300=%d previous_orca_fallback=%d previous_orca_infeasible=%d previous_pbd_corrected=%d speed_p95=%.3f guidance_speed_p95=%.3f orca_speed_p95=%.3f obstacle_speed_p95=%.3f orca_adjusted=%d orca_zero=%d obstacle_hit=%d orca_constraint_p95=%.3f source=MassPipeline"),
        RoundResultPacket.RoundId, Traffic.PositionSlotCommitCount,
        Traffic.PositionReserveCommitCount, Traffic.PositionUnsettledPortalOwnedCount,
        Traffic.PositionUnsettledOutsideComposeRangeCount,
        Traffic.PositionUnsettledGuidanceActiveCount,
        Traffic.PositionUnsettledArrivalSpeedRejectedCount,
        Traffic.PositionUnsettledErrorLe30Count,
        Traffic.PositionUnsettledError31To100Count,
        Traffic.PositionUnsettledError101To300Count,
        Traffic.PositionUnsettledErrorOver300Count,
        Traffic.PositionUnsettledPreviousOrcaFallbackCount,
        Traffic.PositionUnsettledPreviousOrcaInfeasibleCount,
        Traffic.PositionUnsettledPreviousPbdCorrectedCount,
        Traffic.PositionUnsettledSpeedCmpsP95,
        Traffic.PositionUnsettledGuidanceSpeedCmpsP95,
        Traffic.PositionUnsettledOrcaSpeedCmpsP95,
        Traffic.PositionUnsettledObstacleSpeedCmpsP95,
        Traffic.PositionUnsettledOrcaAdjustedCount,
        Traffic.PositionUnsettledOrcaZeroCount,
        Traffic.PositionUnsettledObstacleHitCount,
        Traffic.PositionUnsettledOrcaConstraintP95);
#if WITH_DEV_AUTOMATION_TESTS
      static const bool bIngressDiagnostic = FParse::Param(
        FCommandLine::Get(), TEXT("CrowdDemoSf4IngressDiagnostic"));
      if (bIngressDiagnostic)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf4IngressDiagnostic role=server round_id=%d slot_commit_count=%d slot_commit_error_over_300_count=%d direct_path_target_blocked_count=%d direct_path_stable_blocked_count=%d direct_path_reserve_blocked_count=%d direct_path_commit_blocked_count=%d stable_blocker_pair_count=%d reserve_blocker_pair_count=%d commit_blocker_pair_count=%d assigned_sector_delta_p50=%.3f assigned_sector_delta_p95=%.3f assigned_sector_delta_max=%.3f assigned_radial_delta_p50=%.3f assigned_radial_delta_p95=%.3f assigned_radial_delta_max=%.3f unblocked_alternative_front_count=%d same_side_alternative_front_count=%d no_alternative_front_count=%d orca_constraints_from_stable_count=%d orca_constraints_from_reserve_count=%d orca_constraints_from_commit_count=%d orca_constraints_from_other_count=%d slot_commit_preferred_speed_p95=%.3f slot_commit_orca_speed_p95=%.3f slot_commit_obstacle_speed_p95=%.3f slot_commit_final_speed_p95=%.3f slot_commit_low_speed_steps_max=%d target_exclusion_crossing_count=%d ingress_order_inversion_count=%d pbd_push_away_count=%d obstacle_push_away_count=%d waiting=%d radial_stage=%d angular_align=%d radial_commit=%d gate_invalid=%d radial_commit_blocked=%d route_hash=%u radial_preferred_speed_p95=%.3f radial_orca_speed_p95=%.3f radial_final_speed_p95=%.3f radial_orca_forward_p50=%.3f radial_orca_forward_min=%.3f radial_final_forward_p50=%.3f radial_final_forward_min=%.3f radial_constraint_p95=%.3f radial_constraint_active=%d radial_constraint_waiting=%d radial_constraint_reserve_commit=%d radial_constraint_stable=%d radial_constraint_other=%d radial_error_p50=%.3f radial_error_p95=%.3f radial_error_max=%.3f radial_error_improved=%d radial_quantized_stall=%d compose_boundary_switches=%d minimum_fixture_hash=%u minimum_fixture_constraint_count=%d evaluation_hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId,
          Traffic.PositionIngressSlotCommitCount, Traffic.PositionIngressErrorOver300Count,
          Traffic.PositionIngressTargetBlockedCount, Traffic.PositionIngressStableBlockedCount,
          Traffic.PositionIngressReserveBlockedCount, Traffic.PositionIngressCommitBlockedCount,
          Traffic.PositionIngressStableBlockerPairCount, Traffic.PositionIngressReserveBlockerPairCount,
          Traffic.PositionIngressCommitBlockerPairCount,
          Traffic.PositionIngressSectorDeltaP50, Traffic.PositionIngressSectorDeltaP95,
          Traffic.PositionIngressSectorDeltaMax, Traffic.PositionIngressRadialDeltaP50,
          Traffic.PositionIngressRadialDeltaP95, Traffic.PositionIngressRadialDeltaMax,
          Traffic.PositionIngressUnblockedAlternativeFrontCount,
          Traffic.PositionIngressSameSideAlternativeFrontCount,
          Traffic.PositionIngressNoAlternativeFrontCount,
          Traffic.PositionIngressOrcaFromStableCount, Traffic.PositionIngressOrcaFromReserveCount,
          Traffic.PositionIngressOrcaFromCommitCount, Traffic.PositionIngressOrcaFromOtherCount,
          Traffic.PositionIngressPreferredSpeedP95, Traffic.PositionIngressOrcaSpeedP95,
          Traffic.PositionIngressObstacleSpeedP95, Traffic.PositionIngressFinalSpeedP95,
          Traffic.PositionIngressLowSpeedStepsMax,
          Traffic.PositionIngressTargetExclusionCrossingCount,
          Traffic.PositionIngressOrderInversionCount,
          Traffic.PositionIngressPbdPushAwayCount, Traffic.PositionIngressObstaclePushAwayCount,
          Traffic.PositionFrontAssignedWaitingCount, Traffic.PositionFrontRadialStageCount,
          Traffic.PositionFrontAngularAlignCount, Traffic.PositionFrontRadialCommitCount,
          Traffic.PositionFrontGateInvalidCount, Traffic.PositionFrontRadialCommitBlockedCount,
          Traffic.PositionFrontRouteHash,
          Traffic.PositionFrontRadialPreferredSpeedP95,
          Traffic.PositionFrontRadialOrcaSpeedP95,
          Traffic.PositionFrontRadialFinalSpeedP95,
          Traffic.PositionFrontRadialOrcaForwardSpeedP50,
          Traffic.PositionFrontRadialOrcaForwardSpeedMin,
          Traffic.PositionFrontRadialFinalForwardSpeedP50,
          Traffic.PositionFrontRadialFinalForwardSpeedMin,
          Traffic.PositionFrontRadialOrcaConstraintP95,
          Traffic.PositionFrontRadialConstraintFromActiveCount,
          Traffic.PositionFrontRadialConstraintFromWaitingCount,
          Traffic.PositionFrontRadialConstraintFromReserveCommitCount,
          Traffic.PositionFrontRadialConstraintFromStableCount,
          Traffic.PositionFrontRadialConstraintFromOtherCount,
          Traffic.PositionFrontRadialErrorP50, Traffic.PositionFrontRadialErrorP95,
          Traffic.PositionFrontRadialErrorMax,
          Traffic.PositionFrontRadialErrorImprovedCount,
          Traffic.PositionFrontRadialQuantizedProgressStallCount,
          Traffic.PositionFrontComposeBoundarySwitchCount,
          Traffic.PositionIngressMinimumFixtureHash,
          Traffic.PositionIngressMinimumFixtureConstraintCount,
          Traffic.PositionIngressEvaluationHash);
      }
#endif
      if (Pipeline->IsSf4ReservationOrcaDiagnosticEnabled())
      {
        const auto& Fixture = Pipeline->GetSf4ReservationOrcaDiagnosticFixture();
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf4ReservationOrcaDiagnostic role=server round_id=%d valid=%d too_large=%d primary=%d agents=%d core=%d active_conflict=%d active_disjoint_contained=%d active_outside_corridor=%d waiting=%d stable=%d other=%d branch_disjoint=%d branch_conflict=%d branch_containment=%d fixture_hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, Fixture.bValid ? 1 : 0,
          Fixture.Summary.bFixtureTooLarge ? 1 : 0,
          Fixture.Summary.PrimaryAgentId, Fixture.Agents.Num(),
          Fixture.CoreConstraints.Num(), Fixture.Summary.ActiveRouteConflictCount,
          Fixture.Summary.ActiveRouteDisjointContainedCount,
          Fixture.Summary.ActiveRouteDisjointOutsideCorridorCount,
          Fixture.Summary.WaitingCount, Fixture.Summary.StableCount,
          Fixture.Summary.OtherCount,
          Fixture.Summary.bOnlyDisjointContainedActiveRestoresFeasibility ? 1 : 0,
          Fixture.Summary.bConflictActiveRestoresFeasibility ? 1 : 0,
          Fixture.Summary.bOutsideCorridorActiveRestoresFeasibility ? 1 : 0,
          Fixture.StableHash);
        FString OutputPath;
        const bool bHasOutputPath = FParse::Value(FCommandLine::Get(),
          TEXT("CrowdDemoSf4FixtureOutput="), OutputPath);
        const bool bValidFixture = Fixture.bValid && !Fixture.Summary.bFixtureTooLarge
          && Fixture.Agents.Num() >= 2 && Fixture.Agents.Num() <= 5;
        bool bWritten = false;
        if (bValidFixture && bHasOutputPath && !OutputPath.IsEmpty())
        {
          IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
          PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutputPath));
          bWritten = FFileHelper::SaveStringToFile(
            SerializeSf4ReservationOrcaFixture(Fixture), *OutputPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        if (!bValidFixture || !bWritten)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoSf4ReservationOrcaDiagnostic role=server round_id=%d valid=%d too_large=%d output_path=%d written=%d fixture_hash=%u"),
            RoundResultPacket.RoundId, Fixture.bValid ? 1 : 0,
            Fixture.Summary.bFixtureTooLarge ? 1 : 0,
            bHasOutputPath ? 1 : 0, bWritten ? 1 : 0, Fixture.StableHash);
        }
      }
      if (Pipeline->IsTransitJointDiagnosticEnabled())
      {
        const FCrowdDemoTransitJointDiagnosticFixture& Fixture =
          Pipeline->GetTransitJointDiagnosticFixture();
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTransitJointDiagnostic role=server round_id=%d valid=%d too_large=%d primary=%d agents=%d pairs=%d constraints=%d priority_forward_cmps=%d predicted_speed_cmps=%d obstacle_speed_cmps=%d pbd_speed_cmps=%d reproject_speed_cmps=%d final_speed_cmps=%d downstream_zero_stage=%d joint_status=%d joint_forward_cmps=%d hard_violation=%d safe_forward=%d fixture_hash=%u source=MassPipeline"),
          RoundResultPacket.RoundId, Fixture.bValid ? 1 : 0,
          Fixture.Summary.bFixtureTooLarge ? 1 : 0,
          Fixture.Summary.PrimaryAgentId, Fixture.Summary.ComponentAgentCount,
          Fixture.Summary.ComponentPairCount, Fixture.Summary.ConstraintCount,
          Fixture.Summary.PriorityForwardSpeedCmps, Fixture.Summary.PredictedSpeedCmps,
          Fixture.Summary.ObstacleSpeedCmps, Fixture.Summary.PbdSpeedCmps,
          Fixture.Summary.ReprojectSpeedCmps, Fixture.Summary.FinalSpeedCmps,
          static_cast<int32>(Fixture.Summary.DownstreamZeroStage),
          static_cast<int32>(Fixture.Summary.JointStatus),
          Fixture.Summary.JointForwardSpeedCmps,
          Fixture.Summary.JointHardViolationCount,
          Fixture.Summary.bJointQuantizedSafeForward ? 1 : 0,
          Fixture.StableHash);
        FString OutputPath;
        const bool bHasOutputPath = FParse::Value(FCommandLine::Get(),
          TEXT("CrowdDemoTransitJointFixtureOutput="), OutputPath);
        const bool bWritableFixture = Fixture.bValid;
        bool bWritten = false;
        if (bWritableFixture && bHasOutputPath && !OutputPath.IsEmpty())
        {
          IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
          PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutputPath));
          bWritten = FFileHelper::SaveStringToFile(
            SerializeTransitJointFixture(Fixture), *OutputPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        if (!Fixture.bValid || Fixture.Summary.bFixtureTooLarge || !bWritten)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoTransitJointDiagnostic role=server round_id=%d valid=%d too_large=%d output_path=%d written=%d fixture_hash=%u"),
            RoundResultPacket.RoundId, Fixture.bValid ? 1 : 0,
            Fixture.Summary.bFixtureTooLarge ? 1 : 0,
            bHasOutputPath ? 1 : 0, bWritten ? 1 : 0, Fixture.StableHash);
        }
      }
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

  FCrowdDemoRoundCheckpointTransport::BuildChunks(
    FullFrame,
    CrowdDemoCorrectionFrameChunkSize,
    CorrectionFrameHeader,
    PendingServerCorrectionChunks);
  const int32 ChunkSize = CorrectionFrameHeader.ChunkSize;
  const int32 ChunkCount = CorrectionFrameHeader.ChunkCount;
  PendingServerCorrectionRevision = FullFrame.CorrectionRevision;
  NextPendingServerCorrectionChunkIndex = 0;

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
  RefreshLastCorrectionCounters();
  FlushServerCorrectionChunks();
  ForceNetUpdate();

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

void ACrowdDemoRoundSimCoordinator::FlushServerCorrectionChunks()
{
  if (!HasAuthority() || PendingServerCorrectionChunks.IsEmpty())
  {
    return;
  }

  int32 SentCount = 0;
  while (PendingServerCorrectionChunks.IsValidIndex(NextPendingServerCorrectionChunkIndex)
    && SentCount < CrowdDemoCorrectionChunksPerFlush)
  {
    const FCrowdDemoCorrectionFrameChunk& PendingChunk = PendingServerCorrectionChunks[NextPendingServerCorrectionChunkIndex];
    MulticastCorrectionFrameChunk(PendingChunk);

    ++NextPendingServerCorrectionChunkIndex;
    ++SentCount;
  }

  if (NextPendingServerCorrectionChunkIndex >= PendingServerCorrectionChunks.Num())
  {
    PendingServerCorrectionChunks.Reset();
    NextPendingServerCorrectionChunkIndex = 0;
    PendingServerCorrectionRevision = 0;
  }

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
  Packet.DurationSeconds = Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundObstacle
    ? 20.0f
    : (Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? 30.0f
      : (Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic
        || Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning
        ? 30.0f
        : CrowdDemoRoundDurationSeconds));
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
  const bool bSf3 = CompactRules.Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic;
  const bool bSf4 = CompactRules.Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
  const bool bTraffic = bSf3 || bSf4;
  const int32 SafeAgentCount = FMath::Max(1, AgentCount);
  CompactRules.FormationColumns = (bSf2 || bTraffic)
    ? (SafeAgentCount <= 20 ? 10 : (SafeAgentCount <= 100 ? 25 : 50))
    : (SafeAgentCount > 1 ? FMath::CeilToInt(FMath::Sqrt(static_cast<float>(SafeAgentCount))) : 1);
  CompactRules.FormationSpacingCm = bSf2 ? 128.0f
    : (bTraffic ? 90.0f : (SafeAgentCount > 1 ? 18.0f : 0.0f));
  CompactRules.MaxSpeedCmPerSecond = 800.0f;
  CompactRules.SpawnOrigin = FVector(0.0f, (bSf2 || bTraffic) ? -2850.0f : -2900.0f, 60.0f);
  CompactRules.bEnableSeparation = 0;
  CompactRules.SeparationCellSizeCm = 96.0f;
  CompactRules.SeparationRadiusCm = 78.0f;
  CompactRules.HardSeparationRadiusCm = 42.0f;
  CompactRules.SeparationSpeedCmPerSecond = 120.0f;
  CompactRules.HardSeparationSpeedCmPerSecond = 260.0f;
  CompactRules.SeparationMaxOffsetCm = 520.0f;
  CompactRules.bEnableObstacle = 1;
  CompactRules.bEnableHardSeparationPbd = bTraffic ? 1 : 0;
  CompactRules.HardSeparationPbdIterations = 3;
  CompactRules.HardSeparationPbdMaxCorrectionCm = 24.0f;
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
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetStatic
      || CompactRules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving)
    {
      CompactRules.TargetApproachSettings.bEnabled = 0;
      CompactRules.TargetInfluenceSettings.bEnabled = 1;
      CompactRules.TargetInfluenceSettings.DefaultMinimumCombatCenterDistanceCm = 100.0f;
      CompactRules.TargetInfluenceSettings.DefaultMaximumCombatCenterDistanceCm = 850.0f;
      CompactRules.TargetInfluenceSettings.InfluenceBlendWidthCm = 300.0f;
      CompactRules.TargetInfluenceSettings.RadialGainPerSecond = 2.0f;
      CompactRules.TargetInfluenceSettings.MaxRadialSpeedCmps = 300.0f;
      CompactRules.TargetInfluenceSettings.TargetPhysicalRadiusCm =
        CompactRules.TargetApproachSettings.TargetPhysicalRadiusCm;
      CompactRules.TargetInfluenceSettings.TargetHardSafetyGapCm =
        CompactRules.TargetApproachSettings.TargetHardSafetyGapCm;
      CompactRules.TargetInfluenceSettings.TargetSoftMarginCm =
        CompactRules.TargetApproachSettings.TargetSoftMarginCm;
      CompactRules.TargetInfluenceSettings.PositionQuantumCm =
        CompactRules.TargetApproachSettings.PositionQuantumCm;
      CompactRules.TargetInfluenceSettings.VelocityQuantumCmps =
        CompactRules.TargetApproachSettings.VelocityQuantumCmps;
      CompactRules.TargetInfluenceSettings.AngularSectorCount = 16;
      CompactRules.TargetInfluenceSettings.RadialBandWidthCm = 100.0f;
      CompactRules.TargetInfluenceSettings.DensitySmoothingPassCount = 1;
      CompactRules.TargetInfluenceSettings.DensityMinimumDifference = 1;
      CompactRules.TargetInfluenceSettings.DensitySpeedPerExcessAgentCmps = 20.0f;
      CompactRules.TargetInfluenceSettings.MaximumDensityTangentialSpeedCmps = 120.0f;
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
      CompactRules.TargetMotion.InitialYawDegrees = 0.0f;
      CompactRules.TargetMotion.YawRateDegreesPerSecond = 0.0f;
      CompactRules.TargetSlotLayoutSettings.SourceRevision = 1;
      CompactRules.TargetSlotLayoutSettings.TargetHardSafetyGapCm =
        CompactRules.TargetApproachSettings.TargetHardSafetyGapCm;
      CompactRules.TargetSlotLayoutSettings.PositionQuantumCm =
        CompactRules.TargetApproachSettings.PositionQuantumCm;
      CompactRules.TargetSlotLayoutSettings.AngleQuantumDegrees = 0.01f;
      FCrowdDemoTargetSlotBandRule& Functional =
        CompactRules.TargetSlotLayoutSettings.Bands.AddDefaulted_GetRef();
      Functional.BandId = 1;
      Functional.bFunctional = 1;
      Functional.Capacity = 4;
      Functional.PreferredSurfaceDistanceCm = 160.0f;
      Functional.MinimumCenterDistanceCm = 260.0f;
      Functional.MaximumCenterDistanceCm = 260.0f;
      Functional.StartAngleDegrees = 0.0f;
      Functional.RequiredCapabilityMask = 1u;
      Functional.StablePriorityBase = 0;
      FCrowdDemoTargetSlotBandRule& Fill =
        CompactRules.TargetSlotLayoutSettings.Bands.AddDefaulted_GetRef();
      Fill.BandId = 2;
      Fill.bFunctional = 0;
      Fill.Capacity = 4;
      Fill.PreferredSurfaceDistanceCm = 280.0f;
      Fill.MinimumCenterDistanceCm = 380.0f;
      Fill.MaximumCenterDistanceCm = 380.0f;
      Fill.StartAngleDegrees = 45.0f;
      Fill.RequiredCapabilityMask = 0u;
      Fill.StablePriorityBase = 100;
    }
  }
  CompactRules.ElasticCrowdSettings.FixedStepSeconds = CompactRules.FixedStepSeconds;
  CompactRules.ElasticCrowdSettings.HardSafetyGapCm = 10.0f;
  CompactRules.ElasticCrowdSettings.PreferredSpacingGapCm = 34.0f;
  CompactRules.ElasticCrowdSettings.SpacingGainPerSecond = 2.0f;
  CompactRules.ElasticCrowdSettings.MaxSpacingResponseCmps = 120.0f;
  CompactRules.ElasticCrowdSettings.TransitHorizonSeconds = 0.75f;
  CompactRules.ElasticCrowdSettings.TransitInfluenceFalloffCm = 34.0f;
  CompactRules.ElasticCrowdSettings.TransitGainPerSecond = 2.0f;
  CompactRules.ElasticCrowdSettings.MaxTransitYieldSpeedCmps = 260.0f;
  CompactRules.ElasticCrowdSettings.PositionQuantumCm = 1.0f;
  CompactRules.ElasticCrowdSettings.VelocityQuantumCmps = 1.0f;
  if (bTraffic)
  {
    const bool bP1 = bSf3 && FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoSf3ProfileP1"));
    if (bP1)
    {
      CompactRules.OrcaSettings.NeighborDistanceCm = 800.0f;
      CompactRules.OrcaSettings.MaxNeighbors = 32;
      CompactRules.OrcaSettings.TimeHorizonSeconds = 1.75f;
      CompactRules.TrafficSettings.BandLateralSpeedCmps = 240.0f;
      CompactRules.TrafficSettings.DensityMinimumSpeedScale = 0.25f;
      CompactRules.TrafficSettings.MaxGreenSteps = 60;
    }
    FCrowdDemoTrafficCohortRule& Cohort0 = CompactRules.TrafficCohorts.AddDefaulted_GetRef();
    Cohort0.CohortId = 0;
    Cohort0.FirstFormationIndex = 0;
    Cohort0.AgentCount = AgentCount == 200 ? 100 : AgentCount;
    Cohort0.SpawnOrigin = FVector(0.0f, -2850.0f, 60.0f);
    Cohort0.FormationColumns = SafeAgentCount <= 20 ? 10 : (SafeAgentCount <= 100 ? 25 : 20);
    Cohort0.FlowFieldConfig = CompactRules.FlowFieldConfig;
    if (AgentCount == 200)
    {
      FCrowdDemoTrafficCohortRule& Cohort1 = CompactRules.TrafficCohorts.AddDefaulted_GetRef();
      Cohort1.CohortId = 1;
      Cohort1.FirstFormationIndex = 100;
      Cohort1.AgentCount = 100;
      Cohort1.SpawnOrigin = FVector(0.0f, 1900.0f, 60.0f);
      Cohort1.FormationColumns = 25;
      Cohort1.FlowFieldConfig = CompactRules.FlowFieldConfig;
      Cohort1.FlowFieldConfig.Revision = 2;
      Cohort1.FlowFieldConfig.GoalLocation = FVector(0.0f, -2850.0f, 60.0f);
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
      TEXT("CrowdDemoRoundPlan role=client round_id=%d revision=%d previous_checkpoint_revision=%d start_server_time=%.3f duration=%.3f action=queued source=RoundPlan"),
      Plan.RoundId,
      Plan.Revision,
      Plan.PreviousCheckpointRevision,
      Plan.StartServerTimeSeconds,
      Plan.DurationSeconds);
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
  OutResult.SeparationAppliedAgentCount = Header->SeparationAppliedAgentCount;
  OutResult.SeparationGridCellCount = Header->SeparationGridCellCount;
  OutResult.ObstaclePenetrationCount = Header->ObstaclePenetrationCount;
  OutResult.ArrivalCount = Header->ArrivalCount;
  OutResult.PbdCorrectedAgentCount = Header->PbdCorrectedAgentCount;
  OutResult.PbdCorrectedPairCount = Header->PbdCorrectedPairCount;
  OutResult.PbdMaxPairCorrectionCm = Header->PbdMaxPairCorrectionCm;
  OutResult.PbdMaxAgentTotalCorrectionCm = Header->PbdMaxAgentTotalCorrectionCm;
  OutResult.PbdMaxObstacleReprojectDeltaCm = Header->PbdMaxObstacleReprojectDeltaCm;
  OutResult.PbdMaxFinalSafetyDeltaCm = Header->PbdMaxFinalSafetyDeltaCm;
  OutResult.PbdSolverMsP95 = Header->PbdSolverMsP95;
  OutResult.TrafficMetrics = Header->TrafficMetrics;
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
