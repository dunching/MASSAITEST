#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerContracts.h"

namespace CrowdWorkerMovementFields
{
  constexpr uint64 Position = 1ull << 8;
  constexpr uint64 Velocity = 1ull << 9;
  constexpr uint64 Facing = 1ull << 10;
  constexpr uint64 Movement =
    Position | Velocity | Facing;
}

enum class ECrowdWorkerMovementAuthorityMode : uint8
{
  Shadow = 0,
  Canary,
  Production
};

enum class ECrowdWorkerMovementAcceptResult : uint8
{
  Accepted = 0,
  AcceptedCorrection,
  RejectedNotInitialized,
  RejectedGeneration,
  RejectedLifecycle,
  RejectedOwner,
  RejectedSequence,
  RejectedCorrectionRevision,
  RejectedPayload,
  Violation
};

struct MASSCROWDRUNTIME_API FCrowdWorkerMovementState
{
  FVector StartPosition = FVector::ZeroVector;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  double SimulationTimeSeconds = 0.0;
  uint64 CorrectionRevision = 0;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerMovementStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574D56u;
  static constexpr uint16 SchemaVersion = 2;
  static constexpr int32 EncodedByteCount =
    sizeof(double) * 10 + sizeof(float) + sizeof(uint64);

  static bool Encode(
    const FCrowdWorkerMovementState& State,
    FCrowdWorkerPayload& OutPayload);

  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerMovementState& OutState);
};

struct MASSCROWDRUNTIME_API FCrowdWorkerMovementSample
{
  FCrowdStableEntityRef EntityRef;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  double SimulationTimeSeconds = 0.0;
  uint64 WorkerEpoch = 0;
  uint64 SourceInputSequence = 0;
  uint64 CorrectionRevision = 0;
  bool bInterpolated = false;
  bool bValid = false;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerMovementAuthorityMetrics
{
  uint64 Generation = 0;
  uint64 AcceptedPatchCount = 0;
  uint64 AcceptedCorrectionCount = 0;
  uint64 RejectedEchoCount = 0;
  uint64 RejectedPatchCount = 0;
  uint64 ShadowCompareCount = 0;
  uint64 ShadowMismatchCount = 0;
  int32 CanaryEntityCount = 0;
  int32 StateCount = 0;
  bool bViolation = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerMovementAuthority
{
public:
  bool ResetQuiescent(
    uint64 Generation,
    ECrowdWorkerMovementAuthorityMode Mode,
    TConstArrayView<FCrowdStableEntityRef> CanaryEntities = {});

  bool UpdateCurrentEntities(
    uint64 Generation,
    TConstArrayView<FCrowdStableEntityRef> EntityRefs);

  bool IsWorkerOwner(const FCrowdStableEntityRef& EntityRef) const;
  ECrowdWorkerMovementAuthorityMode GetMode() const
  {
    return Mode;
  }

  bool ValidateNormalInput(
    const FCrowdStableEntityRef& EntityRef,
    uint64 DirtyMask);

  ECrowdWorkerMovementAcceptResult AcceptPatch(
    const FCrowdWorkerStatePatch& Patch);

  ECrowdWorkerMovementAcceptResult ApplyCorrection(
    const FCrowdWorkerCorrectionDelta& Correction,
    uint64 Generation,
    uint64 WorkerEpoch);

  bool CompareShadow(
    const FCrowdStableEntityRef& EntityRef,
    const FCrowdWorkerMovementState& Expected,
    double PositionToleranceCm,
    double VelocityToleranceCmps,
    double YawToleranceDegrees);

  bool Sample(
    const FCrowdStableEntityRef& EntityRef,
    double RenderSimulationTimeSeconds,
    FCrowdWorkerMovementSample& OutSample) const;

  const FCrowdWorkerMovementAuthorityMetrics& GetMetrics() const
  {
    return Metrics;
  }

private:
  struct FHistory
  {
    FCrowdWorkerMovementState Previous;
    FCrowdWorkerMovementState Current;
    uint64 PreviousWorkerEpoch = 0;
    uint64 CurrentWorkerEpoch = 0;
    uint64 PreviousInputSequence = 0;
    uint64 CurrentInputSequence = 0;
    uint64 StateRevision = 0;
    bool bHasPrevious = false;
    bool bHasCurrent = false;
  };

  void LatchViolation();
  void RefreshMetrics();

  TSet<FCrowdStableEntityRef> CurrentEntities;
  TSet<FCrowdStableEntityRef> CanaryEntities;
  TMap<FCrowdStableEntityRef, FHistory> Histories;
  FCrowdWorkerMovementAuthorityMetrics Metrics;
  ECrowdWorkerMovementAuthorityMode Mode =
    ECrowdWorkerMovementAuthorityMode::Shadow;
  bool bInitialized = false;
};
