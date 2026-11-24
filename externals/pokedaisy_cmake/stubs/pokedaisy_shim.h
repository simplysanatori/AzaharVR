/* Force-included into every PokeDaisy file. PokeDaisy is written against stock mGBA 0.10.5; our mGBA
 * (0.10.0-1419 master) renamed these since. Defining them here keeps PokeDaisy's files unmodified. */
#ifndef POKEDAISY_SHIM_H
#define POKEDAISY_SHIM_H
#define color_t mColor
#define desiredVideoDimensions currentVideoSize
#define SAVEDATA_AUTODETECT GBA_SAVEDATA_AUTODETECT
#define IDLE_LOOP_NONE GBA_IDLE_LOOP_NONE
#define BASE_CART0 GBA_BASE_ROM0
#define BASE_CART_SRAM GBA_BASE_SRAM
#define BASE_PALETTE_RAM GBA_BASE_PALETTE_RAM
#define SIZE_CART0 GBA_SIZE_ROM0
#define SIZE_WORKING_RAM GBA_SIZE_EWRAM
#endif
