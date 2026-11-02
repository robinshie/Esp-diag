from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LocalJournalTest(unittest.TestCase):
    def test_actual_worker_remains_readable_during_continuous_logging(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'local_journal')
            subprocess.run(['clang++', '-std=c++11', '-g', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', '-I' + str(ROOT / 'test/journal_stubs'),
                            str(ROOT / 'test/local_journal.cpp'), '-o', binary],
                           cwd=ROOT, check=True, capture_output=True, text=True)
            result = subprocess.run([binary], check=True, capture_output=True, text=True)
            self.assertIn('async read passed', result.stdout)
