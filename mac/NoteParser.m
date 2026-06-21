#import "NoteParser.h"

static uint32_t ReadU32BE(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

@implementation NoteParser

+ (NSData *)previewPNGFromURL:(NSURL *)url
{
    NSData *file = [NSData dataWithContentsOfURL:url];
    if (file == nil)
        return nil;

    const uint8_t *p = (const uint8_t *)file.bytes;
    const NSUInteger n = file.length;
    NSUInteger off = 0;

    // QByteArray magic: uint32 length + bytes, must be "PPNOTE".
    if (off + 4 > n)
        return nil;
    uint32_t magicLen = ReadU32BE(p + off);
    off += 4;
    if (magicLen != 6 || off + magicLen > n || memcmp(p + off, "PPNOTE", 6) != 0)
        return nil;
    off += magicLen;

    // quint32 version.
    if (off + 4 > n)
        return nil;
    uint32_t version = ReadU32BE(p + off);
    off += 4;
    if (version < 3)
        return nil;   // no embedded preview before v3

    // QByteArray preview PNG.
    if (off + 4 > n)
        return nil;
    uint32_t previewLen = ReadU32BE(p + off);
    off += 4;
    if (previewLen == 0 || previewLen == 0xFFFFFFFFu || off + previewLen > n)
        return nil;

    return [file subdataWithRange:NSMakeRange(off, previewLen)];
}

@end
