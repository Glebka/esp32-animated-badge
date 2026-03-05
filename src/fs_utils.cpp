#include <FS.h>
#if USE_LITTLEFS
    #include <LittleFS.h>
#else
    #include <SD_MMC.h>
#endif
#include "hw_config.hpp"
#include "fs_utils.hpp"

static const char *FILESYSTEM_ROOT_PATH = "/";

esp_err_t init_fs()
{
#if USE_LITTLEFS
  if (!LittleFS.begin(false))
  {
    return ESP_FAIL;
  }
#else
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  if (!SD_MMC.begin("/sdcard", true))
  {
    return ESP_FAIL;
  }
#endif
  return ESP_OK;
}

fs::FS &get_media_fs()
{
#if USE_LITTLEFS
  return LittleFS;
#else
  return SD_MMC;
#endif
}

static size_t s_next_supported_index = 0;

bool has_supported_extension(const char *name)
{
  const char *ext = strrchr(name, '.');
  if (ext == NULL || ext[1] == '\0')
  {
    return false;
  }

  ext++;
  char lower_ext[6] = {0};
  size_t i = 0;
  while (ext[i] != '\0' && i < (sizeof(lower_ext) - 1))
  {
    lower_ext[i] = (char)tolower((unsigned char)ext[i]);
    i++;
  }
  lower_ext[i] = '\0';

  return (strcmp(lower_ext, "gif") == 0) ||
        //  (strcmp(lower_ext, "jpg") == 0) ||
         (strcmp(lower_ext, "png") == 0);// ||
        //  (strcmp(lower_ext, "jpeg") == 0);
}

supported_file_type_t get_file_type(const char *name)
{
  const char *ext = strrchr(name, '.');
  if (ext == NULL || ext[1] == '\0')
  {
    return FILE_TYPE_SZ_COUNT;
  }

  ext++;
  char lower_ext[6] = {0};
  size_t i = 0;
  while (ext[i] != '\0' && i < (sizeof(lower_ext) - 1))
  {
    lower_ext[i] = (char)tolower((unsigned char)ext[i]);
    i++;
  }
  lower_ext[i] = '\0';

  if (strcmp(lower_ext, "gif") == 0)
  {
    return FILE_TYPE_GIF;
  }
  // else if ((strcmp(lower_ext, "jpg") == 0) || (strcmp(lower_ext, "jpeg") == 0))
  // {
  //   return FILE_TYPE_JPEG;
  // }
  else if (strcmp(lower_ext, "png") == 0)
  {
    return FILE_TYPE_PNG;
  }
  else
  {
    return FILE_TYPE_SZ_COUNT;
  }
}

esp_err_t find_next_supported_file(char *out_path, size_t out_path_size)
{
  if (out_path == NULL || out_path_size == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  fs::FS &filesystem = get_media_fs();
  File dir = filesystem.open(FILESYSTEM_ROOT_PATH);
  if (!dir || !dir.isDirectory())
  {
    return ESP_ERR_NOT_FOUND;
  }

  size_t supported_count = 0;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile())
  {
    const char *name = entry.name();
    const char *base_name = strrchr(name, '/');
    base_name = (base_name != NULL) ? (base_name + 1) : name;

    if (base_name[0] == '.')
    {
      continue;
    }
    if (!entry.isDirectory() && has_supported_extension(base_name))
    {
      supported_count++;
    }
  }

  if (supported_count == 0)
  {
    dir.close();
    return ESP_ERR_NOT_FOUND;
  }

  size_t target_index = s_next_supported_index % supported_count;
  dir.rewindDirectory();

  size_t current_index = 0;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile())
  {
    const char *name = entry.name();
    const char *base_name = strrchr(name, '/');
    base_name = (base_name != NULL) ? (base_name + 1) : name;

    if (base_name[0] == '.')
    {
      continue;
    }
    if (entry.isDirectory() || !has_supported_extension(base_name))
    {
      continue;
    }

    if (current_index == target_index)
    {
      int written = snprintf(out_path, out_path_size, "/%s", name);
      dir.close();

      if (written <= 0 || (size_t)written >= out_path_size)
      {
        return ESP_ERR_INVALID_SIZE;
      }

      s_next_supported_index = (target_index + 1) % supported_count;
      return ESP_OK;
    }

    current_index++;
  }

  dir.close();
  return ESP_ERR_NOT_FOUND;
}

esp_err_t reset_next_supported_file_iterator() {
    s_next_supported_index = 0;
    return ESP_OK;
}