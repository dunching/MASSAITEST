#include "CrowdDemoPlanningRuntimeHost.h"

bool FCrowdDemoPlanningRuntimeEntityFact::IsValid() const
{
  return EntityRef.IsValid()
    && !Position.ContainsNaN()
    && !Velocity.ContainsNaN()
    && !Facing.ContainsNaN()
    && NavLayer < 64;
}

bool FCrowdDemoPlanningRuntimeHost::Stage(
  FCrowdBehaviorSourceRuntime& Runtime,
  const int64 FixedStepIndex,
  const TConstArrayView<FCrowdDemoPlanningRuntimeEntityFact>
    EntityFacts,
  const TConstArrayView<FCrowdDemoPlannerDecision> Decisions)
{
  if (FixedStepIndex < 0) return false;
  TArray<FCrowdDemoPlanningRuntimeEntityFact> SortedFacts(
    EntityFacts);
  SortedFacts.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  TArray<const FCrowdDemoPlannerDecision*> SortedDecisions;
  SortedDecisions.Reserve(Decisions.Num());
  for (const FCrowdDemoPlannerDecision& Decision : Decisions)
  {
    if (!Decision.bValid) return false;
    SortedDecisions.Add(&Decision);
  }
  SortedDecisions.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  for (int32 Index = 0; Index < SortedFacts.Num(); ++Index)
  {
    if (!SortedFacts[Index].IsValid()
      || (Index > 0
        && !(SortedFacts[Index - 1].EntityRef
          < SortedFacts[Index].EntityRef)))
      return false;
  }
  for (int32 Index = 0; Index < SortedDecisions.Num(); ++Index)
  {
    if (Index > 0
      && !(SortedDecisions[Index - 1]->EntityRef
        < SortedDecisions[Index]->EntityRef))
      return false;
    if (!SortedFacts.ContainsByPredicate(
        [&](const auto& Fact)
        {
          return Fact.EntityRef
            == SortedDecisions[Index]->EntityRef;
        }))
      return false;
  }

  for (const FCrowdDemoPlanningRuntimeEntityFact& Fact
    : SortedFacts)
  {
    const FCrowdDemoPlannerDecision* Decision = nullptr;
    if (const FCrowdDemoPlannerDecision* const* Found =
        SortedDecisions.FindByPredicate(
          [&](const auto* Candidate)
          {
            return Candidate
              && Candidate->EntityRef == Fact.EntityRef;
          }))
      Decision = *Found;
    const FCrowdBehaviorSourceSet* CurrentSet =
      Runtime.FindSourceSet(Fact.EntityRef);
    if (!CurrentSet) return false;
    const TConstArrayView<FCrowdDemoDesiredSource> Desired =
      Decision
      ? TConstArrayView<FCrowdDemoDesiredSource>(
          Decision->DesiredSources)
      : TConstArrayView<FCrowdDemoDesiredSource>();
    TArray<FCrowdBehaviorSourceCommand> Commands;
    if (!FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
        FixedStepIndex, *CurrentSet, Desired, Commands))
      return false;
    for (const FCrowdBehaviorSourceCommand& Command : Commands)
      if (!Runtime.QueueCommand(Command))
        return false;

    FCrowdBehaviorEntityEvaluationContext Context;
    Context.EntityRef = Fact.EntityRef;
    Context.FixedStepIndex = FixedStepIndex;
    Context.Position = Fact.Position;
    Context.Velocity = Fact.Velocity;
    Context.Facing = Fact.Facing;
    if (Decision)
    {
      for (const FCrowdDemoContextRequest& Request
        : Decision->ContextRequests)
      {
        if (Request.FactRevision
          != static_cast<uint64>(FixedStepIndex) + 1)
          return false;
        const FCrowdDemoPlanningRuntimeEntityFact* Subject =
          SortedFacts.FindByPredicate(
            [&](const auto& Candidate)
            {
              return Candidate.EntityRef == Request.SubjectRef;
            });
        if (!Subject) return false;
        FCrowdBehaviorContextRecord& Record =
          Context.Records.AddDefaulted_GetRef();
        if (Request.Kind
          == ECrowdDemoContextRequestKind::TargetKinematics)
        {
          FCrowdTargetKinematicsV1 Target;
          Target.TargetRef = Request.SubjectRef;
          Target.Position = FVector3f(Subject->Position);
          Target.Velocity = FVector3f(Subject->Velocity);
          Target.Facing = FVector3f(Subject->Facing);
          Target.NavLayer = Subject->NavLayer;
          Target.FactRevision = Request.FactRevision;
          if (!Record.Set(
              CrowdStandardSources::TargetKinematicsContextType,
              CrowdStandardSources::ContextSchemaVersion,
              Target))
            return false;
        }
        else if (Request.Kind
          == ECrowdDemoContextRequestKind::FormationAnchor)
        {
          FCrowdFormationAnchorV1 Anchor;
          Anchor.AnchorRef = Request.SubjectRef;
          Anchor.Position = FVector3f(Subject->Position);
          Anchor.Velocity = FVector3f(Subject->Velocity);
          Anchor.Facing = FVector3f(Subject->Facing);
          Anchor.LocalSlotOffset =
            FVector3f(Request.LocalOffset);
          Anchor.NavLayer = Subject->NavLayer;
          Anchor.FactRevision = Request.FactRevision;
          if (!Record.Set(
              CrowdStandardSources::FormationAnchorContextType,
              CrowdStandardSources::ContextSchemaVersion,
              Anchor))
            return false;
        }
        else
        {
          return false;
        }
      }
    }
    Context.RecalculateStableHash();
    if (!Runtime.SetEvaluationContext(Context))
      return false;
  }
  return true;
}
