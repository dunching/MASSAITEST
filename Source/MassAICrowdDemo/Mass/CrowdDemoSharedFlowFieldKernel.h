#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

enum class ECrowdDemoFlowLocationStatus : uint8
{
  Reachable,
  OutOfBounds,
  BlockedRasterCell,
  UnreachableFreeCell
};

struct FCrowdDemoSharedFlowSample
{
  int32 CellIndex = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  ECrowdDemoFlowLocationStatus Status = ECrowdDemoFlowLocationStatus::OutOfBounds;
  FVector FlowDirection = FVector::ZeroVector;
  int32 IntegrationCost = MAX_int32;
  bool bBlocked = false;
  bool bUnreachable = true;
  bool bRecoveredFromRasterMismatch = false;
};

struct FCrowdDemoReachableFlowCellSearchResult
{
  bool bFound = false;
  int32 CellIndex = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  int32 RingDistance = MAX_int32;
  float WorldDistanceCm = MAX_flt;
  int32 IntegrationCost = MAX_int32;
  FVector CellCenter = FVector::ZeroVector;
  FVector FlowDirection = FVector::ZeroVector;
};

struct FCrowdDemoSharedFlowConstraintResult
{
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  bool bHitObstacle = false;
  bool bPenetrating = false;
  bool bHitFlowBounds = false;
  float FlowBoundsReprojectDeltaCm = 0.0f;
};

struct FCrowdDemoSharedFlowConstraintDiagnostic
{
  bool bValid = false;
  FVector Start = FVector::ZeroVector;
  FVector Proposed = FVector::ZeroVector;
  FVector DomainProposed = FVector::ZeroVector;
  FVector SlideX = FVector::ZeroVector;
  FVector SlideY = FVector::ZeroVector;
  float FlowBoundsReprojectDeltaCm = 0.0f;
  bool bHitFlowBounds = false;
  bool bStartInsideAnyObstacle = false;
  bool bEndInsideAnyObstacle = false;
  bool bDirectSegmentClear = false;
  bool bSlideXClear = false;
  bool bSlideYClear = false;
  int32 SelectedObstacleId = INDEX_NONE;
  FVector SelectedInflatedMin = FVector::ZeroVector;
  FVector SelectedInflatedMax = FVector::ZeroVector;
  float SelectedSegmentEntryT = -1.0f;
  float SelectedSegmentExitT = -1.0f;
  bool bStartInsideSelectedObstacle = false;
  bool bEndInsideSelectedObstacle = false;
  TArray<int32> IntersectedObstacleIds;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoSharedFlowField
{
  FCrowdDemoSharedFlowFieldConfig Config;
  TArray<int32> IntegrationCost;
  TArray<FVector> FlowDirection;
  TArray<int32> NextCellIndex;
  TBitArray<> Blocked;
  TBitArray<> Unreachable;
  int32 Width = 0;
  int32 Height = 0;
  int32 GoalCellIndex = INDEX_NONE;
  int32 BlockedCellCount = 0;
  uint32 BuildHash = 0;

  void Reset();
  bool IsValid() const;
  int32 LocationToCellIndex(const FVector& Location) const;
  FVector CellCenter(int32 CellIndex) const;
};

class MASSAICROWDDEMO_API FCrowdDemoSharedFlowFieldKernel
{
public:
  static FCrowdDemoSharedFlowFieldConfig MakeSf1Config(int32 Revision = 1);
  static bool Build(const FCrowdDemoSharedFlowFieldConfig& Config, FCrowdDemoSharedFlowField& OutField);
  static FCrowdDemoSharedFlowSample Sample(const FCrowdDemoSharedFlowField& Field, const FVector& Location);
  static FCrowdDemoReachableFlowCellSearchResult FindNearestReachableCell(
    const FCrowdDemoSharedFlowField& Field,
    const FVector& Location,
    int32 MaximumRingDistance = 8);
  static FCrowdDemoSharedFlowConstraintResult ConstrainMovement(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& Proposed,
    float FixedStepSeconds,
    bool bConstrainToFlowBounds = false);
  static FCrowdDemoSharedFlowConstraintDiagnostic DiagnoseMovementConstraint(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& Proposed,
    bool bConstrainToFlowBounds = false);
  static bool IsInsideInflatedObstacle(const FCrowdDemoSharedFlowFieldConfig& Config, const FVector& Location);
};
