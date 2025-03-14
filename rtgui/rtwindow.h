/*
 *  This file is part of RawTherapee.
 *
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <set>

#include <gtkmm.h>

#if defined(__APPLE__)
#include <gtkosxapplication.h>
#endif

#include "progressconnector.h"
#include "splash.h"

#include "../rtengine/noncopyable.h"

class BatchQueueEntry;
class BatchQueuePanel;
class EditorPanel;
class FilePanel;
class RTWindow :
    public Gtk::Window,
    public rtengine::ProgressListener,
    public rtengine::NonCopyable
{

private:
    Gtk::Notebook* mainNB;
    BatchQueuePanel* bpanel;
    std::set<Glib::ustring> filesEdited;
    std::map<Glib::ustring, EditorPanel*> epanels;

    Splash* splash;
    Gtk::ProgressBar prProgBar;
    PLDBridge* pldBridge;
    bool is_fullscreen;
    bool on_delete_has_run;
    Gtk::Button * btn_fullscreen;

    Gtk::Image *iFullscreen, *iFullscreen_exit;

    bool isSingleTabMode() const;

    bool on_expose_event_epanel (GdkEventExpose* event);
    bool on_expose_event_fpanel (GdkEventExpose* event);
    bool splashClosed (GdkEventAny* event);
    bool isEditorPanel (Widget* panel);
    bool isEditorPanel (guint pageNum);
    void showErrors ();

    Glib::ustring versionStr;
#if defined(__APPLE__)
    GtkosxApplication *osxApp;
#endif

public:
    RTWindow ();
    ~RTWindow() override;

#if defined(__APPLE__)
    bool osxFileOpenEvent (Glib::ustring path);
#endif
    void addEditorPanel (EditorPanel* ep, const std::string &name);
    void remEditorPanel (EditorPanel* ep);
    bool selectEditorPanel (const std::string &name);

    void addBatchQueueJob       (BatchQueueEntry* bqe, bool head = false);
    void addBatchQueueJobs      (const std::vector<BatchQueueEntry*>& entries);

    bool keyPressed (GdkEventKey* event);
    bool on_configure_event (GdkEventConfigure* event) override;
    bool on_delete_event (GdkEventAny* event) override;
    bool on_window_state_event (GdkEventWindowState* event) override;
    void on_mainNB_switch_page (Gtk::Widget* widget, guint page_num);

    void showICCProfileCreator ();
    void showPreferences ();
    void on_realize () override;
    void toggle_fullscreen ();

    void setProgress(double p) override;
    void setProgressStr(const Glib::ustring& str) override;
    void setProgressState(bool inProcessing) override;
    void error(const Glib::ustring& descr) override;

    rtengine::ProgressListener* getProgressListener ()
    {
        return pldBridge;
    }

    EditorPanel*  epanel;
    FilePanel* fpanel;

    void SetEditorCurrent();
    void SetMainCurrent();
    void MoveFileBrowserToEditor();
    void MoveFileBrowserToMain();

    void updateProfiles (const Glib::ustring &printerProfile, rtengine::RenderingIntent printerIntent, bool printerBPC);
    void updateTPVScrollbar (bool hide);
    void updateHistogramPosition (int oldPosition, int newPosition);
    void updateFBQueryTB (bool singleRow);
    void updateFBToolBarVisibility (bool showFilmStripToolBar);
    bool getIsFullscreen()
    {
        return is_fullscreen;
    }
    void set_title_decorated (Glib::ustring fname);
    void closeOpenEditors();
    void setEditorMode (bool tabbedUI);
    void createSetmEditor();

    void writeToolExpandedStatus (std::vector<int> &tpOpen);
};
