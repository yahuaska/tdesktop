/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "data/data_peer.h"

namespace Ui {
class RoundButton;
class VerticalLayout;
} // namespace Ui

class EditPeerPermissionsBox : public Ui::BoxContent {
public:
	EditPeerPermissionsBox(QWidget*, not_null<PeerData*> peer);

	struct Result {
		MTPDchatBannedRights::Flags rights;
		int slowmodeSeconds = 0;
	};

	rpl::producer<Result> saveEvents() const;

protected:
	void prepare() override;

private:
	Fn<int()> addSlowmodeSlider(not_null<Ui::VerticalLayout*> container);
	void addSlowmodeLabels(not_null<Ui::VerticalLayout*> container);
	void addBannedButtons(not_null<Ui::VerticalLayout*> container);

	not_null<PeerData*> _peer;
	Ui::RoundButton *_save = nullptr;
	Fn<Result()> _value;

};

template <typename Flags>
struct EditFlagsControl {
	object_ptr<Ui::RpWidget> widget;
	Fn<Flags()> value;
	rpl::producer<Flags> changes;
};

EditFlagsControl<MTPDchatBannedRights::Flags> CreateEditRestrictions(
	QWidget *parent,
	rpl::producer<QString> header,
	MTPDchatBannedRights::Flags restrictions,
	std::map<MTPDchatBannedRights::Flags, QString> disabledMessages);

EditFlagsControl<MTPDchatAdminRights::Flags> CreateEditAdminRights(
	QWidget *parent,
	rpl::producer<QString> header,
	MTPDchatAdminRights::Flags rights,
	std::map<MTPDchatAdminRights::Flags, QString> disabledMessages,
	bool isGroup,
	bool anyoneCanAddMembers);

ChatAdminRights DisabledByDefaultRestrictions(not_null<PeerData*> peer);
ChatRestrictions FixDependentRestrictions(ChatRestrictions restrictions);
ChatAdminRights FullAdminRights(bool isGroup);
