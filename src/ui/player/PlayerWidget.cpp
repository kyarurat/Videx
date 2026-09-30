#include "PlayerWidget.h"
#include "PlayerControls.h"
#include "ui/images/ImagePane.h"
#include "ui/common/UiComponents.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QPushButton>
#include <QDir>
#include <QLocale>
#include <QKeyEvent>
#include "media/MpvPlayer.h"
#include "media/MediaType.h"
#include "VideoView.h"
#include <QGuiApplication>
#include <QTimer>

PlayerWidget::PlayerWidget(ThemeManager* theme, QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    auto* header = new QWidget(this);
    header->setObjectName("mediaHeader");
    header->setAttribute(Qt::WA_StyledBackground);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18,10,16,10);
    header->setMinimumHeight(54);
    m_showExplorer = new QPushButton(tr("展开文件树"), header);
    m_showExplorer->setObjectName("showExplorerButton");
    m_showExplorer->setToolTip(tr("恢复左侧文件树 (Ctrl+B)"));
    m_showExplorer->hide();
    connect(m_showExplorer, &QPushButton::clicked, this, &PlayerWidget::showExplorerRequested);
    headerLayout->addWidget(m_showExplorer);
    m_title = new QLabel(tr("媒体查看器"), header);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(m_title,1);
    layout->addWidget(header);
    m_notice = new QLabel(this);
    m_notice->setObjectName("browserNotice");
    m_notice->setTextFormat(Qt::PlainText);
    m_notice->setWordWrap(true);
    m_notice->setProperty("role", "error");
    m_notice->setContentsMargins(20,12,20,12);
    m_notice->hide();
    layout->addWidget(m_notice);
    m_stack = new QStackedWidget(this);
    m_empty = new EmptyState({}, {}, this);
    auto* open = m_empty->addAction(tr("打开文件或文件夹"), true);
    open->setObjectName("welcomeOpenDirectory");
    connect(open, &QPushButton::clicked, this, &PlayerWidget::openRequested);
    m_stack->addWidget(m_empty);

    auto* details = new QWidget(this);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(32,24,32,24);
    detailsLayout->setSpacing(18);
    detailsLayout->addStretch();
    m_name = new QLabel(details);
    m_name->setObjectName("selectedFileName");
    m_name->setProperty("role", "muted");
    detailsLayout->addWidget(m_name);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(16);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_path = new QLabel(details);
    m_path->setObjectName("selectedFilePath");
    m_size = new QLabel(details);
    m_type = new QLabel(details);
    m_modified = new QLabel(details);
    for (auto* label : {m_name, m_path, m_size, m_type, m_modified}) {
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    form->addRow(tr("位置"), m_path);
    form->addRow(tr("大小"), m_size);
    form->addRow(tr("扩展名"), m_type);
    form->addRow(tr("修改时间"), m_modified);
    detailsLayout->addLayout(form);
    auto* hint = new QLabel(tr("格式不支持"), details);
    m_formatHint = hint;
    hint->setObjectName("unsupportedFormatMessage");
    hint->setProperty("role", "heading");
    hint->setWordWrap(true);
    detailsLayout->insertWidget(1, hint);
    detailsLayout->addStretch();
    m_stack->addWidget(details);
    m_imagePane = new ImagePane(this);
    m_stack->addWidget(m_imagePane);
    m_playback = new MpvPlayer(this);
    m_seekHoldTimer = new QTimer(this);
    m_seekHoldTimer->setSingleShot(true);
    m_seekHoldTimer->setInterval(300);
    connect(m_seekHoldTimer, &QTimer::timeout, this, [this] {
        const auto& state = m_playback->state();
        if (!m_seekKeyDirection || !state.loaded || !state.seekable) return;
        m_seekHoldActive = true;
        if (m_seekKeyDirection > 0) {
            m_rateBeforeHold = state.rate;
            m_playback->setRate(2.0);
        } else {
            seekFromKeyboard(-1, true);
        }
    });
    m_playbackPane = new QWidget(this);
    m_playbackPane->setObjectName("playbackPane");
    m_playbackPane->setFocusPolicy(Qt::StrongFocus);
    m_playbackPane->installEventFilter(this);
    auto* playbackLayout = new QVBoxLayout(m_playbackPane);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    auto* navigation = new QHBoxLayout;
    navigation->setContentsMargins(12, 6, 12, 6);
    for (const auto& item : {std::pair{tr("上一项"), -1}, std::pair{tr("下一项"), 1}}) {
        auto* button = new QPushButton(item.first, m_playbackPane);
        navigation->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, direction = item.second] { emit mediaNavigationRequested(direction); });
    }
    navigation->addStretch();
    auto* shortcuts = new QLabel(tr("Space 暂停 · ← / → 跳转 · ↑ / ↓ 音量"), m_playbackPane);
    shortcuts->setProperty("role", "muted");
    navigation->addWidget(shortcuts);
    playbackLayout->addLayout(navigation);
    m_videoSurface = QGuiApplication::platformName() == QStringLiteral("offscreen")
        ? new QWidget(m_playbackPane) : new VideoView(m_playback, m_playbackPane);
    m_videoSurface->setObjectName("videoSurface");
    m_videoSurface->setFocusPolicy(Qt::StrongFocus);
    m_videoSurface->installEventFilter(this);
    m_videoSurface->hide();
    playbackLayout->addWidget(m_videoSurface, 1);
    m_audioPane = new QWidget(m_playbackPane);
    auto* audioLayout = new QVBoxLayout(m_audioPane);
    audioLayout->setContentsMargins(24, 24, 24, 24);
    audioLayout->setSpacing(10);
    audioLayout->addStretch();
    m_audioTitle = new QLabel(m_audioPane);
    m_audioTitle->setObjectName("audioTitle");
    m_audioTitle->setProperty("role", "heading");
    m_audioTitle->setTextFormat(Qt::PlainText);
    m_audioTitle->setWordWrap(true);
    m_audioTitle->setAlignment(Qt::AlignCenter);
    audioLayout->addWidget(m_audioTitle);
    m_audioInfo = new QLabel(tr("音频播放"), m_audioPane);
    m_audioInfo->setProperty("role", "muted");
    m_audioInfo->setTextFormat(Qt::PlainText);
    m_audioInfo->setWordWrap(true);
    m_audioInfo->setAlignment(Qt::AlignCenter);
    audioLayout->addWidget(m_audioInfo);
    audioLayout->addStretch();
    for (auto* label : {m_audioTitle, m_audioInfo}) {
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    playbackLayout->addWidget(m_audioPane, 1);
    m_stack->addWidget(m_playbackPane);
    connect(m_imagePane, &ImagePane::failed, this, [this](const QString& message) {
        m_formatHint->setText(message);
        m_stack->setCurrentIndex(1);
    });
    connect(m_imagePane, &ImagePane::navigationRequested, this, &PlayerWidget::imageNavigationRequested);
    layout->addWidget(m_stack,1);
    m_controls = new PlayerControls(theme, this);
    layout->addWidget(m_controls);
    connect(m_controls, &PlayerControls::fullscreenRequested, this, &PlayerWidget::fullscreenRequested);
    connect(m_controls, &PlayerControls::playRequested, m_playback, &MpvPlayer::togglePause);
    connect(m_controls, &PlayerControls::muteRequested, m_playback, &MpvPlayer::toggleMute);
    connect(m_controls, &PlayerControls::seekRequested, this, [this](int seconds) { m_playback->seek(seconds); });
    connect(m_controls, &PlayerControls::volumeRequested, m_playback, &MpvPlayer::setVolume);
    connect(m_controls, &PlayerControls::rateRequested, m_playback, &MpvPlayer::setRate);
    connect(m_playback, &MpvPlayer::stateChanged, this, &PlayerWidget::syncPlayback);
    connect(m_playback, &MpvPlayer::failed, this, [this](const QString& message) {
        m_formatHint->setText(message);
        m_stack->setCurrentIndex(1);
        setPlaybackMedia(PlaybackMedia::None);
    });
    connect(m_playback, &MpvPlayer::operationFailed, this, &PlayerWidget::setNotice);
    connect(m_playback, &MpvPlayer::finished, this, [this] {
        if (m_autoNext) emit mediaNavigationRequested(1);
    });
    setDirectory({});
}

PlayerWidget::~PlayerWidget()
{
    cancelSeekHold();
    // QWidget deletes children after this subclass's lifetime ends. The
    // backend is cleaned up later, so disconnect its UI callbacks now and stop
    // playback while the complete viewing interface is still alive.
    disconnect(m_playback, nullptr, this, nullptr);
    m_playback->stop();
}

void PlayerWidget::setDirectory(const QString& path)
{
    m_directory = path;
    setNotice({});
    setFile({});
}

void PlayerWidget::setFile(const FileDetails& details)
{
    cancelSeekHold();
    const auto type = classifyMedia(details.path);
    if (type != MediaType::Video && type != MediaType::Audio) m_playback->stop();
    m_currentFile = details;
    m_resolvedCategory = MediaType::Unknown;
    m_audioInfoPath.clear();
    setPlaybackMedia(PlaybackMedia::None);
    setNotice({});
    const bool selected = !details.path.isEmpty();
    m_title->setText(selected ? details.name : tr("媒体查看器"));
    m_title->setToolTip(selected ? details.name : QString{});
    if (!selected) {
        m_stack->setCurrentIndex(0);
        m_imagePane->clear();
        m_name->clear();
        m_path->clear();
        m_empty->setText(m_directory.isEmpty() ? tr("本地媒体，一处浏览") : tr("双击打开文件"),
            m_directory.isEmpty() ? tr("选择本地文件直接查看，或选择文件夹浏览其中的内容。")
                                  : tr("双击左侧文件树中的文件，在这里查看名称、位置和基本信息。"));
        return;
    }
    refreshFileDetails(details);
    if (type == MediaType::Video || type == MediaType::Audio) {
        m_imagePane->clear();
        m_videoSurface->setVisible(type == MediaType::Video);
        m_stack->setCurrentWidget(m_playbackPane);
        m_audioTitle->setText(tr("正在加载：%1").arg(details.name));
        m_audioTitle->show();
        m_audioPane->show();
        m_audioInfo->setText(tr("正在准备播放…"));
        m_audioInfo->show();
        m_playback->open(details.path, m_defaultRate, m_volume,
            QGuiApplication::platformName() == QStringLiteral("offscreen")
                ? MpvPlayer::RenderMode::Headless : type == MediaType::Audio
                    ? MpvPlayer::RenderMode::Audio : MpvPlayer::RenderMode::OpenGL);
        return;
    }
    m_stack->setCurrentWidget(m_imagePane);
    // open() resets the view and discards stale requests while preserving
    // decoded neighbors and any in-flight prefetch of this file.
    m_imagePane->open(details.path);
}

void PlayerWidget::refreshFileDetails(const FileDetails& details)
{
    const bool changed = details.path == m_currentFile.path
        && (details.size != m_currentFile.size || details.modified != m_currentFile.modified);
    m_currentFile = details;
    m_name->setText(details.name);
    m_path->setText(QDir::toNativeSeparators(details.path));
    m_size->setText(tr("%1（%2 字节）").arg(QLocale().formattedDataSize(details.size), QLocale().toString(details.size)));
    m_type->setText(details.suffix.isEmpty() ? tr("无扩展名") : details.suffix.toUpper());
    m_modified->setText(QLocale().toString(details.modified, QLocale::ShortFormat));
    const auto type = classifyMedia(details.path);
    if (changed && !details.path.isEmpty() && type != MediaType::Video && type != MediaType::Audio) {
        m_stack->setCurrentWidget(m_imagePane);
        m_imagePane->open(details.path, true);
    }
}

void PlayerWidget::applyPlaybackPreferences(double rate, bool autoNext)
{
    const bool rateChanged = m_defaultRate != rate;
    m_defaultRate = rate;
    m_autoNext = autoNext;
    if (rateChanged && m_playback->state().loaded) m_playback->setRate(rate);
}
void PlayerWidget::togglePause() { m_playback->togglePause(); }
void PlayerWidget::toggleMute() { m_playback->toggleMute(); }
void PlayerWidget::seekFromKeyboard(int direction, bool autoRepeat)
{
    if (direction == 0) return;
    const double seconds = autoRepeat ? 1.0 : 5.0;
    m_playback->seek(direction > 0 ? seconds : -seconds, true);
}
void PlayerWidget::handleSeekKey(int direction, bool pressed, bool autoRepeat)
{
    if (direction == 0) return;
    direction = direction > 0 ? 1 : -1;
    if (autoRepeat) {
        if (direction < 0 && pressed) {
            if (m_seekKeyDirection == direction) {
                m_seekHoldTimer->stop();
                m_seekHoldActive = true;
            }
            seekFromKeyboard(direction, true);
        }
        return;
    }
    if (pressed) {
        if (m_seekKeyDirection != direction && m_playback->state().loaded && m_playback->state().seekable) {
            cancelSeekHold();
            m_seekKeyDirection = direction;
            m_seekHoldTimer->start();
        }
    } else if (m_seekKeyDirection == direction) {
        const bool shortPress = !m_seekHoldActive;
        cancelSeekHold();
        if (shortPress) seekFromKeyboard(direction, false);
    }
}
void PlayerWidget::cancelSeekHold()
{
    m_seekHoldTimer->stop();
    if (m_seekHoldActive && m_seekKeyDirection > 0 && m_playback->state().loaded) m_playback->setRate(m_rateBeforeHold);
    m_seekKeyDirection = 0;
    m_seekHoldActive = false;
}
MediaType PlayerWidget::playbackCategory() const
{
    const auto& state = m_playback->state();
    return state.loaded ? (state.video ? MediaType::Video : MediaType::Audio) : MediaType::Unknown;
}
void PlayerWidget::syncPlayback()
{
    const auto& state = m_playback->state();
    if (!state.loaded || !state.seekable || state.ended) cancelSeekHold();
    m_controls->updateState(state);
    emit playbackSeekingEnabledChanged(state.loaded && state.seekable);
    setPlaybackMedia(state.loaded ? (state.video ? PlaybackMedia::Video : PlaybackMedia::Audio) : PlaybackMedia::None);
    if (!state.loaded) return;
    const auto category = state.video ? MediaType::Video : MediaType::Audio;
    if (m_resolvedCategory != category) {
        m_resolvedCategory = category;
        emit playbackResolved(m_currentFile.path, category);
    }
    m_volume = state.volume;
    m_videoSurface->setVisible(state.video);
    m_audioPane->setVisible(!state.video);
    m_audioTitle->setVisible(!state.video);
    m_audioInfo->setVisible(!state.video);
    if (state.video) return;
    m_audioTitle->setText(state.title.isEmpty() ? m_currentFile.name : state.title);
    if (m_audioInfoPath == m_currentFile.path && m_audioArtist == state.artist && m_audioAlbum == state.album) return;
    m_audioInfoPath = m_currentFile.path;
    m_audioArtist = state.artist;
    m_audioAlbum = state.album;
    QStringList information;
    if (!state.artist.isEmpty()) information.append(state.artist);
    if (!state.album.isEmpty()) information.append(state.album);
    information.append(tr("音频 · %1").arg(m_currentFile.suffix.toUpper()));
    information.append(QDir::toNativeSeparators(m_currentFile.path));
    m_audioInfo->setText(information.join(QLatin1Char('\n')));
}
bool PlayerWidget::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_videoSurface || watched == m_playbackPane) && event->type() == QEvent::FocusOut)
        cancelSeekHold();
    if ((watched == m_videoSurface || watched == m_playbackPane) && event->type() == QEvent::KeyRelease) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Right || key->key() == Qt::Key_Left) && m_seekKeyDirection) {
            handleSeekKey(key->key() == Qt::Key_Right ? 1 : -1, false, key->isAutoRepeat());
            return true;
        }
    }
    if ((watched == m_videoSurface || watched == m_playbackPane) && event->type() == QEvent::MouseButtonPress)
        static_cast<QWidget*>(watched)->setFocus(Qt::MouseFocusReason);
    if ((watched == m_videoSurface || watched == m_playbackPane) && event->type() == QEvent::KeyPress
        && m_playback->state().loaded) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() != Qt::NoModifier) return QWidget::eventFilter(watched, event);
        switch (key->key()) {
        case Qt::Key_Space: if (!key->isAutoRepeat()) togglePause(); break;
        case Qt::Key_M: if (!key->isAutoRepeat()) toggleMute(); break;
        case Qt::Key_Left: handleSeekKey(-1, true, key->isAutoRepeat()); break;
        case Qt::Key_Right: handleSeekKey(1, true, key->isAutoRepeat()); break;
        case Qt::Key_Up: m_playback->adjustVolume(5); break;
        case Qt::Key_Down: m_playback->adjustVolume(-5); break;
        case Qt::Key_PageUp: emit mediaNavigationRequested(-1); break;
        case Qt::Key_PageDown: emit mediaNavigationRequested(1); break;
        default: return QWidget::eventFilter(watched, event);
        }
        key->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void PlayerWidget::prefetchImages(const QStringList& paths) { m_imagePane->prefetch(paths); }

void PlayerWidget::setNotice(const QString& message)
{
    m_notice->setText(message);
    m_notice->setVisible(!message.isEmpty());
}

void PlayerWidget::setPlaybackMedia(PlaybackMedia media)
{
    m_controls->setVisible(media == PlaybackMedia::Video || media == PlaybackMedia::Audio);
}

void PlayerWidget::setFullscreen(bool fullscreen)
{
    m_controls->setFullscreen(fullscreen);
}

void PlayerWidget::setExplorerVisible(bool visible)
{
    m_showExplorer->setVisible(!visible);
}
