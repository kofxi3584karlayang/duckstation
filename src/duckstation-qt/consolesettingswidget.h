#pragma once

#include <QtWidgets/QWidget>

#include "ui_consolesettingswidget.h"

class QtHostInterface;
class SettingsDialog;

class ConsoleSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ConsoleSettingsWidget(QtHostInterface* host_interface, QWidget* parent, SettingsDialog* dialog);
  ~ConsoleSettingsWidget();

private:
  Ui::ConsoleSettingsWidget m_ui;

  QtHostInterface* m_host_interface;
};
