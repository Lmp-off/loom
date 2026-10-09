#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <algorithm>

struct TestFlags {
    bool all = false;
    bool list = false;
    bool verbose = false;
    std::vector<std::string> skip;
    std::vector<std::string> only;
};

TestFlags g_flags;

struct TestCase {
    std::string name;
    std::string group;
    std::function<int()> fn;
    bool enabled = true;
    int duration_ms = 0;
    int result = 0;
};

std::vector<TestCase> g_tests;
int g_passed = 0;
int g_failed = 0;
int g_skipped = 0;

int test_cache_all();
int test_module_all();
int test_uds_all();
int test_jit_all();
int test_connect_all();
int test_send_recv_all();

void register_all_tests() {
    g_tests.push_back({"cache:all", "cache", test_cache_all});
    g_tests.push_back({"module:all", "module", test_module_all});
    g_tests.push_back({"uds:all", "uds", test_uds_all});
    g_tests.push_back({"jit:all", "jit", test_jit_all});
    g_tests.push_back({"connect:all", "connect", test_connect_all});
    g_tests.push_back({"send_recv:all", "send_recv", test_send_recv_all});
}

void print_usage() {
    std::cout << "\nLoom Brutal Tester\n";
    std::cout << "Usage: ./tester [flags]\n\n";
    std::cout << "Flags:\n";
    std::cout << "  -all              Run all tests\n";
    std::cout << "  -list             List all tests\n";
    std::cout << "  -skip <name>      Skip test by name/group\n";
    std::cout << "  -only <name>      Run only this test/group\n";
    std::cout << "  -h, --help        Show this help\n";
    std::cout << "\nExamples:\n";
    std::cout << "  ./tester -all\n";
    std::cout << "  ./tester -only module\n";
    std::cout << "  ./tester -skip uds -skip jit\n";
    std::cout << "  ./tester -list\n";
    std::cout << std::endl;
}

bool parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return false;
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-all") g_flags.all = true;
        else if (arg == "-list") g_flags.list = true;
        else if (arg == "-v" || arg == "--verbose") g_flags.verbose = true;
        else if (arg == "-skip" && i + 1 < argc) g_flags.skip.push_back(argv[++i]);
        else if (arg == "-only" && i + 1 < argc) g_flags.only.push_back(argv[++i]);
        else if (arg == "-h" || arg == "--help") { print_usage(); return false; }
        else { std::cerr << "Unknown flag: " << arg << std::endl; print_usage(); return false; }
    }

    if (!g_flags.all && g_flags.only.empty() && !g_flags.list) {
        print_usage();
        return false;
    }
    return true;
}

void apply_filters() {
    for (auto& test : g_tests) {
        if (!g_flags.only.empty()) {
            bool found = false;
            for (const auto& name : g_flags.only) {
                if (test.name.find(name) != std::string::npos || test.group == name) {
                    found = true; break;
                }
            }
            if (!found) test.enabled = false;
        }
        for (const auto& name : g_flags.skip) {
            if (test.name.find(name) != std::string::npos || test.group == name) {
                test.enabled = false;
            }
        }
    }
}

void list_tests() {
    std::cout << "\nAvailable tests:\n";
    std::string current_group;
    for (const auto& test : g_tests) {
        if (test.group != current_group) {
            current_group = test.group;
            std::cout << "\n[" << current_group << "]\n";
        }
        std::cout << "  " << test.name << (test.enabled ? "" : " (disabled)") << "\n";
    }
    std::cout << std::endl;
}

int run_tests() {
    std::cout << "\n  LOOM BRUTAL TESTER\n\n";

    for (auto& test : g_tests) {
        if (!test.enabled) {
            g_skipped++;
            if (g_flags.verbose) std::cout << "[ SKIP ] " << test.name << "\n";
            continue;
        }

        std::cout << "[ RUN  ] " << test.name << " ... " << std::flush;
        auto start = std::chrono::steady_clock::now();
        test.result = test.fn();
        auto end = std::chrono::steady_clock::now();
        test.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (test.result == 0) {
            std::cout << "✅ PASSED (" << test.duration_ms << "ms)\n";
            g_passed++;
        } else {
            std::cout << "❌ FAILED (" << test.duration_ms << "ms)\n";
            g_failed++;
        }
    }

    std::cout << "  ✅ Passed:  " << g_passed << "\n";
    std::cout << "  ❌ Failed:  " << g_failed << "\n";
    if (g_skipped > 0) std::cout << "  ⏭️  Skipped: " << g_skipped << "\n";
    return g_failed;
}

int main(int argc, char* argv[]) {
    register_all_tests();
    if (!parse_args(argc, argv)) return 1;
    if (g_flags.list) { list_tests(); return 0; }
    apply_filters();
    return run_tests();
}
