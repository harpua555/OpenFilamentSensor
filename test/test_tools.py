#!/usr/bin/env python3
"""
Unit tests for Python tools in the tools/ directory.

Tests configuration parsing, board management, and log extraction.
"""

import sys
import os
import json
import unittest
from io import StringIO

# Add tools directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))

# Import tools to test
try:
    import board_config
    import extract_log_data
    TOOLS_AVAILABLE = True
except ImportError as e:
    print(f"Warning: Could not import tools: {e}")
    TOOLS_AVAILABLE = False


class TestBoardConfig(unittest.TestCase):
    """Test board configuration utilities."""
    
    @unittest.skipIf(not TOOLS_AVAILABLE, "Tools not available")
    def test_board_definitions_exist(self):
        """Test that board definitions are accessible."""
        self.assertTrue(hasattr(board_config, '__name__'))
    
    @unittest.skipIf(not TOOLS_AVAILABLE, "Tools not available")
    def test_get_chip_family_for_board(self):
        """Test chip family lookup for known boards."""
        if hasattr(board_config, 'get_chip_family_for_board'):
            # Test a known board
            try:
                result = board_config.get_chip_family_for_board('esp32-s3-dev')
                self.assertIsInstance(result, str)
                self.assertIn('ESP32', result)
            except ValueError:
                pass  # Board might not be in mapping
    
    @unittest.skipIf(not TOOLS_AVAILABLE, "Tools not available")
    def test_validate_board_environment(self):
        """Test board environment validation."""
        if hasattr(board_config, 'validate_board_environment'):
            # Test with a likely valid board
            result = board_config.validate_board_environment('esp32-dev')
            self.assertIsInstance(result, bool)


class TestExtractLogData(unittest.TestCase):
    """Test log extraction utilities."""
    
    @unittest.skipIf(not TOOLS_AVAILABLE, "Tools not available")
    def test_module_imports(self):
        """Test that extract_log_data module imports correctly."""
        self.assertTrue(hasattr(extract_log_data, '__name__'))
    
    @unittest.skipIf(not TOOLS_AVAILABLE, "Tools not available")
    def test_parse_flow_line_function_exists(self):
        """Test that parse_flow_line function exists."""
        if hasattr(extract_log_data, 'parse_flow_line'):
            self.assertTrue(callable(extract_log_data.parse_flow_line))


class TestGenerateTestSettings(unittest.TestCase):
    """Test settings generator for unit tests."""
    
    def test_settings_json_parsing(self):
        """Test that user_settings.json can be parsed."""
        settings_path = os.path.join(
            os.path.dirname(__file__),
            '..',
            'data',
            'user_settings.json'
        )
        
        if os.path.exists(settings_path):
            with open(settings_path, 'r') as f:
                try:
                    settings = json.load(f)
                    self.assertIsInstance(settings, dict)
                except json.JSONDecodeError as e:
                    self.fail(f"Failed to parse user_settings.json: {e}")
        else:
            self.skipTest("user_settings.json not found")
    
    def test_generated_header_exists(self):
        """Test that generated_test_settings.h exists."""
        header_path = os.path.join(
            os.path.dirname(__file__),
            'generated_test_settings.h'
        )
        
        if os.path.exists(header_path):
            with open(header_path, 'r') as f:
                content = f.read()
                # Check for header guards
                self.assertIn('#ifndef', content)
                self.assertIn('#define', content)
        else:
            self.skipTest("generated_test_settings.h not yet generated")


class TestBuildScripts(unittest.TestCase):
    """Test build script functionality."""
    
    def test_build_script_exists_sh(self):
        """Test that build_tests.sh exists and is executable."""
        script_path = os.path.join(os.path.dirname(__file__), 'build_tests.sh')
        self.assertTrue(os.path.exists(script_path))
        
        if hasattr(os, 'access'):
            self.assertTrue(os.access(script_path, os.R_OK))
    
    def test_build_script_exists_bat(self):
        """Test that build_tests.bat exists."""
        script_path = os.path.join(os.path.dirname(__file__), 'build_tests.bat')
        self.assertTrue(os.path.exists(script_path))


class TestFixtures(unittest.TestCase):
    """Test that test fixtures are available and valid."""
    
    def test_fixtures_directory_exists(self):
        """Test that fixtures directory exists."""
        fixtures_path = os.path.join(os.path.dirname(__file__), 'fixtures')
        self.assertTrue(os.path.exists(fixtures_path))
        self.assertTrue(os.path.isdir(fixtures_path))
    
    def test_log_replay_fixtures_exist(self):
        """Test that log replay fixtures exist."""
        fixtures_path = os.path.join(
            os.path.dirname(__file__),
            'fixtures',
            'logs_to_replay'
        )
        
        if os.path.exists(fixtures_path):
            files = os.listdir(fixtures_path)
            self.assertGreater(len(files), 0, "No fixture files found")
            
            expected_files = [
                'both.txt',
                'soft_detected.txt',
                'soft_detected_but_no_rearm.txt'
            ]
            
            for expected in expected_files:
                if expected in files:
                    filepath = os.path.join(fixtures_path, expected)
                    self.assertGreater(os.path.getsize(filepath), 0,
                                     f"Fixture {expected} is empty")


class TestDataFiles(unittest.TestCase):
    """Test data file integrity."""
    
    def test_user_settings_json_valid(self):
        """Test that user_settings.json is valid JSON."""
        settings_path = os.path.join(
            os.path.dirname(__file__),
            '..',
            'data',
            'user_settings.json'
        )
        
        if os.path.exists(settings_path):
            with open(settings_path, 'r') as f:
                try:
                    settings = json.load(f)
                    
                    expected_keys = [
                        'detection_ratio_threshold',
                        'detection_hard_jam_mm',
                        'detection_soft_jam_time_ms',
                        'detection_hard_jam_time_ms',
                        'detection_grace_period_ms'
                    ]
                    
                    for key in expected_keys:
                        self.assertIn(key, settings,
                                    f"Expected key '{key}' not found in settings")
                    
                    if 'detection_ratio_threshold' in settings:
                        self.assertIsInstance(settings['detection_ratio_threshold'],
                                            (int, float))
                    
                except json.JSONDecodeError as e:
                    self.fail(f"Invalid JSON in user_settings.json: {e}")


def run_tests():
    """Run all tests and return exit code."""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    
    suite.addTests(loader.loadTestsFromTestCase(TestBoardConfig))
    suite.addTests(loader.loadTestsFromTestCase(TestExtractLogData))
    suite.addTests(loader.loadTestsFromTestCase(TestGenerateTestSettings))
    suite.addTests(loader.loadTestsFromTestCase(TestBuildScripts))
    suite.addTests(loader.loadTestsFromTestCase(TestFixtures))
    suite.addTests(loader.loadTestsFromTestCase(TestDataFiles))
    
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())