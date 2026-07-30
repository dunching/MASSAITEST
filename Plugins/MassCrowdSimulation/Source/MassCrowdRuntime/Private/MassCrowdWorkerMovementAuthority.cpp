#include "MassCrowdWorkerMovementAuthority.h"

namespace CrowdWorkerMovementAuthorityPrivate
{
  template<typename T>
  void AppendPod(TArray<uint8>& Bytes, const T& Value)
  {
    const int32 Offset = Bytes.AddUninitialized(sizeof(T));
    FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(T));
  }

  template<typename T>
  bool ReadPod(
    const TArray<uint8>& Bytes,
    int32& Offset,
    T& OutValue)
  {
    if (Offset < 0 || Offset + sizeof(T) > Bytes.Num())
      return false;
    FMemory::Memcpy(
      &OutValue, Bytes.GetData() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
  }

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  float NormalizeYaw(const float Yaw)
  {
    return FRotator::NormalizeAxis(Yaw);
  }
}

using namespace CrowdWorkerMovementAuthorityPrivate;

bool FCrowdWorkerMovementState::IsValid() const
{
  return IsFiniteVector(Position)
    && IsFiniteVector(Velocity)
    && FMath::IsFinite(YawDegrees)
    && FMath::IsFinite(SimulationTimeSeconds)
    && SimulationTimeSeconds >= 0.0;
}

bool FCrowdWorkerMovementStateCodec::Encode(
  const FCrowdWorkerMovementState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes.Reserve(EncodedByteCount);
  AppendPod(OutPayload.Bytes, State.Position.X);
  AppendPod(OutPayload.Bytes, State.Position.Y);
  AppendPod(OutPayload.Bytes, State.Position.Z);
  AppendPod(OutPayload.Bytes, State.Velocity.X);
  AppendPod(OutPayload.Bytes, State.Velocity.Y);
  AppendPod(OutPayload.Bytes, State.Velocity.Z);
  const float NormalizedYaw = NormalizeYaw(State.YawDegrees);
  AppendPod(OutPayload.Bytes, NormalizedYaw);
  AppendPod(OutPayload.Bytes, State.SimulationTimeSeconds);
  AppendPod(OutPayload.Bytes, State.CorrectionRevision);
  OutPayload.RecalculateStableHash();
  return OutPayload.Bytes.Num() == EncodedByteCount;
}

bool FCrowdWorkerMovementStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerMovementState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != EncodedByteCount
    || Payload.CalculateStableHash() != Payload.StableHash)
    return false;
  int32 Offset = 0;
  if (!ReadPod(Payload.Bytes, Offset, OutState.Position.X)
    || !ReadPod(Payload.Bytes, Offset, OutState.Position.Y)
    || !ReadPod(Payload.Bytes, Offset, OutState.Position.Z)
    || !ReadPod(Payload.Bytes, Offset, OutState.Velocity.X)
    || !ReadPod(Payload.Bytes, Offset, OutState.Velocity.Y)
    || !ReadPod(Payload.Bytes, Offset, OutState.Velocity.Z)
    || !ReadPod(Payload.Bytes, Offset, OutState.YawDegrees)
    || !ReadPod(
      Payload.Bytes, Offset, OutState.SimulationTimeSeconds)
    || !ReadPod(
      Payload.Bytes, Offset, OutState.CorrectionRevision)
    || Offset != Payload.Bytes.Num()
    || !OutState.IsValid())
  {
    OutState = {};
    return false;
  }
  OutState.YawDegrees = NormalizeYaw(OutState.YawDegrees);
  return true;
}

bool FCrowdWorkerMovementAuthority::ResetQuiescent(
  const uint64 Generation,
  const ECrowdWorkerMovementAuthorityMode InMode,
  const TConstArrayView<FCrowdStableEntityRef> InCanaryEntities)
{
  if (Generation == 0) return false;
  TSet<FCrowdStableEntityRef> CandidateCanaries;
  for (const FCrowdStableEntityRef& Ref : InCanaryEntities)
  {
    if (!Ref.IsValid() || CandidateCanaries.Contains(Ref))
      return false;
    CandidateCanaries.Add(Ref);
  }
  if (InMode != ECrowdWorkerMovementAuthorityMode::Canary
    && !CandidateCanaries.IsEmpty())
    return false;
  CurrentEntities.Reset();
  CanaryEntities = MoveTemp(CandidateCanaries);
  Histories.Reset();
  Metrics = {};
  Metrics.Generation = Generation;
  Mode = InMode;
  bInitialized = true;
  RefreshMetrics();
  return true;
}

bool FCrowdWorkerMovementAuthority::UpdateCurrentEntities(
  const uint64 Generation,
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs)
{
  if (!bInitialized || Metrics.bViolation
    || Generation != Metrics.Generation)
    return false;
  TSet<FCrowdStableEntityRef> Candidate;
  for (const FCrowdStableEntityRef& Ref : EntityRefs)
  {
    if (!Ref.IsValid() || Candidate.Contains(Ref))
    {
      LatchViolation();
      return false;
    }
    Candidate.Add(Ref);
  }
  CurrentEntities = MoveTemp(Candidate);
  for (auto It = Histories.CreateIterator(); It; ++It)
    if (!CurrentEntities.Contains(It.Key()))
      It.RemoveCurrent();
  for (const FCrowdStableEntityRef& Ref : CanaryEntities)
  {
    if (!CurrentEntities.Contains(Ref))
    {
      LatchViolation();
      return false;
    }
  }
  RefreshMetrics();
  return true;
}

bool FCrowdWorkerMovementAuthority::IsWorkerOwner(
  const FCrowdStableEntityRef& EntityRef) const
{
  if (!bInitialized || Metrics.bViolation
    || !CurrentEntities.Contains(EntityRef))
    return false;
  if (Mode == ECrowdWorkerMovementAuthorityMode::Production)
    return true;
  return Mode == ECrowdWorkerMovementAuthorityMode::Canary
    && CanaryEntities.Contains(EntityRef);
}

bool FCrowdWorkerMovementAuthority::ValidateNormalInput(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 DirtyMask)
{
  if (!bInitialized || Metrics.bViolation
    || !CurrentEntities.Contains(EntityRef))
    return false;
  if (IsWorkerOwner(EntityRef)
    && (DirtyMask & CrowdWorkerMovementFields::Movement) != 0)
  {
    ++Metrics.RejectedEchoCount;
    return false;
  }
  return true;
}

ECrowdWorkerMovementAcceptResult
FCrowdWorkerMovementAuthority::AcceptPatch(
  const FCrowdWorkerStatePatch& Patch)
{
  if (!bInitialized)
    return ECrowdWorkerMovementAcceptResult::RejectedNotInitialized;
  if (Metrics.bViolation)
    return ECrowdWorkerMovementAcceptResult::Violation;
  if (Patch.Generation != Metrics.Generation)
    return ECrowdWorkerMovementAcceptResult::RejectedGeneration;
  if (!CurrentEntities.Contains(Patch.EntityRef))
    return ECrowdWorkerMovementAcceptResult::RejectedLifecycle;
  if (Mode != ECrowdWorkerMovementAuthorityMode::Shadow
    && !IsWorkerOwner(Patch.EntityRef))
    return ECrowdWorkerMovementAcceptResult::RejectedOwner;
  if ((Patch.DirtyMask & CrowdWorkerMovementFields::Movement)
      != CrowdWorkerMovementFields::Movement
    || (Patch.DirtyMask & ~CrowdWorkerMovementFields::Movement) != 0
    || Patch.WorkerEpoch == 0
    || Patch.State.StateRevision == 0
    || Patch.CalculateStableHash() != Patch.StableHash)
  {
    ++Metrics.RejectedPatchCount;
    return ECrowdWorkerMovementAcceptResult::RejectedPayload;
  }
  FCrowdWorkerMovementState Movement;
  if (!FCrowdWorkerMovementStateCodec::Decode(
      Patch.State.Payload, Movement))
  {
    ++Metrics.RejectedPatchCount;
    return ECrowdWorkerMovementAcceptResult::RejectedPayload;
  }
  FHistory& History = Histories.FindOrAdd(Patch.EntityRef);
  if (History.bHasCurrent
    && (Patch.WorkerEpoch < History.CurrentWorkerEpoch
      || (Patch.WorkerEpoch == History.CurrentWorkerEpoch
        && Patch.SourceInputSequence
          <= History.CurrentInputSequence)
      || Patch.State.StateRevision <= History.StateRevision
      || Movement.CorrectionRevision
        < History.Current.CorrectionRevision))
  {
    ++Metrics.RejectedPatchCount;
    return ECrowdWorkerMovementAcceptResult::RejectedSequence;
  }
  if (History.bHasCurrent)
  {
    History.Previous = History.Current;
    History.PreviousWorkerEpoch = History.CurrentWorkerEpoch;
    History.PreviousInputSequence = History.CurrentInputSequence;
    History.bHasPrevious = true;
  }
  History.Current = Movement;
  History.CurrentWorkerEpoch = Patch.WorkerEpoch;
  History.CurrentInputSequence = Patch.SourceInputSequence;
  History.StateRevision = Patch.State.StateRevision;
  History.bHasCurrent = true;
  ++Metrics.AcceptedPatchCount;
  RefreshMetrics();
  return ECrowdWorkerMovementAcceptResult::Accepted;
}

ECrowdWorkerMovementAcceptResult
FCrowdWorkerMovementAuthority::ApplyCorrection(
  const FCrowdWorkerCorrectionDelta& Correction,
  const uint64 Generation,
  const uint64 WorkerEpoch)
{
  if (!bInitialized)
    return ECrowdWorkerMovementAcceptResult::RejectedNotInitialized;
  if (Metrics.bViolation)
    return ECrowdWorkerMovementAcceptResult::Violation;
  if (Generation != Metrics.Generation)
    return ECrowdWorkerMovementAcceptResult::RejectedGeneration;
  if (!CurrentEntities.Contains(Correction.EntityRef))
    return ECrowdWorkerMovementAcceptResult::RejectedLifecycle;
  if (!IsWorkerOwner(Correction.EntityRef))
    return ECrowdWorkerMovementAcceptResult::RejectedOwner;
  if ((Correction.DirtyMask & CrowdWorkerMovementFields::Movement)
      != CrowdWorkerMovementFields::Movement
    || (Correction.DirtyMask
      & ~CrowdWorkerMovementFields::Movement) != 0
    || Correction.CorrectionRevision == 0
    || WorkerEpoch == 0)
    return ECrowdWorkerMovementAcceptResult::RejectedPayload;
  FCrowdWorkerMovementState Movement;
  if (!FCrowdWorkerMovementStateCodec::Decode(
      Correction.FullState, Movement)
    || Movement.CorrectionRevision
      != Correction.CorrectionRevision)
    return ECrowdWorkerMovementAcceptResult::RejectedPayload;
  FHistory& History = Histories.FindOrAdd(Correction.EntityRef);
  if (History.bHasCurrent
    && Correction.CorrectionRevision
      <= History.Current.CorrectionRevision)
    return ECrowdWorkerMovementAcceptResult::
      RejectedCorrectionRevision;
  History = {};
  History.Current = Movement;
  History.CurrentWorkerEpoch = WorkerEpoch;
  History.CurrentInputSequence = Correction.InputSequence;
  History.StateRevision = History.Current.CorrectionRevision;
  History.bHasCurrent = true;
  ++Metrics.AcceptedCorrectionCount;
  RefreshMetrics();
  return ECrowdWorkerMovementAcceptResult::AcceptedCorrection;
}

bool FCrowdWorkerMovementAuthority::CompareShadow(
  const FCrowdStableEntityRef& EntityRef,
  const FCrowdWorkerMovementState& Expected,
  const double PositionToleranceCm,
  const double VelocityToleranceCmps,
  const double YawToleranceDegrees)
{
  if (!bInitialized || Metrics.bViolation
    || Mode != ECrowdWorkerMovementAuthorityMode::Shadow
    || !CurrentEntities.Contains(EntityRef)
    || !Expected.IsValid()
    || PositionToleranceCm < 0.0
    || VelocityToleranceCmps < 0.0
    || YawToleranceDegrees < 0.0)
    return false;
  const FHistory* History = Histories.Find(EntityRef);
  ++Metrics.ShadowCompareCount;
  const bool bMatch = History && History->bHasCurrent
    && History->Current.Position.Equals(
      Expected.Position, PositionToleranceCm)
    && History->Current.Velocity.Equals(
      Expected.Velocity, VelocityToleranceCmps)
    && FMath::Abs(FMath::FindDeltaAngleDegrees(
      History->Current.YawDegrees, Expected.YawDegrees))
        <= YawToleranceDegrees
    && History->Current.CorrectionRevision
      == Expected.CorrectionRevision;
  if (!bMatch) ++Metrics.ShadowMismatchCount;
  return bMatch;
}

bool FCrowdWorkerMovementAuthority::Sample(
  const FCrowdStableEntityRef& EntityRef,
  const double RenderSimulationTimeSeconds,
  FCrowdWorkerMovementSample& OutSample) const
{
  OutSample = {};
  if (!bInitialized || Metrics.bViolation
    || !FMath::IsFinite(RenderSimulationTimeSeconds))
    return false;
  const FHistory* History = Histories.Find(EntityRef);
  if (!History || !History->bHasCurrent) return false;
  const FCrowdWorkerMovementState& Current = History->Current;
  OutSample.EntityRef = EntityRef;
  OutSample.Position = Current.Position;
  OutSample.Velocity = Current.Velocity;
  OutSample.YawDegrees = Current.YawDegrees;
  OutSample.SimulationTimeSeconds = Current.SimulationTimeSeconds;
  OutSample.WorkerEpoch = History->CurrentWorkerEpoch;
  OutSample.SourceInputSequence = History->CurrentInputSequence;
  OutSample.CorrectionRevision = Current.CorrectionRevision;
  if (History->bHasPrevious
    && Current.SimulationTimeSeconds
      > History->Previous.SimulationTimeSeconds
    && RenderSimulationTimeSeconds
      < Current.SimulationTimeSeconds)
  {
    const double Alpha = FMath::Clamp(
      (RenderSimulationTimeSeconds
        - History->Previous.SimulationTimeSeconds)
      / (Current.SimulationTimeSeconds
        - History->Previous.SimulationTimeSeconds),
      0.0, 1.0);
    OutSample.Position = FMath::Lerp(
      History->Previous.Position, Current.Position, Alpha);
    OutSample.Velocity = FMath::Lerp(
      History->Previous.Velocity, Current.Velocity, Alpha);
    OutSample.YawDegrees = FMath::Lerp(
      History->Previous.YawDegrees,
      History->Previous.YawDegrees
        + FMath::FindDeltaAngleDegrees(
          History->Previous.YawDegrees, Current.YawDegrees),
      static_cast<float>(Alpha));
    OutSample.SimulationTimeSeconds =
      RenderSimulationTimeSeconds;
    OutSample.bInterpolated = true;
  }
  OutSample.bValid = true;
  return true;
}

void FCrowdWorkerMovementAuthority::LatchViolation()
{
  Metrics.bViolation = true;
}

void FCrowdWorkerMovementAuthority::RefreshMetrics()
{
  Metrics.CanaryEntityCount = CanaryEntities.Num();
  Metrics.StateCount = Histories.Num();
}
