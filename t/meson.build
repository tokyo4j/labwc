test_lib = static_library(
  'test_lib',
  sources: files(
    '../src/common/buf.c',
    '../src/common/mem.c',
    '../src/common/string-helpers.c',
    '../src/common/xml.c',
    '../src/common/parse-bool.c',
  ),
  include_directories: [labwc_inc],
  dependencies: [
		dep_cmocka,
		glib,
		xml2,
		wlroots,
	],
)

tests = [
  'buf-simple',
  'str',
  'xml',
]

foreach t : tests
  test(
    'test_@0@'.format(t),
    executable(
      'test_@0@'.format(t),
      sources: '@0@.c'.format(t),
      include_directories: [labwc_inc],
      link_with: [test_lib],
			dependencies: [xml2],
    ),
    is_parallel: false,
  )
endforeach
