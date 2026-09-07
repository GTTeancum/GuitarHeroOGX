"""EE-specific decode overrides; never label a VU load as a MIPS64 branch.

ISA encodings/operand fields checked against PCSX2 at
eb3fc0dbea8b2abecdd99dc3e4f60b3c7cbbfd6c, pcsx2/R5900OpcodeTables.cpp
and pcsx2/DebugTools/DisR5900asm.cpp. This is a deliberately partial decoder:
unimplemented EE instructions remain explicit words, not invented scalar ops.
"""

GPR = ("zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 "
       "s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra").split()


def signed16(value):
    value &= 0xffff
    return value - 0x10000 if value & 0x8000 else value


def ee_override(word, address):
    """Return mnemonic/operands for an EE override, None for scalar Capstone."""
    op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
    fs, fd, fn = (word >> 11) & 31, (word >> 6) & 31, word & 63

    def unknown(kind):
        return ".word", f"0x{word:08x} ; unimplemented/invalid EE {kind}"

    # R5900 MULT/MULTU optionally also write LO to rd. Capstone may label
    # this as a MIPS DSP accumulator ($acN), which is not an EE register.
    if op == 0 and fn in (0x18, 0x19):
        registers = [GPR[rs], GPR[rt]]
        if fs:
            registers.insert(0, GPR[fs])
        return "mult" if fn == 0x18 else "multu", ", ".join("$" + r for r in registers)

    # EE SQRT.S uses Ft, not Fs (unlike scalar MIPS decoders).
    # PCSX2 DebugTools/DisR5900asm.cpp SQRT_S at the pinned commit above.
    if op == 0x11 and rs == 16 and fn == 4:
        return "sqrt.s", f"$f{fd}, $f{rt}"

    if op in (0x1e, 0x1f, 0x36, 0x3e):
        name = {0x1e: "lq", 0x1f: "sq", 0x36: "lqc2", 0x3e: "sqc2"}[op]
        reg = f"$vf{rt}" if op in (0x36, 0x3e) else "$" + GPR[rt]
        return name, f"{reg}, {signed16(word):#x}(${GPR[rs]})"
    if op == 0x1c:
        # GH1 RndCam::UpdateWorld's packed transpose at1B20EC..1B2104.
        # Encodings and rd,rs,rt checked against local PCSX2 d073d750,
        # R5900OpcodeTables.cpp MMI0/1/2/3 and DisR5900asm.cpp.
        packed = {(0x08, 0x12): "pextlw", (0x28, 0x12): "pextuw",
                  (0x09, 0x0e): "pcpyld", (0x29, 0x0e): "pcpyud"}
        if (fn, fd) in packed:
            return packed[fn, fd], f"${GPR[fs]}, ${GPR[rs]}, ${GPR[rt]}"
        return unknown("MMI")  # Capstone otherwise selects unrelated DSP/MSA ops.
    if op in (0x13, 0x1d, 0x30, 0x32, 0x34, 0x35, 0x38, 0x3a, 0x3b, 0x3c, 0x3d):
        return unknown("primary opcode")
    if op != 0x12:
        return None
    if rs in (1, 5):
        name = ("qmfc2" if rs == 1 else "qmtc2") + (".i" if word & 1 else "")
        return name, f"${GPR[rt]}, $vf{fs}"
    if rs == 2 and fs == 22:
        return "cfc2" + (".i" if word & 1 else ""), f"${GPR[rt]}, Q"
    if rs == 8 and rt < 4:
        return ("bc2f", "bc2t", "bc2fl", "bc2tl")[rt], hex(address + 4 + signed16(word) * 4)
    if rs < 16:
        return unknown("COP2 transfer")

    mask = "".join(c for bit, c in ((8, "x"), (4, "y"), (2, "z"), (1, "w")) if rs & bit)

    def vector(name, destination, operand=None):
        args = f"{destination}, $vf{fs}"
        if operand is not None:
            args += ", " + operand
        return f"{name}.{mask}", args

    if fn < 28:
        name = ("vadd", "vsub", "vmadd", "vmsub", "vmax", "vmini", "vmul")[fn // 4]
        lane = "xyzw"[fn % 4]
        return vector(name + lane, f"$vf{fd}", f"$vf{rt}{lane}")
    scalar = {28: ("vmulq", "Q"), 29: ("vmaxi", "I"), 30: ("vmuli", "I"), 31: ("vminii", "I"),
              32: ("vaddq", "Q"), 33: ("vmaddq", "Q"), 34: ("vaddi", "I"), 35: ("vmaddi", "I"),
              36: ("vsubq", "Q"), 37: ("vmsubq", "Q"), 38: ("vsubi", "I"), 39: ("vmsubi", "I")}
    if fn in scalar:
        name, operand = scalar[fn]
        return vector(name, f"$vf{fd}", operand)
    if 40 <= fn <= 47:
        name = ("vadd", "vmadd", "vmul", "vmax", "vsub", "vmsub", "vopmsub", "vmini")[fn - 40]
        return vector(name, f"$vf{fd}", f"$vf{rt}")
    if fn < 60:
        return unknown("VU special1")

    sub = (word & 3) | ((word >> 4) & 0x7c)
    if sub < 16 or 24 <= sub <= 27:
        name = ("vadda", "vsuba", "vmadda", "vmsuba")[sub // 4] if sub < 16 else "vmula"
        lane = "xyzw"[sub % 4]
        return vector(name + lane, "ACC", f"$vf{rt}{lane}")
    if 16 <= sub <= 23:
        name = ("vitof" if sub < 20 else "vftoi") + ("0", "4", "12", "15")[sub % 4]
        return vector(name, f"$vf{rt}")
    if sub == 29:
        return vector("vabs", f"$vf{rt}")
    accum_scalar = {28: ("vmulaq", "Q"), 30: ("vmulai", "I"),
                    32: ("vaddaq", "Q"), 33: ("vmaddaq", "Q"), 34: ("vaddai", "I"), 35: ("vmaddai", "I"),
                    36: ("vsubaq", "Q"), 37: ("vmsubaq", "Q"), 38: ("vsubai", "I"), 39: ("vmsubai", "I")}
    if sub in accum_scalar:
        name, operand = accum_scalar[sub]
        return vector(name, "ACC", operand)
    if sub in (40, 41, 42, 44, 45, 46):
        name = {40: "vadda", 41: "vmadda", 42: "vmula", 44: "vsuba", 45: "vmsuba", 46: "vopmula"}[sub]
        return vector(name, "ACC", f"$vf{rt}")
    if sub == 47:
        return "vnop", ""
    if sub in (48, 49):
        return vector("vmove" if sub == 48 else "vmr32", f"$vf{rt}")
    if sub in (56, 57, 58):
        fs_lane, ft_lane = "xyzw"[(word >> 21) & 3], "xyzw"[(word >> 23) & 3]
        operand = f"$vf{rt}{ft_lane}"
        if sub != 57:
            operand = f"$vf{fs}{fs_lane}, " + operand
        return ("vdiv", "vsqrt", "vrsqrt")[sub - 56], "Q, " + operand
    if sub == 59:
        return "vwaitq", ""
    return unknown("VU special2")


def disassemble_ee(code, start, scalar_decoder):
    if start % 4 or len(code) % 4:
        raise ValueError("EE code range must contain aligned 4-byte words")
    for offset in range(0, len(code), 4):
        address = start + offset
        data = code[offset:offset + 4]
        word = int.from_bytes(data, "little")
        decoded = ee_override(word, address)
        if decoded is None:
            instruction = next(scalar_decoder.disasm(data, address, count=1), None)
            decoded = (instruction.mnemonic, instruction.op_str) if instruction else (
                ".word", f"0x{word:08x} ; scalar undecoded")
        mnemonic, operands = decoded
        yield f"0x{address:08x}: {mnemonic:<14} {operands} ; word=0x{word:08x}".rstrip()
