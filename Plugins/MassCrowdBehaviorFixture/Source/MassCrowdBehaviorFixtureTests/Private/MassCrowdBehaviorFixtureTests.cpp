#include "MassCrowdBehaviorFixture.h"
#include "MassCrowdReplicationChannel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  class FLateProvider final : public ICrowdBehaviorSourceProvider
  {
  public:
    virtual FCrowdBehaviorProviderId GetProviderId() const override
    {
      return {0xF1000002u};
    }

    virtual bool Register(
      FCrowdBehaviorRegistryBuilder& Builder) const override
    {
      return false;
    }
  };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorFixturePublicApiTest,
  "MassCrowd.BehaviorSource.ThirdPartyPublicApiFixture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorFixturePublicApiTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("registry freezes with external provider"),
    Runtime.InitializeFromRegisteredProviders());
  TestTrue(TEXT("registry hash is available"),
    Runtime.GetRegistryHash() != 0);
  TestNotNull(TEXT("external spec is discoverable"),
    Runtime.GetEvaluators().FindSpec(
      CrowdBehaviorFixture::SourceTypeId));
  const FCrowdBehaviorSourceSpec* PredictableSpec =
    Runtime.GetEvaluators().FindSpec(
      CrowdBehaviorFixture::SourceTypeId);
  const FCrowdBehaviorSourceSpec* ResolvedOnlySpec =
    Runtime.GetEvaluators().FindSpec(
      CrowdBehaviorFixture::ResolvedOnlySourceTypeId);
  const FCrowdBehaviorSourceSpec* ServerOnlySpec =
    Runtime.GetEvaluators().FindSpec(
      CrowdBehaviorFixture::ServerOnlySourceTypeId);
  TestTrue(TEXT("external provider registers all replication policies"),
    PredictableSpec && ResolvedOnlySpec && ServerOnlySpec);
  if (!PredictableSpec || !ResolvedOnlySpec || !ServerOnlySpec)
    return false;
  TestEqual(TEXT("external predictable policy"),
    PredictableSpec->ReplicationPolicy,
    ECrowdBehaviorSourceReplicationPolicy::Predictable);
  TestEqual(TEXT("external resolved-only policy"),
    ResolvedOnlySpec->ReplicationPolicy,
    ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly);
  TestEqual(TEXT("external server-only policy"),
    ServerOnlySpec->ReplicationPolicy,
    ECrowdBehaviorSourceReplicationPolicy::ServerOnly);

  const FCrowdStableEntityRef EntityRef{77, 1, 1};
  FCrowdCapabilityBinding Binding;
  Binding.ProfileKey = CrowdBehaviorFixture::ProfileKey;
  TestTrue(TEXT("fixture profile registers entity"),
    Runtime.RegisterEntity(EntityRef, Binding));

  FCrowdBehaviorEntityEvaluationContext EvaluationContext;
  EvaluationContext.EntityRef = EntityRef;
  EvaluationContext.FixedStepIndex = 5;
  EvaluationContext.Position = FVector(10.0, 20.0, 30.0);
  EvaluationContext.Velocity = FVector(1.0, 2.0, 0.0);
  EvaluationContext.Facing = FVector::ForwardVector;
  FCrowdBehaviorContextRecord& ContextRecord =
    EvaluationContext.Records.AddDefaulted_GetRef();
  CrowdBehaviorFixture::FContext Extra;
  Extra.VelocityScaleQ15 = CrowdBehavior::FullQ15Weight / 2;
  TestTrue(TEXT("fixture context serializes"),
    ContextRecord.Set(
      CrowdBehaviorFixture::ContextTypeId, 1, Extra));
  EvaluationContext.RecalculateStableHash();
  TestTrue(TEXT("context accepted through public API"),
    Runtime.SetEvaluationContext(EvaluationContext));

  CrowdBehaviorFixture::FPayload Payload;
  Payload.DesiredVelocity = FVector(200.0, 0.0, 0.0);
  Payload.TargetRef = {77, 2, 1};
  Payload.CommitId = 9001;
  FCrowdBehaviorSourceCommand Start;
  Start.EffectiveFixedStep = 5;
  Start.Handle = {EntityRef, {42}, 1};
  Start.CommandSequence = 1;
  Start.Kind = ECrowdBehaviorSourceCommandKind::Start;
  Start.SourceTypeId = CrowdBehaviorFixture::SourceTypeId;
  TestTrue(TEXT("fixture payload serializes"),
    Start.Payload.Set(CrowdBehaviorFixture::PayloadSchemaId, Payload));
  TestTrue(TEXT("start queues"), Runtime.QueueCommand(Start));

  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("all-channel source prepares"),
    Runtime.PrepareBoundary(5, Prepared));
  TestEqual(TEXT("one entity prepares"), Prepared.Entities.Num(), 1);
  if (Prepared.Entities.Num() != 1)
    return false;
  const FCrowdBehaviorPreparedEntity& Entity = Prepared.Entities[0];
  TestTrue(TEXT("movement uses Q15 context scale"),
    Entity.ResolvedChannels.DesiredVelocity.Equals(
      FVector(99.997, 0.0, 0.0), 0.001));
  TestTrue(TEXT("facing resolves"),
    Entity.ResolvedChannels.DesiredFacing.Equals(
      FVector::ForwardVector));
  TestTrue(TEXT("interaction resolves"),
    Entity.ResolvedChannels.bHasInteraction);
  TestEqual(TEXT("business resolves"),
    Entity.ResolvedChannels.Business.Num(), 1);
  TestEqual(TEXT("presentation resolves"),
    Entity.ResolvedChannels.Presentation.Num(), 1);
  TestEqual(TEXT("persistent state is staged"),
    Entity.StagedSourceSet.Instances.Num(), 1);
  if (Entity.StagedSourceSet.Instances.Num() == 1)
  {
    CrowdBehaviorFixture::FState State;
    TestTrue(TEXT("persistent state decodes"),
      Entity.StagedSourceSet.Instances[0].State.Get(
        CrowdBehaviorFixture::StateSchemaId, State));
    TestEqual(TEXT("evaluator advances state"),
      State.EvaluationCount, 1u);
  }
  TestTrue(TEXT("prepared transaction commits"),
    Runtime.CommitPrepared(Prepared));

  EvaluationContext.FixedStepIndex = 6;
  EvaluationContext.RecalculateStableHash();
  TestTrue(TEXT("updated context accepted"),
    Runtime.SetEvaluationContext(EvaluationContext));
  FCrowdBehaviorSourceCommand Update = Start;
  Update.EffectiveFixedStep = 6;
  Update.CommandSequence = 2;
  Update.Kind = ECrowdBehaviorSourceCommandKind::Update;
  Payload.DesiredVelocity = FVector(0.0, 200.0, 0.0);
  TestTrue(TEXT("updated payload serializes"),
    Update.Payload.Set(CrowdBehaviorFixture::PayloadSchemaId, Payload));
  TestTrue(TEXT("update queues"), Runtime.QueueCommand(Update));
  FCrowdBehaviorPreparedBoundary Updated;
  TestTrue(TEXT("update prepares"),
    Runtime.PrepareBoundary(6, Updated));
  if (Updated.Entities.Num() == 1
    && Updated.Entities[0].StagedSourceSet.Instances.Num() == 1)
  {
    CrowdBehaviorFixture::FState State;
    TestTrue(TEXT("updated state decodes"),
      Updated.Entities[0].StagedSourceSet.Instances[0].State.Get(
        CrowdBehaviorFixture::StateSchemaId, State));
    TestEqual(TEXT("state persists across update"),
      State.EvaluationCount, 2u);
  }
  TestTrue(TEXT("updated transaction commits"),
    Runtime.CommitPrepared(Updated));

  const FCrowdBehaviorSourceSet* AuthoritativeSet =
    Runtime.FindSourceSet(EntityRef);
  TestNotNull(TEXT("authoritative fixture source set exists"),
    AuthoritativeSet);
  if (!AuthoritativeSet)
    return false;
  constexpr uint64 ContextSchemaHash = 0xF100000100000001ull;
  FCrowdBehaviorSourceSetReplicationRecord BaselineRecord;
  BaselineRecord.RegistryHash = Runtime.GetRegistryHash();
  BaselineRecord.ContextSchemaHash = ContextSchemaHash;
  BaselineRecord.SourceSet = *AuthoritativeSet;
  BaselineRecord.ResolvedBehaviorHash =
    Updated.ResolvedChannelHash;
  TArray<uint8> NetworkBytes;
  TestTrue(TEXT("third-party source set encodes through v3"),
    FCrowdReplicationCodec::EncodeBehaviorSourceSet(
      BaselineRecord, NetworkBytes));
  FCrowdBehaviorSourceSetReplicationRecord DecodedBaseline;
  TestTrue(TEXT("third-party source set decodes with registry baseline"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      NetworkBytes, Runtime.GetRegistryHash(),
      ContextSchemaHash, DecodedBaseline));
  FCrowdBehaviorSourceRuntime ClientRuntime;
  TestTrue(TEXT("client registry initializes"),
    ClientRuntime.InitializeFromRegisteredProviders());
  TestTrue(TEXT("late-join entity binding registers"),
    ClientRuntime.RegisterEntity(EntityRef, Binding));
  TestTrue(TEXT("late-join applies persistent fixture source"),
    ClientRuntime.ApplyReplicatedSourceSet(
      DecodedBaseline.SourceSet));
  const FCrowdBehaviorSourceSet* ClientSet =
    ClientRuntime.FindSourceSet(EntityRef);
  TestNotNull(TEXT("late-join persistent set is visible"), ClientSet);
  if (ClientSet)
    TestEqual(TEXT("late-join source hash is exact"),
      ClientSet->StableHash, AuthoritativeSet->StableHash);
  TestFalse(TEXT("registry mismatch triggers fixture resync"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      NetworkBytes, Runtime.GetRegistryHash() + 1,
      ContextSchemaHash, DecodedBaseline));

  EvaluationContext.FixedStepIndex = 7;
  EvaluationContext.RecalculateStableHash();
  TestTrue(TEXT("stop context accepted"),
    Runtime.SetEvaluationContext(EvaluationContext));
  FCrowdBehaviorSourceCommand Stop = Update;
  Stop.EffectiveFixedStep = 7;
  Stop.CommandSequence = 3;
  Stop.Kind = ECrowdBehaviorSourceCommandKind::Stop;
  TestTrue(TEXT("stop queues"), Runtime.QueueCommand(Stop));
  FCrowdBehaviorPreparedBoundary Stopped;
  TestTrue(TEXT("stop prepares"), Runtime.PrepareBoundary(7, Stopped));
  if (Stopped.Entities.Num() == 1)
    TestEqual(TEXT("stop removes only fixture instance"),
      Stopped.Entities[0].StagedSourceSet.Instances.Num(), 0);
  TestTrue(TEXT("stop transaction commits"),
    Runtime.CommitPrepared(Stopped));

  FCrowdBehaviorSourceRuntime SameRegistry;
  TestTrue(TEXT("second registry initializes"),
    SameRegistry.InitializeFromRegisteredProviders());
  TestEqual(TEXT("registry hash is stable across runtimes"),
    SameRegistry.GetRegistryHash(), Runtime.GetRegistryHash());

  TestFalse(TEXT("provider registry rejects late registration"),
    RegisterCrowdBehaviorSourceProvider(
      MakeShared<FLateProvider, ESPMode::ThreadSafe>()));
  return true;
}

#endif
