#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerRuntimeV2.h"

// Domain-neutral projection consumed by later Worker stages. Host-specific
// attack/business state remains opaque but travels in the same atomic patch.
struct MASSCROWDRUNTIME_API FCrowdWorkerCombatState
{
  int64 SourceFixedStep = INDEX_NONE;
  bool bAlive = true;
  bool bReactiveActive = false;
  bool bMovementLocked = false;
  FVector HorizontalReactiveVelocity = FVector::ZeroVector;
  float ProposedZ = 0.0f;
  float VerticalVelocityCmps = 0.0f;
  uint64 LastConsumedHitEventId = 0;
  FCrowdWorkerPayload HostState;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerCombatStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574353u;
  static constexpr uint16 SchemaVersion = 2;
  static constexpr int32 MaxEncodedBytes = 64 * 1024;

  static bool Encode(
    const FCrowdWorkerCombatState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerCombatState& OutState);
};
