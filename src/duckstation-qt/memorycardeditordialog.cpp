#include "memorycardeditordialog.h"
#include "common/file_system.h"
#include "common/string_util.h"
#include "core/host_interface.h"
#include "qtutils.h"
#include <QtCore/QFileInfo>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>

static constexpr char MEMORY_CARD_IMAGE_FILTER[] =
  QT_TRANSLATE_NOOP("MemoryCardEditorDialog", "All Memory Card Types (*.mcd *.mcr *.mc)");
static constexpr char MEMORY_CARD_IMPORT_FILTER[] =
  QT_TRANSLATE_NOOP("MemoryCardEditorDialog", "All Importable Memory Card Types (*.mcd *.mcr *.mc *.gme)");

MemoryCardEditorDialog::MemoryCardEditorDialog(QWidget* parent) : QDialog(parent)
{
  m_ui.setupUi(this);
  m_card_a.path_cb = m_ui.cardAPath;
  m_card_a.table = m_ui.cardA;
  m_card_a.blocks_free_label = m_ui.cardAUsage;
  m_card_a.save_button = m_ui.saveCardA;
  m_card_b.path_cb = m_ui.cardBPath;
  m_card_b.table = m_ui.cardB;
  m_card_b.blocks_free_label = m_ui.cardBUsage;
  m_card_b.save_button = m_ui.saveCardB;

  connectUi();
  populateComboBox(m_ui.cardAPath);
  populateComboBox(m_ui.cardBPath);
}

MemoryCardEditorDialog::~MemoryCardEditorDialog() = default;

void MemoryCardEditorDialog::resizeEvent(QResizeEvent* ev)
{
  QtUtils::ResizeColumnsForTableView(m_card_a.table, {32, -1, 100, 45});
  QtUtils::ResizeColumnsForTableView(m_card_b.table, {32, -1, 100, 45});
}

void MemoryCardEditorDialog::closeEvent(QCloseEvent* ev)
{
  promptForSave(&m_card_a);
  promptForSave(&m_card_b);
}

void MemoryCardEditorDialog::connectUi()
{
  connect(m_ui.cardA, &QTableWidget::itemSelectionChanged, this, &MemoryCardEditorDialog::onCardASelectionChanged);
  connect(m_ui.cardB, &QTableWidget::itemSelectionChanged, this, &MemoryCardEditorDialog::onCardBSelectionChanged);
  connect(m_ui.moveLeft, &QPushButton::clicked, this, &MemoryCardEditorDialog::doCopyFile);
  connect(m_ui.moveRight, &QPushButton::clicked, this, &MemoryCardEditorDialog::doCopyFile);
  connect(m_ui.deleteFile, &QPushButton::clicked, this, &MemoryCardEditorDialog::doDeleteFile);

  connect(m_ui.cardAPath, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int index) { loadCardFromComboBox(&m_card_a, index); });
  connect(m_ui.cardBPath, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int index) { loadCardFromComboBox(&m_card_b, index); });
  connect(m_ui.newCardA, &QPushButton::clicked, [this]() { newCard(&m_card_a); });
  connect(m_ui.newCardB, &QPushButton::clicked, [this]() { newCard(&m_card_b); });
  connect(m_ui.saveCardA, &QPushButton::clicked, [this]() { saveCard(&m_card_a); });
  connect(m_ui.saveCardB, &QPushButton::clicked, [this]() { saveCard(&m_card_b); });
  connect(m_ui.importCardA, &QPushButton::clicked, [this]() { importCard(&m_card_a); });
  connect(m_ui.importCardB, &QPushButton::clicked, [this]() { importCard(&m_card_b); });
}

void MemoryCardEditorDialog::populateComboBox(QComboBox* cb)
{
  QSignalBlocker sb(cb);

  cb->clear();

  cb->addItem(QString());
  cb->addItem(tr("Browse..."));

  const std::string base_path(g_host_interface->GetUserDirectoryRelativePath("memcards"));
  FileSystem::FindResultsArray results;
  FileSystem::FindFiles(base_path.c_str(), "*.mcd", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_RELATIVE_PATHS, &results);
  for (FILESYSTEM_FIND_DATA& fd : results)
  {
    std::string real_filename(
      StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s", base_path.c_str(), fd.FileName.c_str()));
    std::string::size_type pos = fd.FileName.rfind('.');
    if (pos != std::string::npos)
      fd.FileName.erase(pos);

    cb->addItem(QString::fromStdString(fd.FileName), QVariant(QString::fromStdString(real_filename)));
  }
}

void MemoryCardEditorDialog::loadCardFromComboBox(Card* card, int index)
{
  QString filename;
  if (index == 1)
  {
    filename = QFileDialog::getOpenFileName(this, tr("Select Memory Card"), QString(), tr(MEMORY_CARD_IMAGE_FILTER));
    if (!filename.isEmpty())
    {
      // add to combo box
      QFileInfo file(filename);
      QSignalBlocker sb(card->path_cb);
      card->path_cb->addItem(file.baseName(), QVariant(filename));
      card->path_cb->setCurrentIndex(card->path_cb->count() - 1);
    }
  }
  else
  {
    filename = card->path_cb->itemData(index).toString();
  }

  if (filename.isEmpty())
    return;

  loadCard(filename, card);
}

void MemoryCardEditorDialog::onCardASelectionChanged()
{
  {
    QSignalBlocker cb(m_card_b.table);
    m_card_b.table->clearSelection();
  }

  updateButtonState();
}

void MemoryCardEditorDialog::onCardBSelectionChanged()
{
  {
    QSignalBlocker cb(m_card_a.table);
    m_card_a.table->clearSelection();
  }

  updateButtonState();
}

void MemoryCardEditorDialog::clearSelection()
{
  {
    QSignalBlocker cb(m_card_a.table);
    m_card_a.table->clearSelection();
  }

  {
    QSignalBlocker cb(m_card_b.table);
    m_card_b.table->clearSelection();
  }

  updateButtonState();
}

bool MemoryCardEditorDialog::loadCard(const QString& filename, Card* card)
{
  promptForSave(card);

  card->table->setRowCount(0);
  card->dirty = false;
  card->blocks_free_label->clear();
  card->save_button->setEnabled(false);

  card->filename.clear();

  std::string filename_str = filename.toStdString();
  if (!MemoryCardImage::LoadFromFile(&card->data, filename_str.c_str()))
  {
    QMessageBox::critical(this, tr("Error"), tr("Failed to load memory card image."));
    return false;
  }

  card->filename = std::move(filename_str);
  updateCardTable(card);
  updateCardBlocksFree(card);
  updateButtonState();
  return true;
}

void MemoryCardEditorDialog::updateCardTable(Card* card)
{
  card->table->setRowCount(0);

  card->files = MemoryCardImage::EnumerateFiles(card->data);
  for (const MemoryCardImage::FileInfo& fi : card->files)
  {
    const int row = card->table->rowCount();
    card->table->insertRow(row);

    if (!fi.icon_frames.empty())
    {
      const QImage image(reinterpret_cast<const u8*>(fi.icon_frames[0].pixels), MemoryCardImage::ICON_WIDTH,
                         MemoryCardImage::ICON_HEIGHT, QImage::Format_RGBA8888);

      QTableWidgetItem* icon = new QTableWidgetItem();
      icon->setIcon(QIcon(QPixmap::fromImage(image)));
      card->table->setItem(row, 0, icon);
    }

    card->table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(fi.title)));
    card->table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(fi.filename)));
    card->table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1").arg(fi.num_blocks)));
  }
}

void MemoryCardEditorDialog::updateCardBlocksFree(Card* card)
{
  card->blocks_free = MemoryCardImage::GetFreeBlockCount(card->data);
  card->blocks_free_label->setText(
    tr("%1 blocks free%2").arg(card->blocks_free).arg(card->dirty ? QStringLiteral(" (*)") : QString()));
}

void MemoryCardEditorDialog::setCardDirty(Card* card)
{
  card->dirty = true;
  card->save_button->setEnabled(true);
}

void MemoryCardEditorDialog::newCard(Card* card)
{
  promptForSave(card);

  QString filename =
    QFileDialog::getSaveFileName(this, tr("Select Memory Card"), QString(), tr(MEMORY_CARD_IMAGE_FILTER));
  if (filename.isEmpty())
    return;

  {
    // add to combo box
    QFileInfo file(filename);
    QSignalBlocker sb(card->path_cb);
    card->path_cb->addItem(file.baseName(), QVariant(filename));
    card->path_cb->setCurrentIndex(card->path_cb->count() - 1);
  }

  card->filename = filename.toStdString();

  MemoryCardImage::Format(&card->data);
  updateCardTable(card);
  updateCardBlocksFree(card);
  updateButtonState();
  saveCard(card);
}

void MemoryCardEditorDialog::saveCard(Card* card)
{
  if (card->filename.empty())
    return;

  if (!MemoryCardImage::SaveToFile(card->data, card->filename.c_str()))
  {
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to write card to '%1'").arg(QString::fromStdString(card->filename)));
    return;
  }

  card->dirty = false;
  card->save_button->setEnabled(false);
  updateCardBlocksFree(card);
}

void MemoryCardEditorDialog::promptForSave(Card* card)
{
  if (card->filename.empty() || !card->dirty)
    return;

  if (QMessageBox::question(this, tr("Save memory card?"),
                            tr("Memory card '%1' is not saved, do you want to save before closing?")
                              .arg(QString::fromStdString(card->filename)),
                            QMessageBox::Yes, QMessageBox::No) == QMessageBox::No)
  {
    return;
  }

  saveCard(card);
}

void MemoryCardEditorDialog::doCopyFile()
{
  const auto [src, fi] = getSelectedFile();
  if (!fi)
    return;

  Card* dst = (src == &m_card_a) ? &m_card_b : &m_card_a;

  if (dst->blocks_free < fi->num_blocks)
  {
    QMessageBox::critical(this, tr("Error"),
                          tr("Insufficient blocks, this file needs %1 but only %2 are available.")
                            .arg(fi->num_blocks)
                            .arg(dst->blocks_free));
    return;
  }

  std::vector<u8> buffer;
  if (!MemoryCardImage::ReadFile(src->data, *fi, &buffer))
  {
    QMessageBox::critical(this, tr("Error"), tr("Failed to read file %1").arg(QString::fromStdString(fi->filename)));
    return;
  }

  if (!MemoryCardImage::WriteFile(&dst->data, fi->filename, buffer))
  {
    QMessageBox::critical(this, tr("Error"), tr("Failed to write file %1").arg(QString::fromStdString(fi->filename)));
    return;
  }

  clearSelection();
  updateCardTable(dst);
  updateCardBlocksFree(dst);
  setCardDirty(dst);
  updateButtonState();
}

void MemoryCardEditorDialog::doDeleteFile()
{
  const auto [card, fi] = getSelectedFile();
  if (!fi)
    return;

  if (!MemoryCardImage::DeleteFile(&card->data, *fi))
  {
    QMessageBox::critical(this, tr("Error"), tr("Failed to delete file %1").arg(QString::fromStdString(fi->filename)));
    return;
  }

  clearSelection();
  updateCardTable(card);
  updateCardBlocksFree(card);
  setCardDirty(card);
  updateButtonState();
}

void MemoryCardEditorDialog::importCard(Card* card)
{
  promptForSave(card);

  QString filename =
    QFileDialog::getOpenFileName(this, tr("Select Import File"), QString(), tr(MEMORY_CARD_IMPORT_FILTER));
  if (filename.isEmpty())
    return;

  std::unique_ptr<MemoryCardImage::DataArray> temp = std::make_unique<MemoryCardImage::DataArray>();
  if (!MemoryCardImage::ImportCard(temp.get(), filename.toStdString().c_str()))
  {
    QMessageBox::critical(this, tr("Error"), tr("Failed to import memory card. The log may contain more information."));
    return;
  }

  clearSelection();

  card->data = *temp;
  updateCardTable(card);
  updateCardBlocksFree(card);
  setCardDirty(card);
  updateButtonState();
}

std::tuple<MemoryCardEditorDialog::Card*, const MemoryCardImage::FileInfo*> MemoryCardEditorDialog::getSelectedFile()
{
  QList<QTableWidgetSelectionRange> sel = m_card_a.table->selectedRanges();
  Card* card = &m_card_a;

  if (sel.isEmpty())
  {
    sel = m_card_b.table->selectedRanges();
    card = &m_card_b;
  }

  if (sel.isEmpty())
    return std::tuple<Card*, const MemoryCardImage::FileInfo*>(nullptr, nullptr);

  const int index = sel.front().topRow();
  Assert(index >= 0 && static_cast<u32>(index) < card->files.size());

  return std::tuple<Card*, const MemoryCardImage::FileInfo*>(card, &card->files[index]);
}

void MemoryCardEditorDialog::updateButtonState()
{
  const auto [selected_card, selected_file] = getSelectedFile();
  const bool is_card_b = (selected_card == &m_card_b);
  const bool has_selection = (selected_file != nullptr);
  const bool card_a_present = !m_card_a.filename.empty();
  const bool card_b_present = !m_card_b.filename.empty();
  const bool both_cards_present = card_a_present && card_b_present;
  m_ui.deleteFile->setEnabled(has_selection);
  m_ui.exportFile->setEnabled(has_selection);
  m_ui.moveLeft->setEnabled(both_cards_present && has_selection && is_card_b);
  m_ui.moveRight->setEnabled(both_cards_present && has_selection && !is_card_b);
  m_ui.importCardA->setEnabled(card_a_present);
  m_ui.importCardB->setEnabled(card_b_present);
}
