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

#include <rawstudio.h>
#include <glib.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <config.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include "application.h"
#include "rs-batch.h"
#include "conf_interface.h"
#include "gettext.h"
#include "gtk-helper.h"
#include "gtk-interface.h"
#include "filename.h"
#include "rs-cache.h"
#include "rs-photo.h"
#include "rs-actions.h"
#include "rs-store.h"

extern GtkWindow *rawstudio_window;

static GtkWidget *make_batchview(RS_QUEUE *queue);
static void size_update_infolabel(RS_QUEUE *queue);
static gchar *batch_queue_filename = NULL;
static void batch_queue_update_sensivity(RS_QUEUE *queue);

static void
batch_queue_save(RS_QUEUE *queue)
{
	xmlTextWriterPtr writer;
	GtkTreeIter iter;
	gchar *filename;
	gint setting_id;

	g_assert(queue != NULL);
	g_assert(batch_queue_filename != NULL);

	writer = xmlNewTextWriterFilename(batch_queue_filename, 0);
	if (!writer)
		return;
	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);
	xmlTextWriterStartElement(writer, BAD_CAST "rawstudio-batch-queue");

	if (gtk_tree_model_get_iter_first(queue->list, &iter))
		do
		{
			gtk_tree_model_get(queue->list, &iter,
				RS_QUEUE_ELEMENT_FILENAME, &filename,
				RS_QUEUE_ELEMENT_SETTING_ID, &setting_id,
				-1);
			xmlTextWriterStartElement(writer, BAD_CAST "entry");
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "filename", "%s", filename);
				xmlTextWriterWriteFormatElement(writer, BAD_CAST "snapshot", "%d", setting_id);
			xmlTextWriterEndElement(writer);
			g_free(filename);
		} while(gtk_tree_model_iter_next(queue->list, &iter));

	xmlTextWriterEndDocument(writer);
	xmlFreeTextWriter(writer);

	return;
}

static void
batch_queue_load(RS_QUEUE *queue)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlNodePtr entry = NULL;
	xmlChar *val;

	g_assert(queue != NULL);

	if (!batch_queue_filename)
		batch_queue_filename = g_build_filename(rs_confdir_get(), "batch-queue.xml", NULL);

	if (!g_file_test(batch_queue_filename, G_FILE_TEST_IS_REGULAR))
		return;

	doc = xmlParseFile(batch_queue_filename);
	if (!doc)
		return;

	cur = xmlDocGetRootElement(doc);
	cur = cur->xmlChildrenNode;

	while(cur)
	{
		if ((!xmlStrcmp(cur->name, BAD_CAST "entry")))
		{
			xmlChar *filename = NULL;
			gint setting_id = -1;

			entry = cur->xmlChildrenNode;

			while (entry)
			{
				if ((!xmlStrcmp(entry->name, BAD_CAST "filename")))
				{
					filename = xmlNodeListGetString(doc, entry->xmlChildrenNode, 1);
				}
				if ((!xmlStrcmp(entry->name, BAD_CAST "snapshot")))
				{
					val = xmlNodeListGetString(doc, entry->xmlChildrenNode, 1);
					setting_id = atoi((char *)val);
					if (setting_id > 2) setting_id = 2;
					if (setting_id < 0) setting_id = 0;
					xmlFree(val);
				}
				entry = entry->next;
			}
			if (filename && (setting_id >= 0))
			{
				rs_batch_add_to_queue(queue, (gchar *) filename, setting_id);
				xmlFree(filename);
			}
		}
		cur = cur->next;
	}

	xmlFreeDoc(doc);
	return;
}

RS_QUEUE* rs_batch_new_queue(RS_BLOB *rs)
{
	gchar *tmp;
	RS_QUEUE *queue = g_new(RS_QUEUE, 1);
	queue->rs = rs;
	queue->output = NULL;

	queue->list = GTK_TREE_MODEL(gtk_list_store_new(5, G_TYPE_STRING,G_TYPE_STRING,
									G_TYPE_INT,G_TYPE_STRING, GDK_TYPE_PIXBUF));

	queue->directory = rs_conf_get_string(CONF_BATCH_DIRECTORY);
	if (queue->directory == NULL)
	{
		rs_conf_set_string(CONF_BATCH_DIRECTORY, DEFAULT_CONF_BATCH_DIRECTORY);
		queue->directory = rs_conf_get_string(CONF_BATCH_DIRECTORY);
	}

	queue->filename = rs_conf_get_string(CONF_BATCH_FILENAME);
	if (queue->filename == NULL)
	{
		rs_conf_set_string(CONF_BATCH_FILENAME, DEFAULT_CONF_BATCH_FILENAME);
		queue->filename = rs_conf_get_string(CONF_BATCH_FILENAME);
	}

	queue->size_lock = LOCK_SCALE;
	queue->size = 100;
	queue->size_window = NULL;
	queue->scale = 100;
	queue->width = 600;
	queue->height = 600;

	/* Load last values */
	rs_conf_get_integer(CONF_BATCH_SIZE_SCALE, &queue->scale);
	rs_conf_get_integer(CONF_BATCH_SIZE_WIDTH, &queue->width);
	rs_conf_get_integer(CONF_BATCH_SIZE_HEIGHT, &queue->height);
	tmp = rs_conf_get_string(CONF_BATCH_SIZE_LOCK);
	if (tmp)
	{
		if (g_str_equal(tmp, "bounding-box"))
			queue->size_lock = LOCK_BOUNDING_BOX;
		else if (g_str_equal(tmp, "width"))
			queue->size_lock = LOCK_WIDTH;
		else if (g_str_equal(tmp, "height"))
			queue->size_lock = LOCK_HEIGHT;
		g_free(tmp);
	}

	return queue;
}

gboolean
rs_batch_add_to_queue(RS_QUEUE *queue, const gchar *filename, const gint setting_id)
{
	gboolean ret = FALSE;
	if (!rs_batch_exists_in_queue(queue, filename, setting_id))
	{
		RSMetadata *metadata;
		gchar *filename_short, *setting_id_abc;
		GdkPixbuf *pixbuf = NULL, *missing_thumb, *pixbuf_temp;

		filename_short = g_path_get_basename(filename);

		switch(setting_id)
		{
			case 0:
				setting_id_abc = _("A");
				break;
			case 1:
				setting_id_abc = _("B");
				break;
			case 2:
				setting_id_abc = _("C");
				break;
			default:
				return ret;
		}

		missing_thumb = gtk_widget_render_icon(GTK_WIDGET(rawstudio_window),
			GTK_STOCK_MISSING_IMAGE, GTK_ICON_SIZE_DIALOG, NULL);

		metadata = rs_metadata_new_from_file(filename);
		pixbuf = rs_metadata_get_thumbnail(metadata);
		g_object_unref(metadata);

		if (pixbuf)
		{
			gint w,h,temp,size = 48;

			w = gdk_pixbuf_get_width(pixbuf);
			h = gdk_pixbuf_get_height(pixbuf);

			if (w > h)
			{
				temp = 1000*h/w;
				pixbuf_temp = gdk_pixbuf_scale_simple(pixbuf, size, size*temp/1000, GDK_INTERP_BILINEAR);
				g_object_unref(pixbuf);
				pixbuf = pixbuf_temp;
			}
			else
			{
				temp = 1000*w/h;
				pixbuf_temp = gdk_pixbuf_scale_simple(pixbuf, size*temp/1000, size, GDK_INTERP_BILINEAR);
				g_object_unref(pixbuf);
				pixbuf = pixbuf_temp;
			}
		}
		else
		{
			pixbuf = missing_thumb;
			g_object_ref (pixbuf);
		}
		g_object_unref(missing_thumb);

		if (!rs_batch_exists_in_queue(queue, filename, setting_id))
		{
			GtkTreeIter iter;

			gtk_list_store_append (GTK_LIST_STORE(queue->list), &iter);
			gtk_list_store_set (GTK_LIST_STORE(queue->list), &iter,
				RS_QUEUE_ELEMENT_FILENAME, filename,
				RS_QUEUE_ELEMENT_FILENAME_SHORT, filename_short,
				RS_QUEUE_ELEMENT_SETTING_ID, setting_id,
				RS_QUEUE_ELEMENT_SETTING_ID_ABC, setting_id_abc,
				RS_QUEUE_ELEMENT_THUMBNAIL, pixbuf,
				-1);
			ret = TRUE;
		}
		g_object_unref(pixbuf);
		g_free(filename_short);
	}

	batch_queue_save(queue);

	batch_queue_update_sensivity(queue);

	return ret;
}

gboolean
rs_batch_remove_from_queue(RS_QUEUE *queue, const gchar *filename, gint setting_id)
{
	gboolean ret = FALSE;
	GtkTreeIter iter;

	gchar *filename_temp = NULL;
	gint setting_id_temp;

	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(queue->list), &iter);

	if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(queue->list), &iter))
	{
		do
		{
			gtk_tree_model_get(queue->list, &iter,
				RS_QUEUE_ELEMENT_FILENAME, &filename_temp,
				RS_QUEUE_ELEMENT_SETTING_ID, &setting_id_temp,
				-1);

			if (g_str_equal(filename, filename_temp))
			{
				if (setting_id == setting_id_temp)
				{
					gtk_list_store_remove(GTK_LIST_STORE(queue->list), &iter);
					ret = TRUE;
				}
			}
			g_free(filename_temp);

			/* Break out of the loop if we got a hit */
			if (ret)
				break;
		} while (gtk_tree_model_iter_next(queue->list, &iter));
	}

	batch_queue_save(queue);

	rs_core_actions_update_menu_items(queue->rs); /* FIXME: should be done with a signal */
	batch_queue_update_sensivity(queue);

	return ret;
}

gboolean
rs_batch_exists_in_queue(RS_QUEUE *queue, const gchar *filename, gint setting_id)
{
	gboolean ret = FALSE;
	GtkTreeIter iter;

	gchar *filename_temp;
	gint setting_id_temp;

	gtk_tree_model_get_iter_first(queue->list, &iter);

	if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(queue->list), &iter))
	{
		do
		{
			gtk_tree_model_get(queue->list, &iter,
				RS_QUEUE_ELEMENT_FILENAME, &filename_temp,
				RS_QUEUE_ELEMENT_SETTING_ID, &setting_id_temp,
				-1);

			if (g_str_equal(filename, filename_temp))
			{
				if (setting_id == setting_id_temp)
					ret = TRUE;
			}
			g_free(filename_temp);
		} while (gtk_tree_model_iter_next(queue->list, &iter) && !ret);
	}
	return ret;
}

static gboolean
window_destroy(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	gboolean *abort_render = (gboolean *) user_data;
	*abort_render = TRUE;
	return(TRUE);
}

static void
cancel_clicked(GtkButton *button, gpointer user_data)
{
	gboolean *abort_render = (gboolean *) user_data;
	*abort_render = TRUE;
	return;
}

void
rs_batch_process(RS_QUEUE *queue)
{
	RS_PHOTO *photo = NULL;
	GtkTreeIter iter;
	gchar *filename_in;
	gint setting_id;
	GtkWidget *preview = gtk_image_new();
	GdkPixbuf *pixbuf = NULL;
	gint width = -1, height = -1;
	gdouble scale = -1.0;
	gchar *parsed_filename, *basename, *parsed_dir;
	GString *filename;
	GString *status = g_string_new(NULL);
	GtkWidget *window;
	GtkWidget *label = gtk_label_new(NULL);
	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
	GtkWidget *cancel;
	gboolean abort_render = FALSE;
	GTimeVal start_time;
	GTimeVal now_time = {0,0};
	gint time, eta;
	GtkWidget *eta_label = gtk_label_new(NULL);
	gchar *eta_text, *title_text;
	gint h = 0, m = 0, s = 0;
	gint done = 0, left = 0;
	RSFilter *finput = rs_filter_new("RSInputImage16", NULL);
	RSFilter *fdemosaic = rs_filter_new("RSDemosaic", finput);
	RSFilter *ffujirotate = rs_filter_new("RSFujiRotate", fdemosaic);
	RSFilter *flensfun = rs_filter_new("RSLensfun", ffujirotate);
	RSFilter *frotate = rs_filter_new("RSRotate", flensfun);
	RSFilter *fcrop = rs_filter_new("RSCrop", frotate);
	RSFilter *ftransform_input = rs_filter_new("RSColorspaceTransform", fcrop);
	RSFilter *fdcp= rs_filter_new("RSDcp", ftransform_input);
	RSFilter *fcache = rs_filter_new("RSCache", fdcp);
	RSFilter *fresample= rs_filter_new("RSResample", fcache);
	RSFilter *fdenoise= rs_filter_new("RSDenoise", fresample);
	RSFilter *ftransform_display = rs_filter_new("RSColorspaceTransform", fdenoise);
	RSFilter *fend = ftransform_display;
	RSFilterResponse *filter_response;
	RSColorSpace *display_color_space;

	gdk_threads_enter();
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_transient_for(GTK_WINDOW(window), rawstudio_window);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
	gtk_window_resize(GTK_WINDOW(window), 250, 250);
	g_signal_connect((gpointer) window, "delete_event", G_CALLBACK(window_destroy), &abort_render);

	cancel = gtk_button_new_with_label(_("Cancel"));
	g_signal_connect (G_OBJECT(cancel), "clicked",
		G_CALLBACK(cancel_clicked), &abort_render);

	gtk_container_add (GTK_CONTAINER (window), vbox);
	gtk_box_pack_start (GTK_BOX (vbox), gui_framed(preview, _("Last image:"), GTK_SHADOW_IN), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), eta_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), cancel, FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);

	gtk_widget_show_all(window);
	while (gtk_events_pending()) gtk_main_iteration();

	display_color_space = rs_get_display_profile(GTK_WIDGET(window));
	g_mkdir_with_parents(queue->directory, 00755);

	g_get_current_time(&start_time);

	while(gtk_tree_model_get_iter_first(queue->list, &iter) && (!abort_render))
	{
		left = rs_batch_num_entries(queue);
		if (done > 0 && now_time.tv_sec > 0)
		{
			time = (gint) (now_time.tv_sec-start_time.tv_sec);
			eta = (time/done)*left;
			h = (eta/3600);
			eta %= 3600;
			m = (eta/60);
			eta %= 60;
			s = eta;

			eta_text = g_strdup_printf(_("Time left: %dh %dm %ds"), h, m, s);
			title_text = g_strdup_printf(_("Processing Image %d/%d"), done+1, done+left);
		}
		else
		{
			eta_text = g_strdup(_("Time left: ..."));
			title_text = g_strdup_printf(_("Processing Image 1/%d."), left);
		}

		gtk_window_set_title(GTK_WINDOW(window), title_text);
		gtk_label_set_text(GTK_LABEL(eta_label), eta_text);
		g_free(eta_text);
		g_free(title_text);
		done++;

		gtk_tree_model_get(queue->list, &iter,
			RS_QUEUE_ELEMENT_FILENAME, &filename_in,
			RS_QUEUE_ELEMENT_SETTING_ID, &setting_id,
			-1);
		basename = g_path_get_basename(filename_in);
		g_string_printf(status, _("Loading %s ..."), basename);
		gtk_label_set_text(GTK_LABEL(label), status->str);
		while (gtk_events_pending()) gtk_main_iteration();
		g_free(basename);

		gdk_threads_leave();
		photo = rs_photo_load_from_file(filename_in);
		if (photo)
		{
			rs_metadata_load_from_file(photo->metadata, filename_in);
			rs_cache_load(photo);

			/* Build new filename */
			if (NULL == g_strrstr(queue->filename, "%p"))
			{
				filename = g_string_new(queue->directory);
				g_string_append(filename, G_DIR_SEPARATOR_S);
				g_string_append(filename, queue->filename);
			} 
			else
				filename = g_string_new(queue->filename);
			
			g_string_append(filename, ".");
			g_string_append(filename, rs_output_get_extension(queue->output));
			parsed_filename = filename_parse(filename->str, filename_in, setting_id, TRUE);
			
			/* Create directory, if it doesn't exist */
			parsed_dir = g_path_get_dirname(parsed_filename);
			if (FALSE == g_file_test(parsed_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
				if (g_mkdir_with_parents(parsed_dir, 0x1ff))
				{
					gdk_threads_enter();
					gui_status_notify(_("Could not create output directory."));
					break;
				}

			GList *filters = g_list_append(NULL, fend);
			rs_photo_apply_to_filters(photo, filters, setting_id);
			g_list_free(filters);

			rs_filter_set_recursive(fend,
				"image", photo->input_response,
				"filename", photo->filename,
				"bounding-box", TRUE,
				"width", 250,
				"height", 250,
				NULL);

			/* Render preview image */
			RSFilterRequest *request = rs_filter_request_new();
			rs_filter_request_set_quick(RS_FILTER_REQUEST(request), FALSE);
			/* FIXME: Should be set to output colorspace, not forced to sRGB */

			rs_filter_param_set_object(RS_FILTER_PARAM(request), "colorspace", display_color_space);	
			filter_response = rs_filter_get_image8(fend, request);
			pixbuf = rs_filter_response_get_image8(filter_response);
			gdk_threads_enter();
			if (pixbuf)
			{
				gtk_image_set_from_pixbuf(GTK_IMAGE(preview), pixbuf);
				g_object_unref(pixbuf);
			}
			g_object_unref(request);
			g_object_unref(filter_response);

			if (left > 1)
			{
				GtkTreeIter iter2 = iter;
				if (gtk_tree_model_iter_next (queue->list, &iter2))
				{
					gtk_tree_model_get(queue->list, &iter2, RS_QUEUE_ELEMENT_FILENAME, &filename_in, -1);
					rs_io_idle_prefetch_file(filename_in, 0xC01A);
				}
			}
			/* Build text for small preview-window */
			basename = g_path_get_basename(parsed_filename);
			g_string_printf(status, _("Saving %s ..."), basename);
			gtk_label_set_text(GTK_LABEL(label), status->str);
			while (gtk_events_pending())
				gtk_main_iteration();
			g_free(basename);
			gdk_threads_leave();

			width = 65535;
			height = 65535;
			/* Calculate new size */
			switch (queue->size_lock)
			{
				case LOCK_SCALE:
					scale = queue->scale/100.0;
					rs_filter_get_size_simple(fcrop, RS_FILTER_REQUEST_QUICK, &width, &height);
					width = (gint) (((gdouble) width) * scale);
					height = (gint) (((gdouble) height) * scale);
					break;
				case LOCK_WIDTH:
					width = queue->width;
					break;
				case LOCK_HEIGHT:
					height = queue->height;
					break;
				case LOCK_BOUNDING_BOX:
					width = queue->width;
					height = queue->height;
					break;
			}
			rs_filter_set_recursive(fend,
				"width", width,
				"height", height,
				NULL);

			/* Save the image */
			if (g_object_class_find_property(G_OBJECT_GET_CLASS(queue->output), "filename"))
				g_object_set(queue->output, "filename", parsed_filename, NULL);

			rs_output_set_from_conf(queue->output, "batch");

			g_assert(RS_IS_OUTPUT(queue->output));
			g_assert(RS_IS_FILTER(fend));

			gboolean exported = rs_output_execute(queue->output, fend);
			gdk_threads_enter();
			if (exported)
				rs_store_set_flags(NULL, photo->filename, NULL, NULL, &exported, NULL);
			else
			{
				gui_status_notify(_("Could not export photo."));
				break;
			}

			g_free(parsed_filename);
			g_string_free(filename, TRUE);
			g_object_unref(photo);
			photo = NULL;
		}
		gtk_list_store_remove(GTK_LIST_STORE(queue->list), &iter);
		batch_queue_save(queue);

		g_get_current_time(&now_time);
	}
	gtk_widget_destroy(window);

	batch_queue_update_sensivity(queue);
	gdk_threads_leave();

	g_object_unref(finput);
	g_object_unref(fdemosaic);
	g_object_unref(ffujirotate);
	g_object_unref(flensfun);
	g_object_unref(frotate);
	g_object_unref(fcrop);
	g_object_unref(fcache);
	g_object_unref(fresample);
	g_object_unref(fdcp);
	g_object_unref(fdenoise);
	g_object_unref(ftransform_input);
	g_object_unref(ftransform_display);
}

static void
cursor_changed(GtkTreeView *tree_view, gpointer user_data)
{
	batch_queue_update_sensivity((RS_QUEUE *) user_data);
}

static GtkWidget *
make_batchview(RS_QUEUE *queue)
{
	GtkWidget *scroller;
	GtkWidget *view;
	GtkCellRenderer *renderer_text, *renderer_pixbuf;
	GtkTreeViewColumn *column_filename, *column_setting_id, *column_pixbuf;

	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	view = gtk_tree_view_new_with_model(queue->list);
	queue->view = GTK_TREE_VIEW(view);

	gtk_tree_view_set_reorderable(queue->view, TRUE);
	gtk_container_add (GTK_CONTAINER (scroller), view);

	renderer_text = gtk_cell_renderer_text_new();
	renderer_pixbuf = gtk_cell_renderer_pixbuf_new();

	column_pixbuf = gtk_tree_view_column_new_with_attributes (_("Icon"),
					renderer_pixbuf,
					"pixbuf", RS_QUEUE_ELEMENT_THUMBNAIL,
					NULL);
	gtk_tree_view_column_set_resizable(column_pixbuf, TRUE);
	gtk_tree_view_column_set_sizing(column_pixbuf, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	column_filename = gtk_tree_view_column_new_with_attributes (_("Filename"),
					renderer_text,
					"text", RS_QUEUE_ELEMENT_FILENAME_SHORT,
					NULL);
	gtk_tree_view_column_set_resizable(column_filename, TRUE);
	gtk_tree_view_column_set_sizing(column_filename, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	column_setting_id = gtk_tree_view_column_new_with_attributes (_("Setting"),
					renderer_text,
					"text", RS_QUEUE_ELEMENT_SETTING_ID_ABC,
					NULL);
	gtk_tree_view_column_set_resizable(column_setting_id, TRUE);
	gtk_tree_view_column_set_sizing(column_setting_id, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	gtk_tree_view_append_column (GTK_TREE_VIEW (view), column_pixbuf);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view), column_filename);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view), column_setting_id);

	g_signal_connect(G_OBJECT(view), "cursor-changed", G_CALLBACK(cursor_changed), queue);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW (view), FALSE);

	return scroller;
}

static void
batch_button_remove_clicked(GtkWidget *button, RS_QUEUE *queue)
{
	GtkTreePath *path;
	GtkTreeViewColumn *column;

	gtk_tree_view_get_cursor(queue->view,&path,&column);

	if(path && column)
	{
		GtkTreeIter iter;

		if(gtk_tree_model_get_iter(queue->list,&iter,path))
		{
			gtk_list_store_remove(GTK_LIST_STORE(queue->list), &iter);
			batch_queue_save(queue);
		}
	}
	rs_core_actions_update_menu_items(queue->rs); /* FIXME: should be done with a signal */
	batch_queue_update_sensivity(queue);
	return;
}

static void
batch_button_remove_all_clicked(GtkWidget *button, RS_QUEUE *queue)
{
	gtk_list_store_clear(GTK_LIST_STORE(queue->list));
	batch_queue_save(queue);
	batch_queue_update_sensivity(queue);
	return;
}

static void
batch_button_start_clicked(GtkWidget *button, RS_QUEUE *queue)
{
	rs_core_action_group_activate("ProcessBatch");
}

static void
batch_queue_update_sensivity(RS_QUEUE *queue)
{
	GtkTreePath *selected_path;
	GtkTreeIter iter;

	/* If we have any entries, enable "Start" and "Remove all" */
	if (gtk_tree_model_get_iter_first(queue->list, &iter))
	{
		rs_core_action_group_set_sensivity("ProcessBatch", TRUE);
		gtk_widget_set_sensitive(queue->start_button, TRUE);
		gtk_widget_set_sensitive(queue->remove_all_button, TRUE);
	}
	else
	{
		rs_core_action_group_set_sensivity("ProcessBatch", FALSE);
		gtk_widget_set_sensitive(queue->start_button, FALSE);
		gtk_widget_set_sensitive(queue->remove_all_button, FALSE);
	}

	/* If anything is selected, enable "Remove" */
	gtk_tree_view_get_cursor(queue->view, &selected_path, NULL);
	if(selected_path)
	{
		gtk_widget_set_sensitive(queue->remove_button, TRUE);
		gtk_tree_path_free(selected_path);
	}
	else
		gtk_widget_set_sensitive(queue->remove_button, FALSE);
}

GtkWidget *
make_batchbuttons(RS_QUEUE *queue)
{
		GtkWidget *box;

		box = gtk_hbox_new(FALSE,4);

		queue->start_button = gui_button_new_from_stock_with_label(GTK_STOCK_EXECUTE, _("Start"));
		g_signal_connect ((gpointer) queue->start_button, "clicked", G_CALLBACK (batch_button_start_clicked), queue);

		queue->remove_button = gui_button_new_from_stock_with_label(GTK_STOCK_REMOVE, _("Remove"));
		g_signal_connect ((gpointer) queue->remove_button, "clicked", G_CALLBACK (batch_button_remove_clicked), queue);

		queue->remove_all_button = gui_button_new_from_stock_with_label(GTK_STOCK_REMOVE, _("Remove All"));
		g_signal_connect ((gpointer) queue->remove_all_button, "clicked", G_CALLBACK (batch_button_remove_all_clicked), queue);

		gtk_box_pack_start(GTK_BOX (box), queue->start_button, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX (box), queue->remove_button, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX (box), queue->remove_all_button, FALSE, FALSE, 0);

		return box;
}

static void
chooser_changed(GtkFileChooser *chooser, gpointer user_data)
{
	RS_QUEUE *queue = (RS_QUEUE *) user_data;
	g_free(queue->directory);
	queue->directory = gtk_file_chooser_get_filename(chooser);
	rs_conf_set_string(CONF_BATCH_DIRECTORY, queue->directory);
	return;
}

static void
filetype_changed(gpointer active, gpointer user_data)
{
	RS_QUEUE *queue = (RS_QUEUE *) user_data;
	GType filetype = (GType)active;

	if (!filetype)
		return;
	if (queue->output)
		g_object_unref(queue->output);
	queue->output = rs_output_new(g_type_name(filetype));
}

static void
size_lockbox_changed(gpointer selected, gpointer user_data)
{
	RS_QUEUE *queue = (RS_QUEUE *) user_data;
	gint i;
	queue->size_lock = GPOINTER_TO_INT(selected);

	for(i=0;i<3;i++)
	{
		gtk_widget_hide(queue->size_width[i]);
		gtk_widget_hide(queue->size_height[i]);
		gtk_widget_hide(queue->size_scale[i]);
	}

	/* Show needed spinners */
	switch (queue->size_lock)
	{
		case LOCK_WIDTH:
			for(i=0;i<3;i++)
				gtk_widget_show(queue->size_width[i]);
			break;
		case LOCK_HEIGHT:
			for(i=0;i<3;i++)
				gtk_widget_show(queue->size_height[i]);
			break;
		case LOCK_SCALE:
			for(i=0;i<3;i++)
				gtk_widget_show(queue->size_scale[i]);
			break;
		case LOCK_BOUNDING_BOX:
			for(i=0;i<3;i++)
			{
				gtk_widget_show(queue->size_width[i]);
				gtk_widget_show(queue->size_height[i]);
			}
			break;
	}

	size_update_infolabel(queue);

	return;
}

static void
size_width_changed(GtkSpinButton *spinbutton, RS_QUEUE *queue)
{
	queue->width = gtk_spin_button_get_value_as_int(spinbutton);
	size_update_infolabel(queue);
	rs_conf_set_integer(CONF_BATCH_SIZE_WIDTH, queue->width);
}

static void
size_height_changed(GtkSpinButton *spinbutton, RS_QUEUE *queue)
{
	queue->height = gtk_spin_button_get_value_as_int(spinbutton);
	size_update_infolabel(queue);
	rs_conf_set_integer(CONF_BATCH_SIZE_HEIGHT, queue->height);
}

static void
size_scale_changed(GtkSpinButton *spinbutton, RS_QUEUE *queue)
{
	queue->scale = gtk_spin_button_get_value_as_int(spinbutton);
	size_update_infolabel(queue);
	rs_conf_set_integer(CONF_BATCH_SIZE_SCALE, queue->scale);
}

static void
size_close_clicked(GtkButton *button, RS_QUEUE *queue)
{
	gtk_widget_hide(queue->size_window);
}

static void
edit_settings_clicked(GtkButton *button, RS_QUEUE *queue)
{
	RSOutput *output = queue->output;
	GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Edit Output Settings"),
							 NULL,
							 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_STOCK_OK,
							 GTK_RESPONSE_ACCEPT,
							 NULL);
	g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);

	GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	GtkWidget *settings = rs_output_get_parameter_widget(output, "batch");
	gtk_container_add (GTK_CONTAINER (content), settings);
	gtk_widget_show_all(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));
}

static void
batch_size_selection(GtkWidget *button, RS_QUEUE *queue)
{
	RS_CONFBOX *lockbox;
	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
	GtkWidget *table;
	GtkWidget *close;

	/* Only open one at a time */
	if (queue->size_window)
	{
		/* Leave the window at its last position */
		gtk_window_set_position(GTK_WINDOW(queue->size_window), GTK_WIN_POS_NONE);
		gtk_widget_show(queue->size_window);
		gtk_window_present(GTK_WINDOW(queue->size_window));
		return;
	}

	/* Make window */
	queue->size_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(G_OBJECT(queue->size_window), "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), queue);
	gtk_window_set_title (GTK_WINDOW(queue->size_window), _("Photo Size"));
	gtk_window_set_position(GTK_WINDOW(queue->size_window), GTK_WIN_POS_MOUSE);
	gtk_widget_realize (queue->size_window);
	gdk_window_set_type_hint(gtk_widget_get_window(queue->size_window), GDK_WINDOW_TYPE_HINT_UTILITY);
	gtk_window_set_transient_for(GTK_WINDOW(queue->size_window), rawstudio_window);

	/* Chooser */
	lockbox = gui_confbox_new(CONF_BATCH_SIZE_LOCK);
	gui_confbox_add_entry(lockbox, "scale", _("Constant Scale"), GINT_TO_POINTER(LOCK_SCALE));
	gui_confbox_add_entry(lockbox, "width", _("Constant Width"), GINT_TO_POINTER(LOCK_WIDTH));
	gui_confbox_add_entry(lockbox, "height", _("Constant Height"), GINT_TO_POINTER(LOCK_HEIGHT));
	gui_confbox_add_entry(lockbox, "bounding-box", _("Maximum Size"), GINT_TO_POINTER(LOCK_BOUNDING_BOX));
	gui_confbox_load_conf(lockbox, "scale");
	gtk_widget_show(gui_confbox_get_widget(lockbox));

	gtk_box_pack_start (GTK_BOX (vbox), gui_confbox_get_widget(lockbox), FALSE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

	/* Spinners */
	table = gtk_table_new(3, 3, FALSE);
	gtk_table_set_col_spacings(GTK_TABLE(table), 0);
	gtk_table_set_row_spacings(GTK_TABLE(table), 0);
	gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, TRUE, 0);

	queue->size_width[0] = gtk_label_new(_("Photo Width:"));
	queue->size_width[1] = gtk_spin_button_new_with_range(10.0, 10000.0, 1.0);
	queue->size_width[2] = gtk_label_new(_("Pixels"));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(queue->size_width[1]), (gdouble) queue->width);
	g_signal_connect(G_OBJECT(queue->size_width[1]), "value_changed", G_CALLBACK(size_width_changed), queue);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_width[0], 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_width[1], 1, 2, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_width[2], 2, 3, 0, 1);

	queue->size_height[0] = gtk_label_new(_("Photo Height:"));
	queue->size_height[1] = gtk_spin_button_new_with_range(10.0, 10000.0, 1.0);
	queue->size_height[2] = gtk_label_new(_("pixels"));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(queue->size_height[1]), (gdouble) queue->height);
	g_signal_connect(G_OBJECT(queue->size_height[1]), "value_changed", G_CALLBACK(size_height_changed), queue);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_height[0], 0, 1, 1, 2);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_height[1], 1, 2, 1, 2);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_height[2], 2, 3, 1, 2);

	queue->size_scale[0] = gtk_label_new(_("Photo Scale:"));
	queue->size_scale[1] = gtk_spin_button_new_with_range(10.0, 10000.0, 1.0);
	queue->size_scale[2] = gtk_label_new(_("%"));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(queue->size_scale[1]), (gdouble) queue->scale);
	g_signal_connect(G_OBJECT(queue->size_scale[1]), "value_changed", G_CALLBACK(size_scale_changed), queue);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_scale[0], 0, 1, 2, 3);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_scale[1], 1, 2, 2, 3);
	gtk_table_attach_defaults(GTK_TABLE(table), queue->size_scale[2], 2, 3, 2, 3);

	/* Align everything nicely */
	gtk_misc_set_alignment(GTK_MISC(queue->size_height[0]), 1.0, 0.5);
	gtk_misc_set_alignment(GTK_MISC(queue->size_height[2]), 0.0, 0.5);
	gtk_misc_set_alignment(GTK_MISC(queue->size_width[0]), 1.0, 0.5);
	gtk_misc_set_alignment(GTK_MISC(queue->size_width[2]), 0.0, 0.5);
	gtk_misc_set_alignment(GTK_MISC(queue->size_scale[0]), 1.0, 0.5);
	gtk_misc_set_alignment(GTK_MISC(queue->size_scale[2]), 0.0, 0.5);

	/* Close button */
	close = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
	g_signal_connect (G_OBJECT(close), "clicked", G_CALLBACK (size_close_clicked), queue);
	gtk_box_pack_end (GTK_BOX (vbox), gui_aligned(close, 1.0, 0.5, 0.0, 0.0), FALSE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (queue->size_window), vbox);
	gtk_widget_show_all(queue->size_window);
	gtk_window_present(GTK_WINDOW(queue->size_window));

	size_lockbox_changed(gui_confbox_get_active(lockbox), queue);
	gui_confbox_set_callback(lockbox, queue, size_lockbox_changed);
}

static void
size_update_infolabel(RS_QUEUE *queue)
{
	GString *gs = g_string_new("");

	switch (queue->size_lock)
	{
		case LOCK_WIDTH:
			g_string_printf(gs, _("Constant Width:\n%d"), queue->width);
			break;
		case LOCK_HEIGHT:
			g_string_printf(gs, _("Constant Height:\n%d"), queue->height);
			break;
		case LOCK_SCALE:
			g_string_printf(gs, _("Constant Scale:\n%d"), queue->scale);
			g_string_append(gs, "%"); /* FIXME: merge with the above line after release */
			break;
		case LOCK_BOUNDING_BOX:
			g_string_printf(gs, _("Maximum Size:\n%d x %d"), queue->width, queue->height);
			break;
	}

	gtk_label_set_justify(GTK_LABEL(queue->size_label), GTK_JUSTIFY_CENTER);
	gtk_label_set_label(GTK_LABEL(queue->size_label), gs->str);

	g_string_free(gs, TRUE);

	return;
}

static GtkWidget *
make_batch_options(RS_QUEUE *queue)
{
	GtkWidget *chooser;
	GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
	GtkWidget *filename;
	RS_CONFBOX *filetype_confbox;
	GtkWidget *size_button;
	gpointer active;

	chooser = gtk_file_chooser_button_new(_("Choose Output Directory"),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	if (g_path_is_absolute(queue->directory))
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), queue->directory);
	g_signal_connect (chooser, "current_folder_changed",
		G_CALLBACK (chooser_changed), queue);
	gtk_box_pack_start (GTK_BOX (vbox), gui_framed(chooser,
		_("Output Directory:"), GTK_SHADOW_NONE), FALSE, FALSE, 0);

	filename = rs_filename_chooser_button_new(&queue->filename, CONF_BATCH_FILENAME);
	gtk_box_pack_start (GTK_BOX (vbox), gui_framed(filename,
		_("Filename Template:"), GTK_SHADOW_NONE), FALSE, FALSE, 0);

	filetype_confbox = gui_confbox_filetype_new(CONF_BATCH_FILETYPE);
	gui_confbox_set_callback(filetype_confbox, queue, filetype_changed);
	gtk_box_pack_start (GTK_BOX (vbox), gui_confbox_get_widget(filetype_confbox), FALSE, TRUE, 0);

	active = gui_confbox_get_active(filetype_confbox);
	if (!active)
		active = GUINT_TO_POINTER(g_type_from_name("RSJpegfile"));
	filetype_changed(active, queue);

	GtkWidget *edit_settings = gtk_button_new_with_label(_("Edit Output Settings"));
	g_signal_connect ((gpointer) edit_settings, "clicked", G_CALLBACK (edit_settings_clicked), queue);
	gtk_box_pack_start (GTK_BOX (vbox), edit_settings, FALSE, TRUE, 0);

	/* Export size */
	hbox = gtk_hbox_new(FALSE, 1);
	queue->size_label = gtk_label_new(NULL);
	size_update_infolabel(queue);
	size_button = gtk_button_new();
	gtk_button_set_label(GTK_BUTTON(size_button), _("Change"));
	g_signal_connect ((gpointer) size_button, "clicked", G_CALLBACK (batch_size_selection), queue);
	gtk_box_pack_start (GTK_BOX (hbox), queue->size_label, FALSE, FALSE, 1);
	gtk_box_pack_end (GTK_BOX (hbox), size_button, FALSE, FALSE, 1);

	gtk_box_pack_start (GTK_BOX (vbox), gui_framed(hbox, _("Export Dimensions"), GTK_SHADOW_IN), FALSE, TRUE, 0);

	return(vbox);
}

GtkWidget *
make_batchbox(RS_QUEUE *queue)
{
	GtkWidget *batchbox;

	batchbox = gtk_vbox_new(FALSE,4);
	gtk_box_pack_start (GTK_BOX (batchbox), make_batch_options(queue), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (batchbox), make_batchview(queue), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (batchbox), make_batchbuttons(queue), FALSE, FALSE, 0);
	batch_queue_load(queue);

	batch_queue_update_sensivity(queue);

	return batchbox;
}

/**
 * Returns the number of entries in the batch queue
 * @param queue A RS_QUEUE
 * @return The number of entries in the queue
 */
gint
rs_batch_num_entries(RS_QUEUE *queue)
{
	gint num = 0;
	GtkTreeIter iter;

	gtk_tree_model_get_iter_first(queue->list, &iter);

	if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(queue->list), &iter))
	{
		do
		{
			num++;
		} while (gtk_tree_model_iter_next(queue->list, &iter));
	}
	return num;
}
