#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdReplicationChannel.h"

struct FCrowdDemoSourceStateFact
{
  FCrowdStableEntityRef EntityRef;
  uint32 DerivedDiagnosticLabel = 0;
};

class FCrowdDemoSourceStatePublisher
{
public:
  static bool AppendChanged(
    const FCrowdBehaviorSourceRuntime& Runtime,
    TConstArrayView<FCrowdDemoSourceStateFact> EntityFacts,
    int32 MaximumRecordCount,
    TMap<FCrowdStableEntityRef, uint32>&
      InOutLastPublishedRevisions,
    uint64& InOutNextSequence,
    TArray<FCrowdReliableStateRecord>& InOutRecords);
};
