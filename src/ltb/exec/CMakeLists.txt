# ##############################################################################
# A Logan Thomas Barnes project
# ##############################################################################
add_executable(basic_triangle_app basic_triangle_app.cpp)
target_link_libraries(basic_triangle_app PRIVATE LtbVlk::LtbVlk)

add_executable(framebuffer_triangle_app framebuffer_triangle_app.cpp)
target_link_libraries(framebuffer_triangle_app PRIVATE LtbVlk::LtbVlk)

#add_executable(composite_app composite_app.cpp)
#target_link_libraries(composite_app PRIVATE LtbVlk::LtbVlk)
#
#add_executable(frames_app frames_app.cpp)
#target_link_libraries(frames_app PRIVATE LtbVlk::LtbVlk)
