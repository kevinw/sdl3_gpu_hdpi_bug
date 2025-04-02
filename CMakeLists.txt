cmake_minimum_required(VERSION 3.10)
project(SDL3_hdpi_bug)

# Load .env file for SDL3_DIR (you can also set it manually in the environment or install it in a place CMake knows to find it)
if (EXISTS ${CMAKE_HOME_DIRECTORY}/.env)
    message(STATUS "Loading .env file")
    file(STRINGS ${CMAKE_HOME_DIRECTORY}/.env ENV_FILE)
    foreach(VAR ${ENV_FILE})
        string(REGEX MATCH "^[^=]*" KEY ${VAR})
        string(REGEX REPLACE "^[^=]*=" "" VALUE ${VAR})
        set(ENV{${KEY}} ${VALUE})
        message(STATUS "Loaded ${KEY} = ${VALUE}")
    endforeach()
else()
    message(STATUS "No .env file found")
endif()

find_package(SDL3 REQUIRED)

add_executable(main main.c)

target_link_libraries(main SDL3::SDL3)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Content $<TARGET_FILE_DIR:main>/Content
)
