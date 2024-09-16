/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/object_ptr.h"
#include "ui/rp_widget.h"
#include "ui/layers/layer_widget.h"

namespace Ui {
class IconButton;
class FlatLabel;
class Menu;
class UserpicButton;
class PopupMenu;
} // namespace Ui

namespace Window {

class SessionController;

class MainMenu : public Ui::LayerWidget, private base::Subscriber {
public:
	MainMenu(QWidget *parent, not_null<SessionController*> controller);

	void parentResized() override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override {
		setFocus();
	}

private:
	void updateControlsGeometry();
	void updatePhone();
	void initResetScaleButton();
	void refreshMenu();
	void refreshBackground();

	class ResetScaleButton;
	not_null<SessionController*> _controller;
	object_ptr<Ui::UserpicButton> _userpicButton = { nullptr };
	object_ptr<Ui::IconButton> _cloudButton = { nullptr };
	object_ptr<Ui::IconButton> _archiveButton = { nullptr };
	object_ptr<ResetScaleButton> _resetScaleButton = { nullptr };
	object_ptr<Ui::Menu> _menu;
	object_ptr<Ui::FlatLabel> _telegram;
	object_ptr<Ui::FlatLabel> _version;
	std::shared_ptr<QPointer<QAction>> _nightThemeAction;
	base::Timer _nightThemeSwitch;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;

	QString _phoneText;
	QImage _background;

};

} // namespace Window
