// $Id: Rw11UnitLP11.hpp 887 2017-04-28 19:32:52Z mueller $
//
// Copyright 2013-2017 by Walter F.J. Mueller <W.F.J.Mueller@gsi.de>
//
// This program is free software; you may redistribute and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 2, or at your option any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for complete details.
// 
// Revision History: 
// Date         Rev Version  Comment
// 2017-04-07   868   1.0.1  Dump(): add detail arg
// 2013-05-01   513   1.0    Initial version
// ---------------------------------------------------------------------------


/*!
  \file
  \brief   Declaration of class Rw11UnitLP11.
*/

#ifndef included_Retro_Rw11UnitLP11
#define included_Retro_Rw11UnitLP11 1

#include "Rw11UnitStreamBase.hpp"

namespace Retro {

  class Rw11CntlLP11;                       // forw decl to avoid circular incl

  class Rw11UnitLP11 : public Rw11UnitStreamBase<Rw11CntlLP11> {
    public:

                    Rw11UnitLP11(Rw11CntlLP11* pcntl, size_t index);
                   ~Rw11UnitLP11();

      virtual void  Dump(std::ostream& os, int ind=0, const char* text=0,
                         int detail=0) const;

    protected:

  };
  
} // end namespace Retro

//#include "Rw11UnitLP11.ipp"

#endif
