
project(
  'gtkegl',
  'c',
  version : '0.0.1',
  license : 'MIT',
  meson_version : '>= 0.47',
  default_options : ['c_std=gnu99', 'warning_level=2',]
)

add_project_arguments('-Wno-deprecated-declarations', language : 'c')

cc = meson.get_compiler('c')
math = cc.find_library('m')
gl = dependency('gl')
glesv2 = dependency('glesv2')
egl = dependency('egl')
gtk = dependency('gtk+-3.0')
gdkx = dependency('gdk-x11-3.0')
deps = [
  glesv2,
  gl,
  egl,
  gtk,
  gdkx,
  math,
]

executable('gtkegl', files('gtkegl.c'), dependencies : deps, install : false)
executable('gtkegles', files('gtkegles.c'), dependencies : deps, install : false)
executable('gtkegles_tex_rgba', files('gtkegles_tex_rgba.c', 'frame-512x512-RGBA.c'), dependencies : deps, install : false)
executable('gtkegles_tex_nv12', files('gtkegles_tex_nv12.c', 'frame-512x512-NV12.c'), dependencies : deps, install : false)
