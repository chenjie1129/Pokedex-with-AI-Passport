import sys
import unittest
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from observe_pokedex_soak import Evidence
SHA='a'*40

class SoakEvidenceTests(unittest.TestCase):
    def complete(self):
        e=Evidence()
        for t in range(0,7201,60):
            if t%1200==0 and t<7200:
                e.feed(t,f'BUILD_ID version=x commit={SHA} source=x dirty=0')
                e.feed(t,'READY display=1 buttons=1 capture_count=20 place_data=1')
            e.feed(t,'STATE scanning attempts=3')
            e.feed(t,'PLACE_RESULT heap_before=110000 heap_after=100000 heap_min=90000 error=ESP_OK')
            e.feed(t,'STATE bestiary_list attempts=0')
            e.feed(t,'STATE bestiary_detail attempts=0')
            e.feed(t,'CRY_START species=4 samples=4800')
            e.feed(t,'CRY_END species=4 complete=1 failed=0')
            e.feed(t,'CAPTURE_COMMITTED result=applied')
        return e
    def test_reset_request_waits_for_home_idle_and_consumes_once(self):
        with tempfile.TemporaryDirectory() as directory:
            request=Path(directory)/'reset.request';request.touch();e=Evidence()
            e.feed(0,'STATE catching attempts=1')
            self.assertFalse(e.take_reset_request(10,request));self.assertTrue(request.exists())
            e.feed(11,'STATE home attempts=0')
            self.assertFalse(e.take_reset_request(15,request))
            self.assertTrue(e.take_reset_request(16,request))
            self.assertFalse(e.take_reset_request(17,request))
            self.assertEqual(e.reset_requests,[16])

    def test_full_evidence_still_requires_operator_review(self):
        result=self.complete().summary(7200,SHA)
        self.assertEqual(result['status'],'READY_FOR_MANUAL_REVIEW')
        self.assertTrue(result['manual_requirements'])
        self.assertFalse(result['physical_power_cut_tested'])
    def test_short_and_disconnected_runs_never_pass(self):
        self.assertIn('less_than_two_hours',self.complete().summary(7199,SHA)['issues'])
        self.assertIn('serial_observation_gap_over_75_seconds',self.complete().summary(7300,SHA)['issues'])
    def test_audio_start_and_duplicate_rewards_do_not_count_as_success(self):
        e=Evidence();e.feed(1,'CRY_START species=4');e.feed(2,'CAPTURE_COMMITTED result=duplicate')
        self.assertEqual(e.counts['audio_completions'],0)
        self.assertEqual(e.counts['confirmed_saves'],0)
    def test_fault_wrong_build_and_memory_drift_block_review(self):
        e=self.complete();e.feed(7200,'abort() was called')
        self.assertIn('runtime_faults_detected',e.summary(7200,SHA)['issues'])
        self.assertIn('clean_expected_build_not_verified',e.summary(7200,'b'*40)['issues'])
        e=self.complete();e.feed(7200,'PLACE_RESULT heap_after=70000 heap_min=30000')
        self.assertIn('minimum_heap_missing_or_below_32768',e.summary(7200,SHA)['issues'])
        self.assertIn('warm_heap_drift_missing_or_over_8192',e.summary(7200,SHA)['issues'])

if __name__=='__main__':unittest.main()
