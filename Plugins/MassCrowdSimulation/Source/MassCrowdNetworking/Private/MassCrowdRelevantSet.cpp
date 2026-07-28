#include "MassCrowdRelevantSet.h"

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  uint64 Fold(uint64 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffull);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  uint64 FoldRef(uint64 Hash, const FCrowdStableEntityRef& Ref)
  {
    Hash = Fold(Hash, Ref.ProviderId);
    Hash = Fold(Hash, Ref.StableEntityId);
    return Fold(Hash, Ref.LifecycleSerial);
  }

  bool IsFinite(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }
}

bool FCrowdRelevantSetLimits::IsValid() const
{
  return FMath::IsFinite(CellSizeCm) && CellSizeCm > 0.0f
    && MaxIndexedEntities > 0 && MaxRelevantEntities > 0
    && MaxRelevantEntities <= MaxIndexedEntities
    && MaxRelationshipEdges >= 0
    && MaxClosureDepth >= 0 && MaxClosureDepth <= 8
    && MaxQueryBuckets > 0;
}

FCrowdSpatialGridRelevantSetProvider::FCrowdSpatialGridRelevantSetProvider(
  const FCrowdRelevantSetLimits& InLimits)
  : Limits(InLimits)
{
}

bool FCrowdSpatialGridRelevantSetProvider::TryCellForLocation(
  const FVector& Location, FIntVector& OutCell) const
{
  if (!Limits.IsValid() || !IsFinite(Location))
    return false;
  const double InvCellSize = 1.0 / static_cast<double>(Limits.CellSizeCm);
  const int64 X = FMath::FloorToInt64(Location.X * InvCellSize);
  const int64 Y = FMath::FloorToInt64(Location.Y * InvCellSize);
  const int64 Z = FMath::FloorToInt64(Location.Z * InvCellSize);
  if (X < MIN_int32 || X > MAX_int32
    || Y < MIN_int32 || Y > MAX_int32
    || Z < MIN_int32 || Z > MAX_int32)
    return false;
  OutCell = FIntVector(
    static_cast<int32>(X), static_cast<int32>(Y), static_cast<int32>(Z));
  return true;
}

bool FCrowdSpatialGridRelevantSetProvider::BuildRelationships(
  const TConstArrayView<FCrowdRelationshipEdge> Relationships,
  const TMap<FCrowdStableEntityRef, FCrowdAuthorityLocationRecord>&
    CandidateLocations,
  TMap<FCrowdStableEntityRef, TArray<FRelationship>>&
    OutRelationships) const
{
  OutRelationships.Reset();
  if (Relationships.Num() > Limits.MaxRelationshipEdges)
    return false;
  TArray<FCrowdRelationshipEdge> Sorted(Relationships);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.From != B.From) return A.From < B.From;
    if (A.To != B.To) return A.To < B.To;
    return A.RelationshipBits < B.RelationshipBits;
  });
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    const FCrowdRelationshipEdge& Edge = Sorted[Index];
    if (!Edge.From.IsValid() || !Edge.To.IsValid()
      || Edge.From == Edge.To || Edge.RelationshipBits == 0
      || !CandidateLocations.Contains(Edge.From)
      || !CandidateLocations.Contains(Edge.To)
      || (Index > 0
        && Edge.From == Sorted[Index - 1].From
        && Edge.To == Sorted[Index - 1].To
        && Edge.RelationshipBits == Sorted[Index - 1].RelationshipBits))
      return false;
    OutRelationships.FindOrAdd(Edge.From).Add(
      {Edge.To, Edge.RelationshipBits});
    OutRelationships.FindOrAdd(Edge.To).Add(
      {Edge.From, Edge.RelationshipBits});
  }
  for (auto& Pair : OutRelationships)
  {
    Pair.Value.Sort([](const FRelationship& A, const FRelationship& B)
    {
      if (A.Other != B.Other) return A.Other < B.Other;
      return A.RelationshipBits < B.RelationshipBits;
    });
  }
  return true;
}

bool FCrowdSpatialGridRelevantSetProvider::RebuildIndex(
  const TConstArrayView<FCrowdAuthorityLocationRecord> Locations,
  const TConstArrayView<FCrowdRelationshipEdge> Relationships)
{
  if (!Limits.IsValid() || Locations.Num() > Limits.MaxIndexedEntities)
    return false;
  TMap<FCrowdStableEntityRef, FCrowdAuthorityLocationRecord>
    CandidateLocations;
  TMap<FIntVector, TArray<FCrowdStableEntityRef>> CandidateBuckets;
  for (const FCrowdAuthorityLocationRecord& Record : Locations)
  {
    FIntVector Cell;
    if (!Record.EntityRef.IsValid()
      || CandidateLocations.Contains(Record.EntityRef)
      || !TryCellForLocation(Record.Location, Cell))
      return false;
    CandidateLocations.Add(Record.EntityRef, Record);
    CandidateBuckets.FindOrAdd(Cell).Add(Record.EntityRef);
  }
  TMap<FCrowdStableEntityRef, TArray<FRelationship>>
    CandidateRelationships;
  if (!BuildRelationships(
      Relationships, CandidateLocations, CandidateRelationships))
    return false;
  for (auto& Pair : CandidateBuckets)
    Pair.Value.Sort();
  LocationsByRef = MoveTemp(CandidateLocations);
  Buckets = MoveTemp(CandidateBuckets);
  RelationshipsByRef = MoveTemp(CandidateRelationships);
  return true;
}

bool FCrowdSpatialGridRelevantSetProvider::ApplyIndexUpdate(
  const FCrowdRelevantSetIndexUpdate& Update)
{
  if (!Limits.IsValid())
    return false;
  TMap<FCrowdStableEntityRef, FCrowdAuthorityLocationRecord>
    CandidateLocations = LocationsByRef;
  TMap<FIntVector, TArray<FCrowdStableEntityRef>>
    CandidateBuckets = Buckets;

  TSet<FCrowdStableEntityRef> Removes;
  for (const FCrowdStableEntityRef& Ref : Update.Removes)
  {
    if (!Ref.IsValid() || Removes.Contains(Ref)
      || !CandidateLocations.Contains(Ref))
      return false;
    Removes.Add(Ref);
    FIntVector OldCell;
    check(TryCellForLocation(
      CandidateLocations.FindChecked(Ref).Location, OldCell));
    TArray<FCrowdStableEntityRef>& Bucket =
      CandidateBuckets.FindChecked(OldCell);
    Bucket.RemoveSingleSwap(Ref, EAllowShrinking::No);
    if (Bucket.IsEmpty())
      CandidateBuckets.Remove(OldCell);
    CandidateLocations.Remove(Ref);
  }

  TSet<FCrowdStableEntityRef> Upserts;
  for (const FCrowdAuthorityLocationRecord& Record : Update.Upserts)
  {
    FIntVector NewCell;
    if (!Record.EntityRef.IsValid()
      || Upserts.Contains(Record.EntityRef)
      || !TryCellForLocation(Record.Location, NewCell))
      return false;
    Upserts.Add(Record.EntityRef);
    if (const FCrowdAuthorityLocationRecord* Existing =
      CandidateLocations.Find(Record.EntityRef))
    {
      FIntVector OldCell;
      check(TryCellForLocation(Existing->Location, OldCell));
      if (OldCell != NewCell)
      {
        TArray<FCrowdStableEntityRef>& OldBucket =
          CandidateBuckets.FindChecked(OldCell);
        OldBucket.RemoveSingleSwap(
          Record.EntityRef, EAllowShrinking::No);
        if (OldBucket.IsEmpty())
          CandidateBuckets.Remove(OldCell);
      }
    }
    CandidateLocations.Add(Record.EntityRef, Record);
    TArray<FCrowdStableEntityRef>& NewBucket =
      CandidateBuckets.FindOrAdd(NewCell);
    NewBucket.AddUnique(Record.EntityRef);
  }
  if (CandidateLocations.Num() > Limits.MaxIndexedEntities)
    return false;

  TMap<FCrowdStableEntityRef, TArray<FRelationship>>
    CandidateRelationships;
  if (Update.bReplaceRelationships)
  {
    if (!BuildRelationships(
        Update.Relationships, CandidateLocations,
        CandidateRelationships))
      return false;
  }
  else
  {
    CandidateRelationships = RelationshipsByRef;
    for (const FCrowdStableEntityRef& Removed : Removes)
      CandidateRelationships.Remove(Removed);
    for (auto& Pair : CandidateRelationships)
    {
      Pair.Value.RemoveAll(
        [&Removes](const FRelationship& Relationship)
        {
          return Removes.Contains(Relationship.Other);
        });
    }
  }
  for (auto& Pair : CandidateBuckets)
    Pair.Value.Sort();
  LocationsByRef = MoveTemp(CandidateLocations);
  Buckets = MoveTemp(CandidateBuckets);
  RelationshipsByRef = MoveTemp(CandidateRelationships);
  return true;
}

bool FCrowdSpatialGridRelevantSetProvider::BuildRelevantSet(
  const FCrowdClientView& View,
  FCrowdRelevantSetResult& OutResult) const
{
  OutResult = {};
  if (!Limits.IsValid() || View.ClientKey == 0
    || !IsFinite(View.Location)
    || !FMath::IsFinite(View.RelevantRadiusCm)
    || View.RelevantRadiusCm < 0.0f)
    return false;

  const FVector Extent(View.RelevantRadiusCm);
  FIntVector MinCell;
  FIntVector MaxCell;
  if (!TryCellForLocation(View.Location - Extent, MinCell)
    || !TryCellForLocation(View.Location + Extent, MaxCell))
    return false;
  const int64 CountX = static_cast<int64>(MaxCell.X) - MinCell.X + 1;
  const int64 CountY = static_cast<int64>(MaxCell.Y) - MinCell.Y + 1;
  const int64 CountZ = static_cast<int64>(MaxCell.Z) - MinCell.Z + 1;
  if (CountX <= 0 || CountY <= 0 || CountZ <= 0
    || CountX > Limits.MaxQueryBuckets
    || CountY > Limits.MaxQueryBuckets
    || CountZ > Limits.MaxQueryBuckets
    || CountX * CountY > Limits.MaxQueryBuckets
    || CountX * CountY * CountZ > Limits.MaxQueryBuckets)
    return false;

  TSet<FCrowdStableEntityRef> Relevant;
  const double RadiusSquared = FMath::Square(
    static_cast<double>(View.RelevantRadiusCm));
  for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
    for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
      for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
      {
        const TArray<FCrowdStableEntityRef>* Bucket =
          Buckets.Find(FIntVector(X, Y, Z));
        if (!Bucket) continue;
        for (const FCrowdStableEntityRef& Ref : *Bucket)
        {
          const FCrowdAuthorityLocationRecord& Record =
            LocationsByRef.FindChecked(Ref);
          if (FVector::DistSquared(
              Record.Location, View.Location) <= RadiusSquared)
          {
            if (Relevant.Num() >= Limits.MaxRelevantEntities)
              return false;
            Relevant.Add(Ref);
          }
        }
      }

  TSet<FCrowdStableEntityRef> Frontier = Relevant;
  for (int32 Depth = 0;
    Depth < Limits.MaxClosureDepth && !Frontier.IsEmpty(); ++Depth)
  {
    TSet<FCrowdStableEntityRef> Next;
    for (const FCrowdStableEntityRef& Ref : Frontier)
    {
      const TArray<FRelationship>* Relationships =
        RelationshipsByRef.Find(Ref);
      if (!Relationships) continue;
      for (const FRelationship& Relationship : *Relationships)
      {
        if ((Relationship.RelationshipBits & View.RelationshipMask) != 0
          && !Relevant.Contains(Relationship.Other))
          Next.Add(Relationship.Other);
      }
    }
    for (const FCrowdStableEntityRef& Ref : Next)
    {
      if (Relevant.Num() >= Limits.MaxRelevantEntities)
        return false;
      Relevant.Add(Ref);
    }
    Frontier = MoveTemp(Next);
  }

  OutResult.EntityRefs = Relevant.Array();
  OutResult.EntityRefs.Sort();
  uint64 Hash = Fold(FnvOffset, 2);
  Hash = Fold(Hash, View.ClientKey);
  Hash = Fold(Hash, OutResult.EntityRefs.Num());
  for (const FCrowdStableEntityRef& Ref : OutResult.EntityRefs)
    Hash = FoldRef(Hash, Ref);
  OutResult.StableHash = Hash;
  OutResult.bValid = Hash != 0;
  return OutResult.bValid;
}
