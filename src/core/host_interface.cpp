#include "host_interface.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/Log.h"
#include "common/audio_stream.h"
#include "system.h"
Log_SetChannel(HostInterface);

HostInterface::HostInterface()
{
  m_settings.gpu_renderer = Settings::GPURenderer::HardwareD3D11;
  m_settings.memory_card_a_filename = "memory_card_a.mcd";
}

HostInterface::~HostInterface() = default;

bool HostInterface::InitializeSystem(const char* filename, const char* exp1_filename)
{
  m_system = std::make_unique<System>(this, m_settings);
  if (!m_system->Initialize())
  {
    m_system.reset();
    return false;
  }

  m_system->Reset();

  if (filename)
  {
    const StaticString filename_str(filename);
    if (filename_str.EndsWith(".psexe", false) || filename_str.EndsWith(".exe", false))
    {
      Log_InfoPrintf("Sideloading EXE file '%s'", filename);
      if (!m_system->LoadEXE(filename))
      {
        Log_ErrorPrintf("Failed to load EXE file '%s'", filename);
        return false;
      }
    }
    else
    {
      Log_InfoPrintf("Inserting CDROM from image file '%s'", filename);
      if (!m_system->InsertMedia(filename))
      {
        Log_ErrorPrintf("Failed to insert media '%s'", filename);
        return false;
      }
    }
  }

  if (exp1_filename)
    m_system->SetExpansionROM(exp1_filename);

  // Resume execution.
  m_settings = m_system->GetSettings();
  return true;
}

bool HostInterface::LoadState(const char* filename)
{
  ByteStream* stream;
  if (!ByteStream_OpenFileStream(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED, &stream))
    return false;

  ReportMessage(SmallString::FromFormat("Loading state from %s...", filename));

  const bool result = m_system->LoadState(stream);
  if (!result)
  {
    ReportMessage(SmallString::FromFormat("Loading state from %s failed. Resetting.", filename));
    m_system->Reset();
  }

  stream->Release();
  return result;
}

bool HostInterface::SaveState(const char* filename)
{
  ByteStream* stream;
  if (!ByteStream_OpenFileStream(filename,
                                 BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_WRITE | BYTESTREAM_OPEN_TRUNCATE |
                                   BYTESTREAM_OPEN_ATOMIC_UPDATE | BYTESTREAM_OPEN_STREAMED,
                                 &stream))
  {
    return false;
  }

  const bool result = m_system->SaveState(stream);
  if (!result)
  {
    ReportMessage(SmallString::FromFormat("Saving state to %s failed.", filename));
    stream->Discard();
  }
  else
  {
    ReportMessage(SmallString::FromFormat("State saved to %s.", filename));
    stream->Commit();
  }

  stream->Release();
  return result;
}
