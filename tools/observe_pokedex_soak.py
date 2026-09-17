#!/usr/bin/env python3
"""Record a two-hour interactive Passport soak; never flash the device.

Serial logs stay in the specified private directory. Counters are evidence, not
physical acceptance. Finish with the operator checklist and before/after NVS audit.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import time

DURATION = 7200

class Evidence:
    def __init__(self):
        self.counts=collections.Counter()
        self.errors=[]
        self.heap=[]
        self.builds=set()
        self.reset_causes=[]
        self.last_time=0.0
        self.max_log_gap=0.0
        self.boot=0
        self.boot_time=0.0
        self.states=[]
        self.last_state_time=0.0
        self.reset_requests=[]

    def feed(self,elapsed,line):
        self.max_log_gap=max(self.max_log_gap,elapsed-self.last_time)
        self.last_time=elapsed
        if any(x in line.lower() for x in ('guru meditation','panic','abort() was called',
                                          'watchdog','bestiary load failed','nvs init failed')):
            self.errors.append({'seconds':round(elapsed,1),'kind':'runtime_fault'})
        match=re.search(r'BUILD_ID .*commit=([a-f0-9]{40}) .*dirty=(\d)',line)
        if match:
            self.builds.add((match[1],int(match[2])))
            self.boot+=1;self.boot_time=elapsed
        if 'rst:' in line:
            match=re.search(r'rst:(0x[0-9a-fA-F]+)\s*\(([^)]+)\)',line)
            if match:self.reset_causes.append({'seconds':round(elapsed,1),'code':match[1],'reason':match[2]})
        if 'READY display=1 buttons=1' in line:self.counts['ready_boots']+=1
        match=re.search(r'\bSTATE (\w+) ',line)
        if match:
            state=match[1]
            previous=self.states[-1] if self.states else None
            self.states.append(state)
            self.last_state_time=elapsed
            if state=='scanning' and previous!='scanning':self.counts['scans_started']+=1
            if state in ('bestiary_list','bestiary_detail') and state!=previous:self.counts['list_detail_transitions']+=1
            if state in ('storage_error','place_storage_error'):self.counts['storage_errors']+=1
        if 'PLACE_RESULT ' in line:self.counts['scan_results']+=1
        if 'CRY_START ' in line:self.counts['audio_starts']+=1
        if 'CRY_END ' in line and 'complete=1 failed=0' in line:self.counts['audio_completions']+=1
        if 'CRY_END ' in line and 'failed=1' in line:self.errors.append({'seconds':round(elapsed,1),'kind':'audio_failure'})
        if ('CAPTURE_COMMITTED ' in line or 'DISCOVERY_COMMITTED ' in line) and 'result=applied' in line:
            self.counts['confirmed_saves']+=1
        if 'SETTINGS_SAVED ' in line:self.counts['confirmed_saves']+=1
        match=re.search(r'heap_after=(\d+) heap_min=(\d+)',line)
        if match:self.heap.append((elapsed,self.boot,elapsed-self.boot_time,int(match[1]),int(match[2])))

    def take_reset_request(self,elapsed,path):
        if not (path.exists() and self.states and self.states[-1]=='home' and
                elapsed-self.last_state_time>=5):
            return False
        path.unlink()
        self.reset_requests.append(round(elapsed,3))
        return True

    def summary(self,elapsed,expected_commit):
        issues=[]
        if elapsed<DURATION:issues.append('less_than_two_hours')
        thresholds={'scans_started':20,'scan_results':20,'list_detail_transitions':50,
                    'audio_completions':20,'confirmed_saves':10,'ready_boots':6}
        for key,minimum in thresholds.items():
            if self.counts[key]<minimum:issues.append(f'{key}_below_{minimum}')
        if self.errors:issues.append('runtime_faults_detected')
        if self.counts['storage_errors']:issues.append('unexpected_storage_errors')
        if self.builds!={(expected_commit,0)}:issues.append('clean_expected_build_not_verified')
        gap=max(self.max_log_gap,elapsed-self.last_time)
        if gap>75:issues.append('serial_observation_gap_over_75_seconds')
        heap_min=min((r[4] for r in self.heap),default=None)
        if heap_min is None or heap_min<32768:issues.append('minimum_heap_missing_or_below_32768')
        warm=[r for r in self.heap if r[2]>=600]
        drift=[]
        for boot in sorted({r[1] for r in warm}):
            values=[r[3] for r in warm if r[1]==boot]
            if len(values)>=2:drift.append(max(values)-min(values))
        if not drift or max(drift)>8192:issues.append('warm_heap_drift_missing_or_over_8192')
        if not self.heap or self.heap[-1][0]<elapsed-600:issues.append('final_heap_sample_missing')
        return dict(status='INCOMPLETE' if issues else 'READY_FOR_MANUAL_REVIEW',
                    elapsed_seconds=round(elapsed,1),counts=dict(self.counts),
                    issues=issues,minimum_heap=heap_min,warm_heap_drift=max(drift,default=None),
                    maximum_serial_gap=round(gap,1),observed_builds=sorted(self.builds),
                    reset_causes=self.reset_causes,requested_resets=self.reset_requests,errors=self.errors,
                    manual_requirements=['All five resets were intentional; no spontaneous reboot',
                        'Buttons and pages remain responsive; audio sounds correct',
                        'Before/after save audit matches the recorded user operations'],
                    location_scope='single_location_only',physical_power_cut_tested=False)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True)
    p.add_argument('--output-dir',required=True,type=Path)
    p.add_argument('--expected-commit',required=True)
    p.add_argument('--reset-on-start',action='store_true',
                   help='Hard reset once after opening the log to capture the initial boot')
    p.add_argument('--enable-reset-requests',action='store_true',
                   help='Allow operator-created reset.request file to request a hard reset while Home is idle')
    args=p.parse_args()
    if not re.fullmatch(r'[a-f0-9]{40}',args.expected_commit):p.error('expected-commit must be a full SHA')
    import serial
    # Fail before creating an evidence directory if the device is disconnected.
    port=serial.Serial(baudrate=115200,timeout=0.2)
    port.dtr=False;port.rts=False;port.port=args.port;port.open()
    args.output_dir.mkdir(mode=0o700,parents=True,exist_ok=False)
    started=time.monotonic();evidence=Evidence();pending=b'';next_update=60
    raw=args.output_dir/'serial.log';events=args.output_dir/'events.jsonl'
    interrupted=False
    try:
        with port, raw.open('xb') as output, events.open('x') as eventlog:
            os.chmod(raw,0o600);os.chmod(events,0o600)
            print('Recording. Start before boot, or restart once to capture build identity. Follow the operator checklist.',flush=True)
            if args.reset_on_start:
                from esptool.reset import HardReset
                print('Initial operator-requested boot reset',flush=True)
                HardReset(port,uses_usb=True)()
            while time.monotonic()-started<DURATION:
                elapsed=time.monotonic()-started
                request=args.output_dir/'reset.request'
                if args.enable_reset_requests and evidence.take_reset_request(elapsed,request):
                    from esptool.reset import HardReset
                    print('Operator-requested hard reset at '+str(round(elapsed,1))+' seconds',flush=True)
                    HardReset(port,uses_usb=True)()
                data=port.read(8192)
                if data:
                    output.write(data);output.flush();pending+=data
                    while b'\n' in pending:
                        line,pending=pending.split(b'\n',1)
                        text=line.decode('utf-8',errors='replace').rstrip('\r')
                        evidence.feed(elapsed,text)
                        eventlog.write(json.dumps({'seconds':round(elapsed,3),'line':text})+'\n');eventlog.flush()
                if elapsed>=next_update:
                    print(json.dumps({'elapsed_minutes':int(elapsed//60),'counts':dict(evidence.counts)}),flush=True)
                    next_update+=60
    except (KeyboardInterrupt,serial.SerialException) as error:
        interrupted=True
        print('Observation interrupted: '+type(error).__name__,flush=True)
    finally:
        elapsed=time.monotonic()-started
        result=evidence.summary(elapsed,args.expected_commit)
        if interrupted:result['status']='INCOMPLETE';result['issues'].append('recording_interrupted')
        (args.output_dir/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2),flush=True)
    return 0 if result['status']=='READY_FOR_MANUAL_REVIEW' else 2

if __name__=='__main__':raise SystemExit(main())
