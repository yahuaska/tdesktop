/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"

#include <QtCore/QTimer>

namespace Platform {

class MainWindow : public Window::MainWindow {
	Q_OBJECT

public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	void psFirstShow();

	virtual QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) = 0;

	static void LibsLoaded();

	~MainWindow();

public slots:
	void psShowTrayMenu();

	void psStatusIconCheck();
	void psUpdateIndicator();

protected:
	void unreadCounterChangedHook() override;

	bool hasTrayIcon() const override;

	void workmodeUpdated(DBIWorkMode mode) override;

	QSystemTrayIcon *trayIcon = nullptr;
	QMenu *trayIconMenu = nullptr;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();

	virtual void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) = 0;

private:
	void updateIconCounters();
	void psCreateTrayIcon();

	QTimer _psCheckStatusIconTimer;
	int _psCheckStatusIconLeft = 100;

	QTimer _psUpdateIndicatorTimer;
	crl::time _psLastIndicatorUpdate = 0;

};

} // namespace Platform
