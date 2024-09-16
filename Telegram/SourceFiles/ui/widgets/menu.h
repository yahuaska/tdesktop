/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QMenu>

namespace Ui {

class ToggleView;
class RippleAnimation;

class Menu : public RpWidget {
public:
	Menu(QWidget *parent, const style::Menu &st = st::defaultMenu);
	Menu(QWidget *parent, QMenu *menu, const style::Menu &st = st::defaultMenu);
	~Menu();

	not_null<QAction*> addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	not_null<QAction*> addAction(const QString &text, Fn<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	not_null<QAction*> addSeparator();
	void clearActions();
	void finishAnimating();

	void clearSelection();

	enum class TriggeredSource {
		Mouse,
		Keyboard,
	};
	void setChildShown(bool shown) {
		_childShown = shown;
	}
	void setShowSource(TriggeredSource source);
	void setForceWidth(int forceWidth);

	const std::vector<not_null<QAction*>> &actions() const;

	void setResizedCallback(Fn<void()> callback) {
		_resizedCallback = std::move(callback);
	}

	void setActivatedCallback(Fn<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_activatedCallback = std::move(callback);
	}
	void setTriggeredCallback(Fn<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_triggeredCallback = std::move(callback);
	}

	void setKeyPressDelegate(Fn<bool(int key)> delegate) {
		_keyPressDelegate = std::move(delegate);
	}
	void handleKeyPress(int key);

	void setMouseMoveDelegate(Fn<void(QPoint globalPosition)> delegate) {
		_mouseMoveDelegate = std::move(delegate);
	}
	void handleMouseMove(QPoint globalPosition);

	void setMousePressDelegate(Fn<void(QPoint globalPosition)> delegate) {
		_mousePressDelegate = std::move(delegate);
	}
	void handleMousePress(QPoint globalPosition);

	void setMouseReleaseDelegate(Fn<void(QPoint globalPosition)> delegate) {
		_mouseReleaseDelegate = std::move(delegate);
	}
	void handleMouseRelease(QPoint globalPosition);

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct ActionData;

	void updateSelected(QPoint globalPosition);
	void actionChanged();
	void init();

	// Returns the new width.
	int processAction(not_null<QAction*> action, int index, int width);
	not_null<QAction*> addAction(not_null<QAction*> action, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);

	void setSelected(int selected);
	void setPressed(int pressed);
	void clearMouseSelection();

	int itemTop(int index);
	void updateItem(int index);
	void updateSelectedItem();
	void itemPressed(TriggeredSource source);
	void itemReleased(TriggeredSource source);

	const style::Menu &_st;

	Fn<void()> _resizedCallback;
	Fn<void(QAction *action, int actionTop, TriggeredSource source)> _activatedCallback;
	Fn<void(QAction *action, int actionTop, TriggeredSource source)> _triggeredCallback;
	Fn<bool(int key)> _keyPressDelegate;
	Fn<void(QPoint globalPosition)> _mouseMoveDelegate;
	Fn<void(QPoint globalPosition)> _mousePressDelegate;
	Fn<void(QPoint globalPosition)> _mouseReleaseDelegate;

	QMenu *_wappedMenu = nullptr;
	std::vector<not_null<QAction*>> _actions;
	std::vector<ActionData> _actionsData;

	int _forceWidth = 0;
	int _itemHeight, _separatorHeight;

	bool _mouseSelection = false;

	int _selected = -1;
	int _pressed = -1;
	bool _childShown = false;

};

} // namespace Ui
