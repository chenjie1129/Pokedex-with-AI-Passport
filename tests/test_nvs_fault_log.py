import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from run_nvs_fault_test import CASES, verify

class LogTests(unittest.TestCase):
    def good(self):
        return ('FAULT_START\n'+''.join('FAULT_CASE_PASS case='+c+'\n' for c in sorted(CASES))+
                'FAULT_RESTART point=after_commit_before_publication\n'+
                'FAULT_SUITE_PASS cases=8 scratch=erased power_cut=not_tested\n')
    def test_complete(self):
        self.assertEqual(set(verify(self.good())),CASES)
    def test_missing_case_restart_cleanup_and_failure(self):
        for token in ['FAULT_CASE_PASS case=real_nvs_full','FAULT_RESTART', 'scratch=erased']:
            with self.assertRaises(ValueError):verify(self.good().replace(token,''))
        for fault in ['FAULT_FAIL reason=test','Guru Meditation Error','panic','abort() was called']:
            with self.assertRaises(ValueError):verify(self.good()+fault)
    def test_summary_alone_is_not_evidence(self):
        with self.assertRaises(ValueError):verify('FAULT_START\nFAULT_SUITE_PASS cases=8 scratch=erased power_cut=not_tested')

if __name__=='__main__':unittest.main()
