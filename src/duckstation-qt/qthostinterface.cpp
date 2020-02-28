#include "qthostinterface.h"
#include "common/assert.h"
#include "common/audio_stream.h"
#include "common/byte_stream.h"
#include "common/log.h"
#include "common/string_util.h"
#include "core/controller.h"
#include "core/game_list.h"
#include "core/gpu.h"
#include "core/system.h"
#include "frontend-common/sdl_audio_stream.h"
#include "frontend-common/sdl_controller_interface.h"
#include "qtsettingsinterface.h"
#include "qtutils.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <memory>
Log_SetChannel(QtHostInterface);

#ifdef WIN32
#include "d3d11displaywindow.h"
#endif

QtHostInterface::QtHostInterface(QObject* parent)
  : QObject(parent), CommonHostInterface(),
    m_qsettings(QString::fromStdString(GetSettingsFileName()), QSettings::IniFormat)
{
  loadSettings();
  refreshGameList();
  createThread();
}

QtHostInterface::~QtHostInterface()
{
  Assert(!m_display_window);
  stopThread();
}

void QtHostInterface::ReportError(const char* message)
{
  HostInterface::ReportError(message);

  emit errorReported(QString::fromLocal8Bit(message));
}

void QtHostInterface::ReportMessage(const char* message)
{
  HostInterface::ReportMessage(message);

  emit messageReported(QString::fromLocal8Bit(message));
}

bool QtHostInterface::ConfirmMessage(const char* message)
{
  return messageConfirmed(QString::fromLocal8Bit(message));
}

QVariant QtHostInterface::getSettingValue(const QString& name, const QVariant& default_value)
{
  std::lock_guard<std::mutex> guard(m_qsettings_mutex);
  return m_qsettings.value(name, default_value);
}

void QtHostInterface::putSettingValue(const QString& name, const QVariant& value)
{
  std::lock_guard<std::mutex> guard(m_qsettings_mutex);
  m_qsettings.setValue(name, value);
}

void QtHostInterface::removeSettingValue(const QString& name)
{
  std::lock_guard<std::mutex> guard(m_qsettings_mutex);
  m_qsettings.remove(name);
}

void QtHostInterface::setDefaultSettings()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "setDefaultSettings", Qt::QueuedConnection);
    return;
  }

  std::lock_guard<std::mutex> guard(m_qsettings_mutex);
  QtSettingsInterface si(m_qsettings);
  UpdateSettings([this, &si]() { m_settings.Load(si); });
  UpdateInputMap(si);
}

void QtHostInterface::applySettings()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "applySettings", Qt::QueuedConnection);
    return;
  }

  std::lock_guard<std::mutex> guard(m_qsettings_mutex);
  QtSettingsInterface si(m_qsettings);
  UpdateSettings([this, &si]() { m_settings.Load(si); });
  UpdateInputMap(si);
}

void QtHostInterface::loadSettings()
{
  // no need to lock here because the emu thread doesn't exist yet
  QtSettingsInterface si(m_qsettings);

  const QSettings::Status settings_status = m_qsettings.status();
  if (settings_status != QSettings::NoError)
  {
    m_qsettings.clear();
    SetDefaultSettings(si);
  }

  CheckSettings(si);
  m_settings.Load(si);

  // input map update is done on the emu thread
}

void QtHostInterface::refreshGameList(bool invalidate_cache /* = false */, bool invalidate_database /* = false */)
{
  std::lock_guard<std::mutex> lock(m_qsettings_mutex);
  QtSettingsInterface si(m_qsettings);
  m_game_list->SetSearchDirectoriesFromSettings(si);
  m_game_list->Refresh(invalidate_cache, invalidate_database);
  emit gameListRefreshed();
}

QtDisplayWindow* QtHostInterface::createDisplayWindow()
{
  Assert(!m_display_window);

#ifdef WIN32
  if (m_settings.gpu_renderer == GPURenderer::HardwareOpenGL)
    m_display_window = new OpenGLDisplayWindow(this, nullptr);
  else
    m_display_window = new D3D11DisplayWindow(this, nullptr);
#else
  m_display_window = new OpenGLDisplayWindow(this, nullptr);
#endif
  connect(m_display_window, &QtDisplayWindow::windowResizedEvent, this, &QtHostInterface::onDisplayWindowResized);
  return m_display_window;
}

void QtHostInterface::bootSystemFromFile(const QString& filename)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "bootSystemFromFile", Qt::QueuedConnection, Q_ARG(const QString&, filename));
    return;
  }

  HostInterface::BootSystemFromFile(filename.toStdString().c_str());
}

void QtHostInterface::resumeSystemFromState(const QString& filename, bool boot_on_failure)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "resumeSystemFromState", Qt::QueuedConnection, Q_ARG(const QString&, filename),
                              Q_ARG(bool, boot_on_failure));
    return;
  }

  if (filename.isEmpty())
    HostInterface::ResumeSystemFromMostRecentState();
  else
    HostInterface::ResumeSystemFromState(filename.toStdString().c_str(), boot_on_failure);
}

void QtHostInterface::bootSystemFromBIOS()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "bootSystemFromBIOS", Qt::QueuedConnection);
    return;
  }

  HostInterface::BootSystemFromBIOS();
}

void QtHostInterface::handleKeyEvent(int key, bool pressed)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "handleKeyEvent", Qt::QueuedConnection, Q_ARG(int, key), Q_ARG(bool, pressed));
    return;
  }

  HandleHostKeyEvent(key, pressed);
}

void QtHostInterface::onDisplayWindowResized(int width, int height)
{
  // this can be null if it was destroyed and the main thread is late catching up
  if (m_display_window)
    m_display_window->onWindowResized(width, height);
}

bool QtHostInterface::AcquireHostDisplay()
{
  DebugAssert(!m_display_window);

  emit createDisplayWindowRequested(m_worker_thread, m_settings.gpu_use_debug_device);
  if (!m_display_window->hasDeviceContext())
  {
    m_display_window = nullptr;
    emit destroyDisplayWindowRequested();
    return false;
  }

  if (!m_display_window->initializeDeviceContext(m_settings.gpu_use_debug_device))
  {
    m_display_window->destroyDeviceContext();
    m_display_window = nullptr;
    emit destroyDisplayWindowRequested();
    return false;
  }

  m_display = m_display_window->getHostDisplayInterface();
  return true;
}

void QtHostInterface::ReleaseHostDisplay()
{
  DebugAssert(m_display_window && m_display == m_display_window->getHostDisplayInterface());
  m_display = nullptr;
  m_display_window->destroyDeviceContext();
  m_display_window = nullptr;
  emit destroyDisplayWindowRequested();
}

void QtHostInterface::SetFullscreen(bool enabled)
{
  emit setFullscreenRequested(enabled);
}

void QtHostInterface::ToggleFullscreen()
{
  emit toggleFullscreenRequested();
}

std::optional<CommonHostInterface::HostKeyCode> QtHostInterface::GetHostKeyCode(const std::string_view key_code) const
{
  const std::optional<int> code =
    QtUtils::ParseKeyString(QString::fromUtf8(key_code.data(), static_cast<int>(key_code.length())));
  if (!code)
    return std::nullopt;

  return static_cast<s32>(*code);
}

void QtHostInterface::OnSystemCreated()
{
  HostInterface::OnSystemCreated();

  wakeThread();
  destroyBackgroundControllerPollTimer();

  emit emulationStarted();
}

void QtHostInterface::OnSystemPaused(bool paused)
{
  HostInterface::OnSystemPaused(paused);

  emit emulationPaused(paused);

  if (!paused)
  {
    wakeThread();
    emit focusDisplayWidgetRequested();
  }
}

void QtHostInterface::OnSystemDestroyed()
{
  HostInterface::OnSystemDestroyed();

  if (m_background_controller_polling_enable_count > 0)
    createBackgroundControllerPollTimer();

  emit emulationStopped();
}

void QtHostInterface::OnSystemPerformanceCountersUpdated()
{
  HostInterface::OnSystemPerformanceCountersUpdated();

  DebugAssert(m_system);
  emit systemPerformanceCountersUpdated(m_system->GetEmulationSpeed(), m_system->GetFPS(), m_system->GetVPS(),
                                        m_system->GetAverageFrameTime(), m_system->GetWorstFrameTime());
}

void QtHostInterface::OnRunningGameChanged()
{
  HostInterface::OnRunningGameChanged();

  if (m_system)
  {
    emit runningGameChanged(QString::fromStdString(m_system->GetRunningPath()),
                            QString::fromStdString(m_system->GetRunningCode()),
                            QString::fromStdString(m_system->GetRunningTitle()));
  }
  else
  {
    emit runningGameChanged(QString(), QString(), QString());
  }
}

void QtHostInterface::OnSystemStateSaved(bool global, s32 slot)
{
  emit stateSaved(QString::fromStdString(m_system->GetRunningCode()), global, slot);
}

void QtHostInterface::OnControllerTypeChanged(u32 slot)
{
  HostInterface::OnControllerTypeChanged(slot);

  updateInputMap();
}

void QtHostInterface::updateInputMap()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "updateInputMap", Qt::QueuedConnection);
    return;
  }

  std::lock_guard<std::mutex> lock(m_qsettings_mutex);
  QtSettingsInterface si(m_qsettings);
  UpdateInputMap(si);
}

void QtHostInterface::powerOffSystem()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "powerOffSystem", Qt::QueuedConnection);
    return;
  }

  if (!m_system)
    return;

  if (m_settings.save_state_on_exit)
    SaveResumeSaveState();

  DestroySystem();
}

void QtHostInterface::synchronousPowerOffSystem()
{
  if (!isOnWorkerThread())
    QMetaObject::invokeMethod(this, "powerOffSystem", Qt::BlockingQueuedConnection);
  else
    powerOffSystem();
}

void QtHostInterface::resetSystem()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "resetSystem", Qt::QueuedConnection);
    return;
  }

  if (!m_system)
  {
    Log_ErrorPrintf("resetSystem() called without system");
    return;
  }

  HostInterface::ResetSystem();
}

void QtHostInterface::pauseSystem(bool paused)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "pauseSystem", Qt::QueuedConnection, Q_ARG(bool, paused));
    return;
  }

  if (!m_system)
    return;

  m_paused = paused;
  m_audio_stream->PauseOutput(paused);
  if (!paused)
    wakeThread();
  emit emulationPaused(paused);
}

void QtHostInterface::changeDisc(const QString& new_disc_filename)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "changeDisc", Qt::QueuedConnection, Q_ARG(const QString&, new_disc_filename));
    return;
  }

  if (!m_system)
    return;

  m_system->InsertMedia(new_disc_filename.toStdString().c_str());
}

void QtHostInterface::populateSaveStateMenus(const char* game_code, QMenu* load_menu, QMenu* save_menu)
{
  const std::vector<SaveStateInfo> available_states(GetAvailableSaveStates(game_code));

  load_menu->clear();
  if (!available_states.empty())
  {
    bool last_global = available_states.front().global;
    s32 last_slot = available_states.front().slot;
    for (const SaveStateInfo& ssi : available_states)
    {
      const s32 slot = ssi.slot;
      const bool global = ssi.global;
      const QDateTime timestamp(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ssi.timestamp)));
      const QString timestamp_str(timestamp.toString(Qt::SystemLocaleShortDate));
      const QString path(QString::fromStdString(ssi.path));

      QString title;
      if (slot < 0)
        title = tr("Resume Save (%1)").arg(timestamp_str);
      else
        title = tr("%1 Save %2 (%3)").arg(global ? tr("Global") : tr("Game")).arg(slot).arg(timestamp_str);

      if (global != last_global || last_slot < 0)
        load_menu->addSeparator();

      last_global = global;
      last_slot = slot;

      QAction* action = load_menu->addAction(title);
      connect(action, &QAction::triggered, [this, path]() { loadState(path); });
    }
  }

  save_menu->clear();
  if (game_code && std::strlen(game_code) > 0)
  {
    for (s32 i = 1; i <= PER_GAME_SAVE_STATE_SLOTS; i++)
    {
      QAction* action = save_menu->addAction(tr("Game Save %1").arg(i));
      connect(action, &QAction::triggered, [this, i]() { saveState(false, i); });
    }

    save_menu->addSeparator();
  }

  for (s32 i = 1; i <= GLOBAL_SAVE_STATE_SLOTS; i++)
  {
    QAction* action = save_menu->addAction(tr("Global Save %1").arg(i));
    connect(action, &QAction::triggered, [this, i]() { saveState(true, i); });
  }
}

void QtHostInterface::loadState(const QString& filename)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "loadState", Qt::QueuedConnection, Q_ARG(const QString&, filename));
    return;
  }

  LoadState(filename.toStdString().c_str());
}

void QtHostInterface::loadState(bool global, qint32 slot)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "loadState", Qt::QueuedConnection, Q_ARG(bool, global), Q_ARG(qint32, slot));
    return;
  }

  LoadState(global, slot);
}

void QtHostInterface::saveState(bool global, qint32 slot, bool block_until_done /* = false */)
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "saveState", block_until_done ? Qt::BlockingQueuedConnection : Qt::QueuedConnection,
                              Q_ARG(bool, global), Q_ARG(qint32, slot), Q_ARG(bool, block_until_done));
    return;
  }

  if (m_system)
    SaveState(global, slot);
}

void QtHostInterface::enableBackgroundControllerPolling()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "enableBackgroundControllerPolling", Qt::BlockingQueuedConnection);
    return;
  }

  if (m_background_controller_polling_enable_count++ > 0)
    return;

  if (!m_system)
  {
    createBackgroundControllerPollTimer();

    // drain the event queue so we don't get events late
    g_sdl_controller_interface.PumpSDLEvents();
  }
}

void QtHostInterface::disableBackgroundControllerPolling()
{
  if (!isOnWorkerThread())
  {
    QMetaObject::invokeMethod(this, "disableBackgroundControllerPolling");
    return;
  }

  Assert(m_background_controller_polling_enable_count > 0);
  if (--m_background_controller_polling_enable_count > 0)
    return;

  if (!m_system)
    destroyBackgroundControllerPollTimer();
}

void QtHostInterface::doBackgroundControllerPoll()
{
  g_sdl_controller_interface.PumpSDLEvents();
}

void QtHostInterface::createBackgroundControllerPollTimer()
{
  DebugAssert(!m_background_controller_polling_timer);
  m_background_controller_polling_timer = new QTimer(this);
  m_background_controller_polling_timer->setSingleShot(false);
  m_background_controller_polling_timer->setTimerType(Qt::VeryCoarseTimer);
  connect(m_background_controller_polling_timer, &QTimer::timeout, this, &QtHostInterface::doBackgroundControllerPoll);
  m_background_controller_polling_timer->start(BACKGROUND_CONTROLLER_POLLING_INTERVAL);
}

void QtHostInterface::destroyBackgroundControllerPollTimer()
{
  delete m_background_controller_polling_timer;
  m_background_controller_polling_timer = nullptr;
}

void QtHostInterface::createThread()
{
  m_original_thread = QThread::currentThread();
  m_worker_thread = new Thread(this);
  m_worker_thread->start();
  moveToThread(m_worker_thread);
}

void QtHostInterface::stopThread()
{
  Assert(!isOnWorkerThread());

  QMetaObject::invokeMethod(this, "doStopThread", Qt::QueuedConnection);
  m_worker_thread->wait();
}

void QtHostInterface::doStopThread()
{
  m_shutdown_flag.store(true);
  m_worker_thread_event_loop->quit();
}

void QtHostInterface::threadEntryPoint()
{
  m_worker_thread_event_loop = new QEventLoop();

  // set up controller interface and immediate poll to pick up the controller attached events
  g_sdl_controller_interface.Initialize(this);
  g_sdl_controller_interface.PumpSDLEvents();
  updateInputMap();

  // TODO: Event which flags the thread as ready
  while (!m_shutdown_flag.load())
  {
    if (!m_system || m_paused)
    {
      // wait until we have a system before running
      m_worker_thread_event_loop->exec();
      continue;
    }

    m_system->RunFrame();

    m_system->GetGPU()->ResetGraphicsAPIState();

    DrawDebugWindows();
    DrawOSDMessages();

    m_display->Render();

    m_system->GetGPU()->RestoreGraphicsAPIState();

    if (m_speed_limiter_enabled)
      m_system->Throttle();

    m_worker_thread_event_loop->processEvents(QEventLoop::AllEvents);
    g_sdl_controller_interface.PumpSDLEvents();
  }

  m_system.reset();
  m_audio_stream.reset();

  g_sdl_controller_interface.Shutdown();

  delete m_worker_thread_event_loop;
  m_worker_thread_event_loop = nullptr;

  // move back to UI thread
  moveToThread(m_original_thread);
}

void QtHostInterface::wakeThread()
{
  if (isOnWorkerThread())
    m_worker_thread_event_loop->quit();
  else
    QMetaObject::invokeMethod(m_worker_thread_event_loop, "quit", Qt::QueuedConnection);
}

QtHostInterface::Thread::Thread(QtHostInterface* parent) : QThread(parent), m_parent(parent) {}

QtHostInterface::Thread::~Thread() = default;

void QtHostInterface::Thread::run()
{
  m_parent->threadEntryPoint();
}
