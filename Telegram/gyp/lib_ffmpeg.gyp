# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
<<<<<<< HEAD
    'common/common.gypi',
=======
    'helpers/common/common.gypi',
>>>>>>> pr
  ],
  'targets': [{
    'target_name': 'lib_ffmpeg',
    'includes': [
<<<<<<< HEAD
      'common/library.gypi',
      'modules/qt.gypi',
=======
      'helpers/common/library.gypi',
      'helpers/modules/qt.gypi',
>>>>>>> pr
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
<<<<<<< HEAD
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
=======
>>>>>>> pr
    },
    'dependencies': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
    ],
    'export_dependent_settings': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
    ],
    'defines': [
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(libs_loc)/ffmpeg',
    ],
    'direct_dependent_settings': {
      'include_dirs': [
        '<(src_loc)',
        '<(libs_loc)/ffmpeg',
      ],
    },
    'sources': [
      '<(src_loc)/ffmpeg/ffmpeg_utility.cpp',
      '<(src_loc)/ffmpeg/ffmpeg_utility.h',
    ],
    'conditions': [[ '"<(special_build_target)" != ""', {
      'defines': [
        'TDESKTOP_OFFICIAL_TARGET=<(special_build_target)',
      ],
    }]],
  }],
}
