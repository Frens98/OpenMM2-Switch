# Native Horizon homebrew executable and Homebrew Menu metadata.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    return()
endif()

if(NOT TARGET openmm2)
    message(FATAL_ERROR "The OpenMM2 executable target is required for an NRO build.")
endif()

find_program(OPENMM2_ELF2NRO elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
find_program(OPENMM2_NACPTOOL nacptool HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
set(OPENMM2_SWITCH_ICON "${PROJECT_SOURCE_DIR}/packaging/icons/openmm2-256.jpg"
    CACHE FILEPATH "256x256 JPEG icon embedded in the Switch NRO")
if(NOT EXISTS "${OPENMM2_SWITCH_ICON}")
    message(FATAL_ERROR "Switch icon not found: ${OPENMM2_SWITCH_ICON}")
endif()

set(_nro "${PROJECT_BINARY_DIR}/bin/openmm2.nro")
set(_nacp "${PROJECT_BINARY_DIR}/bin/openmm2.nacp")
add_custom_command(
    OUTPUT "${_nro}"
    COMMAND "${OPENMM2_NACPTOOL}" --create "Midtown Madness 2" "OpenMM2" "${PROJECT_VERSION}" "${_nacp}"
    COMMAND "${OPENMM2_ELF2NRO}" "$<TARGET_FILE:openmm2>" "${_nro}"
            "--icon=${OPENMM2_SWITCH_ICON}" "--nacp=${_nacp}"
    DEPENDS openmm2 "${OPENMM2_SWITCH_ICON}"
    COMMENT "Packing Midtown Madness 2 as a Nintendo Switch NRO"
    VERBATIM)
add_custom_target(openmm2_nro ALL DEPENDS "${_nro}")
