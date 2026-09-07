"""Instruction fixtures include the actual GH2 ServoBone::Poll inverse multiply."""
import unittest
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
from ps2_ee_disasm import ee_override, disassemble_ee


class EeDecoderTests(unittest.TestCase):
    def test_gh1_camera_packed_transpose(self):
        # Actual consecutive GH1 USA words at1B20EC..1B2104.
        fixtures = (
            (0x71286488, "pextlw", "$t4, $t1, $t0"),
            (0x71286ca8, "pextuw", "$t5, $t1, $t0"),
            (0x716a7488, "pextlw", "$t6, $t3, $t2"),
            (0x716a7ca8, "pextuw", "$t7, $t3, $t2"),
            (0x71cc4389, "pcpyld", "$t0, $t6, $t4"),
            (0x718e4ba9, "pcpyud", "$t1, $t4, $t6"),
            (0x71ed5389, "pcpyld", "$t2, $t7, $t5"))
        for word, mnemonic, operands in fixtures:
            self.assertEqual(ee_override(word, 0), (mnemonic, operands))

    def test_quadword_gpr(self):
        self.assertEqual(ee_override(0x7fb300e0, 0), ("sq", "$s3, 0xe0($sp)"))
        word = (0x1e << 26) | (29 << 21) | (19 << 16) | 0xffe0
        self.assertEqual(ee_override(word, 0), ("lq", "$s3, -0x20($sp)"))

    def test_vector_memory_is_not_branch(self):
        word = (0x36 << 26) | (17 << 21) | (4 << 16)
        self.assertEqual(ee_override(word, 0x180b48), ("lqc2", "$vf4, 0x0($s1)"))
        word = (0x3e << 26) | (2 << 21) | (8 << 16)
        self.assertEqual(ee_override(word, 0), ("sqc2", "$vf8, 0x0($v0)"))

    def test_actual_servo_inverse_translation(self):
        self.assertEqual(ee_override(0x4bc821bc, 0x180b58), ("vmulax.xyz", "ACC, $vf4, $vf8x"))
        self.assertEqual(ee_override(0x4bc828bd, 0x180b5c), ("vmadday.xyz", "ACC, $vf5, $vf8y"))
        self.assertEqual(ee_override(0x4bc8320a, 0x180b60), ("vmaddz.xyz", "$vf8, $vf6, $vf8z"))

    def test_cop2_real_branch_and_interlock(self):
        word = (0x12 << 26) | (8 << 21) | (1 << 16) | 0xfffe
        self.assertEqual(ee_override(word, 0x1000), ("bc2t", "0xffc"))
        word = (0x12 << 26) | (1 << 21) | (2 << 16) | (5 << 11) | 1
        self.assertEqual(ee_override(word, 0), ("qmfc2.i", "$v0, $vf5"))

    def test_unimplemented_stays_explicit(self):
        self.assertEqual(ee_override(0x70000000, 0)[0], ".word")
        self.assertEqual(ee_override(0x48000000, 0)[0], ".word")

    def test_posemeshes_normalization_and_ee_multiply(self):
        self.assertEqual(ee_override(0x460c0004, 0), ("sqrt.s", "$f0, $f12"))
        self.assertEqual(ee_override(0x4a6503be, 0), ("vrsqrt", "Q, $vf0w, $vf5x"))
        self.assertEqual(ee_override(0x4a0503bd, 0), ("vsqrt", "Q, $vf5x"))
        self.assertEqual(ee_override(0x4a0003bf, 0), ("vwaitq", ""))
        self.assertEqual(ee_override(0x4842b000, 0), ("cfc2", "$v0, Q"))
        self.assertEqual(ee_override(0x00621818, 0), ("mult", "$v1, $v1, $v0"))
        self.assertEqual(ee_override(0x00620019, 0), ("multu", "$v1, $v0"))

    def test_scalar_and_word_alignment(self):
        decoder = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
        words = (0x27bdfee0, 0x7fb300e0, 0x70000000, 0x03e00008)
        rows = list(disassemble_ee(b"".join(w.to_bytes(4, "little") for w in words), 0x180a48, decoder))
        self.assertIn("addiu", rows[0])
        self.assertIn("sq", rows[1])
        self.assertIn(".word", rows[2])
        self.assertTrue(rows[3].startswith("0x00180a54: jr"))
        with self.assertRaises(ValueError):
            list(disassemble_ee(b"\0\0\0", 0, decoder))


if __name__ == "__main__":
    unittest.main()
