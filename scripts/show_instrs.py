import json, sys
data = json.load(sys.stdin)
instrs = data.get("instructions", [])
for i, instr in enumerate(instrs):
    t = instr.get("target", {})
    b = instr.get("base", {})
    mt = instr.get("match_type", "")
    t_op = t.get("opcode", "") if t else ""
    t_args = t.get("args", "") if t else "(none)"
    b_op = b.get("opcode", "") if b else ""
    b_args = b.get("args", "") if b else "(none)"
    mark = " <<<<" if mt != "equal" else ""
    print(f"{i:3d}  T: {t_op:8s} {t_args:40s} | B: {b_op:8s} {b_args:40s} {mt}{mark}")
