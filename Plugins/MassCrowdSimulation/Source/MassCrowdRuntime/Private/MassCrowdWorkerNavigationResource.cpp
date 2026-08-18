#include "MassCrowdWorkerNavigationResource.h"

namespace CrowdWorkerNavigationResourcePrivate
{
  template<typename T>
  void NavigationAppendUnsigned(TArray<uint8>& Bytes, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }

  void NavigationAppendSigned(TArray<uint8>& Bytes, const int32 Value)
  {
    NavigationAppendUnsigned(Bytes, static_cast<uint32>(Value));
  }

  void NavigationAppendDouble(TArray<uint8>& Bytes, const double Value)
  {
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    NavigationAppendUnsigned(Bytes, Bits);
  }

  void NavigationAppendVector(TArray<uint8>& Bytes, const FVector& Value)
  {
    NavigationAppendDouble(Bytes, Value.X);
    NavigationAppendDouble(Bytes, Value.Y);
    NavigationAppendDouble(Bytes, Value.Z);
  }

  template<typename T>
  bool NavigationReadUnsigned(
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

  bool NavigationReadSigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int32& OutValue)
  {
    uint32 Value = 0;
    if (!NavigationReadUnsigned(Bytes, Offset, Value)) return false;
    OutValue = static_cast<int32>(Value);
    return true;
  }

  bool NavigationReadDouble(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    double& OutValue)
  {
    uint64 Bits = 0;
    if (!NavigationReadUnsigned(Bytes, Offset, Bits)) return false;
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }

  bool NavigationReadVector(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FVector& OutValue)
  {
    return NavigationReadDouble(Bytes, Offset, OutValue.X)
      && NavigationReadDouble(Bytes, Offset, OutValue.Y)
      && NavigationReadDouble(Bytes, Offset, OutValue.Z);
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
  NavigationAppendUnsigned(Bytes, Resource.TopologyRevision);
  NavigationAppendUnsigned(Bytes, Resource.TopologyHash);
  NavigationAppendSigned(Bytes, Graph.RejectedPortalCount);
  NavigationAppendSigned(Bytes, Graph.RejectedMissingNeighborCount);
  NavigationAppendSigned(Bytes, Graph.RejectedNarrowPortalCount);
  NavigationAppendUnsigned(Bytes, Graph.MinRejectedPortalWidthCm);
  NavigationAppendUnsigned(Bytes, Graph.MaxRejectedPortalWidthCm);
  NavigationAppendSigned(Bytes, Graph.RejectedStepPortalCount);
  NavigationAppendSigned(Bytes, Graph.RejectedSlopePortalCount);
  NavigationAppendUnsigned(Bytes, static_cast<uint32>(Graph.Nodes.Num()));
  for (const FCrowdNavSurfaceNode& Node : Graph.Nodes)
  {
    NavigationAppendUnsigned(Bytes, Node.StableNodeId);
    NavigationAppendUnsigned(Bytes, Node.NavLayer);
    NavigationAppendVector(Bytes, Node.Center);
    NavigationAppendVector(Bytes, Node.SurfaceNormal);
    NavigationAppendUnsigned(
      Bytes, static_cast<uint32>(Node.Vertices.Num()));
    for (const FVector& Vertex : Node.Vertices)
      NavigationAppendVector(Bytes, Vertex);
    NavigationAppendUnsigned(
      Bytes, static_cast<uint32>(Node.Edges.Num()));
    for (const FCrowdNavSurfaceEdge& Edge : Node.Edges)
    {
      NavigationAppendUnsigned(Bytes, Edge.ToStableNodeId);
      NavigationAppendUnsigned(Bytes, Edge.WidthCm);
      NavigationAppendUnsigned(Bytes, Edge.SlopeMilliDegrees);
      NavigationAppendUnsigned(Bytes, Edge.CostQ);
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
  if (!NavigationReadUnsigned(Payload.Bytes, Offset, OutTopologyRevision)
    || !NavigationReadUnsigned(Payload.Bytes, Offset, OutGraph.TopologyHash)
    || !NavigationReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedPortalCount)
    || !NavigationReadSigned(
      Payload.Bytes, Offset,
      OutGraph.RejectedMissingNeighborCount)
    || !NavigationReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedNarrowPortalCount)
    || !NavigationReadUnsigned(
      Payload.Bytes, Offset,
      OutGraph.MinRejectedPortalWidthCm)
    || !NavigationReadUnsigned(
      Payload.Bytes, Offset,
      OutGraph.MaxRejectedPortalWidthCm)
    || !NavigationReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedStepPortalCount)
    || !NavigationReadSigned(
      Payload.Bytes, Offset, OutGraph.RejectedSlopePortalCount)
    || !NavigationReadUnsigned(Payload.Bytes, Offset, NodeCount)
    || OutTopologyRevision == 0 || NodeCount == 0
    || NodeCount > 65536)
    return false;
  OutGraph.Nodes.SetNum(NodeCount);
  for (FCrowdNavSurfaceNode& Node : OutGraph.Nodes)
  {
    uint32 VertexCount = 0;
    uint32 EdgeCount = 0;
    if (!NavigationReadUnsigned(
        Payload.Bytes, Offset, Node.StableNodeId)
      || !NavigationReadUnsigned(Payload.Bytes, Offset, Node.NavLayer)
      || !NavigationReadVector(Payload.Bytes, Offset, Node.Center)
      || !NavigationReadVector(
        Payload.Bytes, Offset, Node.SurfaceNormal)
      || !NavigationReadUnsigned(Payload.Bytes, Offset, VertexCount)
      || Node.StableNodeId == 0 || VertexCount < 3
      || VertexCount > 64)
      return false;
    Node.Vertices.SetNum(VertexCount);
    for (FVector& Vertex : Node.Vertices)
    {
      if (!NavigationReadVector(Payload.Bytes, Offset, Vertex))
        return false;
    }
    if (!NavigationReadUnsigned(Payload.Bytes, Offset, EdgeCount)
      || EdgeCount > 256)
      return false;
    Node.Edges.SetNum(EdgeCount);
    for (FCrowdNavSurfaceEdge& Edge : Node.Edges)
    {
      if (!NavigationReadUnsigned(
          Payload.Bytes, Offset, Edge.ToStableNodeId)
        || !NavigationReadUnsigned(Payload.Bytes, Offset, Edge.WidthCm)
        || !NavigationReadUnsigned(
          Payload.Bytes, Offset, Edge.SlopeMilliDegrees)
        || !NavigationReadUnsigned(Payload.Bytes, Offset, Edge.CostQ))
        return false;
    }
  }
  return Offset == Payload.Bytes.Num()
    && OutGraph.IsValid();
}
