#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

struct FCrowdClientView
{
  uint64 ClientKey = 0;
  FVector Location = FVector::ZeroVector;
  float RelevantRadiusCm = 0.0f;
  uint32 RelationshipMask = 0;
};

struct FCrowdAuthorityLocationRecord
{
  FCrowdStableEntityRef EntityRef;
  FVector Location = FVector::ZeroVector;
  uint32 RelationshipBits = 0;
};

struct FCrowdRelationshipEdge
{
  FCrowdStableEntityRef From;
  FCrowdStableEntityRef To;
  uint32 RelationshipBits = 0;
};

struct FCrowdRelevantSetLimits
{
  float CellSizeCm = 1000.0f;
  int32 MaxIndexedEntities = 65536;
  int32 MaxRelevantEntities = 2048;
  int32 MaxRelationshipEdges = 8192;
  int32 MaxClosureDepth = 2;
  int32 MaxQueryBuckets = 65536;

  bool IsValid() const;
};

struct FCrowdRelevantSetResult
{
  TArray<FCrowdStableEntityRef> EntityRefs;
  uint64 StableHash = 0;
  bool bValid = false;
};

struct FCrowdRelevantSetIndexUpdate
{
  TArray<FCrowdAuthorityLocationRecord> Upserts;
  TArray<FCrowdStableEntityRef> Removes;
  TArray<FCrowdRelationshipEdge> Relationships;
  bool bReplaceRelationships = false;
};

class MASSCROWDNETWORKING_API ICrowdRelevantSetProvider
{
public:
  virtual ~ICrowdRelevantSetProvider() = default;

  virtual bool RebuildIndex(
    TConstArrayView<FCrowdAuthorityLocationRecord> Locations,
    TConstArrayView<FCrowdRelationshipEdge> Relationships) = 0;
  virtual bool ApplyIndexUpdate(
    const FCrowdRelevantSetIndexUpdate& Update) = 0;
  virtual bool BuildRelevantSet(
    const FCrowdClientView& View,
    FCrowdRelevantSetResult& OutResult) const = 0;
};

class MASSCROWDNETWORKING_API FCrowdSpatialGridRelevantSetProvider final
  : public ICrowdRelevantSetProvider
{
public:
  explicit FCrowdSpatialGridRelevantSetProvider(
    const FCrowdRelevantSetLimits& Limits);

  virtual bool RebuildIndex(
    TConstArrayView<FCrowdAuthorityLocationRecord> Locations,
    TConstArrayView<FCrowdRelationshipEdge> Relationships) override;
  virtual bool ApplyIndexUpdate(
    const FCrowdRelevantSetIndexUpdate& Update) override;
  virtual bool BuildRelevantSet(
    const FCrowdClientView& View,
    FCrowdRelevantSetResult& OutResult) const override;

private:
  struct FRelationship
  {
    FCrowdStableEntityRef Other;
    uint32 RelationshipBits = 0;
  };

  bool TryCellForLocation(
    const FVector& Location, FIntVector& OutCell) const;
  bool BuildRelationships(
    TConstArrayView<FCrowdRelationshipEdge> Relationships,
    const TMap<FCrowdStableEntityRef, FCrowdAuthorityLocationRecord>&
      CandidateLocations,
    TMap<FCrowdStableEntityRef, TArray<FRelationship>>&
      OutRelationships) const;

  FCrowdRelevantSetLimits Limits;
  TMap<FCrowdStableEntityRef, FCrowdAuthorityLocationRecord> LocationsByRef;
  TMap<FIntVector, TArray<FCrowdStableEntityRef>> Buckets;
  TMap<FCrowdStableEntityRef, TArray<FRelationship>> RelationshipsByRef;
};
