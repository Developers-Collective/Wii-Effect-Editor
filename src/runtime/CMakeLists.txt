# All NW4R inputs are shipped in this repository. No source downloads here.
file(GLOB_RECURSE NW4R_EFFECT_SOURCES CONFIGURE_DEPENDS nw4r/src/nw4r/ef/*.cpp)
# Resource loading needs an explicit v11 decoder on native platforms.
list(FILTER NW4R_EFFECT_SOURCES EXCLUDE REGEX "/ef_resource.cpp$")
# The imported tree contains two copies of this builder.
list(FILTER NW4R_EFFECT_SOURCES EXCLUDE REGEX "/drawstrategy/ef_drawstrategybuilder.cpp$")
list(FILTER NW4R_EFFECT_SOURCES EXCLUDE REGEX "/emform/ef_emform.cpp$")
add_library(breff_runtime STATIC ${NW4R_EFFECT_SOURCES}
    nw4r/src/nw4r/ut/ut_list.cpp
    nw4r/src/nw4r/math/math_triangular.cpp
    nw4r/src/nw4r/math/math_native.cpp)
target_include_directories(breff_runtime PUBLIC platform nw4r/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(breff_runtime PUBLIC aurora::gx)
target_compile_definitions(breff_runtime PUBLIC NW4R_LITTLE_ENDIAN _USE_MATH_DEFINES)
target_sources(breff_runtime PRIVATE curves.cpp archive.cpp resources.cpp platform.cpp engine.cpp fields.cpp postfield.cpp)
if(MSVC)
    target_compile_options(breff_runtime PRIVATE /Zi)
    target_link_libraries(breff_runtime PUBLIC dbghelp)
    target_link_options(breff_runtime INTERFACE /DEBUG)
    set_source_files_properties(curves.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
    set_source_files_properties(curves.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
