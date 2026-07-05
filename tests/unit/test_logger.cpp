// =============================================================================
// Nyx unit tests: logger.hpp
// =============================================================================
#include "nyx/core/logger.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

TEST_CASE("Logger: level filtering") {
    std::ostringstream sink;
    nyx::Logger::instance().set_sink(&sink);
    nyx::Logger::instance().set_level(nyx::LogLevel::Warn);

    nyx::Logger::instance().info("info-should-not-appear");
    nyx::Logger::instance().warn("warn-should-appear");
    nyx::Logger::instance().error("error-should-appear");

    const std::string s = sink.str();
    CHECK(s.find("info-should-not-appear") == std::string::npos);
    CHECK(s.find("warn-should-appear")     != std::string::npos);
    CHECK(s.find("error-should-appear")    != std::string::npos);

    // Reset to default sink for the rest of the test suite.
    nyx::Logger::instance().set_sink(nullptr);
    nyx::Logger::instance().set_level(nyx::LogLevel::Info);
}

TEST_CASE("Logger: trace is suppressed by default") {
    std::ostringstream sink;
    nyx::Logger::instance().set_sink(&sink);
    nyx::Logger::instance().set_level(nyx::LogLevel::Info);
    nyx::Logger::instance().trace("trace-should-not-appear");
    CHECK(sink.str().find("trace-should-not-appear") == std::string::npos);
    nyx::Logger::instance().set_sink(nullptr);
}

TEST_CASE("to_string(LogLevel): all severities") {
    CHECK(nyx::to_string(nyx::LogLevel::Trace)    == "trace");
    CHECK(nyx::to_string(nyx::LogLevel::Info)     == "info");
    CHECK(nyx::to_string(nyx::LogLevel::Critical) == "critical");
}
