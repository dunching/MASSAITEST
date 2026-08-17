from pathlib import Path

HEADER = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h")
CPP = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp")
PROC_H = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.h")
PROC = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp")


def read(path): return path.read_text(encoding="utf-8-sig")
def write(path, text): path.write_text(text, encoding="utf-8")


def function_span(text, signature):
    pos = text.find(signature)
    if pos < 0: raise RuntimeError(f"missing function {signature}")
    brace = text.find("{", pos)
    if brace < 0: raise RuntimeError(f"missing brace {signature}")
    depth = 0; i = brace; string = char = esc = line = block = False
    while i < len(text):
        c = text[i]; n = text[i+1] if i+1 < len(text) else ""
        if line:
            if c == "\n": line = False
            i += 1; continue
        if block:
            if c == "*" and n == "/": block = False; i += 2; continue
            i += 1; continue
        if string:
            if esc: esc = False
            elif c == "\\": esc = True
            elif c == '"': string = False
            i += 1; continue
        if char:
            if esc: esc = False
            elif c == "\\": esc = True
            elif c == "'": char = False
            i += 1; continue
        if c == "/" and n == "/": line = True; i += 2; continue
        if c == "/" and n == "*": block = True; i += 2; continue
        if c == '"': string = True; i += 1; continue
        if c == "'": char = True; i += 1; continue
        if c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                while end < len(text) and text[end] in " \t\r": end += 1
                if end < len(text) and text[end] == "\n": end += 1
                return pos, end
        i += 1
    raise RuntimeError(f"unbalanced {signature}")


def remove_function(text, signature):
    s, e = function_span(text, signature)
    return text[:s] + text[e:]

h = read(HEADER)
for snippet in [
    "  bool ValidateRoundApplyPlan(\n    TConstArrayView<FCrowdMassCommitTarget> ResolvedTargets);\n",
    "  bool MarkRoundApplyCommitted(double CommitMilliseconds);\n",
    "  const FCrowdBoundaryOrchestratorResult& GetLastBoundaryTransactionResult()\n    const\n  { return LastBoundaryTransactionResult; }\n",
]:
    if snippet not in h:
        raise RuntimeError(f"header residual declaration missing: {snippet[:40]}")
    h = h.replace(snippet, "", 1)
write(HEADER, h)

ph = read(PROC_H)
for snippet in [
    "  bool ValidatePreparedCommit(\n    UCrowdDemoRoundSimPipelineSubsystem& Pipeline);\n",
    "  void CommitValidatedSideEffects(\n    UCrowdDemoRoundSimPipelineSubsystem& Pipeline);\n",
]:
    if snippet not in ph:
        raise RuntimeError(f"facing residual declaration missing: {snippet[:40]}")
    ph = ph.replace(snippet, "", 1)
write(PROC_H, ph)

p = read(PROC)
p = remove_function(
    p,
    "bool FCrowdDemoRoundFacingFinalizeStage::ValidatePreparedCommit(")
p = remove_function(
    p,
    "void FCrowdDemoRoundFacingFinalizeStage::CommitValidatedSideEffects(")
write(PROC, p)

c = read(CPP)
c = remove_function(
    c,
    "bool UCrowdDemoRoundSimPipelineSubsystem::ValidateRoundApplyPlan(")
c = remove_function(
    c,
    "bool UCrowdDemoRoundSimPipelineSubsystem::MarkRoundApplyCommitted(")
# Remove the final orphan scheduler fail block from FailFixedStep.
marker = "void UCrowdDemoRoundSimPipelineSubsystem::FailFixedStep()\n"
ms = c.find(marker)
if ms < 0: raise RuntimeError("FailFixedStep missing")
mb = c.find("{", ms)
me = function_span(c, marker)[1]
body = c[mb:me]
old = "  if (BoundaryOrchestrator.IsValid())\n  {\n    BoundaryOrchestrator->Fail();\n    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();\n  }\n"
if old not in body:
    raise RuntimeError("orphan FailFixedStep scheduler block missing")
body = body.replace(old, "", 1)
c = c[:mb] + body + c[me:]
write(CPP, c)

production = read(HEADER) + read(CPP) + read(PROC_H) + read(PROC)
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
        matches = [line for line in production.splitlines() if retired in line][:10]
        raise RuntimeError(f"residual {retired}: {matches}")
print("transaction residual cleanup passed")
