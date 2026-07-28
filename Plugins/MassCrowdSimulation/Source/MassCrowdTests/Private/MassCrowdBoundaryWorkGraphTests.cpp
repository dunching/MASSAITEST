#include "Misc/AutomationTest.h"

#include "MassCrowdBoundaryWorkGraph.h"
#include "MassCrowdRuntimeBridge.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdMassBoundaryAgentRecord MakeBoundaryRecord(const int32 AgentId)
  {
    FCrowdMassBoundaryAgentRecord Record;
    Record.Identity.AgentId = AgentId;
    Record.Identity.SetStableEntityRef(
      {1u, static_cast<uint64>(AgentId + 100), 1u});
    Record.AgentFacts.StableEntityRef =
      Record.Identity.GetStableEntityRef();
    Record.State.Position = FVector(AgentId * 100.0f, 0.0f, 60.0f);
    Record.State.Velocity = FVector::ZeroVector;
    Record.State.PlanRevision = 7;
    Record.State.bInitialized = true;
    Record.Properties.PhysicalRadiusCm = 30.0f;
    Record.Properties.HardSafetyGapCm = 5.0f;
    Record.Properties.SoftMarginCm = 10.0f;
    Record.Properties.Mobility = 1.0f;
    Record.Properties.MaximumSpeedCmps = 300.0f;
    return Record;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryWorkGraphJoinTest,
  "MassCrowd.Runtime.BoundaryWorkGraph.TypedJoins",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryWorkGraphJoinTest::RunTest(
  const FString& Parameters)
{
  FCrowdMassBoundaryWorkGraphInput Input;
  const TArray<FCrowdMassBoundaryAgentRecord> Records = {
    MakeBoundaryRecord(2), MakeBoundaryRecord(1)};
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    19, 7, Records, Input.ParticleTemplate.Snapshot);
  Input.Movement.Guidance.FixedStepIndex = 19;
  Input.Movement.Guidance.PlanRevision = 7;
  for (const FCrowdMassBoundaryAgentRecord& BoundaryRecord
    : Input.ParticleTemplate.Snapshot.Agents)
  {
    FCrowdMassGatherRecord& Gather =
      Input.Movement.Guidance.Records.AddDefaulted_GetRef();
    Gather.Identity = BoundaryRecord.Identity;
    Gather.AgentFacts = BoundaryRecord.AgentFacts;
    Gather.State = BoundaryRecord.State;
    Gather.Properties = BoundaryRecord.Properties;
  }
  Input.ParticleTemplate.Particle.FixedStepIndex = 19;
  Input.ParticleTemplate.Particle.PlanRevision = 7;
  Input.ParticleTemplate.ExpectedExternalAgentCount = 1;
  for (const int32 AgentId : {2, 1, -100})
  {
    FCrowdParticleConstraintAgent& Agent =
      Input.ParticleTemplate.Particle.Agents.AddDefaulted_GetRef();
    Agent.AgentId = AgentId;
    Agent.StartPosition = FVector(999.0f);
    Agent.PredictedPosition = FVector(999.0f);
    Agent.PhysicalRadiusCm = 30.0f;
    Agent.Mobility = AgentId < 0 ? 0.0f : 1.0f;
  }

  FCrowdMassSharedFlowSampleOutput SharedFlow;
  SharedFlow.FixedStepIndex = 19;
  SharedFlow.PlanRevision = 7;
  SharedFlow.bValid = true;
  for (const int32 AgentId : {2, 1})
  {
    FCrowdMassSharedFlowAgentOutput& Flow =
      SharedFlow.Agents.AddDefaulted_GetRef();
    Flow.AgentId = AgentId;
    Flow.Candidate.AgentId = AgentId;
    Flow.Candidate.PreferredVelocity =
      FVector(AgentId * 11.0f, 3.0f, 0.0f);
    Flow.Candidate.bValid = true;
  }
  FCrowdMassMovementPipelineWorkInput JoinedMovementInput;
  TestTrue(TEXT("shared flow joins into movement input"),
    FCrowdMassBoundaryWorkGraph::BuildMovementInput(
      Input, SharedFlow, JoinedMovementInput));
  TestEqual(TEXT("movement join preserves complete set"),
    JoinedMovementInput.Guidance.Records.Num(), 2);
  TestTrue(TEXT("movement flow candidate joined by agent id"),
    JoinedMovementInput.Guidance.Records[0].Guidance.SharedFlow
      .PreferredVelocity.Equals(FVector(11.0f, 3.0f, 0.0f)));

  FCrowdMassMovementPipelineWorkOutput Movement;
  Movement.bCompleted = true;
  for (const int32 AgentId : {2, 1})
  {
    FCrowdMassPredictedMovement& Predicted =
      Movement.MovementPredict.Results.AddDefaulted_GetRef();
    Predicted.AgentId = AgentId;
    Predicted.StartPosition = FVector(AgentId * 10.0f, 1.0f, 60.0f);
    Predicted.PredictedPosition =
      FVector(AgentId * 10.0f + 3.0f, 1.0f, 60.0f);
    Predicted.bValid = true;
    FCrowdComposedGuidance& Guidance =
      Movement.Guidance.ComposedGuidance.AddDefaulted_GetRef();
    Guidance.AgentId = AgentId;
    Guidance.AutonomousPreferredVelocity =
      FVector(AgentId * 20.0f, 5.0f, 0.0f);
    Guidance.bValid = true;
  }
  FCrowdMassParticlePipelineWorkInput ParticleInput;
  TestTrue(TEXT("movement joins into particle template"),
    FCrowdMassBoundaryWorkGraph::BuildParticleInput(
      Input, Movement, ParticleInput));
  TestEqual(TEXT("particle join preserves all agents"),
    ParticleInput.Particle.Agents.Num(), 3);
  const FCrowdParticleConstraintAgent* Agent1 =
    ParticleInput.Particle.Agents.FindByPredicate(
      [](const auto& Agent) { return Agent.AgentId == 1; });
  TestTrue(TEXT("agent one exists"), Agent1 != nullptr);
  if (Agent1)
    TestTrue(TEXT("predicted position joined by stable agent id"),
      Agent1->PredictedPosition.Equals(FVector(13.0f, 1.0f, 60.0f)));
  const FCrowdParticleConstraintAgent* External =
    ParticleInput.Particle.Agents.FindByPredicate(
      [](const auto& Agent) { return Agent.AgentId == -100; });
  TestTrue(TEXT("external particle remains explicit"),
    External && External->PredictedPosition.Equals(FVector(999.0f)));

  for (const int32 AgentId : {2, 1})
  {
    FCrowdMassBoundaryFacingTemplate& Template =
      Input.FacingTemplates.AddDefaulted_GetRef();
    Template.Input.AgentId = AgentId;
    Template.Input.CurrentYawDegrees = 15.0f;
  }
  FCrowdMassParticlePipelineWorkOutput Particle;
  Particle.bCompleted = true;
  Particle.PublishPlan.bValid = true;
  for (const int32 AgentId : {1, 2})
  {
    FCrowdMassFinalKinematicState& Kinematic =
      Particle.PublishPlan.FinalKinematics.AddDefaulted_GetRef();
    Kinematic.AgentId = AgentId;
    Kinematic.Position = FVector(AgentId * 30.0f, 7.0f, 60.0f);
    Kinematic.Velocity = FVector::ZeroVector;
    Kinematic.bValid = true;
  }
  FCrowdMassFacingFinalizeWorkInput FacingInput;
  TestTrue(TEXT("movement and particle join into facing template"),
    FCrowdMassBoundaryWorkGraph::BuildFacingInput(
      Input, Movement, Particle, FacingInput));
  TestEqual(TEXT("facing join covers complete snapshot"),
    FacingInput.Facing.Agents.Num(), 2);
  TestEqual(TEXT("facing input is stably ordered"),
    FacingInput.Facing.Agents[0].AgentId, 1);
  TestTrue(TEXT("facing location comes from final kinematics"),
    FacingInput.Facing.Agents[0].Location.Equals(FVector2f(30.0f, 7.0f)));
  TestTrue(TEXT("facing intent comes from composed guidance"),
    FacingInput.Facing.Agents[0].AutonomousPreferredVelocity.Equals(
      FVector2f(20.0f, 5.0f)));

  const FCrowdMassPredictedMovement DuplicateMovement =
    Movement.MovementPredict.Results[0];
  Movement.MovementPredict.Results.Add(DuplicateMovement);
  TestFalse(TEXT("duplicate movement identity fails closed"),
    FCrowdMassBoundaryWorkGraph::BuildParticleInput(
      Input, Movement, ParticleInput));
  return true;
}

#endif
