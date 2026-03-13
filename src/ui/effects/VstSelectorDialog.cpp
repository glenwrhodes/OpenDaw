#include "VstSelectorDialog.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>

namespace freedaw {

VstSelectorDialog::VstSelectorDialog(juce::KnownPluginList& pluginList,
                                     bool instrumentsOnly,
                                     QWidget* parent)
    : QDialog(parent), pluginList_(pluginList), instrumentsOnly_(instrumentsOnly)
{
    setWindowTitle(instrumentsOnly ? "Select Instrument" : "Select Plugin");
    setAccessibleName(instrumentsOnly ? "Instrument Selector" : "Plugin Selector");
    setMinimumSize(500, 500);
    resize(550, 600);

    auto& theme = ThemeManager::instance().current();
    setStyleSheet(
        QString("QDialog { background: %1; }"
                "QLabel { color: %2; }"
                "QLineEdit { background: %3; color: %2; border: 1px solid %4;"
                "  padding: 6px; border-radius: 3px; }"
                "QTreeWidget { background: %3; color: %2; border: 1px solid %4; }"
                "QTreeWidget::item:selected { background: %5; }"
                "QHeaderView::section { background: %1; color: %2; border: 1px solid %4;"
                "  padding: 4px; }")
            .arg(theme.surface.name(), theme.text.name(),
                 theme.background.name(), theme.border.name(),
                 theme.accent.name()));

    auto* layout = new QVBoxLayout(this);

    auto* titleLabel = new QLabel(instrumentsOnly
        ? "Select a virtual instrument:" : "Select a plugin:", this);
    titleLabel->setStyleSheet(
        QString("font-weight: bold; font-size: 13px; color: %1;")
            .arg(theme.text.name()));
    layout->addWidget(titleLabel);

    searchField_ = new QLineEdit(this);
    searchField_->setPlaceholderText("Search...");
    searchField_->setAccessibleName("Plugin Search");
    layout->addWidget(searchField_);

    treeWidget_ = new QTreeWidget(this);
    treeWidget_->setAccessibleName("Plugin List");
    treeWidget_->setHeaderLabels({"Name", "Manufacturer", "Format"});
    treeWidget_->setRootIsDecorated(false);
    treeWidget_->setAlternatingRowColors(true);
    treeWidget_->header()->setStretchLastSection(false);
    treeWidget_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    treeWidget_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    treeWidget_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(treeWidget_, 1);

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox_);

    connect(searchField_, &QLineEdit::textChanged,
            this, &VstSelectorDialog::filterList);

    connect(buttonBox_, &QDialogButtonBox::accepted, this, [this]() {
        auto* item = treeWidget_->currentItem();
        if (item) {
            int idx = item->data(0, Qt::UserRole).toInt();
            if (idx >= 0 && idx < static_cast<int>(allDescs_.size())) {
                selectedDesc_ = allDescs_[static_cast<size_t>(idx)];
                hasSelection_ = true;
            }
        }
        accept();
    });

    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(treeWidget_, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                int idx = item->data(0, Qt::UserRole).toInt();
                if (idx >= 0 && idx < static_cast<int>(allDescs_.size())) {
                    selectedDesc_ = allDescs_[static_cast<size_t>(idx)];
                    hasSelection_ = true;
                }
                accept();
            });

    populateList();
}

void VstSelectorDialog::populateList()
{
    allDescs_.clear();

    for (auto& desc : pluginList_.getTypes()) {
        if (instrumentsOnly_ && !desc.isInstrument)
            continue;
        allDescs_.push_back(desc);
    }

    filterList(searchField_->text());
}

void VstSelectorDialog::filterList(const QString& text)
{
    treeWidget_->clear();

    for (size_t i = 0; i < allDescs_.size(); ++i) {
        auto& desc = allDescs_[i];
        QString name = QString::fromStdString(desc.name.toStdString());
        QString mfr = QString::fromStdString(desc.manufacturerName.toStdString());
        QString fmt = QString::fromStdString(desc.pluginFormatName.toStdString());

        if (!text.isEmpty()) {
            if (!name.contains(text, Qt::CaseInsensitive) &&
                !mfr.contains(text, Qt::CaseInsensitive))
                continue;
        }

        auto* item = new QTreeWidgetItem(treeWidget_);
        item->setText(0, name);
        item->setText(1, mfr);
        item->setText(2, fmt);
        item->setData(0, Qt::UserRole, static_cast<int>(i));
    }
}

} // namespace freedaw
