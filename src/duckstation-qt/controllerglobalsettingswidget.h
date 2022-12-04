// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once
#include "common/types.h"
#include <QtCore/QMap>
#include <QtWidgets/QWidget>
#include <array>
#include <vector>

#include "ui_controllerglobalsettingswidget.h"

class ControllerSettingsDialog;

class ControllerGlobalSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  ControllerGlobalSettingsWidget(QWidget* parent, ControllerSettingsDialog* dialog);
  ~ControllerGlobalSettingsWidget();

  void addDeviceToList(const QString& identifier, const QString& name);
  void removeDeviceFromList(const QString& identifier);

Q_SIGNALS:
  void bindingSetupChanged();

private:
  void updateSDLOptionsEnabled();

  Ui::ControllerGlobalSettingsWidget m_ui;
  ControllerSettingsDialog* m_dialog;
};
