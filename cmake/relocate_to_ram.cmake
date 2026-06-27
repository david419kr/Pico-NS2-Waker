# Relocate selected .text.<fn> sections of one compiled object into RAM by
# renaming them to .time_critical.<fn>, which the Pico SDK linker places in RAM.
foreach(_v OBJROOT OBJCOPY SUFFIX RENAMES)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "relocate_to_ram: missing required -D${_v}")
    endif()
endforeach()

file(GLOB_RECURSE _objs "${OBJROOT}/*.o" "${OBJROOT}/*.obj")
string(REGEX REPLACE "\\.o(bj)?$" "" _sfx "${SUFFIX}")
string(LENGTH "${_sfx}" _slen)
set(_match "")
foreach(_o ${_objs})
    string(REGEX REPLACE "\\.o(bj)?$" "" _ob "${_o}")
    string(LENGTH "${_ob}" _olen)
    if(NOT _olen LESS _slen)
        math(EXPR _off "${_olen} - ${_slen}")
        string(SUBSTRING "${_ob}" ${_off} -1 _tail)
        if(_tail STREQUAL "${_sfx}")
            set(_match "${_o}")
            break()
        endif()
    endif()
endforeach()

if(NOT _match)
    message(FATAL_ERROR
        "relocate_to_ram: no object matching '${SUFFIX}' under '${OBJROOT}'")
endif()

string(REPLACE "|" ";" _renames "${RENAMES}")
set(_args "")
foreach(_r ${_renames})
    list(APPEND _args --rename-section "${_r}")
endforeach()

execute_process(COMMAND "${OBJCOPY}" ${_args} "${_match}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "relocate_to_ram: objcopy failed (rc=${_rc}) on ${_match}")
endif()
