# Repo-owned SystemC dependency baseline; no package downloads here.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../contracts/environment.json" _esl_environment)
string(JSON ESL_SYSTEMC_VERSION GET "${_esl_environment}" systemc_version)
string(JSON ESL_CXX_STANDARD GET "${_esl_environment}" cxx_standard)
if(DEFINED ENV{SYSTEMC_HOME})
  list(PREPEND CMAKE_PREFIX_PATH "$ENV{SYSTEMC_HOME}")
endif()
if(SYSTEMC_HOME)
  list(PREPEND CMAKE_PREFIX_PATH "${SYSTEMC_HOME}")
endif()
# Accellera's package version may append a release date (3.0.2.20251031).
# Find it, then compare the semantic release instead of rejecting its suffix.
find_package(SystemCLanguage ${ESL_SYSTEMC_VERSION} CONFIG REQUIRED)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" _esl_found_version "${SystemCLanguage_VERSION}")
if(NOT _esl_found_version STREQUAL ESL_SYSTEMC_VERSION)
  message(FATAL_ERROR "SystemC release ${_esl_found_version} does not match required ${ESL_SYSTEMC_VERSION}")
endif()
