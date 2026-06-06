include_directories(../)

add_library(DataLoadMDF SHARED dataload_mdf.cpp)

target_link_libraries(DataLoadMDF PRIVATE Qt5::Widgets Qt5::Xml plotjuggler_base Upstream::mdf
                                          ${PJ_MDF_EXTRA_LIBRARIES})

target_compile_definitions(DataLoadMDF PRIVATE QT_PLUGIN)

install(TARGETS DataLoadMDF DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY})
