/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/version.h"
#include "settings.h"

constexpr str_const AppNameOld = "Telegram Win (Unofficial)";
constexpr str_const AppName = "Telegram Desktop MediaTube";

constexpr str_const AppId = "{53F49750-6209-4FBF-9CA8-7A333C87D1ED}"; // used in updater.cpp and Setup.iss for Windows
constexpr str_const AppFile = "Telegram";

enum {
	MaxSelectedItems = 100,

	MaxPhoneCodeLength = 4, // max length of country phone code
	MaxPhoneTailLength = 32, // rest of the phone number, without country code (seen 12 at least), need more for service numbers

	LocalEncryptIterCount = 4000, // key derivation iteration count
	LocalEncryptNoPwdIterCount = 4, // key derivation iteration count without pwd (not secure anyway)
	LocalEncryptSaltSize = 32, // 256 bit

	AnimationTimerDelta = 7,
	ClipThreadsCount = 8,
	AverageGifSize = 320 * 240,
	WaitBeforeGifPause = 200, // wait 200ms for gif draw before pausing it
	RecentInlineBotsLimit = 10,

	AVBlockSize = 4096, // 4Kb for ffmpeg blocksize

	AutoSearchTimeout = 900, // 0.9 secs
	SearchPerPage = 50,
	SearchManyPerPage = 100,
	LinksOverviewPerPage = 12,
	MediaOverviewStartPerPage = 5,

	AudioVoiceMsgMaxLength = 100 * 60, // 100 minutes
	AudioVoiceMsgChannels = 2, // stereo

	StickerMaxSize = 2048, // 2048x2048 is a max image size for sticker

	MaxZoomLevel = 7, // x8
	ZoomToScreenLevel = 1024, // just constant

	PreloadHeightsCount = 3, // when 3 screens to scroll left make a preload request

	SearchPeopleLimit = 5,
	UsernameCheckTimeout = 200,

	MaxMessageSize = 4096,

	WebPageUserId = 701000,

	UpdateDelayConstPart = 8 * 3600, // 8 hour min time between update check requests
	UpdateDelayRandPart = 8 * 3600, // 8 hour max - min time between update check requests

	WrongPasscodeTimeout = 1500,

	ChoosePeerByDragTimeout = 1000, // 1 second mouse not moved to choose dialog when dragging a file
};

inline const char *cGUIDStr() {
#ifndef OS_MAC_STORE
	static const char *gGuidStr = "{87A94AB0-E370-4cde-98D3-ACC110C5967D}";
#else // OS_MAC_STORE
	static const char *gGuidStr = "{E51FB841-8C0B-4EF9-9E9E-5A0078567627}";
#endif // OS_MAC_STORE

	return gGuidStr;
}

struct BuiltInDc {
	int id;
	const char *ip;
	int port;
};

static const BuiltInDc _builtInDcs[] = {
	{ 1, "149.154.175.50", 443 },
	{ 2, "149.154.167.51", 443 },
	{ 3, "149.154.175.100", 443 },
	{ 4, "149.154.167.91", 443 },
	{ 5, "149.154.171.5", 443 }
};

static const BuiltInDc _builtInDcsIPv6[] = {
	{ 1, "2001:0b28:f23d:f001:0000:0000:0000:000a", 443 },
	{ 2, "2001:067c:04e8:f002:0000:0000:0000:000a", 443 },
	{ 3, "2001:0b28:f23d:f003:0000:0000:0000:000a", 443 },
	{ 4, "2001:067c:04e8:f004:0000:0000:0000:000a", 443 },
	{ 5, "2001:0b28:f23f:f005:0000:0000:0000:000a", 443 }
};

static const BuiltInDc _builtInTestDcs[] = {
	{ 1, "149.154.175.10", 443 },
	{ 2, "149.154.167.40", 443 },
	{ 3, "149.154.175.117", 443 }
};

static const BuiltInDc _builtInTestDcsIPv6[] = {
	{ 1, "2001:0b28:f23d:f001:0000:0000:0000:000e", 443 },
	{ 2, "2001:067c:04e8:f002:0000:0000:0000:000e", 443 },
	{ 3, "2001:0b28:f23d:f003:0000:0000:0000:000e", 443 }
};

inline const BuiltInDc *builtInDcs() {
	return cTestMode() ? _builtInTestDcs : _builtInDcs;
}

inline int builtInDcsCount() {
	return (cTestMode() ? sizeof(_builtInTestDcs) : sizeof(_builtInDcs)) / sizeof(BuiltInDc);
}

inline const BuiltInDc *builtInDcsIPv6() {
	return cTestMode() ? _builtInTestDcsIPv6 : _builtInDcsIPv6;
}

inline int builtInDcsCountIPv6() {
	return (cTestMode() ? sizeof(_builtInTestDcsIPv6) : sizeof(_builtInDcsIPv6)) / sizeof(BuiltInDc);
}

static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAMA4ViQrjkPZ9xj0lrer3r23JvxOnrtE8nI69XLGSr+sRERz9YnUptnU\n\
BZpkIfKaRcl6XzNJiN28cVwO1Ui5JSa814UAiDHzWUqCaXUiUEQ6NmNTneiGx2sQ\n\
+9PKKlb8mmr3BB9A45ZNwLT6G9AK3+qkZLHojeSA+m84/a6GP4svAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

static const char *UpdatesPublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBALWu9GGs0HED7KG7BM73CFZ6o0xufKBRQsdnq3lwA8nFQEvmdu+g/I1j\n\
0LQ+0IQO7GW4jAgzF/4+soPDb6uHQeNFrlVx1JS9DZGhhjZ5rf65yg11nTCIHZCG\n\
w/CVnbwQOw0g5GBwwFV3r0uTTvy44xx8XXxk+Qknu4eBCsmrAFNnAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

#if defined TDESKTOP_API_ID && defined TDESKTOP_API_HASH

#define TDESKTOP_API_HASH_TO_STRING_HELPER(V) #V
#define TDESKTOP_API_HASH_TO_STRING(V) TDESKTOP_API_HASH_TO_STRING_HELPER(V)

constexpr auto ApiId = TDESKTOP_API_ID;
constexpr auto ApiHash = TDESKTOP_API_HASH_TO_STRING(TDESKTOP_API_HASH);

#undef TDESKTOP_API_HASH_TO_STRING
#undef TDESKTOP_API_HASH_TO_STRING_HELPER

#else // TDESKTOP_API_ID && TDESKTOP_API_HASH

// To build your version of Telegram Desktop you're required to provide
// your own 'api_id' and 'api_hash' for the Telegram API access.
//
// How to obtain your 'api_id' and 'api_hash' is described here:
// https://core.telegram.org/api/obtaining_api_id
//
// If you're building the application not for deployment,
// but only for test purposes you can comment out the error below.
//
// This will allow you to use TEST ONLY 'api_id' and 'api_hash' which are
// very limited by the Telegram API server.
//
// Your users will start getting internal server errors on login
// if you deploy an app using those 'api_id' and 'api_hash'.

#error You are required to provide API_ID and API_HASH.

constexpr auto ApiId = 17349;
constexpr auto ApiHash = "344583e45741c457fe1862106095a5eb";

#endif // TDESKTOP_API_ID && TDESKTOP_API_HASH

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#error "Only little endian is supported!"
#endif // Q_BYTE_ORDER == Q_BIG_ENDIAN

#if (TDESKTOP_ALPHA_VERSION != 0)

// Private key for downloading closed alphas.
#include "../../../DesktopPrivate/alpha_private.h"

#else
static const char *AlphaPrivateKey = "";
#endif

extern QString gKeyFile;
inline const QString &cDataFile() {
	if (!gKeyFile.isEmpty()) return gKeyFile;
	static const QString res(qsl("data"));
	return res;
}

inline const QString &cTempDir() {
	static const QString res = cWorkingDir() + qsl("tdata/tdld/");
	return res;
}

inline const QRegularExpression &cRussianLetters() {
	static QRegularExpression regexp(QString::fromUtf8("[а-яА-ЯёЁ]"));
	return regexp;
}

inline const QStringList &cImgExtensions() {
	static QStringList result;
	if (result.isEmpty()) {
		result.reserve(4);
		result.push_back(qsl(".jpg"));
		result.push_back(qsl(".jpeg"));
		result.push_back(qsl(".png"));
		result.push_back(qsl(".gif"));
	}
	return result;
}

inline const QStringList &cExtensionsForCompress() {
	static QStringList result;
	if (result.isEmpty()) {
		result.push_back(qsl(".jpg"));
		result.push_back(qsl(".jpeg"));
		result.push_back(qsl(".png"));
	}
	return result;
}
