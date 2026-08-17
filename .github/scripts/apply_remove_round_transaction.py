from pathlib import Path
import re

HEADER = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h")
CPP = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp")
PROC = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp")


def read(path):
    return path.read_text(encoding="utf-8-sig")


def write(path, text):
    path.write_text(text, encoding="utf-8")


def replace_exact(text, old, new, expected, label):
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected}, found {count}")
    return text.replace(old, new)


def remove_between(text, start, end, label):
    si = text.find(start)
    if si < 0:
        raise RuntimeError(f"{label}: start missing")
    ei = text.find(end, si)
    if ei < 0:
        raise RuntimeError(f"{label}: end missing")
    return text[:si] + text[ei:]


def function_span(text, signature, occurrence=1):
    pos = -1
    for _ in range(occurrence):
        pos = text.find(signature, pos + 1)
        if pos < 0:
            raise RuntimeError(f"function signature missing: {signature}")
    brace = text.find("{", pos)
    if brace < 0:
        raise RuntimeError(f"function brace missing: {signature}")
    depth = 0
    i = brace
    in_string = False
    in_char = False
    escape = False
    line_comment = False
    block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if c == "\n": line_comment = False
            i += 1; continue
        if block_comment:
            if c == "*" and n == "/": block_comment = False; i += 2; continue
            i += 1; continue
        if in_string:
            if escape: escape = False
            elif c == "\\": escape = True
            elif c == '"': in_string = False
            i += 1; continue
        if in_char:
            if escape: escape = False
            elif c == "\\": escape = True
            elif c == "'": in_char = False
            i += 1; continue
        if c == "/" and n == "/": line_comment = True; i += 2; continue
        if c == "/" and n == "*": block_comment = True; i += 2; continue
        if c == '"': in_string = True; i += 1; continue
        if c == "'": in_char = True; i += 1; continue
        if c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                while end < len(text) and text[end] in " \t": end += 1
                if end < len(text) and text[end] == "\r": end += 1
                if end < len(text) and text[end] == "\n": end += 1
                return pos, end
        i += 1
    raise RuntimeError(f"unbalanced function: {signature}")


def remove_function(text, signature):
    s, e = function_span(text, signature)
    return text[:s] + text[e:]

# ---------------------------------------------------------------------------
# Header: delete the persistent Round transaction vocabulary and expose only
# one-shot Worker bootstrap preparation + Worker-owned commit state.
# ---------------------------------------------------------------------------
h = read(HEADER)
h = h.replace("class FCrowdDemoRoundWorkBatch;\n", "")
h = remove_between(
    h,
    "// Demo-local scheduling vocabulary retained only until the remaining Round\n",
    "struct FCrowdDemoRoundFacingTemplate\n{",
    "remove round scheduling vocabulary",
)
h = "struct FCrowdDemoRoundFacingTemplate\n{".join(h.split("struct FCrowdDemoRoundFacingTemplate\n{", 1)) if False else h
# remove_between keeps the end marker, as intended.
h = replace_exact(
    h,
    "  bool BeginBoundaryTransaction(double GatherMilliseconds);\n",
    "  bool BeginWorkerBootstrapPreparation(double GatherMilliseconds);\n",
    1,
    "rename boundary begin to bootstrap preparation",
)
h = replace_exact(
    h,
    "  // Returns false only when the specialized fast path was eligible but could\n"
    "  // not submit safely. Ineligible frames retain the prepared-boundary path.\n"
    "  bool TrySubmitWorkerV2ClockIntentEarly();\n",
    "",
    1,
    "remove legacy early clock API",
)
h = replace_exact(
    h,
    "  bool TrySubmitFullWorkerProductionIntent();\n",
    "  bool TrySubmitFullWorkerProductionIntent();\n"
    "  bool SubmitPreparedWorkerBootstrapInput();\n",
    1,
    "add bootstrap submit API",
)
h = re.sub(
    r"  ECrowdBoundaryPollResult TryPrepareRoundApply\(\);\n"
    r"  int32 GetLastBoundaryPrepareCheckpoint\(\) const\n"
    r"  \{ return LastBoundaryPrepareCheckpoint; \}\n"
    r"  ECrowdBoundaryTransactionState GetRoundWorkState\(\) const;\n",
    "",
    h,
    count=1,
)
if "TryPrepareRoundApply" in h or "GetRoundWorkState" in h:
    raise RuntimeError("legacy poll API remains in header")
h = h.replace("  int32 LastBoundaryPrepareCheckpoint = 0;\n", "")
h = h.replace("  TSharedPtr<FCrowdDemoRoundWorkBatch> BoundaryOrchestrator;\n", "")
h = h.replace("  FCrowdBoundaryOrchestratorResult LastBoundaryTransactionResult;\n", "")
write(HEADER, h)

# ---------------------------------------------------------------------------
# Pipeline: replace the persistent async Round batch with a stack-local,
# synchronous bootstrap graph. It exists only while building first control
# resources; it owns no transaction state, poll state, or commit envelope.
# ---------------------------------------------------------------------------
c = read(CPP)
class_start = "// Demo-local scheduler for one immutable Round input batch. It intentionally\n"
class_end = "bool FCrowdDemoRoundWorkGraph::BuildMovementInput(\n"
s = c.find(class_start)
e = c.find(class_end, s)
if s < 0 or e < 0:
    raise RuntimeError("round batch class range missing")
bootstrap_graph = r'''namespace
{
struct FCrowdBootstrapStageId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBootstrapStageId&) const = default;
  auto operator<=>(const FCrowdBootstrapStageId&) const = default;
};

struct FCrowdBootstrapTaskTypeId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBootstrapTaskTypeId&) const = default;
  auto operator<=>(const FCrowdBootstrapTaskTypeId&) const = default;
};

struct FCrowdBootstrapTaskKey
{
  FCrowdBootstrapStageId StageId;
  FCrowdBootstrapTaskTypeId TaskTypeId;
  uint64 ScopeKey = 0;
  bool operator==(const FCrowdBootstrapTaskKey&) const = default;
  bool operator<(const FCrowdBootstrapTaskKey& Other) const
  {
    if (StageId != Other.StageId) return StageId < Other.StageId;
    if (TaskTypeId != Other.TaskTypeId) return TaskTypeId < Other.TaskTypeId;
    return ScopeKey < Other.ScopeKey;
  }
  bool IsValid() const
  { return StageId.IsValid() && TaskTypeId.IsValid(); }
};

struct FCrowdBootstrapTaskResult
{
  uint64 StableHash = 14695981039346656037ull;
  bool bSucceeded = false;
  static FCrowdBootstrapTaskResult Success(uint64 Hash)
  {
    FCrowdBootstrapTaskResult Result;
    Result.StableHash = Hash;
    Result.bSucceeded = Hash != 0;
    return Result;
  }
  static FCrowdBootstrapTaskResult Failure() { return {}; }
};

using FCrowdBootstrapTaskBody =
  TUniqueFunction<FCrowdBootstrapTaskResult()>;

class FCrowdDemoBootstrapSynchronousGraph
{
public:
  bool AddTask(
    const FCrowdBootstrapTaskKey Key,
    const TConstArrayView<FCrowdBootstrapTaskKey> Prerequisites,
    FCrowdBootstrapTaskBody&& Body,
    const bool bRequireOffGameThread = true)
  {
    (void)bRequireOffGameThread;
    if (!IsInGameThread() || !Key.IsValid() || !Body
      || Nodes.ContainsByPredicate(
        [&Key](const FNode& Node) { return Node.Key == Key; }))
      return false;
    FNode& Node = Nodes.AddDefaulted_GetRef();
    Node.Key = Key;
    Node.Prerequisites = TArray<FCrowdBootstrapTaskKey>(Prerequisites);
    Node.Prerequisites.Sort();
    for (int32 Index = 0; Index < Node.Prerequisites.Num(); ++Index)
      if (!Node.Prerequisites[Index].IsValid()
        || Node.Prerequisites[Index] == Key
        || (Index > 0
          && !(Node.Prerequisites[Index - 1]
            < Node.Prerequisites[Index])))
        return false;
    Node.Body = MoveTemp(Body);
    return true;
  }

  bool Run()
  {
    if (!IsInGameThread() || Nodes.IsEmpty()) return false;
    TSet<int32> Completed;
    while (Completed.Num() < Nodes.Num())
    {
      int32 Selected = INDEX_NONE;
      for (int32 Index = 0; Index < Nodes.Num(); ++Index)
      {
        if (Completed.Contains(Index)) continue;
        bool bReady = true;
        for (const FCrowdBootstrapTaskKey& Prerequisite
          : Nodes[Index].Prerequisites)
        {
          const int32 PrerequisiteIndex = Nodes.IndexOfByPredicate(
            [&Prerequisite](const FNode& Node)
            { return Node.Key == Prerequisite; });
          if (PrerequisiteIndex == INDEX_NONE
            || !Completed.Contains(PrerequisiteIndex))
          {
            bReady = false;
            break;
          }
        }
        if (!bReady) continue;
        if (Selected == INDEX_NONE
          || Nodes[Index].Key < Nodes[Selected].Key)
          Selected = Index;
      }
      if (Selected == INDEX_NONE) return false;
      FCrowdBootstrapTaskResult Result = Nodes[Selected].Body();
      if (!Result.bSucceeded) return false;
      Completed.Add(Selected);
    }
    return true;
  }

private:
  struct FNode
  {
    FCrowdBootstrapTaskKey Key;
    TArray<FCrowdBootstrapTaskKey> Prerequisites;
    FCrowdBootstrapTaskBody Body;
  };
  TArray<FNode> Nodes;
};
}

'''
c = c[:s] + bootstrap_graph + c[e:]

# Persistent transaction begin/state are gone.
c = remove_function(
    c,
    "bool UCrowdDemoRoundSimPipelineSubsystem::BeginBoundaryTransaction(")
insert_at = c.find("float UCrowdDemoRoundSimPipelineSubsystem::\n  GetCurrentBoundaryWallMilliseconds() const")
if insert_at < 0:
    raise RuntimeError("boundary wall marker missing")
bootstrap_begin = r'''bool UCrowdDemoRoundSimPipelineSubsystem::
  BeginWorkerBootstrapPreparation(const double GatherMilliseconds)
{
  if (!IsInGameThread() || !IsBoundarySnapshotCurrent()
    || bCurrentStepFullWorkerProductionFastPath
    || BoundaryFacingWorkState.IsValid())
    return false;
  CurrentBoundaryRequestStartSeconds = FPlatformTime::Seconds()
    - FMath::Max(0.0, GatherMilliseconds) / 1000.0;
  return true;
}

'''
c = c[:insert_at] + bootstrap_begin + c[insert_at:]
c = remove_function(
    c,
    "ECrowdBoundaryTransactionState\nUCrowdDemoRoundSimPipelineSubsystem::GetRoundWorkState() const")

# Stage admission no longer depends on a cross-frame scheduler state.
replacements = [
("  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()\n"
 "    || BoundaryOrchestrator->GetState()\n"
 "      != ECrowdBoundaryTransactionState::Gathering\n"
 "    || !IsBoundarySnapshotCurrent())",
 "  if (!IsInGameThread() || !IsBoundarySnapshotCurrent())"),
("  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()\n"
 "    || BoundaryOrchestrator->GetState()\n"
 "      != ECrowdBoundaryTransactionState::Gathering\n"
 "    || Input.FixedStepIndex != GetCurrentFixedStepIndex()",
 "  if (!IsInGameThread()\n"
 "    || Input.FixedStepIndex != GetCurrentFixedStepIndex()"),
("    || BoundaryOrchestrator->GetState()\n"
 "      != ECrowdBoundaryTransactionState::Gathering)",
 ")"),
("  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()\n"
 "    || BoundaryOrchestrator->GetState()\n"
 "      != ECrowdBoundaryTransactionState::Gathering\n"
 "    || (BoundaryFacingWorkState.IsValid()",
 "  if (!IsInGameThread()\n"
 "    || (BoundaryFacingWorkState.IsValid()"),
("    || !BoundaryOrchestrator.IsValid()\n"
 "    || BoundaryOrchestrator->GetState()\n"
 "      != ECrowdBoundaryTransactionState::Merging)",
 ")"),
]
for old, new in replacements:
    c = c.replace(old, new)

# Both dispatchers now own a stack-local synchronous bootstrap graph.
for sig in [
    "bool UCrowdDemoRoundSimPipelineSubsystem::\n  DispatchBoundarySoftPressureWorkGraph(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::DispatchBoundaryFacingWork("]:
    pos = c.find(sig)
    if pos < 0: raise RuntimeError(f"dispatcher missing: {sig}")
    brace = c.find("{", pos)
    c = c[:brace+1] + "\n  FCrowdDemoBootstrapSynchronousGraph BootstrapGraph;" + c[brace+1:]

c = c.replace("FCrowdBoundaryTaskKey", "FCrowdBootstrapTaskKey")
c = c.replace("FCrowdBoundaryTaskResult", "FCrowdBootstrapTaskResult")
c = c.replace("BoundaryOrchestrator->AddTask", "BootstrapGraph.AddTask")
c = c.replace("BoundaryOrchestrator->Dispatch()", "BootstrapGraph.Run()")
# Remove now-invalid dispatcher entry state clauses.
c = c.replace(
    "  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()\n"
    "    || BoundaryOrchestrator->GetState()\n"
    "      != ECrowdBoundaryTransactionState::Gathering\n",
    "  if (!IsInGameThread()\n")
c = c.replace(
    "    BoundaryOrchestrator->Fail();\n"
    "    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();\n",
    "")

# Legacy cross-frame prepare/poll/parity path is physically removed.
c = remove_function(
    c,
    "ECrowdBoundaryPollResult\nUCrowdDemoRoundSimPipelineSubsystem::TryPrepareRoundApply()")
# The old early clock path is superseded by the direct Production path.
c = remove_function(
    c,
    "bool UCrowdDemoRoundSimPipelineSubsystem::\n  TrySubmitWorkerV2ClockIntentEarly()")

# Promote the one-shot prepared bootstrap input into the same Worker-owned
# commit state used by ordinary Production frames.
marker = "bool UCrowdDemoRoundSimPipelineSubsystem::\n  CanUseFullWorkerProductionFastPath() const\n"
mi = c.find(marker)
if mi < 0: raise RuntimeError("full production marker missing")
bootstrap_submit = r'''bool UCrowdDemoRoundSimPipelineSubsystem::
  SubmitPreparedWorkerBootstrapInput()
{
  check(IsInGameThread());
  if (!IsFullWorkerProductionMode()
    || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || bCurrentStepFullWorkerProductionFastPath
    || CurrentStepFullWorkerInputSequence != 0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerBootstrapRequiresFullProduction step=%d prepared=%d"),
      GetCurrentFixedStepIndex(),
      BoundaryFacingWorkState.IsValid()
        && BoundaryFacingWorkState->bCompleted ? 1 : 0);
    return false;
  }
  if (!SubmitWorkerV2BoundaryInput())
    return false;
  const uint64 AcceptedInputSequence =
    BoundaryFacingWorkState->WorkerV2InputSequence;
  if (AcceptedInputSequence == 0)
    return false;
  CurrentStepFullWorkerInputSequence = AcceptedInputSequence;
  bCurrentStepFullWorkerProductionFastPath = true;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoWorkerBootstrapCutover step=%d input_sequence=%llu source=WorkerInputSync"),
    GetCurrentFixedStepIndex(), AcceptedInputSequence);
  return true;
}

'''
c = c[:mi] + bootstrap_submit + c[mi:]

# Remove persistent scheduler reset/failure bookkeeping everywhere.
c = c.replace("  BoundaryOrchestrator.Reset();\n", "")
c = re.sub(
    r"  if \(BoundaryOrchestrator\.IsValid\(\)\)\n  \{\n"
    r"    BoundaryOrchestrator->Fail\(\);\n"
    r"    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult\(\);\n"
    r"  \}\n",
    "",
    c,
)
c = c.replace(
    "  const bool bDiscardedInFlight =\n"
    "    bStepInProgress && BoundaryOrchestrator.IsValid();",
    "  const bool bDiscardedInFlight = bStepInProgress;",
)
write(CPP, c)

# ---------------------------------------------------------------------------
# Processor server loop: every submitted server step commits through the same
# Worker Owner Barrier. Bootstrap still runs compatibility preparation once,
# synchronously, only to construct the first Worker control resources.
# ---------------------------------------------------------------------------
p = read(PROC)
advance_start = p.find("bool AdvanceRoundWorkerFrame(\n")
advance_end = p.find("\n}\n\n}\n\nUCrowdDemoWorkerInputSyncProcessor::", advance_start)
if advance_start < 0 or advance_end < 0:
    raise RuntimeError("AdvanceRoundWorkerFrame range missing")
block = p[advance_start:advance_end+2]
block = block.replace(
    "  FCrowdDemoRoundMovementWorkStage CommitMovementWork;\n"
    "  FCrowdDemoRoundParticleConstraintStage CommitParticleConstraint;\n"
    "  FCrowdDemoRoundFacingFinalizeStage CommitFacingFinalize;\n"
    "  CommitFacingFinalize.UseQuery(ResultCommitQuery);\n"
    "  const bool bFullProductionFastPath =\n"
    "    Pipeline->IsCurrentStepFullWorkerProductionFastPath();\n",
    "")
# Remove the entire legacy prepare/finalize branch at the top of Commit.
needle = "    if (!bFullProductionFastPath)\n    {"
ns = block.find(needle)
if ns < 0: raise RuntimeError("legacy commit branch missing")
brace = block.find("{", ns)
depth=0; i=brace
while i < len(block):
    if block[i] == "{": depth += 1
    elif block[i] == "}":
        depth -= 1
        if depth == 0:
            ne=i+1
            break
    i+=1
else: raise RuntimeError("legacy commit branch unbalanced")
replacement = r'''    if (!Pipeline->IsCurrentStepFullWorkerProductionFastPath())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoServerStepMissingWorkerAuthority step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      Pipeline->FailFixedStep();
      return ECrowdDemoRoundFrameStageResult::Failed;
    }'''
block = block[:ns] + replacement + block[ne:]
# Fast sequence validation is now unconditional.
block = block.replace("    if (bFullProductionFastPath)\n    {\n", "    {\n", 1)
block = block.replace(
    "            && (bFullProductionFastPath\n"
    "              || (Pipeline->FinalValidatePreparedTargetResourcePlan(\n"
    "                    *PendingWorkerResult->PreparedTargetResourcePlan)\n"
    "                && CommitFacingFinalize.ValidatePreparedCommit(\n"
    "                  *Pipeline)))\n",
    "")
# Side effects: keep only Worker-owned commit marker.
side_start = block.find("          if (bFullProductionFastPath)\n          {")
if side_start < 0: raise RuntimeError("legacy side-effect branch missing")
brace = block.find("{", side_start)
depth=0; i=brace
while i < len(block):
    if block[i] == "{": depth += 1
    elif block[i] == "}":
        depth -= 1
        if depth == 0:
            # include following else block
            else_pos = block.find("\n          else\n          {", i+1)
            if else_pos < 0: raise RuntimeError("legacy side-effect else missing")
            else_brace = block.find("{", else_pos)
            d2=0; j=else_brace
            while j < len(block):
                if block[j] == "{": d2 += 1
                elif block[j] == "}":
                    d2 -= 1
                    if d2 == 0:
                        side_end=j+1; break
                j+=1
            break
    i+=1
block = block[:side_start] + (
    "          checkf(Pipeline->MarkFullWorkerProductionResultCommitted(\n"
    "              (FPlatformTime::Seconds() - CommitStart) * 1000.0),\n"
    "            TEXT(\"Validated Full Worker Production commit unexpectedly failed\"));"
) + block[side_end:]
block = block.replace(
    "    const bool bApplied = BarrierResult\n"
    "        == ECrowdWorkerResultOwnerCommitResult::Committed\n"
    "      && (bFullProductionFastPath\n"
    "        ? Pipeline->IsCurrentStepWorkerDirtyMassApplied()\n"
    "        : Pipeline->IsMovementFinalizeAppliedCurrent());",
    "    const bool bApplied = BarrierResult\n"
    "        == ECrowdWorkerResultOwnerCommitResult::Committed\n"
    "      && Pipeline->IsCurrentStepWorkerDirtyMassApplied();")
# Server-only Advance no longer runs legacy Authority/PostFinalize adapters.
client_start = block.find("  if (bCommitted && World->GetNetMode() == NM_Client)\n")
if client_start >= 0:
    post_if = block.find("  if (bCommitted)\n  {", client_start)
    if post_if < 0: raise RuntimeError("post commit block missing")
    block = block[:client_start] + block[post_if:]
block = block.replace(
    "    if (!bFullProductionFastPath)\n"
    "    {\n"
    "      FCrowdDemoRoundPostFinalizeMetricsStage PostFinalizeMetrics;\n"
    "      PostFinalizeMetrics.Execute(EntityManager, Context);\n"
    "    }\n",
    "")
# Bootstrap path no longer opens/polls a Round transaction.
block = block.replace(
    "    if (!Pipeline->BeginBoundaryTransaction(SnapshotApplyMilliseconds))\n"
    "    {\n"
    "      UE_LOG(LogTemp, Error,\n"
    "        TEXT(\"VIOLATION CrowdDemoDirtyBoundaryBeginFailed step=%d\"),\n"
    "        Pipeline->GetCurrentFixedStepIndex());\n"
    "      Pipeline->FailFixedStep();\n"
    "      return false;\n"
    "    }",
    "    if (!Pipeline->BeginWorkerBootstrapPreparation(\n"
    "        SnapshotApplyMilliseconds))\n"
    "    {\n"
    "      UE_LOG(LogTemp, Error,\n"
    "        TEXT(\"VIOLATION CrowdDemoWorkerBootstrapBeginFailed step=%d\"),\n"
    "        Pipeline->GetCurrentFixedStepIndex());\n"
    "      Pipeline->FailFixedStep();\n"
    "      return false;\n"
    "    }")
# Old early intent call is gone.
block = re.sub(
    r"    if \(!Pipeline->TrySubmitWorkerV2ClockIntentEarly\(\)\)\n"
    r"    \{\n      Pipeline->FailFixedStep\(\);\n      return false;\n    \}\n",
    "",
    block,
    count=1,
)
# After synchronous Facing bootstrap graph, submit the prepared Worker input and
# immediately mark this fixed step as Worker-owned.
old_tail = '''    MeasureStage(
      ECrowdDemoRoundPerformanceStage::FacingFinalize, [&]
    {
      FacingFinalize.Execute(EntityManager, Context);
    });
    if (Pipeline->GetRoundWorkState()
      != ECrowdBoundaryTransactionState::Working)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryDispatchDidNotEnterWorking step=%d state=%d"),
        Pipeline->GetCurrentFixedStepIndex(),
        static_cast<int32>(
          Pipeline->GetRoundWorkState()));
      Pipeline->FailFixedStep();
      return false;
    }
'''
new_tail = '''    MeasureStage(
      ECrowdDemoRoundPerformanceStage::FacingFinalize, [&]
    {
      FacingFinalize.Execute(EntityManager, Context);
    });
    if (!Pipeline->SubmitPreparedWorkerBootstrapInput())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerBootstrapSubmitFailed step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      Pipeline->FailFixedStep();
      return false;
    }
'''
if old_tail not in block:
    raise RuntimeError("legacy submit tail missing")
block = block.replace(old_tail, new_tail)
p = p[:advance_start] + block + p[advance_end+2:]
write(PROC, p)

# Static gates for this slice.
h = read(HEADER); c = read(CPP); p = read(PROC)
production = h + c + p
for retired in [
    "FCrowdDemoRoundWorkBatch",
    "BeginBoundaryTransaction",
    "TryPrepareRoundApply",
    "BoundaryOrchestrator",
    "GetRoundWorkState",
    "ECrowdBoundaryTransactionState",
    "ECrowdBoundaryPollResult",
    "FCrowdBoundaryOrchestratorResult",
]:
    if retired in production:
        raise RuntimeError(f"retired transaction symbol remains: {retired}")
if "FCrowdWorkerResultOwnerCommitBarrier::Commit(" not in p:
    raise RuntimeError("owner barrier missing")
if "SubmitPreparedWorkerBootstrapInput()" not in p:
    raise RuntimeError("bootstrap cutover call missing")
if "FCrowdDemoBootstrapSynchronousGraph BootstrapGraph" not in c:
    raise RuntimeError("synchronous bootstrap graph missing")
if "UE::Tasks::Launch(\n          TEXT(\"CrowdDemoRoundWork\")" in c:
    raise RuntimeError("legacy Round UE task launch remains")
print("round transaction removal patch applied")
