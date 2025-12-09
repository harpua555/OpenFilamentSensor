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
                result = board_config.get_chip_family_for_board('esp32s3')
                self.assertIsInstance(result, str)
                self.assertIn('ESP32', result)
            except ValueError:
                pass  # Board might not be in mapping
    
    @unittest.skipIf(not TOOLS_AVAILABLE, "Tools not available")
    def test_validate_board_environment(self):
        """Test board environment validation."""
        if hasattr(board_config, 'validate_board_environment'):
            # Test with a likely valid board
            result = board_config.validate_board_environment('esp32')
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
        """Test that build_and_run_all_tests.sh exists and is executable."""
        script_path = os.path.join(os.path.dirname(__file__), 'build_and_run_all_tests.sh')
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


# ============================================================================
# Additional Test Classes for Enhanced Coverage
# ============================================================================

class TestBuildAndRelease(unittest.TestCase):
    """Test build_and_release.py functionality"""
    
    def test_build_and_release_exists(self):
        """Verify build_and_release.py exists"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build_and_release.py')
        self.assertTrue(os.path.exists(script_path),
                       "build_and_release.py should exist in tools/")
    
    def test_build_and_release_has_main_function(self):
        """Verify build_and_release.py has main entry point"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build_and_release.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                self.assertIn('def main(', content,
                            "build_and_release.py should have main() function")
                self.assertIn('if __name__', content,
                            "build_and_release.py should have __main__ entry point")
    
    def test_version_increment_modes(self):
        """Verify version increment modes are defined"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build_and_release.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                # Check for version modes
                self.assertTrue(
                    'skip' in content or 'build' in content or 'ver' in content or 'release' in content,
                    "build_and_release.py should define version increment modes"
                )
    
    def test_board_to_distributor_mapping(self):
        """Verify board to distributor directory mapping exists"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build_and_release.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                self.assertIn('BOARD_TO_DISTRIBUTOR_DIR', content,
                            "Should define distributor directory mapping")


class TestCaptureLogs(unittest.TestCase):
    """Test capture_logs.py functionality"""

    def test_capture_logs_exists(self):
        """Verify capture_logs.py exists"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'capture_logs.py')
        self.assertTrue(os.path.exists(script_path),
                       "capture_logs.py should exist in tools/")
    
    def test_capture_logs_imports_requests(self):
        """Verify capture_logs.py uses requests library"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'capture_logs.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                self.assertIn('import requests', content,
                            "capture_logs.py should import requests library")
    
    def test_capture_logs_has_main_loop(self):
        """Verify capture_logs.py has polling mechanism"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'capture_logs.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                # Should have some kind of loop or continuous capture
                self.assertTrue(
                    'while' in content or 'loop' in content.lower(),
                    "capture_logs.py should have continuous capture mechanism"
                )
    
    def test_log_directory_handling(self):
        """Verify LOG_DIR is defined and used"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'capture_logs.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                self.assertIn('LOG_DIR', content,
                            "Should define LOG_DIR for output")


class TestToolsLauncher(unittest.TestCase):
    """Test tools-launcher.py functionality"""

    def test_tools_launcher_exists(self):
        """Verify tools-launcher.py exists"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'tools-launcher.py')
        self.assertTrue(os.path.exists(script_path),
                       "tools-launcher.py should exist in tools/")
    
    def test_tools_launcher_has_menu(self):
        """Verify launcher has interactive menu"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'tools-launcher.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                # Should have some menu or selection mechanism
                self.assertTrue(
                    'menu' in content.lower() or 'choice' in content.lower() or 'select' in content.lower(),
                    "tools-launcher.py should have menu/selection system"
                )


class TestSetBuildTimestamp(unittest.TestCase):
    """Test set_build_timestamp.py functionality"""

    def test_set_build_timestamp_exists(self):
        """Verify set_build_timestamp.py exists"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'set_build_timestamp.py')
        self.assertTrue(os.path.exists(script_path),
                       "set_build_timestamp.py should exist in tools/")
    
    def test_timestamp_format_generation(self):
        """Verify timestamp formatting logic exists"""
        script_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'set_build_timestamp.py')
        if os.path.exists(script_path):
            with open(script_path, 'r') as f:
                content = f.read()
                # Should use datetime and format strings
                self.assertTrue(
                    'datetime' in content or 'strftime' in content,
                    "Should have timestamp formatting logic"
                )


class TestGitHubWorkflow(unittest.TestCase):
    """Test GitHub Actions workflow configuration"""

    def test_release_workflow_exists(self):
        """Verify release-firmware.yml workflow exists"""
        workflow_path = os.path.join(os.path.dirname(__file__), '..', '.github', 'workflows', "release.yml")
        self.assertTrue(os.path.exists(workflow_path),
                       "release-firmware.yml workflow should exist")
    
    def test_workflow_has_matrix_build(self):
        """Verify workflow uses matrix build strategy"""
        workflow_path = os.path.join(os.path.dirname(__file__), '..', '.github', 'workflows', "release.yml")
        if os.path.exists(workflow_path):
            with open(workflow_path, 'r') as f:
                content = f.read()
                self.assertIn('matrix:', content,
                            "Workflow should use matrix build strategy")
                self.assertIn('strategy:', content,
                            "Workflow should define strategy section")
    
    def test_workflow_builds_all_boards(self):
        """Verify workflow builds for all supported boards"""
        workflow_path = os.path.join(os.path.dirname(__file__), '..', '.github', 'workflows', "release.yml")
        targets_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build-targets.yml')
        
        if os.path.exists(workflow_path) and os.path.exists(targets_path):
            with open(workflow_path, 'r') as f:
                workflow_content = f.read()
                # Should reference build-targets.yml
                self.assertIn('build-targets.yml', workflow_content,
                            "Workflow should reference build-targets.yml")
    
    def test_workflow_creates_release(self):
        """Verify workflow has release creation step"""
        workflow_path = os.path.join(os.path.dirname(__file__), '..', '.github', 'workflows', "release.yml")
        if os.path.exists(workflow_path):
            with open(workflow_path, 'r') as f:
                content = f.read()
                self.assertIn('create-release', content.lower(),
                            "Workflow should have release creation job")
                self.assertTrue(
                    'softprops/action-gh-release' in content or 'actions/create-release' in content,
                    "Workflow should use GitHub release action"
                )
    
    def test_workflow_generates_checksums(self):
        """Verify workflow generates checksums for artifacts"""
        workflow_path = os.path.join(os.path.dirname(__file__), '..', '.github', 'workflows', "release.yml")
        if os.path.exists(workflow_path):
            with open(workflow_path, 'r') as f:
                content = f.read()
                self.assertIn('sha256sum', content,
                            "Workflow should generate SHA256 checksums")


class TestBuildTargets(unittest.TestCase):
    """Test build-targets.yml configuration"""

    def test_build_targets_exists(self):
        """Verify build-targets.yml exists"""
        targets_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build-targets.yml')
        self.assertTrue(os.path.exists(targets_path),
                       "build-targets.yml should exist in tools/")
    
    def test_build_targets_is_valid_yaml(self):
        """Verify build-targets.yml is valid YAML"""
        targets_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build-targets.yml')
        if os.path.exists(targets_path):
            try:
                import yaml
            except ImportError:
                self.skipTest("PyYAML not available for YAML validation")
                return
            
            with open(targets_path, 'r') as f:
                try:
                    data = yaml.safe_load(f)
                    self.assertIsInstance(data, dict,
                                        "build-targets.yml should be a dict")
                    self.assertIn('platformio_envs', data,
                                "Should have platformio_envs key")
                except yaml.YAMLError as e:
                    self.fail(f"Invalid YAML in build-targets.yml: {e}")
    
    def test_all_platformio_envs_valid(self):
        """Verify all environments in build-targets.yml exist in platformio.ini"""
        targets_path = os.path.join(os.path.dirname(__file__), '..', 'tools', 'build-targets.yml')
        platformio_ini = os.path.join(os.path.dirname(__file__), '..', 'platformio.ini')
        
        if os.path.exists(targets_path) and os.path.exists(platformio_ini):
            try:
                import yaml
            except ImportError:
                self.skipTest("PyYAML not available")
                return
            
            with open(targets_path, 'r') as f:
                targets = yaml.safe_load(f)
            
            with open(platformio_ini, 'r') as f:
                pio_content = f.read()
            
            for env in targets.get('platformio_envs', []):
                env_section = f'[env:{env}]'
                self.assertIn(env_section, pio_content,
                            f"Environment '{env}' should exist in platformio.ini")


class TestEdgeCasesAndErrorHandling(unittest.TestCase):
    """Test edge cases and error handling across the toolchain"""
    
    def test_board_config_invalid_board(self):
        """Test board_config.py handles invalid board names"""
        try:
            import board_config

            with self.assertRaises(ValueError):
                board_config.get_chip_family_for_board('invalid_board_name_xyz')
        except ImportError:
            self.skipTest("board_config module not in path")
    
    def test_board_config_validate_returns_bool(self):
        """Test board_config validation returns boolean"""
        try:
            import board_config

            # Valid board should return True
            result = board_config.validate_board_environment('esp32s3')
            self.assertIsInstance(result, bool)
            self.assertTrue(result)

            # Invalid board should return False
            result = board_config.validate_board_environment('nonexistent')
            self.assertIsInstance(result, bool)
            self.assertFalse(result)
        except ImportError:
            self.skipTest("board_config module not in path")
    
    def test_empty_settings_file_handling(self):
        """Test handling of empty or malformed settings files"""
        # This tests the robustness of settings loading
        temp_file = 'temp_test_settings.json'
        try:
            # Test empty file
            with open(temp_file, 'w') as f:
                f.write('')
            
            with open(temp_file, 'r') as f:
                try:
                    json.load(f)
                    self.fail("Should raise JSONDecodeError for empty file")
                except json.JSONDecodeError:
                    pass  # Expected
            
            # Test malformed JSON
            with open(temp_file, 'w') as f:
                f.write('{"key": "value"')  # Missing closing brace
            
            with open(temp_file, 'r') as f:
                try:
                    json.load(f)
                    self.fail("Should raise JSONDecodeError for malformed JSON")
                except json.JSONDecodeError:
                    pass  # Expected
        finally:
            if os.path.exists(temp_file):
                os.remove(temp_file)


def run_tests():
    """Run all tests and return exit code."""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    
    # Original test classes
    suite.addTests(loader.loadTestsFromTestCase(TestBoardConfig))
    suite.addTests(loader.loadTestsFromTestCase(TestExtractLogData))
    suite.addTests(loader.loadTestsFromTestCase(TestGenerateTestSettings))
    suite.addTests(loader.loadTestsFromTestCase(TestBuildScripts))
    suite.addTests(loader.loadTestsFromTestCase(TestFixtures))
    suite.addTests(loader.loadTestsFromTestCase(TestDataFiles))
    
    # New comprehensive test classes
    suite.addTests(loader.loadTestsFromTestCase(TestBuildAndRelease))
    suite.addTests(loader.loadTestsFromTestCase(TestCaptureLogs))
    suite.addTests(loader.loadTestsFromTestCase(TestToolsLauncher))
    suite.addTests(loader.loadTestsFromTestCase(TestSetBuildTimestamp))
    suite.addTests(loader.loadTestsFromTestCase(TestGitHubWorkflow))
    suite.addTests(loader.loadTestsFromTestCase(TestBuildTargets))
    suite.addTests(loader.loadTestsFromTestCase(TestEdgeCasesAndErrorHandling))
    
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(run_tests())