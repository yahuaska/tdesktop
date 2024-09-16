/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_emoji_pack.h"

#include "history/history_item.h"
#include "lottie/lottie_common.h"
#include "ui/emoji_config.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/image/image_source.h"
#include "main/main_session.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "base/call_delayed.h"
#include "apiwrap.h"
#include "app.h"
#include "styles/style_history.h"

#include <QtCore/QBuffer>

namespace Stickers {
namespace details {

using UniversalImages = Ui::Emoji::UniversalImages;

class EmojiImageLoader {
public:
	EmojiImageLoader(
		crl::weak_on_queue<EmojiImageLoader> weak,
		std::shared_ptr<UniversalImages> images,
		bool largeEnabled);

	[[nodiscard]] QImage prepare(EmojiPtr emoji);
	void switchTo(std::shared_ptr<UniversalImages> images);
	std::shared_ptr<UniversalImages> releaseImages();

private:
	crl::weak_on_queue<EmojiImageLoader> _weak;
	std::shared_ptr<UniversalImages> _images;

};

namespace {

constexpr auto kRefreshTimeout = 7200 * crl::time(1000);
constexpr auto kClearSourceTimeout = 10 * crl::time(1000);

[[nodiscard]] QSize SingleSize() {
	const auto single = st::largeEmojiSize;
	const auto outline = st::largeEmojiOutline;
	return QSize(
		2 * outline + single,
		2 * outline + single
	) * cIntRetinaFactor();
}

[[nodiscard]] const Lottie::ColorReplacements *ColorReplacements(int index) {
	Expects(index >= 1 && index <= 5);

	static const auto color1 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0xca907aU },
			{ 0xffb139U, 0xedc5a5U },
			{ 0xffd140U, 0xf7e3c3U },
			{ 0xffdf79U, 0xfbefd6U },
		},
		1,
	};
	static const auto color2 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0xaa7c60U },
			{ 0xffb139U, 0xc8a987U },
			{ 0xffd140U, 0xddc89fU },
			{ 0xffdf79U, 0xe6d6b2U },
		},
		2,
	};
	static const auto color3 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0x8c6148U },
			{ 0xffb139U, 0xad8562U },
			{ 0xffd140U, 0xc49e76U },
			{ 0xffdf79U, 0xd4b188U },
		},
		3,
	};
	static const auto color4 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0x6e3c2cU },
			{ 0xffb139U, 0x925a34U },
			{ 0xffd140U, 0xa16e46U },
			{ 0xffdf79U, 0xac7a52U },
		},
		4,
	};
	static const auto color5 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0x291c12U },
			{ 0xffb139U, 0x472a22U },
			{ 0xffd140U, 0x573b30U },
			{ 0xffdf79U, 0x68493cU },
		},
		5,
	};
	static const auto list = std::array{
		&color1,
		&color2,
		&color3,
		&color4,
		&color5,
	};
	return list[index - 1];
}

class ImageSource : public Images::Source {
public:
	explicit ImageSource(
		EmojiPtr emoji,
		not_null<crl::object_on_queue<EmojiImageLoader>*> loader);

	void load(Data::FileOrigin origin) override;
	void loadEvenCancelled(Data::FileOrigin origin) override;
	QImage takeLoaded() override;
	void unload() override;

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override;
	void automaticLoadSettingsChanged() override;

	bool loading() override;
	bool displayLoading() override;
	void cancel() override;
	float64 progress() override;
	int loadOffset() override;

	const StorageImageLocation &location() override;
	void refreshFileReference(const QByteArray &data) override;
	std::optional<Storage::Cache::Key> cacheKey() override;
	void setDelayedStorageLocation(
		const StorageImageLocation &location) override;
	void performDelayedLoad(Data::FileOrigin origin) override;
	bool isDelayedStorageImage() const override;
	void setImageBytes(const QByteArray &bytes) override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

	QByteArray bytesForCache() override;

private:
	// While HistoryView::Element-s are almost never destroyed
	// we make loading of the image lazy.
	not_null<crl::object_on_queue<EmojiImageLoader>*> _loader;
	EmojiPtr _emoji = nullptr;
	QImage _data;
	QByteArray _format;
	QByteArray _bytes;
	QSize _size;
	base::binary_guard _loading;

};

ImageSource::ImageSource(
	EmojiPtr emoji,
	not_null<crl::object_on_queue<EmojiImageLoader>*> loader)
: _loader(loader)
, _emoji(emoji)
, _size(SingleSize()) {
}

void ImageSource::load(Data::FileOrigin origin) {
	if (!_data.isNull()) {
		return;
	}
	if (_bytes.isEmpty()) {
		_loader->with([
			this,
			emoji = _emoji,
			guard = _loading.make_guard()
		](EmojiImageLoader &loader) mutable {
			if (!guard) {
				return;
			}
			crl::on_main(std::move(guard), [this, image = loader.prepare(emoji)]{
				_data = image;
				Auth().downloaderTaskFinished().notify();
			});
		});
	} else {
		_data = App::readImage(_bytes, &_format, false);
	}
}

void ImageSource::loadEvenCancelled(Data::FileOrigin origin) {
	load(origin);
}

QImage ImageSource::takeLoaded() {
	load({});
	return _data;
}

void ImageSource::unload() {
	if (_bytes.isEmpty() && !_data.isNull()) {
		if (_format != "JPG") {
			_format = "PNG";
		}
		{
			QBuffer buffer(&_bytes);
			_data.save(&buffer, _format);
		}
		Assert(!_bytes.isEmpty());
	}
	_data = QImage();
}

void ImageSource::automaticLoad(
	Data::FileOrigin origin,
	const HistoryItem *item) {
}

void ImageSource::automaticLoadSettingsChanged() {
}

bool ImageSource::loading() {
	return _data.isNull() && _bytes.isEmpty();
}

bool ImageSource::displayLoading() {
	return false;
}

void ImageSource::cancel() {
}

float64 ImageSource::progress() {
	return 1.;
}

int ImageSource::loadOffset() {
	return 0;
}

const StorageImageLocation &ImageSource::location() {
	return StorageImageLocation::Invalid();
}

void ImageSource::refreshFileReference(const QByteArray &data) {
}

std::optional<Storage::Cache::Key> ImageSource::cacheKey() {
	return std::nullopt;
}

void ImageSource::setDelayedStorageLocation(
	const StorageImageLocation &location) {
}

void ImageSource::performDelayedLoad(Data::FileOrigin origin) {
}

bool ImageSource::isDelayedStorageImage() const {
	return false;
}

void ImageSource::setImageBytes(const QByteArray &bytes) {
}

int ImageSource::width() {
	return _size.width();
}

int ImageSource::height() {
	return _size.height();
}

int ImageSource::bytesSize() {
	return _bytes.size();
}

void ImageSource::setInformation(int size, int width, int height) {
	if (width && height) {
		_size = QSize(width, height);
	}
}

QByteArray ImageSource::bytesForCache() {
	auto result = QByteArray();
	{
		QBuffer buffer(&result);
		if (!_data.save(&buffer, _format)) {
			if (_data.save(&buffer, "PNG")) {
				_format = "PNG";
			}
		}
	}
	return result;
}

} // namespace

EmojiImageLoader::EmojiImageLoader(
	crl::weak_on_queue<EmojiImageLoader> weak,
	std::shared_ptr<UniversalImages> images,
	bool largeEnabled)
: _weak(std::move(weak))
, _images(std::move(images)) {
	Expects(_images != nullptr);

	if (largeEnabled) {
		_images->ensureLoaded();
	}
}

QImage EmojiImageLoader::prepare(EmojiPtr emoji) {
	const auto loaded = _images->ensureLoaded();
	const auto factor = cIntRetinaFactor();
	const auto side = st::largeEmojiSize + 2 * st::largeEmojiOutline;
	auto tinted = QImage(
		QSize(st::largeEmojiSize, st::largeEmojiSize) * factor,
		QImage::Format_ARGB32_Premultiplied);
	tinted.fill(Qt::white);
	if (loaded) {
		QPainter p(&tinted);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		_images->draw(
			p,
			emoji,
			st::largeEmojiSize * factor,
			0,
			0);
	}
	auto result = QImage(
		QSize(side, side) * factor,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	if (loaded) {
		QPainter p(&result);
		const auto delta = st::largeEmojiOutline * factor;
		const auto planar = std::array<QPoint, 4>{ {
			{ 0, -1 },
			{ -1, 0 },
			{ 1, 0 },
			{ 0, 1 },
		} };
		for (const auto &shift : planar) {
			for (auto i = 0; i != delta; ++i) {
				p.drawImage(QPoint(delta, delta) + shift * (i + 1), tinted);
			}
		}
		const auto diagonal = std::array<QPoint, 4>{ {
			{ -1, -1 },
			{ 1, -1 },
			{ -1, 1 },
			{ 1, 1 },
		} };
		const auto corrected = int(std::round(delta / sqrt(2.)));
		for (const auto &shift : diagonal) {
			for (auto i = 0; i != corrected; ++i) {
				p.drawImage(QPoint(delta, delta) + shift * (i + 1), tinted);
			}
		}
		_images->draw(
			p,
			emoji,
			st::largeEmojiSize * factor,
			delta,
			delta);
	}
	return result;
}

void EmojiImageLoader::switchTo(std::shared_ptr<UniversalImages> images) {
	_images = std::move(images);
}

std::shared_ptr<UniversalImages> EmojiImageLoader::releaseImages() {
	return std::exchange(
		_images,
		std::make_shared<UniversalImages>(_images->id()));
}

} // namespace details

EmojiPack::EmojiPack(not_null<Main::Session*> session)
: _session(session)
, _imageLoader(prepareSourceImages(), session->settings().largeEmoji())
, _clearTimer([=] { clearSourceImages(); }) {
	refresh();

	session->data().itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		return item->isIsolatedEmoji();
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		remove(item);
	}, _lifetime);

	_session->settings().largeEmojiChanges(
	) | rpl::start_with_next([=](bool large) {
		if (large) {
			_clearTimer.cancel();
		} else {
			_clearTimer.callOnce(details::kClearSourceTimeout);
		}
		refreshAll();
	}, _lifetime);

	Ui::Emoji::Updated(
	) | rpl::start_with_next([=] {
		_images.clear();
		_imageLoader.with([
			source = prepareSourceImages()
		](details::EmojiImageLoader &loader) mutable {
			loader.switchTo(std::move(source));
		});
		refreshAll();
	}, _lifetime);
}

EmojiPack::~EmojiPack() = default;

bool EmojiPack::add(not_null<HistoryItem*> item) {
	auto length = 0;
	if (const auto emoji = item->isolatedEmoji()) {
		_items[emoji].emplace(item);
		return true;
	}
	return false;
}

void EmojiPack::remove(not_null<const HistoryItem*> item) {
	Expects(item->isIsolatedEmoji());

	auto length = 0;
	const auto emoji = item->isolatedEmoji();
	const auto i = _items.find(emoji);
	Assert(i != end(_items));
	const auto j = i->second.find(item);
	Assert(j != end(i->second));
	i->second.erase(j);
	if (i->second.empty()) {
		_items.erase(i);
	}
}

auto EmojiPack::stickerForEmoji(const IsolatedEmoji &emoji) -> Sticker {
	Expects(!emoji.empty());

	if (emoji.items[1] != nullptr) {
		return Sticker();
	}
	const auto first = emoji.items[0];
	const auto i = _map.find(first);
	if (i != end(_map)) {
		return { i->second.get(), nullptr };
	}
	if (!first->colored()) {
		return Sticker();
	}
	const auto j = _map.find(first->original());
	if (j != end(_map)) {
		const auto index = first->variantIndex(first);
		return { j->second.get(), details::ColorReplacements(index) };
	}
	return Sticker();
}

std::shared_ptr<Image> EmojiPack::image(EmojiPtr emoji) {
	const auto i = _images.emplace(emoji, std::weak_ptr<Image>()).first;
	if (const auto result = i->second.lock()) {
		return result;
	}
	auto result = std::make_shared<Image>(
		std::make_unique<details::ImageSource>(emoji, &_imageLoader));
	i->second = result;
	return result;
}

void EmojiPack::refresh() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetAnimatedEmoji()
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
		refreshDelayed();
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		});
	}).fail([=](const RPCError &error) {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void EmojiPack::applySet(const MTPDmessages_stickerSet &data) {
	const auto stickers = collectStickers(data.vdocuments().v);
	auto was = base::take(_map);

	for (const auto &pack : data.vpacks().v) {
		pack.match([&](const MTPDstickerPack &data) {
			applyPack(data, stickers);
		});
	}

	for (const auto &[emoji, document] : _map) {
		const auto i = was.find(emoji);
		if (i == end(was)) {
			refreshItems(emoji);
		} else {
			if (i->second != document) {
				refreshItems(i->first);
			}
			was.erase(i);
		}
	}
	for (const auto &[emoji, Document] : was) {
		refreshItems(emoji);
	}
}

void EmojiPack::refreshAll() {
	for (const auto &[emoji, list] : _items) {
		refreshItems(list);
	}
}

void EmojiPack::refreshItems(EmojiPtr emoji) {
	const auto i = _items.find(IsolatedEmoji{ { emoji } });
	if (i == end(_items)) {
		return;
	}
	refreshItems(i->second);
}

void EmojiPack::refreshItems(
		const base::flat_set<not_null<HistoryItem*>> &list) {
	for (const auto &item : list) {
		_session->data().requestItemViewRefresh(item);
	}
}

auto EmojiPack::prepareSourceImages()
-> std::shared_ptr<Ui::Emoji::UniversalImages> {
	const auto &images = Ui::Emoji::SourceImages();
	if (_session->settings().largeEmoji()) {
		return images;
	}
	Ui::Emoji::ClearSourceImages(images);
	return std::make_shared<Ui::Emoji::UniversalImages>(images->id());
}

void EmojiPack::clearSourceImages() {
	_imageLoader.with([](details::EmojiImageLoader &loader) {
		crl::on_main([images = loader.releaseImages()]{
			Ui::Emoji::ClearSourceImages(images);
		});
	});
}

void EmojiPack::applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map) {
	const auto emoji = [&] {
		return Ui::Emoji::Find(qs(data.vemoticon()));
	}();
	const auto document = [&]() -> DocumentData * {
		for (const auto &id : data.vdocuments().v) {
			const auto i = map.find(id.v);
			if (i != end(map)) {
				return i->second.get();
			}
		}
		return nullptr;
	}();
	if (emoji && document) {
		_map.emplace_or_assign(emoji, document);
	}
}

base::flat_map<uint64, not_null<DocumentData*>> EmojiPack::collectStickers(
		const QVector<MTPDocument> &list) const {
	auto result = base::flat_map<uint64, not_null<DocumentData*>>();
	for (const auto &sticker : list) {
		const auto document = _session->data().processDocument(
			sticker);
		if (document->sticker()) {
			result.emplace(document->id, document);
		}
	}
	return result;
}

void EmojiPack::refreshDelayed() {
	base::call_delayed(details::kRefreshTimeout, _session, [=] {
		refresh();
	});
}

} // namespace Stickers
