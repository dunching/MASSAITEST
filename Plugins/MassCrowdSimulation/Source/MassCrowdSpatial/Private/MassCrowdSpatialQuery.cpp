#include "MassCrowdSpatialQuery.h"

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  void Fold(uint64& Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= FnvPrime;
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    Fold(Hash, Ref.ProviderId);
    Fold(Hash, Ref.StableEntityId);
    Fold(Hash, Ref.LifecycleSerial);
  }

  void FoldVector(uint64& Hash, const FVector& Value)
  {
    Fold(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.X * 100.0)));
    Fold(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Y * 100.0)));
    Fold(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Z * 100.0)));
  }

  bool SegmentSphereHitTime(
    const FVector& Start,
    const FVector& End,
    const float RadiusCm,
    double& OutTime)
  {
    const FVector Segment = End - Start;
    const double RadiusSquared =
      static_cast<double>(RadiusCm) * RadiusCm;
    const double C =
      FVector::DotProduct(Start, Start) - RadiusSquared;
    if (C <= 0.0)
    {
      OutTime = 0.0;
      return true;
    }
    const double A = FVector::DotProduct(Segment, Segment);
    if (A <= UE_DOUBLE_SMALL_NUMBER)
      return false;
    const double B = 2.0 * FVector::DotProduct(Start, Segment);
    const double Discriminant = B * B - 4.0 * A * C;
    if (Discriminant < 0.0)
      return false;
    const double Root =
      (-B - FMath::Sqrt(Discriminant)) / (2.0 * A);
    if (Root < 0.0 || Root > 1.0)
      return false;
    OutTime = Root;
    return true;
  }

  bool SegmentExpandedBoxHitTime(
    const FVector& Start,
    const FVector& End,
    const FVector& BoundsMin,
    const FVector& BoundsMax,
    const float RadiusCm,
    double& OutTime,
    FVector& OutNormal)
  {
    const FVector Minimum = BoundsMin - FVector(RadiusCm);
    const FVector Maximum = BoundsMax + FVector(RadiusCm);
    const FVector Delta = End - Start;
    double EnterTime = 0.0;
    double ExitTime = 1.0;
    FVector EnterNormal = FVector::ZeroVector;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
      const double Origin = Start[Axis];
      const double Direction = Delta[Axis];
      if (FMath::Abs(Direction) <= UE_DOUBLE_SMALL_NUMBER)
      {
        if (Origin < Minimum[Axis] || Origin > Maximum[Axis])
          return false;
        continue;
      }
      double NearTime = (Minimum[Axis] - Origin) / Direction;
      double FarTime = (Maximum[Axis] - Origin) / Direction;
      FVector NearNormal = FVector::ZeroVector;
      NearNormal[Axis] = Direction > 0.0 ? -1.0 : 1.0;
      if (NearTime > FarTime)
        Swap(NearTime, FarTime);
      if (NearTime > EnterTime)
      {
        EnterTime = NearTime;
        EnterNormal = NearNormal;
      }
      ExitTime = FMath::Min(ExitTime, FarTime);
      if (EnterTime > ExitTime)
        return false;
    }
    if (ExitTime < 0.0 || EnterTime > 1.0)
      return false;
    OutTime = FMath::Clamp(EnterTime, 0.0, 1.0);
    OutNormal = EnterNormal.IsNearlyZero()
      ? -Delta.GetSafeNormal() : EnterNormal;
    return !OutNormal.IsNearlyZero();
  }
}

bool FCrowdSpatialBodySnapshot::IsValid() const
{
  if (!EntityRef.IsValid()
    || !IsFiniteVector(StartPosition)
    || !IsFiniteVector(EndPosition)
    || !FMath::IsFinite(RadiusCm) || RadiusCm < 0.0f
    || CollisionMask == 0 || QueryMask == 0 || StableHash == 0)
    return false;
  FCrowdSpatialBodySnapshot Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdSpatialBodySnapshot::RecalculateStableHash()
{
  uint64 Hash = FnvOffset;
  FoldRef(Hash, EntityRef);
  FoldVector(Hash, StartPosition);
  FoldVector(Hash, EndPosition);
  Fold(Hash, static_cast<uint64>(
    FMath::RoundToInt64(RadiusCm * 100.0)));
  Fold(Hash, NavLayer);
  Fold(Hash, CollisionMask);
  Fold(Hash, QueryMask);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdSpatialEnvironmentBody::IsValid() const
{
  if (StableSurfaceId == 0 || !IsFiniteVector(BoundsMin)
    || !IsFiniteVector(BoundsMax)
    || BoundsMin.X > BoundsMax.X
    || BoundsMin.Y > BoundsMax.Y
    || BoundsMin.Z > BoundsMax.Z
    || CollisionMask == 0 || QueryMask == 0
    || CollisionProfileId == 0 || EffectProfileId == 0
    || StableHash == 0)
    return false;
  FCrowdSpatialEnvironmentBody Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdSpatialEnvironmentBody::RecalculateStableHash()
{
  uint64 Hash = FnvOffset;
  Fold(Hash, StableSurfaceId);
  Fold(Hash, NavLayer);
  FoldVector(Hash, BoundsMin);
  FoldVector(Hash, BoundsMax);
  Fold(Hash, CollisionMask);
  Fold(Hash, QueryMask);
  Fold(Hash, CollisionProfileId);
  Fold(Hash, EffectProfileId);
  StableHash = Hash == 0 ? 1 : Hash;
}

int32 FCrowdSpatialQueryIndex::GridCoordinate(
  const double Value) const
{
  return FMath::FloorToInt(Value / CellSizeCm);
}

void FCrowdSpatialQueryIndex::AddSweptBounds(
  const int32 BodyIndex)
{
  const FCrowdSpatialBodySnapshot& Body = Bodies[BodyIndex];
  const FVector Minimum =
    Body.StartPosition.ComponentMin(Body.EndPosition)
    - FVector(Body.RadiusCm);
  const FVector Maximum =
    Body.StartPosition.ComponentMax(Body.EndPosition)
    + FVector(Body.RadiusCm);
  for (int32 Z = GridCoordinate(Minimum.Z);
    Z <= GridCoordinate(Maximum.Z); ++Z)
  {
    for (int32 Y = GridCoordinate(Minimum.Y);
      Y <= GridCoordinate(Maximum.Y); ++Y)
    {
      for (int32 X = GridCoordinate(Minimum.X);
        X <= GridCoordinate(Maximum.X); ++X)
      {
        BodyIndicesByCell.FindOrAdd({
          X, Y, Z, Body.NavLayer}).Add(BodyIndex);
      }
    }
  }
}

bool FCrowdSpatialQueryIndex::Build(
  const TConstArrayView<FCrowdSpatialBodySnapshot> InBodies,
  const float InCellSizeCm)
{
  Bodies.Reset();
  BodyIndicesByCell.Reset();
  if (!FMath::IsFinite(InCellSizeCm) || InCellSizeCm <= 0.0f)
    return false;
  CellSizeCm = InCellSizeCm;
  Bodies.Append(InBodies);
  Bodies.Sort([](
    const FCrowdSpatialBodySnapshot& A,
    const FCrowdSpatialBodySnapshot& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  FCrowdStableEntityRef Previous;
  for (int32 Index = 0; Index < Bodies.Num(); ++Index)
  {
    if (!Bodies[Index].IsValid()
      || (Index > 0 && Bodies[Index].EntityRef == Previous))
    {
      Bodies.Reset();
      BodyIndicesByCell.Reset();
      return false;
    }
    Previous = Bodies[Index].EntityRef;
    AddSweptBounds(Index);
  }
  return true;
}

bool FCrowdSpatialQueryIndex::GatherCandidates(
  const FVector& Start,
  const FVector& End,
  const float RadiusCm,
  const uint32 NavLayer,
  const uint32 QueryMask,
  TArray<int32>& OutBodyIndices) const
{
  OutBodyIndices.Reset();
  if (CellSizeCm <= 0.0f || !IsFiniteVector(Start)
    || !IsFiniteVector(End) || !FMath::IsFinite(RadiusCm)
    || RadiusCm < 0.0f || QueryMask == 0)
    return false;
  const FVector Minimum =
    Start.ComponentMin(End) - FVector(RadiusCm);
  const FVector Maximum =
    Start.ComponentMax(End) + FVector(RadiusCm);
  TBitArray<> Seen(false, Bodies.Num());
  for (int32 Z = GridCoordinate(Minimum.Z);
    Z <= GridCoordinate(Maximum.Z); ++Z)
  {
    for (int32 Y = GridCoordinate(Minimum.Y);
      Y <= GridCoordinate(Maximum.Y); ++Y)
    {
      for (int32 X = GridCoordinate(Minimum.X);
        X <= GridCoordinate(Maximum.X); ++X)
      {
        const TArray<int32>* Indices =
          BodyIndicesByCell.Find({X, Y, Z, NavLayer});
        if (!Indices)
          continue;
        for (const int32 Index : *Indices)
        {
          if (!Seen[Index]
            && (Bodies[Index].CollisionMask & QueryMask) != 0)
          {
            Seen[Index] = true;
            OutBodyIndices.Add(Index);
          }
        }
      }
    }
  }
  OutBodyIndices.Sort([this](const int32 A, const int32 B)
  {
    return Bodies[A].EntityRef < Bodies[B].EntityRef;
  });
  return true;
}

const FCrowdSpatialBodySnapshot&
FCrowdSpatialQueryIndex::GetBodyChecked(
  const int32 BodyIndex) const
{
  return Bodies[BodyIndex];
}

bool FCrowdSpatialSweep::MovingSphere(
  const FVector& QueryStart,
  const FVector& QueryEnd,
  const float QueryRadiusCm,
  const FCrowdSpatialBodySnapshot& Target,
  FCrowdSpatialSweepHit& OutHit)
{
  if (!Target.IsValid() || !IsFiniteVector(QueryStart)
    || !IsFiniteVector(QueryEnd)
    || !FMath::IsFinite(QueryRadiusCm) || QueryRadiusCm < 0.0f)
    return false;
  const FVector RelativeStart = QueryStart - Target.StartPosition;
  const FVector RelativeEnd = QueryEnd - Target.EndPosition;
  double Time = 0.0;
  if (!SegmentSphereHitTime(
    RelativeStart, RelativeEnd,
    QueryRadiusCm + Target.RadiusCm, Time))
    return false;
  OutHit = {};
  OutHit.Target = Target.EntityRef;
  OutHit.TimeOfImpactQ = static_cast<uint32>(
    FMath::Clamp<int64>(
      FMath::RoundToInt64(Time * 1000000.0), 0, 1000000));
  OutHit.Position = FMath::Lerp(QueryStart, QueryEnd, Time);
  const FVector TargetAtImpact = FMath::Lerp(
    Target.StartPosition, Target.EndPosition, Time);
  OutHit.Normal = (OutHit.Position - TargetAtImpact).GetSafeNormal();
  if (OutHit.Normal.IsNearlyZero())
    OutHit.Normal = -(QueryEnd - QueryStart).GetSafeNormal();
  return !OutHit.Normal.IsNearlyZero();
}

bool FCrowdSpatialSweep::EnvironmentAabb(
  const FVector& QueryStart,
  const FVector& QueryEnd,
  const float QueryRadiusCm,
  const FCrowdSpatialEnvironmentBody& Environment,
  FCrowdSpatialSweepHit& OutHit)
{
  if (!Environment.IsValid() || !IsFiniteVector(QueryStart)
    || !IsFiniteVector(QueryEnd)
    || !FMath::IsFinite(QueryRadiusCm) || QueryRadiusCm < 0.0f)
    return false;
  double Time = 0.0;
  FVector Normal = FVector::ZeroVector;
  if (!SegmentExpandedBoxHitTime(
    QueryStart, QueryEnd,
    Environment.BoundsMin, Environment.BoundsMax,
    QueryRadiusCm, Time, Normal))
    return false;
  OutHit = {};
  OutHit.StableSurfaceId = Environment.StableSurfaceId;
  OutHit.Position = FMath::Lerp(QueryStart, QueryEnd, Time);
  OutHit.Normal = Normal;
  OutHit.CollisionProfileId = Environment.CollisionProfileId;
  OutHit.EffectProfileId = Environment.EffectProfileId;
  OutHit.TimeOfImpactQ = static_cast<uint32>(
    FMath::Clamp<int64>(
      FMath::RoundToInt64(Time * 1000000.0), 0, 1000000));
  return true;
}

bool FCrowdSpatialSweep::IsEarlierStableHit(
  const FCrowdSpatialSweepHit& Candidate,
  const FCrowdSpatialSweepHit& Current)
{
  if (Candidate.TimeOfImpactQ != Current.TimeOfImpactQ)
    return Candidate.TimeOfImpactQ < Current.TimeOfImpactQ;
  if (Candidate.IsEnvironment() != Current.IsEnvironment())
    return Candidate.IsEnvironment();
  if (Candidate.IsEnvironment())
    return Candidate.StableSurfaceId < Current.StableSurfaceId;
  return Candidate.Target < Current.Target;
}
