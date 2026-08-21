#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerContracts.h"

// Generic navigation objective resource referenced by FCrowdWorkerObjectiveRef.
// Objective identity and revision remain in the resource store; this payload
// carries only the navigation goal required by fixture and production callers.
struct MASSCROWDRUNTIME_API FCrowdWorkerNavigationObjectiveResource
{
  FVector GoalLocation = FVector::ZeroVector;

  bool IsValid() const
  {
    return !GoalLocation.ContainsNaN();
  }

  bool operator==(
    const FCrowdWorkerNavigationObjectiveResource& Other) const = default;
};

class MASSCROWDRUNTIME_API FCrowdWorkerNavigationObjectiveResourceCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574E4Fu;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerNavigationObjectiveResource& Objective,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerNavigationObjectiveResource& OutObjective);
};
