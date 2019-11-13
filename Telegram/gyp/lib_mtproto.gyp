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
    'target_name': 'lib_mtproto',
    'includes': [
<<<<<<< HEAD
      'common/library.gypi',
      'modules/qt.gypi',
      'modules/pch.gypi',
      'modules/openssl.gypi',
=======
      'helpers/common/library.gypi',
      'helpers/modules/qt.gypi',
      'helpers/modules/pch.gypi',
      'helpers/modules/openssl.gypi',
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
      'pch_source': '<(src_loc)/mtproto/mtp_pch.cpp',
      'pch_header': '<(src_loc)/mtproto/mtp_pch.h',
    },
    'defines': [
    ],
    'dependencies': [
      'lib_scheme.gyp:lib_scheme',
    ],
    'export_dependent_settings': [
      'lib_scheme.gyp:lib_scheme',
    ],
    'include_dirs': [
      '<(src_loc)',
    ],
    'sources': [
      '<(src_loc)/mtproto/mtp_abstract_socket.cpp',
      '<(src_loc)/mtproto/mtp_abstract_socket.h',
      '<(src_loc)/mtproto/mtp_tcp_socket.cpp',
      '<(src_loc)/mtproto/mtp_tcp_socket.h',
      '<(src_loc)/mtproto/mtp_tls_socket.cpp',
      '<(src_loc)/mtproto/mtp_tls_socket.h',
    ],
  }],
}
