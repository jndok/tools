//
//  getpanic.c
//  getpanic
//
//  Created by jndok on 16/02/17.
//  Copyright © 2017 jndok. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <fcntl.h>
#include <unistd.h>

#include <assert.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/afc.h>

void usage(void)
{
    puts("usage: ./getpanic (-u [udid]) (-o [output_path]) (-a) (-c)");
    puts("\t-u: Device UDID (optional)");
    puts("\t-o: Output path (optional, default executable directory)");
    puts("\t-a: Get all panic reports (optional, default only get latest)");
    puts("\t-c: Clear reports on device (optional, default no)");
}

void dump_panic_report(afc_client_t client, char *name, char *path)
{
    assert(client);
    assert(name);
    assert(path);
    
    char **fileinfo = NULL;
    afc_get_file_info(client, name, &fileinfo);
    
    uint64_t panic_report_size = 0;
    
    for (uint32_t i = 0; fileinfo[i]; i+=2) {
        if (strcmp(fileinfo[i], "st_size") == 0) {
            panic_report_size = strtoull(fileinfo[i+1], NULL, 10);
            break;
        }
    }
    
    printf("* Found panic report on device: %s (%lld bytes)\n", name, panic_report_size);
    
    assert(panic_report_size > 0);
    
    void *data = calloc(1, panic_report_size);
    
    uint32_t bytes_read = 0;
    
    uint64_t handle;
    assert(afc_file_open(client, name, AFC_FOPEN_RDONLY, &handle) == AFC_E_SUCCESS);
    assert(afc_file_read(client, handle, data, (uint32_t)panic_report_size, &bytes_read) == AFC_E_SUCCESS);
    
    char dumppath[512] = {0};
    strcpy((char *)&dumppath, path);
    strcat((char *)&dumppath, name);
    
    printf("Dumping to \'%s\'...\n", dumppath);
    
    int fd = open(dumppath, O_RDWR|O_CREAT, 0666);
    write(fd, data, panic_report_size);
    close(fd);
    
    printf("\n");
}

int main(int argc, const char * argv[]) {

    char *udid = NULL;
    char *output_path = "./";
    uint8_t get_all_panic_reports = 0;
    uint8_t clear_reports = 0;
    
    int ch = 0;
    while ((ch = getopt(argc, (char * const *)argv, "u:o:ac")) != -1) {
        switch (ch) {
            case 'u': {
                udid = optarg;
                break;
            }
                
            case 'o': {
                if (access(optarg, R_OK) == -1) {
                    printf("[!] Error: Path \'%s\' cannot be accessed.\n", optarg);
                    return 1;
                }
                
                output_path = optarg;
                break;
            }
                
            case 'a': {
                get_all_panic_reports = 1;
                break;
            }
                
            case 'c': {
                clear_reports = 1;
                break;
            }
                
            default:
                usage();
                break;
        }
    }
    
    if (udid) {
        printf("+ Working with device with UDID: %s\n", udid);
    }
    
    printf("+ Dumping to \'%s\'!\n", output_path);
    
    if (get_all_panic_reports) {
        printf("+ Getting all panic reports from device!\n");
    } else {
        printf("+ Getting only latest panic report from device!\n");
    }
    
    if (clear_reports) {
        printf("+ Clearing reports on device!\n");
    }
    
    printf("\n");
    
    idevice_t device = NULL;
    lockdownd_client_t lockdownd_client = NULL;
    afc_client_t afc_client = NULL;

    idevice_error_t dev_err = IDEVICE_E_SUCCESS;
    lockdownd_error_t lckdwn_err = LOCKDOWN_E_SUCCESS;
    afc_error_t afc_err = AFC_E_SUCCESS;
    
    dev_err = idevice_new(&device, udid);
    assert(dev_err == IDEVICE_E_SUCCESS);
    assert(device);
    
    lckdwn_err = lockdownd_client_new_with_handshake(device, &lockdownd_client, argv[0]);
    assert(lckdwn_err == LOCKDOWN_E_SUCCESS);
    assert(lockdownd_client);
    
    lockdownd_service_descriptor_t service = NULL;
    lckdwn_err = lockdownd_start_service(lockdownd_client, "com.apple.crashreportmover", &service);
    assert(lckdwn_err == LOCKDOWN_E_SUCCESS);
    
    idevice_connection_t conn = NULL;
    dev_err = idevice_connect(device, service->port, &conn);
    assert(dev_err == IDEVICE_E_SUCCESS);
    assert(conn);
    
    char ping[4] = {0};
    uint32_t recvd = 0;
    dev_err = idevice_connection_receive_timeout(conn, (char *)&ping, sizeof(ping), &recvd, 10000);
    
    idevice_disconnect(conn);
    
    assert(dev_err == IDEVICE_E_SUCCESS);
    assert(recvd == sizeof(ping));
    
    lckdwn_err = lockdownd_start_service(lockdownd_client, "com.apple.crashreportcopymobile", &service);
    assert(lckdwn_err == LOCKDOWN_E_SUCCESS);
    assert(service);
    
    afc_err = afc_client_new(device, service, &afc_client);
    assert(afc_err == AFC_E_SUCCESS);
    assert(afc_client);
    
    uint8_t empty = 1;
    
    char **list = NULL;
    afc_err = afc_read_directory(afc_client, ".", &list);
    
    char *latest_name = NULL;
    uint64_t latest_epoch = 0;
    
    while (*list) {
        if (strstr(*list, "panic")) {
            if (empty)
                empty--;
            
            if (get_all_panic_reports) {
                dump_panic_report(afc_client, *list, output_path);
            } else {
                char **fileinfo = NULL;
                afc_get_file_info(afc_client, *list, &fileinfo);
                
                uint64_t epoch = 0;
                
                for (uint32_t i = 0; fileinfo[i]; i+=2)
                    if (strcmp(fileinfo[i], "st_birthtime") == 0)
                        epoch = strtoull(fileinfo[i+1], NULL, 10);
                
                if (epoch > latest_epoch) {
                    latest_name = *list;
                    latest_epoch = epoch;
                }
            }
        }
        
        if (clear_reports) {
            afc_remove_path(afc_client, *list);
        }
        
        list++;
    }
    
    if (!empty) {
        if (!get_all_panic_reports) {
            printf("%s\n", latest_name);
            dump_panic_report(afc_client, latest_name, output_path);
            
            uint32_t bytes_to_show = 2048;
            
            uint64_t handle;
            uint32_t bytes_read = 0;
            void *first_bytes = calloc(1, bytes_to_show);
            assert(afc_file_open(afc_client, latest_name, AFC_FOPEN_RDONLY, &handle) == AFC_E_SUCCESS);
            assert(afc_file_read(afc_client, handle, first_bytes, bytes_to_show, &bytes_read) == AFC_E_SUCCESS);
            assert(bytes_read == bytes_to_show);
            
            printf("** Showing first %d bytes for latest panic **\n%s\n", bytes_to_show, (char *)first_bytes);
            
            afc_file_close(afc_client, handle);
        }
        
    }
    
    return 0;
}
