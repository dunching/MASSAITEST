#include "CrowdDemoBusinessScenarioContract.h"

namespace
{
  const FCrowdDemoBusinessPlannerRegistry* GetDefaultRegistry()
  {
    static FCrowdDemoBusinessPlannerRegistry Registry;
    static const bool bInitialized =
      FCrowdDemoBusinessPlannerRunner::BuildDefaultRegistry(
        Registry);
    return bInitialized ? &Registry : nullptr;
  }
}

bool FCrowdDemoScenarioAgentFact::IsValid() const
{
  return EntityRef.IsValid()
    && !Position.ContainsNaN()
    && !Velocity.ContainsNaN()
    && Health >= 0
    && Revision != 0;
}

bool FCrowdDemoFriendlyLogisticsPlanningFact::IsValid() const
{
  return EntityRef.IsValid()
    && TaskRef.IsValid()
    && !Position.ContainsNaN()
    && !Velocity.ContainsNaN()
    && !SourceLocation.ContainsNaN()
    && !SinkLocation.ContainsNaN()
    && TransitionRevision != 0;
}

bool FCrowdDemoBusinessScenarioContract::EvaluateNoBusiness(
  const int64 FixedStepIndex,
  uint64& OutDecisionHash)
{
  OutDecisionHash = 0;
  const FCrowdDemoBusinessPlannerRegistry* Registry =
    GetDefaultRegistry();
  if (!Registry || FixedStepIndex < 0) return false;
  FCrowdDemoPlanningSnapshot Snapshot;
  Snapshot.ScenarioId = CrowdDemoBusinessScenarios::NoBusiness;
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.FactRevision =
    static_cast<uint64>(FixedStepIndex) + 1;
  FCrowdDemoPlannerDecisionBatch Batch;
  if (!Snapshot.Finalize()
    || !FCrowdDemoBusinessPlannerRunner::Evaluate(
      *Registry, Snapshot, Batch)
    || !Batch.bValid
    || !Batch.Decisions.IsEmpty())
    return false;
  OutDecisionHash = Batch.StableHash;
  return OutDecisionHash != 0;
}

bool FCrowdDemoBusinessScenarioContract::EvaluateAssigned(
  const FCrowdDemoBusinessScenarioId ScenarioId,
  const FCrowdDemoBusinessPlannerId PlannerId,
  const int64 FixedStepIndex,
  const uint64 FactRevision,
  const TConstArrayView<FCrowdDemoScenarioAgentFact> Agents,
  FCrowdDemoPlannerDecisionBatch& OutBatch)
{
  OutBatch = {};
  const FCrowdDemoBusinessPlannerRegistry* Registry =
    GetDefaultRegistry();
  if (!Registry || !ScenarioId.IsValid()
    || !PlannerId.IsValid() || FixedStepIndex < 0
    || FactRevision == 0)
    return false;
  FCrowdDemoPlanningSnapshot Snapshot;
  Snapshot.ScenarioId = ScenarioId;
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.FactRevision = FactRevision;
  Snapshot.Settings.PopulationLimit =
    FMath::Max(1, Agents.Num());
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
  {
    if (!Agents[Index].IsValid()) return false;
    FCrowdDemoPlannerAgentFact& Agent =
      Snapshot.Agents.AddDefaulted_GetRef();
    Agent.EntityRef = Agents[Index].EntityRef;
    Agent.Assignment.PlannerId = PlannerId;
    Agent.Assignment.CohortId = 1;
    Agent.Assignment.Ordinal =
      static_cast<uint16>(Index);
    Agent.Position = Agents[Index].Position;
    Agent.Velocity = Agents[Index].Velocity;
    Agent.Facing = Agents[Index].Velocity.IsNearlyZero()
      ? FVector::ForwardVector
      : Agents[Index].Velocity.GetSafeNormal();
    Agent.Health = Agents[Index].Health;
    Agent.TransitionRevision = Agents[Index].Revision;
  }
  return Snapshot.Finalize()
    && FCrowdDemoBusinessPlannerRunner::Evaluate(
      *Registry, Snapshot, OutBatch)
    && OutBatch.bValid;
}

bool FCrowdDemoBusinessScenarioContract::EvaluateFriendlyLogistics(
  const int64 FixedStepIndex,
  const uint64 FactRevision,
  const FCrowdDemoFriendlyLogisticsPlanningFact& Fact,
  FCrowdDemoPlannerDecision& OutDecision)
{
  OutDecision = {};
  const FCrowdDemoBusinessPlannerRegistry* Registry =
    GetDefaultRegistry();
  if (!Registry || FixedStepIndex < 0
    || FactRevision == 0 || !Fact.IsValid())
    return false;
  FCrowdDemoPlanningSnapshot Snapshot;
  Snapshot.ScenarioId =
    CrowdDemoBusinessScenarios::FriendlyLogistics;
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.FactRevision = FactRevision;
  Snapshot.Settings.PopulationLimit = 20;
  Snapshot.Settings.MaximumSpeedCmps = 260.0f;
  Snapshot.Settings.ScaleMaximumSpeedCmps = 260.0f;
  Snapshot.Settings.InteractionRadiusCm = 100.0f;
  Snapshot.Settings.ScaleInteractionRadiusCm = 100.0f;
  Snapshot.Settings.LogisticsCooldownSteps = 1;
  FCrowdDemoPlannerAgentFact& Agent =
    Snapshot.Agents.AddDefaulted_GetRef();
  Agent.EntityRef = Fact.EntityRef;
  Agent.Assignment.PlannerId =
    CrowdDemoBusinessPlanners::Logistics;
  Agent.Assignment.CohortId = 1;
  Agent.Position = Fact.Position;
  Agent.Velocity = Fact.Velocity;
  Agent.Facing = Fact.Velocity.IsNearlyZero()
    ? FVector::ForwardVector
    : Fact.Velocity.GetSafeNormal();
  Agent.TaskRef = Fact.TaskRef;
  Agent.LastLogisticsFixedStep =
    Fact.LastLogisticsFixedStep;
  Agent.TransitionRevision = Fact.TransitionRevision;
  Agent.bCarrying = Fact.bCarrying;
  Snapshot.Objectives.Add({
    CrowdDemoBusinessObjectives::LogisticsSource,
    Fact.EntityRef, Fact.SourceLocation, FactRevision});
  Snapshot.Objectives.Add({
    CrowdDemoBusinessObjectives::LogisticsSink,
    Fact.EntityRef, Fact.SinkLocation, FactRevision});
  FCrowdDemoPlannerDecisionBatch Batch;
  if (!Snapshot.Finalize()
    || !FCrowdDemoBusinessPlannerRunner::Evaluate(
      *Registry, Snapshot, Batch)
    || Batch.Decisions.Num() != 1)
    return false;
  OutDecision = MoveTemp(Batch.Decisions[0]);
  return OutDecision.bValid;
}
