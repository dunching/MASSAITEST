#include "MassCrowdSpatialSafety.h"

namespace
{
  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }
}

FIntVector FCrowdSpatialSafetyIndex::CellFor(const FVector& Position) const
{
  return FIntVector(
    FMath::FloorToInt(Position.X / CellSizeCm),
    FMath::FloorToInt(Position.Y / CellSizeCm),
    FMath::FloorToInt(Position.Z / LayerToleranceCm));
}

bool FCrowdSpatialSafetyIndex::Build(
  const TConstArrayView<FCrowdSpatialSafetyAgent> Agents,
  const float InCellSizeCm,
  const float InLayerToleranceCm)
{
  AgentsByRef.Reset();
  RefsByCell.Reset();
  if (!FMath::IsFinite(InCellSizeCm) || InCellSizeCm <= 0.0f
    || !FMath::IsFinite(InLayerToleranceCm) || InLayerToleranceCm <= 0.0f)
  {
    return false;
  }
  CellSizeCm = InCellSizeCm;
  LayerToleranceCm = InLayerToleranceCm;
  TArray<FCrowdSpatialSafetyAgent> Sorted(Agents);
  Sorted.Sort([](const FCrowdSpatialSafetyAgent& A,
    const FCrowdSpatialSafetyAgent& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  for (const FCrowdSpatialSafetyAgent& Agent : Sorted)
  {
    if (!Agent.EntityRef.IsValid() || !IsFiniteVector(Agent.Position)
      || !FMath::IsFinite(Agent.RadiusCm) || Agent.RadiusCm < 0.0f
      || AgentsByRef.Contains(Agent.EntityRef))
    {
      AgentsByRef.Reset();
      RefsByCell.Reset();
      return false;
    }
    AgentsByRef.Add(Agent.EntityRef, Agent);
    RefsByCell.FindOrAdd(CellFor(Agent.Position)).Add(Agent.EntityRef);
  }
  return true;
}

bool FCrowdSpatialSafetyIndex::IsCandidateSafe(
  const FCrowdStableEntityRef& MovingRef,
  const FVector& Candidate,
  const float RadiusCm) const
{
  if (!MovingRef.IsValid() || !AgentsByRef.Contains(MovingRef)
    || !IsFiniteVector(Candidate) || !FMath::IsFinite(RadiusCm)
    || RadiusCm < 0.0f || CellSizeCm <= 0.0f || LayerToleranceCm <= 0.0f)
  {
    return false;
  }
  const FIntVector Center = CellFor(Candidate);
  for (int32 Z = Center.Z - 1; Z <= Center.Z + 1; ++Z)
  {
    for (int32 Y = Center.Y - 1; Y <= Center.Y + 1; ++Y)
    {
      for (int32 X = Center.X - 1; X <= Center.X + 1; ++X)
      {
        const TArray<FCrowdStableEntityRef>* Cell =
          RefsByCell.Find(FIntVector(X, Y, Z));
        if (!Cell) continue;
        for (const FCrowdStableEntityRef& OtherRef : *Cell)
        {
          if (OtherRef == MovingRef) continue;
          const FCrowdSpatialSafetyAgent& Other =
            AgentsByRef.FindChecked(OtherRef);
          if (FMath::Abs(Candidate.Z - Other.Position.Z)
              > LayerToleranceCm)
          {
            continue;
          }
          const float Required = RadiusCm + Other.RadiusCm;
          if (FVector::DistSquared(Candidate, Other.Position)
            < FMath::Square(Required))
          {
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool FCrowdSpatialSafetyIndex::Update(
  const FCrowdStableEntityRef& EntityRef,
  const FVector& NewPosition)
{
  FCrowdSpatialSafetyAgent* Agent = AgentsByRef.Find(EntityRef);
  if (!Agent || !IsFiniteVector(NewPosition)) return false;
  const FIntVector OldCell = CellFor(Agent->Position);
  const FIntVector NewCell = CellFor(NewPosition);
  if (OldCell != NewCell)
  {
    TArray<FCrowdStableEntityRef>* OldRefs = RefsByCell.Find(OldCell);
    if (!OldRefs || OldRefs->RemoveSingle(EntityRef) != 1) return false;
    if (OldRefs->IsEmpty()) RefsByCell.Remove(OldCell);
    RefsByCell.FindOrAdd(NewCell).Add(EntityRef);
  }
  Agent->Position = NewPosition;
  return true;
}
