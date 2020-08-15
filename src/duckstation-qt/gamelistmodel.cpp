#include "gamelistmodel.h"
#include "common/string_util.h"

static constexpr std::array<const char*, GameListModel::Column_Count> s_column_names = {
  {"Type", "Code", "Title", "File Title", "Size", "Region", "Compatibility"}};

std::optional<GameListModel::Column> GameListModel::getColumnIdForName(std::string_view name)
{
  for (int column = 0; column < Column_Count; column++)
  {
    if (name == s_column_names[column])
      return static_cast<Column>(column);
  }

  return std::nullopt;
}

const char* GameListModel::getColumnName(Column col)
{
  return s_column_names[static_cast<int>(col)];
}

GameListModel::GameListModel(GameList* game_list, QObject* parent /* = nullptr */)
  : QAbstractTableModel(parent), m_game_list(game_list)
{
  loadCommonImages();
  setColumnDisplayNames();
}
GameListModel::~GameListModel() = default;

int GameListModel::rowCount(const QModelIndex& parent) const
{
  if (parent.isValid())
    return 0;

  return static_cast<int>(m_game_list->GetEntryCount());
}

int GameListModel::columnCount(const QModelIndex& parent) const
{
  if (parent.isValid())
    return 0;

  return Column_Count;
}

QVariant GameListModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid())
    return {};

  const int row = index.row();
  if (row < 0 || row >= static_cast<int>(m_game_list->GetEntryCount()))
    return {};

  const GameListEntry& ge = m_game_list->GetEntries()[row];

  switch (role)
  {
    case Qt::DisplayRole:
    {
      switch (index.column())
      {
        case Column_Code:
          return QString::fromStdString(ge.code);

        case Column_Title:
          return QString::fromStdString(ge.title);

        case Column_FileTitle:
        {
          const std::string_view file_title(GameList::GetTitleForPath(ge.path.c_str()));
          return QString::fromUtf8(file_title.data(), static_cast<int>(file_title.length()));
        }

        case Column_Size:
          return QString("%1 MB").arg(static_cast<double>(ge.total_size) / 1048576.0, 0, 'f', 2);

        default:
          return {};
      }
    }

    case Qt::InitialSortOrderRole:
    {
      switch (index.column())
      {
        case Column_Type:
          return static_cast<int>(ge.type);

        case Column_Code:
          return QString::fromStdString(ge.code);

        case Column_Title:
          return QString::fromStdString(ge.title);

        case Column_FileTitle:
        {
          const std::string_view file_title(GameList::GetTitleForPath(ge.path.c_str()));
          return QString::fromUtf8(file_title.data(), static_cast<int>(file_title.length()));
        }

        case Column_Region:
          return static_cast<int>(ge.region);

        case Column_Compatibility:
          return static_cast<int>(ge.compatibility_rating);

        case Column_Size:
          return static_cast<qulonglong>(ge.total_size);

        default:
          return {};
      }
    }

    case Qt::DecorationRole:
    {
      switch (index.column())
      {
        case Column_Type:
        {
          switch (ge.type)
          {
            case GameListEntryType::Disc:
              return m_type_disc_pixmap;
            case GameListEntryType::Playlist:
              return m_type_playlist_pixmap;
            case GameListEntryType::PSExe:
            default:
              return m_type_exe_pixmap;
          }
        }

        case Column_Region:
        {
          switch (ge.region)
          {
            case DiscRegion::NTSC_J:
              return m_region_jp_pixmap;
            case DiscRegion::NTSC_U:
              return m_region_us_pixmap;
            case DiscRegion::PAL:
            default:
              return m_region_eu_pixmap;
          }
        }

        case Column_Compatibility:
        {
          return m_compatibiliy_pixmaps[static_cast<int>(
            (ge.compatibility_rating >= GameListCompatibilityRating::Count) ? GameListCompatibilityRating::Unknown :
                                                                              ge.compatibility_rating)];
        }

        default:
          return {};
      }

      default:
        return {};
    }
  }
}

QVariant GameListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= Column_Count)
    return {};

  return m_column_display_names[section];
}

void GameListModel::refresh()
{
  beginResetModel();
  endResetModel();
}

bool GameListModel::titlesLessThan(int left_row, int right_row, bool ascending) const
{
  if (left_row < 0 || left_row >= static_cast<int>(m_game_list->GetEntryCount()) || right_row < 0 ||
      right_row >= static_cast<int>(m_game_list->GetEntryCount()))
  {
    return false;
  }

  const GameListEntry& left = m_game_list->GetEntries().at(left_row);
  const GameListEntry& right = m_game_list->GetEntries().at(right_row);
  return ascending ? (left.title < right.title) : (right.title < left.title);
}

bool GameListModel::lessThan(const QModelIndex& left_index, const QModelIndex& right_index, int column,
                             bool ascending) const
{
  if (!left_index.isValid() || !right_index.isValid())
    return false;

  const int left_row = left_index.row();
  const int right_row = right_index.row();
  if (left_row < 0 || left_row >= static_cast<int>(m_game_list->GetEntryCount()) || right_row < 0 ||
      right_row >= static_cast<int>(m_game_list->GetEntryCount()))
  {
    return false;
  }

  const GameListEntry& left = m_game_list->GetEntries()[left_row];
  const GameListEntry& right = m_game_list->GetEntries()[right_row];
  switch (column)
  {
    case Column_Type:
    {
      if (left.type == right.type)
        return titlesLessThan(left_row, right_row, ascending);

      return ascending ? (static_cast<int>(left.type) < static_cast<int>(right.type)) :
                         (static_cast<int>(right.type) > static_cast<int>(left.type));
    }

    case Column_Code:
    {
      if (left.code == right.code)
        return titlesLessThan(left_row, right_row, ascending);
      return ascending ? (left.code < right.code) : (right.code > left.code);
    }

    case Column_Title:
    {
      if (left.title == right.title)
        return titlesLessThan(left_row, right_row, ascending);

      return ascending ? (left.title < right.title) : (right.title > left.title);
    }

    case Column_FileTitle:
    {
      const std::string_view file_title_left(GameList::GetTitleForPath(left.path.c_str()));
      const std::string_view file_title_right(GameList::GetTitleForPath(right.path.c_str()));
      if (file_title_left == file_title_right)
        return titlesLessThan(left_row, right_row, ascending);

      return ascending ? (file_title_left < file_title_right) : (file_title_right > file_title_left);
    }

    case Column_Region:
    {
      if (left.region == right.region)
        return titlesLessThan(left_row, right_row, ascending);
      return ascending ? (static_cast<int>(left.region) < static_cast<int>(right.region)) :
                         (static_cast<int>(right.region) > static_cast<int>(left.region));
    }

    case Column_Compatibility:
    {
      if (left.compatibility_rating == right.compatibility_rating)
        return titlesLessThan(left_row, right_row, ascending);

      return ascending ? (static_cast<int>(left.compatibility_rating) < static_cast<int>(right.compatibility_rating)) :
                         (static_cast<int>(right.compatibility_rating) > static_cast<int>(left.compatibility_rating));
    }

    case Column_Size:
    {
      if (left.total_size == right.total_size)
        return titlesLessThan(left_row, right_row, ascending);

      return ascending ? (left.total_size < right.total_size) : (right.total_size > left.total_size);
    }

    default:
      return false;
  }
}

void GameListModel::loadCommonImages()
{
  // TODO: Use svg instead of png
  m_type_disc_pixmap.load(QStringLiteral(":/icons/media-optical-24.png"));
  m_type_exe_pixmap.load(QStringLiteral(":/icons/applications-system-24.png"));
  m_type_playlist_pixmap.load(QStringLiteral(":/icons/address-book-new-22.png"));
  m_region_eu_pixmap.load(QStringLiteral(":/icons/flag-eu.png"));
  m_region_jp_pixmap.load(QStringLiteral(":/icons/flag-jp.png"));
  m_region_us_pixmap.load(QStringLiteral(":/icons/flag-us.png"));
  m_region_eu_pixmap.load(QStringLiteral(":/icons/flag-eu.png"));

  for (int i = 0; i < static_cast<int>(GameListCompatibilityRating::Count); i++)
    m_compatibiliy_pixmaps[i].load(QStringLiteral(":/icons/star-%1.png").arg(i));
}

void GameListModel::setColumnDisplayNames()
{
  m_column_display_names[Column_Type] = tr("Type");
  m_column_display_names[Column_Code] = tr("Code");
  m_column_display_names[Column_Title] = tr("Title");
  m_column_display_names[Column_FileTitle] = tr("File Title");
  m_column_display_names[Column_Size] = tr("Size");
  m_column_display_names[Column_Region] = tr("Region");
  m_column_display_names[Column_Compatibility] = tr("Compatibility");
}
