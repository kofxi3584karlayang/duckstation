#include "displaysettingswidget.h"
#include "core/gpu.h"
#include "core/settings.h"
#include "postprocessingchainconfigwidget.h"
#include "qtutils.h"
#include "settingsdialog.h"
#include "settingwidgetbinder.h"
#include <QtWidgets/QMessageBox>

// For enumerating adapters.
#include "frontend-common/vulkan_host_display.h"
#ifdef WIN32
#include "frontend-common/d3d11_host_display.h"
#endif

DisplaySettingsWidget::DisplaySettingsWidget(QtHostInterface* host_interface, QWidget* parent, SettingsDialog* dialog)
  : QWidget(parent), m_host_interface(host_interface)
{
  m_ui.setupUi(this);
  setupAdditionalUi();

  SettingWidgetBinder::BindWidgetToEnumSetting(m_host_interface, m_ui.renderer, "GPU", "Renderer",
                                               &Settings::ParseRendererName, &Settings::GetRendererName,
                                               Settings::DEFAULT_GPU_RENDERER);
  SettingWidgetBinder::BindWidgetToEnumSetting(m_host_interface, m_ui.displayAspectRatio, "Display", "AspectRatio",
                                               &Settings::ParseDisplayAspectRatio, &Settings::GetDisplayAspectRatioName,
                                               Settings::DEFAULT_DISPLAY_ASPECT_RATIO);
  SettingWidgetBinder::BindWidgetToEnumSetting(m_host_interface, m_ui.displayCropMode, "Display", "CropMode",
                                               &Settings::ParseDisplayCropMode, &Settings::GetDisplayCropModeName,
                                               Settings::DEFAULT_DISPLAY_CROP_MODE);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.displayLinearFiltering, "Display",
                                               "LinearFiltering");
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.displayIntegerScaling, "Display",
                                               "IntegerScaling");
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.vsync, "Display", "VSync");
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.showOSDMessages, "Display", "ShowOSDMessages",
                                               true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.showFPS, "Display", "ShowFPS", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.showVPS, "Display", "ShowVPS", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.showSpeed, "Display", "ShowSpeed", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_host_interface, m_ui.showResolution, "Display", "ShowResolution",
                                               false);

  connect(m_ui.renderer, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &DisplaySettingsWidget::populateGPUAdapters);
  connect(m_ui.adapter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &DisplaySettingsWidget::onGPUAdapterIndexChanged);
  populateGPUAdapters();

  dialog->registerWidgetHelp(
    m_ui.renderer, tr("Renderer"), Settings::GetRendererDisplayName(Settings::DEFAULT_GPU_RENDERER),
    tr("Chooses the backend to use for rendering the console/game visuals. <br>Depending on your system and hardware, "
       "Direct3D 11 and OpenGL hardware backends may be available. <br>The software renderer offers the best "
       "compatibility, "
       "but is the slowest and does not offer any enhancements."));
  dialog->registerWidgetHelp(
    m_ui.adapter, tr("Adapter"), tr("(Default)"),
    tr("If your system contains multiple GPUs or adapters, you can select which GPU you wish to use for the hardware "
       "renderers. <br>This option is only supported in Direct3D and Vulkan. OpenGL will always use the default "
       "device."));
  dialog->registerWidgetHelp(
    m_ui.displayAspectRatio, tr("Aspect Ratio"), QStringLiteral("4:3"),
    tr("Changes the aspect ratio used to display the console's output to the screen. The default "
       "is 4:3 which matches a typical TV of the era."));
  dialog->registerWidgetHelp(
    m_ui.displayCropMode, tr("Crop Mode"), tr("Only Overscan Area"),
    tr("Determines how much of the area typically not visible on a consumer TV set to crop/hide. <br>"
       "Some games display content in the overscan area, or use it for screen effects. <br>May "
       "not display correctly with the \"All Borders\" setting. \"Only Overscan\" offers a good "
       "compromise between stability and hiding black borders."));
  dialog->registerWidgetHelp(m_ui.displayLinearFiltering, tr("Linear Upscaling"), tr("Checked"),
                             tr("Uses bilinear texture filtering when displaying the console's framebuffer to the "
                                "screen. <br>Disabling filtering "
                                "will producer a sharper, blockier/pixelated image. Enabling will smooth out the "
                                "image. <br>The option will be less "
                                "noticable the higher the resolution scale."));
  dialog->registerWidgetHelp(
    m_ui.displayIntegerScaling, tr("Integer Upscaling"), tr("Unchecked"),
    tr("Adds padding to the display area to ensure that the ratio between pixels on the host to "
       "pixels in the console is an integer number. <br>May result in a sharper image in some 2D games."));
  dialog->registerWidgetHelp(
    m_ui.vsync, tr("VSync"), tr("Checked"),
    tr("Enable this option to match DuckStation's refresh rate with your current monitor or screen. "
       "VSync is automatically disabled when it is not possible (e.g. running at non-100% speed)."));
  dialog->registerWidgetHelp(m_ui.showOSDMessages, tr("Show OSD Messages"), tr("Checked"),
                             tr("Shows on-screen-display messages when events occur such as save states being "
                                "created/loaded, screenshots being taken, etc."));
  dialog->registerWidgetHelp(m_ui.showFPS, tr("Show FPS"), tr("Unchecked"),
                             tr("Shows the internal frame rate of the game in the top-right corner of the display."));
  dialog->registerWidgetHelp(m_ui.showVPS, tr("Show VPS"), tr("Unchecked"),
                             tr("Shows the number of frames (or v-syncs) displayed per second by the system in the "
                                "top-right corner of the display."));
  dialog->registerWidgetHelp(
    m_ui.showSpeed, tr("Show Speed"), tr("Unchecked"),
    tr("Shows the current emulation speed of the system in the top-right corner of the display as a percentage."));
}

DisplaySettingsWidget::~DisplaySettingsWidget() = default;

void DisplaySettingsWidget::setupAdditionalUi()
{
  for (u32 i = 0; i < static_cast<u32>(GPURenderer::Count); i++)
  {
    m_ui.renderer->addItem(
      qApp->translate("GPURenderer", Settings::GetRendererDisplayName(static_cast<GPURenderer>(i))));
  }

  for (u32 i = 0; i < static_cast<u32>(DisplayAspectRatio::Count); i++)
  {
    m_ui.displayAspectRatio->addItem(
      QString::fromUtf8(Settings::GetDisplayAspectRatioName(static_cast<DisplayAspectRatio>(i))));
  }

  for (u32 i = 0; i < static_cast<u32>(DisplayCropMode::Count); i++)
  {
    m_ui.displayCropMode->addItem(
      qApp->translate("DisplayCropMode", Settings::GetDisplayCropModeDisplayName(static_cast<DisplayCropMode>(i))));
  }
}

void DisplaySettingsWidget::populateGPUAdapters()
{
  std::vector<std::string> adapter_names;
  switch (static_cast<GPURenderer>(m_ui.renderer->currentIndex()))
  {
#ifdef WIN32
    case GPURenderer::HardwareD3D11:
      adapter_names = FrontendCommon::D3D11HostDisplay::EnumerateAdapterNames();
      break;
#endif

    case GPURenderer::HardwareVulkan:
      adapter_names = FrontendCommon::VulkanHostDisplay::EnumerateAdapterNames();
      break;

    default:
      break;
  }

  QString current_value = QString::fromStdString(m_host_interface->GetStringSettingValue("GPU", "Adapter"));

  QSignalBlocker blocker(m_ui.adapter);

  // add the default entry - we'll fall back to this if the GPU no longer exists, or there's no options
  m_ui.adapter->clear();
  m_ui.adapter->addItem(tr("(Default)"));

  // add the other adapters
  for (const std::string& adapter_name : adapter_names)
  {
    QString qadapter_name(QString::fromStdString(adapter_name));
    m_ui.adapter->addItem(qadapter_name);

    if (qadapter_name == current_value)
      m_ui.adapter->setCurrentIndex(m_ui.adapter->count() - 1);
  }

  // disable it if we don't have a choice
  m_ui.adapter->setEnabled(!adapter_names.empty());
}

void DisplaySettingsWidget::onGPUAdapterIndexChanged()
{
  if (m_ui.adapter->currentIndex() == 0)
  {
    // default
    m_host_interface->RemoveSettingValue("GPU", "Adapter");
    return;
  }

  m_host_interface->SetStringSettingValue("GPU", "Adapter", m_ui.adapter->currentText().toUtf8().constData());
}
