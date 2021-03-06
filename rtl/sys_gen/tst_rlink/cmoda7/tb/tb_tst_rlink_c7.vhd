-- $Id: tb_tst_rlink_c7.vhd 906 2017-06-04 21:59:13Z mueller $
--
-- Copyright 2017- by Walter F.J. Mueller <W.F.J.Mueller@gsi.de>
--
-- This program is free software; you may redistribute and/or modify it under
-- the terms of the GNU General Public License as published by the Free
-- Software Foundation, either version 2, or at your option any later version.
--
-- This program is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
-- or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
-- for complete details.
--
------------------------------------------------------------------------------
-- Module Name:    tb_tst_rlink_c7
-- Description:    Configuration for tb_tst_rlink_c7 for tb_cmoda7
--
-- Dependencies:   sys_tst_rlink_c7
--
-- To test:        sys_tst_rlink_c7
--
-- Revision History: 
-- Date         Rev Version  Comment
-- 2017-06-04   906   1.0    Initial version 
------------------------------------------------------------------------------

configuration tb_tst_rlink_c7 of tb_cmoda7 is

  for sim
    for all : cmoda7_aif
      use entity work.sys_tst_rlink_c7;
    end for;
  end for;

end tb_tst_rlink_c7;
