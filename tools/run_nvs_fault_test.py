#!/usr/bin/env python3
"""Observe a preinstalled diagnostic. Never flash, erase, or touch player saves."""
import argparse
import re
import time
from pathlib import Path

CASES = frozenset(('before_set', 'after_set_before_commit', 'commit_error',
                   'readback_error', 'corrupt_current', 'repeated_real_nvs_writes',
                   'real_nvs_full', 'restart_after_commit'))

def verify(log):
    if 'FAULT_FAIL' in log or 'Guru Meditation' in log or ('panic' in log.lower() or 'abort() was called' in log):
        raise ValueError('Device diagnostic failed; inspect the local log')
    start = log.find('FAULT_START')
    if start < 0:
        raise ValueError('No diagnostic start marker')
    log = log[start:]
    seen = set(re.findall(r'FAULT_CASE_PASS case=([a-z_]+)', log))
    if seen != CASES or 'FAULT_RESTART point=after_commit_before_publication' not in log:
        raise ValueError('Incomplete run: missing cases or controlled restart')
    if 'FAULT_SUITE_PASS cases=8 scratch=erased power_cut=not_tested' not in log:
        raise ValueError('No completed suite and scratch cleanup marker')
    return sorted(seen)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--log',type=Path, required=True)
    p.add_argument('--timeout',type=float,default=180)
    p.add_argument('--reset',action='store_true',help='Reset once before observing; use only with diagnostic installed')
    args=p.parse_args()
    import serial
    from esptool.reset import HardReset
    args.log.parent.mkdir(parents=True,exist_ok=True)
    with serial.Serial(args.port,115200,timeout=0.2) as port, args.log.open('xb') as output:
        port.setDTR(False)
        port.reset_input_buffer()
        if args.reset: HardReset(port,uses_usb=True)()
        collected=bytearray();end=time.monotonic()+args.timeout
        while time.monotonic()<end:
            data=port.read(8192)
            if not data:continue
            output.write(data);output.flush();collected.extend(data)
            log=collected.decode('utf-8',errors='replace')
            if 'FAULT_FAIL' in log or 'Guru Meditation' in log or ('panic' in log.lower() or 'abort() was called' in log):
                raise SystemExit('FAIL: inspect '+str(args.log))
            if 'FAULT_SUITE_PASS' in log:
                cases=verify(log)
                print('PASS: '+', '.join(cases))
                print('Scratch erased. Physical power loss was not tested.')
                return
        raise SystemExit('INCOMPLETE: timeout; inspect '+str(args.log))

if __name__=='__main__':main()
