cmake_minimum_required(VERSION 3.10)
project(GFX_Examples)

# Add libgfx subdirectory
add_subdirectory(../libgfx)

# ============================================================================
# Simple Example - Basic shapes drawing
# ============================================================================
add_executable(example_simple
    example_simple.cpp
)
target_link_libraries(example_simple PRIVATE libgfx)
target_include_directories(example_simple PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../libgfx)


# ============================================================================
# Input Example - Mouse input tracking
# ============================================================================
add_executable(example_input
    example_input.cpp
)
target_link_libraries(example_input PRIVATE libgfx)
target_include_directories(example_input PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/libgfx)


# ============================================================================
# Paint Example - Simple paint program
# ============================================================================
add_executable(example_paint
    example_paint.cpp
)
target_link_libraries(example_paint PRIVATE libgfx)
target_include_directories(example_paint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/libgfx)


# ============================================================================
# Print build info
# ============================================================================
message(STATUS "GFX Examples:")
message(STATUS "  example_simple - Basic shapes drawing")
message(STATUS "  example_input  - Mouse input tracking")
message(STATUS "  example_paint  - Simple paint program")
