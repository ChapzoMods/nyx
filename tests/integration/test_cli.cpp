// =============================================================================
// Nyx integration tests: CLI smoke tests
// =============================================================================
#include <doctest/doctest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef NYX_BINARY_DIR
#  define NYX_BINARY_DIR "build/bin"
#endif
#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace fs = std::filesystem;

namespace {

std::string run_nyx(const std::vector<std::string>& args, int* exit_code = nullptr) {
    const fs::path bin = fs::path(NYX_BINARY_DIR) / "nyx";
    REQUIRE(fs::exists(bin));

    std::string cmd = bin.string();
    cmd += " 2>&1";  // combine stderr into stdout for simplicity
    for (const auto& a : args) {
        cmd += " \"";
        for (char c : a) {
            if (c == '"') cmd += "\\\"";
            else          cmd += c;
        }
        cmd += "\"";
    }

    std::array<char, 4096> buf{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        if (exit_code) *exit_code = -1;
        return {};
    }
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        result += buf.data();
    }
    const int rc = pclose(pipe);
    if (exit_code) *exit_code = WEXITSTATUS(rc);
    return result;
}

}  // namespace

TEST_CASE("CLI: --version prints banner") {
    int rc = -1;
    const std::string out = run_nyx({"--version"}, &rc);
    CHECK(rc == 0);
    CHECK(out.find("Nyx") != std::string::npos);
    CHECK(out.find("Chapzoo") != std::string::npos);
    CHECK(out.find("GPL") != std::string::npos);
}

TEST_CASE("CLI: --help prints usage") {
    int rc = -1;
    const std::string out = run_nyx({"--help"}, &rc);
    CHECK(rc == 0);
    CHECK(out.find("Usage:") != std::string::npos);
    CHECK(out.find("--format") != std::string::npos);
}

TEST_CASE("CLI: missing input returns exit code 1") {
    int rc = 0;
    const std::string out = run_nyx({}, &rc);
    CHECK(rc == 1);
    CHECK(out.find("no input binary") != std::string::npos);
}

TEST_CASE("CLI: JSON output on sample.elf") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));
    int rc = 0;
    const std::string out = run_nyx({"--format", "json", "--log-level", "error", path}, &rc);
    CHECK(rc == 0);
    CHECK(out.find("\"schema\":") != std::string::npos);
    CHECK(out.find("\"arch\": \"x86-64\"") != std::string::npos);
}

TEST_CASE("CLI: pseudo-C output on sample.elf contains main") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));
    int rc = 0;
    const std::string out = run_nyx({"--format", "pseudo-c", "--log-level", "error", path}, &rc);
    CHECK(rc == 0);
    const bool has_main_void = out.find("void main") != std::string::npos;
    const bool has_main_func = out.find("Function: main") != std::string::npos;
    CHECK((has_main_void || has_main_func));
}

TEST_CASE("CLI: text output on sample.elf is non-empty") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));
    int rc = 0;
    const std::string out = run_nyx({"--format", "text", "--log-level", "error", path}, &rc);
    CHECK(rc == 0);
    CHECK(out.find("Sections") != std::string::npos);
    CHECK(out.find(".text") != std::string::npos);
}
