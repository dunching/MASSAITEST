from pathlib import Path

path = Path(".github/scripts/apply_retire_bootstrap_second_pass.py")
text = path.read_text(encoding="utf-8")

# Make Facing cleanup independent of historical formatting.
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

# Remove the obsolete Target/Resource prepared hash helper too.
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

# The legacy PostFinalizeMinimalQuery test is the terminal automation test in
# this file. Replace it through #endif instead of looking for a next test.
old = '''e = t.find("IMPLEMENT_SIMPLE_AUTOMATION_TEST(", s + len(marker))
if e < 0: raise RuntimeError("next test after old structure test missing")
'''
new_test_end = '''e = t.find("\\n#endif", s + len(marker))
if e < 0: raise RuntimeError("terminal #endif after old structure test missing")
'''
if old not in text:
    raise RuntimeError("old architecture test end finder missing")
text = text.replace(old, new_test_end, 1)

# The particle assertions are inside that same terminal legacy test and vanish
# with it; do not try to patch them a second time.
section_start = text.find("# Update the final particle architecture assertions")
section_end = text.find("write(TESTS, t)\n", section_start)
if section_start < 0 or section_end < 0:
    raise RuntimeError("legacy particle assertion patch section missing")
section_end += len("write(TESTS, t)\n")
text = text[:section_start] + "write(TESTS, t)\n" + text[section_end:]

path.write_text(text, encoding="utf-8")
print("retirement patch fixer applied")
