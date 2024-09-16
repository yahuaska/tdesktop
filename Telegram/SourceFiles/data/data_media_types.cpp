/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_media_types.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_gif.h"
#include "history/view/media/history_view_video.h"
#include "history/view/media/history_view_document.h"
#include "history/view/media/history_view_contact.h"
#include "history/view/media/history_view_location.h"
#include "history/view/media/history_view_game.h"
#include "history/view/media/history_view_invoice.h"
#include "history/view/media/history_view_call.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/media/history_view_poll.h"
#include "history/view/media/history_view_theme_document.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "ui/text_options.h"
#include "ui/emoji_config.h"
#include "storage/storage_shared_media.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_game.h"
#include "data/data_web_page.h"
#include "data/data_poll.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "layout.h"
#include "storage/file_upload.h"
#include "app.h"

namespace Data {
namespace {

Call ComputeCallData(const MTPDmessageActionPhoneCall &call) {
	auto result = Call();
	result.finishReason = [&] {
		if (const auto reason = call.vreason()) {
			switch (reason->type()) {
			case mtpc_phoneCallDiscardReasonBusy:
				return CallFinishReason::Busy;
			case mtpc_phoneCallDiscardReasonDisconnect:
				return CallFinishReason::Disconnected;
			case mtpc_phoneCallDiscardReasonHangup:
				return CallFinishReason::Hangup;
			case mtpc_phoneCallDiscardReasonMissed:
				return CallFinishReason::Missed;
			}
			Unexpected("Call reason type.");
		}
		return CallFinishReason::Hangup;
	}();
	result.duration = call.vduration().value_or_empty();
	return result;
}

Invoice ComputeInvoiceData(
		not_null<HistoryItem*> item,
		const MTPDmessageMediaInvoice &data) {
	auto result = Invoice();
	result.isTest = data.is_test();
	result.amount = data.vtotal_amount().v;
	result.currency = qs(data.vcurrency());
	result.description = qs(data.vdescription());
	result.title = TextUtilities::SingleLine(qs(data.vtitle()));
	result.receiptMsgId = data.vreceipt_msg_id().value_or_empty();
	if (const auto photo = data.vphoto()) {
		result.photo = item->history()->owner().photoFromWeb(*photo);
	}
	return result;
}

QString WithCaptionDialogsText(
		const QString &attachType,
		const QString &caption) {
	if (caption.isEmpty()) {
		return textcmdLink(1, TextUtilities::Clean(attachType));
	}

	return tr::lng_dialogs_text_media(
		tr::now,
		lt_media_part,
		textcmdLink(1, tr::lng_dialogs_text_media_wrapped(
			tr::now,
			lt_media,
			TextUtilities::Clean(attachType))),
		lt_caption,
		TextUtilities::Clean(caption));
}

QString WithCaptionNotificationText(
		const QString &attachType,
		const QString &caption) {
	if (caption.isEmpty()) {
		return attachType;
	}

	return tr::lng_dialogs_text_media(
		tr::now,
		lt_media_part,
		tr::lng_dialogs_text_media_wrapped(
			tr::now,
			lt_media,
			attachType),
		lt_caption,
		caption);
}

} // namespace

TextForMimeData WithCaptionClipboardText(
		const QString &attachType,
		TextForMimeData &&caption) {
	auto result = TextForMimeData();
	result.reserve(5 + attachType.size() + caption.expanded.size());
	result.append(qstr("[ ")).append(attachType).append(qstr(" ]"));
	if (!caption.empty()) {
		result.append('\n').append(std::move(caption));
	}
	return result;
}

Media::Media(not_null<HistoryItem*> parent) : _parent(parent) {
}

not_null<HistoryItem*> Media::parent() const {
	return _parent;
}

DocumentData *Media::document() const {
	return nullptr;
}

PhotoData *Media::photo() const {
	return nullptr;
}

WebPageData *Media::webpage() const {
	return nullptr;
}

const SharedContact *Media::sharedContact() const {
	return nullptr;
}

const Call *Media::call() const {
	return nullptr;
}

GameData *Media::game() const {
	return nullptr;
}

const Invoice *Media::invoice() const {
	return nullptr;
}

LocationThumbnail *Media::location() const {
	return nullptr;
}

PollData *Media::poll() const {
	return nullptr;
}

bool Media::uploading() const {
	return false;
}

Storage::SharedMediaTypesMask Media::sharedMediaTypes() const {
	return {};
}

bool Media::canBeGrouped() const {
	return false;
}

QString Media::chatListText() const {
	auto result = notificationText();
	return result.isEmpty()
		? QString()
		: textcmdLink(1, TextUtilities::Clean(std::move(result)));
}

bool Media::hasReplyPreview() const {
	return false;
}

Image *Media::replyPreview() const {
	return nullptr;
}

bool Media::allowsForward() const {
	return true;
}

bool Media::allowsEdit() const {
	return allowsEditCaption();
}

bool Media::allowsEditCaption() const {
	return false;
}

bool Media::allowsEditMedia() const {
	return false;
}

bool Media::allowsRevoke() const {
	return true;
}

bool Media::forwardedBecomesUnread() const {
	return false;
}

QString Media::errorTextForForward(not_null<PeerData*> peer) const {
	return QString();
}

bool Media::consumeMessageText(const TextWithEntities &text) {
	return false;
}

TextWithEntities Media::consumedMessageText() const {
	return {};
}

std::unique_ptr<HistoryView::Media> Media::createView(
		not_null<HistoryView::Element*> message) {
	return createView(message, message->data());
}

MediaPhoto::MediaPhoto(
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo)
: Media(parent)
, _photo(photo) {
	parent->history()->owner().registerPhotoItem(_photo, parent);
}

MediaPhoto::MediaPhoto(
	not_null<HistoryItem*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo)
: Media(parent)
, _photo(photo)
, _chat(chat) {
	parent->history()->owner().registerPhotoItem(_photo, parent);
}

MediaPhoto::~MediaPhoto() {
	if (uploading() && !App::quitting()) {
		parent()->history()->session().uploader().cancel(parent()->fullId());
	}
	parent()->history()->owner().unregisterPhotoItem(_photo, parent());
}

std::unique_ptr<Media> MediaPhoto::clone(not_null<HistoryItem*> parent) {
	return _chat
		? std::make_unique<MediaPhoto>(parent, _chat, _photo)
		: std::make_unique<MediaPhoto>(parent, _photo);
}

PhotoData *MediaPhoto::photo() const {
	return _photo;
}

bool MediaPhoto::uploading() const {
	return _photo->uploading();
}

Storage::SharedMediaTypesMask MediaPhoto::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_chat) {
		return Type::ChatPhoto;
	}
	return Storage::SharedMediaTypesMask{}
		.added(Type::Photo)
		.added(Type::PhotoVideo);
}

bool MediaPhoto::canBeGrouped() const {
	return true;
}

bool MediaPhoto::hasReplyPreview() const {
	return !_photo->isNull();
}

Image *MediaPhoto::replyPreview() const {
	return _photo->getReplyPreview(parent()->fullId());
}

QString MediaPhoto::notificationText() const {
	return WithCaptionNotificationText(
		tr::lng_in_dlg_photo(tr::now),
		parent()->originalText().text);
}

QString MediaPhoto::chatListText() const {
	return WithCaptionDialogsText(
		tr::lng_in_dlg_photo(tr::now),
		parent()->originalText().text);
}

QString MediaPhoto::pinnedTextSubstring() const {
	return tr::lng_action_pinned_media_photo(tr::now);
}

TextForMimeData MediaPhoto::clipboardText() const {
	return WithCaptionClipboardText(
		tr::lng_in_dlg_photo(tr::now),
		parent()->clipboardText());
}

bool MediaPhoto::allowsEditCaption() const {
	return true;
}

bool MediaPhoto::allowsEditMedia() const {
	return true;
}

QString MediaPhoto::errorTextForForward(not_null<PeerData*> peer) const {
	return Data::RestrictionError(
		peer,
		ChatRestriction::f_send_media
	).value_or(QString());
}

bool MediaPhoto::updateInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaPhoto) {
		return false;
	}
	const auto &data = media.c_messageMediaPhoto();
	const auto content = data.vphoto();
	if (content && !data.vttl_seconds()) {
		const auto photo = parent()->history()->owner().processPhoto(
			*content);
		if (photo == _photo) {
			return true;
		} else {
			photo->collectLocalData(_photo);
		}
	} else {
		LOG(("API Error: "
			"Got MTPMessageMediaPhoto without photo "
			"or with ttl_seconds in updateInlineResultMedia()"));
	}
	return false;
}

bool MediaPhoto::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaPhoto) {
		return false;
	}
	const auto &mediaPhoto = media.c_messageMediaPhoto();
	const auto content = mediaPhoto.vphoto();
	if (!content || mediaPhoto.vttl_seconds()) {
		LOG(("Api Error: "
			"Got MTPMessageMediaPhoto without photo "
			"or with ttl_seconds in updateSentMedia()"));
		return false;
	}
	parent()->history()->owner().photoConvert(_photo, *content);

	if (content->type() != mtpc_photo) {
		return false;
	}
	const auto &photo = content->c_photo();

	struct SizeData {
		MTPstring type = MTP_string();
		int width = 0;
		int height = 0;
		QByteArray bytes;
	};
	const auto saveImageToCache = [&](
			not_null<Image*> image,
			SizeData size) {
		Expects(!size.type.v.isEmpty());

		const auto key = StorageImageLocation(
			StorageFileLocation(
				photo.vdc_id().v,
				_photo->session().userId(),
				MTP_inputPhotoFileLocation(
					photo.vid(),
					photo.vaccess_hash(),
					photo.vfile_reference(),
					size.type)),
			size.width,
			size.height);
		if (!key.valid() || image->isNull() || !image->loaded()) {
			return;
		}
		if (size.bytes.isEmpty()) {
			size.bytes = image->bytesForCache();
		}
		const auto length = size.bytes.size();
		if (!length || length > Storage::kMaxFileInMemory) {
			LOG(("App Error: Bad photo data for saving to cache."));
			return;
		}
		parent()->history()->owner().cache().putIfEmpty(
			key.file().cacheKey(),
			Storage::Cache::Database::TaggedValue(
				std::move(size.bytes),
				Data::kImageCacheTag));
		image->replaceSource(
			std::make_unique<Images::StorageSource>(key, length));
	};
	auto &sizes = photo.vsizes().v;
	auto max = 0;
	auto maxSize = SizeData();
	for (const auto &data : sizes) {
		const auto size = data.match([](const MTPDphotoSize &data) {
			return SizeData{
				data.vtype(),
				data.vw().v,
				data.vh().v,
				QByteArray()
			};
		}, [](const MTPDphotoCachedSize &data) {
			return SizeData{
				data.vtype(),
				data.vw().v,
				data.vh().v,
				qba(data.vbytes())
			};
		}, [](const MTPDphotoSizeEmpty &) {
			return SizeData();
		}, [](const MTPDphotoStrippedSize &data) {
			// No need to save stripped images to local cache.
			return SizeData();
		});
		const auto letter = size.type.v.isEmpty() ? char(0) : size.type.v[0];
		if (!letter) {
			continue;
		}
		if (letter == 's') {
			saveImageToCache(_photo->thumbnailSmall(), size);
		} else if (letter == 'm') {
			saveImageToCache(_photo->thumbnail(), size);
		} else if (letter == 'x' && max < 1) {
			max = 1;
			maxSize = size;
		} else if (letter == 'y' && max < 2) {
			max = 2;
			maxSize = size;
		//} else if (letter == 'w' && max < 3) {
		//	max = 3;
		//	maxSize = size;
		}
	}
	if (!maxSize.type.v.isEmpty()) {
		saveImageToCache(_photo->large(), maxSize);
	}
	return true;
}

std::unique_ptr<HistoryView::Media> MediaPhoto::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	if (_chat) {
		return std::make_unique<HistoryView::Photo>(
			message,
			_chat,
			_photo,
			st::msgServicePhotoWidth);
	}
	return std::make_unique<HistoryView::Photo>(
		message,
		realParent,
		_photo);
}

MediaFile::MediaFile(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document)
: Media(parent)
, _document(document)
, _emoji(document->sticker() ? document->sticker()->alt : QString()) {
	parent->history()->owner().registerDocumentItem(_document, parent);

	if (!_emoji.isEmpty()) {
		if (const auto emoji = Ui::Emoji::Find(_emoji)) {
			_emoji = emoji->text();
		}
	}
}

MediaFile::~MediaFile() {
	if (uploading() && !App::quitting()) {
		parent()->history()->session().uploader().cancel(parent()->fullId());
	}
	parent()->history()->owner().unregisterDocumentItem(
		_document,
		parent());
}

std::unique_ptr<Media> MediaFile::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaFile>(parent, _document);
}

DocumentData *MediaFile::document() const {
	return _document;
}

bool MediaFile::uploading() const {
	return _document->uploading();
}

Storage::SharedMediaTypesMask MediaFile::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_document->sticker()) {
		return {};
	} else if (_document->isVideoMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::RoundFile)
			.added(Type::RoundVoiceFile);
	} else if (_document->isGifv()) {
		return Type::GIF;
	} else if (_document->isVideoFile()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::Video)
			.added(Type::PhotoVideo);
	} else if (_document->isVoiceMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::VoiceFile)
			.added(Type::RoundVoiceFile);
	} else if (_document->isSharedMediaMusic()) {
		return Type::MusicFile;
	}
	return Type::File;
}

bool MediaFile::canBeGrouped() const {
	return _document->isVideoFile();
}

bool MediaFile::hasReplyPreview() const {
	return _document->hasThumbnail();
}

Image *MediaFile::replyPreview() const {
	return _document->getReplyPreview(parent()->fullId());
}

QString MediaFile::chatListText() const {
	if (const auto sticker = _document->sticker()) {
		return Media::chatListText();
	}
	const auto type = [&] {
		if (_document->isVideoMessage()) {
			return tr::lng_in_dlg_video_message(tr::now);
		} else if (_document->isAnimation()) {
			return qsl("GIF");
		} else if (_document->isVideoFile()) {
			return tr::lng_in_dlg_video(tr::now);
		} else if (_document->isVoiceMessage()) {
			return tr::lng_in_dlg_audio(tr::now);
		} else if (const auto name = _document->composeNameString();
				!name.isEmpty()) {
			return name;
		} else if (_document->isAudioFile()) {
			return tr::lng_in_dlg_audio_file(tr::now);
		}
		return tr::lng_in_dlg_file(tr::now);
	}();
	return WithCaptionDialogsText(type, parent()->originalText().text);
}

QString MediaFile::notificationText() const {
	if (const auto sticker = _document->sticker()) {
		return _emoji.isEmpty()
			? tr::lng_in_dlg_sticker(tr::now)
			: tr::lng_in_dlg_sticker_emoji(tr::now, lt_emoji, _emoji);
	}
	const auto type = [&] {
		if (_document->isVideoMessage()) {
			return tr::lng_in_dlg_video_message(tr::now);
		} else if (_document->isAnimation()) {
			return qsl("GIF");
		} else if (_document->isVideoFile()) {
			return tr::lng_in_dlg_video(tr::now);
		} else if (_document->isVoiceMessage()) {
			return tr::lng_in_dlg_audio(tr::now);
		} else if (!_document->filename().isEmpty()) {
			return _document->filename();
		} else if (_document->isAudioFile()) {
			return tr::lng_in_dlg_audio_file(tr::now);
		}
		return tr::lng_in_dlg_file(tr::now);
	}();
	return WithCaptionNotificationText(type, parent()->originalText().text);
}

QString MediaFile::pinnedTextSubstring() const {
	if (const auto sticker = _document->sticker()) {
		if (!_emoji.isEmpty()) {
			return tr::lng_action_pinned_media_emoji_sticker(
				tr::now,
				lt_emoji,
				_emoji);
		}
		return tr::lng_action_pinned_media_sticker(tr::now);
	} else if (_document->isAnimation()) {
		if (_document->isVideoMessage()) {
			return tr::lng_action_pinned_media_video_message(tr::now);
		}
		return tr::lng_action_pinned_media_gif(tr::now);
	} else if (_document->isVideoFile()) {
		return tr::lng_action_pinned_media_video(tr::now);
	} else if (_document->isVoiceMessage()) {
		return tr::lng_action_pinned_media_voice(tr::now);
	} else if (_document->isSong()) {
		return tr::lng_action_pinned_media_audio(tr::now);
	}
	return tr::lng_action_pinned_media_file(tr::now);
}

TextForMimeData MediaFile::clipboardText() const {
	const auto attachType = [&] {
		const auto name = _document->composeNameString();
		const auto addName = !name.isEmpty()
			? qstr(" : ") + name
			: QString();
		if (const auto sticker = _document->sticker()) {
			if (!_emoji.isEmpty()) {
				return tr::lng_in_dlg_sticker_emoji(
					tr::now,
					lt_emoji,
					_emoji);
			}
			return tr::lng_in_dlg_sticker(tr::now);
		} else if (_document->isAnimation()) {
			if (_document->isVideoMessage()) {
				return tr::lng_in_dlg_video_message(tr::now);
			}
			return qsl("GIF");
		} else if (_document->isVideoFile()) {
			return tr::lng_in_dlg_video(tr::now);
		} else if (_document->isVoiceMessage()) {
			return tr::lng_in_dlg_audio(tr::now) + addName;
		} else if (_document->isSong()) {
			return tr::lng_in_dlg_audio_file(tr::now) + addName;
		}
		return tr::lng_in_dlg_file(tr::now) + addName;
	}();
	return WithCaptionClipboardText(
		attachType,
		parent()->clipboardText());
}

bool MediaFile::allowsEditCaption() const {
	return !_document->isVideoMessage() && !_document->sticker();
}

bool MediaFile::allowsEditMedia() const {
	return !_document->isVideoMessage()
		&& !_document->sticker()
		&& !_document->isVoiceMessage();
}

bool MediaFile::forwardedBecomesUnread() const {
	return _document->isVoiceMessage()
		//|| _document->isVideoFile()
		|| _document->isVideoMessage();
}

QString MediaFile::errorTextForForward(not_null<PeerData*> peer) const {
	if (const auto sticker = _document->sticker()) {
		if (const auto error = Data::RestrictionError(
				peer,
				ChatRestriction::f_send_stickers)) {
			return *error;
		}
	} else if (_document->isAnimation()) {
		if (_document->isVideoMessage()) {
			if (const auto error = Data::RestrictionError(
					peer,
					ChatRestriction::f_send_media)) {
				return *error;
			}
		} else {
			if (const auto error = Data::RestrictionError(
					peer,
					ChatRestriction::f_send_gifs)) {
				return *error;
			}
		}
	} else if (const auto error = Data::RestrictionError(
			peer,
			ChatRestriction::f_send_media)) {
		return *error;
	}
	return QString();
}

bool MediaFile::updateInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaDocument) {
		return false;
	}
	const auto &data = media.c_messageMediaDocument();
	const auto content = data.vdocument();
	if (content && !data.vttl_seconds()) {
		const auto document = parent()->history()->owner().processDocument(
			*content);
		if (document == _document) {
			return false;
		} else {
			document->collectLocalData(_document);
		}
	} else {
		LOG(("API Error: "
			"Got MTPMessageMediaDocument without document "
			"or with ttl_seconds in updateInlineResultMedia()"));
	}
	return false;
}

bool MediaFile::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaDocument) {
		return false;
	}
	const auto &data = media.c_messageMediaDocument();
	const auto content = data.vdocument();
	if (!content || data.vttl_seconds()) {
		LOG(("Api Error: "
			"Got MTPMessageMediaDocument without document "
			"or with ttl_seconds in updateSentMedia()"));
		return false;
	}
	parent()->history()->owner().documentConvert(_document, *content);

	if (const auto good = _document->goodThumbnail()) {
		auto bytes = good->bytesForCache();
		if (const auto length = bytes.size()) {
			if (length > Storage::kMaxFileInMemory) {
				LOG(("App Error: Bad thumbnail data for saving to cache."));
			} else {
				parent()->history()->owner().cache().putIfEmpty(
					_document->goodThumbnailCacheKey(),
					Storage::Cache::Database::TaggedValue(
						std::move(bytes),
						Data::kImageCacheTag));
				_document->refreshGoodThumbnail();
			}
		}
	}

	return true;
}

std::unique_ptr<HistoryView::Media> MediaFile::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	if (_document->sticker()) {
		return std::make_unique<HistoryView::UnwrappedMedia>(
			message,
			std::make_unique<HistoryView::Sticker>(message, _document));
	} else if (_document->isAnimation()) {
		return std::make_unique<HistoryView::Gif>(message, _document);
	} else if (_document->isVideoFile()) {
		return std::make_unique<HistoryView::Video>(
			message,
			realParent,
			_document);
	} else if (_document->isTheme() && _document->hasThumbnail()) {
		return std::make_unique<HistoryView::ThemeDocument>(
			message,
			_document);
	}
	return std::make_unique<HistoryView::Document>(message, _document);
}

MediaContact::MediaContact(
	not_null<HistoryItem*> parent,
	UserId userId,
	const QString &firstName,
	const QString &lastName,
	const QString &phoneNumber)
: Media(parent) {
	parent->history()->owner().registerContactItem(userId, parent);

	_contact.userId = userId;
	_contact.firstName = firstName;
	_contact.lastName = lastName;
	_contact.phoneNumber = phoneNumber;
}

MediaContact::~MediaContact() {
	parent()->history()->owner().unregisterContactItem(
		_contact.userId,
		parent());
}

std::unique_ptr<Media> MediaContact::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaContact>(
		parent,
		_contact.userId,
		_contact.firstName,
		_contact.lastName,
		_contact.phoneNumber);
}

const SharedContact *MediaContact::sharedContact() const {
	return &_contact;
}

QString MediaContact::notificationText() const {
	return tr::lng_in_dlg_contact(tr::now);
}

QString MediaContact::pinnedTextSubstring() const {
	return tr::lng_action_pinned_media_contact(tr::now);
}

TextForMimeData MediaContact::clipboardText() const {
	const auto text = qsl("[ ")
		+ tr::lng_in_dlg_contact(tr::now)
		+ qsl(" ]\n")
		+ tr::lng_full_name(
			tr::now,
			lt_first_name,
			_contact.firstName,
			lt_last_name,
			_contact.lastName).trimmed()
		+ '\n'
		+ _contact.phoneNumber;
	return TextForMimeData::Simple(text);
}

bool MediaContact::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaContact::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaContact) {
		return false;
	}
	if (_contact.userId != media.c_messageMediaContact().vuser_id().v) {
		parent()->history()->owner().unregisterContactItem(
			_contact.userId,
			parent());
		_contact.userId = media.c_messageMediaContact().vuser_id().v;
		parent()->history()->owner().registerContactItem(
			_contact.userId,
			parent());
	}
	return true;
}

std::unique_ptr<HistoryView::Media> MediaContact::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryView::Contact>(
		message,
		_contact.userId,
		_contact.firstName,
		_contact.lastName,
		_contact.phoneNumber);
}

MediaLocation::MediaLocation(
	not_null<HistoryItem*> parent,
	const LocationPoint &point)
: MediaLocation(parent, point, QString(), QString()) {
}

MediaLocation::MediaLocation(
	not_null<HistoryItem*> parent,
	const LocationPoint &point,
	const QString &title,
	const QString &description)
: Media(parent)
, _location(parent->history()->owner().location(point))
, _title(title)
, _description(description) {
}

std::unique_ptr<Media> MediaLocation::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaLocation>(
		parent,
		_location->point,
		_title,
		_description);
}

LocationThumbnail *MediaLocation::location() const {
	return _location;
}

QString MediaLocation::chatListText() const {
	return WithCaptionDialogsText(tr::lng_maps_point(tr::now), _title);
}

QString MediaLocation::notificationText() const {
	return WithCaptionNotificationText(tr::lng_maps_point(tr::now), _title);
}

QString MediaLocation::pinnedTextSubstring() const {
	return tr::lng_action_pinned_media_location(tr::now);
}

TextForMimeData MediaLocation::clipboardText() const {
	auto result = TextForMimeData::Simple(
		qstr("[ ") + tr::lng_maps_point(tr::now) + qstr(" ]\n"));
	auto titleResult = TextUtilities::ParseEntities(
		TextUtilities::Clean(_title),
		Ui::WebpageTextTitleOptions().flags);
	auto descriptionResult = TextUtilities::ParseEntities(
		TextUtilities::Clean(_description),
		TextParseLinks | TextParseMultiline | TextParseRichText);
	if (!titleResult.empty()) {
		result.append(std::move(titleResult));
	}
	if (!descriptionResult.text.isEmpty()) {
		result.append(std::move(descriptionResult));
	}
	result.append(LocationClickHandler(_location->point).dragText());
	return result;
}

bool MediaLocation::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaLocation::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaLocation::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryView::Location>(
		message,
		_location,
		_title,
		_description);
}

MediaCall::MediaCall(
	not_null<HistoryItem*> parent,
	const MTPDmessageActionPhoneCall &call)
: Media(parent)
, _call(ComputeCallData(call)) {
}

std::unique_ptr<Media> MediaCall::clone(not_null<HistoryItem*> parent) {
	Unexpected("Clone of call media.");
}

const Call *MediaCall::call() const {
	return &_call;
}

QString MediaCall::notificationText() const {
	auto result = Text(parent(), _call.finishReason);
	if (_call.duration > 0) {
		result = tr::lng_call_type_and_duration(
			tr::now,
			lt_type,
			result,
			lt_duration,
			formatDurationWords(_call.duration));
	}
	return result;
}

QString MediaCall::pinnedTextSubstring() const {
	return QString();
}

TextForMimeData MediaCall::clipboardText() const {
	return TextForMimeData::Simple(
		qstr("[ ") + notificationText() + qstr(" ]"));
}

bool MediaCall::allowsForward() const {
	return false;
}

bool MediaCall::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaCall::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaCall::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryView::Call>(message, &_call);
}

QString MediaCall::Text(
		not_null<HistoryItem*> item,
		CallFinishReason reason) {
	if (item->out()) {
		return (reason == CallFinishReason::Missed)
			? tr::lng_call_cancelled(tr::now)
			: tr::lng_call_outgoing(tr::now);
	} else if (reason == CallFinishReason::Missed) {
		return tr::lng_call_missed(tr::now);
	} else if (reason == CallFinishReason::Busy) {
		return tr::lng_call_declined(tr::now);
	}
	return tr::lng_call_incoming(tr::now);
}

MediaWebPage::MediaWebPage(
	not_null<HistoryItem*> parent,
	not_null<WebPageData*> page)
: Media(parent)
, _page(page) {
	parent->history()->owner().registerWebPageItem(_page, parent);
}

MediaWebPage::~MediaWebPage() {
	parent()->history()->owner().unregisterWebPageItem(_page, parent());
}

std::unique_ptr<Media> MediaWebPage::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaWebPage>(parent, _page);
}

DocumentData *MediaWebPage::document() const {
	return _page->document;
}

PhotoData *MediaWebPage::photo() const {
	return _page->photo;
}

WebPageData *MediaWebPage::webpage() const {
	return _page;
}

bool MediaWebPage::hasReplyPreview() const {
	if (const auto document = MediaWebPage::document()) {
		return document->hasThumbnail() && !document->isPatternWallPaper();
	} else if (const auto photo = MediaWebPage::photo()) {
		return !photo->isNull();
	}
	return false;
}

Image *MediaWebPage::replyPreview() const {
	if (const auto document = MediaWebPage::document()) {
		return document->getReplyPreview(parent()->fullId());
	} else if (const auto photo = MediaWebPage::photo()) {
		return photo->getReplyPreview(parent()->fullId());
	}
	return nullptr;
}

QString MediaWebPage::chatListText() const {
	return notificationText();
}

QString MediaWebPage::notificationText() const {
	return parent()->originalText().text;
}

QString MediaWebPage::pinnedTextSubstring() const {
	return QString();
}

TextForMimeData MediaWebPage::clipboardText() const {
	return TextForMimeData();
}

bool MediaWebPage::allowsEdit() const {
	return true;
}

bool MediaWebPage::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaWebPage::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaWebPage::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryView::WebPage>(message, _page);
}

MediaGame::MediaGame(
	not_null<HistoryItem*> parent,
	not_null<GameData*> game)
: Media(parent)
, _game(game) {
}

std::unique_ptr<Media> MediaGame::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaGame>(parent, _game);
}

bool MediaGame::hasReplyPreview() const {
	if (const auto document = _game->document) {
		return document->hasThumbnail();
	} else if (const auto photo = _game->photo) {
		return !photo->isNull();
	}
	return false;
}

Image *MediaGame::replyPreview() const {
	if (const auto document = _game->document) {
		return document->getReplyPreview(parent()->fullId());
	} else if (const auto photo = _game->photo) {
		return photo->getReplyPreview(parent()->fullId());
	}
	return nullptr;
}

QString MediaGame::notificationText() const {
	// Add a game controller emoji before game title.
	auto result = QString();
	result.reserve(_game->title.size() + 3);
	result.append(
		QChar(0xD83C)
	).append(
		QChar(0xDFAE)
	).append(
		QChar(' ')
	).append(_game->title);
	return result;
}

GameData *MediaGame::game() const {
	return _game;
}

QString MediaGame::pinnedTextSubstring() const {
	const auto title = _game->title;
	return tr::lng_action_pinned_media_game(tr::now, lt_game, title);
}

TextForMimeData MediaGame::clipboardText() const {
	return TextForMimeData();
}

QString MediaGame::errorTextForForward(not_null<PeerData*> peer) const {
	return Data::RestrictionError(
		peer,
		ChatRestriction::f_send_games
	).value_or(QString());
}

bool MediaGame::consumeMessageText(const TextWithEntities &text) {
	_consumedText = text;
	return true;
}

TextWithEntities MediaGame::consumedMessageText() const {
	return _consumedText;
}

bool MediaGame::updateInlineResultMedia(const MTPMessageMedia &media) {
	return updateSentMedia(media);
}

bool MediaGame::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaGame) {
		return false;
	}
	parent()->history()->owner().gameConvert(
		_game, media.c_messageMediaGame().vgame());
	return true;
}

std::unique_ptr<HistoryView::Media> MediaGame::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryView::Game>(
		message,
		_game,
		_consumedText);
}

MediaInvoice::MediaInvoice(
	not_null<HistoryItem*> parent,
	const MTPDmessageMediaInvoice &data)
: Media(parent)
, _invoice(ComputeInvoiceData(parent, data)) {
}

MediaInvoice::MediaInvoice(
	not_null<HistoryItem*> parent,
	const Invoice &data)
: Media(parent)
, _invoice(data) {
}

std::unique_ptr<Media> MediaInvoice::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaInvoice>(parent, _invoice);
}

const Invoice *MediaInvoice::invoice() const {
	return &_invoice;
}

bool MediaInvoice::hasReplyPreview() const {
	if (const auto photo = _invoice.photo) {
		return !photo->isNull();
	}
	return false;
}

Image *MediaInvoice::replyPreview() const {
	if (const auto photo = _invoice.photo) {
		return photo->getReplyPreview(parent()->fullId());
	}
	return nullptr;
}

QString MediaInvoice::notificationText() const {
	return _invoice.title;
}

QString MediaInvoice::pinnedTextSubstring() const {
	return QString();
}

TextForMimeData MediaInvoice::clipboardText() const {
	return TextForMimeData();
}

bool MediaInvoice::updateInlineResultMedia(const MTPMessageMedia &media) {
	return true;
}

bool MediaInvoice::updateSentMedia(const MTPMessageMedia &media) {
	return true;
}

std::unique_ptr<HistoryView::Media> MediaInvoice::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryView::Invoice>(message, &_invoice);
}

MediaPoll::MediaPoll(
	not_null<HistoryItem*> parent,
	not_null<PollData*> poll)
: Media(parent)
, _poll(poll) {
}

MediaPoll::~MediaPoll() {
}

std::unique_ptr<Media> MediaPoll::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaPoll>(parent, _poll);
}

PollData *MediaPoll::poll() const {
	return _poll;
}

QString MediaPoll::notificationText() const {
	return _poll->question;
}

QString MediaPoll::pinnedTextSubstring() const {
	return QChar(171) + _poll->question + QChar(187);
}

TextForMimeData MediaPoll::clipboardText() const {
	const auto text = qstr("[ ")
		+ tr::lng_in_dlg_poll(tr::now)
		+ qstr(" : ")
		+ _poll->question
		+ qstr(" ]")
		+ ranges::accumulate(
			ranges::view::all(
				_poll->answers
			) | ranges::view::transform([](const PollAnswer &answer) {
				return "\n- " + answer.text;
			}),
			QString());
	return TextForMimeData::Simple(text);
}

QString MediaPoll::errorTextForForward(not_null<PeerData*> peer) const {
	return Data::RestrictionError(
		peer,
		ChatRestriction::f_send_polls
	).value_or(QString());
}

bool MediaPoll::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaPoll::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaPoll::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryView::Poll>(message, _poll);
}

} // namespace Data
