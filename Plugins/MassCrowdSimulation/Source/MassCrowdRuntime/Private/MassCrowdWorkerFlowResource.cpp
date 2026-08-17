#include "MassCrowdWorkerFlowResource.h"

namespace CrowdWorkerFlowResourcePrivate
{
  constexpr int32 MaxArrayItems = 2000000;

  template<typename T>
  void AppendUnsigned(TArray<uint8>& Bytes, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }

  void AppendSigned(TArray<uint8>& Bytes, const int32 Value)
  {
    AppendUnsigned(Bytes, static_cast<uint32>(Value));
  }

  void AppendDouble(TArray<uint8>& Bytes, const double Value)
  {
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    AppendUnsigned(Bytes, Bits);
  }

  void AppendVector(TArray<uint8>& Bytes, const FVector& Value)
  {
    AppendDouble(Bytes, Value.X);
    AppendDouble(Bytes, Value.Y);
    AppendDouble(Bytes, Value.Z);
  }

  template<typename T>
  bool ReadUnsigned(
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

  bool ReadSigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int32& OutValue)
  {
    uint32 Value = 0;
    if (!ReadUnsigned(Bytes, Offset, Value)) return false;
    OutValue = static_cast<int32>(Value);
    return true;
  }

  bool ReadDouble(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    double& OutValue)
  {
    uint64 Bits = 0;
    if (!ReadUnsigned(Bytes, Offset, Bits)) return false;
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }

  bool ReadVector(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FVector& OutValue)
  {
    return ReadDouble(Bytes, Offset, OutValue.X)
      && ReadDouble(Bytes, Offset, OutValue.Y)
      && ReadDouble(Bytes, Offset, OutValue.Z);
  }

  bool ReadCount(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int32& OutCount)
  {
    return ReadSigned(Bytes, Offset, OutCount)
      && OutCount >= 0 && OutCount <= MaxArrayItems;
  }

  void AppendIntArray(
    TArray<uint8>& Bytes,
    const TConstArrayView<int32> Values)
  {
    AppendSigned(Bytes, Values.Num());
    for (const int32 Value : Values)
      AppendSigned(Bytes, Value);
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
      if (!ReadSigned(Bytes, Offset, Value))
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
  if (!IsValid() || Position.ContainsNaN())
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
  AppendUnsigned(
    Bytes, static_cast<uint64>(Field.Config.Revision));
  AppendUnsigned(Bytes, Field.BuildHash);
  AppendVector(Bytes, Field.Config.BoundsMin);
  AppendVector(Bytes, Field.Config.BoundsMax);
  AppendVector(Bytes, Field.Config.GoalLocation);
  AppendDouble(Bytes, Field.Config.CellSizeCm);
  AppendDouble(Bytes, Field.Config.AgentInflateCm);
  AppendSigned(
    Bytes, Field.Config.ConnectivityContractVersion);
  AppendSigned(Bytes, Field.Config.ObstacleSpecs.Num());
  for (const FCrowdSharedFlowObstacleSpec& Obstacle :
    Field.Config.ObstacleSpecs)
  {
    AppendSigned(Bytes, Obstacle.ObstacleId);
    AppendVector(Bytes, Obstacle.Center);
    AppendVector(Bytes, Obstacle.Extent);
  }
  AppendSigned(Bytes, Field.Width);
  AppendSigned(Bytes, Field.Height);
  AppendSigned(Bytes, CellCount);
  for (int32 Index = 0; Index < CellCount; ++Index)
  {
    AppendSigned(Bytes, Field.IntegrationCost[Index]);
    AppendVector(Bytes, Field.FlowDirection[Index]);
    AppendSigned(Bytes, Field.NextCellIndex[Index]);
    Bytes.Add(Field.Blocked[Index] ? 1 : 0);
    Bytes.Add(Field.Unreachable[Index] ? 1 : 0);
  }

  AppendSigned(Bytes, Field.NavigationSafeIntervals.Num());
  for (const FCrowdNavigationSafeInterval& Interval :
    Field.NavigationSafeIntervals)
  {
    Bytes.Add(static_cast<uint8>(Interval.Kind));
    AppendSigned(Bytes, Interval.PrimaryCellKey);
    AppendSigned(Bytes, Interval.SecondaryCellKey);
    AppendSigned(Bytes, Interval.IntervalOrdinal);
    AppendSigned(Bytes, Interval.QuantizedMinCm);
    AppendSigned(Bytes, Interval.QuantizedMaxCm);
  }
  AppendSigned(Bytes, Field.NavigationNodes.Num());
  for (const FCrowdNavigationNode& Node : Field.NavigationNodes)
  {
    AppendUnsigned(Bytes, Node.StableNodeKey);
    Bytes.Add(static_cast<uint8>(Node.Kind));
    AppendSigned(Bytes, Node.PrimaryCellKey);
    AppendSigned(Bytes, Node.SecondaryCellKey);
    AppendSigned(Bytes, Node.IntervalOrdinal);
    AppendSigned(Bytes, Node.QuantizedLocationCm.X);
    AppendSigned(Bytes, Node.QuantizedLocationCm.Y);
  }
  AppendSigned(Bytes, Field.NavigationCellNodes.Num());
  for (const TArray<int32>& CellNodes :
    Field.NavigationCellNodes)
    AppendIntArray(Bytes, CellNodes);
  AppendSigned(Bytes, Field.NavigationEdges.Num());
  for (const FCrowdNavigationEdge& Edge : Field.NavigationEdges)
  {
    AppendUnsigned(Bytes, Edge.MinNodeKey);
    AppendUnsigned(Bytes, Edge.MaxNodeKey);
    AppendSigned(Bytes, Edge.QuantizedCost);
  }
  AppendIntArray(Bytes, Field.NavigationIntegrationCost);
  AppendIntArray(Bytes, Field.NavigationNextNodeIndex);
  AppendIntArray(Bytes, Field.GoalAttachmentNodeIndices);
  AppendSigned(Bytes, Field.GoalCellIndex);
  AppendSigned(Bytes, Field.BlockedCellCount);
  AppendSigned(Bytes, Field.ValidDirectedEdgeCount);
  AppendSigned(Bytes, Field.NavigationCenterAnchorCount);
  AppendSigned(Bytes, Field.NavigationConnectionPointCount);
  AppendSigned(Bytes, Field.NavigationSafeIntervalCount);
  AppendSigned(Bytes, Field.NavigationInternalEdgeCount);
  AppendSigned(Bytes, Field.CenterInvalidButConnectedCellCount);
  AppendSigned(Bytes, Field.GoalAttachmentCount);
  AppendUnsigned(Bytes, Field.TopologyHash);
  AppendUnsigned(Bytes, Field.IntegrationHash);
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
  if (!ReadUnsigned(
      Payload.Bytes, Offset, OutResource.Revision)
    || !ReadUnsigned(
      Payload.Bytes, Offset, OutResource.BuildHash)
    || !ReadVector(
      Payload.Bytes, Offset, Field.Config.BoundsMin)
    || !ReadVector(
      Payload.Bytes, Offset, Field.Config.BoundsMax)
    || !ReadVector(
      Payload.Bytes, Offset, Field.Config.GoalLocation)
    || !ReadDouble(Payload.Bytes, Offset, CellSize)
    || !ReadDouble(Payload.Bytes, Offset, AgentInflate)
    || !ReadSigned(
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
    if (!ReadSigned(
        Payload.Bytes, Offset, Obstacle.ObstacleId)
      || !ReadVector(
        Payload.Bytes, Offset, Obstacle.Center)
      || !ReadVector(
        Payload.Bytes, Offset, Obstacle.Extent))
      return false;
  }
  if (!ReadSigned(Payload.Bytes, Offset, Field.Width)
    || !ReadSigned(Payload.Bytes, Offset, Field.Height)
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
    if (!ReadSigned(
        Payload.Bytes, Offset, Field.IntegrationCost[Index])
      || !ReadVector(
        Payload.Bytes, Offset, Field.FlowDirection[Index])
      || !ReadSigned(
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
      || !ReadSigned(
        Payload.Bytes, Offset, Interval.PrimaryCellKey)
      || !ReadSigned(
        Payload.Bytes, Offset, Interval.SecondaryCellKey)
      || !ReadSigned(
        Payload.Bytes, Offset, Interval.IntervalOrdinal)
      || !ReadSigned(
        Payload.Bytes, Offset, Interval.QuantizedMinCm)
      || !ReadSigned(
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
    if (!ReadUnsigned(
        Payload.Bytes, Offset, Node.StableNodeKey)
      || Offset >= Payload.Bytes.Num())
      return false;
    const uint8 Kind = Payload.Bytes[Offset++];
    if (Kind > static_cast<uint8>(
        ECrowdNavigationNodeKind::HorizontalEdgeConnection)
      || !ReadSigned(
        Payload.Bytes, Offset, Node.PrimaryCellKey)
      || !ReadSigned(
        Payload.Bytes, Offset, Node.SecondaryCellKey)
      || !ReadSigned(
        Payload.Bytes, Offset, Node.IntervalOrdinal)
      || !ReadSigned(
        Payload.Bytes, Offset, Node.QuantizedLocationCm.X)
      || !ReadSigned(
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
    if (!ReadUnsigned(
        Payload.Bytes, Offset, Edge.MinNodeKey)
      || !ReadUnsigned(
        Payload.Bytes, Offset, Edge.MaxNodeKey)
      || !ReadSigned(
        Payload.Bytes, Offset, Edge.QuantizedCost))
      return false;
  }
  if (!ReadIntArray(
      Payload.Bytes, Offset, Field.NavigationIntegrationCost)
    || !ReadIntArray(
      Payload.Bytes, Offset, Field.NavigationNextNodeIndex)
    || !ReadIntArray(
      Payload.Bytes, Offset, Field.GoalAttachmentNodeIndices)
    || !ReadSigned(Payload.Bytes, Offset, Field.GoalCellIndex)
    || !ReadSigned(Payload.Bytes, Offset, Field.BlockedCellCount)
    || !ReadSigned(
      Payload.Bytes, Offset, Field.ValidDirectedEdgeCount)
    || !ReadSigned(
      Payload.Bytes, Offset, Field.NavigationCenterAnchorCount)
    || !ReadSigned(
      Payload.Bytes, Offset,
      Field.NavigationConnectionPointCount)
    || !ReadSigned(
      Payload.Bytes, Offset, Field.NavigationSafeIntervalCount)
    || !ReadSigned(
      Payload.Bytes, Offset, Field.NavigationInternalEdgeCount)
    || !ReadSigned(
      Payload.Bytes, Offset,
      Field.CenterInvalidButConnectedCellCount)
    || !ReadSigned(
      Payload.Bytes, Offset, Field.GoalAttachmentCount)
    || !ReadUnsigned(Payload.Bytes, Offset, Field.TopologyHash)
    || !ReadUnsigned(
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
  return Offset == Payload.Bytes.Num()
    && OutResource.IsValid();
}
