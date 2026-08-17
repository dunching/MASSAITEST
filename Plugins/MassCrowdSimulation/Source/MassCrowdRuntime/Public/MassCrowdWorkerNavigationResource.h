#pragma once

#include "CoreMinimal.h"
#include "MassCrowdNavRuntime.h"
#include "MassCrowdWorkerContracts.h"

namespace CrowdWorkerResourceIds
{
  constexpr uint64 NavTopology = 0x43574E4156544F50ull;
  constexpr uint64 Environment = 0x4357454E56524F4Eull;
  constexpr uint64 SimulationRules = 0x435752554C455301ull;
  constexpr uint64 Objective = 0x43574F424A454354ull;
}

class MASSCROWDRUNTIME_API FCrowdWorkerNavTopologyCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574E54u;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr int32 MaxEncodedBytes =
    32 * 1024 * 1024;

  static bool Encode(
    const FCrowdNavGraphResource& Resource,
    FCrowdWorkerPayload& OutPayload);

  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    uint32& OutTopologyRevision,
    FCrowdNavSurfaceGraph& OutGraph);
};
