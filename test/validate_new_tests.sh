#!/bin/bash

# Validation script for new test files
# Ensures all generated tests are present and properly structured

echo "=========================================="
echo "  Validating New Test Files"
echo "=========================================="
echo ""

EXIT_CODE=0

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to check file exists
check_file() {
    local file=$1
    local description=$2
    
    if [ -f "$file" ]; then
        echo -e "${GREEN}✓${NC} $description exists: $file"
        return 0
    else
        echo -e "${RED}✗${NC} $description missing: $file"
        EXIT_CODE=1
        return 1
    fi
}

# Function to check file has content
check_file_content() {
    local file=$1
    local min_lines=$2
    local description=$3
    
    if [ -f "$file" ]; then
        local line_count=$(wc -l < "$file")
        if [ "$line_count" -ge "$min_lines" ]; then
            echo -e "${GREEN}✓${NC} $description has sufficient content ($line_count lines)"
            return 0
        else
            echo -e "${YELLOW}⚠${NC} $description has only $line_count lines (expected $min_lines+)"
            return 0
        fi
    else
        echo -e "${RED}✗${NC} $description not found: $file"
        EXIT_CODE=1
        return 1
    fi
}

# Check new C++ test file
echo "Checking C++ Test Files:"
check_file "test_additional_edge_cases.cpp" "Additional Edge Cases Test"
check_file_content "test_additional_edge_cases.cpp" 400 "test_additional_edge_cases.cpp"

# Check enhanced Python tests
echo ""
echo "Checking Python Test Files:"
check_file "test_tools.py" "Python Tools Test"
check_file_content "test_tools.py" 200 "test_tools.py"

# Verify new test classes exist in test_tools.py
if [ -f "test_tools.py" ]; then
    echo "Checking for new Python test classes:"
    
    classes=(
        "TestBuildAndRelease"
        "TestCaptureLogs"
        "TestToolsLauncher"
        "TestSetBuildTimestamp"
        "TestGitHubWorkflow"
        "TestBuildTargets"
        "TestEdgeCasesAndErrorHandling"
    )
    
    for class in "${classes[@]}"; do
        if grep -q "class $class" test_tools.py; then
            echo -e "${GREEN}✓${NC} Found class: $class"
        else
            echo -e "${RED}✗${NC} Missing class: $class"
            EXIT_CODE=1
        fi
    done
fi

# Check enhanced JavaScript tests
echo ""
echo "Checking JavaScript Test Files:"
check_file "test_distributor.js" "Distributor Test"
check_file_content "test_distributor.js" 200 "test_distributor.js"

# Verify new test functions exist in test_distributor.js
if [ -f "test_distributor.js" ]; then
    echo "Checking for new JavaScript test functions:"
    
    functions=(
        "testFlasherJsStructure"
        "testManifestVersionConsistency"
        "testBoardChipFamilyValidation"
        "testManifestBuildStructure"
        "testFirmwareBinariesExist"
    )
    
    for func in "${functions[@]}"; do
        if grep -q "function $func" test_distributor.js; then
            echo -e "${GREEN}✓${NC} Found function: $func"
        else
            echo -e "${RED}✗${NC} Missing function: $func"
            EXIT_CODE=1
        fi
    done
fi

# Check documentation files
echo ""
echo "Checking Documentation Files:"
check_file "COMPREHENSIVE_TEST_COVERAGE.md" "Comprehensive Coverage Report"
check_file "QUICK_TEST_GUIDE.md" "Quick Test Guide"
check_file "NEW_COMPREHENSIVE_TESTS_SUMMARY.txt" "New Tests Summary"

# Check build script update
echo ""
echo "Checking Build Script Updates:"
check_file "build_and_run_all_tests.sh" "Master Test Runner"

if [ -f "build_and_run_all_tests.sh" ]; then
    if grep -q "test_additional_edge_cases" build_and_run_all_tests.sh; then
        echo -e "${GREEN}✓${NC} build_and_run_all_tests.sh includes new C++ tests"
    else
        echo -e "${RED}✗${NC} build_and_run_all_tests.sh does not include new C++ tests"
        EXIT_CODE=1
    fi
fi

# Summary
echo ""
echo "=========================================="
echo "  Validation Summary"
echo "=========================================="

if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}✓ All validation checks passed${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Run the test suite: ./build_and_run_all_tests.sh"
    echo "  2. Review coverage: cat COMPREHENSIVE_TEST_COVERAGE.md"
else
    echo -e "${RED}✗ Some validation checks failed${NC}"
    echo ""
    echo "Please ensure all test files are properly generated."
fi

echo ""
exit $EXIT_CODE