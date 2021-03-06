#include <stdlib.h>
#include <stdio.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <uhd/utils/msg.hpp>
#include <uhd/types/time_spec.hpp>
#include <iostream>
#include <fstream>
#include <errno.h>
#include <signal.h>
#include <random>
#include "timer.h"
#include "crts.hpp"
#include "extensible_cognitive_radio.hpp"
#include "tun.hpp"

#define DEBUG 0
#if DEBUG == 1
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) /*__VA_ARGS__*/
#endif

int sig_terminate;
bool start_msg_received = false;
time_t start_time_s = 0;
time_t stop_time_s = 0;
std::ofstream log_rx_fstream;
std::ofstream log_tx_fstream;
float rx_stats_fb_period = 1e4;
timer rx_stat_fb_timer;
long int bytes_sent;
long int bytes_received;

void initialize_node_parameters(struct node_parameters *np);
void Initialize_CR(struct node_parameters *np, void *ECR_p,
                   int argc, char **argv);

void initialize_node_parameters(struct node_parameters *np) {
  // general node parameters
 // np->node_type = "cognitive radio";
  np->cognitive_radio_type = 1;
  strncpy(np->server_ip,"192.168.1.11",sizeof(np->server_ip));


  // network parameters
  strncpy(np->crts_ip,"10.0.0.3",sizeof(np->crts_ip));
  //strncpy(np->target_ip,"10.0.0.2",sizeof(np->target_ip));
  np->net_traffic_type = NET_TRAFFIC_STREAM;
  np->net_mean_throughput = 2e6;

  // cognitive engine parameters
// np->cognitive_engine = "CE_Template";
  strncpy(np->cognitive_engine,"CE_Template",sizeof(np->cognitive_engine));
  np->ce_timeout_ms = 200.0;

  // log/report settings
  np->print_rx_frame_metrics = 0;
  np->log_phy_rx = 1;
  np->log_phy_tx = 1;
  np->log_net_rx = 1;
  np->log_net_tx = 1;
  np->generate_octave_logs = 1;

  // initial USRP settings
  np->tx_freq = 862.5e6;
  np->tx_rate = 2e6;
  np->tx_gain = 15.0;


  np->rx_freq = 857.5e6;
  np->rx_rate = 2e6;
  np->rx_gain = 15.0;

  // initial liquid OFDM settings
  np->tx_gain_soft = -12;
  np->tx_modulation = 25;
  np->tx_crc = 6;
  np->tx_fec0 = 11;
  np->tx_fec1 = 1;
  np->tx_cp_len = 16;
  np->rx_cp_len = 16;
  np->tx_taper_len = 4;


  np->tx_subcarriers = 32;
  np->tx_subcarrier_alloc_method = 2;
  np->tx_guard_subcarriers = 4;
  np->tx_central_nulls = 6;
  np->tx_pilot_freq = 4;

  np->rx_subcarriers = 32;
  np->rx_subcarrier_alloc_method = 2;
  np->rx_guard_subcarriers = 4;
  np->rx_central_nulls = 6;
  np->rx_pilot_freq = 4;
}

void Initialize_CR(struct node_parameters *np, void *ECR_p,
                   int argc, char **argv) {

  // initialize ECR parameters if applicable
  if (np->cognitive_radio_type == EXTENSIBLE_COGNITIVE_RADIO) {
    ExtensibleCognitiveRadio *ECR = (ExtensibleCognitiveRadio *)ECR_p;

    // append relative locations for log files
    char phy_rx_log_file_name[255];
    strcpy(phy_rx_log_file_name, "./logs/bin/");
    strcat(phy_rx_log_file_name, np->phy_rx_log_file);
    strcat(phy_rx_log_file_name, ".log");

    char phy_tx_log_file_name[255];
    strcpy(phy_tx_log_file_name, "./logs/bin/");
    strcat(phy_tx_log_file_name, np->phy_tx_log_file);
    strcat(phy_tx_log_file_name, ".log");
    // set cognitive radio parameters
    ECR->set_ip(np->crts_ip); /*sets ip address of the virtual interface*/
    ECR->print_metrics_flag = np->print_rx_frame_metrics;
    ECR->log_phy_rx_flag = np->log_phy_rx;
    ECR->log_phy_tx_flag = np->log_phy_tx;
    ECR->set_ce_timeout_ms(np->ce_timeout_ms);
    strcpy(ECR->phy_rx_log_file, phy_rx_log_file_name);
    strcpy(ECR->phy_tx_log_file, phy_tx_log_file_name);
    ECR->set_rx_freq(np->rx_freq);
    ECR->set_rx_rate(np->rx_rate);
    ECR->set_rx_gain_uhd(np->rx_gain);
    ECR->set_rx_subcarriers(np->rx_subcarriers);
    ECR->set_rx_cp_len(np->rx_cp_len);
    ECR->set_rx_taper_len(np->rx_taper_len);
    ECR->set_tx_freq(np->tx_freq);
    ECR->set_tx_rate(np->tx_rate);
    ECR->set_tx_gain_soft(np->tx_gain_soft);
    ECR->set_tx_gain_uhd(np->tx_gain);
    ECR->set_tx_subcarriers(np->tx_subcarriers);
    ECR->set_tx_cp_len(np->tx_cp_len);
    ECR->set_tx_taper_len(np->tx_taper_len);
    ECR->set_tx_modulation(np->tx_modulation);
    ECR->set_tx_crc(np->tx_crc);
    ECR->set_tx_fec0(np->tx_fec0);
    ECR->set_tx_fec1(np->tx_fec1);
    ECR->set_rx_stat_tracking(false, 0.0);
    ECR->set_ce(np->cognitive_engine, argc, argv);
    ECR->reset_log_files();
    /*
    // copy subcarrier allocations if other than liquid-dsp default
    if (np->tx_subcarrier_alloc_method == CUSTOM_SUBCARRIER_ALLOC ||
        np->tx_subcarrier_alloc_method == STANDARD_SUBCARRIER_ALLOC) {
      ECR->set_tx_subcarrier_alloc(np->tx_subcarrier_alloc);
    } else {
      ECR->set_tx_subcarrier_alloc(NULL);
    }
    if (np->rx_subcarrier_alloc_method == CUSTOM_SUBCARRIER_ALLOC ||
        np->rx_subcarrier_alloc_method == STANDARD_SUBCARRIER_ALLOC) {
      ECR->set_rx_subcarrier_alloc(np->rx_subcarrier_alloc);
    } else {
      ECR->set_rx_subcarrier_alloc(NULL);
    }
    */
  }

}


void terminate(int signum) {
  printf("\nSending termination message to controller\n");
  sig_terminate = 1;
}


void help_CRTS_CR() {
  printf("CRTS_CR -- Start a cognitive radio node. Only needs to be run "
         "explicitly when using CRTS_controller with -m option.\n");
  printf("        -- This program must be run from the main CRTS directory.\n");
  printf(" -h : Help.\n");
  printf(" -a : IP Address of node running CRTS_controller.\n");
}

int main(int argc, char **argv) {

  std::ofstream writeR("Receive.txt");
  // register signal handlers
  signal(SIGINT, terminate);
  signal(SIGQUIT, terminate);
  signal(SIGTERM, terminate);

  int buffer_length = 1000;
  char message[buffer_length];
  // Port to be used by CRTS server and client
  int port = CRTS_CR_PORT;

  // pointer to ECR which may or may not be used
  ExtensibleCognitiveRadio *ECR = NULL;

  // Create node parameters struct and the scenario parameters struct
  // and read info from controller
  struct node_parameters np;
  memset(&np, 0, sizeof(np));
  initialize_node_parameters(&np);
  //struct scenario_parameters sp;
  printf("np.cognitive_radio_type: %d\n", np.cognitive_radio_type);
  // Create and start the ECR or python CR so that they are in a ready
  // state when the experiment begins
  if(np.cognitive_radio_type == EXTENSIBLE_COGNITIVE_RADIO)
  {
    dprintf("Creating ECR object...\n");

    ECR = new ExtensibleCognitiveRadio;

    // set the USRP's timer to 0
    uhd::time_spec_t t0(0, 0, 1e6);

    ECR->usrp_rx->set_time_now(t0, 0);

    rx_stat_fb_timer = timer_create();

    int argc = 0;
    char ** argv = NULL;
    dprintf("Converting ce_args to argc argv format\n");
    str2argcargv(np.ce_args, np.cognitive_engine, argc, argv);
    dprintf("Initializing CR\n");

    Initialize_CR(&np, (void *)ECR, argc, argv);
    freeargcargv(argc, argv);

  }


  // Define address structure for CRTS socket server used to receive network
  // traffic
  struct sockaddr_in udp_server_addr;
  memset(&udp_server_addr, 0, sizeof(udp_server_addr));
  udp_server_addr.sin_family = AF_INET;
  // Only receive packets addressed to the crts_ip
  udp_server_addr.sin_addr.s_addr = inet_addr(np.crts_ip); //10.0.0.3
  udp_server_addr.sin_port = htons(port);
  socklen_t clientlen = sizeof(udp_server_addr);
  int udp_server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  printf("Creating Sockets\n");
  //system("route -n");
  // Define address structure for CRTS socket client used to send network
  // traffic
  struct sockaddr_in udp_client_addr;
  memset(&udp_client_addr, 0, sizeof(udp_client_addr));
  udp_client_addr.sin_family = AF_INET;

  // Bind CRTS server socket
  bind(udp_server_sock, (sockaddr *)&udp_server_addr, clientlen); //10.0.0.3

  // Define a buffer for receiving and a temporary message for sending
  int recv_buffer_len = 8192 * 2;
  char recv_buffer[recv_buffer_len];

  // initialize sig_terminate flag and check return from socket call
  sig_terminate = 0;

  if (udp_server_sock < 0) {
    printf("CRTS failed to create server socket\n");
    sig_terminate = 1;
  }

  fd_set read_fds;

  bytes_sent = 0;
  bytes_received = 0;

  if (np.cognitive_radio_type == EXTENSIBLE_COGNITIVE_RADIO) {
    // Start ECR
    dprintf("Starting ECR object...\n");
    ECR->start_rx();
    ECR->start_tx();
    ECR->start_ce();
  }

  // read all available data from the UDP socket
  int p = 0;
  int recv_len = 0;
  FD_ZERO(&read_fds);
  FD_SET(udp_server_sock, &read_fds);

  while(sig_terminate == 0){
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    dprintf("Waiting for message\n");
    p = select(udp_server_sock + 1, &read_fds, NULL, NULL, &timeout);
    if(p > 0) {
      dprintf("TUN Interface received bytes\n");
      recv_len = recvfrom(udp_server_sock, recv_buffer, recv_buffer_len, 0,
                            (struct sockaddr *)&udp_client_addr, &clientlen);


      // print out/log details of received messages
      if (recv_len > 0) {
        // TODO: Say what address message was received from.
        // (It's in CRTS_server_addr)
        dprintf("CRTS received packet %i containing %i bytes:\n", rx_packet_num,
                  recv_len);

        strncpy(np.target_ip,inet_ntoa(udp_client_addr.sin_addr),20);
        printf("Message Received from %s: ",np.target_ip);
        bytes_received += recv_len;

        for(int i = 0; i < recv_len; i++) {
          writeR << recv_buffer[i];
          std::cout << recv_buffer[i];
        }
        std::string reply = "Message received";
        strncpy(message,reply.c_str(),reply.length());
        int message_length = reply.length();
        sendto(udp_server_sock,(char *)message, message_length, 0,
                  (struct sockaddr *)&udp_client_addr, sizeof(udp_client_addr));
        printf("\n");

      }

      p = 0;

    } else {
        dprintf("Waiting for message %d\n");

    }
    FD_ZERO(&read_fds);
    FD_SET(udp_server_sock, &read_fds);

  }

  // close all network connections
//  close(udp_client_sock);
  close(udp_server_sock);

  // clean up ECR/python process
  if (np.cognitive_radio_type == EXTENSIBLE_COGNITIVE_RADIO) {
    delete ECR;
  }
  printf("CRTS: Reached termination. Sending termination message to controller\n");

  writeR.close();
}
