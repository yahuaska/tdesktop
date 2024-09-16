/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_instance.h"

#include "mtproto/connection.h"
#include "core/application.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "calls/calls_call.h"
#include "calls/calls_panel.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "media/audio/media_audio_track.h"
#include "platform/platform_specific.h"
#include "base/unixtime.h"
#include "mainwidget.h"
#include "boxes/rate_call_box.h"
#include "facades.h"
#include "app.h"

namespace Calls {
namespace {

constexpr auto kServerConfigUpdateTimeoutMs = 24 * 3600 * crl::time(1000);

} // namespace

Instance::Instance(not_null<Main::Session*> session) : _session(session) {
}

void Instance::startOutgoingCall(not_null<UserData*> user) {
	if (alreadyInCall()) { // Already in a call.
		_currentCallPanel->showAndActivate();
		return;
	}
	if (user->callsStatus() == UserData::CallsStatus::Private) {
		// Request full user once more to refresh the setting in case it was changed.
		_session->api().requestFullPeer(user);
		Ui::show(Box<InformBox>(tr::lng_call_error_not_available(tr::now, lt_user, user->name)));
		return;
	}
	requestMicrophonePermissionOrFail(crl::guard(this, [=] {
		createCall(user, Call::Type::Outgoing);
	}));
}

void Instance::callFinished(not_null<Call*> call) {
	destroyCall(call);
}

void Instance::callFailed(not_null<Call*> call) {
	destroyCall(call);
}

void Instance::callRedial(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		refreshDhConfig();
	}
}

void Instance::playSound(Sound sound) {
	switch (sound) {
	case Sound::Busy: {
		if (!_callBusyTrack) {
			_callBusyTrack = Media::Audio::Current().createTrack();
			_callBusyTrack->fillFromFile(
				_session->settings().getSoundPath(qsl("call_busy")));
		}
		_callBusyTrack->playOnce();
	} break;

	case Sound::Ended: {
		if (!_callEndedTrack) {
			_callEndedTrack = Media::Audio::Current().createTrack();
			_callEndedTrack->fillFromFile(
				_session->settings().getSoundPath(qsl("call_end")));
		}
		_callEndedTrack->playOnce();
	} break;

	case Sound::Connecting: {
		if (!_callConnectingTrack) {
			_callConnectingTrack = Media::Audio::Current().createTrack();
			_callConnectingTrack->fillFromFile(
				_session->settings().getSoundPath(qsl("call_connect")));
		}
		_callConnectingTrack->playOnce();
	} break;
	}
}

void Instance::destroyCall(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		destroyCurrentPanel();
		_currentCall.reset();
		_currentCallChanged.notify(nullptr, true);

		if (App::quitting()) {
			LOG(("Calls::Instance doesn't prevent quit any more."));
		}
		Core::App().quitPreventFinished();
	}
}

void Instance::destroyCurrentPanel() {
	_pendingPanels.erase(
		std::remove_if(
			_pendingPanels.begin(),
			_pendingPanels.end(),
			[](auto &&panel) { return !panel; }),
		_pendingPanels.end());
	_pendingPanels.emplace_back(_currentCallPanel.release());
	_pendingPanels.back()->hideAndDestroy(); // Always queues the destruction.
}

void Instance::createCall(not_null<UserData*> user, Call::Type type) {
	auto call = std::make_unique<Call>(getCallDelegate(), user, type);;
	if (_currentCall) {
		_currentCallPanel->replaceCall(call.get());
		std::swap(_currentCall, call);
		call->hangup();
	} else {
		_currentCallPanel = std::make_unique<Panel>(call.get());
		_currentCall = std::move(call);
	}
	_currentCallChanged.notify(_currentCall.get(), true);
	refreshServerConfig();
	refreshDhConfig();
}

void Instance::refreshDhConfig() {
	Expects(_currentCall != nullptr);

	const auto weak = base::make_weak(_currentCall);
	request(MTPmessages_GetDhConfig(
		MTP_int(_dhConfig.version),
		MTP_int(MTP::ModExpFirst::kRandomPowerSize)
	)).done([=](const MTPmessages_DhConfig &result) {
		const auto call = weak.get();
		const auto random = updateDhConfig(result);
		if (!call) {
			return;
		}
		if (!random.empty()) {
			Assert(random.size() == MTP::ModExpFirst::kRandomPowerSize);
			call->start(random);
		} else {
			callFailed(call);
		}
	}).fail([=](const RPCError &error) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		callFailed(call);
	}).send();
}

bytes::const_span Instance::updateDhConfig(
		const MTPmessages_DhConfig &data) {
	const auto validRandom = [](const QByteArray & random) {
		if (random.size() != MTP::ModExpFirst::kRandomPowerSize) {
			return false;
		}
		return true;
	};
	return data.match([&](const MTPDmessages_dhConfig &data)
	-> bytes::const_span {
		auto primeBytes = bytes::make_vector(data.vp().v);
		if (!MTP::IsPrimeAndGood(primeBytes, data.vg().v)) {
			LOG(("API Error: bad p/g received in dhConfig."));
			return {};
		} else if (!validRandom(data.vrandom().v)) {
			return {};
		}
		_dhConfig.g = data.vg().v;
		_dhConfig.p = std::move(primeBytes);
		_dhConfig.version = data.vversion().v;
		return bytes::make_span(data.vrandom().v);
	}, [&](const MTPDmessages_dhConfigNotModified &data)
	-> bytes::const_span {
		if (!_dhConfig.g || _dhConfig.p.empty()) {
			LOG(("API Error: dhConfigNotModified on zero version."));
			return {};
		} else if (!validRandom(data.vrandom().v)) {
			return {};
		}
		return bytes::make_span(data.vrandom().v);
	});
}

void Instance::refreshServerConfig() {
	if (_serverConfigRequestId) {
		return;
	}
	if (_lastServerConfigUpdateTime && (crl::now() - _lastServerConfigUpdateTime) < kServerConfigUpdateTimeoutMs) {
		return;
	}
	_serverConfigRequestId = request(MTPphone_GetCallConfig()).done([this](const MTPDataJSON &result) {
		_serverConfigRequestId = 0;
		_lastServerConfigUpdateTime = crl::now();

		const auto &json = result.c_dataJSON().vdata().v;
		UpdateConfig(std::string(json.data(), json.size()));
	}).fail([this](const RPCError &error) {
		_serverConfigRequestId = 0;
	}).send();
}

void Instance::handleUpdate(const MTPDupdatePhoneCall& update) {
	handleCallUpdate(update.vphone_call());
}

void Instance::showInfoPanel(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		_currentCallPanel->showAndActivate();
	}
}

bool Instance::isQuitPrevent() {
	if (!_currentCall || _currentCall->isIncomingWaiting()) {
		return false;
	}
	_currentCall->hangup();
	if (!_currentCall) {
		return false;
	}
	LOG(("Calls::Instance prevents quit, hanging up a call..."));
	return true;
}

void Instance::handleCallUpdate(const MTPPhoneCall &call) {
	if (call.type() == mtpc_phoneCallRequested) {
		auto &phoneCall = call.c_phoneCallRequested();
		auto user = _session->data().userLoaded(phoneCall.vadmin_id().v);
		if (!user) {
			LOG(("API Error: User not loaded for phoneCallRequested."));
		} else if (user->isSelf()) {
			LOG(("API Error: Self found in phoneCallRequested."));
		}
		if (alreadyInCall() || !user || user->isSelf()) {
			request(MTPphone_DiscardCall(
				MTP_flags(0),
				MTP_inputPhoneCall(phoneCall.vid(), phoneCall.vaccess_hash()),
				MTP_int(0),
				MTP_phoneCallDiscardReasonBusy(),
				MTP_long(0)
			)).send();
		} else if (phoneCall.vdate().v + (Global::CallRingTimeoutMs() / 1000)
			< base::unixtime::now()) {
			LOG(("Ignoring too old call."));
		} else {
			createCall(user, Call::Type::Incoming);
			_currentCall->handleUpdate(call);
		}
	} else if (!_currentCall || !_currentCall->handleUpdate(call)) {
		DEBUG_LOG(("API Warning: unexpected phone call update %1").arg(call.type()));
	}
}

bool Instance::alreadyInCall() {
	return (_currentCall && _currentCall->state() != Call::State::Busy);
}

Call *Instance::currentCall() {
	return _currentCall.get();
}

void Instance::requestMicrophonePermissionOrFail(Fn<void()> onSuccess) {
	Platform::PermissionStatus status=Platform::GetPermissionStatus(Platform::PermissionType::Microphone);
	if (status==Platform::PermissionStatus::Granted) {
		onSuccess();
	} else if(status==Platform::PermissionStatus::CanRequest) {
		Platform::RequestPermission(Platform::PermissionType::Microphone, crl::guard(this, [=](Platform::PermissionStatus status) {
			if (status==Platform::PermissionStatus::Granted) {
				crl::on_main(onSuccess);
			} else {
				if (_currentCall) {
					_currentCall->hangup();
				}
			}
		}));
	} else {
		if (alreadyInCall()) {
			_currentCall->hangup();
		}
		Ui::show(Box<ConfirmBox>(tr::lng_no_mic_permission(tr::now), tr::lng_menu_settings(tr::now), crl::guard(this, [] {
			Platform::OpenSystemSettingsForPermission(Platform::PermissionType::Microphone);
			Ui::hideLayer();
		})));
	}
}

Instance::~Instance() {
	for (auto panel : _pendingPanels) {
		if (panel) {
			delete panel;
		}
	}
}

} // namespace Calls
