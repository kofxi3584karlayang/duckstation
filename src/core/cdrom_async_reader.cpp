#include "cdrom_async_reader.h"
#include "common/assert.h"
#include "common/log.h"
#include "common/timer.h"
Log_SetChannel(CDROMAsyncReader);

CDROMAsyncReader::CDROMAsyncReader() = default;

CDROMAsyncReader::~CDROMAsyncReader()
{
  StopThread();
}

void CDROMAsyncReader::StartThread()
{
  if (IsUsingThread())
    return;

  m_shutdown_flag.store(false);
  m_read_thread = std::thread(&CDROMAsyncReader::WorkerThreadEntryPoint, this);
}

void CDROMAsyncReader::StopThread()
{
  if (!IsUsingThread())
    return;

  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_sector_read_pending.load())
      m_notify_read_complete_cv.wait(lock, [this]() { return !m_sector_read_pending.load(); });

    m_shutdown_flag.store(true);
    m_do_read_cv.notify_one();
  }

  m_read_thread.join();
}

void CDROMAsyncReader::SetMedia(std::unique_ptr<CDImage> media)
{
  WaitForReadToComplete();
  m_media = std::move(media);
}

std::unique_ptr<CDImage> CDROMAsyncReader::RemoveMedia()
{
  WaitForReadToComplete();
  return std::move(m_media);
}

void CDROMAsyncReader::QueueReadSector(CDImage::LBA lba)
{
  if (!IsUsingThread())
  {
    m_sector_read_pending.store(true);
    m_next_position_set.store(true);
    m_next_position = lba;
    DoSectorRead();
    return;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_sector_read_pending.load())
    m_notify_read_complete_cv.wait(lock, [this]() { return !m_sector_read_pending.load(); });

  // don't re-read the same sector if it was the last one we read
  // the CDC code does this when seeking->reading
  if (m_last_read_sector == lba && m_sector_read_result.load())
  {
    Log_DebugPrintf("Skipping re-reading same sector %u", lba);
    return;
  }

  m_sector_read_pending.store(true);
  m_next_position_set.store(true);
  m_next_position = lba;
  m_do_read_cv.notify_one();
}

bool CDROMAsyncReader::ReadSectorUncached(CDImage::LBA lba, CDImage::SubChannelQ* subq, SectorBuffer* data)
{
  WaitForReadToComplete();

  if (m_media->GetPositionOnDisc() != lba && !m_media->Seek(lba))
  {
    Log_WarningPrintf("Seek to LBA %u failed", lba);
    return false;
  }

  if (!m_media->ReadRawSector(data, subq))
  {
    Log_WarningPrintf("Read of LBA %u failed", lba);
    return false;
  }

  return true;
}

void CDROMAsyncReader::QueueReadNextSector()
{
  if (!IsUsingThread())
  {
    m_sector_read_pending.store(true);
    DoSectorRead();
    return;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_sector_read_pending.load())
    m_notify_read_complete_cv.wait(lock, [this]() { return !m_sector_read_pending.load(); });

  m_sector_read_pending.store(true);
  m_do_read_cv.notify_one();
}

bool CDROMAsyncReader::WaitForReadToComplete()
{
  if (!IsUsingThread())
    return m_sector_read_result.load();

  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_sector_read_pending.load())
  {
    Log_DebugPrintf("Sector read pending, waiting");

    Common::Timer wait_timer;
    m_notify_read_complete_cv.wait(lock, [this]() { return !m_sector_read_pending.load(); });

    const double wait_time = wait_timer.GetTimeMilliseconds();
    if (wait_time > 1.0f)
      Log_WarningPrintf("Had to wait %.2f msec for LBA %u", wait_time, m_last_read_sector);
  }

  return m_sector_read_result.load();
}

void CDROMAsyncReader::DoSectorRead()
{
  Common::Timer timer;

  if (m_next_position_set.load())
  {
    if (m_media->GetPositionOnDisc() != m_next_position && !m_media->Seek(m_next_position))
    {
      Log_WarningPrintf("Seek to LBA %u failed", m_next_position);
      m_sector_read_result.store(false);
      return;
    }
  }

  const CDImage::LBA pos = m_media->GetPositionOnDisc();
  if (!m_media->ReadRawSector(m_sector_buffer.data(), &m_subq))
  {
    m_sector_read_result.store(false);
    Log_WarningPrintf("Read of LBA %u failed", pos);
    return;
  }

  m_last_read_sector = pos;
  m_sector_read_result.store(true);

  const double read_time = timer.GetTimeMilliseconds();
  if (read_time > 1.0f)
    Log_DevPrintf("Read LBA %u took %.2f msec", pos, read_time);
}

void CDROMAsyncReader::WorkerThreadEntryPoint()
{
  std::unique_lock lock(m_mutex);

  while (!m_shutdown_flag.load())
  {
    m_do_read_cv.wait(lock, [this]() { return (m_shutdown_flag.load() || m_sector_read_pending.load()); });
    if (m_sector_read_pending.load())
    {
      lock.unlock();
      DoSectorRead();
      lock.lock();
      m_sector_read_pending.store(false);
      m_notify_read_complete_cv.notify_one();
    }
  }
}
