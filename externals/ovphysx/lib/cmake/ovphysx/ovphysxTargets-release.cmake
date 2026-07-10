#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ovphysx::ovphysx" for configuration "Release"
set_property(TARGET ovphysx::ovphysx APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(ovphysx::ovphysx PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/ovphysx.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/ovphysx.dll"
  )

list(APPEND _cmake_import_check_targets ovphysx::ovphysx )
list(APPEND _cmake_import_check_files_for_ovphysx::ovphysx "${_IMPORT_PREFIX}/lib/ovphysx.lib" "${_IMPORT_PREFIX}/bin/ovphysx.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
