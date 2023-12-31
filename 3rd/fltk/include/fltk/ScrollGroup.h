//
// "$Id: ScrollGroup.h 6132 2008-05-29 23:09:01Z TobiasFar $"
//
// Group that adds scrollbars so you can scroll around the area
// covered by all the child widgets. For most uses you will make
// a single child widget, this child may resize in it's layout()
// and the scrollbars will adjust to match.
//
// Due to clipping problems no subclasses of Window may be used
// as child widgets.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA.
//
// Please report all bugs and problems to "fltk-bugs@fltk.org".
//

#ifndef fltk_ScrollGroup_h
#define fltk_ScrollGroup_h

#include "Group.h"
#include "Scrollbar.h"

namespace fltk {

class FL_API ScrollGroup : public Group {

  int xposition_, yposition_;
  int layoutdx, layoutdy;
  int scrolldx, scrolldy;
  bool enable_drag_scroll_;
  bool drag_scrolling_;
  bool delegate_alt_click_;
  int drag_x_, drag_y_, pos_x_, pos_y_;
  int max_x_scroll_, max_y_scroll_;
  static void hscrollbar_cb(Widget*, void*);
  static void scrollbar_cb(Widget*, void*);
  static void draw_clip(void*,const Rectangle&);

protected:
  
  int padding_x, padding_y;
  void draw();

public:

  void bbox(Rectangle&);
  Scrollbar scrollbar;
  Scrollbar hscrollbar;

  void enable_drag_scroll( bool enable ) { enable_drag_scroll_ = true; }
  void set_padding(int px, int py) { padding_x = px, padding_y = py; }

  virtual int handle(int);
  virtual void layout();

  ScrollGroup(int x,int y,int w,int h, const char*l=0, bool begin=false);

  enum { // values for type()
    HORIZONTAL = 1,
    VERTICAL = 2,
    BOTH = 3,
    ALWAYS_ON = 4,
    HORIZONTAL_ALWAYS = 5,
    VERTICAL_ALWAYS = 6,
    BOTH_ALWAYS = 7
  };

  int xposition() const {return xposition_;}
  int yposition() const {return yposition_;}
  void scrollTo(int, int);
};

}

#endif

//
// End of "$Id: ScrollGroup.h 6132 2008-05-29 23:09:01Z TobiasFar $".
//
