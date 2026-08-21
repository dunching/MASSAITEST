#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerRuntimeV2.h"

namespace CrowdWorkerResourceIds
{
  // Generic SharedFlow resources occupy bit 61. Existing fixed resources and
  // ExternalGameplayInput occupy bit 62, while ObjectiveRevision occupies bit
  // 63. Flow keys are caller-owned stable identities, not scenario identities.
  constexpr uint64 FlowResourceNamespace = 0x2000000000000000ull;
  constexpr uint64 FlowResourceKeyMask = 0x1fffffffffffffffull;

  constexpr uint64 FlowResource(const uint64 FlowKey)
  {
    return FlowKey != 0 && (FlowKey & ~FlowResourceKeyMask) == 0
      ? FlowResourceNamespace | FlowKey
      : 0;
  }

  constexpr bool IsFlowResource(const uint64 ResourceId)
  {
    return (ResourceId & ~FlowResourceKeyMask) == FlowResourceNamespace
      && (ResourceId & FlowResourceKeyMask) != 0;
  }
}

// Stable objective identity. Its current version is the independently
// revisioned resource at CrowdWorkerResourceIds::ObjectiveRevision(ObjectiveId).
struct MASSCROWDRUNTIME_API FCrowdWorkerObjectiveRef
{
  uint64 ObjectiveId = 0;

  bool IsValid() const
  {
    return ObjectiveId != 0
      && (ObjectiveId
        & CrowdWorkerResourceIds::ObjectiveRevisionNamespace) == 0;
  }

  uint64 ResolveResourceId() const
  {
    return IsValid()
      ? CrowdWorkerResourceIds::ObjectiveRevision(ObjectiveId)
      : 0;
  }

  bool operator==(const FCrowdWorkerObjectiveRef& Other) const = default;
};

// Entity-level association describing which objective/cohort navigation
// context supplies SharedFlow. CohortKey is explicit, stable grouping metadata
// for a shared macro objective/navigation context; it is not an AgentId or a
// FormationIndex and does not own scheduling or capacity. Movement capability
// remains in MovementProfile.
struct MASSCROWDRUNTIME_API FCrowdWorkerFlowBinding
{
  FCrowdStableEntityRef EntityRef;
  FCrowdWorkerObjectiveRef ObjectiveRef;
  uint32 CohortKey = 0;
  uint64 FlowResourceId = 0;

  bool IsValid() const
  {
    return EntityRef.IsValid()
      && ObjectiveRef.IsValid()
      && CohortKey != 0
      && CrowdWorkerResourceIds::IsFlowResource(FlowResourceId);
  }

  bool operator==(const FCrowdWorkerFlowBinding& Other) const = default;
};

class MASSCROWDRUNTIME_API FCrowdWorkerFlowBindingCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574642u;
  static constexpr uint16 SchemaVersion = 1;
  static constexpr uint32 ClearSchemaId = 0x43574643u;

  static bool Encode(
    const FCrowdWorkerFlowBinding& Binding,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerFlowBinding& OutBinding);
  static bool EncodeClear(FCrowdWorkerPayload& OutPayload);
  static bool IsClearPayload(const FCrowdWorkerPayload& Payload);
};
