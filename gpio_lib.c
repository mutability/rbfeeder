/*
 * gpio_lib.c
 *
 * Copyright 2013 Stefan Mavrodiev <support@olimex.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

//GPIO 1
#define PIN_PG0		SUNXI_GPG(0)
#define PIN_PG1		SUNXI_GPG(1)
#define PIN_PG2		SUNXI_GPG(2)
#define PIN_PG3		SUNXI_GPG(3)
#define PIN_PG4		SUNXI_GPG(4)
#define PIN_PG5		SUNXI_GPG(5)
#define PIN_PG6		SUNXI_GPG(6)
#define PIN_PG7		SUNXI_GPG(7)
#define PIN_PG8		SUNXI_GPG(8)
#define PIN_PG9		SUNXI_GPG(9)
#define PIN_PG10	SUNXI_GPG(10)
#define PIN_PG11	SUNXI_GPG(11)
//#define PIN_PD26	SUNXI_GPD(26)
//#define PIN_PD27	SUNXI_GPD(27)

//GPIO 2
//#define PIN_PB0		SUNXI_GPB(0)
#define PIN_PE0		SUNXI_GPE(0)
//#define PIN_PB1		SUNXI_GPB(1)
#define PIN_PE1		SUNXI_GPE(1)
#define PIN_PI0		SUNXI_GPI(0)
#define PIN_PE2		SUNXI_GPE(2)
#define PIN_PI1		SUNXI_GPI(1)
#define PIN_PE3		SUNXI_GPE(3)
#define PIN_PI2		SUNXI_GPI(2)
#define PIN_PE4		SUNXI_GPE(4)
#define PIN_PI3		SUNXI_GPI(3)
#define PIN_PE5		SUNXI_GPE(5)
#define PIN_PI10	SUNXI_GPI(10)
#define PIN_PE6		SUNXI_GPE(6)
#define PIN_PI11	SUNXI_GPI(11)
#define PIN_PE7		SUNXI_GPE(7)
#define PIN_PC3		SUNXI_GPC(3)
#define PIN_PE8		SUNXI_GPE(8)
#define PIN_PC7		SUNXI_GPC(7)
#define PIN_PE9		SUNXI_GPE(9)
#define PIN_PC16	SUNXI_GPC(16)
#define PIN_PE10	SUNXI_GPE(10)
#define PIN_PC17	SUNXI_GPC(17)
#define PIN_PE11	SUNXI_GPE(11)
#define PIN_PC18	SUNXI_GPC(18)
#define PIN_PI14	SUNXI_GPI(14)
#define PIN_PC23	SUNXI_GPC(23)
#define PIN_PI15	SUNXI_GPI(15)
#define PIN_PC24	SUNXI_GPC(24)
#define PIN_PB23	SUNXI_GPB(23)
#define PIN_PB22	SUNXI_GPB(22)

//GPIO 3
#define PIN_PH0		SUNXI_GPH(0)
#define PIN_PB3		SUNXI_GPB(3)
#define PIN_PH2		SUNXI_GPH(2)
#define PIN_PB4		SUNXI_GPB(4)
#define PIN_PH7		SUNXI_GPH(7)
#define PIN_PB5		SUNXI_GPB(5)
#define PIN_PH9		SUNXI_GPH(9)
#define PIN_PB6		SUNXI_GPB(6)
#define PIN_PH10	SUNXI_GPH(10)
#define PIN_PB7		SUNXI_GPB(7)
#define PIN_PH11	SUNXI_GPH(11)
#define PIN_PB8		SUNXI_GPB(8)
#define PIN_PH12	SUNXI_GPH(12)
#define PIN_PB10	SUNXI_GPB(10)
#define PIN_PH13	SUNXI_GPH(13)
#define PIN_PB11	SUNXI_GPB(11)
#define PIN_PH14	SUNXI_GPH(14)
#define PIN_PB12	SUNXI_GPB(12)
#define PIN_PH15	SUNXI_GPH(15)
#define PIN_PB13	SUNXI_GPB(13)
#define PIN_PH16	SUNXI_GPH(16)
#define PIN_PB14	SUNXI_GPB(14)
#define PIN_PH17	SUNXI_GPH(17)
#define PIN_PB15	SUNXI_GPB(15)
#define PIN_PH18	SUNXI_GPH(18)
#define PIN_PB16	SUNXI_GPB(16)
#define PIN_PH19	SUNXI_GPH(19)
#define PIN_PB17	SUNXI_GPB(17)
#define PIN_PH20	SUNXI_GPH(20)
#define PIN_PH24	SUNXI_GPH(24)
#define PIN_PH21	SUNXI_GPH(21)
#define PIN_PH25	SUNXI_GPH(25)
#define PIN_PH22	SUNXI_GPH(22)
#define PIN_PH26	SUNXI_GPH(26)
#define PIN_PH23	SUNXI_GPH(23)
#define PIN_PH27	SUNXI_GPH(27)




#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#include <unistd.h>

#include "gpio_lib.h"


unsigned int SUNXI_PIO_BASE = 0;
static volatile long int *gpio_map = NULL;

int sunxi_gpio_init(void) {
    int fd;
    unsigned int addr_start, addr_offset;
    unsigned int PageSize, PageMask;


    fd = open("/dev/mem", O_RDWR);
    if(fd < 0) {
        return SETUP_DEVMEM_FAIL;
    }

    PageSize = sysconf(_SC_PAGESIZE);
    PageMask = (~(PageSize-1));

    addr_start = SW_PORTC_IO_BASE & PageMask;
    addr_offset = SW_PORTC_IO_BASE & ~PageMask;

    gpio_map = (void *)mmap(0, PageSize*2, PROT_READ|PROT_WRITE, MAP_SHARED, fd, addr_start);
    if(gpio_map == MAP_FAILED) {
        return SETUP_MMAP_FAIL;
    }

    SUNXI_PIO_BASE = (unsigned int)gpio_map;
    SUNXI_PIO_BASE += addr_offset;

    close(fd);
    return SETUP_OK;
}

int sunxi_gpio_set_cfgpin(unsigned int pin, unsigned int val) {

    unsigned int cfg;
    unsigned int bank = GPIO_BANK(pin);
    unsigned int index = GPIO_CFG_INDEX(pin);
    unsigned int offset = GPIO_CFG_OFFSET(pin);

    if(SUNXI_PIO_BASE == 0) {
        return -1;
    }

    struct sunxi_gpio *pio =
        &((struct sunxi_gpio_reg *)SUNXI_PIO_BASE)->gpio_bank[bank];


    cfg = *(&pio->cfg[0] + index);
    cfg &= ~(0xf << offset);
    cfg |= val << offset;

    *(&pio->cfg[0] + index) = cfg;

    return 0;
}

int sunxi_gpio_get_cfgpin(unsigned int pin) {

    unsigned int cfg;
    unsigned int bank = GPIO_BANK(pin);
    unsigned int index = GPIO_CFG_INDEX(pin);
    unsigned int offset = GPIO_CFG_OFFSET(pin);
    if(SUNXI_PIO_BASE == 0)
    {
        return -1;
    }
    struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *)SUNXI_PIO_BASE)->gpio_bank[bank];
    cfg = *(&pio->cfg[0] + index);
    cfg >>= offset;
    return (cfg & 0xf);
}
int sunxi_gpio_output(unsigned int pin, unsigned int val) {

    unsigned int bank = GPIO_BANK(pin);
    unsigned int num = GPIO_NUM(pin);

    if(SUNXI_PIO_BASE == 0)
    {
        return -1;
    }
    struct sunxi_gpio *pio =&((struct sunxi_gpio_reg *)SUNXI_PIO_BASE)->gpio_bank[bank];

    if(val)
        *(&pio->dat) |= 1 << num;
    else
        *(&pio->dat) &= ~(1 << num);

    return 0;
}

int sunxi_gpio_input(unsigned int pin) {

    unsigned int dat;
    unsigned int bank = GPIO_BANK(pin);
    unsigned int num = GPIO_NUM(pin);

    if(SUNXI_PIO_BASE == 0)
    {
        return -1;
    }

    struct sunxi_gpio *pio =&((struct sunxi_gpio_reg *)SUNXI_PIO_BASE)->gpio_bank[bank];

    dat = *(&pio->dat);
    dat >>= num;

    return (dat & 0x1);
}
void sunxi_gpio_cleanup(void)
{
    unsigned int PageSize;
    if (gpio_map == NULL)
        return;

    PageSize = sysconf(_SC_PAGESIZE);
    munmap((void*)gpio_map, PageSize*2);
}

int main() {

    int result;

    result = sunxi_gpio_init();
    if(result == SETUP_DEVMEM_FAIL) {
        printf("No access to /dev/mem. Try running as root!");
        return 1;
    }
    else if(result == SETUP_MALLOC_FAIL) {
        printf("No memory");
        return 1;
    }
    else if(result == SETUP_MMAP_FAIL) {
        printf("Mmap failed on module import");
        return 1;
    }
    
    
    // Configure GPIO for output
    sunxi_gpio_set_cfgpin(PIN_PG4, OUTPUT); // ADS-B
    sunxi_gpio_set_cfgpin(PIN_PG5, OUTPUT); // Status
    sunxi_gpio_set_cfgpin(PIN_PG6, OUTPUT); // GPS
    sunxi_gpio_set_cfgpin(PIN_PG7, OUTPUT); // PC
    sunxi_gpio_set_cfgpin(PIN_PG8, OUTPUT); // Error
    sunxi_gpio_set_cfgpin(PIN_PG9, OUTPUT); // VHF
    int contador = 0;
    //int last = 0;
    int espera = 250000;
    while (contador < 30) {
        contador++;
                
        sunxi_gpio_output(PIN_PG6,HIGH);
        usleep(espera);
        sunxi_gpio_output(PIN_PG6,LOW);
        sunxi_gpio_output(PIN_PG4,HIGH);
        usleep(espera);
        sunxi_gpio_output(PIN_PG4,LOW);        
        sunxi_gpio_output(PIN_PG8,HIGH);
        usleep(espera);
        sunxi_gpio_output(PIN_PG8,LOW);
        sunxi_gpio_output(PIN_PG5,HIGH);
        usleep(espera);
        sunxi_gpio_output(PIN_PG5,LOW);
        sunxi_gpio_output(PIN_PG7,HIGH);
        usleep(espera);
        sunxi_gpio_output(PIN_PG7,LOW);
        sunxi_gpio_output(PIN_PG9,HIGH);
        usleep(espera);
        sunxi_gpio_output(PIN_PG9,LOW);
        //sleep(1);
    }
    
    sunxi_gpio_cleanup();
    return 0;
    
}
