// PageDocumentItem — inline images: hit-testing, selection, resize handles and text wrapping.
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

QTextImageFormat PageDocumentItem::imageFormatAt(int pos) const
{
    const QTextBlock block = m_doc->findBlock(pos);
    if (!block.isValid())
        return QTextImageFormat();
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid() || !frag.charFormat().isImageFormat())
            continue;
        if (pos >= frag.position() && pos < frag.position() + frag.length())
            return frag.charFormat().toImageFormat();
    }
    return QTextImageFormat();
}

QRectF PageDocumentItem::docRectToItem(const QRectF &d) const
{
    const qreal th = textH();
    const int page = qBound(0, static_cast<int>(d.top() / th), m_pageCount - 1);
    const QRectF tr = textRect(page);
    return QRectF(tr.left() + d.left(), tr.top() + (d.top() - page * th), d.width(), d.height());
}

QRectF PageDocumentItem::imageItemRect(int imagePos) const
{
    const QTextBlock block = m_doc->findBlock(imagePos);
    if (!block.isValid())
        return QRectF();

    QTextImageFormat fmt = imageFormatAt(imagePos);
    qreal w = fmt.width();
    qreal h = fmt.height();
    if (w <= 0 || h <= 0) {
        const QImage im = m_doc->resource(QTextDocument::ImageResource,
                                          QUrl(fmt.name())).value<QImage>();
        if (!im.isNull()) {
            if (w <= 0) w = im.width();
            if (h <= 0) h = im.height();
        }
    }

    // Floating image: take its rect from the laid-out frame.
    if (QTextFrame *frame = imageFrameAt(imagePos)) {
        const qreal m = frame->frameFormat().margin();
        const QRectF fr = m_doc->documentLayout()->frameBoundingRect(frame);
        return docRectToItem(QRectF(fr.left() + m, fr.top() + m, w, h));
    }

    const QRectF blockRect = m_doc->documentLayout()->blockBoundingRect(block);
    qreal docX = blockRect.left();
    qreal docY = blockRect.top();
    if (QTextLayout *layout = block.layout()) {
        const int inBlock = imagePos - block.position();
        const QTextLine line = layout->lineForTextPosition(inBlock);
        if (line.isValid()) {
            docX = blockRect.left() + line.cursorToX(inBlock);
            docY = blockRect.top() + line.y();
            const qreal extra = line.height() - h;   // inline image sits on the baseline
            if (extra > 0)
                docY += extra;
        }
    }
    return docRectToItem(QRectF(docX, docY, w, h));
}

QTextFrame *PageDocumentItem::imageFrameAt(int pos) const
{
    const auto frames = m_doc->rootFrame()->childFrames();
    for (QTextFrame *f : frames) {
        if (f->frameFormat().position() != QTextFrameFormat::InFlow
            && pos >= f->firstPosition() && pos <= f->lastPosition())
            return f;
    }
    return nullptr;
}

int PageDocumentItem::imageAt(const QPointF &itemPos) const
{
    // Inline images in the block under the point.
    const QTextBlock block = m_doc->findBlock(documentPositionAt(itemPos));
    if (block.isValid()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (frag.isValid() && frag.charFormat().isImageFormat()
                && imageItemRect(frag.position()).contains(itemPos))
                return frag.position();
        }
    }
    // Floating images live in child frames, outside the normal block flow.
    const auto frames = m_doc->rootFrame()->childFrames();
    for (QTextFrame *frame : frames) {
        if (frame->frameFormat().position() == QTextFrameFormat::InFlow)
            continue;
        const int imgPos = imageInFrame(frame);
        if (imgPos >= 0 && imageItemRect(imgPos).contains(itemPos))
            return imgPos;
    }
    return -1;
}

int PageDocumentItem::imageInFrame(QTextFrame *frame) const
{
    if (!frame)
        return -1;
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        const QTextBlock b = it.currentBlock();
        if (!b.isValid())
            continue;
        for (auto fit = b.begin(); !fit.atEnd(); ++fit) {
            const QTextFragment frag = fit.fragment();
            if (frag.isValid() && frag.charFormat().isImageFormat())
                return frag.position();
        }
    }
    return -1;
}

PageDocumentItem::WrapMode PageDocumentItem::wrapModeOf(int imagePos) const
{
    QTextFrame *f = imageFrameAt(imagePos);
    if (!f)
        return WrapMode::Inline;
    return f->frameFormat().position() == QTextFrameFormat::FloatRight ? WrapMode::FloatRight
                                                                       : WrapMode::FloatLeft;
}

void PageDocumentItem::changeWrapMode(int imagePos, WrapMode mode)
{
    const WrapMode current = wrapModeOf(imagePos);
    if (current == mode)
        return;
    QTextImageFormat fmt = imageFormatAt(imagePos);
    if (fmt.name().isEmpty())
        return;

    m_cursor.beginEditBlock();
    QTextFrame *frame = imageFrameAt(imagePos);

    if (mode == WrapMode::Inline) {                 // float -> inline
        QTextCursor c(m_doc);
        c.setPosition(frame->firstPosition() - 1);
        c.setPosition(frame->lastPosition() + 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.insertImage(fmt);
        m_selectedImagePos = c.position() - 1;
    } else if (current == WrapMode::Inline) {        // inline -> float
        QTextCursor c(m_doc);
        c.setPosition(imagePos);
        c.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        QTextFrameFormat ff;
        ff.setPosition(mode == WrapMode::FloatLeft ? QTextFrameFormat::FloatLeft
                                                   : QTextFrameFormat::FloatRight);
        ff.setBorder(0);
        ff.setMargin(8);
        ff.setWidth(QTextLength(QTextLength::FixedLength, fmt.width() > 0 ? fmt.width() : 120));
        QTextFrame *nf = c.insertFrame(ff);
        nf->firstCursorPosition().insertImage(fmt);
        m_selectedImagePos = imageInFrame(nf);
    } else {                                         // float-left <-> float-right
        QTextFrameFormat ff = frame->frameFormat();
        ff.setPosition(mode == WrapMode::FloatLeft ? QTextFrameFormat::FloatLeft
                                                   : QTextFrameFormat::FloatRight);
        frame->setFrameFormat(ff);
        m_selectedImagePos = imagePos;
    }

    m_cursor.endEditBlock();
    recomputePages();
    emit contentsChanged();
    notifyCursorUi();
    update();
}

// Only the right / bottom / corner handles resize sanely (a flowed image's
// top-left is pinned by the layout), so we expose just those three.
QPointF PageDocumentItem::handlePoint(const QRectF &r, int index)
{
    switch (index) {
    case 3:  return QPointF(r.right(), r.center().y());   // right edge
    case 5:  return QPointF(r.center().x(), r.bottom());  // bottom edge
    default: return r.bottomRight();                      // 4: corner
    }
}

int PageDocumentItem::handleAt(const QPointF &itemPos) const
{
    if (m_selectedImagePos < 0)
        return -1;
    const QRectF r = imageItemRect(m_selectedImagePos);
    for (int i : {4, 3, 5}) {   // prefer the corner
        const QPointF p = handlePoint(r, i);
        if (QRectF(p.x() - 7, p.y() - 7, 14, 14).contains(itemPos))
            return i;
    }
    return -1;
}

void PageDocumentItem::applyImageSize(int imagePos, qreal w, qreal h)
{
    QTextImageFormat fmt = imageFormatAt(imagePos);
    if (fmt.name().isEmpty())
        return;
    w = qMax(16.0, w);
    h = qMax(16.0, h);
    fmt.setWidth(qRound(w));
    fmt.setHeight(qRound(h));
    QTextCursor c(m_doc);
    c.setPosition(imagePos);
    c.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
    c.setCharFormat(fmt);
    // Keep the floating frame box in step with the image.
    if (QTextFrame *frame = imageFrameAt(imagePos)) {
        QTextFrameFormat ff = frame->frameFormat();
        ff.setWidth(QTextLength(QTextLength::FixedLength, qRound(w)));
        frame->setFrameFormat(ff);
    }
    update();
}

void PageDocumentItem::clearImageSelection()
{
    if (m_selectedImagePos >= 0) {
        m_selectedImagePos = -1;
        update();
    }
}

void PageDocumentItem::insertImage(const QImage &image)
{
    if (image.isNull())
        return;

    // Keep the image at (near) full resolution so it stays crisp when shown
    // small or zoomed in — only the *display* size is fit to the page width.
    // A generous cap bounds memory/file size for very large originals.
    constexpr int kMaxStored = 3000;
    QImage stored = image;
    if (stored.width() > kMaxStored)
        stored = stored.scaledToWidth(kMaxStored, Qt::SmoothTransformation);

    const QString name = QStringLiteral("img://%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    m_doc->addResource(QTextDocument::ImageResource, QUrl(name), stored);

    qreal w = stored.width();
    qreal h = stored.height();
    const qreal maxW = textW();
    if (maxW > 0 && w > maxW) {
        h *= maxW / w;       // fit to text width, preserving aspect
        w = maxW;
    }

    QTextImageFormat fmt;
    fmt.setName(name);
    fmt.setWidth(w);
    fmt.setHeight(h);
    m_cursor.insertImage(fmt);
    afterCursorMoved();
}
