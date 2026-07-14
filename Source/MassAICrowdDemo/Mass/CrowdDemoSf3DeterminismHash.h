#pragma once

#include "CoreMinimal.h"

enum class ECrowdDemoSf3DeterminismStage : uint8
{
  PlanApplyInput = 0,
  TrafficField,
  PortalSchedule,
  FlowPreferredVelocity,
  PassingBandGuidance,
  DeterministicOrca,
  MovementPredict,
  ObstacleConstraint,
  HardPbd,
  ObstacleReproject,
  FinalState,
  Count
};

struct FCrowdDemoSf3StageHash
{
  uint32 Hash = 0;
  int32 ItemCount = 0;
  int32 FixedStepIndex = INDEX_NONE;
  TArray<int32> StableKeys;
};

class MASSAICROWDDEMO_API FCrowdDemoSf3DeterminismHashBuilder
{
public:
  FCrowdDemoSf3DeterminismHashBuilder(int32 FixedStepIndex, int32 ItemCount);

  void AddInt(int32 Value);
  void AddUInt(uint32 Value);
  void AddBool(bool Value) { AddInt(Value ? 1 : 0); }
  void AddPosition(const FVector& Value);
  void AddVelocity(const FVector& Value);
  void AddDirection(const FVector& Value);
  void AddDirection(const FVector2f& Value);
  uint32 Finalize() const { return Hash; }

  static const TCHAR* StageName(ECrowdDemoSf3DeterminismStage Stage);

private:
  uint32 Hash = 2166136261u;
};
