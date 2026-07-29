#include "MassCrowdMovementPipelineWork.h"

#include "HAL/PlatformTime.h"

#define FnvOffset MovementPipeline_FnvOffset
#define FnvPrime MovementPipeline_FnvPrime
#define Fold MovementPipeline_Fold
#define IsFiniteVector MovementPipeline_IsFiniteVector

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

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }
}

FCrowdMassMovementPipelineWorkOutput FCrowdMassMovementPipelineWork::Run(
  const FCrowdMassMovementPipelineWorkInput& Input)
{
  FCrowdMassMovementPipelineWorkOutput Output;
  if (Input.Guidance.FixedStepIndex < 0
    || Input.Guidance.PlanRevision < 0
    || !FMath::IsFinite(Input.FixedStepSeconds)
    || Input.FixedStepSeconds <= 0.0f
    || Input.Guidance.Records.IsEmpty()
    || Input.AgentOverlays.Num() != Input.Guidance.Records.Num())
    return Output;

  TArray<FCrowdMassGatherRecord> Records = Input.Guidance.Records;
  Records.Sort([](const auto& A, const auto& B)
  {
    return A.Identity.AgentId < B.Identity.AgentId;
  });
  TArray<FCrowdMassMovementPipelineAgentOverlay> Overlays = Input.AgentOverlays;
  Overlays.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Records.Num(); ++Index)
  {
    const FCrowdMassGatherRecord& Record = Records[Index];
    const FCrowdMassMovementPipelineAgentOverlay& Overlay = Overlays[Index];
    if (Record.Identity.AgentId == INDEX_NONE
      || Record.Identity.AgentId != Overlay.AgentId
      || (Index > 0
        && Records[Index - 1].Identity.AgentId == Record.Identity.AgentId)
      || !FMath::IsFinite(Overlay.MaximumSpeedCmps)
      || Overlay.MaximumSpeedCmps < 0.0f
      || !IsFiniteVector(Overlay.BoundaryLocation)
      || !FMath::IsFinite(Overlay.ProposedZ)
      || !FMath::IsFinite(Overlay.VerticalVelocityCmps))
      return Output;
  }

  const double GuidanceStartSeconds = FPlatformTime::Seconds();
  FCrowdMassGuidanceWorkInput GuidanceInput = Input.Guidance;
  GuidanceInput.Records = Records;
  Output.Guidance = FCrowdMassGuidanceWork::Compose(GuidanceInput);
  Output.GuidanceWorkMilliseconds = static_cast<float>(
    (FPlatformTime::Seconds() - GuidanceStartSeconds) * 1000.0);
  if (!Output.Guidance.bValid
    || Output.Guidance.ComposedGuidance.Num() != Records.Num())
    return Output;

  if (Input.bRunLocalPredictive)
  {
    FCrowdMassLocalPredictiveWorkInput LocalInput;
    LocalInput.FixedStepIndex = Input.Guidance.FixedStepIndex;
    LocalInput.PlanRevision = Input.Guidance.PlanRevision;
    LocalInput.Environment = Input.Environment;
    LocalInput.Settings = Input.LocalPredictiveSettings;
    LocalInput.PreviousGrantStates = Input.PreviousGrantStates;
    LocalInput.bCaptureDiagnostic = Input.bCaptureLocalPredictiveDiagnostic;
    for (int32 Index = 0; Index < Records.Num(); ++Index)
    {
      const FCrowdMassGatherRecord& Record = Records[Index];
      const FCrowdComposedGuidance& Composed =
        Output.Guidance.ComposedGuidance[Index];
      const FCrowdMassMovementPipelineAgentOverlay& Overlay = Overlays[Index];
      if (Composed.AgentId != Record.Identity.AgentId
        || Composed.PlanRevision != Input.Guidance.PlanRevision
        || !Record.State.bInitialized
        || Record.State.PlanRevision != Input.Guidance.PlanRevision
        || Record.Identity.LifecycleSerial <= 0
        || !FMath::IsFinite(Record.State.Position.X)
        || !FMath::IsFinite(Record.State.Position.Y)
        || !FMath::IsFinite(Record.State.Velocity.X)
        || !FMath::IsFinite(Record.State.Velocity.Y)
        || !FMath::IsFinite(Composed.AutonomousPreferredVelocity.X)
        || !FMath::IsFinite(Composed.AutonomousPreferredVelocity.Y)
        || !FMath::IsFinite(Record.Properties.PhysicalRadiusCm)
        || !FMath::IsFinite(Record.Properties.HardSafetyGapCm)
        || Record.Properties.PhysicalRadiusCm <= 0.0f
        || Record.Properties.HardSafetyGapCm < 0.0f)
        return Output;
      FCrowdLocalPredictiveAgent& Agent =
        LocalInput.Agents.AddDefaulted_GetRef();
      Agent.AgentId = Record.Identity.AgentId;
      Agent.InteractionLayer = Overlay.InteractionLayer;
      Agent.Position = FVector2f(
        Record.State.Position.X, Record.State.Position.Y);
      Agent.Velocity = FVector2f(
        Record.State.Velocity.X, Record.State.Velocity.Y);
      Agent.PreferredVelocity = FVector2f(
        Composed.AutonomousPreferredVelocity.X,
        Composed.AutonomousPreferredVelocity.Y);
      Agent.PhysicalRadiusCm = Record.Properties.PhysicalRadiusCm;
      Agent.HardSafetyGapCm = Record.Properties.HardSafetyGapCm;
      Agent.MaxSpeedCmps = Overlay.MaximumSpeedCmps;
      Agent.BlockedAgeSteps = FMath::Max(0, Overlay.PreviousBlockedAgeSteps);
    }
    Output.LocalPredictiveAgents = LocalInput.Agents;
    const double LocalStartSeconds = FPlatformTime::Seconds();
    Output.LocalPredictive = FCrowdMassLocalPredictiveWork::Solve(LocalInput);
    Output.LocalPredictiveWorkMilliseconds = static_cast<float>(
      (FPlatformTime::Seconds() - LocalStartSeconds) * 1000.0);
    if (!Output.LocalPredictive.bCompleted
      || Output.LocalPredictive.Results.Num() != Records.Num())
      return Output;
  }

  FCrowdMassMovementPredictWorkInput PredictInput;
  PredictInput.FixedStepIndex = Input.Guidance.FixedStepIndex;
  PredictInput.PlanRevision = Input.Guidance.PlanRevision;
  PredictInput.FixedStepSeconds = Input.FixedStepSeconds;
  for (int32 Index = 0; Index < Records.Num(); ++Index)
  {
    const FCrowdMassGatherRecord& Record = Records[Index];
    const FCrowdComposedGuidance& Composed =
      Output.Guidance.ComposedGuidance[Index];
    const FCrowdMassMovementPipelineAgentOverlay& Overlay = Overlays[Index];
    FCrowdMassMovementPredictAgent& Agent =
      PredictInput.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Record.Identity.AgentId;
    Agent.StartPosition = Record.State.Position;
    Agent.AutonomousPreferredVelocity =
      Composed.AutonomousPreferredVelocity;
    Agent.MaximumSpeedCmps = Overlay.MaximumSpeedCmps;
    Agent.bUseLocalVelocity = Input.bRunLocalPredictive;
    if (Input.bRunLocalPredictive)
    {
      const FCrowdLocalPredictiveResult& Local =
        Output.LocalPredictive.Results[Index];
      if (Local.AgentId != Agent.AgentId) return Output;
      Agent.LocalVelocity = FVector(Local.Velocity.X, Local.Velocity.Y, 0.0f);
      Agent.bLocalVelocityValid = Local.bValid;
    }
    Agent.bFreezeAtBoundaryLocation = Overlay.bFreezeAtBoundaryLocation;
    Agent.BoundaryLocation = Overlay.BoundaryLocation;
    Agent.bVerticalOverride = Overlay.bVerticalOverride;
    Agent.ProposedZ = Overlay.ProposedZ;
    Agent.VerticalVelocityCmps = Overlay.VerticalVelocityCmps;
    Agent.bParticleActive = Overlay.bParticleActive;
  }
  const double PredictStartSeconds = FPlatformTime::Seconds();
  Output.MovementPredict = FCrowdMassMovementPredictWork::Predict(PredictInput);
  Output.MovementPredictWorkMilliseconds = static_cast<float>(
    (FPlatformTime::Seconds() - PredictStartSeconds) * 1000.0);
  if (!Output.MovementPredict.bCompleted
    || Output.MovementPredict.Results.Num() != Records.Num())
    return Output;

  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = Fold(Hash, Output.Guidance.StableHash);
  Hash = Fold(Hash, Input.bRunLocalPredictive
    ? Output.LocalPredictive.StableHash : 0u);
  Hash = Fold(Hash, Output.MovementPredict.StableHash);
  Output.StableHash = Hash;
  Output.bCompleted = true;
  return Output;
}

#undef IsFiniteVector
#undef Fold
#undef FnvPrime
#undef FnvOffset
