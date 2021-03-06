-- $Id: rbd_usracc.vhd 758 2016-04-02 18:01:39Z mueller $
--
-- Copyright 2016- by Walter F.J. Mueller <W.F.J.Mueller@gsi.de>
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
-- Module Name:    rbd_usracc - syn
-- Description:    rbus dev: return usr_access register (bitfile+jtag timestamp)
--
-- Dependencies:   xlib/usr_access_unisim
-- Test bench:     -
--
-- Target Devices: generic
-- Tool versions:  viv 2015.4; ghdl 0.33
--
-- Revision History: 
-- Date         Rev Version  Comment
-- 2016-04-02   758   1.0    Initial version 
------------------------------------------------------------------------------
--
-- rbus registers:
--
-- Addr   Bits  Name        r/w/f  Function
--    0         time        r/-/-  use_accress lsb
--    1         time        r/-/-  use_accress msb
--

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.slvtypes.all;
use work.xlib.all;
use work.rblib.all;
use work.rbdlib.all;

entity rbd_usracc is                    -- rbus dev: return usr_access register
  generic (
    RB_ADDR : slv16 := rbaddr_usracc);
  port (
    CLK  : in slbit;                    -- clock
    RB_MREQ : in rb_mreq_type;          -- rbus: request
    RB_SRES : out rb_sres_type          -- rbus: response
  );
end entity rbd_usracc;


architecture syn of rbd_usracc is

  signal R_SEL : slbit := '0';
  signal DATA  : slv32 := (others=>'0');
  
begin

  RBSEL : rb_sel
    generic map (
      RB_ADDR => RB_ADDR,
      SAWIDTH => 1)
    port map (
      CLK     => CLK,
      RB_MREQ => RB_MREQ,
      SEL     => R_SEL
    );

  UA : usr_access_unisim
    port map (DATA => DATA);
    
  proc_next : process (R_SEL, RB_MREQ, DATA)
    variable irb_ack  : slbit := '0';
    variable irb_err  : slbit := '0';
    variable irb_dout : slv16 := (others=>'0');
  begin

    irb_ack  := '0';
    irb_err  := '0';
    irb_dout := (others=>'0');
            
    -- rbus transactions
    if R_SEL = '1' then
      irb_ack := RB_MREQ.re or RB_MREQ.we;
      if RB_MREQ.we = '1' then 
        irb_err := '1';
      end if;
      if RB_MREQ.re = '1' then
        case (RB_MREQ.addr(0)) is
          when '0' => irb_dout := DATA(15 downto  0);
          when '1' => irb_dout := DATA(31 downto 16);
          when others => null;
        end case;
      end if;
    end if;

    RB_SRES.dout <= irb_dout;
    RB_SRES.ack  <= irb_ack;
    RB_SRES.err  <= irb_err;
    RB_SRES.busy <= '0';

  end process proc_next;

end syn;
