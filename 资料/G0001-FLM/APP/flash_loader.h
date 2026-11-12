/***********************************************************************************************************************
    @file    flash_loader.h
    @author  FAE Team
    @date    06-Jul-2026
    @brief   Flash Loader Module (FLM) for MM32G0001
             Based on reverse engineering of MM32G0001_16.FLM
  **********************************************************************************************************************
    @attention

    <h2><center>&copy; Copyright(c) <2023> <MindMotion></center></h2>

      Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
    following conditions are met:
    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
       the following disclaimer in the documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
       promote products derived from this software without specific prior written permission.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *********************************************************************************************************************/

/* Define to prevent recursive inclusion */
#ifndef __FLASH_LOADER_H
#define __FLASH_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Files include */
#include <stdint.h>

/* Exported types *****************************************************************************************************/

/* Exported constants *************************************************************************************************/

/* Exported macro *****************************************************************************************************/

/* Exported variables *************************************************************************************************/

/* Exported functions *************************************************************************************************/
uint32_t Init(uint32_t adr, uint32_t clk, uint32_t fnc);
uint32_t UnInit(uint32_t adr, uint32_t clk, uint32_t fnc);
uint32_t EraseChip(uint32_t adr, uint32_t clk, uint32_t fnc);
uint32_t EraseSector(uint32_t adr, uint32_t sz, uint32_t clk, uint32_t fnc);
uint32_t BlankCheck(uint32_t adr, uint32_t sz, uint32_t clk);
uint32_t ProgramPage(uint32_t adr, uint32_t sz, uint8_t *buf, uint32_t clk, uint32_t fnc);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_LOADER_H */

/********************************************** (C) Copyright MindMotion **********************************************/
