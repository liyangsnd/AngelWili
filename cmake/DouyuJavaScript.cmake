include(FetchContent)
FetchContent_Declare(duktape
    URL https://duktape.org/duktape-2.7.0.tar.xz
    URL_HASH SHA256=90f8d2fa8b5567c6899830ddef2c03f3c27960b11aca222fa17aa7ac613c2890)
FetchContent_MakeAvailable(duktape)

set(DUKTAPE_BUILD_DIR ${CMAKE_BINARY_DIR}/duktape)
file(MAKE_DIRECTORY ${DUKTAPE_BUILD_DIR})
configure_file(${duktape_SOURCE_DIR}/src/duktape.c ${DUKTAPE_BUILD_DIR}/duktape.c COPYONLY)
configure_file(${duktape_SOURCE_DIR}/src/duktape.h ${DUKTAPE_BUILD_DIR}/duktape.h COPYONLY)
file(READ ${duktape_SOURCE_DIR}/src/duk_config.h DUKTAPE_CONFIG)
string(REPLACE "#undef DUK_USE_INTERRUPT_COUNTER" "#define DUK_USE_INTERRUPT_COUNTER" DUKTAPE_CONFIG "${DUKTAPE_CONFIG}")
string(REPLACE "#undef DUK_USE_EXEC_TIMEOUT_CHECK"
    "#ifdef __cplusplus\nextern \"C\" {\n#endif\nextern int angelwili_js_timeout(void *udata);\n#ifdef __cplusplus\n}\n#endif\n#define DUK_USE_EXEC_TIMEOUT_CHECK(udata) angelwili_js_timeout(udata)"
    DUKTAPE_CONFIG "${DUKTAPE_CONFIG}")
file(WRITE ${DUKTAPE_BUILD_DIR}/duk_config.h "${DUKTAPE_CONFIG}")
add_library(angelwili_javascript STATIC ${DUKTAPE_BUILD_DIR}/duktape.c)
target_include_directories(angelwili_javascript PUBLIC ${DUKTAPE_BUILD_DIR})