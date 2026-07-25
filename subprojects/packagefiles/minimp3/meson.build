# minimp3 is header-only: there is nothing to compile, only a directory to put
# on the include path. The implementation is pulled in by exactly one
# translation unit, which defines MINIMP3_IMPLEMENTATION.
project('minimp3', 'c', version: '0.1')

minimp3_dep = declare_dependency(
  include_directories: include_directories('.'),
)
