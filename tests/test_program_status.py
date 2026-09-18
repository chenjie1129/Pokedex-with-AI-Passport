"""The owner exception is narrow and cannot erase unfinished evidence."""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_pokedex_program import load, STATUS_PATH, validate_status


class ProgramStatusTests(unittest.TestCase):
    def setUp(self):
        self.status = load(STATUS_PATH)

    def test_current_decision_is_valid(self):
        validate_status(self.status)

    def test_exception_requires_owner_unfinished_checks_and_specific_stage(self):
        for key, value in [('approved_by', 'unknown'), ('permits_stage', 4),
                           ('verification_status', 'passed'), ('unfinished_checks', [])]:
            with self.subTest(key=key):
                status = copy.deepcopy(self.status)
                status['stages'][1]['verification_deferral'][key] = value
                with self.assertRaises(ValueError):
                    validate_status(status)

    def test_cannot_report_deferred_work_complete_or_unlock_cloud(self):
        status = copy.deepcopy(self.status)
        status['stages'][1]['progress_percent'] = 100
        with self.assertRaises(ValueError):
            validate_status(status)

    def completed_stage3(self):
        status = copy.deepcopy(self.status)
        status['overall_status'] = 'waiting_for_product_gate'
        status['current_stage'] = 3
        stage = status['stages'][2]
        stage.update(status='completed', progress_percent=100,
                     gate_decision='automated_acceptance_passed_stage2_deferred')
        stage['evidence'] = ['docs/verification/pokedex-stage3-device-2026-09-18.md']
        return status

    def test_completed_engineering_does_not_unlock_product_gate(self):
        validate_status(self.completed_stage3())
        for key, value in [('status', 'in_progress'), ('progress_percent', 99),
                           ('gate_decision', 'passed'), ('evidence', [])]:
            with self.subTest(key=key):
                status = self.completed_stage3()
                status['stages'][2][key] = value
                with self.assertRaises(ValueError):
                    validate_status(status)
        status = self.completed_stage3()
        status['current_stage'] = 4
        status['stages'][3]['status'] = 'in_progress'
        with self.assertRaises(ValueError):
            validate_status(status)
        status = copy.deepcopy(self.status)
        status['stages'][3]['status'] = 'planned'
        with self.assertRaises(ValueError):
            validate_status(status)


if __name__ == '__main__':
    unittest.main()
