/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_key.h"
#include "window/section_widget.h"
#include "ui/effects/animations.h"
#include "ui/widgets/scroll_area.h"
#include "ui/special_buttons.h"
#include "api/api_single_message_search.h"

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class TopBarWidget;
} // namespace HistoryView

namespace Ui {
class IconButton;
class PopupMenu;
class DropdownMenu;
class FlatButton;
class FlatInput;
class CrossButton;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Window {
class SessionController;
class ConnectionState;
} // namespace Window

namespace Dialogs {

enum class Mode;
struct RowDescriptor;
class Row;
class FakeRow;
class Key;
struct ChosenRow;
class InnerWidget;
enum class SearchRequestType;

class Widget : public Window::AbstractSectionWidget, public RPCSender {
	Q_OBJECT

public:
	Widget(QWidget *parent, not_null<Window::SessionController*> controller);

	void updateDragInScroll(bool inScroll);

	void searchInChat(Key chat);
	void setInnerFocus();

	void refreshDialog(Key key);
	void removeDialog(Key key);
	void repaintDialogRow(Mode list, not_null<Row*> row);
	void repaintDialogRow(RowDescriptor row);

	void jumpToTop();

	void startWidthAnimation();
	void stopWidthAnimation();

	bool hasTopBarShadow() const {
		return true;
	}
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void showFast();

	void scrollToEntry(const RowDescriptor &entry);

	void searchMessages(const QString &query, Key inChat = {});
	void onSearchMore();

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e) override;
	QRect rectForFloatPlayer() const override;

	void notify_historyMuteUpdated(History *history);

	~Widget();

signals:
	void cancelled();

public slots:
	void onDraggingScrollDelta(int delta);

	void onListScroll();
	bool onCancelSearch();
	void onCancelSearchInChat();

	void onFilterCursorMoved(int from = -1, int to = -1);
	void onCompleteHashtag(QString tag);

	void onDialogMoved(int movedFrom, int movedTo);
	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

	void onChooseByDrag();

private slots:
	void onDraggingScrollTimer();

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	enum class ShowAnimation {
		External,
		Internal,
	};

	void animationCallback();
	void searchReceived(
		SearchRequestType type,
		const MTPmessages_Messages &result,
		mtpRequestId requestId);
	void peerSearchReceived(
		const MTPcontacts_Found &result,
		mtpRequestId requestId);
	void escape();

	void setupSupportMode();
	void setupConnectingWidget();
	bool searchForPeersRequired(const QString &query) const;
	void setSearchInChat(Key chat, UserData *from = nullptr);
	void showJumpToDate();
	void showSearchFrom();
	void showMainMenu();
	void clearSearchCache();
	void updateControlsVisibility(bool fast = false);
	void updateLockUnlockVisibility();
	void updateLoadMoreChatsVisibility();
	void updateJumpToDateVisibility(bool fast = false);
	void updateSearchFromVisibility(bool fast = false);
	void updateControlsGeometry();
	void refreshFolderTopBar();
	void updateForwardBar();
	void checkUpdateStatus();
	void changeOpenedFolder(Data::Folder *folder, anim::type animated);
	QPixmap grabForFolderSlideAnimation();
	void startSlideAnimation();

	void fullSearchRefreshOn(rpl::producer<> events);
	void applyFilterUpdate(bool force = false);
	void refreshLoadMoreButton(bool mayBlock, bool isBlocked);
	void loadMoreBlockedByDate();

	bool searchFailed(SearchRequestType type, const RPCError &error, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	void scrollToTop();
	void setupScrollUpButton();
	void updateScrollUpVisibility();
	void startScrollUpButtonAnimation(bool shown);
	void updateScrollUpPosition();

	bool _dragInScroll = false;
	bool _dragForward = false;
	QTimer _chooseByDragTimer;

	object_ptr<Ui::IconButton> _forwardCancel = { nullptr };
	object_ptr<Ui::RpWidget> _searchControls;
	object_ptr<HistoryView::TopBarWidget> _folderTopBar = { nullptr } ;
	object_ptr<Ui::IconButton> _mainMenuToggle;
	object_ptr<Ui::FlatInput> _filter;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _chooseFromUser;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _jumpToDate;
	object_ptr<Ui::CrossButton> _cancelSearch;
	object_ptr<Ui::IconButton> _lockUnlock;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	class BottomButton;
	object_ptr<BottomButton> _updateTelegram = { nullptr };
	object_ptr<BottomButton> _loadMoreChats = { nullptr };
	std::unique_ptr<Window::ConnectionState> _connecting;

	Ui::Animations::Simple _scrollToAnimation;
	Ui::Animations::Simple _a_show;
	Window::SlideDirection _showDirection = Window::SlideDirection();
	QPixmap _cacheUnder, _cacheOver;
	ShowAnimation _showAnimationType = ShowAnimation::External;

	Ui::Animations::Simple _scrollToTopShown;
	bool _scrollToTopIsShown = false;
	object_ptr<Ui::HistoryDownButton> _scrollToTop;

	Data::Folder *_openedFolder = nullptr;
	Key _searchInChat;
	History *_searchInMigrated = nullptr;
	UserData *_searchFromUser = nullptr;
	QString _lastFilterText;

	QTimer _searchTimer;

	QString _peerSearchQuery;
	bool _peerSearchFull = false;
	mtpRequestId _peerSearchRequest = 0;

	QString _searchQuery;
	UserData *_searchQueryFrom = nullptr;
	int32 _searchNextRate = 0;
	bool _searchFull = false;
	bool _searchFullMigrated = false;
	mtpRequestId _searchRequest = 0;

	QMap<QString, MTPmessages_Messages> _searchCache;
	Api::SingleMessageSearch _singleMessageSearch;
	QMap<mtpRequestId, QString> _searchQueries;
	QMap<QString, MTPcontacts_Found> _peerSearchCache;
	QMap<mtpRequestId, QString> _peerSearchQueries;

	QPixmap _widthAnimationCache;

	object_ptr<QTimer> _draggingScrollTimer = { nullptr };
	int _draggingScrollDelta = 0;

};

} // namespace Dialogs
