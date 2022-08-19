execute_process(
    COMMAND ${CMAKE_BINARY_DIR}/bdist.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/../python
    RESULT_VARIABLE ok)
if (NOT ok EQUAL 0)
    message(FATAL_ERROR "Failed to build python wheel")
endif()
