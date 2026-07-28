#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Mass/CrowdDemoRelevantSnapshotAdapter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdDemoRoundAgentState MakeSnapshotAgent(const int32 AgentId)
  {
    FCrowdDemoRoundAgentState Agent;
    Agent.AgentId = AgentId;
    Agent.LifecycleSerial = 1000 + AgentId;
    Agent.Location = FVector_NetQuantize10(FVector(
      AgentId * 10.0 + 0.1,
      AgentId * -5.0 - 0.2,
      60.3));
    Agent.Velocity = FVector_NetQuantize10(FVector(100.4, -20.5, 0.6));
    Agent.YawDegrees = 17.25f + static_cast<float>(AgentId);
    Agent.RadiusCm = 42.5f;

    FCrowdDemoCombatNetState& Combat = Agent.Combat;
    Combat.Health = 75.25f;
    Combat.MaxHealth = 125.5f;
    Combat.LifecycleState = ECrowdDemoLifecycleState::Alive;
    Combat.bAlive = 1;
    Combat.BusinessState = ECrowdDemoBusinessState::Attacking;
    Combat.BusinessStateRevision = 11 + AgentId;
    Combat.BusinessStateEnterFixedStep = 21 + AgentId;
    Combat.TargetAgentId = AgentId + 1;
    Combat.TargetLifecycleSerial = 2000 + AgentId;
    Combat.AttackPhase = ECrowdDemoAttackPhase::Fire;
    Combat.AttackPhaseEnterFixedStep = 31 + AgentId;
    Combat.CooldownEndFixedStep = 41 + AgentId;
    Combat.LockedTargetAgentId = AgentId + 2;
    Combat.LockedTargetLifecycleSerial = 3000 + AgentId;
    Combat.LockedTargetLocation = FVector_NetQuantize10(FVector(10.1, 20.2, 30.3));
    Combat.FireSequence = 51 + AgentId;
    Combat.bFireRequestIssued = 1;
    Combat.ReactiveMode = ECrowdDemoReactiveMotionMode::KnockUp;
    Combat.HorizontalReactiveVelocity = FVector_NetQuantize10(FVector(40.4, 50.5, 0.0));
    Combat.VerticalReactiveVelocityCmps = 600.75f;
    Combat.ReactiveStartFixedStep = 61 + AgentId;
    Combat.ReactiveEndFixedStep = 71 + AgentId;
    Combat.ReactiveRevision = 81 + AgentId;
    Combat.RestoreBusinessState = ECrowdDemoBusinessState::Moving;
    Combat.ApexCount = 2;
    Combat.LandingCount = 3;
    Combat.HitFlashRevision = 91 + AgentId;
    Combat.HitFlashStartServerTimeSeconds = 12.25f;
    Combat.HitFlashDurationSeconds = 0.5f;
    Combat.HitFlashProfileKey = 0xA5A50000u + static_cast<uint32>(AgentId);
    Combat.HitFlashPeakIntensity = 2.75f;
    Combat.LastConsumedHitEventId = 0x1234567800000000ull + static_cast<uint64>(AgentId);
    Combat.VisualState = ECrowdDemoVisualState::HitReact;
    Combat.VisualRevision = 101 + AgentId;
    Combat.VisualStateStartServerTimeSeconds = 13.5f;
    Combat.VisualPhaseSeed = 0x5A5A0000u + static_cast<uint32>(AgentId);
    return Agent;
  }

  bool EncodeSnapshotAgents(
    const TConstArrayView<FCrowdDemoRoundAgentState> Agents,
    TArray<FCrowdRelevantSnapshotEntityPayload>& OutPayloads)
  {
    return FCrowdDemoRelevantSnapshotAdapter::EncodeAgents(Agents, OutPayloads);
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRelevantSnapshotAdapterRoundTripTest,
  "CrowdDemo.Networking.RelevantSnapshotAdapter.FieldRoundTrip",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRelevantSnapshotAdapterRoundTripTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoRoundAgentState> SourceAgents;
  SourceAgents.Add(MakeSnapshotAgent(1));
  SourceAgents.Add(MakeSnapshotAgent(2));

  TArray<FCrowdRelevantSnapshotEntityPayload> Encoded;
  TestTrue(TEXT("versioned adapter encodes all agent fields"),
    EncodeSnapshotAgents(SourceAgents, Encoded));
  TestEqual(TEXT("one framed entity payload per agent"), Encoded.Num(), SourceAgents.Num());

  TArray<FCrowdDemoRoundAgentState> Decoded;
  TestTrue(TEXT("versioned adapter decodes all agent fields"),
    FCrowdDemoRelevantSnapshotAdapter::DecodeAgents(Encoded, Decoded));
  TestEqual(TEXT("decoded agent count"), Decoded.Num(), SourceAgents.Num());

  TArray<FCrowdRelevantSnapshotEntityPayload> Reencoded;
  TestTrue(TEXT("decoded fields can be encoded again"),
    EncodeSnapshotAgents(Decoded, Reencoded));
  TestTrue(TEXT("field round trip is byte exact"), Encoded == Reencoded);

  TArray<FCrowdRelevantSnapshotEntityPayload> UnsupportedVersion = Encoded;
  UnsupportedVersion[0].Bytes[0] = 2;
  TestFalse(TEXT("unsupported agent payload version fails closed"),
    FCrowdDemoRelevantSnapshotAdapter::DecodeAgents(UnsupportedVersion, Decoded));

  TArray<FCrowdRelevantSnapshotEntityPayload> Reversed = Encoded;
  Swap(Reversed[0], Reversed[1]);
  TestFalse(TEXT("non-canonical AgentId order fails closed"),
    FCrowdDemoRelevantSnapshotAdapter::DecodeAgents(Reversed, Decoded));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRelevantSnapshotAdapterTransportTest,
  "CrowdDemo.Networking.RelevantSnapshotAdapter.Transport500",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRelevantSnapshotAdapterTransportTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoRoundAgentState> SourceAgents;
  SourceAgents.Reserve(500);
  for (int32 AgentId = 1; AgentId <= 500; ++AgentId)
  {
    SourceAgents.Add(MakeSnapshotAgent(AgentId));
  }

  TArray<FCrowdRelevantSnapshotEntityPayload> Encoded;
  TestTrue(TEXT("500 agents encode"), EncodeSnapshotAgents(SourceAgents, Encoded));

  const FCrowdRelevantSnapshotLimits Limits = FCrowdDemoRelevantSnapshotAdapter::MakeLimits();
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  TestTrue(TEXT("500 agents build bounded transport"),
    FCrowdRelevantSnapshotTransport::Build(
      7, 300, 9, Encoded, Limits, Header, Chunks));
  TestEqual(TEXT("500 entity header count"), Header.EntityCount, 500);
  TestTrue(TEXT("500 entities require multiple chunks"), Chunks.Num() > 1);

  FCrowdRelevantSnapshotAssembly Assembly;
  TestTrue(TEXT("assembly begins"), Assembly.Begin(7, Limits));
  TestEqual(TEXT("last chunk accepted before header"),
    Assembly.AcceptChunk(Chunks.Last(), 1.0),
    ECrowdRelevantSnapshotAcceptResult::Accepted);
  TestEqual(TEXT("same duplicate chunk is idempotent"),
    Assembly.AcceptChunk(Chunks.Last(), 1.1),
    ECrowdRelevantSnapshotAcceptResult::Duplicate);
  TestEqual(TEXT("header accepted after a chunk"),
    Assembly.AcceptHeader(Header, 1.2),
    ECrowdRelevantSnapshotAcceptResult::Accepted);
  for (int32 ChunkIndex = Chunks.Num() - 2; ChunkIndex >= 0; --ChunkIndex)
  {
    const ECrowdRelevantSnapshotAcceptResult Result =
      Assembly.AcceptChunk(Chunks[ChunkIndex], 1.3);
    TestTrue(TEXT("remaining reversed chunks accepted"),
      Result == ECrowdRelevantSnapshotAcceptResult::Accepted
        || Result == ECrowdRelevantSnapshotAcceptResult::Complete);
  }

  TArray<FCrowdRelevantSnapshotEntityPayload> Assembled;
  TestTrue(TEXT("reversed 500 entity assembly finalizes"),
    Assembly.TryFinalize(1.4, Assembled));
  TestTrue(TEXT("assembled entity bytes are unchanged"), Assembled == Encoded);

  TArray<FCrowdDemoRoundAgentState> Decoded;
  TestTrue(TEXT("assembled 500 entities decode"),
    FCrowdDemoRelevantSnapshotAdapter::DecodeAgents(Assembled, Decoded));
  TestEqual(TEXT("decoded 500 entity count"), Decoded.Num(), 500);

  FCrowdRelevantSnapshotAssembly MissingChunkAssembly;
  TestTrue(TEXT("missing-chunk assembly begins"),
    MissingChunkAssembly.Begin(7, Limits));
  MissingChunkAssembly.AcceptHeader(Header, 10.0);
  for (int32 ChunkIndex = 0; ChunkIndex + 1 < Chunks.Num(); ++ChunkIndex)
  {
    MissingChunkAssembly.AcceptChunk(Chunks[ChunkIndex], 10.1);
  }
  TestFalse(TEXT("missing chunk cannot finalize"),
    MissingChunkAssembly.TryFinalize(10.2, Assembled));
  TestTrue(TEXT("missing chunk deterministically times out"),
    MissingChunkAssembly.IsTimedOut(10.0 + Limits.AssemblyTimeoutSeconds + 0.01));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRelevantSnapshotArchitectureTest,
  "CrowdDemo.Networking.RelevantSnapshotAdapter.Architecture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRelevantSnapshotArchitectureTest::RunTest(const FString& Parameters)
{
  FString HeaderSource;
  FString CoordinatorSource;
  const FString HeaderPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/CrowdDemoRoundSimCoordinator.h"));
  const FString CoordinatorPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/CrowdDemoRoundSimCoordinator.cpp"));
  TestTrue(TEXT("coordinator header is readable"),
    FFileHelper::LoadFileToString(HeaderSource, *HeaderPath));
  TestTrue(TEXT("coordinator source is readable"),
    FFileHelper::LoadFileToString(CoordinatorSource, *CoordinatorPath));

  TestFalse(TEXT("full bootstrap packet is not a replicated property"),
    HeaderSource.Contains(TEXT("ReplicatedUsing = OnRep_RoundBootstrapPacket")));
  TestFalse(TEXT("full bootstrap packet has no DOREPLIFETIME path"),
    CoordinatorSource.Contains(TEXT("DOREPLIFETIME(ACrowdDemoRoundSimCoordinator, RoundBootstrapPacket)")));
  TestFalse(TEXT("bootstrap metadata has no legacy replicated property"),
    CoordinatorSource.Contains(
      TEXT("DOREPLIFETIME(ACrowdDemoRoundSimCoordinator, BootstrapSnapshotMetadata)")));
  TestFalse(TEXT("bootstrap has no legacy multicast"),
    HeaderSource.Contains(TEXT("MulticastBootstrapSnapshotChunk")));
  TestTrue(TEXT("bootstrap uses owner-only product baseline"),
    CoordinatorSource.Contains(TEXT("PublishProductBaseline"))
      && CoordinatorSource.Contains(
        TEXT("Channel.PublishBaseline")));
  TestFalse(TEXT("round result header is not a replicated property"),
    HeaderSource.Contains(
      TEXT("ReplicatedUsing = OnRep_RoundResultHeader")));
  TestFalse(TEXT("round result header has no DOREPLIFETIME path"),
    CoordinatorSource.Contains(
      TEXT("DOREPLIFETIME(ACrowdDemoRoundSimCoordinator, RoundResultHeader)")));
  TestFalse(TEXT("correction frame has no legacy multicast"),
    HeaderSource.Contains(TEXT("MulticastCorrectionFrameChunk")));
  TestFalse(TEXT("projectile events have no legacy multicast"),
    HeaderSource.Contains(TEXT("MulticastProjectileVisualEvents")));
  TestTrue(TEXT("round result uses public reliable host event"),
    CoordinatorSource.Contains(TEXT("PublishProductRoundResultHeader"))
      && CoordinatorSource.Contains(
        TEXT("ECrowdReliableStateKind::HostEvent")));
  TestTrue(TEXT("movement corrections use one bounded public batch"),
    CoordinatorSource.Contains(
      TEXT("Channel->PublishMovementCorrections(Corrections)")));
  return true;
}

#endif
