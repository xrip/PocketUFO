"""ida_unreached_callees.py -- run INSIDE IDA (File > Script file, or ida-pro-mcp py_eval).

For every row docs/parity/teeth.tsv marks "cited but not reached", read the original's own callee chain
two hops deep out of the live IDB and say whether that chain lands on an address the port both owns and a
test reaches. It splits the deficit into:

  wrapper  -- the original is a thin variant whose real work happens in a shared routine the port has and
              a test reaches (MenuBtn_SetSpeed0 @ 0x18ecc -> Sound_Play + GameSpeed_Set @ 0x260c0).
  residue  -- nothing in the chain is reached, so no test is known to exercise this behaviour at all.

Callee extraction detail: idautils.CodeRefsFrom(fn.start_ea, ...) returns nothing useful -- a call sits on
an instruction inside the body, not on the entry. Walk idautils.FuncItems(fn.start_ea) and take
CodeRefsFrom(item, False) per item (False = follow only normal code refs, not data).

Output: docs/idb/unreached-callees.tsv, one row per deficit, plus the counts and the residue list.
"""
import ida_funcs
import idautils
import idc

TEETH = r"E:\pocketufo\docs\parity\teeth.tsv"
OUT = r"E:\pocketufo\docs\idb\unreached-callees.tsv"


def shape(ea):
    """The function's instruction sequence: mnemonic plus operands, as IDA renders them."""
    fn = ida_funcs.get_func(ea)
    if not fn:
        return None
    out = []
    for item in idautils.FuncItems(fn.start_ea):
        ops = []
        for i in range(3):
            o = idc.print_operand(item, i)
            if o is None:
                break
            ops.append(o)
        out.append((idc.print_insn_mnem(item), tuple(ops)))
    return tuple(out)


def family(name):
    """The handler family a name belongs to: the name without its trailing level number.

    MenuBtn_SetSpeed0..4, Options_SetMusicVol1..5, SaveLoad_Slot0..6, MenuCtl8..14_OnClick are the
    original's per-level button handlers. The port implements each family once, so the fold claim to
    check is: does a sibling of this family share this function's instruction sequence AND is that
    sibling reached by a test through the port's shared routine?
    """
    out = []
    for ch in name:
        if ch.isdigit():
            break
        out.append(ch)
    return "".join(out)


def callees_of(ea):
    fn = ida_funcs.get_func(ea)
    if not fn:
        return []
    out, seen = [], set()
    for item in idautils.FuncItems(fn.start_ea):
        for ref in idautils.CodeRefsFrom(item, False):
            cf = ida_funcs.get_func(ref)
            if cf and cf.start_ea != ea and cf.start_ea not in seen:
                seen.add(cf.start_ea)
                out.append((cf.start_ea, idc.get_name(cf.start_ea)))
    return out


rows = []
for line in open(TEETH, encoding="utf-8"):
    if line.startswith("#") or not line.strip():
        continue
    p = line.rstrip("\n").split("\t")
    if p[0] == "addr":
        continue
    if p[4] == "yes" and p[6] == "no":
        rows.append(p)
rows_owner = {p[0]: p[7] for p in rows}

# an address counts as reached-with-an-owner when the ledger says a test reaches it AND names the port
# function that owns it; that is the pair a wrapper's behaviour has to land on.
reached_owned = {}
for line in open(TEETH, encoding="utf-8"):
    if line.startswith("#") or not line.strip():
        continue
    p = line.rstrip("\n").split("\t")
    if p[0] == "addr":
        continue
    if p[6] == "yes" and p[7].strip():
        reached_owned[int(p[0], 16)] = p[7]

# Fingerprints of the reached rows, grouped by handler family, for the variant test.
fam_reached = {}
for line in open(TEETH, encoding="utf-8"):
    if line.startswith("#") or not line.strip():
        continue
    q = line.rstrip("\n").split("\t")
    if q[0] == "addr" or q[6] != "yes":
        continue
    ea = int(q[0], 16)
    fam_reached.setdefault(family(q[1]), []).append((ea, shape(ea), q[7], q[1]))

HDR = ["# addr\tname\tsubsystem\tcallees_1hop\tcallees_2hop\treaches_reached_port_owner\tvariant_of",
       "# Generated from the live IDB by docs/idb/ida_unreached_callees.py over the",
       "# cited-but-not-reached rows of docs/parity/teeth.tsv.",
       "# wrapper = the chain lands on an address the port owns and a test reaches; the original is then",
       "# a variant of a routine the port has. variant_of = a same-family sibling with the IDENTICAL",
       "# instruction sequence that a test reaches through its port owner: the per-level button handlers.",
       "# Neither claim pins the variant's own constants -- the residue column and the mutation pass do that."]
wrapper = variant = residue = 0
data = []
for p in rows:
    ea = int(p[0], 16)
    c1 = callees_of(ea)
    c2 = []
    for a, _n in c1[:8]:
        c2 += callees_of(a)
    hit = [(a, n, reached_owned[a]) for a, n in c1 + c2 if a in reached_owned]
    my_shape = shape(ea)
    fold = None
    for a, s, owner, nm in fam_reached.get(family(p[1]), []):
        if a != ea and s is not None and my_shape is not None and s == my_shape and owner.strip():
            fold = (a, nm, owner)
            break
    if hit:
        wrapper += 1
    elif fold:
        variant += 1
    else:
        residue += 1
    data.append("\t".join([
        p[0], p[1], p[3],
        "; ".join("%s:%s" % (hex(a), n) for a, n in c1[:6]),
        "; ".join("%s:%s" % (hex(a), n) for a, n in c2[:6]),
        "; ".join("%s:%s:%s" % (hex(a), n, o) for a, n, o in hit[:3]),
        "" if not fold else "%s:%s:%s" % (hex(fold[0]), fold[1], fold[2]),
    ]))
open(OUT, "w", encoding="utf-8").write("\n".join(HDR + data) + "\n")

print("deficits: %d | wrapper: %d | variant fold (identical sibling a test reaches): %d | residue: %d"
      % (len(rows), wrapper, variant, residue))
print("RESIDUE -- no reached routine in the chain and no identical reached sibling:")
for f in (d.split("\t") for d in data):
    if not f[5] and not f[6]:
        print("  %-8s %-38s %-10s owner: %-28s callees: %s"
              % (f[0], f[1], f[2], rows_owner.get(f[0], "(none)"), f[3] or "(leaf)"))
