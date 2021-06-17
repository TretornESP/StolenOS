/////////////////////////////////////////////////////////////////////////
// $Id: plugin.cc 14073 2021-01-08 21:17:47Z vruppert $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2002-2021  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////
//
// This file defines the plugin and plugin-device registration functions and
// the device registration functions.  It handles dynamic loading of modules,
// using the LTDL library for cross-platform support.
//
// This file is based on the plugin.c file from plex86, but with significant
// changes to make it work in Bochs.
// Plex86 is Copyright (C) 1999-2000  The plex86 developers team
//
/////////////////////////////////////////////////////////////////////////

#include "bochs.h"
#include "iodev/iodev.h"
#include "plugin.h"

#ifndef WIN32
#include <dirent.h> /* opendir, readdir */
#include <locale.h> /* setlocale */
#endif

#define LOG_THIS genlog->

#define PLUGIN_INIT_FMT_STRING       "lib%s_LTX_plugin_init"
#define PLUGIN_FINI_FMT_STRING       "lib%s_LTX_plugin_fini"
#define GUI_PLUGIN_INIT_FMT_STRING   "lib%s_gui_plugin_init"
#define GUI_PLUGIN_FINI_FMT_STRING   "lib%s_gui_plugin_fini"
#define SOUND_PLUGIN_INIT_FMT_STRING "lib%s_sound_plugin_init"
#define SOUND_PLUGIN_FINI_FMT_STRING "lib%s_sound_plugin_fini"
#define NET_PLUGIN_INIT_FMT_STRING   "lib%s_net_plugin_init"
#define NET_PLUGIN_FINI_FMT_STRING   "lib%s_net_plugin_fini"
#define USB_PLUGIN_INIT_FMT_STRING   "lib%s_dev_plugin_init"
#define USB_PLUGIN_FINI_FMT_STRING   "lib%s_dev_plugin_fini"
#define IMG_PLUGIN_INIT_FMT_STRING   "lib%s_img_plugin_init"
#define IMG_PLUGIN_FINI_FMT_STRING   "lib%s_img_plugin_fini"
#define PLUGIN_PATH                  ""

#ifndef WIN32
#define PLUGIN_FILENAME_FORMAT       "libbx_%s.so"
#define GUI_PLUGIN_FILENAME_FORMAT   "libbx_%s_gui.so"
#define IMG_PLUGIN_FILENAME_FORMAT   "libbx_%s_img.so"
#define NET_PLUGIN_FILENAME_FORMAT   "libbx_eth_%s.so"
#define SND_PLUGIN_FILENAME_FORMAT   "libbx_sound%s.so"
#else
#define PLUGIN_FILENAME_FORMAT       "bx_%s.dll"
#define GUI_PLUGIN_FILENAME_FORMAT   "bx_%s_gui.dll"
#define IMG_PLUGIN_FILENAME_FORMAT   "bx_%s_img.dll"
#define NET_PLUGIN_FILENAME_FORMAT   "bx_eth_%s.dll"
#define SND_PLUGIN_FILENAME_FORMAT "bx_sound%s.dll"
#endif

logfunctions *pluginlog;

extern "C" {

void  (*pluginRegisterIRQ)(unsigned irq, const char* name) = 0;
void  (*pluginUnregisterIRQ)(unsigned irq, const char* name) = 0;

void (*pluginSetHRQ)(unsigned val) = 0;
void (*pluginSetHRQHackCallback)(void (*callback)(void)) = 0;

int (*pluginRegisterIOReadHandler)(void *thisPtr, ioReadHandler_t callback,
                            unsigned base, const char *name, Bit8u mask) = 0;
int (*pluginRegisterIOWriteHandler)(void *thisPtr, ioWriteHandler_t callback,
                             unsigned base, const char *name, Bit8u mask) = 0;
int (*pluginUnregisterIOReadHandler)(void *thisPtr, ioReadHandler_t callback,
                            unsigned base, Bit8u mask) = 0;
int (*pluginUnregisterIOWriteHandler)(void *thisPtr, ioWriteHandler_t callback,
                             unsigned base, Bit8u mask) = 0;
int (*pluginRegisterIOReadHandlerRange)(void *thisPtr, ioReadHandler_t callback,
                            unsigned base, unsigned end, const char *name, Bit8u mask) = 0;
int (*pluginRegisterIOWriteHandlerRange)(void *thisPtr, ioWriteHandler_t callback,
                             unsigned base, unsigned end, const char *name, Bit8u mask) = 0;
int (*pluginUnregisterIOReadHandlerRange)(void *thisPtr, ioReadHandler_t callback,
                            unsigned begin, unsigned end, Bit8u mask) = 0;
int (*pluginUnregisterIOWriteHandlerRange)(void *thisPtr, ioWriteHandler_t callback,
                             unsigned begin, unsigned end, Bit8u mask) = 0;
int (*pluginRegisterDefaultIOReadHandler)(void *thisPtr, ioReadHandler_t callback,
                            const char *name, Bit8u mask) = 0;
int (*pluginRegisterDefaultIOWriteHandler)(void *thisPtr, ioWriteHandler_t callback,
                             const char *name, Bit8u mask) = 0;

void (*pluginHRQHackCallback)(void);
unsigned pluginHRQ = 0;

plugin_t *plugins = NULL;      /* Head of the linked list of plugins  */
#if BX_PLUGINS
static void plugin_init_one(plugin_t *plugin);
#endif

device_t *devices = NULL;      /* Head of the linked list of registered devices  */
device_t *core_devices = NULL; /* Head of the linked list of registered core devices  */

plugin_t *current_plugin_context = NULL;

/************************************************************************/
/* Builtins declarations                                                */
/************************************************************************/

  static void
builtinRegisterIRQ(unsigned irq, const char* name)
{
  bx_devices.register_irq(irq, name);
}

  static void
builtinUnregisterIRQ(unsigned irq, const char* name)
{
  bx_devices.unregister_irq(irq, name);
}

  static void
builtinSetHRQ(unsigned val)
{
  pluginHRQ = val;
}

  static void
builtinSetHRQHackCallback(void (*callback)(void))
{
  pluginHRQHackCallback = callback;
}

  static int
builtinRegisterIOReadHandler(void *thisPtr, ioReadHandler_t callback,
                            unsigned base, const char *name, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.register_io_read_handler (thisPtr, callback, base, name, mask);
  pluginlog->ldebug("plugin %s registered I/O read  address at %04x", name, base);
  return ret;
}

  static int
builtinRegisterIOWriteHandler(void *thisPtr, ioWriteHandler_t callback,
                             unsigned base, const char *name, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.register_io_write_handler (thisPtr, callback, base, name, mask);
  pluginlog->ldebug("plugin %s registered I/O write address at %04x", name, base);
  return ret;
}

  static int
builtinUnregisterIOReadHandler(void *thisPtr, ioReadHandler_t callback,
                            unsigned base, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.unregister_io_read_handler (thisPtr, callback, base, mask);
  pluginlog->ldebug("plugin unregistered I/O read address at %04x", base);
  return ret;
}

  static int
builtinUnregisterIOWriteHandler(void *thisPtr, ioWriteHandler_t callback,
                             unsigned base, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.unregister_io_write_handler (thisPtr, callback, base, mask);
  pluginlog->ldebug("plugin unregistered I/O write address at %04x", base);
  return ret;
}

  static int
builtinRegisterIOReadHandlerRange(void *thisPtr, ioReadHandler_t callback,
                            unsigned base, unsigned end, const char *name, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.register_io_read_handler_range (thisPtr, callback, base, end, name, mask);
  pluginlog->ldebug("plugin %s registered I/O read addresses %04x to %04x", name, base, end);
  return ret;
}

  static int
builtinRegisterIOWriteHandlerRange(void *thisPtr, ioWriteHandler_t callback,
                             unsigned base, unsigned end, const char *name, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.register_io_write_handler_range (thisPtr, callback, base, end, name, mask);
  pluginlog->ldebug("plugin %s registered I/O write addresses %04x to %04x", name, base, end);
  return ret;
}

  static int
builtinUnregisterIOReadHandlerRange(void *thisPtr, ioReadHandler_t callback,
                            unsigned begin, unsigned end, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.unregister_io_read_handler_range (thisPtr, callback, begin, end, mask);
  pluginlog->ldebug("plugin unregistered I/O read addresses %04x to %04x", begin, end);
  return ret;
}

  static int
builtinUnregisterIOWriteHandlerRange(void *thisPtr, ioWriteHandler_t callback,
                             unsigned begin, unsigned end, Bit8u mask)
{
  int ret;
  BX_ASSERT(mask<8);
  ret = bx_devices.unregister_io_write_handler_range (thisPtr, callback, begin, end, mask);
  pluginlog->ldebug("plugin unregistered I/O write addresses %04x to %04x", begin, end);
  return ret;
}

  static int
builtinRegisterDefaultIOReadHandler(void *thisPtr, ioReadHandler_t callback,
                            const char *name, Bit8u mask)
{
  BX_ASSERT(mask<8);
  bx_devices.register_default_io_read_handler (thisPtr, callback, name, mask);
  pluginlog->ldebug("plugin %s registered default I/O read ", name);
  return 0;
}

  static int
builtinRegisterDefaultIOWriteHandler(void *thisPtr, ioWriteHandler_t callback,
                             const char *name, Bit8u mask)
{
  BX_ASSERT(mask<8);
  bx_devices.register_default_io_write_handler (thisPtr, callback, name, mask);
  pluginlog->ldebug("plugin %s registered default I/O write ", name);
  return 0;
}

#if BX_PLUGINS
/************************************************************************/
/* Search for all available plugins                                           */
/************************************************************************/

void plugin_add_entry(char *pgn_name)
{
  plugin_t *plugin, *temp;
  plugintype_t type;

  if (!strncmp(pgn_name, "eth_", 4)) {
    memmove(pgn_name, pgn_name + 4, strlen(pgn_name) - 3);
    type = PLUGTYPE_NET;
  } else if (!strncmp(pgn_name, "sound", 5)) {
    memmove(pgn_name, pgn_name + 5, strlen(pgn_name) - 4);
    type = PLUGTYPE_SND;
  } else if (!strncmp(pgn_name + strlen(pgn_name) - 4, "_gui", 4)) {
    pgn_name[strlen(pgn_name) - 4] = 0;
    type = PLUGTYPE_GUI;
  } else if (!strncmp(pgn_name + strlen(pgn_name) - 4, "_img", 4)) {
    pgn_name[strlen(pgn_name) - 4] = 0;
    type = PLUGTYPE_IMG;
  } else if (!strncmp(pgn_name, "usb_", 4) &&
             strncmp(pgn_name + strlen(pgn_name) - 3, "hci", 3)) {
    type = PLUGTYPE_USB;
  } else {
    type = PLUGTYPE_DEV;
  }
  plugin = new plugin_t;
  plugin->type = type;
  plugin->name = pgn_name;
  plugin->loaded = 0;
  plugin->initialized = 0;
  /* Insert plugin at the _end_ of the plugin linked list. */
  plugin->next = NULL;
  if (plugins == NULL) {
    /* Empty list, this become the first entry. */
    plugins = plugin;
  } else {
    /* Non-empty list.  Add to end. */
    temp = plugins;

    while (temp->next)
      temp = temp->next;

    temp->next = plugin;
  }
}

void plugins_search(void)
{
  int nlen, flen1, flen2;
  char *fmtptr, *ltdl_path_var, *pgn_name, *pgn_path, *ptr;
  char fmtstr[32];
#ifndef WIN32
  const char *path_sep = ":";
  DIR *dir;
  struct dirent *dent;
#else
  const char *path_sep = ";";
  WIN32_FIND_DATA finddata;
  HANDLE hFind;
  char filter[MAX_PATH];
#endif

#ifndef WIN32
  setlocale(LC_ALL, "en_US");
#endif
  ltdl_path_var = getenv("LTDL_LIBRARY_PATH");
  sprintf(fmtstr, PLUGIN_FILENAME_FORMAT, "*");
  fmtptr = strchr(fmtstr, '*');
  flen1 = fmtptr - fmtstr;
  flen2 = strlen(fmtstr) - flen1 - 1;
  if (ltdl_path_var != NULL) {
    pgn_path = new char[strlen(ltdl_path_var) + 1];
    strcpy(pgn_path, ltdl_path_var);
  } else {
  pgn_path = new char[2];
    strcpy(pgn_path, ".");
  }
  ptr = strtok(pgn_path, path_sep);
  while (ptr != NULL) {
#ifndef WIN32
    dir = opendir(ptr);
    if (dir != NULL) {
      while ((dent=readdir(dir)) != NULL) {
        nlen = strlen(dent->d_name);
        if ((!strncmp(dent->d_name, fmtstr, flen1)) &&
            (!strcmp(dent->d_name + nlen - flen2, fmtptr + 1))) {
          pgn_name = new char[nlen - flen1 - flen2 + 1];
          strncpy(pgn_name, dent->d_name + flen1, nlen - flen1 - flen2);
          pgn_name[nlen - flen1 - flen2] = 0;
          plugin_add_entry(pgn_name);
        }
      }
      closedir(dir);
    }
#else
    sprintf(filter, "%s\\*.dll", ptr);
    hFind = FindFirstFile(filter, &finddata);
    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        nlen = lstrlen(finddata.cFileName);
        if ((!strncmp(finddata.cFileName, fmtstr, flen1)) &&
            (!strcmp(finddata.cFileName + nlen - flen2, fmtptr + 1))) {
          pgn_name = new char[nlen - flen1 - flen2 + 1];
          strncpy(pgn_name, finddata.cFileName + flen1, nlen - flen1 - flen2);
          pgn_name[nlen - flen1 - flen2] = 0;
          plugin_add_entry(pgn_name);
        }
      } while (FindNextFile(hFind, &finddata));
      FindClose(hFind);
    }
#endif
    ptr = strtok(NULL, ":");
  }
  delete [] pgn_path;
}

Bit8u bx_get_plugins_count(plugintype_t type)
{
  plugin_t *temp;
  Bit8u count = 0;

  if (plugins != NULL) {
    temp = plugins;

    while (temp != NULL) {
      if (type == temp->type)
        count++;
      temp = temp->next;
    }
  }
  return count;
}

const char* bx_get_plugin_name(plugintype_t type, Bit8u index)
{
  plugin_t *temp;
  int count = 0;

  if (plugins != NULL) {
    temp = plugins;

    while (temp != NULL) {
      if (type == temp->type) {
        if (count == index)
          return temp->name;
        count++;
      }
      temp = temp->next;
    }
  }
  return NULL;
}

/************************************************************************/
/* Plugin initialization / deinitialization                             */
/************************************************************************/

void plugin_init_one(plugin_t *plugin)
{
  /* initialize the plugin */
  if (plugin->plugin_init(plugin, plugin->type))
  {
    pluginlog->info("Plugin initialization failed for %s", plugin->name);
    plugin_abort();
  }

  plugin->initialized = 1;
}


void plugin_unload(plugin_t *plugin)
{
  if (plugin->loaded) {
    if (plugin->initialized)
      plugin->plugin_fini();
#if defined(WIN32)
    FreeLibrary(plugin->handle);
#else
    lt_dlclose(plugin->handle);
#endif
    if (plugin->type < PLUGTYPE_GUI) {
      plugin->type = PLUGTYPE_DEV;
    }
    plugin->loaded = 0;
  }
}

void plugin_load(const char *name, plugintype_t type)
{
  plugin_t *plugin = NULL, *temp;
  plugintype_t basetype;
#if defined(WIN32)
  char dll_path_list[MAX_PATH];
#endif

  basetype = (type < PLUGTYPE_GUI) ? PLUGTYPE_DEV : type;
  if (plugins != NULL) {
    temp = plugins;

    while (temp != NULL) {
      if (!strcmp(name, temp->name) && (basetype == temp->type)) {
        if (temp->loaded) {
          BX_PANIC(("plugin '%s' already loaded", name));
          return;
        } else {
          plugin = temp;
          break;
        }
      }
      temp = temp->next;
    }
  }
  if (plugin == NULL) {
    BX_PANIC(("plugin '%s' not found", name));
    return;
  }
  if (plugin->type == PLUGTYPE_DEV) {
    plugin->type = type;
  }

  char plugin_filename[BX_PATHNAME_LEN], tmpname[BX_PATHNAME_LEN];
  if (type == PLUGTYPE_GUI) {
    sprintf(tmpname, GUI_PLUGIN_FILENAME_FORMAT, name);
  } else if (type == PLUGTYPE_IMG) {
    sprintf(tmpname, IMG_PLUGIN_FILENAME_FORMAT, name);
  } else if (type == PLUGTYPE_NET) {
    sprintf(tmpname, NET_PLUGIN_FILENAME_FORMAT, name);
  } else if (type == PLUGTYPE_SND) {
    sprintf(tmpname, SND_PLUGIN_FILENAME_FORMAT, name);
  } else {
    sprintf(tmpname, PLUGIN_FILENAME_FORMAT, name);
  }
  sprintf(plugin_filename, "%s%s", PLUGIN_PATH, tmpname);

  // Set context so that any devices that the plugin registers will
  // be able to see which plugin created them.  The registration will
  // be called from either dlopen (global constructors) or plugin_init.
  BX_ASSERT(current_plugin_context == NULL);
  current_plugin_context = plugin;
#if defined(WIN32)
  char *ptr;
  plugin->handle = LoadLibrary(plugin_filename);
  if (!plugin->handle) {
    if (GetEnvironmentVariable("LTDL_LIBRARY_PATH", dll_path_list, MAX_PATH)) {
      ptr = strtok(dll_path_list, ";");
      while ((ptr) && !plugin->handle) {
        sprintf(plugin_filename, "%s\\%s", ptr, tmpname);
        plugin->handle = LoadLibrary(plugin_filename);
        ptr = strtok(NULL, ";");
      }
    }
  }
  BX_INFO(("DLL handle is %p", plugin->handle));
  if (!plugin->handle) {
    current_plugin_context = NULL;
    BX_PANIC(("LoadLibrary failed for module '%s' (%s): error=%d", name,
              plugin_filename, GetLastError()));
    return;
  }
#else
  plugin->handle = lt_dlopen(plugin_filename);
  BX_INFO(("lt_dlhandle is %p", plugin->handle));
  if (!plugin->handle) {
    current_plugin_context = NULL;
    BX_PANIC(("dlopen failed for module '%s' (%s): %s", name, plugin_filename,
              lt_dlerror()));
    return;
  }
#endif

  if (type == PLUGTYPE_GUI) {
    sprintf(tmpname, GUI_PLUGIN_INIT_FMT_STRING, name);
  } else if (type == PLUGTYPE_SND) {
    sprintf(tmpname, SOUND_PLUGIN_INIT_FMT_STRING, name);
  } else if (type == PLUGTYPE_NET) {
    sprintf(tmpname, NET_PLUGIN_INIT_FMT_STRING, name);
  } else if (type == PLUGTYPE_USB) {
    sprintf(tmpname, USB_PLUGIN_INIT_FMT_STRING, name);
  } else if (type == PLUGTYPE_IMG) {
    sprintf(tmpname, IMG_PLUGIN_INIT_FMT_STRING, name);
  } else if (type != PLUGTYPE_USER) {
    sprintf(tmpname, PLUGIN_INIT_FMT_STRING, name);
  } else {
    sprintf(tmpname, PLUGIN_INIT_FMT_STRING, "user");
  }
#if defined(WIN32)
  plugin->plugin_init = (plugin_init_t) GetProcAddress(plugin->handle, tmpname);
  if (plugin->plugin_init == NULL) {
    pluginlog->panic("could not find plugin_init: error=%d", GetLastError());
    plugin_abort();
  }
#else
  plugin->plugin_init = (plugin_init_t) lt_dlsym(plugin->handle, tmpname);
  if (plugin->plugin_init == NULL) {
    pluginlog->panic("could not find plugin_init: %s", lt_dlerror());
    plugin_abort();
  }
#endif

  if (type == PLUGTYPE_GUI) {
    sprintf(tmpname, GUI_PLUGIN_FINI_FMT_STRING, name);
  } else if (type == PLUGTYPE_SND) {
    sprintf(tmpname, SOUND_PLUGIN_FINI_FMT_STRING, name);
  } else if (type == PLUGTYPE_NET) {
    sprintf(tmpname, NET_PLUGIN_FINI_FMT_STRING, name);
  } else if (type == PLUGTYPE_USB) {
    sprintf(tmpname, USB_PLUGIN_FINI_FMT_STRING, name);
  } else if (type == PLUGTYPE_IMG) {
    sprintf(tmpname, IMG_PLUGIN_FINI_FMT_STRING, name);
  } else if (type != PLUGTYPE_USER) {
    sprintf(tmpname, PLUGIN_FINI_FMT_STRING, name);
  } else {
    sprintf(tmpname, PLUGIN_FINI_FMT_STRING, "user");
  }
#if defined(WIN32)
  plugin->plugin_fini = (plugin_fini_t) GetProcAddress(plugin->handle, tmpname);
  if (plugin->plugin_fini == NULL) {
    pluginlog->panic("could not find plugin_fini: error=%d", GetLastError());
    plugin_abort();
  }
#else
  plugin->plugin_fini = (plugin_fini_t) lt_dlsym(plugin->handle, tmpname);
  if (plugin->plugin_fini == NULL) {
    pluginlog->panic("could not find plugin_fini: %s", lt_dlerror());
    plugin_abort();
  }
#endif
  pluginlog->info("loaded plugin %s",plugin_filename);
  plugin->loaded = 1;

  plugin_init_one(plugin);

  // check that context didn't change.  This should only happen if we
  // need a reentrant plugin_load.
  BX_ASSERT(current_plugin_context == plugin);
  current_plugin_context = NULL;
}

void plugin_abort(void)
{
  pluginlog->panic("plugin load aborted");
}

#endif   /* end of #if BX_PLUGINS */

/************************************************************************/
/* Plugin system: initialisation of plugins entry points                */
/************************************************************************/

void plugin_startup(void)
{
  pluginRegisterIRQ = builtinRegisterIRQ;
  pluginUnregisterIRQ = builtinUnregisterIRQ;

  pluginSetHRQHackCallback = builtinSetHRQHackCallback;
  pluginSetHRQ = builtinSetHRQ;

  pluginRegisterIOReadHandler = builtinRegisterIOReadHandler;
  pluginRegisterIOWriteHandler = builtinRegisterIOWriteHandler;

  pluginUnregisterIOReadHandler = builtinUnregisterIOReadHandler;
  pluginUnregisterIOWriteHandler = builtinUnregisterIOWriteHandler;

  pluginRegisterIOReadHandlerRange = builtinRegisterIOReadHandlerRange;
  pluginRegisterIOWriteHandlerRange = builtinRegisterIOWriteHandlerRange;

  pluginUnregisterIOReadHandlerRange = builtinUnregisterIOReadHandlerRange;
  pluginUnregisterIOWriteHandlerRange = builtinUnregisterIOWriteHandlerRange;

  pluginRegisterDefaultIOReadHandler = builtinRegisterDefaultIOReadHandler;
  pluginRegisterDefaultIOWriteHandler = builtinRegisterDefaultIOWriteHandler;

  pluginlog = new logfunctions();
  pluginlog->put("PLUGIN");
#if BX_PLUGINS
#if !defined(WIN32)
  int status = lt_dlinit();
  if (status != 0) {
    BX_ERROR(("initialization error in ltdl library (for loading plugins)"));
    BX_PANIC(("error message was: %s", lt_dlerror()));
  }
#endif
  plugins_search();
#endif
}

void plugin_cleanup(void)
{
#if BX_PLUGINS
  plugin_t *dead_plug;

  while (plugins != NULL) {
    if (plugins->loaded) {
      plugin_unload(plugins);
    }
    delete [] plugins->name;

    dead_plug = plugins;
    plugins = plugins->next;
    delete dead_plug;
  }
#endif
}


/************************************************************************/
/* Plugin system: Device registration                                   */
/************************************************************************/

void pluginRegisterDeviceDevmodel(plugin_t *plugin, plugintype_t type, bx_devmodel_c *devmodel, const char *name)
{
  device_t **devlist;

  device_t *device = new device_t;

  device->name = name;
  BX_ASSERT(devmodel != NULL);
  device->devmodel = devmodel;
  device->plugin = plugin;  // this can be NULL
  device->next = NULL;
  device->plugtype = type;

  switch (type) {
    case PLUGTYPE_CORE:
    case PLUGTYPE_VGA:
      devlist = &core_devices;
      break;
    case PLUGTYPE_STANDARD:
    case PLUGTYPE_OPTIONAL:
    case PLUGTYPE_USER:
    default:
      devlist = &devices;
      break;
  }

  if (!*devlist) {
    /* Empty list, this become the first entry. */
    *devlist = device;
  } else {
    /* Non-empty list.  Add to end. */
    device_t *temp = *devlist;

    while (temp->next)
      temp = temp->next;

    temp->next = device;
  }
}

/************************************************************************/
/* Plugin system: Remove registered plugin device                       */
/************************************************************************/

void pluginUnregisterDeviceDevmodel(const char *name)
{
  device_t *device, *prev = NULL;

  for (device = devices; device; device = device->next) {
    if (!strcmp(name, device->name)) {
      if (prev == NULL) {
        devices = device->next;
      } else {
        prev->next = device->next;
      }
      delete device;
      break;
    } else {
      prev = device;
    }
  }
}

/************************************************************************/
/* Plugin system: Check if a plugin is loaded                           */
/************************************************************************/

bx_bool pluginDevicePresent(const char *name)
{
  device_t *device;

  for (device = devices; device; device = device->next)
  {
    if (!strcmp(name, device->name)) return 1;
  }

  return 0;
}

#if BX_PLUGINS
/************************************************************************/
/* Plugin system: Load one plugin                                       */
/************************************************************************/

int bx_load_plugin(const char *name, plugintype_t type)
{
  plugin_t *plugin;

  if (!strcmp(name, "*")) {
    for (plugin = plugins; plugin; plugin = plugin->next) {
      if ((type == plugin->type) && !plugin->loaded) {
        plugin_load(plugin->name, type);
      }
    }
  } else {
    plugin_load(name, type);
  }
  return 1;
}

void bx_unload_plugin(const char *name, bx_bool devflag)
{
  plugin_t *plugin;

  for (plugin = plugins; plugin; plugin = plugin->next) {
    if (!strcmp(plugin->name, name)) {
      if (devflag) {
        pluginUnregisterDeviceDevmodel(plugin->name);
      }
      plugin_unload(plugin);
      break;
    }
  }
}

void bx_unload_plugin_type(const char *name, plugintype_t type)
{
  plugin_t *plugin;

  for (plugin = plugins; plugin; plugin = plugin->next) {
    if (!strcmp(plugin->name, name) && (plugin->type == type)) {
      plugin_unload(plugin);
      break;
    }
  }
}

#endif   /* end of #if BX_PLUGINS */

/*************************************************************************/
/* Plugin system: Execute init function of all registered plugin-devices */
/*************************************************************************/

void bx_init_plugins()
{
  device_t *device;

  for (device = core_devices; device; device = device->next) {
    pluginlog->info("init_dev of '%s' plugin device by virtual method",device->name);
    device->devmodel->init();
  }
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_STANDARD) {
      pluginlog->info("init_dev of '%s' plugin device by virtual method",device->name);
      device->devmodel->init();
    }
  }
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_OPTIONAL) {
      pluginlog->info("init_dev of '%s' plugin device by virtual method",device->name);
      device->devmodel->init();
    }
  }
#if BX_PLUGINS
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_USER) {
      pluginlog->info("init_dev of '%s' plugin device by virtual method",device->name);
      device->devmodel->init();
    }
  }
#endif
}

/**************************************************************************/
/* Plugin system: Execute reset function of all registered plugin-devices */
/**************************************************************************/

void bx_reset_plugins(unsigned signal)
{
  device_t *device;

  for (device = core_devices; device; device = device->next) {
    pluginlog->info("reset of '%s' plugin device by virtual method",device->name);
    device->devmodel->reset(signal);
  }
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_STANDARD) {
      pluginlog->info("reset of '%s' plugin device by virtual method",device->name);
      device->devmodel->reset(signal);
    }
  }
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_OPTIONAL) {
      pluginlog->info("reset of '%s' plugin device by virtual method",device->name);
      device->devmodel->reset(signal);
    }
  }
#if BX_PLUGINS
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_USER) {
      pluginlog->info("reset of '%s' plugin device by virtual method",device->name);
      device->devmodel->reset(signal);
    }
  }
#endif
}

/*******************************************************/
/* Plugin system: Unload all registered plugin-devices */
/*******************************************************/

void bx_unload_plugins()
{
  device_t *device, *next;

  device = devices;
  while (device != NULL) {
    if (device->plugin != NULL) {
#if BX_PLUGINS
      bx_unload_plugin(device->name, 0);
#endif
    } else {
#if !BX_PLUGINS
      if (!bx_unload_opt_plugin(device->name, 0)) {
        delete device->devmodel;
      }
#endif
    }
    next = device->next;
    delete device;
    device = next;
  }
  devices = NULL;
}

void bx_unload_core_plugins()
{
  device_t *device, *next;

  device = core_devices;
  while (device != NULL) {
    if (device->plugin != NULL) {
#if BX_PLUGINS
      bx_unload_plugin(device->name, 0);
#endif
    } else {
      delete device->devmodel;
    }
    next = device->next;
    delete device;
    device = next;
  }
  core_devices = NULL;
}

/**************************************************************************/
/* Plugin system: Register device state of all registered plugin-devices  */
/**************************************************************************/

void bx_plugins_register_state()
{
  device_t *device;

  for (device = core_devices; device; device = device->next) {
    pluginlog->info("register state of '%s' plugin device by virtual method",device->name);
    device->devmodel->register_state();
  }
  for (device = devices; device; device = device->next) {
    pluginlog->info("register state of '%s' plugin device by virtual method",device->name);
    device->devmodel->register_state();
  }
}

/***************************************************************************/
/* Plugin system: Execute code after restoring state of all plugin devices */
/***************************************************************************/

void bx_plugins_after_restore_state()
{
  device_t *device;

  for (device = core_devices; device; device = device->next) {
    device->devmodel->after_restore_state();
  }
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_STANDARD) {
      device->devmodel->after_restore_state();
    }
  }
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_OPTIONAL) {
      device->devmodel->after_restore_state();
    }
  }
#if BX_PLUGINS
  for (device = devices; device; device = device->next) {
    if (device->plugtype == PLUGTYPE_USER) {
      device->devmodel->after_restore_state();
    }
  }
#endif
}

#if !BX_PLUGINS

// Special code for loading gui, optional and sound plugins when plugin support
// is turned off.

typedef struct {
  const char*   name;
  plugintype_t  type;
  plugin_init_t plugin_init;
  plugin_fini_t plugin_fini;
  bx_bool       status;
} builtin_plugin_t;

#define BUILTIN_GUI_PLUGIN_ENTRY(mod) {#mod, PLUGTYPE_GUI, lib##mod##_gui_plugin_init, lib##mod##_gui_plugin_fini, 0}
#define BUILTIN_OPT_PLUGIN_ENTRY(mod) {#mod, PLUGTYPE_OPTIONAL, lib##mod##_LTX_plugin_init, lib##mod##_LTX_plugin_fini, 0}
#define BUILTIN_SND_PLUGIN_ENTRY(mod) {#mod, PLUGTYPE_SND, lib##mod##_sound_plugin_init, lib##mod##_sound_plugin_fini, 0}
#define BUILTIN_NET_PLUGIN_ENTRY(mod) {#mod, PLUGTYPE_NET, lib##mod##_net_plugin_init, lib##mod##_net_plugin_fini, 0}
#define BUILTIN_USB_PLUGIN_ENTRY(mod) {#mod, PLUGTYPE_USB, lib##mod##_dev_plugin_init, lib##mod##_dev_plugin_fini, 0}
#define BUILTIN_VGA_PLUGIN_ENTRY(mod) {#mod, PLUGTYPE_VGA, lib##mod##_LTX_plugin_init, lib##mod##_LTX_plugin_fini, 0}
#define BUILTIN_IMG_PLUGIN_ENTRY(mod) {#mod, PLUGTYPE_IMG, lib##mod##_img_plugin_init, lib##mod##_img_plugin_fini, 0}

static builtin_plugin_t builtin_plugins[] = {
#if BX_WITH_AMIGAOS
  BUILTIN_GUI_PLUGIN_ENTRY(amigaos),
#endif
#if BX_WITH_CARBON
  BUILTIN_GUI_PLUGIN_ENTRY(carbon),
#endif
#if BX_WITH_MACOS
  BUILTIN_GUI_PLUGIN_ENTRY(macos),
#endif
#if BX_WITH_NOGUI
  BUILTIN_GUI_PLUGIN_ENTRY(nogui),
#endif
#if BX_WITH_RFB
  BUILTIN_GUI_PLUGIN_ENTRY(rfb),
#endif
#if BX_WITH_SDL
  BUILTIN_GUI_PLUGIN_ENTRY(sdl),
#endif
#if BX_WITH_SDL2
  BUILTIN_GUI_PLUGIN_ENTRY(sdl2),
#endif
#if BX_WITH_TERM
  BUILTIN_GUI_PLUGIN_ENTRY(term),
#endif
#if BX_WITH_VNCSRV
  BUILTIN_GUI_PLUGIN_ENTRY(vncsrv),
#endif
#if BX_WITH_WIN32
  BUILTIN_GUI_PLUGIN_ENTRY(win32),
#endif
#if BX_WITH_WX
  BUILTIN_GUI_PLUGIN_ENTRY(wx),
#endif
#if BX_WITH_X11
  BUILTIN_GUI_PLUGIN_ENTRY(x),
#endif
  BUILTIN_OPT_PLUGIN_ENTRY(unmapped),
  BUILTIN_OPT_PLUGIN_ENTRY(biosdev),
  BUILTIN_OPT_PLUGIN_ENTRY(speaker),
  BUILTIN_OPT_PLUGIN_ENTRY(extfpuirq),
  BUILTIN_OPT_PLUGIN_ENTRY(parallel),
  BUILTIN_OPT_PLUGIN_ENTRY(serial),
#if BX_SUPPORT_BUSMOUSE
  BUILTIN_OPT_PLUGIN_ENTRY(busmouse),
#endif
#if BX_SUPPORT_E1000
  BUILTIN_OPT_PLUGIN_ENTRY(e1000),
#endif
#if BX_SUPPORT_ES1370
  BUILTIN_OPT_PLUGIN_ENTRY(es1370),
#endif
#if BX_SUPPORT_GAMEPORT
  BUILTIN_OPT_PLUGIN_ENTRY(gameport),
#endif
#if BX_SUPPORT_IODEBUG
  BUILTIN_OPT_PLUGIN_ENTRY(iodebug),
#endif
#if BX_SUPPORT_NE2K
  BUILTIN_OPT_PLUGIN_ENTRY(ne2k),
#endif
#if BX_SUPPORT_PCIDEV
  BUILTIN_OPT_PLUGIN_ENTRY(pcidev),
#endif
#if BX_SUPPORT_PCIPNIC
  BUILTIN_OPT_PLUGIN_ENTRY(pcipnic),
#endif
#if BX_SUPPORT_SB16
  BUILTIN_OPT_PLUGIN_ENTRY(sb16),
#endif
#if BX_SUPPORT_USB_UHCI
  BUILTIN_OPT_PLUGIN_ENTRY(usb_uhci),
#endif
#if BX_SUPPORT_USB_OHCI
  BUILTIN_OPT_PLUGIN_ENTRY(usb_ohci),
#endif
#if BX_SUPPORT_USB_EHCI
  BUILTIN_OPT_PLUGIN_ENTRY(usb_ehci),
#endif
#if BX_SUPPORT_USB_XHCI
  BUILTIN_OPT_PLUGIN_ENTRY(usb_xhci),
#endif
#if BX_SUPPORT_VOODOO
  BUILTIN_VGA_PLUGIN_ENTRY(voodoo),
  BUILTIN_OPT_PLUGIN_ENTRY(voodoo),
#endif
#if BX_SUPPORT_SOUNDLOW
  BUILTIN_SND_PLUGIN_ENTRY(dummy),
  BUILTIN_SND_PLUGIN_ENTRY(file),
#if BX_HAVE_SOUND_ALSA
  BUILTIN_SND_PLUGIN_ENTRY(alsa),
#endif
#if BX_HAVE_SOUND_OSS
  BUILTIN_SND_PLUGIN_ENTRY(oss),
#endif
#if BX_HAVE_SOUND_OSX
  BUILTIN_SND_PLUGIN_ENTRY(osx),
#endif
#if BX_HAVE_SOUND_SDL
  BUILTIN_SND_PLUGIN_ENTRY(sdl),
#endif
#if BX_HAVE_SOUND_WIN
  BUILTIN_SND_PLUGIN_ENTRY(win),
#endif
#endif
#if BX_NETWORKING
#if BX_NETMOD_FBSD
  BUILTIN_NET_PLUGIN_ENTRY(fbsd),
#endif
#if BX_NETMOD_LINUX
  BUILTIN_NET_PLUGIN_ENTRY(linux),
#endif
  BUILTIN_NET_PLUGIN_ENTRY(null),
#if BX_NETMOD_SLIRP
  BUILTIN_NET_PLUGIN_ENTRY(slirp),
#endif
#if BX_NETMOD_SOCKET
  BUILTIN_NET_PLUGIN_ENTRY(socket),
#endif
#if BX_NETMOD_TAP
  BUILTIN_NET_PLUGIN_ENTRY(tap),
#endif
#if BX_NETMOD_TUNTAP
  BUILTIN_NET_PLUGIN_ENTRY(tuntap),
#endif
#if BX_NETMOD_VDE
  BUILTIN_NET_PLUGIN_ENTRY(vde),
#endif
  BUILTIN_NET_PLUGIN_ENTRY(vnet),
#if BX_NETMOD_WIN32
  BUILTIN_NET_PLUGIN_ENTRY(win32),
#endif
#endif
#if BX_SUPPORT_PCIUSB
  BUILTIN_USB_PLUGIN_ENTRY(usb_cbi),
  BUILTIN_USB_PLUGIN_ENTRY(usb_hid),
  BUILTIN_USB_PLUGIN_ENTRY(usb_hub),
  BUILTIN_USB_PLUGIN_ENTRY(usb_msd),
  BUILTIN_USB_PLUGIN_ENTRY(usb_printer),
#endif
  BUILTIN_IMG_PLUGIN_ENTRY(vmware3),
  BUILTIN_IMG_PLUGIN_ENTRY(vmware4),
  BUILTIN_IMG_PLUGIN_ENTRY(vbox),
  BUILTIN_IMG_PLUGIN_ENTRY(vpc),
  BUILTIN_IMG_PLUGIN_ENTRY(vvfat),
  {"NULL", PLUGTYPE_GUI, NULL, NULL, 0}
};

int bx_load_plugin2(const char *name, plugintype_t type)
{
  int i = 0;
  while (strcmp(builtin_plugins[i].name, "NULL")) {
    if ((!strcmp(name, builtin_plugins[i].name)) &&
        (type == builtin_plugins[i].type)) {
      if (builtin_plugins[i].status == 0) {
        builtin_plugins[i].plugin_init(NULL, type);
        builtin_plugins[i].status = 1;
      }
      return 1;
    }
    i++;
  };
  return 0;
}

int bx_unload_opt_plugin(const char *name, bx_bool devflag)
{
  int i = 0;
  while (strcmp(builtin_plugins[i].name, "NULL")) {
    if ((!strcmp(name, builtin_plugins[i].name)) &&
        (builtin_plugins[i].type == PLUGTYPE_OPTIONAL)) {
      if (builtin_plugins[i].status == 1) {
        if (devflag) {
          pluginUnregisterDeviceDevmodel(builtin_plugins[i].name);
        }
        builtin_plugins[i].plugin_fini();
        builtin_plugins[i].status = 0;
      }
      return 1;
    }
    i++;
  };
  return 0;
}

#endif

}
