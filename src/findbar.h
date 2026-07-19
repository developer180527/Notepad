#ifndef FINDBAR_H
#define FINDBAR_H

#include <QWidget>

class QLineEdit;
class QCheckBox;

// A slim find/replace bar shown above the canvas. It owns no search logic — it
// just emits intent; MainWindow drives the document.
class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    void activate();          // show, focus and select the find field
    void dismiss();           // hide and notify (close button, Esc, Ctrl/Cmd+F toggle)

signals:
    // Emitted live as the find text / options change, for highlighting all matches.
    void highlightRequested(const QString &text, bool caseSensitive, bool wholeWords);
    void findRequested(const QString &text, bool forward, bool caseSensitive, bool wholeWords);
    void replaceRequested(const QString &text, const QString &with, bool caseSensitive,
                          bool wholeWords);
    void replaceAllRequested(const QString &text, const QString &with, bool caseSensitive,
                             bool wholeWords);
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool caseSensitive() const;
    bool wholeWords() const;
    void emitFind(bool forward);

    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QCheckBox *m_case = nullptr;
    QCheckBox *m_whole = nullptr;
};

#endif // FINDBAR_H
