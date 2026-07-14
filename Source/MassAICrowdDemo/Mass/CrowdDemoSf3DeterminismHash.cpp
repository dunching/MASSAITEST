#include "Mass/CrowdDemoSf3DeterminismHash.h"

FCrowdDemoSf3DeterminismHashBuilder::FCrowdDemoSf3DeterminismHashBuilder(
  const int32 FixedStepIndex,
  const int32 ItemCount)
{
  AddInt(FixedStepIndex);
  AddInt(ItemCount);
}

void FCrowdDemoSf3DeterminismHashBuilder::AddInt(const int32 Value)
{
  AddUInt(static_cast<uint32>(Value));
}

void FCrowdDemoSf3DeterminismHashBuilder::AddUInt(const uint32 Value)
{
  for (int32 Shift = 0; Shift < 32; Shift += 8)
  {
    Hash ^= (Value >> Shift) & 0xffu;
    Hash *= 16777619u;
  }
}

void FCrowdDemoSf3DeterminismHashBuilder::AddPosition(const FVector& Value)
{
  AddInt(FMath::RoundToInt(Value.X));
  AddInt(FMath::RoundToInt(Value.Y));
  AddInt(FMath::RoundToInt(Value.Z));
}

void FCrowdDemoSf3DeterminismHashBuilder::AddVelocity(const FVector& Value)
{
  AddInt(FMath::RoundToInt(Value.X));
  AddInt(FMath::RoundToInt(Value.Y));
  AddInt(FMath::RoundToInt(Value.Z));
}

void FCrowdDemoSf3DeterminismHashBuilder::AddDirection(const FVector& Value)
{
  AddInt(FMath::RoundToInt(Value.GetSafeNormal2D().X * 32767.0f));
  AddInt(FMath::RoundToInt(Value.GetSafeNormal2D().Y * 32767.0f));
}

void FCrowdDemoSf3DeterminismHashBuilder::AddDirection(const FVector2f& Value)
{
  const FVector2f Direction = Value.GetSafeNormal();
  AddInt(FMath::RoundToInt(Direction.X * 32767.0f));
  AddInt(FMath::RoundToInt(Direction.Y * 32767.0f));
}

const TCHAR* FCrowdDemoSf3DeterminismHashBuilder::StageName(
  const ECrowdDemoSf3DeterminismStage Stage)
{
  switch (Stage)
  {
  case ECrowdDemoSf3DeterminismStage::PlanApplyInput: return TEXT("PlanApplyInput");
  case ECrowdDemoSf3DeterminismStage::TrafficField: return TEXT("TrafficField");
  case ECrowdDemoSf3DeterminismStage::PortalSchedule: return TEXT("PortalSchedule");
  case ECrowdDemoSf3DeterminismStage::FlowPreferredVelocity: return TEXT("FlowPreferredVelocity");
  case ECrowdDemoSf3DeterminismStage::PassingBandGuidance: return TEXT("PassingBandGuidance");
  case ECrowdDemoSf3DeterminismStage::DeterministicOrca: return TEXT("DeterministicORCA");
  case ECrowdDemoSf3DeterminismStage::MovementPredict: return TEXT("MovementPredict");
  case ECrowdDemoSf3DeterminismStage::ObstacleConstraint: return TEXT("ObstacleConstraint");
  case ECrowdDemoSf3DeterminismStage::HardPbd: return TEXT("HardPBD");
  case ECrowdDemoSf3DeterminismStage::ObstacleReproject: return TEXT("ObstacleReproject");
  case ECrowdDemoSf3DeterminismStage::FinalState: return TEXT("FinalState");
  default: return TEXT("Unknown");
  }
}
