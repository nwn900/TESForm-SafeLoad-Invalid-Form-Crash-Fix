cmake_minimum_required(VERSION 3.21)
project(TESFormSafeLoadTests LANGUAGES CXX)

include(CTest)

if(BUILD_TESTING)
    add_executable(saveloadguard_core_tests
        tests/saveload_guard_core_tests.cpp
        src/Runtime/SaveLoadGuardScan.cpp
        src/Validation/MemoryReader.cpp
        src/Validation/SaveLoadGuardValidation.cpp)
    target_include_directories(saveloadguard_core_tests PRIVATE src)
    target_compile_features(saveloadguard_core_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(saveloadguard_core_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(saveloadguard_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME saveloadguard_core_tests COMMAND saveloadguard_core_tests)
endif()
