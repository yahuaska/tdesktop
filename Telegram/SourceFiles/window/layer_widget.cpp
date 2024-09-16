/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/layer_widget.h"

#include "boxes/abstract_box.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"
#include "window/window_main_menu.h"
#include "app.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"

namespace Window {

class LayerStackWidget::BackgroundWidget : public TWidget {
public:
	BackgroundWidget(QWidget *parent) : TWidget(parent) {
	}

	void setDoneCallback(Fn<void()> callback) {
		_doneCallback = std::move(callback);
	}

	void setLayerBoxes(const QRect &specialLayerBox, const QRect &layerBox);
	void setCacheImages(
		QPixmap &&bodyCache,
		QPixmap &&mainMenuCache,
		QPixmap &&specialLayerCache,
		QPixmap &&layerCache);
	void removeBodyCache();
	void startAnimation(Action action);
	void skipAnimation(Action action);
	void finishAnimating();

	bool animating() const {
		return _a_mainMenuShown.animating() || _a_specialLayerShown.animating() || _a_layerShown.animating();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	bool isShown() const {
		return _mainMenuShown || _specialLayerShown || _layerShown;
	}
	void checkIfDone();
	void setMainMenuShown(bool shown);
	void setSpecialLayerShown(bool shown);
	void setLayerShown(bool shown);
	void checkWasShown(bool wasShown);
	void animationCallback();

	QPixmap _bodyCache;
	QPixmap _mainMenuCache;
	int _mainMenuCacheWidth = 0;
	QPixmap _specialLayerCache;
	QPixmap _layerCache;

	Fn<void()> _doneCallback;

	bool _wasAnimating = false;
	bool _inPaintEvent = false;
	Ui::Animations::Simple _a_shown;
	Ui::Animations::Simple _a_mainMenuShown;
	Ui::Animations::Simple _a_specialLayerShown;
	Ui::Animations::Simple _a_layerShown;

	QRect _specialLayerBox, _specialLayerCacheBox;
	QRect _layerBox, _layerCacheBox;
	int _mainMenuRight = 0;

	bool _mainMenuShown = false;
	bool _specialLayerShown = false;
	bool _layerShown = false;

};

void LayerStackWidget::BackgroundWidget::setCacheImages(
		QPixmap &&bodyCache,
		QPixmap &&mainMenuCache,
		QPixmap &&specialLayerCache,
		QPixmap &&layerCache) {
	_bodyCache = std::move(bodyCache);
	_mainMenuCache = std::move(mainMenuCache);
	_specialLayerCache = std::move(specialLayerCache);
	_layerCache = std::move(layerCache);
	_specialLayerCacheBox = _specialLayerBox;
	_layerCacheBox = _layerBox;
	setAttribute(Qt::WA_OpaquePaintEvent, !_bodyCache.isNull());
}

void LayerStackWidget::BackgroundWidget::removeBodyCache() {
	if (!_bodyCache.isNull()) {
		_bodyCache = {};
		setAttribute(Qt::WA_OpaquePaintEvent, false);
	}
}

void LayerStackWidget::BackgroundWidget::startAnimation(Action action) {
	if (action == Action::ShowMainMenu) {
		setMainMenuShown(true);
	} else if (action != Action::HideLayer
		&& action != Action::HideSpecialLayer) {
		setMainMenuShown(false);
	}
	if (action == Action::ShowSpecialLayer) {
		setSpecialLayerShown(true);
	} else if (action == Action::ShowMainMenu
		|| action == Action::HideAll
		|| action == Action::HideSpecialLayer) {
		setSpecialLayerShown(false);
	}
	if (action == Action::ShowLayer) {
		setLayerShown(true);
	} else if (action != Action::ShowSpecialLayer
		&& action != Action::HideSpecialLayer) {
		setLayerShown(false);
	}
	_wasAnimating = true;
	checkIfDone();
}

void LayerStackWidget::BackgroundWidget::skipAnimation(Action action) {
	startAnimation(action);
	finishAnimating();
}

void LayerStackWidget::BackgroundWidget::checkIfDone() {
	if (!_wasAnimating || _inPaintEvent || animating()) {
		return;
	}
	_wasAnimating = false;
	_mainMenuCache = _specialLayerCache = _layerCache = QPixmap();
	removeBodyCache();
	if (_doneCallback) {
		_doneCallback();
	}
}

void LayerStackWidget::BackgroundWidget::setMainMenuShown(bool shown) {
	auto wasShown = isShown();
	if (_mainMenuShown != shown) {
		_mainMenuShown = shown;
		_a_mainMenuShown.start([this] { animationCallback(); }, _mainMenuShown ? 0. : 1., _mainMenuShown ? 1. : 0., st::boxDuration, anim::easeOutCirc);
	}
	_mainMenuCacheWidth = (_mainMenuCache.width() / cIntRetinaFactor()) - st::boxRoundShadow.extend.right();
	_mainMenuRight = _mainMenuShown ? _mainMenuCacheWidth : 0;
	checkWasShown(wasShown);
}

void LayerStackWidget::BackgroundWidget::setSpecialLayerShown(bool shown) {
	auto wasShown = isShown();
	if (_specialLayerShown != shown) {
		_specialLayerShown = shown;
		_a_specialLayerShown.start([this] { animationCallback(); }, _specialLayerShown ? 0. : 1., _specialLayerShown ? 1. : 0., st::boxDuration);
	}
	checkWasShown(wasShown);
}

void LayerStackWidget::BackgroundWidget::setLayerShown(bool shown) {
	auto wasShown = isShown();
	if (_layerShown != shown) {
		_layerShown = shown;
		_a_layerShown.start([this] { animationCallback(); }, _layerShown ? 0. : 1., _layerShown ? 1. : 0., st::boxDuration);
	}
	checkWasShown(wasShown);
}

void LayerStackWidget::BackgroundWidget::checkWasShown(bool wasShown) {
	if (isShown() != wasShown) {
		_a_shown.start([this] { animationCallback(); }, wasShown ? 1. : 0., wasShown ? 0. : 1., st::boxDuration, anim::easeOutCirc);
	}
}

void LayerStackWidget::BackgroundWidget::setLayerBoxes(const QRect &specialLayerBox, const QRect &layerBox) {
	_specialLayerBox = specialLayerBox;
	_layerBox = layerBox;
	update();
}

void LayerStackWidget::BackgroundWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	_inPaintEvent = true;
	auto guard = gsl::finally([this] {
		_inPaintEvent = false;
		crl::on_main(this, [=] { checkIfDone(); });
	});

	if (!_bodyCache.isNull()) {
		p.drawPixmap(0, 0, _bodyCache);
	}

	auto specialLayerBox = _specialLayerCache.isNull() ? _specialLayerBox : _specialLayerCacheBox;
	auto layerBox = _layerCache.isNull() ? _layerBox : _layerCacheBox;

	auto mainMenuProgress = _a_mainMenuShown.value(-1);
	auto mainMenuRight = (_mainMenuCache.isNull() || mainMenuProgress < 0) ? _mainMenuRight : (mainMenuProgress < 0) ? _mainMenuRight : anim::interpolate(0, _mainMenuCacheWidth, mainMenuProgress);
	if (mainMenuRight) {
		// Move showing boxes to the right while main menu is hiding.
		if (!_specialLayerCache.isNull()) {
			specialLayerBox.moveLeft(specialLayerBox.left() + mainMenuRight / 2);
		}
		if (!_layerCache.isNull()) {
			layerBox.moveLeft(layerBox.left() + mainMenuRight / 2);
		}
	}
	auto bgOpacity = _a_shown.value(isShown() ? 1. : 0.);
	auto specialLayerOpacity = _a_specialLayerShown.value(_specialLayerShown ? 1. : 0.);
	auto layerOpacity = _a_layerShown.value(_layerShown ? 1. : 0.);
	if (bgOpacity == 0.) {
		return;
	}

	p.setOpacity(bgOpacity);
	auto overSpecialOpacity = (layerOpacity * specialLayerOpacity);
	auto bg = myrtlrect(mainMenuRight, 0, width() - mainMenuRight, height());

	if (_mainMenuCache.isNull() && mainMenuRight > 0) {
		// All cache images are taken together with their shadows,
		// so we paint shadow only when there is no cache.
		Ui::Shadow::paint(p, myrtlrect(0, 0, mainMenuRight, height()), width(), st::boxRoundShadow, RectPart::Right);
	}

	if (_specialLayerCache.isNull() && !specialLayerBox.isEmpty()) {
		// All cache images are taken together with their shadows,
		// so we paint shadow only when there is no cache.
		auto sides = RectPart::Left | RectPart::Right;
		auto topCorners = (specialLayerBox.y() > 0);
		auto bottomCorners = (specialLayerBox.y() + specialLayerBox.height() < height());
		if (topCorners) {
			sides |= RectPart::Top;
		}
		if (bottomCorners) {
			sides |= RectPart::Bottom;
		}
		if (topCorners || bottomCorners) {
			p.setClipRegion(QRegion(rect()) - specialLayerBox.marginsRemoved(QMargins(st::boxRadius, 0, st::boxRadius, 0)) - specialLayerBox.marginsRemoved(QMargins(0, st::boxRadius, 0, st::boxRadius)));
		}
		Ui::Shadow::paint(p, specialLayerBox, width(), st::boxRoundShadow, sides);

		if (topCorners || bottomCorners) {
			// In case of painting the shadow above the special layer we get
			// glitches in the corners, so we need to paint the corners once more.
			p.setClipping(false);
			auto parts = (topCorners ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
				| (bottomCorners ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None);
			App::roundRect(p, specialLayerBox, st::boxBg, BoxCorners, nullptr, parts);
		}
	}

	if (!layerBox.isEmpty() && !_specialLayerCache.isNull() && overSpecialOpacity < bgOpacity) {
		// In case of moving special layer below the background while showing a box
		// we need to fill special layer rect below its cache with a complex opacity
		// (alpha_final - alpha_current) / (1 - alpha_current) so we won't get glitches
		// in the transparent special layer cache corners after filling special layer
		// rect above its cache with alpha_current opacity.
		auto region = QRegion(bg) - specialLayerBox;
		for (auto rect : region.rects()) {
			p.fillRect(rect, st::layerBg);
		}
		p.setOpacity((bgOpacity - overSpecialOpacity) / (1. - (overSpecialOpacity * st::layerBg->c.alphaF())));
		p.fillRect(specialLayerBox, st::layerBg);
		p.setOpacity(bgOpacity);
	} else {
		p.fillRect(bg, st::layerBg);
	}

	if (!_specialLayerCache.isNull() && specialLayerOpacity > 0) {
		p.setOpacity(specialLayerOpacity);
		auto cacheLeft = specialLayerBox.x() - st::boxRoundShadow.extend.left();
		auto cacheTop = specialLayerBox.y() - (specialLayerBox.y() > 0 ? st::boxRoundShadow.extend.top() : 0);
		p.drawPixmapLeft(cacheLeft, cacheTop, width(), _specialLayerCache);
	}
	if (!layerBox.isEmpty()) {
		if (!_specialLayerCache.isNull()) {
			p.setOpacity(overSpecialOpacity);
			p.fillRect(specialLayerBox, st::layerBg);
		}
		if (_layerCache.isNull()) {
			p.setOpacity(layerOpacity);
			Ui::Shadow::paint(p, layerBox, width(), st::boxRoundShadow);
		}
	}
	if (!_layerCache.isNull() && layerOpacity > 0) {
		p.setOpacity(layerOpacity);
		p.drawPixmapLeft(layerBox.topLeft() - QPoint(st::boxRoundShadow.extend.left(), st::boxRoundShadow.extend.top()), width(), _layerCache);
	}
	if (!_mainMenuCache.isNull() && mainMenuRight > 0) {
		p.setOpacity(1.);
		auto shownWidth = mainMenuRight + st::boxRoundShadow.extend.right();
		auto sourceWidth = shownWidth * cIntRetinaFactor();
		auto sourceRect = style::rtlrect(_mainMenuCache.width() - sourceWidth, 0, sourceWidth, _mainMenuCache.height(), _mainMenuCache.width());
		p.drawPixmapLeft(0, 0, shownWidth, height(), width(), _mainMenuCache, sourceRect);
	}
}

void LayerStackWidget::BackgroundWidget::finishAnimating() {
	_a_shown.stop();
	_a_mainMenuShown.stop();
	_a_specialLayerShown.stop();
	_a_layerShown.stop();
	checkIfDone();
}

void LayerStackWidget::BackgroundWidget::animationCallback() {
	update();
	checkIfDone();
}

LayerStackWidget::LayerStackWidget(QWidget *parent)
: RpWidget(parent)
, _background(this) {
	setGeometry(parentWidget()->rect());
	hide();
	_background->setDoneCallback([this] { animationDone(); });
}

void LayerWidget::setInnerFocus() {
	if (!isAncestorOf(window()->focusWidget())) {
		doSetInnerFocus();
	}
}

bool LayerWidget::overlaps(const QRect &globalRect) {
	if (isHidden()) {
		return false;
	}
	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	if (testAttribute(Qt::WA_OpaquePaintEvent)) {
		return rect().contains(testRect);
	}
	if (QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius).contains(testRect)) {
		return true;
	}
	if (QRect(st::boxRadius, 0, width() - 2 * st::boxRadius, height()).contains(testRect)) {
		return true;
	}
	return false;
}

void LayerWidget::mousePressEvent(QMouseEvent *e) {
	e->accept();
}

void LayerWidget::resizeEvent(QResizeEvent *e) {
	if (_resizedCallback) {
		_resizedCallback();
	}
}

void LayerStackWidget::setHideByBackgroundClick(bool hide) {
	_hideByBackgroundClick = hide;
}

void LayerStackWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		hideCurrent(anim::type::normal);
	}
}

void LayerStackWidget::mousePressEvent(QMouseEvent *e) {
	Ui::PostponeCall(this, [=] { backgroundClicked(); });
}

void LayerStackWidget::backgroundClicked() {
	if (!_hideByBackgroundClick) {
		return;
	}
	if (const auto layer = currentLayer()) {
		if (!layer->closeByOutsideClick()) {
			return;
		}
	} else if (const auto special = _specialLayer.data()) {
		if (!special->closeByOutsideClick()) {
			return;
		}
	}
	hideCurrent(anim::type::normal);
}

void LayerStackWidget::hideCurrent(anim::type animated) {
	return currentLayer() ? hideLayers(animated) : hideAll(animated);
}

void LayerStackWidget::hideLayers(anim::type animated) {
	startAnimation([] {}, [&] {
		clearLayers();
	}, Action::HideLayer, animated);
}

void LayerStackWidget::hideAll(anim::type animated) {
	startAnimation([] {}, [&] {
		clearLayers();
		clearSpecialLayer();
		_mainMenu.destroy();
	}, Action::HideAll, animated);
}

void LayerStackWidget::hideTopLayer(anim::type animated) {
	if (_specialLayer || _mainMenu) {
		hideLayers(animated);
	} else {
		hideAll(animated);
	}
}

void LayerStackWidget::removeBodyCache() {
	_background->removeBodyCache();
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

bool LayerStackWidget::layerShown() const {
	return _specialLayer || currentLayer() || _mainMenu;
}

void LayerStackWidget::setCacheImages() {
	auto bodyCache = QPixmap(), mainMenuCache = QPixmap();
	auto specialLayerCache = QPixmap();
	if (_specialLayer) {
		Ui::SendPendingMoveResizeEvents(_specialLayer);
		auto sides = RectPart::Left | RectPart::Right;
		if (_specialLayer->y() > 0) {
			sides |= RectPart::Top;
		}
		if (_specialLayer->y() + _specialLayer->height() < height()) {
			sides |= RectPart::Bottom;
		}
		specialLayerCache = Ui::Shadow::grab(_specialLayer, st::boxRoundShadow, sides);
	}
	auto layerCache = QPixmap();
	if (auto layer = currentLayer()) {
		layerCache = Ui::Shadow::grab(layer, st::boxRoundShadow);
	}
	if (isAncestorOf(window()->focusWidget())) {
		setFocus();
	}
	if (_mainMenu) {
		removeBodyCache();
		hideChildren();
		bodyCache = Ui::GrabWidget(parentWidget());
		showChildren();
		mainMenuCache = Ui::Shadow::grab(_mainMenu, st::boxRoundShadow, RectPart::Right);
	}
	setAttribute(Qt::WA_OpaquePaintEvent, !bodyCache.isNull());
	updateLayerBoxes();
	_background->setCacheImages(std::move(bodyCache), std::move(mainMenuCache), std::move(specialLayerCache), std::move(layerCache));
}

void LayerStackWidget::closeLayer(not_null<LayerWidget*> layer) {
	const auto weak = Ui::MakeWeak(layer.get());
	if (weak->inFocusChain()) {
		setFocus();
	}
	if (!weak || !weak->setClosing()) {
		// This layer is already closing.
		return;
	} else if (!weak) {
		// setClosing() could've killed the layer.
		return;
	}

	if (layer == _specialLayer) {
		hideAll(anim::type::normal);
	} else if (layer == currentLayer()) {
		if (_layers.size() == 1) {
			hideCurrent(anim::type::normal);
		} else {
			const auto taken = std::move(_layers.back());
			_layers.pop_back();

			layer = currentLayer();
			layer->parentResized();
			if (!_background->animating()) {
				layer->show();
				showFinished();
			}
		}
	} else {
		for (auto i = _layers.begin(), e = _layers.end(); i != e; ++i) {
			if (layer == i->get()) {
				const auto taken = std::move(*i);
				_layers.erase(i);
				break;
			}
		}
	}
}

void LayerStackWidget::updateLayerBoxes() {
	const auto layerBox = [&] {
		if (const auto layer = currentLayer()) {
			return layer->geometry();
		}
		return QRect();
	}();
	const auto specialLayerBox = _specialLayer
		? _specialLayer->geometry()
		: QRect();
	_background->setLayerBoxes(specialLayerBox, layerBox);
	update();
}

void LayerStackWidget::finishAnimating() {
	_background->finishAnimating();
}

bool LayerStackWidget::canSetFocus() const {
	return (currentLayer() || _specialLayer || _mainMenu);
}

void LayerStackWidget::setInnerFocus() {
	if (_background->animating()) {
		setFocus();
	} else if (auto l = currentLayer()) {
		l->setInnerFocus();
	} else if (_specialLayer) {
		_specialLayer->setInnerFocus();
	} else if (_mainMenu) {
		_mainMenu->setInnerFocus();
	}
}

bool LayerStackWidget::contentOverlapped(const QRect &globalRect) {
	if (isHidden()) {
		return false;
	}
	if (_specialLayer && _specialLayer->overlaps(globalRect)) {
		return true;
	}
	if (auto layer = currentLayer()) {
		return layer->overlaps(globalRect);
	}
	return false;
}

template <typename SetupNew, typename ClearOld>
void LayerStackWidget::startAnimation(
		SetupNew setupNewWidgets,
		ClearOld clearOldWidgets,
		Action action,
		anim::type animated) {
	if (animated == anim::type::instant) {
		setupNewWidgets();
		clearOldWidgets();
		prepareForAnimation();
		_background->skipAnimation(action);
	} else {
		setupNewWidgets();
		setCacheImages();
		const auto weak = Ui::MakeWeak(this);
		clearOldWidgets();
		if (weak) {
			prepareForAnimation();
			_background->startAnimation(action);
		}
	}
}

void LayerStackWidget::resizeEvent(QResizeEvent *e) {
	const auto weak = Ui::MakeWeak(this);
	_background->setGeometry(rect());
	if (!weak) {
		return;
	}
	if (_specialLayer) {
		_specialLayer->parentResized();
		if (!weak) {
			return;
		}
	}
	if (const auto layer = currentLayer()) {
		layer->parentResized();
		if (!weak) {
			return;
		}
	}
	if (_mainMenu) {
		_mainMenu->resize(_mainMenu->width(), height());
		if (!weak) {
			return;
		}
	}
	updateLayerBoxes();
}

void LayerStackWidget::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	if (options & LayerOption::KeepOther) {
		if (options & LayerOption::ShowAfterOther) {
			prependBox(std::move(box), animated);
		} else {
			appendBox(std::move(box), animated);
		}
	} else {
		replaceBox(std::move(box), animated);
	}
}

void LayerStackWidget::replaceBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	const auto pointer = pushBox(std::move(box), animated);
	const auto removeTill = ranges::find(
		_layers,
		pointer,
		&std::unique_ptr<LayerWidget>::get);
	_closingLayers.insert(
		end(_closingLayers),
		std::make_move_iterator(begin(_layers)),
		std::make_move_iterator(removeTill));
	_layers.erase(begin(_layers), removeTill);
	clearClosingLayers();
}

void LayerStackWidget::prepareForAnimation() {
	if (isHidden()) {
		show();
	}
	if (_mainMenu) {
		_mainMenu->hide();
	}
	if (_specialLayer) {
		_specialLayer->hide();
	}
	if (const auto layer = currentLayer()) {
		layer->hide();
	}
}

void LayerStackWidget::animationDone() {
	bool hidden = true;
	if (_mainMenu) {
		_mainMenu->show();
		hidden = false;
	}
	if (_specialLayer) {
		_specialLayer->show();
		hidden = false;
	}
	if (auto layer = currentLayer()) {
		layer->show();
		hidden = false;
	}
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	if (hidden) {
		_hideFinishStream.fire({});
	} else {
		showFinished();
	}
}

rpl::producer<> LayerStackWidget::hideFinishEvents() const {
	return _hideFinishStream.events();
}

void LayerStackWidget::showFinished() {
	fixOrder();
	sendFakeMouseEvent();
	updateLayerBoxes();
	if (_specialLayer) {
		_specialLayer->showFinished();
	}
	if (auto layer = currentLayer()) {
		layer->showFinished();
	}
	if (canSetFocus()) {
		setInnerFocus();
	}
}

void LayerStackWidget::showSpecialLayer(
		object_ptr<LayerWidget> layer,
		anim::type animated) {
	startAnimation([&] {
		_specialLayer.destroy();
		_specialLayer = std::move(layer);
		initChildLayer(_specialLayer);
	}, [&] {
		_mainMenu.destroy();
	}, Action::ShowSpecialLayer, animated);
}

bool LayerStackWidget::showSectionInternal(
		not_null<Window::SectionMemento *> memento,
		const SectionShow &params) {
	if (_specialLayer) {
		return _specialLayer->showSectionInternal(memento, params);
	}
	return false;
}

void LayerStackWidget::hideSpecialLayer(anim::type animated) {
	startAnimation([] {}, [&] {
		clearSpecialLayer();
		_mainMenu.destroy();
	}, Action::HideSpecialLayer, animated);
}

void LayerStackWidget::showMainMenu(
		not_null<Window::SessionController*> controller,
		anim::type animated) {
	startAnimation([&] {
		_mainMenu.create(this, controller);
		_mainMenu->setGeometryToLeft(0, 0, _mainMenu->width(), height());
		_mainMenu->setParent(this);
	}, [&] {
		clearLayers();
		_specialLayer.destroy();
	}, Action::ShowMainMenu, animated);
}

void LayerStackWidget::appendBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	pushBox(std::move(box), animated);
}

LayerWidget *LayerStackWidget::pushBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	auto oldLayer = currentLayer();
	if (oldLayer) {
		if (oldLayer->inFocusChain()) setFocus();
		oldLayer->hide();
	}
	_layers.push_back(std::make_unique<AbstractBox>(this, std::move(box)));
	const auto raw = _layers.back().get();
	initChildLayer(raw);

	if (_layers.size() > 1) {
		if (!_background->animating()) {
			raw->setVisible(true);
			showFinished();
		}
	} else {
		startAnimation([] {}, [&] {
			_mainMenu.destroy();
		}, Action::ShowLayer, animated);
	}

	return raw;
}

void LayerStackWidget::prependBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	if (_layers.empty()) {
		replaceBox(std::move(box), animated);
		return;
	}
	_layers.insert(
		begin(_layers),
		std::make_unique<AbstractBox>(this, std::move(box)));
	const auto raw = _layers.front().get();
	raw->hide();
	initChildLayer(raw);
}

bool LayerStackWidget::takeToThirdSection() {
	return _specialLayer
		? _specialLayer->takeToThirdSection()
		: false;
}

void LayerStackWidget::clearLayers() {
	_closingLayers.insert(
		end(_closingLayers),
		std::make_move_iterator(begin(_layers)),
		std::make_move_iterator(end(_layers)));
	_layers.clear();
	clearClosingLayers();
}

void LayerStackWidget::clearClosingLayers() {
	const auto weak = Ui::MakeWeak(this);
	while (!_closingLayers.empty()) {
		const auto index = _closingLayers.size() - 1;
		const auto layer = _closingLayers.back().get();
		if (layer->inFocusChain()) {
			setFocus();
		}

		// This may destroy LayerStackWidget (by calling Ui::hideLayer).
		// So each time we check a weak pointer (if we are still alive).
		layer->setClosing();

		// setClosing() could destroy 'this' or could call clearLayers().
		if (weak && !_closingLayers.empty()) {
			// We could enqueue more closing layers, so we remove by index.
			Assert(index < _closingLayers.size());
			Assert(_closingLayers[index].get() == layer);
			_closingLayers.erase(begin(_closingLayers) + index);
		} else {
			// Everything was destroyed in clearLayers or ~LayerStackWidget.
			break;
		}
	}
}

void LayerStackWidget::clearSpecialLayer() {
	if (_specialLayer) {
		_specialLayer->setClosing();
		_specialLayer.destroy();
	}
}

void LayerStackWidget::initChildLayer(LayerWidget *layer) {
	layer->setParent(this);
	layer->setClosedCallback([=] { closeLayer(layer); });
	layer->setResizedCallback([=] { updateLayerBoxes(); });
	Ui::SendPendingMoveResizeEvents(layer);
	layer->parentResized();
}

void LayerStackWidget::fixOrder() {
	if (auto layer = currentLayer()) {
		_background->raise();
		layer->raise();
	} else if (_specialLayer) {
		_specialLayer->raise();
	}
	if (_mainMenu) {
		_mainMenu->raise();
	}
}

void LayerStackWidget::sendFakeMouseEvent() {
	SendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
}

LayerStackWidget::~LayerStackWidget() {
	// Some layer destructors call back into LayerStackWidget.
	while (!_layers.empty() || !_closingLayers.empty()) {
		hideAll(anim::type::instant);
		clearClosingLayers();
	}
}

} // namespace Window
