#include "EffectChainWidget.h"
#include "DelayEffectWidget.h"
#include "EffectSelectorDialog.h"
#include "PluginEditorWindow.h"
#include "utils/ThemeManager.h"
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSignalBlocker>
#include <algorithm>

namespace OpenDaw {

// ── EffectSlotWidget ────────────────────────────────────────────────────────

EffectSlotWidget::EffectSlotWidget(te::Plugin* plugin, EditManager* editMgr,
                                   QWidget* parent)
    : QWidget(parent), plugin_(plugin), editMgr_(editMgr)
{
    auto& theme = ThemeManager::instance().current();

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(4, 4, 4, 4);
    layout_->setSpacing(2);

    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surfaceLight);
    setPalette(pal);

    // Header row
    auto* headerRow = new QHBoxLayout();

    nameLabel_ = new QLabel(
        QString::fromStdString(plugin->getName().toStdString()), this);
    nameLabel_->setStyleSheet(
        QString("font-weight: bold; font-size: 11px; color: %1;")
            .arg(theme.text.name()));
    headerRow->addWidget(nameLabel_, 1);

    bypassBtn_ = new QPushButton("Byp", this);
    bypassBtn_->setAccessibleName("Bypass Effect");
    bypassBtn_->setCheckable(true);
    bypassBtn_->setFixedSize(32, 20);
    bypassBtn_->setChecked(!plugin_->isEnabled());
    bypassBtn_->setStyleSheet(
        QString("QPushButton { font-size: 9px; background: %1; color: %2; border: 1px solid %3; border-radius: 2px; }"
                "QPushButton:checked { background: %4; color: #000; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.muteButton.name()));
    connect(bypassBtn_, &QPushButton::toggled, this, [this](bool bypassed) {
        if (plugin_) plugin_->setEnabled(!bypassed);
    });
    headerRow->addWidget(bypassBtn_);

    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin_)) {
        editBtn_ = new QPushButton("UI", this);
        editBtn_->setAccessibleName("Open Plugin Editor");
        editBtn_->setFixedSize(28, 20);
        editBtn_->setToolTip("Open VST Editor");
        editBtn_->setStyleSheet(
            QString("QPushButton { font-size: 9px; background: %1; color: %2; "
                    "border: 1px solid %3; border-radius: 2px; }"
                    "QPushButton:hover { background: %4; }")
                .arg(theme.surface.name(), theme.accent.name(),
                     theme.border.name(), theme.surfaceLight.name()));
        connect(editBtn_, &QPushButton::clicked, this, [ext]() {
            PluginEditorWindow::showForPlugin(*ext);
        });
        headerRow->addWidget(editBtn_);
    }

    removeBtn_ = new QPushButton("X", this);
    removeBtn_->setAccessibleName("Remove Effect");
    removeBtn_->setFixedSize(20, 20);
    removeBtn_->setStyleSheet(
        QString("QPushButton { font-size: 9px; background: %1; color: %2; "
                "border: 1px solid %3; border-radius: 2px; }"
                "QPushButton:hover { background: %4; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.recordArm.name()));
    connect(removeBtn_, &QPushButton::clicked, this, [this]() {
        emit removeRequested(plugin_);
    });
    headerRow->addWidget(removeBtn_);

    layout_->addLayout(headerRow);

    buildControls();
}

void EffectSlotWidget::buildControls()
{
    if (!plugin_) return;

    auto& theme = ThemeManager::instance().current();

    if (plugin_->canSidechain()) {
        auto* scRow = new QHBoxLayout();
        scRow->setSpacing(4);

        auto* scLabel = new QLabel("Sidechain:", this);
        scLabel->setStyleSheet(
            QString("font-size: 9px; color: %1;").arg(theme.textDim.name()));
        scRow->addWidget(scLabel);

        sidechainCombo_ = new QComboBox(this);
        sidechainCombo_->setAccessibleName("Sidechain Source");
        sidechainCombo_->setToolTip("Select which track feeds this plugin's sidechain input");
        sidechainCombo_->setFixedHeight(20);
        sidechainCombo_->setStyleSheet(
            QString("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 2px; font-size: 9px; padding: 1px 3px; }"
                    "QComboBox:hover { border: 1px solid %4; }"
                    "QComboBox::drop-down { width: 12px; }"
                    "QComboBox QAbstractItemView { background: %1; color: %2; "
                    "selection-background-color: %5; font-size: 9px; }")
                .arg(theme.background.name(), theme.text.name(),
                     theme.border.name(), QColor(0, 188, 212).name(),
                     theme.surfaceLight.name()));

        auto sourceNames = plugin_->getSidechainSourceNames(true);
        sidechainCombo_->blockSignals(true);
        for (int i = 0; i < sourceNames.size(); ++i)
            sidechainCombo_->addItem(
                QString::fromStdString(sourceNames[i].toStdString()));

        auto currentName = plugin_->getSidechainSourceName();
        if (currentName.isNotEmpty()) {
            QString suffix = QString::fromStdString(
                ". " + currentName.toStdString());
            for (int i = 0; i < sidechainCombo_->count(); ++i) {
                if (sidechainCombo_->itemText(i).endsWith(suffix)) {
                    sidechainCombo_->setCurrentIndex(i);
                    break;
                }
            }
        }
        sidechainCombo_->blockSignals(false);

        connect(sidechainCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
            if (!plugin_ || idx < 0) return;
            juce::String name(sidechainCombo_->itemText(idx).toStdString());
            plugin_->setSidechainSourceByName(name);
            plugin_->guessSidechainRouting();
            if (editMgr_)
                emit editMgr_->editChanged();
        });

        scRow->addWidget(sidechainCombo_, 1);
        layout_->addLayout(scRow);
    }

    auto params = plugin_->getAutomatableParameters();

    auto* paramsGrid = new QGridLayout();
    paramsGrid->setSpacing(4);

    paramKnobs_.clear();
    const int cols = 4;
    int maxKnobs = std::min(8, static_cast<int>(params.size()));
    for (int i = 0; i < maxKnobs; ++i) {
        auto* param = params[i];
        auto* knob = new RotaryKnob(this);
        knob->setRange(0.0, 1.0);
        knob->setValue(param->getCurrentValue());
        knob->setLabel(QString::fromStdString(param->getParameterName().toStdString()));
        knob->setFixedSize(44, 52);

        connect(knob, &RotaryKnob::valueChanged, this, [param](double v) {
            param->setParameter(float(v), juce::sendNotificationAsync);
        });

        paramsGrid->addWidget(knob, i / cols, i % cols);
        paramKnobs_.append({param, knob});
    }

    layout_->addLayout(paramsGrid);
}

void EffectSlotWidget::updateKnobsFromAutomation()
{
    for (auto& pk : paramKnobs_) {
        if (!pk.param || !pk.knob) continue;
        QSignalBlocker block(pk.knob);
        pk.knob->setValue(double(pk.param->getCurrentValue()));
    }
}

// ── EffectChainWidget ───────────────────────────────────────────────────────

EffectChainWidget::EffectChainWidget(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("Effect Chain");
    auto& theme = ThemeManager::instance().current();

    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.background);
    setPalette(pal);

    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(4, 4, 4, 4);
    mainLayout_->setSpacing(4);

    trackLabel_ = new QLabel("No track selected", this);
    trackLabel_->setStyleSheet(
        QString("font-weight: bold; font-size: 12px; color: %1;")
            .arg(theme.text.name()));
    mainLayout_->addWidget(trackLabel_);

    addBtn_ = new QPushButton("+ Add Effect", this);
    addBtn_->setAccessibleName("Add Effect");
    addBtn_->setFixedHeight(28);
    addBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 3px; font-size: 11px; }"
                "QPushButton:hover { background: %4; }")
            .arg(theme.surface.name(), theme.accent.name(),
                 theme.border.name(), theme.surfaceLight.name()));
    connect(addBtn_, &QPushButton::clicked, this, [this]() {
        if (!targetPluginList()) return;
        EffectSelectorDialog dlg(knownPlugins_, this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedEffect().isEmpty()) {
            if (dlg.isVstEffect())
                addVstEffect(dlg.selectedVstPlugin());
            else
                addEffect(dlg.selectedEffect());
        }
    });
    mainLayout_->addWidget(addBtn_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameStyle(QFrame::NoFrame);
    scrollArea_->setStyleSheet(
        QString("QScrollArea { background: %1; border: none; }")
            .arg(theme.background.name()));

    slotsContainer_ = new QWidget();
    slotsContainer_->setAutoFillBackground(true);
    QPalette slotsPal;
    slotsPal.setColor(QPalette::Window, theme.background);
    slotsContainer_->setPalette(slotsPal);
    slotsLayout_ = new QVBoxLayout(slotsContainer_);
    slotsLayout_->setContentsMargins(0, 0, 0, 0);
    slotsLayout_->setSpacing(2);
    slotsLayout_->addStretch();

    scrollArea_->setWidget(slotsContainer_);
    mainLayout_->addWidget(scrollArea_, 1);

    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(50);
    connect(&rebuildTimer_, &QTimer::timeout, this, &EffectChainWidget::rebuild);

    connect(editMgr_, &EditManager::editChanged, this, [this]() {
        if (track_ || masterMode_) scheduleRebuild();
    });

    // The slots hold raw plugin/parameter pointers into the Edit being replaced.
    connect(editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        rebuildTimer_.stop();
        pendingRemovals_.clear();
        if (track_) {
            track_ = nullptr;
            trackLabel_->setText("No track selected");
        }
        clearSlots();
    });

    connect(editMgr_, &EditManager::tracksChanged, this, [this]() {
        if (track_ && !editMgr_->getAudioTracks().contains(track_))
            setTrack(nullptr);
    });

    connect(&automationPollTimer_, &QTimer::timeout, this, [this]() {
        if (!editMgr_ || !editMgr_->edit()) return;
        for (int i = 0; i < slotsLayout_->count(); ++i) {
            if (auto* slot = qobject_cast<EffectSlotWidget*>(slotsLayout_->itemAt(i)->widget()))
                slot->updateKnobsFromAutomation();
        }
    });
    automationPollTimer_.start(33);
}

void EffectChainWidget::setTrack(te::AudioTrack* track)
{
    track_ = track;
    masterMode_ = false;
    if (track_) {
        trackLabel_->setText(
            QString("FX: %1").arg(
                QString::fromStdString(track_->getName().toStdString())));
    } else {
        trackLabel_->setText("No track selected");
    }
    rebuild();
}

void EffectChainWidget::setMasterMode()
{
    track_ = nullptr;
    masterMode_ = true;
    trackLabel_->setText("FX: Master");
    rebuild();
}

te::PluginList* EffectChainWidget::targetPluginList() const
{
    if (track_)
        return &track_->pluginList;
    if (masterMode_ && editMgr_ && editMgr_->edit())
        return &editMgr_->edit()->getMasterPluginList();
    return nullptr;
}

void EffectChainWidget::scheduleRebuild()
{
    if (!rebuildTimer_.isActive())
        rebuildTimer_.start();
}

void EffectChainWidget::clearSlots()
{
    QLayoutItem* item;
    while ((item = slotsLayout_->takeAt(0)) != nullptr) {
        if (auto* w = item->widget()) {
            // Still alive until deleteLater runs; keep it off EditManager signals.
            QObject::disconnect(editMgr_, nullptr, w, nullptr);
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

void EffectChainWidget::rebuild()
{
    rebuildTimer_.stop();
    if (track_ && editMgr_ && !editMgr_->getAudioTracks().contains(track_)) {
        track_ = nullptr;
        trackLabel_->setText("No track selected");
    }
    clearSlots();

    auto* plist = targetPluginList();
    if (!plist) {
        slotsLayout_->addStretch();
        return;
    }

    for (auto* plugin : plist->getPlugins()) {
        if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;

        if (auto* delayPlugin = dynamic_cast<te::DelayPlugin*>(plugin)) {
            auto* delayWidget = new DelayEffectWidget(
                delayPlugin, editMgr_, slotsContainer_);
            connect(delayWidget, &DelayEffectWidget::removeRequested,
                    this, &EffectChainWidget::removeEffect);
            slotsLayout_->addWidget(delayWidget);
        } else {
            auto* slot = new EffectSlotWidget(plugin, editMgr_, slotsContainer_);
            connect(slot, &EffectSlotWidget::removeRequested,
                    this, &EffectChainWidget::removeEffect);
            slotsLayout_->addWidget(slot);
        }
    }
    slotsLayout_->addStretch();
}

void EffectChainWidget::addEffect(const QString& effectName)
{
    auto* plist = targetPluginList();
    if (!plist || !editMgr_ || !editMgr_->edit()) return;

    auto& cache = editMgr_->edit()->getPluginCache();
    const char* xmlType = nullptr;

    if (effectName == "Reverb")           xmlType = te::ReverbPlugin::xmlTypeName;
    else if (effectName == "EQ")          xmlType = te::EqualiserPlugin::xmlTypeName;
    else if (effectName == "Compressor")  xmlType = te::CompressorPlugin::xmlTypeName;
    else if (effectName == "Delay")       xmlType = te::DelayPlugin::xmlTypeName;
    else if (effectName == "Chorus")      xmlType = te::ChorusPlugin::xmlTypeName;
    else if (effectName == "Phaser")      xmlType = te::PhaserPlugin::xmlTypeName;
    else if (effectName == "Low Pass Filter") xmlType = te::LowPassPlugin::xmlTypeName;
    else if (effectName == "Pitch Shift") xmlType = te::PitchShiftPlugin::xmlTypeName;

    if (xmlType) {
        auto plugin = cache.createNewPlugin(juce::String(xmlType), {});
        if (plugin) {
            int insertIndex = plist->size();
            for (int i = 0; i < plist->size(); ++i) {
                if (dynamic_cast<te::LevelMeterPlugin*>((*plist)[i])) {
                    insertIndex = i;
                    break;
                }
            }
            plist->insertPlugin(plugin, insertIndex, nullptr);
        }
    }

    rebuild();
    emit editMgr_->editChanged();
}

void EffectChainWidget::addVstEffect(const juce::PluginDescription& desc)
{
    auto* plist = targetPluginList();
    if (!plist || !editMgr_ || !editMgr_->edit()) return;

    auto pluginState = te::ExternalPlugin::create(editMgr_->edit()->engine, desc);
    if (auto plugin = editMgr_->edit()->getPluginCache().createNewPlugin(pluginState)) {
        int insertIndex = plist->size();
        for (int i = 0; i < plist->size(); ++i) {
            if (dynamic_cast<te::LevelMeterPlugin*>((*plist)[i])) {
                insertIndex = i;
                break;
            }
        }
        plist->insertPlugin(plugin, insertIndex, nullptr);
    }

    rebuild();
    emit editMgr_->editChanged();
}

void EffectChainWidget::removeEffect(te::Plugin* plugin)
{
    if (!targetPluginList() || !plugin) return;
    plugin->setEnabled(false);
    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
        PluginEditorWindow::closeForPlugin(*ext);
    pendingRemovals_.push_back(plugin);
    QTimer::singleShot(50, this, [this, plugin]() {
        // Gone from the list means the Edit changed and the plugin went with it.
        auto it = std::find_if(pendingRemovals_.begin(), pendingRemovals_.end(),
                               [plugin](const te::Plugin::Ptr& p) { return p.get() == plugin; });
        if (it == pendingRemovals_.end()) return;
        te::Plugin::Ptr keep = *it;
        pendingRemovals_.erase(it);
        if (!keep->getOwnerList()) return;   // already removed (undo, AI)
        keep->deleteFromParent();
        rebuild();
        if (editMgr_) emit editMgr_->editChanged();
    });
}

} // namespace OpenDaw
