#ifndef CALC_DATA_H
#define CALC_DATA_H

typedef struct packet {
	char version;
	char userID;
	uint16_t sequence;
	uint16_t length;
	uint16_t command;
	char data[2048];
}PACKET;

#endif
