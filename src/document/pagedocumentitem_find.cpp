// PageDocumentItem — find/replace, search highlighting and the .note preview render.
//
// Part of the PageDocumentItem implementation, split across several files by
// responsibility; see pagedocumentitem.h for the class definition.

#include "document/pagedocumentitem.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QFileInfo>
#include <QFocusEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionGraphicsItem>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QUrl>
#include <QTimer>
#include <QUrl>
#include <cmath>

QImage PageDocumentItem::renderPreview(int maxWidthPx) const
{
    const qreal sheetW = m_sheetSize.width();
    const qreal sheetH = m_sheetSize.height();
    if (sheetW <= 0 || sheetH <= 0 || maxWidthPx <= 0)
        return QImage();

    const qreal scale = maxWidthPx / sheetW;
    QImage img(QSize(maxWidthPx, qRound(sheetH * scale)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.scale(scale, scale);

    // The white sheet with a hairline border.
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(220, 220, 220), 1));
    p.drawRect(QRectF(0, 0, sheetW, sheetH));

    // Page 1's slice of the document, inset by the margins.
    const QRectF tr(m_margins.left(), m_margins.top(), textW(), textH());
    p.save();
    p.setClipRect(tr);
    p.translate(tr.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    QPalette pal;
    pal.setColor(QPalette::Text, Qt::black);
    ctx.palette = pal;
    ctx.clip = QRectF(0, 0, textW(), textH());
    m_doc->documentLayout()->draw(&p, ctx);
    p.restore();
    p.end();
    return img;
}

void PageDocumentItem::setSearchHighlight(const QString &text, QTextDocument::FindFlags flags)
{
    m_searchText = text;
    m_searchFlags = flags;
    recomputeSearchMatches();
    update();
}

void PageDocumentItem::recomputeSearchMatches()
{

    m_matchesByPage.clear();
    if (m_searchText.isEmpty())
        return;
    const QTextDocument::FindFlags flags = m_searchFlags & ~QTextDocument::FindBackward;
    QTextCursor c(m_doc);
    int guard = 0;
    forever {
        c = m_doc->find(m_searchText, c, flags);
        if (c.isNull())
            break;
        m_matchesByPage[pageForPosition(c.selectionStart())].append(c);
        if (++guard >= 5000)   // safety cap for pathological documents
            break;
    }
}

int PageDocumentItem::pageForPosition(int pos) const
{
    const QTextBlock block = m_doc->findBlock(pos);
    if (!block.isValid())
        return 0;
    qreal y = m_doc->documentLayout()->blockBoundingRect(block).top();
    if (QTextLayout *layout = block.layout()) {
        const QTextLine line = layout->lineForTextPosition(pos - block.position());
        if (line.isValid())
            y += line.y();
    }
    const qreal th = textH();
    return th > 0 ? qMax(0, static_cast<int>(y / th)) : 0;
}

bool PageDocumentItem::find(const QString &text, QTextDocument::FindFlags flags)
{
    if (text.isEmpty())
        return false;
    QTextCursor found = m_doc->find(text, m_cursor, flags);
    if (found.isNull()) {
        // Wrap around.
        QTextCursor start(m_doc);
        if (flags & QTextDocument::FindBackward)
            start.movePosition(QTextCursor::End);
        found = m_doc->find(text, start, flags);
    }
    if (found.isNull())
        return false;
    setCursorAndNotify(found);
    return true;
}

bool PageDocumentItem::replaceSelection(const QString &text, const QString &with,
                                        QTextDocument::FindFlags flags)
{
    const bool cs = flags & QTextDocument::FindCaseSensitively;
    if (m_cursor.hasSelection()
        && QString::compare(m_cursor.selectedText(), text,
                            cs ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0) {
        m_cursor.insertText(with);
        afterCursorMoved();
    }
    return find(text, flags);
}

int PageDocumentItem::replaceAll(const QString &text, const QString &with,
                                 QTextDocument::FindFlags flags)
{
    if (text.isEmpty())
        return 0;
    int count = 0;
    QTextCursor editor(m_doc);
    editor.beginEditBlock();
    QTextCursor search(m_doc);
    search.movePosition(QTextCursor::Start);
    QTextDocument::FindFlags forward = flags & ~QTextDocument::FindBackward;
    forever {
        search = m_doc->find(text, search, forward);
        if (search.isNull())
            break;
        search.insertText(with);
        ++count;
    }
    editor.endEditBlock();
    if (count > 0) {
        m_typingFormat = m_cursor.charFormat();
        notifyCursorUi();
        update();
    }
    return count;
}
