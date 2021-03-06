/*
* @Author: BlahGeek
* @Date:   2014-05-26
* @Last Modified by:   BlahGeek
* @Last Modified time: 2014-06-05
*/

#include <stdio.h>
#include "defs.h"
#include "ethernet.h"
#include "ulib.h"

// This utils file is for ethernet only.
#ifndef _H_UTILS_
#define _H_UTILS_ value

#define nop() __asm volatile ("nop")
#define LSB(x) ((x) & 0xFF)
#define MSB(x) (((x) >> 8) & 0xFF)

void delay_ms(int ms);
void delay_us(int us);

int eth_memcmp(int * a, int * b, int length);
void eth_memcpy(int * dst, int * src, int length);

int mem2int(int * data, int length);
void int2mem(int * data, int length, int val);

#endif

void delay_ms(int ms) {
	int i, j;
    for(i = 0 ; i < ms ; i += 1) {
        //cprintf(".");
        for(j = 0 ; j < 1000 ; j += 1);
      }
}

void delay_us(int us) {
	int i;
    for(i = 0 ; i < us ; i += 1) {
        nop();nop();nop();
        nop();nop();

        nop();nop();nop();
        nop();nop();

        nop();nop();nop();
    }
}


int eth_memcmp(int * a, int * b, int length) {
	int i;
    for(i = 0 ; i < length ; i += 1)
        if(a[i] != b[i])
            return -1;
    return 0;
}

void eth_memcpy(int * dst, int * src, int length) {
	int i;
    for(i = 0 ; i < length ; i += 1)
        dst[i] = LSB(src[i]);
}


int MAC_ADDR[6] = {0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
int ethernet_rx_data[2048];
int ethernet_rx_len;
int ethernet_tx_data[2048];
int ethernet_tx_len;

unsigned int ethernet_read(unsigned int addr) {
    *(unsigned volatile int *)(ENET_IO_ADDR) = addr;
    nop();nop();nop();
    return *(unsigned volatile int *)(ENET_DATA_ADDR);
}

void ethernet_write(unsigned int addr, unsigned int data) {
    *(unsigned volatile int *)(ENET_IO_ADDR) = addr;
    nop();
    *(unsigned volatile int *)(ENET_DATA_ADDR) = data;
    nop();
}

void ethernet_init() {
    cprintf("Network initializing...\n");
    ethernet_powerup();
    cprintf("Power up done.\n");
    ethernet_reset();
    cprintf("reset done.\n");
    ethernet_phy_reset();
    cprintf("phy reset done.\n");

	// auto nego
    ethernet_phy_write ( 4, 0x01E1 | 0x0400 );
    ethernet_phy_write ( 0, 0x1200 );

	// 10M duplex mode
    //ethernet_phy_write ( 4, 0x0041 | 0x0400 );
    //ethernet_phy_write ( 0, 0x0100 );

	// 10M duplex mode
    //ethernet_phy_write ( 4, 0x0021 );
    //ethernet_phy_write ( 0, 0x0000 );

	int i;
    // set MAC address
    for(i = 0 ; i < 6 ; i += 1)
        ethernet_write(DM9000_REG_PAR0 + i, MAC_ADDR[i]);
    // initialize hash table
    for(i = 0 ; i < 8 ; i += 1)
        ethernet_write(DM9000_REG_MAR0 + i, 0x00);
    cprintf(".");
    // accept broadcast
    ethernet_write(DM9000_REG_MAR7, 0x80);
    // enable pointer auto return function
    ethernet_write(DM9000_REG_IMR, IMR_PAR);
    // clear NSR status
    ethernet_write(DM9000_REG_NSR, NSR_WAKEST | NSR_TX2END | NSR_TX1END);
    // clear interrupt flag
    ethernet_write(DM9000_REG_ISR,
        ISR_UDRUN | ISR_ROO | ISR_ROS | ISR_PT | ISR_PR);
    // enable interrupt (recv only)
    ethernet_write(DM9000_REG_IMR, IMR_PAR | IMR_PRI);
    // enable reciever
    ethernet_write(DM9000_REG_RCR,
        RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN);
    // enable checksum calc
    ethernet_write(DM9000_REG_TCSCR,
        TCSCR_IPCSE);
    cprintf("Done\n");
    //!pic_enable(IRQ_ETH);
}

int ethernet_check_iomode() {
    int val = ethernet_read(DM9000_REG_ISR) & ISR_IOMODE;
    if(val) return 8;
    return 16;
}
int ethernet_check_link() {
    return (ethernet_read(0x01) & 0x40) >> 6;
}
int ethernet_check_speed() {
    int val = ethernet_read(0x01) & 0x80;
    if(val == 0) return 100;
    return 10;
}
int ethernet_check_duplex() {
    return (ethernet_read(0x00) & 0x08) >> 3;
}

void ethernet_phy_write(int offset, int value) {
    ethernet_write(DM9000_REG_EPAR, offset | 0x40);
    ethernet_write(DM9000_REG_EPDRH, MSB(value));
    ethernet_write(DM9000_REG_EPDRL, LSB(value));

    ethernet_write(DM9000_REG_EPCR, EPCR_EPOS | EPCR_ERPRW);
    while(ethernet_read(DM9000_REG_EPCR) & EPCR_ERRE);
    delay_us(5);
    ethernet_write(DM9000_REG_EPCR, EPCR_EPOS);
}

int ethernet_phy_read(int offset) {
    ethernet_write(DM9000_REG_EPAR, offset | 0x40);
    ethernet_write(DM9000_REG_EPCR, EPCR_EPOS | EPCR_ERPRR);
    while(ethernet_read(DM9000_REG_EPCR) & EPCR_ERRE);

    ethernet_write(DM9000_REG_EPCR, EPCR_EPOS);
    delay_us(5);
    return (ethernet_read(DM9000_REG_EPDRH) << 8) |
            ethernet_read(DM9000_REG_EPDRL);
}

void ethernet_powerup() {
    ethernet_write(DM9000_REG_GPR, 0x00);
    delay_ms(100);
}

void ethernet_reset() {
    ethernet_write(DM9000_REG_NCR, NCR_RST);
    int r;
    while((r=ethernet_read(DM9000_REG_NCR)) & NCR_RST) {
        cprintf("NCR : %02x\n", r);
    };
}

void ethernet_phy_reset() {
    ethernet_phy_write(DM9000_PHY_REG_BMCR, BMCR_RST);
    while(ethernet_phy_read(DM9000_PHY_REG_BMCR) & BMCR_RST);
}

void print_tx_data() {
	int i;
	cprintf("dump tx data: \n");
	for (i=0; i<ethernet_tx_len; i++) {
		int d = ethernet_tx_data[i];
		cprintf("%02x", d);
		if (i&1) cprintf(" ");
		if ((i&15)==15) cprintf("\n");
	}
	cprintf("\n");
}

int get_send_addr() {
	return ethernet_read(DM9000_REG_MWRL) | (ethernet_read(DM9000_REG_MWRH)<<8);
}
void set_send_addr(int addr) {
	ethernet_write(DM9000_REG_MWRL, addr & 0xff);
	ethernet_write(DM9000_REG_MWRH, (addr>>8) & 0xff);
}

int get_tx_addr() {
	return ethernet_read(DM9000_REG_TRPAL) | (ethernet_read(DM9000_REG_TRPAH)<<8);
}
void set_tx_addr(int addr) {
	ethernet_write(DM9000_REG_TRPAL, addr & 0xff);
	ethernet_write(DM9000_REG_TRPAH, (addr>>8) & 0xff);
}

void ethernet_send() {
	ethernet_tx_data[ethernet_tx_len] = 0;
	//print_tx_data();

	int tmp, t1=0, t2=0;
	/*
	// make sure sending is begin
	while ((tmp=ethernet_read ( 0x01 )) & (4|8)) {
		t1++;
		ethernet_write ( 0x01, (4|8));
	}
	*/
    // int is char
    // A dummy write
	int val=0;
	//set_send_addr(get_tx_addr());
	cprintf("addr before : %04x\n", get_send_addr());
	cprintf("addr tx before : %04x\n", get_tx_addr());
	/*
	cprintf("addr before : %04x\n", get_send_addr());

    *(volatile unsigned int *)(ENET_IO_ADDR) = DM9000_REG_MWCMD;
    nop(); nop();
	for(int i = 0 ; i < ethernet_tx_len ; i += 2){
    	ethernet_write(DM9000_REG_MWCMD, (i<<8) | 0xaa);
	}
	cprintf("addr before : %04x\n", get_send_addr());

	set_send_addr(0);
	cprintf("addr before : %04x\n", get_send_addr());
	*/
    // write length
    ethernet_write(DM9000_REG_TXPLH, MSB(ethernet_tx_len));
    ethernet_write(DM9000_REG_TXPLL, LSB(ethernet_tx_len));

    // select reg
    //*(volatile unsigned int *)(ENET_IO_ADDR) = DM9000_REG_MWCMD;
    //nop(); nop();
	//delay_ms(1);
	int i;
    for(i = 0 ; i < ethernet_tx_len ; i += 2){
        val = ethernet_tx_data[i] |= (ethernet_tx_data[i+1] << 8);
		//XXX if(i + 1 != ethernet_tx_len) val |= (ethernet_tx_data[i+1] << 8);
		//val = val+1;
        //*(unsigned volatile int *)(ENET_DATA_ADDR) = val;
		//nop();
        //nop(); nop(); nop();
		ethernet_write(DM9000_REG_MWCMD, val);
        //nop(); nop(); nop();
		//cprintf("addr : %04x\n", get_send_addr());
    }
	//delay_ms(1);

    // clear interrupt flag
    ethernet_write(DM9000_REG_ISR, ISR_PT);
    // transfer data
    ethernet_write(DM9000_REG_TCR, TCR_TXREQ);

	//cprintf("ethernet_send len : %d\n", ethernet_tx_len);
	/*
	while (ethernet_read(DM9000_REG_TCR) & TCR_TXREQ) {
		t1++;
		nop();
	}
	*/

	while (!((tmp=ethernet_read ( 0x01 )) & (4|8))) t2++;
	cprintf("ethernet_send len : %d NSR : %02x TSR : %02x done\n", ethernet_tx_len, tmp,
		ethernet_read(3));
	//cprintf("ethernet_send len : %d NSR : %02x done\n", ethernet_tx_len, ethernet_read ( 0x01 ));
	//ethernet_write ( 0x01, (4|8));
	//cprintf("ethernet_send len : %d NSR : %02x done\n", ethernet_tx_len, ethernet_read ( 0x01 ));
	cprintf("t12 : %d, %d\n", t1, t2);

	cprintf("addr after : %04x\n", get_send_addr());
	cprintf("addr tx after : %04x\n", get_tx_addr());
}

void ethernet_recv() {
	int isr = ethernet_read(DM9000_REG_ISR);
	if (isr & 1) {
		cprintf("Get RX intr\n");
		//ethernet_write(DM9000_REG_ISR, 1);
	} else {
		//ethernet_rx_len = -1;
		//return;
	}
    // a dummy read
    ethernet_read(DM9000_REG_MRCMDX);
    // select reg
    *(unsigned volatile int *)(ENET_IO_ADDR) = DM9000_REG_MRCMDX1;
    nop(); nop();
    int status = LSB(*(unsigned volatile int *)(ENET_DATA_ADDR));
    if(status != 0x01){
        ethernet_rx_len = -1;
        return;
    }
    *(unsigned volatile int *)(ENET_IO_ADDR) = DM9000_REG_MRCMD;
    nop(); nop();
    status = MSB(*(unsigned volatile int *)(ENET_DATA_ADDR));
    nop(); nop();
    ethernet_rx_len = *(unsigned volatile int *)(ENET_DATA_ADDR);
    nop(); nop();
    if(status & (RSR_LCS | RSR_RWTO | RSR_PLE |
                 RSR_AE | RSR_CE | RSR_FOE)) {
        ethernet_rx_len = -1;
        return;
    }
	int i;
	cprintf("Get data len %d\n", ethernet_rx_len);
    for(i = 0 ; i < ethernet_rx_len ; i += 2) {
        int data = *(unsigned volatile int *)(ENET_DATA_ADDR);
        ethernet_rx_data[i] = LSB(data);
        ethernet_rx_data[i+1] = MSB(data);
    }
    // clear intrrupt
    ethernet_write(DM9000_REG_ISR, ISR_PR);
}

void ethernet_set_tx(int * dst, int type) {
    eth_memcpy(ethernet_tx_data + ETHERNET_DST_MAC, dst, 6);
    eth_memcpy(ethernet_tx_data + ETHERNET_SRC_MAC, MAC_ADDR, 6);
	cprintf("Set dest mac : ");
	for (int i=0; i<6; i++) cprintf("%02x ", dst[i]);
	cprintf("\nSet src mac : ");
	for (int i=0; i<6; i++) cprintf("%02x ", MAC_ADDR[i]);
	cprintf("\n");
    ethernet_tx_data[12] = MSB(type);
    ethernet_tx_data[13] = LSB(type);
}



void print_device_info() {
    int mac[6];
    int multi[8];
    int i;
    cprintf("---------- Device info --------------\n");
    for (i=0; i<6; i++)
        mac[i] = ethernet_read(DM9000_REG_PAR0+i);
    cprintf("MAC : ");
    for (i=0; i<6; i++)
        cprintf("%02x ", mac[i]);
    cprintf("\n");

    for (i=0; i<8; i++)
        multi[i] = ethernet_read(DM9000_REG_MAR0+i);

    cprintf("Multicast : ");
    for (i=0; i<8; i++)
        cprintf("%02x ", multi[i]);
    cprintf("\n");

    int mode = ethernet_read(DM9000_REG_NSR);
    cprintf("NSR : %02x\n", mode);
    cprintf("Link : %d\n", !!(mode & 0x40));
    cprintf("Speed mode : %d\n", !!(mode & 0x80));
    mode = ethernet_read(DM9000_REG_NCR);
    cprintf("NCR : %02x\n", mode);

    cprintf("Duplex : %d\n", !!(mode & 8));

    mode = ethernet_read(DM9000_REG_IMR);
    cprintf("IMR : %02x\n", mode);
    cprintf("BPTR : %02x\n", ethernet_read(DM9000_REG_BPTR));
    cprintf("FCTR : %02x\n", ethernet_read(DM9000_REG_FCTR));
    cprintf("FCR : %02x\n", ethernet_read(DM9000_REG_FCR));
    cprintf("SMCR : %02x\n", ethernet_read(DM9000_REG_SMCR));
    cprintf("GPR : %02x\n", ethernet_read(DM9000_REG_GPR));
    cprintf("VID : %02x\n", ethernet_read(0x28));
}

#include "arp.h"

void ethernet_intr()
{
	int no_pack = 0;
	while(1)
	{
		ethernet_recv();
		if(ethernet_rx_len == -1) {
			no_pack ++;
			if ((no_pack & 0xff) == 0)
				cprintf("No pack %d\n", no_pack);
			if (no_pack > 10000) return;
			delay_ms(1);
			continue;
		}
		no_pack = 0;
		int type = ethernet_rx_type;
		if(type == ETHERNET_TYPE_ARP) {
			cprintf("handle arp\n");
		    arp_handle();
		} else
			cprintf("Unknow package type %d\n", type);
		//if(type == ETHERNET_TYPE_IP)
		//    ip_handle();
	}
}

int from_hex(char *s) {
    int r=0;
    int i=0;
    while (s[i] != '\0') {
        if (s[i] >= '0' && s[i] <= '9')
            r = (r*16) + (s[i]-'0');
        else
        if (s[i] >= 'a' && s[i] <= 'f')
            r = (r*16) + (s[i]-'a') + 10;
        i++;
    }
    return r;
}

int main(int argc, char**argv) {
	((int*)SEG_ADDR)[0] = 0xab;

    cprintf("argc : %d\n", argc);
    for (int i=0; i<argc; i++)
        cprintf("argv[%d] : %s\n", i, argv[i]);

	if (argc == 2 && argv[1][0] == 'r') {
		print_device_info();
		cprintf("Begin listen pack.....\n");
		ethernet_intr();
		print_device_info();
		return 0;
	}

	if (argc == 2 && argv[1][0] == 'i') {
	    print_device_info();
	    ethernet_init();
	    print_device_info();
		return 0;
	}

	if (argc == 2 && argv[1][0] == 'l') {
	    print_device_info();
	    int i=0;
	    while (i<10) {
	        i++;
	        sleep(2);
	        cprintf("%d", ethernet_check_link());
	        //print_device_info();
	    }
	    print_device_info();
		return 0;
	}

    if (argc > 1) {
		print_device_info();
        int reg = from_hex(argv[1]);
        if (argc == 2) {
            cprintf("read reg[0x%02x] = %04x\n", reg, ethernet_read(reg));
        } else {
            int v = from_hex(argv[2]);
            ethernet_write(reg, v);
            cprintf("write reg[0x%02x] = %04x\n", reg, v);
        }
        print_device_info();
        return 0;
    }
	cprintf("no opt\n");
	print_device_info();
    return 0;
}
