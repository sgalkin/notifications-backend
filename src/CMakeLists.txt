add_executable(${CMAKE_PROJECT_NAME}
  main.cpp
  flags.cpp
  db.cpp
  ssl.cpp
  address.cpp
  feedback_client.cpp
  notification_client.cpp
  )

target_link_libraries(${CMAKE_PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})
#target_link_libraries(${CMAKE_PROJECT_NAME} -lproxygenhttpserver)
target_link_libraries(${CMAKE_PROJECT_NAME} -lproxygenlib)
target_link_libraries(${CMAKE_PROJECT_NAME} -lwangle)
target_link_libraries(${CMAKE_PROJECT_NAME} -lfolly)
target_link_libraries(${CMAKE_PROJECT_NAME} -lglog)
target_link_libraries(${CMAKE_PROJECT_NAME} -lgflags)
target_link_libraries(${CMAKE_PROJECT_NAME} -lleveldb)
target_link_libraries(${CMAKE_PROJECT_NAME} ${OPENSSL_LIBRARIES})
#target_link_libraries(${CMAKE_PROJECT_NAME} -lcrypto)

install(
  TARGETS ${CMAKE_PROJECT_NAME}
  RUNTIME DESTINATION bin
  )
