from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ConsoleSessionTests(unittest.TestCase):
    def test_usb_console_session_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = str(Path(temporary) / 'console_session')
            result = subprocess.run([
                'clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                str(ROOT / 'test/console_session.cpp'), '-o', binary,
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([binary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('16 USB Console interface scenarios passed', result.stdout)

    def test_uart_and_tcp_port_adapters(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = str(Path(temporary) / 'console_ports')
            subprocess.run([
                'clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I' + str(ROOT / 'test/console_stubs'),
                str(ROOT / 'test/console_ports.cpp'), '-o', binary,
            ], check=True, capture_output=True, text=True)
            subprocess.run([binary], check=True, capture_output=True, text=True)

    def test_dual_channel_failover(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = str(Path(temporary) / 'failover_transport')
            subprocess.run([
                'clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                str(ROOT / 'test/failover_transport.cpp'), '-o', binary,
            ], check=True, capture_output=True, text=True)
            subprocess.run([binary], check=True, capture_output=True, text=True)
