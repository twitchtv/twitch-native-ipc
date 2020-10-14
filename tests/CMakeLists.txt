add_executable(nativeipc_tests)

target_sources(nativeipc_tests PRIVATE
  ConnectionTests.cpp
  )

target_compile_features(nativeipc_tests PRIVATE cxx_std_17)

if(MSVC)
  target_compile_definitions(nativeipc_tests PRIVATE
	$<IF:$<BOOL:${BUILD_SHARED_LIBS}>, NATIVEIPC_IMPORT , >
	)
endif()

target_link_libraries(nativeipc_tests PRIVATE
  CONAN_PKG::gtest
  nativeipc)

if(UNIX)
  target_link_libraries(nativeipc_tests PRIVATE
	m)
endif()

googletest_add_test("nativeipc_tests" nativeipc_tests)
