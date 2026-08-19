#include <SD.h>
#include <SPI.h>
#include <string.h>
#include "FileBrowser.h"
#include "Config.h"

// =====================================================================
// Ghoul OS - FileBrowser.cpp
// =====================================================================

bool sdReady = false;
char currentPath[FB_MAX_PATH_LEN] = "/";
FileEntry fileEntries[FB_MAX_ENTRIES];
uint8_t fileEntryCount = 0;
bool hasParentEntry = false;

// Builds the absolute path of fileEntries[index] into outPath, taking
// care not to double up the '/' when currentPath is the root.
static void buildEntryPath(uint8_t index, char* outPath, size_t outSize)
{
    size_t len = strlen(currentPath);
    bool hasTrailingSlash = (len > 0) && (currentPath[len - 1] == '/');

    if (hasTrailingSlash)
    {
        snprintf(outPath, outSize, "%s%s", currentPath, fileEntries[index].name);
    }
    else
    {
        snprintf(outPath, outSize, "%s/%s", currentPath, fileEntries[index].name);
    }
}

bool filesInit()
{
    sdReady = SD.begin(SD_CS_PIN);

    strncpy(currentPath, "/", sizeof(currentPath) - 1);
    currentPath[sizeof(currentPath) - 1] = '\0';

    if (sdReady)
    {
        filesListCurrentDirectory();
    }
    else
    {
        fileEntryCount = 0;
        hasParentEntry = false;
    }

    return sdReady;
}

void filesListCurrentDirectory()
{
    fileEntryCount = 0;
    hasParentEntry = (strcmp(currentPath, "/") != 0);

    if (!sdReady)
    {
        return;
    }

    File dir = SD.open(currentPath);
    if (!dir || !dir.isDirectory())
    {
        if (dir) dir.close();
        return;
    }

    File entry = dir.openNextFile();
    while (entry && fileEntryCount < FB_MAX_ENTRIES)
    {
        // Some cores return the entry's full path from name(); keep only
        // the last path segment so the UI just shows the plain filename.
        const char* rawName = entry.name();
        const char* lastSlash = strrchr(rawName, '/');
        const char* shortName = lastSlash ? lastSlash + 1 : rawName;

        strncpy(fileEntries[fileEntryCount].name, shortName, FB_MAX_NAME_LEN - 1);
        fileEntries[fileEntryCount].name[FB_MAX_NAME_LEN - 1] = '\0';
        fileEntries[fileEntryCount].isDir = entry.isDirectory();
        fileEntries[fileEntryCount].size = entry.size();

        fileEntryCount++;
        entry.close();
        entry = dir.openNextFile();
    }

    if (entry) entry.close();
    dir.close();
}

bool filesEnterDirectory(uint8_t index)
{
    if (index >= fileEntryCount || !fileEntries[index].isDir)
    {
        return false;
    }

    char newPath[FB_MAX_PATH_LEN];
    buildEntryPath(index, newPath, sizeof(newPath));

    strncpy(currentPath, newPath, sizeof(currentPath) - 1);
    currentPath[sizeof(currentPath) - 1] = '\0';

    filesListCurrentDirectory();
    return true;
}

bool filesGoUp()
{
    if (strcmp(currentPath, "/") == 0)
    {
        return false;
    }

    // Strip a possible trailing slash, then strip the last path segment.
    size_t len = strlen(currentPath);
    if (len > 1 && currentPath[len - 1] == '/')
    {
        currentPath[len - 1] = '\0';
        len--;
    }

    char* lastSlash = strrchr(currentPath, '/');
    if (lastSlash == currentPath)
    {
        // The parent of a top-level folder is the root.
        currentPath[1] = '\0';
    }
    else if (lastSlash != nullptr)
    {
        *lastSlash = '\0';
    }
    else
    {
        strncpy(currentPath, "/", sizeof(currentPath) - 1);
        currentPath[sizeof(currentPath) - 1] = '\0';
    }

    filesListCurrentDirectory();
    return true;
}

bool filesReadPreview(uint8_t index, char* buffer, size_t bufferSize, size_t* bytesRead)
{
    if (index >= fileEntryCount || fileEntries[index].isDir || bufferSize == 0)
    {
        if (bytesRead) *bytesRead = 0;
        return false;
    }

    char path[FB_MAX_PATH_LEN];
    buildEntryPath(index, path, sizeof(path));

    File f = SD.open(path);
    if (!f)
    {
        if (bytesRead) *bytesRead = 0;
        return false;
    }

    // Read in small chunks rather than one single big read() call. A
    // single large read is far more likely to hit a transient SPI/SD
    // hiccup on real hardware (breadboard wiring, a slower card, etc.)
    // than several small ones -- and previously, if that one call
    // stumbled at all, the WHOLE preview was thrown away, which is what
    // showed up as "could not read file" specifically on bigger files.
    // Reading in chunks means a hiccup partway through just stops us
    // where we are, and whatever was read successfully before that is
    // still shown instead of being discarded.
    const size_t CHUNK = 256;
    size_t total = 0;
    size_t toRead = bufferSize - 1; // leave room for the NUL terminator

    while (total < toRead)
    {
        size_t want = toRead - total;
        if (want > CHUNK) want = CHUNK;

        size_t n = f.read((uint8_t*)buffer + total, want);

        // Defensive: a well-behaved read() never returns more bytes than
        // requested. Some SD/Stream implementations report a failed read
        // by returning a negative int internally, which -- because the
        // public API's return type is size_t -- would silently become a
        // huge unsigned value (SIZE_MAX) here instead of a small error
        // code. Treat any out-of-range count as "this chunk failed"
        // rather than ever trusting it.
        if (n > want) n = 0;

        if (n == 0)
        {
            // One retry covers a single transient hiccup. If the retry
            // also comes back empty, stop here and keep whatever total
            // bytes we already have -- a partial preview beats none.
            n = f.read((uint8_t*)buffer + total, want);
            if (n > want) n = 0;
            if (n == 0) break;
        }

        total += n;

        if (n < want) break; // short read -- end of file
    }

    f.close();

    buffer[total] = '\0';

    if (bytesRead) *bytesRead = total;

    // Only report outright failure when we truly got nothing back from
    // a non-empty file. Any partial content read successfully is still
    // worth showing.
    return (total > 0) || (fileEntries[index].size == 0);
}
