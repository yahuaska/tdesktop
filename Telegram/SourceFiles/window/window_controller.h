/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mainwindow.h"
#include "ui/layers/layer_widget.h"

namespace Main {
class Account;
} // namespace Main

namespace Window {

class Controller final {
public:
	explicit Controller(not_null<Main::Account*> account);
	~Controller();

	Controller(const Controller &other) = delete;
	Controller &operator=(const Controller &other) = delete;

	Main::Account &account() const {
		return *_account;
	}
	not_null<::MainWindow*> widget() {
		return &_widget;
	}
	SessionController *sessionController() const {
		return _sessionController.get();
	}

	void firstShow();

	void setupPasscodeLock();
	void clearPasscodeLock();
	void setupIntro();
	void setupMain();

	void showSettings();

	template <typename BoxType>
	QPointer<BoxType> show(
			object_ptr<BoxType> content,
			Ui::LayerOptions options = Ui::LayerOption::KeepOther,
			anim::type animated = anim::type::normal) {
		const auto result = QPointer<BoxType>(content.data());
		showBox(std::move(content), options, animated);
		return result;
	}

	void showRightColumn(object_ptr<TWidget> widget);

	void activate();
	void reActivate();
	void updateIsActive(int timeout);
	void minimize();
	void close();

	QPoint getPointForCallPanelCenter() const;

	void tempDirDelete(int task);

private:
	void showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated);
	void checkThemeEditor();

	not_null<Main::Account*> _account;
	::MainWindow _widget;
	std::unique_ptr<SessionController> _sessionController;

	rpl::lifetime _lifetime;

};

} // namespace Window
