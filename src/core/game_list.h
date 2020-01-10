#pragma once
#include "types.h"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class CDImage;
class ByteStream;

class SettingsInterface;

class GameList
{
public:
  struct GameDatabaseEntry
  {
    std::string code;
    std::string title;
    ConsoleRegion region;
  };

  using DatabaseMap = std::unordered_map<std::string, GameDatabaseEntry>;

  enum class EntryType
  {
    Disc,
    PSExe
  };

  struct GameListEntry
  {
    std::string path;
    std::string code;
    std::string title;
    u64 total_size;
    u64 last_modified_time;
    ConsoleRegion region;
    EntryType type;
  };

  using EntryList = std::vector<GameListEntry>;
  using CacheMap = std::unordered_map<std::string, GameListEntry>;

  GameList();
  ~GameList();

  static const char* EntryTypeToString(EntryType type);

  /// Returns true if the filename is a PlayStation executable we can inject.
  static bool IsExeFileName(const char* path);

  static std::string GetGameCodeForImage(CDImage* cdi);
  static std::string GetGameCodeForPath(const char* image_path);
  static std::optional<ConsoleRegion> GetRegionForCode(std::string_view code);
  static std::optional<ConsoleRegion> GetRegionFromSystemArea(CDImage* cdi);
  static std::optional<ConsoleRegion> GetRegionForImage(CDImage* cdi);
  static std::optional<ConsoleRegion> GetRegionForPath(const char* image_path);

  const DatabaseMap& GetDatabase() const { return m_database; }
  const EntryList& GetEntries() const { return m_entries; }
  const u32 GetEntryCount() const { return static_cast<u32>(m_entries.size()); }

  void SetPathsFromSettings(SettingsInterface& si);
  void AddDirectory(std::string path, bool recursive);
  void Refresh(bool invalidate_cache, bool invalidate_database);

private:
  enum : u32
  {
    GAME_LIST_CACHE_SIGNATURE = 0x45434C47,
    GAME_LIST_CACHE_VERSION = 2
  };

  struct DirectoryEntry
  {
    std::string path;
    bool recursive;
  };

  static bool GetExeListEntry(const char* path, GameListEntry* entry);

  bool GetGameListEntry(const std::string& path, GameListEntry* entry);
  bool GetGameListEntryFromCache(const std::string& path, GameListEntry* entry);
  void ScanDirectory(const char* path, bool recursive);

  void LoadCache();
  bool LoadEntriesFromCache(ByteStream* stream);
  bool OpenCacheForWriting();
  bool WriteEntryToCache(const GameListEntry* entry, ByteStream* stream);
  void CloseCacheFileStream();
  void DeleteCacheFile();

  void LoadDatabase();
  void ClearDatabase();

  DatabaseMap m_database;
  EntryList m_entries;
  CacheMap m_cache_map;
  std::unique_ptr<ByteStream> m_cache_write_stream;

  std::vector<DirectoryEntry> m_search_directories;
  std::string m_cache_filename;
  std::string m_database_filename;
  bool m_database_load_tried = false;
};
