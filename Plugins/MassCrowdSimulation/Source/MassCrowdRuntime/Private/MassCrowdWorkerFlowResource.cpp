#include "MassCrowdWorkerFlowResource.h"

namespace CrowdWorkerFlowResourcePrivate
{
  constexpr int32 MaxArrayItems = 2000000;

  template<typename T>
  void FlowAppendUnsigned(TArray<uint8>& Bytes, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }

  void FlowAppendSigned(TArray<uint8>& Bytes, const int32 Value)
  {
    FlowAppendUnsigned(Bytes, static_cast<uint32>(Value));
  }

  void FlowAppendDouble(TArray<uint8>& Bytes, const double Value)
  {
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    FlowAppendUnsigned(Bytes, Bits);
  }

  void FlowAppendVector(TArray<uint8>& Bytes, const FVector& Value)
  {
    FlowAppendDouble(Bytes, Value.X);
    FlowAppendDouble(Bytes, Value.Y);
    FlowAppendDouble(Bytes, Value.Z);
  }

  template<typename T>
  bool FlowReadUnsigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    T& OutValue)
  {
    static_assert(std::is_unsigned_v<T>);
    if (Offset < 0
      || Offset + static_cast<int32>(sizeof(T)) > Bytes.Num())
      return false;
    T Value = 0;
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Value |= static_cast<T>(Bytes[Offset + Byte])
        << (Byte * 8);
    Offset += sizeof(T);
    OutValue = Value;
    return true;
  }

  bool FlowReadSigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int32& OutValue)
  {
    uint32 Value = 0;
    if (!FlowReadUnsigned(Bytes, Offset, Value)) return false;
    OutValue = static_cast<int32>(Value);
    return true;
  }

  bool FlowReadDouble(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    double& OutValue)
  {
    uint64 Bits = 0;
    if (!FlowReadUnsigned(Bytes, Offset, Bits)) return false;
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }

  bool FlowReadVector(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FVector& OutValue)
  {
    return FlowReadDouble(Bytes, Offset, OutValue.X)
      && FlowReadDouble(Bytes, Offset, OutValue.Y)
      && FlowReadDouble(Bytes, Offset, OutValue.Z);
  }

  bool ReadCount(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int32& OutCount)
  {
    return FlowReadSigned(Bytes, Offset, OutCount)
      && OutCount >= 0 && OutCount <= MaxArrayItems;
  }

  void AppendIntArray(
    TArray<uint8>& Bytes,
    const TConstArrayView<int32> Values)
  {
    FlowAppendSigned(Bytes, Values.Num());
    for (const int32 Value : Values)
      FlowAppendSigned(Bytes, Value);
  }

  bool ReadIntArray(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    TArray<int32>& OutValues)
  {
    int32 Count = 0;
    if (!ReadCount(Bytes, Offset, Count)) return false;
    OutValues.SetNum(Count);
    for (int32& Value : OutValues)
      if (!FlowReadSigned(Bytes, Offset, Value))
        return false;
    return true;
  }

  bool IsCanonicalConfig(
    const FCrowdSharedFlowFieldConfig& Config)
  {
    int32 PreviousObstacleId = MIN_int32;
    for (const FCrowdSharedFlowObstacleSpec& Obstacle :
      Config.ObstacleSpecs)
    {
      if (Obstacle.ObstacleId <= PreviousObstacleId
        || Obstacle.Center.ContainsNaN()
        || Obstacle.Extent.ContainsNaN())
        return false;
      PreviousObstacleId = Obstacle.ObstacleId;
    }
    return true;
  }
}

using namespace CrowdWorkerFlowResourcePrivate;

bool FCrowdWorkerFlowFieldResource::IsValid() const
{
  const int64 CellCount =
    static_cast<int64>(Width) * Height;
  return Revision != 0
    && BuildHash != 0
    && Field.IsValid()
    && IsCanonicalConfig(Field.Config)
    && static_cast<uint64>(Field.Config.Revision) == Revision
    && Field.BuildHash == BuildHash
    && BoundsMin.Equals(Field.Config.BoundsMin, 0.0)
    && BoundsMax.Equals(Field.Config.BoundsMax, 0.0)
    && GoalLocation.Equals(Field.Config.GoalLocation, 0.0)
    && CellSizeCm == Field.Config.CellSizeCm
    && Width == Field.Width
    && Height == Field.Height
    && CellCount > 0 && CellCount <= MAX_int32
    && FlowDirections == Field.FlowDirection
    && Blocked == Field.Blocked
    && Unreachable == Field.Unreachable;
}

bool FCrowdWorkerFlowFieldResource::Sample(
  const FVector& Position,
  FVector& OutDirection,
  bool& bOutReachable) const
{
  OutDirection = FVector::ZeroVector;
  bOutReachable = false;
  if (!bStructurallyValidated || Position.ContainsNaN())
    return false;
  const FCrowdSharedFlowSample SampleResult =
    FCrowdSharedFlowFieldKernel::Sample(Field, Position);
  OutDirection = SampleResult.FlowDirection.GetSafeNormal();
  bOutReachable =
    SampleResult.Status == ECrowdFlowLocationStatus::Reachable
    && !SampleResult.bBlocked
    && !SampleResult.bUnreachable;
  return true;
}

bool FCrowdWorkerFlowFieldResourceCodec::Encode(
  const FCrowdSharedFlowField& Field,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Field.IsValid()
    || Field.Config.Revision <= 0
    || Field.BuildHash == 0
    || !IsCanonicalConfig(Field.Config))
    return false;
  const int32 CellCount = Field.Width * Field.Height;
  if (CellCount <= 0
    || Field.IntegrationCost.Num() != CellCount
    || Field.FlowDirection.Num() != CellCount
    || Field.NextCellIndex.Num() != CellCount
    || Field.Blocked.Num() != CellCount
    || Field.Unreachable.Num() != CellCount)
    return false;

  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  TArray<uint8>& Bytes = OutPayload.Bytes;
  FlowAppendUnsigned(
    Bytes, static_cast<uint64>(Field.Config.Revision));
  FlowAppendUnsigned(Bytes, Field.BuildHash);
  FlowAppendVector(Bytes, Field.Config.BoundsMin);
  FlowAppendVector(Bytes, Field.Config.BoundsMax);
  FlowAppendVector(Bytes, Field.Config.GoalLocation);
  FlowAppendDouble(Bytes, Field.Config.CellSizeCm);
  FlowAppendDouble(Bytes, Field.Config.AgentInflateCm);
  FlowAppendSigned(
    Bytes, Field.Config.ConnectivityContractVersion);
  FlowAppendSigned(Bytes, Field.Config.ObstacleSpecs.Num());
  for (const FCrowdSharedFlowObstacleSpec& Obstacle :
    Field.Config.ObstacleSpecs)
  {
    FlowAppendSigned(Bytes, Obstacle.ObstacleId);
    FlowAppendVector(Bytes, Obstacle.Center);
    FlowAppendVector(Bytes, Obstacle.Extent);
  }
  FlowAppendSigned(Bytes, Field.Width);
  FlowAppendSigned(Bytes, Field.Height);
  FlowAppendSigned(Bytes, CellCount);
  for (int32 Index = 0; Index < CellCount; ++Index)
  {
    FlowAppendSigned(Bytes, Field.IntegrationCost[Index]);
    FlowAppendVector(Bytes, Field.FlowDirection[Index]);
    FlowAppendSigned(Bytes, Field.NextCellIndex[Index]);
    Bytes.Add(Field.Blocked[Index] ? 1 : 0);
    Bytes.Add(Field.Unreachable[Index] ? 1 : 0);
  }

  FlowAppendSigned(Bytes, Field.NavigationSafeIntervals.Num());
  for (const FCrowdNavigationSafeInterval& Interval :
    Field.NavigationSafeIntervals)
  {
    Bytes.Add(static_cast<uint8>(Interval.Kind));
    FlowAppendSigned(Bytes, Interval.PrimaryCellKey);
    FlowAppendSigned(Bytes, Interval.SecondaryCellKey);
    FlowAppendSigned(Bytes, Interval.IntervalOrdinal);
    FlowAppendSigned(Bytes, Interval.QuantizedMinCm);
    FlowAppendSigned(Bytes, Interval.QuantizedMaxCm);
  }
  FlowAppendSigned(Bytes, Field.NavigationNodes.Num());
  for (const FCrowdNavigationNode& Node : Field.NavigationNodes)
  {
    FlowAppendUnsigned(Bytes, Node.StableNodeKey);
    Bytes.Add(static_cast<uint8>(Node.Kind));
    FlowAppendSigned(Bytes, Node.PrimaryCellKey);
    FlowAppendSigned(Bytes, Node.SecondaryCellKey);
    FlowAppendSigned(Bytes, Node.IntervalOrdinal);
    FlowAppendSigned(Bytes, Node.QuantizedLocationCm.X);
    FlowAppendSigned(Bytes, Node.QuantizedLocationCm.Y);
  }
  FlowAppendSigned(Bytes, Field.NavigationCellNodes.Num());
  for (const TArray<int32>& CellNodes :
    Field.NavigationCellNodes)
    AppendIntArray(Bytes, CellNodes);
  FlowAppendSigned(Bytes, Field.NavigationEdges.Num());
  for (const FCrowdNavigationEdge& Edge : Field.NavigationEdges)
  {
    FlowAppendUnsigned(Bytes, Edge.MinNodeKey);
    FlowAppendUnsigned(Bytes, Edge.MaxNodeKey);
    FlowAppendSigned(Bytes, Edge.QuantizedCost);
  }
  AppendIntArray(Bytes, Field.NavigationIntegrationCost);
  AppendIntArray(Bytes, Field.NavigationNextNodeIndex);
  AppendIntArray(Bytes, Field.GoalAttachmentNodeIndices);
  FlowAppendSigned(Bytes, Field.GoalCellIndex);
  FlowAppendSigned(Bytes, Field.BlockedCellCount);
  FlowAppendSigned(Bytes, Field.ValidDirectedEdgeCount);
  FlowAppendSigned(Bytes, Field.NavigationCenterAnchorCount);
  FlowAppendSigned(Bytes, Field.NavigationConnectionPointCount);
  FlowAppendSigned(Bytes, Field.NavigationSafeIntervalCount);
  FlowAppendSigned(Bytes, Field.NavigationInternalEdgeCount);
  FlowAppendSigned(Bytes, Field.CenterInvalidButConnectedCellCount);
  FlowAppendSigned(Bytes, Field.GoalAttachmentCount);
  FlowAppendUnsigned(Bytes, Field.TopologyHash);
  FlowAppendUnsigned(Bytes, Field.IntegrationHash);
  if (Bytes.Num() > MaxEncodedBytes)
  {
    OutPayload = {};
    return false;
  }
  OutPayload.RecalculateStableHash();
  return OutPayload.IsValid(MaxEncodedBytes);
}

bool FCrowdWorkerFlowFieldResourceCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerFlowFieldResource& OutResource)
{
  OutResource = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || !Payload.IsValid(MaxEncodedBytes))
    return false;
  int32 Offset = 0;
  double CellSize = 0.0;
  double AgentInflate = 0.0;
  int32 ObstacleCount = 0;
  int32 CellCount = 0;
  FCrowdSharedFlowField& Field = OutResource.Field;
  if (!FlowReadUnsigned(
      Payload.Bytes, Offset, OutResource.Revision)
    || !FlowReadUnsigned(
      Payload.Bytes, Offset, OutResource.BuildHash)
    || !FlowReadVector(
      Payload.Bytes, Offset, Field.Config.BoundsMin)
    || !FlowReadVector(
      Payload.Bytes, Offset, Field.Config.BoundsMax)
    || !FlowReadVector(
      Payload.Bytes, Offset, Field.Config.GoalLocation)
    || !FlowReadDouble(Payload.Bytes, Offset, CellSize)
    || !FlowReadDouble(Payload.Bytes, Offset, AgentInflate)
    || !FlowReadSigned(
      Payload.Bytes, Offset,
      Field.Config.ConnectivityContractVersion)
    || !ReadCount(Payload.Bytes, Offset, ObstacleCount))
    return false;
  Field.Config.Revision =
    static_cast<int32>(OutResource.Revision);
  Field.Config.CellSizeCm = static_cast<float>(CellSize);
  Field.Config.AgentInflateCm =
    static_cast<float>(AgentInflate);
  Field.Config.ObstacleSpecs.SetNum(ObstacleCount);
  for (FCrowdSharedFlowObstacleSpec& Obstacle :
    Field.Config.ObstacleSpecs)
  {
    if (!FlowReadSigned(
        Payload.Bytes, Offset, Obstacle.ObstacleId)
      || !FlowReadVector(
        Payload.Bytes, Offset, Obstacle.Center)
      || !FlowReadVector(
        Payload.Bytes, Offset, Obstacle.Extent))
      return false;
  }
  if (!FlowReadSigned(Payload.Bytes, Offset, Field.Width)
    || !FlowReadSigned(Payload.Bytes, Offset, Field.Height)
    || !ReadCount(Payload.Bytes, Offset, CellCount)
    || CellCount <= 0
    || static_cast<int64>(Field.Width) * Field.Height
      != CellCount)
    return false;
  Field.IntegrationCost.SetNum(CellCount);
  Field.FlowDirection.SetNum(CellCount);
  Field.NextCellIndex.SetNum(CellCount);
  Field.Blocked.Init(false, CellCount);
  Field.Unreachable.Init(false, CellCount);
  for (int32 Index = 0; Index < CellCount; ++Index)
  {
    if (!FlowReadSigned(
        Payload.Bytes, Offset, Field.IntegrationCost[Index])
      || !FlowReadVector(
        Payload.Bytes, Offset, Field.FlowDirection[Index])
      || !FlowReadSigned(
        Payload.Bytes, Offset, Field.NextCellIndex[Index])
      || Offset + 2 > Payload.Bytes.Num())
      return false;
    const uint8 bBlocked = Payload.Bytes[Offset++];
    const uint8 bUnreachable = Payload.Bytes[Offset++];
    if (bBlocked > 1 || bUnreachable > 1)
      return false;
    Field.Blocked[Index] = bBlocked != 0;
    Field.Unreachable[Index] = bUnreachable != 0;
  }

  int32 Count = 0;
  if (!ReadCount(Payload.Bytes, Offset, Count))
    return false;
  Field.NavigationSafeIntervals.SetNum(Count);
  for (FCrowdNavigationSafeInterval& Interval :
    Field.NavigationSafeIntervals)
  {
    if (Offset >= Payload.Bytes.Num())
      return false;
    const uint8 Kind = Payload.Bytes[Offset++];
    if (Kind > static_cast<uint8>(
        ECrowdNavigationNodeKind::HorizontalEdgeConnection)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Interval.PrimaryCellKey)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Interval.SecondaryCellKey)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Interval.IntervalOrdinal)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Interval.QuantizedMinCm)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Interval.QuantizedMaxCm))
      return false;
    Interval.Kind =
      static_cast<ECrowdNavigationNodeKind>(Kind);
  }
  if (!ReadCount(Payload.Bytes, Offset, Count))
    return false;
  Field.NavigationNodes.SetNum(Count);
  for (FCrowdNavigationNode& Node : Field.NavigationNodes)
  {
    if (!FlowReadUnsigned(
        Payload.Bytes, Offset, Node.StableNodeKey)
      || Offset >= Payload.Bytes.Num())
      return false;
    const uint8 Kind = Payload.Bytes[Offset++];
    if (Kind > static_cast<uint8>(
        ECrowdNavigationNodeKind::HorizontalEdgeConnection)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Node.PrimaryCellKey)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Node.SecondaryCellKey)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Node.IntervalOrdinal)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Node.QuantizedLocationCm.X)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Node.QuantizedLocationCm.Y))
      return false;
    Node.Kind = static_cast<ECrowdNavigationNodeKind>(Kind);
  }
  if (!ReadCount(Payload.Bytes, Offset, Count))
    return false;
  Field.NavigationCellNodes.SetNum(Count);
  for (TArray<int32>& CellNodes : Field.NavigationCellNodes)
    if (!ReadIntArray(Payload.Bytes, Offset, CellNodes))
      return false;
  if (!ReadCount(Payload.Bytes, Offset, Count))
    return false;
  Field.NavigationEdges.SetNum(Count);
  for (FCrowdNavigationEdge& Edge : Field.NavigationEdges)
  {
    if (!FlowReadUnsigned(
        Payload.Bytes, Offset, Edge.MinNodeKey)
      || !FlowReadUnsigned(
        Payload.Bytes, Offset, Edge.MaxNodeKey)
      || !FlowReadSigned(
        Payload.Bytes, Offset, Edge.QuantizedCost))
      return false;
  }
  if (!ReadIntArray(
      Payload.Bytes, Offset, Field.NavigationIntegrationCost)
    || !ReadIntArray(
      Payload.Bytes, Offset, Field.NavigationNextNodeIndex)
    || !ReadIntArray(
      Payload.Bytes, Offset, Field.GoalAttachmentNodeIndices)
    || !FlowReadSigned(Payload.Bytes, Offset, Field.GoalCellIndex)
    || !FlowReadSigned(Payload.Bytes, Offset, Field.BlockedCellCount)
    || !FlowReadSigned(
      Payload.Bytes, Offset, Field.ValidDirectedEdgeCount)
    || !FlowReadSigned(
      Payload.Bytes, Offset, Field.NavigationCenterAnchorCount)
    || !FlowReadSigned(
      Payload.Bytes, Offset,
      Field.NavigationConnectionPointCount)
    || !FlowReadSigned(
      Payload.Bytes, Offset, Field.NavigationSafeIntervalCount)
    || !FlowReadSigned(
      Payload.Bytes, Offset, Field.NavigationInternalEdgeCount)
    || !FlowReadSigned(
      Payload.Bytes, Offset,
      Field.CenterInvalidButConnectedCellCount)
    || !FlowReadSigned(
      Payload.Bytes, Offset, Field.GoalAttachmentCount)
    || !FlowReadUnsigned(Payload.Bytes, Offset, Field.TopologyHash)
    || !FlowReadUnsigned(
      Payload.Bytes, Offset, Field.IntegrationHash))
    return false;
  Field.BuildHash = OutResource.BuildHash;
  OutResource.BoundsMin = Field.Config.BoundsMin;
  OutResource.BoundsMax = Field.Config.BoundsMax;
  OutResource.GoalLocation = Field.Config.GoalLocation;
  OutResource.CellSizeCm = Field.Config.CellSizeCm;
  OutResource.Width = Field.Width;
  OutResource.Height = Field.Height;
  OutResource.FlowDirections = Field.FlowDirection;
  OutResource.Blocked = Field.Blocked;
  OutResource.Unreachable = Field.Unreachable;
  if (Offset != Payload.Bytes.Num() || !OutResource.IsValid())
    return false;
  OutResource.bStructurallyValidated = true;
  return true;
}

bool FCrowdWorkerFlowResourceCache::Resolve(
  const uint64 ResourceId,
  const uint64 Revision,
  const FCrowdWorkerPayload& Payload,
  const FCrowdWorkerFlowFieldResource*& OutResource)
{
  OutResource = nullptr;
  if (ResourceId == 0 || Revision == 0) return false;
  const FCrowdWorkerFlowResourceCacheKey Key{
    ResourceId, Revision};
  if (const FEntry* Existing = Entries.Find(Key))
  {
    if (Existing->PayloadStableHash != Payload.StableHash
      || !Existing->Resource)
      return false;
    OutResource = Existing->Resource.Get();
    return true;
  }

  ++DecodeCount;
  TSharedPtr<FCrowdWorkerFlowFieldResource> Decoded =
    MakeShared<FCrowdWorkerFlowFieldResource>();
  if (!FCrowdWorkerFlowFieldResourceCodec::Decode(
      Payload, *Decoded))
    return false;
  ++ValidationCount;
  if (Decoded->Revision != Revision) return false;

  FEntry Entry;
  Entry.PayloadStableHash = Payload.StableHash;
  Entry.Resource = MoveTemp(Decoded);
  FEntry& Stored = Entries.Add(Key, MoveTemp(Entry));
  OutResource = Stored.Resource.Get();
  return true;
}
