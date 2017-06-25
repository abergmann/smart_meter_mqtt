#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <mosquitto.h>

#include <sml/sml_file.h>
#include <sml/sml_transport.h>


struct mosquitto *mosq = NULL;
char *sm_counter_topic = "smart_meter/main/counter";
char *sm_current_topic = "smart_meter/main/current";

void print_help() {
	fprintf(stderr, "Usage is: smart_meter_mqtt [options]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Options are:\n");
	fprintf(stderr, "  -d <device>  USB tty device.\n");
	fprintf(stderr, "  -h <host>    MQTT Host. (Default: 127.0.0.1)\n");
	fprintf(stderr, "  -p <port>    MQTT Port. (Default: 1883\n");
	fprintf(stderr, "  -x <topic>   Smart meter power counter MQTT topic.\n");
	fprintf(stderr, "               (Default: smart_meter/main/counter)\n");
	fprintf(stderr, "  -y <topic>   Smart meter current power usage MQTT topic.\n");
	fprintf(stderr, "               (Default: smart_meter/main/current)\n");
	fprintf(stderr, "  -h           This help.\n");
}

int serial_port_open(const char* device) {
	int bits;
	struct termios config;
	memset(&config, 0, sizeof(config));

	int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) {
		printf("error: open(%s): %s\n", device, strerror(errno));
		return -1;
	}

	// set RTS
	ioctl(fd, TIOCMGET, &bits);
	bits |= TIOCM_RTS;
	ioctl(fd, TIOCMSET, &bits);

	tcgetattr( fd, &config );

	// set 8-N-1
	config.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	config.c_oflag &= ~OPOST;
	config.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	config.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
	config.c_cflag |= CS8;

	// set speed to 9600 baud
	cfsetispeed( &config, B9600);
	cfsetospeed( &config, B9600);

	tcsetattr(fd, TCSANOW, &config);
	return fd;
}

void transport_receiver(unsigned char *buffer, size_t buffer_len) {
	int i;
	double value;
	char msg[20];

	sml_file *file = sml_file_parse(buffer + 8, buffer_len - 16);
	// Access the relevant Smart Meter values //
	for (i = 0; i < file->messages_len; i++) {
		sml_message *message = file->messages[i];
		if (*message->message_body->tag == SML_MESSAGE_GET_LIST_RESPONSE) {
			sml_list *entry;
			sml_get_list_response *body;
			body = (sml_get_list_response *) message->message_body->data;
			for (entry = body->val_list; entry != NULL; entry = entry->next) {
				switch (entry->value->type) {
					case 0x51: value = *entry->value->data.int8; break;
					case 0x52: value = *entry->value->data.int16; break;
					case 0x54: value = *entry->value->data.int32; break;
					case 0x58: value = *entry->value->data.int64; break;
					case 0x61: value = *entry->value->data.uint8; break;
					case 0x62: value = *entry->value->data.uint16; break;
					case 0x64: value = *entry->value->data.uint32; break;
					case 0x68: value = *entry->value->data.uint64; break;
					default: value = 0;
				}
				int scaler = (entry->scaler) ? *entry->scaler : 1;
				if (scaler==-1)
					value *= 0.0001;
				if (value) {
					// Check for object 1.8.0; smart meter conter //
					if (entry->obj_name->str[2] == 1 
							&& entry->obj_name->str[3] == 8
							&& entry->obj_name->str[4] == 0) {
						sprintf (msg, "%.4f", value);
						mosquitto_publish (mosq, NULL, sm_counter_topic, strlen (msg), msg, 0, false);
					}
					// Check for object 16.7.0; smart meter current power usage //
					if (entry->obj_name->str[2] == 16
							&& entry->obj_name->str[3] == 7
							&& entry->obj_name->str[4] == 0) {
						sprintf(msg, "%.0f", value);
						mosquitto_publish (mosq, NULL, sm_current_topic, strlen (msg), msg, 0, false);
					}
				}
			}
			sml_file_free(file);
			return;
		}
	}
}

int main(int argc, char **argv) {
	char *device = NULL;
	char *host = "127.0.0.1";
	int port = 1883;
	int c;

	while ((c = getopt (argc, argv, "d:H:p:x:y:h")) != -1) {
		switch (c) {
			case 'd':
				device = optarg;
				break;
			case 'H':
				host = optarg;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'x':
				sm_counter_topic = optarg;
				break;
			case 'y':
				sm_current_topic = optarg;
				break;
			case 'h':
				print_help();
				return 1;
			case '?':
				if (optopt == 'd')
					fprintf(stderr, "Option -%c requires a USB tty device as argument.\n", optopt);
				else if (optopt == 'H')
					fprintf(stderr, "Option -%c requires a hostname/ip address as argument.\n", optopt);
				else if (optopt == 'p')
					fprintf(stderr, "Option -%c requires a port number as argument.\n", optopt);
				else if (optopt == 'x')
					fprintf(stderr, "Option -%c requires a smart meter MQTT topic string as argument.\n", optopt);
				else if (optopt == 'y')
					fprintf(stderr, "Option -%c requires a smart meter MQTT topic string as argument.\n", optopt);
				else if (isprint (optopt))
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf(stderr,
							"Unknown option character `\\x%x'.\n",
							optopt);
				return 1;
			default:
				abort();
		}
	}
	
	if(device == NULL) {
		fprintf(stderr, "Required option -d <tty device> is missing.\n");
		return 1;
	}

	fprintf (stderr, "OUT: %s\n", device);
	fprintf (stderr, "HOST: %s\n", host);
	fprintf (stderr, "PORT: %d\n", port);
	fprintf (stderr, "COUNTER: %s\n", sm_counter_topic);
	fprintf (stderr, "CURRENT: %s\n", sm_current_topic);
	// Initilize Mosquitto library
	mosquitto_lib_init();
	// Create new Mosquitto object
	mosq = mosquitto_new (NULL, true, NULL);
	if (!mosq) {
		fprintf (stderr, "Can't initialize Mosquitto library\n");
		exit (-1);
	}
	// Connect to MQTT host
	int ret = mosquitto_connect (mosq, host, port, 0);
	if (ret) {
		fprintf (stderr, "Can't connect to Mosquitto server\n");
		exit (-1);
	}
	int fd = serial_port_open(device);
	if (fd > 0) {
		// listen on the serial device, this call is blocking.
		sml_transport_listen(fd, &transport_receiver);
		printf("test\n");
		close(fd);
	}

	return 0;
}
