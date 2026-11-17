add_library(mm-vfs STATIC src/content_vfs.c)
set_target_properties(mm-vfs PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(mm-vfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
