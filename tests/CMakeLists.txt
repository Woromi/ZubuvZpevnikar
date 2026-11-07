find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(zub_core_tests
    test_version.cpp
)
target_link_libraries(zub_core_tests PRIVATE Zub::core zub_warnings Catch2::Catch2WithMain)
target_compile_definitions(zub_core_tests PRIVATE ZUB_EXPECTED_VERSION="${PROJECT_VERSION}")

catch_discover_tests(zub_core_tests DISCOVERY_MODE PRE_TEST)
