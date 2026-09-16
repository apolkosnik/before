import unittest
from decode_exception import MAGIC, decode


def capture(words):
    return "\n".join(f"0x{word:016x}" for word in words)


class DecodeTests(unittest.TestCase):
    def delivery(self, reason=1, match=1):
        header = (3 << 48) | (3 << 46) | (reason << 44) | (match << 32) | (6 << 16)
        payload = [0, 0x10E65FF400000000, 0x068F560003FFFFBC,
                   0x10E65F20040573BC, 0, 0, 0, header]
        if match == 1:
            payload[0] = 0x0002784C0002784E
            payload[1] |= 0x4E718000
            payload[7] |= 0x209
        if reason == 3:
            payload[1] = 0
        return payload

    def test_delivery_match(self):
        result = decode(capture([MAGIC, 1] + self.delivery() + [MAGIC, 1]))
        self.assertEqual(result["kernel_pc"], "0x040573bc")
        self.assertEqual(result["frame_address"], "0x10e65ff4")
        self.assertEqual(result["kernel_d2_low16"], "0x0006")
        self.assertEqual(result["exception"]["stacked_pc"], "0x0002784e")
        self.assertEqual(result["exception"]["saved_sr"], "0x8000")

    def test_breakpoint_match(self):
        payload = self.delivery(reason=2)
        payload[7] = (payload[7] & ~0xfff) | 47
        result = decode(capture([MAGIC, 1] + payload + [MAGIC, 1]))
        self.assertEqual(result["status"], "kernel breakpoint delivery")
        self.assertEqual(result["exception"]["vector"], 47)

    def test_unmatched_context(self):
        for match in (2, 3, 4):
            payload = self.delivery(reason=3 if match == 4 else 1, match=match)
            result = decode(capture([MAGIC, 1] + payload + [MAGIC, 1]))
            self.assertIsNone(result["exception"])

    def test_delivery_reserved_bits(self):
        for word, bit in ((4, 0), (7, 36), (7, 12)):
            payload = self.delivery()
            payload[word] |= 1 << bit
            with self.assertRaisesRegex(ValueError, "reserved"):
                decode(capture([MAGIC, 1] + payload + [MAGIC, 1]))

    def test_unmatched_cannot_claim_pc(self):
        payload = self.delivery(match=3)
        payload[0] = 0x1234
        with self.assertRaisesRegex(ValueError, "must not claim"):
            decode(capture([MAGIC, 1] + payload + [MAGIC, 1]))

    def test_delivery_class_and_reason(self):
        for change in (1 << 46, 1 << 44):
            payload = self.delivery()
            payload[7] ^= change
            with self.assertRaisesRegex(ValueError, "header"):
                decode(capture([MAGIC, 1] + payload + [MAGIC, 1]))

    def test_empty(self):
        result = decode(capture([MAGIC, 0] + [0] * 8 + [MAGIC, 0]))
        self.assertIn("armed", result["status"])

    def test_packet(self):
        payload = [
            0x0000030000000302, 0x000003004E718000,
            0x040A900003FFFFF0, 0,
            0, 0, 0, 0x0002000180000209,
        ]
        result = decode(capture([MAGIC, 1] + payload + [MAGIC, 1]))
        self.assertEqual(result["vector"], 9)
        self.assertEqual(result["frame_format"], 2)
        self.assertEqual(result["stacked_pc"], "0x00000302")
        self.assertEqual(result["saved_sr"], "0x8000")
        self.assertEqual(result["urp"], "0x040a9000")
        self.assertEqual(result["last_rte"], {
            "valid": True, "target_pc": "0x00000300",
            "restored_sr": "0x8000",
        })

    def test_incomplete(self):
        with self.assertRaises(ValueError):
            decode(capture([MAGIC, 1]))

    def test_changed_header(self):
        with self.assertRaisesRegex(ValueError, "header changed"):
            decode(capture([MAGIC, 0] + [0] * 8 + [MAGIC, 1]))

    def test_bad_magic(self):
        with self.assertRaisesRegex(ValueError, "not initialized"):
            decode(capture([0] * 12))

    def test_bad_version(self):
        with self.assertRaisesRegex(ValueError, "version"):
            decode(capture([MAGIC, 1] + [0] * 8 + [MAGIC, 1]))


if __name__ == "__main__":
    unittest.main()
