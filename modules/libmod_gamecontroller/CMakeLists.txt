project(mod_gamecontroller)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "../../bin/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "../../bin/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "../../bin/")

set(CMAKE_MODULE_PATH ../../cmake/Modules)

find_package(SDL2 REQUIRED)

add_definitions(${EXTRA_CFLAGS})

include_directories(${SDL2_INCLUDE_DIR} ${SDL2_INCLUDE_DIRS} ../../core/include ../../core/bgdrtm ../../vendor/stb)

file(GLOB SOURCES_LIBMOD_GAMECONTROLLER
     "../../modules/libmod_gamecontroller/*.c"
     )

add_library(mod_gamecontroller ${LIBRARY_BUILD_TYPE} ${SOURCES_LIBMOD_GAMECONTROLLER})
target_link_libraries(mod_gamecontroller ${SDL2_LIBRARY} ${SDL2_LIBRARIES} -L../../bin bgdrtm)
