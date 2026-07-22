#include "MassCrowdMovementPredictWork.h"

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

  uint32 FoldFloat(uint32 Hash, const float Value)
  {
    return FoldInt(Hash, FMath::RoundToInt(Value * 100.0f));
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

FCrowdMassMovementPredictWorkOutput FCrowdMassMovementPredictWork::Predict(
  const FCrowdMassMovementPredictWorkInput& Input)
{
  FCrowdMassMovementPredictWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || !FMath::IsFinite(Input.FixedStepSeconds)
    || Input.FixedStepSeconds <= 0.0f || Input.Agents.IsEmpty())
    return Output;
  TArray<FCrowdMassMovementPredictAgent> Agents = Input.Agents;
  Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = FoldInt(Hash, Input.FixedStepIndex);
  Hash = FoldInt(Hash, Input.PlanRevision);
  Hash = FoldFloat(Hash, Input.FixedStepSeconds);
  int32 PreviousAgentId = INDEX_NONE;
  for (const FCrowdMassMovementPredictAgent& Agent : Agents)
  {
    if (Agent.AgentId == INDEX_NONE || Agent.AgentId <= PreviousAgentId
      || !IsFiniteVector(Agent.StartPosition)
      || !IsFiniteVector(Agent.AutonomousPreferredVelocity)
      || !IsFiniteVector(Agent.LocalVelocity)
      || !IsFiniteVector(Agent.BoundaryLocation)
      || !FMath::IsFinite(Agent.MaximumSpeedCmps)
      || !FMath::IsFinite(Agent.ProposedZ)
      || !FMath::IsFinite(Agent.VerticalVelocityCmps)
      || Agent.MaximumSpeedCmps < 0.0f)
      return Output;
    PreviousAgentId = Agent.AgentId;
    FCrowdMassPredictedMovement& Result = Output.Results.AddDefaulted_GetRef();
    Result.AgentId = Agent.AgentId;
    Result.StartPosition = Agent.bFreezeAtBoundaryLocation
      ? Agent.BoundaryLocation : Agent.StartPosition;
    Result.Velocity = Agent.bFreezeAtBoundaryLocation
      ? FVector::ZeroVector
      : (Agent.bUseLocalVelocity
        ? (Agent.bLocalVelocityValid
          ? Agent.LocalVelocity.GetClampedToMaxSize2D(Agent.MaximumSpeedCmps)
          : FVector::ZeroVector)
        : Agent.AutonomousPreferredVelocity);
    Result.PredictedPosition = Result.StartPosition
      + Result.Velocity * Input.FixedStepSeconds;
    if (Agent.bVerticalOverride && !Agent.bFreezeAtBoundaryLocation)
    {
      Result.PredictedPosition.Z = Agent.ProposedZ;
      Result.Velocity.Z = Agent.VerticalVelocityCmps;
    }
    else
    {
      Result.PredictedPosition.Z = Result.StartPosition.Z;
      if (!Agent.bVerticalOverride) Result.Velocity.Z = 0.0f;
    }
    Result.bParticleActive = Agent.bParticleActive;
    Result.bValid = true;
    Hash = FoldInt(Hash, Result.AgentId);
    Hash = FoldVector(Hash, Result.StartPosition);
    Hash = FoldVector(Hash, Result.PredictedPosition);
    Hash = FoldVector(Hash, Result.Velocity);
    Hash = Fold(Hash, Result.bParticleActive ? 1u : 0u);
  }
  Output.StableHash = Hash;
  Output.bCompleted = true;
  return Output;
}
