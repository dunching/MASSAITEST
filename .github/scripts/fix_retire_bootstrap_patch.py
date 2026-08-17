from pathlib import Path

path = Path(".github/scripts/apply_retire_bootstrap_second_pass.py")
text = path.read_text(encoding="utf-8")
start = text.find("old_dispatch = '''  if (bBuildingBoundaryGraph)")
end_marker = 'facing = replace_once(facing, old_dispatch, new_dispatch, "facing dispatch collapse")\n'
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise RuntimeError("old facing dispatch patch block missing")
end += len(end_marker)
new = r'''new_dispatch = ''' + "'''" + r'''  const bool bDispatched = bUsesParticle
    ? Pipeline->DispatchBoundarySoftPressureWorkGraph(
        MoveTemp(CombinedInput),
        MoveTemp(PreviousSettleStepsByAgentId),
        MoveTemp(TerminalOwnerByAgentId))
    : Pipeline->DispatchBoundaryFacingWork(
        MoveTemp(CombinedInput),
        MoveTemp(ConsecutiveSettleStepsByAgentId),
        MoveTemp(FinalSettledByAgentId));
  if (!bDispatched)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryWorkGraphDispatchRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
''' + "'''" + r'''
dispatch_token = "if (bBuildingBoundaryGraph)"
dispatch_pos = facing.find(dispatch_token)
if dispatch_pos < 0:
    raise RuntimeError("facing legacy dispatch branch missing")
dispatch_start = facing.rfind("\n", 0, dispatch_pos) + 1
facing = facing[:dispatch_start] + new_dispatch + "}\n"
'''
text = text[:start] + new + text[end:]
needle = 'c = read(CPP)\nfor prefix in [\n'
if needle not in text:
    raise RuntimeError("pipeline cleanup insertion marker missing")
text = text.replace(
    needle,
    'c = read(CPP)\n'
    'c = remove_block(c, "  uint64 CalculatePreparedTargetResourceHash(")\n'
    'for prefix in [\n',
    1)
gate = '    "FCrowdDemoPreparedTargetResourcePlan",\n'
if gate not in text:
    raise RuntimeError("retired gate insertion marker missing")
text = text.replace(
    gate,
    gate + '    "CalculatePreparedTargetResourceHash",\n',
    1)
path.write_text(text, encoding="utf-8")
print("retirement patch fixer applied")
