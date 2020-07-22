#pragma once
#include "common/types.h"
#include "ui_inputbindingdialog.h"
#include <QtWidgets/QDialog>
#include <string>
#include <vector>

class QtHostInterface;

class InputBindingDialog : public QDialog
{
  Q_OBJECT

public:
  InputBindingDialog(QtHostInterface* host_interface, std::string section_name, std::string key_name,
                     std::vector<std::string> bindings, QWidget* parent);
  ~InputBindingDialog();

protected Q_SLOTS:
  void onAddBindingButtonClicked();
  void onRemoveBindingButtonClicked();
  void onClearBindingsButtonClicked();
  void onInputListenTimerTimeout();

protected:
  enum : u32
  {
    TIMEOUT_FOR_BINDING = 5
  };

  virtual bool eventFilter(QObject* watched, QEvent* event) override;

  virtual void startListeningForInput(u32 timeout_in_seconds);
  virtual void stopListeningForInput();

  bool isListeningForInput() const { return m_input_listen_timer != nullptr; }
  void addNewBinding(std::string new_binding);

  void updateList();
  void saveListToSettings();

  Ui::InputBindingDialog m_ui;

  QtHostInterface* m_host_interface;

  std::string m_section_name;
  std::string m_key_name;
  std::vector<std::string> m_bindings;
  std::string m_new_binding_value;

  QTimer* m_input_listen_timer = nullptr;
  u32 m_input_listen_remaining_seconds = 0;
};

class InputButtonBindingDialog : public InputBindingDialog
{
  Q_OBJECT

public:
  InputButtonBindingDialog(QtHostInterface* host_interface, std::string section_name, std::string key_name,
                           std::vector<std::string> bindings, QWidget* parent);
  ~InputButtonBindingDialog();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
  void bindToControllerAxis(int controller_index, int axis_index, bool positive);
  void bindToControllerButton(int controller_index, int button_index);

protected:
  void startListeningForInput(u32 timeout_in_seconds) override;
  void stopListeningForInput() override;
  void hookControllerInput();
  void unhookControllerInput();
};

class InputAxisBindingDialog : public InputBindingDialog
{
  Q_OBJECT

public:
  InputAxisBindingDialog(QtHostInterface* host_interface, std::string section_name, std::string key_name,
                           std::vector<std::string> bindings, QWidget* parent);
  ~InputAxisBindingDialog();

private Q_SLOTS:
  void bindToControllerAxis(int controller_index, int axis_index);

protected:
  void startListeningForInput(u32 timeout_in_seconds) override;
  void stopListeningForInput() override;
  void hookControllerInput();
  void unhookControllerInput();
};
