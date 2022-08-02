file(REMOVE_RECURSE
    ${CMAKE_BINARY_DIR}/../python/tpu_perf.egg-info
    ${CMAKE_BINARY_DIR}/../python/dist
    ${CMAKE_BINARY_DIR}/../python/build)
execute_process(
    COMMAND python3 ./setup.py bdist_wheel
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/../python
    RESULT_VARIABLE ok)
if (NOT ok EQUAL 0)
    message(FATAL_ERROR "Failed to build python wheel")
endif()
