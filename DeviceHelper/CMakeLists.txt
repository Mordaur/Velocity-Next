add_library(device_helper_protocol STATIC protocol.cpp)
target_include_directories(device_helper_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(device_helper_protocol PUBLIC cxx_std_20)
set_target_properties(device_helper_protocol PROPERTIES FOLDER "Tools")

add_library(device_validation STATIC device_validation.cpp)
target_include_directories(device_validation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(device_validation PUBLIC cxx_std_20)
set_target_properties(device_validation PROPERTIES FOLDER "Tools")

add_executable(velocity-device-helper main.cpp)
target_link_libraries(velocity-device-helper PRIVATE device_helper_protocol device_validation velocity_compiler_flags)
target_compile_features(velocity-device-helper PRIVATE cxx_std_20)
set_target_properties(velocity-device-helper PROPERTIES FOLDER "Tools")
