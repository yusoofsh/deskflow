# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME AudioProtocolTests
  DEPENDS gui
  SOURCE AudioProtocolTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME AudioLinkTests
  DEPENDS gui
  SOURCE AudioLinkTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME AudioOutputTests
  DEPENDS gui
  SOURCE AudioOutputTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME AudioWidgetTests
  DEPENDS gui
  SOURCE AudioWidgetTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME StreamConverterTests
  DEPENDS gui
  SOURCE StreamConverterTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
