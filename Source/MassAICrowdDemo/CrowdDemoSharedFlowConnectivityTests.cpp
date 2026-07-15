#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

namespace
{
  FCrowdDemoSharedFlowFieldConfig MakeConnectivityConfig(
    const FVector& BoundsMax = FVector(500.0f, 500.0f, 0.0f))
  {
    FCrowdDemoSharedFlowFieldConfig Config;
    Config.Revision = 71;
    Config.BoundsMin = FVector::ZeroVector;
    Config.BoundsMax = BoundsMax;
    Config.CellSizeCm = 100.0f;
    Config.AgentInflateCm = 0.0f;
    Config.ConnectivityContractVersion = 1;
    Config.GoalLocation = FVector(BoundsMax.X - 50.0f, BoundsMax.Y - 50.0f, 60.0f);
    return Config;
  }

  void AddObstacle(
    FCrowdDemoSharedFlowFieldConfig& Config,
    const int32 Id,
    const FVector& Center,
    const FVector& Extent)
  {
    auto& Obstacle = Config.ObstacleSpecs.AddDefaulted_GetRef();
    Obstacle.ObstacleId = Id;
    Obstacle.Center = Center;
    Obstacle.Extent = Extent;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSharedFlowCellConnectivityTest,
  "CrowdDemo.SoftPressure.FlowConnectivity.CellGraphContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSharedFlowCellConnectivityTest::RunTest(const FString& Parameters)
{
  {
    const auto Config = MakeConnectivityConfig();
    FCrowdDemoSharedFlowField Field;
    TestTrue(TEXT("open field builds"), FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));
    TestTrue(TEXT("open field has directed edges"), Field.ValidDirectedEdgeCount > 0);
    TestTrue(TEXT("open cardinal edge is traversable"),
      FCrowdDemoSharedFlowFieldKernel::CanTraverseCellEdge(Field, 0, 1));
    for (int32 CellIndex = 0; CellIndex < Field.Width * Field.Height; ++CellIndex)
    {
      TestFalse(FString::Printf(TEXT("open cell %d is not blocked"), CellIndex),
        Field.Blocked[CellIndex]);
      TestFalse(FString::Printf(TEXT("open cell %d is reachable"), CellIndex),
        Field.Unreachable[CellIndex]);
      if (CellIndex != Field.GoalCellIndex)
      {
        TestTrue(FString::Printf(TEXT("open cell %d has a next edge"), CellIndex),
          Field.NextCellIndex[CellIndex] != INDEX_NONE);
        TestTrue(FString::Printf(TEXT("next edge for cell %d obeys contract"), CellIndex),
          FCrowdDemoSharedFlowFieldKernel::CanTraverseCellEdge(
            Field, CellIndex, Field.NextCellIndex[CellIndex]));
      }
    }
  }

  {
    FCrowdDemoParticleProfile Profile;
    TestEqual(TEXT("profile hard clearance is 52cm"),
      Profile.GetNavigationHardClearanceCm(), 52.0f);
    TestEqual(TEXT("profile wall soft distance is 69cm"),
      Profile.GetWallSoftDistanceCm(), 69.0f);

    auto MakeClearanceField = [](const FCrowdDemoParticleProfile& InProfile)
    {
      FCrowdDemoSharedFlowFieldConfig Config = MakeConnectivityConfig(
        FVector(200.0f, 200.0f, 0.0f));
      Config.BoundsMin = FVector(-200.0f, -200.0f, 0.0f);
      Config.BoundsMax = FVector(200.0f, 200.0f, 0.0f);
      Config.GoalLocation = FVector(-50.0f, 50.0f, 60.0f);
      Config.AgentInflateCm = InProfile.GetNavigationHardClearanceCm();
      AddObstacle(Config, 20, FVector(106.0f, 50.0f, 60.0f),
        FVector(4.0f, 20.0f, 100.0f));
      FCrowdDemoSharedFlowField Field;
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Field);
      return Field;
    };

    FCrowdDemoParticleProfile Clearance48 = Profile;
    Clearance48.HardSafetyGapCm = 6.0f;
    const FCrowdDemoSharedFlowField Field48 = MakeClearanceField(Clearance48);
    const FCrowdDemoSharedFlowField Field52 = MakeClearanceField(Profile);
    const int32 Probe48 = Field48.LocationToCellIndex(FVector(50.0f, 50.0f, 60.0f));
    const int32 Probe52 = Field52.LocationToCellIndex(FVector(50.0f, 50.0f, 60.0f));
    TestFalse(TEXT("48cm clearance leaves probe node valid"), Field48.Blocked[Probe48]);
    TestTrue(TEXT("52cm clearance blocks probe node"), Field52.Blocked[Probe52]);
    TestNotEqual(TEXT("hard clearance changes graph hash"),
      Field48.BuildHash, Field52.BuildHash);

    FCrowdDemoParticleProfile WiderSoft = Profile;
    WiderSoft.SoftMarginCm = 41.0f;
    const FCrowdDemoSharedFlowField WiderSoftField = MakeClearanceField(WiderSoft);
    TestEqual(TEXT("soft margin does not change hard graph hash"),
      WiderSoftField.BuildHash, Field52.BuildHash);
    TestTrue(TEXT("soft margin does not change blocked graph"),
      WiderSoftField.Blocked == Field52.Blocked);
    TestTrue(TEXT("soft margin does not change reachability"),
      WiderSoftField.Unreachable == Field52.Unreachable);
  }

  {
    auto Config = MakeConnectivityConfig(FVector(300.0f, 100.0f, 0.0f));
    AddObstacle(Config, 1, FVector(100.0f, 50.0f, 60.0f), FVector(1.0f, 40.0f, 100.0f));
    FCrowdDemoSharedFlowField Field;
    TestTrue(TEXT("thin-wall field builds"), FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));
    TestFalse(TEXT("thin wall does not block either adjacent center"), Field.Blocked[0] || Field.Blocked[1]);
    TestFalse(TEXT("thin wall removes center-to-center edge"),
      FCrowdDemoSharedFlowFieldKernel::CanTraverseCellEdge(Field, 0, 1));
    TestTrue(TEXT("Dijkstra cannot cross thin wall"), Field.Unreachable[0]);
  }

  {
    auto Config = MakeConnectivityConfig(FVector(200.0f, 200.0f, 0.0f));
    AddObstacle(Config, 1, FVector(150.0f, 50.0f, 60.0f), FVector(40.0f, 40.0f, 100.0f));
    FCrowdDemoSharedFlowField Field;
    TestTrue(TEXT("corner field builds"), FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));
    TestFalse(TEXT("diagonal target remains a valid node"), Field.Blocked[3]);
    TestTrue(TEXT("orthogonal blocker exists"), Field.Blocked[1]);
    TestFalse(TEXT("diagonal corner cut is rejected"),
      FCrowdDemoSharedFlowFieldKernel::CanTraverseCellEdge(Field, 0, 3));
  }

  {
    auto Config = MakeConnectivityConfig();
    Config.AgentInflateCm = 52.0f;
    AddObstacle(Config, 10, FVector(100.0f, 250.0f, 60.0f), FVector(100.0f, 10.0f, 100.0f));
    AddObstacle(Config, 11, FVector(400.0f, 250.0f, 60.0f), FVector(100.0f, 10.0f, 100.0f));
    Config.GoalLocation = FVector(250.0f, 450.0f, 60.0f);
    FCrowdDemoSharedFlowField Field;
    TestTrue(TEXT("100cm-gap field builds"), FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));
    const auto Start = FCrowdDemoSharedFlowFieldKernel::Sample(Field, FVector(250.0f, 150.0f, 60.0f));
    TestTrue(TEXT("100cm gap has no 104cm-hard cell chain"), Start.bUnreachable || Start.bBlocked);
  }

  {
    FCrowdDemoSharedFlowFieldConfig Config = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
    Config.AgentInflateCm = 52.0f;
    Config.ConnectivityContractVersion = 1;
    FCrowdDemoSharedFlowField A, B;
    TestTrue(TEXT("Agent0 hard-clearance graph builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, A));
    Algo::Reverse(Config.ObstacleSpecs);
    TestTrue(TEXT("reordered Agent0 graph builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, B));
    TestEqual(TEXT("obstacle input order does not alter graph hash"), B.BuildHash, A.BuildHash);
    const FVector Agent0(2449.0f, -956.0f, 60.0f);
    const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(A, Agent0);
    TestTrue(TEXT("Agent0 cannot receive unsafe direct desired segment"),
      Sample.FlowDirection.IsNearlyZero()
      || FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
        A.Config, Agent0,
        Agent0 + Sample.FlowDirection * FMath::Min(800.0f / 30.0f,
          Sample.GuidanceDistanceCm)));
    FCrowdDemoSharedFlowField C;
    Algo::Reverse(Config.ObstacleSpecs);
    TestTrue(TEXT("second stable Agent0 graph builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, C));
    TestEqual(TEXT("consecutive Agent0 builds have the same hash"),
      C.BuildHash, A.BuildHash);
  }

  {
    FCrowdDemoSharedFlowField Sf1;
    TestTrue(TEXT("SF1 legacy field builds"), FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1), Sf1));
    TestEqual(TEXT("SF1 legacy hash remains golden"), Sf1.BuildHash, 267519150u);
    const int32 Agent0Cell = Sf1.LocationToCellIndex(FVector(2449.0f, -956.0f, 60.0f));
    TestEqual(TEXT("SF1 Agent0 remains in legacy cell"), Agent0Cell, 1246);
    TestFalse(TEXT("SF1 Agent0 legacy cell remains unblocked"), Sf1.Blocked[Agent0Cell]);
    TestFalse(TEXT("SF1 Agent0 legacy cell remains reachable"), Sf1.Unreachable[Agent0Cell]);
    TestTrue(TEXT("SF1 Agent0 legacy next cell remains present"),
      Sf1.NextCellIndex[Agent0Cell] != INDEX_NONE);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSharedFlowV2ConnectionGraphTest,
  "CrowdDemo.SoftPressure.FlowConnectivity.V2CellConnectionGraph",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSharedFlowV2ConnectionGraphTest::RunTest(const FString& Parameters)
{
  auto MakeCorridor = [](const float Clearance, const float Width)
  {
    FCrowdDemoSharedFlowFieldConfig Config = MakeConnectivityConfig(
      FVector(400.0f, 400.0f, 0.0f));
    Config.AgentInflateCm = Clearance;
    Config.ConnectivityContractVersion = 2;
    const float LeftSurface = 200.0f - Width * 0.5f;
    const float RightSurface = 200.0f + Width * 0.5f;
    AddObstacle(Config, 101, FVector(LeftSurface * 0.5f, 200.0f, 60.0f),
      FVector(LeftSurface * 0.5f, 200.0f, 100.0f));
    AddObstacle(Config, 102, FVector((RightSurface + 400.0f) * 0.5f, 200.0f, 60.0f),
      FVector((400.0f - RightSurface) * 0.5f, 200.0f, 100.0f));
    Config.GoalLocation = FVector(200.0f, 340.0f, 60.0f);
    return Config;
  };
  auto HasEdge = [](const FCrowdDemoSharedFlowField& Field, uint64 A, uint64 B)
  {
    const uint64 Min = FMath::Min(A, B);
    const uint64 Max = FMath::Max(A, B);
    return Field.NavigationEdges.ContainsByPredicate([&](const auto& Edge)
    {
      return Edge.MinNodeKey == Min && Edge.MaxNodeKey == Max;
    });
  };

  {
    FCrowdDemoSharedFlowFieldConfig Config = MakeConnectivityConfig(
      FVector(200.0f, 200.0f, 0.0f));
    Config.AgentInflateCm = 52.0f;
    Config.ConnectivityContractVersion = 2;
    Config.GoalLocation = FVector(125.0f, 75.0f, 60.0f);
    FCrowdDemoSharedFlowField Field;
    TestTrue(TEXT("V2 builds when every 100cm cell center is boundary-invalid"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));
    TestEqual(TEXT("V2 has no valid center anchors"),
      Field.NavigationCenterAnchorCount, 0);
    TestTrue(TEXT("V2 preserves shared-edge connection points"),
      Field.NavigationConnectionPointCount > 0);
    TestTrue(TEXT("V2 reports center-invalid connected cells"),
      Field.CenterInvalidButConnectedCellCount > 0);
    const FCrowdDemoSharedFlowSample Sample =
      FCrowdDemoSharedFlowFieldKernel::Sample(Field, FVector(75.0f, 75.0f, 60.0f));
    TestEqual(TEXT("source attaches through a connection node"),
      Sample.Status, ECrowdDemoFlowLocationStatus::Reachable);
    TestTrue(TEXT("source receives a stable navigation node"), Sample.NavigationNodeKey != 0);
    TestTrue(TEXT("source guidance remains hard safe"), Sample.FlowDirection.IsNearlyZero()
      || FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
        Config, FVector(75.0f, 75.0f, 60.0f),
        FVector(75.0f, 75.0f, 60.0f) + Sample.FlowDirection
          * FMath::Min(800.0f / 30.0f, Sample.GuidanceDistanceCm)));
    TestTrue(TEXT("goal has at least one stable attachment"), Field.GoalAttachmentCount > 0);
  }

  FCrowdDemoSharedFlowField Corridor200;
  const auto Corridor200Config = MakeCorridor(52.0f, 200.0f);
  TestTrue(TEXT("200cm corridor V2 builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(Corridor200Config, Corridor200));
  const auto CorridorSample = FCrowdDemoSharedFlowFieldKernel::Sample(
    Corridor200, FVector(200.0f, 60.0f, 60.0f));
  TestEqual(TEXT("200cm corridor is reachable"), CorridorSample.Status,
    ECrowdDemoFlowLocationStatus::Reachable);
  TestTrue(TEXT("200cm corridor desired segment is hard safe"),
    FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
      Corridor200Config, FVector(200.0f, 60.0f, 60.0f),
      FVector(200.0f, 60.0f, 60.0f) + CorridorSample.FlowDirection
        * FMath::Min(800.0f / 30.0f, CorridorSample.GuidanceDistanceCm)));

  {
    const auto NarrowConfig = MakeCorridor(52.0f, 100.0f);
    FCrowdDemoSharedFlowField Narrow;
    const bool bBuilt = FCrowdDemoSharedFlowFieldKernel::Build(NarrowConfig, Narrow);
    const auto NarrowSample = bBuilt
      ? FCrowdDemoSharedFlowFieldKernel::Sample(Narrow, FVector(200.0f, 60.0f, 60.0f))
      : FCrowdDemoSharedFlowSample();
    TestTrue(TEXT("100cm corridor cannot carry a 104cm hard diameter"),
      !bBuilt || NarrowSample.Status != ECrowdDemoFlowLocationStatus::Reachable);
  }

  {
    auto Config = MakeConnectivityConfig(FVector(200.0f, 200.0f, 0.0f));
    Config.AgentInflateCm = 10.0f;
    Config.ConnectivityContractVersion = 2;
    Config.GoalLocation = FVector(150.0f, 150.0f, 60.0f);
    AddObstacle(Config, 1, FVector(100.0f, 25.0f, 60.0f),
      FVector(1.0f, 15.0f, 100.0f));
    FCrowdDemoSharedFlowField Offset;
    TestTrue(TEXT("offset interval field builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Offset));
    const auto* Connection = Offset.NavigationNodes.FindByPredicate([](const auto& Node)
    {
      return Node.Kind == ECrowdDemoNavigationNodeKind::VerticalEdgeConnection
        && Node.PrimaryCellKey == 0 && Node.SecondaryCellKey == 1;
    });
    TestTrue(TEXT("offset interval creates a connection"), Connection != nullptr);
    if (Connection)
      TestTrue(TEXT("connection uses safe-interval midpoint instead of full-edge midpoint"),
        Connection->QuantizedLocationCm.Y > 60);
  }

  {
    auto Config = MakeConnectivityConfig(FVector(200.0f, 200.0f, 0.0f));
    Config.ConnectivityContractVersion = 2;
    Config.AgentInflateCm = 0.0f;
    Config.GoalLocation = FVector(150.0f, 150.0f, 60.0f);
    AddObstacle(Config, 2, FVector(100.0f, 25.0f, 60.0f), FVector(1.0f, 5.0f, 100.0f));
    AddObstacle(Config, 1, FVector(100.0f, 75.0f, 60.0f), FVector(1.0f, 5.0f, 100.0f));
    FCrowdDemoSharedFlowField A, B;
    TestTrue(TEXT("multiple interval field builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, A));
    const int32 MultipleCount = A.NavigationSafeIntervals.FilterByPredicate([](const auto& Interval)
    {
      return Interval.Kind == ECrowdDemoNavigationNodeKind::VerticalEdgeConnection
        && Interval.PrimaryCellKey == 0 && Interval.SecondaryCellKey == 1;
    }).Num();
    TestTrue(TEXT("all safe intervals on one shared edge are retained"), MultipleCount >= 3);
    Algo::Reverse(Config.ObstacleSpecs);
    TestTrue(TEXT("reversed multiple interval field builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, B));
    TestEqual(TEXT("multiple interval order does not change V2 hash"), B.BuildHash, A.BuildHash);
    TestEqual(TEXT("multiple interval order does not change node count"),
      B.NavigationNodes.Num(), A.NavigationNodes.Num());
    TestEqual(TEXT("multiple interval order does not change edge count"),
      B.NavigationEdges.Num(), A.NavigationEdges.Num());
  }

  {
    auto Config = MakeConnectivityConfig(FVector(300.0f, 100.0f, 0.0f));
    Config.BoundsMin = FVector(-100.0f, 0.0f, 0.0f);
    Config.BoundsMax = FVector(200.0f, 100.0f, 0.0f);
    Config.ConnectivityContractVersion = 2;
    Config.AgentInflateCm = 0.0f;
    Config.GoalLocation = FVector(150.0f, 50.0f, 60.0f);
    AddObstacle(Config, 9, FVector(50.0f, 50.0f, 60.0f),
      FVector(1.0f, 50.0f, 100.0f));
    FCrowdDemoSharedFlowField Split;
    TestTrue(TEXT("interior split field builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Split));
    const auto* Left = Split.NavigationNodes.FindByPredicate([](const auto& Node)
    {
      return Node.Kind == ECrowdDemoNavigationNodeKind::VerticalEdgeConnection
        && Node.PrimaryCellKey == 0 && Node.SecondaryCellKey == 1;
    });
    const auto* Right = Split.NavigationNodes.FindByPredicate([](const auto& Node)
    {
      return Node.Kind == ECrowdDemoNavigationNodeKind::VerticalEdgeConnection
        && Node.PrimaryCellKey == 1 && Node.SecondaryCellKey == 2;
    });
    TestTrue(TEXT("split cell keeps both boundary connections"), Left && Right);
    if (Left && Right)
      TestFalse(TEXT("obstacle-split cell does not create a false internal edge"),
        HasEdge(Split, Left->StableNodeKey, Right->StableNodeKey));
  }

  {
    const auto Config48 = MakeCorridor(48.0f, 100.0f);
    const auto Config52 = MakeCorridor(52.0f, 100.0f);
    FCrowdDemoSharedFlowField Field48, Field52;
    const bool b48 = FCrowdDemoSharedFlowFieldKernel::Build(Config48, Field48);
    const bool b52 = FCrowdDemoSharedFlowFieldKernel::Build(Config52, Field52);
    const bool bReach48 = b48 && FCrowdDemoSharedFlowFieldKernel::Sample(
      Field48, FVector(200.0f, 60.0f, 60.0f)).Status
        == ECrowdDemoFlowLocationStatus::Reachable;
    const bool bReach52 = b52 && FCrowdDemoSharedFlowFieldKernel::Sample(
      Field52, FVector(200.0f, 60.0f, 60.0f)).Status
        == ECrowdDemoFlowLocationStatus::Reachable;
    TestTrue(TEXT("48cm and 52cm clearances deterministically change 100cm corridor"),
      bReach48 && !bReach52);
  }

  {
    FCrowdDemoSharedFlowField A, B;
    TestTrue(TEXT("first 200cm corridor rebuild"),
      FCrowdDemoSharedFlowFieldKernel::Build(Corridor200Config, A));
    auto Reordered = Corridor200Config;
    Algo::Reverse(Reordered.ObstacleSpecs);
    TestTrue(TEXT("reordered 200cm corridor rebuild"),
      FCrowdDemoSharedFlowFieldKernel::Build(Reordered, B));
    TestEqual(TEXT("V2 repeated/reordered build hash"), B.BuildHash, A.BuildHash);
    TestEqual(TEXT("V2 repeated/reordered next-node count"),
      B.NavigationNextNodeIndex.Num(), A.NavigationNextNodeIndex.Num());
    for (int32 Index = 0; Index < A.NavigationNextNodeIndex.Num(); ++Index)
      TestEqual(FString::Printf(TEXT("V2 next-node %d stable"), Index),
        B.NavigationNextNodeIndex[Index], A.NavigationNextNodeIndex[Index]);
  }

  {
    FCrowdDemoSharedFlowField Sf1;
    TestTrue(TEXT("V2 suite SF1 build"), FCrowdDemoSharedFlowFieldKernel::Build(
      FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1), Sf1));
    TestEqual(TEXT("V2 suite preserves SF1 golden"), Sf1.BuildHash, 267519150u);
    auto V1Config = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
    V1Config.AgentInflateCm = 52.0f;
    V1Config.ConnectivityContractVersion = 1;
    FCrowdDemoSharedFlowField V1;
    TestTrue(TEXT("V1 regression field builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(V1Config, V1));
    TestEqual(TEXT("V1 regression hash remains 8378 hash"), V1.BuildHash, 104583288u);
    TestEqual(TEXT("V1 regression blocked count"), V1.BlockedCellCount, 872);
    TestEqual(TEXT("V1 regression directed edge count"), V1.ValidDirectedEdgeCount, 14724);
  }

  {
    auto Agent0Config = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
    Agent0Config.AgentInflateCm = 52.0f;
    Agent0Config.ConnectivityContractVersion = 2;
    FCrowdDemoSharedFlowField Agent0Field;
    TestTrue(TEXT("Agent0 V2 field builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Agent0Config, Agent0Field));
    FVector Position(2449.0f, -956.0f, 60.0f);
    const float InitialGoalDistance = FVector::Dist2D(
      Position, FVector(Agent0Config.GoalLocation));
    for (int32 Step = 0; Step < 60; ++Step)
    {
      const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(Agent0Field, Position);
      TestEqual(FString::Printf(TEXT("Agent0 V2 step %d reachable"), Step),
        Sample.Status, ECrowdDemoFlowLocationStatus::Reachable);
      if (Sample.Status != ECrowdDemoFlowLocationStatus::Reachable) break;
      const FVector Next = Position + Sample.FlowDirection
        * FMath::Min(800.0f / 30.0f, Sample.GuidanceDistanceCm);
      TestTrue(FString::Printf(TEXT("Agent0 V2 step %d desired safe"), Step),
        FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
          Agent0Config, Position, Next));
      Position = Next;
    }
    TestTrue(TEXT("Agent0 V2 makes macroscopic goal progress"),
      FVector::Dist2D(Position, FVector(Agent0Config.GoalLocation))
        <= InitialGoalDistance - 500.0f);
  }
  return true;
}

#endif
