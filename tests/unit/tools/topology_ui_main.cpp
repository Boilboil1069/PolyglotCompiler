// Custom Catch2 entry point for test_topology_ui.
//
// This binary instantiates a Qt QApplication via function-local statics in the
// individual test translation units. On macOS the global destructor phase
// races with Qt's own teardown of static plugin/font/event-dispatcher state,
// which causes a SIGSEGV after every test has already passed. The race is
// harmless to the test outcome, but it makes CTest mark the run as failed.
//
// We work around it by running the Catch2 session manually and then calling
// std::_Exit(), which terminates the process without invoking C++ global
// destructors. All test results have already been reported by the time we
// get here.
#include <catch2/catch_session.hpp>
#include <cstdlib>

int main(int argc, char *argv[]) {
  int result = Catch::Session().run(argc, argv);
  std::_Exit(result);
}
