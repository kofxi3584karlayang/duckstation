#pragma once
#include "common/timer.h"
#include "common/types.h"
#include "qtprogresscallback.h"
#include "ui_coverdownloaddialog.h"
#include <QtWidgets/QDialog>
#include <array>
#include <memory>
#include <string>

class CoverDownloadDialog final : public QDialog
{
  Q_OBJECT

public:
  CoverDownloadDialog(QWidget* parent = nullptr);
  ~CoverDownloadDialog();

Q_SIGNALS:
  void coverRefreshRequested();

protected:
  void closeEvent(QCloseEvent* ev);

private Q_SLOTS:
  void onDownloadStatus(const QString& text);
  void onDownloadProgress(int value, int range);
  void onDownloadComplete();
  void onStartClicked();
  void onCloseClicked();
  void updateEnabled();

private:
  class CoverDownloadThread : public QtAsyncProgressThread
  {
  public:
    CoverDownloadThread(QWidget* parent, const QString& urls);
    ~CoverDownloadThread();

  protected:
    void runAsync() override;

  private:
    std::vector<std::string> m_urls;
  };

  void startThread();
  void cancelThread();

  Ui::CoverDownloadDialog m_ui;
  std::unique_ptr<CoverDownloadThread> m_thread;
  Common::Timer m_last_refresh_time;
};
