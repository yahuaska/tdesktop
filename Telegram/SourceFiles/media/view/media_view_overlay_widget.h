/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "data/data_shared_media.h"
#include "data/data_user_photos.h"
#include "data/data_web_page.h"
#include "data/data_cloud_themes.h" // Data::CloudTheme.
#include "media/view/media_view_playback_controls.h"

namespace Ui {
	class PopupMenu;
	class LinkButton;
	class RoundButton;
} // namespace Ui

namespace Window {
	namespace Theme {
		struct Preview;
	} // namespace Theme
} // namespace Window

namespace Notify {
	struct PeerUpdate;
} // namespace Notify

namespace Media {
	namespace Player {
		struct TrackState;
	} // namespace Player
	namespace Streaming {
		struct Information;
		struct Update;
		enum class Error;
	} // namespace Streaming
} // namespace Media

namespace Media {
	namespace View {

		class GroupThumbs;

#if defined Q_OS_MAC && !defined OS_MAC_OLD
#define USE_OPENGL_OVERLAY_WIDGET
#endif // Q_OS_MAC && !OS_MAC_OLD

#ifdef USE_OPENGL_OVERLAY_WIDGET
		using OverlayParent = Ui::RpWidgetWrap<QOpenGLWidget>;
#else // USE_OPENGL_OVERLAY_WIDGET
		using OverlayParent = Ui::RpWidget;
#endif // USE_OPENGL_OVERLAY_WIDGET

		class OverlayWidget final
			: public OverlayParent
			, private base::Subscriber
			, public ClickHandlerHost
			, private PlaybackControls::Delegate {
			Q_OBJECT

		public:
			OverlayWidget();

			void showPhoto(not_null<PhotoData*> photo, HistoryItem* context);
			void showPhoto(not_null<PhotoData*> photo, not_null<PeerData*> context);
			void showDocument(
				not_null<DocumentData*> document,
				HistoryItem* context);
			void showTheme(
				not_null<DocumentData*> document,
				const Data::CloudTheme& cloud);

			void leaveToChildEvent(QEvent* e, QWidget* child) override { // e -- from enterEvent() of child TWidget
				updateOverState(OverNone);
			}
			void enterFromChildEvent(QEvent* e, QWidget* child) override { // e -- from leaveEvent() of child TWidget
				updateOver(mapFromGlobal(QCursor::pos()));
			}

			void close();

			void activateControls();
			void onDocClick();

			PeerData* ui_getPeerForMouseAction();

			void notifyFileDialogShown(bool shown);

			void clearData();

			~OverlayWidget();

			// ClickHandlerHost interface
			void clickHandlerActiveChanged(const ClickHandlerPtr& p, bool active) override;
			void clickHandlerPressedChanged(const ClickHandlerPtr& p, bool pressed) override;

		private slots:
			void onHideControls(bool force = false);

			void onScreenResized(int screen);

			void onToMessage();
			void onSaveAs();
			void onDownload();
			void onSaveCancel();
			void onShowInFolder();
			void onForward();
			void onDelete();
			void onOverview();
			void onCopy();
			void onMenuDestroy(QObject* obj);
			void receiveMouse();
			void onAttachedStickers();

			void onDropdown();

			void onTouchTimer();

			void updateImage();

		private:
			struct Streamed;

			enum OverState {
				OverNone,
				OverLeftNav,
				OverRightNav,
				OverClose,
				OverHeader,
				OverName,
				OverDate,
				OverSave,
				OverMore,
				OverIcon,
				OverVideo,
			};
			struct Entity {
				base::optional_variant<
					not_null<PhotoData*>,
					not_null<DocumentData*>> data;
				HistoryItem* item;
			};

			void paintEvent(QPaintEvent* e) override;

			void keyPressEvent(QKeyEvent* e) override;
			void wheelEvent(QWheelEvent* e) override;
			void mousePressEvent(QMouseEvent* e) override;
			void mouseDoubleClickEvent(QMouseEvent* e) override;
			void mouseMoveEvent(QMouseEvent* e) override;
			void mouseReleaseEvent(QMouseEvent* e) override;
			void contextMenuEvent(QContextMenuEvent* e) override;
			void touchEvent(QTouchEvent* e);

			bool eventHook(QEvent* e) override;
			bool eventFilter(QObject* obj, QEvent* e) override;

			void setVisibleHook(bool visible) override;

			void playbackControlsPlay() override;
			void playbackControlsPause() override;
			void playbackControlsSeekProgress(crl::time position) override;
			void playbackControlsSeekFinished(crl::time position) override;
			void playbackControlsVolumeChanged(float64 volume) override;
			float64 playbackControlsCurrentVolume() override;
			void playbackControlsToFullScreen() override;
			void playbackControlsFromFullScreen() override;
			void playbackPauseResume();
			void playbackToggleFullScreen();
			void playbackPauseOnCall();
			void playbackResumeOnCall();
			void playbackPauseMusic();
			void playbackWaitingChange(bool waiting);

			void updateOver(QPoint mpos);
			void moveToScreen(bool force = false);
			bool moveToNext(int delta);
			void preloadData(int delta);

			Entity entityForUserPhotos(int index) const;
			Entity entityForSharedMedia(int index) const;
			Entity entityForCollage(int index) const;
			Entity entityByIndex(int index) const;
			Entity entityForItemId(const FullMsgId& itemId) const;
			bool moveToEntity(const Entity& entity, int preloadDelta = 0);
			void setContext(base::optional_variant<
				not_null<HistoryItem*>,
				not_null<PeerData*>> context);

			void refreshLang();
			void showSaveMsgFile();
			void updateMixerVideoVolume() const;

			struct SharedMedia;
			using SharedMediaType = SharedMediaWithLastSlice::Type;
			using SharedMediaKey = SharedMediaWithLastSlice::Key;
			std::optional<SharedMediaType> sharedMediaType() const;
			std::optional<SharedMediaKey> sharedMediaKey() const;
			std::optional<SharedMediaType> computeOverviewType() const;
			bool validSharedMedia() const;
			void validateSharedMedia();
			void handleSharedMediaUpdate(SharedMediaWithLastSlice&& update);

			struct UserPhotos;
			using UserPhotosKey = UserPhotosSlice::Key;
			std::optional<UserPhotosKey> userPhotosKey() const;
			bool validUserPhotos() const;
			void validateUserPhotos();
			void handleUserPhotosUpdate(UserPhotosSlice&& update);

			struct Collage;
			using CollageKey = WebPageCollage::Item;
			std::optional<CollageKey> collageKey() const;
			bool validCollage() const;
			void validateCollage();

			Data::FileOrigin fileOrigin() const;

			void refreshFromLabel(HistoryItem* item);
			void refreshCaption(HistoryItem* item);
			void refreshMediaViewer();
			void refreshNavVisibility();
			void refreshGroupThumbs();

			void dropdownHidden();
			void updateDocSize();
			void updateControls();
			void updateActions();
			void resizeCenteredControls();
			void resizeContentByScreenSize();

			void showDocument(
				not_null<DocumentData*> document,
				HistoryItem* context,
				const Data::CloudTheme& cloud);
			void displayPhoto(not_null<PhotoData*> photo, HistoryItem* item);
			void displayDocument(
				DocumentData* document,
				HistoryItem* item,
				const Data::CloudTheme& cloud = Data::CloudTheme());
			void displayFinished();
			void redisplayContent();
			void findCurrent();

			void updateCursor();
			void setZoomLevel(int newZoom);

			void updatePlaybackState();
			void restartAtSeekPosition(crl::time position);

			void refreshClipControllerGeometry();
			void refreshCaptionGeometry();

			void initStreaming();
			void initStreamingThumbnail();
			void streamingReady(Streaming::Information&& info);
			void createStreamingObjects();
			void handleStreamingUpdate(Streaming::Update&& update);
			void handleStreamingError(Streaming::Error&& error);
			void validateStreamedGoodThumbnail();

			void initThemePreview();
			void destroyThemePreview();
			void updateThemePreviewGeometry();

			void documentUpdated(DocumentData* doc);
			void changingMsgId(not_null<HistoryItem*> row, MsgId newId);

			QRect contentRect() const;
			void contentSizeChanged();

			// Radial animation interface.
			float64 radialProgress() const;
			bool radialLoading() const;
			QRect radialRect() const;
			void radialStart();
			crl::time radialTimeShift() const;

			void updateHeader();
			void snapXY();

			void clearControlsState();
			bool stateAnimationCallback(crl::time ms);
			bool radialAnimationCallback(crl::time now);
			void waitingAnimationCallback();
			bool updateControlsAnimation(crl::time now);

			void zoomIn();
			void zoomOut();
			void zoomReset();
			void zoomUpdate(int32& newZoom);

			void paintRadialLoading(Painter& p, bool radial, float64 radialOpacity);
			void paintRadialLoadingContent(
				Painter& p,
				QRect inner,
				bool radial,
				float64 radialOpacity) const;
			void paintThemePreview(Painter& p, QRect clip);

			void updateOverRect(OverState state);
			bool updateOverState(OverState newState);
			float64 overLevel(OverState control) const;

			void checkGroupThumbsAnimation();
			void initGroupThumbs();

			void validatePhotoImage(Image* image, bool blurred);
			void validatePhotoCurrentImage();

			[[nodiscard]] bool videoShown() const;
			[[nodiscard]] QSize videoSize() const;
			[[nodiscard]] bool videoIsGifv() const;
			[[nodiscard]] QImage videoFrame() const;
			[[nodiscard]] QImage videoFrameForDirectPaint() const;
			[[nodiscard]] QImage transformVideoFrame(QImage frame) const;
			[[nodiscard]] bool documentContentShown() const;
			[[nodiscard]] bool documentBubbleShown() const;
			void paintTransformedVideoFrame(Painter& p);
			void clearStreaming();

			QBrush _transparentBrush;

			PhotoData* _photo = nullptr;
			DocumentData* _doc = nullptr;
			std::unique_ptr<SharedMedia> _sharedMedia;
			std::optional<SharedMediaWithLastSlice> _sharedMediaData;
			std::optional<SharedMediaWithLastSlice::Key> _sharedMediaDataKey;
			std::unique_ptr<UserPhotos> _userPhotos;
			std::optional<UserPhotosSlice> _userPhotosData;
			std::unique_ptr<Collage> _collage;
			std::optional<WebPageCollage> _collageData;

			QRect _closeNav, _closeNavIcon;
			QRect _leftNav, _leftNavIcon, _rightNav, _rightNavIcon;
			QRect _headerNav, _nameNav, _dateNav;
			QRect _saveNav, _saveNavIcon, _moreNav, _moreNavIcon;
			bool _leftNavVisible = false;
			bool _rightNavVisible = false;
			bool _saveVisible = false;
			bool _headerHasLink = false;
			QString _dateText;
			QString _headerText;

			bool _streamingStartPaused = false;
			bool _fullScreenVideo = false;
			int _fullScreenZoomCache = 0;

			std::unique_ptr<GroupThumbs> _groupThumbs;
			QRect _groupThumbsRect;
			int _groupThumbsAvailableWidth = 0;
			int _groupThumbsLeft = 0;
			int _groupThumbsTop = 0;
			Ui::Text::String _caption;
			QRect _captionRect;

			int _width = 0;
			int _height = 0;
			int _x = 0, _y = 0, _w = 0, _h = 0;
			int _xStart = 0, _yStart = 0;
			int _zoom = 0; // < 0 - out, 0 - none, > 0 - in
			int _scale = 0;
			float64 _zoomToScreen = 0.; // for documents
			QPoint _mStart;
			bool _pressed = false;
			int32 _dragging = 0;
			int32 _scaling = 0;
			QPixmap _current;
			bool _blurred = true;

			std::unique_ptr<Streamed> _streamed;

			const style::icon* _docIcon = nullptr;
			style::color _docIconColor;
			QString _docName, _docSize, _docExt;
			int _docNameWidth = 0, _docSizeWidth = 0, _docExtWidth = 0;
			QRect _docRect, _docIconRect;
			int _docThumbx = 0, _docThumby = 0, _docThumbw = 0;
			object_ptr<Ui::LinkButton> _docDownload;
			object_ptr<Ui::LinkButton> _docSaveAs;
			object_ptr<Ui::LinkButton> _docCancel;

			QRect _photoRadialRect;
			Ui::RadialAnimation _radial;
			QImage _radialCache;

			History* _migrated = nullptr;
			History* _history = nullptr; // if conversation photos or files overview
			PeerData* _peer = nullptr;
			UserData* _user = nullptr; // if user profile photos overview

			// We save the information about the reason of the current mediaview show:
			// did we open a peer profile photo or a photo from some message.
			// We use it when trying to delete a photo: if we've opened a peer photo,
			// then we'll delete group photo instead of the corresponding message.
			bool _firstOpenedPeerPhoto = false;

			PeerData* _from = nullptr;
			QString _fromName;
			Ui::Text::String _fromNameLabel;

			std::optional<int> _index; // Index in current _sharedMedia data.
			std::optional<int> _fullIndex; // Index in full shared media.
			std::optional<int> _fullCount;
			FullMsgId _msgid;
			bool _canForwardItem = false;
			bool _canDeleteItem = false;

			mtpRequestId _loadRequest = 0;

			OverState _over = OverNone;
			OverState _down = OverNone;
			QPoint _lastAction, _lastMouseMovePos, _windowPos;
			bool _ignoringDropdown = false;

			Ui::Animations::Basic _stateAnimation;

			enum ControlsState {
				ControlsShowing,
				ControlsShown,
				ControlsHiding,
				ControlsHidden,
			};
			ControlsState _controlsState = ControlsShown;
			crl::time _controlsAnimStarted = 0;
			QTimer _controlsHideTimer;
			anim::value _controlsOpacity;
			bool _mousePressed = false;

			Ui::PopupMenu* _menu = nullptr;
			object_ptr<Ui::DropdownMenu> _dropdown;
			object_ptr<QTimer> _dropdownShowTimer;

			struct ActionData {
				QString text;
				const char* member;
			};
			QList<ActionData> _actions;

			bool _receiveMouse = true;

			bool _touchPress = false;
			bool _touchMove = false;
			bool _touchRightButton = false;
			QTimer _touchTimer;
			QPoint _touchStart;
			QPoint _accumScroll;

			QString _saveMsgFilename;
			crl::time _saveMsgStarted = 0;
			anim::value _saveMsgOpacity;
			QRect _saveMsg;
			QTimer _saveMsgUpdater;
			Ui::Text::String _saveMsgText;

			base::flat_map<OverState, crl::time> _animations;
			base::flat_map<OverState, anim::value> _animationOpacities;

			int _verticalWheelDelta = 0;

			bool _themePreviewShown = false;
			uint64 _themePreviewId = 0;
			QRect _themePreviewRect;
			std::unique_ptr<Window::Theme::Preview> _themePreview;
			object_ptr<Ui::RoundButton> _themeApply = { nullptr };
			object_ptr<Ui::RoundButton> _themeCancel = { nullptr };
			object_ptr<Ui::RoundButton> _themeShare = { nullptr };
			Data::CloudTheme _themeCloudData;

			bool _wasRepainted = false;

		};

	} // namespace View
} // namespace Media
