#!/usr/bin/env python3
"""Host checks; NOT an ESP32 hardware build. Run with Python 3 and g++."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

root = Path(__file__).resolve().parent
mock = root / 'mocks'
# Wrapper headers are generated for the host include path, never for Arduino.
headers = ['Arduino.h', 'Wire.h', 'Adafruit_LSM6DS3TRC.h', 'BLEDevice.h', 'BLE2902.h',
           'esp_system.h', 'esp_gattc_api.h', 'Adafruit_DRV2605.h', 'freertos/FreeRTOS.h', 'freertos/queue.h', 'freertos/task.h']
with tempfile.TemporaryDirectory() as temporary:
    temp = Path(temporary)
    for name in headers:
        header = temp / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "MockApi.h"\n')
    for test in ['detector_protocol_test', 'foot_flow_test', 'wrist_flow_test']:
        exe = temp / test
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-DCONFIG_BLUEDROID_ENABLED', '-I' + str(temp), '-I' + str(mock),
                        str(root / (test + '.cpp')), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

spec = importlib.util.spec_from_file_location('logger', root.parent / 'pc_logger/record_walk.py')
logger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(logger)
assert logger.parse_data('DATA,123,1.000,0,-1,10,-20,0') == [123, 1.0, 0.0, -1.0, 10.0, -20.0, 0.0]
for invalid in ['# ALERT type=HARD_LANDING', 'DATA,123,1', 'DATA,x,1,2,3,4,5,6', 'DATA,123,nan,2,3,4,5,6']:
    assert logger.parse_data(invalid) is None
print('PASS PC logger data parsing and malformed records')

# Exercise actual logger file output with fragmented serial lines and a fake
# USB port. No dependency on a real device or installed pyserial is needed.
import builtins
import csv
import sys
import types
from unittest.mock import patch
class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.chunks = [b'# SESSION foot_boot=42 | TEST\nDATA,12',
                       b'3,1.000,0,-1,10,-20,0\n# ALERT type=HARD_',
                       b'LANDING foot_ms=123\n']
        self.written = []
    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0
    def write(self, value):
        self.written.append(value)
    def read(self, count):
        if not self.chunks:
            raise KeyboardInterrupt
        return self.chunks.pop(0)
    def __enter__(self):
        return self
    def __exit__(self, *args):
        return False
serial_mock = types.ModuleType('serial')
serial_mock.Serial = FakeSerial
serial_mock.SerialException = OSError
tools_mock = types.ModuleType('serial.tools')
tools_mock.list_ports = types.SimpleNamespace(comports=lambda: [])
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory) / 'capture'
    with patch.dict(sys.modules, {'serial': serial_mock, 'serial.tools': tools_mock}), \
            patch.object(sys, 'argv', ['record_walk.py', '--port', 'MOCK', '--out', str(output)]), \
            patch.object(builtins, 'input', side_effect=EOFError):
        logger.main()
    imu = list(csv.DictReader((output/'imu.csv').open()))
    events = list(csv.DictReader((output/'events.csv').open()))
    assert len(imu)==1 and imu[0]['foot_ms']=='123' and imu[0]['foot_boot']=='42'
    assert any(row['record_type']=='ALERT' and 'HARD_LANDING' in row['message'] for row in events)
    assert 'DATA,123,1.000' in (output/'serial_log.txt').read_text()
print('PASS PC logger fragmented serial input and IMU/event/full-log files')
