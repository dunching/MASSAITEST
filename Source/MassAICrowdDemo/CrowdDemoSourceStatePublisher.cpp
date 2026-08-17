#include "CrowdDemoSourceStatePublisher.h"

bool FCrowdDemoSourceStatePublisher::AppendChanged(
  const FCrowdBehaviorSourceRuntime& Runtime,
  const TConstArrayView<FCrowdDemoSourceStateFact> EntityFacts,
  const int32 MaximumRecordCount,
  TMap<FCrowdStableEntityRef, uint32>&
    InOutLastPublishedRevisions,
  uint64& InOutNextSequence,
  TArray<FCrowdReliableStateRecord>& InOutRecords)
{
  if (MaximumRecordCount < 0 || InOutNextSequence == 0)
    return false;
  TArray<FCrowdDemoSourceStateFact> SortedFacts(EntityFacts);
  SortedFacts.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  int32 AddedCount = 0;
  for (int32 Index = 0; Index < SortedFacts.Num(); ++Index)
  {
    const FCrowdDemoSourceStateFact& Fact = SortedFacts[Index];
    if (!Fact.EntityRef.IsValid()
      || (Index > 0
        && !(SortedFacts[Index - 1].EntityRef
          < Fact.EntityRef)))
      return false;
    if (AddedCount >= MaximumRecordCount)
      break;
    const FCrowdBehaviorSourceSet* SourceSet =
      Runtime.FindSourceSet(Fact.EntityRef);
    const FCrowdResolvedBehaviorChannels* Resolved =
      Runtime.FindResolvedChannels(Fact.EntityRef);
    if (!SourceSet || !Resolved || !Resolved->bValid)
      continue;
    const uint32* LastRevision =
      InOutLastPublishedRevisions.Find(Fact.EntityRef);
    if (LastRevision && *LastRevision == SourceSet->Revision)
      continue;

    FCrowdBehaviorSourceSetReplicationRecord SourceRecord;
    SourceRecord.RegistryHash = Runtime.GetRegistryHash();
    SourceRecord.ContextSchemaHash =
      Runtime.GetContextSchemaHash();
    SourceRecord.SourceSet = *SourceSet;
    SourceRecord.SourceSet.Instances.RemoveAll(
      [](const FCrowdBehaviorSourceInstance& Instance)
      {
        return Instance.ReplicationPolicy
          != ECrowdBehaviorSourceReplicationPolicy::Predictable;
      });
    SourceRecord.SourceSet.RecalculateStableHash();
    SourceRecord.ResolvedBehaviorHash = Resolved->StableHash;
    SourceRecord.DerivedDiagnosticLabel =
      Fact.DerivedDiagnosticLabel;
    TArray<uint8> Payload;
    if (!FCrowdReplicationCodec::EncodeBehaviorSourceSet(
        SourceRecord, Payload))
      return false;

    FCrowdReliableStateRecord& Record =
      InOutRecords.AddDefaulted_GetRef();
    Record.Sequence = InOutNextSequence++;
    Record.Kind = ECrowdReliableStateKind::BehaviorSourceSet;
    Record.EntityRef = Fact.EntityRef;
    Record.Revision = SourceSet->Revision;
    Record.Payload = MoveTemp(Payload);
    Record.StableHash =
      FCrowdReplicationTransport::CalculateReliableRecordHash(
        Record);
    InOutLastPublishedRevisions.Add(
      Fact.EntityRef, SourceSet->Revision);
    ++AddedCount;
  }
  return true;
}
