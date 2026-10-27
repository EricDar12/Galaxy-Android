# ---------------------------------------------------------------------------
# Warning coverage, split in two because the tree is in two: the decompiled
# GameCube SDK and the owned platform/ layer.  The root CMakeLists.txt includes
# this; the variables are read there when decomp and game are created.
# ---------------------------------------------------------------------------

# The decompiled SDK is translated 32-bit GameCube code: it casts pointers into
# u32, calls functions through pre-C99 prototypes, still spells register in the
# headers and leans on MSVC extensions. It is not ours to fix, so it keeps every
# suppression.
set(DECOMP_WARNING_SUPPRESSIONS
  -Wno-register
  -Wno-multichar
  -Wno-invalid-offsetof
  -Wno-c++11-narrowing
  -Wno-writable-strings
  -Wno-null-conversion
  -Wno-deprecated-declarations
  -Wno-int-to-pointer-cast
  -Wno-pointer-to-int-cast
  -Wno-invalid-source-encoding
  -Wno-unused-value
  -Wno-return-type
  -Wno-switch
  -Wno-parentheses
  -Wno-dangling-else
  -Wno-tautological-compare
  -Wno-tautological-undefined-compare
  -Wno-undefined-bool-conversion
  -Wno-constant-conversion
  -Wno-shift-negative-value
  -Wno-implicit-function-declaration
  -Wno-int-conversion
  -Wno-incompatible-pointer-types
  -Wno-incompatible-function-pointer-types
  -Wno-pointer-sign
  -Wno-unknown-pragmas
  -Wno-ignored-attributes
  -Wno-microsoft-template
  -Wno-void-pointer-to-int-cast
  -Wno-microsoft-cast
  -Wno-microsoft-anon-tag
  -Wno-microsoft-include
  -Wno-language-extension-token
)

# platform/ and platform/mobile are ours, so they do not inherit the decomp SDK's
# suppressions: the headers they include are passed as SYSTEM below, which silences
# the SDK's own diagnostics at the place they come from.  One option stays off, for
# the one owned source that still needs it:
#   -Wno-deprecated-declarations    ASensorManager_getInstance() is deprecated since
#                                   API 26, and platform/mobile/input/Sensors.cpp:23
#                                   still reaches for it when getInstanceForPackage()
#                                   answers null on a package the port cannot name.
# Everything else the compiler had to say about platform/ has been answered in the
# sources, so a new warning here means a new warning in the build log.
set(OWNED_WARNING_SUPPRESSIONS
  -Wno-deprecated-declarations
)

# -Wall -Wextra for the owned sources, which the decomp suppression list used to
# switch off for everything, plus the specific classes a port bug can hide behind
# pinned by name so they stay audible even if the umbrella flags change.
set(OWNED_DIAGNOSTICS
  -Wall
  -Wextra
  -Wint-to-pointer-cast
  -Wtautological-compare
  -Wreturn-type
  -Wswitch
  -Wparentheses
  -Wdangling-else
  -Wsizeof-array-argument
  $<$<COMPILE_LANGUAGE:C>:-Wpointer-sign>
  $<$<COMPILE_LANGUAGE:C>:-Wimplicit-function-declaration>
)
# GCC only implements the next three for C and answers a C++ translation unit with
# "valid for C but not for C++" on every file, and it rejects the clang-only
# spellings below outright. Clang has all of them for both languages.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  list(APPEND OWNED_DIAGNOSTICS
    $<$<COMPILE_LANGUAGE:C>:-Wpointer-to-int-cast>
    $<$<COMPILE_LANGUAGE:C>:-Wincompatible-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wint-conversion>
  )
else()
  list(APPEND OWNED_DIAGNOSTICS
    -Wpointer-to-int-cast
    -Wincompatible-pointer-types
    -Wint-conversion
    -Wvoid-pointer-to-int-cast
    -Wnull-conversion
    -Wtautological-undefined-compare
    -Wundefined-bool-conversion
    -Wconstant-conversion
    -Wincompatible-function-pointer-types
  )
endif()
