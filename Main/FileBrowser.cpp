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

    size_t toRead = bufferSize - 1;
    size_t n = f.read((uint8_t*)buffer, toRead);
    buffer[n] = '\0';
    f.close();

    if (bytesRead) *bytesRead = n;
    return true;
}