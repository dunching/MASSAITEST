#include "MassCrowdBoundaryWorkGraph.h"

#define FnvOffset64 BoundaryWorkGraph_FnvOffset64
#define FnvPrime64 BoundaryWorkGraph_FnvPrime64
#define Fold BoundaryWorkGraph_Fold

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  uint64 Fold(uint64 Hash, const uint64 Value)
  {
    for (int32 Shift = 0; Shift < 64; Shift += 8)
    {
      Hash ^= static_cast<uint8>(Value >> Shift);
      Hash *= FnvPrime64;
    }
    return Hash;
  }
}

bool FCrowdMassBoundaryWorkGraph::BuildMovementInput(
  const FCrowdMassBoundaryWorkGraphInput& Input,
  const FCrowdMassSharedFlowSampleOutput& SharedFlow,
  FCrowdMassMovementPipelineWorkInput& OutMovement)
{
  OutMovement = {};
  if (!SharedFlow.bValid
    || SharedFlow.FixedStepIndex != Input.Movement.Guidance.FixedStepIndex
    || SharedFlow.PlanRevision != Input.Movement.Guidance.PlanRevision
    || SharedFlow.Agents.Num() != Input.Movement.Guidance.Records.Num())
    return false;
  TMap<int32, const FCrowdMassSharedFlowAgentOutput*> FlowById;
  for (const FCrowdMassSharedFlowAgentOutput& Agent : SharedFlow.Agents)
  {
    if (Agent.AgentId == INDEX_NONE || FlowById.Contains(Agent.AgentId))
      return false;
    FlowById.Add(Agent.AgentId, &Agent);
  }
  OutMovement = Input.Movement;
  for (FCrowdMassGatherRecord& Record : OutMovement.Guidance.Records)
  {
    const FCrowdMassSharedFlowAgentOutput* const* Flow =
      FlowById.Find(Record.Identity.AgentId);
    if (!Flow) return false;
    Record.Guidance.SharedFlow = (*Flow)->Candidate;
  }
  return FlowById.Num() == OutMovement.Guidance.Records.Num();
}

bool FCrowdMassBoundaryWorkGraph::BuildParticleInput(
  const FCrowdMassBoundaryWorkGraphInput& Input,
  const FCrowdMassMovementPipelineWorkOutput& Movement,
  FCrowdMassParticlePipelineWorkInput& OutParticle)
{
  OutParticle = {};
  if (!Movement.bCompleted
    || !Input.ParticleTemplate.Snapshot.bValid
    || Movement.MovementPredict.Results.Num()
      != Input.ParticleTemplate.Snapshot.Agents.Num())
    return false;

  OutParticle = Input.ParticleTemplate;
  OutParticle.PredictedMovements = Movement.MovementPredict.Results;
  TMap<int32, const FCrowdMassPredictedMovement*> PredictedById;
  int32 ActivePredictedCount = 0;
  for (const FCrowdMassPredictedMovement& Predicted
    : OutParticle.PredictedMovements)
  {
    if (!Predicted.bValid || PredictedById.Contains(Predicted.AgentId))
      return false;
    PredictedById.Add(Predicted.AgentId, &Predicted);
    if (Predicted.bParticleActive) ++ActivePredictedCount;
  }
  int32 Joined = 0;
  for (FCrowdParticleConstraintAgent& Agent : OutParticle.Particle.Agents)
  {
    const FCrowdMassPredictedMovement* const* Predicted =
      PredictedById.Find(Agent.AgentId);
    if (!Predicted) continue; // Explicit external particle.
    Agent.StartPosition = (*Predicted)->StartPosition;
    Agent.PredictedPosition = (*Predicted)->PredictedPosition;
    ++Joined;
  }
  OutParticle.Particle.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  return Joined == ActivePredictedCount
    && OutParticle.Particle.Agents.Num() - Joined
      == OutParticle.ExpectedExternalAgentCount;
}

bool FCrowdMassBoundaryWorkGraph::BuildFacingInput(
  const FCrowdMassBoundaryWorkGraphInput& Input,
  const FCrowdMassMovementPipelineWorkOutput& Movement,
  const FCrowdMassParticlePipelineWorkOutput& Particle,
  FCrowdMassFacingFinalizeWorkInput& OutFacing)
{
  if (!Movement.bCompleted || !Particle.bCompleted
    || !Particle.PublishPlan.bValid
    || Input.FacingTemplates.Num()
      != Input.ParticleTemplate.Snapshot.Agents.Num())
    return false;
  return BuildFacingInputFromKinematics(
    Input, Input.ParticleTemplate.Snapshot, Movement,
    Particle.PublishPlan.FinalKinematics, OutFacing);
}

bool FCrowdMassBoundaryWorkGraph::BuildFacingInputFromKinematics(
  const FCrowdMassBoundaryWorkGraphInput& Input,
  const FCrowdMassBoundarySnapshot& Snapshot,
  const FCrowdMassMovementPipelineWorkOutput& Movement,
  const TConstArrayView<FCrowdMassFinalKinematicState> Kinematics,
  FCrowdMassFacingFinalizeWorkInput& OutFacing)
{
  OutFacing = {};
  if (!Movement.bCompleted || !Snapshot.bValid
    || Input.FacingTemplates.Num() != Snapshot.Agents.Num()
    || Kinematics.Num() != Snapshot.Agents.Num())
    return false;
  TMap<int32, const FCrowdComposedGuidance*> GuidanceById;
  for (const FCrowdComposedGuidance& Guidance
    : Movement.Guidance.ComposedGuidance)
  {
    if (GuidanceById.Contains(Guidance.AgentId)) return false;
    GuidanceById.Add(Guidance.AgentId, &Guidance);
  }
  TMap<int32, const FCrowdMassFinalKinematicState*> KinematicById;
  for (const FCrowdMassFinalKinematicState& Kinematic
    : Kinematics)
  {
    if (!Kinematic.bValid || KinematicById.Contains(Kinematic.AgentId))
      return false;
    KinematicById.Add(Kinematic.AgentId, &Kinematic);
  }
  OutFacing.Facing.FixedStepIndex =
    Snapshot.FixedStepIndex;
  OutFacing.Facing.PlanRevision =
    Snapshot.PlanRevision;
  OutFacing.Facing.Settings = Input.FacingSettings;
  for (const FCrowdMassBoundaryFacingTemplate& Template
    : Input.FacingTemplates)
  {
    const FCrowdComposedGuidance* const* Guidance =
      GuidanceById.Find(Template.Input.AgentId);
    const FCrowdMassFinalKinematicState* const* Kinematic =
      KinematicById.Find(Template.Input.AgentId);
    if (!Guidance || !Kinematic) return false;
    FCrowdFacingInput& Facing = OutFacing.Facing.Agents.AddDefaulted_GetRef();
    Facing = Template.Input;
    Facing.AutonomousPreferredVelocity = FVector2f(
      (*Guidance)->AutonomousPreferredVelocity.X,
      (*Guidance)->AutonomousPreferredVelocity.Y);
    Facing.Location = FVector2f(
      (*Kinematic)->Position.X, (*Kinematic)->Position.Y);
  }
  OutFacing.Facing.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  OutFacing.Snapshot = Snapshot;
  OutFacing.Kinematics = TArray<FCrowdMassFinalKinematicState>(
    Kinematics);
  return GuidanceById.Num() == OutFacing.Facing.Agents.Num()
    && KinematicById.Num() == OutFacing.Facing.Agents.Num();
}

FCrowdMassBoundaryWorkGraphOutput FCrowdMassBoundaryWorkGraph::Run(
  const FCrowdMassBoundaryWorkGraphInput& Input)
{
  FCrowdMassBoundaryWorkGraphOutput Output;
  Output.SharedFlow =
    FCrowdMassSharedFlowWork::BuildPreferred(Input.SharedFlow);
  FCrowdMassMovementPipelineWorkInput MovementInput;
  if (!BuildMovementInput(Input, Output.SharedFlow, MovementInput))
    return Output;
  Output.Movement = FCrowdMassMovementPipelineWork::Run(MovementInput);
  FCrowdMassParticlePipelineWorkInput ParticleInput;
  if (!BuildParticleInput(Input, Output.Movement, ParticleInput))
    return Output;
  Output.Particle = FCrowdMassParticlePipelineWork::Run(ParticleInput);
  FCrowdMassFacingFinalizeWorkInput FacingInput;
  if (!BuildFacingInput(
      Input, Output.Movement, Output.Particle, FacingInput))
    return Output;
  Output.FacingFinalize =
    FCrowdMassFacingFinalizeWork::Run(FacingInput);
  if (!Output.FacingFinalize.bCompleted) return Output;
  uint64 Hash = Fold(FnvOffset64, 1);
  Hash = Fold(Hash, Output.SharedFlow.StableHash);
  Hash = Fold(Hash, Output.Movement.StableHash);
  Hash = Fold(Hash, Output.Particle.StableHash);
  Hash = Fold(Hash, Output.FacingFinalize.StableHash);
  Output.StableHash = Hash;
  Output.bCompleted = Hash != 0;
  return Output;
}

#undef Fold
#undef FnvPrime64
#undef FnvOffset64
