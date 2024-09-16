/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_controller.h"

#include "core/application.h"
#include "main/main_account.h"
#include "ui/layers/box_content.h"
#include "ui/layers/layer_widget.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "mainwindow.h"
#include "facades.h"
#include "app.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Window {

Controller::Controller(not_null<Main::Account*> account)
: _account(account)
, _widget(this) {
	_account->sessionValue(
	) | rpl::start_with_next([=](Main::Session *session) {
		_sessionController = session
			? std::make_unique<SessionController>(session, this)
			: nullptr;
		_widget.updateWindowIcon();
	}, _lifetime);

	_widget.init();
}

Controller::~Controller() {
	// We want to delete all widgets before the _sessionController.
	_widget.clearWidgets();
}

void Controller::firstShow() {
	_widget.firstShow();
	checkThemeEditor();
}

void Controller::checkThemeEditor() {
	using namespace Window::Theme;

	if (const auto editing = Background()->editingTheme()) {
		showRightColumn(Box<Editor>(this, *editing));
	}
}

void Controller::setupPasscodeLock() {
	_widget.setupPasscodeLock();
}

void Controller::clearPasscodeLock() {
	_widget.clearPasscodeLock();
}

void Controller::setupIntro() {
	_widget.setupIntro();
}

void Controller::setupMain() {
	_widget.setupMain();
}

void Controller::showSettings() {
	_widget.showSettings();
}

void Controller::showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated) {
	_widget.ui_showBox(std::move(content), options, animated);
}

void Controller::showRightColumn(object_ptr<TWidget> widget) {
	_widget.showRightColumn(std::move(widget));
}

void Controller::activate() {
	_widget.activate();
}

void Controller::reActivate() {
	_widget.reActivateWindow();
}

void Controller::updateIsActive(int timeout) {
	_widget.updateIsActive(timeout);
}

void Controller::minimize() {
	if (Global::WorkMode().value() == dbiwmTrayOnly) {
		_widget.minimizeToTray();
	} else {
		_widget.setWindowState(Qt::WindowMinimized);
	}
}

void Controller::close() {
	if (!_widget.hideNoQuit()) {
		_widget.close();
	}
}

QPoint Controller::getPointForCallPanelCenter() const {
	Expects(_widget.windowHandle() != nullptr);

	return _widget.isActive()
		? _widget.geometry().center()
		: _widget.windowHandle()->screen()->geometry().center();
}

void Controller::tempDirDelete(int task) {
	_widget.tempDirDelete(task);
}

} // namespace Window
