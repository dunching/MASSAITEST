#include "CrowdDemoTypes.h"

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRoundResultHeaderNetSerializeTest,
  "CrowdDemo.SF.RoundResultHeader.NetSerialize",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRoundResultHeaderNetSerializeTest::RunTest(const FString& Parameters)
{
  FCrowdDemoRoundResultHeader Source;
  Source.PayloadKind = 1;
  Source.bValid = 1;
  Source.RoundId = 17;
  Source.Revision = 19;
  Source.CheckpointRevision = 23;
  Source.StateFrameRevision = 29;
  Source.EndServerTimeSeconds = 31.25f;
  Source.AgentCount = 20;
  Source.SharedFlowMetrics.AgentStateHash = 0x9e3779b9u;
  Source.SharedFlowMetrics.SharedFlowFieldBuildHash = 267519150u;
  Source.ParticleMetrics.ParticleCandidateHash = 0x85ebca6bu;
  Source.ParticleMetrics.ParticleAppliedStateHash = 0xc2b2ae35u;
  for (int32 Index = 0; Index < 7; ++Index)
  {
    FCrowdDemoCapabilityProfileMetrics& Profile =
      Source.ParticleMetrics.CapabilityProfiles.AddDefaulted_GetRef();
    Profile.CapabilityProfileKey = 0x811c9dc5u + static_cast<uint32>(Index * 7919);
    Profile.AgentCount = Index < 6 ? 3 : 2;
    Profile.FeasibleRegionCount = 6 + Index;
    Profile.FeasibleRegionCoverageCount = Profile.AgentCount;
    Profile.InsideBandCount = Profile.AgentCount;
    Profile.TopologyHash = 0x01000193u * static_cast<uint32>(Index + 1);
    Profile.DemandHash = Profile.TopologyHash ^ 0x9e3779b9u;
    Profile.TransportHash = Profile.TopologyHash ^ 0x85ebca6bu;
    Profile.GuidanceHash = Profile.TopologyHash ^ 0xc2b2ae35u;
    Profile.ValidationHash = Profile.TopologyHash ^ 0x27d4eb2fu;
  }
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  bool bSaveSuccess = false;
  Source.NetSerialize(Writer, nullptr, bSaveSuccess);
  Writer.Close();
  AddInfo(FString::Printf(TEXT("representative T6 header bytes=%d"), Bytes.Num()));
  TestTrue(TEXT("maximum representative result header serializes"), bSaveSuccess);
  TestTrue(TEXT("serialized header stays within 2048-byte contract"),
    Bytes.Num() <= FCrowdDemoRoundResultHeader::MaximumSerializedBytes);
  TestEqual(TEXT("reported serialized bytes match archive"),
    Source.SerializedByteCount, Bytes.Num());

  FCrowdDemoRoundResultHeader Restored;
  FMemoryReader Reader(Bytes, true);
  bool bLoadSuccess = false;
  Restored.NetSerialize(Reader, nullptr, bLoadSuccess);
  Reader.Close();
  TestTrue(TEXT("serialized result header round-trips"), bLoadSuccess);
  TestEqual(TEXT("round id round-trips"), Restored.RoundId, Source.RoundId);
  TestEqual(TEXT("particle candidate hash round-trips"),
    Restored.ParticleMetrics.ParticleCandidateHash,
    Source.ParticleMetrics.ParticleCandidateHash);
  TestEqual(TEXT("capability profile count round-trips"),
    Restored.ParticleMetrics.CapabilityProfiles.Num(), 7);
  TestEqual(TEXT("soft-pressure payload does not carry projectile metrics"),
    Restored.ProjectileMetrics.EventHash, 2166136261u);

  FCrowdDemoRoundResultHeader Combat = Source;
  Combat.PayloadKind = 2;
  Combat.ParticleMetrics.CapabilityProfiles.Reset();
  Combat.ProjectileMetrics.bValid = 1;
  Combat.ProjectileMetrics.ProjectileSpawnedCount = 50;
  Combat.ProjectileMetrics.ProjectileImpactedCount = 50;
  Combat.ProjectileMetrics.EventHash = 0x165667b1u;
  TArray<uint8> CombatBytes;
  FMemoryWriter CombatWriter(CombatBytes, true);
  bool bCombatSaveSuccess = false;
  Combat.NetSerialize(CombatWriter, nullptr, bCombatSaveSuccess);
  CombatWriter.Close();
  AddInfo(FString::Printf(TEXT("representative combat header bytes=%d"), CombatBytes.Num()));
  TestTrue(TEXT("representative combat result header serializes"), bCombatSaveSuccess);
  TestTrue(TEXT("combat header stays within 2048-byte contract"),
    CombatBytes.Num() <= FCrowdDemoRoundResultHeader::MaximumSerializedBytes);
  FCrowdDemoRoundResultHeader RestoredCombat;
  FMemoryReader CombatReader(CombatBytes, true);
  bool bCombatLoadSuccess = false;
  RestoredCombat.NetSerialize(CombatReader, nullptr, bCombatLoadSuccess);
  CombatReader.Close();
  TestTrue(TEXT("combat result header round-trips"), bCombatLoadSuccess);
  TestEqual(TEXT("projectile event hash round-trips"),
    RestoredCombat.ProjectileMetrics.EventHash, Combat.ProjectileMetrics.EventHash);
  return true;
}

#endif
