/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_ui_integration.h"

#include "core/local_url_handlers.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/click_handler_types.h"
#include "ui/basic_click_handlers.h"
#include "ui/emoji_config.h"
#include "lang/lang_keys.h"
#include "platform/platform_specific.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mainwindow.h"

namespace Core {

void UiIntegration::postponeCall(FnMut<void()> &&callable) {
	Sandbox::Instance().postponeCall(std::move(callable));
}

void UiIntegration::registerLeaveSubscription(not_null<QWidget*> widget) {
	Core::App().registerLeaveSubscription(widget);
}

void UiIntegration::unregisterLeaveSubscription(not_null<QWidget*> widget) {
	Core::App().unregisterLeaveSubscription(widget);
}

void UiIntegration::writeLogEntry(const QString &entry) {
	Logs::writeMain(entry);
}

QString UiIntegration::emojiCacheFolder() {
	return cWorkingDir() + "tdata/emoji";
}

void UiIntegration::textActionsUpdated() {
	if (const auto window = App::wnd()) {
		window->updateGlobalMenu();
	}
}

void UiIntegration::activationFromTopPanel() {
	Platform::IgnoreApplicationActivationRightNow();
}

std::shared_ptr<ClickHandler> UiIntegration::createLinkHandler(
		EntityType type,
		const QString &text,
		const QString &data,
		const TextParseOptions &options) {
	switch (type) {
	case EntityType::CustomUrl:
		return !data.isEmpty()
			? std::make_shared<HiddenUrlClickHandler>(data)
			: nullptr;

	case EntityType::BotCommand:
		return std::make_shared<BotCommandClickHandler>(data);

	case EntityType::Hashtag:
		if (options.flags & TextTwitterMentions) {
			return std::make_shared<UrlClickHandler>(
				(qsl("https://twitter.com/hashtag/")
					+ data.mid(1)
					+ qsl("?src=hash")),
				true);
		} else if (options.flags & TextInstagramMentions) {
			return std::make_shared<UrlClickHandler>(
				(qsl("https://instagram.com/explore/tags/")
					+ data.mid(1)
					+ '/'),
				true);
		}
		return std::make_shared<HashtagClickHandler>(data);

	case EntityType::Cashtag:
		return std::make_shared<CashtagClickHandler>(data);

	case EntityType::Mention:
		if (options.flags & TextTwitterMentions) {
			return std::make_shared<UrlClickHandler>(
				qsl("https://twitter.com/") + data.mid(1),
				true);
		} else if (options.flags & TextInstagramMentions) {
			return std::make_shared<UrlClickHandler>(
				qsl("https://instagram.com/") + data.mid(1) + '/',
				true);
		}
		return std::make_shared<MentionClickHandler>(data);

	case EntityType::MentionName: {
		auto fields = TextUtilities::MentionNameDataToFields(data);
		if (fields.userId) {
			return std::make_shared<MentionNameClickHandler>(
				text,
				fields.userId,
				fields.accessHash);
		} else {
			LOG(("Bad mention name: %1").arg(data));
		}
	} break;
	}
	return nullptr;
}

bool UiIntegration::handleUrlClick(
		const QString &url,
		const QVariant &context) {
	const auto local = Core::TryConvertUrlToLocal(url);
	if (Core::InternalPassportLink(local)) {
		return true;
	}

	if (UrlClickHandler::IsEmail(url)) {
		File::OpenEmailLink(url);
		return true;
	} else if (local.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		Core::App().openLocalUrl(local, context);
		return true;
	}
	return false;

}

rpl::producer<> UiIntegration::forcePopupMenuHideRequests() {
	return rpl::merge(
		Core::App().passcodeLockChanges(),
		Core::App().termsLockChanges()
	) | rpl::map([] { return rpl::empty_value(); });
}

QString UiIntegration::convertTagToMimeTag(const QString &tagId) {
	if (TextUtilities::IsMentionLink(tagId)) {
		const auto &account = Core::App().activeAccount();
		if (account.sessionExists()) {
			return tagId + ':' + QString::number(account.session().userId());
		}
	}
	return tagId;
}

const Ui::Emoji::One *UiIntegration::defaultEmojiVariant(
		const Ui::Emoji::One *emoji) {
	if (!emoji || !emoji->hasVariants()) {
		return emoji;
	}
	const auto nonColored = emoji->nonColoredId();
	const auto it = cEmojiVariants().constFind(nonColored);
	const auto result = (it != cEmojiVariants().cend())
		? emoji->variant(it.value())
		: emoji;
	AddRecentEmoji(result);
	return result;
}

QString UiIntegration::phraseContextCopyText() {
	return tr::lng_context_copy_text(tr::now);
}

QString UiIntegration::phraseContextCopyEmail() {
	return tr::lng_context_copy_email(tr::now);
}

QString UiIntegration::phraseContextCopyLink() {
	return tr::lng_context_copy_link(tr::now);
}

QString UiIntegration::phraseContextCopySelected() {
	return tr::lng_context_copy_selected(tr::now);
}

QString UiIntegration::phraseFormattingTitle() {
	return tr::lng_menu_formatting(tr::now);
}

QString UiIntegration::phraseFormattingLinkCreate() {
	return tr::lng_menu_formatting_link_create(tr::now);
}

QString UiIntegration::phraseFormattingLinkEdit() {
	return tr::lng_menu_formatting_link_edit(tr::now);
}

QString UiIntegration::phraseFormattingClear() {
	return tr::lng_menu_formatting_clear(tr::now);
}

QString UiIntegration::phraseFormattingBold() {
	return tr::lng_menu_formatting_bold(tr::now);
}

QString UiIntegration::phraseFormattingItalic() {
	return tr::lng_menu_formatting_italic(tr::now);
}

QString UiIntegration::phraseFormattingUnderline() {
	return tr::lng_menu_formatting_underline(tr::now);
}

QString UiIntegration::phraseFormattingStrikeOut() {
	return tr::lng_menu_formatting_strike_out(tr::now);
}

QString UiIntegration::phraseFormattingMonospace() {
	return tr::lng_menu_formatting_monospace(tr::now);
}

} // namespace Core
