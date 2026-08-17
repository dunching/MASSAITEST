#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoRoundWorkKernel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoRoundWorkComposeTest,
  "CrowdDemo.SF.Work.GuidanceComposeOrderAndIsolation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoRoundWorkComposeTest::RunTest(const FString& Parameters)
{
  FCrowdDemoRoundWorkInput Input;
  Input.FixedStepIndex = 17;
  for (const int32 AgentId : {2, 1})
  {
    FCrowdDemoRoundWorkAgentInput& Agent = Input.Agents.AddDefaulted_GetRef();
    Agent.AgentId = AgentId;
    Agent.PlanRevision = 3;
    Agent.StopLocation = FVector(AgentId * 10.0f, 0.0f, 60.0f);
    Agent.SharedFlow = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
      AgentId, ECrowdDemoGuidanceProvider::SharedFlow, 3,
      FVector(100.0f, 0.0f, 0.0f), Agent.StopLocation, 0.0f, true);
    Agent.TargetRegion = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
      AgentId, ECrowdDemoGuidanceProvider::TargetRegion, 3,
      FVector(0.0f, 200.0f, 0.0f), Agent.StopLocation, 90.0f,
      AgentId == 1);
  }
  const FCrowdDemoRoundWorkOutput Forward =
    FCrowdDemoRoundWorkKernel::ComposeGuidance(Input);
  TestTrue(TEXT("work output valid"), Forward.bValid);
  TestEqual(TEXT("work output stable order first"),
    Forward.ComposedGuidance[0].AgentId, 1);
  TestEqual(TEXT("target provider wins for eligible agent"),
    Forward.ComposedGuidance[0].SelectedProvider,
    ECrowdDemoGuidanceProvider::TargetRegion);
  TestEqual(TEXT("flow remains provider for other agent"),
    Forward.ComposedGuidance[1].SelectedProvider,
    ECrowdDemoGuidanceProvider::SharedFlow);

  Algo::Reverse(Input.Agents);
  const FCrowdDemoRoundWorkOutput Reversed =
    FCrowdDemoRoundWorkKernel::ComposeGuidance(Input);
  TestTrue(TEXT("reversed work output valid"), Reversed.bValid);
  TestEqual(TEXT("input order does not change work hash"),
    Reversed.StableHash, Forward.StableHash);
  return true;
}

#endif
