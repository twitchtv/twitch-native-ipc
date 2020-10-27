
add_library(nativeipc)

target_compile_features(nativeipc PRIVATE cxx_std_17)

target_sources(nativeipc PRIVATE
  src/ClientConnection.cpp
  src/ClientConnection.h
  src/ConnectionBase.cpp
  src/ConnectionBase.h
  src/ConnectionExports.cpp
  src/ConnectionFactory.cpp
  src/ConnectionFactoryPrivate.h
  src/ConnectionFactoryTCP.cpp
  src/DeleteConstructors.h
  src/IClientTransport.h
  src/IServerTransport.h
  src/ITransportBase.h
  src/LogMacrosNoHandle.h
  src/LogMacrosWithHandle.h
  src/Message.h
  src/OperationQueue.cpp
  src/OperationQueue.h
  src/Pipe-ClientTransport.cpp
  src/Pipe-ClientTransport.h
  src/Pipe-ServerTransport.cpp
  src/Pipe-ServerTransport.h
  src/ServerConnection.cpp
  src/ServerConnection.h
  src/ServerConnectionSingle.cpp
  src/ServerConnectionSingle.h
  src/TCP-ClientTransport.cpp
  src/TCP-ClientTransport.h
  src/TCP-ServerTransport.cpp
  src/TCP-ServerTransport.h
  src/Transport.h
  src/UVClientTransport.cpp
  src/UVClientTransport.h
  src/UVServerTransport.cpp
  src/UVServerTransport.h
  src/UVTransportBase.cpp
  src/UVTransportBase.h
  include/nativeipc/ConnectionExports.h
  include/nativeipc/ConnectionFactory.h
  include/nativeipc/IConnection.h
  include/nativeipc/IServerConnection.h
  )

if(MSVC)
  target_sources(nativeipc PRIVATE $<IF:$<BOOL:${BUILD_SHARED_LIBS}>, src/platform/win/dllmain.cpp,>)
  
  target_compile_definitions(nativeipc PRIVATE
    $<IF:$<BOOL:${BUILD_SHARED_LIBS}>, NATIVEIPC_EXPORT , NATIVEIPC_STATIC_DEFINE>
    )
endif()

target_include_directories(nativeipc PUBLIC
  include/nativeipc
  )

target_link_libraries(nativeipc PRIVATE
  CONAN_PKG::libuv)

set_target_properties(nativeipc
  PROPERTIES COMPILE_PDB_NAME nativeipc)

install(TARGETS nativeipc
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  )

install(DIRECTORY include
  DESTINATION ${CMAKE_INSTALL_PREFIX}
  )

install(FILES ${CMAKE_BINARY_DIR}/bin/nativeipc.pdb
  CONFIGURATIONS Debug RelWithDebInfo
  DESTINATION lib
  OPTIONAL
  )
