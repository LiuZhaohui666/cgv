//
// "$Id: FillSlider.h 5450 2006-09-19 02:33:42Z spitzak $"
//
// Copyright 2006 by Bill Spitzak and others.
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

#ifndef fltk_FillSlider_h
#define fltk_FillSlider_h

#include "Slider.h"

namespace fltk {

/*! Vertical Slider that is filled from the end (like a progress bar) */
class FL_API FillSlider : public Slider {
public:
  FillSlider(int x,int y,int w,int h,const char *l=0);
};

}

#endif

//
// End of "$Id: FillSlider.h 5450 2006-09-19 02:33:42Z spitzak $".
//
