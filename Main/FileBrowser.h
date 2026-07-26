#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <Arduino.h>

// =====================================================================
// Ghoul OS - FileBrowser.h
//
// Thin wrapper around the SD library that the Files app's UI drives.
// Keeps all SD-card/file-system concerns (init, directory listing,
// path traversal, reading a file's bytes) separate from drawing code.
// =====================================================================

#define FB_MAX_ENTRIES      40   // entries listed per directory
#define FB_MAX_NAME_LEN     32
#define FB_MAX_PATH_LEN     128
#define FB_PREVIEW_BUF_SIZE 512  // bytes read when previewing a file

struct FileEntry
{
    char name[FB_MAX_NAME_LEN];
    bool isDir;
    uint32_t size;
};

extern bool sdReady;
extern char currentPath[FB_MAX_PATH_LEN];
extern FileEntry fileEntries[FB_MAX_ENTRIES];
extern uint8_t fileEntryCount;
extern bool hasParentEntry; // true whenever currentPath isn't the root

// Initializes the SD card on SD_CS_PIN and lists the root directory.
// Returns false if no card could be mounted -- the Files app then shows
// a friendly "SD card not found" message instead of crashing.
bool filesInit();

// (Re)loads fileEntries[] / fileEntryCount / hasParentEntry from
// whatever directory currentPath currently points at.
void filesListCurrentDirectory();

// Enters the sub-directory at fileEntries[index] (must be a directory).
// Updates currentPath and reloads the listing. Returns false if index
// is out of range or the entry isn't a directory.
bool filesEnterDirectory(uint8_t index);

// Moves currentPath up one level and reloads the listing.
// Returns false if already at the root ("/").
bool filesGoUp();

// Reads up to bufferSize-1 bytes of fileEntries[index] into buffer and
// null-terminates it. Returns false if index is out of range, the
// entry is a directory, or the file couldn't be opened.
bool filesReadPreview(uint8_t index, char* buffer, size_t bufferSize, size_t* bytesRead);

#endif // FILE_BROWSER_H