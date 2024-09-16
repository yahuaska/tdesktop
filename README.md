[![Build status](https://ci.appveyor.com/api/projects/status/y4ouiv0p9o168ht5/branch/build-dev?svg=true)](https://ci.appveyor.com/project/SlavikMIPT/tdesktop-9ooq7/branch/dev)
# Telegram Desktop MediaTube – Based on Official Messenger

## Floating Video

[![Preview of Floating Video][preview_image2]][preview_image2_url]

## Improved loading speeds.

[![Preview of Telegram Desktop][preview_image]][preview_image_url]

The source code is published under GPLv3 with OpenSSL exception, the license is available [here][license].

## Supported systems

* Windows XP - Windows 10 (**not** RT)
* Mac OS X 10.8 - Mac OS X 10.15
* Mac OS X 10.6 - Mac OS X 10.7 (separate build)
* Ubuntu 12.04 - Ubuntu 19.04
* Fedora 22 - Fedora 30
* [Snappy](https://snapcraft.io/telegram-desktop)
* [Flathub](https://flathub.org/apps/details/org.telegram.desktop)

## Third-party

* Qt 5.3.2 and 5.6.2, slightly patched ([LGPL](http://doc.qt.io/qt-5/lgpl.html))
* OpenSSL 1.0.1g ([OpenSSL License](https://www.openssl.org/source/license.html))
* zlib 1.2.8 ([zlib License](http://www.zlib.net/zlib_license.html))
* libexif 0.6.20 ([LGPL](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Google Breakpad ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
* Google Crashpad ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
* GYP ([BSD License](https://github.com/bnoordhuis/gyp/blob/master/LICENSE))
* Ninja ([Apache License 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING))
* OpenAL Soft ([LGPL](https://github.com/kcat/openal-soft/blob/master/COPYING))
* Opus codec ([BSD License](http://www.opus-codec.org/license/))
* FFmpeg ([LGPL](https://www.ffmpeg.org/legal.html))
* Guideline Support Library ([MIT License](https://github.com/Microsoft/GSL/blob/master/LICENSE))
* Mapbox Variant ([BSD License](https://github.com/mapbox/variant/blob/master/LICENSE))
* Range-v3 ([Boost License](https://github.com/ericniebler/range-v3/blob/master/LICENSE.txt))
* Open Sans font ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))
* Emoji alpha codes ([MIT License](https://github.com/emojione/emojione/blob/master/extras/alpha-codes/LICENSE.md))
* Catch test framework ([Boost License](https://github.com/philsquared/Catch/blob/master/LICENSE.txt))
* xxHash ([BSD License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE))

## Build instructions

* [Visual Studio 2019][msvc]
* [Xcode 10][xcode]
* [GYP/CMake on GNU/Linux][cmake]

[//]: # (LINKS)
[telegram]: https://telegram.org
[telegram_desktop]: https://desktop.telegram.org
[telegram_api]: https://core.telegram.org
[telegram_proto]: https://core.telegram.org/mtproto
[license]: LICENSE
[msvc]: docs/building-msvc.md
[xcode]: docs/building-xcode.md
[xcode_old]: docs/building-xcode-old.md
[cmake]: docs/building-cmake.md
[preview_image]: https://github.com/mediatube/tdesktop/blob/dev/docs/assets/preview.gif "Preview of Telegram Desktop"
[preview_image_url]: https://raw.githubusercontent.com/mediatube/tdesktop/dev/docs/assets/preview.gif
[preview_image2]: https://github.com/mediatube/tdesktop/blob/dev/docs/assets/preview_float.gif "Preview of Floating Video"
[preview_image2_url]: https://raw.githubusercontent.com/mediatube/tdesktop/dev/docs/assets/preview_float.gif
