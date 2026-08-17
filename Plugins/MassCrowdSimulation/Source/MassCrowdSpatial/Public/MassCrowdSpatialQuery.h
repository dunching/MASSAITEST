#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

struct MASSCROWDSPATIAL_API FCrowdSpatialBodySnapshot
{
  FCrowdStableEntityRef EntityRef;
  FVector StartPosition = FVector::ZeroVector;
  FVector EndPosition = FVector::ZeroVector;
  float RadiusCm = 0.0f;
  uint32 NavLayer = 0;
  uint32 CollisionMask = MAX_uint32;
  uint32 QueryMask = MAX_uint32;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDSPATIAL_API FCrowdSpatialEnvironmentBody
{
  uint64 StableSurfaceId = 0;
  uint32 NavLayer = 0;
  FVector BoundsMin = FVector::ZeroVector;
  FVector BoundsMax = FVector::ZeroVector;
  uint32 CollisionMask = MAX_uint32;
  uint32 QueryMask = MAX_uint32;
  uint32 CollisionProfileId = 0;
  uint32 EffectProfileId = 0;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDSPATIAL_API FCrowdSpatialSweepHit
{
  FCrowdStableEntityRef Target;
  uint64 StableSurfaceId = 0;
  FVector Position = FVector::ZeroVector;
  FVector Normal = FVector::UpVector;
  uint32 CollisionProfileId = 0;
  uint32 EffectProfileId = 0;
  uint32 TimeOfImpactQ = 0;

  bool IsEnvironment() const
  {
    return StableSurfaceId != 0;
  }
};

class MASSCROWDSPATIAL_API ICrowdEnvironmentCollisionSnapshotProvider
{
public:
  virtual ~ICrowdEnvironmentCollisionSnapshotProvider() = default;

  virtual bool Gather(
    int64 FixedStepIndex,
    TArray<FCrowdSpatialEnvironmentBody>& OutBodies) const = 0;
};

class MASSCROWDSPATIAL_API FCrowdSpatialQueryIndex
{
public:
  bool Build(
    TConstArrayView<FCrowdSpatialBodySnapshot> InBodies,
    float InCellSizeCm);

  bool GatherCandidates(
    const FVector& Start,
    const FVector& End,
    float RadiusCm,
    uint32 NavLayer,
    uint32 QueryMask,
    TArray<int32>& OutBodyIndices) const;

  const FCrowdSpatialBodySnapshot& GetBodyChecked(
    int32 BodyIndex) const;
  int32 Num() const { return Bodies.Num(); }

private:
  struct FCellKey
  {
    int32 X = 0;
    int32 Y = 0;
    int32 Z = 0;
    uint32 NavLayer = 0;

    bool operator==(const FCellKey&) const = default;
    friend uint32 GetTypeHash(const FCellKey& Key)
    {
      uint32 Hash = HashCombineFast(
        ::GetTypeHash(Key.X), ::GetTypeHash(Key.Y));
      Hash = HashCombineFast(Hash, ::GetTypeHash(Key.Z));
      return HashCombineFast(Hash, ::GetTypeHash(Key.NavLayer));
    }
  };

  int32 GridCoordinate(double Value) const;
  void AddSweptBounds(int32 BodyIndex);

  TArray<FCrowdSpatialBodySnapshot> Bodies;
  TMap<FCellKey, TArray<int32>> BodyIndicesByCell;
  float CellSizeCm = 0.0f;
};

class MASSCROWDSPATIAL_API FCrowdSpatialSweep
{
public:
  static bool MovingSphere(
    const FVector& QueryStart,
    const FVector& QueryEnd,
    float QueryRadiusCm,
    const FCrowdSpatialBodySnapshot& Target,
    FCrowdSpatialSweepHit& OutHit);

  static bool EnvironmentAabb(
    const FVector& QueryStart,
    const FVector& QueryEnd,
    float QueryRadiusCm,
    const FCrowdSpatialEnvironmentBody& Environment,
    FCrowdSpatialSweepHit& OutHit);

  static bool IsEarlierStableHit(
    const FCrowdSpatialSweepHit& Candidate,
    const FCrowdSpatialSweepHit& Current);
};
