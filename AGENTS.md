# AGENTS.md

## 1. Project Overview

**Project Name:** Videx

**Product positioning:** Videx is a lightweight local media browser and viewer for video, audio, and images. Browsing courses is one possible use case, not the product identity. Product copy should describe general local media browsing, viewing, and playback.

Videx is a native, cross-platform desktop application built with **C++20** and **Qt 6 Widgets**. Image viewing uses Qt image APIs, LibRaw, and libheif; Exiv2 reads photographic metadata. The video/audio backend is **libmpv**.

**Implementation status:** The application supports directory browsing, persisted preferences, still-image viewing (including SVG, HEIC/AVIF and LibRaw-supported camera RAW), viewing controls, and image details with EXIF/IPTC/XMP and GPS when present. Basic video/audio playback uses libmpv with a Qt OpenGL widget for video, shared controls, scoped shortcuts, same-category navigation, and optional auto-next. Playback position restoration is deferred. Unsupported formats show a clear message in the right-hand viewer. See `docs/image-viewer.md` and `docs/playback.md` for scope and limitations; do not claim every codec, camera, compression variant, animation, or multi-page format is supported.

The application provides a VS Code-style desktop layout:

- File and directory explorer on the left
- Media viewing area on the right
- Media-specific controls within the right-hand area
- Optional status bar / toolbar / menu bar
- Support for Windows and Linux

The primary use case is browsing local directories containing videos, audio recordings, music, and images, then viewing or playing them directly inside the same application.

Videx is intended to remain lightweight and native. It must not become a browser-based desktop application.

---

## 2. Target Platforms

Videx must support:

- Windows 11 x64
- Ubuntu Desktop 24.04 x64

Platform-specific implementation is allowed when necessary, but the majority of application logic should remain cross-platform.

Do not introduce platform-specific code unless Qt or libmpv cannot provide a reasonable cross-platform implementation.

Use conditional compilation only when required:

```cpp
#ifdef Q_OS_WIN
// Windows-specific implementation
#elif defined(Q_OS_LINUX)
// Linux-specific implementation
#endif
```

Keep platform-specific code isolated from business logic.

---

## 3. Technology Stack

Required technologies:

- C++20
- Qt 6
- Qt Widgets
- CMake
- libmpv (for video/audio playback when integrated)
- QFileSystemModel
- QTreeView
- QSplitter

Preferred Qt modules:

```text
Qt6::Core
Qt6::Gui
Qt6::Widgets
```

Additional Qt modules may be introduced only when necessary.

### Prohibited technologies

Do not introduce:

- Electron
- Chromium
- WebView
- Qt WebEngine
- React
- Vue
- HTML-based UI
- JavaScript-based desktop frameworks

Do not replace Qt Widgets with Qt Quick/QML unless explicitly requested.

The UI must remain a native Qt Widgets application.

---

## 4. Build System

Use **CMake** as the only supported build system.

Do not add:

- qmake project files
- Makefiles maintained manually
- Visual Studio-specific project files as the canonical project definition

The project should be buildable using standard CMake workflows.

Example:

```bash
cmake -S . -B build
cmake --build build
```

On multi-configuration generators:

```bash
cmake --build build --config Release
```

Prefer modern target-based CMake.

Example:

```cmake
target_link_libraries(videx PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
```

Avoid global compiler options and global include directories when target-scoped equivalents are available.

---

## 5. Recommended Project Structure

Prefer the following structure:

```text
videx/
├── AGENTS.md
├── README.md
├── CMakeLists.txt
│
├── cmake/
│
├── src/
│   ├── main.cpp
│   │
│   ├── app/
│   │   ├── Application.h
│   │   └── Application.cpp
│   │
│   ├── ui/
│   │   ├── MainWindow.h
│   │   ├── MainWindow.cpp
│   │   │
│   │   ├── explorer/
│   │   │   ├── ExplorerWidget.h
│   │   │   └── ExplorerWidget.cpp
│   │   │
│   │   ├── player/
│   │   │   ├── PlayerWidget.h
│   │   │   ├── PlayerWidget.cpp
│   │   │   ├── PlayerControls.h
│   │   │   └── PlayerControls.cpp
│   │   │
│   │   └── common/
│   │
│   ├── player/
│   │   ├── MpvPlayer.h
│   │   └── MpvPlayer.cpp
│   │
│   ├── services/
│   │   ├── SettingsService.h
│   │   ├── SettingsService.cpp
│   │   ├── PlaybackHistory.h
│   │   └── PlaybackHistory.cpp
│   │
│   ├── models/
│   │
│   └── utils/
│
├── resources/
│   ├── icons/
│   └── styles/
│       └── dark.qss
│
├── tests/
│
└── docs/
```

The exact structure may evolve, but maintain clear separation between:

- UI
- media classification, video/audio playback, and image viewing
- filesystem logic
- persistent state
- reusable utilities

Do not place the entire application inside `MainWindow.cpp`.

The tree above is illustrative. Add a dedicated image-viewing component when implementing images; do not rename or move working modules merely to match this structure.

---

## 6. Architecture Principles

Videx should follow a simple layered architecture.

```text
UI Layer
   ↓
Application / Service Layer
   ↓
Media backends / Filesystem abstraction
   ↓
Qt / libmpv
```

The UI should not directly contain complicated decoding, playback, or persistence logic.

Keep reusable media classification and dispatch separate from widgets. Distinguish video, audio, image, and unknown/unsupported files without assuming every opened file has a timeline or video frames. Route video and audio through the same MpvPlayer abstraction; route images through a dedicated Qt image-viewing component. Avoid introducing a generic plugin framework prematurely.

When switching media, stop the previous playback session, release obsolete image data, and reset controls and metadata. Discard stale asynchronous results so a previously opened file cannot overwrite the current view or continue playing unexpectedly.

For example:

```text
PlayerControls
    ↓
MpvPlayer
    ↓
libmpv
```

Instead of:

```text
PlayerControls
    ↓
direct mpv C API calls everywhere
```

All libmpv interaction should preferably be wrapped inside a dedicated class such as:

```cpp
class MpvPlayer;
```

---

## 7. Main Window Layout

The main interface should resemble the basic structure of VS Code without attempting to clone VS Code completely.

Recommended layout:

```text
┌──────────────────────────────────────────────────────┐
│ File   View   Playback   Help                       │
├──────────────┬───────────────────────────────────────┤
│ EXPLORER     │                                       │
│              │                                       │
│ ▼ Media      │                                       │
│   ▼ Local    │              MEDIA                    │
│     01.mp4   │              VIEWER                   │
│     02.jpg   │                                       │
│   ▶ Other    │                                       │
│              │                                       │
├──────────────┴───────────────────────────────────────┤
│ ▶   ━━━━━━━●━━━━━━━━   12:32 / 45:18    1.5x   🔊 │
└──────────────────────────────────────────────────────┘
```

Use:

```cpp
QSplitter
```

to separate the explorer and media viewing area.

The diagram illustrates video/audio playback. Controls belong to the right-hand media area; the explorer occupies its own full-height column. Images use viewing controls, while file information and empty states hide playback controls.

Users must be able to resize the explorer panel.

Do not hardcode the explorer width.

---

## 8. File Explorer

Use:

```cpp
QFileSystemModel
QTreeView
```

as the basis for the filesystem explorer.

Do not implement an unnecessary recursive filesystem scanner when QFileSystemModel already provides the required behavior.

The explorer should eventually support:

- Opening a root directory
- Directory expansion and collapse
- Selecting files
- Double-clicking supported media files
- File icons
- Directory icons
- Current file highlighting
- Context menu
- Refresh
- Recent directories

Initial implementation should prioritize reliability over custom visual effects.

### Media recognition and optional filtering

Preserve the current default of showing all files and directories. Media recognition determines how a file opens; it must not silently hide non-media files. Any future media-only filter must be explicit and optional.

Candidate formats for implementation and verification include:

```text
Video: .mp4 .mkv .webm .mov .avi .m4v .ts .m2ts
Audio: .mp3 .flac .wav .ogg .opus .m4a .aac .wma
Image: .jpg .jpeg .png .bmp .webp .gif .tif .tiff
```

These are candidate extensions, not guaranteed support. Keep case-insensitive classification reusable and shared by opening, optional filters, and navigation. Extensions are hints; containers can contain audio, video, or both.

Actual video/audio capability is determined by mpv; image capability is determined by the available Qt image readers and deployed plugins. Resolve ambiguous types using backend results when opening the file, without probing every file during directory browsing. Unsupported files should retain useful file information and receive an actionable message when viewing fails.

---

## 9. Natural File Sorting

Files containing numbers should use natural sorting when possible.

Correct:

```text
1.mp4
2.mp4
3.mp4
10.mp4
11.mp4
```

Avoid:

```text
1.mp4
10.mp4
11.mp4
2.mp4
3.mp4
```

Apply the same ordering to video, audio, and image navigation, including numbered recordings and image sequences.

Keep natural sorting implementation reusable rather than embedding it directly inside UI event handlers.

---

## 10. Video/Audio Playback and Image Viewing

### Video and audio

Use **libmpv** as the shared video and audio playback engine. Audio-only files are first-class media and must not require a video stream to load successfully. Show a simple audio view with filename and available metadata; cover art is optional and must not be mistaken for a playable video stream.

Do not implement the primary player using `QMediaPlayer` unless explicitly requested.

Qt is responsible for:

- Window management
- UI
- Input
- File explorer
- Settings
- History
- Menus
- Shortcuts

mpv is responsible for:

- Video decoding
- Audio decoding
- Seeking
- Playback speed
- Subtitle handling
- Audio track handling
- Hardware acceleration
- Media format compatibility

Wrap libmpv behind a C++ abstraction.

Example:

```cpp
class MpvPlayer : public QObject
{
    Q_OBJECT

public:
    explicit MpvPlayer(QObject* parent = nullptr);

    bool initialize();
    void openFile(const QString& path);

    void play();
    void pause();
    void togglePause();

    void seek(double seconds);
    void setVolume(int volume);
    void setPlaybackRate(double rate);

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pausedChanged(bool paused);
    void mediaLoaded(const QString& path);
    void playbackFinished();
};
```

This is an architectural example, not a mandatory exact API.

### Images

Prefer `QImageReader` and `QImage` for common formats, LibRaw for camera RAW, and libheif for HEIC/AVIF, with native Qt Widgets for display. Use Exiv2 for photographic metadata. Keep decoder details in `src/media` and viewing controls in `src/ui/images`; third-party sources and licenses are documented in `docs/third-party.md`. Do not force still images through mpv or invent a duration for them.

The initial image viewer should support aspect-ratio-preserving fit-to-window, actual size, zoom, pan, and previous/next image navigation. Respect orientation metadata where supported. Rotation, animated images, multi-page images, and slideshows are separate enhancements; do not claim them without implementation and verification.

Handle corrupt files, unavailable image plugins, and excessive image dimensions gracefully. Bound decoded memory and caches, use scaled decoding where supported, and keep expensive decoding off the GUI thread. Create display resources and update widgets on the GUI thread.

---

## 11. Media Controls

Video and audio playback should include:

- Play / Pause
- Seek bar
- Current playback time
- Total duration
- Volume
- Mute
- Playback speed
- Fullscreen

Images use fit, actual size, zoom, and navigation controls instead of seek, volume, or playback-rate controls. Fullscreen applies to the viewing area. Only show playback controls for a loaded video/audio session, not merely for a selected filename or extension. Disable operations unsupported by the current media, such as seeking when no seekable timeline is available.

Useful playback rates:

```text
0.5x
0.75x
1.0x
1.25x
1.5x
1.75x
2.0x
```

Playback shortcuts should eventually include common conventions within the appropriate media context:

```text
Space       Play / Pause
Left        Seek backward
Right       Seek forward
Up          Volume up
Down        Volume down
F           Fullscreen
M           Mute
Ctrl+O      Open directory or file
```

Do not capture shortcuts globally unless required.

Preserve native file-tree and input-widget keyboard navigation. Playback shortcuts must not steal arrow keys or Space from focused controls. Image navigation and zoom shortcuts should be scoped to the image viewer and documented when implemented.

Explicit user-requested exception: while seekable video/audio is loaded and a file row is selected, the explorer's unmodified Left/Right keys control playback. A short press seeks 5 seconds; held Right temporarily plays at 2× and restores the previous rate on release; held Left steps backward 1 second. Folder rows keep native expand/collapse, and other focused input controls keep their own keys. Cancel held playback actions on focus loss or when seeking becomes disabled.

---

## 12. Playback History

Videx should support restoring video and audio playback position.

Persist at least:

```text
file path
last playback position
duration
playback rate
last played time
```

Example conceptual model:

```text
PlaybackHistoryEntry
├── path
├── position
├── duration
├── playbackRate
└── lastPlayedAt
```

For the first version, Qt settings or a lightweight local storage implementation is acceptable.

SQLite may be introduced later if the data model becomes more complex.

Avoid introducing a database prematurely.

Images may later remember the last viewed file or view state, but must not create fictitious playback positions or durations. Keep image view state distinct from timed-media history and preserve compatibility with existing settings when extending persistence.

---

## 13. Settings

Use Qt-supported platform-appropriate configuration mechanisms.

`QSettings` is preferred for basic application settings.

Possible settings:

```text
last opened directory
window geometry
splitter position
volume
playback speed
theme
auto-resume
auto-play-next
```

Restore window state when the application starts.

Do not store machine-specific absolute development paths in source files.

---

## 14. Media Navigation and Auto Play Next

When the current video or audio finishes naturally, Videx should eventually be able to play the next file of the same media category in the current directory. Do not auto-advance on load failure, manual stop, or media switching. Mixed-media queues require a separate explicit feature; do not unexpectedly switch between audio, video, and images.

Ordering should follow the explorer's natural sorting rules.

Example:

```text
01 Introduction.mp4
02 Installation.mp4
03 Configuration.mp4
```

After `01` finishes:

```text
02 Installation.mp4
```

may automatically start if the feature is enabled.

Keep this feature configurable.

Images use explicit previous/next navigation through images in the current directory. Timed slideshows are optional future functionality and must not reuse audio/video completion semantics. At the end of a sequence, stop unless repeat has been explicitly enabled.

---

## 15. Subtitle Support

Subtitle support should primarily rely on mpv.

Eventually support:

```text
.srt
.ass
.ssa
.vtt
```

Prefer mpv's native subtitle loading and discovery mechanisms instead of implementing subtitle rendering manually.

Do not render ASS subtitles with custom Qt painting unless there is a strong technical reason.

---

## 16. Audio and Subtitle Tracks

Future versions should expose mpv's available:

- audio tracks
- subtitle tracks

through the application UI.

Do not hardcode a single audio or subtitle stream.

The architecture should avoid assumptions that make multiple tracks difficult to support later.

---

## 17. Theme and Styling

The default visual style should be a clean dark desktop interface inspired by VS Code.

Do not attempt a pixel-perfect VS Code clone.

Prefer restrained desktop styling.

QSS may be used for styling:

```text
resources/styles/dark.qss
```

Avoid large amounts of inline stylesheet code such as:

```cpp
widget->setStyleSheet("...");
```

for every individual widget.

Centralize reusable styling.

Colors, spacing and dimensions should preferably be configurable or defined in one place.

---

## 18. UI Guidelines

Prefer:

- compact controls
- consistent spacing
- simple icons
- native desktop interaction
- clear hover and selected states
- keyboard accessibility

Avoid:

- oversized mobile-style buttons
- excessive animations
- glassmorphism
- browser-like cards everywhere
- unnecessary gradients
- large empty margins
- UI patterns designed primarily for touchscreens

Videx is a desktop productivity/media tool.

---

## 19. Resource Management

Use RAII.

Do not manually manage ownership when Qt parent-child ownership or standard smart pointers can safely handle it.

Prefer:

```cpp
std::unique_ptr
```

when ownership is exclusive and Qt parenting is not involved.

Be careful when combining:

- QObject ownership
- smart pointers
- native mpv handles

Avoid double deletion.

All libmpv resources must be released correctly during application shutdown.

---

## 20. Qt Coding Guidelines

Use Qt idioms where appropriate.

Prefer:

```cpp
QString
QFileInfo
QDir
QUrl
QVariant
```

inside the Qt-facing parts of the application.

Use STL facilities where they improve clarity or belong to lower-level independent logic.

Do not mechanically convert everything between Qt and STL types without need.

Use signals and slots for UI communication.

Prefer modern function-pointer connections:

```cpp
connect(button, &QPushButton::clicked,
        this, &PlayerWidget::onPlayClicked);
```

Avoid old string-based syntax unless interacting with legacy APIs.

---

## 21. C++ Style

Use C++20.

Prefer:

- RAII
- `nullptr`
- `override`
- `const`
- scoped enums
- range-based loops
- smart pointers where appropriate
- strongly typed interfaces

Avoid:

- raw owning pointers
- global mutable state
- unnecessary macros
- C-style casts
- manual memory management where avoidable

Example:

```cpp
enum class PlaybackState
{
    Stopped,
    Playing,
    Paused
};
```

Prefer descriptive names over excessive abbreviations.

---

## 22. Naming Convention

Recommended convention:

### Classes

```cpp
MainWindow
ExplorerWidget
PlayerWidget
MpvPlayer
PlaybackHistory
SettingsService
```

### Methods

```cpp
openFile()
setPlaybackRate()
restorePlaybackPosition()
loadDirectory()
```

### Local variables

```cpp
currentFile
playbackPosition
rootDirectory
```

### Member variables

Use one consistent convention.

Preferred:

```cpp
m_player
m_fileModel
m_currentFile
```

Do not mix multiple member naming styles.

---

## 23. Header Discipline

Keep headers lightweight.

Prefer forward declarations when possible.

Avoid including large or unnecessary headers from `.h` files.

Implementation-specific dependencies should normally remain in `.cpp` files.

This is especially important for:

- mpv headers
- operating-system headers
- heavy Qt modules

---

## 24. Logging

Use Qt logging facilities.

For example:

```cpp
qDebug()
qInfo()
qWarning()
qCritical()
```

Do not scatter `printf` or `std::cout` through the application.

Important failures should include enough context to diagnose the problem.

Example:

```cpp
qWarning() << "Failed to open media file:" << filePath;
```

Do not log excessive messages on every playback timer update.

---

## 25. Error Handling

Failures should not normally crash the application.

Handle errors such as:

- invalid file
- unsupported media
- deleted file
- inaccessible directory
- mpv initialization failure
- missing library
- failed playback
- image decoding failure or excessive image dimensions
- missing image format plugin
- invalid configuration

Show actionable errors to the user when appropriate.

Technical diagnostics may also be logged.

Do not silently ignore important errors.

---

## 26. Performance

Videx should remain lightweight.

Avoid:

- scanning entire disks recursively
- loading file contents unnecessarily
- generating all thumbnails at startup
- polling the filesystem continuously
- frequent synchronous disk operations on the UI thread

Use lazy loading where possible.

QFileSystemModel already handles filesystem population asynchronously and should be leveraged.

Any expensive future operations such as:

- thumbnail generation
- metadata extraction
- media probing
- large image decoding and scaling

must not block the GUI thread.

---

## 27. Threading

The Qt GUI must only be modified from the GUI thread.

Background workers may be introduced for expensive operations.

Prefer Qt's threading abstractions when interacting heavily with Qt:

```text
QThread
QThreadPool
QtConcurrent
```

Do not introduce threads for trivial tasks.

Do not create complicated concurrency architecture prematurely.

---

## 28. Dependency Policy

Keep external dependencies minimal.

Current image dependencies and planned playback dependency:

```text
Qt 6
Qt Image Formats plugins
LibRaw
Exiv2
libheif (libde265 / libaom)
zlib
libmpv
```

Do not add a library merely to implement functionality already provided cleanly by Qt or the C++ standard library.

Before adding a new dependency, consider:

1. Is it really necessary?
2. Does Qt already provide this?
3. Is it actively maintained?
4. Does it support Windows and Linux?
5. Does it complicate packaging?
6. Does its license fit the project?

---

## 29. Windows Packaging

The Windows build should eventually be distributable without requiring a development environment.

Expected packaging components may include:

```text
videx.exe
Qt runtime DLLs
Qt platform plugin
libmpv.dll
mpv/FFmpeg related runtime dependencies
application resources
Qt image format plugins required by supported image formats
```

Use Qt deployment tools where appropriate, such as:

```text
windeployqt
```

Do not depend on Qt Creator being installed on the user's machine.

---

## 30. Linux Packaging

Primary Linux target:

```text
Ubuntu Desktop 24.04 x64
```

Initially, running against system-installed Qt/mpv libraries during development is acceptable.

Future distribution may use:

- AppImage
- `.deb`
- portable directory bundle

Packaging architecture should not affect core application design.

On both platforms, verify advertised image formats against the packaged Qt image plugins and video/audio formats against the distributed or system mpv backend. Development-machine support alone does not establish packaged support.

---

## 31. Development Milestones

These describe incremental work areas, not completion status. Preserve existing browsing behavior while adding each media backend. Basic image viewing and audio playback are core scope, not advanced extras that must wait for every video enhancement.

### Phase 1 — Application Skeleton

Implement:

- Qt Widgets application
- MainWindow
- menu bar
- QSplitter layout
- explorer area
- media viewer placeholder
- status bar
- dark theme

No media decoding is required until the basic UI structure is stable.

### Phase 2 — File Explorer

Implement:

- QFileSystemModel
- QTreeView
- open directory
- remember last directory
- double-click file handling
- reusable video/audio/image classification with an unknown-file fallback

### Phase 3 — Basic Media Backends

Implement:

- libmpv initialization
- embedded video output
- open video and audio, including audio-only files
- play
- pause
- seek
- volume
- playback position
- duration
- Qt-based still-image decoding and display
- safe switching between file information, images, video, and audio

### Phase 4 — Media Controls

Implement:

- seek slider
- time display
- playback speed
- mute
- fullscreen
- keyboard shortcuts
- image fit, actual size, zoom, and pan
- controls and shortcuts scoped to the active media type

### Phase 5 — Usability

Implement:

- resume video/audio playback
- recent directories
- previous/next media within the current category
- configurable auto-play-next for video/audio
- natural sorting
- window state persistence

### Phase 6 — Advanced Media Features

Potential additions:

- subtitle selection
- audio track selection
- external subtitle loading
- playlist
- search
- favorites
- richer media metadata and audio cover art
- thumbnails
- animated/multi-page images and slideshows

Do not prematurely implement Phase 6 while basic playback, image viewing, or media switching remains unstable.

---

## 32. Scope Control

Avoid feature creep.

The primary product concept is:

> Browse local files on the left; view images and play video/audio on the right.

Do not turn Videx into:

- a full file manager
- a video editor
- an image editor or digital audio workstation
- an IDE
- a media server
- a streaming platform
- a browser
- a full VLC replacement

Features should directly improve browsing and consuming local media.

---

## 33. AI Agent Instructions

When modifying this repository:

1. Inspect existing code before editing.
2. Preserve the existing architecture unless there is a clear reason to change it.
3. Avoid large rewrites when a small change is sufficient.
4. Keep platform compatibility in mind.
5. Do not introduce Electron, WebView, Qt Quick or QML.
6. Do not move unrelated code during a focused task.
7. Do not add dependencies without justification.
8. Build the project after meaningful changes when the environment allows it.
9. Resolve compiler warnings introduced by your changes.
10. Do not claim a feature works unless it has been compiled or reasonably verified.
11. If a dependency or environment prevents verification, state that clearly.
12. Prefer maintainable code over compressed or clever code.

---

## 34. Refactoring Rules

Refactoring is allowed when it improves:

- maintainability
- separation of concerns
- testability
- portability
- readability

However:

- do not mix broad refactoring with unrelated feature implementation
- do not rename large parts of the project without need
- do not rewrite functioning modules solely due to style preference

When a class becomes too large, split responsibilities instead of continuously expanding it.

`MainWindow` should coordinate the UI, not implement the entire application.

---

## 35. Testing

Add tests for logic that can reasonably be tested without a GUI.

Good candidates:

- natural filename sorting
- playback history logic
- settings serialization
- media extension filtering
- media classification and backend dispatch
- media switching and stale-result handling
- image sizing, orientation, and decode-failure handling
- playlist ordering
- time formatting

Avoid creating fragile UI tests for trivial widget behavior unless there is a clear benefit.

---

## 36. Definition of Done

A task should normally be considered complete only when:

- implementation is finished
- code compiles
- no new obvious compiler warnings are introduced
- relevant behavior has been verified
- Windows/Linux portability has been considered
- error handling is reasonable
- unrelated code has not been unnecessarily modified

If the environment does not allow full verification, document what remains unverified.

---

## 37. Current Product Direction

The current priority is a lightweight local media browser for video, audio, and images with a VS Code-inspired workflow.

Core direction:

```text
Qt 6 Widgets
+
C++20
+
CMake
+
QFileSystemModel
+
QTreeView
+
libmpv (video/audio) + Qt image APIs (images)
```

Target experience:

```text
Open directory
     ↓
Browse files in explorer
     ↓
Open a video, audio file, or image
     ↓
Play or view inside the application
     ↓
Remember playback progress for video/audio
     ↓
Navigate to the next file in the current media category
```

Keep this workflow simple, fast, and reliable.
