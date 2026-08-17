#include "MassCrowdBehaviorSourceRuntime.h"

#define FnvOffset64 BehaviorSourceRuntime_FnvOffset64
#define FnvPrime64 BehaviorSourceRuntime_FnvPrime64
#define FoldUnsigned BehaviorSourceRuntime_FoldUnsigned
#define FoldRef BehaviorSourceRuntime_FoldRef

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;
  constexpr int32 MaxPendingBehaviorCommands = 4096;
  constexpr int32 MaxBehaviorBindingUpdates = 16384;
  FCriticalSection ProviderRegistryMutex;
  TMap<
    FCrowdBehaviorProviderId,
    TSharedPtr<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>>
      RegisteredProviders;
  bool bProviderRegistrationFrozen = false;

  template <typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
    {
      Hash ^= static_cast<uint8>(Value >> (ByteIndex * 8));
      Hash *= FnvPrime64;
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldUnsigned(Hash, Ref.ProviderId);
    FoldUnsigned(Hash, Ref.StableEntityId);
    FoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  uint64 CalculateBindingUpdateHash(
    const int64 FixedStep,
    const FCrowdStableEntityRef& EntityRef,
    const FCrowdCapabilityBinding& Binding)
  {
    uint64 Hash = FnvOffset64;
    FoldUnsigned(Hash, static_cast<uint64>(FixedStep));
    FoldRef(Hash, EntityRef);
    FoldUnsigned(Hash, Binding.ProfileKey.Value);
    FoldUnsigned(Hash, Binding.ModifierRevision);
    FoldUnsigned(Hash, Binding.ModifierCount);
    for (uint8 Index = 0; Index < Binding.ModifierCount; ++Index)
    {
      FoldUnsigned(Hash, Binding.Modifiers[Index].CapabilityId.Value);
      FoldUnsigned(Hash, static_cast<uint8>(
        Binding.Modifiers[Index].Operation));
    }
    return Hash;
  }

  bool AreBindingsEqual(
    const FCrowdCapabilityBinding& A,
    const FCrowdCapabilityBinding& B)
  {
    if (A.ProfileKey != B.ProfileKey
      || A.ModifierRevision != B.ModifierRevision
      || A.ModifierCount != B.ModifierCount)
      return false;
    for (uint8 Index = 0; Index < A.ModifierCount; ++Index)
      if (!(A.Modifiers[Index] == B.Modifiers[Index]))
        return false;
    return true;
  }

  uint64 CalculatePreparedEntityHash(
    const FCrowdBehaviorPreparedEntity& Entity)
  {
    uint64 Hash = FnvOffset64;
    FoldRef(Hash, Entity.EntityRef);
    FoldUnsigned(Hash, Entity.BaseSourceSetHash);
    FoldUnsigned(Hash, Entity.EvaluationContextHash);
    FoldUnsigned(Hash, Entity.StagedSourceSet.StableHash);
    FoldUnsigned(Hash, Entity.ResolvedChannels.StableHash);
    FoldUnsigned(Hash, Entity.CommandBatchHash);
    FoldUnsigned(Hash, static_cast<uint32>(Entity.Events.Num()));
    for (const FCrowdBehaviorSourceEvent& Event : Entity.Events)
    {
      FoldUnsigned(Hash, static_cast<uint8>(Event.Kind));
      FoldUnsigned(Hash, static_cast<uint64>(Event.FixedStepIndex));
      FoldUnsigned(Hash, Event.Handle.ControllerId.Value);
      FoldUnsigned(Hash, Event.Handle.SourceSequence);
      FoldUnsigned(Hash, Event.SourceTypeId.Value);
    }
    return Hash;
  }

  uint64 CalculatePreparedBoundaryHash(
    const FCrowdBehaviorPreparedBoundary& Prepared)
  {
    uint64 Hash = FnvOffset64;
    FoldUnsigned(Hash, static_cast<uint64>(Prepared.FixedStepIndex));
    FoldUnsigned(Hash, Prepared.RegistryHash);
    FoldUnsigned(Hash, static_cast<uint32>(Prepared.Entities.Num()));
    FoldUnsigned(Hash, Prepared.SourceSetHash);
    FoldUnsigned(Hash, Prepared.CommandBatchHash);
    FoldUnsigned(Hash, Prepared.ResolvedChannelHash);
    for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
      FoldUnsigned(Hash, Entity.StableHash);
    return Hash;
  }

  uint64 CalculateSourceSetContentHash(
    const FCrowdBehaviorSourceSet& SourceSet)
  {
    FCrowdBehaviorSourceSet Canonical = SourceSet;
    Canonical.Revision = 1;
    Canonical.RecalculateStableHash();
    return Canonical.StableHash;
  }
}

const FCrowdBehaviorContextRecord*
FCrowdBehaviorSourceEvaluationContext::FindContext(
  const FCrowdBehaviorContextTypeId TypeId) const
{
  for (const FCrowdBehaviorContextRecord& Record : ContextRecords)
    if (Record.TypeId == TypeId) return &Record;
  return nullptr;
}

bool FCrowdBehaviorEntityEvaluationContext::IsValid() const
{
  if (!EntityRef.IsValid() || FixedStepIndex < 0
    || Position.ContainsNaN() || Velocity.ContainsNaN()
    || Facing.ContainsNaN() || Facing.IsNearlyZero()
    || Records.Num() > CrowdBehavior::MaxContextRecordsPerEntity
    || StableHash == 0)
    return false;
  FCrowdBehaviorContextTypeId Previous;
  for (const FCrowdBehaviorContextRecord& Record : Records)
  {
    if (!Record.IsValid()
      || (Previous.IsValid() && !(Previous < Record.TypeId)))
      return false;
    Previous = Record.TypeId;
  }
  FCrowdBehaviorEntityEvaluationContext Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdBehaviorEntityEvaluationContext::RecalculateStableHash()
{
  Records.Sort([](const auto& A, const auto& B)
  {
    return A.TypeId < B.TypeId;
  });
  uint64 Hash = FnvOffset64;
  FoldRef(Hash, EntityRef);
  FoldUnsigned(Hash, static_cast<uint64>(FixedStepIndex));
  const FVector Values[] = {Position, Velocity, Facing};
  for (const FVector& Value : Values)
  {
    FoldUnsigned(Hash, static_cast<uint32>(
      FMath::RoundToInt(Value.X * 1000.0)));
    FoldUnsigned(Hash, static_cast<uint32>(
      FMath::RoundToInt(Value.Y * 1000.0)));
    FoldUnsigned(Hash, static_cast<uint32>(
      FMath::RoundToInt(Value.Z * 1000.0)));
  }
  FoldUnsigned(Hash, static_cast<uint32>(Records.Num()));
  for (const FCrowdBehaviorContextRecord& Record : Records)
    FoldUnsigned(Hash, Record.CalculateStableHash());
  StableHash = Hash;
}

FCrowdBehaviorContributionWriter::FCrowdBehaviorContributionWriter(
  const FCrowdBehaviorSourceSpec& InSpec,
  const FCrowdBehaviorSourceInstance& Instance,
  FCrowdBehaviorContributions& OutContributions)
  : Spec(InSpec)
  , Key{
      Instance.Priority,
      Instance.SourceTypeId,
      Instance.Handle.ControllerId,
      Instance.Handle.SourceSequence}
  , Out(OutContributions)
{
  bSucceeded = Spec.IsValid()
    && Instance.IsValid()
    && Spec.TypeId == Instance.SourceTypeId
    && Spec.Version == Instance.SourceVersion;
}

bool FCrowdBehaviorContributionWriter::CanWrite(
  const ECrowdBehaviorChannel Channel,
  const int32 CurrentCount)
{
  bSucceeded = bSucceeded
    && (Spec.ChannelMask & CrowdBehaviorChannelBit(Channel)) != 0
    && CurrentCount < CrowdBehavior::MaxContributionsPerChannel;
  return bSucceeded;
}

bool FCrowdBehaviorContributionWriter::AddMovement(
  FCrowdMovementContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Movement, Out.Movement.Num()))
    return false;
  Contribution.Key = Key;
  Out.Movement.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddFacing(
  FCrowdFacingContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Facing, Out.Facing.Num()))
    return false;
  Contribution.Key = Key;
  Out.Facing.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddConstraint(
  FCrowdConstraintContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Constraint,
      Out.Constraints.Num()))
    return false;
  Contribution.Key = Key;
  Out.Constraints.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddInteraction(
  FCrowdInteractionContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Interaction,
      Out.Interactions.Num()))
    return false;
  Contribution.Key = Key;
  Out.Interactions.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddBusiness(
  FCrowdBusinessContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Business, Out.Business.Num()))
    return false;
  Contribution.Key = Key;
  Out.Business.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddPresentation(
  FCrowdPresentationContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Presentation,
      Out.Presentation.Num()))
    return false;
  Contribution.Key = Key;
  Out.Presentation.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::SetNextState(
  const FCrowdBehaviorSourceState& State)
{
  bSucceeded = bSucceeded && !bHasNextState && State.IsValid()
    && ((Spec.StateSchemaId == 0 && State.SchemaId == 0)
      || Spec.StateSchemaId == State.SchemaId);
  if (!bSucceeded) return false;
  NextState = State;
  bHasNextState = true;
  return true;
}

bool FCrowdBehaviorSourceEvaluatorRegistry::Register(
  const FCrowdBehaviorSourceSpec& Spec,
  TSharedRef<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
    Evaluator)
{
  if (bFrozen || Evaluators.Contains(Spec.TypeId)
    || !Specs.Register(Spec))
    return false;
  Evaluators.Add(Spec.TypeId, Evaluator);
  return true;
}

bool FCrowdBehaviorSourceEvaluatorRegistry::Freeze()
{
  if (bFrozen || Evaluators.IsEmpty() || !Specs.Freeze())
    return false;
  bFrozen = true;
  return true;
}

const FCrowdBehaviorSourceSpec*
FCrowdBehaviorSourceEvaluatorRegistry::FindSpec(
  const FCrowdBehaviorSourceTypeId TypeId) const
{
  return bFrozen ? Specs.Find(TypeId) : nullptr;
}

TSharedPtr<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
FCrowdBehaviorSourceEvaluatorRegistry::FindEvaluator(
  const FCrowdBehaviorSourceTypeId TypeId) const
{
  if (!bFrozen) return nullptr;
  const auto* Found = Evaluators.Find(TypeId);
  return Found ? *Found : nullptr;
}

uint64 FCrowdBehaviorSourceEvaluatorRegistry::CalculateStableHash() const
{
  return bFrozen ? Specs.CalculateStableHash() : 0;
}

bool FCrowdBehaviorRegistryBuilder::RegisterProfile(
  FCrowdCapabilityProfile Profile)
{
  return Profiles.Register(MoveTemp(Profile));
}

bool FCrowdBehaviorRegistryBuilder::RegisterSource(
  const FCrowdBehaviorSourceSpec& Spec,
  TSharedRef<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
    Evaluator)
{
  return Evaluators.Register(Spec, Evaluator);
}

bool FCrowdBehaviorRegistryBuilder::RegisterContextSchema(
  const FCrowdBehaviorContextSchema& Schema)
{
  if (!Schema.IsValid() || ContextSchemas.Contains(Schema.TypeId))
    return false;
  ContextSchemas.Add(Schema.TypeId, Schema);
  return true;
}

bool RegisterCrowdBehaviorSourceProvider(
  TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
    Provider)
{
  const FCrowdBehaviorProviderId Id = Provider->GetProviderId();
  if (!Id.IsValid()) return false;
  FScopeLock Lock(&ProviderRegistryMutex);
  if (bProviderRegistrationFrozen || RegisteredProviders.Contains(Id))
    return false;
  RegisteredProviders.Add(Id, Provider);
  return true;
}

bool UnregisterCrowdBehaviorSourceProvider(
  const FCrowdBehaviorProviderId ProviderId)
{
  FScopeLock Lock(&ProviderRegistryMutex);
  return !bProviderRegistrationFrozen
    && ProviderId.IsValid()
    && RegisteredProviders.Remove(ProviderId) == 1;
}

bool FCrowdBehaviorCapabilityBindingUpdate::IsValid() const
{
  return EffectiveFixedStep >= 0
    && EntityRef.IsValid()
    && Binding.IsValid()
    && StableHash == CalculateBindingUpdateHash(
      EffectiveFixedStep, EntityRef, Binding);
}

void FCrowdBehaviorCapabilityBindingUpdate::RecalculateStableHash()
{
  StableHash = CalculateBindingUpdateHash(
    EffectiveFixedStep, EntityRef, Binding);
}

bool FCrowdBehaviorSourceRuntime::InitializeFromRegisteredProviders()
{
  Reset();
  TArray<
    TPair<
      FCrowdBehaviorProviderId,
      TSharedPtr<const ICrowdBehaviorSourceProvider,
        ESPMode::ThreadSafe>>> Providers;
  {
    FScopeLock Lock(&ProviderRegistryMutex);
    bProviderRegistrationFrozen = true;
    for (const auto& Pair : RegisteredProviders)
      Providers.Emplace(Pair.Key, Pair.Value);
  }
  Providers.Sort([](const auto& A, const auto& B)
  {
    return A.Key < B.Key;
  });
  FCrowdBehaviorRegistryBuilder Builder(
    CapabilityProfiles, Evaluators, ContextSchemas);
  for (const auto& Pair : Providers)
    if (!Pair.Value.IsValid() || !Pair.Value->Register(Builder))
    {
      UE_LOG(LogTemp, Error,
        TEXT("Crowd behavior provider registration failed provider_id=%u"),
        Pair.Key.Value);
      return false;
    }

  if (!CapabilityProfiles.Freeze() || !Evaluators.Freeze())
  {
    UE_LOG(LogTemp, Error,
      TEXT("Crowd behavior registry freeze failed providers=%d"),
      Providers.Num());
    return false;
  }
  RegistryHash = FnvOffset64;
  FoldUnsigned(RegistryHash, static_cast<uint32>(Providers.Num()));
  for (const auto& Pair : Providers)
    FoldUnsigned(RegistryHash, Pair.Key.Value);
  FoldUnsigned(RegistryHash, CapabilityProfiles.CalculateStableHash());
  FoldUnsigned(RegistryHash, Evaluators.CalculateStableHash());
  TArray<FCrowdBehaviorContextTypeId> ContextKeys;
  ContextSchemas.GetKeys(ContextKeys);
  ContextKeys.Sort();
  ContextSchemaHash = FnvOffset64;
  FoldUnsigned(RegistryHash, static_cast<uint32>(ContextKeys.Num()));
  FoldUnsigned(ContextSchemaHash, static_cast<uint32>(ContextKeys.Num()));
  for (const FCrowdBehaviorContextTypeId Key : ContextKeys)
  {
    const FCrowdBehaviorContextSchema& Schema = ContextSchemas[Key];
    FoldUnsigned(RegistryHash, Key.Value);
    FoldUnsigned(RegistryHash, Schema.Version);
    FoldUnsigned(RegistryHash, Schema.Size);
    FoldUnsigned(ContextSchemaHash, Key.Value);
    FoldUnsigned(ContextSchemaHash, Schema.Version);
    FoldUnsigned(ContextSchemaHash, Schema.Size);
  }
  bInitialized = true;
  return true;
}

void FCrowdBehaviorSourceRuntime::Reset()
{
  CapabilityProfiles = {};
  Evaluators = {};
  ContextSchemas.Reset();
  EvaluationContexts.Reset();
  SourceSets.Reset();
  LastResolvedChannels.Reset();
  PendingCommands.Reset();
  WorkerInputCommandJournal.Reset();
  WorkerInputContextJournal.Reset();
  WorkerInputBindingJournal.Reset();
  PendingBindingUpdates.Reset();
  LastCommittedEvents.Reset();
  RegistryHash = 0;
  ContextSchemaHash = 0;
  bWorkerInputCommandJournalOverflowed = false;
  bInitialized = false;
}

bool FCrowdBehaviorSourceRuntime::RegisterEntity(
  const FCrowdStableEntityRef EntityRef,
  const FCrowdCapabilityBinding& Binding)
{
  if (!bInitialized || !EntityRef.IsValid()
    || SourceSets.Contains(EntityRef)
    || PendingBindingUpdates.Num()
      >= MaxBehaviorBindingUpdates)
    return false;
  FCrowdResolvedCapabilitySet Resolved;
  if (!CapabilityProfiles.Resolve(Binding, Resolved))
    return false;
  FCrowdBehaviorSourceSet Set;
  Set.EntityRef = EntityRef;
  Set.CapabilityBinding = Binding;
  Set.Revision = 1;
  Set.RecalculateStableHash();
  if (!Set.IsValid()) return false;
  FCrowdBehaviorCapabilityBindingUpdate InitialBinding;
  InitialBinding.EffectiveFixedStep = 0;
  InitialBinding.EntityRef = EntityRef;
  InitialBinding.Binding = Binding;
  InitialBinding.RecalculateStableHash();
  if (!InitialBinding.IsValid()) return false;
  SourceSets.Add(EntityRef, MoveTemp(Set));
  PendingBindingUpdates.Add(MoveTemp(InitialBinding));
  return true;
}

bool FCrowdBehaviorSourceRuntime::RemoveEntity(
  const FCrowdStableEntityRef EntityRef)
{
  if (!bInitialized || SourceSets.Remove(EntityRef) != 1)
    return false;
  LastResolvedChannels.Remove(EntityRef);
  EvaluationContexts.Remove(EntityRef);
  WorkerInputContextJournal.RemoveAll(
    [&](const auto& Context)
    {
      return Context.EntityRef == EntityRef;
    });
  WorkerInputCommandJournal.RemoveAll([&](const auto& Command)
  {
    return Command.Handle.EntityRef == EntityRef;
  });
  WorkerInputBindingJournal.RemoveAll([&](const auto& Update)
  {
    return Update.EntityRef == EntityRef;
  });
  PendingCommands.RemoveAll([&](const auto& Command)
  {
    return Command.Handle.EntityRef == EntityRef;
  });
  PendingBindingUpdates.RemoveAll([&](const auto& Update)
  {
    return Update.EntityRef == EntityRef;
  });
  return true;
}

bool FCrowdBehaviorSourceRuntime::QueueCommand(
  const FCrowdBehaviorSourceCommand& Command)
{
  if (!bInitialized || !Command.IsValid()
    || !SourceSets.Contains(Command.Handle.EntityRef)
    || PendingCommands.Num() >= MaxPendingBehaviorCommands)
    return false;
  PendingCommands.Add(Command);
  return true;
}

bool FCrowdBehaviorSourceRuntime::AcknowledgeWorkerInputCommands(
  const int32 Count)
{
  if (Count < 0 || Count > WorkerInputCommandJournal.Num())
    return false;
  if (Count > 0)
    WorkerInputCommandJournal.RemoveAt(
      0, Count, EAllowShrinking::No);
  return true;
}

bool FCrowdBehaviorSourceRuntime::AcknowledgeWorkerInputContexts(
  const int32 Count)
{
  if (Count < 0 || Count > WorkerInputContextJournal.Num())
    return false;
  if (Count > 0)
    WorkerInputContextJournal.RemoveAt(
      0, Count, EAllowShrinking::No);
  return true;
}

bool FCrowdBehaviorSourceRuntime::AcknowledgeWorkerInputBindings(
  const int32 Count)
{
  if (Count < 0 || Count > WorkerInputBindingJournal.Num())
    return false;
  if (Count > 0)
    WorkerInputBindingJournal.RemoveAt(
      0, Count, EAllowShrinking::No);
  return true;
}

bool FCrowdBehaviorSourceRuntime::QueueCapabilityBinding(
  const int64 EffectiveFixedStep,
  const FCrowdStableEntityRef EntityRef,
  const FCrowdCapabilityBinding& Binding)
{
  if (!bInitialized || EffectiveFixedStep < 0
    || !EntityRef.IsValid() || !Binding.IsValid()
    || !SourceSets.Contains(EntityRef)
    || PendingBindingUpdates.Num()
      >= MaxBehaviorBindingUpdates)
    return false;
  FCrowdBehaviorCapabilityBindingUpdate Update;
  Update.EffectiveFixedStep = EffectiveFixedStep;
  Update.EntityRef = EntityRef;
  Update.Binding = Binding;
  Update.RecalculateStableHash();
  PendingBindingUpdates.Add(Update);
  return true;
}

bool FCrowdBehaviorSourceRuntime::SetEvaluationContext(
  const FCrowdBehaviorEntityEvaluationContext& Context)
{
  if (!bInitialized || !Context.IsValid()
    || !SourceSets.Contains(Context.EntityRef))
    return false;
  for (const FCrowdBehaviorContextRecord& Record : Context.Records)
  {
    const FCrowdBehaviorContextSchema* Schema =
      ContextSchemas.Find(Record.TypeId);
    if (!Schema || Schema->Version != Record.SchemaVersion
      || Schema->Size != Record.Size)
      return false;
  }
  EvaluationContexts.Add(Context.EntityRef, Context);
  return true;
}

bool FCrowdBehaviorSourceRuntime::PrepareBoundary(
  const int64 FixedStepIndex,
  FCrowdBehaviorPreparedBoundary& OutPrepared) const
{
  OutPrepared = {};
  if (!bInitialized || FixedStepIndex < 0) return false;

  TArray<FCrowdStableEntityRef> EntityRefs;
  SourceSets.GetKeys(EntityRefs);
  EntityRefs.Sort();
  OutPrepared.FixedStepIndex = FixedStepIndex;
  OutPrepared.RegistryHash = RegistryHash;
  OutPrepared.Entities.Reserve(EntityRefs.Num());
  uint64 SourceSetHash = FnvOffset64;
  uint64 CommandHash = FnvOffset64;
  uint64 ResolvedHash = FnvOffset64;
  TArray<FCrowdBehaviorCapabilityBindingUpdate> DueBindingUpdates;
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update
    : PendingBindingUpdates)
  {
    if (Update.EffectiveFixedStep <= FixedStepIndex)
      DueBindingUpdates.Add(Update);
  }
  DueBindingUpdates.Sort([](
    const FCrowdBehaviorCapabilityBindingUpdate& A,
    const FCrowdBehaviorCapabilityBindingUpdate& B)
  {
    if (A.EffectiveFixedStep != B.EffectiveFixedStep)
      return A.EffectiveFixedStep < B.EffectiveFixedStep;
    if (!(A.EntityRef == B.EntityRef))
      return A.EntityRef < B.EntityRef;
    return A.StableHash < B.StableHash;
  });
  for (int32 Index = 1; Index < DueBindingUpdates.Num(); ++Index)
  {
    const auto& Previous = DueBindingUpdates[Index - 1];
    const auto& Current = DueBindingUpdates[Index];
    if (Previous.EffectiveFixedStep == Current.EffectiveFixedStep
      && Previous.EntityRef == Current.EntityRef
      && Previous.StableHash != Current.StableHash)
      return false;
  }

  for (const FCrowdStableEntityRef EntityRef : EntityRefs)
  {
    const FCrowdBehaviorSourceSet& Current = SourceSets[EntityRef];
    FCrowdBehaviorSourceSet BoundaryBase = Current;
    bool bBindingChanged = false;
    for (const FCrowdBehaviorCapabilityBindingUpdate& Update
      : DueBindingUpdates)
    {
      if (Update.EffectiveFixedStep <= FixedStepIndex
        && Update.EntityRef == EntityRef)
      {
        if (!Update.IsValid()) return false;
        if (!AreBindingsEqual(
            BoundaryBase.CapabilityBinding, Update.Binding))
        {
          BoundaryBase.CapabilityBinding = Update.Binding;
          bBindingChanged = true;
        }
      }
    }
    if (bBindingChanged) BoundaryBase.RecalculateStableHash();

    FCrowdResolvedCapabilitySet Capabilities;
    if (!CapabilityProfiles.Resolve(
        BoundaryBase.CapabilityBinding, Capabilities))
      return false;
    TArray<FCrowdBehaviorSourceCommand> DueCommands;
    for (const FCrowdBehaviorSourceCommand& Command : PendingCommands)
      if (Command.EffectiveFixedStep <= FixedStepIndex
        && Command.Handle.EntityRef == EntityRef)
        DueCommands.Add(Command);

    FCrowdBehaviorPreparedEntity& Prepared =
      OutPrepared.Entities.AddDefaulted_GetRef();
    Prepared.EntityRef = EntityRef;
    Prepared.BaseSourceSetHash = Current.StableHash;
    if (!FCrowdBehaviorSourceStateMachine::Apply(
        BoundaryBase, DueCommands, FixedStepIndex,
        Evaluators.GetSpecs(), Capabilities,
        Prepared.StagedSourceSet, Prepared.Events,
        Prepared.CommandBatchHash))
      return false;
    if (bBindingChanged
      && Prepared.StagedSourceSet.Revision == Current.Revision)
    {
      ++Prepared.StagedSourceSet.Revision;
      if (Prepared.StagedSourceSet.Revision == 0)
        Prepared.StagedSourceSet.Revision = 1;
      Prepared.StagedSourceSet.RecalculateStableHash();
    }

    const FCrowdBehaviorEntityEvaluationContext* EntityContext =
      EvaluationContexts.Find(EntityRef);
    if (EntityContext
      && (EntityContext->FixedStepIndex != FixedStepIndex
        || !EntityContext->IsValid()))
      return false;
    Prepared.EvaluationContextHash = EntityContext
      ? EntityContext->StableHash : FnvOffset64;
    FCrowdBehaviorContributions Contributions;
    bool bStateChanged = false;
    for (FCrowdBehaviorSourceInstance& Instance
      : Prepared.StagedSourceSet.Instances)
    {
      const FCrowdBehaviorSourceSpec* Spec =
        Evaluators.FindSpec(Instance.SourceTypeId);
      const auto Evaluator =
        Evaluators.FindEvaluator(Instance.SourceTypeId);
      if (!Spec || !Evaluator.IsValid()) return false;
      FCrowdBehaviorSourceEvaluationContext Context;
      Context.FixedStepIndex = FixedStepIndex;
      Context.Position = EntityContext
        ? EntityContext->Position : FVector::ZeroVector;
      Context.Velocity = EntityContext
        ? EntityContext->Velocity : FVector::ZeroVector;
      Context.Facing = EntityContext
        ? EntityContext->Facing : FVector::ForwardVector;
      Context.Capabilities = Capabilities;
      Context.Instance = Instance;
      if (EntityContext)
        Context.ContextRecords = EntityContext->Records;
      FCrowdBehaviorContributionWriter Writer(
        *Spec, Instance, Contributions);
      if (!Evaluator->Evaluate(Context, Writer)
        || !Writer.Succeeded())
        return false;
      if (Writer.HasNextState()
        && !(Writer.GetNextState() == Instance.State))
      {
        Instance.State = Writer.GetNextState();
        bStateChanged = true;
      }
    }
    if (bStateChanged)
    {
      if (Prepared.StagedSourceSet.Revision == Current.Revision)
      {
        ++Prepared.StagedSourceSet.Revision;
        if (Prepared.StagedSourceSet.Revision == 0)
          Prepared.StagedSourceSet.Revision = 1;
      }
      Prepared.StagedSourceSet.RecalculateStableHash();
    }
    if (!FCrowdBehaviorResolver::Resolve(
        Contributions, Prepared.ResolvedChannels))
      return false;
    Prepared.StableHash = CalculatePreparedEntityHash(Prepared);
    if (Prepared.StableHash == 0) return false;
    FoldUnsigned(SourceSetHash, Prepared.StagedSourceSet.StableHash);
    FoldUnsigned(CommandHash, Prepared.CommandBatchHash);
    FoldUnsigned(ResolvedHash, Prepared.ResolvedChannels.StableHash);
  }

  for (const FCrowdBehaviorSourceCommand& Command : PendingCommands)
    if (Command.EffectiveFixedStep <= FixedStepIndex
      && !SourceSets.Contains(Command.Handle.EntityRef))
      return false;
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update
    : PendingBindingUpdates)
    if (Update.EffectiveFixedStep <= FixedStepIndex
      && !SourceSets.Contains(Update.EntityRef))
      return false;

  OutPrepared.SourceSetHash = SourceSetHash;
  OutPrepared.CommandBatchHash = CommandHash;
  OutPrepared.ResolvedChannelHash = ResolvedHash;
  OutPrepared.StableHash =
    CalculatePreparedBoundaryHash(OutPrepared);
  OutPrepared.bValid = OutPrepared.StableHash != 0;
  return OutPrepared.bValid;
}

bool FCrowdBehaviorSourceRuntime::CommitPrepared(
  const FCrowdBehaviorPreparedBoundary& Prepared)
{
  if (!ValidatePrepared(Prepared)) return false;
  int32 DueCommandCount = 0;
  for (const FCrowdBehaviorSourceCommand& Command : PendingCommands)
    if (Command.EffectiveFixedStep <= Prepared.FixedStepIndex)
      ++DueCommandCount;
  int32 CommittedContextCount = 0;
  int32 DueBindingCount = 0;
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update
    : PendingBindingUpdates)
    if (Update.EffectiveFixedStep <= Prepared.FixedStepIndex)
      ++DueBindingCount;
  for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
  {
    const FCrowdBehaviorEntityEvaluationContext* Context =
      EvaluationContexts.Find(Entity.EntityRef);
    if (Context
      && Context->FixedStepIndex == Prepared.FixedStepIndex)
      ++CommittedContextCount;
  }
  if (WorkerInputCommandJournal.Num() + DueCommandCount
      > MaxPendingBehaviorCommands
    || WorkerInputContextJournal.Num()
      + CommittedContextCount > MaxPendingBehaviorCommands
    || WorkerInputBindingJournal.Num() + DueBindingCount
      > MaxBehaviorBindingUpdates)
  {
    bWorkerInputCommandJournalOverflowed = true;
    return false;
  }
  LastCommittedEvents.Reset();
  for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
  {
    SourceSets[Entity.EntityRef] = Entity.StagedSourceSet;
    LastResolvedChannels.Add(
      Entity.EntityRef, Entity.ResolvedChannels);
    LastCommittedEvents.Append(Entity.Events);
    const FCrowdBehaviorEntityEvaluationContext* Context =
      EvaluationContexts.Find(Entity.EntityRef);
    if (Context
      && Context->FixedStepIndex == Prepared.FixedStepIndex)
      WorkerInputContextJournal.Add(*Context);
  }
  for (const FCrowdBehaviorSourceCommand& Command : PendingCommands)
  {
    if (Command.EffectiveFixedStep > Prepared.FixedStepIndex)
      continue;
    WorkerInputCommandJournal.Add(Command);
  }
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update
    : PendingBindingUpdates)
  {
    if (Update.EffectiveFixedStep <= Prepared.FixedStepIndex)
      WorkerInputBindingJournal.Add(Update);
  }
  PendingCommands.RemoveAll([&](const auto& Command)
  {
    return Command.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  PendingBindingUpdates.RemoveAll([&](const auto& Update)
  {
    return Update.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  return true;
}

bool FCrowdBehaviorSourceRuntime::CommitWorkerPrepared(
  const FCrowdBehaviorPreparedBoundary& Prepared,
  const TConstArrayView<FCrowdBehaviorWorkerCommitEntity> WorkerEntities,
  const TConstArrayView<FCrowdBehaviorSourceEvent> WorkerEvents)
{
  if (!ValidatePrepared(Prepared)
    || WorkerEntities.Num() != Prepared.Entities.Num())
    return false;

  FCrowdStableEntityRef PreviousRef;
  for (int32 Index = 0; Index < Prepared.Entities.Num(); ++Index)
  {
    const FCrowdBehaviorPreparedEntity& Expected =
      Prepared.Entities[Index];
    const FCrowdBehaviorWorkerCommitEntity& Worker =
      WorkerEntities[Index];
    if ((!PreviousRef.IsUnset() && !(PreviousRef < Worker.EntityRef))
      || Worker.EntityRef != Expected.EntityRef
      || Worker.SourceSet.EntityRef != Worker.EntityRef
      || !Worker.SourceSet.IsValid()
      || !Worker.ResolvedChannels.bValid
      || (Worker.SourceSet.StableHash
          != Expected.StagedSourceSet.StableHash
        && (Worker.SourceSet.Revision
            < Expected.StagedSourceSet.Revision
          || CalculateSourceSetContentHash(Worker.SourceSet)
            != CalculateSourceSetContentHash(
              Expected.StagedSourceSet)))
      || Worker.ResolvedChannels.StableHash
        != Expected.ResolvedChannels.StableHash
      || Worker.EvaluationContextHash
        != Expected.EvaluationContextHash)
      return false;
    PreviousRef = Worker.EntityRef;
  }
  for (const FCrowdBehaviorSourceEvent& Event : WorkerEvents)
  {
    if (Event.Kind >= ECrowdBehaviorSourceEventKind::Count
      || Event.FixedStepIndex < 0
      || Event.FixedStepIndex > Prepared.FixedStepIndex
      || !Event.Handle.IsValid()
      || !Event.SourceTypeId.IsValid()
      || !SourceSets.Contains(Event.Handle.EntityRef))
      return false;
  }

  LastCommittedEvents.Reset(WorkerEvents.Num());
  LastCommittedEvents.Append(WorkerEvents);
  for (int32 Index = 0; Index < WorkerEntities.Num(); ++Index)
  {
    const FCrowdBehaviorWorkerCommitEntity& Worker =
      WorkerEntities[Index];
    SourceSets[Worker.EntityRef] = Worker.SourceSet;
    LastResolvedChannels.Add(
      Worker.EntityRef, Worker.ResolvedChannels);
  }
  PendingCommands.RemoveAll([&](const auto& Command)
  {
    return Command.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  PendingBindingUpdates.RemoveAll([&](const auto& Update)
  {
    return Update.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  return true;
}

bool FCrowdBehaviorSourceRuntime::CommitWorkerAuthoritative(
  const FCrowdBehaviorPreparedBoundary& Prepared,
  const TConstArrayView<FCrowdBehaviorWorkerCommitEntity> WorkerEntities,
  const TConstArrayView<FCrowdBehaviorSourceEvent> WorkerEvents)
{
  if (!ValidatePrepared(Prepared)
    || WorkerEntities.Num() != Prepared.Entities.Num())
    return false;

  FCrowdStableEntityRef PreviousRef;
  for (int32 Index = 0; Index < Prepared.Entities.Num(); ++Index)
  {
    const FCrowdBehaviorPreparedEntity& Expected =
      Prepared.Entities[Index];
    const FCrowdBehaviorWorkerCommitEntity& Worker =
      WorkerEntities[Index];
    const FCrowdBehaviorSourceSet* Current =
      SourceSets.Find(Worker.EntityRef);
    if ((!PreviousRef.IsUnset() && !(PreviousRef < Worker.EntityRef))
      || Worker.EntityRef != Expected.EntityRef
      || Worker.SourceSet.EntityRef != Worker.EntityRef
      || !Current
      || !Worker.SourceSet.IsValid()
      || Worker.SourceSet.Revision < Current->Revision
      || !Worker.ResolvedChannels.bValid
      || Worker.EvaluationContextHash == 0)
      return false;
    PreviousRef = Worker.EntityRef;
  }
  for (const FCrowdBehaviorSourceEvent& Event : WorkerEvents)
  {
    if (Event.Kind >= ECrowdBehaviorSourceEventKind::Count
      || Event.FixedStepIndex < 0
      || Event.FixedStepIndex > Prepared.FixedStepIndex
      || !Event.Handle.IsValid()
      || !Event.SourceTypeId.IsValid()
      || !SourceSets.Contains(Event.Handle.EntityRef))
      return false;
  }

  LastCommittedEvents.Reset(WorkerEvents.Num());
  LastCommittedEvents.Append(WorkerEvents);
  for (const FCrowdBehaviorWorkerCommitEntity& Worker : WorkerEntities)
  {
    SourceSets[Worker.EntityRef] = Worker.SourceSet;
    LastResolvedChannels.Add(
      Worker.EntityRef, Worker.ResolvedChannels);
  }
  PendingCommands.RemoveAll([&](const auto& Command)
  {
    return Command.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  PendingBindingUpdates.RemoveAll([&](const auto& Update)
  {
    return Update.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  return true;
}

bool FCrowdBehaviorSourceRuntime::ValidatePrepared(
  const FCrowdBehaviorPreparedBoundary& Prepared) const
{
  if (!bInitialized || !Prepared.bValid
    || Prepared.FixedStepIndex < 0
    || Prepared.RegistryHash != RegistryHash
    || Prepared.Entities.Num() != SourceSets.Num()
    || Prepared.StableHash != CalculatePreparedBoundaryHash(Prepared))
    return false;
  uint64 SourceSetHash = FnvOffset64;
  uint64 CommandHash = FnvOffset64;
  uint64 ResolvedHash = FnvOffset64;
  FCrowdStableEntityRef PreviousRef;
  for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
  {
    if (!PreviousRef.IsUnset() && !(PreviousRef < Entity.EntityRef))
      return false;
    const FCrowdBehaviorSourceSet* Current =
      SourceSets.Find(Entity.EntityRef);
    const FCrowdBehaviorEntityEvaluationContext* Context =
      EvaluationContexts.Find(Entity.EntityRef);
    const uint64 ExpectedContextHash = Context
      && Context->FixedStepIndex == Prepared.FixedStepIndex
      ? Context->StableHash : FnvOffset64;
    if (!Current
      || Current->StableHash != Entity.BaseSourceSetHash
      || Entity.EvaluationContextHash != ExpectedContextHash
      || !Entity.StagedSourceSet.IsValid()
      || !Entity.ResolvedChannels.bValid
      || Entity.StableHash != CalculatePreparedEntityHash(Entity))
      return false;
    FoldUnsigned(SourceSetHash, Entity.StagedSourceSet.StableHash);
    FoldUnsigned(CommandHash, Entity.CommandBatchHash);
    FoldUnsigned(ResolvedHash, Entity.ResolvedChannels.StableHash);
    PreviousRef = Entity.EntityRef;
  }
  return Prepared.SourceSetHash == SourceSetHash
    && Prepared.CommandBatchHash == CommandHash
    && Prepared.ResolvedChannelHash == ResolvedHash;
}

void FCrowdBehaviorSourceRuntime::RollbackPendingCommandsTo(
  const int32 Count)
{
  check(Count >= 0 && Count <= PendingCommands.Num());
  if (Count < PendingCommands.Num())
    PendingCommands.SetNum(Count, EAllowShrinking::No);
}

const FCrowdBehaviorSourceSet*
FCrowdBehaviorSourceRuntime::FindSourceSet(
  const FCrowdStableEntityRef EntityRef) const
{
  return SourceSets.Find(EntityRef);
}

bool FCrowdBehaviorSourceRuntime::ApplyReplicatedSourceSet(
  const FCrowdBehaviorSourceSet& ReplicatedSet)
{
  if (!bInitialized || !ReplicatedSet.IsValid())
    return false;
  FCrowdBehaviorSourceSet* Current =
    SourceSets.Find(ReplicatedSet.EntityRef);
  if (!Current
    || Current->CapabilityBinding.ProfileKey
      != ReplicatedSet.CapabilityBinding.ProfileKey)
    return false;
  if (ReplicatedSet.Revision < Current->Revision)
    return false;
  if (ReplicatedSet.Revision == Current->Revision)
    return ReplicatedSet.StableHash == Current->StableHash;
  for (const FCrowdBehaviorSourceInstance& Instance
    : ReplicatedSet.Instances)
  {
    const FCrowdBehaviorSourceSpec* Spec =
      Evaluators.FindSpec(Instance.SourceTypeId);
    if (!Spec
      || Spec->Version != Instance.SourceVersion
      || Spec->PayloadSchemaId != Instance.Payload.SchemaId
      || Spec->StateSchemaId != Instance.State.SchemaId
      || Spec->ReplicationPolicy
        != Instance.ReplicationPolicy
      || Instance.ReplicationPolicy
        != ECrowdBehaviorSourceReplicationPolicy::Predictable)
      return false;
  }
  *Current = ReplicatedSet;
  return true;
}

const FCrowdResolvedBehaviorChannels*
FCrowdBehaviorSourceRuntime::FindResolvedChannels(
  const FCrowdStableEntityRef EntityRef) const
{
  return LastResolvedChannels.Find(EntityRef);
}

bool FCrowdBehaviorSourceRuntime::IsSourceActive(
  const FCrowdBehaviorSourceHandle& Handle) const
{
  const FCrowdBehaviorSourceSet* Set =
    SourceSets.Find(Handle.EntityRef);
  return Set && Set->Instances.ContainsByPredicate(
    [&](const FCrowdBehaviorSourceInstance& Instance)
    {
      return Instance.Handle == Handle;
    });
}

#undef FoldRef
#undef FoldUnsigned
#undef FnvPrime64
#undef FnvOffset64

bool FCrowdBehaviorSourceRuntime::HasCommittedEvent(
  const FCrowdBehaviorSourceHandle& Handle,
  const ECrowdBehaviorSourceEventKind Kind,
  const int64 MinimumFixedStep) const
{
  return LastCommittedEvents.ContainsByPredicate(
    [&](const FCrowdBehaviorSourceEvent& Event)
    {
      return Event.Handle == Handle
        && Event.Kind == Kind
        && Event.FixedStepIndex >= MinimumFixedStep;
    });
}
