#!/usr/bin/env python3
"""Record wrist USB output; no phone app required. Requires pyserial."""
import argparse
import csv
from datetime import datetime, timezone
from pathlib import Path
import queue
import re
import threading


def parse_data(line):
    fields = line.split(',')
    if len(fields) != 8 or fields[0] != 'DATA':
        return None
    try:
        timestamp = int(fields[1])
        values = [float(value) for value in fields[2:]]
        import math
        if timestamp < 0 or not all(math.isfinite(v) for v in values):
            return None
        return [timestamp, *values]
    except ValueError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true', help='List available COM/serial ports')
    parser.add_argument('--port', help='WRIST serial port, for example COM7')
    parser.add_argument('--out', type=Path, help='New recording folder (must not already exist)')
    args = parser.parse_args()
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        parser.exit(1, 'Install dependency first: python -m pip install pyserial\n')
    if args.list:
        for port in list_ports.comports():
            print(f'{port.device}: {port.description}')
        return
    if not args.port:
        parser.error('--port is required unless using --list')
    output = args.out or Path('walk_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
    if output.exists():
        parser.error(f'Output already exists: {output}; choose a new folder to protect previous data.')
    try:
        link = serial.Serial(args.port, 115200, timeout=0.25)
    except serial.SerialException as exc:
        parser.exit(1, f'Cannot open WRIST port: {exc}\nClose Arduino Serial Monitor/Plotter and retry.\n')
    output.mkdir(parents=True)
    print(f'Recording to {output.resolve()}\nType a command + Enter (h for help); Ctrl+C to stop.')
    commands = queue.Queue()

    def keyboard():
        while True:
            try:
                value = input().strip()
            except (EOFError, OSError):
                return
            if value:
                commands.put(value)

    threading.Thread(target=keyboard, daemon=True).start()
    boot = ''
    raw_count = event_count = 0
    try:
        with link, (output / 'serial_log.txt').open('w', encoding='utf-8', buffering=1) as log, \
                (output / 'imu.csv').open('w', newline='', encoding='utf-8') as imu_file, \
                (output / 'events.csv').open('w', newline='', encoding='utf-8') as event_file:
            imu_writer, event_writer = csv.writer(imu_file), csv.writer(event_file)
            imu_writer.writerow(['pc_utc', 'foot_boot', 'foot_ms', 'ax_g', 'ay_g', 'az_g', 'gx_dps', 'gy_dps', 'gz_dps'])
            event_writer.writerow(['pc_utc', 'foot_boot', 'record_type', 'message'])
            link.write(b'h\ns\n')
            pending = bytearray()
            while True:
                while not commands.empty():
                    command = commands.get_nowait()
                    link.write((command + '\n').encode('ascii', errors='ignore'))
                    log.write(f'[PC_COMMAND] {command}\n')
                block = link.read(max(1, min(link.in_waiting, 4096)))
                pending.extend(block)
                while b'\n' in pending:
                    row, _, remainder = pending.partition(b'\n')
                    pending = bytearray(remainder)
                    line = row.decode('utf-8', errors='replace').rstrip('\r')
                    stamp = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
                    log.write(f'{stamp} {line}\n')
                    session = re.search(r'# SESSION foot_boot=(\d+)', line)
                    if session:
                        boot = session.group(1)
                    data = parse_data(line)
                    if data is not None:
                        imu_writer.writerow([stamp, boot, *data])
                        raw_count += 1
                    else:
                        record_type = line.split()[1] if line.startswith('# ') and len(line.split()) > 1 else 'OTHER'
                        event_writer.writerow([stamp, boot, record_type, line])
                        event_count += 1
                        print(line)
                # Flush even during quiet periods so partial sessions remain usable.
                imu_file.flush()
                event_file.flush()
                if len(pending) > 16384:
                    log.write('[LOGGER_WARNING] Unterminated serial record discarded\n')
                    pending.clear()
    except KeyboardInterrupt:
        print('\nRecording stopped.')
    except serial.SerialException as exc:
        print(f'\nUSB connection lost: {exc}. Captured data is saved; start a new recording after reconnecting.')
    print(f'Saved {raw_count} IMU rows and {event_count} event/status rows to {output.resolve()}')


if __name__ == '__main__':
    main()
