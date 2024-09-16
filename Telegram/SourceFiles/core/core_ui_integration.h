/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/ui_integration.h"

namespace Core {

class UiIntegration : public Ui::Integration {
public:
	void postponeCall(FnMut<void()> &&callable) override;
	void registerLeaveSubscription(not_null<QWidget*> widget) override;
	void unregisterLeaveSubscription(not_null<QWidget*> widget) override;

	void writeLogEntry(const QString &entry) override;
	QString emojiCacheFolder() override;

	void textActionsUpdated() override;
	void activationFromTopPanel() override;

	std::shared_ptr<ClickHandler> createLinkHandler(
		EntityType type,
		const QString &text,
		const QString &data,
		const TextParseOptions &options) override;
	bool handleUrlClick(
		const QString &url,
		const QVariant &context) override;
	rpl::producer<> forcePopupMenuHideRequests() override;
	QString convertTagToMimeTag(const QString &tagId) override;
	const Ui::Emoji::One *defaultEmojiVariant(
		const Ui::Emoji::One *emoji) override;

	QString phraseContextCopyText() override;
	QString phraseContextCopyEmail() override;
	QString phraseContextCopyLink() override;
	QString phraseContextCopySelected() override;
	QString phraseFormattingTitle() override;
	QString phraseFormattingLinkCreate() override;
	QString phraseFormattingLinkEdit() override;
	QString phraseFormattingClear() override;
	QString phraseFormattingBold() override;
	QString phraseFormattingItalic() override;
	QString phraseFormattingUnderline() override;
	QString phraseFormattingStrikeOut() override;
	QString phraseFormattingMonospace() override;

};

} // namespace Core
