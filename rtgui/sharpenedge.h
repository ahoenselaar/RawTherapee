/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
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
 *
 *
 *  Manuel Llorens' algorithm of edge sharpening
 *
 *
 */
#pragma once

#include <gtkmm.h>

#include "adjuster.h"
#include "toolpanel.h"

class SharpenEdge :
    public ToolParamBlock,
    public AdjusterListener,
    public FoldableToolPanel
{

protected:

    Adjuster* passes;
    Adjuster* amount;
    Gtk::CheckButton* threechannels;

    sigc::connection chanthreeconn;
    bool lastchanthree;

public:

    SharpenEdge              ();

    void read                (const rtengine::procparams::ProcParams* pp, const ParamsEdited* pedited = nullptr) override;
    void write               (rtengine::procparams::ProcParams* pp, ParamsEdited* pedited = nullptr) override;
    void setDefaults         (const rtengine::procparams::ProcParams* defParams, const ParamsEdited* pedited = nullptr) override;
    void setBatchMode        (bool batchMode) override;
    void trimValues          (rtengine::procparams::ProcParams* pp) override;
    void setAdjusterBehavior (bool amountadd, bool passadd);
    void adjusterChanged     (Adjuster* a, double newval) override;

    void enabledChanged      () override;
    void chanthree_toggled   ();

};
