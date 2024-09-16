/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

<<<<<<< HEAD:Telegram/SourceFiles/ui/main_queue_processor.h
namespace Ui {
=======
#include "base/integration.h"

namespace Core {
>>>>>>> pr:Telegram/SourceFiles/core/base_integration.h

class BaseIntegration : public base::Integration {
public:
	BaseIntegration(int argc, char *argv[]);

	void enterFromEventLoop(FnMut<void()> &&method) override;
	void logMessage(const QString &message) override;
	void logAssertionViolation(const QString &info) override;

};

} // namespace Ui
