/*
 *  SPDX-FileCopyrightText: 2024 Wolthera van Hövell tot Westerflier <griffinvalley@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "GlyphPaletteProxyModel.h"
#include <KLocalizedString>
#include <QDebug>
#include <data/KoUnicodeBlockData.h>

struct GlyphPaletteProxyModel::Private {
    KoUnicodeBlockData block{KoUnicodeBlockDataFactory::noBlock()};
    int filterIndex{0};
    QString searchText;
};

GlyphPaletteProxyModel::GlyphPaletteProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , d(new Private)
{

}

GlyphPaletteProxyModel::~GlyphPaletteProxyModel()
{
}

int GlyphPaletteProxyModel::blockFilter() const
{
    return d->filterIndex;
}

QString GlyphPaletteProxyModel::searchText() const
{
    return d->searchText;
}

QVariantList GlyphPaletteProxyModel::blockLabels() const
{
    QVariantList labels;

    KoFontGlyphModel *model = qobject_cast<KoFontGlyphModel*>(sourceModel());
    labels.append(QVariantMap({{"name", i18nc("@title", "All glyphs")}, {"value", 0}}));
    if (model) {
        for (int i=0; i < model->blocks().size(); i++) {
            labels.append(QVariantMap({{"name", model->blocks().at(i).name}, {"value", i+1}}));
        }
    }
    return labels;
}

void GlyphPaletteProxyModel::setSearchText(const QString &text)
{
    QString adjustedText = text;
    if (text.startsWith("U+")) {
        bool ok = false;
        const uint code = text.mid(2).toUInt(&ok, 16);
        if (ok) {
            adjustedText = QString::fromUcs4(&code, 1);
        }
    }
    if (!d->searchText.isEmpty() && !adjustedText.isEmpty() && d->searchText.toUcs4().first() == adjustedText.toUcs4().first()) {
        return;
    } else {
        d->searchText = adjustedText.isEmpty()? adjustedText: QString::fromUcs4(&adjustedText.toUcs4().first(), 1);
        emit searchTextChanged();
        invalidateFilter();
    }
}

void GlyphPaletteProxyModel::setBlockFilter(int filter)
{
    if (d->filterIndex == filter) {
        return;
    }
    if (filter == 0) {
        d->filterIndex = 0;
        d->block = KoUnicodeBlockDataFactory::noBlock();
        emit blockFilterChanged();
        invalidateFilter();
    } else {
        KoFontGlyphModel *model = qobject_cast<KoFontGlyphModel*>(sourceModel());
        if (model) {
            int actualFilter = filter - 1;
            if (actualFilter < model->blocks().size()) {
                d->filterIndex = filter;
                d->block = model->blocks().at(actualFilter);
                emit blockFilterChanged();
                invalidateFilter();
            }
        }
    }
}

void GlyphPaletteProxyModel::emitBlockLabelsChanged()
{
    emit blockLabelsChanged();
}

bool GlyphPaletteProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (sourceParent.isValid()) return true;
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString main = sourceModel()->data(idx).toString();
    if (main.isEmpty()) return false;
    const uint firstChar = main.toUcs4().first();

    if (!d->searchText.isEmpty()) {
        const QString decomposition = QChar::decomposition(firstChar);
        const uint searchFirst = d->searchText.toUcs4().first();
        return searchFirst == firstChar
                || (!decomposition.isEmpty() && searchFirst == decomposition.toUcs4().first());
    }
    if (d->filterIndex == 0) return true;

    return d->block.match(firstChar);
}
