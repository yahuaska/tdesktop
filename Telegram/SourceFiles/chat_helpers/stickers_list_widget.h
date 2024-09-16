/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/stickers.h"
#include "base/variant.h"
#include "base/timer.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class LinkButton;
class RippleAnimation;
class BoxContent;
} // namespace Ui

namespace Lottie {
class Animation;
class MultiPlayer;
class FrameRenderer;
} // namespace Lottie

namespace ChatHelpers {

struct StickerIcon;

class StickersListWidget
	: public TabbedSelector::Inner
	, private base::Subscriber
	, private MTP::Sender {
public:
	StickersListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	Main::Session &session() const;

	rpl::producer<not_null<DocumentData*>> chosen() const;
	rpl::producer<> scrollUpdated() const;
	rpl::producer<> checkForHide() const;

	void refreshRecent() override;
	void preloadImages() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void showStickerSet(uint64 setId);
	void showMegagroupSet(ChannelData *megagroup);

	void afterShown() override;
	void beforeHiding() override;

	void refreshStickers();

	void fillIcons(QList<StickerIcon> &icons);
	bool preventAutoHide();

	uint64 currentSet(int yOffset) const;

	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);
	void clearInstalledLocally();

	void sendSearchRequest();
	void searchForSets(const QString &query);

	std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();

	~StickersListWidget();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

	TabbedSelector::InnerFooter *getFooter() const override;
	void processHideFinished() override;
	void processPanelHideFinished() override;
	int countDesiredHeight(int newWidth) override;

private:
	class Footer;

	enum class Section {
		Featured,
		Stickers,
		Search,
	};

	struct OverSticker {
		int section = 0;
		int index = 0;
		bool overDelete = false;
	};
	struct OverSet {
		int section = 0;
	};
	struct OverButton {
		int section = 0;
	};
	struct OverGroupAdd {
	};
	friend inline bool operator==(OverSticker a, OverSticker b) {
		return (a.section == b.section)
			&& (a.index == b.index)
			&& (a.overDelete == b.overDelete);
	}
	friend inline bool operator==(OverSet a, OverSet b) {
		return (a.section == b.section);
	}
	friend inline bool operator==(OverButton a, OverButton b) {
		return (a.section == b.section);
	}
	friend inline bool operator==(OverGroupAdd a, OverGroupAdd b) {
		return true;
	}
	using OverState = base::optional_variant<
		OverSticker,
		OverSet,
		OverButton,
		OverGroupAdd>;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};

	struct Sticker {
		not_null<DocumentData*> document;
		Lottie::Animation *animated = nullptr;
	};

	struct Set {
		Set(
			uint64 id,
			MTPDstickerSet::Flags flags,
			const QString &title,
			const QString &shortName,
			ImagePtr thumbnail,
			bool externalLayout,
			int count,
			std::vector<Sticker> &&stickers = {});
		Set(Set &&other);
		Set &operator=(Set &&other);
		~Set();

		uint64 id = 0;
		MTPDstickerSet::Flags flags = MTPDstickerSet::Flags();
		QString title;
		QString shortName;
		ImagePtr thumbnail;
		std::vector<Sticker> stickers;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		Lottie::MultiPlayer *lottiePlayer = nullptr;
		bool externalLayout = false;
		int count = 0;
	};
	struct LottieSet {
		struct Item {
			not_null<Lottie::Animation*> animation;
			bool stale = false;
		};
		std::unique_ptr<Lottie::MultiPlayer> player;
		base::flat_map<DocumentId, Item> items;
		bool stale = false;
		rpl::lifetime lifetime;
	};

	static std::vector<Sticker> PrepareStickers(const Stickers::Pack &pack);

	QSize boundingBoxSize() const;

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void setSection(Section section);
	void displaySet(uint64 setId);
	void checkHideWithBox(QPointer<Ui::BoxContent> box);
	void installSet(uint64 setId);
	void removeMegagroupSet(bool locally);
	void removeSet(uint64 setId);
	void sendInstallRequest(
		uint64 setId,
		const MTPInputStickerSet &input);
	void refreshMySets();
	void refreshFeaturedSets();
	void refreshSearchSets();
	void refreshSearchIndex();

	bool setHasTitle(const Set &set) const;
	bool stickerHasDeleteButton(const Set &set, int index) const;
	std::vector<Sticker> collectRecentStickers();
	void refreshRecentStickers(bool resize = true);
	void refreshFavedStickers();
	enum class GroupStickersPlace {
		Visible,
		Hidden,
	};
	void refreshMegagroupStickers(GroupStickersPlace place);
	void refreshSettingsVisibility();

	void updateSelected();
	void setSelected(OverState newSelected);
	void setPressed(OverState newPressed);
	std::unique_ptr<Ui::RippleAnimation> createButtonRipple(int section);
	QPoint buttonRippleTopLeft(int section) const;

	enum class ValidateIconAnimations {
		Full,
		Scroll,
		None,
	};
	void validateSelectedIcon(ValidateIconAnimations animations);

	std::vector<Set> &shownSets();
	const std::vector<Set> &shownSets() const;
	int featuredRowHeight() const;
	void checkVisibleFeatured(int visibleTop, int visibleBottom);
	void readVisibleFeatured(int visibleTop, int visibleBottom);

	void paintStickers(Painter &p, QRect clip);
	void paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected);
	void paintSticker(Painter &p, Set &set, int y, int section, int index, bool selected, bool deleteSelected);
	void paintEmptySearchResults(Painter &p);

	void ensureLottiePlayer(Set &set);
	void setupLottie(Set &set, int section, int index);
	void markLottieFrameShown(Set &set);
	void checkVisibleLottie();
	void pauseInvisibleLottieIn(const SectionInfo &info);
	void destroyLottieIn(Set &set);
	void refillLottieData();
	void refillLottieData(Set &set);
	void clearLottieData();

	int stickersRight() const;
	bool featuredHasAddButton(int index) const;
	QRect featuredAddRect(int index) const;
	bool hasRemoveButton(int index) const;
	QRect removeButtonRect(int index) const;
	int megagroupSetInfoLeft() const;
	void refreshMegagroupSetGeometry();
	QRect megagroupSetButtonRectFinal() const;

	enum class AppendSkip {
		None,
		Archived,
		Installed,
	};
	void appendSet(
		std::vector<Set> &to,
		uint64 setId,
		bool externalLayout,
		AppendSkip skip = AppendSkip::None);

	void selectEmoji(EmojiPtr emoji);
	int stickersLeft() const;
	QRect stickerRect(int section, int sel);

	void removeRecentSticker(int section, int index);
	void removeFavedSticker(int section, int index);
	void setColumnCount(int count);
	void refreshFooterIcons();

	void cancelSetsSearch();
	void showSearchResults();
	void searchResultsDone(const MTPmessages_FoundStickerSets &result);
	void refreshSearchRows();
	void refreshSearchRows(const std::vector<uint64> *cloudSets);
	void fillLocalSearchRows(const QString &query);
	void fillCloudSearchRows(const std::vector<uint64> &cloudSets);
	void addSearchRow(not_null<const Stickers::Set*> set);

	void showPreview();

	ChannelData *_megagroupSet = nullptr;
	uint64 _megagroupSetIdRequested = 0;
	std::vector<Set> _mySets;
	std::vector<Set> _featuredSets;
	std::vector<Set> _searchSets;
	base::flat_set<uint64> _installedLocallySets;
	std::vector<bool> _custom;
	base::flat_set<not_null<DocumentData*>> _favedStickersMap;
	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;

	Section _section = Section::Stickers;

	bool _displayingSet = false;
	uint64 _removingSetId = 0;

	Footer *_footer = nullptr;
	int _rowsLeft = 0;
	int _columnCount = 1;
	QSize _singleSize;

	OverState _selected;
	OverState _pressed;
	QPoint _lastMousePosition;

	Ui::Text::String _megagroupSetAbout;
	QString _megagroupSetButtonText;
	int _megagroupSetButtonTextWidth = 0;
	QRect _megagroupSetButtonRect;
	std::unique_ptr<Ui::RippleAnimation> _megagroupSetButtonRipple;

	QString _addText;
	int _addWidth;

	object_ptr<Ui::LinkButton> _settings;

	base::Timer _previewTimer;
	bool _previewShown = false;

	std::map<QString, std::vector<uint64>> _searchCache;
	std::vector<std::pair<uint64, QStringList>> _searchIndex;
	base::Timer _searchRequestTimer;
	QString _searchQuery, _searchNextQuery;
	mtpRequestId _searchRequestId = 0;

	base::flat_map<uint64, LottieSet> _lottieData;

	rpl::event_stream<not_null<DocumentData*>> _chosen;
	rpl::event_stream<> _scrollUpdated;
	rpl::event_stream<> _checkForHide;

};

} // namespace ChatHelpers
