/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/changelogs.h"

#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> BetaLogs() {
	return {
	{
		1006004,
		"- Replace media when editing messages with media content.\n"

		"- Jump quickly to the top of your chats list.\n"

		"- Get emoji suggestions for the first word you type in a message.\n"

		"- Help Telegram improve emoji suggestions in your language "
		"using this interface https://translations.telegram.org/en/emoji"
	},
	{
		1007001,
		"- Disable pinned messages notifications in Settings."
	},
	{
		1007004,
		"- Download video files while watching them using streaming."
	},
	{
		1007008,
		"\xE2\x80\xA2 Hide archived chats in the main menu.\n"

		"\xE2\x80\xA2 See who is online straight from the chat list.\n"

		"\xE2\x80\xA2 Apply formatting to selected text parts "
		"from the MacBook Pro TouchBar."
	},
	{
		1007011,
		"\xE2\x80\xA2 Use strikethrough and underline formatting.\n"

		"\xE2\x80\xA2 Bug fixes and other minor improvements."
	},
	{
		1008005,
		"\xE2\x80\xA2 Create new themes based on your color and wallpaper choices.\n"

		"\xE2\x80\xA2 Share your themes with other users via links.\n"

		"\xE2\x80\xA2 Update your theme for all its users when you change something.\n"
	},
	{
		1009000,
		"\xE2\x80\xA2 System spellchecker on Windows 8+ and macOS 10.12+.\n"
	},
	};
};

QString FormatVersionDisplay(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ ((version % 1000)
			? ('.' + QString::number(version % 1000))
			: QString());
}

QString FormatVersionPrecise(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ '.' + QString::number(version % 1000);
}

} // namespace

Changelogs::Changelogs(not_null<Main::Session*> session, int oldVersion)
: _session(session)
, _oldVersion(oldVersion) {
	_session->data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return !folder;
	}) | rpl::start_with_next([=] {
		requestCloudLogs();
	}, _chatsSubscription);
}

std::unique_ptr<Changelogs> Changelogs::Create(
		not_null<Main::Session*> session) {
	const auto oldVersion = Local::oldMapVersion();
	return (oldVersion > 0 && oldVersion < AppVersion)
		? std::make_unique<Changelogs>(session, oldVersion)
		: nullptr;
}

void Changelogs::requestCloudLogs() {
	_chatsSubscription.destroy();

	const auto callback = [this](const MTPUpdates &result) {
		_session->api().applyUpdates(result);

		auto resultEmpty = true;
		switch (result.type()) {
		case mtpc_updateShortMessage:
		case mtpc_updateShortChatMessage:
		case mtpc_updateShort:
			resultEmpty = false;
			break;
		case mtpc_updatesCombined:
			resultEmpty = result.c_updatesCombined().vupdates().v.isEmpty();
			break;
		case mtpc_updates:
			resultEmpty = result.c_updates().vupdates().v.isEmpty();
			break;
		case mtpc_updatesTooLong:
		case mtpc_updateShortSentMessage:
			LOG(("API Error: Bad updates type in app changelog."));
			break;
		}
		if (resultEmpty) {
			addLocalLogs();
		}
	};
	_session->api().requestChangelog(
		FormatVersionPrecise(_oldVersion),
		crl::guard(this, callback));
}

void Changelogs::addLocalLogs() {
	if (AppBetaVersion || cAlphaVersion()) {
		addBetaLogs();
	}
	if (!_addedSomeLocal) {
		const auto text = tr::lng_new_version_wrap(
			tr::now,
			lt_version,
			QString::fromLatin1(AppVersionStr),
			lt_changes,
			tr::lng_new_version_minor(tr::now),
			lt_link,
			qsl("https://desktop.telegram.org/changelog"));
		addLocalLog(text.trimmed());
	}
}

void Changelogs::addLocalLog(const QString &text) {
	auto textWithEntities = TextWithEntities{ text };
	TextUtilities::ParseEntities(textWithEntities, TextParseLinks);
	_session->data().serviceNotification(textWithEntities);
	_addedSomeLocal = true;
};

void Changelogs::addBetaLogs() {
	for (const auto [version, changes] : BetaLogs()) {
		addBetaLog(version, changes);
	}
}

void Changelogs::addBetaLog(int changeVersion, const char *changes) {
	if (_oldVersion >= changeVersion) {
		return;
	}
	const auto version = FormatVersionDisplay(changeVersion);
	const auto text = qsl("New in version %1:\n\n").arg(version)
		+ QString::fromUtf8(changes).trimmed();
	addLocalLog(text);
}

} // namespace Core
