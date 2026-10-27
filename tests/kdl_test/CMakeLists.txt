project(kdl_test)

add_executable(${PROJECT_NAME} ${PROJECT_NAME}.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE glz_test_common)
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
target_code_coverage(${PROJECT_NAME} AUTO ALL)

add_executable(kdl_direct_test kdl_direct_test.cpp)
target_link_libraries(kdl_direct_test PRIVATE glz_test_common)
add_test(NAME kdl_direct_test COMMAND kdl_direct_test)
target_code_coverage(kdl_direct_test AUTO ALL)

add_executable(kdl_mixed_test kdl_mixed_test.cpp)
target_link_libraries(kdl_mixed_test PRIVATE glz_test_common)
add_test(NAME kdl_mixed_test COMMAND kdl_mixed_test)
target_code_coverage(kdl_mixed_test AUTO ALL)

foreach(kdl_target IN ITEMS
    kdl_standard_test kdl_custom_test kdl_scan_test kdl_borrow_test kdl_lazy_test
    kdl_stream_test kdl_tools_test kdl_query_test kdl_rpc_test kdl_schema_test
    kdl_hash_test kdl_constraint_test kdl_header_compatibility_test)
    add_executable(${kdl_target} ${kdl_target}.cpp)
    target_link_libraries(${kdl_target} PRIVATE glz_test_common)
    add_test(NAME ${kdl_target} COMMAND ${kdl_target})
    target_code_coverage(${kdl_target} AUTO ALL)
endforeach()

add_executable(kdl_scan_scalar_test kdl_scan_test.cpp)
target_link_libraries(kdl_scan_scalar_test PRIVATE glz_test_common)
target_compile_definitions(kdl_scan_scalar_test PRIVATE GLZ_DISABLE_SIMD)
add_test(NAME kdl_scan_scalar_test COMMAND kdl_scan_scalar_test)
target_code_coverage(kdl_scan_scalar_test AUTO ALL)

option(glaze_KDL_BUILD_BENCHMARK "Build the standalone KDL scanning benchmark" OFF)
if(glaze_KDL_BUILD_BENCHMARK)
    add_executable(kdl_benchmark kdl_benchmark.cpp)
    target_link_libraries(kdl_benchmark PRIVATE glaze::glaze)
endif()

add_executable(kdl_exceptions_test kdl_exceptions_test.cpp)
target_link_libraries(kdl_exceptions_test PRIVATE glz_test_exceptions)
add_test(NAME kdl_exceptions_test COMMAND kdl_exceptions_test)
target_code_coverage(kdl_exceptions_test AUTO ALL)

# The upstream corpus can be supplied without downloading dependencies during a build.
set(glaze_KDL_TEST_SUITE "" CACHE PATH "Path to kdl-org/kdl tests/test_cases")
if(glaze_KDL_TEST_SUITE)
    add_executable(kdl_conformance kdl_conformance.cpp)
    target_link_libraries(kdl_conformance PRIVATE glz_test_common)
    add_test(NAME kdl_conformance COMMAND kdl_conformance "${glaze_KDL_TEST_SUITE}")
    target_code_coverage(kdl_conformance AUTO ALL)

    get_filename_component(kdl_upstream_root "${glaze_KDL_TEST_SUITE}/../.." ABSOLUTE)
    if(EXISTS "${kdl_upstream_root}/examples/kdl-schema.kdl")
        add_executable(kdl_schema_conformance kdl_schema_conformance.cpp)
        target_link_libraries(kdl_schema_conformance PRIVATE glz_test_common)
        add_test(NAME kdl_schema_conformance COMMAND kdl_schema_conformance "${kdl_upstream_root}/examples/kdl-schema.kdl")
        target_code_coverage(kdl_schema_conformance AUTO ALL)
    endif()

    add_executable(kdl_conformance_scalar kdl_conformance.cpp)
    target_link_libraries(kdl_conformance_scalar PRIVATE glz_test_common)
    target_compile_definitions(kdl_conformance_scalar PRIVATE GLZ_DISABLE_SIMD)
    add_test(NAME kdl_conformance_scalar COMMAND kdl_conformance_scalar "${glaze_KDL_TEST_SUITE}")
    target_code_coverage(kdl_conformance_scalar AUTO ALL)
endif()

if(glaze_ENABLE_REFLECTION26)
    add_executable(kdl_reflection_test kdl_reflection_test.cpp)
    target_link_libraries(kdl_reflection_test PRIVATE glz_test_common)
    add_test(NAME kdl_reflection_test COMMAND kdl_reflection_test)
    target_code_coverage(kdl_reflection_test AUTO ALL)
endif()
