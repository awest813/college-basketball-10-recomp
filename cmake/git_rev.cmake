# Runs at build time: cmake -DSRC=<repo> -DOUT=<header> -P git_rev.cmake
# Writes NCAABB10_GIT, the checkout's short commit (with -dirty for uncommitted changes), into
# OUT, and rewrites OUT only when that changes, so a build without a new commit recompiles nothing.
execute_process(
  COMMAND git -C "${SRC}" describe --always --dirty --abbrev=7 --exclude=*
  OUTPUT_VARIABLE rev
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE result)
if (NOT result EQUAL 0 OR rev STREQUAL "")
  set(rev "unknown")  # No git, or not a checkout (a source archive).
endif ()
set(text "#define NCAABB10_GIT \"${rev}\"\n")
set(old "")
if (EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif ()
if (NOT old STREQUAL text)
  file(WRITE "${OUT}" "${text}")
endif ()
