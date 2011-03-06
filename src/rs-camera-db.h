/*
 * * Copyright (C) 2006-2011 Anders Brander <anders@brander.dk>, 
 * * Anders Kvist <akv@lnxbx.dk> and Klaus Post <klauspost@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef RS_CAMERA_DB_H
#define RS_CAMERA_DB_H

#include <glib-object.h>
#include "application.h"

G_BEGIN_DECLS

#define RS_TYPE_CAMERA_DB rs_camera_db_get_type()
#define RS_CAMERA_DB(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_CAMERA_DB, RSCameraDb))
#define RS_CAMERA_DB_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_CAMERA_DB, RSCameraDbClass))
#define RS_IS_CAMERA_DB(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_CAMERA_DB))
#define RS_IS_CAMERA_DB_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RS_TYPE_CAMERA_DB))
#define RS_CAMERA_DB_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), RS_TYPE_CAMERA_DB, RSCameraDbClass))

typedef struct _RSCameraDb RSCameraDb;

typedef struct {
	GObjectClass parent_class;
} RSCameraDbClass;

GType rs_camera_db_get_type(void);

RSCameraDb *rs_camera_db_new(const gchar *path);

RSCameraDb *rs_camera_db_get_singleton(void);

void rs_camera_db_save_defaults(RSCameraDb *camera_db, RS_PHOTO *photo);

gboolean
rs_camera_db_photo_get_defaults(RSCameraDb *camera_db, RS_PHOTO *photo, RSSettings **dest_settings, gpointer *dest_profile);

gboolean rs_camera_db_photo_set_defaults(RSCameraDb *camera_db, RS_PHOTO *photo);

G_END_DECLS

#endif /* RS_CAMERA_DB_H */
