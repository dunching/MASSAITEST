#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSourceRuntime.h"

namespace CrowdBehaviorFixture
{
  inline constexpr FCrowdBehaviorProviderId ProviderId{0xF1000001u};
  inline constexpr FCrowdCapabilityId CapabilityId{60001u};
  inline constexpr FCrowdCapabilityProfileKey ProfileKey{60001u};
  inline constexpr FCrowdBehaviorSourceTypeId SourceTypeId{60001u};
  inline constexpr FCrowdBehaviorSourceTypeId
    ResolvedOnlySourceTypeId{60002u};
  inline constexpr FCrowdBehaviorSourceTypeId
    ServerOnlySourceTypeId{60003u};
  inline constexpr FCrowdBehaviorContextTypeId ContextTypeId{60001u};
  inline constexpr uint32 PayloadSchemaId = 60001u;
  inline constexpr uint32 StateSchemaId = 60002u;

  struct FPayload
  {
    FVector DesiredVelocity = FVector::ZeroVector;
    FCrowdStableEntityRef TargetRef;
    uint64 CommitId = 0;
  };

  struct FContext
  {
    int32 VelocityScaleQ15 = CrowdBehavior::FullQ15Weight;
  };

  struct FState
  {
    uint32 EvaluationCount = 0;
  };

  static_assert(std::is_trivially_copyable_v<FPayload>);
  static_assert(sizeof(FPayload) <= CrowdBehavior::MaxPayloadBytes);
  static_assert(std::is_trivially_copyable_v<FContext>);
  static_assert(sizeof(FContext) <= CrowdBehavior::MaxContextRecordBytes);
  static_assert(std::is_trivially_copyable_v<FState>);
  static_assert(sizeof(FState) <= CrowdBehavior::MaxStateBytes);
}
