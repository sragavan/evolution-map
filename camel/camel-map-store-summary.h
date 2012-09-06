#ifndef CAMEL_MAP_STORE_SUMMARY_H
#define CAMEL_MAP_STORE_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MAP_STORE_SUMMARY \
	(camel_map_store_summary_get_type ())
#define CAMEL_MAP_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAP_STORE_SUMMARY, CamelMapStoreSummary))
#define CAMEL_MAP_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAP_STORE_SUMMARY, CamelMapStoreSummaryClass))
#define CAMEL_IS_MAP_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAP_STORE_SUMMARY))
#define CAMEL_IS_MAP_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAP_STORE_SUMMARY))
#define CAMEL_MAP_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAP_STORE_SUMMARY, CamelMapStoreSummaryClass))

/* the last possible value from CAMEL_FOLDER_TYPE_MASK range */
#define CAMEL_MAP_FOLDER_TYPE_JOURNAL \
	(((CAMEL_FOLDER_TYPE_MASK >> CAMEL_FOLDER_TYPE_BIT) - 1) << \
	CAMEL_FOLDER_TYPE_BIT)

G_BEGIN_DECLS

typedef struct _CamelMapStoreSummary CamelMapStoreSummary;
typedef struct _CamelMapStoreSummaryClass CamelMapStoreSummaryClass;
typedef struct _CamelMapStoreSummaryPrivate CamelMapStoreSummaryPrivate;

struct _CamelMapStoreSummary {
	CamelObject parent;
	CamelMapStoreSummaryPrivate *priv;
};

struct _CamelMapStoreSummaryClass {
	CamelObjectClass parent_class;
};

GType		camel_map_store_summary_get_type	(void);

CamelMapStoreSummary *
		camel_map_store_summary_new	(const gchar *path);
gboolean	camel_map_store_summary_load	(CamelMapStoreSummary *map_summary,
						 GError **error);
gboolean	camel_map_store_summary_save	(CamelMapStoreSummary *map_summary,
						 GError **error);
gboolean	camel_map_store_summary_clear	(CamelMapStoreSummary *map_summary);
gboolean	camel_map_store_summary_remove	(CamelMapStoreSummary *map_summary);
void		camel_map_store_summary_rebuild_hashes
						(CamelMapStoreSummary *map_summary);

void		camel_map_store_summary_set_folder_name
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 const gchar *display_name);
void		camel_map_store_summary_set_parent_folder_id
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 const gchar *parent_id);
void		camel_map_store_summary_set_change_key
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 const gchar *change_key);
void		camel_map_store_summary_set_sync_state
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 const gchar *sync_state);
void		camel_map_store_summary_set_folder_flags
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 guint64 flags);
void		camel_map_store_summary_set_folder_unread
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 guint64 unread);
void		camel_map_store_summary_set_folder_total
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 guint64 total);

gchar *	camel_map_store_summary_get_folder_name
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *camel_map_store_summary_get_folder_full_name
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *	camel_map_store_summary_get_parent_folder_id
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *	camel_map_store_summary_get_change_key
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *	camel_map_store_summary_get_sync_state
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
guint64		camel_map_store_summary_get_folder_flags
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
guint64		camel_map_store_summary_get_folder_unread
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
guint64		camel_map_store_summary_get_folder_total
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);
GSList *	camel_map_store_summary_get_folders
						(CamelMapStoreSummary *map_summary,
						 const gchar *prefix);

void		camel_map_store_summary_store_string_val
						(CamelMapStoreSummary *map_summary,
						 const gchar *key,
						 const gchar *value);

gchar *	camel_map_store_summary_get_string_val
						(CamelMapStoreSummary *map_summary,
						 const gchar *key,
						 GError **error);

gboolean	camel_map_store_summary_remove_folder
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 GError **error);

void		camel_map_store_summary_new_folder
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_id,
						 const gchar *parent_fid,
						 const gchar *change_key,
						 const gchar *display_name,
						 guint64 folder_flags,
						 guint64 total);

gchar *		camel_map_store_summary_get_folder_id_from_name
						(CamelMapStoreSummary *map_summary,
						 const gchar *folder_name);

gchar *		camel_map_store_summary_get_folder_id_from_folder_type
						(CamelMapStoreSummary *map_summary,
						 guint64 folder_type);

gboolean	camel_map_store_summary_has_folder
						(CamelMapStoreSummary *map_summary,
						 const gchar *id);

G_END_DECLS

#endif /* CAMEL_MAP_STORE_SUMMARY_H */
