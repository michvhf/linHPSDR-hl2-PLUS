/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

#include <gtk/gtk.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

#include "discovered.h"
#include "bpsk.h"
#include "mode.h"
#include "filter.h"
#include "band.h"
#include "receiver.h"
#include "transmitter.h"
#include "receiver.h"
#include "wideband.h"
#include "adc.h"
#include "dac.h"
#include "radio.h"
#include "midi.h"
#include "alsa_midi.h"
#include "midi_dialog.h"
#include "property.h"

enum {
  EVENT_COLUMN,
  CHANNEL_COLUMN,
  NOTE_COLUMN,
  TYPE_COLUMN,
  ACTION_COLUMN,
  N_COLUMNS
};

static GtkWidget *midi_enable_b;

static GtkListStore *store;
static GtkWidget *view;
static GtkWidget *scrolled_window=NULL;
static gulong selection_signal_id;
static GtkTreeModel *model;
static GtkTreeIter iter;
struct desc *current_cmd;

static GtkWidget *filename;

static GtkWidget *newEvent;
static GtkWidget *newChannel;
static GtkWidget *newNote;
static GtkWidget *newVal;
static GtkWidget *newType;
static GtkWidget *newMin;
static GtkWidget *newMax;
static GtkWidget *newAction;
static GtkWidget *configure_b;
static GtkWidget *add_b;
static GtkWidget *update_b;
static GtkWidget *delete_b;

static enum MIDIevent thisEvent=EVENT_NONE;
static int thisChannel;
static int thisNote;
static int thisVal;
static int thisMin;
static int thisMax;
static enum MIDItype thisType;
static enum MIDIaction thisAction;

static gint device=-1;
gchar *midi_device_name=NULL;


enum {
  UPDATE_NEW,
  UPDATE_CURRENT,
  UPDATE_EXISTING
};

static int update(void *data);
static void load_store();

static gboolean midi_enable_cb(GtkWidget *widget,gpointer data) {
  RADIO *r=(RADIO *)data;
  if(r->midi_enabled) {
    close_midi_device();
  }
  r->midi_enabled=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (widget));
  if(r->midi_enabled && midi_device_name!=NULL) {
    if(register_midi_device(midi_device_name)<0) {
      r->midi_enabled=false;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (widget), r->midi_enabled);
    }
  }
  return TRUE;
}

static void configure_cb(GtkWidget *widget, gpointer data) {
  gboolean conf=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (widget));
  configure_midi_device(conf);
}

static void device_changed_cb(GtkWidget *widget, gpointer data) {
  RADIO *r=(RADIO *)data;
  device = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  if(midi_device_name!=NULL) {
    g_free(midi_device_name);
  }
  midi_device_name=g_new(gchar,strlen(midi_devices[device].name)+1);
  strcpy(midi_device_name,midi_devices[device].name);
  if(r->midi_enabled) {
    close_midi_device();
    if(register_midi_device(midi_device_name)) {
      r->midi_enabled=false;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(midi_enable_b), r->midi_enabled);
    }
  }
}

static void type_changed_cb(GtkWidget *widget, gpointer data) {
  int i=1;
  int j=1;

  // update actions available for the type
  gchar *type=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));

  //g_print("%s: type=%s\n",__FUNCTION__,type);
  gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(newAction));
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newAction),NULL,ActionTable[0].str);
  if(type==NULL || strcmp(type,"NONE")==0) {
    // leave empty
    gtk_combo_box_set_active (GTK_COMBO_BOX(newAction),0);
  } else if(strcmp(type,"KEY")==0) {
    // add all the Key actions
    while(ActionTable[i].action!=ACTION_NONE) {
      if(ActionTable[i].type&MIDI_KEY) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newAction),NULL,ActionTable[i].str);
	if(ActionTable[i].action==thisAction) {
          gtk_combo_box_set_active (GTK_COMBO_BOX(newAction),j);
	}
	j++;
      }
      i++;
    }
  } else if(strcmp(type,"KNOB/SLIDER")==0) {
    // add all the Knob actions
    while(ActionTable[i].action!=ACTION_NONE) {
      if(ActionTable[i].type&MIDI_KNOB) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newAction),NULL,ActionTable[i].str);
	if(ActionTable[i].action==thisAction) {
          gtk_combo_box_set_active (GTK_COMBO_BOX(newAction),j);
	}
	j++;
      }
      i++;
    }
  } else if(strcmp(type,"WHEEL")==0) {
    // add all the Wheel actions
    while(ActionTable[i].action!=ACTION_NONE) {
      if(ActionTable[i].type&MIDI_WHEEL || ActionTable[i].type&MIDI_KNOB) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newAction),NULL,ActionTable[i].str);
	if(ActionTable[i].action==thisAction) {
          gtk_combo_box_set_active (GTK_COMBO_BOX(newAction),j);
	}
	j++;
      }
      i++;
    }
  }
}

static void row_inserted_cb(GtkTreeModel *tree_model,GtkTreePath *path, GtkTreeIter *iter,gpointer user_data) {
  //g_print("%s\n",__FUNCTION__);
  gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,false);
}


static void tree_selection_changed_cb (GtkTreeSelection *selection, gpointer data) {
  char *str_event;
  char *str_channel;
  char *str_note;
  char *str_type;
  char *str_action;

  //g_print("%s\n",__FUNCTION__);
  //if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(configure_b))) {
    if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
      gtk_tree_model_get(model, &iter, EVENT_COLUMN, &str_event, -1);
      gtk_tree_model_get(model, &iter, CHANNEL_COLUMN, &str_channel, -1);
      gtk_tree_model_get(model, &iter, NOTE_COLUMN, &str_note, -1);
      gtk_tree_model_get(model, &iter, TYPE_COLUMN, &str_type, -1);
      gtk_tree_model_get(model, &iter, ACTION_COLUMN, &str_action, -1);

      //g_print("%s: %s %s %s %s %s\n",__FUNCTION__,str_event,str_channel,str_note,str_type,str_action);

      if(str_event!=NULL && str_channel!=NULL && str_note!=NULL && str_type!=NULL && str_action!=NULL) {

        if(strcmp(str_event,"CTRL")==0) {
          thisEvent=MIDI_CTRL;
        } else if(strcmp(str_event,"PITCH")==0) {
          thisEvent=MIDI_PITCH;
        } else if(strcmp(str_event,"NOTE")==0) {
          thisEvent=MIDI_NOTE;
        } else {
          thisEvent=EVENT_NONE;
        }
        thisChannel=atoi(str_channel);
        thisNote=atoi(str_note);
        thisVal=0;
        thisMin=0;
        thisMax=0;
        if(strcmp(str_type,"KEY")==0) {
          thisType=MIDI_KEY;
        } else if(strcmp(str_type,"KNOB/SLIDER")==0) {
          thisType=MIDI_KNOB;
        } else if(strcmp(str_type,"WHEEL")==0) {
          thisType=MIDI_WHEEL;
        } else {
          thisType=TYPE_NONE;
        }
        thisAction=ACTION_NONE;
        int i=1;
        while(ActionTable[i].action!=ACTION_NONE) {
          if(strcmp(ActionTable[i].str,str_action)==0) {
            thisAction=ActionTable[i].action;
            break;
          }
          i++;
        }
        g_idle_add(update,GINT_TO_POINTER(UPDATE_EXISTING));
      }
    }
  //}
}

static void find_current_cmd() {
  struct desc *cmd;
  //g_print("%s:\n",__FUNCTION__);
  cmd=MidiCommandsTable.desc[thisNote];
  while(cmd!=NULL) {
    if((cmd->channel==thisChannel || cmd->channel==-1) && cmd->type==thisType && cmd->action==thisAction) {
  //g_print("%s: found cmd\n",__FUNCTION__);
      break;
    }
    cmd=cmd->next;
  }
  current_cmd=cmd;
}

static void clear_cb(GtkWidget *widget,gpointer user_data) {
  struct desc *cmd;
  struct desc *next;
  for(int i=0;i<128;i++) {
    cmd=MidiCommandsTable.desc[i];
    while(cmd!=NULL) {
      next=cmd->next;
      g_free(cmd);
      cmd=next;
    }
    MidiCommandsTable.desc[i]=NULL;
  }
  gtk_list_store_clear(store);
}

static void save_cb(GtkWidget *widget,gpointer user_data) {
  RADIO *r=(RADIO *)user_data;
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
  gchar *filename;
  gint res;
  struct desc *cmd;

  dialog = gtk_file_chooser_dialog_new ("Save File",
                                      GTK_WINDOW(r->dialog),
                                      action,
                                      "_Cancel",
                                      GTK_RESPONSE_CANCEL,
                                      "_Save",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);
  if(midi_device_name==NULL) {
    filename=g_new(gchar,10);
    sprintf(filename,"midi.midi");
  } else {
    filename=g_new(gchar,strlen(midi_device_name)+6);
    sprintf(filename,"%s.midi",midi_device_name);
  }
  gtk_file_chooser_set_current_name(chooser,filename);
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if(res==GTK_RESPONSE_ACCEPT) {
    char *savefilename=gtk_file_chooser_get_filename(chooser);
    initProperties();
    midi_save_state();
    saveProperties(savefilename);
    g_free(savefilename);
  }
  gtk_widget_destroy(dialog);
  g_free(filename);
}

static void load_cb(GtkWidget *widget,gpointer user_data) {
  RADIO *r=(RADIO *)user_data;
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
  gchar *filename;
  gint res;
  struct desc *cmd;

  dialog = gtk_file_chooser_dialog_new ("Open MIDI File",
                                      GTK_WINDOW(r->dialog),
                                      action,
                                      "_Cancel",
                                      GTK_RESPONSE_CANCEL,
                                      "_Save",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  if(midi_device_name==NULL) {
    filename=g_new(gchar,10);
    sprintf(filename,"midi.midi");
  } else {
    filename=g_new(gchar,strlen(midi_device_name)+6);
    sprintf(filename,"%s.midi",midi_device_name);
  }
  gtk_file_chooser_set_current_name(chooser,filename);
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if(res==GTK_RESPONSE_ACCEPT) {
    char *loadfilename=gtk_file_chooser_get_filename(chooser);
    clear_cb(NULL,NULL);
    initProperties();
    loadProperties(loadfilename);
    midi_restore_state();
    load_store();
    g_free(loadfilename);
  }
  gtk_widget_destroy(dialog);
  g_free(filename);
}

static void load_original_cb(GtkWidget *widget,gpointer user_data) {
  RADIO *r=(RADIO *)user_data;
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
  gchar *filename;
  gint res;
  struct desc *cmd;

  dialog = gtk_file_chooser_dialog_new ("Open ORIGINAL MIDI File",
                                      GTK_WINDOW(r->dialog),
                                      action,
                                      "_Cancel",
                                      GTK_RESPONSE_CANCEL,
                                      "_Save",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  filename=g_new(gchar,strlen(midi_device_name)+6);
  sprintf(filename,"%s.midi",midi_device_name);
  gtk_file_chooser_set_current_name(chooser,filename);
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if(res==GTK_RESPONSE_ACCEPT) {
    char *loadfilename=gtk_file_chooser_get_filename(chooser);
    clear_cb(NULL,NULL);
    MIDIstartup(loadfilename);
    load_store();
    g_free(loadfilename);
  }
  gtk_widget_destroy(dialog);
  g_free(filename);
}

static void add_store(int key,struct desc *cmd) {
  char str_event[16];
  char str_channel[16];
  char str_note[16];
  char str_type[32];
  char str_action[32];

  switch(cmd->event) {
    case EVENT_NONE:
      strcpy(str_event,"NONE");
      break;
    case MIDI_NOTE:
      strcpy(str_event,"NOTE");
      break;
    case MIDI_CTRL:
      strcpy(str_event,"CTRL");
      break;
    case MIDI_PITCH:
      strcpy(str_event,"PITCH");
      break;
  }
  sprintf(str_channel,"%d",cmd->channel);
  sprintf(str_note,"%d",key);
  switch(cmd->type) {
    case TYPE_NONE:
      strcpy(str_type,"NONE");
      break;
    case MIDI_KEY:
      strcpy(str_type,"KEY");
      break;
    case MIDI_KNOB:
      strcpy(str_type,"KNOB/SLIDER");
      break;
    case MIDI_WHEEL:
      strcpy(str_type,"WHEEL");
      break;
  }
  strcpy(str_action,ActionTable[cmd->action].str);
  gtk_list_store_prepend(store,&iter);
  gtk_list_store_set(store,&iter,
      EVENT_COLUMN,str_event,
      CHANNEL_COLUMN,str_channel,
      NOTE_COLUMN,str_note,
      TYPE_COLUMN,str_type,
      ACTION_COLUMN,str_action,
      -1);

  if(scrolled_window!=NULL) {
    GtkAdjustment *adjustment=gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW(scrolled_window));
    //g_print("%s: adjustment=%f lower=%f upper=%f\n",__FUNCTION__,gtk_adjustment_get_value(adjustment),gtk_adjustment_get_lower(adjustment),gtk_adjustment_get_upper(adjustment));
    if(gtk_adjustment_get_value(adjustment)!=0.0) {
      gtk_adjustment_set_value(adjustment,0.0);
    }
  }
}

static void load_store() {
  struct desc *cmd;
  gtk_list_store_clear(store);
  for(int i=0;i<128;i++) {
    cmd=MidiCommandsTable.desc[i];
    while(cmd!=NULL) {
      add_store(i,cmd);
      cmd=cmd->next;
    }
  }
}

static void add_cb(GtkButton *widget,gpointer user_data) {

  gchar *str_type=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(newType));
  gchar *str_action=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(newAction));
;

  gint i;
  gint type;
  gint action;

  if(strcmp(str_type,"KEY")==0) {
    type=MIDI_KEY;
  } else if(strcmp(str_type,"KNOB/SLIDER")==0) {
    type=MIDI_KNOB;
  } else if(strcmp(str_type,"WHEEL")==0) {
    type=MIDI_WHEEL;
  } else {
    type=TYPE_NONE;
  }

  action=ACTION_NONE;
  i=1;
  while(ActionTable[i].action!=ACTION_NONE) {
    if(strcmp(ActionTable[i].str,str_action)==0) {
      action=ActionTable[i].action;
      break;
    }
    i++;
  }

  //g_print("%s: type=%s (%d) action=%s (%d)\n",__FUNCTION__,str_type,type,str_action,action);

  struct desc *desc;
  desc = (struct desc *) malloc(sizeof(struct desc));
  desc->next = NULL;
  desc->action = action; // MIDIaction
  desc->type = type; // MIDItype
  desc->event = thisEvent; // MIDevent
  desc->onoff = 0;
  if(type==MIDI_KEY && (action==CWLEFT || action==CWRIGHT || action==MIDI_PTT)) {
      desc->onoff = 1;
  }
  desc->delay = 0;
  desc->vfl1  = -1;
  desc->vfl2  = 57;
  desc->fl1   = 57;
  desc->fl2   = 60;
  desc->lft1  = 60;
  desc->lft2  = 63;
  desc->rgt1  = 64;
  desc->rgt2  = 67;
  desc->fr1   = 67;
  desc->fr2   = 73;
  desc->vfr1  = 73;
  desc->vfr2  = 128;
  desc->channel  = thisChannel;

  gint key=thisNote;
  if(key<0) key=0;
  if(key>127) key=0;


  if(MidiCommandsTable.desc[key]!=NULL) {
    desc->next=MidiCommandsTable.desc[key];
  }
  MidiCommandsTable.desc[key]=desc;
    
  add_store(key,desc);

  gtk_widget_set_sensitive(add_b,false);
  gtk_widget_set_sensitive(update_b,true);
  gtk_widget_set_sensitive(delete_b,true);

}

static void update_cb(GtkButton *widget,gpointer user_data) {
  char str_event[16];
  char str_channel[16];
  char str_note[16];
  int i;


  gchar *str_type=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(newType));
  gchar *str_action=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(newAction));
;
  //g_print("%s: type=%s action=%s\n",__FUNCTION__,str_type,str_action);

  if(strcmp(str_type,"KEY")==0) {
    thisType=MIDI_KEY;
  } else if(strcmp(str_type,"KNOB/SLIDER")==0) {
    thisType=MIDI_KNOB;
  } else if(strcmp(str_type,"WHEEL")==0) {
    thisType=MIDI_WHEEL;
  } else {
    thisType=TYPE_NONE;
  }

  thisAction=ACTION_NONE;
  i=1;
  while(ActionTable[i].action!=ACTION_NONE) {
    if(strcmp(ActionTable[i].str,str_action)==0) {
      thisAction=ActionTable[i].action;
      break;
    }
    i++;
  }

  current_cmd->channel=thisChannel;
  current_cmd->type=thisType;
  current_cmd->action=thisAction;

  switch(current_cmd->event) {
    case EVENT_NONE:
      strcpy(str_event,"NONE");
      break;
    case MIDI_NOTE:
      strcpy(str_event,"NOTE");
      break;
    case MIDI_CTRL:
      strcpy(str_event,"CTRL");
      break;
    case MIDI_PITCH:
      strcpy(str_event,"PITCH");
      break;
  }
  sprintf(str_channel,"%d",current_cmd->channel);
  sprintf(str_note,"%d",thisNote);

  //g_print("%s: event=%s channel=%s note=%s type=%s action=%s\n",
  //        __FUNCTION__,str_event,str_channel,str_note,str_type,str_action);
  gtk_list_store_set(store,&iter,
      EVENT_COLUMN,str_event,
      CHANNEL_COLUMN,str_channel,
      NOTE_COLUMN,str_note,
      TYPE_COLUMN,str_type,
      ACTION_COLUMN,str_action,
      -1);
}

static void delete_cb(GtkButton *widget,gpointer user_data) {
  struct desc *previous_cmd;
  struct desc *next_cmd;
  GtkTreeIter saved_iter;

  //g_print("%s\n",__FUNCTION__);
  saved_iter=iter;


  // remove from MidiCommandsTable
  if(MidiCommandsTable.desc[thisNote]==current_cmd) {
    MidiCommandsTable.desc[thisNote]=current_cmd->next;
    g_free(current_cmd);
  } else {
    previous_cmd=MidiCommandsTable.desc[thisNote];
    while(previous_cmd->next!=NULL) {
      next_cmd=previous_cmd->next;
      if(next_cmd==current_cmd) {
	previous_cmd->next=next_cmd->next;
	g_free(next_cmd);
	break;
      }
      previous_cmd=next_cmd;
    }
  }

  // remove from list store
  gtk_list_store_remove(store,&saved_iter);

  gtk_widget_set_sensitive(add_b,true);
  gtk_widget_set_sensitive(update_b,false);
  gtk_widget_set_sensitive(delete_b,false);

}

GtkWidget *create_midi_dialog(RADIO *r) {
  int i;
  int col=0;
  int row=0;
  GtkCellRenderer *renderer;

  GtkWidget *midi_frame=gtk_frame_new("MIDI");
  GtkWidget *grid=gtk_grid_new();
  gtk_grid_set_row_homogeneous(GTK_GRID(grid),FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid),FALSE);
  gtk_grid_set_column_spacing(GTK_GRID(grid),5);
  gtk_container_add(GTK_CONTAINER(midi_frame),grid);

  row=0;
  col=0;

  get_midi_devices();
  if(n_midi_devices>0) {
    GtkWidget *devices_label=gtk_label_new("Select MIDI device: ");
    gtk_grid_attach(GTK_GRID(grid),devices_label,col,row,1,1);
    col++;

    GtkWidget *devices=gtk_combo_box_text_new();
    for(int i=0;i<n_midi_devices;i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(devices),NULL,midi_devices[i].name);
      if(midi_device_name!=NULL) {
        if(strcmp(midi_device_name,midi_devices[i].name)==0) {
          device=i;
        }
      }
    }
    gtk_grid_attach(GTK_GRID(grid),devices,col,row,6,1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(devices),device);
    g_signal_connect(devices,"changed",G_CALLBACK(device_changed_cb),r);
  } else {
    GtkWidget *message=gtk_label_new("No MIDI devices found!");
    gtk_grid_attach(GTK_GRID(grid),message,col,row,1,1);
  }
  row++;
  col=0;

  midi_enable_b=gtk_check_button_new_with_label("MIDI Enable");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (midi_enable_b), r->midi_enabled);
  gtk_grid_attach(GTK_GRID(grid),midi_enable_b,col,row,1,1);
  g_signal_connect(midi_enable_b,"toggled",G_CALLBACK(midi_enable_cb),r);

  row++;
  col=0;

  GtkWidget *clear_b=gtk_button_new_with_label("Clear");
  gtk_grid_attach(GTK_GRID(grid),clear_b,col,row,1,1);
  g_signal_connect(clear_b,"clicked",G_CALLBACK(clear_cb),r);
  col++;

  GtkWidget *save_b=gtk_button_new_with_label("Save");
  gtk_grid_attach(GTK_GRID(grid),save_b,col,row,1,1);
  g_signal_connect(save_b,"clicked",G_CALLBACK(save_cb),r);
  col++;

  GtkWidget *load_b=gtk_button_new_with_label("Load");
  gtk_grid_attach(GTK_GRID(grid),load_b,col,row,1,1);
  g_signal_connect(load_b,"clicked",G_CALLBACK(load_cb),r);
  col++;

  GtkWidget *load_original_b=gtk_button_new_with_label("Load Original");
  gtk_grid_attach(GTK_GRID(grid),load_original_b,col,row,1,1);
  g_signal_connect(load_original_b,"clicked",G_CALLBACK(load_original_cb),r);
  col++;

  row++;
  col=0;

  configure_b=gtk_check_button_new_with_label("MIDI Configure");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (configure_b), false);
  gtk_grid_attach(GTK_GRID(grid),configure_b,col,row,1,1);
  g_signal_connect(configure_b,"toggled",G_CALLBACK(configure_cb),r);
  col++;


  row++;
  col=0;
  GtkWidget *label=gtk_label_new("Event");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);
  label=gtk_label_new("Channel");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);
  label=gtk_label_new("Note");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);
  label=gtk_label_new("Type");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);
  label=gtk_label_new("Value");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);
  label=gtk_label_new("Min");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);
  label=gtk_label_new("Max");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);
  label=gtk_label_new("Action");
  gtk_grid_attach(GTK_GRID(grid),label,col++,row,1,1);


  row++;
  col=0;
  newEvent=gtk_label_new("");
  gtk_grid_attach(GTK_GRID(grid),newEvent,col++,row,1,1);
  newChannel=gtk_label_new("");
  gtk_grid_attach(GTK_GRID(grid),newChannel,col++,row,1,1);
  newNote=gtk_label_new("");
  gtk_grid_attach(GTK_GRID(grid),newNote,col++,row,1,1);
  newType=gtk_combo_box_text_new();
  gtk_grid_attach(GTK_GRID(grid),newType,col++,row,1,1);
  g_signal_connect(newType,"changed",G_CALLBACK(type_changed_cb),NULL);
  newVal=gtk_label_new("");
  gtk_grid_attach(GTK_GRID(grid),newVal,col++,row,1,1);
  newMin=gtk_label_new("");
  gtk_grid_attach(GTK_GRID(grid),newMin,col++,row,1,1);
  newMax=gtk_label_new("");
  gtk_grid_attach(GTK_GRID(grid),newMax,col++,row,1,1);
  newAction=gtk_combo_box_text_new();
  gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(newAction),5);
  gtk_grid_attach(GTK_GRID(grid),newAction,col++,row,1,1);

  add_b=gtk_button_new_with_label("Add");
  g_signal_connect(add_b, "pressed", G_CALLBACK(add_cb),NULL);
  gtk_grid_attach(GTK_GRID(grid),add_b,col++,row,1,1);
  gtk_widget_set_sensitive(add_b,false);


  update_b=gtk_button_new_with_label("Update");
  g_signal_connect(update_b, "pressed", G_CALLBACK(update_cb),NULL);
  gtk_grid_attach(GTK_GRID(grid),update_b,col++,row,1,1);
  gtk_widget_set_sensitive(update_b,false);

  delete_b=gtk_button_new_with_label("Delete");
  g_signal_connect(delete_b, "pressed", G_CALLBACK(delete_cb),NULL);
  gtk_grid_attach(GTK_GRID(grid),delete_b,col++,row,1,1);
  gtk_widget_set_sensitive(delete_b,false);
  row++;
  col=0;

  scrolled_window=gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(scrolled_window,400,400);

  view=gtk_tree_view_new();

  renderer=gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Event", renderer, "text", EVENT_COLUMN, NULL);

  renderer=gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "CHANNEL", renderer, "text", CHANNEL_COLUMN, NULL);

  renderer=gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "NOTE", renderer, "text", NOTE_COLUMN, NULL);

  renderer=gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "TYPE", renderer, "text", TYPE_COLUMN, NULL);

  renderer=gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "ACTION", renderer, "text", ACTION_COLUMN, NULL);

  store=gtk_list_store_new(N_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);

  load_store();

  gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(store));

  gtk_container_add(GTK_CONTAINER(scrolled_window),view);

  gtk_grid_attach(GTK_GRID(grid), scrolled_window, col, row, 6, 10);

  model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
  g_signal_connect(model,"row-inserted",G_CALLBACK(row_inserted_cb),NULL);

  GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

  selection_signal_id=g_signal_connect(G_OBJECT(selection),"changed",G_CALLBACK(tree_selection_changed_cb),NULL);

  return midi_frame;
}

static int update(void *data) {
  int state=GPOINTER_TO_INT(data);
  gchar text[32];
  gint i=1;

  //g_print("%s\n",__FUNCTION__);
  switch(state) {
    case UPDATE_NEW:
      switch(thisEvent) {
        case EVENT_NONE:
          gtk_label_set_text(GTK_LABEL(newEvent),"NONE");
          break;
        case MIDI_NOTE:
          gtk_label_set_text(GTK_LABEL(newEvent),"NOTE");
          break;
        case MIDI_CTRL:
          gtk_label_set_text(GTK_LABEL(newEvent),"CTRL");
          break;
        case MIDI_PITCH:
          gtk_label_set_text(GTK_LABEL(newEvent),"PITCH");
          break;
      }
      sprintf(text,"%d",thisChannel);
      gtk_label_set_text(GTK_LABEL(newChannel),text);
      sprintf(text,"%d",thisNote);
      gtk_label_set_text(GTK_LABEL(newNote),text);
      gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(newType));
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"NONE");
      switch(thisEvent) {
        case EVENT_NONE:
          gtk_combo_box_set_active (GTK_COMBO_BOX(newType),0);
          break;
        case MIDI_NOTE:
        case MIDI_PITCH:
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"KEY");
          gtk_combo_box_set_active (GTK_COMBO_BOX(newType),0);
          break;
        case MIDI_CTRL:
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"KNOB/SLIDER");
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"WHEEL");
          gtk_combo_box_set_active (GTK_COMBO_BOX(newType),0);
          break;
      }
      gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(newAction));
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newAction),NULL,"NONE");
      gtk_combo_box_set_active (GTK_COMBO_BOX(newAction),0);
      sprintf(text,"%d",thisVal);
      gtk_label_set_text(GTK_LABEL(newVal),text);
      sprintf(text,"%d",thisMin);
      gtk_label_set_text(GTK_LABEL(newMin),text);
      sprintf(text,"%d",thisMax);
      gtk_label_set_text(GTK_LABEL(newMax),text);

      gtk_widget_set_sensitive(add_b,true);
      gtk_widget_set_sensitive(update_b,false);
      gtk_widget_set_sensitive(delete_b,false);
      break;

    case UPDATE_CURRENT:
      sprintf(text,"%d",thisVal);
      gtk_label_set_text(GTK_LABEL(newVal),text);
      sprintf(text,"%d",thisMin);
      gtk_label_set_text(GTK_LABEL(newMin),text);
      sprintf(text,"%d",thisMax);
      gtk_label_set_text(GTK_LABEL(newMax),text);
      break;

    case UPDATE_EXISTING:
      switch(thisEvent) {
        case EVENT_NONE:
          gtk_label_set_text(GTK_LABEL(newEvent),"NONE");
          break;
        case MIDI_NOTE:
          gtk_label_set_text(GTK_LABEL(newEvent),"NOTE");
          break;
        case MIDI_CTRL:
          gtk_label_set_text(GTK_LABEL(newEvent),"CTRL");
          break;
        case MIDI_PITCH:
          gtk_label_set_text(GTK_LABEL(newEvent),"PITCH");
          break;
      }
      sprintf(text,"%d",thisChannel);
      gtk_label_set_text(GTK_LABEL(newChannel),text);
      sprintf(text,"%d",thisNote);
      gtk_label_set_text(GTK_LABEL(newNote),text);
      gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(newType));
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"NONE");
      switch(thisEvent) {
        case EVENT_NONE:
	  gtk_combo_box_set_active (GTK_COMBO_BOX(newType),0);
          break;
        case MIDI_NOTE:
        case MIDI_PITCH:
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"KEY");
	  if(thisType==TYPE_NONE) {
	    gtk_combo_box_set_active (GTK_COMBO_BOX(newType),0);
	  } else if(thisType==MIDI_KEY) {
	    gtk_combo_box_set_active (GTK_COMBO_BOX(newType),1);
	  }
          break;
        case MIDI_CTRL:
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"KNOB/SLIDER");
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType),NULL,"WHEEL");
	  if(thisType==TYPE_NONE) {
	    gtk_combo_box_set_active (GTK_COMBO_BOX(newType),0);
	  } else if(thisType==MIDI_KNOB) {
	    gtk_combo_box_set_active (GTK_COMBO_BOX(newType),1);
	  } else if(thisType==MIDI_WHEEL) {
            gtk_combo_box_set_active (GTK_COMBO_BOX(newType),2);
          }
          break;
      }
      sprintf(text,"%d",thisVal);
      gtk_label_set_text(GTK_LABEL(newVal),text);
      sprintf(text,"%d",thisMin);
      gtk_label_set_text(GTK_LABEL(newMin),text);
      sprintf(text,"%d",thisMax);
      gtk_label_set_text(GTK_LABEL(newMax),text);
  
      find_current_cmd();

      gtk_widget_set_sensitive(add_b,false);
      gtk_widget_set_sensitive(update_b,true);
      gtk_widget_set_sensitive(delete_b,true);
      break;

  }

  return 0;
}

void NewMidiConfigureEvent(enum MIDIevent event, int channel, int note, int val) {

  gboolean valid;
  char *str_event;
  char *str_channel;
  char *str_note;
  char *str_type;
  char *str_action;

  gint tree_event;
  gint tree_channel;
  gint tree_note;

  //g_print("%s: event=%d channel=%d note=%d val=%d\n", __FUNCTION__,event,channel,note,val);

  if(event==thisEvent && channel==thisChannel && note==thisNote) {
    //g_print("%s: current event\n",__FUNCTION__);
    thisVal=val;
    if(val<thisMin) thisMin=val;
    if(val>thisMax) thisMax=val;
    g_idle_add(update,GINT_TO_POINTER(UPDATE_CURRENT));
  } else {
    //g_print("%s: new or existing event\n",__FUNCTION__);
    thisEvent=event;
    thisChannel=channel;
    thisNote=note;
    thisVal=val;
    thisMin=val;
    thisMax=val;
    thisType=TYPE_NONE;
    thisAction=ACTION_NONE;

    // search tree to see if it is existing event
    valid=gtk_tree_model_get_iter_first(model,&iter);
    while(valid) {
      gtk_tree_model_get(model, &iter, EVENT_COLUMN, &str_event, -1);
      gtk_tree_model_get(model, &iter, CHANNEL_COLUMN, &str_channel, -1);
      gtk_tree_model_get(model, &iter, NOTE_COLUMN, &str_note, -1);
      gtk_tree_model_get(model, &iter, TYPE_COLUMN, &str_type, -1);
      gtk_tree_model_get(model, &iter, ACTION_COLUMN, &str_action, -1);

      //g_print("%s: %s %s %s %s %s\n",__FUNCTION__,str_event,str_channel,str_note,str_type,str_action);

      if(str_event!=NULL && str_channel!=NULL && str_note!=NULL && str_type!=NULL && str_action!=NULL) {
        if(strcmp(str_event,"CTRL")==0) {
          tree_event=MIDI_CTRL;
        } else if(strcmp(str_event,"PITCH")==0) {
          tree_event=MIDI_PITCH;
        } else if(strcmp(str_event,"NOTE")==0) {
          tree_event=MIDI_NOTE;
        } else {
          tree_event=EVENT_NONE;
        }
        tree_channel=atoi(str_channel);
        tree_note=atoi(str_note);

	if(thisEvent==tree_event && (thisChannel==tree_channel || tree_channel==-1) && thisNote==tree_note) {
          thisVal=0;
          thisMin=0;
          thisMax=0;
          if(strcmp(str_type,"KEY")==0) {
            thisType=MIDI_KEY;
          } else if(strcmp(str_type,"KNOB/SLIDER")==0) {
            thisType=MIDI_KNOB;
          } else if(strcmp(str_type,"WHEEL")==0) {
            thisType=MIDI_WHEEL;
          } else {
            thisType=TYPE_NONE;
          }
          thisAction=ACTION_NONE;
          int i=1;
          while(ActionTable[i].action!=ACTION_NONE) {
            if(strcmp(ActionTable[i].str,str_action)==0) {
              thisAction=ActionTable[i].action;
              break;
            }
            i++;
          }
	  gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),gtk_tree_model_get_path(model,&iter),NULL,false);
          g_idle_add(update,GINT_TO_POINTER(UPDATE_EXISTING));
          return;
	}
      }

      valid=gtk_tree_model_iter_next(model,&iter);
    }
    
    g_idle_add(update,GINT_TO_POINTER(UPDATE_NEW));
  }
}

void midi_save_state() {
  char name[80];
  char value[80];
  struct desc *cmd;
  gint channels;

  if(device!=-1) {
    setProperty("midi_device",midi_devices[device].name);
    for(int i=0;i<128;i++) {
      channels=0;
      cmd=MidiCommandsTable.desc[i];
      while(cmd!=NULL) {
        //g_print("%s:  channel=%d key=%d event=%s onoff=%d type=%s action=%s\n",__FUNCTION__,cmd->channel,i,midi_events[cmd->event],cmd->onoff,midi_types[cmd->type],ActionTable[cmd->action].str);

        sprintf(name,"midi[%d].channel[%d]",i,channels);
        sprintf(value,"%d",cmd->channel);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].event",i,cmd->channel);
        setProperty(name,midi_events[cmd->event]);
        sprintf(name,"midi[%d].channel[%d].onoff",i,cmd->channel);
        sprintf(value,"%d",cmd->onoff);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].type",i,cmd->channel);
        setProperty(name,midi_types[cmd->type]);
        sprintf(name,"midi[%d].channel[%d].action",i,cmd->channel);
        sprintf(value,"%s",ActionTable[cmd->action].str);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].vfl1",i,cmd->channel);
        sprintf(value,"%d",cmd->vfl1);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].vfl2",i,cmd->channel);
        sprintf(value,"%d",cmd->vfl2);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].fl1",i,cmd->channel);
        sprintf(value,"%d",cmd->fl1);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].fl2",i,cmd->channel);
        sprintf(value,"%d",cmd->fl2);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].lft1",i,cmd->channel);
        sprintf(value,"%d",cmd->lft1);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].lft2",i,cmd->channel);
        sprintf(value,"%d",cmd->lft2);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].vfr1",i,cmd->channel);
        sprintf(value,"%d",cmd->vfr1);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].vfr2",i,cmd->channel);
        sprintf(value,"%d",cmd->vfr2);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].fr1",i,cmd->channel);
        sprintf(value,"%d",cmd->fr1);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].fr2",i,cmd->channel);
        sprintf(value,"%d",cmd->fr2);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].rgt1",i,cmd->channel);
        sprintf(value,"%d",cmd->rgt1);
        setProperty(name,value);
        sprintf(name,"midi[%d].channel[%d].rgt2",i,cmd->channel);
        sprintf(value,"%d",cmd->rgt2);
        setProperty(name,value);
        cmd=cmd->next;
        channels++;
      }

      if(channels!=0) {
        sprintf(name,"midi[%d].channels",i);
        sprintf(value,"%d",channels);
        setProperty(name,value);
      }

    }
  }
}

void midi_restore_state() {
  char name[80];
  char *value;
  gint channels;
  gint channel;
  gint event;
  gint onoff;
  gint type;
  gint action;
  gint vfl1 = -1;
  gint vfl2 = 57;
  gint fl1  = 57;
  gint fl2  = 60;
  gint lft1 = 60;
  gint lft2 = 63;
  gint rgt1 = 64;
  gint rgt2 = 67;
  gint fr1  = 67;
  gint fr2  = 73;
  gint vfr1 = 73;
  gint vfr2 = 128;

  struct desc *cmd;

  get_midi_devices();

  //g_print("%s\n",__FUNCTION__);
  value=getProperty("midi_device");
  if(value) {
    //g_print("%s: device=%s\n",__FUNCTION__,value);
    midi_device_name=g_new(gchar,strlen(value)+1);
    strcpy(midi_device_name,value);

    
    for(int i=0;i<n_midi_devices;i++) {
      if(strcmp(midi_devices[i].name,value)==0) {
        device=i;
        g_print("%s: found device at %d\n",__FUNCTION__,i);
        break;
      }
    }
  }

  for(int i=0;i<128;i++) {
    sprintf(name,"midi[%d].channels",i);
    value=getProperty(name);
    if(value) {
      channels=atoi(value);
      for(int c=0;c<channels;c++) {
        sprintf(name,"midi[%d].channel[%d]",i,c);
        value=getProperty(name);
        if(value) {
	  channel=atoi(value);
          sprintf(name,"midi[%d].channel[%d].event",i,channel);
          value=getProperty(name);
	  event=EVENT_NONE;
          if(value) {
            for(int j=0;j<4;j++) {
	      if(strcmp(value,midi_events[j])==0) {
		event=j;
		break;
              }
	    }
	  }
          sprintf(name,"midi[%d].channel[%d].onoff",i,channel);
          value=getProperty(name);
          if(value) onoff=atoi(value);
          sprintf(name,"midi[%d].channel[%d].type",i,channel);
          value=getProperty(name);
	  type=TYPE_NONE;
          if(value) {
            for(int j=0;j<5;j++) {
              if(strcmp(value,midi_types[j])==0) {
                type=j;
                break;
              }
            }
	  }
          sprintf(name,"midi[%d].channel[%d].action",i,channel);
          value=getProperty(name);
	  action=ACTION_NONE;
      if(value) {
          int j=1;
          while(ActionTable[j].type!=ACTION_NONE) {
              if(strcmp(value,ActionTable[j].str)==0) {
                  action=ActionTable[j].action;
                  break;
              }
              j++;
          }
	  }

      sprintf(name,"midi[%d].channel[%d].vfl1",i,channel);
      value=getProperty(name);
	  if(value) vfl1=atoi(value);
      sprintf(name,"midi[%d].channel[%d].vfl2",i,channel);
      value=getProperty(name);
	  if(value) vfl2=atoi(value);
      sprintf(name,"midi[%d].channel[%d].fl1",i,channel);
      value=getProperty(name);
	  if(value) fl1=atoi(value);
      sprintf(name,"midi[%d].channel[%d].fl2",i,channel);
      value=getProperty(name);
	  if(value) fl2=atoi(value);
      sprintf(name,"midi[%d].channel[%d].lft1",i,channel);
      value=getProperty(name);
	  if(value) lft1=atoi(value);
      sprintf(name,"midi[%d].channel[%d].lft2",i,channel);
      value=getProperty(name);
	  if(value) lft2=atoi(value);
      sprintf(name,"midi[%d].channel[%d].rgt1",i,channel);
      value=getProperty(name);
	  if(value) rgt1=atoi(value);
      sprintf(name,"midi[%d].channel[%d].rgt2",i,channel);
      value=getProperty(name);
	  if(value) rgt2=atoi(value);
      sprintf(name,"midi[%d].channel[%d].fr1",i,channel);
      value=getProperty(name);
	  if(value) fr1=atoi(value);
      sprintf(name,"midi[%d].channel[%d].fr2",i,channel);
      value=getProperty(name);
	  if(value) fr2=atoi(value);
      sprintf(name,"midi[%d].channel[%d].vfr1",i,channel);
      value=getProperty(name);
	  if(value) vfr1=atoi(value);
      sprintf(name,"midi[%d].channel[%d].vfr2",i,channel);
      value=getProperty(name);
	  if(value) vfr2=atoi(value);

	  struct desc *desc;
          desc = (struct desc *) malloc(sizeof(struct desc));
          desc->next = NULL;
          desc->action = action; // MIDIaction
          desc->type = type; // MIDItype
          desc->event = event; // MIDevent
          desc->onoff = onoff;
          desc->delay = 0;
	  desc->delay = 0;
          desc->vfl1  = vfl1;
          desc->vfl2  = vfl2;
          desc->fl1   = fl1;
          desc->fl2   = fl2;
          desc->lft1  = lft1;
          desc->lft2  = lft2;
          desc->rgt1  = rgt1;
          desc->rgt2  = rgt2;
          desc->fr1   = fr1;
          desc->fr2   = fr2;
          desc->vfr1  = vfr1;
          desc->vfr2  = vfr2;
          desc->channel  = channel;

	  if(MidiCommandsTable.desc[i]!=NULL) {
            desc->next=MidiCommandsTable.desc[i];
          }
          MidiCommandsTable.desc[i]=desc;
        }
      }
    }
  }

}

