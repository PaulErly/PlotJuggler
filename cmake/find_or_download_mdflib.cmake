function(find_or_download_mdflib)

  set(PJ_MDF_EXTRA_LIBRARIES)

  if(TARGET mdf)
    message(STATUS "MDFLib target already defined")
    set(PJ_MDF_EXTRA_LIBRARIES ${PJ_MDF_EXTRA_LIBRARIES} PARENT_SCOPE)
    return()
  endif()

  find_package(mdflib QUIET)

  if(TARGET Upstream::mdf)
    message(STATUS "Found MDFLib in system")
  else()
    message(STATUS "MDFLib not found, downloading")

    if(NOT TARGET zlibstatic)
      set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Build zlib examples" FORCE)
      cpmaddpackage(
        NAME zlib
        URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip)
    endif()

    if(TARGET zlibstatic)
      set(ZLIB_FOUND TRUE CACHE BOOL "zlib found for mdflib" FORCE)
      set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
      set(ZLIB_LIBRARIES zlibstatic)
      list(APPEND PJ_MDF_EXTRA_LIBRARIES zlibstatic)
    endif()

    if(NOT TARGET expat)
      set(EXPAT_BUILD_DOCS OFF CACHE BOOL "Build expat docs" FORCE)
      set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "Build expat examples" FORCE)
      set(EXPAT_BUILD_FUZZERS OFF CACHE BOOL "Build expat fuzzers" FORCE)
      set(EXPAT_BUILD_PKGCONFIG OFF CACHE BOOL "Build expat pkg-config file" FORCE)
      set(EXPAT_BUILD_TESTS OFF CACHE BOOL "Build expat tests" FORCE)
      set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "Build expat tools" FORCE)
      set(EXPAT_SHARED_LIBS OFF CACHE BOOL "Build expat shared library" FORCE)
      cpmaddpackage(
        NAME expat
        URL https://github.com/libexpat/libexpat/archive/refs/tags/R_2_6_4.zip
        SOURCE_SUBDIR expat)
    endif()

    if(TARGET expat)
      set(EXPAT_FOUND TRUE CACHE BOOL "expat found for mdflib" FORCE)
      set(EXPAT_INCLUDE_DIRS ${expat_SOURCE_DIR}/expat/lib ${expat_BINARY_DIR}/expat)
      set(EXPAT_LIBRARIES expat)
      list(APPEND PJ_MDF_EXTRA_LIBRARIES expat)
    endif()

    set(MDF_BUILD_SHARED_LIB OFF CACHE BOOL "Build mdflib shared library" FORCE)
    set(MDF_BUILD_SHARED_LIB_NET OFF CACHE BOOL "Build mdflib .NET library" FORCE)
    set(MDF_BUILD_SHARED_LIB_EXAMPLE OFF CACHE BOOL "Build mdflib examples" FORCE)
    set(MDF_BUILD_DOC OFF CACHE BOOL "Build mdflib docs" FORCE)
    set(MDF_BUILD_TOOL OFF CACHE BOOL "Build mdflib tools" FORCE)
    set(MDF_BUILD_TEST OFF CACHE BOOL "Build mdflib tests" FORCE)
    set(MDF_BUILD_PYTHON OFF CACHE BOOL "Build mdflib Python module" FORCE)

    cpmaddpackage(
      NAME mdflib
      URL https://github.com/ihedvall/mdflib/archive/refs/tags/v2.3.0.zip
      DOWNLOAD_ONLY YES)

    if(mdflib_ADDED)
      add_subdirectory(${mdflib_SOURCE_DIR}/mdflib ${mdflib_BINARY_DIR}/mdflib EXCLUDE_FROM_ALL)
    endif()
  endif()

  if(TARGET mdf AND NOT TARGET Upstream::mdf)
    add_library(Upstream::mdf ALIAS mdf)
  endif()

  set(PJ_MDF_EXTRA_LIBRARIES ${PJ_MDF_EXTRA_LIBRARIES} PARENT_SCOPE)

endfunction()
