#include "fontlibrary.h"

#include <QFile>
#include <QFontDatabase>
#include <QHash>

namespace {
struct Library {
    QStringList families;
    QHash<QString, QByteArray> bytesByFamily;
    QString defaultFamily;
    bool loaded = false;
};
Library &lib()
{
    static Library instance;
    return instance;
}

// Each bundled face: resource path + whether it's our preferred default sans.
struct Bundled { const char *path; bool preferred; };
const Bundled kBundled[] = {
    {":/fonts/Inter.ttf", true},
    {":/fonts/Lora.ttf", false},
    {":/fonts/JetBrainsMono.ttf", false},
};
} // namespace

namespace FontLibrary {

void load()
{
    Library &l = lib();
    if (l.loaded)
        return;
    l.loaded = true;

    for (const Bundled &b : kBundled) {
        QFile file(QString::fromLatin1(b.path));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = file.readAll();
        const int id = QFontDatabase::addApplicationFontFromData(data);
        if (id < 0)
            continue;
        const QStringList fams = QFontDatabase::applicationFontFamilies(id);
        if (fams.isEmpty())
            continue;
        const QString family = fams.first();
        l.families << family;
        l.bytesByFamily.insert(family, data);
        if (b.preferred && l.defaultFamily.isEmpty())
            l.defaultFamily = family;
    }
    if (l.defaultFamily.isEmpty() && !l.families.isEmpty())
        l.defaultFamily = l.families.first();
}

const QStringList &families() { return lib().families; }

QString defaultFamily() { return lib().defaultFamily; }

QByteArray fontData(const QString &family) { return lib().bytesByFamily.value(family); }

void registerFontData(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    QFontDatabase::addApplicationFontFromData(data);
}

} // namespace FontLibrary
