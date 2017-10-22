//
//	IDC File to disassemble Sega Genesis/Megadrive rom
//	by Kaneda
//
//	Useage:
//	  launch IDA with "idag -a -p68000 -Smida.idc"
//	  Select your .bin file
//	  Press OK to the 2 dialog boxes following
//
//	0.1 (12 Nov 2004 ): Initial release
//  0.2 (01 Jun 2005 ): Support for start adress <0x200 (skip header)
//
//	Update on http://www.consoledev.fr.st
//

#include <idc.idc>

//-------------------------------------------------------------------------
static CW(off,name,cmt) {
  auto x;
  x = off;
  MakeWord(x);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

//-------------------------------------------------------------------------
static CD(off,name,cmt) {
  auto x;
  x = off;
  MakeDword(x);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

//-------------------------------------------------------------------------
static CB(off,name,cmt) {
  auto x;
  x = off;
  MakeByte(x);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

static CS(off,end,name,cmt) {
  auto x;
  x = off;
  MakeStr(x, end);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

static mdVector(  ) {
auto i, addr;

CD(0x00, "initStack", "Initial Stack");
CD(0x04, "startAddress", "Start Address");
CD(0x08, "", "Bus Error");
CD(0x0C, "", "Address Error");
CD(0x10, "", "Illegal instruction");
CD(0x14, "", "Zero Divide");
CD(0x18, "", "CHK instruction");
CD(0x1C, "", "TRAPV instruction");
CD(0x20, "", "Privilege Violation");
CD(0x24, "", "Trace");
CD(0x28, "", "Line 1010 Emulator");
CD(0x2C, "", "Line 1111 Emulator");
CD(0x30, "", "Reserved");
CD(0x34, "", "Reserved");
CD(0x38, "", "Reserved");
CD(0x3C, "", "Unitialized Interrrupt");
CD(0x40, "", "Reserved");
CD(0x44, "", "Reserved");
CD(0x48, "", "Reserved");
CD(0x4C, "", "Reserved");
CD(0x50, "", "Reserved");
CD(0x54, "", "Reserved");
CD(0x58, "", "Reserved");
CD(0x5C, "", "Reserved");
CD(0x60, "", "Spurious Interrupt");
CD(0x64, "", "Level 1/gfx interrupt");
CD(0x68, "", "Level 2/md interrupt");
CD(0x6C, "", "Level 3/timer interrupt");
CD(0x70, "", "Level 4/cdd interrupt");
CD(0x74, "", "Level 5/cdc interrupt");
CD(0x78, "", "Level 6/subcode interrupt");
CD(0x7C, "", "Level 7 interrupt");

i=0x80;
while (i <= 0xBC){
  CD(i,"", "Trap");
  i = i+1;
}

i=0xC0;
while (i <= 0xFF){
  CD(i,"", "Reserved");
  i = i+1;
  }

for ( i=0x08; i< 0x200; i=i+4 ) {
  addr = Dword( i );
  MakeCode(addr);
  }
}

static mdHeader( ) {
auto addr;

addr =  Dword( 0x04 );
if (addr < 0x200)
{
	Warning("Start address unusual");
    return;
}
CS(0x100,0x110,"","");
 CS(0x110,0x120,"","");
 CS(0x120,0x130,"","");
 CS(0x130,0x140,"","");
 CS(0x140,0x150,"","");
 CS(0x150,0x160,"","");
 CS(0x160,0x170,"","");
 CS(0x170,0x180,"","");
 CS(0x180,0x18D,"","Serial Number");
 CW(0x18E, "CheckSum","");
 CS(0x190,0x1A0,"","");
 CD(0x1A0, "RomStartAdr", "Rom Start Adress");
 CD(0x1A4, "RomEndAdr", "Rom End Adress");
 CD(0x1A8, "RamStartAdr", "Ram Start Adress");
 CD(0x1AC, "RamEndAdr", "Ram End Adress");
 CS(0x1B0, 0x1BC, "", "SRam data");
 CS(0x1BC, 0x1C8, "", "Modem data");
 CS(0x1C8, 0x1DC, "", "Memo");
 CS(0x1DC, 0x1F0, "", "");
 CS(0x1F0, 0x200, "Country", "Countries codes");
 }
 
static mdAddress( ){
 // CD(0xC00000, "VDP_Data","");
}
 
//-------------------------------------------------------------------------
static MakeIrq(addr,name) {
  MakeName(addr,name);
  //MakeCode(addr);
  MakeFunction(addr,BADADDR);
   Wait( );
}

static main() {
  auto addr;
  
  SetPrcsr( "68000");

  SegCreate(0x000000,0x07FFFF,0,1,0,2);
  SegRename(0x000000,"prg_ram");

  SegCreate(0xFE0000,0xFE3FFF,0,1,0,2);
  SegRename(0xFE0000,"bram");

  SegCreate(0xFF0000,0xFF3FFF,0,1,0,2);
  SegRename(0xFF0000,"pcm");

  SegCreate(0xFF8000,0xFF81FF,0,1,0,2);
  SegRename(0xFF8000,"regs");
  
  mdVector( );
  mdHeader( );
  mdAddress( );

  MakeIrq(Dword(0x04), "main");
  MakeIrq(Dword(0x64), "gfx_irq");
  MakeIrq(Dword(0x68), "md_irq");
  MakeIrq(Dword(0x6c), "timer_irq");
  MakeIrq(Dword(0x70), "cdd_irq");
  MakeIrq(Dword(0x74), "cdc_irq");
  MakeIrq(Dword(0x78), "subcode_irq");
}
