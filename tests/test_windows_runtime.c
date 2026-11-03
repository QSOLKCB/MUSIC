/* Packaged-runtime diagnostic; runs only with a Windows desktop. No devices. */
#include <gtk/gtk.h>
#include <stdio.h>
int main(void) {
    GSettingsSchema *schema;
    GSettingsSchemaSource *schemas;
    GdkPixbuf *pixbuf;
    GError *error=NULL;
    const char *svg=g_getenv("MUSIC_PACKAGE_SVG");
    if(!gtk_init_check()) { fputs("Cannot initialize native GTK.\n",stderr); return 1; }
    schemas=g_settings_schema_source_get_default();
    schema=schemas?g_settings_schema_source_lookup(schemas,"org.gtk.gtk4.Settings.FileChooser",TRUE):NULL;
    if(!schema) { fputs("Bundled GTK file-chooser schema missing.\n",stderr); return 1; }
    g_settings_schema_unref(schema);
    if(!gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(gdk_display_get_default()),"document-open-symbolic")) {
        fputs("Bundled symbolic icon missing.\n",stderr); return 1;
    }
    if(!svg) { fputs("Set MUSIC_PACKAGE_SVG to a bundled Adwaita SVG to check its loader.\n",stderr); return 1; }
    pixbuf=gdk_pixbuf_new_from_file(svg,&error);
    if(!pixbuf) { fprintf(stderr,"Bundled SVG loader failed: %s\n",error?error->message:"unknown error");g_clear_error(&error);return 1; }
    if(gdk_pixbuf_get_width(pixbuf)<1 || gdk_pixbuf_get_height(pixbuf)<1) {g_object_unref(pixbuf);return 1;}
    g_object_unref(pixbuf);puts("Windows GTK runtime: PASS (schemas, symbolic icons, SVG loader)");return 0;
}
