include(functions/FetchContent_ExcludeFromAll_backport)
include(FetchContent)

# Only the library itself is needed (no tools, no animation or mux helpers).
set(WEBP_BUILD_ANIM_UTILS OFF)
set(WEBP_BUILD_CWEBP OFF)
set(WEBP_BUILD_DWEBP OFF)
set(WEBP_BUILD_GIF2WEBP OFF)
set(WEBP_BUILD_IMG2WEBP OFF)
set(WEBP_BUILD_VWEBP OFF)
set(WEBP_BUILD_WEBPINFO OFF)
set(WEBP_BUILD_WEBPMUX OFF)
set(WEBP_BUILD_EXTRAS OFF)
set(WEBP_BUILD_LIBWEBPMUX OFF)
set(WEBP_BUILD_FUZZTEST OFF)
set(WEBP_LINK_STATIC ON)
if(DEVILUTIONX_STATIC_LIBWEBP)
  set(BUILD_SHARED_LIBS OFF)
else()
  set(BUILD_SHARED_LIBS ON)
endif()

FetchContent_Declare_ExcludeFromAll(libwebp
  GIT_REPOSITORY https://github.com/webmproject/libwebp.git
  GIT_TAG f7710292e894c8413edaca4610d11dde843c04fd # v1.5.0
)
FetchContent_MakeAvailable_ExcludeFromAll(libwebp)

if(NOT TARGET WebP::webp)
  add_library(WebP::webp ALIAS webp)
endif()

# External library, disable warnings.
target_compile_options(webp PRIVATE -w)
