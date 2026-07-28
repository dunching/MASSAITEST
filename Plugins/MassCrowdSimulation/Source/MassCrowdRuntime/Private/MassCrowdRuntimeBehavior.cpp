#include "MassCrowdRuntimeBehavior.h"

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

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
}

bool FCrowdBusinessCommitRequest::IsValid() const
{
  if (Kind == ECrowdBusinessCommitKind::None)
    return CommitId == 0 && Quantity == 0;
  return Kind < ECrowdBusinessCommitKind::Count
    && CommitId != 0
    && FixedStepIndex >= 0
    && TransitionRevision != 0
    && AgentRef.IsValid()
    && (TaskRef.IsUnset() || TaskRef.IsValid())
    && (TargetRef.IsUnset() || TargetRef.IsValid())
    && Quantity > 0;
}

uint64 FCrowdBehaviorCommitId::Make(
  const ECrowdBusinessCommitKind Kind,
  const FCrowdRuntimeBehaviorContext& Context)
{
  if (Context.ExternalCommitId != 0) return Context.ExternalCommitId;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, static_cast<uint8>(Kind));
  FoldUnsigned(Hash, static_cast<uint64>(Context.FixedStepIndex));
  FoldUnsigned(Hash, Context.TransitionRevision);
  FoldRef(Hash, Context.AgentFacts.StableEntityRef);
  FoldRef(Hash, Context.TaskRef);
  FoldRef(Hash, Context.TargetRef);
  FoldUnsigned(Hash, Context.InteractionPayloadKey);
  FoldUnsigned(Hash, static_cast<uint32>(Context.InteractionQuantity));
  return Hash == 0 ? 1 : Hash;
}
