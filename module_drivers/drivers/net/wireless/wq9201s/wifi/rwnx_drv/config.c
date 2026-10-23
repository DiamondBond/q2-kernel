#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h> /* for IEEE80211_HE_MCS_SUPPORT_0_11 */

#include "config.h"
#include "rwnx_tx.h" /* for NX_TX_PAYLOAD_MAX and RWNX_TX_LIFETIME_MS */
#include "wq_log.h"

#ifndef sizeof_field
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#endif

#define WQ_CONF_BUF_SIZE 128

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0))
#define WQ_REQUEST_FIRMWARE firmware_request_nowarn
#else
#define WQ_REQUEST_FIRMWARE request_firmware
#endif

/* instance of wq_conf and default */
#define WQ_CONF_ITEM(_name, _type, _default, desc) ._name = _default,
#define WQ_CONF_ARRAY_ITEM(_name, _type, _array_size, _default, desc)          \
	._name = _default,

struct wq_conf wq_conf = { WQ_CONF_ITEMS };
EXPORT_SYMBOL(wq_conf);

const struct wq_conf wq_conf_def = { WQ_CONF_ITEMS };

#undef WQ_CONF_ITEM
#undef WQ_CONF_ARRAY_ITEM

/* module parameters */
#define WQ_CONF_ITEM(_name, _type, _default, desc)                             \
	module_param_named(_name, (wq_conf._name), _type, S_IRUGO | S_IWUSR);  \
	MODULE_PARM_DESC(_name, desc);
#define WQ_CONF_ARRAY_ITEM(_name, _type, _array_size, _default, desc)          \
	module_param_array_named(_name, wq_conf._name, _type, NULL,            \
				 S_IRUGO | S_IWUSR);                           \
	MODULE_PARM_DESC(_name, desc);
WQ_CONF_ITEMS;

#undef WQ_CONF_ITEM
#undef WQ_CONF_ARRAY_ITEM

/* mapping */
struct wq_conf_map {
	const char name[24];
	u16 offset;
	u16 array_size;
	u16 field_size; /* total bytes */
	int (*set)(const struct wq_conf_map *map, const char *val,
		   void *result);
	int (*get)(const struct wq_conf_map *map, char *buffer, const void *p);
};

#define WQ_CONF_P(_conf, _map) ((void *)((u8 *)(_conf) + (_map)->offset))

#define WQ_STANDARD_PARAM_DEF(name, type, format, strtolfn)                    \
	int wq_conf_set_##name(const struct wq_conf_map *map, const char *val, \
			       void *result)                                   \
	{                                                                      \
		return strtolfn(val, 0, (type *)result);                       \
	}                                                                      \
                                                                               \
	int wq_conf_get_##name(const struct wq_conf_map *map, char *buffer,    \
			       const void *p)                                  \
	{                                                                      \
		return scnprintf(buffer, WQ_CONF_BUF_SIZE, format,             \
				 *(type *)p);                                  \
	}

static int wq_strtobool(const char *buf, int useless, bool *val)
{
	(void)useless;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
		return kstrtobool(buf, val);
#else
		return strtobool(buf, val);
#endif
}

WQ_STANDARD_PARAM_DEF(byte, unsigned char, "%hhu", kstrtou8);
WQ_STANDARD_PARAM_DEF(int, int, "%i", kstrtoint);
WQ_STANDARD_PARAM_DEF(bool, bool, "%d", wq_strtobool);

#define WQ_CONF_ITEM(_name, type, _default, desc)                              \
	{                                                                      \
		.name = #_name,                                                \
		.offset = offsetof(struct wq_conf, _name),                     \
		.array_size = 1,                                               \
		.field_size = sizeof_field(struct wq_conf, _name),             \
		.set = wq_conf_set_##type,                                     \
		.get = wq_conf_get_##type,                                     \
	},

static int wq_conf_set_bytes(const struct wq_conf_map *map, const char *val,
			     void *result)
{
	if (strcmp(map->name, "mac_addr") == 0) {
		u8 mac[ETH_ALEN] = {};

		if (sscanf(val, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", mac + 0,
			   mac + 1, mac + 2, mac + 3, mac + 4,
			   mac + 5) == ETH_ALEN &&
		    is_valid_ether_addr(mac)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
			ether_addr_copy((u8 *)result, mac);
#else
			(void)memcpy(result, mac, ETH_ALEN);
#endif
		} else {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: %s: \"%s\" is invalid!\n", __func__,
			       map->name, val);
		}
	} else if (strcmp(map->name, "drvcc") == 0) {
		strncpy((char *)result, val, map->array_size);
	} else if (strcmp(map->name, "fw_log_path") == 0) {
		strncpy((char *)result, val, map->array_size);
	}

	return 0;
}

static int wq_conf_get_bytes(const struct wq_conf_map *map, char *buffer,
			     const void *p)
{
	if (strcmp(map->name, "mac_addr") == 0)
		return scnprintf(buffer, WQ_CONF_BUF_SIZE, "%pM", (u8 *)p);
	else if (strcmp(map->name, "drvcc") == 0)
		return scnprintf(buffer, WQ_CONF_BUF_SIZE, "%s", (char *)p);
	else if (strcmp(map->name, "fw_log_path") == 0)
		return scnprintf(buffer, WQ_CONF_BUF_SIZE, "%s", (char *)p);

	return 0;
}

#define WQ_CONF_ARRAY_ITEM(_name, type, _array_size, _default, desc)           \
	{                                                                      \
		.name = #_name,                                                \
		.offset = offsetof(struct wq_conf, _name),                     \
		.array_size = _array_size,                                     \
		.field_size = sizeof_field(struct wq_conf, _name),             \
		.set = wq_conf_set_##type##s,                                  \
		.get = wq_conf_get_##type##s,                                  \
	},

const struct wq_conf_map wq_conf_maps[] = { WQ_CONF_ITEMS };

#undef WQ_CONF_ITEM
#undef WQ_CONF_ARRAY_ITEM

static inline int __wq_conf_compare(const struct wq_conf_map *map,
				    const void *a, const void *b)
{
#if 0
	if (map->cmp_of)
		return map->cmp_of(a, b, map->field_size);
#endif
	return memcmp(a, b, map->field_size);
}

static int wq_conf_compare(const struct wq_conf_map *map,
			   const struct wq_conf *a, const struct wq_conf *b)
{
	return __wq_conf_compare(map, WQ_CONF_P(a, map), WQ_CONF_P(b, map));
}

static int wq_conf_map_find(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wq_conf_maps); i++) {
		if (strcasecmp(name, wq_conf_maps[i].name) == 0)
			return i;
	}
	return -1;
}

int wq_config_load(const char *filename)
{
	const struct firmware *config_fw;
	char *dup = NULL;
	char *values[ARRAY_SIZE(wq_conf_maps)] = {};
	int mi; /* map index */
	char *line;
	char *next;
	char *name;
	int ret;

	if (!filename || !filename[0])
		filename = WQ_WLAN_CONFIG_FILE_NAME;

	if ((ret = WQ_REQUEST_FIRMWARE(&config_fw, filename, NULL)) ||
	    (!config_fw || !config_fw->data || !config_fw->size)) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: Failed to get config \"%s\"\n",
		       __func__, filename);
		goto config_exit;
	}

	WQ_DBG(DM_GENERIC, DL_ERR, "%s: %s (%zu bytes)\n", __func__, filename,
	       config_fw->size);

	dup = kstrndup(config_fw->data, config_fw->size, GFP_ATOMIC);
	if (!dup) {
		ret = -ENOMEM;
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: kstrndup %zu failed\n",
		       __func__, config_fw->size);
		goto config_exit;
	}

	next = dup;
	while ((line = strsep(&next, "\r\n"))) {
		line = skip_spaces(line);
		if (line[0] == '\0' || line[0] == '#')
			continue;

		name = strim(strsep(&line, "="));
		if (!line) {
			WQ_DBG(DM_GENERIC, DL_WRN, "%s: \"%s\": no value!\n",
			       __func__, name);
		} else if ((mi = wq_conf_map_find(name)) >= 0) {
			if (values[mi])
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "%s: \"%s\": new \"%s\" old \"%s\"\n",
				       __func__, name, line, values[mi]);
			values[mi] = line; /* don't trim value */
		} else {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: unknown config item \"%s\"=%s\n", __func__,
			       name, line);
		}
	}

	for (mi = 0; mi < ARRAY_SIZE(wq_conf_maps); mi++) {
		const struct wq_conf_map *map = &wq_conf_maps[mi];

		if (!values[mi])
			continue;

		WQ_DBG(DM_GENERIC, DL_WRN, "%s: %s\n", map->name, values[mi]);
		/* don't overwrite it if it's already updated by module parameter */
		if (wq_conf_compare(map, &wq_conf, &wq_conf_def) == 0) {
			if (map->set(map, values[mi],
				     WQ_CONF_P(&wq_conf, map)) == 0) {
				WQ_DBG(DM_GENERIC, DL_WRN, "updated\n");
			}
		}
	}

config_exit:
	if (dup)
		kfree(dup);

	release_firmware(config_fw);
	return ret;
}

void wq_config_dump(void)
{
	int mi;

	WQ_DBG(DM_GENERIC, DL_WRN, "config items:\n");
	for (mi = 0; mi < ARRAY_SIZE(wq_conf_maps); mi++) {
		const struct wq_conf_map *map = &wq_conf_maps[mi];
		char v0[WQ_CONF_BUF_SIZE];
		char v1[WQ_CONF_BUF_SIZE];

		map->get(map, v1, WQ_CONF_P(&wq_conf, map));
		if (wq_conf_compare(map, &wq_conf, &wq_conf_def) != 0) {
			map->get(map, v0, WQ_CONF_P(&wq_conf_def, map));
			WQ_DBG(DM_GENERIC, DL_WRN, "%24s = %s (default: %s)\n",
			       map->name, v1, v0);
		} else {
			WQ_DBG(DM_GENERIC, DL_WRN, "%24s = %s\n", map->name,
			       v1);
		}
	}
}
