#!/usr/bin/env python3
"""PSK protocol regression tests with simulated USB responses only."""
import io
from pathlib import Path
import runpy
import struct
import sys
import types
import unittest
from unittest.mock import Mock, patch

usb = types.ModuleType('usb')
usb.core = types.ModuleType('usb.core')
usb.util = types.ModuleType('usb.util')
usb.core.USBError = type('USBError', (Exception,), {})
usb.core.find = Mock()
usb.util.claim_interface = Mock()
usb.util.dispose_resources = Mock()
with patch.dict(sys.modules, {'usb': usb, 'usb.core': usb.core, 'usb.util': usb.util}):
    api = runpy.run_path(str(Path(__file__).with_name('goodix-psk-autofix')))


def response(command, payload):
    return api['encode_pack'](api['encode_proto'](payload, command))


def ack(command):
    return response(api['CMD_ACK'], bytes([command, 1]))


def hash_reply(value=None):
    return response(api['CMD_PSK_READ'], b'\0' + struct.pack('<II', api['PSK_READ_FLAGS'], 32)
                    + (api['PMK_HASH'] if value is None else value))


class Device:
    def __init__(self, responses):
        self.responses = list(responses)
        self.writes = []

    def write(self, endpoint, data, timeout):
        self.writes.append(data)
        return len(data)

    def read(self, endpoint, size, timeout):
        if not self.responses:
            raise usb.core.USBError('No response')
        return self.responses.pop(0)

    def is_kernel_driver_active(self, interface):
        return False

    def set_configuration(self):
        pass


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.sleep = patch('time.sleep').start()
        self.addCleanup(patch.stopall)
        usb.util.dispose_resources.reset_mock()

    def test_correct_hash_and_read_wire_format(self):
        device = Device([ack(api['CMD_PSK_READ']), hash_reply()])
        self.assertTrue(api['check_psk'](api['Sensor'](device)))
        self.assertEqual(device.writes[0][7:19], struct.pack('<III', api['PSK_READ_FLAGS'], 32, 0))

    def test_mismatched_hash(self):
        device = Device([ack(api['CMD_PSK_READ']), hash_reply(b'\0' * 32)])
        self.assertFalse(api['check_psk'](api['Sensor'](device)))

    def test_fragmented_and_coalesced_responses(self):
        header = ack(api['CMD_PSK_READ'])
        device = Device([header[:2], header[2:] + hash_reply()])
        self.assertTrue(api['check_psk'](api['Sensor'](device)))

    def test_padded_responses(self):
        header = ack(api['CMD_PSK_READ'])
        device = Device([header + b'\0' * (64 - len(header)) + hash_reply()])
        self.assertTrue(api['check_psk'](api['Sensor'](device)))

    def test_write_is_verified_and_uses_32_bit_length(self):
        device = Device([ack(api['CMD_PSK_WRITE']), response(api['CMD_PSK_WRITE'], b'\0'),
                         ack(api['CMD_PSK_READ']), hash_reply()])
        self.assertTrue(api['write_psk'](api['Sensor'](device)))
        payload = struct.pack('<II', api['PSK_WRITE_FLAGS'], len(api['PSK_WHITE_BOX'])) + api['PSK_WHITE_BOX']
        self.assertEqual(device.writes[0][7:7 + len(payload)], payload)

    def test_ack_does_not_hide_write_failure(self):
        device = Device([ack(api['CMD_PSK_WRITE']), response(api['CMD_PSK_WRITE'], b'\1')] * 3)
        with patch('sys.stderr', new=io.StringIO()):
            self.assertFalse(api['write_psk'](api['Sensor'](device)))
        self.assertEqual(len(device.writes), 3)

    def test_read_back_mismatch_is_failure(self):
        device = Device([ack(api['CMD_PSK_WRITE']), response(api['CMD_PSK_WRITE'], b'\0'),
                         ack(api['CMD_PSK_READ']), hash_reply(b'\0' * 32)] * 3)
        with patch('sys.stderr', new=io.StringIO()):
            self.assertFalse(api['write_psk'](api['Sensor'](device)))

    def test_main_failure_is_nonzero_and_releases_device(self):
        device = Device([ack(api['CMD_PSK_READ']), response(api['CMD_PSK_READ'], b'\0')])
        usb.core.find.return_value = device
        with patch('sys.stderr', new=io.StringIO()):
            self.assertEqual(api['main'](), 1)
        usb.util.dispose_resources.assert_called_once_with(device)
        matcher = usb.core.find.call_args.kwargs['custom_match']
        self.assertTrue(matcher(types.SimpleNamespace(idProduct=0x55b4)))
        self.assertFalse(matcher(types.SimpleNamespace(idProduct=0x1234)))
        self.assertEqual(len(device.writes), 2)  # NOP and read; never write an unknown key.


if __name__ == '__main__':
    unittest.main()
