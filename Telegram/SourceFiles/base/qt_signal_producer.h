/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/base_integration.h"

namespace base {

// This method allows to create an rpl::producer from a Qt object
// and a signal with none or one reported value.
//
// QtSignalProducer(qtWindow, &QWindow::activeChanged) | rpl::start_
//
// This producer values construct a custom event loop leave point.
// This means that all postponeCall's will be invoked right after
// the value processing by the current consumer finishes.
template <typename Object, typename Signal>
auto qt_signal_producer(Object *object, Signal signal);

namespace details {

template <typename Signal>
struct qt_signal_argument;

template <typename Class, typename Return, typename Value>
struct qt_signal_argument<Return(Class::*)(Value)> {
	using type = Value;
};

template <typename Class, typename Return>
struct qt_signal_argument<Return(Class::*)()> {
	using type = void;
};

} // namespace details

template <typename Object, typename Signal>
auto qt_signal_producer(Object *object, Signal signal) {
	using Value = typename details::qt_signal_argument<Signal>::type;
	static constexpr auto NoArgument = std::is_same_v<Value, void>;
	using Produced = std::conditional_t<
		NoArgument,
		rpl::empty_value,
		std::remove_const_t<std::decay_t<Value>>>;
	const auto guarded = QPointer<Object>(object);
	return rpl::make_producer<Produced>([=](auto consumer) {
		if (!guarded) {
			return rpl::lifetime();
		}
		const auto connect = [&](auto &&handler) {
			const auto listener = new QObject(guarded.data());
			QObject::connect(
				guarded,
				signal,
				listener,
				std::forward<decltype(handler)>(handler));
			const auto weak = QPointer<QObject>(listener);
			return rpl::lifetime([=] {
				if (weak) {
					delete weak;
				}
			});
		};
		auto put = [=](const Produced &value) {
			EnterFromEventLoop([&] {
				consumer.put_next_copy(value);
			});
		};
		if constexpr (NoArgument) {
			return connect([put = std::move(put)] { put({}); });
		} else {
			return connect(std::move(put));
		}
	});
}

} // namespace base
