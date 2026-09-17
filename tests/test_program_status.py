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
        status = copy.deepcopy(self.status)
        status['stages'][3]['status'] = 'planned'
        with self.assertRaises(ValueError):
            validate_status(status)


if __name__ == '__main__':
    unittest.main()
