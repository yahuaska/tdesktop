/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"
#include "base/platform/win/base_windows_h.h"

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

inline void SetWatchingMediaKeys(bool watching) {
}

QString CurrentExecutablePath(int argc, char *argv[]);

inline void IgnoreApplicationActivationRightNow() {
}

namespace ThirdParty {

void start();

inline void finish() {
}

} // namespace ThirdParty
} // namespace Platform

inline QString psServerPrefix() {
	return qsl("Global\\");
}
inline void psCheckLocalSocket(const QString &) {
}

void psWriteDump();

void psDeleteDir(const QString &dir);

QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psAppDataPath();
QString psAppDataPathOld();
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();

int psCleanup();
int psFixPrevious();

void psNewVersion();

inline QByteArray psDownloadPathBookmark(const QString &path) {
	return QByteArray();
}
inline QByteArray psPathBookmark(const QString &path) {
	return QByteArray();
}
inline void psDownloadPathEnableAccess() {
}

class PsFileBookmark {
public:
	PsFileBookmark(const QByteArray &bookmark) {
	}
	bool check() const {
		return true;
	}
	bool enable() const {
		return true;
	}
	void disable() const {
	}
	const QString &name(const QString &original) const {
		return original;
	}
	QByteArray bookmark() const {
		return QByteArray();
	}

};

bool psLaunchMaps(const Data::LocationPoint &point);
