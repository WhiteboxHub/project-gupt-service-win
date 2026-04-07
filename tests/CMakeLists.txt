# Test programs

# Test: Screen Capture
add_executable(test_capture
    test_capture.cpp
    ../host/capture.cpp
)

target_include_directories(test_capture PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_capture PRIVATE
    gupt_common
    d3d11.lib
    dxgi.lib
)

# Test: Encoder
add_executable(test_encoder
    test_encoder.cpp
    ../host/capture.cpp
    ../host/encoder.cpp
)

target_include_directories(test_encoder PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_encoder PRIVATE
    gupt_common
    d3d11.lib
    dxgi.lib
)

# Test: Network Client
add_executable(test_network_client
    test_network_client.cpp
)

target_include_directories(test_network_client PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_network_client PRIVATE
    gupt_common
    ws2_32.lib
)

# Install tests
install(TARGETS test_capture test_encoder test_network_client
    RUNTIME DESTINATION bin/tests
)
