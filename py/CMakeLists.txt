find_program(PYTHON python3 REQUIRED)

if (PYTHON)
  install(PROGRAMS seadsa.py DESTINATION bin)
  install(PROGRAMS seadsa-yaml.py DESTINATION bin)
endif()
