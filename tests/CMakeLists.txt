add_executable(ObserverMetaTests observer_meta_tests.cpp)
target_link_libraries(ObserverMetaTests PRIVATE ObserverMeta::ObserverMeta)

if(MSVC)
    target_compile_options(ObserverMetaTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(ObserverMetaTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME ObserverMeta.Unit COMMAND ObserverMetaTests)
set_tests_properties(ObserverMeta.Unit PROPERTIES LABELS "unit;foundation")
