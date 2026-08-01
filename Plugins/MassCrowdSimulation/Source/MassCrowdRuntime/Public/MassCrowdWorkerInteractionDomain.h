#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerRuntimeV2.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerInteractionPairKey
{
  FCrowdStableEntityRef A;
  FCrowdStableEntityRef B;

  bool Normalize();
  bool IsValid() const;
  bool operator==(const FCrowdWorkerInteractionPairKey& Other)
    const = default;
  bool operator<(const FCrowdWorkerInteractionPairKey& Other) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerSpatialEntry
{
  FCrowdStableEntityRef EntityRef;
  FVector Position = FVector::ZeroVector;
  float PhysicalRadiusCm = 0.0f;
  float HardSafetyGapCm = 0.0f;
  float SoftMarginCm = 0.0f;
  float Mobility = 0.0f;
};

class MASSCROWDRUNTIME_API FCrowdWorkerSpatialIndex
{
public:
  bool Reset(
    int32 InMaxEntities,
    float InCellSizeCm = 400.0f);
  bool Rebuild(const FCrowdWorkerEntityStateStore& States);
  bool Spawn(
    const FCrowdWorkerEntityStateStore& States,
    const FCrowdStableEntityRef& EntityRef);
  bool Despawn(const FCrowdStableEntityRef& EntityRef);
  bool UpdateEntity(
    const FCrowdWorkerEntityStateStore& States,
    const FCrowdStableEntityRef& EntityRef);
  bool QueryNeighbors(
    const FCrowdStableEntityRef& EntityRef,
    float RadiusCm,
    TArray<FCrowdWorkerSpatialEntry>& OutNeighbors) const;
  const FCrowdWorkerSpatialEntry* Find(
    const FCrowdStableEntityRef& EntityRef) const;
  int32 Num() const { return Entries.Num(); }
  uint64 GetFullRebuildCount() const { return FullRebuildCount; }
  uint64 GetIncrementalUpdateCount() const
  {
    return IncrementalUpdateCount;
  }
  uint64 GetCellMigrationCount() const { return CellMigrationCount; }
  uint64 CalculateStableHash() const;

private:
  FIntVector CellFor(const FVector& Position) const;

  int32 MaxEntities = 0;
  float CellSizeCm = 400.0f;
  TMap<FCrowdStableEntityRef, FCrowdWorkerSpatialEntry> Entries;
  TMap<FIntVector, TArray<FCrowdStableEntityRef>> Cells;
  uint64 FullRebuildCount = 0;
  uint64 IncrementalUpdateCount = 0;
  uint64 CellMigrationCount = 0;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerParticleState
{
  FVector PositionOffset = FVector::ZeroVector;
  FVector VelocityDelta = FVector::ZeroVector;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerParticleStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575049u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerParticleState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerParticleState& OutState);
};

class MASSCROWDRUNTIME_API
FCrowdWorkerParticleInteractionDomainExecutor final
  : public ICrowdWorkerDomainExecutor
{
public:
  virtual ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::ParticleInteraction;
  }

  virtual void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const override;
  virtual bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;
};

class MASSCROWDRUNTIME_API
FCrowdWorkerFacingFinalizeDomainExecutor final
  : public ICrowdWorkerDomainExecutor
{
public:
  virtual ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::FacingFinalize;
  }

  virtual void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const override;
  virtual bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;
};
