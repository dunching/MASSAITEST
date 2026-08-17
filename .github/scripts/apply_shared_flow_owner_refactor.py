from pathlib import Path

HEADER = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h")
CPP = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp")
WORKER_INPUT = Path("Source/MassAICrowdDemo/Mass/CrowdDemoWorkerInputSync.cpp")


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


header = HEADER.read_text(encoding="utf-8-sig")
cpp = CPP.read_text(encoding="utf-8-sig")
worker_input = WORKER_INPUT.read_text(encoding="utf-8-sig")

if '#include "MassCrowdWorkerNavigationResource.h"' not in cpp:
    cpp = replace_exact(
        cpp,
        '#include "MassCrowdWorkerMovementControlResource.h"\n'
        '#include "MassCrowdWorkerTargetDomain.h"',
        '#include "MassCrowdWorkerMovementControlResource.h"\n'
        '#include "MassCrowdWorkerNavigationResource.h"\n'
        '#include "MassCrowdWorkerTargetDomain.h"',
        1,
        "add explicit environment resource id include",
    )

cpp = replace_exact(
    cpp,
    "    FCrowdDemoPreparedRoundCommitPlan* Pending =\n"
    "      PeekPreparedRoundCommitPlan();",
    "  FCrowdDemoPreparedRoundCommitPlan* Pending =\n"
    "    PeekPreparedRoundCommitPlan();",
    1,
    "normalize target prepare indentation",
)

if "GetRuntimeSharedFlowField" in header:
    raise RuntimeError("legacy runtime flow getter remains in header")
if "FCrowdMassSharedFlowResource RuntimeSharedFlowResource;" in header:
    raise RuntimeError("legacy primary runtime flow member remains in header")
if "CrowdDemoRoundSimPipelineSubsystem.h" in worker_input:
    raise RuntimeError("WorkerInputSync still includes RoundSimPipeline")
if "UCrowdDemoRoundSimPipelineSubsystem" in worker_input:
    raise RuntimeError("WorkerInputSync still queries RoundSimPipeline")
if "RuntimeSubsystem.GetSharedFlowResource().Field" not in worker_input:
    raise RuntimeError("WorkerInputSync is not sourcing flow from RuntimeSubsystem")
if "reinterpret_cast<uint64>(\n    &RuntimeSharedFlowResource.Field)" in cpp:
    raise RuntimeError("pointer-address target resource id remains")
if "reinterpret_cast<uint64>(\n      &RuntimeSharedFlowResource.Field)" in cpp:
    raise RuntimeError("pointer-address target resource validation remains")
if cpp.count("CrowdWorkerResourceIds::Environment") < 2:
    raise RuntimeError("stable Environment resource id is not used for prepare and validation")

CPP.write_text(cpp, encoding="utf-8")
print("shared-flow owner refactor final structural checks passed")
