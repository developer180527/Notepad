// Freedesktop thumbnailer for Notepad .note files (GNOME/KDE/XFCE etc).
// Invoked as:  note-thumbnailer <input.note> <output.png> <size>
//
// It reads the embedded preview PNG (.note v3+) and writes it scaled to <size>.
// No QApplication is needed — QImage load/scale/save works standalone.

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QString>

int main(int argc, char *argv[])
{
    if (argc < 3)
        return 1;

    const QString inPath = QString::fromLocal8Bit(argv[1]);
    const QString outPath = QString::fromLocal8Bit(argv[2]);
    const int size = (argc > 3) ? QString::fromLocal8Bit(argv[3]).toInt() : 256;

    QFile file(inPath);
    if (!file.open(QIODevice::ReadOnly))
        return 1;

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_5);

    QByteArray magic;
    quint32 version = 0;
    in >> magic >> version;
    if (magic != QByteArray("PPNOTE") || version < 3)
        return 1;     // no embedded preview before v3

    QByteArray png;
    in >> png;
    if (png.isEmpty())
        return 1;

    QImage image;
    if (!image.loadFromData(png, "PNG"))
        return 1;

    if (size > 0 && (image.width() > size || image.height() > size))
        image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return image.save(outPath, "PNG") ? 0 : 1;
}
