#include "MassCrowdWorkerNavigationResource.h"

namespace CrowdWorkerNavigationResourcePrivate
{
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
}

using namespace CrowdWorkerNavigationResourcePrivate;

bool FCrowdWorkerNavTopologyCodec::Encode(
  const FCrowdNavGraphResource& Resource,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Resource.IsReady()
    || Resource.TopologyRevision == 0)
    return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  TArray<uint8>& Bytes = OutPayload.Bytes;
  const FCrowdNavSurfaceGraph& Graph = *Resource.Graph;
  AppendUnsigned(Bytes, Resource.TopologyRevision);
  AppendUnsigned(Bytes, Resource.TopologyHash);
  AppendSigned(Bytes, Graph.RejectedPortalCount);
  AppendSigned(Bytes, Graph.RejectedMissingNeighborCount);
  AppendSigned(Bytes, Graph.RejectedNarrowPortalCount);
  AppendUnsigned(Bytes, Graph.MinRejectedPortalWidthCm);
  AppendUnsigned(Bytes, Graph.MaxRejectedPortalWidthCm);
  AppendSigned(Bytes, Graph.RejectedStepPortalCount);
  AppendSigned(Bytes, Graph.RejectedSlopePortalCount);
  AppendUnsigned(Bytes, static_cast<uint32>(Graph.Nodes.Num()));
  for (const FCrowdNavSurfaceNode& Node : Graph.Nodes)
  {
    AppendUnsigned(Bytes, Node.StableNodeId);
    AppendUnsigned(Bytes, Node.NavLayer);
    AppendVector(Bytes, Node.Center);
    AppendVector(Bytes, Node.SurfaceNormal);
    AppendUnsigned(
      Bytes, static_cast<uint32>(Node.Vertices.Num()));
    for (const FVector& Vertex : Node.Vertices)
      AppendVector(Bytes, Vertex);
    AppendUnsigned(
      Bytes, static_cast<uint32>(Node.Edges.Num()));
    for (const FCrowdNavSurfaceEdge& Edge : Node.Edges)
    {
      AppendUnsigned(Bytes, Edge.ToStableNodeId);
      AppendUnsigned(Bytes, Edge.WidthCm);
      AppendUnsigned(Bytes, Edge.SlopeMilliDegrees);
      AppendUnsigned(Bytes, Edge.CostQ);
    }
    if (Bytes.Num() > MaxEncodedBytes)
    {
      OutPayload = {};
      return false;
    }
  }
  OutPayload.RecalculateStableHash();
  return OutPayload.IsValid(MaxEncodedBytes);
}

bool FCrowdWorkerNavTopologyCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  uint32& OutTopologyRevision,
  FCrowdNavSurfaceGraph& OutGraph)
{
  OutTopologyRevision = 0;
  OutGraph.Reset();
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || !Payload.IsValid(MaxEncodedBytes))
    return false;
  int32 Offset = 0;
  uint32 NodeCount = 0;
  if (!ReadUnsigned(Payload.Bytes, Offset, OutTopologyRevision)
    || !ReadUnsigned(Payload.Bytes, Offset, OutGraph.TopologyHash)
    || !ReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedPortalCount)
    || !ReadSigned(
      Payload.Bytes, Offset,
      OutGraph.RejectedMissingNeighborCount)
    || !ReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedNarrowPortalCount)
    || !ReadUnsigned(
      Payload.Bytes, Offset,
      OutGraph.MinRejectedPortalWidthCm)
    || !ReadUnsigned(
      Payload.Bytes, Offset,
      OutGraph.MaxRejectedPortalWidthCm)
    || !ReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedStepPortalCount)
    || !ReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedSlopePortalCount)
    || !ReadUnsigned(Payload.Bytes, Offset, NodeCount)
    || OutTopologyRevision == 0 || NodeCount == 0
    || NodeCount > 65536)
    return false;
  OutGraph.Nodes.SetNum(NodeCount);
  for (FCrowdNavSurfaceNode& Node : OutGraph.Nodes)
  {
    uint32 VertexCount = 0;
    uint32 EdgeCount = 0;
    if (!ReadUnsigned(
        Payload.Bytes, Offset, Node.StableNodeId)
      || !ReadUnsigned(Payload.Bytes, Offset, Node.NavLayer)
      || !ReadVector(Payload.Bytes, Offset, Node.Center)
      || !ReadVector(
        Payload.Bytes, Offset, Node.SurfaceNormal)
      || !ReadUnsigned(Payload.Bytes, Offset, VertexCount)
      || Node.StableNodeId == 0 || VertexCount < 3
      || VertexCount > 64)
      return false;
    Node.Vertices.SetNum(VertexCount);
    for (FVector& Vertex : Node.Vertices)
    {
      if (!ReadVector(Payload.Bytes, Offset, Vertex))
        return false;
    }
    if (!ReadUnsigned(Payload.Bytes, Offset, EdgeCount)
      || EdgeCount > 256)
      return false;
    Node.Edges.SetNum(EdgeCount);
    for (FCrowdNavSurfaceEdge& Edge : Node.Edges)
    {
      if (!ReadUnsigned(
          Payload.Bytes, Offset, Edge.ToStableNodeId)
        || !ReadUnsigned(Payload.Bytes, Offset, Edge.WidthCm)
        || !ReadUnsigned(
          Payload.Bytes, Offset, Edge.SlopeMilliDegrees)
        || !ReadUnsigned(Payload.Bytes, Offset, Edge.CostQ))
        return false;
    }
  }
  return Offset == Payload.Bytes.Num()
    && OutGraph.IsValid();
}
