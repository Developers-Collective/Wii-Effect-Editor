add_library(breff_codec STATIC resource_file.cpp records.cpp animation.cpp effect.cpp texture.cpp form.cpp version7.cpp)
target_include_directories(breff_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(breff_codec PUBLIC nlohmann_json::nlohmann_json)
