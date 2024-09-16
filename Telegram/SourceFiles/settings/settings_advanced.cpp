/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_advanced.h"

#include "settings/settings_common.h"
#include "settings/settings_chat.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "boxes/connection_box.h"
#include "boxes/about_box.h"
#include "boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "base/platform/base_platform_info.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "layout.h"
#include "facades.h"
#include "app.h"
#include "styles/style_settings.h"

namespace Settings {

bool HasConnectionType() {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	return true;
#endif // TDESKTOP_DISABLE_NETWORK_PROXY
	return false;
}

void SetupConnectionType(not_null<Ui::VerticalLayout*> container) {
	if (!HasConnectionType()) {
		return;
	}
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	const auto connectionType = [] {
		const auto transport = MTP::dctransport();
		if (Global::ProxySettings() != ProxyData::Settings::Enabled) {
			return transport.isEmpty()
				? tr::lng_connection_auto_connecting(tr::now)
				: tr::lng_connection_auto(tr::now, lt_transport, transport);
		} else {
			return transport.isEmpty()
				? tr::lng_connection_proxy_connecting(tr::now)
				: tr::lng_connection_proxy(tr::now, lt_transport, transport);
		}
	};
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_connection_type(),
		rpl::single(
			rpl::empty_value()
		) | rpl::then(base::ObservableViewer(
			Global::RefConnectionTypeChanged()
		)) | rpl::map(connectionType),
		st::settingsButton);
	button->addClickHandler([] {
		Ui::show(ProxiesBoxController::CreateOwningBox());
	});
#endif // TDESKTOP_DISABLE_NETWORK_PROXY
}

bool HasUpdate() {
	return !Core::UpdaterDisabled();
}

void SetupUpdate(not_null<Ui::VerticalLayout*> container) {
	if (!HasUpdate()) {
		return;
	}

	const auto texts = Ui::CreateChild<rpl::event_stream<QString>>(
		container.get());
	const auto downloading = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());
	const auto version = tr::lng_settings_current_version(
		tr::now,
		lt_version,
		currentVersionText());
	const auto toggle = AddButton(
		container,
		tr::lng_settings_update_automatically(),
		st::settingsUpdateToggle);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		toggle.get(),
		texts->events(),
		st::settingsUpdateState);

	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = options->entity();
	const auto install = cAlphaVersion() ? nullptr : AddButton(
		inner,
		tr::lng_settings_install_beta(),
		st::settingsButton).get();

	const auto check = AddButton(
		inner,
		tr::lng_settings_check_now(),
		st::settingsButton);
	const auto update = Ui::CreateChild<Button>(
		check.get(),
		tr::lng_update_telegram() | Ui::Text::ToUpper(),
		st::settingsUpdate);
	update->hide();
	check->widthValue() | rpl::start_with_next([=](int width) {
		update->resizeToWidth(width);
		update->moveToLeft(0, 0);
	}, update->lifetime());

	rpl::combine(
		toggle->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=] {
		label->moveToLeft(
			st::settingsUpdateStatePosition.x(),
			st::settingsUpdateStatePosition.y());
	}, label->lifetime());
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto showDownloadProgress = [=](int64 ready, int64 total) {
		texts->fire(tr::lng_settings_downloading_update(
			tr::now,
			lt_progress,
			formatDownloadText(ready, total)));
		downloading->fire(true);
	};
	const auto setDefaultStatus = [=](const Core::UpdateChecker &checker) {
		using State = Core::UpdateChecker::State;
		const auto state = checker.state();
		switch (state) {
		case State::Download:
			showDownloadProgress(checker.already(), checker.size());
			break;
		case State::Ready:
			texts->fire(tr::lng_settings_update_ready(tr::now));
			update->show();
			break;
		default:
			texts->fire_copy(version);
			break;
		}
	};

	toggle->toggleOn(rpl::single(cAutoUpdate()));
	toggle->toggledValue(
	) | rpl::filter([](bool toggled) {
		return (toggled != cAutoUpdate());
	}) | rpl::start_with_next([=](bool toggled) {
		cSetAutoUpdate(toggled);

		Local::writeSettings();
		Core::UpdateChecker checker;
		if (cAutoUpdate()) {
			checker.start();
		} else {
			checker.stop();
			setDefaultStatus(checker);
		}
	}, toggle->lifetime());

	if (install) {
		install->toggleOn(rpl::single(cInstallBetaVersion()));
		install->toggledValue(
		) | rpl::filter([](bool toggled) {
			return (toggled != cInstallBetaVersion());
		}) | rpl::start_with_next([=](bool toggled) {
			cSetInstallBetaVersion(toggled);
			Core::App().writeInstallBetaVersionsSetting();

			Core::UpdateChecker checker;
			checker.stop();
			if (toggled) {
				cSetLastUpdateCheck(0);
			}
			checker.start();
		}, toggle->lifetime());
	}

	Core::UpdateChecker checker;
	options->toggleOn(rpl::combine(
		toggle->toggledValue(),
		downloading->events_starting_with(
			checker.state() == Core::UpdateChecker::State::Download)
	) | rpl::map([](bool check, bool downloading) {
		return check && !downloading;
	}));

	checker.checking() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents);
		texts->fire(tr::lng_settings_update_checking(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.isLatest() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_latest_installed(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.progress(
	) | rpl::start_with_next([=](Core::UpdateChecker::Progress progress) {
		showDownloadProgress(progress.already, progress.size);
	}, options->lifetime());
	checker.failed() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_update_fail(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.ready() | rpl::start_with_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_update_ready(tr::now));
		update->show();
		downloading->fire(false);
	}, options->lifetime());

	setDefaultStatus(checker);

	check->addClickHandler([] {
		Core::UpdateChecker checker;

		cSetLastUpdateCheck(0);
		checker.start();
	});
	update->addClickHandler([] {
		if (!Core::UpdaterDisabled()) {
			Core::checkReadyUpdate();
		}
		App::restart();
	});
}

bool HasSystemSpellchecker() {
#ifdef TDESKTOP_DISABLE_SPELLCHECK
	return false;
#endif // TDESKTOP_DISABLE_SPELLCHECK
	return (Platform::IsWindows() && Platform::IsWindows8OrGreater())
		|| Platform::IsMac();
}

void SetupSpellchecker(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto session = &controller->session();
	AddButton(
		container,
		tr::lng_settings_system_spellchecker(),
		st::settingsButton
	)->toggleOn(
		rpl::single(session->settings().spellcheckerEnabled())
	)->toggledValue(
	) | rpl::filter([=](bool enabled) {
		return (enabled != session->settings().spellcheckerEnabled());
	}) | rpl::start_with_next([=](bool enabled) {
		session->settings().setSpellcheckerEnabled(enabled);
		session->saveSettingsDelayed();
	}, container->lifetime());
}

bool HasTray() {
	return cSupportTray() || Platform::IsWindows();
}

void SetupTrayContent(not_null<Ui::VerticalLayout*> container) {
	const auto checkbox = [&](const QString &label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			label,
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](const QString &label, bool checked) {
		return container->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding);
	};
	const auto addSlidingCheckbox = [&](const QString &label, bool checked) {
		return container->add(
			object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
				container,
				checkbox(label, checked),
				st::settingsCheckboxPadding));
	};

	const auto trayEnabled = [] {
		const auto workMode = Global::WorkMode().value();
		return (workMode == dbiwmTrayOnly)
			|| (workMode == dbiwmWindowAndTray);
	};
	const auto tray = addCheckbox(
		tr::lng_settings_workmode_tray(tr::now),
		trayEnabled());

	const auto taskbarEnabled = [] {
		const auto workMode = Global::WorkMode().value();
		return (workMode == dbiwmWindowOnly)
			|| (workMode == dbiwmWindowAndTray);
	};
	const auto taskbar = Platform::IsWindows()
		? addCheckbox(
			tr::lng_settings_workmode_window(tr::now),
			taskbarEnabled())
		: nullptr;

	const auto updateWorkmode = [=] {
		const auto newMode = tray->checked()
			? ((!taskbar || taskbar->checked())
				? dbiwmWindowAndTray
				: dbiwmTrayOnly)
			: dbiwmWindowOnly;
		if ((newMode == dbiwmWindowAndTray || newMode == dbiwmTrayOnly)
			&& Global::WorkMode().value() != newMode) {
			cSetSeenTrayTooltip(false);
		}
		Global::RefWorkMode().set(newMode);
		Local::writeSettings();
	};

	tray->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != trayEnabled());
	}) | rpl::start_with_next([=](bool checked) {
		if (!checked && taskbar && !taskbar->checked()) {
			taskbar->setChecked(true);
		} else {
			updateWorkmode();
		}
	}, tray->lifetime());

	if (taskbar) {
		taskbar->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != taskbarEnabled());
		}) | rpl::start_with_next([=](bool checked) {
			if (!checked && !tray->checked()) {
				tray->setChecked(true);
			} else {
				updateWorkmode();
			}
		}, taskbar->lifetime());
	}

#ifndef OS_WIN_STORE
	if (Platform::IsWindows()) {
		const auto minimizedToggled = [] {
			return cStartMinimized() && !Global::LocalPasscode();
		};

		const auto autostart = addCheckbox(
			tr::lng_settings_auto_start(tr::now),
			cAutoStart());
		const auto minimized = addSlidingCheckbox(
			tr::lng_settings_start_min(tr::now),
			minimizedToggled());
		const auto sendto = addCheckbox(
			tr::lng_settings_add_sendto(tr::now),
			cSendToMenu());

		autostart->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != cAutoStart());
		}) | rpl::start_with_next([=](bool checked) {
			cSetAutoStart(checked);
			psAutoStart(checked);
			if (checked) {
				Local::writeSettings();
			} else if (minimized->entity()->checked()) {
				minimized->entity()->setChecked(false);
			} else {
				Local::writeSettings();
			}
		}, autostart->lifetime());

		minimized->toggleOn(autostart->checkedValue());
		minimized->entity()->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != minimizedToggled());
		}) | rpl::start_with_next([=](bool checked) {
			if (Global::LocalPasscode()) {
				minimized->entity()->setChecked(false);
				Ui::show(Box<InformBox>(
					tr::lng_error_start_minimized_passcoded(tr::now)));
			} else {
				cSetStartMinimized(checked);
				Local::writeSettings();
			}
		}, minimized->lifetime());

		base::ObservableViewer(
			Global::RefLocalPasscodeChanged()
		) | rpl::start_with_next([=] {
			minimized->entity()->setChecked(minimizedToggled());
		}, minimized->lifetime());

		sendto->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != cSendToMenu());
		}) | rpl::start_with_next([](bool checked) {
			cSetSendToMenu(checked);
			psSendToMenu(checked);
			Local::writeSettings();
		}, sendto->lifetime());
	}
#endif // OS_WIN_STORE
}

void SetupTray(not_null<Ui::VerticalLayout*> container) {
	if (!HasTray()) {
		return;
	}

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	SetupTrayContent(wrap.data());

	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap)));

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupAnimations(not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		tr::lng_settings_enable_animations(),
		st::settingsButton
	)->toggleOn(
		rpl::single(!anim::Disabled())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled == anim::Disabled());
	}) | rpl::start_with_next([](bool enabled) {
		anim::SetDisabled(!enabled);
		Local::writeSettings();
	}, container->lifetime());
}

void SetupPerformance(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	SetupAnimations(container);

	const auto session = &controller->session();
	AddButton(
		container,
		tr::lng_settings_autoplay_gifs(),
		st::settingsButton
	)->toggleOn(
		rpl::single(session->settings().autoplayGifs())
	)->toggledValue(
	) | rpl::filter([=](bool enabled) {
		return (enabled != session->settings().autoplayGifs());
	}) | rpl::start_with_next([=](bool enabled) {
		session->settings().setAutoplayGifs(enabled);
		if (!enabled) {
			session->data().stopAutoplayAnimations();
		}
		session->saveSettingsDelayed();
	}, container->lifetime());
}

void SetupSystemIntegration(
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_system_integration());
	AddButton(
		container,
		tr::lng_settings_section_call_settings(),
		st::settingsButton
	)->addClickHandler([=] {
		showOther(Type::Calls);
	});
	SetupTray(container);
	AddSkip(container);
}

Advanced::Advanced(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<Type> Advanced::sectionShowOther() {
	return _showOther.events();
}

void Advanced::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	auto empty = true;
	const auto addDivider = [&] {
		if (empty) {
			empty = false;
		} else {
			AddDivider(content);
		}
	};
	const auto addUpdate = [&] {
		if (HasUpdate()) {
			addDivider();
			AddSkip(content);
			AddSubsectionTitle(content, tr::lng_settings_version_info());
			SetupUpdate(content);
			AddSkip(content);
		}
	};
	if (!cAutoUpdate()) {
		addUpdate();
	}
	if (HasConnectionType()) {
		addDivider();
		AddSkip(content);
		AddSubsectionTitle(content, tr::lng_settings_network_proxy());
		SetupConnectionType(content);
		AddSkip(content);
	}
	SetupDataStorage(controller, content);
	SetupAutoDownload(controller, content);
	SetupSystemIntegration(content, [=](Type type) {
		_showOther.fire_copy(type);
	});

	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_performance());
	SetupPerformance(controller, content);
	AddSkip(content);

	if (HasSystemSpellchecker()) {
		AddDivider(content);
		AddSkip(content);
		AddSubsectionTitle(content, tr::lng_settings_spellchecker());
		SetupSpellchecker(controller, content);
		AddSkip(content);
	}

	if (cAutoUpdate()) {
		addUpdate();
	}

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
