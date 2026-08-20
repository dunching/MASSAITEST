#include "Mass/CrowdDemoT7PresentationEventStream.h"

bool FCrowdDemoT7PresentationEventStream::Enqueue(
  const FCrowdDemoT7PresentationEvent& Event)
{
  if (Event.RoundId < 0 || Event.AgentId < 0
    || Event.LifecycleSerial <= 0 || Event.FixedStepIndex < 0
    || !FMath::IsFinite(Event.ServerTimeSeconds))
    return false;
  if (RoundId != Event.RoundId)
  {
    Reset();
    RoundId = Event.RoundId;
  }
  FTrack& Track = Tracks.FindOrAdd(Event.AgentId);
  if ((Track.bHasCurrent
      && Track.Current.LifecycleSerial == Event.LifecycleSerial
      && Track.Current.FixedStepIndex == Event.FixedStepIndex)
    || Track.Pending.ContainsByPredicate(
      [&Event](const FCrowdDemoT7PresentationEvent& Pending)
      {
        return Pending.LifecycleSerial == Event.LifecycleSerial
          && Pending.FixedStepIndex == Event.FixedStepIndex;
      }))
    return true;
  Track.Pending.Add(Event);
  Track.Pending.Sort([](
    const FCrowdDemoT7PresentationEvent& A,
    const FCrowdDemoT7PresentationEvent& B)
  {
    return A.FixedStepIndex < B.FixedStepIndex;
  });
  return true;
}

bool FCrowdDemoT7PresentationEventStream::Resolve(
  const int32 ExpectedRoundId,
  const int32 AgentId,
  const int32 LifecycleSerial,
  const double WorldSeconds,
  FCrowdDemoT7PresentationEvent& OutEvent)
{
  FTrack* Track = Tracks.Find(AgentId);
  if (ExpectedRoundId != RoundId || !Track
    || !FMath::IsFinite(WorldSeconds))
    return false;
  if (!Track->bHasCurrent && !Track->Pending.IsEmpty())
  {
    Track->Current = Track->Pending[0];
    Track->Pending.RemoveAt(0, 1, EAllowShrinking::No);
    Track->NextAdvanceWorldSeconds =
      WorldSeconds + MinimumPresentationSeconds;
    Track->bHasCurrent = true;
  }
  else if (Track->bHasCurrent && !Track->Pending.IsEmpty()
    && WorldSeconds >= Track->NextAdvanceWorldSeconds)
  {
    Track->Current = Track->Pending[0];
    Track->Pending.RemoveAt(0, 1, EAllowShrinking::No);
    Track->NextAdvanceWorldSeconds =
      WorldSeconds + MinimumPresentationSeconds;
  }
  if (!Track->bHasCurrent
    || Track->Current.LifecycleSerial != LifecycleSerial)
    return false;
  OutEvent = Track->Current;
  return true;
}

void FCrowdDemoT7PresentationEventStream::Reset()
{
  RoundId = INDEX_NONE;
  Tracks.Reset();
}
