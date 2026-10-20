if(NOT DEFINED SDL_SOURCE_DIR OR NOT EXISTS "${SDL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL_SOURCE_DIR does not point to an SDL3 source tree.")
endif()
if(NOT DEFINED PATCH_DIR)
    message(FATAL_ERROR "PATCH_DIR was not provided.")
endif()
if(NOT DEFINED GIT_EXECUTABLE)
    find_package(Git REQUIRED)
    set(GIT_EXECUTABLE "${GIT_EXECUTABLE}")
endif()

function(apply_sdl_patch name strip_components)
    set(patch "${PATCH_DIR}/${name}")
    if(NOT EXISTS "${patch}")
        message(FATAL_ERROR "SDL3 Switch patch not found: ${patch}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "-p${strip_components}" "${patch}"
        WORKING_DIRECTORY "${SDL_SOURCE_DIR}"
        RESULT_VARIABLE check_result
        OUTPUT_VARIABLE check_output
        ERROR_VARIABLE check_error)
    if(check_result EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "-p${strip_components}" "${patch}"
            WORKING_DIRECTORY "${SDL_SOURCE_DIR}"
            RESULT_VARIABLE apply_result
            OUTPUT_VARIABLE apply_output
            ERROR_VARIABLE apply_error)
        if(NOT apply_result EQUAL 0)
            message(FATAL_ERROR "Could not apply ${name}: ${apply_error}${apply_output}")
        endif()
        message(STATUS "Applied SDL3 Switch patch ${name}")
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "-p${strip_components}" "${patch}"
        WORKING_DIRECTORY "${SDL_SOURCE_DIR}"
        RESULT_VARIABLE reverse_result)
    if(reverse_result EQUAL 0)
        message(STATUS "SDL3 Switch patch ${name} is already applied")
        return()
    endif()

    message(FATAL_ERROR "SDL3 Switch patch ${name} does not match this SDL source revision.\n${check_error}${check_output}")
endfunction()

apply_sdl_patch("sdl3-switch.patch" 3)
apply_sdl_patch("sdl3-switch-gles3.patch" 1)
