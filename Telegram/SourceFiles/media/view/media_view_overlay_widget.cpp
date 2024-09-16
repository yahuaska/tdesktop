/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_widget.h"

#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/toast/toast.h"
#include "ui/text_options.h"
#include "ui/ui_utility.h"
#include "boxes/confirm_box.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_controls.h"
#include "media/view/media_view_group_thumbs.h"
#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_reader.h"
#include "media/player/media_player_instance.h"
#include "history/history.h"
#include "history/history_message.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_account.h" // Account::sessionValue.
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "observer_peer.h"
#include "main/main_session.h"
#include "layout.h"
#include "storage/file_download.h"
#include "calls/calls_instance.h"
#include "facades.h"
#include "app.h"
#include "styles/style_mediaview.h"
#include "styles/style_history.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtCore/QBuffer>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Media
{
	namespace View
	{
		namespace
		{
			constexpr auto kGoodThumbnailQuality = 87;
			constexpr auto kWaitingFastDuration = crl::time(200);
			constexpr auto kWaitingShowDuration = crl::time(500);
			constexpr auto kWaitingShowDelay = crl::time(500);
			constexpr auto kPreloadCount = 4;

			// macOS OpenGL renderer fails to render larger texture
			// even though it reports that max texture size is 16384.
			constexpr auto kMaxDisplayImageSize = 4096;

			// Preload X message ids before and after current.
			constexpr auto kIdsLimit = 48;

			// Preload next messages if we went further from current than that.
			constexpr auto kIdsPreloadAfter = 28;

			Images::Options VideoThumbOptions(not_null<DocumentData*> document)
			{
				const auto result = Images::Option::Smooth | Images::Option::Blurred;
				return (document && document->isVideoMessage())
					       ? (result | Images::Option::Circled)
					       : result;
			}

			void PaintImageProfile(QPainter& p, const QImage& image, QRect rect, QRect fill)
			{
				const auto argb = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
				const auto rgb = image.convertToFormat(QImage::Format_RGB32);
				const auto argbp = QPixmap::fromImage(argb);
				const auto rgbp = QPixmap::fromImage(rgb);
				const auto width = image.width();
				const auto height = image.height();
				const auto xcopies = (fill.width() + width - 1) / width;
				const auto ycopies = (fill.height() + height - 1) / height;
				const auto copies = xcopies * ycopies;
				auto times = QStringList();
				const auto bench = [&](QString label, auto&& paint)
				{
					const auto single = [&](QString label)
					{
						auto now = crl::now();
						const auto push = [&]
						{
							times.push_back(QString("%1").arg(crl::now() - now, 4, 10, QChar(' ')));
							now = crl::now();
						};
						paint(rect);
						push();
						{
							PainterHighQualityEnabler hq(p);
							paint(rect);
						}
						push();
						for (auto i = 0; i < xcopies; ++i)
						{
							for (auto j = 0; j < ycopies; ++j)
							{
								paint(QRect(
									fill.topLeft() + QPoint(i * width, j * height),
									QSize(width, height)));
							}
						}
						push();
						LOG(("FRAME (%1): %2 (copies: %3)").arg(label).arg(times.join(' ')).arg(copies));
						times = QStringList();
						now = crl::now();
					};
					p.setCompositionMode(QPainter::CompositionMode_Source);
					single(label + " S");
					p.setCompositionMode(QPainter::CompositionMode_SourceOver);
					single(label + " O");
				};
				bench("ARGB I", [&](QRect rect)
				{
					p.drawImage(rect, argb);
				});
				bench("RGB  I", [&](QRect rect)
				{
					p.drawImage(rect, rgb);
				});
				bench("ARGB P", [&](QRect rect)
				{
					p.drawPixmap(rect, argbp);
				});
				bench("RGB  P", [&](QRect rect)
				{
					p.drawPixmap(rect, rgbp);
				});
			}

			QPixmap PrepareStaticImage(const QString& path)
			{
				auto image = App::readImage(path, nullptr, false);
#if defined Q_OS_MAC && !defined OS_MAC_OLD
				if (image.width() > kMaxDisplayImageSize
					|| image.height() > kMaxDisplayImageSize) {
					image = image.scaled(
						kMaxDisplayImageSize,
						kMaxDisplayImageSize,
						Qt::KeepAspectRatio,
						Qt::SmoothTransformation);
				}
#endif // Q_OS_MAC && !OS_MAC_OLD
				return App::pixmapFromImageInPlace(std::move(image));
			}
		} // namespace

		struct OverlayWidget::SharedMedia
		{
			SharedMedia(SharedMediaKey key) : key(key)
			{
			}

			SharedMediaKey key;
			rpl::lifetime lifetime;
		};

		struct OverlayWidget::UserPhotos
		{
			UserPhotos(UserPhotosKey key) : key(key)
			{
			}

			UserPhotosKey key;
			rpl::lifetime lifetime;
		};

		struct OverlayWidget::Collage
		{
			Collage(CollageKey key) : key(key)
			{
			}

			CollageKey key;
		};

		struct OverlayWidget::Streamed
		{
			template <typename Callback>
			Streamed(
				not_null<Data::Session*> owner,
				std::shared_ptr<Streaming::Reader> reader,
				QWidget* controlsParent,
				not_null<Delegate*> controlsDelegate,
				Callback&& loadingCallback);

			Streaming::Player player;
			Streaming::Information info;
			PlaybackControls controls;

			bool waiting = false;
			Ui::InfiniteRadialAnimation radial;
			Ui::Animations::Simple fading;
			base::Timer timer;
			QImage frameForDirectPaint;

			bool withSound = false;
			bool pausedBySeek = false;
			bool resumeOnCallEnd = false;
		};

		template <typename Callback>
		OverlayWidget::Streamed::Streamed(
			not_null<Data::Session*> owner,
			std::shared_ptr<Streaming::Reader> reader,
			QWidget* controlsParent,
			not_null<Delegate*> controlsDelegate,
			Callback&& loadingCallback)
			: player(owner, std::move(reader))
			  , controls(controlsParent, controlsDelegate)
			  , radial(
				  std::forward<Callback>(loadingCallback),
				  st::mediaviewStreamingRadial)
		{
		}

		OverlayWidget::OverlayWidget()
			: OverlayParent(nullptr)
			  , _transparentBrush(style::transparentPlaceholderBrush())
			  , _docDownload(this, tr::lng_media_download(tr::now), st::mediaviewFileLink)
			  , _docSaveAs(this, tr::lng_mediaview_save_as(tr::now), st::mediaviewFileLink)
			  , _docCancel(this, tr::lng_cancel(tr::now), st::mediaviewFileLink)
			  , _radial([=](crl::time now) { return radialAnimationCallback(now); })
			  , _lastAction(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction)
			  , _stateAnimation([=](crl::time now) { return stateAnimationCallback(now); })
			  , _dropdown(this, st::mediaviewDropdownMenu)
			  , _dropdownShowTimer(this)
		{
			subscribe(Lang::Current().updated(), [this] { refreshLang(); });

			setWindowIcon(Window::CreateIcon(&Core::App().activeAccount()));
			setWindowTitle(qsl("Media viewer"));

			const auto text = tr::lng_mediaview_saved_to(
				tr::now,
				lt_downloads,
				Ui::Text::Link(
					tr::lng_mediaview_downloads(tr::now),
					"internal:show_saved_message"),
				Ui::Text::WithEntities);
			_saveMsgText.setMarkedText(st::mediaviewSaveMsgStyle, text, Ui::DialogTextOptions());
			_saveMsg = QRect(
				0, 0, _saveMsgText.maxWidth() + st::mediaviewSaveMsgPadding.left() + st::mediaviewSaveMsgPadding.
				right(),
				st::mediaviewSaveMsgStyle.font->height + st::mediaviewSaveMsgPadding.top() + st::mediaviewSaveMsgPadding
				.bottom());

			connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(onScreenResized(int)));

			// While we have one mediaview for all sessions we have to do this.
			Core::App().activeAccount().sessionValue(
			) | start_with_next([=](Main::Session* session)
			{
				if (session)
				{
					subscribe(session->downloaderTaskFinished(), [=]
					{
						if (!isHidden())
						{
							updateControls();
						}
					});
					subscribe(session->calls().currentCallChanged(), [=](Calls::Call* call)
					{
						if (!_streamed)
						{
							return;
						}
						if (call)
						{
							playbackPauseOnCall();
						}
						else
						{
							playbackResumeOnCall();
						}
					});
					subscribe(session->documentUpdated, [=](DocumentData* document)
					{
						if (!isHidden())
						{
							documentUpdated(document);
						}
					});
					subscribe(session->messageIdChanging, [=](std::pair<not_null<HistoryItem*>, MsgId> update)
					{
						changingMsgId(update.first, update.second);
					});
				}
				else
				{
					_sharedMedia = nullptr;
					_userPhotos = nullptr;
					_collage = nullptr;
				}
			}, lifetime());

#ifdef Q_OS_LINUX
			setWindowFlags(Qt::FramelessWindowHint | Qt::MaximizeUsingFullscreenGeometryHint);
#else // Q_OS_LINUX
			setWindowFlags(
				Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::CustomizeWindowHint | Qt::
				FramelessWindowHint);
			//setMaximumSize(854, 480);
			//QSizePolicy qsp(QSizePolicy::Preferred, QSizePolicy::Preferred);
			//qsp.setHeightForWidth(true);
			//setSizePolicy(qsp);
#endif // Q_OS_LINUX
			moveToScreen();
			setAttribute(Qt::WA_NoSystemBackground, true);
			setAttribute(Qt::WA_NativeWindow, true);
			setAttribute(Qt::WA_Resized, true);
			setAttribute(Qt::WA_Moved, true);
			setAttribute(Qt::WA_TranslucentBackground, true);

			setMouseTracking(true);
			_scale = 0;
			hide();
			createWinId();
			if constexpr (Platform::IsLinux())
			{
				windowHandle()->setTransientParent(App::wnd()->windowHandle());
				setWindowModality(Qt::WindowModal);
			}
			//if (!Platform::IsMac()) {
			//	setWindowState(Qt::WindowFullScreen);
			//}

			_saveMsgUpdater.setSingleShot(true);
			connect(&_saveMsgUpdater, SIGNAL(timeout()), this, SLOT(updateImage()));

			setAttribute(Qt::WA_AcceptTouchEvents);
			_touchTimer.setSingleShot(true);
			connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

			_controlsHideTimer.setSingleShot(true);
			connect(&_controlsHideTimer, SIGNAL(timeout()), this, SLOT(onHideControls()));

			_docDownload->addClickHandler([=] { onDownload(); });
			_docSaveAs->addClickHandler([=] { onSaveAs(); });
			_docCancel->addClickHandler([=] { onSaveCancel(); });

			_dropdown->setHiddenCallback([this] { dropdownHidden(); });
			_dropdownShowTimer->setSingleShot(true);
			connect(_dropdownShowTimer, SIGNAL(timeout()), this, SLOT(onDropdown()));
		}

		void OverlayWidget::refreshLang()
		{
			InvokeQueued(this, [this] { updateThemePreviewGeometry(); });
		}

		void OverlayWidget::moveToScreen(bool force)
		{
			const auto widgetScreen = [&](auto&& widget) -> QScreen*
			{
				if (auto handle = widget ? widget->windowHandle() : nullptr)
				{
					return handle->screen();
				}
				return nullptr;
			};
			const auto window = Core::App().activeWindow()
				                    ? Core::App().activeWindow()->widget().get()
				                    : nullptr;
			const auto activeWindowScreen = widgetScreen(window);
			const auto myScreen = widgetScreen(this);
			if (activeWindowScreen && myScreen && myScreen != activeWindowScreen)
			{
				windowHandle()->setScreen(activeWindowScreen);
			}

			const auto screen = activeWindowScreen
				                    ? activeWindowScreen
				                    : QApplication::primaryScreen();
			const auto available = screen->geometry();
			if (!force && geometry() == available)
			{
				return;
			}

			if (_photo)
			{
				setGeometry(available);
			}
			else
			{
				auto geometryFrameFloating = available;
				_w = qRound(available.width() / 2.5);
				_h = qRound(available.height() / 2.5);
				_x = qRound(available.width() / 10.);
				_y = qRound(available.height() / 10.);
				geometryFrameFloating.setWidth(_w);
				geometryFrameFloating.setHeight(_h);
				geometryFrameFloating.setX(_x);
				geometryFrameFloating.setY(_y);
				setGeometry(geometryFrameFloating);
			}

			auto navSkip = 2 * st::mediaviewControlMargin + st::mediaviewControlSize;
			_closeNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize,
			                      st::mediaviewControlMargin, st::mediaviewControlSize, st::mediaviewControlSize);
			_closeNavIcon = style::centerrect(_closeNav, st::mediaviewClose);
			_leftNav = myrtlrect(st::mediaviewControlMargin, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
			_leftNavIcon = style::centerrect(_leftNav, st::mediaviewLeft);
			_rightNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, navSkip,
			                      st::mediaviewControlSize, height() - 2 * navSkip);
			_rightNavIcon = style::centerrect(_rightNav, st::mediaviewRight);

			_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
			_photoRadialRect = QRect(
				QPoint((width() - st::radialSize.width()) / 2, (height() - st::radialSize.height()) / 2),
				st::radialSize);

			//resizeContentByScreenSize();
			snapXY();
			update();
		}

		bool OverlayWidget::videoShown() const
		{
			return _streamed && !_streamed->info.video.cover.isNull();
		}

		QSize OverlayWidget::videoSize() const
		{
			Expects(videoShown());

			return _streamed->info.video.size;
		}

		bool OverlayWidget::videoIsGifv() const
		{
			return _streamed && _doc->isAnimation() && !_doc->isVideoMessage();
		}

		QImage OverlayWidget::videoFrame() const
		{
			Expects(videoShown());

			auto request = Streaming::FrameRequest();
			//request.radius = (_doc && _doc->isVideoMessage())
			//	? ImageRoundRadius::Ellipse
			//	: ImageRoundRadius::None;
			return _streamed->player.ready()
				       ? _streamed->player.frame(request)
				       : _streamed->info.video.cover;
		}

		QImage OverlayWidget::videoFrameForDirectPaint() const
		{
			Expects(_streamed != nullptr);

			const auto result = videoFrame();

#ifdef USE_OPENGL_OVERLAY_WIDGET
			const auto bytesPerLine = result.bytesPerLine();
			if (bytesPerLine == result.width() * 4) {
				return result;
			}

			// On macOS 10.8+ we use QOpenGLWidget as OverlayWidget base class.
			// The OpenGL painter can't paint textures where byte data is with strides.
			// So in that case we prepare a compact copy of the frame to render.
			//
			// See Qt commit ed557c037847e343caa010562952b398f806adcd
			//
			auto& cache = _streamed->frameForDirectPaint;
			if (cache.size() != result.size()) {
				cache = QImage(result.size(), result.format());
			}
			const auto height = result.height();
			const auto line = cache.bytesPerLine();
			Assert(line == result.width() * 4);
			Assert(line < bytesPerLine);

			auto from = result.bits();
			auto to = cache.bits();
			for (auto y = 0; y != height; ++y) {
				memcpy(to, from, line);
				to += line;
				from += bytesPerLine;
			}
			return cache;
#endif // USE_OPENGL_OVERLAY_WIDGET

			return result;
		}

		bool OverlayWidget::documentContentShown() const
		{
			return _doc && (!_current.isNull() || videoShown());
		}

		bool OverlayWidget::documentBubbleShown() const
		{
			return (!_photo && !_doc)
				|| (_doc
					&& !_themePreviewShown
					&& !_streamed
					&& _current.isNull());
		}

		void OverlayWidget::clearStreaming()
		{
			_fullScreenVideo = false;
			_streamed = nullptr;
		}

		void OverlayWidget::documentUpdated(DocumentData* doc)
		{
			if (_doc && _doc == doc)
			{
				if (documentBubbleShown())
				{
					if ((_doc->loading() && _docCancel->isHidden()) || (!_doc->loading() && !_docCancel->isHidden()))
					{
						updateControls();
					}
					else if (_doc->loading())
					{
						updateDocSize();
						update(_docRect);
					}
				}
				else if (_streamed)
				{
					const auto ready = _doc->loaded()
						                   ? _doc->size
						                   : _doc->loading()
						                   ? std::clamp(_doc->loadOffset(), 0, _doc->size)
						                   : 0;
					_streamed->controls.setLoadingProgress(ready, _doc->size);
				}
			}
		}

		void OverlayWidget::changingMsgId(not_null<HistoryItem*> row, MsgId newId)
		{
			if (row->fullId() == _msgid)
			{
				_msgid = FullMsgId(_msgid.channel, newId);
				refreshMediaViewer();
			}
		}

		void OverlayWidget::updateDocSize()
		{
			if (!_doc || !documentBubbleShown()) return;

			if (_doc->loading())
			{
				quint64 ready = _doc->loadOffset(), total = _doc->size;
				QString readyStr, totalStr, mb;
				if (total >= 1024 * 1024)
				{
					// more than 1 mb
					qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
					readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
					totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
					mb = qsl("MB");
				}
				else if (total >= 1024)
				{
					qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
					readyStr = QString::number(readyKb);
					totalStr = QString::number(totalKb);
					mb = qsl("KB");
				}
				else
				{
					readyStr = QString::number(ready);
					totalStr = QString::number(total);
					mb = qsl("B");
				}
				_docSize = tr::lng_media_save_progress(tr::now, lt_ready, readyStr, lt_total, totalStr, lt_mb, mb);
			}
			else
			{
				_docSize = formatSizeText(_doc->size);
			}
			_docSizeWidth = st::mediaviewFont->width(_docSize);
			int32 maxw = st::mediaviewFileSize.width() - st::mediaviewFileIconSize - st::mediaviewFilePadding * 3;
			if (_docSizeWidth > maxw)
			{
				_docSize = st::mediaviewFont->elided(_docSize, maxw);
				_docSizeWidth = st::mediaviewFont->width(_docSize);
			}
		}

		void OverlayWidget::refreshNavVisibility()
		{
			if (_sharedMediaData)
			{
				_leftNavVisible = _index && (*_index > 0);
				_rightNavVisible = _index && (*_index + 1 < _sharedMediaData->size());
			}
			else if (_userPhotosData)
			{
				_leftNavVisible = _index && (*_index > 0);
				_rightNavVisible = _index && (*_index + 1 < _userPhotosData->size());
			}
			else if (_collageData)
			{
				_leftNavVisible = _index && (*_index > 0);
				_rightNavVisible = _index && (*_index + 1 < _collageData->items.size());
			}
			else
			{
				_leftNavVisible = false;
				_rightNavVisible = false;
			}
		}

		void OverlayWidget::updateControls()
		{
			if (_doc && documentBubbleShown())
			{
				if (_doc->loading())
				{
					_docDownload->hide();
					_docSaveAs->hide();
					_docCancel->moveToLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize,
					                       _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
					_docCancel->show();
				}
				else
				{
					if (_doc->loaded(DocumentData::FilePathResolve::Checked))
					{
						_docDownload->hide();
						_docSaveAs->moveToLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize,
						                       _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
						_docSaveAs->show();
						_docCancel->hide();
					}
					else
					{
						_docDownload->moveToLeft(
							_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize,
							_docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
						_docDownload->show();
						_docSaveAs->moveToLeft(
							_docRect.x() + 2.5 * st::mediaviewFilePadding + st::mediaviewFileIconSize + _docDownload->
							width(), _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
						_docSaveAs->show();
						_docCancel->hide();
					}
				}
				updateDocSize();
			}
			else
			{
				_docDownload->hide();
				_docSaveAs->hide();
				_docCancel->hide();
			}
			radialStart();

			updateThemePreviewGeometry();

			_saveVisible = (_photo && _photo->loaded())
				|| (_doc
					&& _doc->filepath(DocumentData::FilePathResolve::Checked).isEmpty()
					&& !_doc->loading());
			_saveNav = myrtlrect(width() - st::mediaviewIconSize.width() * 2, height() - st::mediaviewIconSize.height(),
			                     st::mediaviewIconSize.width(), st::mediaviewIconSize.height());
			_saveNavIcon = style::centerrect(_saveNav, st::mediaviewSave);
			_moreNav = myrtlrect(width() - st::mediaviewIconSize.width(), height() - st::mediaviewIconSize.height(),
			                     st::mediaviewIconSize.width(), st::mediaviewIconSize.height());
			_moreNavIcon = style::centerrect(_moreNav, st::mediaviewMore);

			const auto dNow = QDateTime::currentDateTime();
			const auto d = [&]
			{
				if (const auto item = Auth().data().message(_msgid))
				{
					return ItemDateTime(item);
				}
				if (_photo)
				{
					return base::unixtime::parse(_photo->date);
				}
				if (_doc)
				{
					return base::unixtime::parse(_doc->date);
				}
				return dNow;
			}();
			if (d.date() == dNow.date())
			{
				_dateText = tr::lng_mediaview_today(tr::now, lt_time, d.time().toString(cTimeFormat()));
			}
			else if (d.date().addDays(1) == dNow.date())
			{
				_dateText = tr::lng_mediaview_yesterday(tr::now, lt_time, d.time().toString(cTimeFormat()));
			}
			else
			{
				_dateText = tr::lng_mediaview_date_time(tr::now, lt_date, d.date().toString(qsl("dd.MM.yy")), lt_time,
				                                        d.time().toString(cTimeFormat()));
			}
			if (!_fromName.isEmpty())
			{
				_fromNameLabel.setText(st::mediaviewTextStyle, _fromName, Ui::NameTextOptions());
				_nameNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewTextTop,
				                     qMin(_fromNameLabel.maxWidth(), width() / 3), st::mediaviewFont->height);
				_dateNav = myrtlrect(st::mediaviewTextLeft + _nameNav.width() + st::mediaviewTextSkip,
				                     height() - st::mediaviewTextTop, st::mediaviewFont->width(_dateText),
				                     st::mediaviewFont->height);
			}
			else
			{
				_nameNav = QRect();
				_dateNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewTextTop,
				                     st::mediaviewFont->width(_dateText), st::mediaviewFont->height);
			}
			updateHeader();
			refreshNavVisibility();
			resizeCenteredControls();

			updateOver(mapFromGlobal(QCursor::pos()));
			update();
		}

		void OverlayWidget::resizeCenteredControls()
		{
			const auto bottomSkip = std::max(
					_dateNav.left() + _dateNav.width(),
					_headerNav.left() + _headerNav.width())
				+ st::mediaviewCaptionMargin.width();
			_groupThumbsAvailableWidth = std::max(
				width() - 2 * bottomSkip,
				st::msgMinWidth
				+ st::mediaviewCaptionPadding.left()
				+ st::mediaviewCaptionPadding.right());
			_groupThumbsLeft = (width() - _groupThumbsAvailableWidth) / 2;
			refreshGroupThumbs();
			_groupThumbsTop = _groupThumbs ? (height() - _groupThumbs->height()) : 0;

			refreshClipControllerGeometry();
			refreshCaptionGeometry();
		}

		void OverlayWidget::refreshCaptionGeometry()
		{
			if (_caption.isEmpty())
			{
				_captionRect = QRect();
				return;
			}

			if (_groupThumbs && _groupThumbs->hiding())
			{
				_groupThumbs = nullptr;
				_groupThumbsRect = QRect();
			}
			const auto captionBottom = (_streamed && !videoIsGifv())
				                           ? (_streamed->controls.y() - st::mediaviewCaptionMargin.height())
				                           : _groupThumbs
				                           ? _groupThumbsTop
				                           : height() - st::mediaviewCaptionMargin.height();
			const auto captionWidth = std::min(
				_groupThumbsAvailableWidth
				- st::mediaviewCaptionPadding.left()
				- st::mediaviewCaptionPadding.right(),
				_caption.maxWidth());
			const auto captionHeight = std::min(
				_caption.countHeight(captionWidth),
				height() / 4
				- st::mediaviewCaptionPadding.top()
				- st::mediaviewCaptionPadding.bottom()
				- 2 * st::mediaviewCaptionMargin.height());
			_captionRect = QRect(
				(width() - captionWidth) / 2,
				captionBottom
				- captionHeight
				- st::mediaviewCaptionPadding.bottom(),
				captionWidth,
				captionHeight);
		}

		void OverlayWidget::updateActions()
		{
			_actions.clear();

			if (_doc && _doc->loading())
			{
				_actions.push_back({tr::lng_cancel(tr::now), SLOT(onSaveCancel())});
			}
			if (IsServerMsgId(_msgid.msg))
			{
				_actions.push_back({tr::lng_context_to_msg(tr::now), SLOT(onToMessage())});
			}
			if (_doc && !_doc->filepath(DocumentData::FilePathResolve::Checked).isEmpty())
			{
				_actions.push_back({
					Platform::IsMac()
						? tr::lng_context_show_in_finder(tr::now)
						: tr::lng_context_show_in_folder(tr::now), SLOT(onShowInFolder())
				});
			}
			if ((_doc && documentContentShown()) || (_photo && _photo->loaded()))
			{
				_actions.push_back({tr::lng_mediaview_copy(tr::now), SLOT(onCopy())});
			}
			if (_photo && _photo->hasSticker)
			{
				_actions.push_back({tr::lng_context_attached_stickers(tr::now), SLOT(onAttachedStickers())});
			}
			if (_canForwardItem)
			{
				_actions.push_back({tr::lng_mediaview_forward(tr::now), SLOT(onForward())});
			}
			auto canDelete = [&]
			{
				if (_canDeleteItem)
				{
					return true;
				}
				if (!_msgid && _photo && _user && _user == Auth().user())
				{
					return _userPhotosData && _fullIndex && _fullCount;
				}
				if (_photo && _photo->peer && _photo->peer->userpicPhotoId() == _photo->id)
				{
					if (auto chat = _photo->peer->asChat())
					{
						return chat->canEditInformation();
					}
					if (auto channel = _photo->peer->asChannel())
					{
						return channel->canEditInformation();
					}
				}
				return false;
			}();
			if (canDelete)
			{
				_actions.push_back({tr::lng_mediaview_delete(tr::now), SLOT(onDelete())});
			}
			_actions.push_back({tr::lng_mediaview_save_as(tr::now), SLOT(onSaveAs())});

			if (const auto overviewType = computeOverviewType())
			{
				_actions.push_back({
					_doc ? tr::lng_mediaview_files_all(tr::now) : tr::lng_mediaview_photos_all(tr::now),
					SLOT(onOverview())
				});
			}
		}

		auto OverlayWidget::computeOverviewType() const
		-> std::optional<SharedMediaType>
		{
			if (const auto mediaType = sharedMediaType())
			{
				if (const auto overviewType = SharedMediaOverviewType(*mediaType))
				{
					return overviewType;
				}
				if (mediaType == SharedMediaType::PhotoVideo)
				{
					if (_photo)
					{
						return SharedMediaOverviewType(SharedMediaType::Photo);
					}
					if (_doc)
					{
						return SharedMediaOverviewType(SharedMediaType::Video);
					}
				}
			}
			return std::nullopt;
		}

		bool OverlayWidget::stateAnimationCallback(crl::time now)
		{
			if (anim::Disabled())
			{
				now += st::mediaviewShowDuration + st::mediaviewHideDuration;
			}
			for (auto i = begin(_animations); i != end(_animations);)
			{
				const auto [state, started] = *i;
				updateOverRect(state);
				const auto dt = float64(now - started) / st::mediaviewFadeDuration;
				if (dt >= 1)
				{
					_animationOpacities.erase(state);
					i = _animations.erase(i);
				}
				else
				{
					_animationOpacities[state].update(dt, anim::linear);
					++i;
				}
			}
			return !_animations.empty() || updateControlsAnimation(now);
		}

		bool OverlayWidget::updateControlsAnimation(crl::time now)
		{
			if (_controlsState != ControlsShowing
				&& _controlsState != ControlsHiding)
			{
				return false;
			}
			const auto duration = (_controlsState == ControlsShowing)
				                      ? st::mediaviewShowDuration
				                      : st::mediaviewHideDuration;
			const auto dt = float64(now - _controlsAnimStarted)
				/ duration;
			if (dt >= 1)
			{
				_controlsOpacity.finish();
				_controlsState = (_controlsState == ControlsShowing)
					                 ? ControlsShown
					                 : ControlsHidden;
				updateCursor();
			}
			else
			{
				_controlsOpacity.update(dt, anim::linear);
			}
			const auto toUpdate = QRegion()
				+ (_over == OverLeftNav ? _leftNav : _leftNavIcon)
				+ (_over == OverRightNav ? _rightNav : _rightNavIcon)
				+ (_over == OverClose ? _closeNav : _closeNavIcon)
				+ _saveNavIcon
				+ _moreNavIcon
				+ _headerNav
				+ _nameNav
				+ _dateNav
				+ _captionRect.marginsAdded(st::mediaviewCaptionPadding)
				+ _groupThumbsRect;
			update(toUpdate);
			return (dt < 1);
		}

		void OverlayWidget::waitingAnimationCallback()
		{
			if (!anim::Disabled())
			{
				update(radialRect());
			}
		}

		void OverlayWidget::updateCursor()
		{
			setCursor(_controlsState == ControlsHidden
				          ? Qt::BlankCursor
				          : (_over == OverNone ? style::cur_default : style::cur_pointer));
		}

		QRect OverlayWidget::contentRect() const
		{
			return {_x, _y, _w, _h};
		}

		void OverlayWidget::contentSizeChanged()
		{
			_width = _w;
			_height = _h;
			resizeContentByScreenSize();
		}

		void OverlayWidget::resizeContentByScreenSize()
		{
			if (_w > 0 && _h > 0)
			{
				_zoomToScreen = float64(width()) / _w;
				if (_h * _zoomToScreen > height())
				{
					_zoomToScreen = float64(height()) / _h;
				}
				if (_zoomToScreen >= 1.)
				{
					_zoomToScreen -= 1.;
				}
				else
				{
					_zoomToScreen = 1. - (1. / _zoomToScreen);
				}
			}
			else
			{
				_zoomToScreen = 0;
			}
			if ((_w > width()) || (_h > height()) || _fullScreenVideo)
			{
				_zoom = ZoomToScreenLevel;
				if (_zoomToScreen >= 0)
				{
					_w = qRound(_w * (_zoomToScreen + 1));
					_h = qRound(_h * (_zoomToScreen + 1));
				}
				else
				{
					_w = qRound(_w / (-_zoomToScreen + 1));
					_h = qRound(_h / (-_zoomToScreen + 1));
				}
			}
			else
			{
				_zoom = 0;
				_w = _width;
				_h = _height;
			}
			_x = (width() - _w) / 2;
			_y = (height() - _h) / 2;
		}

		float64 OverlayWidget::radialProgress() const
		{
			if (_doc)
			{
				return _doc->progress();
			}
			if (_photo)
			{
				return _photo->large()->progress();
			}
			return 1.;
		}

		bool OverlayWidget::radialLoading() const
		{
			if (_doc)
			{
				return _doc->loading() && !_streamed;
			}
			if (_photo)
			{
				return _photo->large()->loading();
			}
			return false;
		}

		QRect OverlayWidget::radialRect() const
		{
			if (_photo)
			{
				return _photoRadialRect;
			}
			if (_doc)
			{
				return QRect(
					QPoint(
						_docIconRect.x() + ((_docIconRect.width() - st::radialSize.width()) / 2),
						_docIconRect.y() + ((_docIconRect.height() - st::radialSize.height()) / 2)),
					st::radialSize);
			}
			return QRect();
		}

		void OverlayWidget::radialStart()
		{
			if (radialLoading() && !_radial.animating())
			{
				_radial.start(radialProgress());
				if (auto shift = radialTimeShift())
				{
					_radial.update(radialProgress(), !radialLoading(), crl::now() + shift);
				}
			}
		}

		crl::time OverlayWidget::radialTimeShift() const
		{
			return _photo ? st::radialDuration : 0;
		}

		bool OverlayWidget::radialAnimationCallback(crl::time now)
		{
			if ((!_doc && !_photo) || _streamed)
			{
				return false;
			}
			const auto wasAnimating = _radial.animating();
			const auto updated = _radial.update(
				radialProgress(),
				!radialLoading(),
				now + radialTimeShift());
			if ((wasAnimating || _radial.animating())
				&& (!anim::Disabled() || updated))
			{
				update(radialRect());
			}
			const auto ready = _doc && _doc->loaded();
			const auto streamVideo = ready && _doc->canBePlayed();
			const auto tryOpenImage = ready && (_doc->size < App::kImageSizeLimit);
			if (ready && ((tryOpenImage && !_radial.animating()) || streamVideo))
			{
				_streamingStartPaused = false;
				if (streamVideo)
				{
					redisplayContent();
				}
				else
				{
					auto& location = _doc->location(true);
					if (location.accessEnable())
					{
						if (_doc->isTheme()
							|| QImageReader(location.name()).canRead())
						{
							redisplayContent();
						}
						location.accessDisable();
					}
				}
			}
			return true;
		}

		void OverlayWidget::zoomIn()
		{
			int32 newZoom = _zoom;
			if (newZoom == ZoomToScreenLevel)
			{
				if (qCeil(_zoomToScreen) <= MaxZoomLevel)
				{
					newZoom = qCeil(_zoomToScreen);
				}
			}
			else
			{
				if (newZoom < _zoomToScreen && (newZoom + 1 > _zoomToScreen || (_zoomToScreen > MaxZoomLevel && newZoom
					== MaxZoomLevel)))
				{
					newZoom = ZoomToScreenLevel;
				}
				else if (newZoom < MaxZoomLevel)
				{
					++newZoom;
				}
			}
			zoomUpdate(newZoom);
		}

		void OverlayWidget::zoomOut()
		{
			int32 newZoom = _zoom;
			if (newZoom == ZoomToScreenLevel)
			{
				if (qFloor(_zoomToScreen) >= -MaxZoomLevel)
				{
					newZoom = qFloor(_zoomToScreen);
				}
			}
			else
			{
				if (newZoom > _zoomToScreen && (newZoom - 1 < _zoomToScreen || (_zoomToScreen < -MaxZoomLevel && newZoom
					== -MaxZoomLevel)))
				{
					newZoom = ZoomToScreenLevel;
				}
				else if (newZoom > -MaxZoomLevel)
				{
					--newZoom;
				}
			}
			zoomUpdate(newZoom);
		}

		void OverlayWidget::zoomReset()
		{
			int32 newZoom = _zoom;
			if (_zoom == 0)
			{
				if (qFloor(_zoomToScreen) == qCeil(_zoomToScreen) && qRound(_zoomToScreen) >= -MaxZoomLevel &&
					qRound(_zoomToScreen) <= MaxZoomLevel)
				{
					newZoom = qRound(_zoomToScreen);
				}
				else
				{
					newZoom = ZoomToScreenLevel;
				}
			}
			else
			{
				newZoom = 0;
			}
			_x = -_width / 2;
			_y = -_height / 2;
			float64 z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
			if (z >= 0)
			{
				_x = qRound(_x * (z + 1));
				_y = qRound(_y * (z + 1));
			}
			else
			{
				_x = qRound(_x / (-z + 1));
				_y = qRound(_y / (-z + 1));
			}
			_x += width() / 2;
			_y += height() / 2;
			update();
			zoomUpdate(newZoom);
		}

		void OverlayWidget::zoomUpdate(int32& newZoom)
		{
			if (newZoom != ZoomToScreenLevel)
			{
				while ((newZoom < 0 && (-newZoom + 1) > _w) || (-newZoom + 1) > _h)
				{
					++newZoom;
				}
			}
			setZoomLevel(newZoom);
		}

		void OverlayWidget::clearData()
		{
			if (!isHidden())
			{
				hide();
			}
			if (!_animations.empty())
			{
				_animations.clear();
				_stateAnimation.stop();
			}
			if (!_animationOpacities.empty())
			{
				_animationOpacities.clear();
			}
			clearStreaming();
			delete _menu;
			_menu = nullptr;
			setContext(std::nullopt);
			_from = nullptr;
			_fromName = QString();
			_photo = nullptr;
			_doc = nullptr;
			_fullScreenVideo = false;
			_caption.clear();
		}

		OverlayWidget::~OverlayWidget()
		{
			delete base::take(_menu);
		}

		void OverlayWidget::clickHandlerActiveChanged(const ClickHandlerPtr& p, bool active)
		{
			setCursor((active || ClickHandler::getPressed()) ? style::cur_pointer : style::cur_default);
			update(QRegion(_saveMsg) + _captionRect);
		}

		void OverlayWidget::clickHandlerPressedChanged(const ClickHandlerPtr& p, bool pressed)
		{
			setCursor((pressed || ClickHandler::getActive()) ? style::cur_pointer : style::cur_default);
			update(QRegion(_saveMsg) + _captionRect);
		}

		void OverlayWidget::showSaveMsgFile()
		{
			File::ShowInFolder(_saveMsgFilename);
		}

		void OverlayWidget::updateMixerVideoVolume() const
		{
			if (_streamed)
			{
				Player::mixer()->setVideoVolume(Global::VideoVolume());
			}
		}

		void OverlayWidget::close()
		{
			_scale = 0;
			Core::App().hideMediaView();
		}

		void OverlayWidget::activateControls()
		{
			if (!_menu && !_mousePressed)
			{
				_controlsHideTimer.start(int(st::mediaviewWaitHide));
			}
			if (_fullScreenVideo)
			{
				if (_streamed)
				{
					_streamed->controls.showAnimated();
				}
			}
			if (_controlsState == ControlsHiding || _controlsState == ControlsHidden)
			{
				_controlsState = ControlsShowing;
				_controlsAnimStarted = crl::now();
				_controlsOpacity.start(1);
				if (!_stateAnimation.animating())
				{
					_stateAnimation.start();
				}
			}
		}

		void OverlayWidget::onHideControls(bool force)
		{
			if (!force)
			{
				if (!_dropdown->isHidden()
					|| _menu
					|| _mousePressed
					|| (_fullScreenVideo
						&& !videoIsGifv()
						&& _streamed->controls.geometry().contains(_lastMouseMovePos)))
				{
					return;
				}
			}
			if (_fullScreenVideo)
			{
				_streamed->controls.hideAnimated();
			}
			if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) return;

			_lastMouseMovePos = mapFromGlobal(QCursor::pos());
			_controlsState = ControlsHiding;
			_controlsAnimStarted = crl::now();
			_controlsOpacity.start(0);
			if (!_stateAnimation.animating())
			{
				_stateAnimation.start();
			}
		}

		void OverlayWidget::dropdownHidden()
		{
			setFocus();
			_ignoringDropdown = true;
			_lastMouseMovePos = mapFromGlobal(QCursor::pos());
			updateOver(_lastMouseMovePos);
			_ignoringDropdown = false;
			if (!_controlsHideTimer.isActive())
			{
				onHideControls(true);
			}
		}

		void OverlayWidget::onScreenResized(int screen)
		{
			if (isHidden())
			{
				return;
			}

			const auto screens = QApplication::screens();
			const auto changed = (screen >= 0 && screen < screens.size())
				                     ? screens[screen]
				                     : nullptr;
			if (!windowHandle()
				|| !windowHandle()->screen()
				|| !changed
				|| windowHandle()->screen() == changed)
			{
				moveToScreen();
			}
		}

		void OverlayWidget::onToMessage()
		{
			if (const auto item = Auth().data().message(_msgid))
			{
				close();
				Ui::showPeerHistoryAtItem(item);
			}
		}

		void OverlayWidget::notifyFileDialogShown(bool shown)
		{
			if (shown && isHidden())
			{
				return;
			}
			if (shown)
			{
				Ui::Platform::BringToBack(this);
			}
			else
			{
				Ui::Platform::ShowOverAll(this);
			}
		}

		void OverlayWidget::onSaveAs()
		{
			QString file;
			if (_doc)
			{
				const FileLocation& location(_doc->location(true));
				if (!_doc->data().isEmpty() || location.accessEnable())
				{
					QFileInfo alreadyInfo(location.name());
					QDir alreadyDir(alreadyInfo.dir());
					QString name = alreadyInfo.fileName(), filter;
					const auto mimeType = Core::MimeTypeForName(_doc->mimeString());
					QStringList p = mimeType.globPatterns();
					QString pattern = p.isEmpty() ? QString() : p.front();
					if (name.isEmpty())
					{
						name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
					}

					if (pattern.isEmpty())
					{
						filter = QString();
					}
					else
					{
						filter = mimeType.filterString() + qsl(";;") + FileDialog::AllFilesFilter();
					}

					Ui::Platform::BringToBack(this);
					file = FileNameForSave(tr::lng_save_file(tr::now), filter, qsl("doc"), name, true, alreadyDir);
					Ui::Platform::ShowOverAll(this);
					if (!file.isEmpty() && file != location.name())
					{
						if (_doc->data().isEmpty())
						{
							QFile(file).remove();
							QFile(location.name()).copy(file);
						}
						else
						{
							QFile f(file);
							f.open(QIODevice::WriteOnly);
							f.write(_doc->data());
						}
					}

					if (_doc->data().isEmpty()) location.accessDisable();
				}
				else
				{
					DocumentSaveClickHandler::Save(
						fileOrigin(),
						_doc,
						DocumentSaveClickHandler::Mode::ToNewFile);
					updateControls();
					updateOver(_lastMouseMovePos);
				}
			}
			else
			{
				if (!_photo || !_photo->loaded()) return;

				Ui::Platform::BringToBack(this);
				auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
				FileDialog::GetWritePath(
					this,
					tr::lng_save_photo(tr::now),
					filter,
					filedialogDefaultName(
						qsl("photo"),
						qsl(".jpg"),
						QString(),
						false,
						_photo->date),
					crl::guard(this, [=, photo = _photo](const QString& result)
					{
						if (!result.isEmpty() && _photo == photo && photo->loaded())
						{
							photo->large()->original().save(result, "JPG");
						}
						Ui::Platform::ShowOverAll(this);
					}), crl::guard(this, [=]
					{
						Ui::Platform::ShowOverAll(this);
					}));
			}
			activateWindow();
			QApplication::setActiveWindow(this);
			setFocus();
		}

		void OverlayWidget::onDocClick()
		{
			if (_doc->loading())
			{
				onSaveCancel();
			}
			else
			{
				DocumentOpenClickHandler::Open(
					fileOrigin(),
					_doc,
					Auth().data().message(_msgid));
				if (_doc->loading() && !_radial.animating())
				{
					_radial.start(_doc->progress());
				}
			}
		}

		PeerData* OverlayWidget::ui_getPeerForMouseAction()
		{
			return _history ? _history->peer.get() : nullptr;
		}

		void OverlayWidget::onDownload()
		{
			if (Global::AskDownloadPath())
			{
				return onSaveAs();
			}

			QString path;
			if (Global::DownloadPath().isEmpty())
			{
				path = File::DefaultDownloadPath();
			}
			else if (Global::DownloadPath() == qsl("tmp"))
			{
				path = cTempDir();
			}
			else
			{
				path = Global::DownloadPath();
			}
			QString toName;
			if (_doc)
			{
				const auto& location = _doc->location(true);
				if (location.accessEnable())
				{
					if (!QDir().exists(path)) QDir().mkpath(path);
					toName = filedialogNextFilename(
						_doc->filename(),
						location.name(),
						path);
					if (!toName.isEmpty() && toName != location.name())
					{
						QFile(toName).remove();
						if (!QFile(location.name()).copy(toName))
						{
							toName = QString();
						}
					}
					location.accessDisable();
				}
				else
				{
					if (_doc->filepath(DocumentData::FilePathResolve::Checked).isEmpty()
						&& !_doc->loading())
					{
						DocumentSaveClickHandler::Save(
							fileOrigin(),
							_doc,
							DocumentSaveClickHandler::Mode::ToFile);
						updateControls();
					}
					else
					{
						_saveVisible = false;
						update(_saveNav);
					}
					updateOver(_lastMouseMovePos);
				}
			}
			else
			{
				if (!_photo || !_photo->loaded())
				{
					_saveVisible = false;
					update(_saveNav);
				}
				else
				{
					if (!QDir().exists(path)) QDir().mkpath(path);
					toName = filedialogDefaultName(qsl("photo"), qsl(".jpg"), path);
					if (!_photo->large()->original().save(toName, "JPG"))
					{
						toName = QString();
					}
				}
			}
			if (!toName.isEmpty())
			{
				_saveMsgFilename = toName;
				_saveMsgStarted = crl::now();
				_saveMsgOpacity.start(1);
				updateImage();
			}
		}

		void OverlayWidget::onSaveCancel()
		{
			if (_doc && _doc->loading())
			{
				_doc->cancel();
				if (_doc->canBePlayed())
				{
					redisplayContent();
				}
			}
		}

		void OverlayWidget::onShowInFolder()
		{
			if (!_doc) return;

			auto filepath = _doc->filepath(DocumentData::FilePathResolve::Checked);
			if (!filepath.isEmpty())
			{
				File::ShowInFolder(filepath);
			}
		}

		void OverlayWidget::onForward()
		{
			auto item = Auth().data().message(_msgid);
			if (!item || !IsServerMsgId(item->id) || item->serviceMsg())
			{
				return;
			}

			close();
			ShowForwardMessagesBox(
				App::wnd()->sessionController(),
				{1, item->fullId()});
		}

		void OverlayWidget::onDelete()
		{
			close();
			const auto deletingPeerPhoto = [this]
			{
				if (!_msgid)
				{
					return true;
				}
				if (_photo && _history)
				{
					if (_history->peer->userpicPhotoId() == _photo->id)
					{
						return _firstOpenedPeerPhoto;
					}
				}
				return false;
			};

			if (deletingPeerPhoto())
			{
				App::main()->deletePhotoLayer(_photo);
			}
			else if (const auto item = Auth().data().message(_msgid))
			{
				const auto suggestModerateActions = true;
				Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
			}
		}

		void OverlayWidget::onOverview()
		{
			if (_menu) _menu->hideMenu(true);
			update();
			if (const auto overviewType = computeOverviewType())
			{
				close();
				SharedMediaShowOverview(*overviewType, _history);
			}
		}

		void OverlayWidget::onCopy()
		{
			_dropdown->hideAnimated(Ui::DropdownMenu::HideOption::IgnoreShow);
			if (_doc)
			{
				if (videoShown())
				{
					QGuiApplication::clipboard()->setImage(
						transformVideoFrame(videoFrame()));
				}
				else if (!_current.isNull())
				{
					QGuiApplication::clipboard()->setPixmap(_current);
				}
			}
			else if (_photo && _photo->loaded())
			{
				QGuiApplication::clipboard()->setPixmap(_photo->large()->pix(fileOrigin()));
			}
		}

		void OverlayWidget::onAttachedStickers()
		{
			close();
			Auth().api().requestAttachedStickerSets(_photo);
		}

		std::optional<OverlayWidget::SharedMediaType> OverlayWidget::sharedMediaType() const
		{
			using Type = SharedMediaType;
			if (const auto item = Auth().data().message(_msgid))
			{
				if (const auto media = item->media())
				{
					if (media->webpage())
					{
						return std::nullopt;
					}
				}
				if (_photo)
				{
					if (item->toHistoryMessage())
					{
						return Type::PhotoVideo;
					}
					return Type::ChatPhoto;
				}
				if (_doc)
				{
					if (_doc->isGifv())
					{
						return Type::GIF;
					}
					if (_doc->isVideoFile())
					{
						return Type::PhotoVideo;
					}
					return Type::File;
				}
			}
			return std::nullopt;
		}

		std::optional<OverlayWidget::SharedMediaKey> OverlayWidget::sharedMediaKey() const
		{
			if (!_msgid && _peer && !_user && _photo && _peer->userpicPhotoId() == _photo->id)
			{
				return SharedMediaKey{
					_history->peer->id,
					_migrated ? _migrated->peer->id : 0,
					SharedMediaType::ChatPhoto,
					_peer->userpicPhotoId()
				};
			}
			if (!IsServerMsgId(_msgid.msg))
			{
				return std::nullopt;
			}
			auto keyForType = [this](SharedMediaType type) -> SharedMediaKey
			{
				return {
					_history->peer->id,
					_migrated ? _migrated->peer->id : 0,
					type,
					(_msgid.channel == _history->channelId()) ? _msgid.msg : (_msgid.msg - ServerMaxMsgId)
				};
			};
			return
				sharedMediaType()
				| keyForType;
		}

		Data::FileOrigin OverlayWidget::fileOrigin() const
		{
			if (_msgid)
			{
				return _msgid;
			}
			if (_photo && _user)
			{
				return Data::FileOriginUserPhoto(_user->bareId(), _photo->id);
			}
			if (_photo && _peer && _peer->userpicPhotoId() == _photo->id)
			{
				return Data::FileOriginPeerPhoto(_peer->id);
			}
			return Data::FileOrigin();
		}

		bool OverlayWidget::validSharedMedia() const
		{
			if (auto key = sharedMediaKey())
			{
				if (!_sharedMedia)
				{
					return false;
				}
				using Key = SharedMediaWithLastSlice::Key;
				auto inSameDomain = [](const Key& a, const Key& b)
				{
					return (a.type == b.type)
						&& (a.peerId == b.peerId)
						&& (a.migratedPeerId == b.migratedPeerId);
				};
				auto countDistanceInData = [&](const Key& a, const Key& b)
				{
					return [&](const SharedMediaWithLastSlice& data)
					{
						return inSameDomain(a, b)
							       ? data.distance(a, b)
							       : std::optional<int>();
					};
				};

				if (key == _sharedMedia->key)
				{
					return true;
				}
				if (!_sharedMediaDataKey
					|| _sharedMedia->key != *_sharedMediaDataKey)
				{
					return false;
				}
				auto distance = _sharedMediaData
					| countDistanceInData(*key, _sharedMedia->key)
					| func::abs;
				if (distance)
				{
					return (*distance < kIdsPreloadAfter);
				}
			}
			return (_sharedMedia == nullptr);
		}

		void OverlayWidget::validateSharedMedia()
		{
			if (auto key = sharedMediaKey())
			{
				_sharedMedia = std::make_unique<SharedMedia>(*key);
				auto viewer = (key->type == SharedMediaType::ChatPhoto)
					              ? SharedMediaWithLastReversedViewer
					              : SharedMediaWithLastViewer;
				viewer(
					*key,
					kIdsLimit,
					kIdsLimit
				) | start_with_next([this](
				                    SharedMediaWithLastSlice&& update)
				                    {
					                    handleSharedMediaUpdate(std::move(update));
				                    }, _sharedMedia->lifetime);
			}
			else
			{
				_sharedMedia = nullptr;
				_sharedMediaData = std::nullopt;
				_sharedMediaDataKey = std::nullopt;
			}
		}

		void OverlayWidget::handleSharedMediaUpdate(SharedMediaWithLastSlice&& update)
		{
			if ((!_photo && !_doc) || !_sharedMedia)
			{
				_sharedMediaData = std::nullopt;
				_sharedMediaDataKey = std::nullopt;
			}
			else
			{
				_sharedMediaData = std::move(update);
				_sharedMediaDataKey = _sharedMedia->key;
			}
			findCurrent();
			updateControls();
			preloadData(0);
		}

		std::optional<OverlayWidget::UserPhotosKey> OverlayWidget::userPhotosKey() const
		{
			if (!_msgid && _user && _photo)
			{
				return UserPhotosKey{
					_user->bareId(),
					_photo->id
				};
			}
			return std::nullopt;
		}

		bool OverlayWidget::validUserPhotos() const
		{
			if (const auto key = userPhotosKey())
			{
				if (!_userPhotos)
				{
					return false;
				}
				const auto countDistanceInData = [](const auto& a, const auto& b)
				{
					return [&](const UserPhotosSlice& data)
					{
						return data.distance(a, b);
					};
				};

				const auto distance = (key == _userPhotos->key)
					                      ? 0
					                      : _userPhotosData
					                      | countDistanceInData(*key, _userPhotos->key)
					                      | func::abs;
				if (distance)
				{
					return (*distance < kIdsPreloadAfter);
				}
			}
			return (_userPhotos == nullptr);
		}

		void OverlayWidget::validateUserPhotos()
		{
			if (const auto key = userPhotosKey())
			{
				_userPhotos = std::make_unique<UserPhotos>(*key);
				UserPhotosReversedViewer(
					*key,
					kIdsLimit,
					kIdsLimit
				) | start_with_next([this](
				                    UserPhotosSlice&& update)
				                    {
					                    handleUserPhotosUpdate(std::move(update));
				                    }, _userPhotos->lifetime);
			}
			else
			{
				_userPhotos = nullptr;
				_userPhotosData = std::nullopt;
			}
		}

		void OverlayWidget::handleUserPhotosUpdate(UserPhotosSlice&& update)
		{
			if (!_photo || !_userPhotos)
			{
				_userPhotosData = std::nullopt;
			}
			else
			{
				_userPhotosData = std::move(update);
			}
			findCurrent();
			updateControls();
			preloadData(0);
		}

		std::optional<OverlayWidget::CollageKey> OverlayWidget::collageKey() const
		{
			if (const auto item = Auth().data().message(_msgid))
			{
				if (const auto media = item->media())
				{
					if (const auto page = media->webpage())
					{
						for (const auto& item : page->collage.items)
						{
							if (item == _photo || item == _doc)
							{
								return item;
							}
						}
					}
				}
			}
			return std::nullopt;
		}

		bool OverlayWidget::validCollage() const
		{
			if (const auto key = collageKey())
			{
				if (!_collage)
				{
					return false;
				}
				const auto countDistanceInData = [](const auto& a, const auto& b)
				{
					return [&](const WebPageCollage& data)
					{
						const auto i = ranges::find(data.items, a);
						const auto j = ranges::find(data.items, b);
						return (i != end(data.items) && j != end(data.items))
							       ? std::make_optional(i - j)
							       : std::nullopt;
					};
				};

				if (key == _collage->key)
				{
					return true;
				}
				if (_collageData)
				{
					const auto& items = _collageData->items;
					if (ranges::find(items, *key) != end(items)
						&& ranges::find(items, _collage->key) != end(items))
					{
						return true;
					}
				}
			}
			return (_collage == nullptr);
		}

		void OverlayWidget::validateCollage()
		{
			if (const auto key = collageKey())
			{
				_collage = std::make_unique<Collage>(*key);
				_collageData = WebPageCollage();
				if (const auto item = Auth().data().message(_msgid))
				{
					if (const auto media = item->media())
					{
						if (const auto page = media->webpage())
						{
							_collageData = page->collage;
						}
					}
				}
			}
			else
			{
				_collage = nullptr;
				_collageData = std::nullopt;
			}
		}

		void OverlayWidget::refreshMediaViewer()
		{
			if (!validSharedMedia())
			{
				validateSharedMedia();
			}
			if (!validUserPhotos())
			{
				validateUserPhotos();
			}
			if (!validCollage())
			{
				validateCollage();
			}
			findCurrent();
			updateControls();
			preloadData(0);
		}

		void OverlayWidget::refreshFromLabel(HistoryItem* item)
		{
			if (_msgid && item)
			{
				_from = item->senderOriginal();
				if (const auto info = item->hiddenForwardedInfo())
				{
					_fromName = info->name;
				}
				else
				{
					Assert(_from != nullptr);
					const auto from = _from->migrateTo() ? _from->migrateTo() : _from;
					_fromName = from->name;
				}
			}
			else
			{
				_from = _user;
				_fromName = _user ? _user->name : QString();
			}
		}

		void OverlayWidget::refreshCaption(HistoryItem* item)
		{
			_caption = Ui::Text::String();
			if (!item)
			{
				return;
			}
			if (const auto media = item->media())
			{
				if (media->webpage())
				{
					return;
				}
			}
			const auto caption = item->originalText();
			if (caption.text.isEmpty())
			{
				return;
			}
			const auto asBot = [&]
			{
				if (const auto author = item->author()->asUser())
				{
					return author->isBot();
				}
				return false;
			}();
			_caption = Ui::Text::String(st::msgMinWidth);
			_caption.setMarkedText(
				st::mediaviewCaptionStyle,
				caption,
				Ui::ItemTextOptions(item));
		}

		void OverlayWidget::refreshGroupThumbs()
		{
			const auto existed = (_groupThumbs != nullptr);
			if (_index && _sharedMediaData)
			{
				GroupThumbs::Refresh(
					_groupThumbs,
					*_sharedMediaData,
					*_index,
					_groupThumbsAvailableWidth);
			}
			else if (_index && _userPhotosData)
			{
				GroupThumbs::Refresh(
					_groupThumbs,
					*_userPhotosData,
					*_index,
					_groupThumbsAvailableWidth);
			}
			else if (_index && _collageData)
			{
				GroupThumbs::Refresh(
					_groupThumbs,
					{_msgid, &*_collageData},
					*_index,
					_groupThumbsAvailableWidth);
			}
			else if (_groupThumbs)
			{
				_groupThumbs->clear();
				_groupThumbs->resizeToWidth(_groupThumbsAvailableWidth);
			}
			if (_groupThumbs && !existed)
			{
				initGroupThumbs();
			}
		}

		void OverlayWidget::initGroupThumbs()
		{
			Expects(_groupThumbs != nullptr);

			_groupThumbs->updateRequests(
			) | start_with_next([this](QRect rect)
			{
				const auto shift = (width() / 2);
				_groupThumbsRect = QRect(
					shift + rect.x(),
					_groupThumbsTop,
					rect.width(),
					_groupThumbs->height());
				update(_groupThumbsRect);
			}, _groupThumbs->lifetime());

			_groupThumbs->activateRequests(
			) | start_with_next([this](GroupThumbs::Key key)
			{
				using CollageKey = GroupThumbs::CollageKey;
				if (const auto photoId = base::get_if<PhotoId>(&key))
				{
					const auto photo = Auth().data().photo(*photoId);
					moveToEntity({photo, nullptr});
				}
				else if (const auto itemId = base::get_if<FullMsgId>(&key))
				{
					moveToEntity(entityForItemId(*itemId));
				}
				else if (const auto collageKey = base::get_if<CollageKey>(&key))
				{
					if (_collageData)
					{
						moveToEntity(entityForCollage(collageKey->index));
					}
				}
			}, _groupThumbs->lifetime());

			_groupThumbsRect = QRect(
				_groupThumbsLeft,
				_groupThumbsTop,
				width() - 2 * _groupThumbsLeft,
				height() - _groupThumbsTop);
		}

		void OverlayWidget::clearControlsState()
		{
			_saveMsgStarted = 0;
			_loadRequest = 0;
			_over = _down = OverNone;
			_pressed = false;
			_dragging = 0;
			setCursor(style::cur_default);
			if (!_animations.empty())
			{
				_animations.clear();
				_stateAnimation.stop();
			}
			if (!_animationOpacities.empty())
			{
				_animationOpacities.clear();
			}
		}

		void OverlayWidget::showPhoto(not_null<PhotoData*> photo, HistoryItem* context)
		{
			if (context)
			{
				setContext(context);
			}
			else
			{
				setContext(std::nullopt);
			}

			clearControlsState();
			_firstOpenedPeerPhoto = false;
			_photo = photo;

			refreshMediaViewer();

			displayPhoto(photo, context);
			preloadData(0);
			activateControls();
		}

		void OverlayWidget::showPhoto(not_null<PhotoData*> photo, not_null<PeerData*> context)
		{
			setContext(context);

			clearControlsState();
			_firstOpenedPeerPhoto = true;
			_photo = photo;

			refreshMediaViewer();

			displayPhoto(photo, nullptr);
			preloadData(0);
			activateControls();
		}

		void OverlayWidget::showDocument(not_null<DocumentData*> document, HistoryItem* context)
		{
			showDocument(document, context, Data::CloudTheme());
		}

		void OverlayWidget::showTheme(
			not_null<DocumentData*> document,
			const Data::CloudTheme& cloud)
		{
			showDocument(document, nullptr, cloud);
		}

		void OverlayWidget::showDocument(
			not_null<DocumentData*> document,
			HistoryItem* context,
			const Data::CloudTheme& cloud)
		{
			if (context)
			{
				setContext(context);
			}
			else
			{
				setContext(std::nullopt);
			}

			clearControlsState();
			_photo = nullptr;

			_streamingStartPaused = false;
			displayDocument(document, context, cloud);
			preloadData(0);
			activateControls();
		}

		void OverlayWidget::displayPhoto(not_null<PhotoData*> photo, HistoryItem* item)
		{
			if (photo->isNull())
			{
				displayDocument(nullptr, item);
				return;
			}
			if (isHidden())
			{
				moveToScreen();
			}

			clearStreaming();
			destroyThemePreview();
			_doc = nullptr;
			_fullScreenVideo = false;
			_photo = photo;
			_radial.stop();

			refreshMediaViewer();
			refreshCaption(item);

			_zoom = 0;
			_zoomToScreen = 0;
			Auth().downloader().clearPriorities();
			_blurred = true;
			_current = QPixmap();
			_down = OverNone;
			_w = style::ConvertScale(photo->width());
			_h = style::ConvertScale(photo->height());
			contentSizeChanged();
			refreshFromLabel(item);
			_photo->download(fileOrigin());
			displayFinished();
		}

		void OverlayWidget::destroyThemePreview()
		{
			_themePreviewId = 0;
			_themePreviewShown = false;
			_themePreview.reset();
			_themeApply.destroy();
			_themeCancel.destroy();
			_themeShare.destroy();
		}

		void OverlayWidget::redisplayContent()
		{
			if (isHidden())
			{
				return;
			}
			const auto item = Auth().data().message(_msgid);
			if (_photo)
			{
				displayPhoto(_photo, item);
			}
			else
			{
				displayDocument(_doc, item);
			}
		}

		// Empty messages shown as docs: doc can be nullptr.
		void OverlayWidget::displayDocument(
			DocumentData* doc,
			HistoryItem* item,
			const Data::CloudTheme& cloud)
		{
			if (isHidden())
			{
				moveToScreen(true);
			}
			_fullScreenVideo = false;
			_current = QPixmap();
			clearStreaming();
			destroyThemePreview();
			_doc = doc;
			_themeCloudData = cloud;
			_photo = nullptr;
			_radial.stop();

			refreshMediaViewer();
			refreshCaption(item);
			if (_doc)
			{
				if (_doc->sticker())
				{
					if (const auto image = _doc->getStickerLarge())
					{
						_current = image->pix(fileOrigin());
					}
					else if (_doc->hasThumbnail())
					{
						_current = _doc->thumbnail()->pixBlurred(
							fileOrigin(),
							_doc->dimensions.width(),
							_doc->dimensions.height());
					}
				}
				else
				{
					_doc->automaticLoad(fileOrigin(), item);

					if (_doc->canBePlayed())
					{
						initStreaming();
					}
					else if (_doc->isVideoFile())
					{
						initStreamingThumbnail();
					}
					else if (_doc->isTheme())
					{
						initThemePreview();
					}
					else
					{
						auto& location = _doc->location(true);
						if (location.accessEnable())
						{
							const auto& path = location.name();
							if (QImageReader(path).canRead())
							{
								_current = PrepareStaticImage(path);
							}
						}
						location.accessDisable();
					}
				}
			}

			_docIconRect = QRect((width() - st::mediaviewFileIconSize) / 2, (height() - st::mediaviewFileIconSize) / 2,
			                     st::mediaviewFileIconSize, st::mediaviewFileIconSize);
			if (documentBubbleShown())
			{
				if (!_doc || !_doc->hasThumbnail())
				{
					int32 colorIndex = documentColorIndex(_doc, _docExt);
					_docIconColor = documentColor(colorIndex);
					const style::icon* (thumbs[]) = {
						&st::mediaviewFileBlue, &st::mediaviewFileGreen, &st::mediaviewFileRed, &st::mediaviewFileYellow
					};
					_docIcon = thumbs[colorIndex];

					int32 extmaxw = (st::mediaviewFileIconSize - st::mediaviewFileExtPadding * 2);
					_docExtWidth = st::mediaviewFileExtFont->width(_docExt);
					if (_docExtWidth > extmaxw)
					{
						_docExt = st::mediaviewFileExtFont->elided(_docExt, extmaxw, Qt::ElideMiddle);
						_docExtWidth = st::mediaviewFileExtFont->width(_docExt);
					}
				}
				else
				{
					_doc->loadThumbnail(fileOrigin());
					int32 tw = _doc->thumbnail()->width(), th = _doc->thumbnail()->height();
					if (!tw || !th)
					{
						_docThumbx = _docThumby = _docThumbw = 0;
					}
					else if (tw > th)
					{
						_docThumbw = (tw * st::mediaviewFileIconSize) / th;
						_docThumbx = (_docThumbw - st::mediaviewFileIconSize) / 2;
						_docThumby = 0;
					}
					else
					{
						_docThumbw = st::mediaviewFileIconSize;
						_docThumbx = 0;
						_docThumby = ((th * _docThumbw) / tw - st::mediaviewFileIconSize) / 2;
					}
				}

				int32 maxw = st::mediaviewFileSize.width() - st::mediaviewFileIconSize - st::mediaviewFilePadding * 3;

				if (_doc)
				{
					_docName = (_doc->type == StickerDocument)
						           ? tr::lng_in_dlg_sticker(tr::now)
						           : (_doc->type == AnimatedDocument
							              ? qsl("GIF")
							              : (_doc->filename().isEmpty()
								                 ? tr::lng_mediaview_doc_image(tr::now)
								                 : _doc->filename()));
				}
				else
				{
					_docName = tr::lng_message_empty(tr::now);
				}
				_docNameWidth = st::mediaviewFileNameFont->width(_docName);
				if (_docNameWidth > maxw)
				{
					_docName = st::mediaviewFileNameFont->elided(_docName, maxw, Qt::ElideMiddle);
					_docNameWidth = st::mediaviewFileNameFont->width(_docName);
				}

				// _docSize is updated in updateControls()

				_docRect = QRect((width() - st::mediaviewFileSize.width()) / 2,
				                 (height() - st::mediaviewFileSize.height()) / 2, st::mediaviewFileSize.width(),
				                 st::mediaviewFileSize.height());
				_docIconRect = myrtlrect(_docRect.x() + st::mediaviewFilePadding,
				                         _docRect.y() + st::mediaviewFilePadding, st::mediaviewFileIconSize,
				                         st::mediaviewFileIconSize);
			}
			else if (_themePreviewShown)
			{
				updateThemePreviewGeometry();
			}
			else if (!_current.isNull())
			{
				_current.setDevicePixelRatio(cRetinaFactor());
				_w = style::ConvertScale(_current.width());
				_h = style::ConvertScale(_current.height());
			}
			else if (videoShown())
			{
				const auto contentSize = style::ConvertScale(videoSize());
				_w = contentSize.width();
				_h = contentSize.height();
			}
			contentSizeChanged();
			refreshFromLabel(item);
			_blurred = false;
			displayFinished();
		}

		void OverlayWidget::updateThemePreviewGeometry()
		{
			if (_themePreviewShown)
			{
				auto previewRect = QRect((width() - st::themePreviewSize.width()) / 2,
				                         (height() - st::themePreviewSize.height()) / 2, st::themePreviewSize.width(),
				                         st::themePreviewSize.height());
				_themePreviewRect = previewRect.marginsAdded(st::themePreviewMargin);
				if (_themeApply)
				{
					auto right = qMax(width() - _themePreviewRect.x() - _themePreviewRect.width(), 0) + st::
						themePreviewMargin.right();
					auto bottom = qMin(height(), _themePreviewRect.y() + _themePreviewRect.height());
					_themeApply->moveToRight(
						right,
						bottom - st::themePreviewMargin.bottom() + (st::themePreviewMargin.bottom() - _themeApply->
							height()) / 2);
					right += _themeApply->width() + st::themePreviewButtonsSkip;
					_themeCancel->moveToRight(right, _themeApply->y());
					if (_themeShare)
					{
						_themeShare->moveToLeft(previewRect.x(), _themeApply->y());
					}
				}

				// For context menu event.
				_x = _themePreviewRect.x();
				_y = _themePreviewRect.y();
				_w = _themePreviewRect.width();
				_h = _themePreviewRect.height();
			}
		}

		void OverlayWidget::displayFinished()
		{
			updateControls();
			if (isHidden())
			{
				Ui::Platform::UpdateOverlayed(this);
#ifdef Q_OS_LINUX
				showFullScreen();
#else // Q_OS_LINUX
				show();
#endif // Q_OS_LINUX
				Ui::Platform::ShowOverAll(this, true);
				activateWindow();
				QApplication::setActiveWindow(this);
				setFocus();
			}
		}

		void OverlayWidget::initStreaming()
		{
			Expects(_doc != nullptr);
			Expects(_doc->canBePlayed());

			if (_streamed)
			{
				return;
			}
			initStreamingThumbnail();
			createStreamingObjects();

			Core::App().updateNonIdle();

			_streamed->player.updates(
			) | start_with_next_error([=](Streaming::Update&& update)
			                          {
				                          handleStreamingUpdate(std::move(update));
			                          }, [=](Streaming::Error&& error)
			                          {
				                          handleStreamingError(std::move(error));
			                          }, _streamed->player.lifetime());

			_streamed->player.fullInCache(
			) | start_with_next([=](bool fullInCache)
			{
				_doc->setLoadedInMediaCache(fullInCache);
			}, _streamed->player.lifetime());

			restartAtSeekPosition(0);
		}

		void OverlayWidget::initStreamingThumbnail()
		{
			Expects(_doc != nullptr);

			const auto good = _doc->goodThumbnail();
			const auto useGood = (good && good->loaded());
			const auto thumb = _doc->thumbnail();
			const auto useThumb = (thumb && thumb->loaded());
			const auto blurred = _doc->thumbnailInline();
			if (good && !useGood)
			{
				good->load({});
			}
			else if (thumb && !useThumb)
			{
				thumb->load(fileOrigin());
			}
			const auto size = useGood ? good->size() : _doc->dimensions;
			if (!useGood && !thumb && !blurred)
			{
				return;
			}
			if (size.isEmpty())
			{
				return;
			}
			const auto w = size.width();
			const auto h = size.height();
			const auto options = VideoThumbOptions(_doc);
			const auto goodOptions = (options & ~Images::Option::Blurred);
			_current = (useGood
				            ? good
				            : useThumb
				            ? thumb
				            : blurred
				            ? blurred
				            : Image::BlankMedia().get())->pixNoCache(
				fileOrigin(),
				w,
				h,
				useGood ? goodOptions : options,
				w / cIntRetinaFactor(),
				h / cIntRetinaFactor());
			_current.setDevicePixelRatio(cRetinaFactor());
		}

		void OverlayWidget::streamingReady(Streaming::Information&& info)
		{
			_streamed->info = std::move(info);
			validateStreamedGoodThumbnail();
			if (videoShown())
			{
				const auto contentSize = style::ConvertScale(videoSize());
				if (contentSize != QSize(_width, _height))
				{
					update(contentRect());
					_w = contentSize.width();
					_h = contentSize.height();
					contentSizeChanged();
				}
			}
			this->update(contentRect());
			playbackWaitingChange(false);
		}

		void OverlayWidget::createStreamingObjects()
		{
			_streamed = std::make_unique<Streamed>(
				&_doc->owner(),
				_doc->owner().documentStreamedReader(_doc, fileOrigin()),
				this,
				static_cast<Delegate*>(this),
				[=] { waitingAnimationCallback(); });
			_streamed->withSound = _doc->isAudioFile()
				|| _doc->isVideoFile()
				|| _doc->isVoiceMessage()
				|| _doc->isVideoMessage();

			if (videoIsGifv())
			{
				_streamed->controls.hide();
			}
			else
			{
				refreshClipControllerGeometry();
				_streamed->controls.show();
			}
		}

		QImage OverlayWidget::transformVideoFrame(QImage frame) const
		{
			Expects(videoShown());

			if (_streamed->info.video.rotation != 0)
			{
				auto transform = QTransform();
				transform.rotate(_streamed->info.video.rotation);
				frame = frame.transformed(transform);
			}
			if (frame.size() != _streamed->info.video.size)
			{
				frame = frame.scaled(
					_streamed->info.video.size,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}
			return frame;
		}

		void OverlayWidget::validateStreamedGoodThumbnail()
		{
			Expects(_streamed != nullptr);
			Expects(_doc != nullptr);

			const auto good = _doc->goodThumbnail();
			if (!videoShown() || (good && good->loaded()) || _doc->uploading())
			{
				return;
			}
			auto image = transformVideoFrame(_streamed->info.video.cover);
			auto bytes = QByteArray();
			{
				auto buffer = QBuffer(&bytes);
				image.save(&buffer, "JPG", kGoodThumbnailQuality);
			}
			const auto length = bytes.size();
			if (!length || length > Storage::kMaxFileInMemory)
			{
				LOG(("App Error: Bad thumbnail data for saving to cache."));
			}
			else if (_doc->uploading())
			{
				_doc->setGoodThumbnailOnUpload(
					std::move(image),
					std::move(bytes));
			}
			else
			{
				_doc->owner().cache().putIfEmpty(
					_doc->goodThumbnailCacheKey(),
					Storage::Cache::Database::TaggedValue(
						std::move(bytes),
						Data::kImageCacheTag));
				_doc->refreshGoodThumbnail();
			}
		}

		void OverlayWidget::handleStreamingUpdate(Streaming::Update&& update)
		{
			using namespace Streaming;

			update.data.match([&](Information& update)
			                  {
				                  streamingReady(std::move(update));
			                  }, [&](const PreloadedVideo& update)
			                  {
				                  _streamed->info.video.state.receivedTill = update.till;
				                  updatePlaybackState();
			                  }, [&](const UpdateVideo& update)
			                  {
				                  _streamed->info.video.state.position = update.position;
				                  this->update(contentRect());
				                  Core::App().updateNonIdle();
				                  updatePlaybackState();
			                  }, [&](const PreloadedAudio& update)
			                  {
				                  _streamed->info.audio.state.receivedTill = update.till;
				                  updatePlaybackState();
			                  }, [&](const UpdateAudio& update)
			                  {
				                  _streamed->info.audio.state.position = update.position;
				                  updatePlaybackState();
			                  }, [&](const WaitingForData& update)
			                  {
				                  playbackWaitingChange(update.waiting);
			                  }, [&](MutedByOther)
			                  {
			                  }, [&](Finished)
			                  {
				                  const auto finishTrack = [](TrackState& state)
				                  {
					                  state.position = state.receivedTill = state.duration;
				                  };
				                  finishTrack(_streamed->info.audio.state);
				                  finishTrack(_streamed->info.video.state);
				                  updatePlaybackState();
			                  });
		}

		void OverlayWidget::handleStreamingError(Streaming::Error&& error)
		{
			if (error == Streaming::Error::NotStreamable)
			{
				_doc->setNotSupportsStreaming();
			}
			else if (error == Streaming::Error::OpenFailed)
			{
				_doc->setInappPlaybackFailed();
			}
			if (!_doc->canBePlayed())
			{
				redisplayContent();
			}
			else
			{
				playbackWaitingChange(false);
				updatePlaybackState();
			}
		}

		void OverlayWidget::playbackWaitingChange(bool waiting)
		{
			Expects(_streamed != nullptr);

			if (_streamed->waiting == waiting)
			{
				return;
			}
			_streamed->waiting = waiting;
			const auto fade = [=](crl::time duration)
			{
				if (!_streamed->radial.animating())
				{
					_streamed->radial.start(
						st::defaultInfiniteRadialAnimation.sineDuration);
				}
				_streamed->fading.start(
					[=] { update(radialRect()); },
					_streamed->waiting ? 0. : 1.,
					_streamed->waiting ? 1. : 0.,
					duration);
			};
			if (waiting)
			{
				if (_streamed->radial.animating())
				{
					_streamed->timer.cancel();
					fade(kWaitingFastDuration);
				}
				else
				{
					_streamed->timer.callOnce(kWaitingShowDelay);
					_streamed->timer.setCallback([=]
					{
						fade(kWaitingShowDuration);
					});
				}
			}
			else
			{
				_streamed->timer.cancel();
				if (_streamed->radial.animating())
				{
					fade(kWaitingFastDuration);
				}
			}
		}

		void OverlayWidget::initThemePreview()
		{
			using namespace Window::Theme;

			Assert(_doc && _doc->isTheme());

			const auto bytes = _doc->data();
			auto& location = _doc->location();
			if (bytes.isEmpty()
				&& (location.isEmpty() || !location.accessEnable()))
			{
				return;
			}
			_themePreviewShown = true;

			auto current = CurrentData();
			current.backgroundId = Background()->id();
			current.backgroundImage = Background()->createCurrentImage();
			current.backgroundTiled = Background()->tile();

			const auto& cloudList = _doc->session().data().cloudThemes().list();
			const auto i = ranges::find(
				cloudList,
				_doc->id,
				&Data::CloudTheme::documentId);
			const auto cloud = (i != end(cloudList)) ? *i : Data::CloudTheme();
			const auto isTrusted = (cloud.documentId != 0);
			const auto fields = [&]
			{
				auto result = _themeCloudData.id ? _themeCloudData : cloud;
				if (!result.documentId)
				{
					result.documentId = _doc->id;
				}
				return result;
			}();

			const auto path = _doc->location().name();
			const auto id = _themePreviewId = rand_value<uint64>();
			const auto weak = MakeWeak(this);
			crl::async([=, data = std::move(current)]() mutable
			{
				auto preview = GeneratePreview(
					bytes,
					path,
					fields,
					std::move(data),
					PreviewType::Extended);
				crl::on_main(weak, [=, result = std::move(preview)]() mutable
				{
					if (id != _themePreviewId)
					{
						return;
					}
					_themePreviewId = 0;
					_themePreview = std::move(result);
					if (_themePreview)
					{
						_themeApply.create(
							this,
							tr::lng_theme_preview_apply(),
							st::themePreviewApplyButton);
						_themeApply->show();
						_themeApply->setClickedCallback([=]
						{
							const auto& object = Background()->themeObject();
							const auto currentlyIsCustom = !object.cloud.id
								&& !IsEmbeddedTheme(object.pathAbsolute);
							auto preview = std::move(_themePreview);
							close();
							Apply(std::move(preview));
							if (isTrusted && !currentlyIsCustom)
							{
								KeepApplied();
							}
						});
						_themeCancel.create(
							this,
							tr::lng_cancel(),
							st::themePreviewCancelButton);
						_themeCancel->show();
						_themeCancel->setClickedCallback([this] { close(); });
						if (const auto slug = _themeCloudData.slug; !slug.isEmpty())
						{
							_themeShare.create(
								this,
								tr::lng_theme_share(),
								st::themePreviewCancelButton);
							_themeShare->show();
							_themeShare->setClickedCallback([=]
							{
								QGuiApplication::clipboard()->setText(
									Core::App().createInternalLinkFull("addtheme/" + slug));
								auto config = Ui::Toast::Config();
								config.text = tr::lng_background_link_copied(tr::now);
								Show(this, config);
							});
						}
						else
						{
							_themeShare.destroy();
						}
						updateControls();
					}
					update();
				});
			});
			location.accessDisable();
		}

		void OverlayWidget::refreshClipControllerGeometry()
		{
			if (!_streamed || videoIsGifv())
			{
				return;
			}

			if (_groupThumbs && _groupThumbs->hiding())
			{
				_groupThumbs = nullptr;
				_groupThumbsRect = QRect();
			}
			const auto controllerBottom = _groupThumbs
				                              ? _groupThumbsTop
				                              : height();
			_streamed->controls.resize(st::mediaviewControllerSize);
			_streamed->controls.move(
				(width() - _streamed->controls.width()) / 2,
				controllerBottom - _streamed->controls.height() - st::mediaviewCaptionPadding.bottom() - st::
				mediaviewCaptionMargin.height());
			SendPendingMoveResizeEvents(&_streamed->controls);
		}

		void OverlayWidget::playbackControlsPlay()
		{
			playbackPauseResume();
		}

		void OverlayWidget::playbackControlsPause()
		{
			playbackPauseResume();
		}

		void OverlayWidget::playbackControlsToFullScreen()
		{
			playbackToggleFullScreen();
		}

		void OverlayWidget::playbackControlsFromFullScreen()
		{
			playbackToggleFullScreen();
		}

		void OverlayWidget::playbackPauseResume()
		{
			Expects(_streamed != nullptr);

			_streamed->resumeOnCallEnd = false;
			if (const auto item = Auth().data().message(_msgid))
			{
				if (_streamed->player.failed())
				{
					clearStreaming();
					initStreaming();
				}
				else if (_streamed->player.finished())
				{
					_streamingStartPaused = false;
					restartAtSeekPosition(0);
				}
				else if (_streamed->player.paused())
				{
					_streamed->player.resume();
					updatePlaybackState();
					playbackPauseMusic();
				}
				else
				{
					_streamed->player.pause();
					updatePlaybackState();
				}
			}
			else
			{
				clearStreaming();
				updateControls();
				update();
			}
		}

		void OverlayWidget::restartAtSeekPosition(crl::time position)
		{
			Expects(_streamed != nullptr);
			Expects(_doc != nullptr);

			if (videoShown())
			{
				_streamed->info.video.cover = videoFrame();
				_current = Images::PixmapFast(transformVideoFrame(videoFrame()));
				update(contentRect());
			}
			auto options = Streaming::PlaybackOptions();
			options.position = position;
			options.audioId = AudioMsgId(_doc, _msgid);
			if (!_streamed->withSound)
			{
				options.mode = Streaming::Mode::Video;
				options.loop = true;
			}
			_streamed->player.play(options);
			if (_streamingStartPaused)
			{
				_streamed->player.pause();
			}
			else
			{
				playbackPauseMusic();
			}
			_streamed->pausedBySeek = false;

			_streamed->info.audio.state.position
				= _streamed->info.video.state.position
				= position;
			updatePlaybackState();
			playbackWaitingChange(true);
		}

		void OverlayWidget::playbackControlsSeekProgress(crl::time position)
		{
			Expects(_streamed != nullptr);

			if (!_streamed->player.paused() && !_streamed->player.finished())
			{
				_streamed->pausedBySeek = true;
				playbackControlsPause();
			}
		}

		void OverlayWidget::playbackControlsSeekFinished(crl::time position)
		{
			Expects(_streamed != nullptr);

			_streamingStartPaused = !_streamed->pausedBySeek
				&& !_streamed->player.finished();
			restartAtSeekPosition(position);
		}

		void OverlayWidget::playbackControlsVolumeChanged(float64 volume)
		{
			Global::SetVideoVolume(volume);
			updateMixerVideoVolume();
			Global::RefVideoVolumeChanged().notify();
			Auth().saveSettingsDelayed();
		}

		float64 OverlayWidget::playbackControlsCurrentVolume()
		{
			return Global::VideoVolume();
		}

		void OverlayWidget::playbackToggleFullScreen()
		{
			Expects(_streamed != nullptr);

			if (!videoShown() || (videoIsGifv() && !_fullScreenVideo))
			{
				return;
			}
			_fullScreenVideo = !_fullScreenVideo;
			if (_fullScreenVideo)
			{
				_fullScreenZoomCache = _zoom;
				showFullScreen();
				resizeContentByScreenSize();
				zoomReset();
				//setZoomLevel(ZoomToScreenLevel);
			}
			else
			{
				showNormal();
				resizeContentByScreenSize();
				setZoomLevel(_fullScreenZoomCache);
				_streamed->controls.showAnimated();
			}

			_streamed->controls.setInFullScreen(_fullScreenVideo);
			updateControls();
			update();
		}

		void OverlayWidget::playbackPauseOnCall()
		{
			Expects(_streamed != nullptr);

			if (_streamed->player.finished() || _streamed->player.paused())
			{
				return;
			}
			_streamed->resumeOnCallEnd = true;
			_streamed->player.pause();
			updatePlaybackState();
		}

		void OverlayWidget::playbackResumeOnCall()
		{
			Expects(_streamed != nullptr);

			if (_streamed->resumeOnCallEnd)
			{
				_streamed->resumeOnCallEnd = false;
				_streamed->player.resume();
				updatePlaybackState();
				playbackPauseMusic();
			}
		}

		void OverlayWidget::playbackPauseMusic()
		{
			Expects(_streamed != nullptr);

			if (!_streamed->withSound)
			{
				return;
			}
			Player::instance()->pause(AudioMsgId::Type::Voice);
			Player::instance()->pause(AudioMsgId::Type::Song);
		}

		void OverlayWidget::updatePlaybackState()
		{
			Expects(_streamed != nullptr);

			if (videoIsGifv())
			{
				return;
			}
			const auto state = _streamed->player.prepareLegacyState();
			if (state.position != kTimeUnknown && state.length != kTimeUnknown)
			{
				_streamed->controls.updatePlayback(state);
			}
		}

		void OverlayWidget::validatePhotoImage(Image* image, bool blurred)
		{
			if (!image || !image->loaded())
			{
				if (!blurred)
				{
					image->load(fileOrigin());
				}
				return;
			}
			if (!_current.isNull() && (blurred || !_blurred))
			{
				return;
			}
			const auto w = _width * cIntRetinaFactor();
			const auto h = _height * cIntRetinaFactor();
			_current = image->pixNoCache(
				fileOrigin(),
				w,
				h,
				Images::Option::Smooth
				| (blurred ? Images::Option::Blurred : Images::Option(0)));
			_current.setDevicePixelRatio(cRetinaFactor());
			_blurred = blurred;
		}

		void OverlayWidget::validatePhotoCurrentImage()
		{
			validatePhotoImage(_photo->large(), false);
			validatePhotoImage(_photo->thumbnail(), true);
			validatePhotoImage(_photo->thumbnailSmall(), true);
			validatePhotoImage(_photo->thumbnailInline(), true);
			if (_current.isNull())
			{
				_photo->loadThumbnailSmall(fileOrigin());
			}
		}

		void OverlayWidget::paintEvent(QPaintEvent* e)
		{
			const auto r = e->rect();
			const auto& region = e->region();
			const auto rects = region.rects();

			const auto contentShown = _photo || documentContentShown();
			const auto bgRects = contentShown
				                     ? (region - contentRect()).rects()
				                     : rects;

			auto ms = crl::now();

			Painter p(this);

			bool name = false;

			p.setClipRegion(region);

			// main bg
			const auto m = p.compositionMode();
			p.setCompositionMode(QPainter::CompositionMode_Source);
			const auto bgColor = _fullScreenVideo ? st::mediaviewVideoBg : st::mediaviewBg;
			for (const auto& rect : bgRects)
			{
				p.fillRect(rect, bgColor);
			}
			p.setCompositionMode(m);

			// photo
			if (_photo)
			{
				validatePhotoCurrentImage();
			}
			p.setOpacity(1);
			if (contentShown)
			{
				const auto rect = contentRect();
				if (rect.intersects(r))
				{
					if (videoShown())
					{
						paintTransformedVideoFrame(p);
					}
					else
					{
						if ((!_doc || !_doc->getStickerLarge())
							&& (_current.isNull() || _current.hasAlpha()))
						{
							p.fillRect(rect, _transparentBrush);
						}
						if (!_current.isNull())
						{
							PainterHighQualityEnabler hq(p);
							p.drawPixmap(rect, _current);
						}
					}

					const auto radial = _radial.animating();
					const auto radialOpacity = radial ? _radial.opacity() : 0.;
					paintRadialLoading(p, radial, radialOpacity);
				}
				if (_saveMsgStarted && _saveMsg.intersects(r))
				{
					float64 dt = float64(ms) - _saveMsgStarted, hidingDt = dt - st::mediaviewSaveMsgShowing - st::
						        mediaviewSaveMsgShown;
					if (dt < st::mediaviewSaveMsgShowing + st::mediaviewSaveMsgShown + st::mediaviewSaveMsgHiding)
					{
						if (hidingDt >= 0 && _saveMsgOpacity.to() > 0.5)
						{
							_saveMsgOpacity.start(0);
						}
						float64 progress = (hidingDt >= 0)
							                   ? (hidingDt / st::mediaviewSaveMsgHiding)
							                   : (dt / st::mediaviewSaveMsgShowing);
						_saveMsgOpacity.update(qMin(progress, 1.), anim::linear);
						if (_saveMsgOpacity.current() > 0)
						{
							p.setOpacity(_saveMsgOpacity.current());
							App::roundRect(p, _saveMsg, st::mediaviewSaveMsgBg, MediaviewSaveCorners);
							st::mediaviewSaveMsgCheck.paint(p, _saveMsg.topLeft() + st::mediaviewSaveMsgCheckPos,
							                                width());

							p.setPen(st::mediaviewSaveMsgFg);
							p.setTextPalette(st::mediaviewTextPalette);
							_saveMsgText.draw(p, _saveMsg.x() + st::mediaviewSaveMsgPadding.left(),
							                  _saveMsg.y() + st::mediaviewSaveMsgPadding.top(),
							                  _saveMsg.width() - st::mediaviewSaveMsgPadding.left() - st::
							                  mediaviewSaveMsgPadding.right());
							p.restoreTextPalette();
							p.setOpacity(1);
						}
						if (!_blurred)
						{
							auto nextFrame = (dt < st::mediaviewSaveMsgShowing || hidingDt >= 0)
								                 ? int(AnimationTimerDelta)
								                 : (st::mediaviewSaveMsgShowing + st::mediaviewSaveMsgShown + 1 - dt);
							_saveMsgUpdater.start(nextFrame);
						}
					}
					else
					{
						_saveMsgStarted = 0;
					}
				}
			}
			else if (_themePreviewShown)
			{
				paintThemePreview(p, r);
			}
			else if (documentBubbleShown())
			{
				if (_docRect.intersects(r))
				{
					p.fillRect(_docRect, st::mediaviewFileBg);
					if (_docIconRect.intersects(r))
					{
						const auto radial = _radial.animating();
						const auto radialOpacity = radial ? _radial.opacity() : 0.;
						if (!_doc || !_doc->hasThumbnail())
						{
							p.fillRect(_docIconRect, _docIconColor);
							if ((!_doc || _doc->loaded()) && (!radial || radialOpacity < 1) && _docIcon)
							{
								_docIcon->paint(p, _docIconRect.x() + (_docIconRect.width() - _docIcon->width()),
								                _docIconRect.y(), width());
								p.setPen(st::mediaviewFileExtFg);
								p.setFont(st::mediaviewFileExtFont);
								if (!_docExt.isEmpty())
								{
									p.drawText(_docIconRect.x() + (_docIconRect.width() - _docExtWidth) / 2,
									           _docIconRect.y() + st::mediaviewFileExtTop + st::mediaviewFileExtFont->
									           ascent, _docExt);
								}
							}
						}
						else
						{
							int32 rf(cIntRetinaFactor());
							p.drawPixmap(_docIconRect.topLeft(), _doc->thumbnail()->pix(fileOrigin(), _docThumbw),
							             QRect(_docThumbx * rf, _docThumby * rf, st::mediaviewFileIconSize * rf,
							                   st::mediaviewFileIconSize * rf));
						}

						paintRadialLoading(p, radial, radialOpacity);
					}

					if (!_docIconRect.contains(r))
					{
						name = true;
						p.setPen(st::mediaviewFileNameFg);
						p.setFont(st::mediaviewFileNameFont);
						p.drawTextLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize,
						               _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileNameTop, width(),
						               _docName, _docNameWidth);

						p.setPen(st::mediaviewFileSizeFg);
						p.setFont(st::mediaviewFont);
						p.drawTextLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize,
						               _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileSizeTop, width(),
						               _docSize, _docSizeWidth);
					}
				}
			}

			float64 co = _fullScreenVideo ? 0. : _controlsOpacity.current();
			if (co > 0)
			{
				// left nav bar
				if (_leftNav.intersects(r) && _leftNavVisible)
				{
					auto o = overLevel(OverLeftNav);
					if (o > 0)
					{
						p.setOpacity(o * co);
						for (const auto& rect : rects)
						{
							const auto fill = _leftNav.intersected(rect);
							if (!fill.isEmpty()) p.fillRect(fill, st::mediaviewControlBg);
						}
					}
					if (_leftNavIcon.intersects(r))
					{
						p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
						st::mediaviewLeft.paintInCenter(p, _leftNavIcon);
					}
				}

				// right nav bar
				if (_rightNav.intersects(r) && _rightNavVisible)
				{
					auto o = overLevel(OverRightNav);
					if (o > 0)
					{
						p.setOpacity(o * co);
						for (const auto& rect : rects)
						{
							const auto fill = _rightNav.intersected(rect);
							if (!fill.isEmpty()) p.fillRect(fill, st::mediaviewControlBg);
						}
					}
					if (_rightNavIcon.intersects(r))
					{
						p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
						st::mediaviewRight.paintInCenter(p, _rightNavIcon);
					}
				}

				// close button
				if (_closeNav.intersects(r))
				{
					auto o = overLevel(OverClose);
					if (o > 0)
					{
						p.setOpacity(o * co);
						for (const auto& rect : rects)
						{
							const auto fill = _closeNav.intersected(rect);
							if (!fill.isEmpty()) p.fillRect(fill, st::mediaviewControlBg);
						}
					}
					if (_closeNavIcon.intersects(r))
					{
						p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
						st::mediaviewClose.paintInCenter(p, _closeNavIcon);
					}
				}

				// save button
				if (_saveVisible && _saveNavIcon.intersects(r))
				{
					auto o = overLevel(OverSave);
					p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
					st::mediaviewSave.paintInCenter(p, _saveNavIcon);
				}

				// more area
				if (_moreNavIcon.intersects(r))
				{
					auto o = overLevel(OverMore);
					p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
					st::mediaviewMore.paintInCenter(p, _moreNavIcon);
				}

				p.setPen(st::mediaviewControlFg);
				p.setFont(st::mediaviewThickFont);

				// header
				if (_headerNav.intersects(r))
				{
					auto o = _headerHasLink ? overLevel(OverHeader) : 0;
					p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
					p.drawText(_headerNav.left(), _headerNav.top() + st::mediaviewThickFont->ascent, _headerText);

					if (o > 0)
					{
						p.setOpacity(o * co);
						p.drawLine(_headerNav.left(), _headerNav.top() + st::mediaviewThickFont->ascent + 1,
						           _headerNav.right(), _headerNav.top() + st::mediaviewThickFont->ascent + 1);
					}
				}

				p.setFont(st::mediaviewFont);

				// name
				if (_nameNav.isValid() && _nameNav.intersects(r))
				{
					float64 o = _from ? overLevel(OverName) : 0.;
					p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
					_fromNameLabel.drawElided(p, _nameNav.left(), _nameNav.top(), _nameNav.width());

					if (o > 0)
					{
						p.setOpacity(o * co);
						p.drawLine(_nameNav.left(), _nameNav.top() + st::mediaviewFont->ascent + 1, _nameNav.right(),
						           _nameNav.top() + st::mediaviewFont->ascent + 1);
					}
				}

				// date
				if (_dateNav.intersects(r))
				{
					float64 o = overLevel(OverDate);
					p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
					p.drawText(_dateNav.left(), _dateNav.top() + st::mediaviewFont->ascent, _dateText);

					if (o > 0)
					{
						p.setOpacity(o * co);
						p.drawLine(_dateNav.left(), _dateNav.top() + st::mediaviewFont->ascent + 1, _dateNav.right(),
						           _dateNav.top() + st::mediaviewFont->ascent + 1);
					}
				}

				// caption
				if (!_caption.isEmpty())
				{
					QRect outer(_captionRect.marginsAdded(st::mediaviewCaptionPadding));
					if (outer.intersects(r))
					{
						p.setOpacity(co);
						p.setBrush(st::mediaviewCaptionBg);
						p.setPen(Qt::NoPen);
						p.drawRoundedRect(outer, st::mediaviewCaptionRadius, st::mediaviewCaptionRadius);
						if (_captionRect.intersects(r))
						{
							p.setTextPalette(st::mediaviewTextPalette);
							p.setPen(st::mediaviewCaptionFg);
							_caption.drawElided(p, _captionRect.x(), _captionRect.y(), _captionRect.width(),
							                    _captionRect.height() / st::mediaviewCaptionStyle.font->height);
							p.restoreTextPalette();
						}
					}
				}

				if (_groupThumbs && _groupThumbsRect.intersects(r))
				{
					p.setOpacity(co);
					_groupThumbs->paint(
						p,
						_groupThumbsLeft,
						_groupThumbsTop,
						width());
					if (_groupThumbs->hidden())
					{
						_groupThumbs = nullptr;
						_groupThumbsRect = QRect();
					}
				}
			}
			checkGroupThumbsAnimation();
		}

		void OverlayWidget::checkGroupThumbsAnimation()
		{
			if (_groupThumbs && (!_streamed || _streamed->player.ready()))
			{
				_groupThumbs->checkForAnimationStart();
			}
		}

		void OverlayWidget::paintTransformedVideoFrame(Painter& p)
		{
			const auto rect = contentRect();
			const auto image = videoFrameForDirectPaint();
			//if (_fullScreenVideo) {
			//	const auto fill = rect.intersected(this->rect());
			//	PaintImageProfile(p, image, rect, fill);
			//} else {
			const auto rotation = _streamed->info.video.rotation;
			const auto rotated = [](QRect rect, int rotation)
			{
				switch (rotation)
				{
				case 0: return rect;
				case 90: return QRect(
						rect.y(),
						-rect.x() - rect.width(),
						rect.height(),
						rect.width());
				case 180: return QRect(
						-rect.x() - rect.width(),
						-rect.y() - rect.height(),
						rect.width(),
						rect.height());
				case 270: return QRect(
						-rect.y() - rect.height(),
						rect.x(),
						rect.height(),
						rect.width());
				}
				Unexpected("Rotation in OverlayWidget::paintTransformedVideoFrame");
			};

			PainterHighQualityEnabler hq(p);
			if (rotation)
			{
				p.save();
				p.rotate(rotation);
			}
			p.drawImage(rotated(rect, rotation), image);
			if (rotation)
			{
				p.restore();
			}
			//}
		}

		void OverlayWidget::paintRadialLoading(
			Painter& p,
			bool radial,
			float64 radialOpacity)
		{
			if (_streamed)
			{
				if (!_streamed->radial.animating())
				{
					return;
				}
				if (!_streamed->fading.animating() && !_streamed->waiting)
				{
					if (!_streamed->waiting)
					{
						_streamed->radial.stop(anim::type::instant);
					}
					return;
				}
			}
			else if (!radial && (!_doc || _doc->loaded()))
			{
				return;
			}

			const auto inner = radialRect();
			Assert(!inner.isEmpty());

#ifdef USE_OPENGL_OVERLAY_WIDGET
			{
				if (_radialCache.size() != inner.size() * cIntRetinaFactor()) {
					_radialCache = QImage(
						inner.size() * cIntRetinaFactor(),
						QImage::Format_ARGB32_Premultiplied);
					_radialCache.setDevicePixelRatio(cRetinaFactor());
				}
				_radialCache.fill(Qt::transparent);

				Painter q(&_radialCache);
				const auto moved = inner.translated(-inner.topLeft());
				paintRadialLoadingContent(q, moved, radial, radialOpacity);
			}
			p.drawImage(inner.topLeft(), _radialCache);
#else // USE_OPENGL_OVERLAY_WIDGET
			paintRadialLoadingContent(p, inner, radial, radialOpacity);
#endif // USE_OPENGL_OVERLAY_WIDGET
		}

		void OverlayWidget::paintRadialLoadingContent(
			Painter& p,
			QRect inner,
			bool radial,
			float64 radialOpacity) const
		{
			const auto arc = inner.marginsRemoved(QMargins(
				st::radialLine,
				st::radialLine,
				st::radialLine,
				st::radialLine));
			const auto paintBg = [&](float64 opacity, QBrush brush)
			{
				p.setOpacity(opacity);
				p.setPen(Qt::NoPen);
				p.setBrush(brush);
				{
					PainterHighQualityEnabler hq(p);
					p.drawEllipse(inner);
				}
				p.setOpacity(1.);
			};

			if (_streamed)
			{
				paintBg(
					_streamed->fading.value(_streamed->waiting ? 1. : 0.),
					st::radialBg);
				_streamed->radial.draw(p, arc.topLeft(), arc.size(), width());
				return;
			}
			if (_photo)
			{
				paintBg(radialOpacity, st::radialBg);
			}
			else
			{
				const auto o = overLevel(OverIcon);
				paintBg(
					_doc->loaded() ? radialOpacity : 1.,
					anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, o));

				const auto icon = [&]() -> const style::icon*
				{
					if (radial || _doc->loading())
					{
						return &st::historyFileThumbCancel;
					}
					return &st::historyFileThumbDownload;
				}();
				if (icon)
				{
					icon->paintInCenter(p, inner);
				}
			}
			if (radial)
			{
				p.setOpacity(1);
				_radial.draw(p, arc, st::radialLine, st::radialFg);
			}
		}

		void OverlayWidget::paintThemePreview(Painter& p, QRect clip)
		{
			auto fill = _themePreviewRect.intersected(clip);
			if (!fill.isEmpty())
			{
				if (_themePreview)
				{
					p.drawImage(
						myrtlrect(_themePreviewRect).topLeft(),
						_themePreview->preview);
				}
				else
				{
					p.fillRect(fill, st::themePreviewBg);
					p.setFont(st::themePreviewLoadingFont);
					p.setPen(st::themePreviewLoadingFg);
					p.drawText(
						_themePreviewRect,
						(_themePreviewId
							 ? tr::lng_theme_preview_generating(tr::now)
							 : tr::lng_theme_preview_invalid(tr::now)),
						QTextOption(style::al_center));
				}
			}

			auto fillOverlay = [&](QRect fill)
			{
				auto clipped = fill.intersected(clip);
				if (!clipped.isEmpty())
				{
					p.setOpacity(st::themePreviewOverlayOpacity);
					p.fillRect(clipped, st::themePreviewBg);
					p.setOpacity(1.);
				}
			};
			auto titleRect = QRect(_themePreviewRect.x(), _themePreviewRect.y(), _themePreviewRect.width(),
			                       st::themePreviewMargin.top());
			if (titleRect.x() < 0)
			{
				titleRect = QRect(0, _themePreviewRect.y(), width(), st::themePreviewMargin.top());
			}
			if (auto fillTitleRect = (titleRect.y() < 0))
			{
				titleRect.moveTop(0);
				fillOverlay(titleRect);
			}
			titleRect = titleRect.marginsRemoved(QMargins(st::themePreviewMargin.left(), st::themePreviewTitleTop,
			                                              st::themePreviewMargin.right(),
			                                              titleRect.height() - st::themePreviewTitleTop - st::
			                                              themePreviewTitleFont->height));
			if (titleRect.intersects(clip))
			{
				p.setFont(st::themePreviewTitleFont);
				p.setPen(st::themePreviewTitleFg);
				const auto title = _themeCloudData.title.isEmpty()
					                   ? tr::lng_theme_preview_title(tr::now)
					                   : _themeCloudData.title;
				const auto elided = st::themePreviewTitleFont->elided(title, titleRect.width());
				p.drawTextLeft(titleRect.x(), titleRect.y(), width(), elided);
			}

			auto buttonsRect = QRect(_themePreviewRect.x(),
			                         _themePreviewRect.y() + _themePreviewRect.height() - st::themePreviewMargin.
			                         bottom(), _themePreviewRect.width(), st::themePreviewMargin.bottom());
			if (auto fillButtonsRect = (buttonsRect.y() + buttonsRect.height() > height()))
			{
				buttonsRect.moveTop(height() - buttonsRect.height());
				fillOverlay(buttonsRect);
			}
			if (_themeShare && _themeCloudData.usersCount > 0)
			{
				p.setFont(st::boxTextFont);
				p.setPen(st::windowSubTextFg);
				const auto left = _themeShare->x() + _themeShare->width() - (st::themePreviewCancelButton.width / 2);
				const auto baseline = _themeShare->y() + st::themePreviewCancelButton.padding.top() + +st::
					themePreviewCancelButton.textTop + st::themePreviewCancelButton.font->ascent;
				p.drawText(left, baseline, tr::lng_theme_preview_users(tr::now, lt_count, _themeCloudData.usersCount));
			}
		}

		void OverlayWidget::keyPressEvent(QKeyEvent* e)
		{
			const auto ctrl = e->modifiers().testFlag(Qt::ControlModifier);
			if (_streamed)
			{
				// Ctrl + F for full screen toggle is in eventFilter().
				const auto toggleFull = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return)
					&& (e->modifiers().testFlag(Qt::AltModifier) || ctrl);
				if (toggleFull)
				{
					playbackToggleFullScreen();
					return;
				}
				if (e->key() == Qt::Key_Space)
				{
					playbackPauseResume();
					return;
				}
				if (_fullScreenVideo)
				{
					if (e->key() == Qt::Key_Escape)
					{
						playbackToggleFullScreen();
					}
					return;
				}
			}
			if (!_menu && e->key() == Qt::Key_Escape)
			{
				if (_doc && _doc->loading() && !_streamed)
				{
					onDocClick();
				}
				else
				{
					close();
				}
			}
			else if (e == QKeySequence::Save || e == QKeySequence::SaveAs)
			{
				onSaveAs();
			}
			else if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && ctrl))
			{
				onCopy();
			}
			else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space)
			{
				if (_streamed)
				{
					playbackPauseResume();
				}
				else if (_doc && !_doc->loading() && (documentBubbleShown() || !_doc->loaded()))
				{
					onDocClick();
				}
			}
			else if (e->key() == Qt::Key_Left)
			{
				if (_controlsHideTimer.isActive())
				{
					activateControls();
				}
				moveToNext(-1);
			}
			else if (e->key() == Qt::Key_Right)
			{
				if (_controlsHideTimer.isActive())
				{
					activateControls();
				}
				moveToNext(1);
			}
			else if (ctrl)
			{
				if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == Qt::Key_Asterisk || e->key() ==
					']')
				{
					zoomIn();
				}
				else if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore)
				{
					zoomOut();
				}
				else if (e->key() == Qt::Key_0)
				{
					zoomReset();
				}
				else if (e->key() == Qt::Key_I)
				{
					update();
				}
			}
		}

		void OverlayWidget::wheelEvent(QWheelEvent* e)
		{
#ifdef OS_MAC_OLD
			constexpr auto step = 120;
#else // OS_MAC_OLD
			constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
#endif // OS_MAC_OLD

			_verticalWheelDelta += e->angleDelta().y();
			while (qAbs(_verticalWheelDelta) >= step)
			{
				if (_verticalWheelDelta < 0)
				{
					_verticalWheelDelta += step;
					if (e->modifiers().testFlag(Qt::ControlModifier))
					{
						zoomOut();
					}
					else
					{
#ifndef OS_MAC_OLD
						if (e->source() == Qt::MouseEventNotSynthesized)
						{
							moveToNext(1);
						}
#endif // OS_MAC_OLD
					}
				}
				else
				{
					_verticalWheelDelta -= step;
					if (e->modifiers().testFlag(Qt::ControlModifier))
					{
						zoomIn();
					}
					else
					{
#ifndef OS_MAC_OLD
						if (e->source() == Qt::MouseEventNotSynthesized)
						{
							moveToNext(-1);
						}
#endif // OS_MAC_OLD
					}
				}
			}
		}

		void OverlayWidget::setZoomLevel(int newZoom)
		{
			if (_zoom == newZoom) return;

			float64 nx, ny, z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
			const auto contentSize = videoShown()
				                         ? style::ConvertScale(videoSize())
				                         : QSize(_width, _height);
			_w = contentSize.width();
			_h = contentSize.height();
			if (z >= 0)
			{
				nx = (_x - width() / 2.) / (z + 1);
				ny = (_y - height() / 2.) / (z + 1);
			}
			else
			{
				nx = (_x - width() / 2.) * (-z + 1);
				ny = (_y - height() / 2.) * (-z + 1);
			}
			_zoom = newZoom;
			z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
			if (z > 0)
			{
				_w = qRound(_w * (z + 1));
				_h = qRound(_h * (z + 1));
				_x = qRound(nx * (z + 1) + width() / 2.);
				_y = qRound(ny * (z + 1) + height() / 2.);
			}
			else
			{
				_w = qRound(_w / (-z + 1));
				_h = qRound(_h / (-z + 1));
				_x = qRound(nx / (-z + 1) + width() / 2.);
				_y = qRound(ny / (-z + 1) + height() / 2.);
			}
			snapXY();
			update();
		}

		OverlayWidget::Entity OverlayWidget::entityForUserPhotos(int index) const
		{
			Expects(_userPhotosData.has_value());

			if (index < 0 || index >= _userPhotosData->size())
			{
				return {std::nullopt, nullptr};
			}
			if (auto photo = Auth().data().photo((*_userPhotosData)[index]))
			{
				return {photo, nullptr};
			}
			return {std::nullopt, nullptr};
		}

		OverlayWidget::Entity OverlayWidget::entityForSharedMedia(int index) const
		{
			Expects(_sharedMediaData.has_value());

			if (index < 0 || index >= _sharedMediaData->size())
			{
				return {std::nullopt, nullptr};
			}
			auto value = (*_sharedMediaData)[index];
			if (const auto photo = base::get_if<not_null<PhotoData*>>(&value))
			{
				// Last peer photo.
				return {*photo, nullptr};
			}
			if (const auto itemId = base::get_if<FullMsgId>(&value))
			{
				return entityForItemId(*itemId);
			}
			return {std::nullopt, nullptr};
		}

		OverlayWidget::Entity OverlayWidget::entityForCollage(int index) const
		{
			Expects(_collageData.has_value());

			const auto item = Auth().data().message(_msgid);
			const auto& items = _collageData->items;
			if (!item || index < 0 || index >= items.size())
			{
				return {std::nullopt, nullptr};
			}
			if (const auto document = base::get_if<DocumentData*>(&items[index]))
			{
				return {*document, item};
			}
			if (const auto photo = base::get_if<PhotoData*>(&items[index]))
			{
				return {*photo, item};
			}
			return {std::nullopt, nullptr};
		}

		OverlayWidget::Entity OverlayWidget::entityForItemId(const FullMsgId& itemId) const
		{
			if (const auto item = Auth().data().message(itemId))
			{
				if (const auto media = item->media())
				{
					if (const auto photo = media->photo())
					{
						return {photo, item};
					}
					if (const auto document = media->document())
					{
						return {document, item};
					}
				}
				return {std::nullopt, item};
			}
			return {std::nullopt, nullptr};
		}

		OverlayWidget::Entity OverlayWidget::entityByIndex(int index) const
		{
			if (_sharedMediaData)
			{
				return entityForSharedMedia(index);
			}
			if (_userPhotosData)
			{
				return entityForUserPhotos(index);
			}
			if (_collageData)
			{
				return entityForCollage(index);
			}
			return {std::nullopt, nullptr};
		}

		void OverlayWidget::setContext(base::optional_variant<
			not_null<HistoryItem*>,
			not_null<PeerData*>> context)
		{
			if (auto item = base::get_if<not_null<HistoryItem*>>(&context))
			{
				_msgid = (*item)->fullId();
				_canForwardItem = (*item)->allowsForward();
				_canDeleteItem = (*item)->canDelete();
				_history = (*item)->history();
				_peer = _history->peer;
			}
			else if (auto peer = base::get_if<not_null<PeerData*>>(&context))
			{
				_msgid = FullMsgId();
				_canForwardItem = _canDeleteItem = false;
				_history = (*peer)->owner().history(*peer);
				_peer = *peer;
			}
			else
			{
				_msgid = FullMsgId();
				_canForwardItem = _canDeleteItem = false;
				_history = nullptr;
				_peer = nullptr;
			}
			_migrated = nullptr;
			if (_history)
			{
				if (_history->peer->migrateFrom())
				{
					_migrated = _history->owner().history(_history->peer->migrateFrom());
				}
				else if (_history->peer->migrateTo())
				{
					_migrated = _history;
					_history = _history->owner().history(_history->peer->migrateTo());
				}
			}
			_user = _peer ? _peer->asUser() : nullptr;
		}

		bool OverlayWidget::moveToNext(int delta)
		{
			if (!_index)
			{
				return false;
			}
			auto newIndex = *_index + delta;
			return moveToEntity(entityByIndex(newIndex));
		}

		bool OverlayWidget::moveToEntity(const Entity& entity, int preloadDelta)
		{
			if (!entity.data && !entity.item)
			{
				return false;
			}
			if (const auto item = entity.item)
			{
				setContext(item);
			}
			else if (_peer)
			{
				setContext(_peer);
			}
			else
			{
				setContext(std::nullopt);
			}
			clearStreaming();
			_streamingStartPaused = false;
			if (auto photo = base::get_if<not_null<PhotoData*>>(&entity.data))
			{
				displayPhoto(*photo, entity.item);
			}
			else if (auto document = base::get_if<not_null<DocumentData*>>(&entity.data))
			{
				displayDocument(*document, entity.item);
			}
			else
			{
				displayDocument(nullptr, entity.item);
			}
			preloadData(preloadDelta);
			return true;
		}

		void OverlayWidget::preloadData(int delta)
		{
			if (!_index)
			{
				return;
			}
			auto from = *_index + (delta ? delta : -1);
			auto till = *_index + (delta ? delta * kPreloadCount : 1);
			if (from > till) std::swap(from, till);

			if (delta != 0)
			{
				auto forgetIndex = *_index - delta * 2;
				auto entity = entityByIndex(forgetIndex);
				if (auto photo = base::get_if<not_null<PhotoData*>>(&entity.data))
				{
					(*photo)->unload();
				}
				else if (auto document = base::get_if<not_null<DocumentData*>>(&entity.data))
				{
					(*document)->unload();
				}
			}

			for (auto index = from; index != till; ++index)
			{
				auto entity = entityByIndex(index);
				if (auto photo = base::get_if<not_null<PhotoData*>>(&entity.data))
				{
					(*photo)->download(fileOrigin());
				}
				else if (auto document = base::get_if<not_null<DocumentData*>>(&entity.data))
				{
					if (const auto image = (*document)->getStickerLarge())
					{
						image->load(fileOrigin());
					}
					else
					{
						(*document)->loadThumbnail(fileOrigin());
						(*document)->automaticLoad(fileOrigin(), entity.item);
					}
				}
			}
		}

		void OverlayWidget::mousePressEvent(QMouseEvent* e)
		{
			updateOver(e->pos());
			if (_menu || !_receiveMouse) return;

			ClickHandler::pressed();

			if (e->button() == Qt::LeftButton)
			{
				_down = OverNone;
				if (!ClickHandler::getPressed())
				{
					if (_over == OverLeftNav && moveToNext(-1))
					{
						_lastAction = e->pos();
					}
					else if (_over == OverRightNav && moveToNext(1))
					{
						_lastAction = e->pos();
					}
					else if (_over == OverName
						|| _over == OverDate
						|| _over == OverHeader
						|| _over == OverSave
						|| _over == OverIcon
						|| _over == OverMore
						|| _over == OverClose)
					{
						_down = _over;
					}
					else if (_over == OverVideo && (!_saveMsg.contains(e->pos()) || !_saveMsgStarted))
					{
						_down = _over;
						_pressed = true;
						_dragging = 0;
						updateCursor();
						_mStart = e->pos();
						_xStart = _x;
						_yStart = _y;
					}
					else if (!_saveMsg.contains(e->pos()) || !_saveMsgStarted)
					{
						_pressed = true;
						_dragging = 0;
						updateCursor();
						_mStart = e->pos();
						_xStart = _x;
						_yStart = _y;
					}
				}
			}
			else if (e->button() == Qt::MiddleButton)
			{
				if (_over == OverVideo && !_fullScreenVideo && (!_saveMsg.contains(e->pos()) || !_saveMsgStarted))
				{
					//Temporary solution
					_scale = _scale ? 0 : 1;
					_x = x();
					_y = y();
					if (_scale)
					{
						_w = _w * 2;
						_h = _h * 2;
					}
					else
					{
						_w = _w / 2;
						_h = _h / 2;
					}

					setGeometry(_x, _y, _w, _h);
					auto navSkip = 2 * st::mediaviewControlMargin + st::mediaviewControlSize;
					_closeNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize,
					                      st::mediaviewControlMargin, st::mediaviewControlSize,
					                      st::mediaviewControlSize);
					_closeNavIcon = style::centerrect(_closeNav, st::mediaviewClose);
					_leftNav = myrtlrect(st::mediaviewControlMargin, navSkip, st::mediaviewControlSize,
					                     height() - 2 * navSkip);
					_leftNavIcon = style::centerrect(_leftNav, st::mediaviewLeft);
					_rightNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, navSkip,
					                      st::mediaviewControlSize, height() - 2 * navSkip);
					_rightNavIcon = style::centerrect(_rightNav, st::mediaviewRight);

					_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
					_photoRadialRect = QRect(
						QPoint((width() - st::radialSize.width()) / 2, (height() - st::radialSize.height()) / 2),
						st::radialSize);
					updateControls();
					snapXY();
					update();
				}
			}
			activateControls();
		}

		void OverlayWidget::mouseDoubleClickEvent(QMouseEvent* e)
		{
			updateOver(e->pos());

			if (_over == OverVideo && e->button() == Qt::LeftButton)
			{
				playbackToggleFullScreen();
				playbackPauseResume();
			}
			else
			{
				e->ignore();
				return OverlayParent::mouseDoubleClickEvent(e);
			}
		}

		void OverlayWidget::snapXY()
		{
			int32 xmin = width() - _w, xmax = 0;
			int32 ymin = height() - _h, ymax = 0;
			if (xmin > (width() - _w) / 2) xmin = (width() - _w) / 2;
			if (xmax < (width() - _w) / 2) xmax = (width() - _w) / 2;
			if (ymin > (height() - _h) / 2) ymin = (height() - _h) / 2;
			if (ymax < (height() - _h) / 2) ymax = (height() - _h) / 2;
			if (_x < xmin) _x = xmin;
			if (_x > xmax) _x = xmax;
			if (_y < ymin) _y = ymin;
			if (_y > ymax) _y = ymax;
		}

		void OverlayWidget::mouseMoveEvent(QMouseEvent* e)
		{
			updateOver(e->pos());
			if (_lastAction.x() >= 0 && (e->pos() - _lastAction).manhattanLength() >= st::mediaviewDeltaFromLastAction)
			{
				_lastAction = QPoint(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction);
			}
			if (_pressed && !_fullScreenVideo && !_photo)
			{
				if (!_dragging && (e->pos() - _mStart).manhattanLength() >= QApplication::startDragDistance())
				{
					_dragging = QRect(_x, _y, _w, _h).contains(_mStart) ? 1 : -1;
					if (_dragging > 0)
					{
						if (_w > width() || _h > height())
						{
							setCursor(style::cur_sizeall);
						}
						else
						{
							setCursor(style::cur_sizeall);
						}
					}
				}
				if (_dragging > 0)
				{
					_x = _xStart + (e->globalPos() - _mStart).x();
					_y = _yStart + (e->globalPos() - _mStart).y();
					move(_x, _y);
					snapXY();
					update();
				}
			}
		}

		void OverlayWidget::updateOverRect(OverState state)
		{
			switch (state)
			{
			case OverLeftNav: update(_leftNav);
				break;
			case OverRightNav: update(_rightNav);
				break;
			case OverName: update(_nameNav);
				break;
			case OverDate: update(_dateNav);
				break;
			case OverSave: update(_saveNavIcon);
				break;
			case OverIcon: update(_docIconRect);
				break;
			case OverHeader: update(_headerNav);
				break;
			case OverClose: update(_closeNav);
				break;
			case OverMore: update(_moreNavIcon);
				break;
			}
		}

		bool OverlayWidget::updateOverState(OverState newState)
		{
			bool result = true;
			if (_over != newState)
			{
				if (newState == OverMore && !_ignoringDropdown)
				{
					_dropdownShowTimer->start(0);
				}
				else
				{
					_dropdownShowTimer->stop();
				}
				updateOverRect(_over);
				updateOverRect(newState);
				if (_over != OverNone)
				{
					_animations[_over] = crl::now();
					const auto i = _animationOpacities.find(_over);
					if (i != end(_animationOpacities))
					{
						i->second.start(0);
					}
					else
					{
						_animationOpacities.emplace(_over, anim::value(1, 0));
					}
					if (!_stateAnimation.animating())
					{
						_stateAnimation.start();
					}
				}
				else
				{
					result = false;
				}
				_over = newState;
				if (newState != OverNone)
				{
					_animations[_over] = crl::now();
					const auto i = _animationOpacities.find(_over);
					if (i != end(_animationOpacities))
					{
						i->second.start(1);
					}
					else
					{
						_animationOpacities.emplace(_over, anim::value(0, 1));
					}
					if (!_stateAnimation.animating())
					{
						_stateAnimation.start();
					}
				}
				updateCursor();
			}
			return result;
		}

		void OverlayWidget::updateOver(QPoint pos)
		{
			ClickHandlerPtr lnk;
			ClickHandlerHost* lnkhost = nullptr;
			if (_saveMsgStarted && _saveMsg.contains(pos))
			{
				auto textState = _saveMsgText.getState(
					pos - _saveMsg.topLeft() - QPoint(st::mediaviewSaveMsgPadding.left(),
					                                  st::mediaviewSaveMsgPadding.top()),
					_saveMsg.width() - st::mediaviewSaveMsgPadding.left() - st::mediaviewSaveMsgPadding.right());
				lnk = textState.link;
				lnkhost = this;
			}
			else if (_captionRect.contains(pos))
			{
				auto textState = _caption.getState(pos - _captionRect.topLeft(), _captionRect.width());
				lnk = textState.link;
				lnkhost = this;
			}
			else if (_groupThumbs && _groupThumbsRect.contains(pos))
			{
				const auto point = pos - QPoint(_groupThumbsLeft, _groupThumbsTop);
				lnk = _groupThumbs->getState(point);
				lnkhost = this;
			}


			// retina
			if (pos.x() == width())
			{
				pos.setX(pos.x() - 1);
			}
			if (pos.y() == height())
			{
				pos.setY(pos.y() - 1);
			}

			ClickHandler::setActive(lnk, lnkhost);

			if (_pressed || _dragging) return;

			if (_fullScreenVideo)
			{
				updateOverState(OverVideo);
			}
			else if (_leftNavVisible && _leftNav.contains(pos))
			{
				updateOverState(OverLeftNav);
			}
			else if (_rightNavVisible && _rightNav.contains(pos))
			{
				updateOverState(OverRightNav);
			}
			else if (_from && _nameNav.contains(pos))
			{
				updateOverState(OverName);
			}
			else if (IsServerMsgId(_msgid.msg) && _dateNav.contains(pos))
			{
				updateOverState(OverDate);
			}
			else if (_headerHasLink && _headerNav.contains(pos))
			{
				updateOverState(OverHeader);
			}
			else if (_saveVisible && _saveNav.contains(pos))
			{
				updateOverState(OverSave);
			}
			else if (_doc && documentBubbleShown() && _docIconRect.contains(pos))
			{
				updateOverState(OverIcon);
			}
			else if (_moreNav.contains(pos))
			{
				updateOverState(OverMore);
			}
			else if (_closeNav.contains(pos))
			{
				updateOverState(OverClose);
			}
			else if (documentContentShown() && contentRect().contains(pos))
			{
				if ((_doc->isVideoFile() || _doc->isVideoMessage()) && _streamed)
				{
					updateOverState(OverVideo);
				}
				else if (!_doc->loaded())
				{
					updateOverState(OverIcon);
				}
				else if (_over != OverNone)
				{
					updateOverState(OverNone);
				}
			}
			else if (_over != OverNone)
			{
				updateOverState(OverNone);
			}
		}

		void OverlayWidget::mouseReleaseEvent(QMouseEvent* e)
		{
			updateOver(e->pos());

			if (const auto activated = ClickHandler::unpressed())
			{
				if (activated->dragText() == qstr("internal:show_saved_message"))
				{
					showSaveMsgFile();
					return;
				}
				ActivateClickHandler(this, activated, e->button());
				return;
			}

			if (_over == OverName && _down == OverName)
			{
				if (_from)
				{
					close();
					Ui::showPeerProfile(_from);
				}
			}
			else if (_over == OverDate && _down == OverDate)
			{
				onToMessage();
			}
			else if (_over == OverHeader && _down == OverHeader)
			{
				onOverview();
			}
			else if (_over == OverSave && _down == OverSave)
			{
				onDownload();
			}
			else if (_over == OverIcon && _down == OverIcon)
			{
				onDocClick();
			}
			else if (_over == OverMore && _down == OverMore)
			{
				QTimer::singleShot(0, this, SLOT(onDropdown()));
			}
			else if (_over == OverClose && _down == OverClose)
			{
				close();
			}
			else if (_over == OverVideo && _down == OverVideo && !_dragging)
			{
				if (_streamed)
				{
					_pressed = false;
					playbackPauseResume();
				}
			}
			else if (_pressed)
			{
				if (_dragging)
				{
					if (_dragging > 0)
					{
						_x = _xStart + (e->globalPos() - _mStart).x();
						_y = _yStart + (e->globalPos() - _mStart).y();
						move(_x, _y);
						snapXY();
						update();
					}
					_dragging = 0;
					setCursor(style::cur_default);
				}
				else if ((e->pos() - _lastAction).manhattanLength() >= st::mediaviewDeltaFromLastAction)
				{
					if (_themePreviewShown)
					{
						if (!_themePreviewRect.contains(e->pos()))
						{
							close();
						}
					}
					else if (!_doc
						|| documentContentShown()
						|| !documentBubbleShown()
						|| !_docRect.contains(e->pos()))
					{
						close();
					}
				}
				_pressed = false;
			}
			_down = OverNone;
			if (!isHidden())
			{
				activateControls();
			}
		}

		void OverlayWidget::contextMenuEvent(QContextMenuEvent* e)
		{
			if (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _w, _h).contains(e->pos()))
			{
				if (_menu)
				{
					_menu->deleteLater();
					_menu = nullptr;
				}
				_menu = new Ui::PopupMenu(this, st::mediaviewPopupMenu);
				updateActions();
				for_const(auto & action, _actions)
				{
					_menu->addAction(action.text, this, action.member);
				}
				connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
				_menu->popup(e->globalPos());
				e->accept();
				activateControls();
			}
		}

		void OverlayWidget::touchEvent(QTouchEvent* e)
		{
			switch (e->type())
			{
			case QEvent::TouchBegin:
				{
					if (_touchPress || e->touchPoints().isEmpty()) return;
					_touchTimer.start(QApplication::startDragTime());
					_touchPress = true;
					_touchMove = _touchRightButton = false;
					_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
				}
				break;

			case QEvent::TouchUpdate:
				{
					if (!_touchPress || e->touchPoints().isEmpty()) return;
					if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).
						manhattanLength() >= QApplication::startDragDistance())
					{
						_touchMove = true;
					}
				}
				break;

			case QEvent::TouchEnd:
				{
					if (!_touchPress) return;
					auto weak = MakeWeak(this);
					if (!_touchMove)
					{
						Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
						auto mapped = mapFromGlobal(_touchStart);

						QMouseEvent pressEvent(QEvent::MouseButtonPress, mapped, mapped, _touchStart, btn,
						                       Qt::MouseButtons(btn), Qt::KeyboardModifiers());
						pressEvent.accept();
						if (weak) mousePressEvent(&pressEvent);

						QMouseEvent releaseEvent(QEvent::MouseButtonRelease, mapped, mapped, _touchStart, btn,
						                         Qt::MouseButtons(btn), Qt::KeyboardModifiers());
						if (weak) mouseReleaseEvent(&releaseEvent);

						if (weak && _touchRightButton)
						{
							QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
							contextMenuEvent(&contextEvent);
						}
					}
					else if (_touchMove)
					{
						if ((!_leftNavVisible || !_leftNav.contains(mapFromGlobal(_touchStart))) && (!_rightNavVisible
							|| !_rightNav.contains(mapFromGlobal(_touchStart))))
						{
							QPoint d = (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart);
							if (d.x() * d.x() > d.y() * d.y() && (d.x() > st::mediaviewSwipeDistance || d.x() < -st::
								mediaviewSwipeDistance))
							{
								moveToNext(d.x() > 0 ? -1 : 1);
							}
						}
					}
					if (weak)
					{
						_touchTimer.stop();
						_touchPress = _touchMove = _touchRightButton = false;
					}
				}
				break;

			case QEvent::TouchCancel:
				{
					_touchPress = false;
					_touchTimer.stop();
				}
				break;
			}
		}

		bool OverlayWidget::eventHook(QEvent* e)
		{
			if (e->type() == QEvent::UpdateRequest)
			{
				_wasRepainted = true;
			}
			else if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::
				TouchEnd || e->type() == QEvent::TouchCancel)
			{
				QTouchEvent* ev = static_cast<QTouchEvent*>(e);
				if (ev->device()->type() == QTouchDevice::TouchScreen)
				{
					if (ev->type() != QEvent::TouchBegin || ev->touchPoints().isEmpty() || !childAt(
						mapFromGlobal(ev->touchPoints().cbegin()->screenPos().toPoint())))
					{
						touchEvent(ev);
						return true;
					}
				}
			}
			else if (e->type() == QEvent::Wheel)
			{
				QWheelEvent* ev = static_cast<QWheelEvent*>(e);
				if (ev->phase() == Qt::ScrollBegin)
				{
					_accumScroll = ev->angleDelta();
				}
				else
				{
					_accumScroll += ev->angleDelta();
					if (ev->phase() == Qt::ScrollEnd)
					{
						if (ev->orientation() == Qt::Horizontal)
						{
							if (_accumScroll.x() * _accumScroll.x() > _accumScroll.y() * _accumScroll.y() &&
								_accumScroll.x() != 0)
							{
								moveToNext(_accumScroll.x() > 0 ? -1 : 1);
							}
							_accumScroll = QPoint();
						}
					}
				}
			}
			return OverlayParent::eventHook(e);
		}

		bool OverlayWidget::eventFilter(QObject* obj, QEvent* e)
		{
			auto type = e->type();
			if (type == QEvent::ShortcutOverride)
			{
				const auto keyEvent = static_cast<QKeyEvent*>(e);
				const auto ctrl = keyEvent->modifiers().testFlag(Qt::ControlModifier);
				if (keyEvent->key() == Qt::Key_F && ctrl && _streamed)
				{
					playbackToggleFullScreen();
				}
				return true;
			}
			if ((type == QEvent::MouseMove || type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease) &&
				obj->isWidgetType())
			{
				if (isAncestorOf(static_cast<QWidget*>(obj)))
				{
					const auto mouseEvent = static_cast<QMouseEvent*>(e);
					const auto mousePosition = mapFromGlobal(mouseEvent->globalPos());
					const auto delta = (mousePosition - _lastMouseMovePos);
					auto activate = delta.manhattanLength() >= st::mediaviewDeltaFromLastAction;
					if (activate)
					{
						_lastMouseMovePos = mousePosition;
					}
					if (type == QEvent::MouseButtonPress)
					{
						_mousePressed = true;
						activate = true;
					}
					else if (type == QEvent::MouseButtonRelease)
					{
						_mousePressed = false;
						activate = true;
					}
					if (activate)
					{
						activateControls();
					}
				}
			}
			return OverlayParent::eventFilter(obj, e);
		}

		void OverlayWidget::setVisibleHook(bool visible)
		{
			if (!visible)
			{
				_sharedMedia = nullptr;
				_sharedMediaData = std::nullopt;
				_sharedMediaDataKey = std::nullopt;
				_userPhotos = nullptr;
				_userPhotosData = std::nullopt;
				_collage = nullptr;
				_collageData = std::nullopt;
				if (_menu) _menu->hideMenu(true);
				_controlsHideTimer.stop();
				_controlsState = ControlsShown;
				_controlsOpacity = anim::value(1, 1);
				_groupThumbs = nullptr;
				_groupThumbsRect = QRect();
#ifdef USE_OPENGL_OVERLAY_WIDGET
				// QOpenGLWidget can't properly destroy a child widget if
				// it is hidden exactly after that, so it must be repainted
		// before it is hidden without the child widget.
				if (!isHidden()) {
					_dropdown->hideFast();
					hideChildren();
					_wasRepainted = false;
					repaint();
					if (!_wasRepainted) {
						// Qt has some optimization to prevent too frequent repaints.
				// If the previous repaint was less than 1/60 second it silently
				// converts repaint() call to an update() call. But we have to
						// repaint right now, before hide(), with _streamingControls destroyed.
						auto event = QEvent(QEvent::UpdateRequest);
						QApplication::sendEvent(this, &event);
					}
				}
#endif // USE_OPENGL_OVERLAY_WIDGET
			}
			OverlayParent::setVisibleHook(visible);
			if (visible)
			{
				QCoreApplication::instance()->installEventFilter(this);
			}
			else
			{
				QCoreApplication::instance()->removeEventFilter(this);

				clearStreaming();
				destroyThemePreview();
				_radial.stop();
				_current = QPixmap();
				_themePreview = nullptr;
				_themeApply.destroyDelayed();
				_themeCancel.destroyDelayed();
				_themeShare.destroyDelayed();
			}
		}

		void OverlayWidget::onMenuDestroy(QObject* obj)
		{
			if (_menu == obj)
			{
				_menu = nullptr;
				activateControls();
			}
			_receiveMouse = false;
			QTimer::singleShot(0, this, SLOT(receiveMouse()));
		}

		void OverlayWidget::receiveMouse()
		{
			_receiveMouse = true;
		}

		void OverlayWidget::onDropdown()
		{
			updateActions();
			_dropdown->clearActions();
			for_const(auto & action, _actions)
			{
				_dropdown->addAction(action.text, this, action.member);
			}
			_dropdown->moveToRight(0, height() - _dropdown->height());
			_dropdown->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
			_dropdown->setFocus();
		}

		void OverlayWidget::onTouchTimer()
		{
			_touchRightButton = true;
		}

		void OverlayWidget::updateImage()
		{
			update(_saveMsg);
		}

		void OverlayWidget::findCurrent()
		{
			using namespace rpl::mappers;
			if (_sharedMediaData)
			{
				_index = _msgid
					         ? _sharedMediaData->indexOf(_msgid)
					         : _photo
					         ? _sharedMediaData->indexOf(_photo)
					         : std::nullopt;
				_fullIndex = _sharedMediaData->skippedBefore()
					             ? (_index | func::add(*_sharedMediaData->skippedBefore()))
					             : std::nullopt;
				_fullCount = _sharedMediaData->fullCount();
			}
			else if (_userPhotosData)
			{
				_index = _photo ? _userPhotosData->indexOf(_photo->id) : std::nullopt;
				_fullIndex = _userPhotosData->skippedBefore()
					             ? (_index | func::add(*_userPhotosData->skippedBefore()))
					             : std::nullopt;
				_fullCount = _userPhotosData->fullCount();
			}
			else if (_collageData)
			{
				const auto item = _photo ? WebPageCollage::Item(_photo) : _doc;
				const auto& items = _collageData->items;
				const auto i = ranges::find(items, item);
				_index = (i != end(items))
					         ? std::make_optional(int(i - begin(items)))
					         : std::nullopt;
				_fullIndex = _index;
				_fullCount = items.size();
			}
			else
			{
				_index = _fullIndex = _fullCount = std::nullopt;
			}
		}

		void OverlayWidget::updateHeader()
		{
			auto index = _fullIndex ? *_fullIndex : -1;
			auto count = _fullCount ? *_fullCount : -1;
			if (index >= 0 && index < count && count > 1)
			{
				if (_doc)
				{
					_headerText = tr::lng_mediaview_file_n_of_amount(
						tr::now,
						lt_file,
						(_doc->filename().isEmpty()
							 ? tr::lng_mediaview_doc_image(tr::now)
							 : _doc->filename()),
						lt_n,
						QString::number(index + 1),
						lt_amount,
						QString::number(count));
				}
				else
				{
					_headerText = tr::lng_mediaview_n_of_amount(
						tr::now,
						lt_n,
						QString::number(index + 1),
						lt_amount,
						QString::number(count));
				}
			}
			else
			{
				if (_doc)
				{
					_headerText = _doc->filename().isEmpty() ? tr::lng_mediaview_doc_image(tr::now) : _doc->filename();
				}
				else if (_msgid)
				{
					_headerText = tr::lng_mediaview_single_photo(tr::now);
				}
				else if (_user)
				{
					_headerText = tr::lng_mediaview_profile_photo(tr::now);
				}
				else if ((_history && _history->channelId() && !_history->isMegagroup())
					|| (_peer && _peer->isChannel() && !_peer->isMegagroup()))
				{
					_headerText = tr::lng_mediaview_channel_photo(tr::now);
				}
				else if (_peer)
				{
					_headerText = tr::lng_mediaview_group_photo(tr::now);
				}
				else
				{
					_headerText = tr::lng_mediaview_single_photo(tr::now);
				}
			}
			_headerHasLink = computeOverviewType() != std::nullopt;
			auto hwidth = st::mediaviewThickFont->width(_headerText);
			if (hwidth > width() / 3)
			{
				hwidth = width() / 3;
				_headerText = st::mediaviewThickFont->elided(_headerText, hwidth, Qt::ElideMiddle);
			}
			_headerNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewHeaderTop, hwidth,
			                       st::mediaviewThickFont->height);
		}

		float64 OverlayWidget::overLevel(OverState control) const
		{
			auto i = _animationOpacities.find(control);
			return (i == end(_animationOpacities))
				       ? (_over == control ? 1. : 0.)
				       : i->second.current();
		}
	} // namespace View
} // namespace Media
