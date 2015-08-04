/*
* @Author: BlahGeek
* @Date:   2014-06-05
* @Last Modified by:   BlahGeek
* @Last Modified time: 2014-06-09
*/
#include "stdio.h"
#include "tcp.h"
#include "ethernet.h"
#include "ip.h"
#include "defs.h"
#include "utils.h"

#define WINDOW_SIZE 1000
#define INIT_SEQ 1001
#define TIMEOUT 30


int tcp_inited = 0;

#define MYDATA_LENGTH (724/4)
// hard code your http request here.
#define MAX_HTTP_REQUEST_LEN (500/4)
int http_r_len = 0;
char* pagedata = "GET / HTTP/1.0\r\nHost: local_host\r\nUser-Agent: thu_mips\r\n\r\n";
int http_request[MAX_HTTP_REQUEST_LEN];
	// "<!DOCTYPE html>\n"
	// "<html>\n"
	// "	<h1>It works!</h1>\n"
	// "	<p>�������Լ�ԭ������ʵ����������CPU��һ��ŭ�����������ڵ������������������б��ڻ��۳���һ�����������ҹ����ˣ��ҹ����ˣ���</p>\n"
	// "</html>";

int MYDATA[MYDATA_LENGTH * 4];
#define BUF_LENGTH MYDATA_LENGTH*4
#define BUF MYDATA
// int BUF[BUF_LENGTH];
int tcp_recving = 0, tcp_sending = 0;

/*
int MYDATA_LENGTH;
char* truedata = "<div>It works!asdflkajsdlfjasldkfjalksdjf</div>      ";
int* MYDATA;
*/

#define CHUNK_LEN 1000
#define LAST_CHUNK_POS ((MYDATA_LENGTH / CHUNK_LEN) * CHUNK_LEN)

int send_pos = 0;
int send_len = 0;
int last_chunk_pos = 0;

int recv_pos = 0;

int tcp_timeout = 0;

int tcp_src_port, tcp_dst_port;
int tcp_src_addr[4], tcp_dst_addr[4];
int tcp_ack = 0, tcp_seq = INIT_SEQ;
int tcp_state = TCP_CLOSED;


void tcp_handshake(int src_port, int dst_port, int *src_addr, int *dst_addr) {
		if(tcp_inited == 0)
		{
			tcp_inited = 1;
			for(http_r_len = 0; http_r_len < MAX_HTTP_REQUEST_LEN
				&& pagedata[http_r_len] != '\0'; ++http_r_len)
				http_request[http_r_len] = pagedata[http_r_len];
				http_request[http_r_len] = pagedata[http_r_len];
		}
		if (tcp_state != TCP_CLOSED)
			return;
		cprintf("TCP handshake initiated\n");
		tcp_src_port = src_port;
		tcp_dst_port = dst_port;
		eth_memcpy(tcp_src_addr, src_addr, 4);
		eth_memcpy(tcp_dst_addr, dst_addr, 4);
		// send SYN
		tcp_send_packet(TCP_FLAG_SYN, 0, 0);
		tcp_state = TCP_SYNC_SENT;
}

void tcp_handle(int length) {
	if(tcp_inited == 0)
	{
		tcp_inited = 1;
	}
    int * data = ethernet_rx_data + ETHERNET_HDR_LEN + IP_HDR_LEN;
    // writeint(tcp_state);
    if(tcp_state != TCP_CLOSED) tcp_timeout += 1;
    else tcp_timeout = 0;
    if(tcp_timeout == TIMEOUT) {
        tcp_timeout = 0;
        tcp_state = TCP_CLOSED;
    }
    if((data[TCP_FLAGS] & TCP_FLAG_SYN) &&
		(tcp_state == TCP_CLOSED || tcp_state == 0)) {
        tcp_src_port = mem2int(data + TCP_DST_PORT, 2);
        tcp_dst_port = mem2int(data + TCP_SRC_PORT, 2);
        eth_memcpy(tcp_src_addr, data - IP_HDR_LEN + IP_DST, 4);
        eth_memcpy(tcp_dst_addr, data - IP_HDR_LEN + IP_SRC, 4);
        tcp_ack = mem2int(data + TCP_SEQ, 4) + 1;
        tcp_seq = INIT_SEQ;
        tcp_state = TCP_SYNC_RECVED;
        send_pos = 0;
        tcp_send_packet(TCP_FLAG_SYN | TCP_FLAG_ACK,
                        0, 0);
        return;
    }
    // not closed, check port & addr
    if(tcp_src_port != mem2int(data + TCP_DST_PORT, 2)
        || tcp_dst_port != mem2int(data + TCP_SRC_PORT, 2)
        || eth_memcmp(data - IP_HDR_LEN + IP_DST, tcp_src_addr, 4) != 0
        || eth_memcmp(data - IP_HDR_LEN + IP_SRC, tcp_dst_addr, 4) != 0) {
        return;
    }
		if ((data[TCP_FLAGS] & TCP_FLAG_SYN) &&
		(data[TCP_FLAGS] & TCP_FLAG_ACK) && (tcp_state == TCP_SYNC_SENT)) {
			tcp_seq = mem2int(data+TCP_ACK, 4);
			tcp_ack = mem2int(data+TCP_SEQ, 4) + 1;
			// send out ACK
			tcp_send_packet(TCP_FLAG_ACK, 0, 0);
			tcp_state = TCP_ESTABLISHED;
			cprintf("TCP handshake complete\n");
			// send out http request
			tcp_send_packet(TCP_FLAG_PSH, http_request, http_r_len);
			tcp_recving = 1;
			return;
		}
    if(data[TCP_FLAGS] & TCP_FLAG_RST) {
        tcp_state = TCP_CLOSED;
        return;
    }
    if(tcp_state == TCP_FIN_SENT) {
        tcp_seq = mem2int(data + TCP_ACK, 4);
        tcp_send_packet(TCP_FLAG_RST, 0, 0);
        tcp_state = TCP_CLOSED;
        return;
    }
    if(tcp_state == TCP_SYNC_RECVED &&
        (data[TCP_FLAGS] & TCP_FLAG_ACK)) {
        tcp_seq = mem2int(data + TCP_ACK, 4);
        tcp_ack = mem2int(data + TCP_SEQ, 4) + 1;
        tcp_state = TCP_ESTABLISHED;
        return;
    }
    if(tcp_state == TCP_ESTABLISHED) {
			int tcphdrlen = 4*(int)(data[TCP_DATA_OFFSET]>>4);
			int datalen = length - tcphdrlen;

			// cprintf("tcp: datalen: %d, tcphdrlen: %d", datalen, tcphdrlen);

			if (tcp_recving && (data[TCP_FLAGS] & TCP_FLAG_PSH)) {
				if (recv_pos+datalen > BUF_LENGTH) {
					cprintf("tcp_handle: recving buffer overflow\n");
					return;
				}
				if (datalen>0) eth_memcpy(BUF+recv_pos,
					data+tcphdrlen, datalen);
					recv_pos += datalen;
					cprintf("http recved: \n");
					for (int i = 0; i < datalen;++i)
						cprintf("%c", (char)*(data+tcphdrlen+i));
					cprintf("\n");
        tcp_ack = mem2int(data + TCP_SEQ, 4) + datalen;
				tcp_send_packet(TCP_FLAG_ACK, 0, 0);

			// if (tcp_sending && (data[TCP_FLAGS] & TCP_FLAG_ACK)) {
      //   tcp_seq = mem2int(data + TCP_ACK, 4);
      //   // int pos = tcp_seq - (INIT_SEQ + 1);
			// 	int pos = send_pos;
      //   if(pos == 0 && length == TCP_HDR_LEN) return;
      //   if(pos == send_len) {
      //       // tcp_send_packet(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
      //       // tcp_state = TCP_FIN_SENT;
			// 			tcp_sending = 0;
			// 			tcp_recving = 1;
      //       return;
      //   }
      //   int len = CHUNK_LEN;
      //   if(pos == last_chunk_pos)
      //       len = send_len - pos;
      //   int flag = 0;
      //   if(pos != 0) flag |= TCP_FLAG_PSH;
      //   tcp_send_packet(flag, http_request + pos, len);
			// 	send_pos += len;
        // tcp_seq += 3;
        // tcp_send_packet(TCP_FLAG_RST, 0, 0);
        // tcp_state = TCP_CLOSED;
        return;
			}
    }
}

// length is the len(data)
void tcp_send_packet(int flags, int * data, int length) {
    int * packet = ethernet_tx_data + ETHERNET_HDR_LEN + IP_HDR_LEN;
    int2mem(packet + TCP_SRC_PORT, 2, tcp_src_port);
    int2mem(packet + TCP_DST_PORT, 2, tcp_dst_port);
    int2mem(packet + TCP_SEQ, 4, tcp_seq);
    int2mem(packet + TCP_ACK, 4, tcp_ack);
    packet[TCP_DATA_OFFSET] = 0x50;
    packet[TCP_FLAGS] = flags;
    packet[TCP_URGEN] = 0;
    packet[TCP_URGEN + 1] = 0;
    packet[TCP_CHECKSUM] = 0;
    packet[TCP_CHECKSUM + 1] = 0;
    int2mem(packet + TCP_WINDOW, 2, 1000);
    eth_memcpy(packet + TCP_DATA, data, length);
    // calc checksum
    int sum = 0;
    sum += mem2int(tcp_src_addr, 2) + mem2int(tcp_src_addr + 2, 2);
    sum += mem2int(tcp_dst_addr, 2) + mem2int(tcp_dst_addr + 2, 2);
    sum += IP_PROTOCAL_TCP;
    length += TCP_HDR_LEN;
    sum += length;
		int i;
    for(i = 0 ; i < length ; i += 2) {
        int val = (packet[i] << 8);
        if(i + 1 != length) val |= packet[i+1];
        sum += val;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum = (sum >> 16) + (sum & 0xffff);
    sum = ~sum;
    packet[TCP_CHECKSUM] = MSB(sum);
    packet[TCP_CHECKSUM + 1] = LSB(sum);
    ip_make_reply(IP_PROTOCAL_TCP, length);
    ethernet_tx_len = ETHERNET_HDR_LEN + IP_HDR_LEN + length;
    ethernet_send();
}
