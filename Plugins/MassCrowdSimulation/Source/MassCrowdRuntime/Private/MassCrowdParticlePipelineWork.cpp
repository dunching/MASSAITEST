#include "MassCrowdParticlePipelineWork.h"

#define FnvOffset ParticlePipeline_FnvOffset
#define FnvPrime ParticlePipeline_FnvPrime
#define Fold ParticlePipeline_Fold
#define FoldInt ParticlePipeline_FoldInt
#define FoldFloat ParticlePipeline_FoldFloat
#define FoldVector ParticlePipeline_FoldVector
#define IsFiniteVector ParticlePipeline_IsFiniteVector

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  uint32 FoldInt(uint32 Hash, const int32 Value)
  {
    return Fold(Hash, static_cast<uint32>(Value));
  }

  uint32 FoldFloat(uint32 Hash, const float Value, const float Scale = 100.0f)
  {
    return FoldInt(Hash, FMath::RoundToInt(Value * Scale));
  }

  uint32 FoldVector(uint32 Hash, const FVector& Value)
  {
    Hash = FoldFloat(Hash, static_cast<float>(Value.X));
    Hash = FoldFloat(Hash, static_cast<float>(Value.Y));
    return FoldFloat(Hash, static_cast<float>(Value.Z));
  }

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }
}

FCrowdMassParticlePipelineWorkOutput FCrowdMassParticlePipelineWork::Run(
  const FCrowdMassParticlePipelineWorkInput& Input)
{
  FCrowdMassParticlePipelineWorkOutput Output;
  if (!Input.Snapshot.bValid
    || Input.Particle.FixedStepIndex != Input.Snapshot.FixedStepIndex
    || Input.Particle.PlanRevision != Input.Snapshot.PlanRevision
    || Input.ExpectedExternalAgentCount < 0
    || Input.PredictedMovements.Num() != Input.Snapshot.Agents.Num())
    return Output;

  TArray<FCrowdMassPredictedMovement> Predicted = Input.PredictedMovements;
  Predicted.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Predicted.Num(); ++Index)
  {
    const FCrowdMassBoundaryAgentRecord& Boundary = Input.Snapshot.Agents[Index];
    if (Predicted[Index].AgentId != Boundary.Identity.AgentId
      || !Predicted[Index].bValid
      || !IsFiniteVector(Predicted[Index].StartPosition)
      || !IsFiniteVector(Predicted[Index].PredictedPosition)
      || !IsFiniteVector(Predicted[Index].Velocity))
      return Output;
  }

  Output.Particle = FCrowdMassParticleWork::Solve(Input.Particle);
  if (!Output.Particle.bCompleted)
    return Output;

  TArray<FCrowdParticleConstraintResult> SolverResults =
    Output.Particle.Results;
  SolverResults.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 1; Index < SolverResults.Num(); ++Index)
    if (SolverResults[Index].AgentId == SolverResults[Index - 1].AgentId)
      return Output;

  TMap<int32, const FCrowdParticleConstraintResult*> SolverByAgentId;
  for (const FCrowdParticleConstraintResult& Result : SolverResults)
    SolverByAgentId.Add(Result.AgentId, &Result);

  int32 ActiveAgentCount = 0;
  for (const FCrowdMassPredictedMovement& Value : Predicted)
    if (Value.bParticleActive) ++ActiveAgentCount;
  if (SolverResults.Num()
      != ActiveAgentCount + Input.ExpectedExternalAgentCount)
    return Output;

  FCrowdMassParticlePublishPlan& Plan = Output.PublishPlan;
  Plan.FixedStepIndex = Input.Snapshot.FixedStepIndex;
  Plan.PlanRevision = Input.Snapshot.PlanRevision;
  Plan.Records.Reserve(Predicted.Num());
  Plan.FinalKinematics.Reserve(Predicted.Num());
  if (Output.Particle.Summary.bValid)
    Plan.PreparedResults = SolverResults;

  for (const FCrowdMassPredictedMovement& Value : Predicted)
  {
    const FCrowdParticleConstraintResult* const* Solver =
      SolverByAgentId.Find(Value.AgentId);
    if (Value.bParticleActive && !Solver)
      return Output;

    FCrowdMassParticlePublishRecord& Record =
      Plan.Records.AddDefaulted_GetRef();
    Record.AgentId = Value.AgentId;
    Record.bParticleActive = Value.bParticleActive;
    Record.bAppliedStateSample = Value.bParticleActive;
    if (!Value.bParticleActive)
    {
      Record.Result.AgentId = Value.AgentId;
      Record.Result.CorrectedPosition = Value.PredictedPosition;
      Record.Result.CorrectedVelocity = FVector::ZeroVector;
      Record.Result.RealizedCorrection = FVector::ZeroVector;
      Record.Result.FirstInfluencedIteration = INDEX_NONE;
      Record.Result.CorrectedPairCount = 0;
      Plan.PreparedResults.Add(Record.Result);
    }
    else if (Output.Particle.Summary.bValid)
    {
      Record.Result = **Solver;
      Record.bUsedSolverResult = true;
    }
    else
    {
      Record.Result.AgentId = Value.AgentId;
      Record.Result.CorrectedPosition = Value.StartPosition;
      Record.Result.CorrectedVelocity = FVector::ZeroVector;
      Record.Result.RealizedCorrection =
        Value.StartPosition - Value.PredictedPosition;
      Record.Result.FirstInfluencedIteration = INDEX_NONE;
      Record.Result.CorrectedPairCount = 0;
      Plan.PreparedResults.Add(Record.Result);
    }

    FCrowdMassFinalKinematicState& Kinematic =
      Plan.FinalKinematics.AddDefaulted_GetRef();
    Kinematic.AgentId = Record.AgentId;
    Kinematic.Position = Record.Result.CorrectedPosition;
    Kinematic.Velocity = Record.Result.CorrectedVelocity;
    Kinematic.bValid = true;
  }

  Plan.PreparedResults.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  uint32 PublishHash = Fold(FnvOffset, 1u);
  PublishHash = FoldInt(PublishHash, Plan.FixedStepIndex);
  PublishHash = FoldInt(PublishHash, Plan.PlanRevision);
  PublishHash = Fold(
    PublishHash, static_cast<uint32>(Input.Snapshot.StableHash));
  PublishHash = Fold(
    PublishHash, static_cast<uint32>(Input.Snapshot.StableHash >> 32));
  for (const FCrowdMassParticlePublishRecord& Record : Plan.Records)
  {
    PublishHash = FoldInt(PublishHash, Record.AgentId);
    PublishHash = FoldVector(PublishHash, Record.Result.CorrectedPosition);
    PublishHash = FoldVector(PublishHash, Record.Result.CorrectedVelocity);
    PublishHash = FoldVector(PublishHash, Record.Result.RealizedCorrection);
    PublishHash = Fold(PublishHash, Record.bParticleActive ? 1u : 0u);
    PublishHash = Fold(PublishHash, Record.bUsedSolverResult ? 1u : 0u);
    PublishHash = Fold(PublishHash, Record.bAppliedStateSample ? 1u : 0u);
  }
  Plan.StableHash = PublishHash;
  Plan.bValid = Plan.Records.Num() == Input.Snapshot.Agents.Num()
    && Plan.FinalKinematics.Num() == Input.Snapshot.Agents.Num();
  if (!Plan.bValid)
    return Output;

  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = Fold(Hash, Output.Particle.StableHash);
  Hash = Fold(Hash, Plan.StableHash);
  Output.StableHash = Hash;
  Output.bCompleted = true;
  return Output;
}

#undef IsFiniteVector
#undef FoldVector
#undef FoldFloat
#undef FoldInt
#undef Fold
#undef FnvPrime
#undef FnvOffset
