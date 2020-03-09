
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>  
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <graphics/gfxbase.h>

#include "custom_defines.h"


#define GETLONG(var) (*(volatile ULONG*)&var)

#define TILES_SIZE 23296
#define MAP_SIZE    320

#define BPL0 (25 * 2) + 1
#define BPL1 (27 * 2) + 1
#define BPL2 (29 * 2) + 1
#define BPL3 (31 * 2) + 1

#define PLANES 4

#define LINEBLOCKS 13
#define WBLOCK 16
#define BLOCKSBYTESPERLINE 26

#define BITMAPLINEBYTES 40
#define BITMAPLINEBYTESI 160


#define POTGOR *(UWORD *)0xDFF016     

#define WIDTH 320		
#define HEIGHT 256

struct GfxBase *GfxBase;
struct copinit *oldCopinit;

struct CIA *cia = (struct CIA *) 0xBFE001;
struct Custom *custom = (struct Custom *)0xdff000;


UWORD chip cop[] =
{
    FMODE, 0,
    
	BPLCON0, 0x4200,
	BPLCON1, 0,

	DIWSTRT, 0x2C81,
	DIWSTOP, 0x2CC1,
 
	DDFSTRT, 0x0038, 
	DDFSTOP, 0x00d0,

    // Ojo, aquí declaramos el módulo de la pantalla para encontrar la siguiente línea
    // en este caso que son 4 bitplanes, el salto para la siguiente línea real en pantalla son 120 bytes, 78 en hexadecimal
	BPL1MOD, 0x78,
	BPL2MOD, 0x78,

	SPR0PTH, 0,
	SPR0PTL, 0,
        
	SPR1PTH, 0,
	SPR1PTL, 0,
        
	SPR2PTH, 0,
	SPR2PTL, 0,
        
	SPR3PTH, 0,
	SPR3PTL, 0,
        
	SPR4PTH, 0,
	SPR4PTL, 0,
        
	SPR5PTH, 0,
	SPR5PTL, 0,
        
	SPR6PTH, 0,
	SPR6PTL, 0,
        
	SPR7PTH, 0,
	SPR7PTL, 0,

	BPL1PTH, 0,
	BPL1PTL, 0,
	
	BPL2PTH, 0,
	BPL2PTL, 0,

	BPL3PTH, 0,
	BPL3PTL, 0,

	BPL4PTH, 0,
	BPL4PTL, 0,

	COLOR00,0x0000,
	COLOR01,0x0435,
	COLOR02,0x0834,
	COLOR03,0x0254,
	COLOR04,0x0223,
	COLOR05,0x0357,
	COLOR06,0x0373,
	COLOR07,0x0859,
	COLOR08,0x0b54,
	COLOR09,0x048A,
	COLOR10,0x0698,
	COLOR11,0x04a4,
	COLOR12,0x0d85,
	COLOR13,0x08b9,
	COLOR14,0x0ed4,
	COLOR15,0x0efe,

	0x2b07, 0xfffe,

	0xffff, 0xfffe
};

void HardWaitBlitter(void)
{
    while (custom->dmaconr & DMAF_BLTDONE)
    {}
}


// Draw a block with mask of 16x16 pixels
void bm_drawBlock(UBYTE *orig, UBYTE *dest, WORD x, WORD y, WORD tile)
{
    LONG orig_offset = ((tile % LINEBLOCKS) << 1) + ((tile / LINEBLOCKS) * WBLOCK * BLOCKSBYTESPERLINE * PLANES * 2);
    LONG dest_offset = ((x >> 3) & 0xFFFE) + (y * BITMAPLINEBYTESI);

    HardWaitBlitter();
    
    custom->bltcon0 = 0xFCA;
    custom->bltcon1 = 0;
    custom->bltafwm = 0xFFFF;
    custom->bltalwm = 0xFFFF;
    custom->bltamod = 50; // El salto para encontrar la siguiente linea del bloque a pintar son 24 bytes para saltar a la siguiente linea + 26 bytes de la línea de la mascara = 50 bytes
    custom->bltbmod = 50;
    
    custom->bltcmod = BITMAPLINEBYTES - 2; // El salto para la siguiente línea en pantalla son 40 bytes menos los dos bytes del bloque que pintamos
    custom->bltdmod = BITMAPLINEBYTES - 2;
    
    custom->bltapt  = orig + orig_offset + BLOCKSBYTESPERLINE;  // Máscara, orig_offset apunta a la primera línea del bitmap a pintar, 
                                                                // se añaden 26 bytes puesto que cada linea esta intercalada con su máscara y los tiles ocupan 26 bytes de ancho
    custom->bltbpt  = orig + orig_offset;   // Bloque a dibujar
    custom->bltcpt	= dest + dest_offset;   // Pantalla
    custom->bltdpt	= dest + dest_offset;

    custom->bltsize = 4097; // PLANES*(WBLOCK<<6)+1;
}



void gfx_wait_vblank(void) {
  ULONG vpos_vhpos;

  do {
    vpos_vhpos = *(volatile ULONG*)&custom->vposr;
  } while ((vpos_vhpos & 0x1FF00) >= 0x12F00);

  do {
    vpos_vhpos = *(volatile ULONG*)&custom->vposr;
  } while ((vpos_vhpos & 0x1FF00) < 0x12F00);
}



int main()
{
	UWORD oldInt, oldDMA;
    UBYTE *ptr0, *ptr1, *ptr2, *ptr3;
    UWORD i, x, y;
    WORD  fps = 0;
    
    BPTR file_ptr;
    UBYTE *tiles = (UBYTE*)AllocMem(TILES_SIZE, MEMF_CHIP);
    UBYTE* map = (UBYTE*)AllocMem(MAP_SIZE, 0L);

    
    if (file_ptr = Open("data/tiles_test.dat", MODE_OLDFILE)) 
    {
            Read(file_ptr, tiles, TILES_SIZE);
            Close(file_ptr);
    } 
    
    
    if (file_ptr = Open("data/map.dat", MODE_OLDFILE)) 
    {
            Read(file_ptr, map, MAP_SIZE);
            Close(file_ptr);
    }
    
	GfxBase = (struct GfxBase*)OldOpenLibrary("graphics.library");
	oldCopinit = GfxBase->copinit;
	CloseLibrary((struct Library *)GfxBase);
   
    
	// Save interrupts and DMA
	// initiate our copper
    oldInt = custom->intenar;
    oldDMA = custom->dmaconr;

	// disable all interrupts and DMA
	custom->intena = 0x7fff;
	custom->intreq = 0x7fff; 
	custom->intreq = 0x7fff;	// needed twice?  Works fine with 1
	custom->dmacon = 0x7fff;

	// set required bits of DMA 
	custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_BLITHOG | DMAF_SPRITE | DMAF_BLITTER | DMAF_COPPER;

    // Aquí declaramos los bitplanes de la pantalla de modo interleaved, intercalamos cada linea de cada plano
    // de modo que el puntero inicial de cada plano, tiene un desplazamiento de 40 bytes, que es el ancho en bytes de una línea en la pantalla
    // También hay que definir el modulo de la pantalla correctamente en la copper list, cada linea real en pantalla (con sus 4 planos) ocupa
    // 160 bytes (40 bytes de la línea del plano * 4 planos), de modo que el módulo para la pantalla es de 120 bytes 
    
    ptr0 = AllocMem(PLANES*((WIDTH*HEIGHT)/8),MEMF_CHIP|MEMF_CLEAR);
    ptr1 = ptr0+40;
    ptr2 = ptr1+40;
    ptr3 = ptr2+40;
    
    cop[BPL0] = (GETLONG(ptr0) >> 16) & 0xffff;
    cop[BPL0 + 2] = GETLONG(ptr0) & 0xffff;

    cop[BPL1] = (GETLONG(ptr1) >> 16) & 0xffff;
    cop[BPL1 + 2] = GETLONG(ptr1) & 0xffff;

    cop[BPL2] = (GETLONG(ptr2) >> 16) & 0xffff;
    cop[BPL2 + 2] = GETLONG(ptr2) & 0xffff;

    cop[BPL3] = (GETLONG(ptr3) >> 16) & 0xffff;
    cop[BPL3 + 2] = GETLONG(ptr3) & 0xffff;
    
	custom->cop1lc = (ULONG)cop;
    
    gfx_wait_vblank();
    
    // Pintamos el mapa
    i = 0;
    for (y = 0; y < HEIGHT-1; y += WBLOCK) 
    {
        for (x = 0; x < WIDTH-1; x+= WBLOCK) 
        {          
            
            WORD tile = map[i]-1;
            bm_drawBlock(tiles, ptr0, x, y, 32);
               
            if (tile>=0)
                bm_drawBlock(tiles, ptr0, x, y, tile);

            i++;  
        }
    }
    
    // Pintamos un cajón
    bm_drawBlock(tiles, ptr0, 48, 48, 87);
    
	while(cia->ciapra & CIAF_GAMEPORT0)
	{
        gfx_wait_vblank();
       
        fps++;
    }
    
	// restore DMA
	custom->dmacon = 0x7fff;
	custom->dmacon = oldDMA | DMAF_SETCLR | DMAF_MASTER;

	// restore original copper
	custom->cop1lc = (ULONG)oldCopinit;

	// restore interrupts
	custom->intena = oldInt | 0xc000;

    FreeMem(ptr0, PLANES*((WIDTH*HEIGHT)/8));
    FreeMem(tiles, TILES_SIZE);
    
	return 0;
}
