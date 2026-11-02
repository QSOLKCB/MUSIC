#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"
#include "audio.h"

enum { OP_IMPORT,OP_OPEN,OP_SAVE,OP_EXPORT };
enum { CONFIRM_NEW,CONFIRM_OPEN,CONFIRM_CLOSE,CONFIRM_DEMO };
typedef struct {
    GtkApplication *application;
    GtkWidget *window,*canvas,*list,*status,*clock,*spinner,*properties;
    GtkWidget *add,*open,*save,*export,*new_button,*play,*record,*stop,*cancel,*demo,*bpm;
    GtkWidget *name,*offset,*trim_in,*trim_out,*gain,*mute,*solo,*remove,*snap;
    GtkWidget *row_labels[MUSIC_TRACKS];
    MusicProject project; MusicAudio *audio;
    GFile *file; GCancellable *cancel_io;
    uint16_t *peaks[MUSIC_TRACKS]; uint32_t peak_count[MUSIC_TRACKS];
    unsigned selected; uint32_t position,drag_start; double drag_seconds;
    int dirty,busy,updating,closing_after_save,confirm_action,dialog_open;
    int drag_track,smoke,smoke_stage,smoke_failed; char *smoke_dir;
    guint timer; guint64 smoke_ticks;
} App;
typedef struct {
    App *app; GFile *file; int operation; MusicProject loaded;
    char *message; GError *error; GOutputStream *stream;
} FileJob;
typedef struct { App *app; int operation; } Choice;

static void refresh(App *a);
static void properties(App *a);
static void choose_file(App *a,int operation);
static void ask_discard(App *a,int action);
static void stop_audio(App *a);
static void status(App *a,const char *text) { gtk_label_set_text(GTK_LABEL(a->status),text); }
static GtkWidget *label(const char *text) { GtkWidget *w=gtk_label_new(text); gtk_label_set_xalign(GTK_LABEL(w),0); return w; }
static GtkWidget *button(const char *text,GCallback callback,App *a) {
    GtkWidget *w=gtk_button_new_with_label(text); g_signal_connect(w,"clicked",callback,a); return w;
}
static void margins(GtkWidget *w,int n) {
    gtk_widget_set_margin_start(w,n); gtk_widget_set_margin_end(w,n);
    gtk_widget_set_margin_top(w,n); gtk_widget_set_margin_bottom(w,n);
}
static double view_seconds(App *a) {
    uint32_t end=music_duration(&a->project);
    if(a->position>end) end=a->position;
    return fmax(12.0,(double)end/MUSIC_RATE+1.0);
}
static void rebuild_peaks(App *a) {
    unsigned i;
    for(i=0;i<MUSIC_TRACKS;++i) {
        free(a->peaks[i]); a->peaks[i]=NULL; a->peak_count[i]=0;
        if(i<a->project.count) {
            MusicTrackData *t=&a->project.tracks[i]; uint32_t f,bins=(t->frames+255)/256;
            a->peaks[i]=(uint16_t*)calloc(bins,sizeof(uint16_t));
            if(!a->peaks[i]) continue;
            a->peak_count[i]=bins;
            for(f=0;f<t->frames;++f) {
                int left=abs((int)t->samples[(size_t)f*2]),right=abs((int)t->samples[(size_t)f*2+1]);
                unsigned peak=(unsigned)(left>right?left:right);
                if(peak>a->peaks[i][f/256]) a->peaks[i][f/256]=(uint16_t)peak;
            }
        }
    }
}
static void select_row(GtkListBox *list,GtkListBoxRow *row,gpointer data) {
    App *a=data; (void)list;
    if(row) { a->selected=(unsigned)gtk_list_box_row_get_index(row); properties(a); gtk_widget_queue_draw(a->canvas); }
}
static void rebuild_tracks(App *a) {
    GtkWidget *child; unsigned i;
    a->updating=1;
    while((child=gtk_widget_get_first_child(a->list))) gtk_list_box_remove(GTK_LIST_BOX(a->list),child);
    for(i=0;i<a->project.count;++i) {
        GtkWidget *row=gtk_list_box_row_new(),*box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*name=label("");
        margins(box,12); gtk_widget_set_size_request(row,190,80);
        gtk_label_set_ellipsize(GTK_LABEL(name),PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(name),20);
        gtk_box_append(GTK_BOX(box),name); a->row_labels[i]=name;
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),box); gtk_list_box_append(GTK_LIST_BOX(a->list),row);
    }
    if(a->selected>=a->project.count) a->selected=a->project.count?a->project.count-1:0;
    gtk_list_box_select_row(GTK_LIST_BOX(a->list),gtk_list_box_get_row_at_index(GTK_LIST_BOX(a->list),(int)a->selected));
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a->canvas),(int)fmax(320,40+a->project.count*80));
    a->updating=0; properties(a); refresh(a);
}
static void draw(GtkDrawingArea *area,cairo_t *cr,int width,int height,gpointer data) {
    App *a=data; double seconds=view_seconds(a),scale=width/seconds,beat=60.0/a->project.bpm; unsigned i;
    (void)area;
    cairo_set_source_rgb(cr,.075,.085,.10); cairo_paint(cr);
    cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(cr,11);
    { double step=beat; while(seconds/step>80) step*=4;
      for(double s=0;s<seconds;s+=step) {
        double x=s*scale; char text[32];
        cairo_set_source_rgb(cr,.16,.18,.21); cairo_move_to(cr,x,32); cairo_line_to(cr,x,height); cairo_stroke(cr);
        snprintf(text,sizeof(text),"%.1fs",s); cairo_set_source_rgb(cr,.65,.69,.74); cairo_move_to(cr,x+5,22); cairo_show_text(cr,text);
      }
    }
    for(i=0;i<a->project.count;++i) {
        MusicTrackData *t=&a->project.tracks[i]; double x=(double)t->offset/MUSIC_RATE*scale;
        double w=(double)(t->trim_out-t->trim_in)/MUSIC_RATE*scale,y=40+i*80.0;
        int from=(int)fmax(0,x),to=(int)fmin(width,ceil(x+w)),pixel;
        cairo_set_source_rgb(cr,.19,.22,.26); cairo_move_to(cr,0,y+79); cairo_line_to(cr,width,y+79); cairo_stroke(cr);
        if(t->mute) cairo_set_source_rgb(cr,.23,.26,.29);
        else if(i==a->selected) cairo_set_source_rgb(cr,.18,.46,.43);
        else cairo_set_source_rgb(cr,.14,.33,.34);
        cairo_rectangle(cr,x,y+8,w,64); cairo_fill(cr);
        cairo_set_source_rgb(cr,t->mute?.43:.58,t->mute?.46:.86,t->mute?.49:.79);
        for(pixel=from;pixel<to && a->peaks[i];++pixel) {
            double begin=fmax(0,(pixel-x)/scale)*MUSIC_RATE+t->trim_in;
            double end=fmin(t->trim_out,((pixel+1-x)/scale)*MUSIC_RATE+t->trim_in);
            uint32_t b=(uint32_t)begin/256,last=(uint32_t)fmax(begin,end)/256; unsigned peak=0;
            for(;b<=last && b<a->peak_count[i];++b) if(a->peaks[i][b]>peak) peak=a->peaks[i][b];
            double amplitude=fmin(1.0,(double)peak*t->gain/(32768.0*MUSIC_UNITY))*25;
            cairo_move_to(cr,pixel+.5,y+40-amplitude); cairo_line_to(cr,pixel+.5,y+40+amplitude);
        }
        cairo_stroke(cr);
        if(i==a->selected) { cairo_set_source_rgb(cr,.54,.85,.76); cairo_rectangle(cr,x+.5,y+8.5,fmax(0,w-1),63); cairo_stroke(cr); }
    }
    if(!a->project.count) {
        cairo_set_font_size(cr,22); cairo_set_source_rgb(cr,.85,.88,.9); cairo_move_to(cr,36,105); cairo_show_text(cr,"Start with a sound.");
        cairo_set_font_size(cr,14); cairo_move_to(cr,36,140); cairo_show_text(cr,"Add a WAV, record a take, or open the demo.");
        cairo_move_to(cr,36,165); cairo_show_text(cr,"Arrange clips here. Your audio stays on this computer.");
    }
    { double x=(double)a->position/MUSIC_RATE*scale;
      cairo_set_source_rgb(cr,1,.65,.31); cairo_set_line_width(cr,2); cairo_move_to(cr,x,30); cairo_line_to(cr,x,height); cairo_stroke(cr); }
}
static void refresh(App *a) {
    int active=audio_active(a->audio),idle=!a->busy&&!active; unsigned i; char text[256];
    GtkWidget *edit[]={a->add,a->open,a->save,a->export,a->new_button,a->demo,a->bpm,a->properties,a->canvas,a->list};
    for(i=0;i<G_N_ELEMENTS(edit);++i) gtk_widget_set_sensitive(edit[i],idle);
    gtk_widget_set_sensitive(a->properties,idle && a->project.count>0);
    gtk_widget_set_sensitive(a->export,idle && a->project.count>0);
    gtk_widget_set_sensitive(a->play,!a->busy && !audio_recording(a->audio) && a->project.count>0);
    gtk_widget_set_sensitive(a->record,!a->busy);
    gtk_widget_set_sensitive(a->stop,active); gtk_widget_set_visible(a->cancel,a->busy);
    if(a->busy)gtk_spinner_start(GTK_SPINNER(a->spinner));else gtk_spinner_stop(GTK_SPINNER(a->spinner));
    gtk_button_set_label(GTK_BUTTON(a->play),active?"Pause":"Play");
    gtk_button_set_label(GTK_BUTTON(a->record),audio_recording(a->audio)?"Finish take":"Record");
    snprintf(text,sizeof(text),"%02u:%05.2f",a->position/MUSIC_RATE/60,(double)a->position/MUSIC_RATE-floor((double)a->position/MUSIC_RATE/60)*60);
    gtk_label_set_text(GTK_LABEL(a->clock),text);
    for(i=0;i<a->project.count;++i) if(a->row_labels[i]) {
        char *valid=g_utf8_make_valid(a->project.tracks[i].name,-1);
        snprintf(text,sizeof(text),"%02u  %s%s%s",i+1,valid,a->project.tracks[i].mute?"  [M]":"",a->project.tracks[i].solo?"  [S]":"");
        gtk_label_set_text(GTK_LABEL(a->row_labels[i]),text); g_free(valid);
    }
    { char *name=a->file?g_file_get_basename(a->file):g_strdup("Untitled"),*valid=g_utf8_make_valid(name,-1);
      char *title=g_strdup_printf("%s%s — MUSIC",a->dirty?"• ":"",valid); gtk_window_set_title(GTK_WINDOW(a->window),title); g_free(title); g_free(valid); g_free(name); }
    gtk_widget_queue_draw(a->canvas);
}
static void properties(App *a) {
    MusicTrackData *t; a->updating=1; gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->bpm),a->project.bpm);
    if(!a->project.count) { a->updating=0; return; } t=&a->project.tracks[a->selected];
    char *valid=g_utf8_make_valid(t->name,-1); gtk_editable_set_text(GTK_EDITABLE(a->name),valid); g_free(valid);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(a->offset),0,(double)(MUSIC_TIMELINE_FRAMES-(t->trim_out-t->trim_in))/MUSIC_RATE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->offset),(double)t->offset/MUSIC_RATE);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(a->trim_in),0,(double)(t->trim_out-1)/MUSIC_RATE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->trim_in),(double)t->trim_in/MUSIC_RATE);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(a->trim_out),(double)(t->trim_in+1)/MUSIC_RATE,(double)t->frames/MUSIC_RATE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->trim_out),(double)t->trim_out/MUSIC_RATE);
    gtk_range_set_value(GTK_RANGE(a->gain),(double)t->gain/MUSIC_UNITY);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(a->mute),t->mute); gtk_check_button_set_active(GTK_CHECK_BUTTON(a->solo),t->solo);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->bpm),a->project.bpm); a->updating=0;
}
static void changed(GtkWidget *widget,gpointer data) {
    App *a=data; MusicTrackData *t; (void)widget;
    if(a->updating || a->busy || audio_active(a->audio) || !a->project.count) return;
    t=&a->project.tracks[a->selected];
    { const char *text=gtk_editable_get_text(GTK_EDITABLE(a->name)); size_t original=strlen(text),length=MIN(original,sizeof(t->name)-1);
      while(length && ((unsigned char)text[length]&0xc0)==0x80)--length;
      memcpy(t->name,text,length);t->name[length]=0;
      if(length<original){a->updating=1;gtk_editable_set_text(GTK_EDITABLE(a->name),t->name);a->updating=0;} }
    t->trim_in=(uint32_t)llround(gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->trim_in))*MUSIC_RATE);
    t->trim_out=(uint32_t)llround(gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->trim_out))*MUSIC_RATE);
    if(t->trim_out>t->frames) t->trim_out=t->frames;
    if(!t->trim_out) t->trim_out=1;
    if(t->trim_in>=t->trim_out) t->trim_in=t->trim_out-1;
    t->offset=(uint32_t)llround(gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->offset))*MUSIC_RATE);
    if(t->offset>MUSIC_TIMELINE_FRAMES-(t->trim_out-t->trim_in)) t->offset=MUSIC_TIMELINE_FRAMES-(t->trim_out-t->trim_in);
    t->gain=(uint32_t)llround(gtk_range_get_value(GTK_RANGE(a->gain))*MUSIC_UNITY);
    t->mute=gtk_check_button_get_active(GTK_CHECK_BUTTON(a->mute)); t->solo=gtk_check_button_get_active(GTK_CHECK_BUTTON(a->solo));
    a->dirty=1; if(widget!=a->name)properties(a); refresh(a);
}
static void bpm_changed(GtkSpinButton *spin,gpointer data) {
    App *a=data; if(a->updating || a->busy || audio_active(a->audio)) return;
    a->project.bpm=(unsigned)gtk_spin_button_get_value_as_int(spin); a->dirty=1; refresh(a);
}
static void drag_begin(GtkGestureDrag *gesture,double x,double y,gpointer data) {
    App *a=data; int i=(int)((y-40)/80); (void)gesture; a->drag_track=-1;
    if(a->busy || audio_active(a->audio)) return;
    if(y>=40 && i>=0 && (unsigned)i<a->project.count) {
        a->selected=(unsigned)i; a->drag_start=a->project.tracks[i].offset; a->drag_track=i; a->drag_seconds=view_seconds(a)/gtk_widget_get_width(a->canvas);
        gtk_list_box_select_row(GTK_LIST_BOX(a->list),gtk_list_box_get_row_at_index(GTK_LIST_BOX(a->list),i));
    } else {
        a->position=(uint32_t)fmin(MUSIC_TIMELINE_FRAMES-1,fmax(0,x/gtk_widget_get_width(a->canvas)*view_seconds(a)*MUSIC_RATE)); refresh(a);
    }
}
static void drag_update(GtkGestureDrag *gesture,double dx,double dy,gpointer data) {
    App *a=data; MusicTrackData *t; double frame; (void)gesture; (void)dy;
    if(a->drag_track<0 || a->busy || audio_active(a->audio)) return;
    t=&a->project.tracks[a->drag_track]; frame=a->drag_start+dx*a->drag_seconds*MUSIC_RATE;
    if(gtk_check_button_get_active(GTK_CHECK_BUTTON(a->snap))) { double beat=60.0*MUSIC_RATE/a->project.bpm; frame=round(frame/beat)*beat; }
    t->offset=(uint32_t)fmin(MUSIC_TIMELINE_FRAMES-(t->trim_out-t->trim_in),fmax(0,frame));
    a->dirty=1; properties(a); refresh(a);
}
static void stop_audio(App *a) {
    uint32_t frames,start; int16_t *captured; char error[256]; int was_recording=audio_recording(a->audio);
    if(!audio_active(a->audio)) return;
    captured=audio_stop(a->audio,&frames,&start); a->position=audio_position(a->audio);
    if(captured && frames) {
        if(music_add_pcm(&a->project,"Recorded take",captured,frames,start,error)) {
            a->dirty=1; a->selected=a->project.count-1; rebuild_peaks(a); rebuild_tracks(a); status(a,"Take added. Save the project to keep it.");
        } else { free(captured); status(a,error); }
    } else { free(captured); status(a,was_recording?"No audio frames were captured.":"Stopped."); }
    refresh(a);
}
static void play_clicked(GtkButton *b,gpointer data) {
    App *a=data; char error[256]; (void)b;
    if(a->busy || a->dialog_open)return;
    if(audio_active(a->audio)) { stop_audio(a); return; }
    if(a->position>=music_duration(&a->project)) a->position=0;
    if(!audio_start(a->audio,&a->project,a->position,0,error)) status(a,error); else status(a,"Playing locally."); refresh(a);
}
static void record_clicked(GtkButton *b,gpointer data) {
    App *a=data; char error[256]; (void)b;
    if(a->busy || a->dialog_open)return;
    if(audio_recording(a->audio)) { stop_audio(a); return; }
    stop_audio(a);
    if(!audio_start(a->audio,&a->project,a->position,1,error)) status(a,error);
    else status(a,"Recording the default input. Live microphone monitoring is off. Finish take to keep it."); refresh(a);
}
static void stop_clicked(GtkButton *b,gpointer data) { (void)b; stop_audio(data); }
static void rewind_clicked(GtkButton *b,gpointer data) { App *a=data; (void)b; if(a->busy)return; stop_audio(a); a->position=0; refresh(a); }
static void remove_clicked(GtkButton *b,gpointer data) {
    App *a=data; (void)b; if(!a->project.count || a->busy || audio_active(a->audio))return;
    music_remove(&a->project,a->selected); a->dirty=1; rebuild_peaks(a); rebuild_tracks(a);
}
static int stream_write(void *context,const void *data,size_t size) {
    FileJob *job=context; gsize written;
    return g_output_stream_write_all(job->stream,data,size,&written,job->app->cancel_io,&job->error);
}
static void file_worker(GTask *task,gpointer source,gpointer task_data,GCancellable *cancel) {
    FileJob *j=task_data; char error[256]; int ok=0; (void)source;
    if(j->operation==OP_IMPORT || j->operation==OP_OPEN) {
        GFileInfo *info=g_file_query_info(j->file,G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,G_FILE_QUERY_INFO_NONE,cancel,&j->error);
        if(info) {
            goffset size=g_file_info_get_size(info); guint64 limit=j->operation==OP_IMPORT?128u*1024u*1024u:MUSIC_MEMORY_LIMIT+16384u;
            if(g_file_info_get_file_type(info)!=G_FILE_TYPE_REGULAR || size<0 || (guint64)size>limit) j->message=g_strdup("Choose a regular local file within the preview's size limits.");
            else {
                char *bytes=NULL; gsize length=0;
                if(g_file_load_contents(j->file,cancel,&bytes,&length,NULL,&j->error)) {
                    if(j->operation==OP_OPEN) ok=music_load(&j->loaded,(const unsigned char*)bytes,length,error);
                    else { char *name=g_file_get_basename(j->file); ok=music_import_wav(&j->loaded,name,(const unsigned char*)bytes,length,error); g_free(name); }
                    if(!ok) j->message=g_strdup(error); g_free(bytes);
                }
            }
            g_object_unref(info);
        }
    } else {
        GFile *parent=g_file_get_parent(j->file),*temporary=NULL; char *uuid=g_uuid_string_random(),*name=g_strdup_printf(".music-%s.partial",uuid);
        if(parent) temporary=g_file_get_child(parent,name);
        if(temporary) j->stream=G_OUTPUT_STREAM(g_file_create(temporary,G_FILE_CREATE_PRIVATE,cancel,&j->error));
        if(j->stream) {
            ok=j->operation==OP_SAVE?music_save(&j->app->project,stream_write,j,error):music_export_wav(&j->app->project,stream_write,j,error);
            if(!ok && !j->error) j->message=g_strdup(error);
            if(ok) ok=g_output_stream_close(j->stream,cancel,&j->error);
            g_clear_object(&j->stream);
            if(ok && !g_cancellable_set_error_if_cancelled(cancel,&j->error))
                ok=g_file_move(temporary,j->file,G_FILE_COPY_OVERWRITE,NULL,NULL,NULL,&j->error);
            else ok=0;
        }
        if(temporary) { if(!ok) g_file_delete(temporary,NULL,NULL); g_object_unref(temporary); }
        g_clear_object(&parent); g_free(name); g_free(uuid);
    }
    g_task_return_boolean(task,ok);
}
static void job_free(gpointer data) {
    FileJob *j=data; music_free(&j->loaded); g_clear_object(&j->file); g_clear_object(&j->stream); g_clear_error(&j->error); g_free(j->message); g_free(j);
}
static void file_finished(GObject *object,GAsyncResult *result,gpointer data) {
    App *a=data; GTask *task=G_TASK(result); FileJob *j=g_task_get_task_data(task); int ok=g_task_propagate_boolean(task,NULL); char error[256]; (void)object;
    a->busy=0; g_clear_object(&a->cancel_io);
    if(ok && j->operation==OP_IMPORT) {
        MusicTrackData *t=&j->loaded.tracks[0];
        if(music_add_pcm(&a->project,t->name,t->samples,t->frames,0,error)) { t->samples=NULL; a->selected=a->project.count-1; a->dirty=1; status(a,"WAV added. Drag the clip or adjust its start and trim below."); }
        else { ok=0; status(a,error); }
    } else if(ok && j->operation==OP_OPEN) {
        music_free(&a->project); a->project=j->loaded; music_init(&j->loaded);
        g_set_object(&a->file,j->file); a->dirty=0; a->position=0; a->selected=0; status(a,"Project opened.");
    } else if(ok && j->operation==OP_SAVE) { g_set_object(&a->file,j->file); a->dirty=0; status(a,"Project saved, including its audio."); }
    else if(ok) status(a,"Stereo WAV exported.");
    else status(a,j->error?j->error->message:j->message?j->message:"File operation failed; the previous destination was kept.");
    rebuild_peaks(a); rebuild_tracks(a);
    if(a->closing_after_save) { a->closing_after_save=0; if(ok && j->operation==OP_SAVE) gtk_window_destroy(GTK_WINDOW(a->window)); }
}
static void start_file(App *a,GFile *file,int operation) {
    FileJob *job; GTask *task; char *path=g_file_get_path(file);
    if(!path) { status(a,"Choose a file on this computer."); return; } g_free(path);
    if(a->busy || audio_active(a->audio))return;
    job=g_new0(FileJob,1); job->app=a; job->file=g_object_ref(file); job->operation=operation; music_init(&job->loaded);
    a->busy=1; a->cancel_io=g_cancellable_new();
    status(a,operation==OP_IMPORT?"Reading WAV…":operation==OP_OPEN?"Opening project…":operation==OP_SAVE?"Saving project and audio…":"Rendering stereo WAV…"); refresh(a);
    task=g_task_new(NULL,a->cancel_io,file_finished,a); g_task_set_check_cancellable(task,FALSE);
    g_task_set_task_data(task,job,job_free); g_task_run_in_thread(task,file_worker); g_object_unref(task);
}
static void file_chosen(GObject *object,GAsyncResult *result,gpointer data) {
    Choice *choice=data; App *a=choice->app; GError *error=NULL; GFile *file;
    a->dialog_open=0;
    file=choice->operation==OP_IMPORT || choice->operation==OP_OPEN?gtk_file_dialog_open_finish(GTK_FILE_DIALOG(object),result,&error):gtk_file_dialog_save_finish(GTK_FILE_DIALOG(object),result,&error);
    if(file) { start_file(a,file,choice->operation); g_object_unref(file); }
    else { a->closing_after_save=0; if(error && !g_error_matches(error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_DISMISSED)) status(a,error->message); }
    g_clear_error(&error); g_free(choice);
}
static void choose_file(App *a,int operation) {
    GtkFileDialog *dialog; GtkFileFilter *filter; GListStore *filters; Choice *choice;
    if(a->busy || audio_active(a->audio) || a->dialog_open)return;
    dialog=gtk_file_dialog_new(); gtk_file_dialog_set_title(dialog,operation==OP_IMPORT?"Add WAV audio":operation==OP_OPEN?"Open MUSIC project":operation==OP_SAVE?"Save MUSIC project":"Export stereo WAV");
    gtk_file_dialog_set_modal(dialog,TRUE); filter=gtk_file_filter_new();
    gtk_file_filter_set_name(filter,operation==OP_IMPORT || operation==OP_EXPORT?"WAV audio":"MUSIC project");
    gtk_file_filter_add_pattern(filter,operation==OP_IMPORT || operation==OP_EXPORT?"*.wav":"*.music");
    if(operation==OP_IMPORT) gtk_file_filter_add_pattern(filter,"*.WAV");
    filters=g_list_store_new(GTK_TYPE_FILE_FILTER); g_list_store_append(filters,filter); gtk_file_dialog_set_filters(dialog,G_LIST_MODEL(filters));
    if(operation==OP_SAVE) gtk_file_dialog_set_initial_name(dialog,"Untitled.music");
    if(operation==OP_EXPORT) gtk_file_dialog_set_initial_name(dialog,"Mix.wav");
    choice=g_new0(Choice,1); choice->app=a; choice->operation=operation; a->dialog_open=1;
    if(operation==OP_IMPORT || operation==OP_OPEN) gtk_file_dialog_open(dialog,GTK_WINDOW(a->window),NULL,file_chosen,choice);
    else gtk_file_dialog_save(dialog,GTK_WINDOW(a->window),NULL,file_chosen,choice);
    g_object_unref(filters); g_object_unref(filter); g_object_unref(dialog);
}
static void perform_action(App *a,int action) {
    char error[256];
    if(action==CONFIRM_CLOSE) { gtk_window_destroy(GTK_WINDOW(a->window)); return; }
    if(action==CONFIRM_OPEN) { choose_file(a,OP_OPEN); return; }
    if(action==CONFIRM_DEMO) { if(!music_demo(&a->project,error)) {status(a,error);return;} a->dirty=1; }
    else { music_free(&a->project); a->dirty=0; }
    g_clear_object(&a->file); a->position=0; a->selected=0; rebuild_peaks(a); rebuild_tracks(a);
    status(a,action==CONFIRM_DEMO?"Demo sketch loaded. Nothing is playing or recording yet.":"New project. Add a WAV or record a take.");
}
static void confirm_finished(GObject *object,GAsyncResult *result,gpointer data) {
    App *a=data; GError *error=NULL; int choice=gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(object),result,&error);
    a->dialog_open=0;
    if(choice==1) perform_action(a,a->confirm_action);
    else if(choice==2 && a->confirm_action==CONFIRM_CLOSE) { a->closing_after_save=1; if(a->file)start_file(a,a->file,OP_SAVE);else choose_file(a,OP_SAVE); }
    g_clear_error(&error);
}
static void ask_discard(App *a,int action) {
    GtkAlertDialog *dialog; const char *buttons[]={"Cancel","Discard changes","Save and close",NULL};
    if(a->busy || a->dialog_open)return; stop_audio(a);
    if(!a->dirty) { perform_action(a,action); return; }
    dialog=gtk_alert_dialog_new("Keep your changes?"); gtk_alert_dialog_set_detail(dialog,"This project has unsaved edits or recorded audio.");
    if(action!=CONFIRM_CLOSE) buttons[2]=NULL;
    gtk_alert_dialog_set_buttons(dialog,buttons); gtk_alert_dialog_set_cancel_button(dialog,0); gtk_alert_dialog_set_default_button(dialog,0);
    a->confirm_action=action; a->dialog_open=1; gtk_alert_dialog_choose(dialog,GTK_WINDOW(a->window),NULL,confirm_finished,a); g_object_unref(dialog);
}
static void new_clicked(GtkButton *b,gpointer data) {(void)b;ask_discard(data,CONFIRM_NEW);}
static void open_clicked(GtkButton *b,gpointer data) {(void)b;ask_discard(data,CONFIRM_OPEN);}
static void demo_clicked(GtkButton *b,gpointer data) {(void)b;ask_discard(data,CONFIRM_DEMO);}
static void add_clicked(GtkButton *b,gpointer data) {(void)b;choose_file(data,OP_IMPORT);}
static void save_clicked(GtkButton *b,gpointer data) {App *a=data;(void)b;if(a->file)start_file(a,a->file,OP_SAVE);else choose_file(a,OP_SAVE);}
static void export_clicked(GtkButton *b,gpointer data) {(void)b;choose_file(data,OP_EXPORT);}
static void cancel_clicked(GtkButton *b,gpointer data) {App *a=data;(void)b;if(a->cancel_io)g_cancellable_cancel(a->cancel_io);}
static gboolean close_requested(GtkWindow *window,gpointer data) {
    App *a=data;(void)window;
    if(a->busy) { status(a,"Finish or cancel the file operation before closing.");return TRUE; }
    if(a->smoke) return FALSE;
    ask_discard(a,CONFIRM_CLOSE); return TRUE;
}
static gboolean drop(GtkDropTarget *target,const GValue *value,double x,double y,gpointer data) {
    App *a=data; GSList *files; (void)target;(void)x;(void)y;
    if(a->busy || a->dialog_open || audio_active(a->audio))return FALSE;
    files=gdk_file_list_get_files(g_value_get_boxed(value));
    if(!files)return FALSE;
    if(files->next) {status(a,"Add one WAV at a time in this preview.");return FALSE;}
    start_file(a,G_FILE(files->data),OP_IMPORT);return TRUE;
}
static GtkWidget *field(GtkWidget *box,const char *title,GtkWidget *control) {
    GtkWidget *column=gtk_box_new(GTK_ORIENTATION_VERTICAL,5),*caption=label(title);
    gtk_widget_add_css_class(caption,"dim-label"); gtk_box_append(GTK_BOX(column),caption); gtk_box_append(GTK_BOX(column),control);
    gtk_box_append(GTK_BOX(box),column);return control;
}
static gboolean tick(gpointer data) {
    App *a=data;
    if(audio_active(a->audio)) {
        a->position=audio_position(a->audio);
        if(audio_finished(a->audio)) { int record=audio_recording(a->audio); stop_audio(a); if(record)status(a,"Recording stopped at the preview's take or memory limit. Save the project to keep it."); }
        refresh(a);
    }
    if(a->smoke) {
        ++a->smoke_ticks;
        if(a->smoke_ticks==10) {
            char error[256]; if(!music_demo(&a->project,error))a->smoke_failed=1; rebuild_peaks(a);rebuild_tracks(a);
            gtk_editable_set_text(GTK_EDITABLE(a->name),"Bass sketch — MIDI comes later");
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->offset),.5);gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->trim_in),.25);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->trim_out),7.5);gtk_range_set_value(GTK_RANGE(a->gain),.25);
            play_clicked(NULL,a);if(!audio_active(a->audio))a->smoke_failed=1;
        }
        if(a->smoke_ticks==25) { if(!audio_position(a->audio))a->smoke_failed=1;stop_audio(a);record_clicked(NULL,a);if(!audio_recording(a->audio))a->smoke_failed=1; }
        if(a->smoke_ticks==40) {stop_audio(a);if(a->project.count!=4 || !music_valid(&a->project))a->smoke_failed=1;a->smoke_stage=1;a->smoke_dir=g_dir_make_tmp("music-smoke-XXXXXX",NULL);if(!a->smoke_dir)a->smoke_failed=1;}
        if(a->smoke_ticks>=45 && !a->busy && a->smoke_dir && a->smoke_stage>=1 && a->smoke_stage<=3) {
            char *path=g_build_filename(a->smoke_dir,a->smoke_stage==2?"Mix.wav":"Session.music",NULL);GFile *file=g_file_new_for_path(path);
            if(a->smoke_stage==2 && a->dirty)a->smoke_failed=1;
            start_file(a,file,a->smoke_stage==1?OP_SAVE:a->smoke_stage==2?OP_EXPORT:OP_OPEN);
            ++a->smoke_stage;g_object_unref(file);g_free(path);
        }
        if(a->smoke_stage==4 && !a->busy) {
            char *path=g_build_filename(a->smoke_dir,"Mix.wav",NULL),*bytes=NULL;gsize size=0;MusicProject mix;char error[256];music_init(&mix);
            if(a->project.count!=4 || a->dirty || !g_file_get_contents(path,&bytes,&size,NULL) || !music_import_wav(&mix,"export",(unsigned char*)bytes,size,error) || music_duration(&mix)!=music_duration(&a->project))a->smoke_failed=1;
            if(a->project.tracks[0].offset!=24000 || a->project.tracks[0].trim_in!=12000 || a->project.tracks[0].trim_out!=360000 ||
               a->project.tracks[0].gain!=MUSIC_UNITY/4 || strcmp(a->project.tracks[0].name,"Bass sketch — MIDI comes later"))a->smoke_failed=1;
            music_free(&mix);g_free(bytes);g_free(path);a->smoke_stage=5;
        }
        if(a->smoke_ticks>=750) {a->smoke_failed=1;if(a->cancel_io)g_cancellable_cancel(a->cancel_io);}
        if(a->smoke_ticks>=90 && !a->busy && (a->smoke_stage==5 || a->smoke_failed)) {
            g_print("GTK/audio smoke: %s (null device, playback, capture, save, export, reopen; no physical microphone)\n",a->smoke_failed?"FAIL":"PASS");
            a->timer=0;gtk_window_destroy(GTK_WINDOW(a->window));return G_SOURCE_REMOVE;
        }
    }
    return G_SOURCE_CONTINUE;
}
static void activate(GtkApplication *application,gpointer data) {
    App *a=data; GtkWidget *root,*header,*title,*transport,*scroll,*tracks,*left,*heading,*row,*footer;
    GtkGesture *gesture; GtkDropTarget *target;
    a->application=application;a->window=gtk_application_window_new(application);gtk_window_set_default_size(GTK_WINDOW(a->window),1160,760);
    g_signal_connect(a->window,"close-request",G_CALLBACK(close_requested),a);
    header=gtk_header_bar_new();title=gtk_label_new("MUSIC");gtk_widget_add_css_class(title,"title");gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),title);
    a->new_button=button("New",G_CALLBACK(new_clicked),a);a->open=button("Open",G_CALLBACK(open_clicked),a);a->save=button("Save",G_CALLBACK(save_clicked),a);a->export=button("Export WAV",G_CALLBACK(export_clicked),a);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header),a->new_button);gtk_header_bar_pack_start(GTK_HEADER_BAR(header),a->open);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header),a->export);gtk_header_bar_pack_end(GTK_HEADER_BAR(header),a->save);gtk_window_set_titlebar(GTK_WINDOW(a->window),header);
    root=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);margins(root,18);gtk_window_set_child(GTK_WINDOW(a->window),root);
    transport=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_append(GTK_BOX(root),transport);
    a->add=button("Add WAV…",G_CALLBACK(add_clicked),a);gtk_box_append(GTK_BOX(transport),a->add);
    gtk_box_append(GTK_BOX(transport),button("|‹",G_CALLBACK(rewind_clicked),a));a->play=button("Play",G_CALLBACK(play_clicked),a);gtk_widget_add_css_class(a->play,"suggested-action");gtk_box_append(GTK_BOX(transport),a->play);
    a->stop=button("Stop",G_CALLBACK(stop_clicked),a);gtk_box_append(GTK_BOX(transport),a->stop);a->record=button("Record",G_CALLBACK(record_clicked),a);gtk_box_append(GTK_BOX(transport),a->record);
    a->clock=label("00:00.00");gtk_widget_add_css_class(a->clock,"title-2");gtk_widget_set_hexpand(a->clock,TRUE);gtk_label_set_xalign(GTK_LABEL(a->clock),.5);gtk_box_append(GTK_BOX(transport),a->clock);
    gtk_box_append(GTK_BOX(transport),label("BPM"));a->bpm=gtk_spin_button_new_with_range(40,240,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->bpm),120);gtk_widget_set_tooltip_text(a->bpm,"Arrangement grid only; changing tempo does not stretch audio.");gtk_box_append(GTK_BOX(transport),a->bpm);g_signal_connect(a->bpm,"value-changed",G_CALLBACK(bpm_changed),a);
    a->snap=gtk_check_button_new_with_label("Snap");gtk_check_button_set_active(GTK_CHECK_BUTTON(a->snap),TRUE);gtk_box_append(GTK_BOX(transport),a->snap);
    a->demo=button("Demo",G_CALLBACK(demo_clicked),a);gtk_box_append(GTK_BOX(transport),a->demo);
    scroll=gtk_scrolled_window_new();gtk_widget_set_vexpand(scroll,TRUE);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_box_append(GTK_BOX(root),scroll);
    tracks=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),tracks);
    left=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);heading=label("  TRACKS");gtk_widget_set_size_request(heading,190,40);gtk_box_append(GTK_BOX(left),heading);
    a->list=gtk_list_box_new();gtk_widget_set_size_request(a->list,190,-1);gtk_box_append(GTK_BOX(left),a->list);g_signal_connect(a->list,"row-selected",G_CALLBACK(select_row),a);gtk_box_append(GTK_BOX(tracks),left);
    a->canvas=gtk_drawing_area_new();gtk_widget_set_hexpand(a->canvas,TRUE);gtk_widget_set_size_request(a->canvas,520,320);gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a->canvas),draw,a,NULL);gtk_box_append(GTK_BOX(tracks),a->canvas);
    gesture=gtk_gesture_drag_new();g_signal_connect(gesture,"drag-begin",G_CALLBACK(drag_begin),a);g_signal_connect(gesture,"drag-update",G_CALLBACK(drag_update),a);gtk_widget_add_controller(a->canvas,GTK_EVENT_CONTROLLER(gesture));
    target=gtk_drop_target_new(GDK_TYPE_FILE_LIST,GDK_ACTION_COPY);g_signal_connect(target,"drop",G_CALLBACK(drop),a);gtk_widget_add_controller(a->window,GTK_EVENT_CONTROLLER(target));
    a->properties=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);gtk_box_append(GTK_BOX(root),a->properties);
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);gtk_box_append(GTK_BOX(a->properties),row);a->name=field(row,"Selected track",gtk_entry_new());gtk_widget_set_hexpand(a->name,TRUE);g_signal_connect(a->name,"changed",G_CALLBACK(changed),a);
    a->offset=field(row,"Start (s)",gtk_spin_button_new_with_range(0,1800,.01));a->trim_in=field(row,"Trim in (s)",gtk_spin_button_new_with_range(0,600,.01));a->trim_out=field(row,"Trim out (s)",gtk_spin_button_new_with_range(0,600,.01));
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(a->offset),3);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(a->trim_in),3);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(a->trim_out),3);
    g_signal_connect(a->offset,"value-changed",G_CALLBACK(changed),a);g_signal_connect(a->trim_in,"value-changed",G_CALLBACK(changed),a);g_signal_connect(a->trim_out,"value-changed",G_CALLBACK(changed),a);
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);gtk_box_append(GTK_BOX(a->properties),row);gtk_box_append(GTK_BOX(row),label("Gain"));a->gain=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0,4,.01);gtk_widget_set_hexpand(a->gain,TRUE);gtk_scale_set_digits(GTK_SCALE(a->gain),2);gtk_box_append(GTK_BOX(row),a->gain);g_signal_connect(a->gain,"value-changed",G_CALLBACK(changed),a);
    a->mute=gtk_check_button_new_with_label("Mute");a->solo=gtk_check_button_new_with_label("Solo");gtk_box_append(GTK_BOX(row),a->mute);gtk_box_append(GTK_BOX(row),a->solo);g_signal_connect(a->mute,"toggled",G_CALLBACK(changed),a);g_signal_connect(a->solo,"toggled",G_CALLBACK(changed),a);
    a->remove=button("Remove track",G_CALLBACK(remove_clicked),a);gtk_box_append(GTK_BOX(row),a->remove);
    footer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);a->status=label("Local multitrack audio. Add a WAV, record a take, or open the demo.");gtk_label_set_wrap(GTK_LABEL(a->status),TRUE);gtk_widget_set_hexpand(a->status,TRUE);gtk_box_append(GTK_BOX(footer),a->status);a->spinner=gtk_spinner_new();gtk_box_append(GTK_BOX(footer),a->spinner);a->cancel=button("Cancel",G_CALLBACK(cancel_clicked),a);gtk_box_append(GTK_BOX(footer),a->cancel);gtk_box_append(GTK_BOX(root),footer);
    a->audio=audio_new(a->smoke);rebuild_tracks(a);a->timer=g_timeout_add(40,tick,a);gtk_window_present(GTK_WINDOW(a->window));
}
int main(int argc,char **argv) {
    App a; GtkApplication *application; int result; unsigned i; memset(&a,0,sizeof(a));music_init(&a.project);
    if(argc==2 && !strcmp(argv[1],"--version")) {g_print("MUSIC %s\n",MUSIC_VERSION);return 0;}
    if(argc==2 && !strcmp(argv[1],"--smoke")) {a.smoke=1;argc=1;}
    application=gtk_application_new("org.qsolkcb.Music",G_APPLICATION_NON_UNIQUE);g_signal_connect(application,"activate",G_CALLBACK(activate),&a);
    result=g_application_run(G_APPLICATION(application),argc,argv);
    if(a.timer)g_source_remove(a.timer);audio_free(a.audio);music_free(&a.project);g_clear_object(&a.file);g_clear_object(&a.cancel_io);
    for(i=0;i<MUSIC_TRACKS;++i)free(a.peaks[i]);
    if(a.smoke_dir) {const char *names[]={"Mix.wav","Session.music"};for(i=0;i<2;++i){char *path=g_build_filename(a.smoke_dir,names[i],NULL);GFile *file=g_file_new_for_path(path);g_file_delete(file,NULL,NULL);g_object_unref(file);g_free(path);}GFile *dir=g_file_new_for_path(a.smoke_dir);g_file_delete(dir,NULL,NULL);g_object_unref(dir);g_free(a.smoke_dir);}
    g_object_unref(application);return a.smoke_failed?1:result;
}
